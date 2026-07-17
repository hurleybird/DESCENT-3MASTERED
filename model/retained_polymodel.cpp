#include "retained_polymodel.h"

#include "3d.h"
#include "../renderer/HardwareInternal.h"
#include "renderer.h"
#include "../renderer/gl_mesh.h"
#include "psrand.h"
#include "lightmap_info.h"
#include "lightmap.h"

#include <array>
#include <algorithm>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

struct RetainedPolymodelFace
{
	ElementRange index_range;
	uint32_t first_vertex = 0;
	uint32_t vertex_count = 0;
};

struct RetainedPolymodelSubmodel
{
	std::vector<RetainedPolymodelFace> faces;
};

struct RetainedPolymodelCache
{
	VertexBuffer vertices{ false, false, VertexBufferLayout::RetainedPolymodel };
	IndexBuffer indices{ false, false };
	std::vector<RetainedPolymodelSubmodel> submodels;
	std::vector<RendVertex> cpu_vertices;
	uint32_t renderer_generation = 0;
	bool built = false;
};

struct RetainedPolymodelLightmapCache
{
	VertexBuffer vertices{ false, false, VertexBufferLayout::RetainedPolymodel };
	int model_num = -1;
	uint32_t renderer_generation = 0;
};

struct RetainedPolymodelDeformation
{
	poly_model *pm = nullptr;
	bsp_info *sm = nullptr;
	uint32_t seed = 0;
	float range = 0.0f;
	bool enabled = false;
};

static std::array<std::unique_ptr<RetainedPolymodelCache>, MAX_POLY_MODELS> Retained_polymodel_caches;
static std::unordered_map<const lightmap_object *, std::unique_ptr<RetainedPolymodelLightmapCache>>
	Retained_polymodel_lightmap_caches;
static RetainedPolymodelDeformation Retained_polymodel_deformation;

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
	for (auto& entry : Retained_polymodel_lightmap_caches)
	{
		RetainedPolymodelLightmapCache& cache = *entry.second;
		if (cache.renderer_generation == rend_GetGeneration())
			cache.vertices.Destroy();
		else
			cache.vertices.Invalidate();
	}
	Retained_polymodel_lightmap_caches.clear();
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
		retained_submodel.faces.resize(sm->num_faces);
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
				vertex.face_normal = face->normal;
				vertex.source_vertex = vertex_num;
				vertices.push_back(vertex);
			}

			const uint32_t first_index = (uint32_t)indices.size();
			for (int triangle = 0; triangle < face->nverts - 2; triangle++)
			{
				indices.push_back(base_vertex);
				indices.push_back(base_vertex + triangle + 1);
				indices.push_back(base_vertex + triangle + 2);
			}
			RetainedPolymodelFace& retained_face = retained_submodel.faces[facenum];
			retained_face.index_range = ElementRange(first_index, (uint32_t)indices.size() - first_index);
			retained_face.first_vertex = base_vertex;
			retained_face.vertex_count = face->nverts;
		}
	}

	if (vertices.empty() || indices.empty())
		return false;
	cache.vertices.Initialize((uint32_t)vertices.size(),
		(uint32_t)(vertices.size() * sizeof(RendVertex)), vertices.data());
	cache.indices.Initialize((uint32_t)indices.size(),
		(uint32_t)(indices.size() * sizeof(uint32_t)), indices.data());
	cache.cpu_vertices = std::move(vertices);
	cache.renderer_generation = rend_GetGeneration();
	cache.built = true;
	return true;
}

