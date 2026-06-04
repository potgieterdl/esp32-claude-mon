# F2 — Rendering performance notes (2026-06)

> ⚠️ **HISTORICAL — research phase, superseded.** This documents the *early* plan (Arduino_GFX +
> `Arduino_ESP32SPIDMA` @ **40 MHz**). The shipped solution is **LovyanGFX async GDMA + double buffers @
> **80 MHz** (write-only bus dodges the matrix cap). Current truth: [`docs/ARCHITECTURE.md`](ARCHITECTURE.md#render-path).
> Kept for the "why we tried X" trail.

Owner: F2. Board: ESP32-C6 (512KB SRAM, **no PSRAM**), ST7789V2 280×240 landscape,
Arduino_GFX + LVGL v9.2.2, single core. Goal: smoother swipes/animations without
adding a draw thread (no RTOS draw unit — `LV_USE_OS = LV_OS_NONE`, keep
`LV_DRAW_SW_DRAW_UNIT_CNT = 1`).

## Change summary
1. **SPI databus: `Arduino_HWSPI` → `Arduino_ESP32SPIDMA`.**
   DMA-driven SPI lets the single CPU core do LVGL render work while the previous
   tile is being pushed to the panel over SPI (render/flush overlap). This is the
   biggest win on a single-core part.
2. **Clock: `gfx->begin(40000000)` (40 MHz), fallback 27 MHz.**
   `begin()` returns `bool`; on `false` retry at 27 MHz. LCD_SCK=GPIO1/LCD_DIN=GPIO2
   are GPIO-matrix routed (not IOMUX), so 80 MHz is unrealistic — 40 MHz is the
   practical ceiling, 27 MHz the safe fallback if artifacts appear.
3. **Two partial DMA draw buffers, ~1/10 screen each.**
   `280 × 24 × 2 B = 13,440 B` per buffer (×2 ≈ 26.9 KB) from
   `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`, `LV_DISPLAY_RENDER_MODE_PARTIAL`.
   Two buffers let LVGL render the next tile while the first is flushing via DMA.
   Frees ~40 KB vs the current single `280×120×2 = 67.2 KB` buffer.
4. **Loop cadence paced by `lv_timer_handler()` return value** (next-due delay, ms),
   clamped to ≤16 ms, instead of `lv_task_handler()` + fixed `delay(5)`.
   (`lv_task_handler` is just the legacy alias of `lv_timer_handler`; switch so we
   can read the return value.)
5. **`LV_DEF_REFR_PERIOD 33 → 16`** in `lv_conf.h` (target ~60 Hz refresh tick).

## Rationale / validation against current code
- `Arduino_ESP32SPIDMA` is **confirmed C6-compatible**: `CONFIG_IDF_TARGET_ESP32C6`
  is in the `#if` target guard of both `.h`/`.cpp`, and it is **already compiled**
  in this tree (`firmware/.pio/build/esp32-c6/.../Arduino_ESP32SPIDMA.cpp.o`).
- Constructor maps 1:1 with the current `Arduino_HWSPI(dc, cs, sck, mosi)` call.
  On C6 the signature is
  `Arduino_ESP32SPIDMA(dc, cs, sck, mosi, miso=GFX_NOT_DEFINED, spi_num=FSPI, is_shared_interface=false)`.
  So `new Arduino_ESP32SPIDMA(LCD_DC, LCD_CS, LCD_SCK, LCD_DIN)` is correct
  (miso defaults to unused, FSPI → `_spi_num-1` = SPI2_HOST, the free host).
- `gfx->draw16bitRGBBitmap(...)` in `disp_flush` is an `Arduino_GFX` method that
  routes pixels through the databus `writePixels()` — **DMA is engaged with no
  change to `disp_flush`** (keep it as-is, including `lv_disp_flush_ready`).
- `disp_flush` calls `lv_disp_flush_ready()` **synchronously** after
  `draw16bitRGBBitmap` returns. The vendored GFX `draw16bitRGBBitmap` blocks until
  the DMA queue drains for that call, so this remains correct (no premature buffer
  reuse). The render/flush overlap comes from LVGL having a *second* buffer to draw
  into; we are not introducing async flush. This is the safe, proven Waveshare path.
- Backlight is already LEDC PWM (GPIO6) in `setup()` — **not touched**.

## DMA scratch buffers (driver-internal)
`Arduino_ESP32SPIDMA::begin()` allocates two internal scratch buffers of
`ESP32SPIDMA_MAX_PIXELS_AT_ONCE * 2` bytes each from `MALLOC_CAP_DMA`
(default 1024 px → 2 KB ×2 = 4 KB), and chunks `writePixels` at that size.
- **Default (1024) is fine** for a 6720-px tile (it chunks internally). No build
  flag is strictly required.
- Optional: raise `-DESP32SPIDMA_MAX_PIXELS_AT_ONCE=2048` to cut transfer count
  (~4 chunks → 2 chunks per tile) for slightly less per-call overhead; costs
  2 KB→4 KB ×2 = 8 KB DMA scratch. Keep modest — do **not** size it to a full tile
  (6720) as that adds ~27 KB of DMA-internal RAM pressure for little gain.

## Expected impact
- Visibly smoother tileview swipes / spinner / progress animations: CPU renders the
  next tile during the SPI push instead of stalling on it.
- ~40 KB SRAM freed (67 KB → ~27 KB draw buffers + ~4–8 KB DMA scratch), useful
  headroom for WiFi/TLS, the data-poll buffers (F4) and the optional screenshot
  shadow buffer (F9).
- Higher effective frame rate from `REFR_PERIOD 16` + paced loop (no fixed 5 ms
  idle, no over-refreshing).

## Risks / fallback
- **Artifacts / no image at 40 MHz** (matrix-routed pins): drop to 27 MHz — handled
  by the `begin()` return-value fallback in the integration steps.
- **DMA buffer alloc failure** (fragmentation): `lv_display_set_buffers` / the GFX
  `begin()` would fail. Mitigate by allocating the draw buffers **early in setup()
  before WiFi.begin()** (they already are, since alloc is in `setup()`), and assert
  non-NULL. Two 13.4 KB internal-DMA blocks are small; low risk on a fresh heap.
- **Single core**: a full-width tileview swipe still invalidates the whole 280 px
  width per row-band — keep invalidated areas small where possible (UI concern, not
  this change). Do **not** raise `LV_DRAW_SW_DRAW_UNIT_CNT` (>1 needs an RTOS draw
  thread we deliberately avoid).
- `LV_DEF_REFR_PERIOD 16` raises refresh cadence; if CPU-bound it self-throttles
  (timer just runs late) — safe, no correctness risk.
- Keep `lv_disp_flush_ready` synchronous (don't move it to a DMA-complete callback)
  unless a future change makes the GFX call truly non-blocking.

## Verify smoothness on device
1. Build/flash; confirm serial shows the chosen SPI clock (add a log in the
   fallback branch) and no `ESP_ERROR_CHECK` abort from `spi_bus_initialize`.
2. Enable LVGL perf monitor temporarily: in `lv_conf.h` set `LV_USE_SYSMON 1`,
   `LV_USE_PERF_MONITOR 1` → on-screen FPS/CPU overlay. Compare FPS during a
   tileview swipe before vs after. (Revert after measuring.)
3. Eyeball: swipe between screens + watch the session/weekly progress animation —
   should be tear-free and fluid; no flicker/garbage rows (would indicate clock too
   high → 27 MHz).
4. Check free heap (`ESP.getFreeHeap()`) at end of setup — expect ~40 KB more than
   the HWSPI/67 KB-buffer baseline.
