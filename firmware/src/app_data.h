#pragma once
#include <Arduino.h>
// Claude usage data (Feature F4, device side).
//
// Non-blocking HTTP client: when net_online(), polls the proxy URL (from
// settings(), seeded from config.json) every poll interval with the shared bearer token, parses
// the proxy's JSON contract (ArduinoJson v7), and caches the latest snapshot.
// All work happens in data_loop() on the main loop — no blocking in setup().

void data_begin();      // init (cheap; does not block on network)
void data_loop();       // call from loop(); does the timed, non-blocking poll

// Latest snapshot accessors --------------------------------------------------
bool data_valid();      // true once we have at least one good fetch
bool data_stale();      // true if data is old / proxy reported stale / offline

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
