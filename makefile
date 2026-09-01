# BACKEND selects the compile configuration and object directory: X11 | GL | VK.
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
ifneq ($(filter $(BACKEND),GL VK),)
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
ifneq ($(filter $(BACKEND),GL VK),)
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
ifneq ($(filter $(BACKEND),GL VK),)
IMGUI_OBJECTS=$(addprefix $(OBJDIR)/,$(addsuffix .o,$(DEAR_IMGUI_UNITS)))
endif
ifeq ($(BACKEND),GL)
GL_OBJECTS=$(OBJDIR)/glad.o $(OBJDIR)/stbTruetypeImpl.o
endif
# Task 44a/44b: the ImGuiOptionsMenu unit lands on BOTH GPU legs (44a = GL,
# 44b = VK). The seam header itself is include-guarded and only reaches TUs
# that ask for it.
MENU_OBJECTS=
ifneq ($(filter $(BACKEND),GL VK),)
MENU_OBJECTS=$(OBJDIR)/optionsMenu.o
endif
# Task 38: the VK leg consumes stb font METRICS (pass-1 window sizing via the
# shared D15 formula) — the SAME impl TU as GL; glad stays GL-only.
ifeq ($(BACKEND),VK)
VK_STB_OBJECT=$(OBJDIR)/stbTruetypeImpl.o
endif

.PHONY: objects
# All three game objects on EVERY leg (task 27: the D14 units' #else engine
# branches compile guards-closed on GL/VK, macro'd on X11) + the
# backend-agnostic self-test/vendor units on the GPU legs.
GAME_OBJECTS=$(OBJDIR)/rotatorDisplayData.o $(OBJDIR)/compositePixmap.o
objects: $(OBJDIR)/XAsteroids.o $(GAME_OBJECTS) $(GLVK_OBJECTS) $(IMGUI_OBJECTS) $(GL_OBJECTS) $(VK_STB_OBJECT) $(MENU_OBJECTS)

$(OBJDIR):
	mkdir -p $(OBJDIR)

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

$(OBJDIR)/XAsteroids.o: XAsteroids.C utilities/rendering/x11Backend.H utilities/rendering/glBackend.H utilities/rendering/vkBackend.H gamePlay/optionsMenu.H $(GAME_XBMS) $(OPTIONS_XBMS) utilities/box.H objects/bullet.H utilities/pixmaps/composite/compositePixmap.H objects/enemies/enemyBulletGroup.H objects/enemies/enemyGroup.H objects/explosions/explosion.H objects/explosions/explosionGraphic.H utilities/frames/frameList.H utilities/frames/frameTimer.H utilities/intersection2d.H utilities/liner.H utilities/linkedArray.H objects/movableObject.H gamePlay/options/options.H gamePlay/playingField.H objects/rocks/rockGroup.H utilities/pixmaps/rotated/rotator.H utilities/pixmaps/rotated/rotatorDisplayData.H gamePlay/score.H objects/ships/shipBulletGroup.H objects/ships/shipGroup.H gamePlay/options/button.H gamePlay/shipYard.H objects/rocks/spawner.H gamePlay/stage.H utilities/vector2d.H | $(OBJDIR)
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
VK_LOADER_GATE=@qa/vk-probe.sh --link-check || exit 1

ifeq ($(BACKEND),GL)
XAsteroids: obj/GL/XAsteroids.o $(GAME_OBJECTS) $(GL_OBJECTS) $(IMGUI_OBJECTS) $(MENU_OBJECTS)
	${CXX} ${CXXFLAGS} obj/GL/XAsteroids.o $(GAME_OBJECTS) $(GL_OBJECTS) $(IMGUI_OBJECTS) $(MENU_OBJECTS) ${LDFLAGS} -lglfw -lGL -o XAsteroids
