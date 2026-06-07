# ADR 0007: Claude-bot rendering — transform-free, opaque stage, larger LVGL pool
- **Status:** Accepted
- **Date:** 2026-06-07

## Context
The shake-to-summon bot (#31) and the idle sleeper (#6) share one shape-built character (`ui/ui.cpp`
`bot_draw`) animated with LVGL v9 on the single-core, **PSRAM-less** ESP32-C6 (partial render, `LV_DRAW_SW`,
no GPU/matrix). Three things bit us, each found in the desktop simulator before any flash (per ADR-0005):

1. **LVGL's software transform-scale path infinite-loops here.** Animating `transform_scale_*` to squash the
   eyes/mouth (the obvious "blink"/"pop" approach the research suggested) hangs inside the renderer every
   time, in both sim and the identical device LVGL config. Setting only `scale_y` is *worse* (`scale_x`
   defaults to 0 → degenerate), but even valid non-uniform scale hangs.
2. **A failed `lv_malloc` is configured to hang, not crash.** `lv_conf.h` sets `LV_ASSERT_HANDLER while(1);`
   with `LV_USE_LOG 0`, so an out-of-memory assertion is a *silent freeze*. The richer bot art (~23 shapes)
   exhausted the 64 KB LVGL pool while the live screen behind was *also* still rendering its big glyphs,
   tripping this — a device-bricking hang.
3. The screen behind the overlay keeps rendering **regardless of the overlay's opacity** — VERIFIED: even a
   fully opaque full-screen stage still OOMs at 64 KB on a clock glyph, because LVGL's cover-check does NOT
   skip the (scrollable) tileview underneath. So opacity is a *look* choice, not a memory fix; the pool size
   is the fix. (An early guess that "go opaque → LVGL skips the screen → stay at 64 KB" was disproven in the sim.)

## Decision
- **No `transform_scale` anywhere in the bot.** Every animation is transform-free: **translate** (spring-in,
  bob, slide-off), **height + re-center** (eye blink/wake), **opacity** (chest pulse, stage fade). These need
  no big composited layer and stay smooth.
- **Draw the easter-egg bot on a full-screen OPAQUE stage** — a clean black backdrop for the bot. (This is a
  *look* choice; it does **not** save memory — the screen behind still renders, see Context #3.) Parent it to
  the **active screen**, not `lv_layer_top`, so it sits *below* functional modals/toasts (those stay on the
  top layer and must win) while still floating above the tileview.
- **Build the bot on show, destroy it on hide** — keeps the bot's ~23 objects out of the pool while away
  (zero idle footprint), so the +32 KB below covers only the transient easter-egg peak, not steady state.
- **Raise `LV_MEM_SIZE` 64 KB → 96 KB** — the actual memory fix. The UI legitimately grew (full-body sleeper +
  easter-egg bot rendered over the live screen); 64 KB OOMs the peak (verified in the sim). Verified on-device:
  RAM ~49 % of 320 KB, free heap still ample for WiFi/TLS (the LVGL pool is separate from the ESP heap).

## Consequences
- (+) The bot is smooth and **can't hang the device** on a memory spike during the easter egg.
- (+) Sim-first caught all three issues; the `while(1)` assert is why a bot bug shows as a *freeze* — if the
  UI ever hangs on a new screen, suspect an LVGL OOM and check `LV_MEM_SIZE`/object count first.
- (−) +32 KB static RAM for the pool (one-time). It buys headroom against the `while(1)` OOM-hang; given that
  failure mode, the margin is the point — don't shave it to the bare minimum.
- (+) The opaque stage reads as a clean "stage" for the bot AND keeps the easter egg off `lv_layer_top`, so a
  real alert (modal/toast) still surfaces above it.
- **Do NOT** reintroduce `transform_scale` for bot animation, route functional alerts through the bot (keep
  them on the cheap modal/toast primitive), or drop `LV_MEM_SIZE` back to 64 KB — the easter egg OOM-hangs
  there (re-check in the simulator before changing it).
