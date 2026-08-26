#include<iostream>
#include<math.h>
#include"rotatorDisplayData.H"
#ifdef X11_BACKEND
#include<X11/Xutil.h>
#include<X11/Intrinsic.h>
#endif

using namespace std;

#ifdef X11_BACKEND
RotatorDisplayData::RotatorDisplayData(RenderingEngine& eng,
                                       const Vector2d* const vecs,
                                       const int numVecs): display((Display*)eng.nativeHandle().display),
                                                           radius(0),
                                                           area(0)

 {double r=vecs[0].MagnitudeSquared();
  for(int i=0;i<numVecs;++i)
   {if (r>radius)
      radius=r;
    int j=i+1==numVecs ? 0
                       : i+1;
    double s=vecs[j].MagnitudeSquared(),
           t=(vecs[i]-vecs[j]).MagnitudeSquared(),
           temp=r-s+t;
    area+=sqrt(4*r*t-temp*temp);
    r=s;
   }
  area*=.25;
  radius=sqrt(radius);
  sideSize=2*radius+1.5;
  box.SetBox(Vector2d(),radius);
 }

RotatorDisplayData::RotatorDisplayData(RenderingEngine& eng,
                                       const Vector2d* const vecs, const int numVecs,
				                               const double a): display((Display*)eng.nativeHandle().display),
                                                        radius(0),
                                                        area(a)
 {for (int i=0;i<numVecs;++i)
   {double r=vecs[i].MagnitudeSquared();
    if (r>radius)
      radius=r;
   }
  radius=sqrt(radius);
  sideSize=2*radius+1.5;
  box.SetBox(Vector2d(),radius);
 }

RotatorDisplayData::RotatorDisplayData(RenderingEngine& eng,
                                       const int sideSz): display((Display*)eng.nativeHandle().display),
                                                          radius((sideSz-1)/2.0),
                                                          area(0),
                                                          sideSize(sideSz)
 {
 }

RotatorDisplayData::~RotatorDisplayData()
 {
 }

const Vector2d* const RotatorDisplayData::GetVecs(const double angle) const
 {cout<<endl<<"Reference made to nonexistent RotatorDislayData::GetVecs vitual function.  Execution terminated."<<endl;
  abort();
  return NULL;
 }

const int RotatorDisplayData::GetNumVecs() const
 {return 0;
 }

GC& RotatorDisplayData::GetGC(const double angle) const
 {cout<<endl<<"Reference made to nonexistent RotatorDislayData::GetGC virtual function.  Execution terminated."<<endl;
  abort();
  return *(GC*)NULL;
 }

const Pixmap& RotatorDisplayData::GetMaskAtTime(const double angle) const
 {cout<<endl<<"Reference made to nonexistent RotatorDislayData::GetMaskAtTime virtual function.  Execution terminated."<<endl;
  abort();
  return *(Pixmap*)NULL;
 }

NonRotVectorData::NonRotVectorData(RenderingEngine& eng, const RotColor& color,
                                   
                                   const Vector2d* const vecs, const int nVecs): RotatorDisplayData(eng,vecs,nVecs),
                                                                                 numVecs(nVecs)
 {NoDecorInit((Drawable)eng.nativeHandle().window,color,vecs);
 }

NonRotVectorData::NonRotVectorData(RenderingEngine& eng, const RotColor& color,
                                   
                                   const Vector2d* const vecs, const int nVecs,
                                   const double area):
                                        RotatorDisplayData(eng,vecs,nVecs,area),
                                        numVecs(nVecs)
 {NoDecorInit((Drawable)eng.nativeHandle().window,color,vecs);
 }


void NonRotVectorData::NoDecorInit(Drawable drawable,
                                   const RotColor& color,
				   const Vector2d* const vecs)
 {int screen=DefaultScreen(display),
      background=BlackPixel(display,screen);
  // The allocation request the referenced-XColor form issued, assembled from
  // the RotColor components (same request, same pixel).
  XColor requested_c47={0,color.red,color.green,color.blue,DoRed|DoGreen|DoBlue,0};
  int foreground=XAllocColor(display,DefaultColormap(display,screen),&requested_c47)
                 ? requested_c47.pixel
                 : WhitePixel(display,screen);
  GC gc=XCreateGC(display,drawable,0,NULL);
  BuildPixmap(drawable,screen,foreground,background,gc,vecs);
  XFreeGC(display,gc);
 }

NonRotVectorData::NonRotVectorData(RenderingEngine& eng, const RotColor& color,
                                   
                                   const Vector2d* const vecs, const int nVecs,
                                   const unsigned char* const bitmap,
                                   const int width , const int height): RotatorDisplayData(eng,vecs,nVecs),
                                                                        numVecs(nVecs)
 {BitmapDecorInit((Drawable)eng.nativeHandle().window,color,vecs,bitmap,width,height);
 }


NonRotVectorData::NonRotVectorData(RenderingEngine& eng, const RotColor& color,
                                   
                                   const Vector2d* const vecs, const int nVecs,
                                   const double area,
                                   const unsigned char* const bitmap,
                                   const int width , const int height): RotatorDisplayData(eng,vecs,nVecs,area),
                                                                        numVecs(nVecs)
 {BitmapDecorInit((Drawable)eng.nativeHandle().window,color,vecs,bitmap,width,height);
 }

void NonRotVectorData::BitmapDecorInit(Drawable drawable,
                                       const RotColor& color,
                                       const Vector2d* const vecs,
                                       const unsigned char* const bitmap, const int width, const int height)
 {int screen=DefaultScreen(display),
      background=BlackPixel(display,screen);
  // The allocation request the referenced-XColor form issued, assembled from
  // the RotColor components (same request, same pixel).
  XColor requested_c47={0,color.red,color.green,color.blue,DoRed|DoGreen|DoBlue,0};
  int foreground=XAllocColor(display,DefaultColormap(display,screen),&requested_c47)
                 ? requested_c47.pixel
                 : WhitePixel(display,screen);
  GC gc=XCreateGC(display,drawable,0,NULL);
  BuildPixmap(drawable,screen,foreground,background,gc,vecs);
  Pixmap bitPixmap=XCreateBitmapFromData(display,drawable,
                                         (const char * const)bitmap,width,height);
  XSetGraphicsExposures(display,gc,FALSE);
  XCopyPlane(display,bitPixmap,pixmap,gc,
             0,0,width,height,
             (sideSize-width)/2,(sideSize-height)/2,
             1);
  XFreePixmap(display,bitPixmap);
  XFreeGC(display,gc);
 }