else ifeq ($(BACKEND),VK)
# Task 43/44b: the real game links the menu units too (ImGuiOptionsMenu +
# dear_imgui core — the adapter consumes only RenderingEngine types, D9).
XAsteroids: obj/VK/XAsteroids.o $(GAME_OBJECTS) $(VK_STB_OBJECT) $(IMGUI_OBJECTS) $(MENU_OBJECTS)
	$(VK_LOADER_GATE)
	${CXX} ${CXXFLAGS} obj/VK/XAsteroids.o $(GAME_OBJECTS) $(VK_STB_OBJECT) $(IMGUI_OBJECTS) $(MENU_OBJECTS) ${LDFLAGS} -lglfw -lvulkan -o XAsteroids
else
XAsteroids: obj/X11/XAsteroids.o obj/X11/rotatorDisplayData.o obj/X11/compositePixmap.o
	${CXX} ${CXXFLAGS} ${X11_BACKEND} obj/X11/XAsteroids.o obj/X11/rotatorDisplayData.o obj/X11/compositePixmap.o ${LDFLAGS} -lXm -lXt -lX11 -oXAsteroids
endif

# Task 37: pass-0 probe driver (test/vk/vkprobe.C -> VKBackend::
# probeStandalone). Links -lglfw because vkBackend.H references glfw symbols
# even in standalone mode (no glfwInit call at runtime, but the TU needs them
# at link time). Gated like every other VK link.
.PHONY: vkprobe
vkprobe: obj/VK/vkprobe

obj/VK/vkprobe: test/vk/vkprobe.C utilities/rendering/vkBackend.H utilities/rendering/renderingEngine.H utilities/rendering/windowSize.H vendor/stb/stb_truetype.h obj/VK/stbTruetypeImpl.o | obj/VK
	$(VK_LOADER_GATE)
	${CXX} ${CXXFLAGS} ${VENDOR_INCS} -Ivendor/vulkan/include -Iutilities/rendering $< obj/VK/stbTruetypeImpl.o ${LDFLAGS} -lglfw -lvulkan -o $@

# Task 38: surface+swapchain probe driver (test/vk/vksurface.C ->
# VKBackend::initWindow end-to-end). Run under Xvfb with DISPLAY set; copy
# to the repo root first (font paths resolve via /proc/self/exe).
.PHONY: vksurface
vksurface: obj/VK/vksurface

obj/VK/vksurface: test/vk/vksurface.C utilities/rendering/vkBackend.H utilities/rendering/renderingEngine.H utilities/rendering/windowSize.H vendor/stb/stb_truetype.h obj/VK/stbTruetypeImpl.o | obj/VK
	$(VK_LOADER_GATE)
	${CXX} ${CXXFLAGS} ${VENDOR_INCS} -Ivendor/vulkan/include -Iutilities/rendering $< obj/VK/stbTruetypeImpl.o ${LDFLAGS} -lglfw -lvulkan -o $@

# Task 39: frame-sync soak driver (test/vk/vksoak.C -> beginFrame/endFrame
# acquire/submit/present cycle + forced-resize re-bootstrap exercise). Run
# under Xvfb with DISPLAY set; copy to the repo root first (font paths
# resolve via /proc/self/exe).
.PHONY: vksoak
vksoak: obj/VK/vksoak

obj/VK/vksoak: test/vk/vksoak.C utilities/rendering/vkBackend.H utilities/rendering/renderingEngine.H utilities/rendering/windowSize.H vendor/stb/stb_truetype.h obj/VK/stbTruetypeImpl.o | obj/VK
	$(VK_LOADER_GATE)
	${CXX} ${CXXFLAGS} ${VENDOR_INCS} -Ivendor/vulkan/include -Iutilities/rendering $< obj/VK/stbTruetypeImpl.o ${LDFLAGS} -lglfw -lvulkan -o $@

# Task 40: render-pass/framebuffer + scissor-proof driver (test/vk/vkpass.C
# -> 600-frame soak + QA-hook readback proof of the dynamic scissor). Same
# run recipe as vksoak.
.PHONY: vkpass
vkpass: obj/VK/vkpass

obj/VK/vkpass: test/vk/vkpass.C utilities/rendering/vkBackend.H utilities/rendering/renderingEngine.H utilities/rendering/windowSize.H vendor/stb/stb_truetype.h obj/VK/stbTruetypeImpl.o | obj/VK
	$(VK_LOADER_GATE)
	${CXX} ${CXXFLAGS} ${VENDOR_INCS} -Ivendor/vulkan/include -Iutilities/rendering $< obj/VK/stbTruetypeImpl.o ${LDFLAGS} -lglfw -lvulkan -o $@

