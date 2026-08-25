// test/vk/vksurface.C — task-38 surface+swapchain probe driver.
//
// Drives VKBackend::initWindow() end-to-end (pass 0 instance -> pass 1 font
// metrics -> GLFW_NO_API window -> VkSurfaceKHR -> device WITH
// VK_KHR_swapchain + present family -> swapchain + image views) and asserts
// the task-38 acceptance facts:
//   - swapchain created with >= 2 images
//   - swapchain extent == window framebuffer size
//   - 0 counted ERROR-level validation messages (VUID-/UNASSIGNED only;
//     loader noise never counted — see vkBackend.H header comment)
//   - clean shutdown, exit 0
//
// Built by the makefile's obj/VK/vksurface rule (links -lglfw -lvulkan plus
// obj/VK/stbTruetypeImpl.o — pass 1 consumes stb metrics). Run under Xvfb
// with DISPLAY set; copy to the repo root first or font resolution
// (/proc/self/exe-relative) cannot find vendor/fonts.

#include<stdio.h>
#include<stdlib.h>

#include"vkBackend.H"

static const char* formatName(VkFormat f)
 {switch (f)
   {case VK_FORMAT_B8G8R8A8_UNORM: return "VK_FORMAT_B8G8R8A8_UNORM";
    case VK_FORMAT_B8G8R8A8_SRGB:  return "VK_FORMAT_B8G8R8A8_SRGB";
    default:                       return "(other)";
   }
 }

int main()
 {VKBackend vk;
  // Mirrors main()'s setCanonicalLayout call: PlayingField::playArea is
  // 640x512; ShipGroup::maxIconHeight = max(31,30,44) = 44.
  vk.setCanonicalLayout(640,512,44);

  if (!vk.initWindow("vksurface"))
   {fprintf(stderr,"vksurface: FAIL: initWindow failed\n");
    return 1;
   }

  int fw=0,fh=0;
  glfwGetFramebufferSize(vk.window,&fw,&fh);

  printf("vksurface: SURFACE+SWAPCHAIN REPORT\n");
  printf("  window: %dx%d requested, framebuffer %dx%d\n",
         vk.canonicalWidth_,vk.canonicalHeight_,fw,fh);
  printf("  surface: created\n");
  printf("  present queue family: %u (%s)\n",vk.presentFamily,
         vk.presentFamilyMatchesGraphics_
          ? "graphics family also presents"
          : "separate from graphics");
  printf("  swapchain: %u images, %s, extent %ux%u\n",
         vk.swapchainImageCount_,formatName(vk.swapchainFormat_),
         vk.swapchainExtent_.width,vk.swapchainExtent_.height);
  printf("  validation layer: enabled=%s\n",
         vk.validationEnabled_?"yes":"no");
  printf("  validation ERROR count (VUID-/UNASSIGNED only): %u\n",
         vk.validationErrorCount_);
  if (vk.loaderErrorCount_>0)
    printf("  loader ERROR noise (not counted): %u\n",
           vk.loaderErrorCount_);

  int failures=0;
  if (vk.swapchainImageCount_<2)
   {fprintf(stderr,"vksurface: FAIL: image count %u < 2\n",
            vk.swapchainImageCount_);
    ++failures;
   }
  if ((uint32_t)fw!=vk.swapchainExtent_.width
      ||(uint32_t)fh!=vk.swapchainExtent_.height)
   {fprintf(stderr,"vksurface: FAIL: extent %ux%u != framebuffer %dx%d\n",
            vk.swapchainExtent_.width,vk.swapchainExtent_.height,fw,fh);
    ++failures;
   }
  if (vk.validationErrorCount_!=0)
   {fprintf(stderr,"vksurface: FAIL: %u ERROR-level validation message(s)\n",
            vk.validationErrorCount_);
    ++failures;
   }
  if (vk.presentFamily==UINT32_MAX)
   {fprintf(stderr,"vksurface: FAIL: no present family recorded\n");
    ++failures;
   }

  vk.shutdown();

  if (failures)
   {fprintf(stderr,"vksurface: FAIL: %d assertion(s)\n",failures);
    return 1;
   }
  printf("vksurface: PASS\n");
  return 0;
 }
