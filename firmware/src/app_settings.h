#pragma once
#include <Arduino.h>
// Runtime settings (Feature F0/F3) — the device keeps a config.json on its LittleFS, editable
// over the LAN via GET/PUT /config.json. Precedence: compiled defaults (CFG_* defines injected from
// the repo-root config.json by firmware/load_config.py)  <-  device config.json  <-  live PUT edits.
//
// The compiled CFG_* values are always the FALLBACK, so a missing or malformed device config.json
// can never lock the device off the network — it just boots on the built-in creds.

struct AppSettings {
  // — secrets (fallback to compiled CFG_* defaults from config.json) —
  char     wifi_ssid[33];
  char     wifi_pass[65];
  char     device_token[97];  // basic-auth password for the device's web endpoints (/config.json, OTA)
  // — Claude OAuth (device calls api.anthropic.com/api/oauth/usage directly; no proxy) —
  // Delivered over WiFi by claude_token_sync.js (PUT /config.json), persisted to LittleFS, and
  // refreshed on-device via platform.claude.com. Empty until first sync -> UI shows "run sync".
  char     oauth_access[320];   // Bearer access token (sk-ant-oat01-…), short-lived (~8h)
  char     oauth_refresh[320];  // refresh token (sk-ant-ort01-…), rotates on each refresh
  uint32_t oauth_expires_at;    // epoch secs (UTC) when the access token expires; 0 = unknown
  char     oauth_tier[40];      // rateLimitTier (e.g. "default_claude_max_5x") -> plan label
  // — tuning —
  uint16_t poll_seconds;     // usage-API poll cadence
  uint8_t  warn_pct;         // usage % that triggers the soft 2-note chime
  uint8_t  max_pct;          // usage % that triggers the reset/maxed chime
  // — time —
  char     tz[48];           // POSIX TZ string (e.g. Ireland "GMT0IST,M3.5.0/1,M10.5.0")
  // — display —
  uint8_t  brightness;       // 0..100 active backlight level
  bool     dim_on_idle;      // auto-dim the backlight when there's been no touch
  uint16_t dim_after_s;      // idle seconds before dimming
  uint8_t  dim_brightness;   // 0..100 dimmed level (used only when dim_on_idle)
  uint16_t sleep_after_s;    // idle+untouched seconds before the Clock "sleep mode" (#6); 0 = never
};

void          settings_begin();        // mount LittleFS, load config.json (seed from defaults if absent)
AppSettings  &settings();              // mutable global accessor
bool          settings_save();         // serialize current settings -> /config.json
String        settings_to_json();      // serialize current settings to a JSON string (GET body)
bool          settings_apply_json(const String &body, String &err);  // merge JSON over current + save
// Persist a freshly-refreshed OAuth token (called by the on-device refresh). Writes to LittleFS
// so the rotated refresh token survives reboots/OTA. Pass expires_at as epoch seconds.
bool          settings_update_oauth(const char *access, const char *refresh, uint32_t expires_at);
bool          settings_has_oauth();   // true once a token has been synced (access+refresh present)
