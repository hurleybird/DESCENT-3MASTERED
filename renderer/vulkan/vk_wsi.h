/* Vulkan swapchain selection, acquire/present, and generation ownership. */
#pragma once

#include "vk_platform.h"
#include "../core/render_wsi_contract.h"

#include <string>

namespace piccu
{
namespace render
{
namespace vk
{

class ResourceStateTracker;

enum class WsiStatus : uint32_t
{
	Success = 0,
	Suboptimal,
	NotReady,
	Paused,
	OutOfDate,
	SurfaceLost,
	DeviceLost,
	SubmitFailure,
	InvalidArgument,
	InvalidState,
	SurfaceQueryFailed,
	UnsupportedSurface,
	SwapchainCreationFailed,
	ImageEnumerationFailed,
	ImageViewCreationFailed,
	SemaphoreCreationFailed,
	FenceCreationFailed,
};

const char *WsiStatusName(WsiStatus status) noexcept;

// drawable_width/height are the actual drawable pixel dimensions (for SDL,
// SDL_GetWindowSizeInPixels), never logical window or game dimensions.
struct WsiCreateInfo
{
	uint32_t drawable_width = 0;
	uint32_t drawable_height = 0;
	uint32_t frame_context_count = kSwapchainOwnershipContract.cpu_frame_contexts;
	uint32_t request_transfer_source = 0;
	uint32_t request_transfer_destination = 0;
};

struct AcquireRequest
{
	uint32_t frame_context_index = 0;
	uint64_t completed_graphics_timeline = 0;
	// UINT64_MAX is the ordinary pacing path.  Zero is reserved for tests and
	// failure injection; VK_NOT_READY/VK_TIMEOUT does not claim the frame slot.
	uint64_t timeout_nanoseconds = UINT64_MAX;
};

struct AcquiredImage
{
	WsiStatus status = WsiStatus::InvalidState;
	uint32_t actions = 0;
	uint64_t generation = 0;
	uint64_t token = 0;
	uint32_t frame_context_index = 0;
	uint32_t image_index = 0;
	VkImage image = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
	VkSemaphore acquire_semaphore = VK_NULL_HANDLE;
	VkSemaphore render_finished_semaphore = VK_NULL_HANDLE;
	VkExtent2D extent = {};
	uint32_t suboptimal = 0;

	bool HasImage() const noexcept
	{
		return status == WsiStatus::Success || status == WsiStatus::Suboptimal;
	}
};

// The compiler appends these records to its vkQueueSubmit2 submission.  The
// acquire wait begins only at the final swapchain color-attachment pass;
// offscreen work before it remains unconstrained.
struct WsiSubmissionSync
{
	WsiStatus status = WsiStatus::InvalidState;
	VkSemaphoreSubmitInfo acquire_wait = {};
	VkSemaphoreSubmitInfo render_finished_signal = {};
};

struct PresentResult
{
	WsiStatus status = WsiStatus::InvalidState;
	uint32_t actions = 0;
	uint64_t generation = 0;
	uint32_t image_index = 0;
	uint64_t present_id = 0;
	uint32_t accepted_presentation = 0;
	uint32_t histories_may_advance = 0;
	uint32_t render_finished_poisoned = 0;
};

struct RetirementResult
{
	WsiStatus status = WsiStatus::Success;
	uint32_t generations_destroyed = 0;
	uint32_t generations_remaining = 0;
	uint32_t scoped_present_queue_waits = 0;
};

class Wsi final
{
public:
	explicit Wsi(Platform &platform);
	~Wsi();

	Wsi(const Wsi &) = delete;
	Wsi &operator=(const Wsi &) = delete;
	Wsi(Wsi &&) = delete;
	Wsi &operator=(Wsi &&) = delete;
	void SetStateTracker(ResourceStateTracker *state_tracker) noexcept;

	// Initialize and Recreate are transactional.  Recreate first builds a
	// parallel candidate without retiring the current VkSwapchainKHR.  A WSI
	// returning VK_ERROR_NATIVE_WINDOW_IN_USE_KHR receives one conventional
	// oldSwapchain retry after all countable sync objects are precreated.  If
	// post-create setup then fails, the prior surface configuration is restored
	// immediately; an unsuccessful restoration leaves no acquirable generation.
	// Publication is one pointer swap only after every image view and generation
	// semaphore exists.
	WsiStatus Initialize(const WsiCreateInfo &create_info);
	WsiStatus Recreate(const WsiCreateInfo &create_info);

	// Poll is nonblocking.  The scoped queue-idle fallback is used only for an
	// already-retired generation on implementations without present fences or
	// present-wait, and for any generation made ambiguous by a failed present.
	// It never calls vkDeviceWaitIdle.
	RetirementResult CollectRetired(uint64_t completed_graphics_timeline,
		bool allow_scoped_present_queue_wait);

	AcquiredImage Acquire(const AcquireRequest &request);
	WsiSubmissionSync SubmissionSync(uint64_t token) const;
	// Call exactly once with the result of the submit containing SubmissionSync.
	// timeline_value is published only for VK_SUCCESS.
	WsiStatus ConfirmSubmission(uint64_t token, uint64_t timeline_value,
		VkResult submit_result);
	// Consumes a successful acquire when later compilation is abandoned.  The
	// caller reserves a strictly increasing timeline value for this drain submit.
	// The image is deliberately not presented and the generation must rebuild.
	WsiStatus DrainAbandonedAcquire(uint64_t token, uint64_t timeline_value);
	PresentResult Present(uint64_t token);

	// Final renderer teardown may wait for the device.  Device-loss teardown
	// performs no wait and never asks the caller to wait for an unsignaled value.
	void Shutdown(bool device_lost = false) noexcept;

	bool Ready() const noexcept;
	bool Paused() const noexcept;
	bool GenerationStopped() const noexcept;
	uint64_t Generation() const noexcept;
	const CapturedWsiSignature &Signature() const noexcept;
	VkSwapchainKHR Swapchain() const noexcept;
	VkFormat Format() const noexcept;
	VkColorSpaceKHR ColorSpace() const noexcept;
	VkPresentModeKHR PresentMode() const noexcept;
	VkImageUsageFlags ImageUsage() const noexcept;
	VkExtent2D Extent() const noexcept;
	uint32_t ImageCount() const noexcept;
	VkImage Image(uint32_t index) const noexcept;
	VkImageView ImageView(uint32_t index) const noexcept;
	VkResult LastVulkanResult() const noexcept;
	const std::string &LastError() const noexcept;

private:
	struct Impl;
	Impl *impl_;
};

} // namespace vk
} // namespace render
} // namespace piccu
