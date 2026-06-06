# Claude Code "needs input" alert hooks

Light up the desk monitor when a **Claude Code** session is waiting on you, and clear it when you
come back. The device exposes an authenticated `POST /notify` endpoint (issue #2); these
[Claude Code hooks](https://code.claude.com/docs/en/hooks) `curl` it on the right lifecycle events:

| Claude Code event | Fires when… | We send |
|---|---|---|
| `Notification` | Claude is blocked on a permission prompt, or you've been idle ~60 s | `{"event":"needs_input"}` → banner + chime |
| `UserPromptSubmit` | You submit your next prompt | `{"event":"clear"}` → banner clears |

On the device this shows a **"Claude needs your input · Check your terminal"** banner (with a green
**OK** button) and a chime. Besides the next prompt, the banner also clears if you tap **OK**, or
after a 15-min safety timeout — so a missed `UserPromptSubmit` (e.g. you *approved a permission
dialog* instead of typing) can't pin it forever.

> **One banner.** If you run Claude in several projects at once they share the single banner — it
> just says "input needed", not which project. Fine for an at-a-glance desk signal.

## Setup

**1. Copy a hook script** to your home Claude Code hooks dir (pick the one your shell runs):

```bash
mkdir -p ~/.claude/hooks
cp docs/claude-code-hooks/esp32-notify.sh  ~/.claude/hooks/    # bash (macOS/Linux, or Git Bash on Windows)
cp docs/claude-code-hooks/esp32-notify.ps1 ~/.claude/hooks/    # PowerShell (native Windows) — alternative
```

**2. Tell the hook your device token** (the `device.token` from `config.json`). Export it in your
shell profile so the hook process inherits it — it's never written into `settings.json`:

```bash
export CLAUDE_MONITOR_TOKEN="your-device-token"
export CLAUDE_MONITOR_HOST="claude-monitor.local"   # optional; this is the default
```

**3. Wire the hooks** into your **global** `~/.claude/settings.json` (global so *every* project's
sessions reach the display). Bash version:

```json
{
  "hooks": {
    "Notification": [
      { "hooks": [ { "type": "command", "command": "bash \"$HOME/.claude/hooks/esp32-notify.sh\" needs_input", "timeout": 10 } ] }
    ],
    "UserPromptSubmit": [
      { "hooks": [ { "type": "command", "command": "bash \"$HOME/.claude/hooks/esp32-notify.sh\" clear", "timeout": 10 } ] }
    ]
  }
}
```

PowerShell version — same structure, with the `command` strings escaped for JSON:

```json
{
  "hooks": {
    "Notification": [
      { "hooks": [ { "type": "command", "command": "pwsh -NoProfile -File \"$HOME/.claude/hooks/esp32-notify.ps1\" needs_input", "timeout": 10 } ] }
    ],
    "UserPromptSubmit": [
      { "hooks": [ { "type": "command", "command": "pwsh -NoProfile -File \"$HOME/.claude/hooks/esp32-notify.ps1\" clear", "timeout": 10 } ] }
    ]
  }
}
```

> `$HOME` is expanded by the shell Claude Code launches the hook with (bash / Git Bash). If your
> setup doesn't expand it, use the script's **absolute path** instead (e.g. `C:\Users\you\.claude\hooks\esp32-notify.ps1`).

**4. Test it** without Claude Code — POST directly (the hooks just wrap this):

```bash
TOKEN="your-device-token"
curl -s -u "admin:$TOKEN" -H "Content-Type: application/json" \
     -d '{"event":"needs_input"}' http://claude-monitor.local/notify                    # → banner + chime
curl -s -u "admin:$TOKEN" -H "Content-Type: application/json" \
     -d '{"event":"clear"}' http://claude-monitor.local/notify                          # → clears
```

The scripts are deliberately defensive: a missing token or an unreachable device is a **silent
no-op** (`exit 0`), so the hook never blocks or errors your Claude Code session.

## Want every-turn alerts instead?

`Notification` only fires on permission/idle. If you'd rather the banner light up the instant
Claude finishes **any** turn, add a `Stop` hook the same way (`… needs_input`). Be warned it's
noisy — it chimes after every reply during active back-and-forth.
