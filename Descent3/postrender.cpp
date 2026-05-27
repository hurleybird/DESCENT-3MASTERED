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

#ifdef NEWEDITOR //include first to get rid of ugly warning message about macro redfinitions
#include "..\neweditor\globals.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include "object.h"
#include "viseffect.h"
#include "render.h"
#include "renderobject.h"
#include "room.h"
#include "postrender.h"
#include "config.h"
#include "weapon.h"
#include "terrain.h"
#include "renderer.h"
#include "gameloop.h"
#include "findintersection.h"

postrender_struct Postrender_list[MAX_POSTRENDERS];
int Num_postrenders = 0;

static vector Viewer_eye;
static matrix Viewer_orient;
static int Viewer_roomnum;

static bool PostRenderObjectHasWeaponStreamer(const object* objp)
{
	return objp && objp->type == OBJ_WEAPON && (Weapons[objp->id].flags & WF_STREAMER);
}

static const char* PostRenderObjectTypeName(int type)
{
	switch (type)
	{
	case OBJ_WALL: return "Wall";
	case OBJ_FIREBALL: return "Fireball";
	case OBJ_ROBOT: return "Robot";
	case OBJ_SHARD: return "Shard";
	case OBJ_PLAYER: return "Player";
	case OBJ_WEAPON: return "Weapon";
	case OBJ_VIEWER: return "Viewer";
	case OBJ_POWERUP: return "Powerup";
	case OBJ_DEBRIS: return "Debris";
	case OBJ_CAMERA: return "Camera";
	case OBJ_SHOCKWAVE: return "Shockwave";
	case OBJ_CLUTTER: return "Clutter";
	case OBJ_GHOST: return "Ghost";
	case OBJ_LIGHT: return "Light";
	case OBJ_COOP: return "Coop";
	case OBJ_MARKER: return "Marker";
	case OBJ_BUILDING: return "Building";
	case OBJ_DOOR: return "Door";
	case OBJ_ROOM: return "Room";
	case OBJ_PARTICLE: return "Particle";
	case OBJ_SPLINTER: return "Splinter";
	case OBJ_DUMMY: return "Dummy";
	case OBJ_OBSERVER: return "Observer";
	case OBJ_DEBUG_LINE: return "DebugLine";
	case OBJ_SOUNDSOURCE: return "SoundSource";
	case OBJ_WAYPOINT: return "Waypoint";
	default: return "Other";
	}
}

static void PostRenderRecordCounter(double marker_time, const char* marker_name, int count)
{
	if (count <= 0)
		return;

	char marker[96];
	snprintf(marker, sizeof(marker), "%s=%d", marker_name, count);
	PerfMarkersRecordDuration(marker, marker_time, 0.0);
}

struct PostRenderObjectOcclusionStats
{
	double first_time;
	double total_time;
	int tested;
	int clear;
	int blocked;
	int culled;
	int rays;
	int footprint_clear;
	int outside_tested;
	int outside_blocked;
	int outside_culled;
	int mine_tested;
	int mine_blocked;
	int mine_culled;
	int type_tested[MAX_OBJECT_TYPES];
	int type_blocked[MAX_OBJECT_TYPES];
	int type_culled[MAX_OBJECT_TYPES];
	int hit_wall;
	int hit_object;
	int hit_terrain;
	int hit_backface;
	int hit_bad_start;
	int hit_out_of_bounds;
	int hit_other;
};

static bool PostRenderObjectOcclusionEligible(const object* objp)
{
	if (!objp || objp->render_type != RT_POLYOBJ)
		return false;

	switch (objp->type)
	{
	case OBJ_PLAYER:
	case OBJ_VIEWER:
	case OBJ_OBSERVER:
	case OBJ_WEAPON:
	case OBJ_DOOR:
		return false;
	default:
		return true;
	}
}

static bool PostRenderObjectOcclusionFateBlocks(int fate)
{
	switch (fate)
	{
	case HIT_WALL:
	case HIT_TERRAIN:
	case HIT_BACKFACE:
	case HIT_CEILING:
	case HIT_CORNER_WALL:
	case HIT_EDGE_WALL:
	case HIT_FACE_WALL:
		return true;
	default:
		return false;
	}
}