NonRotVectorData::NonRotVectorData(RenderingEngine& eng, const RotColor& color,
                                   
                                   const Vector2d* const vecs, const int nVecs,
			                             const CompositePixmap& compositePixmap): RotatorDisplayData(eng,vecs,nVecs),
                                                                            numVecs(nVecs)
 {PixmapDecorInit((Drawable)eng.nativeHandle().window,color,vecs,compositePixmap);
 }

NonRotVectorData::NonRotVectorData(RenderingEngine& eng, const RotColor& color,
                                   
                                   const Vector2d* const vecs, const int nVecs,
                                   const double area,
			                             const CompositePixmap& compositePixmap): RotatorDisplayData(eng,vecs,nVecs,area),
                                                                            numVecs(nVecs)
 {PixmapDecorInit((Drawable)eng.nativeHandle().window,color,vecs,compositePixmap);
 }

void NonRotVectorData::PixmapDecorInit(Drawable drawable,
                                       const RotColor& color,
                                       const Vector2d* const vecs,
                                       const CompositePixmap& compositePixmap)
 {int screen=DefaultScreen(display),
      background=BlackPixel(display,screen);
  // The allocation request the referenced-XColor form issued, assembled from
  // the RotColor components (same request, same pixel).
  XColor requested_c47={0,color.red,color.green,color.blue,DoRed|DoGreen|DoBlue,0};
  int foreground=XAllocColor(display,DefaultColormap(display,screen),&requested_c47)
                 ? requested_c47.pixel
                 : WhitePixel(display,screen);
  GC gc=XCreateGC(display,drawable,0,NULL);
  BuildPixmap(drawable,screen,foreground,background,gc,vecs);
  XSetGraphicsExposures(display,gc,FALSE);
  XCopyArea(display,compositePixmap.GetPixmap(),pixmap,gc,
            0,0,compositePixmap.GetPixWidth(),compositePixmap.GetPixHeight(),
            (sideSize-compositePixmap.GetPixWidth())/2,
            (sideSize-compositePixmap.GetPixHeight())/2);
  XFreeGC(display,gc);
 }

void NonRotVectorData::BuildPixmap(Drawable drawable, const int screen,
				                           const int foreground, const int background, GC& gc,
                                   const Vector2d* const vecs)
 {vectors=new Vector2d[numVecs];
  XSetLineAttributes(display,gc,1,LineSolid,CapButt,JoinMiter);
  XSetBackground(display,gc,background);
  XPoint* points=new XPoint[numVecs+1];
  pixmap=XCreatePixmap(display,drawable,sideSize,sideSize,DefaultDepth(display,screen));
  XSetForeground(display,gc,background);
  XFillRectangle(display,pixmap,gc,0,0,sideSize,sideSize);
  for(int i=0;i<numVecs;++i)
   {vectors[i]=vecs[i];
    points[i].x=vectors[i].x+radius+.5;
    points[i].y=vectors[i].y+radius+.5;
   }
  points[numVecs]=points[0];
  XSetForeground(display,gc,foreground);
  XDrawLines(display,pixmap,gc,points,numVecs+1,CoordModeOrigin);
  delete [] points;
 }

NonRotVectorData::~NonRotVectorData()
 {XFreePixmap(display,pixmap);
  delete [] vectors;
 }

const Vector2d* const NonRotVectorData::GetVecs(const double angle) const
 {return vectors;
 }

const int NonRotVectorData::GetNumVecs() const
 {return numVecs;
 }

const Pixmap& NonRotVectorData::GetPixmap(const double angle) const
 {return pixmap;
 }

const int NonRotVectorData::GetNumPix() const
 {return 1;
 }

RotVectorData::RotVectorData(RenderingEngine& eng, const RotColor& color,
                             
                             const Vector2d* const vecs, const int nVecs): RotatorDisplayData(eng,vecs,nVecs),
                                                                           numVecs(nVecs)
 {NoDecorInit((Drawable)eng.nativeHandle().window,color,vecs);
 }


RotVectorData::RotVectorData(RenderingEngine& eng, const RotColor& color,
                             
                             const Vector2d* const vecs, const int nVecs,
                             const double area): RotatorDisplayData(eng,vecs,nVecs,area),
                                                 numVecs(nVecs)
 {NoDecorInit((Drawable)eng.nativeHandle().window,color,vecs);
 }

void RotVectorData::NoDecorInit(Drawable drawable,
                                const RotColor& color,
                                const Vector2d* const vecs)
 {int screen=DefaultScreen(display),
      background=BlackPixel(display,screen);
  // The allocation request the referenced-XColor form issued, assembled from
  // the RotColor components (same request, same pixel).
  XColor requested_c47={0,color.red,color.green,color.blue,DoRed|DoGreen|DoBlue,0};
  int foreground=XAllocColor(display,DefaultColormap(display,screen),&requested_c47)
                 ? requested_c47.pixel
                 : WhitePixel(display,screen);
  GC gc=XCreateGC(display,drawable,0,NULL);
  RotateVecs(drawable,screen,foreground,background,gc,vecs);
  XFreeGC(display,gc);
 }

RotVectorData::RotVectorData(RenderingEngine& eng, const RotColor& color,
                             
                             const Vector2d* const vecs, const int nVecs,
                             const unsigned char* const bitmap,
                             const int width , const int height): RotatorDisplayData(eng,vecs,nVecs),
                                                                  numVecs(nVecs)
 {BitmapDecorInit((Drawable)eng.nativeHandle().window,color,vecs,bitmap,width,height);
 }

RotVectorData::RotVectorData(RenderingEngine& eng, const RotColor& color,
                             
                             const Vector2d* const vecs, const int nVecs,
                             const double area,
                             const unsigned char* const bitmap,
                             const int width , const int height): RotatorDisplayData(eng,vecs,nVecs,area),
                                                                  numVecs(nVecs)
 {BitmapDecorInit((Drawable)eng.nativeHandle().window,color,vecs,bitmap,width,height);
 }

