/*
 * BenchCalLib  -  per-cell bench calibration using HX711_ADC library
 * ------------------------------------------------------------------
 * Same idea as the example by Olav Kallhovd, but extended to walk through
 * multiple cells one after another so you can compare them.
 *
 * Library: "HX711_ADC" by Olav Kallhovd (install in Library Manager if missing).
 *
 * WIRING (ONE HX711):
 *   HX711 DT  -> D4
 *   HX711 SCK -> D5
 *   HX711 VCC -> 5V
 *   HX711 GND -> GND
 *   Load cell -> HX711: Red=E+ Black=E- Green=A+ White=A- Yellow->GND
 *
 * LOAD CELL SPECS (from datasheet):
 *   - Rated capacity:        100 kg
 *   - Best accuracy range:   20-80 kg
 *   - Sensitivity:           2.0 mV/V (+/- 0.2)
 *   - Input impedance:       350 +/- 6 ohm
 *   - Output impedance:      405 +/- 6 ohm
 *   - Nonlinearity:          0.02 % F.S
 *   - Hysteresis:            0.02 % F.S
 *   - Comprehensive error:   +/- 0.03 % F.S
 *   - Safety overload:       120 %  (120 kg - do NOT exceed in use)
 *   - Limit overload:        150 %  (150 kg - PERMANENT DAMAGE above this)
 *   - Operating temp:        -10 C to +40 C
 *   - Expected: ~42000 counts/kg with HX711 at gain 128, AVDD ~5 V.
 *
 * Serial: 57600 baud, line ending "Newline".
 *
 * FLOW:
 *   On boot it starts calibrating Cell #1. It walks you through it:
 *     1) Set up the cell, empty.
 *     2) When the live value is steady, send 't' -> tare.
 *     3) Put the known weight on, send the weight value in g (e.g. 7500 for 7.5 kg).
 *     4) It prints and remembers the calibration factor for this cell.
 *   After Cell #1 it stays live (you can see grams). To do the next cell:
 *     - send 'r'  -> calibrate the NEXT cell (cell #2, then #3, ...)
 *     - send 's'  -> summary of all cells you have calibrated so far
 *     - send 't'  -> re-tare current cell
 *     - send 'c'  -> manually edit the current cal factor
 * ------------------------------------------------------------------
 */

#include <HX711_ADC.h>

const int HX711_dout = 4;
const int HX711_sck  = 5;

HX711_ADC LoadCell(HX711_dout, HX711_sck);

const byte MAX_CELLS = 8;
float cellFactors[MAX_CELLS];
byte  cellCount = 0;

unsigned long lastPrint = 0;
const unsigned long PRINT_INTERVAL = 250;  // ms between live prints

bool postCal = false;          // true after first successful calibration

// ---- Event-driven scale: one stable "Weight: X g" line per measurement ----
// State machine: EMPTY -> LOADING (something put on) -> WEIGHED (locked stable value)
// -> CHANGING (load shifted) -> back to WEIGHED or EMPTY.
enum ScaleState { S_EMPTY, S_LOADING, S_WEIGHED, S_CHANGING };
ScaleState scaleState = S_EMPTY;
float lockedWeight = 0;

const float ONSET_G       = 80.0;   // something present if |avg| > this
const float CLEAR_G       = 30.0;   // back to "empty" if |avg| < this
const float STABLE_BAND_G = 25.0;   // range over WINDOW samples to declare stable
const float RECHECK_G     = 40.0;   // re-measure if avg differs this much from locked
const byte  WINDOW        = 8;
float winBuf[WINDOW];
byte  winIdx   = 0;
byte  winCount = 0;

// ---- Auto-zero (drift compensation) ----
// While the value sits within +/- AZ_BAND_G grams for AZ_STABLE_MS milliseconds,
// the sketch re-tares automatically. This kills the slow electrical/thermal drift
// you see when empty. Toggle with command 'a'.
bool         azOn        = true;
const float  AZ_BAND_G   = 50.0;     // re-zero only if |value| < 50 g
const unsigned long AZ_STABLE_MS   = 4000;   // must stay in band this long
const unsigned long AZ_MIN_GAP_MS  = 6000;   // min time between auto-tares
unsigned long azInBandSince = 0;
unsigned long lastAutoZero  = 0;

