#ifndef _ROTATORDISPLAYDATA_H
#define _ROTATORDISPLAYDATA_H


#ifdef X11_BACKEND
#include<X11/Xlib.h>
#endif
#include"../../vector2d.H"
#include"../../box.H"
#include"../../rendering/renderingEngine.H"
#include"../composite/compositePixmap.H"

#ifdef X11_BACKEND

class RotatorDisplayData
 {public:
    RotatorDisplayData(RenderingEngine&, const Vector2d* const, const int);
    RotatorDisplayData(RenderingEngine&, const Vector2d* const, const int, const double area);
    RotatorDisplayData(RenderingEngine&, const int);
    virtual ~RotatorDisplayData();
    virtual const Vector2d* const GetVecs(const double) const;
    virtual const int GetNumVecs() const;
    virtual const Pixmap& GetPixmap(const double) const=0;
    virtual const int GetNumPix() const=0;
    virtual GC& GetGC(const double) const;
    virtual const Pixmap& GetMaskAtTime(const double) const;
    const Box& GetBox() const;
    const double GetRadius() const;
    const double GetArea() const;
    const int GetPixWidth() const;
    const int GetPixHeight() const;

  protected:
    Display* const display;
    Box box;
    double radius,
           area;
    int sideSize;

  private:
    RotatorDisplayData& operator=(const RotatorDisplayData&);
  };

class NonRotVectorData: public RotatorDisplayData
 {public:
    NonRotVectorData(RenderingEngine&, const RotColor&,
                     const Vector2d* const, const int);
    NonRotVectorData(RenderingEngine&, const RotColor&,
                     const Vector2d* const, const int, const double area);
    NonRotVectorData(RenderingEngine&, const RotColor&,
                     const Vector2d* const, const int,
                     const unsigned char* const, const int, const int);
    NonRotVectorData(RenderingEngine&, const RotColor&,
                     const Vector2d* const, const int, const double area,
                     const unsigned char* const, const int, const int);
    NonRotVectorData(RenderingEngine&, const RotColor&,
                     const Vector2d* const, const int,
                     const CompositePixmap&);
    NonRotVectorData(RenderingEngine&, const RotColor&,
                     const Vector2d* const, const int, const double area,
                     const CompositePixmap&);
    virtual ~NonRotVectorData();
    virtual const Vector2d* const GetVecs(const double) const;
    virtual const int GetNumVecs() const;
    virtual const Pixmap& GetPixmap(const double) const;
    virtual const int GetNumPix() const;

  private:
    NonRotVectorData(const NonRotVectorData&);
    NonRotVectorData& operator=(const NonRotVectorData&);
    void NoDecorInit(Drawable,
                     const RotColor&,
                     const Vector2d* const);
    void BitmapDecorInit(Drawable,
                         const RotColor&,
                         const Vector2d* const,
                         const unsigned char* const, const int, const int);
    void PixmapDecorInit(Drawable,
                         const RotColor&,
                         const Vector2d* const,
                         const CompositePixmap&);
    void BuildPixmap(Drawable, const int,
                     const int, const int, GC& gc,
                     const Vector2d* const);

    Vector2d* vectors;
    Pixmap pixmap;
    int numVecs;
  };

