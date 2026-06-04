# ESP32-C6 Claude Monitor — Build Plan

Features (large items) → Stories (capabilities) → tasks (owned/expanded by the feature's agent).
Architecture: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). Hardware: [boards/…/SPEC.md](boards/esp32c6/esp32-c6-touch-lcd-1.69/SPEC.md).
Historical research: [docs/research-notes.md](docs/research-notes.md), [docs/perf-notes.md](docs/perf-notes.md).

Legend: `[ ]` todo · `[~]` in progress · `[x]` done · ⚑ = touches shared file (coordinate).

---

## Phase 0 — Foundation (do first; unblocks the rest)

### F0. Core scaffold & modular structure ⚑
- [x] Init git repo at project root + .gitignore (vendor/, docs/demo/, .pio/, secrets) — commit d7b13c9
- [~] Modular `main.cpp` integrator with `init()` hooks — `app_net`, `app_web` done; add `audio/`, `data/`, `ota/`, `screenshot/`, `sys/` as features land (keep `ui/` portable)
- [x] One shared **built-in `WebServer`** (lighter than Async; OTA+screenshot attach via `web_server()`)
- [x] Settings plumbing (`config.json` on LittleFS) shared by features — **done v1.6.0** (`app_settings`,
      GET/PUT `/config.json`, defaults injected from the repo-root `config.json`)

### F1. Display quality & theme  ✅ quick win (in progress this session)
- [x] Backlight PWM dimming (LEDC GPIO6, ~50%)
- [x] True-black background + richer/saturated palette
- [x] Expose brightness as a setting (config.json) + optional dim-on-idle — **done v1.6.0** (live brightness,
      dim-on-idle toggle + timeout, wake on touch)
- [ ] (optional) gamma/VCOM fine-tune on device

### F3. Connectivity (WiFi + settings)
- [x] Secrets in one gitignored root `config.json` (+ `config.example.json` template) — wifi/proxy/session-key/device.
      Firmware injects them at build via `load_config.py`; proxy reads the same file. (Was `wifi_config.h` + `.env`.)
- [x] `/config.json` on LittleFS + ArduinoJson v7 — **done v1.6.0**; precedence is compiled-default → config.json → live PUT
      (auth `admin`/token; clamped + 400 on bad body)
- [x] Event-driven connect + auto-reconnect (`app_net`, `net_online()`)
- [x] Feed status to top-bar dot + Device screen (`ui_set_online`) — live

---

## Phase 1 — Parallel features (after Phase 0)

