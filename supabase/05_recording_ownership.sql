-- Migration: make a recording OWN its readings.
-- Until now `recordings` was just a (started_at, stopped_at) bookmark over the
-- shared `readings` log. We add a nullable FK so the bridge can tag each reading
-- with the active recording. Tagged readings are kept forever; untagged (idle
-- stream) readings are pruned after 7 days by the bridge.

alter table readings
  add column if not exists recording_id bigint references recordings(id) on delete set null;

create index if not exists readings_recording_id_idx
  on readings (recording_id) where recording_id is not null;
