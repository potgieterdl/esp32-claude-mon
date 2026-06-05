# Architecture Decision Records (ADRs)

Short, numbered records of the **key** decisions behind this project — the *why*, so we don't relitigate
settled choices or lose the "we tried X, it didn't work, so we chose Y" trail. Delivered features live in
[`README.md`](../README.md); what's left lives in [GitHub Issues](https://github.com/potgieterdl/esp32-claude-mon/issues); **ADRs are the reasoning**.

## Before you decide — check these first
**Read the existing ADRs before making an architectural change.** A new choice should be *consistent* with
the records below. If one genuinely must change, don't silently contradict or delete it — write a **new** ADR
that **explicitly supersedes** it: mark the new one `Supersedes ADR-NNNN`, and set the old one's status to
`Superseded by ADR-NNNN`. This keeps the trail of *why it changed* intact.

## When to write one
Write an ADR when a change is **architectural, hard to reverse, or surprising** — something a future
contributor (or Claude) would otherwise question or accidentally undo. Examples that earned one: the
render path, the proxy architecture, the single-config approach.

**Don't** write one for routine work: bug fixes, copy tweaks, dependency bumps, a new screen, normal
features. If you'd have to stretch to fill "Consequences", it's not an ADR.

## How to write one
1. Copy the format below into `adr/NNNN-short-title.md` (next number, zero-padded).
2. Keep it tight — context, the decision, what it costs. One screen, not an essay.
3. Status: `Proposed` → `Accepted` (when shipped) → `Superseded by ADR-NNNN` (if later replaced; don't delete the old one).

```markdown
# ADR NNNN: <Title>
- **Status:** Accepted
- **Date:** YYYY-MM-DD

## Context
What forced a decision — constraints, what was tried and failed.

## Decision
What we chose, stated plainly.

## Consequences
The trade-offs we accept (+ good / − bad), and anything that must NOT be undone.
```

## Index
- [ADR 0001 — Self-hosted proxy for claude.ai usage data](0001-self-hosted-proxy.md) *(superseded by 0006)*
- [ADR 0002 — Render path: LovyanGFX async-DMA + 80 MHz write-only SPI](0002-render-path-lovyangfx-80mhz.md)
- [ADR 0003 — One root `config.json` for all secrets + settings](0003-single-config-json.md)
- [ADR 0004 — Registry dependencies + per-device `boards/` scaffold](0004-registry-deps-and-boards.md)
- [ADR 0005 — Validate the UI in the simulator, not via on-device screenshots](0005-simulator-over-device-screenshot.md)
- [ADR 0006 — Device calls Anthropic usage API directly (on-device OAuth)](0006-device-direct-oauth.md) *(supersedes 0001)*
