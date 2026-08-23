# Momus Review — Round 9 (general whole-plan verification)

- **round_id:** 9
- **launch_id:** launch-r9-momus-01
- **Target:** `.omo/plans/rendering-abstraction.md` (v6.3 FINAL)
- **Pre-flight:** sha256 `08f0e8c28222328de15280f77d71f137857fdaddb9043b916fae48a59551bff1` ✓ · 1179 lines ✓ · 253024 bytes ✓ — **MATCH, review proceeded.**
- **Scope honored:** read-only review; no plan/draft/INDEX/receipt/product file edited. Settled owner decisions (KEEP VULKAN, D9 ImGui menus, S2/D17.4 letterbox) reviewed for PROPAGATION only.

## VERDICT: **CHANGES_REQUESTED**

0 CRITICAL · 4 MAJOR · 6 MINOR. The plan's skeleton, census, API arithmetic, S1/S2 propagation and round-8 seam intents are sound and source-verified; the four majors are residual contradictions inside round-8-touched regions (task 13 wrap-set arithmetic, task 23 island enumeration, the W6 backend-header-writer cluster, and the unscheduled 45a golden capture) that cause gate failure/rework if executed as written.

## Findings

### R9-M1 (MAJOR) — Task 13 Change(ii) wraps the wrong set: stale "227 − 19 event-island"
- **Cite:** plan:623 — `"...bullet 3+2 of the 434-site census — the 227 − 19 event-island sites minus what 12 moved) is **wrapped in \`#ifdef X11_BACKEND\`**"`
- **Why it breaks:** M5-C1 corrected the island to **22** sites (plan:1104), and task 15's canonical list (plan:639) plus task 13's own Actual-calls (plan:624: "the 22-site event island IS part of the wrapped set … 8+66+41+29+22+4+8+3+5 = 186") both include it. Change(ii) as written EXCLUDES a 19-site island from the guard wrap, so ~19–22 raw X11 event calls stay visible to the GL/VK preprocessor and task 13's first-mandatory-green acceptance (`make BACKEND=GL objects`, plan:625) fails → rework on the riskiest single-commit cutover.
- **Fix (paste-ready):** replace `— the 227 − 19 event-island sites minus what 12 moved` with
  `— all 227 domain sites minus the 41 moved into the backend at task 12 (= 186; the 22-site event island is part of the wrapped set, guarded raw, untouched until task 25)`.

### R9-M2 (MAJOR) — Task 23's "exact" island list drops site :477 (21 lines ≠ 22 claimed)
- **Cite:** plan:694 — `"the exact line list from task 15: :332/:333/:335/:338/:340/:342/:348/:359/:406/:453/:468/:474/:572/:574/:577/:579/:587 + :286/:296/:301/:329" = 229 sites`
- **Why it breaks:** that enumeration contains **21** lines; task 15's list (plan:639) has **22** including `XAutoRepeatOn/Off ×6 (:338/:342/:468/:477/:577/:579)` — `:477` is missing. Source-verified: `playingField.H:477` = `XAutoRepeatOn`. Task 23's gate asserts "exactly 207 + 22 = 229 … exact, not a range", so the Phase-1 exit sweep reports 22 hits against a 21-line declared list → gate self-contradiction / unresolvable red.
- **Fix (paste-ready):** insert `/:477` after `:468` → `:332/:333/:335/:338/:340/:342/:348/:359/:406/:453/:468/:474/:477/:572/:574/:577/:579/:587 + :286/:296/:301/:329` (18 + 4 = 22).