void setup() {
  Serial.begin(57600);
  delay(10);
  Serial.println();
  Serial.println(F("=== BenchCalLib (HX711_ADC) ==="));
  Serial.println(F("Wiring: DT=D4, SCK=D5"));

  LoadCell.begin();
  LoadCell.setReverseOutput();   // your cell reads negative when loaded -> flip it in software

  unsigned long stabilizingTime = 5000;   // longer settle so the initial tare isn't taken on a moving signal
  boolean _tare = true;
  LoadCell.start(stabilizingTime, _tare);
  if (LoadCell.getTareTimeoutFlag() || LoadCell.getSignalTimeoutFlag()) {
    Serial.println(F("Timeout - check HX711 wiring. Halting."));
    while (1);
  }
  LoadCell.setCalFactor(1.0);
  Serial.println(F("Startup OK."));
  while (!LoadCell.update());
  calibrate();
}

void loop() {
  static boolean newDataReady = false;
  if (LoadCell.update()) newDataReady = true;

  if (newDataReady) {
    float v  = LoadCell.getData();
    float av = (v < 0) ? -v : v;

    // ---- AUTO-ZERO TRACKING ----
    // While the platform is empty (|v| < AZ_BAND_G) and has been for AZ_STABLE_MS,
    // re-tare automatically. This kills slow drift while idle.
    if (postCal && azOn) {
      if (av < AZ_BAND_G) {
        if (azInBandSince == 0) azInBandSince = millis();
        if (millis() - azInBandSince > AZ_STABLE_MS
            && millis() - lastAutoZero > AZ_MIN_GAP_MS) {
          LoadCell.tareNoDelay();
          lastAutoZero  = millis();
          azInBandSince = 0;
          Serial.println(F("  [auto-zero]"));
        }
      } else {
        azInBandSince = 0;
      }
    }

    // ---- STABILITY WINDOW ----
    winBuf[winIdx] = v;
    winIdx = (winIdx + 1) % WINDOW;
    if (winCount < WINDOW) winCount++;
    float avg = v;
    bool stable = false;
    if (winCount == WINDOW) {
      float mn = winBuf[0], mx = winBuf[0], sum = 0;
      for (byte i = 0; i < WINDOW; i++) {
        if (winBuf[i] < mn) mn = winBuf[i];
        if (winBuf[i] > mx) mx = winBuf[i];
        sum += winBuf[i];
      }
      avg = sum / WINDOW;
      stable = (mx - mn) < STABLE_BAND_G;
    }
    float aAvg = avg < 0 ? -avg : avg;

    // ---- PRINT ----
    if (!postCal) {
      // pre-cal: stream live raw readings so user can see settling
      if (millis() - lastPrint > PRINT_INTERVAL) {
        Serial.print(F("  live = "));
        Serial.println(v, 2);
        lastPrint = millis();
      }
    } else {
      // post-cal: print one stable line per measurement (event-driven)
      switch (scaleState) {
        case S_EMPTY:
          if (aAvg > ONSET_G) { scaleState = S_LOADING; Serial.println(F("  ...settling...")); }
          break;
        case S_LOADING:
          if (stable) {
            lockedWeight = avg;
            Serial.print(F("  Weight: ")); Serial.print(avg, 1); Serial.println(F(" g"));
            scaleState = S_WEIGHED;
          }
          break;
        case S_WEIGHED:
          if (aAvg < CLEAR_G) {
            Serial.println(F("  Cleared. 0 g"));
            scaleState = S_EMPTY;
          } else if (fabs(avg - lockedWeight) > RECHECK_G) {
            scaleState = S_CHANGING;
            Serial.println(F("  ...changing..."));
          }
          break;
        case S_CHANGING:
          if (aAvg < CLEAR_G) {
            Serial.println(F("  Cleared. 0 g"));
            scaleState = S_EMPTY;
          } else if (stable) {
            lockedWeight = avg;
            Serial.print(F("  Weight: ")); Serial.print(avg, 1); Serial.println(F(" g"));
            scaleState = S_WEIGHED;
          }
          break;
      }
    }
    newDataReady = false;
  }

  if (Serial.available() > 0) {
    char c = Serial.read();
    if      (c == 't') LoadCell.tareNoDelay();
    else if (c == 'r') calibrate();
    else if (c == 's') summary();
    else if (c == 'c') changeCalFactor();
    else if (c == 'a') {
      azOn = !azOn;
      Serial.print(F("Auto-zero "));
      Serial.println(azOn ? F("ON") : F("OFF"));
      azInBandSince = 0;
    }
  }

  if (LoadCell.getTareStatus() == true) {
    Serial.println(F("Tare complete"));
  }
}

