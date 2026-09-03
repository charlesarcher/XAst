# BACKEND selects the compile configuration and object directory: X11 | GL | VK | MTL.
# Phase 0: only X11 links; GL/VK are compile-only (`make BACKEND=<B> objects`).
BACKEND?=X11

CXX=g++
CC=gcc
CXXFLAGS=-I/usr/include/X11 -O3 -Wall -Wextra -Wno-unused-parameter -std=c++17
LDFLAGS=-L/usr/lib/X11
X11_BACKEND=-DX11_BACKEND
OBJDIR=obj/$(BACKEND)

# Vendor trees (task 30, O5-N2 offline-first): plain -I paths, no submodule
# machinery. GL AND VK object legs only — the X11 leg must never see them
# (its compile line stays byte-identical to the pre-vendor baseline).
# No extra defines: dear_imgui v1.92.9b and glad 4.5-core compile with these
# includes alone (flags kept minimal per task 30; nothing added beyond -I).
VENDOR_INCS=-Ivendor/glad/include -Ivendor/stb -Ivendor/dear_imgui -Ivendor/glfw

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  GLFW_LIBDIR=$(shell brew --prefix glfw 2>/dev/null)/lib
  GLFW_CFLAGS=$(shell PKG_CONFIG_PATH=$(GLFW_LIBDIR)/pkgconfig pkg-config --cflags glfw3 2>/dev/null)
  GLFW_LIB=-L$(GLFW_LIBDIR) -lglfw -Wl,-rpath,$(GLFW_LIBDIR)
  OPENGL_LINK=-framework OpenGL
  VK_LOADER_GATE=:
  VK_LOADER_LIB=/opt/homebrew/opt/vulkan-loader/lib/libvulkan.dylib
  VK_MVK_FRAMEWORKS=-framework Metal -framework MetalKit -framework Foundation \
    -framework CoreGraphics -framework CoreVideo -framework CoreMedia -framework AVFoundation \
    -framework IOSurface -framework Quartz
  VK_LINK_EXTRA=$(VK_LOADER_LIB) $(VK_MVK_FRAMEWORKS) -Wl,-rpath,$(VK_LOADER_LIB:libvulkan.dylib=%)
  # MoltenVK ships its own vulkan headers; the repo's vendored copy still works
  # but we must not gate on a Linux-style vulkan-loader icd check.
else
  GLFW_LIB=-lglfw
  OPENGL_LINK=-lGL
  VK_LOADER_GATE=@qa/vk-probe.sh --link-check || exit 1
  VK_LINK_EXTRA=-lvulkan
endif

# --- Task 41 (revised): SPIR-V pre-compilation ---
# GLSL shaders in utilities/rendering/vkShaders/*.{vert,frag} are compiled to
# SPIR-V at BUILD time (not runtime) into obj/VK/spv/. This removes the
# runtime glslc dependency + /proc/self/exe shader path resolution entirely;
# the binary loads pre-built .spv blobs relative to its own directory.
VK_SHADER_DIR=utilities/rendering/vkShaders
VK_SPV_DIR=$(OBJDIR)/spv
# Explicit .spv output names (vert/frag share a base name, so frag gets _fs suffix).
VK_SPV_TARGETS=$(VK_SPV_DIR)/prim.spv $(VK_SPV_DIR)/prim_fs.spv \
               $(VK_SPV_DIR)/tex.spv $(VK_SPV_DIR)/tex_fs.spv \
               $(VK_SPV_DIR)/masked.spv
# glslc is found on PATH (Linux Vulkan SDK or homebrew shaderc on macOS).
# Flags: SPIR-V 1.5 for MoltenVK 1.4.2 compatibility + Vulkan 1.1/1.2 ICD.
VK_GLSLC=glslc
VK_GLSLC_FLAGS=--target-spv=spv1.5 --target-env=vulkan1.1

# --- Task 49+: MSL pre-compilation (Metal backend, Darwin-only) ---
# MSL shaders in utilities/rendering/mtlShaders/*.metal are compiled into a
# single .metallib at BUILD time via xcrun -sdk macosx metal. The binary
# loads the .metallib relative to its own directory.
MTL_SHADER_DIR=utilities/rendering/mtlShaders
MTL_METALLIB=$(OBJDIR)/aestroids.metallib
MTL_METAL=metal
MTL_METAL_FLAGS=-O

