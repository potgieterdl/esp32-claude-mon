#  Pre-build hook: inject settings from the repo-root config.json as compile-time -D defines.
#  This replaces the old wifi_config.h — config.json is the single place to edit secrets/settings.
#  app_settings.cpp reads the CFG_* macros (with placeholder fallbacks so it still compiles
#  without a config.json, e.g. in CI). Secrets are never printed.
import json, os
Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

proj = env["PROJECT_DIR"]
candidates = [
    os.environ.get("CONFIG_FILE"),
    os.path.join(os.path.dirname(proj), "config.json"),  # repo root (../config.json)
    os.path.join(proj, "config.json"),                   # fallback: firmware/config.json
]
cfg_path = next((p for p in candidates if p and os.path.isfile(p)), None)

if not cfg_path:
    # Don't fail the build — app_settings.cpp has placeholder fallbacks (compiles in CI, etc.).
    print("[load_config] WARNING: no config.json found — copy config.example.json -> config.json and "
          "fill it in. Building with placeholder defaults (the device won't connect to your Wi-Fi).")
else:
    with open(cfg_path, encoding="utf-8") as f:
        cfg = json.load(f)
    wifi, dev = cfg.get("wifi", {}), cfg.get("device", {})
    th, disp = dev.get("thresholds", {}), dev.get("display", {})

    def S(v):  # safely stringify for a C string macro (handles quotes/specials in passwords)
        return env.StringifyMacro(str(v))

    defs = []
    if "ssid" in wifi:  defs.append(("CFG_WIFI_SSID",   S(wifi["ssid"])))
    if "pass" in wifi:  defs.append(("CFG_WIFI_PASS",   S(wifi["pass"])))
    # device.token is the web/OTA basic-auth password (NOT a proxy thing — the proxy is gone).
    # The OAuth usage token is NOT baked in at build time; it's synced over WiFi by claude_token_sync.js.
    if "token" in dev:  defs.append(("CFG_DEVICE_TOKEN", S(dev["token"])))
    if "poll_seconds" in dev: defs.append(("CFG_POLL_SECONDS", int(dev["poll_seconds"])))
    if "tz" in dev:     defs.append(("CFG_TZ", S(dev["tz"])))
    if "warn_pct" in th: defs.append(("CFG_WARN_PCT", int(th["warn_pct"])))
    if "max_pct" in th:  defs.append(("CFG_MAX_PCT",  int(th["max_pct"])))
    if "brightness" in disp:     defs.append(("CFG_BRIGHTNESS",     int(disp["brightness"])))
    if "dim_after_s" in disp:    defs.append(("CFG_DIM_AFTER_S",    int(disp["dim_after_s"])))
    if "dim_brightness" in disp: defs.append(("CFG_DIM_BRIGHTNESS", int(disp["dim_brightness"])))
    if "dim_on_idle" in disp:    defs.append(("CFG_DIM_ON_IDLE", 1 if disp["dim_on_idle"] else 0))

    env.Append(CPPDEFINES=defs)
    print(f"[load_config] {os.path.relpath(cfg_path, proj)} -> injected {len(defs)} settings "
          f"(wifi='{wifi.get('ssid','?')}', secrets redacted)")
