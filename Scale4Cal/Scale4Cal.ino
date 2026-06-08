/*
 * Scale4Cal  -  4-cell platform scale + on-platform calibration
 * --------------------------------------------------------------
 * Same wiring as Scale4. Adds a 'k' command that calibrates the
 * ASSEMBLED platform with a known weight, on top of per-cell factors.
 *
 * WIRING (one HX711 per cell, all share 5V/GND):
 *   Sensor 1:  DT -> D4   SCK -> D3
 *   Sensor 2:  DT -> D5   SCK -> D8
 *   Sensor 3:  DT -> D6   SCK -> D9
 *   Sensor 4:  DT -> D7   SCK -> D10
 *   Cell:  Red=E+  Black=E-  Green=A+  White=A-  Yellow -> GND
 *
 * SERIAL: 57600 baud, line ending "Newline".
 *
 * FLOW:
 *   1) Boot -> auto-tare empty platform.
 *   2) Press 'd' with dumbbell on -> see per-cell contributions.
 *      If any cell is negative when loaded, fix its sign (in code or wires).
 *   3) Press 'k' with NOTHING on -> re-tare prompt.
 *   4) Place known weight (5520 g dumbbell), type its weight + Enter.
 *      Sketch averages 3 s and computes a single PLATFORM factor.
 *   5) From now on, weight = ( sum_of_per_cell_grams ) * platformFactor.
 *
 * COMMANDS:
 *   t  re-tare to 0
 *   k  KNOWN-weight platform calibration
 *   p  print current cal (per-cell + platform factor)
 *   d  toggle DEBUG stream (per-cell contribution)
 *   r  reset platform factor to 1.0
 * --------------------------------------------------------------
 */

#include <HX711_ADC.h>

const byte NUM_CELLS = 4;

HX711_ADC cellA(4, 3);
HX711_ADC cellB(5, 8);
HX711_ADC cellC(6, 9);
HX711_ADC cellD(7, 10);
HX711_ADC* cells[NUM_CELLS] = { &cellA, &cellB, &cellC, &cellD };

// Per-cell counts/gram from bench cal (CALIBRATION.md).
// Cell #2 is NEGATIVE because A+/A- were wired opposite during bench cal.
// If you wired all 4 cells the SAME way on the platform, flip cell #2 to +43.85f.
float cellFactors[NUM_CELLS] = {
   44.66f,   // Cell #1
  -43.85f,   // Cell #2  <-- flip sign if wires are now matched
   44.16f,   // Cell #3
   44.81f    // Cell #4
};

// Platform-level multiplier set by 'k'. 1.0 = use per-cell factors only.
float platformFactor = 1.0f;

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
  Serial.println(F("=== Scale4Cal - 4-cell platform + on-platform cal ==="));
  Serial.println(F("Wiring:"));
  Serial.println(F("  S1  DT=D4  SCK=D3 | S2  DT=D5  SCK=D8"));
  Serial.println(F("  S3  DT=D6  SCK=D9 | S4  DT=D7  SCK=D10"));
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
  for (byte k = 0; k < 5; k++) for (byte i = 0; i < NUM_CELLS; i++) cells[i]->update();

  Serial.println(F("Ready."));
  Serial.println(F("Commands:  t=tare  k=KNOWN-weight cal  p=print  d=debug  r=reset platform"));
  Serial.println();
}

void loop() {
  bool anyNew = false;
  for (byte i = 0; i < NUM_CELLS; i++) {
    if (cells[i]->update()) anyNew = true;
  }

  if (anyNew) {
    float totalG = sumGrams() * platformFactor;

    if (maCount == MA_N) maSum -= maBuf[maIdx];
    maBuf[maIdx] = totalG;
    maSum += totalG;
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
    else if (c == 'k') platformCalibrate();
    else if (c == 'p') printCal();
    else if (c == 'd') {
      debugStream = !debugStream;
      Serial.print(F("Debug stream "));
      Serial.println(debugStream ? F("ON") : F("OFF"));
    }
    else if (c == 'r') {
      platformFactor = 1.0f;
      Serial.println(F("Platform factor reset to 1.0"));
    }
  }
}

float sumGrams() {
  float total = 0;
  for (byte i = 0; i < NUM_CELLS; i++) {
    total += cells[i]->getData() / cellFactors[i];
  }
  return total;
}

void doTare() {
  Serial.println(F("Taring all 4 cells - platform must be EMPTY..."));
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

void platformCalibrate() {
  Serial.println();
  Serial.println(F("=== PLATFORM CALIBRATION ==="));
  Serial.println(F("Step 1) Remove EVERYTHING from the platform."));
  Serial.println(F("        Press any key when empty to tare..."));
  while (Serial.available()) Serial.read();
  while (!Serial.available()) {
    for (byte i = 0; i < NUM_CELLS; i++) cells[i]->update();
  }
  while (Serial.available()) Serial.read();
  doTare();

  Serial.println();
  Serial.println(F("Step 2) Place your known weight on the platform."));
  Serial.println(F("Step 3) Type its weight in GRAMS + Enter (e.g. 5520):"));

  float trueG = 0;
  while (trueG <= 0) {
    for (byte i = 0; i < NUM_CELLS; i++) cells[i]->update();
    if (Serial.available()) {
      trueG = Serial.parseFloat();
      while (Serial.available()) Serial.read();
      if (trueG <= 0) Serial.println(F("Need a positive number. Try again:"));
    }
  }
  Serial.print(F("Known weight = ")); Serial.print(trueG, 0); Serial.println(F(" g"));

  // Average ~3 seconds of measurements (under current per-cell factors only)
  Serial.println(F("Averaging 3 s..."));
  float oldPlatform = platformFactor;
  platformFactor = 1.0f;          // measure unadjusted sum
  float sum = 0;
  int   n   = 0;
  unsigned long t0 = millis();
  while (millis() - t0 < 3000) {
    bool any = false;
    for (byte i = 0; i < NUM_CELLS; i++) if (cells[i]->update()) any = true;
    if (any) { sum += sumGrams(); n++; }
  }
  if (n < 5) {
    Serial.println(F("Too few samples - aborting."));
    platformFactor = oldPlatform;
    return;
  }
  float measured = sum / n;
  Serial.print(F("Measured (per-cell only): ")); Serial.print(measured, 1); Serial.println(F(" g"));

  if (measured < 50.0f && measured > -50.0f) {
    Serial.println(F("Measured ~0 - is the weight actually on the platform? Aborting."));
    platformFactor = oldPlatform;
    return;
  }

  platformFactor = trueG / measured;
  Serial.print(F("New platform factor = ")); Serial.println(platformFactor, 4);
  if (platformFactor < 0) {
    Serial.println(F("WARNING: platform factor is NEGATIVE."));
    Serial.println(F("That means the sum was negative - check cell signs with 'd'."));
  }
  Serial.println(F("Done. Remove the weight, then place anything to verify."));
  Serial.println();
}

void printCal() {
  Serial.println(F("Calibration:"));
  for (byte i = 0; i < NUM_CELLS; i++) {
    Serial.print(F("  Cell #")); Serial.print(i + 1);
    Serial.print(F("  factor = ")); Serial.println(cellFactors[i], 2);
  }
  Serial.print(F("  Platform factor = ")); Serial.println(platformFactor, 4);
}
