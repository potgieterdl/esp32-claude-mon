#include "app_settings.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

// Compiled defaults come from the repo-root config.json, injected as CFG_* defines by
// firmware/load_config.py (pre-build). These #ifndef fallbacks are placeholders so the project
// still compiles without a config.json (CI, first checkout). Edit settings in config.json, not here.
#ifndef CFG_WIFI_SSID
#define CFG_WIFI_SSID   "YOUR_WIFI_SSID"
#endif
#ifndef CFG_WIFI_PASS
#define CFG_WIFI_PASS   "YOUR_WIFI_PASSWORD"
#endif
#ifndef CFG_DEVICE_TOKEN
#define CFG_DEVICE_TOKEN ""   // basic-auth password for the device's web endpoints (/config.json, OTA)
#endif
#ifndef CFG_POLL_SECONDS
#define CFG_POLL_SECONDS 60   // usage-API poll cadence; gentle (data changes slowly, avoids 429s)
#endif
#ifndef CFG_TZ
#define CFG_TZ           "GMT0IST,M3.5.0/1,M10.5.0"   // Ireland
#endif
#ifndef CFG_WARN_PCT
#define CFG_WARN_PCT     70
#endif
#ifndef CFG_MAX_PCT
#define CFG_MAX_PCT      100
#endif
#ifndef CFG_BRIGHTNESS
#define CFG_BRIGHTNESS   50
#endif
#ifndef CFG_DIM_ON_IDLE
#define CFG_DIM_ON_IDLE  0
#endif
#ifndef CFG_DIM_AFTER_S
#define CFG_DIM_AFTER_S  60
#endif
#ifndef CFG_DIM_BRIGHTNESS
#define CFG_DIM_BRIGHTNESS 10
#endif
#define CONFIG_PATH       "/config.json"

static AppSettings g;

// ── clamps so a bad PUT can't produce nonsense (or brick the backlight) ─────
static uint8_t clamp_pct(int v) { return v < 0 ? 0 : (v > 100 ? 100 : (uint8_t)v); }

static void clamp_all() {
  g.brightness     = clamp_pct(g.brightness);
  g.dim_brightness = clamp_pct(g.dim_brightness);
  g.warn_pct       = clamp_pct(g.warn_pct);
  g.max_pct        = clamp_pct(g.max_pct);
  if (g.poll_seconds < 10)   g.poll_seconds = 10;     // don't hammer the usage API
  if (g.poll_seconds > 3600) g.poll_seconds = 3600;
  if (g.dim_after_s < 3)     g.dim_after_s = 3;
  if (g.dim_after_s > 3600)  g.dim_after_s = 3600;
}

static void seed_defaults() {
  strlcpy(g.wifi_ssid,    CFG_WIFI_SSID,    sizeof g.wifi_ssid);
  strlcpy(g.wifi_pass,    CFG_WIFI_PASS,    sizeof g.wifi_pass);
  strlcpy(g.device_token, CFG_DEVICE_TOKEN, sizeof g.device_token);
  // OAuth token is NOT seeded at build time (no rotating secret in the firmware image);
  // it arrives over WiFi via the sync script. Empty => UI prompts to run the sync script.
  g.oauth_access[0]  = '\0';
  g.oauth_refresh[0] = '\0';
  g.oauth_expires_at = 0;
  g.oauth_tier[0]    = '\0';
  g.poll_seconds   = CFG_POLL_SECONDS;
  g.warn_pct       = CFG_WARN_PCT;
  g.max_pct        = CFG_MAX_PCT;
  strlcpy(g.tz, CFG_TZ, sizeof g.tz);
  g.brightness     = CFG_BRIGHTNESS;
  g.dim_on_idle    = CFG_DIM_ON_IDLE;
  g.dim_after_s    = CFG_DIM_AFTER_S;
  g.dim_brightness = CFG_DIM_BRIGHTNESS;
  clamp_all();
}

