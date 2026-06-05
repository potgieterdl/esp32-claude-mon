# Roadmap — what's left to build

> **Delivered features → [README.md](README.md#features)** · why we decided things → [`adr/`](adr/README.md) ·
> rollback / build history → [`firmware/releases/README.md`](firmware/releases/README.md).
>
> Kept lean on purpose: when a feature ships in a new firmware version it moves from here into the README's
> Features list (see [`.claude/rules/release.md`](.claude/rules/release.md)). This file is only *what's next*.

## Next
- **"Needs input" alerts** — light the device up when a Claude Code session is waiting on you. Claude Code
  hooks (`Notification` → alert, `UserPromptSubmit` → clear) need a path to the device: simplest is a small
  device endpoint the hook POSTs to; the device then shows an "⚠ INPUT NEEDED" banner + project + chime.
- **Weather** — current conditions on the Clock screen (location + API key via `config.json`).
- **OTA rollback validation** — `esp_ota_mark_app_valid_cancel_rollback()` after a healthy boot, so a bad OTA
  auto-reverts instead of needing a USB reflash.

## Polish backlog
- Weekly reset-day text from the usage epoch (currently a static label).
- Idle behaviour: default to the Clock screen when there's no Claude activity.
- Audio mute / volume in `config.json`.
- Battery-% ADC calibration (needs a LiPo connected to tune the curve).
- Simulator: an all-alert-states montage + a watch-rebuild helper.
