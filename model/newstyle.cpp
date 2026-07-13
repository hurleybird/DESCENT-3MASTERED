/* 
* Descent 3 
* Copyright (C) 2024 Parallax Software
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "pserror.h"
#include "pstypes.h"

#include "3d.h"
#include "vecmat.h"
#include "grdefs.h"
#include "polymodel.h"
#include "gametexture.h"
#include "BYTESWAP.H"
#include "renderer.h"
#include "lighting.h"
#include "game.h"
#include "render.h"
#include "fireball.h"
#include "lightmap_info.h"
#include "lightmap.h"
#include "lighting.h"
#include "findintersection.h"
#include "gameloop.h"
#include "config.h"


#include <stdio.h>
#include <stdlib.h>
#include <search.h>
#include <string.h>
#include <functional>
#include <stdint.h>
#include <algorithm>
#include <unordered_map>
#include <vector>

#include "psrand.h"

static float face_depth[MAX_POLYGON_VECS];
static ubyte triangulated_faces[MAX_FACES_PER_ROOM];

static ubyte FacingPass=0;
static int Multicolor_texture=-1;
static bool Polymodel_cockpit_batching = false;
static bool Polymodel_cockpit_transparent_face_filter_enabled = false;
static int Polymodel_cockpit_transparent_face_filter_model = -1;
static uint Polymodel_cockpit_transparent_face_filter_mask = 0;

static vector Fog_plane;
static float Fog_distance,Fog_eye_distance;
static vector Specular_view_pos,Bump_view_pos;
static matrix Unscaled_bumpmap_matrix;

static double Polymodel_perf_first_time = 0.0;
static double Polymodel_perf_draw_model_time = 0.0;
static double Polymodel_perf_draw_model_setup_time = 0.0;
static double Polymodel_perf_render_polygon_time = 0.0;
static double Polymodel_perf_root_pass_time = 0.0;
static double Polymodel_perf_facing_pass_time = 0.0;
static double Polymodel_perf_submodel_instance_time = 0.0;
static double Polymodel_perf_submodel_rotate_time = 0.0;
static double Polymodel_perf_submodel_faces_time = 0.0;
static double Polymodel_perf_faces_unsorted_time = 0.0;
static double Polymodel_perf_faces_unsorted_scan_time = 0.0;
static double Polymodel_perf_faces_unsorted_sort_time = 0.0;
static double Polymodel_perf_faces_sorted_time = 0.0;
static double Polymodel_perf_face_base_time = 0.0;
static double Polymodel_perf_face_base_key_time = 0.0;
static double Polymodel_perf_face_base_point_time = 0.0;
static double Polymodel_perf_face_base_state_time = 0.0;
static double Polymodel_perf_face_base_per_pixel_time = 0.0;
static double Polymodel_perf_face_base_clip_time = 0.0;
static double Polymodel_perf_face_base_copy_time = 0.0;
static double Polymodel_perf_face_base_batch_add_time = 0.0;
static double Polymodel_perf_face_base_batch_prepare_time = 0.0;
static double Polymodel_perf_face_base_draw_time = 0.0;
static double Polymodel_perf_lightmap_face_time = 0.0;
static double Polymodel_perf_lightmap_face_draw_time = 0.0;
static double Polymodel_perf_specular_face_time = 0.0;
static double Polymodel_perf_specular_pass_time = 0.0;
static double Polymodel_perf_fog_face_time = 0.0;
static double Polymodel_perf_fog_pass_time = 0.0;
static double Polymodel_perf_facing_effect_time = 0.0;
static int Polymodel_perf_draw_model_count = 0;
static int Polymodel_perf_render_polygon_count = 0;
static int Polymodel_perf_submodel_visit_count = 0;
static int Polymodel_perf_submodel_draw_count = 0;
static int Polymodel_perf_faces_considered_count = 0;
static int Polymodel_perf_base_face_count = 0;
static int Polymodel_perf_base_face_clipped_count = 0;
static int Polymodel_perf_alpha_face_count = 0;
static int Polymodel_perf_lightmap_face_count = 0;
static int Polymodel_perf_specular_face_count = 0;
static int Polymodel_perf_fog_face_count = 0;
static int Polymodel_perf_draw_poly_count = 0;
static int Polymodel_perf_facing_effect_count = 0;
static int Polymodel_perf_base_batch_add_count = 0;
static int Polymodel_perf_base_batch_last_hit_count = 0;
static int Polymodel_perf_base_batch_lookup_hit_count = 0;
static int Polymodel_perf_base_batch_miss_count = 0;
static int Polymodel_perf_base_batch_key_compare_count = 0;
static int Polymodel_perf_base_batch_created_count = 0;
static int Polymodel_perf_base_batch_flushed_count = 0;
static int Polymodel_perf_base_batch_flush_count = 0;
static int Polymodel_perf_base_batch_max_batches_per_flush = 0;
static int Polymodel_perf_base_batch_max_faces_per_batch = 0;

static double PolymodelPerfNow()
{
	return Perf_markers_enabled ? PerfMarkersNow() : 0.0;
}

static void PolymodelPerfTouch(double start_time)
{
	if (Perf_markers_enabled && Polymodel_perf_first_time == 0.0)
		Polymodel_perf_first_time = start_time;
}

static void PolymodelPerfAdd(double& bucket, double start_time)
{
	if (Perf_markers_enabled)
		bucket += PerfMarkersNow() - start_time;
}

static void PolymodelPerfRecordDuration(double marker_time, const char* marker_name, double duration)
{
	if (duration > 0.0)
		PerfMarkersRecordDuration(marker_name, marker_time, duration);
}

static void PolymodelPerfRecordCounter(double marker_time, const char* marker_name, int count)
{
	if (count <= 0)
		return;

	char marker[96];
	snprintf(marker, sizeof(marker), "%s=%d", marker_name, count);
	PerfMarkersRecordDuration(marker, marker_time, 0.0);
}

static void PolymodelPerfUpdateMax(int& bucket, size_t value)
{
	if (Perf_markers_enabled && value > (size_t)bucket)
		bucket = (int)value;
}

void PolymodelPerfReset()
{
	Polymodel_perf_first_time = 0.0;
	Polymodel_perf_draw_model_time = 0.0;
	Polymodel_perf_draw_model_setup_time = 0.0;
	Polymodel_perf_render_polygon_time = 0.0;
	Polymodel_perf_root_pass_time = 0.0;
	Polymodel_perf_facing_pass_time = 0.0;
	Polymodel_perf_submodel_instance_time = 0.0;
	Polymodel_perf_submodel_rotate_time = 0.0;
	Polymodel_perf_submodel_faces_time = 0.0;
	Polymodel_perf_faces_unsorted_time = 0.0;
	Polymodel_perf_faces_unsorted_scan_time = 0.0;
	Polymodel_perf_faces_unsorted_sort_time = 0.0;
	Polymodel_perf_faces_sorted_time = 0.0;
	Polymodel_perf_face_base_time = 0.0;
	Polymodel_perf_face_base_key_time = 0.0;
	Polymodel_perf_face_base_point_time = 0.0;
	Polymodel_perf_face_base_state_time = 0.0;
	Polymodel_perf_face_base_per_pixel_time = 0.0;
	Polymodel_perf_face_base_clip_time = 0.0;
	Polymodel_perf_face_base_copy_time = 0.0;
	Polymodel_perf_face_base_batch_add_time = 0.0;
	Polymodel_perf_face_base_batch_prepare_time = 0.0;
	Polymodel_perf_face_base_draw_time = 0.0;
	Polymodel_perf_lightmap_face_time = 0.0;
	Polymodel_perf_lightmap_face_draw_time = 0.0;
	Polymodel_perf_specular_face_time = 0.0;
	Polymodel_perf_specular_pass_time = 0.0;
	Polymodel_perf_fog_face_time = 0.0;
	Polymodel_perf_fog_pass_time = 0.0;
	Polymodel_perf_facing_effect_time = 0.0;
	Polymodel_perf_draw_model_count = 0;
	Polymodel_perf_render_polygon_count = 0;
	Polymodel_perf_submodel_visit_count = 0;
	Polymodel_perf_submodel_draw_count = 0;
	Polymodel_perf_faces_considered_count = 0;
	Polymodel_perf_base_face_count = 0;
	Polymodel_perf_base_face_clipped_count = 0;
	Polymodel_perf_alpha_face_count = 0;
	Polymodel_perf_lightmap_face_count = 0;
	Polymodel_perf_specular_face_count = 0;
	Polymodel_perf_fog_face_count = 0;
	Polymodel_perf_draw_poly_count = 0;
	Polymodel_perf_facing_effect_count = 0;
	Polymodel_perf_base_batch_add_count = 0;
	Polymodel_perf_base_batch_last_hit_count = 0;
	Polymodel_perf_base_batch_lookup_hit_count = 0;
	Polymodel_perf_base_batch_miss_count = 0;
	Polymodel_perf_base_batch_key_compare_count = 0;
	Polymodel_perf_base_batch_created_count = 0;
	Polymodel_perf_base_batch_flushed_count = 0;
	Polymodel_perf_base_batch_flush_count = 0;
	Polymodel_perf_base_batch_max_batches_per_flush = 0;
	Polymodel_perf_base_batch_max_faces_per_batch = 0;
}

void PolymodelPerfFlush()
{
	if (!Perf_markers_enabled || Polymodel_perf_first_time == 0.0)
		return;

	double marker_time = Polymodel_perf_first_time;
	PolymodelPerfRecordDuration(marker_time, "Polymodel.DrawPolygonModel.Aggregate", Polymodel_perf_draw_model_time);
	PolymodelPerfRecordDuration(marker_time, "Polymodel.DrawPolygonModel.Setup.Aggregate", Polymodel_perf_draw_model_setup_time);
	PolymodelPerfRecordDuration(marker_time, "Polymodel.RenderPolygonModel.Aggregate", Polymodel_perf_render_polygon_time);
	PolymodelPerfRecordDuration(marker_time, "Polymodel.RootPass.Aggregate", Polymodel_perf_root_pass_time);
	PolymodelPerfRecordDuration(marker_time, "Polymodel.FacingPass.Aggregate", Polymodel_perf_facing_pass_time);
	PolymodelPerfRecordDuration(marker_time, "Polymodel.Submodel.InstanceSetup.Aggregate", Polymodel_perf_submodel_instance_time);
	PolymodelPerfRecordDuration(marker_time, "Polymodel.Submodel.RotatePoints.Aggregate", Polymodel_perf_submodel_rotate_time);
	PolymodelPerfRecordDuration(marker_time, "Polymodel.Submodel.RenderFaces.Aggregate", Polymodel_perf_submodel_faces_time);
	PolymodelPerfRecordDuration(marker_time, "Polymodel.FacesUnsorted.Aggregate", Polymodel_perf_faces_unsorted_time);
	PolymodelPerfRecordDuration(marker_time, "Polymodel.FacesUnsorted.ScanCull.Aggregate", Polymodel_perf_faces_unsorted_scan_time);
	PolymodelPerfRecordDuration(marker_time, "Polymodel.FacesUnsorted.StateSort.Aggregate", Polymodel_perf_faces_unsorted_sort_time);
	PolymodelPerfRecordDuration(marker_time, "Polymodel.FacesSorted.Aggregate", Polymodel_perf_faces_sorted_time);
	PolymodelPerfRecordDuration(marker_time, "Polymodel.Face.Base.Total.Aggregate", Polymodel_perf_face_base_time);
	PolymodelPerfRecordDuration(marker_time, "Polymodel.Face.Base.KeySetup.Aggregate", Polymodel_perf_face_base_key_time);
	PolymodelPerfRecordDuration(marker_time, "Polymodel.Face.Base.PointSetup.Aggregate", Polymodel_perf_face_base_point_time);
	PolymodelPerfRecordDuration(marker_time, "Polymodel.Face.Base.StateSetup.Aggregate", Polymodel_perf_face_base_state_time);
	PolymodelPerfRecordDuration(marker_time, "Polymodel.Face.Base.PerPixelSetup.Aggregate", Polymodel_perf_face_base_per_pixel_time);
	PolymodelPerfRecordDuration(marker_time, "Polymodel.Face.Base.Clip.Aggregate", Polymodel_perf_face_base_clip_time);
	PolymodelPerfRecordDuration(marker_time, "Polymodel.Face.Base.CopyPoints.Aggregate", Polymodel_perf_face_base_copy_time);
	PolymodelPerfRecordDuration(marker_time, "Polymodel.Face.Base.BatchAdd.Aggregate", Polymodel_perf_face_base_batch_add_time);
	PolymodelPerfRecordDuration(marker_time, "Polymodel.Face.Base.BatchPrepare.Aggregate", Polymodel_perf_face_base_batch_prepare_time);
	PolymodelPerfRecordDuration(marker_time, "Polymodel.Face.Base.DrawPoly.Aggregate", Polymodel_perf_face_base_draw_time);
	PolymodelPerfRecordDuration(marker_time, "Polymodel.Face.Lightmap.Total.Aggregate", Polymodel_perf_lightmap_face_time);
	PolymodelPerfRecordDuration(marker_time, "Polymodel.Face.Lightmap.DrawPoly.Aggregate", Polymodel_perf_lightmap_face_draw_time);
	PolymodelPerfRecordDuration(marker_time, "Polymodel.Face.Specular.Total.Aggregate", Polymodel_perf_specular_face_time);
	PolymodelPerfRecordDuration(marker_time, "Polymodel.Face.Specular.Pass.Aggregate", Polymodel_perf_specular_pass_time);
	PolymodelPerfRecordDuration(marker_time, "Polymodel.Face.Fog.Total.Aggregate", Polymodel_perf_fog_face_time);
	PolymodelPerfRecordDuration(marker_time, "Polymodel.Face.Fog.Pass.Aggregate", Polymodel_perf_fog_pass_time);
	PolymodelPerfRecordDuration(marker_time, "Polymodel.FacingEffect.Aggregate", Polymodel_perf_facing_effect_time);

	PolymodelPerfRecordCounter(marker_time, "Polymodel.Count.DrawPolygonModel", Polymodel_perf_draw_model_count);
	PolymodelPerfRecordCounter(marker_time, "Polymodel.Count.RenderPolygonModel", Polymodel_perf_render_polygon_count);
	PolymodelPerfRecordCounter(marker_time, "Polymodel.Count.SubmodelsVisited", Polymodel_perf_submodel_visit_count);
	PolymodelPerfRecordCounter(marker_time, "Polymodel.Count.SubmodelsDrawn", Polymodel_perf_submodel_draw_count);
	PolymodelPerfRecordCounter(marker_time, "Polymodel.Count.FacesConsidered", Polymodel_perf_faces_considered_count);
	PolymodelPerfRecordCounter(marker_time, "Polymodel.Count.BaseFacesDrawn", Polymodel_perf_base_face_count);
	PolymodelPerfRecordCounter(marker_time, "Polymodel.Count.BaseFacesClipped", Polymodel_perf_base_face_clipped_count);
	PolymodelPerfRecordCounter(marker_time, "Polymodel.Count.AlphaFaces", Polymodel_perf_alpha_face_count);
	PolymodelPerfRecordCounter(marker_time, "Polymodel.Count.LightmapFaces", Polymodel_perf_lightmap_face_count);
	PolymodelPerfRecordCounter(marker_time, "Polymodel.Count.SpecularFaces", Polymodel_perf_specular_face_count);
	PolymodelPerfRecordCounter(marker_time, "Polymodel.Count.FogFaces", Polymodel_perf_fog_face_count);
	PolymodelPerfRecordCounter(marker_time, "Polymodel.Count.DrawPolyCalls", Polymodel_perf_draw_poly_count);
	PolymodelPerfRecordCounter(marker_time, "Polymodel.Count.FacingEffects", Polymodel_perf_facing_effect_count);
	PolymodelPerfRecordCounter(marker_time, "Polymodel.Count.BaseBatchAdds", Polymodel_perf_base_batch_add_count);
	PolymodelPerfRecordCounter(marker_time, "Polymodel.Count.BaseBatchLastHits", Polymodel_perf_base_batch_last_hit_count);
	PolymodelPerfRecordCounter(marker_time, "Polymodel.Count.BaseBatchLookupHits", Polymodel_perf_base_batch_lookup_hit_count);
	PolymodelPerfRecordCounter(marker_time, "Polymodel.Count.BaseBatchMisses", Polymodel_perf_base_batch_miss_count);
	PolymodelPerfRecordCounter(marker_time, "Polymodel.Count.BaseBatchKeyComparisons", Polymodel_perf_base_batch_key_compare_count);
	PolymodelPerfRecordCounter(marker_time, "Polymodel.Count.BaseBatchesCreated", Polymodel_perf_base_batch_created_count);
	PolymodelPerfRecordCounter(marker_time, "Polymodel.Count.BaseBatchesFlushed", Polymodel_perf_base_batch_flushed_count);
	PolymodelPerfRecordCounter(marker_time, "Polymodel.Count.BaseBatchFlushes", Polymodel_perf_base_batch_flush_count);
	PolymodelPerfRecordCounter(marker_time, "Polymodel.Count.BaseBatchMaxBatchesPerFlush", Polymodel_perf_base_batch_max_batches_per_flush);
	PolymodelPerfRecordCounter(marker_time, "Polymodel.Count.BaseBatchMaxFacesPerBatch", Polymodel_perf_base_batch_max_faces_per_batch);
}

void PolymodelPerfAddDrawModel(double start_time)
{
	if (!Perf_markers_enabled)
		return;

	PolymodelPerfTouch(start_time);
	Polymodel_perf_draw_model_count++;
	Polymodel_perf_draw_model_time += PerfMarkersNow() - start_time;
}

void PolymodelPerfAddDrawModelSetup(double start_time)
{
	if (!Perf_markers_enabled)
		return;

	PolymodelPerfTouch(start_time);
	Polymodel_perf_draw_model_setup_time += PerfMarkersNow() - start_time;
}

void PolymodelSetCockpitBatching(bool enabled)
{
	Polymodel_cockpit_batching = enabled;
}

void PolymodelSetCockpitTransparentFaceFilter(int model_num, uint submodel_mask, bool enabled)
{
	Polymodel_cockpit_transparent_face_filter_enabled = enabled;
	Polymodel_cockpit_transparent_face_filter_model = model_num;
	Polymodel_cockpit_transparent_face_filter_mask = submodel_mask;
}

static int ModelFaceSortFunc(const short *a, const short *b)
{
	float az,bz;

	az = face_depth[*a];
	bz = face_depth[*b];

	if (az < bz)
		return -1;
	else if (az > bz)
		return 1;
	else
		return 0;
}

#ifdef _DEBUG
void model_draw_outline(int nverts,g3Point **pointlist)
{
   int i;

   for (i=0;i<nverts-1;i++)
      g3_DrawLine(GR_RGB(255,255,255),pointlist[i],pointlist[i+1]);

   g3_DrawLine(GR_RGB(255,255,255),pointlist[i],pointlist[0]);

}

void DrawSubmodelFaceOutline (int nv,g3Point **pointlist)
{
	int i;
	g3Point tpnt[64];
	g3Point *tpnt_list[64];

	ASSERT (nv<64);

	for (i=0;i<nv;i++)
	{
		tpnt[i]=*pointlist[i];
		tpnt_list[i]=&tpnt[i];
	}

	for (i=0;i<nv-1;i++)
		g3_DrawLine(GR_RGB(255,255,0),tpnt_list[i],tpnt_list[i+1]);
	g3_DrawLine(GR_RGB(255,255,0),tpnt_list[i],tpnt_list[0]);

}
#endif

#define CROSS_WIDTH 8.0
int Lightmap_debug_subnum=-1;
int Lightmap_debug_facenum=-1;
int Lightmap_debug_model=-1;

static bool UsePerPixelPolymodelLighting();
static light_state GetPolymodelGouraudLightingState();
static void SetPolymodelGouraudPointLighting(g3Point& point, const vector& normal);

struct PolymodelBatchedFace
{
	int nv;
	size_t first_point;
	uint32_t model_id;
	uint32_t submodel_id;
	uint32_t face_id;
	uint32_t retained_exclusion_bits;
	bool retained_payload_representable;
};

static void AppendPolymodelBatchPoints(PolymodelBatchedFace& face,
	std::vector<g3Point>& points, std::vector<vector>* retained_positions,
	g3Point **pointlist, int nv)
{
	face.nv = nv;
	face.first_point = points.size();
	points.resize(face.first_point + static_cast<size_t>(nv));
	if (retained_positions)
		retained_positions->resize(points.size());
	for (int i = 0; i < nv; i++)
	{
		g3Point& point = points[face.first_point + static_cast<size_t>(i)];
		point = *pointlist[i];
		point.p3_flags &= ~PF_TEMP_POINT;
		if (!(point.p3_flags & PF_PROJECTED))
			g3_ProjectPoint(&point);
		if (retained_positions)
			(*retained_positions)[face.first_point + static_cast<size_t>(i)] =
				point.p3_motion_world_valid ? point.p3_motion_world_pos :
				point.p3_vecPreRot;
	}
}

struct PolymodelBaseFaceBatchKey
{
	int bitmap_handle;
	int overlay_map;
	texture_type texture_type_value;
	ubyte overlay_type;
	light_state lighting;
	color_model color_model_value;
	sbyte alpha_type;
	ubyte alpha_value;
	ddgr_color flat_color;
	vector light_direction;

	bool Equals(const PolymodelBaseFaceBatchKey& other) const
	{
		return bitmap_handle == other.bitmap_handle &&
			overlay_map == other.overlay_map &&
			texture_type_value == other.texture_type_value &&
			overlay_type == other.overlay_type &&
			lighting == other.lighting &&
			color_model_value == other.color_model_value &&
			alpha_type == other.alpha_type &&
			alpha_value == other.alpha_value &&
			flat_color == other.flat_color &&
			(lighting != LS_PHONG || light_direction == other.light_direction);
	}
};

static size_t PolymodelBaseFaceBatchHashCombine(size_t seed, size_t value)
{
	return seed ^ (value + 0x9e3779b9 + (seed << 6) + (seed >> 2));
}

static size_t PolymodelBaseFaceBatchHashInt(size_t seed, int value)
{
	return PolymodelBaseFaceBatchHashCombine(seed, std::hash<int>()(value));
}

static size_t PolymodelBaseFaceBatchHashFloat(size_t seed, float value)
{
	uint32_t bits = 0;
	memcpy(&bits, &value, sizeof(bits));
	return PolymodelBaseFaceBatchHashCombine(seed, std::hash<uint32_t>()(bits));
}

struct PolymodelBaseFaceBatchKeyHasher
{
	size_t operator()(const PolymodelBaseFaceBatchKey& key) const
	{
		size_t seed = 0;
		seed = PolymodelBaseFaceBatchHashInt(seed, key.bitmap_handle);
		seed = PolymodelBaseFaceBatchHashInt(seed, key.overlay_map);
		seed = PolymodelBaseFaceBatchHashInt(seed, (int)key.texture_type_value);
		seed = PolymodelBaseFaceBatchHashInt(seed, key.overlay_type);
		seed = PolymodelBaseFaceBatchHashInt(seed, (int)key.lighting);
		seed = PolymodelBaseFaceBatchHashInt(seed, (int)key.color_model_value);
		seed = PolymodelBaseFaceBatchHashInt(seed, key.alpha_type);
		seed = PolymodelBaseFaceBatchHashInt(seed, key.alpha_value);
		seed = PolymodelBaseFaceBatchHashCombine(seed, std::hash<unsigned int>()((unsigned int)key.flat_color));
		if (key.lighting == LS_PHONG)
		{
			seed = PolymodelBaseFaceBatchHashFloat(seed, key.light_direction.x);
			seed = PolymodelBaseFaceBatchHashFloat(seed, key.light_direction.y);
			seed = PolymodelBaseFaceBatchHashFloat(seed, key.light_direction.z);
		}
		return seed;
	}
};

struct PolymodelBaseFaceBatchKeyEqual
{
	bool operator()(const PolymodelBaseFaceBatchKey& left, const PolymodelBaseFaceBatchKey& right) const
	{
		if (Perf_markers_enabled)
			Polymodel_perf_base_batch_key_compare_count++;
		return left.Equals(right);
	}
};

struct PolymodelBaseFaceBatch
{
	PolymodelBaseFaceBatchKey key;
	std::vector<PolymodelBatchedFace> faces;
};

class PolymodelBaseFaceBatcher
{
public:
	void Reserve(size_t batch_count, size_t face_count)
	{
		if (batch_count > m_batches.capacity())
			m_batches.reserve(batch_count);
		if (batch_count > m_batch_lookup.bucket_count())
			m_batch_lookup.reserve(batch_count);
		if (face_count > m_batch_items.capacity())
			m_batch_items.reserve(face_count);
		if (face_count > m_retained_batch_items.capacity())
			m_retained_batch_items.reserve(face_count);
		const size_t point_count = face_count * 4u;
		if (point_count > m_points.capacity())
			m_points.reserve(point_count);
		if (point_count > m_point_pointers.capacity())
			m_point_pointers.reserve(point_count);
		if (point_count > m_retained_positions.capacity())
			m_retained_positions.reserve(point_count);
	}

	void CopyPoints(PolymodelBatchedFace& face, g3Point **pointlist, int nv)
	{
		AppendPolymodelBatchPoints(face, m_points, &m_retained_positions,
			pointlist, nv);
	}

	PolymodelBatchedFace& Add(const PolymodelBaseFaceBatchKey& key)
	{
		if (Perf_markers_enabled)
			Polymodel_perf_base_batch_add_count++;

		if (m_last_batch_index != (size_t)-1 && m_last_batch_index < m_batches.size())
		{
			if (Perf_markers_enabled)
				Polymodel_perf_base_batch_key_compare_count++;
			if (m_batches[m_last_batch_index].key.Equals(key))
			{
				PolymodelBaseFaceBatch& batch = m_batches[m_last_batch_index];
				batch.faces.emplace_back();
				if (Perf_markers_enabled)
					Polymodel_perf_base_batch_last_hit_count++;
				PolymodelPerfUpdateMax(Polymodel_perf_base_batch_max_faces_per_batch, batch.faces.size());
				return batch.faces.back();
			}
		}

		if (m_batches.empty())
		{
			m_batches.reserve(16);
			m_batch_lookup.reserve(16);
		}

		BatchLookup::iterator iter = m_batch_lookup.find(key);
		if (iter != m_batch_lookup.end())
		{
			PolymodelBaseFaceBatch& batch = m_batches[iter->second];
			batch.faces.emplace_back();
			m_last_batch_index = iter->second;
			if (Perf_markers_enabled)
				Polymodel_perf_base_batch_lookup_hit_count++;
			PolymodelPerfUpdateMax(Polymodel_perf_base_batch_max_faces_per_batch, batch.faces.size());
			return batch.faces.back();
		}

		const size_t batch_index = m_batches.size();
		m_batches.emplace_back();
		PolymodelBaseFaceBatch& batch = m_batches.back();
		batch.key = key;
		batch.faces.reserve(4);
		batch.faces.emplace_back();
		m_batch_lookup.emplace(batch.key, batch_index);
		m_last_batch_index = batch_index;
		if (Perf_markers_enabled)
		{
			Polymodel_perf_base_batch_miss_count++;
			Polymodel_perf_base_batch_created_count++;
		}
		PolymodelPerfUpdateMax(Polymodel_perf_base_batch_max_faces_per_batch, batch.faces.size());
		return batch.faces.back();
	}

	void Flush()
	{
		if (m_batches.empty())
			return;

		if (Perf_markers_enabled)
		{
			Polymodel_perf_base_batch_flush_count++;
			Polymodel_perf_base_batch_flushed_count += (int)m_batches.size();
			PolymodelPerfUpdateMax(Polymodel_perf_base_batch_max_batches_per_flush, m_batches.size());
		}
		m_point_pointers.resize(m_points.size());
		for (size_t point_index = 0; point_index < m_points.size(); ++point_index)
			m_point_pointers[point_index] = &m_points[point_index];

		for (size_t i = 0; i < m_batches.size(); i++)
		{
			PolymodelBaseFaceBatch& batch = m_batches[i];
			if (batch.faces.empty())
				continue;

			double state_setup_start_time = PolymodelPerfNow();
			rend_SetOverlayType(batch.key.overlay_type);
			if (batch.key.overlay_type != OT_NONE)
				rend_SetOverlayMap(batch.key.overlay_map);
			rend_SetColorModel(batch.key.color_model_value);
			rend_SetTextureType(batch.key.texture_type_value);
			if (batch.key.lighting == LS_PHONG)
				rend_SetPerPixelLightingDirection(&batch.key.light_direction);
			rend_SetLighting(batch.key.lighting);
			rend_SetFlatColor(batch.key.flat_color);
			rend_SetAlphaValue(batch.key.alpha_value);
			rend_SetAlphaType(batch.key.alpha_type);
			PolymodelPerfAdd(Polymodel_perf_face_base_state_time, state_setup_start_time);
			PolymodelPerfAdd(Polymodel_perf_face_base_time, state_setup_start_time);

			double batch_prepare_start_time = PolymodelPerfNow();
			const bool retained = rend_SupportsRetainedPolymodels();
			m_batch_items.resize(retained ? 0 : batch.faces.size());
			m_retained_batch_items.resize(retained ? batch.faces.size() : 0);
			for (size_t face_index = 0; face_index < batch.faces.size();
				face_index++)
			{
				PolymodelBatchedFace &face = batch.faces[face_index];
				g3Point **pointlist = m_point_pointers.data() + face.first_point;
				if (retained)
				{
					renderer_retained_poly_batch_item &item =
						m_retained_batch_items[face_index];
					item.pointlist = pointlist;
					item.world_positions = m_retained_positions.data() +
						face.first_point;
					item.nv = face.nv;
					item.source_kind = RENDERER_RETAINED_STATIC_POLYMODEL;
					item.source_id = face.model_id;
					item.source_generation = 1;
					item.subobject = face.submodel_id;
					item.face = face.face_id;
					item.classification = face.face_id;
					item.exclusion_bits = face.retained_exclusion_bits;
					item.payload_representable =
						face.retained_payload_representable;
				}
				else
				{
					m_batch_items[face_index].pointlist = pointlist;
					m_batch_items[face_index].nv = face.nv;
				}
			}
			PolymodelPerfAdd(Polymodel_perf_face_base_batch_prepare_time, batch_prepare_start_time);
			PolymodelPerfAdd(Polymodel_perf_face_base_time, batch_prepare_start_time);

			double draw_start_time = PolymodelPerfNow();
			if (retained)
				rend_DrawRetainedPolygon3DBatch(batch.key.bitmap_handle,
					m_retained_batch_items.data(),
					static_cast<int>(m_retained_batch_items.size()),
					MAP_TYPE_BITMAP);
			else
				rend_DrawPolygon3DBatch(batch.key.bitmap_handle,
					m_batch_items.data(), static_cast<int>(m_batch_items.size()),
					MAP_TYPE_BITMAP);
			if (Perf_markers_enabled)
				Polymodel_perf_draw_poly_count++;
			PolymodelPerfAdd(Polymodel_perf_face_base_draw_time, draw_start_time);
			PolymodelPerfAdd(Polymodel_perf_face_base_time, draw_start_time);
		}

		if (Polymodel_light_type == POLYMODEL_LIGHTING_GOURAUD && UsePerPixelPolymodelLighting())
			rend_SetLighting(LS_GOURAUD);

		m_batches.clear();
		m_batch_lookup.clear();
		m_points.clear();
		m_point_pointers.clear();
		m_retained_positions.clear();
		m_last_batch_index = (size_t)-1;
	}

private:
	typedef std::unordered_map<PolymodelBaseFaceBatchKey, size_t,
		PolymodelBaseFaceBatchKeyHasher, PolymodelBaseFaceBatchKeyEqual> BatchLookup;

	std::vector<PolymodelBaseFaceBatch> m_batches;
	std::vector<renderer_poly_batch_item> m_batch_items;
	std::vector<renderer_retained_poly_batch_item> m_retained_batch_items;
	std::vector<g3Point> m_points;
	std::vector<g3Point*> m_point_pointers;
	std::vector<vector> m_retained_positions;
	BatchLookup m_batch_lookup;
	size_t m_last_batch_index = (size_t)-1;
};

class PolymodelFogFaceBatcher
{
public:
	void Reserve(size_t face_count)
	{
		if (face_count > m_faces.capacity())
			m_faces.reserve(face_count);
		if (face_count > m_batch_items.capacity())
			m_batch_items.reserve(face_count);
		const size_t point_count = face_count * 4u;
		if (point_count > m_points.capacity())
			m_points.reserve(point_count);
		if (point_count > m_point_pointers.capacity())
			m_point_pointers.reserve(point_count);
	}

	void Add(g3Point **pointlist, int nv)
	{
		m_faces.emplace_back();
		AppendPolymodelBatchPoints(m_faces.back(), m_points, nullptr,
			pointlist, nv);
	}

	void Flush()
	{
		if (m_faces.empty())
			return;

		m_batch_items.resize(m_faces.size());
		m_point_pointers.resize(m_points.size());
		for (size_t point_index = 0; point_index < m_points.size(); ++point_index)
			m_point_pointers[point_index] = &m_points[point_index];
		for (size_t face_index = 0; face_index < m_faces.size(); face_index++)
		{
			m_batch_items[face_index].pointlist = m_point_pointers.data() +
				m_faces[face_index].first_point;
			m_batch_items[face_index].nv = m_faces[face_index].nv;
		}

		rend_DrawPolygon3DBatch(0, m_batch_items.data(),
			static_cast<int>(m_batch_items.size()), MAP_TYPE_BITMAP);
		if (Perf_markers_enabled)
			Polymodel_perf_draw_poly_count++;
		m_faces.clear();
		m_points.clear();
		m_point_pointers.clear();
	}

private:
	std::vector<PolymodelBatchedFace> m_faces;
	std::vector<renderer_poly_batch_item> m_batch_items;
	std::vector<g3Point> m_points;
	std::vector<g3Point*> m_point_pointers;
};

static PolymodelBaseFaceBatcher *Polymodel_active_opaque_batcher = nullptr;
static PolymodelBaseFaceBatcher *Polymodel_active_alpha_batcher = nullptr;

static ddgr_color GetPolymodelCustomFlatColor(ddgr_color base_color)
{
	int r = GR_COLOR_RED(base_color);
	int g = GR_COLOR_GREEN(base_color);
	int b = GR_COLOR_BLUE(base_color);

	if (Polymodel_light_type == POLYMODEL_LIGHTING_GOURAUD)
	{
		if (Polymodel_use_effect && Polymodel_effect.type & PEF_COLOR)
		{
			r = Polymodel_effect.r * (float)r * Polylighting_static_red;
			g = Polymodel_effect.g * (float)g * Polylighting_static_green;
			b = Polymodel_effect.b * (float)b * Polylighting_static_blue;
		}
		else
		{
			r = (float)r * Polylighting_static_red;
			g = (float)g * Polylighting_static_green;
			b = (float)b * Polylighting_static_blue;
		}
	}

	return GR_RGB(r, g, b);
}

static texture *GetPolymodelFaceTexture(poly_model *pm, bsp_info *sm, polyface *fp)
{
	texture *texp = NULL;
	if (fp->texnum != -1)
		texp = &GameTextures[pm->textures[fp->texnum]];

	if (texp && (sm->flags & SOF_CUSTOM) && Polymodel_use_effect && (Polymodel_effect.type & PEF_CUSTOM_TEXTURE))
		texp = &GameTextures[Polymodel_effect.custom_texture];

	return texp;
}

static bool PolymodelFaceUsesAlpha(poly_model *pm, bsp_info *sm, int facenum)
{
	polyface *fp = &sm->faces[facenum];
	texture *texp = GetPolymodelFaceTexture(pm, sm, fp);
	if (texp && ((texp->flags & (TF_ALPHA | TF_SATURATE)) || texp->alpha < 0.99f))
		return true;

	if (!sm->alpha)
		return false;

	for (int i = 0; i < fp->nverts; i++)
	{
		if (sm->alpha[fp->vertnums[i]] < 0.99f)
			return true;
	}

	return false;
}

static int PolymodelCountFaces(poly_model *pm)
{
	int faces = 0;
	for (int i = 0; i < pm->n_models; i++)
		faces += pm->submodel[i].num_faces;
	return faces;
}

static bool PolymodelFaceUsesCockpitTransparentMaterial(poly_model *pm, bsp_info *sm, int facenum)
{
	polyface *fp = &sm->faces[facenum];
	texture *texp = GetPolymodelFaceTexture(pm, sm, fp);
	if (texp && ((texp->flags & (TF_ALPHA | TF_SATURATE | TF_BREAKABLE)) || texp->alpha < 0.99f))
		return true;

	if (!sm->alpha)
		return false;

	for (int i = 0; i < fp->nverts; i++)
	{
		if (sm->alpha[fp->vertnums[i]] < 0.99f)
			return true;
	}

	return false;
}

static bool PolymodelShouldSkipTransparentCockpitFace(poly_model *pm, bsp_info *sm, int facenum)
{
	if (!Polymodel_cockpit_transparent_face_filter_enabled)
		return false;
	if (Polymodel_cockpit_transparent_face_filter_model < 0 ||
		pm != &Poly_models[Polymodel_cockpit_transparent_face_filter_model])
	{
		return false;
	}

	int submodel_num = sm - pm->submodel;
	if (submodel_num < 0 || submodel_num >= 32)
		return false;
	if (!(Polymodel_cockpit_transparent_face_filter_mask & (1u << submodel_num)))
		return false;

	return PolymodelFaceUsesCockpitTransparentMaterial(pm, sm, facenum);
}

static bool TryBatchSubmodelBaseFace(poly_model *pm, bsp_info *sm, int facenum,
	PolymodelBaseFaceBatcher& batcher, bool allow_alpha = false)
{
	polyface *fp = &sm->faces[facenum];
	if (PolymodelShouldSkipTransparentCockpitFace(pm, sm, facenum))
		return true;
	if (fp->nverts < 3 || fp->nverts >= 100)
		return false;

	if (StateLimited)
		return false;
	const bool lightmap_lit = Polymodel_light_type == POLYMODEL_LIGHTING_LIGHTMAP;

	if (Polymodel_use_effect && (Polymodel_effect.type & PEF_ALPHA))
		return false;

	texture *texp = GetPolymodelFaceTexture(pm, sm, fp);

	if (texp && (Polymodel_effect.type & PEF_BUMPMAPPED) && texp->bumpmap != -1 &&
		Polymodel_light_type == POLYMODEL_LIGHTING_GOURAUD)
		return false;

	double face_start_time = PolymodelPerfNow();
	PolymodelPerfTouch(face_start_time);
	double key_setup_start_time = face_start_time;
	g3Codes face_cc;
	face_cc.cc_and = 0xff;
	face_cc.cc_or = 0;
	triangulated_faces[facenum] = 0;

	PolymodelBaseFaceBatchKey key = {};
	key.bitmap_handle = 0;
	key.overlay_map = 0;
	key.texture_type_value = TT_FLAT;
	key.overlay_type = OT_NONE;
	key.lighting = LS_NONE;
	key.color_model_value = CM_RGB;
	key.alpha_type = ATF_CONSTANT + ATF_VERTEX;
	key.alpha_value = 255;
	key.flat_color = GetPolymodelCustomFlatColor(fp->color);
	if (Polymodel_light_direction)
		key.light_direction = *Polymodel_light_direction;

	int modelnum = sm - pm->submodel;
	int lightmap_lmi_handle = -1;
	if (lightmap_lit)
	{
		lightmap_lmi_handle = Polylighting_lightmap_object->lightmap_faces[modelnum][facenum].lmi_handle;
		key.overlay_type = OT_BLEND;
		key.overlay_map = LightmapInfo[lightmap_lmi_handle].lm_handle;
		if (UsePerPixelPolymodelLighting())
		{
			renderer_per_pixel_light per_pixel_lights[RENDERER_MAX_PER_PIXEL_DYNAMIC_LIGHTS];
			int per_pixel_light_count = GetPerPixelLightmapLights((ushort)lightmap_lmi_handle, per_pixel_lights,
				RENDERER_MAX_PER_PIXEL_DYNAMIC_LIGHTS);
			if (per_pixel_light_count > 0)
			{
				PolymodelPerfAdd(Polymodel_perf_face_base_key_time, key_setup_start_time);
				return false;
			}
		}
	}

	if (texp)
	{
		if (!allow_alpha && (texp->flags & (TF_ALPHA | TF_SATURATE)))
		{
			PolymodelPerfAdd(Polymodel_perf_face_base_key_time, key_setup_start_time);
			return false;
		}

		key.bitmap_handle = GetTextureBitmap(texp - GameTextures, 0);
		key.texture_type_value = TT_LINEAR;
		key.alpha_value = (ubyte)(texp->alpha * 255.0f);
		if (texp->flags & TF_SATURATE)
			key.alpha_type = AT_SATURATE_CONSTANT_VERTEX;
		else if (texp->flags & TF_ALPHA)
			key.alpha_type = ATF_CONSTANT + ATF_VERTEX;
		else
			key.alpha_type = ATF_TEXTURE + ATF_VERTEX;
		key.flat_color = 0;
		if (Polymodel_light_type == POLYMODEL_LIGHTING_GOURAUD)
			key.lighting = GetPolymodelGouraudLightingState();

		if (texp->flags & TF_LIGHT)
		{
			key.lighting = LS_FLAT_GOURAUD;
			key.flat_color = GR_RGB(255, 255, 255);
		}

		if (Polymodel_use_effect && (Polymodel_effect.type & PEF_CUSTOM_COLOR) &&
			(texp - GameTextures) == Multicolor_texture)
		{
			key.lighting = LS_FLAT_GOURAUD;
			key.flat_color = GetPolymodelCustomFlatColor(Polymodel_effect.custom_color);
		}
	}

	float uchange = 0, vchange = 0;
	if (texp && texp->slide_u != 0)
	{
		int int_time = Gametime / texp->slide_u;
		float norm_time = Gametime - (int_time * texp->slide_u);
		norm_time /= texp->slide_u;
		uchange = norm_time;
	}

	if (texp && texp->slide_v != 0)
	{
		int int_time = Gametime / texp->slide_v;
		float norm_time = Gametime - (int_time * texp->slide_v);
		norm_time /= texp->slide_v;
		vchange = norm_time;
	}
	PolymodelPerfAdd(Polymodel_perf_face_base_key_time, key_setup_start_time);

	g3Point *source_pointlist[100];

	double point_setup_start_time = PolymodelPerfNow();
	for (int t = 0; t < fp->nverts; t++)
	{
		g3Point *p = &Robot_points[fp->vertnums[t]];
		source_pointlist[t] = p;
		if (texp)
		{
			p->p3_uvl.u = fp->u[t] + uchange;
			p->p3_uvl.v = fp->v[t] + vchange;
			p->p3_uvl.a = sm->alpha[fp->vertnums[t]];
			p->p3_flags |= PF_UV + PF_RGBA + PF_L;
		}

		if (lightmap_lit)
		{
			p->p3_flags |= PF_UV2;
			p->p3_uvl.u2 = Polylighting_lightmap_object->lightmap_faces[modelnum][facenum].u2[t];
			p->p3_uvl.v2 = Polylighting_lightmap_object->lightmap_faces[modelnum][facenum].v2[t];
		}

		face_cc.cc_or |= p->p3_codes;
		face_cc.cc_and &= p->p3_codes;
	}
	PolymodelPerfAdd(Polymodel_perf_face_base_point_time, point_setup_start_time);

	if (face_cc.cc_and)
	{
		if (Perf_markers_enabled)
			Polymodel_perf_base_face_count++;
		PolymodelPerfAdd(Polymodel_perf_face_base_time, face_start_time);
		return true;
	}

	int draw_nv = fp->nverts;
	g3Point **draw_pointlist = source_pointlist;
	bool was_clipped = false;
	if (face_cc.cc_or)
	{
		double clip_start_time = PolymodelPerfNow();
		if (Polymodel_use_effect && (Polymodel_effect.type &
			(PEF_FOGGED_MODEL | PEF_SPECULAR_MODEL | PEF_SPECULAR_FACES)))
		{
			PolymodelPerfAdd(Polymodel_perf_face_base_clip_time, clip_start_time);
			return false;
		}

		draw_pointlist = g3_ClipPolygon(source_pointlist, &draw_nv, &face_cc);
		was_clipped = true;
		if (draw_nv == 0 || (face_cc.cc_or & CC_BEHIND) || face_cc.cc_and)
		{
			g3_FreeTempPoints(draw_pointlist, draw_nv);
			if (Perf_markers_enabled)
				Polymodel_perf_base_face_count++;
			PolymodelPerfAdd(Polymodel_perf_face_base_clip_time, clip_start_time);
			PolymodelPerfAdd(Polymodel_perf_face_base_time, face_start_time);
			return true;
		}

		if (draw_nv < 3 || draw_nv >= 100)
		{
			g3_FreeTempPoints(draw_pointlist, draw_nv);
			PolymodelPerfAdd(Polymodel_perf_face_base_clip_time, clip_start_time);
			return false;
		}
		if (Perf_markers_enabled)
			Polymodel_perf_base_face_clipped_count++;
		PolymodelPerfAdd(Polymodel_perf_face_base_clip_time, clip_start_time);
	}

	double batch_add_start_time = PolymodelPerfNow();
	PolymodelBatchedFace& batched_face = batcher.Add(key);
	PolymodelPerfAdd(Polymodel_perf_face_base_batch_add_time, batch_add_start_time);

	double copy_start_time = PolymodelPerfNow();
	batcher.CopyPoints(batched_face, draw_pointlist, draw_nv);
	batched_face.model_id = static_cast<uint32_t>(pm - Poly_models);
	batched_face.submodel_id = static_cast<uint32_t>(modelnum);
	batched_face.face_id = static_cast<uint32_t>(facenum);
	batched_face.retained_exclusion_bits = 0;
	batched_face.retained_payload_representable = key.lighting != LS_PHONG;
	if (was_clipped ||
		(Polymodel_use_effect && (Polymodel_effect.type & PEF_DEFORM)) ||
		(sm->flags & SOF_JITTER))
		batched_face.retained_exclusion_bits |=
			RENDERER_RETAINED_EXCLUDE_GENERATED_OR_MORPHED;
	if (sm->flags & SOF_CUSTOM)
		batched_face.retained_exclusion_bits |=
			RENDERER_RETAINED_EXCLUDE_SOF_CUSTOM;
	for (int vertex = 0; vertex < draw_nv; ++vertex)
	{
		const g3Point &point = *draw_pointlist[vertex];
		if (!point.p3_motion_world_valid)
			batched_face.retained_payload_representable = false;
	}
	PolymodelPerfAdd(Polymodel_perf_face_base_copy_time, copy_start_time);

	if (was_clipped)
		g3_FreeTempPoints(draw_pointlist, draw_nv);

	if (Perf_markers_enabled)
		Polymodel_perf_base_face_count++;
	PolymodelPerfAdd(Polymodel_perf_face_base_time, face_start_time);
	return true;
}


inline void RenderSubmodelFace (poly_model *pm,bsp_info *sm,int facenum)
{
	double face_start_time = PolymodelPerfNow();
	PolymodelPerfTouch(face_start_time);
	if (PolymodelShouldSkipTransparentCockpitFace(pm, sm, facenum))
		return;
	if (Perf_markers_enabled)
		Polymodel_perf_base_face_count++;
	
	g3Point	*pointlist[100];
	int bm_handle;
	int smooth=0;
	polyface *fp=&sm->faces[facenum];
	int modelnum=sm-pm->submodel;
	texture *texp=NULL;
	int t;
	int custom=0;
	g3Codes face_cc;
	int triface=0;
	int lightmap_lmi_handle = -1;
	int per_pixel_light_count = 0;
	renderer_per_pixel_light per_pixel_lights[RENDERER_MAX_PER_PIXEL_DYNAMIC_LIGHTS];

	face_cc.cc_and=0xff;
	face_cc.cc_or=0;

	triangulated_faces[facenum]=0;


	if (sm->flags & SOF_CUSTOM)
		custom=1;
	
	// Setup texturing	
	if (fp->texnum!=-1)
		texp=&GameTextures[pm->textures[fp->texnum]];
	
	if (texp && custom && Polymodel_use_effect && (Polymodel_effect.type & PEF_CUSTOM_TEXTURE))
		texp=&GameTextures[Polymodel_effect.custom_texture];
		
	// Set radiosity lightmaps if needed
	if (Polymodel_light_type==POLYMODEL_LIGHTING_LIGHTMAP)
	{
		lightmap_lmi_handle = Polylighting_lightmap_object->lightmap_faces[modelnum][facenum].lmi_handle;
		rend_SetOverlayMap (LightmapInfo[lightmap_lmi_handle].lm_handle);
		
	}

	// Do bump mapping
	if ((Polymodel_effect.type & PEF_BUMPMAPPED) &&texp && texp->bumpmap!=-1 && Polymodel_light_type==POLYMODEL_LIGHTING_GOURAUD )
	{
		rend_SetOverlayType (OT_NONE);
		rend_SetBumpmapReadyState(1,texp->bumpmap);
		if ((GameTextures[fp->texnum].flags & TF_SMOOTH_SPECULAR))
			smooth=1;
	}
				
	float uchange=0,vchange=0;

	// Figure out if there is any texture sliding
	if (texp && texp->slide_u!=0)
	{
		int int_time=Gametime/texp->slide_u;
		float norm_time=Gametime-(int_time*texp->slide_u);
		norm_time/=texp->slide_u;

		uchange=norm_time;
	}

	if (texp && texp->slide_v!=0)
	{
		int int_time=Gametime/texp->slide_v;
		float norm_time=Gametime-(int_time*texp->slide_v);
		norm_time/=texp->slide_v;
		vchange=norm_time;
	}

	ASSERT (fp->nverts<100);
				
	// Setup the points for this face
	double point_setup_start_time = PolymodelPerfNow();
	for (t=0;t<fp->nverts;t++) 
	{
		g3Point *p = &Robot_points[fp->vertnums[t]];
		pointlist[t] = p;

		if (texp)
		{
			p->p3_uvl.u = fp->u[t]+uchange;
			p->p3_uvl.v = fp->v[t]+vchange;
			p->p3_uvl.a = sm->alpha[fp->vertnums[t]];
			p->p3_flags |=PF_UV+PF_RGBA+PF_L;

			// Assign bump mapping coords
			if ((Polymodel_effect.type & PEF_BUMPMAPPED) && texp->bumpmap!=-1 && Polymodel_light_type==POLYMODEL_LIGHTING_GOURAUD )
			{
				p->p3_flags|=PF_UV2;

				vector vert=sm->verts[fp->vertnums[t]];

				vector vertnorm;
		
				if (smooth)
					vertnorm=sm->vertnorms[fp->vertnums[t]];
				else
					vertnorm=fp->normal;

				vector subvec=Bump_view_pos-vert;
				vm_NormalizeVectorFast (&subvec);

				vector incident_norm=vert-Polymodel_bump_pos;
				vm_NormalizeVectorFast (&incident_norm);
			
				float d=incident_norm * vertnorm;
				vector upvec=d * vertnorm;
				incident_norm-=(2*upvec);

				float dotp=(subvec * incident_norm);

				if (dotp<0) 
					dotp=0;
				if (dotp>1)
					dotp=1;
								
				float val=dotp*.5;

				p->p3_uvl.u2=val;
				p->p3_uvl.v2=val;
			}
		}

		if (Polymodel_light_type==POLYMODEL_LIGHTING_LIGHTMAP)
		{
			p->p3_flags |=PF_UV2;
			p->p3_uvl.u2=Polylighting_lightmap_object->lightmap_faces[modelnum][facenum].u2[t];
			p->p3_uvl.v2=Polylighting_lightmap_object->lightmap_faces[modelnum][facenum].v2[t];
		}

		face_cc.cc_or|=p->p3_codes;
		face_cc.cc_and&=p->p3_codes;
	}
	PolymodelPerfAdd(Polymodel_perf_face_base_point_time, point_setup_start_time);

	if (face_cc.cc_or && Polymodel_use_effect && (Polymodel_effect.type & (PEF_FOGGED_MODEL|PEF_SPECULAR_MODEL|PEF_SPECULAR_FACES)))
	{
		triface=1;
		triangulated_faces[facenum]=1;
	}

	// If there is a texture, set it up 
	double state_setup_start_time = PolymodelPerfNow();
	if (texp)
	{
		bm_handle=GetTextureBitmap(texp-GameTextures,0);

		rend_SetTextureType (TT_LINEAR);
		if (Polymodel_light_type==POLYMODEL_LIGHTING_GOURAUD)
		{
			if (UsePerPixelPolymodelLighting())
				rend_SetPerPixelLightingDirection(Polymodel_light_direction);
			rend_SetLighting(GetPolymodelGouraudLightingState());
		}

		// If this is a light texture, make the texture full bright
		if (texp->flags & TF_LIGHT)
		{	
			rend_SetLighting (LS_FLAT_GOURAUD);
			rend_SetFlatColor (GR_RGB(255,255,255));
		}

		// Setup custom color if there is one
		if (Polymodel_use_effect && (Polymodel_effect.type & PEF_CUSTOM_COLOR) && (texp-GameTextures)==Multicolor_texture)
		{
			rend_SetLighting (LS_FLAT_GOURAUD);
			
			int r=GR_COLOR_RED (Polymodel_effect.custom_color);
			int g=GR_COLOR_GREEN (Polymodel_effect.custom_color);
			int b=GR_COLOR_BLUE (Polymodel_effect.custom_color);
		
			if (Polymodel_light_type==POLYMODEL_LIGHTING_GOURAUD)
			{
				if (Polymodel_use_effect && Polymodel_effect.type & PEF_COLOR)
				{	
					r=Polymodel_effect.r*(float)r*Polylighting_static_red;
					g=Polymodel_effect.g*(float)g*Polylighting_static_green;
					b=Polymodel_effect.b*(float)b*Polylighting_static_blue;
				}
				else
				{
					r=(float)r*Polylighting_static_red;
					g=(float)g*Polylighting_static_green;
					b=(float)b*Polylighting_static_blue;
				}
			}

			rend_SetFlatColor(GR_RGB(r,g,b));
		}

		

		if (Polymodel_use_effect && (Polymodel_effect.type & PEF_ALPHA))
			rend_SetAlphaValue (texp->alpha * Polymodel_effect.alpha * 255 );
		else
			rend_SetAlphaValue (texp->alpha*255);

		if (texp->flags & TF_SATURATE)
			rend_SetAlphaType (AT_SATURATE_CONSTANT_VERTEX);
		else
		{
			if ((texp->flags & TF_ALPHA) || (Polymodel_use_effect && (Polymodel_effect.type & PEF_ALPHA)))
				rend_SetAlphaType (ATF_CONSTANT+ATF_VERTEX);
			else
				rend_SetAlphaType (ATF_TEXTURE+ATF_VERTEX);
		}
	}
	else
	{
		rend_SetAlphaType (ATF_CONSTANT+ATF_VERTEX);
		if (Polymodel_use_effect && (Polymodel_effect.type & PEF_ALPHA))
			rend_SetAlphaValue (Polymodel_effect.alpha * 255 );
		else
			rend_SetAlphaValue (255);

		rend_SetLighting(LS_NONE);
		rend_SetTextureType (TT_FLAT);

		int r,g,b;
			
		r=GR_COLOR_RED (fp->color);
		g=GR_COLOR_GREEN (fp->color);
		b=GR_COLOR_BLUE (fp->color);
		
		if (Polymodel_light_type==POLYMODEL_LIGHTING_GOURAUD)
		{
			if (Polymodel_use_effect && Polymodel_effect.type & PEF_COLOR)
			{	
				r=Polymodel_effect.r*(float)r*Polylighting_static_red;
				g=Polymodel_effect.g*(float)g*Polylighting_static_green;
				b=Polymodel_effect.b*(float)b*Polylighting_static_blue;
			}
			else
			{
				r=(float)r*Polylighting_static_red;
				g=(float)g*Polylighting_static_green;
				b=(float)b*Polylighting_static_blue;
			}
		}

		rend_SetFlatColor(GR_RGB(r,g,b));
		
		bm_handle=0;
	}

	if (triface)
		g3_SetTriangulationTest(1);
	PolymodelPerfAdd(Polymodel_perf_face_base_state_time, state_setup_start_time);

	if (lightmap_lmi_handle >= 0 && UsePerPixelPolymodelLighting())
	{
		double per_pixel_start_time = PolymodelPerfNow();
		per_pixel_light_count = GetPerPixelLightmapLights((ushort)lightmap_lmi_handle, per_pixel_lights,
			RENDERER_MAX_PER_PIXEL_DYNAMIC_LIGHTS);
		if (per_pixel_light_count > 0)
			rend_SetPerPixelDynamicLighting(&LightmapInfo[lightmap_lmi_handle].normal,
				per_pixel_light_count, per_pixel_lights);
		PolymodelPerfAdd(Polymodel_perf_face_base_per_pixel_time, per_pixel_start_time);
	}
		
	double draw_start_time = PolymodelPerfNow();
	g3_DrawPoly(fp->nverts,pointlist,bm_handle,MAP_TYPE_BITMAP,&face_cc);
	if (Perf_markers_enabled)
		Polymodel_perf_draw_poly_count++;
	PolymodelPerfAdd(Polymodel_perf_face_base_draw_time, draw_start_time);

	if (per_pixel_light_count > 0)
		rend_SetPerPixelDynamicLighting(nullptr, 0, nullptr);

	if (Polymodel_light_type==POLYMODEL_LIGHTING_GOURAUD && UsePerPixelPolymodelLighting())
		rend_SetLighting(LS_GOURAUD);

	if (triface)
		g3_SetTriangulationTest(0);

	if (texp && (Polymodel_effect.type & PEF_BUMPMAPPED) && texp->bumpmap!=-1 && Polymodel_light_type==POLYMODEL_LIGHTING_GOURAUD )
	{
		rend_SetBumpmapReadyState(0,0);
	}
	PolymodelPerfAdd(Polymodel_perf_face_base_time, face_start_time);

	#ifdef _DEBUG
	if (Polymodel_outline_mode)
		DrawSubmodelFaceOutline (fp->nverts,pointlist);
	
/*	if (Lightmap_debug_model==(pm-Poly_models) && Polymodel_light_type==POLYMODEL_LIGHTING_LIGHTMAP && Lightmap_debug_subnum==modelnum && Lightmap_debug_facenum==facenum)
	{
		
		int lmi_handle=Polylighting_lightmap_object->lightmap_faces[modelnum][facenum].lmi_handle;
		lightmap_object_face *lfp=&Polylighting_lightmap_object->lightmap_faces[modelnum][facenum];
		lightmap_info *lmi_ptr=&LightmapInfo[lmi_handle];
		int w=lmi_w (lmi_handle);
		int h=lmi_h (lmi_handle);
		vector rvec=lfp->rvec*lmi_ptr->xspacing;
		vector uvec=lfp->uvec*lmi_ptr->yspacing;
		ushort *src_data=(ushort *)lm_data(lmi_ptr->lm_handle);

		for (int i=0;i<w*h;i++) 
		{
			int t;
			g3Point epoints[20];
			vector evec[20];
			int y=i/w;
			int x=i%w;

			evec[0]=lmi_ptr->upper_left-(y*uvec)+(x*rvec);
			g3_RotatePoint(&epoints[0],&evec[0]);
			pointlist[0] = &epoints[0];

			evec[1]=lmi_ptr->upper_left-(y*uvec)+((x+1)*rvec);
			g3_RotatePoint(&epoints[1],&evec[1]);
			pointlist[1] = &epoints[1];

			evec[2]=lmi_ptr->upper_left-((y+1)*uvec)+((x+1)*rvec);
			g3_RotatePoint(&epoints[2],&evec[2]);
			pointlist[2] = &epoints[2];

			evec[3]=lmi_ptr->upper_left-((y+1)*uvec)+(x*rvec);
			g3_RotatePoint(&epoints[3],&evec[3]);
			pointlist[3] = &epoints[3];
	
			if (!(src_data[y*w+x] & OPAQUE_FLAG))
			{
				for (t=0;t<4;t++)
					g3_DrawLine(GR_RGB(255,0,255),pointlist[t],pointlist[(t+1)%4]);
			}
			else
			{
				for (t=0;t<4;t++)
					g3_DrawLine(GR_RGB(255,255,255),pointlist[t],pointlist[(t+1)%4]);
			}
		}

		// Draw red cross where upper left is
		ubyte c0;
		g3Point p0;
		p0.p3_flags=0;
		c0 = g3_RotatePoint(&p0,&LightmapInfo[lmi_handle].upper_left);

		if (! c0) 
		{

			//Draw a little cross at the current vert
			g3_ProjectPoint(&p0);	  //make sure projected
			rend_SetFlatColor(GR_RGB(255,0,0));
			rend_DrawLine(p0.p3_sx-CROSS_WIDTH,p0.p3_sy,p0.p3_sx,p0.p3_sy-CROSS_WIDTH);
			rend_DrawLine(p0.p3_sx,p0.p3_sy-CROSS_WIDTH,p0.p3_sx+CROSS_WIDTH,p0.p3_sy);
			rend_DrawLine(p0.p3_sx+CROSS_WIDTH,p0.p3_sy,p0.p3_sx,p0.p3_sy+CROSS_WIDTH);
			rend_DrawLine(p0.p3_sx,p0.p3_sy+CROSS_WIDTH,p0.p3_sx-CROSS_WIDTH,p0.p3_sy);
		}
		
	}*/


	#endif
}

