// test/vk/vkprobe.C — task-37 Vulkan pass-0 probe driver (U15/m15 evidence).
//
// Drives VKBackend::probeStandalone() (surfaceless: NO glfwInit, zero surface
// extensions), prints the recorded-fact report, exits 0/1. Built by the
// makefile's obj/VK/vkprobe rule (links -lglfw -lvulkan — this TU references
// glfw symbols transitively through vkBackend.H even though standalone mode
// never calls glfwInit).
//
// Gate: exit 0 requires instance+device init success AND zero counted
// ERROR-level validation messages (VUID-/UNASSIGNED only — loader noise is
// never counted; see vkBackend.H header comment).

#include<stdio.h>
#include<stdlib.h>

#include"vkBackend.H"

static const char* deviceTypeName(uint32_t t)
 {switch (t)
   {case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return "discrete";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return "virtual";
    case VK_PHYSICAL_DEVICE_TYPE_CPU:            return "cpu";
    default:                                     return "other";
   }
 }

int main()
 {VKBackend vk;
  bool ok=vk.probeStandalone();
  if (!ok)
   {fprintf(stderr,"vkprobe: FAIL: pass-0 init failed\n");
    return 1;
   }

  printf("vkprobe: PASS-0 REPORT\n");
  printf("  instance: created (header pin 1.4.%d)\n",VK_HEADER_VERSION);
  printf("  device: %s (%s, apiVersion %u.%u.%u)\n",
         vk.deviceName_,deviceTypeName(vk.deviceType_),
         VK_VERSION_MAJOR(vk.deviceApiVersion_),
         VK_VERSION_MINOR(vk.deviceApiVersion_),
         VK_VERSION_PATCH(vk.deviceApiVersion_));
  printf("  graphics queue family: %u\n",vk.graphicsFamily);
  printf("  debug utils available: %s, messenger: %s\n",
         vk.debugUtilsAvailable_?"yes":"no",
         vk.debugMessenger!=VK_NULL_HANDLE?"created":"absent");
  printf("  validation layer: instance-present=%s device-present=%s "
         "enabled=%s\n",
         vk.instanceLayerPresent_?"yes":"no",
         vk.deviceLayerPresent_?"yes":"no",
         vk.validationEnabled_?"yes":"no");
  printf("  swapchain ext available: %s (NOT enabled until task 38)\n",
         vk.swapchainAvailable_?"yes":"no");
  printf("  validation ERROR count (VUID-/UNASSIGNED only): %u\n",
         vk.validationErrorCount_);
  if (vk.loaderErrorCount_>0)
    printf("  loader ERROR noise (not counted): %u\n",
           vk.loaderErrorCount_);

  vk.shutdown();

  if (vk.validationErrorCount_!=0)
   {fprintf(stderr,"vkprobe: FAIL: %u ERROR-level validation message(s)\n",
            vk.validationErrorCount_);
    return 1;
   }
  printf("vkprobe: PASS\n");
  return 0;
 }
