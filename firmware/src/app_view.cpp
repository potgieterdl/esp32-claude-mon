#include "app_view.h"
#include <Arduino.h>
#include "ui.h"
#include "app_data.h"
#include "app_time.h"
#include "app_net.h"
#include "app_audio.h"
#include "app_config.h"
#include "app_settings.h"

#define BAT_ADC 0    // battery sense (VBAT/3 via on-board divider)
#define BAT_EN  15   // drive HIGH to enable the divider

// Soft chime once per usage-threshold crossing (debounced, 5% hysteresis).
// Thresholds are runtime settings (settings().warn_pct / max_pct).
static void audio_check_usage(int pct) {
  static bool warned = false, maxed = false;
  int warn = settings().warn_pct, mx = settings().max_pct;
  if (pct >= mx)        { if (!maxed)  { audio_chime_reset(); maxed  = true; } }
  else if (pct >= warn) { if (!warned) { audio_chime_warn();  warned = true; } }
  if (pct < warn - 5) warned = false;   // 5% hysteresis so it doesn't re-chime on jitter
  if (pct < mx   - 5) maxed  = false;
}

void view_begin() {
  pinMode(BAT_EN, OUTPUT);
  digitalWrite(BAT_EN, HIGH);
}

void view_tick() {
  static uint32_t lastPush = 0;
  uint32_t now = millis();
  if (now - lastPush < 1000) return;   // ~1 Hz
  lastPush = now;

  if (time_valid()) data_set_now(time_now());

  // Only show usage figures when we have FRESH data; otherwise blank them so the
  // screen never displays stale/placeholder numbers while offline (honest display).
  if (data_valid() && !data_stale()) {
    char at[24] = "", next[24] = "";
    uint32_t r = data_five_hour_resets_at();
    if (time_valid() && r) {
      char hm[12]; time_fmt_hm(r, hm, sizeof hm);
      snprintf(at,   sizeof at,   "at %s", hm);
      snprintf(next, sizeof next, "next reset %s", hm);
    }
    ui_set_session(data_five_hour_pct(), data_five_hour_secs_left(), at);
    ui_set_weekly(data_weekly_pct(), data_weekly_secs_left());
    ui_set_clock_reset(next);
    audio_check_usage(data_five_hour_pct());
  } else {
    ui_clear_usage();
  }
  ui_set_online(net_online(), data_stale());

  // Clock keys off time validity (RTC keeps time even if WiFi drops) — only fall back to
  // "--:--" when we genuinely have no time. CONNECTING vs SYNCING distinguishes the reason.
  if (time_valid()) {
    char tb[8], db[20];
    time_fmt_clock(tb, sizeof tb); time_fmt_date(db, sizeof db);
    ui_set_clock(tb, db);
  } else if (!net_online()) {
    ui_set_clock("--:--", "CONNECTING");
  } else {
    ui_set_clock("--:--", "SYNCING");
  }

  // One-time smooth reveal: we boot on the Clock screen; the first time we're connected with
  // live data, slide over to the Session screen so usage is front-and-centre.
  static bool revealed = false;
  if (!revealed && net_online() && data_valid() && !data_stale()) {
    ui_goto_anim(0);
    revealed = true;
  }

  // Device screen
  bool on = net_online();
  int vbat = analogReadMilliVolts(BAT_ADC) * 3;
  int bpct = (vbat - 3000) * 100 / (4200 - 3000);
  if (bpct < 0) bpct = 0; if (bpct > 100) bpct = 100;
  char sigBuf[16], battBuf[12], heapBuf[16];
  if (on) snprintf(sigBuf, sizeof sigBuf, "%d dBm", net_rssi());
  else    snprintf(sigBuf, sizeof sigBuf, "-");
  snprintf(battBuf, sizeof battBuf, "%d%%", bpct);
  snprintf(heapBuf, sizeof heapBuf, "%u KB", (unsigned)(ESP.getFreeHeap() / 1024));
  ui_set_device(on ? net_ssid() : "offline", on ? net_ip().c_str() : "-",
                sigBuf, battBuf, heapBuf, FW_VERSION);
}
