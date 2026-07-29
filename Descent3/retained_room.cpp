#include "retained_room.h"

#include "3d.h"
#include "room.h"
#include "render.h"
#include "../renderer/HardwareInternal.h"
#include "../renderer/gl_mesh.h"
#include "renderer.h"
#include "lightmap.h"
#include "lightmap_info.h"
#include "terrain.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <vector>

struct RetainedRoomFace
{
	ElementRange index_range;
	ElementRange reflected_index_range;
	uint32_t first_vertex = 0;
	uint32_t vertex_count = 0;
};

struct RetainedRoomCache
{
	VertexBuffer vertices{ false, false, VertexBufferLayout::RetainedPolymodel };
	VertexBuffer specular_vertices{ false, false, VertexBufferLayout::RetainedRoomSpecular };
	IndexBuffer indices{ false, false };
	std::vector<RetainedRoomFace> faces;
	std::vector<RendVertex> cpu_vertices;
	uint32_t renderer_generation = 0;
	int num_vertices = 0;
	int num_faces = 0;
	const vector* room_vertices = nullptr;
	const face* room_faces = nullptr;
	bool built = false;
	bool specular_built = false;
};

struct RetainedRoomDeformation
{
	uint32_t seed = 0;
	vector direction = {};
	float range = 0.0f;
	bool enabled = false;
};

struct RetainedRoomTransform
{
	float matrix[16] = {};
	bool enabled = false;
};

static std::array<std::unique_ptr<RetainedRoomCache>, MAX_ROOMS> Retained_room_caches;
static std::array<RetainedRoomDeformation, MAX_ROOMS> Retained_room_deformations;
static std::array<RetainedRoomTransform, MAX_ROOMS> Retained_room_transforms;
static std::vector<int> Retained_room_lightmap_handles;
static uint32_t Retained_room_lightmap_attempt_generation = 0;

static void SetIdentity(float matrix[16])
{
	for (int i = 0; i < 16; i++)
		matrix[i] = 0.0f;
	matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0f;
}

void RetainedRoomInvalidateAll()
{
	for (int i = 0; i < MAX_ROOMS; i++)
	{
		std::unique_ptr<RetainedRoomCache>& cache = Retained_room_caches[i];
		Retained_room_deformations[i] = {};
		Retained_room_transforms[i] = {};
		if (!cache)
			continue;
		if (cache->built && cache->renderer_generation == rend_GetGeneration())
		{
			cache->vertices.Destroy();
			if (cache->specular_built)
				cache->specular_vertices.Destroy();
			cache->indices.Destroy();
		}
		else
		{
			cache->vertices.Invalidate();
			cache->specular_vertices.Invalidate();
			cache->indices.Invalidate();
		}
		cache.reset();
	}
}

void RetainedRoomSetDeformation(room* rp, unsigned int seed,
	const vector* direction, float range)
{
	const int roomnum = rp ? (int)(rp - Rooms) : -1;
	if (roomnum < 0 || roomnum >= MAX_ROOMS || !direction)
		return;
	RetainedRoomDeformation& deformation = Retained_room_deformations[roomnum];
	deformation.seed = seed;
	deformation.direction = *direction;
	deformation.range = range;
	deformation.enabled = true;
}

void RetainedRoomClearDeformation(room* rp)
{
	const int roomnum = rp ? (int)(rp - Rooms) : -1;
	if (roomnum >= 0 && roomnum < MAX_ROOMS)
		Retained_room_deformations[roomnum] = {};
}

void RetainedRoomSetTransform(room* rp, const float transform[16])
{
	const int roomnum = rp ? (int)(rp - Rooms) : -1;
	if (roomnum < 0 || roomnum >= MAX_ROOMS || !transform)
		return;
	memcpy(Retained_room_transforms[roomnum].matrix, transform,
		sizeof(Retained_room_transforms[roomnum].matrix));
	Retained_room_transforms[roomnum].enabled = true;
}

void RetainedRoomClearTransform(room* rp)
{
	const int roomnum = rp ? (int)(rp - Rooms) : -1;
	if (roomnum >= 0 && roomnum < MAX_ROOMS)
		Retained_room_transforms[roomnum] = {};
}

static void TransformDirection(const float matrix[16], const vector& input,
	vector& output)
{
	output.x = matrix[0] * input.x + matrix[4] * input.y + matrix[8] * input.z;
	output.y = matrix[1] * input.x + matrix[5] * input.y + matrix[9] * input.z;
	output.z = matrix[2] * input.x + matrix[6] * input.y + matrix[10] * input.z;
}

