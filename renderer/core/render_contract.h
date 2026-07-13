/*
 * PiccuEngine API-neutral renderer contract.
 *
 * This file is a mechanical transcription of the frozen Vulkan renderer
 * specification.  It is shared by capture, trace, compiler, retained-world,
 * and verification code.  Vulkan and OpenGL API types are forbidden here.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <type_traits>

namespace piccu
{
namespace render
{

constexpr uint32_t kRenderContractVersion = 1;
constexpr uint32_t kCaptureSchemaVersion = 2;
constexpr uint32_t kShaderAbiVersion = 1;
constexpr uint32_t kInvalidId = 0xffffffffu;
constexpr uint32_t kMaxSpecularSources = 4;
constexpr uint32_t kMaxDynamicLights = 8;
constexpr uint32_t kMaxTerrainDynamicLights = 4u * kMaxDynamicLights;
constexpr uint32_t kWorldSamplerCount = 32;
constexpr uint32_t kWorldArrayImageCount = 8;
constexpr uint32_t kFrameContextCount = 2;
constexpr uint32_t kClearDepthFlag = 1;
constexpr uint32_t kClearColorFlag = 2;

using StateId = uint32_t;
using TransformId = uint32_t;
using MaterialRef = uint32_t;
using PayloadRef = uint32_t;
using ViewStateId = uint32_t;
using ViewportId = uint32_t;
using TargetVersionId = uint32_t;
using TextureVersionId = uint32_t;
using ReadbackRequestId = uint32_t;
using RenderTargetSignatureId = uint32_t;
using TargetLayoutId = uint32_t;
using PresentRectId = uint32_t;
using WsiSignatureId = uint32_t;
using PayloadDataId = uint32_t;

struct Span32
{
	uint32_t offset;
	uint32_t count;
};

struct LogicalRect
{
	int32_t x;
	int32_t y;
	int32_t width;
	int32_t height;
};

struct MeshHandle
{
	uint32_t id;
	uint32_t generation;
};

enum class RenderTargetClass : uint32_t
{
	Scene = 0,
	PostPresent = 1,
	CockpitScene = 2,
	Count = 3,
};

enum class GeometryMode : uint32_t
{
	T0Stream = 0,
	T1Retained = 1,
	T2Terrain = 2,
};

enum class PrimitiveSourceKind : uint32_t
{
	PolygonFan = 0,
	ExplicitTriangles,
	Polygon2D,
	Bitmap,
	Font,
	ParticleInstances,
	Line,
	SpecialLine,
	SpecialLineBatch,
	Point,
	TerrainEmitter,
	Editor,
	Count,
};

enum class ReadbackFormat : uint32_t
{
	RawRgba8 = 0,
	Rgb8TopDown,
	Rgb565,
	R32Float,
	Rg16Float,
	R32Uint,
	Count,
};

enum class ReadbackRowOrder : uint32_t
{
	TopDown = 0,
	BottomUp = 1,
	Count = 2,
};

enum class ImageSemantic : uint32_t
{
	SceneColor = 0,
	SceneDepth,
	Velocity,
	ProtectionMask,
	AoClass,
	MotionObjectId,
	CapturedWorldColor,
	CapturedWorldDepth,
	PostPresent,
	CockpitScene,
	CockpitComposite,
	Swapchain,
	Count,
};

enum class DepthInterpretation : uint32_t
{
	EyeZLegacyMapped = 0,
	AlreadyMapped = 1,
	Irrelevant = 2,
	Count = 3,
};

enum class RenderFormat : uint32_t
{
	R8G8B8A8Unorm = 0,
	R16G16Sfloat,
	R8G8Unorm,
	R8Unorm,
	R32Uint,
	D32Sfloat,
	R32G32Sfloat,
	R16G16B16A16Sfloat,
	Count,
};

enum class BlendClass : uint32_t
{
	Opaque = 0,
	Alpha,
	Saturate,
	Multiply,
};

enum class RasterFamily : uint32_t
{
	Ordinary = 0,
	ExpandedLine = 1,
	ExpandedPoint = 2,
};

enum MrtWriteBits : uint32_t
{
	kWriteColor = 1u << 0,
	kWriteVelocity = 1u << 1,
	kWriteProtectionMask = 1u << 2,
	kWriteAoClass = 1u << 3,
	kWriteObjectId = 1u << 4,
};

enum AttachmentChannelBits : uint32_t
{
	kChannelRed = 1u << 0,
	kChannelGreen = 1u << 1,
	kChannelBlue = 1u << 2,
	kChannelAlpha = 1u << 3,
	kChannelRgba = (1u << 4) - 1,
};

enum TargetAttachmentBits : uint32_t
{
	kTargetAttachmentColor = kWriteColor,
	kTargetAttachmentVelocity = kWriteVelocity,
	kTargetAttachmentProtectionMask = kWriteProtectionMask,
	kTargetAttachmentAoClass = kWriteAoClass,
	kTargetAttachmentObjectId = kWriteObjectId,
	kTargetAttachmentDepth = 1u << 5,
	kTargetAttachmentAll = (1u << 6) - 1,
	kTargetAttachmentMandatory = kTargetAttachmentColor | kTargetAttachmentDepth,
};

enum TargetFeatureBits : uint32_t
{
	kTargetFeatureLatePost = 1u << 0,
	kTargetFeatureBloom = 1u << 1,
	kTargetFeatureGtao = 1u << 2,
	kTargetFeatureMotionConsumer = 1u << 3,
	kTargetFeatureGtaoTemporal = 1u << 4,
	kTargetFeatureCockpitDeferral = 1u << 5,
	kTargetFeatureSoftParticles = 1u << 6,
	kTargetFeatureAll = (1u << 7) - 1,
};

// The only legal logical write-mask combinations in ABI v1.
constexpr uint32_t kLegalMrtWriteMasks[] = {
	kWriteColor,
	kWriteColor | kWriteProtectionMask,
	kWriteColor | kWriteProtectionMask | kWriteAoClass,
	kWriteColor | kWriteVelocity | kWriteProtectionMask | kWriteAoClass,
	kWriteColor | kWriteVelocity | kWriteProtectionMask | kWriteAoClass | kWriteObjectId,
	kWriteProtectionMask,
	kWriteProtectionMask | kWriteAoClass,
	kWriteVelocity | kWriteObjectId,
};

struct BaseVertex
{
	float position[3];
	uint32_t rgba8;
	float uv0[2];
	float uv1[2];
};

struct alignas(16) PerspectiveVertexPayload
{
	float value_q[4];
};

struct alignas(16) MotionVertexPayload
{
	float current_q[4];
	float previous_q[4];
};

struct alignas(16) SpecularVertexPayload
{
	float normal_or_position_q[4];
	float field_center_q[kMaxSpecularSources][4];
	float field_color[kMaxSpecularSources][4];
};

// Frozen T2 retained-input records.  These records are capture-owned PODs and
// are shared verbatim by the retained-world bridge and the frame compiler.
// The four-wide aliases deliberately match the std430 records consumed by the
// classify/scan/emit kernels.  packed = segment; rotation/lightmap-page/
// texture-layer; batch index; batch output first vertex.  Heights are the
// segment +W, +W+1, +1, and segment corners in that order.
struct alignas(16) TerrainEmitterCell
{
	uint32_t packed[4];
	float height[4];
};

struct alignas(16) TerrainWorkItem
{
	uint32_t cell_index;
	int32_t source_texture;
	uint32_t source_flags;
	uint32_t full_draw_order;
};

struct alignas(16) TerrainBatchInput
{
	int32_t source_texture;
	uint32_t texture_layer;
	uint32_t first_work_item;
	uint32_t work_item_count;
	uint32_t first_output_vertex;
	uint32_t output_vertex_capacity;
	uint32_t indirect_command_index;
	uint32_t reserved0;
};

struct alignas(16) TerrainViewInput
{
	float terrain_row0[4];
	float terrain_x_step[4];
	float terrain_z_step[4];
	float terrain_y_step[4];
	float projection_center_half_size[4];
	float viewport_size_inv_size[4];
	float clip_scale[4];
};

enum TerrainCellFlags : uint32_t
{
	kTerrainCellDynamic = 0x01,
	kTerrainCellSpecialWater = 0x04,
	kTerrainCellSpecialMine = 0x08,
	kTerrainCellInvisible = 0x10,
	kTerrainCellRegionMask = 0xe0,
	kTerrainCellAllFlags = kTerrainCellDynamic | kTerrainCellSpecialWater |
		kTerrainCellSpecialMine | kTerrainCellInvisible | kTerrainCellRegionMask,
};

enum TerrainPackedCellConstants : uint32_t
{
	kTerrainSegmentXMask = 0x000000ffu,
	kTerrainSegmentZMask = 0x0000ff00u,
	kTerrainSegmentZShift = 8,
	kTerrainRotationMask = 0x000000ffu,
	kTerrainRotatorMask = 0x0000000fu,
	kTerrainTileMask = 0x000000f0u,
	kTerrainTileShift = 4,
	kTerrainLightmapPageMask = 0x0000ff00u,
	kTerrainLightmapPageShift = 8,
	kTerrainTextureLayerMask = 0xffff0000u,
	kTerrainTextureLayerShift = 16,
	kTerrainPackedPageLightmapMask = 0x000000ffu,
	kTerrainPackedPageTextureShift = 8,
};

constexpr uint32_t kTerrainLightmapPageCount = 4;
constexpr uint32_t kTerrainClipMaxVertices = 8;
constexpr uint32_t kTerrainMaximumOutputVerticesPerCell = 36;

// Mandatory T2 output stream.  Page IDs are flat shader inputs; world_q is
// noperspective and divided by w in the terrain fragment path.
struct alignas(16) TerrainVertexPayload
{
	float world_q[4];
	uint32_t packed_pages;
	uint32_t reserved[3];
};

struct alignas(16) GpuDrawHeader
{
	uint32_t state_index;
	uint32_t material_index;
	uint32_t transform_index;
	uint32_t flags;
	uint32_t vertex_payload_offset;
	uint32_t motion_payload_offset;
	uint32_t specular_payload_offset;
	uint32_t room_or_terrain_index;
};

struct alignas(16) GpuShaderState
{
	uint32_t shader_flags;
	uint32_t texture_type;
	uint32_t overlay_type;
	uint32_t lighting_color_model;
	uint32_t alpha_type;
	uint32_t alpha_value;
	uint32_t blend_class;
	uint32_t draw_classification;
	float alpha_factor;
	float z_bias;
	float fog_near_mapped;
	float fog_far_mapped;
	float flat_color[4];
	float fog_color[4];
	float light_direction[4];
	float post_values[4];
	uint32_t dynamic_light_first;
	uint32_t dynamic_light_count;
	uint32_t specular_block_index;
	uint32_t motion_object_id;
	uint32_t motion_flags;
	uint32_t ao_class;
	uint32_t state_flags2;
	// Vulkan gl_VertexIndex includes firstVertex/baseVertex.  Per-draw payload
	// arrays are local, so the compiler records the draw's index base here.
	uint32_t vertex_index_base;
};

struct alignas(16) GpuMaterial
{
	uint32_t image2d[4];
	uint32_t image2d_array[4];
	uint32_t sampler[4];
	float uv_params[4];
};

struct alignas(16) GpuTransform
{
	float current_model[16];
	float previous_model[16];
};

struct alignas(16) GpuDynamicLight
{
	float position_radius[4];
	float color_falloff[4];
	float direction_dot_range[4];
	float specular_position_radius[4];
	float specular_and_flags[4];
};

struct alignas(16) GpuSpecularDef
{
	float center[4];
	float color[4];
};

struct alignas(16) GpuSpecularBlock
{
	int32_t count;
	int32_t exponent;
	float strength;
	float lightmap_mix;
	float alpha_strength;
	float field_mode;
	float debug_tint;
	float debug_authored;
	GpuSpecularDef sources[kMaxSpecularSources];
};

struct alignas(16) GpuWorldAux
{
	float fog_color[4];
	float fog_plane[4];
	float params[4];
	uint32_t indices[4];
};

struct alignas(16) FrameViewGlobals
{
	float projection[16];
	float view[16];
	float view_projection[16];
	float inverse_modelview[16];
	float inverse_view_projection[16];
	float previous_view_projection[16];
	float cockpit_previous_view_projection[16];
	float viewport_xywh[4];
	float visible_origin_size[4];
	float target_extent_inv_extent[4];
	uint32_t history_target_flags[4];
};

struct alignas(16) WorldBatchPush
{
	uint32_t draw_header_base;
	uint32_t view_record_index;
	uint32_t target_flags;
	uint32_t payload_word_base;
};

enum GpuDrawFlags : uint32_t
{
	kDrawGeometryModeMask = 0x3u,
	kDrawTargetAbsolute = 1u << 2,
	kDrawHasPerspectivePayload = 1u << 3,
	kDrawHasMotionPayload = 1u << 4,
	kDrawHasSpecularPayload = 1u << 5,
	kDrawRawPerPixelSpecularNormal = 1u << 6,
	kDrawPackedPhongDynamicPosition = 1u << 7,
	kDrawHasSoftDepthScalar = 1u << 8,
	kDrawHasCockpitMotion = 1u << 9,
};

enum ShaderFlags : uint32_t
{
	kShaderTextured = 1u << 0,
	kShaderLightmapped = 1u << 1,
	kShaderGenericFog = 1u << 2,
	kShaderPhong = 1u << 3,
	kShaderDynamicLights = 1u << 4,
	kShaderPerPixelSpecular = 1u << 5,
	kShaderSpecularMask = 1u << 6,
	kShaderFieldSpecular = 1u << 7,
	kShaderSoftParticle = 1u << 8,
	kShaderLuminancePostMask = 1u << 9,
	kShaderPostMaskOnly = 1u << 10,
	kShaderMotionWrite = 1u << 11,
	kShaderObjectIdWrite = 1u << 12,
	kShaderCockpit = 1u << 13,
	kShaderTerrain = 1u << 14,
	kShaderSpecialTextureFlatColor = 1u << 15,
	kShaderAoCaptureWeight = 1u << 16,
	kShaderTerrainFogBloomSuppression = 1u << 17,
};

enum StateFlags2 : uint32_t
{
	kStateWrapMask = 0x3u,
	kStateFiltering = 1u << 2,
	kStateMipping = 1u << 3,
	kStatePrimaryPayloadMask = 0x3u << 4,
	kStateFieldMode = 1u << 6,
	kStateSeparateSoftDepthScalar = 1u << 7,
};

enum WorldDescriptorBindings : uint32_t
{
	kSet0FrameViewGlobals = 0,
	kSet0Samplers = 1,
	kSet1FloatImages2D = 0,
	kSet1FloatImageArrays = 1,
	kSet2DrawHeaders = 0,
	kSet2ShaderStates = 1,
	kSet2Materials = 2,
	kSet2Transforms = 3,
	kSet2DynamicLights = 4,
	kSet2SpecularBlocks = 5,
	kSet2OptionalPayloadWords = 6,
	kSet2WorldAux = 7,
};

struct CapturedMaterial
{
	TextureVersionId image2d[4];
	TextureVersionId image2d_array[4];
	uint32_t sampler[4];
	float uv_params[4];
};

struct CapturedTextureVersion
{
	TextureVersionId id;
	RenderFormat format;
	uint32_t width;
	uint32_t height;
	uint32_t depth_or_layers;
	uint32_t mip_count;
	uint32_t handle_generation;
	uint64_t content_serial;
	uint64_t last_use_timeline;
	uint32_t residency;
	PayloadDataId immutable_upload_payload;
};

struct CapturedTransform
{
	float current_model[16];
	float previous_model[16];
};

struct CapturedWorldView
{
	float eye[4];
	float unscaled_view[16];
	float matrix_scale[4];
	float projection[16];
	float view[16];
	float view_projection[16];
	float inverse_modelview[16];
	float inverse_view_projection[16];
	float previous_view_projection[16];
	float cockpit_previous_view_projection[16];
	float window_center_extent[4];
	float anchor_offset[4];
	LogicalRect logical_clip;
	uint32_t finite_far_clip_enabled;
	float finite_far_clip;
	uint32_t custom_plane_enabled;
	float custom_plane[4];
	float custom_plane_point[4];
	float custom_plane_slack;
};

struct CapturedViewport
{
	LogicalRect logical_rect;
	LogicalRect physical_rect;
	uint32_t target_width;
	uint32_t target_height;
	uint32_t ssaa_factor;
	uint32_t scissor_enabled;
};

// Normalized, pointer-free copy of every renderer_preferred_state field.
struct CapturedPreferredState
{
	uint32_t mipping;
	uint32_t filtering;
	uint32_t antialised;
	uint32_t bit_depth;
	float gamma;
	uint32_t width;
	uint32_t height;
	uint32_t window_width;
	uint32_t window_height;
	uint32_t fullscreen;
	uint32_t supersampling_factor;
	uint32_t msaa_samples;
	uint32_t per_pixel_lighting;
	uint32_t bloom_enabled;
	float bloom_threshold;
	float bloom_intensity;
	float bloom_spread;
	uint32_t gtao_enabled;
	uint32_t gtao_resolution;
	uint32_t gtao_sample_count;
	uint32_t gtao_blur_radius;
	float gtao_radius;
	float gtao_intensity;
	float gtao_bias;
	uint32_t gtao_overscan_percent;
	uint32_t gtao_debug_preview;
	float gtao_temporal_blend;
	float gtao_temporal_depth_reject;
	float gtao_temporal_velocity_reject;
	uint32_t gtao_temporal_debug_preview;
	float gtao_terrain_occlusion;
	float gtao_polyobject_occlusion;
	float gtao_mine_rock_occlusion;
	float gtao_mine_occlusion;
	uint32_t motion_vector_mode;
	uint32_t motion_vector_debug_preview;
	float pixel_motion_blur_strength;
	uint32_t combined_motion_blur;
	float combined_motion_blur_legacy_strength;
	float combined_motion_blur_legacy_frame_time;
	float combined_motion_blur_legacy_sphere_size;
	float combined_motion_blur_legacy_copy_density;
	int32_t combined_motion_blur_legacy_max_iterations;
	float combined_motion_blur_legacy_alpha_exponent;
	float pixel_motion_blur_periphery_strength;
	float pixel_motion_blur_legacy_object_strength;
	float pixel_motion_blur_center_suppression;
	float pixel_motion_blur_legacy_object_center_suppression;
	uint32_t pixel_motion_blur_samples;
	float afterburner_fov_multiplier;
	float afterburner_pixel_blur_multiplier;
};

struct CapturedTargetLayout
{
	RenderTargetClass target;
	uint32_t logical_width;
	uint32_t logical_height;
	uint32_t internal_width;
	uint32_t internal_height;
	uint32_t drawable_width;
	uint32_t drawable_height;
	uint32_t ssaa_factor;
	uint32_t msaa_samples;
	uint32_t attachment_mask;
	RenderFormat attachment_formats[6];
	uint32_t feature_flags;
	uint32_t overscan_percent;
	RenderFormat present_format;
};

struct CapturedPostDynamicState
{
	uint32_t paused;
	uint32_t histories_frozen;
	uint32_t captured_world_valid;
	uint32_t captured_depth_valid;
	uint32_t gtao_history_valid;
	uint32_t motion_history_valid;
	uint32_t cockpit_history_valid;
	uint32_t cockpit_deferral_active;
	uint32_t defer_bloom;
	uint32_t motion_consumer_active;
	uint32_t frame_serial;
	float frame_time;
	float afterburner_scalar;
	float visible_origin_size[4];
	float source_destination_extent[4];
};

// Ordered frame/post state referenced only by graph commands.  It is never a
// draw-state or pipeline key.
struct CapturedTargetSignature
{
	TargetLayoutId target_layout; // Scene
	TargetLayoutId post_present_layout;
	TargetLayoutId cockpit_scene_layout;
	CapturedPreferredState preferred;
	CapturedPostDynamicState dynamic;
};

struct CapturedTargetVersion
{
	RenderTargetClass target;
	uint32_t version;
	TargetLayoutId target_layout;
	uint32_t width;
	uint32_t height;
	uint32_t samples;
	uint32_t color_epoch;
	uint32_t depth_epoch;
};

struct CapturedPresentRect
{
	uint32_t drawable_width;
	uint32_t drawable_height;
	LogicalRect rect;
	uint32_t surface_transform;
	uint64_t swapchain_generation;
};

// Logical behavior state captured before descriptor/buffer lowering.  Unlike
// GpuShaderState, it deliberately contains no compiled buffer offsets.
struct CapturedShaderState
{
	uint32_t shader_flags;
	uint32_t texture_type;
	uint32_t overlay_type;
	uint32_t lighting_color_model;
	uint32_t alpha_type;
	uint32_t alpha_value;
	uint32_t blend_class;
	uint32_t draw_classification;
	float alpha_factor;
	float z_bias;
	float fog_near_mapped;
	float fog_far_mapped;
	float flat_color[4];
	float fog_color[4];
	float light_direction[4];
	float post_values[4];
	uint32_t dynamic_light_count;
	uint32_t motion_object_id;
	uint32_t motion_flags;
	uint32_t ao_class;
	uint32_t state_flags2;
	uint32_t reserved0;
};

struct CapturedShaderRasterState
{
	CapturedShaderState shader;
	TargetLayoutId target_layout;
	uint32_t sample_count;
	uint32_t mrt_write_mask;
	RasterFamily raster_family;
	uint32_t cull_enabled;
	uint32_t front_face;
	uint32_t depth_test_enabled;
	uint32_t depth_write_enabled;
	uint32_t depth_compare;
	uint32_t depth_bias_enabled;
	float depth_bias_factor;
	float depth_bias_units;
	ViewportId viewport;
	ViewportId scissor;
};

struct CapturedPayloadRecord
{
	uint32_t byte_offset;
	uint32_t byte_size;
	uint32_t alignment;
	uint32_t semantic;
};

enum CapturedPayloadSemantic : uint32_t
{
	kPayloadPerspectiveVertices = 1,
	kPayloadMotionVertices = 2,
	kPayloadSpecularVertices = 3,
	kPayloadDynamicLights = 4,
	kPayloadSpecularBlock = 5,
	kPayloadWorldAux = 6,
	kPayloadCockpitMotion = 7,
	kPayloadTerrainCells = 8,
	kPayloadTerrainWorkList = 9,
	kPayloadCockpitBacking = 10,
	kPayloadSoftDepthScalar = 11,
	kPayloadGeometryAux = 12,
	kPayloadTextureUpload = 13,
	kPayloadTerrainBatches = 14,
	kPayloadTerrainViewInput = 15,
};

enum CapturedPayloadValidityBits : uint32_t
{
	kPayloadHasPerspectiveVertices = 1u << 0,
	kPayloadHasMotionVertices = 1u << 1,
	kPayloadHasSpecularVertices = 1u << 2,
	kPayloadHasDynamicLights = 1u << 3,
	kPayloadHasSpecularBlock = 1u << 4,
	kPayloadHasWorldAux = 1u << 5,
	kPayloadHasCockpitMotion = 1u << 6,
	kPayloadHasSoftDepthScalar = 1u << 7,
	kPayloadHasGeometryAux = 1u << 8,
	kPayloadHasTerrainCells = 1u << 9,
	kPayloadHasTerrainWorkItems = 1u << 10,
	kPayloadHasTerrainBatches = 1u << 11,
	kPayloadHasTerrainViewInput = 1u << 12,
	kPayloadValidityAll = (1u << 13) - 1,
};

// One draw references this immutable typed binding.  Each member points into
// capture-owned payload bytes, never a backend buffer or descriptor.
struct CapturedPayloadBinding
{
	PayloadDataId perspective_vertices;
	PayloadDataId motion_vertices;
	PayloadDataId specular_vertices;
	PayloadDataId dynamic_lights;
	PayloadDataId specular_block;
	PayloadDataId world_aux;
	PayloadDataId cockpit_motion;
	PayloadDataId soft_depth_scalar;
	PayloadDataId geometry_aux;
	uint32_t validity_flags;
	PayloadDataId terrain_cells;
	PayloadDataId terrain_work_items;
	PayloadDataId terrain_batches;
	PayloadDataId terrain_view_input;
};

struct CapturedCockpitBackingEffect
{
	uint32_t enabled;
	float alpha;
	float darkness;
	uint32_t scanlines_enabled;
	float scanline_strength;
	float scanline_spacing;
	float scanline_thickness;
	float scanline_phase;
};

constexpr CapturedPayloadBinding EmptyPayloadBinding()
{
	return { kInvalidId, kInvalidId, kInvalidId, kInvalidId, kInvalidId,
		kInvalidId, kInvalidId, kInvalidId, kInvalidId, 0, kInvalidId,
		kInvalidId, kInvalidId, kInvalidId };
}

struct DrawClassification
{
	uint32_t category;
	uint32_t category_3d;
	PrimitiveSourceKind source_kind;
	uint32_t flags;
};

struct StreamGeometryRef
{
	Span32 vertices;
	Span32 indices;
	Span32 optional_payload_words;
	DepthInterpretation depth_interpretation;
};

struct CapturedDraw
{
	StreamGeometryRef geometry;
	StateId state;
	TransformId transform;
	MaterialRef material;
	PayloadRef optional_payload;
	DrawClassification classification;
};

// Deterministic semantic replacement for GL4's 216-byte GL_vertices[0].
// It is compatibility scratch only and is never the submitted vertex ABI.
struct LegacyPrimitiveScratch0
{
	float position[3];
	uint32_t rgba8;
	float tex_coord[3];
	float tex_coord2[3];
	float normal[4];
	float motion_world_position[4];
	float motion_previous_world_position[4];
	float field_specular_center[kMaxSpecularSources][4];
	float field_specular_color[kMaxSpecularSources][4];
};

// Complete mutable behavioral state.  Values use explicit scalar storage so
// byte hashing never depends on compiler bool layout or uninitialized padding.
struct LegacyRenderState
{
	uint32_t transform_mode;
	float current_view[16];
	float previous_view[16];
	float current_object[16];
	float previous_object[16];
	float current_submodel[16];
	float previous_submodel[16];

	uint32_t texture_type;
	int32_t bitmap_handle;
	TextureVersionId bitmap_version;
	uint32_t map_type;
	uint32_t overlay_type;
	int32_t overlay_handle;
	TextureVersionId overlay_version;
	TextureVersionId specular_version;

	uint32_t lighting_state;
	uint32_t color_model;
	uint32_t flat_color;
	float per_pixel_direction[4];
	PayloadDataId dynamic_lights;
	uint32_t dynamic_light_count;
	float dynamic_face_normal[4];
	uint32_t specular_mode;
	PayloadDataId specular_block;

	uint32_t alpha_type;
	uint32_t alpha_value;
	float alpha_factor;
	uint32_t blend_class;

	uint32_t fog_enabled;
	uint32_t fog_color;
	float fog_near_mapped;
	float fog_far_mapped;
	PayloadDataId room_or_terrain_fog;

	uint32_t wrap_type;
	uint32_t filtering;
	uint32_t mipping;
	uint32_t sampler_indices[4];

	uint32_t depth_test_enabled;
	uint32_t depth_write_enabled;
	uint32_t depth_compare;
	float reported_near_z;
	float reported_far_z;
	float z_bias;
	float coplanar_factor;
	float coplanar_units;
	uint32_t coplanar_enabled;
	uint32_t cull_enabled;
	uint32_t front_face;

	uint32_t selected_draw_buffers;       // MrtWriteBits; NONE is zero.
	uint32_t attachment_color_masks[5];   // AttachmentChannelBits per location.
	ViewportId viewport;
	ViewportId scissor;

	uint32_t finite_far_clip_enabled;
	float finite_far_clip;
	uint32_t custom_plane_enabled;
	float custom_plane[4];
	float custom_plane_point[4];
	float custom_plane_slack;

	float ao_suppression;
	float bloom_suppression;
	uint32_t ao_class;
	float ao_weight;
	uint32_t luminance_mask;
	uint32_t post_mask_only;

	uint32_t soft_particle_enabled;
	uint32_t depth_epoch;
	uint32_t motion_object_active;
	uint32_t motion_object_id;
	uint32_t motion_flags;
	uint32_t motion_capture_locked;
	uint32_t motion_suppression_depth;

	uint32_t cockpit_scene_active;
	PayloadDataId cockpit_backing_state;
	DrawClassification draw_classification;
	float gamma;
};

struct LegacyTextureUnitBinding
{
	int32_t logical_handle;
	TextureVersionId version;
	uint32_t map_type;
	uint32_t sampler_index;
};

struct LegacyTextureBindingShadow
{
	LegacyTextureUnitBinding units[4];
	uint32_t last_selected_unit;
};

enum class CaptureCommandType : uint32_t
{
	BeginFrameTarget = 0,
	ClearColor = 1,
	ClearDepth = 2,
	ClearAlphaOnly = 3,
	DrawStream = 4,
	DrawRetained = 5,
	EnqueueFontGlyph = 6,
	FlushFontBatches = 7,
	AcquireSoftDepth = 8,
	CaptureBloomSource = 9,
	BeginPostPresent = 10,
	BeginCockpitScene = 11,
	EndCockpitScene = 12,
	PerfMarker = 13,
	ReadPixel = 14,
	ReadImage = 15,
	EndFrame = 16,
	Present = 17,
	Count = 18,
};

struct BeginFrameTargetCommand
{
	RenderTargetClass target;
	LogicalRect logical_clip;
	ViewportId physical_viewport;
	uint32_t clear_flags;
	ViewStateId view_state;
	TargetVersionId active_target_version;
};

struct ClearColorCommand
{
	RenderTargetClass target;
	LogicalRect rect;
	uint32_t whole_target;
	float rgba[4];
	uint32_t selected_attachments;
	uint32_t attachment_channel_masks[5];
};

struct ClearDepthCommand
{
	RenderTargetClass target;
	LogicalRect rect;
	uint32_t whole_target;
	float depth;
};

struct ClearAlphaOnlyCommand
{
	ImageSemantic image;
	LogicalRect rect;
	uint32_t whole_target;
	float alpha;
};

struct DrawStreamCommand
{
	StreamGeometryRef geometry;
	StateId state;
	TransformId transform;
	MaterialRef material;
	PayloadRef optional_payload;
	// The view is draw-owned. A later g3_StartFrame (for example a HUD pass)
	// must not retroactively change already captured world geometry.
	ViewStateId view;
	DrawClassification classification;
};

struct DrawRetainedCommand
{
	MeshHandle mesh;
	uint32_t first_index;
	uint32_t index_count;
	int32_t base_vertex;
	GeometryMode geometry_mode;
	// Persistent typed ranges returned by IRetainedWorld::ResolveFace.  These
	// are T1 geometry identity and cannot be reconstructed from indices.  T2
	// uses the canonical nonindexed zero form and leaves all three spans empty.
	Span32 perspective_payload;
	Span32 motion_payload;
	Span32 specular_payload;
	StateId state;
	TransformId transform;
	MaterialRef material;
	PayloadRef optional_payload;
	ViewStateId view;
	DrawClassification classification;
};

struct EnqueueFontGlyphCommand
{
	TextureVersionId texture_version;
	uint32_t texture_layer;
	BaseVertex vertices[6];
	uint32_t rgba8;
	float alpha;
	uint32_t bucket;
	uint32_t enqueue_serial;
};

struct FlushFontBatchesCommand
{
	RenderTargetClass target;
	ViewStateId view_state;
	uint32_t flush_serial;
};

struct AcquireSoftDepthCommand
{
	TargetVersionId scene_target_version;
	uint32_t depth_epoch;
	uint32_t snapshot_id;
};

struct CaptureBloomSourceCommand
{
	TargetVersionId scene_target_version;
	ViewStateId projection;
	ViewStateId view_projection;
	ViewStateId inverse_modelview;
	LogicalRect visible_rect;
};

struct BeginPostPresentCommand
{
	uint32_t defer_bloom;
	RenderTargetSignatureId signature;
};

struct BeginCockpitSceneCommand
{
	LogicalRect logical_rect;
	PayloadDataId backing_effect_state;
	uint32_t capture_serial;
};

struct EndCockpitSceneCommand
{
	uint32_t capture_serial;
};

struct PerfMarkerCommand
{
	uint32_t gpu_scene_mark;
	uint32_t nesting_serial;
};

struct ReadPixelCommand
{
	ImageSemantic source;
	int32_t x;
	int32_t y;
	ReadbackFormat format;
	ReadbackRequestId request;
};

struct ReadImageCommand
{
	ImageSemantic source;
	LogicalRect rect;
	ReadbackRowOrder row_order;
	ReadbackFormat format;
	ReadbackRequestId request;
};

struct EndFrameCommand
{
	uint32_t view_interval_serial;
};

struct PresentCommand
{
	uint32_t presented_frame_serial;
	WsiSignatureId window_swapchain_signature;
	PresentRectId present_rect;
};

union CaptureCommandPayload
{
	BeginFrameTargetCommand begin_frame_target;
	ClearColorCommand clear_color;
	ClearDepthCommand clear_depth;
	ClearAlphaOnlyCommand clear_alpha_only;
	DrawStreamCommand draw_stream;
	DrawRetainedCommand draw_retained;
	EnqueueFontGlyphCommand enqueue_font_glyph;
	FlushFontBatchesCommand flush_font_batches;
	AcquireSoftDepthCommand acquire_soft_depth;
	CaptureBloomSourceCommand capture_bloom_source;
	BeginPostPresentCommand begin_post_present;
	BeginCockpitSceneCommand begin_cockpit_scene;
	EndCockpitSceneCommand end_cockpit_scene;
	PerfMarkerCommand perf_marker;
	ReadPixelCommand read_pixel;
	ReadImageCommand read_image;
	EndFrameCommand end_frame;
	PresentCommand present;
};

struct CaptureCommand
{
	CaptureCommandType type;
	uint32_t schema_version;
	uint64_t serial;
	CaptureCommandPayload payload;
};

enum class GraphDomain : uint32_t
{
	Scene = 0,
	Resolve,
	PostAuthored,
	PostGammaEncoded,
	Cockpit,
	Present,
};

enum class GraphNodeId : uint32_t
{
	CapWorld = 0,
	CapDepthLogical,
	ResolveColor,
	ResolveDepth,
	ResolveVelocity,
	ResolveObjectId,
	ResolveProtectionMask,
	ResolveAoClass,
	Ssaa4To2,
	Ssaa2To1,
	PrepareDepthLogical,
	AoDepth,
	AoRaw,
	AoBlurX,
	AoBlurY,
	AoTemporal,
	AoSuppress,
	AoApply,
	AoDeferredComposite,
	BloomThreshold,
	BloomDown,
	BloomUp,
	NormalComposite,
	NormalBlit,
	MotionNormal,
	MotionDebugNormal,
	NormalUi,
	CockpitLinearCopy,
	MotionCockpitPre,
	MotionDebugCockpitPre,
	CockpitUiPre,
	CockpitScene,
	PostAlphaClear,
	CockpitResolve,
	BloomDeferred,
	CockpitOver,
	CockpitBloomGamma,
	CockpitGammaOnly,
	CockpitUiPost,
	Present,
	Count,
};

struct GraphNodeManifestEntry
{
	GraphNodeId id;
	const char *symbolic_name;
	const char *diagnostic_name;
	GraphDomain domain;
	uint32_t loads_or_composites;
};

extern const GraphNodeManifestEntry kFrozenGraphManifest[];
extern const size_t kFrozenGraphManifestCount;

enum class AttachmentBlendMode : uint32_t
{
	LegacyColorByDraw = 0,
	Replace,
	ComponentMax,
	Depth,
};

enum class BlendFactorContract : uint32_t
{
	Zero = 0,
	One,
	SourceAlpha,
	OneMinusSourceAlpha,
	DestinationColor,
};

enum class BlendOperationContract : uint32_t
{
	Add = 0,
	Maximum,
};

enum class ResolveRule : uint32_t
{
	AverageSamples = 0,
	SampleZero,
	NearestSpatialSampleZero,
	NotApplicable,
};

struct BlendClassContract
{
	BlendClass blend_class;
	uint32_t blend_enabled;
	BlendFactorContract source_rgb;
	BlendFactorContract destination_rgb;
	BlendFactorContract source_alpha;
	BlendFactorContract destination_alpha;
};

extern const BlendClassContract kBlendClassContract[];
extern const size_t kBlendClassContractCount;

struct SceneAttachmentContract
{
	uint32_t location;
	ImageSemantic semantic;
	RenderFormat format;
	uint32_t is_depth;
	uint32_t integer_attachment;
	AttachmentBlendMode blend_mode;
	BlendFactorContract source_factor;
	BlendFactorContract destination_factor;
	BlendOperationContract blend_operation;
	ResolveRule resolve_rule;
	float clear_float[4];
	uint32_t clear_uint[4];
};

extern const SceneAttachmentContract kSceneAttachmentContract[];
extern const size_t kSceneAttachmentContractCount;

enum StartFrameResetBits : uint32_t
{
	kResetAoSuppression = 1u << 0,
	kResetBloomSuppression = 1u << 1,
	kResetAoClass = 1u << 2,
	kResetAoWeight = 1u << 3,
	kEnableDepthWrites = 1u << 4,
	kMarkSceneDirty = 1u << 5,
	kInvalidateSoftDepth = 1u << 6,
	kClearProtectionAndAoClassOnce = 1u << 7,
	kClearMotionAndObjectIdOnceIfUnfrozen = 1u << 8,
	kColorAttachmentOnly = 1u << 9,
	kFlushFontsBeforeRoute = 1u << 10,
	kApplyRequestedClears = 1u << 11,
	kRestoreDefaultSceneDrawBuffers = 1u << 12,
	kUseTargetMsaaState = 1u << 13,
	kDisableMsaa = 1u << 14,
	kUnconditionalTransparentColorAndDepthClear = 1u << 15,
	kUpdateClipProjectionViewport = 1u << 16,
	kPreserveUnlistedStickyState = 1u << 17,
};

struct StartFrameResetContract
{
	RenderTargetClass target;
	uint32_t reset_bits;
	float ao_suppression;
	float bloom_suppression;
	uint32_t ao_class;
	float ao_weight;
	uint32_t depth_writes_enabled;
	uint32_t selected_attachments;
};

extern const StartFrameResetContract kStartFrameResetContract[];
extern const size_t kStartFrameResetContractCount;

// Exact GL4-equivalent scalar helpers used by both capture tests and shaders.
float GL4DepthFromEyeZ(float eye_z);
uint32_t NormalizeRequestedMsaa(uint32_t msaa_samples, bool legacy_antialias);
uint32_t NormalizeOverscanPercent(const CapturedPreferredState &state);
bool WantsMotionResources(const CapturedPreferredState &state);
bool IsLegalMrtWriteMask(uint32_t mask);
bool IsCaptureCommandTypeValid(CaptureCommandType type);

static_assert(sizeof(BaseVertex) == 32, "BaseVertex ABI must remain 32 bytes");
static_assert(sizeof(PerspectiveVertexPayload) == 16, "Perspective payload ABI changed");
static_assert(sizeof(MotionVertexPayload) == 32, "Motion payload ABI changed");
static_assert(sizeof(SpecularVertexPayload) == 144, "Specular payload ABI changed");
static_assert(sizeof(TerrainEmitterCell) == 32, "TerrainEmitterCell ABI changed");
static_assert(sizeof(TerrainWorkItem) == 16, "TerrainWorkItem ABI changed");
static_assert(sizeof(TerrainBatchInput) == 32, "TerrainBatchInput ABI changed");
static_assert(sizeof(TerrainViewInput) == 112, "TerrainViewInput ABI changed");
static_assert(std::is_trivially_copyable<TerrainEmitterCell>::value &&
	std::is_trivially_copyable<TerrainWorkItem>::value &&
	std::is_trivially_copyable<TerrainBatchInput>::value &&
	std::is_trivially_copyable<TerrainViewInput>::value,
	"T2 retained inputs must remain capture-copyable PODs");
static_assert(sizeof(TerrainVertexPayload) == 32, "Terrain payload ABI changed");
static_assert(sizeof(CapturedPayloadBinding) == 56, "CapturedPayloadBinding ABI changed");
static_assert(offsetof(CapturedPayloadBinding, validity_flags) == 36,
	"CapturedPayloadBinding validity offset changed");
static_assert(offsetof(CapturedPayloadBinding, terrain_cells) == 40,
	"CapturedPayloadBinding T2 offset changed");
static_assert(offsetof(CapturedPayloadBinding, terrain_view_input) == 52,
	"CapturedPayloadBinding T2 tail changed");
static_assert(kPayloadTerrainCells == 8 && kPayloadTerrainWorkList == 9 &&
	kPayloadTerrainBatches == 14 && kPayloadTerrainViewInput == 15,
	"T2 payload semantic IDs changed");
static_assert(kPayloadHasTerrainCells == (1u << 9) &&
	kPayloadHasTerrainWorkItems == (1u << 10) &&
	kPayloadHasTerrainBatches == (1u << 11) &&
	kPayloadHasTerrainViewInput == (1u << 12),
	"T2 payload validity bits changed");
static_assert(sizeof(GpuDrawHeader) == 32, "GpuDrawHeader ABI changed");
static_assert(sizeof(GpuShaderState) == 144, "GpuShaderState ABI changed");
static_assert(sizeof(GpuMaterial) == 64, "GpuMaterial ABI changed");
static_assert(sizeof(GpuTransform) == 128, "GpuTransform ABI changed");
static_assert(sizeof(GpuDynamicLight) == 80, "GpuDynamicLight ABI changed");
static_assert(sizeof(GpuSpecularDef) == 32, "GpuSpecularDef ABI changed");
static_assert(sizeof(GpuSpecularBlock) == 160, "GpuSpecularBlock ABI changed");
static_assert(sizeof(GpuWorldAux) == 64, "GpuWorldAux ABI changed");
static_assert(sizeof(FrameViewGlobals) == 512, "FrameViewGlobals ABI changed");
static_assert(sizeof(WorldBatchPush) == 16, "WorldBatchPush ABI changed");
static_assert(sizeof(LegacyPrimitiveScratch0) == 216,
	"LegacyPrimitiveScratch0 must mirror GL4 GL_vertices[0] semantics");
static_assert(std::is_trivially_copyable<CaptureCommand>::value,
	"Capture commands must be immutable POD trace records");
static_assert(static_cast<uint32_t>(CaptureCommandType::Count) == 18,
	"Public capture schema changed without a version bump");
static_assert((kDrawTargetAbsolute | kDrawHasPerspectivePayload |
	kDrawHasMotionPayload | kDrawHasSpecularPayload |
	kDrawRawPerPixelSpecularNormal | kDrawPackedPhongDynamicPosition |
	kDrawHasSoftDepthScalar | kDrawHasCockpitMotion) < (1u << 10),
	"GpuDrawHeader ABI reserves bits 10-31");
static_assert(kShaderTerrainFogBloomSuppression == (1u << 17),
	"GpuShaderState shader flag ABI changed");
static_assert(kStateSeparateSoftDepthScalar == (1u << 7),
	"GpuShaderState state_flags2 ABI changed");

} // namespace render
} // namespace piccu
