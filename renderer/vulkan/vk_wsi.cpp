/* Vulkan swapchain selection, acquire/present, and generation ownership. */
#include "vk_wsi.h"
#include "vk_state_tracker.h"

#include <algorithm>
#include <memory>
#include <sstream>
#include <utility>
#include <vector>

namespace piccu
{
namespace render
{
namespace vk
{
namespace
{

template <typename T>
T ClampValue(T value, T minimum, T maximum)
{
	return std::max(minimum, std::min(value, maximum));
}

WsiResultClass ClassifyAcquire(VkResult result)
{
	switch (result)
	{
	case VK_SUCCESS: return WsiResultClass::Success;
	case VK_SUBOPTIMAL_KHR: return WsiResultClass::Suboptimal;
	case VK_NOT_READY:
	case VK_TIMEOUT: return WsiResultClass::NotReady;
	case VK_ERROR_OUT_OF_DATE_KHR: return WsiResultClass::OutOfDate;
	case VK_ERROR_SURFACE_LOST_KHR: return WsiResultClass::SurfaceLost;
	case VK_ERROR_DEVICE_LOST: return WsiResultClass::DeviceLost;
	default: return WsiResultClass::SubmitFailure;
	}
}

WsiResultClass ClassifyPresent(VkResult result)
{
	switch (result)
	{
	case VK_SUCCESS: return WsiResultClass::Success;
	case VK_SUBOPTIMAL_KHR: return WsiResultClass::Suboptimal;
	case VK_ERROR_OUT_OF_DATE_KHR: return WsiResultClass::OutOfDate;
	case VK_ERROR_SURFACE_LOST_KHR: return WsiResultClass::SurfaceLost;
	case VK_ERROR_DEVICE_LOST: return WsiResultClass::DeviceLost;
	default: return WsiResultClass::SubmitFailure;
	}
}

WsiStatus StatusForResultClass(WsiResultClass value)
{
	switch (value)
	{
	case WsiResultClass::Success: return WsiStatus::Success;
	case WsiResultClass::Suboptimal: return WsiStatus::Suboptimal;
	case WsiResultClass::NotReady: return WsiStatus::NotReady;
	case WsiResultClass::OutOfDate: return WsiStatus::OutOfDate;
	case WsiResultClass::SurfaceLost: return WsiStatus::SurfaceLost;
	case WsiResultClass::DeviceLost: return WsiStatus::DeviceLost;
	case WsiResultClass::SubmitFailure: return WsiStatus::SubmitFailure;
	default: return WsiStatus::SubmitFailure;
	}
}

WsiStatus CreationFailureStatus(VkResult result, WsiStatus fallback)
{
	if (result == VK_ERROR_DEVICE_LOST)
		return WsiStatus::DeviceLost;
	if (result == VK_ERROR_SURFACE_LOST_KHR)
		return WsiStatus::SurfaceLost;
	if (result == VK_ERROR_OUT_OF_DATE_KHR)
		return WsiStatus::OutOfDate;
	return fallback;
}

uint32_t ActionsFor(WsiCallPhase phase, WsiResultClass result)
{
	const WsiResultDecisionContract *decision = FindWsiResultDecision(phase, result);
	return decision ? decision->actions : 0;
}

SurfacePixelFormat ContractFormat(VkFormat format)
{
	return format == VK_FORMAT_B8G8R8A8_UNORM ?
		SurfacePixelFormat::B8G8R8A8Unorm : SurfacePixelFormat::R8G8B8A8Unorm;
}

PresentModeContract ContractPresentMode(VkPresentModeKHR mode)
{
	if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR)
		return PresentModeContract::Immediate;
	if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
		return PresentModeContract::Mailbox;
	return PresentModeContract::Fifo;
}

CompositeAlphaContract ContractCompositeAlpha(VkCompositeAlphaFlagBitsKHR value)
{
	switch (value)
	{
	case VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR: return CompositeAlphaContract::Opaque;
	case VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR:
		return CompositeAlphaContract::PreMultiplied;
	case VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR:
		return CompositeAlphaContract::PostMultiplied;
	default: return CompositeAlphaContract::Inherit;
	}
}

VkResult EnumerateSurfaceFormats(VkPhysicalDevice physical, VkSurfaceKHR surface,
	std::vector<VkSurfaceFormatKHR> *formats)
{
	for (uint32_t attempt = 0; attempt < 4; ++attempt)
	{
		uint32_t count = 0;
		VkResult result = vkGetPhysicalDeviceSurfaceFormatsKHR(
			physical, surface, &count, nullptr);
		if (result != VK_SUCCESS)
			return result;
		formats->resize(count);
		result = vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface,
			&count, count ? formats->data() : nullptr);
		if (result == VK_SUCCESS)
		{
			formats->resize(count);
			return result;
		}
		if (result != VK_INCOMPLETE)
			return result;
	}
	return VK_INCOMPLETE;
}

VkResult EnumeratePresentModes(VkPhysicalDevice physical, VkSurfaceKHR surface,
	std::vector<VkPresentModeKHR> *modes)
{
	for (uint32_t attempt = 0; attempt < 4; ++attempt)
	{
		uint32_t count = 0;
		VkResult result = vkGetPhysicalDeviceSurfacePresentModesKHR(
			physical, surface, &count, nullptr);
		if (result != VK_SUCCESS)
			return result;
		modes->resize(count);
		result = vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface,
			&count, count ? modes->data() : nullptr);
		if (result == VK_SUCCESS)
		{
			modes->resize(count);
			return result;
		}
		if (result != VK_INCOMPLETE)
			return result;
	}
	return VK_INCOMPLETE;
}

VkResult EnumerateSwapchainImages(VkDevice device, VkSwapchainKHR swapchain,
	std::vector<VkImage> *images)
{
	for (uint32_t attempt = 0; attempt < 4; ++attempt)
	{
		uint32_t count = 0;
		VkResult result = vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr);
		if (result != VK_SUCCESS)
			return result;
		images->resize(count);
		result = vkGetSwapchainImagesKHR(device, swapchain, &count,
			count ? images->data() : nullptr);
		if (result == VK_SUCCESS)
		{
			images->resize(count);
			return result;
		}
		if (result != VK_INCOMPLETE)
			return result;
	}
	return VK_INCOMPLETE;
}

bool SupportsSwapchainColorAttachment(VkPhysicalDevice physical, VkFormat format)
{
	VkFormatProperties properties = {};
	vkGetPhysicalDeviceFormatProperties(physical, format, &properties);
	return (properties.optimalTilingFeatures &
		VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0;
}

} // namespace

const char *WsiStatusName(WsiStatus status) noexcept
{
	switch (status)
	{
	case WsiStatus::Success: return "success";
	case WsiStatus::Suboptimal: return "suboptimal";
	case WsiStatus::NotReady: return "not-ready";
	case WsiStatus::Paused: return "paused";
	case WsiStatus::OutOfDate: return "out-of-date";
	case WsiStatus::SurfaceLost: return "surface-lost";
	case WsiStatus::DeviceLost: return "device-lost";
	case WsiStatus::SubmitFailure: return "submit-failure";
	case WsiStatus::InvalidArgument: return "invalid-argument";
	case WsiStatus::InvalidState: return "invalid-state";
	case WsiStatus::SurfaceQueryFailed: return "surface-query-failed";
	case WsiStatus::UnsupportedSurface: return "unsupported-surface";
	case WsiStatus::SwapchainCreationFailed: return "swapchain-creation-failed";
	case WsiStatus::ImageEnumerationFailed: return "image-enumeration-failed";
	case WsiStatus::ImageViewCreationFailed: return "image-view-creation-failed";
	case WsiStatus::SemaphoreCreationFailed: return "semaphore-creation-failed";
	case WsiStatus::FenceCreationFailed: return "fence-creation-failed";
	default: return "unknown";
	}
}

