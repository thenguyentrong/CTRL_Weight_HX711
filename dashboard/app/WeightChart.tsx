"use client";

import { useEffect, useMemo, useState } from "react";
import {
  Line,
  LineChart,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from "recharts";
import { supabase } from "../lib/supabase";

type Reading = { weight_g: number; recorded_at: string };
type RangeKey = "5min" | "1h" | "24h" | "custom";

const QUICK: { key: RangeKey; label: string; ms: number }[] = [
  { key: "5min", label: "5 min", ms: 5 * 60 * 1000 },
  { key: "1h", label: "1 h", ms: 60 * 60 * 1000 },
  { key: "24h", label: "24 h", ms: 24 * 60 * 60 * 1000 },
];

const MAX_POINTS = 1200;

function downsample<T>(arr: T[], maxPoints: number): T[] {
  if (arr.length <= maxPoints) return arr;
  const step = arr.length / maxPoints;
  const out: T[] = [];
  for (let i = 0; i < maxPoints; i++) out.push(arr[Math.floor(i * step)]);
  return out;
}

function toLocalInputValue(d: Date): string {
  const pad = (n: number) => String(n).padStart(2, "0");
  return (
    `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}` +
    `T${pad(d.getHours())}:${pad(d.getMinutes())}`
  );
}

export type ExternalRange = { from: Date; to: Date; label: string } | null;

export default function WeightChart({
  externalRange,
  onClearExternal,
}: {
  externalRange?: ExternalRange;
  onClearExternal?: () => void;
} = {}) {
  const [range, setRange] = useState<RangeKey>("1h");
  const now = useMemo(() => new Date(), []);
  const [customFrom, setCustomFrom] = useState(
    toLocalInputValue(new Date(now.getTime() - 24 * 60 * 60 * 1000))
  );
  const [customTo, setCustomTo] = useState(toLocalInputValue(now));

  const [readings, setReadings] = useState<Reading[]>([]);
  const [loading, setLoading] = useState(false);
  const [tick, setTick] = useState(0);

  // auto-refresh the live ranges every 10 s (skip when external range is pinned)
  useEffect(() => {
    if (externalRange || range === "custom") return;
    const id = setInterval(() => setTick((t) => t + 1), 10_000);
    return () => clearInterval(id);
  }, [range, externalRange]);

  useEffect(() => {
    let cancelled = false;

    let from: Date;
    let to: Date;
    if (externalRange) {
      from = externalRange.from;
      to = externalRange.to;
    } else if (range === "custom") {
      if (!customFrom || !customTo) return;
      from = new Date(customFrom);
      to = new Date(customTo);
      if (isNaN(from.getTime()) || isNaN(to.getTime()) || from >= to) return;
    } else {
      const cfg = QUICK.find((q) => q.key === range)!;
      to = new Date();
      from = new Date(to.getTime() - cfg.ms);
    }

    setLoading(true);
    supabase
      .from("readings")
      .select("weight_g, recorded_at")
      .gte("recorded_at", from.toISOString())
      .lte("recorded_at", to.toISOString())
      .order("recorded_at", { ascending: true })
      .limit(50_000)
      .then(({ data }) => {
        if (cancelled) return;
        const rows = (data ?? []) as Reading[];
        setReadings(downsample(rows, MAX_POINTS));
        setLoading(false);
      });

    return () => {
      cancelled = true;
    };
  }, [range, customFrom, customTo, tick, externalRange]);

  const chartData = useMemo(
    () =>
      readings.map((r) => ({
        t: new Date(r.recorded_at).getTime(),
        kg: r.weight_g / 1000,
      })),
    [readings]
  );

  const fmtTick = (t: number) => {
    const d = new Date(t);
    if (range === "5min" || range === "1h") {
      return d.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
    }
    if (range === "24h") {
      return d.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
    }
    return d.toLocaleDateString();
  };

  return (
    <section style={{ marginTop: "3rem" }}>
      <h2
        style={{
          fontSize: 12,
          color: "#666",
          letterSpacing: 1,
          marginBottom: "0.75rem",
        }}
      >
        WEIGHT OVER TIME
        {externalRange && (
          <span style={{ marginLeft: "0.75rem", color: "#7adfff" }}>
            · viewing: {externalRange.label}{" "}
            <button onClick={onClearExternal} style={clearBtn}>
              ← back to live
            </button>
          </span>
        )}
      </h2>

      <div
        style={{
          display: "flex",
          gap: "0.5rem",
          flexWrap: "wrap",
          alignItems: "center",
          marginBottom: "1rem",
          opacity: externalRange ? 0.4 : 1,
          pointerEvents: externalRange ? "none" : "auto",
        }}
      >
        {QUICK.map((q) => (
          <button
            key={q.key}
            onClick={() => setRange(q.key)}
            style={btnStyle(range === q.key)}
          >
            {q.label}
          </button>
        ))}
        <button
          onClick={() => setRange("custom")}
          style={btnStyle(range === "custom")}
        >
          Custom
        </button>

        {range === "custom" && (
          <div
            style={{
              display: "flex",
              gap: "0.5rem",
              alignItems: "center",
              marginLeft: "0.5rem",
              color: "#888",
              fontSize: 13,
            }}
          >
            <label>
              from{" "}
              <input
                type="datetime-local"
                value={customFrom}
                onChange={(e) => setCustomFrom(e.target.value)}
                style={inputStyle}
              />
            </label>
            <label>
              to{" "}
              <input
                type="datetime-local"
                value={customTo}
                onChange={(e) => setCustomTo(e.target.value)}
                style={inputStyle}
              />
            </label>
          </div>
        )}

        {loading && (
          <span style={{ color: "#666", fontSize: 12, marginLeft: "auto" }}>
            loading…
          </span>
        )}
      </div>

      <div
        style={{
          background: "#0f0f0f",
          border: "1px solid #1f1f1f",
          borderRadius: 8,
          padding: "1rem",
          height: 340,
        }}
      >
        {chartData.length === 0 ? (
          <div
            style={{
              height: "100%",
              display: "grid",
              placeItems: "center",
              color: "#444",
              fontSize: 13,
            }}
          >
            no data in this range
          </div>
        ) : (
          <ResponsiveContainer width="100%" height="100%">
            <LineChart data={chartData} margin={{ top: 10, right: 20, bottom: 0, left: 0 }}>
              <XAxis
                dataKey="t"
                type="number"
                domain={["dataMin", "dataMax"]}
                tickFormatter={fmtTick}
                stroke="#666"
                fontSize={11}
              />
              <YAxis
                tickFormatter={(v) => `${(v as number).toFixed(1)}`}
                stroke="#666"
                fontSize={11}
                width={40}
              />
              <Tooltip
                contentStyle={{
                  background: "#161616",
                  border: "1px solid #2a2a2a",
                  borderRadius: 6,
                  color: "#eee",
                }}
                labelFormatter={(t) => new Date(t as number).toLocaleString()}
                formatter={(v) => [`${(v as number).toFixed(2)} kg`, "weight"]}
              />
              <Line
                type="monotone"
                dataKey="kg"
                stroke="#7adfff"
                strokeWidth={2}
                dot={false}
                isAnimationActive={false}
              />
            </LineChart>
          </ResponsiveContainer>
        )}
      </div>

      <div style={{ color: "#555", fontSize: 11, marginTop: "0.5rem" }}>
        {readings.length} points · auto-refresh every 10 s (live ranges)
      </div>
    </section>
  );
}

function btnStyle(active: boolean): React.CSSProperties {
  return {
    background: active ? "#243846" : "#161616",
    color: active ? "#7adfff" : "#aaa",
    border: "1px solid " + (active ? "#3a6178" : "#222"),
    borderRadius: 6,
    padding: "0.45rem 0.9rem",
    fontSize: 13,
    cursor: "pointer",
  };
}

const inputStyle: React.CSSProperties = {
  background: "#161616",
  color: "#eee",
  border: "1px solid #2a2a2a",
  borderRadius: 4,
  padding: "0.3rem 0.5rem",
  fontSize: 12,
};

const clearBtn: React.CSSProperties = {
  background: "transparent",
  color: "#7adfff",
  border: "1px solid #2a3f4a",
  borderRadius: 4,
  padding: "0.15rem 0.5rem",
  fontSize: 11,
  cursor: "pointer",
  marginLeft: "0.5rem",
};
