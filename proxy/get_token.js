// get_token.js — pull the local Claude Code OAuth token and feed it to the proxy.
//
// Run this on a machine where you've logged into Claude Code (`claude` then sign
// in with your subscription). It reads Claude Code's stored credential, writes
// the token blob into the repo-root config.json (proxy.oauth_token), AND — if a
// proxy is reachable — POSTs it live to the proxy's /token endpoint so a dead
// token is fixed in real time (no restart, no redeploy).
//
//   node proxy/get_token.js                 # update config.json + push if reachable
//   node proxy/get_token.js --no-push       # only update config.json
//   node proxy/get_token.js --no-config     # only push to the proxy
//   node proxy/get_token.js --config ../config.json
//
// No npm deps (built-in fs/path/os/child_process + fetch), same as the proxy.

import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { execFileSync } from "node:child_process";

const args = process.argv.slice(2);
const has = (f) => args.includes(f);
const argVal = (f) => { const i = args.indexOf(f); return i >= 0 ? args[i + 1] : null; };

// ── 1. locate + read Claude Code credentials (cross-platform) ───────────────
function readClaudeCreds() {
  const dir = process.env.CLAUDE_CONFIG_DIR || path.join(os.homedir(), ".claude");
  const file = path.join(dir, ".credentials.json");
  if (fs.existsSync(file)) {
    return JSON.parse(fs.readFileSync(file, "utf8"));
  }
  if (process.platform === "darwin") {
    // macOS stores it in the Keychain, not a file.
    try {
      const out = execFileSync(
        "security",
        ["find-generic-password", "-s", "Claude Code-credentials", "-w"],
        { encoding: "utf8" }
      );
      return JSON.parse(out);
    } catch (e) {
      throw new Error("Claude Code credentials not in Keychain — run `claude` and sign in first.");
    }
  }
  throw new Error(
    `Claude Code credentials not found at ${file} — install Claude Code, run \`claude\`, and sign in first.`
  );
}

function extractBlob(creds) {
  const o = creds && creds.claudeAiOauth;
  if (!o || !o.refreshToken) {
    throw new Error("no OAuth refresh token in Claude Code credentials — sign in with a subscription, not an API key.");
  }
  return {
    accessToken: o.accessToken || null,
    refreshToken: o.refreshToken,
    expiresAt: o.expiresAt || 0,
    rateLimitTier: o.rateLimitTier || null,
    subscriptionType: o.subscriptionType || null,
  };
}

// ── 2. config.json (repo root, one level up from proxy/) ────────────────────
function resolveConfigPath() {
  if (argVal("--config")) return path.resolve(argVal("--config"));
  const candidates = [
    process.env.CONFIG_FILE,
    path.resolve(process.cwd(), "../config.json"),
    path.resolve(process.cwd(), "config.json"),
  ].filter(Boolean);
  for (const p of candidates) if (fs.existsSync(p)) return p;
  return candidates[1] || candidates[0];   // default to ../config.json
}

function updateConfig(cfgPath, blob) {
  let cfg = {};
  if (fs.existsSync(cfgPath)) cfg = JSON.parse(fs.readFileSync(cfgPath, "utf8"));
  cfg.proxy = cfg.proxy || {};
  cfg.proxy.oauth_token = blob;
  fs.writeFileSync(cfgPath, JSON.stringify(cfg, null, 2) + "\n");
  return cfg;
}

// ── 3. push to a running proxy (best-effort) ────────────────────────────────
async function pushToProxy(cfg, blob) {
  const proxy = (cfg && cfg.proxy) || {};
  const token = (process.env.PROXY_TOKEN || proxy.token || "").trim();
  if (!proxy.url || !token) {
    console.log("• push skipped: proxy.url / proxy.token not set in config.json");
    return;
  }
  const base = String(proxy.url).replace(/\/usage\/?$/, "").replace(/\/+$/, "");
  const target = base + "/token";
  try {
    const r = await fetch(target, {
      method: "POST",
      headers: { "authorization": "Bearer " + token, "content-type": "application/json" },
      body: JSON.stringify(blob),
    });
    const body = await r.text().catch(() => "");
    if (r.ok) console.log(`✓ pushed token live to ${target}  -> ${body}`);
    else console.log(`• push to ${target} returned HTTP ${r.status}: ${body.slice(0, 200)}`);
  } catch (e) {
    console.log(`• proxy not reachable at ${target} (${e.message}) — config.json updated; it'll pick up on next deploy/restart.`);
  }
}

// ── main ────────────────────────────────────────────────────────────────────
(async () => {
  const blob = extractBlob(readClaudeCreds());
  const exp = blob.expiresAt ? new Date(blob.expiresAt).toISOString() : "n/a";
  console.log(`Found Claude Code OAuth token (tier=${blob.rateLimitTier || blob.subscriptionType || "?"}, access expires ${exp}).`);

  let cfg = {};
  const cfgPath = resolveConfigPath();
  if (!has("--no-config")) {
    cfg = updateConfig(cfgPath, blob);
    console.log(`✓ wrote proxy.oauth_token into ${cfgPath}`);
  } else if (fs.existsSync(cfgPath)) {
    cfg = JSON.parse(fs.readFileSync(cfgPath, "utf8"));
  }

  if (!has("--no-push")) await pushToProxy(cfg, blob);

  console.log("Done. The proxy auto-refreshes this token from here on; re-run this script only if it ever fully expires.");
})().catch((e) => { console.error("ERROR: " + e.message); process.exit(1); });
