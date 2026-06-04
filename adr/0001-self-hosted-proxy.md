# ADR 0001: Self-hosted proxy for claude.ai usage data
- **Status:** Accepted
- **Date:** 2026-06-04

## Context
The device needs the user's Claude usage (5-hour + weekly limits). Two hard constraints:
- claude.ai has **no official usage API**; the web app reads an unofficial endpoint authenticated by a
  browser **session cookie**.
- The ESP32-C6 **can't call claude.ai directly** — Cloudflare blocks its TLS fingerprint, and baking the
  session key into firmware would be insecure and still wouldn't get past the bot check.

## Decision
Run a small **self-hosted proxy** (Node, zero npm deps, Docker) on the LAN. It holds the session key,
polls claude.ai with browser-like headers on a gentle interval, caches the result, and serves a clean
**bearer-token JSON contract** to the device. All claude.ai-specific, reverse-engineered logic is isolated
in `proxy/claude.js`; `server.js` + the device contract stay stable if Anthropic changes the web app.

## Consequences
- **+** Device stays simple and secret-light (no session key, no Cloudflare/TLS problem); polls a clean LAN endpoint.
- **+** One place (`claude.js`) to adapt when claude.ai shifts; the contract shields the firmware.
- **−** Requires an always-on host (e.g. a Raspberry Pi).
- **−** The session key **expires** and must be refreshed (edit `config.json` + restart) — an accepted manual chore.
