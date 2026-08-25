// test/vk/vksoak.C — task-39 frame-sync soak + forced-OUT_OF_DATE driver.
//
// Drives the real beginFrame/endFrame acquire/submit/present cycle:
//   Phase A: continuous cycling (default 3600 frames; XAST_VK_SOAK_FRAMES
//            overrides) — no hang, no semaphore error, 0 counted ERROR-level
//            validation messages (VUID-/UNASSIGNED only).
//   Phase B: forced OUT_OF_DATE exercise at 2/5 through the run via a
//            scripted glfwSetWindowSize — the framebuffer-size callback
//            fires inside beginFrame's glfwPollEvents, sets swapchainDirty_,
//            and the next beginFrame re-bootstraps (S2/Q15 natural trigger).
//            Asserts the branch engaged and the cycle continued after.
//
// Built by the makefile's obj/VK/vksoak rule (links -lglfw -lvulkan plus
// obj/VK/stbTruetypeImpl.o). Run under Xvfb with DISPLAY set; copy to the
// repo root first or font resolution (/proc/self/exe-relative) cannot find
// vendor/fonts.

#include<stdio.h>
#include<stdlib.h>
#include<time.h>

#include"vkBackend.H"

static double nowSeconds()
 {struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC,&ts);
  return (double)ts.tv_sec+(double)ts.tv_nsec/1e9;
 }

int main()
 {long soakFrames=3600;
  long postResizeFrames=600;
  if (const char* env=getenv("XAST_VK_SOAK_FRAMES"))
    soakFrames=atol(env);
  if (const char* env=getenv("XAST_VK_POST_RESIZE_FRAMES"))
    postResizeFrames=atol(env);

  VKBackend vk;
  // Mirrors main()'s setCanonicalLayout call: PlayingField::playArea is
  // 640x512; ShipGroup::maxIconHeight = max(31,30,44) = 44.
  vk.setCanonicalLayout(640,512,44);

  if (!vk.initWindow("vksoak"))
   {fprintf(stderr,"vksoak: FAIL: initWindow failed\n");
    return 1;
   }

  printf("vksoak: config: frames-in-flight=%d acquire-timeout=%ums "
         "fence-timeout=%ums stall-limit=%u\n",
         VKBackend::kMaxFramesInFlight,VKBackend::kAcquireTimeoutMs,
         VKBackend::kFenceTimeoutMs,VKBackend::kStallShutdownFrames);
  printf("vksoak: plan: %ld frames + resize at %ld + %ld post-resize\n",
         soakFrames,soakFrames*2/5,postResizeFrames);

  int failures=0;
  const unsigned rbBefore=vk.rebootstrapCount_;
  const long totalFrames=soakFrames+postResizeFrames;

  double t0=nowSeconds();
  for (long i=0;i<totalFrames;++i)
   {if (i==soakFrames*2/5)
      glfwSetWindowSize(vk.window,920,640);   // forced OUT_OF_DATE trigger
     vk.beginFrame();
     vk.endFrame();
     if (vk.fatalStallShutdown_)
      {fprintf(stderr,"vksoak: FAIL: fatal stall shutdown at frame %ld\n",
               i);
       ++failures;
       break;
      }
    }
  double dt=nowSeconds()-t0;

  printf("vksoak: SOAK REPORT\n");
  printf("  frames completed (submit+present): %u / %ld\n",
         vk.frameCount_,totalFrames);
  printf("  wall time: %.2fs -> %.1f frames/s\n",dt,
         dt>0.0?(double)vk.frameCount_/dt:0.0);
  printf("  re-bootstraps: %u (was %u before the resize)\n",
         vk.rebootstrapCount_,rbBefore);
  printf("  recovered stalls skipped: %u\n",vk.stallSkipCount_);
  printf("  presentation suspended: %s\n",
         vk.presentationSuspended_?"yes":"no");
  printf("  validation layer enabled: %s\n",
         vk.validationEnabled_?"yes":"no");
  printf("  validation ERROR count (VUID-/UNASSIGNED only): %u\n",
         vk.validationErrorCount_);

  // A handful of boundary frames may legitimately be skipped (the
  // OUT_OF_DATE frame itself); anything more is a real defect.
  if (vk.frameCount_<totalFrames-16)
   {fprintf(stderr,"vksoak: FAIL: only %u/%ld frames completed\n",
            vk.frameCount_,totalFrames);
    ++failures;
   }
  if (vk.rebootstrapCount_<=rbBefore)
   {fprintf(stderr,"vksoak: FAIL: re-bootstrap branch never engaged\n");
    ++failures;
   }
  else
    printf("vksoak: re-bootstrap ENGAGED (%u rebuild(s)); cycle continued "
           "for %u frame(s) after it\n",vk.rebootstrapCount_-rbBefore,
           vk.frameCount_);
  if (vk.validationErrorCount_!=0)
   {fprintf(stderr,"vksoak: FAIL: %u validation ERROR(s)\n",
            vk.validationErrorCount_);
    ++failures;
   }
  int fw=0,fh=0;
  glfwGetFramebufferSize(vk.window,&fw,&fh);
  printf("  final extent: %ux%u (framebuffer %dx%d)\n",
         vk.swapchainExtent_.width,vk.swapchainExtent_.height,fw,fh);
  if ((uint32_t)fw!=vk.swapchainExtent_.width
      ||(uint32_t)fh!=vk.swapchainExtent_.height)
   {fprintf(stderr,"vksoak: FAIL: extent %ux%u != framebuffer %dx%d "
                    "(re-bootstrap missed the new size)\n",
            vk.swapchainExtent_.width,vk.swapchainExtent_.height,fw,fh);
    ++failures;
   }

  vk.shutdown();

  if (failures)
   {fprintf(stderr,"vksoak: FAIL: %d assertion(s)\n",failures);
    return 1;
   }
  printf("vksoak: PASS\n");
  return 0;
 }