inline void RenderSubmodelLightmapFace (poly_model *pm,bsp_info *sm,int facenum)
{
	double face_start_time = PolymodelPerfNow();
	PolymodelPerfTouch(face_start_time);
	if (Perf_markers_enabled)
		Polymodel_perf_lightmap_face_count++;

	g3Point	*pointlist[100];
		
	polyface *fp=&sm->faces[facenum];
	int modelnum=sm-pm->submodel;
	int t;
		
	int lm_handle=LightmapInfo[Polylighting_lightmap_object->lightmap_faces[modelnum][facenum].lmi_handle].lm_handle;
	float xscalar=(float)GameLightmaps[lm_handle].width/(float)GameLightmaps[lm_handle].square_res;
	float yscalar=(float)GameLightmaps[lm_handle].height/(float)GameLightmaps[lm_handle].square_res;

			
	ASSERT (fp->nverts<100);
				
	// Setup the points for this face
	for (t=0;t<fp->nverts;t++) 
	{
		g3Point *p = &Robot_points[fp->vertnums[t]];
		pointlist[t] = p;

		p->p3_uvl.u = Polylighting_lightmap_object->lightmap_faces[modelnum][facenum].u2[t]*xscalar;
		p->p3_uvl.v = Polylighting_lightmap_object->lightmap_faces[modelnum][facenum].v2[t]*yscalar;
		p->p3_uvl.l = 1.0;
			
		p->p3_flags |=PF_UV2+PF_RGBA+PF_L;
	}

	if (triangulated_faces[facenum])
		g3_SetTriangulationTest (1);
	
	double draw_start_time = PolymodelPerfNow();
	g3_DrawPoly(fp->nverts,pointlist,lm_handle,MAP_TYPE_LIGHTMAP);
	if (Perf_markers_enabled)
		Polymodel_perf_draw_poly_count++;
	PolymodelPerfAdd(Polymodel_perf_lightmap_face_draw_time, draw_start_time);

	if (triangulated_faces[facenum])
		g3_SetTriangulationTest (0);
	PolymodelPerfAdd(Polymodel_perf_lightmap_face_time, face_start_time);
}

