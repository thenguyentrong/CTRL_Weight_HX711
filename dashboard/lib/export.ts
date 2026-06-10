import { fetchRecordingReadings } from "./analytics";

function download(filename: string, blob: Blob) {
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = filename;
  document.body.appendChild(a);
  a.click();
  a.remove();
  URL.revokeObjectURL(url);
}

function safeName(s: string): string {
  return (s || "recording").replace(/[^\w.-]+/g, "_").slice(0, 60);
}

function escapeXml(s: string): string {
  return s.replace(
    /[<>&'"]/g,
    (c) =>
      ({ "<": "&lt;", ">": "&gt;", "&": "&amp;", "'": "&apos;", '"': "&quot;" }[
        c
      ]!)
  );
}

/** Full-resolution CSV of a recording's owned readings. */
export async function exportRecordingCSV(recordingId: number, name: string) {
  const rows = await fetchRecordingReadings(recordingId);
  const header = "recorded_at,weight_g,weight_kg\n";
  const body = rows
    .map(
      (r) => `${r.recorded_at},${r.weight_g},${(r.weight_g / 1000).toFixed(3)}`
    )
    .join("\n");
  download(
    `${safeName(name)}.csv`,
    new Blob([header + body], { type: "text/csv;charset=utf-8" })
  );
}

function getChartSvg(): SVGSVGElement | null {
  const c = document.getElementById("weight-chart-export");
  return c ? c.querySelector("svg") : null;
}

// Clone the live Recharts <svg>, wrap it with a dark background + title band.
function buildSvgString(
  title: string
): { svg: string; width: number; height: number } | null {
  const src = getChartSvg();
  if (!src) return null;
  const clone = src.cloneNode(true) as SVGSVGElement;
  const rect = src.getBoundingClientRect();
  const width = Math.round(rect.width) || 800;
  const chartH = Math.round(rect.height) || 360;
  const band = 44;
  const fullH = chartH + band;

  clone.setAttribute("width", String(width));
  clone.setAttribute("height", String(chartH));
  clone.setAttribute("xmlns", "http://www.w3.org/2000/svg");
  const inner = new XMLSerializer().serializeToString(clone);

  const svg =
    `<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${fullH}" viewBox="0 0 ${width} ${fullH}">` +
    `<rect width="100%" height="100%" fill="#0f0f0f"/>` +
    `<text x="16" y="28" fill="#eaeaea" font-family="system-ui,-apple-system,sans-serif" font-size="16" font-weight="600">${escapeXml(
      title
    )}</text>` +
    `<g transform="translate(0,${band})">${inner}</g>` +
    `</svg>`;
  return { svg, width, height: fullH };
}

export function exportChartSVG(title: string) {
  const built = buildSvgString(title);
  if (!built) {
    alert("Chart not ready to export — open a recording first.");
    return;
  }
  download(
    `${safeName(title)}.svg`,
    new Blob([built.svg], { type: "image/svg+xml;charset=utf-8" })
  );
}

export function exportChartPNG(title: string) {
  const built = buildSvgString(title);
  if (!built) {
    alert("Chart not ready to export — open a recording first.");
    return;
  }
  const { svg, width, height } = built;
  const scale = 2; // 2x for a crisp raster
  const img = new Image();
  const url = URL.createObjectURL(
    new Blob([svg], { type: "image/svg+xml;charset=utf-8" })
  );
  img.onload = () => {
    const canvas = document.createElement("canvas");
    canvas.width = width * scale;
    canvas.height = height * scale;
    const ctx = canvas.getContext("2d")!;
    ctx.scale(scale, scale);
    ctx.drawImage(img, 0, 0);
    URL.revokeObjectURL(url);
    canvas.toBlob((b) => {
      if (b) download(`${safeName(title)}.png`, b);
    }, "image/png");
  };
  img.onerror = () => {
    URL.revokeObjectURL(url);
    alert("PNG export failed.");
  };
  img.src = url;
}
