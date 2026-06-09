# ESP32-C6 Claude Monitor

Desk status display on a **Waveshare ESP32-C6-Touch-LCD-1.69**: shows my Claude usage limits + next reset,
with swipe between screens (clock, device status; later weather). LVGL UI, live data fetched **directly from
the Anthropic usage API** over CA-pinned HTTPS, with the OAuth token refreshed on-device (no proxy).

> **Starting a new task in a fresh context? Run the [`orient`](.claude/skills/orient/SKILL.md) skill FIRST.**
> It reads the canonical docs + folder rules + the subsystem's source, traces how that piece connects to the
> rest, and reports a structured understanding before any edit — the "understand before you act" loop. Skip
> only for trivial one-line lookups.

> **Read the docs in this order (single source of truth each — don't duplicate):**
> 1. **[`README.md`](README.md)** FIRST — what the app is, its delivered **Features**, hardware, setup.
> 2. **[GitHub Issues](https://github.com/potgieterdl/esp32-claude-mon/issues)** — the roadmap (only what's
>    left to build). Feature requests carry `type: feature`; **`agent: ready`** = vetted/scoped enough to pick up.
> 3. Then whatever's relevant: **[`adr/`](adr/README.md)** (why key decisions were made) ·
>    **[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)** (portable core ↔ device adapter, render path, modules) ·
>    **hardware** → [`boards/<arch>/<slug>/SPEC.md`](boards/README.md) ·
>    **rollback / build history** → [`firmware/releases/README.md`](firmware/releases/README.md).
> Folder-scoped rules auto-load from [`.claude/rules/`](.claude/rules) when you edit `firmware/` or `ui/`
> (incl. the **release checklist** that closes the shipped issue, moves the feature into README, and pushes `main`).

> **Before any non-obvious or architectural decision, CHECK [`adr/`](adr/README.md) FIRST.** Follow the
> existing ADRs — don't relitigate or silently contradict a settled choice; if one genuinely needs to change,
> supersede it **explicitly** (new ADR marked "Supersedes ADR-NNNN", old one updated to "Superseded by").
> When you make a NEW key architectural choice — or a non-obvious "tried X, chose Y" not evident from the code —
> **write an ADR** for it (template in [`adr/README.md`](adr/README.md)). Key decisions only; not routine features/fixes.
>
> **Usage data is fetched on-device directly from Anthropic** — see [ADR-0006](adr/0006-device-direct-oauth.md)
> (which supersedes [ADR-0001](adr/0001-self-hosted-proxy.md)).

## Repo layout
- `ui/` — **portable LVGL UI** (`ui.cpp`/`ui.h`), shared by firmware + simulator. **Edit UI here.**
- `firmware/` — device PlatformIO project: hardware glue (`src/`), config (`include/`), `releases/` (known-good bins).
- `experiments/sim/` — desktop simulator (native/gcc) → PNG screenshots, no hardware needed.
- `boards/` — per-device hardware specs (one folder per board; scales to more devices).
- `docs/` — `ARCHITECTURE.md`, schematic PDF, demo bundle; `perf-notes`/`research-notes` are historical.
- `claude_token_sync.js` (repo root) — one-shot setup/recovery: dedicated `claude auth login` → PUTs an OAuth
  token to `claude-monitor.local`. `vendor/` — upstream Waveshare refs (pull, don't edit).

## Workflow
- **Pull upstream first** when starting: `git -C vendor/ESP32-C6-Touch-LCD-1.69 pull`.
- **Edit UI in `ui/`** → preview in the simulator → only then flash. The `ui/` module is portable (no Arduino).
- **Validate via the LOCAL sim, not on-device screenshots** (we tried; the device flush only has the logical
  LVGL buffer the sim already renders, and it costs ~134 KB RAM). **Sim = layout + data; physical photo = panel truth.**

## Roadmap & issues (GitHub) — the work loop
**The roadmap lives in [GitHub Issues](https://github.com/potgieterdl/esp32-claude-mon/issues), not a file.**
`todo.md` is retired. Feature requests are filed via the issue forms in `.github/ISSUE_TEMPLATE/`; a GitHub
Action appends an agent-context footer (read CLAUDE.md + run `orient`) to every new issue.
- **Labels:** `type:` feature/bug/chore · `area:` ui/firmware/data/build/docs · `priority: high` · `status:`
  blocked/in-progress · **`agent: ready`** (scoped, ready to implement) · `epic` (one multi-phase feature → one issue + one PR).
  Release versions are tracked with **milestones** (`fw-vX.Y.Z`).
- **Pick → branch → finish → PR → (you merge).** One issue per change. **A PR means "finished and ready for
  your approval," never work-in-progress** — do the COMPLETE job on the branch *before* `gh pr create`:
  implement, validate (sim / build / on-device), update all docs, run the architect + doc reviews and resolve
  findings, **sync with `main`** (pull + resolve conflicts locally), secrets check. **Never open a PR early and patch it.**
  ```powershell
  gh issue list --label "agent: ready"           # what's ready to work
  gh issue view <N>                              # the issue body IS the brief — read it fully
  git switch -c feature/<N>-short-slug           # branch named after the issue
  # ... implement → test-flash → fix/polish → docs → architect + doc review → sync main → secrets check ...
  git fetch origin && git merge origin/main      # pull latest main, resolve conflicts + re-validate locally
  gh pr create --base main --title "feat: … (closes #<N>)" --body "Closes #<N>`n`n<what changed>"
  ```
  Use `feature/`, `fix/`, or `chore/` branch prefixes. Put `Closes #<N>` in the **PR body** (auto-closes on
  merge to the default branch). Reference other issues without closing as plain `#<N>`.
- **You review and merge every PR manually — the agent never runs `gh pr merge`** unless you say so in that
  moment. After your merge: `--delete-branch` + prune local merged branches.
- **Working-tree safety — each session runs in its own `claude --worktree`, which isolates branches.** A
  session has its own working dir + branch, and git refuses to check out one branch in two worktrees, so a
  commit can't silently land on another session's WIP. Two residual rules:
  - **The repo root is the unsafe spot:** it's a worktree with `main` checked out, so a *plain* `claude`
    (no `-w`) launched there commits straight onto `main`. Before committing anywhere, sanity-check `git
    branch --show-current` + `git status`; to change `main`, branch first (or use a throwaway `git worktree
    add -b <branch> <path> origin/main` → PR → `git worktree remove`), never `git switch` in place.
  - **Stage explicit paths** (`git add <file>…`), never `git add -A` — the per-session worktrees live under
    `.claude/worktrees/` *inside* the repo (gitignored), so a blanket add from the root can still rope in
    other trees or build artifacts.
- **Epics = one issue and one PR — never sub-issues, never a drip of small PRs.** An epic is a *single
  cohesive feature*; its body carries a **`## Todo`** section — a markdown `[ ]` checklist of subtasks (the
  work breakdown, sibling to acceptance criteria). Build the whole feature on **one** branch (multiple commits
  and *test* flashes are fine; nothing merges mid-stream); tick boxes as subtasks land; when the feature is
  complete + validated, open **one** PR that `Closes #<N>`. How to execute one → *Working an epic* below.
- **Pre-PR architectural review (required before opening a code PR).** Before `gh pr create`, spawn an
  **independent subagent** (the Agent tool) to review the diff against `main`. Brief it to weigh **SOLID**,
  **DRY**, and general best practices alongside this project's conventions (portable `ui/` boundary,
  honest-display, the render-path/ADR decisions) and to **report back ONLY noteworthy items** — blockers /
  should-fix / notable nits, no rubber-stamping or restating what's fine. Resolve blockers + should-fix
  before creating the PR; note any deliberately-deferred items in the PR body. Skip only for trivial
  doc/copy/config changes.
- **Pre-PR sync with `main` (required, right AFTER the reviews).** Once the architect + doc findings are
  resolved and *before* `gh pr create`, pull the latest `main` into the branch and deal with any conflicts
  **locally** — `git fetch origin && git merge origin/main` (rebase is fine too) — then **re-build / re-validate**
  the merged result. This surfaces conflicts on *your* machine, where you can test the integrated diff, instead
  of discovering them at merge time, and keeps the PR mergeable. Do it *after* the reviews so they critique the
  real, about-to-merge diff (and re-run a quick build if the merge pulled in overlapping code).

## Multi-agent (use where it helps)
Delegate to subagents (the Agent tool) to keep the main context lean and to parallelize — it has paid off here:
- **Research fan-out:** parallel agents for independent topics (per-subsystem / per-library); each returns a synthesis.
- **Parallel feature dev:** one agent per independent module; integrate serially (single device → flashing is serialized).
- **`claude-code-guide`** agent for Claude Code questions (hooks, settings, SDK, memory/rules).
- Reserve direct work for single-file edits / quick lookups; delegate broad, file-spanning investigations.

**Working an epic** (one issue, one `## Todo`, one PR at the end):
1. **Build context + validate the `## Todo`.** It may be stale or from another session — run `orient`, re-read
   the issue, and confirm the breakdown is still correct (update it if not) *before* implementing.
2. **Coordinator + workers.** The top agent stays a **coordinator** and keeps its context lean — it does *not*
   implement. For each Todo item it spawns an **implementation subagent** with the context that item needs.
   **Parallelize independent items**; serialize dependent ones (on-device flashing is always serial).
3. **Per-subtask gates.** Before an item is ticked, run the **architect review** *and* the **doc reviewer** on
   it, so each piece is well-built and consistent across the codebase before moving on.
4. **Tick the box, integrate, next.** When every box is done and the feature is validated end-to-end, open the
   single PR (`Closes #<N>`) for the maintainer to merge.

## Build & flash (PlatformIO, project in `firmware/`)
```powershell
$env:PYTHONIOENCODING='utf-8'   # avoid a cosmetic UnicodeEncodeError on Windows
pio run -d firmware                 # build
pio run -d firmware -t upload       # build + flash over USB (auto-detects COM port)
```
- Uses the **pioarduino** platform fork (`55.03.38-1`) — required for ESP32-C6. Board/pin facts → the board SPEC.
- **If `Could not open COMx`:** replug the USB-C cable (native USB drifts after resets), then retry — or use OTA ↓.
- **Serial (HWCDC) is unreliable on the C6** — confirm "is it running?" via the **screen/backlight**, not `Serial`.

### Flash over WiFi (OTA) — Claude can run this end-to-end, no USB
When USB drifts (or you just don't want a cable), flash the running device over the LAN via ElegantOTA.
Prereq: device booted + on WiFi (reach it at `http://claude-monitor.local/` or its IP on the Device screen).
```powershell
$ip="claude-monitor.local"; $tok="<device.token from config.json>"
$bin="firmware/.pio/build/esp32-c6/firmware.bin"          # the app image — NOT firmware.factory.bin
$md5=(Get-FileHash $bin -Algorithm MD5).Hash.ToLower()
curl.exe -s -u "admin:$tok" "http://$ip/ota/start?mode=fr&hash=$md5"          # -> OK (HTTP 200)
curl.exe -s -u "admin:$tok" -F "file=@$bin;type=application/octet-stream" "http://$ip/ota/upload"  # -> OK; verifies MD5 + reboots
curl.exe -s "http://$ip/"   # verify: page shows "Firmware: <new version>"
```
- `/update` is the ElegantOTA browser page; `/ota/start`+`/ota/upload` are the API it calls (what the curl above uses).
- Auth is basic `admin` / the device token (`device.token`). OTA writes the *inactive* slot and only boots it if the MD5 checks out,
  so a bad upload can't brick the device. The web server can drop the **first** request (curl `000`) — just retry.
- **Self-test over WiFi too:** `GET/PUT http://<ip>/config.json` exercises settings; `/` reports firmware/heap.

## Versioning & rollback (IMPORTANT)
- **Always bump `FW_VERSION`** in `firmware/include/app_config.h` before each feature build (shows on the Device
  screen + `/`). Keep it in lockstep with the `fw-vX.Y.Z` git tag and `firmware/releases/<name>.bin`.
- **One feature per flash.** Integrate + flash a single change; the user verifies on the device before the next.
- **Keep known-good builds.** After a confirmed-good flash, archive `.pio/build/esp32-c6/firmware.factory.bin`
  → `firmware/releases/<name>.bin` and `git tag` the commit. Rollback = flash that `.bin` at 0x0.
  Latest good: `fw-v1.9.4-good`.

## Config & settings
**One file: the repo-root `config.json`** (gitignored; `config.example.json` is the template) holds
`wifi` {ssid,pass}, `device` {token, poll_seconds, tz, thresholds, display, audio}, and an `oauth` block
{access_token, refresh_token, expires_at, rate_limit_tier}. **No `proxy` section.** `device.token` is the
basic-auth password for the device's web endpoints (`/config.json`, `/status`, OTA `/update`) and for the
sync script — it is *not* a proxy token. The `oauth` block is written by `claude_token_sync.js` (and rotated
on-device on refresh), never hand-edited.
- **Device build:** `firmware/load_config.py` (pre-build) reads `../config.json` → `CFG_*` compile defines →
  `app_settings` seed defaults. (Placeholder fallbacks let it compile with no config.json.)
- **Token setup:** `node claude_token_sync.js` runs `claude auth login` into a dedicated dir (`~/.claude-device`,
  so the device's token family is independent of your own login) and PUTs the token to `claude-monitor.local`.
- **Live device settings:** the device also serves `GET/PUT http://claude-monitor.local/config.json`
  (auth `admin`/device-token) to change runtime settings (brightness, dim, thresholds…) without reflashing —
  seeded from the build defaults.

## Secrets & publishing (CHECK before testing or pushing)
Real secrets live ONLY in the gitignored `config.json` — WiFi pass, `device.token`, and the `oauth` tokens
(access/refresh); the live OAuth token also sits on the device (LittleFS), never in git. `config.example.json`
is the template a new user fills in. **Checked in on purpose** (help contributors): `.claude/rules/*.md`,
`.claude/settings.json`, `config.example.json`.
- **On a fresh clone:** `cp config.example.json config.json` and fill it in (else the device build uses
  placeholder Wi-Fi and won't connect), then `node claude_token_sync.js` to give the device a token.
- **Before any `git push` / making the repo public, verify nothing secret is tracked:**
  ```powershell
  git ls-files | Select-String '^config\.json$|settings\.local|/\.env$|releases/.*\.bin$'   # expect: NONE
  git grep -niE 'sk-ant-(oat|ort)0[0-9]' -- .                                               # OAuth tokens — expect: NONE
  ```
  Both must come back empty. Built images embed compiled creds, so `firmware/releases/*.bin` stay gitignored.
  `.claude/settings.local.json` holds a private env id — gitignored; never commit it.

> **What's done / what's next:** delivered features → [`README.md`](README.md#features); roadmap →
> [GitHub Issues](https://github.com/potgieterdl/esp32-claude-mon/issues).
> On every version ship, follow the release checklist in [`.claude/rules/release.md`](.claude/rules/release.md)
> (close the issue, move the feature into README, add an ADR if it was a key decision, archive/tag, push `main`).
