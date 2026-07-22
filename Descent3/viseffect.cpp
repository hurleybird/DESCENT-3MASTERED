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

#include <algorithm>
#include <vector>
#include "viseffect.h"
#include "fireball.h"
#include "terrain.h"
#include "game.h"
#include "room.h"
#include "vclip.h"
#include "bitmap.h"
#include "gametexture.h"
#include "object.h"
#include <memory.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <float.h>
#include "PHYSICS.H"
#include "weapon.h"
#include "lighting.h"
#include "dedicated_server.h"
#include "player.h"
#include "config.h"
#include "gameloop.h"
#include "weather.h"
#include "render.h"
#include "polymodel.h"
#include "renderer.h"
#include "../renderer/HardwareInternal.h"
#include "psrand.h"
#include "mem.h"

vis_effect* VisEffects = NULL;
static short* Vis_free_list = NULL;
ushort* VisDeadList = NULL;

ushort max_vis_effects = 0;

int NumVisDead = 0;
int Highest_vis_effect_index = 0;
int Num_vis_effects = 0;

static const float SNOW_FADE_IN_TIME = 0.1f;
static int Enhanced_snow_atlas_handle = BAD_BITMAP_HANDLE;
static int Enhanced_snow_fallback_handle = BAD_BITMAP_HANDLE;
static std::vector<int> Close_screen_weapon_object_handles;

enum
{
	CLOSE_SCREEN_LATE_VISEFFECT,
	CLOSE_SCREEN_LATE_WEAPON_OBJECT,
	CLOSE_SCREEN_LATE_ALPHA
};

struct CloseScreenLateRenderItem
{
	ubyte type;
	short index;
	int handle;
	float z;
	float r;
	float g;
	float b;
	float alpha;
	int sequence;
};

static std::vector<CloseScreenLateRenderItem> Close_screen_late_items;
static std::vector<unsigned char> Close_screen_late_vis_queued;
static std::vector<int> Close_screen_late_object_handles;
static int Close_screen_late_sequence = 0;
static bool Close_screen_collecting_late = false;
static bool Close_screen_rendering_late = false;
static vector Close_screen_late_view_pos;
static matrix Close_screen_late_view_orient;
static bool Close_screen_late_view_valid = false;

static float VisEffectSnowFadeInFactor(float time_live)
{
	if (time_live <= 0.0f)
		return 0.0f;
	if (time_live >= SNOW_FADE_IN_TIME)
		return 1.0f;
	return time_live / SNOW_FADE_IN_TIME;
}

static float EnhancedSnowGaussian(float x, float y, float cx, float cy,
	float radius_x, float radius_y, float angle_radians)
{
	const float c = cosf(angle_radians);
	const float s = sinf(angle_radians);
	const float dx = x - cx;
	const float dy = y - cy;
	const float along = (dx * c + dy * s) / radius_x;
	const float across = (-dx * s + dy * c) / radius_y;
	return expf(-(along * along + across * across) * 2.2f);
}

static float EnhancedSnowCoverage(int variant, float x, float y)
{
	float coverage;
	if (variant == 0)
	{
		coverage = EnhancedSnowGaussian(x, y, 0.0f, 0.0f, 0.66f, 0.19f, 0.08f);
		coverage = std::max(coverage, 0.62f *
			EnhancedSnowGaussian(x, y, -0.23f, 0.18f, 0.30f, 0.13f, -0.35f));
	}
	else if (variant == 1)
	{
		coverage = EnhancedSnowGaussian(x, y, -0.15f, -0.02f, 0.42f, 0.25f, 0.24f);
		coverage = std::max(coverage, 0.82f *
			EnhancedSnowGaussian(x, y, 0.27f, 0.13f, 0.29f, 0.20f, -0.26f));
		coverage = std::max(coverage, 0.48f *
			EnhancedSnowGaussian(x, y, 0.03f, -0.28f, 0.22f, 0.12f, 0.08f));
	}
	else if (variant == 2)
	{
		coverage = EnhancedSnowGaussian(x, y, 0.0f, 0.0f, 0.74f, 0.115f, 0.34f);
		coverage = std::max(coverage, 0.38f *
			EnhancedSnowGaussian(x, y, -0.08f, -0.11f, 0.40f, 0.17f, 0.05f));
	}
	else
	{
		coverage = EnhancedSnowGaussian(x, y, 0.0f, 0.0f, 0.53f, 0.17f, -0.18f);
		coverage = std::max(coverage, 0.55f *
			EnhancedSnowGaussian(x, y, 0.18f, -0.16f, 0.24f, 0.10f, 0.30f));
	}

	return coverage > 1.0f ? 1.0f : coverage;
}

static ushort EnhancedSnowPixel(int variant, int x, int y)
{
	float coverage = 0.0f;
	for (int sy = 0; sy < 2; ++sy)
	{
		for (int sx = 0; sx < 2; ++sx)
		{
			const float px = ((x + (sx + 0.5f) * 0.5f) / 32.0f) * 2.0f - 1.0f;
			const float py = ((y + (sy + 0.5f) * 0.5f) / 32.0f) * 2.0f - 1.0f;
			coverage += EnhancedSnowCoverage(variant, px, py);
		}
	}
	coverage *= 0.25f;
	if (coverage <= 0.025f)
		return NEW_TRANSPARENT_COLOR;

	int value = (int)(30.0f + 225.0f * powf(coverage, 0.65f));
	if (value > 255)
		value = 255;
	return GR_RGB16(value - 12, value - 5, value) | OPAQUE_FLAG;
}

static bool VisEffectCreateEnhancedSnowTextures()
{
	if (Enhanced_snow_atlas_handle > BAD_BITMAP_HANDLE &&
		GameBitmaps[Enhanced_snow_atlas_handle].used &&
		Enhanced_snow_fallback_handle > BAD_BITMAP_HANDLE &&
		GameBitmaps[Enhanced_snow_fallback_handle].used)
	{
		return true;
	}

	Enhanced_snow_atlas_handle = bm_AllocBitmap(128, 32, 0);
	Enhanced_snow_fallback_handle = bm_AllocBitmap(32, 32, 0);
	if (Enhanced_snow_atlas_handle <= BAD_BITMAP_HANDLE ||
		Enhanced_snow_fallback_handle <= BAD_BITMAP_HANDLE)
	{
		if (Enhanced_snow_atlas_handle > BAD_BITMAP_HANDLE)
			bm_FreeBitmap(Enhanced_snow_atlas_handle);
		if (Enhanced_snow_fallback_handle > BAD_BITMAP_HANDLE)
			bm_FreeBitmap(Enhanced_snow_fallback_handle);
		Enhanced_snow_atlas_handle = BAD_BITMAP_HANDLE;
		Enhanced_snow_fallback_handle = BAD_BITMAP_HANDLE;
		return false;
	}

	ushort* atlas = bm_data(Enhanced_snow_atlas_handle, 0);
	ushort* fallback = bm_data(Enhanced_snow_fallback_handle, 0);
	for (int y = 0; y < 32; ++y)
	{
		for (int x = 0; x < 32; ++x)
		{
			fallback[y * 32 + x] = EnhancedSnowPixel(0, x, y);
			for (int variant = 0; variant < 4; ++variant)
				atlas[y * 128 + variant * 32 + x] = EnhancedSnowPixel(variant, x, y);
		}
	}
	GameBitmaps[Enhanced_snow_atlas_handle].flags |= BF_CHANGED;
	GameBitmaps[Enhanced_snow_fallback_handle].flags |= BF_CHANGED;
	return true;
}

struct EnhancedSnowRenderParams
{
	angle rotation;
	float width;
	float height;
	float alpha;
};

static EnhancedSnowRenderParams VisEffectEnhancedSnowRenderParams(const vis_effect* vis,
	float time_live, float norm_time)
{
	EnhancedSnowRenderParams params = {};
	if (vis->custom_handle & 4)
	{
		params.rotation = 0;
		params.width = vis->size * 0.82f;
		params.height = vis->size * 0.48f;
		float settle_fade = vis->lifeleft / 0.32f;
		if (settle_fade > 1.0f) settle_fade = 1.0f;
		if (settle_fade < 0.0f) settle_fade = 0.0f;
		params.alpha = settle_fade * 0.26f;
		return params;
	}

	const float phase = vis->mass + time_live * vis->drag;
	vector relative_velocity = vis->velocity;
	if (Viewer_object && Viewer_object->movement_type == MT_PHYSICS)
		relative_velocity -= Viewer_object->mtype.phys_info.velocity;

	matrix view_orient;
	g3_GetUnscaledMatrix(&view_orient);
	const float screen_x = vm_DotProduct(&relative_velocity, &view_orient.rvec);
	const float screen_y = vm_DotProduct(&relative_velocity, &view_orient.uvec);
	const float fall_angle = atan2f(-screen_x, -screen_y);
	params.rotation = (angle)((int)((fall_angle + sinf(phase) * 0.42f) *
		(65536.0f / 6.28318530718f)));

	const float tumble = 0.48f + 0.52f * fabsf(cosf(phase * 0.73f));
	float relative_speed = vm_GetMagnitudeFast(&relative_velocity);
	vector to_view = vis->pos - Viewer_object->pos;
	const float distance = vm_GetMagnitudeFast(&to_view);
	float proximity = 1.0f - distance / 85.0f;
	if (proximity < 0.0f) proximity = 0.0f;
	if (proximity > 1.0f) proximity = 1.0f;
	float streak = proximity * relative_speed * 0.015f;
	if (streak > 1.45f) streak = 1.45f;

	params.width = vis->size * (0.42f + tumble * 0.38f);
	params.height = vis->size * (1.10f + streak);
	const float fade_in = time_live < 0.24f ? time_live / 0.24f : 1.0f;
	const float fade_out = norm_time > 0.82f ? (1.0f - norm_time) / 0.18f : 1.0f;
	float size_alpha = 0.45f + vis->size * 0.28f;
	if (size_alpha > 0.75f) size_alpha = 0.75f;
	params.alpha = fade_in * fade_out * size_alpha;
	if (params.alpha < 0.0f) params.alpha = 0.0f;
	return params;
}

static bool VisEffectCanUseLateCloseScreenPass()
{
	return Renderer_type == RENDERER_OPENGL &&
		OpenGLProfile == GLPROFILE_CORE &&
		VisEffectViewerIsFirstPersonLocalPlayerView();
}

bool VisEffectViewerIsFirstPersonLocalPlayerView()
{
	if (Player_object == NULL || Viewer_object == NULL)
		return false;
	if (Viewer_object != Player_object)
		return false;
	if (Players[Player_num].flags & PLAYER_FLAGS_REARVIEW)
		return false;

	return true;
}

static void VisEffectClearCloseScreenLateQueue()
{
	Close_screen_late_items.clear();
	Close_screen_late_object_handles.clear();
	if (!Close_screen_late_vis_queued.empty())
		std::fill(Close_screen_late_vis_queued.begin(), Close_screen_late_vis_queued.end(), 0);
	Close_screen_late_sequence = 0;
	Close_screen_late_view_valid = false;
}

static float VisEffectLateRenderZ(const vector* pos)
{
	vector eye;
	matrix orient;

	g3_GetViewPosition(&eye);
	g3_GetUnscaledMatrix(&orient);
	if (!Close_screen_late_view_valid)
	{
		Close_screen_late_view_pos = eye;
		Close_screen_late_view_orient = orient;
		Close_screen_late_view_valid = true;
	}

	vector delta = *pos - eye;
	return vm_DotProduct(&delta, &orient.fvec);
}

void VisEffectBeginCloseScreenFrame()
{
	VisEffectClearCloseScreenLateQueue();
	Close_screen_rendering_late = false;
	Close_screen_collecting_late = VisEffectCanUseLateCloseScreenPass();
}

void VisEffectEndCloseScreenCollection()
{
	Close_screen_collecting_late = false;
}

void ShutdownVisEffects()
{
	if (Enhanced_snow_atlas_handle > BAD_BITMAP_HANDLE)
		bm_FreeBitmap(Enhanced_snow_atlas_handle);
	if (Enhanced_snow_fallback_handle > BAD_BITMAP_HANDLE)
		bm_FreeBitmap(Enhanced_snow_fallback_handle);
	Enhanced_snow_atlas_handle = BAD_BITMAP_HANDLE;
	Enhanced_snow_fallback_handle = BAD_BITMAP_HANDLE;
	if (VisEffects)
		mem_free(VisEffects);
	if (VisDeadList)
		mem_free(VisDeadList);
	if (Vis_free_list)
		mem_free(Vis_free_list);
}

// Goes through our array and clears the slots out
void InitVisEffects()
{
	static ushort old_max_vis = 0;
	max_vis_effects = MAX_VIS_EFFECTS;		//always peg to max on PC
	Close_screen_weapon_object_handles.clear();
	VisEffectClearCloseScreenLateQueue();

	if (old_max_vis == max_vis_effects)
		return;

	if (VisEffects != NULL)
	{
		VisEffects = (vis_effect*)mem_realloc(VisEffects, sizeof(vis_effect) * max_vis_effects);
		VisDeadList = (ushort*)mem_realloc(VisDeadList, sizeof(ushort) * max_vis_effects);
		Vis_free_list = (short*)mem_realloc(Vis_free_list, sizeof(short) * max_vis_effects);
	}
	else if (VisEffects == NULL)
	{
		VisEffects = (vis_effect*)mem_malloc(sizeof(vis_effect) * max_vis_effects);
		VisDeadList = (ushort*)mem_malloc(sizeof(ushort) * max_vis_effects);
		Vis_free_list = (short*)mem_malloc(sizeof(short) * max_vis_effects);
}
	for (int i = 0; i < max_vis_effects; i++)
	{
		VisEffects[i].type = VIS_NONE;
		VisEffects[i].roomnum = -1;
		VisEffects[i].prev = -1;
		VisEffects[i].next = -1;
		Vis_free_list[i] = i;
	}
	Close_screen_late_vis_queued.assign(max_vis_effects, 0);
	old_max_vis = max_vis_effects;
	Num_vis_effects = 0;
	Highest_vis_effect_index = 0;

	atexit(ShutdownVisEffects);
}


// Returns the next free viseffect
int VisEffectAllocate()
{
	if (Num_vis_effects == max_vis_effects)
	{
		mprintf((0, "Couldn't allocate vis effect!\n"));
		return -1;
	}

	int n = Vis_free_list[Num_vis_effects++];
	ASSERT(VisEffects[n].type == VIS_NONE);	// Get Jason

	if (n > Highest_vis_effect_index)
	{
		Highest_vis_effect_index = n;
	}

	return n;
}

// Frees up a viseffect for use
int VisEffectFree(int visnum)
{
	ASSERT(visnum >= 0 && visnum <= max_vis_effects);

	Vis_free_list[--Num_vis_effects] = visnum;
	VisEffects[visnum].type = VIS_NONE;

	if (visnum == Highest_vis_effect_index)
	{
		while (VisEffects[Highest_vis_effect_index].type != VIS_NONE && Highest_vis_effect_index > 0)
			Highest_vis_effect_index--;
	}

	return 1;
}

int VisEffectInitType(vis_effect* vis)
{
	int ret = 1;

	vis->size = Fireballs[vis->id].size;
	vis->flags |= VF_USES_LIFELEFT;
	vis->lifeleft = Fireballs[vis->id].total_life;
	vis->lifetime = vis->lifeleft;

	if (Fireballs[vis->id].type == FT_EXPLOSION && vis->id != CUSTOM_EXPLOSION_INDEX && vis->id != NAPALM_BALL_INDEX)
		vis->lighting_color = OPAQUE_FLAG | GR_RGB16(255, 180, 20);

	return ret;
}


//initialize a new viseffect.  adds to the list for the given room
//returns the vis number
int VisEffectCreate(ubyte type, ubyte id, int roomnum, vector* pos)
{
	int visnum;
	vis_effect* vis;

	if ((Game_mode & GM_MULTI) && Dedicated_server)
		return -1;	// Dedicated servers don't need to create vis effects

	if (ROOMNUM_OUTSIDE(roomnum))
	{
		if (CELLNUM(roomnum) > TERRAIN_WIDTH * TERRAIN_DEPTH)
			return -1;
		if (CELLNUM(roomnum) < 0)
			return -1;

		if (Gametime - Last_terrain_render_time > 5.0)
			return -1;
	}
	else
	{
		ASSERT(roomnum >= 0 && roomnum <= Highest_room_index && Rooms[roomnum].used);

		// Don't create viseffects in rooms we haven't seen in a while
		if (Gametime - Rooms[roomnum].last_render_time > 5.0)
			return -1;
	}

	// Find next free object
	visnum = VisEffectAllocate();

	if (visnum == -1)		//no free objects
		return -1;

	ASSERT(VisEffects[visnum].type == VIS_NONE);		//make sure unused 

	vis = &VisEffects[visnum];

	ASSERT(vis->roomnum == -1);

	memset(vis, 0, sizeof(vis_effect));

	//Fill in fields
	vis->type = VIS_FIREBALL;
	vis->id = id;
	vis->pos = *pos;
	vis->flags = 0;
	vis->phys_flags = 0;
	vis->roomnum = roomnum;
	vis->movement_type = MT_NONE;
	vis->attach_info.end_vertnum = -1;
	vis->next = -1;
	vis->prev = -1;
	vis->lighting_color = 0;

	ASSERT(roomnum != -1);

	vis->creation_time = Gametime;

	//Initialize all the type-specific info
	VisEffectInitType(vis);

	VisEffectLink(visnum, roomnum);

	return visnum;
}

bool VisEffectIsCloseScreenSourceObject(object* obj)
{
	if (obj == NULL)
		return false;
	if (!VisEffectViewerIsFirstPersonLocalPlayerView())
		return false;

	return obj == Player_object;
}

bool VisEffectIsLocalPlayerAttachedSourceObject(object* obj)
{
	if (obj == NULL || Player_object == NULL)
		return false;
	if (!VisEffectViewerIsFirstPersonLocalPlayerView())
		return false;

	if (obj == Player_object)
		return true;

	return (obj->flags & OF_ATTACHED) != 0 && ObjGet(obj->attach_ultimate_handle) == Player_object;
}

bool VisEffectIsLocalPlayerViewSourceObject(object* obj)
{
	if (obj == NULL || Player_object == NULL)
		return false;

	return VisEffectIsLocalPlayerAttachedSourceObject(obj);
}

bool VisEffectIsNearLocalPlayerView(object* obj, float padding)
{
	if (obj == NULL || Player_object == NULL || Viewer_object == NULL)
		return false;
	if (!VisEffectViewerIsFirstPersonLocalPlayerView())
		return false;

	float player_view_distance = vm_VectorDistanceQuick(&Player_object->pos, &Viewer_object->pos);
	float player_envelope = player_view_distance + Player_object->size + obj->size + padding;
	if (vm_VectorDistanceQuick(&obj->pos, &Player_object->pos) <= player_envelope)
		return true;

	float viewer_envelope = obj->size + padding;
	return vm_VectorDistanceQuick(&obj->pos, &Viewer_object->pos) <= viewer_envelope;
}

static bool VisEffectShouldQueueLateCloseScreenItem()
{
	return Close_screen_collecting_late && !Close_screen_rendering_late &&
		!Render_mirror_for_room &&
		VisEffectViewerIsFirstPersonLocalPlayerView();
}

static bool VisEffectQueueCloseScreenVisEffect(vis_effect* vis)
{
	if (!VisEffectShouldQueueLateCloseScreenItem() || vis == NULL)
		return false;

	int visnum = vis - VisEffects;
	if (visnum < 0 || visnum >= max_vis_effects)
		return false;

	if (Close_screen_late_vis_queued.size() != max_vis_effects)
		Close_screen_late_vis_queued.assign(max_vis_effects, 0);

	if (Close_screen_late_vis_queued[visnum])
		return true;

	Close_screen_late_vis_queued[visnum] = 1;
	CloseScreenLateRenderItem item = {};
	item.type = CLOSE_SCREEN_LATE_VISEFFECT;
	item.index = (short)visnum;
	item.handle = -1;
	item.z = VisEffectLateRenderZ(&vis->pos);
	item.sequence = Close_screen_late_sequence++;
	Close_screen_late_items.push_back(item);
	return true;
}

bool VisEffectQueueCloseScreenWeaponObject(object* obj)
{
	if (!VisEffectShouldQueueLateCloseScreenItem() || obj == NULL ||
		obj->handle == OBJECT_HANDLE_NONE)
		return false;

	if (std::find(Close_screen_late_object_handles.begin(),
			Close_screen_late_object_handles.end(), obj->handle) != Close_screen_late_object_handles.end())
	{
		return true;
	}

	Close_screen_late_object_handles.push_back(obj->handle);
	CloseScreenLateRenderItem item = {};
	item.type = CLOSE_SCREEN_LATE_WEAPON_OBJECT;
	item.index = -1;
	item.handle = obj->handle;
	item.z = VisEffectLateRenderZ(&obj->pos);
	item.sequence = Close_screen_late_sequence++;
	Close_screen_late_items.push_back(item);
	return true;
}

bool VisEffectQueueCloseScreenAlpha(float r, float g, float b, float alpha)
{
	if (!VisEffectShouldQueueLateCloseScreenItem())
		return false;

	CloseScreenLateRenderItem item = {};
	item.type = CLOSE_SCREEN_LATE_ALPHA;
	item.index = -1;
	item.handle = -1;
	item.z = -FLT_MAX;
	item.r = r;
	item.g = g;
	item.b = b;
	item.alpha = alpha;
	item.sequence = Close_screen_late_sequence++;
	Close_screen_late_items.push_back(item);
	return true;
}

void VisEffectMarkCloseScreenWeaponObject(object* obj)
{
	if (obj == NULL || obj->handle == OBJECT_HANDLE_NONE)
		return;
	if (!VisEffectViewerIsFirstPersonLocalPlayerView())
		return;

	if (std::find(Close_screen_weapon_object_handles.begin(), Close_screen_weapon_object_handles.end(), obj->handle) != Close_screen_weapon_object_handles.end())
		return;

	if (Close_screen_weapon_object_handles.size() >= MAX_OBJECTS)
		Close_screen_weapon_object_handles.erase(Close_screen_weapon_object_handles.begin());

	Close_screen_weapon_object_handles.push_back(obj->handle);
}

