# Multi-board architecture — spec & direction

> **Status:** Proposed (POC validated). This is the design spec the multi-board **epic** points to. It
> captures the POC's lessons, the chosen direction, and the target architecture with reasoning. Formal
> decisions become **ADRs** during implementation (the epic Todo lists them). Single source of truth for the
> *why*; hardware facts stay in `boards/<arch>/<slug>/SPEC.md`, the app/software view in `docs/ARCHITECTURE.md`.

## 1. Goal

Support a family of small Waveshare displays (≈1.5″–2.4″, LCD **and** QSPI-AMOLED, landscape / wide / square /
round) as a desk monitor, from **one portable UI** and a **per-board adapter** — DRY/SOLID and pluggable, yet
allowing a board to carry genuinely custom complexity (a round panel may need circular rim text + a bespoke
layout). No regression to the shipped ESP32-C6 reference board.

## 2. What the POC proved (and deliberately shortcut)

A proof-of-concept on `test/multi-board-poc` took two hard boards (S3 1.8″ 368×448 square, S3 1.91″ 536×240
wide) end-to-end through the simulator + a full firmware compile.

**Validated:**
- A runtime **`ui_config_t` + `ui_config_for(w,h,shape)`** makes the portable UI resolution-aware; one codebase
  rendered three correct layouts (landscape/wide/portrait) with a derived font tier. The portable `ui/`
  boundary held (no hardware deps).
- **C6 zero-regression is achievable and measurable** — sim PNGs for weekly/clock/device came back
  **byte-identical (MD5)** to the committed `screenshots/`.
- **LovyanGFX 1.2.21 already ships `Panel_CO5300` / `Panel_RM67162` / `Panel_RM690B0`** — AMOLED needs **no
  library change**, just a QSPI bus config + `0x51` brightness. Major de-risk.
- A **compile-time board adapter** (`firmware/include/board.h` selector + per-board headers + shared-`[env]`
  `platformio.ini`) compiles cleanly for all three envs (`esp32-c6`, `esp32-s3-amoled-191`,
  `esp32-s3-touch-amoled-18`) with OPI-PSRAM + 16 MB partitions.

**Deliberately shortcut in the POC (must be addressed for production):**
- Layout dispatch is a **`switch(g_cfg.aspect)` inside every screen builder** — an Open/Closed smell (a new
  board/aspect edits every screen) with cols/stack duplication.
- `ui_set_*` setters **poke specific global widget handles** and hold the reset-drain/idle/countdown logic, so
  a custom (round) screen would have to re-implement that logic.
- The board HAL is **macro-thin and display-only**: no power-sequencing hook, no capability consumption; the
  1.8″ PMIC/expander control plane is stubbed (`LCD_RST=-1`), touch disabled, 1.91″ pins are placeholders.
- Fonts for **all** tiers are compiled into **every** board; the sim profile list is hardcoded, not derived.

## 3. Decisions & direction

| # | Decision | Rationale |
|---|---|---|
| D1 | **Adopt the full target architecture** (tokens + composition blocks + screen registry + view-model + formal board HAL) as the epic's foundation, phased so the **C6 stays byte-identical** at each step. | The POC's `switch(aspect)` won't scale to ~10 boards incl. round; pay the refactor once, up front. |
| D2 | **Bump LVGL 9.3.0 → 9.4+** (re-validate C6 byte-identical in sim + a clean device build before committing). | 9.4+ ships the native **`lv_arclabel`** curved-text widget — the clean way to do round-panel rim text. Avoids hand-rolled per-glyph trig. |
| D3 | **Keep the single render-loop model on all boards for now** (no dual-core). | Our UI is low-refresh; dual-core's win is *jitter isolation*, not throughput. Defer it (+ the required LVGL mutex) until a board actually shows swipe jank. Documented as a future option below. |
| D4 | **Per-board differences confined to the adapter + `boards/`**, never `ui/`; compile-time board selection (one board per build), no runtime vtable on the hot path. | Preserves the portable boundary; idiomatic for embedded. |

## 4. Target architecture

Two pillars (HAL + UI) plus the build/sim/test scaffolding.

### Pillar A — Board HAL (`firmware/`)

Formalize the adapter contract the portable app depends on. Keep the `board.h`-selected, per-board-header
pattern from the POC; grow it to a real contract:

