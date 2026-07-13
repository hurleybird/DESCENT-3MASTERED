#include "vk_compiler.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <sstream>

namespace piccu
{
namespace render
{
namespace vk
{

namespace
{

struct DrawWork
{
	uint32_t capture_command = kInvalidId;
	RenderTargetClass target = RenderTargetClass::Scene;
	ViewStateId view = kInvalidId;
	CapturedShaderRasterState raster = {};
	CapturedMaterial material = {};
	CapturedTransform transform = {};
	PayloadRef payload = kInvalidId;
	PrimitiveSourceKind source = PrimitiveSourceKind::PolygonFan;
	DepthInterpretation depth = DepthInterpretation::Irrelevant;
	uint32_t first_vertex = 0;
	uint32_t vertex_count = 0;
	uint32_t first_index = 0;
	uint32_t index_count = 0;
	int32_t vertex_offset = 0;
	uint32_t draw_header_index = 0;
	uint32_t page_index = 0;
	uint32_t indirect_index = kInvalidId;
	uint32_t expanded_instance_count = 0;
	PayloadDataId cockpit_backing = kInvalidId;
	GeometryMode geometry_mode = GeometryMode::T0Stream;
	MeshHandle mesh = { kInvalidId, 0 };
	RetainedMeshBufferReferences retained;
	FrameBufferSlice terrain_scratch;
	FrameBufferSlice terrain_base_output;
	FrameBufferSlice terrain_payload_output;
	FrameBufferSlice terrain_indirect_output;
	FrameBufferSlice terrain_view;
	uint32_t terrain_work_count = 0;
	uint32_t terrain_batch_count = 0;
	uint32_t terrain_output_capacity = 0;
	bool indexed = false;
};

struct RetainedPayloadCopy
{
	VkBuffer source = VK_NULL_HANDLE;
	VkDeviceSize source_offset = 0;
	uint32_t destination_word = 0;
	VkDeviceSize byte_size = 0;
};

struct TextureDescriptorKey
{
	TextureVersionId image = kInvalidId;
	uint32_t sampler = 0;

	bool operator==(const TextureDescriptorKey &other) const noexcept
	{
		return image == other.image && sampler == other.sampler;
	}
};

struct DescriptorPage
{
	ViewStateId view = kInvalidId;
	RenderTargetClass target = RenderTargetClass::Scene;
	std::vector<TextureDescriptorKey> images;
	std::vector<TextureDescriptorKey> arrays;
	std::vector<VkImageView> image_views;
	std::vector<VkImageView> array_views;
	std::vector<VkSampler> image_samplers;
	std::vector<VkSampler> array_samplers;
	WorldDescriptorSets sets;
	FrameBufferSlice frame_view;
	FrameBufferSlice payload_override;
	bool exclusive = false;
};

struct StagedTables
{
	FrameBufferSlice vertices;
	FrameBufferSlice indices;
	FrameBufferSlice draw_headers;
	FrameBufferSlice states;
	FrameBufferSlice materials;
	FrameBufferSlice transforms;
	FrameBufferSlice lights;
	FrameBufferSlice specular;
	FrameBufferSlice payload_words;
	FrameBufferSlice world_aux;
	FrameBufferSlice indirect;
};

class StateTrackerTransaction
{
public:
	explicit StateTrackerTransaction(ResourceStateTracker *tracker)
		: tracker_(tracker), before_(tracker ? tracker->Snapshot(1) :
			ResourceStateSnapshot())
	{
	}

	~StateTrackerTransaction()
	{
		if (tracker_ && !submitted_)
			tracker_->Restore(before_);
	}

	void MarkSubmitted() noexcept { submitted_ = true; }

private:
	ResourceStateTracker *tracker_ = nullptr;
	ResourceStateSnapshot before_;
	bool submitted_ = false;
};

VkDeviceSize AlignUp(VkDeviceSize value, VkDeviceSize alignment)
{
	if (alignment <= 1)
		return value;
	const VkDeviceSize remainder = value % alignment;
	return remainder ? value + (alignment - remainder) : value;
}

uint32_t FirstResource(GraphResourceMask mask)
{
	for (uint32_t bit = 0; bit < static_cast<uint32_t>(GraphResource::Count);
		++bit)
		if ((mask & (GraphResourceMask(1) << bit)) != 0)
			return bit;
	return static_cast<uint32_t>(GraphResource::Count);
}

VkImageSubresourceRange FullRange(const TargetImageRef &image)
{
	VkImageSubresourceRange range = {};
	range.aspectMask = image.aspect;
	range.baseMipLevel = 0;
	range.levelCount = 1;
	range.baseArrayLayer = 0;
	range.layerCount = 1;
	return range;
}

VkPrimitiveTopology TopologyFor(PrimitiveSourceKind source)
{
	switch (source)
	{
	case PrimitiveSourceKind::Line:
	case PrimitiveSourceKind::SpecialLine:
	case PrimitiveSourceKind::SpecialLineBatch:
		return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
	case PrimitiveSourceKind::Point:
		return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
	default:
		return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	}
}

constexpr TextureVersionId kSoftDepthTextureSentinel = kInvalidId - 1u;

bool IsExpandedFamily(WorldPipelineFamily family)
{
	return family == WorldPipelineFamily::ExpandedLine ||
		family == WorldPipelineFamily::ExpandedPoint;
}

bool IsWorldDrawCommand(CaptureCommandType type)
{
	return type == CaptureCommandType::DrawStream ||
		type == CaptureCommandType::DrawRetained ||
		type == CaptureCommandType::FlushFontBatches;
}

bool DrawCommandsAreContiguous(const RenderCaptureSegment &capture,
	uint32_t first_command, uint32_t last_command)
{
	if (first_command > last_command || last_command >= capture.Commands().size())
		return false;
	for (uint32_t command = first_command; command <= last_command; ++command)
		if (!IsWorldDrawCommand(capture.Commands()[command].type))
			return false;
	return true;
}

void GtaoSamplePattern(uint32_t requested, uint32_t *directions,
	uint32_t *steps)
{
	uint32_t samples = std::max(1u, std::min(1024u, requested));
	uint32_t count = 0;
	if (samples <= 12)
		count = std::min(4u, samples);
	else if (samples <= 24)
		count = 6;
	else if (samples <= 32)
		count = 8;
	else
		count = std::max(8u, std::min(32u, static_cast<uint32_t>(
			std::sqrt(static_cast<float>(samples) * 2.0f) + 0.5f)));
	*directions = count;
	*steps = std::max(1u, (samples + count - 1u) / count);
}

WorldPipelineFamily FamilyFor(PrimitiveSourceKind source,
	RasterFamily raster)
{
	if (raster == RasterFamily::ExpandedLine)
		return WorldPipelineFamily::ExpandedLine;
	if (raster == RasterFamily::ExpandedPoint)
		return WorldPipelineFamily::ExpandedPoint;
	switch (source)
	{
	case PrimitiveSourceKind::Polygon2D:
	case PrimitiveSourceKind::Bitmap:
	case PrimitiveSourceKind::Editor:
		return WorldPipelineFamily::Ui;
	case PrimitiveSourceKind::Font:
		return WorldPipelineFamily::Font;
	case PrimitiveSourceKind::ParticleInstances:
		return WorldPipelineFamily::Particle;
	case PrimitiveSourceKind::Line:
	case PrimitiveSourceKind::SpecialLine:
	case PrimitiveSourceKind::SpecialLineBatch:
		return WorldPipelineFamily::ExpandedLine;
	case PrimitiveSourceKind::Point:
		return WorldPipelineFamily::ExpandedPoint;
	default:
		return WorldPipelineFamily::Stream;
	}
}

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

const CapturedTextureVersion *FindCapturedTexture(
	const RenderCaptureSegment &capture, TextureVersionId id)
{
	for (const CapturedTextureVersion &version : capture.TextureVersions())
		if (version.id == id)
			return &version;
	return nullptr;
}

const CapturedPayloadRecord *PayloadRecord(const RenderCaptureSegment &capture,
	PayloadDataId id)
{
	return id != kInvalidId && id < capture.PayloadRecords().size() ?
		&capture.PayloadRecords()[id] : nullptr;
}

const uint8_t *PayloadData(const RenderCaptureSegment &capture,
	PayloadDataId id)
{
	return capture.PayloadData(id);
}

void CopyMatrix(float output[16], const float input[16])
{
	std::memcpy(output, input, sizeof(float) * 16);
}

void Identity(float matrix[16])
{
	std::memset(matrix, 0, sizeof(float) * 16);
	matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0f;
}

} // namespace

struct FrameCompiler::Impl
{
	FrameCompilerCreateInfo ci = {};
	FramePlanner planner;
	FramePlan last_plan;
	std::string last_error;
	std::vector<ResourceStateSnapshot> snapshots;
	// CompileAndSubmit is single-threaded.  Reuse its high-water allocations so
	// a warm frame does not rebuild the complete lowering workspace on the heap.
	std::vector<BaseVertex> scratch_vertices;
	std::vector<uint32_t> scratch_indices;
	std::vector<DrawWork> scratch_draws;
	std::vector<WorldPipelineKey> scratch_world_pipelines;
	std::vector<GpuDrawHeader> scratch_headers;
	std::vector<GpuShaderState> scratch_states;
	std::vector<GpuMaterial> scratch_materials;
	std::vector<GpuTransform> scratch_transforms;
	std::vector<GpuDynamicLight> scratch_lights;
	std::vector<GpuSpecularBlock> scratch_specular;
	std::vector<uint32_t> scratch_payload_words;
	std::vector<GpuWorldAux> scratch_world_aux;
	std::vector<RetainedPayloadCopy> scratch_retained_copies;
	std::vector<DescriptorPage> scratch_pages;
	std::vector<VkDrawIndexedIndirectCommand> scratch_indirect_commands;
	uint64_t next_snapshot_serial = 0;
	bool ready = false;

	bool Fail(const char *stage, const char *message)
	{
		std::ostringstream stream;
		stream << stage << ": " << message;
		last_error = stream.str();
		return false;
	}

	TargetImageRef ResourceImage(GraphResource resource, uint32_t level,
		uint32_t post_present_index) const
	{
		if (resource == GraphResource::PostPresent)
			return ci.targets->GraphImage(resource, post_present_index);
		return ci.targets->GraphImage(resource, level);
	}

	TargetImageRef ImageSemanticRef(ImageSemantic semantic,
		uint32_t post_present_index) const
	{
		switch (semantic)
		{
		case ImageSemantic::SceneColor:
			return ci.targets->Attachment(RenderTargetClass::Scene, 0);
		case ImageSemantic::SceneDepth:
			return ci.targets->Attachment(RenderTargetClass::Scene, 5);
		case ImageSemantic::Velocity:
			return ci.targets->Attachment(RenderTargetClass::Scene, 1);
		case ImageSemantic::ProtectionMask:
			return ci.targets->Attachment(RenderTargetClass::Scene, 2);
		case ImageSemantic::AoClass:
			return ci.targets->Attachment(RenderTargetClass::Scene, 3);
		case ImageSemantic::MotionObjectId:
			return ci.targets->Attachment(RenderTargetClass::Scene, 4);
		case ImageSemantic::CapturedWorldColor:
			return ci.targets->GraphImage(GraphResource::CapturedWorldColor);
		case ImageSemantic::CapturedWorldDepth:
			return ci.targets->GraphImage(GraphResource::CapturedWorldDepth);
		case ImageSemantic::PostPresent:
			return ci.targets->GraphImage(GraphResource::PostPresent,
				post_present_index);
		case ImageSemantic::CockpitScene:
			return ci.targets->Attachment(RenderTargetClass::CockpitScene, 0);
		case ImageSemantic::CockpitComposite:
			return ci.targets->GraphImage(GraphResource::CockpitResolved);
		default:
			return {};
		}
	}

	bool BuildDrawWork(const RenderCaptureSegment &capture,
		std::vector<BaseVertex> *vertices,
		std::vector<DrawWork> *draws) const
	{
		RenderTargetClass target = RenderTargetClass::Scene;
		ViewStateId view = kInvalidId;
		TargetLayoutId target_layout = kInvalidId;
		ViewportId viewport = kInvalidId;
		PayloadDataId cockpit_backing = kInvalidId;
		std::vector<EnqueueFontGlyphCommand> glyphs;
		for (uint32_t command_index = 0;
			command_index < capture.Commands().size(); ++command_index)
		{
			const CaptureCommand &command = capture.Commands()[command_index];
			if (command.type == CaptureCommandType::BeginFrameTarget)
			{
				target = command.payload.begin_frame_target.target;
				view = command.payload.begin_frame_target.view_state;
				viewport = command.payload.begin_frame_target.physical_viewport;
				const TargetVersionId version =
					command.payload.begin_frame_target.active_target_version;
				if (version >= capture.TargetVersions().size())
					return false;
				target_layout = capture.TargetVersions()[version].target_layout;
			}
			else if (command.type == CaptureCommandType::BeginCockpitScene)
				cockpit_backing =
					command.payload.begin_cockpit_scene.backing_effect_state;
			else if (command.type == CaptureCommandType::EndCockpitScene)
				cockpit_backing = kInvalidId;
			else if (command.type == CaptureCommandType::DrawStream)
			{
				const DrawStreamCommand &draw = command.payload.draw_stream;
				if (draw.state >= capture.States().size() ||
					draw.material >= capture.Materials().size() ||
					draw.transform >= capture.Transforms().size() ||
					draw.view >= capture.Views().size())
					return false;
				DrawWork work = {};
				work.capture_command = command_index;
				work.target = target;
				work.view = draw.view;
				work.raster = capture.States()[draw.state];
				work.material = capture.Materials()[draw.material];
				work.transform = capture.Transforms()[draw.transform];
				work.payload = draw.optional_payload;
				work.cockpit_backing = cockpit_backing;
				work.source = draw.classification.source_kind;
				work.depth = draw.geometry.depth_interpretation;
				work.first_vertex = draw.geometry.vertices.offset;
				work.vertex_count = draw.geometry.vertices.count;
				work.first_index = draw.geometry.indices.offset;
				work.index_count = draw.geometry.indices.count;
				work.vertex_offset = static_cast<int32_t>(
					draw.geometry.vertices.offset);
				work.indexed = draw.geometry.indices.count != 0;
				draws->push_back(work);
			}
			else if (command.type == CaptureCommandType::DrawRetained)
			{
				const DrawRetainedCommand &draw = command.payload.draw_retained;
				if (draw.state >= capture.States().size() ||
					draw.material >= capture.Materials().size() ||
					draw.transform >= capture.Transforms().size() ||
					draw.view >= capture.Views().size() ||
					(draw.geometry_mode != GeometryMode::T1Retained &&
					 draw.geometry_mode != GeometryMode::T2Terrain) ||
					!ci.retained_world)
					return false;
				DrawWork work = {};
				work.capture_command = command_index;
				work.target = target;
				work.view = draw.view;
				work.raster = capture.States()[draw.state];
				work.material = capture.Materials()[draw.material];
				work.transform = capture.Transforms()[draw.transform];
				work.payload = draw.optional_payload;
				work.source = draw.classification.source_kind;
				work.depth = draw.geometry_mode == GeometryMode::T2Terrain ?
					DepthInterpretation::AlreadyMapped :
					DepthInterpretation::EyeZLegacyMapped;
				work.first_index = draw.first_index;
				work.index_count = draw.index_count;
				work.vertex_offset = draw.base_vertex;
				work.indexed = draw.geometry_mode == GeometryMode::T1Retained;
				work.geometry_mode = draw.geometry_mode;
				work.mesh = draw.mesh;
				work.cockpit_backing = cockpit_backing;
				if (!ci.retained_world->GetMeshBufferReferences(draw.mesh,
					&work.retained))
					return false;
				if (draw.geometry_mode == GeometryMode::T1Retained &&
					(!work.retained.vertices.Valid() || !work.retained.indices.Valid()))
					return false;
				if (draw.geometry_mode == GeometryMode::T2Terrain)
				{
					if (!work.retained.terrain ||
						!work.retained.terrain_cells.Valid() ||
						!work.retained.terrain_work.Valid() ||
						!work.retained.terrain_batches.Valid() ||
						!work.retained.terrain_indirect.Valid() ||
						work.retained.terrain_maximum_output_vertices == 0 ||
						draw.optional_payload == kInvalidId ||
						draw.optional_payload >= capture.PayloadBindings().size())
						return false;
					const CapturedPayloadBinding &binding =
						capture.PayloadBindings()[draw.optional_payload];
					if ((binding.validity_flags & kPayloadHasTerrainViewInput) == 0)
						return false;
					work.terrain_work_count = work.retained.terrain_work.element_count;
					work.terrain_batch_count = work.retained.terrain_batches.element_count;
					work.terrain_output_capacity =
						work.retained.terrain_maximum_output_vertices;
					work.material.image2d_array[0] =
						work.retained.terrain_base_texture_array;
					work.material.image2d_array[1] =
						work.retained.terrain_lightmap_array;
				}
				draws->push_back(work);
			}
			else if (command.type == CaptureCommandType::EnqueueFontGlyph)
				glyphs.push_back(command.payload.enqueue_font_glyph);
			else if (command.type == CaptureCommandType::FlushFontBatches)
			{
				size_t begin = 0;
				while (begin < glyphs.size())
				{
					const TextureVersionId texture = glyphs[begin].texture_version;
					const uint32_t bucket = glyphs[begin].bucket;
					size_t end = begin + 1;
					while (end < glyphs.size() &&
						glyphs[end].texture_version == texture &&
						glyphs[end].bucket == bucket)
						++end;
					DrawWork work = {};
					work.capture_command = command_index;
					work.target = target;
					work.view = command.payload.flush_font_batches.view_state;
					work.source = PrimitiveSourceKind::Font;
					work.depth = DepthInterpretation::Irrelevant;
					work.first_vertex = static_cast<uint32_t>(vertices->size());
					work.vertex_count = static_cast<uint32_t>((end - begin) * 6);
					work.indexed = false;
					work.raster.target_layout = target_layout;
					work.raster.sample_count = target == RenderTargetClass::PostPresent ?
						1u : ci.targets->SceneLayout().msaa_samples;
					work.raster.mrt_write_mask = target == RenderTargetClass::Scene ?
						(kWriteColor | kWriteProtectionMask | kWriteAoClass) :
						kWriteColor;
					work.raster.raster_family = RasterFamily::Ordinary;
					work.raster.front_face = 0;
					work.raster.depth_compare = 1;
					work.raster.viewport = work.raster.scissor = viewport;
					work.raster.shader.shader_flags = kShaderTextured;
					work.raster.shader.alpha_type = bucket ? 9u : 1u;
					work.raster.shader.blend_class = bucket ?
						static_cast<uint32_t>(BlendClass::Saturate) :
						static_cast<uint32_t>(BlendClass::Alpha);
					work.raster.shader.alpha_value = 255;
					work.raster.shader.alpha_factor = 1.0f;
					work.raster.shader.ao_class = 255;
					Identity(work.transform.current_model);
					Identity(work.transform.previous_model);
					for (uint32_t slot = 0; slot < 4; ++slot)
					{
						work.material.image2d[slot] = ci.textures->Diagnostic2D();
						work.material.image2d_array[slot] =
							ci.textures->DiagnosticArray();
						work.material.sampler[slot] = 14;
					}
					work.material.image2d_array[0] = texture;
					for (size_t glyph_index = begin; glyph_index < end;
						++glyph_index)
					{
						const EnqueueFontGlyphCommand &glyph = glyphs[glyph_index];
						for (uint32_t vertex_index = 0; vertex_index < 6;
							++vertex_index)
						{
							BaseVertex vertex = glyph.vertices[vertex_index];
							vertex.uv1[0] = static_cast<float>(glyph.texture_layer);
							uint32_t alpha = static_cast<uint32_t>(
								std::max(0.0f, std::min(1.0f, glyph.alpha)) * 255.0f + 0.5f);
							vertex.rgba8 = (vertex.rgba8 & 0x00ffffffu) |
								(alpha << 24);
							vertices->push_back(vertex);
						}
					}
					draws->push_back(work);
					begin = end;
				}
				glyphs.clear();
			}
		}
		return glyphs.empty();
	}