void RotVectorData::BitmapDecorInit(Drawable drawable,
                                    const RotColor& color,
                                    const Vector2d* const vecs,
                                    const unsigned char* const bitmap, const int width, const int height)
 {int screen=DefaultScreen(display),
      background=BlackPixel(display,screen);
  // The allocation request the referenced-XColor form issued, assembled from
  // the RotColor components (same request, same pixel).
  XColor requested_c47={0,color.red,color.green,color.blue,DoRed|DoGreen|DoBlue,0};
  int foreground=XAllocColor(display,DefaultColormap(display,screen),&requested_c47)
                 ? requested_c47.pixel
                 : WhitePixel(display,screen);
  GC gc=XCreateGC(display,drawable,0,NULL);
  RotateVecs(drawable,screen,foreground,background,gc,vecs);
  int lineFill=8-width%8,
      bitNum=0;
  for(int j=0;j<height;++j)
   {for(int i=0;i<width;++i)
     {if (bitmap[bitNum/8]>>bitNum%8&0x01)
        for(int k=0;k<numPix;++k)
         {double x=i-.5*(width-1),
                 y=j-.5*(height-1),
          angle=k*incAngle;
          int xRot=x*cos(angle)-y*sin(angle)+radius+.5,
              yRot=y*cos(angle)+x*sin(angle)+radius+.5;
          XDrawPoint(display,rotatedPix[k],gc,xRot,yRot);
         }
      ++bitNum;
     }
    bitNum+=lineFill;
   }
  XFreeGC(display,gc);
 }

RotVectorData::RotVectorData(RenderingEngine& eng, const RotColor& color,
                             
                             const Vector2d* const vecs, const int nVecs,
			                       const CompositePixmap& compositePixmap): RotatorDisplayData(eng,vecs,nVecs),
                                                                      numVecs(nVecs)
 {PixmapDecorInit((Drawable)eng.nativeHandle().window,color,vecs,compositePixmap);
 }

RotVectorData::RotVectorData(RenderingEngine& eng, const RotColor& color,
                             
                             const Vector2d* const vecs, const int nVecs,
                             const double area,
			                       const CompositePixmap& compositePixmap): RotatorDisplayData(eng,vecs,nVecs,area),
                                                                      numVecs(nVecs)
 {PixmapDecorInit((Drawable)eng.nativeHandle().window,color,vecs,compositePixmap);
 }

void RotVectorData::PixmapDecorInit(Drawable drawable,
                                    const RotColor& color,
                                    const Vector2d* const vecs,
                                    const CompositePixmap& compositePixmap)
 {int screen=DefaultScreen(display),
      background=BlackPixel(display,screen);
  // The allocation request the referenced-XColor form issued, assembled from
  // the RotColor components (same request, same pixel).
  XColor requested_c47={0,color.red,color.green,color.blue,DoRed|DoGreen|DoBlue,0};
  int foreground=XAllocColor(display,DefaultColormap(display,screen),&requested_c47)
                 ? requested_c47.pixel
                 : WhitePixel(display,screen);
  GC gc=XCreateGC(display,drawable,0,NULL);
  RotateVecs(drawable,screen,foreground,background,gc,vecs);
  XImage* compositeImage=XGetImage(display,compositePixmap.GetPixmap(),
                                   0,0,compositePixmap.GetPixWidth(),compositePixmap.GetPixHeight(),
                                   AllPlanes,ZPixmap);
  for(int j=0;j<compositePixmap.GetPixHeight();++j)
    for(int i=0;i<compositePixmap.GetPixWidth();++i)
     {int pixel=XGetPixel(compositeImage,i,j);
      if (pixel!=background)
        for(int k=0;k<numPix;++k)
         {double x=i-.5*(compositePixmap.GetPixWidth()-1),
                 y=j-.5*(compositePixmap.GetPixHeight()-1),
	        angle=k*incAngle;
          int xRot=x*cos(angle)-y*sin(angle)+radius+.5,
              yRot=y*cos(angle)+x*sin(angle)+radius+.5;
          XSetForeground(display,gc,pixel);
          XDrawPoint(display,rotatedPix[k],gc,xRot,yRot);
	       }
     }
  XDestroyImage(compositeImage);
  XFreeGC(display,gc);
 }

void RotVectorData::RotateVecs(Drawable drawable, const int screen,
                               const int foreground, const int background, GC& gc,
                               const Vector2d* const vecs)
 {numPix=6.28318530717958*radius+.5;
  incAngle=6.28318530717958/numPix;
  rotatedVecs=new Vector2d*[numPix];
  rotatedPix=new Pixmap[numPix];
  XSetLineAttributes(display,gc,1,LineSolid,CapButt,JoinMiter);
  XSetBackground(display,gc,background);
  XPoint* points=new XPoint[numVecs+1];
  for(int i=0;i<numPix;++i)
   {rotatedVecs[i]=new Vector2d[numVecs];
    rotatedPix[i]=XCreatePixmap(display,drawable,sideSize,sideSize,DefaultDepth(display,screen));
    XSetForeground(display,gc,background);
    XFillRectangle(display,rotatedPix[i],gc,0,0,sideSize,sideSize);
    double angle=i*incAngle;
    for(int j=0;j<numVecs;++j)
     {rotatedVecs[i][j]=vecs[j].Rotate(angle);
      points[j].x=rotatedVecs[i][j].x+radius+.5;
      points[j].y=rotatedVecs[i][j].y+radius+.5;
     }
    points[numVecs]=points[0];
    XSetForeground(display,gc,foreground);
    XDrawLines(display,rotatedPix[i],gc,points,numVecs+1,CoordModeOrigin);
   }
  delete [] points;
 }

RotVectorData::~RotVectorData()
 {for(int i=numPix;i--;)
   {XFreePixmap(display,rotatedPix[i]);
    delete [] rotatedVecs[i];
   }
  delete [] rotatedPix;
  delete [] rotatedVecs;
 }

const Vector2d* const RotVectorData::GetVecs(const double angle) const
 {return rotatedVecs[int((fmod(angle,6.28318530717958)+6.28318530717958)
                         /incAngle+.5)%numPix];
 }

const int RotVectorData::GetNumVecs() const
 {return numVecs;
 }

const Pixmap& RotVectorData::GetPixmap(const double angle) const
 {return rotatedPix[int((fmod(angle,6.28318530717958)+6.28318530717958)
                        /incAngle+.5)%numPix];
 }

const int RotVectorData::GetNumPix() const
 {return numPix;
 }

MaskedRotVectorData::MaskedRotVectorData(RenderingEngine& eng, const RotColor& color,
                                         
                                         const Vector2d* const vecs, const int nVecs): RotatorDisplayData(eng,vecs,nVecs),
                                                                                       numVecs(nVecs)
 {NoDecorInit((Drawable)eng.nativeHandle().window,color,vecs);
 }

MaskedRotVectorData::MaskedRotVectorData(RenderingEngine& eng, const RotColor& color,
                                         
                                         const Vector2d* const vecs, const int nVecs,
                                         const double area): RotatorDisplayData(eng,vecs,nVecs,area),
                                                             numVecs(nVecs)
 {NoDecorInit((Drawable)eng.nativeHandle().window,color,vecs);
 }

