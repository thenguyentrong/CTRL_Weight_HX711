/*
 * CTRL_Weight_HX711  -  4x load cell scale
 * ==================================================================
 * Hardware: Arduino UNO + 4x HX711 + 4x 5-wire FULL-BRIDGE load cell.
 *
 * WIRING  (separate SCK per module -- VERIFY THIS MATCHES YOUR BOARD!)
 *   S1: DT -> D4,  SCK -> D3
 *   S2: DT -> D5,  SCK -> D8
 *   S3: DT -> D6,  SCK -> D9
 *   S4: DT -> D7,  SCK -> D10
 *   All HX711 VCC -> 5V , GND -> GND (common). Use a clean 5V supply.
 *   Load cell (5 wire): Red=E+ Black=E- Green=A+ White=A- Yellow=Shield->GND
 *
 * DRIFT FIGHTING (software):
 *   - MEDIAN of several samples per cell  -> rejects wild spikes
 *   - SPIKE clamp                         -> ignores impossible 1-step jumps
 *   - AUTO-ZERO TRACKING (AZT)            -> holds 0 when empty & still
 *   - STABILITY lock                      -> shows [STABLE] when settled
 *   NOTE: AZT can only correct drift while the platform is EMPTY/known.
 *         It cannot fix a cell that drifts kg's WHILE loaded - that is a
 *         hardware/connection problem (re-solder S1/S2, off the breadboard).
 *
 * SERIAL: 57600 baud, line ending "Newline".
 * COMMANDS:
 *   h help | d live on/off | t tare | c calibrate | n noise test
 *   i identify corner | a auto-zero on/off | p print | s save | e erase
 * ==================================================================
 */

#include <EEPROM.h>

const byte NUM = 4;
const byte DT_PINS[NUM]  = {4, 5, 6, 7};
const byte SCK_PINS[NUM] = {3, 8, 9, 10};

// ---- persisted calibration ----
struct CalData {
  uint32_t magic;
  long     offset[NUM];   // tare offset per cell (raw counts)
  float    systemFactor;  // counts per KG for the SUM of all 4 cells
};
const uint32_t MAGIC  = 0x48583735;  // "HX75" (bumped -> resets old bad cal)
const int      EE_ADDR = 0;
CalData cal;

// ---- tuning ----
const byte  MED_N      = 5;        // samples per cell per reading (median)
const long  SPIKE_LIMIT= 200000;   // reject 1-step jump bigger than this (~10 kg)
const float EMA_A      = 0.30;     // display smoothing
const long  AZ_BAND    = 8000;     // near-zero band per cell for AZT (~0.4 kg)
const long  STAB_BAND  = 4000;     // stability window on total (~0.2 kg)
const byte  STAB_NEEDED= 6;        // consecutive stable reads to lock
const float AZ_GAIN    = 0.10;     // how fast the zero is pulled back

// ---- runtime ----
bool  liveView = true;
bool  azOn     = true;             // auto-zero tracking enabled
float azDrift[NUM] = {0,0,0,0};    // tracked zero correction (counts)
float fNet[NUM]    = {0,0,0,0};    // smoothed net per cell (counts)
bool  emaInit  = false;
long  lastRaw[NUM];
bool  haveLast = false;
float fTotal   = 0;
byte  stableCount = 0;

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
  printCal();
}

