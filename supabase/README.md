# Switching on accounts (Supabase)

Ride Log uses [Supabase](https://supabase.com) for sign-in and cloud sync. The free plan is plenty for personal use.
Setup takes about 10 minutes and you only do it once.

## 1. Create a project
1. Sign up at **supabase.com** and click **New project**.
2. Name it `ride-log`, choose a strong database password (you won't need it in the app), and pick the region closest to you.
3. Wait a minute or two while it starts.

## 2. Create the tables
1. In the left menu open **SQL Editor** → **New query**.
2. Paste the whole of [`setup.sql`](setup.sql) and click **Run**. It should say *Success. No rows returned*.

## 3. Tell Supabase where the app lives
Open **Authentication** → **URL Configuration**:
- **Site URL:** `https://gt265.github.io/ride-log/`
- **Redirect URLs:** add `https://gt265.github.io/ride-log/`

This makes the "confirm your email" and "reset password" links open Ride Log.

## 4. Connect the app
Open **Project Settings** → **API Keys** (on older projects: **API**) and copy:
- the **Project URL** (looks like `https://abcdefghijkl.supabase.co`)
- the **publishable** key (`sb_publishable_…`), or on older projects the **anon public** key (`eyJ…`)

Put both into [`config.js`](../config.js), then change `VERSION` in [`sw.js`](../sw.js) so installed copies update.
Both values are safe to publish: the row-level security rules in `setup.sql` stop anyone reading or changing rides that aren't theirs.
**Never** put the `service_role` / secret key in the app.

## Good to know
- Supabase's built-in email sender is limited to a few emails per hour, which is fine for a personal app.
  For more, add your own SMTP server under **Authentication → Emails**.
- To turn off email confirmation (sign up and go straight in), switch off **Confirm email** under **Authentication → Sign In / Providers → Email**.
- Free projects pause after a week with no activity; open the Supabase dashboard to wake it up.