	bool AppendPayloadWords(const RenderCaptureSegment &capture,
		PayloadDataId payload, std::vector<uint32_t> *words,
		uint32_t *word_offset) const
	{
		if (payload == kInvalidId)
		{
			*word_offset = 0;
			return true;
		}
		const CapturedPayloadRecord *record = PayloadRecord(capture, payload);
		const uint8_t *data = PayloadData(capture, payload);
		if (!record || !data)
			return false;
		while ((words->size() * sizeof(uint32_t)) % record->alignment)
			words->push_back(0);
		*word_offset = static_cast<uint32_t>(words->size());
		const size_t old_bytes = words->size() * sizeof(uint32_t);
		const VkDeviceSize aligned_size = AlignUp(record->byte_size, 4);
		if (aligned_size > std::numeric_limits<size_t>::max() - old_bytes)
			return false;
		const size_t new_bytes = old_bytes + static_cast<size_t>(aligned_size);
		words->resize(new_bytes / sizeof(uint32_t));
		std::memcpy(reinterpret_cast<uint8_t *>(words->data()) + old_bytes,
			data, record->byte_size);
		return true;
	}

	bool BuildGpuTables(const RenderCaptureSegment &capture,
		const std::vector<BaseVertex> &vertices, std::vector<DrawWork> *draws,
		std::vector<GpuDrawHeader> *headers,
		std::vector<GpuShaderState> *states,
		std::vector<GpuMaterial> *materials,
		std::vector<GpuTransform> *transforms,
		std::vector<GpuDynamicLight> *lights,
		std::vector<GpuSpecularBlock> *specular,
		std::vector<uint32_t> *payload_words,
		std::vector<GpuWorldAux> *world_aux,
		std::vector<RetainedPayloadCopy> *retained_copies) const
	{
		// Index zero is the normative null record.  Real payloads begin at one.
		lights->push_back(GpuDynamicLight{});
		specular->push_back(GpuSpecularBlock{});
		world_aux->push_back(GpuWorldAux{});
		for (DrawWork &draw : *draws)
		{
			GpuShaderState state = {};
			const CapturedShaderState &source = draw.raster.shader;
			state.shader_flags = source.shader_flags;
			state.texture_type = source.texture_type;
			state.overlay_type = source.overlay_type;
			state.lighting_color_model = source.lighting_color_model;
			state.alpha_type = source.alpha_type;
			state.alpha_value = source.alpha_value;
			state.blend_class = source.blend_class;
			state.draw_classification = source.draw_classification;
			state.alpha_factor = source.alpha_factor;
			state.z_bias = source.z_bias;
			state.fog_near_mapped = source.fog_near_mapped;
			state.fog_far_mapped = source.fog_far_mapped;
			std::memcpy(state.flat_color, source.flat_color,
				sizeof(state.flat_color));
			std::memcpy(state.fog_color, source.fog_color,
				sizeof(state.fog_color));
			std::memcpy(state.light_direction, source.light_direction,
				sizeof(state.light_direction));
			std::memcpy(state.post_values, source.post_values,
				sizeof(state.post_values));
			state.motion_object_id = source.motion_object_id;
			state.motion_flags = source.motion_flags;
			state.ao_class = source.ao_class;
			state.state_flags2 = source.state_flags2;
			state.vertex_index_base = draw.geometry_mode == GeometryMode::T2Terrain ?
				0u : static_cast<uint32_t>(draw.indexed ?
					draw.vertex_offset : static_cast<int32_t>(draw.first_vertex));

			GpuDrawHeader header = {};
			header.state_index = static_cast<uint32_t>(states->size());
			header.material_index = static_cast<uint32_t>(materials->size());
			header.transform_index = static_cast<uint32_t>(transforms->size());
			header.flags = static_cast<uint32_t>(draw.geometry_mode);

			if (draw.payload != kInvalidId)
			{
				if (draw.payload >= capture.PayloadBindings().size())
					return false;
				const CapturedPayloadBinding &binding =
					capture.PayloadBindings()[draw.payload];
				if ((binding.validity_flags & kPayloadHasPerspectiveVertices) &&
					!AppendPayloadWords(capture, binding.perspective_vertices,
						payload_words, &header.vertex_payload_offset))
					return false;
				if (binding.validity_flags & kPayloadHasPerspectiveVertices)
					header.flags |= kDrawHasPerspectivePayload;
				if ((binding.validity_flags & kPayloadHasMotionVertices) &&
					!AppendPayloadWords(capture, binding.motion_vertices,
						payload_words, &header.motion_payload_offset))
					return false;
				if (binding.validity_flags & kPayloadHasMotionVertices)
					header.flags |= kDrawHasMotionPayload;
				if ((binding.validity_flags & kPayloadHasSpecularVertices) &&
					!AppendPayloadWords(capture, binding.specular_vertices,
						payload_words, &header.specular_payload_offset))
					return false;
				if (binding.validity_flags & kPayloadHasSpecularVertices)
					header.flags |= kDrawHasSpecularPayload;

				if (binding.validity_flags & kPayloadHasDynamicLights)
				{
					const CapturedPayloadRecord *record = PayloadRecord(capture,
						binding.dynamic_lights);
					const uint8_t *data = PayloadData(capture,
						binding.dynamic_lights);
					if (!record || !data ||
						record->byte_size % sizeof(GpuDynamicLight) != 0)
						return false;
					state.dynamic_light_first = static_cast<uint32_t>(lights->size());
					state.dynamic_light_count = record->byte_size /
						sizeof(GpuDynamicLight);
					const GpuDynamicLight *typed =
						reinterpret_cast<const GpuDynamicLight *>(data);
					lights->insert(lights->end(), typed,
						typed + state.dynamic_light_count);
				}
				if (binding.validity_flags & kPayloadHasSpecularBlock)
				{
					const CapturedPayloadRecord *record = PayloadRecord(capture,
						binding.specular_block);
					const uint8_t *data = PayloadData(capture,
						binding.specular_block);
					if (!record || !data || record->byte_size != sizeof(GpuSpecularBlock))
						return false;
					state.specular_block_index =
						static_cast<uint32_t>(specular->size());
					specular->push_back(
						*reinterpret_cast<const GpuSpecularBlock *>(data));
				}
				if (binding.validity_flags & kPayloadHasWorldAux)
				{
					const CapturedPayloadRecord *record = PayloadRecord(capture,
						binding.world_aux);
					const uint8_t *data = PayloadData(capture, binding.world_aux);
					if (!record || !data || record->byte_size != sizeof(GpuWorldAux))
						return false;
					header.room_or_terrain_index =
						static_cast<uint32_t>(world_aux->size());
					world_aux->push_back(*reinterpret_cast<const GpuWorldAux *>(data));
				}
			}

			if (draw.geometry_mode == GeometryMode::T1Retained)
			{
				if (draw.capture_command >= capture.Commands().size())
					return false;
				const DrawRetainedCommand &retained_draw =
					capture.Commands()[draw.capture_command].payload.draw_retained;
				auto append_retained = [&](const RetainedBufferReference &reference,
					const Span32 &span, uint32_t stride, uint32_t flag,
					uint32_t *header_offset) -> bool {
					if (span.count == 0)
						return true;
					if (!reference.Valid() || span.offset < reference.first_element)
						return false;
					const uint32_t relative = span.offset - reference.first_element;
					if (relative > reference.element_count ||
						span.count > reference.element_count - relative)
						return false;
					while ((payload_words->size() * sizeof(uint32_t)) % 16u)
						payload_words->push_back(0);
					*header_offset = static_cast<uint32_t>(payload_words->size());
					const VkDeviceSize bytes = VkDeviceSize(span.count) * stride;
					RetainedPayloadCopy copy;
					copy.source = reference.buffer;
					copy.source_offset = reference.byte_offset +
						VkDeviceSize(relative) * stride;
					copy.destination_word = *header_offset;
					copy.byte_size = bytes;
					retained_copies->push_back(copy);
					payload_words->resize(payload_words->size() +
						static_cast<size_t>(AlignUp(bytes, 4) / 4));
					header.flags |= flag;
					return true;
				};
				if (!append_retained(draw.retained.perspective_payload,
						retained_draw.perspective_payload,
						sizeof(PerspectiveVertexPayload), kDrawHasPerspectivePayload,
						&header.vertex_payload_offset) ||
					!append_retained(draw.retained.motion_payload,
						retained_draw.motion_payload,
						sizeof(MotionVertexPayload), kDrawHasMotionPayload,
						&header.motion_payload_offset) ||
					!append_retained(draw.retained.specular_payload,
						retained_draw.specular_payload,
						sizeof(SpecularVertexPayload), kDrawHasSpecularPayload,
						&header.specular_payload_offset))
					return false;
			}

			if ((source.shader_flags & kShaderCockpit) != 0 &&
				draw.cockpit_backing != kInvalidId)
			{
				const CapturedPayloadRecord *record = PayloadRecord(capture,
					draw.cockpit_backing);
				const uint8_t *data = PayloadData(capture, draw.cockpit_backing);
				if (!record || !data ||
					record->byte_size != sizeof(CapturedCockpitBackingEffect))
					return false;
				const CapturedCockpitBackingEffect &backing =
					*reinterpret_cast<const CapturedCockpitBackingEffect *>(data);
				GpuWorldAux aux = {};
				aux.params[0] = backing.enabled ? 1.0f : 0.0f;
				aux.params[1] = backing.alpha;
				aux.params[2] = backing.darkness;
				aux.params[3] = backing.scanlines_enabled ? 1.0f : 0.0f;
				aux.fog_color[0] = backing.scanline_strength;
				aux.fog_color[1] = backing.scanline_spacing;
				aux.fog_color[2] = backing.scanline_thickness;
				aux.fog_color[3] = backing.scanline_phase;
				header.room_or_terrain_index =
					static_cast<uint32_t>(world_aux->size());
				world_aux->push_back(aux);
			}

			const WorldPipelineFamily family = FamilyFor(draw.source,
				draw.raster.raster_family);
			if (IsExpandedFamily(family))
			{
				if (draw.first_vertex > vertices.size() ||
					draw.vertex_count > vertices.size() - draw.first_vertex)
					return false;
				while ((payload_words->size() * sizeof(uint32_t)) % 16u)
					payload_words->push_back(0);
				header.vertex_payload_offset =
					static_cast<uint32_t>(payload_words->size());
				if (family == WorldPipelineFamily::ExpandedLine)
				{
					if ((draw.vertex_count & 1u) != 0)
						return false;
					draw.expanded_instance_count = draw.vertex_count / 2u;
					for (uint32_t instance = 0; instance < draw.expanded_instance_count;
						++instance)
					{
						ExpandedLineInstance expanded = {};
						expanded.endpoints[0] = vertices[draw.first_vertex + instance * 2u];
						expanded.endpoints[1] = vertices[draw.first_vertex + instance * 2u + 1u];
						expanded.half_width_pixels = 0.5f;
						const size_t old_words = payload_words->size();
						payload_words->resize(old_words + kExpandedLineInstanceWords);
						std::memcpy(payload_words->data() + old_words, &expanded,
							sizeof(expanded));
					}
				}
				else
				{
					draw.expanded_instance_count = draw.vertex_count;
					for (uint32_t instance = 0; instance < draw.expanded_instance_count;
						++instance)
					{
						ExpandedPointInstance expanded = {};
						expanded.point = vertices[draw.first_vertex + instance];
						expanded.half_size_pixels = 0.5f;
						const size_t old_words = payload_words->size();
						payload_words->resize(old_words + kExpandedPointInstanceWords);
						std::memcpy(payload_words->data() + old_words, &expanded,
							sizeof(expanded));
					}
				}
			}

			GpuMaterial material = {};
			std::memcpy(material.image2d, draw.material.image2d,
				sizeof(material.image2d));
			std::memcpy(material.image2d_array, draw.material.image2d_array,
				sizeof(material.image2d_array));
			std::memcpy(material.sampler, draw.material.sampler,
				sizeof(material.sampler));
			std::memcpy(material.uv_params, draw.material.uv_params,
				sizeof(material.uv_params));
			GpuTransform transform = {};
			std::memcpy(transform.current_model, draw.transform.current_model,
				sizeof(transform.current_model));
			std::memcpy(transform.previous_model, draw.transform.previous_model,
				sizeof(transform.previous_model));

			draw.draw_header_index = static_cast<uint32_t>(headers->size());
			headers->push_back(header);
			states->push_back(state);
			materials->push_back(material);
			transforms->push_back(transform);
		}
		if (payload_words->empty()) payload_words->resize(4);
		return true;
	}

	bool BuildDescriptorPages(const RenderCaptureSegment &capture,
		std::vector<DrawWork> *draws, std::vector<GpuMaterial> *materials,
		std::vector<DescriptorPage> *pages) const
	{
		const uint32_t tier = ci.pipelines->DescriptorPageTier();
		if (tier == 0)
			return false;
		for (DrawWork &draw : *draws)
		{
			TextureDescriptorKey needed_2d[4] = {};
			TextureDescriptorKey needed_array[4] = {};
			uint32_t needed_2d_count = 0;
			uint32_t needed_array_count = 0;
			auto append_unique = [](TextureDescriptorKey (&keys)[4],
				uint32_t *count, const TextureDescriptorKey &key) {
				for (uint32_t index = 0; index < *count; ++index)
					if (keys[index] == key)
						return;
				keys[(*count)++] = key;
			};
			for (uint32_t i = 0; i < 4; ++i)
			{
				const TextureVersionId image_id = i == 3 &&
					(draw.raster.shader.shader_flags & kShaderSoftParticle) != 0 ?
					kSoftDepthTextureSentinel : draw.material.image2d[i];
				const TextureDescriptorKey image = { image_id,
					draw.material.sampler[i] };
				const TextureDescriptorKey array = {
					draw.material.image2d_array[i], draw.material.sampler[i] };
				if (image.image != kInvalidId)
					append_unique(needed_2d, &needed_2d_count, image);
				if (array.image != kInvalidId)
					append_unique(needed_array, &needed_array_count, array);
			}
			if (needed_2d_count > tier || needed_array_count > 8)
				return false;
			DescriptorPage *page = pages->empty() ? nullptr : &pages->back();
			uint32_t new_2d = 0, new_array = 0;
			if (page && (page->view != draw.view || page->target != draw.target ||
				page->exclusive || draw.geometry_mode == GeometryMode::T2Terrain))
				page = nullptr;
			if (page)
			{
				for (uint32_t index = 0; index < needed_2d_count; ++index)
				{
					const TextureDescriptorKey &key = needed_2d[index];
					if (std::find(page->images.begin(), page->images.end(), key) ==
						page->images.end()) ++new_2d;
				}
				for (uint32_t index = 0; index < needed_array_count; ++index)
				{
					const TextureDescriptorKey &key = needed_array[index];
					if (std::find(page->arrays.begin(), page->arrays.end(), key) ==
						page->arrays.end()) ++new_array;
				}
				if (page->images.size() + new_2d > tier ||
					page->arrays.size() + new_array > 8)
					page = nullptr;
			}
			if (!page)
			{
				DescriptorPage created;
				created.view = draw.view;
				created.target = draw.target;
				created.exclusive = draw.geometry_mode == GeometryMode::T2Terrain;
				created.images.push_back({ ci.textures->Diagnostic2D(), 0u });
				created.arrays.push_back({ ci.textures->DiagnosticArray(), 16u });
				pages->push_back(created);
				page = &pages->back();
			}
			for (uint32_t index = 0; index < needed_2d_count; ++index)
			{
				const TextureDescriptorKey &key = needed_2d[index];
				if (std::find(page->images.begin(), page->images.end(), key) ==
					page->images.end()) page->images.push_back(key);
			}
			for (uint32_t index = 0; index < needed_array_count; ++index)
			{
				const TextureDescriptorKey &key = needed_array[index];
				if (std::find(page->arrays.begin(), page->arrays.end(), key) ==
					page->arrays.end()) page->arrays.push_back(key);
			}
			draw.page_index = static_cast<uint32_t>(pages->size() - 1);
			GpuMaterial &gpu = (*materials)[draw.draw_header_index];
			for (uint32_t i = 0; i < 4; ++i)
			{
				const TextureVersionId image_id = i == 3 &&
					(draw.raster.shader.shader_flags & kShaderSoftParticle) != 0 ?
					kSoftDepthTextureSentinel : draw.material.image2d[i];
				const TextureDescriptorKey image_key = { image_id,
					draw.material.sampler[i] };
				const TextureDescriptorKey array_key = {
					draw.material.image2d_array[i], draw.material.sampler[i] };
				const auto image = std::find(page->images.begin(),
					page->images.end(), image_key);
				const auto array = std::find(page->arrays.begin(),
					page->arrays.end(), array_key);
				gpu.image2d[i] = image == page->images.end() ? 0u :
					static_cast<uint32_t>(image - page->images.begin());
				gpu.image2d_array[i] = array == page->arrays.end() ? 0u :
					static_cast<uint32_t>(array - page->arrays.begin());
			}
		}
		(void)capture;
		return true;
	}

