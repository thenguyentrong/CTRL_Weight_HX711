/*
 * StabilityTest  -  10-minute HX711 baseline drift / noise test
 * --------------------------------------------------------------
 * Purpose: prove (or disprove) that the HX711 + cell + wiring can sit at
 * one value while nothing touches it. No calibration, no auto-zero, no
 * smoothing - just raw streaming so you can SEE the drift and spikes.
 *
 * WIRING (one HX711):
 *   HX711 DT  -> D4
 *   HX711 SCK -> D5
 *   HX711 VCC -> 5V
 *   HX711 GND -> GND
 *   Load cell: Red=E+  Black=E-  Green=A+  White=A-  Yellow->GND
 *
 * SERIAL: 57600 baud.
 *
 * FLOW:
 *   1) Boot, settle 5 s, tare once.  After that NOTHING resets the zero.
 *   2) Streams one line every 250 ms:
 *        t=  12.5s   v=     -3   min=    -47   max=     38   range=  85
 *   3) Every 60 s prints a one-line MINUTE summary.
 *   4) After 600 s (10 min) prints FINAL summary and stops.
 *
 * HOW TO READ IT:
 *   - Don't touch the table, don't bump the cable, don't walk near it.
 *   - "v" is raw counts after the initial tare. Should hover near 0.
 *   - "range" over 10 min tells you the worst case drift+noise window.
 *   - For your 100 kg cell, ~42 counts == 1 gram. So a range of 420
 *     means the empty cell wandered by 10 g - that's your noise floor.
 *   - Big sudden jumps (thousands or millions) = wiring / breadboard.
 * --------------------------------------------------------------
 */

#include <HX711_ADC.h>

const int HX711_dout = 4;
const int HX711_sck  = 5;
HX711_ADC LoadCell(HX711_dout, HX711_sck);

const unsigned long PRINT_INTERVAL  = 250;     // ms between live prints
const unsigned long MINUTE_MS       = 60000UL;
const unsigned long TEST_DURATION   = 600000UL; // 10 minutes

unsigned long t0          = 0;     // millis() at start of test
unsigned long lastPrint   = 0;
unsigned long lastMinute  = 0;
unsigned long sampleCount = 0;

float vMin = 0, vMax = 0;
double vSum = 0;       // for mean
bool   firstSample = true;

// per-minute window
float  minMin = 0, minMax = 0;
double minSum = 0;
unsigned long minCount = 0;
unsigned long minuteIdx = 0;
bool   minuteFirst = true;

bool   testDone = false;

void setup() {
  Serial.begin(57600);
  delay(10);
  Serial.println();
  Serial.println(F("=== StabilityTest (10 min HX711 drift baseline) ==="));
  Serial.println(F("Wiring: DT=D4, SCK=D5"));
  Serial.println(F("Goal: do NOT touch the cell for 10 minutes. Watch the range."));
  Serial.println();

  LoadCell.begin();
  LoadCell.setReverseOutput();   // your cell reads negative when loaded
  LoadCell.setCalFactor(1.0);    // raw counts mode

  unsigned long stabilizingTime = 5000;
  boolean _tare = true;
  LoadCell.start(stabilizingTime, _tare);
  if (LoadCell.getTareTimeoutFlag() || LoadCell.getSignalTimeoutFlag()) {
    Serial.println(F("Timeout - check HX711 wiring. Halting."));
    while (1);
  }
  while (!LoadCell.update());
  Serial.println(F("Tared. Starting 10-minute baseline. DO NOT TOUCH."));
  Serial.println();

  t0         = millis();
  lastPrint  = t0;
  lastMinute = t0;
}

void loop() {
  if (testDone) return;

  if (LoadCell.update()) {
    float v = LoadCell.getData();

    // overall stats
    if (firstSample) { vMin = vMax = v; vSum = v; firstSample = false; }
    else {
      if (v < vMin) vMin = v;
      if (v > vMax) vMax = v;
      vSum += v;
    }
    sampleCount++;

    // per-minute stats
    if (minuteFirst) { minMin = minMax = v; minSum = v; minuteFirst = false; }
    else {
      if (v < minMin) minMin = v;
      if (v > minMax) minMax = v;
      minSum += v;
    }
    minCount++;

    unsigned long now = millis();

    // line stream every 250 ms
    if (now - lastPrint >= PRINT_INTERVAL) {
      lastPrint = now;
      float elapsedSec = (now - t0) / 1000.0;
      Serial.print(F("t="));   Serial.print(elapsedSec, 1); Serial.print(F("s"));
      Serial.print(F("   v="));     Serial.print(v, 1);
      Serial.print(F("   min="));   Serial.print(vMin, 1);
      Serial.print(F("   max="));   Serial.print(vMax, 1);
      Serial.print(F("   range=")); Serial.println(vMax - vMin, 1);
    }

    // minute summary
    if (now - lastMinute >= MINUTE_MS) {
      minuteIdx++;
      double mean = (minCount > 0) ? minSum / minCount : 0;
      Serial.println();
      Serial.print(F(">>> MINUTE "));   Serial.print(minuteIdx);
      Serial.print(F("  samples=")); Serial.print(minCount);
      Serial.print(F("  mean="));    Serial.print((float)mean, 1);
      Serial.print(F("  min="));     Serial.print(minMin, 1);
      Serial.print(F("  max="));     Serial.print(minMax, 1);
      Serial.print(F("  range="));   Serial.println(minMax - minMin, 1);
      Serial.println();

      // reset per-minute
      minuteFirst = true;
      minMin = minMax = 0;
      minSum = 0;
      minCount = 0;
      lastMinute = now;
    }

    // end of test
    if (now - t0 >= TEST_DURATION) {
      double mean = (sampleCount > 0) ? vSum / sampleCount : 0;
      Serial.println();
      Serial.println(F("================ FINAL (10 min) ================"));
      Serial.print(F("samples : ")); Serial.println(sampleCount);
      Serial.print(F("mean    : ")); Serial.println((float)mean, 2);
      Serial.print(F("min     : ")); Serial.println(vMin, 2);
      Serial.print(F("max     : ")); Serial.println(vMax, 2);
      Serial.print(F("range   : ")); Serial.println(vMax - vMin, 2);
      Serial.println();
      Serial.println(F("Reference: 100 kg cell @ 2 mV/V, HX711 g128 -> ~42 counts/gram."));
      Serial.println(F("  range / 42  =  drift+noise in grams over 10 minutes."));
      Serial.println(F("================================================"));
      testDone = true;
    }
  }
}
