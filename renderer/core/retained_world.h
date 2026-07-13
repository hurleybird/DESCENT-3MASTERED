/* Backend-neutral retained room, terrain, and polymodel bridge. */
#pragma once

#include "render_contract.h"

namespace piccu
{
namespace render
{

enum class RetainedSourceKind : uint32_t
{
	Room = 0,
	Terrain = 1,
	Polymodel = 2,
};

struct RetainedSourceKey
{
	RetainedSourceKind kind;
	uint32_t source_id;
	uint32_t source_generation;
};

struct RetainedFaceToken
{
	RetainedSourceKey source;
	uint32_t subobject;
	uint32_t face;
	uint32_t classification;
};

struct RetainedRange
{
	MeshHandle mesh;
	uint32_t first_index;
	uint32_t index_count;
	int32_t base_vertex;
	Span32 perspective_payload;
	Span32 motion_payload;
	Span32 specular_payload;
};

struct RetainedMeshUpload
{
	RetainedSourceKey source;
	const BaseVertex *vertices;
	uint32_t vertex_count;
	const uint32_t *indices;
	uint32_t index_count;
	const PerspectiveVertexPayload *perspective_payload;
	uint32_t perspective_payload_count;
	const MotionVertexPayload *motion_payload;
	uint32_t motion_payload_count;
	const SpecularVertexPayload *specular_payload;
	uint32_t specular_payload_count;
};

struct RetainedFaceRangeUpload
{
	RetainedFaceToken token;
	uint32_t first_index;
	uint32_t index_count;
	int32_t base_vertex;
	Span32 perspective_payload;
	Span32 motion_payload;
	Span32 specular_payload;
};

struct TerrainIndirectCommand
{
	uint32_t vertex_count;
	uint32_t instance_count;
	uint32_t first_vertex;
	uint32_t first_instance;
};

struct RetainedTerrainUpload
{
	RetainedSourceKey source;
	const TerrainEmitterCell *cells;
	uint32_t cell_count;
	const TerrainWorkItem *full_draw_work;
	uint32_t full_draw_work_count;
	const TerrainBatchInput *batches;
	uint32_t batch_count;
	TextureVersionId base_texture_array;
	TextureVersionId lightmap_array;
};

enum TerrainClipPlaneBits : uint32_t
{
	kTerrainClipLeft = 1u << 0,
	kTerrainClipRight = 1u << 1,
	kTerrainClipBottom = 1u << 2,
	kTerrainClipTop = 1u << 3,
	kTerrainClipBehind = 1u << 7,
};

struct TerrainEmitterContract
{
	uint32_t classify_group_size;
	uint32_t scan_group_size;
	uint32_t emit_group_size;
	uint32_t clip_plane_order[4];
	uint32_t stable_full_draw_prefix_order;
	uint32_t full_work_sorted_source_texture_lightmap_segment;
	uint32_t batch_breaks_only_on_source_texture;
	uint32_t batch_index_equals_texture_layer;
	uint32_t lightmap_page_is_per_vertex;
	uint32_t pre_clip_facing_test;
	uint32_t reject_behind_after_side_clips;
	uint32_t hardcoded_white_rgba8;
	float minimum_eye_z;
};

constexpr TerrainEmitterContract kTerrainEmitterContract = {
	256, 256, 256,
	{ kTerrainClipLeft, kTerrainClipRight, kTerrainClipBottom, kTerrainClipTop },
	1, 1, 1, 1, 1, 1, 1, 0xffffffffu, 0.000001f
};

struct TerrainMaterialContract
{
	uint32_t color_model_rgb;
	uint32_t texture_type_linear;
	uint32_t alpha_type_constant_texture;
	uint32_t alpha_value;
	uint32_t lighting_none;
	uint32_t wrap_repeat;
	BlendClass blend_class;
	uint32_t ao_class_encoded_value;
};

// Numeric fields mirror CM_RGB=1, TT_LINEAR=1, AT_CONSTANT_TEXTURE=3,
// LS_NONE=0, WT_WRAP=0 in the public renderer ABI.
constexpr TerrainMaterialContract kTerrainMaterialContract =
	{ 1, 1, 3, 255, 0, 0, BlendClass::Alpha, 1 };

// Implementations synchronously deep-copy every upload input.  No caller
// pointer remains live after the method returns.
class IRetainedWorld
{
public:
	virtual ~IRetainedWorld() = default;

	virtual bool CreateMesh(const RetainedMeshUpload &upload,
		const RetainedFaceRangeUpload *face_ranges, uint32_t face_range_count,
		MeshHandle *out_handle) = 0;
	virtual bool ReplaceMesh(MeshHandle old_handle,
		const RetainedMeshUpload &upload,
		const RetainedFaceRangeUpload *face_ranges, uint32_t face_range_count,
		MeshHandle *out_handle) = 0;
	virtual bool ResolveFace(const RetainedFaceToken &token,
		RetainedRange *out_range) const = 0;
	virtual bool CreateTerrain(const RetainedTerrainUpload &upload,
		MeshHandle *out_handle) = 0;
	virtual bool ReplaceTerrain(MeshHandle old_handle,
		const RetainedTerrainUpload &upload, MeshHandle *out_handle) = 0;
	virtual void ReleaseMesh(MeshHandle handle) = 0;
	virtual void ReleaseSource(const RetainedSourceKey &source) = 0;
	virtual void ResetAll() = 0;
};

} // namespace render
} // namespace piccu

static_assert(sizeof(piccu::render::TerrainEmitterCell) == 32,
	"TerrainEmitterCell must match the frozen std430 input stride");
static_assert(offsetof(piccu::render::TerrainEmitterCell, packed) == 0,
	"TerrainEmitterCell packed offset");
static_assert(offsetof(piccu::render::TerrainEmitterCell, height) == 16,
	"TerrainEmitterCell height offset");
static_assert(sizeof(piccu::render::TerrainWorkItem) == 16,
	"TerrainWorkItem ABI changed");
static_assert(offsetof(piccu::render::TerrainWorkItem, source_texture) == 4,
	"TerrainWorkItem source texture offset");
static_assert(sizeof(piccu::render::TerrainBatchInput) == 32,
	"TerrainBatchInput ABI changed");
static_assert(offsetof(piccu::render::TerrainBatchInput, first_work_item) == 8,
	"TerrainBatchInput work offset");
static_assert(offsetof(piccu::render::TerrainBatchInput, first_output_vertex) == 16,
	"TerrainBatchInput output offset");
static_assert(sizeof(piccu::render::TerrainViewInput) == 112,
	"TerrainViewInput ABI changed");
static_assert(offsetof(piccu::render::TerrainViewInput, terrain_x_step) == 16,
	"TerrainViewInput X step offset");
static_assert(offsetof(piccu::render::TerrainViewInput, terrain_z_step) == 32,
	"TerrainViewInput Z step offset");
static_assert(offsetof(piccu::render::TerrainViewInput, terrain_y_step) == 48,
	"TerrainViewInput Y step offset");
static_assert(sizeof(piccu::render::TerrainIndirectCommand) == 16,
	"Terrain indirect command ABI changed");
