// claude_token_sync.js — set up / recover the Claude Monitor device's token.
//
// The device calls api.anthropic.com directly and refreshes its own token. It needs a credential
// that (a) has the `user:profile` scope the usage endpoint requires, and (b) lives on its OWN
// refresh-token family so the device refreshing it never logs YOU out of your everyday Claude Code.
//
// The default flow is SMART — a browser login is the last resort, not the first step:
//   1. Reach the device (fail fast, with retry — its web server can drop a first request) and
//      read the token pair it currently holds (the newest member of the rotating family).
//   2. Gather every stored credential — device pair, the dedicated dir (~/.claude-device),
//      the config.json record — newest first.
//   3. Reuse the first one that still works (verified read-only against the usage endpoint),
//      refreshing it via platform.claude.com when the access token has expired.
//   4. Only when every stored credential is dead: run `claude auth login` into the DEDICATED
//      config dir (separate from your normal ~/.claude, so the families stay independent).
//   5. PUT the result to the device (basic-auth admin / device token), with retry. A rotated
//      pair is persisted BEFORE the PUT, so a failed sync never strands it — just re-run.
//
//   node claude_token_sync.js                          # device = claude-monitor.local (mDNS)
//   node claude_token_sync.js --device 10.0.0.118      # explicit IP/host
//   node claude_token_sync.js --login                  # force a fresh browser login
//   node claude_token_sync.js --refresh                # rotate the pair even if it still works
//   node claude_token_sync.js --no-login               # never open a browser (fail if all dead)
//   node claude_token_sync.js --dir ~/.claude-monitor  # use a different dedicated config dir
//   node claude_token_sync.js --token <devicetoken>    # override the device token (device.token)
//
// Node 20+; no npm deps (built-in fs/os/path/child_process + fetch).

import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { spawnSync, execFileSync } from "node:child_process";

const args = process.argv.slice(2);
const has = (f) => args.includes(f);
const argVal = (f) => { const i = args.indexOf(f); return i >= 0 ? args[i + 1] : null; };

function expandHome(p) {
  if (!p) return p;
  return p.startsWith("~") ? path.join(os.homedir(), p.slice(1)) : p;
}

// Dedicated config dir for the DEVICE's login — kept separate from your real ~/.claude so the two
// credentials are independent token families (refreshing one never invalidates the other).
const deviceDir = expandHome(argVal("--dir")) || process.env.CLAUDE_DEVICE_DIR ||
                  path.join(os.homedir(), ".claude-device");

// Anthropic OAuth contract — mirrors what the firmware sends (firmware/src/app_data.cpp).
const USAGE_URL    = "https://api.anthropic.com/api/oauth/usage";
const TOKEN_URL    = "https://platform.claude.com/v1/oauth/token";
const OAUTH_CLIENT = "9d1c250a-e61b-44d9-88ed-5944d1962f5e";   // public Claude Code PKCE client (no secret)
const USER_AGENT   = "claude-code/1.0.0";                       // non-claude-code UA -> 429 bucket
const OAUTH_BETA   = "oauth-2025-04-20";
const EXPIRY_SKEW_MS = 10 * 60 * 1000;   // treat an access token expiring this soon as expired

// expires_at arrives as epoch seconds (device) or milliseconds (.credentials.json) — normalise to ms.
const toMs = (e) => { e = Number(e) || 0; return e && e < 1e11 ? e * 1000 : e; };

// fetch with one retry on connection-level failure (the device web server is known to drop a
// first request; transient LAN/mDNS hiccups look the same). HTTP error statuses are NOT retried.
async function fetchRetry(url, opts, tries = 2) {
  for (let i = 1; ; i++) {
    try { return await fetch(url, opts); }
    catch (e) {
      if (i >= tries) throw e;
      await new Promise((r) => setTimeout(r, 700));
    }
  }
}

// ── interactive browser login (the last resort) ─────────────────────────────
function claudeLogin() {
  fs.mkdirSync(deviceDir, { recursive: true });
  console.log("\nSigning in a DEDICATED device credential (separate from your everyday Claude Code).");
  console.log("A browser window will open — approve it with your Claude subscription.");
  console.log(`(login stored in ${deviceDir}, not your normal ~/.claude)\n`);
  const res = spawnSync("claude", ["auth", "login", "--claudeai"], {
    stdio: "inherit",
    shell: true,                                   // resolve claude.cmd/.ps1 on Windows
    env: { ...process.env, CLAUDE_CONFIG_DIR: deviceDir },
  });
  if (res.error) {
    throw new Error(`could not run \`claude auth login\` (${res.error.message}). Install Claude Code first.`);
  }
  if (res.status !== 0) throw new Error(`\`claude auth login\` exited with code ${res.status}.`);
}

