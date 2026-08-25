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

## 5. Vulkan headers (task 37, Phase 4)

- **Upstream:** https://gitlab.khronos.org/vulkan/vulkan (Arch source package: https://archlinux.org/packages/extra/any/vulkan-headers/)
- **Pin:** Arch package **vulkan-headers 1:1.4.357.0-1** — EXACT version match with the installed
  loader (`vulkan-icd-loader 1.4.357.0-1.1`; header API == loader ABI).
- **Fetch recipe (O5-N2 checksummed regime):**
  ```
  URL=$(pacman -Sp vulkan-headers)        # https://archlinux.cachyos.org/repo/extra/os/x86_64/vulkan-headers-1:1.4.357.0-1-any.pkg.tar.zst
  curl -fsSLO "$URL"
  tar --zstd --force-local -xf vulkan-headers-1:1.4.357.0-1-any.pkg.tar.zst -C <dir>
  cp -r <dir>/usr/include/vulkan <dir>/usr/include/vk_video vendor/vulkan/include/
  find vendor/vulkan/include \( -name '*.hpp' -o -name '*.cppm' \) -delete   # C-API backend: drop ~20MB C++ bindings
  cp <dir>/usr/share/licenses/vulkan-headers/MIT.txt vendor/vulkan/LICENSE
  ```
- **Layout:** `vendor/vulkan/include/{vulkan,vk_video}` — plain `-Ivendor/vulkan/include` on the
  VK leg only. 22MB package → 1.6MB vendored: 34 C headers + LICENSE = 35 files.
  The `*.hpp`/`*.cppm` C++ bindings (vulkan.hpp, vulkan_raii.hpp, modules) are DROPPED —
  this backend is C-API only.
- **Loader NOT vendored** — links system `libvulkan.so.1` via `-lvulkan`.
- **License:** MIT (upstream MIT.txt vendored as `vendor/vulkan/LICENSE`).

### Manifest