void MaskedRotVectorData::NoDecorInit(Drawable drawable,
                                      const RotColor& color,
                                      const Vector2d* const vecs)
 {int screen=DefaultScreen(display),
      background=BlackPixel(display,screen);
  // The allocation request the referenced-XColor form issued, assembled from
  // the RotColor components (same request, same pixel).
  XColor requested_c47={0,color.red,color.green,color.blue,DoRed|DoGreen|DoBlue,0};
  int foreground=XAllocColor(display,DefaultColormap(display,screen),&requested_c47)
                 ? requested_c47.pixel
                 : WhitePixel(display,screen);
  GC gc=XCreateGC(display,drawable,0,NULL);
  RotateVecs(drawable,screen,foreground,background,gc,vecs);
  XFreeGC(display,gc);
 }

MaskedRotVectorData::MaskedRotVectorData(RenderingEngine& eng, const RotColor& color,
                                         
                                         const Vector2d* const vecs, const int nVecs,
			                                   const unsigned char* const bitmap,
                                         const int width , const int height): RotatorDisplayData(eng,vecs,nVecs),
                                                                              numVecs(nVecs)
 {BitmapDecorInit((Drawable)eng.nativeHandle().window,color,vecs,bitmap,width,height);
 }

MaskedRotVectorData::MaskedRotVectorData(RenderingEngine& eng, const RotColor& color,
                                         
                                         const Vector2d* const vecs, const int nVecs,
                                         const double area,
			                                   const unsigned char* const bitmap,
                                         const int width , const int height): RotatorDisplayData(eng,vecs,nVecs,area),
                                                                              numVecs(nVecs)
 {BitmapDecorInit((Drawable)eng.nativeHandle().window,color,vecs,bitmap,width,height);
 }

void MaskedRotVectorData::BitmapDecorInit(Drawable drawable,
                                          const RotColor& color,
                                          const Vector2d* const vecs,
	                                        const unsigned char* const bitmap, const int width, const int height)
 {int screen=DefaultScreen(display),
      background=BlackPixel(display,screen);
  // The allocation request the referenced-XColor form issued, assembled from
  // the RotColor components (same request, same pixel).
  XColor requested_c47={0,color.red,color.green,color.blue,DoRed|DoGreen|DoBlue,0};
  int foreground=XAllocColor(display,DefaultColormap(display,screen),&requested_c47)
                 ? requested_c47.pixel
                 : WhitePixel(display,screen);
  GC gc=XCreateGC(display,drawable,0,NULL);
  RotateVecs(drawable,screen,foreground,background,gc,vecs);
  int lineFill=8-width%8,
       bitNum=0;
  for(int j=0;j<height;++j)
   {for(int i=0;i<width;++i)
      {if (bitmap[bitNum/8]>>bitNum%8&0x01)
         for(int k=0;k<numPix;++k)
          {double x=i-.5*(width-1),
                  y=j-.5*(height-1),
	                angle=k*incAngle;
           int xRot=x*cos(angle)-y*sin(angle)+radius+.5,
               yRot=y*cos(angle)+x*sin(angle)+radius+.5;
           XDrawPoint(display,rotatedPix[k],gc,xRot,yRot);
	        }
       ++bitNum;
      }
     bitNum+=lineFill;
    }
  XFreeGC(display,gc);
 }

MaskedRotVectorData::MaskedRotVectorData(RenderingEngine& eng, const RotColor& color,
                                         
                                         const Vector2d* const vecs, const int nVecs,
			                                   const CompositePixmap& compositePixmap): RotatorDisplayData(eng,vecs,nVecs),
                                                                                  numVecs(nVecs)
 {PixmapDecorInit((Drawable)eng.nativeHandle().window,color,vecs,compositePixmap);
 }

MaskedRotVectorData::MaskedRotVectorData(RenderingEngine& eng, const RotColor& color,
                                         
                                         const Vector2d* const vecs, const int nVecs,
                                         const double area,
			                                   const CompositePixmap& compositePixmap): RotatorDisplayData(eng,vecs,nVecs,area),
                                                                                  numVecs(nVecs)
 {PixmapDecorInit((Drawable)eng.nativeHandle().window,color,vecs,compositePixmap);
 }

void MaskedRotVectorData::PixmapDecorInit(Drawable drawable,
                                          const RotColor& color,
                                          const Vector2d* const vecs,
                                          const CompositePixmap& compositePixmap)
 {int screen=DefaultScreen(display),
      background=BlackPixel(display,screen);
  // The allocation request the referenced-XColor form issued, assembled from
  // the RotColor components (same request, same pixel).
  XColor requested_c47={0,color.red,color.green,color.blue,DoRed|DoGreen|DoBlue,0};
  int foreground=XAllocColor(display,DefaultColormap(display,screen),&requested_c47)
                 ? requested_c47.pixel
                 : WhitePixel(display,screen);
  GC gc=XCreateGC(display,drawable,0,NULL);
  RotateVecs(drawable,screen,foreground,background,gc,vecs);
  XImage* compositeImage=XGetImage(display,compositePixmap.GetPixmap(),
                                   0,0,compositePixmap.GetPixWidth(),compositePixmap.GetPixHeight(),
                                   AllPlanes,ZPixmap);
  for(int j=0;j<compositePixmap.GetPixHeight();++j)
    for(int i=0;i<compositePixmap.GetPixWidth();++i)
     {int pixel=XGetPixel(compositeImage,i,j);
      if (pixel!=background)
        for(int k=0;k<numPix;++k)
         {double x=i-.5*(compositePixmap.GetPixWidth()-1),
                 y=j-.5*(compositePixmap.GetPixHeight()-1),
                 angle=k*incAngle;
          int xRot=x*cos(angle)-y*sin(angle)+radius+.5,
              yRot=y*cos(angle)+x*sin(angle)+radius+.5;
          XSetForeground(display,gc,pixel);
          XDrawPoint(display,rotatedPix[k],gc,xRot,yRot);
	       }
     }
  XDestroyImage(compositeImage);
  XFreeGC(display,gc);
 }

