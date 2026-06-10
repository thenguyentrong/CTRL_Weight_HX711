// Tiny UI auth gate. NOT a substitute for protecting the Supabase API (the anon
// key is still public) — it just keeps the dashboard pages behind a username +
// password. Credentials live in server-only env vars; the cookie stores an HMAC
// token, never the password. Web Crypto is used so this also runs on the Edge
// (middleware) runtime.

export const AUTH_COOKIE = "ctrl_auth";

/** True only when the gate is configured; otherwise auth is disabled (no lockout). */
export function authEnabled(): boolean {
  return Boolean(process.env.DASHBOARD_USER && process.env.DASHBOARD_PASS);
}

/** Deterministic HMAC token = HMAC_SHA256("authed:<user>", COOKIE_SECRET). */
export async function authToken(): Promise<string> {
  const user = process.env.DASHBOARD_USER || "";
  const secret = process.env.DASHBOARD_COOKIE_SECRET || "ctrl-weight-default-secret";
  const key = await crypto.subtle.importKey(
    "raw",
    new TextEncoder().encode(secret),
    { name: "HMAC", hash: "SHA-256" },
    false,
    ["sign"]
  );
  const sig = await crypto.subtle.sign(
    "HMAC",
    key,
    new TextEncoder().encode(`authed:${user}`)
  );
  return Array.from(new Uint8Array(sig))
    .map((b) => b.toString(16).padStart(2, "0"))
    .join("");
}

export function checkCredentials(username: string, password: string): boolean {
  return (
    authEnabled() &&
    username === process.env.DASHBOARD_USER &&
    password === process.env.DASHBOARD_PASS
  );
}