bool VisEffectIsCloseScreenWeaponObject(object* obj)
{
	if (obj == NULL || obj->type != OBJ_WEAPON)
		return false;
	if (!VisEffectViewerIsFirstPersonLocalPlayerView())
		return false;

	return std::find(Close_screen_weapon_object_handles.begin(), Close_screen_weapon_object_handles.end(), obj->handle) != Close_screen_weapon_object_handles.end();
}

// Creates vis effects but has the caller set their parameters
//initialize a new viseffect.  adds to the list for the given room
//returns the vis number
int VisEffectCreateControlled(ubyte type, object* parent, ubyte id, int roomnum, vector* pos, float lifetime, vector* velocity, int phys_flags, float size, float drag, float mass, bool isreal)
{
	int visnum;
	vis_effect* vis;
	static int napalm_id = -1;

	if (napalm_id == -1)
		napalm_id = FindWeaponName("Napalm");

	if (isreal)
	{
		ASSERT(parent != NULL);	// You have a spew that has no parent object?

		if (ROOMNUM_OUTSIDE(roomnum))
		{
			if (CELLNUM(roomnum) > TERRAIN_WIDTH * TERRAIN_DEPTH)
				return -1;
			if (CELLNUM(roomnum) < 0)
				return -1;

			if (Gametime - Last_terrain_render_time > 5.0)
				return -1;
		}
		else
		{
			ASSERT(roomnum >= 0 && roomnum <= Highest_room_index && Rooms[roomnum].used);

			// Don't create real objects in rooms we haven't seen in a while
			if (Gametime - Rooms[roomnum].last_render_time > 5.0)
				return -1;
		}

		int objnum = CreateAndFireWeapon(pos, velocity, parent, napalm_id);
		if (objnum >= 0)
		{
			object* obj = &Objects[objnum];

			obj->lifeleft = lifetime;
			obj->lifetime = lifetime;
			obj->movement_type = MT_PHYSICS;

			if (mass > 0 && drag > 0)
			{
				obj->mtype.phys_info.mass = mass;
				obj->mtype.phys_info.drag = drag;
			}

			if (size > 0)
				obj->size = size;

			obj->mtype.phys_info.velocity = *velocity;

			if (phys_flags)
				obj->mtype.phys_info.flags = phys_flags;

			obj->ctype.laser_info.casts_light = false;
			if (VisEffectIsLocalPlayerAttachedSourceObject(parent))
				VisEffectMarkCloseScreenWeaponObject(obj);

		}

		return objnum;
	}

	visnum = VisEffectCreate(type, id, roomnum, pos);

	if (visnum < 0)
		return -1;		// No vis effects free

	vis = &VisEffects[visnum];

	vis->lifeleft = lifetime;
	vis->lifetime = lifetime;
	vis->movement_type = MT_PHYSICS;
	vis->mass = mass;
	vis->drag = drag;

	if (size > 0)
		vis->size = size;

	vis->velocity = *velocity;

	//float mag=vm_GetMagnitudeFast (&vis->velocity);
	//mprintf ((0,"CREATION:Velocity mag is %f\n",mag));

	if (phys_flags)
		vis->phys_flags = phys_flags;

	if (ROOMNUM_OUTSIDE(roomnum))
		vis->phys_flags |= PF_NO_COLLIDE;

	vis->flags |= VF_NO_Z_ADJUST;
	if (VisEffectIsCloseScreenSourceObject(parent) || VisEffectIsLocalPlayerAttachedSourceObject(parent))
		vis->flags |= VF_CLOSE_SCREEN_EFFECT;
	vis->lighting_color &= ~(0x8000);


	return visnum;
}


//link the viseffect  into the list for its room
// Does nothing for effects over terrain
void VisEffectLink(int visnum, int roomnum)
{
	vis_effect* vis = &VisEffects[visnum];

	ASSERT(visnum != -1);

	if (!ROOMNUM_OUTSIDE(vis->roomnum))
		ASSERT(roomnum >= 0 && roomnum <= Highest_room_index);

	vis->roomnum = roomnum;

	if (ROOMNUM_OUTSIDE(vis->roomnum))
		return;

	vis->next = Rooms[roomnum].vis_effects;
	Rooms[roomnum].vis_effects = visnum;
	ASSERT(vis->next != visnum);

	vis->prev = -1;

	if (vis->next != -1)
		VisEffects[vis->next].prev = visnum;
}

// Unlinks a viseffect from a room
// Does nothing for terrain
void VisEffectUnlink(int visnum)
{
	vis_effect* vis = &VisEffects[visnum];

	ASSERT(visnum != -1);

	if (ROOMNUM_OUTSIDE(vis->roomnum))
		return;
	else
	{

		room* rp = &Rooms[vis->roomnum];

		if (vis->prev == -1)
			rp->vis_effects = vis->next;
		else
			VisEffects[vis->prev].next = vis->next;

		if (vis->next != -1)
			VisEffects[vis->next].prev = vis->prev;

		vis->roomnum = -1;
	}
}

//when an effect has moved into a new room, this function unlinks it
//from its old room and links it into the new room
void VisEffectRelink(int visnum, int newroomnum)
{
	ASSERT(visnum >= 0 && visnum < max_vis_effects);

	VisEffectUnlink(visnum);
	VisEffectLink(visnum, newroomnum);
}



// Kills all the effects that are dead
void VisEffectDeleteDead()
{
	int i;
	for (i = 0; i < NumVisDead; i++)
	{
		VisEffectDelete(VisDeadList[i]);
	}

	NumVisDead = 0;
}


//remove viseffect from the world
void VisEffectDelete(int visnum)
{
	vis_effect* vis = &VisEffects[visnum];

	ASSERT(visnum != -1);
	ASSERT(vis->type != VIS_NONE);

	VisEffectUnlink(visnum);

	vis->type = VIS_NONE;		//unused!
	vis->roomnum = -1;				// zero it!

	VisEffectFree(visnum);
}

// Frees all the objects that are currently in use
void FreeAllVisEffects()
{
	Close_screen_weapon_object_handles.clear();

	for (int i = 0; i < max_vis_effects; i++)
		if (VisEffects[i].type != VIS_NONE)
			VisEffectDelete(i);
}

// Creates a some sparks that go in random directions
void CreateRandomLineSparks(int num_sparks, vector* pos, int roomnum, ushort color, float force_scalar)
{
	// Make more sparks if Katmai
	if (Katmai)
		num_sparks *= 2;

	// Create some sparks
	for (int i = 0; i < num_sparks; i++)
	{
		int visnum = VisEffectCreate(VIS_FIREBALL, FADING_LINE_INDEX, roomnum, pos);
		if (visnum >= 0)
		{
			vis_effect* vis = &VisEffects[visnum];

			vis->movement_type = MT_PHYSICS;
			vis->mass = 500;
			vis->drag = .001f;
			vis->phys_flags |= PF_GRAVITY | PF_NO_COLLIDE;

			vis->velocity.x = (ps_rand() % 100) - 50;
			vis->velocity.y = (ps_rand() % 100);
			vis->velocity.z = (ps_rand() % 100) - 50;

			vm_NormalizeVectorFast(&vis->velocity);

			vis->velocity *= 20 + (ps_rand() % 10);
			vis->velocity *= force_scalar;
			vis->size = .7 + ((ps_rand() % 10) * .04);
			vis->flags |= VF_USES_LIFELEFT;
			float lifetime = 1 + ((ps_rand() % 10) * .15);
			vis->lifeleft = lifetime;
			vis->lifetime = lifetime;

			if (color == 0)
				vis->lighting_color = GR_RGB16(200 + (ps_rand() % 50), 150 + (ps_rand() % 50), ps_rand() % 50);
			else
				vis->lighting_color = color;
		}
	}
}


// Creates a some sparks that go in random directions
void CreateRandomSparks(int num_sparks, vector* pos, int roomnum, int which_index, float force_scalar)
{
	// Make more sparks if Katmai
	if (Katmai)
		num_sparks *= 2;


	// Create some sparks
	for (int i = 0; i < num_sparks; i++)
	{
		int sparknum;
		int index;

		if (ps_rand() % 2)
			index = HOT_SPARK_INDEX;
		else
			index = COOL_SPARK_INDEX;

		if (which_index != -1)
			index = which_index;

		sparknum = VisEffectCreate(VIS_FIREBALL, index, roomnum, pos);

		if (sparknum >= 0)
		{
			vis_effect* vis = &VisEffects[sparknum];

			vis->movement_type = MT_PHYSICS;
			vis->mass = 100;
			vis->drag = .1f;

			vis->phys_flags |= PF_GRAVITY | PF_NO_COLLIDE;

			vis->velocity.x = (ps_rand() % 100) - 50;
			vis->velocity.y = (ps_rand() % 100);
			vis->velocity.z = (ps_rand() % 100) - 50;

			vm_NormalizeVectorFast(&vis->velocity);
			vis->velocity *= 10 + (ps_rand() % 10);
			vis->velocity *= force_scalar;
			vis->size = .2 + ((ps_rand() % 10) * .01);
			vis->flags |= VF_USES_LIFELEFT;
			float lifetime = 1 + ((ps_rand() % 10) * .15);
			vis->lifeleft = lifetime;
			vis->lifetime = lifetime;
		}
	}
}

// Creates a some particles that go in random directions
void CreateRandomParticles(int num_sparks, vector* pos, int roomnum, int bm_handle,
	float size, float life, bool close_screen_effect, float age)
{
	// Create some sparks
	float tenth_life = life / 10.0;
	float tenth_size = size / 10.0;

	for (int i = 0; i < num_sparks; i++)
	{
		int sparknum;

		sparknum = VisEffectCreate(VIS_FIREBALL, PARTICLE_INDEX, roomnum, pos);

		if (sparknum >= 0)
		{
			vis_effect* vis = &VisEffects[sparknum];


			vis->movement_type = MT_PHYSICS;
			vis->mass = 100;
			vis->drag = .1f;

			vis->phys_flags |= PF_GRAVITY | PF_NO_COLLIDE;

			vis->velocity.x = (ps_rand() % 100) - 50;
			vis->velocity.y = (ps_rand() % 100);
			vis->velocity.z = (ps_rand() % 100) - 50;

			vm_NormalizeVectorFast(&vis->velocity);
			vis->velocity *= 10 + (ps_rand() % 10);
			vis->pos += vis->velocity * age;
			vis->size = size + (((ps_rand() % 11) - 5) * tenth_size);
			vis->flags |= VF_USES_LIFELEFT;
			float lifetime = life + (((ps_rand() % 11) - 5) * tenth_life);
			vis->lifeleft = lifetime - age;
			vis->lifetime = lifetime;
			vis->creation_time -= age;
			vis->custom_handle = bm_handle;
			if (close_screen_effect)
				vis->flags |= VF_CLOSE_SCREEN_EFFECT;
		}
	}
}




// Draws a weapons fading line
void DrawVisFadingLine(vis_effect* vis)
{
	float norm_time;
	float time_live = Gametime - vis->creation_time;
	float size = vis->size;

	int visnum = vis - VisEffects;
	norm_time = time_live / vis->lifetime;

	if (norm_time >= 1)
		norm_time = .99999f;		// don't go over!

	rend_SetAlphaType(AT_SATURATE_VERTEX);
	rend_SetTextureType(TT_FLAT);
	rend_SetLighting(LS_GOURAUD);
	rend_SetColorModel(CM_RGB);
	rend_SetOverlayType(OT_NONE);

	vector vecs[2];
	g3Point pnts[2];
	int i;

	vecs[0] = vis->pos;
	vecs[1] = vis->end_pos;

	if (!(vis->flags & VF_WINDSHIELD_EFFECT))	// bash end position
	{
		vector vel = -vis->velocity;
		vm_NormalizeVectorFast(&vel);
		vecs[1] = vis->pos + (vel * vis->size);
	}

	ddgr_color color = GR_16_TO_COLOR(vis->lighting_color);
	int r = GR_COLOR_RED(color);
	int g = GR_COLOR_GREEN(color);
	int b = GR_COLOR_BLUE(color);

	for (i = 0; i < 2; i++)
	{
		g3_RotatePoint(&pnts[i], &vecs[i]);
		pnts[i].p3_flags |= PF_RGBA;
		pnts[i].p3_r = (r / 255.0);
		pnts[i].p3_g = (g / 255.0);;
		pnts[i].p3_b = (b / 255.0);;
	}

	if (vis->flags & VF_WINDSHIELD_EFFECT)
		pnts[0].p3_a = .3f;
	else
		pnts[0].p3_a = 1.0 - norm_time;

	pnts[1].p3_a = 0.0;

	rend_SetZBufferWriteMask(0);
	rend_SetAOSuppression(1.0f);
	g3_DrawSpecialLine(&pnts[0], &pnts[1]);
	rend_SetAOSuppression(0.0f);
	rend_SetZBufferWriteMask(1);

}

static bool VisEffectShouldUseSoftParticles();
static ubyte VisEffectClampAlpha(float value);

// Draws a blast ring vis effect
void DrawVisBlastRing(vis_effect* vis)
{
	vector inner_vecs[30], outer_vecs[30];
	g3Point inner_points[30], outer_points[30];
	float lifenorm = (vis->lifetime - vis->lifeleft) / vis->lifetime;
	float cur_size = lifenorm * vis->size;
	int i;
	g3Point* pntlist[4];
	matrix orient;
	vector fvec;

	if (vis->flags & VF_REVERSE)
	{
		lifenorm = 1 - lifenorm;
		cur_size = lifenorm * vis->size;
	}

	if (vis->flags & VF_PLANAR)
		fvec = vis->end_pos;
	else
	{
		fvec = Viewer_object->pos - vis->pos;
		vm_NormalizeVectorFast(&fvec);
	}

	if (vm_GetMagnitudeFast(&fvec) < .5)
		return;
	vm_VectorToMatrix(&orient, &fvec, NULL, NULL);

	int num_segments = 20;

	int ring_increment = 65536 / num_segments;
	int ring_angle = 0;

	if (lifenorm > 1.0)
		lifenorm = 1.0;

	rend_SetAlphaType(AT_SATURATE_TEXTURE_VERTEX);
	rend_SetOverlayType(OT_NONE);
	rend_SetTextureType(TT_LINEAR);
	rend_SetLighting(LS_NONE);
	rend_SetZBias(-1.0);
	rend_SetZBufferWriteMask(0);
	rend_SetAOSuppression(1.0f);
	rend_SetSoftParticleState(VisEffectShouldUseSoftParticles() ? 1 : 0);

	ring_angle = 0;

	for (i = 0; i < num_segments; i++, ring_angle += ring_increment)
	{
		float ring_sin = FixSin(ring_angle);
		float ring_cos = FixCos(ring_angle);

		inner_vecs[i] = orient.rvec * (ring_cos * (cur_size / 2));
		inner_vecs[i] += orient.uvec * (ring_sin * (cur_size / 2));
		inner_vecs[i] += vis->pos;

		outer_vecs[i] = orient.rvec * (ring_cos * cur_size);
		outer_vecs[i] += orient.uvec * (ring_sin * cur_size);
		outer_vecs[i] += vis->pos;

		g3_RotatePoint(&inner_points[i], &inner_vecs[i]);
		g3_RotatePoint(&outer_points[i], &outer_vecs[i]);
		outer_points[i].p3_flags |= PF_UV | PF_RGBA;
		inner_points[i].p3_flags |= PF_UV | PF_RGBA;

		outer_points[i].p3_a = (1.0 - lifenorm);
		inner_points[i].p3_a = 0;
		inner_points[i].p3_l = 1;
		inner_points[i].p3_l = 1;
	}

	for (i = 0; i < num_segments; i++)
	{
		int next = (i + 1) % num_segments;

		outer_points[i].p3_u = 0 + lifenorm;
		outer_points[next].p3_u = 1.0 + lifenorm;
		outer_points[i].p3_v = 0;
		outer_points[next].p3_v = 0;

		inner_points[i].p3_u = 0 + lifenorm;
		inner_points[next].p3_u = 1.0 + lifenorm;
		inner_points[i].p3_v = 1;
		inner_points[next].p3_v = 1;

		pntlist[0] = &outer_points[i];
		pntlist[1] = &outer_points[next];
		pntlist[2] = &inner_points[next];
		pntlist[3] = &inner_points[i];

		g3_DrawPoly(4, pntlist, vis->custom_handle);
	}

	rend_SetSoftParticleState(0);
	rend_SetAOSuppression(0.0f);
	rend_SetZBufferWriteMask(1);
	rend_SetZBias(0);
}

// Draws a raindrop on the players windshield
void DrawVisRainDrop(vis_effect* vis)
{
	float norm_time;
	float time_live = Gametime - vis->creation_time;
	float size = vis->size;

	int visnum = vis - VisEffects;
	int bm_handle;
	fireball* fb = &Fireballs[vis->id];

	norm_time = time_live / vis->lifetime;

	if (norm_time >= 1)
		norm_time = .99999f;		// don't go over!

	size *= (1 - (norm_time / 2));

	bm_handle = fb->bm_handle;

	float val;
	if (norm_time > .5)
		val = 1.0 - ((norm_time - .5) / .5);
	else if (norm_time < .1)
		val = norm_time / .1;
	else
		val = 1.0;


	rend_SetAlphaValue(val * .4 * 255);
	rend_SetOverlayType(OT_NONE);
	rend_SetWrapType(WT_CLAMP);
	rend_SetLighting(LS_NONE);

	// Set position
	vector pos;

	if (vis->id == RAINDROP_INDEX)
	{
		rend_SetZBufferState(0);
		rend_SetAlphaType(AT_SATURATE_TEXTURE);
		rend_SetAlphaValue(val * .4 * 255);

		pos = Viewer_object->pos;
		pos += Viewer_object->orient.rvec * vis->pos.x;
		pos += Viewer_object->orient.uvec * vis->pos.y;
		pos += Viewer_object->orient.fvec * vis->pos.z;
		g3_DrawRotatedBitmap(&pos, 0, size, (size * bm_h(bm_handle, 0)) / bm_w(bm_handle, 0), bm_handle);
		rend_SetZBufferState(1);
	}
	else
	{
		rend_SetAlphaType(AT_SATURATE_TEXTURE);
		rend_SetAlphaValue(val * .2 * 255);
		rend_SetZBufferWriteMask(0);
		pos = vis->pos;
		ASSERT(!((vis->end_pos.x == 0.0) && (vis->end_pos.y == 0.0) && (vis->end_pos.z == 0.0)));
		g3_DrawPlanarRotatedBitmap(&pos, &vis->end_pos, 0, size, (size * bm_h(bm_handle, 0)) / bm_w(bm_handle, 0), bm_handle);
		rend_SetZBufferWriteMask(1);
	}

	rend_SetWrapType(WT_WRAP);

}


// Draws a snowflake
void DrawVisSnowflake(vis_effect* vis)
{
	float norm_time;
	float time_live = Gametime - vis->creation_time;
	float size = vis->size;
	const bool enhanced = (vis->flags & VF_ENHANCED_SNOW) != 0;

	int visnum = vis - VisEffects;
	int bm_handle;
	fireball* fb = &Fireballs[vis->id];

	norm_time = time_live / vis->lifetime;

	if (norm_time >= 1)
		norm_time = .99999f;		// don't go over!

	if (enhanced && VisEffectCreateEnhancedSnowTextures())
	{
		const EnhancedSnowRenderParams params =
			VisEffectEnhancedSnowRenderParams(vis, time_live, norm_time);
		rend_SetAlphaValue(VisEffectClampAlpha(params.alpha * 255.0f));
		rend_SetOverlayType(OT_NONE);
		rend_SetWrapType(WT_CLAMP);
		rend_SetLighting(LS_NONE);
		rend_SetAlphaType(AT_SATURATE_TEXTURE);
		rend_SetZBias(0.0f);
		rend_SetZBufferWriteMask(0);
		rend_SetAOSuppression(1.0f);
		rend_SetSoftParticleState(0);
		const ddgr_color color = GR_16_TO_COLOR(vis->lighting_color);
		if (vis->custom_handle & 4)
			g3_DrawPlanarRotatedBitmap(&vis->pos, &vis->velocity, 0, params.width,
				params.height, Enhanced_snow_fallback_handle);
		else
			g3_DrawRotatedBitmap(&vis->pos, params.rotation, params.width, params.height,
				Enhanced_snow_fallback_handle, color);
		rend_SetSoftParticleState(0);
		rend_SetAOSuppression(0.0f);
		rend_SetZBufferWriteMask(1);
		rend_SetZBias(0.0f);
		rend_SetWrapType(WT_WRAP);
		return;
	}

	//size*=(1-(norm_time/2));

	bm_handle = fb->bm_handle;

	float val;

	val = (1.0 - (norm_time)) * VisEffectSnowFadeInFactor(time_live);

	rend_SetAlphaValue(val * .6 * 255);
	rend_SetOverlayType(OT_NONE);
	//rend_SetWrapType (WT_CLAMP);
	rend_SetLighting(LS_NONE);
	//rend_SetZBufferState (0);
	rend_SetAlphaType(AT_SATURATE_TEXTURE);

	ddgr_color color = GR_16_TO_COLOR(vis->lighting_color);
	rend_SetSoftParticleState(VisEffectShouldUseSoftParticles() ? 1 : 0);
	g3_DrawBitmap(&vis->pos, size, (size * bm_h(bm_handle, 0)) / bm_w(bm_handle, 0), bm_handle, color);
	rend_SetSoftParticleState(0);

	//rend_SetZBufferState (1);
	//rend_SetWrapType (WT_WRAP);

}

