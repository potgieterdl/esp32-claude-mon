// ESP32-C6 Claude Monitor — device firmware (hardware + LVGL glue).
// The UI itself lives in the portable ui/ module (shared with the simulator).
//
// Display: Arduino_GFX ST7789V2, landscape 280x240 (rotation 1, x-offset 20).
// Touch:   CST816T via SensorLib, mapped to landscape coords.

#include <lvgl.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "TouchDrvCSTXXX.hpp"
#include <Wire.h>
#include "ui.h"
#include "app_net.h"
#include "app_web.h"
#include "app_data.h"
#include "app_audio.h"
#include "app_time.h"
#include "app_view.h"
#include "app_diag.h"
#include "app_imu.h"
#include "app_config.h"
#include "app_settings.h"
#include <Preferences.h>

#define LCD_SCK 1
#define LCD_DIN 2
#define LCD_CS  5
#define LCD_DC  3
#define LCD_RST 4
#define LCD_BL  6
#define I2C_SDA 8
#define I2C_SCL 7
#define TOUCH_INT 11   // CST816 INT (TP_INT); gate I2C reads on it so we don't poll an idle chip (#18)

// LovyanGFX device: ST7789V2 over SPI2 with GDMA (async flush → overlaps render + transfer).
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel;
  lgfx::Bus_SPI _bus;
public:
  LGFX() {
    { auto c = _bus.config();
      c.spi_host = SPI2_HOST; c.spi_mode = 0;
      c.freq_write = 80000000; c.freq_read = 16000000;   // 80MHz: write-only bus dodges the matrix read cap
      c.dma_channel = SPI_DMA_CH_AUTO;
      c.pin_sclk = LCD_SCK; c.pin_mosi = LCD_DIN; c.pin_miso = -1; c.pin_dc = LCD_DC;
      _bus.config(c); _panel.setBus(&_bus); }
    { auto c = _panel.config();
      c.pin_cs = LCD_CS; c.pin_rst = LCD_RST; c.pin_busy = -1;
      c.panel_width = 240; c.panel_height = 280;
      c.offset_x = 0; c.offset_y = 20;          // ST7789V2 240x280 gap
      c.readable = false; c.invert = true;      // IPS needs inversion
      c.rgb_order = true;  c.bus_shared = false;   // true = RGB (coral renders coral, not blue)
      _panel.config(c); }
    setPanel(&_panel);
  }
};
static LGFX tft;

TouchDrvCSTXXX touch;
static bool touchOk = false;
static volatile bool s_touch_irq = false;   // set by the CST816 INT (GPIO11) on a touch event (#18)
static void IRAM_ATTR touch_isr() { s_touch_irq = true; }

static uint32_t millis_cb() { return millis(); }

// Backlight: live brightness from settings + optional dim-on-idle (resets on touch).
static uint32_t g_last_activity_ms = 0;
static void update_backlight() {
  const AppSettings &s = settings();
  uint8_t pct = s.brightness;
  if (s.dim_on_idle && (millis() - g_last_activity_ms) > (uint32_t)s.dim_after_s * 1000)
    pct = s.dim_brightness;
  static int last_duty = -1;
  int duty = (int)pct * 255 / 100;
  if (duty != last_duty) { ledcWrite(LCD_BL, duty); last_duty = duty; }
}

static void disp_flush(lv_display_t *d, const lv_area_t *area, uint8_t *px) {
  uint32_t w = lv_area_get_width(area);
  uint32_t h = lv_area_get_height(area);
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushPixelsDMA((uint16_t *)px, w * h);   // async DMA; CPU renders next buffer meanwhile
  tft.endWrite();
  lv_disp_flush_ready(d);
}

static void touch_read(lv_indev_t *indev, lv_indev_data_t *data) {
  LV_UNUSED(indev);
  if (!touchOk) { data->state = LV_INDEV_STATE_RELEASED; return; }

  // #18: only hit the I2C bus when the CST816 INT (GPIO11) reports activity. Polling an idle
  // controller every cycle returns ~1/s [259] ESP_ERR_INVALID_STATE. The INT wakes a polling
  // burst; we keep reading each cycle until the finger lifts (getPoint==0), then go quiet again —
  // so movement + release still register, but an untouched panel does zero bus traffic.
  // No critical section needed on s_touch_irq: the only edge we could lose is one arriving as we
  // enter polling mode, which is moot — we then poll every cycle until release. `polling` is
  // task-only (the ISR never touches it).
  static bool polling = false;
  if (s_touch_irq) { s_touch_irq = false; polling = true; }
  if (!polling) { data->state = LV_INDEV_STATE_RELEASED; return; }

  int16_t x[1], y[1];
  uint8_t n = touch.getPoint(x, y, 1);   // native portrait coords (0..239, 0..279)
  if (n > 0) {
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = y[0];                // landscape x = native y
    data->point.y = (UI_H - 1) - x[0];   // landscape y = flipped native x
    g_last_activity_ms = millis();       // wake the backlight from idle-dim
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
    polling = false;                     // finger lifted — back to INT-gated idle
  }
}

static lv_obj_t *g_main_scr = nullptr;
static void splash_to_main(lv_timer_t *t) {
  lv_screen_load_anim(g_main_scr, LV_SCR_LOAD_ANIM_FADE_IN, 350, 0, true);
  lv_timer_delete(t);
}

