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

## Behaviour (normal scale mode)
Event-driven — it does **not** stream numbers:
- On power-up it **auto-zeros** (keep the platform empty at startup).
- While empty it stays **silent** and auto-holds zero.
- Place something on it → once it settles it prints **one** line: `Weight: 7.52 kg`.
- Remove it → prints `Cleared -> 0.00 kg. Ready.` and goes quiet.

For raw per-cell debugging, turn on the verbose stream with `d`.

## Serial commands
Type the letter and press Enter:

| Key | Action |
|-----|--------|
| `h` | help |
| `t` | tare (zero) — platform must be empty |
| `c` | simple calibrate — one known weight in the middle (all cells share one factor) |
| `k` | **corner calibrate** (cells MOUNTED) — zero, then weight on each corner; solves 4×4 to account for force leakage |
| `b` | **bench calibrate** (cells OUT) — clamp each cell, put the weight on its load end; isolated per-cell cal, **best accuracy** |
| `n` | noise test — shows how shaky each sensor is (find the bad cell) |
| `i` | identify — press a corner, it tells you the sensor number (S1–S4) |
| `a` | auto-zero tracking ON/OFF |
| `d` | DEBUG stream ON/OFF (raw per-cell numbers) |
| `p` | print current calibration & offsets |
| `s` | save to EEPROM |
| `e` | erase EEPROM (back to defaults) |

Tare + calibration are stored in **EEPROM**, so they survive a reset (good for 24/7 use).

### Corner calibration (`k`) — the accurate one
Each cell has its own factor (`kgPerCount[i]`), so total weight = `Σ net_i × factor_i`.
That sum equals the true load **at any position** (statics: the 4 support forces add up
to the total). The `k` flow: enter the test weight → zero (empty) → place the weight over
**each corner** in turn, pressing a key to advance. It reads all 4 cells at each corner
(force leaks into neighbours, so it must) and solves the 4×4 system for each cell's factor.

Needs all 4 cells **repeatable** first — calibration sets counts→kg, it cannot fix a
cell whose reading jumps around.

## Drift fighting (software)
The firmware applies, in order:
- **Median** of several samples per cell → rejects wild single-sample spikes
- **Spike clamp** → ignores physically impossible one-step jumps
- **Auto-Zero Tracking (AZT)** → when the platform is empty **and** still, it slowly
  pulls each cell's zero back to 0 (the scale version of IMU complementary filtering —
  it corrects against the "empty = 0" reference). Toggle with `a`.
- **Stability lock** → shows `[STABLE]` once the reading settles.

Limitation: AZT only corrects drift while empty/known. A cell that drifts kg's **while
loaded** is a hardware fault (re-solder S1/S2 off the breadboard, clean 5V, common GND).

## Online streaming (Vercel + Supabase)

View the live scale value from any browser:
- `supabase/schema.sql` — Postgres tables (`live` singleton + `weighings` history) + RLS.
- `bridge/` — Python script that runs on the 24/7 PC, reads USB serial, throttles
  a 1 Hz live update, and inserts one row per stable weighing event.
- `dashboard/` — Next.js app deployed on Vercel that subscribes to Supabase realtime
  and renders the current weight + the last 50 weighings.

Each piece has its own README with setup steps.

## Status / known issues
- Load cells confirmed **5-wire full bridge**, ~400 Ω, all healthy → cells are NOT the problem.
- **S1 and S2** drift hard; **S3 and S4** are rock-stable. A new HX711 on S1 fixed it only
  temporarily → the fault is most likely the **connections (breadboard)**, not the chips.
- Real fix: get S1/S2's DT/SCK/VCC/GND **off the breadboard** (solder or screw terminals),
  clean 5 V, solid common ground. Software (median + auto-zero) only masks idle drift.