void calibrate() {
  postCal = false;             // show live stream during cal
  scaleState = S_EMPTY;
  winCount = 0; winIdx = 0;
  Serial.println(F("***"));
  Serial.print(F("CALIBRATING Cell #")); Serial.println(cellCount + 1);
  Serial.println(F("1) Clamp the FIXED end of the cell; nothing on the LOAD end."));
  Serial.println(F("2) Watch 'live' below. When it is STEADY, send  't'  to tare."));

  boolean done = false;
  while (!done) {
    LoadCell.update();
    if (millis() - lastPrint > PRINT_INTERVAL) {
      float v = LoadCell.getData();
      Serial.print(F("  live = ")); Serial.println(v, 2);
      lastPrint = millis();
    }
    if (Serial.available() > 0) {
      char c = Serial.read();
      if (c == 't') LoadCell.tareNoDelay();
    }
    if (LoadCell.getTareStatus() == true) {
      Serial.println(F("Tare complete."));
      done = true;
    }
  }

  Serial.println();
  Serial.println(F("3) Put your known weight on the LOAD end of the cell."));
  Serial.println(F("4) Send the weight in GRAMS (e.g.  7500  for 7.5 kg, + Enter)."));

  float knownMass = 0;
  done = false;
  while (!done) {
    LoadCell.update();
    if (millis() - lastPrint > PRINT_INTERVAL) {
      float v = LoadCell.getData();
      Serial.print(F("  live = ")); Serial.println(v, 2);
      lastPrint = millis();
    }
    if (Serial.available() > 0) {
      knownMass = Serial.parseFloat();
      if (knownMass != 0) {
        Serial.print(F("Known mass = ")); Serial.print(knownMass); Serial.println(F(" g"));
        done = true;
      }
    }
  }

  LoadCell.refreshDataSet();
  float newCal = LoadCell.getNewCalibration(knownMass);
  Serial.print(F("New cal factor = ")); Serial.println(newCal);

  if (cellCount < MAX_CELLS) {
    cellFactors[cellCount] = newCal;
    cellCount++;
  }
  Serial.print(F("Recorded as Cell #")); Serial.println(cellCount);
  Serial.println(F("***"));
  Serial.println(F("Live value now shows grams. Remove the weight - should go to ~0."));
  Serial.println(F("Next:  r = calibrate next cell  |  s = show summary  |  t = re-tare  |  c = edit factor  |  a = auto-zero on/off"));
  Serial.println(F("Place something on the cell - I print one  Weight: X g  line once it settles."));
  Serial.println(F("Remove it -> 'Cleared. 0 g'. Auto-zero is ON."));
  Serial.println();
  postCal = true;
  scaleState = S_EMPTY;
  winCount = 0; winIdx = 0;
}

void summary() {
  Serial.println();
  Serial.println(F("=== SUMMARY ==="));
  if (cellCount == 0) { Serial.println(F("No cells calibrated yet.")); return; }
  float sum = 0;
  for (byte i = 0; i < cellCount; i++) sum += cellFactors[i];
  float avg = sum / cellCount;
  Serial.print(F("Average cal factor: ")); Serial.println(avg);
  Serial.println();
  for (byte i = 0; i < cellCount; i++) {
    Serial.print(F("Cell #")); Serial.print(i + 1);
    Serial.print(F("  factor = ")); Serial.print(cellFactors[i]);
    if (avg != 0) {
      float diff = cellFactors[i] - avg;
      float ad   = diff < 0 ? -diff : diff;
      int   pct  = (int)(ad * 100.0 / avg);
      Serial.print(F("   ("));
      Serial.print(diff < 0 ? '-' : '+');
      Serial.print(pct);
      Serial.print(F("%)"));
      if (pct > 15) Serial.print(F("  <-- OUTLIER (likely damaged)"));
    }
    Serial.println();
  }
  Serial.println();
}

void changeCalFactor() {
  float oldCal = LoadCell.getCalFactor();
  Serial.println(F("***"));
  Serial.print(F("Current cal factor: ")); Serial.println(oldCal);
  Serial.println(F("Type the new value + Enter:"));
  boolean done = false;
  float newCal;
  while (!done) {
    LoadCell.update();
    if (Serial.available() > 0) {
      newCal = Serial.parseFloat();
      if (newCal != 0) {
        LoadCell.setCalFactor(newCal);
        Serial.print(F("Cal factor set to: ")); Serial.println(newCal);
        done = true;
      }
    }
  }
  Serial.println(F("***"));
}
