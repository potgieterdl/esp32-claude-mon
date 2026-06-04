# claude-usage-proxy

A tiny self-hosted service that lets the **ESP32-C6 Claude monitor** show your
**claude.ai subscription** usage (5-hour rolling window, weekly window, plan).

The device can't query claude.ai directly: Cloudflare blocks the ESP32's TLS
fingerprint. So this proxy (running on a machine you control — a NAS, a home
server, a Pi) holds your claude.ai **session key**, polls claude.ai's usage
endpoint with browser-like headers, **caches** the result, and serves a clean
JSON contract on your LAN behind a **shared bearer token**. The device just
polls the proxy over Wi-Fi.

```
 ESP32-C6  ──Bearer token──▶  proxy (this)  ──sessionKey cookie──▶  claude.ai
   /usage  ◀── clean JSON ──   (caches 60s)  ◀── usage JSON ──
```

- Node 22, **zero npm dependencies** (built-in `fetch` + `http`).
- Caches upstream so many device polls never hit claude.ai.
- Session key is refreshable **without rebuilding** the image.

---

## 1. Get your claude.ai session key

1. Log in to <https://claude.ai> in a desktop browser.
2. Open **DevTools → Application (Chrome) / Storage (Firefox) → Cookies →
   `https://claude.ai`**.
3. Copy the value of the **`sessionKey`** cookie. It looks like
   `sk-ant-sid01-…` (a long string).
4. (Optional, faster) While in DevTools → **Network**, click around claude.ai
   and note the request `…/api/organizations/<UUID>/…`. That `<UUID>` is your
   `CLAUDE_ORG_ID` — set it to skip the org lookup.

> The session key **expires** (typically days–weeks, and on logout). When the
> device shows stale/offline and `/health` reports `auth rejected`, repeat this
> step — see **Refreshing the session key** below. No rebuild needed.

## 2. Configure

The proxy reads the **repo-root `config.json`** (the single project config — copy it from
`config.example.json` at the root and fill in the `proxy` section):
```jsonc
"proxy": {
  "url":   "http://<this-host-lan-ip>:7890/usage",  // device points here; not used by the proxy itself
  "token": "<long random string, e.g. openssl rand -hex 24>",   // must match what the device flashed
  "session_key": "sk-ant-sid01-...",                // the cookie from step 1
  "poll_seconds": 60
}
```
`config.json` is **gitignored**. Env vars (`PROXY_TOKEN`, `CLAUDE_SESSION_KEY`, `POLL_SECONDS`,
`CLAUDE_ORG_ID`) still override it if you'd rather not use the file.

## 3. Run

`docker compose` (in `proxy/`) mounts `../config.json` into the container:
```bash
cd proxy
docker compose up -d --build
docker compose logs -f          # watch it poll claude.ai

curl http://localhost:8080/health                                       # health (no token)
curl -H "Authorization: Bearer <your token>" http://localhost:8080/usage  # real endpoint
```
(Make sure the repo-root `config.json` sits one level up from `proxy/` on this host.)

Run without Docker (Node 22+) if you prefer (reads `../config.json`, or env vars):
```bash
node server.js
# or override: PROXY_TOKEN=... CLAUDE_SESSION_KEY=sk-ant-sid01-... node server.js
```

## 4. Refreshing the session key (no rebuild)

The key lives in `proxy/session_key`, bind-mounted into the container.

```bash
printf '%s' 'sk-ant-sid01-NEW-KEY' > session_key
docker compose restart            # picks up the new key (also re-read each poll)
```

`server.js` re-reads the file on every poll, so a restart isn't strictly
required — but it forces an immediate refresh.

---

## JSON contract (`GET /usage`)

Requires header `Authorization: Bearer <PROXY_TOKEN>`.

**200 OK** — fresh or cached data:

```json
{
  "plan": "max_20x",
  "five_hour":   { "used_pct": 68, "resets_at": 1769900000 },
  "weekly":      { "used_pct": 41, "resets_at": 1770300000 },
  "weekly_opus": { "used_pct": 22, "resets_at": 1770300000 },
  "fetched_at": 1769896400,
  "stale": false,
  "age_seconds": 12
}
```

| Field | Type | Meaning |
|---|---|---|
| `plan` | string | Subscription tier as reported by claude.ai (e.g. `max_20x`, `pro`, or `unknown`). |
| `five_hour.used_pct` | int 0–100 | Percent of the 5-hour rolling window used. |
| `five_hour.resets_at` | epoch secs \| null | When the 5-hour window resets (UTC). `null` if unknown. |
| `weekly.used_pct` | int 0–100 | Percent of the weekly (7-day) window used. |
| `weekly.resets_at` | epoch secs \| null | When the weekly window resets (UTC). |
| `weekly_opus` | object \| null | Same shape, Opus-specific weekly cap. `null` if the plan has none. |
| `fetched_at` | epoch secs | When the proxy last successfully fetched from claude.ai. |
| `stale` | bool | `true` if the cached data is older than 3× the poll interval. |
| `age_seconds` | int | Age of the cached data. |

**Error responses**

- `401 {"error":"unauthorized"}` — missing/wrong bearer token.
- `503 {"error":"<reason>","stale":true}` — no good data yet (just started, or
  session key invalid/expired). The device should keep showing its last value
  and flag offline/stale.

**`GET /health`** (no token) — for setup/monitoring:

```json
{ "ok": true, "has_cache": true, "last_error": null, "last_attempt": 1769896400, "poll_seconds": 60 }
```

---

## Sources & assumptions (the unofficial bit)

claude.ai's usage endpoint is **unofficial and undocumented**. Anthropic's
*documented* API (`/v1/organizations/usage_report/...`) is the token/cost
**Admin API** (needs `sk-ant-admin…`) — it does **not** expose the
subscription 5-hour/weekly *limit* windows the claude.ai web UI shows. So, like
the community menu-bar/tray monitors, this proxy uses the same internal endpoint
the web app itself calls:

1. `GET https://claude.ai/api/organizations` → resolve your org **UUID**.
2. `GET https://claude.ai/api/organizations/{uuid}/usage` → windows with
   `utilization` (%) and `resets_at` (ISO 8601), incl. `five_hour`,
   `seven_day`, and `seven_day_opus`.

Authenticated by the **`sessionKey`** cookie + browser-like headers (notably a
realistic `User-Agent`) so Cloudflare treats it as a real browser session.

**This was authored without a live session key to test against**, so the exact
endpoint path, headers and field names are best-effort from the community
projects below. All of it is isolated in **`claude.js`** (the `⚠ ADJUST-ME`
section). If Anthropic changes the web app: open DevTools on claude.ai, copy the
real request URL / headers / response, and adjust **`claude.js` only** — the
JSON contract above and the firmware stay unchanged. `claude.js` already
tolerates several observed field-name variants and both `0–1` and `0–100`
utilization scales.

Reference community implementations of the same endpoint:
- thanoban/claude-usage-app — calls `/api/organizations/{orgId}/usage`.
- f-is-h/usage4claude, theDanButuc/Claude-Usage-Monitor, hamed-elfayome/Claude-Usage-Tracker — macOS menu-bar monitors using the `sessionKey` cookie.
- jens-duttke/usage-monitor-for-claude — Windows tray monitor.

These read the `sessionKey` (and sometimes Cloudflare's `cf_clearance`) cookie
to look like the logged-in browser; this proxy does the same with a key you
paste in.
