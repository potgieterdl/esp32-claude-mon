# ADR 0006: Device calls Anthropic usage API directly (on-device OAuth)
- **Status:** Accepted
- **Date:** 2026-06-05
- **Supersedes** [ADR-0001](0001-self-hosted-proxy.md)

## Context
ADR-0001 stood on one premise: *the ESP32-C6 can't get past Cloudflare, so a LAN proxy must do the talking.*
That premise conflated two different hosts. **claude.ai** does serve a Cloudflare browser challenge the ESP
genuinely can't clear. But the **usage data lives on `api.anthropic.com`** — and that host accepts the C6's
mbedTLS handshake cleanly: proven on real hardware (HTTP 200 + usage JSON, ~1 s, ~55 KB peak heap). The
Cloudflare wall was never on the host we actually need. That distinction is what reverses ADR-0001 — the
always-on proxy, Docker host, and LAN dependency exist to solve a problem the device doesn't have.

## Decision
Drop the proxy. The device talks to Anthropic directly:
- **Usage:** `GET api.anthropic.com/api/oauth/usage` with a Claude OAuth access token (`Bearer`) plus
  `anthropic-beta: oauth-2025-04-20` and `User-Agent: claude-code/x`.
- **Self-refresh:** near expiry / on 401, the device `POST`s `platform.claude.com/v1/oauth/token`
  (`grant_type=refresh_token`, public PKCE `client_id`, no secret) and persists the **rotating** refresh
  token to LittleFS.
- **TLS pinned:** verified against bundled root CAs in `firmware/src/anthropic_ca.h` (GTS Root R4 for
  `api.anthropic.com`, ISRG Root X1 for `platform.claude.com`).
- **Provisioning:** a root-level `claude_token_sync.js` runs a **dedicated** `claude auth login` into a
  separate `~/.claude-device` config dir, giving the device its **own** refresh-token family. Hard-won:
  copying the user's main token meant the device's refresh-rotation invalidated the user's everyday Claude
  Code login. A dedicated login family fixes it.
- **Isolation:** all Anthropic-specific calls/parsing live in `firmware/src/app_data.cpp` behind the stable
  `ui_set_*` contract — the same shielding role ADR-0001 gave `proxy/claude.js`.

## Consequences
- **+** No always-on proxy host, no Docker, no LAN dependency — the device is self-contained and self-refreshing.
- **+** Anthropic-endpoint fragility stays isolated in `app_data.cpp` behind `ui_set_*`; the UI is shielded if the endpoints shift.
- **−** A long-lived OAuth **refresh token now sits in the device's unencrypted LittleFS** — accepted risk, mitigated by it being a dedicated, individually-revocable login and by pinned TLS.
- **−** The device depends on **undocumented Anthropic OAuth endpoints** (the same fragility ADR-0001 isolated in `proxy/claude.js`, now in `app_data.cpp`).
- **Tried, rejected:** `claude setup-token` — it lacks the `user:profile` scope the usage endpoint requires, so its token can't read usage. Hence the dedicated `claude auth login` flow above.
