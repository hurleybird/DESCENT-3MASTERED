#include "retained_room.h"

#include "3d.h"
#include "room.h"
#include "render.h"
#include "../renderer/HardwareInternal.h"
#include "../renderer/gl_mesh.h"
#include "renderer.h"
#include "lightmap.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <vector>

struct RetainedRoomFace
{
	ElementRange index_range;
	uint32_t vertex_count = 0;
};

struct RetainedRoomCache
{
	VertexBuffer vertices{ false, false, VertexBufferLayout::RetainedPolymodel };
	IndexBuffer indices{ false, false };
	std::vector<RetainedRoomFace> faces;
	uint32_t renderer_generation = 0;
	int num_vertices = 0;
	int num_faces = 0;
	const vector* room_vertices = nullptr;
	const face* room_faces = nullptr;
	bool built = false;
};

static std::array<std::unique_ptr<RetainedRoomCache>, MAX_ROOMS> Retained_room_caches;

static void SetIdentity(float matrix[16])
{
	for (int i = 0; i < 16; i++)
		matrix[i] = 0.0f;
	matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0f;
}

void RetainedRoomInvalidateAll()
{
	for (std::unique_ptr<RetainedRoomCache>& cache : Retained_room_caches)
	{
		if (!cache)
			continue;
		if (cache->built && cache->renderer_generation == rend_GetGeneration())
		{
			cache->vertices.Destroy();
			cache->indices.Destroy();
		}
		else
		{
			cache->vertices.Invalidate();
			cache->indices.Invalidate();
		}
		cache.reset();
	}
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
			vertex.u1 = uv.u;
			vertex.v1 = uv.v;
			vertex.u2 = uv.u2;
			vertex.v2 = uv.v2;
			vertex.source_vertex = vertex_num;
			vertices.push_back(vertex);
		}

		const uint32_t first_index = (uint32_t)indices.size();
		for (int triangle = 0; triangle < fp->num_verts - 2; triangle++)
		{
			indices.push_back(base_vertex);
			indices.push_back(base_vertex + triangle + 1);
			indices.push_back(base_vertex + triangle + 2);
		}
		cache.faces[facenum].index_range =
			ElementRange(first_index, (uint32_t)indices.size() - first_index);
		cache.faces[facenum].vertex_count = fp->num_verts;
	}

	if (vertices.empty() || indices.empty())
		return false;
	cache.vertices.Initialize((uint32_t)vertices.size(),
		(uint32_t)(vertices.size() * sizeof(RendVertex)), vertices.data());
	cache.indices.Initialize((uint32_t)indices.size(),
		(uint32_t)(indices.size() * sizeof(uint32_t)), indices.data());
	cache.renderer_generation = rend_GetGeneration();
	cache.num_vertices = rp->num_verts;
	cache.num_faces = rp->num_faces;
	cache.room_vertices = rp->verts;
	cache.room_faces = rp->faces;
	cache.built = true;
	return true;
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
			cache.indices.Destroy();
		}
		else
		{
			cache.vertices.Invalidate();
			cache.indices.Invalidate();
		}
		cache.built = false;
	}
	if (!cache.built && !BuildRetainedRoom(rp, cache))
		return nullptr;
	return &cache;
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
	float u_offset, float v_offset, float light_scalar, int lightmap_handle)
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
		{
			ranges.push_back(retained_face.index_range);
			vertex_count += retained_face.vertex_count;
		}
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
	if (lightmap_handle >= 0 && GameLightmaps[lightmap_handle].square_res > 0)
	{
		draw.uv2_scale[0] = (float)GameLightmaps[lightmap_handle].width /
			GameLightmaps[lightmap_handle].square_res;
		draw.uv2_scale[1] = (float)GameLightmaps[lightmap_handle].height /
			GameLightmaps[lightmap_handle].square_res;
	}
	draw.depth_bias = Z_bias;
	draw.legacy_depth = true;
	draw.lighting_mode_override = 0;
	draw.fog_depth = 1.0f;
	draw.effect_alpha_scale = 1.0f;
	draw.polygon_count = (int)ranges.size();
	draw.vertex_count = vertex_count;
	draw.has_previous = false;

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
	return true;
}

bool RetainedRoomDrawFogFaces(room* rp, const int* facenums, int count,
	const vector* fog_plane, float fog_distance, float fog_eye_distance,
	float fog_depth, bool use_fog_plane)
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
		{
			ranges.push_back(face.index_range);
			vertex_count += face.vertex_count;
		}
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
	draw.effect_mode = use_fog_plane ? 2 : 1;
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
	return true;
}