static void SetupPolymodelFogViewPlane()
{
	g3_RotateDeltaVec(&Fog_plane, &Polymodel_fog_plane);
	if (vm_NormalizeVector(&Fog_plane) <= 0.0001f)
	{
		Fog_distance = 0.0f;
		Fog_eye_distance = 0.0f;
		return;
	}

	Fog_distance = Polymodel_effect.fog_eye_distance;
	Fog_eye_distance = Polymodel_effect.fog_eye_distance;
}

static float GetPolymodelFogMagnitude(g3Point *point)
{
	if (Polymodel_effect.fog_plane_check == 1)
		return point->p3_z;

	float dist = (point->p3_vec * Fog_plane) + Fog_distance;
	float denom = Fog_eye_distance - dist;
	if (denom > -0.0001f && denom < 0.0001f)
		return 0.0f;

	float t = Fog_eye_distance / denom;
	vector portal_point = t * point->p3_vec;
	float mag = point->p3_z - portal_point.z;
	return mag > 0.0f ? mag : 0.0f;
}

inline void RenderSubmodelFaceFogged (poly_model *pm,bsp_info *sm,int facenum)
{
	double face_start_time = PolymodelPerfNow();
	PolymodelPerfTouch(face_start_time);
	if (PolymodelShouldSkipTransparentCockpitFace(pm, sm, facenum))
		return;
	if (Perf_markers_enabled)
		Polymodel_perf_fog_face_count++;

	g3Point	*pointlist[100];
	polyface *fp=&sm->faces[facenum];
	int modelnum=sm-pm->submodel;
	int t;

	for (t=0;t<fp->nverts;t++) 
	{
		g3Point *p = &Robot_points[fp->vertnums[t]];
		pointlist[t] = p;

		float mag = GetPolymodelFogMagnitude(p);

		float scalar=mag/Polymodel_effect.fog_depth;

		if (scalar>1)
			scalar=1;
		if (scalar<0)
			scalar=0;
		p->p3_a=scalar;
 					
		p->p3_flags |= PF_RGBA;
	}

	if (triangulated_faces[facenum])
		g3_SetTriangulationTest (1);

	g3_DrawPoly(fp->nverts,pointlist,0);
	if (Perf_markers_enabled)
		Polymodel_perf_draw_poly_count++;

	if (triangulated_faces[facenum])
		g3_SetTriangulationTest (0);
	PolymodelPerfAdd(Polymodel_perf_fog_face_time, face_start_time);

}

