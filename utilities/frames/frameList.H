#ifndef _FRAMELIST_H
#define _FRAMELIST_H

#ifdef X11_BACKEND
#include<X11/Xlib.h>
#endif
#include"../pixmaps/composite/compositePixmap.H"


class FrameList
 {public:
   FrameList(const CompositePixmap* const,
             const int numFrames,
             const int firstFrame=0);
#ifdef X11_BACKEND
   // Task 47: the guarded draw branches consume the raw frame handle; the
   // GPU legs use ExplosionGraphic::FrameId instead.
   const Pixmap& GetFrame() const;
#endif
   int PixWidth() const;
   int PixHeight() const;
   int GetNumFrames() const;
   int GetFrameNumber() const;
   int GetFirstFrameNumber() const;
   FrameList& UpdateFrameNumber();
   FrameList& Reset();
   FrameList& SetFrame(const int frameNum=0);
 private:
   const CompositePixmap* const frameData;
   const int numFrames,
             firstFrame;
   int currentFrame;
 };


inline FrameList::FrameList(const CompositePixmap* const fData,
                            const int nFrames,
			                      const int frame1): frameData(fData),
                                               numFrames(nFrames),
                                               firstFrame(frame1),
                                               currentFrame(frame1)
 {
 }

#ifdef X11_BACKEND
inline const Pixmap& FrameList::GetFrame() const
 {return frameData[currentFrame].GetPixmap();
 }
#endif

inline int FrameList::PixWidth() const
 {return frameData->GetPixWidth();
 }

inline int FrameList::PixHeight() const
 {return frameData->GetPixHeight();
 }

inline int FrameList::GetNumFrames() const
 {return numFrames;
 }

inline int FrameList::GetFrameNumber() const
 {return currentFrame;
 }

inline int FrameList::GetFirstFrameNumber() const
 {return firstFrame;
 }

inline FrameList& FrameList::UpdateFrameNumber()
 {if (++currentFrame>=numFrames)
    currentFrame=0;
   return *this;
 }

inline FrameList& FrameList::Reset()
 {currentFrame=firstFrame;
  return *this;
 }

inline FrameList& FrameList::SetFrame(const int frameNum)
 {currentFrame=frameNum;
  return *this;
 }

#endif
