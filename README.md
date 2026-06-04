# ESP32-C6 Claude Monitor

A desk monitor for Claude usage on a **Waveshare ESP32-C6-Touch-LCD-1.69**. Shows your Claude
subscription limits (5-hour + weekly windows with live reset countdowns), a clock, and device
status on swipeable LVGL screens — with soft audio alerts, runtime settings, and WiFi/USB updates.

Usage data comes from a small self-hosted **proxy** (Docker) that reads your claude.ai usage and
serves it to the device over your LAN (the ESP32 can't reach claude.ai directly — Cloudflare blocks
its TLS fingerprint).

## Features
- **Live Claude usage** — 5-hour and weekly limit rings with live reset countdowns, pulled from the proxy.
- **Swipeable screens** — **Session** (5h ring + countdown + weekly bar) · **Weekly** (ring + 7-day bars) ·
  **Clock** (time/date + next reset) · **Device** (wifi/ip/proxy/battery/heap/firmware). Boots to the clock,
  then slides to Session once connected.
- **Honest display** — keeps the last reading through brief WiFi blips, blanks to `--` when truly offline
  (never fake numbers), and the ring **drains to zero** when a usage window resets.
- **WiFi** — auto-(re)connects in the background to reach the proxy; survives drops without nuking the screen.
- **Smooth UI on a single core** — fluid swipes/animations via **LovyanGFX async-DMA + double buffering at
  80 MHz SPI**, getting the most out of the single-core ESP32-C6.
- **One-file config** — WiFi, proxy, alert thresholds, timezone, brightness and dim-on-idle all in
  `config.json`; tweak the device's settings live over the LAN, no reflash.
- **Audio alerts** — soft chimes at usage thresholds (ES8311 codec).
- **Real clock** — NTP + on-board RTC, timezone-aware, with live reset countdowns.
- **Wireless + USB updates** — flash over WiFi (OTA) or cable.
- **Desktop simulator** — preview the UI as PNGs with no hardware.

## Hardware
**Waveshare ESP32-C6-Touch-LCD-1.69** — ESP32-C6 (RISC-V, WiFi 6 / BLE) · 1.69″ ST7789V2 240×280 IPS ·
CST816T touch · ES8311 audio · PCF85063 RTC. Canonical pinout, quirks & flashing →
[`boards/esp32c6/esp32-c6-touch-lcd-1.69/SPEC.md`](boards/esp32c6/esp32-c6-touch-lcd-1.69/SPEC.md).
The repo is structured so [more boards](boards/README.md) can be added (portable UI + per-device adapter).

## First-time setup
**One config file for everything:** `cp config.example.json config.json` and fill it in (WiFi, proxy
URL/token, claude.ai session key, device settings). That single file feeds **both** the device build
and the proxy — it's gitignored, so your secrets never get committed.
1. **Config** — copy + fill `config.json` (above).
2. **Proxy** — stand up the Docker proxy on a server/Pi (*Proxy setup* below); it reads `config.json`'s `proxy` section.
3. **Flash** — build + flash the device over USB (first time), then OTA over WiFi after that.