static void ApplyRetainedRoomTransform(room* rp,
	renderer_retained_polymodel_draw& draw)
{
	const int roomnum = rp ? (int)(rp - Rooms) : -1;
	if (roomnum < 0 || roomnum >= MAX_ROOMS)
		return;
	const RetainedRoomTransform& transform = Retained_room_transforms[roomnum];
	if (!transform.enabled)
		return;
	float transform_matrix[16];
	memcpy(transform_matrix, transform.matrix, sizeof(transform_matrix));
	g3_Mat4Multiply(draw.transform, transform_matrix);
	g3_Mat4Multiply(draw.modelview, transform_matrix);
	memcpy(draw.current_world, transform.matrix, sizeof(draw.current_world));
	memcpy(draw.previous_world, transform.matrix, sizeof(draw.previous_world));
}

static void ApplyRetainedRoomDeformation(room* rp,
	renderer_retained_polymodel_draw& draw)
{
	const int roomnum = rp ? (int)(rp - Rooms) : -1;
	if (roomnum < 0 || roomnum >= MAX_ROOMS)
		return;
	const RetainedRoomDeformation& deformation = Retained_room_deformations[roomnum];
	if (!deformation.enabled)
		return;
	draw.deform_enabled = true;
	draw.deform_mode = 2;
	draw.deform_seed = deformation.seed;
	draw.deform_range = deformation.range;
	vector direction = deformation.direction;
	const RetainedRoomTransform& transform = Retained_room_transforms[roomnum];
	if (transform.enabled)
	{
		// Deformation is additive after the CPU reflection. The retained shader
		// applies it before the reflection matrix, so reflect the direction once
		// here (a reflection is its own inverse) to preserve that ordering.
		TransformDirection(transform.matrix, deformation.direction, direction);
	}
	draw.deform_direction[0] = direction.x;
	draw.deform_direction[1] = direction.y;
	draw.deform_direction[2] = direction.z;
}

static void ApplyRetainedRoomClipping(int clip_codes,
	renderer_retained_polymodel_draw& draw)
{
	draw.near_clip_enabled = (clip_codes & CC_BEHIND) != 0;
	draw.far_clip_enabled = (clip_codes & CC_OFF_FAR) != 0;
	draw.far_clip_z = Far_clip_z;
	draw.custom_clip_enabled = (clip_codes & CC_OFF_CUSTOM) != 0 && Clip_custom != 0;
	if (!draw.custom_clip_enabled)
		return;
	draw.custom_clip_point[0] = Clip_plane_point.x;
	draw.custom_clip_point[1] = Clip_plane_point.y;
	draw.custom_clip_point[2] = Clip_plane_point.z;
	draw.custom_clip_plane[0] = Clip_plane.x;
	draw.custom_clip_plane[1] = Clip_plane.y;
	draw.custom_clip_plane[2] = Clip_plane.z;
	draw.custom_clip_scale[0] = Matrix_scale.x;
	draw.custom_clip_scale[1] = Matrix_scale.y;
	draw.custom_clip_scale[2] = Matrix_scale.z;
}

static void ApplyRetainedRoomLegacyProjection(
	renderer_retained_polymodel_draw& draw)
{
	// Reproduce g3's view transform and biased depth contract in the retained
	// shader. The shader keeps eye-space Z in homogeneous W so clipping remains
	// coherent instead of switching near-camera faces to a CPU-projected stream.
	draw.legacy_world_projection = true;
	draw.legacy_view_position[0] = View_position.x;
	draw.legacy_view_position[1] = View_position.y;
	draw.legacy_view_position[2] = View_position.z;
	draw.legacy_view_right[0] = View_matrix.rvec.x;
	draw.legacy_view_right[1] = View_matrix.rvec.y;
	draw.legacy_view_right[2] = View_matrix.rvec.z;
	draw.legacy_view_up[0] = View_matrix.uvec.x;
	draw.legacy_view_up[1] = View_matrix.uvec.y;
	draw.legacy_view_up[2] = View_matrix.uvec.z;
	draw.legacy_view_forward[0] = View_matrix.fvec.x;
	draw.legacy_view_forward[1] = View_matrix.fvec.y;
	draw.legacy_view_forward[2] = View_matrix.fvec.z;
	draw.legacy_viewport_scale[0] = Window_width > 0 ?
		(2.0f * Window_w2) / (float)Window_width : 1.0f;
	draw.legacy_viewport_scale[1] = Window_height > 0 ?
		(2.0f * Window_h2) / (float)Window_height : 1.0f;
	draw.legacy_viewport_center[0] = Window_width > 0 ?
		(2.0f * Window_cx) / (float)Window_width - 1.0f : 0.0f;
	draw.legacy_viewport_center[1] = Window_height > 0 ?
		1.0f - (2.0f * Window_cy) / (float)Window_height : 0.0f;
}

