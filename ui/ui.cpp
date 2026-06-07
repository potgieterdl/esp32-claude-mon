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
#define C_BLUE  lv_color_hex(0x3DA5FF)  // "online, awaiting usage-API data" — distinct from red
// Claude-bot (#6 sleeper + #31 easter egg) palette — a warm coral/cream take on the chunky robot.
#define C_CORAL_DK lv_color_hex(0xBA5230)  // darker coral: outlines, neck/hips, depth shadow
#define C_CORAL_HI lv_color_hex(0xF58457)  // lighter coral: helmet dome crown
#define C_EYE      lv_color_hex(0x171411)  // warm near-black: visor + chest grille
#define C_SHINE    lv_color_hex(0xFFFDF8)  // bright warm-white: glass-dome shine + chest light

// live-data handles (F4) — captured in build_*; updated by ui_set_*.
static lv_obj_t *g_sess_ring = nullptr, *g_sess_pct = nullptr, *g_sess_wk_bar = nullptr, *g_sess_at = nullptr;
static lv_obj_t *g_sess_badge = nullptr;   // Session-screen plan pill (MAX 5X / MAX 20X / PRO), wired to data_plan()
static lv_obj_t *g_sess_wk_pct = nullptr;   // Session-screen weekly-% label (was a discarded handle → froze at mock 41%)
static lv_obj_t *g_wk_ring = nullptr, *g_wk_pct = nullptr, *g_wk_reset = nullptr;
static lv_obj_t *g_clock_time = nullptr, *g_clock_date = nullptr, *g_clock_next = nullptr;
static lv_obj_t *g_status_dots[4];
static int       g_status_dot_n = 0;
// device-screen value labels (F7)
static lv_obj_t *g_dev_wifi = nullptr, *g_dev_ip = nullptr, *g_dev_sig = nullptr,
                *g_dev_batt = nullptr, *g_dev_heap = nullptr, *g_dev_fw = nullptr,
                *g_dev_api = nullptr, *g_dev_tok = nullptr, *g_dev_renew = nullptr;

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

// Returns the badge label so a caller (Session) can keep its handle and update it live.
static lv_obj_t *topbar(lv_obj_t *t, const char *badge, lv_color_t bg, lv_color_t fg) {
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
  return b;
}

// ── live countdown (sample) ─────────────────────────────────
static lv_obj_t *g_countdown = nullptr;
static int32_t g_secs = -1;  // <0 = no data yet → show "--:--" (set by ui_set_session once live)

static void tick_1s(lv_timer_t *) {
  if (!g_countdown) return;
  if (g_secs < 0) { lv_label_set_text(g_countdown, "--:--"); return; }
  if (g_secs > 0) g_secs--;
  // Ceil to whole minutes so the label reads "0:00" only at the actual reset — flooring would
  // show "0:00" for the entire final minute (1–59 s still remaining).
  int mins = (g_secs + 59) / 60;
  char buf[8];
  snprintf(buf, sizeof(buf), "%d:%02d", mins / 60, mins % 60);
  lv_label_set_text(g_countdown, buf);
}