static int PostRenderObjectOcclusionTracePoint(const object* objp, vector target)
{
	fvi_query fq = {};
	fvi_info hit_info = {};
	fq.p0 = &Viewer_eye;
	fq.p1 = &target;
	fq.startroom = Viewer_roomnum;
	fq.rad = 0.0f;
	fq.thisobjnum = OBJNUM(objp);
	fq.ignore_obj_list = NULL;
	fq.flags = FQ_NO_RELINK | FQ_TRANSPOINT;

	return fvi_FindIntersection(&fq, &hit_info);
}

static bool PostRenderCullObjectByOcclusion(object* objp, bool object_outside,
	PostRenderObjectOcclusionStats& stats)
{
	if (!PostRenderObjectOcclusionEligible(objp))
		return false;
	if (Render_mirror_for_room || !Rendering_main_view)
		return false;
	if (!object_outside && !ROOMNUM_OUTSIDE(Viewer_roomnum) && objp->roomnum == Viewer_roomnum)
		return false;

	double start_time = Perf_markers_enabled ? PerfMarkersNow() : 0.0;
	if (Perf_markers_enabled && stats.first_time == 0.0)
		stats.first_time = start_time;

	int rays = 1;
	int fate = PostRenderObjectOcclusionTracePoint(objp, objp->pos);
	bool blocked = PostRenderObjectOcclusionFateBlocks(fate);
	bool footprint_clear = false;

	if (blocked && objp->size > 0.01f)
	{
		const float sample_radius = objp->size * 1.15f;
		const float diagonal_radius = sample_radius * 0.70710678f;
		vector samples[8];

		samples[0] = objp->pos + (Viewer_orient.rvec * sample_radius);
		samples[1] = objp->pos - (Viewer_orient.rvec * sample_radius);
		samples[2] = objp->pos + (Viewer_orient.uvec * sample_radius);
		samples[3] = objp->pos - (Viewer_orient.uvec * sample_radius);
		samples[4] = objp->pos + (Viewer_orient.rvec * diagonal_radius) + (Viewer_orient.uvec * diagonal_radius);
		samples[5] = objp->pos + (Viewer_orient.rvec * diagonal_radius) - (Viewer_orient.uvec * diagonal_radius);
		samples[6] = objp->pos - (Viewer_orient.rvec * diagonal_radius) + (Viewer_orient.uvec * diagonal_radius);
		samples[7] = objp->pos - (Viewer_orient.rvec * diagonal_radius) - (Viewer_orient.uvec * diagonal_radius);

		for (int sample = 0; sample < 8; sample++)
		{
			rays++;
			const int sample_fate = PostRenderObjectOcclusionTracePoint(objp, samples[sample]);
			if (!PostRenderObjectOcclusionFateBlocks(sample_fate))
			{
				blocked = false;
				footprint_clear = true;
				fate = sample_fate;
				break;
			}
		}
	}

	const bool culled = blocked;

	if (Perf_markers_enabled)
	{
		stats.total_time += PerfMarkersNow() - start_time;
		stats.tested++;
		stats.rays += rays;
		if (footprint_clear)
			stats.footprint_clear++;
		if (objp->type >= 0 && objp->type < MAX_OBJECT_TYPES)
			stats.type_tested[objp->type]++;

		if (object_outside)
			stats.outside_tested++;
		else
			stats.mine_tested++;

		if (fate == HIT_NONE)
			stats.clear++;

		if (blocked)
		{
			stats.blocked++;
			if (objp->type >= 0 && objp->type < MAX_OBJECT_TYPES)
				stats.type_blocked[objp->type]++;
			if (object_outside)
				stats.outside_blocked++;
			else
				stats.mine_blocked++;
		}

		if (culled)
		{
			stats.culled++;
			if (objp->type >= 0 && objp->type < MAX_OBJECT_TYPES)
				stats.type_culled[objp->type]++;
			if (object_outside)
				stats.outside_culled++;
			else
				stats.mine_culled++;
		}

		switch (fate)
		{
		case HIT_WALL:
		case HIT_FACE_WALL:
		case HIT_CORNER_WALL:
		case HIT_EDGE_WALL:
			stats.hit_wall++;
			break;
		case HIT_OBJECT:
		case HIT_SPHERE_2_POLY_OBJECT:
			stats.hit_object++;
			break;
		case HIT_TERRAIN:
			stats.hit_terrain++;
			break;
		case HIT_BACKFACE:
			stats.hit_backface++;
			break;
		case HIT_BAD_P0:
			stats.hit_bad_start++;
			break;
		case HIT_OUT_OF_TERRAIN_BOUNDS:
			stats.hit_out_of_bounds++;
			break;
		default:
			if (fate != HIT_NONE)
				stats.hit_other++;
			break;
		}
	}

	return culled;
}