// ── the dedicated-dir credential (~/.claude-device/.credentials.json) ───────
function readDeviceDirCreds() {
  const file = path.join(deviceDir, ".credentials.json");
  if (fs.existsSync(file)) return JSON.parse(fs.readFileSync(file, "utf8"));
  if (process.platform === "darwin") {       // macOS may use the Keychain even with a custom dir
    try {
      const out = execFileSync("security",
        ["find-generic-password", "-s", "Claude Code-credentials", "-w"], { encoding: "utf8" });
      return JSON.parse(out);
    } catch { /* fall through */ }
  }
  return null;
}

// claudeAiOauth (camelCase) -> the device /config.json snake_case shape (expires_at in ms).
function credsToOauth(creds) {
  const o = creds && creds.claudeAiOauth;
  if (!o || !o.refreshToken) return null;
  // The usage endpoint requires user:profile; warn early if a login type lacks it (e.g. an
  // inference-only `claude setup-token`, which 403s on /api/oauth/usage).
  const scopes = Array.isArray(o.scopes) ? o.scopes : [];
  if (scopes.length && !scopes.includes("user:profile")) {
    // Don't print the scope list (it's credential-derived) — just the actionable warning.
    console.warn("⚠ this credential lacks the 'user:profile' scope the usage endpoint needs.");
    console.warn("  The device will get 403 from the usage endpoint. Use a normal subscription login.");
  }
  return {
    access_token: o.accessToken || "",
    refresh_token: o.refreshToken,
    expires_at: toMs(o.expiresAt),
    rate_limit_tier: o.rateLimitTier || o.subscriptionType || "",
  };
}

// ── credential validation (read-only) + refresh ─────────────────────────────
// True iff the access token works against the usage endpoint right now.
async function accessTokenWorks(access) {
  if (!access) return false;
  const r = await fetchRetry(USAGE_URL, {
    headers: {
      "authorization": `Bearer ${access}`,
      "anthropic-beta": OAUTH_BETA,
      "user-agent": USER_AGENT,
      "accept": "application/json",
    },
  });
  return r.ok;
}

// Exchange a refresh token for a fresh pair (same contract as the firmware's refreshToken()).
// Returns the rotated oauth block, or null when the token is dead (4xx). Network errors throw —
// "Anthropic unreachable" must not be mistaken for "credential dead" (a login wouldn't help either).
async function refreshCredential(cand) {
  const r = await fetchRetry(TOKEN_URL, {
    method: "POST",
    headers: { "content-type": "application/json", "user-agent": USER_AGENT, "accept": "application/json" },
    body: JSON.stringify({ grant_type: "refresh_token", refresh_token: cand.refresh_token,
                           client_id: OAUTH_CLIENT }),
  });
  if (!r.ok) {
    if ([400, 401, 403].includes(r.status)) return null;         // refresh token dead (as the firmware treats it)
    throw new Error(`token endpoint returned HTTP ${r.status} — try again later.`);
  }
  const j = await r.json();
  if (!j.access_token) return null;
  return {
    access_token: j.access_token,
    refresh_token: j.refresh_token || cand.refresh_token,        // rotates; keep old if absent
    expires_at: Date.now() + (Number(j.expires_in) || 28800) * 1000,
    rate_limit_tier: cand.rate_limit_tier || "",
  };
}

// Try the stored candidates newest-first: reuse a working access token as-is, refresh an expired
// one (always refresh under --refresh), skip dead ones. Returns {oauth, how, source} or null when
// everything is dead.
async function findUsableCredential(candidates) {
  for (const cand of candidates) {
    const { source, ...oauth } = cand;
    if (!has("--refresh") &&
        oauth.expires_at - EXPIRY_SKEW_MS > Date.now() && await accessTokenWorks(oauth.access_token)) {
      return { oauth, how: "reused", source };
    }
    const rotated = await refreshCredential(oauth);
    if (rotated && await accessTokenWorks(rotated.access_token)) {
      return { oauth: rotated, how: "refreshed", source };
    }
    console.log(`✗ stored credential (${source}) is dead — trying the next one.`);
  }
  return null;
}

// Newest-first (by expiry — a later expiry = a later generation of the rotating family),
// deduped by refresh token so the same pair recorded in two places is only tried once.
function gatherCandidates(deviceOauth, cfg) {
  const list = [];
  const add = (o, source) => {
    if (!o || !o.refresh_token) return;
    if (list.some((c) => c.refresh_token === o.refresh_token)) return;
    list.push({ access_token: o.access_token || "", refresh_token: o.refresh_token,
                expires_at: toMs(o.expires_at), rate_limit_tier: o.rate_limit_tier || "", source });
  };
  add(deviceOauth, "device");
  add(credsToOauth(readDeviceDirCreds()), deviceDir);
  add(cfg && cfg.oauth, "config.json");
  return list.sort((a, b) => b.expires_at - a.expires_at);
}

