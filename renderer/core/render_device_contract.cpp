#include "render_device_contract.h"

namespace piccu
{
namespace render
{

const DeviceFeatureRequirement kRequiredDeviceFeatures[] = {
	{ RequiredDeviceFeature::DynamicRendering, "dynamicRendering", 1 },
	{ RequiredDeviceFeature::Synchronization2, "synchronization2", 1 },
	{ RequiredDeviceFeature::TimelineSemaphore, "timelineSemaphore", 1 },
	{ RequiredDeviceFeature::IndependentBlend, "independentBlend", 1 },
	{ RequiredDeviceFeature::MultiDrawIndirect, "multiDrawIndirect", 1 },
	{ RequiredDeviceFeature::ShaderDrawParameters, "shaderDrawParameters/gl_DrawID", 1 },
	{ RequiredDeviceFeature::NonUniformSampledImageIndexing, "nonUniform sampled-image indexing", 1 },
	{ RequiredDeviceFeature::ExtendedDynamicRasterDepthState,
		"Vulkan 1.3 dynamic cull/front/depth/depth-bias commands", 1 },
	{ RequiredDeviceFeature::Swapchain, "VK_KHR_swapchain", 1 },
	{ RequiredDeviceFeature::PlatformSurface, "platform surface extension", 1 },
	{ RequiredDeviceFeature::GraphicsQueue, "graphics queue", 1 },
	{ RequiredDeviceFeature::ComputeOnGraphicsQueue, "compute bit on graphics queue", 1 },
	{ RequiredDeviceFeature::Presentation, "presentation support", 1 },
};

const size_t kRequiredDeviceFeatureCount =
	sizeof(kRequiredDeviceFeatures) / sizeof(kRequiredDeviceFeatures[0]);

const DeviceLimitRequirement kRequiredDeviceLimits[] = {
	{ DeviceLimit::MaxColorAttachments, "maxColorAttachments", LimitValidation::Minimum, 5 },
	{ DeviceLimit::MaxFragmentOutputAttachments, "maxFragmentOutputAttachments", LimitValidation::Minimum, 5 },
	{ DeviceLimit::MaxFragmentCombinedOutputResources, "maxFragmentCombinedOutputResources", LimitValidation::DerivedFromReflectedAbi, 0 },
	{ DeviceLimit::MaxBoundDescriptorSets, "maxBoundDescriptorSets", LimitValidation::Minimum, 3 },
	{ DeviceLimit::MaxPerStageResources, "maxPerStageResources", LimitValidation::DerivedFromReflectedAbi, 0 },
	{ DeviceLimit::MaxPerStageDescriptorUniformBuffers, "maxPerStageDescriptorUniformBuffers", LimitValidation::Minimum, 1 },
	{ DeviceLimit::MaxDescriptorSetUniformBuffers, "maxDescriptorSetUniformBuffers", LimitValidation::Minimum, 1 },
	{ DeviceLimit::MaxDescriptorSetUniformBuffersDynamic, "maxDescriptorSetUniformBuffersDynamic", LimitValidation::Minimum, 1 },
	{ DeviceLimit::MaxPerStageDescriptorStorageBuffers, "maxPerStageDescriptorStorageBuffers", LimitValidation::Minimum, 8 },
	{ DeviceLimit::MaxDescriptorSetStorageBuffers, "maxDescriptorSetStorageBuffers", LimitValidation::Minimum, 8 },
	{ DeviceLimit::MaxPerStageDescriptorSamplers, "maxPerStageDescriptorSamplers", LimitValidation::Minimum, 32 },
	{ DeviceLimit::MaxDescriptorSetSamplers, "maxDescriptorSetSamplers", LimitValidation::Minimum, 32 },
	{ DeviceLimit::MaxPerStageDescriptorSampledImages, "maxPerStageDescriptorSampledImages", LimitValidation::DerivedFromReflectedAbi, 40 },
	{ DeviceLimit::MaxDescriptorSetSampledImages, "maxDescriptorSetSampledImages", LimitValidation::DerivedFromReflectedAbi, 40 },
	{ DeviceLimit::MaxStorageBufferRange, "maxStorageBufferRange", LimitValidation::DerivedFromRequestedSignature, 0 },
	{ DeviceLimit::MinStorageBufferOffsetAlignment, "minStorageBufferOffsetAlignment", LimitValidation::QueryForDiagnostics, 0 },
	{ DeviceLimit::MinUniformBufferOffsetAlignment, "minUniformBufferOffsetAlignment", LimitValidation::QueryForDiagnostics, 0 },
	{ DeviceLimit::MaxBufferSize, "maxBufferSize", LimitValidation::DerivedFromRequestedSignature, 0 },
	{ DeviceLimit::MaxTexelBufferElements, "maxTexelBufferElements", LimitValidation::DerivedFromRequestedSignature, 0 },
	{ DeviceLimit::MaxDrawIndirectCount, "maxDrawIndirectCount", LimitValidation::Minimum, 1 },
	{ DeviceLimit::MaxPushConstantsSize, "maxPushConstantsSize", LimitValidation::Minimum, 32 },
	{ DeviceLimit::MaxComputeWorkGroupInvocations, "maxComputeWorkGroupInvocations", LimitValidation::Minimum, 256 },
	{ DeviceLimit::MaxComputeWorkGroupSizeX, "maxComputeWorkGroupSize[0]", LimitValidation::Minimum, 256 },
	{ DeviceLimit::MaxComputeSharedMemorySize, "maxComputeSharedMemorySize", LimitValidation::Minimum, 16 * 1024 },
	{ DeviceLimit::MaxComputeWorkGroupCount, "maxComputeWorkGroupCount", LimitValidation::DerivedFromRequestedSignature, 0 },
	{ DeviceLimit::MaxImageDimension2D, "maxImageDimension2D", LimitValidation::DerivedFromRequestedSignature, 0 },
	{ DeviceLimit::MaxImageArrayLayers, "maxImageArrayLayers", LimitValidation::DerivedFromRequestedSignature, 0 },
	{ DeviceLimit::MaxFramebufferWidth, "maxFramebufferWidth", LimitValidation::DerivedFromRequestedSignature, 0 },
	{ DeviceLimit::MaxFramebufferHeight, "maxFramebufferHeight", LimitValidation::DerivedFromRequestedSignature, 0 },
	{ DeviceLimit::MaxFramebufferLayers, "maxFramebufferLayers", LimitValidation::DerivedFromRequestedSignature, 0 },
	{ DeviceLimit::MaxViewportDimensions, "maxViewportDimensions", LimitValidation::DerivedFromRequestedSignature, 0 },
	{ DeviceLimit::ViewportBoundsRange, "viewportBoundsRange", LimitValidation::DerivedFromRequestedSignature, 0 },
	{ DeviceLimit::MaxVertexInputBindings, "maxVertexInputBindings", LimitValidation::DerivedFromReflectedAbi, 0 },
	{ DeviceLimit::MaxVertexInputAttributes, "maxVertexInputAttributes", LimitValidation::DerivedFromReflectedAbi, 0 },
	{ DeviceLimit::MaxVertexInputBindingStride, "maxVertexInputBindingStride", LimitValidation::DerivedFromReflectedAbi, sizeof(BaseVertex) },
	{ DeviceLimit::IndexAndDrawOffsetRanges, "index/draw offset ranges", LimitValidation::DerivedFromRequestedSignature, 0 },
	{ DeviceLimit::NonCoherentAtomSize, "nonCoherentAtomSize", LimitValidation::QueryForDiagnostics, 0 },
	{ DeviceLimit::CopyOffsetAlignment, "optimalBufferCopyOffsetAlignment", LimitValidation::QueryForDiagnostics, 0 },
	{ DeviceLimit::CopyRowPitchAlignment, "optimalBufferCopyRowPitchAlignment", LimitValidation::QueryForDiagnostics, 0 },
	{ DeviceLimit::TimestampPeriod, "timestampPeriod", LimitValidation::QueryForDiagnostics, 0 },
	{ DeviceLimit::TimestampValidBits, "timestampValidBits", LimitValidation::QueryForDiagnostics, 0 },
	{ DeviceLimit::FramebufferSampleCounts, "framebuffer sample-count intersection", LimitValidation::DerivedFromRequestedSignature, 0 },
	{ DeviceLimit::MemoryHeapBudgets, "memory heaps/VMA budgets", LimitValidation::QueryForDiagnostics, 0 },
};

const size_t kRequiredDeviceLimitCount =
	sizeof(kRequiredDeviceLimits) / sizeof(kRequiredDeviceLimits[0]);

const FormatRequirement kRequiredFormats[] = {
	{ FormatSemantic::SceneAndPostColor, RenderFormat::R8G8B8A8Unorm,
		kFormatSampled | kFormatColorAttachment | kFormatBlend |
		kFormatTransferSource | kFormatTransferDestination |
		kFormatLinearFilter | kFormatNearestFilter | kFormatMultisample },
	{ FormatSemantic::Velocity, RenderFormat::R16G16Sfloat,
		kFormatSampled | kFormatColorAttachment | kFormatTransferSource |
		kFormatTransferDestination | kFormatLinearFilter |
		kFormatNearestFilter | kFormatMultisample },
	{ FormatSemantic::ProtectionMask, RenderFormat::R8G8Unorm,
		kFormatSampled | kFormatColorAttachment | kFormatBlend |
		kFormatTransferSource | kFormatTransferDestination |
		kFormatLinearFilter | kFormatNearestFilter | kFormatMultisample },
	{ FormatSemantic::AoClass, RenderFormat::R8Unorm,
		kFormatSampled | kFormatColorAttachment | kFormatTransferSource |
		kFormatTransferDestination | kFormatLinearFilter |
		kFormatNearestFilter | kFormatMultisample },
	{ FormatSemantic::MotionObjectId, RenderFormat::R32Uint,
		kFormatSampled | kFormatColorAttachment | kFormatTransferSource |
		kFormatTransferDestination | kFormatNearestFilter | kFormatMultisample },
	{ FormatSemantic::Depth, RenderFormat::D32Sfloat,
		kFormatSampled | kFormatDepthAttachment | kFormatTransferSource |
		kFormatTransferDestination | kFormatNearestFilter | kFormatMultisample },
	{ FormatSemantic::GtaoDepthWeight, RenderFormat::R32G32Sfloat,
		kFormatSampled | kFormatColorAttachment | kFormatTransferSource |
		kFormatTransferDestination | kFormatLinearFilter | kFormatNearestFilter },
	{ FormatSemantic::GtaoRawBlur, RenderFormat::R16G16Sfloat,
		kFormatSampled | kFormatColorAttachment | kFormatTransferSource |
		kFormatTransferDestination | kFormatLinearFilter | kFormatNearestFilter },
	{ FormatSemantic::GtaoHistory, RenderFormat::R16G16B16A16Sfloat,
		kFormatSampled | kFormatColorAttachment | kFormatTransferSource |
		kFormatTransferDestination | kFormatLinearFilter | kFormatNearestFilter },
	{ FormatSemantic::GtaoSuppression, RenderFormat::R8Unorm,
		kFormatSampled | kFormatColorAttachment | kFormatTransferSource |
		kFormatTransferDestination | kFormatLinearFilter | kFormatNearestFilter },
};

const size_t kRequiredFormatCount = sizeof(kRequiredFormats) / sizeof(kRequiredFormats[0]);

bool PhysicalDeviceUuidIsZero(const PhysicalDeviceUuid &uuid)
{
	for (uint32_t i = 0; i < kPhysicalDeviceUuidSize; ++i)
		if (uuid.bytes[i] != 0)
			return false;
	return true;
}

bool PhysicalDeviceUuidEqual(const PhysicalDeviceUuid &left,
	const PhysicalDeviceUuid &right)
{
	for (uint32_t i = 0; i < kPhysicalDeviceUuidSize; ++i)
		if (left.bytes[i] != right.bytes[i])
			return false;
	return true;
}

namespace
{

bool RequiredApiVersionSupported(const DeviceProfileSupport &profile)
{
	return profile.api_major > kVulkanRequiredApiMajor ||
		(profile.api_major == kVulkanRequiredApiMajor &&
		 profile.api_minor >= kVulkanRequiredApiMinor);
}

uint32_t HighestCommonSampleCount(uint32_t mask)
{
	const uint32_t ordered[] = { 8, 4, 2, 1 };
	for (size_t i = 0; i < sizeof(ordered) / sizeof(ordered[0]); ++i)
		if ((mask & ordered[i]) != 0)
			return ordered[i];
	return 0;
}

int32_t CompareUuidAscending(const PhysicalDeviceUuid &left,
	const PhysicalDeviceUuid &right)
{
	for (uint32_t i = 0; i < kPhysicalDeviceUuidSize; ++i)
	{
		if (left.bytes[i] < right.bytes[i])
			return 1;
		if (left.bytes[i] > right.bytes[i])
			return -1;
	}
	return 0;
}

PhysicalDeviceSelection EmptyDeviceSelection(DeviceSelectionStatus status)
{
	PhysicalDeviceSelection selection = {};
	selection.status = status;
	selection.candidate_position = kInvalidId;
	selection.enumeration_index = kInvalidId;
	return selection;
}

void BindSelection(PhysicalDeviceSelection *selection,
	const PhysicalDeviceCandidate &candidate, uint32_t position)
{
	selection->candidate_position = position;
	selection->enumeration_index = candidate.enumeration_index;
	selection->uuid = candidate.uuid;
}

} // namespace

bool SupportsRequiredDeviceProfile(const DeviceProfileSupport &profile)
{
	if (!RequiredApiVersionSupported(profile) ||
		profile.required_feature_bits != kAllRequiredDeviceFeatureBits ||
		profile.required_format_bits != kAllRequiredFormatBits ||
		profile.all_required_limits_satisfied != 1 ||
		profile.surface_configuration_satisfied != 1 ||
		profile.requested_signature_supported != 1 ||
		profile.graphics_compute_queue_family == kInvalidId ||
		profile.present_queue_family == kInvalidId ||
		(profile.common_sample_count_mask & 1u) == 0)
		return false;

	return profile.descriptor_page_tier >= kDescriptorPageTiers[0] &&
		SelectDescriptorPageTier(profile.descriptor_page_tier) ==
			profile.descriptor_page_tier;
}

int32_t ComparePhysicalDevicePreference(const PhysicalDeviceCandidate &left,
	const PhysicalDeviceCandidate &right)
{
	const uint32_t left_unified =
		left.profile.graphics_compute_queue_family == left.profile.present_queue_family;
	const uint32_t right_unified =
		right.profile.graphics_compute_queue_family == right.profile.present_queue_family;
	if (left_unified != right_unified)
		return left_unified ? 1 : -1;

	const uint32_t left_type = static_cast<uint32_t>(left.type);
	const uint32_t right_type = static_cast<uint32_t>(right.type);
	if (left_type != right_type)
		return left_type > right_type ? 1 : -1;
	if (left.device_local_budget_bytes != right.device_local_budget_bytes)
		return left.device_local_budget_bytes > right.device_local_budget_bytes ? 1 : -1;
	if (left.profile.descriptor_page_tier != right.profile.descriptor_page_tier)
		return left.profile.descriptor_page_tier > right.profile.descriptor_page_tier ? 1 : -1;

	const uint32_t left_samples =
		HighestCommonSampleCount(left.profile.common_sample_count_mask);
	const uint32_t right_samples =
		HighestCommonSampleCount(right.profile.common_sample_count_mask);
	if (left_samples != right_samples)
		return left_samples > right_samples ? 1 : -1;
	return CompareUuidAscending(left.uuid, right.uuid);
}

PhysicalDeviceSelection SelectPhysicalDevice(
	const PhysicalDeviceCandidate *candidates, size_t candidate_count,
	const DeviceSelectionOverride &selection_override)
{
	if ((candidate_count != 0 && candidates == nullptr) ||
		candidate_count > UINT32_MAX ||
		static_cast<uint32_t>(selection_override.kind) >=
			static_cast<uint32_t>(DeviceSelectionOverrideKind::Count))
		return EmptyDeviceSelection(DeviceSelectionStatus::InvalidInput);

	if ((selection_override.kind == DeviceSelectionOverrideKind::Uuid &&
		 PhysicalDeviceUuidIsZero(selection_override.uuid)) ||
		(selection_override.kind == DeviceSelectionOverrideKind::EnumerationIndex &&
		 selection_override.enumeration_index == kInvalidId))
		return EmptyDeviceSelection(DeviceSelectionStatus::InvalidOverride);

	for (size_t i = 0; i < candidate_count; ++i)
	{
		if (PhysicalDeviceUuidIsZero(candidates[i].uuid) ||
			candidates[i].enumeration_index >= candidate_count ||
			static_cast<uint32_t>(candidates[i].type) >=
				static_cast<uint32_t>(PhysicalDeviceType::Count))
			return EmptyDeviceSelection(DeviceSelectionStatus::InvalidInput);
		for (size_t j = 0; j < i; ++j)
		{
			if (PhysicalDeviceUuidEqual(candidates[i].uuid, candidates[j].uuid))
				return EmptyDeviceSelection(DeviceSelectionStatus::DuplicateUuid);
			if (candidates[i].enumeration_index == candidates[j].enumeration_index)
				return EmptyDeviceSelection(
					DeviceSelectionStatus::DuplicateEnumerationIndex);
		}
	}

	if (selection_override.kind != DeviceSelectionOverrideKind::None)
	{
		for (size_t i = 0; i < candidate_count; ++i)
		{
			const bool match = selection_override.kind ==
					DeviceSelectionOverrideKind::Uuid ?
				PhysicalDeviceUuidEqual(candidates[i].uuid, selection_override.uuid) :
				candidates[i].enumeration_index ==
					selection_override.enumeration_index;
			if (!match)
				continue;
			PhysicalDeviceSelection selection = EmptyDeviceSelection(
				SupportsRequiredDeviceProfile(candidates[i].profile) ?
					DeviceSelectionStatus::Success :
					DeviceSelectionStatus::OverrideRejectedByRequiredProfile);
			BindSelection(&selection, candidates[i], static_cast<uint32_t>(i));
			return selection;
		}
		return EmptyDeviceSelection(DeviceSelectionStatus::OverrideNotFound);
	}

	uint32_t best = kInvalidId;
	for (size_t i = 0; i < candidate_count; ++i)
	{
		if (!SupportsRequiredDeviceProfile(candidates[i].profile))
			continue;
		if (best == kInvalidId || ComparePhysicalDevicePreference(
			candidates[i], candidates[best]) > 0)
			best = static_cast<uint32_t>(i);
	}
	if (best == kInvalidId)
		return EmptyDeviceSelection(DeviceSelectionStatus::NoRequiredProfileDevice);

	PhysicalDeviceSelection selection =
		EmptyDeviceSelection(DeviceSelectionStatus::Success);
	BindSelection(&selection, candidates[best], best);
	return selection;
}

const WorldDescriptorBindingContract kWorldDescriptorBindings[] = {
	{ 0, 0, DescriptorKind::DynamicUniformBuffer, 1, 0, kStageVertex | kStageFragment | kStageCompute },
	{ 0, 1, DescriptorKind::Sampler, kWorldSamplerCount, 0, kStageVertex | kStageFragment | kStageCompute },
	{ 1, 0, DescriptorKind::SampledFloat2D, 0, 1, kStageVertex | kStageFragment | kStageCompute },
	{ 1, 1, DescriptorKind::SampledFloat2DArray, kWorldArrayImageCount, 0, kStageVertex | kStageFragment | kStageCompute },
	{ 2, 0, DescriptorKind::StorageBuffer, 1, 0, kStageVertex | kStageFragment | kStageCompute },
	{ 2, 1, DescriptorKind::StorageBuffer, 1, 0, kStageVertex | kStageFragment | kStageCompute },
	{ 2, 2, DescriptorKind::StorageBuffer, 1, 0, kStageVertex | kStageFragment | kStageCompute },
	{ 2, 3, DescriptorKind::StorageBuffer, 1, 0, kStageVertex | kStageFragment | kStageCompute },
	{ 2, 4, DescriptorKind::StorageBuffer, 1, 0, kStageVertex | kStageFragment | kStageCompute },
	{ 2, 5, DescriptorKind::StorageBuffer, 1, 0, kStageVertex | kStageFragment | kStageCompute },
	{ 2, 6, DescriptorKind::StorageBuffer, 1, 0, kStageVertex | kStageFragment | kStageCompute },
	{ 2, 7, DescriptorKind::StorageBuffer, 1, 0, kStageVertex | kStageFragment | kStageCompute },
};

const size_t kWorldDescriptorBindingCount =
	sizeof(kWorldDescriptorBindings) / sizeof(kWorldDescriptorBindings[0]);

#define POST_UNIFORMS(node, variant) \
	{ GraphNodeId::node, PostPassVariant::variant, kPostDescriptorSet, \
	  kPostUniformBinding, PostDescriptorKind::UniformBuffer, 1, \
	  PostDescriptorRequirement::Required, \
	  PostDescriptorResourceSemantic::PassUniforms, GraphResource::Count, \
	  GraphInputSemantic::None },
#define POST_UNIFORMS_MATRICES(node, variant) \
	{ GraphNodeId::node, PostPassVariant::variant, kPostDescriptorSet, \
	  kPostUniformBinding, PostDescriptorKind::UniformBuffer, 1, \
	  PostDescriptorRequirement::Required, \
	  PostDescriptorResourceSemantic::PassUniforms, GraphResource::CapturedMatrices, \
	  GraphInputSemantic::None },
#define POST_UNIFORMS_OPTIONAL_MATRICES(node, variant) \
	{ GraphNodeId::node, PostPassVariant::variant, kPostDescriptorSet, \
	  kPostUniformBinding, PostDescriptorKind::UniformBuffer, 1, \
	  PostDescriptorRequirement::Required, \
	  PostDescriptorResourceSemantic::PassUniforms, GraphResource::CapturedMatrices, \
	  GraphInputSemantic::None },
#define POST_SAMPLERS(node, variant) \
	{ GraphNodeId::node, PostPassVariant::variant, kPostDescriptorSet, \
	  kPostSamplerTableBinding, PostDescriptorKind::Sampler, \
	  kPostSamplerTableSize, PostDescriptorRequirement::Required, \
	  PostDescriptorResourceSemantic::FixedSamplerTable, GraphResource::Count, \
	  GraphInputSemantic::None },
#define POST_RESOURCE(node, variant, binding_index, descriptor_kind, required, graph_resource) \
	{ GraphNodeId::node, PostPassVariant::variant, kPostDescriptorSet, \
	  binding_index, PostDescriptorKind::descriptor_kind, 1, \
	  PostDescriptorRequirement::required, \
	  PostDescriptorResourceSemantic::GraphResource, GraphResource::graph_resource, \
	  GraphInputSemantic::None },
#define POST_SELECTED(node, variant, binding_index, descriptor_kind, selected) \
	{ GraphNodeId::node, PostPassVariant::variant, kPostDescriptorSet, \
	  binding_index, PostDescriptorKind::descriptor_kind, 1, \
	  PostDescriptorRequirement::Required, \
	  PostDescriptorResourceSemantic::SelectedGraphInput, GraphResource::Count, \
	  GraphInputSemantic::selected },
#define POST_SELECTED_OPTIONAL(node, variant, binding_index, descriptor_kind, selected) \
	{ GraphNodeId::node, PostPassVariant::variant, kPostDescriptorSet, \
	  binding_index, PostDescriptorKind::descriptor_kind, 1, \
	  PostDescriptorRequirement::Optional, \
	  PostDescriptorResourceSemantic::SelectedGraphInput, GraphResource::Count, \
	  GraphInputSemantic::selected },
#define POST_HEADER(node, variant) \
	POST_UNIFORMS(node, variant) \
	POST_SAMPLERS(node, variant)
#define POST_HEADER_MATRICES(node, variant) \
	POST_UNIFORMS_MATRICES(node, variant) \
	POST_SAMPLERS(node, variant)
#define POST_HEADER_OPTIONAL_MATRICES(node, variant) \
	POST_UNIFORMS_OPTIONAL_MATRICES(node, variant) \
	POST_SAMPLERS(node, variant)

const PostPassDescriptorBindingContract kPostPassDescriptorBindings[] = {
	POST_HEADER_MATRICES(CapWorld, SingleSample)
	POST_RESOURCE(CapWorld, SingleSample, 2, SampledFloat2D, Required, SceneColor)
	POST_HEADER_MATRICES(CapWorld, Multisample)
	POST_RESOURCE(CapWorld, Multisample, 2, SampledFloat2DMultisample, Required, SceneColor)
	POST_HEADER_MATRICES(CapWorld, SsaaFourToTwo)
	POST_RESOURCE(CapWorld, SsaaFourToTwo, 2, SampledFloat2D, Optional, SceneColor)
	POST_RESOURCE(CapWorld, SsaaFourToTwo, 3, SampledFloat2D, Optional, CapturedWorldMsaaResolved)
	POST_HEADER_MATRICES(CapWorld, SsaaTwoToOne)
	POST_RESOURCE(CapWorld, SsaaTwoToOne, 2, SampledFloat2D, Optional, SceneColor)
	POST_RESOURCE(CapWorld, SsaaTwoToOne, 3, SampledFloat2D, Optional, CapturedWorldMsaaResolved)
	POST_RESOURCE(CapWorld, SsaaTwoToOne, 4, SampledFloat2D, Optional, CapturedWorldIntermediate2x)

	POST_HEADER(CapDepthLogical, SingleSample)
	POST_RESOURCE(CapDepthLogical, SingleSample, 2, SampledDepth2D, Required, SceneDepth)
	POST_HEADER(CapDepthLogical, Multisample)
	POST_RESOURCE(CapDepthLogical, Multisample, 2, SampledDepth2DMultisample, Required, SceneDepth)

	POST_HEADER(ResolveColor, Multisample)
	POST_RESOURCE(ResolveColor, Multisample, 2, SampledFloat2DMultisample, Required, SceneColor)
	POST_HEADER(ResolveDepth, Multisample)
	POST_RESOURCE(ResolveDepth, Multisample, 2, SampledDepth2DMultisample, Required, SceneDepth)
	POST_HEADER(ResolveVelocity, Multisample)
	POST_RESOURCE(ResolveVelocity, Multisample, 2, SampledFloat2DMultisample, Required, SceneVelocity)
	POST_HEADER(ResolveObjectId, Multisample)
	POST_RESOURCE(ResolveObjectId, Multisample, 2, SampledUint2DMultisample, Required, SceneObjectId)
	POST_HEADER(ResolveProtectionMask, Multisample)
	POST_RESOURCE(ResolveProtectionMask, Multisample, 2, SampledFloat2DMultisample, Required, SceneProtectionMask)
	POST_HEADER(ResolveAoClass, Multisample)
	POST_RESOURCE(ResolveAoClass, Multisample, 2, SampledFloat2DMultisample, Required, SceneAoClass)

	POST_HEADER(Ssaa4To2, Only)
	POST_SELECTED(Ssaa4To2, Only, 2, SampledFloat2D, SceneColorAfterMsaa)
	POST_HEADER(Ssaa2To1, Only)
	POST_SELECTED(Ssaa2To1, Only, 2, SampledFloat2D, SsaaTwoToOneColor)
	POST_HEADER(PrepareDepthLogical, Only)
	POST_SELECTED(PrepareDepthLogical, Only, 2, SampledDepth2D, PostDepthSource)

	POST_HEADER(AoDepth, Only)
	POST_RESOURCE(AoDepth, Only, 2, SampledDepth2D, Required, PostLogicalDepth)
	POST_SELECTED(AoDepth, Only, 3, SampledFloat2D, AoClassSource)
	POST_HEADER(AoRaw, Only)
	POST_RESOURCE(AoRaw, Only, 2, SampledFloat2D, Required, GtaoDepthWeight)
	POST_RESOURCE(AoRaw, Only, 3, SampledFloat2D, Required, GtaoNoise)
	POST_HEADER(AoBlurX, Only)
	POST_RESOURCE(AoBlurX, Only, 2, SampledFloat2D, Required, GtaoRaw)
	POST_HEADER(AoBlurY, Only)
	POST_RESOURCE(AoBlurY, Only, 2, SampledFloat2D, Required, GtaoBlurTemporary)
	POST_HEADER_MATRICES(AoTemporal, Only)
	POST_SELECTED(AoTemporal, Only, 2, SampledFloat2D, AoPreTemporalSource)
	POST_SELECTED_OPTIONAL(AoTemporal, Only, 3, SampledFloat2D, TemporalHistorySource)
	POST_SELECTED(AoTemporal, Only, 4, SampledFloat2D, VelocitySource)
	POST_SELECTED(AoTemporal, Only, 5, SampledUint2D, ObjectIdSource)
	POST_RESOURCE(AoTemporal, Only, 6, SampledDepth2D, Optional, PostLogicalDepth)
	POST_HEADER(AoSuppress, Only)
	POST_SELECTED(AoSuppress, Only, 2, SampledFloat2D, ProtectionMaskSource)
	POST_SELECTED(AoSuppress, Only, 3, SampledFloat2D, AuthoredBaseColor)
	POST_HEADER(AoApply, Only)
	POST_SELECTED(AoApply, Only, 2, SampledFloat2D, AuthoredBaseColor)
	POST_SELECTED(AoApply, Only, 3, SampledFloat2D, AoFinalSource)
	POST_RESOURCE(AoApply, Only, 4, SampledFloat2D, Required, GtaoSuppression)
	POST_HEADER(AoDeferredComposite, Only)
	POST_SELECTED(AoDeferredComposite, Only, 2, SampledFloat2D, AuthoredBaseColor)
	POST_RESOURCE(AoDeferredComposite, Only, 3, SampledFloat2D, Required, CapturedWorldColor)
	POST_RESOURCE(AoDeferredComposite, Only, 4, SampledFloat2D, Required, GtaoApplied)
	POST_SELECTED(AoDeferredComposite, Only, 5, SampledFloat2D, ProtectionMaskSource)

	POST_HEADER(BloomThreshold, Only)
	POST_SELECTED(BloomThreshold, Only, 2, SampledFloat2D, FinalAuthoredColor)
	POST_RESOURCE(BloomThreshold, Only, 3, SampledDepth2D, Required, PostLogicalDepth)
	POST_SELECTED(BloomThreshold, Only, 4, SampledFloat2D, ProtectionMaskSource)
	POST_RESOURCE(BloomThreshold, Only, 5, SampledFloat2D, Optional, CockpitAlpha)
	POST_HEADER(BloomDown, Only)
	POST_RESOURCE(BloomDown, Only, 2, SampledFloat2D, Required, BloomCurrentLevel)
	POST_HEADER(BloomUp, Only)
	POST_RESOURCE(BloomUp, Only, 2, SampledFloat2D, Required, BloomCurrentLevel)
	POST_RESOURCE(BloomUp, Only, 3, SampledFloat2D, Required, BloomSmallerLevel)

	POST_HEADER(NormalComposite, Only)
	POST_SELECTED(NormalComposite, Only, 2, SampledFloat2D, FinalAuthoredColor)
	POST_RESOURCE(NormalComposite, Only, 3, SampledFloat2D, Required, BloomMerged)
	POST_SELECTED(NormalComposite, Only, 4, SampledFloat2D, ProtectionMaskSource)
	POST_HEADER(NormalBlit, Only)
	POST_SELECTED(NormalBlit, Only, 2, SampledFloat2D, FinalAuthoredColor)
	POST_HEADER_MATRICES(MotionNormal, Only)
	POST_RESOURCE(MotionNormal, Only, 2, SampledFloat2D, Required, PostPresent)
	POST_RESOURCE(MotionNormal, Only, 3, SampledDepth2D, Required, PostLogicalDepth)
	POST_SELECTED(MotionNormal, Only, 4, SampledFloat2D, VelocitySource)
	POST_SELECTED(MotionNormal, Only, 5, SampledUint2D, ObjectIdSource)
	POST_HEADER_OPTIONAL_MATRICES(MotionDebugNormal, Only)
	POST_SELECTED(MotionDebugNormal, Only, 2, SampledFloat2D, VelocitySource)
	POST_RESOURCE(MotionDebugNormal, Only, 3, SampledDepth2D, Optional, PostLogicalDepth)
	POST_SELECTED_OPTIONAL(MotionDebugNormal, Only, 4, SampledUint2D, ObjectIdSource)

	POST_HEADER(CockpitLinearCopy, Only)
	POST_SELECTED(CockpitLinearCopy, Only, 2, SampledFloat2D, FinalAuthoredColor)
	POST_HEADER_MATRICES(MotionCockpitPre, Only)
	POST_RESOURCE(MotionCockpitPre, Only, 2, SampledFloat2D, Required, PostPresent)
	POST_RESOURCE(MotionCockpitPre, Only, 3, SampledDepth2D, Required, PostLogicalDepth)
	POST_SELECTED(MotionCockpitPre, Only, 4, SampledFloat2D, VelocitySource)
	POST_SELECTED(MotionCockpitPre, Only, 5, SampledUint2D, ObjectIdSource)
	POST_HEADER_OPTIONAL_MATRICES(MotionDebugCockpitPre, Only)
	POST_SELECTED(MotionDebugCockpitPre, Only, 2, SampledFloat2D, VelocitySource)
	POST_RESOURCE(MotionDebugCockpitPre, Only, 3, SampledDepth2D, Optional, PostLogicalDepth)
	POST_SELECTED_OPTIONAL(MotionDebugCockpitPre, Only, 4, SampledUint2D, ObjectIdSource)

	POST_HEADER(CockpitResolve, SingleSample)
	POST_RESOURCE(CockpitResolve, SingleSample, 2, SampledFloat2D, Required, CockpitScene)
	POST_HEADER(CockpitResolve, Multisample)
	POST_RESOURCE(CockpitResolve, Multisample, 2, SampledFloat2DMultisample, Required, CockpitScene)
	POST_HEADER(CockpitResolve, SsaaFourToTwo)
	POST_RESOURCE(CockpitResolve, SsaaFourToTwo, 2, SampledFloat2D, Optional, CockpitScene)
	POST_RESOURCE(CockpitResolve, SsaaFourToTwo, 3, SampledFloat2D, Optional, CockpitMsaaResolved)
	POST_HEADER(CockpitResolve, SsaaTwoToOne)
	POST_RESOURCE(CockpitResolve, SsaaTwoToOne, 2, SampledFloat2D, Optional, CockpitScene)
	POST_RESOURCE(CockpitResolve, SsaaTwoToOne, 3, SampledFloat2D, Optional, CockpitMsaaResolved)
	POST_RESOURCE(CockpitResolve, SsaaTwoToOne, 4, SampledFloat2D, Optional, CockpitSsaaIntermediate2x)
	POST_HEADER(BloomDeferred, BloomThresholdPhase)
	POST_RESOURCE(BloomDeferred, BloomThresholdPhase, 2, SampledFloat2D, Required, PostPresent)
	POST_RESOURCE(BloomDeferred, BloomThresholdPhase, 3, SampledDepth2D, Required, PostLogicalDepth)
	POST_SELECTED(BloomDeferred, BloomThresholdPhase, 4, SampledFloat2D, ProtectionMaskSource)
	POST_RESOURCE(BloomDeferred, BloomThresholdPhase, 5, SampledFloat2D, Required, CockpitAlpha)
	POST_HEADER(BloomDeferred, BloomDownsamplePhase)
	POST_RESOURCE(BloomDeferred, BloomDownsamplePhase, 2, SampledFloat2D, Required, BloomCurrentLevel)
	POST_HEADER(BloomDeferred, BloomMergePhase)
	POST_RESOURCE(BloomDeferred, BloomMergePhase, 2, SampledFloat2D, Required, BloomCurrentLevel)
	POST_RESOURCE(BloomDeferred, BloomMergePhase, 3, SampledFloat2D, Required, BloomSmallerLevel)
	POST_HEADER(CockpitOver, Only)
	POST_RESOURCE(CockpitOver, Only, 2, SampledFloat2D, Required, CockpitResolved)
	POST_HEADER(CockpitBloomGamma, Only)
	POST_RESOURCE(CockpitBloomGamma, Only, 2, SampledFloat2D, Required, PostPresent)
	POST_RESOURCE(CockpitBloomGamma, Only, 3, SampledFloat2D, Required, BloomMerged)
	POST_SELECTED(CockpitBloomGamma, Only, 4, SampledFloat2D, ProtectionMaskSource)
	POST_HEADER(CockpitGammaOnly, Only)
	POST_RESOURCE(CockpitGammaOnly, Only, 2, SampledFloat2D, Required, PostPresent)
	POST_HEADER(Present, Only)
	POST_RESOURCE(Present, Only, 2, SampledFloat2D, Required, PostPresent)
};

const size_t kPostPassDescriptorBindingCount =
	sizeof(kPostPassDescriptorBindings) / sizeof(kPostPassDescriptorBindings[0]);

#define POST_SET(node, variant, binding_total) \
	{ GraphNodeId::node, PostPassVariant::variant, \
	  PostPassDescriptorUse::ImmutablePostSet, binding_total, sizeof(PostPassUniforms), 1, 1, 1, 0 }
#define POST_SET_LOAD(node, variant, binding_total, graph_resource) \
	{ GraphNodeId::node, PostPassVariant::variant, \
	  PostPassDescriptorUse::ImmutablePostSet, binding_total, sizeof(PostPassUniforms), 1, 1, 1, \
	  GraphResourceBit(GraphResource::graph_resource) }
#define WORLD_SET(node) \
	{ GraphNodeId::node, PostPassVariant::Only, \
	  PostPassDescriptorUse::WorldDescriptorAbi, 0, 0, 0, 0, 0, 0 }
#define WORLD_SET_LOAD(node, graph_resource) \
	{ GraphNodeId::node, PostPassVariant::Only, \
	  PostPassDescriptorUse::WorldDescriptorAbi, 0, 0, 0, 0, 0, \
	  GraphResourceBit(GraphResource::graph_resource) }
#define ATTACHMENT_SET_LOAD(node, graph_resource) \
	{ GraphNodeId::node, PostPassVariant::Only, \
	  PostPassDescriptorUse::AttachmentOperation, 0, 0, 0, 0, 0, \
	  GraphResourceBit(GraphResource::graph_resource) }

const PostPassDescriptorSetContract kPostPassDescriptorSets[] = {
	POST_SET(CapWorld, SingleSample, 3),
	POST_SET(CapWorld, Multisample, 3),
	POST_SET(CapWorld, SsaaFourToTwo, 4),
	POST_SET(CapWorld, SsaaTwoToOne, 5),
	POST_SET(CapDepthLogical, SingleSample, 3),
	POST_SET(CapDepthLogical, Multisample, 3),
	POST_SET(ResolveColor, Multisample, 3),
	POST_SET(ResolveDepth, Multisample, 3),
	POST_SET(ResolveVelocity, Multisample, 3),
	POST_SET(ResolveObjectId, Multisample, 3),
	POST_SET(ResolveProtectionMask, Multisample, 3),
	POST_SET(ResolveAoClass, Multisample, 3),
	POST_SET(Ssaa4To2, Only, 3),
	POST_SET(Ssaa2To1, Only, 3),
	POST_SET(PrepareDepthLogical, Only, 3),
	POST_SET(AoDepth, Only, 4),
	POST_SET(AoRaw, Only, 4),
	POST_SET(AoBlurX, Only, 3),
	POST_SET(AoBlurY, Only, 3),
	POST_SET(AoTemporal, Only, 7),
	POST_SET(AoSuppress, Only, 4),
	POST_SET(AoApply, Only, 5),
	POST_SET(AoDeferredComposite, Only, 6),
	POST_SET(BloomThreshold, Only, 6),
	POST_SET(BloomDown, Only, 3),
	POST_SET(BloomUp, Only, 4),
	POST_SET(NormalComposite, Only, 5),
	POST_SET(NormalBlit, Only, 3),
	POST_SET(MotionNormal, Only, 6),
	POST_SET_LOAD(MotionDebugNormal, Only, 5, PostPresent),
	WORLD_SET_LOAD(NormalUi, PostPresent),
	POST_SET(CockpitLinearCopy, Only, 3),
	POST_SET(MotionCockpitPre, Only, 6),
	POST_SET_LOAD(MotionDebugCockpitPre, Only, 5, PostPresent),
	WORLD_SET_LOAD(CockpitUiPre, PostPresent),
	WORLD_SET(CockpitScene),
	ATTACHMENT_SET_LOAD(PostAlphaClear, PostPresent),
	POST_SET(CockpitResolve, SingleSample, 3),
	POST_SET(CockpitResolve, Multisample, 3),
	POST_SET(CockpitResolve, SsaaFourToTwo, 4),
	POST_SET(CockpitResolve, SsaaTwoToOne, 5),
	POST_SET(BloomDeferred, BloomThresholdPhase, 6),
	POST_SET(BloomDeferred, BloomDownsamplePhase, 3),
	POST_SET(BloomDeferred, BloomMergePhase, 4),
	POST_SET_LOAD(CockpitOver, Only, 3, PostPresent),
	POST_SET(CockpitBloomGamma, Only, 5),
	POST_SET(CockpitGammaOnly, Only, 3),
	WORLD_SET_LOAD(CockpitUiPost, PostPresent),
	POST_SET(Present, Only, 3),
};

const size_t kPostPassDescriptorSetCount =
	sizeof(kPostPassDescriptorSets) / sizeof(kPostPassDescriptorSets[0]);

#define U(field) PostUniformBit(PostUniformField::field)
#define POST_USAGE(node, required, conditional) \
	{ GraphNodeId::node, required, conditional }

#define POST_FIELD_LAYOUT(field_name, member, field_size, scalar) \
	{ PostUniformField::field_name, \
	  static_cast<uint32_t>(offsetof(PostPassUniforms, member)), field_size, \
	  PostUniformScalarKind::scalar }

const PostUniformFieldLayoutContract kPostUniformFieldLayoutContract[] = {
	POST_FIELD_LAYOUT(CurrentProjection, current_projection, 64, Float32),
	POST_FIELD_LAYOUT(CurrentInverseModelview, current_inverse_modelview, 64, Float32),
	POST_FIELD_LAYOUT(PreviousViewProjection, previous_view_projection, 64, Float32),
	POST_FIELD_LAYOUT(SourceExtent, source_extent_inv_extent, 16, Float32),
	POST_FIELD_LAYOUT(DestinationExtent, destination_extent_inv_extent, 16, Float32),
	POST_FIELD_LAYOUT(VisibleOriginSize, visible_origin_size, 16, Float32),
	POST_FIELD_LAYOUT(SourceVisibleOriginSize, source_visible_origin_size, 16, Float32),
	POST_FIELD_LAYOUT(PrimaryUv, uv_origin_scale, 16, Float32),
	POST_FIELD_LAYOUT(SecondaryUv, secondary_uv_origin_scale, 16, Float32),
	POST_FIELD_LAYOUT(VelocityUv, velocity_uv_origin_scale, 16, Float32),
	POST_FIELD_LAYOUT(SceneUv, scene_uv_origin_scale, 16, Float32),
	POST_FIELD_LAYOUT(AoUv, ao_uv_origin_scale, 16, Float32),
	POST_FIELD_LAYOUT(AlphaMaskUv, alpha_mask_uv_origin_scale, 16, Float32),
	POST_FIELD_LAYOUT(ScreenSize, screen_size_inv_size, 16, Float32),
	POST_FIELD_LAYOUT(AoScreenSize, ao_screen_size_inv_size, 16, Float32),
	POST_FIELD_LAYOUT(ProjectionInfo, projection_info, 16, Float32),
	POST_FIELD_LAYOUT(NearFarRadius, near_far_radius_radius_pixels, 16, Float32),
	POST_FIELD_LAYOUT(NoiseOriginJitter, noise_origin_jitter, 16, Float32),
	POST_FIELD_LAYOUT(BlurParameters, blur_delta_sharpness_reserved, 16, Float32),
	POST_FIELD_LAYOUT(BloomParameters, bloom_gamma_threshold_intensity_spread, 16, Float32),
	POST_FIELD_LAYOUT(AoParameters, ao_max_radius_neg_inv_radius2_bias_intensity, 16, Float32),
	POST_FIELD_LAYOUT(AoClassWeights, ao_class_weights, 16, Float32),
	POST_FIELD_LAYOUT(TemporalParameters, temporal_blend_depth_velocity_frame_time, 16, Float32),
	POST_FIELD_LAYOUT(MotionParameters0, motion_strength_legacy_object_centers, 16, Float32),
	POST_FIELD_LAYOUT(MotionParameters1, motion_legacy_frame_sphere_density_exponent, 16, Float32),
	POST_FIELD_LAYOUT(MotionParameters2, motion_periphery_combined_strength_sphere_density, 16, Float32),
	POST_FIELD_LAYOUT(MotionParameters3, motion_afterburner_exponent_fov_pixel_scalar, 16, Float32),
	POST_FIELD_LAYOUT(SampleCounts, sample_counts, 16, Uint32),
	POST_FIELD_LAYOUT(FeatureFlags, feature_flags, 16, Uint32),
	POST_FIELD_LAYOUT(IntegerParameters, integer_params, 16, Int32),
	POST_FIELD_LAYOUT(FrameBranch, frame_branch, 16, Uint32),
};

const size_t kPostUniformFieldLayoutContractCount =
	sizeof(kPostUniformFieldLayoutContract) /
	sizeof(kPostUniformFieldLayoutContract[0]);

#undef POST_FIELD_LAYOUT

const PostPassUniformUsageContract kPostPassUniformUsageContract[] = {
	POST_USAGE(CapWorld,
		U(CurrentProjection)|U(CurrentInverseModelview)|U(PreviousViewProjection)|
		U(SourceExtent)|U(DestinationExtent)|U(PrimaryUv)|U(BloomParameters)|
		U(SampleCounts)|U(IntegerParameters)|U(FrameBranch),
		U(VisibleOriginSize)|U(SourceVisibleOriginSize)|U(FeatureFlags)),
	POST_USAGE(CapDepthLogical,
		U(SourceExtent)|U(DestinationExtent)|U(PrimaryUv)|U(SampleCounts)|U(FrameBranch),
		U(VisibleOriginSize)),
	POST_USAGE(ResolveColor, U(SourceExtent)|U(DestinationExtent)|U(SampleCounts)|U(FrameBranch), 0),
	POST_USAGE(ResolveDepth, U(SourceExtent)|U(DestinationExtent)|U(SampleCounts)|U(FrameBranch), 0),
	POST_USAGE(ResolveVelocity, U(SourceExtent)|U(DestinationExtent)|U(SampleCounts)|U(FrameBranch), 0),
	POST_USAGE(ResolveObjectId, U(SourceExtent)|U(DestinationExtent)|U(SampleCounts)|U(FrameBranch), 0),
	POST_USAGE(ResolveProtectionMask, U(SourceExtent)|U(DestinationExtent)|U(SampleCounts)|U(FrameBranch), 0),
	POST_USAGE(ResolveAoClass, U(SourceExtent)|U(DestinationExtent)|U(SampleCounts)|U(FrameBranch), 0),
	POST_USAGE(Ssaa4To2,
		U(SourceExtent)|U(DestinationExtent)|U(SourceVisibleOriginSize)|U(PrimaryUv)|
		U(BloomParameters)|U(IntegerParameters)|U(FrameBranch), U(FeatureFlags)),
	POST_USAGE(Ssaa2To1,
		U(SourceExtent)|U(DestinationExtent)|U(SourceVisibleOriginSize)|U(PrimaryUv)|
		U(BloomParameters)|U(IntegerParameters)|U(FrameBranch), U(FeatureFlags)),
	POST_USAGE(PrepareDepthLogical,
		U(SourceExtent)|U(DestinationExtent)|U(PrimaryUv)|U(SampleCounts)|U(FrameBranch), 0),
	POST_USAGE(AoDepth,
		U(SourceExtent)|U(DestinationExtent)|U(ScreenSize)|U(AoScreenSize)|
		U(AoClassWeights)|U(SampleCounts)|U(FeatureFlags)|U(FrameBranch), 0),
	POST_USAGE(AoRaw,
		U(SourceExtent)|U(DestinationExtent)|U(ScreenSize)|U(AoScreenSize)|
		U(ProjectionInfo)|U(NearFarRadius)|U(NoiseOriginJitter)|U(AoParameters)|
		U(SampleCounts)|U(FrameBranch), 0),
	POST_USAGE(AoBlurX,
		U(SourceExtent)|U(DestinationExtent)|U(BlurParameters)|U(IntegerParameters)|U(FrameBranch), 0),
	POST_USAGE(AoBlurY,
		U(SourceExtent)|U(DestinationExtent)|U(BlurParameters)|U(IntegerParameters)|U(FrameBranch), 0),
	POST_USAGE(AoTemporal,
		U(CurrentProjection)|U(CurrentInverseModelview)|U(PreviousViewProjection)|
		U(SourceExtent)|U(DestinationExtent)|U(VelocityUv)|U(TemporalParameters)|
		U(FeatureFlags)|U(FrameBranch), U(ScreenSize)),
	POST_USAGE(AoSuppress,
		U(SourceExtent)|U(DestinationExtent)|U(ScreenSize)|U(AoScreenSize)|
		U(SourceVisibleOriginSize)|U(BloomParameters)|U(SampleCounts)|
		U(FeatureFlags)|U(FrameBranch), 0),
	POST_USAGE(AoApply,
		U(SourceExtent)|U(DestinationExtent)|U(AoUv)|U(AoParameters)|
		U(FeatureFlags)|U(IntegerParameters)|U(FrameBranch), 0),
	POST_USAGE(AoDeferredComposite,
		U(SourceExtent)|U(DestinationExtent)|U(VisibleOriginSize)|U(FeatureFlags)|U(FrameBranch),
		U(SceneUv)),
	POST_USAGE(BloomThreshold,
		U(SourceExtent)|U(DestinationExtent)|U(BloomParameters)|U(FeatureFlags)|U(FrameBranch),
		U(AlphaMaskUv)),
	POST_USAGE(BloomDown, U(SourceExtent)|U(DestinationExtent)|U(FrameBranch), 0),
	POST_USAGE(BloomUp,
		U(SourceExtent)|U(DestinationExtent)|U(BloomParameters)|U(FrameBranch), 0),
	POST_USAGE(NormalComposite,
		U(SourceExtent)|U(DestinationExtent)|U(PrimaryUv)|U(SceneUv)|
		U(BloomParameters)|U(FeatureFlags)|U(FrameBranch), 0),
	POST_USAGE(NormalBlit,
		U(SourceExtent)|U(DestinationExtent)|U(PrimaryUv)|U(BloomParameters)|U(FrameBranch), 0),
	POST_USAGE(MotionNormal,
		U(CurrentProjection)|U(CurrentInverseModelview)|U(PreviousViewProjection)|
		U(SourceExtent)|U(DestinationExtent)|U(VelocityUv)|U(MotionParameters0)|
		U(MotionParameters1)|U(MotionParameters2)|U(MotionParameters3)|
		U(SampleCounts)|U(FeatureFlags)|U(IntegerParameters)|U(FrameBranch), 0),
	POST_USAGE(MotionDebugNormal,
		U(SourceExtent)|U(DestinationExtent)|U(VelocityUv)|U(ScreenSize)|
		U(FeatureFlags)|U(FrameBranch),
		U(CurrentProjection)|U(CurrentInverseModelview)|U(PreviousViewProjection)),
	POST_USAGE(NormalUi, 0, 0),
	POST_USAGE(CockpitLinearCopy,
		U(SourceExtent)|U(DestinationExtent)|U(PrimaryUv)|U(BloomParameters)|U(FrameBranch), 0),
	POST_USAGE(MotionCockpitPre,
		U(CurrentProjection)|U(CurrentInverseModelview)|U(PreviousViewProjection)|
		U(SourceExtent)|U(DestinationExtent)|U(VelocityUv)|U(MotionParameters0)|
		U(MotionParameters1)|U(MotionParameters2)|U(MotionParameters3)|
		U(SampleCounts)|U(FeatureFlags)|U(IntegerParameters)|U(FrameBranch), 0),
	POST_USAGE(MotionDebugCockpitPre,
		U(SourceExtent)|U(DestinationExtent)|U(VelocityUv)|U(ScreenSize)|
		U(FeatureFlags)|U(FrameBranch),
		U(CurrentProjection)|U(CurrentInverseModelview)|U(PreviousViewProjection)),
	POST_USAGE(CockpitUiPre, 0, 0),
	POST_USAGE(CockpitScene, 0, 0),
	POST_USAGE(PostAlphaClear, 0, 0),
	POST_USAGE(CockpitResolve,
		U(SourceExtent)|U(DestinationExtent)|U(SourceVisibleOriginSize)|U(PrimaryUv)|
		U(BloomParameters)|U(SampleCounts)|U(FrameBranch), U(FeatureFlags)),
	POST_USAGE(BloomDeferred,
		U(SourceExtent)|U(DestinationExtent)|U(PrimaryUv)|U(AlphaMaskUv)|
		U(BloomParameters)|U(FeatureFlags)|U(FrameBranch), U(SceneUv)),
	POST_USAGE(CockpitOver,
		U(SourceExtent)|U(DestinationExtent)|U(PrimaryUv)|U(FrameBranch), 0),
	POST_USAGE(CockpitBloomGamma,
		U(SourceExtent)|U(DestinationExtent)|U(PrimaryUv)|U(SecondaryUv)|
		U(BloomParameters)|U(FeatureFlags)|U(FrameBranch), 0),
	POST_USAGE(CockpitGammaOnly,
		U(SourceExtent)|U(DestinationExtent)|U(PrimaryUv)|U(BloomParameters)|U(FrameBranch), 0),
	POST_USAGE(CockpitUiPost, 0, 0),
	POST_USAGE(Present,
		U(SourceExtent)|U(DestinationExtent)|U(VisibleOriginSize)|U(PrimaryUv)|
		U(BloomParameters)|U(IntegerParameters)|U(FrameBranch), 0),
};

const size_t kPostPassUniformUsageContractCount =
	sizeof(kPostPassUniformUsageContract) / sizeof(kPostPassUniformUsageContract[0]);

const CompilerGraphPhaseContract kCompilerGraphPhaseContract[] = {
	{ GraphNodeId::CapWorld, 0, 6, CompilerGraphPhaseKind::SampledPostPass,
		GraphResourceBit(GraphResource::SceneColor) |
		GraphResourceBit(GraphResource::CapturedMatrices),
		GraphResourceBit(GraphResource::CapturedWorldColor), 0, 0, 0, 1,
		PostPassVariant::SingleSample,
		CompilerGraphPhaseInvocationRule::CaptureColorSingleSsaaOne,
		{ RenderFormat::R8G8B8A8Unorm, RenderFormat::Count }, 1, 0 },
	{ GraphNodeId::CapWorld, 1, 6, CompilerGraphPhaseKind::MultisampleResolve,
		GraphResourceBit(GraphResource::SceneColor) |
		GraphResourceBit(GraphResource::CapturedMatrices),
		GraphResourceBit(GraphResource::CapturedWorldColor), 0, 0, 0, 1,
		PostPassVariant::Multisample,
		CompilerGraphPhaseInvocationRule::CaptureColorMsaaSsaaOne,
		{ RenderFormat::R8G8B8A8Unorm, RenderFormat::Count }, 1, 0 },
	{ GraphNodeId::CapWorld, 2, 6, CompilerGraphPhaseKind::MultisampleResolve,
		GraphResourceBit(GraphResource::SceneColor) |
		GraphResourceBit(GraphResource::CapturedMatrices),
		GraphResourceBit(GraphResource::CapturedWorldMsaaResolved), 0, 0, 0, 1,
		PostPassVariant::Multisample,
		CompilerGraphPhaseInvocationRule::CaptureColorMsaaBeforeSsaa,
		{ RenderFormat::R8G8B8A8Unorm, RenderFormat::Count }, 1, 0 },
	{ GraphNodeId::CapWorld, 3, 6, CompilerGraphPhaseKind::SsaaDownsample,
		GraphResourceBit(GraphResource::SceneColor) |
		GraphResourceBit(GraphResource::CapturedWorldMsaaResolved) |
		GraphResourceBit(GraphResource::CapturedMatrices),
		GraphResourceBit(GraphResource::CapturedWorldIntermediate2x), 0, 0, 0, 1,
		PostPassVariant::SsaaFourToTwo,
		CompilerGraphPhaseInvocationRule::CaptureColorSsaaFourToTwo,
		{ RenderFormat::R8G8B8A8Unorm, RenderFormat::Count }, 1, 0 },
	{ GraphNodeId::CapWorld, 4, 6, CompilerGraphPhaseKind::SsaaDownsample,
		GraphResourceBit(GraphResource::SceneColor) |
		GraphResourceBit(GraphResource::CapturedWorldMsaaResolved) |
		GraphResourceBit(GraphResource::CapturedWorldIntermediate2x) |
		GraphResourceBit(GraphResource::CapturedMatrices),
		GraphResourceBit(GraphResource::CapturedWorldColor), 0, 0, 0, 1,
		PostPassVariant::SsaaTwoToOne,
		CompilerGraphPhaseInvocationRule::CaptureColorSsaaTwoToOne,
		{ RenderFormat::R8G8B8A8Unorm, RenderFormat::Count }, 1, 0 },
	{ GraphNodeId::CapWorld, 5, 6,
		CompilerGraphPhaseKind::AttachmentAlphaOnlyClear,
		GraphResourceBit(GraphResource::SceneColor),
		GraphResourceBit(GraphResource::SceneColor),
		GraphResourceBit(GraphResource::SceneColor),
		kWriteColor, kChannelAlpha, 1, PostPassVariant::Only,
		CompilerGraphPhaseInvocationRule::OncePerLogicalInvocation,
		{ RenderFormat::R8G8B8A8Unorm, RenderFormat::Count }, 1, 0 },

	{ GraphNodeId::CockpitResolve, 0, 6, CompilerGraphPhaseKind::SampledPostPass,
		GraphResourceBit(GraphResource::CockpitScene),
		GraphResourceBit(GraphResource::CockpitResolved), 0, 0, 0, 1,
		PostPassVariant::SingleSample,
		CompilerGraphPhaseInvocationRule::CockpitSingleSsaaOne,
		{ RenderFormat::R8G8B8A8Unorm, RenderFormat::Count }, 1, 0 },
	{ GraphNodeId::CockpitResolve, 1, 6, CompilerGraphPhaseKind::MultisampleResolve,
		GraphResourceBit(GraphResource::CockpitScene),
		GraphResourceBit(GraphResource::CockpitResolved), 0, 0, 0, 1,
		PostPassVariant::Multisample,
		CompilerGraphPhaseInvocationRule::CockpitMsaaSsaaOne,
		{ RenderFormat::R8G8B8A8Unorm, RenderFormat::Count }, 1, 0 },
	{ GraphNodeId::CockpitResolve, 2, 6, CompilerGraphPhaseKind::MultisampleResolve,
		GraphResourceBit(GraphResource::CockpitScene),
		GraphResourceBit(GraphResource::CockpitMsaaResolved), 0, 0, 0, 1,
		PostPassVariant::Multisample,
		CompilerGraphPhaseInvocationRule::CockpitMsaaBeforeSsaa,
		{ RenderFormat::R8G8B8A8Unorm, RenderFormat::Count }, 1, 0 },
	{ GraphNodeId::CockpitResolve, 3, 6, CompilerGraphPhaseKind::SsaaDownsample,
		GraphResourceBit(GraphResource::CockpitScene) |
		GraphResourceBit(GraphResource::CockpitMsaaResolved),
		GraphResourceBit(GraphResource::CockpitSsaaIntermediate2x), 0, 0, 0, 1,
		PostPassVariant::SsaaFourToTwo,
		CompilerGraphPhaseInvocationRule::CockpitSsaaFourToTwo,
		{ RenderFormat::R8G8B8A8Unorm, RenderFormat::Count }, 1, 0 },
	{ GraphNodeId::CockpitResolve, 4, 6, CompilerGraphPhaseKind::SsaaDownsample,
		GraphResourceBit(GraphResource::CockpitScene) |
		GraphResourceBit(GraphResource::CockpitMsaaResolved) |
		GraphResourceBit(GraphResource::CockpitSsaaIntermediate2x),
		GraphResourceBit(GraphResource::CockpitResolved), 0, 0, 0, 1,
		PostPassVariant::SsaaTwoToOne,
		CompilerGraphPhaseInvocationRule::CockpitSsaaTwoToOne,
		{ RenderFormat::R8G8B8A8Unorm, RenderFormat::Count }, 1, 0 },
	{ GraphNodeId::CockpitResolve, 5, 6,
		CompilerGraphPhaseKind::ResourceChannelAlias,
		GraphResourceBit(GraphResource::CockpitResolved),
		GraphResourceBit(GraphResource::CockpitAlpha), 0, 0, kChannelAlpha, 1,
		PostPassVariant::Only,
		CompilerGraphPhaseInvocationRule::CockpitAlphaAliasWhenBloom,
		{ RenderFormat::R8G8B8A8Unorm, RenderFormat::R8G8B8A8Unorm }, 1, 1 },

	{ GraphNodeId::BloomDeferred, 0, 3, CompilerGraphPhaseKind::BloomThreshold,
		GraphResourceBit(GraphResource::PostPresent) |
		GraphResourceBit(GraphResource::PostLogicalDepth) |
		GraphResourceBit(GraphResource::SceneProtectionMask) |
		GraphResourceBit(GraphResource::ResolvedProtectionMask) |
		GraphResourceBit(GraphResource::CockpitAlpha),
		GraphResourceBit(GraphResource::BloomCurrentLevel), 0, 0, 0, 1,
		PostPassVariant::BloomThresholdPhase,
		CompilerGraphPhaseInvocationRule::DeferredBloomThreshold,
		{ RenderFormat::R8G8B8A8Unorm, RenderFormat::Count }, 1, 0 },
	{ GraphNodeId::BloomDeferred, 1, 3, CompilerGraphPhaseKind::BloomDownsample,
		GraphResourceBit(GraphResource::BloomCurrentLevel),
		GraphResourceBit(GraphResource::BloomSmallerLevel), 0, 0, 0, 1,
		PostPassVariant::BloomDownsamplePhase,
		CompilerGraphPhaseInvocationRule::DeferredBloomDownLevels,
		{ RenderFormat::R8G8B8A8Unorm, RenderFormat::Count }, 1, 0 },
	{ GraphNodeId::BloomDeferred, 2, 3, CompilerGraphPhaseKind::BloomMerge,
		GraphResourceBit(GraphResource::BloomCurrentLevel) |
		GraphResourceBit(GraphResource::BloomSmallerLevel),
		GraphResourceBit(GraphResource::BloomMerged), 0, 0, 0, 1,
		PostPassVariant::BloomMergePhase,
		CompilerGraphPhaseInvocationRule::DeferredBloomMergeLevels,
		{ RenderFormat::R8G8B8A8Unorm, RenderFormat::Count }, 1, 0 },
};

const size_t kCompilerGraphPhaseContractCount =
	sizeof(kCompilerGraphPhaseContract) / sizeof(kCompilerGraphPhaseContract[0]);

uint32_t EvaluateCompilerGraphPhaseInvocationCount(
	const CompilerGraphPhaseContract &phase,
	const GraphEvaluationContext &context)
{
	const uint32_t bloom_levels = ComputeBloomPyramidLevelCount(
		context.bloom_source_width, context.bloom_source_height);
	switch (phase.invocation_rule)
	{
	case CompilerGraphPhaseInvocationRule::OncePerLogicalInvocation:
		return phase.node == GraphNodeId::CapWorld ? context.capture_call_count :
			EvaluateGraphNodeInvocationCount(phase.node, context);
	case CompilerGraphPhaseInvocationRule::CaptureColorSingleSsaaOne:
		return context.msaa_samples == 1 && context.ssaa_factor == 1 ?
			context.world_color_capture_call_count : 0;
	case CompilerGraphPhaseInvocationRule::CaptureColorMsaaSsaaOne:
		return context.msaa_samples > 1 && context.ssaa_factor == 1 ?
			context.world_color_capture_call_count : 0;
	case CompilerGraphPhaseInvocationRule::CaptureColorMsaaBeforeSsaa:
		return context.msaa_samples > 1 && context.ssaa_factor > 1 ?
			context.world_color_capture_call_count : 0;
	case CompilerGraphPhaseInvocationRule::CaptureColorSsaaFourToTwo:
		return context.ssaa_factor == 4 ? context.world_color_capture_call_count : 0;
	case CompilerGraphPhaseInvocationRule::CaptureColorSsaaTwoToOne:
		return context.ssaa_factor >= 2 ? context.world_color_capture_call_count : 0;
	case CompilerGraphPhaseInvocationRule::CockpitSingleSsaaOne:
		return context.msaa_samples == 1 && context.ssaa_factor == 1 ?
			context.cockpit_frame_count : 0;
	case CompilerGraphPhaseInvocationRule::CockpitMsaaSsaaOne:
		return context.msaa_samples > 1 && context.ssaa_factor == 1 ?
			context.cockpit_frame_count : 0;
	case CompilerGraphPhaseInvocationRule::CockpitMsaaBeforeSsaa:
		return context.msaa_samples > 1 && context.ssaa_factor > 1 ?
			context.cockpit_frame_count : 0;
	case CompilerGraphPhaseInvocationRule::CockpitSsaaFourToTwo:
		return context.ssaa_factor == 4 ? context.cockpit_frame_count : 0;
	case CompilerGraphPhaseInvocationRule::CockpitSsaaTwoToOne:
		return context.ssaa_factor >= 2 ? context.cockpit_frame_count : 0;
	case CompilerGraphPhaseInvocationRule::CockpitAlphaAliasWhenBloom:
		return context.cockpit_frame_count != 0 && context.bloom_enabled != 0 &&
			bloom_levels != 0 ? context.cockpit_frame_count : 0;
	case CompilerGraphPhaseInvocationRule::DeferredBloomThreshold:
		return context.cockpit_frame_count != 0 && context.bloom_enabled != 0 &&
			bloom_levels != 0 ? context.cockpit_frame_count : 0;
	case CompilerGraphPhaseInvocationRule::DeferredBloomDownLevels:
	case CompilerGraphPhaseInvocationRule::DeferredBloomMergeLevels:
		return context.cockpit_frame_count != 0 && context.bloom_enabled != 0 &&
			bloom_levels > 1 ? context.cockpit_frame_count * (bloom_levels - 1) : 0;
	case CompilerGraphPhaseInvocationRule::Count:
		break;
	}
	return 0;
}

PostUniformSourceSelector SelectCompilerGraphPhaseSource(
	const CompilerGraphPhaseContract &phase,
	const GraphEvaluationContext &context)
{
	switch (phase.invocation_rule)
	{
	case CompilerGraphPhaseInvocationRule::CaptureColorSsaaFourToTwo:
	case CompilerGraphPhaseInvocationRule::CockpitSsaaFourToTwo:
		return context.msaa_samples > 1 ?
			PostUniformSourceSelector::MsaaResolved2D :
			PostUniformSourceSelector::Primary2D;
	case CompilerGraphPhaseInvocationRule::CaptureColorSsaaTwoToOne:
	case CompilerGraphPhaseInvocationRule::CockpitSsaaTwoToOne:
		if (context.ssaa_factor == 4)
			return PostUniformSourceSelector::SsaaIntermediate2x2D;
		return context.msaa_samples > 1 ?
			PostUniformSourceSelector::MsaaResolved2D :
			PostUniformSourceSelector::Primary2D;
	default:
		return PostUniformSourceSelector::Primary2D;
	}
}

#undef POST_USAGE
#undef U

#undef ATTACHMENT_SET_LOAD
#undef WORLD_SET_LOAD
#undef WORLD_SET
#undef POST_SET_LOAD
#undef POST_SET
#undef POST_HEADER_OPTIONAL_MATRICES
#undef POST_HEADER_MATRICES
#undef POST_HEADER
#undef POST_SELECTED_OPTIONAL
#undef POST_SELECTED
#undef POST_RESOURCE
#undef POST_SAMPLERS
#undef POST_UNIFORMS_OPTIONAL_MATRICES
#undef POST_UNIFORMS_MATRICES
#undef POST_UNIFORMS

const SamplerContract kSamplerContract[] = {
	{ SamplerSemantic::GenericRepeatPointNoMip, SamplerAddressMode::Repeat, SamplerAddressMode::Repeat, SamplerFilterMode::Nearest, SamplerFilterMode::Nearest, SamplerMipMode::Disabled, 0 },
	{ SamplerSemantic::GenericRepeatLinearNoMip, SamplerAddressMode::Repeat, SamplerAddressMode::Repeat, SamplerFilterMode::Linear, SamplerFilterMode::Linear, SamplerMipMode::Disabled, 0 },
	{ SamplerSemantic::GenericRepeatPointMip, SamplerAddressMode::Repeat, SamplerAddressMode::Repeat, SamplerFilterMode::Nearest, SamplerFilterMode::Nearest, SamplerMipMode::Nearest, 0 },
	{ SamplerSemantic::GenericRepeatLinearMip, SamplerAddressMode::Repeat, SamplerAddressMode::Repeat, SamplerFilterMode::Linear, SamplerFilterMode::Linear, SamplerMipMode::Linear, 0 },
	{ SamplerSemantic::GenericClampPointNoMip, SamplerAddressMode::ClampToEdge, SamplerAddressMode::ClampToEdge, SamplerFilterMode::Nearest, SamplerFilterMode::Nearest, SamplerMipMode::Disabled, 0 },
	{ SamplerSemantic::GenericClampLinearNoMip, SamplerAddressMode::ClampToEdge, SamplerAddressMode::ClampToEdge, SamplerFilterMode::Linear, SamplerFilterMode::Linear, SamplerMipMode::Disabled, 0 },
	{ SamplerSemantic::GenericClampPointMip, SamplerAddressMode::ClampToEdge, SamplerAddressMode::ClampToEdge, SamplerFilterMode::Nearest, SamplerFilterMode::Nearest, SamplerMipMode::Nearest, 0 },
	{ SamplerSemantic::GenericClampLinearMip, SamplerAddressMode::ClampToEdge, SamplerAddressMode::ClampToEdge, SamplerFilterMode::Linear, SamplerFilterMode::Linear, SamplerMipMode::Linear, 0 },
	{ SamplerSemantic::GenericClampURepeatVPointNoMip, SamplerAddressMode::ClampToEdge, SamplerAddressMode::Repeat, SamplerFilterMode::Nearest, SamplerFilterMode::Nearest, SamplerMipMode::Disabled, 0 },
	{ SamplerSemantic::GenericClampURepeatVLinearNoMip, SamplerAddressMode::ClampToEdge, SamplerAddressMode::Repeat, SamplerFilterMode::Linear, SamplerFilterMode::Linear, SamplerMipMode::Disabled, 0 },
	{ SamplerSemantic::GenericClampURepeatVPointMip, SamplerAddressMode::ClampToEdge, SamplerAddressMode::Repeat, SamplerFilterMode::Nearest, SamplerFilterMode::Nearest, SamplerMipMode::Nearest, 0 },
	{ SamplerSemantic::GenericClampURepeatVLinearMip, SamplerAddressMode::ClampToEdge, SamplerAddressMode::Repeat, SamplerFilterMode::Linear, SamplerFilterMode::Linear, SamplerMipMode::Linear, 0 },
	{ SamplerSemantic::LightmapClamp, SamplerAddressMode::ClampToEdge, SamplerAddressMode::ClampToEdge, SamplerFilterMode::Linear, SamplerFilterMode::Linear, SamplerMipMode::Disabled, 0 },
	{ SamplerSemantic::LightmapRepeat, SamplerAddressMode::Repeat, SamplerAddressMode::Repeat, SamplerFilterMode::Linear, SamplerFilterMode::Linear, SamplerMipMode::Disabled, 0 },
	{ SamplerSemantic::Font, SamplerAddressMode::ClampToEdge, SamplerAddressMode::ClampToEdge, SamplerFilterMode::Linear, SamplerFilterMode::Linear, SamplerMipMode::Disabled, 0 },
	{ SamplerSemantic::PostNearest, SamplerAddressMode::ClampToEdge, SamplerAddressMode::ClampToEdge, SamplerFilterMode::Nearest, SamplerFilterMode::Nearest, SamplerMipMode::Disabled, 0 },
	{ SamplerSemantic::PostLinear, SamplerAddressMode::ClampToEdge, SamplerAddressMode::ClampToEdge, SamplerFilterMode::Linear, SamplerFilterMode::Linear, SamplerMipMode::Disabled, 0 },
	{ SamplerSemantic::GtaoNoise, SamplerAddressMode::Repeat, SamplerAddressMode::Repeat, SamplerFilterMode::Nearest, SamplerFilterMode::Nearest, SamplerMipMode::Disabled, 0 },
	{ SamplerSemantic::HistoryLinear, SamplerAddressMode::ClampToEdge, SamplerAddressMode::ClampToEdge, SamplerFilterMode::Linear, SamplerFilterMode::Linear, SamplerMipMode::Disabled, 0 },
	{ SamplerSemantic::BloomLinear, SamplerAddressMode::ClampToEdge, SamplerAddressMode::ClampToEdge, SamplerFilterMode::Linear, SamplerFilterMode::Linear, SamplerMipMode::Disabled, 0 },
	{ SamplerSemantic::MaskNearest, SamplerAddressMode::ClampToEdge, SamplerAddressMode::ClampToEdge, SamplerFilterMode::Nearest, SamplerFilterMode::Nearest, SamplerMipMode::Disabled, 0 },
	{ SamplerSemantic::DepthNearest, SamplerAddressMode::ClampToEdge, SamplerAddressMode::ClampToEdge, SamplerFilterMode::Nearest, SamplerFilterMode::Nearest, SamplerMipMode::Disabled, 0 },
};

const size_t kSamplerContractCount = sizeof(kSamplerContract) / sizeof(kSamplerContract[0]);

const PostSamplerSlotContract kPostSamplerSlots[] = {
	{ 0, SamplerSemantic::PostNearest },
	{ 1, SamplerSemantic::PostLinear },
	{ 2, SamplerSemantic::GtaoNoise },
	{ 3, SamplerSemantic::HistoryLinear },
	{ 4, SamplerSemantic::BloomLinear },
	{ 5, SamplerSemantic::MaskNearest },
	{ 6, SamplerSemantic::DepthNearest },
};

const size_t kPostSamplerSlotCount =
	sizeof(kPostSamplerSlots) / sizeof(kPostSamplerSlots[0]);

const GtaoNoiseTextureContract kGtaoNoiseTextureContract = {
	4, 4, 2, RenderFormat::R8G8Unorm, SamplerSemantic::GtaoNoise, 1
};

const uint8_t kGtaoNoiseRg8[32] = {
	141, 1, 180, 148, 60, 253, 250, 30,
	130, 142, 245, 142, 137, 84, 106, 234,
	186, 19, 2, 168, 109, 130, 149, 231,
	111, 158, 15, 30, 60, 202, 11, 157,
};

const size_t kGtaoNoiseRg8Size = sizeof(kGtaoNoiseRg8);

const AlphaTypeBlendContract kAlphaTypeBlendContract[] = {
	{ 0, BlendClass::Opaque, 0 }, { 2, BlendClass::Opaque, 0 },
	{ 1, BlendClass::Alpha, 0 }, { 3, BlendClass::Alpha, 0 },
	{ 7, BlendClass::Alpha, 0 }, { 6, BlendClass::Alpha, 0 },
	{ 5, BlendClass::Alpha, 0 }, { 4, BlendClass::Alpha, 0 },
	{ 9, BlendClass::Saturate, 1 }, { 33, BlendClass::Saturate, 1 },
	{ 12, BlendClass::Saturate, 1 }, { 13, BlendClass::Saturate, 1 },
	{ 14, BlendClass::Saturate, 1 }, { 32, BlendClass::Saturate, 0 },
	{ 8, BlendClass::Multiply, 0 }, { 15, BlendClass::Multiply, 0 },
};

const size_t kAlphaTypeBlendContractCount =
	sizeof(kAlphaTypeBlendContract) / sizeof(kAlphaTypeBlendContract[0]);

const TextureMappingInvalidationContract kTextureMappingInvalidationContract[] = {
	{ TextureMappingInvalidationReason::ResetCache, 1, 0, 1 },
	{ TextureMappingInvalidationReason::LevelUnload, 1, 0, 1 },
	{ TextureMappingInvalidationReason::DestroyedTexture, 1, 0, 1 },
	{ TextureMappingInvalidationReason::RendererRecreation, 1, 0, 1 },
};

const size_t kTextureMappingInvalidationContractCount =
	sizeof(kTextureMappingInvalidationContract) /
	sizeof(kTextureMappingInvalidationContract[0]);

const DescriptorPageContract kDescriptorPageContract = {
	1, kWorldArrayImageCount, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0
};

const AlphaTypeBlendContract *FindAlphaTypeBlendContract(uint32_t alpha_type)
{
	for (size_t i = 0; i < kAlphaTypeBlendContractCount; ++i)
		if (kAlphaTypeBlendContract[i].alpha_type == alpha_type)
			return &kAlphaTypeBlendContract[i];
	return nullptr;
}

const PostPassDescriptorSetContract *FindPostPassDescriptorSet(
	GraphNodeId node, PostPassVariant variant)
{
	for (size_t i = 0; i < kPostPassDescriptorSetCount; ++i)
	{
		const PostPassDescriptorSetContract &contract = kPostPassDescriptorSets[i];
		if (contract.node == node && contract.variant == variant)
			return &contract;
	}
	return nullptr;
}

namespace
{

bool DirectResourceKindMatches(const PostPassDescriptorBindingContract &binding)
{
	const bool multisample = binding.variant == PostPassVariant::Multisample;
	switch (binding.resource)
	{
	case GraphResource::SceneDepth:
		return binding.kind == (multisample ?
			PostDescriptorKind::SampledDepth2DMultisample :
			PostDescriptorKind::SampledDepth2D);
	case GraphResource::CapturedWorldDepth:
	case GraphResource::ResolvedDepth:
	case GraphResource::PostLogicalDepth:
	case GraphResource::SoftDepthSnapshot:
		return !multisample && binding.kind == PostDescriptorKind::SampledDepth2D;
	case GraphResource::SceneObjectId:
		return binding.kind == (multisample ?
			PostDescriptorKind::SampledUint2DMultisample :
			PostDescriptorKind::SampledUint2D);
	case GraphResource::ResolvedObjectId:
		return !multisample && binding.kind == PostDescriptorKind::SampledUint2D;
	case GraphResource::CapturedMatrices:
	case GraphResource::Swapchain:
	case GraphResource::Count:
		return false;
	default:
		return binding.kind == (multisample ?
			PostDescriptorKind::SampledFloat2DMultisample :
			PostDescriptorKind::SampledFloat2D);
	}
}

bool SelectedResourceKindMatches(const PostPassDescriptorBindingContract &binding)
{
	switch (binding.selected_input)
	{
	case GraphInputSemantic::PostDepthSource:
		return binding.kind == PostDescriptorKind::SampledDepth2D;
	case GraphInputSemantic::ObjectIdSource:
		return binding.kind == PostDescriptorKind::SampledUint2D;
	case GraphInputSemantic::None:
	case GraphInputSemantic::Count:
		return false;
	default:
		return binding.kind == PostDescriptorKind::SampledFloat2D;
	}
}

GraphResourceMask SelectedInputResourceDomain(GraphInputSemantic semantic)
{
	switch (semantic)
	{
	case GraphInputSemantic::SceneColorAfterMsaa:
		return GraphResourceBit(GraphResource::SceneColor) |
			GraphResourceBit(GraphResource::ResolvedColor);
	case GraphInputSemantic::SsaaTwoToOneColor:
		return GraphResourceBit(GraphResource::SceneColor) |
			GraphResourceBit(GraphResource::ResolvedColor) |
			GraphResourceBit(GraphResource::SsaaIntermediate2x);
	case GraphInputSemantic::AuthoredBaseColor:
		return GraphResourceBit(GraphResource::SceneColor) |
			GraphResourceBit(GraphResource::ResolvedColor) |
			GraphResourceBit(GraphResource::LogicalAuthoredColor);
	case GraphInputSemantic::FinalAuthoredColor:
		return GraphResourceBit(GraphResource::SceneColor) |
			GraphResourceBit(GraphResource::ResolvedColor) |
			GraphResourceBit(GraphResource::LogicalAuthoredColor) |
			GraphResourceBit(GraphResource::GtaoApplied) |
			GraphResourceBit(GraphResource::GtaoDeferredComposite);
	case GraphInputSemantic::PostDepthSource:
		return GraphResourceBit(GraphResource::SceneDepth) |
			GraphResourceBit(GraphResource::ResolvedDepth) |
			GraphResourceBit(GraphResource::CapturedWorldDepth);
	case GraphInputSemantic::VelocitySource:
		return GraphResourceBit(GraphResource::SceneVelocity) |
			GraphResourceBit(GraphResource::ResolvedVelocity);
	case GraphInputSemantic::ObjectIdSource:
		return GraphResourceBit(GraphResource::SceneObjectId) |
			GraphResourceBit(GraphResource::ResolvedObjectId);
	case GraphInputSemantic::ProtectionMaskSource:
		return GraphResourceBit(GraphResource::SceneProtectionMask) |
			GraphResourceBit(GraphResource::ResolvedProtectionMask);
	case GraphInputSemantic::AoClassSource:
		return GraphResourceBit(GraphResource::SceneAoClass) |
			GraphResourceBit(GraphResource::ResolvedAoClass);
	case GraphInputSemantic::AoPreTemporalSource:
	case GraphInputSemantic::AoFinalSource:
		return GraphResourceBit(GraphResource::GtaoRaw) |
			GraphResourceBit(GraphResource::GtaoCurrent);
	case GraphInputSemantic::TemporalHistorySource:
		return GraphResourceBit(GraphResource::GtaoHistoryPrevious);
	case GraphInputSemantic::None:
	case GraphInputSemantic::Count:
		return 0;
	}
	return 0;
}

bool IsOptionalDirectGraphResource(GraphNodeId node, PostPassVariant variant,
	GraphResource resource)
{
	return (node == GraphNodeId::BloomThreshold && resource == GraphResource::CockpitAlpha) ||
		(node == GraphNodeId::AoTemporal && resource == GraphResource::PostLogicalDepth) ||
		(node == GraphNodeId::CapWorld &&
		 (variant == PostPassVariant::SsaaFourToTwo ||
		  variant == PostPassVariant::SsaaTwoToOne) &&
		 (resource == GraphResource::SceneColor ||
		  resource == GraphResource::CapturedWorldMsaaResolved ||
		  resource == GraphResource::CapturedWorldIntermediate2x)) ||
		(node == GraphNodeId::CockpitResolve &&
		 (variant == PostPassVariant::SsaaFourToTwo ||
		  variant == PostPassVariant::SsaaTwoToOne) &&
		 (resource == GraphResource::CockpitScene ||
		  resource == GraphResource::CockpitMsaaResolved ||
		  resource == GraphResource::CockpitSsaaIntermediate2x)) ||
		((node == GraphNodeId::MotionDebugNormal ||
		  node == GraphNodeId::MotionDebugCockpitPre) &&
		 resource == GraphResource::PostLogicalDepth);
}

bool IsOptionalSelectedGraphInput(GraphNodeId node, GraphInputSemantic input)
{
	return input == GraphInputSemantic::TemporalHistorySource ||
		((node == GraphNodeId::MotionDebugNormal ||
		  node == GraphNodeId::MotionDebugCockpitPre) &&
		 input == GraphInputSemantic::ObjectIdSource);
}

uint32_t ExpectedVariantMask(GraphNodeId node)
{
	const uint32_t only = 1u << static_cast<uint32_t>(PostPassVariant::Only);
	const uint32_t single = 1u << static_cast<uint32_t>(PostPassVariant::SingleSample);
	const uint32_t multisample = 1u << static_cast<uint32_t>(PostPassVariant::Multisample);
	const uint32_t ssaa_four = 1u << static_cast<uint32_t>(PostPassVariant::SsaaFourToTwo);
	const uint32_t ssaa_two = 1u << static_cast<uint32_t>(PostPassVariant::SsaaTwoToOne);
	const uint32_t bloom_threshold =
		1u << static_cast<uint32_t>(PostPassVariant::BloomThresholdPhase);
	const uint32_t bloom_down =
		1u << static_cast<uint32_t>(PostPassVariant::BloomDownsamplePhase);
	const uint32_t bloom_merge =
		1u << static_cast<uint32_t>(PostPassVariant::BloomMergePhase);
	switch (node)
	{
	case GraphNodeId::CapWorld:
		return single | multisample | ssaa_four | ssaa_two;
	case GraphNodeId::CapDepthLogical:
		return single | multisample;
	case GraphNodeId::CockpitResolve:
		return single | multisample | ssaa_four | ssaa_two;
	case GraphNodeId::BloomDeferred:
		return bloom_threshold | bloom_down | bloom_merge;
	case GraphNodeId::ResolveColor:
	case GraphNodeId::ResolveDepth:
	case GraphNodeId::ResolveVelocity:
	case GraphNodeId::ResolveObjectId:
	case GraphNodeId::ResolveProtectionMask:
	case GraphNodeId::ResolveAoClass:
		return multisample;
	default:
		return only;
	}
}

GraphResourceMask ExpectedDescriptorInputDomain(GraphNodeId node,
	PostPassVariant variant)
{
	#define DR(resource) GraphResourceBit(GraphResource::resource)
	if (node == GraphNodeId::CapWorld)
	{
		if (variant == PostPassVariant::SingleSample ||
			variant == PostPassVariant::Multisample)
			return DR(SceneColor) | DR(CapturedMatrices);
		if (variant == PostPassVariant::SsaaFourToTwo)
			return DR(SceneColor) | DR(CapturedWorldMsaaResolved) |
				DR(CapturedMatrices);
		if (variant == PostPassVariant::SsaaTwoToOne)
			return DR(SceneColor) | DR(CapturedWorldMsaaResolved) |
				DR(CapturedWorldIntermediate2x) | DR(CapturedMatrices);
	}
	if (node == GraphNodeId::CockpitResolve)
	{
		if (variant == PostPassVariant::SingleSample ||
			variant == PostPassVariant::Multisample)
			return DR(CockpitScene);
		if (variant == PostPassVariant::SsaaFourToTwo)
			return DR(CockpitScene) | DR(CockpitMsaaResolved);
		if (variant == PostPassVariant::SsaaTwoToOne)
			return DR(CockpitScene) | DR(CockpitMsaaResolved) |
				DR(CockpitSsaaIntermediate2x);
	}
	if (node == GraphNodeId::BloomDeferred)
	{
		if (variant == PostPassVariant::BloomThresholdPhase)
			return DR(PostPresent) | DR(PostLogicalDepth) |
				DR(SceneProtectionMask) | DR(ResolvedProtectionMask) |
				DR(CockpitAlpha);
		if (variant == PostPassVariant::BloomDownsamplePhase)
			return DR(BloomCurrentLevel);
		if (variant == PostPassVariant::BloomMergePhase)
			return DR(BloomCurrentLevel) | DR(BloomSmallerLevel);
	}
	#undef DR
	return kFrozenGraphNodeContract[static_cast<size_t>(node)].inputs;
}

} // namespace

bool ValidatePostPassDescriptorContract()
{
	if (kPostUniformFieldLayoutContractCount !=
		static_cast<size_t>(PostUniformField::Count))
		return false;
	uint32_t next_uniform_offset = 0;
	for (size_t i = 0; i < kPostUniformFieldLayoutContractCount; ++i)
	{
		const PostUniformFieldLayoutContract &field =
			kPostUniformFieldLayoutContract[i];
		if (static_cast<size_t>(field.field) != i ||
			field.byte_offset != next_uniform_offset ||
			(field.byte_offset & 15u) != 0 ||
			(field.byte_size != 16 && field.byte_size != 64) ||
			static_cast<uint32_t>(field.scalar_kind) >=
				static_cast<uint32_t>(PostUniformScalarKind::Count))
			return false;
		next_uniform_offset += field.byte_size;
	}
	if (next_uniform_offset != sizeof(PostPassUniforms))
		return false;
	if (kPostPassUniformUsageContractCount !=
		static_cast<size_t>(GraphNodeId::Count))
		return false;
	const PostUniformFieldMask all_uniform_fields =
		(PostUniformFieldMask(1) << static_cast<uint32_t>(PostUniformField::Count)) - 1;
	for (size_t i = 0; i < kPostPassUniformUsageContractCount; ++i)
	{
		const PostPassUniformUsageContract &usage = kPostPassUniformUsageContract[i];
		if (static_cast<size_t>(usage.node) != i ||
			((usage.required_fields | usage.conditional_fields) & ~all_uniform_fields) != 0 ||
			(usage.required_fields & usage.conditional_fields) != 0)
			return false;
	}
	if (kCompilerGraphPhaseContractCount != 15)
		return false;
	for (size_t i = 0; i < kCompilerGraphPhaseContractCount; ++i)
	{
		const CompilerGraphPhaseContract &phase = kCompilerGraphPhaseContract[i];
		const uint32_t node = static_cast<uint32_t>(phase.node);
		if (node >= static_cast<uint32_t>(GraphNodeId::Count) ||
			phase.phase_count == 0 || phase.phase_index >= phase.phase_count ||
			static_cast<uint32_t>(phase.kind) >=
				static_cast<uint32_t>(CompilerGraphPhaseKind::Count) ||
			static_cast<uint32_t>(phase.invocation_rule) >=
				static_cast<uint32_t>(CompilerGraphPhaseInvocationRule::Count) ||
			static_cast<uint32_t>(phase.descriptor_variant) >=
				static_cast<uint32_t>(PostPassVariant::Count) ||
			(phase.inputs & ~kFrozenGraphNodeContract[node].inputs) != 0 ||
			(phase.outputs & ~kFrozenGraphNodeContract[node].outputs) != 0 ||
			(phase.attachment_load_inputs & ~phase.inputs) != 0 ||
			phase.output_location_count > 2 ||
			phase.output_alpha_channel_alias > 1)
			return false;
		for (uint32_t output = 0; output < phase.output_location_count; ++output)
			if (phase.output_formats[output] == RenderFormat::Count)
				return false;
		if (phase.kind == CompilerGraphPhaseKind::AttachmentAlphaOnlyClear)
		{
			if (phase.attachment_load_inputs !=
					GraphResourceBit(GraphResource::SceneColor) ||
				phase.selected_attachments != kWriteColor ||
				phase.color_channel_mask != kChannelAlpha)
				return false;
		}
		else if (phase.kind == CompilerGraphPhaseKind::ResourceChannelAlias)
		{
			if (phase.output_alpha_channel_alias != 1 ||
				phase.color_channel_mask != kChannelAlpha ||
				phase.output_location_count != 1)
				return false;
		}
		else if (FindPostPassDescriptorSet(phase.node,
			phase.descriptor_variant) == nullptr)
			return false;
		if (i != 0 && kCompilerGraphPhaseContract[i - 1].node == phase.node)
		{
			if (phase.phase_index !=
				kCompilerGraphPhaseContract[i - 1].phase_index + 1 ||
				phase.phase_count != kCompilerGraphPhaseContract[i - 1].phase_count)
				return false;
		}
		else if (phase.phase_index != 0)
			return false;
	}
	if (kPostSamplerSlotCount != kPostSamplerTableSize)
		return false;
	const SamplerSemantic expected_samplers[kPostSamplerTableSize] = {
		SamplerSemantic::PostNearest, SamplerSemantic::PostLinear,
		SamplerSemantic::GtaoNoise, SamplerSemantic::HistoryLinear,
		SamplerSemantic::BloomLinear, SamplerSemantic::MaskNearest,
		SamplerSemantic::DepthNearest,
	};
	for (size_t i = 0; i < kPostSamplerSlotCount; ++i)
	{
		if (kPostSamplerSlots[i].slot != i ||
			kPostSamplerSlots[i].semantic != expected_samplers[i])
			return false;
	}

	uint32_t variants[static_cast<size_t>(GraphNodeId::Count)] = {};
	for (size_t i = 0; i < kPostPassDescriptorSetCount; ++i)
	{
		const PostPassDescriptorSetContract &set = kPostPassDescriptorSets[i];
		const uint32_t node = static_cast<uint32_t>(set.node);
		const uint32_t variant = static_cast<uint32_t>(set.variant);
		if (node >= static_cast<uint32_t>(GraphNodeId::Count) ||
			variant >= static_cast<uint32_t>(PostPassVariant::Count) ||
			static_cast<uint32_t>(set.use) >=
			static_cast<uint32_t>(PostPassDescriptorUse::Count))
			return false;
		const GraphResourceMask expected_inputs =
			ExpectedDescriptorInputDomain(set.node, set.variant);
		if ((expected_inputs & ~kFrozenGraphNodeContract[node].inputs) != 0 ||
			(set.attachment_load_inputs & ~expected_inputs) != 0)
			return false;
		const uint32_t variant_bit = 1u << variant;
		if ((variants[node] & variant_bit) != 0)
			return false;
		variants[node] |= variant_bit;

		uint32_t actual_binding_count = 0;
		uint64_t binding_mask = 0;
		GraphResourceMask covered_inputs = set.attachment_load_inputs;
		GraphResourceMask sampled_inputs = 0;
		for (size_t j = 0; j < kPostPassDescriptorBindingCount; ++j)
		{
			const PostPassDescriptorBindingContract &binding =
				kPostPassDescriptorBindings[j];
			if (binding.node != set.node || binding.variant != set.variant)
				continue;
			if (binding.set != kPostDescriptorSet || binding.binding >= 64 ||
				binding.count == 0 ||
				static_cast<uint32_t>(binding.kind) >=
				static_cast<uint32_t>(PostDescriptorKind::Count) ||
				static_cast<uint32_t>(binding.requirement) >=
				static_cast<uint32_t>(PostDescriptorRequirement::Count) ||
				static_cast<uint32_t>(binding.semantic) >=
				static_cast<uint32_t>(PostDescriptorResourceSemantic::Count))
				return false;
			const uint64_t bit = uint64_t(1) << binding.binding;
			if ((binding_mask & bit) != 0)
				return false;
			binding_mask |= bit;
			++actual_binding_count;

			switch (binding.semantic)
			{
			case PostDescriptorResourceSemantic::PassUniforms:
				if (binding.binding != kPostUniformBinding ||
					binding.kind != PostDescriptorKind::UniformBuffer ||
					binding.count != 1 ||
					(binding.resource != GraphResource::Count &&
					 binding.resource != GraphResource::CapturedMatrices) ||
					binding.selected_input != GraphInputSemantic::None ||
					(binding.resource != GraphResource::Count &&
					 (kFrozenGraphNodeContract[node].inputs &
					  GraphResourceBit(binding.resource)) == 0) ||
					binding.requirement != PostDescriptorRequirement::Required)
					return false;
				if (binding.resource != GraphResource::Count)
					covered_inputs |= GraphResourceBit(binding.resource);
				break;
			case PostDescriptorResourceSemantic::FixedSamplerTable:
				if (binding.binding != kPostSamplerTableBinding ||
					binding.kind != PostDescriptorKind::Sampler ||
					binding.count != kPostSamplerTableSize ||
					binding.requirement != PostDescriptorRequirement::Required ||
					binding.resource != GraphResource::Count ||
					binding.selected_input != GraphInputSemantic::None)
					return false;
				break;
			case PostDescriptorResourceSemantic::GraphResource:
				if (binding.binding < kPostFirstImageBinding || binding.count != 1 ||
					binding.selected_input != GraphInputSemantic::None ||
					!DirectResourceKindMatches(binding) ||
					(kFrozenGraphNodeContract[node].inputs &
					 GraphResourceBit(binding.resource)) == 0 ||
					(binding.requirement == PostDescriptorRequirement::Optional) !=
					 IsOptionalDirectGraphResource(binding.node, binding.variant,
						 binding.resource))
					return false;
				covered_inputs |= GraphResourceBit(binding.resource);
				sampled_inputs |= GraphResourceBit(binding.resource);
				break;
			case PostDescriptorResourceSemantic::SelectedGraphInput:
				if (binding.binding < kPostFirstImageBinding || binding.count != 1 ||
					binding.resource != GraphResource::Count ||
					!SelectedResourceKindMatches(binding) ||
					(SelectedInputResourceDomain(binding.selected_input) &
					 ~kFrozenGraphNodeContract[node].inputs) != 0 ||
					(binding.requirement == PostDescriptorRequirement::Optional) !=
					 IsOptionalSelectedGraphInput(binding.node, binding.selected_input))
					return false;
				covered_inputs |= SelectedInputResourceDomain(binding.selected_input);
				sampled_inputs |= SelectedInputResourceDomain(binding.selected_input);
				break;
			default:
				return false;
			}
		}

		if (actual_binding_count != set.binding_count ||
			(sampled_inputs & set.attachment_load_inputs) != 0)
			return false;
		if (covered_inputs != expected_inputs)
			return false;
		if (set.use == PostPassDescriptorUse::ImmutablePostSet)
		{
			if (set.binding_count < kPostFirstImageBinding + 1 ||
				set.uniform_record_size != sizeof(PostPassUniforms) ||
				set.immutable_per_invocation != 1 ||
				set.ordinary_sampled_images_only != 1 ||
				set.optional_slots_always_valid != 1 ||
				binding_mask != ((uint64_t(1) << set.binding_count) - 1))
				return false;
		}
		else if (set.binding_count != 0 || set.uniform_record_size != 0 ||
			set.immutable_per_invocation != 0 ||
			set.ordinary_sampled_images_only != 0 ||
			set.optional_slots_always_valid != 0 || binding_mask != 0)
			return false;
	}

	for (uint32_t node = 0; node < static_cast<uint32_t>(GraphNodeId::Count); ++node)
	{
		if (variants[node] != ExpectedVariantMask(static_cast<GraphNodeId>(node)))
			return false;
		bool immutable_post_set = false;
		for (size_t set_index = 0; set_index < kPostPassDescriptorSetCount; ++set_index)
			if (static_cast<uint32_t>(kPostPassDescriptorSets[set_index].node) == node &&
				kPostPassDescriptorSets[set_index].use ==
					PostPassDescriptorUse::ImmutablePostSet)
				immutable_post_set = true;
		const PostPassUniformUsageContract &usage =
			kPostPassUniformUsageContract[node];
		if (immutable_post_set)
		{
			const PostUniformFieldMask common =
				PostUniformBit(PostUniformField::SourceExtent) |
				PostUniformBit(PostUniformField::DestinationExtent) |
				PostUniformBit(PostUniformField::FrameBranch);
			if ((usage.required_fields & common) != common)
				return false;
		}
		else if (usage.required_fields != 0 || usage.conditional_fields != 0)
			return false;
	}

	for (size_t i = 0; i < kPostPassDescriptorBindingCount; ++i)
	{
		const PostPassDescriptorBindingContract &binding =
			kPostPassDescriptorBindings[i];
		if (FindPostPassDescriptorSet(binding.node, binding.variant) == nullptr)
			return false;
	}
	return true;
}

TextureVersionBindDecision EvaluateTextureVersionBind(
	const TextureVersionBindInput &input)
{
	TextureVersionBindDecision decision = {};
	const uint32_t dirty = input.dirty_flags & kTextureDirtyMask;
	const bool has_mapping = input.has_logical_mapping != 0;
	const bool identity_matches = input.mapped_identity_matches != 0;
	const bool needs_snapshot = dirty != 0 || !has_mapping || !identity_matches;

	if (!needs_snapshot)
	{
		decision.reuse_mapped_logical_version = 1;
		return decision;
	}

	decision.snapshot_cpu_pixels_now = 1;
	decision.create_new_logical_version = 1;
	decision.attach_new_version_to_subsequent_draws = 1;
	decision.clear_dirty_flags_now = dirty;
	decision.invalidate_stale_mapping = has_mapping && !identity_matches;

	const bool unfinished = has_mapping &&
		input.mapped_version_last_use_timeline > input.completed_timeline;
	const bool referenced_earlier = has_mapping &&
		input.mapped_version_referenced_earlier_in_segment != 0;
	decision.copy_on_write_image_required = unfinished || referenced_earlier;
	decision.may_recycle_completed_image = has_mapping &&
		!decision.copy_on_write_image_required;
	return decision;
}

namespace
{

uint32_t TextureUploadBytesPerTexel(RenderFormat format)
{
	switch (format)
	{
	case RenderFormat::R8G8B8A8Unorm:
	case RenderFormat::R16G16Sfloat:
	case RenderFormat::R32Uint:
	case RenderFormat::D32Sfloat:
		return 4;
	case RenderFormat::R8G8Unorm:
		return 2;
	case RenderFormat::R8Unorm:
		return 1;
	case RenderFormat::R32G32Sfloat:
	case RenderFormat::R16G16B16A16Sfloat:
		return 8;
	default:
		return 0;
	}
}

uint32_t MaximumTextureMipCount(uint32_t width, uint32_t height)
{
	uint32_t mip_count = 1;
	while (width > 1 || height > 1)
	{
		width = width > 1 ? width / 2 : 1;
		height = height > 1 ? height / 2 : 1;
		++mip_count;
	}
	return mip_count;
}

bool MultiplyOverflows(uint64_t left, uint64_t right, uint64_t *product)
{
	if (left != 0 && right > UINT64_MAX / left)
		return true;
	*product = left * right;
	return false;
}

TextureUploadLayoutResult ComputeTextureUploadLayout(
	const TextureUploadLayoutInput &input)
{
	TextureUploadLayoutResult result = {};
	const uint32_t bytes_per_texel = TextureUploadBytesPerTexel(input.format);
	if (bytes_per_texel == 0)
	{
		result.error = TextureUploadLayoutError::InvalidFormat;
		return result;
	}
	if (input.width == 0 || input.height == 0 || input.layer_count == 0)
	{
		result.error = TextureUploadLayoutError::InvalidExtentOrLayerCount;
		return result;
	}
	if (input.mip_count == 0 || input.mip_count >
		MaximumTextureMipCount(input.width, input.height))
	{
		result.error = TextureUploadLayoutError::InvalidMipCount;
		return result;
	}
	if (input.payload_alignment != kCapturedTextureUploadPayloadAlignment)
	{
		result.error = TextureUploadLayoutError::InvalidPayloadAlignment;
		return result;
	}

	const uint64_t count = uint64_t(input.mip_count) * input.layer_count;
	if (count > UINT32_MAX)
	{
		result.error = TextureUploadLayoutError::SubresourceCountOverflow;
		return result;
	}
	result.subresource_count = static_cast<uint32_t>(count);

	uint32_t width = input.width;
	uint32_t height = input.height;
	uint64_t total = 0;
	for (uint32_t mip = 0; mip < input.mip_count; ++mip)
	{
		uint64_t row_pitch = 0;
		uint64_t layer_size = 0;
		uint64_t level_size = 0;
		if (MultiplyOverflows(width, bytes_per_texel, &row_pitch) ||
			MultiplyOverflows(row_pitch, height, &layer_size) ||
			MultiplyOverflows(layer_size, input.layer_count, &level_size) ||
			level_size > UINT64_MAX - total)
		{
			result.error = TextureUploadLayoutError::ByteSizeOverflow;
			return result;
		}
		total += level_size;
		width = width > 1 ? width / 2 : 1;
		height = height > 1 ? height / 2 : 1;
	}
	result.total_byte_size = total;
	if ((total % input.payload_alignment) != 0)
		result.error = TextureUploadLayoutError::PayloadSizeAlignmentMismatch;
	return result;
}

} // namespace

TextureUploadLayoutInput MakeCapturedTextureUploadLayoutInput(
	const CapturedTextureVersion &version)
{
	TextureUploadLayoutInput input = {};
	input.format = version.format;
	input.width = version.width;
	input.height = version.height;
	input.layer_count = version.depth_or_layers;
	input.mip_count = version.mip_count;
	input.payload_alignment = kCapturedTextureUploadPayloadAlignment;
	return input;
}

TextureUploadLayoutResult BuildCapturedTextureUploadLayout(
	const TextureUploadLayoutInput &input,
	TextureUploadSubresourceLayout *subresources,
	uint32_t subresource_capacity)
{
	TextureUploadLayoutResult result = ComputeTextureUploadLayout(input);
	if (result.error != TextureUploadLayoutError::None)
		return result;
	if (subresources == nullptr)
	{
		result.error = TextureUploadLayoutError::NullLayout;
		return result;
	}
	if (subresource_capacity < result.subresource_count)
	{
		result.error = TextureUploadLayoutError::InsufficientOutputCapacity;
		return result;
	}

	const uint32_t bytes_per_texel = TextureUploadBytesPerTexel(input.format);
	uint32_t width = input.width;
	uint32_t height = input.height;
	uint64_t offset = 0;
	uint32_t output_index = 0;
	for (uint32_t mip = 0; mip < input.mip_count; ++mip)
	{
		const uint64_t row_pitch = uint64_t(width) * bytes_per_texel;
		const uint64_t layer_size = row_pitch * height;
		for (uint32_t layer = 0; layer < input.layer_count; ++layer)
		{
			TextureUploadSubresourceLayout &layout = subresources[output_index++];
			layout.mip_level = mip;
			layout.array_layer = layer;
			layout.width = width;
			layout.height = height;
			layout.byte_offset = offset;
			layout.row_pitch_bytes = row_pitch;
			layout.byte_size = layer_size;
			offset += layer_size;
		}
		width = width > 1 ? width / 2 : 1;
		height = height > 1 ? height / 2 : 1;
	}
	return result;
}

TextureUploadLayoutError ValidateCapturedTextureUploadLayout(
	const TextureUploadLayoutInput &input,
	const TextureUploadSubresourceLayout *subresources,
	uint32_t subresource_count, uint64_t payload_byte_size)
{
	const TextureUploadLayoutResult expected = ComputeTextureUploadLayout(input);
	if (expected.error != TextureUploadLayoutError::None)
		return expected.error;
	if (subresources == nullptr)
		return TextureUploadLayoutError::NullLayout;
	if (subresource_count != expected.subresource_count)
		return TextureUploadLayoutError::NonCanonicalSubresource;
	if (payload_byte_size != expected.total_byte_size)
		return TextureUploadLayoutError::PayloadSizeMismatch;

	const uint32_t bytes_per_texel = TextureUploadBytesPerTexel(input.format);
	uint32_t width = input.width;
	uint32_t height = input.height;
	uint64_t offset = 0;
	uint32_t input_index = 0;
	for (uint32_t mip = 0; mip < input.mip_count; ++mip)
	{
		const uint64_t row_pitch = uint64_t(width) * bytes_per_texel;
		const uint64_t layer_size = row_pitch * height;
		for (uint32_t layer = 0; layer < input.layer_count; ++layer)
		{
			const TextureUploadSubresourceLayout &layout =
				subresources[input_index++];
			if (layout.mip_level != mip || layout.array_layer != layer ||
				layout.width != width || layout.height != height ||
				layout.byte_offset != offset ||
				layout.row_pitch_bytes != row_pitch ||
				layout.byte_size != layer_size ||
				(layout.byte_offset % bytes_per_texel) != 0)
				return TextureUploadLayoutError::NonCanonicalSubresource;
			offset += layer_size;
		}
		width = width > 1 ? width / 2 : 1;
		height = height > 1 ? height / 2 : 1;
	}
	return TextureUploadLayoutError::None;
}

uint32_t SelectDescriptorPageTier(uint32_t supported_float_2d_images)
{
	uint32_t selected = 0;
	for (size_t i = 0; i < sizeof(kDescriptorPageTiers) / sizeof(kDescriptorPageTiers[0]); ++i)
	{
		if (kDescriptorPageTiers[i] <= supported_float_2d_images)
			selected = kDescriptorPageTiers[i];
	}
	return selected;
}

uint32_t SelectSupportedMsaa(uint32_t normalized_request, uint32_t supported_sample_count_mask)
{
	const uint32_t candidates[] = { 8, 4, 2 };
	for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i)
	{
		const uint32_t samples = candidates[i];
		if (samples <= normalized_request && (supported_sample_count_mask & samples) != 0)
			return samples;
	}
	return 0;
}

static_assert(sizeof(kRequiredDeviceFeatures) / sizeof(kRequiredDeviceFeatures[0]) ==
	static_cast<size_t>(RequiredDeviceFeature::Count), "device feature contract mismatch");
static_assert(sizeof(kRequiredDeviceLimits) / sizeof(kRequiredDeviceLimits[0]) ==
	static_cast<size_t>(DeviceLimit::Count), "device limit contract mismatch");
static_assert(sizeof(kRequiredFormats) / sizeof(kRequiredFormats[0]) ==
	static_cast<size_t>(FormatSemantic::Count), "format contract mismatch");
static_assert(sizeof(kSamplerContract) / sizeof(kSamplerContract[0]) ==
	static_cast<size_t>(SamplerSemantic::Count), "sampler contract mismatch");
static_assert(sizeof(kPostSamplerSlots) / sizeof(kPostSamplerSlots[0]) ==
	kPostSamplerTableSize, "post sampler table mismatch");
static_assert(sizeof(kPostPassDescriptorSets) / sizeof(kPostPassDescriptorSets[0]) ==
	static_cast<size_t>(GraphNodeId::Count) + 9,
	"post descriptor manifest must cover every graph node and sample-type variant");
static_assert(sizeof(kTextureMappingInvalidationContract) /
	sizeof(kTextureMappingInvalidationContract[0]) ==
	static_cast<size_t>(TextureMappingInvalidationReason::Count),
	"texture mapping invalidation contract mismatch");

} // namespace render
} // namespace piccu