static bool TryBatchSubmodelFaceFogged(poly_model *pm, bsp_info *sm, int facenum,
	PolymodelFogFaceBatcher& batcher)
{
	if (PolymodelShouldSkipTransparentCockpitFace(pm, sm, facenum))
		return true;

	polyface *fp=&sm->faces[facenum];
	if (fp->nverts < 3 || fp->nverts >= 100)
		return false;
	if (triangulated_faces[facenum])
		return false;

	g3Point	*pointlist[100];
	for (int t=0;t<fp->nverts;t++) 
	{
		g3Point *p = &Robot_points[fp->vertnums[t]];
		pointlist[t] = p;

		float mag = GetPolymodelFogMagnitude(p);
		float scalar=mag/Polymodel_effect.fog_depth;

		if (scalar>1)
			scalar=1;
		if (scalar<0)
			scalar=0;
		p->p3_a=scalar;
 					
		p->p3_flags |= PF_RGBA;
	}

	batcher.Add(pointlist, fp->nverts);
	if (Perf_markers_enabled)
		Polymodel_perf_fog_face_count++;
	return true;
}

inline void RenderSubmodelFaceSpecular (poly_model *pm,bsp_info *sm,int facenum)
{
	double face_start_time = PolymodelPerfNow();
	PolymodelPerfTouch(face_start_time);
	if (PolymodelShouldSkipTransparentCockpitFace(pm, sm, facenum))
		return;
	if (Perf_markers_enabled)
		Polymodel_perf_specular_face_count++;

	g3Point	*pointlist[100];
	polyface *fp=&sm->faces[facenum];
	int modelnum=sm-pm->submodel;
	int t;
	bool smooth=0;
	
	if (sm->vertnorms != nullptr)
	{
		if ((Polymodel_effect.type & PEF_SPECULAR_FACES) && (GameTextures[fp->texnum].flags & TF_SMOOTH_SPECULAR))
			smooth=1;
		else if ((Polymodel_effect.type & PEF_SPECULAR_MODEL) && UsePerPixelPolymodelLighting())
			smooth=1;
	}
	
	for (t=0;t<fp->nverts;t++) 
	{
		g3Point *p = &Robot_points[fp->vertnums[t]];
		vector vert=sm->verts[fp->vertnums[t]];

		vector vertnorm;

		if (smooth)
			vertnorm=sm->vertnorms[fp->vertnums[t]];
		else
			vertnorm=fp->normal;

		pointlist[t] = p;

		p->p3_flags |= PF_RGBA;
		p->p3_a=0.0;

		vector subvec=Specular_view_pos-vert;
		vm_NormalizeVectorFast (&subvec);

		vector incident_norm=vert-Polymodel_specular_pos;
		vm_NormalizeVectorFast (&incident_norm);
			
		float d=incident_norm * vertnorm;
		vector upvec=d * vertnorm;
		incident_norm-=(2*upvec);

		float dotp=subvec * incident_norm;

		if (dotp<0) 
			continue;
		if (dotp>1)
			dotp=1;
			
		if (dotp>0)
		{
			int index=((float)(MAX_SPECULAR_INCREMENTS-1)*dotp);
			float val=Specular_tables[2][index];
			
			p->p3_a=val*Polymodel_effect.spec_scalar;
		}
	}

	if (triangulated_faces[facenum])
		g3_SetTriangulationTest (1);

	g3_DrawPoly(fp->nverts,pointlist,0);
	if (Perf_markers_enabled)
		Polymodel_perf_draw_poly_count++;

	if (triangulated_faces[facenum])
		g3_SetTriangulationTest (0);
	PolymodelPerfAdd(Polymodel_perf_specular_face_time, face_start_time);
}


