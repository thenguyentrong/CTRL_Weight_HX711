# Dashboard

Next.js page that subscribes to Supabase realtime and shows the current scale
value + recent weighings.

## Local dev

1. `cp .env.local.example .env.local` and fill in the two `NEXT_PUBLIC_` vars from
   Supabase → Project Settings → API.
2. `npm install`
3. `npm run dev` → http://localhost:3000

## Deploy to Vercel

1. Push the repo to GitHub (already done — `thenguyentrong/CTRL_Weight_HX711`).
2. In Vercel: New Project → import the repo → set **Root Directory** to `dashboard`.
3. Add the same two env vars (`NEXT_PUBLIC_SUPABASE_URL`, `NEXT_PUBLIC_SUPABASE_ANON_KEY`)
   under Project Settings → Environment Variables.
4. Deploy. The live URL updates in real time as the bridge writes.
