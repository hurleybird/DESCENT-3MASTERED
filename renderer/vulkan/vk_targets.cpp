#include "vk_targets.h"

#include "../core/render_device_contract.h"

#include <algorithm>
#include <limits>

namespace piccu
{
namespace render
{
namespace vk
{

namespace
{

VkSampleCountFlagBits SampleBits(uint32_t samples)
{
	switch (samples)
	{
	case 8: return VK_SAMPLE_COUNT_8_BIT;
	case 4: return VK_SAMPLE_COUNT_4_BIT;
	case 2: return VK_SAMPLE_COUNT_2_BIT;
	default: return VK_SAMPLE_COUNT_1_BIT;
	}
}

uint32_t SafeCeilScale(uint32_t value, uint32_t percent, uint32_t factor)
{
	const uint64_t overscanned =
		(uint64_t(value) * percent + 99u) / 100u;
	const uint64_t scaled = overscanned * factor;
	return scaled > UINT32_MAX ? 0u : static_cast<uint32_t>(scaled);
}

VkImageUsageFlags ColorUsage()
{
	return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
}

VkImageUsageFlags DepthUsage()
{
	return VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
		VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT;
}

} // namespace

TargetManager::TargetManager()
	: allocator_(nullptr), state_tracker_(nullptr),
	  supported_sample_mask_(VK_SAMPLE_COUNT_1_BIT),
	  next_generation_(0), initialized_(false)
{
}

TargetManager::~TargetManager()
{
	Shutdown(false);
}

bool TargetManager::Initialize(ResourceAllocator *allocator,
	ResourceStateTracker *state_tracker, uint32_t supported_sample_mask)
{
	if (initialized_ || !allocator || !allocator->Ready() || !state_tracker)
		return false;
	allocator_ = allocator;
	state_tracker_ = state_tracker;
	supported_sample_mask_ = supported_sample_mask | VK_SAMPLE_COUNT_1_BIT;
	initialized_ = true;
	return true;
}

void TargetManager::Shutdown(bool device_lost) noexcept
{
	if (!initialized_)
		return;
	if (!device_lost)
	{
		RetireGeneration(&active_, active_.generation ?
			std::numeric_limits<uint64_t>::max() : 0);
		// The frame scheduler waits the final renderer timeline before target
		// shutdown, so all remaining versions are now reclaimable.
		allocator_->Reclaim(std::numeric_limits<uint64_t>::max());
	}
	else
	{
		UnregisterGenerationState(&active_);
		active_ = GenerationState();
	}
	allocator_ = nullptr;
	state_tracker_ = nullptr;
	initialized_ = false;
	next_generation_ = 0;
}

bool TargetManager::Ready() const noexcept
{
	return initialized_ && allocator_ && allocator_->Ready() &&
		active_.generation != 0;
}

VkFormat TargetManager::VulkanFormat(RenderFormat format)
{
	switch (format)
	{
	case RenderFormat::R8G8B8A8Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
	case RenderFormat::R16G16Sfloat: return VK_FORMAT_R16G16_SFLOAT;
	case RenderFormat::R8G8Unorm: return VK_FORMAT_R8G8_UNORM;
	case RenderFormat::R8Unorm: return VK_FORMAT_R8_UNORM;
	case RenderFormat::R32Uint: return VK_FORMAT_R32_UINT;
	case RenderFormat::D32Sfloat: return VK_FORMAT_D32_SFLOAT;
	case RenderFormat::R32G32Sfloat: return VK_FORMAT_R32G32_SFLOAT;
	case RenderFormat::R16G16B16A16Sfloat: return VK_FORMAT_R16G16B16A16_SFLOAT;
	default: return VK_FORMAT_UNDEFINED;
	}
}

RenderFormat TargetManager::ContractFormat(uint32_t attachment_index)
{
	return attachment_index < 6 ? kSceneAttachmentContract[attachment_index].format :
		RenderFormat::Count;
}

uint32_t TargetManager::BytesPerPixel(VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_R8_UNORM: return 1;
	case VK_FORMAT_R8G8_UNORM: return 2;
	case VK_FORMAT_R16G16_SFLOAT: return 4;
	case VK_FORMAT_R32_UINT: return 4;
	case VK_FORMAT_D32_SFLOAT: return 4;
	case VK_FORMAT_R8G8B8A8_UNORM: return 4;
	case VK_FORMAT_R32G32_SFLOAT: return 8;
	case VK_FORMAT_R16G16B16A16_SFLOAT: return 8;
	default: return 0;
	}
}

uint32_t TargetManager::FeatureFlags(const CapturedPreferredState &preferred)
{
	const bool gtao_temporal = preferred.gtao_enabled &&
		(preferred.gtao_temporal_blend > 0.0f ||
		 preferred.gtao_temporal_debug_preview);
	const bool motion = WantsMotionResources(preferred);
	return (preferred.bloom_enabled ? uint32_t(kTargetFeatureBloom) : 0u) |
		(preferred.gtao_enabled ? uint32_t(kTargetFeatureGtao) : 0u) |
		(gtao_temporal ? uint32_t(kTargetFeatureGtaoTemporal) : 0u) |
		(motion ? uint32_t(kTargetFeatureMotionConsumer) : 0u) |
		((preferred.bloom_enabled || preferred.gtao_enabled || motion) ?
		 uint32_t(kTargetFeatureLatePost) : 0u);
}

uint32_t TargetManager::ResolveGtaoScale(
	const CapturedPreferredState &preferred, uint32_t width, uint32_t height,
	uint32_t applied_msaa)
{
	switch (preferred.gtao_resolution)
	{
	case 1: return 1; // GTAO_RESOLUTION_FULL
	case 2: return 2; // GTAO_RESOLUTION_HALF
	case 3: return 4; // GTAO_RESOLUTION_QUARTER
	default: break;
	}
	uint64_t target_pixels = uint64_t(1920) * 1080;
	if (applied_msaa >= 8)
		target_pixels /= 4;
	else if (applied_msaa >= 4)
		target_pixels /= 2;
	uint32_t scale = 1;
	while (scale < 4)
	{
		const uint64_t w = (uint64_t(width) + scale - 1) / scale;
		const uint64_t h = (uint64_t(height) + scale - 1) / scale;
		if (w * h <= target_pixels)
			break;
		scale *= 2;
	}
	return scale;
}

bool TargetManager::AddAttachment(AllocatedImage *output, VkFormat format,
	VkExtent2D extent, VkSampleCountFlagBits samples, VkImageUsageFlags usage,
	VkImageAspectFlags aspect, const char *debug_name,
	uint64_t *estimated_bytes)
{
	ImageCreateRequest request = {};
	request.format = format;
	request.extent = { extent.width, extent.height, 1 };
	request.samples = samples;
	request.usage = usage;
	request.aspect = aspect;
	request.debug_name = debug_name;
	if (!allocator_->CreateImage(request, output))
		return false;
	const uint64_t sample_count = samples == VK_SAMPLE_COUNT_8_BIT ? 8 :
		(samples == VK_SAMPLE_COUNT_4_BIT ? 4 :
		 (samples == VK_SAMPLE_COUNT_2_BIT ? 2 : 1));
	*estimated_bytes += uint64_t(extent.width) * extent.height *
		BytesPerPixel(format) * sample_count;
	return true;
}

bool TargetManager::AddGraphImage(GenerationState *generation,
	GraphResource resource, uint32_t level, VkFormat format, VkExtent2D extent,
	VkImageUsageFlags usage, VkImageAspectFlags aspect, const char *debug_name)
{
	GraphImageEntry entry = {};
	entry.resource = resource;
	entry.level = level;
	if (!AddAttachment(&entry.allocation, format, extent,
		VK_SAMPLE_COUNT_1_BIT, usage, aspect, debug_name,
		&generation->estimated_bytes))
		return false;
	generation->graph_images.push_back(entry);
	return true;
}

bool TargetManager::BuildGeneration(const CapturedPreferredState &preferred,
	VkExtent2D drawable_extent, GenerationState *generation)
{
	if (!generation || preferred.width == 0 || preferred.height == 0 ||
		drawable_extent.width == 0 || drawable_extent.height == 0 ||
		(preferred.supersampling_factor != 1 &&
		 preferred.supersampling_factor != 2 &&
		 preferred.supersampling_factor != 4))
		return false;
	const uint32_t overscan = NormalizeOverscanPercent(preferred);
	const uint32_t internal_width = SafeCeilScale(preferred.width, overscan,
		preferred.supersampling_factor);
	const uint32_t internal_height = SafeCeilScale(preferred.height, overscan,
		preferred.supersampling_factor);
	if (!internal_width || !internal_height)
		return false;
	const uint32_t normalized = NormalizeRequestedMsaa(preferred.msaa_samples,
		preferred.antialised != 0);
	const uint32_t selected = SelectSupportedMsaa(normalized,
		supported_sample_mask_);
	const uint32_t applied_msaa = selected ? selected : 1;
	const VkSampleCountFlagBits sample_bits = SampleBits(applied_msaa);
	const uint32_t feature_flags = FeatureFlags(preferred);
	const uint32_t attachment_mask = kTargetAttachmentAll;
	const VkExtent2D internal = { internal_width, internal_height };
	const VkExtent2D logical = { preferred.width, preferred.height };

	generation->generation = ++next_generation_;
	generation->preferred = preferred;
	generation->scene.target = RenderTargetClass::Scene;
	generation->scene.logical_width = preferred.width;
	generation->scene.logical_height = preferred.height;
	generation->scene.internal_width = internal_width;
	generation->scene.internal_height = internal_height;
	generation->scene.drawable_width = drawable_extent.width;
	generation->scene.drawable_height = drawable_extent.height;
	generation->scene.ssaa_factor = preferred.supersampling_factor;
	generation->scene.msaa_samples = applied_msaa;
	generation->scene.attachment_mask = attachment_mask;
	generation->scene.feature_flags = feature_flags;
	generation->scene.overscan_percent = overscan;
	generation->scene.present_format = RenderFormat::R8G8B8A8Unorm;
	for (uint32_t i = 0; i < 6; ++i)
		generation->scene.attachment_formats[i] = ContractFormat(i);

	generation->cockpit = generation->scene;
	generation->cockpit.target = RenderTargetClass::CockpitScene;
	generation->cockpit.attachment_mask = kTargetAttachmentMandatory;
	generation->cockpit.feature_flags = 0;
	generation->post = generation->scene;
	generation->post.target = RenderTargetClass::PostPresent;
	generation->post.internal_width = preferred.width;
	generation->post.internal_height = preferred.height;
	generation->post.ssaa_factor = 1;
	generation->post.msaa_samples = 1;
	generation->post.attachment_mask = kTargetAttachmentColor;
	generation->post.feature_flags = 0;
	generation->post.overscan_percent = 100;

	for (uint32_t i = 0; i < 6; ++i)
	{
		const VkFormat format = VulkanFormat(ContractFormat(i));
		const bool depth = i == 5;
		if (!AddAttachment(&generation->scene_attachments[i], format, internal,
			sample_bits, depth ? DepthUsage() : ColorUsage(),
			depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT,
			depth ? "vk.scene.depth" : "vk.scene.color", &generation->estimated_bytes))
			return false;
	}
	if (!AddAttachment(&generation->post_attachments[0],
		VK_FORMAT_R8G8B8A8_UNORM, logical, VK_SAMPLE_COUNT_1_BIT,
		ColorUsage(), VK_IMAGE_ASPECT_COLOR_BIT, "vk.post.color",
		&generation->estimated_bytes))
		return false;
	for (uint32_t i = 0; i < 6; ++i)
	{
		if (!(generation->cockpit.attachment_mask & (1u << i)))
			continue;
		const bool depth = i == 5;
		if (!AddAttachment(&generation->cockpit_attachments[i],
			VulkanFormat(ContractFormat(i)), internal, sample_bits,
			depth ? DepthUsage() : ColorUsage(), depth ? VK_IMAGE_ASPECT_DEPTH_BIT :
			VK_IMAGE_ASPECT_COLOR_BIT, depth ? "vk.cockpit.depth" :
			"vk.cockpit.color", &generation->estimated_bytes))
			return false;
	}

	const GraphResource resolved_resources[6] = {
		GraphResource::ResolvedColor, GraphResource::ResolvedVelocity,
		GraphResource::ResolvedProtectionMask, GraphResource::ResolvedAoClass,
		GraphResource::ResolvedObjectId, GraphResource::ResolvedDepth
	};
	if (applied_msaa > 1)
		for (uint32_t i = 0; i < 6; ++i)
		{
			const bool depth = i == 5;
			if (!AddGraphImage(generation, resolved_resources[i], 0,
				VulkanFormat(ContractFormat(i)), internal,
				depth ? DepthUsage() : ColorUsage(), depth ?
				VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT,
				"vk.scene.resolved"))
				return false;
		}

	if (preferred.supersampling_factor == 4 &&
		!AddGraphImage(generation, GraphResource::SsaaIntermediate2x, 0,
			VK_FORMAT_R8G8B8A8_UNORM,
			{ SafeCeilScale(preferred.width, overscan, 2),
			  SafeCeilScale(preferred.height, overscan, 2) }, ColorUsage(),
			VK_IMAGE_ASPECT_COLOR_BIT, "vk.ssaa.2x"))
		return false;
	if (!AddGraphImage(generation, GraphResource::LogicalAuthoredColor, 0,
		VK_FORMAT_R8G8B8A8_UNORM, logical, ColorUsage(),
		VK_IMAGE_ASPECT_COLOR_BIT, "vk.logical.authored") ||
		!AddGraphImage(generation, GraphResource::PostLogicalDepth, 0,
		VK_FORMAT_D32_SFLOAT, logical, DepthUsage(), VK_IMAGE_ASPECT_DEPTH_BIT,
		"vk.logical.depth") ||
		!AddGraphImage(generation, GraphResource::CapturedWorldColor, 0,
		VK_FORMAT_R8G8B8A8_UNORM, logical, ColorUsage(),
		VK_IMAGE_ASPECT_COLOR_BIT, "vk.capture.color") ||
		!AddGraphImage(generation, GraphResource::CapturedWorldDepth, 0,
		VK_FORMAT_D32_SFLOAT, logical, DepthUsage(), VK_IMAGE_ASPECT_DEPTH_BIT,
		"vk.capture.depth"))
		return false;
	if (applied_msaa > 1 &&
		!AddGraphImage(generation, GraphResource::CapturedWorldMsaaResolved, 0,
			VK_FORMAT_R8G8B8A8_UNORM, internal, ColorUsage(),
			VK_IMAGE_ASPECT_COLOR_BIT, "vk.capture.msaa_resolved"))
		return false;
	if (preferred.supersampling_factor == 4 &&
		!AddGraphImage(generation, GraphResource::CapturedWorldIntermediate2x, 0,
			VK_FORMAT_R8G8B8A8_UNORM,
			{ SafeCeilScale(preferred.width, overscan, 2),
			  SafeCeilScale(preferred.height, overscan, 2) }, ColorUsage(),
			VK_IMAGE_ASPECT_COLOR_BIT, "vk.capture.2x"))
		return false;

	generation->gtao_scale = ResolveGtaoScale(preferred, preferred.width,
		preferred.height, applied_msaa);
	const VkExtent2D ao = {
		(preferred.width + generation->gtao_scale - 1) / generation->gtao_scale,
		(preferred.height + generation->gtao_scale - 1) / generation->gtao_scale
	};
	if (preferred.gtao_enabled)
	{
		const struct AoImage { GraphResource resource; VkFormat format; const char *name; } ao_images[] = {
			{ GraphResource::GtaoDepthWeight, VK_FORMAT_R32G32_SFLOAT, "vk.gtao.depth_weight" },
			{ GraphResource::GtaoRaw, VK_FORMAT_R16G16_SFLOAT, "vk.gtao.raw" },
			{ GraphResource::GtaoBlurTemporary, VK_FORMAT_R16G16_SFLOAT, "vk.gtao.blur" },
			{ GraphResource::GtaoCurrent, VK_FORMAT_R16G16_SFLOAT, "vk.gtao.current" },
			{ GraphResource::GtaoHistoryPrevious, VK_FORMAT_R16G16B16A16_SFLOAT, "vk.gtao.history.previous" },
			{ GraphResource::GtaoHistoryNext, VK_FORMAT_R16G16B16A16_SFLOAT, "vk.gtao.history.next" },
			{ GraphResource::GtaoSuppression, VK_FORMAT_R8_UNORM, "vk.gtao.suppression" },
		};
		for (const AoImage &image : ao_images)
			if (!AddGraphImage(generation, image.resource, 0, image.format, ao,
				ColorUsage(), VK_IMAGE_ASPECT_COLOR_BIT, image.name))
				return false;
		if (!AddGraphImage(generation, GraphResource::GtaoApplied, 0,
			VK_FORMAT_R8G8B8A8_UNORM, logical, ColorUsage(),
			VK_IMAGE_ASPECT_COLOR_BIT, "vk.gtao.applied") ||
			!AddGraphImage(generation, GraphResource::GtaoDeferredComposite, 0,
			VK_FORMAT_R8G8B8A8_UNORM, logical, ColorUsage(),
			VK_IMAGE_ASPECT_COLOR_BIT, "vk.gtao.deferred") ||
			!AddGraphImage(generation, GraphResource::GtaoNoise, 0,
			VK_FORMAT_R8G8_UNORM, { 4, 4 }, ColorUsage(),
			VK_IMAGE_ASPECT_COLOR_BIT, "vk.gtao.noise"))
			return false;
	}

	if (preferred.bloom_enabled && preferred.width >= 16 && preferred.height >= 16)
	{
		uint32_t width = preferred.width / 2;
		uint32_t height = preferred.height / 2;
		while (width >= 8 && height >= 8 && generation->bloom_level_count < 8)
		{
			const uint32_t level = generation->bloom_level_count++;
			if (!AddGraphImage(generation, GraphResource::BloomCurrentLevel,
				level, VK_FORMAT_R8G8B8A8_UNORM, { width, height }, ColorUsage(),
				VK_IMAGE_ASPECT_COLOR_BIT, "vk.bloom.current") ||
				!AddGraphImage(generation, GraphResource::BloomSmallerLevel,
				level, VK_FORMAT_R8G8B8A8_UNORM, { width, height }, ColorUsage(),
				VK_IMAGE_ASPECT_COLOR_BIT, "vk.bloom.smaller") ||
				!AddGraphImage(generation, GraphResource::BloomMerged,
				level, VK_FORMAT_R8G8B8A8_UNORM, { width, height }, ColorUsage(),
				VK_IMAGE_ASPECT_COLOR_BIT, "vk.bloom.merged"))
				return false;
			width /= 2;
			height /= 2;
		}
	}

	if (!AddGraphImage(generation, GraphResource::PostPresent, 1,
		VK_FORMAT_R8G8B8A8_UNORM, logical, ColorUsage(),
		VK_IMAGE_ASPECT_COLOR_BIT, "vk.post.present.b") ||
		!AddGraphImage(generation, GraphResource::VelocityDebugReadback, 0,
		VK_FORMAT_R8G8B8A8_UNORM, logical, ColorUsage(),
		VK_IMAGE_ASPECT_COLOR_BIT, "vk.motion.debug") ||
		!AddGraphImage(generation, GraphResource::CockpitResolved, 0,
		VK_FORMAT_R8G8B8A8_UNORM, logical, ColorUsage(),
		VK_IMAGE_ASPECT_COLOR_BIT, "vk.cockpit.resolved") ||
		!AddGraphImage(generation, GraphResource::CockpitAlpha, 0,
		VK_FORMAT_R8_UNORM, logical, ColorUsage(), VK_IMAGE_ASPECT_COLOR_BIT,
		"vk.cockpit.alpha") ||
		!AddGraphImage(generation, GraphResource::SoftDepthSnapshot, 0,
		VK_FORMAT_D32_SFLOAT, logical, DepthUsage(), VK_IMAGE_ASPECT_DEPTH_BIT,
		"vk.soft_depth"))
		return false;
	if (applied_msaa > 1 &&
		!AddGraphImage(generation, GraphResource::CockpitMsaaResolved, 0,
			VK_FORMAT_R8G8B8A8_UNORM, internal, ColorUsage(),
			VK_IMAGE_ASPECT_COLOR_BIT, "vk.cockpit.msaa_resolved"))
		return false;
	if (preferred.supersampling_factor == 4 &&
		!AddGraphImage(generation, GraphResource::CockpitSsaaIntermediate2x, 0,
			VK_FORMAT_R8G8B8A8_UNORM,
			{ SafeCeilScale(preferred.width, overscan, 2),
			  SafeCeilScale(preferred.height, overscan, 2) }, ColorUsage(),
			VK_IMAGE_ASPECT_COLOR_BIT, "vk.cockpit.2x"))
		return false;
	return true;
}

bool TargetManager::Configure(const CapturedPreferredState &preferred,
	VkExtent2D drawable_extent, uint64_t retire_after_timeline)
{
	if (!initialized_)
		return false;
	GenerationState replacement;
	if (!BuildGeneration(preferred, drawable_extent, &replacement))
	{
		RetireGeneration(&replacement, 0);
		allocator_->Reclaim(0);
		return false;
	}
	GenerationState old;
	old = std::move(active_);
	UnregisterGenerationState(&old);
	active_ = std::move(replacement);
	RegisterGenerationState(&active_);
	RetireGeneration(&old, retire_after_timeline);
	return true;
}

void TargetManager::RegisterGenerationState(GenerationState *generation)
{
	if (!generation || generation->state_registered || !state_tracker_)
		return;
	const ImageUse initial = {
		VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_QUEUE_FAMILY_IGNORED,
		ResourceIntent::Read
	};
	auto register_image = [&](const AllocatedImage &image) {
		if (!image.Valid())
			return;
		VkImageSubresourceRange range = {};
		range.aspectMask = image.aspect;
		range.baseMipLevel = 0;
		range.levelCount = image.mip_levels;
		range.baseArrayLayer = 0;
		range.layerCount = image.array_layers;
		state_tracker_->ImportFreshImage(image.handle, range, initial);
	};
	for (uint32_t i = 0; i < 6; ++i)
	{
		register_image(generation->scene_attachments[i]);
		register_image(generation->post_attachments[i]);
		register_image(generation->cockpit_attachments[i]);
	}
	for (const GraphImageEntry &entry : generation->graph_images)
		register_image(entry.allocation);
	generation->state_registered = true;
}

void TargetManager::UnregisterGenerationState(
	GenerationState *generation) noexcept
{
	if (!generation || !generation->state_registered || !state_tracker_)
		return;
	auto unregister_image = [&](const AllocatedImage &image) {
		if (image.Valid())
			state_tracker_->ForgetImage(image.handle);
	};
	for (uint32_t i = 0; i < 6; ++i)
	{
		unregister_image(generation->scene_attachments[i]);
		unregister_image(generation->post_attachments[i]);
		unregister_image(generation->cockpit_attachments[i]);
	}
	for (const GraphImageEntry &entry : generation->graph_images)
		unregister_image(entry.allocation);
	generation->state_registered = false;
}

void TargetManager::RetireGeneration(GenerationState *generation,
	uint64_t retire_after_timeline) noexcept
{
	if (!generation || !allocator_)
		return;
	UnregisterGenerationState(generation);
	for (uint32_t i = 0; i < 6; ++i)
	{
		allocator_->RetireImage(&generation->scene_attachments[i],
			retire_after_timeline);
		allocator_->RetireImage(&generation->post_attachments[i],
			retire_after_timeline);
		allocator_->RetireImage(&generation->cockpit_attachments[i],
			retire_after_timeline);
	}
	for (GraphImageEntry &entry : generation->graph_images)
		allocator_->RetireImage(&entry.allocation, retire_after_timeline);
	*generation = GenerationState();
}

TargetImageRef TargetManager::Reference(const AllocatedImage &image) const
{
	TargetImageRef result = {};
	if (!image.Valid())
		return result;
	result.image = image.handle;
	result.view = image.view;
	result.format = image.format;
	result.extent = { image.extent.width, image.extent.height };
	result.samples = image.samples;
	result.aspect = image.aspect;
	result.generation = active_.generation;
	return result;
}

const AllocatedImage *TargetManager::FindGraph(GraphResource resource,
	uint32_t level) const
{
	for (const GraphImageEntry &entry : active_.graph_images)
		if (entry.resource == resource && entry.level == level)
			return &entry.allocation;
	return nullptr;
}

TargetImageRef TargetManager::Attachment(RenderTargetClass target,
	uint32_t attachment_index) const
{
	if (!Ready() || attachment_index >= 6)
		return {};
	switch (target)
	{
	case RenderTargetClass::Scene:
		return Reference(active_.scene_attachments[attachment_index]);
	case RenderTargetClass::PostPresent:
		return Reference(active_.post_attachments[attachment_index]);
	case RenderTargetClass::CockpitScene:
		return Reference(active_.cockpit_attachments[attachment_index]);
	default:
		return {};
	}
}

TargetImageRef TargetManager::GraphImage(GraphResource resource,
	uint32_t level) const
{
	if (!Ready())
		return {};
	if (resource == GraphResource::PostPresent && level == 0)
		return Attachment(RenderTargetClass::PostPresent, 0);
	const AllocatedImage *image = FindGraph(resource, level);
	if (image)
		return Reference(*image);
	// Single-sample scene resources are their own resolved form.
	if (active_.scene.msaa_samples == 1)
	{
		switch (resource)
		{
		case GraphResource::ResolvedColor: return Attachment(RenderTargetClass::Scene, 0);
		case GraphResource::ResolvedVelocity: return Attachment(RenderTargetClass::Scene, 1);
		case GraphResource::ResolvedProtectionMask: return Attachment(RenderTargetClass::Scene, 2);
		case GraphResource::ResolvedAoClass: return Attachment(RenderTargetClass::Scene, 3);
		case GraphResource::ResolvedObjectId: return Attachment(RenderTargetClass::Scene, 4);
		case GraphResource::ResolvedDepth: return Attachment(RenderTargetClass::Scene, 5);
		default: break;
		}
	}
	switch (resource)
	{
	case GraphResource::SceneColor: return Attachment(RenderTargetClass::Scene, 0);
	case GraphResource::SceneVelocity: return Attachment(RenderTargetClass::Scene, 1);
	case GraphResource::SceneProtectionMask: return Attachment(RenderTargetClass::Scene, 2);
	case GraphResource::SceneAoClass: return Attachment(RenderTargetClass::Scene, 3);
	case GraphResource::SceneObjectId: return Attachment(RenderTargetClass::Scene, 4);
	case GraphResource::SceneDepth: return Attachment(RenderTargetClass::Scene, 5);
	case GraphResource::CockpitScene: return Attachment(RenderTargetClass::CockpitScene, 0);
	default: return {};
	}
}

const CapturedTargetLayout &TargetManager::SceneLayout() const { return active_.scene; }
const CapturedTargetLayout &TargetManager::PostLayout() const { return active_.post; }
const CapturedTargetLayout &TargetManager::CockpitLayout() const { return active_.cockpit; }

CapturedTargetVersion TargetManager::DescribeVersion(RenderTargetClass target,
	TargetLayoutId layout_id, uint32_t color_epoch, uint32_t depth_epoch) const
{
	CapturedTargetVersion result = {};
	result.target = target;
	result.version = static_cast<uint32_t>(active_.generation);
	result.target_layout = layout_id;
	const CapturedTargetLayout *layout = target == RenderTargetClass::Scene ?
		&active_.scene : (target == RenderTargetClass::PostPresent ? &active_.post :
		&active_.cockpit);
	result.width = layout->internal_width;
	result.height = layout->internal_height;
	result.samples = layout->msaa_samples;
	result.color_epoch = color_epoch;
	result.depth_epoch = depth_epoch;
	return result;
}

uint32_t TargetManager::BloomLevelCount() const noexcept { return active_.bloom_level_count; }
uint32_t TargetManager::GtaoScale() const noexcept { return active_.gtao_scale; }
uint64_t TargetManager::Generation() const noexcept { return active_.generation; }
uint64_t TargetManager::EstimatedImageBytes() const noexcept { return active_.estimated_bytes; }

void TargetManager::StampUse(uint64_t timeline_value)
{
	for (uint32_t i = 0; i < 6; ++i)
	{
		active_.scene_attachments[i].last_use_timeline = timeline_value;
		active_.post_attachments[i].last_use_timeline = timeline_value;
		active_.cockpit_attachments[i].last_use_timeline = timeline_value;
	}
	for (GraphImageEntry &entry : active_.graph_images)
		entry.allocation.last_use_timeline = timeline_value;
}

void TargetManager::InvalidateHistories()
{
	active_.gtao_history_valid = false;
	active_.motion_history_valid = false;
	active_.cockpit_history_valid = false;
}

bool TargetManager::GtaoHistoryValid() const noexcept { return active_.gtao_history_valid; }
bool TargetManager::MotionHistoryValid() const noexcept { return active_.motion_history_valid; }
bool TargetManager::CockpitHistoryValid() const noexcept { return active_.cockpit_history_valid; }

void TargetManager::SetHistoryValidity(bool gtao, bool motion, bool cockpit)
{
	active_.gtao_history_valid = gtao;
	active_.motion_history_valid = motion;
	active_.cockpit_history_valid = cockpit;
}

} // namespace vk
} // namespace render
} // namespace piccu