#define MAX_PARTS	100

// Draws a glowing cone of light that represents thrusters
void DrawThrusterEffect (vector *pos,float r,float g,float b,vector *norm,float size,float length)
{
	vector cur_pos=*pos;
	float cur_length=0;
	vector glow_pos[MAX_PARTS];
	float glow_size[MAX_PARTS];
	int total_parts=0;

	if (length<.1)
		return;

	int num_divs=length*3;
	if (num_divs>MAX_PARTS)
		num_divs=MAX_PARTS;

	float size_change=size/num_divs;
	float pos_change=length/num_divs;

	if (!UseHardware)
		return;		// No software stuff here!
	
	rend_SetZBufferWriteMask (0);
	rend_SetAlphaType (AT_SATURATE_TEXTURE);
	rend_SetAlphaValue (.3*255);

	rend_SetLighting (LS_GOURAUD);
	rend_SetColorModel (CM_RGB);

	ddgr_color color=GR_RGB(r*255,g*255,b*255);
	int bm_handle=Fireballs[GRADIENT_BALL_INDEX].bm_handle;
	int t;

	// We must draw the small ones first, but we're starting the iteration from the 
	// large one.  Consequently, we must store our variables so we can draw in reverse order
	for (t=0;t<num_divs && size>.05;t++)
	{
		glow_pos[t]=cur_pos;
		glow_size[t]=size;

		size-=size_change;
		cur_pos+=((*norm)*pos_change);
		
		total_parts++;
	}

	for (t=total_parts-1;t>=0;t--)
	{
		rend_SetZBias (-glow_size[t]);
		g3_DrawBitmap (&glow_pos[t],glow_size[t],(glow_size[t]*bm_h(bm_handle,0))/bm_w(bm_handle,0),bm_handle,color);
	}
	rend_SetZBias (0.0);
	rend_SetZBufferWriteMask (1);
}

