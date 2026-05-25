/*
 * CTRL_Weight_HX711  -  4x load cell scale (event-driven)
 * ==================================================================
 * Hardware: Arduino UNO + 4x HX711 + 4x 5-wire FULL-BRIDGE load cell.
 *
 * WIRING (separate SCK per module):
 *   S1: DT->D4 SCK->D3 | S2: DT->D5 SCK->D8 | S3: DT->D6 SCK->D9 | S4: DT->D7 SCK->D10
 *   All HX711 VCC->5V, GND->GND (common). Cell: Red=E+ Black=E- Green=A+ White=A- Yellow=shield->GND
 *
 * BEHAVIOUR (this is the normal "scale" mode):
 *   - On power-up it zeroes itself (keep the platform EMPTY at startup).
 *   - While empty it stays SILENT and auto-holds zero.
 *   - When something is placed on it, once the reading settles it prints ONE line:
 *         Weight: 7.52 kg
 *   - When removed it prints  "Cleared -> 0.00 kg. Ready."  and goes quiet again.
 *
 * SERIAL: 57600 baud, line ending "Newline".
 * COMMANDS:
 *   h help | t tare(zero) | c calibrate | n noise test | i identify corner
 *   a auto-zero on/off | d DEBUG stream on/off (raw per-cell) | p print | s save | e erase
 * ==================================================================
 */

#include <EEPROM.h>

const byte NUM = 4;
const byte DT_PINS[NUM]  = {4, 5, 6, 7};
const byte SCK_PINS[NUM] = {3, 8, 9, 10};

struct CalData {
  uint32_t magic;
  long     offset[NUM];
  float    systemFactor;   // counts per KG for the SUM of all cells
};
const uint32_t MAGIC  = 0x48583735;  // "HX75"
const int      EE_ADDR = 0;
CalData cal;

// ---- tuning ----
const byte  MED_N       = 5;       // samples per cell per reading (median)
const long  SPIKE_LIMIT = 200000;  // reject 1-step jump bigger than this (~10 kg)
const float EMA_A       = 0.30;    // smoothing
const long  AZ_BAND     = 8000;    // near-zero band per cell for auto-zero (~0.4 kg)
const long  STAB_BAND   = 4000;    // stability window on total (~0.2 kg)
const byte  STAB_NEEDED = 6;       // consecutive stable reads to "settle"
const float AZ_GAIN     = 0.10;    // how fast the zero is pulled back

const float ONSET_KG    = 3.0;     // something placed if total exceeds this
const float CLEAR_KG    = 1.0;     // considered removed/empty below this
const float RECHECK_KG  = 2.0;     // re-measure if load changes this much after report
const unsigned long WEIGH_TIMEOUT = 8000; // report anyway if not settled in this time

// ---- runtime ----
bool  debugStream = false;         // 'd' -> verbose per-cell stream (for debugging)
bool  azOn        = true;          // auto-zero tracking
float azDrift[NUM] = {0,0,0,0};
float fNet[NUM]    = {0,0,0,0};
bool  emaInit  = false;
long  lastRaw[NUM];
bool  haveLast = false;
float fTotal   = 0;
byte  stableCount = 0;

enum ScaleState { S_READY, S_WEIGH, S_DONE };
ScaleState scaleState = S_READY;
float reportedKg = 0;
unsigned long weighStart = 0;

// ---------------------------------------------------------------- setup
void setup() {
  Serial.begin(57600);
  delay(300);
  for (byte i = 0; i < NUM; i++) {
    pinMode(DT_PINS[i], INPUT);
    pinMode(SCK_PINS[i], OUTPUT);
    digitalWrite(SCK_PINS[i], LOW);
  }
  Serial.println();
  Serial.println(F("=== CTRL_Weight_HX711 ==="));
  loadEEPROM();
  printHelp();

  // auto-zero at startup (platform must be empty)
  Serial.println(F("Starting - keep platform EMPTY..."));
  delay(1500);
  long o[NUM];
  if (readAvgAll(20, o)) for (byte i = 0; i < NUM; i++) cal.offset[i] = o[i];
  resetRuntime();
  Serial.println(F("Ready. Place something on the platform to weigh."));
}