	template <typename T>
	bool StageVector(const std::vector<T> &values, FrameBufferClass kind,
		VkDeviceSize alignment, FrameBufferSlice *slice) const
	{
		if (values.empty())
			return false;
		return ci.frames->StageBuffer(values.data(),
			VkDeviceSize(values.size()) * sizeof(T), kind, alignment, slice);
	}

	bool PrepareTerrainEmitters(const RenderCaptureSegment &capture,
		std::vector<DrawWork> *draws, std::vector<DescriptorPage> *pages,
		VkCommandBuffer command)
	{
		const VkDeviceSize storage_alignment = std::max<VkDeviceSize>(1,
			ci.platform->SelectedDevice().properties.limits.
				minStorageBufferOffsetAlignment);
		for (DrawWork &draw : *draws)
		{
			if (draw.geometry_mode != GeometryMode::T2Terrain)
				continue;
			if (draw.page_index >= pages->size() || draw.payload == kInvalidId ||
				draw.payload >= capture.PayloadBindings().size())
				return false;
			const CapturedPayloadBinding &binding =
				capture.PayloadBindings()[draw.payload];
			const CapturedPayloadRecord *view_record = PayloadRecord(capture,
				binding.terrain_view_input);
			const uint8_t *view_bytes = PayloadData(capture,
				binding.terrain_view_input);
			if (!view_record || !view_bytes ||
				view_record->byte_size != sizeof(TerrainViewInput) ||
				draw.terrain_work_count == 0 || draw.terrain_batch_count == 0 ||
				draw.terrain_output_capacity == 0)
				return false;
			if (!ci.frames->StageBuffer(view_bytes, sizeof(TerrainViewInput),
					FrameBufferClass::Storage, storage_alignment, &draw.terrain_view))
				return false;
			const VkDeviceSize scratch_bytes = VkDeviceSize(draw.terrain_work_count) *
				sizeof(uint32_t) * 2u;
			const VkDeviceSize base_bytes = VkDeviceSize(draw.terrain_output_capacity) *
				sizeof(BaseVertex);
			const VkDeviceSize payload_bytes =
				VkDeviceSize(draw.terrain_output_capacity) *
				sizeof(TerrainVertexPayload);
			draw.terrain_scratch = ci.frames->Allocate(FrameBufferClass::Storage,
				scratch_bytes, storage_alignment);
			draw.terrain_base_output = ci.frames->Allocate(FrameBufferClass::Vertex,
				base_bytes, storage_alignment);
			draw.terrain_payload_output = ci.frames->Allocate(FrameBufferClass::Storage,
				payload_bytes, storage_alignment);
			draw.terrain_indirect_output.buffer =
				draw.retained.terrain_indirect.buffer;
			draw.terrain_indirect_output.offset =
				draw.retained.terrain_indirect.byte_offset;
			draw.terrain_indirect_output.size =
				draw.retained.terrain_indirect.byte_size;
			if (!draw.terrain_scratch.Valid() ||
				!draw.terrain_base_output.Valid() ||
				!draw.terrain_payload_output.Valid() ||
				!draw.terrain_indirect_output.Valid())
				return false;

			TerrainEmitterPush push;
			push.work_item_count = draw.terrain_work_count;
			push.batch_count = draw.terrain_batch_count;
			push.output_vertex_capacity = draw.terrain_output_capacity;
			TerrainEmitterDispatchPlan plan;
			if (!ci.pipelines->PlanTerrainEmitter(push, &plan))
				return false;
			(void)plan;
			VkDescriptorSet set = VK_NULL_HANDLE;
			if (!ci.pipelines->AllocateTerrainDescriptorSet(
					ci.frames->Current()->descriptor_pool, &set))
				return false;
			TerrainEmitterDescriptorWrite write;
			auto assign = [&](uint32_t slot, const FrameBufferSlice &slice) {
				write.bindings[slot].buffer = slice.buffer;
				write.bindings[slot].offset = slice.offset;
				write.bindings[slot].range = slice.size;
			};
			auto retained_slice = [](const RetainedBufferReference &reference) {
				FrameBufferSlice slice;
				slice.buffer = reference.buffer;
				slice.offset = reference.byte_offset;
				slice.size = reference.byte_size;
				return slice;
			};
			const FrameBufferSlice cells = retained_slice(draw.retained.terrain_cells);
			const FrameBufferSlice work = retained_slice(draw.retained.terrain_work);
			const FrameBufferSlice batches = retained_slice(draw.retained.terrain_batches);
			assign(kTerrainCellsBinding, cells);
			assign(kTerrainWorkBinding, work);
			assign(kTerrainBatchesBinding, batches);
			assign(kTerrainViewBinding, draw.terrain_view);
			assign(kTerrainScratchBinding, draw.terrain_scratch);
			assign(kTerrainBaseOutputBinding, draw.terrain_base_output);
			assign(kTerrainPayloadOutputBinding, draw.terrain_payload_output);
			assign(kTerrainIndirectOutputBinding, draw.terrain_indirect_output);
			if (!ci.pipelines->UpdateTerrainDescriptorSet(set, write, push))
				return false;

			const FrameBufferSlice transfer_writes[] = { draw.terrain_view };
			for (const FrameBufferSlice &slice : transfer_writes)
				ci.state_tracker->UseBuffer(slice.buffer, slice.offset, slice.size,
					{ VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
					  ci.platform->GraphicsQueueFamily(), ResourceIntent::Write });
			const FrameBufferSlice compute_reads[] = { cells, work, batches,
				draw.terrain_view };
			for (const FrameBufferSlice &slice : compute_reads)
				ci.state_tracker->UseBuffer(slice.buffer, slice.offset, slice.size,
					{ VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					  VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
					  ci.platform->GraphicsQueueFamily(), ResourceIntent::Read });
			const FrameBufferSlice compute_writes[] = { draw.terrain_scratch,
				draw.terrain_base_output, draw.terrain_payload_output,
				draw.terrain_indirect_output };
			for (const FrameBufferSlice &slice : compute_writes)
				ci.state_tracker->UseBuffer(slice.buffer, slice.offset, slice.size,
					{ VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					  VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
						VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
					  ci.platform->GraphicsQueueFamily(), ResourceIntent::ReadWrite });
			ci.state_tracker->Flush(command);
			if (!ci.pipelines->RecordTerrainEmitter(command, set, push))
				return false;
			(*pages)[draw.page_index].payload_override =
				draw.terrain_payload_output;
		}
		return true;
	}

	bool UploadGtaoNoise(const RenderCaptureSegment &capture,
		VkCommandBuffer command)
	{
		if (capture.TargetSignatures().empty() ||
			capture.TargetSignatures().back().preferred.gtao_enabled == 0)
			return true;
		const TargetImageRef noise = ci.targets->GraphImage(GraphResource::GtaoNoise);
		if (!noise.Valid() || noise.extent.width != 4 || noise.extent.height != 4)
			return false;
		static const float values[32] = {
			0.556725f,0.005520f,0.708315f,0.583199f,0.236644f,0.992380f,0.981091f,0.119804f,
			0.510866f,0.560499f,0.961497f,0.557862f,0.539955f,0.332871f,0.417807f,0.920779f,
			0.730747f,0.076690f,0.008562f,0.660104f,0.428921f,0.511342f,0.587871f,0.906406f,
			0.437980f,0.620309f,0.062196f,0.119485f,0.235646f,0.795892f,0.044437f,0.617311f,
		};
		FrameBufferSlice upload = ci.frames->AllocateUpload(32, 4);
		if (!upload.Valid() || !upload.mapped)
			return false;
		uint8_t *bytes = static_cast<uint8_t *>(upload.mapped);
		for (uint32_t i = 0; i < 32; ++i)
			bytes[i] = static_cast<uint8_t>(values[i] * 255.0f);
		if (!ci.allocator->Flush(ci.frames->Current()->upload,
			upload.offset, upload.size))
			return false;
		ci.state_tracker->UseBuffer(upload.buffer, upload.offset, upload.size,
			{ VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
			  ci.platform->GraphicsQueueFamily(), ResourceIntent::Read });
		ci.state_tracker->UseImage(noise.image, FullRange(noise),
			{ VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
			  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			  ci.platform->GraphicsQueueFamily(), ResourceIntent::Write });
		ci.state_tracker->Flush(command);
		VkBufferImageCopy2 region = { VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2 };
		region.bufferOffset = upload.offset;
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.layerCount = 1;
		region.imageExtent = { 4, 4, 1 };
		VkCopyBufferToImageInfo2 copy = {
			VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2
		};
		copy.srcBuffer = upload.buffer;
		copy.dstImage = noise.image;
		copy.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		copy.regionCount = 1;
		copy.pRegions = &region;
		vkCmdCopyBufferToImage2(command, &copy);
		return true;
	}

	FrameViewGlobals MakeFrameView(const RenderCaptureSegment &capture,
		const DescriptorPage &page) const
	{
		FrameViewGlobals output = {};
		if (page.view != kInvalidId && page.view < capture.Views().size())
		{
			const CapturedWorldView &view = capture.Views()[page.view];
			CopyMatrix(output.projection, view.projection);
			CopyMatrix(output.view, view.view);
			CopyMatrix(output.view_projection, view.view_projection);
			CopyMatrix(output.inverse_modelview, view.inverse_modelview);
			CopyMatrix(output.inverse_view_projection, view.inverse_view_projection);
			CopyMatrix(output.previous_view_projection,
				view.previous_view_projection);
			CopyMatrix(output.cockpit_previous_view_projection,
				view.cockpit_previous_view_projection);
			output.visible_origin_size[0] = static_cast<float>(view.logical_clip.x);
			output.visible_origin_size[1] = static_cast<float>(view.logical_clip.y);
			output.visible_origin_size[2] = static_cast<float>(view.logical_clip.width);
			output.visible_origin_size[3] = static_cast<float>(view.logical_clip.height);
		}
		else
		{
			Identity(output.projection); Identity(output.view);
			Identity(output.view_projection); Identity(output.inverse_modelview);
			Identity(output.inverse_view_projection);
			Identity(output.previous_view_projection);
			Identity(output.cockpit_previous_view_projection);
		}
		const CapturedTargetLayout &layout = page.target == RenderTargetClass::Scene ?
			ci.targets->SceneLayout() : (page.target == RenderTargetClass::PostPresent ?
			ci.targets->PostLayout() : ci.targets->CockpitLayout());
		// T0 vertices retain the engine's logical pixel coordinates.  The Vulkan
		// viewport performs the SSAA expansion, just as GL4's ortho projection and
		// glViewport do; dividing by the internal extent here applies the scale a
		// second time and confines 4x-SSAA UI to one quarter of each axis.
		if (output.visible_origin_size[2] <= 0.0f ||
			output.visible_origin_size[3] <= 0.0f)
		{
			output.visible_origin_size[0] = output.visible_origin_size[1] = 0.0f;
			output.visible_origin_size[2] = static_cast<float>(layout.logical_width);
			output.visible_origin_size[3] = static_cast<float>(layout.logical_height);
		}
		// Font glyph inputs are local to the active clip, but font.vert maps them
		// through the full-target viewport.  Preserve the clip origin so its
		// target-absolute path matches GL4's offset_x/offset_y adjustment.
		output.viewport_xywh[0] = output.visible_origin_size[0];
		output.viewport_xywh[1] = output.visible_origin_size[1];
		output.viewport_xywh[2] = output.visible_origin_size[2];
		output.viewport_xywh[3] = output.visible_origin_size[3];
		output.target_extent_inv_extent[0] =
			static_cast<float>(layout.internal_width);
		output.target_extent_inv_extent[1] =
			static_cast<float>(layout.internal_height);
		output.target_extent_inv_extent[2] = 1.0f / layout.internal_width;
		output.target_extent_inv_extent[3] = 1.0f / layout.internal_height;
		if (!capture.TargetSignatures().empty())
		{
			const CapturedPostDynamicState &dynamic =
				capture.TargetSignatures().back().dynamic;
			if (dynamic.motion_history_valid)
				output.history_target_flags[0] |= 1u;
			if (dynamic.cockpit_history_valid)
				output.history_target_flags[0] |= 2u;
		}
		output.history_target_flags[1] = static_cast<uint32_t>(page.target);
		// Font batches use target-absolute logical coordinates and temporarily
		// draw through the full target viewport.  z/w carry that logical extent;
		// x/y remain the history flags and target class ABI.
		output.history_target_flags[2] = layout.logical_width;
		output.history_target_flags[3] = layout.logical_height;
		return output;
	}

	bool PrepareDescriptorPages(const RenderCaptureSegment &capture,
		const StagedTables &tables, std::vector<DescriptorPage> *pages)
	{
		const VkDeviceSize uniform_alignment = std::max<VkDeviceSize>(1,
			ci.platform->SelectedDevice().properties.limits.minUniformBufferOffsetAlignment);
		for (DescriptorPage &page : *pages)
		{
			page.image_views.assign(ci.pipelines->DescriptorPageTier(),
				VK_NULL_HANDLE);
			page.array_views.assign(8, VK_NULL_HANDLE);
			page.image_samplers.assign(page.image_views.size(), VK_NULL_HANDLE);
			page.array_samplers.assign(page.array_views.size(), VK_NULL_HANDLE);
			for (uint32_t i = 0; i < page.images.size(); ++i)
			{
				const bool soft_depth =
					page.images[i].image == kSoftDepthTextureSentinel;
				if (soft_depth)
				{
					const TargetImageRef soft_depth_image = ci.targets->GraphImage(
						GraphResource::SoftDepthSnapshot);
					if (!soft_depth_image.Valid())
						return false;
					page.image_views[i] = soft_depth_image.view;
				}
				else
				{
					const CapturedTextureVersion *captured = FindCapturedTexture(capture,
						page.images[i].image);
					ResidentTexture resident = {};
					if (!captured || !ci.textures->EnsureResident(*captured, capture,
						&resident))
						return false;
					page.image_views[i] = resident.view;
				}
				// Scene depth is a discrete per-pixel signal.  Match GL4's explicit
				// GL_NEAREST binding instead of inheriting the particle texture's
				// filtering/mip sampler through material slot 3.
				page.image_samplers[i] = soft_depth ?
					ci.pipelines->Sampler(SamplerSemantic::DepthNearest) :
					ci.textures->WorldSampler(page.images[i].sampler);
				if (!page.image_samplers[i]) return false;
			}
			for (uint32_t i = 0; i < page.arrays.size(); ++i)
			{
				const CapturedTextureVersion *captured = FindCapturedTexture(capture,
					page.arrays[i].image);
				ResidentTexture resident = {};
				if (!captured || !ci.textures->EnsureResident(*captured, capture,
					&resident))
					return false;
				page.array_views[i] = resident.view;
				page.array_samplers[i] = ci.textures->WorldSampler(
					page.arrays[i].sampler);
				if (!page.array_samplers[i]) return false;
			}
			for (uint32_t i = static_cast<uint32_t>(page.images.size());
				i < page.image_views.size(); ++i)
			{
				page.image_views[i] = page.image_views[0];
				page.image_samplers[i] = page.image_samplers[0];
			}
			for (uint32_t i = static_cast<uint32_t>(page.arrays.size());
				i < page.array_views.size(); ++i)
			{
				page.array_views[i] = page.array_views[0];
				page.array_samplers[i] = page.array_samplers[0];
			}

			const FrameViewGlobals globals = MakeFrameView(capture, page);
			if (!ci.frames->StageBuffer(&globals, sizeof(globals),
				FrameBufferClass::Storage, uniform_alignment, &page.frame_view) ||
				!ci.pipelines->AllocateWorldDescriptorSets(
					ci.frames->Current()->descriptor_pool, &page.sets))
				return false;
			ci.state_tracker->UseBuffer(page.frame_view.buffer,
				page.frame_view.offset, page.frame_view.size,
				{ VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
				  ci.platform->GraphicsQueueFamily(), ResourceIntent::Write });
			WorldSet0Write set0 = {};
			set0.frame_view_buffer = page.frame_view.buffer;
			set0.offset = page.frame_view.offset;
			WorldSet1Write set1 = {};
			set1.float_images_2d = page.image_views.data();
			set1.float_image_samplers = page.image_samplers.data();
			set1.float_image_count = static_cast<uint32_t>(page.image_views.size());
			set1.float_image_arrays = page.array_views.data();
			set1.float_image_array_samplers = page.array_samplers.data();
			set1.float_image_array_count = static_cast<uint32_t>(page.array_views.size());
			WorldSet2Write set2 = {};
			const FrameBufferSlice slices[8] = {
				tables.draw_headers, tables.states, tables.materials,
				tables.transforms, tables.lights, tables.specular,
				page.payload_override.Valid() ? page.payload_override :
					tables.payload_words,
				tables.world_aux
			};
			for (uint32_t binding = 0; binding < 8; ++binding)
			{
				set2.buffers[binding] = slices[binding].buffer;
				set2.offsets[binding] = slices[binding].offset;
				set2.ranges[binding] = slices[binding].size;
			}
			if (!ci.pipelines->UpdateWorldSet0(page.sets.set0, set0) ||
				!ci.pipelines->UpdateWorldSet1(page.sets.set1, set1) ||
				!ci.pipelines->UpdateWorldSet2(page.sets.set2, set2))
				return false;
		}
		return true;
	}

