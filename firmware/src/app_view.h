#pragma once
#include <stdint.h>
// Presenter (audit #1): maps device state (data/time/net/battery) -> UI once per second,
// and owns the usage-threshold chime FSM + the idle "sleep mode" navigation (#6). Keeps main.cpp
// a thin integrator and isolates all formatting/business logic in one testable place.
void view_begin();   // one-time init (battery-sense pin)
void view_tick(uint32_t input_idle_ms);   // call every loop(); refreshes UI at ~1 Hz.
                                          // input_idle_ms = ms since the last touch (drives sleep mode)
