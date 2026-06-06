#include "app_view.h"
#include <Arduino.h>
#include "ui.h"
#include "app_data.h"
#include "app_time.h"
#include "app_net.h"
#include "app_audio.h"
#include "app_config.h"
#include "app_settings.h"
#include "app_notify.h"

#define BAT_ADC 0    // battery sense (VBAT/3 via on-board divider)
#define BAT_EN  15   // drive HIGH to enable the divider

enum { NOTI_TOKEN = 1, NOTI_INPUT = 2 };   // notification slot ids (token modal / "input needed" banner)

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

void view_tick(uint32_t input_idle_ms) {
  static uint32_t lastPush = 0;
  uint32_t now = millis();
  if (now - lastPush < 1000) return;   // ~1 Hz
  lastPush = now;

  if (time_valid()) data_set_now(time_now());

  // ── Notifications (single modal slot, priority-ordered) ──────────────────────────────────────
  // 10 TOKEN RECEIVED (ack, one-shot)  >  7 INPUT NEEDED (passive)  >  5 TOKEN NEEDED (passive).
  // The ack-modal is protected by ui_modal_show's own guard; the two PASSIVE modals share the one
  // slot, so we pick exactly one winner each tick (a later show would otherwise stomp an earlier).
  // Safety net: clear a stuck alert whose "clear" POST never arrived (e.g. a permission dialog was
  // approved rather than a prompt submitted). Owned here so the notify module stays pure state.
  if (notify_input_age_s() >= NOTIFY_INPUT_TIMEOUT_S) notify_input_clear();
  bool input_waiting = notify_input_active();   // a Claude Code session is waiting (issue #2)

  // Chime once when a session STARTS waiting (rising edge) — same debounced model as the usage FSM.
  static bool input_prev = false;
  if (input_waiting && !input_prev) audio_chime_warn();
  input_prev = input_waiting;

  // (1) A fresh token sync shows an OK-to-dismiss confirmation; protected once up.
  if (data_take_token_synced())
    ui_modal_show(NOTI_TOKEN, UI_SEV_OK, 10, LV_SYMBOL_OK " TOKEN RECEIVED",
                  "New token received.\nUpdating your usage now.", "OK", nullptr, nullptr);

  // (2) "Input needed" banner — project name in the body; outranks the passive token prompt.
  if (input_waiting) {
    const char *proj = notify_input_project();
    ui_modal_show(NOTI_INPUT, UI_SEV_WARN, 7, LV_SYMBOL_WARNING " INPUT NEEDED",
                  (proj && proj[0]) ? proj : "A Claude Code session is waiting.",
                  nullptr, nullptr, nullptr);
  } else {
    ui_modal_clear(NOTI_INPUT);
  }

  // (3) Passive "run the sync script" prompt — only claims the slot when nothing above holds it.
  if (!input_waiting && data_needs_token())
    ui_modal_show(NOTI_TOKEN, UI_SEV_WARN, 5, "TOKEN NEEDED",
                  "Run on your computer:\nnode claude_token_sync.js", nullptr, nullptr, nullptr);
  else if (!input_waiting)
    ui_modal_clear(NOTI_TOKEN);

  // Only show usage figures when we have FRESH data; otherwise blank them so the
  // screen never displays stale/placeholder numbers while offline (honest display).
  if (data_valid() && !data_stale()) {
    int  fh_pct = data_five_hour_pct();
    long fh_secs = data_five_hour_secs_left();
    bool fh_active = fh_secs > 0;                  // a live window counting down
    // Idle = the window has *elapsed* (secs_left == 0, not -1) AND usage drained to 0 → genuinely no
    // active window. Keying off == 0 (not !active) avoids mislabeling the secs_left == -1 "no clock
    // yet" case as "No current session".
    bool fh_idle = (fh_secs == 0 && fh_pct == 0);
    char at[24] = "", next[24] = "";
    uint32_t r = data_five_hour_resets_at();
    if (fh_active && time_valid() && r) {          // only show a reset time for a live window
      char hm[12]; time_fmt_hm(r, hm, sizeof hm);
      snprintf(at,   sizeof at,   "at %s", hm);
      snprintf(next, sizeof next, "next reset %s", hm);
    }
    if (fh_idle)
      ui_set_session_idle(fh_pct);                 // "No current session" instead of a stuck 0:00
    else
      ui_set_session(fh_pct, fh_secs, at);         // live countdown (or "--:--" when secs_left < 0)
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

  // ── Idle "sleep mode" (#6) ────────────────────────────────────────────────
  // We boot on the Clock screen. When a live 5-hour window appears we slide to Session so usage is
  // front-and-centre (this also serves as the first-connect reveal). When Claude has been *fully
  // idle* (window expired → ring "--") for sleep_after_s AND there's been no touch for that long, we
  // drift back to the Clock and a small bot dozes. A touch nudges the bot awake (staying put); fresh
  // activity wakes it straight to Session. sleep_after_s == 0 disables the whole behaviour.
  bool fresh  = data_valid() && !data_stale();
  bool active = fresh && data_five_hour_secs_left() > 0;                                  // live window
  bool idle   = fresh && data_five_hour_secs_left() == 0 && data_five_hour_pct() == 0;    // fully expired
  uint32_t sleep_after = (uint32_t)settings().sleep_after_s * 1000;

  static bool     was_active = false;   // last *definite* activity reading (held across stale blips)
  static bool     asleep     = false;
  static uint32_t idle_since = 0;
  if (!idle) idle_since = now;          // the idle clock only runs while we positively know it's idle

  if (active && !was_active) {                       // activity resumed (incl. first connect) → Session
    ui_set_sleeping(false); asleep = false;
    ui_goto_anim(0);
  } else if (asleep && input_idle_ms < 2000) {       // a touch nudges the bot awake; stay where we are
    ui_set_sleeping(false); asleep = false;
  } else if (!asleep && idle && sleep_after &&       // long-idle + untouched → drift to the dozing Clock
             (now - idle_since) >= sleep_after && input_idle_ms >= sleep_after) {
    ui_goto_anim(2);
    ui_set_sleeping(true); asleep = true;
  }
  // Latch only on a *definite* classification (active or fully-idle). The transient secs_left == -1
  // reading (null five_hour / time-sync drop) and stale/offline blips leave was_active untouched, so
  // they can't re-fire the wake and yank the user off the screen they're on.
  if (active || idle) was_active = active;

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