# Task 41: pipeline proof driver (test/vk/vkpipe.C -> line/tri/outline/tex
# pipelines + thick-line geometry + transform identity + live-draw soak).
# Same run recipe as vksoak.
.PHONY: vkpipe
vkpipe: obj/VK/vkpipe

obj/VK/vkpipe: test/vk/vkpipe.C utilities/rendering/vkBackend.H utilities/rendering/renderingEngine.H utilities/rendering/windowSize.H utilities/rendering/vkShaders/prim.vert utilities/rendering/vkShaders/prim.frag utilities/rendering/vkShaders/tex.vert utilities/rendering/vkShaders/tex.frag vendor/stb/stb_truetype.h obj/VK/stbTruetypeImpl.o | obj/VK
	$(VK_LOADER_GATE)
	${CXX} ${CXXFLAGS} ${VENDOR_INCS} -Ivendor/vulkan/include -Iutilities/rendering $< obj/VK/stbTruetypeImpl.o ${LDFLAGS} -lglfw -lvulkan -o $@

# Task 42: engine-methods proof (test/vk/vkmethods.C) + its GL reference leg
# (test/vk/vkmethods-gl.C renders identityScene.H through glBackend and dumps
# normalized top-down RGBA for the byte-compare). vkmethods links the CPU
# composite unit (task-27 explosion frames) and X11/XTest (the D16 live
# injection proof); the GL leg needs glad + stb and NO ImGui symbols (none are
# odr-used). Run BOTH from the repo root under their own Xvfb displays.
.PHONY: vkmethods vkmethods-gl
vkmethods: obj/VK/vkmethods
vkmethods-gl: obj/VK/vkmethods-gl

obj/VK/vkmethods: test/vk/vkmethods.C test/vk/vkinput.C test/vk/identityScene.H utilities/rendering/vkBackend.H utilities/rendering/renderingEngine.H utilities/rendering/windowSize.H utilities/pixmaps/xbmDecode.H utilities/pixmaps/composite/compositePixmap.H utilities/pixmaps/composite/compositePixmap.C utilities/rendering/vkShaders/prim.vert utilities/rendering/vkShaders/prim.frag utilities/rendering/vkShaders/tex.vert utilities/rendering/vkShaders/tex.frag utilities/rendering/vkShaders/masked.frag vendor/stb/stb_truetype.h obj/VK/stbTruetypeImpl.o obj/VK/compositePixmap.o | obj/VK
	$(VK_LOADER_GATE)
	${CXX} ${CXXFLAGS} ${VENDOR_INCS} -Ivendor/vulkan/include -Iutilities/rendering test/vk/vkmethods.C test/vk/vkinput.C obj/VK/stbTruetypeImpl.o obj/VK/compositePixmap.o ${LDFLAGS} -lglfw -lvulkan -lX11 -lXtst -o $@

obj/VK/vkmethods-gl: test/vk/vkmethods-gl.C test/vk/identityScene.H utilities/rendering/glBackend.H utilities/rendering/renderingEngine.H utilities/rendering/windowSize.H vendor/stb/stb_truetype.h obj/GL/glad.o obj/GL/stbTruetypeImpl.o | obj/VK
	${CXX} ${CXXFLAGS} ${VENDOR_INCS} -Iutilities/rendering $< obj/GL/glad.o obj/GL/stbTruetypeImpl.o ${LDFLAGS} -lglfw -lGL -ldl -o $@

AutoRepeatOn: AutoRepeatOn.C
	${CXX} ${CXXFLAGS} ${X11_BACKEND} AutoRepeatOn.C ${LDFLAGS} -lX11 -o AutoRepeatOn

clean:
	\rm -rf XAsteroids AutoRepeatOn *.o *.u *.bak *.CKP obj/X11 obj/GL obj/VK obj/xbmDecodeSelfTest obj/harness
