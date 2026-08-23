# BACKEND selects the compile configuration and object directory: X11 | GL | VK.
# Phase 0: only X11 links; GL/VK are compile-only (`make BACKEND=<B> objects`).
BACKEND?=X11

CXX=g++
CXXFLAGS=-I/usr/include/X11 -O3 -Wall -Wextra -Wno-unused-parameter -std=c++17
LDFLAGS=-L/usr/lib/X11
X11_BACKEND=-DX11_BACKEND
OBJDIR=obj/$(BACKEND)

ifeq ($(BACKEND),X11)
BACKEND_CXXFLAGS=$(X11_BACKEND)
endif

# Objects feeding the X11 link always carry the X11 macro, whatever BACKEND says.
obj/X11/XAsteroids.o obj/X11/rotatorDisplayData.o obj/X11/compositePixmap.o: BACKEND_CXXFLAGS=$(X11_BACKEND)

all: XAsteroids AutoRepeatOn

# Compile-only per-backend objects; no link step (GL/VK link rules: tasks 29/37).
# The XBM decode self-test (task 26) compiles into the GL/VK legs ONLY: the
# decode unit is backend-agnostic, so building it there proves it compiles
# WITHOUT -DX11_BACKEND. X11 keeps its native XCreateBitmapFromData paths.
GLVK_OBJECTS=
ifneq ($(filter $(BACKEND),GL VK),)
GLVK_OBJECTS=$(OBJDIR)/xbmDecodeSelfTest.o
endif

.PHONY: objects
objects: $(OBJDIR)/rotatorDisplayData.o $(OBJDIR)/compositePixmap.o $(OBJDIR)/XAsteroids.o $(GLVK_OBJECTS)

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/rotatorDisplayData.o: utilities/pixmaps/rotated/rotatorDisplayData.C utilities/pixmaps/rotated/rotatorDisplayData.H | $(OBJDIR)
	${CXX} ${CXXFLAGS} ${BACKEND_CXXFLAGS} -c $< -o $@

$(OBJDIR)/compositePixmap.o: utilities/pixmaps/composite/compositePixmap.C utilities/pixmaps/composite/compositePixmap.H | $(OBJDIR)
	${CXX} ${CXXFLAGS} ${BACKEND_CXXFLAGS} -c $< -o $@

$(OBJDIR)/xbmDecodeSelfTest.o: utilities/pixmaps/xbmDecodeSelfTest.C utilities/pixmaps/xbmDecode.H $(wildcard bitmaps/*.xbm) | $(OBJDIR)
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
