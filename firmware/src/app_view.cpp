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

enum { NOTI_TOKEN = 1 };   // notification slot id for the token modal

// Relative-time formatters for the Device-screen token rows.
static void fmt_ago(char *b, size_t n, uint32_t ep, uint32_t now) {
  if (!ep || !now || now < ep) { strlcpy(b, ep ? "ok" : "-", n); return; }
  uint32_t d = now - ep;
  if (d < 60)         strlcpy(b, "just now", n);
  else if (d < 3600)  snprintf(b, n, "%lum ago", (unsigned long)(d / 60));
  else if (d < 86400) snprintf(b, n, "%luh ago", (unsigned long)(d / 3600));
  else                snprintf(b, n, "%lud ago", (unsigned long)(d / 86400));
}
static void fmt_in(char *b, size_t n, uint32_t ep, uint32_t now) {
  if (!ep || !now)    { strlcpy(b, "-", n); return; }
  if (ep <= now)      { strlcpy(b, "due", n); return; }
  uint32_t d = ep - now;
  if (d < 3600)       snprintf(b, n, "in %lum", (unsigned long)(d / 60));
  else if (d < 86400) snprintf(b, n, "in %luh", (unsigned long)(d / 3600));
  else                snprintf(b, n, "in %lud", (unsigned long)(d / 86400));
}

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

  // Token notification (reusable modal): a fresh sync shows an OK-to-dismiss confirmation
  // (prio 10, outranks the prompt); otherwise a passive "run the sync script" prompt while a
  // token is needed; else cleared. NOTI_TOKEN is this notification's slot id.
  if (data_take_token_synced())
    ui_modal_show(NOTI_TOKEN, UI_SEV_OK, 10, LV_SYMBOL_OK " TOKEN RECEIVED",
                  "New token received.\nUpdating your usage now.", "OK", nullptr, nullptr);
  else if (data_needs_token())
    ui_modal_show(NOTI_TOKEN, UI_SEV_WARN, 5, "TOKEN NEEDED",
                  "Run on your computer:\nnode claude_token_sync.js", nullptr, nullptr, nullptr);
  else
    ui_modal_clear(NOTI_TOKEN);

  // Only show usage figures when we have FRESH data; otherwise blank them so the
  // screen never displays stale/placeholder numbers while offline (honest display).
  if (data_valid() && !data_stale()) {
    int  fh_pct = data_five_hour_pct();
    long fh_secs = data_five_hour_secs_left();
    bool fh_active = fh_secs > 0;                  // a live window counting down
    char at[24] = "", next[24] = "";
    uint32_t r = data_five_hour_resets_at();
    if (fh_active && time_valid() && r) {          // only show a reset time for a live window
      char hm[12]; time_fmt_hm(r, hm, sizeof hm);
      snprintf(at,   sizeof at,   "at %s", hm);
      snprintf(next, sizeof next, "next reset %s", hm);
    }
    // No active 5h window (elapsed while idle: countdown done + usage back at 0) → "No current
    // session" instead of a stuck 0:00; otherwise drive the live countdown.
    if (!fh_active && fh_pct == 0)
      ui_set_session_idle(fh_pct);
    else
      ui_set_session(fh_pct, fh_secs, at);
    ui_set_weekly(data_weekly_pct(), data_weekly_secs_left());
    ui_set_clock_reset(next);                       // "" when idle → hides the Clock "next reset" line
    audio_check_usage(data_five_hour_pct());
    // Plan badge from the real tier (e.g. "max_5x" -> "MAX 5X"); skip until known.
    const char *plan = data_plan();
    if (plan && plan[0] && strcmp(plan, "unknown") != 0) {
      char pb[24]; size_t j = 0;
      for (size_t i = 0; plan[i] && j < sizeof(pb) - 1; i++)
        pb[j++] = (plan[i] == '_') ? ' ' : (char)toupper((unsigned char)plan[i]);
      pb[j] = '\0';
      ui_set_plan(pb);
    }
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

  // Token rows: when it was last synced/refreshed, and when it next auto-refreshes.
  uint32_t now_ep = time_valid() ? time_now() : 0;
  char tokBuf[16], renewBuf[16];
  fmt_ago(tokBuf,   sizeof tokBuf,   data_token_last_sync_epoch(), now_ep);
  fmt_in (renewBuf, sizeof renewBuf, data_token_expires_at(),      now_ep);
  ui_set_token_info(tokBuf, renewBuf);
}
