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

// ---- Display smoothing ----
// Moving average over MA_N samples (~1.6 s at 10 SPS), then round the printed
// weight to DISP_ROUND_G grams. Smooths out the slow thermal wobble (~33 g) so
// the live readout looks clean. The RAW value is still used for tare / cal /
// auto-zero so those stay responsive.
const byte  MA_N         = 16;
const float DISP_ROUND_G = 10.0;
float maBuf[MA_N];
byte  maIdx   = 0;
byte  maCount = 0;
float maSum   = 0;

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

    // ---- MOVING AVERAGE (display only) ----
    if (maCount == MA_N) maSum -= maBuf[maIdx];
    maBuf[maIdx] = v;
    maSum += v;
    maIdx = (maIdx + 1) % MA_N;
    if (maCount < MA_N) maCount++;
    float vSmooth = maSum / maCount;

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

    // ---- STREAM ----
    if (millis() - lastPrint > PRINT_INTERVAL) {
      if (!postCal) {
        // pre-cal: print the RAW value so we can see when it's steady to tare
        Serial.print(F("  live = "));
        Serial.println(v, 2);
      } else {
        // post-cal: print the SMOOTHED + rounded weight for a clean readout
        float disp = roundf(vSmooth / DISP_ROUND_G) * DISP_ROUND_G;
        Serial.print(F("  weight = "));
        Serial.print(disp, 0);
        Serial.println(F(" g"));
      }
      lastPrint = millis();
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
  Serial.println(F("Live weight streams continuously now. Remove the weight - should go to ~0."));
  Serial.println();
  postCal = true;
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
