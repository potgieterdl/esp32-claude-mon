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
- Keep the portable boundary: hardware glue lives in `firmware/src/`; **never** add Arduino/hardware deps to `ui/`.