void MaskedRotVectorData::RotateVecs(Drawable drawable, const int screen,
                               const int foreground, const int background, GC& gc,
                               const Vector2d* const vecs)
 {numPix=6.28318530717958*radius+.5;
  incAngle=6.28318530717958/numPix;
  rotatedVecs=new Vector2d*[numPix];
  rotatedPix=new Pixmap[numPix];
  clipMasks=new Pixmap[numPix];
  maskGC=new GC[numPix];
  XSetLineAttributes(display,gc,1,LineSolid,CapButt,JoinMiter);
  XSetBackground(display,gc,background);
  XPoint* points=new XPoint[numVecs+1];
  for(int i=0;i<numPix;++i)
   {rotatedVecs[i]=new Vector2d[numVecs];
    rotatedPix[i]=XCreatePixmap(display,drawable,sideSize,sideSize,DefaultDepth(display,screen));
    XSetForeground(display,gc,background);
    XFillRectangle(display,rotatedPix[i],gc,0,0,sideSize,sideSize);
    clipMasks[i]=XCreatePixmap(display,drawable,sideSize,sideSize,1);
    GC clipGC=XCreateGC(display,clipMasks[i],NULL,0);
    XSetBackground(display,clipGC,BlackPixel(display,screen));
    XSetLineAttributes(display,clipGC,1,LineSolid,CapButt,JoinMiter);
    XSetForeground(display,clipGC,BlackPixel(display,screen));
    XFillRectangle(display,clipMasks[i],clipGC,0,0,sideSize,sideSize);
    maskGC[i]=XCreateGC(display,drawable,NULL,0);
    XSetForeground(display,maskGC[i],foreground);
    XSetBackground(display,maskGC[i],background);
    XSetGraphicsExposures(display,maskGC[i],FALSE);
    double angle=i*incAngle;
    for(int j=0;j<numVecs;++j)
     {rotatedVecs[i][j]=vecs[j].Rotate(angle);
      points[j].x=rotatedVecs[i][j].x+radius+.5;
      points[j].y=rotatedVecs[i][j].y+radius+.5;
     }
    points[numVecs]=points[0];
    XSetForeground(display,gc,foreground);
    XDrawLines(display,rotatedPix[i],gc,points,numVecs+1,CoordModeOrigin);
    XSetForeground(display,clipGC,WhitePixel(display,screen));
    XDrawLines(display,clipMasks[i],clipGC,points,numVecs+1,CoordModeOrigin);
    XFillPolygon(display,clipMasks[i],clipGC,points,numVecs+1,Nonconvex,CoordModeOrigin);
    XSetClipMask(display,maskGC[i],clipMasks[i]);
    XFreeGC(display,clipGC);
   }
  delete [] points;
 }

MaskedRotVectorData::~MaskedRotVectorData()
 {for(int i=numPix;i--;)
   {XFreeGC(display,maskGC[i]);
    XFreePixmap(display,clipMasks[i]);
    XFreePixmap(display,rotatedPix[i]);
    delete [] rotatedVecs[i];
   }
  delete [] maskGC;
  delete [] clipMasks;
  delete [] rotatedPix;
  delete [] rotatedVecs;
 }

const Vector2d* const MaskedRotVectorData::GetVecs(const double angle) const
 {return rotatedVecs[int((fmod(angle,6.28318530717958)+6.28318530717958)
                         /incAngle+.5)%numPix];
 }

const int MaskedRotVectorData::GetNumVecs() const
 {return numVecs;
 }

const Pixmap& MaskedRotVectorData::GetPixmap(const double angle) const
 {return rotatedPix[int((fmod(angle,6.28318530717958)+6.28318530717958)
                        /incAngle+.5)%numPix];
 }

const int MaskedRotVectorData::GetNumPix() const
 {return numPix;
 }

GC& MaskedRotVectorData::GetGC(const double angle) const
 {return maskGC[int((fmod(angle,6.28318530717958)+6.28318530717958)
                /incAngle+.5)%numPix];
 }

const Pixmap& MaskedRotVectorData::GetMaskAtTime(const double angle) const
 {return clipMasks[int((fmod(angle,6.28318530717958)+6.28318530717958)
                /incAngle+.5)%numPix];
 }

RotPixmapData::RotPixmapData(RenderingEngine& eng, const RotColor& color,
                             
			     const unsigned char* const bitmap, const int width , const int height):
                                  RotatorDisplayData(eng,width>height ? width
                                                                       : height)
 {int screen=DefaultScreen(display),
      background=BlackPixel(display,screen);
  // The allocation request the referenced-XColor form issued, assembled from
  // the RotColor components (same request, same pixel).
  XColor requested_c47={0,color.red,color.green,color.blue,DoRed|DoGreen|DoBlue,0};
  int foreground=XAllocColor(display,DefaultColormap(display,screen),&requested_c47)
                 ? requested_c47.pixel
                 : WhitePixel(display,screen);
  GC gc=XCreateGC(display,(Drawable)eng.nativeHandle().window,0,NULL);
  CreatePix((Drawable)eng.nativeHandle().window,screen,background,gc);
  XSetForeground(display,gc,foreground);
  int lineFill=8-width%8,
      bitNum=0;
   for(int j=0;j<height;++j)
    {for(int i=0;i<width;++i)
      {if (bitmap[bitNum/8]>>bitNum%8&0x01)
         for(int k=0;k<numPix;++k)
          {double x=i-.5*(width-1),
                  y=j-.5*(height-1),
	         angle=k*incAngle;
           int xRot=x*cos(angle)-y*sin(angle)+radius+.5,
               yRot=y*cos(angle)+x*sin(angle)+radius+.5;
           XDrawPoint(display,rotatedPix[k],gc,xRot,yRot);
	        }
       ++bitNum;
      }
     bitNum+=lineFill;
    }
  XFreeGC(display,gc);
 }


RotPixmapData::RotPixmapData(RenderingEngine& eng, const CompositePixmap& compositePixmap): RotatorDisplayData(eng,compositePixmap.GetPixWidth()>compositePixmap.GetPixHeight()
                                                                                          ? compositePixmap.GetPixWidth()
                                                                                          : compositePixmap.GetPixHeight())
 {int screen=DefaultScreen(display),
      background=BlackPixel(display,screen);
  GC gc=XCreateGC(display,(Drawable)eng.nativeHandle().window,0,NULL);
  CreatePix((Drawable)eng.nativeHandle().window,screen,background,gc);
  XImage* compositeImage=XGetImage(display,compositePixmap.GetPixmap(),
                                    0,0,compositePixmap.GetPixWidth(),compositePixmap.GetPixHeight(),
                                    AllPlanes,ZPixmap);
   for(int j=0;j<compositePixmap.GetPixHeight();++j)
     for(int i=0;i<compositePixmap.GetPixWidth();++i)
      {int pixel=XGetPixel(compositeImage,i,j);
       if (pixel!=background)
         for(int k=0;k<numPix;++k)
          {double x=i-.5*(compositePixmap.GetPixWidth()-1),
                  y=j-.5*(compositePixmap.GetPixHeight()-1),
	         angle=k*incAngle;
           int xRot=x*cos(angle)-y*sin(angle)+radius+.5,
               yRot=y*cos(angle)+x*sin(angle)+radius+.5;
           XSetForeground(display,gc,pixel);
           XDrawPoint(display,rotatedPix[k],gc,xRot,yRot);
	        }
      }
  XDestroyImage(compositeImage);
  XFreeGC(display,gc);
 }