// Overlay any fields present in `o` onto the current settings (absent fields keep their value).
static void merge(JsonObjectConst o) {
  JsonObjectConst w = o["wifi"];
  if (!w.isNull()) {
    if (w["ssid"].is<const char *>()) strlcpy(g.wifi_ssid, w["ssid"], sizeof g.wifi_ssid);
    if (w["pass"].is<const char *>()) strlcpy(g.wifi_pass, w["pass"], sizeof g.wifi_pass);
  }
  JsonObjectConst d = o["device"];
  if (!d.isNull()) {
    if (d["token"].is<const char *>()) strlcpy(g.device_token, d["token"], sizeof g.device_token);
    if (d["poll_seconds"].is<int>())   g.poll_seconds = d["poll_seconds"].as<int>();
    if (d["tz"].is<const char *>())    strlcpy(g.tz, d["tz"], sizeof g.tz);
    JsonObjectConst th = d["thresholds"];
    if (!th.isNull()) {
      if (th["warn_pct"].is<int>()) g.warn_pct = th["warn_pct"].as<int>();
      if (th["max_pct"].is<int>())  g.max_pct  = th["max_pct"].as<int>();
    }
    JsonObjectConst disp = d["display"];
    if (!disp.isNull()) {
      if (disp["brightness"].is<int>())     g.brightness     = disp["brightness"].as<int>();
      if (disp["dim_on_idle"].is<bool>())   g.dim_on_idle    = disp["dim_on_idle"].as<bool>();
      if (disp["dim_after_s"].is<int>())    g.dim_after_s    = disp["dim_after_s"].as<int>();
      if (disp["dim_brightness"].is<int>()) g.dim_brightness = disp["dim_brightness"].as<int>();
    }
  }
  // OAuth token blob (top-level "oauth", snake_case) — written by claude_token_sync.js (PUT) and
  // by the device's own refresh. Empty strings clear it (-> UI prompts to run the sync script).
  JsonObjectConst oa = o["oauth"];
  if (!oa.isNull()) {
    if (oa["access_token"].is<const char *>())    strlcpy(g.oauth_access,  oa["access_token"],  sizeof g.oauth_access);
    if (oa["refresh_token"].is<const char *>())   strlcpy(g.oauth_refresh, oa["refresh_token"], sizeof g.oauth_refresh);
    if (oa["rate_limit_tier"].is<const char *>()) strlcpy(g.oauth_tier,    oa["rate_limit_tier"], sizeof g.oauth_tier);
    // expires_at may be epoch seconds or milliseconds; normalise to seconds.
    if (oa["expires_at"].is<long long>() || oa["expires_at"].is<unsigned long long>()) {
      long long e = oa["expires_at"].as<long long>();
      if (e > 100000000000LL) e /= 1000;   // ms -> s
      g.oauth_expires_at = (uint32_t)e;
    }
  }
  clamp_all();
}

static void build_json(JsonDocument &doc) {
  JsonObject w = doc["wifi"].to<JsonObject>();
  w["ssid"] = g.wifi_ssid;  w["pass"] = g.wifi_pass;
  JsonObject d = doc["device"].to<JsonObject>();
  d["token"]        = g.device_token;
  d["poll_seconds"] = g.poll_seconds;
  d["tz"]           = g.tz;
  JsonObject th = d["thresholds"].to<JsonObject>();
  th["warn_pct"] = g.warn_pct;  th["max_pct"] = g.max_pct;
  JsonObject disp = d["display"].to<JsonObject>();
  disp["brightness"]     = g.brightness;
  disp["dim_on_idle"]    = g.dim_on_idle;
  disp["dim_after_s"]    = g.dim_after_s;
  disp["dim_brightness"] = g.dim_brightness;
  JsonObject oa = doc["oauth"].to<JsonObject>();
  oa["access_token"]    = g.oauth_access;
  oa["refresh_token"]   = g.oauth_refresh;
  oa["expires_at"]      = g.oauth_expires_at;
  oa["rate_limit_tier"] = g.oauth_tier;
}

// ── public API ──────────────────────────────────────────────
AppSettings &settings() { return g; }

void settings_begin() {
  seed_defaults();
  if (!LittleFS.begin(true)) {   // format on first mount failure
    Serial.println("[settings] LittleFS mount failed — using compiled defaults");
    return;
  }
  File f = LittleFS.open(CONFIG_PATH, "r");
  if (!f) {
    Serial.println("[settings] no config.json — seeding defaults to flash");
    settings_save();             // write a starter file the user can GET/edit
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    Serial.printf("[settings] config.json parse error (%s) — using defaults\n", err.c_str());
    return;                      // keep compiled defaults; don't overwrite the user's file
  }
  merge(doc.as<JsonObjectConst>());
  Serial.println("[settings] loaded config.json");
}

bool settings_save() {
  File f = LittleFS.open(CONFIG_PATH, "w");
  if (!f) { Serial.println("[settings] save: open failed"); return false; }
  JsonDocument doc;
  build_json(doc);
  bool ok = serializeJsonPretty(doc, f) > 0;
  f.close();
  return ok;
}

String settings_to_json() {
  JsonDocument doc;
  build_json(doc);
  String out;
  serializeJsonPretty(doc, out);
  return out;
}

bool settings_apply_json(const String &body, String &err) {
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, body);
  if (e) { err = String("json: ") + e.c_str(); return false; }
  if (!doc.is<JsonObject>()) { err = "expected a JSON object"; return false; }
  merge(doc.as<JsonObjectConst>());
  if (!settings_save()) { err = "saved in RAM but flash write failed"; return false; }
  return true;
}

bool settings_update_oauth(const char *access, const char *refresh, uint32_t expires_at) {
  if (access)  strlcpy(g.oauth_access,  access,  sizeof g.oauth_access);
  if (refresh) strlcpy(g.oauth_refresh, refresh, sizeof g.oauth_refresh);
  g.oauth_expires_at = expires_at;
  return settings_save();
}

bool settings_has_oauth() {
  return g.oauth_access[0] != '\0' && g.oauth_refresh[0] != '\0';
}