// Draws a lighting bolt from one area to another
// Velocity.x is how much randomness goes into drawing
// Velocity.y is the scalar that effects how many segments to draw
void DrawVisLightningBolt(vis_effect* vis)
{
	vector line_norm;
	float line_mag;
	int num_segs;
	float lightning_mag;

	g3Point pnt1, pnt2;

	line_norm = vis->pos - vis->end_pos;
	line_mag = vm_GetMagnitudeFast(&line_norm);

	line_norm /= line_mag;

	if (line_mag < 1)
		return;

	float alpha_norm;

	if (vis->flags & VF_EXPAND)
	{
		num_segs = line_mag * vis->velocity.y;
		lightning_mag = vis->velocity.x;

		alpha_norm = vis->lifeleft / vis->lifetime;
	}
	else
	{
		num_segs = line_mag * vis->velocity.y;
		lightning_mag = vis->velocity.x;

		alpha_norm = .7f;

		// Make it powerup up based on distance
		if (line_mag > 30 && (vis->flags & VF_NO_Z_ADJUST))
		{
			float scalar = 1.0 - ((line_mag - 30) / 150.0);
			if (scalar < 0)
				return;

			alpha_norm *= scalar;
		}
	}

	if (num_segs < 2)
		return;

	// Set some states

	rend_SetTextureType(TT_FLAT);
	rend_SetAlphaType(AT_SATURATE_VERTEX);
	rend_SetLighting(LS_NONE);
	// Lightning is a surface line effect, never a soft billboard.
	rend_SetSoftParticleState(0);
	// Attached bolts start exactly on model vertices.  The unified opaque
	// prepass makes their parent's depth complete before this transparent draw,
	// so use a small surface-overlay bias to avoid self-z-fighting while keeping
	// the ordinary depth test against walls and other objects.
	const bool attached_surface_overlay = (vis->flags & VF_ATTACHED) != 0;
	if (attached_surface_overlay)
		rend_SetZBias(-0.1f);
	rend_SetZBufferWriteMask(0);
	rend_SetAOSuppression(1.0f);

	if (vis->id == GRAY_LIGHTNING_BOLT_INDEX)
	{
		// get the color from the struct
		rend_SetFlatColor(GR_16_TO_COLOR(vis->lighting_color));
	}
	else
	{
		rend_SetFlatColor(GR_RGB(10, 60, 200));
	}

	pnt1.p3_a = alpha_norm;
	pnt2.p3_a = alpha_norm;

	vector vecs[50];

	num_segs = std::min(num_segs, 50);

	CreateLightningRodPositions(&vis->pos, &vis->end_pos, vecs, num_segs, lightning_mag, false);

	for (int i = 0; i < num_segs - 1; i++)
	{
		g3_RotatePoint(&pnt1, &vecs[i]);
		g3_RotatePoint(&pnt2, &vecs[i + 1]);

		pnt1.p3_flags |= PF_RGBA;
		pnt2.p3_flags |= PF_RGBA;

		g3_DrawSpecialLine(&pnt1, &pnt2);
	}

	rend_SetAOSuppression(0.0f);
	rend_SetZBufferWriteMask(1);
	if (attached_surface_overlay)
		rend_SetZBias(0.0f);
}

// Draws a lighting bolt sine wave from one area to another
// Velocity.x represents how many increments to take (a scalar)
// Velocity.y represents how "wide" the arcs are from the center of the line
void DrawVisSineWave(vis_effect* vis)
{
	vector line_norm;
	float line_mag;
	matrix mat;
	int num_segs;

	vector from, to, base_from, rvec, uvec;
	g3Point pnt1, pnt2;

	line_norm = vis->pos - vis->end_pos;
	line_mag = vm_GetMagnitudeFast(&line_norm);
	if (line_mag < .1)
		return;

	line_norm /= line_mag;


	float alpha_norm;

	if (vis->flags & VF_EXPAND)
		alpha_norm = vis->lifeleft / vis->lifetime;

	num_segs = vis->velocity.x * line_mag; // /2
	line_norm /= vis->velocity.x;  // *2

	alpha_norm = vis->lifeleft / vis->lifetime;

	vm_VectorToMatrix(&mat, &line_norm, &vis->velocity, NULL);
	rvec = mat.rvec * vis->velocity.y; // /4
	uvec = mat.uvec * vis->velocity.y; // /4

	// Set some states

	rend_SetTextureType(TT_FLAT);
	rend_SetAlphaType(AT_SATURATE_VERTEX);
	rend_SetLighting(LS_NONE);
	rend_SetZBufferWriteMask(0);
	rend_SetFlatColor(GR_RGB(10, 60, 200));
	rend_SetAOSuppression(1.0f);
	int cur_sin = (vis - VisEffects) * 5000;

	cur_sin = Get60HzVisualAngle(2000.0f, cur_sin);

	base_from = vis->end_pos;
	from = base_from;

	pnt1.p3_a = alpha_norm;
	pnt2.p3_a = alpha_norm;

	for (int i = 0; i < num_segs; i++, base_from += line_norm)
	{
		to = base_from + line_norm + (FixSin((cur_sin) % 65536) * uvec);
		to += (FixCos((cur_sin) % 65536) * rvec);

		g3_RotatePoint(&pnt1, &from);
		g3_RotatePoint(&pnt2, &to);

		pnt1.p3_flags |= PF_RGBA;
		pnt2.p3_flags |= PF_RGBA;

		g3_DrawSpecialLine(&pnt1, &pnt2);

		from = to;

		cur_sin += 4000;
	}

	rend_SetAOSuppression(0.0f);
	rend_SetZBufferWriteMask(1);
}

// Calculates the corners for a billboard
// Returns 0 if off screen, or 1 if we should draw
int GetBillboardCorners(g3Point* pnts, g3Point* top_point, g3Point* bot_point, float width)
{
	// get the camera's world position
	vector viewerPos;
	g3_GetViewPosition(&viewerPos);

	// calculate the vector from the top point to the bottom point
	ASSERT(bot_point->p3_flags & PF_ORIGPOINT);
	ASSERT(top_point->p3_flags & PF_ORIGPOINT);
	vector deltaVec = bot_point->p3_vecPreRot - top_point->p3_vecPreRot;
	vm_NormalizeVector(&deltaVec);

	// get the vector from the camera to the top point
	vector top = top_point->p3_vecPreRot - viewerPos;
	vm_NormalizeVector(&top);

	// calculate the vector out from the 'rod' that is facing the camera
	vector rodNorm;
	vm_CrossProduct(&rodNorm, &deltaVec, &top);
	vm_NormalizeVector(&rodNorm);

	// get the offset vector
	vector tempv = rodNorm * width;

	// setup the points
	int i, codesAND = 0xFF;
	for (i = 0; i < 4; ++i)
	{
		float scale = (i == 0 || i == 3) ? -1.0f : 1.0f;
		vector bbPt = ((i < 2) ? top_point->p3_vecPreRot : bot_point->p3_vecPreRot) + (tempv * scale);

		// initialize the point
		codesAND &= g3_RotatePoint(&pnts[i], &bbPt);
	}

	if (codesAND)
		return 0;

	return 1;
}

struct VisFireballBatchKey
{
	int bitmap_handle;
	sbyte alpha_type;
	bool soft_particles;
	int roomnum;

	bool Equals(const VisFireballBatchKey& other) const
	{
		return bitmap_handle == other.bitmap_handle &&
			alpha_type == other.alpha_type &&
			soft_particles == other.soft_particles &&
			roomnum == other.roomnum;
	}
};

static const int VIS_FIREBALL_BATCH_MAX_VERTS = 16;

struct VisFireballBatchItem
{
	g3Point points[VIS_FIREBALL_BATCH_MAX_VERTS];
	g3Point* pointlist[VIS_FIREBALL_BATCH_MAX_VERTS];
	int nv;

	void RefreshPointList()
	{
		for (int i = 0; i < nv; i++)
			pointlist[i] = &points[i];
	}
};

struct VisWeatherBatchKey
{
	int bitmap_handle;
	sbyte alpha_type;
	bool soft_particles;
	bool zbuffer_state;
	int roomnum;

	bool Equals(const VisWeatherBatchKey& other) const
	{
		return bitmap_handle == other.bitmap_handle &&
			alpha_type == other.alpha_type &&
			soft_particles == other.soft_particles &&
			zbuffer_state == other.zbuffer_state &&
			roomnum == other.roomnum;
	}
};

struct VisWeatherLineBatchKey
{
	bool soft_particles;
	int roomnum;

	bool Equals(const VisWeatherLineBatchKey& other) const
	{
		return soft_particles == other.soft_particles && roomnum == other.roomnum;
	}
};

struct VisWeatherLineBatchItem
{
	g3Point points[2];
	renderer_line_batch_item item;

	void RefreshItem()
	{
		item.p0 = &points[0];
		item.p1 = &points[1];
	}
};

struct VisSmokeTrailBatchKey
{
	int bitmap_handle;
	sbyte alpha_type;
	bool soft_particles;
	int roomnum;

	bool Equals(const VisSmokeTrailBatchKey& other) const
	{
		return bitmap_handle == other.bitmap_handle &&
			alpha_type == other.alpha_type &&
			soft_particles == other.soft_particles &&
			roomnum == other.roomnum;
	}
};

struct VisMassDriverBatchKey
{
	int bitmap_handle;
	sbyte alpha_type;
	bool soft_particles;
	int roomnum;

	bool Equals(const VisMassDriverBatchKey& other) const
	{
		return bitmap_handle == other.bitmap_handle &&
			alpha_type == other.alpha_type &&
			soft_particles == other.soft_particles &&
			roomnum == other.roomnum;
	}
};

struct VisMassDriverBatchPart
{
	VisMassDriverBatchKey key;
	VisFireballBatchItem item;
};

struct VisFireballAtlasFrame
{
	int source_handle;
	int atlas_handle;
	int width;
	int height;
	int bitmap_format;
	char source_name[BITMAP_NAME_LEN];
	float u0;
	float v0;
	float u1;
	float v1;
};

struct VisFireballSharedAtlas
{
	int atlas_handle;
	int cell_width;
	int cell_height;
	int bitmap_format;
	int columns;
	int rows;
	int next_slot;
	bool valid;
	bool failed;
	std::vector<VisFireballAtlasFrame> frames;
};

static bool VisFireball_batch_valid = false;
static VisFireballBatchKey VisFireball_batch_key = {};
static std::vector<VisFireballBatchItem> VisFireball_batch_items;
static std::vector<VisFireballSharedAtlas> VisFireball_shared_atlases;
static bool VisWeather_quad_batch_valid = false;
static VisWeatherBatchKey VisWeather_quad_batch_key = {};
static std::vector<VisFireballBatchItem> VisWeather_quad_batch_items;
static bool VisWeather_line_batch_valid = false;
static VisWeatherLineBatchKey VisWeather_line_batch_key = {};
static std::vector<VisWeatherLineBatchItem> VisWeather_line_batch_items;
static bool VisSmokeTrail_batch_valid = false;
static VisSmokeTrailBatchKey VisSmokeTrail_batch_key = {};
static std::vector<VisFireballBatchItem> VisSmokeTrail_batch_items;
static bool VisMassDriver_batch_valid = false;
static VisMassDriverBatchKey VisMassDriver_batch_key = {};
static std::vector<VisFireballBatchItem> VisMassDriver_batch_items;
static const bool VIS_FIREBALL_BARRIER_FLUSHES_ENABLED = false;
static const int VIS_FIREBALL_ATLAS_PADDING = 1;
static const int VIS_FIREBALL_ATLAS_MAX_DIMENSION = 2048;

static bool VisEffectHasQueuedBatch()
{
	return VisFireball_batch_valid && !VisFireball_batch_items.empty();
}

static bool VisEffectHasQueuedWeatherQuadBatch()
{
	return VisWeather_quad_batch_valid && !VisWeather_quad_batch_items.empty();
}

static bool VisEffectHasQueuedWeatherLineBatch()
{
	return VisWeather_line_batch_valid && !VisWeather_line_batch_items.empty();
}

static bool VisEffectHasQueuedSmokeTrailBatch()
{
	return VisSmokeTrail_batch_valid && !VisSmokeTrail_batch_items.empty();
}

static bool VisEffectHasQueuedMassDriverBatch()
{
	return VisMassDriver_batch_valid && !VisMassDriver_batch_items.empty();
}

static bool VisEffectShouldUseSoftParticles()
{
	if (Close_screen_rendering_late)
		return false;

	return Render_soft_vis_effects && rend_CanUseNewrender();
}

static bool VisEffectShouldUseSoftParticlesForEffect(const vis_effect* vis)
{
	if (vis && (vis->flags & VF_NO_SOFT_PARTICLES))
		return false;

	return VisEffectShouldUseSoftParticles();
}

static float VisEffectBillboardDepthBias(const vis_effect* vis, const fireball* fb,
	float size, bool soft_particles)
{
	if (vis && (vis->flags & VF_NO_Z_ADJUST))
		return 0.0f;

	// A radius-sized forward bias defeats the depth-distance fade. Smoke is a
	// genuine volumetric billboard, so use its authored depth when soft
	// intersections are enabled. Other effect types retain legacy ordering.
	if (soft_particles && fb && fb->type == FT_SMOKE)
		return 0.0f;

	return -size;
}

static bool VisEffectShouldUseSoftSnowParticles(bool zbuffer_state)
{
	if (Close_screen_rendering_late)
		return false;

	return Render_soft_vis_effects && zbuffer_state && rend_CanUseNewrender();
}

struct VisEffectVClipFrameBlend
{
	int frame0;
	int frame1;
	float frame1_weight;
	bool has_frame1;
};

static VisEffectVClipFrameBlend VisEffectCalcVClipFrameBlend(const vclip* vc, float norm_time, bool loop)
{
	VisEffectVClipFrameBlend blend = {};
	blend.frame0 = 0;
	blend.frame1 = 0;
	blend.frame1_weight = 0.0f;
	blend.has_frame1 = false;

	if (!vc || vc->num_frames <= 0)
		return blend;

	float frame_pos = vc->num_frames * norm_time;
	if (frame_pos < 0.0f)
		frame_pos = 0.0f;
	if (frame_pos >= vc->num_frames)
		frame_pos = vc->num_frames - 0.00001f;

	blend.frame0 = (int)frame_pos;
	if (blend.frame0 < 0)
		blend.frame0 = 0;
	if (blend.frame0 >= vc->num_frames)
		blend.frame0 = vc->num_frames - 1;

	if (vc->num_frames <= 1)
		return blend;

	blend.frame1_weight = frame_pos - blend.frame0;
	if (blend.frame1_weight <= 0.001f)
		return blend;

	blend.frame1 = blend.frame0 + 1;
	if (blend.frame1 >= vc->num_frames)
	{
		if (!loop)
			return blend;
		blend.frame1 = 0;
	}

	blend.has_frame1 = true;
	return blend;
}

static int VisEffectNextPowerOfTwo(int value)
{
	int power = 1;
	while (power < value)
		power <<= 1;
	return power;
}

static bool VisEffectAtlasFrameSourceMatches(const VisFireballAtlasFrame& frame, int source_handle)
{
	if (source_handle <= BAD_BITMAP_HANDLE || !GameBitmaps[source_handle].used)
		return false;

	if (frame.source_handle != source_handle ||
		frame.width != bm_w(source_handle, 0) ||
		frame.height != bm_h(source_handle, 0) ||
		frame.bitmap_format != GameBitmaps[source_handle].format)
		return false;

	if (strncmp(frame.source_name, GameBitmaps[source_handle].name, BITMAP_NAME_LEN) != 0)
		return false;

	return true;
}

static VisFireballAtlasFrame* VisEffectFindSharedAtlasFrame(int source_handle)
{
	for (size_t atlas_index = 0; atlas_index < VisFireball_shared_atlases.size(); atlas_index++)
	{
		VisFireballSharedAtlas& atlas = VisFireball_shared_atlases[atlas_index];
		for (size_t frame_index = 0; frame_index < atlas.frames.size(); frame_index++)
		{
			if (VisEffectAtlasFrameSourceMatches(atlas.frames[frame_index], source_handle))
				return &atlas.frames[frame_index];
		}
	}

	return NULL;
}

static void VisEffectCopyAtlasFrame(int atlas_handle, int dest_x, int dest_y, int source_handle)
{
	ushort* dest = bm_data(atlas_handle, 0);
	ushort* src = bm_data(source_handle, 0);
	const int atlas_width = bm_w(atlas_handle, 0);
	const int source_width = bm_w(source_handle, 0);
	const int source_height = bm_h(source_handle, 0);

	if (!dest || !src)
		return;

	const int image_x = dest_x + VIS_FIREBALL_ATLAS_PADDING;
	const int image_y = dest_y + VIS_FIREBALL_ATLAS_PADDING;

	for (int y = 0; y < source_height; y++)
	{
		ushort* dest_row = dest + ((image_y + y) * atlas_width) + image_x;
		ushort* src_row = src + (y * source_width);
		memcpy(dest_row, src_row, source_width * sizeof(ushort));

		dest_row[-1] = src_row[0];
		dest_row[source_width] = src_row[source_width - 1];
	}

	ushort* dest_top = dest + (dest_y * atlas_width) + image_x;
	ushort* dest_bottom = dest + ((image_y + source_height) * atlas_width) + image_x;
	ushort* src_top = src;
	ushort* src_bottom = src + ((source_height - 1) * source_width);
	memcpy(dest_top, src_top, source_width * sizeof(ushort));
	memcpy(dest_bottom, src_bottom, source_width * sizeof(ushort));

	dest_top[-1] = src_top[0];
	dest_top[source_width] = src_top[source_width - 1];
	dest_bottom[-1] = src_bottom[0];
	dest_bottom[source_width] = src_bottom[source_width - 1];
}

static bool VisEffectBuildSharedAtlas(int source_width, int source_height, int bitmap_format,
	VisFireballSharedAtlas& atlas)
{
	atlas.cell_width = source_width;
	atlas.cell_height = source_height;
	atlas.bitmap_format = bitmap_format;
	atlas.atlas_handle = BAD_BITMAP_HANDLE;
	atlas.columns = 0;
	atlas.rows = 0;
	atlas.next_slot = 0;
	atlas.valid = false;
	atlas.failed = true;
	atlas.frames.clear();

	if (source_width <= 0 || source_height <= 0)
		return false;

	const int padded_width = source_width + (VIS_FIREBALL_ATLAS_PADDING * 2);
	const int padded_height = source_height + (VIS_FIREBALL_ATLAS_PADDING * 2);
	if (padded_width > VIS_FIREBALL_ATLAS_MAX_DIMENSION ||
		padded_height > VIS_FIREBALL_ATLAS_MAX_DIMENSION)
		return false;

	const int target_width = std::min(VIS_FIREBALL_ATLAS_MAX_DIMENSION,
		VisEffectNextPowerOfTwo(padded_width * 16));
	const int target_height = std::min(VIS_FIREBALL_ATLAS_MAX_DIMENSION,
		VisEffectNextPowerOfTwo(padded_height * 16));
	const int atlas_width = std::max(padded_width, target_width);
	const int atlas_height = std::max(padded_height, target_height);

	const int atlas_handle = bm_AllocBitmap(atlas_width, atlas_height, 0);
	if (atlas_handle <= BAD_BITMAP_HANDLE)
		return false;

	GameBitmaps[atlas_handle].flags |= BF_TRANSPARENT | BF_CHANGED;
	GameBitmaps[atlas_handle].format = bitmap_format;
	snprintf(GameBitmaps[atlas_handle].name, BITMAP_NAME_LEN, "vfx%dx%d_%d",
		source_width, source_height, bitmap_format);

	ushort* atlas_data = bm_data(atlas_handle, 0);
	if (!atlas_data)
	{
		bm_FreeBitmap(atlas_handle);
		return false;
	}

	for (int i = 0; i < atlas_width * atlas_height; i++)
		atlas_data[i] = NEW_TRANSPARENT_COLOR;

	atlas.columns = atlas_width / padded_width;
	atlas.rows = atlas_height / padded_height;
	atlas.next_slot = 0;
	atlas.atlas_handle = atlas_handle;
	atlas.valid = true;
	atlas.failed = false;
	return true;
}

static VisFireballSharedAtlas* VisEffectFindSharedAtlasWithSpace(int source_width, int source_height,
	int bitmap_format)
{
	for (size_t i = 0; i < VisFireball_shared_atlases.size(); i++)
	{
		VisFireballSharedAtlas& atlas = VisFireball_shared_atlases[i];
		if (!atlas.valid || atlas.failed ||
			atlas.cell_width != source_width ||
			atlas.cell_height != source_height ||
			atlas.bitmap_format != bitmap_format)
			continue;

		if (atlas.next_slot < atlas.columns * atlas.rows)
			return &atlas;
	}

	VisFireballSharedAtlas new_atlas = {};
	if (!VisEffectBuildSharedAtlas(source_width, source_height, bitmap_format, new_atlas))
		return NULL;
	VisFireball_shared_atlases.push_back(new_atlas);
	VisFireballSharedAtlas& atlas = VisFireball_shared_atlases.back();
	return &atlas;
}

static bool VisEffectAddSharedAtlasFrame(int source_handle, int* bitmap_handle,
	int* width, int* height, float* u0, float* v0, float* u1, float* v1)
{
	if (source_handle <= BAD_BITMAP_HANDLE || !GameBitmaps[source_handle].used)
		return false;

	const int source_width = bm_w(source_handle, 0);
	const int source_height = bm_h(source_handle, 0);
	const int bitmap_format = GameBitmaps[source_handle].format;
	if (source_width <= 0 || source_height <= 0)
		return false;

	VisFireballSharedAtlas* atlas = VisEffectFindSharedAtlasWithSpace(source_width, source_height, bitmap_format);
	if (!atlas)
		return false;

	const int padded_width = source_width + (VIS_FIREBALL_ATLAS_PADDING * 2);
	const int padded_height = source_height + (VIS_FIREBALL_ATLAS_PADDING * 2);
	const int slot = atlas->next_slot++;
	const int cell_x = (slot % atlas->columns) * padded_width;
	const int cell_y = (slot / atlas->columns) * padded_height;
	VisEffectCopyAtlasFrame(atlas->atlas_handle, cell_x, cell_y, source_handle);
	GameBitmaps[atlas->atlas_handle].flags |= BF_CHANGED;

	VisFireballAtlasFrame frame = {};
	frame.source_handle = source_handle;
	frame.atlas_handle = atlas->atlas_handle;
	frame.width = source_width;
	frame.height = source_height;
	frame.bitmap_format = bitmap_format;
	strncpy(frame.source_name, GameBitmaps[source_handle].name, BITMAP_NAME_LEN - 1);
	frame.source_name[BITMAP_NAME_LEN - 1] = '\0';
	frame.u0 = (float)(cell_x + VIS_FIREBALL_ATLAS_PADDING) / (float)bm_w(atlas->atlas_handle, 0);
	frame.v0 = (float)(cell_y + VIS_FIREBALL_ATLAS_PADDING) / (float)bm_h(atlas->atlas_handle, 0);
	frame.u1 = (float)(cell_x + VIS_FIREBALL_ATLAS_PADDING + source_width) / (float)bm_w(atlas->atlas_handle, 0);
	frame.v1 = (float)(cell_y + VIS_FIREBALL_ATLAS_PADDING + source_height) / (float)bm_h(atlas->atlas_handle, 0);
	atlas->frames.push_back(frame);

	*bitmap_handle = atlas->atlas_handle;
	*width = source_width;
	*height = source_height;
	*u0 = frame.u0;
	*v0 = frame.v0;
	*u1 = frame.u1;
	*v1 = frame.v1;
	return true;
}

