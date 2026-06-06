#pragma once
// Dev-time serial diagnostics (device side). USB-serial only — no web endpoints, since this is
// only needed while developing (USB attached). Prints a boot banner (reset reason + I2C bus scan)
// and a periodic one-line health summary. (Raw Arduino-HAL I2C errors surface directly in the serial
// stream — see app_diag.cpp; since #18 the bus is quiet at idle, so any reappearance is a regression.)
void diag_begin();   // call once in setup() AFTER Wire.begin() + the app modules have started
void diag_loop();    // call from loop(); prints a health line on a ~10s cadence