	WorldTargetFormats FormatsForTarget(RenderTargetClass target) const
	{
		WorldTargetFormats formats;
		if (target == RenderTargetClass::PostPresent)
		{
			for (uint32_t i = 1; i < 5; ++i)
				formats.color[i] = VK_FORMAT_UNDEFINED;
			formats.depth = VK_FORMAT_UNDEFINED;
		}
		else if (target == RenderTargetClass::CockpitScene)
		{
			for (uint32_t i = 1; i < 5; ++i)
				formats.color[i] = VK_FORMAT_UNDEFINED;
		}
		return formats;
	}

	WorldPipelineKey DrawPipelineKey(const DrawWork &draw) const
	{
		WorldPipelineKey key;
		key.family = draw.geometry_mode != GeometryMode::T0Stream ?
			WorldPipelineFamily::RetainedWorld :
			FamilyFor(draw.source, draw.raster.raster_family);
		key.blend = static_cast<BlendClass>(draw.raster.shader.blend_class);
		key.raster = draw.raster.raster_family;
		key.topology = IsExpandedFamily(key.family) ?
			VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST : TopologyFor(draw.source);
		key.samples = SampleBits(draw.raster.sample_count);
		key.depth_interpretation = draw.depth;
		key.mrt_write_mask = draw.raster.mrt_write_mask;
		key.cull_enabled = draw.raster.cull_enabled;
		key.front_face = draw.raster.front_face ? VK_FRONT_FACE_CLOCKWISE :
			VK_FRONT_FACE_COUNTER_CLOCKWISE;
		key.depth_test_enabled = draw.raster.depth_test_enabled;
		key.depth_write_enabled = draw.raster.depth_write_enabled;
		key.depth_compare = draw.raster.depth_compare ?
			VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_ALWAYS;
		key.depth_bias_enabled = draw.raster.depth_bias_enabled;
		key.formats = FormatsForTarget(draw.target);
		return key;
	}

	bool CanBatchDraws(const DrawWork &left, const DrawWork &right) const
	{
		if (left.geometry_mode == GeometryMode::T2Terrain ||
			right.geometry_mode == GeometryMode::T2Terrain ||
			left.page_index != right.page_index || left.target != right.target ||
			left.indexed != right.indexed ||
			left.raster.viewport != right.raster.viewport ||
			left.raster.scissor != right.raster.scissor ||
			left.raster.depth_bias_factor != right.raster.depth_bias_factor ||
			left.raster.depth_bias_units != right.raster.depth_bias_units ||
			left.indirect_index == kInvalidId ||
			right.indirect_index != left.indirect_index + 1u ||
			!(DrawPipelineKey(left) == DrawPipelineKey(right)))
			return false;
		if (left.geometry_mode != right.geometry_mode)
			return false;
		if (left.geometry_mode == GeometryMode::T1Retained)
			return left.retained.vertices.buffer == right.retained.vertices.buffer &&
				left.retained.indices.buffer == right.retained.indices.buffer;
		return true;
	}

	bool EncodeDrawRun(const RenderCaptureSegment &capture,
		const std::vector<DrawWork> &draws, uint32_t first_draw,
		uint32_t draw_count, const StagedTables &tables,
		const DescriptorPage &page, VkCommandBuffer command) const
	{
		if (first_draw >= draws.size() || draw_count == 0 ||
			draw_count > draws.size() - first_draw)
			return false;
		const DrawWork &draw = draws[first_draw];
		const WorldPipelineKey key = DrawPipelineKey(draw);
		const VkPipeline pipeline = ci.pipelines->FindWorldPipeline(key);
		if (pipeline == VK_NULL_HANDLE)
			return false;
		const bool expanded = IsExpandedFamily(key.family);
		const bool retained = draw.geometry_mode == GeometryMode::T1Retained;
		const bool terrain = draw.geometry_mode == GeometryMode::T2Terrain;
		const VkBuffer vertex_buffer = terrain ? draw.terrain_base_output.buffer :
			(retained ? draw.retained.vertices.buffer : tables.vertices.buffer);
		const VkDeviceSize vertex_range_offset = terrain ?
			draw.terrain_base_output.offset : (retained ?
			draw.retained.vertices.byte_offset : tables.vertices.offset);
		const VkDeviceSize vertex_range_size = terrain ?
			draw.terrain_base_output.size : (retained ?
			draw.retained.vertices.byte_size : tables.vertices.size);
		if (!expanded)
			ci.state_tracker->UseBuffer(vertex_buffer,
				vertex_range_offset, vertex_range_size,
				{ VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT,
				  VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
				  ci.platform->GraphicsQueueFamily(), ResourceIntent::Read });
		if (draw.indexed && !expanded)
			ci.state_tracker->UseBuffer(retained ? draw.retained.indices.buffer :
				tables.indices.buffer, retained ? draw.retained.indices.byte_offset :
				tables.indices.offset, retained ? draw.retained.indices.byte_size :
				tables.indices.size,
				{ VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT,
				  VK_ACCESS_2_INDEX_READ_BIT,
				  ci.platform->GraphicsQueueFamily(), ResourceIntent::Read });
		const FrameBufferSlice storage_slices[] = {
			tables.draw_headers, tables.states, tables.materials, tables.transforms,
			tables.lights, tables.specular,
			page.payload_override.Valid() ? page.payload_override : tables.payload_words,
			tables.world_aux,
			page.frame_view
		};
		for (const FrameBufferSlice &slice : storage_slices)
			ci.state_tracker->UseBuffer(slice.buffer, slice.offset, slice.size,
				{ VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
				  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
				  VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
				  VK_ACCESS_2_UNIFORM_READ_BIT,
				  ci.platform->GraphicsQueueFamily(), ResourceIntent::Read });
		if ((draw.raster.shader.shader_flags & kShaderSoftParticle) != 0)
		{
			const TargetImageRef soft_depth = ci.targets->GraphImage(
				GraphResource::SoftDepthSnapshot);
			if (!soft_depth.Valid())
				return false;
			ci.state_tracker->UseImage(soft_depth.image, FullRange(soft_depth),
				{ VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
				  VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
				  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				  ci.platform->GraphicsQueueFamily(), ResourceIntent::Read });
		}
		if (terrain)
			ci.state_tracker->UseBuffer(draw.terrain_indirect_output.buffer,
				draw.terrain_indirect_output.offset,
				draw.terrain_indirect_output.size,
				{ VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
				  VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
				  ci.platform->GraphicsQueueFamily(), ResourceIntent::Read });
		else
			ci.state_tracker->UseBuffer(tables.indirect.buffer,
				tables.indirect.offset + VkDeviceSize(draw.indirect_index) *
					sizeof(VkDrawIndexedIndirectCommand),
				VkDeviceSize(draw_count) * sizeof(VkDrawIndexedIndirectCommand),
				{ VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
				  VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
				  ci.platform->GraphicsQueueFamily(), ResourceIntent::Read });
		ci.state_tracker->Flush(command);

		vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		if (!expanded)
		{
			// Retained physical shards are always bound at byte zero.  ResolveFace
			// already returns shard-absolute firstIndex/baseVertex values.
			const VkDeviceSize vertex_offset = retained ? 0 : vertex_range_offset;
			vkCmdBindVertexBuffers(command, 0, 1, &vertex_buffer,
				&vertex_offset);
		}
		if (draw.indexed && !expanded)
			vkCmdBindIndexBuffer(command, retained ? draw.retained.indices.buffer :
				tables.indices.buffer, retained ? 0 : tables.indices.offset,
				VK_INDEX_TYPE_UINT32);
		const VkDescriptorSet sets[3] = {
			page.sets.set0, page.sets.set1, page.sets.set2
		};
		const uint32_t dynamic_offset = 0;
		vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
			ci.pipelines->WorldPipelineLayout(), 0, 3, sets, 1, &dynamic_offset);
		VkViewport viewport_info = {};
		VkRect2D scissor_info = {};
		const CapturedTargetLayout &layout = draw.target == RenderTargetClass::Scene ?
			ci.targets->SceneLayout() : (draw.target == RenderTargetClass::PostPresent ?
			ci.targets->PostLayout() : ci.targets->CockpitLayout());
		viewport_info.width = static_cast<float>(layout.internal_width);
		viewport_info.height = static_cast<float>(layout.internal_height);
		viewport_info.minDepth = 0.0f;
		viewport_info.maxDepth = 1.0f;
		scissor_info.extent = { layout.internal_width, layout.internal_height };
		// GL4 flushes font batches with a full-target viewport because glyph
		// positions are target-absolute even when text was queued in a clipped
		// frame interval.  Keep the captured scissor, but match that viewport.
		if (draw.source != PrimitiveSourceKind::Font &&
			draw.raster.viewport < capture.Viewports().size())
		{
			const CapturedViewport &viewport = capture.Viewports()[draw.raster.viewport];
			viewport_info.x = static_cast<float>(viewport.physical_rect.x);
			viewport_info.y = static_cast<float>(viewport.physical_rect.y);
			viewport_info.width = static_cast<float>(viewport.physical_rect.width);
			viewport_info.height = static_cast<float>(viewport.physical_rect.height);
		}
		if (draw.raster.scissor < capture.Viewports().size())
		{
			const CapturedViewport &scissor = capture.Viewports()[draw.raster.scissor];
			scissor_info.offset.x = scissor.physical_rect.x;
			scissor_info.offset.y = scissor.physical_rect.y;
			scissor_info.extent.width = scissor.physical_rect.width;
			scissor_info.extent.height = scissor.physical_rect.height;
		}
		vkCmdSetViewport(command, 0, 1, &viewport_info);
		vkCmdSetScissor(command, 0, 1, &scissor_info);
		vkCmdSetDepthBias(command, draw.raster.depth_bias_units, 0.0f,
			draw.raster.depth_bias_factor);
		if (terrain)
		{
			WorldBatchPush push = {};
			push.draw_header_base = draw.draw_header_index;
			push.target_flags = static_cast<uint32_t>(draw.target);
			vkCmdPushConstants(command, ci.pipelines->WorldPipelineLayout(),
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
				sizeof(push), &push);
			for (uint32_t batch = 0; batch < draw.terrain_batch_count; ++batch)
				vkCmdDrawIndirect(command, draw.terrain_indirect_output.buffer,
					draw.terrain_indirect_output.offset +
						VkDeviceSize(batch) * sizeof(TerrainIndirectCommand),
					1, sizeof(TerrainIndirectCommand));
		}
		else
		{
			const uint32_t maximum = std::max(1u,
				ci.platform->SelectedDevice().properties.limits.maxDrawIndirectCount);
			uint32_t emitted = 0;
			while (emitted < draw_count)
			{
				const uint32_t chunk = std::min(maximum, draw_count - emitted);
				WorldBatchPush push = {};
				push.draw_header_base = draws[first_draw + emitted].draw_header_index;
				push.target_flags = static_cast<uint32_t>(draw.target);
				vkCmdPushConstants(command, ci.pipelines->WorldPipelineLayout(),
					VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
					sizeof(push), &push);
				const VkDeviceSize offset = tables.indirect.offset +
					VkDeviceSize(draw.indirect_index + emitted) *
						sizeof(VkDrawIndexedIndirectCommand);
				if (expanded || !draw.indexed)
					vkCmdDrawIndirect(command, tables.indirect.buffer, offset,
						chunk, sizeof(VkDrawIndexedIndirectCommand));
				else
					vkCmdDrawIndexedIndirect(command, tables.indirect.buffer,
						offset, chunk, sizeof(VkDrawIndexedIndirectCommand));
				emitted += chunk;
			}
		}
		return true;
	}

	PostPassVariant VariantFor(const PlanOperation &operation) const
	{
		if (operation.type == PlanOperationType::CompilerGraphPhase)
			return operation.descriptor_variant;
		if (operation.graph_node == GraphNodeId::CapDepthLogical)
			return last_plan.graph.msaa_samples > 1 ?
				PostPassVariant::Multisample : PostPassVariant::SingleSample;
		if (operation.graph_node >= GraphNodeId::ResolveColor &&
			operation.graph_node <= GraphNodeId::ResolveAoClass)
			return PostPassVariant::Multisample;
		return PostPassVariant::Only;
	}

	GraphResource OperationOutput(const PlanOperation &operation) const
	{
		if (operation.type == PlanOperationType::InsertedGraphNode &&
			operation.inserted_graph_node == InsertedGraphNodeId::AcquireSoftDepth)
			return GraphResource::SoftDepthSnapshot;
		GraphResourceMask outputs = 0;
		if (operation.type == PlanOperationType::CompilerGraphPhase &&
			operation.compiler_phase_index < kCompilerGraphPhaseContractCount)
			outputs = kCompilerGraphPhaseContract[
				operation.compiler_phase_index].outputs;
		else if (operation.graph_node != GraphNodeId::Count)
			outputs = EvaluateGraphNodeOutputs(operation.graph_node,
				last_plan.graph);
		return static_cast<GraphResource>(FirstResource(outputs));
	}

	TargetImageRef DescriptorResource(GraphResource resource,
		const PlanOperation &operation, uint32_t post_present_index) const
	{
		uint32_t level = operation.graph_level;
		if (resource == GraphResource::BloomCurrentLevel ||
			resource == GraphResource::BloomSmallerLevel ||
			resource == GraphResource::BloomMerged)
			level = std::min(level, std::max(1u,
				ci.targets->BloomLevelCount()) - 1);
		TargetImageRef image = ResourceImage(resource, level,
			post_present_index);
		if (image.Valid())
			return image;
		// Optional descriptors always receive a valid correctly typed fallback.
		switch (resource)
		{
		case GraphResource::SceneDepth:
		case GraphResource::ResolvedDepth:
		case GraphResource::CapturedWorldDepth:
		case GraphResource::PostLogicalDepth:
		case GraphResource::SoftDepthSnapshot:
			return ci.targets->GraphImage(GraphResource::PostLogicalDepth);
		case GraphResource::SceneObjectId:
		case GraphResource::ResolvedObjectId:
			return ci.targets->Attachment(RenderTargetClass::Scene, 4);
		default:
			return ci.targets->GraphImage(GraphResource::LogicalAuthoredColor);
		}
	}

	const CapturedWorldView *CapturedSceneView(
		const RenderCaptureSegment &capture) const
	{
		for (auto command = capture.Commands().rbegin();
			command != capture.Commands().rend(); ++command)
		{
			if (command->type != CaptureCommandType::CaptureBloomSource)
				continue;
			const ViewStateId view =
				command->payload.capture_bloom_source.projection;
			if (view < capture.Views().size())
				return &capture.Views()[view];
		}
		return capture.Views().empty() ? nullptr : &capture.Views().back();
	}

