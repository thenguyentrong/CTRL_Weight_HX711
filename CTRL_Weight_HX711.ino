/*
 * CTRL_Weight_HX711
 * --------------------------------------------------------------
 * 4x load cell scale using 4x HX711 modules on Arduino UNO.
 *
 * WIRING (current setup - separate SCK per module):
 *   S1: DT -> D4,  SCK -> D3
 *   S2: DT -> D5,  SCK -> D8
 *   S3: DT -> D6,  SCK -> D9
 *   S4: DT -> D7,  SCK -> D10
 *   All HX711 VCC -> 5V (use a clean/external 5V supply, not weak USB)
 *   All HX711 GND -> GND (common ground)
 *
 *   Each load cell -> its HX711:  Red=E+  Black=E-  Green=A+  White=A-
 *
 * Serial Monitor: 57600 baud
 *
 * STATUS: starting point. Debug/drift work in progress.
 * --------------------------------------------------------------
 */

const byte NUM_SENSORS = 4;

const byte DT_PINS[NUM_SENSORS]  = {4, 5, 6, 7};
const byte SCK_PINS[NUM_SENSORS] = {3, 8, 9, 10};

void setup() {
  Serial.begin(57600);
  delay(500);

  for (byte i = 0; i < NUM_SENSORS; i++) {
    pinMode(DT_PINS[i], INPUT);
    pinMode(SCK_PINS[i], OUTPUT);
    digitalWrite(SCK_PINS[i], LOW);
  }

  Serial.println("CTRL_Weight_HX711 ready.");
}

void loop() {
  // TODO: read sensors, tare, calibrate, output kg.
}
