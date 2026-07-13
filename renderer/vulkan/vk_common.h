/* PiccuEngine Vulkan loader/common declarations. */
#pragma once

// Volk owns every Vulkan entry point.  No translation unit in the Vulkan
// renderer may include prototype-bearing Vulkan headers before this file.
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#endif

#include <vulkan/vulkan.h>
#include <volk.h>

#include <stdint.h>

namespace piccu
{
namespace render
{
namespace vk
{

constexpr uint32_t kApiVersion = VK_API_VERSION_1_3;
constexpr uint32_t kInvalidQueueFamily = 0xffffffffu;

const char *ResultName(VkResult result) noexcept;

inline bool IsSuccess(VkResult result) noexcept
{
	return result >= 0;
}

inline bool IsDeviceLoss(VkResult result) noexcept
{
	return result == VK_ERROR_DEVICE_LOST;
}

} // namespace vk
} // namespace render
} // namespace piccu

