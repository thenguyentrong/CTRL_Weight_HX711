"use client";

import { useEffect, useState } from "react";
import { supabase } from "../lib/supabase";

type Recording = {
  id: number;
  name: string | null;
  started_at: string;
  stopped_at: string | null;
};

export default function RecordPanel({
  onStateChange,
}: {
  onStateChange?: (active: Recording | null) => void;
}) {
  const [active, setActive] = useState<Recording | null>(null);
  const [name, setName] = useState("");
  const [busy, setBusy] = useState(false);

  // load any active recording (stopped_at IS NULL) on mount
  useEffect(() => {
    supabase
      .from("recordings")
      .select("*")
      .is("stopped_at", null)
      .order("started_at", { ascending: false })
      .limit(1)
      .then(({ data }) => {
        const a = (data?.[0] as Recording | undefined) ?? null;
        setActive(a);
        onStateChange?.(a);
      });
  }, [onStateChange]);

  async function start() {
    setBusy(true);
    const { data, error } = await supabase
      .from("recordings")
      .insert({ name: name.trim() || null })
      .select()
      .single();
    setBusy(false);
    if (error) {
      alert("Start failed: " + error.message);
      return;
    }
    const a = data as Recording;
    setActive(a);
    setName("");
    onStateChange?.(a);
  }

  async function stop() {
    if (!active) return;
    setBusy(true);
    const { error } = await supabase
      .from("recordings")
      .update({ stopped_at: new Date().toISOString() })
      .eq("id", active.id);
    setBusy(false);
    if (error) {
      alert("Stop failed: " + error.message);
      return;
    }
    setActive(null);
    onStateChange?.(null);
  }

  const elapsed = active
    ? formatElapsed(Date.now() - new Date(active.started_at).getTime())
    : null;

  return (
    <section
      style={{
        marginTop: "1.5rem",
        padding: "1rem 1.25rem",
        background: active ? "#1a0d0d" : "#0f0f0f",
        border: "1px solid " + (active ? "#5a2222" : "#1f1f1f"),
        borderRadius: 8,
        display: "flex",
        alignItems: "center",
        gap: "1rem",
        flexWrap: "wrap",
      }}
    >
      {active ? (
        <>
          <span
            style={{
              width: 10,
              height: 10,
              borderRadius: "50%",
              background: "#ff4d4d",
              boxShadow: "0 0 8px #ff4d4d",
              animation: "pulse 1.5s infinite",
            }}
          />
          <div style={{ flex: 1 }}>
            <div style={{ color: "#ff8080", fontSize: 13, letterSpacing: 1 }}>
              RECORDING {elapsed}
            </div>
            <div style={{ color: "#ccc", fontSize: 16 }}>
              {active.name || "(unnamed)"}
            </div>
          </div>
          <button onClick={stop} disabled={busy} style={btnStop}>
            ■ STOP
          </button>
        </>
      ) : (
        <>
          <input
            type="text"
            placeholder="recording name (optional)"
            value={name}
            onChange={(e) => setName(e.target.value)}
            onKeyDown={(e) => e.key === "Enter" && start()}
            style={inputStyle}
          />
          <button onClick={start} disabled={busy} style={btnStart}>
            ● START RECORDING
          </button>
        </>
      )}
      <style>{"@keyframes pulse { 0%,100% { opacity: 1 } 50% { opacity: 0.4 } }"}</style>
    </section>
  );
}

function formatElapsed(ms: number): string {
  const s = Math.floor(ms / 1000);
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const ss = s % 60;
  const pad = (n: number) => String(n).padStart(2, "0");
  return h > 0 ? `${h}:${pad(m)}:${pad(ss)}` : `${m}:${pad(ss)}`;
}

const inputStyle: React.CSSProperties = {
  flex: 1,
  minWidth: 200,
  background: "#161616",
  color: "#eee",
  border: "1px solid #2a2a2a",
  borderRadius: 4,
  padding: "0.5rem 0.7rem",
  fontSize: 14,
};

const btnStart: React.CSSProperties = {
  background: "#1a3a22",
  color: "#7affb5",
  border: "1px solid #3a7050",
  borderRadius: 6,
  padding: "0.55rem 1.2rem",
  fontSize: 14,
  fontWeight: 600,
  cursor: "pointer",
};

const btnStop: React.CSSProperties = {
  background: "#3a1a1a",
  color: "#ff8080",
  border: "1px solid #703a3a",
  borderRadius: 6,
  padding: "0.55rem 1.2rem",
  fontSize: 14,
  fontWeight: 600,
  cursor: "pointer",
};