	PostPassUniforms MakePostUniforms(const RenderCaptureSegment &capture,
		const PlanOperation &operation, const TargetImageRef &source,
		const TargetImageRef &destination) const
	{
		PostPassUniforms uniforms = {};
		const CapturedWorldView *scene_view = CapturedSceneView(capture);
		if (scene_view)
		{
			const CapturedWorldView &view = *scene_view;
			CopyMatrix(uniforms.current_projection, view.projection);
			CopyMatrix(uniforms.current_inverse_modelview,
				view.inverse_modelview);
			CopyMatrix(uniforms.previous_view_projection,
				view.previous_view_projection);
		}
		const float sw = static_cast<float>(std::max(1u, source.extent.width));
		const float sh = static_cast<float>(std::max(1u, source.extent.height));
		const float dw = static_cast<float>(std::max(1u, destination.extent.width));
		const float dh = static_cast<float>(std::max(1u, destination.extent.height));
		float logical_screen_width = dw;
		float logical_screen_height = dh;
		const float source_extent[4] = { sw, sh, 1.0f / sw, 1.0f / sh };
		const float destination_extent[4] = { dw, dh, 1.0f / dw, 1.0f / dh };
		std::memcpy(uniforms.source_extent_inv_extent, source_extent,
			sizeof(source_extent));
		std::memcpy(uniforms.destination_extent_inv_extent, destination_extent,
			sizeof(destination_extent));
		const float uv[4] = { 0, 0, 1, 1 };
		std::memcpy(uniforms.uv_origin_scale, uv, sizeof(uv));
		std::memcpy(uniforms.secondary_uv_origin_scale, uv, sizeof(uv));
		std::memcpy(uniforms.velocity_uv_origin_scale, uv, sizeof(uv));
		std::memcpy(uniforms.scene_uv_origin_scale, uv, sizeof(uv));
		std::memcpy(uniforms.ao_uv_origin_scale, uv, sizeof(uv));
		std::memcpy(uniforms.alpha_mask_uv_origin_scale, uv, sizeof(uv));
		uniforms.ao_screen_size_inv_size[0] = dw;
		uniforms.ao_screen_size_inv_size[1] = dh;
		uniforms.ao_screen_size_inv_size[2] = 1.0f / dw;
		uniforms.ao_screen_size_inv_size[3] = 1.0f / dh;
		const CapturedTargetSignature *signature = capture.TargetSignatures().empty() ?
			nullptr : &capture.TargetSignatures().back();
		if (signature)
		{
			const CapturedPreferredState &p = signature->preferred;
			const CapturedPostDynamicState &dynamic = signature->dynamic;
			logical_screen_width = static_cast<float>(std::max(1u, p.width));
			logical_screen_height = static_cast<float>(std::max(1u, p.height));
			std::memcpy(uniforms.visible_origin_size,
				dynamic.visible_origin_size, sizeof(uniforms.visible_origin_size));
			std::memcpy(uniforms.source_visible_origin_size,
				dynamic.visible_origin_size,
				sizeof(uniforms.source_visible_origin_size));
			const float velocity_width = std::max(1.0f,
				dynamic.source_destination_extent[0]);
			const float velocity_height = std::max(1.0f,
				dynamic.source_destination_extent[1]);
			uniforms.velocity_uv_origin_scale[0] =
				dynamic.visible_origin_size[0] / velocity_width;
			uniforms.velocity_uv_origin_scale[1] =
				dynamic.visible_origin_size[1] / velocity_height;
			uniforms.velocity_uv_origin_scale[2] =
				dynamic.visible_origin_size[2] / velocity_width;
			uniforms.velocity_uv_origin_scale[3] =
				dynamic.visible_origin_size[3] / velocity_height;
			if (source.extent.width == ci.targets->SceneLayout().internal_width &&
				source.extent.height == ci.targets->SceneLayout().internal_height &&
				dynamic.visible_origin_size[2] > 0.0f &&
				dynamic.visible_origin_size[3] > 0.0f)
			{
				uniforms.uv_origin_scale[0] = dynamic.visible_origin_size[0] / sw;
				uniforms.uv_origin_scale[1] = dynamic.visible_origin_size[1] / sh;
				uniforms.uv_origin_scale[2] = dynamic.visible_origin_size[2] / sw;
				uniforms.uv_origin_scale[3] = dynamic.visible_origin_size[3] / sh;
			}
			// CapturedPreferredState stores the user-facing gamma value. GL4's
			// post shaders receive the display exponent, 1 / user_gamma.
			uniforms.bloom_gamma_threshold_intensity_spread[0] =
				1.0f / std::max(p.gamma, 0.0001f);
			uniforms.bloom_gamma_threshold_intensity_spread[1] = p.bloom_threshold;
			uniforms.bloom_gamma_threshold_intensity_spread[2] = p.bloom_intensity;
			uniforms.bloom_gamma_threshold_intensity_spread[3] = p.bloom_spread;
			const float radius = std::max(0.1f, std::min(12.0f, p.gtao_radius));
			const uint32_t gtao_scale = std::max(1u, ci.targets->GtaoScale());
			const float ao_width = static_cast<float>((p.width + gtao_scale - 1u) /
				gtao_scale);
			const float ao_height = static_cast<float>((p.height + gtao_scale - 1u) /
				gtao_scale);
			uniforms.ao_screen_size_inv_size[0] = ao_width;
			uniforms.ao_screen_size_inv_size[1] = ao_height;
			uniforms.ao_screen_size_inv_size[2] = 1.0f / std::max(1.0f, ao_width);
			uniforms.ao_screen_size_inv_size[3] = 1.0f / std::max(1.0f, ao_height);
			uniforms.noise_origin_jitter[0] = dynamic.visible_origin_size[0] *
				ao_width / std::max(1.0f, dynamic.source_destination_extent[0]);
			uniforms.noise_origin_jitter[1] = dynamic.visible_origin_size[1] *
				ao_height / std::max(1.0f, dynamic.source_destination_extent[1]);
			const float m00 = std::fabs(uniforms.current_projection[0]) < 1.0e-6f ?
				1.0f : uniforms.current_projection[0];
			const float m11 = std::fabs(uniforms.current_projection[5]) < 1.0e-6f ?
				1.0f : uniforms.current_projection[5];
			uniforms.projection_info[0] = 2.0f / m00;
			uniforms.projection_info[1] = 2.0f / m11;
			uniforms.projection_info[2] =
				(uniforms.current_projection[8] - 1.0f) / m00;
			uniforms.projection_info[3] =
				(uniforms.current_projection[9] - 1.0f) / m11;
			const float c = uniforms.current_projection[10];
			const float d = uniforms.current_projection[14];
			float near_z = std::fabs(c - 1.0f) > 1.0e-6f ? d / (c - 1.0f) : 0.1f;
			float far_z = std::fabs(c + 1.0f) > 1.0e-6f ? d / (c + 1.0f) : 5000.0f;
			near_z = std::max(0.0001f, std::fabs(near_z));
			far_z = std::max(near_z + 0.001f, std::fabs(far_z));
			uniforms.near_far_radius_radius_pixels[0] = near_z;
			uniforms.near_far_radius_radius_pixels[1] = far_z;
			uniforms.near_far_radius_radius_pixels[2] = radius;
			uniforms.near_far_radius_radius_pixels[3] =
				radius * 0.5f * ao_height * m11;
			uniforms.ao_max_radius_neg_inv_radius2_bias_intensity[0] = std::max(
				16.0f, 128.0f * std::sqrt(std::max(1.0f, ao_width * ao_height) /
					(1080.0f * 1920.0f)));
			uniforms.ao_max_radius_neg_inv_radius2_bias_intensity[1] =
				-1.0f / (radius * radius);
			uniforms.ao_max_radius_neg_inv_radius2_bias_intensity[2] =
				std::max(0.0f, std::min(1.0f, p.gtao_bias));
			uniforms.ao_max_radius_neg_inv_radius2_bias_intensity[3] =
				std::max(0.0f, std::min(4.0f, p.gtao_intensity));
			uniforms.ao_class_weights[0] = p.gtao_terrain_occlusion;
			uniforms.ao_class_weights[1] = p.gtao_polyobject_occlusion;
			uniforms.ao_class_weights[2] = p.gtao_mine_rock_occlusion;
			uniforms.ao_class_weights[3] = p.gtao_mine_occlusion;
			uniforms.temporal_blend_depth_velocity_frame_time[0] = p.gtao_temporal_blend;
			uniforms.temporal_blend_depth_velocity_frame_time[1] = p.gtao_temporal_depth_reject;
			uniforms.temporal_blend_depth_velocity_frame_time[2] = p.gtao_temporal_velocity_reject;
			uniforms.temporal_blend_depth_velocity_frame_time[3] = dynamic.frame_time;
			constexpr float kMotionReferenceFrameTime = 1.0f / 60.0f;
			const float motion_frame_time = dynamic.frame_time < 0.001f ?
				kMotionReferenceFrameTime : dynamic.frame_time;
			const float motion_frame_scale = std::max(0.25f, std::min(4.0f,
				kMotionReferenceFrameTime / motion_frame_time));
			const float afterburner_scale = 1.0f +
				std::max(0.0f, std::min(1.0f, dynamic.afterburner_scalar)) *
				std::max(0.0f, std::min(4.0f,
					p.afterburner_pixel_blur_multiplier));
			uniforms.motion_strength_legacy_object_centers[0] =
				p.pixel_motion_blur_strength * motion_frame_scale * afterburner_scale;
			uniforms.motion_strength_legacy_object_centers[1] =
				p.pixel_motion_blur_legacy_object_strength * motion_frame_scale *
				afterburner_scale;
			uniforms.motion_strength_legacy_object_centers[2] = p.pixel_motion_blur_center_suppression;
			uniforms.motion_strength_legacy_object_centers[3] = p.pixel_motion_blur_legacy_object_center_suppression;
			uniforms.motion_legacy_frame_sphere_density_exponent[0] = dynamic.frame_time;
			uniforms.motion_legacy_frame_sphere_density_exponent[1] =
				p.combined_motion_blur_legacy_sphere_size;
			uniforms.motion_legacy_frame_sphere_density_exponent[2] =
				p.combined_motion_blur_legacy_copy_density;
			uniforms.motion_legacy_frame_sphere_density_exponent[3] =
				p.combined_motion_blur_legacy_alpha_exponent;
			uniforms.motion_periphery_combined_strength_sphere_density[0] =
				p.pixel_motion_blur_periphery_strength;
			uniforms.motion_periphery_combined_strength_sphere_density[1] =
				p.combined_motion_blur_legacy_strength;
			uniforms.motion_periphery_combined_strength_sphere_density[2] =
				p.combined_motion_blur_legacy_sphere_size;
			uniforms.motion_periphery_combined_strength_sphere_density[3] =
				p.combined_motion_blur_legacy_copy_density;
			uniforms.motion_afterburner_exponent_fov_pixel_scalar[0] =
				p.combined_motion_blur_legacy_alpha_exponent;
			uniforms.motion_afterburner_exponent_fov_pixel_scalar[1] =
				p.afterburner_fov_multiplier;
			uniforms.motion_afterburner_exponent_fov_pixel_scalar[2] =
				p.afterburner_pixel_blur_multiplier;
			uniforms.motion_afterburner_exponent_fov_pixel_scalar[3] =
				dynamic.afterburner_scalar;
			uniforms.sample_counts[1] = std::max(1u, p.pixel_motion_blur_samples);
			GtaoSamplePattern(p.gtao_sample_count,
				&uniforms.sample_counts[2], &uniforms.sample_counts[3]);
			uniforms.integer_params[0] = static_cast<int32_t>(
				std::min(20u, p.gtao_blur_radius));
			uniforms.integer_params[1] = static_cast<int32_t>(p.motion_vector_debug_preview);
			uniforms.frame_branch[0] = dynamic.frame_serial;
			uniforms.frame_branch[2] = dynamic.gtao_history_valid;
			uint32_t features = 0;
			if (scene_view && dynamic.captured_depth_valid &&
				dynamic.motion_history_valid)
				features |= kPostUniformHasStaticReconstruction;
			if (last_plan.graph.motion_consumer_active)
				features |= kPostUniformHasDynamicVelocity;
			if (dynamic.paused || dynamic.histories_frozen)
				features |= kPostUniformPausedOrFrozen;
			switch (operation.graph_node)
			{
			case GraphNodeId::AoDepth:
				features |= kPostUniformHasAoClass;
				break;
			case GraphNodeId::AoTemporal:
				if (dynamic.gtao_history_valid)
					features |= kPostUniformHistoryValid;
				if (p.gtao_temporal_debug_preview)
					features |= kPostUniformDebugTemporal;
				break;
			case GraphNodeId::AoSuppress:
				features |= kPostUniformHasMask |
					kPostUniformUseProtectionMask |
					kPostUniformSourceVisibleRect;
				if (p.bloom_enabled)
					features |= kPostUniformUseBloomMask;
				break;
			case GraphNodeId::AoApply:
				features |= kPostUniformHasSuppressionMask;
				uniforms.integer_params[1] =
					static_cast<int32_t>(p.gtao_debug_preview);
				break;
			case GraphNodeId::AoDeferredComposite:
				features |= kPostUniformUseVisibleRect |
					kPostUniformUseProtectionMask;
				break;
			case GraphNodeId::BloomThreshold:
				features |= kPostUniformUseAlphaOcclusionMask |
					kPostUniformUseProtectionMask;
				break;
			case GraphNodeId::NormalComposite:
				features |= kPostUniformUseAlphaMask |
					kPostUniformUseProtectionMask;
				break;
			case GraphNodeId::CockpitBloomGamma:
				features |= kPostUniformUseProtectionMask;
				break;
			default:
				break;
			}
			uniforms.feature_flags[0] = features;
			const float sharpness = std::max(180.0f, std::min(1400.0f,
				far_z / (radius * 4.0f)));
			if (operation.graph_node == GraphNodeId::AoBlurX)
				uniforms.blur_delta_sharpness_reserved[0] =
					uniforms.ao_screen_size_inv_size[2];
			else if (operation.graph_node == GraphNodeId::AoBlurY)
				uniforms.blur_delta_sharpness_reserved[1] =
					uniforms.ao_screen_size_inv_size[3];
			uniforms.blur_delta_sharpness_reserved[2] = sharpness;
			if (dynamic.gtao_history_valid)
			{
				uniforms.noise_origin_jitter[2] = std::fmod(
					(static_cast<float>(dynamic.frame_serial) + 1.0f) * 0.754877666f,
					1.0f);
				uniforms.noise_origin_jitter[3] = std::fmod(
					(static_cast<float>(dynamic.frame_serial) + 1.0f) * 0.569840291f,
					1.0f);
			}
		}
		else
		{
			uniforms.visible_origin_size[2] = static_cast<float>(
				ci.targets->SceneLayout().logical_width);
			uniforms.visible_origin_size[3] = static_cast<float>(
				ci.targets->SceneLayout().logical_height);
			std::memcpy(uniforms.source_visible_origin_size,
				uniforms.visible_origin_size, sizeof(uniforms.visible_origin_size));
		}
		uniforms.screen_size_inv_size[0] = logical_screen_width;
		uniforms.screen_size_inv_size[1] = logical_screen_height;
		uniforms.screen_size_inv_size[2] = 1.0f /
			std::max(1.0f, uniforms.screen_size_inv_size[0]);
		uniforms.screen_size_inv_size[3] = 1.0f /
			std::max(1.0f, uniforms.screen_size_inv_size[1]);
		uniforms.sample_counts[0] = last_plan.graph.msaa_samples;
		uniforms.frame_branch[1] = static_cast<uint32_t>(operation.source_selector);
		uniforms.frame_branch[3] = operation.graph_invocation;
		return uniforms;
	}

