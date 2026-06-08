/*
 * Scale4Recal  -  in-situ per-cell calibration of the assembled platform
 * ----------------------------------------------------------------------
 * Same wiring as Scale4. Adds a 'c' command that walks you through a
 * per-cell calibration on the assembled platform:
 *   - tare empty
 *   - you type the known weight in grams (e.g. 5520)
 *   - for each sensor 1..4 in turn: you place the weight on that
 *     sensor's load point, press any key, 10 s countdown captures the
 *     signal, sketch computes that cell's counts/gram factor.
 *
 * NOTE on placement: for the result to be right, the weight has to load
 * MAINLY the cell being calibrated. On a stiff platform deck the weight
 * spreads to all 4 cells, so put it as close to that cell's load stud
 * as possible (and on a small block, if needed, so the deck doesn't
 * carry it sideways).
 *
 * WIRING (one HX711 per cell, all share 5V/GND):
 *   Sensor 1:  DT=D4  SCK=D3
 *   Sensor 2:  DT=D5  SCK=D8
 *   Sensor 3:  DT=D6  SCK=D9
 *   Sensor 4:  DT=D7  SCK=D10
 *   Cell:  Red=E+  Black=E-  Green=A+  White=A-  Yellow -> GND
 *
 * SERIAL: 57600 baud, line ending "Newline".
 *
 * COMMANDS:
 *   t  re-tare to 0
 *   c  run per-cell platform calibration
 *   p  print current per-cell factors
 *   d  toggle debug stream (per-cell contribution + total)
 * ----------------------------------------------------------------------
 */

#include <HX711_ADC.h>

const byte NUM_CELLS = 4;

HX711_ADC cellA(4, 3);
HX711_ADC cellB(5, 8);
HX711_ADC cellC(6, 9);
HX711_ADC cellD(7, 10);
HX711_ADC* cells[NUM_CELLS] = { &cellA, &cellB, &cellC, &cellD };

// Per-cell counts/gram. Initial values from bench cal (CALIBRATION.md);
// they get REPLACED by whatever 'c' calibration produces.
// Signs auto-adjust because the cal uses raw_signal / known_grams.
float cellFactors[NUM_CELLS] = {
  -44.66f,   // Cell #1
  -43.85f,   // Cell #2
   44.16f,   // Cell #3
   44.81f    // Cell #4
};

const byte  MA_N         = 16;
const float DISP_ROUND_G = 10.0;
float maBuf[MA_N];
byte  maIdx = 0, maCount = 0;
float maSum = 0;

unsigned long lastPrint = 0;
const unsigned long PRINT_INTERVAL = 250;

bool debugStream = false;

void setup() {
  Serial.begin(57600);
  delay(10);
  Serial.println();
  Serial.println(F("=== Scale4Recal - in-situ per-cell calibration ==="));
  Serial.println(F("Wiring: S1 D4/D3 | S2 D5/D8 | S3 D6/D9 | S4 D7/D10"));
  Serial.println();
  printCal();
  Serial.println();
  Serial.println(F("Settling 3 s, then auto-taring. Keep platform EMPTY."));

  for (byte i = 0; i < NUM_CELLS; i++) {
    cells[i]->begin();
    cells[i]->setReverseOutput();
    cells[i]->setCalFactor(1.0f);
  }
  for (byte i = 0; i < NUM_CELLS; i++) {
    cells[i]->start(3000, true);
    if (cells[i]->getTareTimeoutFlag() || cells[i]->getSignalTimeoutFlag()) {
      Serial.print(F("Cell #")); Serial.print(i + 1);
      Serial.println(F(": TIMEOUT - check wiring!"));
    }
  }
  for (byte k = 0; k < 5; k++)
    for (byte i = 0; i < NUM_CELLS; i++) cells[i]->update();

  Serial.println(F("Ready."));
  Serial.println(F("Commands:  c=per-cell cal   t=tare   p=print   d=debug"));
  Serial.println();
}

void loop() {
  bool any = false;
  for (byte i = 0; i < NUM_CELLS; i++) if (cells[i]->update()) any = true;

  if (any) {
    float total = 0;
    for (byte i = 0; i < NUM_CELLS; i++)
      total += cells[i]->getData() / cellFactors[i];

    if (maCount == MA_N) maSum -= maBuf[maIdx];
    maBuf[maIdx] = total;
    maSum += total;
    maIdx = (maIdx + 1) % MA_N;
    if (maCount < MA_N) maCount++;
    float smooth = maSum / maCount;

    if (millis() - lastPrint > PRINT_INTERVAL) {
      lastPrint = millis();
      float disp = roundf(smooth / DISP_ROUND_G) * DISP_ROUND_G;
      if (debugStream) {
        Serial.print(F("  "));
        for (byte i = 0; i < NUM_CELLS; i++) {
          float gi = cells[i]->getData() / cellFactors[i];
          Serial.print(F("c")); Serial.print(i + 1);
          Serial.print(F("=")); Serial.print(gi, 0); Serial.print(F("g  "));
        }
        Serial.print(F("| total = ")); Serial.print(disp, 0); Serial.println(F(" g"));
      } else {
        Serial.print(F("  weight = ")); Serial.print(disp, 0); Serial.println(F(" g"));
      }
    }
  }

  if (Serial.available()) {
    char c = Serial.read();
    if      (c == 't') doTare();
    else if (c == 'c') runCalibration();
    else if (c == 'p') printCal();
    else if (c == 'd') {
      debugStream = !debugStream;
      Serial.print(F("Debug stream "));
      Serial.println(debugStream ? F("ON") : F("OFF"));
    }
  }
}