void RotPixmapData::CreatePix(Drawable drawable, const int screen, const int background, GC& gc)
 {numPix=6.28318530717958*radius+.5;
  incAngle=6.28318530717958/numPix;
  rotatedPix=new Pixmap[numPix];
  XSetBackground(display,gc,background);
  XSetForeground(display,gc,background);
  for(int i=0;i<numPix;++i)
   {rotatedPix[i]=XCreatePixmap(display,drawable,sideSize,sideSize,DefaultDepth(display,screen));
    XFillRectangle(display,rotatedPix[i],gc,0,0,sideSize,sideSize);
   }
 }

RotPixmapData::~RotPixmapData()
 {for(int i=numPix;i--;)
    XFreePixmap(display,rotatedPix[i]);
  delete [] rotatedPix;
 }

const Pixmap& RotPixmapData::GetPixmap(const double angle) const
 {return rotatedPix[int((fmod(angle,6.28318530717958)+6.28318530717958)
                        /incAngle+.5)%numPix];
 }

const int RotPixmapData::GetNumPix() const
 {return numPix;
 }

MaskedRotPixmapData::MaskedRotPixmapData(RenderingEngine& eng, const RotColor& color,
                                         
			                                   const unsigned char* const bitmap,
                                         const int width , const int height): RotatorDisplayData(eng,width>height
                                                                               ? width
                                                                               : height)
 {int screen=DefaultScreen(display),
      background=BlackPixel(display,screen);
  XColor requested_c47={0,color.red,color.green,color.blue,DoRed|DoGreen|DoBlue,0};
  int foreground=XAllocColor(display,DefaultColormap(display,screen),&requested_c47)
                 ? requested_c47.pixel
                 : WhitePixel(display,screen);
  GC gcPixmap,
     gcBitmap;
  CreatePix((Drawable)eng.nativeHandle().window,screen,background,gcPixmap,gcBitmap);
  XSetForeground(display,gcPixmap,foreground);
  int lineFill=8-width%8,
       bitNum=0;
   for(int j=0;j<height;++j)
    {for(int i=0;i<width;++i)
      {if (bitmap[bitNum/8]>>bitNum%8&0x01)
         for(int k=0;k<numPix;++k)
          {double x=i-.5*(width-1),
                 y=j-.5*(height-1),
	         angle=k*incAngle;
           int xRot=x*cos(angle)-y*sin(angle)+radius+.5,
               yRot=y*cos(angle)+x*sin(angle)+radius+.5;
           XDrawPoint(display,rotatedPix[k],gcPixmap,xRot,yRot);
           XDrawPoint(display,clipMasks[k],gcBitmap,xRot,yRot);
	        }
       ++bitNum;
      }
     bitNum+=lineFill;
    }
  XFreeGC(display,gcBitmap);
  XFreeGC(display,gcPixmap);
  for(int i=0;i<numPix;++i)
    XSetClipMask(display,maskGC[i],clipMasks[i]);
 }


MaskedRotPixmapData::MaskedRotPixmapData(RenderingEngine& eng, const CompositePixmap& compositePixmap): RotatorDisplayData(eng,compositePixmap.GetPixWidth()>compositePixmap.GetPixHeight()
                                                                                   ? compositePixmap.GetPixWidth()
                                                                                   : compositePixmap.GetPixHeight())
 {int screen=DefaultScreen(display),
      background=BlackPixel(display,screen);
  GC gcPixmap,
     gcBitmap;
  CreatePix((Drawable)eng.nativeHandle().window,screen,background,gcPixmap,gcBitmap);
  XImage* compositeImage=XGetImage(display,compositePixmap.GetPixmap(),
                                    0,0,compositePixmap.GetPixWidth(),compositePixmap.GetPixHeight(),
                                    AllPlanes,ZPixmap);
  for(int j=0;j<compositePixmap.GetPixHeight();++j)
    for(int i=0;i<compositePixmap.GetPixWidth();++i)
     {int pixel=XGetPixel(compositeImage,i,j);
      if (pixel!=background)
        for(int k=0;k<numPix;++k)
         {double x=i-.5*(compositePixmap.GetPixWidth()-1),
                 y=j-.5*(compositePixmap.GetPixHeight()-1),
	               angle=k*incAngle;
          int xRot=x*cos(angle)-y*sin(angle)+radius+.5,
              yRot=y*cos(angle)+x*sin(angle)+radius+.5;
          XSetForeground(display,gcPixmap,pixel);
          XDrawPoint(display,rotatedPix[k],gcPixmap,xRot,yRot);
          XDrawPoint(display,clipMasks[k],gcBitmap,xRot,yRot);
	       }
     }
  XDestroyImage(compositeImage);
  XFreeGC(display,gcBitmap);
  XFreeGC(display,gcPixmap);
  for(int i=0;i<numPix;++i)
    XSetClipMask(display,maskGC[i],clipMasks[i]);
 }

