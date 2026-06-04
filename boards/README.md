# Supported devices

One self-contained folder per board, grouped by MCU architecture: `boards/<arch>/<slug>/`. Each holds the
**canonical** hardware truth for that board (`SPEC.md` + machine-readable `board.yml`). The portable UI
(`ui/`) is hardware-agnostic and never changes per board; only the device adapter (`firmware/src/` glue)
and this folder do. See [`docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md) for the portable-core ↔
device-adapter contract.

| Device | Arch | Status | Spec |
|---|---|---|---|
| Waveshare ESP32-C6-Touch-LCD-1.69 | `esp32c6` | ✅ supported | [SPEC](esp32c6/esp32-c6-touch-lcd-1.69/SPEC.md) |

## Adding a new device
1. Create `boards/<arch>/<slug>/` with `SPEC.md` (copy an existing one as the template) + `board.yml`.
2. Add a PlatformIO env in [`firmware/platformio.ini`](../firmware/platformio.ini) — extend a shared base
   section so common flags aren't duplicated; name the env the board slug.
3. Implement the device adapter in `firmware/src/` (display flush, touch, backlight, battery). **Do not edit
   `ui/`** — if you need to, the boundary is wrong.
4. Build + flash, confirm on device, archive a known-good `.bin`, and add a row to the table above.
