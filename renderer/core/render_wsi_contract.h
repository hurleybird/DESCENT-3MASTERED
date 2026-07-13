/* Frozen platform/swapchain selection and ownership policy. */
#pragma once

#include "render_contract.h"

namespace piccu
{
namespace render
{

enum class SurfacePixelFormat : uint32_t
{
	B8G8R8A8Unorm = 0,
	R8G8B8A8Unorm,
	UndefinedSentinel,
	Count,
};

enum class SurfaceColorSpace : uint32_t
{
	SrgbNonlinear = 0,
	Other,
	Count,
};

struct SurfaceFormatPreference
{
	SurfacePixelFormat format;
	SurfaceColorSpace color_space;
	uint32_t preference_order;
	uint32_t safe_authored_unorm;
};

constexpr SurfaceFormatPreference kSurfaceFormatPreferences[] = {
	{ SurfacePixelFormat::B8G8R8A8Unorm, SurfaceColorSpace::SrgbNonlinear, 0, 1 },
	{ SurfacePixelFormat::R8G8B8A8Unorm, SurfaceColorSpace::SrgbNonlinear, 1, 1 },
};

enum class PresentModeContract : uint32_t
{
	Immediate = 0,
	Mailbox,
	Fifo,
	Count,
};

constexpr PresentModeContract kPresentModePreference[] = {
	PresentModeContract::Immediate,
	PresentModeContract::Mailbox,
	PresentModeContract::Fifo,
};

enum class CompositeAlphaContract : uint32_t
{
	Opaque = 0,
	PreMultiplied,
	PostMultiplied,
	Inherit,
	Count,
};

constexpr CompositeAlphaContract kCompositeAlphaPreference[] = {
	CompositeAlphaContract::Opaque,
	CompositeAlphaContract::PreMultiplied,
	CompositeAlphaContract::PostMultiplied,
	CompositeAlphaContract::Inherit,
};

enum class SwapchainRetirementProof : uint32_t
{
	PresentFenceMaintenance1 = 0,
	CompletedPresentId,
	ScopedPresentQueueIdle,
};

enum class WsiResultClass : uint32_t
{
	Success = 0,
	Suboptimal,
	NotReady,
	OutOfDate,
	SurfaceLost,
	DeviceLost,
	SubmitFailure,
};

enum class WsiCallPhase : uint32_t
{
	Acquire = 0,
	QueueSubmit,
	Present,
	Count,
};

enum WsiActionBits : uint32_t
{
	kWsiContinueFrame = 1u << 0,
	kWsiAcquireSignalOutstanding = 1u << 1,
	kWsiConsumeAcquireSignal = 1u << 2,
	kWsiSubmittedTimelineIsValid = 1u << 3,
	kWsiRenderFinishedSignaled = 1u << 4,
	kWsiAcceptedPresentation = 1u << 5,
	kWsiAdvancePresentedHistories = 1u << 6,
	kWsiRebuildAtNextBoundary = 1u << 7,
	kWsiStopCurrentGeneration = 1u << 8,
	kWsiRecreateSurfaceOnce = 1u << 9,
	kWsiFatalRendererTermination = 1u << 10,
	kWsiPoisonRenderFinishedIfUnconsumed = 1u << 11,
	kWsiNoTimelineValueWasSubmitted = 1u << 12,
	kWsiRetryAcquireWithoutFrameContext = 1u << 13,
};

struct WsiResultDecisionContract
{
	WsiCallPhase phase;
	WsiResultClass result;
	uint32_t actions;
};

// Result-class decisions before the Vulkan implementation adds handle work.
// A successful acquire signal remains outstanding until a successful submit
// consumes it; abandoning later work must explicitly drain that signal.
constexpr WsiResultDecisionContract kWsiResultDecisionContract[] = {
	{ WsiCallPhase::Acquire, WsiResultClass::Success,
		kWsiContinueFrame | kWsiAcquireSignalOutstanding },
	{ WsiCallPhase::Acquire, WsiResultClass::Suboptimal,
		kWsiContinueFrame | kWsiAcquireSignalOutstanding | kWsiRebuildAtNextBoundary },
	{ WsiCallPhase::Acquire, WsiResultClass::NotReady,
		kWsiRetryAcquireWithoutFrameContext },
	{ WsiCallPhase::Acquire, WsiResultClass::OutOfDate,
		kWsiStopCurrentGeneration | kWsiRebuildAtNextBoundary },
	{ WsiCallPhase::Acquire, WsiResultClass::SurfaceLost,
		kWsiStopCurrentGeneration | kWsiRecreateSurfaceOnce },
	{ WsiCallPhase::Acquire, WsiResultClass::DeviceLost,
		kWsiStopCurrentGeneration | kWsiFatalRendererTermination },
	{ WsiCallPhase::QueueSubmit, WsiResultClass::Success,
		kWsiConsumeAcquireSignal | kWsiSubmittedTimelineIsValid |
		kWsiRenderFinishedSignaled },
	{ WsiCallPhase::QueueSubmit, WsiResultClass::SubmitFailure,
		kWsiAcquireSignalOutstanding | kWsiNoTimelineValueWasSubmitted |
		kWsiStopCurrentGeneration | kWsiFatalRendererTermination },
	{ WsiCallPhase::QueueSubmit, WsiResultClass::DeviceLost,
		kWsiAcquireSignalOutstanding | kWsiNoTimelineValueWasSubmitted |
		kWsiStopCurrentGeneration | kWsiFatalRendererTermination },
	{ WsiCallPhase::Present, WsiResultClass::Success,
		kWsiAcceptedPresentation | kWsiAdvancePresentedHistories },
	{ WsiCallPhase::Present, WsiResultClass::Suboptimal,
		kWsiAcceptedPresentation | kWsiAdvancePresentedHistories |
		kWsiRebuildAtNextBoundary },
	{ WsiCallPhase::Present, WsiResultClass::OutOfDate,
		kWsiStopCurrentGeneration | kWsiRebuildAtNextBoundary |
		kWsiPoisonRenderFinishedIfUnconsumed },
	{ WsiCallPhase::Present, WsiResultClass::SurfaceLost,
		kWsiStopCurrentGeneration | kWsiRecreateSurfaceOnce |
		kWsiPoisonRenderFinishedIfUnconsumed },
	{ WsiCallPhase::Present, WsiResultClass::DeviceLost,
		kWsiStopCurrentGeneration | kWsiFatalRendererTermination |
		kWsiPoisonRenderFinishedIfUnconsumed },
};

inline const WsiResultDecisionContract *FindWsiResultDecision(
	WsiCallPhase phase, WsiResultClass result)
{
	for (size_t i = 0; i < sizeof(kWsiResultDecisionContract) /
		sizeof(kWsiResultDecisionContract[0]); ++i)
	{
		if (kWsiResultDecisionContract[i].phase == phase &&
			kWsiResultDecisionContract[i].result == result)
			return &kWsiResultDecisionContract[i];
	}
	return nullptr;
}

struct WsiAcquireResultDecision
{
	uint32_t valid;
	uint32_t actions;
	uint32_t retry_without_frame_context;
	uint32_t frame_context_claimed;
	uint32_t acquire_signal_outstanding;
};

inline WsiAcquireResultDecision ResolveWsiAcquireResult(WsiResultClass result)
{
	const WsiResultDecisionContract *contract =
		FindWsiResultDecision(WsiCallPhase::Acquire, result);
	if (!contract)
		return { 0, 0, 0, 0, 0 };
	const uint32_t actions = contract->actions;
	return { 1, actions,
		(actions & kWsiRetryAcquireWithoutFrameContext) != 0 ? 1u : 0u,
		(actions & kWsiContinueFrame) != 0 ? 1u : 0u,
		(actions & kWsiAcquireSignalOutstanding) != 0 ? 1u : 0u };
}

struct WsiAcquireSemaphoreInput
{
	uint32_t signal_outstanding;
	uint32_t abandon_acquired_image;
	uint32_t successful_submit_consumed_signal;
	uint32_t drain_submit_consumed_signal;
	uint32_t consumer_submission_timeline_complete;
};

struct WsiAcquireSemaphoreDecision
{
	uint32_t valid;
	uint32_t signal_still_outstanding;
	uint32_t drain_submit_required;
	uint32_t reusable;
};

// An acquired-but-abandoned image must consume the signaled semaphore with a
// drain submission.  The frame-context semaphore is reusable only after the
// consuming submission's exact timeline value completes.
inline WsiAcquireSemaphoreDecision EvaluateAcquireSemaphore(
	const WsiAcquireSemaphoreInput &input)
{
	const bool bits_valid = input.signal_outstanding <= 1 &&
		input.abandon_acquired_image <= 1 &&
		input.successful_submit_consumed_signal <= 1 &&
		input.drain_submit_consumed_signal <= 1 &&
		input.consumer_submission_timeline_complete <= 1;
	const bool contradictory = input.successful_submit_consumed_signal &&
		input.drain_submit_consumed_signal;
	const bool consume_without_signal = !input.signal_outstanding &&
		(input.successful_submit_consumed_signal || input.drain_submit_consumed_signal);
	const bool invalid_drain = input.drain_submit_consumed_signal &&
		!input.abandon_acquired_image;
	if (!bits_valid || contradictory || consume_without_signal || invalid_drain)
		return { 0, 0, 0, 0 };
	const bool consumed = input.successful_submit_consumed_signal ||
		input.drain_submit_consumed_signal;
	const bool outstanding = input.signal_outstanding && !consumed;
	const bool drain_required = outstanding && input.abandon_acquired_image;
	const bool reusable = !input.signal_outstanding ||
		(consumed && input.consumer_submission_timeline_complete);
	return { 1, outstanding ? 1u : 0u, drain_required ? 1u : 0u,
		reusable ? 1u : 0u };
}

struct WsiPresentSemaphoreDecision
{
	uint32_t valid;
	uint32_t consumed;
	uint32_t poisoned;
	uint32_t reusable;
	uint32_t destroyable;
	uint32_t replacement_required;
};

struct WsiPresentSemaphoreInput
{
	WsiResultClass result;
	uint32_t present_wait_was_consumed;
	uint32_t image_reacquired_from_same_generation;
	uint32_t producing_graphics_timeline_complete;
	uint32_t generation_presentation_complete;
};

inline WsiPresentSemaphoreDecision ResolvePresentSemaphoreDecision(
	const WsiPresentSemaphoreInput &input)
{
	const bool bits_valid = input.present_wait_was_consumed <= 1 &&
		input.image_reacquired_from_same_generation <= 1 &&
		input.producing_graphics_timeline_complete <= 1 &&
		input.generation_presentation_complete <= 1;
	const bool accepted = input.result == WsiResultClass::Success ||
		input.result == WsiResultClass::Suboptimal;
	const bool result_valid = input.result != WsiResultClass::NotReady &&
		input.result != WsiResultClass::SubmitFailure;
	const bool valid = bits_valid && result_valid &&
		(!accepted || input.present_wait_was_consumed) &&
		(!input.image_reacquired_from_same_generation || accepted);
	if (!valid)
		return { 0, 0, 0, 0, 0, 0 };
	const bool consumed = input.present_wait_was_consumed != 0;
	const bool poisoned = !accepted && !consumed;
	const bool reusable = accepted && consumed &&
		input.image_reacquired_from_same_generation;
	const bool destroyable = input.producing_graphics_timeline_complete &&
		input.generation_presentation_complete && (consumed || poisoned);
	return { 1, consumed ? 1u : 0u, poisoned ? 1u : 0u,
		reusable ? 1u : 0u, destroyable ? 1u : 0u,
		poisoned ? 1u : 0u };
}

struct WsiGenerationRetirementInput
{
	uint32_t maximum_graphics_timeline_complete;
	uint32_t present_fence_supported;
	uint32_t present_fence_complete;
	uint32_t present_wait_supported;
	uint32_t present_id_complete;
	uint32_t scoped_present_queue_idle_complete;
};

struct WsiGenerationRetirementDecision
{
	SwapchainRetirementProof proof;
	uint32_t presentation_complete;
	uint32_t destroy_generation_objects;
	uint32_t destroy_generation_semaphores;
};

inline WsiGenerationRetirementDecision EvaluateWsiGenerationRetirement(
	const WsiGenerationRetirementInput &input)
{
	SwapchainRetirementProof proof = SwapchainRetirementProof::ScopedPresentQueueIdle;
	uint32_t presentation_complete = input.scoped_present_queue_idle_complete != 0;
	if (input.present_fence_supported)
	{
		proof = SwapchainRetirementProof::PresentFenceMaintenance1;
		presentation_complete = input.present_fence_complete != 0;
	}
	else if (input.present_wait_supported)
	{
		proof = SwapchainRetirementProof::CompletedPresentId;
		presentation_complete = input.present_id_complete != 0;
	}
	const uint32_t destroy = input.maximum_graphics_timeline_complete &&
		presentation_complete;
	return { proof, presentation_complete, destroy, destroy };
}

struct SwapchainOwnershipContract
{
	uint32_t cpu_frame_contexts;
	uint32_t requested_extra_images;
	uint32_t acquire_semaphore_per_frame_context;
	uint32_t render_finished_semaphore_per_swapchain_image;
	uint32_t infinite_ordinary_acquire_timeout;
	uint32_t clear_full_swapchain_black;
	uint32_t draw_only_present_rectangle;
	uint32_t zero_extent_pauses_present;
	uint32_t device_wait_idle_in_normal_recreation;
};

constexpr SwapchainOwnershipContract kSwapchainOwnershipContract =
	{ 2, 1, 1, 1, 1, 1, 1, 1, 0 };

struct CapturedWsiSignature
{
	uint64_t swapchain_generation;
	SurfacePixelFormat format;
	SurfaceColorSpace color_space;
	PresentModeContract present_mode;
	CompositeAlphaContract composite_alpha;
	uint32_t surface_transform;
	uint32_t drawable_width;
	uint32_t drawable_height;
	uint32_t image_count;
	uint32_t graphics_queue_family;
	uint32_t present_queue_family;
	uint32_t concurrent_sharing;
	uint32_t safe_authored_unorm;
};

} // namespace render
} // namespace piccu