| Contract | C6 (LCD/SPI) | AMOLED (QSPI) | Notes |
|---|---|---|---|
| `power_preinit()` | no-op | **I²C → AXP2101 rails → TCA9554 panel/touch resets** | **New, critical.** Runs *before* `display_init()`; PMIC/expander boards (1.8/2.06/2.41) blank-screen without it. Uses XPowersLib. |
| `display_init()` | `Panel_ST7789` + `Bus_SPI` 80 MHz | `Panel_AMOLED`-derived (CO5300/RM67162/RM690B0) over QSPI `Bus_SPI` (pin_io0..3) | Let the `Panel_*` class own window/offset/even-alignment quirks (the "x must be even" rule is **per-controller**, not universal). |
| `flush(area, px)` | async GDMA | async GDMA, partial-rect | Same model; AMOLED supports partial updates. |
| `set_brightness(0..255)` | LEDC PWM pin | **MIPI `0x51`** (`tft.setBrightness`) | Abstract the *mechanism*, not just the value. |
| `touch_read()` | CST816 | FT3168 / CST816S / CST9217 / FT6336 (per board) | I²C; gated by a capability so non-touch SKUs (1.91″) build clean. |
| `battery()` | ADC + BAT_EN | via AXP2101 (I²C) | Optional capability. |
| buffer policy | 2 partial buffers in **internal DMA SRAM** | same (SRAM partial) | **Do not DMA from PSRAM** on S3 (descriptor/bandwidth limits). PSRAM = optional "canvas + small SRAM bounce" for future image screens only. |
| capabilities | from `board.yml` | from `board.yml` | Drives UI feature gating (§Pillar B). |

- **Render loop:** single-loop on all boards now (D3). Future dual-core (S3): pin LVGL/flush to core 1, WiFi/TLS
  to core 0, **add an `lv_lock` mutex** around cross-task UI access — captured as a deferred ADR, not built yet.
- **TE/tearing:** `0x35` off by default; optional flush-gate hook only if tearing appears.

### Pillar B — UI layout system (`ui/`)

Replace `switch(aspect)` with **tokens + blocks + a registry + a view-model**:

1. **Design tokens** — a geometry-scaled `ui_tokens_t` (spacing/radius/ring-stroke/ring-size/pad + the existing
   font tier + a color palette), derived once in `ui_config_for`, fed into a handful of **shared `lv_style_t`**
   (LVGL-native theming). Kills the magic-number duplication; builders express intent, not pixels.
