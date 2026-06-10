# CLAUDE.md — context for any Claude session on this project

Read this first when entering the repo. It tells you what exists, why, and how it
all fits together. The codebase itself, `CALIBRATION.md`, and `git log` are
authoritative for current state — this file just gives you the map.

## What this project is

A DIY 4-cell platform scale:
- Arduino UNO + 4× HX711 + 4× 100 kg parallel-bar aluminum load cells in a steel
  frame.
- Each cell on its own HX711 module, separate DT/SCK pins (see wiring table in
  `README.md` and `Scale4/Scale4.ino` header).
- The Arduino streams readings over USB serial at 57600 baud.
- A Python bridge running on a 24/7 PC reads that serial, forwards data to
  Supabase, and a Next.js dashboard on Vercel shows it live to the user.

User's accuracy target is **±0.5 kg**. Storage drift / noise floor is ~30 g, so
the hardware is over-spec'd for the requirement.

## Architecture

```
Arduino UNO  ──USB serial──►  Bridge PC (Python)  ──HTTPS──►  Supabase Postgres
                                                                  │
                                                                  ├─ live        (1 row, UPDATEd at 1 Hz)
                                                                  ├─ readings    (1 row/sec, full time-series)
                                                                  ├─ weighings   (1 row per stable event)
                                                                  └─ recordings  (named time-slice bookmarks)
                                                                                       │
                                                                                  Realtime
                                                                                       │
                                                                                       ▼
                                                                              Vercel Next.js dashboard
```

## Folder map

| Path | Purpose |
|------|---------|
| `Scale4/Scale4.ino` | **Production firmware.** 4 HX711s with per-cell factors. This is what runs on the 24/7 setup. |
| `Scale4Cal/`, `Scale4Recal/` | In-situ calibration variants — used to re-cal on the assembled platform. |
| `RepeatTest/` | Single-cell repeatability tester. Used for the bench calibration of each cell. |
| `BenchCalLib/` | Library-based per-cell bench calibrator. Earlier iteration of RepeatTest. |
| `BenchCal/` | Bit-banged version of the same. Diagnostic backup. |
| `StabilityTest/` | 10-minute drift / noise baseline. Run to debug HX711 health. |
| `CALIBRATION.md` | **Per-cell calibration log.** Counts/gram for each of the 4 cells. Keep updated when re-cal'd. |
| `README.md` | Hardware overview + serial commands. |
| `supabase/schema.sql` | Full Postgres schema. |
| `supabase/02_readings.sql` | Migration: adds `readings` table to existing DBs. |
| `supabase/03_recordings.sql` | Migration: adds `recordings` table to existing DBs. |
| `bridge/bridge.py` | Serial → Supabase forwarder. Runs on the 24/7 PC. |
| `bridge/README.md` | Setup + run-as-service instructions. |
| `dashboard/` | Next.js 14 (App Router) app deployed on Vercel. |
| `dashboard/app/page.tsx` | Layout: current weight, record panel, chart, recordings list, weighings list. |
| `dashboard/app/WeightChart.tsx` | Recharts chart with 5min/1h/24h/custom range + external range override. |
| `dashboard/app/RecordPanel.tsx` | START/STOP recording controls. |
| `dashboard/app/RecordingsList.tsx` | List of past recordings; click "view" → loads into the chart. |
| `dashboard/lib/supabase.ts` | Supabase client (build-safe with placeholders if env vars missing). |

## Repos & deployments

- GitHub: `thenguyentrong/CTRL_Weight_HX711` (public).
- Vercel project: `ctrl-weight`. Root Directory = `dashboard`. Framework = Next.js.
- Supabase project: created via the Vercel × Supabase integration. The
  `NEXT_PUBLIC_SUPABASE_URL` / `NEXT_PUBLIC_SUPABASE_ANON_KEY` /
  `SUPABASE_SERVICE_ROLE_KEY` env vars are wired to Vercel automatically; the
  bridge expects the latter two in `bridge/.env`.

## Data flow contract

The bridge parses Scale4's debug-mode serial line:
```
  c1=1380g  c2=1370g  c3=1390g  c4=1360g  | total = 5500 g
```
and a fallback non-debug line:
```
  weight = 5500 g
```
Regexes live in `bridge/bridge.py` (`LINE_RE`, `SIMPLE_RE`). **Don't change the
Scale4 serial format without updating these.**

What the bridge writes:
- `live` UPDATEd ~once per second (current weight + per-cell).
- `readings` INSERTed once per second (the time-series log; `weight_g` only).
- `weighings` INSERTed once per stable event (load detected → settles for 1.5 s
  inside ±10 g → row inserted). The event state machine constants
  (`LOAD_THRESHOLD_G=100`, `UNLOAD_THRESHOLD_G=50`, `STABLE_BAND_G=10`,
  `STABLE_TIME_S=1.5`) match the ones in `RepeatTest.ino` so behavior is
  consistent.

What the dashboard writes:
- `recordings` rows (INSERT on START, UPDATE on STOP, DELETE on user click).
  These are bookmarks over the readings log — they don't store readings, they
  just store a time range plus a name.

## Calibration status

See `CALIBRATION.md` for the latest per-cell counts/gram. Brief summary:

- Cells #1, #3, #4 are healthy. Cell #2's bench repeatability was bad (1440 g
  spread) but its magnitude matches the others — almost certainly a placement /
  mounting issue, not the cell.
- The 4-cell platform's mechanical mount is the known weak point: the deck
  doesn't engage cells #1 and #2 evenly. When 85 kg of body weight is on the
  platform, those two cells see almost nothing, and the total reads ~half. **Fix
  is mechanical, not software** — shim cells #1 and #2 up until the deck rests
  on all four evenly.

## When you're asked to do X — quick paths

- **"Recalibrate"** → `Scale4Recal/Scale4Recal.ino` runs the in-situ per-cell cal
  flow. Update `CALIBRATION.md` and the `cellFactors[]` array in `Scale4.ino`
  with the new numbers.
- **"Change the serial format"** → update `Scale4/Scale4.ino` AND
  `bridge/bridge.py` LINE_RE/SIMPLE_RE together.
- **"Add a new table"** → add to `supabase/schema.sql` for the full reference,
  AND ship a numbered idempotent migration (`supabase/0N_<name>.sql`) so users
  with an existing DB can apply it.
- **"Hit Supabase free-tier limit"** → add a cron-pruning function (delete
  `readings` older than N days) or downsample to 5-second buckets. The bridge's
  `READINGS_INSERT_PERIOD_S` knob also exists.
- **"Change the cal factors"** → edit `cellFactors[]` in `Scale4/Scale4.ino`.
  Negative factor means that cell's A+/A- is wired opposite — flipping the sign
  in code is equivalent to swapping green/white at the cell.

## Communication style with this user

- They speak quickly and casually — lots of typos and shortened words. Don't
  over-correct or re-quote; just understand and act.
- They prefer **answer-first, then code**. For "should I…?" / advice messages,
  discuss briefly before implementing. Wait for an explicit go-ahead before
  writing code. (This is captured as a feedback memory but it bears repeating.)
- They work on this in short sessions, often across multiple PCs. State should
  live in the repo (this file, `CALIBRATION.md`, code), not in memory.
- Don't be overly verbose. Short, actionable updates. Show exact commands when
  giving instructions.
