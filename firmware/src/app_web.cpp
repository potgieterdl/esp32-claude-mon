#include "app_web.h"
#include "app_net.h"
#include "app_config.h"
#include "app_settings.h"
#include "app_data.h"
#include "app_notify.h"
#include <ArduinoJson.h>
#include <ElegantOTA.h>

static WebServer server(80);

WebServer &web_server() { return server; }

// Basic-auth gate (user "admin", pass = the device token) for the endpoints that expose secrets.
static bool require_auth() {
  if (server.authenticate("admin", settings().device_token)) return true;
  server.requestAuthentication();
  return false;
}

void web_begin() {
  server.on("/", []() {
    String h = "<!doctype html><html><head><meta name=viewport content='width=device-width'>";
    h += "<title>" DEVICE_NAME "</title></head>";
    h += "<body style='font-family:system-ui;background:#111;color:#eee;padding:20px'>";
    h += "<h2 style='color:#E8663C'>Claude Monitor</h2>";
    h += "<p>Firmware: " FW_VERSION "</p>";
    h += "<p>WiFi: " + String(net_online() ? "online" : "offline") + " (" + net_ip() + ")</p>";
    h += "<p>Uptime: " + String(millis() / 1000) + "s &middot; Free heap: " +
         String(ESP.getFreeHeap() / 1024) + " KB</p>";
    h += "<p><a style='color:#E8663C' href='/update'>Firmware update (OTA)</a> &middot; "
         "<code>GET/PUT /config.json</code> (auth: admin / token)</p>";
    h += "</body></html>";
    server.send(200, "text/html", h);
  });

  // Settings as JSON (Feature F0/F3). Auth-protected — it holds Wi-Fi + OAuth secrets.
  //   GET  /config.json            -> current settings
  //   PUT  /config.json  {json}    -> merge + persist to LittleFS, returns the new settings
  server.on("/config.json", HTTP_GET, []() {
    if (!require_auth()) return;
    server.send(200, "application/json", settings_to_json());
  });
  auto apply_cfg = []() {
    if (!require_auth()) return;
    String body = server.arg("plain");
    String err;
    if (settings_apply_json(body, err)) {
      // If this PUT delivered a USABLE OAuth token, confirm receipt on-screen and re-poll. Guard on
      // settings_has_oauth() so a token-clearing PUT (empty strings) doesn't flash a false "received".
      if ((body.indexOf("access_token") >= 0 || body.indexOf("refresh_token") >= 0) && settings_has_oauth())
        data_note_token_synced();
      server.send(200, "application/json", settings_to_json());
    } else
      server.send(400, "application/json", String("{\"error\":\"") + err + "\"}");
  };
  server.on("/config.json", HTTP_PUT,  apply_cfg);
  server.on("/config.json", HTTP_POST, apply_cfg);   // POST accepted too, for curl convenience

  // Live usage snapshot as JSON (auth-protected) — for setup/verification/debugging. Mirrors the
  // numbers on screen + whether a token sync is needed and the last fetch error.
  server.on("/status", HTTP_GET, []() {
    if (!require_auth()) return;
    JsonDocument doc;
    doc["plan"]        = data_plan();
    doc["valid"]       = data_valid();
    doc["stale"]       = data_stale();
    doc["needs_token"] = data_needs_token();
    doc["age_seconds"] = data_age_seconds();
    doc["last_error"]  = data_last_error();
    doc["free_heap"]   = (uint32_t)ESP.getFreeHeap();
    JsonObject f = doc["five_hour"].to<JsonObject>();
    f["used_pct"] = data_five_hour_pct();  f["secs_left"] = data_five_hour_secs_left();
    JsonObject w = doc["weekly"].to<JsonObject>();
    w["used_pct"] = data_weekly_pct();     w["secs_left"] = data_weekly_secs_left();
    if (data_has_weekly_opus()) doc["weekly_opus_pct"] = data_weekly_opus_pct();
    String out; serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  // "Needs input" alert (issue #2) — a Claude Code hook POSTs here when a session is waiting on
  // the user, and again to clear it. Auth-protected (same device token). Body is JSON:
  //   {"event":"needs_input"}   -> raise banner + chime
  //   {"event":"clear"}         -> lower banner
  // Any extra fields are ignored. The presenter (app_view) reads notify_input_* each tick.
  server.on("/notify", HTTP_POST, []() {
    if (!require_auth()) return;
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
      server.send(400, "application/json", "{\"error\":\"bad json\"}");
      return;
    }
    const char *event = doc["event"] | "";
    if (!strcmp(event, "needs_input")) {
      notify_input_set();
      server.send(200, "application/json", "{\"ok\":true,\"state\":\"needs_input\"}");
    } else if (!strcmp(event, "clear")) {
      notify_input_clear();
      server.send(200, "application/json", "{\"ok\":true,\"state\":\"clear\"}");
    } else {
      server.send(400, "application/json", "{\"error\":\"event must be needs_input or clear\"}");
    }
  });

  // OTA at /update — HTTP basic-auth (user "admin", pass = the device token).
  // Pass creds to begin(); a separate setAuth() gets cleared by begin()'s default empty creds.
  ElegantOTA.begin(&server, "admin", settings().device_token);

  server.begin();
}

void web_handle() {
  server.handleClient();
  ElegantOTA.loop();   // handles the post-upload reboot
}
