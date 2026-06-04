# ESP32-C6 Claude Monitor

Desk status display on a **Waveshare ESP32-C6-Touch-LCD-1.69**: shows my Claude usage limits + next reset,
with swipe between screens (clock, device status; later weather). LVGL UI, live data from a self-hosted proxy.

> **Starting a new task in a fresh context? Run the [`orient`](.claude/skills/orient/SKILL.md) skill FIRST.**
> It reads the canonical docs + folder rules + the subsystem's source, traces how that piece connects to the
> rest, and reports a structured understanding before any edit — the "understand before you act" loop. Skip
> only for trivial one-line lookups.

> **Read the docs in this order (single source of truth each — don't duplicate):**
> 1. **[`README.md`](README.md)** FIRST — what the app is, its delivered **Features**, hardware, setup.
> 2. **[`todo.md`](todo.md)** — the roadmap (only what's left to build).
> 3. Then whatever's relevant: **[`adr/`](adr/README.md)** (why key decisions were made) ·
>    **[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)** (portable core ↔ device adapter, render path, modules) ·
>    **hardware** → [`boards/<arch>/<slug>/SPEC.md`](boards/README.md) · **proxy** → [`proxy/README.md`](proxy/README.md) ·
>    **rollback / build history** → [`firmware/releases/README.md`](firmware/releases/README.md).
> Folder-scoped rules auto-load from [`.claude/rules/`](.claude/rules) when you edit `firmware/`, `ui/`, or `proxy/`
> (incl. the **release checklist** that moves shipped features todo→README and pushes `main`).

> **Before any non-obvious or architectural decision, CHECK [`adr/`](adr/README.md) FIRST.** Follow the
> existing ADRs — don't relitigate or silently contradict a settled choice; if one genuinely needs to change,
> supersede it **explicitly** (new ADR marked "Supersedes ADR-NNNN", old one updated to "Superseded by").
> When you make a NEW key architectural choice — or a non-obvious "tried X, chose Y" not evident from the code —
> **write an ADR** for it (template in [`adr/README.md`](adr/README.md)). Key decisions only; not routine features/fixes.

## Repo layout
- `ui/` — **portable LVGL UI** (`ui.cpp`/`ui.h`), shared by firmware + simulator. **Edit UI here.**
- `firmware/` — device PlatformIO project: hardware glue (`src/`), config (`include/`), `releases/` (known-good bins).
- `experiments/sim/` — desktop simulator (native/gcc) → PNG screenshots, no hardware needed.
- `boards/` — per-device hardware specs (one folder per board; scales to more devices).
- `docs/` — `ARCHITECTURE.md`, schematic PDF, demo bundle; `perf-notes`/`research-notes` are historical.
- `proxy/` — Dockerized claude.ai→usage-JSON proxy. `vendor/` — upstream Waveshare refs (pull, don't edit).

## Workflow
- **Pull upstream first** when starting: `git -C vendor/ESP32-C6-Touch-LCD-1.69 pull`.
- **Edit UI in `ui/`** → preview in the simulator → only then flash. The `ui/` module is portable (no Arduino).
- **Validate via the LOCAL sim, not on-device screenshots** (we tried; the device flush only has the logical
  LVGL buffer the sim already renders, and it costs ~134 KB RAM). **Sim = layout + data; physical photo = panel truth.**

## Multi-agent (use where it helps)
Delegate to subagents (the Agent tool) to keep the main context lean and to parallelize — it has paid off here:
- **Research fan-out:** parallel agents for independent topics (per-subsystem / per-library); each returns a synthesis.
- **Parallel feature dev:** one agent per independent module; integrate serially (single device → flashing is serialized).
- **`claude-code-guide`** agent for Claude Code questions (hooks, settings, SDK, memory/rules).
- Reserve direct work for single-file edits / quick lookups; delegate broad, file-spanning investigations.

## Build & flash (PlatformIO, project in `firmware/`)
```powershell
$env:PYTHONIOENCODING='utf-8'   # avoid a cosmetic UnicodeEncodeError on Windows
pio run -d firmware                 # build
pio run -d firmware -t upload       # build + flash over USB (auto-detects COM port)
```
- Uses the **pioarduino** platform fork (`55.03.38-1`) — required for ESP32-C6. Board/pin facts → the board SPEC.
- **If `Could not open COMx`:** replug the USB-C cable (native USB drifts after resets), then retry — or use OTA ↓.
- **Serial (HWCDC) is unreliable on the C6** — confirm "is it running?" via the **screen/backlight**, not `Serial`.

### Flash over WiFi (OTA) — Claude can run this end-to-end, no USB
When USB drifts (or you just don't want a cable), flash the running device over the LAN via ElegantOTA.
Prereq: device booted + on WiFi (find its IP on the Device screen or `http://<device-ip>/`).
```powershell
$ip="<device-ip>"; $tok="<proxy.token from config.json>"
$bin="firmware/.pio/build/esp32-c6/firmware.bin"          # the app image — NOT firmware.factory.bin
$md5=(Get-FileHash $bin -Algorithm MD5).Hash.ToLower()
curl.exe -s -u "admin:$tok" "http://$ip/ota/start?mode=fr&hash=$md5"          # -> OK (HTTP 200)
curl.exe -s -u "admin:$tok" -F "file=@$bin;type=application/octet-stream" "http://$ip/ota/upload"  # -> OK; verifies MD5 + reboots
curl.exe -s "http://$ip/"   # verify: page shows "Firmware: <new version>"
```
- `/update` is the ElegantOTA browser page; `/ota/start`+`/ota/upload` are the API it calls (what the curl above uses).
- Auth is basic `admin` / `PROXY_TOKEN`. OTA writes the *inactive* slot and only boots it if the MD5 checks out,
  so a bad upload can't brick the device. The web server can drop the **first** request (curl `000`) — just retry.
- **Self-test over WiFi too:** `GET/PUT http://<ip>/config.json` exercises settings; `/` reports firmware/heap.

## Versioning & rollback (IMPORTANT)
- **Always bump `FW_VERSION`** in `firmware/include/app_config.h` before each feature build (shows on the Device
  screen + `/`). Keep it in lockstep with the `fw-vX.Y.Z` git tag and `firmware/releases/<name>.bin`.
- **One feature per flash.** Integrate + flash a single change; the user verifies on the device before the next.
- **Keep known-good builds.** After a confirmed-good flash, archive `.pio/build/esp32-c6/firmware.factory.bin`
  → `firmware/releases/<name>.bin` and `git tag` the commit. Rollback = flash that `.bin` at 0x0.
  Latest good: `fw-v1.7.0-good`.

## Config & settings
**One file: the repo-root `config.json`** (gitignored; `config.example.json` is the template) holds
everything — `wifi`, `proxy` (url/token/session_key/poll), `device` (poll/tz/thresholds/display).
- **Device build:** `firmware/load_config.py` (pre-build) reads `../config.json` → `CFG_*` compile defines →
  `app_settings` seed defaults. (Placeholder fallbacks let it compile with no config.json.)
- **Proxy:** reads the same file's `proxy` section (env vars override). Docker mounts `../config.json`.
- **Live device settings:** the device also serves `GET/PUT http://<ip>/config.json` (auth `admin`/token) to
  change runtime settings (brightness, dim, thresholds…) without reflashing — seeded from the build defaults.

## Secrets & publishing (CHECK before testing or pushing)
Real secrets live ONLY in the gitignored `config.json`; `config.example.json` is the template a new user fills in.
**Checked in on purpose** (help contributors): `.claude/rules/*.md`, `.claude/settings.json`, `config.example.json`.
- **On a fresh clone:** `cp config.example.json config.json` and fill it in (else the device build uses
  placeholder Wi-Fi and won't connect).
- **Before any `git push` / making the repo public, verify nothing secret is tracked:**
  ```powershell
  git ls-files | Select-String '^config\.json$|settings\.local|/\.env$|session_key|releases/.*\.bin$'   # expect: NONE
  git grep -niE 'sk-ant-sid01-[a-z0-9]{6}' -- .                                                          # expect: NONE
  ```
  Both must come back empty. Built images embed compiled creds, so `firmware/releases/*.bin` stay gitignored.
  `.claude/settings.local.json` holds a private env id — gitignored; never commit it.

> **What's done / what's next:** delivered features → [`README.md`](README.md#features); roadmap → [`todo.md`](todo.md).
> On every version ship, follow the release checklist in [`.claude/rules/release.md`](.claude/rules/release.md)
> (move the feature todo→README, add an ADR if it was a key decision, archive/tag, push `main`).