// ---------------------------------------------------------------- loop
void loop() {
  handleSerial();

  long raw[NUM];
  readMedianAll(raw);

  long netRaw[NUM];
  float total = 0;
  for (byte i = 0; i < NUM; i++) netRaw[i] = raw[i] - cal.offset[i];
  for (byte i = 0; i < NUM; i++) {
    float net = netRaw[i] - azDrift[i];
    fNet[i] = emaInit ? (EMA_A * net + (1 - EMA_A) * fNet[i]) : net;
    total  += fNet[i];
  }
  emaInit = true;

  if (fabs(total - fTotal) < STAB_BAND) { if (stableCount < 250) stableCount++; }
  else stableCount = 0;
  fTotal = total;
  bool stable = stableCount >= STAB_NEEDED;
  float totalKg = (cal.systemFactor != 0) ? total / cal.systemFactor : 0;

  // ---- DEBUG stream mode (only if you turn it on with 'd') ----
  if (debugStream) {
    for (byte i = 0; i < NUM; i++) { Serial.print(F("S")); Serial.print(i + 1); Serial.print('='); Serial.print((long)fNet[i]); Serial.print(' '); }
    Serial.print(F("| ")); Serial.print(totalKg, 3); Serial.print(F(" kg"));
    if (stable) Serial.print(F("  [STABLE]"));
    Serial.println();
    delay(120);
    return;
  }

  // ---- normal SCALE mode (quiet, event-driven) ----
  bool nearZero = true;
  for (byte i = 0; i < NUM; i++) if (fabs(netRaw[i] - azDrift[i]) > AZ_BAND) nearZero = false;

  switch (scaleState) {
    case S_READY:
      if (azOn && stable && nearZero)
        for (byte i = 0; i < NUM; i++) azDrift[i] += AZ_GAIN * (netRaw[i] - azDrift[i]);
      if (totalKg > ONSET_KG) { scaleState = S_WEIGH; stableCount = 0; weighStart = millis(); }
      break;

    case S_WEIGH:
      if (stable || millis() - weighStart > WEIGH_TIMEOUT) {
        reportedKg = totalKg;
        Serial.print(F("Weight: "));
        if (!stable) Serial.print('~');
        Serial.print(totalKg, 2);
        Serial.print(F(" kg"));
        if (!stable) Serial.print(F("  (still moving)"));
        Serial.println();
        scaleState = S_DONE;
      }
      break;

    case S_DONE:
      if (totalKg < CLEAR_KG) {
        Serial.println(F("Cleared -> 0.00 kg. Ready."));
        scaleState = S_READY; stableCount = 0;
      } else if (fabs(totalKg - reportedKg) > RECHECK_KG) {
        scaleState = S_WEIGH; stableCount = 0; weighStart = millis();
      }
      break;
  }
  delay(120);
}

// ------------------------------------------------------- HX711 reading
bool readRaw(byte i, long &out) {
  byte dt = DT_PINS[i], sck = SCK_PINS[i];
  unsigned long t0 = millis();
  while (digitalRead(dt) == HIGH) { if (millis() - t0 > 200) return false; }
  unsigned long v = 0;
  for (byte b = 0; b < 24; b++) {
    digitalWrite(sck, HIGH); delayMicroseconds(1);
    v = (v << 1);
    if (digitalRead(dt)) v++;
    digitalWrite(sck, LOW); delayMicroseconds(1);
  }
  digitalWrite(sck, HIGH); delayMicroseconds(1);
  digitalWrite(sck, LOW);  delayMicroseconds(1);
  if (v & 0x800000UL) v |= 0xFF000000UL;
  out = (long)v;
  return true;
}

bool readRound(long raw[NUM]) {
  bool ok = true;
  for (byte i = 0; i < NUM; i++) if (!readRaw(i, raw[i])) { ok = false; raw[i] = 0; }
  return ok;
}

static int cmpLong(const void *a, const void *b) {
  long la = *(const long *)a, lb = *(const long *)b;
  return (la > lb) - (la < lb);
}

void readMedianAll(long out[NUM]) {
  long buf[NUM][MED_N];
  for (byte k = 0; k < MED_N; k++)
    for (byte i = 0; i < NUM; i++) {
      long r;
      if (!readRaw(i, r)) r = (k > 0) ? buf[i][k - 1] : (haveLast ? lastRaw[i] : 0);
      buf[i][k] = r;
    }
  for (byte i = 0; i < NUM; i++) {
    qsort(buf[i], MED_N, sizeof(long), cmpLong);
    long m = buf[i][MED_N / 2];
    if (haveLast) { long d = m - lastRaw[i]; if (d < 0) d = -d; if (d > SPIKE_LIMIT) m = lastRaw[i]; }
    out[i] = m;
    lastRaw[i] = m;
  }
  haveLast = true;
}

