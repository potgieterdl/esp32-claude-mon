#pragma once
#include <Arduino.h>

// QMI8658 IMU (#31): poll-based shake detection on the shared I2C bus (addr 0x6B).
//
// The IMU's INT pins are NOT broken out on this board (only the touch INT is), so
// we can't use the chip's hardware motion engine via interrupt. Instead we POLL
// the accelerometer from loop() — imu_poll_shake() rate-limits the actual I2C read
// to ~50Hz internally (loop() runs far faster; over-polling I2C caused the error
// flood fought in #18) and runs a small reversal-counting detector in software.
// The detection thresholds/window/debounce are named constants at the top of
// app_imu.cpp — those are the on-device tuning knobs, not the algorithm itself.
//
// NOTE: assumes Wire.begin(SDA=8, SCL=7) has already run (main.cpp does this for
// the shared I2C bus). imu_begin() does NOT touch Wire config.

bool     imu_begin();          // init sensor (accel only); true if found+configured. Call once after Wire.begin().
bool     imu_poll_shake();     // call every loop(); reads accel (rate-limited ~50Hz); returns true ONCE per debounced shake
bool     imu_ok();             // true if the IMU initialised OK (for app_diag boot line)
uint32_t imu_shake_count();    // total shakes detected so far (for app_diag)