static VertexBuffer *GetRetainedPolymodelLightmapVertices(poly_model *pm,
	RetainedPolymodelCache *model_cache, lightmap_object *lightmap)
{
	if (!pm || !model_cache || !lightmap)
		return nullptr;

	auto iter = Retained_polymodel_lightmap_caches.find(lightmap);
	if (iter != Retained_polymodel_lightmap_caches.end())
	{
		RetainedPolymodelLightmapCache& cache = *iter->second;
		if (cache.renderer_generation == rend_GetGeneration() && cache.model_num == (pm - Poly_models))
			return &cache.vertices;
		if (cache.renderer_generation == rend_GetGeneration())
			cache.vertices.Destroy();
		else
			cache.vertices.Invalidate();
		Retained_polymodel_lightmap_caches.erase(iter);
	}

	std::vector<RendVertex> vertices = model_cache->cpu_vertices;
	if (lightmap->num_models != pm->n_models)
		return nullptr;
	for (int submodel_num = 0; submodel_num < pm->n_models; submodel_num++)
	{
		bsp_info *sm = &pm->submodel[submodel_num];
		if (lightmap->num_faces[submodel_num] != sm->num_faces ||
			(sm->num_faces > 0 && !lightmap->lightmap_faces[submodel_num]))
		{
			return nullptr;
		}
		for (int facenum = 0; facenum < sm->num_faces; facenum++)
		{
			const RetainedPolymodelFace& retained_face = model_cache->submodels[submodel_num].faces[facenum];
			lightmap_object_face& lightmap_face = lightmap->lightmap_faces[submodel_num][facenum];
			if ((int)retained_face.vertex_count != lightmap_face.num_verts ||
				(retained_face.vertex_count > 0 && (!lightmap_face.u2 || !lightmap_face.v2)))
			{
				return nullptr;
			}
			for (uint32_t corner = 0; corner < retained_face.vertex_count; corner++)
			{
				RendVertex& vertex = vertices[retained_face.first_vertex + corner];
				vertex.u2 = lightmap_face.u2[corner];
				vertex.v2 = lightmap_face.v2[corner];
			}
		}
	}

	auto inserted = Retained_polymodel_lightmap_caches.emplace(lightmap,
		std::make_unique<RetainedPolymodelLightmapCache>());
	RetainedPolymodelLightmapCache& cache = *inserted.first->second;
	cache.vertices.Initialize((uint32_t)vertices.size(),
		(uint32_t)(vertices.size() * sizeof(RendVertex)), vertices.data());
	cache.model_num = pm - Poly_models;
	cache.renderer_generation = rend_GetGeneration();
	return &cache.vertices;
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
#ifdef _DEBUG
	if (Polymodel_outline_mode)
		return false;
#endif
	if (Polymodel_use_effect && (Polymodel_effect.type & PEF_BUMPMAPPED))
		return false;
	if (sm->faces[facenum].nverts < 3 || sm->faces[facenum].nverts >= 100)
		return false;
	RetainedPolymodelCache *cache = GetRetainedPolymodel(pm);
	const int submodel_num = sm - pm->submodel;
	if (!cache || submodel_num < 0 || submodel_num >= (int)cache->submodels.size())
		return false;
	if (Polymodel_light_type == POLYMODEL_LIGHTING_LIGHTMAP)
	{
		if (!Polylighting_lightmap_object ||
			Polylighting_lightmap_object->num_models != pm->n_models ||
			Polylighting_lightmap_object->num_faces[submodel_num] != sm->num_faces ||
			!Polylighting_lightmap_object->lightmap_faces[submodel_num])
		{
			return false;
		}
		const lightmap_object_face& lightmap_face =
			Polylighting_lightmap_object->lightmap_faces[submodel_num][facenum];
		if (lightmap_face.num_verts != sm->faces[facenum].nverts ||
			!lightmap_face.u2 || !lightmap_face.v2 || lightmap_face.lmi_handle == BAD_LMI_INDEX)
		{
			return false;
		}
		// Validate and create the object-specific UV2 stream before accepting any
		// face into a deferred batch.  A later draw can then never silently lose
		// the batch because a different face in the object had malformed data.
		if (!GetRetainedPolymodelLightmapVertices(pm, cache, Polylighting_lightmap_object))
			return false;
	}
	return true;
}

bool RetainedPolymodelCanSkipPointRotation(poly_model *pm, bsp_info *sm)
{
	if (!pm || !sm || !RetainedPolymodelEnabled())
		return false;
	for (int facenum = 0; facenum < sm->num_faces; facenum++)
	{
		if (!RetainedPolymodelCanDrawBaseFace(pm, sm, facenum))
			return false;
	}
	return true;
}

void RetainedPolymodelPrepareSubmodel(poly_model *pm, bsp_info *sm, bool advance_visual_random)
{
	Retained_polymodel_deformation = {};
	if (!pm || !sm)
		return;

	const bool deform = (sm->flags & SOF_JITTER) ||
		(Polymodel_use_effect && (Polymodel_effect.type & PEF_DEFORM));
	if (!deform)
		return;

	Retained_polymodel_deformation.pm = pm;
	Retained_polymodel_deformation.sm = sm;
	Retained_polymodel_deformation.seed = ps_rand_get_state();
	Retained_polymodel_deformation.range = Polymodel_effect.deform_range;
	Retained_polymodel_deformation.enabled = true;
	if (advance_visual_random)
	{
		// When CPU point rotation is skipped, preserve the global visual-random
		// stream exactly as RotateModelPoints would have advanced it.  Mixed-path
		// submodels capture the same seed here and let RotateModelPoints consume it.
		for (int i = 0; i < sm->nverts; i++)
			ps_rand();
	}
}