ifeq ($(BACKEND),X11)
BACKEND_CXXFLAGS=$(X11_BACKEND)
endif
# GL/VK legs: NO backend macro (task 13 removed the task-30 stopgap — a GL
# object that compiles X11 code paths is not green). The domain headers carry
# `#ifdef X11_BACKEND` body guards, so guards-closed compilation is the real
# first mandatory-green (D-A A3). Vendor -I paths ride on these legs ONLY.
# Task 27 landed the D14 engine `#else` branches, so the two D14 .C units
# (rotatorDisplayData/compositePixmap) now compile on EVERY leg: guards-closed
# on GL/VK (engine-rotation data path + CPU compositing), macro'd on X11.
# Since task 29 they are ALSO in every link list ($(GAME_OBJECTS)): their
# engine-branch symbols are referenced by the domain.
ifneq ($(filter $(BACKEND),GL VK MTL),)
BACKEND_CXXFLAGS=$(VENDOR_INCS)
endif
# Task 31: the GL leg compiles main()'s GL backend branch (GLBackend engine +
# paced stub loop). Task 43: the VK leg compiles main()'s VK backend branch
# the same way (-DVK_BACKEND rides the leg's objects exactly like -DGL_BACKEND
# rides the GL leg) — before 43 the domain stayed macro-free and main() fell
# through to the stub return 0.
ifeq ($(BACKEND),GL)
BACKEND_CXXFLAGS+=-DGL_BACKEND
endif
# Tasks 37/43: the VK leg carries the vendored Vulkan C headers (-I) plus the
# backend macro that opens XAsteroids.C/playingField.H VK branches. GL/X11
# compile lines stay byte-identical.
ifeq ($(BACKEND),VK)
BACKEND_CXXFLAGS+=-Ivendor/vulkan/include -DVK_BACKEND
endif
# Tasks 49+: the MTL (Metal) leg is Darwin-only — carries the backend macro
# that opens XAsteroids.C/playingField.H Metal branches. No Vulkan loader
# gate; Metal is a system framework on macOS.
ifeq ($(BACKEND),MTL)
  ifneq ($(UNAME_S),Darwin)
    $(error BACKEND=MTL requires macOS (Darwin))
  endif
  BACKEND_CXXFLAGS+=-DMETAL_BACKEND
endif

# Objects feeding the X11 link always carry the X11 macro, whatever BACKEND says.
obj/X11/XAsteroids.o obj/X11/rotatorDisplayData.o obj/X11/compositePixmap.o: BACKEND_CXXFLAGS=$(X11_BACKEND)

# The decode unit must stay macro-free on every leg that builds it.
$(OBJDIR)/xbmDecodeSelfTest.o: BACKEND_CXXFLAGS=$(VENDOR_INCS)

# Default goal (task 29): AutoRepeatOn is an X11-only utility (F1 exception
# zone (d)) — built on the X11 leg only. GPU legs build the game binary alone.
ifeq ($(BACKEND),X11)
all: XAsteroids AutoRepeatOn
else
all: XAsteroids
endif

# Compile-only per-backend objects; no link step (GL/VK link rules: tasks 29/37).
# The XBM decode self-test (task 26) compiles into the GL/VK legs ONLY: the
# decode unit is backend-agnostic, so building it there proves it compiles
# WITHOUT -DX11_BACKEND. X11 keeps its native XCreateBitmapFromData paths.
GLVK_OBJECTS=
ifneq ($(filter $(BACKEND),GL VK MTL),)
GLVK_OBJECTS=$(OBJDIR)/xbmDecodeSelfTest.o
endif

# Vendor TUs (task 30). dear_imgui core units compile into BOTH GPU legs —
# the menu adapter consumes only RenderingEngine types (D9), so no per-backend
# ImGui code exists outside glBackend/vkBackend. glad.c is the OpenGL loader:
# GL leg ONLY (Vulkan vendors its own loader/headers in Phase 4, task 37).
# Compiled as C with gcc (PINNED.md verification record used gcc; g++ would
# force .c through the C++ front end).
DEAR_IMGUI_UNITS=imgui imgui_draw imgui_tables imgui_widgets imgui_demo
IMGUI_OBJECTS=
GL_OBJECTS=
ifneq ($(filter $(BACKEND),GL VK MTL),)
IMGUI_OBJECTS=$(addprefix $(OBJDIR)/,$(addsuffix .o,$(DEAR_IMGUI_UNITS)))
endif
ifeq ($(BACKEND),GL)
GL_OBJECTS=$(OBJDIR)/glad.o $(OBJDIR)/stbTruetypeImpl.o
endif
# Task 44a/44b: the ImGuiOptionsMenu unit lands on BOTH GPU legs (44a = GL,
# 44b = VK). The seam header itself is include-guarded and only reaches TUs
# that ask for it.
MENU_OBJECTS=
ifneq ($(filter $(BACKEND),GL VK MTL),)
MENU_OBJECTS=$(OBJDIR)/optionsMenu.o
endif
# Task 38: the VK leg consumes stb font METRICS (pass-1 window sizing via the
# shared D15 formula) — the SAME impl TU as GL; glad stays GL-only.
ifeq ($(BACKEND),VK)
VK_STB_OBJECT=$(OBJDIR)/stbTruetypeImpl.o
endif
# Task 49+: the MTL leg consumes stb font METRICS the same way as VK (pass-1
# window sizing via the shared D15 formula); glad stays GL-only.
ifeq ($(BACKEND),MTL)
MTL_STB_OBJECT=$(OBJDIR)/stbTruetypeImpl.o
endif