static bool VisEffectGetSharedAtlasFrame(int source_handle, int* bitmap_handle,
	int* width, int* height, float* u0, float* v0, float* u1, float* v1)
{
	VisFireballAtlasFrame* frame = VisEffectFindSharedAtlasFrame(source_handle);
	if (frame)
	{
		if (frame->atlas_handle > BAD_BITMAP_HANDLE && GameBitmaps[frame->atlas_handle].used)
		{
			*bitmap_handle = frame->atlas_handle;
			*width = frame->width;
			*height = frame->height;
			*u0 = frame->u0;
			*v0 = frame->v0;
			*u1 = frame->u1;
			*v1 = frame->v1;
			return true;
		}
	}

	return VisEffectAddSharedAtlasFrame(source_handle, bitmap_handle, width, height, u0, v0, u1, v1);
}

static void VisEffectApplyBatchUVs(VisFireballBatchItem& item, float u0, float v0, float u1, float v1)
{
	item.points[0].p3_u = u0;
	item.points[0].p3_v = v0;
	item.points[1].p3_u = u1;
	item.points[1].p3_v = v0;
	item.points[2].p3_u = u1;
	item.points[2].p3_v = v1;
	item.points[3].p3_u = u0;
	item.points[3].p3_v = v1;
}

static void VisEffectSelectBitmapForBatch(int source_handle, int* bitmap_handle,
	int* width, int* height, float* u0, float* v0, float* u1, float* v1)
{
	*bitmap_handle = BAD_BITMAP_HANDLE;
	*width = 0;
	*height = 0;
	*u0 = 0.0f;
	*v0 = 0.0f;
	*u1 = 1.0f;
	*v1 = 1.0f;

	if (source_handle <= BAD_BITMAP_HANDLE || !GameBitmaps[source_handle].used)
		return;

	if (VisEffectGetSharedAtlasFrame(source_handle, bitmap_handle, width, height, u0, v0, u1, v1))
		return;

	*bitmap_handle = source_handle;
	*width = bm_w(source_handle, 0);
	*height = bm_h(source_handle, 0);
}

static void VisEffectSelectVClipBitmap(int vclip_handle, int frame_index, int* bitmap_handle,
	int* width, int* height, float* u0, float* v0, float* u1, float* v1)
{
	*bitmap_handle = BAD_BITMAP_HANDLE;
	*width = 0;
	*height = 0;
	*u0 = 0.0f;
	*v0 = 0.0f;
	*u1 = 1.0f;
	*v1 = 1.0f;

	if (vclip_handle < 0 || vclip_handle >= MAX_VCLIPS)
		return;

	vclip* vc = &GameVClips[vclip_handle];
	if (!vc->used || !vc->frames || vc->num_frames <= 0)
		return;

	if (frame_index < 0)
		frame_index = 0;
	if (frame_index >= vc->num_frames)
		frame_index = vc->num_frames - 1;

	VisEffectSelectBitmapForBatch(vc->frames[frame_index], bitmap_handle, width, height, u0, v0, u1, v1);
}


static ubyte VisEffectClampAlpha(float value)
{
	if (value <= 0.0f)
		return 0;
	if (value >= 255.0f)
		return 255;
	return (ubyte)value;
}

static sbyte VisEffectBatchAlphaType(sbyte alpha_type, ubyte alpha_value, float* vertex_alpha)
{
	*vertex_alpha = 1.0f;

	if (alpha_type == AT_SATURATE_TEXTURE)
	{
		*vertex_alpha = alpha_value / 255.0f;
		return AT_SATURATE_TEXTURE_VERTEX;
	}

	if (alpha_type == AT_CONSTANT_TEXTURE)
	{
		*vertex_alpha = alpha_value / 255.0f;
		return AT_TEXTURE_VERTEX;
	}

	if (alpha_type == AT_LIGHTMAP_BLEND)
	{
		*vertex_alpha = alpha_value / 255.0f;
		return AT_LIGHTMAP_BLEND_VERTEX;
	}

	return alpha_type;
}

static bool VisEffectClipAndProjectBatchItem(VisFireballBatchItem& item, float z_bias, bool soft_particles)
{
	if (item.nv < 3 || item.nv > VIS_FIREBALL_BATCH_MAX_VERTS)
		return false;

	item.RefreshPointList();

	g3Codes clip_codes = {};
	clip_codes.cc_or = 0;
	clip_codes.cc_and = 0xff;
	for (int i = 0; i < item.nv; i++)
	{
		const ubyte code = item.points[i].p3_codes;
		clip_codes.cc_or |= code;
		clip_codes.cc_and &= code;
	}

	if (clip_codes.cc_and)
		return false;

	if (clip_codes.cc_or)
	{
		int clipped_nv = item.nv;
		g3Point** clipped_points = g3_ClipPolygon(item.pointlist, &clipped_nv, &clip_codes);
		const bool clipped_valid = clipped_nv >= 3 &&
			clipped_nv <= VIS_FIREBALL_BATCH_MAX_VERTS &&
			!(clip_codes.cc_or & CC_BEHIND) &&
			!clip_codes.cc_and;

		g3Point copied_points[VIS_FIREBALL_BATCH_MAX_VERTS];
		if (clipped_valid)
		{
			for (int i = 0; i < clipped_nv; i++)
			{
				copied_points[i] = *clipped_points[i];
				copied_points[i].p3_flags &= ~PF_TEMP_POINT;
			}
		}

		g3_FreeTempPoints(clipped_points, clipped_nv);

		if (!clipped_valid)
			return false;

		item.nv = clipped_nv;
		for (int i = 0; i < item.nv; i++)
			item.points[i] = copied_points[i];
	}

	for (int i = 0; i < item.nv; i++)
	{
		if (item.points[i].p3_codes & CC_BEHIND)
			return false;
		if (!(item.points[i].p3_flags & PF_PROJECTED))
			g3_ProjectPoint(&item.points[i]);
		if (soft_particles)
		{
			item.points[i].p3_motion_world_pos.z = item.points[i].p3_z;
			item.points[i].p3_motion_world_valid = 2;
		}
		item.points[i].p3_z += z_bias;
	}

	return true;
}

static bool VisEffectProjectBatchItemNoViewportClip(VisFireballBatchItem& item, float z_bias,
	bool soft_particles)
{
	if (item.nv < 3 || item.nv > VIS_FIREBALL_BATCH_MAX_VERTS)
		return false;

	item.RefreshPointList();

	for (int i = 0; i < item.nv; i++)
	{
		if (item.points[i].p3_codes & CC_BEHIND)
			return false;
		if (!(item.points[i].p3_flags & PF_PROJECTED))
			g3_ProjectPoint(&item.points[i]);
		if (soft_particles)
		{
			item.points[i].p3_motion_world_pos.z = item.points[i].p3_z;
			item.points[i].p3_motion_world_valid = 2;
		}
		item.points[i].p3_z += z_bias;
	}

	return true;
}

static void VisEffectColorToFloat(int color, float* red, float* green, float* blue)
{
	*red = GR_COLOR_RED(color) / 255.0f;
	*green = GR_COLOR_GREEN(color) / 255.0f;
	*blue = GR_COLOR_BLUE(color) / 255.0f;
}

static void VisEffectApplyBatchVertexColor(VisFireballBatchItem& item, float red, float green,
	float blue, float alpha)
{
	for (int i = 0; i < item.nv; i++)
	{
		item.points[i].p3_flags |= PF_RGBA;
		item.points[i].p3_r = red;
		item.points[i].p3_g = green;
		item.points[i].p3_b = blue;
		item.points[i].p3_a = alpha;
	}
}

static bool VisEffectBuildRotatedBatchItem(VisFireballBatchItem& item, const vector* pos, angle rot_angle,
	float width, float height)
{
	g3Point center;
	if (g3_RotatePoint(&center, const_cast<vector*>(pos)) & CC_BEHIND)
		return false;
	if (center.p3_codes & CC_OFF_FAR)
		return false;

	matrix rot_matrix;
	vm_AnglesToMatrix(&rot_matrix, 0, 0, rot_angle);
	rot_matrix.rvec *= Matrix_scale.x;
	rot_matrix.uvec *= Matrix_scale.y;

	vector rot_vectors[4];
	rot_vectors[0].x = -width;
	rot_vectors[0].y = height;
	rot_vectors[1].x = width;
	rot_vectors[1].y = height;
	rot_vectors[2].x = width;
	rot_vectors[2].y = -height;
	rot_vectors[3].x = -width;
	rot_vectors[3].y = -height;

	item.nv = 4;
	for (int i = 0; i < 4; i++)
	{
		rot_vectors[i].z = 0;
		vm_MatrixMulVector(&item.points[i].p3_vec, &rot_vectors[i], &rot_matrix);
		item.points[i].p3_flags = PF_UV | PF_RGBA;
		item.points[i].p3_l = 1.0f;
		item.points[i].p3_vec += center.p3_vec;
		g3_SetPointPreRotFromView(&item.points[i]);
		g3_CodePoint(&item.points[i]);
	}

	item.points[0].p3_u = 0.0f;
	item.points[0].p3_v = 0.0f;
	item.points[1].p3_u = 1.0f;
	item.points[1].p3_v = 0.0f;
	item.points[2].p3_u = 1.0f;
	item.points[2].p3_v = 1.0f;
	item.points[3].p3_u = 0.0f;
	item.points[3].p3_v = 1.0f;

	return true;
}

static bool VisEffectBuildPlanarBatchItem(VisFireballBatchItem& item, const vector* pos, const vector* norm,
	angle rot_angle, float width, float height)
{
	matrix rot_matrix;
	vm_VectorToMatrix(&rot_matrix, const_cast<vector*>(norm), NULL, NULL);
	vm_TransposeMatrix(&rot_matrix);

	matrix twist_matrix;
	vm_AnglesToMatrix(&twist_matrix, 0, 0, rot_angle);

	vector rot_vectors[4];
	rot_vectors[0] = (twist_matrix.rvec * -width);
	rot_vectors[0] += (twist_matrix.uvec * height);
	rot_vectors[1] = (twist_matrix.rvec * width);
	rot_vectors[1] += (twist_matrix.uvec * height);
	rot_vectors[2] = (twist_matrix.rvec * width);
	rot_vectors[2] -= (twist_matrix.uvec * height);
	rot_vectors[3] = (twist_matrix.rvec * -width);
	rot_vectors[3] -= (twist_matrix.uvec * height);

	item.nv = 4;
	for (int i = 0; i < 4; ++i)
	{
		vector temp_vec = rot_vectors[i];
		vm_MatrixMulVector(&rot_vectors[i], &temp_vec, &rot_matrix);
		rot_vectors[i] += *pos;
		g3_RotatePoint(&item.points[i], &rot_vectors[i]);
		item.points[i].p3_flags |= PF_UV | PF_L | PF_RGBA;
		item.points[i].p3_l = 1.0f;
	}

	item.points[0].p3_u = 0.0f;
	item.points[0].p3_v = 0.0f;
	item.points[1].p3_u = 1.0f;
	item.points[1].p3_v = 0.0f;
	item.points[2].p3_u = 1.0f;
	item.points[2].p3_v = 1.0f;
	item.points[3].p3_u = 0.0f;
	item.points[3].p3_v = 1.0f;

	return true;
}

static bool VisEffectIsSpecialFireball(int id)
{
	return id == LIGHTNING_BOLT_INDEX ||
		id == GRAY_LIGHTNING_BOLT_INDEX ||
		id == MASSDRIVER_EFFECT_INDEX ||
		id == MERCBOSS_MASSDRIVER_EFFECT_INDEX ||
		id == BILLBOARD_SMOKETRAIL_INDEX ||
		id == THICK_LIGHTNING_INDEX ||
		id == SINE_WAVE_INDEX ||
		id == BLAST_RING_INDEX ||
		id == FADING_LINE_INDEX ||
		id == SNOWFLAKE_INDEX ||
		id == RAINDROP_INDEX ||
		id == PUDDLEDROP_INDEX ||
		id == AXIS_BILLBOARD_INDEX;
}

static float GetSunCoronaSizeScale(int visnum)
{
	uint32_t noise = (uint32_t)Get60HzVisualTick() * 1664525u +
		(uint32_t)visnum * 1013904223u;
	noise ^= noise >> 16;
	return 1.0f + ((noise % 10u) / 100.0f);
}

static bool VisEffectBuildFireballBatchItem(vis_effect* vis, VisFireballBatchKey& key,
	VisFireballBatchItem& item, VisFireballBatchKey& blend_key, VisFireballBatchItem& blend_item,
	bool& has_blend_item)
{
	has_blend_item = false;

	if (vis->type != VIS_FIREBALL || VisEffectIsSpecialFireball(vis->id))
		return false;

	float time_live = Gametime - vis->creation_time;
	if (time_live < 0)
		time_live = 0;

	float size = vis->size;
	if (Katmai)
	{
		if (vis->id == BIG_EXPLOSION_INDEX || vis->id == MED_EXPLOSION_INDEX ||
			vis->id == MED_EXPLOSION_INDEX2 || vis->id == MED_EXPLOSION_INDEX3)
			size *= 1.8f;
	}

	const int visnum = vis - VisEffects;
	angle rot_angle;
	fireball* fb = &Fireballs[vis->id];

	if (fb->type == FT_BILLOW)
		rot_angle = Get60HzVisualAngle(160.0f, visnum * 5000);
	else if (vis->flags & VF_ATTACHED)
		rot_angle = 0;
	else if (vis->id == RUBBLE1_INDEX || vis->id == RUBBLE2_INDEX)
		rot_angle = Get60HzVisualAngle(860.0f, visnum * 5000);
	else if (vis->id == SUN_CORONA_INDEX)
	{
		rot_angle = Get60HzVisualAngle(500.0f, visnum * 5000);
		size *= GetSunCoronaSizeScale(visnum);
	}
	else
		rot_angle = (visnum * 5000) % 65536;

	float norm_time = time_live / vis->lifetime;
	if (vis->flags & VF_ATTACHED)
	{
		int int_time_live = time_live;
		norm_time = time_live - int_time_live;
	}

	if (norm_time >= 1)
		norm_time = 0.99999f;

	if (vis->flags & VF_EXPAND)
		size = (vis->size / 2) + ((vis->size * norm_time) / 2);

	int bm_handle = BAD_BITMAP_HANDLE;
	int bitmap_width = 0;
	int bitmap_height = 0;
	float u0 = 0.0f;
	float v0 = 0.0f;
	float u1 = 1.0f;
	float v1 = 1.0f;
	int blend_bm_handle = BAD_BITMAP_HANDLE;
	int blend_bitmap_width = 0;
	int blend_bitmap_height = 0;
	float blend_u0 = 0.0f;
	float blend_v0 = 0.0f;
	float blend_u1 = 1.0f;
	float blend_v1 = 1.0f;
	float frame1_weight = 0.0f;
	auto select_vclip_bitmap = [&](int vnum, bool loop) {
		vclip* vc = &GameVClips[vnum];
		VisEffectVClipFrameBlend frame_blend = VisEffectCalcVClipFrameBlend(vc, norm_time, loop);
		VisEffectSelectVClipBitmap(vnum, frame_blend.frame0, &bm_handle, &bitmap_width, &bitmap_height,
			&u0, &v0, &u1, &v1);
		if (frame_blend.has_frame1)
		{
			VisEffectSelectVClipBitmap(vnum, frame_blend.frame1, &blend_bm_handle,
				&blend_bitmap_width, &blend_bitmap_height, &blend_u0, &blend_v0, &blend_u1, &blend_v1);
			if (blend_bm_handle > BAD_BITMAP_HANDLE)
				frame1_weight = frame_blend.frame1_weight;
		}
	};

	if (vis->id == SMOKE_TRAIL_INDEX)
	{
		int texnum = vis->custom_handle;
		if (GameTextures[texnum].flags & TF_ANIMATED)
		{
			int vnum = GameTextures[texnum].bm_handle;
			select_vclip_bitmap(vnum, (vis->flags & VF_ATTACHED) != 0);
		}
		else
			VisEffectSelectBitmapForBatch(GameTextures[texnum].bm_handle, &bm_handle,
				&bitmap_width, &bitmap_height, &u0, &v0, &u1, &v1);
	}
	else if (vis->id == SPRAY_INDEX)
	{
		int vnum = vis->custom_handle;
		select_vclip_bitmap(vnum, (vis->flags & VF_ATTACHED) != 0);
	}
	else if (vis->id == CUSTOM_EXPLOSION_INDEX || vis->id == PARTICLE_INDEX)
	{
		if (GameTextures[vis->custom_handle].flags & TF_ANIMATED)
		{
			int vnum = GameTextures[vis->custom_handle].bm_handle;
			select_vclip_bitmap(vnum, (vis->flags & VF_ATTACHED) != 0);
		}
		else
			VisEffectSelectBitmapForBatch(GetTextureBitmap(vis->custom_handle, 0), &bm_handle,
				&bitmap_width, &bitmap_height, &u0, &v0, &u1, &v1);
	}
	else if (fb->type == FT_SPARK)
	{
		VisEffectSelectBitmapForBatch(fb->bm_handle, &bm_handle, &bitmap_width, &bitmap_height,
			&u0, &v0, &u1, &v1);
		size *= (1.0f - norm_time);
	}
	else if (vis->id == SUN_CORONA_INDEX || vis->id == MUZZLE_FLASH_INDEX ||
		vis->id == RUBBLE1_INDEX || vis->id == RUBBLE2_INDEX)
	{
		VisEffectSelectBitmapForBatch(fb->bm_handle, &bm_handle, &bitmap_width, &bitmap_height,
			&u0, &v0, &u1, &v1);
	}
	else
	{
		int vnum = fb->bm_handle;
		select_vclip_bitmap(vnum, (vis->flags & VF_ATTACHED) != 0);
	}

	if (bitmap_width <= 0 || bitmap_height <= 0)
	{
		if (bm_handle <= BAD_BITMAP_HANDLE || !GameBitmaps[bm_handle].used)
			return false;

		bitmap_width = bm_w(bm_handle, 0);
		bitmap_height = bm_h(bm_handle, 0);
	}

	if (bitmap_width <= 0 || bitmap_height <= 0)
		return false;

	if (fb->type == FT_SMOKE)
	{
		if (norm_time > 0.3f)
		{
			float temp_time = (norm_time - 0.3f) / 0.7f;
			if (vis->flags & VF_REVERSE)
				size /= (1 + (temp_time * 2.3f));
			else
				size *= (1 + (temp_time * 2.3f));
		}
	}

	sbyte alpha_type;
	if (vis->id == SMOKE_TRAIL_INDEX || vis->id == CUSTOM_EXPLOSION_INDEX || vis->id == PARTICLE_INDEX)
	{
		if (GameTextures[vis->custom_handle].flags & TF_SATURATE)
			alpha_type = AT_SATURATE_TEXTURE;
		else
			alpha_type = ATF_CONSTANT + ATF_TEXTURE;
	}
	else if (vis->id == BLACK_SMOKE_INDEX)
	{
		alpha_type = AT_LIGHTMAP_BLEND;
	}
	else if ((fb->type == FT_SMOKE && vis->id != MED_SMOKE_INDEX) ||
		vis->id == RUBBLE1_INDEX || vis->id == RUBBLE2_INDEX)
	{
		alpha_type = ATF_CONSTANT + ATF_TEXTURE;
	}
	else
	{
		alpha_type = AT_SATURATE_TEXTURE;
	}

	float val;
	if (norm_time > 0.5f)
		val = 1.0f - ((norm_time - 0.5f) / 0.5f);
	else
		val = 1.0f;

	if (size > MAX_FIREBALL_SIZE)
		size = MAX_FIREBALL_SIZE;
	if ((vis->id != BIG_EXPLOSION_INDEX && vis->id != BLUE_EXPLOSION_INDEX) &&
		size > (MAX_FIREBALL_SIZE / 2))
		size = MAX_FIREBALL_SIZE / 2;

	ubyte alpha_value;
	if (vis->id == SMOKE_TRAIL_INDEX || vis->id == CUSTOM_EXPLOSION_INDEX || vis->id == PARTICLE_INDEX)
		alpha_value = VisEffectClampAlpha(val * GameTextures[vis->custom_handle].alpha * 255.0f);
	else if (fb->type == FT_SMOKE)
		alpha_value = VisEffectClampAlpha(val * SMOKE_ALPHA * 255.0f);
	else if (vis->id == RUBBLE1_INDEX || vis->id == RUBBLE2_INDEX)
		alpha_value = 255;
	else if (vis->id == MUZZLE_FLASH_INDEX)
		alpha_value = 128;
	else if (vis->flags & VF_ATTACHED)
		alpha_value = VisEffectClampAlpha(FIREBALL_ALPHA * 255.0f);
	else
		alpha_value = VisEffectClampAlpha(val * FIREBALL_ALPHA * 255.0f);

	float vertex_alpha;
	const sbyte batch_alpha_type = VisEffectBatchAlphaType(alpha_type, alpha_value, &vertex_alpha);

	key.bitmap_handle = bm_handle;
	key.alpha_type = batch_alpha_type;
	key.soft_particles = VisEffectShouldUseSoftParticlesForEffect(vis);
	key.roomnum = vis->roomnum;

	blend_key = key;
	blend_key.bitmap_handle = blend_bm_handle;

	float vertex_red = 1.0f;
	float vertex_green = 1.0f;
	float vertex_blue = 1.0f;

	if (vis->id == RUBBLE1_INDEX || vis->id == RUBBLE2_INDEX || vis->id == GRAY_SPARK_INDEX)
	{
		VisEffectColorToFloat(GR_16_TO_COLOR(vis->lighting_color), &vertex_red, &vertex_green,
			&vertex_blue);
	}

	const float width = size;
	const float height = (size * bitmap_height) / bitmap_width;
	const float z_bias = VisEffectBillboardDepthBias(vis, fb, size, key.soft_particles);
	const bool blend_valid = frame1_weight > 0.001f && blend_bm_handle > BAD_BITMAP_HANDLE &&
		blend_bitmap_width > 0 && blend_bitmap_height > 0;
	bool built;

	if (vis->flags & VF_PLANAR)
		built = VisEffectBuildPlanarBatchItem(item, &vis->pos, &vis->end_pos, rot_angle, width, height);
	else
		built = VisEffectBuildRotatedBatchItem(item, &vis->pos, rot_angle, width, height);

	if (built)
	{
		if (blend_valid)
		{
			blend_item = item;
			VisEffectApplyBatchUVs(blend_item, blend_u0, blend_v0, blend_u1, blend_v1);
			VisEffectApplyBatchVertexColor(blend_item, vertex_red, vertex_green, vertex_blue,
				vertex_alpha * frame1_weight);
			has_blend_item = true;
		}

		VisEffectApplyBatchUVs(item, u0, v0, u1, v1);
		VisEffectApplyBatchVertexColor(item, vertex_red, vertex_green, vertex_blue,
			vertex_alpha * (blend_valid ? (1.0f - frame1_weight) : 1.0f));
		built = VisEffectClipAndProjectBatchItem(item, z_bias, key.soft_particles);
		if (built && has_blend_item)
			built = VisEffectClipAndProjectBatchItem(blend_item, z_bias, blend_key.soft_particles);
	}

	return built;
}

