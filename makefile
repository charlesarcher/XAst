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
# paced stub loop). VK stays macro-free until vkBackend lands (task 37).
ifeq ($(BACKEND),GL)
BACKEND_CXXFLAGS+=-DGL_BACKEND
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

.PHONY: objects
# All three game objects on EVERY leg (task 27: the D14 units' #else engine
# branches compile guards-closed on GL/VK, macro'd on X11) + the
# backend-agnostic self-test/vendor units on the GPU legs.
GAME_OBJECTS=$(OBJDIR)/rotatorDisplayData.o $(OBJDIR)/compositePixmap.o
objects: $(OBJDIR)/XAsteroids.o $(GAME_OBJECTS) $(GLVK_OBJECTS) $(IMGUI_OBJECTS) $(GL_OBJECTS)

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

# Host utility: build + run the XBM decode self-test (pure std-C++, no backend).
#   make xbm-selftest && ./obj/xbmDecodeSelfTest > qa/xbm-golden/goldens.txt
.PHONY: xbm-selftest
xbm-selftest: obj/xbmDecodeSelfTest

obj/xbmDecodeSelfTest: utilities/pixmaps/xbmDecodeSelfTest.C utilities/pixmaps/xbmDecode.H $(wildcard bitmaps/*.xbm)
	${CXX} ${CXXFLAGS} $< -o $@

# Deterministic headless QA harness (task 9, D17.3/B7): Xvfb driver + XTest
# script executor + XGetImage capture + masked diff. Binary is a build artifact
# in obj/ (not shipped); sources live in test/harness/. Links libXtst for
# XTestFakeKeyEvent/XTestFakeButtonEvent/motion.
#   make harness && ./obj/harness --seed N --script test/harness/scripts/f --out dir
.PHONY: harness
harness: obj/harness

obj/harness: test/harness/harness.C
	${CXX} ${CXXFLAGS} $< ${LDFLAGS} -lX11 -lXtst -o $@

# XBM dependency list — refreshed at task 29 (D10/D13; was stale). Reconciliation:
# 32 datasets on disk = 26 game datasets consumed by this TU chain (incl. the
# previously-missing eightball/peace/yinyang via rockGroup.H and fortytwo via
# shipGroup.H) + 6 Options-side scoring icons (transitive via options.H, which
# is in this TU's include chain — listed so icon edits trigger recompiles).
# Both _CORP_LOGO_ variants are covered because every casing variant is listed.
GAME_XBMS=bitmaps/ENEMYDecor.xbm bitmaps/ROCKDecor1.xbm bitmaps/ROCKDecor2.xbm \
 bitmaps/ROCKDecor3.xbm bitmaps/eightball.xbm bitmaps/peace.xbm bitmaps/yinyang.xbm \
 bitmaps/enemyBulletDecor.xbm bitmaps/enemyDecor.xbm bitmaps/explosionCenter.xbm \
 bitmaps/explosionEdge.xbm bitmaps/explosionMiddle.xbm bitmaps/fortytwo.xbm \
 bitmaps/NCC1701ADecor.xbm bitmaps/NCC1701AIcon.xbm bitmaps/NCC1701AThrustDecor.xbm \
 bitmaps/NCC1701DDecorBottom.xbm bitmaps/NCC1701DDecorTop.xbm bitmaps/NCC1701DIcon.xbm \
 bitmaps/NCC1701DThrustDecor.xbm bitmaps/shipBulletDecor.xbm bitmaps/starDestroyerDecor.xbm \
 bitmaps/starDestroyerIcon.xbm bitmaps/starDestroyerThrustCenter.xbm \
 bitmaps/starDestroyerThrustEdge.xbm bitmaps/starDestroyerThrustMiddle.xbm
OPTIONS_XBMS=bitmaps/bulletScoringIcon.xbm bitmaps/enemyScoringIcon.xbm \
 bitmaps/ENEMYScoringIcon.xbm bitmaps/rockScoringIcon.xbm bitmaps/ROckScoringIcon.xbm \
 bitmaps/ROCKScoringIcon.xbm

$(OBJDIR)/XAsteroids.o: XAsteroids.C utilities/rendering/x11Backend.H utilities/rendering/glBackend.H $(GAME_XBMS) $(OPTIONS_XBMS) utilities/box.H objects/bullet.H utilities/pixmaps/composite/compositePixmap.H objects/enemies/enemyBulletGroup.H objects/enemies/enemyGroup.H objects/explosions/explosion.H objects/explosions/explosionGraphic.H utilities/frames/frameList.H utilities/frames/frameTimer.H utilities/intersection2d.H utilities/liner.H utilities/linkedArray.H objects/movableObject.H gamePlay/options/options.H gamePlay/playingField.H objects/rocks/rockGroup.H utilities/pixmaps/rotated/rotator.H utilities/pixmaps/rotated/rotatorDisplayData.H gamePlay/score.H objects/ships/shipBulletGroup.H objects/ships/shipGroup.H gamePlay/options/button.H gamePlay/shipYard.H objects/rocks/spawner.H gamePlay/stage.H utilities/vector2d.H | $(OBJDIR)
	${CXX} ${CXXFLAGS} ${BACKEND_CXXFLAGS} -c $< -o $@

# --- Link rules (task 29, F3/D10/U23) ---------------------------------------
# F3 asserts each backend's link line BY RECIPE INSPECTION (make V=1), not by
# eyeball: GL links -lglfw -lGL with NO -lXm/-lXt/-lX11; the X11 line keeps
# -lXm -lXt -lX11 byte-identical to the pre-task-29 tree. The VK link rule
# lands at Phase 4 task 37 (until then BACKEND=VK supports `objects` only).
# Both legs link the two D14 units ($(GAME_OBJECTS)): since task 27 they
# compile on EVERY leg (guards-closed engine branches on GL) and DEFINE the
# RotatorDisplayData-subclass / CPU-composite symbols the domain references.
ifeq ($(BACKEND),GL)
XAsteroids: obj/GL/XAsteroids.o $(GAME_OBJECTS) $(GL_OBJECTS)
	${CXX} ${CXXFLAGS} obj/GL/XAsteroids.o $(GAME_OBJECTS) $(GL_OBJECTS) ${LDFLAGS} -lglfw -lGL -o XAsteroids
else
XAsteroids: obj/X11/XAsteroids.o obj/X11/rotatorDisplayData.o obj/X11/compositePixmap.o
	${CXX} ${CXXFLAGS} ${X11_BACKEND} obj/X11/XAsteroids.o obj/X11/rotatorDisplayData.o obj/X11/compositePixmap.o ${LDFLAGS} -lXm -lXt -lX11 -oXAsteroids
endif

AutoRepeatOn: AutoRepeatOn.C
	${CXX} ${CXXFLAGS} ${X11_BACKEND} AutoRepeatOn.C ${LDFLAGS} -lX11 -o AutoRepeatOn

clean:
	\rm -rf XAsteroids AutoRepeatOn *.o *.u *.bak *.CKP obj/X11 obj/GL obj/VK obj/xbmDecodeSelfTest obj/harness
