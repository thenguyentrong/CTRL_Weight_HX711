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
 * Serial: 57600 baud, line ending "Newline".
 *
 * FLOW:
 *   On boot it starts calibrating Cell #1. It walks you through it:
 *     1) Set up the cell, empty.
 *     2) When the live value is steady, send 't' -> tare.
 *     3) Put the known weight on, send the weight value (e.g. 7.5).
 *     4) It prints and remembers the calibration factor for this cell.
 *   After Cell #1 it stays live (you can see kg). To do the next cell:
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

void setup() {
  Serial.begin(57600);
  delay(10);
  Serial.println();
  Serial.println(F("=== BenchCalLib (HX711_ADC) ==="));
  Serial.println(F("Wiring: DT=D4, SCK=D5"));

  LoadCell.begin();
  // If your cell reads NEGATIVE when loaded, uncomment the next line:
  // LoadCell.setReverseOutput();

  unsigned long stabilizingTime = 2000;
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

  if (newDataReady && (millis() - lastPrint > PRINT_INTERVAL)) {
    float v = LoadCell.getData();
    Serial.print(F("  live = "));
    Serial.println(v, 2);
    newDataReady = false;
    lastPrint = millis();
  }

  if (Serial.available() > 0) {
    char c = Serial.read();
    if      (c == 't') LoadCell.tareNoDelay();
    else if (c == 'r') calibrate();
    else if (c == 's') summary();
    else if (c == 'c') changeCalFactor();
  }

  if (LoadCell.getTareStatus() == true) {
    Serial.println(F("Tare complete"));
  }
}

void calibrate() {
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
  Serial.println(F("4) Send the weight in kg (e.g.  7.5  + Enter)."));

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
        Serial.print(F("Known mass = ")); Serial.print(knownMass); Serial.println(F(" kg"));
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
  Serial.println(F("Live value now shows kg. Remove the weight - should go to ~0."));
  Serial.println(F("Next:  r = calibrate next cell  |  s = show summary  |  t = re-tare  |  c = edit factor"));
  Serial.println();
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