```
7e3a1ce177c12546d410f3179ce1b81f2da7a8eba4f525b29cd563ab4099c0e5  vendor/vulkan/include/vk_video/vulkan_video_codec_av1std_decode.h
8d166b4543260a38347860443b1a59c7a5e86cdb0d0facaf4c704f667de030e3  vendor/vulkan/include/vk_video/vulkan_video_codec_av1std_encode.h
c75c1d324b97d247aef0008024bc7f3adb98c741ab7f66c882ec38fdacc7ee33  vendor/vulkan/include/vk_video/vulkan_video_codec_av1std.h
37b970c3d80536ad1ac074cfe19b58ee03c3075378e02179e1dc5e4351266821  vendor/vulkan/include/vk_video/vulkan_video_codec_h264std_decode.h
227e092b53c4e7ca1a948ed021704511c1ed16040cd6188ff6703e5ae66db64d  vendor/vulkan/include/vk_video/vulkan_video_codec_h264std_encode.h
fded484cef9f90fbbd089e0647268f36e5f4292179fbc5428a2c9e8d7709bbe9  vendor/vulkan/include/vk_video/vulkan_video_codec_h264std.h
879a0dd370a1b1ad184638906c52bdfe7d80d639fbc5a5baa8b02d7c5b60b147  vendor/vulkan/include/vk_video/vulkan_video_codec_h265std_decode.h
abb3e72af22e4e0a3dbe5dff7be1b275949388809fb3987830341e8f495ea1c7  vendor/vulkan/include/vk_video/vulkan_video_codec_h265std_encode.h
0b81f8986ada015ef2e127449eff9d9634899bb59a0a9277a4054e9ec4416c12  vendor/vulkan/include/vk_video/vulkan_video_codec_h265std.h
d2e7caa396c521d03d491a269572b1d31b925b5127d22fda14199951ebae89f8  vendor/vulkan/include/vk_video/vulkan_video_codecs_common.h
1ceb1a8d0e3370e508cf688a6e57dc314cd82186b60f3cef420ea4b1b483865d  vendor/vulkan/include/vk_video/vulkan_video_codec_vp9std_decode.h
0a47125865376a3fe7014b69ff6db9d04e30ebf8c4d15664f1d894649ad5c09d  vendor/vulkan/include/vk_video/vulkan_video_codec_vp9std.h
ba22828f76de7231e7dfbb5d8564e7a58dd9f5dbeac8c1f0ff6c056c92db744a  vendor/vulkan/include/vulkan/vk_icd.h
a9fb1343e7e1fa3456189bf146f2186b8df46c1b1a01f88f8b9ae4cd25b1623c  vendor/vulkan/include/vulkan/vk_layer.h
a2cd9085c66776845d2524c3b0e73ccf4827ba046aa6f676c4b8ddbed47d6552  vendor/vulkan/include/vulkan/vk_platform.h
9dcce545a790b5b1ce00e103ade95c8efa4c8a0bf2ff39865d76a7d2f987aef2  vendor/vulkan/include/vulkan/vulkan_android.h
bb43577a445c357c3f0c03572b3dd3ebd9fca39914c0092f7d77e942af05d3fc  vendor/vulkan/include/vulkan/vulkan_beta.h
a7ac0d7e35e77f642c175af5d36729cfdae2d626d2aaa9145c030a42bd44edc6  vendor/vulkan/include/vulkan/vulkan_core.h
efecf5a15380f61c16ef257ed55a998f2cc651789bec22056ccc81d2518daefc  vendor/vulkan/include/vulkan/vulkan_directfb.h
17aedccaae68825bfa2954faec1bc9e953b106f1161e1b2ab26ab4cdccf21a06  vendor/vulkan/include/vulkan/vulkan_fuchsia.h
b68cbbf19b9397ee63dd6ba94526059bd2ff000c66083243758e3a04e215bd2c  vendor/vulkan/include/vulkan/vulkan_ggp.h
096f50152d0b298d8df84a561a37d8190c8a60af34dff0e2cc8328b24491a640  vendor/vulkan/include/vulkan/vulkan.h
ebaeffc3f4ec0484dcf34753678f1642caf73f6db4e17f11f1b82302151b918c  vendor/vulkan/include/vulkan/vulkan_ios.h
3ae5522081741e9021be86727e11949ad8c695e66f75e1c422e577afd7657f59  vendor/vulkan/include/vulkan/vulkan_macos.h
d5fe0caf881cc9c72ea2ba31ca86c684e97cdec6741d6e6298ecbd8f58dc8c5e  vendor/vulkan/include/vulkan/vulkan_metal.h
3cc40f845b5fd75cda0d8a3b1f2782522a3591657f67c656a211b4639eb23b16  vendor/vulkan/include/vulkan/vulkan_ohos.h
b9e7e7921b4be199c8ecfe518bd71bd8e533adbbab193714c542157b87b10e86  vendor/vulkan/include/vulkan/vulkan_screen.h
ac106317c017d1975f26184d3979dba0352f10aa1cfc7ef5373be92fef6bd137  vendor/vulkan/include/vulkan/vulkan_ubm.h
a53e35bb1b3113e6ef4932cb9358a27861a198d7865227ecd93555fecc73dd68  vendor/vulkan/include/vulkan/vulkan_vi.h
6c4146149d45bcbb5a22c7540feaa396ee9a49f077737c7a343c62de5199fbc7  vendor/vulkan/include/vulkan/vulkan_wayland.h
72f0b6de71287d3b04d12235e4f2fef8f343126f85be4963c516d31d5bb4c099  vendor/vulkan/include/vulkan/vulkan_win32.h
3d49f5eb52090e72e1cf7cde545088225ac524a2ff1d1f35737e7d944474c1b7  vendor/vulkan/include/vulkan/vulkan_xcb.h
3c44e97d3f380eb912e01a79e9667457fdf5e18164f0b09b3c61431f421bc551  vendor/vulkan/include/vulkan/vulkan_xlib.h
188233d112d812cca1777cfa6c1585073f1f2b033fa08b2f11c8d0757fcc232e  vendor/vulkan/include/vulkan/vulkan_xlib_xrandr.h
1ca3502222d967f3be5751c55f6b7ee735b5383909c3b501495f54b216dbf227  vendor/vulkan/LICENSE
```

## Verification Record (2026-08-24, task 37)

- [x] sha256 manifest §5 computed from on-disk files; `sha256sum -c` passes **35/35 OK**
- [x] `vulkan_core.h` header version macros: `VK_API_VERSION_1_4` / header patch **357**
      (`VK_HEADER_VERSION_COMPLETE` = 1.4.357) — matches installed loader 1.4.357.0-1.1
- [x] Zero `*.hpp`/`*.cppm` under vendor/vulkan (C-API only)
- [x] Supersedes the 2026-08-23 record line "No Vulkan loader/headers vendored" (that was
      true at task 30; Phase 4/task 37 vendors headers, loader stays system `-lvulkan`)

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