class RotVectorData: public RotatorDisplayData
 {public:
    RotVectorData(RenderingEngine&, const RotColor&,
                  const Vector2d* const, const int);
    RotVectorData(RenderingEngine&, const RotColor&,
                  const Vector2d* const, const int, const double area);
    RotVectorData(RenderingEngine&, const RotColor&,
                  const Vector2d* const, const int,
                  const unsigned char* const, const int, const int);
    RotVectorData(RenderingEngine&, const RotColor&,
                  const Vector2d* const, const int, const double area,
                  const unsigned char* const, const int, const int);
    RotVectorData(RenderingEngine&, const RotColor&,
                  const Vector2d* const, const int,
                  const CompositePixmap&);
    RotVectorData(RenderingEngine&, const RotColor&,
                  const Vector2d* const, const int, const double area,
                  const CompositePixmap&);
    virtual ~RotVectorData();
    virtual const Vector2d* const GetVecs(const double) const;
    virtual const int GetNumVecs() const;
    virtual const Pixmap& GetPixmap(const double) const;
    virtual const int GetNumPix() const;

  private:
    RotVectorData(const RotVectorData&);
    RotVectorData& operator=(const RotVectorData&);
    void NoDecorInit(Drawable,
                     const RotColor&,
                     const Vector2d* const);
    void BitmapDecorInit(Drawable,
                         const RotColor&,
                         const Vector2d* const,
                         const unsigned char* const, const int, const int);
    void PixmapDecorInit(Drawable,
                         const RotColor&,
                         const Vector2d* const,
                         const CompositePixmap&);
    void RotateVecs(Drawable, const int,
                    const int, const int, GC&,
                    const Vector2d* const);

    Vector2d** rotatedVecs;
    Pixmap* rotatedPix;
    double incAngle;
    int numPix,
        numVecs;
   };

class MaskedRotVectorData: public RotatorDisplayData
 {public:
    MaskedRotVectorData(RenderingEngine&, const RotColor&,
                  const Vector2d* const, const int);
    MaskedRotVectorData(RenderingEngine&, const RotColor&,
                  const Vector2d* const, const int, const double area);
    MaskedRotVectorData(RenderingEngine&, const RotColor&,
                  const Vector2d* const, const int,
                  const unsigned char* const, const int, const int);
    MaskedRotVectorData(RenderingEngine&, const RotColor&,
                  const Vector2d* const, const int, const double area,
                  const unsigned char* const, const int, const int);
    MaskedRotVectorData(RenderingEngine&, const RotColor&,
                  const Vector2d* const, const int,
                  const CompositePixmap&);
    MaskedRotVectorData(RenderingEngine&, const RotColor&,
                  const Vector2d* const, const int, const double area,
                  const CompositePixmap&);
    virtual ~MaskedRotVectorData();
    virtual const Vector2d* const GetVecs(const double) const;
    virtual const int GetNumVecs() const;
    virtual const Pixmap& GetPixmap(const double) const;
    virtual const int GetNumPix() const;
    virtual GC& GetGC(const double) const;
    virtual const Pixmap& GetMaskAtTime(const double) const;

  private:
    MaskedRotVectorData(const MaskedRotVectorData&);
    MaskedRotVectorData& operator=(const MaskedRotVectorData&);
    void NoDecorInit(Drawable,
                     const RotColor&,
                     const Vector2d* const);
    void BitmapDecorInit(Drawable,
                         const RotColor&,
                         const Vector2d* const,
                         const unsigned char* const, const int, const int);
    void PixmapDecorInit(Drawable,
                         const RotColor&,
                         const Vector2d* const,
                         const CompositePixmap&);
    void RotateVecs(Drawable, const int,
                    const int, const int, GC&,
                    const Vector2d* const);

    Vector2d** rotatedVecs;
    Pixmap* rotatedPix,
          * clipMasks;
    GC* maskGC;
    double incAngle;
    int numPix,
        numVecs;
   };

class RotPixmapData: public RotatorDisplayData
 {public:
    RotPixmapData(RenderingEngine&, const RotColor&,
                  const unsigned char* const, const int, const int);
    RotPixmapData(RenderingEngine&, const CompositePixmap&);
    virtual ~RotPixmapData();
    virtual const Pixmap& GetPixmap(const double) const;
    virtual const int GetNumPix() const;

  private:
    RotPixmapData(const RotVectorData&);
    RotPixmapData& operator=(const RotPixmapData&);
    void CreatePix(Drawable, const int, const int, GC&);

    Pixmap* rotatedPix;
    double incAngle;
    int numPix;
   };

class MaskedRotPixmapData: public RotatorDisplayData
 {public:
    MaskedRotPixmapData(RenderingEngine&, const RotColor&,
                         const unsigned char* const, const int, const int);
    MaskedRotPixmapData(RenderingEngine&, const CompositePixmap&);
    virtual ~MaskedRotPixmapData();
    virtual const Pixmap& GetPixmap(const double) const;
    virtual const int GetNumPix() const;
    virtual GC& GetGC(const double) const;
    const Pixmap& GetMaskAtTime(const double) const;

  private:
    MaskedRotPixmapData(const RotVectorData&);
    MaskedRotPixmapData& operator=(const MaskedRotPixmapData&);
    void CreatePix(Drawable, const int, const int, GC&, GC&);

    Pixmap* rotatedPix,
          * clipMasks;
    GC* maskGC;
    double incAngle;
    int numPix;
   };

