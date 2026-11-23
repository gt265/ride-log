// Ride Log settings.
// To switch on accounts and cloud sync, fill in your Supabase project's URL and
// publishable ("anon") key, then bump VERSION in sw.js. See supabase/README.md.
// The publishable key is designed to be public: your rides are protected by the
// database's row-level security rules (supabase/setup.sql), not by hiding this key.
window.RIDELOG_CONFIG = {
  supabaseUrl: '',   // e.g. 'https://abcdefghijkl.supabase.co'
  supabaseKey: '',   // e.g. 'sb_publishable_...' or the legacy 'eyJ...' anon key
};
