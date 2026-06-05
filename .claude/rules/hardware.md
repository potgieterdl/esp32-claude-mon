---
description: Hardware-specific firmware — pins, drivers, board quirks, flashing
paths:
  - firmware/**
  - boards/**
---
# Hardware rules (firmware/ + boards/)

- **Before any architectural change** (render path, build system, data flow, config model, etc.) **check
  [`adr/`](../../adr/README.md) first** — follow the existing decisions; supersede an ADR *explicitly* if one
  must change, and write a new ADR for a new key decision. Key decisions only, not routine features.
- **Canonical hardware truth = `boards/<arch>/<slug>/SPEC.md`** (pinout, offsets, clocks, quirks). If you
  change a pin, display offset, SPI clock, or board quirk in code, **update that SPEC.md in the same change.**
  Don't restate hardware facts in CLAUDE.md/README — link to the SPEC.
- **Verify on the SCREEN/backlight, not `Serial`** — HWCDC serial is unreliable on the C6.
- **Render path is fixed:** LovyanGFX async GDMA + double partial buffers + 80 MHz write-only SPI. Don't
  revert to Arduino_GFX/TFT_eSPI or change the clock without reading `docs/ARCHITECTURE.md#render-path`.
- **Flashing:** USB `pio run -d firmware -t upload`; if the COM port drifts (`Could not open COMx`), replug
  **or flash over WiFi (OTA)** — see CLAUDE.md → *Flash over WiFi*. Claude can run OTA itself.
- **Per-flash discipline:** bump `FW_VERSION` (app_config.h) before each build; one feature per flash;
  archive a known-good `.bin` + tag after the user confirms it on the device.
- **After a serial/USB flash, check the boot diagnostics (`app_diag`).** Read the serial output (e.g.
  `pio device monitor -e esp32-c6`, or open `COMx` at 115200) and confirm a **healthy boot** before calling
  the flash good: expected `[diag] boot:` reset reason, the `[diag] I2C scan:` lists all four devices
  (`0x15` touch · `0x18` audio · `0x51` RTC · `0x6B` IMU), heap is sane, and the `[diag]` health line shows
  no new error flood / leak / WiFi thrash. The screen confirms *"it's running"*; the serial diag confirms
  *"it's healthy"*. **When a change is worth verifying at boot** (a new I2C/SPI device, a subsystem init, a
  resource budget), **add a line to `app_diag`** so it's checked on every flash from then on.
- Keep the portable boundary: hardware glue lives in `firmware/src/`; **never** add Arduino/hardware deps to `ui/`.
