#!/usr/bin/env bash
# SessionStart hook — remind agents about pending firmware release / doc housekeeping.
#
# Why: .claude/rules/release.md fires when firmware/ is edited, but the release checklist is deferred
# to "after the user confirms on the device" — usually a LATER session, by which point the rule's
# trigger is gone. This re-checks the actual repo state every session and surfaces drift so it isn't
# forgotten across that gap. Advisory only: injects a note, never blocks, silent when clean, and must
# never fail the session.
set -uo pipefail

root="${CLAUDE_PROJECT_DIR:-$(git rev-parse --show-toplevel 2>/dev/null)}"
{ [ -n "${root:-}" ] && cd "$root" 2>/dev/null; } || exit 0

cfg="firmware/include/app_config.h"
[ -f "$cfg" ] || exit 0   # not this repo / no firmware — stay silent

fw="$(sed -nE 's/.*#define[[:space:]]+FW_VERSION[[:space:]]+"([^"]+)".*/\1/p' "$cfg" | head -1)"
[ -n "$fw" ] || exit 0

issues=""
add() { issues="${issues:+$issues; }$1"; }

# 1) firmware/releases/README.md has a row for this version
if [ -f firmware/releases/README.md ] && ! grep -q "v${fw}" firmware/releases/README.md 2>/dev/null; then
  add "no firmware/releases/README.md row for v${fw}"
fi

# 2) CLAUDE.md 'Latest good' pointer matches FW_VERSION
latest="$(grep -i 'Latest good' CLAUDE.md 2>/dev/null | grep -oE 'fw-v[0-9]+\.[0-9]+\.[0-9]+' | head -1)"
if [ -n "$latest" ] && [ "$latest" != "fw-v${fw}" ]; then
  add "CLAUDE.md 'Latest good' is ${latest} but FW_VERSION is ${fw}"
fi

# 3) git tag fw-vX.Y.Z-good exists
if ! git rev-parse -q --verify "refs/tags/fw-v${fw}-good" >/dev/null 2>&1; then
  add "no git tag fw-v${fw}-good"
fi

# 4) every firmware/src/*.cpp documented in docs/ARCHITECTURE.md
if [ -f docs/ARCHITECTURE.md ]; then
  miss=""
  for f in firmware/src/*.cpp; do
    [ -e "$f" ] || continue
    b="$(basename "$f" .cpp)"
    grep -q "$b" docs/ARCHITECTURE.md 2>/dev/null || miss="${miss:+$miss }$b"
  done
  [ -n "$miss" ] && add "firmware module(s) missing from docs/ARCHITECTURE.md: ${miss}"
fi

[ -z "$issues" ] && exit 0   # all consistent — nothing to say

note="Release/doc housekeeping may be pending for FW_VERSION ${fw} (.claude/rules/release.md): ${issues}. If this version shipped and is device-confirmed, finish the release checklist (README Features bullet, firmware/releases row + archived .bin, CLAUDE.md 'Latest good', tag fw-v${fw}-good) and add any new modules to docs/ARCHITECTURE.md. If it has not shipped yet, ignore."

# Emit as SessionStart context (note text is JSON-safe: no quotes/backslashes).
printf '{"hookSpecificOutput":{"hookEventName":"SessionStart","additionalContext":"%s"}}\n' "$note"
exit 0