## Build & flash
Needs [PlatformIO](https://platformio.org/). Uses the **pioarduino** platform fork (required for ESP32-C6).
The first build downloads the toolchain + libraries from the PlatformIO registry (slow, one-time); later
source-only builds are incremental. **No manual library setup** — `docs/demo/` and `vendor/` are optional
Waveshare reference material (gitignored), not needed to build.

**Over USB**
```powershell
$env:PYTHONIOENCODING='utf-8'        # avoid a cosmetic Windows console error
pio run -d firmware -t upload         # build + flash (auto-detects COM port)
```
If you hit **`Could not open COMx`**, replug the USB-C cable (the C6's native USB port drifts after
resets) and retry. If it keeps fighting you, use OTA instead ↓.

**Over WiFi (OTA)** — no cable; the recommended path once the device is on the network.
Build first, then upload `firmware/.pio/build/esp32-c6/firmware.bin` (the app image, **not** the
`*.factory.bin`) to **`http://<device-ip>/update`** in a browser (user `admin`, pass = `PROXY_TOKEN`).
OTA writes the *inactive* app slot and only boots it if the MD5 checks out, so a bad upload can't
brick the device. To script it with `curl`, see [`CLAUDE.md`](CLAUDE.md) → *Flash over WiFi (OTA)*.

**Rollback:** flash a known-good image from `firmware/releases/` — see
[`firmware/releases/README.md`](firmware/releases/README.md). **Always bump `FW_VERSION`** in
`firmware/include/app_config.h` before a build (shows on the Device screen + `/` page; matches the git tag).

> **Render path:** **LovyanGFX** (async GDMA SPI @ 80 MHz, double partial buffers) + **LVGL v9** —
> *not* TFT_eSPI / Arduino_GFX (incompatible or slower on the single-core C6).

## Live device settings (`/config.json` endpoint)
Beyond the root `config.json` you edit at setup, the **device** also serves its *live* settings on the LAN —
change brightness, thresholds, etc. **without reflashing**. (The device seeds these from the build-time
defaults baked in from your root `config.json`'s `device` section.) Auth: basic `admin` / `PROXY_TOKEN`.
```bash
curl -u admin:$TOKEN http://<device-ip>/config.json                          # read current settings
curl -u admin:$TOKEN -X PUT -H "Content-Type: application/json" \
     -d '{"display":{"brightness":30,"dim_on_idle":true,"dim_after_s":60}}' \
     http://<device-ip>/config.json                                          # merge + persist
```
Configurable: **WiFi**, **proxy** url/token, **poll_seconds**, alert **thresholds** (warn/max %),
**timezone** (POSIX TZ), and **display** (brightness, dim-on-idle + timeout). Brightness applies live;
others on next use. A bad value is clamped; a malformed body returns `400` and never overwrites the file.

## Web endpoints (port 80)
| Path | Auth | Purpose |
|---|---|---|
| `/` | none | status page — firmware, wifi/ip, uptime, free heap |
| `/config.json` | `admin`/token | GET/PUT runtime settings |
| `/update` | `admin`/token | OTA firmware upload (ElegantOTA) |

## UI simulator (preview without the device)
Renders the shared `ui/` module to PNG on the desktop — check layout/data before flashing.
```powershell
# gcc required once:  scoop install gcc
$env:PATH = "$env:USERPROFILE\scoop\apps\gcc\current\bin;$env:PATH"
pio run -d experiments/sim
.\experiments\sim\.pio\build\sim\program.exe .\experiments\sim\out   # -> out/01..04_*.png (+ offline & reset-drain previews)
```
Set `PROXY_URL`/`PROXY_TOKEN` env to render **live** proxy data into the PNGs (else it uses mock data).

## Proxy setup (Docker — e.g. a Raspberry Pi)
The proxy holds your claude.ai **session key** and serves usage JSON on the LAN. It reads the `proxy`
section of the **same `config.json`** (mounted into the container), so there's nothing extra to configure.
```bash
# copy the repo (or at least config.json + proxy/) to the server, then:
cd ~/esp32-c6-claude-monitor/proxy                 # proxy/ sits next to the root config.json
docker compose up -d --build                       # mounts ../config.json; restart:unless-stopped
curl -H "Authorization: Bearer <PROXY_TOKEN>" http://localhost:7890/usage   # verify (token from config.json)
```
Make sure `config.json`'s `proxy.token` matches what you flashed to the device, and `proxy.session_key`
holds your claude.ai cookie. **Refresh an expired key:** edit `config.json` + `docker compose restart`.
Details & JSON contract: [`proxy/README.md`](proxy/README.md).

## Secrets & contributing
**All secrets live in one gitignored file** — copy the template and fill it in:

| Copy this template | → to (gitignored) | Holds |
|---|---|---|
| `config.example.json` | `config.json` | **everything**: WiFi SSID/pass, proxy URL/token, claude.ai session key, device settings |

The device build reads it via a pre-build script (`firmware/load_config.py` → compile-time defines); the proxy
reads its `proxy` section. Env vars still override on the proxy if you want. Shared Claude Code config **is**
checked in on purpose (`.claude/rules/`, `.claude/settings.json`) so contributors inherit the conventions.
Built firmware images embed compiled-in creds, so `firmware/releases/*.bin` are gitignored. Before pushing,
verify no secrets are tracked — see [`CLAUDE.md`](CLAUDE.md) → *Secrets & publishing*.

## Repo layout
| Path | What |
|---|---|
| `ui/` | Portable LVGL UI (shared by firmware **and** simulator) — **edit screens here** |
| `firmware/` | Device PlatformIO project (`src/` glue, `include/` config, `releases/` known-good bins) |
| `experiments/sim/` | Desktop simulator → renders the UI to PNG, no hardware needed |
| `boards/` | Per-device hardware specs (one folder per board; scales to more devices) |
| `proxy/` | Dockerized claude.ai → usage-JSON proxy (runs on a server/Pi) |
| `adr/` | Architecture Decision Records — the *why* behind key choices |
| `docs/` | `ARCHITECTURE.md`, schematic PDF, design mockups (`perf-notes`/`research-notes` are historical) |
| `vendor/` | Upstream Waveshare + lcars-esp32 reference repos (gitignored) |
| `CLAUDE.md` · `todo.md` | Dev workflow · roadmap (what's next) |

## More
Hardware spec → **[`boards/…/SPEC.md`](boards/esp32c6/esp32-c6-touch-lcd-1.69/SPEC.md)** ·
architecture → **[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)** · decisions → **[`adr/`](adr/README.md)** ·
roadmap → **[`todo.md`](todo.md)** · dev workflow & flashing recipes → **[`CLAUDE.md`](CLAUDE.md)**.
