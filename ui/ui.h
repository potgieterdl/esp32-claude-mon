// Portable LVGL UI for the Claude monitor — NO Arduino/hardware deps.
// Shared by the device firmware (firmware/) and the desktop simulator (experiments/sim/).
#pragma once
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Canvas size (landscape).
#define UI_W 280
#define UI_H 240

// Build the whole UI (tileview + 4 screens + page dots) under `parent`
// (typically lv_screen_active()). Call after lv_init() + display creation.
void ui_build(lv_obj_t *parent);

// Jump directly to a screen 0..3 (no animation) and sync the page dots.
// Handy for the simulator to screenshot each screen.
void ui_goto(int idx);
void ui_goto_anim(int idx);   // same, but animated slide (one-time Clock->Session reveal once connected)

// Live data hooks (F4). pct 0..100; secs_left < 0 = unknown (keeps internal timer).
void ui_set_session(int five_hour_pct, long secs_left, const char *reset_at);  // reset_at e.g. "at 4:30 PM"
void ui_set_weekly(int weekly_pct, long secs_left);
void ui_set_online(bool online, bool stale);   // top-bar status dot color
void ui_clear_usage();   // blank Session/Weekly figures to "--" when offline / no data (honest display)
void ui_set_clock(const char *time_str, const char *date_str);  // Clock screen (F6)
void ui_set_clock_reset(const char *next_reset);   // Clock screen "next reset .." line ("" to hide)

// Boot splash (Concept A): builds on its OWN screen (coral ring sweep + version fade-in).
// Caller loads it, then transitions to the main screen after ~5s. Returns the screen obj.
lv_obj_t *ui_build_splash(const char *version);

// Device screen live values (F7) — pass preformatted strings.
void ui_set_device(const char *wifi, const char *ip, const char *signal,
                   const char *batt, const char *heap, const char *fw);

#ifdef __cplusplus
}
#endif
