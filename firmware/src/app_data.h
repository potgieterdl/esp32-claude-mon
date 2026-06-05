#pragma once
#include <Arduino.h>
// Claude usage data (Feature F4, device side).
//
// Non-blocking HTTPS client: when net_online(), calls api.anthropic.com/api/oauth/usage directly
// every poll interval with the on-device OAuth token (CA-pinned, see anthropic_ca.h), refreshes
// that token on-device when it nears expiry, parses the response (ArduinoJson v7), and caches the
// latest snapshot. All work happens in data_loop() on the main loop — no blocking in setup().

void data_begin();      // init (cheap; does not block on network)
void data_loop();       // call from loop(); does the timed, non-blocking poll

// Latest snapshot accessors --------------------------------------------------
bool data_valid();      // true once we have at least one good fetch
bool data_stale();      // true if data is old (age-based) / offline
bool data_needs_token(); // true if no OAuth token is synced or it's expired/dead (UI: "run sync")
void data_note_token_synced();  // call when a fresh token arrives (PUT /config.json): re-poll + flag UI
bool data_take_token_synced();  // one-shot: true once after a sync (presenter shows "token received")
uint32_t data_token_last_sync_epoch();  // epoch of last on-device token update (0 = none this boot)
uint32_t data_token_expires_at();       // access-token expiry epoch (next refresh ≈ this − few min)

int  data_five_hour_pct();        // 0..100  (5-hour rolling window used)
int  data_weekly_pct();           // 0..100  (weekly window used)
bool data_has_weekly_opus();      // plan exposes an Opus weekly cap?
int  data_weekly_opus_pct();      // 0..100  (valid only if has_weekly_opus)

// Reset times as Unix epoch seconds (UTC); 0 if unknown.
uint32_t data_five_hour_resets_at();
uint32_t data_weekly_resets_at();

const char *data_plan();          // e.g. "max_20x" / "pro" / "unknown"

// Seconds until reset, computed from epoch + a synced wall clock. Returns -1 if
// unknown (no reset epoch, or no time sync yet — see data_set_now()).
long data_five_hour_secs_left();
long data_weekly_secs_left();

// Feed a trusted current epoch (e.g. from NTP/RTC, Feature F6) so countdowns
// work. Until called, *_secs_left() return -1 and the UI should hide them.
void data_set_now(uint32_t epoch_now);

uint32_t data_age_seconds();      // seconds since last successful fetch (0 if none)
const char *data_last_error();    // last error string ("" if none)