void MaskedRotPixmapData::CreatePix(Drawable drawable, const int screen,
                                    const int background,
                                    GC& gcPixmap, GC& gcBitmap)
 {numPix=6.28318530717958*radius+.5;
  incAngle=6.28318530717958/numPix;
  rotatedPix=new Pixmap[numPix];
  clipMasks=new Pixmap[numPix];
  maskGC=new GC[numPix];
  gcPixmap=XCreateGC(display,drawable,0,NULL);
  XSetBackground(display,gcPixmap,background);
  XSetForeground(display,gcPixmap,background);
  rotatedPix[0]=XCreatePixmap(display,drawable,sideSize,sideSize,DefaultDepth(display,screen));
  XFillRectangle(display,rotatedPix[0],gcPixmap,0,0,sideSize,sideSize);
  clipMasks[0]=XCreatePixmap(display,drawable,sideSize,sideSize,1);
  gcBitmap=XCreateGC(display,clipMasks[0],NULL,0);
  XSetBackground(display,gcBitmap,background);
  XSetForeground(display,gcBitmap,background);
  XFillRectangle(display,clipMasks[0],gcBitmap,0,0,sideSize,sideSize);
  maskGC[0]=XCreateGC(display,drawable,NULL,0);
  XSetForeground(display,maskGC[0],background);
  XSetBackground(display,maskGC[0],background);
  XSetGraphicsExposures(display,maskGC[0],FALSE);
  for(int i=1;i<numPix;++i)
   {rotatedPix[i]=XCreatePixmap(display,drawable,sideSize,sideSize,DefaultDepth(display,screen));
    XFillRectangle(display,rotatedPix[i],gcPixmap,0,0,sideSize,sideSize);
    clipMasks[i]=XCreatePixmap(display,drawable,sideSize,sideSize,1);
    XFillRectangle(display,clipMasks[i],gcBitmap,0,0,sideSize,sideSize);
    maskGC[i]=XCreateGC(display,drawable,NULL,0);
    XSetForeground(display,maskGC[i],background);
    XSetBackground(display,maskGC[i],background);
    XSetGraphicsExposures(display,maskGC[i],FALSE);
   }
  XSetForeground(display,gcBitmap,WhitePixel(display,screen));
  XSetLineAttributes(display,gcBitmap,1,LineSolid,CapButt,JoinMiter);
 }

MaskedRotPixmapData::~MaskedRotPixmapData()
 {for(int i=numPix;i--;)
   {XFreeGC(display,maskGC[i]);
    XFreePixmap(display,clipMasks[i]);
    XFreePixmap(display,rotatedPix[i]);
   }
  delete [] maskGC;
  delete [] clipMasks;
  delete [] rotatedPix;
 }

const Pixmap& MaskedRotPixmapData::GetPixmap(const double angle) const
 {return rotatedPix[int((fmod(angle,6.28318530717958)+6.28318530717958)
                        /incAngle+.5)%numPix];
 }

const int MaskedRotPixmapData::GetNumPix() const
 {return numPix;
 }


GC& MaskedRotPixmapData::GetGC(const double angle) const
 {return maskGC[int((fmod(angle,6.28318530717958)+6.28318530717958)
                /incAngle+.5)%numPix];
 }

const Pixmap& MaskedRotPixmapData::GetMaskAtTime(const double angle) const
 {return clipMasks[int((fmod(angle,6.28318530717958)+6.28318530717958)
                /incAngle+.5)%numPix];
 }
#else
#include<cstdlib>

// ---------------------------------------------------------------------------
// Engine-rotation data path — the D14 `#else` branch (rendering-abstraction
// task 27, re-signed at task 47: the transitional Display/Drawable/XColor
// parameters are gone, the color rides RotColor components). No server
// resources exist in this configuration: the constructors capture the
// geometric outline, the decor bitmap reference and the 16-bit color
// components so the rotation consumer (plan task 35) can drive
// setTransform/drawTexture/drawTextureMasked from this data. The vector
// pre-rotation mirrors the guarded pipeline's math exactly (same angle count
// formula, same index formula), so both paths agree element-for-element.
// ---------------------------------------------------------------------------

RotatorDisplayData::RotatorDisplayData(RenderingEngine&,
                                       const Vector2d* const vecs,
                                       const int numVecs):
                                                           radius(0),
                                                           area(0)

 {double r=vecs[0].MagnitudeSquared();
  for(int i=0;i<numVecs;++i)
   {if (r>radius)
      radius=r;
    int j=i+1==numVecs ? 0
                       : i+1;
    double s=vecs[j].MagnitudeSquared(),
           t=(vecs[i]-vecs[j]).MagnitudeSquared(),
           temp=r-s+t;
    area+=sqrt(4*r*t-temp*temp);
    r=s;
   }
  area*=.25;
  radius=sqrt(radius);
  sideSize=2*radius+1.5;
  box.SetBox(Vector2d(),radius);
 }

RotatorDisplayData::RotatorDisplayData(RenderingEngine&,
                                       const Vector2d* const vecs, const int numVecs,
				                               const double a):
                                                         radius(0),
                                                         area(a)
 {for (int i=0;i<numVecs;++i)
   {double r=vecs[i].MagnitudeSquared();
    if (r>radius)
      radius=r;
   }
  radius=sqrt(radius);
  sideSize=2*radius+1.5;
  box.SetBox(Vector2d(),radius);
 }

RotatorDisplayData::RotatorDisplayData(RenderingEngine&,
                                       const int sideSz):
                                                          radius((sideSz-1)/2.0),
                                                          area(0),
                                                          sideSize(sideSz)
 {
 }

RotatorDisplayData::~RotatorDisplayData()
 {
 }

const Vector2d* const RotatorDisplayData::GetVecs(const double angle) const
 {cout<<endl<<"Reference made to nonexistent RotatorDislayData::GetVecs vitual function.  Execution terminated."<<endl;
  abort();
  return NULL;
 }

const int RotatorDisplayData::GetNumVecs() const
 {return 0;
 }

NonRotVectorData::NonRotVectorData(RenderingEngine& eng, const RotColor& color,
                                   const Vector2d* const vecs, const int nVecs): RotatorDisplayData(eng,vecs,nVecs),
                                                                                 numVecs(nVecs)
 {decorRed=color.red;
  decorGreen=color.green;
  decorBlue=color.blue;
  decorBits=NULL;
  decorWidth=0;
  decorHeight=0;
  vectors=new Vector2d[numVecs];
  for(int i=0;i<numVecs;++i)
    vectors[i]=vecs[i];
 }

NonRotVectorData::NonRotVectorData(RenderingEngine& eng, const RotColor& color,
                                   const Vector2d* const vecs, const int nVecs,
                                   const unsigned char* const bitmap,
                                   const int width , const int height): RotatorDisplayData(eng,vecs,nVecs),
                                                                        numVecs(nVecs)
 {decorRed=color.red;
  decorGreen=color.green;
  decorBlue=color.blue;
  decorBits=bitmap;
  decorWidth=width;
  decorHeight=height;
  vectors=new Vector2d[numVecs];
  for(int i=0;i<numVecs;++i)
    vectors[i]=vecs[i];
 }

NonRotVectorData::~NonRotVectorData()
 {delete [] vectors;
 }

const Vector2d* const NonRotVectorData::GetVecs(const double angle) const
 {return vectors;
 }

const int NonRotVectorData::GetNumVecs() const
 {return numVecs;
 }

