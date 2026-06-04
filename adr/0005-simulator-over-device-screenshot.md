# ADR 0005: Validate the UI in the simulator, not via on-device screenshots
- **Status:** Accepted
- **Date:** 2026-06-04

## Context
We wanted to verify UI changes without flashing the device every time. One attempt was an **on-device
screenshot** endpoint: capture the framebuffer and serve it as a BMP over HTTP. It disappointed on every axis:
it only captured the **logical LVGL buffer** (exactly what a desktop render already produces), cost
**~134 KB RAM** on a no-PSRAM C6 (a lot to give up permanently), and still **couldn't reveal panel-level
issues** (rgb_order, the row offset, contrast) because those happen in the physical panel, not the buffer.

## Decision
Keep the UI in a **portable `ui/` module** (LVGL only, no hardware) shared by the firmware **and a desktop
simulator** that renders each screen to PNG. Validate layout + data in the simulator; use a **physical
photo** for panel truth. The on-device screenshot was dropped.

## Consequences
- **+** Fast iteration with zero device RAM cost; UI changes are checked before flashing.
- **+** Clear rule: **sim = layout + data correctness; a physical photo = panel truth** (rgb_order/offset/contrast).
- **−** The simulator can't catch panel-level rendering issues — that still needs eyes on the real screen.