static void RegisterRetainedRoomReleaseCallback()
{
	static bool registered = false;
	if (!registered)
	{
		rend_RegisterResourceReleaseCallback(RetainedRoomInvalidateAll);
		registered = true;
	}
}

static bool BuildRetainedRoom(room* rp, RetainedRoomCache& cache)
{
	if (!rp || !rp->used || !rp->verts || !rp->faces || rp->num_faces <= 0)
		return false;

	std::vector<RendVertex> vertices;
	std::vector<uint32_t> indices;
	cache.faces.clear();
	cache.faces.resize(rp->num_faces);

	for (int facenum = 0; facenum < rp->num_faces; facenum++)
	{
		face* fp = &rp->faces[facenum];
		if (fp->num_verts < 3 || !fp->face_verts || !fp->face_uvls)
			continue;

		const uint32_t base_vertex = (uint32_t)vertices.size();
		for (int corner = 0; corner < fp->num_verts; corner++)
		{
			const int vertex_num = fp->face_verts[corner];
			if (vertex_num < 0 || vertex_num >= rp->num_verts)
				return false;
			const roomUVL& uv = fp->face_uvls[corner];
			RendVertex vertex = {};
			vertex.position = rp->verts[vertex_num];
			vertex.normal = fp->normal;
			vertex.face_normal = fp->normal;
			vertex.r = vertex.g = vertex.b = vertex.a = 255;
			vertex.r = (ubyte)RoomFaceAOClass(rp, fp);
			vertex.u1 = uv.u;
			vertex.v1 = uv.v;
			vertex.lmpage = -1;
			vertex.u2 = uv.u2;
			vertex.v2 = uv.v2;
			if (RoomFaceUsesLightmap(fp))
			{
				const int lightmap_handle = LightmapInfo[fp->lmi_handle].lm_handle;
				if (lightmap_handle >= 0 && lightmap_handle < MAX_LIGHTMAPS &&
					GameLightmaps[lightmap_handle].square_res > 0)
				{
					vertex.lmpage =
						rend_GetRetainedRoomLightmapPage(lightmap_handle);
					vertex.u2 *= (float)GameLightmaps[lightmap_handle].width /
						GameLightmaps[lightmap_handle].square_res;
					vertex.v2 *= (float)GameLightmaps[lightmap_handle].height /
						GameLightmaps[lightmap_handle].square_res;
				}
			}
			const int retained_lmi = (fp->flags & FF_LIGHTMAP) ?
				fp->lmi_handle : 0xffff;
			vertex.source_vertex = (int)(((uint32_t)retained_lmi << 16) |
				((uint32_t)vertex_num & 0xffffu));
			vertices.push_back(vertex);
		}

		const uint32_t first_index = (uint32_t)indices.size();
		for (int triangle = 0; triangle < fp->num_verts - 2; triangle++)
		{
			indices.push_back(base_vertex);
			indices.push_back(base_vertex + triangle + 1);
			indices.push_back(base_vertex + triangle + 2);
		}
		cache.faces[facenum].first_vertex = base_vertex;
		cache.faces[facenum].index_range =
			ElementRange(first_index, (uint32_t)indices.size() - first_index);
		const uint32_t first_reflected_index = (uint32_t)indices.size();
		for (int triangle = 0; triangle < fp->num_verts - 2; triangle++)
		{
			indices.push_back(base_vertex + fp->num_verts - 1);
			indices.push_back(base_vertex + fp->num_verts - 2 - triangle);
			indices.push_back(base_vertex + fp->num_verts - 3 - triangle);
		}
		cache.faces[facenum].reflected_index_range = ElementRange(
			first_reflected_index, (uint32_t)indices.size() - first_reflected_index);
		cache.faces[facenum].vertex_count = fp->num_verts;
	}

	if (vertices.empty() || indices.empty())
		return false;
	cache.vertices.Initialize((uint32_t)vertices.size(),
		(uint32_t)(vertices.size() * sizeof(RendVertex)), vertices.data());
	cache.indices.Initialize((uint32_t)indices.size(),
		(uint32_t)(indices.size() * sizeof(uint32_t)), indices.data());
	cache.cpu_vertices = std::move(vertices);
	cache.renderer_generation = rend_GetGeneration();
	cache.num_vertices = rp->num_verts;
	cache.num_faces = rp->num_faces;
	cache.room_vertices = rp->verts;
	cache.room_faces = rp->faces;
	cache.built = true;
	cache.specular_built = false;
	return true;
}

