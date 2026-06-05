---
name: orient
description: >-
  Build full context on a subsystem BEFORE touching it. Invoke at the start of any new task
  in a fresh context — "let's work on the data layer", "add a feature to the UI", "change how
  the token refreshes", "I want to change X" — anything that means reading and understanding code before
  acting. Reads the canonical docs in order, the folder rules, the actual source, traces how
  the piece connects to the rest of the system, then reports a structured understanding and
  asks what to do. Skip only for trivial one-line lookups where the answer is already known.
---

# Orient — understand before you act

The point: never start editing a subsystem you haven't mapped. Read the docs, read the code,
trace the data flow across module boundaries, then state your understanding back to the user
and let them confirm before any change. Lean on subagents for breadth (see the multi-agent
note in `CLAUDE.md`) so the main context stays focused.

Work through these in order. Don't skip the docs to get to the code — the docs say *why*, the
code says *how*, and you need both.

## 1. Read the canonical docs, in this order
These are the single source of truth — read them top-down, don't duplicate or relitigate them.
1. **`README.md`** — what the app is, delivered features, hardware, setup. The whole picture.
2. **[GitHub Issues](https://github.com/potgieterdl/esp32-claude-mon/issues)** — the roadmap (only what's
   left). Tells you if the task is already filed: check `gh issue list` (look for `agent: ready` + the
   relevant `area:` label, and the `epic` issues for multi-phase efforts).
3. **`adr/`** — the *why* behind key decisions. **Read any ADR relevant to the subsystem** before
   proposing an architectural change. Follow settled decisions; don't silently contradict one.
4. Then whatever the task touches:
   - **`docs/ARCHITECTURE.md`** — portable core ↔ device adapter, render path, module map.
   - **hardware** → `boards/<arch>/<slug>/SPEC.md` ·
     **rollback / build history** → `firmware/releases/README.md`.

## 2. Read the folder-scoped rules
The subsystem you're about to touch almost certainly has rules in **`.claude/rules/`**
(`hardware.md` for `firmware/`+`boards/`, `ui.md` for `ui/`, `release.md` for shipping). These
auto-load when editing those folders — read them up front so you know the constraints (e.g.
"UI: no Arduino deps in `ui/`") *before* designing.

## 3. Read the actual source of the subsystem
- Glob the folder to see every file, then **read the source — not just the README.** For each
  module note: what it owns, its public functions / endpoints / exported API, and its state.
- For a service: enumerate **every endpoint** (method, path, auth, request/response shape) and
  the core functions behind them.
- For firmware/UI: enumerate the public API (the `.h`), the main loop / render entry points,
  and any config or state it holds.

## 4. Trace how it connects to the rest of the system
The most important step, and the easiest to skip. A subsystem is defined by its boundaries:
- **Who consumes it?** Find the other side of every contract (e.g. the presenter that drives the
  `ui_set_*` setters, the UI that calls a `data_*()` accessor). Grep across folders for the symbols.
- **What's the contract that must stay stable** while internals change? Name it explicitly.
- **Where does config / secrets come from?** (`config.json` → build defines /
  live `/config.json` endpoint.) How is it refreshed?

## 5. Check the roadmap & known gaps for this subsystem
Re-scan the open [GitHub Issues](https://github.com/potgieterdl/esp32-claude-mon/issues) (`gh issue list`,
filter by `area:`) and the relevant rules/ADRs for items that touch this piece — half-built features, known
bugs, "researched — see notes" flags. Surface them; the task may relate.

## 6. Report your understanding, then wait
Write a **structured summary** back to the user before changing anything:
- **What it is** + where it sits in the system (one paragraph).
- **Files & responsibilities** — the module map for this subsystem.
- **API surface** — a table of endpoints/functions (path/signature, auth, shape).
- **Data flow & contracts** — how data moves in/out and what must stay stable.
- **Config & secrets** — where they live, how they're refreshed.
- **Known gaps / roadmap** — anything pending that touches this.

End by asking what specifically to work on. **Don't start editing until the user confirms** —
the goal of this skill is a shared, correct mental model first.
