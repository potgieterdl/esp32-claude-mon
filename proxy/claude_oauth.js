// claude_oauth.js — OAuth-token path to the user's Claude *subscription* usage.
//
// ┌──────────────────────────────────────────────────────────────────────────┐
// │  ⚠  ADJUST-ME SECTION (like claude.js, this hits undocumented endpoints)   │
// │  Unlike claude.js (which scrapes claude.ai behind Cloudflare with a        │
// │  browser cookie), this talks to api.anthropic.com — the same endpoint      │
// │  Claude Code's own usage display calls — using an OAuth token obtained by  │
// │  `claude login`. That host is NOT behind claude.ai's browser bot-check,    │
// │  and the token AUTO-REFRESHES, so there's no recurring manual cookie chore.│
// └──────────────────────────────────────────────────────────────────────────┘
//
// Token lifecycle (the "option b" store): the proxy is *seeded* once from
// config.json's proxy.oauth_token (written there by proxy/get_token.js), then
// keeps the live, rotating token in a writable STORE FILE that this module
// refreshes in place. config.json stays read-only; the store is the source of
// truth at runtime. POST /token (see server.js) hot-swaps the store live.
//
// Both endpoints below are undocumented and reverse-engineered (see proxy/README
// "Sources & assumptions"); keep all of that isolated here, behind the stable
// /usage JSON contract in server.js.

import fs from "node:fs";
import { epoch, pct, window_ } from "./claude.js";

const USAGE_URL = "https://api.anthropic.com/api/oauth/usage";
const TOKEN_URL = "https://platform.claude.com/v1/oauth/token";
// Public Claude Code OAuth client (PKCE, no secret) — identical for every install.
const CLIENT_ID = "9d1c250a-e61b-44d9-88ed-5944d1962f5e";
// The usage endpoint buckets non-Claude-Code callers into an aggressive 429
// rate-limit; presenting a claude-code User-Agent avoids it. Bump if it drifts.
const USER_AGENT = "claude-code/1.0.0";
const OAUTH_BETA = "oauth-2025-04-20";
// Refresh this long BEFORE expiry so a poll never races a just-expired token.
const REFRESH_SKEW_MS = 5 * 60 * 1000;

// ── token store (the live, refreshing copy) ─────────────────────────────────
// blob shape: { accessToken, refreshToken, expiresAt(ms epoch), rateLimitTier?, subscriptionType? }
let storeFile = null;
let token = null;

// Seed/initialise. Prefers an existing store (freshest rotated refresh token);
// falls back to the config.json seed on first run or a wiped volume.
export function initOAuth({ storeFile: f, seed }) {
  storeFile = f || null;
  const stored = readStore();
  if (stored) {
    token = stored;
  } else if (seed && seed.refreshToken) {
    token = { ...seed };
    writeStore();              // materialise the seed into the live store
  } else {
    token = null;
  }
  return hasToken();
}

export function hasToken() { return !!(token && token.refreshToken); }

// Hot-swap the token at runtime (used by POST /token). Returns true if usable.
export function setToken(blob) {
  if (!blob || !blob.refreshToken) return false;
  token = {
    accessToken: blob.accessToken || null,
    refreshToken: blob.refreshToken,
    expiresAt: Number(blob.expiresAt) || 0,
    rateLimitTier: blob.rateLimitTier || null,
    subscriptionType: blob.subscriptionType || null,
  };
  writeStore();
  return true;
}

function readStore() {
  try {
    if (storeFile && fs.existsSync(storeFile)) {
      const t = JSON.parse(fs.readFileSync(storeFile, "utf8"));
      if (t && t.refreshToken) return t;
    }
  } catch (e) {
    console.warn("[oauth] store read error: " + e.message);
  }
  return null;
}

function writeStore() {
  if (!storeFile || !token) return;
  try {
    fs.writeFileSync(storeFile, JSON.stringify(token, null, 2), { mode: 0o600 });
  } catch (e) {
    console.warn("[oauth] could not persist token store: " + e.message);
  }
}

// ── refresh ─────────────────────────────────────────────────────────────────
async function refresh() {
  if (!token || !token.refreshToken) {
    throw new Error("no refresh token — run proxy/get_token.js to reseed");
  }
  const r = await fetch(TOKEN_URL, {
    method: "POST",
    headers: { "content-type": "application/json", "user-agent": USER_AGENT },
    body: JSON.stringify({
      grant_type: "refresh_token",
      refresh_token: token.refreshToken,
      client_id: CLIENT_ID,
    }),
  });
  if (!r.ok) {
    const body = await r.text().catch(() => "");
    throw new Error(
      `token refresh failed (${r.status}) — refresh token likely dead; ` +
      `re-run proxy/get_token.js. ${body.slice(0, 200)}`
    );
  }
  const j = await r.json();
  token.accessToken = j.access_token || token.accessToken;
  // Refresh tokens ROTATE — persist the new one or the next refresh fails.
  if (j.refresh_token) token.refreshToken = j.refresh_token;
  const expIn = Number(j.expires_in) || 8 * 3600;
  token.expiresAt = Date.now() + expIn * 1000;
  writeStore();
  console.log("[oauth] access token refreshed");
}

async function refreshIfNeeded() {
  if (!token) throw new Error("no oauth token configured");
  const valid = token.accessToken && token.expiresAt &&
                Date.now() < token.expiresAt - REFRESH_SKEW_MS;
  if (!valid) await refresh();
}

async function getUsageRaw() {
  return fetch(USAGE_URL, {
    headers: {
      "authorization": "Bearer " + token.accessToken,
      "anthropic-beta": OAUTH_BETA,
      "user-agent": USER_AGENT,
      "accept": "application/json",
    },
  });
}

// ── public: fetch + normalise into OUR /usage contract ──────────────────────
export async function fetchUsageOAuth() {
  await refreshIfNeeded();
  let r = await getUsageRaw();
  if (r.status === 401 || r.status === 403) {
    // Access token may have just died upstream — force one refresh + retry.
    token.expiresAt = 0;
    await refresh();
    r = await getUsageRaw();
  }
  if (r.status === 401 || r.status === 403) {
    throw new Error("auth rejected — refresh token likely dead; re-run proxy/get_token.js");
  }
  if (!r.ok) throw new Error(`usage endpoint ${r.status}`);
  const ct = r.headers.get("content-type") || "";
  if (!ct.includes("application/json")) {
    throw new Error(`non-JSON usage response (got "${ct}")`);
  }
  return normalize(await r.json());
}

// claude.ai's /api/oauth/usage shape:
//   { five_hour:{utilization,resets_at}, seven_day:{...}, seven_day_opus:{...}|null, ... }
// utilization is 0..100, resets_at is ISO 8601 — both handled by window_().
function normalize(u) {
  return {
    plan: planLabel(),
    five_hour: window_(u.five_hour),
    weekly: window_(u.seven_day),
    weekly_opus: u.seven_day_opus ? window_(u.seven_day_opus) : null,
    fetched_at: Math.floor(Date.now() / 1000),
  };
}

// rateLimitTier is e.g. "default_claude_max_5x" → "max_5x" (closer to the
// existing contract's "max_20x"/"pro"). Falls back to subscriptionType.
function planLabel() {
  const t = token && (token.rateLimitTier || token.subscriptionType);
  if (!t) return "unknown";
  return String(t).replace(/^default_claude_/, "");
}
