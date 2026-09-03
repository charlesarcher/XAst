# Flavor-stamp fix — stale-binary trap (BACKEND switch executes foreign-flavor binary)

Date: 2026-09-03 (UTC stamp 20260903T114553Z) · branch `rendering-abstraction-plan` · macOS arm64

## What was broken

`make BACKEND=MTL run` executed the WRONG binary. All four backends (X11|GL|VK|MTL)
link to the same output name `XAsteroids` with no flavor marker anywhere make can
see. The root `./XAsteroids` on disk was a stale **VK-flavored** build
(otool -L → `libvulkan.1.dylib`; see `baseline-otool.txt`). Every `obj/MTL/`
prerequisite was already up-to-date, so make skipped the MTL link and ran the
stale VK binary:

```
./XAsteroids
vkBackend: glfwCreateWindowSurface failed: -3 (VK_ERROR_INITIALIZATION_FAILED)
XAsteroids: initialization failed.
make: *** [run] Error 1
```

(`baseline-mtl-run.log` — reproduced verbatim before any edit.)

## The fix (makefile only)

A GLOBAL flavor stamp, `obj/.backend` (FLAVOR_STAMP), records the flavor the root
binary was last linked under:

- `$(FLAVOR_STAMP): FORCE | obj` — recipe runs on every invocation but only
  touches the file when `BACKEND` differs from the recorded flavor (or the stamp
  is missing). On a switch: `echo $(BACKEND) > obj/.backend && rm -f XAsteroids`
  → the stale foreign-flavor binary is deleted and the link rule must rebuild it.
  On a same-flavor rerun: no-op → stamp mtime stable → no spurious relink.
- A per-obj-dir stamp cannot work: the thing that changed flavor is the shared
  root binary, not any per-flavor object dir.
- `$(FLAVOR_STAMP)` added as a prerequisite of ALL FOUR `XAsteroids` link rules
  (GL, VK, MTL, X11). All four link recipe lines are byte-identical to before.
- `clean` now also removes `$(FLAVOR_STAMP)`.
- Rationale comment + the 2026-09-03 incident are documented in the makefile.

Refinement vs the design skeleton: clean removes `$(FLAVOR_STAMP)` (the file in
obj/) rather than the whole `obj/` tree — removing the stamp fully satisfies
"clean removes the stamp" without changing anything else about clean's behavior.

## Matrix results (all green)

| Step | Command | Effect (from make stdout) | `qa/flavor-check.sh <B>` |
|---|---|---|---|
| 1 | `make BACKEND=MTL` | MTL link line shown (stale VK binary rm'd + relinked) | MTL → PASS (rc 0) |
| 2 | `make BACKEND=VK` | VK link line shown (relink forced) | VK → PASS (rc 0) |
| 3 | `make BACKEND=MTL` | MTL link line shown (relink forced) | MTL → PASS (rc 0) |
| 4 | `make BACKEND=MTL` (repeat) | **zero output** — silent skip, mtime before==after → no spurious relink | MTL → PASS (rc 0) |

The VK leg built fine on this Mac (homebrew glslc + pre-built SPIR-V in
obj/VK/spv), so the GL fallback leg was not needed. X11 leg skipped (no X11
headers on this Mac). Matrix logs: `matrix-1-mtl.log` … `matrix-4-mtl-same-flavor.log`
(+ per-step `*-flavor-*.txt` with full otool evidence).

## `make BACKEND=MTL run` — bounded real run

- `gtimeout 12 ./XAsteroids` → **exit 124 = process ALIVE at the 12 s mark and
  killed by the timeout (PASS, not a failure)**. Semantics recorded in
  `bounded-run-rc.txt`. `out2-rerun.log` head:

  ```
  mtlBackend: canonical window 688x702
  mtlBackend: 5 render pipelines created
  mtlBackend: initWindow OK (1376x1404 fb, scale 2.0)
  ```

- Which behavior happened: the GLFW window **initialized OK** (1376x1404
  framebuffer), but the backend's **nil-drawable guard** then skipped every
  frame draw (`mtlBackend: nil drawable — skipping frame` × ~10,400 lines in 12 s —
  the loop spins at ~870 fps, zero crashes). This is the known graceful
  headless/no-drawable path; no crash, abort, or trace markers anywhere in
  `out.log`/`out2-rerun.log`. Screenshot of the display at t≈5 s:
  `window-20260903-064751.png`.
- Interrupt probe: launched, `kill -TERM` after 4 s → **exit 143** (clean
  SIGTERM termination, no crash text, quick termination — `sigterm-probe-rc.txt`).

## mtlmethods self-test

`make BACKEND=MTL mtlmethods && ./obj/MTL/mtlmethods qa/evidence-.../mtl-identity.raw`
→ **exit 0**:

```
mtlBackend: 5 render pipelines created
mtlBackend: initWindow OK (1376x1404 fb, scale 2.0)
mtlmethods: wrote .../mtl-identity.raw (640x512, 2 text rects, 1310856 bytes total)
resize: 1024x768 ... MATCH
resize: 300x200 ... MATCH
mtlmethods: PASS
```

Byte count 1,310,856 matches the task-11 identity-gate golden exactly
(`mtl-identity.raw` retained here). Environment note: the one-shot binary in
obj/MTL/ resolves its metallib/fonts relative to its own directory, so the
gitignored build tree needed two runtime symlinks restored:
`obj/MTL/obj -> ..` and `obj/MTL/vendor -> ../../vendor` (same arrangement as the
task-11 identity gate). No product code touched.

## Out of scope (observed, NOT fixed)

Why the stale VK binary's `glfwCreateWindowSurface` failed with
VK_ERROR_INITIALIZATION_FAILED on this Mac is a separate lane's question
(VK-on-macos surface/ICD behavior); noted here for the record only.

## Files

makefile (fix) · qa/flavor-check.sh (reusable matrix guard) · this evidence dir.
Final state: `./XAsteroids` is MTL flavor (stamp = MTL) — user can re-run
`make BACKEND=MTL run` immediately. No game process left running.