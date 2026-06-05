#include "app_net.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include "app_settings.h"
#include "app_config.h"   // DEVICE_NAME -> claude-monitor.local

static volatile bool     s_online     = false;
static volatile uint32_t s_ip_raw     = 0;   // IPAddress packed as a 32-bit word (atomic across tasks)
static volatile uint32_t s_down_since = 0;   // millis() we went offline; 0 while online

// Runs in the WiFi event task — keep it minimal. Reconnect is driven from net_loop()
// (NOT here): calling WiFi.reconnect() inside the event callback races the core's own
// autoreconnect and re-fires on every disconnect event, which can wedge the radio.
static void onEvent(WiFiEvent_t e) {
  switch (e) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      s_ip_raw     = (uint32_t)WiFi.localIP();
      s_online     = true;
      s_down_since = 0;
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      s_online = false;
      if (s_down_since == 0) s_down_since = millis();
      break;
    default:
      break;
  }
}

void net_begin() {
  WiFi.onEvent(onEvent);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.setSleep(false);   // disable modem power-save — stops the periodic beacon-miss drops (USB-powered, always-on)
  WiFi.begin(settings().wifi_ssid, settings().wifi_pass);
}

void net_loop() {
  // mDNS (claude-monitor.local) — start once online, tear down on drop so it re-announces on
  // reconnect. Done here (main loop), not in the WiFi event task. Lets the setup script reach the
  // device by name instead of chasing its DHCP IP.
  static bool s_mdns_up = false;
  if (s_online && !s_mdns_up) {
    if (MDNS.begin(DEVICE_NAME)) { MDNS.addService("http", "tcp", 80); s_mdns_up = true; }
  } else if (!s_online && s_mdns_up) {
    MDNS.end();
    s_mdns_up = false;
  }

  // Backoff reconnect: let the core's autoreconnect try first, then nudge every ~10s while down.
  static uint32_t lastTry = 0;
  if (s_online || s_down_since == 0) return;
  uint32_t now = millis();
  if (now - s_down_since < 3000) return;   // grace for autoreconnect
  if (now - lastTry    < 10000) return;    // then retry on a 10s cadence
  lastTry = now;
  WiFi.reconnect();
}

bool        net_online() { return s_online; }
String      net_ip()     { return IPAddress((uint32_t)s_ip_raw).toString(); }  // format fresh, no shared String
int         net_rssi()   { return s_online ? WiFi.RSSI() : 0; }
const char *net_ssid()   { return settings().wifi_ssid; }
