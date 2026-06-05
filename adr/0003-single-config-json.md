# ADR 0003: One root `config.json` for all secrets + settings
- **Status:** Accepted
- **Date:** 2026-06-04

## Context
Configuration started split across **three files in two formats**: `firmware/include/wifi_config.h`
(device build defines), `proxy/.env` (token), and `proxy/session_key` (claude.ai cookie). A new user had
to find and edit all three, and "where's my Wi-Fi password actually stored?" was a recurring question.
An embedded device has no runtime env, so build-time values are compiled in regardless of file format —
a `.env` would not have changed the security posture.

## Decision
**One gitignored root `config.json`** (with `config.example.json` as the committed template) holds
everything: `wifi`, `device` settings, and `oauth` (Claude token/refresh-token, provisioned by
`claude_token_sync.js` — see [ADR-0006](0006-device-direct-oauth.md)).
- **Device build** reads it via a PlatformIO pre-build script (`firmware/load_config.py`) that injects
  `CFG_*` compile-time defines; `app_settings` uses them as its seed defaults (placeholder `#ifndef`
  fallbacks keep it compiling with no config.json, e.g. CI).
- The device also serves its **live** settings at `PUT /config.json` (runtime tweaks without reflash).

> The original `proxy` section (url/token/session_key/poll) was removed when [ADR-0006](0006-device-direct-oauth.md)
> dropped the proxy; the device now talks to Anthropic directly with an on-device OAuth token under `oauth`.

## Consequences
- **+** One file to edit; gitignored, so secrets never commit.
- **+** `config.example.json` documents the full schema for newcomers.
- **−** Credentials are compiled into `firmware.bin` — acceptable here: the binary is built locally, flashed
  over USB, and gitignored (`firmware/releases/*.bin`). We explicitly chose *not* to do runtime Wi-Fi
  provisioning (captive portal) — see the trade-off; revisit if the device is ever distributed pre-built.