static bool DrawRetainedPolymodelRanges(poly_model *pm, bsp_info *sm, const int *facenums, int count,
	float u_offset, float v_offset, const vector *base_color, int effect_mode,
	const vector *fog_plane, float fog_distance, float fog_eye_distance, float fog_depth,
	const vector *specular_view_position, const vector *specular_light_position,
	float specular_scalar, bool specular_smooth)
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
		if (facenum < 0 || facenum >= (int)cache->submodels[submodel_num].faces.size())
			return false;
		const ElementRange range = cache->submodels[submodel_num].faces[facenum].index_range;
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
	draw.uv2_scale[0] = draw.uv2_scale[1] = 1.0f;
	draw.depth_bias = Z_bias;
	draw.legacy_depth = true;
	draw.lighting_mode_override = -1;
	draw.effect_mode = effect_mode;
	draw.effect_alpha_scale = 1.0f;
	if (Polymodel_light_type == POLYMODEL_LIGHTING_LIGHTMAP && count > 0)
	{
		const int submodel_num = sm - pm->submodel;
		const int facenum = facenums[0];
		const int lmi_handle = Polylighting_lightmap_object->lightmap_faces[submodel_num][facenum].lmi_handle;
		const int lightmap_handle = LightmapInfo[lmi_handle].lm_handle;
		if (lightmap_handle >= 0 && GameLightmaps[lightmap_handle].square_res > 0)
		{
			draw.uv2_scale[0] = (float)GameLightmaps[lightmap_handle].width /
				GameLightmaps[lightmap_handle].square_res;
			draw.uv2_scale[1] = (float)GameLightmaps[lightmap_handle].height /
				GameLightmaps[lightmap_handle].square_res;
		}
	}
	if (fog_plane)
	{
		draw.fog_plane[0] = fog_plane->x;
		draw.fog_plane[1] = fog_plane->y;
		draw.fog_plane[2] = fog_plane->z;
	}
	draw.fog_distance = fog_distance;
	draw.fog_eye_distance = fog_eye_distance;
	draw.fog_depth = fog_depth > 0.0f ? fog_depth : 1.0f;
	if (specular_view_position)
	{
		draw.specular_view_position[0] = specular_view_position->x;
		draw.specular_view_position[1] = specular_view_position->y;
		draw.specular_view_position[2] = specular_view_position->z;
	}
	if (specular_light_position)
	{
		draw.specular_light_position[0] = specular_light_position->x;
		draw.specular_light_position[1] = specular_light_position->y;
		draw.specular_light_position[2] = specular_light_position->z;
	}
	draw.specular_scalar = specular_scalar;
	draw.specular_smooth = specular_smooth;
	if (Retained_polymodel_deformation.enabled &&
		Retained_polymodel_deformation.pm == pm && Retained_polymodel_deformation.sm == sm)
	{
		draw.deform_enabled = true;
		draw.deform_mode = 1;
		draw.deform_seed = Retained_polymodel_deformation.seed;
		draw.deform_range = Retained_polymodel_deformation.range;
	}
	draw.custom_clip_enabled = Clip_custom != 0;
	if (draw.custom_clip_enabled)
	{
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
	draw.polygon_count = 0;
	draw.vertex_count = 0;
	for (int i = 0; i < count; i++)
	{
		const int facenum = facenums[i];
		if (facenum >= 0 && facenum < sm->num_faces &&
			cache->submodels[submodel_num].faces[facenum].index_range.count != 0)
		{
			draw.polygon_count++;
			draw.vertex_count += sm->faces[facenum].nverts;
		}
	}

	VertexBuffer *vertices = &cache->vertices;
	if (Polymodel_light_type == POLYMODEL_LIGHTING_LIGHTMAP)
	{
		vertices = GetRetainedPolymodelLightmapVertices(pm, cache, Polylighting_lightmap_object);
		if (!vertices)
			return false;
	}
	vertices->Bind();
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
		base_color, 0, nullptr, 0.0f, 0.0f, 1.0f, nullptr, nullptr, 0.0f, false);
}

bool RetainedPolymodelDrawFogFaces(poly_model *pm, bsp_info *sm, const int *facenums, int count,
	const vector *fog_plane, float fog_distance, float fog_eye_distance, float fog_depth,
	bool use_fog_plane)
{
	return DrawRetainedPolymodelRanges(pm, sm, facenums, count, 0.0f, 0.0f,
		nullptr, use_fog_plane ? 2 : 1, fog_plane, fog_distance, fog_eye_distance, fog_depth,
		nullptr, nullptr, 0.0f, false);
}

bool RetainedPolymodelDrawSpecularFaces(poly_model *pm, bsp_info *sm, const int *facenums, int count,
	const vector *view_position, const vector *light_position, float scalar, bool smooth)
{
	return DrawRetainedPolymodelRanges(pm, sm, facenums, count, 0.0f, 0.0f,
		nullptr, 3, nullptr, 0.0f, 0.0f, 1.0f, view_position, light_position, scalar, smooth);
}

void RetainedPolymodelInvalidateLightmapObject(lightmap_object *lightmap)
{
	if (!lightmap)
		return;
	auto iter = Retained_polymodel_lightmap_caches.find(lightmap);
	if (iter == Retained_polymodel_lightmap_caches.end())
		return;
	RetainedPolymodelLightmapCache& cache = *iter->second;
	if (cache.renderer_generation == rend_GetGeneration())
		cache.vertices.Destroy();
	else
		cache.vertices.Invalidate();
	Retained_polymodel_lightmap_caches.erase(iter);
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
	for (auto iter = Retained_polymodel_lightmap_caches.begin();
		iter != Retained_polymodel_lightmap_caches.end();)
	{
		if (iter->second->model_num != model_num)
		{
			++iter;
			continue;
		}
		RetainedPolymodelLightmapCache& lightmap_cache = *iter->second;
		if (lightmap_cache.renderer_generation == rend_GetGeneration())
			lightmap_cache.vertices.Destroy();
		else
			lightmap_cache.vertices.Invalidate();
		iter = Retained_polymodel_lightmap_caches.erase(iter);
	}
}
