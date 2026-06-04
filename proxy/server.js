// claude-usage-proxy — LAN proxy for the ESP32-C6 Claude monitor.
//
// Holds the claude.ai session key (which the device cannot use directly: the
// ESP32's TLS fingerprint is blocked by Cloudflare). Polls claude.ai's
// subscription usage endpoint with browser-like headers on a gentle interval,
// caches the result, and serves a clean JSON contract on the LAN guarded by a
// shared bearer token.
//
// Node 20+ only (uses built-in fetch + node:http). No external dependencies.

import http from "node:http";
import fs from "node:fs";
import path from "node:path";
import { fetchUsage } from "./claude.js";

// ── config: the repo-root config.json (single source for the whole project), with env override ──
// Reads the `proxy` section of config.json. Looked up via $CONFIG_FILE, then ../config.json
// (repo root — proxy/ is the cwd), then ./config.json. Env vars still win for ad-hoc overrides.
function loadProxyConfig() {
  const candidates = [
    process.env.CONFIG_FILE,
    path.resolve(process.cwd(), "../config.json"),
    path.resolve(process.cwd(), "config.json"),
  ].filter(Boolean);
  for (const p of candidates) {
    try {
      if (fs.existsSync(p)) {
        const c = JSON.parse(fs.readFileSync(p, "utf8")).proxy || {};
        console.log(`[proxy] loaded config from ${p}`);
        return c;
      }
    } catch (e) {
      console.warn(`[proxy] config.json parse error at ${p}: ${e.message}`);
    }
  }
  return {};
}
const cfg = loadProxyConfig();

const PORT          = parseInt(process.env.PORT || "8080", 10);
const TOKEN         = (process.env.PROXY_TOKEN || cfg.token || "").trim();
const POLL_SECONDS  = Math.max(15, parseInt(process.env.POLL_SECONDS || cfg.poll_seconds || "60", 10));
// Session key: a mounted FILE is checked first (refreshable without restart), then env, then config.json.
const KEY_FILE      = process.env.SESSION_KEY_FILE || "/run/secrets/session_key";
const KEY_ENV       = process.env.CLAUDE_SESSION_KEY || "";
const KEY_CFG       = cfg.session_key || "";
// Optional: pin the org UUID to skip the /api/organizations lookup.
const ORG_ID        = (process.env.CLAUDE_ORG_ID || cfg.org_id || "").trim();

if (!TOKEN) {
  console.error("[proxy] FATAL: PROXY_TOKEN is not set. Refusing to start (would be an open endpoint).");
  process.exit(1);
}

// ── session key (refreshable at runtime) ────────────────────
function readSessionKey() {
  try {
    if (fs.existsSync(KEY_FILE)) {
      const k = fs.readFileSync(KEY_FILE, "utf8").trim();
      if (k) return k;
    }
  } catch (_) { /* fall through */ }
  return (KEY_ENV || KEY_CFG).trim();   // env, then config.json's proxy.session_key
}

// ── cache ───────────────────────────────────────────────────
// Last good payload + last error. Device polls are served entirely from here,
// so many device polls never reach claude.ai.
let cache = null;        // last successful contract object
let lastError = null;    // string | null
let lastAttempt = 0;     // epoch seconds of last upstream attempt

async function poll() {
  lastAttempt = Math.floor(Date.now() / 1000);
  const key = readSessionKey();
  if (!key) {
    lastError = "no session key configured";
    console.warn("[proxy] poll skipped: " + lastError);
    return;
  }
  try {
    const data = await fetchUsage({ sessionKey: key, orgId: ORG_ID });
    cache = data;
    lastError = null;
    console.log(`[proxy] ok  5h=${data.five_hour.used_pct}%  weekly=${data.weekly.used_pct}%  plan=${data.plan}`);
  } catch (e) {
    lastError = String(e && e.message ? e.message : e);
    console.warn("[proxy] poll failed: " + lastError);
  }
}

// ── HTTP server ─────────────────────────────────────────────
function send(res, code, obj) {
  const body = JSON.stringify(obj);
  res.writeHead(code, {
    "content-type": "application/json",
    "content-length": Buffer.byteLength(body),
    "cache-control": "no-store",
  });
  res.end(body);
}

function authorized(req) {
  const h = req.headers["authorization"] || "";
  return h === `Bearer ${TOKEN}`;
}

const server = http.createServer((req, res) => {
  const url = (req.url || "/").split("?")[0];

  // Health check is open (no token) so you can curl it during setup.
  if (req.method === "GET" && url === "/health") {
    return send(res, 200, {
      ok: cache != null,
      has_cache: cache != null,
      last_error: lastError,
      last_attempt: lastAttempt,
      poll_seconds: POLL_SECONDS,
    });
  }

  if (req.method === "GET" && url === "/usage") {
    if (!authorized(req)) return send(res, 401, { error: "unauthorized" });
    if (!cache) {
      // No good data yet (e.g. just started, or session key invalid).
      return send(res, 503, { error: lastError || "no data yet", stale: true });
    }
    const age = Math.floor(Date.now() / 1000) - cache.fetched_at;
    // Surface staleness so the device can show a "stale" state without guessing.
    return send(res, 200, { ...cache, stale: age > POLL_SECONDS * 3, age_seconds: age });
  }

  send(res, 404, { error: "not found" });
});

server.listen(PORT, () => {
  console.log(`[proxy] listening on :${PORT}  (poll every ${POLL_SECONDS}s)`);
  poll();                                   // prime immediately
  setInterval(poll, POLL_SECONDS * 1000);   // then on interval
});
