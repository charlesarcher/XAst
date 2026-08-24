# Vendor Dependency Pins — XAst Rendering Plan

Task 30 (download+pin portion) of `.omo/plans/rendering-abstraction.md`.
Regime: O5-N2 — pinned SHA + checksummed fetch + offline-first build.
All checksums are sha256. Re-verify with: `sha256sum -c` against the manifest below
(from repo root, after stripping paths into a check file).

## 1. GLFW

- **Upstream:** https://github.com/glfw/glfw
- **Pin:** system GLFW **3.5.1** (`pkg-config --modversion glfw3` = 3.5.1; `libglfw.so.3` strings confirm `3.5.1`)
- **Strategy:** headers-only vendored copy; **links system `libglfw.so.3` (version 3.5.1)** at build time via `-lglfw`. System version ≥ 3.3, so no prebuilt tarball was downloaded.
- **Header version macros:** `GLFW_VERSION_MAJOR 3 / MINOR 5 / REVISION 1` (matches system lib)
- **Source of headers:** `/usr/include/GLFW/` (pkg-config `--variable=includedir glfw3` = `/usr/include`)
- **License:** zlib/libpng license (per upstream `LICENSE.md`; headers carry no separate license block)

### Manifest

```
95ecc16e4875bca18cff863232d5dbb623f3457b05f07efd26fe8bc8a06345b6  vendor/glfw/GLFW/glfw3.h
d301085c2a998345c0f80127d8e67d4394f1c7b12ca9c2cf3b73b7fa15c804ae  vendor/glfw/GLFW/glfw3native.h
```

## 2. glad (OpenGL loader)

- **Upstream generator:** https://github.com/Dav1dde/glad (v1 lineage)
- **Generator version:** glad **0.1.36** (run via `uv tool run --from 'glad<2' glad ...`)
- **Generation command:**
  `glad --generator c --out-path vendor/glad --api 'gl=4.5' --profile core`
- **API/Profile:** `gl=4.5`, profile **core** — header comment states `APIs: gl=4.5`, `Profile: core`.
  **NOT 4.6** (B5/m17: GL 4.6 bricks Mesa/Intel 4.5 drivers).
- **Extensions:** all extensions known to the bundled spec at generation time (loader only resolves what the app requests).
- **Layout note:** glad 0.1.36 emits `include/glad/glad.h`; the real header lives at `include/GL/glad.h` to match the plan's expected layout (`vendor/glad/include/GL/glad.h`). Because generated `src/glad.c` does `#include <glad/glad.h>`, a one-line forwarding shim exists at `include/glad/glad.h` → `#include "../GL/glad.h"`. Both spellings work with `-I vendor/glad/include`. The shim is local glue, not upstream content.
- **License:** generated files state MIT/WTFPL dual license (standard glad v1 output notice); spec data © Khronos Group (MIT/Khronos-free terms).

### Manifest

```
fc2379e521c10cef33e2e20efe99c5df6267597f5de2d8b5ffd4b854e4f01676  vendor/glad/include/GL/glad.h
7b1e01aaa7ad8f6fc34b5c7bdf79ebf5189bb09e2c4d2e79fc5d350623d11e83  vendor/glad/include/KHR/khrplatform.h
6d5d29d820e8defaaf5742079706757818126163973de8e1bd4a3dd1a4b9ba4a  vendor/glad/src/glad.c
<local-shim, not upstream content>  vendor/glad/include/glad/glad.h
```

## 3. stb_truetype

- **Upstream:** https://github.com/nothings/stb
- **File:** `stb_truetype.h` fetched from pinned master commit:
  https://raw.githubusercontent.com/nothings/stb/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_truetype.h
- **Commit SHA:** `2c980bb59875b0d32144a71867fbdebb2f77cd20` (master HEAD at pin time)
- **Version string in file:** `stb_truetype.h - v1.26 - public domain`
- **License:** public domain / MIT (dual, per file header and end-of-file license block)

### Manifest

```
ecd30b05e0dd4fea3a13c26810dd9e1992dc379049482c393d5a19e6b5090aab  vendor/stb/stb_truetype.h
```

## 4. Dear ImGui

