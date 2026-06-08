# Cell calibration log

Per-cell `counts/g` factors from BenchCalLib / RepeatTest.
Theoretical for these 100 kg / 2 mV/V cells with HX711 g128 @ 5 V: ~42 counts/g.
Cells within ±15 % of the group average are healthy. Outliers = damaged.

| Cell | counts/g | cal weight | cal date | sketch | repeatability spread | notes |
|------|----------|------------|----------|--------|----------------------|-------|
| #1   | **+44.66** | 5520 g dumbbell | 2026-06-03 | RepeatTest | 0 g over 3 placements | healthy |
| #2   | **−43.85** | 5520 g dumbbell | 2026-06-03 | RepeatTest | 1440 g over 7 placements | A+/A− swapped vs #1 (negative sign). Magnitude matches cell #1 within 1.8 %. Repeatability poor — suspect dumbbell-on-load-end placement issue, NOT cell electrical fault. 1145 g cube was repeatable (1170, 1170). Re-test with flat plate under dumbbell + tighter clamp before flagging as damaged. |
| #3   | **+44.16** | 5520 g dumbbell | 2026-06-03 | RepeatTest | 10 g over 3 placements (5520/5520/5510); cube 0 g (1140/1140) | healthy |
| #4   | **+44.81** | 5520 g dumbbell | 2026-06-03 | RepeatTest | 40 g over 6 placements (5520/5510/5490/5490/5480/5520); cube 0 g (1120/1120) | healthy |

## Group summary (4 cells, bench)
- Average magnitude: **44.37 counts/g**.
- Spread across cells: ±1.2 % — far inside the datasheet ±10 % spec. All four gauges electrically healthy.
- 3 cells (#1, #3, #4) repeatable to ≤ 40 g on 5520 g.
- 1 cell (#2) has bad repeatability (1440 g spread) but normal sensitivity → mechanical issue suspected (dumbbell seating on load end / fixed-end clamp), not electrical.

## Earlier reference points
- BenchCalLib on cell #1 with the 1145 g cube: **44.21 counts/g** (consistent with 44.66 within 1 %).

## Procedure (per cell)
1. Wire one cell to the bench HX711 (DT=D4, SCK=D5).
2. Upload `RepeatTest.ino`.
3. Tare empty ('t'), place 5520 g dumbbell, type `5520` + Enter.
4. Note the printed `New cal factor =` → fill in the table.
5. Remove + place 3× to verify repeatability → spread should be < 50 g.
6. Disconnect, swap to next cell.
