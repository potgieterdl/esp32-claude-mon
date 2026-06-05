#include "app_diag.h"
#include <Arduino.h>
#include <Wire.h>
#include "esp_system.h"   // esp_reset_reason
#include "app_net.h"
#include "app_data.h"
#include "app_config.h"

#define DIAG_PERIOD_MS 10000

static const char *reset_reason_str(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_EXT:       return "ext-reset";
    case ESP_RST_SW:        return "sw-reset";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "int-WDT";
    case ESP_RST_TASK_WDT:  return "task-WDT";
    case ESP_RST_WDT:       return "other-WDT";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    default:                return "unknown";
  }
}

// One-time bus scan so we know which I2C devices ACK at boot. On this board:
// 0x15 CST816 touch · 0x18 ES8311 audio · 0x51 PCF85063 RTC · 0x6B QMI8658 IMU (unused).
static void i2c_scan() {
  Serial.print("[diag] I2C scan:");
  int found = 0;
  for (uint8_t a = 0x08; a <= 0x77; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { Serial.printf(" 0x%02X", a); found++; }
  }
  if (!found) Serial.print(" (none)");
  Serial.println();
}

void diag_begin() {
  Serial.printf("[diag] boot: reset=%s fw=%s heap=%uKB chip=%s\n",
                reset_reason_str(esp_reset_reason()), FW_VERSION,
                (unsigned)(ESP.getFreeHeap() / 1024), ESP.getChipModel());
  i2c_scan();
  // NOTE: the recurring Arduino-HAL I2C errors print via ets_printf, not esp_log, so they can't be
  // counted through esp_log_set_vprintf — they remain visible directly in the raw serial stream.
}

void diag_loop() {
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last < DIAG_PERIOD_MS) return;
  last = now;
  // Only emit when a USB host actually has the CDC port open. Headless (the multi-day case) this is
  // false, so we never touch HWCDC — no write stalls, no overhead. `Serial`'s bool operator reflects
  // the USB-CDC connection state.
  if (!Serial) return;
  Serial.printf("[diag] up=%lus heap=%uKB/min=%uKB rssi=%ddBm wifi(up=%s,conn=%lu,drop=%lu) "
                "data(age=%lus err='%s')\n",
                (unsigned long)(now / 1000),
                (unsigned)(ESP.getFreeHeap() / 1024),
                (unsigned)(ESP.getMinFreeHeap() / 1024),
                net_online() ? net_rssi() : 0,
                net_online() ? "Y" : "N",
                (unsigned long)net_connects(),
                (unsigned long)net_disconnects(),
                (unsigned long)data_age_seconds(),
                data_last_error());
}
