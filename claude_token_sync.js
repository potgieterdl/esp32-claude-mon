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
//   node claude_token_sync.js --config <path>          # use a specific config.json
//   node claude_token_sync.js --no-config              # don't record the token in config.json
//
// Node 20+; no npm deps (built-in fs/os/path/child_process + fetch).
//
// macOS note: Claude Code stores ALL logins in one shared Keychain item (no per-CLAUDE_CONFIG_DIR
// namespacing — anthropics/claude-code#20553). The Keychain is therefore only read immediately
// after this script's own login (never as a stored candidate), and a device login on a Mac
// replaces your everyday credential — sign back in afterwards (the script warns about this).

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

// JSON.parse that never leaks the input into the error message — these inputs hold tokens, and
// Node's native parse error quotes the bytes around the failure point.
function parseJson(text, what) {
  try { return JSON.parse(text); }
  catch { throw new Error(`${what} is not valid JSON.`); }
}

// fetch with a bounded timeout and one retry on connection-level failure (the device web server
// is known to drop a first request; transient LAN/mDNS hiccups look the same). HTTP error
// statuses are NOT retried.
async function fetchRetry(url, opts, tries = 2) {
  for (let i = 1; ; i++) {
    try { return await fetch(url, { signal: AbortSignal.timeout(20000), ...opts }); }
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
  if (process.platform === "darwin") {
    console.warn("⚠ macOS: Claude Code keeps ONE shared Keychain item for ALL logins (anthropics/claude-code#20553),");
    console.warn("  so this device login will REPLACE your everyday Claude Code credential in the Keychain.");
    console.warn("  Once the sync completes, run `claude auth login` in your normal terminal to sign back in");
    console.warn("  (the device keeps its own copy — your re-login won't affect it).\n");
  }
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
// File ONLY — used when gathering stored candidates. The macOS Keychain is deliberately NOT a
// candidate source: Claude Code keeps a single shared "Claude Code-credentials" item for EVERY
// CLAUDE_CONFIG_DIR profile (anthropics/claude-code#20553), so a stored item is just as likely
// your everyday login — syncing it would put YOUR token family on the device, and the device's
// first refresh would then log your everyday Claude Code out.
function readDeviceDirCreds() {
  const file = path.join(deviceDir, ".credentials.json");
  return fs.existsSync(file) ? parseJson(fs.readFileSync(file, "utf8"), file) : null;
}

// Read the credential RIGHT AFTER our own `claude auth login` — the one moment the shared macOS
// Keychain item is trustworthy, because the device login we just ran is what wrote it. On darwin
// the Keychain comes FIRST: the fresh login writes only the Keychain, so a leftover (just-declared-
// dead) .credentials.json must not shadow it.
function readFreshLoginCreds() {
  if (process.platform === "darwin") {
    try {
      const out = execFileSync("security",
        ["find-generic-password", "-s", "Claude Code-credentials", "-w"], { encoding: "utf8" });
      const creds = parseJson(out, "the Keychain credential");
      if (creds && creds.claudeAiOauth) return creds;
    } catch { /* fall back to the file */ }
  }
  return readDeviceDirCreds();
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
// True iff the access token works against the usage endpoint right now. Only 401/403 mean "this
// credential doesn't work" — a 429/5xx is the SERVICE misbehaving and throws, so a transient
// failure is never mistaken for a dead credential (which would trigger a needless rotation).
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
  if (r.ok) return true;
  if (r.status === 401 || r.status === 403) return false;
  throw new Error(`usage endpoint returned HTTP ${r.status} — try again later.`);
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
  const j = parseJson(await r.text(), "the token endpoint response");
  // A 200 may already have rotated the family — bail out rather than "dead, try the next one",
  // which would replay sibling generations of a family that just moved on.
  if (!j.access_token) throw new Error("token endpoint returned an unusable response — try again later.");
  return {
    access_token: j.access_token,
    refresh_token: j.refresh_token || cand.refresh_token,        // rotates; keep old if absent
    expires_at: Date.now() + (Number(j.expires_in) || 28800) * 1000,
    rate_limit_tier: cand.rate_limit_tier || "",
  };
}

// Try the stored candidates newest-first: reuse a working access token as-is, refresh an expired
// one (always refresh under --refresh), skip dead ones. `persist` is called with a rotated pair
// the moment the token endpoint returns it — the old refresh token is consumed at that point, so
// the new pair must hit disk before ANY further call can fail. Returns {oauth, how, source} or
// null when everything is dead.
async function findUsableCredential(candidates, persist) {
  for (const cand of candidates) {
    const { source, ...oauth } = cand;
    if (!has("--refresh") &&
        oauth.expires_at - EXPIRY_SKEW_MS > Date.now() && await accessTokenWorks(oauth.access_token)) {
      return { oauth, how: "reused", source };
    }
    const rotated = await refreshCredential(oauth);
    if (rotated) {
      persist(rotated);
      // A 200 from the token endpoint proves the chain is alive; the probe only catches a
      // credential the USAGE endpoint won't accept (e.g. one lacking the user:profile scope).
      if (await accessTokenWorks(rotated.access_token)) return { oauth: rotated, how: "refreshed", source };
      console.log(`✗ stored credential (${source}) refreshes but can't read usage (missing scope?) — trying the next one.`);
      continue;
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

// Record the pair everywhere we read candidates from. Must never throw — it runs at the exact
// moment a rotation has consumed the previous refresh token, and aborting there would strand the
// new pair in memory; a failed write is only a warning (the other copy is still attempted).
function persistOauth(cfgPath, oauth) {
  let recorded = false;
  if (!has("--no-config") && cfgPath) {
    try {
      let cfg = {};
      if (fs.existsSync(cfgPath)) cfg = parseJson(fs.readFileSync(cfgPath, "utf8"), cfgPath);
      cfg.oauth = oauth;                   // top-level oauth block (record; device is source of truth)
      fs.writeFileSync(cfgPath, JSON.stringify(cfg, null, 2) + "\n");
      recorded = true;
    } catch (e) { console.warn(`⚠ could not record the token in ${cfgPath} (${e.message})`); }
  }
  // ALWAYS write the dedicated dir's copy (creating it if needed) — the one guaranteed sink,
  // immune to --no-config and to a missing/unwritable config.json. Without it a rotation could
  // consume the device's refresh token and leave the new pair existing only in memory (or, on
  // macOS, only in the shared Keychain item the user is told to overwrite by re-logging in).
  const file = path.join(deviceDir, ".credentials.json");
  try {
    let creds = {};
    if (fs.existsSync(file)) {
      try { creds = parseJson(fs.readFileSync(file, "utf8"), file); }
      catch { /* corrupt — rebuild it; candidates are validated before use anyway */ }
    }
    creds.claudeAiOauth = { ...(creds.claudeAiOauth || {}), accessToken: oauth.access_token,
                            refreshToken: oauth.refresh_token, expiresAt: oauth.expires_at };
    fs.mkdirSync(deviceDir, { recursive: true });
    fs.writeFileSync(file, JSON.stringify(creds, null, 2) + "\n");
  } catch (e) { console.warn(`⚠ could not record the token in ${file} (${e.message})`); }
  return recorded;   // true = the config.json record was written (drives the "recorded in" log)
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
  const cfg = cfgPath && fs.existsSync(cfgPath)
    ? parseJson(fs.readFileSync(cfgPath, "utf8"), cfgPath) : {};
  const host  = argVal("--device") || "claude-monitor.local";
  const token = deviceToken(cfg);
  const auth  = "Basic " + Buffer.from("admin:" + token).toString("base64");
  const url   = `http://${host}/config.json`;

  // 1. Reach the device FIRST — fail fast before any login, and read its current token pair
  // (the newest member of the rotating family). Any degraded read aborts: refreshing an OLDER
  // local copy while the device holds a newer one replays a consumed generation of the family.
  let devRes;
  try {
    devRes = await fetchRetry(url, { headers: { "authorization": auth } });
  } catch (e) {
    console.error(`✗ could not reach the device at ${host} (${e.message}).`);
    console.error("  Pass --device <ip> (find it on the device's screen), or check it's on WiFi.");
    process.exit(1);
  }
  if (devRes.status === 401) throw new Error("device rejected the device token (HTTP 401) — check device.token.");
  if (!devRes.ok) throw new Error(`device returned HTTP ${devRes.status} reading its config — fix that first (the sync PUT would fail the same way).`);
  const deviceOauth = parseJson(await devRes.text(), "the device's config response").oauth;
  console.log(`✓ device reachable at ${host}.`);

  // 2./3. Reuse or refresh a stored credential; 4. browser login only when all are dead.
  const persist = (oauth) => persistOauth(cfgPath, oauth);
  let found = has("--login") ? null
            : await findUsableCredential(gatherCandidates(deviceOauth, cfg), persist);
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
    const oauth = credsToOauth(readFreshLoginCreds());
    if (!oauth) throw new Error(`no credential found in ${deviceDir} — did the subscription login complete?`);
    // Probe before shipping it to the device — catches a scope-lacking login (and, on macOS, a
    // stale credential masquerading as the fresh one) here instead of as 403s on the device.
    if (!(await accessTokenWorks(oauth.access_token))) {
      throw new Error("the fresh login can't read the usage endpoint (missing user:profile scope?) — use a normal subscription login.");
    }
    found = { oauth, how: "new login" };
  }

  // 5. Persist BEFORE the PUT (a rotated pair must never exist only in flight; the refreshed
  // path already persisted inside findUsableCredential — this re-write is a harmless no-op).
  if (persistOauth(cfgPath, found.oauth)) console.log(`✓ recorded in ${cfgPath}`);
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
  if (found.how === "new login" && process.platform === "darwin") {
    console.warn("⚠ reminder: this login replaced your everyday Claude Code's Keychain credential —");
    console.warn("  run `claude auth login` in your normal terminal to sign back in.");
  }
})().catch((e) => { console.error("ERROR: " + e.message); process.exit(1); });