static void AppendRetainedRoomFaceRanges(room* rp, const RetainedRoomCache& cache,
	int facenum, std::vector<ElementRange>& ranges, int& vertex_count)
{
	const RetainedRoomFace& retained_face = cache.faces[facenum];
	const int roomnum = (int)(rp - Rooms);
	const bool reflected = roomnum >= 0 && roomnum < MAX_ROOMS &&
		Retained_room_transforms[roomnum].enabled;
	const ElementRange& face_range = reflected ?
		retained_face.reflected_index_range : retained_face.index_range;
	ranges.push_back(face_range);
	vertex_count += retained_face.vertex_count;
}

static VertexBuffer* GetRetainedRoomSpecularVertices(room* rp,
	RetainedRoomCache& cache)
{
	if (cache.specular_built)
		return &cache.specular_vertices;
	if (cache.cpu_vertices.empty())
		return nullptr;

	std::vector<RetainedRoomSpecularVertex> vertices(cache.cpu_vertices.size());
	for (size_t i = 0; i < cache.cpu_vertices.size(); i++)
		vertices[i].base = cache.cpu_vertices[i];
	for (int facenum = 0; facenum < (int)cache.faces.size(); facenum++)
	{
		const RetainedRoomFace& retained_face = cache.faces[facenum];
		if (retained_face.vertex_count == 0)
			continue;
		PopulateRetainedRoomSpecularVertices(rp, facenum,
			vertices.data() + retained_face.first_vertex,
			(int)retained_face.vertex_count);
	}

	cache.specular_vertices.Initialize((uint32_t)vertices.size(),
		(uint32_t)(vertices.size() * sizeof(RetainedRoomSpecularVertex)),
		vertices.data());
	cache.specular_built = true;
	return &cache.specular_vertices;
}

static RetainedRoomCache* GetRetainedRoom(room* rp)
{
	RegisterRetainedRoomReleaseCallback();
	const int roomnum = rp ? (int)(rp - Rooms) : -1;
	if (roomnum < 0 || roomnum >= MAX_ROOMS)
		return nullptr;

	std::unique_ptr<RetainedRoomCache>& cache_ptr = Retained_room_caches[roomnum];
	if (!cache_ptr)
		cache_ptr = std::make_unique<RetainedRoomCache>();
	RetainedRoomCache& cache = *cache_ptr;
	const bool room_changed = cache.built &&
		(cache.num_vertices != rp->num_verts || cache.num_faces != rp->num_faces ||
		 cache.room_vertices != rp->verts || cache.room_faces != rp->faces);
	if (cache.built && (cache.renderer_generation != rend_GetGeneration() || room_changed))
	{
		if (cache.renderer_generation == rend_GetGeneration())
		{
			cache.vertices.Destroy();
			if (cache.specular_built)
				cache.specular_vertices.Destroy();
			cache.indices.Destroy();
		}
		else
		{
			cache.vertices.Invalidate();
			cache.specular_vertices.Invalidate();
			cache.indices.Invalidate();
		}
		cache.built = false;
		cache.specular_built = false;
		cache.cpu_vertices.clear();
	}
	if (!cache.built && !BuildRetainedRoom(rp, cache))
		return nullptr;
	return &cache;
}