// ---------- helpers ----------
void flushInput() { while (Serial.available()) Serial.read(); }

void waitKey() {
  flushInput();
  while (!Serial.available())
    for (byte i = 0; i < NUM_CELLS; i++) cells[i]->update();
  flushInput();
}

void doTare() {
  Serial.println(F("Taring all 4 cells - keep platform EMPTY..."));
  for (byte i = 0; i < NUM_CELLS; i++) cells[i]->tareNoDelay();
  bool done[NUM_CELLS] = { false, false, false, false };
  unsigned long t0 = millis();
  while (millis() - t0 < 5000) {
    bool all = true;
    for (byte i = 0; i < NUM_CELLS; i++) {
      cells[i]->update();
      if (cells[i]->getTareStatus()) done[i] = true;
      if (!done[i]) all = false;
    }
    if (all) break;
  }
  maCount = 0; maIdx = 0; maSum = 0;
  Serial.println(F("Tare complete."));
}

// ---------- the main thing: per-cell platform calibration ----------
void runCalibration() {
  Serial.println();
  Serial.println(F("===== PER-CELL PLATFORM CALIBRATION ====="));
  Serial.println(F("Step 1) Remove EVERYTHING. Press any key to tare..."));
  waitKey();
  doTare();

  Serial.println();
  Serial.println(F("Step 2) Type the known weight in GRAMS + Enter  (e.g. 5520)"));
  float knownG = 0;
  while (knownG <= 0) {
    for (byte i = 0; i < NUM_CELLS; i++) cells[i]->update();
    if (Serial.available()) {
      knownG = Serial.parseFloat();
      flushInput();
      if (knownG <= 0) Serial.println(F("Need a positive number. Try again:"));
    }
  }
  Serial.print(F("Known weight = ")); Serial.print(knownG, 0); Serial.println(F(" g"));

  float newFactors[NUM_CELLS];
  for (byte i = 0; i < NUM_CELLS; i++) newFactors[i] = cellFactors[i];

  for (byte i = 0; i < NUM_CELLS; i++) {
    Serial.println();
    Serial.print(F("--- Cell #")); Serial.print(i + 1); Serial.println(F(" ---"));
    Serial.print(F("Place the weight on / over SENSOR ")); Serial.print(i + 1);
    Serial.println(F("'s load point."));
    Serial.println(F("When ready, press any key. 10 s countdown then capture."));
    waitKey();

    unsigned long t0 = millis();
    int lastSec = -1;
    float sum = 0;
    int   n   = 0;
    while (millis() - t0 < 10000) {
      bool a = false;
      for (byte j = 0; j < NUM_CELLS; j++) if (cells[j]->update()) a = true;
      unsigned long elapsed = millis() - t0;
      int sec = elapsed / 1000;
      if (sec != lastSec) {
        lastSec = sec;
        Serial.print(F("  ")); Serial.print(10 - sec); Serial.println(F(" s..."));
      }
      // Average during the last 4 seconds (settled)
      if (elapsed >= 6000 && a) {
        sum += cells[i]->getData();
        n++;
      }
    }
    if (n < 5) {
      Serial.println(F("Too few samples. Keeping old factor for this cell."));
      continue;
    }
    float avg = sum / n;
    Serial.print(F("Cell #")); Serial.print(i + 1);
    Serial.print(F("  averaged signal = ")); Serial.println(avg, 1);

    float f = avg / knownG;
    if (f > -1.0f && f < 1.0f) {
      Serial.print(F("Factor ")); Serial.print(f, 4);
      Serial.println(F(" is suspiciously small - weight may not be on this cell. Keeping old factor."));
      continue;
    }
    newFactors[i] = f;
    Serial.print(F("Cell #")); Serial.print(i + 1);
    Serial.print(F("  new factor = ")); Serial.println(f, 2);
  }

  // Apply
  for (byte i = 0; i < NUM_CELLS; i++) cellFactors[i] = newFactors[i];

  Serial.println();
  Serial.println(F("===== CALIBRATION DONE ====="));
  printCal();
  maCount = 0; maIdx = 0; maSum = 0;
  Serial.println(F("Remove the weight. Then place anything to verify."));
  Serial.println();
}

void printCal() {
  Serial.println(F("Calibration (counts/gram):"));
  for (byte i = 0; i < NUM_CELLS; i++) {
    Serial.print(F("  Cell #")); Serial.print(i + 1);
    Serial.print(F("  factor = ")); Serial.println(cellFactors[i], 2);
  }
}
