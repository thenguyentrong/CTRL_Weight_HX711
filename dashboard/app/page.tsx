"use client";

import { useEffect, useState } from "react";
import { supabase, supabaseConfigured } from "../lib/supabase";

type Live = {
  weight_g: number;
  c1_g: number | null;
  c2_g: number | null;
  c3_g: number | null;
  c4_g: number | null;
  updated_at: string;
};

type Weighing = {
  id: number;
  weight_g: number;
  captured_at: string;
};

export default function Page() {
  const [live, setLive] = useState<Live | null>(null);
  const [weighings, setWeighings] = useState<Weighing[]>([]);

  useEffect(() => {
    let cancelled = false;

    supabase
      .from("live")
      .select("*")
      .eq("id", 1)
      .single()
      .then(({ data }) => {
        if (!cancelled && data) setLive(data as Live);
      });

    supabase
      .from("weighings")
      .select("*")
      .order("captured_at", { ascending: false })
      .limit(50)
      .then(({ data }) => {
        if (!cancelled && data) setWeighings(data as Weighing[]);
      });

    const channel = supabase
      .channel("scale")
      .on(
        "postgres_changes",
        { event: "UPDATE", schema: "public", table: "live" },
        (payload) => setLive(payload.new as Live)
      )
      .on(
        "postgres_changes",
        { event: "INSERT", schema: "public", table: "weighings" },
        (payload) =>
          setWeighings((prev) =>
            [payload.new as Weighing, ...prev].slice(0, 50)
          )
      )
      .subscribe();

    return () => {
      cancelled = true;
      supabase.removeChannel(channel);
    };
  }, []);

  const totalKg = live ? (live.weight_g / 1000).toFixed(2) : "—";
  const cells: (number | null)[] = live
    ? [live.c1_g, live.c2_g, live.c3_g, live.c4_g]
    : [null, null, null, null];

  return (
    <main style={{ maxWidth: 900, margin: "0 auto", padding: "2rem" }}>
      <h1 style={{ fontSize: 18, color: "#666", letterSpacing: 1, margin: 0 }}>
        CTRL WEIGHT — LIVE
      </h1>
      {!supabaseConfigured && (
        <div
          style={{
            marginTop: "1rem",
            padding: "0.75rem 1rem",
            background: "#3a2a00",
            border: "1px solid #6b4a00",
            borderRadius: 6,
            color: "#ffd07a",
            fontSize: 13,
          }}
        >
          Supabase env vars not set. Add{" "}
          <code>NEXT_PUBLIC_SUPABASE_URL</code> and{" "}
          <code>NEXT_PUBLIC_SUPABASE_ANON_KEY</code> in Vercel project settings
          and redeploy.
        </div>
      )}

      <div style={{ textAlign: "center", margin: "3rem 0" }}>
        <div
          style={{
            fontSize: 96,
            fontWeight: 700,
            letterSpacing: -3,
            lineHeight: 1,
          }}
        >
          {totalKg} kg
        </div>
        {live && (
          <div style={{ color: "#666", marginTop: "1rem", fontSize: 14 }}>
            updated {timeAgo(live.updated_at)}
          </div>
        )}
      </div>

      <div
        style={{
          display: "grid",
          gridTemplateColumns: "repeat(4, 1fr)",
          gap: "0.75rem",
          marginBottom: "3rem",
        }}
      >
        {cells.map((g, i) => (
          <div
            key={i}
            style={{
              padding: "1rem",
              background: "#161616",
              borderRadius: 8,
              textAlign: "center",
            }}
          >
            <div style={{ color: "#666", fontSize: 11, letterSpacing: 1 }}>
              CELL {i + 1}
            </div>
            <div style={{ fontSize: 22, marginTop: 4 }}>
              {g === null ? "—" : `${(g / 1000).toFixed(2)} kg`}
            </div>
          </div>
        ))}
      </div>

      <h2
        style={{
          fontSize: 12,
          color: "#666",
          letterSpacing: 1,
          marginBottom: "0.5rem",
        }}
      >
        RECENT WEIGHINGS
      </h2>
      <table style={{ width: "100%", borderCollapse: "collapse" }}>
        <thead>
          <tr style={{ color: "#666", textAlign: "left", fontSize: 12 }}>
            <th style={{ padding: "0.5rem 0", fontWeight: 400 }}>when</th>
            <th
              style={{
                padding: "0.5rem 0",
                fontWeight: 400,
                textAlign: "right",
              }}
            >
              weight
            </th>
          </tr>
        </thead>
        <tbody>
          {weighings.length === 0 && (
            <tr>
              <td colSpan={2} style={{ padding: "1rem 0", color: "#444" }}>
                no weighings yet — place something on the scale
              </td>
            </tr>
          )}
          {weighings.map((w) => (
            <tr key={w.id} style={{ borderTop: "1px solid #1f1f1f" }}>
              <td style={{ padding: "0.6rem 0", color: "#aaa" }}>
                {new Date(w.captured_at).toLocaleString()}
              </td>
              <td style={{ padding: "0.6rem 0", textAlign: "right" }}>
                {(w.weight_g / 1000).toFixed(2)} kg
              </td>
            </tr>
          ))}
        </tbody>
      </table>
    </main>
  );
}

function timeAgo(iso: string): string {
  const diff = (Date.now() - new Date(iso).getTime()) / 1000;
  if (diff < 5) return "just now";
  if (diff < 60) return `${Math.floor(diff)} s ago`;
  if (diff < 3600) return `${Math.floor(diff / 60)} min ago`;
  return `${Math.floor(diff / 3600)} h ago`;
}
