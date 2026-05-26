/*
 * BenchCal  -  single-HX711 cell-by-cell bench calibration & tester
 * ----------------------------------------------------------------
 * Hook ONE HX711 to the Arduino. Connect each load cell to it in turn.
 * The sketch streams the live raw value continuously, walks you through
 * zero + load + record for each cell, and at the end prints a comparison
 * table that flags any cell whose factor is far from the others (likely
 * damaged).
 *
 * WIRING (one HX711 on the Arduino):
 *   HX711 DT  -> D4
 *   HX711 SCK -> D3
 *   HX711 VCC -> 5V
 *   HX711 GND -> GND
 *   Load cell -> HX711:  Red=E+  Black=E-  Green=A+  White=A-  Yellow->GND
 *
 * SERIAL: 57600 baud, line ending "Newline".
 *
 * FLOW:
 *   1) Boot: enter the test weight in kg (e.g. 7.5).
 *   2) For each cell:
 *        - Hook up the cell. Live raw value streams below the prompt.
 *        - Empty cell, press ENTER -> captures ZERO.
 *        - Put the weight on the LOAD end, press ENTER -> captures LOADED.
 *        - It prints net counts and counts/kg for that cell.
 *   3) Press ENTER for the next cell, or 'q' + ENTER to finish and see summary.
 * ----------------------------------------------------------------
 */

const byte DT_PIN  = 4;
const byte CLK_PIN = 3;

const byte MAX_CELLS = 8;
struct Result { long offset; long net; long countsPerKg; bool ok; };
Result results[MAX_CELLS];
byte   cellCount = 0;
float  testWeight = 0;

const unsigned long STREAM_INTERVAL = 400;   // ms between live prints
unsigned long lastStreamMs = 0;

// ---------------- HX711 ----------------
bool readRaw(long &out) {
  unsigned long t0 = millis();
  while (digitalRead(DT_PIN) == HIGH) { if (millis() - t0 > 200) return false; }
  unsigned long v = 0;
  for (byte b = 0; b < 24; b++) {
    digitalWrite(CLK_PIN, HIGH); delayMicroseconds(1);
    v = (v << 1);
    if (digitalRead(DT_PIN)) v++;
    digitalWrite(CLK_PIN, LOW); delayMicroseconds(1);
  }
  digitalWrite(CLK_PIN, HIGH); delayMicroseconds(1);
  digitalWrite(CLK_PIN, LOW);  delayMicroseconds(1);
  if (v & 0x800000UL) v |= 0xFF000000UL;
  out = (long)v;
  return true;
}

// Average of n successful samples, with timeout safety.
bool readAvg(int n, long &out) {
  long sum = 0; int got = 0;
  for (int i = 0; i < n + 10 && got < n; i++) {
    long r;
    if (readRaw(r)) { sum += r; got++; }
  }
  if (got < 3) return false;
  out = sum / got;
  return true;
}

// ---------------- input helpers ----------------
float inputFloat() {
  String s = "";
  while (Serial.available()) Serial.read();
  while (true) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        if (s.length() > 0) { while (Serial.available()) Serial.read(); return s.toFloat(); }
      } else if (c >= 32) {
        s += c;
      }
    }
  }
}

// Stream the live raw value. When user presses ENTER, capture an averaged sample
// and return it in outAvg. Returns false on read error.
bool streamUntilEnter(long &outAvg) {
  while (Serial.available()) Serial.read();
  lastStreamMs = 0;
  while (true) {
    if (millis() - lastStreamMs >= STREAM_INTERVAL) {
      long r;
      if (readRaw(r)) { Serial.print(F("  live = ")); Serial.println(r); }
      else            { Serial.println(F("  (HX711 not ready - check wiring)")); }
      lastStreamMs = millis();
    }
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        while (Serial.available()) Serial.read();
        return readAvg(20, outAvg);
      }
      // any other char: ignore while streaming
    }
  }
}

// Wait for ENTER (next cell) or 'q' (quit). Streams live value while waiting.
// Returns true if user wants to quit.
bool waitNextOrQuit() {
  while (Serial.available()) Serial.read();
  lastStreamMs = 0;
  while (true) {
    if (millis() - lastStreamMs >= STREAM_INTERVAL) {
      long r;
      if (readRaw(r)) { Serial.print(F("  live = ")); Serial.println(r); }
      lastStreamMs = millis();
    }
    if (Serial.available()) {
      char c = Serial.read();
      if (c == 'q' || c == 'Q') { while (Serial.available()) Serial.read(); return true; }
      if (c == '\n' || c == '\r') { while (Serial.available()) Serial.read(); return false; }
    }
  }
}

