import { supabase } from "./supabase";

export type Reading = { weight_g: number; recorded_at: string };

export type DryingStats = {
  count: number;
  startWeightG: number;
  endWeightG: number;
  lossG: number;
  lossPct: number;
  durationMs: number;
  avgRateGPerH: number; // total loss / total duration
  recentRateGPerH: number; // loss rate over the last hour of data
  startAt: string;
  endAt: string;
};

/** Fetch all readings owned by a recording, oldest first (full resolution). */
export async function fetchRecordingReadings(
  recordingId: number
): Promise<Reading[]> {
  const { data } = await supabase
    .from("readings")
    .select("weight_g, recorded_at")
    .eq("recording_id", recordingId)
    .order("recorded_at", { ascending: true })
    .limit(200_000);
  return (data ?? []) as Reading[];
}

/** Compute drying / evaporation stats from an oldest-first reading series. */
export function computeDryingStats(rows: Reading[]): DryingStats | null {
  if (rows.length < 2) return null;

  const first = rows[0];
  const last = rows[rows.length - 1];
  const startWeightG = first.weight_g;
  const endWeightG = last.weight_g;
  const lossG = startWeightG - endWeightG;
  const lossPct = startWeightG !== 0 ? (lossG / startWeightG) * 100 : 0;

  const startT = new Date(first.recorded_at).getTime();
  const endT = new Date(last.recorded_at).getTime();
  const durationMs = endT - startT;
  const hours = durationMs / 3_600_000;
  const avgRateGPerH = hours > 0 ? lossG / hours : 0;

  // recent rate: only the readings within the last hour of the series
  const cutoff = endT - 3_600_000;
  const recent = rows.filter(
    (r) => new Date(r.recorded_at).getTime() >= cutoff
  );
  let recentRateGPerH = 0;
  if (recent.length >= 2) {
    const rf = recent[0];
    const rl = recent[recent.length - 1];
    const rh =
      (new Date(rl.recorded_at).getTime() -
        new Date(rf.recorded_at).getTime()) /
      3_600_000;
    if (rh > 0) recentRateGPerH = (rf.weight_g - rl.weight_g) / rh;
  }

  return {
    count: rows.length,
    startWeightG,
    endWeightG,
    lossG,
    lossPct,
    durationMs,
    avgRateGPerH,
    recentRateGPerH,
    startAt: first.recorded_at,
    endAt: last.recorded_at,
  };
}

export function formatDuration(ms: number): string {
  const s = Math.max(0, Math.floor(ms / 1000));
  const d = Math.floor(s / 86400);
  const h = Math.floor((s % 86400) / 3600);
  const m = Math.floor((s % 3600) / 60);
  if (d > 0) return `${d}d ${h}h ${m}m`;
  if (h > 0) return `${h}h ${m}m`;
  return `${m}m`;
}
