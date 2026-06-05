# Firmware release archive

Known-good firmware images, kept so we can flash back **instantly** when a change misbehaves
(no rebuild needed). Archive a `.factory.bin` here only **after it's confirmed working on the device**.

## Roll back to a known-good build
Full image flashed at offset 0x0:
```powershell
python -m esptool --chip esp32c6 --port COM3 write_flash 0x0 firmware/releases/<file>.bin
```
(Port busy/missing? Replug. For download mode: hold BOOT, tap RESET, release BOOT.)

## How a build gets archived
After a confirmed-good flash:
```powershell
Copy-Item firmware/.pio/build/esp32-c6/firmware.factory.bin firmware/releases/<name>.bin
git add firmware/releases/<name>.bin ; git commit ; git tag fw-<name>
```

> **Note:** these `.bin`s embed compiled-in Wi-Fi credentials, so they're **gitignored** (kept on
> disk only, not committed). Git **tags** mark the matching source commits.

## Builds (newest first)
| File | Tag | Contents | Status |
|------|-----|----------|--------|
| `v1.8.0.bin` | `v1.8.0` / `fw-v1.8.0-good` | **Direct-to-Anthropic (proxy removed).** Device calls `api.anthropic.com/api/oauth/usage` directly over CA-pinned HTTPS (bundled GTS R4 + ISRG X1) and refreshes its own OAuth token via `platform.claude.com` (rotating token persisted to LittleFS). Token provisioned by root `claude_token_sync.js` (dedicated `claude auth login`). mDNS `claude-monitor.local`; new `/status` endpoint; reusable `ui_modal`/`ui_toast` notifications (token-needed / token-received); Device screen API + token rows. Config schema → `wifi`/`device`/`oauth` (`device.token`). **Flashed via OTA, full fresh-setup flow confirmed on device.** See [ADR-0006](../../adr/0006-device-direct-oauth.md). | ✅ confirmed on device |
| `v1.7.0.bin` | `v1.7.0` / `fw-v1.7.0-good` | **Single `config.json` + registry deps + docs.** All secrets/settings in one gitignored root `config.json` (build script injects them; proxy reads the same file). lvgl + SensorLib from the PlatformIO registry (builds on a fresh clone; `docs/demo` optional). Per-device `boards/` scaffold, lean CLAUDE.md, `.claude/rules`. **Full clean build, flashed via OTA**, device confirmed on 1.7.0. | ✅ confirmed on device |
| `v1.6.1.bin` | `v1.6.1` / `fw-v1.6.1-good` | **Settings + connection hardening + reset animation.** `config.json` on LittleFS (GET/PUT `/config.json`: wifi/proxy/poll/thresholds/tz/brightness/dim-on-idle; compiled `wifi_config.h` fallback). WiFi: `setSleep(false)` + loop-driven backoff reconnect + IP cross-task race fix. Keep-last-known **grace** on drop (~90s, no instant blank). Ring **drains to 0** on a window reset (3s). Flashed via **OTA over WiFi** (USB had drifted). | ✅ confirmed on device (config.json round-trip tested) |
| `v1.5.1.bin` | `v1.5.1` / `fw-v1.5.1-good` | **Connection-aware UI.** Honest display: Session/Weekly blank to "--" when offline/no-data (no stale mock numbers); boots on the Clock screen, smooth one-time slide to Session once connected with live data. Clock keeps the time across WiFi drops (RTC). Fixes: frozen weekly-% label (was mock 41%) + off-centre ring %. Supersedes the un-archived v1.5.0. | ✅ confirmed on device |
| `v1.4.2.bin` | `v1.4.2` / `fw-v1.4.2-good` | **Boot splash** (coral ring sweep + version, shown only on version change via NVS) + presenter refactor (`app_view`). Static text + 1s settle + ring-only 3s sweep → fade to UI. | ✅ confirmed |
| `v1.3.0.bin` | `v1.3.0` / `fw-v1.3.0-good` | **Smoothness final**: presenter refactor (`app_view`) + **SPI 80 MHz** (write-only bus). Smoother swipe, no glitches. | ✅ confirmed |
| `v1.1.0.bin` | `v1.1.0` / `fw-v1.1.0-good` | **LovyanGFX async-DMA render** — noticeably smoother swipe (double-buffered DMA, overlaps render+SPI); `rgb_order=true` for correct colors. | ✅ confirmed |
| `v1.0.0.bin` | `v1.0.0` / `fw-v1.0.0-good` | **v1.0 — first full release.** v0.6.0 + render smoothness tuning (opaque tiles, tighter cadence; marginal — blocking-flush ceiling). All core features live. | ✅ |
| `v0.6.0-ota.bin` | `fw-v0.6.0-good` | v0.5.0 + **OTA (F8)** via ElegantOTA at `/update` (basic-auth admin/token) + `FW_VERSION` wired to Device screen. | ✅ confirmed (auth 401/200) |
| `v0.5.0-time.bin` | `fw-v0.5.0-good` | v0.4.0 + **time sync (F6)**: NTP + PCF85063 RTC, real Clock screen + live reset countdowns (Ireland TZ). | ✅ confirmed |
| `v0.4.0-status.bin` | `fw-v0.4.0-good` | v0.3.0 + clearer proxy status: blue "no data" dot state + a Device-screen **PROXY** row (connected/no data/offline). | ✅ confirmed |
| `v0.3.0-data.bin` | `fw-v0.3.0-good` | v0.2.0 + **Device screen real data (F7)** + Wi-Fi creds + **live Claude data** via the Pi proxy. | ✅ confirmed: Wi-Fi green, live 5h/weekly % |
| `v0.2.0-audio.bin` | `fw-v0.2.0-good` | v0.1.0 + **audio (F5)** enabled (ES8311 soft chimes, boot chime). HWSPI display. | ✅ confirmed on device (responsive + chime) |
| `v0.1.0-ui-net-data.bin` | `fw-v0.1.0-good` | LVGL UI (4 screens, deep-black theme, PWM backlight) + WiFi/web scaffold + Claude data client. **Display: Arduino_HWSPI** (single buffer). Audio OFF; DMA-perf reverted. | ✅ working & responsive on device |