### R9-M3 (MAJOR) — x11Backend.H writer contradiction cluster across the W6 nine-wide wave
- **Cites:** plan:575 — `"tasks 16-22 add NO new \`x11Backend.H\` methods — they CONSUME \`drawTexture\`/\`drawTextureMasked\` (implemented at 17-18); their Files listings were defensive. Task 15 is the sole real backend-header writer"`; plan:651 — task 17 Files "`x11Backend.H` (the \`createTextureFromBitmap\`/\`deleteTexture\` X11 implementations)"; plan:658 — task 18 Files "the \`drawTextureMasked\` X11 implementation"; plan:630 — task 14 Files "`x11Backend.H` (the \`endFrame\` blit implementation)"; plan:895 — Appendix W6 "zero backend-header edits".
- **Why it breaks:** three mutually inconsistent statements govern who edits the header during NINE-WIDE W6: the note says 16–22 make zero edits and calls their Files lines defensive, yet its own parenthetical says the methods are "implemented at 17-18", tasks 14/17/18's cards still instruct header implementations, and W6's note says "zero backend-header edits" while the same note-block says task 15 IS a writer. Literal-card execution puts ≥3 concurrent writers on one header-inline file → merge conflicts / duplicate method definitions → wave rework. ("Implemented at 17-18" also contradicts task 12, which already implements all 27 pass-through methods, plan:616.)
- **Fix (paste-ready):** in plan:575 change `(implemented at 17-18)` → `(implemented at task 12)` and extend the sentence to `their — and task 14's — Files listings were defensive`; in plan:895 change `zero backend-header edits` → `single backend-header writer (task 15); zero other backend-header edits in 14-22`.

### R9-M4 (MAJOR) — Appendix wave schedule never schedules 45a (pre-task-24 golden capture)
- **Cites:** plan:906 — `W16 | 37, 44a, 45b`; plan:917 excised variant — `W16 carries 44a + 45b alone`; vs plan:857 — "**45a** — numeric GOLDEN CAPTURE on the PRE-task-24 tree … **45b** — the lane itself … DIFFING against the 45a goldens"; plan:885 — waves are a complete view where "no row is added, removed, or reordered".
- **Why it breaks:** 45a appears in NO wave W0–W25 (nor the excised variant). Goldens must exist before task 24 (W8); an executor running the authorized parallel schedule reaches 45b at W16 with no goldens → numeric lane fails or forces improvised reconstruction of a pre-task-24 tree (rework).
- **Fix (paste-ready):** change the W2 row to `| W2 | 3, 4, 9, 30, 45a | 30 hoisted (R8/S2); 9 needs 7+8; 45a rides task 9's baseline session on the pre-task-24 tree (R8-M2) |` (W16 unchanged; quantification unchanged).

### R9-N1 (MINOR) — Stale "24-virtual API" in live task-12 body
- **Cite:** plan:616 — "the 24-virtual API deliberately does not expose raw X11 resources". Current API is 27 (21+6); every other live statement says 27. **Fix:** "the 27-method API deliberately does not expose raw X11 resources".

### R9-N2 (MINOR) — Task 16 kept pre-correction lbearing cites ":140/143"
- **Cite:** plan:645 — "the per-character \`XTextExtents\` lbearing path at \`button.H:140/143\`". M5-N4 corrected these to call :137 / uses :139/:142 (plan:132); source confirms (`button.H:137` call; lbearing uses :139/:142). **Fix:** ":137 call, uses at :139/:142".

### R9-N3 (MINOR) — W3 note mislabels task 11's deliverable
- **Cite:** plan:892 — `10, 11 | score.H fixes + x11Backend.H skeleton`. Task 11 creates `renderingEngine.H` (plan:608); x11Backend.H lands at task 12/W4. **Fix:** "score.H fixes + RenderingEngine interface header".

### R9-N4 (MINOR) — W5 note mislabels task 13 as "rotatorDisplayData migration"
- **Cite:** plan:894 — `13 | rotatorDisplayData migration (lettered 13a/13b if split)`. Task 13 is the DI cutover + per-header body-guard wrap; rotatorDisplayData is D14 exception-zone (include-guards task 4, `#else` branches task 27) and is never migrated at 13. **Fix:** "DI cutover + per-header body guards (lettered 13a/13b if split)".

### R9-N5 (MINOR) — Sort-range cite drift between D17.5 and task 45(a)
- **Cite:** plan:857 — "swept-intersect sort (\`:754-766\`)" vs plan:189 — "\`:754-763\`". Source: sort block ends :763; :766 opens the next loop (`utilities/intersection2d.H` inspected). **Fix:** align task 45(a) to `:754-763`.

