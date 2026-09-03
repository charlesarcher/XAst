// getwid.c — print "windowid X Y W H" for the on-screen normal-layer (layer 0)
// window(s) of a given pid, using CGWindowListCopyWindowInfo. Robust for CLI
// processes (no System Events / PyObjC dependency). Usage: getwid <pid>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: getwid <pid>\n"); return 2; }
  pid_t want = (pid_t)atoi(argv[1]);
  CFArrayRef list = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly, kCGNullWindowID);
  if (!list) { fprintf(stderr, "no window list\n"); return 3; }
  int found = 0;
  CFIndex n = CFArrayGetCount(list);
  for (CFIndex i = 0; i < n; i++) {
    CFDictionaryRef d = (CFDictionaryRef)CFArrayGetValueAtIndex(list, i);
    CFNumberRef pr = (CFNumberRef)CFDictionaryGetValue(d, kCGWindowOwnerPID);
    if (!pr) continue;
    int wp = 0; CFNumberGetValue(pr, kCFNumberIntType, &wp);
    if ((pid_t)wp != want) continue;
    CFNumberRef lr = (CFNumberRef)CFDictionaryGetValue(d, kCGWindowLayer);
    int layer = lr ? 0 : 99; if (lr) CFNumberGetValue(lr, kCFNumberIntType, &layer);
    if (layer != 0) continue;
CFNumberRef wr = (CFNumberRef)CFDictionaryGetValue(d, kCGWindowNumber);
    int wid = 0; CFNumberGetValue(wr, kCFNumberIntType, &wid);
    double x = 0, y = 0, w = 0, h = 0;
    CFDictionaryRef bd = (CFDictionaryRef)CFDictionaryGetValue(d, kCGWindowBounds);
    if (bd) {
      CFNumberRef nr; double val;
      nr = (CFNumberRef)CFDictionaryGetValue(bd, CFSTR("X"));     if (nr) { CFNumberGetValue(nr, kCFNumberDoubleType, &val); x = val; }
      nr = (CFNumberRef)CFDictionaryGetValue(bd, CFSTR("Y"));     if (nr) { CFNumberGetValue(nr, kCFNumberDoubleType, &val); y = val; }
      nr = (CFNumberRef)CFDictionaryGetValue(bd, CFSTR("Width")); if (nr) { CFNumberGetValue(nr, kCFNumberDoubleType, &val); w = val; }
      nr = (CFNumberRef)CFDictionaryGetValue(bd, CFSTR("Height"));if (nr) { CFNumberGetValue(nr, kCFNumberDoubleType, &val); h = val; }
    }
    printf("%d %.0f %.0f %.0f %.0f\n", wid, x, y, w, h);
    found = 1;
  }
  CFRelease(list);
  return found ? 0 : 1;
}