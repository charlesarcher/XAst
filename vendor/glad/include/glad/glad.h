#ifndef GLAD_SHIM_H
#define GLAD_SHIM_H
/* Forwarding shim: real glad.h lives at include/GL/glad.h (see vendor/PINNED.md).
   Keeps glad.c-generated #include <glad/glad.h> working alongside the
   plan-required include/GL/ layout. */
#include "../GL/glad.h"
#endif