// ---------------------------------------------------------------- loop
void loop() {
  handleSerial();
  if (!liveView) { delay(50); return; }

  long raw[NUM];
  readMedianAll(raw);

  long netRaw[NUM];
  for (byte i = 0; i < NUM; i++) netRaw[i] = raw[i] - cal.offset[i];

  // smooth net (after removing tracked drift)
  float total = 0;
  for (byte i = 0; i < NUM; i++) {
    float net = netRaw[i] - azDrift[i];
    fNet[i] = emaInit ? (EMA_A * net + (1 - EMA_A) * fNet[i]) : net;
    total  += fNet[i];
  }
  emaInit = true;

  // stability detector (on the smoothed total)
  if (fabs(total - fTotal) < STAB_BAND) { if (stableCount < 250) stableCount++; }
  else stableCount = 0;
  fTotal = total;
  bool stable = stableCount >= STAB_NEEDED;

  // auto-zero tracking: only when stable AND every cell is near its zero
  bool nearZero = true;
  for (byte i = 0; i < NUM; i++)
    if (fabs(netRaw[i] - azDrift[i]) > AZ_BAND) nearZero = false;

  bool zeroing = false;
  if (azOn && stable && nearZero) {
    zeroing = true;
    for (byte i = 0; i < NUM; i++) azDrift[i] += AZ_GAIN * (netRaw[i] - azDrift[i]);
  }

  float totalKg = (cal.systemFactor != 0) ? total / cal.systemFactor : 0;

  for (byte i = 0; i < NUM; i++) {
    Serial.print(F("S")); Serial.print(i + 1); Serial.print('=');
    Serial.print((long)fNet[i]); Serial.print(' ');
  }
  Serial.print(F("| ")); Serial.print(totalKg, 3); Serial.print(F(" kg"));
  if (stable)  Serial.print(F("  [STABLE]"));
  if (zeroing) Serial.print(F("  [auto-zero]"));
  Serial.println();

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

// Median of MED_N samples per cell + spike clamp vs last accepted value.
void readMedianAll(long out[NUM]) {
  long buf[NUM][MED_N];
  for (byte k = 0; k < MED_N; k++) {
    for (byte i = 0; i < NUM; i++) {
      long r;
      if (!readRaw(i, r)) r = (k > 0) ? buf[i][k - 1] : (haveLast ? lastRaw[i] : 0);
      buf[i][k] = r;
    }
  }
  for (byte i = 0; i < NUM; i++) {
    qsort(buf[i], MED_N, sizeof(long), cmpLong);
    long m = buf[i][MED_N / 2];
    if (haveLast) {
      long d = m - lastRaw[i]; if (d < 0) d = -d;
      if (d > SPIKE_LIMIT) m = lastRaw[i];   // reject impossible jump
    }
    out[i] = m;
    lastRaw[i] = m;
  }
  haveLast = true;
}

// Average n rounds, dropping each sensor's min & max (for tare / calibrate).
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
    case 'd': liveView = !liveView;
              Serial.print(F("Live view ")); Serial.println(liveView ? F("ON") : F("OFF"));
              emaInit = false; break;
    case 'a': azOn = !azOn;
              Serial.print(F("Auto-zero tracking ")); Serial.println(azOn ? F("ON") : F("OFF")); break;
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
}

void doIdentify() {
  bool wasLive = liveView; liveView = false;
  Serial.println(F("IDENTIFY MODE:"));
  Serial.println(F("Press DOWN on ONE corner at a time - I'll name its sensor."));
  Serial.println(F("Send any key + Enter to stop."));

  long base[NUM];
  if (!readAvgAll(15, base)) { Serial.println(F("read failed - check wiring")); liveView = wasLive; return; }
  float fbase[NUM];
  for (byte i = 0; i < NUM; i++) fbase[i] = base[i];
  const long PRESS = 50000;

  while (!Serial.available()) {
    long raw[NUM];
    if (!readRound(raw)) continue;
    long d[NUM], bestAbs = 0; byte best = 0;
    for (byte i = 0; i < NUM; i++) {
      d[i] = raw[i] - (long)fbase[i];
      long a = (d[i] < 0) ? -d[i] : d[i];
      if (a > bestAbs) { bestAbs = a; best = i; }
    }
    if (bestAbs < PRESS) {
      for (byte i = 0; i < NUM; i++) fbase[i] = 0.97 * fbase[i] + 0.03 * raw[i];
    } else {
      Serial.print(F(">>> corner = S")); Serial.print(best + 1);
      if (d[best] < 0) Serial.print(F("  (NEGATIVE = this cell is wired reversed!)"));
      Serial.print(F("   ["));
      for (byte i = 0; i < NUM; i++) { Serial.print(F("S")); Serial.print(i + 1); Serial.print('='); Serial.print(d[i]); if (i < NUM - 1) Serial.print(' '); }
      Serial.println(']');
    }
    delay(120);
  }
  while (Serial.available()) Serial.read();
  Serial.println(F("Identify stopped."));
  liveView = wasLive;
}

