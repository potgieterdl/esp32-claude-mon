---
description: Node.js claude.ai usage proxy (Docker, runs on a server/Pi)
paths:
  - proxy/**
---
# Proxy rules (proxy/)

- **Zero npm dependencies** — built-in `fetch` + `node:http` only. Don't add packages.
- **Only `claude.js` talks to claude.ai** (unofficial, reverse-engineered endpoints). Isolate all
  claude.ai-specific parsing there; keep the `/usage` JSON contract in `server.js` stable for the device.
- Tier/plan note: read `rate_limit_tier` off the org object (not the generic `billing_type`) — see todo F12.
- **Secrets** (session key, `PROXY_TOKEN`) come from the repo-root `config.json` (`proxy` section); env vars override. Never commit them.
- Full architecture, setup, and JSON contract: [`proxy/README.md`](../../proxy/README.md).
