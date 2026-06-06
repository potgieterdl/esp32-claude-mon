#!/usr/bin/env bash
# esp32-notify.sh — Claude Code hook → ESP32 Claude Monitor "needs input" alert (issue #2).
#
# Lights the desk display when a Claude Code session is waiting on you, and clears it when you
# come back. Wire it into your *global* Claude Code settings (~/.claude/settings.json) so every
# project's sessions drive the device — see docs/claude-code-hooks/README.md for the snippet:
#   Notification     → bash "$HOME/.claude/hooks/esp32-notify.sh" needs_input
#   UserPromptSubmit → bash "$HOME/.claude/hooks/esp32-notify.sh" clear
#
# Config via environment (export these in your shell profile so the hook process inherits them):
#   CLAUDE_MONITOR_TOKEN  (required)  the device token  (config.json → device.token)
#   CLAUDE_MONITOR_HOST   (optional)  default: claude-monitor.local
#
# Claude Code pipes the event as JSON on stdin; the project name is the last component of ".cwd".
# The hook ALWAYS exits 0 and never blocks Claude — a missing token or unreachable device is a no-op.
set +e
signal="${1:-needs_input}"
host="${CLAUDE_MONITOR_HOST:-claude-monitor.local}"
token="${CLAUDE_MONITOR_TOKEN:-}"
[ -z "$token" ] && exit 0          # not configured → silently do nothing

if [ "$signal" = "clear" ]; then
  body='{"event":"clear"}'
else
  # Pull "cwd" out of the JSON without needing jq, normalise \ → / (Windows paths), take the leaf.
  input="$(cat)"
  cwd="$(printf '%s' "$input" | sed -n 's/.*"cwd"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | tr '\\' '/')"
  project="$(basename "$cwd" 2>/dev/null)"
  body="{\"event\":\"needs_input\",\"project\":\"${project}\"}"
fi

curl -s -m 5 -u "admin:$token" -H "Content-Type: application/json" -d "$body" \
  "http://$host/notify" >/dev/null 2>&1
exit 0
