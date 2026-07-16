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
	bool separated_object[MAX_POSTRENDERS] = {};
	bool separated_object_rendered[MAX_POSTRENDERS] = {};
	unsigned int separated_object_random_state[MAX_POSTRENDERS] = {};
	// GL4 depth consumers (GTAO, motion vectors, and soft particles) all require
	// the same stable set of opaque polygon depth.  Keep the split independent of
	// the soft-particle toggle so changing one effect cannot remove object depth
	// from another.  Compatibility GL keeps its legacy one-pass ordering.
	const bool separate_polygon_objects = rend_CanUseNewrender();
	if (separate_polygon_objects)
	{
		PERF_MARKER_SCOPE("PostRender.ObjectOpaquePass");
		ForceFlushVisEffectBatches();
		FlushWeaponStreamerBatches();
		for (i = 0; i < Num_postrenders; i++)
		{
			if (Postrender_list[i].type != PRT_OBJECT)
				continue;
			object* objp = &Objects[Postrender_list[i].objnum];
			if (!RenderObjectCanUseSeparatedPasses(objp))
				continue;

			separated_object[i] = true;
			const bool object_outside = OBJECT_OUTSIDE(objp);

			if (!object_outside)
			{
				double setup_start_time = Perf_markers_enabled ? PerfMarkersNow() : 0.0;
				if (first_room_setup_time == 0.0)
					first_room_setup_time = setup_start_time;
				SetupPostrenderRoom(&Rooms[objp->roomnum]);
				if (Perf_markers_enabled)
					room_setup_time += PerfMarkersNow() - setup_start_time;
			}

			double object_start_time = Perf_markers_enabled ? PerfMarkersNow() : 0.0;
			if (first_object_time == 0.0)
				first_object_time = object_start_time;
			separated_object_rendered[i] = RenderObjectOpaque(
				objp, &separated_object_random_state[i]);
			if (Perf_markers_enabled && separated_object_rendered[i])
			{
				const double duration = PerfMarkersNow() - object_start_time;
				object_time += duration;
				if (objp->type >= 0 && objp->type < MAX_OBJECT_TYPES)
					object_type_time[objp->type] += duration;
				if (object_outside)
				{
					object_outside_time += duration;
					if (objp->type >= 0 && objp->type < MAX_OBJECT_TYPES)
						object_outside_type_time[objp->type] += duration;
				}
				else
				{
					object_mine_time += duration;
					if (objp->type >= 0 && objp->type < MAX_OBJECT_TYPES)
						object_mine_type_time[objp->type] += duration;
				}
			}
		}
	}

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
				const bool object_was_separated = separated_object[i];
				bool object_outside = OBJECT_OUTSIDE(objp);
				if (object_was_separated && !separated_object_rendered[i])
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
				if (object_was_separated)
					RenderObjectTransparents(objp, separated_object_random_state[i]);
				else
				{
					// In GL4, non-polygon post-render objects are transparent items.  The
					// lock prevents old helpers (shards, splinters, smolder, bitmap
					// weapons) from accidentally restoring depth writes mid-queue.  A
					// fallback polygon model must retain legacy depth writes.
					const bool lock_transparent_depth = separate_polygon_objects &&
						objp->render_type != RT_POLYOBJ;
					if (lock_transparent_depth)
						rend_BeginDepthWriteLock();
					RenderObject(objp);
					if (lock_transparent_depth)
						rend_EndDepthWriteLock();
				}
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
