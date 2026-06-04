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
everything: `wifi`, `proxy` (url/token/session_key/poll), and `device` settings.
- **Device build** reads it via a PlatformIO pre-build script (`firmware/load_config.py`) that injects
  `CFG_*` compile-time defines; `app_settings` uses them as its seed defaults (placeholder `#ifndef`
  fallbacks keep it compiling with no config.json, e.g. CI).
- **Proxy** reads the same file's `proxy` section (env vars still override); Docker mounts `../config.json`.
- The device also serves its **live** settings at `PUT /config.json` (runtime tweaks without reflash).

## Consequences
- **+** One file to edit; same schema feeds both the device and the proxy; gitignored, so secrets never commit.
- **+** `config.example.json` documents the full schema for newcomers.
- **−** Credentials are compiled into `firmware.bin` — acceptable here: the binary is built locally, flashed
  over USB, and gitignored (`firmware/releases/*.bin`). We explicitly chose *not* to do runtime Wi-Fi
  provisioning (captive portal) — see the trade-off; revisit if the device is ever distributed pre-built.
- **−** The proxy needs `config.json` reachable on its host (Docker mounts it from one level up).