bool readAvgAll(int n, long out[NUM]) {
  if (n < 3) n = 3;
  long sum[NUM] = {0,0,0,0}, mn[NUM], mx[NUM];
  bool first = true; int got = 0;
  for (int k = 0; k < n; k++) {
    long r[NUM]; bool ok = true;
    for (byte i = 0; i < NUM; i++) if (!readRaw(i, r[i])) ok = false;
    if (!ok) continue;
    if (first) { for (byte i = 0; i < NUM; i++) mn[i] = mx[i] = r[i]; first = false; }
    else for (byte i = 0; i < NUM; i++) { if (r[i] < mn[i]) mn[i] = r[i]; if (r[i] > mx[i]) mx[i] = r[i]; }
    for (byte i = 0; i < NUM; i++) sum[i] += r[i];
    got++;
  }
  if (got < 3) return false;
  for (byte i = 0; i < NUM; i++) out[i] = (sum[i] - mn[i] - mx[i]) / (got - 2);
  return true;
}

// ------------------------------------------------------------- commands
void handleSerial() {
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c == '\n' || c == '\r' || c == ' ') return;
  switch (tolower(c)) {
    case 'h': case '?': printHelp(); break;
    case 'd': debugStream = !debugStream;
              Serial.print(F("Debug stream ")); Serial.println(debugStream ? F("ON") : F("OFF"));
              emaInit = false; break;
    case 'a': azOn = !azOn;
              Serial.print(F("Auto-zero ")); Serial.println(azOn ? F("ON") : F("OFF")); break;
    case 't': doTare(); break;
    case 'c': doCalibrate(); break;
    case 'n': doNoiseTest(); break;
    case 'i': doIdentify(); break;
    case 'p': printCal(); break;
    case 's': saveEEPROM(); break;
    case 'e': setDefaults(); saveEEPROM(); resetRuntime(); Serial.println(F("Erased -> defaults.")); break;
    default : Serial.print(F("Unknown command: ")); Serial.println(c); printHelp(); break;
  }
}

void resetRuntime() {
  for (byte i = 0; i < NUM; i++) { azDrift[i] = 0; fNet[i] = 0; }
  emaInit = false; haveLast = false; stableCount = 0; fTotal = 0;
  scaleState = S_READY; reportedKg = 0;
}

void doTare() {
  Serial.println(F("TARE: keep platform EMPTY and still..."));
  delay(1500);
  long o[NUM];
  if (readAvgAll(20, o)) {
    for (byte i = 0; i < NUM; i++) cal.offset[i] = o[i];
    resetRuntime();
    Serial.println(F("Tare done. Ready."));
    saveEEPROM();
  } else Serial.println(F("Tare FAILED - sensor not responding."));
}

void doCalibrate() {
  Serial.println(F("CALIBRATE:"));
  Serial.println(F("  1) Tare first (empty)."));
  Serial.println(F("  2) Put a KNOWN weight on the platform."));
  Serial.println(F("  3) Type its weight in kg (e.g. 7.5) and Enter."));
  Serial.print  (F("  weight kg> "));
  float knownKg = readSerialFloat();
  if (knownKg <= 0) { Serial.println(F("\nCancelled.")); return; }
  Serial.print(knownKg, 3); Serial.println(F(" kg"));
  Serial.println(F("Reading..."));
  long raw[NUM];
  if (!readAvgAll(20, raw)) { Serial.println(F("Calibration FAILED - read error.")); return; }
  long netSum = 0;
  for (byte i = 0; i < NUM; i++) netSum += (raw[i] - cal.offset[i]);
  Serial.print(F("Per-cell net: "));
  for (byte i = 0; i < NUM; i++) { Serial.print(F("S")); Serial.print(i + 1); Serial.print('='); Serial.print(raw[i] - cal.offset[i]); Serial.print(' '); }
  Serial.println();
  if (netSum == 0) { Serial.println(F("No weight detected.")); return; }
  if (netSum < 0)  Serial.println(F("NOTE: total net NEGATIVE -> a cell may be reversed."));
  cal.systemFactor = (float)netSum / knownKg;
  Serial.print(F("New systemFactor (counts/kg) = ")); Serial.println(cal.systemFactor, 2);
  saveEEPROM();
  resetRuntime();
  Serial.println(F("Remove weight - should go to ~0. Ready."));
}

