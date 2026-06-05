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
  int16_t x[1], y[1];
  uint8_t n = touch.getPoint(x, y, 1);   // native portrait coords (0..239, 0..279)
  if (n > 0) {
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = y[0];                // landscape x = native y
    data->point.y = (UI_H - 1) - x[0];   // landscape y = flipped native x
    g_last_activity_ms = millis();       // wake the backlight from idle-dim
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

static lv_obj_t *g_main_scr = nullptr;
static void splash_to_main(lv_timer_t *t) {
  lv_screen_load_anim(g_main_scr, LV_SCR_LOAD_ANIM_FADE_IN, 350, 0, true);
  lv_timer_delete(t);
}

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
  g_last_activity_ms = millis();   // start the idle-dim clock now (don't dim during boot)
  Serial.println("[net+web+data+audio+time] started");
}

void loop() {
  lv_task_handler();
  net_loop();
  web_handle();
  data_loop();
  time_loop();

  view_tick();        // 1Hz data -> UI (presenter)
  update_backlight();  // live brightness + dim-on-idle

  delay(1);   // tighter LVGL cadence for smoother swipe/animation
}
