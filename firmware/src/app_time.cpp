#include "app_time.h"
#include <time.h>
#include <sys/time.h>
#include <ctype.h>
#include <Wire.h>
#include "esp_sntp.h"
#include "SensorPCF85063.hpp"
#include "app_settings.h"

#define EPOCH_2024 1704067200UL   // sanity floor for "valid time"

static SensorPCF85063 rtc;
static bool s_rtc_ok = false;
static bool s_persisted = false;

void time_begin() {
  const char *tz = settings().tz;
  setenv("TZ", tz, 1);
  tzset();

  s_rtc_ok = rtc.begin(Wire);   // shared I2C already started in main
  if (s_rtc_ok) {
    RTC_DateTime d = rtc.getDateTime();   // RTC holds LOCAL time
    struct tm lt = {};
    lt.tm_year = d.getYear() - 1900; lt.tm_mon = d.getMonth() - 1; lt.tm_mday = d.getDay();
    lt.tm_hour = d.getHour(); lt.tm_min = d.getMinute(); lt.tm_sec = d.getSecond();
    lt.tm_isdst = -1;
    time_t t = mktime(&lt);                // local -> UTC epoch (uses TZ)
    if (t > (time_t)EPOCH_2024) {
      struct timeval tv = { t, 0 };
      settimeofday(&tv, nullptr);
    }
  }
  // Start SNTP (syncs whenever Wi-Fi is up; harmless if offline now).
  configTzTime(tz, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
}

void time_loop() {
  // Once NTP has corrected the clock, write it back to the RTC (once).
  if (!s_persisted && s_rtc_ok && sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
    time_t now = time(nullptr);
    struct tm lt; localtime_r(&now, &lt);
    rtc.setDateTime(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
                    lt.tm_hour, lt.tm_min, lt.tm_sec);
    s_persisted = true;
  }
}

bool     time_valid() { return time(nullptr) > (time_t)EPOCH_2024; }
uint32_t time_now()   { time_t t = time(nullptr); return t > 0 ? (uint32_t)t : 0; }

void time_fmt_clock(char *out, size_t n) {
  time_t t = time(nullptr); struct tm lt; localtime_r(&t, &lt);
  strftime(out, n, "%H:%M", &lt);
}

void time_fmt_date(char *out, size_t n) {
  time_t t = time(nullptr); struct tm lt; localtime_r(&t, &lt);
  strftime(out, n, "%a %d %b %Y", &lt);
  for (char *p = out; *p; ++p) *p = toupper((unsigned char)*p);
}

void time_fmt_hm(uint32_t epoch, char *out, size_t n) {
  time_t t = (time_t)epoch; struct tm lt; localtime_r(&t, &lt);
  strftime(out, n, "%I:%M %p", &lt);
  if (out[0] == '0') memmove(out, out + 1, strlen(out));   // "04:30 PM" -> "4:30 PM"
}

void time_fmt_day_hm(uint32_t epoch, char *out, size_t n) {
  time_t t = (time_t)epoch; struct tm lt; localtime_r(&t, &lt);
  strftime(out, n, "%a %H:%M", &lt);                       // "Mon 09:00"
  for (char *p = out; *p; ++p) *p = toupper((unsigned char)*p);   // -> "MON 09:00"
}
