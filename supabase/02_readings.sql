-- Migration: add the `readings` time-series table for charting.
-- Run this in the Supabase SQL editor if you already created the project from schema.sql.

create table if not exists readings (
  id           bigserial primary key,
  weight_g     real not null,
  recorded_at  timestamptz not null default now()
);

create index if not exists readings_recorded_at_idx
  on readings (recorded_at desc);

alter table readings enable row level security;

do $$
begin
  if not exists (
    select 1 from pg_policies
    where schemaname = 'public' and tablename = 'readings' and policyname = 'anon read readings'
  ) then
    create policy "anon read readings" on readings for select using (true);
  end if;
end $$;