struct Wsi::Impl
{
	enum class FrameSemaphoreState : uint32_t
	{
		Idle = 0,
		Acquired,
		Submitted,
	};

	enum class ImageState : uint32_t
	{
		Available = 0,
		Acquired,
		Submitted,
		Presented,
		Abandoned,
		Poisoned,
	};

	struct FrameSemaphore
	{
		VkSemaphore semaphore = VK_NULL_HANDLE;
		FrameSemaphoreState state = FrameSemaphoreState::Idle;
		uint64_t token = 0;
		uint64_t release_timeline = 0;
	};

	struct ImageRecord
	{
		VkImage image = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
		VkSemaphore render_finished = VK_NULL_HANDLE;
		VkFence present_fence = VK_NULL_HANDLE;
		uint32_t present_fence_in_use = 0;
		ImageState state = ImageState::Available;
		uint64_t token = 0;
		uint64_t producing_timeline = 0;
	};

	struct Generation
	{
		VkSwapchainKHR swapchain = VK_NULL_HANDLE;
		VkFormat format = VK_FORMAT_UNDEFINED;
		VkColorSpaceKHR color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
		VkCompositeAlphaFlagBitsKHR composite_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		VkSurfaceTransformFlagBitsKHR pre_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
		VkImageUsageFlags usage = 0;
		VkExtent2D extent = {};
		uint64_t id = 0;
		uint64_t next_token = 1;
		uint64_t next_present_id = 1;
		uint64_t maximum_graphics_timeline = 0;
		uint64_t last_accepted_present_id = 0;
		uint32_t concurrent_sharing = 0;
		uint32_t stopped = 0;
		uint32_t rebuild_requested = 0;
		uint32_t maintenance_fence_enabled = 0;
		uint32_t present_wait_enabled = 0;
		// A failed vkQueuePresentKHR does not establish whether the queue consumed
		// its wait semaphore or began using the swapchain.  Per-present fences and
		// accepted present IDs cannot prove completion of that failed call, so this
		// generation requires a scoped present-queue idle proof before destruction.
		uint32_t present_completion_ambiguous = 0;
		bool state_registered = false;
		WsiCreateInfo requested_create_info = {};
		std::vector<FrameSemaphore> frame_semaphores;
		std::vector<ImageRecord> images;
		CapturedWsiSignature signature = {};
	};

	Platform *platform;
	ResourceStateTracker *state_tracker = nullptr;
	std::unique_ptr<Generation> current;
	std::vector<std::unique_ptr<Generation>> retired;
	uint64_t next_generation = 1;
	uint32_t initialized = 0;
	uint32_t paused = 0;
	VkResult last_result = VK_SUCCESS;
	std::string last_error;

	explicit Impl(Platform &owner) : platform(&owner) {}

	void RegisterGenerationStateFresh(Generation *generation)
	{
		if (!generation || generation->state_registered || !state_tracker)
			return;
		const VkImageSubresourceRange range = {
			VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1
		};
		const ImageUse initial = {
			VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_QUEUE_FAMILY_IGNORED,
			ResourceIntent::Read
		};
		for (const ImageRecord &image : generation->images)
			state_tracker->ImportFreshImage(image.image, range, initial);
		generation->state_registered = true;
	}

	void UnregisterGenerationState(Generation *generation) noexcept
	{
		if (!generation || !generation->state_registered || !state_tracker)
			return;
		for (const ImageRecord &image : generation->images)
			state_tracker->ForgetImage(image.image);
		generation->state_registered = false;
	}

	void SetError(VkResult result, const char *operation, const char *detail)
	{
		last_result = result;
		std::ostringstream stream;
		stream << operation << ": " << detail;
		if (result != VK_SUCCESS)
			stream << " (" << ResultName(result) << ")";
		last_error = stream.str();
		platform->NotifyDeviceResult(result);
	}

	void DestroyGeneration(Generation *generation) noexcept
	{
		UnregisterGenerationState(generation);
		if (!generation || platform->Device() == VK_NULL_HANDLE)
			return;
		const VkDevice device = platform->Device();
		for (size_t i = 0; i < generation->images.size(); ++i)
		{
			if (generation->images[i].present_fence != VK_NULL_HANDLE)
				vkDestroyFence(device, generation->images[i].present_fence, nullptr);
			if (generation->images[i].render_finished != VK_NULL_HANDLE)
				vkDestroySemaphore(device, generation->images[i].render_finished, nullptr);
			if (generation->images[i].view != VK_NULL_HANDLE)
				vkDestroyImageView(device, generation->images[i].view, nullptr);
		}
		for (size_t i = 0; i < generation->frame_semaphores.size(); ++i)
			if (generation->frame_semaphores[i].semaphore != VK_NULL_HANDLE)
				vkDestroySemaphore(device, generation->frame_semaphores[i].semaphore, nullptr);
		if (generation->swapchain != VK_NULL_HANDLE)
			vkDestroySwapchainKHR(device, generation->swapchain, nullptr);
	}

	bool HasOutstandingAcquire(const Generation &generation) const
	{
		for (size_t i = 0; i < generation.frame_semaphores.size(); ++i)
			if (generation.frame_semaphores[i].state == FrameSemaphoreState::Acquired)
				return true;
		return false;
	}

	ImageRecord *FindImageByToken(Generation &generation, uint64_t token)
	{
		for (size_t i = 0; i < generation.images.size(); ++i)
			if (generation.images[i].token == token)
				return &generation.images[i];
		return nullptr;
	}

	FrameSemaphore *FindFrameByToken(Generation &generation, uint64_t token)
	{
		for (size_t i = 0; i < generation.frame_semaphores.size(); ++i)
			if (generation.frame_semaphores[i].token == token)
				return &generation.frame_semaphores[i];
		return nullptr;
	}

