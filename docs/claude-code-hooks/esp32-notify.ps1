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
# Reads Claude Code's JSON event on stdin; project name = leaf of ".cwd". Always exits 0 (never blocks).
param([ValidateSet('needs_input', 'clear')][string]$Signal = 'needs_input')

$monitorHost = if ($env:CLAUDE_MONITOR_HOST) { $env:CLAUDE_MONITOR_HOST } else { 'claude-monitor.local' }
$token = $env:CLAUDE_MONITOR_TOKEN
if (-not $token) { exit 0 }        # not configured → silently do nothing

if ($Signal -eq 'clear') {
  $body = '{"event":"clear"}'
}
else {
  $project = ''
  try {
    $stdin = [Console]::In.ReadToEnd()
    if ($stdin) { $project = Split-Path -Leaf ([string](($stdin | ConvertFrom-Json).cwd)) }
  }
  catch { }
  $body = @{ event = 'needs_input'; project = $project } | ConvertTo-Json -Compress
}

try {
  curl.exe -s -m 5 -u "admin:$token" -H 'Content-Type: application/json' -d $body "http://$monitorHost/notify" | Out-Null
}
catch { }
exit 0
