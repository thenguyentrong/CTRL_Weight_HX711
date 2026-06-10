-- Migration: add the `commands` table so the dashboard can ask the bridge to
-- act on the Arduino (e.g. tare / zero). The dashboard INSERTs a row via the
-- anon key; the bridge (service role) polls for unprocessed rows, sends the
-- matching serial command to the scale, then stamps processed_at.

create table if not exists commands (
  id           bigserial primary key,
  command      text not null,
  created_at   timestamptz not null default now(),
  processed_at timestamptz
);
create index if not exists commands_unprocessed_idx
  on commands (created_at) where processed_at is null;

alter table commands enable row level security;

do $$
begin
  if not exists (
    select 1 from pg_policies
    where schemaname = 'public' and tablename = 'commands' and policyname = 'anon insert commands'
  ) then
    create policy "anon insert commands" on commands for insert with check (true);
  end if;

  if not exists (
    select 1 from pg_policies
    where schemaname = 'public' and tablename = 'commands' and policyname = 'anon read commands'
  ) then
    create policy "anon read commands" on commands for select using (true);
  end if;
end $$;