// ── Screen 1: Session ───────────────────────────────────────
static void build_session(lv_obj_t *t) {
  g_sess_badge = topbar(t, "CLAUDE", C_CORAL, C_BG);   // placeholder until data_plan() arrives

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
  g_wk_reset = mklabel(t, "RESETS --", &lv_font_montserrat_12, C_DIM);   // real day/time fed from the usage epoch
  lv_obj_align(g_wk_reset, LV_ALIGN_BOTTOM_LEFT, 16, -22);

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

// ── Sleeping companion (#6) ─────────────────────────────────
// The full Claude bot — the SAME character as the #31 easter egg, drawn small by bot_draw — dozing in
// the Clock screen's bottom-left corner with three "Z"s that drift up-right and fade, looped. The
// whole group lives under g_sleep_grp (hidden until idle). Only the Zzz move; the bot itself is static.
static lv_obj_t *g_sleep_grp = nullptr;             // bottom-left container on the Clock tile
static lv_obj_t *g_zzz[3]    = {nullptr, nullptr, nullptr};
static int32_t   g_zzz_phase = 0;                   // lv_anim identity token (value unused)
static bool      g_sleeping  = false;
static const int Z_BX[3] = {58, 70, 80};            // base x (group-rel), small → large; above his head
static const int Z_BY[3] = {44, 30, 16};            // base y (group-rel), lower → higher
static void bot_draw(lv_obj_t *p, int sc, bool asleep,    // full-body Claude bot (defined with #31 below)
                     lv_obj_t **eye, lv_obj_t **chest);

// One animation drives all three Z's by a shared 0..1000 phase; each is staggered a third of a
// cycle so there's always a Z rising. Per Z: rise + slight right drift, opacity fades in then out.
static void zzz_exec(void *, int32_t v) {
  for (int i = 0; i < 3; i++) {
    if (!g_zzz[i]) continue;
    int li  = (int)(((uint32_t)v + (uint32_t)i * 333u) % 1000u);  // 0..999, staggered
    int tri = li < 500 ? li : (1000 - li);                        // 0→500→0 triangle
    lv_obj_set_style_text_opa(g_zzz[i], (lv_opa_t)(tri * 255 / 500), 0);
    lv_obj_set_pos(g_zzz[i], Z_BX[i] + li * 6 / 1000, Z_BY[i] - li * 16 / 1000);  // drift up-right
  }
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

  // Sleeping companion — bottom-left so it clears the centred time/date; hidden until ui_set_sleeping.
  g_sleep_grp = lv_obj_create(t); plain(g_sleep_grp);
  lv_obj_set_size(g_sleep_grp, 104, 160);
  lv_obj_align(g_sleep_grp, LV_ALIGN_BOTTOM_LEFT, 6, -2);
  lv_obj_add_flag(g_sleep_grp, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *sb = lv_obj_create(g_sleep_grp); plain(sb);   // the bot, anchored to the bottom of the group
  lv_obj_set_size(sb, 96, 132);
  lv_obj_align(sb, LV_ALIGN_BOTTOM_MID, 0, 0);
  bot_draw(sb, 58, true, nullptr, nullptr);               // same character, drawn small + asleep

  const lv_font_t *zf[3] = {&lv_font_montserrat_12, &lv_font_montserrat_14, &lv_font_montserrat_16};
  for (int i = 0; i < 3; i++) {                       // the drifting Zzz (positioned by zzz_exec)
    g_zzz[i] = mklabel(g_sleep_grp, "Z", zf[i], C_CORAL);
    lv_obj_set_pos(g_zzz[i], Z_BX[i], Z_BY[i]);
  }
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
  lv_obj_set_size(col, 248, 192);
  lv_obj_align(col, LV_ALIGN_BOTTOM_MID, 0, -14);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(col, 4, 0);

  g_dev_wifi  = devrow(col, "WI-FI",    "-");
  g_dev_ip    = devrow(col, "IP",       "-");
  g_dev_sig   = devrow(col, "SIGNAL",   "-");
  g_dev_api   = devrow(col, "API",      "-");   // usage-API connection status (was "PROXY")
  g_dev_tok   = devrow(col, "TOKEN",    "-");   // when the OAuth token was last synced/refreshed
  g_dev_renew = devrow(col, "RENEWS",   "-");   // when the token next auto-refreshes
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
static bool s_sess_idle  = false;  // no active 5h window → the ring % reads "--" (not "0%"), per #6

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
static void sess_anim_done(lv_anim_t *) {
  s_sess_anim = false;
  if (s_sess_idle && g_sess_pct) lv_label_set_text(g_sess_pct, "--");  // drained into idle → blank the %
}
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

// Update the Session ring/% (honouring an in-flight reset-drain). Shared by the active and
// idle setters so both render the ring identically.
static void sess_update_ring(int p) {
  if (s_sess_anim) return;
  if (s_sess_shown >= 0 && p <= s_sess_shown - RESET_DROP_PCT) {
    s_sess_anim = true;                          // window reset → drain old → new
    start_drain(g_sess_ring, sess_anim_exec, sess_anim_done, s_sess_shown, p);
  } else {
    sess_apply(p);
  }
}

void ui_set_session(int p, long secs_left, const char *reset_at) {
  s_sess_idle = false;          // a live window → show the real % again
  sess_update_ring(p);
  if (g_countdown) lv_obj_set_style_text_opa(g_countdown, LV_OPA_COVER, 0);  // active = full opacity
  if (secs_left >= 0) g_secs = secs_left;   // drives existing 1s countdown
  // Always (re)set the sub-label so a prior "No current session" can't linger into an active
  // render; empty when we have no reset time (e.g. clock not yet synced).
  if (g_sess_at) lv_label_set_text(g_sess_at, (reset_at && reset_at[0]) ? reset_at : "");
}

// No active 5-hour window (it elapsed while idle): the usage API has no resets_at to count toward,
// so show a faded "--:--" + "No current session" instead of a frozen 0:00. The live countdown
// returns automatically once a new window starts (presenter switches back to ui_set_session).
void ui_set_session_idle(int p) {
  s_sess_idle = true;
  sess_update_ring(p);   // still drains the ring down if a window just reset; arc settles empty at 0
  // No active window → the % is meaningless, so blank it to "--" (matches "nothing on"). If a drain
  // is in flight the blank is applied by sess_anim_done when it finishes.
  if (!s_sess_anim && g_sess_pct) lv_label_set_text(g_sess_pct, "--");
  g_secs = -1;                                                          // countdown → "--:--"
  if (g_countdown) lv_obj_set_style_text_opa(g_countdown, LV_OPA_50, 0);  // faded (de-emphasised)
  if (g_sess_at)   lv_label_set_text(g_sess_at, "No current session");
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

// Weekly reset caption, derived from the real usage epoch by the presenter (e.g. "MON 09:00").
// Empty/blank -> "RESETS --" (honest display: no guessed day when there's no data).
void ui_set_weekly_reset(const char *when) {
  if (!g_wk_reset) return;
  if (when && when[0]) {
    char b[24];
    snprintf(b, sizeof b, "RESETS %s", when);
    lv_label_set_text(g_wk_reset, b);
  } else {
    lv_label_set_text(g_wk_reset, "RESETS --");
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
  ui_set_weekly_reset(nullptr);   // -> "RESETS --" (reuse the setter; single source for the blank)
  if (g_clock_next)  lv_label_set_text(g_clock_next, "");   // Clock screen "next reset" is session data too
  if (g_countdown)   lv_obj_set_style_text_opa(g_countdown, LV_OPA_COVER, 0);  // un-fade (may have been idle)
  s_sess_shown = s_wk_shown = -1;
  s_sess_idle = false;   // offline blank, not the idle "no session" state
  g_secs = -1;   // countdown → "--:--"
}

void ui_set_online(bool online, bool stale) {
  lv_color_t c; const char *txt;
  if (!online)    { c = C_RED;   txt = "offline"; }
  else if (stale) { c = C_BLUE;  txt = "no data"; }
  else            { c = C_GREEN; txt = "connected"; }
  for (int i = 0; i < g_status_dot_n; i++) lv_obj_set_style_bg_color(g_status_dots[i], c, 0);
  if (g_dev_api) {
    lv_label_set_text(g_dev_api, txt);
    lv_obj_set_style_text_color(g_dev_api, c, 0);
  }
}

void ui_set_token_info(const char *last_sync, const char *renews) {
  if (g_dev_tok)   lv_label_set_text(g_dev_tok, last_sync);
  if (g_dev_renew) lv_label_set_text(g_dev_renew, renews);
}

void ui_set_plan(const char *label) {   // Session plan pill (e.g. "MAX 5X")
  if (g_sess_badge && label && label[0]) lv_label_set_text(g_sess_badge, label);
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

void ui_set_sleeping(bool sleeping) {
  if (sleeping == g_sleeping || !g_sleep_grp) return;   // idempotent
  g_sleeping = sleeping;
  if (sleeping) {
    lv_obj_remove_flag(g_sleep_grp, LV_OBJ_FLAG_HIDDEN);
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, &g_zzz_phase);                   // identity token only (value unused)
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, 2600);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_set_exec_cb(&a, zzz_exec);
    lv_anim_start(&a);
  } else {
    lv_anim_del(&g_zzz_phase, zzz_exec);                 // stop the loop → no render cost when awake
    lv_obj_add_flag(g_sleep_grp, LV_OBJ_FLAG_HIDDEN);
  }
}

// ── Claude bot — shared full-body character (#6 sleeper + #31 easter egg) ────
// A chunky, friendly robot: rounded coral head with a glass-dome crown + dark visor and two bright
// cream eyes, a stubby torso with a lit chest grille, amber joints/feet, little arms and legs — all
// from rounded-rect/circle primitives (portable; no image decoder, no PSRAM). bot_draw() lays the
// whole guy into a parent, scaled by `sc` percent, so the SAME character is the big centred
// easter-egg bot AND the small corner sleeper. `asleep` swaps the open eyes for closed dashes and
// dims the chest light.
//
// Easter-egg bot (#31) draws on an OPAQUE full-screen stage parented to the active screen (NOT
// lv_layer_top), so the covered tileview isn't re-rendered. Every animation is TRANSFORM-FREE — body
// moves by translate, eyes animate by HEIGHT (re-centred), parts fade by opacity. (LVGL's software
// transform-SCALE path infinite-loops here in partial-render mode, so no scale anywhere; see ADR-0007.)
// Built on show / destroyed on hide → zero LVGL-pool footprint while away.
static lv_obj_t *g_bot = nullptr;          // opaque full-screen stage (also catches swipe/tap to dismiss)
static lv_obj_t *g_bot_body  = nullptr;    // centred group — the whole bot, moved as one via translate
static lv_obj_t *g_bot_eye[2] = {nullptr, nullptr};
static lv_obj_t *g_bot_chest = nullptr;    // chest light (soft "alive" pulse; flashes the beep on arrival)
static int32_t   g_bot_eye_phase = 0;      // identity token: one anim drives BOTH eyes (open/blink)
static bool      g_bot_shown = false;      // true only in the interactive phase (false during exit teardown)
static void (*g_bot_dismiss_cb)(void) = nullptr;

#define BOT_W      200          // body container (transparent; size is free — translate makes no layer)
#define BOT_H      220
#define BOT_RISE   104          // entrance: body starts this many px low, then springs up to centre
#define EYE_OPEN   256          // eye "openness" phase: 256 = fully open
#define EYE_SHUT   28           // openness when blinking → a thin dark dash
#define EYE_DX     17           // eye centre offset from the bot midline (±) — close-set = alert
#define EYE_DY     (-46)        // eye centre offset (negative = up, on the visor)
#define EYE_H_MAX  32           // eye height (px) fully open (tall pills read focused)
#define EYE_H_MIN  5            // eye height (px) shut

// one styled, non-interactive shape (rounded rect / circle) aligned to its parent's center
static lv_obj_t *botshape(lv_obj_t *p, int dx, int dy, int w, int h, lv_color_t col, int r) {
  lv_obj_t *o = lv_obj_create(p);
  lv_obj_set_size(o, w, h);
  lv_obj_align(o, LV_ALIGN_CENTER, dx, dy);
  lv_obj_set_style_bg_color(o, col, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(o, r, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);   // only the stage is interactive
  return o;
}

// Draw the whole bot into `p`, scaled by `sc` percent; capture the animated parts (pass NULL to skip).
// Shapes are added back-to-front (feet → … → eyes). asleep = closed-dash eyes + a dimmed chest.
static void bot_draw(lv_obj_t *p, int sc, bool asleep, lv_obj_t **eye, lv_obj_t **chest) {
  auto S = [sc](int v) -> int { int r = v * sc / 100; return (r == 0 && v != 0) ? (v < 0 ? -1 : 1) : r; };
  for (int i = 0; i < 2; i++) {                                          // legs + amber feet
    botshape(p, S(i ? 18 : -18), S(95), S(26), S(12), C_AMBER, S(6));
    botshape(p, S(i ? 16 : -16), S(84), S(18), S(20), C_CORAL, S(7));
  }
  botshape(p, 0, S(74), S(46), S(12), C_CORAL_DK, S(4));                 // hips
  for (int i = 0; i < 2; i++) {                                          // arms + coral mitts, tucked in/down
    botshape(p, S(i ? 50 : -50), S(64), S(18), S(16), C_CORAL, S(8));    //   hand (coral, like the reference)
    botshape(p, S(i ? 48 : -48), S(40), S(16), S(40), C_CORAL, S(8));    //   arm
  }
  botshape(p, 0, S(38), S(80), S(66), C_CORAL, S(18));                   // torso
  for (int i = 0; i < 2; i++)
    botshape(p, S(i ? 42 : -42), S(26), S(16), S(16), C_AMBER, S(8));    // amber shoulder joints (on the torso)
  botshape(p, 0, S(44), S(48), S(18), C_EYE, S(6));                      // chest grille
  lv_obj_t *cl = botshape(p, 0, S(44), S(26), S(6), C_SHINE, S(2));      // chest light (also reads as a mouth)
  if (asleep) lv_obj_set_style_bg_opa(cl, LV_OPA_30, 0);
  if (chest) *chest = cl;
  botshape(p, S(16), S(30), S(7), S(7), C_AMBER, S(4));                  // little amber status LED (it's a monitor!)
  botshape(p, 0, S(3), S(34), S(12), C_CORAL_DK, S(4));                  // neck
  botshape(p, 0, S(-50), S(116), S(100), C_CORAL, S(32));               // head
  for (int i = 0; i < 2; i++) {                                          // ear-knobs — the reference's signature nubs
    botshape(p, S(i ? 60 : -60), S(-50), S(14), S(26), C_CORAL, S(6));
    botshape(p, S(i ? 60 : -60), S(-50), S(7),  S(7),  C_AMBER, S(4));   //   amber bolt
  }
  botshape(p, 0, S(-74), S(92), S(44), C_CORAL_HI, S(22));             // glass-dome crown
  lv_obj_t *shine = botshape(p, S(-26), S(-78), S(22), S(12), C_SHINE, S(6));  // dome reflection (glass glare)
  lv_obj_set_style_bg_opa(shine, LV_OPA_60, 0);
  botshape(p, 0, S(-48), S(78), S(46), C_EYE, S(18));                   // visor (inset → thicker coral frame, cuter)
  for (int i = 0; i < 2; i++) {                                         // eyes (open) / drooped dashes (asleep)
    lv_obj_t *e = asleep
      ? botshape(p, S(i ? EYE_DX : -EYE_DX), S(EYE_DY + 2), S(20), S(3),          C_INK, S(2))
      : botshape(p, S(i ? EYE_DX : -EYE_DX), S(EYE_DY),     S(20), S(EYE_H_MAX), C_INK, S(8));
    if (eye) eye[i] = e;
  }
  // (no catchlights — the bright plain cream eyes read best, and match the reference)
}

// exec callbacks — one stable fn per animated property so teardown can target each precisely
static void bot_bodyy_exec (void *o, int32_t v) { lv_obj_set_style_translate_y((lv_obj_t *)o, v, 0); }
static void bot_bodyx_exec (void *o, int32_t v) { lv_obj_set_style_translate_x((lv_obj_t *)o, v, 0); }
static void bot_scrim_exec (void *o, int32_t v) { lv_obj_set_style_bg_opa((lv_obj_t *)o, (lv_opa_t)v, 0); }
static void bot_chest_exec (void *o, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)o, (lv_opa_t)v, 0); }
static void bot_eyes_exec  (void *, int32_t v) {            // height-blink BOTH eyes (glints clip with them)
  if (v < 0) v = 0; if (v > 256) v = 256;
  int h = EYE_H_MIN + (EYE_H_MAX - EYE_H_MIN) * v / 256;
  for (int i = 0; i < 2; i++) if (g_bot_eye[i]) {
    lv_obj_set_height(g_bot_eye[i], h);
    lv_obj_align(g_bot_eye[i], LV_ALIGN_CENTER, i ? EYE_DX : -EYE_DX, EYE_DY);
  }
}

// small one-shot anim helper (looping anims are spelled out so their extra knobs read clearly)
static void bot_anim(void *var, lv_anim_exec_xcb_t exec, int32_t from, int32_t to,
                     uint32_t dur, lv_anim_path_cb_t path, uint32_t delay, lv_anim_completed_cb_t done) {
  lv_anim_t a; lv_anim_init(&a);
  lv_anim_set_var(&a, var);
  lv_anim_set_exec_cb(&a, exec);
  lv_anim_set_values(&a, from, to);
  lv_anim_set_duration(&a, dur);
  lv_anim_set_path_cb(&a, path);
  if (delay) lv_anim_set_delay(&a, delay);
  if (done)  lv_anim_set_completed_cb(&a, done);
  lv_anim_start(&a);
}

static void bot_bob_start() {   // gentle vertical "breathing" — translate only (no layer), forever
  lv_anim_t a; lv_anim_init(&a);
  lv_anim_set_var(&a, g_bot_body);
  lv_anim_set_exec_cb(&a, bot_bodyy_exec);
  lv_anim_set_values(&a, -5, 5);
  lv_anim_set_duration(&a, 1600);
  lv_anim_set_reverse_duration(&a, 1600);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_start(&a);
}
static void bot_blink_start() {  // slow blink every ~2.8 s: a quick height squash→open on both eyes
  lv_anim_t a; lv_anim_init(&a);
  lv_anim_set_var(&a, &g_bot_eye_phase);
  lv_anim_set_exec_cb(&a, bot_eyes_exec);
  lv_anim_set_values(&a, EYE_OPEN, EYE_SHUT);
  lv_anim_set_duration(&a, 90);
  lv_anim_set_reverse_duration(&a, 110);
  lv_anim_set_delay(&a, 1600);
  lv_anim_set_repeat_delay(&a, 2800);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_start(&a);
}
static void bot_chest_glow(lv_anim_t *) {   // after the beep, settle into a soft "alive" chest pulse
  if (!g_bot_shown || !g_bot_chest) return;
  lv_anim_t a; lv_anim_init(&a);
  lv_anim_set_var(&a, g_bot_chest);
  lv_anim_set_exec_cb(&a, bot_chest_exec);
  lv_anim_set_values(&a, 150, 235);
  lv_anim_set_duration(&a, 1200);
  lv_anim_set_reverse_duration(&a, 1200);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_start(&a);
}
static void bot_rise_done(lv_anim_t *) { if (g_bot_shown) bot_bob_start(); }   // rise → idle bob
static void bot_wake_done(lv_anim_t *) { if (g_bot_shown) bot_blink_start(); } // eyes open → blink loop

static void bot_gesture_cb(lv_event_t *e);   // fwd

static void bot_build() {   // built fresh each summon → the bot holds ZERO LVGL-pool memory while hidden
  // An OPAQUE dark "stage": full-screen + fully covers the tileview, so LVGL's cover-check SKIPS drawing
  // it (and its big glyphs) entirely. It must sit in the SAME layer as the tileview (a child of the active
  // screen, not lv_layer_top) for that skip to apply — otherwise the screen renders independently and its
  // glyph buffers overflow the 64 KB LVGL pool, which this config turns into a hang. Opaque also looks clean.
  g_bot = lv_obj_create(lv_screen_active());
  lv_obj_set_size(g_bot, UI_W, UI_H);
  lv_obj_center(g_bot);
  lv_obj_set_style_bg_color(g_bot, C_BG, 0);
  lv_obj_set_style_bg_opa(g_bot, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(g_bot, 0, 0);
  lv_obj_set_style_pad_all(g_bot, 0, 0);
  lv_obj_remove_flag(g_bot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(g_bot, LV_OBJ_FLAG_CLICKABLE);      // catch swipe (gesture) + tap (click) to dismiss
  lv_obj_add_event_cb(g_bot, bot_gesture_cb, LV_EVENT_GESTURE, nullptr);
  lv_obj_add_event_cb(g_bot, bot_gesture_cb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *b = lv_obj_create(g_bot);          // the bot group — moves as one via translate
  g_bot_body = b;
  lv_obj_set_size(b, BOT_W, BOT_H);
  lv_obj_center(b);
  lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(b, 0, 0);
  lv_obj_set_style_pad_all(b, 0, 0);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);

  bot_draw(b, 100, false, g_bot_eye, &g_bot_chest);   // the whole guy, full size
}

bool ui_bot_show() {
  // Reject a re-summon while shown AND while a previous dismissal is still tearing down: g_bot stays
  // non-null for the whole show+exit lifetime (nulled only in bot_destroy). Rebuilding mid-exit would
  // let the old exit chain delete the NEW objects and orphan the old opaque stage (a dead black screen).
  if (g_bot_shown || g_bot) return false;
  g_bot_shown = true;
  bot_build();                        // fresh build; destroyed on hide → no idle footprint
  lv_obj_set_style_translate_y(g_bot_body, BOT_RISE, 0);   // start low + eyes shut, then spring up + wake
  bot_eyes_exec(nullptr, EYE_SHUT);
  lv_obj_move_foreground(g_bot);                          // above any modal/toast

  bot_anim(g_bot_body, bot_bodyy_exec, BOT_RISE, 0, 580, lv_anim_path_overshoot, 0, bot_rise_done);  // spring up
  bot_anim(&g_bot_eye_phase, bot_eyes_exec, EYE_SHUT, EYE_OPEN, 420, lv_anim_path_overshoot, 220, bot_wake_done);  // wake
  // "beep-bop-bop-beep": the chest light flashes 4× on arrival, then settles into the soft pulse
  lv_anim_t c; lv_anim_init(&c);
  lv_anim_set_var(&c, g_bot_chest);
  lv_anim_set_exec_cb(&c, bot_chest_exec);
  lv_anim_set_values(&c, 255, 70);
  lv_anim_set_duration(&c, 130);
  lv_anim_set_reverse_duration(&c, 130);
  lv_anim_set_delay(&c, 360);
  lv_anim_set_repeat_count(&c, 4);
  lv_anim_set_path_cb(&c, lv_anim_path_ease_in_out);
  lv_anim_set_completed_cb(&c, bot_chest_glow);
  lv_anim_start(&c);
  return true;
}

// stop every looping idle anim (leaves the bot standing still, ready for an exit move)
static void bot_stop_idle() {
  lv_anim_delete(g_bot_body, bot_bodyy_exec);
  lv_anim_delete(&g_bot_eye_phase, bot_eyes_exec);
  lv_anim_delete(g_bot_chest, bot_chest_exec);
}
// exit, step 2: stage fade done → delete EVERYTHING (frees the scrim + all bot objects → 0 pool while away)
static void bot_destroy(lv_anim_t *) {
  lv_anim_delete(&g_bot_eye_phase, NULL);   // token anim isn't tied to an object → cancel explicitly
  if (g_bot) lv_obj_delete(g_bot);
  g_bot = g_bot_body = g_bot_chest = nullptr;
  g_bot_eye[0] = g_bot_eye[1] = nullptr;
}
// exit, step 1 done (bot slid off the dark stage) → hide it, then fade the stage out so the screen returns.
// The bot is hidden before the fade so only the (light) screen draws while the stage is semi-transparent.
static void bot_exit_fade(lv_anim_t *) {
  if (g_bot_body) lv_obj_add_flag(g_bot_body, LV_OBJ_FLAG_HIDDEN);
  bot_anim(g_bot, bot_scrim_exec, LV_OPA_COVER, 0, 260, lv_anim_path_ease_out, 0, bot_destroy);
}

// converge all dismiss paths here. dir = swipe direction (LV_DIR_NONE → drop-down default exit).
static void bot_dismiss(lv_dir_t dir) {
  if (!g_bot_shown) return;
  g_bot_shown = false;
  bot_stop_idle();
  if (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT) {       // swiped off sideways
    bot_anim(g_bot_body, bot_bodyx_exec, 0, dir == LV_DIR_LEFT ? -UI_W : UI_W, 300, lv_anim_path_ease_in, 0, bot_exit_fade);
  } else if (dir == LV_DIR_TOP) {                         // swiped up
    bot_anim(g_bot_body, bot_bodyy_exec, 0, -UI_H, 300, lv_anim_path_ease_in, 0, bot_exit_fade);
  } else {   // tap / 15 s timer / second shake → close the eyes and drop off the bottom
    bot_anim(&g_bot_eye_phase, bot_eyes_exec, EYE_OPEN, EYE_SHUT, 180, lv_anim_path_ease_in, 0, nullptr);
    bot_anim(g_bot_body, bot_bodyy_exec, 0, UI_H, 340, lv_anim_path_ease_in, 0, bot_exit_fade);
  }
}

static void bot_gesture_cb(lv_event_t *e) {
  if (!g_bot_shown) return;                         // already exiting (guards GESTURE+CLICKED double-fire)
  lv_dir_t dir = LV_DIR_NONE;                        // a tap → default exit
  if (lv_event_get_code(e) == LV_EVENT_GESTURE)
    dir = lv_indev_get_gesture_dir(lv_indev_active());
  bot_dismiss(dir);
  if (g_bot_dismiss_cb) g_bot_dismiss_cb();          // let firmware cancel its auto-hide timer
}

void ui_bot_hide() { bot_dismiss(LV_DIR_NONE); }     // firmware's auto-hide / second-shake path
void ui_bot_set_dismiss_cb(void (*cb)(void)) { g_bot_dismiss_cb = cb; }
bool ui_bot_visible() { return g_bot_shown; }

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

// ── Reusable notifications: one modal slot + one toast (top layer, above all screens) ──
// MODAL = dim scrim + centered card (the clock shows behind). A modal with >=1 button is an
// ACKNOWLEDGE modal: it stays until the user taps (cb fires) and ui_modal_clear can't remove it.
// A button-less modal is PASSIVE: shown while a condition holds, removed by ui_modal_clear(id).
// Single slot: higher prio wins; an unacknowledged ack-modal isn't downgraded by a lower prio.
static lv_obj_t *g_modal = nullptr, *g_modal_title = nullptr, *g_modal_body = nullptr,
                *g_modal_btnrow = nullptr, *g_modal_b0 = nullptr, *g_modal_b1 = nullptr,
                *g_modal_b0l = nullptr, *g_modal_b1l = nullptr;
static int          g_modal_id = 0, g_modal_prio = 0;
static bool         g_modal_ack = false;   // current modal is awaiting a button tap
static ui_action_cb g_modal_cb = nullptr;

static lv_color_t sev_color(ui_severity_t s) {
  switch (s) { case UI_SEV_OK: return C_GREEN; case UI_SEV_WARN: return C_AMBER;
               case UI_SEV_ERROR: return C_RED; case UI_SEV_BRAND: return C_CORAL;
               default: return C_BLUE; }
}

static void modal_btn_cb(lv_event_t *e) {
  int btn = (int)(intptr_t)lv_event_get_user_data(e);
  ui_action_cb cb = g_modal_cb; int id = g_modal_id;
  g_modal_id = 0; g_modal_prio = 0; g_modal_ack = false; g_modal_cb = nullptr;
  lv_obj_add_flag(g_modal, LV_OBJ_FLAG_HIDDEN);
  if (cb) cb(id, btn);
}

static void modal_build() {
  lv_obj_t *scrim = lv_obj_create(lv_layer_top());   // dim backdrop; screen behind stays visible
  lv_obj_set_size(scrim, UI_W, UI_H);
  lv_obj_center(scrim);
  lv_obj_set_style_bg_color(scrim, C_BG, 0);
  lv_obj_set_style_bg_opa(scrim, LV_OPA_70, 0);
  lv_obj_set_style_border_width(scrim, 0, 0);
  lv_obj_set_style_pad_all(scrim, 0, 0);
  lv_obj_remove_flag(scrim, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *card = lv_obj_create(scrim);
  lv_obj_set_size(card, UI_W - 40, LV_SIZE_CONTENT);
  lv_obj_center(card);
  lv_obj_set_style_bg_color(card, C_TRACK, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(card, 14, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_pad_hor(card, 16, 0);
  lv_obj_set_style_pad_ver(card, 28, 0);   // extra top/bottom breathing room (taller card)
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(card, 12, 0);

  g_modal_title = mklabel(card, "", &lv_font_montserrat_24, C_INK);
  lv_label_set_long_mode(g_modal_title, LV_LABEL_LONG_WRAP);   // never clip a long title
  lv_obj_set_width(g_modal_title, UI_W - 40 - 32);
  lv_obj_set_style_text_align(g_modal_title, LV_TEXT_ALIGN_CENTER, 0);
  g_modal_body = mklabel(card, "", &lv_font_montserrat_14, C_INK);
  lv_label_set_long_mode(g_modal_body, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(g_modal_body, UI_W - 40 - 32);
  lv_obj_set_style_text_align(g_modal_body, LV_TEXT_ALIGN_CENTER, 0);

  g_modal_btnrow = lv_obj_create(card); plain(g_modal_btnrow);
  lv_obj_set_size(g_modal_btnrow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(g_modal_btnrow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(g_modal_btnrow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(g_modal_btnrow, 10, 0);
  g_modal_b0 = lv_button_create(g_modal_btnrow);
  lv_obj_set_style_bg_color(g_modal_b0, C_GREEN, 0);   // primary action — green "OK"
  lv_obj_set_style_pad_hor(g_modal_b0, 20, 0); lv_obj_set_style_pad_ver(g_modal_b0, 8, 0);
  lv_obj_add_event_cb(g_modal_b0, modal_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)0);
  g_modal_b0l = mklabel(g_modal_b0, "OK", &lv_font_montserrat_14, C_BG); lv_obj_center(g_modal_b0l);
  g_modal_b1 = lv_button_create(g_modal_btnrow);
  lv_obj_set_style_bg_color(g_modal_b1, C_TRACK, 0);
  lv_obj_set_style_border_width(g_modal_b1, 1, 0);
  lv_obj_set_style_border_color(g_modal_b1, C_DIM, 0);
  lv_obj_set_style_pad_hor(g_modal_b1, 20, 0); lv_obj_set_style_pad_ver(g_modal_b1, 8, 0);
  lv_obj_add_event_cb(g_modal_b1, modal_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);
  g_modal_b1l = mklabel(g_modal_b1, "", &lv_font_montserrat_14, C_INK); lv_obj_center(g_modal_b1l);

  g_modal = scrim;
  lv_obj_add_flag(g_modal, LV_OBJ_FLAG_HIDDEN);
}

void ui_modal_show(int id, ui_severity_t sev, int prio, const char *title, const char *body,
                   const char *b0, const char *b1, ui_action_cb cb, bool sticky) {
  if (!g_modal) modal_build();
  // Protect an un-acknowledged STICKY modal of a different id from a lower-or-equal-prio show.
  if (g_modal_id && g_modal_ack && id != g_modal_id && prio <= g_modal_prio) return;

  bool wasHidden = lv_obj_has_flag(g_modal, LV_OBJ_FLAG_HIDDEN);
  g_modal_id = id; g_modal_prio = prio; g_modal_cb = cb;
  g_modal_ack = sticky;                       // protection is "sticky", independent of having a button
  bool has_btn = (b0 && b0[0]) || (b1 && b1[0]);

  lv_label_set_text(g_modal_title, title ? title : "");
  lv_obj_set_style_text_color(g_modal_title, sev_color(sev), 0);
  lv_label_set_text(g_modal_body, body ? body : "");

  if (b0 && b0[0]) { lv_label_set_text(g_modal_b0l, b0); lv_obj_remove_flag(g_modal_b0, LV_OBJ_FLAG_HIDDEN); }
  else             lv_obj_add_flag(g_modal_b0, LV_OBJ_FLAG_HIDDEN);
  if (b1 && b1[0]) { lv_label_set_text(g_modal_b1l, b1); lv_obj_remove_flag(g_modal_b1, LV_OBJ_FLAG_HIDDEN); }
  else             lv_obj_add_flag(g_modal_b1, LV_OBJ_FLAG_HIDDEN);
  if (has_btn) lv_obj_remove_flag(g_modal_btnrow, LV_OBJ_FLAG_HIDDEN);
  else         lv_obj_add_flag(g_modal_btnrow, LV_OBJ_FLAG_HIDDEN);

  lv_obj_remove_flag(g_modal, LV_OBJ_FLAG_HIDDEN);
  if (wasHidden) lv_obj_move_foreground(g_modal);
}

void ui_modal_clear(int id) {
  if (!g_modal || g_modal_id != id) return;
  if (g_modal_ack) return;                  // sticky/awaiting a tap — don't yank it out from under the user
  g_modal_id = 0; g_modal_prio = 0; g_modal_cb = nullptr;
  lv_obj_add_flag(g_modal, LV_OBJ_FLAG_HIDDEN);
}

// ── transient toast ─────────────────────────────────────────
static lv_obj_t *g_toast = nullptr, *g_toast_lbl = nullptr;
static void toast_timer_cb(lv_timer_t *t) {
  if (g_toast) lv_obj_add_flag(g_toast, LV_OBJ_FLAG_HIDDEN);
  lv_timer_delete(t);
}
void ui_toast(ui_severity_t sev, const char *text, uint32_t ms) {
  if (!g_toast) {
    g_toast = lv_obj_create(lv_layer_top());
    lv_obj_set_size(g_toast, UI_W - 24, LV_SIZE_CONTENT);
    lv_obj_align(g_toast, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_radius(g_toast, 8, 0);
    lv_obj_set_style_border_width(g_toast, 0, 0);
    lv_obj_set_style_pad_all(g_toast, 8, 0);
    lv_obj_remove_flag(g_toast, LV_OBJ_FLAG_SCROLLABLE);
    g_toast_lbl = mklabel(g_toast, "", &lv_font_montserrat_14, C_BG);
    lv_label_set_long_mode(g_toast_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_toast_lbl, UI_W - 24 - 16);
    lv_obj_set_style_text_align(g_toast_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(g_toast_lbl);
  }
  lv_label_set_text(g_toast_lbl, text ? text : "");
  lv_obj_set_style_bg_color(g_toast, sev_color(sev), 0);
  lv_obj_set_style_bg_opa(g_toast, LV_OPA_COVER, 0);
  lv_obj_remove_flag(g_toast, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(g_toast);
  lv_timer_t *t = lv_timer_create(toast_timer_cb, ms ? ms : 3000, NULL);
  lv_timer_set_repeat_count(t, 1);
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

void ui_goto_anim(int idx) {   // animated slide (presenter-driven: activity reveal → Session, idle drift → Clock)
  if (idx < 0 || idx > 3 || !g_tv) return;
  lv_obj_set_tile_id(g_tv, idx, 0, LV_ANIM_ON);
  set_dot(idx);
}
