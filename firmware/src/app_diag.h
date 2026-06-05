#pragma once
// Dev-time serial diagnostics (device side). USB-serial only — no web endpoints, since this is
// only needed while developing (USB attached). Prints a boot banner (reset reason + I2C bus scan)
// and a periodic one-line health summary, and installs an esp_log hook that counts I2C-related
// error lines so the recurring "esp32-hal-i2c-ng ESP_ERR_INVALID_STATE" flood is quantified.
void diag_begin();   // call once in setup() AFTER Wire.begin() + the app modules have started
void diag_loop();    // call from loop(); prints a health line on a ~10s cadence