### R9-N6 (MINOR) — Task 46 commit prefix violates the stated convention
- **Cite:** plan:868 — commit `` `fix: GL/VK edge cases + long-session stability…` `` on a "(verification only)" row, while plan:472 reserves `fix:` for behavior bugs and records the sole prefix exception for task 1. **Fix:** prefix `qa:`.

## Checked clean (positively verified)

1. **Pre-flight:** hash/line/byte counts exact; ledger's re-hash record matches disk.
2. **Structure:** exactly 48 rows matching `^- [ ] N.`, contiguous 1–48, all column-zero; section order TL;DR(:9)→Scope(:13)→Design Decisions(:43)→RenderingEngine API(:192)→Phases(:470–882)→Appendix waves(:883)→Success Criteria(:921)→Round-8 fixes(:1139)→Discrepancies(:1156) intact; every row carries Files/Change/Actual-calls/Acceptance/QA/Commit.
3. **Census replicated independently** (56-symbol whitelist grep over all *.H/*.C): **434 sites / 16 files**, and every per-file number matches the table exactly (146/66/49/47/41/29/20/11/8/4/3/3/2/2/2/1); whitelist sums 434; category sum 98+172+97+31+18+13+5=434; rotatorDisplayData.C=146 + compositePixmap.C=11 ✓.
4. **Split arithmetic:** 207 exception (146+11+47+3) + 227 domain pre-task-25; 229 + 205 = 434 post-task-25 — consistent at plan:409/:440/:694/:695/:713/:734/:871–873/:879/:934; SC10's 146+11+47+3+22=229 ✓.
5. **Island composition = 22 verified line-by-line in playingField.H** (all anchors incl. :477; XRefreshKeyboardMapping ×4 at :335/:348/:474/:574).
6. **API count:** "27 = 21 core + 6 seams" consistent at :11/:16/:70/:192–194/:609–613; sketch contains exactly 27 pure-virtual + dtor (= task 11's grep=28 gate). Residual "26 methods"/"19 core"/"24 virtual" appear ONLY in historical provenance/traceability/assembly notes (:1094/:1128/:1133/:1173) except R9-N1.
7. **Round-8 seams:** (a) task 36 Q5 scoped to the X11/Motif leg at :793/:795/:796 + W15 note :904; no ungreenable "Q1-Q9 green" acceptance anywhere; (b) 45a captured PRE-task-24/D8-split, executed with task 9's session; 45b wording is diff-only — no post-D8 capture residue; (c) task 9 uses frame-completion-handshake language per D17.3/O5-M3 (:595), no wall-clock schedule; (d) hoists 26→W1 / 30→W2 match task-body amendments (:719/:751); barrier exemptions for build-only 29 and vendoring 30 stated (:735); (e) 44a/44b split vs Phase-4 concurrency mutually consistent (:848–854 ↔ :906/:912/:917); (f) covered by R9-M3 (not clean).
8. **S1/S2 propagation:** no live text asserts reversed rulings — grayed-out-menu / min=max pin / "resize dropped" appear only inside reversal statements or historical tables (:92,:98,:187,:989,:1094,:1128,:1136); README instructions match consequences (delete grayed-out note, document menus+resizing); errorInfo X11-only rule intact (D12/tasks 12/31/33).
9. **Dependency spot-checks (tasks 9, 24, 26, 30, 36, 44a/b, 45b):** listed deps exist as task ids, no forward reference to later-phase artifacts, acceptances achievable at scheduled positions — sole exception R9-M4 (45a unscheduled).
10. **Decision-completeness sample (14 tasks: 1, 2, 5, 7, 9, 10, 12, 13, 15, 24, 26, 30, 36, 44):** exact paths, exact commands/gates (`make V=1`, `compare -metric AE`, ASan, grep gates), Must-NOT-Have respected (e.g., task 12 pollEvents partial scope; task 13 GL-binary clause deleted; task 36 Q5 exclusion), QA rows name tool + invocation + evidence path; commit lines present on all 48 rows.
11. **Cross-ref integrity:** ~45 file:line cites verified against sources across D1–D17 and tasks 2–5, 7, 10, 13–23, 47 (playingField.H, stage.H, options.H, button.H, shipGroup/shipYard/rockGroup/bullet/enemyGroup/enemyBulletGroup/shipBulletGroup/explosion/explosionGraphic, rotatorDisplayData, compositePixmap.H, utilities/box.H, intersection2d.H, score.H, XAsteroids.C, makefile, README) — all contain claimed content except the two drifts R9-N2/R9-N5. Highlights: XSetFunction GXor :135; uSecondsPerFrame=62500 :98; clears :202/:312/:638; XDrawString 4+9=13; stage fonts :134–138 & formula :151–159 & border 5px :186; srand :218–219; XFreeFont×4 leak :231–234; Options pump :2542–2571 (+ :2543/:2547/:2548/:2550/:2554/:2560/:2568); XtAppMainLoop absent (grep=0); B2 six sites :69/:300/:457/:565/:591/:616; score.H defects :68-69/:71-85/:108-110; strstream sites all verified; makefile:2/:6-14/:15/:18-19 and 22-line size; button Drawable& :160/:208/:45, CreateButton 0 XCopyArea; shipGroup 8th site XAllocColor :222; gravity zero-distance guard :222.
12. **Stale-string sweep:** `post task 10` / `t0 + i*62.5ms` hit ONLY their intentional finding-quote rows (:1145/:1151); `Q5 (both legs)`, `vendored snapshot`, `golden set commits`, `Q1-Q9` — zero body hits.
13. **USER-GATE annotations** present at the four pinned decision points (:7, :798, :800, :905); VK excision recipe internally consistent (SC13 formula wording, O5-M2 "every backend leg present at run time").
14. **F1 gate mechanics:** whitelist regex (:398) matches the 56-symbol enumeration; exception-zone paths cited exist in-tree; Gate-2 preprocessing command well-formed.

### Deviations from review-contract expectations (recorded, NOT counted as plan defects)
- No task row carries a nested "Recommended task executor category:" annotation, and there are no `- [ ] F<n>.` final-verifier rows (task 48 is the final verification wave as a normal row). Greps of `.omo/drafts` + all rounds-2–8 receipts find NO requirement for either grammar element, and the round-8 execution ledger enumerates all applied edits without them; their absence does not impede execution (tasks are fully specified). Flagged here so silence is not mistaken for omission.

### Watch-items (non-blocking)
- Concurrent makefile editors within one wave: W2 (9 ∥ 30) and W16 (37 ∥ 44a) — different regions of a 22-line file, likely auto-mergeable; a one-line sequencing note would mirror R8-N4.

## Scorecard

| # | Dimension | Result |
|---|-----------|--------|
| 1 | Pre-flight hash/lines/bytes | PASS |
| 2 | Structure (48 rows, order, grammar) | PASS (executor-category/F-row contract elements absent — see deviations) |
| 3 | Census arithmetic 434/16 + splits 207/227·229/205 | PASS (independently replicated) |
| 4 | Island count 22 + line-level verification | PASS sources; FAIL task-23 enumeration (R9-M2); stale 19 at task 13 (R9-M1) |
| 5 | API count 27=21+6 everywhere | PASS (R9-N1 residual) |
| 6 | Round-8 seams a/b/c/d/e | PASS |
| 7 | Round-8 seam f (W6 writers) | FAIL (R9-M3) |
| 8 | S1/S2 propagation | PASS |
| 9 | Dependency soundness (sampled 8 tasks) | PASS except 45a scheduling (R9-M4) |
| 10 | Decision completeness (14 tasks sampled) | PASS |
| 11 | Cross-ref integrity (~45 cites) | PASS (2 minor drifts: R9-N2, R9-N5) |
| 12 | Stale-string sweep | PASS |
| 13 | Hygiene (prefixes, labels) | MINOR findings only (R9-N3/N4/N6) |

---

**One-line verdict:** `R9 VERDICT: CHANGES_REQUESTED — v6.3 (sha 08f0e8c2…51bff1, 1179/253024) is census-sound and propagation-clean, but 4 MAJOR residuals (task-13 "227−19" wrap-set, task-23 island list missing :477, W6 x11Backend.H-writer contradiction cluster, unscheduled 45a goldens) must be fixed before execution; 0 CRITICAL / 4 MAJOR / 6 MINOR.`
