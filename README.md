# CTRL_Weight_HX711

4x load cell scale built with **4x HX711** modules on an **Arduino UNO**.
Goal: a stable 24/7 weight readout in kg.

## Hardware
- Arduino UNO
- 4x load cell + 4x HX711 amplifier module
- Clean/external 5V supply (weak USB causes noise/drift)

## Wiring (separate SCK per module)
| Sensor | DT pin | SCK pin |
|--------|--------|---------|
| S1     | D4     | D3      |
| S2     | D5     | D8      |
| S3     | D6     | D9      |
| S4     | D7     | D10     |

- All HX711 `VCC` -> 5V, all `GND` -> common GND
- Each load cell (5-wire **full bridge**) -> HX711:
  `Red=E+`, `Black=E-`, `Green=A+`, `White=A-`, `Yellow=Shield -> GND`

Serial Monitor: **57600 baud**, line ending **Newline**.

## Serial commands
Type the letter and press Enter:

| Key | Action |
|-----|--------|
| `h` | help |
| `d` | live readings ON/OFF |
| `t` | tare (zero) — platform must be empty |
| `c` | calibrate with a known weight on the platform |
| `n` | noise test — shows how shaky each sensor is (find the bad cell) |
| `i` | identify — press a corner, it tells you the sensor number (S1–S4) |
| `p` | print current calibration & offsets |
| `s` | save to EEPROM |
| `e` | erase EEPROM (back to defaults) |

Tare + calibration are stored in **EEPROM**, so they survive a reset (good for 24/7 use).

Calibration uses one **systemFactor** (counts/kg of the summed signal): put a known
weight on the platform, type its kg — works for "stand on it to calibrate".

## Status / known issues
- Load cells confirmed **5-wire full bridge** → one HX711 per cell is correct.
- Drift to negative after tare (creep / thermal / settling) — tare only when empty & settled.
- Wide fluctuation concentrated on **S1** and **S4** (suspect wiring / HX711 / mounting).
  Use the `n` noise test to confirm which cell is bad, then swap modules to isolate.
