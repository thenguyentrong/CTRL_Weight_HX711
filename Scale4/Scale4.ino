/*
 * Scale4  -  4-cell platform scale with per-cell bench calibration
 * ----------------------------------------------------------------
 * Uses the factors measured on the bench (see CALIBRATION.md).
 * Each cell has its own HX711, separate DT/SCK pins. Total weight
 * = sum( cell_i / factor_i ) which is correct at ANY load position
 * because the 4 support forces add up to the true load (statics).
 *
 * WIRING (one HX711 per cell, all share 5V/GND):
 *
 *   Each HX711: VCC -> Arduino 5V, GND -> common GND.
 *   Each load cell: Red=E+  Black=E-  Green=A+  White=A-  Yellow -> GND.
 *
 *   Sensor 1:  DT -> D4   SCK -> D3
 *   Sensor 2:  DT -> D5   SCK -> D8
 *   Sensor 3:  DT -> D6   SCK -> D9
 *   Sensor 4:  DT -> D7   SCK -> D10
 *
 * SERIAL: 57600 baud, line ending "Newline".
 *
 * BEHAVIOR:
 *   1) Boot -> 3 s settling, then AUTO-TARE (platform MUST be empty).
 *   2) Place object -> rounded weight streams every 250 ms.
 *   3) Press 't' anytime to re-tare to 0.
 *
 * COMMANDS:
 *   t  re-tare to 0  (platform must be empty)
 *   p  print current per-cell calibration factors
 *   d  toggle DEBUG stream (per-cell contribution + total)
 * ----------------------------------------------------------------
 */

#include <HX711_ADC.h>

const byte NUM_CELLS = 4;

HX711_ADC cellA(4, 3);    // S1: DT=D4, SCK=D3
HX711_ADC cellB(5, 8);    // S2: DT=D5, SCK=D8
HX711_ADC cellC(6, 9);    // S3: DT=D6, SCK=D9
HX711_ADC cellD(7, 10);   // S4: DT=D7, SCK=D10
HX711_ADC* cells[NUM_CELLS] = { &cellA, &cellB, &cellC, &cellD };

// ---- PER-CELL counts/gram factors (from CALIBRATION.md) ----
// All positive: requires all 4 cells wired the SAME way (Green=A+, White=A-).
// If a cell makes the weight go DOWN when pressed, swap its Green<->White wires.
float cellFactors[NUM_CELLS] = {
   44.66f,   // Cell #1
   43.85f,   // Cell #2
   44.16f,   // Cell #3
   44.81f    // Cell #4
};

// ---- Display smoothing (same idea as RepeatTest) ----
const byte  MA_N         = 16;     // ~1.6 s window at 10 SPS
const float DISP_ROUND_G = 10.0;   // round shown weight to nearest 10 g
float maBuf[MA_N];
byte  maIdx = 0, maCount = 0;
float maSum = 0;

unsigned long lastPrint = 0;
const unsigned long PRINT_INTERVAL = 250;

bool debugStream = true;   // start with per-cell debug ON (press 'd' to toggle off)

void setup() {
  Serial.begin(57600);
  delay(10);
  Serial.println();
  Serial.println(F("=== Scale4 - 4-cell platform ==="));
  Serial.println(F("Wiring:"));
  Serial.println(F("  S1  DT=D4   SCK=D3"));
  Serial.println(F("  S2  DT=D5   SCK=D8"));
  Serial.println(F("  S3  DT=D6   SCK=D9"));
  Serial.println(F("  S4  DT=D7   SCK=D10"));
  Serial.println(F("All HX711 VCC=5V, GND=common GND."));
  Serial.println(F("Cell:  Red=E+  Black=E-  Green=A+  White=A-  Yellow=GND"));
  Serial.println();

  printCal();
  Serial.println();
  Serial.println(F("Settling 3 s, then auto-taring. Keep platform EMPTY."));

  for (byte i = 0; i < NUM_CELLS; i++) {
    cells[i]->begin();
    cells[i]->setReverseOutput();
    cells[i]->setCalFactor(1.0f);   // we apply per-cell factor manually
  }

  const unsigned long stab = 3000;
  for (byte i = 0; i < NUM_CELLS; i++) {
    cells[i]->start(stab, true);    // settle + tare
    if (cells[i]->getTareTimeoutFlag() || cells[i]->getSignalTimeoutFlag()) {
      Serial.print(F("Cell #")); Serial.print(i + 1);
      Serial.println(F(": TIMEOUT - check wiring!"));
    }
  }
  // Discard a few samples so the MA starts clean
  for (byte k = 0; k < 5; k++) {
    for (byte i = 0; i < NUM_CELLS; i++) cells[i]->update();
  }

  Serial.println(F("Ready. Place your object."));
  Serial.println(F("Commands:  t=re-tare   p=print cal   d=debug stream"));
  Serial.println();
}

void loop() {
  bool anyNew = false;
  for (byte i = 0; i < NUM_CELLS; i++) {
    if (cells[i]->update()) anyNew = true;
  }

  if (anyNew) {
    float totalG = 0;
    for (byte i = 0; i < NUM_CELLS; i++) {
      totalG += cells[i]->getData() / cellFactors[i];
    }

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
    if (c == 't') {
      Serial.println(F("Taring all 4 cells - keep platform EMPTY..."));
      for (byte i = 0; i < NUM_CELLS; i++) cells[i]->tareNoDelay();
      bool tareDone[NUM_CELLS] = { false, false, false, false };
      unsigned long t0 = millis();
      while (millis() - t0 < 5000) {
        bool all = true;
        for (byte i = 0; i < NUM_CELLS; i++) {
          cells[i]->update();
          if (cells[i]->getTareStatus()) tareDone[i] = true;
          if (!tareDone[i]) all = false;
        }
        if (all) break;
      }
      maCount = 0; maIdx = 0; maSum = 0;
      Serial.println(F("Tare complete. Place your object."));
    }
    else if (c == 'p') printCal();
    else if (c == 'd') {
      debugStream = !debugStream;
      Serial.print(F("Debug stream "));
      Serial.println(debugStream ? F("ON") : F("OFF"));
    }
  }
}

void printCal() {
  Serial.println(F("Calibration (counts/gram):"));
  for (byte i = 0; i < NUM_CELLS; i++) {
    Serial.print(F("  Cell #")); Serial.print(i + 1);
    Serial.print(F("  factor = ")); Serial.println(cellFactors[i], 2);
  }
}