// Draws a glowing cone of light
void DrawGlowEffect (vector *pos,float r,float g,float b,vector *norm,float size)
{
	if (!UseHardware)
		return;		// No software stuff here!

	if (Polymodel_use_effect && Polymodel_effect.type & PEF_NO_GLOWS)
		return;
	
	rend_SetZBufferWriteMask (0);
	rend_SetAlphaType (AT_SATURATE_TEXTURE);
	rend_SetAlphaValue (.8*255);
	rend_SetLighting (LS_GOURAUD);
	rend_SetColorModel (CM_RGB);

	ddgr_color color=GR_RGB(r*255,g*255,b*255);
	int bm_handle=Fireballs[GRADIENT_BALL_INDEX].bm_handle;

	rend_SetZBias (-size);
	g3_DrawBitmap (pos,size,(size*bm_h(bm_handle,0))/bm_w(bm_handle,0),bm_handle,color);

	rend_SetZBias (0.0);
	rend_SetZBufferWriteMask (1);
}

void RenderSubmodelFacesSorted (poly_model *pm,bsp_info *sm)
{
	double sorted_start_time = PolymodelPerfNow();
	PolymodelPerfTouch(sorted_start_time);
	int i,t;
		
	int rcount;
	int model_render_order[MAX_POLYGON_VECS];
	int modelnum=sm-pm->submodel;

	ASSERT (sm->nverts<MAX_POLYGON_VECS);

	//Build list of visible (non-backfacing) faces, & compute average face depths
	for (i=rcount=0;i<sm->num_faces;i++) 
	{
		if (Perf_markers_enabled)
			Polymodel_perf_faces_considered_count++;
		polyface		*fp = &sm->faces[i];
	
		//check for visible face
		if (g3_CheckNormalFacing(&sm->verts[fp->vertnums[0]],&fp->normal))	
		{
			face_depth[i] = 0;
			for (t=0;t<fp->nverts;t++)
				face_depth[i] += Robot_points[fp->vertnums[t]].p3_z;

			face_depth[i] /= fp->nverts;

			//initialize order list
			model_render_order[rcount] = i;

			rcount++;

			ASSERT (rcount<MAX_POLYGON_VECS);
		}
	
	}

	//Sort the faces
	qsort(model_render_order,rcount,sizeof(*model_render_order),(int (*)(const void*,const void*)) ModelFaceSortFunc);
	
	for (i=rcount-1;i>=0;i--)
	{
		int facenum=model_render_order[i];

		if (PolymodelShouldSkipTransparentCockpitFace(pm, sm, facenum))
			continue;
		RenderSubmodelFace (pm,sm,facenum);
	}
	PolymodelPerfAdd(Polymodel_perf_faces_sorted_time, sorted_start_time);
}
void RenderSubmodelFacesUnsorted (poly_model *pm,bsp_info *sm)
{
	double unsorted_start_time = PolymodelPerfNow();
	PolymodelPerfTouch(unsorted_start_time);
	int i;
	int modelnum=sm-pm->submodel;
	short alpha_faces[MAX_FACES_PER_ROOM],num_alpha_faces=0;
	int rcount=0;
	vector view_pos;
	PolymodelBaseFaceBatcher base_face_batcher;
	base_face_batcher.Reserve(std::min(sm->num_faces, 32), sm->num_faces);

	g3_GetViewPosition (&view_pos);
	g3_GetUnscaledMatrix (&Unscaled_bumpmap_matrix);

	Specular_view_pos=view_pos;
	Bump_view_pos=view_pos;
	
	if (modelnum<0 || modelnum>=pm->n_models)
	{
		Error ("Got bad model number %d from polymodel %s!",modelnum,pm->name);
		return;
	}

	if (sm->flags & SOF_CUSTOM)
		rend_SetZBias (-.5);
	
	for (i=0;i<sm->num_faces;i++)
	{
		double scan_start_time = PolymodelPerfNow();
		if (Perf_markers_enabled)
			Polymodel_perf_faces_considered_count++;
		vector tempv;
		polyface *fp=&sm->faces[i];
		texture *texp;

		// Check to see if this face even faces us!
		tempv = view_pos - sm->verts[fp->vertnums[0]];
		if ((tempv * fp->normal)<0)
		{
			PolymodelPerfAdd(Polymodel_perf_faces_unsorted_scan_time, scan_start_time);
			continue;
		}

		if (PolymodelShouldSkipTransparentCockpitFace(pm, sm, i))
		{
			PolymodelPerfAdd(Polymodel_perf_faces_unsorted_scan_time, scan_start_time);
			continue;
		}

		if (!StateLimited && Polymodel_active_opaque_batcher && Polymodel_active_alpha_batcher)
		{
			const bool alpha_face = PolymodelFaceUsesAlpha(pm, sm, i);
			PolymodelBaseFaceBatcher& batcher = alpha_face ? *Polymodel_active_alpha_batcher : *Polymodel_active_opaque_batcher;
			if (alpha_face && Perf_markers_enabled)
				Polymodel_perf_alpha_face_count++;
			PolymodelPerfAdd(Polymodel_perf_faces_unsorted_scan_time, scan_start_time);
			if (TryBatchSubmodelBaseFace(pm, sm, i, batcher, alpha_face))
				continue;

			Polymodel_active_opaque_batcher->Flush();
			Polymodel_active_alpha_batcher->Flush();
			RenderSubmodelFace(pm, sm, i);
			continue;
		}
		
		if (fp->texnum!=-1)
		{
			texp=&GameTextures[pm->textures[fp->texnum]];

			if (texp->flags & TF_ALPHA || texp->flags & TF_SATURATE)
			{
				alpha_faces[num_alpha_faces++] = i;
				if (Perf_markers_enabled)
					Polymodel_perf_alpha_face_count++;
				PolymodelPerfAdd(Polymodel_perf_faces_unsorted_scan_time, scan_start_time);
				continue;
			}
		}

		if (StateLimited)
		{
			State_elements[rcount].facenum=i;
			State_elements[rcount].sort_key=pm->textures[fp->texnum];
			rcount++;
			PolymodelPerfAdd(Polymodel_perf_faces_unsorted_scan_time, scan_start_time);
		}
		else
		{
			PolymodelPerfAdd(Polymodel_perf_faces_unsorted_scan_time, scan_start_time);
			if (TryBatchSubmodelBaseFace(pm, sm, i, base_face_batcher))
				continue;
			base_face_batcher.Flush();
			RenderSubmodelFace (pm,sm,i);
		}
	}

	if (!StateLimited && !Polymodel_active_opaque_batcher)
		base_face_batcher.Flush();

	if (StateLimited)
	{
		double sort_start_time = PolymodelPerfNow();
		SortStates (State_elements,rcount);
		for (i=rcount-1;i>=0;i--)
		{
			int facenum=State_elements[i].facenum;
			if (PolymodelShouldSkipTransparentCockpitFace(pm, sm, facenum))
				continue;
			RenderSubmodelFace (pm,sm,facenum);
		}

		if (!NoLightmaps)
		{
			if (!UseMultitexture && Polymodel_light_type==POLYMODEL_LIGHTING_LIGHTMAP)
			{
				rend_SetAlphaType(AT_LIGHTMAP_BLEND);
				rend_SetLighting (LS_GOURAUD);
				rend_SetColorModel (CM_MONO);
				rend_SetOverlayType (OT_NONE);
				rend_SetTextureType(TT_PERSPECTIVE);
				rend_SetWrapType (WT_CLAMP);
				rend_SetMipState (0);	

				for (i=rcount-1;i>=0;i--)
				{
					int facenum=State_elements[i].facenum;
					RenderSubmodelLightmapFace (pm,sm,facenum);
				}

			rend_SetWrapType (WT_WRAP);
			rend_SetMipState (1);
		}
		PolymodelPerfAdd(Polymodel_perf_faces_unsorted_sort_time, sort_start_time);
	}
	}

	// Now render all alpha faces
	//rend_SetZBufferWriteMask (0);
	if (!Polymodel_active_alpha_batcher)
	{
		for (i=0;i<num_alpha_faces;i++)
		{
			if (PolymodelShouldSkipTransparentCockpitFace(pm, sm, alpha_faces[i]))
				continue;
			RenderSubmodelFace(pm,sm,alpha_faces[i]);
		}
	}
	//rend_SetZBufferWriteMask (1);

	if (sm->flags & SOF_CUSTOM)
		rend_SetZBias (0);

	// Draw specular faces if needed
	if (Polymodel_use_effect && Polymodel_effect.type & (PEF_SPECULAR_MODEL|PEF_SPECULAR_FACES))
	{
		double specular_start_time = PolymodelPerfNow();
		rend_SetOverlayType (OT_NONE);
		rend_SetTextureType (TT_FLAT);
		rend_SetLighting (LS_NONE);
		rend_SetColorModel (CM_MONO);
		rend_SetAlphaType (AT_SATURATE_VERTEX);
		rend_SetAlphaValue (255);
		rend_SetZBufferWriteMask (0);
	
		rend_SetFlatColor (GR_RGB((int)(Polymodel_effect.spec_r*255.0),(int)(Polymodel_effect.spec_g*255.0),(int)(Polymodel_effect.spec_b*255.0)));

		for (i=0;i<sm->num_faces;i++)
		{
			polyface *fp=&sm->faces[i];

			if (!g3_CheckNormalFacing(&sm->verts[fp->vertnums[0]],&fp->normal))	
				continue;

		/*	vector subvec=sm->verts[fp->vertnums[0]]-Polymodel_specular_pos;
			if ((fp->normal * subvec)> 0)
				continue;*/

			if ((Polymodel_effect.type & PEF_SPECULAR_MODEL) || (fp->texnum!=-1 && GameTextures[pm->textures[fp->texnum]].flags & TF_SPECULAR))
				RenderSubmodelFaceSpecular (pm,sm,i);
		}
		
		rend_SetZBufferWriteMask (1);
		PolymodelPerfAdd(Polymodel_perf_specular_pass_time, specular_start_time);
	}

	// Draw fog if need be
	if (Polymodel_use_effect && Polymodel_effect.type & PEF_FOGGED_MODEL)
	{
		double fog_start_time = PolymodelPerfNow();
		PolymodelFogFaceBatcher fog_face_batcher;
		fog_face_batcher.Reserve(sm->num_faces);
		const bool batch_fog = UseHardware && !StateLimited;

		if (Polymodel_effect.fog_plane_check!=1)
			SetupPolymodelFogViewPlane();

		rend_SetOverlayType (OT_NONE);
		rend_SetTextureType (TT_FLAT);
		rend_SetLighting (LS_NONE);
		rend_SetColorModel (CM_MONO);
		rend_SetAlphaType (AT_VERTEX);
		rend_SetAlphaValue (255);
		rend_SetZBufferWriteMask (0);
		rend_SetCoplanarPolygonOffset(1);
		rend_SetFlatColor (GR_RGB((int)(Polymodel_effect.fog_r*255.0),(int)(Polymodel_effect.fog_g*255.0),(int)(Polymodel_effect.fog_b*255.0)));
		
		for (i=0;i<sm->num_faces;i++)
		{
			polyface *fp=&sm->faces[i];

			if (!g3_CheckNormalFacing(&sm->verts[fp->vertnums[0]],&fp->normal))	
				continue;

			if (batch_fog && TryBatchSubmodelFaceFogged(pm, sm, i, fog_face_batcher))
				continue;

			fog_face_batcher.Flush();
			RenderSubmodelFaceFogged (pm,sm,i);
		}
		
		fog_face_batcher.Flush();
		rend_SetCoplanarPolygonOffset(0);
		rend_SetZBufferWriteMask (1);
		PolymodelPerfAdd(Polymodel_perf_fog_pass_time, fog_start_time);
	}

	PolymodelPerfAdd(Polymodel_perf_faces_unsorted_time, unsorted_start_time);
}


