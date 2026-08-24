// stbTruetypeImpl.C — the single translation unit expanding the
// stb_truetype implementation (task 31). Kept OUT of header-inline
// glBackend.H so the vendored third-party code never inflates the
// game TU's warning baseline (same precedent as glad.c: compiled with
// plain -O3, no targeted-warning flags, gcc/C — stb_truetype.h carries
// extern "C" guards so C++ consumers link cleanly).
#define STB_TRUETYPE_IMPLEMENTATION
#include"stb_truetype.h"
