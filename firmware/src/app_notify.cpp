#include "app_notify.h"

// State is touched from the web handler (POST /notify) and read from the presenter (view_tick).
// Both run on the SAME thread — the Arduino loop() calls web_handle() then view_tick() — so no
// locking is needed; plain statics are safe here.
static bool     s_active  = false;
static uint32_t s_set_ms  = 0;             // millis() when the alert was raised (for the timeout)

void notify_input_set()   { s_active = true; s_set_ms = millis(); }
void notify_input_clear() { s_active = false; }

bool notify_input_active() { return s_active; }   // pure query; the presenter enforces the timeout

// Seconds since the alert was raised (unsigned subtraction → millis() rollover-safe); 0 when
// inactive. The presenter compares this against NOTIFY_INPUT_TIMEOUT_S and clears a stuck alert.
uint32_t notify_input_age_s() {
  if (!s_active) return 0;
  return (millis() - s_set_ms) / 1000;
}