void BuildModelAngleMatrix( matrix *mat, angle ang,vector *axis);
void StartLightInstance (vector *,matrix *);
void DoneLightInstance();

static bool UsePerPixelPolymodelLighting()
{
	return UseHardware && Render_preferred_state.per_pixel_lighting &&
		rend_SupportsPerPixelLighting();
}

static light_state GetPolymodelGouraudLightingState()
{
	return UsePerPixelPolymodelLighting() ? LS_PHONG : LS_GOURAUD;
}

static void SetPolymodelGouraudPointLighting(g3Point& point, const vector& normal)
{
	const bool use_effect_color = Polymodel_use_effect && (Polymodel_effect.type & PEF_COLOR);
	const float effect_r = use_effect_color ? Polymodel_effect.r : 1.0f;
	const float effect_g = use_effect_color ? Polymodel_effect.g : 1.0f;
	const float effect_b = use_effect_color ? Polymodel_effect.b : 1.0f;

	float light = 1.0f;
	if (UsePerPixelPolymodelLighting())
	{
		point.p3_vecPreRot = normal;
	}
	else
	{
		light = (-vm_DotProduct(Polymodel_light_direction, &normal) + 1.0f) / 2.0f;
	}

	point.p3_r = effect_r * light * Polylighting_static_red;
	point.p3_g = effect_g * light * Polylighting_static_green;
	point.p3_b = effect_b * light * Polylighting_static_blue;
}

// Rotates all of the points of a submodel, plus supplies color info 
void RotateModelPoints (poly_model *pm,bsp_info *sm)
{

	// Figure out lighting
	if (Polymodel_light_type==POLYMODEL_LIGHTING_STATIC)
	{
		if ((Polymodel_use_effect && (Polymodel_effect.type & PEF_DEFORM)) || (sm->flags & SOF_JITTER))
		{
			for (int i=0;i<sm->nverts;i++)
			{
				vector vec=sm->verts[i];
				
				float val=((ps_rand()%1000)-500.0)/500.0;
				vec*=1.0+(Polymodel_effect.deform_range*val);

				g3_RotatePoint(&Robot_points[i],&vec);
				PolymodelMotionSetPoint(&Robot_points[i], pm, sm - pm->submodel, &vec);
			}
		}
		else
		{
			for (int i=0;i<sm->nverts;i++)
			{
				g3_RotatePoint(&Robot_points[i],&sm->verts[i]);
				PolymodelMotionSetPoint(&Robot_points[i], pm, sm - pm->submodel, &sm->verts[i]);
			}
		}
	}
	else if (Polymodel_light_type==POLYMODEL_LIGHTING_LIGHTMAP)
	{
		if ((Polymodel_use_effect && (Polymodel_effect.type & PEF_DEFORM)) || (sm->flags & SOF_JITTER))
		{
			for (int i=0;i<sm->nverts;i++)
			{
				vector vec=sm->verts[i];
				float val=((ps_rand()%1000)-500.0)/500.0;
				vec*=1.0+(Polymodel_effect.deform_range*val);

				g3_RotatePoint(&Robot_points[i],&vec);
				PolymodelMotionSetPoint(&Robot_points[i], pm, sm - pm->submodel, &vec);

				Robot_points[i].p3_r=1.0;
				Robot_points[i].p3_g=1.0;
				Robot_points[i].p3_b=1.0;
			}	
		}
		else
		{
			for (int i=0;i<sm->nverts;i++)
			{
				g3_RotatePoint(&Robot_points[i],&sm->verts[i]);
				PolymodelMotionSetPoint(&Robot_points[i], pm, sm - pm->submodel, &sm->verts[i]);
				Robot_points[i].p3_r=1.0;
				Robot_points[i].p3_g=1.0;
				Robot_points[i].p3_b=1.0;
			}
		}
	}
	else if (Polymodel_light_type==POLYMODEL_LIGHTING_GOURAUD)
	{
		const bool deform = (Polymodel_use_effect && (Polymodel_effect.type & PEF_DEFORM)) || (sm->flags & SOF_JITTER);
		for (int i=0;i<sm->nverts;i++)
		{
			vector vec=sm->verts[i];
			if (deform)
			{
				float val=((ps_rand()%1000)-500.0)/500.0;
				vec*=1.0+(Polymodel_effect.deform_range*val);
			}

			g3_RotatePoint(&Robot_points[i],&vec);
			PolymodelMotionSetPoint(&Robot_points[i], pm, sm - pm->submodel, &vec);
			vector normvec=sm->vertnorms[i];
			SetPolymodelGouraudPointLighting(Robot_points[i], normvec);
		}
	}

	#ifndef RELEASE
	if (!UseHardware)
	{
		for (int i=0;i<sm->nverts;i++)
			Robot_points[i].p3_l=Robot_points[i].p3_g;
		rend_SetColorModel (CM_MONO);
	}
	#endif
	

}

