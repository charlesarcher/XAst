# Round 5 — Native Momus Receipt (rendering-abstraction)

- **Lane:** momus (native) · **Round:** 5 · **Launch ID:** launch-r5-momus-01
- **Session:** ses_fd691310dffegmjHv4ViyxOAqT (bg_0d3a1df1)
- **Target:** .omo/plans/rendering-abstraction.md · **SHA-256:** fcdd54e3420a83530984abf6bbdf55c8743f145f3d7cdfd9a6a122f15baa3be2 (echoed by reviewer; 1045 lines; TL;DR present; 48 contiguous task rows verified)
- **Workspace root:** /home/archerc/code/XAst · **Runtime home:** null
- **Verdict: CHANGES_REQUESTED**

## Findings

### CRITICAL
**M5-C1 — Event-island line list undercounts `XRefreshKeyboardMapping`: 19 declared vs 22 actual; cascades to wrong "exact" totals in five downstream gates.**
Plan declares the playingField.H event island as "exactly 19" sites via line list (plan L605–607, L661) and D16 cites `XRefreshKeyboardMapping (:335)` only (L163). Repo fact: it occurs **4×** in playingField.H — :335, **:348, :474, :574** — all inside event-dispatch switches (nested LeaveNotify-spin switches, PlayTheGame title loop), outside the WM block (:539–566) and canvas lifecycle (:130–149): unambiguously island content task 25 must relocate. Consequences if executed literally: (a) task 15 acceptance "F1 sub-grep … = exactly 19 hits" fails on a correct migration (actual 22); (b) task 23 residual census "exactly 207+19=226" wrong (actual 229); (c) task 25 relocates only listed 19 and its acceptance grep (L681) omits `XRefreshKeyboardMapping`, so 3 residuals surface only at task 47 terminal sweep (L838–839), task 48 F1 sweep (L845), SC10 "exactly 226" (L863) — all formally failing; per the plan's own reopen protocol (L663) the executor is sent in circles. Non-executable as specified at these gates. Fix: add :348/:474/:574 to island list; 19→22 and 226→229 at L606/607/661/662/680/838/839/845/846/863; add `XRefreshKeyboardMapping` to task 25's forbidden-symbol grep. Migrated arithmetic must also reconcile: playingField 66 − 22 residual = 44 moved (not 47).

### MAJOR
**M5-M1 — Task 13 requires a running GL binary; the GL link rule and backend don't exist until tasks 29–31. Internal contradiction.**
Task 13 acceptance demands "GL binary: engine init + stub loop runs under Xvfb without a window crash" (L593); change text asserts "pre-25 the GL/VK binaries run engine init + a stub game loop" (L591). But GL link rule lands at task 29 ("first executable full link is task 31", L711); glBackend.H created at task 31 (L724); GLFW/glad vendored at task 30; task 28's GL leg is objects-only (L700); task 23 correctly asserts only the objects target (L661 item 4). Unsatisfiable in sequence. Fix: delete or relocate the clause to task 31.

### MINOR
**M5-N1 — USER GATE provenance cite drifted.** Plan L7 cites `.omo/drafts/rendering-abstraction.md:33` for "Vulkan is essential (user requirement)"; quote exists verbatim but has shifted (drafts file edited since pinning). Re-pin to current line at revision time.
**M5-N2 — Task 13 "190 domain sites" contradicts its own enumeration.** L592 says guard wrap covers 190 domain sites; same line enumerates 8+66+41+29+(20+2)+4+8+(2+1)+(3+2) = 186 (= 227 − 41 moved at task 12). Descriptive only.
**M5-N3 — Phase-4 excision recipe under-enumerates VK references (excisability itself holds).** L442/L767 claim deletion of tasks 37-43, Q11, VK rows of F2/F3, "the one SC row that names VK" suffices; actually SC1 (L854), SC3 (L856), SC7 ("all three backends", L860), task 44/46/48 rows also reference VK legs. Verified genuinely excisable — no Phase 0–3/5 task depends on a Vulkan artifact — but recipe misses rows.
**M5-N4 — Small cite/count drifts, none gate-affecting.** (a) GenHelpScreen list (L605) omits :651, lists continuation :664; 4+9=13 count correct. (b) button.H lbearing path cited ":140/143" (L126, L612); actual XTextExtents :137, usages :139/:142. (c) D9 cites stage.Refresh() at options.H ":2556" (L91); actual :2554. (d) Stage's single XTextWidth (:143) claimed by task 14 (L598) but unassigned between tasks 12/14 counts (49=41+8 leaves it out; end-state gate unaffected). (e) Task 4 XCreateBitmapFromData ordinals (L523) name 5 of 7 repo sites (unmentioned 2 = options.H:2450/:2474, safely inside D9 exception zone; census total 7 correct).
**M5-N5 — GL/VK TTF font assets never named (open choice, bounded).** D12 (L125–127) and tasks 30/31/33 mandate four stb_truetype substitutions but name no actual TTF files. Masked pixel font gate makes any readable substitution pass, but strictly an executor judgment call.

## Verification record
- Census reproduced EXACTLY: 56-symbol whitelist over *.H/*.C → 434 total, 16 files, every per-file count matches plan table L316–333. Exception arithmetic 146+11+47=207 ✓; domain 227 ✓.
- 50+ line-level anchors verified exact (fonts verbatim, island sites, WM block, key table, gravity guards, score.H, makefile, bitmaps/32 files, _CORP_LOGO_ 4-file swap, intersection2d.H 998 LOC, Options pump, XtAppMainLoop/XtAppInitialize absent, strstream/seekp sites).
- Traceability audit (20+ ids sampled across B1–B9, R4-M*, R4-N*, U*, m*): every claimed fix exists in phase bodies; id sets confirmed real in round-3/round-4 receipts.
- Consistency: TL;DR = SC13 = index = rows = 48; API 24 methods verified by count; F/Q/SC sets present and coherent.

## Overall judgment
"This v4 plan is the rare large refactor plan whose factual substrate almost entirely survives adversarial re-measurement… The two defects blocking approval are narrow but sit exactly where the plan claims maximal strength — its 'exact, machine-gated' numbers: the event-island line list drops three real XRefreshKeyboardMapping sites, corrupting the derived totals asserted at tasks 15, 23, 47, 48 and SC10, and task 13 carries one acceptance bullet that contradicts the plan's own build sequencing. Both are mechanically repairable in a single revision pass … Fix M5-C1 and M5-M1 and this plan is execution-ready; the MINOR items are polish and can ride along in the same pass."

---
*Receipt persisted by Prometheus from the reviewer's terminal message, 2026-08-22. Full transcript: opencode session ses_fd691310dffegmjHv4ViyxOAqT.*
