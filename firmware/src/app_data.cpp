#include "app_data.h"
#include "app_net.h"
#include "app_settings.h"
#include "anthropic_ca.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Device talks DIRECTLY to Anthropic (no proxy): GET api.anthropic.com/api/oauth/usage with the
// on-device Claude OAuth token, refreshing it via platform.claude.com when it nears expiry. TLS is
// pinned to the bundled root CAs (anthropic_ca.h). The /usage shape (utilization 0..100 +
// ISO-8601 resets_at) is normalised here into the same Snapshot the UI already consumes.

// Endpoints / auth constants (mirror what Claude Code's own usage display sends).
#define USAGE_URL    "https://api.anthropic.com/api/oauth/usage"
#define TOKEN_URL    "https://platform.claude.com/v1/oauth/token"
#define OAUTH_CLIENT "9d1c250a-e61b-44d9-88ed-5944d1962f5e"   // public Claude Code PKCE client (no secret)
#define USER_AGENT   "claude-code/1.0.0"                       // non-claude-code UA -> 429 bucket
#define OAUTH_BETA   "oauth-2025-04-20"

// HTTPS to the internet is a bit slower than a LAN call; allow headroom but stay bounded so a
// hang can't stall the LVGL loop for long. Refresh happens at most ~once/8h.
#ifndef DATA_HTTP_TIMEOUT_MS
#define DATA_HTTP_TIMEOUT_MS 9000
#endif
// Refresh this many seconds before the access token expires (needs a synced wall clock).
#define REFRESH_SKEW_S 300

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
  uint32_t fetched_at_ms = 0;
};

Snapshot   s_snap;
String     s_lastError = "";
uint32_t   s_lastPollMs = 0;
bool       s_firstPoll  = true;
uint32_t   s_resetRepollFor = 0;   // resets_at we've already boundary-re-polled for (once per window)
bool       s_needsToken = false;   // true => no/dead token; UI should prompt "run sync script"
bool       s_tokenRcvd  = false;   // one-shot: a fresh token was just synced (UI confirms receipt)
uint32_t   s_tokenUpdatedEpoch = 0;   // epoch of the last token update (sync or on-device refresh)

// Wall-clock sync: epoch at the moment millis() == s_epochAtMs.
uint32_t   s_epochRef   = 0;
uint32_t   s_epochAtMs  = 0;

uint32_t nowEpoch() {
  if (s_epochRef == 0) return 0;
  return s_epochRef + (millis() - s_epochAtMs) / 1000;
}

// "2026-06-05T11:20:00.915213+00:00" -> epoch secs. Assumes UTC (observed offset is +00:00);
// fractional seconds and the trailing offset are ignored. 0 on parse failure.
uint32_t iso8601_to_epoch(const char *s) {
  if (!s || !*s) return 0;
  int Y, Mo, D, h, m, sec;
  if (sscanf(s, "%d-%d-%dT%d:%d:%d", &Y, &Mo, &D, &h, &m, &sec) != 6) return 0;
  // days_from_civil (Howard Hinnant) — calendar date -> days since 1970-01-01.
  Y -= (Mo <= 2);
  long era = (Y >= 0 ? Y : Y - 399) / 400;
  unsigned yoe = (unsigned)(Y - era * 400);
  unsigned doy = (153 * (Mo + (Mo > 2 ? -3 : 9)) + 2) / 5 + D - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = era * 146097L + (long)doe - 719468L;
  return (uint32_t)(days * 86400L + h * 3600L + m * 60L + sec);
}

int pct_round(float v) {
  if (v < 0) v = 0;
  if (v > 100) v = 100;
  return (int)(v + 0.5f);
}

void readWindow(JsonObjectConst o, Window &w) {
  if (o.isNull()) { w.used_pct = 0; w.resets_at = 0; return; }
  w.used_pct  = pct_round(o["utilization"] | 0.0f);
  w.resets_at = o["resets_at"].is<const char *>() ? iso8601_to_epoch(o["resets_at"]) : 0;
}

// "default_claude_max_5x" -> "max_5x"; "" -> "unknown".
void planLabel(char *out, size_t n) {
  const char *t = settings().oauth_tier;
  if (!t || !*t) { strlcpy(out, "unknown", n); return; }
  const char *p = strstr(t, "default_claude_");
  strlcpy(out, p ? p + 15 : t, n);
}

// One CA-pinned HTTPS request. Returns the HTTP status (<0 = connect/TLS error), fills `body`.
int httpsRequest(const char *url, bool post, const String &postBody,
                 const char *bearer, String &body) {
  WiFiClientSecure client;
  client.setCACert(ANTHROPIC_ROOT_CA);     // pin to bundled roots (GTS R4 / ISRG X1)
  client.setTimeout(DATA_HTTP_TIMEOUT_MS / 1000);

  HTTPClient http;
  http.setConnectTimeout(DATA_HTTP_TIMEOUT_MS);
  http.setTimeout(DATA_HTTP_TIMEOUT_MS);
  if (!http.begin(client, url)) return -1000;

  http.addHeader("User-Agent", USER_AGENT);
  http.addHeader("Accept", "application/json");
  if (bearer) {
    http.addHeader("Authorization", String("Bearer ") + bearer);
    http.addHeader("anthropic-beta", OAUTH_BETA);
  }
  int code;
  if (post) {
    http.addHeader("Content-Type", "application/json");
    code = http.POST(postBody);
  } else {
    code = http.GET();
  }
  if (code > 0) body = http.getString();
  http.end();
  client.stop();
  return code;
}

