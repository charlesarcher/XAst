// test/vk/vkinput.C — XTest injection helpers for vkmethods' D16 live proof.
// Deliberately a separate TU with C linkage: it includes the REAL X11 headers,
// which must never meet x11types.H's guards-closed anonymous-tag mirror in one
// TU (the task-45b probe.C lesson).
#include<X11/Xlib.h>
#include<X11/Xutil.h>
#include<X11/extensions/XTest.h>
#include<string.h>
#include<unistd.h>

static Display* dpy=NULL;
static Window g_win=None;
static int g_absX=0,g_absY=0;      // window client origin on the root
static int g_clientW=0,g_clientH=0;

extern "C" int xastInputOpen(void)
 {dpy=XOpenDisplay(NULL);
  return dpy!=NULL;
 }

extern "C" void xastInputClose(void)
 {if (dpy)
    XCloseDisplay(dpy);
  dpy=NULL;
 }

static Window findByName(Display* d,Window w,const char* name)
 {if (w!=DefaultRootWindow(d))
   {char* wn=NULL;
    if (XFetchName(d,w,&wn))
     {const int match=wn&&strcmp(wn,name)==0;
      if (wn)
        XFree(wn);
      if (match)
        return w;
     }
    }
  Window root=None,parent=None,*children=NULL;
  unsigned n=0;
  if (!XQueryTree(d,w,&root,&parent,&children,&n))
    return None;
  Window found=None;
  for (unsigned i=0;i<n&&found==None;++i)
    found=findByName(d,children[i],name);
  if (children)
    XFree(children);
  return found;
 }

extern "C" int xastFocusWindow(const char* name,int clientW,int clientH)
 {if (!dpy)
    return 0;
  Window win=findByName(dpy,DefaultRootWindow(dpy),name);
  if (win==None)
    return 0;
  g_win=win;
  g_clientW=clientW;
  g_clientH=clientH;
  Window rootReturn=None,child=None;
  unsigned w=0,h=0,bw=0,depth=0;
  if (!XGetGeometry(dpy,win,&rootReturn,&g_absX,&g_absY,&w,&h,&bw,&depth))
    return 0;
  XTranslateCoordinates(dpy,win,DefaultRootWindow(dpy),0,0,
                        &g_absX,&g_absY,&child);   // client ORIGIN on root
  XSetInputFocus(dpy,win,RevertToParent,CurrentTime);
  XWarpPointer(dpy,None,DefaultRootWindow(dpy),0,0,0,0,
               g_absX+clientW/2,g_absY+clientH/2);
  XSync(dpy,False);
  usleep(300000);
  return 1;
 }

extern "C" int xastInjectKeyChar(const char* character,int down)
 {if (!dpy)
    return 0;
  KeySym sym=XStringToKeysym(character);
  if (sym==NoSymbol)
    return 0;
  KeyCode kc=XKeysymToKeycode(dpy,sym);
  if (kc==0)
    return 0;
  return XTestFakeKeyEvent(dpy,kc,down?True:False,CurrentTime)==True;
 }

extern "C" void xastInjectMotion(int x,int y)
 {if (dpy)
    XTestFakeMotionEvent(dpy,0,g_absX+x,g_absY+y,CurrentTime);
 }

extern "C" int xastInjectButton(int button,int down)
 {if (!dpy)
    return 0;
  return XTestFakeButtonEvent(dpy,button,down?True:False,CurrentTime)==True;
 }

extern "C" void xastSync(void)
 {if (dpy)
    XSync(dpy,False);
 }
