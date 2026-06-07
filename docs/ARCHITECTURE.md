# Architecture

How the project is structured and the few decisions that must not be undone. Hardware facts live per-board
in [`boards/`](../boards/README.md); the *why* behind key choices lives in [`adr/`](../adr/README.md);
this doc is the app/software view.

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
| `main.cpp` | Hardware init (LovyanGFX display, CST816T touch, LVGL buffers), main loop, backlight + dim-on-idle; shake→bot easter-egg lifecycle (#31: summon/dismiss + 15 s auto-hide) |
| `app_settings` | device `config.json` (LittleFS) — `settings()` accessor; defaults injected from the repo-root `config.json` |
| `app_net` | WiFi (non-blocking; `setSleep(false)`; loop-driven backoff reconnect) |
| `app_web` | HTTP server: `/` status · `/config.json` settings · `/status` usage JSON · `/notify` "needs input" alert · `/update` OTA (ElegantOTA) |
| `app_data` | Calls `api.anthropic.com/api/oauth/usage` directly (CA-pinned HTTPS, OAuth bearer) → cached usage snapshot (stale = age-based); refreshes its own OAuth token on-device |
| `app_notify` | "Needs input" alert state (issue #2): set/cleared by `app_web`'s `/notify` endpoint (a Claude Code hook POSTs it), read by `app_view`. Pure state — exposes the alert's age; the presenter enforces the safety timeout |
| `app_view` | 1 Hz presenter: device state → `ui_set_*`; usage-chime FSM; reset-drain trigger; idle "sleep mode" nav (drifts to Clock + sleeping bot after `sleep_after_s` idle+untouched, wakes to Session on the activity edge); raises the "input needed" banner + chime and enforces its safety timeout |
| `app_time` | NTP + PCF85063 RTC; timezone from settings |
| `app_audio` | ES8311 chimes on a dedicated FreeRTOS task (never block the LVGL loop) |
| `app_imu` | QMI8658 IMU (`0x6B`) — **poll-based** shake detection (the IMU INT isn't broken out); a reversal-counting detector with hysteresis + debounce. Fires the shake-to-summon bot easter egg (#31) |
| `app_diag` | Dev-time serial diagnostics: boot banner (reset reason + I2C bus scan + `[imu]`) + ~10 s health line (heap, RSSI, WiFi drops, shake count, data age); USB-serial only, silent headless (`if(Serial)`) |

## Render path (the smoothness win — **do not revert**)
**LovyanGFX async GDMA** + **double partial buffers** (overlaps SPI transfer with CPU render — the only
parallelism a single-core C6 has) + **SPI 80 MHz**. 80 MHz is safe because the bus is **write-only**
(`miso = -1`), so the GPIO-matrix ~40 MHz cap — a *MISO-read* constraint — doesn't apply. `rgb_order = true`.

History: an early **Arduino_ESP32SPIDMA** attempt crashed (C6 lib bug: spi host = -1 → abort, looked like
"DMA is bad") → the fix was LovyanGFX's *working* DMA. **Arduino_GFX / TFT_eSPI are not the display path**
(TFT_eSPI has no C6 support). Ceiling: single-core (the LP core can't render).

> `docs/perf-notes.md` and `docs/research-notes.md` are the **pre-80 MHz research phase** (40 MHz, Arduino_GFX,
> async ElegantOTA) — historical, kept for the "why we tried X" trail. **This doc + the code are current truth.**

## Claude-bot art (#6 sleeper + #31 shake easter egg)
The robot mascot lives in `ui/` (`bot_draw`, rounded-rect/circle shapes only — one character, scaled for
both the corner sleeper and the big easter-egg bot). Two LVGL constraints shaped it (see
[ADR-0007](../adr/0007-bot-render-constraints.md)): the software renderer's **transform-scale path hangs**
in our partial-render config, so every bot animation is **transform-free** (translate / height / opacity);
and the richer art needed the LVGL heap pool raised to **`LV_MEM_SIZE` 96 KB** (the bot renders *over* the
live screen — opacity doesn't let LVGL skip it; the bigger pool is the real fix). The easter-egg bot draws on
an **opaque** full-screen stage (a clean backdrop) and is **built on show / destroyed on hide**, so it costs
zero pool memory while away.

## Data flow
`api.anthropic.com → device app_data (≈30 s poll, HTTPS + OAuth, on-device token refresh) → app_view (1 Hz) → ui_set_*`.
The device calls Anthropic's usage API **directly** — no proxy. TLS is verified against bundled root CAs
(GTS Root R4 + ISRG Root X1, `firmware/src/anthropic_ca.h`); the OAuth token is refreshed on-device via
`platform.claude.com` (rotating refresh token persisted to LittleFS). One-time setup pushes the first token
with `claude_token_sync.js` (repo root). Why direct-not-proxy: [ADR-0006](../adr/0006-device-direct-oauth.md)
(supersedes [ADR-0001](../adr/0001-self-hosted-proxy.md)).

## Build targets
- **Device:** `pio run -d firmware` (PlatformIO, pioarduino fork). Per-board: one env per board, extending a
  shared base (see [`boards/README.md`](../boards/README.md)).
- **Simulator:** `pio run -d experiments/sim` (native/gcc) → PNG screenshots.