// Exchange the stored refresh token for a fresh access token; persist the rotated pair.
// Returns true on success. Sets s_lastError / s_needsToken on failure.
bool refreshToken() {
  if (!settings().oauth_refresh[0]) { s_needsToken = true; s_lastError = "no refresh token"; return false; }
  JsonDocument req;
  req["grant_type"]   = "refresh_token";
  req["refresh_token"] = settings().oauth_refresh;
  req["client_id"]    = OAUTH_CLIENT;
  String body; serializeJson(req, body);

  String resp;
  int code = httpsRequest(TOKEN_URL, true, body, nullptr, resp);
  if (code != 200) {
    s_lastError = "refresh HTTP " + String(code);
    if (code == 400 || code == 401 || code == 403) s_needsToken = true;  // refresh token dead
    return false;
  }
  JsonDocument j;
  if (deserializeJson(j, resp)) { s_lastError = "refresh: bad json"; return false; }
  const char *access  = j["access_token"];
  const char *refresh = j["refresh_token"] | settings().oauth_refresh;  // rotates; keep old if absent
  long expires_in     = j["expires_in"] | 28800;
  if (!access) { s_lastError = "refresh: no access_token"; return false; }
  uint32_t now = nowEpoch();
  uint32_t exp = now ? now + (uint32_t)expires_in : 0;
  settings_update_oauth(access, refresh, exp);
  s_needsToken = false;
  s_tokenUpdatedEpoch = now;
  Serial.println("[data] oauth token refreshed");
  return true;
}

bool fillFromUsage(const String &resp) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, resp);
  if (err) { s_lastError = String("usage json: ") + err.c_str(); return false; }
  Snapshot n;
  n.valid = true;
  readWindow(doc["five_hour"].as<JsonObjectConst>(), n.five_hour);
  readWindow(doc["seven_day"].as<JsonObjectConst>(), n.weekly);
  JsonObjectConst opus = doc["seven_day_opus"].as<JsonObjectConst>();
  if (!opus.isNull()) { n.has_weekly_opus = true; readWindow(opus, n.weekly_opus); }
  planLabel(n.plan, sizeof n.plan);
  n.fetched_at_ms = millis();
  s_snap = n;
  s_lastError = "";
  return true;
}

void poll() {
  s_lastPollMs = millis();

  if (!settings_has_oauth()) { s_needsToken = true; s_lastError = "no token — run sync script"; return; }

  // Proactive refresh when we know the clock and the token is near/after expiry.
  uint32_t now = nowEpoch();
  if (now && settings().oauth_expires_at && now + REFRESH_SKEW_S >= settings().oauth_expires_at) {
    if (!refreshToken()) return;   // refresh failed (error/needs-token already set)
  }

  String resp;
  int code = httpsRequest(USAGE_URL, false, "", settings().oauth_access, resp);

  // Reactive refresh: access token rejected -> refresh once and retry.
  if (code == 401 || code == 403) {
    if (!refreshToken()) return;
    code = httpsRequest(USAGE_URL, false, "", settings().oauth_access, resp);
  }
  if (code != 200) {
    s_lastError = "usage HTTP " + String(code);   // keep last good snapshot; data_stale() handles it
    return;
  }
  s_needsToken = false;
  fillFromUsage(resp);
}

}  // namespace

// ── public API ──────────────────────────────────────────────
void data_begin() {
  s_snap = Snapshot();
  s_lastError = "";
  s_lastPollMs = 0;
  s_firstPoll = true;
  s_needsToken = false;
}

void data_loop() {
  if (!net_online()) return;  // wait for WiFi; keep last snapshot meanwhile
  uint32_t now = millis();
  // Boundary re-poll: the instant the 5h window's reset time passes, fetch once right away so a
  // freshly-started window's countdown appears promptly (not after up to a full poll interval).
  // Once per reset epoch, so an idle (unchanging) resets_at doesn't trigger repeated polls.
  uint32_t r = s_snap.five_hour.resets_at, ne = nowEpoch();
  if (r && ne >= r && s_resetRepollFor != r) { s_resetRepollFor = r; s_firstPoll = true; }
  if (s_firstPoll || (now - s_lastPollMs) >= (uint32_t)settings().poll_seconds * 1000) {
    s_firstPoll = false;
    poll();
  }
}

bool data_valid() { return s_snap.valid; }

bool data_needs_token() { return s_needsToken; }

void data_note_token_synced() {
  s_tokenRcvd  = true;     // UI shows the "received" confirmation
  s_needsToken = false;    // optimistic; the immediate poll below confirms
  s_firstPoll  = true;     // poll now with the new token instead of waiting for the interval
  s_tokenUpdatedEpoch = nowEpoch();
}

// Device-screen token info. last_sync = epoch of the last on-device token update (sync/refresh),
// 0 if none this boot or no clock yet. expires_at = the access token's expiry (next refresh ~ that
// minus a few min); 0 if unknown.
uint32_t data_token_last_sync_epoch() { return s_tokenUpdatedEpoch; }
uint32_t data_token_expires_at()      { return settings().oauth_expires_at; }

bool data_take_token_synced() {   // one-shot consume for the presenter
  if (!s_tokenRcvd) return false;
  s_tokenRcvd = false;
  return true;
}

bool data_stale() {
  if (!s_snap.valid) return true;
  // Age-based: keep showing the last values through a brief WiFi drop; blank once older than
  // ~3 poll intervals. Polling pauses while offline, so the snapshot simply ages.
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
