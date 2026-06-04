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
#ifndef CFG_PROXY_URL
#define CFG_PROXY_URL   "http://192.168.1.50:7890/usage"
#endif
#ifndef CFG_PROXY_TOKEN
#define CFG_PROXY_TOKEN ""
#endif
#ifndef CFG_POLL_SECONDS
#define CFG_POLL_SECONDS 30
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
  if (g.poll_seconds < 10)   g.poll_seconds = 10;     // don't hammer the proxy
  if (g.poll_seconds > 3600) g.poll_seconds = 3600;
  if (g.dim_after_s < 3)     g.dim_after_s = 3;
  if (g.dim_after_s > 3600)  g.dim_after_s = 3600;
}

static void seed_defaults() {
  strlcpy(g.wifi_ssid,   CFG_WIFI_SSID,   sizeof g.wifi_ssid);
  strlcpy(g.wifi_pass,   CFG_WIFI_PASS,   sizeof g.wifi_pass);
  strlcpy(g.proxy_url,   CFG_PROXY_URL,   sizeof g.proxy_url);
  strlcpy(g.proxy_token, CFG_PROXY_TOKEN, sizeof g.proxy_token);
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
  JsonObjectConst p = o["proxy"];
  if (!p.isNull()) {
    if (p["url"].is<const char *>())   strlcpy(g.proxy_url,   p["url"],   sizeof g.proxy_url);
    if (p["token"].is<const char *>()) strlcpy(g.proxy_token, p["token"], sizeof g.proxy_token);
  }
  if (o["poll_seconds"].is<int>()) g.poll_seconds = o["poll_seconds"].as<int>();
  JsonObjectConst th = o["thresholds"];
  if (!th.isNull()) {
    if (th["warn_pct"].is<int>()) g.warn_pct = th["warn_pct"].as<int>();
    if (th["max_pct"].is<int>())  g.max_pct  = th["max_pct"].as<int>();
  }
  if (o["tz"].is<const char *>()) strlcpy(g.tz, o["tz"], sizeof g.tz);
  JsonObjectConst d = o["display"];
  if (!d.isNull()) {
    if (d["brightness"].is<int>())      g.brightness     = d["brightness"].as<int>();
    if (d["dim_on_idle"].is<bool>())    g.dim_on_idle    = d["dim_on_idle"].as<bool>();
    if (d["dim_after_s"].is<int>())     g.dim_after_s    = d["dim_after_s"].as<int>();
    if (d["dim_brightness"].is<int>())  g.dim_brightness = d["dim_brightness"].as<int>();
  }
  clamp_all();
}

static void build_json(JsonDocument &doc) {
  JsonObject w = doc["wifi"].to<JsonObject>();
  w["ssid"] = g.wifi_ssid;  w["pass"] = g.wifi_pass;
  JsonObject p = doc["proxy"].to<JsonObject>();
  p["url"]  = g.proxy_url;  p["token"] = g.proxy_token;
  doc["poll_seconds"] = g.poll_seconds;
  JsonObject th = doc["thresholds"].to<JsonObject>();
  th["warn_pct"] = g.warn_pct;  th["max_pct"] = g.max_pct;
  doc["tz"] = g.tz;
  JsonObject d = doc["display"].to<JsonObject>();
  d["brightness"]     = g.brightness;
  d["dim_on_idle"]    = g.dim_on_idle;
  d["dim_after_s"]    = g.dim_after_s;
  d["dim_brightness"] = g.dim_brightness;
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
