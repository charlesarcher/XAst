// dyld_setenv_test.cpp — does dyld honor a setenv()'d DYLD_FALLBACK_LIBRARY_PATH
// for a LATER bare-name dlopen? (Determines if the MTL binary can self-provision
// the loader with zero env / zero makefile change.)
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

static int tryBare(const char* label) {
  void* h = dlopen("libvulkan.1.dylib", RTLD_NOW | RTLD_LOCAL);
  if (h) {
    fprintf(stderr, "[%s] bare dlopen(\"libvulkan.1.dylib\") -> FOUND\n", label);
    dlclose(h);
    return 1;
  }
  const char* e = dlerror();
  fprintf(stderr, "[%s] bare dlopen -> NOT FOUND (%s)\n", label, e ? e : "?");
  return 0;
}

int main() {
  fprintf(stderr, "DYLD_FALLBACK_LIBRARY_PATH at start = %s\n",
          getenv("DYLD_FALLBACK_LIBRARY_PATH") ? getenv("DYLD_FALLBACK_LIBRARY_PATH") : "(null)");
  // Baseline: no env manipulation.
  int base = tryBare("baseline");
  // Now setenv at runtime and retry.
  setenv("DYLD_FALLBACK_LIBRARY_PATH", "/opt/homebrew/lib", 1);
  setenv("DYLD_LIBRARY_PATH", "/opt/homebrew/lib", 1);
  int after = tryBare("after-setenv");
  fprintf(stderr, "=== RESULT: baseline=%d after-setenv=%d => runtime-setenv %s effective for dyld ===\n",
          base, after, (after && !base) ? "IS" : "is NOT");
  return 0;
}