void doTare() {
  bool wasLive = liveView; liveView = false;
  Serial.println(F("TARE: keep platform EMPTY and still..."));
  delay(1500);
  long o[NUM];
  if (readAvgAll(20, o)) {
    for (byte i = 0; i < NUM; i++) cal.offset[i] = o[i];
    resetRuntime();
    Serial.println(F("Tare done (zeroed)."));
    saveEEPROM();
    printCal();
  } else {
    Serial.println(F("Tare FAILED - sensor not responding. Check wiring."));
  }
  liveView = wasLive;
}

void doCalibrate() {
  bool wasLive = liveView; liveView = false;
  Serial.println(F("CALIBRATE:"));
  Serial.println(F("  1) Tare first (empty platform) if you have not."));
  Serial.println(F("  2) Put a KNOWN weight on the platform."));
  Serial.println(F("  3) Type its weight in kg (e.g. 7.5) and press Enter."));
  Serial.print  (F("  weight kg> "));

  float knownKg = readSerialFloat();
  if (knownKg <= 0) { Serial.println(F("\nCancelled (need a number > 0).")); liveView = wasLive; return; }
  Serial.print(knownKg, 3); Serial.println(F(" kg"));

  Serial.println(F("Reading..."));
  long raw[NUM];
  if (!readAvgAll(20, raw)) { Serial.println(F("Calibration FAILED - read error.")); liveView = wasLive; return; }

  long netSum = 0;
  for (byte i = 0; i < NUM; i++) netSum += (raw[i] - cal.offset[i]);
  Serial.print(F("Per-cell net: "));
  for (byte i = 0; i < NUM; i++) { Serial.print(F("S")); Serial.print(i + 1); Serial.print('='); Serial.print(raw[i] - cal.offset[i]); Serial.print(' '); }
  Serial.println();

  if (netSum == 0) { Serial.println(F("Total net is 0 - no weight detected.")); liveView = wasLive; return; }
  if (netSum < 0)  Serial.println(F("NOTE: total net NEGATIVE -> a cell may be reversed."));

  cal.systemFactor = (float)netSum / knownKg;
  Serial.print(F("New systemFactor (counts/kg) = ")); Serial.println(cal.systemFactor, 2);
  saveEEPROM();
  Serial.println(F("Remove weight - should read ~0. Put back to verify."));
  resetRuntime();
  liveView = wasLive;
}

void doNoiseTest() {
  bool wasLive = liveView; liveView = false;
  const int N = 60;
  Serial.print(F("NOISE test (")); Serial.print(N);
  Serial.println(F(" samples) - keep platform empty & still..."));
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
  if (got == 0) { Serial.println(F("NO DATA - check wiring.")); liveView = wasLive; return; }
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
  liveView = wasLive;
}

float readSerialFloat() {
  while (Serial.available()) Serial.read();
  String s = ""; unsigned long t0 = millis();
  while (true) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') { if (s.length()) break; }
      else s += c;
      t0 = millis();
    }
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

// ------------------------------------------------------------- prints
void printCal() {
  Serial.println(F("---- calibration ----"));
  for (byte i = 0; i < NUM; i++) { Serial.print(F("  S")); Serial.print(i + 1); Serial.print(F(" offset=")); Serial.println(cal.offset[i]); }
  Serial.print(F("  systemFactor (counts/kg) = ")); Serial.println(cal.systemFactor, 2);
  Serial.print(F("  auto-zero: ")); Serial.println(azOn ? F("ON") : F("OFF"));
  Serial.println(F("---------------------"));
}
void printHelp() {
  Serial.println(F("Cmds: h help | d live | t tare | c calibrate | n noise | i identify | a auto-zero | p print | s save | e erase"));
}
