/* Explicit renderer-owned synchronization and resource lifetime contract. */
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace piccu
{
namespace render
{

enum PipelineStageBits : uint64_t
{
	kPipelineStageHost = uint64_t(1) << 0,
	kPipelineStageTransfer = uint64_t(1) << 1,
	kPipelineStageCompute = uint64_t(1) << 2,
	kPipelineStageIndirect = uint64_t(1) << 3,
	kPipelineStageVertexInput = uint64_t(1) << 4,
	kPipelineStageVertexShader = uint64_t(1) << 5,
	kPipelineStageFragmentShader = uint64_t(1) << 6,
	kPipelineStageColorAttachment = uint64_t(1) << 7,
	kPipelineStageDepthAttachment = uint64_t(1) << 8,
	kPipelineStagePresent = uint64_t(1) << 9,
};

constexpr uint64_t kTrackedPipelineStageMask = kPipelineStageHost |
	kPipelineStageTransfer | kPipelineStageCompute | kPipelineStageIndirect |
	kPipelineStageVertexInput | kPipelineStageVertexShader |
	kPipelineStageFragmentShader | kPipelineStageColorAttachment |
	kPipelineStageDepthAttachment | kPipelineStagePresent;

enum ResourceAccessBits : uint64_t
{
	kAccessHostWrite = uint64_t(1) << 0,
	kAccessHostRead = uint64_t(1) << 1,
	kAccessTransferRead = uint64_t(1) << 2,
	kAccessTransferWrite = uint64_t(1) << 3,
	kAccessUniformRead = uint64_t(1) << 4,
	kAccessSampledImageRead = uint64_t(1) << 5,
	kAccessStorageRead = uint64_t(1) << 6,
	kAccessStorageWrite = uint64_t(1) << 7,
	kAccessIndirectRead = uint64_t(1) << 8,
	kAccessVertexAttributeRead = uint64_t(1) << 9,
	kAccessIndexRead = uint64_t(1) << 10,
	kAccessColorAttachmentRead = uint64_t(1) << 11,
	kAccessColorAttachmentWrite = uint64_t(1) << 12,
	kAccessDepthAttachmentRead = uint64_t(1) << 13,
	kAccessDepthAttachmentWrite = uint64_t(1) << 14,
};

constexpr uint64_t kTrackedResourceAccessMask = kAccessHostWrite |
	kAccessHostRead | kAccessTransferRead | kAccessTransferWrite |
	kAccessUniformRead | kAccessSampledImageRead | kAccessStorageRead |
	kAccessStorageWrite | kAccessIndirectRead | kAccessVertexAttributeRead |
	kAccessIndexRead | kAccessColorAttachmentRead |
	kAccessColorAttachmentWrite | kAccessDepthAttachmentRead |
	kAccessDepthAttachmentWrite;

constexpr uint64_t kAccessShaderRead = kAccessUniformRead |
	kAccessSampledImageRead | kAccessStorageRead;
constexpr uint64_t kAccessShaderWrite = kAccessStorageWrite;
constexpr uint64_t kAccessVertexIndexRead =
	kAccessVertexAttributeRead | kAccessIndexRead;
constexpr uint64_t kAccessAttachmentRead =
	kAccessColorAttachmentRead | kAccessDepthAttachmentRead;
constexpr uint64_t kAccessAttachmentWrite =
	kAccessColorAttachmentWrite | kAccessDepthAttachmentWrite;
constexpr uint64_t kTrackedWriteAccessMask = kAccessHostWrite |
	kAccessTransferWrite | kAccessStorageWrite | kAccessColorAttachmentWrite |
	kAccessDepthAttachmentWrite;

enum class ResourceLayout : uint32_t
{
	Undefined = 0,
	TransferSource,
	TransferDestination,
	ShaderReadOnly,
	ColorAttachment,
	DepthAttachment,
	Present,
	General,
	NotApplicable,
	Count,
};

using ResourceLayoutMask = uint32_t;
constexpr ResourceLayoutMask ResourceLayoutBit(ResourceLayout layout)
{
	return ResourceLayoutMask(1) << static_cast<uint32_t>(layout);
}

constexpr ResourceLayoutMask kTrackedResourceLayoutMask =
	(ResourceLayoutMask(1) << static_cast<uint32_t>(ResourceLayout::Count)) - 1;

struct ResourceStateContract
{
	uint64_t pipeline_stages;
	uint64_t access_mask;
	ResourceLayout layout;
	uint32_t queue_family;
	uint32_t write_intent;
	uint32_t reserved0;
	uint64_t last_use_timeline;
};

enum class DependencyEdge : uint32_t
{
	UploadToVertexIndexIndirectStorageUniformSampled = 0,
	AttachmentWriteToLoad,
	AttachmentWriteToResolveCopySample,
	MsaaToResolve,
	DepthWriteToSoftCaptureGtao,
	SceneWriteToWorldCapturePost,
	PostWriteToNextPostSample,
	TemporalHistoryAcrossFrames,
	TextureMeshUploadAndRemesh,
	ReadbackCopyToHost,
	SwapchainAcquireToColorRender,
	SwapchainColorRenderToPresent,
	ContinuationAfterSynchronousReadback,
	Count,
};

enum DependencyTrackedSubresourceBits : uint32_t
{
	kTrackBufferByteRange = 1u << 0,
	kTrackImageAspectMipLayer = 1u << 1,
	kTrackLogicalResourceVersion = 1u << 2,
	kTrackSwapchainImage = 1u << 3,
	kTrackHostMappedAtomRange = 1u << 4,
	kTrackAllResourceRanges = (1u << 5) - 1,
};

enum class DependencyTimelineRule : uint32_t
{
	SameSubmissionOrdering = 0,
	SubmissionTimelineRetirement,
	CrossFrameTimelineVersion,
	HostWaitForExactSubmittedTimeline,
	WsiBinaryAcquireRenderPresent,
	ContinuationWaitAndInheritPrefix,
	Count,
};

// Swapchain images may use concurrent sharing when graphics and presentation
// families differ.  With exclusive sharing, these are the exact ownership
// directions carried by the two image barriers surrounding color rendering.
enum class WsiQueueOwnershipDirection : uint32_t
{
	None = 0,
	PresentToGraphics,
	GraphicsToPresent,
	Count,
};

enum class WsiBinarySemaphoreOperation : uint32_t
{
	None = 0,
	WaitAcquireBeforeColorRender,
	SignalRenderFinishedBeforePresent,
	Count,
};

struct DependencyEdgeContract
{
	DependencyEdge edge;
	const char *diagnostic_name;
	// These masks are the exact legal state domains for this coverage edge.
	// The barrier generator narrows them to the actual tracked producer and
	// consumer state; the table is not a replacement for the range tracker.
	uint64_t producer_stages;
	uint64_t producer_access;
	ResourceLayoutMask producer_layouts;
	uint64_t consumer_stages;
	uint64_t consumer_access;
	ResourceLayoutMask consumer_layouts;
	uint32_t tracked_subresources;
	DependencyTimelineRule timeline_rule;
	uint32_t producer_may_write;
	uint32_t consumer_may_write;
	uint32_t queue_family_tracked;
	uint32_t barrier_from_actual_state;
	uint32_t transition_layout_when_different;
	uint32_t exact_timeline_completion_required;
	uint32_t wsi_binary_chain;
	uint32_t continuation_inherits_exact_state;
	WsiQueueOwnershipDirection wsi_queue_ownership;
	WsiBinarySemaphoreOperation wsi_binary_operation;
};

#define L(layout) ResourceLayoutBit(layout)

constexpr DependencyEdgeContract kDependencyEdgeContract[] = {
	{ DependencyEdge::UploadToVertexIndexIndirectStorageUniformSampled,
	  "upload -> GPU consumers",
	  kPipelineStageHost | kPipelineStageTransfer,
	  kAccessHostWrite | kAccessTransferWrite,
	  L(ResourceLayout::NotApplicable) | L(ResourceLayout::TransferDestination),
	  kPipelineStageIndirect | kPipelineStageVertexInput |
	  kPipelineStageVertexShader | kPipelineStageFragmentShader |
	  kPipelineStageCompute,
	  kAccessIndirectRead | kAccessVertexIndexRead | kAccessShaderRead,
	  L(ResourceLayout::NotApplicable) | L(ResourceLayout::ShaderReadOnly) |
	  L(ResourceLayout::General),
	  kTrackBufferByteRange | kTrackImageAspectMipLayer |
	  kTrackLogicalResourceVersion,
	  DependencyTimelineRule::SubmissionTimelineRetirement,
	  1, 0, 1, 1, 1, 0, 0, 0 },
	{ DependencyEdge::AttachmentWriteToLoad,
	  "attachment write -> attachment LOAD",
	  kPipelineStageColorAttachment | kPipelineStageDepthAttachment,
	  kAccessAttachmentWrite,
	  L(ResourceLayout::ColorAttachment) | L(ResourceLayout::DepthAttachment),
	  kPipelineStageColorAttachment | kPipelineStageDepthAttachment,
	  kAccessAttachmentRead | kAccessAttachmentWrite,
	  L(ResourceLayout::ColorAttachment) | L(ResourceLayout::DepthAttachment),
	  kTrackImageAspectMipLayer | kTrackLogicalResourceVersion,
	  DependencyTimelineRule::SameSubmissionOrdering,
	  1, 1, 1, 1, 1, 0, 0, 0 },
	{ DependencyEdge::AttachmentWriteToResolveCopySample,
	  "attachment write -> resolve/copy/sample",
	  kPipelineStageColorAttachment | kPipelineStageDepthAttachment,
	  kAccessAttachmentWrite,
	  L(ResourceLayout::ColorAttachment) | L(ResourceLayout::DepthAttachment),
	  kPipelineStageTransfer | kPipelineStageFragmentShader |
	  kPipelineStageCompute | kPipelineStageColorAttachment |
	  kPipelineStageDepthAttachment,
	  kAccessTransferRead | kAccessSampledImageRead | kAccessAttachmentRead,
	  L(ResourceLayout::TransferSource) | L(ResourceLayout::ShaderReadOnly) |
	  L(ResourceLayout::ColorAttachment) | L(ResourceLayout::DepthAttachment),
	  kTrackImageAspectMipLayer | kTrackLogicalResourceVersion,
	  DependencyTimelineRule::SameSubmissionOrdering,
	  1, 0, 1, 1, 1, 0, 0, 0 },
	{ DependencyEdge::MsaaToResolve,
	  "MSAA source -> fixed/custom resolve",
	  kPipelineStageColorAttachment | kPipelineStageDepthAttachment,
	  kAccessAttachmentWrite,
	  L(ResourceLayout::ColorAttachment) | L(ResourceLayout::DepthAttachment),
	  kPipelineStageColorAttachment | kPipelineStageDepthAttachment |
	  kPipelineStageFragmentShader | kPipelineStageCompute |
	  kPipelineStageTransfer,
	  kAccessAttachmentRead | kAccessSampledImageRead | kAccessTransferRead,
	  L(ResourceLayout::ColorAttachment) | L(ResourceLayout::DepthAttachment) |
	  L(ResourceLayout::ShaderReadOnly) | L(ResourceLayout::TransferSource),
	  kTrackImageAspectMipLayer | kTrackLogicalResourceVersion,
	  DependencyTimelineRule::SameSubmissionOrdering,
	  1, 0, 1, 1, 1, 0, 0, 0 },
	{ DependencyEdge::DepthWriteToSoftCaptureGtao,
	  "depth write -> soft/capture/GTAO",
	  kPipelineStageDepthAttachment, kAccessDepthAttachmentWrite,
	  L(ResourceLayout::DepthAttachment),
	  kPipelineStageTransfer | kPipelineStageFragmentShader |
	  kPipelineStageCompute,
	  kAccessTransferRead | kAccessSampledImageRead,
	  L(ResourceLayout::TransferSource) | L(ResourceLayout::ShaderReadOnly),
	  kTrackImageAspectMipLayer | kTrackLogicalResourceVersion,
	  DependencyTimelineRule::SameSubmissionOrdering,
	  1, 0, 1, 1, 1, 0, 0, 0 },
	{ DependencyEdge::SceneWriteToWorldCapturePost,
	  "scene write -> world capture/post",
	  kPipelineStageColorAttachment | kPipelineStageDepthAttachment,
	  kAccessAttachmentWrite,
	  L(ResourceLayout::ColorAttachment) | L(ResourceLayout::DepthAttachment),
	  kPipelineStageTransfer | kPipelineStageFragmentShader |
	  kPipelineStageCompute,
	  kAccessTransferRead | kAccessSampledImageRead,
	  L(ResourceLayout::TransferSource) | L(ResourceLayout::ShaderReadOnly),
	  kTrackImageAspectMipLayer | kTrackLogicalResourceVersion,
	  DependencyTimelineRule::SameSubmissionOrdering,
	  1, 0, 1, 1, 1, 0, 0, 0 },
	{ DependencyEdge::PostWriteToNextPostSample,
	  "post write -> next post sample",
	  kPipelineStageColorAttachment | kPipelineStageCompute,
	  kAccessColorAttachmentWrite | kAccessStorageWrite,
	  L(ResourceLayout::ColorAttachment) | L(ResourceLayout::General),
	  kPipelineStageFragmentShader | kPipelineStageCompute,
	  kAccessSampledImageRead | kAccessStorageRead,
	  L(ResourceLayout::ShaderReadOnly) | L(ResourceLayout::General),
	  kTrackImageAspectMipLayer | kTrackLogicalResourceVersion,
	  DependencyTimelineRule::SameSubmissionOrdering,
	  1, 0, 1, 1, 1, 0, 0, 0 },
	{ DependencyEdge::TemporalHistoryAcrossFrames,
	  "temporal history across frames",
	  kPipelineStageColorAttachment | kPipelineStageCompute,
	  kAccessColorAttachmentWrite | kAccessStorageWrite,
	  L(ResourceLayout::ColorAttachment) | L(ResourceLayout::General),
	  kPipelineStageFragmentShader | kPipelineStageCompute,
	  kAccessSampledImageRead | kAccessStorageRead,
	  L(ResourceLayout::ShaderReadOnly) | L(ResourceLayout::General),
	  kTrackImageAspectMipLayer | kTrackLogicalResourceVersion,
	  DependencyTimelineRule::CrossFrameTimelineVersion,
	  1, 0, 1, 1, 1, 0, 0, 0 },
	{ DependencyEdge::TextureMeshUploadAndRemesh,
	  "texture/mesh upload and remesh",
	  kPipelineStageHost | kPipelineStageTransfer | kPipelineStageCompute,
	  kAccessHostWrite | kAccessTransferWrite | kAccessShaderWrite,
	  L(ResourceLayout::NotApplicable) | L(ResourceLayout::TransferDestination) |
	  L(ResourceLayout::General),
	  kPipelineStageTransfer | kPipelineStageCompute | kPipelineStageIndirect |
	  kPipelineStageVertexInput | kPipelineStageVertexShader |
	  kPipelineStageFragmentShader,
	  kAccessTransferRead | kAccessTransferWrite | kAccessSampledImageRead |
	  kAccessStorageRead | kAccessStorageWrite | kAccessIndirectRead |
	  kAccessVertexAttributeRead | kAccessIndexRead,
	  L(ResourceLayout::NotApplicable) | L(ResourceLayout::TransferDestination) |
	  L(ResourceLayout::General) | L(ResourceLayout::ShaderReadOnly),
	  kTrackBufferByteRange | kTrackImageAspectMipLayer |
	  kTrackLogicalResourceVersion,
	  DependencyTimelineRule::SubmissionTimelineRetirement,
	  1, 1, 1, 1, 1, 0, 0, 0 },
	{ DependencyEdge::ReadbackCopyToHost,
	  "readback copy -> host",
	  kPipelineStageTransfer, kAccessTransferWrite,
	  L(ResourceLayout::NotApplicable),
	  kPipelineStageHost, kAccessHostRead, L(ResourceLayout::NotApplicable),
	  kTrackBufferByteRange | kTrackHostMappedAtomRange,
	  DependencyTimelineRule::HostWaitForExactSubmittedTimeline,
	  1, 0, 1, 1, 0, 1, 0, 0 },
	{ DependencyEdge::SwapchainAcquireToColorRender,
	  "swapchain acquire -> color render",
	  kPipelineStagePresent, 0,
	  L(ResourceLayout::Undefined) | L(ResourceLayout::Present),
	  kPipelineStageColorAttachment,
	  kAccessColorAttachmentWrite,
	  L(ResourceLayout::ColorAttachment),
	  kTrackImageAspectMipLayer | kTrackSwapchainImage,
	  DependencyTimelineRule::WsiBinaryAcquireRenderPresent,
	  0, 1, 1, 1, 1, 0, 1, 0,
	  WsiQueueOwnershipDirection::PresentToGraphics,
	  WsiBinarySemaphoreOperation::WaitAcquireBeforeColorRender },
	{ DependencyEdge::SwapchainColorRenderToPresent,
	  "swapchain color render -> present",
	  kPipelineStageColorAttachment, kAccessColorAttachmentWrite,
	  L(ResourceLayout::ColorAttachment),
	  kPipelineStagePresent, 0, L(ResourceLayout::Present),
	  kTrackImageAspectMipLayer | kTrackSwapchainImage,
	  DependencyTimelineRule::WsiBinaryAcquireRenderPresent,
	  1, 0, 1, 1, 1, 0, 1, 0,
	  WsiQueueOwnershipDirection::GraphicsToPresent,
	  WsiBinarySemaphoreOperation::SignalRenderFinishedBeforePresent },
	{ DependencyEdge::ContinuationAfterSynchronousReadback,
	  "continuation after prefix readback",
	  kTrackedPipelineStageMask, kTrackedResourceAccessMask,
	  kTrackedResourceLayoutMask,
	  kTrackedPipelineStageMask, kTrackedResourceAccessMask,
	  kTrackedResourceLayoutMask,
	  kTrackAllResourceRanges,
	  DependencyTimelineRule::ContinuationWaitAndInheritPrefix,
	  1, 1, 1, 1, 1, 1, 0, 1 },
};

#undef L

constexpr bool DependencyContractBooleansValid(
	const DependencyEdgeContract &edge)
{
	const bool wsi_edge = edge.timeline_rule ==
		DependencyTimelineRule::WsiBinaryAcquireRenderPresent;
	return edge.producer_may_write <= 1 && edge.consumer_may_write <= 1 &&
		edge.queue_family_tracked == 1 && edge.barrier_from_actual_state == 1 &&
		edge.transition_layout_when_different <= 1 &&
		edge.exact_timeline_completion_required <= 1 &&
		edge.wsi_binary_chain <= 1 && edge.continuation_inherits_exact_state <= 1 &&
		(edge.producer_may_write != 0) ==
			((edge.producer_access & kTrackedWriteAccessMask) != 0) &&
		(edge.consumer_may_write != 0) ==
			((edge.consumer_access & kTrackedWriteAccessMask) != 0) &&
		(edge.transition_layout_when_different != 0) ==
			((edge.tracked_subresources & kTrackImageAspectMipLayer) != 0) &&
		(edge.exact_timeline_completion_required != 0) ==
			(edge.timeline_rule ==
				 DependencyTimelineRule::HostWaitForExactSubmittedTimeline ||
			 edge.timeline_rule ==
				 DependencyTimelineRule::ContinuationWaitAndInheritPrefix) &&
		(edge.wsi_binary_chain != 0) ==
			wsi_edge &&
		(wsi_edge == (edge.wsi_queue_ownership !=
			WsiQueueOwnershipDirection::None)) &&
		(wsi_edge == (edge.wsi_binary_operation !=
			WsiBinarySemaphoreOperation::None)) &&
		static_cast<uint32_t>(edge.wsi_queue_ownership) <
			static_cast<uint32_t>(WsiQueueOwnershipDirection::Count) &&
		static_cast<uint32_t>(edge.wsi_binary_operation) <
			static_cast<uint32_t>(WsiBinarySemaphoreOperation::Count) &&
		(edge.continuation_inherits_exact_state != 0) ==
			(edge.timeline_rule == DependencyTimelineRule::ContinuationWaitAndInheritPrefix);
}

constexpr uint32_t kQueueFamilyIgnored = 0xffffffffu;

struct WsiQueueOwnershipDecision
{
	uint32_t valid;
	uint32_t emit_ownership_transfer;
	uint32_t source_queue_family;
	uint32_t destination_queue_family;
};

// This decision is directly consumable by the synchronization compiler.  A
// same-family or concurrent image uses VK_QUEUE_FAMILY_IGNORED; an exclusive
// separate-family image receives the direction named by its dependency edge.
inline WsiQueueOwnershipDecision ResolveWsiQueueOwnership(
	WsiQueueOwnershipDirection direction, uint32_t graphics_queue_family,
	uint32_t present_queue_family, uint32_t concurrent_sharing)
{
	const bool valid = direction != WsiQueueOwnershipDirection::None &&
		static_cast<uint32_t>(direction) <
			static_cast<uint32_t>(WsiQueueOwnershipDirection::Count) &&
		graphics_queue_family != kQueueFamilyIgnored &&
		present_queue_family != kQueueFamilyIgnored && concurrent_sharing <= 1;
	if (!valid)
		return { 0, 0, kQueueFamilyIgnored, kQueueFamilyIgnored };
	if (concurrent_sharing || graphics_queue_family == present_queue_family)
		return { 1, 0, kQueueFamilyIgnored, kQueueFamilyIgnored };
	if (direction == WsiQueueOwnershipDirection::PresentToGraphics)
		return { 1, 1, present_queue_family, graphics_queue_family };
	return { 1, 1, graphics_queue_family, present_queue_family };
}

constexpr bool DependencyContractsValid(size_t index)
{
	return index == static_cast<size_t>(DependencyEdge::Count) ? true :
		(static_cast<size_t>(kDependencyEdgeContract[index].edge) == index &&
		 kDependencyEdgeContract[index].producer_stages != 0 &&
		 kDependencyEdgeContract[index].consumer_stages != 0 &&
		 (kDependencyEdgeContract[index].producer_stages &
		  ~kTrackedPipelineStageMask) == 0 &&
		 (kDependencyEdgeContract[index].consumer_stages &
		  ~kTrackedPipelineStageMask) == 0 &&
		 (kDependencyEdgeContract[index].producer_access &
		  ~kTrackedResourceAccessMask) == 0 &&
		 (kDependencyEdgeContract[index].consumer_access &
		  ~kTrackedResourceAccessMask) == 0 &&
		 kDependencyEdgeContract[index].producer_layouts != 0 &&
		 kDependencyEdgeContract[index].consumer_layouts != 0 &&
		 (kDependencyEdgeContract[index].producer_layouts &
		  ~kTrackedResourceLayoutMask) == 0 &&
		 (kDependencyEdgeContract[index].consumer_layouts &
		  ~kTrackedResourceLayoutMask) == 0 &&
		 kDependencyEdgeContract[index].tracked_subresources != 0 &&
		 (kDependencyEdgeContract[index].tracked_subresources &
		  ~kTrackAllResourceRanges) == 0 &&
		 static_cast<uint32_t>(kDependencyEdgeContract[index].timeline_rule) <
		  static_cast<uint32_t>(DependencyTimelineRule::Count) &&
		 DependencyContractBooleansValid(kDependencyEdgeContract[index]) &&
		 DependencyContractsValid(index + 1));
}

enum class DeathRowObjectType : uint32_t
{
	ImageViewSamplerVersion = 0,
	BufferAllocationOrVirtualRange,
	DescriptorPoolOrPage,
	PipelineOrLayout,
	InternalTargetOrHistory,
	QueryPool,
	RetainedMeshGeneration,
	WsiGeneration,
};

struct FrameContextOwnershipContract
{
	uint32_t frame_context_count;
	uint32_t one_renderer_timeline;
	uint32_t reset_only_after_exact_timeline;
	uint32_t mapped_upload_then_device_local_copy;
	uint32_t ordinary_submission_count;
	uint32_t device_wait_idle_hot_path;
};

constexpr FrameContextOwnershipContract kFrameContextOwnershipContract =
	{ 2, 1, 1, 1, 1, 0 };

static_assert(sizeof(kDependencyEdgeContract) / sizeof(kDependencyEdgeContract[0]) ==
	static_cast<size_t>(DependencyEdge::Count), "dependency edge contract mismatch");
static_assert(DependencyContractsValid(0),
	"dependency edges require explicit valid stage/access/layout/range/timeline facts");

} // namespace render
} // namespace piccu