void RetainedRoomPrecacheAll(bool include_specular)
{
	if (!UseHardware || StateLimited || !rend_CanUseNewrender() || NoLightmaps)
		return;

	RegisterRetainedRoomReleaseCallback();
	std::vector<ubyte> seen_lightmaps(MAX_LIGHTMAPS, 0);
	std::vector<int> lightmap_handles;
	for (int roomnum = 0; roomnum <= Highest_room_index; roomnum++)
	{
		room* rp = &Rooms[roomnum];
		if (!rp->used)
			continue;
		for (int facenum = 0; facenum < rp->num_faces; facenum++)
		{
			const face& fp = rp->faces[facenum];
			if (!RoomFaceUsesLightmap(const_cast<face*>(&fp)))
				continue;
			const int lightmap_handle = LightmapInfo[fp.lmi_handle].lm_handle;
			if (lightmap_handle >= 0 && lightmap_handle < MAX_LIGHTMAPS &&
				!seen_lightmaps[lightmap_handle])
			{
				seen_lightmaps[lightmap_handle] = 1;
				lightmap_handles.push_back(lightmap_handle);
			}
		}
	}
	Retained_room_lightmap_handles = lightmap_handles;
	Retained_room_lightmap_attempt_generation = rend_GetGeneration();
	rend_PrepareRetainedRoomLightmaps(lightmap_handles.data(),
		(int)lightmap_handles.size());
	for (int roomnum = 0; roomnum <= Highest_room_index; roomnum++)
	{
		room* rp = &Rooms[roomnum];
		if (!rp->used)
			continue;

		RetainedRoomCache* cache = GetRetainedRoom(rp);
		if (include_specular && cache)
			GetRetainedRoomSpecularVertices(rp, *cache);
	}

	// The last upload leaves its VAO and index buffer bound. Restore the
	// renderer's streaming vertex state before level startup continues.
	rendTEMP_UnbindVertexBuffer();
}

void RetainedRoomEnsureLightmaps()
{
	if (Retained_room_lightmap_handles.empty())
		return;
	if (rend_RetainedRoomLightmapsReady())
	{
		rend_RefreshRetainedRoomLightmaps();
		return;
	}

	const uint32_t generation = rend_GetGeneration();
	if (Retained_room_lightmap_attempt_generation == generation)
		return;
	Retained_room_lightmap_attempt_generation = generation;
	rend_PrepareRetainedRoomLightmaps(Retained_room_lightmap_handles.data(),
		(int)Retained_room_lightmap_handles.size());
}

bool RetainedRoomCanDrawBaseFace(room* rp, int facenum)
{
	if (!UseHardware || StateLimited || !rend_CanUseNewrender() || NoLightmaps ||
		!rp || !rp->used || facenum < 0 || facenum >= rp->num_faces)
	{
		return false;
	}
	RetainedRoomCache* cache = GetRetainedRoom(rp);
	return cache && facenum < (int)cache->faces.size() &&
		cache->faces[facenum].index_range.count != 0;
}

bool RetainedRoomDrawFaces(room* rp, const int* facenums, int count,
	int bitmap_handle, float u_offset, float v_offset, float light_scalar,
	int lightmap_handle, int clip_codes, bool per_pixel_specular_payload,
	bool retained_room_lightmap_arrays)
{
	if (!rp || !facenums || count <= 0)
		return false;
	RetainedRoomCache* cache = GetRetainedRoom(rp);
	if (!cache)
		return false;

	thread_local std::vector<ElementRange> ranges;
	ranges.clear();
	ranges.reserve(count);
	int vertex_count = 0;
	for (int i = 0; i < count; i++)
	{
		const int facenum = facenums[i];
		if (facenum < 0 || facenum >= (int)cache->faces.size())
			return false;
		const RetainedRoomFace& retained_face = cache->faces[facenum];
		if (retained_face.index_range.count != 0)
			AppendRetainedRoomFaceRanges(rp, *cache, facenum, ranges,
				vertex_count);
	}
	if (ranges.empty())
		return true;

	renderer_retained_polymodel_draw draw = {};
	memcpy(draw.transform, gTransformFull, sizeof(draw.transform));
	memcpy(draw.modelview, gTransformModelView, sizeof(draw.modelview));
	SetIdentity(draw.current_world);
	SetIdentity(draw.previous_world);
	draw.base_color[0] = light_scalar;
	draw.base_color[1] = light_scalar;
	draw.base_color[2] = light_scalar;
	draw.u_offset = u_offset;
	draw.v_offset = v_offset;
	draw.uv2_scale[0] = draw.uv2_scale[1] = 1.0f;
	// Room cache vertices carry each face's final lightmap scale. This permits
	// a material batch to select independent lightmaps without changing the
	// legacy normalized sampling coordinates.
	draw.depth_bias = Z_bias;
	draw.legacy_depth = true;
	draw.lighting_mode_override = 0;
	draw.fog_depth = 1.0f;
	draw.effect_alpha_scale = 1.0f;
	draw.polygon_count = (int)ranges.size();
	draw.vertex_count = vertex_count;
	draw.has_previous = false;
	draw.per_pixel_specular_payload = per_pixel_specular_payload;
	// Per-pixel specular reuses this geometry entry point with a different
	// material contract; only the ordinary base-material pass is eligible.
	draw.fast_room_base = !per_pixel_specular_payload;
	draw.retained_room_lightmap_arrays = retained_room_lightmap_arrays;
	ApplyRetainedRoomTransform(rp, draw);
	ApplyRetainedRoomDeformation(rp, draw);
	ApplyRetainedRoomClipping(clip_codes, draw);
	ApplyRetainedRoomLegacyProjection(draw);

	if (!ranges.empty())
	{
		VertexBuffer* vertices = per_pixel_specular_payload ?
			GetRetainedRoomSpecularVertices(rp, *cache) : &cache->vertices;
		if (!vertices)
			return false;
		vertices->Bind();
		cache->indices.Bind();
		if (!rend_BeginRetainedPolymodelDraw(&draw))
		{
			rendTEMP_UnbindVertexBuffer();
			return false;
		}
		vertices->DrawIndexedRanges(PrimitiveType::Triangles, ranges.data(),
			(uint32_t)ranges.size(), RENDERER_DRAW_CALL_3D);
		rend_EndRetainedPolymodelDraw();
		rendTEMP_UnbindVertexBuffer();
	}
	return true;
}