void doNoiseTest() {
  const int N = 60;
  Serial.print(F("NOISE test (")); Serial.print(N); Serial.println(F(" samples) - empty & still..."));
  long mn[NUM], mx[NUM], sum[NUM] = {0,0,0,0};
  bool first = true; int got = 0;
  for (int k = 0; k < N; k++) {
    long r[NUM]; bool ok = true;
    for (byte i = 0; i < NUM; i++) if (!readRaw(i, r[i])) ok = false;
    if (!ok) continue;
    if (first) { for (byte i = 0; i < NUM; i++) mn[i] = mx[i] = r[i]; first = false; }
    else for (byte i = 0; i < NUM; i++) { if (r[i] < mn[i]) mn[i] = r[i]; if (r[i] > mx[i]) mx[i] = r[i]; }
    for (byte i = 0; i < NUM; i++) sum[i] += r[i];
    got++;
  }
  if (got == 0) { Serial.println(F("NO DATA - check wiring.")); return; }
  for (byte i = 0; i < NUM; i++) {
    long pp = mx[i] - mn[i];
    float ppkg = (cal.systemFactor != 0) ? pp / cal.systemFactor : 0;
    Serial.print(F("S")); Serial.print(i + 1);
    Serial.print(F(" avg=")); Serial.print(sum[i] / got);
    Serial.print(F(" p2p=")); Serial.print(pp);
    Serial.print(F(" (")); Serial.print(ppkg, 3); Serial.print(F(" kg)"));
    if (ppkg > 0.20) Serial.print(F("   <<< NOISY"));
    Serial.println();
  }
}

void doIdentify() {
  Serial.println(F("IDENTIFY: press ONE corner at a time. Any key + Enter to stop."));
  long base[NUM];
  if (!readAvgAll(15, base)) { Serial.println(F("read failed")); return; }
  float fbase[NUM];
  for (byte i = 0; i < NUM; i++) fbase[i] = base[i];
  const long PRESS = 50000;
  while (!Serial.available()) {
    long raw[NUM];
    if (!readRound(raw)) continue;
    long d[NUM], bestAbs = 0; byte best = 0;
    for (byte i = 0; i < NUM; i++) { d[i] = raw[i] - (long)fbase[i]; long a = (d[i] < 0) ? -d[i] : d[i]; if (a > bestAbs) { bestAbs = a; best = i; } }
    if (bestAbs < PRESS) { for (byte i = 0; i < NUM; i++) fbase[i] = 0.97 * fbase[i] + 0.03 * raw[i]; }
    else {
      Serial.print(F(">>> corner = S")); Serial.print(best + 1);
      if (d[best] < 0) Serial.print(F("  (reversed wiring!)"));
      Serial.println();
    }
    delay(150);
  }
  while (Serial.available()) Serial.read();
  Serial.println(F("Identify stopped."));
}

float readSerialFloat() {
  while (Serial.available()) Serial.read();
  String s = ""; unsigned long t0 = millis();
  while (true) {
    if (Serial.available()) { char c = Serial.read(); if (c == '\n' || c == '\r') { if (s.length()) break; } else s += c; t0 = millis(); }
    if (millis() - t0 > 30000) return -1;
  }
  return s.toFloat();
}

// ------------------------------------------------------------- EEPROM
void setDefaults() {
  cal.magic = MAGIC;
  for (byte i = 0; i < NUM; i++) cal.offset[i] = 0;
  cal.systemFactor = 20189.4;
}
void loadEEPROM() {
  CalData tmp; EEPROM.get(EE_ADDR, tmp);
  if (tmp.magic == MAGIC) { cal = tmp; Serial.println(F("Loaded calibration from EEPROM.")); }
  else { setDefaults(); Serial.println(F("No saved calibration - using defaults.")); }
}
void saveEEPROM() { cal.magic = MAGIC; EEPROM.put(EE_ADDR, cal); Serial.println(F("Saved to EEPROM.")); }

void printCal() {
  Serial.println(F("---- calibration ----"));
  for (byte i = 0; i < NUM; i++) { Serial.print(F("  S")); Serial.print(i + 1); Serial.print(F(" offset=")); Serial.println(cal.offset[i]); }
  Serial.print(F("  systemFactor (counts/kg) = ")); Serial.println(cal.systemFactor, 2);
  Serial.print(F("  auto-zero: ")); Serial.println(azOn ? F("ON") : F("OFF"));
  Serial.println(F("---------------------"));
}
void printHelp() {
  Serial.println(F("Cmds: h help | t tare | c calibrate | n noise | i identify | a auto-zero | d debug-stream | p print | s save | e erase"));
}
