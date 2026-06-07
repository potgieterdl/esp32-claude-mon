#include "app_imu.h"

#include <Wire.h>
#include <math.h>
#include "SensorQMI8658.hpp"

// ---- I2C address / pins (shared bus; Wire already begun in main.cpp) ----
#define IMU_I2C_ADDR 0x6B          // QMI8658 (low-address strap) on this board
#define I2C_SDA 8
#define I2C_SCL 7

// ---- Shake detector tuning knobs (the ON-DEVICE knobs live HERE) ------------
// A shake = the user oscillating the device, so |a| (the rotation-invariant accel magnitude) PEAKS on
// every swing and settles back toward 1 g between swings. We count PEAKS: |a| must deviate from gravity
// by >= SHAKE_PEAK_G (a strong swing), then settle back within SHAKE_RESET_G of 1 g before the next peak
// counts — needing SHAKE_MIN_PEAKS peaks inside SHAKE_WINDOW_MS to fire. We use the ABSOLUTE deviation
// |‖a‖-1g| so it's direction-agnostic: a side-to-side shake keeps ‖a‖ = √(g²+a_lin²) >= 1 g and never
// dips into free-fall, so an earlier "must also dip below 1 g" test silently missed normal shakes — this
// counts the upward magnitude spikes instead. A single bump = 1 peak (never reaches the minimum); slow
// carrying / a touchscreen swipe barely moves ‖a‖ off 1 g, so no peaks. After a hit we go deaf for
// SHAKE_DEBOUNCE_MS so one shake = one event and the settle wobble can't re-fire. Tune these on-device.
static const float    SHAKE_PEAK_G      = 0.28f;  // |‖a‖-1g| must exceed this to count one peak (~1.28 g / 0.72 g)
static const float    SHAKE_RESET_G     = 0.14f;  // ...then settle back within this of 1 g before the next peak
static const uint8_t  SHAKE_MIN_PEAKS   = 3;      // strong peaks needed to fire (3 back-and-forth swings; a single jolt = 1)
static const uint32_t SHAKE_WINDOW_MS   = 900;    // ...all within this sliding window (a short, brisk shake fits easily)
static const uint32_t SHAKE_DEBOUNCE_MS = 800;    // ignore further shakes for this long after one fires
static const uint32_t IMU_POLL_MS       = 20;     // ~50Hz sensor read cadence (loop() is far faster; spare the I2C bus)

static const float    GRAVITY_G = 1.0f;           // resting ‖a‖ baseline (gravity, orientation-independent)

// ---- Module state ----
static SensorQMI8658 s_imu;
static bool     s_ok     = false;
static uint32_t s_shakes = 0;

static uint32_t s_last_read_ms   = 0;   // ~50Hz rate-limit gate
static uint32_t s_suppress_until = 0;   // debounce deadline (millis)
static bool     s_armed          = true; // ready to count the next peak (set when ‖a‖ settles near 1 g)
static uint8_t  s_peaks          = 0;   // strong peaks in the current sequence
static uint32_t s_seq_start_ms   = 0;   // time of the first peak in the current sequence

bool imu_begin() {
  // Init the QMI8658 on the EXISTING shared Wire (pass pins like touch.begin does;
  // do NOT re-begin Wire — main.cpp already brought up the shared I2C bus).
  if (!s_imu.begin(Wire, IMU_I2C_ADDR, I2C_SDA, I2C_SCL)) {
    Serial.println("[imu] QMI8658 init FAILED (shake gesture disabled)");
    return false;
  }
  // Accelerometer only — gyro stays off (lower power, and the accel keeps its own
  // ODR instead of being slaved to the gyro rate). Default sample mode is async,
  // so getDataReady() reflects the accel data-ready bit.
  s_imu.configAccelerometer(SensorQMI8658::ACC_RANGE_4G,    // +/-4g spans a hand shake without clipping
                            SensorQMI8658::ACC_ODR_125Hz,   // chip samples >2x our ~50Hz read cadence
                            SensorQMI8658::LPF_MODE_0);      // mild low-pass to tame high-freq noise
  s_imu.enableAccelerometer();
  s_ok = true;
  Serial.println("[imu] QMI8658 ok (accel, poll-mode shake)");
  return true;
}

bool     imu_ok()          { return s_ok; }
uint32_t imu_shake_count() { return s_shakes; }

bool imu_poll_shake() {
  if (!s_ok) return false;                       // defensive: no sensor -> never shakes

  uint32_t now = millis();
  if (now - s_last_read_ms < IMU_POLL_MS) return false;   // ~50Hz rate-limit (don't hammer the I2C bus)
  s_last_read_ms = now;

  if (!s_imu.getDataReady()) return false;
  float x, y, z;
  if (!s_imu.getAccelerometer(x, y, z)) return false;     // x/y/z are in g

  // During debounce: keep draining samples but hold the detector reset so the
  // post-shake settle wobble can't immediately re-arm it.
  if ((int32_t)(now - s_suppress_until) < 0) {   // wrap-safe: still inside the debounce window
    s_armed = true; s_peaks = 0;
    return false;
  }

  float ad = fabsf(sqrtf(x * x + y * y + z * z) - GRAVITY_G);   // strength of the magnitude swing off gravity

  if (ad <= SHAKE_RESET_G) {
    s_armed = true;                                       // settled near 1 g -> ready to count the next peak
  } else if (s_armed && ad >= SHAKE_PEAK_G) {             // a strong swing while armed = one peak
    s_armed = false;                                      // disarm until ‖a‖ settles again (don't double-count one peak)
    if (s_peaks == 0 || now - s_seq_start_ms > SHAKE_WINDOW_MS) {
      s_peaks        = 1;                                 // start a fresh sequence (first peak, or the previous timed out)
      s_seq_start_ms = now;
    } else {
      s_peaks++;                                          // another peak within the window
    }
    if (s_peaks >= SHAKE_MIN_PEAKS) {                     // enough swings in time -> a deliberate shake
      s_shakes++;
      s_peaks = 0; s_armed = true;
      s_suppress_until = now + SHAKE_DEBOUNCE_MS;
      return true;                                        // one debounced shake event
    }
  }
  return false;
}
