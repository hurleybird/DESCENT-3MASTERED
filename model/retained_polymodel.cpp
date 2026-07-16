#include "retained_polymodel.h"

#include "3d.h"
#include "../renderer/HardwareInternal.h"
#include "renderer.h"
#include "../renderer/gl_mesh.h"

#include <array>
#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

struct RetainedPolymodelSubmodel
{
	std::vector<ElementRange> face_ranges;
};

struct RetainedPolymodelCache
{
	VertexBuffer vertices{ false, false, VertexBufferLayout::RetainedPolymodel };
	IndexBuffer indices{ false, false };
	std::vector<RetainedPolymodelSubmodel> submodels;
	uint32_t renderer_generation = 0;
	bool built = false;
};

static std::array<std::unique_ptr<RetainedPolymodelCache>, MAX_POLY_MODELS> Retained_polymodel_caches;

static void ReleaseRetainedPolymodelCaches()
{
	for (std::unique_ptr<RetainedPolymodelCache>& cache : Retained_polymodel_caches)
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

static void RegisterRetainedPolymodelReleaseCallback()
{
	static bool registered = false;
	if (!registered)
	{
		rend_RegisterResourceReleaseCallback(ReleaseRetainedPolymodelCaches);
		registered = true;
	}
}

static void SetIdentity(float matrix[16])
{
	for (int i = 0; i < 16; i++)
		matrix[i] = 0.0f;
	matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0f;
}

bool RetainedPolymodelEnabled()
{
	return UseHardware && !StateLimited && rend_CanUseNewrender();
}

static bool BuildRetainedPolymodel(poly_model *pm, RetainedPolymodelCache& cache)
{
	if (!pm || !pm->submodel || pm->n_models <= 0)
		return false;

	std::vector<RendVertex> vertices;
	std::vector<uint32_t> indices;
	cache.submodels.clear();
	cache.submodels.resize(pm->n_models);

	for (int submodel_num = 0; submodel_num < pm->n_models; submodel_num++)
	{
		bsp_info *sm = &pm->submodel[submodel_num];
		RetainedPolymodelSubmodel& retained_submodel = cache.submodels[submodel_num];
		retained_submodel.face_ranges.resize(sm->num_faces);
		for (int facenum = 0; facenum < sm->num_faces; facenum++)
		{
			polyface *face = &sm->faces[facenum];
			if (face->nverts < 3)
				continue;

			const uint32_t base_vertex = (uint32_t)vertices.size();
			for (int corner = 0; corner < face->nverts; corner++)
			{
				const int vertex_num = face->vertnums[corner];
				RendVertex vertex = {};
				vertex.position = sm->verts[vertex_num];
				vertex.normal = sm->vertnorms ? sm->vertnorms[vertex_num] : face->normal;
				vertex.r = vertex.g = vertex.b = 255;
				float alpha = sm->alpha ? sm->alpha[vertex_num] : 1.0f;
				vertex.a = (ubyte)std::max(0.0f, std::min(alpha * 255.0f, 255.0f));
				vertex.u1 = face->u ? face->u[corner] : 0.0f;
				vertex.v1 = face->v ? face->v[corner] : 0.0f;
				vertices.push_back(vertex);
			}

			const uint32_t first_index = (uint32_t)indices.size();
			for (int triangle = 0; triangle < face->nverts - 2; triangle++)
			{
				indices.push_back(base_vertex);
				indices.push_back(base_vertex + triangle + 1);
				indices.push_back(base_vertex + triangle + 2);
			}
			retained_submodel.face_ranges[facenum] =
				ElementRange(first_index, (uint32_t)indices.size() - first_index);
		}
	}

	if (vertices.empty() || indices.empty())
		return false;
	cache.vertices.Initialize((uint32_t)vertices.size(),
		(uint32_t)(vertices.size() * sizeof(RendVertex)), vertices.data());
	cache.indices.Initialize((uint32_t)indices.size(),
		(uint32_t)(indices.size() * sizeof(uint32_t)), indices.data());
	cache.renderer_generation = rend_GetGeneration();
	cache.built = true;
	return true;
}

static RetainedPolymodelCache *GetRetainedPolymodel(poly_model *pm)
{
	RegisterRetainedPolymodelReleaseCallback();
	const int model_num = pm ? (int)(pm - Poly_models) : -1;
	if (model_num < 0 || model_num >= MAX_POLY_MODELS)
		return nullptr;

	std::unique_ptr<RetainedPolymodelCache>& cache_ptr = Retained_polymodel_caches[model_num];
	if (!cache_ptr)
		cache_ptr = std::make_unique<RetainedPolymodelCache>();
	RetainedPolymodelCache& cache = *cache_ptr;
	if (cache.built && cache.renderer_generation != rend_GetGeneration())
	{
		cache.vertices.Invalidate();
		cache.indices.Invalidate();
		cache.built = false;
	}
	if (!cache.built && !BuildRetainedPolymodel(pm, cache))
		return nullptr;
	return &cache;
}

bool RetainedPolymodelCanDrawBaseFace(poly_model *pm, bsp_info *sm, int facenum)
{
	if (!RetainedPolymodelEnabled() || !pm || !sm || facenum < 0 || facenum >= sm->num_faces)
		return false;
	if (Clip_custom || Polymodel_light_type == POLYMODEL_LIGHTING_LIGHTMAP)
		return false;
#ifdef _DEBUG
	if (Polymodel_outline_mode)
		return false;
#endif
	if ((sm->flags & SOF_JITTER) ||
		(Polymodel_use_effect && (Polymodel_effect.type & (PEF_DEFORM | PEF_BUMPMAPPED))))
		return false;
	if (sm->faces[facenum].nverts < 3 || sm->faces[facenum].nverts >= 100)
		return false;
	RetainedPolymodelCache *cache = GetRetainedPolymodel(pm);
	const int submodel_num = sm - pm->submodel;
	return cache && submodel_num >= 0 && submodel_num < (int)cache->submodels.size();
}

bool RetainedPolymodelCanSkipPointRotation(poly_model *pm, bsp_info *sm)
{
	if (!pm || !sm || !RetainedPolymodelEnabled())
		return false;
	if (Polymodel_use_effect &&
		(Polymodel_effect.type & (PEF_SPECULAR_MODEL | PEF_SPECULAR_FACES)))
		return false;
	for (int facenum = 0; facenum < sm->num_faces; facenum++)
	{
		if (!RetainedPolymodelCanDrawBaseFace(pm, sm, facenum))
			return false;
	}
	return true;
}

static bool DrawRetainedPolymodelRanges(poly_model *pm, bsp_info *sm, const int *facenums, int count,
	float u_offset, float v_offset, const vector *base_color, int effect_mode,
	const vector *fog_plane, float fog_distance, float fog_eye_distance, float fog_depth)
{
	if (!pm || !sm || !facenums || count <= 0)
		return false;
	RetainedPolymodelCache *cache = GetRetainedPolymodel(pm);
	const int submodel_num = sm - pm->submodel;
	if (!cache || submodel_num < 0 || submodel_num >= (int)cache->submodels.size())
		return false;

	thread_local std::vector<ElementRange> ranges;
	ranges.clear();
	ranges.reserve(count);
	for (int i = 0; i < count; i++)
	{
		const int facenum = facenums[i];
		if (facenum < 0 || facenum >= (int)cache->submodels[submodel_num].face_ranges.size())
			return false;
		const ElementRange range = cache->submodels[submodel_num].face_ranges[facenum];
		if (range.count != 0)
			ranges.push_back(range);
	}
	if (ranges.empty())
		return true;

	renderer_retained_polymodel_draw draw = {};
	memcpy(draw.transform, gTransformFull, sizeof(draw.transform));
	memcpy(draw.modelview, gTransformModelView, sizeof(draw.modelview));
	SetIdentity(draw.current_world);
	SetIdentity(draw.previous_world);
	draw.has_previous = false;
	PolymodelMotionGetSubmodelMatrices(pm, submodel_num, draw.current_world,
		draw.previous_world, &draw.has_previous);
	draw.base_color[0] = base_color ? base_color->x : 1.0f;
	draw.base_color[1] = base_color ? base_color->y : 1.0f;
	draw.base_color[2] = base_color ? base_color->z : 1.0f;
	draw.u_offset = u_offset;
	draw.v_offset = v_offset;
	draw.effect_mode = effect_mode;
	if (fog_plane)
	{
		draw.fog_plane[0] = fog_plane->x;
		draw.fog_plane[1] = fog_plane->y;
		draw.fog_plane[2] = fog_plane->z;
	}
	draw.fog_distance = fog_distance;
	draw.fog_eye_distance = fog_eye_distance;
	draw.fog_depth = fog_depth > 0.0f ? fog_depth : 1.0f;
	draw.polygon_count = 0;
	draw.vertex_count = 0;
	for (int i = 0; i < count; i++)
	{
		const int facenum = facenums[i];
		if (facenum >= 0 && facenum < sm->num_faces &&
			cache->submodels[submodel_num].face_ranges[facenum].count != 0)
		{
			draw.polygon_count++;
			draw.vertex_count += sm->faces[facenum].nverts;
		}
	}

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

bool RetainedPolymodelDrawFaces(poly_model *pm, bsp_info *sm, const int *facenums, int count,
	float u_offset, float v_offset, const vector *base_color)
{
	return DrawRetainedPolymodelRanges(pm, sm, facenums, count, u_offset, v_offset,
		base_color, 0, nullptr, 0.0f, 0.0f, 1.0f);
}

bool RetainedPolymodelDrawFogFaces(poly_model *pm, bsp_info *sm, const int *facenums, int count,
	const vector *fog_plane, float fog_distance, float fog_eye_distance, float fog_depth,
	bool use_fog_plane)
{
	return DrawRetainedPolymodelRanges(pm, sm, facenums, count, 0.0f, 0.0f,
		nullptr, use_fog_plane ? 2 : 1, fog_plane, fog_distance, fog_eye_distance, fog_depth);
}

void RetainedPolymodelInvalidateModel(int model_num)
{
	if (model_num < 0 || model_num >= MAX_POLY_MODELS || !Retained_polymodel_caches[model_num])
		return;
	RetainedPolymodelCache& cache = *Retained_polymodel_caches[model_num];
	if (cache.built && cache.renderer_generation == rend_GetGeneration())
	{
		cache.vertices.Destroy();
		cache.indices.Destroy();
	}
	else
	{
		cache.vertices.Invalidate();
		cache.indices.Invalidate();
	}
	Retained_polymodel_caches[model_num].reset();
}
