// claude_token_sync.js — set up / refresh the Claude Monitor device's token.
//
// The device calls api.anthropic.com directly and refreshes its own token. It needs a credential
// that (a) has the `user:profile` scope the usage endpoint requires, and (b) lives on its OWN
// refresh-token family so the device refreshing it never logs YOU out of your everyday Claude Code.
//
// This script gives it both: it runs `claude auth login` into a DEDICATED config dir
// (~/.claude-device by default — separate from your normal ~/.claude), reads the resulting token,
// and syncs it to the device over WiFi (PUT /config.json, basic-auth admin / device token).
//
// Run it once at first setup, and again only if the device's token chain ever fully dies
// (each run mints a fresh login). Your everyday Claude Code session is never touched.
//
//   node claude_token_sync.js                          # device = claude-monitor.local (mDNS)
//   node claude_token_sync.js --device 10.0.0.118      # explicit IP/host
//   node claude_token_sync.js --no-login               # reuse the dedicated-dir token (skip browser)
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

// ── 1. dedicated login (interactive browser) ────────────────────────────────
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

// ── 2. read the dedicated credential ────────────────────────────────────────
function readDeviceCreds() {
  const file = path.join(deviceDir, ".credentials.json");
  if (fs.existsSync(file)) return JSON.parse(fs.readFileSync(file, "utf8"));
  if (process.platform === "darwin") {       // macOS may use the Keychain even with a custom dir
    try {
      const out = execFileSync("security",
        ["find-generic-password", "-s", "Claude Code-credentials", "-w"], { encoding: "utf8" });
      return JSON.parse(out);
    } catch { /* fall through to error below */ }
  }
  throw new Error(`no credential found in ${deviceDir} — the login may have failed. Re-run without --no-login.`);
}

function deviceOAuth(creds) {
  const o = creds && creds.claudeAiOauth;
  if (!o || !o.refreshToken) {
    throw new Error("no OAuth refresh token in the device credential — did the subscription login complete?");
  }
  // The usage endpoint requires user:profile; warn early if a login type lacks it (e.g. an
  // inference-only `claude setup-token`, which 403s on /api/oauth/usage).
  const scopes = Array.isArray(o.scopes) ? o.scopes : [];
  if (scopes.length && !scopes.includes("user:profile")) {
    console.warn(`⚠ this credential lacks the 'user:profile' scope (has: ${scopes.join(", ")}).`);
    console.warn("  The device will get 403 from the usage endpoint. Use a normal subscription login.");
  }
  return {                                 // device /config.json snake_case shape
    access_token: o.accessToken || "",
    refresh_token: o.refreshToken,
    expires_at: o.expiresAt || 0,          // ms epoch; device normalises to seconds
    rate_limit_tier: o.rateLimitTier || o.subscriptionType || "",
  };
}

// ── 3. record in config.json (repo root, next to this script) ───────────────
function resolveConfigPath() {
  if (argVal("--config")) return path.resolve(expandHome(argVal("--config")));
  const c = [process.env.CONFIG_FILE,
             path.resolve(process.cwd(), "config.json"),
             path.resolve(process.cwd(), "../config.json")].filter(Boolean);
  return c.find((p) => fs.existsSync(p)) || c[1] || c[0];
}

function updateConfig(cfgPath, oauth) {
  let cfg = {};
  if (cfgPath && fs.existsSync(cfgPath)) cfg = JSON.parse(fs.readFileSync(cfgPath, "utf8"));
  cfg.oauth = oauth;                       // top-level oauth block (record; device is source of truth)
  fs.writeFileSync(cfgPath, JSON.stringify(cfg, null, 2) + "\n");
  return cfg;
}

// ── 4. device token (web/OTA basic-auth password) ───────────────────────────
function deviceToken(cfg) {
  if (argVal("--token")) return argVal("--token");
  if (process.env.DEVICE_TOKEN) return process.env.DEVICE_TOKEN;
  const t = cfg && cfg.device && cfg.device.token;
  if (t) return t;
  throw new Error("device token not found — pass --token <t> or set device.token in config.json.");
}

// ── main ────────────────────────────────────────────────────────────────────
(async () => {
  if (!has("--no-login")) claudeLogin();

  const oauth = deviceOAuth(readDeviceCreds());
  const exp = oauth.expires_at ? new Date(oauth.expires_at).toISOString() : "n/a";
  console.log(`Device credential ready (tier=${oauth.rate_limit_tier || "?"}, access expires ${exp}).`);

  let cfg = {};
  const cfgPath = resolveConfigPath();
  if (!has("--no-config")) {
    cfg = updateConfig(cfgPath, oauth);
    console.log(`✓ recorded in ${cfgPath}`);
  } else if (cfgPath && fs.existsSync(cfgPath)) {
    cfg = JSON.parse(fs.readFileSync(cfgPath, "utf8"));
  }

  const host  = argVal("--device") || "claude-monitor.local";
  const token = deviceToken(cfg);
  const url = `http://${host}/config.json`;
  const auth = "Basic " + Buffer.from("admin:" + token).toString("base64");
  try {
    const r = await fetch(url, {
      method: "PUT",
      headers: { "authorization": auth, "content-type": "application/json" },
      body: JSON.stringify({ oauth }),
    });
    const body = await r.text().catch(() => "");
    if (r.ok) console.log(`✓ token synced to device at ${host}. It refreshes itself from here on.`);
    else { console.error(`✗ device returned HTTP ${r.status}: ${body.slice(0, 200)}`); process.exit(1); }
  } catch (e) {
    console.error(`✗ could not reach the device at ${host} (${e.message}).`);
    console.error("  Pass --device <ip> (find it on the device's screen), or check it's on WiFi.");
    process.exit(1);
  }
})().catch((e) => { console.error("ERROR: " + e.message); process.exit(1); });