static bool VisEffectIsWeatherBatchCandidate(int id)
{
	return id == RAINDROP_INDEX ||
		id == PUDDLEDROP_INDEX ||
		id == SNOWFLAKE_INDEX ||
		id == FADING_LINE_INDEX;
}

static bool VisEffectClipAndProjectLineBatchItem(VisWeatherLineBatchItem& item)
{
	g3Point* p0 = &item.points[0];
	g3Point* p1 = &item.points[1];

	if (p0->p3_codes & p1->p3_codes)
		return false;

	const ubyte codes_or = p0->p3_codes | p1->p3_codes;
	bool was_clipped = false;
	if (codes_or)
	{
		ClipLine(&p0, &p1, codes_or);
		was_clipped = true;
	}

	if (!(p0->p3_flags & PF_PROJECTED))
		g3_ProjectPoint(p0);
	if (!(p1->p3_flags & PF_PROJECTED))
		g3_ProjectPoint(p1);

	g3Point clipped_points[2] = { *p0, *p1 };
	clipped_points[0].p3_flags &= ~PF_TEMP_POINT;
	clipped_points[1].p3_flags &= ~PF_TEMP_POINT;

	if (was_clipped)
	{
		if (p0->p3_flags & PF_TEMP_POINT)
			FreeTempPoint(p0);
		if (p1->p3_flags & PF_TEMP_POINT)
			FreeTempPoint(p1);
		CheckTempPoints();
	}

	item.points[0] = clipped_points[0];
	item.points[1] = clipped_points[1];
	return true;
}

static bool VisEffectBuildWeatherLineBatchItem(vis_effect* vis, VisWeatherLineBatchItem& item,
	VisWeatherLineBatchKey& key)
{
	if (vis->id != FADING_LINE_INDEX)
		return false;

	float time_live = Gametime - vis->creation_time;
	if (time_live < 0.0f)
		time_live = 0.0f;
	float norm_time = time_live / vis->lifetime;
	if (norm_time >= 1.0f)
		norm_time = 0.99999f;

	vector vecs[2];
	vecs[0] = vis->pos;
	vecs[1] = vis->end_pos;

	if (!(vis->flags & VF_WINDSHIELD_EFFECT))
	{
		vector vel = -vis->velocity;
		vm_NormalizeVectorFast(&vel);
		vecs[1] = vis->pos + (vel * vis->size);
	}

	ddgr_color color = GR_16_TO_COLOR(vis->lighting_color);
	const float red = GR_COLOR_RED(color) / 255.0f;
	const float green = GR_COLOR_GREEN(color) / 255.0f;
	const float blue = GR_COLOR_BLUE(color) / 255.0f;

	for (int i = 0; i < 2; i++)
	{
		g3_RotatePoint(&item.points[i], &vecs[i]);
		item.points[i].p3_flags |= PF_RGBA;
		item.points[i].p3_r = red;
		item.points[i].p3_g = green;
		item.points[i].p3_b = blue;
	}

	item.points[0].p3_a = (vis->flags & VF_WINDSHIELD_EFFECT) ? 0.3f : (1.0f - norm_time);
	item.points[1].p3_a = 0.0f;

	key.soft_particles = false;
	key.roomnum = vis->roomnum;
	return VisEffectClipAndProjectLineBatchItem(item);
}

static bool VisEffectBuildWeatherQuadBatchItem(vis_effect* vis, VisWeatherBatchKey& key,
	VisFireballBatchItem& item)
{
	if (vis->id != RAINDROP_INDEX && vis->id != PUDDLEDROP_INDEX && vis->id != SNOWFLAKE_INDEX)
		return false;

	float time_live = Gametime - vis->creation_time;
	if (time_live < 0.0f)
		time_live = 0.0f;
	float norm_time = time_live / vis->lifetime;
	if (norm_time >= 1.0f)
		norm_time = 0.99999f;

	fireball* fb = &Fireballs[vis->id];
	int bm_handle = BAD_BITMAP_HANDLE;
	int bitmap_width = 0;
	int bitmap_height = 0;
	float u0 = 0.0f;
	float v0 = 0.0f;
	float u1 = 1.0f;
	float v1 = 1.0f;
	const bool enhanced_snow = vis->id == SNOWFLAKE_INDEX &&
		(vis->flags & VF_ENHANCED_SNOW) != 0;
	if (enhanced_snow && VisEffectCreateEnhancedSnowTextures())
	{
		bm_handle = Enhanced_snow_atlas_handle;
		bitmap_width = 32;
		bitmap_height = 32;
		const int variant = vis->custom_handle & 3;
		u0 = (variant * 32.0f + 0.5f) / 128.0f;
		u1 = ((variant + 1) * 32.0f - 0.5f) / 128.0f;
		v0 = 0.5f / 32.0f;
		v1 = 31.5f / 32.0f;
	}
	else
	{
		VisEffectSelectBitmapForBatch(fb->bm_handle, &bm_handle, &bitmap_width, &bitmap_height,
			&u0, &v0, &u1, &v1);
	}

	if (bm_handle <= BAD_BITMAP_HANDLE || bitmap_width <= 0 || bitmap_height <= 0)
		return false;

	vector pos = vis->pos;
	float size = vis->size;
	float alpha = 1.0f;
	bool zbuffer_state = true;
	bool built = false;

	if (vis->id == RAINDROP_INDEX || vis->id == PUDDLEDROP_INDEX)
	{
		size *= (1.0f - (norm_time / 2.0f));
		float val;
		if (norm_time > 0.5f)
			val = 1.0f - ((norm_time - 0.5f) / 0.5f);
		else if (norm_time < 0.1f)
			val = norm_time / 0.1f;
		else
			val = 1.0f;

		if (vis->id == RAINDROP_INDEX)
		{
			zbuffer_state = false;
			alpha = val * 0.4f;
			pos = Viewer_object->pos;
			pos += Viewer_object->orient.rvec * vis->pos.x;
			pos += Viewer_object->orient.uvec * vis->pos.y;
			pos += Viewer_object->orient.fvec * vis->pos.z;
			const float width = size;
			const float height = (size * bitmap_height) / bitmap_width;
			built = VisEffectBuildRotatedBatchItem(item, &pos, 0, width, height);
		}
		else
		{
			zbuffer_state = true;
			alpha = val * 0.2f;
			ASSERT(!((vis->end_pos.x == 0.0) && (vis->end_pos.y == 0.0) && (vis->end_pos.z == 0.0)));
			const float width = size;
			const float height = (size * bitmap_height) / bitmap_width;
			built = VisEffectBuildPlanarBatchItem(item, &pos, &vis->end_pos, 0, width, height);
		}
	}
	else if (enhanced_snow)
	{
		const EnhancedSnowRenderParams params =
			VisEffectEnhancedSnowRenderParams(vis, time_live, norm_time);
		alpha = params.alpha;
		if (vis->custom_handle & 4)
			built = VisEffectBuildPlanarBatchItem(item, &pos, &vis->velocity, 0,
				params.width, params.height);
		else
			built = VisEffectBuildRotatedBatchItem(item, &pos, params.rotation,
				params.width, params.height);
	}
	else
	{
		alpha = (1.0f - norm_time) * 0.6f * VisEffectSnowFadeInFactor(time_live);
		const float width = size;
		const float height = (size * bitmap_height) / bitmap_width;
		built = VisEffectBuildRotatedBatchItem(item, &pos, 0, width, height);
	}

	if (!built)
		return false;

	VisEffectApplyBatchUVs(item, u0, v0, u1, v1);

	float red = 1.0f;
	float green = 1.0f;
	float blue = 1.0f;
	if (vis->id == SNOWFLAKE_INDEX)
		VisEffectColorToFloat(GR_16_TO_COLOR(vis->lighting_color), &red, &green, &blue);
	VisEffectApplyBatchVertexColor(item, red, green, blue, alpha);

	key.bitmap_handle = bm_handle;
	key.alpha_type = AT_SATURATE_TEXTURE_VERTEX;
	key.soft_particles = (vis->id == SNOWFLAKE_INDEX && !enhanced_snow) ?
		VisEffectShouldUseSoftSnowParticles(zbuffer_state) : false;
	key.zbuffer_state = zbuffer_state;
	key.roomnum = vis->roomnum;

	return VisEffectClipAndProjectBatchItem(item, 0.0f, key.soft_particles);
}

static bool VisEffectBuildSmokeTrailBatchItem(vis_effect* vis, VisSmokeTrailBatchKey& key,
	VisFireballBatchItem& item)
{
	if (vis->type != VIS_FIREBALL || vis->id != BILLBOARD_SMOKETRAIL_INDEX)
		return false;
	if (vis->lifetime <= 0.0f)
		return false;

	const float time_live = Gametime - vis->creation_time;
	float norm_time = time_live / vis->lifetime;
	if (norm_time >= 1.0f)
		norm_time = 0.99999f;
	if (norm_time < 0.0f)
		norm_time = 0.0f;

	float alpha_norm = vis->lifeleft / vis->lifetime;
	if (alpha_norm <= 0.0f)
		return false;

	float width = vis->billboard_info.width * alpha_norm;
	if (alpha_norm > 0.8f)
	{
		float newnorm = 1.0f - ((alpha_norm - 0.8f) / 0.2f);
		width *= newnorm;
	}
	if (width <= 0.0f)
		return false;

	alpha_norm *= 0.7f;

	g3Point top_point;
	g3Point bot_point;
	g3_RotatePoint(&top_point, &vis->pos);
	g3_RotatePoint(&bot_point, &vis->end_pos);

	if (!GetBillboardCorners(item.points, &top_point, &bot_point, width))
		return false;
	item.nv = 4;

	int texnum = vis->custom_handle;
	if (texnum < 0 || texnum >= MAX_TEXTURES || !GameTextures[texnum].used)
		return false;

	int source_bm_handle = BAD_BITMAP_HANDLE;
	if (GameTextures[texnum].flags & TF_ANIMATED)
	{
		vclip* vc = &GameVClips[GameTextures[texnum].bm_handle];
		if (!vc->used || !vc->frames || vc->num_frames <= 0)
			return false;
		int int_frame = vc->num_frames * norm_time;
		if (int_frame < 0)
			int_frame = 0;
		if (int_frame >= vc->num_frames)
			int_frame = vc->num_frames - 1;
		source_bm_handle = vc->frames[int_frame];
	}
	else
	{
		source_bm_handle = GetTextureBitmap(texnum, 0);
	}

	int bm_handle = BAD_BITMAP_HANDLE;
	int bitmap_width = 0;
	int bitmap_height = 0;
	float u0 = 0.0f;
	float v0 = 0.0f;
	float u1 = 1.0f;
	float v1 = 1.0f;
	VisEffectSelectBitmapForBatch(source_bm_handle, &bm_handle, &bitmap_width, &bitmap_height,
		&u0, &v0, &u1, &v1);
	if (bm_handle <= BAD_BITMAP_HANDLE || bitmap_width <= 0 || bitmap_height <= 0)
		return false;

	VisEffectApplyBatchUVs(item, u0, v0, u1, v1);
	float red = 1.0f;
	float green = 1.0f;
	float blue = 1.0f;
	VisEffectColorToFloat(GR_16_TO_COLOR(vis->lighting_color), &red, &green, &blue);
	VisEffectApplyBatchVertexColor(item, red, green, blue, alpha_norm);

	key.bitmap_handle = bm_handle;
	key.alpha_type = (GameTextures[texnum].flags & TF_SATURATE) ?
		AT_SATURATE_TEXTURE_VERTEX : AT_TEXTURE_VERTEX;
	key.soft_particles = VisEffectShouldUseSoftParticles();
	key.roomnum = vis->roomnum;

	return VisEffectProjectBatchItemNoViewportClip(item, 0.0f, key.soft_particles);
}

static bool VisEffectIsMassDriverTrail(int id)
{
	return id == MASSDRIVER_EFFECT_INDEX || id == MERCBOSS_MASSDRIVER_EFFECT_INDEX;
}

static int VisEffectGetMassDriverTrailBitmap()
{
	static int masstrail = -1;

	if (masstrail == -1)
		masstrail = FindTextureName("MassTrail");
	ASSERT(masstrail != -1);
	if (masstrail == -1)
		return BAD_BITMAP_HANDLE;

	return GetTextureBitmap(masstrail, 0);
}

static void VisEffectSetBatchItemLighting(VisFireballBatchItem& item, int color, float alpha)
{
	float red = 1.0f;
	float green = 1.0f;
	float blue = 1.0f;
	VisEffectColorToFloat(color, &red, &green, &blue);
	for (int i = 0; i < item.nv; i++)
	{
		item.points[i].p3_flags |= PF_L;
		item.points[i].p3_l = 1.0f;
	}
	VisEffectApplyBatchVertexColor(item, red, green, blue, alpha);
}

static void VisEffectAppendMassDriverTubeBatchParts(const vis_effect* vis, const matrix& orient,
	float radius, float mag, int bm_handle, int color, float alpha,
	std::vector<VisMassDriverBatchPart>& parts)
{
	const int circle_pieces = 16;
	vector center_vecs[2];
	g3Point arc_points[32];

	center_vecs[0] = vis->pos;
	center_vecs[1] = vis->end_pos;

	for (int i = 0; i < 2; i++)
	{
		for (int t = 0; t < circle_pieces; t++)
		{
			float norm = (float)t / (float)circle_pieces;
			vector arc_vec = center_vecs[i];

			arc_vec += orient.uvec * radius * FixSin(norm * 65536);
			arc_vec -= orient.rvec * radius * FixCos(norm * 65536);

			g3_RotatePoint(&arc_points[i * circle_pieces + t], &arc_vec);
			arc_points[i * circle_pieces + t].p3_flags |= PF_UV | PF_RGBA;
		}
	}

	VisMassDriverBatchKey key = {};
	key.bitmap_handle = bm_handle;
	key.alpha_type = AT_SATURATE_TEXTURE_VERTEX;
	key.soft_particles = false;
	key.roomnum = vis->roomnum;

	for (int k = 0; k < circle_pieces; k++)
	{
		int next = (k + 1) % circle_pieces;

		VisMassDriverBatchPart part = {};
		part.key = key;
		part.item.nv = 4;
		part.item.points[0] = arc_points[circle_pieces + k];
		part.item.points[1] = arc_points[k];
		part.item.points[2] = arc_points[next];
		part.item.points[3] = arc_points[circle_pieces + next];

		part.item.points[0].p3_u = 0.0f;
		part.item.points[0].p3_v = 0.0f;
		part.item.points[1].p3_u = 0.0f;
		part.item.points[1].p3_v = mag / 2.0f;
		part.item.points[2].p3_u = 1.0f;
		part.item.points[2].p3_v = mag / 2.0f;
		part.item.points[3].p3_u = 1.0f;
		part.item.points[3].p3_v = 0.0f;

		VisEffectSetBatchItemLighting(part.item, color, alpha);
		if (VisEffectClipAndProjectBatchItem(part.item, 0.0f, part.key.soft_particles))
			parts.push_back(part);
	}
}

static bool VisEffectBuildMassDriverBatchParts(vis_effect* vis, std::vector<VisMassDriverBatchPart>& parts)
{
	if (vis->type != VIS_FIREBALL || !VisEffectIsMassDriverTrail(vis->id))
		return false;
	if (vis->lifetime <= 0.0f)
		return false;

	const bool f_boss = vis->id == MERCBOSS_MASSDRIVER_EFFECT_INDEX;
	vector normvec = vis->end_pos - vis->pos;
	const float mag = vm_VectorDistanceQuick(&vis->pos, &vis->end_pos);
	if (mag <= 0.001f)
		return false;

	vector dir_norm = normvec / mag;
	vm_NormalizeVector(&normvec);
	matrix orient;
	vm_VectorToMatrix(&orient, &normvec, NULL, NULL);

	float size = ((float)vis->billboard_info.width) * 0.25f;
	if (f_boss)
		size *= 3.0f;

	float alpha_norm = (vis->lifeleft / vis->lifetime) * 0.5f;
	if (alpha_norm <= 0.0f)
		return true;
	if (alpha_norm > 1.0f)
		alpha_norm = 1.0f;

	int ring_bm_handle = GetTextureBitmap(vis->custom_handle, 0);
	if (ring_bm_handle <= BAD_BITMAP_HANDLE)
		return false;

	int rings = (int)(mag / 3.0f);
	if (rings > 400)
		rings = 400;
	parts.reserve(32 + (f_boss ? 0 : rings));

	const int inner_color = f_boss ? GR_RGB(255, 170, 170) : GR_RGB(200, 200, 255);
	VisEffectAppendMassDriverTubeBatchParts(vis, orient, size / 4.0f, mag, ring_bm_handle,
		inner_color, alpha_norm, parts);
	VisEffectAppendMassDriverTubeBatchParts(vis, orient, size, mag, ring_bm_handle,
		GR_16_TO_COLOR(vis->lighting_color), alpha_norm, parts);

	if (f_boss)
		return true;

	int trail_bm_handle = VisEffectGetMassDriverTrailBitmap();
	if (trail_bm_handle <= BAD_BITMAP_HANDLE)
		return false;

	float fsize = 3.0f;
	if (mag / 3.0f > 400.0f)
		fsize = mag / 400.0f;

	VisMassDriverBatchKey trail_key = {};
	trail_key.bitmap_handle = trail_bm_handle;
	trail_key.alpha_type = AT_SATURATE_TEXTURE_VERTEX;
	trail_key.soft_particles = false;
	trail_key.roomnum = vis->roomnum;

	const int int_gametime = (int)Gametime;
	const int frameroll = (int)((Gametime - int_gametime) * -(65536 * 4));
	for (int i = 1; i < rings; i++)
	{
		int rot_angle = (i * 2000) % 65536;
		vector pos = vis->pos + (dir_norm * (i * fsize));
		float new_size = 1.0f;
		new_size += FixSin((i * 9000) + frameroll) * 0.3f;

		VisMassDriverBatchPart part = {};
		part.key = trail_key;
		if (!VisEffectBuildPlanarBatchItem(part.item, &pos, &dir_norm, rot_angle, new_size,
			(new_size * bm_h(trail_bm_handle, 0)) / bm_w(trail_bm_handle, 0)))
			continue;

		VisEffectSetBatchItemLighting(part.item, GR_RGB(255, 255, 255), alpha_norm);
		if (VisEffectClipAndProjectBatchItem(part.item, 0.0f, part.key.soft_particles))
			parts.push_back(part);
	}

	return true;
}

static void FlushVisFireballBatchesNow()
{
	if (!VisEffectHasQueuedBatch())
	{
		return;
	}
	char marker_buffer[96];
	const char* marker = "VisEffect.FlushFireball";
	if (Perf_markers_enabled)
	{
		snprintf(marker_buffer, sizeof(marker_buffer), "VisEffect.FlushFireball.Count=%d", (int)VisFireball_batch_items.size());
		marker = marker_buffer;
	}
	PERF_MARKER_SCOPE(marker);
	RoomMaterialFogScope material_fog(Close_screen_rendering_late ? -1 :
		VisFireball_batch_key.roomnum);

	renderer_3d_draw_call_scope effect_draw_scope(RENDERER_DRAW_CALL_3D_EFFECT);

	rend_SetAlphaType(VisFireball_batch_key.alpha_type);
	rend_SetAlphaValue(255);
	rend_SetOverlayType(OT_NONE);
	rend_SetZBias(0.0f);
	rend_SetZBufferWriteMask(0);
	rend_SetWrapType(WT_CLAMP);
	rend_SetLighting(LS_GOURAUD);
	rend_SetColorModel(CM_RGB);
	rend_SetAOSuppression(1.0f);
	rend_SetTextureType(TT_LINEAR);
	rend_SetSoftParticleState(VisFireball_batch_key.soft_particles ? 1 : 0);

	std::vector<renderer_poly_batch_item> renderer_items;
	renderer_items.resize(VisFireball_batch_items.size());
	for (size_t i = 0; i < VisFireball_batch_items.size(); i++)
	{
		VisFireball_batch_items[i].RefreshPointList();
		renderer_items[i].pointlist = VisFireball_batch_items[i].pointlist;
		renderer_items[i].nv = VisFireball_batch_items[i].nv;
	}

	rend_DrawPolygon3DBatch(VisFireball_batch_key.bitmap_handle, renderer_items.data(),
		(int)renderer_items.size(), MAP_TYPE_BITMAP);

	rend_SetSoftParticleState(0);
	rend_SetAOSuppression(0.0f);
	rend_SetZBias(0.0f);
	rend_SetZBufferWriteMask(1);
	rend_SetWrapType(WT_WRAP);
	rend_SetLighting(LS_NONE);
	rend_SetColorModel(CM_MONO);

	VisFireball_batch_items.clear();
	VisFireball_batch_valid = false;
}

