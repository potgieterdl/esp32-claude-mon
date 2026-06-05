#pragma once
#include <Arduino.h>
// WiFi connectivity (non-blocking, auto-reconnect). Foundation module.
void        net_begin();    // start WiFi from settings creds (non-blocking)
void        net_loop();     // call from loop(); drives backoff reconnect when down
bool        net_online();   // true once we have an IP
String      net_ip();       // current IP ("0.0.0.0" if offline)
int         net_rssi();     // RSSI dBm
const char *net_ssid();     // configured SSID
uint32_t    net_connects();    // count of GOT_IP events this boot (initial + reconnects)
uint32_t    net_disconnects(); // count of STA_DISCONNECTED events this boot
