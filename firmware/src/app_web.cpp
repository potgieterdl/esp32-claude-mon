#include "app_web.h"
#include "app_net.h"
#include "app_config.h"
#include "app_settings.h"
#include <ElegantOTA.h>

static WebServer server(80);

WebServer &web_server() { return server; }

// Basic-auth gate (user "admin", pass = proxy token) for the endpoints that expose secrets.
static bool require_auth() {
  if (server.authenticate("admin", settings().proxy_token)) return true;
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

  // Settings as JSON (Feature F0/F3). Auth-protected — it holds Wi-Fi + proxy secrets.
  //   GET  /config.json            -> current settings
  //   PUT  /config.json  {json}    -> merge + persist to LittleFS, returns the new settings
  server.on("/config.json", HTTP_GET, []() {
    if (!require_auth()) return;
    server.send(200, "application/json", settings_to_json());
  });
  auto apply_cfg = []() {
    if (!require_auth()) return;
    String err;
    if (settings_apply_json(server.arg("plain"), err))
      server.send(200, "application/json", settings_to_json());
    else
      server.send(400, "application/json", String("{\"error\":\"") + err + "\"}");
  };
  server.on("/config.json", HTTP_PUT,  apply_cfg);
  server.on("/config.json", HTTP_POST, apply_cfg);   // POST accepted too, for curl convenience

  // OTA at /update — HTTP basic-auth (user "admin", pass = shared token).
  // Pass creds to begin(); a separate setAuth() gets cleared by begin()'s default empty creds.
  ElegantOTA.begin(&server, "admin", settings().proxy_token);

  server.begin();
}

void web_handle() {
  server.handleClient();
  ElegantOTA.loop();   // handles the post-upload reboot
}