	bool EncodeGraphOperation(const RenderCaptureSegment &capture,
		const PlanOperation &operation, VkCommandBuffer command,
		uint32_t *post_present_index, uint32_t *rendering_instances)
	{
		if (operation.graph_node == GraphNodeId::Present ||
			operation.graph_node == GraphNodeId::NormalUi ||
			operation.graph_node == GraphNodeId::CockpitUiPre ||
			operation.graph_node == GraphNodeId::CockpitScene ||
			operation.graph_node == GraphNodeId::CockpitUiPost)
			return true;
		if (operation.type == PlanOperationType::CompilerGraphPhase &&
			operation.compiler_phase_index < kCompilerGraphPhaseContractCount &&
			kCompilerGraphPhaseContract[operation.compiler_phase_index].kind ==
				CompilerGraphPhaseKind::ResourceChannelAlias)
			return true;

		GraphResource output_resource = OperationOutput(operation);
		GraphNodeId pipeline_node = operation.graph_node;
		PostPassVariant variant = VariantFor(operation);
		if (operation.type == PlanOperationType::CompilerGraphPhase &&
			operation.compiler_phase_index < kCompilerGraphPhaseContractCount &&
			kCompilerGraphPhaseContract[operation.compiler_phase_index].kind ==
				CompilerGraphPhaseKind::AttachmentAlphaOnlyClear)
		{
			pipeline_node = GraphNodeId::PostAlphaClear;
			variant = PostPassVariant::Only;
			output_resource = GraphResource::SceneColor;
		}
		if (output_resource == GraphResource::Count)
			return true;

		bool reads_post_present = false;
		for (size_t i = 0; i < kPostPassDescriptorBindingCount; ++i)
		{
			const PostPassDescriptorBindingContract &binding =
				kPostPassDescriptorBindings[i];
			if (binding.node != pipeline_node || binding.variant != variant)
				continue;
			GraphResource resource = binding.semantic ==
				PostDescriptorResourceSemantic::GraphResource ?
				binding.resource : SelectGraphInputSource(binding.selected_input,
					last_plan.sources);
			reads_post_present |= resource == GraphResource::PostPresent;
		}
		uint32_t destination_post_index = *post_present_index;
		if (output_resource == GraphResource::PostPresent && reads_post_present)
			destination_post_index = 1u - *post_present_index;
		TargetImageRef output = output_resource == GraphResource::PostPresent ?
			ci.targets->GraphImage(output_resource, destination_post_index) :
			DescriptorResource(output_resource, operation, *post_present_index);
		if (!output.Valid())
			return false;

		std::vector<PostImageWrite> image_writes;
		TargetImageRef first_source = output;
		for (size_t i = 0; i < kPostPassDescriptorBindingCount; ++i)
		{
			const PostPassDescriptorBindingContract &binding =
				kPostPassDescriptorBindings[i];
			if (binding.node != pipeline_node || binding.variant != variant ||
				binding.kind == PostDescriptorKind::UniformBuffer ||
				binding.kind == PostDescriptorKind::Sampler)
				continue;
			GraphResource resource = binding.semantic ==
				PostDescriptorResourceSemantic::GraphResource ?
				binding.resource : SelectGraphInputSource(binding.selected_input,
					last_plan.sources);
			TargetImageRef input = DescriptorResource(resource, operation,
				*post_present_index);
			if (!input.Valid())
				return false;
			if (image_writes.empty())
				first_source = input;
			PostImageWrite write = {};
			write.binding = binding.binding;
			write.kind = binding.kind;
			write.view = input.view;
			write.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			image_writes.push_back(write);
			ci.state_tracker->UseImage(input.image, FullRange(input),
				{ VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
				  VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
				  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				  ci.platform->GraphicsQueueFamily(), ResourceIntent::Read });
		}

		const bool depth_output = (output.aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0;
		const bool initialize_soft_depth =
			operation.type == PlanOperationType::InsertedGraphNode &&
			operation.inserted_graph_node == InsertedGraphNodeId::AcquireSoftDepth;
		if (initialize_soft_depth && variant == PostPassVariant::Multisample)
		{
			// Match GL4's multisample framebuffer depth blit with Vulkan's native
			// depth resolve.  Sampling texture2DMS in a fullscreen shader produced
			// discontinuous soft-particle fades even though every individual sample
			// reduction rule yielded the same artifact.
			if (!first_source.Valid() || first_source.samples == VK_SAMPLE_COUNT_1_BIT ||
				output.samples != VK_SAMPLE_COUNT_1_BIT ||
				first_source.extent.width != output.extent.width ||
				first_source.extent.height != output.extent.height)
				return false;
			ci.state_tracker->UseImage(first_source.image, FullRange(first_source),
				{ VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
				  VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
				  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
				  VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
				  ci.platform->GraphicsQueueFamily(), ResourceIntent::Read });
			ci.state_tracker->UseImage(output.image, FullRange(output),
				{ VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
				  VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
				  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				  VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
				  ci.platform->GraphicsQueueFamily(), ResourceIntent::Write });
			ci.state_tracker->Flush(command);

			VkRenderingAttachmentInfo depth = {
				VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO
			};
			depth.imageView = first_source.view;
			depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
			depth.resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
			depth.resolveImageView = output.view;
			depth.resolveImageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
			depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
			depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			VkRenderingInfo rendering = { VK_STRUCTURE_TYPE_RENDERING_INFO };
			rendering.renderArea.extent = output.extent;
			rendering.layerCount = 1;
			rendering.pDepthAttachment = &depth;
			vkCmdBeginRendering(command, &rendering);
			++*rendering_instances;
			vkCmdEndRendering(command);
			return true;
		}
		ci.state_tracker->UseImage(output.image, FullRange(output),
			{ depth_output ? VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
				VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT :
				VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			  depth_output ? (initialize_soft_depth ?
				VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT :
				VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
				VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT) :
				VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
					VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			  depth_output ? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL :
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			  ci.platform->GraphicsQueueFamily(), initialize_soft_depth ?
				ResourceIntent::Write : ResourceIntent::ReadWrite });
		ci.state_tracker->Flush(command);

		const bool attachment_only = pipeline_node == GraphNodeId::PostAlphaClear;
		VkDescriptorSet set = VK_NULL_HANDLE;
		if (!attachment_only)
		{
			const PostPassUniforms uniforms = MakePostUniforms(capture, operation,
				first_source, output);
			const VkDeviceSize uniform_alignment = std::max<VkDeviceSize>(1,
				ci.platform->SelectedDevice().properties.limits.
					minUniformBufferOffsetAlignment);
			FrameBufferSlice uniform_slice;
			if (!ci.frames->StageBuffer(&uniforms, sizeof(uniforms),
				FrameBufferClass::Storage, uniform_alignment, &uniform_slice))
				return false;
			ci.state_tracker->UseBuffer(uniform_slice.buffer, uniform_slice.offset,
				uniform_slice.size, { VK_PIPELINE_STAGE_2_COPY_BIT,
				VK_ACCESS_2_TRANSFER_WRITE_BIT, ci.platform->GraphicsQueueFamily(),
				ResourceIntent::Write });
			ci.state_tracker->UseBuffer(uniform_slice.buffer, uniform_slice.offset,
				uniform_slice.size, { VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
				VK_ACCESS_2_UNIFORM_READ_BIT, ci.platform->GraphicsQueueFamily(),
				ResourceIntent::Read });
			ci.state_tracker->Flush(command);
			if (!ci.pipelines->AllocatePostDescriptorSet(
					ci.frames->Current()->descriptor_pool, pipeline_node, variant, &set))
				return false;
			PostDescriptorWrite descriptor = {};
			descriptor.uniform_buffer = uniform_slice.buffer;
			descriptor.uniform_offset = uniform_slice.offset;
			descriptor.images = image_writes.data();
			descriptor.image_count = static_cast<uint32_t>(image_writes.size());
			if (!ci.pipelines->UpdatePostDescriptorSet(set, pipeline_node, variant,
				descriptor))
				return false;
		}

		VkRenderingAttachmentInfo attachment = {
			VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO
		};
		attachment.imageView = output.view;
		attachment.imageLayout = depth_output ?
			VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL :
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		attachment.loadOp = initialize_soft_depth ? VK_ATTACHMENT_LOAD_OP_CLEAR :
			VK_ATTACHMENT_LOAD_OP_LOAD;
		attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		if (initialize_soft_depth)
			attachment.clearValue.depthStencil.depth = 1.0f;
		VkRenderingInfo rendering = { VK_STRUCTURE_TYPE_RENDERING_INFO };
		rendering.renderArea.extent = output.extent;
		rendering.layerCount = 1;
		if (depth_output)
			rendering.pDepthAttachment = &attachment;
		else
		{
			rendering.colorAttachmentCount = 1;
			rendering.pColorAttachments = &attachment;
		}
		vkCmdBeginRendering(command, &rendering);
		++*rendering_instances;
		const VkPipeline pipeline = ci.pipelines->FindPostPipeline(pipeline_node,
			variant, VK_FORMAT_UNDEFINED, output.samples);
		if (pipeline == VK_NULL_HANDLE)
			return false;
		vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		if (!attachment_only)
			vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
				ci.pipelines->PostPipelineLayout(pipeline_node, variant), 0, 1,
				&set, 0, nullptr);
		VkViewport viewport = { 0, 0, static_cast<float>(output.extent.width),
			static_cast<float>(output.extent.height), 0.0f, 1.0f };
		VkRect2D scissor = { {0,0}, output.extent };
		vkCmdSetViewport(command, 0, 1, &viewport);
		vkCmdSetScissor(command, 0, 1, &scissor);
		vkCmdDraw(command, 3, 1, 0, 0);
		vkCmdEndRendering(command);
		if (output_resource == GraphResource::PostPresent)
			*post_present_index = destination_post_index;
		return true;
	}
};

FrameCompiler::FrameCompiler() : impl_(new Impl) {}

FrameCompiler::~FrameCompiler()
{
	Shutdown(impl_ && impl_->ci.platform && impl_->ci.platform->DeviceLost());
	delete impl_;
	impl_ = nullptr;
}

bool FrameCompiler::Initialize(const FrameCompilerCreateInfo &create_info)
{
	if (!impl_ || impl_->ready || !create_info.platform ||
		!create_info.allocator || !create_info.frames ||
		!create_info.state_tracker || !create_info.targets ||
		!create_info.textures || !create_info.pipelines ||
		!create_info.retained_world || !create_info.wsi ||
		!create_info.platform->Ready() || !create_info.frames->Ready() ||
		!create_info.targets->Ready() || !create_info.textures->Ready() ||
		!create_info.pipelines->Ready() || !create_info.retained_world->Ready())
		return false;
	impl_->ci = create_info;
	impl_->snapshots.reserve(8);
	impl_->ready = true;
	return true;
}

void FrameCompiler::Shutdown(bool) noexcept
{
	if (!impl_)
		return;
	impl_->ready = false;
	impl_->snapshots.clear();
	impl_->last_plan = FramePlan();
	impl_->ci = FrameCompilerCreateInfo();
}

bool FrameCompiler::Ready() const noexcept
{
	return impl_ && impl_->ready && impl_->ci.platform->Ready();
}

const std::string &FrameCompiler::LastError() const noexcept
{
	static const std::string empty;
	return impl_ ? impl_->last_error : empty;
}

const FramePlan &FrameCompiler::LastPlan() const noexcept
{
	static const FramePlan empty;
	return impl_ ? impl_->last_plan : empty;
}

const ResourceStateSnapshot *FrameCompiler::FindSnapshot(uint64_t serial) const
{
	if (!impl_ || serial == 0)
		return nullptr;
	for (const ResourceStateSnapshot &snapshot : impl_->snapshots)
		if (snapshot.serial == serial)
			return &snapshot;
	return nullptr;
}

bool FrameCompiler::CompileAndSubmit(RenderCaptureSegment *capture,
	bool present, CompilerSubmission *submission)
{
	using CpuClock = std::chrono::steady_clock;
	const CpuClock::time_point cpu_begin = CpuClock::now();
	CpuClock::time_point cpu_plan = cpu_begin;
	CpuClock::time_point cpu_draw_lowering = cpu_begin;
	CpuClock::time_point cpu_pipelines = cpu_begin;
	CpuClock::time_point cpu_tables = cpu_begin;
	CpuClock::time_point cpu_pages = cpu_begin;
	CpuClock::time_point cpu_frame_begin = cpu_begin;
	CpuClock::time_point cpu_prepare = cpu_begin;
	CpuClock::time_point cpu_record = cpu_begin;
	CpuClock::time_point cpu_submit = cpu_begin;

	if (!Ready() || !capture || !submission || !capture->IsFrozen())
		return false;
	*submission = CompilerSubmission();
	impl_->last_error.clear();
	if (!impl_->planner.Build(*capture, &impl_->last_plan))
	{
		std::ostringstream reason;
		reason << "capture or graph lowering rejected: errors=0x" << std::hex
			<< impl_->last_plan.errors << std::dec << " command="
			<< impl_->last_plan.error_command_index;
		if ((impl_->last_plan.errors & kFramePlanInvalidGraphContext) != 0)
		{
			const GraphEvaluationContext &graph = impl_->last_plan.graph;
			reason << " graphErrors=0x" << std::hex
				<< ValidateGraphEvaluationContext(graph) << std::dec
				<< " captures=" << graph.capture_call_count << '/'
				<< graph.world_color_capture_call_count << '/'
				<< graph.depth_capture_call_count << " post="
				<< graph.post_frame_active << " present=" << graph.present_count
				<< " late=" << graph.late_post_active << " gtao="
				<< graph.gtao_enabled << " bloom=" << graph.bloom_enabled
				<< " cockpit=" << graph.cockpit_deferral_active << '/'
				<< graph.cockpit_frame_count << " ui="
				<< graph.normal_ui_span_count << '/'
				<< graph.cockpit_ui_pre_span_count << '/'
				<< graph.cockpit_ui_post_span_count;
		}
		return impl_->Fail("frame-plan", reason.str().c_str());
	}
	cpu_plan = CpuClock::now();

	std::vector<BaseVertex> &vertices = impl_->scratch_vertices;
	vertices.assign(capture->StreamVertices().begin(),
		capture->StreamVertices().end());
	std::vector<uint32_t> &indices = impl_->scratch_indices;
	indices.assign(capture->StreamIndices().begin(),
		capture->StreamIndices().end());
	std::vector<DrawWork> &draws = impl_->scratch_draws;
	draws.clear();
	draws.reserve(impl_->last_plan.direct_draw_count + 16);
	if (!impl_->BuildDrawWork(*capture, &vertices, &draws))
		return impl_->Fail("draw-lowering", "invalid or unsupported draw record");
	cpu_draw_lowering = CpuClock::now();
	std::vector<WorldPipelineKey> &required_world_pipelines =
		impl_->scratch_world_pipelines;
	required_world_pipelines.clear();
	required_world_pipelines.reserve(draws.size());
	for (const DrawWork &draw : draws)
		required_world_pipelines.push_back(impl_->DrawPipelineKey(draw));
	if (!impl_->ci.pipelines->EnsureWorldPipelines(required_world_pipelines))
		return impl_->Fail("world-pipelines",
			"required draw-state pipeline could not be created");
	cpu_pipelines = CpuClock::now();
	std::vector<GpuDrawHeader> &headers = impl_->scratch_headers;
	std::vector<GpuShaderState> &states = impl_->scratch_states;
	std::vector<GpuMaterial> &materials = impl_->scratch_materials;
	std::vector<GpuTransform> &transforms = impl_->scratch_transforms;
	std::vector<GpuDynamicLight> &lights = impl_->scratch_lights;
	std::vector<GpuSpecularBlock> &specular = impl_->scratch_specular;
	std::vector<uint32_t> &payload_words = impl_->scratch_payload_words;
	std::vector<GpuWorldAux> &world_aux = impl_->scratch_world_aux;
	std::vector<RetainedPayloadCopy> &retained_copies =
		impl_->scratch_retained_copies;
	headers.clear(); states.clear(); materials.clear(); transforms.clear();
	lights.clear(); specular.clear(); payload_words.clear(); world_aux.clear();
	retained_copies.clear();
	headers.reserve(draws.size()); states.reserve(draws.size());
	materials.reserve(draws.size()); transforms.reserve(draws.size());
	if (!impl_->BuildGpuTables(*capture, vertices, &draws, &headers, &states, &materials,
		&transforms, &lights, &specular, &payload_words, &world_aux,
		&retained_copies))
		return impl_->Fail("gpu-table-lowering", "typed payload lowering failed");
	cpu_tables = CpuClock::now();
	// Descriptor bindings remain valid for frames containing only clears, post
	// work, or presentation.  No draw references these null records.
	if (headers.empty()) headers.push_back(GpuDrawHeader{});
	if (states.empty()) states.push_back(GpuShaderState{});
	if (materials.empty()) materials.push_back(GpuMaterial{});
	if (transforms.empty()) transforms.push_back(GpuTransform{});
	if (payload_words.empty()) payload_words.push_back(0);
	std::vector<DescriptorPage> &pages = impl_->scratch_pages;
	pages.clear();
	pages.reserve(draws.size() / 32 + 4);
	if (!impl_->BuildDescriptorPages(*capture, &draws, &materials, &pages))
		return impl_->Fail("descriptor-pages", "a draw does not fit a typed page");
	std::vector<VkDrawIndexedIndirectCommand> &indirect_commands =
		impl_->scratch_indirect_commands;
	indirect_commands.clear();
	indirect_commands.reserve(draws.size());
	for (DrawWork &draw : draws)
	{
		if (draw.geometry_mode == GeometryMode::T2Terrain)
			continue;
		VkDrawIndexedIndirectCommand indirect = {};
		const bool expanded = IsExpandedFamily(impl_->DrawPipelineKey(draw).family);
		if (expanded)
		{
			// VkDrawIndexedIndirectCommand is deliberately used as a 20-byte
			// storage stride for both command forms.  Its first four words are
			// layout-compatible with VkDrawIndirectCommand when vertexOffset is
			// zero, and Vulkan permits a larger aligned indirect stride.
			indirect.indexCount = kExpandedPrimitiveVertexCount;
			indirect.instanceCount = draw.expanded_instance_count;
			indirect.firstIndex = 0;
			indirect.vertexOffset = 0;
		}
		else if (draw.indexed)
		{
			indirect.indexCount = draw.index_count;
			indirect.instanceCount = 1;
			indirect.firstIndex = draw.first_index;
			indirect.vertexOffset = draw.vertex_offset;
		}
		else
		{
			indirect.indexCount = draw.vertex_count;
			indirect.instanceCount = 1;
			indirect.firstIndex = draw.first_vertex;
			indirect.vertexOffset = 0;
		}
		draw.indirect_index = static_cast<uint32_t>(indirect_commands.size());
		indirect_commands.push_back(indirect);
	}
	cpu_pages = CpuClock::now();

	FrameRequirements requirements = impl_->last_plan.requirements;
	requirements.vertex_bytes = std::max<VkDeviceSize>(sizeof(BaseVertex),
		VkDeviceSize(vertices.size()) * sizeof(BaseVertex));
	requirements.index_bytes = std::max<VkDeviceSize>(sizeof(uint32_t),
		VkDeviceSize(indices.size()) * sizeof(uint32_t));
	requirements.storage_bytes = std::max<VkDeviceSize>(4096,
		VkDeviceSize(headers.size()) * sizeof(GpuDrawHeader) +
		VkDeviceSize(states.size()) * sizeof(GpuShaderState) +
		VkDeviceSize(materials.size()) * sizeof(GpuMaterial) +
		VkDeviceSize(transforms.size()) * sizeof(GpuTransform) +
		VkDeviceSize(lights.size()) * sizeof(GpuDynamicLight) +
		VkDeviceSize(specular.size()) * sizeof(GpuSpecularBlock) +
		VkDeviceSize(payload_words.size()) * sizeof(uint32_t) +
		VkDeviceSize(world_aux.size()) * sizeof(GpuWorldAux) +
		VkDeviceSize(pages.size()) * sizeof(FrameViewGlobals) +
		VkDeviceSize(impl_->last_plan.graph_pass_count) * sizeof(PostPassUniforms));
	requirements.indirect_bytes = std::max<VkDeviceSize>(
		sizeof(VkDrawIndexedIndirectCommand),
		VkDeviceSize(indirect_commands.size()) *
			sizeof(VkDrawIndexedIndirectCommand));
	uint32_t terrain_draw_count = 0;
	for (const DrawWork &draw : draws)
		if (draw.geometry_mode == GeometryMode::T2Terrain)
		{
			++terrain_draw_count;
			requirements.vertex_bytes +=
				VkDeviceSize(draw.terrain_output_capacity) * sizeof(BaseVertex);
			requirements.storage_bytes +=
				VkDeviceSize(draw.terrain_work_count) * sizeof(uint32_t) * 2u +
				VkDeviceSize(draw.terrain_output_capacity) *
					sizeof(TerrainVertexPayload) + sizeof(TerrainViewInput);
		}
	requirements.upload_bytes = requirements.vertex_bytes +
		requirements.index_bytes + requirements.storage_bytes +
		requirements.indirect_bytes;
	if (!capture->TargetSignatures().empty() &&
		capture->TargetSignatures().back().preferred.gtao_enabled)
		requirements.upload_bytes += 32;
	for (const CapturedTextureVersion &version : capture->TextureVersions())
		if (version.immutable_upload_payload != kInvalidId &&
			version.immutable_upload_payload < capture->PayloadRecords().size())
		{
			const VkDeviceSize bytes =
				capture->PayloadRecords()[version.immutable_upload_payload].byte_size;
			if (bytes < kDedicatedTextureUploadThreshold)
				requirements.upload_bytes += AlignUp(bytes, 256);
		}
	// Color readbacks use an RGBA8 transfer representation even when the legacy
	// destination is RGB565/RGB8.
	VkDeviceSize rgba_readback_bytes = 0;
	for (const CaptureCommand &command : capture->Commands())
	{
		VkDeviceSize bytes = 0;
		if (command.type == CaptureCommandType::ReadPixel)
			bytes = 4;
		else if (command.type == CaptureCommandType::ReadImage)
			bytes = VkDeviceSize(command.payload.read_image.rect.width) *
				command.payload.read_image.rect.height * 4u;
		if (bytes > std::numeric_limits<VkDeviceSize>::max() - rgba_readback_bytes)
			return impl_->Fail("frame-reserve", "readback byte count overflow");
		rgba_readback_bytes += bytes;
	}
	requirements.readback_bytes = std::max(requirements.readback_bytes,
		rgba_readback_bytes);
	requirements.descriptor_sets = std::max(requirements.descriptor_sets,
		static_cast<uint32_t>(pages.size() * 3 +
		impl_->last_plan.graph_pass_count + terrain_draw_count + 16));
	// Set 1 of every world page owns a full device-tier 2D image array plus
	// the fixed array-image table. Post passes use small sampled-image tables;
	// retain the old conservative allowance of sixteen descriptors per pass.
	const uint64_t world_sampled_images = uint64_t(pages.size()) *
		(impl_->ci.pipelines->DescriptorPageTier() + kWorldArrayImageCount);
	const uint64_t post_sampled_images =
		(uint64_t(impl_->last_plan.graph_pass_count) + 1u) * 16u;
	if (world_sampled_images + post_sampled_images > UINT32_MAX)
		return impl_->Fail("frame-reserve",
			"sampled-image descriptor count overflow");
	requirements.descriptor_sampled_images = std::max(
		requirements.descriptor_sampled_images,
		static_cast<uint32_t>(world_sampled_images + post_sampled_images));
	impl_->ci.retained_world->Reclaim(impl_->ci.frames->PollCompletedTimeline());
	if (!impl_->ci.retained_world->PreflightReserve(requirements))
		return impl_->Fail("frame-reserve", "transient arenas could not grow");
	FrameContext *frame = impl_->ci.frames->BeginFrame();
	if (!frame)
		return impl_->Fail("frame-begin", "frame context timeline wait/reset failed");
	cpu_frame_begin = CpuClock::now();
	VkCommandBuffer command = frame->recording_command;
	const SelectedDeviceInfo &selected_device = impl_->ci.platform->SelectedDevice();
	const bool gpu_timestamps_enabled = frame->timestamp_capacity >= 2 &&
		selected_device.timestamp_valid_bits != 0 &&
		selected_device.properties.limits.timestampPeriod > 0.0f;
	const double completed_gpu_frame_ms = frame->completed_gpu_frame_ms;
	const bool completed_gpu_frame_valid = frame->completed_gpu_frame_valid;
	if (gpu_timestamps_enabled)
		vkCmdWriteTimestamp2(command, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
			frame->timestamp_pool, 0);
	StateTrackerTransaction state_transaction(impl_->ci.state_tracker);
	impl_->ci.state_tracker->BeginRecording();
	if (!impl_->ci.retained_world->RecordPendingUploads(command))
	{
		impl_->ci.frames->AbandonCurrentRecording();
		impl_->ci.retained_world->AbandonPendingUploadRecording();
		impl_->ci.textures->DiscardCapture(*capture);
		return impl_->Fail("retained-upload", "retained upload recording failed");
	}

	StagedTables tables;
	if (vertices.empty()) vertices.push_back(BaseVertex{});
	if (indices.empty()) indices.push_back(0);
	if (indirect_commands.empty())
		indirect_commands.push_back(VkDrawIndexedIndirectCommand{});
	if (!impl_->StageVector(vertices, FrameBufferClass::Vertex, 16,
		&tables.vertices) ||
		!impl_->StageVector(indices, FrameBufferClass::Index, 4,
			&tables.indices) ||
		!impl_->StageVector(headers, FrameBufferClass::Storage, 16,
			&tables.draw_headers) ||
		!impl_->StageVector(states, FrameBufferClass::Storage, 16,
			&tables.states) ||
		!impl_->StageVector(materials, FrameBufferClass::Storage, 16,
			&tables.materials) ||
		!impl_->StageVector(transforms, FrameBufferClass::Storage, 16,
			&tables.transforms) ||
		!impl_->StageVector(lights, FrameBufferClass::Storage, 16,
			&tables.lights) ||
		!impl_->StageVector(specular, FrameBufferClass::Storage, 16,
			&tables.specular) ||
		!impl_->StageVector(payload_words, FrameBufferClass::Storage, 16,
			&tables.payload_words) ||
		!impl_->StageVector(world_aux, FrameBufferClass::Storage, 16,
			&tables.world_aux) ||
		!impl_->StageVector(indirect_commands, FrameBufferClass::Indirect, 4,
			&tables.indirect))
	{
		impl_->ci.frames->AbandonCurrentRecording();
		impl_->ci.retained_world->AbandonPendingUploadRecording();
		impl_->ci.textures->DiscardCapture(*capture);
		return impl_->Fail("frame-upload", "stream/table staging overflowed");
	}
	const FrameBufferSlice transfer_slices[] = {
		tables.vertices, tables.indices, tables.draw_headers, tables.states,
		tables.materials, tables.transforms, tables.lights, tables.specular,
		tables.payload_words, tables.world_aux, tables.indirect
	};
	for (const FrameBufferSlice &slice : transfer_slices)
		impl_->ci.state_tracker->UseBuffer(slice.buffer, slice.offset, slice.size,
			{ VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
			  impl_->ci.platform->GraphicsQueueFamily(), ResourceIntent::Write });
	for (const RetainedPayloadCopy &copy : retained_copies)
	{
		impl_->ci.state_tracker->UseBuffer(copy.source, copy.source_offset,
			copy.byte_size,
			{ VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
			  impl_->ci.platform->GraphicsQueueFamily(), ResourceIntent::Read });
		const VkDeviceSize destination_offset = tables.payload_words.offset +
			VkDeviceSize(copy.destination_word) * sizeof(uint32_t);
		impl_->ci.state_tracker->UseBuffer(tables.payload_words.buffer,
			destination_offset, copy.byte_size,
			{ VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
			  impl_->ci.platform->GraphicsQueueFamily(), ResourceIntent::Write });
		impl_->ci.state_tracker->Flush(command);
		VkBufferCopy2 region = { VK_STRUCTURE_TYPE_BUFFER_COPY_2 };
		region.srcOffset = copy.source_offset;
		region.dstOffset = destination_offset;
		region.size = copy.byte_size;
		VkCopyBufferInfo2 info = { VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2 };
		info.srcBuffer = copy.source;
		info.dstBuffer = tables.payload_words.buffer;
		info.regionCount = 1;
		info.pRegions = &region;
		vkCmdCopyBuffer2(command, &info);
	}
	if (!impl_->PrepareTerrainEmitters(*capture, &draws, &pages, command))
	{
		impl_->ci.frames->AbandonCurrentRecording();
		impl_->ci.retained_world->AbandonPendingUploadRecording();
		impl_->ci.textures->DiscardCapture(*capture);
		return impl_->Fail("terrain-emitter",
			"T2 terrain compute preparation or dispatch failed");
	}
	if (!impl_->UploadGtaoNoise(*capture, command))
	{
		impl_->ci.frames->AbandonCurrentRecording();
		impl_->ci.retained_world->AbandonPendingUploadRecording();
		impl_->ci.textures->DiscardCapture(*capture);
		return impl_->Fail("gtao-noise", "GTAO noise upload failed");
	}
	if (!impl_->PrepareDescriptorPages(*capture, tables, &pages))
	{
		impl_->ci.frames->AbandonCurrentRecording();
		impl_->ci.retained_world->AbandonPendingUploadRecording();
		impl_->ci.textures->DiscardCapture(*capture);
		return impl_->Fail("descriptor-residency",
			"texture upload or immutable descriptor initialization failed");
	}
	cpu_prepare = CpuClock::now();

	bool rendering_open = false;
	RenderTargetClass rendering_target = RenderTargetClass::Count;
	bool first_scene_target = true;
	uint32_t post_present_index = 0;
	uint32_t rendering_instances = 0;
	AcquiredImage acquired;
	bool acquired_image = false;
	auto end_rendering = [&]() {
		if (rendering_open)
		{
			vkCmdEndRendering(command);
			rendering_open = false;
			rendering_target = RenderTargetClass::Count;
		}
	};
	auto begin_target = [&](const BeginFrameTargetCommand &begin) -> bool {
		end_rendering();
		const CapturedTargetLayout &layout = begin.target == RenderTargetClass::Scene ?
			impl_->ci.targets->SceneLayout() :
			(begin.target == RenderTargetClass::PostPresent ?
			 impl_->ci.targets->PostLayout() : impl_->ci.targets->CockpitLayout());
		VkRenderingAttachmentInfo colors[5] = {};
		uint32_t color_count = begin.target == RenderTargetClass::Scene ? 5u : 1u;
		for (uint32_t i = 0; i < color_count; ++i)
		{
			TargetImageRef image = begin.target == RenderTargetClass::PostPresent ?
				impl_->ci.targets->GraphImage(GraphResource::PostPresent,
					post_present_index) : impl_->ci.targets->Attachment(begin.target, i);
			if (!image.Valid()) return false;
			impl_->ci.state_tracker->UseImage(image.image, FullRange(image),
				{ VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
				  VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
				  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
				  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				  impl_->ci.platform->GraphicsQueueFamily(), ResourceIntent::ReadWrite });
			colors[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
			colors[i].imageView = image.view;
			colors[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			colors[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
			colors[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			if ((begin.clear_flags & 2) && i == 0)
			{
				colors[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
				colors[i].clearValue.color.float32[3] = 1.0f;
			}
			if (begin.target == RenderTargetClass::CockpitScene && i == 0)
			{
				colors[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
				colors[i].clearValue = {};
			}
			if (begin.target == RenderTargetClass::Scene && first_scene_target && i > 0)
			{
				colors[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
				colors[i].clearValue = {};
			}
		}
		VkRenderingAttachmentInfo depth = {
			VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO
		};
		if (begin.target != RenderTargetClass::PostPresent)
		{
			TargetImageRef image = impl_->ci.targets->Attachment(begin.target, 5);
			impl_->ci.state_tracker->UseImage(image.image, FullRange(image),
				{ VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
				  VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
				  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
				  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				  VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
				  impl_->ci.platform->GraphicsQueueFamily(), ResourceIntent::ReadWrite });
			depth.imageView = image.view;
			depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
			depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
			depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			if ((begin.clear_flags & 1) ||
				begin.target == RenderTargetClass::CockpitScene)
			{
				depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
				depth.clearValue.depthStencil.depth = 1.0f;
			}
		}
		impl_->ci.state_tracker->Flush(command);
		VkRenderingInfo info = { VK_STRUCTURE_TYPE_RENDERING_INFO };
		info.renderArea.extent = { layout.internal_width, layout.internal_height };
		info.layerCount = 1;
		info.colorAttachmentCount = color_count;
		info.pColorAttachments = colors;
		if (begin.target != RenderTargetClass::PostPresent)
			info.pDepthAttachment = &depth;
		vkCmdBeginRendering(command, &info);
		rendering_open = true;
		rendering_target = begin.target;
		++rendering_instances;
		if (begin.target == RenderTargetClass::Scene) first_scene_target = false;
		return true;
	};

	uint32_t draw_cursor = 0;
	uint32_t indirect_batch_count = 0;
	auto fail_recording = [&](const char *stage, const char *message) -> bool {
		end_rendering();
		impl_->ci.frames->AbandonCurrentRecording();
		impl_->ci.retained_world->AbandonPendingUploadRecording();
		impl_->ci.textures->DiscardCapture(*capture);
		if (acquired_image)
		{
			const uint64_t drain_value = impl_->ci.frames->NextTimelineValue();
			if (drain_value && impl_->ci.wsi->DrainAbandonedAcquire(
				acquired.token, drain_value) == WsiStatus::Success)
				impl_->ci.frames->ConfirmExternalSubmission(drain_value);
		}
		return impl_->Fail(stage, message);
	};

	for (const PlanOperation &operation : impl_->last_plan.operations)
	{
		if (operation.type != PlanOperationType::CapturedCommand)
		{
			const bool resume_scene = rendering_open &&
				operation.type == PlanOperationType::InsertedGraphNode &&
				operation.inserted_graph_node ==
					InsertedGraphNodeId::AcquireSoftDepth &&
				rendering_target == RenderTargetClass::Scene;
			end_rendering();
			if (operation.graph_node == GraphNodeId::Present)
				continue;
			if (!impl_->EncodeGraphOperation(*capture, operation, command,
				&post_present_index, &rendering_instances))
				return fail_recording("graph-encode",
					"post pipeline/resource contract could not be encoded");
			if (resume_scene)
			{
				BeginFrameTargetCommand resume = {};
				resume.target = RenderTargetClass::Scene;
				if (!begin_target(resume))
					return fail_recording("target-resume",
						"scene target could not resume after soft-depth capture");
			}
			continue;
		}
		if (operation.capture_command_index >= capture->Commands().size())
			return fail_recording("command-encode", "command index is out of range");
		const CaptureCommand &captured =
			capture->Commands()[operation.capture_command_index];
		switch (captured.type)
		{
		case CaptureCommandType::BeginFrameTarget:
			if (!begin_target(captured.payload.begin_frame_target))
				return fail_recording("target-begin", "target attachment is unavailable");
			break;
		case CaptureCommandType::ClearColor:
		{
			if (!rendering_open) return fail_recording("clear", "no active target");
			const ClearColorCommand &clear = captured.payload.clear_color;
			VkClearAttachment attachments[5] = {};
			uint32_t count = 0;
			const uint32_t bits[5] = { kWriteColor, kWriteVelocity,
				kWriteProtectionMask, kWriteAoClass, kWriteObjectId };
			for (uint32_t i = 0; i < 5; ++i)
				if ((clear.selected_attachments & bits[i]) != 0)
				{
					attachments[count].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					attachments[count].colorAttachment = i;
					std::memcpy(attachments[count].clearValue.color.float32,
						clear.rgba, sizeof(clear.rgba));
					++count;
				}
			VkClearRect rect = {};
			const CapturedTargetLayout &layout = rendering_target ==
				RenderTargetClass::Scene ? impl_->ci.targets->SceneLayout() :
				(rendering_target == RenderTargetClass::PostPresent ?
				 impl_->ci.targets->PostLayout() : impl_->ci.targets->CockpitLayout());
			rect.rect.offset = clear.whole_target ? VkOffset2D{0,0} :
				VkOffset2D{clear.rect.x * static_cast<int32_t>(layout.ssaa_factor),
				 clear.rect.y * static_cast<int32_t>(layout.ssaa_factor)};
			rect.rect.extent = clear.whole_target ?
				VkExtent2D{layout.internal_width, layout.internal_height} :
				VkExtent2D{static_cast<uint32_t>(clear.rect.width) * layout.ssaa_factor,
				 static_cast<uint32_t>(clear.rect.height) * layout.ssaa_factor};
			rect.layerCount = 1;
			if (count) vkCmdClearAttachments(command, count, attachments, 1, &rect);
			break;
		}
		case CaptureCommandType::ClearDepth:
		{
			if (!rendering_open || rendering_target == RenderTargetClass::PostPresent)
				break;
			VkClearAttachment clear = {};
			clear.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			clear.clearValue.depthStencil.depth = captured.payload.clear_depth.depth;
			const CapturedTargetLayout &layout = rendering_target ==
				RenderTargetClass::Scene ? impl_->ci.targets->SceneLayout() :
				impl_->ci.targets->CockpitLayout();
			VkClearRect rect = {};
			rect.rect.extent = { layout.internal_width, layout.internal_height };
			rect.layerCount = 1;
			vkCmdClearAttachments(command, 1, &clear, 1, &rect);
			break;
		}
		case CaptureCommandType::DrawStream:
		case CaptureCommandType::DrawRetained:
		case CaptureCommandType::FlushFontBatches:
			while (draw_cursor < draws.size() &&
				DrawCommandsAreContiguous(*capture,
					operation.capture_command_index,
					draws[draw_cursor].capture_command))
			{
				uint32_t run_count = 1;
				while (draw_cursor + run_count < draws.size() &&
					DrawCommandsAreContiguous(*capture,
						draws[draw_cursor + run_count - 1].capture_command,
						draws[draw_cursor + run_count].capture_command) &&
					impl_->CanBatchDraws(draws[draw_cursor + run_count - 1],
						draws[draw_cursor + run_count]))
					++run_count;
				if (!rendering_open || !impl_->EncodeDrawRun(*capture, draws,
					draw_cursor, run_count, tables,
					pages[draws[draw_cursor].page_index], command))
					return fail_recording("world-draw",
						"precreated exact world pipeline is unavailable");
				draw_cursor += run_count;
				++indirect_batch_count;
			}
			break;
		case CaptureCommandType::ReadPixel:
		case CaptureCommandType::ReadImage:
		{
			end_rendering();
			ImageSemantic semantic = captured.type == CaptureCommandType::ReadPixel ?
				captured.payload.read_pixel.source : captured.payload.read_image.source;
			TargetImageRef source = impl_->ImageSemanticRef(semantic,
				post_present_index);
			if (impl_->ci.targets->SceneLayout().msaa_samples > 1)
			{
				GraphNodeId resolve_node = GraphNodeId::Count;
				GraphResource resolve_resource = GraphResource::Count;
				switch (semantic)
				{
				case ImageSemantic::SceneColor:
					resolve_node = GraphNodeId::ResolveColor;
					resolve_resource = GraphResource::ResolvedColor;
					break;
				case ImageSemantic::SceneDepth:
					resolve_node = GraphNodeId::ResolveDepth;
					resolve_resource = GraphResource::ResolvedDepth;
					break;
				case ImageSemantic::Velocity:
					resolve_node = GraphNodeId::ResolveVelocity;
					resolve_resource = GraphResource::ResolvedVelocity;
					break;
				case ImageSemantic::ProtectionMask:
					resolve_node = GraphNodeId::ResolveProtectionMask;
					resolve_resource = GraphResource::ResolvedProtectionMask;
					break;
				case ImageSemantic::AoClass:
					resolve_node = GraphNodeId::ResolveAoClass;
					resolve_resource = GraphResource::ResolvedAoClass;
					break;
				case ImageSemantic::MotionObjectId:
					resolve_node = GraphNodeId::ResolveObjectId;
					resolve_resource = GraphResource::ResolvedObjectId;
					break;
				default:
					break;
				}
				if (resolve_node != GraphNodeId::Count)
				{
					PlanOperation resolve;
					resolve.type = PlanOperationType::GraphNode;
					resolve.graph_node = resolve_node;
					if (!impl_->EncodeGraphOperation(*capture, resolve, command,
							&post_present_index, &rendering_instances))
						return fail_recording("readback",
							"multisample readback resolve failed");
					source = impl_->ci.targets->GraphImage(resolve_resource);
				}
			}
			if (!source.Valid())
				return fail_recording("readback", "source image is unavailable");
			impl_->ci.state_tracker->UseImage(source.image, FullRange(source),
				{ VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
				  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				  impl_->ci.platform->GraphicsQueueFamily(), ResourceIntent::Read });
			impl_->ci.state_tracker->Flush(command);
			LogicalRect rect = captured.type == CaptureCommandType::ReadPixel ?
				LogicalRect{ captured.payload.read_pixel.x,
					captured.payload.read_pixel.y, 1, 1 } :
				captured.payload.read_image.rect;
			const VkDeviceSize bytes = VkDeviceSize(rect.width) * rect.height *
				((source.aspect & VK_IMAGE_ASPECT_COLOR_BIT) ? 4u : 4u);
			FrameBufferSlice destination = impl_->ci.frames->Allocate(
				FrameBufferClass::Readback, bytes, 4);
			if (!destination.Valid())
				return fail_recording("readback", "readback arena exhausted");
			VkBufferImageCopy2 region = { VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2 };
			region.bufferOffset = destination.offset;
			region.imageSubresource.aspectMask = source.aspect;
			region.imageSubresource.layerCount = 1;
			region.imageOffset = { rect.x, rect.y, 0 };
			region.imageExtent = { static_cast<uint32_t>(rect.width),
				static_cast<uint32_t>(rect.height), 1 };
			VkCopyImageToBufferInfo2 copy = {
				VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2
			};
			copy.srcImage = source.image;
			copy.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			copy.dstBuffer = destination.buffer;
			copy.regionCount = 1;
			copy.pRegions = &region;
			vkCmdCopyImageToBuffer2(command, &copy);
			CompiledReadback readback;
			readback.request = captured.type == CaptureCommandType::ReadPixel ?
				captured.payload.read_pixel.request : captured.payload.read_image.request;
			readback.format = captured.type == CaptureCommandType::ReadPixel ?
				captured.payload.read_pixel.format : captured.payload.read_image.format;
			readback.row_order = captured.type == CaptureCommandType::ReadPixel ?
				ReadbackRowOrder::TopDown : captured.payload.read_image.row_order;
			readback.width = rect.width;
			readback.height = rect.height;
			readback.slice = destination;
			submission->readbacks.push_back(readback);
			break;
		}
		default:
			break;
		}
	}
	end_rendering();

	if (present)
	{
		AcquireRequest request;
		request.frame_context_index = frame->frame_slot;
		request.completed_graphics_timeline = impl_->ci.frames->PollCompletedTimeline();
		acquired = impl_->ci.wsi->Acquire(request);
		if (acquired.HasImage())
		{
			acquired_image = true;
			TargetImageRef source = impl_->ci.targets->GraphImage(
				GraphResource::PostPresent, post_present_index);
			TargetImageRef swapchain;
			swapchain.image = acquired.image;
			swapchain.view = acquired.view;
			swapchain.format = impl_->ci.wsi->Format();
			swapchain.extent = acquired.extent;
			swapchain.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
			impl_->ci.state_tracker->UseImage(source.image, FullRange(source),
				{ VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
				  VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
				  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				  impl_->ci.platform->GraphicsQueueFamily(), ResourceIntent::Read });
			impl_->ci.state_tracker->UseImage(swapchain.image, FullRange(swapchain),
				{ VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
				  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
				  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				  impl_->ci.platform->GraphicsQueueFamily(), ResourceIntent::Write });
			impl_->ci.state_tracker->Flush(command);
			PlanOperation present_operation;
			present_operation.type = PlanOperationType::GraphNode;
			present_operation.graph_node = GraphNodeId::Present;
			const PostPassUniforms uniforms = impl_->MakePostUniforms(*capture,
				present_operation, source, swapchain);
			FrameBufferSlice uniform_slice;
			const VkDeviceSize alignment = std::max<VkDeviceSize>(1,
				impl_->ci.platform->SelectedDevice().properties.limits.minUniformBufferOffsetAlignment);
			if (!impl_->ci.frames->StageBuffer(&uniforms, sizeof(uniforms),
				FrameBufferClass::Storage, alignment, &uniform_slice))
				return fail_recording("present", "present uniform staging failed");
			impl_->ci.state_tracker->UseBuffer(uniform_slice.buffer,
				uniform_slice.offset, uniform_slice.size,
				{ VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
				  impl_->ci.platform->GraphicsQueueFamily(), ResourceIntent::Write });
			impl_->ci.state_tracker->UseBuffer(uniform_slice.buffer,
				uniform_slice.offset, uniform_slice.size,
				{ VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
				  VK_ACCESS_2_UNIFORM_READ_BIT,
				  impl_->ci.platform->GraphicsQueueFamily(), ResourceIntent::Read });
			impl_->ci.state_tracker->Flush(command);
			VkDescriptorSet set = VK_NULL_HANDLE;
			if (!impl_->ci.pipelines->AllocatePostDescriptorSet(frame->descriptor_pool,
				GraphNodeId::Present, PostPassVariant::Only, &set))
				return fail_recording("present", "present descriptor allocation failed");
			PostImageWrite image;
			image.binding = 2;
			image.kind = PostDescriptorKind::SampledFloat2D;
			image.view = source.view;
			PostDescriptorWrite descriptor;
			descriptor.uniform_buffer = uniform_slice.buffer;
			descriptor.uniform_offset = uniform_slice.offset;
			descriptor.images = &image;
			descriptor.image_count = 1;
			if (!impl_->ci.pipelines->UpdatePostDescriptorSet(set,
				GraphNodeId::Present, PostPassVariant::Only, descriptor))
				return fail_recording("present", "present descriptor update failed");
			VkRenderingAttachmentInfo color = {
				VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO
			};
			color.imageView = acquired.view;
			color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			color.clearValue.color.float32[3] = 1.0f;
			VkRenderingInfo rendering = { VK_STRUCTURE_TYPE_RENDERING_INFO };
			rendering.renderArea.extent = acquired.extent;
			rendering.layerCount = 1;
			rendering.colorAttachmentCount = 1;
			rendering.pColorAttachments = &color;
			vkCmdBeginRendering(command, &rendering);
			++rendering_instances;
			VkPipeline pipeline = impl_->ci.pipelines->FindPostPipeline(
				GraphNodeId::Present, PostPassVariant::Only, impl_->ci.wsi->Format());
			if (!pipeline)
				return fail_recording("present", "safe-UNORM present pipeline missing");
			vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
			vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
				impl_->ci.pipelines->PostPipelineLayout(GraphNodeId::Present,
					PostPassVariant::Only), 0, 1, &set, 0, nullptr);
			LogicalRect present_rect = { 0, 0,
				static_cast<int32_t>(acquired.extent.width),
				static_cast<int32_t>(acquired.extent.height) };
			for (auto it = capture->Commands().rbegin();
				it != capture->Commands().rend(); ++it)
				if (it->type == CaptureCommandType::Present &&
					it->payload.present.present_rect < capture->PresentRects().size())
				{
					present_rect = capture->PresentRects()[
						it->payload.present.present_rect].rect;
					break;
				}
			VkViewport viewport = { static_cast<float>(present_rect.x),
				static_cast<float>(present_rect.y),
				static_cast<float>(present_rect.width),
				static_cast<float>(present_rect.height), 0, 1 };
			VkRect2D scissor = {
				{ present_rect.x, present_rect.y },
				{ static_cast<uint32_t>(present_rect.width),
				  static_cast<uint32_t>(present_rect.height) }
			};
			vkCmdSetViewport(command, 0, 1, &viewport);
			vkCmdSetScissor(command, 0, 1, &scissor);
			vkCmdDraw(command, 3, 1, 0, 0);
			vkCmdEndRendering(command);
			impl_->ci.state_tracker->UseImage(swapchain.image, FullRange(swapchain),
				{ VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE,
				  VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
				  impl_->ci.platform->PresentQueueFamily(), ResourceIntent::Read });
			impl_->ci.state_tracker->Flush(command);
		}
		else if (acquired.status != WsiStatus::Paused)
			return fail_recording("acquire", impl_->ci.wsi->LastError().c_str());
	}

	if (gpu_timestamps_enabled)
	{
		vkCmdWriteTimestamp2(command, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
			frame->timestamp_pool, 1);
		frame->recorded_timestamp_count = 2;
	}
	if (!impl_->ci.frames->EndRecording())
		return fail_recording("command-end", "vkEndCommandBuffer failed");
	cpu_record = CpuClock::now();
	FrameSubmitInfo submit_info;
	if (acquired_image)
	{
		const WsiSubmissionSync sync = impl_->ci.wsi->SubmissionSync(acquired.token);
		if (sync.status != WsiStatus::Success)
			return fail_recording("wsi-sync", "invalid acquire token state");
		submit_info.wait_binary = sync.acquire_wait.semaphore;
		submit_info.wait_stage = sync.acquire_wait.stageMask;
		submit_info.signal_binary = sync.render_finished_signal.semaphore;
	}
	uint64_t timeline = 0;
	if (!impl_->ci.frames->Submit(submit_info, &timeline))
	{
		const VkResult submit_result = impl_->ci.platform->LastVulkanResult();
		if (acquired_image)
		{
			impl_->ci.wsi->ConfirmSubmission(acquired.token, 0, submit_result);
			if (submit_result != VK_ERROR_DEVICE_LOST)
			{
				// vkQueueSubmit2 failure leaves the successful acquire semaphore
				// signaled and unconsumed.  Consume it with an independent empty
				// submission and publish that submission's global timeline value so
				// swapchain recreation can retire this stopped generation.
				const uint64_t drain_value = impl_->ci.frames->NextTimelineValue();
				if (drain_value != 0 && impl_->ci.wsi->DrainAbandonedAcquire(
					acquired.token, drain_value) == WsiStatus::Success)
					impl_->ci.frames->ConfirmExternalSubmission(drain_value);
			}
		}
		impl_->ci.frames->AbandonCurrentRecording();
		impl_->ci.textures->DiscardCapture(*capture);
		impl_->ci.retained_world->AbandonPendingUploadRecording();
		return impl_->Fail("queue-submit", "vkQueueSubmit2 failed");
	}
	cpu_submit = CpuClock::now();
	// From this point onward the tracked transitions exist on the queue and
	// must never be rolled back, even if WSI bookkeeping subsequently fails.
	state_transaction.MarkSubmitted();
	impl_->ci.state_tracker->StampSubmission(timeline);
	impl_->ci.retained_world->CommitPendingUploads(timeline);
	for (const DrawWork &draw : draws)
		if (draw.geometry_mode != GeometryMode::T0Stream)
			impl_->ci.retained_world->StampUse(draw.mesh, timeline);
	impl_->ci.targets->StampUse(timeline);
	impl_->ci.textures->PinCapture(*capture, timeline);
	if (acquired_image && impl_->ci.wsi->ConfirmSubmission(acquired.token,
		timeline, VK_SUCCESS) != WsiStatus::Success)
		return impl_->Fail("wsi-submit-confirm", "WSI rejected successful submit");
	if (!capture->MarkCompiled())
		return impl_->Fail("capture-complete", "capture lifecycle transition failed");
	ResourceStateSnapshot snapshot = impl_->ci.state_tracker->Snapshot(
		++impl_->next_snapshot_serial);
	if (impl_->snapshots.size() == 8)
		impl_->snapshots.erase(impl_->snapshots.begin());
	impl_->snapshots.push_back(snapshot);
	submission->timeline_value = timeline;
	submission->resource_state_snapshot_serial = snapshot.serial;
	submission->wsi_token = acquired_image ? acquired.token : 0;
	submission->direct_draws = 0;
	submission->indirect_commands = 0;
	for (const DrawWork &draw : draws)
		if (draw.geometry_mode == GeometryMode::T2Terrain)
			submission->indirect_commands += draw.terrain_batch_count;
		else
			++submission->indirect_commands;
	submission->indirect_batches = indirect_batch_count;
	submission->graph_passes = impl_->last_plan.graph_pass_count;
	submission->dynamic_rendering_instances = rendering_instances;
	submission->descriptor_page_binds = static_cast<uint32_t>(pages.size());
	if (acquired_image)
		submission->presentation = impl_->ci.wsi->Present(acquired.token);
	else if (present)
		submission->presentation.status = WsiStatus::Paused;

	const CpuClock::time_point cpu_end = CpuClock::now();
	const char *profile_path = std::getenv("PICCU_VK_PERF_LOG");
	if (profile_path && profile_path[0])
	{
		auto milliseconds = [](CpuClock::time_point first,
			CpuClock::time_point last) {
			return std::chrono::duration<double, std::milli>(last - first).count();
		};
		FILE *profile = std::fopen(profile_path, "ab+");
		if (profile)
		{
			std::fseek(profile, 0, SEEK_END);
			if (std::ftell(profile) == 0)
				std::fprintf(profile,
					"frame\tpresent\tcommands\tdraws\tstream\tretained\tterrain\tpages\tbatches\t"
					"stream_other\tstream_room\tstream_terrain\tstream_object\t"
					"stream_cockpit\tstream_gauge\tstream_effect\tstream_vertices\tstream_indices\t"
					"plan_ms\tlower_ms\tpipelines_ms\ttables_ms\tpage_build_ms\t"
					"reserve_begin_ms\tprepare_ms\trecord_ms\tsubmit_ms\tpresent_ms\ttotal_ms\t"
					"gpu_ms\n");
			uint32_t stream_draws = 0;
			uint32_t retained_draws = 0;
			uint32_t terrain_draws = 0;
			uint32_t stream_categories[7] = {};
			uint64_t stream_vertices = 0;
			uint64_t stream_indices = 0;
			for (const DrawWork &draw : draws)
			{
				if (draw.geometry_mode == GeometryMode::T1Retained)
					++retained_draws;
				else if (draw.geometry_mode == GeometryMode::T2Terrain)
					++terrain_draws;
				else
				{
					++stream_draws;
					const uint32_t category_3d =
						(draw.raster.shader.draw_classification >> 16u) & 0xffffu;
					if (category_3d < 7)
						++stream_categories[category_3d];
					stream_vertices += draw.vertex_count;
					stream_indices += draw.index_count;
				}
			}
			std::fprintf(profile,
				"%u\t%u\t%zu\t%zu\t%u\t%u\t%u\t%zu\t%u\t"
				"%u\t%u\t%u\t%u\t%u\t%u\t%u\t%llu\t%llu\t"
				"%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\n",
				capture->PresentedFrameSerial(), present ? 1u : 0u,
				capture->Commands().size(), draws.size(), stream_draws,
				retained_draws, terrain_draws, pages.size(), indirect_batch_count,
				stream_categories[0], stream_categories[1], stream_categories[2],
				stream_categories[3], stream_categories[4], stream_categories[5],
				stream_categories[6],
				static_cast<unsigned long long>(stream_vertices),
				static_cast<unsigned long long>(stream_indices),
				milliseconds(cpu_begin, cpu_plan),
				milliseconds(cpu_plan, cpu_draw_lowering),
				milliseconds(cpu_draw_lowering, cpu_pipelines),
				milliseconds(cpu_pipelines, cpu_tables),
				milliseconds(cpu_tables, cpu_pages),
				milliseconds(cpu_pages, cpu_frame_begin),
				milliseconds(cpu_frame_begin, cpu_prepare),
				milliseconds(cpu_prepare, cpu_record),
				milliseconds(cpu_record, cpu_submit),
				milliseconds(cpu_submit, cpu_end),
				milliseconds(cpu_begin, cpu_end),
				completed_gpu_frame_valid ? completed_gpu_frame_ms : -1.0);
			std::fclose(profile);
		}
	}
	return true;
}

} // namespace vk
} // namespace render
} // namespace piccu
