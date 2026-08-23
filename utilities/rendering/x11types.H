#ifndef _X11TYPES_H
#define _X11TYPES_H

// Transitional backend seam (rendering-abstraction task 2; DELETED at task 47).
//
// Lets headers name X11 types while the real X11/Motif headers are stripped by
// their `#ifdef X11_BACKEND` include guards.  Under X11_BACKEND it just pulls in
// the real headers.  The stand-in typedefs deliberately mirror Xlib's own
// declarations exactly (LP64: XID is unsigned long), so this header may coexist
// with the real <X11/Xlib.h> in the same translation unit in either order.

#ifdef X11_BACKEND

#include<X11/Xlib.h>
#include<X11/Xutil.h>

#else

typedef struct _XDisplay Display;

typedef unsigned long XID;
typedef unsigned long Window;
typedef unsigned long Drawable;
typedef unsigned long Pixmap;
typedef unsigned long Colormap;
typedef unsigned long Font;

typedef struct _XGC *GC;

// XFontStruct must NOT be declared here, not even as `struct XFontStruct;`:
// Xlib.h defines it as `typedef struct XFontStruct { ... } XFontStruct;`
// (tag == typedef name), which g++ rejects when the name is already declared
// earlier in the TU.  Until the task-3/4 guards stop <X11/Xlib.h> from leaking
// in transitively (e.g. via options/button.H), the real header always follows
// this one, so the name stays entirely its.

struct _XImage;
typedef struct _XImage XImage;

#endif

#endif
