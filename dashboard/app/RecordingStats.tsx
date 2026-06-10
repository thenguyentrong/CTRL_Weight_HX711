"use client";

import { useEffect, useState } from "react";
import {
  computeDryingStats,
  fetchRecordingReadings,
  formatDuration,
  type DryingStats,
} from "../lib/analytics";

export default function RecordingStats({
  recordingId,
  label,
  live,
}: {
  recordingId: number;
  label?: string;
  live?: boolean; // recording still running -> refresh periodically
}) {
  const [stats, setStats] = useState<DryingStats | null>(null);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    let cancelled = false;
    async function load() {
      const rows = await fetchRecordingReadings(recordingId);
      if (cancelled) return;
      setStats(computeDryingStats(rows));
      setLoading(false);
    }
    setLoading(true);
    load();
    const id = live ? setInterval(load, 30_000) : null;
    return () => {
      cancelled = true;
      if (id) clearInterval(id);
    };
  }, [recordingId, live]);

  return (
    <section style={{ marginTop: "2rem" }}>
      <h2
        style={{
          fontSize: 12,
          color: "#666",
          letterSpacing: 1,
          marginBottom: "0.75rem",
        }}
      >
        DRYING / WEIGHT LOSS{label ? ` · ${label}` : ""}
      </h2>

      {loading ? (
        <div style={{ color: "#555", fontSize: 13 }}>computing…</div>
      ) : !stats ? (
        <div style={{ color: "#555", fontSize: 13 }}>
          not enough data yet for this recording
        </div>
      ) : (
        <div
          style={{
            display: "grid",
            gridTemplateColumns: "repeat(auto-fit, minmax(140px, 1fr))",
            gap: "0.75rem",
          }}
        >
          <Stat label="START" value={`${(stats.startWeightG / 1000).toFixed(2)} kg`} />
          <Stat label="CURRENT" value={`${(stats.endWeightG / 1000).toFixed(2)} kg`} />
          <Stat
            label="LOST"
            value={`${(stats.lossG / 1000).toFixed(2)} kg`}
            sub={`${stats.lossPct.toFixed(1)} %`}
            highlight
          />
          <Stat label="DURATION" value={formatDuration(stats.durationMs)} />
          <Stat label="AVG RATE" value={`${stats.avgRateGPerH.toFixed(0)} g/h`} />
          <Stat label="LAST HOUR" value={`${stats.recentRateGPerH.toFixed(0)} g/h`} />
        </div>
      )}
    </section>
  );
}

function Stat({
  label,
  value,
  sub,
  highlight,
}: {
  label: string;
  value: string;
  sub?: string;
  highlight?: boolean;
}) {
  return (
    <div
      style={{
        padding: "0.9rem 1rem",
        background: "#141414",
        border: "1px solid #1f1f1f",
        borderRadius: 8,
      }}
    >
      <div style={{ color: "#666", fontSize: 10, letterSpacing: 1 }}>{label}</div>
      <div
        style={{
          fontSize: 24,
          marginTop: 4,
          color: highlight ? "#9be39b" : "#eaeaea",
        }}
      >
        {value}
      </div>
      {sub && (
        <div style={{ color: highlight ? "#6fae6f" : "#777", fontSize: 13 }}>
          {sub}
        </div>
      )}
    </div>
  );
}