bool RetainedRoomDrawFogFaces(room* rp, const int* facenums, int count,
	const vector* fog_plane, float fog_distance, float fog_eye_distance,
	float fog_depth, bool use_fog_plane, const vector* viewer_eye,
	const matrix* viewer_orient, int clip_codes)
{
	if (!rp || !facenums || count <= 0)
		return false;
	RetainedRoomCache* cache = GetRetainedRoom(rp);
	if (!cache)
		return false;

	thread_local std::vector<ElementRange> ranges;
	ranges.clear();
	ranges.reserve(count);
	int vertex_count = 0;
	for (int i = 0; i < count; i++)
	{
		const int facenum = facenums[i];
		if (facenum < 0 || facenum >= (int)cache->faces.size())
			return false;
		const RetainedRoomFace& face = cache->faces[facenum];
		if (face.index_range.count != 0)
			AppendRetainedRoomFaceRanges(rp, *cache, facenum, ranges,
				vertex_count);
	}
	if (ranges.empty())
		return true;

	renderer_retained_polymodel_draw draw = {};
	memcpy(draw.transform, gTransformFull, sizeof(draw.transform));
	memcpy(draw.modelview, gTransformModelView, sizeof(draw.modelview));
	SetIdentity(draw.current_world);
	SetIdentity(draw.previous_world);
	draw.base_color[0] = draw.base_color[1] = draw.base_color[2] = 1.0f;
	draw.uv2_scale[0] = draw.uv2_scale[1] = 1.0f;
	draw.depth_bias = Z_bias;
	draw.legacy_depth = true;
	draw.lighting_mode_override = 0;
	// Room planes are expressed in world coordinates. Polymodel fog modes use
	// submodel/view coordinates, so keep the coordinate spaces explicit here.
	draw.effect_mode = use_fog_plane ? 4 : 5;
	draw.effect_alpha_scale = Room_light_val;
	if (fog_plane)
	{
		draw.fog_plane[0] = fog_plane->x;
		draw.fog_plane[1] = fog_plane->y;
		draw.fog_plane[2] = fog_plane->z;
	}
	draw.fog_distance = fog_distance;
	draw.fog_eye_distance = fog_eye_distance;
	draw.fog_depth = fog_depth > 0.0f ? fog_depth : 1.0f;
	draw.polygon_count = (int)ranges.size();
	draw.vertex_count = vertex_count;
	ApplyRetainedRoomTransform(rp, draw);
	ApplyRetainedRoomDeformation(rp, draw);
	ApplyRetainedRoomClipping(clip_codes, draw);
	ApplyRetainedRoomLegacyProjection(draw);

	if (!ranges.empty())
	{
		cache->vertices.Bind();
		cache->indices.Bind();
		if (!rend_BeginRetainedPolymodelDraw(&draw))
		{
			rendTEMP_UnbindVertexBuffer();
			return false;
		}
		cache->vertices.DrawIndexedRanges(PrimitiveType::Triangles, ranges.data(),
			(uint32_t)ranges.size(), RENDERER_DRAW_CALL_3D);
		rend_EndRetainedPolymodelDraw();
		rendTEMP_UnbindVertexBuffer();
	}
	return true;
}
