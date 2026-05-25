/*
 * CTRL_Weight_HX711  -  4x load cell scale (calibration + live debug)
 * ==================================================================
 * Hardware: Arduino UNO + 4x HX711 + 4x 5-wire FULL-BRIDGE load cell.
 *
 * WIRING  (separate SCK per module -- VERIFY THIS MATCHES YOUR BOARD!)
 *   S1: DT -> D4,  SCK -> D3
 *   S2: DT -> D5,  SCK -> D8
 *   S3: DT -> D6,  SCK -> D9
 *   S4: DT -> D7,  SCK -> D10
 *   All HX711 VCC -> 5V , GND -> GND (common). Use a clean 5V supply.
 *
 * Load cell (5 wire) -> its HX711:
 *   Red = E+ , Black = E- , Green = A+ , White = A- , Yellow = Shield -> GND
 *
 * SERIAL: 57600 baud. Set line ending to "Newline" in Serial Monitor.
 *
 * COMMANDS (type the letter, press Enter):
 *   h  : help
 *   d  : live readings ON/OFF
 *   t  : TARE (zero) - platform MUST be empty
 *   c  : CALIBRATE with a known weight on the platform
 *   n  : NOISE test - shows how shaky each sensor is (find bad cell)
 *   p  : print current calibration & offsets
 *   s  : save to EEPROM
 *   e  : erase EEPROM (back to defaults)
 *
 * Tare and calibration are saved to EEPROM, so they survive a reset.
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
const uint32_t MAGIC  = 0x48583734;  // "HX74"
const int      EE_ADDR = 0;

CalData cal;

bool  liveView  = true;
bool  emaInit   = false;
float emaKg[NUM];
const float EMA_A = 0.20;   // display smoothing (0..1, higher = snappier)

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
  bool ok = readRound(raw);

  float total = 0;
  for (byte i = 0; i < NUM; i++) {
    float kg = (cal.systemFactor != 0)
                 ? (float)(raw[i] - cal.offset[i]) / cal.systemFactor
                 : 0;
    emaKg[i] = emaInit ? (EMA_A * kg + (1 - EMA_A) * emaKg[i]) : kg;
    total   += emaKg[i];
  }
  emaInit = true;

  if (!ok) Serial.println(F("[!] read error on a sensor - check wiring"));

  for (byte i = 0; i < NUM; i++) {
    Serial.print(F("S")); Serial.print(i + 1);
    Serial.print(F(" net=")); Serial.print(raw[i] - cal.offset[i]);
    Serial.print(F(" (")); Serial.print(emaKg[i], 3); Serial.print(F("kg) | "));
  }
  Serial.print(F("TOTAL ")); Serial.print(total, 3); Serial.println(F(" kg"));

  delay(200);
}

// ------------------------------------------------------- HX711 reading
// Read one 24-bit sample from sensor i. Returns false on timeout.
bool readRaw(byte i, long &out) {
  byte dt = DT_PINS[i], sck = SCK_PINS[i];

  unsigned long t0 = millis();
  while (digitalRead(dt) == HIGH) {
    if (millis() - t0 > 200) return false;   // not ready
  }

  unsigned long v = 0;
  for (byte b = 0; b < 24; b++) {
    digitalWrite(sck, HIGH);
    delayMicroseconds(1);
    v = (v << 1);
    if (digitalRead(dt)) v++;
    digitalWrite(sck, LOW);
    delayMicroseconds(1);
  }
  // 25th pulse -> channel A, gain 128
  digitalWrite(sck, HIGH);
  delayMicroseconds(1);
  digitalWrite(sck, LOW);
  delayMicroseconds(1);

  if (v & 0x800000UL) v |= 0xFF000000UL;     // sign-extend 24->32 bit
  out = (long)v;
  return true;
}

// Read all sensors once. All 4 HX711 sample together, so after the first
// one is ready the others are ready too -> a whole round is ~100 ms.
bool readRound(long raw[NUM]) {
  bool ok = true;
  for (byte i = 0; i < NUM; i++) {
    if (!readRaw(i, raw[i])) { ok = false; raw[i] = 0; }
  }
  return ok;
}

// Average n rounds, dropping each sensor's min & max (spike rejection).
bool readAvgAll(int n, long out[NUM]) {
  if (n < 3) n = 3;
  long sum[NUM] = {0, 0, 0, 0};
  long mn[NUM], mx[NUM];
  bool first = true;
  int  got = 0;

  for (int k = 0; k < n; k++) {
    long r[NUM];
    bool roundOk = true;
    for (byte i = 0; i < NUM; i++) if (!readRaw(i, r[i])) roundOk = false;
    if (!roundOk) continue;

    if (first) {
      for (byte i = 0; i < NUM; i++) { mn[i] = mx[i] = r[i]; }
      first = false;
    } else {
      for (byte i = 0; i < NUM; i++) {
        if (r[i] < mn[i]) mn[i] = r[i];
        if (r[i] > mx[i]) mx[i] = r[i];
      }
    }
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
    case 't': doTare(); break;
    case 'c': doCalibrate(); break;
    case 'n': doNoiseTest(); break;
    case 'p': printCal(); break;
    case 's': saveEEPROM(); break;
    case 'e': setDefaults(); saveEEPROM(); Serial.println(F("Erased -> defaults.")); break;
    default : Serial.print(F("Unknown command: ")); Serial.println(c); printHelp(); break;
  }
}

void doTare() {
  bool wasLive = liveView; liveView = false;
  Serial.println(F("TARE: keep platform EMPTY and still..."));
  delay(1500);

  long o[NUM];
  if (readAvgAll(20, o)) {
    for (byte i = 0; i < NUM; i++) cal.offset[i] = o[i];
    emaInit = false;
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
  Serial.println(F("  3) Type its weight in kg (e.g. 20.0) and press Enter."));
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

  if (netSum == 0) { Serial.println(F("Total net is 0 - no weight detected. Aborted.")); liveView = wasLive; return; }
  if (netSum < 0)  Serial.println(F("NOTE: total net is NEGATIVE -> a cell may be reversed (swap its green/white)."));

  cal.systemFactor = (float)netSum / knownKg;
  Serial.print(F("New systemFactor (counts/kg) = ")); Serial.println(cal.systemFactor, 2);
  saveEEPROM();
  Serial.println(F("Remove the weight - it should read ~0 kg. Then put it back to verify."));
  emaInit = false;
  liveView = wasLive;
}

void doNoiseTest() {
  bool wasLive = liveView; liveView = false;
  const int N = 60;
  Serial.print(F("NOISE test (")); Serial.print(N);
  Serial.println(F(" samples) - keep platform empty & still..."));

  long mn[NUM], mx[NUM], sum[NUM] = {0, 0, 0, 0};
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
  Serial.println(F("p2p = peak-to-peak noise. A good cell is small & similar to the others."));
  liveView = wasLive;
}

// Wait for a number on Serial (30 s timeout). Returns -1 on timeout.
float readSerialFloat() {
  while (Serial.available()) Serial.read();   // flush old input
  String s = "";
  unsigned long t0 = millis();
  while (true) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') { if (s.length()) break; }
      else { s += c; }
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
  cal.systemFactor = 20189.4;   // counts/kg, from your earlier ~20.1894 counts/g
}

void loadEEPROM() {
  CalData tmp;
  EEPROM.get(EE_ADDR, tmp);
  if (tmp.magic == MAGIC) { cal = tmp; Serial.println(F("Loaded calibration from EEPROM.")); }
  else { setDefaults(); Serial.println(F("No saved calibration - using defaults.")); }
}

void saveEEPROM() {
  cal.magic = MAGIC;
  EEPROM.put(EE_ADDR, cal);
  Serial.println(F("Saved to EEPROM."));
}

// ------------------------------------------------------------- prints
void printCal() {
  Serial.println(F("---- calibration ----"));
  for (byte i = 0; i < NUM; i++) {
    Serial.print(F("  S")); Serial.print(i + 1);
    Serial.print(F(" offset=")); Serial.println(cal.offset[i]);
  }
  Serial.print(F("  systemFactor (counts/kg) = ")); Serial.println(cal.systemFactor, 2);
  Serial.println(F("---------------------"));
}

void printHelp() {
  Serial.println(F("Commands: h=help d=live t=tare c=calibrate n=noise p=print s=save e=erase"));
}