#else

// Guards-closed flavors (task-27 data-capture shapes; task 47 dropped the
// transitional Display/Drawable/XColor parameters the shim used to type).
// No server resources exist here: the constructors capture the outline,
// decor bits and color components for the engine-side upload/rotation
// consumers; the pixmap/GC/mask accessors do not exist in this configuration.
class RotatorDisplayData
 {public:
    RotatorDisplayData(RenderingEngine&, const Vector2d* const, const int);
    RotatorDisplayData(RenderingEngine&, const Vector2d* const, const int, const double area);
    RotatorDisplayData(RenderingEngine&, const int);
    virtual ~RotatorDisplayData();
    virtual const Vector2d* const GetVecs(const double) const;
    virtual const int GetNumVecs() const;
    virtual const int GetNumPix() const=0;
    const Box& GetBox() const;
    const double GetRadius() const;
    const double GetArea() const;
    const int GetPixWidth() const;
    const int GetPixHeight() const;

  protected:
    Box box;
    double radius,
           area;
    int sideSize;

  private:
    RotatorDisplayData& operator=(const RotatorDisplayData&);
  };

class NonRotVectorData: public RotatorDisplayData
 {public:
    NonRotVectorData(RenderingEngine&, const RotColor&,
                     const Vector2d* const, const int);
    NonRotVectorData(RenderingEngine&, const RotColor&,
                     const Vector2d* const, const int,
                     const unsigned char* const, const int, const int);
    virtual ~NonRotVectorData();
    virtual const Vector2d* const GetVecs(const double) const;
    virtual const int GetNumVecs() const;
    virtual const int GetNumPix() const;

  private:
    NonRotVectorData(const NonRotVectorData&);
    NonRotVectorData& operator=(const NonRotVectorData&);

    Vector2d* vectors;
    int numVecs;

    unsigned short decorRed,
                   decorGreen,
                   decorBlue;
    const unsigned char* decorBits;
    int decorWidth,
        decorHeight;

  public:
    const unsigned char* DecorBits() const
      {return decorBits;
      }
    int DecorWidth() const
      {return decorWidth;
      }
    int DecorHeight() const
      {return decorHeight;
      }
    unsigned short DecorRed() const
      {return decorRed;
      }
    unsigned short DecorGreen() const
      {return decorGreen;
      }
    unsigned short DecorBlue() const
      {return decorBlue;
      }
  };

class RotVectorData: public RotatorDisplayData
 {public:
    RotVectorData(RenderingEngine&, const RotColor&,
                  const Vector2d* const, const int);
    RotVectorData(RenderingEngine&, const RotColor&,
                  const Vector2d* const, const int, const double area);
    RotVectorData(RenderingEngine&, const RotColor&,
                  const Vector2d* const, const int,
                  const unsigned char* const, const int, const int);
    RotVectorData(RenderingEngine&, const RotColor&,
                  const Vector2d* const, const int, const double area,
                  const unsigned char* const, const int, const int);
    virtual ~RotVectorData();
    virtual const Vector2d* const GetVecs(const double) const;
    virtual const int GetNumVecs() const;
    virtual const int GetNumPix() const;

  private:
    RotVectorData(const RotVectorData&);
    RotVectorData& operator=(const RotVectorData&);

    void RotateVecs(const Vector2d* const);

    Vector2d** rotatedVecs;
    double incAngle;
    int numPix,
        numVecs;

    unsigned short decorRed,
                   decorGreen,
                   decorBlue;
    const unsigned char* decorBits;
    int decorWidth,
        decorHeight;

  public:
    const unsigned char* DecorBits() const
      {return decorBits;
      }
    int DecorWidth() const
      {return decorWidth;
      }
    int DecorHeight() const
      {return decorHeight;
      }
    unsigned short DecorRed() const
      {return decorRed;
      }
    unsigned short DecorGreen() const
      {return decorGreen;
      }
    unsigned short DecorBlue() const
      {return decorBlue;
      }
   };