.PHONY: objects
# All three game objects on EVERY leg (task 27: the D14 units' #else engine
# branches compile guards-closed on GL/VK, macro'd on X11) + the
# backend-agnostic self-test/vendor units on the GPU legs. The SPIR-V targets
# ride the VK leg only and the MSL metallib rides the MTL leg only, so the
# convenience target stays buildable on every leg.
GAME_OBJECTS=$(OBJDIR)/rotatorDisplayData.o $(OBJDIR)/compositePixmap.o
ifeq ($(BACKEND),VK)
objects: $(OBJDIR)/XAsteroids.o $(GAME_OBJECTS) $(GLVK_OBJECTS) $(IMGUI_OBJECTS) $(GL_OBJECTS) $(VK_STB_OBJECT) $(MTL_STB_OBJECT) $(MENU_OBJECTS) $(VK_SPV_TARGETS)
else ifeq ($(BACKEND),MTL)
objects: $(OBJDIR)/XAsteroids.o $(GAME_OBJECTS) $(GLVK_OBJECTS) $(IMGUI_OBJECTS) $(GL_OBJECTS) $(VK_STB_OBJECT) $(MTL_STB_OBJECT) $(MENU_OBJECTS) $(MTL_METALLIB) $(OBJDIR)/mtlCocoa.o $(OBJDIR)/mtlBridge.o
else
objects: $(OBJDIR)/XAsteroids.o $(GAME_OBJECTS) $(GLVK_OBJECTS) $(IMGUI_OBJECTS) $(GL_OBJECTS) $(VK_STB_OBJECT) $(MTL_STB_OBJECT) $(MENU_OBJECTS)
endif

$(OBJDIR):
	mkdir -p $(OBJDIR)

# SPIR-V pre-compilation — only relevant on the VK leg. The .spv files live
# in obj/VK/spv/ alongside the .o files; the binary locates them via its own
# directory at runtime (no /proc/self/exe, no runtime glslc).
ifeq ($(BACKEND),VK)
$(VK_SPV_DIR):
	mkdir -p $(VK_SPV_DIR)

# Explicit per-shader rules — two pattern rules for %.spv can't disambiguate
# prim.vert.spv from prim.frag.spv when the stem % maps to different source
# extensions.
$(VK_SPV_DIR)/prim.spv: utilities/rendering/vkShaders/prim.vert | $(VK_SPV_DIR)
	@echo "glslc $< -> $@"
	$(VK_GLSLC) $(VK_GLSLC_FLAGS) -o $@ $<
$(VK_SPV_DIR)/prim_fs.spv: utilities/rendering/vkShaders/prim.frag | $(VK_SPV_DIR)
	@echo "glslc $< -> $@"
	$(VK_GLSLC) $(VK_GLSLC_FLAGS) -o $@ $<
$(VK_SPV_DIR)/tex.spv: utilities/rendering/vkShaders/tex.vert | $(VK_SPV_DIR)
	@echo "glslc $< -> $@"
	$(VK_GLSLC) $(VK_GLSLC_FLAGS) -o $@ $<
$(VK_SPV_DIR)/tex_fs.spv: utilities/rendering/vkShaders/tex.frag | $(VK_SPV_DIR)
	@echo "glslc $< -> $@"
	$(VK_GLSLC) $(VK_GLSLC_FLAGS) -o $@ $<
$(VK_SPV_DIR)/masked.spv: utilities/rendering/vkShaders/masked.frag | $(VK_SPV_DIR)
	@echo "glslc $< -> $@"
	$(VK_GLSLC) $(VK_GLSLC_FLAGS) -o $@ $<
endif

