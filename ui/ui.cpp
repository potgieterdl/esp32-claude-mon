// Portable LVGL UI — Claude monitor. No Arduino/hardware dependencies.
#include "ui.h"
#include <stdio.h>

// ── Theme ───────────────────────────────────────────────────
#define C_BG    lv_color_hex(0x000000)  // true black (deepest on this IPS, esp. with dimmed backlight)
#define C_INK   lv_color_hex(0xF2ECE0)
#define C_DIM   lv_color_hex(0x9A9384)
#define C_CORAL lv_color_hex(0xE8663C)  // richer Claude coral
#define C_GREEN lv_color_hex(0x46C877)
#define C_AMBER lv_color_hex(0xF0A93A)
#define C_RED   lv_color_hex(0xE5484D)
#define C_TRACK lv_color_hex(0x2A2620)
#define C_BLUE  lv_color_hex(0x3DA5FF)  // "online, awaiting proxy data" — distinct from red

// live-data handles (F4) — captured in build_*; updated by ui_set_*.
static lv_obj_t *g_sess_ring = nullptr, *g_sess_pct = nullptr, *g_sess_wk_bar = nullptr, *g_sess_at = nullptr;
static lv_obj_t *g_sess_wk_pct = nullptr;   // Session-screen weekly-% label (was a discarded handle → froze at mock 41%)
static lv_obj_t *g_wk_ring = nullptr, *g_wk_pct = nullptr;
static lv_obj_t *g_clock_time = nullptr, *g_clock_date = nullptr, *g_clock_next = nullptr;
static lv_obj_t *g_status_dots[4];
static int       g_status_dot_n = 0;
// device-screen value labels (F7)
static lv_obj_t *g_dev_wifi = nullptr, *g_dev_ip = nullptr, *g_dev_sig = nullptr,
                *g_dev_batt = nullptr, *g_dev_heap = nullptr, *g_dev_fw = nullptr,
                *g_dev_proxy = nullptr;

// ── small style helpers ─────────────────────────────────────
static void plain(lv_obj_t *o) {
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_set_style_radius(o, 0, 0);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *mklabel(lv_obj_t *p, const char *txt, const lv_font_t *f, lv_color_t c) {
  lv_obj_t *l = lv_label_create(p);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, c, 0);
  return l;
}

