#ifndef _ROTATOR_H
#define _ROTATOR_H

#include<math.h>
#ifdef X11_BACKEND
#include<X11/Xlib.h>
#endif
#include"rotatorDisplayData.H"

class AngVel
 {friend class Rotator;
  public:
    operator double() const;
    AngVel& operator=(const AngVel&);
    AngVel& operator=(const double);

  private:
    AngVel();
    AngVel(const AngVel&);
    double angularVelocity,
          maxAngularVelocity;
 };

class Rotator
 {public:
    Rotator(const double mav);
    Rotator(const RotatorDisplayData* const,
              const double angularVelocity,
              const double maxLinearVelocity);
    Rotator& SetRotator(const RotatorDisplayData* const rddAddr,
                    const double av);
    const Vector2d*
      GetVecsAtTime(const double time) const;
    int
      GetNumVecs() const;
#ifdef X11_BACKEND
    // Task 47: the pixmap/mask accessors are consumed only by the guarded
    // draw branches; GetMaskGCAtTime died unconsumed with the transition
    // bridges. The engine-side texture ids are captured at DeferDraw time.
    const Pixmap&
      GetPixAtTime(const double time) const;
    const Pixmap&
      GetMaskAtTime(const double time) const;
#endif
    const Box&
      GetPixBox() const;
    int
      PixWidth() const;
    int
      PixHeight() const;
    double
      Radius() const;
    double
      Area() const;
    double
      Angle() const;
    double
      AngularVelocity() const;
    double
      MaxAngularVelocity() const;
    double&
      Angle();
    AngVel&
      AngularVelocity();
    Rotator&
      UpdateAngle(const double time);
    Rotator&
      UpdateMaxLinearVelocity(const double maxLinearVelocity);
  private:
    const RotatorDisplayData* rotData;
    AngVel angularVelocity;
    double angle;
 };

inline AngVel::AngVel()
 {
 }

inline AngVel::operator double() const
 {return angularVelocity;
 }

inline AngVel& AngVel::operator=(const AngVel& av)
 {angularVelocity=av.angularVelocity;
  if (fabs(angularVelocity)>maxAngularVelocity)
    angularVelocity=angularVelocity<0 ? -maxAngularVelocity
                                      : maxAngularVelocity;
  return *this;
 }

inline AngVel& AngVel::operator=(const double fav)
 {angularVelocity=fav;
  if (fabs(angularVelocity)>maxAngularVelocity)
    angularVelocity=angularVelocity<0 ? -maxAngularVelocity
                                      : maxAngularVelocity;
  return *this;
 }

inline Rotator::Rotator(const double mav): rotData(NULL),
                                           angle(0)
 {angularVelocity.maxAngularVelocity=mav;
  angularVelocity.angularVelocity=0;
 }

inline Rotator::Rotator(const RotatorDisplayData* const rddAddr,
                        const double av,
			const double mlv): rotData(rddAddr),
                                          angle(0)
 {angularVelocity.maxAngularVelocity=mlv/(4*rotData->GetRadius());
  angularVelocity=av;
 }

inline Rotator& Rotator::SetRotator(const RotatorDisplayData* const rddAddr,
                                    const double av)
 {rotData=rddAddr;
  angle=0;
  angularVelocity=av;
  return *this;
 }

inline const Vector2d*
Rotator::GetVecsAtTime(const double time) const
 {return rotData->GetVecs(angle+angularVelocity.angularVelocity*time);
 }

inline int
Rotator::GetNumVecs() const
 {return rotData->GetNumVecs();
 }

#ifdef X11_BACKEND
inline const Pixmap&
Rotator::GetPixAtTime(const double time) const
 {return rotData->GetPixmap(angle+angularVelocity.angularVelocity*time);
 }

inline const Pixmap&
Rotator::GetMaskAtTime(const double time) const
 {return rotData->GetMaskAtTime(angle+angularVelocity.angularVelocity*time);
 }
#endif

inline const Box&
Rotator::GetPixBox() const
{return rotData->GetBox();
 }

inline int
Rotator::PixWidth() const
 {return rotData->GetPixWidth();
 }

inline int
Rotator::PixHeight() const
 {return rotData->GetPixHeight();
 }

inline double
Rotator::Radius() const
 {return rotData->GetRadius();
 }

inline double
Rotator::Area() const
 {return rotData->GetArea();
 }

inline double
Rotator::Angle() const
 {return angle;
 }

inline double
Rotator::AngularVelocity() const
 {return angularVelocity.angularVelocity;
 }

inline double
Rotator::MaxAngularVelocity() const
 {return angularVelocity.maxAngularVelocity;
 }

inline double&
Rotator::Angle()
 {return angle;
 }

inline AngVel&
Rotator::AngularVelocity()
 {return angularVelocity;
 }

inline Rotator&
Rotator::UpdateAngle(const double time)
 {angle=fmod(angle+angularVelocity.angularVelocity*time,6.28318530717958);
  return *this;
 }

inline Rotator&
Rotator::UpdateMaxLinearVelocity(const double maxLinearVelocity)
 {angularVelocity.maxAngularVelocity=maxLinearVelocity/(4*rotData->GetRadius());
  angularVelocity=angularVelocity.angularVelocity;
  return *this;
 }

#endif
