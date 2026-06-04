// claude.js — the ONLY file that talks to claude.ai.
//
// ┌──────────────────────────────────────────────────────────────────────────┐
// │  ⚠  ADJUST-ME SECTION                                                      │
// │  claude.ai's usage endpoint is UNOFFICIAL and undocumented. The endpoint   │
// │  path, request headers and response field names below are reverse-         │
// │  engineered from the browser and from community tools (see README          │
// │  "Sources & assumptions"). If Anthropic changes the web app, edit THIS     │
// │  file only — the JSON contract in server.js / the device stay stable.      │
// └──────────────────────────────────────────────────────────────────────────┘
//
// Flow (matches how the claude.ai web app and community monitors do it):
//   1. GET https://claude.ai/api/organizations            -> list orgs, pick UUID
//   2. GET https://claude.ai/api/organizations/{uuid}/usage -> usage windows
// Both authenticated with the `sessionKey` cookie + browser-like headers so
// Cloudflare treats the request as a real browser session.

const BASE = "https://claude.ai";

// Browser-like headers. A realistic, current User-Agent matters most for
// getting past Cloudflare's bot check. Bump this when your real browser's UA
// drifts (copy it from DevTools > Network > any claude.ai request).
function browserHeaders(sessionKey) {
  return {
    "accept": "*/*",
    "accept-language": "en-US,en;q=0.9",
    "user-agent":
      "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 " +
      "(KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36",
    "referer": "https://claude.ai/",
    "origin": "https://claude.ai",
    "sec-fetch-dest": "empty",
    "sec-fetch-mode": "cors",
    "sec-fetch-site": "same-origin",
    // sessionKey is the login cookie copied from the browser (sk-ant-sid...).
    // If your browser also has a cf_clearance cookie you can append it here
    // ("; cf_clearance=...") should plain sessionKey ever get Cloudflare-blocked.
    "cookie": `sessionKey=${sessionKey}`,
  };
}

async function getJSON(path, sessionKey) {
  const r = await fetch(BASE + path, { headers: browserHeaders(sessionKey) });
  if (r.status === 401 || r.status === 403) {
    throw new Error(`auth rejected (${r.status}) — session key likely expired/invalid`);
  }
  if (!r.ok) {
    throw new Error(`upstream ${r.status} for ${path}`);
  }
  const ct = r.headers.get("content-type") || "";
  if (!ct.includes("application/json")) {
    // Cloudflare challenge pages come back as HTML — make that diagnosable.
    throw new Error(`non-JSON response for ${path} (got "${ct}") — possibly Cloudflare-blocked`);
  }
  return r.json();
}

// ── helpers to normalise claude.ai's shape into OUR contract ────────────────
function epoch(v) {
  if (v == null) return null;
  if (typeof v === "number") return v > 1e12 ? Math.floor(v / 1000) : v; // ms or s
  const t = Date.parse(v); // ISO 8601
  return Number.isNaN(t) ? null : Math.floor(t / 1000);
}

// claude.ai expresses utilization as either 0..1 or 0..100 depending on field;
// clamp to an integer percent either way.
function pct(v) {
  if (v == null) return 0;
  let n = Number(v);
  if (Number.isNaN(n)) return 0;
  if (n > 0 && n <= 1) n *= 100;
  return Math.max(0, Math.min(100, Math.round(n)));
}

// Pull a window {used_pct, resets_at} out of claude.ai's object, tolerating the
// handful of field names community tools have observed across versions.
function window_(o) {
  if (!o || typeof o !== "object") return { used_pct: 0, resets_at: null };
  const used =
    o.utilization ?? o.used ?? o.usage ?? o.used_pct ?? o.percent ?? o.value;
  const reset =
    o.resets_at ?? o.reset_at ?? o.resetsAt ?? o.reset ?? o.resets ?? o.next_reset;
  return { used_pct: pct(used), resets_at: epoch(reset) };
}

// ── public: fetch + normalise ───────────────────────────────────────────────
export async function fetchUsage({ sessionKey, orgId }) {
  // 1. resolve org UUID (unless pinned via CLAUDE_ORG_ID)
  let uuid = orgId;
  let plan = "unknown";
  if (!uuid) {
    const orgs = await getJSON("/api/organizations", sessionKey);
    const list = Array.isArray(orgs) ? orgs : (orgs?.organizations || []);
    if (!list.length) throw new Error("no organizations returned for this session key");
    // Prefer an org that actually has a chat/subscription capability.
    const chat = list.find(
      (o) => Array.isArray(o.capabilities) && o.capabilities.some((c) => /chat|claude/i.test(c))
    );
    const org = chat || list[0];
    uuid = org.uuid || org.id;
    plan = org.billing_type || org.subscription_tier || org.plan || plan;
  }

  // 2. usage for that org
  const u = await getJSON(`/api/organizations/${uuid}/usage`, sessionKey);

  // The usage payload nests the windows; tolerate a couple of shapes.
  const root = u.usage || u;
  const fiveHour = root.five_hour || root.fiveHour || root.session || root["5h"];
  const weekly   = root.seven_day || root.sevenDay || root.weekly || root["7d"];
  const weekly_opus = root.seven_day_opus || root.sevenDayOpus || root.weekly_opus || null;

  if (root.plan) plan = root.plan;
  if (root.subscription_tier) plan = root.subscription_tier;

  return {
    plan,
    five_hour: window_(fiveHour),
    weekly: window_(weekly),
    weekly_opus: weekly_opus ? window_(weekly_opus) : null, // null if plan has none
    fetched_at: Math.floor(Date.now() / 1000),
  };
}
