"use client";

import { useState } from "react";
import {
  exportChartPNG,
  exportChartSVG,
  exportRecordingCSV,
} from "../lib/export";

export default function ExportPanel({
  recordingId,
  title,
}: {
  recordingId: number;
  title: string;
}) {
  const [busy, setBusy] = useState(false);

  async function csv() {
    setBusy(true);
    try {
      await exportRecordingCSV(recordingId, title);
    } finally {
      setBusy(false);
    }
  }

  return (
    <div
      style={{
        display: "flex",
        gap: "0.5rem",
        alignItems: "center",
        marginTop: "0.75rem",
      }}
    >
      <span style={{ color: "#666", fontSize: 11, letterSpacing: 1 }}>
        EXPORT
      </span>
      <button style={btn} onClick={csv} disabled={busy}>
        {busy ? "…" : "CSV"}
      </button>
      <button style={btn} onClick={() => exportChartPNG(title)}>
        PNG
      </button>
      <button style={btn} onClick={() => exportChartSVG(title)}>
        SVG
      </button>
    </div>
  );
}

const btn: React.CSSProperties = {
  background: "#161616",
  color: "#cfe9ff",
  border: "1px solid #2a3f4a",
  borderRadius: 6,
  padding: "0.35rem 0.9rem",
  fontSize: 12,
  letterSpacing: 1,
  cursor: "pointer",
};
