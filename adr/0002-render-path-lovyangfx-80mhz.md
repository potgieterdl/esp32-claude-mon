# ADR 0002: Render path — LovyanGFX async-DMA + 80 MHz write-only SPI
- **Status:** Accepted
- **Date:** 2026-06-04

## Context
Goal: smooth swipes and animations on a **single-core ESP32-C6 with no PSRAM**. The journey:
- **Arduino_GFX + HWSPI** (blocking flush) — the CPU stalls waiting on each SPI push; swipes stutter.
- **Arduino_ESP32SPIDMA** — crashed on the C6 (library bug: SPI host resolved to `-1` → abort). This
  *looked* like "DMA is bad" but was a library defect, not a DMA problem.
- **TFT_eSPI** — has no ESP32-C6 support on Arduino-ESP32 3.x / IDF 5.5 (typed register structs break its macros).
- SCK/MOSI are on GPIO1/2, routed through the **GPIO matrix**, whose ~40 MHz cap is a *MISO-read* constraint.

## Decision
Use **LovyanGFX** with **async GDMA + double partial buffers** (overlaps the SPI transfer with CPU render —
the only parallelism a single-core part has), at **SPI 80 MHz**. 80 MHz is safe because the bus is
**write-only** (`miso = -1`), so the GPIO-matrix read cap doesn't apply. `rgb_order = true` for correct colors.

## Consequences
- **+** Visibly smoother swipes/animations; frees RAM vs a full-screen buffer.
- **+** Turnkey (LovyanGFX handles the async DMA); `esp_lcd` would also work but was more plumbing.
- **−** Tied to LovyanGFX config in `main.cpp`. The ceiling is the single core (the LP core can't render) —
  further gains only come from cutting redraw area, not faster SPI.
- **DO NOT** revert to Arduino_GFX / TFT_eSPI / Arduino_ESP32SPIDMA — they are not the display path here.
