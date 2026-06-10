"use client";

import { useEffect, useState } from "react";
import { supabase } from "../lib/supabase";

export type Recording = {
  id: number;
  name: string | null;
  started_at: string;
  stopped_at: string | null;
};

export default function RecordingsList({
  onView,
}: {
  onView: (r: Recording) => void;
}) {
  const [items, setItems] = useState<Recording[]>([]);

  useEffect(() => {
    let cancelled = false;
    supabase
      .from("recordings")
      .select("*")
      .order("started_at", { ascending: false })
      .limit(100)
      .then(({ data }) => {
        if (!cancelled && data) setItems(data as Recording[]);
      });

    const ch = supabase
      .channel("recordings")
      .on(
        "postgres_changes",
        { event: "*", schema: "public", table: "recordings" },
        () => {
          supabase
            .from("recordings")
            .select("*")
            .order("started_at", { ascending: false })
            .limit(100)
            .then(({ data }) => {
              if (data) setItems(data as Recording[]);
            });
        }
      )
      .subscribe();

    return () => {
      cancelled = true;
      supabase.removeChannel(ch);
    };
  }, []);

  async function remove(id: number) {
    if (!confirm("Delete this recording?")) return;
    const { error } = await supabase.from("recordings").delete().eq("id", id);
    if (error) alert("Delete failed: " + error.message);
  }

  return (
    <section style={{ marginTop: "3rem" }}>
      <h2
        style={{
          fontSize: 12,
          color: "#666",
          letterSpacing: 1,
          marginBottom: "0.5rem",
        }}
      >
        RECORDINGS
      </h2>
      <table style={{ width: "100%", borderCollapse: "collapse" }}>
        <thead>
          <tr style={{ color: "#666", textAlign: "left", fontSize: 12 }}>
            <th style={{ padding: "0.5rem 0", fontWeight: 400 }}>name</th>
            <th style={{ padding: "0.5rem 0", fontWeight: 400 }}>started</th>
            <th style={{ padding: "0.5rem 0", fontWeight: 400 }}>duration</th>
            <th style={{ padding: "0.5rem 0", fontWeight: 400 }}></th>
          </tr>
        </thead>
        <tbody>
          {items.length === 0 && (
            <tr>
              <td colSpan={4} style={{ padding: "1rem 0", color: "#444" }}>
                no recordings yet — hit START above
              </td>
            </tr>
          )}
          {items.map((r) => {
            const active = r.stopped_at === null;
            const start = new Date(r.started_at);
            const end = r.stopped_at ? new Date(r.stopped_at) : new Date();
            const dur = formatDuration(end.getTime() - start.getTime());
            return (
              <tr key={r.id} style={{ borderTop: "1px solid #1f1f1f" }}>
                <td style={{ padding: "0.6rem 0", color: "#ccc" }}>
                  {active && (
                    <span
                      style={{
                        color: "#ff4d4d",
                        marginRight: "0.5rem",
                        fontSize: 10,
                      }}
                    >
                      ●
                    </span>
                  )}
                  {r.name || <span style={{ color: "#555" }}>(unnamed)</span>}
                </td>
                <td style={{ padding: "0.6rem 0", color: "#aaa" }}>
                  {start.toLocaleString()}
                </td>
                <td style={{ padding: "0.6rem 0", color: "#aaa" }}>{dur}</td>
                <td style={{ padding: "0.6rem 0", textAlign: "right" }}>
                  <button onClick={() => onView(r)} style={smallBtn}>
                    view
                  </button>{" "}
                  <button
                    onClick={() => remove(r.id)}
                    style={{ ...smallBtn, color: "#cc8080" }}
                  >
                    delete
                  </button>
                </td>
              </tr>
            );
          })}
        </tbody>
      </table>
    </section>
  );
}

function formatDuration(ms: number): string {
  const s = Math.floor(ms / 1000);
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const ss = s % 60;
  if (h > 0) return `${h}h ${m}m`;
  if (m > 0) return `${m}m ${ss}s`;
  return `${ss}s`;
}

const smallBtn: React.CSSProperties = {
  background: "transparent",
  color: "#7adfff",
  border: "1px solid #2a3f4a",
  borderRadius: 4,
  padding: "0.25rem 0.6rem",
  fontSize: 12,
  cursor: "pointer",
};
