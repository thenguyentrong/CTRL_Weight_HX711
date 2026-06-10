import { createClient } from "@supabase/supabase-js";

// Placeholders so the build succeeds even before the real env vars are set
// (e.g. first deploy on Vercel). At runtime, requests fail silently until you
// add real NEXT_PUBLIC_SUPABASE_URL / NEXT_PUBLIC_SUPABASE_ANON_KEY in Vercel
// project settings and redeploy.
const url =
  process.env.NEXT_PUBLIC_SUPABASE_URL || "https://placeholder.supabase.co";
const anonKey =
  process.env.NEXT_PUBLIC_SUPABASE_ANON_KEY || "placeholder-anon-key";

export const supabaseConfigured = Boolean(
  process.env.NEXT_PUBLIC_SUPABASE_URL && process.env.NEXT_PUBLIC_SUPABASE_ANON_KEY
);

export const supabase = createClient(url, anonKey, {
  realtime: { params: { eventsPerSecond: 5 } },
});
