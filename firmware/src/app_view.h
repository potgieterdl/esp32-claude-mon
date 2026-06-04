#pragma once
// Presenter (audit #1): maps device state (data/time/net/battery) -> UI once per second,
// and owns the usage-threshold chime FSM. Keeps main.cpp a thin integrator and isolates
// all formatting/business logic in one testable place.
void view_begin();   // one-time init (battery-sense pin)
void view_tick();    // call every loop(); refreshes the UI at ~1 Hz
