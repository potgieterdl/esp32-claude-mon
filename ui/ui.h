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
void ui_set_plan(const char *label);   // Session plan badge text (e.g. "MAX 5X" / "MAX 20X" / "PRO")
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
// Device screen token rows: when the token was last synced/refreshed + when it next renews.
void ui_set_token_info(const char *last_sync, const char *renews);

// ── Reusable notifications (shown above all screens) ────────────────────────
// Two primitives cover every popup: a MODAL (dim scrim + centered card, 0/1/2 action buttons) and
// a transient TOAST. Severity picks the accent color.
typedef enum { UI_SEV_INFO, UI_SEV_OK, UI_SEV_WARN, UI_SEV_ERROR } ui_severity_t;
typedef void (*ui_action_cb)(int id, int button);   // button = 0 (b0) or 1 (b1)

// Show/replace the single modal slot. `id` is a caller-owned tag (re-asserting the same id updates
// in place — safe to call every tick). `prio`: higher wins the slot. A modal with >=1 button is an
// ACKNOWLEDGE modal — it stays until the user taps (cb fires) and is NOT removed by ui_modal_clear;
// a button-less modal is PASSIVE — shown while a condition holds, removed by ui_modal_clear(id).
// b0/b1 may be NULL. An unacknowledged ack-modal is never downgraded by a lower-prio show.
void ui_modal_show(int id, ui_severity_t sev, int prio, const char *title, const char *body,
                   const char *b0, const char *b1, ui_action_cb cb);
void ui_modal_clear(int id);                          // remove the modal if it's this id and passive
void ui_toast(ui_severity_t sev, const char *text, uint32_t ms);   // transient (ms=0 -> ~3s)

#ifdef __cplusplus
}
#endif