static void FlushVisSmokeTrailBatchesNow()
{
	if (!VisEffectHasQueuedSmokeTrailBatch())
		return;
	char marker_buffer[96];
	const char* marker = "VisEffect.FlushSmokeTrail";
	if (Perf_markers_enabled)
	{
		snprintf(marker_buffer, sizeof(marker_buffer), "VisEffect.FlushSmokeTrail.Count=%d", (int)VisSmokeTrail_batch_items.size());
		marker = marker_buffer;
	}
	PERF_MARKER_SCOPE(marker);
	RoomMaterialFogScope material_fog(Close_screen_rendering_late ? -1 :
		VisSmokeTrail_batch_key.roomnum);

	renderer_3d_draw_call_scope effect_draw_scope(RENDERER_DRAW_CALL_3D_EFFECT);

	rend_SetAlphaType(VisSmokeTrail_batch_key.alpha_type);
	rend_SetAlphaValue(255);
	rend_SetOverlayType(OT_NONE);
	rend_SetZBias(0.0f);
	rend_SetZBufferWriteMask(0);
	rend_SetWrapType(WT_CLAMP);
	rend_SetLighting(LS_GOURAUD);
	rend_SetColorModel(CM_RGB);
	rend_SetAOSuppression(1.0f);
	rend_SetTextureType(TT_LINEAR);
	rend_SetSoftParticleState(VisSmokeTrail_batch_key.soft_particles ? 1 : 0);

	std::vector<renderer_poly_batch_item> renderer_items;
	renderer_items.resize(VisSmokeTrail_batch_items.size());
	for (size_t i = 0; i < VisSmokeTrail_batch_items.size(); i++)
	{
		VisSmokeTrail_batch_items[i].RefreshPointList();
		renderer_items[i].pointlist = VisSmokeTrail_batch_items[i].pointlist;
		renderer_items[i].nv = VisSmokeTrail_batch_items[i].nv;
	}

	rend_DrawPolygon3DBatch(VisSmokeTrail_batch_key.bitmap_handle, renderer_items.data(),
		(int)renderer_items.size(), MAP_TYPE_BITMAP);

	rend_SetSoftParticleState(0);
	rend_SetAOSuppression(0.0f);
	rend_SetZBias(0.0f);
	rend_SetZBufferWriteMask(1);
	rend_SetWrapType(WT_WRAP);
	rend_SetLighting(LS_NONE);
	rend_SetColorModel(CM_MONO);

	VisSmokeTrail_batch_items.clear();
	VisSmokeTrail_batch_valid = false;
}

static void FlushVisMassDriverBatchesNow()
{
	if (!VisEffectHasQueuedMassDriverBatch())
		return;
	char marker_buffer[96];
	const char* marker = "VisEffect.FlushMassDriver";
	if (Perf_markers_enabled)
	{
		snprintf(marker_buffer, sizeof(marker_buffer), "VisEffect.FlushMassDriver.Count=%d", (int)VisMassDriver_batch_items.size());
		marker = marker_buffer;
	}
	PERF_MARKER_SCOPE(marker);
	RoomMaterialFogScope material_fog(Close_screen_rendering_late ? -1 :
		VisMassDriver_batch_key.roomnum);

	renderer_3d_draw_call_scope effect_draw_scope(RENDERER_DRAW_CALL_3D_EFFECT);

	rend_SetAlphaType(VisMassDriver_batch_key.alpha_type);
	rend_SetAlphaValue(255);
	rend_SetOverlayType(OT_NONE);
	rend_SetZBias(0.0f);
	rend_SetZBufferWriteMask(0);
	rend_SetWrapType(WT_WRAP);
	rend_SetLighting(LS_GOURAUD);
	rend_SetColorModel(CM_RGB);
	rend_SetAOSuppression(1.0f);
	rend_SetTextureType(TT_LINEAR);
	rend_SetSoftParticleState(VisMassDriver_batch_key.soft_particles ? 1 : 0);

	std::vector<renderer_poly_batch_item> renderer_items;
	renderer_items.resize(VisMassDriver_batch_items.size());
	for (size_t i = 0; i < VisMassDriver_batch_items.size(); i++)
	{
		VisMassDriver_batch_items[i].RefreshPointList();
		renderer_items[i].pointlist = VisMassDriver_batch_items[i].pointlist;
		renderer_items[i].nv = VisMassDriver_batch_items[i].nv;
	}

	rend_DrawPolygon3DBatch(VisMassDriver_batch_key.bitmap_handle, renderer_items.data(),
		(int)renderer_items.size(), MAP_TYPE_BITMAP);

	rend_SetSoftParticleState(0);
	rend_SetAOSuppression(0.0f);
	rend_SetZBias(0.0f);
	rend_SetZBufferWriteMask(1);
	rend_SetWrapType(WT_WRAP);
	rend_SetLighting(LS_NONE);
	rend_SetColorModel(CM_MONO);

	VisMassDriver_batch_items.clear();
	VisMassDriver_batch_valid = false;
}

static void FlushVisWeatherQuadBatchesNow()
{
	if (!VisEffectHasQueuedWeatherQuadBatch())
		return;
	char marker_buffer[96];
	const char* marker = "VisEffect.FlushWeatherQuad";
	if (Perf_markers_enabled)
	{
		snprintf(marker_buffer, sizeof(marker_buffer), "VisEffect.FlushWeatherQuad.Count=%d", (int)VisWeather_quad_batch_items.size());
		marker = marker_buffer;
	}
	PERF_MARKER_SCOPE(marker);
	RoomMaterialFogScope material_fog(Close_screen_rendering_late ? -1 :
		VisWeather_quad_batch_key.roomnum);

	renderer_3d_draw_call_scope effect_draw_scope(RENDERER_DRAW_CALL_3D_EFFECT);

	rend_SetAlphaType(VisWeather_quad_batch_key.alpha_type);
	rend_SetAlphaValue(255);
	rend_SetOverlayType(OT_NONE);
	rend_SetZBias(0.0f);
	rend_SetZBufferState(VisWeather_quad_batch_key.zbuffer_state ? 1 : 0);
	rend_SetZBufferWriteMask(0);
	rend_SetWrapType(WT_CLAMP);
	rend_SetLighting(LS_GOURAUD);
	rend_SetColorModel(CM_RGB);
	rend_SetAOSuppression(1.0f);
	rend_SetTextureType(TT_LINEAR);
	rend_SetSoftParticleState(VisWeather_quad_batch_key.soft_particles ? 1 : 0);

	static std::vector<renderer_poly_batch_item> renderer_items;
	renderer_items.clear();
	renderer_items.resize(VisWeather_quad_batch_items.size());
	for (size_t i = 0; i < VisWeather_quad_batch_items.size(); i++)
	{
		VisWeather_quad_batch_items[i].RefreshPointList();
		renderer_items[i].pointlist = VisWeather_quad_batch_items[i].pointlist;
		renderer_items[i].nv = VisWeather_quad_batch_items[i].nv;
	}

	rend_DrawPolygon3DBatch(VisWeather_quad_batch_key.bitmap_handle, renderer_items.data(),
		(int)renderer_items.size(), MAP_TYPE_BITMAP);

	rend_SetSoftParticleState(0);
	rend_SetAOSuppression(0.0f);
	rend_SetZBias(0.0f);
	rend_SetZBufferState(1);
	rend_SetZBufferWriteMask(1);
	rend_SetWrapType(WT_WRAP);
	rend_SetLighting(LS_NONE);
	rend_SetColorModel(CM_MONO);

	VisWeather_quad_batch_items.clear();
	VisWeather_quad_batch_valid = false;
}

static void FlushVisWeatherLineBatchesNow()
{
	if (!VisEffectHasQueuedWeatherLineBatch())
		return;
	char marker_buffer[96];
	const char* marker = "VisEffect.FlushWeatherLine";
	if (Perf_markers_enabled)
	{
		snprintf(marker_buffer, sizeof(marker_buffer), "VisEffect.FlushWeatherLine.Count=%d", (int)VisWeather_line_batch_items.size());
		marker = marker_buffer;
	}
	PERF_MARKER_SCOPE(marker);
	RoomMaterialFogScope material_fog(Close_screen_rendering_late ? -1 :
		VisWeather_line_batch_key.roomnum);

	renderer_3d_draw_call_scope effect_draw_scope(RENDERER_DRAW_CALL_3D_EFFECT);

	rend_SetAlphaType(AT_SATURATE_VERTEX);
	rend_SetTextureType(TT_FLAT);
	rend_SetLighting(LS_GOURAUD);
	rend_SetColorModel(CM_RGB);
	rend_SetOverlayType(OT_NONE);
	rend_SetZBias(0.0f);
	rend_SetZBufferState(1);
	rend_SetZBufferWriteMask(0);
	rend_SetAOSuppression(1.0f);
	rend_SetSoftParticleState(VisWeather_line_batch_key.soft_particles ? 1 : 0);

	std::vector<renderer_line_batch_item> renderer_items;
	renderer_items.resize(VisWeather_line_batch_items.size());
	for (size_t i = 0; i < VisWeather_line_batch_items.size(); i++)
	{
		VisWeather_line_batch_items[i].RefreshItem();
		renderer_items[i] = VisWeather_line_batch_items[i].item;
	}

	rend_DrawSpecialLineBatch(renderer_items.data(), (int)renderer_items.size());

	rend_SetSoftParticleState(0);
	rend_SetAOSuppression(0.0f);
	rend_SetZBias(0.0f);
	rend_SetZBufferWriteMask(1);
	rend_SetLighting(LS_NONE);
	rend_SetColorModel(CM_MONO);

	VisWeather_line_batch_items.clear();
	VisWeather_line_batch_valid = false;
}

static void FlushVisWeatherBatchesNow()
{
	FlushVisWeatherQuadBatchesNow();
	FlushVisWeatherLineBatchesNow();
}

static void FlushVisEffectBatchesNow()
{
	FlushVisFireballBatchesNow();
	FlushVisWeatherBatchesNow();
	FlushVisSmokeTrailBatchesNow();
	FlushVisMassDriverBatchesNow();
}

void FlushVisEffectBatches()
{
	if (VIS_FIREBALL_BARRIER_FLUSHES_ENABLED)
		FlushVisEffectBatchesNow();
}

void ForceFlushVisEffectBatches()
{
	FlushVisEffectBatchesNow();
}

static void QueueVisEffectBatchItem(const VisFireballBatchKey& key, const VisFireballBatchItem& item)
{
	FlushVisMassDriverBatchesNow();

	if (VisFireball_batch_valid && !VisFireball_batch_key.Equals(key))
		FlushVisFireballBatchesNow();

	if (!VisFireball_batch_valid)
	{
		VisFireball_batch_key = key;
		VisFireball_batch_valid = true;
	}

	VisFireball_batch_items.push_back(item);
}

static void QueueVisWeatherQuadBatchItem(const VisWeatherBatchKey& key, const VisFireballBatchItem& item)
{
	FlushVisFireballBatchesNow();
	FlushVisSmokeTrailBatchesNow();
	FlushVisMassDriverBatchesNow();
	FlushVisWeatherLineBatchesNow();

	if (VisWeather_quad_batch_valid && !VisWeather_quad_batch_key.Equals(key))
		FlushVisWeatherQuadBatchesNow();

	if (!VisWeather_quad_batch_valid)
	{
		VisWeather_quad_batch_key = key;
		VisWeather_quad_batch_valid = true;
	}

	VisWeather_quad_batch_items.push_back(item);
}

static void QueueVisWeatherLineBatchItem(const VisWeatherLineBatchKey& key,
	const VisWeatherLineBatchItem& item)
{
	FlushVisFireballBatchesNow();
	FlushVisSmokeTrailBatchesNow();
	FlushVisMassDriverBatchesNow();
	FlushVisWeatherQuadBatchesNow();

	if (VisWeather_line_batch_valid && !VisWeather_line_batch_key.Equals(key))
		FlushVisWeatherLineBatchesNow();

	if (!VisWeather_line_batch_valid)
	{
		VisWeather_line_batch_key = key;
		VisWeather_line_batch_valid = true;
	}

	VisWeather_line_batch_items.push_back(item);
}

static void QueueVisSmokeTrailBatchItem(const VisSmokeTrailBatchKey& key, const VisFireballBatchItem& item)
{
	FlushVisFireballBatchesNow();
	FlushVisWeatherBatchesNow();
	FlushVisMassDriverBatchesNow();

	if (VisSmokeTrail_batch_valid && !VisSmokeTrail_batch_key.Equals(key))
		FlushVisSmokeTrailBatchesNow();

	if (!VisSmokeTrail_batch_valid)
	{
		VisSmokeTrail_batch_key = key;
		VisSmokeTrail_batch_valid = true;
	}

	VisSmokeTrail_batch_items.push_back(item);
}

static void QueueVisMassDriverBatchItem(const VisMassDriverBatchKey& key,
	const VisFireballBatchItem& item)
{
	FlushVisFireballBatchesNow();
	FlushVisWeatherBatchesNow();
	FlushVisSmokeTrailBatchesNow();

	if (VisMassDriver_batch_valid && !VisMassDriver_batch_key.Equals(key))
		FlushVisMassDriverBatchesNow();

	if (!VisMassDriver_batch_valid)
	{
		VisMassDriver_batch_key = key;
		VisMassDriver_batch_valid = true;
	}

	VisMassDriver_batch_items.push_back(item);
}

static bool VisEffectIsLineOrStreakForCloseScreenLatePass(int id)
{
	return id == FADING_LINE_INDEX ||
		id == LIGHTNING_BOLT_INDEX ||
		id == GRAY_LIGHTNING_BOLT_INDEX ||
		id == THICK_LIGHTNING_INDEX ||
		id == SINE_WAVE_INDEX ||
		id == BILLBOARD_SMOKETRAIL_INDEX ||
		VisEffectIsMassDriverTrail(id);
}

static bool VisEffectIsCloseScreenLatePassCandidate(vis_effect* vis)
{
	if (!Rendering_main_view)
		return false;
	if (!VisEffectViewerIsFirstPersonLocalPlayerView())
		return false;

	if (vis->type != VIS_FIREBALL)
		return (vis->flags & VF_CLOSE_SCREEN_EFFECT) != 0;

	if (VisEffectIsLineOrStreakForCloseScreenLatePass(vis->id))
		return false;

	if ((vis->flags & VF_CLOSE_SCREEN_EFFECT) != 0)
		return true;

	if ((vis->flags & VF_WINDSHIELD_EFFECT) != 0 &&
		(vis->id == RAINDROP_INDEX || vis->id == SNOWFLAKE_INDEX))
		return true;

	return false;
}

void DrawEnhancedSnowParticlesBatched()
{
	int count = 0;
	const enhanced_snow_particle* particles = GetEnhancedSnowParticles(&count);
	if (!particles || count <= 0)
		return;

	if (!VisEffectCreateEnhancedSnowTextures())
		return;

	const int roomnum = Viewer_object ? Viewer_object->roomnum : 0;
	static std::vector<renderer_weather_quad> fast_quads;
	fast_quads.clear();
	if (fast_quads.capacity() < (size_t)count)
		fast_quads.reserve(count);

	for (int i = 0; i < count; ++i)
	{
		const enhanced_snow_particle& particle = particles[i];
		vis_effect vis = {};
		vis.type = VIS_FIREBALL;
		vis.id = SNOWFLAKE_INDEX;
		vis.pos = particle.pos;
		vis.velocity = particle.velocity;
		vis.end_pos = particle.ground_data;
		vis.mass = particle.phase;
		vis.drag = particle.flutter_frequency;
		vis.size = particle.size;
		vis.lifeleft = particle.lifeleft;
		vis.lifetime = particle.lifetime;
		vis.creation_time = particle.creation_time;
		vis.roomnum = roomnum;
		vis.custom_handle = (short)(particle.variant | ((particle.flags & 2) ? 4 : 0));
		vis.lighting_color = particle.lighting_color;
		vis.flags = VF_ENHANCED_SNOW;

		float time_live = Gametime - vis.creation_time;
		if (time_live < 0.0f)
			time_live = 0.0f;
		float norm_time = time_live / vis.lifetime;
		if (norm_time >= 1.0f)
			norm_time = 0.99999f;
		const EnhancedSnowRenderParams params =
			VisEffectEnhancedSnowRenderParams(&vis, time_live, norm_time);
		if (params.alpha > 0.0f)
		{
			renderer_weather_quad quad = {};
			quad.pos[0] = vis.pos.x;
			quad.pos[1] = vis.pos.y;
			quad.pos[2] = vis.pos.z;
			quad.velocity[0] = vis.velocity.x;
			quad.velocity[1] = vis.velocity.y;
			quad.velocity[2] = vis.velocity.z;
			quad.plane_normal[0] = vis.velocity.x;
			quad.plane_normal[1] = vis.velocity.y;
			quad.plane_normal[2] = vis.velocity.z;
			quad.width = params.width;
			quad.height = params.height;
			quad.rotation = (params.rotation / 65536.0f) * 6.28318530718f;
			const int variant = vis.custom_handle & 3;
			quad.u0 = (variant * 32.0f + 0.5f) / 128.0f;
			quad.u1 = ((variant + 1) * 32.0f - 0.5f) / 128.0f;
			quad.v0 = 0.5f / 32.0f;
			quad.v1 = 31.5f / 32.0f;
			VisEffectColorToFloat(GR_16_TO_COLOR(vis.lighting_color), &quad.r, &quad.g, &quad.b);
			quad.a = params.alpha;
			quad.planar = (vis.custom_handle & 4) != 0;
			fast_quads.push_back(quad);
		}
	}

	if (!fast_quads.empty())
	{
		RoomMaterialFogScope material_fog(Close_screen_rendering_late ? -1 : roomnum);
		renderer_3d_draw_call_scope effect_draw_scope(RENDERER_DRAW_CALL_3D_EFFECT);
		rend_SetAlphaType(AT_SATURATE_TEXTURE_VERTEX);
		rend_SetAlphaValue(255);
		rend_SetOverlayType(OT_NONE);
		rend_SetZBias(0.0f);
		rend_SetZBufferState(1);
		rend_SetZBufferWriteMask(0);
		rend_SetWrapType(WT_CLAMP);
		rend_SetLighting(LS_GOURAUD);
		rend_SetColorModel(CM_RGB);
		rend_SetAOSuppression(1.0f);
		rend_SetTextureType(TT_LINEAR);
		rend_SetSoftParticleState(0);
		if (rend_DrawWeatherQuadBatch(Enhanced_snow_atlas_handle, fast_quads.data(),
			(int)fast_quads.size(), MAP_TYPE_BITMAP))
		{
			rend_SetAOSuppression(0.0f);
			rend_SetZBias(0.0f);
			rend_SetZBufferState(1);
			rend_SetZBufferWriteMask(1);
			rend_SetWrapType(WT_WRAP);
			rend_SetLighting(LS_NONE);
			rend_SetColorModel(CM_MONO);
			return;
		}
		rend_SetAOSuppression(0.0f);
		rend_SetZBias(0.0f);
		rend_SetZBufferState(1);
		rend_SetZBufferWriteMask(1);
		rend_SetWrapType(WT_WRAP);
		rend_SetLighting(LS_NONE);
		rend_SetColorModel(CM_MONO);
	}

	for (int i = 0; i < count; ++i)
	{
		const enhanced_snow_particle& particle = particles[i];
		vis_effect vis = {};
		vis.type = VIS_FIREBALL;
		vis.id = SNOWFLAKE_INDEX;
		vis.pos = particle.pos;
		vis.velocity = particle.velocity;
		vis.end_pos = particle.ground_data;
		vis.mass = particle.phase;
		vis.drag = particle.flutter_frequency;
		vis.size = particle.size;
		vis.lifeleft = particle.lifeleft;
		vis.lifetime = particle.lifetime;
		vis.creation_time = particle.creation_time;
		vis.roomnum = roomnum;
		vis.custom_handle = (short)(particle.variant | ((particle.flags & 2) ? 4 : 0));
		vis.lighting_color = particle.lighting_color;
		vis.flags = VF_ENHANCED_SNOW;

		VisWeatherBatchKey weather_key = {};
		VisFireballBatchItem weather_item = {};
		if (VisEffectBuildWeatherQuadBatchItem(&vis, weather_key, weather_item))
			QueueVisWeatherQuadBatchItem(weather_key, weather_item);
	}
}

void DrawVisEffectMaybeBatched(vis_effect* vis)
{
	const bool close_screen_effect = VisEffectIsCloseScreenLatePassCandidate(vis);
	if (close_screen_effect && VisEffectQueueCloseScreenVisEffect(vis))
		return;

	if (vis->type == VIS_FIREBALL && VisEffectIsMassDriverTrail(vis->id))
	{
		std::vector<VisMassDriverBatchPart> mass_driver_parts;
		if (VisEffectBuildMassDriverBatchParts(vis, mass_driver_parts))
		{
			for (size_t i = 0; i < mass_driver_parts.size(); i++)
				QueueVisMassDriverBatchItem(mass_driver_parts[i].key, mass_driver_parts[i].item);
			return;
		}

		FlushVisEffectBatchesNow();
		DrawVisEffect(vis);
		return;
	}

	if (vis->type == VIS_FIREBALL && VisEffectIsWeatherBatchCandidate(vis->id))
	{
		if (vis->id == FADING_LINE_INDEX)
		{
			VisWeatherLineBatchItem line_item = {};
			VisWeatherLineBatchKey line_key = {};
			if (VisEffectBuildWeatherLineBatchItem(vis, line_item, line_key))
			{
				QueueVisWeatherLineBatchItem(line_key, line_item);
				return;
			}
		}
		else
		{
			VisWeatherBatchKey weather_key = {};
			VisFireballBatchItem weather_item = {};
			if (VisEffectBuildWeatherQuadBatchItem(vis, weather_key, weather_item))
			{
				QueueVisWeatherQuadBatchItem(weather_key, weather_item);
				return;
			}
			// Enhanced snow is a world-space volume and intentionally contains many
			// off-screen flakes. The batch builder has already clipped them; do not
			// fall through into the legacy Whiteball renderer.
			if (vis->id == SNOWFLAKE_INDEX && (vis->flags & VF_ENHANCED_SNOW))
				return;
		}
	}

	if (vis->type == VIS_FIREBALL && vis->id == BILLBOARD_SMOKETRAIL_INDEX)
	{
		VisSmokeTrailBatchKey smoke_key = {};
		VisFireballBatchItem smoke_item = {};
		if (VisEffectBuildSmokeTrailBatchItem(vis, smoke_key, smoke_item))
		{
			QueueVisSmokeTrailBatchItem(smoke_key, smoke_item);
			return;
		}
	}

	FlushVisWeatherBatchesNow();
	FlushVisSmokeTrailBatchesNow();

	VisFireballBatchKey key = {};
	VisFireballBatchItem item = {};
	VisFireballBatchKey blend_key = {};
	VisFireballBatchItem blend_item = {};
	bool has_blend_item = false;
	if (!VisEffectBuildFireballBatchItem(vis, key, item, blend_key, blend_item, has_blend_item))
	{
		FlushVisEffectBatchesNow();
		DrawVisEffect(vis);
		return;
	}

	QueueVisEffectBatchItem(key, item);
	if (has_blend_item)
		QueueVisEffectBatchItem(blend_key, blend_item);
}

