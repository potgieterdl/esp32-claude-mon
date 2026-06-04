#pragma once
#include <WebServer.h>
// Shared HTTP server (synchronous, built-in). Features attach routes via web_server().
// Light heap footprint; OTA (ElegantOTA) + screenshot endpoint plug in here later.
WebServer &web_server();   // shared instance (port 80)
void       web_begin();    // register base routes + start listening
void       web_handle();   // call from loop()