// Resets out postrender list for a new frame
void ResetPostrenderList()
{
	Num_postrenders = 0;
}

// Sorts our texture states using the quicksort algorithm
void SortPostrenders()
{
	if (Num_postrenders > 0)
		std::sort(&Postrender_list[0], &Postrender_list[Num_postrenders]);
}


void SetupPostrenderRoom(room* rp)
{
	// Setup faces if this is a fogged room
	if (rp->flags & RF_FOG)
		SetupRoomFog(rp, &Viewer_eye, &Viewer_orient, Viewer_roomnum);
}


// Rotates a face, and then renders it
void DrawPostrenderFace(int roomnum, int facenum, bool change_z)
{
	PERF_MARKER_SCOPE("PostRenderFace");
	int i;

	// Always draw as non state limited
	bool save_state = StateLimited;
	StateLimited = false;

	ASSERT(roomnum >= 0 && roomnum < (MAX_ROOMS + MAX_PALETTE_ROOMS));
	ASSERT(Rooms[roomnum].used);

	room* rp = &Rooms[roomnum];

	ASSERT(facenum >= 0 && facenum < rp->num_faces);

	face* fp = &rp->faces[facenum];

	// Rotate points
	rp->wpb_index = 0;
	{
		PERF_MARKER_SCOPE("PostRenderFace.RotateProject");
		for (i = 0; i < fp->num_verts; i++)
		{
			g3_RotatePoint(&World_point_buffer[fp->face_verts[i]], &rp->verts[fp->face_verts[i]]);
			g3_ProjectPoint(&World_point_buffer[fp->face_verts[i]]);
		}
	}

	{
		PERF_MARKER_SCOPE("PostRenderFace.SetupRoom");
		SetupPostrenderRoom(rp);
	}

	// Render!
	if (change_z)
		rend_SetZBufferWriteMask(0);
	{
		PERF_MARKER_SCOPE("PostRenderFace.RenderFace");
		RenderFace(rp, facenum);
	}

	// Render any effects for this face
	if (Num_specular_faces_to_render > 0)
	{
		PERF_MARKER_SCOPE("PostRenderFace.Specular");
		RenderSpecularFacesFlat(rp);
		Num_specular_faces_to_render = 0;
	}

	if (Num_fog_faces_to_render > 0)
	{
		PERF_MARKER_SCOPE("PostRenderFace.Fog");
		RenderFogFaces(rp);
		Num_fog_faces_to_render = 0;
	}

	// Restore statelimited setting
	StateLimited = save_state;

	if (change_z)
		rend_SetZBufferWriteMask(1);
}

