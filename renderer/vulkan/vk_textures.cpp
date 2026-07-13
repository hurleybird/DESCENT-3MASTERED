/* Immutable legacy-texture capture, Vulkan residency, and sampler ownership. */
#include "vk_textures.h"

#include "../../lib/bitmap.h"
#include "../../lib/lightmap.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace piccu
{
namespace render
{
namespace vk
{

namespace
{

constexpr uint32_t kLegacyMapTypeBitmap = 0;
constexpr uint32_t kLegacyMapTypeLightmap = 1;
constexpr int32_t kVulkanCacheCookie = 0x564b;

uint8_t Expand5(uint32_t value) noexcept
{
	// This is exactly trunc(255.0f * (value / 31.0f)) for value in [0,31].
	return static_cast<uint8_t>((value * 255u) / 31u);
}

uint8_t Expand4(uint32_t value) noexcept
{
	return static_cast<uint8_t>((value * 255u) / 15u);
}

VkSamplerAddressMode VulkanAddress(SamplerAddressMode mode) noexcept
{
	return mode == SamplerAddressMode::Repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT :
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
}

VkFilter VulkanFilter(SamplerFilterMode mode) noexcept
{
	return mode == SamplerFilterMode::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
}

VkSamplerMipmapMode VulkanMipMode(SamplerMipMode mode) noexcept
{
	return mode == SamplerMipMode::Linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR :
		VK_SAMPLER_MIPMAP_MODE_NEAREST;
}

bool PayloadRange(const RenderCaptureSegment &capture, PayloadDataId id,
	const uint8_t **bytes, uint32_t *byte_size)
{
	if (!bytes || !byte_size || id == kInvalidId ||
		id >= capture.PayloadRecords().size())
		return false;
	const CapturedPayloadRecord &record = capture.PayloadRecords()[id];
	if (record.semantic != kPayloadTextureUpload || record.alignment !=
		kCapturedTextureUploadPayloadAlignment ||
		record.byte_offset > capture.PayloadBytes().size() ||
		record.byte_size > capture.PayloadBytes().size() - record.byte_offset)
		return false;
	*bytes = capture.PayloadBytes().data() + record.byte_offset;
	*byte_size = record.byte_size;
	return true;
}

bool AlignUp(VkDeviceSize value, VkDeviceSize alignment,
	VkDeviceSize *aligned) noexcept
{
	if (!aligned || alignment == 0 || (alignment & (alignment - 1)) != 0 ||
		value > std::numeric_limits<VkDeviceSize>::max() - (alignment - 1))
		return false;
	*aligned = (value + alignment - 1) & ~(alignment - 1);
	return true;
}

} // namespace

void ConvertLegacy1555ToRgba8(uint16_t source, uint8_t output[4]) noexcept
{
	if (!output)
		return;
	if ((source & 0x8000u) == 0)
	{
		output[0] = output[1] = output[2] = output[3] = 0;
		return;
	}
	output[0] = Expand5((source >> 10u) & 0x1fu);
	output[1] = Expand5((source >> 5u) & 0x1fu);
	output[2] = Expand5(source & 0x1fu);
	output[3] = 255;
}

void ConvertLegacy4444ToRgba8(uint16_t source, uint8_t output[4]) noexcept
{
	if (!output)
		return;
	output[0] = Expand4((source >> 8u) & 0x0fu);
	output[1] = Expand4((source >> 4u) & 0x0fu);
	output[2] = Expand4(source & 0x0fu);
	output[3] = Expand4((source >> 12u) & 0x0fu);
}

TextureManager::TextureManager()
	: platform_(nullptr), allocator_(nullptr), frames_(nullptr),
	  state_tracker_(nullptr), diagnostic_2d_(kInvalidId),
	  diagnostic_array_(kInvalidId), terrain_base_version_(kInvalidId),
	  terrain_lightmap_version_(kInvalidId), terrain_base_signature_(0),
	  terrain_lightmap_signature_(0), terrain_array_generation_(0),
	  next_content_serial_(1),
	  resolve_serial_(0), ready_(false)
{
	std::memset(samplers_, 0, sizeof(samplers_));
	std::memset(world_samplers_, 0, sizeof(world_samplers_));
}

TextureManager::~TextureManager()
{
	Shutdown();
}

bool TextureManager::Initialize(Platform *platform, ResourceAllocator *allocator,
	FrameScheduler *frames, ResourceStateTracker *state_tracker,
	const TextureResidencyConfig &config)
{
	if (ready_ || !platform || !allocator || !frames || !state_tracker ||
		!platform->Ready() || !allocator->Ready() || !frames->Ready())
		return false;
	platform_ = platform;
	allocator_ = allocator;
	frames_ = frames;
	state_tracker_ = state_tracker;
	config_ = config;
	if (config_.maximum_font_layers == 0)
		config_.maximum_font_layers = 1;
	config_.maximum_font_layers = std::min(config_.maximum_font_layers,
		platform_->SelectedDevice().properties.limits.maxImageArrayLayers);
	config_.maximum_font_layers = std::min(config_.maximum_font_layers, 256u);
	bitmap_mappings_.resize(MAX_BITMAPS);
	lightmap_mappings_.resize(MAX_LIGHTMAPS);
	if (!CreateSamplers())
	{
		Shutdown();
		return false;
	}

	const uint8_t diagnostic_pixels[16] = {
		255, 0, 255, 255, 0, 0, 0, 255,
		0, 0, 0, 255, 255, 0, 255, 255,
	};
	std::vector<uint8_t> pixels_2d(diagnostic_pixels,
		diagnostic_pixels + sizeof(diagnostic_pixels));
	Version *diagnostic = CreateVersion(2, 2, 1, 1, 1, false, true,
		std::move(pixels_2d), kInvalidId);
	if (!diagnostic)
	{
		Shutdown();
		return false;
	}
	diagnostic_2d_ = diagnostic->captured.id;
	std::vector<uint8_t> pixels_array(diagnostic_pixels,
		diagnostic_pixels + sizeof(diagnostic_pixels));
	diagnostic = CreateVersion(2, 2, 1, 1, 1, true, true,
		std::move(pixels_array), kInvalidId);
	if (!diagnostic)
	{
		Shutdown();
		return false;
	}
	diagnostic_array_ = diagnostic->captured.id;
	ready_ = true;
	return true;
}

void TextureManager::Shutdown(bool device_lost) noexcept
{
	(void)device_lost;
	for (size_t i = 0; i < bitmap_mappings_.size(); ++i)
		if (bitmap_mappings_[i].version != kInvalidId)
			GameBitmaps[i].cache_slot = -1;
	for (size_t i = 0; i < lightmap_mappings_.size(); ++i)
		if (lightmap_mappings_[i].version != kInvalidId)
			GameLightmaps[i].cache_slot = -1;
	for (const FontLayer &layer : font_array_.layers)
		if (layer.handle >= 0 && layer.handle < MAX_BITMAPS)
			GameBitmaps[layer.handle].cache_slot = -1;
	if (allocator_ && allocator_->Ready())
	{
		for (Version &version : versions_)
			if (version.allocation.Valid())
				allocator_->RetireImage(&version.allocation,
					version.captured.last_use_timeline);
	}
	DestroySamplers();
	bitmap_mappings_.clear();
	lightmap_mappings_.clear();
	versions_.clear();
	font_array_ = FontArray();
	platform_ = nullptr;
	allocator_ = nullptr;
	frames_ = nullptr;
	state_tracker_ = nullptr;
	diagnostic_2d_ = kInvalidId;
	diagnostic_array_ = kInvalidId;
	terrain_base_version_ = terrain_lightmap_version_ = kInvalidId;
	terrain_base_signature_ = terrain_lightmap_signature_ = 0;
	terrain_array_generation_ = 0;
	next_content_serial_ = 1;
	resolve_serial_ = 0;
	stats_ = TextureResidencyStats();
	ready_ = false;
}

TextureManager::Version *TextureManager::FindVersion(TextureVersionId id)
{
	if (id == kInvalidId || id == 0 || id > versions_.size())
		return nullptr;
	return &versions_[id - 1];
}

const TextureManager::Version *TextureManager::FindVersion(
	TextureVersionId id) const
{
	if (id == kInvalidId || id == 0 || id > versions_.size())
		return nullptr;
	return &versions_[id - 1];
}

bool TextureManager::IdentityEqual(const LegacyIdentity &left,
	const LegacyIdentity &right) noexcept
{
	return left.data == right.data && left.width == right.width &&
		left.height == right.height && left.storage_width == right.storage_width &&
		left.mip_count == right.mip_count && left.format == right.format &&
		left.used == right.used && left.cache_cookie == right.cache_cookie;
}

VkDeviceSize TextureManager::TextureBytes(uint32_t width, uint32_t height,
	uint32_t layers, uint32_t mip_count) noexcept
{
	VkDeviceSize result = 0;
	for (uint32_t mip = 0; mip < mip_count; ++mip)
	{
		const VkDeviceSize level = static_cast<VkDeviceSize>(width) * height *
			layers * 4u;
		if (level > std::numeric_limits<VkDeviceSize>::max() - result)
			return 0;
		result += level;
		width = width > 1 ? width / 2 : 1;
		height = height > 1 ? height / 2 : 1;
	}
	return result;
}

uint64_t TextureManager::CaptureKey(
	const RenderCaptureSegment &capture) noexcept
{
	return (static_cast<uint64_t>(capture.PresentedFrameSerial()) << 32u) |
		capture.SegmentSerial();
}

TextureManager::Version *TextureManager::CreateVersion(uint32_t width,
	uint32_t height, uint32_t layers, uint32_t mip_count, uint32_t generation,
	bool array_texture, bool diagnostic, std::vector<uint8_t> &&snapshot,
	TextureVersionId recycle_candidate)
{
	if (width == 0 || height == 0 || layers == 0 || mip_count == 0 ||
		versions_.size() >= static_cast<size_t>(kInvalidId - 1) ||
		next_content_serial_ == UINT64_MAX)
		return nullptr;
	const VkDeviceSize byte_size = TextureBytes(width, height, layers, mip_count);
	if (byte_size == 0 || byte_size != snapshot.size() ||
		byte_size > static_cast<VkDeviceSize>(UINT32_MAX))
		return nullptr;
	Version version = {};
	version.captured.id = static_cast<TextureVersionId>(versions_.size() + 1);
	version.captured.format = RenderFormat::R8G8B8A8Unorm;
	version.captured.width = width;
	version.captured.height = height;
	version.captured.depth_or_layers = layers;
	version.captured.mip_count = mip_count;
	version.captured.handle_generation = generation;
	version.captured.content_serial = next_content_serial_++;
	version.captured.residency = static_cast<uint32_t>(
		TextureResidencyState::CpuSnapshot);
	version.captured.immutable_upload_payload = kInvalidId;
	version.cpu_snapshot = std::move(snapshot);
	version.recycle_candidate = recycle_candidate;
	version.byte_size = byte_size;
	version.array_texture = array_texture ? 1u : 0u;
	version.diagnostic = diagnostic ? 1u : 0u;
	versions_.push_back(std::move(version));
	stats_.logical_versions = versions_.size();
	return &versions_.back();
}

bool TextureManager::ObserveBitmap(int32_t handle, LegacyIdentity *identity,
	uint32_t *dirty_flags) const
{
	if (!identity || !dirty_flags || handle < 0 || handle >= MAX_BITMAPS ||
		GameBitmaps[handle].used == 0)
		return false;
	uint16_t *data = bm_data(handle, 0);
	if (!data || GameBitmaps[handle].used == 0)
		return false;
	const int width = bm_w(handle, 0);
	const int height = bm_h(handle, 0);
	if (width <= 0 || height <= 0)
		return false;
	uint32_t mip_count = 1;
	if (bm_mipped(handle) > 0)
	{
		const int supplied = bm_miplevels(handle);
		if (supplied <= 0)
			return false;
		mip_count = 0;
		for (int mip = 0; mip < supplied; ++mip)
		{
			// Legacy storage uses raw shifts rather than Vulkan's per-axis
			// clamp-to-one rule. Preserve every supplied, nonempty level and
			// stop before a legacy zero-height/zero-width tail.
			if (mip >= 31 || (width >> mip) == 0 || (height >> mip) == 0)
				break;
			++mip_count;
		}
		if (mip_count == 0)
			return false;
	}
	identity->data = reinterpret_cast<uintptr_t>(data);
	identity->width = static_cast<uint32_t>(width);
	identity->height = static_cast<uint32_t>(height);
	identity->storage_width = static_cast<uint32_t>(width);
	identity->mip_count = mip_count;
	identity->format = static_cast<uint32_t>(bm_format(handle));
	identity->used = 1; // Lifetime-valid marker; reference-count changes are not identity.
	identity->cache_cookie = GameBitmaps[handle].cache_slot;
	*dirty_flags = 0;
	if (GameBitmaps[handle].flags & BF_CHANGED)
		*dirty_flags |= kTextureBitmapChanged;
	if (GameBitmaps[handle].flags & BF_BRAND_NEW)
		*dirty_flags |= kTextureBitmapBrandNew;
	return identity->format == BITMAP_FORMAT_1555 ||
		identity->format == BITMAP_FORMAT_4444;
}

bool TextureManager::ObserveLightmap(int32_t handle, LegacyIdentity *identity,
	uint32_t *dirty_flags, float uv_scale[2]) const
{
	if (!identity || !dirty_flags || !uv_scale || handle < 0 ||
		handle >= MAX_LIGHTMAPS || GameLightmaps[handle].used == 0 ||
		GameLightmaps[handle].data == nullptr ||
		GameLightmaps[handle].width == 0 || GameLightmaps[handle].height == 0 ||
		GameLightmaps[handle].square_res == 0 ||
		GameLightmaps[handle].width > GameLightmaps[handle].square_res ||
		GameLightmaps[handle].height > GameLightmaps[handle].square_res)
		return false;
	identity->data = reinterpret_cast<uintptr_t>(GameLightmaps[handle].data);
	identity->width = GameLightmaps[handle].width;
	identity->height = GameLightmaps[handle].height;
	identity->storage_width = GameLightmaps[handle].square_res;
	identity->mip_count = 1;
	identity->format = BITMAP_FORMAT_1555;
	identity->used = 1; // Lifetime-valid marker; reference-count changes are not identity.
	identity->cache_cookie = GameLightmaps[handle].cache_slot;
	*dirty_flags = 0;
	if (GameLightmaps[handle].flags & LF_CHANGED)
		*dirty_flags |= kTextureLightmapChanged;
	if (GameLightmaps[handle].flags & LF_BRAND_NEW)
		*dirty_flags |= kTextureLightmapBrandNew;
	uv_scale[0] = static_cast<float>(identity->width) /
		static_cast<float>(identity->storage_width);
	uv_scale[1] = static_cast<float>(identity->height) /
		static_cast<float>(identity->storage_width);
	return true;
}

bool TextureManager::SnapshotBitmap(int32_t handle, LegacyIdentity *identity,
	std::vector<uint8_t> *rgba, uint32_t *dirty_flags) const
{
	if (!rgba || !ObserveBitmap(handle, identity, dirty_flags))
		return false;
	const VkDeviceSize byte_size = TextureBytes(identity->width,
		identity->height, 1, identity->mip_count);
	if (byte_size == 0 || byte_size > static_cast<VkDeviceSize>(UINT32_MAX))
		return false;
	rgba->resize(static_cast<size_t>(byte_size));
	size_t destination = 0;
	for (uint32_t mip = 0; mip < identity->mip_count; ++mip)
	{
		const uint16_t *source = bm_data(handle, static_cast<int>(mip));
		const uint32_t width = std::max(1u, identity->width >> mip);
		const uint32_t height = std::max(1u, identity->height >> mip);
		if (!source)
			return false;
		for (uint32_t i = 0; i < width * height; ++i, destination += 4)
		{
			if (identity->format == BITMAP_FORMAT_4444)
				ConvertLegacy4444ToRgba8(source[i], rgba->data() + destination);
			else
				ConvertLegacy1555ToRgba8(source[i], rgba->data() + destination);
		}
	}
	return destination == rgba->size();
}

bool TextureManager::SnapshotLightmap(int32_t handle, LegacyIdentity *identity,
	std::vector<uint8_t> *rgba, uint32_t *dirty_flags, float uv_scale[2]) const
{
	if (!rgba || !ObserveLightmap(handle, identity, dirty_flags, uv_scale))
		return false;
	const uint32_t square = identity->storage_width;
	rgba->assign(static_cast<size_t>(square) * square * 4u, 0);
	const uint16_t *source = GameLightmaps[handle].data;
	for (uint32_t y = 0; y < identity->height; ++y)
		for (uint32_t x = 0; x < identity->width; ++x)
		{
			const size_t destination = (static_cast<size_t>(y) * square + x) * 4u;
			ConvertLegacy1555ToRgba8(source[static_cast<size_t>(y) *
				identity->width + x], rgba->data() + destination);
		}
	return true;
}

bool TextureManager::SnapshotFontLayer(int32_t handle,
	LegacyIdentity *identity, std::vector<uint8_t> *rgba,
	uint32_t *dirty_flags) const
{
	if (!rgba || !ObserveBitmap(handle, identity, dirty_flags))
		return false;
	identity->mip_count = 1;
	const uint16_t *source = bm_data(handle, 0);
	if (!source)
		return false;
	const uint64_t texel_count = static_cast<uint64_t>(identity->width) *
		identity->height;
	if (texel_count == 0 || texel_count > UINT32_MAX / 4u)
		return false;
	rgba->resize(static_cast<size_t>(texel_count * 4u));
	for (size_t i = 0; i < static_cast<size_t>(texel_count); ++i)
	{
		if (identity->format == BITMAP_FORMAT_4444)
			ConvertLegacy4444ToRgba8(source[i], rgba->data() + i * 4u);
		else
			ConvertLegacy1555ToRgba8(source[i], rgba->data() + i * 4u);
	}
	return true;
}

bool TextureManager::IsReferenced(const RenderCaptureSegment &capture,
	TextureVersionId id) const
{
	for (const CapturedTextureVersion &version : capture.TextureVersions())
		if (version.id == id)
			return true;
	return false;
}

void TextureManager::ClearCurrentMapping(TextureVersionId id)
{
	Version *version = FindVersion(id);
	if (!version)
		return;
	version->current_mapping = 0;
	if (version->allocation.Valid())
		version->captured.residency = static_cast<uint32_t>(
			TextureResidencyState::Evictable);
}

void TextureManager::InvalidateMappingsTo(TextureVersionId id)
{
	if (id == kInvalidId)
		return;
	for (size_t i = 0; i < bitmap_mappings_.size(); ++i)
		if (bitmap_mappings_[i].version == id)
		{
			GameBitmaps[i].cache_slot = -1;
			bitmap_mappings_[i] = LogicalMapping();
		}
	for (size_t i = 0; i < lightmap_mappings_.size(); ++i)
		if (lightmap_mappings_[i].version == id)
		{
			GameLightmaps[i].cache_slot = -1;
			lightmap_mappings_[i] = LogicalMapping();
		}
	if (font_array_.version == id)
	{
		for (const FontLayer &layer : font_array_.layers)
			if (layer.handle >= 0 && layer.handle < MAX_BITMAPS)
				GameBitmaps[layer.handle].cache_slot = -1;
		font_array_ = FontArray();
	}
	if (terrain_base_version_ == id)
	{
		terrain_base_version_ = kInvalidId;
		terrain_base_signature_ = 0;
	}
	if (terrain_lightmap_version_ == id)
	{
		terrain_lightmap_version_ = kInvalidId;
		terrain_lightmap_signature_ = 0;
	}
	ClearCurrentMapping(id);
}

bool TextureManager::EmitCapturedVersion(Version *version,
	RenderCaptureSegment *capture, CapturedTextureVersion *captured)
{
	if (!version || !capture || !captured)
		return false;
	for (const CapturedTextureVersion &existing : capture->TextureVersions())
		if (existing.id == version->captured.id)
		{
			*captured = existing;
			const uint64_t key = CaptureKey(*capture);
			if (std::find(version->pending_capture_keys.begin(),
				version->pending_capture_keys.end(), key) ==
				version->pending_capture_keys.end())
				version->pending_capture_keys.push_back(key);
			return true;
		}
	*captured = version->captured;
	captured->immutable_upload_payload = kInvalidId;
	if (!version->allocation.Valid())
	{
		if (version->cpu_snapshot.empty() ||
			version->cpu_snapshot.size() > static_cast<size_t>(UINT32_MAX))
			return false;
		const PayloadDataId payload = capture->CopyPayloadData(
			version->cpu_snapshot.data(),
			static_cast<uint32_t>(version->cpu_snapshot.size()),
			kCapturedTextureUploadPayloadAlignment, kPayloadTextureUpload);
		if (payload == kInvalidId)
			return false;
		captured->immutable_upload_payload = payload;
		captured->residency = static_cast<uint32_t>(
			TextureResidencyState::PendingUpload);
		version->captured.residency = captured->residency;
	}
	version->last_resolve_serial = ++resolve_serial_;
	if (!capture->RegisterTextureVersion(*captured))
		return false;
	const uint64_t key = CaptureKey(*capture);
	if (std::find(version->pending_capture_keys.begin(),
		version->pending_capture_keys.end(), key) ==
		version->pending_capture_keys.end())
		version->pending_capture_keys.push_back(key);
	return true;
}

bool TextureManager::ResolveDiagnostic(bool array_texture,
	RenderCaptureSegment *capture, ResolvedTexture *resolved)
{
	Version *version = FindVersion(array_texture ? diagnostic_array_ :
		diagnostic_2d_);
	if (!version || !EmitCapturedVersion(version, capture, &resolved->version))
		return false;
	resolved->array_layer = 0;
	resolved->font_bucket = 0;
	resolved->uv_scale[0] = resolved->uv_scale[1] = 1.0f;
	return true;
}

bool TextureManager::ResolveBitmap(const TextureRequest &request,
	RenderCaptureSegment *capture, ResolvedTexture *resolved)
{
	if (request.logical_handle < 0 || request.logical_handle >= MAX_BITMAPS)
		return false;
	LogicalMapping &mapping = bitmap_mappings_[request.logical_handle];
	LegacyIdentity observed = {};
	uint32_t dirty_flags = 0;
	if (!ObserveBitmap(request.logical_handle, &observed, &dirty_flags))
		return false;
	const bool has_mapping = mapping.version != kInvalidId &&
		FindVersion(mapping.version) != nullptr;
	const bool identity_matches = has_mapping &&
		IdentityEqual(mapping.identity, observed);
	const Version *mapped = FindVersion(mapping.version);
	TextureVersionBindInput input = {};
	input.dirty_flags = dirty_flags;
	input.has_logical_mapping = has_mapping ? 1u : 0u;
	input.mapped_identity_matches = identity_matches ? 1u : 0u;
	input.mapped_version_referenced_earlier_in_segment = has_mapping &&
		(IsReferenced(*capture, mapping.version) ||
		 (mapped && !mapped->pending_capture_keys.empty())) ? 1u : 0u;
	input.mapped_version_last_use_timeline = mapped ?
		mapped->captured.last_use_timeline : 0;
	input.completed_timeline = frames_->CompletedTimeline();
	const TextureVersionBindDecision decision = EvaluateTextureVersionBind(input);
	if (decision.reuse_mapped_logical_version)
	{
		Version *version = FindVersion(mapping.version);
		if (!version || !EmitCapturedVersion(version, capture, &resolved->version))
			return false;
		resolved->array_layer = resolved->font_bucket = 0;
		resolved->uv_scale[0] = resolved->uv_scale[1] = 1.0f;
		return true;
	}

	std::vector<uint8_t> snapshot;
	if (!SnapshotBitmap(request.logical_handle, &observed, &snapshot,
		&dirty_flags))
		return false;
	uint32_t generation = has_mapping ? mapping.generation : 0;
	if (!has_mapping || !identity_matches ||
		(dirty_flags & kTextureBitmapBrandNew) != 0)
		++generation;
	const TextureVersionId candidate = decision.may_recycle_completed_image ?
		mapping.version : kInvalidId;
	Version *version = CreateVersion(observed.width, observed.height, 1,
		observed.mip_count, generation, false, false, std::move(snapshot), candidate);
	if (!version)
		return false;
	if (!EmitCapturedVersion(version, capture, &resolved->version))
	{
		versions_.pop_back();
		stats_.logical_versions = versions_.size();
		return false;
	}
	if (has_mapping)
		ClearCurrentMapping(mapping.version);
	version = FindVersion(resolved->version.id);
	version->current_mapping = 1;
	mapping.version = version->captured.id;
	mapping.generation = generation;
	GameBitmaps[request.logical_handle].cache_slot =
		static_cast<short>(kVulkanCacheCookie);
	observed.cache_cookie = kVulkanCacheCookie;
	mapping.identity = observed;
	if (decision.clear_dirty_flags_now)
		GameBitmaps[request.logical_handle].flags &=
			static_cast<ubyte>(~(BF_CHANGED | BF_BRAND_NEW));
	resolved->array_layer = resolved->font_bucket = 0;
	resolved->uv_scale[0] = resolved->uv_scale[1] = 1.0f;
	return true;
}

bool TextureManager::ResolveLightmap(const TextureRequest &request,
	RenderCaptureSegment *capture, ResolvedTexture *resolved)
{
	if (request.logical_handle < 0 || request.logical_handle >= MAX_LIGHTMAPS)
		return false;
	LogicalMapping &mapping = lightmap_mappings_[request.logical_handle];
	LegacyIdentity observed = {};
	uint32_t dirty_flags = 0;
	float uv_scale[2] = { 1.0f, 1.0f };
	if (!ObserveLightmap(request.logical_handle, &observed, &dirty_flags,
		uv_scale))
		return false;
	const bool has_mapping = mapping.version != kInvalidId &&
		FindVersion(mapping.version) != nullptr;
	const bool identity_matches = has_mapping &&
		IdentityEqual(mapping.identity, observed);
	const Version *mapped = FindVersion(mapping.version);
	TextureVersionBindInput input = {};
	input.dirty_flags = dirty_flags;
	input.has_logical_mapping = has_mapping ? 1u : 0u;
	input.mapped_identity_matches = identity_matches ? 1u : 0u;
	input.mapped_version_referenced_earlier_in_segment = has_mapping &&
		(IsReferenced(*capture, mapping.version) ||
		 (mapped && !mapped->pending_capture_keys.empty())) ? 1u : 0u;
	input.mapped_version_last_use_timeline = mapped ?
		mapped->captured.last_use_timeline : 0;
	input.completed_timeline = frames_->CompletedTimeline();
	const TextureVersionBindDecision decision = EvaluateTextureVersionBind(input);
	if (decision.reuse_mapped_logical_version)
	{
		Version *version = FindVersion(mapping.version);
		if (!version || !EmitCapturedVersion(version, capture, &resolved->version))
			return false;
		resolved->array_layer = resolved->font_bucket = 0;
		resolved->uv_scale[0] = uv_scale[0];
		resolved->uv_scale[1] = uv_scale[1];
		return true;
	}

	std::vector<uint8_t> snapshot;
	if (!SnapshotLightmap(request.logical_handle, &observed, &snapshot,
		&dirty_flags, uv_scale))
		return false;
	uint32_t generation = has_mapping ? mapping.generation : 0;
	if (!has_mapping || !identity_matches ||
		(dirty_flags & kTextureLightmapBrandNew) != 0)
		++generation;
	const TextureVersionId candidate = decision.may_recycle_completed_image ?
		mapping.version : kInvalidId;
	Version *version = CreateVersion(observed.storage_width,
		observed.storage_width, 1, 1, generation, false, false,
		std::move(snapshot), candidate);
	if (!version)
		return false;
	if (!EmitCapturedVersion(version, capture, &resolved->version))
	{
		versions_.pop_back();
		stats_.logical_versions = versions_.size();
		return false;
	}
	if (has_mapping)
		ClearCurrentMapping(mapping.version);
	version = FindVersion(resolved->version.id);
	version->current_mapping = 1;
	mapping.version = version->captured.id;
	mapping.generation = generation;
	GameLightmaps[request.logical_handle].cache_slot =
		static_cast<short>(kVulkanCacheCookie);
	observed.cache_cookie = kVulkanCacheCookie;
	mapping.identity = observed;
	if (decision.clear_dirty_flags_now)
		GameLightmaps[request.logical_handle].flags &=
			static_cast<ubyte>(~(LF_CHANGED | LF_BRAND_NEW));
	resolved->array_layer = resolved->font_bucket = 0;
	resolved->uv_scale[0] = uv_scale[0];
	resolved->uv_scale[1] = uv_scale[1];
	return true;
}

bool TextureManager::ResolveFont(const TextureRequest &request,
	RenderCaptureSegment *capture, ResolvedTexture *resolved)
{
	if (request.logical_handle < 0 || request.logical_handle >= MAX_BITMAPS)
		return false;
	LegacyIdentity observed = {};
	uint32_t dirty_flags = 0;
	if (!ObserveBitmap(request.logical_handle, &observed, &dirty_flags))
		return false;
	observed.mip_count = 1;
	const bool dimensions_changed = font_array_.version == kInvalidId ||
		font_array_.width != observed.width || font_array_.height != observed.height;
	int32_t layer_index = -1;
	if (!dimensions_changed)
		for (size_t i = 0; i < font_array_.layers.size(); ++i)
			if (font_array_.layers[i].handle == request.logical_handle)
			{
				layer_index = static_cast<int32_t>(i);
				break;
			}
	const bool identity_matches = layer_index >= 0 && IdentityEqual(
		font_array_.layers[static_cast<size_t>(layer_index)].identity, observed);
	if (!dimensions_changed && layer_index >= 0 && identity_matches &&
		dirty_flags == 0)
	{
		Version *version = FindVersion(font_array_.version);
		if (!version || !EmitCapturedVersion(version, capture, &resolved->version))
			return false;
		resolved->array_layer = static_cast<uint32_t>(layer_index);
		resolved->font_bucket = 0;
		resolved->uv_scale[0] = resolved->uv_scale[1] = 1.0f;
		return true;
	}

	std::vector<uint8_t> layer_pixels;
	if (!SnapshotFontLayer(request.logical_handle, &observed, &layer_pixels,
		&dirty_flags))
		return false;
	const size_t layer_bytes = static_cast<size_t>(observed.width) *
		observed.height * 4u;
	const TextureVersionId old_version = font_array_.version;
	if (dimensions_changed)
	{
		for (const FontLayer &layer : font_array_.layers)
			if (layer.handle >= 0 && layer.handle < MAX_BITMAPS)
				GameBitmaps[layer.handle].cache_slot = -1;
		font_array_.width = observed.width;
		font_array_.height = observed.height;
		font_array_.layers.clear();
		font_array_.rgba.clear();
		layer_index = -1;
		++font_array_.generation;
	}
	if (layer_index < 0)
	{
		if (font_array_.layers.size() >= config_.maximum_font_layers)
			return false;
		if (layer_bytes > UINT32_MAX || font_array_.layers.size() + 1u >
			UINT32_MAX / layer_bytes)
			return false;
		FontLayer layer = {};
		layer.handle = request.logical_handle;
		layer.generation = 1;
		layer.identity = observed;
		font_array_.layers.push_back(layer);
		font_array_.rgba.insert(font_array_.rgba.end(), layer_pixels.begin(),
			layer_pixels.end());
		layer_index = static_cast<int32_t>(font_array_.layers.size() - 1);
		++font_array_.generation;
	}
	else
	{
		FontLayer &layer = font_array_.layers[static_cast<size_t>(layer_index)];
		if (!identity_matches || (dirty_flags & kTextureBitmapBrandNew) != 0)
			++layer.generation;
		layer.identity = observed;
		std::copy(layer_pixels.begin(), layer_pixels.end(),
			font_array_.rgba.begin() + static_cast<size_t>(layer_index) * layer_bytes);
		++font_array_.generation;
	}

	TextureVersionId recycle_candidate = kInvalidId;
	const Version *old = FindVersion(old_version);
	if (old && old->pending_capture_keys.empty() &&
		!IsReferenced(*capture, old_version) &&
		old->captured.last_use_timeline <= frames_->CompletedTimeline() &&
		old->captured.depth_or_layers == font_array_.layers.size() &&
		old->captured.width == font_array_.width &&
		old->captured.height == font_array_.height)
		recycle_candidate = old_version;
	std::vector<uint8_t> full_snapshot = font_array_.rgba;
	Version *version = CreateVersion(font_array_.width, font_array_.height,
		static_cast<uint32_t>(font_array_.layers.size()), 1,
		font_array_.generation, true, false, std::move(full_snapshot),
		recycle_candidate);
	if (!version)
		return false;
	if (!EmitCapturedVersion(version, capture, &resolved->version))
	{
		versions_.pop_back();
		stats_.logical_versions = versions_.size();
		return false;
	}
	if (old_version != kInvalidId)
		ClearCurrentMapping(old_version);
	version = FindVersion(resolved->version.id);
	version->current_mapping = 1;
	font_array_.version = version->captured.id;
	GameBitmaps[request.logical_handle].cache_slot =
		static_cast<short>(kVulkanCacheCookie);
	observed.cache_cookie = kVulkanCacheCookie;
	font_array_.layers[static_cast<size_t>(layer_index)].identity = observed;
	if (dirty_flags != 0)
		GameBitmaps[request.logical_handle].flags &=
			static_cast<ubyte>(~(BF_CHANGED | BF_BRAND_NEW));
	resolved->array_layer = static_cast<uint32_t>(layer_index);
	resolved->font_bucket = 0;
	resolved->uv_scale[0] = resolved->uv_scale[1] = 1.0f;
	return true;
}

bool TextureManager::Resolve(const TextureRequest &request,
	RenderCaptureSegment *capture, ResolvedTexture *resolved)
{
	if (!ready_ || !capture || !resolved ||
		capture->GetLifecycle() != RenderCaptureSegment::Lifecycle::Capturing)
		return false;
	*resolved = ResolvedTexture();
	const bool array_role = request.role == TextureRole::FontArray ||
		request.role == TextureRole::TerrainBaseArray ||
		request.role == TextureRole::TerrainLightmapArray ||
		request.role == TextureRole::ReservedArray;
	// Descriptor Set 1 is deliberately fully populated on devices without
	// nullDescriptor. Every page reserves slot zero in both typed arrays for
	// these immutable fallbacks, so make both versions part of the capture even
	// when the draw itself only references one texture type.
	ResolvedTexture diagnostic_2d = {};
	ResolvedTexture diagnostic_array = {};
	if (!ResolveDiagnostic(false, capture, &diagnostic_2d) ||
		!ResolveDiagnostic(true, capture, &diagnostic_array))
		return false;
	bool success = false;
	if (request.logical_handle < 0)
		success = ResolveDiagnostic(array_role, capture, resolved);
	else if (request.role == TextureRole::FontArray)
		success = ResolveFont(request, capture, resolved);
	else if (array_role)
		return false; // Retained terrain/other arrays have dedicated builders.
	else if (request.map_type == kLegacyMapTypeLightmap)
		success = ResolveLightmap(request, capture, resolved);
	else if (request.map_type == kLegacyMapTypeBitmap)
		success = ResolveBitmap(request, capture, resolved);
	else
		success = ResolveDiagnostic(array_role, capture, resolved);
	if (success)
		resolved->sampler_index = SelectWorldSamplerIndex(request, array_role);
	return success;
}

bool TextureManager::ResolveTerrainArrays(const TerrainArrayRequest &request,
	RenderCaptureSegment *capture, ResolvedTexture *base,
	ResolvedTexture *lightmap)
{
	if (!ready_ || !capture || !base || !lightmap ||
		!request.base_bitmap_handles || request.base_bitmap_count == 0 ||
		request.base_bitmap_count > platform_->SelectedDevice().properties.
			limits.maxImageArrayLayers)
		return false;
	*base = ResolvedTexture();
	*lightmap = ResolvedTexture();
	auto hash_bytes = [](const std::vector<uint8_t> &bytes, uint64_t seed) {
		uint64_t hash = seed;
		for (uint8_t byte : bytes)
		{
			hash ^= byte;
			hash *= UINT64_C(1099511628211);
		}
		return hash;
	};
	auto hash_u32 = [](uint64_t hash, uint32_t value) {
		for (uint32_t i = 0; i < 4; ++i)
		{
			hash ^= static_cast<uint8_t>(value >> (i * 8u));
			hash *= UINT64_C(1099511628211);
		}
		return hash;
	};
	try
	{
		uint32_t width = 0, height = 0;
		for (uint32_t layer = 0; layer < request.base_bitmap_count; ++layer)
		{
			const int32_t handle = request.base_bitmap_handles[layer];
			LegacyIdentity observed = {};
			uint32_t dirty = 0;
			if (!ObserveBitmap(handle, &observed, &dirty)) return false;
			width = std::max(width, observed.width);
			height = std::max(height, observed.height);
		}
		uint32_t mip_count = 1;
		for (uint32_t w = width, h = height; w > 1 || h > 1;
			w = w > 1 ? w / 2 : 1, h = h > 1 ? h / 2 : 1)
			++mip_count;
		const VkDeviceSize base_bytes = TextureBytes(width, height,
			request.base_bitmap_count, mip_count);
		if (base_bytes == 0 || base_bytes > SIZE_MAX) return false;
		std::vector<uint8_t> base_snapshot;
		base_snapshot.reserve(static_cast<size_t>(base_bytes));
		std::vector<uint8_t> level(static_cast<size_t>(width) * height *
			request.base_bitmap_count * 4u);
		for (uint32_t layer = 0; layer < request.base_bitmap_count; ++layer)
		{
			const int32_t handle = request.base_bitmap_handles[layer];
			const uint32_t source_width = static_cast<uint32_t>(bm_w(handle, 0));
			const uint32_t source_height = static_cast<uint32_t>(bm_h(handle, 0));
			const uint16_t *source = bm_data(handle, 0);
			const int format = bm_format(handle);
			if (!source || source_width == 0 || source_height == 0) return false;
			for (uint32_t y = 0; y < height; ++y)
				for (uint32_t x = 0; x < width; ++x)
				{
					const uint32_t source_x = x * source_width / width;
					const uint32_t source_y = y * source_height / height;
					uint8_t *destination = level.data() +
						((static_cast<size_t>(layer) * height + y) * width + x) * 4u;
					if (format == BITMAP_FORMAT_4444)
						ConvertLegacy4444ToRgba8(source[source_y * source_width + source_x],
							destination);
					else
						ConvertLegacy1555ToRgba8(source[source_y * source_width + source_x],
							destination);
				}
		}
		uint32_t level_width = width, level_height = height;
		base_snapshot.insert(base_snapshot.end(), level.begin(), level.end());
		for (uint32_t mip = 1; mip < mip_count; ++mip)
		{
			const uint32_t next_width = level_width > 1 ? level_width / 2 : 1;
			const uint32_t next_height = level_height > 1 ? level_height / 2 : 1;
			std::vector<uint8_t> next(static_cast<size_t>(next_width) * next_height *
				request.base_bitmap_count * 4u);
			for (uint32_t layer = 0; layer < request.base_bitmap_count; ++layer)
				for (uint32_t y = 0; y < next_height; ++y)
					for (uint32_t x = 0; x < next_width; ++x)
						for (uint32_t channel = 0; channel < 4; ++channel)
						{
							uint32_t sum = 0;
							for (uint32_t oy = 0; oy < 2; ++oy)
								for (uint32_t ox = 0; ox < 2; ++ox)
								{
									const uint32_t sx = std::min(x * 2u + ox,
										level_width - 1u);
									const uint32_t sy = std::min(y * 2u + oy,
										level_height - 1u);
									sum += level[((static_cast<size_t>(layer) *
										level_height + sy) * level_width + sx) * 4u + channel];
								}
							next[((static_cast<size_t>(layer) * next_height + y) *
								next_width + x) * 4u + channel] =
								static_cast<uint8_t>((sum + 2u) >> 2u);
						}
			base_snapshot.insert(base_snapshot.end(), next.begin(), next.end());
			level.swap(next);
			level_width = next_width;
			level_height = next_height;
		}

		const int32_t first_lightmap = request.lightmap_handles[0];
		if (first_lightmap < 0 || first_lightmap >= MAX_LIGHTMAPS ||
			GameLightmaps[first_lightmap].square_res <= 0)
			return false;
		const uint32_t lightmap_size = static_cast<uint32_t>(
			GameLightmaps[first_lightmap].square_res);
		std::vector<uint8_t> lightmap_snapshot(static_cast<size_t>(lightmap_size) *
			lightmap_size * 4u * 4u, 0);
		for (uint32_t layer_index = 0; layer_index < 4; ++layer_index)
		{
			const int32_t handle = request.lightmap_handles[layer_index];
			LegacyIdentity observed = {};
			uint32_t dirty = 0;
			float scale[2] = {};
			if (!ObserveLightmap(handle, &observed, &dirty, scale) ||
				observed.storage_width != lightmap_size)
				return false;
			const uint16_t *source = lm_data(handle);
			for (uint32_t y = 0; y < observed.height; ++y)
				for (uint32_t x = 0; x < observed.width; ++x)
				{
					uint8_t *destination = lightmap_snapshot.data() +
						((static_cast<size_t>(layer_index) * lightmap_size + y) *
						lightmap_size + x) * 4u;
					ConvertLegacy1555ToRgba8(source[y * observed.width + x],
						destination);
				}
		}

		uint64_t base_signature = hash_u32(UINT64_C(1469598103934665603), width);
		base_signature = hash_u32(base_signature, height);
		base_signature = hash_u32(base_signature, request.base_bitmap_count);
		base_signature = hash_u32(base_signature, mip_count);
		base_signature = hash_bytes(base_snapshot, base_signature);
		uint64_t lightmap_signature = hash_u32(UINT64_C(1469598103934665603),
			lightmap_size);
		lightmap_signature = hash_bytes(lightmap_snapshot, lightmap_signature);

		auto resolve_array = [&](std::vector<uint8_t> &&snapshot,
			uint32_t array_width, uint32_t array_height, uint32_t layers,
			uint32_t mips, uint64_t signature, TextureVersionId *cached_id,
			uint64_t *cached_signature, CapturedTextureVersion *captured) {
			Version *version = signature == *cached_signature ?
				FindVersion(*cached_id) : nullptr;
			if (!version)
			{
				const TextureVersionId candidate = *cached_id;
				if (++terrain_array_generation_ == 0) ++terrain_array_generation_;
				version = CreateVersion(array_width, array_height, layers, mips,
					terrain_array_generation_, true, false, std::move(snapshot), candidate);
				if (!version) return false;
				if (*cached_id != kInvalidId) ClearCurrentMapping(*cached_id);
				version->current_mapping = 1;
				*cached_id = version->captured.id;
				*cached_signature = signature;
			}
			return EmitCapturedVersion(version, capture, captured);
		};
		if (!resolve_array(std::move(base_snapshot), width, height,
				request.base_bitmap_count, mip_count, base_signature,
				&terrain_base_version_, &terrain_base_signature_, &base->version) ||
			!resolve_array(std::move(lightmap_snapshot), lightmap_size,
				lightmap_size, 4, 1, lightmap_signature,
				&terrain_lightmap_version_, &terrain_lightmap_signature_,
				&lightmap->version))
			return false;
		TextureRequest sampler_request = {};
		sampler_request.logical_handle = request.base_bitmap_handles[0];
		sampler_request.map_type = kLegacyMapTypeBitmap;
		sampler_request.role = TextureRole::TerrainBaseArray;
		sampler_request.wrap_type = 0;
		sampler_request.filtering = request.filtering;
		sampler_request.mipping = request.mipping;
		base->sampler_index = SelectWorldSamplerIndex(sampler_request, true);
		lightmap->sampler_index = 13u;
		base->uv_scale[0] = base->uv_scale[1] = 1.0f;
		lightmap->uv_scale[0] = lightmap->uv_scale[1] = 1.0f;
		return true;
	}
	catch (const std::bad_alloc &)
	{
		return false;
	}
}

bool TextureManager::EnsureResident(const CapturedTextureVersion &captured,
	const RenderCaptureSegment &capture, ResidentTexture *resident)
{
	if (!ready_ || captured.id == kInvalidId)
		return false;
	Version *version = FindVersion(captured.id);
	if (!version || version->captured.width != captured.width ||
		version->captured.height != captured.height ||
		version->captured.depth_or_layers != captured.depth_or_layers ||
		version->captured.mip_count != captured.mip_count ||
		version->captured.handle_generation != captured.handle_generation ||
		version->captured.content_serial != captured.content_serial)
		return false;
	if (version->allocation.Valid())
		return resident ? GetResident(captured.id, resident) : true;
	const uint8_t *payload = nullptr;
	uint32_t payload_size = 0;
	if (!PayloadRange(capture, captured.immutable_upload_payload, &payload,
		&payload_size) || payload_size != version->byte_size)
		return false;
	FrameContext *frame = frames_->Current();
	if (!frame || !frame->recording || frame->recording_command == VK_NULL_HANDLE)
		return false;
	std::vector<TextureUploadSubresourceLayout> layouts(
		static_cast<size_t>(captured.mip_count) * captured.depth_or_layers);
	const TextureUploadLayoutInput layout_input =
		MakeCapturedTextureUploadLayoutInput(captured);
	const TextureUploadLayoutResult layout_result = BuildCapturedTextureUploadLayout(
		layout_input, layouts.data(), static_cast<uint32_t>(layouts.size()));
	if (layout_result.error != TextureUploadLayoutError::None ||
		layout_result.total_byte_size != payload_size)
		return false;
	const VkPhysicalDeviceLimits &limits =
		platform_->SelectedDevice().properties.limits;
	const VkDeviceSize offset_alignment = std::max<VkDeviceSize>(
		kCapturedTextureUploadPayloadAlignment,
		limits.optimalBufferCopyOffsetAlignment);
	const VkDeviceSize row_alignment = std::max<VkDeviceSize>(
		kCapturedTextureUploadPayloadAlignment,
		limits.optimalBufferCopyRowPitchAlignment);
	std::vector<VkDeviceSize> staging_offsets(layouts.size());
	std::vector<VkDeviceSize> staging_row_pitches(layouts.size());
	VkDeviceSize staging_size = 0;
	for (size_t i = 0; i < layouts.size(); ++i)
	{
		VkDeviceSize row_pitch = 0;
		VkDeviceSize offset = 0;
		if (!AlignUp(layouts[i].row_pitch_bytes, row_alignment, &row_pitch) ||
			!AlignUp(staging_size, offset_alignment, &offset) ||
			row_pitch > std::numeric_limits<VkDeviceSize>::max() /
				layouts[i].height)
			return false;
		const VkDeviceSize subresource_size = row_pitch * layouts[i].height;
		if (subresource_size > std::numeric_limits<VkDeviceSize>::max() - offset)
			return false;
		staging_offsets[i] = offset;
		staging_row_pitches[i] = row_pitch;
		staging_size = offset + subresource_size;
	}
	if (staging_size == 0 || staging_size >
		static_cast<VkDeviceSize>(std::numeric_limits<size_t>::max()))
		return false;
	FrameBufferSlice upload = frames_->AllocateUpload(staging_size,
		offset_alignment);
	if (!upload.Valid() || !upload.mapped)
		return false;
	std::memset(upload.mapped, 0, static_cast<size_t>(staging_size));
	for (size_t i = 0; i < layouts.size(); ++i)
		for (uint32_t row = 0; row < layouts[i].height; ++row)
			std::memcpy(static_cast<uint8_t *>(upload.mapped) +
				staging_offsets[i] + staging_row_pitches[i] * row,
				payload + layouts[i].byte_offset + layouts[i].row_pitch_bytes * row,
				static_cast<size_t>(layouts[i].row_pitch_bytes));
	if (!allocator_->Flush(frame->upload, upload.offset, upload.size))
		return false;
	std::vector<VkBufferImageCopy2> regions(layouts.size());
	for (size_t i = 0; i < layouts.size(); ++i)
	{
		regions[i] = { VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2 };
		regions[i].bufferOffset = upload.offset + staging_offsets[i];
		regions[i].bufferRowLength = staging_row_pitches[i] ==
			layouts[i].row_pitch_bytes ? 0 :
			static_cast<uint32_t>(staging_row_pitches[i] / 4u);
		regions[i].bufferImageHeight = 0;
		regions[i].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		regions[i].imageSubresource.mipLevel = layouts[i].mip_level;
		regions[i].imageSubresource.baseArrayLayer = layouts[i].array_layer;
		regions[i].imageSubresource.layerCount = 1;
		regions[i].imageExtent = { layouts[i].width, layouts[i].height, 1 };
	}

	Version *candidate = FindVersion(version->recycle_candidate);
	if (candidate && candidate->allocation.Valid() && !candidate->current_mapping &&
		candidate->captured.last_use_timeline <= frames_->CompletedTimeline() &&
		candidate->captured.width == captured.width &&
		candidate->captured.height == captured.height &&
		candidate->captured.depth_or_layers == captured.depth_or_layers &&
		candidate->captured.mip_count == captured.mip_count &&
		candidate->array_texture == version->array_texture)
	{
		version->allocation = candidate->allocation;
		candidate->allocation = AllocatedImage();
		candidate->captured.residency = static_cast<uint32_t>(
			TextureResidencyState::Retired);
		++stats_.recycled_images;
	}
	else
	{
		ImageCreateRequest image = {};
		image.view_type = version->array_texture ? VK_IMAGE_VIEW_TYPE_2D_ARRAY :
			VK_IMAGE_VIEW_TYPE_2D;
		image.format = VK_FORMAT_R8G8B8A8_UNORM;
		image.extent = { captured.width, captured.height, 1 };
		image.mip_levels = captured.mip_count;
		image.array_layers = captured.depth_or_layers;
		image.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
			VK_IMAGE_USAGE_SAMPLED_BIT;
		image.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		image.debug_name = version->diagnostic ? "vk.texture.diagnostic" :
			"vk.texture.version";
		if (!allocator_->CreateImage(image, &version->allocation))
			return false;
		++stats_.resident_images;
		stats_.resident_bytes += version->byte_size;
	}

	BufferUse host_write = {};
	host_write.stages = VK_PIPELINE_STAGE_2_HOST_BIT;
	host_write.access = VK_ACCESS_2_HOST_WRITE_BIT;
	host_write.queue_family = platform_->GraphicsQueueFamily();
	host_write.intent = ResourceIntent::Write;
	state_tracker_->ImportBuffer(upload.buffer, upload.offset, upload.size,
		host_write);
	BufferUse transfer_read = {};
	transfer_read.stages = VK_PIPELINE_STAGE_2_COPY_BIT;
	transfer_read.access = VK_ACCESS_2_TRANSFER_READ_BIT;
	transfer_read.queue_family = platform_->GraphicsQueueFamily();
	transfer_read.intent = ResourceIntent::Read;
	if (!state_tracker_->UseBuffer(upload.buffer, upload.offset, upload.size,
		transfer_read))
		return false;
	VkImageSubresourceRange full_range = {};
	full_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	full_range.levelCount = captured.mip_count;
	full_range.layerCount = captured.depth_or_layers;
	ImageUse transfer_write = {};
	transfer_write.stages = VK_PIPELINE_STAGE_2_COPY_BIT;
	transfer_write.access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	transfer_write.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	transfer_write.queue_family = platform_->GraphicsQueueFamily();
	transfer_write.intent = ResourceIntent::Write;
	if (!state_tracker_->UseImage(version->allocation.handle, full_range,
		transfer_write))
		return false;
	state_tracker_->Flush(frame->recording_command);
	VkCopyBufferToImageInfo2 copy = {
		VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2
	};
	copy.srcBuffer = upload.buffer;
	copy.dstImage = version->allocation.handle;
	copy.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	copy.regionCount = static_cast<uint32_t>(regions.size());
	copy.pRegions = regions.data();
	vkCmdCopyBufferToImage2(frame->recording_command, &copy);
	ImageUse sampled_read = {};
	sampled_read.stages = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	sampled_read.access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
	sampled_read.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	sampled_read.queue_family = platform_->GraphicsQueueFamily();
	sampled_read.intent = ResourceIntent::Read;
	if (!state_tracker_->UseImage(version->allocation.handle, full_range,
		sampled_read))
		return false;
	state_tracker_->Flush(frame->recording_command);
	version->captured.residency = static_cast<uint32_t>(version->current_mapping ||
		version->diagnostic ? TextureResidencyState::Resident :
		TextureResidencyState::Evictable);
	if (!version->diagnostic)
		version->cpu_snapshot.clear();
	++stats_.uploads;
	stats_.upload_bytes += payload_size;
	if (config_.resident_byte_budget != 0 &&
		stats_.resident_bytes > config_.resident_byte_budget)
		Evict(stats_.resident_bytes - config_.resident_byte_budget,
			frames_->CompletedTimeline());
	return resident ? GetResident(captured.id, resident) : true;
}

bool TextureManager::GetResident(TextureVersionId id,
	ResidentTexture *resident) const
{
	if (!resident)
		return false;
	*resident = ResidentTexture();
	const Version *version = FindVersion(id);
	if (!version || !version->allocation.Valid())
		return false;
	resident->id = id;
	resident->image = version->allocation.handle;
	resident->view = version->allocation.view;
	resident->format = version->allocation.format;
	resident->extent = version->allocation.extent;
	resident->mip_count = version->captured.mip_count;
	resident->array_texture = version->array_texture;
	resident->last_use_timeline = version->captured.last_use_timeline;
	return true;
}

bool TextureManager::Pin(TextureVersionId id, uint64_t submission_timeline)
{
	Version *version = FindVersion(id);
	if (!version || !version->allocation.Valid() || submission_timeline == 0)
		return false;
	version->captured.last_use_timeline = std::max(
		version->captured.last_use_timeline, submission_timeline);
	version->allocation.last_use_timeline = std::max(
		version->allocation.last_use_timeline, submission_timeline);
	version->pending_capture_keys.clear();
	return true;
}

bool TextureManager::PinCapture(const RenderCaptureSegment &capture,
	uint64_t submission_timeline)
{
	if (!ready_ || submission_timeline == 0)
		return false;
	const uint64_t key = CaptureKey(capture);
	bool success = true;
	for (const CapturedTextureVersion &captured : capture.TextureVersions())
	{
		Version *version = FindVersion(captured.id);
		if (!version || !version->allocation.Valid())
		{
			success = false;
			continue;
		}
		version->captured.last_use_timeline = std::max(
			version->captured.last_use_timeline, submission_timeline);
		version->allocation.last_use_timeline = std::max(
			version->allocation.last_use_timeline, submission_timeline);
		version->pending_capture_keys.erase(std::remove(
			version->pending_capture_keys.begin(),
			version->pending_capture_keys.end(), key),
			version->pending_capture_keys.end());
	}
	return success;
}

void TextureManager::DiscardCapture(const RenderCaptureSegment &capture)
{
	const uint64_t key = CaptureKey(capture);
	for (const CapturedTextureVersion &captured : capture.TextureVersions())
	{
		Version *version = FindVersion(captured.id);
		if (!version)
			continue;
		version->pending_capture_keys.erase(std::remove(
			version->pending_capture_keys.begin(),
			version->pending_capture_keys.end(), key),
			version->pending_capture_keys.end());
		if (!version->current_mapping && version->allocation.Valid())
			version->captured.residency = static_cast<uint32_t>(
				TextureResidencyState::Evictable);
		else if (!version->current_mapping && !version->allocation.Valid() &&
			!version->diagnostic && version->pending_capture_keys.empty())
		{
			version->cpu_snapshot.clear();
			version->captured.residency = static_cast<uint32_t>(
				TextureResidencyState::Retired);
		}
	}
}

void TextureManager::RetireVersion(Version *version,
	uint64_t completed_timeline)
{
	if (!version || !version->allocation.Valid())
		return;
	const VkDeviceSize bytes = version->byte_size;
	allocator_->RetireImage(&version->allocation,
		version->captured.last_use_timeline);
	version->captured.residency = static_cast<uint32_t>(
		TextureResidencyState::Retired);
	if (stats_.resident_images > 0)
		--stats_.resident_images;
	stats_.resident_bytes = stats_.resident_bytes > bytes ?
		stats_.resident_bytes - bytes : 0;
	if (version->captured.last_use_timeline <= completed_timeline)
		allocator_->Reclaim(completed_timeline);
}

void TextureManager::Collect(uint64_t completed_timeline)
{
	if (!ready_)
		return;
	allocator_->Reclaim(completed_timeline);
	if (config_.resident_byte_budget != 0 &&
		stats_.resident_bytes > config_.resident_byte_budget)
		Evict(stats_.resident_bytes - config_.resident_byte_budget,
			completed_timeline);
}

VkDeviceSize TextureManager::Evict(VkDeviceSize bytes_to_free,
	uint64_t completed_timeline)
{
	VkDeviceSize freed = 0;
	while (freed < bytes_to_free)
	{
		Version *oldest = nullptr;
		for (Version &version : versions_)
		{
			if (!version.allocation.Valid() || version.diagnostic ||
				!version.pending_capture_keys.empty() ||
				version.captured.last_use_timeline >
				completed_timeline)
				continue;
			// Prefer versions which are already detached from a logical handle,
			// then fall back to invalidating the least-recent completed mapping.
			if (!oldest || (oldest->current_mapping && !version.current_mapping) ||
				(oldest->current_mapping == version.current_mapping &&
				 version.last_resolve_serial < oldest->last_resolve_serial))
				oldest = &version;
		}
		if (!oldest)
			break;
		const VkDeviceSize bytes = oldest->byte_size;
		InvalidateMappingsTo(oldest->captured.id);
		RetireVersion(oldest, completed_timeline);
		freed += bytes;
		++stats_.evicted_images;
	}
	return freed;
}

void TextureManager::Invalidate(TextureMappingInvalidationReason reason,
	uint64_t completed_timeline)
{
	if (!ready_ || static_cast<uint32_t>(reason) >=
		static_cast<uint32_t>(TextureMappingInvalidationReason::Count))
		return;
	for (size_t i = 0; i < bitmap_mappings_.size(); ++i)
	{
		ClearCurrentMapping(bitmap_mappings_[i].version);
		if (bitmap_mappings_[i].version != kInvalidId)
			GameBitmaps[i].cache_slot = -1;
		bitmap_mappings_[i] = LogicalMapping();
	}
	for (size_t i = 0; i < lightmap_mappings_.size(); ++i)
	{
		ClearCurrentMapping(lightmap_mappings_[i].version);
		if (lightmap_mappings_[i].version != kInvalidId)
			GameLightmaps[i].cache_slot = -1;
		lightmap_mappings_[i] = LogicalMapping();
	}
	ClearCurrentMapping(font_array_.version);
	for (const FontLayer &layer : font_array_.layers)
		if (layer.handle >= 0 && layer.handle < MAX_BITMAPS)
			GameBitmaps[layer.handle].cache_slot = -1;
	font_array_ = FontArray();
	ClearCurrentMapping(terrain_base_version_);
	ClearCurrentMapping(terrain_lightmap_version_);
	terrain_base_version_ = terrain_lightmap_version_ = kInvalidId;
	terrain_base_signature_ = terrain_lightmap_signature_ = 0;
	for (Version &version : versions_)
		if (!version.diagnostic && !version.current_mapping &&
			version.pending_capture_keys.empty() &&
			version.allocation.Valid())
			RetireVersion(&version, completed_timeline);
	allocator_->Reclaim(completed_timeline);
}

bool TextureManager::ReleaseLogicalHandle(int32_t logical_handle,
	uint32_t map_type, uint64_t completed_timeline)
{
	if (!ready_ || logical_handle < 0)
		return false;
	LogicalMapping *mapping = nullptr;
	if (map_type == kLegacyMapTypeBitmap && logical_handle < MAX_BITMAPS)
		mapping = &bitmap_mappings_[logical_handle];
	else if (map_type == kLegacyMapTypeLightmap &&
		logical_handle < MAX_LIGHTMAPS)
		mapping = &lightmap_mappings_[logical_handle];
	else
		return false;
	Version *version = FindVersion(mapping->version);
	ClearCurrentMapping(mapping->version);
	*mapping = LogicalMapping();
	if (map_type == kLegacyMapTypeBitmap)
		GameBitmaps[logical_handle].cache_slot = -1;
	else
		GameLightmaps[logical_handle].cache_slot = -1;
	// A captured-but-unsubmitted draw may still name this version. Physical
	// retirement is left to Collect/Evict after PinCapture or DiscardCapture.
	if (version && version->allocation.Valid() &&
		version->pending_capture_keys.empty() &&
		version->captured.last_use_timeline <= completed_timeline)
		RetireVersion(version, completed_timeline);
	allocator_->Reclaim(completed_timeline);
	return true;
}

bool TextureManager::CreateSamplers()
{
	if (!platform_ || !platform_->Ready() ||
		kSamplerContractCount != static_cast<size_t>(SamplerSemantic::Count))
		return false;
	for (size_t i = 0; i < kSamplerContractCount; ++i)
	{
		const SamplerContract &contract = kSamplerContract[i];
		const uint32_t semantic = static_cast<uint32_t>(contract.semantic);
		if (semantic >= static_cast<uint32_t>(SamplerSemantic::Count) ||
			contract.anisotropy_enabled != 0)
			return false;
		VkSamplerCreateInfo create = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		create.magFilter = VulkanFilter(contract.magnification);
		create.minFilter = VulkanFilter(contract.minification);
		create.mipmapMode = VulkanMipMode(contract.mip);
		create.addressModeU = VulkanAddress(contract.address_u);
		create.addressModeV = VulkanAddress(contract.address_v);
		create.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		create.mipLodBias = 0.0f;
		create.anisotropyEnable = VK_FALSE;
		create.maxAnisotropy = 1.0f;
		create.compareEnable = VK_FALSE;
		create.compareOp = VK_COMPARE_OP_ALWAYS;
		create.minLod = 0.0f;
		create.maxLod = contract.mip == SamplerMipMode::Disabled ? 0.0f :
			VK_LOD_CLAMP_NONE;
		create.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
		create.unnormalizedCoordinates = VK_FALSE;
		const VkResult result = vkCreateSampler(platform_->Device(), &create,
			nullptr, &samplers_[semantic]);
		platform_->NotifyDeviceResult(result);
		if (result != VK_SUCCESS)
		{
			DestroySamplers();
			return false;
		}
	}
	for (uint32_t i = 0; i < 12; ++i)
		world_samplers_[i] = samplers_[i];
	world_samplers_[12] = Sampler(SamplerSemantic::LightmapClamp);
	world_samplers_[13] = Sampler(SamplerSemantic::LightmapRepeat);
	world_samplers_[14] = Sampler(SamplerSemantic::Font);
	world_samplers_[15] = Sampler(SamplerSemantic::GenericClampPointNoMip);
	for (uint32_t i = 0; i < 12; ++i)
		world_samplers_[16 + i] = samplers_[i];
	for (uint32_t i = 28; i < kWorldSamplerCount; ++i)
		world_samplers_[i] = Sampler(SamplerSemantic::GenericClampPointNoMip);
	return true;
}

void TextureManager::DestroySamplers() noexcept
{
	if (platform_ && platform_->Device() != VK_NULL_HANDLE)
		for (VkSampler &sampler : samplers_)
		{
			if (sampler != VK_NULL_HANDLE)
				vkDestroySampler(platform_->Device(), sampler, nullptr);
			sampler = VK_NULL_HANDLE;
		}
	else
		std::memset(samplers_, 0, sizeof(samplers_));
	std::memset(world_samplers_, 0, sizeof(world_samplers_));
}

VkSampler TextureManager::Sampler(SamplerSemantic semantic) const noexcept
{
	const uint32_t index = static_cast<uint32_t>(semantic);
	return index < static_cast<uint32_t>(SamplerSemantic::Count) ?
		samplers_[index] : VK_NULL_HANDLE;
}

VkSampler TextureManager::WorldSampler(uint32_t index) const noexcept
{
	return index < kWorldSamplerCount ? world_samplers_[index] : VK_NULL_HANDLE;
}

SamplerSemantic TextureManager::SelectSampler(
	const TextureRequest &request) const noexcept
{
	if (request.role == TextureRole::FontArray)
		return SamplerSemantic::Font;
	if (request.map_type == kLegacyMapTypeLightmap)
	{
		const bool repeat = request.logical_handle >= 0 &&
			request.logical_handle < MAX_LIGHTMAPS &&
			(GameLightmaps[request.logical_handle].flags & LF_WRAP) != 0;
		return repeat ? SamplerSemantic::LightmapRepeat :
			SamplerSemantic::LightmapClamp;
	}
	const uint32_t wrap = request.wrap_type < 3 ? request.wrap_type : 1;
	const uint32_t offset = (request.mipping ? 2u : 0u) +
		(request.filtering ? 1u : 0u);
	return static_cast<SamplerSemantic>(wrap * 4u + offset);
}

uint32_t TextureManager::SelectWorldSamplerIndex(
	const TextureRequest &request, bool array_texture) const noexcept
{
	const SamplerSemantic semantic = SelectSampler(request);
	const uint32_t index = static_cast<uint32_t>(semantic);
	if (index < 12)
		return index + (array_texture ? 16u : 0u);
	if (semantic == SamplerSemantic::LightmapClamp)
		return 12;
	if (semantic == SamplerSemantic::LightmapRepeat)
		return 13;
	if (semantic == SamplerSemantic::Font)
		return 14;
	return 15;
}

} // namespace vk
} // namespace render
} // namespace piccu
