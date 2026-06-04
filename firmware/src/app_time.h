#pragma once
#include <Arduino.h>
// Time (F6): NTP over Wi-Fi + PCF85063 RTC for persistence across reboots/offline.
// Timezone is baked in (Ireland). RTC stores LOCAL time (human-readable, like the
// Waveshare example); system clock is seeded from it at boot, then NTP corrects it.

void     time_begin();   // init RTC, seed clock from it, start NTP (non-blocking)
void     time_loop();    // call from loop(); persists NTP->RTC once synced
bool     time_valid();   // true once we have a plausible wall clock (>= 2024)
uint32_t time_now();     // current epoch seconds (UTC), 0 if unknown

void time_fmt_clock(char *out, size_t n);                 // local "HH:MM"
void time_fmt_date(char *out, size_t n);                  // local "WED 04 JUN 2026"
void time_fmt_hm(uint32_t epoch, char *out, size_t n);    // local "4:30 PM" for an epoch