// ---------------- per-cell flow ----------------
void doNextCell() {
  byte idx = cellCount;
  Serial.println();
  Serial.print(F("=== CELL #")); Serial.print(idx + 1); Serial.println(F(" ==="));
  Serial.println(F("1) Hook this cell to the HX711."));
  Serial.println(F("2) Make sure NOTHING is on the cell."));
  Serial.println(F("3) Press ENTER to capture ZERO (live values stream below)..."));

  long offset;
  if (!streamUntilEnter(offset)) {
    Serial.println(F("READ FAILED - check wiring. Skipping this cell."));
    results[idx].ok = false;
    cellCount++;
    return;
  }
  Serial.print(F("  ZERO = ")); Serial.println(offset);
  Serial.println();

  Serial.print(F("4) Put the ")); Serial.print(testWeight, 2);
  Serial.println(F(" kg on the LOAD END."));
  Serial.println(F("5) Wait until it settles, then press ENTER..."));

  long loaded;
  if (!streamUntilEnter(loaded)) {
    Serial.println(F("READ FAILED."));
    results[idx].ok = false;
    cellCount++;
    return;
  }
  long net = loaded - offset;
  long anet = (net < 0) ? -net : net;
  Serial.print(F("  LOADED = ")); Serial.print(loaded);
  Serial.print(F("    net = ")); Serial.println(net);

  if (anet < 1000) {
    Serial.println(F("  !! BARELY RESPONDED - cell damaged or not loaded properly."));
    results[idx].ok = false;
  } else {
    long cpk = (long)((float)net / testWeight);
    if (cpk < 0) cpk = -cpk;
    results[idx].ok = true;
    results[idx].countsPerKg = cpk;
    Serial.print(F("  Cell #")); Serial.print(idx + 1);
    Serial.print(F(" factor = ")); Serial.print(cpk); Serial.print(F(" counts/kg"));
    if (net < 0) Serial.print(F("  (NEGATIVE = reversed wiring; swap green/white)"));
    Serial.println();
  }
  results[idx].offset = offset;
  results[idx].net    = net;
  cellCount++;

  Serial.println();
  Serial.println(F("Disconnect this cell. Hook up the next."));
  Serial.println(F("Press ENTER for next cell, or 'q' + ENTER to finish."));

  if (waitNextOrQuit()) {
    printSummary();
    Serial.println();
    Serial.println(F("Done. Reset the board to start over."));
    while (1) delay(10000);
  }
}

void printSummary() {
  Serial.println();
  Serial.println(F("=== SUMMARY ==="));

  long sumCpk = 0; int valid = 0;
  for (byte i = 0; i < cellCount; i++) {
    if (results[i].ok) { sumCpk += results[i].countsPerKg; valid++; }
  }
  long avgCpk = (valid > 0) ? (sumCpk / valid) : 0;
  Serial.print(F("Average counts/kg (good cells): ")); Serial.println(avgCpk);
  Serial.println();
  Serial.println(F("cell |   offset    |    net      | counts/kg  | dev"));
  Serial.println(F("-----+-------------+-------------+------------+------"));
  for (byte i = 0; i < cellCount; i++) {
    Serial.print(F("  #")); Serial.print(i + 1); Serial.print(F(" | "));
    printNum(results[i].offset, 11); Serial.print(F(" | "));
    printNum(results[i].net,    11); Serial.print(F(" | "));
    if (results[i].ok) {
      printNum(results[i].countsPerKg, 10); Serial.print(F(" | "));
      if (avgCpk > 0) {
        long diff = results[i].countsPerKg - avgCpk;
        long ad   = diff < 0 ? -diff : diff;
        int  pct  = (int)(ad * 100L / avgCpk);
        Serial.print(diff < 0 ? '-' : '+');
        Serial.print(pct); Serial.print('%');
        if (pct > 15) Serial.print(F("  <-- OUTLIER (likely damaged)"));
      }
    } else {
      Serial.print(F("    FAILED"));
    }
    Serial.println();
  }
}

void printNum(long n, byte width) {
  char buf[14];
  ltoa(n, buf, 10);
  int len = strlen(buf);
  for (int i = len; i < (int)width; i++) Serial.print(' ');
  Serial.print(buf);
}

// ---------------- setup / loop ----------------
void setup() {
  pinMode(DT_PIN, INPUT);
  pinMode(CLK_PIN, OUTPUT);
  digitalWrite(CLK_PIN, LOW);
  Serial.begin(57600);
  delay(300);

  Serial.println();
  Serial.println(F("=== BenchCal - one-HX711 cell tester ==="));
  Serial.println(F("Wiring: DT -> D4, SCK -> D3."));
  Serial.println(F("Make sure ONE HX711 is wired and a cell is connected."));
  Serial.println();
  Serial.print  (F("Test weight (kg) > "));
  testWeight = inputFloat();
  if (testWeight <= 0) {
    Serial.println(F("Need a positive number. Halting - reset to try again."));
    while (1) delay(10000);
  }
  Serial.print(F("OK, using ")); Serial.print(testWeight, 3); Serial.println(F(" kg."));
}

void loop() {
  if (cellCount >= MAX_CELLS) {
    Serial.println(F("Max cells reached."));
    printSummary();
    while (1) delay(10000);
  }
  doNextCell();
}