void VisEffectRenderCloseScreenEffectsPostAO()
{
	VisEffectEndCloseScreenCollection();
	if (Close_screen_late_items.empty())
		return;

	if (Viewer_object == NULL || !rend_BeginPostPresentFrame())
	{
		VisEffectClearCloseScreenLateQueue();
		return;
	}

	PERF_MARKER_SCOPE("CloseScreenEffects.PostAO");

	std::stable_sort(Close_screen_late_items.begin(), Close_screen_late_items.end(),
		[](const CloseScreenLateRenderItem& left, const CloseScreenLateRenderItem& right)
		{
			if (left.z != right.z)
				return left.z < right.z;

			return left.sequence > right.sequence;
		});

	rend_StartPostPresentFrame(Game_window_x, Game_window_y,
		Game_window_x + Game_window_w, Game_window_y + Game_window_h, RF_CLEAR_ZBUFFER);

	const vector* view_pos = Close_screen_late_view_valid ? &Close_screen_late_view_pos : &Viewer_object->pos;
	const matrix* view_orient = Close_screen_late_view_valid ? &Close_screen_late_view_orient : &Viewer_object->orient;
	g3_StartFrame((vector*)view_pos, (matrix*)view_orient, Render_zoom);
	rend_SetSoftParticleState(0);

	Close_screen_rendering_late = true;
	for (int i = (int)Close_screen_late_items.size() - 1; i >= 0; i--)
	{
		const CloseScreenLateRenderItem& item = Close_screen_late_items[i];
		if (item.type == CLOSE_SCREEN_LATE_VISEFFECT)
		{
			if (item.index < 0 || item.index > Highest_vis_effect_index)
				continue;

			vis_effect* vis = &VisEffects[item.index];
			if (vis->type == VIS_NONE || (vis->flags & VF_DEAD))
				continue;

			FlushWeaponStreamerBatches();
			DrawVisEffectMaybeBatched(vis);
		}
		else if (item.type == CLOSE_SCREEN_LATE_WEAPON_OBJECT)
		{
			object* obj = ObjGet(item.handle);
			if (obj == NULL || obj->type != OBJ_WEAPON)
				continue;

			ForceFlushVisEffectBatches();
			DrawWeaponObject(obj);
		}
		else if (item.type == CLOSE_SCREEN_LATE_ALPHA)
		{
			ForceFlushVisEffectBatches();
			FlushWeaponStreamerBatches();
			DrawAlphaBlendedScreen(item.r, item.g, item.b, item.alpha);
		}
	}
	ForceFlushVisEffectBatches();
	FlushWeaponStreamerBatches();
	rend_SetSoftParticleState(0);
	Close_screen_rendering_late = false;

	g3_EndFrame();
	rend_EndFrame();
	VisEffectClearCloseScreenLateQueue();
}


// Draws a thick lighting bolt from one area to another
// Velocity.x is how many times to saturate
// Velocity.y is how fast the lightning moves across the v component
// Velocity.z is how many times the effect is tiled across the v component
// If Billboard_info.texture is not zero, it does autotiling
void DrawVisThickLightning(vis_effect* vis)
{
	//compute the corners of a rod.  fills in vertbuf.
	g3Point top_point, bot_point;
	g3Point pnts[4], * pntlist[4];

	g3_RotatePoint(&top_point, &vis->pos);
	g3_RotatePoint(&bot_point, &vis->end_pos);

	if (!GetBillboardCorners(pnts, &top_point, &bot_point, vis->billboard_info.width))
		return;

	vector line_norm = vis->end_pos - vis->pos;
	float line_mag = vm_GetMagnitudeFast(&line_norm);

	line_norm /= line_mag;

	if (line_mag < 1)
		return;

	int texnum = vis->custom_handle;
	int bm_handle = GetTextureBitmap(texnum, 0, true);
	float tile_factor = vis->velocity.z;
	float alpha_norm;
	int i, codes_and;

	if (vis->flags & VF_EXPAND)
		alpha_norm = vis->lifeleft / vis->lifetime;
	else
		alpha_norm = .7f;

	rend_SetOverlayType(OT_NONE);
	rend_SetTextureType(TT_LINEAR);
	rend_SetLighting(LS_FLAT_GOURAUD);
	rend_SetAlphaType(AT_SATURATE_TEXTURE);
	rend_SetWrapType(WT_WRAP);
	rend_SetFlatColor(GR_16_TO_COLOR(vis->lighting_color));
	rend_SetAlphaValue(255 * alpha_norm);
	rend_SetZBufferWriteMask(0);
	rend_SetAOSuppression(1.0f);
	rend_SetSoftParticleState(VisEffectShouldUseSoftParticles() ? 1 : 0);

	for (i = 0, codes_and = 0xff; i < 4; i++)
	{
		pntlist[i] = &pnts[i];
	}

	if (vis->billboard_info.texture)	// do autotiling
	{
		float ratio = line_mag / (float)vis->billboard_info.width;
		tile_factor *= ratio;
	}

	pnts[0].p3_u = 0;
	pnts[0].p3_v = 0;

	pnts[1].p3_u = 1;
	pnts[1].p3_v = 0;

	pnts[2].p3_u = 1;
	pnts[2].p3_v = tile_factor;

	pnts[3].p3_u = 0;
	pnts[3].p3_v = tile_factor;

	float vchange = 0;

	if (vis->velocity.y != 0)
	{
		int int_time = Gametime / vis->velocity.y;
		float norm_time = Gametime - (int_time * vis->velocity.y);
		norm_time /= vis->velocity.y;

		vchange = norm_time;
	}

	for (i = 0; i < 4; i++)
	{
		pnts[i].p3_v += vchange;
		pnts[i].p3_flags |= PF_UV;
	}

	// Now draw this thing!
	for (i = 0; i < vis->velocity.x; i++)
	{
		g3_DrawPoly(4, pntlist, bm_handle);
	}

	rend_SetSoftParticleState(0);
	rend_SetAOSuppression(0.0f);
	rend_SetZBufferWriteMask(1);
}

// Draws a bitmap that can orient around an axis
void DrawVisAxisBillboard(vis_effect* vis)
{
	float norm_time, alpha_norm = 1;
	float uchange = 0, vchange = 0;
	float time_live = Gametime - vis->creation_time;
	float size = vis->size;

	int visnum = vis - VisEffects;
	int bm_handle;
	int i;

	// Get corners of this billboard
	g3Point top_point, bot_point;
	g3Point pnts[4], * pntlist[4];

	g3_RotatePoint(&top_point, &vis->pos);
	g3_RotatePoint(&bot_point, &vis->end_pos);

	if (!GetBillboardCorners(pnts, &top_point, &bot_point, vis->billboard_info.width))
		return;

	for (i = 0; i < 4; i++)
	{
		pnts[i].p3_flags |= PF_UV;
		pntlist[i] = &pnts[i];
	}

	fireball* fb = &Fireballs[vis->id];

	norm_time = time_live / vis->lifetime;

	if (vis->flags & VF_EXPAND)
		alpha_norm = vis->lifeleft / vis->lifetime;

	if (norm_time >= 1)
		norm_time = 0.99999f;		// don't go over!

	if (vis->billboard_info.texture) // If its a texture, get image from texture
	{
		int texnum = vis->custom_handle;
		if (GameTextures[texnum].flags & TF_ANIMATED)
		{
			vclip* vc = &GameVClips[GameTextures[texnum].bm_handle];
			int int_frame = vc->num_frames * norm_time;
			bm_handle = vc->frames[int_frame];
		}
		else
			bm_handle = GetTextureBitmap(texnum, 0);

		if (GameTextures[texnum].flags & TF_SATURATE)
			rend_SetAlphaType(AT_SATURATE_TEXTURE);
		else
			rend_SetAlphaType(ATF_CONSTANT + ATF_TEXTURE);

		rend_SetAlphaValue(alpha_norm * GameTextures[texnum].alpha * 255);

		if (GameTextures[texnum].slide_u != 0)
		{
			int int_time = Gametime / GameTextures[texnum].slide_u;
			float norm_time = Gametime - (int_time * GameTextures[texnum].slide_u);
			norm_time /= GameTextures[texnum].slide_u;

			uchange = norm_time;
		}

		if (GameTextures[texnum].slide_v != 0)
		{
			int int_time = Gametime / GameTextures[texnum].slide_v;
			float norm_time = Gametime - (int_time * GameTextures[texnum].slide_v);
			norm_time /= GameTextures[texnum].slide_v;
			vchange = norm_time;
		}
	}
	else
	{
		bm_handle = vis->custom_handle;
		rend_SetAlphaType(ATF_CONSTANT + ATF_TEXTURE);
		rend_SetAlphaValue(alpha_norm * 255);
	}

	rend_SetOverlayType(OT_NONE);

	rend_SetZBufferWriteMask(0);
	rend_SetAOSuppression(1.0f);
	rend_SetSoftParticleState(VisEffectShouldUseSoftParticles() ? 1 : 0);

	if (uchange == 0 && vchange == 0)
		rend_SetWrapType(WT_CLAMP);
	else
		rend_SetWrapType(WT_WRAP);

	rend_SetLighting(LS_NONE);
	rend_SetColorModel(CM_MONO);
	rend_SetTextureType(TT_LINEAR);

	pnts[0].p3_u = 0;
	pnts[0].p3_v = 0;

	pnts[1].p3_u = 1;
	pnts[1].p3_v = 0;

	pnts[2].p3_u = 1;
	pnts[2].p3_v = 1;

	pnts[3].p3_u = 0;
	pnts[3].p3_v = 1;

	for (i = 0; i < 4; i++)
	{
		pnts[i].p3_u += uchange;
		pnts[i].p3_v += vchange;
	}

	g3_DrawPoly(4, pntlist, bm_handle);

	rend_SetSoftParticleState(0);
	rend_SetAOSuppression(0.0f);
	rend_SetZBufferWriteMask(1);

	rend_SetWrapType(WT_WRAP);
}

// Draws a bitmap that can orient around an axis
void DrawVisBillboardSmoketrail(vis_effect* vis)
{
	float norm_time, alpha_norm = 1;
	float uchange = 0, vchange = 0;
	float time_live = Gametime - vis->creation_time;
	float size = vis->size;

	int visnum = vis - VisEffects;
	int bm_handle;

	// Get corners of this billboard
	g3Point top_point, bot_point;
	g3Point pnts[4], * pntlist[4];

	g3_RotatePoint(&top_point, &vis->pos);
	g3_RotatePoint(&bot_point, &vis->end_pos);

	norm_time = time_live / vis->lifetime;
	alpha_norm = vis->lifeleft / vis->lifetime;
	float width = vis->billboard_info.width * alpha_norm;

	if (alpha_norm > 0.8f)
	{
		// Make this smoke puff a bit smaller at the beginning of its life
		float newnorm = 1.0f - ((alpha_norm - 0.8f) / 0.2f);
		width *= newnorm;
	}

	alpha_norm *= 0.7f;

	if (!GetBillboardCorners(pnts, &top_point, &bot_point, width))
		return;


	fireball* fb = &Fireballs[vis->id];

	for (int i = 0; i < 4; i++)
	{
		pnts[i].p3_flags |= PF_UV | PF_RGBA;
		pntlist[i] = &pnts[i];
		pnts[i].p3_a = alpha_norm;
	}

	if (norm_time >= 1)
		norm_time = 0.99999f;		// don't go over!

	int texnum = vis->custom_handle;
	if (GameTextures[texnum].flags & TF_ANIMATED)
	{
		vclip* vc = &GameVClips[GameTextures[texnum].bm_handle];
		int int_frame = vc->num_frames * norm_time;
		bm_handle = vc->frames[int_frame];
	}
	else
		bm_handle = GetTextureBitmap(texnum, 0);

	if (GameTextures[texnum].flags & TF_SATURATE)
		rend_SetAlphaType(AT_SATURATE_TEXTURE_VERTEX);
	else
		rend_SetAlphaType(AT_TEXTURE_VERTEX);

	rend_SetOverlayType(OT_NONE);

	rend_SetZBufferWriteMask(0);
	rend_SetAOSuppression(1.0f);

	rend_SetFlatColor(GR_16_TO_COLOR(vis->lighting_color));
	rend_SetColorModel(CM_MONO);
	rend_SetTextureType(TT_LINEAR);
	rend_SetLighting(LS_FLAT_GOURAUD);

	pnts[0].p3_u = 0;
	pnts[0].p3_v = 0;

	pnts[1].p3_u = 1;
	pnts[1].p3_v = 0;

	pnts[2].p3_u = 1;
	pnts[2].p3_v = 1;

	pnts[3].p3_u = 0;
	pnts[3].p3_v = 1;

	rend_SetSoftParticleState(VisEffectShouldUseSoftParticles() ? 1 : 0);
	bool valid = true;
	for (int i = 0; i < 4; i++)
	{
		if (pnts[i].p3_codes & CC_BEHIND)
		{
			valid = false;
			break;
		}
		if (!(pnts[i].p3_flags & PF_PROJECTED))
			g3_ProjectPoint(&pnts[i]);
	}
	if (valid)
		rend_DrawPolygon3D(bm_handle, pntlist, 4, MAP_TYPE_BITMAP);
	rend_SetSoftParticleState(0);

	rend_SetAOSuppression(0.0f);
	rend_SetZBufferWriteMask(1);
}

// Draws a long "stick" to represent the mass driver trail
void DrawVisMassDriverEffect(vis_effect* vis, bool f_boss)
{
	int i, t, k;
	static int masstrail = -1;

	if (masstrail == -1)
		masstrail = FindTextureName("MassTrail");
	ASSERT(masstrail != -1);		//DAJ -1FIX

	vector center_vecs[2];
	g3Point arc_points[32], * pntlist[32];

	center_vecs[0] = vis->pos;
	center_vecs[1] = vis->end_pos;

	vector normvec = center_vecs[1] - center_vecs[0];
	matrix orient;

	vm_NormalizeVector(&normvec);
	vm_VectorToMatrix(&orient, &normvec, NULL, NULL);

	float size = ((float)vis->billboard_info.width) * .25;
	int circle_pieces = 16;

	if (f_boss)
	{
		size *= 3.0f;
	}

	float mag = vm_VectorDistanceQuick(&vis->pos, &vis->end_pos);
	vector dir_norm = (vis->end_pos - vis->pos) / mag;
	float alpha_norm = (vis->lifeleft / vis->lifetime) * .5;
	int bm_handle = GetTextureBitmap(vis->custom_handle, 0);

	rend_SetAlphaType(AT_SATURATE_TEXTURE);
	rend_SetOverlayType(OT_NONE);
	rend_SetTextureType(TT_LINEAR);
	rend_SetLighting(LS_FLAT_GOURAUD);
	rend_SetZBufferWriteMask(0);
	rend_SetAOSuppression(1.0f);
	if (f_boss)
		rend_SetFlatColor(GR_RGB(255, 170, 170));
	else
		rend_SetFlatColor(GR_RGB(200, 200, 255));

	rend_SetAlphaValue(255 * alpha_norm);

	for (i = 0; i < 2; i++)
	{
		for (t = 0; t < circle_pieces; t++)
		{
			float norm = (float)t / (float)circle_pieces;
			vector arc_vec = center_vecs[i];

			arc_vec += (orient.uvec * (size / 4) * FixSin(norm * 65536));
			arc_vec -= (orient.rvec * (size / 4) * FixCos(norm * 65536));

			g3_RotatePoint(&arc_points[i * circle_pieces + t], &arc_vec);
			arc_points[i * circle_pieces + t].p3_flags |= PF_UV | PF_RGBA;
		}
	}

	for (k = 0; k < circle_pieces; k++)
	{
		int next = (k + 1) % circle_pieces;
		arc_points[k].p3_u = 0;
		arc_points[k].p3_v = mag / 2;

		arc_points[k + circle_pieces].p3_u = 0;
		arc_points[k + circle_pieces].p3_v = 0;

		arc_points[next + circle_pieces].p3_u = 1;
		arc_points[next + circle_pieces].p3_v = 0;


		arc_points[next].p3_u = 1;
		arc_points[next].p3_v = mag / 2;

		pntlist[0] = &arc_points[t + k];
		pntlist[1] = &arc_points[t + k - circle_pieces];
		pntlist[2] = &arc_points[t + next - circle_pieces];
		pntlist[3] = &arc_points[t + next];

		g3_DrawPoly(4, pntlist, bm_handle);
	}

	for (i = 0; i < 2; i++)
	{
		for (t = 0; t < circle_pieces; t++)
		{
			float norm = (float)t / (float)circle_pieces;
			vector arc_vec = center_vecs[i];

			arc_vec += (orient.uvec * (size)*FixSin(norm * 65536));
			arc_vec -= (orient.rvec * (size)*FixCos(norm * 65536));

			g3_RotatePoint(&arc_points[i * circle_pieces + t], &arc_vec);
			arc_points[i * circle_pieces + t].p3_flags |= PF_UV;
		}
	}

	rend_SetFlatColor(GR_16_TO_COLOR(vis->lighting_color));
	rend_SetAlphaValue(255 * alpha_norm);

	for (k = 0; k < circle_pieces; k++)
	{
		int next = (k + 1) % circle_pieces;
		arc_points[k].p3_u = 0;
		arc_points[k].p3_v = mag / 2;

		arc_points[k + circle_pieces].p3_u = 0;
		arc_points[k + circle_pieces].p3_v = 0;

		arc_points[next + circle_pieces].p3_u = 1;
		arc_points[next + circle_pieces].p3_v = 0;

		arc_points[next].p3_u = 1;
		arc_points[next].p3_v = mag / 2;

		pntlist[0] = &arc_points[t + k];
		pntlist[1] = &arc_points[t + k - circle_pieces];
		pntlist[2] = &arc_points[t + next - circle_pieces];
		pntlist[3] = &arc_points[t + next];

		if (!g3_DrawPoly(4, pntlist, bm_handle))
		{
			rend_SetAOSuppression(0.0f);
			rend_SetZBufferWriteMask(1);
			return;
		}
	}

	// Now draw some rings
	int rings = mag / 3;
	float fsize = 3;

	if (rings > 400)
	{
		rings = 400;
		fsize = mag / 400.0;
	}

	rend_SetLighting(LS_NONE);
	rend_SetAlphaType(AT_SATURATE_TEXTURE);
	rend_SetAlphaValue(255 * alpha_norm);

	bm_handle = GetTextureBitmap(masstrail, 0);

	int int_gametime = Gametime;
	int frameroll = (Gametime - int_gametime) * -(65536 * 4);


	if (!f_boss)
	{
		for (i = 1; i < rings; i++)
		{
			int rot_angle = (i * 2000) % 65536;
			vector pos = vis->pos + (dir_norm * (i * fsize));
			float new_size = 1.0;
			new_size += FixSin((i * 9000) + frameroll) * .3;

			g3_DrawPlanarRotatedBitmap(&pos, &dir_norm, rot_angle, new_size, (new_size * bm_h(bm_handle, 0)) / bm_w(bm_handle, 0), bm_handle);
		}
	}
	rend_SetAOSuppression(0.0f);
	rend_SetZBufferWriteMask(1);

}