// ── persistence (config.json record + the dedicated dir) ────────────────────
function resolveConfigPath() {
  if (argVal("--config")) return path.resolve(expandHome(argVal("--config")));
  const c = [process.env.CONFIG_FILE,
             path.resolve(process.cwd(), "config.json"),
             path.resolve(process.cwd(), "../config.json")].filter(Boolean);
  return c.find((p) => fs.existsSync(p)) || c[0];
}

function persistOauth(cfgPath, oauth) {
  if (!has("--no-config") && cfgPath) {
    let cfg = {};
    if (fs.existsSync(cfgPath)) cfg = JSON.parse(fs.readFileSync(cfgPath, "utf8"));
    cfg.oauth = oauth;                     // top-level oauth block (record; device is source of truth)
    fs.writeFileSync(cfgPath, JSON.stringify(cfg, null, 2) + "\n");
    console.log(`✓ recorded in ${cfgPath}`);
  }
  // Keep the dedicated dir's copy current too (when it exists as a file), so it stays a live
  // candidate for the next run instead of a long-consumed generation of the family.
  const file = path.join(deviceDir, ".credentials.json");
  if (fs.existsSync(file)) {
    try {
      const creds = JSON.parse(fs.readFileSync(file, "utf8"));
      creds.claudeAiOauth = { ...(creds.claudeAiOauth || {}), accessToken: oauth.access_token,
                              refreshToken: oauth.refresh_token, expiresAt: oauth.expires_at };
      fs.writeFileSync(file, JSON.stringify(creds, null, 2) + "\n");
    } catch { /* a stale record there is harmless — candidates are validated before use */ }
  }
}

// ── device access ────────────────────────────────────────────────────────────
function deviceToken(cfg) {
  if (argVal("--token")) return argVal("--token");
  if (process.env.DEVICE_TOKEN) return process.env.DEVICE_TOKEN;
  const t = cfg && cfg.device && cfg.device.token;
  if (t) return t;
  throw new Error("device token not found — pass --token <t> or set device.token in config.json.");
}

// ── main ────────────────────────────────────────────────────────────────────
(async () => {
  const cfgPath = resolveConfigPath();
  const cfg = cfgPath && fs.existsSync(cfgPath) ? JSON.parse(fs.readFileSync(cfgPath, "utf8")) : {};
  const host  = argVal("--device") || "claude-monitor.local";
  const token = deviceToken(cfg);
  const auth  = "Basic " + Buffer.from("admin:" + token).toString("base64");
  const url   = `http://${host}/config.json`;

  // 1. Reach the device FIRST — fail fast before any login, and read its current token pair.
  let deviceOauth = null;
  try {
    const r = await fetchRetry(url, { headers: { "authorization": auth } });
    if (r.status === 401) throw new Error("device rejected the device token (HTTP 401) — check device.token.");
    if (r.ok) deviceOauth = (await r.json()).oauth;
    else console.warn(`⚠ device returned HTTP ${r.status} reading its config — continuing without its stored token.`);
  } catch (e) {
    if (/device rejected/.test(e.message)) throw e;
    console.error(`✗ could not reach the device at ${host} (${e.message}).`);
    console.error("  Pass --device <ip> (find it on the device's screen), or check it's on WiFi.");
    process.exit(1);
  }
  console.log(`✓ device reachable at ${host}.`);

  // 2./3. Reuse or refresh a stored credential; 4. browser login only when all are dead.
  let found = has("--login") ? null : await findUsableCredential(gatherCandidates(deviceOauth, cfg));
  if (found) {
    console.log(found.how === "reused"
      ? `✓ stored credential (${found.source}) still works — reusing it, no login needed.`
      : `✓ stored credential (${found.source}) refreshed — no login needed.`);
  } else {
    if (has("--no-login")) {
      throw new Error("no stored credential is usable and --no-login forbids a fresh login — re-run without --no-login.");
    }
    if (!has("--login")) console.log("→ every stored credential is dead — a fresh browser login is needed.");
    claudeLogin();
    const oauth = credsToOauth(readDeviceDirCreds());
    if (!oauth) throw new Error(`no credential found in ${deviceDir} — did the subscription login complete?`);
    found = { oauth, how: "new login" };
  }

  // 5. Persist BEFORE the PUT (a rotated pair must never exist only in flight), then sync.
  persistOauth(cfgPath, found.oauth);
  let r;
  try {
    r = await fetchRetry(url, {
      method: "PUT",
      headers: { "authorization": auth, "content-type": "application/json" },
      body: JSON.stringify({ oauth: found.oauth }),
    });
  } catch (e) {
    throw new Error(`could not sync to the device at ${host} (${e.message}) — ` +
                    "the credential is saved locally; just re-run (no new login will be needed).");
  }
  if (!r.ok) {
    const body = await r.text().catch(() => "");
    throw new Error(`device returned HTTP ${r.status}: ${body.slice(0, 200)}`);
  }
  console.log(`✓ token synced to device at ${host} (${found.how}). It refreshes itself from here on.`);
})().catch((e) => { console.error("ERROR: " + e.message); process.exit(1); });
