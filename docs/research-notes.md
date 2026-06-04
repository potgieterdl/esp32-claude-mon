# Research Notes — implementation decisions (2026-06)

> ⚠️ **HISTORICAL — early research, partly superseded.** Some choices here were changed in implementation
> (render path → **LovyanGFX 80 MHz**, OTA → **sync** WebServer not async, config.json precedence →
> compiled→json→PUT). Current truth: [`docs/ARCHITECTURE.md`](ARCHITECTURE.md) + the board
> [`SPEC.md`](../boards/esp32c6/esp32-c6-touch-lcd-1.69/SPEC.md). Kept for rationale/history.

Distilled from parallel research agents. Board: ESP32-C6 (512KB SRAM, **no PSRAM**, 8MB flash),
ST7789V2 280×240 landscape, Arduino_GFX + LVGL v9, pioarduino/Arduino-ESP32 3.3.8. TFT_eSPI unusable on C6.

## Display quality (richer color, deeper black) — quick win
Root cause: backlight at 100% + UI background was `0x15130F` (warm grey, not black).
1. **Backlight PWM via LEDC on GPIO6** (Arduino-ESP32 3.x API): `ledcAttach(LCD_BL,5000,8); ledcWrite(LCD_BL,~128)` (~50%). Biggest perceived-black win. Don't mix with digitalWrite.
2. **True-black UI background** (`0x000000`) + more **saturated accents**.
3. Optional gamma/VCOM via `bus->writeC8D8(0xBB, vcom)` etc. after `gfx->begin()` — marginal, do last.
ST7789 has no saturation register — saturate in RGB565. Inversion/RGB order already correct (IPS=true → INVON).

## Rendering performance / smoothness
- **Use `Arduino_ESP32SPIDMA`** (confirmed C6-compatible in vendored source) instead of `Arduino_HWSPI` — DMA frees the single core during pixel push (biggest win). `gfx->begin(40000000)` — 40 MHz (pins 1/2 are GPIO-matrix routed, so 80 MHz unrealistic; fall back 27 MHz if artifacts).
- **Two partial draw buffers** ~1/10 screen each (`280×24×2 ≈ 13.4KB` ×2 = 27KB) in `MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL`; PARTIAL render mode. Frees ~40KB vs current single 67KB buffer and enables render/flush overlap.
- **Loop cadence**: pace by `lv_timer_handler()` return value, cap ~16ms. Set `LV_DEF_REFR_PERIOD 16` in lv_conf.h. Single core → keep invalidated area small (tileview swipe redraws full width).

## Audio notifications (ES8311 + NS4150B + speaker)
From `docs/demo/.../01_audio_out` & `04_es8311_example`:
- ES8311 I2C addr **0x18**; I2S pins **MCLK=19, BCLK=20, LRCK=22, DOUT=23, DIN=21**.
- Libs: built-in `ESP_I2S.h` (`I2SClass`) + vendored `es8311` driver. **Do NOT** add ESP32-audioI2S.
- Init: `es8311_init` (16-bit, mclk=sr*256), `es8311_voice_volume_set(handle, ~35)` for soft.
- **Synthesize short sine chime in RAM** (~5KB) with attack/decay **envelope** (avoid clicks). Play on a **dedicated FreeRTOS task** (never `i2s.write` from LVGL loop — it blocks). Idle `es8311_voice_mute` to kill hiss (no amp-enable GPIO).
- Triggers: soft 2-note at ≥70% (debounce: once per crossing), gentle single low note at 100%/reset. "Soft" = low codec volume + low digital amplitude (~7000/32767).

## WiFi / settings
- **Now:** gitignored `firmware/include/wifi_config.h` (SSID/pass/proxy url+token/thresholds) + `wifi_config.example.h`. Zero deps, unblocks data path.
- **Soon:** `/config.json` on **LittleFS** (`board_build.filesystem=littlefs`, `data/`, `pio run -t uploadfs`), parsed with **ArduinoJson v7**; precedence **NVS → config.json → header**. Editable without reflash.
- Connect: `WiFi.onEvent` + `setAutoReconnect(true)` + `persistent(false)`; keep callback trivial (set flag), poll from loop on `wifiUp`. Don't touch LVGL from the WiFi task.
- Skip tzapu WiFiManager (no C6 badge); Espressif SoftAP/captive portal is the later option.

## OTA updates
- **ElegantOTA v3 async** on a shared **ESPAsyncWebServer** (use the **ESP32Async forks**, not me-no-dev): `ESP32Async/AsyncTCP`, `ESP32Async/ESPAsyncWebServer`, `ayushsharma82/ElegantOTA`; `-DELEGANTOTA_USE_ASYNC_WEBSERVER=1`; `ElegantOTA.setAuth(...)`.
- **8MB OTA partition CSV** (two 3MB app slots + littlefs); `board_build.partitions=...`, `board_upload.flash_size=8MB`. **Do one USB flash** after switching tables (else bootloop).
- Optional `[env:...-ota]` `upload_protocol=espota` for wireless dev push. Optional IDF rollback: `esp_ota_mark_app_valid_cancel_rollback()` after healthy boot.

## Remote screenshot (debug only)
- `lv_snapshot` outputs **ARGB8888 (~268KB)** → infeasible on 512KB w/ WiFi+TLS. **Don't use.**
- Instead: **full-frame RGB565 shadow buffer (~134KB)** filled inside `disp_flush` (memcpy per region). **Allocate before `WiFi.begin()`** (contiguous block).
- Serve as **raw BMP over plain HTTP** (no TLS), 16bpp BITFIELDS 5-6-5, rows bottom-up, chunked stream. `curl http://<ip>/screenshot.bmp`.
- **Compile-time gated `#ifdef ENABLE_SCREENSHOT`, debug env only** — 134KB is too much to give up permanently. Watch RGB565 byte order vs BMP (may need swap).

## Key sources
ESP-IDF SPI master (C6 IOMUX vs matrix), esp-bsp LVGL performance guide, LVGL timer_handler & snapshot docs,
ElegantOTA async/auth docs, Espressif partition-tables guide, Arduino-ESP32 WiFi events, vendored
`Arduino_ESP32SPIDMA.cpp` / `Arduino_ST7789.*` / `es8311.*` / `bsp_display.*`.