// Renders all the objects/viseffects/walls we have in our postrender list
void PostRender(int roomnum)
{
	PERF_MARKER_SCOPE("PostRender");
	{
		PERF_MARKER_SCOPE("PostRender.GetView");
		g3_GetViewPosition(&Viewer_eye);
		g3_GetUnscaledMatrix(&Viewer_orient);
	}

	Viewer_roomnum = roomnum;

	int i, index;
	// Sort the objects
	{
		PERF_MARKER_SCOPE("PostRender.Sort");
		SortPostrenders();
	}
	//qsort(Postrender_list,Num_postrenders,sizeof(*Postrender_list),(int (cdecl *)(const void*,const void*))Postrender_sort_func);

	double vis_effect_time = 0.0;
	double object_time = 0.0;
	double room_setup_time = 0.0;
	double face_time = 0.0;
	double object_outside_time = 0.0;
	double object_mine_time = 0.0;
	double object_type_time[MAX_OBJECT_TYPES] = {};
	double object_outside_type_time[MAX_OBJECT_TYPES] = {};
	double object_mine_type_time[MAX_OBJECT_TYPES] = {};
	double first_vis_effect_time = 0.0;
	double first_object_time = 0.0;
	double first_room_setup_time = 0.0;
	double first_face_time = 0.0;
	int object_outside_count = 0;
	int object_mine_count = 0;
	int object_type_count[MAX_OBJECT_TYPES] = {};
	int object_outside_type_count[MAX_OBJECT_TYPES] = {};
	int object_mine_type_count[MAX_OBJECT_TYPES] = {};
	PostRenderObjectOcclusionStats object_occlusion = {};

	{
		PERF_MARKER_SCOPE("PostRender.RenderItems");
		for (i = Num_postrenders - 1; i >= 0; i--)
		{

			if (Postrender_list[i].type == PRT_VISEFFECT)
			{
				{
					PERF_MARKER_SCOPE("PostRender.FlushWeaponStreamers.BeforeVisEffect");
					FlushWeaponStreamerBatches();
				}
				double start_time = Perf_markers_enabled ? PerfMarkersNow() : 0.0;
				if (first_vis_effect_time == 0.0)
					first_vis_effect_time = start_time;
				index = Postrender_list[i].visnum;
				DrawVisEffectMaybeBatched(&VisEffects[index]);
				if (Perf_markers_enabled)
					vis_effect_time += PerfMarkersNow() - start_time;
			}
			else if (Postrender_list[i].type == PRT_OBJECT)
			{
				object* objp = &Objects[Postrender_list[i].objnum];
				bool object_outside = OBJECT_OUTSIDE(objp);
				if (PostRenderCullObjectByOcclusion(objp, object_outside, object_occlusion))
					continue;
				{
					PERF_MARKER_SCOPE("PostRender.FlushVisEffects.BeforeObject");
					FlushVisEffectBatches();
				}
				double object_start_time = Perf_markers_enabled ? PerfMarkersNow() : 0.0;
				if (first_object_time == 0.0)
					first_object_time = object_start_time;
				if (!PostRenderObjectHasWeaponStreamer(objp))
				{
					PERF_MARKER_SCOPE("PostRender.FlushWeaponStreamers.BeforeObject");
					FlushWeaponStreamerBatches();
				}
				if (objp->type == OBJ_POWERUP)
				{
					PERF_MARKER_SCOPE("PostRender.ForceFlushVisEffects.BeforePowerup");
					ForceFlushVisEffectBatches();
				}

				if (!object_outside)
				{
					double setup_start_time = Perf_markers_enabled ? PerfMarkersNow() : 0.0;
					if (first_room_setup_time == 0.0)
						first_room_setup_time = setup_start_time;
					SetupPostrenderRoom(&Rooms[objp->roomnum]);
					if (Perf_markers_enabled)
						room_setup_time += PerfMarkersNow() - setup_start_time;
				}
				RenderObject(objp);
				if (Perf_markers_enabled)
				{
					double duration = PerfMarkersNow() - object_start_time;
					object_time += duration;
					if (object_outside)
					{
						object_outside_time += duration;
						object_outside_count++;
					}
					else
					{
						object_mine_time += duration;
						object_mine_count++;
					}
					if (objp->type >= 0 && objp->type < MAX_OBJECT_TYPES)
					{
						object_type_time[objp->type] += duration;
						object_type_count[objp->type]++;
						if (object_outside)
						{
							object_outside_type_time[objp->type] += duration;
							object_outside_type_count[objp->type]++;
						}
						else
						{
							object_mine_type_time[objp->type] += duration;
							object_mine_type_count[objp->type]++;
						}
					}
				}
			}
			else
			{
				{
					PERF_MARKER_SCOPE("PostRender.FlushVisEffects.BeforeFace");
					FlushVisEffectBatches();
				}
				{
					PERF_MARKER_SCOPE("PostRender.FlushWeaponStreamers.BeforeFace");
					FlushWeaponStreamerBatches();
				}
				double start_time = Perf_markers_enabled ? PerfMarkersNow() : 0.0;
				if (first_face_time == 0.0)
					first_face_time = start_time;
				// Do room face
				DrawPostrenderFace(Postrender_list[i].roomnum, Postrender_list[i].facenum);
				if (Perf_markers_enabled)
					face_time += PerfMarkersNow() - start_time;
			}
		}
		{
			PERF_MARKER_SCOPE("PostRender.ForceFlushVisEffects.End");
			ForceFlushVisEffectBatches();
		}
		{
			PERF_MARKER_SCOPE("PostRender.FlushWeaponStreamers.End");
			FlushWeaponStreamerBatches();
		}
	}
	if (Perf_markers_enabled)
	{
		if (vis_effect_time > 0.0)
			PerfMarkersRecordDuration("PostRender.VisEffects.Aggregate", first_vis_effect_time, vis_effect_time);
		if (object_time > 0.0)
			PerfMarkersRecordDuration("PostRender.Objects.Aggregate", first_object_time, object_time);
		if (object_outside_time > 0.0)
		{
			char marker[96];
			snprintf(marker, sizeof(marker), "PostRender.Objects.Outside.Count=%d", object_outside_count);
			PerfMarkersRecordDuration(marker, first_object_time, object_outside_time);
		}
		if (object_mine_time > 0.0)
		{
			char marker[96];
			snprintf(marker, sizeof(marker), "PostRender.Objects.Mine.Count=%d", object_mine_count);
			PerfMarkersRecordDuration(marker, first_object_time, object_mine_time);
		}
		for (int object_type = 0; object_type < MAX_OBJECT_TYPES; object_type++)
		{
			if (object_type_count[object_type] <= 0)
				continue;

			char marker[96];
			snprintf(marker, sizeof(marker), "PostRender.Objects.Type.%s.Count=%d",
				PostRenderObjectTypeName(object_type), object_type_count[object_type]);
			PerfMarkersRecordDuration(marker, first_object_time, object_type_time[object_type]);

			if (object_outside_type_count[object_type] > 0)
			{
				snprintf(marker, sizeof(marker), "PostRender.Objects.Outside.Type.%s.Count=%d",
					PostRenderObjectTypeName(object_type), object_outside_type_count[object_type]);
				PerfMarkersRecordDuration(marker, first_object_time, object_outside_type_time[object_type]);
			}
			if (object_mine_type_count[object_type] > 0)
			{
				snprintf(marker, sizeof(marker), "PostRender.Objects.Mine.Type.%s.Count=%d",
					PostRenderObjectTypeName(object_type), object_mine_type_count[object_type]);
				PerfMarkersRecordDuration(marker, first_object_time, object_mine_type_time[object_type]);
			}
		}
		if (object_occlusion.tested > 0)
		{
			char marker[96];
			snprintf(marker, sizeof(marker), "PostRender.ObjectOcclusion.Tests.Count=%d",
				object_occlusion.tested);
			PerfMarkersRecordDuration(marker, object_occlusion.first_time, object_occlusion.total_time);
			PostRenderRecordCounter(object_occlusion.first_time, "PostRender.ObjectOcclusion.Rays.Count",
				object_occlusion.rays);
			PostRenderRecordCounter(object_occlusion.first_time, "PostRender.ObjectOcclusion.FootprintClear.Count",
				object_occlusion.footprint_clear);
			PostRenderRecordCounter(object_occlusion.first_time, "PostRender.ObjectOcclusion.Clear.Count",
				object_occlusion.clear);
			PostRenderRecordCounter(object_occlusion.first_time, "PostRender.ObjectOcclusion.Blocked.Count",
				object_occlusion.blocked);
			PostRenderRecordCounter(object_occlusion.first_time, "PostRender.ObjectOcclusion.Culled.Count",
				object_occlusion.culled);
			PostRenderRecordCounter(object_occlusion.first_time, "PostRender.ObjectOcclusion.OutsideTested.Count",
				object_occlusion.outside_tested);
			PostRenderRecordCounter(object_occlusion.first_time, "PostRender.ObjectOcclusion.OutsideBlocked.Count",
				object_occlusion.outside_blocked);
			PostRenderRecordCounter(object_occlusion.first_time, "PostRender.ObjectOcclusion.OutsideCulled.Count",
				object_occlusion.outside_culled);
			PostRenderRecordCounter(object_occlusion.first_time, "PostRender.ObjectOcclusion.MineTested.Count",
				object_occlusion.mine_tested);
			PostRenderRecordCounter(object_occlusion.first_time, "PostRender.ObjectOcclusion.MineBlocked.Count",
				object_occlusion.mine_blocked);
			PostRenderRecordCounter(object_occlusion.first_time, "PostRender.ObjectOcclusion.MineCulled.Count",
				object_occlusion.mine_culled);
			PostRenderRecordCounter(object_occlusion.first_time, "PostRender.ObjectOcclusion.HitWall.Count",
				object_occlusion.hit_wall);
			PostRenderRecordCounter(object_occlusion.first_time, "PostRender.ObjectOcclusion.HitObject.Count",
				object_occlusion.hit_object);
			PostRenderRecordCounter(object_occlusion.first_time, "PostRender.ObjectOcclusion.HitTerrain.Count",
				object_occlusion.hit_terrain);
			PostRenderRecordCounter(object_occlusion.first_time, "PostRender.ObjectOcclusion.HitBackface.Count",
				object_occlusion.hit_backface);
			PostRenderRecordCounter(object_occlusion.first_time, "PostRender.ObjectOcclusion.HitBadStart.Count",
				object_occlusion.hit_bad_start);
			PostRenderRecordCounter(object_occlusion.first_time, "PostRender.ObjectOcclusion.HitOutOfBounds.Count",
				object_occlusion.hit_out_of_bounds);
			PostRenderRecordCounter(object_occlusion.first_time, "PostRender.ObjectOcclusion.HitOther.Count",
				object_occlusion.hit_other);
			for (int object_type = 0; object_type < MAX_OBJECT_TYPES; object_type++)
			{
				if (object_occlusion.type_tested[object_type] <= 0)
					continue;
				snprintf(marker, sizeof(marker), "PostRender.ObjectOcclusion.Type.%s.Tested.Count",
					PostRenderObjectTypeName(object_type));
				PostRenderRecordCounter(object_occlusion.first_time, marker, object_occlusion.type_tested[object_type]);
				snprintf(marker, sizeof(marker), "PostRender.ObjectOcclusion.Type.%s.Blocked.Count",
					PostRenderObjectTypeName(object_type));
				PostRenderRecordCounter(object_occlusion.first_time, marker, object_occlusion.type_blocked[object_type]);
				snprintf(marker, sizeof(marker), "PostRender.ObjectOcclusion.Type.%s.Culled.Count",
					PostRenderObjectTypeName(object_type));
				PostRenderRecordCounter(object_occlusion.first_time, marker, object_occlusion.type_culled[object_type]);
			}
		}
		if (room_setup_time > 0.0)
			PerfMarkersRecordDuration("PostRender.SetupRooms.Aggregate", first_room_setup_time, room_setup_time);
		if (face_time > 0.0)
			PerfMarkersRecordDuration("PostRender.Faces.Aggregate", first_face_time, face_time);
	}
	Num_postrenders = 0;
	rend_SetFogState(0);
	{
		PERF_MARKER_SCOPE("PostRender.RenderLightGlows");
		RenderLightGlows();
	}
}
