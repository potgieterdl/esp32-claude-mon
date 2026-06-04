---
description: Shipping a firmware version — release checklist that keeps the docs self-cleaning
paths:
  - firmware/**
---
# Release checklist (run this when a feature ships in a new firmware version)

When you bump `FW_VERSION` and the change is **confirmed working on the device**, do the FULL ship — not
just the flash — so the docs never drift:

1. **Clean `todo.md`** — remove the delivered item from the roadmap (it's no longer "next").
2. **Add it to README "Features"** — one concise, user-facing bullet (a benefit, not an internal task).
3. **ADR (only if key)** — if the change was architectural / hard-to-reverse / a "tried X, chose Y" worth
   keeping, add `adr/NNNN-title.md` per [`adr/README.md`](../../adr/README.md). Skip for routine features/fixes.
4. **Archive + record** — copy `firmware/.pio/build/esp32-c6/firmware.factory.bin` → `firmware/releases/vX.Y.Z.bin`;
   add a row to `firmware/releases/README.md`; bump "Latest good" in `CLAUDE.md`.
5. **Commit + tag + push** — commit on **`main`**, tag `fw-vX.Y.Z-good`, push `main` (+ the tag) to `origin`.

> Branch model: we develop on **`main`** (the published single-commit-onward history). The local `master`
> branch is the frozen pre-publish archive — **never** push `master` or its old `v*`/`fw-v*` tags.

Net effect: `todo.md` = only what's left · `README` = what's delivered · `adr/` = why · `firmware/releases/` = how to roll back.