class MaskedRotVectorData: public RotatorDisplayData
 {public:
    MaskedRotVectorData(RenderingEngine&, const RotColor&,
                  const Vector2d* const, const int);
    MaskedRotVectorData(RenderingEngine&, const RotColor&,
                  const Vector2d* const, const int,
                  const unsigned char* const, const int, const int);
    virtual ~MaskedRotVectorData();
    virtual const Vector2d* const GetVecs(const double) const;
    virtual const int GetNumVecs() const;
    virtual const int GetNumPix() const;

  private:
    MaskedRotVectorData(const MaskedRotVectorData&);
    MaskedRotVectorData& operator=(const MaskedRotVectorData&);

    void RotateVecs(const Vector2d* const);

    Vector2d** rotatedVecs;
    double incAngle;
    int numPix,
        numVecs;

    unsigned short decorRed,
                   decorGreen,
                   decorBlue;
    const unsigned char* decorBits;
    int decorWidth,
        decorHeight;

  public:
    const unsigned char* DecorBits() const
      {return decorBits;
      }
    int DecorWidth() const
      {return decorWidth;
      }
    int DecorHeight() const
      {return decorHeight;
      }
    unsigned short DecorRed() const
      {return decorRed;
      }
    unsigned short DecorGreen() const
      {return decorGreen;
      }
    unsigned short DecorBlue() const
      {return decorBlue;
      }
   };

class RotPixmapData: public RotatorDisplayData
 {public:
    RotPixmapData(RenderingEngine&, const RotColor&,
                  const unsigned char* const, const int, const int);
    virtual ~RotPixmapData();
    virtual const int GetNumPix() const;

  private:
    RotPixmapData(const RotVectorData&);
    RotPixmapData& operator=(const RotPixmapData&);

    double incAngle;
    int numPix;

    unsigned short decorRed,
                   decorGreen,
                   decorBlue;
    const unsigned char* decorBits;
    int decorWidth,
        decorHeight;

  public:
    const unsigned char* DecorBits() const
      {return decorBits;
      }
    int DecorWidth() const
      {return decorWidth;
      }
    int DecorHeight() const
      {return decorHeight;
      }
    unsigned short DecorRed() const
      {return decorRed;
      }
    unsigned short DecorGreen() const
      {return decorGreen;
      }
    unsigned short DecorBlue() const
      {return decorBlue;
      }
   };

class MaskedRotPixmapData: public RotatorDisplayData
 {public:
    MaskedRotPixmapData(RenderingEngine&, const RotColor&,
                         const unsigned char* const, const int, const int);
    virtual ~MaskedRotPixmapData();
    virtual const int GetNumPix() const;

  private:
    MaskedRotPixmapData(const RotVectorData&);
    MaskedRotPixmapData& operator=(const MaskedRotPixmapData&);

    double incAngle;
    int numPix;

    unsigned short decorRed,
                   decorGreen,
                   decorBlue;
    const unsigned char* decorBits;
    int decorWidth,
        decorHeight;

  public:
    const unsigned char* DecorBits() const
      {return decorBits;
      }
    int DecorWidth() const
      {return decorWidth;
      }
    int DecorHeight() const
      {return decorHeight;
      }
    unsigned short DecorRed() const
      {return decorRed;
      }
    unsigned short DecorGreen() const
      {return decorGreen;
      }
    unsigned short DecorBlue() const
      {return decorBlue;
      }
   };
#endif

inline const Box& RotatorDisplayData::GetBox() const
 {return box;
 }

inline const double RotatorDisplayData::GetRadius() const
 {return radius;
 }

inline const double RotatorDisplayData::GetArea() const
 {return area;
 }

inline const int RotatorDisplayData::GetPixWidth() const
 {return sideSize;
 }

inline const int RotatorDisplayData::GetPixHeight() const
 {return sideSize;
 }


#endif
