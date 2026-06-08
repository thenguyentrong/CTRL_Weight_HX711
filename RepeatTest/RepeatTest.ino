/*
 * RepeatTest  -  single-cell repeatability tester
 * ------------------------------------------------
 * Goal: prove that the same object placed multiple times reads the SAME value.
 *
 * Flow:
 *   1) Boot, settle 5 s, tare.
 *   2) Place the 5 kg dumbbell (or any known weight) on the cell.
 *      Type its weight in GRAMS + Enter  (e.g.  5000)  -> sets the cal factor.
 *   3) Remove the calibration weight - reading goes back to ~0.
 *   4) Place ANY object. When the reading is stable for 1.5 s, the sketch
 *      auto-captures it and prints e.g.   "MEASUREMENT #1 = 1150 g".
 *   5) Remove it. Place it (or anything) again -> "MEASUREMENT #2 = ..."
 *   6) After each capture it prints a SUMMARY of all readings so far:
 *      list, min, max, spread.
 *
 * Wiring (same as BenchCalLib):
 *   HX711 DT  -> D4
 *   HX711 SCK -> D5
 *   HX711 VCC -> 5V
 *   HX711 GND -> GND
 *   Cell: Red=E+  Black=E-  Green=A+  White=A-  Yellow->GND
 *
 * Serial: 57600 baud, line ending "Newline".
 *
 * Commands while running:
 *   t  re-tare (platform must be empty)
 *   r  re-do calibration
 *   s  show the current measurement list + stats
 *   c  clear the measurement list
 * ------------------------------------------------
 */

#include <HX711_ADC.h>

const int HX711_dout = 4;
const int HX711_sck  = 5;
HX711_ADC LoadCell(HX711_dout, HX711_sck);

// ---- Display smoothing ----
const byte  MA_N         = 16;     // ~1.6 s at 10 SPS
const float DISP_ROUND_G = 10.0;   // round shown weight to nearest 10 g
float maBuf[MA_N];
byte  maIdx   = 0;
byte  maCount = 0;
float maSum   = 0;

// ---- State machine ----
enum State { S_WAITING, S_LOADING, S_WEIGHED };
State state = S_WAITING;

const float LOAD_THRESHOLD_G   = 100.0;   // above this = something is on the cell
const float UNLOAD_THRESHOLD_G = 50.0;    // below this = it's been removed
const float STABLE_BAND_G      = 10.0;    // must stay inside this band
const unsigned long STABLE_TIME_MS = 1500;
float stableRef       = 0;
unsigned long stableSince = 0;

// ---- Measurement log ----
const byte MAX_REC = 30;
float records[MAX_REC];
byte  recCount = 0;

// ---- Streaming print throttle ----
unsigned long lastPrint = 0;
const unsigned long PRINT_INTERVAL = 500;

bool postCal = false;

void setup() {
  Serial.begin(57600);
  delay(10);
  Serial.println();
  Serial.println(F("=== RepeatTest ==="));
  Serial.println(F("Wiring: DT=D4, SCK=D5"));

  LoadCell.begin();
  LoadCell.setReverseOutput();

  unsigned long stabilizingTime = 5000;
  boolean _tare = true;
  LoadCell.start(stabilizingTime, _tare);
  if (LoadCell.getTareTimeoutFlag() || LoadCell.getSignalTimeoutFlag()) {
    Serial.println(F("Timeout - check HX711 wiring. Halting."));
    while (1);
  }
  LoadCell.setCalFactor(1.0);
  while (!LoadCell.update());
  Serial.println(F("Startup OK."));
  calibrate();
}

void loop() {
  static boolean newDataReady = false;
  if (LoadCell.update()) newDataReady = true;

  if (newDataReady) {
    float v = LoadCell.getData();

    // moving average
    if (maCount == MA_N) maSum -= maBuf[maIdx];
    maBuf[maIdx] = v;
    maSum += v;
    maIdx = (maIdx + 1) % MA_N;
    if (maCount < MA_N) maCount++;
    float vSmooth = maSum / maCount;

    if (postCal) runStateMachine(vSmooth);
    streamLive(v, vSmooth);

    newDataReady = false;
  }

  if (Serial.available() > 0) {
    char c = Serial.read();
    if      (c == 't') { LoadCell.tareNoDelay(); Serial.println(F("Tare requested.")); }
    else if (c == 'r') calibrate();
    else if (c == 's') summary();
    else if (c == 'c') { recCount = 0; Serial.println(F("Records cleared.")); }
  }

  if (LoadCell.getTareStatus() == true) {
    Serial.println(F("Tare complete."));
  }
}

