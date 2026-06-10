"use client";

import { useState } from "react";
import { supabase } from "../lib/supabase";

type Status = "idle" | "sending" | "sent" | "error";

export default function TareButton() {
  const [status, setStatus] = useState<Status>("idle");

  async function handleTare() {
    setStatus("sending");
    const { error } = await supabase.from("commands").insert({ command: "tare" });
    if (error) {
      setStatus("error");
    } else {
      setStatus("sent");
    }
    setTimeout(() => setStatus("idle"), 4000);
  }

  const label =
    status === "sending"
      ? "ZEROING…"
      : status === "sent"
      ? "✓ TARE SENT"
      : status === "error"
      ? "✗ FAILED — RETRY"
      : "TARE / ZERO";

  return (
    <div style={{ textAlign: "center", marginTop: "-1.5rem", marginBottom: "3rem" }}>
      <button
        onClick={handleTare}
        disabled={status === "sending"}
        style={{
          padding: "0.65rem 1.8rem",
          fontSize: 13,
          letterSpacing: 1,
          fontWeight: 600,
          color: status === "error" ? "#ffb4b4" : status === "sent" ? "#9be39b" : "#ddd",
          background: "#161616",
          border: "1px solid #2a2a2a",
          borderRadius: 8,
          cursor: status === "sending" ? "default" : "pointer",
        }}
      >
        {label}
      </button>
      <div style={{ color: "#555", fontSize: 11, marginTop: "0.5rem" }}>
        zeroes the scale — keep the platform empty
      </div>
    </div>
  );
}
