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
- Each load cell -> HX711: `Red=E+`, `Black=E-`, `Green=A+`, `White=A-`

Serial Monitor: **57600 baud**.

## Status / known issues
- Drift to negative after tare (creep / thermal / settling).
- Wide fluctuation concentrated on **S1** and **S4** (suspect wiring / HX711 / mounting).
- Open question: are the load cells **half-bridge (3 wire)** or **full-bridge (4 wire)**?
  Half-bridge cells must be combined into ONE full bridge feeding ONE HX711.
