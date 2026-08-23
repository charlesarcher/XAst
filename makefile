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
# GL/VK legs: today's game objects are still the X11-flavored sources (the
# backend split lands at tasks 31+), so they keep -DX11_BACKEND to compile;
# vendor -I paths ride along on these legs ONLY. The XBM decode self-test
# overrides below to drop the macro — building WITHOUT -DX11_BACKEND there is
# its whole reason to exist on the GPU legs.
ifneq ($(filter $(BACKEND),GL VK),)
BACKEND_CXXFLAGS=$(X11_BACKEND) $(VENDOR_INCS)
endif

# Objects feeding the X11 link always carry the X11 macro, whatever BACKEND says.
obj/X11/XAsteroids.o obj/X11/rotatorDisplayData.o obj/X11/compositePixmap.o: BACKEND_CXXFLAGS=$(X11_BACKEND)

# The decode unit must stay macro-free on every leg that builds it.
$(OBJDIR)/xbmDecodeSelfTest.o: BACKEND_CXXFLAGS=$(VENDOR_INCS)

all: XAsteroids AutoRepeatOn

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
GL_OBJECTS=$(OBJDIR)/glad.o
endif

.PHONY: objects
objects: $(OBJDIR)/rotatorDisplayData.o $(OBJDIR)/compositePixmap.o $(OBJDIR)/XAsteroids.o $(GLVK_OBJECTS) $(IMGUI_OBJECTS) $(GL_OBJECTS)

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

$(OBJDIR)/XAsteroids.o: XAsteroids.C bitmaps/ENEMYDecor.xbm bitmaps/ROCKDecor1.xbm bitmaps/ROCKDecor2.xbm bitmaps/ROCKDecor3.xbm XAsteroids.C utilities/box.H objects/bullet.H utilities/pixmaps/composite/compositePixmap.H bitmaps/enemyBulletDecor.xbm objects/enemies/enemyBulletGroup.H bitmaps/enemyDecor.xbm objects/enemies/enemyGroup.H objects/explosions/explosion.H bitmaps/explosionCenter.xbm bitmaps/explosionEdge.xbm objects/explosions/explosionGraphic.H bitmaps/explosionMiddle.xbm utilities/frames/frameList.H utilities/frames/frameTimer.H utilities/intersection2d.H utilities/liner.H utilities/linkedArray.H objects/movableObject.H gamePlay/options/options.H gamePlay/playingField.H objects/rocks/rockGroup.H utilities/pixmaps/rotated/rotator.H utilities/pixmaps/rotated/rotatorDisplayData.H gamePlay/score.H bitmaps/starDestroyerIcon.xbm bitmaps/NCC1701DIcon.xbm bitmaps/NCC1701AIcon.xbm bitmaps/shipBulletDecor.xbm objects/ships/shipBulletGroup.H bitmaps/starDestroyerDecor.xbm bitmaps/NCC1701DDecorBottom.xbm bitmaps/NCC1701DDecorTop.xbm bitmaps/NCC1701ADecor.xbm objects/ships/shipGroup.H gamePlay/options/button.H gamePlay/shipYard.H objects/rocks/spawner.H gamePlay/stage.H bitmaps/starDestroyerThrustCenter.xbm bitmaps/starDestroyerThrustEdge.xbm bitmaps/starDestroyerThrustMiddle.xbm bitmaps/NCC1701DThrustDecor.xbm bitmaps/NCC1701AThrustDecor.xbm utilities/vector2d.H | $(OBJDIR)
	${CXX} ${CXXFLAGS} ${BACKEND_CXXFLAGS} -c $< -o $@

XAsteroids: obj/X11/XAsteroids.o obj/X11/rotatorDisplayData.o obj/X11/compositePixmap.o
	${CXX} ${CXXFLAGS} ${X11_BACKEND} obj/X11/XAsteroids.o obj/X11/rotatorDisplayData.o obj/X11/compositePixmap.o ${LDFLAGS} -lXm -lXt -lX11 -oXAsteroids

AutoRepeatOn: AutoRepeatOn.C
	${CXX} ${CXXFLAGS} ${X11_BACKEND} AutoRepeatOn.C ${LDFLAGS} -lX11 -o AutoRepeatOn

clean:
	\rm -rf XAsteroids AutoRepeatOn *.o *.u *.bak *.CKP obj/X11 obj/GL obj/VK obj/xbmDecodeSelfTest obj/harness
