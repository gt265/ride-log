-- Ride Log: database setup for accounts + cloud sync.
-- Paste this whole file into Supabase → SQL Editor → New query → Run.
-- Safe to run more than once.

-- One row per ride. The ride itself lives in `data` (same shape as in the app).
create table if not exists public.rides (
  id          text primary key,
  user_id     uuid not null default auth.uid() references auth.users(id) on delete cascade,
  data        jsonb,
  deleted     boolean not null default false,       -- tombstone so deletions reach other devices
  updated_at  timestamptz not null default now(),   -- when the rider last edited it (device clock)
  synced_at   timestamptz not null default now()    -- when the server received it (set by trigger)
);
create index if not exists rides_user_synced_idx on public.rides (user_id, synced_at);

-- One row per rider: name, monthly goal, weight, FTP, max HR.
create table if not exists public.profiles (
  user_id     uuid primary key default auth.uid() references auth.users(id) on delete cascade,
  data        jsonb not null default '{}'::jsonb,
  updated_at  timestamptz not null default now()
);

-- The server stamps every ride write, so devices can ask "what changed since I last looked?"
create or replace function public.ridelog_stamp_synced_at() returns trigger
language plpgsql set search_path = '' as $$
begin
  new.synced_at := clock_timestamp();
  return new;
end $$;
drop trigger if exists rides_stamp_synced_at on public.rides;
create trigger rides_stamp_synced_at before insert or update on public.rides
  for each row execute function public.ridelog_stamp_synced_at();

-- Let signed-in users use the tables through the app's API (newer Supabase projects
-- don't grant this automatically). Signed-out visitors get no access at all.
grant usage on schema public to authenticated;
grant select, insert, update, delete on table public.rides    to authenticated;
grant select, insert, update, delete on table public.profiles to authenticated;

-- Row-level security: every rider can only ever see and change their own rows.
alter table public.rides    enable row level security;
alter table public.profiles enable row level security;

drop policy if exists "Riders manage their own rides" on public.rides;
create policy "Riders manage their own rides" on public.rides
  for all to authenticated
  using (user_id = (select auth.uid()))
  with check (user_id = (select auth.uid()));

drop policy if exists "Riders manage their own profile" on public.profiles;
create policy "Riders manage their own profile" on public.profiles
  for all to authenticated
  using (user_id = (select auth.uid()))
  with check (user_id = (select auth.uid()));