2. **Composition blocks** — `ring_stat`, `detail_column`, `topbar`, `devrow`, `page_dots` as **family-blind**
   components taking tokens (formalizing the POC's `build_session_detail`). Prefer flex/grid containers over
   absolute `lv_obj_align` so "arrangement" is a container property.
3. **Screen registry** — `UI_SCREENS[screen][family] → { build, bind }` (function pointers; no RTTI). The
   **family** (COLS / STACK / ROUND) is derived from geometry; a board overrides one screen by setting **one
   table entry**. Adding a board/family **adds a row, never edits a screen** (fixes the OCP smell). `#if` appears
   only at the *registration/compile* layer: the **device** compiles only its families; the **simulator
   compiles all** so it renders every profile.
4. **View-model behind `ui_set_*`** (non-breaking) — a retained `ui_model_t` + per-screen `bind()`. The
   reset-drain / idle / ceil-minute logic centralizes; a custom round Session reuses 100% of the data path and
   only re-renders. Presenter (`app_view.cpp`) is untouched.
5. **Capability gating** — `board.yml` `capabilities` → a `ui_caps_t` in the UI context, so e.g. a `battery:false`
   board drops the BATTERY row instead of showing `-`, and a non-touch SKU adjusts nav affordances.

### Pillar C — build / sim / test

- **Per-board font gating** — declare `fonts: [...]` in `board.yml`; `load_config.py` emits the
  `-DLV_FONT_MONTSERRAT_*` set per env so a board only pays flash for the sizes its tier uses.
- **Sim profiles from `board.yml`** — generate the simulator's `PROFILES[]` (and device geometry) from the
  board metadata (one source of truth), so adding a board is "add a folder," not "edit the sim."
- **Round in the sim** — add a round profile + a **circular mask** in the PNG dump so reviewers see true
  round-panel truth (LVGL has no built-in round sim).
- **Regression** — commit reference PNGs per profile/screen and **snapshot-diff in CI**; **unit-test the pure
  `ui_config_for()` / token math** for boundary geometries so a new tier threshold can't silently move the C6.

## 5. Round / squircle displays

The hardest and most custom case (1.75″ round 466×466, 2.06″ squircle 410×502):
- **Curved rim text via `lv_arclabel`** (LVGL 9.4+, D2) for "CLAUDE USAGE" (static) + "RESETS hh:mm" (live);
  bake purely-decorative arcs to `lv_image` if needed.
- **Safe area = inscribed square (0.707·D)**; radial insets; never corner-anchor. The usage ring doubles as the
  rim frame.
- **Per-screen round treatment:** Session ring + Clock stay parametric-round (center in the inscribed square);
  **Weekly's 7-day bar row → radial "petals"** around the rim; **Device's key/value list → paginated centered
  cards** (a left-justified list is the canonical round-screen failure). ⇒ ~2 of 4 screens parametric, 2
  bespoke-for-round — registered as `FAM_ROUND` overrides; everything else inherited.

## 6. Board family & support matrix

Grouped by effort. SPI-LCD reuses the entire C6 render path; AMOLED adds the QSPI/brightness/power deltas.

| Board | MCU | Driver / Res | Shape | Touch | New control plane |
|---|---|---|---|---|---|
| **C6 1.69** (ref) | C6 | ST7789V2 240×280 | landscape | CST816T | — (shipped) |
| S3 1.69 LCD | S3R8 | ST7789V2 240×280 | landscape | CST816T | OPI PSRAM build only |
| S3 2.0 LCD | S3R8 | ST7789T3 240×320 | landscape | CST816D | — |
| S3 1.64 AMOLED | S3R8 | CO5300 280×456 | tall rect | FT3168 | QSPI + 0x51; pins unpublished |
| S3 1.91 AMOLED | S3R8 | RM67162 536×240 | wide | FT3168 *(non-touch SKU)* | QSPI + 0x51; display-only path |
| S3 1.8 AMOLED | S3R8 | CO5300 368×448 | square | FT3168/CST816S | QSPI + 0x51 + **AXP2101 + TCA9554** |
| S3 1.75 AMOLED | S3R8 | CO5300 466×466 | **round** | CST9217 | QSPI + 0x51 + AXP2101 |
| S3 2.06 AMOLED | S3R8 | CO5300 410×502 | **squircle** | FT3168 | QSPI + 0x51 + **AXP2101 (gates panel/touch power)** |
| S3 2.41 AMOLED | S3R8 | RM690B0 600×450 | landscape | FT6336 | QSPI + 0x51 + TCA9554; **quad** PSRAM |

⚠ Pin maps for 1.64 / 1.91 / 2.06 are **not published in text** — must be read from each board's schematic PDF
before flashing. PMIC/expander pin→function mappings need schematic confirmation too.

## 7. What else to consider

- **ADRs** (write during implementation): *Multi-display UI — tokens + registry + view-model* (supersedes the
  aspect-switch POC); *Multi-board HAL — power-preinit + brightness mechanism + buffer/loop policy*; *LVGL 9.4
  bump*. Per CLAUDE.md these are key, hard-to-reverse decisions.
- **Per-board known-good bins + versioning** — `firmware/releases/` and `FW_VERSION`/tags become per-board
  (`fw-vX.Y.Z-<slug>`) once >1 board ships.
- **UI contract versioning** — `ui_set_*` + `ui_model_t` is shared by firmware + sim; add a compile-time guard
  so they can't drift.
- **Docs** — keep hardware facts in `boards/<slug>/SPEC.md`, the software view in `docs/ARCHITECTURE.md`; this
  spec is the *why* for the multi-board move; no duplication.
- **Secrets/OTA** unchanged per board (basic-auth device token; ElegantOTA inactive-slot); confirm 16 MB OTA
  partition gives two app slots.

## 8. Phased migration (C6 byte-identical at every step)

0. **Tokens** — extract magic numbers → `ui_tokens_t` + shared styles. (Removes most duplication; C6 unchanged.)
1. **Blocks** — promote `topbar/ring_stat/detail_column/devrow/page_dots` to family-blind components.
2. **View-model** — `ui_model_t` + `bind()`; rewrite `ui_set_*` as "write model + notify" (presenter untouched).
3. **Registry + families** — replace `switch(aspect)` with `UI_SCREENS[screen][family]`; derive family in
   `ui_config_for`; C6 resolves to COLS with the original constants → byte-identical.
4. **Board HAL** — formalize the contract; add `power_preinit()`, brightness-mechanism, capability flow; wire
   the SPI-LCD S3 boards (lowest effort) end-to-end.
5. **Build/sim gating** — per-board fonts from `board.yml`; generate sim `PROFILES[]`; snapshot CI; unit tests.
6. **LVGL 9.4 bump** — re-validate C6; gain `lv_arclabel`.
7. **AMOLED boards** — rectangular first (1.91 display-only, then 1.8 with AXP2101/TCA9554 power-preinit + touch).
8. **Round/squircle** — `FAM_ROUND` screens (`lv_arclabel` rim text, radial weekly, paginated device); round
   sim profile + mask. 1.75″ round, then 2.06″ squircle.

## 9. Open risks / to verify

- LVGL 9.3→9.4 API deltas (re-validate C6 byte-identical + clean device build before committing the bump).
- AMOLED `setAddrWindow` even-x/width alignment is **per-controller** — confirm on CO5300 first flash.
- AXP2101 + TCA9554 bring-up order and pin→function mapping per board (schematic).
- Unpublished pin maps (1.64 / 1.91 / 2.06) — schematic PDFs.
- DMA-from-PSRAM is **not** safe on S3 — keep flush buffers in internal SRAM.

## 10. References

POC branch `test/multi-board-poc`. Research syntheses informing this spec: ESP-IDF external-RAM/LCD guides &
`esp_lvgl_port` (render/PSRAM/dual-core), LovyanGFX `Panel_AMOLED`/`Panel_RM690B0` + disc. #663 (QSPI), Waveshare
`waveshareteam` BSPs (PMIC/expander order), LVGL `lv_arclabel` docs + watch-face projects (fbiego, LilyGO,
ESP-Brookesia) for round UI, and ESPHome/ZMK/QMK/ESP-Brookesia for the registry/tokens patterns.