// Renders a vis effect
void DrawVisEffect(vis_effect* vis)
{
	ASSERT(vis->type != VIS_NONE);
	RoomMaterialFogScope material_fog(Close_screen_rendering_late ? -1 : vis->roomnum);
	renderer_3d_draw_call_scope effect_draw_scope(RENDERER_DRAW_CALL_3D_EFFECT);

	// First check to see if these are special types

	if (vis->id == LIGHTNING_BOLT_INDEX || vis->id == GRAY_LIGHTNING_BOLT_INDEX)
	{
		DrawVisLightningBolt(vis);
		return;
	}
	else if (vis->id == MASSDRIVER_EFFECT_INDEX)
	{
		DrawVisMassDriverEffect(vis, false);
		return;
	}
	else if (vis->id == MERCBOSS_MASSDRIVER_EFFECT_INDEX)
	{
		DrawVisMassDriverEffect(vis, true);
		return;
	}
	else if (vis->id == BILLBOARD_SMOKETRAIL_INDEX)
	{
		DrawVisBillboardSmoketrail(vis);
		return;
	}
	else if (vis->id == THICK_LIGHTNING_INDEX)
	{
		DrawVisThickLightning(vis);
		return;
	}
	else if (vis->id == SINE_WAVE_INDEX)
	{
		DrawVisSineWave(vis);
		return;
	}
	else if (vis->id == BLAST_RING_INDEX)
	{
		DrawVisBlastRing(vis);
		return;
	}
	else if (vis->id == FADING_LINE_INDEX)
	{
		DrawVisFadingLine(vis);
		return;
	}
	else if (vis->id == SNOWFLAKE_INDEX)
	{
		DrawVisSnowflake(vis);
		return;
	}
	else if (vis->id == RAINDROP_INDEX || vis->id == PUDDLEDROP_INDEX)
	{
		DrawVisRainDrop(vis);
		return;
	}
	else if (vis->id == AXIS_BILLBOARD_INDEX)
	{
		DrawVisAxisBillboard(vis);
		return;
	}

	float norm_time;
	float time_live = Gametime - vis->creation_time;

	//This hack is needed for the demo system and Gamegauge, since it adjusts gametime during
	//the game there are times when this could be a negative number (which would be bad)
	if (time_live < 0)
		time_live = 0;

	float size = vis->size;

	// Bigger explosions for Katmai
	if (Katmai)
	{
		if (vis->id == BIG_EXPLOSION_INDEX || vis->id == MED_EXPLOSION_INDEX || vis->id == MED_EXPLOSION_INDEX2 || vis->id == MED_EXPLOSION_INDEX3)
			size *= 1.8f;
	}

	int visnum = vis - VisEffects;
	int rot_angle;
	int bm_handle;
	int blend_bm_handle = BAD_BITMAP_HANDLE;
	float frame1_weight = 0.0f;

	fireball* fb = &Fireballs[vis->id];

	if (fb->type == FT_BILLOW)
		rot_angle = Get60HzVisualAngle(160.0f, visnum * 5000);
	else if (vis->flags & VF_ATTACHED)
		rot_angle = 0;
	else if (vis->id == RUBBLE1_INDEX || vis->id == RUBBLE2_INDEX)
		rot_angle = Get60HzVisualAngle(860.0f, visnum * 5000);
	else if (vis->id == SUN_CORONA_INDEX)
	{
		rot_angle = Get60HzVisualAngle(500.0f, visnum * 5000);
		size *= GetSunCoronaSizeScale(visnum);
	}
	else
		rot_angle = (visnum * 5000) % 65536;

	norm_time = time_live / vis->lifetime;

	// TEMP!!
	if (vis->flags & VF_ATTACHED)
	{
		int int_time_live = time_live;
		norm_time = time_live - int_time_live;
	}

	if (norm_time >= 1)
		norm_time = .99999f;		// don't go over!

	if (vis->flags & VF_EXPAND)
	{
		size = (vis->size / 2) + ((vis->size * norm_time) / 2);
	}

	auto select_vclip_frame = [&](int vnum, bool loop) {
		vclip* vc = &GameVClips[vnum];
		VisEffectVClipFrameBlend frame_blend = VisEffectCalcVClipFrameBlend(vc, norm_time, loop);
		bm_handle = vc->frames[frame_blend.frame0];
		if (frame_blend.has_frame1)
		{
			blend_bm_handle = vc->frames[frame_blend.frame1];
			frame1_weight = frame_blend.frame1_weight;
		}
	};

	if (vis->id == SMOKE_TRAIL_INDEX) // If its a smoke trail, get image from texture
	{
		int texnum = vis->custom_handle;
		if (GameTextures[texnum].flags & TF_ANIMATED)
		{
			select_vclip_frame(GameTextures[texnum].bm_handle, (vis->flags & VF_ATTACHED) != 0);
		}
		else
			bm_handle = GameTextures[texnum].bm_handle;
	}
	else if (vis->id == SPRAY_INDEX)
	{
		int vnum = vis->custom_handle;
		select_vclip_frame(vnum, (vis->flags & VF_ATTACHED) != 0);

		//	if (norm_time<.5)
				//size=1+((vis->size-1)*(norm_time));
	}
	else if (vis->id == CUSTOM_EXPLOSION_INDEX || vis->id == PARTICLE_INDEX)	// Do custom
	{
		if ((GameTextures[vis->custom_handle].flags & TF_ANIMATED))
		{
			int vnum = GameTextures[vis->custom_handle].bm_handle;
			select_vclip_frame(vnum, (vis->flags & VF_ATTACHED) != 0);
		}
		else
			bm_handle = GetTextureBitmap(vis->custom_handle, 0);
	}
	else if (fb->type == FT_SPARK)	// Do spark
	{
		bm_handle = fb->bm_handle;
		size *= (1.0 - norm_time);

	}
	else if (vis->id == SUN_CORONA_INDEX || vis->id == MUZZLE_FLASH_INDEX || vis->id == RUBBLE1_INDEX || vis->id == RUBBLE2_INDEX)
	{
		bm_handle = fb->bm_handle;
	}
	else
	{
		select_vclip_frame(fb->bm_handle, (vis->flags & VF_ATTACHED) != 0);
	}


	if (fb->type == FT_SMOKE)
	{
		if (norm_time > .3)
		{
			float temp_time = (norm_time - .3);
			temp_time /= .7f;

			if (vis->flags & VF_REVERSE)
				size /= (1 + (temp_time * 2.3));
			else
				size *= (1 + (temp_time * 2.3));
		}
	}

	// Set some alpha	
	if (vis->id == SMOKE_TRAIL_INDEX || vis->id == CUSTOM_EXPLOSION_INDEX || vis->id == PARTICLE_INDEX)
	{
		if (GameTextures[vis->custom_handle].flags & TF_SATURATE)
			rend_SetAlphaType(AT_SATURATE_TEXTURE);
		else
			rend_SetAlphaType(ATF_CONSTANT + ATF_TEXTURE);
	}
	else if (vis->id == BLACK_SMOKE_INDEX)
	{
		rend_SetAlphaType(AT_LIGHTMAP_BLEND);
	}
	else if ((fb->type == FT_SMOKE && vis->id != MED_SMOKE_INDEX) || vis->id == RUBBLE1_INDEX || vis->id == RUBBLE2_INDEX)
	{
		rend_SetAlphaType(ATF_CONSTANT + ATF_TEXTURE);
	}
	else
		rend_SetAlphaType(AT_SATURATE_TEXTURE);

	float val;
	if (norm_time > .5)
		val = 1.0 - ((norm_time - .5) / .5);
	else
		val = 1.0;

	// Cap size
	if (size > MAX_FIREBALL_SIZE)
		size = MAX_FIREBALL_SIZE;
	if ((vis->id != BIG_EXPLOSION_INDEX && vis->id != BLUE_EXPLOSION_INDEX) && size > (MAX_FIREBALL_SIZE / 2))
		size = MAX_FIREBALL_SIZE / 2;

	float base_alpha;
	if (vis->id == SMOKE_TRAIL_INDEX || vis->id == CUSTOM_EXPLOSION_INDEX || vis->id == PARTICLE_INDEX)
		base_alpha = val * GameTextures[vis->custom_handle].alpha * 255;
	else if (fb->type == FT_SMOKE)
		base_alpha = val * SMOKE_ALPHA * 255;
	else if (vis->id == RUBBLE1_INDEX || vis->id == RUBBLE2_INDEX)
		base_alpha = 255;
	else if (vis->id == MUZZLE_FLASH_INDEX)
		base_alpha = 128;
	else if (vis->flags & VF_ATTACHED)
		base_alpha = FIREBALL_ALPHA * 255;
	else
		base_alpha = val * FIREBALL_ALPHA * 255;

	const bool blend_frame = frame1_weight > 0.001f && blend_bm_handle > BAD_BITMAP_HANDLE;
	rend_SetAlphaValue(VisEffectClampAlpha(base_alpha * (blend_frame ? (1.0f - frame1_weight) : 1.0f)));

	rend_SetOverlayType(OT_NONE);

	const bool use_soft_intersection = VisEffectShouldUseSoftParticlesForEffect(vis);
	rend_SetZBias(VisEffectBillboardDepthBias(vis, fb, size, use_soft_intersection));

	rend_SetZBufferWriteMask(0);
	rend_SetWrapType(WT_CLAMP);
	rend_SetLighting(LS_NONE);
	rend_SetAOSuppression(1.0f);
	rend_SetSoftParticleState(use_soft_intersection ? 1 : 0);

	auto draw_frame = [&](int frame_bm_handle) {
		if (vis->id == RUBBLE1_INDEX || vis->id == RUBBLE2_INDEX || vis->id == GRAY_SPARK_INDEX)
		{
			int color = GR_16_TO_COLOR(vis->lighting_color);
			g3_DrawRotatedBitmap(&vis->pos, rot_angle, size,
				(size * bm_h(frame_bm_handle, 0)) / bm_w(frame_bm_handle, 0), frame_bm_handle, color);
		}
		else
		{
			if (vis->flags & VF_PLANAR)
				g3_DrawPlanarRotatedBitmap(&vis->pos, &vis->end_pos, rot_angle, size,
					(size * bm_h(frame_bm_handle, 0)) / bm_w(frame_bm_handle, 0), frame_bm_handle);
			else
				g3_DrawRotatedBitmap(&vis->pos, rot_angle, size,
					(size * bm_h(frame_bm_handle, 0)) / bm_w(frame_bm_handle, 0), frame_bm_handle);
		}
	};

	draw_frame(bm_handle);
	if (blend_frame)
	{
		rend_SetAlphaValue(VisEffectClampAlpha(base_alpha * frame1_weight));
		draw_frame(blend_bm_handle);
	}

	rend_SetSoftParticleState(0);
	rend_SetAOSuppression(0.0f);
	rend_SetZBias(0.0f);
	rend_SetZBufferWriteMask(1);

	rend_SetWrapType(WT_WRAP);
}

void VisEffectSetDeadFlag(vis_effect* vis)
{
	if (vis->flags & VF_DEAD)
		return;
	if (vis->type == VIS_NONE)
		return;

	vis->flags |= VF_DEAD;

	VisDeadList[NumVisDead++] = vis - VisEffects;
}

// Moves 
void VisEffectMoveOne(vis_effect* vis)
{
	if (vis->flags & VF_USES_LIFELEFT)
		vis->lifeleft -= Frametime;		//...inevitable countdown towards death

	// Chris, do your stuff here
	if (vis->movement_type == MT_PHYSICS)
		do_vis_physics_sim(vis);

	if (vis->flags & VF_USES_LIFELEFT)
	{
		if (vis->lifeleft < 0)
			VisEffectSetDeadFlag(vis);
	}

	if (vis->id == SNOWFLAKE_INDEX)
	{
		const bool enhanced = (vis->flags & VF_ENHANCED_SNOW) != 0;
		if (enhanced != Render_enhanced_weather)
		{
			VisEffectSetDeadFlag(vis);
			return;
		}

		Weather.snowflakes_to_create++;
		if (enhanced)
		{
			if (!(vis->custom_handle & 4))
			{
				const float time_live = Gametime - vis->creation_time;
				const float phase = vis->mass + time_live * vis->drag;
				const float flutter_speed = sinf(phase) * (2.0f + vis->size * 3.5f);
				vector frame_velocity = vis->velocity;
				frame_velocity.x += vis->end_pos.x * flutter_speed;
				frame_velocity.z += vis->end_pos.z * flutter_speed;
				frame_velocity.y += cosf(phase * 0.71f) * 0.8f;
				vis->pos += frame_velocity * Frametime;

				if (vis->pos.y - vis->end_pos.y < 24.0f)
				{
					vector ground_normal;
					const float ground_y = GetTerrainGroundPoint(&vis->pos, &ground_normal);
					vis->end_pos.y = ground_y;
					if (vis->pos.y <= ground_y + 0.12f)
					{
						vis->pos.y = ground_y + 0.04f;
						vis->velocity = ground_normal;
						vis->custom_handle |= 4;
						if (vis->lifeleft > 0.32f)
							vis->lifeleft = 0.32f;
					}
				}
			}

			if (Viewer_object)
			{
				const vector delta = vis->pos - Viewer_object->pos;
				const float horizontal_distance_squared =
					delta.x * delta.x + delta.z * delta.z;
				vector view_forward = Viewer_object->orient.fvec;
				view_forward.y = 0.0f;
				vm_NormalizeVector(&view_forward);
				const float forward_distance =
					delta.x * view_forward.x + delta.z * view_forward.z;
				if (horizontal_distance_squared > 390.0f * 390.0f ||
					delta.y < -150.0f || delta.y > 195.0f || forward_distance < -90.0f)
				{
					VisEffectSetDeadFlag(vis);
				}
			}
		}
		else
		{
			vis->pos += vis->velocity * Frametime;
			if (vis->pos.y < 1)
				VisEffectSetDeadFlag(vis);
		}
	}

	// Do attached viseffect stuff here
	if (vis->flags & VF_ATTACHED)
	{
		int objnum = vis->attach_info.obj_handle & HANDLE_OBJNUM_MASK;
		uint sig = vis->attach_info.obj_handle & HANDLE_COUNT_MASK;
		object* obj = &Objects[objnum];

		if ((obj->flags & OF_DEAD) || (obj->handle & HANDLE_COUNT_MASK) != sig)
		{
			// The object we're attached to doesn't exist anymore
			VisEffectSetDeadFlag(vis);

		}
		else if (obj->type == OBJ_PLAYER && (Players[obj->id].flags & (PLAYER_FLAGS_DYING | PLAYER_FLAGS_DEAD)))
		{
			// The object we're attached to doesn't exist anymore
			VisEffectSetDeadFlag(vis);
		}
		else
		{
			if (vis->id == THICK_LIGHTNING_INDEX)
			{
				if (vis->flags & VF_PLANAR)
				{
					// Do object to object attachment
					int dest_objnum = vis->attach_info.dest_objhandle & HANDLE_OBJNUM_MASK;
					uint dest_sig = vis->attach_info.dest_objhandle & HANDLE_COUNT_MASK;
					object* dest_obj = &Objects[dest_objnum];

					if ((dest_obj->flags & OF_DEAD) || (dest_obj->handle & HANDLE_COUNT_MASK) != dest_sig)
					{
						// The object we're attached to doesn't exist anymore
						VisEffectSetDeadFlag(vis);
						return;
					}

					if (dest_obj->type == OBJ_PLAYER && (Players[dest_obj->id].flags & (PLAYER_FLAGS_DYING | PLAYER_FLAGS_DEAD)))
					{
						VisEffectSetDeadFlag(vis);
						return;
					}

					vis->pos = obj->pos;
					vis->end_pos = dest_obj->pos;

					// If were are shooting to the exact center of the viewer object then move
					// the positions a little bit or it will look wrong:
					if (obj == Viewer_object)
					{
						vis->pos -= (obj->orient.uvec * .1f);
					}

					if (dest_obj == Viewer_object)
					{
						vis->end_pos -= (dest_obj->orient.uvec * .1f);
					}

				}
				else
				{
					if (obj->rtype.pobj_info.model_num != vis->attach_info.modelnum)
					{
						VisEffectSetDeadFlag(vis);
					}
					else
					{
						WeaponCalcGun(&vis->pos, NULL, obj, vis->attach_info.vertnum);
						WeaponCalcGun(&vis->end_pos, NULL, obj, vis->attach_info.end_vertnum);
					}
				}
			}
			else
			{
				float normalized_time[MAX_SUBOBJECTS];

				if (obj->lowest_attached_vis == -1)
				{
					obj->lowest_attached_vis = vis - VisEffects;
					poly_model* pm = &Poly_models[obj->rtype.pobj_info.model_num];

					int i;

					SetNormalizedTimeObj(obj, normalized_time);
					SetModelAngles(pm, normalized_time);
					SetModelInterpPos(pm, normalized_time);


					for (i = vis - VisEffects; i <= Highest_vis_effect_index; i++)
					{
						vis_effect* this_vis = &VisEffects[i];
						if (this_vis->type != VIS_NONE && (this_vis->flags & VF_ATTACHED) && ((this_vis->attach_info.obj_handle & HANDLE_OBJNUM_MASK) == objnum) && this_vis->id != THICK_LIGHTNING_INDEX)
						{
							bsp_info* sm = &pm->submodel[this_vis->attach_info.subnum];
							GetPolyModelPointInWorld(&this_vis->pos, pm, &obj->pos, &obj->orient, this_vis->attach_info.subnum, normalized_time, &sm->verts[this_vis->attach_info.vertnum], NULL);

							if (this_vis->attach_info.end_vertnum != -1)
								GetPolyModelPointInWorld(&this_vis->end_pos, pm, &obj->pos, &obj->orient, this_vis->attach_info.subnum, normalized_time, &sm->verts[this_vis->attach_info.end_vertnum], NULL);
						}
					}
				}
			}

			// Relink if need be
			if (obj->roomnum != vis->roomnum)
				VisEffectRelink(vis - VisEffects, obj->roomnum);

		}
	}

	// Do vis effect explosion lighting
	if (Detail_settings.Dynamic_lighting && vis->lighting_color != 0 && (vis->lighting_color & OPAQUE_FLAG) && (vis->flags & VF_USES_LIFELEFT) && !(vis->flags & VF_DEAD))
	{
		float scalar;

		scalar = (vis->lifetime - vis->lifeleft) / vis->lifetime;

		if (scalar > .5)
		{
			scalar -= .5;

			scalar = 1.0 - (scalar / .5);
		}
		else
		{
			scalar *= 2;
		}

		if (scalar > .05)
		{
			int color = GR_16_TO_COLOR(vis->lighting_color);
			float r = (GR_COLOR_RED(color)) / 255.0;
			float g = (GR_COLOR_GREEN(color)) / 255.0;
			float b = (GR_COLOR_BLUE(color)) / 255.0;


			if (ROOMNUM_OUTSIDE(vis->roomnum))
			{
				int cellnum = CELLNUM(vis->roomnum);

				if (cellnum >= 0 && cellnum < TERRAIN_WIDTH * TERRAIN_DEPTH)
					ApplyLightingToTerrain(&vis->pos, cellnum, vis->size * scalar * 3, r, g, b);
				else
					mprintf((0, "Vis effect not in world!\n"));
			}
			else
			{
				if (vis->roomnum >= 0 && vis->roomnum <= Highest_room_index && Rooms[vis->roomnum].used)
					ApplyLightingToRooms(&vis->pos, vis->roomnum, vis->size * scalar * 3, r, g, b);
			}
		}

	}

	// Link this effect to the viewer if needed
	if (vis->flags & VF_LINK_TO_VIEWER)
	{
		if (vis->roomnum != Viewer_object->roomnum)
		{
			VisEffectRelink(vis - VisEffects, Viewer_object->roomnum);
		}
	}

}

// Moves our visuals
void VisEffectMoveAll()
{
	int i;

	for (i = 0; i <= Highest_vis_effect_index; i++)
	{
		if (VisEffects[i].type != VIS_NONE)
			VisEffectMoveOne(&VisEffects[i]);
	}

}

/*
// Attaches viseffects that move with an object
void AttachRandomVisEffectsToObject (int num,int handle,object *obj)
{
	int i;
	vector zero_pos={0,0,0};
	poly_model *pm = &Poly_models[obj->rtype.pobj_info.model_num];



	for (i=0;i<num;i++)
	{

		int visnum=VisEffectCreate(VIS_FIREBALL,NAPALM_BALL_INDEX,obj->roomnum,&zero_pos);
		if (visnum>=0)
		{
			vis_effect *vis=&VisEffects[visnum];
			vis->size=1.0;
			vis->lifetime=15.0;
			vis->lifeleft=15.0;
			vis->flags |=VF_ATTACHED;
			vis->attach_info.obj_handle=obj->handle;

			int subnum=ps_rand()%pm->n_models;
			bsp_info *sm=&pm->submodel[subnum];

			vis->attach_info.subnum=subnum;
			vis->attach_info.vertnum=ps_rand()%sm->nverts;

		}
	}
}*/

// Attaches viseffects that move with an object
void AttachRandomNapalmEffectsToObject(object* obj)
{
	if (obj->flags & OF_DEAD)
		return;
	float effect_ages[8] = {};
	const int effect_events = Get60HzVisualEventAges(OBJNUM(obj), obj->handle,
		VIS60_ATTACHED_NAPALM, effect_ages, 8);
	if (effect_events == 0)
		return;

	vector velocity_norm = obj->mtype.phys_info.velocity;
	vm_NormalizeVector(&velocity_norm);
	vector pos = obj->pos - (velocity_norm * (obj->size / 2));

	// Napalm effects here are attached to the burning object. Only napalm attached
	// to the local player belongs to the close-screen late pass.
	const bool close_screen_napalm = VisEffectIsLocalPlayerAttachedSourceObject(obj);
	float size_scalar = obj->size / 7.0;

	size_scalar = std::max(1.0f, size_scalar);
	size_scalar = std::min(4.0f, size_scalar);

	for (int event = 0; event < effect_events; ++event)
	{
		if ((obj->movement_type == MT_PHYSICS && OBJECT_OUTSIDE(obj) &&
			(ps_rand() % 3) == 0) || (ps_rand() % 3) == 0)
		{
			int smoke_visnum = CreateFireball(&pos, BLACK_SMOKE_INDEX, obj->roomnum, VISUAL_FIREBALL);
			if (smoke_visnum >= 0)
			{
				VisEffects[smoke_visnum].creation_time -= effect_ages[event];
				VisEffects[smoke_visnum].lifeleft -= effect_ages[event];
				if (close_screen_napalm)
					VisEffects[smoke_visnum].flags |= VF_CLOSE_SCREEN_EFFECT;
			}
		}

		// Create an explosion that follows every now and then.
		if ((ps_rand() % 3) != 0)
			continue;
		if (!(obj->flags & OF_POLYGON_OBJECT))
			return;

		int num = 1;

		num += (obj->size / 15);

		for (int i = 0; i < num; i++)
		{
			vector dest;
			poly_model* pm = &Poly_models[obj->rtype.pobj_info.model_num];

			if (pm->n_models == 0)
				return;

			int subnum = ps_rand() % pm->n_models;

			if (IsNonRenderableSubmodel(pm, subnum))
				continue;

			bsp_info* sm = &pm->submodel[subnum];

			if (sm->nverts == 0)
				return;

			int vertnum = ps_rand() % sm->nverts;

			GetPolyModelPointInWorld(&dest, &Poly_models[obj->rtype.pobj_info.model_num], &obj->pos, &obj->orient, subnum, &sm->verts[vertnum]);
			int visnum = VisEffectCreate(VIS_FIREBALL, GetRandomSmallExplosion(), obj->roomnum, &dest);
			if (visnum == -1)
				return;

			if (close_screen_napalm)
				VisEffects[visnum].flags |= VF_CLOSE_SCREEN_EFFECT;

			VisEffects[visnum].creation_time -= effect_ages[event];
			VisEffects[visnum].lifeleft -= effect_ages[event];
			VisEffects[visnum].size += ((ps_rand() % 20) / 20.0) * 1.0;

			VisEffects[visnum].size *= size_scalar;

			if ((ps_rand() % 2))
			{
				if (obj->movement_type == MT_PHYSICS)
				{
					VisEffects[visnum].movement_type = MT_PHYSICS;
					VisEffects[visnum].velocity = obj->mtype.phys_info.velocity;
					VisEffects[visnum].mass = obj->mtype.phys_info.mass;
					VisEffects[visnum].drag = obj->mtype.phys_info.drag;
					VisEffects[visnum].pos += VisEffects[visnum].velocity * effect_ages[event];
				}
			}
		}
	}
}