# MSL pre-compilation — only relevant on the MTL leg (Darwin-only). The
# .metallib lives in obj/MTL/ alongside the .o files; the binary locates it
# via its own directory at runtime. Mirrors the SPIR-V precedent (147-172)
# but uses xcrun -sdk macosx metal instead of glslc.
ifeq ($(BACKEND),MTL)
# Pre-flight gate: abort with a clear message if the Metal toolchain is
# missing (e.g. Xcode not yet installed) BEFORE attempting the MSL compile.
$(MTL_METALLIB): $(wildcard $(MTL_SHADER_DIR)/*.metal) | $(OBJDIR)
	@if ! xcrun --find $(MTL_METAL) >/dev/null 2>&1; then \
		echo "ERROR: 'xcrun --find metal' failed — the Metal toolchain is not installed."; \
		echo "Install Xcode (or run 'xcode-select --install') and accept the license."; \
		exit 1; \
	fi
	@echo "metal $< -> $@"
	xcrun -sdk macosx $(MTL_METAL) $(MTL_METAL_FLAGS) -c $< -o $(OBJDIR)/aestroids.air
	xcrun -sdk macosx metallib $(OBJDIR)/aestroids.air -o $@

# ObjC++ TU rule for .mm files. Isolated from CXXFLAGS: the ObjC++ front end
# is selected per-TU via -x objective-c++, so no ObjC++ flags leak into the
# global CXXFLAGS (which would poison the C++ TUs on every leg).
$(OBJDIR)/%.o: utilities/rendering/%.mm | $(OBJDIR)
	${CXX} ${CXXFLAGS} ${BACKEND_CXXFLAGS} -x objective-c++ -c $< -o $@
endif

$(OBJDIR)/rotatorDisplayData.o: utilities/pixmaps/rotated/rotatorDisplayData.C utilities/pixmaps/rotated/rotatorDisplayData.H | $(OBJDIR)
	${CXX} ${CXXFLAGS} ${BACKEND_CXXFLAGS} -c $< -o $@

$(OBJDIR)/compositePixmap.o: utilities/pixmaps/composite/compositePixmap.C utilities/pixmaps/composite/compositePixmap.H | $(OBJDIR)
	${CXX} ${CXXFLAGS} ${BACKEND_CXXFLAGS} -c $< -o $@

$(OBJDIR)/xbmDecodeSelfTest.o: utilities/pixmaps/xbmDecodeSelfTest.C utilities/pixmaps/xbmDecode.H $(wildcard bitmaps/*.xbm) | $(OBJDIR)
	${CXX} ${CXXFLAGS} ${BACKEND_CXXFLAGS} -c $< -o $@

# Vendor TUs, task 30. glad.c: C, gcc, GL leg only. dear_imgui core units:
# C++17 via global CXXFLAGS. Static pattern rule (a plain `imgui%.o` implicit
# rule cannot build imgui.o — GNU make stems are never empty).
$(OBJDIR)/glad.o: vendor/glad/src/glad.c vendor/glad/include/glad/glad.h vendor/glad/include/GL/glad.h vendor/glad/include/KHR/khrplatform.h | $(OBJDIR)
	${CC} ${VENDOR_INCS} -O3 -c $< -o $@

# stb_truetype implementation TU (task 31): the single expansion point of the
# vendored third-party code — kept out of header-inline glBackend.H so the
# game TU's warning baseline is untouched (glad.c precedent: plain -O3, gcc).
$(OBJDIR)/stbTruetypeImpl.o: utilities/rendering/stbTruetypeImpl.C vendor/stb/stb_truetype.h | $(OBJDIR)
	${CC} ${VENDOR_INCS} -O3 -c $< -o $@

$(IMGUI_OBJECTS): $(OBJDIR)/%.o: vendor/dear_imgui/%.cpp vendor/dear_imgui/imgui.h vendor/dear_imgui/imconfig.h vendor/dear_imgui/imgui_internal.h $(wildcard vendor/dear_imgui/imstb_*.h) | $(OBJDIR)
	${CXX} ${CXXFLAGS} ${BACKEND_CXXFLAGS} -c $< -o $@

# Task 44a: ImGuiOptionsMenu implementation (GL + VK legs — see MENU_OBJECTS).
$(OBJDIR)/optionsMenu.o: gamePlay/optionsMenu.C gamePlay/optionsMenu.H utilities/rendering/menuAdapter.H utilities/rendering/renderingEngine.H vendor/dear_imgui/imgui.h | $(OBJDIR)
	${CXX} ${CXXFLAGS} ${BACKEND_CXXFLAGS} -c $< -o $@

# Host utility: build + run the XBM decode self-test (pure std-C++, no backend).
#   make xbm-selftest && ./obj/xbmDecodeSelfTest > qa/xbm-golden/goldens.txt
.PHONY: xbm-selftest
xbm-selftest: obj/xbmDecodeSelfTest

obj/xbmDecodeSelfTest: utilities/pixmaps/xbmDecodeSelfTest.C utilities/pixmaps/xbmDecode.H $(wildcard bitmaps/*.xbm)
	${CXX} ${CXXFLAGS} -o $@ $<

# Deterministic headless QA harness (task 9, D17.3/B7): Xvfb driver + XTest
# script executor + XGetImage capture + masked diff. Binary is a build artifact
# in obj/ (not shipped); sources live in test/harness/. Links libXtst for
# XTestFakeKeyEvent/XTestFakeButtonEvent/motion.
#   make harness && ./obj/harness --seed N --script test/harness/scripts/f --out dir
.PHONY: harness
harness: obj/harness

obj/harness: test/harness/harness.C
	${CXX} ${CXXFLAGS} $< ${LDFLAGS} -lX11 -lXtst -o $@

# Task 45b: numeric QA lane (test/numeric/) — backend-agnostic test target.
# Unit legs (53-golden diff vs the PRE-task-24 pins, gravity FP-guard asserts,
# 500-angle seeded suite on the X11 flavor AND the guards-closed GL-leg domain
# config) + seeded full-game state-hash parity on the X11 and GL binaries.
# Kept in the test-target region, disjoint from the VK link rules below.
#   make test-numeric                        # full lane
#   XAST_NUMERIC_SKIP_GAME=1 make test-numeric   # unit legs only
.PHONY: test-numeric
test-numeric:
	test/numeric/lane.sh

# XBM dependency list — refreshed at task 29 (D10/D13; was stale). Reconciliation:
# 32 datasets on disk = 26 game datasets consumed by this TU chain (incl. the
# previously-missing eightball/peace/yinyang via rockGroup.H and fortytwo via
# shipGroup.H) + 6 Options-side scoring icons (transitive via options.H, which
# is in this TU's include chain — listed so icon edits trigger recompiles).
# Both _CORP_LOGO_ variants are covered because every casing variant is listed.
GAME_XBMS=bitmaps/ENEMYDecor_13x5.xbm bitmaps/ROCKDecor1.xbm bitmaps/ROCKDecor2.xbm \
    bitmaps/ROCKDecor3.xbm bitmaps/eightball.xbm bitmaps/peace.xbm bitmaps/yinyang.xbm \
    bitmaps/enemyBulletDecor.xbm bitmaps/enemyDecor_7x3.xbm bitmaps/explosionCenter.xbm \
 bitmaps/explosionEdge.xbm bitmaps/explosionMiddle.xbm bitmaps/fortytwo.xbm \
 bitmaps/NCC1701ADecor.xbm bitmaps/NCC1701AIcon.xbm bitmaps/NCC1701AThrustDecor.xbm \
 bitmaps/NCC1701DDecorBottom.xbm bitmaps/NCC1701DDecorTop.xbm bitmaps/NCC1701DIcon.xbm \
 bitmaps/NCC1701DThrustDecor.xbm bitmaps/shipBulletDecor.xbm bitmaps/starDestroyerDecor.xbm \
 bitmaps/starDestroyerIcon.xbm bitmaps/starDestroyerThrustCenter.xbm \
 bitmaps/starDestroyerThrustEdge.xbm bitmaps/starDestroyerThrustMiddle.xbm
OPTIONS_XBMS=bitmaps/bulletScoringIcon.xbm bitmaps/enemyScoringIcon_17x7.xbm \
    bitmaps/ENEMYScoringIcon_31x11.xbm bitmaps/rockScoringIcon_14x14.xbm bitmaps/ROckScoringIcon_28x28.xbm \
    bitmaps/ROCKScoringIcon_40x40.xbm

$(OBJDIR)/XAsteroids.o: XAsteroids.C utilities/rendering/x11Backend.H utilities/rendering/glBackend.H utilities/rendering/vkBackend.H utilities/rendering/mtlBackend.H utilities/rendering/mtlCocoa.H utilities/rendering/mtlBridge.H gamePlay/optionsMenu.H $(GAME_XBMS) $(OPTIONS_XBMS) utilities/box.H objects/bullet.H utilities/pixmaps/composite/compositePixmap.H objects/enemies/enemyBulletGroup.H objects/enemies/enemyGroup.H objects/explosions/explosion.H objects/explosions/explosionGraphic.H utilities/frames/frameList.H utilities/frames/frameTimer.H utilities/intersection2d.H utilities/liner.H utilities/linkedArray.H objects/movableObject.H gamePlay/options/options.H gamePlay/playingField.H objects/rocks/rockGroup.H utilities/pixmaps/rotated/rotator.H utilities/pixmaps/rotated/rotatorDisplayData.H gamePlay/score.H objects/ships/shipBulletGroup.H objects/ships/shipGroup.H gamePlay/options/button.H gamePlay/shipYard.H objects/rocks/spawner.H gamePlay/stage.H utilities/vector2d.H | $(OBJDIR)
	${CXX} ${CXXFLAGS} ${BACKEND_CXXFLAGS} -c $< -o $@

# --- Link rules (task 29, F3/D10/U23; three-way since task 37) --------------
# F3 asserts each backend's link line BY RECIPE INSPECTION (make V=1), not by
# eyeball: GL links -lglfw -lGL with NO -lXm/-lXt/-lX11; VK (task 37) links
# -lglfw -lvulkan with NO -lXm/-lXt/-lX11; the X11 line keeps -lXm -lXt -lX11
# byte-identical to the pre-task-29 tree. Both GPU legs link the two D14 units
# ($(GAME_OBJECTS)): since task 27 they compile on EVERY leg (guards-closed
# engine branches on GL/VK) and DEFINE the RotatorDisplayData-subclass /
# CPU-composite symbols the domain references.
#
# U15 gate (task 37): $(VK_LOADER_GATE) is the FIRST recipe line of BOTH VK
# link rules — a machine without libvulkan.so.1 aborts BEFORE any link. The
# check is loader-presence ONLY (no vulkaninfo/probe-exe dependency: the
# probe's own build must not be gated on itself).

# --- Flavor stamp (stale-binary trap fix): all four legs link to the SAME
# output name (XAsteroids) and carry no flavor marker make can see, so a
# BACKEND=X->Y switch left the previous flavor's root binary in place: every
# obj/Y prerequisite was up-to-date, the link was skipped, and make executed
# a FOREIGN-flavor binary (observed 2026-09-03: BACKEND=MTL run executed a
# stale VK build -> glfwCreateWindowSurface -3). Per-obj-dir stamps cannot
# detect this (the shared root binary is what changed flavor), so the stamp
# is GLOBAL: obj/.backend records the flavor the root binary was last linked
# under. On a switch the stamp rewrites itself and rm's the stale root
# binary, forcing a relink; on a same-flavor rerun it is a no-op (file
# untouched, mtime stable -> no spurious relink). Gated into every link rule
# below; clean removes it.
# The per-flavor phony prerequisite below closes the GNU make 3.81 same-second race.
FLAVOR_STAMP := obj/.backend
FORCE: ;
$(FLAVOR_STAMP): FORCE | obj
	@if [ ! -f $(FLAVOR_STAMP) ] || [ "$$(cat $(FLAVOR_STAMP) 2>/dev/null)" != "$(BACKEND)" ]; then \
		echo "$(BACKEND)" > $(FLAVOR_STAMP) && rm -f XAsteroids; \
	fi
.PHONY: XAsteroids-flavor-$(BACKEND)
XAsteroids-flavor-$(BACKEND):
obj:
	@mkdir -p obj

ifeq ($(BACKEND),GL)
XAsteroids: obj/GL/XAsteroids.o $(GAME_OBJECTS) $(GL_OBJECTS) $(IMGUI_OBJECTS) $(MENU_OBJECTS) $(FLAVOR_STAMP) XAsteroids-flavor-$(BACKEND)
	${CXX} ${CXXFLAGS} obj/GL/XAsteroids.o $(GAME_OBJECTS) $(GL_OBJECTS) $(IMGUI_OBJECTS) $(MENU_OBJECTS) ${LDFLAGS} $(GLFW_LIB) $(OPENGL_LINK) -o XAsteroids
else ifeq ($(BACKEND),VK)
# Task 43/44b: the real game links the menu units too (ImGuiOptionsMenu +
# dear_imgui core — the adapter consumes only RenderingEngine types, D9).
XAsteroids: obj/VK/XAsteroids.o $(GAME_OBJECTS) $(VK_STB_OBJECT) $(IMGUI_OBJECTS) $(MENU_OBJECTS) $(VK_SPV_TARGETS) $(FLAVOR_STAMP) XAsteroids-flavor-$(BACKEND)
	$(VK_LOADER_GATE)
	${CXX} ${CXXFLAGS} obj/VK/XAsteroids.o $(GAME_OBJECTS) $(VK_STB_OBJECT) $(IMGUI_OBJECTS) $(MENU_OBJECTS) ${LDFLAGS} $(GLFW_LIB) -o XAsteroids $(VK_LINK_EXTRA)
else ifeq ($(BACKEND),MTL)
# Task 49+: the MTL (Metal) leg links the game + menu units + the pre-compiled
# .metallib + the ObjC++ bridges (mtlCocoa.o + mtlBridge.o). Mirrors the VK
# link structure but WITHOUT the Vulkan loader gate: Metal is a system
# framework on macOS. Links GLFW + the Metal frameworks.
XAsteroids: obj/MTL/XAsteroids.o $(GAME_OBJECTS) $(MTL_STB_OBJECT) $(IMGUI_OBJECTS) $(MENU_OBJECTS) $(MTL_METALLIB) $(OBJDIR)/mtlCocoa.o $(OBJDIR)/mtlBridge.o $(FLAVOR_STAMP) XAsteroids-flavor-$(BACKEND)
	${CXX} ${CXXFLAGS} obj/MTL/XAsteroids.o $(GAME_OBJECTS) $(MTL_STB_OBJECT) $(IMGUI_OBJECTS) $(MENU_OBJECTS) $(OBJDIR)/mtlCocoa.o $(OBJDIR)/mtlBridge.o ${LDFLAGS} $(GLFW_LIB) -framework Metal -framework MetalKit -framework Foundation -framework QuartzCore -framework AppKit -o XAsteroids
else
XAsteroids: obj/X11/XAsteroids.o obj/X11/rotatorDisplayData.o obj/X11/compositePixmap.o $(FLAVOR_STAMP) XAsteroids-flavor-$(BACKEND)
	${CXX} ${CXXFLAGS} ${X11_BACKEND} obj/X11/XAsteroids.o obj/X11/rotatorDisplayData.o obj/X11/compositePixmap.o ${LDFLAGS} -lXm -lXt -lX11 -oXAsteroids
endif

# Task 45b: macOS Vulkan run helper — GLFW dlopen("libvulkan.1.dylib") by bare
# name, which dyld does NOT search /opt/homebrew/lib/ for on macOS 26. The
# DYLD_FALLBACK_LIBRARY_PATH makes the homebrew vulkan-loader discoverable
# so glfwVulkanSupported() returns true and glfwGetRequiredInstanceExtensions
# produces its 2 required exts (VK_KHR_surface + VK_EXT_metal_surface).
# MoltenVK ICD is resolved via VK_ICD_FILENAMES at runtime.
run: XAsteroids
ifeq ($(BACKEND),VK)
ifeq ($(UNAME_S),Darwin)
	@if [ ! -d /opt/homebrew/opt/vulkan-loader ]; then \
	 echo "VK run requires: brew install vulkan-loader molten-vk"; exit 1; \
	fi
	dyld_env="DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/opt/vulkan-loader/lib"; \
	icd="/opt/homebrew/opt/molten-vk/etc/vulkan/icd.d/MoltenVK_icd.json"; \
	if [ -f "$$icd" ]; then export VK_ICD_FILENAMES="$$icd"; fi; \
	echo "vkBackend: run via $$dyld_env + $$VK_ICD_FILENAMES"; \
	eval "$$dyld_env ./XAsteroids"
else
	./XAsteroids
endif
else
	./XAsteroids
endif

# Task 37: pass-0 probe driver (test/vk/vkprobe.C -> VKBackend::
# probeStandalone). Links -lglfw because vkBackend.H references glfw symbols
# even in standalone mode (no glfwInit call at runtime, but the TU needs them
# at link time). Gated like every other VK link.
.PHONY: vkprobe
vkprobe: obj/VK/vkprobe

obj/VK/vkprobe: test/vk/vkprobe.C utilities/rendering/vkBackend.H utilities/rendering/renderingEngine.H utilities/rendering/windowSize.H vendor/stb/stb_truetype.h obj/VK/stbTruetypeImpl.o $(VK_SPV_TARGETS) | obj/VK
	$(VK_LOADER_GATE)
	${CXX} ${CXXFLAGS} ${VENDOR_INCS} -Ivendor/vulkan/include -Iutilities/rendering $< obj/VK/stbTruetypeImpl.o ${LDFLAGS} $(GLFW_LIB) -o $@ $(VK_LINK_EXTRA)

# Task 38: surface+swapchain probe driver (test/vk/vksurface.C ->
# VKBackend::initWindow end-to-end). Run under Xvfb with DISPLAY set; copy
# to the repo root first (font paths resolve via /proc/self/exe).
.PHONY: vksurface
vksurface: obj/VK/vksurface

obj/VK/vksurface: test/vk/vksurface.C utilities/rendering/vkBackend.H utilities/rendering/renderingEngine.H utilities/rendering/windowSize.H vendor/stb/stb_truetype.h obj/VK/stbTruetypeImpl.o $(VK_SPV_TARGETS) | obj/VK
	$(VK_LOADER_GATE)
	${CXX} ${CXXFLAGS} ${VENDOR_INCS} -Ivendor/vulkan/include -Iutilities/rendering $< obj/VK/stbTruetypeImpl.o ${LDFLAGS} $(GLFW_LIB) -o $@ $(VK_LINK_EXTRA)

# Task 39: frame-sync soak driver (test/vk/vksoak.C -> beginFrame/endFrame
# acquire/submit/present cycle + forced-resize re-bootstrap exercise). Run
# under Xvfb with DISPLAY set; copy to the repo root first (font paths
# resolve via /proc/self/exe).
.PHONY: vksoak
vksoak: obj/VK/vksoak

obj/VK/vksoak: test/vk/vksoak.C utilities/rendering/vkBackend.H utilities/rendering/renderingEngine.H utilities/rendering/windowSize.H vendor/stb/stb_truetype.h obj/VK/stbTruetypeImpl.o $(VK_SPV_TARGETS) | obj/VK
	$(VK_LOADER_GATE)
	${CXX} ${CXXFLAGS} ${VENDOR_INCS} -Ivendor/vulkan/include -Iutilities/rendering $< obj/VK/stbTruetypeImpl.o ${LDFLAGS} $(GLFW_LIB) -o $@ $(VK_LINK_EXTRA)

# Task 40: render-pass/framebuffer + scissor-proof driver (test/vk/vkpass.C
# -> 600-frame soak + QA-hook readback proof of the dynamic scissor). Same
# run recipe as vksoak.
.PHONY: vkpass
vkpass: obj/VK/vkpass

obj/VK/vkpass: test/vk/vkpass.C utilities/rendering/vkBackend.H utilities/rendering/renderingEngine.H utilities/rendering/windowSize.H vendor/stb/stb_truetype.h obj/VK/stbTruetypeImpl.o $(VK_SPV_TARGETS) | obj/VK
	$(VK_LOADER_GATE)
	${CXX} ${CXXFLAGS} ${VENDOR_INCS} -Ivendor/vulkan/include -Iutilities/rendering $< obj/VK/stbTruetypeImpl.o ${LDFLAGS} $(GLFW_LIB) -o $@ $(VK_LINK_EXTRA)

# Task 41: pipeline proof driver (test/vk/vkpipe.C -> line/tri/outline/tex
# pipelines + thick-line geometry + transform identity + live-draw soak).
# Same run recipe as vksoak.
.PHONY: vkpipe
vkpipe: obj/VK/vkpipe

obj/VK/vkpipe: test/vk/vkpipe.C utilities/rendering/vkBackend.H utilities/rendering/renderingEngine.H utilities/rendering/windowSize.H utilities/rendering/vkShaders/prim.vert utilities/rendering/vkShaders/prim.frag utilities/rendering/vkShaders/tex.vert utilities/rendering/vkShaders/tex.frag vendor/stb/stb_truetype.h obj/VK/stbTruetypeImpl.o $(VK_SPV_TARGETS) | obj/VK
	$(VK_LOADER_GATE)
	${CXX} ${CXXFLAGS} ${VENDOR_INCS} -Ivendor/vulkan/include -Iutilities/rendering $< obj/VK/stbTruetypeImpl.o ${LDFLAGS} $(GLFW_LIB) -o $@ $(VK_LINK_EXTRA)

# Task 42: engine-methods proof (test/vk/vkmethods.C) + its GL reference leg
# (test/vk/vkmethods-gl.C renders identityScene.H through glBackend and dumps
# normalized top-down RGBA for the byte-compare). vkmethods links the CPU
# composite unit (task-27 explosion frames) and X11/XTest (the D16 live
# injection proof); the GL leg needs glad + stb and NO ImGui symbols (none are
# odr-used). Run BOTH from the repo root under their own Xvfb displays.
#
# Darwin (task 11): vkmethods builds with -DXAST_NO_XTEST, skipping XTest
# injection which requires X11/libXtst. Phases A-D+F still run; Phase E is
# compiled out via the preprocessor guard.
.PHONY: vkmethods vkmethods-gl
vkmethods: obj/VK/vkmethods
vkmethods-gl: obj/VK/vkmethods-gl

obj/VK/vkmethods-gl: test/vk/vkmethods-gl.C test/vk/identityScene.H utilities/rendering/glBackend.H utilities/rendering/renderingEngine.H utilities/rendering/windowSize.H vendor/stb/stb_truetype.h obj/GL/glad.o obj/GL/stbTruetypeImpl.o | obj/VK
	${CXX} ${CXXFLAGS} ${VENDOR_INCS} -Iutilities/rendering $< obj/GL/glad.o obj/GL/stbTruetypeImpl.o ${LDFLAGS} $(GLFW_LIB) $(OPENGL_LINK) -ldl -o $@

ifneq ($(UNAME_S),Darwin)
obj/VK/vkmethods: test/vk/vkmethods.C test/vk/vkinput.C test/vk/identityScene.H utilities/rendering/vkBackend.H utilities/rendering/renderingEngine.H utilities/rendering/windowSize.H utilities/pixmaps/xbmDecode.H utilities/pixmaps/composite/compositePixmap.H utilities/pixmaps/composite/compositePixmap.C utilities/rendering/vkShaders/prim.vert utilities/rendering/vkShaders/prim.frag utilities/rendering/vkShaders/tex.vert utilities/rendering/vkShaders/tex.frag utilities/rendering/vkShaders/masked.frag vendor/stb/stb_truetype.h obj/VK/stbTruetypeImpl.o obj/VK/compositePixmap.o $(VK_SPV_TARGETS) | obj/VK
	$(VK_LOADER_GATE)
	${CXX} ${CXXFLAGS} ${VENDOR_INCS} -Ivendor/vulkan/include -Iutilities/rendering test/vk/vkmethods.C test/vk/vkinput.C obj/VK/stbTruetypeImpl.o obj/VK/compositePixmap.o ${LDFLAGS} $(GLFW_LIB) -lX11 -lXtst -o $@ $(VK_LINK_EXTRA)
else
obj/VK/vkmethods: test/vk/vkmethods.C test/vk/identityScene.H utilities/rendering/vkBackend.H utilities/rendering/renderingEngine.H utilities/rendering/windowSize.H utilities/pixmaps/xbmDecode.H utilities/pixmaps/composite/compositePixmap.H utilities/pixmaps/composite/compositePixmap.C utilities/rendering/vkShaders/prim.vert utilities/rendering/vkShaders/prim.frag utilities/rendering/vkShaders/tex.vert utilities/rendering/vkShaders/tex.frag utilities/rendering/vkShaders/masked.frag vendor/stb/stb_truetype.h obj/VK/stbTruetypeImpl.o obj/VK/compositePixmap.o $(VK_SPV_TARGETS) | obj/VK
	${CXX} ${CXXFLAGS} -DXAST_NO_XTEST ${VENDOR_INCS} -Ivendor/vulkan/include -Iutilities/rendering test/vk/vkmethods.C obj/VK/stbTruetypeImpl.o obj/VK/compositePixmap.o ${LDFLAGS} $(GLFW_LIB) -o $@ $(VK_LINK_EXTRA)
endif

# Task 6: MTL self-diagnostic build (test/vk/mtlmethods.C -> MTLBackend).
# Renders identityScene.H on the window target for one frame, proving every
# draw method family links and runs without Metal validation errors. Links
# stbTruetypeImpl (font metrics) + the ObjC++ bridges + the metallib.
.PHONY: mtlmethods
mtlmethods: obj/MTL/mtlmethods

obj/MTL/mtlmethods: test/vk/mtlmethods.C test/vk/identityScene.H utilities/rendering/mtlBackend.H utilities/rendering/renderingEngine.H utilities/rendering/windowSize.H utilities/rendering/mtlCocoa.H utilities/rendering/mtlBridge.H vendor/stb/stb_truetype.h obj/MTL/stbTruetypeImpl.o obj/MTL/mtlCocoa.o obj/MTL/mtlBridge.o $(MTL_METALLIB) | obj/MTL
	${CXX} ${CXXFLAGS} ${VENDOR_INCS} -Iutilities/rendering $< obj/MTL/stbTruetypeImpl.o obj/MTL/mtlCocoa.o obj/MTL/mtlBridge.o ${LDFLAGS} $(GLFW_LIB) -framework Metal -framework MetalKit -framework Foundation -framework QuartzCore -framework AppKit -o $@

AutoRepeatOn: AutoRepeatOn.C
	${CXX} ${CXXFLAGS} ${X11_BACKEND} AutoRepeatOn.C ${LDFLAGS} -lX11 -o AutoRepeatOn

clean:
	\rm -rf XAsteroids XAsteroids_vk XAsteroids_gl AutoRepeatOn *.o *.u *.bak *.CKP obj/X11 obj/GL obj/VK obj/MTL obj/xbmDecodeSelfTest obj/harness $(FLAVOR_STAMP)
