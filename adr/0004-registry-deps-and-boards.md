# ADR 0004: Registry dependencies + per-device `boards/` scaffold
- **Status:** Accepted
- **Date:** 2026-06-04

## Context
Two problems surfaced when preparing the repo to be shared and to (eventually) support more than one board:
- The firmware/sim builds **symlinked `lvgl` + `SensorLib` out of `docs/demo/`** — the gitignored Waveshare
  demo bundle. A fresh clone got the source but **couldn't build**, and the symlink paths even leaked a
  local username.
- Hardware facts (pinout, quirks) were **duplicated** across CLAUDE.md and a reference doc, with no
  structure for adding a second device.

## Decision
- Pull **`lvgl` and `SensorLib` from the PlatformIO registry** at the exact versions Waveshare ships
  (9.3.0 / 0.3.1), like the other deps. `docs/demo/` + `vendor/` become **optional reference**, not build inputs.
- Adopt a **per-device `boards/<arch>/<slug>/`** layout (`SPEC.md` = canonical hardware truth + `board.yml`
  metadata), with `boards/README.md` as the device index. The portable `ui/` never changes per board; only
  the `firmware/src/` adapter + the board folder do. (Patterned on ESPHome/ZMK/QMK/Meshtastic/Marlin.)

## Consequences
- **+** A fresh clone builds with **no manual library setup**.
- **+** Adding a board = a new `boards/<slug>/` + a PlatformIO env, **never** touching `ui/`.
- **+** Hardware facts have one home (the board SPEC); other docs link to it.
- **−** We pin exact registry versions instead of vendoring; a registry version going missing would need a re-pin.
