#include "app_data.h"
#include "app_net.h"
#include "app_settings.h"
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>

// Poll cadence is a runtime setting now (settings().poll_seconds, default 30) — the proxy
// caches upstream so polling it is cheap; this keeps the screen fresh without being chatty.

// HTTP timeout (ms). LAN call to the proxy — keep short so a missing proxy
// doesn't stall the LVGL loop for long. The request is the only blocking part
// of this module; at 30s cadence a ~1.5s worst-case stall is acceptable.
#ifndef DATA_HTTP_TIMEOUT_MS
#define DATA_HTTP_TIMEOUT_MS 1500
#endif

namespace {

struct Window {
  int      used_pct  = 0;
  uint32_t resets_at = 0;   // epoch secs, 0 = unknown
};

struct Snapshot {
  bool     valid = false;
  Window   five_hour;
  Window   weekly;
  bool     has_weekly_opus = false;
  Window   weekly_opus;
  char     plan[24] = "unknown";
  uint32_t fetched_at_ms = 0;   // millis() of last good fetch (for age)
  bool     proxy_stale = false; // proxy's own "stale" flag
};

Snapshot   s_snap;
String     s_lastError = "";
uint32_t   s_lastPollMs = 0;
bool       s_firstPoll  = true;

// Wall-clock sync: epoch at the moment millis() == s_epochAtMs.
uint32_t   s_epochRef   = 0;
uint32_t   s_epochAtMs  = 0;

uint32_t nowEpoch() {
  if (s_epochRef == 0) return 0;
  return s_epochRef + (millis() - s_epochAtMs) / 1000;
}

void readWindow(JsonObjectConst o, Window &w) {
  if (o.isNull()) { w.used_pct = 0; w.resets_at = 0; return; }
  w.used_pct  = o["used_pct"]  | 0;
  if (w.used_pct < 0)   w.used_pct = 0;
  if (w.used_pct > 100) w.used_pct = 100;
  // resets_at may be a JSON null -> 0.
  w.resets_at = o["resets_at"].isNull() ? 0 : (uint32_t)(o["resets_at"] | 0);
}

void poll() {
  s_lastPollMs = millis();

  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, settings().proxy_url)) {
    s_lastError = "http begin failed";
    return;
  }
  http.setTimeout(DATA_HTTP_TIMEOUT_MS);
  http.setConnectTimeout(DATA_HTTP_TIMEOUT_MS);
  http.addHeader("Authorization", String("Bearer ") + settings().proxy_token);

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    s_lastError = "HTTP " + String(code);
    // Keep last good snapshot; just mark it stale (handled by data_stale()).
    http.end();
    return;
  }

  // Bounded parse — the contract is tiny (~256B); 768B filter buffer is plenty.
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) {
    s_lastError = String("json: ") + err.c_str();
    return;
  }

  Snapshot n;
  n.valid = true;
  readWindow(doc["five_hour"].as<JsonObjectConst>(), n.five_hour);
  readWindow(doc["weekly"].as<JsonObjectConst>(),    n.weekly);
  JsonObjectConst opus = doc["weekly_opus"].as<JsonObjectConst>();
  if (!opus.isNull()) { n.has_weekly_opus = true; readWindow(opus, n.weekly_opus); }
  const char *plan = doc["plan"] | "unknown";
  strncpy(n.plan, plan, sizeof(n.plan) - 1);
  n.plan[sizeof(n.plan) - 1] = '\0';
  n.proxy_stale  = doc["stale"] | false;
  n.fetched_at_ms = millis();

  s_snap = n;
  s_lastError = "";
}

}  // namespace

// ── public API ──────────────────────────────────────────────
void data_begin() {
  s_snap = Snapshot();
  s_lastError = "";
  s_lastPollMs = 0;
  s_firstPoll = true;
}

void data_loop() {
  if (!net_online()) return;  // wait for WiFi; keep last snapshot meanwhile
  uint32_t now = millis();
  if (s_firstPoll || (now - s_lastPollMs) >= (uint32_t)settings().poll_seconds * 1000) {
    s_firstPoll = false;
    poll();
  }
}

bool data_valid() { return s_snap.valid; }

bool data_stale() {
  if (!s_snap.valid) return true;
  if (s_snap.proxy_stale) return true;
  // Age-based only: keep showing the last-known values across a brief WiFi drop (they're
  // still meaningful — the proxy caches ~60s). Polling pauses while offline, so the snapshot
  // simply ages; once it's older than ~3 poll intervals (~90s) it goes stale and the UI blanks.
  // (net_online() is reflected separately by the status dot, so a drop is still visible.)
  return (millis() - s_snap.fetched_at_ms) > (uint32_t)settings().poll_seconds * 3000;
}

int  data_five_hour_pct()        { return s_snap.five_hour.used_pct; }
int  data_weekly_pct()           { return s_snap.weekly.used_pct; }
bool data_has_weekly_opus()      { return s_snap.has_weekly_opus; }
int  data_weekly_opus_pct()      { return s_snap.weekly_opus.used_pct; }

uint32_t data_five_hour_resets_at() { return s_snap.five_hour.resets_at; }
uint32_t data_weekly_resets_at()    { return s_snap.weekly.resets_at; }

const char *data_plan() { return s_snap.plan; }

static long secsLeft(uint32_t resetEpoch) {
  uint32_t now = nowEpoch();
  if (resetEpoch == 0 || now == 0) return -1;
  if (resetEpoch <= now) return 0;
  return (long)(resetEpoch - now);
}
long data_five_hour_secs_left() { return secsLeft(s_snap.five_hour.resets_at); }
long data_weekly_secs_left()    { return secsLeft(s_snap.weekly.resets_at); }

void data_set_now(uint32_t epoch_now) {
  s_epochRef  = epoch_now;
  s_epochAtMs = millis();
}

uint32_t data_age_seconds() {
  if (!s_snap.valid) return 0;
  return (millis() - s_snap.fetched_at_ms) / 1000;
}

const char *data_last_error() { return s_lastError.c_str(); }
