# Web API and live device settings

The device runs a small HTTP server on port 80, reachable by mDNS at `claude-monitor.local` (or its DHCP IP).
Use it to read status, change settings live without reflashing, and update the firmware over the air. Auth is
HTTP basic: user `admin`, password the `device` token from your `config.json`.

## Endpoints

| Path | Auth | Purpose |
|---|---|---|
| `/` | none | Status page: firmware, WiFi/IP, uptime, free heap. |
| `/config.json` | `admin` / token | GET or PUT runtime settings (also holds the WiFi and OAuth secrets). |
| `/status` | `admin` / token | GET a JSON usage snapshot (plan, valid, stale, needs_token, five_hour, weekly). |
| `/notify` | `admin` / token | POST `{"event":"needs_input"\|"clear"}` for the "needs input" alert. |
| `/update` | `admin` / token | OTA firmware upload (ElegantOTA). |

## Live settings (no reflash)

Beyond the `config.json` you fill in at setup, the device serves its live settings on the LAN, so you can
change brightness, thresholds and so on without rebuilding. It seeds these from the build-time defaults.

```bash
# read current settings
curl -u admin:$TOKEN http://claude-monitor.local/config.json

# change a few display settings (merged and persisted)
curl -u admin:$TOKEN -X PUT -H "Content-Type: application/json" \
     -d '{"display":{"brightness":30,"dim_on_idle":true,"dim_after_s":60,"sleep_after_s":300}}' \
     http://claude-monitor.local/config.json
```

Configurable: WiFi, `poll_seconds`, alert thresholds (warn and max %), timezone (POSIX TZ), display
(brightness, dim-on-idle and its timeout, and the idle-sleep timeout `sleep_after_s`, where `0` never sleeps),
and audio (`mute`, and `volume` 0-100 scaling the chimes).
Brightness applies live; the rest apply on next use. A bad value is clamped, and a malformed body returns
`400` without overwriting the file. The `oauth` block also lives here but is managed by the device and the
sync script, so do not edit it by hand.

## Updating over the air (OTA)

Once the device is on WiFi you can flash without a cable. Build the firmware, then upload
`firmware/.pio/build/esp32-c6/firmware.bin` (the app image, not `*.factory.bin`) to
`http://<device-ip>/update` in a browser. OTA writes the inactive app slot and only boots it if the MD5
checks out, so a bad upload cannot brick the device. For the scripted `curl` version, see
[`CLAUDE.md`](../CLAUDE.md) under "Flash over WiFi (OTA)".

## Rolling back

Flash a known-good image from [`firmware/releases/`](../firmware/releases/README.md).

## "Needs input" alerts (Claude Code hooks)

The `/notify` endpoint powers the "Claude needs your input" banner: a Claude Code hook POSTs to it when a
session is waiting on you, and again to clear it. Copy-paste hook scripts and the wiring are in
[`docs/claude-code-hooks/`](claude-code-hooks/README.md).