void RenderSubmodel (poly_model *pm,bsp_info *sm, uint f_render_sub)
{
	int i;
	matrix lightmatrix;
	if (Perf_markers_enabled)
		Polymodel_perf_submodel_visit_count++;

	// Don't render door housings
	if (IsNonRenderableSubmodel (pm,sm-pm->submodel))
		return;

	double instance_setup_start_time = PolymodelPerfNow();
	if (Polymodel_light_type!=POLYMODEL_LIGHTING_LIGHTMAP)
	{
		// Turn off bumpmapping if not needed
		rend_SetBumpmapReadyState(0,0);
	}
	else
	{
		if (!StateLimited || UseMultitexture)
			rend_SetOverlayType (OT_BLEND);
	}


	if (Multicolor_texture==-1 && Polymodel_use_effect && (Polymodel_effect.type & PEF_CUSTOM_COLOR))
		Multicolor_texture=FindTextureName ("MultiColor");


	rend_SetColorModel (CM_RGB);			
	StartPolyModelPosInstance(&sm->mod_pos);
	vector temp_vec=sm->mod_pos+sm->offset;
	g3_StartInstanceAngles(&temp_vec,&sm->angs );
	
	vm_AnglesToMatrix (&lightmatrix,sm->angs.p,sm->angs.h,sm->angs.b);
	StartLightInstance(&temp_vec,&lightmatrix);
	PolymodelPerfAdd(Polymodel_perf_submodel_instance_time, instance_setup_start_time);
			
	// Check my bit to see if I get drawn
	if(f_render_sub & (0x00000001 << (sm - pm->submodel))) 
	{
		if (sm->flags & SOF_CUSTOM)
		{
			if (!(Polymodel_effect.type & PEF_CUSTOM_TEXTURE))
				goto pop_lighting;
		}

		// Check to draw glow faces
		if (sm->flags & (SOF_GLOW | SOF_THRUSTER))
		{
			if (!FacingPass)
				goto pop_lighting;

			double facing_effect_start_time = PolymodelPerfNow();
			vector zero_pos={0,0,0};
			rend_SetOverlayType (OT_NONE);

			if (Polymodel_use_effect && Polymodel_effect.type & PEF_GLOW_SCALAR)
			{
				if (Polymodel_effect.type & PEF_CUSTOM_GLOW)
					DrawThrusterEffect (&zero_pos,Polymodel_effect.glow_r,Polymodel_effect.glow_g,Polymodel_effect.glow_b,&sm->glow_info->normal,sm->glow_info->glow_size*Polymodel_effect.glow_size_scalar,3*Polymodel_effect.glow_length_scalar);
				else
					DrawThrusterEffect (&zero_pos,sm->glow_info->glow_r,sm->glow_info->glow_g,sm->glow_info->glow_b,&sm->glow_info->normal,sm->glow_info->glow_size*Polymodel_effect.glow_size_scalar,3*Polymodel_effect.glow_length_scalar);
			}
			else
			{
				if (Polymodel_use_effect && Polymodel_effect.type & PEF_CUSTOM_GLOW)
					DrawGlowEffect (&zero_pos,Polymodel_effect.glow_r,Polymodel_effect.glow_g,Polymodel_effect.glow_b,&sm->glow_info->normal,sm->glow_info->glow_size);
				else
					DrawGlowEffect (&zero_pos,sm->glow_info->glow_r,sm->glow_info->glow_g,sm->glow_info->glow_b,&sm->glow_info->normal,sm->glow_info->glow_size);
			}
			if (Perf_markers_enabled)
				Polymodel_perf_facing_effect_count++;
			PolymodelPerfAdd(Polymodel_perf_facing_effect_time, facing_effect_start_time);
			
			goto pop_lighting;
		}
		else if (sm->flags & SOF_FACING)
		{
			if (!FacingPass)
				goto pop_lighting;
			
			double facing_effect_start_time = PolymodelPerfNow();
			vector pos;
			rend_SetLighting (LS_NONE);
			rend_SetColorModel (CM_MONO);
			rend_SetOverlayType (OT_NONE);

			int bm_handle=GetTextureBitmap(pm->textures[sm->faces[0].texnum],0);
			rend_SetAlphaValue (GameTextures[pm->textures[sm->faces[0].texnum]].alpha*255);

			vm_MakeZero (&pos);
	
			if (GameTextures[pm->textures[sm->faces[0].texnum]].flags & TF_SATURATE)
				rend_SetAlphaType (AT_SATURATE_TEXTURE);
			else
				rend_SetAlphaType (ATF_CONSTANT+ATF_TEXTURE);
	
			rend_SetZBufferWriteMask (0);	
			g3_DrawBitmap (&pos,sm->rad,(sm->rad*bm_h(bm_handle,0))/bm_w(bm_handle,0),bm_handle);
			rend_SetZBufferWriteMask (1);	
			if (Perf_markers_enabled)
				Polymodel_perf_facing_effect_count++;
			PolymodelPerfAdd(Polymodel_perf_facing_effect_time, facing_effect_start_time);
		
			goto pop_lighting;
		}
		else
		{
			if (FacingPass)
				goto pop_lighting;
		}

		if (Perf_markers_enabled)
			Polymodel_perf_submodel_draw_count++;
		double rotate_start_time = PolymodelPerfNow();
		RotateModelPoints (pm,sm);
		PolymodelPerfAdd(Polymodel_perf_submodel_rotate_time, rotate_start_time);
			
		double faces_start_time = PolymodelPerfNow();
		if (!UseHardware)
			RenderSubmodelFacesSorted (pm, sm);
		else
			RenderSubmodelFacesUnsorted (pm, sm);
		PolymodelPerfAdd(Polymodel_perf_submodel_faces_time, faces_start_time);
	}
	
	pop_lighting:
		
	for (i=0;i<sm->num_children;i++)
	{
		RenderSubmodel(pm,&pm->submodel[sm->children[i]], f_render_sub);
	}
		
	
	g3_DoneInstance();
	DonePolyModelPosInstance();
	DoneLightInstance();
}

int RenderPolygonModel(poly_model * pm, uint f_render_sub)
{
	double render_start_time = PolymodelPerfNow();
	PolymodelPerfTouch(render_start_time);
	if (Perf_markers_enabled)
		Polymodel_perf_render_polygon_count++;

	ASSERT (pm->new_style==1);
	int i=0;
	
	rend_SetAlphaType (ATF_CONSTANT+ATF_VERTEX);
	rend_SetWrapType (WT_WRAP);
	
	FacingPass=0;
	PolymodelBaseFaceBatcher cockpit_opaque_batcher;
	PolymodelBaseFaceBatcher cockpit_alpha_batcher;
	const int model_face_count = Render_cpu_batch_cache ? PolymodelCountFaces(pm) : 0;
	const bool use_cockpit_batches = Polymodel_cockpit_batching && UseHardware && !StateLimited &&
		Polymodel_light_type != POLYMODEL_LIGHTING_LIGHTMAP && !Polymodel_use_effect;
	if (use_cockpit_batches)
	{
		cockpit_opaque_batcher.Reserve(64, model_face_count);
		cockpit_alpha_batcher.Reserve(16, model_face_count / 4);
		Polymodel_active_opaque_batcher = &cockpit_opaque_batcher;
		Polymodel_active_alpha_batcher = &cockpit_alpha_batcher;
	}
	double root_pass_start_time = PolymodelPerfNow();
	for (i=0;i<pm->n_models;i++)
	{
		bsp_info *sm=&pm->submodel[i];
		if (sm->parent==-1)
			RenderSubmodel (pm,sm, f_render_sub);
	}
	if (use_cockpit_batches)
	{
		Polymodel_active_opaque_batcher = nullptr;
		Polymodel_active_alpha_batcher = nullptr;
		cockpit_opaque_batcher.Flush();
		cockpit_alpha_batcher.Flush();
	}
	PolymodelPerfAdd(Polymodel_perf_root_pass_time, root_pass_start_time);

	// Now render any facing submodels
	if (pm->flags & PMF_FACING)
	{
		double facing_pass_start_time = PolymodelPerfNow();
		// Don't render if we have it set for no glows
		FacingPass=1;
		rend_SetOverlayType (OT_NONE);
		for (i=0;i<pm->n_models;i++)
		{
			bsp_info *sm=&pm->submodel[i];
			if (sm->parent==-1)
				RenderSubmodel (pm,sm, f_render_sub);
		}
		PolymodelPerfAdd(Polymodel_perf_facing_pass_time, facing_pass_start_time);
	}

	FacingPass=0;
	PolymodelPerfAdd(Polymodel_perf_render_polygon_time, render_start_time);
		
	return 1;
}

float	ComputeDefaultSizeFunc(int handle, float *size_ptr, vector *offset_ptr, bool f_use_all_frames)
{
	poly_model *pm;
	matrix m;
	float normalized_time[MAX_SUBOBJECTS];
	int i, j, n;
	float cur_dist;
	float size = 0.0;
	int start_frame = 0;
	int end_frame = 0;

	vector geometric_center = Zero_vector;

	// Chris: Come see me when you are ready to deal with the paging problem - JL
	pm = GetPolymodelPointer(handle);

	ASSERT(start_frame <= end_frame);
	ASSERT(end_frame <= pm->frame_max);

	if(f_use_all_frames)
	{
		end_frame = pm->frame_max;
	}

	if(offset_ptr)
	{ 
		//[ISB] initialize these, because some models have a first submodel with 0 verts. 
		vector min_xyz = {};
		vector max_xyz = {};

		for(n = start_frame; n <= end_frame; n++)
		{
			// Because size changes with animation, we need the worst case point -- so, check every keyframe
			// NOTE:  This code does not currently account for all $turret and $rotate positions
			
			SetNormalizedTimeAnim(n, normalized_time, pm);

			SetModelAnglesAndPos (pm,normalized_time);

			for (i = 0;i < pm->n_models; i++)
			{
				bsp_info *sm=&pm->submodel[i];
					
				// For every vertex
				for(j = 0; j < sm->nverts; j++)
				{
					vector pnt;
					int mn;
				
					// Get the point and its current sub-object
					pnt    = sm->verts[j];
					mn     = i;

					// Instance up the tree
					while (mn != -1)
					{
						vector tpnt;

						vm_AnglesToMatrix(&m, pm->submodel[mn].angs.p,pm->submodel[mn].angs.h, pm->submodel[mn].angs.b);
						vm_TransposeMatrix(&m);

						tpnt    = pnt * m;

						pnt = tpnt + pm->submodel[mn].offset + pm->submodel[mn].mod_pos;
							
						mn = pm->submodel[mn].parent;
					}

	//				Maybe use for other code -- Accounts for world coordinates
	//				m = obj->orient;
	//				vm_TransposeMatrix(&m);
	//
	//				pnt = pnt * m;
	//
	//				*gun_point += obj->pos;

					// Find the min_xyz and max_xyz
					if(n == start_frame && i == 0 && j == 0)
					{
						min_xyz = max_xyz = pnt;
					}
					else
					{
						if(pnt.x < min_xyz.x) min_xyz.x = pnt.x;
						else if(pnt.x > max_xyz.x) max_xyz.x = pnt.x;

						if(pnt.y < min_xyz.y) min_xyz.y = pnt.y;
						else if(pnt.y > max_xyz.y) max_xyz.y = pnt.y;

						if(pnt.z < min_xyz.z) min_xyz.z = pnt.z;
						else if(pnt.z > max_xyz.z) max_xyz.z = pnt.z;
					}
				}
			}
		}

		geometric_center = (max_xyz + min_xyz)/2.0;
		*offset_ptr = geometric_center;
	}

	for(n = start_frame; n <= end_frame; n++)
	{
		// Because size changes with animation, we need the worst case point -- so, check every keyframe
		// NOTE:  This code does not currently account for all $turret and $rotate positions
		
		SetNormalizedTimeAnim(n, normalized_time, pm);

		SetModelAnglesAndPos (pm,normalized_time);

		for (i = 0;i < pm->n_models; i++)
		{
			bsp_info *sm=&pm->submodel[i];
				
			// For every vertex
			for(j = 0; j < sm->nverts; j++)
			{
				vector pnt;
				int mn;
			
				// Get the point and its current sub-object
				pnt    = sm->verts[j];
				mn     = i;

				// Instance up the tree
				while (mn != -1)
				{
					vector tpnt;

					vm_AnglesToMatrix(&m, pm->submodel[mn].angs.p,pm->submodel[mn].angs.h, pm->submodel[mn].angs.b);
					vm_TransposeMatrix(&m);

					tpnt    = pnt * m;

					pnt = tpnt + pm->submodel[mn].offset + pm->submodel[mn].mod_pos;
						
					mn = pm->submodel[mn].parent;
				}

//				Maybe use for other code -- Accounts for world coordinates
//				m = obj->orient;
//				vm_TransposeMatrix(&m);
//
//				pnt = pnt * m;
//
//				*gun_point += obj->pos;

				cur_dist = vm_VectorDistance(&geometric_center, &pnt);
				if(cur_dist > size) size = cur_dist;
			}
		}
	}

	// This is a arbitary value.  It allows for some turret and rotations to be caught
	size = size + 0.01f;
	
	if(size_ptr)	//DAJ
		*size_ptr = size;

	return size;
}

float	ComputeDefaultSize(int type, int handle, float *size_ptr)
{
	float size = ComputeDefaultSizeFunc(handle, size_ptr, NULL, true); 
	
	if(type != OBJ_WEAPON && type != OBJ_DEBRIS && type != OBJ_POWERUP)
	{
		ComputeDefaultSizeFunc(handle, &Poly_models[handle].wall_size, &Poly_models[handle].wall_size_offset, false);
		ComputeDefaultSizeFunc(handle, &Poly_models[handle].anim_size, &Poly_models[handle].anim_size_offset, true);

		if (type == OBJ_PLAYER)
		{
			Poly_models[handle].anim_size *= PLAYER_SIZE_SCALAR;
			Poly_models[handle].anim_size_offset = Zero_vector;
		}
	}
	else
	{
		if(type == OBJ_POWERUP)
		{
			size *= 2.0f;
			*size_ptr *= 2.0f;
		}

		Poly_models[handle].wall_size = size;
		Poly_models[handle].wall_size_offset = Zero_vector;

		Poly_models[handle].anim_size = size;
		Poly_models[handle].anim_size_offset = Zero_vector;
	}
	
	Poly_models[handle].flags |= PMF_SIZE_COMPUTED;
	
	return size;
}
