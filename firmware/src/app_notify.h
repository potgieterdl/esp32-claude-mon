#pragma once
#include <Arduino.h>
// "Needs input" alert (issue #2) — a tiny shared-state module that decouples the WRITER
// (app_web: the POST /notify endpoint a Claude Code hook calls) from the READER (app_view:
// the 1 Hz presenter that raises the banner + chime). Neither side talks to the other; both
// just touch this state. No init needed — it starts inactive.
//
// Flow: a Claude Code `Notification` hook POSTs {"event":"needs_input","project":"…"} when a
// session is waiting on the user; a `UserPromptSubmit` hook POSTs {"event":"clear"} when the
// user resumes. The presenter shows a passive modal while active and chimes once on the rising
// edge, and enforces a safety timeout (via notify_input_age_s) so a missed "clear" POST can't pin
// the banner forever — e.g. you approved a permission dialog, which is not a prompt submit.
//
// This module is just the shared state container: pure getters, no policy. The timeout VALUE
// lives here; the presenter (app_view) owns when to act on it.
#define NOTIFY_INPUT_TIMEOUT_S  900   // 15 min — presenter clears an alert older than this

void        notify_input_set(const char *project);  // raise: a session is waiting (project may be null/empty)
void        notify_input_clear();                   // lower: the user is back
bool        notify_input_active();                  // true while waiting (pure query)
const char *notify_input_project();                 // project name for the banner ("" if none given)
uint32_t    notify_input_age_s();                   // seconds since the alert was raised; 0 when inactive