- **Upstream:** https://github.com/ocornut/imgui
- **Tag:** `v1.92.9b` (latest stable release tag at pin time; non-docking line)
- **Commit SHA:** `f1cc2ae15e53a861a874c3034aae6798fde194ab` (shallow clone at tag)
- **Files copied:** core set per D9 regime — imgui.cpp, imgui.h, imgui_draw.cpp, imgui_internal.h, imgui_tables.cpp, imgui_widgets.cpp, imconfig.h, imgui_demo.cpp, imstb_rectpack.h, imstb_textedit.h, imstb_truetype.h, LICENSE.txt
- **License:** MIT (LICENSE.txt vendored alongside)

### Manifest

```
5755e1b8d6ab0d7811a9d7cac0f509878b51fc5ee77e09bd0f235bcb414971e7  vendor/dear_imgui/imconfig.h
5a1e5128c0305f50b6556a77f71fde35836505883b74c1d6ae44723243f1e8de  vendor/dear_imgui/imgui.cpp
8930ec6bb26844a3d24aa519a07f674ec666b8c22c4d8a74fa6cc128033ed494  vendor/dear_imgui/imgui_demo.cpp
83d30419a8e06a5f0a8692ee6de186f7ecfddecdc33769e76244837e2975b400  vendor/dear_imgui/imgui_draw.cpp
0d8db1045db01d908853adfd26ae07c5bc5ab4789d4515f6ea34234a69ade0ca  vendor/dear_imgui/imgui.h
efba9bccc971cc49ec3da3168968ba412c7ea89745c104a7f5c004c8349bc71f  vendor/dear_imgui/imgui_internal.h
b5deabe5b569ab712c11b6556562a646bf88327320fc60a451071b2a36498d72  vendor/dear_imgui/imgui_tables.cpp
a8a0b2f65caa5711467d9e3ca4028fded48329ea65e11ec92a968293dc2aa231  vendor/dear_imgui/imgui_widgets.cpp
889b396795202d1457560a797a7242e96f6f132d4b88ca2d69be58bf05e1771f  vendor/dear_imgui/imstb_rectpack.h
a985f5fa0ed97353d493b497961e9eef52082edcd045cf6954b69990ec9d0741  vendor/dear_imgui/imstb_textedit.h
c51a0f7e7ea760f2366bd3752635ec58e21fccfec4a832501639990ba6ce0528  vendor/dear_imgui/imstb_truetype.h
173506a2d6f7fb67990d257fb2507f188690eca39060c39469ae7bef43aae2a3  vendor/dear_imgui/LICENSE.txt
```

## Verification Record (2026-08-23)

- [x] sha256 manifest above computed from on-disk files; re-run passes byte-for-byte (18/18 OK)
- [x] `grep -c 'glGetString' vendor/glad/include/GL/glad.h` → **4** (> 0)
- [x] glad.h header comment: `OpenGL loader generated by glad 0.1.36`, `APIs: gl=4.5`, `Profile: core`
- [x] `ls vendor/dear_imgui/imgui.cpp` → exists
- [x] `gcc -c -Ivendor/glad/include vendor/glad/src/glad.c` → compiles clean (sole added TU)
- [x] No Vulkan loader/headers vendored (Phase 4 task 37 scope)
- [x] Plain files only — no CMake/submodule machinery (O5-N2)

## Consumers

| Dep | Consumed by |
|---|---|
| glad.c | GL backend (Phase 3) as sole added TU |
| stb_truetype.h | text atlas (task 33) |
| dear_imgui | menu adapter (task 44) |
| libglfw.so.3 (system) | window/context layer |

Makefile integration is owned by task 1 — intentionally absent here.

## Fonts (task 31, D12)

| File | Source | sha256 | License |
|---|---|---|---|
| vendor/fonts/DejaVuSans-Bold.ttf | /usr/share/fonts/TTF/DejaVuSans-Bold.ttf | `a4326ba7b4cb0907d6aa1ea8a2512ac99e80bcc95ea2bfc975a46ee1f1bfa405`* | DejaVu Fonts license — free, redistributable in-tree |
| vendor/fonts/DejaVuSansMono-Bold.ttf | /usr/share/fonts/TTF/DejaVuSansMono-Bold.ttf | see manifest below* | DejaVu Fonts license — free, redistributable in-tree |

\* authoritative hashes (recorded 2026-08-24 at vendoring time; DejaVu license text ships with the upstream package):
- DejaVuSans-Bold.ttf `b5d64817b6331723b5e59eaaa6db90057cbed58e9733f65687f110638192359f`
- DejaVuSansMono-Bold.ttf `738db66c1f30008ddc331e46ba373f0b28c9455f54f0174e4aee670d6999193a`