// ── Shake-to-summon Claude bot (#31) ─────────────────────────────────────────
// app_imu detects a deliberate shake; we toggle the easter-egg bot, play its jingle, and arm a 15 s
// auto-hide. A swipe/tap dismiss in the UI calls bot_dismissed() so our timer can't re-fire on a
// bot that's already gone. All dismiss paths (2nd shake / swipe / tap / 15 s) end with it hidden.
static uint32_t g_bot_hide_at = 0;                   // millis() auto-hide deadline (0 = not shown by us)
static void bot_dismissed() { g_bot_hide_at = 0; }   // UI swipe/tap dismissed it → cancel our timer

void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL);
  view_begin();        // battery-sense pin
  settings_begin();    // LittleFS config.json (Wi-Fi/device/oauth/...) — must precede net/web/time

  tft.init();
  tft.setRotation(1);            // landscape 280x240
  tft.setSwapBytes(true);        // LVGL RGB565 (LE) -> ST7789 (BE)
  tft.fillScreen(0);             // black
  ledcAttach(LCD_BL, 5000, 8);   // backlight PWM (8-bit @ 5kHz)
  ledcWrite(LCD_BL, settings().brightness * 255 / 100);   // live brightness (default 50%)

  touchOk = touch.begin(Wire, CST816_SLAVE_ADDRESS, I2C_SDA, I2C_SCL);
  Serial.println(touchOk ? "[touch] CST816 ok" : "[touch] CST816 init FAILED (display still runs)");
  pinMode(TOUCH_INT, INPUT_PULLUP);                                       // #18: gate touch reads on the INT line
  attachInterrupt(digitalPinToInterrupt(TOUCH_INT), touch_isr, FALLING); // CST816 pulses INT low on a touch event

  lv_init();
  lv_tick_set_cb(millis_cb);

  // Double DMA buffers (~1/4 screen each) so LVGL renders buf B while DMA ships buf A.
  const size_t BUF_SZ = UI_W * 60 * 2;
  static lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(BUF_SZ, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  static lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(BUF_SZ, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  lv_display_t *disp = lv_display_create(UI_W, UI_H);
  lv_display_set_flush_cb(disp, disp_flush);
  lv_display_set_buffers(disp, buf1, buf2, BUF_SZ, LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touch_read);

  g_main_scr = lv_obj_create(NULL);
  ui_build(g_main_scr);
  ui_bot_set_dismiss_cb(bot_dismissed);   // #31: UI swipe/tap dismiss → cancel our auto-hide timer
  ui_goto(2);   // start on the Clock screen (shows CONNECTING while WiFi comes up; usage screens blank until data)

  // Boot splash only when the firmware version changed since last boot (stored in NVS).
  Preferences prefs;
  prefs.begin("sys", false);
  bool fresh = (prefs.getString("fwseen", "") != String(FW_VERSION));
  if (fresh) prefs.putString("fwseen", FW_VERSION);
  prefs.end();
  if (fresh) {
    lv_screen_load(ui_build_splash(FW_VERSION));
    lv_timer_create(splash_to_main, 5000, nullptr);   // 1s settle + 3s sweep + ~1s hold, then fade
  } else {
    lv_screen_load(g_main_scr);
  }
  Serial.println("[ui] ready");

  net_begin();   // WiFi (non-blocking; creds from config.json)
  web_begin();   // shared HTTP server (status page at /)
  data_begin();  // Claude usage poller (F4)
  audio_begin(); // ES8311 chimes (F5)
  audio_chime_reset();  // soft boot chime — confirms audio works
  time_begin();  // NTP + RTC (F6)
  imu_begin();   // QMI8658 IMU — shake-to-summon the bot easter egg (#31); Wire already up above
  g_last_activity_ms = millis();   // start the idle-dim clock now (don't dim during boot)
  Serial.println("[net+web+data+audio+time+imu] started");
  diag_begin();    // dev-time serial diagnostics: reset reason + I2C scan + log-error counter
}

void loop() {
  lv_task_handler();
  net_loop();
  web_handle();
  data_loop();
  time_loop();

  // Shake-to-summon the Claude bot easter egg (#31). Reset the idle clock first so this same tick's
  // view_tick treats the shake as activity → wakes the #6 sleeper; un-dims the backlight too.
  if (imu_poll_shake()) {
    g_last_activity_ms = millis();
    if (ui_bot_visible()) { ui_bot_hide(); g_bot_hide_at = 0; }              // a second shake dismisses
    else if (ui_bot_show()) { audio_chime_bot(); g_bot_hide_at = millis() + 15000; }  // real summon → jingle + 15 s timer
    // (ui_bot_show() returns false if a previous dismissal is still tearing down → no chime, no orphaned stage)
  }
  if (g_bot_hide_at && (int32_t)(millis() - g_bot_hide_at) >= 0) {           // 15 s auto-hide
    if (ui_bot_visible()) ui_bot_hide();
    g_bot_hide_at = 0;
  }

  view_tick(millis() - g_last_activity_ms);   // 1Hz data -> UI; pass touch-idle for sleep mode (#6)
  update_backlight();  // live brightness + dim-on-idle
  diag_loop();         // ~10s serial health line (dev-time)

  delay(1);   // tighter LVGL cadence for smoother swipe/animation
}