### F2. Rendering performance
Findings (researched): already at HWSPI **40 MHz** (GFX default) — and SCK/MOSI on GPIO1/2 route
through the GPIO matrix, so ~40 MHz is the reliable ceiling. `Arduino_ESP32SPIDMA` is a dead end on
C6 (lib bug: SPI host `-1` → abort = the faded/unresponsive crash; and it's blocking-poll anyway).
Double-buffering is pointless with a blocking flush on one core.
- [x] Reverted DMA → **Arduino_HWSPI + single buffer** (known-good)
- [x] Smoothness tuning: opaque tiles + `LV_DEF_REFR_PERIOD 10` + loop `delay(1)` (marginal effect)
- [x] **Smoothness achieved (v1.1.0→v1.3.0):** swapped to **LovyanGFX async GDMA + double buffer**
      (overlaps SPI transfer with render — the one parallelism a single-core C6 has) **+ 80 MHz SPI**
      (write-only bus dodges the GPIO-matrix read-cap). Confirmed smoother on device. esp_lcd would also
      work but LovyanGFX was turnkey. Single-core (LP core can't render) is the ceiling.
- [ ] Further headroom only via cutting redraw area (cheaper swipe transition / dirty-rect) — optional.

### F4. Claude data integration  ← core value
**Server (Docker proxy):**
- [x] Dockerized proxy built (`proxy/`): session key → usage JSON, browser headers, bearer auth, 60s cache — ⚠ untested against live claude.ai (needs session key)
- [x] Refresh session key without rebuild (`proxy/session_key` + restart); `.env` secrets
- [x] JSON contract defined + documented (`proxy/README.md`)
**Device:**
- [x] HTTP client polling proxy + ArduinoJson parse (`app_data`), non-blocking
- [x] Wire data → Session/Weekly rings/labels + top-bar status dot (`ui_set_*`)
- [x] Offline/stale states — **v1.5.x connection-aware UI**: dot colors + blanks Session/Weekly figures to
      "--" when offline/no-data (honest display, `ui_clear_usage`); boots on the Clock screen and does a
      one-time smooth slide to Session once connected with live data (`ui_goto_anim`). Clock keeps showing
      the time whenever it's valid (RTC survives WiFi drops); "CONNECTING"/"SYNCING" only when no time yet.
      Fixes: weekly-% label was frozen at mock 41% (discarded handle); ring % drifted off-centre on
      content change (now fixed-width centred box).
- [ ] Stretch: "concurrent tasks"/extra session info (endpoint doesn't expose it)

### F5. Audio notifications
- [x] ES8311 + I2S init (`app_audio`, vendored es8311 lib), soft volume + idle mute
- [x] Non-blocking chime task w/ attack/decay envelope (no clicks)
- [x] Triggers: ≥70% soft 2-note, 100%/reset gentle note (debounced, hysteresis) — wired in main loop
- [x] Verified on device (boot chime audible + soft, no regression)
- [ ] Mute/volume in config.json (after F3 LittleFS)

### F6. Clock, time & weather
- [x] RTC PCF85063 read + NTP sync + timezone (Ireland) — `app_time.*`
- [x] Clock screen real time/date + live Session/Weekly reset countdowns
- [ ] Weather API (config: location/key) → icon + temp on Clock screen
- [ ] Idle behaviour: default to Clock when no Claude activity

### F7. Device/status screen (real data)  ✅ (done v0.4.0; reconciled)
- [x] Real WiFi SSID/IP/RSSI, free heap (`app_view`) — uptime shown on web `/`, not the Device screen
- [x] Battery via BAT_ADC (GPIO0) gated by BAT_EN (GPIO15); voltage → % (needs a LiPo to calibrate the curve)
- [x] Firmware version string (`FW_VERSION`)

### F8. OTA updates
- [x] 8MB OTA partitions (`default_8MB.csv`) + `flash_size=8MB`
- [x] ElegantOTA v3 (sync WebServer) at `/update`, basic-auth admin/token — verified 401/200
- [ ] Optional: rollback mark-valid after healthy boot; mDNS hostname

### F9. Remote screenshot (debug tool)
- [ ] RGB565 shadow buffer filled in `disp_flush` (alloc before WiFi), `#ifdef ENABLE_SCREENSHOT` ⚑(main.cpp disp_flush)
- [ ] `/screenshot.bmp` endpoint (plain HTTP, chunked, bottom-up) ⚑(web server)
- [ ] Document `curl` usage; debug env only

### F10. Simulator / tooling (nice-to-have)
- [ ] Sim: render alert states (70% / 100%) to preview alert visuals
- [ ] **Sim: pull LIVE data from the proxy if reachable (else mock) → real-data PNG previews** — replaces the dropped on-device screenshot (F9): same value (real data on screen) without the 134KB device RAM cost
- [~] F9 on-device screenshot — **DROPPED**: only captured the logical buffer (= what the sim already shows), cost 134KB RAM, didn't validate panel output. Use sim+photos instead.
- [ ] Sim: all-states montage + watch-rebuild helper
- [ ] Keep CLAUDE.md / docs current

### Boot splash ✅ (done, v1.4.x)
- [x] Version-gated splash: coral ring sweep 0→100 + version + "CLAUDE MONITOR"; shown only on version
      change (NVS `fwseen`), fades into UI. Optimised: static text + 1s settle + ring-only sweep.
      `ui/ui.cpp` `ui_build_splash` + `firmware/src/main.cpp`.

### F11. "Needs input" alerts (Claude Code hooks)  ← idea
Light up the device when a Claude Code session is waiting for you. Hooks (set once per machine in
`~/.claude/settings.json`, cover all projects; payload has `session_id`+`cwd`):
- [ ] Proxy: `/alert` + `/clear` endpoints; track attention by session_id (+ project from `cwd`); expose in `/usage`
- [ ] Hooks: `Notification` (blocked/permission) → `/alert`; `UserPromptSubmit` → `/clear`; optional `Stop` (turn-end)
- [ ] Device: "⚠ INPUT NEEDED" banner + project + chime; clears on response; multi-session → count + latest project
- [ ] Trigger granularity choice: blocked-only vs also turn-end (Notification vs +Stop)

### F12. Polish backlog
- [~] Plan badge (currently hardcoded "MAX 20X"). **Researched (v1.5.0):** the tier is in **`rate_limit_tier`**
      on the org object (`default_claude_max_5x` / `_max_20x` / `_pro`), NOT `billing_type` (returns generic
      "stripe_subscription"). Fix = proxy reads `rate_limit_tier` + selects the org by `capabilities`
      (chat/claude_*, not index 0; you likely have an extra API-only org), then device wires
      `data_plan()`→ new `ui_set_plan()` with a label map. **Needs a Pi redeploy. NEXT after v1.5.0.**
- [ ] Weekly reset-day text from proxy epoch (currently static)
- [ ] Brightness as a setting + optional dim-on-idle; battery ADC calibration (needs LiPo)
- [ ] mDNS hostname (`claude-monitor.local`); audio mute/volume in config; "default to Clock when idle"
- [ ] Proxy: tiny admin page to refresh the session key without SSH

---

## Parallelization & agent ownership
- **One agent owns one Feature**: it (1) reviews/validates the stories, (2) writes a small task list, (3) implements story-by-story, (4) validates — UI via the simulator (`experiments/sim`), hardware via flash.
- **Order:** Phase 0 first (single owner / main thread) — establishes git baseline + modular `init()` hooks + shared web server + config + WiFi + the display win. Then Phase 1 features run in parallel.
- **Conflict control:** features marked ⚑ touch shared files (`main.cpp`, `platformio.ini`, `lv_conf.h`, web server). After git init, run parallel agents in **git worktrees** (isolation) and merge, OR partition so only one agent edits a given shared file at a time. Module dirs (`audio/`, `data/`, etc.) are conflict-free.
- **Dependencies:** F4-device, F6, F7, F8, F9 depend on F3 (WiFi) + F0 (web server). F2 independent. F1 done.
