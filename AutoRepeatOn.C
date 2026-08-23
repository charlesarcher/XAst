#include<stdlib.h>
#include<iostream>
#ifdef X11_BACKEND
#include<X11/Xlib.h>
#endif
#include"utilities/rendering/x11types.H"
using namespace std;
int main ()
 {Display* display;
  if (!(display=XOpenDisplay(NULL)))
    cout<<"Could not open display."<<endl;
  else
   {XAutoRepeatOn(display);
    XCloseDisplay(display);
   }
  return 0;
 }
