-- CTRL_Weight_HX711 — Supabase schema
-- Run this in the Supabase SQL editor once after creating the project.

-- 1) `live` is a singleton row (id=1) updated continuously by the bridge.
create table live (
  id          int primary key default 1,
  weight_g    real not null default 0,
  c1_g        real,
  c2_g        real,
  c3_g        real,
  c4_g        real,
  updated_at  timestamptz not null default now(),
  constraint live_singleton check (id = 1)
);
insert into live (id) values (1) on conflict do nothing;

-- 2) `weighings` gets one row per stable measurement event the bridge detects.
create table weighings (
  id          bigserial primary key,
  weight_g    real not null,
  label       text,
  captured_at timestamptz not null default now()
);
create index weighings_captured_at_idx on weighings (captured_at desc);

-- 3) `readings` is the full time-series log (~1 row/second) used for charting.
create table readings (
  id           bigserial primary key,
  weight_g     real not null,
  recorded_at  timestamptz not null default now()
);
create index readings_recorded_at_idx on readings (recorded_at desc);

-- 4) Realtime publication (chart re-queries on range change, so readings stays off).
alter publication supabase_realtime add table live;
alter publication supabase_realtime add table weighings;

-- 5) RLS: anon can read, writes only from the bridge (service-role key).
alter table live      enable row level security;
alter table weighings enable row level security;
alter table readings  enable row level security;

create policy "anon read live"      on live      for select using (true);
create policy "anon read weighings" on weighings for select using (true);
create policy "anon read readings"  on readings  for select using (true);
