# esp32-notify.ps1 — Claude Code hook → ESP32 Claude Monitor "needs input" alert (issue #2).
#
# Native-Windows alternative to esp32-notify.sh (use whichever your Claude Code runs). Lights the
# desk display when a session is waiting on you and clears it when you return. Wire into your
# *global* Claude Code settings (~/.claude/settings.json) — see README.md for the snippet:
#   Notification     → pwsh -NoProfile -File "<path>\esp32-notify.ps1" needs_input
#   UserPromptSubmit → pwsh -NoProfile -File "<path>\esp32-notify.ps1" clear
#
# Config via environment:
#   CLAUDE_MONITOR_TOKEN  (required)  the device token  (config.json → device.token)
#   CLAUDE_MONITOR_HOST   (optional)  default: claude-monitor.local
#
# Always exits 0 (never blocks Claude); a missing token or unreachable device is a silent no-op.
param([ValidateSet('needs_input', 'clear')][string]$Signal = 'needs_input')

$monitorHost = if ($env:CLAUDE_MONITOR_HOST) { $env:CLAUDE_MONITOR_HOST } else { 'claude-monitor.local' }
$token = $env:CLAUDE_MONITOR_TOKEN
if (-not $token) { exit 0 }        # not configured → silently do nothing

$body = if ($Signal -eq 'clear') { '{"event":"clear"}' } else { '{"event":"needs_input"}' }

try {
  curl.exe -s -m 5 -u "admin:$token" -H 'Content-Type: application/json' -d $body "http://$monitorHost/notify" | Out-Null
}
catch { }
exit 0
