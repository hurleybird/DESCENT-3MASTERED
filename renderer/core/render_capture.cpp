#include "render_capture.h"

#include <assert.h>
#include <algorithm>
#include <cmath>
#include <limits.h>
#include <stdint.h>
#include <string.h>

namespace piccu
{
namespace render
{
namespace
{

bool EqualState(const CapturedShaderRasterState &a,
	const CapturedShaderRasterState &b)
{
	return memcmp(&a.shader, &b.shader, sizeof(a.shader)) == 0 &&
		a.target_layout == b.target_layout &&
		a.sample_count == b.sample_count &&
		a.mrt_write_mask == b.mrt_write_mask &&
		a.raster_family == b.raster_family &&
		a.cull_enabled == b.cull_enabled &&
		a.front_face == b.front_face &&
		a.depth_test_enabled == b.depth_test_enabled &&
		a.depth_write_enabled == b.depth_write_enabled &&
		a.depth_compare == b.depth_compare &&
		a.depth_bias_enabled == b.depth_bias_enabled &&
		memcmp(&a.depth_bias_factor, &b.depth_bias_factor, sizeof(float)) == 0 &&
		memcmp(&a.depth_bias_units, &b.depth_bias_units, sizeof(float)) == 0 &&
		a.viewport == b.viewport && a.scissor == b.scissor;
}

bool EqualMaterial(const CapturedMaterial &a, const CapturedMaterial &b)
{
	return memcmp(&a, &b, sizeof(a)) == 0;
}

bool EqualTransform(const CapturedTransform &a, const CapturedTransform &b)
{
	return memcmp(&a, &b, sizeof(a)) == 0;
}

bool EqualTextureVersion(const CapturedTextureVersion &a, const CapturedTextureVersion &b)
{
	return a.id == b.id && a.format == b.format && a.width == b.width &&
		a.height == b.height && a.depth_or_layers == b.depth_or_layers &&
		a.mip_count == b.mip_count && a.handle_generation == b.handle_generation &&
		a.content_serial == b.content_serial && a.last_use_timeline == b.last_use_timeline &&
		a.residency == b.residency &&
		a.immutable_upload_payload == b.immutable_upload_payload;
}

uint32_t AlignUp(uint32_t value, uint32_t alignment)
{
	const uint32_t mask = alignment - 1;
	return (value + mask) & ~mask;
}

bool IsPowerOfTwo(uint32_t value)
{
	return value != 0 && (value & (value - 1)) == 0;
}

uint64_t HashBytes(uint64_t hash, const void *data, size_t byte_size)
{
	const uint8_t *bytes = static_cast<const uint8_t *>(data);
	for (size_t i = 0; i < byte_size; ++i)
	{
		hash ^= bytes[i];
		hash *= UINT64_C(1099511628211);
	}
	return hash;
}

uint64_t HashState(const CapturedShaderRasterState &state)
{
	uint64_t hash = UINT64_C(1469598103934665603);
	hash = HashBytes(hash, &state.shader, sizeof(state.shader));
	hash = HashBytes(hash, &state.target_layout, sizeof(state.target_layout));
	hash = HashBytes(hash, &state.sample_count, sizeof(state.sample_count));
	hash = HashBytes(hash, &state.mrt_write_mask, sizeof(state.mrt_write_mask));
	hash = HashBytes(hash, &state.raster_family, sizeof(state.raster_family));
	hash = HashBytes(hash, &state.cull_enabled, sizeof(state.cull_enabled));
	hash = HashBytes(hash, &state.front_face, sizeof(state.front_face));
	hash = HashBytes(hash, &state.depth_test_enabled, sizeof(state.depth_test_enabled));
	hash = HashBytes(hash, &state.depth_write_enabled, sizeof(state.depth_write_enabled));
	hash = HashBytes(hash, &state.depth_compare, sizeof(state.depth_compare));
	hash = HashBytes(hash, &state.depth_bias_enabled, sizeof(state.depth_bias_enabled));
	hash = HashBytes(hash, &state.depth_bias_factor, sizeof(state.depth_bias_factor));
	hash = HashBytes(hash, &state.depth_bias_units, sizeof(state.depth_bias_units));
	hash = HashBytes(hash, &state.viewport, sizeof(state.viewport));
	return HashBytes(hash, &state.scissor, sizeof(state.scissor));
}

size_t HashCapacity(size_t minimum_entries)
{
	size_t capacity = 16;
	const size_t target = minimum_entries > SIZE_MAX / 2 ? SIZE_MAX : minimum_entries * 2;
	while (capacity < target && capacity <= SIZE_MAX / 2)
		capacity *= 2;
	return capacity;
}

template <typename T>
bool SourceAliasesVector(const void *source, uint64_t source_bytes,
	const std::vector<T> &storage)
{
	if (!source || source_bytes == 0 || storage.empty())
		return false;
	const uintptr_t source_begin = reinterpret_cast<uintptr_t>(source);
	const uintptr_t storage_begin = reinterpret_cast<uintptr_t>(storage.data());
	const uint64_t storage_bytes = uint64_t(storage.size()) * sizeof(T);
	if (source_bytes > UINTPTR_MAX - source_begin || storage_bytes > UINTPTR_MAX - storage_begin)
		return true;
	const uintptr_t source_end = source_begin + static_cast<uintptr_t>(source_bytes);
	const uintptr_t storage_end = storage_begin + static_cast<uintptr_t>(storage_bytes);
	return source_begin < storage_end && storage_begin < source_end;
}

bool IsPayloadShapeValid(CapturedPayloadSemantic semantic, uint32_t byte_size,
	uint32_t alignment)
{
	if (byte_size == 0)
		return false;
	switch (semantic)
	{
	case kPayloadPerspectiveVertices:
		return alignment == 16 && byte_size % sizeof(PerspectiveVertexPayload) == 0;
	case kPayloadMotionVertices:
		return alignment == 16 && byte_size % sizeof(MotionVertexPayload) == 0;
	case kPayloadSpecularVertices:
		return alignment == 16 && byte_size % sizeof(SpecularVertexPayload) == 0;
	case kPayloadDynamicLights:
		return alignment == 16 && byte_size % sizeof(GpuDynamicLight) == 0 &&
			byte_size / sizeof(GpuDynamicLight) <= kMaxTerrainDynamicLights;
	case kPayloadSpecularBlock:
		return alignment == 16 && byte_size == sizeof(GpuSpecularBlock);
	case kPayloadWorldAux:
		return alignment == 16 && byte_size % sizeof(GpuWorldAux) == 0;
	case kPayloadCockpitMotion:
		return alignment == 16 && byte_size % sizeof(MotionVertexPayload) == 0;
	case kPayloadTerrainCells:
		return alignment == 16 && byte_size % sizeof(TerrainEmitterCell) == 0;
	case kPayloadTerrainWorkList:
		return alignment == 16 && byte_size % sizeof(TerrainWorkItem) == 0;
	case kPayloadCockpitBacking:
		return alignment == 4 && byte_size == sizeof(CapturedCockpitBackingEffect);
	case kPayloadSoftDepthScalar:
		return alignment == 4 && byte_size == sizeof(float);
	case kPayloadGeometryAux:
	case kPayloadTextureUpload:
		return alignment == 4 && byte_size % 4 == 0;
	case kPayloadTerrainBatches:
		return alignment == 16 && byte_size % sizeof(TerrainBatchInput) == 0;
	case kPayloadTerrainViewInput:
		return alignment == 16 && byte_size == sizeof(TerrainViewInput);
	default:
		return false;
	}
}

bool SpanFits(const Span32 &span, size_t size)
{
	return span.offset != kInvalidId && span.offset <= size &&
		span.count <= size - span.offset;
}

bool IsCanonicalRetainedSpan(const Span32 &span)
{
	if (span.count == 0)
		return span.offset == kInvalidId;
	return span.offset != kInvalidId && span.offset <= UINT32_MAX - span.count;
}

uint32_t FormatBytesPerTexel(RenderFormat format)
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

uint64_t TextureUploadByteSize(const CapturedTextureVersion &version)
{
	const uint32_t bytes_per_texel = FormatBytesPerTexel(version.format);
	if (bytes_per_texel == 0)
		return UINT64_MAX;
	uint64_t total = 0;
	uint32_t width = version.width;
	uint32_t height = version.height;
	for (uint32_t mip = 0; mip < version.mip_count; ++mip)
	{
		const uint64_t level = uint64_t(width) * height *
			version.depth_or_layers * bytes_per_texel;
		if (level > UINT64_MAX - total)
			return UINT64_MAX;
		total += level;
		width = std::max(1u, width / 2u);
		height = std::max(1u, height / 2u);
	}
	return total;
}

bool RectHasPositiveArea(const LogicalRect &rect)
{
	return rect.width > 0 && rect.height > 0;
}

bool RectFitsInside(const LogicalRect &inner, const LogicalRect &outer)
{
	if (!RectHasPositiveArea(inner) || !RectHasPositiveArea(outer))
		return false;
	const int64_t inner_right = int64_t(inner.x) + inner.width;
	const int64_t inner_bottom = int64_t(inner.y) + inner.height;
	const int64_t outer_right = int64_t(outer.x) + outer.width;
	const int64_t outer_bottom = int64_t(outer.y) + outer.height;
	return inner.x >= outer.x && inner.y >= outer.y &&
		inner_right <= outer_right && inner_bottom <= outer_bottom;
}

bool IsFiniteUnitFloat(float value)
{
	return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
}

} // namespace

RenderCaptureSegment::RenderCaptureSegment()
	: presented_frame_serial_(0), segment_serial_(0), next_command_serial_(0),
	  lifecycle_(Lifecycle::Empty), start_kind_(CaptureSegmentStartKind::Fresh),
	  continuation_state_{}
{
}

void RenderCaptureSegment::Reserve(const CaptureReserve &reserve)
{
	if (lifecycle_ == Lifecycle::Frozen)
		return;
	commands_.reserve(reserve.commands);
	states_.reserve(reserve.states);
	RebuildStateHash(reserve.states);
	materials_.reserve(reserve.materials);
	RebuildMaterialHash(reserve.materials);
	texture_versions_.reserve(reserve.texture_versions);
	transforms_.reserve(reserve.transforms);
	RebuildTransformHash(reserve.transforms);
	views_.reserve(reserve.views);
	viewports_.reserve(reserve.viewports);
	target_layouts_.reserve(reserve.target_layouts);
	target_signatures_.reserve(reserve.target_signatures);
	target_versions_.reserve(reserve.target_versions);
	present_rects_.reserve(reserve.present_rects);
	wsi_signatures_.reserve(reserve.wsi_signatures);
	payload_bindings_.reserve(reserve.payload_bindings);
	payload_records_.reserve(reserve.payload_records);
	external_payloads_.reserve(reserve.payload_records);
	payload_bytes_.reserve(reserve.payload_bytes);
	stream_vertices_.reserve(reserve.stream_vertices);
	stream_indices_.reserve(reserve.stream_indices);
	stream_payload_words_.reserve(reserve.stream_payload_words);
}

bool RenderCaptureSegment::Reset(uint32_t presented_frame_serial,
	uint32_t segment_serial, uint64_t first_command_serial)
{
	if (lifecycle_ != Lifecycle::Empty && lifecycle_ != Lifecycle::Compiled)
		return false;
	presented_frame_serial_ = presented_frame_serial;
	segment_serial_ = segment_serial;
	next_command_serial_ = first_command_serial;
	lifecycle_ = Lifecycle::Capturing;
	start_kind_ = CaptureSegmentStartKind::Fresh;
	continuation_state_ = {};
	commands_.clear();
	states_.clear();
	std::fill(state_hash_slots_.begin(), state_hash_slots_.end(), kInvalidId);
	materials_.clear();
	std::fill(material_hash_slots_.begin(), material_hash_slots_.end(), kInvalidId);
	texture_versions_.clear();
	transforms_.clear();
	std::fill(transform_hash_slots_.begin(), transform_hash_slots_.end(), kInvalidId);
	views_.clear();
	viewports_.clear();
	target_layouts_.clear();
	target_signatures_.clear();
	target_versions_.clear();
	present_rects_.clear();
	wsi_signatures_.clear();
	payload_bindings_.clear();
	payload_records_.clear();
	external_payloads_.clear();
	payload_bytes_.clear();
	stream_vertices_.clear();
	stream_indices_.clear();
	stream_payload_words_.clear();
	return true;
}

bool RenderCaptureSegment::ResetContinuation(uint32_t presented_frame_serial,
	uint32_t segment_serial, uint64_t first_command_serial,
	const CaptureContinuationState &state)
{
	if (!Reset(presented_frame_serial, segment_serial, first_command_serial))
		return false;
	start_kind_ = CaptureSegmentStartKind::ContinuationAfterReadback;
	continuation_state_ = state;
	return true;
}

bool RenderCaptureSegment::ResetContinuationFrom(
	const RenderCaptureSegment &prefix, uint32_t segment_serial,
	uint64_t first_command_serial, const CaptureContinuationState &state)
{
	if (&prefix == this ||
		(prefix.lifecycle_ != Lifecycle::Frozen &&
		 prefix.lifecycle_ != Lifecycle::Compiled) ||
		!ResetContinuation(prefix.presented_frame_serial_, segment_serial,
			first_command_serial, state))
		return false;

	states_ = prefix.states_;
	materials_ = prefix.materials_;
	texture_versions_ = prefix.texture_versions_;
	transforms_ = prefix.transforms_;
	views_ = prefix.views_;
	viewports_ = prefix.viewports_;
	target_layouts_ = prefix.target_layouts_;
	target_signatures_ = prefix.target_signatures_;
	target_versions_ = prefix.target_versions_;
	present_rects_ = prefix.present_rects_;
	wsi_signatures_ = prefix.wsi_signatures_;
	payload_bindings_ = prefix.payload_bindings_;
	payload_records_ = prefix.payload_records_;
	external_payloads_ = prefix.external_payloads_;
	payload_bytes_ = prefix.payload_bytes_;
	RebuildStateHash(states_.size() * 2 + 1);
	RebuildMaterialHash(materials_.size() * 2 + 1);
	RebuildTransformHash(transforms_.size() * 2 + 1);
	return true;
}

bool RenderCaptureSegment::CanMutate() const
{
	assert(lifecycle_ == Lifecycle::Capturing &&
		"attempted to mutate a render capture segment outside capture");
	return lifecycle_ == Lifecycle::Capturing;
}

void RenderCaptureSegment::RebuildStateHash(size_t minimum_entries)
{
	const size_t capacity = HashCapacity(minimum_entries);
	state_hash_slots_.assign(capacity, kInvalidId);
	for (size_t i = 0; i < states_.size(); ++i)
	{
		size_t slot = static_cast<size_t>(HashState(states_[i])) & (capacity - 1);
		while (state_hash_slots_[slot] != kInvalidId)
			slot = (slot + 1) & (capacity - 1);
		state_hash_slots_[slot] = static_cast<uint32_t>(i);
	}
}

void RenderCaptureSegment::RebuildMaterialHash(size_t minimum_entries)
{
	const size_t capacity = HashCapacity(minimum_entries);
	material_hash_slots_.assign(capacity, kInvalidId);
	for (size_t i = 0; i < materials_.size(); ++i)
	{
		size_t slot = static_cast<size_t>(HashBytes(UINT64_C(1469598103934665603),
			&materials_[i], sizeof(materials_[i]))) & (capacity - 1);
		while (material_hash_slots_[slot] != kInvalidId)
			slot = (slot + 1) & (capacity - 1);
		material_hash_slots_[slot] = static_cast<uint32_t>(i);
	}
}

void RenderCaptureSegment::RebuildTransformHash(size_t minimum_entries)
{
	const size_t capacity = HashCapacity(minimum_entries);
	transform_hash_slots_.assign(capacity, kInvalidId);
	for (size_t i = 0; i < transforms_.size(); ++i)
	{
		size_t slot = static_cast<size_t>(HashBytes(UINT64_C(1469598103934665603),
			&transforms_[i], sizeof(transforms_[i]))) & (capacity - 1);
		while (transform_hash_slots_[slot] != kInvalidId)
			slot = (slot + 1) & (capacity - 1);
		transform_hash_slots_[slot] = static_cast<uint32_t>(i);
	}
}

StateId RenderCaptureSegment::InternState(const CapturedShaderRasterState &state)
{
	if (!CanMutate())
		return kInvalidId;
	if (states_.size() >= static_cast<size_t>(UINT32_MAX))
		return kInvalidId;
	if (state_hash_slots_.empty() || (states_.size() + 1) * 2 > state_hash_slots_.size())
		RebuildStateHash(states_.size() + 1);
	size_t slot = static_cast<size_t>(HashState(state)) & (state_hash_slots_.size() - 1);
	while (state_hash_slots_[slot] != kInvalidId)
	{
		const uint32_t candidate = state_hash_slots_[slot];
		if (EqualState(states_[candidate], state))
			return candidate;
		slot = (slot + 1) & (state_hash_slots_.size() - 1);
	}
	states_.push_back(state);
	const StateId id = static_cast<StateId>(states_.size() - 1);
	state_hash_slots_[slot] = id;
	return id;
}

MaterialRef RenderCaptureSegment::InternMaterial(const CapturedMaterial &material)
{
	if (!CanMutate())
		return kInvalidId;
	if (materials_.size() >= static_cast<size_t>(UINT32_MAX))
		return kInvalidId;
	if (material_hash_slots_.empty() || (materials_.size() + 1) * 2 > material_hash_slots_.size())
		RebuildMaterialHash(materials_.size() + 1);
	const uint64_t hash = HashBytes(UINT64_C(1469598103934665603), &material, sizeof(material));
	size_t slot = static_cast<size_t>(hash) & (material_hash_slots_.size() - 1);
	while (material_hash_slots_[slot] != kInvalidId)
	{
		const uint32_t candidate = material_hash_slots_[slot];
		if (EqualMaterial(materials_[candidate], material))
			return candidate;
		slot = (slot + 1) & (material_hash_slots_.size() - 1);
	}
	materials_.push_back(material);
	const MaterialRef id = static_cast<MaterialRef>(materials_.size() - 1);
	material_hash_slots_[slot] = id;
	return id;
}

bool RenderCaptureSegment::RegisterTextureVersion(const CapturedTextureVersion &version)
{
	if (!CanMutate() || version.id == kInvalidId || version.width == 0 ||
		version.height == 0 || version.depth_or_layers == 0 || version.mip_count == 0)
		return false;
	for (size_t i = 0; i < texture_versions_.size(); ++i)
	{
		if (texture_versions_[i].id == version.id)
			return EqualTextureVersion(texture_versions_[i], version);
	}
	texture_versions_.push_back(version);
	return true;
}

TransformId RenderCaptureSegment::InternTransform(const CapturedTransform &transform)
{
	if (!CanMutate())
		return kInvalidId;
	if (transforms_.size() >= static_cast<size_t>(UINT32_MAX))
		return kInvalidId;
	if (transform_hash_slots_.empty() || (transforms_.size() + 1) * 2 > transform_hash_slots_.size())
		RebuildTransformHash(transforms_.size() + 1);
	const uint64_t hash = HashBytes(UINT64_C(1469598103934665603), &transform, sizeof(transform));
	size_t slot = static_cast<size_t>(hash) & (transform_hash_slots_.size() - 1);
	while (transform_hash_slots_[slot] != kInvalidId)
	{
		const uint32_t candidate = transform_hash_slots_[slot];
		if (EqualTransform(transforms_[candidate], transform))
			return candidate;
		slot = (slot + 1) & (transform_hash_slots_.size() - 1);
	}
	transforms_.push_back(transform);
	const TransformId id = static_cast<TransformId>(transforms_.size() - 1);
	transform_hash_slots_[slot] = id;
	return id;
}

ViewStateId RenderCaptureSegment::InternView(const CapturedWorldView &view)
{
	if (!CanMutate())
		return kInvalidId;
	for (size_t i = 0; i < views_.size(); ++i)
	{
		if (memcmp(&views_[i], &view, sizeof(view)) == 0)
			return static_cast<ViewStateId>(i);
	}
	if (views_.size() >= static_cast<size_t>(UINT32_MAX))
		return kInvalidId;
	views_.push_back(view);
	return static_cast<ViewStateId>(views_.size() - 1);
}

bool RenderCaptureSegment::ReplaceView(ViewStateId id,
	const CapturedWorldView &view)
{
	if (!CanMutate() || id >= views_.size())
		return false;
	views_[id] = view;
	return true;
}

ViewportId RenderCaptureSegment::InternViewport(const CapturedViewport &viewport)
{
	if (!CanMutate())
		return kInvalidId;
	for (size_t i = 0; i < viewports_.size(); ++i)
	{
		if (memcmp(&viewports_[i], &viewport, sizeof(viewport)) == 0)
			return static_cast<ViewportId>(i);
	}
	if (viewports_.size() >= static_cast<size_t>(UINT32_MAX))
		return kInvalidId;
	viewports_.push_back(viewport);
	return static_cast<ViewportId>(viewports_.size() - 1);
}

TargetLayoutId RenderCaptureSegment::InternTargetLayout(const CapturedTargetLayout &layout)
{
	if (!CanMutate())
		return kInvalidId;
	for (size_t i = 0; i < target_layouts_.size(); ++i)
	{
		if (memcmp(&target_layouts_[i], &layout, sizeof(layout)) == 0)
			return static_cast<TargetLayoutId>(i);
	}
	if (target_layouts_.size() >= static_cast<size_t>(UINT32_MAX))
		return kInvalidId;
	target_layouts_.push_back(layout);
	return static_cast<TargetLayoutId>(target_layouts_.size() - 1);
}

RenderTargetSignatureId RenderCaptureSegment::InternTargetSignature(
	const CapturedTargetSignature &signature)
{
	if (!CanMutate())
		return kInvalidId;
	for (size_t i = 0; i < target_signatures_.size(); ++i)
	{
		if (memcmp(&target_signatures_[i], &signature, sizeof(signature)) == 0)
			return static_cast<RenderTargetSignatureId>(i);
	}
	if (target_signatures_.size() >= static_cast<size_t>(UINT32_MAX))
		return kInvalidId;
	target_signatures_.push_back(signature);
	return static_cast<RenderTargetSignatureId>(target_signatures_.size() - 1);
}

TargetVersionId RenderCaptureSegment::InternTargetVersion(const CapturedTargetVersion &version)
{
	if (!CanMutate())
		return kInvalidId;
	for (size_t i = 0; i < target_versions_.size(); ++i)
	{
		if (memcmp(&target_versions_[i], &version, sizeof(version)) == 0)
			return static_cast<TargetVersionId>(i);
	}
	if (target_versions_.size() >= static_cast<size_t>(UINT32_MAX))
		return kInvalidId;
	target_versions_.push_back(version);
	return static_cast<TargetVersionId>(target_versions_.size() - 1);
}

PresentRectId RenderCaptureSegment::InternPresentRect(const CapturedPresentRect &rect)
{
	if (!CanMutate())
		return kInvalidId;
	for (size_t i = 0; i < present_rects_.size(); ++i)
	{
		if (memcmp(&present_rects_[i], &rect, sizeof(rect)) == 0)
			return static_cast<PresentRectId>(i);
	}
	if (present_rects_.size() >= static_cast<size_t>(UINT32_MAX))
		return kInvalidId;
	present_rects_.push_back(rect);
	return static_cast<PresentRectId>(present_rects_.size() - 1);
}

WsiSignatureId RenderCaptureSegment::InternWsiSignature(
	const CapturedWsiSignature &signature)
{
	if (!CanMutate())
		return kInvalidId;
	for (size_t i = 0; i < wsi_signatures_.size(); ++i)
	{
		const CapturedWsiSignature &candidate = wsi_signatures_[i];
		if (candidate.swapchain_generation == signature.swapchain_generation &&
			candidate.format == signature.format &&
			candidate.color_space == signature.color_space &&
			candidate.present_mode == signature.present_mode &&
			candidate.composite_alpha == signature.composite_alpha &&
			candidate.surface_transform == signature.surface_transform &&
			candidate.drawable_width == signature.drawable_width &&
			candidate.drawable_height == signature.drawable_height &&
			candidate.image_count == signature.image_count &&
			candidate.graphics_queue_family == signature.graphics_queue_family &&
			candidate.present_queue_family == signature.present_queue_family &&
			candidate.concurrent_sharing == signature.concurrent_sharing &&
			candidate.safe_authored_unorm == signature.safe_authored_unorm)
			return static_cast<WsiSignatureId>(i);
	}
	if (wsi_signatures_.size() >= static_cast<size_t>(UINT32_MAX))
		return kInvalidId;
	wsi_signatures_.push_back(signature);
	return static_cast<WsiSignatureId>(wsi_signatures_.size() - 1);
}

PayloadDataId RenderCaptureSegment::CopyPayloadData(const void *data, uint32_t byte_size,
	uint32_t alignment, CapturedPayloadSemantic semantic)
{
	if (!CanMutate() || (byte_size != 0 && data == nullptr) ||
		!IsPowerOfTwo(alignment) || !IsPayloadShapeValid(semantic, byte_size, alignment) ||
		SourceAliasesVector(data, byte_size, payload_bytes_))
		return kInvalidId;

	if (payload_records_.size() >= static_cast<size_t>(kInvalidId) ||
		payload_bytes_.size() >= static_cast<size_t>(kInvalidId))
		return kInvalidId;
	const uint32_t old_size = static_cast<uint32_t>(payload_bytes_.size());
	if (old_size > UINT32_MAX - (alignment - 1))
		return kInvalidId;
	const uint32_t offset = AlignUp(old_size, alignment);
	if (offset == kInvalidId || byte_size >= kInvalidId - offset)
		return kInvalidId;

	payload_bytes_.resize(static_cast<size_t>(offset) + byte_size, 0);
	if (byte_size != 0)
		memcpy(payload_bytes_.data() + offset, data, byte_size);

	CapturedPayloadRecord record = { offset, byte_size, alignment, static_cast<uint32_t>(semantic) };
	payload_records_.push_back(record);
	external_payloads_.push_back(nullptr);
	return static_cast<PayloadDataId>(payload_records_.size() - 1);
}

PayloadDataId RenderCaptureSegment::ReferencePayloadData(
	const std::shared_ptr<const std::vector<uint8_t>> &data,
	uint32_t alignment, CapturedPayloadSemantic semantic)
{
	if (!CanMutate() || !data || data->empty() ||
		data->size() >= static_cast<size_t>(kInvalidId) ||
		!IsPowerOfTwo(alignment) ||
		!IsPayloadShapeValid(semantic, static_cast<uint32_t>(data->size()), alignment) ||
		payload_records_.size() >= static_cast<size_t>(kInvalidId))
		return kInvalidId;

	CapturedPayloadRecord record = { kInvalidId,
		static_cast<uint32_t>(data->size()), alignment,
		static_cast<uint32_t>(semantic) };
	payload_records_.push_back(record);
	external_payloads_.push_back(data);
	return static_cast<PayloadDataId>(payload_records_.size() - 1);
}

const uint8_t *RenderCaptureSegment::PayloadData(PayloadDataId id) const
{
	if (id == kInvalidId || id >= payload_records_.size() ||
		id >= external_payloads_.size())
		return nullptr;
	const CapturedPayloadRecord &record = payload_records_[id];
	const std::shared_ptr<const std::vector<uint8_t>> &external =
		external_payloads_[id];
	if (external)
		return external->size() == record.byte_size ? external->data() : nullptr;
	return record.byte_offset <= payload_bytes_.size() &&
		record.byte_size <= payload_bytes_.size() - record.byte_offset ?
		payload_bytes_.data() + record.byte_offset : nullptr;
}

PayloadRef RenderCaptureSegment::InternPayloadBinding(const CapturedPayloadBinding &binding)
{
	if (!CanMutate())
		return kInvalidId;
	if ((binding.validity_flags & ~kPayloadValidityAll) != 0)
		return kInvalidId;
	const PayloadDataId ids[] = { binding.perspective_vertices, binding.motion_vertices,
		binding.specular_vertices, binding.dynamic_lights, binding.specular_block,
		binding.world_aux, binding.cockpit_motion, binding.soft_depth_scalar,
		binding.geometry_aux, binding.terrain_cells, binding.terrain_work_items,
		binding.terrain_batches, binding.terrain_view_input };
	const uint32_t flags[] = { kPayloadHasPerspectiveVertices, kPayloadHasMotionVertices,
		kPayloadHasSpecularVertices, kPayloadHasDynamicLights, kPayloadHasSpecularBlock,
		kPayloadHasWorldAux, kPayloadHasCockpitMotion, kPayloadHasSoftDepthScalar,
		kPayloadHasGeometryAux, kPayloadHasTerrainCells, kPayloadHasTerrainWorkItems,
		kPayloadHasTerrainBatches, kPayloadHasTerrainViewInput };
	for (uint32_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
	{
		const bool present = (binding.validity_flags & flags[i]) != 0;
		if ((present && ids[i] >= payload_records_.size()) || (!present && ids[i] != kInvalidId))
			return kInvalidId;
	}
	for (size_t i = 0; i < payload_bindings_.size(); ++i)
	{
		if (memcmp(&payload_bindings_[i], &binding, sizeof(binding)) == 0)
			return static_cast<PayloadRef>(i);
	}
	if (payload_bindings_.size() >= static_cast<size_t>(UINT32_MAX))
		return kInvalidId;
	payload_bindings_.push_back(binding);
	return static_cast<PayloadRef>(payload_bindings_.size() - 1);
}

StreamGeometryRef RenderCaptureSegment::CopyStreamGeometry(const BaseVertex *vertices,
	uint32_t vertex_count, const uint32_t *indices, uint32_t index_count,
	const uint32_t *optional_payload_words, uint32_t optional_payload_word_count,
	DepthInterpretation depth_interpretation)
{
	StreamGeometryRef result = {};
	result.vertices.offset = kInvalidId;
	result.indices.offset = kInvalidId;
	result.optional_payload_words.offset = kInvalidId;
	result.depth_interpretation = depth_interpretation;

	if (!CanMutate() || (vertex_count != 0 && vertices == nullptr) ||
		(index_count != 0 && indices == nullptr) ||
		(optional_payload_word_count != 0 && optional_payload_words == nullptr) ||
		static_cast<uint32_t>(depth_interpretation) >=
			static_cast<uint32_t>(DepthInterpretation::Count) ||
		SourceAliasesVector(vertices, uint64_t(vertex_count) * sizeof(BaseVertex), stream_vertices_) ||
		SourceAliasesVector(indices, uint64_t(index_count) * sizeof(uint32_t), stream_indices_) ||
		SourceAliasesVector(optional_payload_words,
			uint64_t(optional_payload_word_count) * sizeof(uint32_t), stream_payload_words_))
		return result;

	if (stream_vertices_.size() > UINT32_MAX - vertex_count ||
		stream_indices_.size() > UINT32_MAX - index_count ||
		stream_payload_words_.size() > UINT32_MAX - optional_payload_word_count)
		return result;

	result.vertices = { static_cast<uint32_t>(stream_vertices_.size()), vertex_count };
	result.indices = { static_cast<uint32_t>(stream_indices_.size()), index_count };
	result.optional_payload_words = {
		static_cast<uint32_t>(stream_payload_words_.size()), optional_payload_word_count };

	if (vertex_count != 0)
		stream_vertices_.insert(stream_vertices_.end(), vertices, vertices + vertex_count);
	if (index_count != 0)
		stream_indices_.insert(stream_indices_.end(), indices, indices + index_count);
	if (optional_payload_word_count != 0)
	{
		stream_payload_words_.insert(stream_payload_words_.end(), optional_payload_words,
			optional_payload_words + optional_payload_word_count);
	}
	return result;
}

bool RenderCaptureSegment::AppendCopy(const CaptureCommand &command)
{
	if (!CanMutate() || command.schema_version != kCaptureSchemaVersion ||
		!IsCaptureCommandTypeValid(command.type) || next_command_serial_ == UINT64_MAX)
		return false;
	CaptureCommand copy = {};
	copy.type = command.type;
	copy.schema_version = kCaptureSchemaVersion;
	switch (command.type)
	{
	case CaptureCommandType::BeginFrameTarget: copy.payload.begin_frame_target = command.payload.begin_frame_target; break;
	case CaptureCommandType::ClearColor: copy.payload.clear_color = command.payload.clear_color; break;
	case CaptureCommandType::ClearDepth: copy.payload.clear_depth = command.payload.clear_depth; break;
	case CaptureCommandType::ClearAlphaOnly: copy.payload.clear_alpha_only = command.payload.clear_alpha_only; break;
	case CaptureCommandType::DrawStream: copy.payload.draw_stream = command.payload.draw_stream; break;
	case CaptureCommandType::DrawRetained: copy.payload.draw_retained = command.payload.draw_retained; break;
	case CaptureCommandType::EnqueueFontGlyph: copy.payload.enqueue_font_glyph = command.payload.enqueue_font_glyph; break;
	case CaptureCommandType::FlushFontBatches: copy.payload.flush_font_batches = command.payload.flush_font_batches; break;
	case CaptureCommandType::AcquireSoftDepth: copy.payload.acquire_soft_depth = command.payload.acquire_soft_depth; break;
	case CaptureCommandType::CaptureBloomSource: copy.payload.capture_bloom_source = command.payload.capture_bloom_source; break;
	case CaptureCommandType::BeginPostPresent: copy.payload.begin_post_present = command.payload.begin_post_present; break;
	case CaptureCommandType::BeginCockpitScene: copy.payload.begin_cockpit_scene = command.payload.begin_cockpit_scene; break;
	case CaptureCommandType::EndCockpitScene: copy.payload.end_cockpit_scene = command.payload.end_cockpit_scene; break;
	case CaptureCommandType::PerfMarker: copy.payload.perf_marker = command.payload.perf_marker; break;
	case CaptureCommandType::ReadPixel: copy.payload.read_pixel = command.payload.read_pixel; break;
	case CaptureCommandType::ReadImage: copy.payload.read_image = command.payload.read_image; break;
	case CaptureCommandType::EndFrame: copy.payload.end_frame = command.payload.end_frame; break;
	case CaptureCommandType::Present: copy.payload.present = command.payload.present; break;
	default: return false;
	}
	copy.serial = next_command_serial_++;
	commands_.push_back(copy);
	return true;
}

bool RenderCaptureSegment::Validate(CaptureValidationResult *out_result) const
{
	CaptureValidationResult result = {};
	result.command_index = kInvalidId;
	result.table_index = kInvalidId;
	if (lifecycle_ != Lifecycle::Capturing && lifecycle_ != Lifecycle::Frozen)
		result.errors |= kCaptureInvalidLifecycle;

	auto record_error = [&](uint64_t error, uint32_t command, uint32_t table) {
		result.errors |= error;
		if (result.command_index == kInvalidId && command != kInvalidId)
			result.command_index = command;
		if (result.table_index == kInvalidId && table != kInvalidId)
			result.table_index = table;
	};
	auto valid_payload_data = [&](PayloadDataId id, CapturedPayloadSemantic semantic) {
		return id < payload_records_.size() && payload_records_[id].semantic ==
			static_cast<uint32_t>(semantic);
	};
	auto has_texture = [&](TextureVersionId id) {
		for (size_t i = 0; i < texture_versions_.size(); ++i)
			if (texture_versions_[i].id == id)
				return true;
		return false;
	};
	auto validate_draw_payload = [&](StateId state_id, PayloadRef payload_id,
		uint32_t expected_vertex_count, GeometryMode geometry_mode,
		bool retained_has_specular,
		uint32_t command_index) {
		if (state_id >= states_.size())
			return;
		const bool retained = geometry_mode != GeometryMode::T0Stream;
		const bool terrain = geometry_mode == GeometryMode::T2Terrain;
		const uint32_t terrain_bits = kPayloadHasTerrainCells |
			kPayloadHasTerrainWorkItems | kPayloadHasTerrainBatches |
			kPayloadHasTerrainViewInput;
		const uint32_t light_count = states_[state_id].shader.dynamic_light_count;
		const bool needs_separate_soft_depth =
			(states_[state_id].shader.state_flags2 & kStateSeparateSoftDepthScalar) != 0;
		const uint32_t primary_payload_kind =
			(states_[state_id].shader.state_flags2 & kStatePrimaryPayloadMask) >> 4;
		const bool needs_specular_block =
			(states_[state_id].shader.shader_flags &
			 (kShaderPerPixelSpecular | kShaderFieldSpecular)) != 0;
		if (retained && primary_payload_kind != 0 && !retained_has_specular)
			record_error(kCaptureInvalidPayloadBinding, command_index, kInvalidId);
		if (payload_id == kInvalidId)
		{
			if (terrain || light_count != 0 || needs_separate_soft_depth || needs_specular_block ||
				(!retained && primary_payload_kind != 0))
				record_error(kCaptureInvalidPayloadBinding, command_index, kInvalidId);
			return;
		}
		if (payload_id >= payload_bindings_.size())
			return;
		const CapturedPayloadBinding &binding = payload_bindings_[payload_id];
		if (terrain)
		{
			if ((binding.validity_flags & terrain_bits) != terrain_bits ||
				(binding.validity_flags & kPayloadHasGeometryAux) != 0)
				record_error(kCaptureInvalidPayloadBinding, command_index, payload_id);
		}
		else if ((binding.validity_flags & terrain_bits) != 0)
			record_error(kCaptureInvalidPayloadBinding, command_index, payload_id);
		const bool has_lights = (binding.validity_flags & kPayloadHasDynamicLights) != 0;
		if ((light_count != 0) != has_lights)
			record_error(kCaptureInvalidPayloadBinding, command_index, payload_id);
		else if (has_lights && binding.dynamic_lights < payload_records_.size() &&
			payload_records_[binding.dynamic_lights].byte_size /
				sizeof(GpuDynamicLight) != light_count)
			record_error(kCaptureInvalidPayloadShape, command_index, payload_id);
		const bool has_soft = (binding.validity_flags & kPayloadHasSoftDepthScalar) != 0;
		if (has_soft != needs_separate_soft_depth)
			record_error(kCaptureInvalidPayloadBinding, command_index, payload_id);
		const bool has_specular_block =
			(binding.validity_flags & kPayloadHasSpecularBlock) != 0;
		if (has_specular_block != needs_specular_block)
			record_error(kCaptureInvalidPayloadBinding, command_index, payload_id);
		const bool has_specular_vertices =
			(binding.validity_flags & kPayloadHasSpecularVertices) != 0;
		if (!retained && primary_payload_kind != 0 && !has_specular_vertices)
			record_error(kCaptureInvalidPayloadBinding, command_index, payload_id);
		const PayloadDataId vertex_payloads[] = { binding.perspective_vertices,
			binding.motion_vertices, binding.specular_vertices, binding.cockpit_motion };
		const uint32_t vertex_flags[] = { kPayloadHasPerspectiveVertices,
			kPayloadHasMotionVertices, kPayloadHasSpecularVertices, kPayloadHasCockpitMotion };
		const uint32_t strides[] = { sizeof(PerspectiveVertexPayload),
			sizeof(MotionVertexPayload), sizeof(SpecularVertexPayload),
			sizeof(MotionVertexPayload) };
		for (uint32_t member = 0; member < 4; ++member)
		{
			const bool present = (binding.validity_flags & vertex_flags[member]) != 0;
			if (retained && member < 3 && present)
				record_error(kCaptureInvalidPayloadBinding, command_index, payload_id);
			if (!retained && present && vertex_payloads[member] < payload_records_.size() &&
				payload_records_[vertex_payloads[member]].byte_size / strides[member] !=
					expected_vertex_count)
				record_error(kCaptureInvalidPayloadShape, command_index, payload_id);
		}
	};

	auto validate_t2_payload = [&](PayloadRef payload_id, StateId state_id,
		uint32_t command_index) {
		const uint32_t terrain_bits = kPayloadHasTerrainCells |
			kPayloadHasTerrainWorkItems | kPayloadHasTerrainBatches |
			kPayloadHasTerrainViewInput;
		if (payload_id == kInvalidId || payload_id >= payload_bindings_.size())
			return;
		const CapturedPayloadBinding &binding = payload_bindings_[payload_id];
		if ((binding.validity_flags & terrain_bits) != terrain_bits ||
			(binding.validity_flags & kPayloadHasGeometryAux) != 0)
			return;
		if (!valid_payload_data(binding.terrain_cells, kPayloadTerrainCells) ||
			!valid_payload_data(binding.terrain_work_items, kPayloadTerrainWorkList) ||
			!valid_payload_data(binding.terrain_batches, kPayloadTerrainBatches) ||
			!valid_payload_data(binding.terrain_view_input, kPayloadTerrainViewInput))
			return;

		const CapturedPayloadRecord &cell_record = payload_records_[binding.terrain_cells];
		const CapturedPayloadRecord &work_record = payload_records_[binding.terrain_work_items];
		const CapturedPayloadRecord &batch_record = payload_records_[binding.terrain_batches];
		const CapturedPayloadRecord &view_record = payload_records_[binding.terrain_view_input];
		const uint32_t cell_count = cell_record.byte_size / sizeof(TerrainEmitterCell);
		const uint32_t work_count = work_record.byte_size / sizeof(TerrainWorkItem);
		const uint32_t batch_count = batch_record.byte_size / sizeof(TerrainBatchInput);
		if (cell_count == 0 || work_count == 0 || batch_count == 0 ||
			view_record.byte_size != sizeof(TerrainViewInput))
		{
			record_error(kCaptureInvalidPayloadShape, command_index, payload_id);
			return;
		}

		auto copy_element = [&](const CapturedPayloadRecord &record, uint32_t index,
			uint32_t stride, void *destination) {
			const uint64_t relative = uint64_t(index) * stride;
			if (relative + stride > record.byte_size ||
				uint64_t(record.byte_offset) + relative + stride > payload_bytes_.size())
				return false;
			memcpy(destination, payload_bytes_.data() + record.byte_offset +
				static_cast<size_t>(relative), stride);
			return true;
		};

		std::vector<uint8_t> seen_cells(cell_count, 0);
		std::vector<uint8_t> seen_orders(work_count, 0);
		uint32_t expected_work_first = 0;
		uint32_t expected_output_first = 0;
		int32_t previous_source_texture = -1;
		for (uint32_t batch_index = 0; batch_index < batch_count; ++batch_index)
		{
			TerrainBatchInput batch = {};
			if (!copy_element(batch_record, batch_index, sizeof(batch), &batch))
			{
				record_error(kCaptureInvalidPayloadShape, command_index, payload_id);
				return;
			}
			const uint64_t capacity = uint64_t(batch.work_item_count) *
				kTerrainMaximumOutputVerticesPerCell;
			if (batch.reserved0 != 0)
				record_error(kCaptureInvalidReservedBits, command_index, payload_id);
			if (batch.source_texture < 0 ||
				(batch_index != 0 && batch.source_texture <= previous_source_texture) ||
				batch.texture_layer != batch_index ||
				batch.indirect_command_index != batch_index ||
				batch.first_work_item != expected_work_first ||
				batch.work_item_count == 0 ||
				batch.work_item_count > work_count - expected_work_first ||
				capacity > UINT32_MAX ||
				batch.first_output_vertex != expected_output_first ||
				batch.output_vertex_capacity != static_cast<uint32_t>(capacity) ||
				batch.output_vertex_capacity > UINT32_MAX - expected_output_first)
			{
				record_error(kCaptureInvalidPayloadShape, command_index, payload_id);
				return;
			}
			previous_source_texture = batch.source_texture;

			uint32_t previous_lightmap_page = 0;
			uint32_t previous_segment = 0;
			bool have_previous_cell_key = false;
			for (uint32_t local = 0; local < batch.work_item_count; ++local)
			{
				const uint32_t work_index = batch.first_work_item + local;
				TerrainWorkItem work = {};
				if (!copy_element(work_record, work_index, sizeof(work), &work) ||
					work.cell_index >= cell_count || seen_cells[work.cell_index] ||
					work.source_texture != batch.source_texture ||
					(work.source_flags & ~kTerrainCellAllFlags) != 0 ||
					work.full_draw_order >= work_count || seen_orders[work.full_draw_order])
				{
					record_error(kCaptureInvalidPayloadShape, command_index, payload_id);
					return;
				}
				seen_cells[work.cell_index] = 1;
				seen_orders[work.full_draw_order] = 1;

				TerrainEmitterCell cell = {};
				if (!copy_element(cell_record, work.cell_index, sizeof(cell), &cell))
				{
					record_error(kCaptureInvalidPayloadShape, command_index, payload_id);
					return;
				}
				const uint32_t segment = cell.packed[0];
				const uint32_t lightmap_page =
					(cell.packed[1] & kTerrainLightmapPageMask) >>
					kTerrainLightmapPageShift;
				const uint32_t texture_layer =
					(cell.packed[1] & kTerrainTextureLayerMask) >>
					kTerrainTextureLayerShift;
				if ((segment & ~(kTerrainSegmentXMask | kTerrainSegmentZMask)) != 0 ||
					lightmap_page >= kTerrainLightmapPageCount ||
					texture_layer != batch.texture_layer ||
					cell.packed[2] != batch_index ||
					cell.packed[3] != batch.first_output_vertex ||
					(have_previous_cell_key &&
						(lightmap_page < previous_lightmap_page ||
						 (lightmap_page == previous_lightmap_page &&
						  segment <= previous_segment))))
				{
					record_error(kCaptureInvalidPayloadShape, command_index, payload_id);
					return;
				}
				for (uint32_t height = 0; height < 4; ++height)
					if (!std::isfinite(cell.height[height]))
					{
						record_error(kCaptureInvalidPayloadShape, command_index, payload_id);
						return;
					}
				previous_lightmap_page = lightmap_page;
				previous_segment = segment;
				have_previous_cell_key = true;
			}
			expected_work_first += batch.work_item_count;
			expected_output_first += batch.output_vertex_capacity;
		}
		if (expected_work_first != work_count)
		{
			record_error(kCaptureInvalidPayloadShape, command_index, payload_id);
			return;
		}

		TerrainViewInput view = {};
		if (!copy_element(view_record, 0, sizeof(view), &view))
		{
			record_error(kCaptureInvalidPayloadShape, command_index, payload_id);
			return;
		}
		const float *view_groups[] = { view.terrain_row0, view.terrain_x_step,
			view.terrain_z_step, view.terrain_y_step,
			view.projection_center_half_size, view.viewport_size_inv_size,
			view.clip_scale };
		for (uint32_t group = 0; group < 7; ++group)
			for (uint32_t scalar = 0; scalar < 4; ++scalar)
				if (!std::isfinite(view_groups[group][scalar]))
				{
					record_error(kCaptureInvalidPayloadShape, command_index, payload_id);
					return;
				}
		if (view.terrain_row0[3] != 0.0f || view.terrain_x_step[3] != 0.0f ||
			view.terrain_z_step[3] != 0.0f || view.terrain_y_step[3] != 0.0f)
			record_error(kCaptureInvalidReservedBits, command_index, payload_id);
		if (view.projection_center_half_size[2] <= 0.0f ||
			view.projection_center_half_size[3] <= 0.0f ||
			view.viewport_size_inv_size[0] <= 0.0f ||
			view.viewport_size_inv_size[1] <= 0.0f ||
			view.viewport_size_inv_size[2] <= 0.0f ||
			view.viewport_size_inv_size[3] <= 0.0f)
			record_error(kCaptureInvalidPayloadShape, command_index, payload_id);

		if (state_id >= states_.size())
			return;
		const uint32_t light_count = states_[state_id].shader.dynamic_light_count;
		if ((binding.validity_flags & kPayloadHasWorldAux) == 0 ||
			!valid_payload_data(binding.world_aux, kPayloadWorldAux))
		{
			record_error(kCaptureInvalidPayloadBinding, command_index, payload_id);
			return;
		}
		const CapturedPayloadRecord &aux_record = payload_records_[binding.world_aux];
		GpuWorldAux aux = {};
		if (aux_record.byte_size != sizeof(aux) ||
			!copy_element(aux_record, 0, sizeof(aux), &aux))
		{
			record_error(kCaptureInvalidPayloadShape, command_index, payload_id);
			return;
		}
		uint32_t expected_first = 0;
		for (uint32_t page = 0; page < kTerrainLightmapPageCount; ++page)
		{
			const uint32_t packed = aux.indices[page];
			const uint32_t count = packed & 0xffu;
			const uint32_t first = packed >> 8;
			if (count > kMaxDynamicLights || first != expected_first ||
				first > light_count || count > light_count - first)
			{
				record_error(kCaptureInvalidPayloadShape, command_index, payload_id);
				return;
			}
			expected_first += count;
		}
		if (expected_first != light_count)
			record_error(kCaptureInvalidPayloadShape, command_index, payload_id);
	};

	for (size_t i = 0; i < payload_records_.size(); ++i)
	{
		const CapturedPayloadRecord &record = payload_records_[i];
		const uint8_t *payload = PayloadData(static_cast<PayloadDataId>(i));
		if (!payload ||
			!IsPayloadShapeValid(static_cast<CapturedPayloadSemantic>(record.semantic),
				record.byte_size, record.alignment))
			record_error(kCaptureInvalidPayloadShape, kInvalidId, static_cast<uint32_t>(i));
		else if (record.semantic == kPayloadSpecularBlock)
		{
			GpuSpecularBlock block = {};
			memcpy(&block, payload, sizeof(block));
			if (block.count < 0 || block.count > static_cast<int32_t>(kMaxSpecularSources))
				record_error(kCaptureInvalidPayloadShape, kInvalidId, static_cast<uint32_t>(i));
		}
	}

	for (size_t i = 0; i < payload_bindings_.size(); ++i)
	{
		const CapturedPayloadBinding &binding = payload_bindings_[i];
		if ((binding.validity_flags & ~kPayloadValidityAll) != 0)
			record_error(kCaptureInvalidReservedBits, kInvalidId, static_cast<uint32_t>(i));
		const PayloadDataId ids[] = { binding.perspective_vertices, binding.motion_vertices,
			binding.specular_vertices, binding.dynamic_lights, binding.specular_block,
			binding.world_aux, binding.cockpit_motion, binding.soft_depth_scalar,
			binding.geometry_aux, binding.terrain_cells, binding.terrain_work_items,
			binding.terrain_batches, binding.terrain_view_input };
		const CapturedPayloadSemantic semantics[] = { kPayloadPerspectiveVertices,
			kPayloadMotionVertices, kPayloadSpecularVertices, kPayloadDynamicLights,
			kPayloadSpecularBlock, kPayloadWorldAux, kPayloadCockpitMotion,
			kPayloadSoftDepthScalar, kPayloadGeometryAux, kPayloadTerrainCells,
			kPayloadTerrainWorkList, kPayloadTerrainBatches, kPayloadTerrainViewInput };
		const uint32_t flags[] = { kPayloadHasPerspectiveVertices,
			kPayloadHasMotionVertices, kPayloadHasSpecularVertices,
			kPayloadHasDynamicLights, kPayloadHasSpecularBlock, kPayloadHasWorldAux,
			kPayloadHasCockpitMotion, kPayloadHasSoftDepthScalar, kPayloadHasGeometryAux,
			kPayloadHasTerrainCells, kPayloadHasTerrainWorkItems,
			kPayloadHasTerrainBatches, kPayloadHasTerrainViewInput };
		for (uint32_t member = 0; member < sizeof(ids) / sizeof(ids[0]); ++member)
		{
			const bool present = (binding.validity_flags & flags[member]) != 0;
			if ((present && !valid_payload_data(ids[member], semantics[member])) ||
				(!present && ids[member] != kInvalidId))
				record_error(kCaptureInvalidPayloadBinding, kInvalidId, static_cast<uint32_t>(i));
		}
	}

	for (size_t i = 0; i < target_signatures_.size(); ++i)
	{
		const CapturedTargetSignature &signature = target_signatures_[i];
		if (signature.target_layout >= target_layouts_.size() ||
			signature.post_present_layout >= target_layouts_.size() ||
			signature.cockpit_scene_layout >= target_layouts_.size())
		{
			record_error(kCaptureInvalidTargetRelation, kInvalidId, static_cast<uint32_t>(i));
			continue;
		}
		const CapturedTargetLayout &scene = target_layouts_[signature.target_layout];
		const CapturedTargetLayout &post = target_layouts_[signature.post_present_layout];
		const CapturedTargetLayout &cockpit = target_layouts_[signature.cockpit_scene_layout];
		const CapturedPreferredState &preferred = signature.preferred;
		const uint32_t requested_msaa = NormalizeRequestedMsaa(preferred.msaa_samples,
			preferred.antialised != 0);
		const bool applied_msaa_valid = requested_msaa == 0 ? scene.msaa_samples == 1 :
			scene.msaa_samples <= requested_msaa;
		const uint32_t overscan = NormalizeOverscanPercent(preferred);
		const uint64_t overscanned_width =
			(uint64_t(preferred.width) * overscan + 99u) / 100u;
		const uint64_t overscanned_height =
			(uint64_t(preferred.height) * overscan + 99u) / 100u;
		const uint64_t expected_internal_width =
			overscanned_width * preferred.supersampling_factor;
		const uint64_t expected_internal_height =
			overscanned_height * preferred.supersampling_factor;
		const bool extent_matches = preferred.width == scene.logical_width &&
			preferred.height == scene.logical_height &&
			preferred.supersampling_factor == scene.ssaa_factor &&
			uint64_t(scene.internal_width) == expected_internal_width &&
			uint64_t(scene.internal_height) == expected_internal_height &&
			post.logical_width == preferred.width && post.logical_height == preferred.height &&
			post.internal_width == preferred.width && post.internal_height == preferred.height &&
			cockpit.logical_width == scene.logical_width &&
			cockpit.logical_height == scene.logical_height &&
			cockpit.internal_width == scene.internal_width &&
			cockpit.internal_height == scene.internal_height &&
			scene.drawable_width == post.drawable_width &&
			scene.drawable_height == post.drawable_height &&
			scene.drawable_width == cockpit.drawable_width &&
			scene.drawable_height == cockpit.drawable_height;
		const bool gtao_temporal = preferred.gtao_enabled != 0 &&
			(preferred.gtao_temporal_blend > 0.0f ||
			 preferred.gtao_temporal_debug_preview != 0);
		const bool motion_consumer = WantsMotionResources(preferred);
		const uint32_t derived_feature_bits =
			(preferred.bloom_enabled ? uint32_t(kTargetFeatureBloom) : 0u) |
			(preferred.gtao_enabled ? uint32_t(kTargetFeatureGtao) : 0u) |
			(gtao_temporal ? uint32_t(kTargetFeatureGtaoTemporal) : 0u) |
			(motion_consumer ? uint32_t(kTargetFeatureMotionConsumer) : 0u) |
			((preferred.bloom_enabled || preferred.gtao_enabled || motion_consumer) ?
			 uint32_t(kTargetFeatureLatePost) : 0u);
		const uint32_t comparable_feature_bits = kTargetFeatureBloom |
			kTargetFeatureGtao | kTargetFeatureGtaoTemporal |
			kTargetFeatureMotionConsumer | kTargetFeatureLatePost;
		if (!applied_msaa_valid || !extent_matches || preferred.width == 0 ||
			preferred.height == 0 || preferred.supersampling_factor == 0 ||
			expected_internal_width > UINT32_MAX || expected_internal_height > UINT32_MAX ||
			scene.target != RenderTargetClass::Scene ||
			post.target != RenderTargetClass::PostPresent ||
			cockpit.target != RenderTargetClass::CockpitScene ||
			(scene.feature_flags & comparable_feature_bits) != derived_feature_bits ||
			scene.overscan_percent != overscan ||
			cockpit.overscan_percent != overscan ||
			post.overscan_percent != 100 || post.ssaa_factor != 1 ||
			post.msaa_samples != 1 || cockpit.ssaa_factor != scene.ssaa_factor ||
			cockpit.msaa_samples != scene.msaa_samples)
			record_error(kCaptureInvalidTargetRelation, kInvalidId, static_cast<uint32_t>(i));
	}
	for (size_t i = 0; i < target_layouts_.size(); ++i)
	{
		const CapturedTargetLayout &layout = target_layouts_[i];
		const bool valid_samples = layout.msaa_samples == 1 || layout.msaa_samples == 2 ||
			layout.msaa_samples == 4 || layout.msaa_samples == 8;
		const bool valid_ssaa = layout.ssaa_factor == 1 || layout.ssaa_factor == 2 ||
			layout.ssaa_factor == 4;
		const bool valid_target = static_cast<uint32_t>(layout.target) <
			static_cast<uint32_t>(RenderTargetClass::Count);
		const bool scene_target = layout.target == RenderTargetClass::Scene;
		const bool post_target = layout.target == RenderTargetClass::PostPresent;
		const bool cockpit_target = layout.target == RenderTargetClass::CockpitScene;
		const bool mandatory_attachments = scene_target ?
			(layout.attachment_mask & kTargetAttachmentMandatory) ==
				kTargetAttachmentMandatory :
			(post_target ? layout.attachment_mask == kTargetAttachmentColor :
				layout.attachment_mask == kTargetAttachmentMandatory);
		const bool motion_pair =
			((layout.attachment_mask & kTargetAttachmentVelocity) != 0) ==
			((layout.attachment_mask & kTargetAttachmentObjectId) != 0);
		const bool auxiliary_pair =
			((layout.attachment_mask & kTargetAttachmentProtectionMask) != 0) ==
			((layout.attachment_mask & kTargetAttachmentAoClass) != 0);
		const bool motion_features_satisfied = !scene_target ||
			(layout.feature_flags & (kTargetFeatureMotionConsumer |
			 kTargetFeatureGtaoTemporal)) == 0 ||
			(layout.attachment_mask & (kTargetAttachmentVelocity |
			 kTargetAttachmentObjectId)) == (kTargetAttachmentVelocity |
			 kTargetAttachmentObjectId);
		const bool auxiliary_features_satisfied = !scene_target ||
			(layout.feature_flags & (kTargetFeatureBloom | kTargetFeatureGtao |
			 kTargetFeatureGtaoTemporal | kTargetFeatureCockpitDeferral)) == 0 ||
			(layout.attachment_mask & (kTargetAttachmentProtectionMask |
			 kTargetAttachmentAoClass)) == (kTargetAttachmentProtectionMask |
			 kTargetAttachmentAoClass);
		const uint64_t scaled_width =
			(uint64_t(layout.logical_width) * layout.overscan_percent + 99u) / 100u *
			layout.ssaa_factor;
		const uint64_t scaled_height =
			(uint64_t(layout.logical_height) * layout.overscan_percent + 99u) / 100u *
			layout.ssaa_factor;
		const bool extent_relation = post_target ?
			(layout.internal_width == layout.logical_width &&
			 layout.internal_height == layout.logical_height) :
			(uint64_t(layout.internal_width) == scaled_width &&
			 uint64_t(layout.internal_height) == scaled_height);
		if (!valid_target || layout.logical_width == 0 || layout.logical_height == 0 ||
			layout.internal_width == 0 || layout.internal_height == 0 ||
			layout.drawable_width == 0 || layout.drawable_height == 0 ||
			!valid_samples || !valid_ssaa || !mandatory_attachments ||
			(scene_target && (!motion_pair || !auxiliary_pair)) ||
			!auxiliary_pair || !motion_features_satisfied ||
			!auxiliary_features_satisfied ||
			!extent_relation || layout.overscan_percent < 100 ||
			layout.overscan_percent > 150 ||
			(post_target && (layout.msaa_samples != 1 || layout.ssaa_factor != 1 ||
			 layout.overscan_percent != 100 || layout.feature_flags != 0)) ||
			(cockpit_target && layout.feature_flags != 0) ||
			(layout.attachment_mask & ~kTargetAttachmentAll) != 0 ||
			(layout.feature_flags & ~kTargetFeatureAll) != 0 ||
			layout.present_format != RenderFormat::R8G8B8A8Unorm)
			record_error(kCaptureInvalidTargetRelation, kInvalidId, static_cast<uint32_t>(i));
		for (uint32_t attachment = 0; attachment < 6; ++attachment)
			if (layout.attachment_formats[attachment] !=
				kSceneAttachmentContract[attachment].format)
				record_error(kCaptureInvalidEnum, kInvalidId, static_cast<uint32_t>(i));
	}
	for (size_t i = 0; i < target_versions_.size(); ++i)
	{
		if (target_versions_[i].target_layout >= target_layouts_.size() ||
			static_cast<uint32_t>(target_versions_[i].target) >=
				static_cast<uint32_t>(RenderTargetClass::Count))
			record_error(kCaptureInvalidTargetRelation, kInvalidId, static_cast<uint32_t>(i));
		else
		{
			const CapturedTargetLayout &layout = target_layouts_[target_versions_[i].target_layout];
			const bool logical_target = layout.target == RenderTargetClass::PostPresent;
			const uint32_t expected_width = logical_target ?
				layout.logical_width : layout.internal_width;
			const uint32_t expected_height = logical_target ?
				layout.logical_height : layout.internal_height;
			if (target_versions_[i].target != layout.target ||
				target_versions_[i].width == 0 || target_versions_[i].height == 0 ||
				target_versions_[i].width != expected_width ||
				target_versions_[i].height != expected_height ||
				target_versions_[i].samples != layout.msaa_samples ||
				((layout.attachment_mask & kTargetAttachmentDepth) == 0 &&
				 target_versions_[i].depth_epoch != 0))
				record_error(kCaptureInvalidTargetRelation, kInvalidId, static_cast<uint32_t>(i));
		}
	}

	for (size_t i = 0; i < texture_versions_.size(); ++i)
	{
		const CapturedTextureVersion &version = texture_versions_[i];
		if (version.id == kInvalidId || version.width == 0 || version.height == 0 ||
			version.depth_or_layers == 0 || version.mip_count == 0 ||
			static_cast<uint32_t>(version.format) >= static_cast<uint32_t>(RenderFormat::Count) ||
			(version.immutable_upload_payload != kInvalidId &&
			 !valid_payload_data(version.immutable_upload_payload, kPayloadTextureUpload)))
			record_error(kCaptureInvalidTextureVersion, kInvalidId, static_cast<uint32_t>(i));
		else if (version.immutable_upload_payload != kInvalidId)
		{
			const uint64_t expected_upload_size = TextureUploadByteSize(version);
			const CapturedPayloadRecord &upload =
				payload_records_[version.immutable_upload_payload];
			if (expected_upload_size == UINT64_MAX ||
				expected_upload_size != upload.byte_size)
				record_error(kCaptureInvalidPayloadShape, kInvalidId, static_cast<uint32_t>(i));
		}
	}

	for (size_t i = 0; i < states_.size(); ++i)
	{
		const CapturedShaderRasterState &state = states_[i];
		if (state.target_layout >= target_layouts_.size() || state.viewport >= viewports_.size() ||
			state.scissor >= viewports_.size())
			record_error(kCaptureInvalidTableReference, kInvalidId, static_cast<uint32_t>(i));
		else if (state.sample_count != target_layouts_[state.target_layout].msaa_samples)
			record_error(kCaptureInvalidTargetRelation, kInvalidId, static_cast<uint32_t>(i));
		if (!IsLegalMrtWriteMask(state.mrt_write_mask))
			record_error(kCaptureIllegalMrtMask, kInvalidId, static_cast<uint32_t>(i));
		else if (state.target_layout < target_layouts_.size() &&
			(state.mrt_write_mask & ~target_layouts_[state.target_layout].attachment_mask) != 0)
			record_error(kCaptureInvalidTargetRelation, kInvalidId, static_cast<uint32_t>(i));
		if (static_cast<uint32_t>(state.raster_family) >
			static_cast<uint32_t>(RasterFamily::ExpandedPoint))
			record_error(kCaptureInvalidEnum, kInvalidId, static_cast<uint32_t>(i));
		if ((state.shader.shader_flags & ~((1u << 18) - 1)) != 0 ||
			(state.shader.state_flags2 & ~((1u << 8) - 1)) != 0 || state.shader.reserved0 != 0)
			record_error(kCaptureInvalidReservedBits, kInvalidId, static_cast<uint32_t>(i));
		const uint32_t primary_payload_kind =
			(state.shader.state_flags2 & kStatePrimaryPayloadMask) >> 4;
		if (primary_payload_kind > 2 ||
			((state.shader.state_flags2 & kStateSeparateSoftDepthScalar) != 0 &&
			 (state.shader.shader_flags & kShaderSoftParticle) == 0))
			record_error(kCaptureInvalidEnum, kInvalidId, static_cast<uint32_t>(i));
		const uint32_t maximum_lights =
			(state.shader.shader_flags & kShaderTerrain) != 0 ?
			kMaxTerrainDynamicLights : kMaxDynamicLights;
		if (state.shader.dynamic_light_count > maximum_lights)
			record_error(kCaptureInvalidPayloadShape, kInvalidId, static_cast<uint32_t>(i));
	}

	for (size_t i = 0; i < materials_.size(); ++i)
	{
		for (uint32_t image = 0; image < 4; ++image)
		{
			if (!has_texture(materials_[i].image2d[image]) ||
				!has_texture(materials_[i].image2d_array[image]))
				record_error(kCaptureInvalidTextureVersion, kInvalidId, static_cast<uint32_t>(i));
			if (materials_[i].sampler[image] >= kWorldSamplerCount)
				record_error(kCaptureInvalidTableReference, kInvalidId, static_cast<uint32_t>(i));
		}
	}
	for (size_t i = 0; i < wsi_signatures_.size(); ++i)
	{
		const CapturedWsiSignature &signature = wsi_signatures_[i];
		if ((signature.format != SurfacePixelFormat::B8G8R8A8Unorm &&
			 signature.format != SurfacePixelFormat::R8G8B8A8Unorm) ||
			signature.color_space != SurfaceColorSpace::SrgbNonlinear ||
			static_cast<uint32_t>(signature.present_mode) >=
				static_cast<uint32_t>(PresentModeContract::Count) ||
			static_cast<uint32_t>(signature.composite_alpha) >=
				static_cast<uint32_t>(CompositeAlphaContract::Count) ||
			signature.surface_transform == 0 || signature.drawable_width == 0 ||
			signature.drawable_height == 0 || signature.image_count < 2 ||
			signature.graphics_queue_family == kInvalidId ||
			signature.present_queue_family == kInvalidId ||
			signature.concurrent_sharing > 1 || signature.safe_authored_unorm != 1)
			record_error(kCaptureInvalidTargetRelation, kInvalidId,
				static_cast<uint32_t>(i));
	}

	const bool continuation =
		start_kind_ == CaptureSegmentStartKind::ContinuationAfterReadback;
	if (static_cast<uint32_t>(start_kind_) >
		static_cast<uint32_t>(CaptureSegmentStartKind::ContinuationAfterReadback))
		record_error(kCaptureInvalidEnum, kInvalidId, kInvalidId);
	if (continuation)
	{
		const CaptureContinuationState &state = continuation_state_;
		const bool valid_target = static_cast<uint32_t>(state.active_target) <
			static_cast<uint32_t>(RenderTargetClass::Count);
		const bool valid_attachment_set = state.active_target == RenderTargetClass::Scene ?
			((state.active_attachment_mask & kTargetAttachmentMandatory) ==
				kTargetAttachmentMandatory &&
			 (state.active_attachment_mask & ~kTargetAttachmentAll) == 0) :
			(state.active_target == RenderTargetClass::PostPresent ?
				state.active_attachment_mask == kTargetAttachmentColor :
				state.active_attachment_mask == kTargetAttachmentMandatory);
		if (state.schema_version != kCaptureContinuationSchemaVersion || !valid_target ||
			!RectHasPositiveArea(state.logical_clip) || !valid_attachment_set ||
			state.load_attachment_mask != state.active_attachment_mask ||
			state.post_present_begun > 1 || state.cockpit_open > 1 ||
			state.have_last_view_interval_serial > 1 ||
			state.have_last_font_enqueue_serial > 1 ||
			state.prior_submitted_timeline == 0 ||
			state.resource_state_snapshot_serial == 0 ||
			(state.cockpit_open && (!state.post_present_begun ||
			 state.active_target != RenderTargetClass::CockpitScene)) ||
			(state.active_target == RenderTargetClass::PostPresent &&
			 !state.post_present_begun))
			record_error(kCaptureInvalidTargetRelation, kInvalidId, kInvalidId);
		if ((!state.have_last_view_interval_serial && state.last_view_interval_serial != 0) ||
			(!state.have_last_font_enqueue_serial && state.last_font_enqueue_serial != 0) ||
			(!state.cockpit_open && state.cockpit_capture_serial != 0) ||
			(state.cockpit_open && state.cockpit_capture_serial == 0) ||
			(state.active_target == RenderTargetClass::Scene && state.post_present_begun) ||
			(state.active_target == RenderTargetClass::CockpitScene &&
			 (!state.post_present_begun || !state.cockpit_open)))
			record_error(kCaptureInvalidCommandGrammar, kInvalidId, kInvalidId);
		if (state.active_target_version >= target_versions_.size())
			record_error(kCaptureInvalidTableReference, kInvalidId,
				state.active_target_version);
		else
		{
			const CapturedTargetVersion &version =
				target_versions_[state.active_target_version];
			if (version.target_layout >= target_layouts_.size())
				record_error(kCaptureInvalidTableReference, kInvalidId,
					state.active_target_version);
			else
			{
				const CapturedTargetLayout &layout = target_layouts_[version.target_layout];
				bool clip_fits = false;
				if (layout.logical_width <= INT32_MAX && layout.logical_height <= INT32_MAX)
				{
					const LogicalRect target_rect = { 0, 0,
						static_cast<int32_t>(layout.logical_width),
						static_cast<int32_t>(layout.logical_height) };
					clip_fits = RectFitsInside(state.logical_clip, target_rect);
				}
				if (version.target != state.active_target || layout.target != state.active_target ||
					version.color_epoch != state.color_epoch ||
					version.depth_epoch != state.depth_epoch ||
					layout.attachment_mask != state.active_attachment_mask ||
					!clip_fits ||
					((layout.attachment_mask & kTargetAttachmentDepth) == 0 &&
					 state.depth_epoch != 0))
					record_error(kCaptureInvalidTargetRelation, kInvalidId,
						state.active_target_version);
			}
		}
	}
	bool frame_interval_open = continuation;
	bool began_any_frame_interval = continuation;
	bool post_present_begun = continuation && continuation_state_.post_present_begun != 0;
	bool cockpit_open = continuation && continuation_state_.cockpit_open != 0;
	bool present_seen = false;
	RenderTargetSignatureId post_present_signature = kInvalidId;
	RenderTargetClass active_target = continuation ? continuation_state_.active_target :
		RenderTargetClass::Scene;
	TargetVersionId active_target_version = continuation ?
		continuation_state_.active_target_version : kInvalidId;
	LogicalRect active_logical_clip = continuation ? continuation_state_.logical_clip :
		LogicalRect{};
	uint32_t cockpit_capture_serial = continuation ?
		continuation_state_.cockpit_capture_serial : 0;
	uint32_t end_frame_count = 0;
	uint32_t last_view_interval_serial = continuation ?
		continuation_state_.last_view_interval_serial : 0;
	bool have_view_interval_serial = continuation &&
		continuation_state_.have_last_view_interval_serial != 0;
	uint32_t last_font_enqueue_serial = continuation ?
		continuation_state_.last_font_enqueue_serial : 0;
	bool have_font_enqueue_serial = continuation &&
		continuation_state_.have_last_font_enqueue_serial != 0;
	if (commands_.empty())
		record_error(kCaptureInvalidCommandGrammar, kInvalidId, kInvalidId);

	for (size_t i = 0; i < commands_.size(); ++i)
	{
		const CaptureCommand &command = commands_[i];
		const uint32_t ci = static_cast<uint32_t>(i);
		if (command.schema_version != kCaptureSchemaVersion || !IsCaptureCommandTypeValid(command.type))
		{
			record_error(kCaptureInvalidCommand, ci, kInvalidId);
			continue;
		}
		if (i > 0 && command.serial != commands_[i - 1].serial + 1)
			record_error(kCaptureInvalidCommand, ci, kInvalidId);
		if (present_seen)
			record_error(kCaptureInvalidCommandGrammar, ci, kInvalidId);
		switch (command.type)
		{
		case CaptureCommandType::BeginFrameTarget:
		{
			const BeginFrameTargetCommand &begin = command.payload.begin_frame_target;
			if (frame_interval_open || present_seen ||
				(begin.target == RenderTargetClass::PostPresent &&
				 !post_present_begun) ||
				(begin.target == RenderTargetClass::CockpitScene &&
				 !cockpit_open))
				record_error(kCaptureInvalidCommandGrammar, ci, kInvalidId);
			if (static_cast<uint32_t>(begin.target) >=
					static_cast<uint32_t>(RenderTargetClass::Count) ||
				(begin.clear_flags &
					~(kClearDepthFlag | kClearColorFlag)) != 0)
				record_error(kCaptureInvalidEnum, ci, kInvalidId);
			if (begin.physical_viewport >= viewports_.size() ||
				begin.view_state >= views_.size() ||
				begin.active_target_version >= target_versions_.size())
				record_error(kCaptureInvalidTableReference, ci, kInvalidId);
			else
			{
				const CapturedViewport &viewport = viewports_[begin.physical_viewport];
				const CapturedTargetVersion &version =
					target_versions_[begin.active_target_version];
				if (!RectFitsInside(begin.logical_clip, viewport.logical_rect))
					record_error(kCaptureInvalidRect, ci, kInvalidId);
				if (version.target_layout >= target_layouts_.size())
					record_error(kCaptureInvalidTableReference, ci,
						begin.active_target_version);
				else
				{
					const CapturedTargetLayout &layout = target_layouts_[version.target_layout];
					if (version.target != begin.target || layout.target != begin.target ||
						viewport.target_width != version.width ||
						viewport.target_height != version.height ||
						viewport.ssaa_factor != layout.ssaa_factor ||
						((begin.clear_flags & kClearDepthFlag) != 0 &&
						 (layout.attachment_mask & kTargetAttachmentDepth) == 0))
						record_error(kCaptureInvalidTargetRelation, ci,
							begin.active_target_version);
				}
			}
			frame_interval_open = true;
			began_any_frame_interval = true;
			active_target = begin.target;
			active_target_version = begin.active_target_version;
			active_logical_clip = begin.logical_clip;
			break;
		}
		case CaptureCommandType::DrawStream:
		{
			const DrawStreamCommand &draw = command.payload.draw_stream;
			if (!frame_interval_open)
				record_error(kCaptureInvalidCommandGrammar, ci, kInvalidId);
			if (!SpanFits(draw.geometry.vertices, stream_vertices_.size()) ||
				!SpanFits(draw.geometry.indices, stream_indices_.size()) ||
				!SpanFits(draw.geometry.optional_payload_words, stream_payload_words_.size()))
				record_error(kCaptureInvalidSpan, ci, kInvalidId);
			else
			{
				if (draw.geometry.vertices.count == 0 ||
					(draw.geometry.indices.count != 0 &&
					 draw.geometry.indices.count % 3 != 0))
					record_error(kCaptureInvalidGeometry, ci, kInvalidId);
				for (uint32_t index = 0; index < draw.geometry.indices.count; ++index)
					if (stream_indices_[draw.geometry.indices.offset + index] >=
						draw.geometry.vertices.count)
					{
						record_error(kCaptureInvalidGeometry, ci, index);
						break;
					}
			}
			if (static_cast<uint32_t>(draw.geometry.depth_interpretation) >=
					static_cast<uint32_t>(DepthInterpretation::Count) ||
				static_cast<uint32_t>(draw.classification.source_kind) >=
					static_cast<uint32_t>(PrimitiveSourceKind::Count))
				record_error(kCaptureInvalidEnum, ci, kInvalidId);
			if (draw.state >= states_.size() || draw.transform >= transforms_.size() ||
				draw.material >= materials_.size() ||
				draw.view >= views_.size() ||
				(draw.optional_payload != kInvalidId &&
				 draw.optional_payload >= payload_bindings_.size()))
				record_error(kCaptureInvalidTableReference, ci, kInvalidId);
			else if (active_target_version >= target_versions_.size() ||
				states_[draw.state].target_layout !=
					target_versions_[active_target_version].target_layout)
				record_error(kCaptureInvalidTargetRelation, ci, draw.state);
			validate_draw_payload(draw.state, draw.optional_payload,
				draw.geometry.vertices.count, GeometryMode::T0Stream, false, ci);
			break;
		}
		case CaptureCommandType::DrawRetained:
		{
			const DrawRetainedCommand &draw = command.payload.draw_retained;
			if (!frame_interval_open)
				record_error(kCaptureInvalidCommandGrammar, ci, kInvalidId);
			const bool t1 = draw.geometry_mode == GeometryMode::T1Retained;
			const bool t2 = draw.geometry_mode == GeometryMode::T2Terrain;
			if (!t1 && !t2)
				record_error(kCaptureInvalidEnum, ci, kInvalidId);
			if (draw.mesh.id == kInvalidId)
				record_error(kCaptureInvalidSpan, ci, kInvalidId);
			if (t1)
			{
				if (draw.index_count == 0 ||
					draw.first_index > UINT32_MAX - draw.index_count ||
					draw.index_count % 3 != 0 ||
					!IsCanonicalRetainedSpan(draw.perspective_payload) ||
					!IsCanonicalRetainedSpan(draw.motion_payload) ||
					!IsCanonicalRetainedSpan(draw.specular_payload))
					record_error(kCaptureInvalidSpan, ci, kInvalidId);
			}
			else if (t2)
			{
				if (draw.first_index != 0 || draw.index_count != 0 ||
					draw.base_vertex != 0 ||
					draw.perspective_payload.offset != kInvalidId ||
					draw.perspective_payload.count != 0 ||
					draw.motion_payload.offset != kInvalidId ||
					draw.motion_payload.count != 0 ||
					draw.specular_payload.offset != kInvalidId ||
					draw.specular_payload.count != 0)
					record_error(kCaptureInvalidGeometry, ci, kInvalidId);
			}
			if (static_cast<uint32_t>(draw.classification.source_kind) >=
				static_cast<uint32_t>(PrimitiveSourceKind::Count))
				record_error(kCaptureInvalidEnum, ci, kInvalidId);
			else if ((t2 && draw.classification.source_kind !=
					PrimitiveSourceKind::TerrainEmitter) ||
				(t1 && draw.classification.source_kind == PrimitiveSourceKind::TerrainEmitter))
				record_error(kCaptureInvalidGeometry, ci, kInvalidId);
			if (draw.state >= states_.size() || draw.transform >= transforms_.size() ||
				draw.material >= materials_.size() ||
				draw.view >= views_.size() ||
				(draw.optional_payload != kInvalidId &&
				 draw.optional_payload >= payload_bindings_.size()))
				record_error(kCaptureInvalidTableReference, ci, kInvalidId);
			else if (active_target_version >= target_versions_.size() ||
				states_[draw.state].target_layout !=
					target_versions_[active_target_version].target_layout)
				record_error(kCaptureInvalidTargetRelation, ci, draw.state);
			if (t2 && draw.state < states_.size() &&
				(states_[draw.state].shader.shader_flags & kShaderTerrain) == 0)
				record_error(kCaptureInvalidPayloadBinding, ci, draw.state);
			validate_draw_payload(draw.state, draw.optional_payload, 0,
				draw.geometry_mode, t1 && draw.specular_payload.count != 0, ci);
			if (t2)
				validate_t2_payload(draw.optional_payload, draw.state, ci);
			break;
		}
		case CaptureCommandType::EnqueueFontGlyph:
			if (!frame_interval_open)
				record_error(kCaptureInvalidCommandGrammar, ci, kInvalidId);
			if (!has_texture(command.payload.enqueue_font_glyph.texture_version) ||
				command.payload.enqueue_font_glyph.bucket > 1 ||
				!IsFiniteUnitFloat(command.payload.enqueue_font_glyph.alpha) ||
				(have_font_enqueue_serial &&
				 command.payload.enqueue_font_glyph.enqueue_serial <= last_font_enqueue_serial))
				record_error(kCaptureInvalidTextureVersion, ci, kInvalidId);
			for (size_t texture = 0; texture < texture_versions_.size(); ++texture)
				if (texture_versions_[texture].id ==
					command.payload.enqueue_font_glyph.texture_version &&
					command.payload.enqueue_font_glyph.texture_layer >=
					texture_versions_[texture].depth_or_layers)
					record_error(kCaptureInvalidTextureVersion, ci,
						static_cast<uint32_t>(texture));
			last_font_enqueue_serial = command.payload.enqueue_font_glyph.enqueue_serial;
			have_font_enqueue_serial = true;
			break;
		case CaptureCommandType::ClearColor:
			if (!frame_interval_open ||
				command.payload.clear_color.target != active_target)
				record_error(kCaptureInvalidCommandGrammar, ci, kInvalidId);
			if (static_cast<uint32_t>(command.payload.clear_color.target) >=
				static_cast<uint32_t>(RenderTargetClass::Count) ||
				command.payload.clear_color.selected_attachments == 0 ||
				(command.payload.clear_color.selected_attachments &
				 ~(kWriteColor | kWriteVelocity | kWriteProtectionMask |
				   kWriteAoClass | kWriteObjectId)) != 0 ||
				command.payload.clear_color.whole_target > 1)
				record_error(kCaptureInvalidEnum, ci, kInvalidId);
			if (command.payload.clear_color.target != RenderTargetClass::Scene &&
				command.payload.clear_color.selected_attachments != kWriteColor)
				record_error(kCaptureInvalidTargetRelation, ci, kInvalidId);
			if (active_target_version >= target_versions_.size() ||
				target_versions_[active_target_version].target_layout >= target_layouts_.size() ||
				(command.payload.clear_color.selected_attachments &
				 ~target_layouts_[target_versions_[active_target_version].target_layout].attachment_mask) != 0)
				record_error(kCaptureInvalidTargetRelation, ci, active_target_version);
			{
				const uint32_t attachment_bits[5] = { kWriteColor, kWriteVelocity,
					kWriteProtectionMask, kWriteAoClass, kWriteObjectId };
				const uint32_t legal_channels[5] = { kChannelRgba,
					kChannelRed | kChannelGreen, kChannelRed | kChannelGreen,
					kChannelRed, kChannelRed };
				for (uint32_t attachment = 0; attachment < 5; ++attachment)
				{
					const bool selected = (command.payload.clear_color.selected_attachments &
						attachment_bits[attachment]) != 0;
					const uint32_t channels =
						command.payload.clear_color.attachment_channel_masks[attachment];
					if ((selected && (channels == 0 || (channels & ~legal_channels[attachment]) != 0)) ||
						(!selected && channels != 0))
						record_error(kCaptureInvalidTargetRelation, ci, attachment);
				}
			}
			if (!command.payload.clear_color.whole_target &&
				!RectFitsInside(command.payload.clear_color.rect, active_logical_clip))
				record_error(kCaptureInvalidRect, ci, kInvalidId);
			for (uint32_t channel = 0; channel < 4; ++channel)
				if (!IsFiniteUnitFloat(command.payload.clear_color.rgba[channel]))
					record_error(kCaptureInvalidEnum, ci, channel);
			break;
		case CaptureCommandType::ClearDepth:
			if (!frame_interval_open ||
				command.payload.clear_depth.target != active_target)
				record_error(kCaptureInvalidCommandGrammar, ci, kInvalidId);
			if (static_cast<uint32_t>(command.payload.clear_depth.target) >=
				static_cast<uint32_t>(RenderTargetClass::Count) ||
				command.payload.clear_depth.whole_target > 1 ||
				!IsFiniteUnitFloat(command.payload.clear_depth.depth))
				record_error(kCaptureInvalidEnum, ci, kInvalidId);
			if (!command.payload.clear_depth.whole_target &&
				!RectFitsInside(command.payload.clear_depth.rect, active_logical_clip))
				record_error(kCaptureInvalidRect, ci, kInvalidId);
			if (active_target_version >= target_versions_.size() ||
				target_versions_[active_target_version].target_layout >= target_layouts_.size() ||
				(target_layouts_[target_versions_[active_target_version].target_layout].attachment_mask &
				 kTargetAttachmentDepth) == 0)
				record_error(kCaptureInvalidTargetRelation, ci, active_target_version);
			break;
		case CaptureCommandType::ClearAlphaOnly:
			if (!frame_interval_open)
				record_error(kCaptureInvalidCommandGrammar, ci, kInvalidId);
			if (static_cast<uint32_t>(command.payload.clear_alpha_only.image) >=
				static_cast<uint32_t>(ImageSemantic::Count) ||
				command.payload.clear_alpha_only.whole_target > 1 ||
				!IsFiniteUnitFloat(command.payload.clear_alpha_only.alpha))
				record_error(kCaptureInvalidEnum, ci, kInvalidId);
			if (!command.payload.clear_alpha_only.whole_target &&
				!RectFitsInside(command.payload.clear_alpha_only.rect, active_logical_clip))
				record_error(kCaptureInvalidRect, ci, kInvalidId);
			break;
		case CaptureCommandType::FlushFontBatches:
			if (!began_any_frame_interval || present_seen ||
				static_cast<uint32_t>(command.payload.flush_font_batches.target) >=
				static_cast<uint32_t>(RenderTargetClass::Count))
				record_error(kCaptureInvalidCommandGrammar, ci, kInvalidId);
			if (command.payload.flush_font_batches.view_state >= views_.size())
				record_error(kCaptureInvalidTableReference, ci, kInvalidId);
			break;
		case CaptureCommandType::AcquireSoftDepth:
			if (!frame_interval_open || active_target != RenderTargetClass::Scene)
				record_error(kCaptureInvalidCommandGrammar, ci, kInvalidId);
			if (command.payload.acquire_soft_depth.scene_target_version >= target_versions_.size())
				record_error(kCaptureInvalidTableReference, ci, kInvalidId);
			else
			{
				const TargetVersionId checkpoint_id =
					command.payload.acquire_soft_depth.scene_target_version;
				const CapturedTargetVersion &checkpoint = target_versions_[checkpoint_id];
				if (active_target_version >= target_versions_.size() ||
					checkpoint.target != RenderTargetClass::Scene ||
					checkpoint.target_layout != target_versions_[active_target_version].target_layout ||
					checkpoint.color_epoch < target_versions_[active_target_version].color_epoch ||
					checkpoint.depth_epoch < target_versions_[active_target_version].depth_epoch ||
					command.payload.acquire_soft_depth.depth_epoch != checkpoint.depth_epoch)
					record_error(kCaptureInvalidTargetRelation, ci, kInvalidId);
				else
					active_target_version = checkpoint_id;
			}
			break;
		case CaptureCommandType::CaptureBloomSource:
			if (!frame_interval_open || active_target != RenderTargetClass::Scene)
				record_error(kCaptureInvalidCommandGrammar, ci, kInvalidId);
			if (command.payload.capture_bloom_source.scene_target_version >= target_versions_.size() ||
				command.payload.capture_bloom_source.projection >= views_.size() ||
				command.payload.capture_bloom_source.view_projection >= views_.size() ||
				command.payload.capture_bloom_source.inverse_modelview >= views_.size())
				record_error(kCaptureInvalidTableReference, ci, kInvalidId);
			else
			{
				const TargetVersionId checkpoint_id =
					command.payload.capture_bloom_source.scene_target_version;
				const CapturedTargetVersion &checkpoint = target_versions_[checkpoint_id];
				if (active_target_version >= target_versions_.size() ||
					checkpoint.target != RenderTargetClass::Scene ||
					checkpoint.target_layout != target_versions_[active_target_version].target_layout ||
					checkpoint.color_epoch < target_versions_[active_target_version].color_epoch ||
					checkpoint.depth_epoch < target_versions_[active_target_version].depth_epoch ||
					!RectFitsInside(command.payload.capture_bloom_source.visible_rect,
						active_logical_clip))
					record_error(kCaptureInvalidTargetRelation, ci, kInvalidId);
				else
					active_target_version = checkpoint_id;
			}
			break;
		case CaptureCommandType::BeginPostPresent:
			if (frame_interval_open || post_present_begun || cockpit_open || present_seen ||
				command.payload.begin_post_present.defer_bloom > 1)
				record_error(kCaptureInvalidCommandGrammar, ci, kInvalidId);
			if (command.payload.begin_post_present.signature >= target_signatures_.size())
				record_error(kCaptureInvalidTableReference, ci, kInvalidId);
			else
				post_present_signature = command.payload.begin_post_present.signature;
			post_present_begun = true;
			break;
		case CaptureCommandType::BeginCockpitScene:
			if (frame_interval_open || cockpit_open || !post_present_begun || present_seen ||
				!RectHasPositiveArea(command.payload.begin_cockpit_scene.logical_rect))
				record_error(kCaptureInvalidCommandGrammar, ci, kInvalidId);
			if (!valid_payload_data(command.payload.begin_cockpit_scene.backing_effect_state,
				kPayloadCockpitBacking))
				record_error(kCaptureInvalidPayloadShape, ci, kInvalidId);
			cockpit_open = true;
			cockpit_capture_serial = command.payload.begin_cockpit_scene.capture_serial;
			break;
		case CaptureCommandType::EndCockpitScene:
			if (!cockpit_open || frame_interval_open ||
				command.payload.end_cockpit_scene.capture_serial != cockpit_capture_serial)
				record_error(kCaptureInvalidCommandGrammar, ci, kInvalidId);
			cockpit_open = false;
			break;
		case CaptureCommandType::Present:
			if (command.payload.present.present_rect >= present_rects_.size() ||
				command.payload.present.window_swapchain_signature >= wsi_signatures_.size() ||
				command.payload.present.presented_frame_serial != presented_frame_serial_)
				record_error(kCaptureInvalidTableReference, ci, kInvalidId);
			else
			{
				const CapturedPresentRect &rect =
					present_rects_[command.payload.present.present_rect];
				const CapturedWsiSignature &signature =
					wsi_signatures_[command.payload.present.window_swapchain_signature];
				const int64_t right = int64_t(rect.rect.x) + rect.rect.width;
				const int64_t bottom = int64_t(rect.rect.y) + rect.rect.height;
				bool target_drawable_matches = false;
				if (post_present_signature < target_signatures_.size())
				{
					const CapturedTargetSignature &target_signature =
						target_signatures_[post_present_signature];
					if (target_signature.target_layout < target_layouts_.size() &&
						target_signature.post_present_layout < target_layouts_.size() &&
						target_signature.cockpit_scene_layout < target_layouts_.size())
					{
						const CapturedTargetLayout *layouts[3] = {
							&target_layouts_[target_signature.target_layout],
							&target_layouts_[target_signature.post_present_layout],
							&target_layouts_[target_signature.cockpit_scene_layout],
						};
						target_drawable_matches = true;
						for (uint32_t layout_index = 0; layout_index < 3; ++layout_index)
							target_drawable_matches = target_drawable_matches &&
								layouts[layout_index]->drawable_width == signature.drawable_width &&
								layouts[layout_index]->drawable_height == signature.drawable_height;
					}
				}
				if (rect.drawable_width != signature.drawable_width ||
					rect.drawable_height != signature.drawable_height ||
					rect.surface_transform != signature.surface_transform ||
					rect.swapchain_generation != signature.swapchain_generation ||
					rect.rect.x < 0 || rect.rect.y < 0 || rect.rect.width <= 0 ||
					rect.rect.height <= 0 ||
					right > signature.drawable_width || bottom > signature.drawable_height ||
					!target_drawable_matches)
						record_error(kCaptureInvalidTargetRelation, ci, kInvalidId);
			}
			if (frame_interval_open || cockpit_open || !post_present_begun ||
				end_frame_count == 0 || i + 1 != commands_.size())
				record_error(kCaptureInvalidCommandGrammar, ci, kInvalidId);
			present_seen = true;
			break;
		case CaptureCommandType::ReadPixel:
			if (!frame_interval_open || i + 1 != commands_.size())
				record_error(kCaptureInvalidCommandGrammar, ci, kInvalidId);
			if (static_cast<uint32_t>(command.payload.read_pixel.source) >=
					static_cast<uint32_t>(ImageSemantic::Count) ||
				static_cast<uint32_t>(command.payload.read_pixel.format) >=
				static_cast<uint32_t>(ReadbackFormat::Count))
				record_error(kCaptureInvalidEnum, ci, kInvalidId);
			if (command.payload.read_pixel.x < active_logical_clip.x ||
				command.payload.read_pixel.y < active_logical_clip.y ||
				command.payload.read_pixel.x >=
					int64_t(active_logical_clip.x) + active_logical_clip.width ||
				command.payload.read_pixel.y >=
					int64_t(active_logical_clip.y) + active_logical_clip.height)
				record_error(kCaptureInvalidRect, ci, kInvalidId);
			break;
		case CaptureCommandType::ReadImage:
			if (!began_any_frame_interval || present_seen)
				record_error(kCaptureInvalidCommandGrammar, ci, kInvalidId);
			if (static_cast<uint32_t>(command.payload.read_image.source) >=
					static_cast<uint32_t>(ImageSemantic::Count) ||
				static_cast<uint32_t>(command.payload.read_image.format) >=
				static_cast<uint32_t>(ReadbackFormat::Count) ||
				static_cast<uint32_t>(command.payload.read_image.row_order) >=
				static_cast<uint32_t>(ReadbackRowOrder::Count))
				record_error(kCaptureInvalidEnum, ci, kInvalidId);
			if (frame_interval_open &&
				!RectFitsInside(command.payload.read_image.rect, active_logical_clip))
				record_error(kCaptureInvalidRect, ci, kInvalidId);
			break;
		case CaptureCommandType::EndFrame:
			if (!frame_interval_open ||
				(have_view_interval_serial &&
				 command.payload.end_frame.view_interval_serial <= last_view_interval_serial))
				record_error(kCaptureInvalidCommandGrammar, ci, kInvalidId);
			frame_interval_open = false;
			active_target_version = kInvalidId;
			++end_frame_count;
			last_view_interval_serial = command.payload.end_frame.view_interval_serial;
			have_view_interval_serial = true;
			break;
		default:
			break;
		}
	}
	if (!commands_.empty())
	{
		const CaptureCommandType terminal = commands_.back().type;
		if (terminal != CaptureCommandType::Present &&
			terminal != CaptureCommandType::ReadPixel)
			record_error(kCaptureInvalidCommandGrammar,
				static_cast<uint32_t>(commands_.size() - 1), kInvalidId);
		if (terminal == CaptureCommandType::Present && !present_seen)
			record_error(kCaptureInvalidCommandGrammar,
				static_cast<uint32_t>(commands_.size() - 1), kInvalidId);
	}
	if (out_result)
		*out_result = result;
	return result.errors == 0;
}

bool RenderCaptureSegment::Freeze(CaptureValidationResult *result)
{
	if (lifecycle_ != Lifecycle::Capturing)
		return false;
	if (!Validate(result))
		return false;
	lifecycle_ = Lifecycle::Frozen;
	return true;
}

bool RenderCaptureSegment::MarkCompiled()
{
	if (lifecycle_ != Lifecycle::Frozen)
		return false;
	lifecycle_ = Lifecycle::Compiled;
	return true;
}

} // namespace render
} // namespace piccu