const int NonRotVectorData::GetNumPix() const
 {return 1;
 }

RotVectorData::RotVectorData(RenderingEngine& eng, const RotColor& color,
                             const Vector2d* const vecs, const int nVecs): RotatorDisplayData(eng,vecs,nVecs),
                                                                           numVecs(nVecs)
 {decorRed=color.red;
  decorGreen=color.green;
  decorBlue=color.blue;
  decorBits=NULL;
  decorWidth=0;
  decorHeight=0;
  RotateVecs(vecs);
 }

RotVectorData::RotVectorData(RenderingEngine& eng, const RotColor& color,
                             const Vector2d* const vecs, const int nVecs,
                             const double area): RotatorDisplayData(eng,vecs,nVecs,area),
                                                 numVecs(nVecs)
 {decorRed=color.red;
  decorGreen=color.green;
  decorBlue=color.blue;
  decorBits=NULL;
  decorWidth=0;
  decorHeight=0;
  RotateVecs(vecs);
 }

RotVectorData::RotVectorData(RenderingEngine& eng, const RotColor& color,
                             const Vector2d* const vecs, const int nVecs,
                             const unsigned char* const bitmap,
                             const int width , const int height): RotatorDisplayData(eng,vecs,nVecs),
                                                                  numVecs(nVecs)
 {decorRed=color.red;
  decorGreen=color.green;
  decorBlue=color.blue;
  decorBits=bitmap;
  decorWidth=width;
  decorHeight=height;
  RotateVecs(vecs);
 }

RotVectorData::RotVectorData(RenderingEngine& eng, const RotColor& color,
                             const Vector2d* const vecs, const int nVecs,
                             const double area,
                             const unsigned char* const bitmap,
                             const int width , const int height): RotatorDisplayData(eng,vecs,nVecs,area),
                                                                  numVecs(nVecs)
 {decorRed=color.red;
  decorGreen=color.green;
  decorBlue=color.blue;
  decorBits=bitmap;
  decorWidth=width;
  decorHeight=height;
  RotateVecs(vecs);
 }

void RotVectorData::RotateVecs(const Vector2d* const vecs)
 {numPix=6.28318530717958*radius+.5;
  incAngle=6.28318530717958/numPix;
  rotatedVecs=new Vector2d*[numPix];
  for(int i=0;i<numPix;++i)
   {rotatedVecs[i]=new Vector2d[numVecs];
    double angle=i*incAngle;
    for(int j=0;j<numVecs;++j)
      rotatedVecs[i][j]=vecs[j].Rotate(angle);
   }
 }

RotVectorData::~RotVectorData()
 {for(int i=numPix;i--;)
    delete [] rotatedVecs[i];
  delete [] rotatedVecs;
 }

const Vector2d* const RotVectorData::GetVecs(const double angle) const
 {return rotatedVecs[int((fmod(angle,6.28318530717958)+6.28318530717958)
                         /incAngle+.5)%numPix];
 }

const int RotVectorData::GetNumVecs() const
 {return numVecs;
 }

const int RotVectorData::GetNumPix() const
 {return numPix;
 }

MaskedRotVectorData::MaskedRotVectorData(RenderingEngine& eng, const RotColor& color,
                                         const Vector2d* const vecs, const int nVecs): RotatorDisplayData(eng,vecs,nVecs),
                                                                                       numVecs(nVecs)
 {decorRed=color.red;
  decorGreen=color.green;
  decorBlue=color.blue;
  decorBits=NULL;
  decorWidth=0;
  decorHeight=0;
  RotateVecs(vecs);
 }

MaskedRotVectorData::MaskedRotVectorData(RenderingEngine& eng, const RotColor& color,
                                         const Vector2d* const vecs, const int nVecs,
			                                   const unsigned char* const bitmap,
                                         const int width , const int height): RotatorDisplayData(eng,vecs,nVecs),
                                                                              numVecs(nVecs)
 {decorRed=color.red;
  decorGreen=color.green;
  decorBlue=color.blue;
  decorBits=bitmap;
  decorWidth=width;
  decorHeight=height;
  RotateVecs(vecs);
 }

void MaskedRotVectorData::RotateVecs(const Vector2d* const vecs)
 {numPix=6.28318530717958*radius+.5;
  incAngle=6.28318530717958/numPix;
  rotatedVecs=new Vector2d*[numPix];
  for(int i=0;i<numPix;++i)
   {rotatedVecs[i]=new Vector2d[numVecs];
    double angle=i*incAngle;
    for(int j=0;j<numVecs;++j)
      rotatedVecs[i][j]=vecs[j].Rotate(angle);
   }
 }

MaskedRotVectorData::~MaskedRotVectorData()
 {for(int i=numPix;i--;)
    delete [] rotatedVecs[i];
  delete [] rotatedVecs;
 }

const Vector2d* const MaskedRotVectorData::GetVecs(const double angle) const
 {return rotatedVecs[int((fmod(angle,6.28318530717958)+6.28318530717958)
                         /incAngle+.5)%numPix];
 }

const int MaskedRotVectorData::GetNumVecs() const
 {return numVecs;
 }

const int MaskedRotVectorData::GetNumPix() const
 {return numPix;
 }

RotPixmapData::RotPixmapData(RenderingEngine& eng, const RotColor& color,
			     const unsigned char* const bitmap, const int width , const int height):
                                  RotatorDisplayData(eng,width>height ? width
                                                                       : height)
 {decorRed=color.red;
  decorGreen=color.green;
  decorBlue=color.blue;
  decorBits=bitmap;
  decorWidth=width;
  decorHeight=height;
  numPix=6.28318530717958*radius+.5;
  incAngle=6.28318530717958/numPix;
 }

RotPixmapData::~RotPixmapData()
 {
 }

const int RotPixmapData::GetNumPix() const
 {return numPix;
 }

MaskedRotPixmapData::MaskedRotPixmapData(RenderingEngine& eng, const RotColor& color,
			                                   const unsigned char* const bitmap,
                                         const int width , const int height): RotatorDisplayData(eng,width>height
                                                                               ? width
                                                                               : height)
 {decorRed=color.red;
  decorGreen=color.green;
  decorBlue=color.blue;
  decorBits=bitmap;
  decorWidth=width;
  decorHeight=height;
  numPix=6.28318530717958*radius+.5;
  incAngle=6.28318530717958/numPix;
 }

MaskedRotPixmapData::~MaskedRotPixmapData()
 {
 }

const int MaskedRotPixmapData::GetNumPix() const
 {return numPix;
 }
#endif
