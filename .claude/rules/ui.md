---
description: Portable LVGL UI (shared by firmware + desktop simulator)
paths:
  - ui/**
---
# UI rules (ui/)

- `ui/` is **portable LVGL only** — no Arduino or hardware headers. It must compile for BOTH the device
  firmware and the desktop simulator. If you reach for a hardware API here, the boundary is wrong.
- **Preview every change in the simulator first** (`experiments/sim` → PNGs) before flashing. Rule:
  **sim = layout + data correctness; a physical photo = panel truth** (rgb_order / row-offset / contrast).
- Live data enters via the `ui_set_*` setters; the presenter (`firmware/src/app_view.cpp`) drives them at 1 Hz.
- Show honest state: blank to `--` when offline/no-data (`ui_clear_usage`), never stale mock numbers.
