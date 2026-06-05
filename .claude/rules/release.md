---
description: Shipping a firmware version — release checklist that keeps the docs self-cleaning
paths:
  - firmware/**
---
# Release checklist (run this when a feature ships in a new firmware version)

When you bump `FW_VERSION` and the change is **confirmed working on the device**, do the FULL ship — not
just the flash — so the docs never drift:

1. **Close the issue** — the shipping PR's `Closes #<N>` closes it on merge; confirm it's closed. (The
   roadmap is [GitHub Issues](https://github.com/potgieterdl/esp32-claude-mon/issues), not a file.)
2. **Add it to README "Features"** — one concise, user-facing bullet (a benefit, not an internal task).
3. **ADR (only if key)** — if the change was architectural / hard-to-reverse / a "tried X, chose Y" worth
   keeping, add `adr/NNNN-title.md` per [`adr/README.md`](../../adr/README.md). Skip for routine features/fixes.
4. **Archive + record** — copy `firmware/.pio/build/esp32-c6/firmware.factory.bin` → `firmware/releases/vX.Y.Z.bin`;
   add a row to `firmware/releases/README.md`; bump "Latest good" in `CLAUDE.md`.
5. **Merge + tag + push** — merge the feature PR into **`main`** (its `Closes #<N>` auto-closes the issue),
   tag the merge commit `fw-vX.Y.Z-good`, push `main` (+ the tag) to `origin`.

> Branch model: feature work happens on a `feature/<issue#>-slug` branch and merges into **`main`** via a PR
> that `Closes` the issue (see [`CLAUDE.md`](../../CLAUDE.md) → *Roadmap & issues*). The local `master` branch
> is the frozen pre-publish archive — **never** push `master` or its old `v*`/`fw-v*` tags.

Net effect: **GitHub Issues** = only what's left · `README` = what's delivered · `adr/` = why · `firmware/releases/` = how to roll back.