	WsiStatus BuildGeneration(const WsiCreateInfo &request,
		const Generation *previous, std::unique_ptr<Generation> *output,
		std::unique_ptr<Generation> *failed_compatibility_candidate,
		bool *previous_retired_by_compatibility_retry)
	{
		output->reset();
		if (failed_compatibility_candidate)
			failed_compatibility_candidate->reset();
		if (previous_retired_by_compatibility_retry)
			*previous_retired_by_compatibility_retry = false;
		VkSurfaceCapabilitiesKHR capabilities = {};
		VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
			platform->PhysicalDevice(), platform->Surface(), &capabilities);
		if (result != VK_SUCCESS)
		{
			SetError(result, "surface-capabilities", "unable to query surface capabilities");
			return StatusForResultClass(ClassifyAcquire(result));
		}
		if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0)
		{
			SetError(VK_ERROR_FORMAT_NOT_SUPPORTED, "surface-capabilities",
				"swapchain images do not support color attachment usage");
			return WsiStatus::UnsupportedSurface;
		}

		std::vector<VkSurfaceFormatKHR> formats;
		result = EnumerateSurfaceFormats(platform->PhysicalDevice(),
			platform->Surface(), &formats);
		if (result != VK_SUCCESS || formats.empty())
		{
			SetError(result, "surface-formats", "unable to enumerate a surface format");
			return WsiStatus::SurfaceQueryFailed;
		}

		VkSurfaceFormatKHR selected_format = {};
		bool format_found = false;
		const VkFormat preferred_formats[] = {
			VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM
		};
		const bool undefined_sentinel = formats.size() == 1 &&
			formats[0].format == VK_FORMAT_UNDEFINED &&
			formats[0].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		for (size_t preference = 0; !format_found && preference < 2; ++preference)
		{
			if (undefined_sentinel && SupportsSwapchainColorAttachment(
				platform->PhysicalDevice(), preferred_formats[preference]))
			{
				selected_format.format = preferred_formats[preference];
				selected_format.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
				format_found = true;
				break;
			}
			for (size_t i = 0; i < formats.size(); ++i)
				if (formats[i].format == preferred_formats[preference] &&
					formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
					SupportsSwapchainColorAttachment(platform->PhysicalDevice(),
						formats[i].format))
				{
					selected_format = formats[i];
					format_found = true;
					break;
				}
		}
		if (!format_found)
		{
			SetError(VK_ERROR_FORMAT_NOT_SUPPORTED, "surface-formats",
				"no authored-space UNORM/SRGB_NONLINEAR pair is available");
			return WsiStatus::UnsupportedSurface;
		}

		std::vector<VkPresentModeKHR> modes;
		result = EnumeratePresentModes(platform->PhysicalDevice(),
			platform->Surface(), &modes);
		if (result != VK_SUCCESS || modes.empty())
		{
			SetError(result, "present-modes", "unable to enumerate a present mode");
			return WsiStatus::SurfaceQueryFailed;
		}
		VkPresentModeKHR selected_mode = VK_PRESENT_MODE_FIFO_KHR;
		const VkPresentModeKHR preferred_modes[] = {
			VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR,
			VK_PRESENT_MODE_FIFO_KHR
		};
		bool mode_found = false;
		for (size_t preference = 0; !mode_found && preference < 3; ++preference)
			for (size_t i = 0; i < modes.size(); ++i)
				if (modes[i] == preferred_modes[preference])
				{
					selected_mode = modes[i];
					mode_found = true;
					break;
				}
		if (!mode_found)
		{
			SetError(VK_ERROR_INITIALIZATION_FAILED, "present-modes",
				"surface exposes no immediate, mailbox, or FIFO mode");
			return WsiStatus::UnsupportedSurface;
		}

		VkExtent2D extent = {};
		if (capabilities.currentExtent.width != UINT32_MAX)
			extent = capabilities.currentExtent;
		else
		{
			extent.width = ClampValue(request.drawable_width,
				capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
			extent.height = ClampValue(request.drawable_height,
				capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
		}
		if (extent.width == 0 || extent.height == 0)
			return WsiStatus::Paused;

		uint32_t image_count = std::max(capabilities.minImageCount,
			kSwapchainOwnershipContract.cpu_frame_contexts +
			kSwapchainOwnershipContract.requested_extra_images);
		if (capabilities.maxImageCount != 0)
			image_count = std::min(image_count, capabilities.maxImageCount);
		image_count = std::max(image_count, capabilities.minImageCount);
		if (image_count < 2)
		{
			SetError(VK_ERROR_INITIALIZATION_FAILED, "surface-capabilities",
				"surface cannot provide the minimum two swapchain images");
			return WsiStatus::UnsupportedSurface;
		}

		VkSurfaceTransformFlagBitsKHR pre_transform = capabilities.currentTransform;
		if ((capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) != 0)
			pre_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
		const VkCompositeAlphaFlagBitsKHR alpha_preferences[] = {
			VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
			VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
			VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
			VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
		};
		VkCompositeAlphaFlagBitsKHR composite_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		bool alpha_found = false;
		for (size_t i = 0; i < 4; ++i)
			if ((capabilities.supportedCompositeAlpha & alpha_preferences[i]) != 0)
			{
				composite_alpha = alpha_preferences[i];
				alpha_found = true;
				break;
			}
		if (!alpha_found)
		{
			SetError(VK_ERROR_INITIALIZATION_FAILED, "surface-capabilities",
				"surface exposes no supported composite-alpha mode");
			return WsiStatus::UnsupportedSurface;
		}

		VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		if (request.request_transfer_source &&
			(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0)
			usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		if (request.request_transfer_destination &&
			(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0)
			usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

		std::unique_ptr<Generation> generation(new Generation);
		generation->format = selected_format.format;
		generation->color_space = selected_format.colorSpace;
		generation->present_mode = selected_mode;
		generation->composite_alpha = composite_alpha;
		generation->pre_transform = pre_transform;
		generation->usage = usage;
		generation->extent = extent;
		generation->concurrent_sharing = platform->UsesSeparatePresentQueue() ? 1u : 0u;
		generation->maintenance_fence_enabled =
			platform->SelectedDevice().swapchain_maintenance1_enabled;
		generation->present_wait_enabled =
			platform->SelectedDevice().present_id_enabled &&
			platform->SelectedDevice().present_wait_enabled;
		generation->requested_create_info = request;

		// Allocate every generation-owned synchronization object whose count is
		// known before touching the native window.  This keeps the conventional
		// oldSwapchain compatibility retry's non-atomic window as small as Vulkan
		// permits; image views and implementation-added images remain post-create.
		VkSemaphoreCreateInfo semaphore = {};
		semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		generation->frame_semaphores.resize(request.frame_context_count);
		for (size_t i = 0; i < generation->frame_semaphores.size(); ++i)
		{
			result = vkCreateSemaphore(platform->Device(), &semaphore, nullptr,
				&generation->frame_semaphores[i].semaphore);
			if (result != VK_SUCCESS)
			{
				SetError(result, "acquire-semaphore", "unable to precreate frame acquire semaphore");
				DestroyGeneration(generation.get());
				return CreationFailureStatus(result, WsiStatus::SemaphoreCreationFailed);
			}
		}
		generation->images.resize(image_count);
		for (size_t i = 0; i < generation->images.size(); ++i)
		{
			result = vkCreateSemaphore(platform->Device(), &semaphore, nullptr,
				&generation->images[i].render_finished);
			if (result != VK_SUCCESS)
			{
				SetError(result, "render-finished-semaphore",
					"unable to precreate per-image render-finished semaphore");
				DestroyGeneration(generation.get());
				return CreationFailureStatus(result, WsiStatus::SemaphoreCreationFailed);
			}
			if (generation->maintenance_fence_enabled)
			{
				VkFenceCreateInfo fence = {};
				fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
				result = vkCreateFence(platform->Device(), &fence, nullptr,
					&generation->images[i].present_fence);
				if (result != VK_SUCCESS)
				{
					SetError(result, "present-fence",
						"unable to precreate per-image presentation fence");
					DestroyGeneration(generation.get());
					return CreationFailureStatus(result, WsiStatus::FenceCreationFailed);
				}
			}
		}

		const uint32_t queue_families[] = {
			platform->GraphicsQueueFamily(), platform->PresentQueueFamily()
		};
		VkSwapchainCreateInfoKHR create = {};
		create.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		create.surface = platform->Surface();
		create.minImageCount = image_count;
		create.imageFormat = selected_format.format;
		create.imageColorSpace = selected_format.colorSpace;
		create.imageExtent = extent;
		create.imageArrayLayers = 1;
		create.imageUsage = usage;
		create.imageSharingMode = generation->concurrent_sharing ?
			VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;
		create.queueFamilyIndexCount = generation->concurrent_sharing ? 2u : 0u;
		create.pQueueFamilyIndices = generation->concurrent_sharing ? queue_families : nullptr;
		create.preTransform = pre_transform;
		create.compositeAlpha = composite_alpha;
		create.presentMode = selected_mode;
		create.clipped = VK_TRUE;
		// Strict transactional attempt: do not retire the published generation.
		create.oldSwapchain = VK_NULL_HANDLE;
		result = vkCreateSwapchainKHR(platform->Device(), &create, nullptr,
			&generation->swapchain);
		bool compatibility_retry_retired_previous = false;
		if (result == VK_ERROR_NATIVE_WINDOW_IN_USE_KHR && previous)
		{
			// Some WSI implementations forbid two live swapchains for one native
			// window.  This is the sole compatibility path that can retire the old
			// generation before the candidate's image views have been created.
			generation->swapchain = VK_NULL_HANDLE;
			create.oldSwapchain = previous->swapchain;
			result = vkCreateSwapchainKHR(platform->Device(), &create, nullptr,
				&generation->swapchain);
			compatibility_retry_retired_previous = result == VK_SUCCESS;
			if (compatibility_retry_retired_previous &&
				previous_retired_by_compatibility_retry)
				*previous_retired_by_compatibility_retry = true;
		}
		if (result != VK_SUCCESS)
		{
			SetError(result, "swapchain-create", "unable to create candidate swapchain");
			DestroyGeneration(generation.get());
			return StatusForResultClass(ClassifyAcquire(result)) == WsiStatus::SubmitFailure ?
				WsiStatus::SwapchainCreationFailed : StatusForResultClass(ClassifyAcquire(result));
		}
		auto abandon_candidate = [&](WsiStatus failure) {
			if (compatibility_retry_retired_previous && failed_compatibility_candidate)
				*failed_compatibility_candidate = std::move(generation);
			else
				DestroyGeneration(generation.get());
			return failure;
		};

		std::vector<VkImage> images;
		result = EnumerateSwapchainImages(platform->Device(), generation->swapchain, &images);
		if (result != VK_SUCCESS || images.size() < 2)
		{
			SetError(result, "swapchain-images", "unable to enumerate candidate images");
			return abandon_candidate(CreationFailureStatus(result,
				WsiStatus::ImageEnumerationFailed));
		}
		if (images.size() < generation->images.size())
		{
			for (size_t i = images.size(); i < generation->images.size(); ++i)
			{
				if (generation->images[i].present_fence != VK_NULL_HANDLE)
					vkDestroyFence(platform->Device(), generation->images[i].present_fence,
						nullptr);
				if (generation->images[i].render_finished != VK_NULL_HANDLE)
					vkDestroySemaphore(platform->Device(),
						generation->images[i].render_finished, nullptr);
			}
		}
		generation->images.resize(images.size());
		for (size_t i = 0; i < images.size(); ++i)
		{
			generation->images[i].image = images[i];
			if (generation->images[i].render_finished == VK_NULL_HANDLE)
			{
				result = vkCreateSemaphore(platform->Device(), &semaphore, nullptr,
					&generation->images[i].render_finished);
				if (result != VK_SUCCESS)
				{
					SetError(result, "render-finished-semaphore",
						"unable to create synchronization for an implementation-added image");
					return abandon_candidate(CreationFailureStatus(result,
						WsiStatus::SemaphoreCreationFailed));
				}
			}
			if (generation->maintenance_fence_enabled &&
				generation->images[i].present_fence == VK_NULL_HANDLE)
			{
				VkFenceCreateInfo fence = {};
				fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
				result = vkCreateFence(platform->Device(), &fence, nullptr,
					&generation->images[i].present_fence);
				if (result != VK_SUCCESS)
				{
					SetError(result, "present-fence",
						"unable to create presentation fence for an implementation-added image");
					return abandon_candidate(CreationFailureStatus(result,
						WsiStatus::FenceCreationFailed));
				}
			}
			VkImageViewCreateInfo view = {};
			view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			view.image = images[i];
			view.viewType = VK_IMAGE_VIEW_TYPE_2D;
			view.format = selected_format.format;
			view.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
			view.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
			view.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
			view.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
			view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			view.subresourceRange.baseMipLevel = 0;
			view.subresourceRange.levelCount = 1;
			view.subresourceRange.baseArrayLayer = 0;
			view.subresourceRange.layerCount = 1;
			result = vkCreateImageView(platform->Device(), &view, nullptr,
				&generation->images[i].view);
			if (result != VK_SUCCESS)
			{
				SetError(result, "swapchain-image-view", "unable to create candidate image view");
				return abandon_candidate(CreationFailureStatus(result,
					WsiStatus::ImageViewCreationFailed));
			}
		}

		generation->id = next_generation++;
		generation->signature.swapchain_generation = generation->id;
		generation->signature.format = ContractFormat(generation->format);
		generation->signature.color_space = SurfaceColorSpace::SrgbNonlinear;
		generation->signature.present_mode = ContractPresentMode(generation->present_mode);
		generation->signature.composite_alpha = ContractCompositeAlpha(composite_alpha);
		generation->signature.surface_transform = static_cast<uint32_t>(pre_transform);
		generation->signature.drawable_width = extent.width;
		generation->signature.drawable_height = extent.height;
		generation->signature.image_count = static_cast<uint32_t>(generation->images.size());
		generation->signature.graphics_queue_family = platform->GraphicsQueueFamily();
		generation->signature.present_queue_family = platform->PresentQueueFamily();
		generation->signature.concurrent_sharing = generation->concurrent_sharing;
		generation->signature.safe_authored_unorm = 1;
		*output = std::move(generation);
		last_result = VK_SUCCESS;
		last_error.clear();
		return WsiStatus::Success;
	}
};

Wsi::Wsi(Platform &platform) : impl_(new Impl(platform)) {}

void Wsi::SetStateTracker(ResourceStateTracker *state_tracker) noexcept
{
	if (impl_)
		impl_->state_tracker = state_tracker;
}

Wsi::~Wsi()
{
	if (impl_)
	{
		Shutdown(impl_->platform->DeviceLost());
		delete impl_;
	}
}

WsiStatus Wsi::Initialize(const WsiCreateInfo &create_info)
{
	if (!impl_ || impl_->initialized)
		return WsiStatus::InvalidState;
	if (!impl_->platform->Ready())
		return impl_->platform->DeviceLost() ? WsiStatus::DeviceLost :
			WsiStatus::InvalidArgument;
	if (create_info.frame_context_count !=
		kSwapchainOwnershipContract.cpu_frame_contexts ||
		create_info.request_transfer_source > 1 ||
		create_info.request_transfer_destination > 1)
		return WsiStatus::InvalidArgument;
	impl_->initialized = 1;
	if (create_info.drawable_width == 0 || create_info.drawable_height == 0)
	{
		impl_->paused = 1;
		return WsiStatus::Paused;
	}
	std::unique_ptr<Impl::Generation> generation;
	const WsiStatus status = impl_->BuildGeneration(create_info, nullptr,
		&generation, nullptr, nullptr);
	if (status == WsiStatus::Paused)
	{
		impl_->paused = 1;
		return status;
	}
	if (status != WsiStatus::Success)
	{
		impl_->initialized = 0;
		return status;
	}
	impl_->current = std::move(generation);
	impl_->RegisterGenerationStateFresh(impl_->current.get());
	impl_->paused = 0;
	return WsiStatus::Success;
}

WsiStatus Wsi::Recreate(const WsiCreateInfo &create_info)
{
	if (!impl_ || !impl_->initialized)
		return WsiStatus::InvalidArgument;
	if (!impl_->platform->Ready())
		return impl_->platform->DeviceLost() ? WsiStatus::DeviceLost :
			WsiStatus::InvalidState;
	if (create_info.frame_context_count != kSwapchainOwnershipContract.cpu_frame_contexts ||
		create_info.request_transfer_source > 1 ||
		create_info.request_transfer_destination > 1)
		return WsiStatus::InvalidArgument;
	if (impl_->current && impl_->HasOutstandingAcquire(*impl_->current))
	{
		impl_->SetError(VK_ERROR_INITIALIZATION_FAILED, "swapchain-recreate",
			"a signaled acquire semaphore must be submitted or drained before recreation");
		return WsiStatus::InvalidState;
	}
	if (create_info.drawable_width == 0 || create_info.drawable_height == 0)
	{
		impl_->paused = 1;
		return WsiStatus::Paused;
	}
	std::unique_ptr<Impl::Generation> candidate;
	std::unique_ptr<Impl::Generation> failed_compatibility_candidate;
	bool previous_retired = false;
	const WsiStatus status = impl_->BuildGeneration(create_info,
		impl_->current.get(), &candidate, &failed_compatibility_candidate,
		&previous_retired);
	if (status != WsiStatus::Success)
	{
		if (!previous_retired || !impl_->current ||
			!failed_compatibility_candidate)
			return status;

		// The strict parallel candidate was rejected by this WSI and the sole
		// compatibility retry retired the old native swapchain before post-create
		// image setup failed.  Rebuild the exact prior surface configuration once,
		// using the failed candidate as oldSwapchain.  Success restores the prior
		// renderer preference atomically from the engine's point of view.
		const VkResult requested_failure_result = impl_->last_result;
		const std::string requested_failure_text = impl_->last_error;
		std::unique_ptr<Impl::Generation> restored;
		std::unique_ptr<Impl::Generation> failed_restoration_candidate;
		bool failed_candidate_retired = false;
		const WsiStatus restore_status = impl_->BuildGeneration(
			impl_->current->requested_create_info,
			failed_compatibility_candidate.get(), &restored,
			&failed_restoration_candidate, &failed_candidate_retired);
		if (restore_status == WsiStatus::Success)
		{
			// Neither candidate ever entered a queue submission or presentation.
			impl_->DestroyGeneration(failed_compatibility_candidate.get());
			failed_compatibility_candidate.reset();
			impl_->UnregisterGenerationState(impl_->current.get());
			impl_->retired.push_back(std::move(impl_->current));
			impl_->current = std::move(restored);
			impl_->RegisterGenerationStateFresh(impl_->current.get());
			impl_->paused = 0;
			impl_->last_result = requested_failure_result;
			impl_->last_error = requested_failure_text +
				"; prior surface configuration restored after conventional oldSwapchain retry";
			return status;
		}

		// Restoration failed: no generation is legally acquirable.  Keep the
		// original, already-retired generation in the retirement queue so its
		// prior graphics/presentation use still receives exact completion proof.
		impl_->DestroyGeneration(failed_restoration_candidate.get());
		failed_restoration_candidate.reset();
		impl_->DestroyGeneration(failed_compatibility_candidate.get());
		failed_compatibility_candidate.reset();
		impl_->current->stopped = 1;
		impl_->UnregisterGenerationState(impl_->current.get());
		impl_->retired.push_back(std::move(impl_->current));
		impl_->paused = 0;
		impl_->last_error = requested_failure_text +
			"; fatal: failed to restore prior surface configuration (" +
			WsiStatusName(restore_status) + ")";
		return restore_status == WsiStatus::DeviceLost ?
			WsiStatus::DeviceLost : WsiStatus::InvalidState;
	}
	if (impl_->current)
	{
		impl_->UnregisterGenerationState(impl_->current.get());
		impl_->retired.push_back(std::move(impl_->current));
	}
	impl_->current = std::move(candidate);
	impl_->RegisterGenerationStateFresh(impl_->current.get());
	impl_->paused = 0;
	return WsiStatus::Success;
}

RetirementResult Wsi::CollectRetired(uint64_t completed_graphics_timeline,
	bool allow_scoped_present_queue_wait)
{
	RetirementResult result;
	if (!impl_ || !impl_->initialized)
	{
		result.status = WsiStatus::InvalidState;
		return result;
	}
	if (impl_->platform->DeviceLost())
	{
		result.status = WsiStatus::DeviceLost;
		result.generations_remaining = static_cast<uint32_t>(impl_->retired.size());
		return result;
	}
	for (size_t i = 0; i < impl_->retired.size();)
	{
		Impl::Generation &generation = *impl_->retired[i];
		const bool graphics_complete = generation.maximum_graphics_timeline <=
			completed_graphics_timeline;
		const bool requires_queue_idle = generation.present_completion_ambiguous != 0;
		bool present_fences_complete = true;
		if (generation.maintenance_fence_enabled && !requires_queue_idle)
		{
			for (size_t image_index = 0; image_index < generation.images.size();
				++image_index)
			{
				if (!generation.images[image_index].present_fence_in_use)
					continue;
				const VkResult fence_result = vkGetFenceStatus(impl_->platform->Device(),
					generation.images[image_index].present_fence);
				if (fence_result == VK_NOT_READY)
					present_fences_complete = false;
				else if (fence_result != VK_SUCCESS)
				{
					impl_->SetError(fence_result, "present-fence",
						"unable to query swapchain presentation completion");
					result.status = fence_result == VK_ERROR_DEVICE_LOST ?
						WsiStatus::DeviceLost : WsiStatus::SubmitFailure;
					result.generations_remaining =
						static_cast<uint32_t>(impl_->retired.size());
					return result;
				}
			}
		}

		bool present_id_complete = generation.last_accepted_present_id == 0;
		if (!requires_queue_idle && !generation.maintenance_fence_enabled &&
			generation.present_wait_enabled &&
			generation.last_accepted_present_id != 0)
		{
			const VkResult wait_result = vkWaitForPresentKHR(impl_->platform->Device(),
				generation.swapchain, generation.last_accepted_present_id, 0);
			if (wait_result == VK_SUCCESS)
				present_id_complete = true;
			else if (wait_result != VK_TIMEOUT)
			{
				impl_->SetError(wait_result, "present-wait",
					"unable to poll completed present ID");
				result.status = wait_result == VK_ERROR_DEVICE_LOST ?
					WsiStatus::DeviceLost : WsiStatus::SubmitFailure;
				result.generations_remaining =
					static_cast<uint32_t>(impl_->retired.size());
				return result;
			}
		}

		bool queue_idle_complete = false;
		if (graphics_complete && allow_scoped_present_queue_wait &&
			(requires_queue_idle || (!generation.maintenance_fence_enabled &&
			 !generation.present_wait_enabled)))
		{
			const VkResult idle_result = vkQueueWaitIdle(impl_->platform->PresentQueue());
			impl_->platform->NotifyDeviceResult(idle_result);
			if (idle_result != VK_SUCCESS)
			{
				impl_->SetError(idle_result, "present-queue-idle",
					"scoped retired-swapchain wait failed");
				result.status = idle_result == VK_ERROR_DEVICE_LOST ?
					WsiStatus::DeviceLost : WsiStatus::SubmitFailure;
				result.generations_remaining =
					static_cast<uint32_t>(impl_->retired.size());
				return result;
			}
			queue_idle_complete = true;
			++result.scoped_present_queue_waits;
		}

		WsiGenerationRetirementInput input = {};
		input.maximum_graphics_timeline_complete = graphics_complete ? 1u : 0u;
		input.present_fence_supported = requires_queue_idle ? 0u :
			generation.maintenance_fence_enabled;
		input.present_fence_complete = present_fences_complete ? 1u : 0u;
		input.present_wait_supported = requires_queue_idle ? 0u :
			generation.present_wait_enabled;
		input.present_id_complete = present_id_complete ? 1u : 0u;
		input.scoped_present_queue_idle_complete = queue_idle_complete ? 1u : 0u;
		const WsiGenerationRetirementDecision decision =
			EvaluateWsiGenerationRetirement(input);
		if (!decision.destroy_generation_objects)
		{
			++i;
			continue;
		}
		impl_->DestroyGeneration(&generation);
		impl_->retired.erase(impl_->retired.begin() + static_cast<ptrdiff_t>(i));
		++result.generations_destroyed;
	}
	result.generations_remaining = static_cast<uint32_t>(impl_->retired.size());
	return result;
}

AcquiredImage Wsi::Acquire(const AcquireRequest &request)
{
	AcquiredImage output;
	if (!impl_ || !impl_->initialized)
	{
		output.status = WsiStatus::InvalidState;
		return output;
	}
	if (!impl_->platform->Ready())
	{
		output.status = impl_->platform->DeviceLost() ?
			WsiStatus::DeviceLost : WsiStatus::InvalidState;
		return output;
	}
	if (impl_->paused || !impl_->current)
	{
		output.status = WsiStatus::Paused;
		return output;
	}
	Impl::Generation &generation = *impl_->current;
	if (generation.stopped)
	{
		output.status = WsiStatus::OutOfDate;
		output.actions = kWsiStopCurrentGeneration | kWsiRebuildAtNextBoundary;
		return output;
	}
	if (request.frame_context_index >= generation.frame_semaphores.size())
	{
		output.status = WsiStatus::InvalidArgument;
		return output;
	}
	Impl::FrameSemaphore &frame =
		generation.frame_semaphores[request.frame_context_index];
	if (frame.state == Impl::FrameSemaphoreState::Submitted &&
		frame.release_timeline <= request.completed_graphics_timeline)
	{
		frame.state = Impl::FrameSemaphoreState::Idle;
		frame.token = 0;
		frame.release_timeline = 0;
	}
	if (frame.state != Impl::FrameSemaphoreState::Idle)
	{
		output.status = WsiStatus::InvalidState;
		return output;
	}

	uint32_t image_index = 0;
	const VkResult acquire_result = vkAcquireNextImageKHR(impl_->platform->Device(),
		generation.swapchain, request.timeout_nanoseconds, frame.semaphore,
		VK_NULL_HANDLE, &image_index);
	impl_->last_result = acquire_result;
	impl_->platform->NotifyDeviceResult(acquire_result);
	const WsiResultClass result_class = ClassifyAcquire(acquire_result);
	const WsiAcquireResultDecision decision = ResolveWsiAcquireResult(result_class);
	output.status = StatusForResultClass(result_class);
	output.actions = decision.actions;
	if (result_class == WsiResultClass::OutOfDate ||
		result_class == WsiResultClass::SurfaceLost ||
		result_class == WsiResultClass::DeviceLost ||
		result_class == WsiResultClass::SubmitFailure)
		generation.stopped = 1;
	if (result_class == WsiResultClass::Suboptimal ||
		result_class == WsiResultClass::OutOfDate)
		generation.rebuild_requested = 1;
	if (!decision.acquire_signal_outstanding)
	{
		if (result_class == WsiResultClass::SubmitFailure)
		{
			output.actions = kWsiStopCurrentGeneration |
				kWsiFatalRendererTermination;
			impl_->SetError(acquire_result, "acquire", "unexpected swapchain acquire failure");
		}
		return output;
	}
	if (image_index >= generation.images.size())
	{
		const uint64_t token = (generation.id << 32) ^ generation.next_token++;
		frame.state = Impl::FrameSemaphoreState::Acquired;
		frame.token = token;
		generation.stopped = 1;
		impl_->SetError(VK_ERROR_INITIALIZATION_FAILED, "acquire",
			"driver returned an image index outside the swapchain image inventory");
		output.status = WsiStatus::InvalidState;
		output.actions = kWsiAcquireSignalOutstanding |
			kWsiStopCurrentGeneration | kWsiFatalRendererTermination;
		output.generation = generation.id;
		output.token = token;
		output.frame_context_index = request.frame_context_index;
		output.acquire_semaphore = frame.semaphore;
		return output;
	}
	Impl::ImageRecord &image = generation.images[image_index];
	const bool image_unusable = image.state == Impl::ImageState::Poisoned ||
		image.state == Impl::ImageState::Acquired ||
		image.state == Impl::ImageState::Submitted ||
		image.state == Impl::ImageState::Abandoned;
	if (image.state == Impl::ImageState::Presented &&
		generation.maintenance_fence_enabled && image.present_fence_in_use)
	{
		// Reacquisition is the safe per-image proof that presentation consumed
		// render_finished.  The maintenance fence must agree before it is reset.
		VkResult fence_result = vkGetFenceStatus(impl_->platform->Device(),
			image.present_fence);
		if (fence_result == VK_SUCCESS)
			fence_result = vkResetFences(impl_->platform->Device(), 1,
				&image.present_fence);
		if (fence_result != VK_SUCCESS)
		{
			generation.stopped = 1;
			impl_->SetError(fence_result, "present-fence-reuse",
				"reacquired image presentation fence is not reusable");
		}
		else
			image.present_fence_in_use = 0;
	}
	const uint64_t token = (generation.id << 32) ^ generation.next_token++;
	frame.state = Impl::FrameSemaphoreState::Acquired;
	frame.token = token;
	image.state = Impl::ImageState::Acquired;
	image.token = token;
	image.producing_timeline = 0;
	output.generation = generation.id;
	output.token = token;
	output.frame_context_index = request.frame_context_index;
	output.image_index = image_index;
	output.image = image.image;
	output.view = image.view;
	output.acquire_semaphore = frame.semaphore;
	output.render_finished_semaphore = image.render_finished;
	output.extent = generation.extent;
	output.suboptimal = result_class == WsiResultClass::Suboptimal ? 1u : 0u;
	if (image_unusable || generation.stopped)
	{
		// The acquire signal is outstanding, but no poisoned per-image semaphore
		// is exposed for rendering.  DrainAbandonedAcquire remains valid for the
		// returned token and is the only legal continuation.
		output.status = WsiStatus::InvalidState;
		output.actions = kWsiAcquireSignalOutstanding |
			kWsiStopCurrentGeneration | kWsiFatalRendererTermination;
		output.render_finished_semaphore = VK_NULL_HANDLE;
	}
	return output;
}

WsiSubmissionSync Wsi::SubmissionSync(uint64_t token) const
{
	WsiSubmissionSync output;
	if (!impl_ || !impl_->current || token == 0)
		return output;
	Impl::Generation &generation = *impl_->current;
	Impl::FrameSemaphore *frame = impl_->FindFrameByToken(generation, token);
	Impl::ImageRecord *image = impl_->FindImageByToken(generation, token);
	if (!frame || !image || frame->state != Impl::FrameSemaphoreState::Acquired ||
		image->state != Impl::ImageState::Acquired)
		return output;
	output.status = WsiStatus::Success;
	output.acquire_wait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	output.acquire_wait.semaphore = frame->semaphore;
	output.acquire_wait.value = 0;
	output.acquire_wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	output.acquire_wait.deviceIndex = 0;
	output.render_finished_signal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	output.render_finished_signal.semaphore = image->render_finished;
	output.render_finished_signal.value = 0;
	output.render_finished_signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	output.render_finished_signal.deviceIndex = 0;
	return output;
}

WsiStatus Wsi::ConfirmSubmission(uint64_t token, uint64_t timeline_value,
	VkResult submit_result)
{
	if (!impl_ || !impl_->current || token == 0)
		return WsiStatus::InvalidState;
	Impl::Generation &generation = *impl_->current;
	Impl::FrameSemaphore *frame = impl_->FindFrameByToken(generation, token);
	Impl::ImageRecord *image = impl_->FindImageByToken(generation, token);
	if (!frame || !image || frame->state != Impl::FrameSemaphoreState::Acquired ||
		image->state != Impl::ImageState::Acquired)
		return WsiStatus::InvalidState;
	impl_->last_result = submit_result;
	impl_->platform->NotifyDeviceResult(submit_result);
	if (submit_result != VK_SUCCESS)
	{
		generation.stopped = 1;
		impl_->SetError(submit_result, "queue-submit",
			"submission did not consume the outstanding acquire semaphore");
		return submit_result == VK_ERROR_DEVICE_LOST ?
			WsiStatus::DeviceLost : WsiStatus::SubmitFailure;
	}
	if (timeline_value == 0)
	{
		generation.stopped = 1;
		frame->state = Impl::FrameSemaphoreState::Submitted;
		frame->release_timeline = UINT64_MAX;
		image->state = Impl::ImageState::Poisoned;
		impl_->SetError(VK_ERROR_INITIALIZATION_FAILED, "queue-submit",
			"successful submission has no timeline ownership value");
		return WsiStatus::InvalidArgument;
	}
	frame->state = Impl::FrameSemaphoreState::Submitted;
	frame->release_timeline = timeline_value;
	image->state = Impl::ImageState::Submitted;
	image->producing_timeline = timeline_value;
	generation.maximum_graphics_timeline = std::max(
		generation.maximum_graphics_timeline, timeline_value);
	return WsiStatus::Success;
}

WsiStatus Wsi::DrainAbandonedAcquire(uint64_t token, uint64_t timeline_value)
{
	if (!impl_ || !impl_->current || token == 0 || timeline_value == 0)
		return WsiStatus::InvalidArgument;
	Impl::Generation &generation = *impl_->current;
	Impl::FrameSemaphore *frame = impl_->FindFrameByToken(generation, token);
	Impl::ImageRecord *image = impl_->FindImageByToken(generation, token);
	if (!frame || frame->state != Impl::FrameSemaphoreState::Acquired ||
		(image && image->state != Impl::ImageState::Acquired))
		return WsiStatus::InvalidState;
	if (timeline_value <= generation.maximum_graphics_timeline)
		return WsiStatus::InvalidArgument;
	VkSemaphoreSubmitInfo wait = {};
	wait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	wait.semaphore = frame->semaphore;
	wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	VkSemaphoreSubmitInfo signal = {};
	signal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	signal.semaphore = impl_->platform->TimelineSemaphore();
	signal.value = timeline_value;
	signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	VkSubmitInfo2 submit = {};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	submit.waitSemaphoreInfoCount = 1;
	submit.pWaitSemaphoreInfos = &wait;
	submit.signalSemaphoreInfoCount = 1;
	submit.pSignalSemaphoreInfos = &signal;
	const VkResult result = vkQueueSubmit2(impl_->platform->GraphicsQueue(),
		1, &submit, VK_NULL_HANDLE);
	impl_->last_result = result;
	impl_->platform->NotifyDeviceResult(result);
	if (result != VK_SUCCESS)
	{
		generation.stopped = 1;
		impl_->SetError(result, "acquire-drain-submit",
			"failed to consume an abandoned acquire semaphore");
		return result == VK_ERROR_DEVICE_LOST ? WsiStatus::DeviceLost : WsiStatus::SubmitFailure;
	}
	frame->state = Impl::FrameSemaphoreState::Submitted;
	frame->release_timeline = timeline_value;
	if (image)
	{
		image->state = Impl::ImageState::Abandoned;
		image->producing_timeline = timeline_value;
	}
	generation.maximum_graphics_timeline = std::max(
		generation.maximum_graphics_timeline, timeline_value);
	generation.stopped = 1;
	generation.rebuild_requested = 1;
	return WsiStatus::Success;
}

PresentResult Wsi::Present(uint64_t token)
{
	PresentResult output;
	if (!impl_ || !impl_->current || token == 0)
		return output;
	if (impl_->platform->DeviceLost())
	{
		output.status = WsiStatus::DeviceLost;
		output.actions = kWsiStopCurrentGeneration |
			kWsiFatalRendererTermination | kWsiPoisonRenderFinishedIfUnconsumed;
		return output;
	}
	Impl::Generation &generation = *impl_->current;
	Impl::ImageRecord *image = impl_->FindImageByToken(generation, token);
	if (!image || image->state != Impl::ImageState::Submitted)
		return output;
	if (generation.maintenance_fence_enabled &&
		(image->present_fence == VK_NULL_HANDLE || image->present_fence_in_use))
	{
		generation.stopped = 1;
		image->state = Impl::ImageState::Poisoned;
		impl_->SetError(VK_ERROR_INITIALIZATION_FAILED, "present-fence",
			"per-image presentation fence was not safely reacquired and reset");
		output.status = WsiStatus::InvalidState;
		output.actions = kWsiStopCurrentGeneration |
			kWsiFatalRendererTermination | kWsiPoisonRenderFinishedIfUnconsumed;
		output.render_finished_poisoned = 1;
		return output;
	}
	const uint32_t image_index = static_cast<uint32_t>(image - generation.images.data());
	output.generation = generation.id;
	output.image_index = image_index;

	const uint64_t present_id = generation.next_present_id++;
	VkPresentIdKHR present_id_info = {};
	present_id_info.sType = VK_STRUCTURE_TYPE_PRESENT_ID_KHR;
	present_id_info.swapchainCount = 1;
	present_id_info.pPresentIds = &present_id;
	VkSwapchainPresentFenceInfoEXT fence_info = {};
	fence_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT;
	fence_info.swapchainCount = 1;
	fence_info.pFences = &image->present_fence;
	if (generation.present_wait_enabled && generation.maintenance_fence_enabled)
	{
		present_id_info.pNext = &fence_info;
	}

	VkPresentInfoKHR present = {};
	present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	if (generation.present_wait_enabled)
		present.pNext = &present_id_info;
	else if (generation.maintenance_fence_enabled)
		present.pNext = &fence_info;
	present.waitSemaphoreCount = 1;
	present.pWaitSemaphores = &image->render_finished;
	present.swapchainCount = 1;
	present.pSwapchains = &generation.swapchain;
	present.pImageIndices = &image_index;
	const VkResult present_result = vkQueuePresentKHR(impl_->platform->PresentQueue(), &present);
	impl_->last_result = present_result;
	impl_->platform->NotifyDeviceResult(present_result);
	const WsiResultClass result_class = ClassifyPresent(present_result);
	uint32_t actions = ActionsFor(WsiCallPhase::Present, result_class);
	if (result_class == WsiResultClass::SubmitFailure)
		actions = kWsiStopCurrentGeneration | kWsiFatalRendererTermination |
			kWsiPoisonRenderFinishedIfUnconsumed;
	const bool accepted = result_class == WsiResultClass::Success ||
		result_class == WsiResultClass::Suboptimal;
	output.status = StatusForResultClass(result_class);
	output.actions = actions;
	output.present_id = generation.present_wait_enabled ? present_id : 0;
	output.accepted_presentation = accepted ? 1u : 0u;
	output.histories_may_advance =
		(actions & kWsiAdvancePresentedHistories) != 0 ? 1u : 0u;
	if (accepted)
	{
		image->state = Impl::ImageState::Presented;
		image->present_fence_in_use = generation.maintenance_fence_enabled;
		if (generation.present_wait_enabled)
			generation.last_accepted_present_id = present_id;
		if (result_class == WsiResultClass::Suboptimal)
			generation.rebuild_requested = 1;
	}
	else
	{
		// A failed present is not proof that its binary wait was consumed.  It is
		// never waited or reused again; generation retirement destroys it only
		// after the producing timeline and, for this ambiguous call, a scoped
		// present-queue idle proof are complete.
		image->state = Impl::ImageState::Poisoned;
		image->present_fence_in_use = 0;
		generation.stopped = 1;
		if (present_result != VK_ERROR_DEVICE_LOST)
			generation.present_completion_ambiguous = 1;
		output.render_finished_poisoned = 1;
		if (result_class == WsiResultClass::OutOfDate)
			generation.rebuild_requested = 1;
		if (result_class == WsiResultClass::SubmitFailure)
			impl_->SetError(present_result, "queue-present", "unexpected presentation failure");
	}
	return output;
}

void Wsi::Shutdown(bool device_lost) noexcept
{
	if (!impl_ || !impl_->initialized)
		return;
	const bool usable_device = impl_->platform->Device() != VK_NULL_HANDLE;
	if (usable_device && !device_lost && !impl_->platform->DeviceLost())
	{
		const VkResult result = vkDeviceWaitIdle(impl_->platform->Device());
		impl_->platform->NotifyDeviceResult(result);
	}
	if (usable_device)
	{
		impl_->DestroyGeneration(impl_->current.get());
		for (size_t i = 0; i < impl_->retired.size(); ++i)
			impl_->DestroyGeneration(impl_->retired[i].get());
	}
	else
	{
		impl_->UnregisterGenerationState(impl_->current.get());
		for (size_t i = 0; i < impl_->retired.size(); ++i)
			impl_->UnregisterGenerationState(impl_->retired[i].get());
	}
	impl_->current.reset();
	impl_->retired.clear();
	impl_->initialized = 0;
	impl_->paused = 0;
}

bool Wsi::Ready() const noexcept
{
	return impl_ && impl_->initialized && impl_->platform->Ready() &&
		(impl_->paused || impl_->current.get() != nullptr);
}

bool Wsi::Paused() const noexcept { return impl_ && impl_->paused != 0; }

bool Wsi::GenerationStopped() const noexcept
{
	return !impl_ || !impl_->current || impl_->current->stopped != 0;
}

uint64_t Wsi::Generation() const noexcept
{
	return impl_ && impl_->current ? impl_->current->id : 0;
}

const CapturedWsiSignature &Wsi::Signature() const noexcept
{
	static const CapturedWsiSignature empty = {};
	return impl_ && impl_->current ? impl_->current->signature : empty;
}

VkSwapchainKHR Wsi::Swapchain() const noexcept
{
	return impl_ && impl_->current ? impl_->current->swapchain : VK_NULL_HANDLE;
}

VkFormat Wsi::Format() const noexcept
{
	return impl_ && impl_->current ? impl_->current->format : VK_FORMAT_UNDEFINED;
}

VkColorSpaceKHR Wsi::ColorSpace() const noexcept
{
	return impl_ && impl_->current ? impl_->current->color_space :
		VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
}

VkPresentModeKHR Wsi::PresentMode() const noexcept
{
	return impl_ && impl_->current ? impl_->current->present_mode :
		VK_PRESENT_MODE_FIFO_KHR;
}

VkImageUsageFlags Wsi::ImageUsage() const noexcept
{
	return impl_ && impl_->current ? impl_->current->usage : 0;
}

VkExtent2D Wsi::Extent() const noexcept
{
	return impl_ && impl_->current ? impl_->current->extent : VkExtent2D{};
}

uint32_t Wsi::ImageCount() const noexcept
{
	return impl_ && impl_->current ?
		static_cast<uint32_t>(impl_->current->images.size()) : 0;
}

VkImage Wsi::Image(uint32_t index) const noexcept
{
	return impl_ && impl_->current && index < impl_->current->images.size() ?
		impl_->current->images[index].image : VK_NULL_HANDLE;
}

VkImageView Wsi::ImageView(uint32_t index) const noexcept
{
	return impl_ && impl_->current && index < impl_->current->images.size() ?
		impl_->current->images[index].view : VK_NULL_HANDLE;
}

VkResult Wsi::LastVulkanResult() const noexcept
{
	return impl_ ? impl_->last_result : VK_ERROR_INITIALIZATION_FAILED;
}

const std::string &Wsi::LastError() const noexcept
{
	static const std::string empty;
	return impl_ ? impl_->last_error : empty;
}

} // namespace vk
} // namespace render
} // namespace piccu
