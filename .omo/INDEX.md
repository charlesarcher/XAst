# .omo Artifact Index

Updated: 2026-08-23 (rounds 9+10 closure)

## Map

- `plans/` — execution plans (single source of truth for work sequencing)
  - `rendering-abstraction.md` — CURRENT plan, v6.4 FINAL (rounds-9/10 remediation incl. R9-M2 refutation record; USER GATE resolved: KEEP VULKAN; 48 contiguous tasks / 6 phases / Appendix wave schedule W0–W25; round-10 unit extraction lives in its receipt — 71 units, 23/17 waves). sha256 `ce059e6afc4ef1dee8f9e3a91dcf79e76a1600473e575fa71daea0c20ecbddfa` (1202 lines / 258817 bytes, 2026-08-23).
  - `rendering-abstraction-v3-archived.md` — superseded v3, byte-identical archive.
- `reviews/` — review receipts (frozen once cited; never edited post-citation)
  - `rendering-abstraction-high-accuracy-review.md` — round-2 evidence
  - `rendering-abstraction-hyperplan-review.md` — round-3 receipt
  - `rendering-abstraction-hyperplan-review-round4.md` + `round4-validator-findings.md` — round-4
  - `rendering-abstraction-round5-momus.md` / `rendering-abstraction-round5-oracle.md` — round-5 dual lanes
  - `rendering-abstraction-round8-momus-dependency.md` — round-8 (deep dependency-analysis mandate; waves W0–W25 source)
  - `rendering-abstraction-round9-momus.md` — round-9 general whole-plan verification (CHANGES_REQUESTED; R9-M2 refuted by planner reconciliation — see draft ledger)
  - `rendering-abstraction-round10-momus-parallelism.md` — round-10 max-parallelism unit extraction (NO BLOCKER; 71 units, W′ schedule, 23/17 waves)
- `drafts/` — state ledgers, one per plan (YAML front-matter status machine + newest-first Session Log)
  - `rendering-abstraction.md` — live ledger for the current plan
- `run-continuation/` — harness session-state JSONs (ephemeral)

## Retention rules

1. Receipts are immutable once cited by a plan or ledger — corrections land as NEW documents or ledger annotations, never in-place edits.
2. Archived plans stay byte-identical; old versions are never patched.
3. `run-continuation/` JSONs with mtime older than the current working day are deletable after confirming no live session references them.
4. Update this index at every plan version bump and review-round closure; a plan mutation always pairs with a fresh sha256 re-hash recorded in the draft ledger front-matter.

## Environment notes

- `.omo/` is intentionally untracked. Appending `.omo/` to `.gitignore` was user-authorized (2026-08-22) but falls outside the planning agent's enforced `.omo/*.md` write boundary — recorded as a pre-execution handoff todo in the draft ledger `pending-action`.