static lv_obj_t *mkring(lv_obj_t *p, int val, lv_color_t col, int size) {
  lv_obj_t *a = lv_arc_create(p);
  lv_obj_set_size(a, size, size);
  lv_arc_set_rotation(a, 270);
  lv_arc_set_bg_angles(a, 0, 360);
  lv_arc_set_range(a, 0, 100);
  lv_arc_set_value(a, val);
  lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(a, 11, LV_PART_MAIN);
  lv_obj_set_style_arc_color(a, C_TRACK, LV_PART_MAIN);
  lv_obj_set_style_arc_width(a, 11, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(a, col, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_set_style_pad_all(a, 0, LV_PART_KNOB);
  return a;
}

static void topbar(lv_obj_t *t, const char *badge, lv_color_t bg, lv_color_t fg) {
  lv_obj_t *brand = mklabel(t, "CLAUDE", &lv_font_montserrat_16, C_CORAL);
  lv_obj_set_style_text_letter_space(brand, 2, 0);
  lv_obj_align(brand, LV_ALIGN_TOP_LEFT, 18, 9);

  lv_obj_t *b = mklabel(t, badge, &lv_font_montserrat_12, fg);
  lv_obj_set_style_bg_color(b, bg, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_hor(b, 7, 0);
  lv_obj_set_style_pad_ver(b, 2, 0);
  lv_obj_set_style_radius(b, 8, 0);
  lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 7);

  lv_obj_t *dot = lv_obj_create(t);
  lv_obj_set_size(dot, 8, 8);
  lv_obj_set_style_radius(dot, 4, 0);
  lv_obj_set_style_bg_color(dot, C_GREEN, 0);
  lv_obj_set_style_border_width(dot, 0, 0);
  lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(dot, LV_ALIGN_TOP_RIGHT, -62, 11);
  if (g_status_dot_n < 4) g_status_dots[g_status_dot_n++] = dot;
  lv_obj_t *wf = mklabel(t, "WI-FI", &lv_font_montserrat_12, C_DIM);
  lv_obj_align(wf, LV_ALIGN_TOP_RIGHT, -18, 10);
}

// ── live countdown (sample) ─────────────────────────────────
static lv_obj_t *g_countdown = nullptr;
static int32_t g_secs = -1;  // <0 = no data yet → show "--:--" (set by ui_set_session once live)

static void tick_1s(lv_timer_t *) {
  if (!g_countdown) return;
  if (g_secs < 0) { lv_label_set_text(g_countdown, "--:--"); return; }
  if (g_secs > 0) g_secs--;
  char buf[8];
  snprintf(buf, sizeof(buf), "%d:%02d", g_secs / 3600, (g_secs % 3600) / 60);
  lv_label_set_text(g_countdown, buf);
}

// ── Screen 1: Session ───────────────────────────────────────
static void build_session(lv_obj_t *t) {
  topbar(t, "MAX 20X", C_CORAL, C_BG);

  lv_obj_t *ring = mkring(t, 0, C_CORAL, 116);   // 0 until live data (was mock 68)
  lv_obj_align(ring, LV_ALIGN_LEFT_MID, 10, 8);
  g_sess_ring = ring;
  lv_obj_t *pct = mklabel(t, "--", &lv_font_montserrat_28, C_INK);
  g_sess_pct = pct;
  lv_obj_set_width(pct, 116);                                  // fixed box = ring width so the % stays
  lv_obj_set_style_text_align(pct, LV_TEXT_ALIGN_CENTER, 0);   // centered as the text grows ("--" -> "25%")
  lv_obj_align_to(pct, ring, LV_ALIGN_CENTER, 0, -6);
  lv_obj_t *used = mklabel(t, "USED", &lv_font_montserrat_12, C_DIM);
  lv_obj_align_to(used, ring, LV_ALIGN_CENTER, 0, 16);
  lv_obj_t *cap = mklabel(t, "5-HOUR WINDOW", &lv_font_montserrat_12, C_DIM);
  lv_obj_align(cap, LV_ALIGN_BOTTOM_LEFT, 18, -22);

  lv_obj_t *col = lv_obj_create(t);
  plain(col);
  lv_obj_set_size(col, 132, 184);
  lv_obj_align(col, LV_ALIGN_RIGHT_MID, -12, 2);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(col, 2, 0);

  mklabel(col, "RESETS IN", &lv_font_montserrat_12, C_DIM);
  g_countdown = mklabel(col, "--:--", &lv_font_montserrat_48, C_INK);
  g_sess_at = mklabel(col, "", &lv_font_montserrat_12, C_DIM);

  lv_obj_t *spacer = lv_obj_create(col); plain(spacer);
  lv_obj_set_size(spacer, 1, 8);

  lv_obj_t *wkrow = lv_obj_create(col); plain(wkrow);
  lv_obj_set_size(wkrow, 130, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(wkrow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(wkrow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  mklabel(wkrow, "WEEKLY", &lv_font_montserrat_12, C_DIM);
  g_sess_wk_pct = mklabel(wkrow, "--", &lv_font_montserrat_14, C_INK);   // captured → updated by ui_set_weekly

  lv_obj_t *bar = lv_bar_create(col);
  lv_obj_set_size(bar, 130, 8);
  lv_obj_set_style_radius(bar, 4, 0);
  lv_obj_set_style_bg_color(bar, C_TRACK, LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, C_AMBER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);
  lv_bar_set_range(bar, 0, 100);
  lv_bar_set_value(bar, 0, LV_ANIM_OFF);   // 0 until live data (was mock 41)
  g_sess_wk_bar = bar;
}

// ── Screen 2: Weekly ────────────────────────────────────────
static void build_weekly(lv_obj_t *t) {
  topbar(t, "WEEKLY", C_AMBER, C_BG);

  lv_obj_t *ring = mkring(t, 0, C_AMBER, 122);   // 0 until live data (was mock 41)
  lv_obj_align(ring, LV_ALIGN_LEFT_MID, 12, 4);
  g_wk_ring = ring;
  lv_obj_t *pct = mklabel(t, "--", &lv_font_montserrat_28, C_INK);
  g_wk_pct = pct;
  lv_obj_set_width(pct, 122);                                  // fixed box = ring width so the % stays
  lv_obj_set_style_text_align(pct, LV_TEXT_ALIGN_CENTER, 0);   // centered as the text grows ("--" -> "20%")
  lv_obj_align_to(pct, ring, LV_ALIGN_CENTER, 0, -6);
  lv_obj_t *of = mklabel(t, "OF WEEKLY", &lv_font_montserrat_12, C_DIM);
  lv_obj_align_to(of, ring, LV_ALIGN_CENTER, 0, 16);
  lv_obj_t *cap = mklabel(t, "RESETS MON 09:00", &lv_font_montserrat_12, C_DIM);
  lv_obj_align(cap, LV_ALIGN_BOTTOM_LEFT, 16, -22);

  lv_obj_t *col = lv_obj_create(t); plain(col);
  lv_obj_set_size(col, 132, 184);
  lv_obj_align(col, LV_ALIGN_RIGHT_MID, -12, 2);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(col, 7, 0);

  mklabel(col, "LAST 7 DAYS", &lv_font_montserrat_12, C_DIM);

  const int pcts[7] = {60, 85, 40, 95, 30, 15, 55};
  lv_obj_t *days = lv_obj_create(col); plain(days);
  lv_obj_set_size(days, 128, 56);
  lv_obj_set_flex_flow(days, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(days, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  for (int i = 0; i < 7; i++) {
    lv_obj_t *bar = lv_obj_create(days);
    lv_obj_set_size(bar, 13, 10 + pcts[i] * 44 / 100);
    lv_obj_set_style_radius(bar, 2, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, C_CORAL, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  }

  lv_obj_t *r1 = lv_obj_create(col); plain(r1);
  lv_obj_set_size(r1, 128, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(r1, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r1, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  mklabel(r1, "PEAK DAY", &lv_font_montserrat_12, C_DIM);
  mklabel(r1, "THU", &lv_font_montserrat_14, C_INK);
}

// ── Screen 3: Clock ─────────────────────────────────────────
static void build_clock(lv_obj_t *t) {
  topbar(t, "CLOCK", C_TRACK, C_DIM);
  lv_obj_t *time = mklabel(t, "--:--", &lv_font_montserrat_48, C_INK);
  lv_obj_align(time, LV_ALIGN_CENTER, 0, -14);
  g_clock_time = time;
  lv_obj_t *date = mklabel(t, "SYNCING", &lv_font_montserrat_16, C_DIM);
  g_clock_date = date;
  lv_obj_set_style_text_letter_space(date, 2, 0);
  lv_obj_align(date, LV_ALIGN_CENTER, 0, 24);
  lv_obj_t *nx = mklabel(t, "", &lv_font_montserrat_12, C_CORAL);   // live next-reset (blank until data)
  lv_obj_align(nx, LV_ALIGN_BOTTOM_MID, 0, -22);
  g_clock_next = nx;
}

// ── Screen 4: Device ────────────────────────────────────────
static lv_obj_t *devrow(lv_obj_t *p, const char *k, const char *v) {
  lv_obj_t *row = lv_obj_create(p); plain(row);
  lv_obj_set_size(row, 244, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  mklabel(row, k, &lv_font_montserrat_14, C_DIM);
  return mklabel(row, v, &lv_font_montserrat_14, C_INK);   // value label (updatable)
}

static void build_device(lv_obj_t *t) {
  topbar(t, "DEVICE", C_TRACK, C_DIM);
  lv_obj_t *col = lv_obj_create(t); plain(col);
  lv_obj_set_size(col, 248, 178);
  lv_obj_align(col, LV_ALIGN_BOTTOM_MID, 0, -16);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(col, 6, 0);

  g_dev_wifi  = devrow(col, "WI-FI",    "-");
  g_dev_ip    = devrow(col, "IP",       "-");
  g_dev_sig   = devrow(col, "SIGNAL",   "-");
  g_dev_proxy = devrow(col, "PROXY",    "-");
  g_dev_batt  = devrow(col, "BATTERY",  "-");
  g_dev_heap  = devrow(col, "FREE RAM", "-");
  g_dev_fw    = devrow(col, "FIRMWARE", "-");
}

// ── page dots ───────────────────────────────────────────────
static lv_obj_t *g_dots[4];
static lv_obj_t *g_tiles[4];
static lv_obj_t *g_tv;

static void set_dot(int idx) {
  for (int i = 0; i < 4; i++) {
    bool on = (i == idx);
    lv_obj_set_size(g_dots[i], on ? 14 : 6, 6);
    lv_obj_set_style_bg_color(g_dots[i], on ? C_CORAL : C_TRACK, 0);
  }
}

static void tv_changed(lv_event_t *e) {
  LV_UNUSED(e);
  lv_obj_t *act = lv_tileview_get_tile_active(g_tv);
  for (int i = 0; i < 4; i++) if (act == g_tiles[i]) { set_dot(i); break; }
}

// usage% → accent color (matches the F5 >=70% alert threshold).
static lv_color_t pct_color(int p) {
  return p >= 90 ? C_RED : (p >= 70 ? C_AMBER : C_CORAL);
}

// ── reset "drain" animation ─────────────────────────────────
// Usage within a window only ever climbs, so a DROP means the window just reset. We catch
// that and animate the ring + number down to the new value over 3s instead of snapping.
#define RESET_DROP_PCT 5       // a fall of >= this since the last shown value counts as a reset
static int  s_sess_shown = -1, s_wk_shown = -1;   // last value applied (-1 = blank/unknown)
static bool s_sess_anim  = false, s_wk_anim  = false;

static void sess_apply(int v) {
  if (g_sess_ring) {
    lv_arc_set_value(g_sess_ring, v);
    lv_obj_set_style_arc_color(g_sess_ring, pct_color(v), LV_PART_INDICATOR);
  }
  if (g_sess_pct) { char b[8]; snprintf(b, sizeof b, "%d%%", v); lv_label_set_text(g_sess_pct, b); }
  s_sess_shown = v;
}
static void wk_apply(int v) {
  if (g_wk_ring) {
    lv_arc_set_value(g_wk_ring, v);
    lv_obj_set_style_arc_color(g_wk_ring, pct_color(v), LV_PART_INDICATOR);
  }
  char b[8]; snprintf(b, sizeof b, "%d%%", v);
  if (g_wk_pct)      lv_label_set_text(g_wk_pct, b);
  if (g_sess_wk_pct) lv_label_set_text(g_sess_wk_pct, b);
  if (g_sess_wk_bar) lv_bar_set_value(g_sess_wk_bar, v, LV_ANIM_OFF);
  s_wk_shown = v;
}
static void sess_anim_exec(void *, int32_t v) { sess_apply((int)v); }
static void wk_anim_exec  (void *, int32_t v) { wk_apply((int)v); }
static void sess_anim_done(lv_anim_t *) { s_sess_anim = false; }
static void wk_anim_done  (lv_anim_t *) { s_wk_anim  = false; }

static void start_drain(lv_obj_t *var, lv_anim_exec_xcb_t exec, lv_anim_ready_cb_t done, int from, int to) {
  lv_anim_t a; lv_anim_init(&a);
  lv_anim_set_var(&a, var);
  lv_anim_set_values(&a, from, to);
  lv_anim_set_duration(&a, 3000);                 // 3s drain
  lv_anim_set_path_cb(&a, lv_anim_path_linear);   // even tick-down
  lv_anim_set_exec_cb(&a, exec);
  lv_anim_set_ready_cb(&a, done);
  lv_anim_start(&a);
}

void ui_set_session(int p, long secs_left, const char *reset_at) {
  if (!s_sess_anim) {
    if (s_sess_shown >= 0 && p <= s_sess_shown - RESET_DROP_PCT) {
      s_sess_anim = true;                          // window reset → drain old → new
      start_drain(g_sess_ring, sess_anim_exec, sess_anim_done, s_sess_shown, p);
    } else {
      sess_apply(p);
    }
  }
  if (secs_left >= 0) g_secs = secs_left;   // drives existing 1s countdown
  if (reset_at && reset_at[0] && g_sess_at) lv_label_set_text(g_sess_at, reset_at);
}

void ui_set_weekly(int p, long secs_left) {
  (void)secs_left;
  if (s_wk_anim) return;
  if (s_wk_shown >= 0 && p <= s_wk_shown - RESET_DROP_PCT) {
    s_wk_anim = true;                              // weekly window reset → drain
    start_drain(g_wk_ring, wk_anim_exec, wk_anim_done, s_wk_shown, p);
  } else {
    wk_apply(p);
  }
}

// Blank every live Claude figure to "--" / empty rings. Called by the presenter when we're
// offline or have no fresh data, so the device never shows stale or placeholder usage numbers.
void ui_clear_usage() {
  lv_anim_del(g_sess_ring, sess_anim_exec);   // kill any in-flight drain so it can't fight the blank
  lv_anim_del(g_wk_ring,   wk_anim_exec);
  s_sess_anim = s_wk_anim = false;
  if (g_sess_ring)   lv_arc_set_value(g_sess_ring, 0);
  if (g_sess_pct)    lv_label_set_text(g_sess_pct, "--");
  if (g_sess_wk_bar) lv_bar_set_value(g_sess_wk_bar, 0, LV_ANIM_OFF);
  if (g_sess_wk_pct) lv_label_set_text(g_sess_wk_pct, "--");
  if (g_sess_at)     lv_label_set_text(g_sess_at, "");
  if (g_wk_ring)     lv_arc_set_value(g_wk_ring, 0);
  if (g_wk_pct)      lv_label_set_text(g_wk_pct, "--");
  if (g_clock_next)  lv_label_set_text(g_clock_next, "");   // Clock screen "next reset" is session data too
  s_sess_shown = s_wk_shown = -1;
  g_secs = -1;   // countdown → "--:--"
}

void ui_set_online(bool online, bool stale) {
  lv_color_t c; const char *txt;
  if (!online)    { c = C_RED;   txt = "offline"; }
  else if (stale) { c = C_BLUE;  txt = "no data"; }
  else            { c = C_GREEN; txt = "connected"; }
  for (int i = 0; i < g_status_dot_n; i++) lv_obj_set_style_bg_color(g_status_dots[i], c, 0);
  if (g_dev_proxy) {
    lv_label_set_text(g_dev_proxy, txt);
    lv_obj_set_style_text_color(g_dev_proxy, c, 0);
  }
}

static void setlbl(lv_obj_t *l, const char *s) { if (l) lv_label_set_text(l, s); }
void ui_set_device(const char *wifi, const char *ip, const char *signal,
                   const char *batt, const char *heap, const char *fw) {
  setlbl(g_dev_wifi, wifi); setlbl(g_dev_ip, ip);   setlbl(g_dev_sig, signal);
  setlbl(g_dev_batt, batt); setlbl(g_dev_heap, heap); setlbl(g_dev_fw, fw);
}

void ui_set_clock(const char *time_str, const char *date_str) {
  setlbl(g_clock_time, time_str);
  setlbl(g_clock_date, date_str);
}

void ui_set_clock_reset(const char *next_reset) {   // e.g. "next reset 4:30 PM"; "" hides it
  setlbl(g_clock_next, next_reset);
}

lv_obj_t *ui_build_splash(const char *version) {
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, C_BG, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *ring = mkring(scr, 0, C_CORAL, 110);   // smaller so it clears the brand text
  lv_obj_align(ring, LV_ALIGN_CENTER, 0, -10);

  char buf[16];
  snprintf(buf, sizeof buf, "v%s", version);
  lv_obj_t *ver = mklabel(scr, buf, &lv_font_montserrat_24, C_INK);
  lv_obj_align_to(ver, ring, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t *brand = mklabel(scr, "CLAUDE MONITOR", &lv_font_montserrat_14, C_CORAL);
  lv_obj_set_style_text_letter_space(brand, 3, 0);
  lv_obj_align(brand, LV_ALIGN_CENTER, 0, 62);

  // Optimisation: all text is drawn ONCE up front (no fade anims). After a 1s settle, the ONLY thing
  // animating is the ring annulus — the brand text sits outside the ring's invalidation region, so it
  // costs nothing per frame. The ring gets the full CPU/SPI budget => smoothest sweep. 0->100 over 3s.
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, ring);
  lv_anim_set_values(&a, 0, 100);
  lv_anim_set_duration(&a, 3000);
  lv_anim_set_delay(&a, 1000);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&a, [](void *o, int32_t v) { lv_arc_set_value((lv_obj_t *)o, v); });
  lv_anim_start(&a);

  return scr;
}

void ui_build(lv_obj_t *parent) {
  g_status_dot_n = 0;
  lv_obj_set_style_bg_color(parent, C_BG, 0);

  g_tv = lv_tileview_create(parent);
  lv_obj_set_style_bg_color(g_tv, C_BG, 0);
  lv_obj_set_style_border_width(g_tv, 0, 0);
  lv_obj_set_scrollbar_mode(g_tv, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_size(g_tv, UI_W, UI_H);

  g_tiles[0] = lv_tileview_add_tile(g_tv, 0, 0, LV_DIR_RIGHT);
  g_tiles[1] = lv_tileview_add_tile(g_tv, 1, 0, (lv_dir_t)(LV_DIR_LEFT | LV_DIR_RIGHT));
  g_tiles[2] = lv_tileview_add_tile(g_tv, 2, 0, (lv_dir_t)(LV_DIR_LEFT | LV_DIR_RIGHT));
  g_tiles[3] = lv_tileview_add_tile(g_tv, 3, 0, LV_DIR_LEFT);
  for (int i = 0; i < 4; i++) {
    plain(g_tiles[i]);
    // Opaque tile background: avoids per-pixel blending while tiles slide during a swipe.
    lv_obj_set_style_bg_color(g_tiles[i], C_BG, 0);
    lv_obj_set_style_bg_opa(g_tiles[i], LV_OPA_COVER, 0);
  }

  build_session(g_tiles[0]);
  build_weekly(g_tiles[1]);
  build_clock(g_tiles[2]);
  build_device(g_tiles[3]);

  lv_obj_t *dotbar = lv_obj_create(parent); plain(dotbar);
  lv_obj_set_size(dotbar, 80, 10);
  lv_obj_align(dotbar, LV_ALIGN_BOTTOM_MID, 0, -7);
  lv_obj_set_flex_flow(dotbar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(dotbar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(dotbar, 5, 0);
  for (int i = 0; i < 4; i++) {
    g_dots[i] = lv_obj_create(dotbar);
    lv_obj_set_size(g_dots[i], 6, 6);
    lv_obj_set_style_radius(g_dots[i], 3, 0);
    lv_obj_set_style_border_width(g_dots[i], 0, 0);
    lv_obj_remove_flag(g_dots[i], LV_OBJ_FLAG_SCROLLABLE);
  }
  set_dot(0);

  lv_obj_add_event_cb(g_tv, tv_changed, LV_EVENT_VALUE_CHANGED, NULL);
  lv_timer_create(tick_1s, 1000, NULL);
}

void ui_goto(int idx) {
  if (idx < 0 || idx > 3 || !g_tv) return;
  lv_obj_set_tile_id(g_tv, idx, 0, LV_ANIM_OFF);
  set_dot(idx);
}

void ui_goto_anim(int idx) {   // animated slide (used for the one-time Clock->Session reveal once connected)
  if (idx < 0 || idx > 3 || !g_tv) return;
  lv_obj_set_tile_id(g_tv, idx, 0, LV_ANIM_ON);
  set_dot(idx);
}
