---
description: Node.js claude.ai usage proxy (Docker, runs on a server/Pi)
paths:
  - proxy/**
---
# Proxy rules (proxy/)

- **Zero npm dependencies** — built-in `fetch` + `node:http` only. Don't add packages.
- **Two auth paths, both isolated from `server.js`:** `claude.js` (legacy `sessionKey` cookie → claude.ai,
  behind Cloudflare) and `claude_oauth.js` (**preferred**: Claude Code OAuth token → `api.anthropic.com`,
  auto-refreshing). All unofficial/reverse-engineered upstream parsing lives in those two files; keep the
  `/usage` JSON contract in `server.js` stable for the device. `server.js` uses OAuth when a token is
  configured, else falls back to the session key.
- **OAuth token store (option b):** seeded from `config.json` `proxy.oauth_token`, then kept live (the
  rotating refresh token) in a writable store file (`OAUTH_STORE_FILE`, a Docker volume) — `config.json`
  is mounted read-only. `proxy/get_token.js` (local helper) seeds it and can hot-swap via `POST /token`.
  Refresh tokens **rotate** — always persist the new one.
- Tier/plan note: the OAuth path derives plan from the credential's `rateLimitTier`. For the session path,
  read `rate_limit_tier` off the org object (not the generic `billing_type`) — see todo F12.
- **Secrets** (`oauth_token`, session key, `PROXY_TOKEN`) come from the repo-root `config.json` (`proxy` section); env vars override. Never commit them.
- Full architecture, setup, and JSON contract: [`proxy/README.md`](../../proxy/README.md).