// ---------- state machine ----------
void runStateMachine(float vSmooth) {
  unsigned long now = millis();
  float av = vSmooth < 0 ? -vSmooth : vSmooth;

  switch (state) {
    case S_WAITING:
      if (av > LOAD_THRESHOLD_G) {
        state = S_LOADING;
        stableRef   = vSmooth;
        stableSince = now;
        Serial.println(F(">> something placed - waiting for stable..."));
      }
      break;

    case S_LOADING: {
      float d = vSmooth - stableRef;
      if (d < 0) d = -d;
      if (d > STABLE_BAND_G) {
        stableRef   = vSmooth;
        stableSince = now;
      } else if (now - stableSince > STABLE_TIME_MS) {
        float disp = roundf(vSmooth / DISP_ROUND_G) * DISP_ROUND_G;
        logRecord(disp);
        state = S_WEIGHED;
      }
      break;
    }

    case S_WEIGHED:
      if (av < UNLOAD_THRESHOLD_G) {
        state = S_WAITING;
        Serial.println(F(">> cleared - ready for next."));
        Serial.println();
      }
      break;
  }
}

void logRecord(float disp) {
  if (recCount < MAX_REC) {
    records[recCount++] = disp;
  }
  Serial.println();
  Serial.print(F("*** MEASUREMENT #"));
  Serial.print(recCount);
  Serial.print(F(" = "));
  Serial.print(disp, 0);
  Serial.println(F(" g ***"));
  summary();
}

void summary() {
  Serial.println(F("--- summary ---"));
  if (recCount == 0) { Serial.println(F("(no records yet)")); return; }
  float mn = records[0], mx = records[0], sum = 0;
  Serial.print(F("readings: "));
  for (byte i = 0; i < recCount; i++) {
    if (records[i] < mn) mn = records[i];
    if (records[i] > mx) mx = records[i];
    sum += records[i];
    Serial.print(records[i], 0);
    if (i < recCount - 1) Serial.print(F(", "));
  }
  Serial.println();
  float avg = sum / recCount;
  Serial.print(F("count="));   Serial.print(recCount);
  Serial.print(F("  min="));   Serial.print(mn, 0);
  Serial.print(F("  max="));   Serial.print(mx, 0);
  Serial.print(F("  spread="));Serial.print(mx - mn, 0);
  Serial.print(F(" g  avg="));  Serial.print(avg, 0);
  Serial.println(F(" g"));
  Serial.println(F("---------------"));
}

// ---------- live stream (low rate, just so you can see it's alive) ----------
void streamLive(float v, float vSmooth) {
  if (millis() - lastPrint < PRINT_INTERVAL) return;
  lastPrint = millis();
  if (!postCal) {
    Serial.print(F("  live = "));
    Serial.println(v, 2);
  } else if (state != S_WEIGHED) {
    float disp = roundf(vSmooth / DISP_ROUND_G) * DISP_ROUND_G;
    Serial.print(F("  weight = "));
    Serial.print(disp, 0);
    Serial.println(F(" g"));
  }
}

// ---------- cal flow ----------
void calibrate() {
  postCal  = false;
  recCount = 0;
  state    = S_WAITING;
  Serial.println(F("***"));
  Serial.println(F("CALIBRATION"));
  Serial.println(F("1) Make sure the cell is EMPTY. When 'live' is steady, send 't' to tare."));

  boolean done = false;
  while (!done) {
    LoadCell.update();
    if (millis() - lastPrint > PRINT_INTERVAL) {
      Serial.print(F("  live = ")); Serial.println(LoadCell.getData(), 2);
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
  Serial.println(F("2) Put the KNOWN WEIGHT on the cell (5 kg dumbbell recommended)."));
  Serial.println(F("3) Send the weight in GRAMS + Enter   (5 kg = 5000)"));

  float knownMass = 0;
  done = false;
  while (!done) {
    LoadCell.update();
    if (millis() - lastPrint > PRINT_INTERVAL) {
      Serial.print(F("  live = ")); Serial.println(LoadCell.getData(), 2);
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
  Serial.println(F("***"));
  Serial.println(F("Remove the calibration weight. Then place any object and watch."));
  Serial.println(F("Each stable placement auto-captures a measurement."));
  Serial.println(F("Commands: t=tare  r=recal  s=summary  c=clear records"));
  Serial.println();

  // reset MA + state for clean post-cal
  maCount = 0; maIdx = 0; maSum = 0;
  state = S_WAITING;
  postCal = true;
}
