# Architecture

How the project is structured and the few decisions that must not be undone. Hardware facts live per-board
in [`boards/`](../boards/README.md); this doc is the app/software view.

## Two layers: portable core + device adapter
- **Portable core — `ui/`** (LVGL only, no hardware). Builds the 4 screens + splash; live data enters via
  `ui_set_*` setters. Compiles **unchanged** for the device firmware *and* the desktop simulator
  (`experiments/sim`), so UI work is validated on PNGs before flashing.
- **Device adapter — `firmware/src/`** glues the core to one board. The contract a board must implement:
  **display flush** (LVGL → panel), **touch read**, **backlight** (PWM), **battery** (ADC), and optionally
  **audio** / **RTC**. Per-board pins & quirks: `boards/<arch>/<slug>/SPEC.md`.

> **Adding a board touches only the adapter + `boards/<slug>/`, never `ui/`.** If a UI change is needed for a
> new board, the abstraction has leaked.

## Firmware modules (`firmware/src/`)
| Module | Role |
|---|---|
| `main.cpp` | Hardware init (LovyanGFX display, CST816T touch, LVGL buffers), main loop, backlight + dim-on-idle |
| `app_settings` | device `config.json` (LittleFS) — `settings()` accessor; defaults injected from the repo-root `config.json` |
| `app_net` | WiFi (non-blocking; `setSleep(false)`; loop-driven backoff reconnect) |
| `app_web` | HTTP server: `/` status · `/config.json` settings · `/update` OTA (ElegantOTA) |
| `app_data` | Polls the proxy → cached usage snapshot (stale = age-based) |
| `app_view` | 1 Hz presenter: device state → `ui_set_*`; usage-chime FSM; reset-drain trigger |
| `app_time` | NTP + PCF85063 RTC; timezone from settings |
| `app_audio` | ES8311 chimes on a dedicated FreeRTOS task (never block the LVGL loop) |

## Render path (the smoothness win — **do not revert**)
**LovyanGFX async GDMA** + **double partial buffers** (overlaps SPI transfer with CPU render — the only
parallelism a single-core C6 has) + **SPI 80 MHz**. 80 MHz is safe because the bus is **write-only**
(`miso = -1`), so the GPIO-matrix ~40 MHz cap — a *MISO-read* constraint — doesn't apply. `rgb_order = true`.

History: an early **Arduino_ESP32SPIDMA** attempt crashed (C6 lib bug: spi host = -1 → abort, looked like
"DMA is bad") → the fix was LovyanGFX's *working* DMA. **Arduino_GFX / TFT_eSPI are not the display path**
(TFT_eSPI has no C6 support). Ceiling: single-core (the LP core can't render).

> `docs/perf-notes.md` and `docs/research-notes.md` are the **pre-80 MHz research phase** (40 MHz, Arduino_GFX,
> async ElegantOTA) — historical, kept for the "why we tried X" trail. **This doc + the code are current truth.**

## Data flow
`claude.ai → proxy (Docker, proxy/) → device app_data (≈30 s poll) → app_view (1 Hz) → ui_set_*`.
The device can't reach claude.ai directly (Cloudflare); the proxy holds the session key and serves usage
JSON on the LAN. Contract & setup: [`proxy/README.md`](../proxy/README.md).

## Build targets
- **Device:** `pio run -d firmware` (PlatformIO, pioarduino fork). Per-board: one env per board, extending a
  shared base (see [`boards/README.md`](../boards/README.md)).
- **Simulator:** `pio run -d experiments/sim` (native/gcc) → PNG screenshots.
