# Roadmap — what's left to build

> **Delivered features → [README.md](README.md#features)** · why we decided things → [`adr/`](adr/README.md) ·
> rollback / build history → [`firmware/releases/README.md`](firmware/releases/README.md).
>
> Kept lean on purpose: when a feature ships in a new firmware version it moves from here into the README's
> Features list (see [`.claude/rules/release.md`](.claude/rules/release.md)). This file is only *what's next*.

## Next
- **Plan-tier badge** — show the real plan (Max 5 / Max 20x / Pro). The device currently hardcodes the badge;
  the proxy needs to read **`rate_limit_tier`** off the org object (not the generic `billing_type`) and select
  the org by capability, then the device wires `data_plan()` → a new `ui_set_plan()` label map. Needs a proxy
  change + redeploy. *(researched — see proxy notes)*
- **"Needs input" alerts** — light the device up when a Claude Code session is waiting on you. Proxy gains
  `/alert` + `/clear`; Claude Code hooks (`Notification` → alert, `UserPromptSubmit` → clear) drive it; the
  device shows an "⚠ INPUT NEEDED" banner + project + chime. Lowest-latency path is **SSE push**, not polling.
- **Weather** — current conditions on the Clock screen (location + API key via `config.json`).
- **OTA rollback validation** — `esp_ota_mark_app_valid_cancel_rollback()` after a healthy boot, so a bad OTA
  auto-reverts instead of needing a USB reflash.

## Polish backlog
- Weekly reset-day text from the proxy epoch (currently a static label).
- Idle behaviour: default to the Clock screen when there's no Claude activity.
- mDNS hostname (`claude-monitor.local`) so you don't have to chase the DHCP IP.
- Audio mute / volume in `config.json`.
- Battery-% ADC calibration (needs a LiPo connected to tune the curve).
- Proxy: a tiny admin page to refresh the session key without editing the file.
- Simulator: an all-alert-states montage + a watch-rebuild helper.
