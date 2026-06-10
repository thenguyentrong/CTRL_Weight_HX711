-- Migration: add the `recordings` table for named time-slice bookmarks.
-- The bridge already logs every reading at ~1 Hz into `readings`. A "recording"
-- is just a (started_at, stopped_at) pair plus a name, so the dashboard can
-- replay a window of the continuous log later.

create table if not exists recordings (
  id          bigserial primary key,
  name        text,
  notes       text,
  started_at  timestamptz not null default now(),
  stopped_at  timestamptz
);
create index if not exists recordings_started_at_idx
  on recordings (started_at desc);

alter table recordings enable row level security;

do $$
begin
  if not exists (
    select 1 from pg_policies
    where schemaname = 'public' and tablename = 'recordings' and policyname = 'anon read recordings'
  ) then
    create policy "anon read recordings" on recordings for select using (true);
  end if;

  if not exists (
    select 1 from pg_policies
    where schemaname = 'public' and tablename = 'recordings' and policyname = 'anon insert recordings'
  ) then
    create policy "anon insert recordings" on recordings for insert with check (true);
  end if;

  if not exists (
    select 1 from pg_policies
    where schemaname = 'public' and tablename = 'recordings' and policyname = 'anon update recordings'
  ) then
    create policy "anon update recordings" on recordings for update using (true);
  end if;

  if not exists (
    select 1 from pg_policies
    where schemaname = 'public' and tablename = 'recordings' and policyname = 'anon delete recordings'
  ) then
    create policy "anon delete recordings" on recordings for delete using (true);
  end if;
end $$;

do $$
begin
  begin
    alter publication supabase_realtime add table recordings;
  exception when duplicate_object then
    null;
  end;
end $$;
