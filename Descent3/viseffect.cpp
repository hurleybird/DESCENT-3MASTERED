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
#include "PHYSICS.H"
#include "weapon.h"
#include "lighting.h"
#include "dedicated_server.h"
#include "player.h"
#include "config.h"
#include "weather.h"
#include "polymodel.h"
#include "renderer.h"
#include "psrand.h"
#include "mem.h"

vis_effect* VisEffects = NULL;
static short* Vis_free_list = NULL;
ushort* VisDeadList = NULL;

ushort max_vis_effects = 0;

int NumVisDead = 0;
int Highest_vis_effect_index = 0;
int Num_vis_effects = 0;

void ShutdownVisEffects()
{
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
void CreateRandomParticles(int num_sparks, vector* pos, int roomnum, int bm_handle, float size, float life)
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
			vis->size = size + (((ps_rand() % 11) - 5) * tenth_size);
			vis->flags |= VF_USES_LIFELEFT;
			float lifetime = life + (((ps_rand() % 11) - 5) * tenth_life);
			vis->lifeleft = lifetime;
			vis->lifetime = lifetime;
			vis->custom_handle = bm_handle;
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

	int visnum = vis - VisEffects;
	int bm_handle;
	fireball* fb = &Fireballs[vis->id];

	norm_time = time_live / vis->lifetime;

	if (norm_time >= 1)
		norm_time = .99999f;		// don't go over!

	//size*=(1-(norm_time/2));

	bm_handle = fb->bm_handle;

	float val;

	val = 1.0 - (norm_time);

	rend_SetAlphaValue(val * .6 * 255);
	rend_SetOverlayType(OT_NONE);
	//rend_SetWrapType (WT_CLAMP);
	rend_SetLighting(LS_NONE);
	//rend_SetZBufferState (0);
	rend_SetAlphaType(AT_SATURATE_TEXTURE);

	ddgr_color color = GR_16_TO_COLOR(vis->lighting_color);
	g3_DrawBitmap(&vis->pos, size, (size * bm_h(bm_handle, 0)) / bm_w(bm_handle, 0), bm_handle, color);

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

	cur_sin += (FrameCount * 2000);

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

	bool Equals(const VisFireballBatchKey& other) const
	{
		return bitmap_handle == other.bitmap_handle &&
			alpha_type == other.alpha_type;
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

struct VisFireballAtlasFrame
{
	int source_handle;
	int width;
	int height;
	float u0;
	float v0;
	float u1;
	float v1;
};

struct VisFireballVClipAtlas
{
	int vclip_handle;
	int atlas_handle;
	bool valid;
	bool failed;
	std::vector<VisFireballAtlasFrame> frames;
};

static bool VisFireball_batch_valid = false;
static VisFireballBatchKey VisFireball_batch_key = {};
static std::vector<VisFireballBatchItem> VisFireball_batch_items;
static std::vector<VisFireballVClipAtlas> VisFireball_vclip_atlases;
static const bool VIS_FIREBALL_BARRIER_FLUSHES_ENABLED = false;
static const bool VIS_FIREBALL_BATCH_DEBUG_TINT = false;
static const int VIS_FIREBALL_BATCH_DEBUG_TINT_COLOR = GR_RGB(0, 255, 96);
static const int VIS_FIREBALL_ATLAS_PADDING = 1;
static const int VIS_FIREBALL_ATLAS_MAX_DIMENSION = 2048;

static vis_fireball_batch_debug_stats VisFireball_batch_debug_stats = {};
static int VisFireball_batch_debug_frame = -1;

static void VisEffectResetBatchDebugStatsForFrame()
{
	if (VisFireball_batch_debug_frame == FrameCount)
		return;

	VisFireball_batch_debug_stats = {};
	VisFireball_batch_debug_frame = FrameCount;
}

static bool VisEffectHasQueuedBatch()
{
	return VisFireball_batch_valid && !VisFireball_batch_items.empty();
}

void VisEffectGetBatchDebugStats(vis_fireball_batch_debug_stats* stats)
{
	if (!stats)
		return;

	VisEffectResetBatchDebugStatsForFrame();
	*stats = VisFireball_batch_debug_stats;
}

static int VisEffectNextPowerOfTwo(int value)
{
	int power = 1;
	while (power < value)
		power <<= 1;
	return power;
}

static VisFireballVClipAtlas* VisEffectFindVClipAtlas(int vclip_handle)
{
	for (size_t i = 0; i < VisFireball_vclip_atlases.size(); i++)
	{
		if (VisFireball_vclip_atlases[i].vclip_handle == vclip_handle)
			return &VisFireball_vclip_atlases[i];
	}

	return NULL;
}

static bool VisEffectAtlasMatchesVClip(const VisFireballVClipAtlas& atlas, int vclip_handle)
{
	if (vclip_handle < 0 || vclip_handle >= MAX_VCLIPS)
		return false;

	vclip* vc = &GameVClips[vclip_handle];
	if (!vc->used || !vc->frames || vc->num_frames <= 0 ||
		vc->num_frames != (int)atlas.frames.size())
		return false;

	for (int i = 0; i < vc->num_frames; i++)
	{
		if (atlas.frames[i].source_handle != vc->frames[i])
			return false;
	}

	return true;
}

static void VisEffectFreeVClipAtlas(VisFireballVClipAtlas& atlas)
{
	if (atlas.atlas_handle > BAD_BITMAP_HANDLE && GameBitmaps[atlas.atlas_handle].used)
		bm_FreeBitmap(atlas.atlas_handle);

	atlas.atlas_handle = BAD_BITMAP_HANDLE;
	atlas.valid = false;
	atlas.failed = true;
	atlas.frames.clear();
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

static bool VisEffectBuildVClipAtlas(int vclip_handle, VisFireballVClipAtlas& atlas)
{
	atlas.vclip_handle = vclip_handle;
	atlas.atlas_handle = BAD_BITMAP_HANDLE;
	atlas.valid = false;
	atlas.failed = true;
	atlas.frames.clear();

	if (vclip_handle < 0 || vclip_handle >= MAX_VCLIPS)
		return false;

	vclip* vc = &GameVClips[vclip_handle];
	if (!vc->used || !vc->frames || vc->num_frames <= 0)
		return false;

	int max_width = 0;
	int max_height = 0;
	int bitmap_format = BITMAP_FORMAT_STANDARD;
	for (int i = 0; i < vc->num_frames; i++)
	{
		const int frame_handle = vc->frames[i];
		if (frame_handle <= BAD_BITMAP_HANDLE || !GameBitmaps[frame_handle].used)
			return false;

		if (i == 0)
			bitmap_format = GameBitmaps[frame_handle].format;
		else if (GameBitmaps[frame_handle].format != bitmap_format)
			return false;

		const int width = bm_w(frame_handle, 0);
		const int height = bm_h(frame_handle, 0);
		if (width <= 0 || height <= 0)
			return false;

		max_width = std::max(max_width, width);
		max_height = std::max(max_height, height);
	}

	const int cell_width = max_width + (VIS_FIREBALL_ATLAS_PADDING * 2);
	const int cell_height = max_height + (VIS_FIREBALL_ATLAS_PADDING * 2);
	int columns = 1;
	while (columns * columns < vc->num_frames)
		columns++;

	const int rows = (vc->num_frames + columns - 1) / columns;
	const int atlas_width = VisEffectNextPowerOfTwo(columns * cell_width);
	const int atlas_height = VisEffectNextPowerOfTwo(rows * cell_height);

	if (atlas_width > VIS_FIREBALL_ATLAS_MAX_DIMENSION || atlas_height > VIS_FIREBALL_ATLAS_MAX_DIMENSION)
		return false;

	const int atlas_handle = bm_AllocBitmap(atlas_width, atlas_height, 0);
	if (atlas_handle <= BAD_BITMAP_HANDLE)
		return false;

	GameBitmaps[atlas_handle].flags |= BF_TRANSPARENT | BF_CHANGED;
	GameBitmaps[atlas_handle].format = bitmap_format;
	snprintf(GameBitmaps[atlas_handle].name, BITMAP_NAME_LEN, "vfxatlas%d", vclip_handle);

	ushort* atlas_data = bm_data(atlas_handle, 0);
	if (!atlas_data)
	{
		bm_FreeBitmap(atlas_handle);
		return false;
	}

	for (int i = 0; i < atlas_width * atlas_height; i++)
		atlas_data[i] = NEW_TRANSPARENT_COLOR;

	atlas.frames.resize(vc->num_frames);
	for (int i = 0; i < vc->num_frames; i++)
	{
		const int frame_handle = vc->frames[i];
		const int frame_width = bm_w(frame_handle, 0);
		const int frame_height = bm_h(frame_handle, 0);
		const int cell_x = (i % columns) * cell_width;
		const int cell_y = (i / columns) * cell_height;

		VisEffectCopyAtlasFrame(atlas_handle, cell_x, cell_y, frame_handle);

		VisFireballAtlasFrame& frame = atlas.frames[i];
		frame.source_handle = frame_handle;
		frame.width = frame_width;
		frame.height = frame_height;
		frame.u0 = (float)(cell_x + VIS_FIREBALL_ATLAS_PADDING) / (float)atlas_width;
		frame.v0 = (float)(cell_y + VIS_FIREBALL_ATLAS_PADDING) / (float)atlas_height;
		frame.u1 = (float)(cell_x + VIS_FIREBALL_ATLAS_PADDING + frame_width) / (float)atlas_width;
		frame.v1 = (float)(cell_y + VIS_FIREBALL_ATLAS_PADDING + frame_height) / (float)atlas_height;
	}

	atlas.atlas_handle = atlas_handle;
	atlas.valid = true;
	atlas.failed = false;
	return true;
}

static bool VisEffectGetVClipAtlasFrame(int vclip_handle, int frame_index, int* bitmap_handle,
	int* width, int* height, float* u0, float* v0, float* u1, float* v1)
{
	VisFireballVClipAtlas* atlas = VisEffectFindVClipAtlas(vclip_handle);
	if (!atlas)
	{
		VisFireballVClipAtlas new_atlas;
		VisEffectBuildVClipAtlas(vclip_handle, new_atlas);
		VisFireball_vclip_atlases.push_back(new_atlas);
		atlas = &VisFireball_vclip_atlases.back();
	}
	else if (!VisEffectAtlasMatchesVClip(*atlas, vclip_handle))
	{
		VisEffectFreeVClipAtlas(*atlas);
		VisEffectBuildVClipAtlas(vclip_handle, *atlas);
	}

	if (!atlas->valid || atlas->failed || frame_index < 0 || frame_index >= (int)atlas->frames.size())
		return false;

	const VisFireballAtlasFrame& frame = atlas->frames[frame_index];
	*bitmap_handle = atlas->atlas_handle;
	*width = frame.width;
	*height = frame.height;
	*u0 = frame.u0;
	*v0 = frame.v0;
	*u1 = frame.u1;
	*v1 = frame.v1;
	return true;
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

	if (VisEffectGetVClipAtlasFrame(vclip_handle, frame_index, bitmap_handle, width, height, u0, v0, u1, v1))
	{
		VisEffectResetBatchDebugStatsForFrame();
		VisFireball_batch_debug_stats.atlas_hits++;
		return;
	}

	VisEffectResetBatchDebugStatsForFrame();
	VisFireball_batch_debug_stats.atlas_fallbacks++;

	*bitmap_handle = vc->frames[frame_index];
	*width = bm_w(*bitmap_handle, 0);
	*height = bm_h(*bitmap_handle, 0);
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

static bool VisEffectClipAndProjectBatchItem(VisFireballBatchItem& item, float z_bias)
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

static bool VisEffectBuildFireballBatchItem(vis_effect* vis, VisFireballBatchKey& key,
	VisFireballBatchItem& item)
{
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
		rot_angle = ((visnum * 5000) + (FrameCount * 160)) % 65536;
	else if (vis->flags & VF_ATTACHED)
		rot_angle = 0;
	else if (vis->id == RUBBLE1_INDEX || vis->id == RUBBLE2_INDEX)
		rot_angle = ((visnum * 5000) + (FrameCount * 860)) % 65536;
	else if (vis->id == SUN_CORONA_INDEX)
	{
		rot_angle = ((visnum * 5000) + (FrameCount * 500)) % 65536;
		size *= 1.0f + ((rand() % 10) / 100.0f);
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

	int bm_handle;
	int bitmap_width = 0;
	int bitmap_height = 0;
	float u0 = 0.0f;
	float v0 = 0.0f;
	float u1 = 1.0f;
	float v1 = 1.0f;
	if (vis->id == SMOKE_TRAIL_INDEX)
	{
		int texnum = vis->custom_handle;
		if (GameTextures[texnum].flags & TF_ANIMATED)
		{
			int vnum = GameTextures[texnum].bm_handle;
			vclip* vc = &GameVClips[vnum];
			int int_frame = vc->num_frames * norm_time;
			VisEffectSelectVClipBitmap(vnum, int_frame, &bm_handle, &bitmap_width, &bitmap_height,
				&u0, &v0, &u1, &v1);
		}
		else
			bm_handle = GameTextures[texnum].bm_handle;
	}
	else if (vis->id == SPRAY_INDEX)
	{
		int vnum = vis->custom_handle;
		vclip* vc = &GameVClips[vnum];
		int int_frame = vc->num_frames * norm_time;
		VisEffectSelectVClipBitmap(vnum, int_frame, &bm_handle, &bitmap_width, &bitmap_height,
			&u0, &v0, &u1, &v1);
	}
	else if (vis->id == CUSTOM_EXPLOSION_INDEX || vis->id == PARTICLE_INDEX)
	{
		if (GameTextures[vis->custom_handle].flags & TF_ANIMATED)
		{
			int vnum = GameTextures[vis->custom_handle].bm_handle;
			vclip* vc = &GameVClips[vnum];
			int int_frame = vc->num_frames * norm_time;
			VisEffectSelectVClipBitmap(vnum, int_frame, &bm_handle, &bitmap_width, &bitmap_height,
				&u0, &v0, &u1, &v1);
		}
		else
			bm_handle = GetTextureBitmap(vis->custom_handle, 0);
	}
	else if (fb->type == FT_SPARK)
	{
		bm_handle = fb->bm_handle;
		size *= (1.0f - norm_time);
	}
	else if (vis->id == SUN_CORONA_INDEX || vis->id == MUZZLE_FLASH_INDEX ||
		vis->id == RUBBLE1_INDEX || vis->id == RUBBLE2_INDEX)
	{
		bm_handle = fb->bm_handle;
	}
	else
	{
		int vnum = fb->bm_handle;
		vclip* vc = &GameVClips[vnum];
		int int_frame = vc->num_frames * norm_time;
		VisEffectSelectVClipBitmap(vnum, int_frame, &bm_handle, &bitmap_width, &bitmap_height,
			&u0, &v0, &u1, &v1);
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

	float vertex_red = 1.0f;
	float vertex_green = 1.0f;
	float vertex_blue = 1.0f;

	if (vis->id == RUBBLE1_INDEX || vis->id == RUBBLE2_INDEX || vis->id == GRAY_SPARK_INDEX)
	{
		VisEffectColorToFloat(GR_16_TO_COLOR(vis->lighting_color), &vertex_red, &vertex_green,
			&vertex_blue);
	}
	else if (VIS_FIREBALL_BATCH_DEBUG_TINT)
	{
		VisEffectColorToFloat(VIS_FIREBALL_BATCH_DEBUG_TINT_COLOR, &vertex_red, &vertex_green,
			&vertex_blue);
	}

	const float width = size;
	const float height = (size * bitmap_height) / bitmap_width;
	const float z_bias = (vis->flags & VF_NO_Z_ADJUST) ? 0.0f : -size;
	bool built;

	if (vis->flags & VF_PLANAR)
		built = VisEffectBuildPlanarBatchItem(item, &vis->pos, &vis->end_pos, rot_angle, width, height);
	else
		built = VisEffectBuildRotatedBatchItem(item, &vis->pos, rot_angle, width, height);

	if (built)
	{
		VisEffectApplyBatchUVs(item, u0, v0, u1, v1);
		VisEffectApplyBatchVertexColor(item, vertex_red, vertex_green, vertex_blue, vertex_alpha);
		built = VisEffectClipAndProjectBatchItem(item, z_bias);
	}

	return built;
}

static void FlushVisEffectBatchesNow()
{
	if (!VisEffectHasQueuedBatch())
	{
		return;
	}

	VisEffectResetBatchDebugStatsForFrame();
	const unsigned item_count = (unsigned)VisFireball_batch_items.size();
	VisFireball_batch_debug_stats.flushes++;
	VisFireball_batch_debug_stats.flushed_items += item_count;
	if (item_count > VisFireball_batch_debug_stats.max_batch_items)
		VisFireball_batch_debug_stats.max_batch_items = item_count;
	if (item_count == 1)
		VisFireball_batch_debug_stats.single_item_flushes++;

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
	rend_SetSoftParticleState(Render_soft_vis_effects ? 1 : 0);

	std::vector<renderer_poly_batch_item> renderer_items(VisFireball_batch_items.size());
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

void FlushVisEffectBatches()
{
	if (VIS_FIREBALL_BARRIER_FLUSHES_ENABLED)
		FlushVisEffectBatchesNow();
}

void ForceFlushVisEffectBatches()
{
	if (VisEffectHasQueuedBatch())
	{
		VisEffectResetBatchDebugStatsForFrame();
		VisFireball_batch_debug_stats.forced_flushes++;
	}

	FlushVisEffectBatchesNow();
}

static void QueueVisEffectBatchItem(const VisFireballBatchKey& key, const VisFireballBatchItem& item)
{
	if (VisFireball_batch_valid && !VisFireball_batch_key.Equals(key))
	{
		VisEffectResetBatchDebugStatsForFrame();
		VisFireball_batch_debug_stats.key_flushes++;
		FlushVisEffectBatchesNow();
	}

	if (!VisFireball_batch_valid)
	{
		VisFireball_batch_key = key;
		VisFireball_batch_valid = true;
	}

	VisFireball_batch_items.push_back(item);
	VisFireball_batch_debug_stats.queued++;
}

void DrawVisEffectMaybeBatched(vis_effect* vis)
{
	if (!Render_batched_vis_effects)
	{
		DrawVisEffect(vis);
		return;
	}

	VisEffectResetBatchDebugStatsForFrame();
	VisFireball_batch_debug_stats.attempts++;

	VisFireballBatchKey key = {};
	VisFireballBatchItem item = {};
	if (!VisEffectBuildFireballBatchItem(vis, key, item))
	{
		VisFireball_batch_debug_stats.rejected++;
		if (VisEffectHasQueuedBatch())
			VisFireball_batch_debug_stats.fallback_flushes++;

		FlushVisEffectBatchesNow();
		DrawVisEffect(vis);
		return;
	}

	VisFireball_batch_debug_stats.accepted++;
	QueueVisEffectBatchItem(key, item);
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

	g3_DrawPoly(4, pntlist, bm_handle);

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

	fireball* fb = &Fireballs[vis->id];

	if (fb->type == FT_BILLOW)
		rot_angle = ((visnum * 5000) + (FrameCount * 160)) % 65536;
	else if (vis->flags & VF_ATTACHED)
		rot_angle = 0;
	else if (vis->id == RUBBLE1_INDEX || vis->id == RUBBLE2_INDEX)
		rot_angle = ((visnum * 5000) + (FrameCount * 860)) % 65536;
	else if (vis->id == SUN_CORONA_INDEX)
	{
		rot_angle = ((visnum * 5000) + (FrameCount * 500)) % 65536;
		size *= 1.0 + ((rand() % 10) / 100);
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

	if (vis->id == SMOKE_TRAIL_INDEX) // If its a smoke trail, get image from texture
	{
		int texnum = vis->custom_handle;
		if (GameTextures[texnum].flags & TF_ANIMATED)
		{
			vclip* vc = &GameVClips[GameTextures[texnum].bm_handle];
			int int_frame = vc->num_frames * norm_time;
			bm_handle = vc->frames[int_frame];
		}
		else
			bm_handle = GameTextures[texnum].bm_handle;
	}
	else if (vis->id == SPRAY_INDEX)
	{
		int vnum = vis->custom_handle;
		vclip* vc = &GameVClips[vnum];
		int int_frame = vc->num_frames * norm_time;
		bm_handle = vc->frames[int_frame];

		//	if (norm_time<.5)
				//size=1+((vis->size-1)*(norm_time));
	}
	else if (vis->id == CUSTOM_EXPLOSION_INDEX || vis->id == PARTICLE_INDEX)	// Do custom
	{
		if ((GameTextures[vis->custom_handle].flags & TF_ANIMATED))
		{
			int vnum = GameTextures[vis->custom_handle].bm_handle;
			vclip* vc = &GameVClips[vnum];
			int int_frame = vc->num_frames * norm_time;
			bm_handle = vc->frames[int_frame];
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
		vclip* vc = &GameVClips[fb->bm_handle];
		int int_frame = vc->num_frames * norm_time;
		bm_handle = vc->frames[int_frame];
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

	if (vis->id == SMOKE_TRAIL_INDEX || vis->id == CUSTOM_EXPLOSION_INDEX || vis->id == PARTICLE_INDEX)
		rend_SetAlphaValue(val * GameTextures[vis->custom_handle].alpha * 255);
	else if (fb->type == FT_SMOKE)
		rend_SetAlphaValue(val * SMOKE_ALPHA * 255);
	else if (vis->id == RUBBLE1_INDEX || vis->id == RUBBLE2_INDEX)
		rend_SetAlphaValue(255);
	else if (vis->id == MUZZLE_FLASH_INDEX)
		rend_SetAlphaValue(128);
	else if (vis->flags & VF_ATTACHED)
		rend_SetAlphaValue(FIREBALL_ALPHA * 255);
	else
		rend_SetAlphaValue(val * FIREBALL_ALPHA * 255);

	rend_SetOverlayType(OT_NONE);

	if (!(vis->flags & VF_NO_Z_ADJUST))
		rend_SetZBias(-size);

	rend_SetZBufferWriteMask(0);
	rend_SetWrapType(WT_CLAMP);
	rend_SetLighting(LS_NONE);
	rend_SetAOSuppression(1.0f);
	rend_SetSoftParticleState(Render_soft_vis_effects ? 1 : 0);

	// Draw!!
	if (vis->id == RUBBLE1_INDEX || vis->id == RUBBLE2_INDEX || vis->id == GRAY_SPARK_INDEX)
	{
		int color = GR_16_TO_COLOR(vis->lighting_color);
		g3_DrawRotatedBitmap(&vis->pos, rot_angle, size, (size * bm_h(bm_handle, 0)) / bm_w(bm_handle, 0), bm_handle, color);
	}
	else
	{
		if (vis->flags & VF_PLANAR)
			g3_DrawPlanarRotatedBitmap(&vis->pos, &vis->end_pos, rot_angle, size, (size * bm_h(bm_handle, 0)) / bm_w(bm_handle, 0), bm_handle);
		else
			g3_DrawRotatedBitmap(&vis->pos, rot_angle, size, (size * bm_h(bm_handle, 0)) / bm_w(bm_handle, 0), bm_handle);
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
		Weather.snowflakes_to_create++;

		vis->pos += vis->velocity * Frametime;


		if (vis->pos.y < 1)
			VisEffectSetDeadFlag(vis);
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

	vector velocity_norm = obj->mtype.phys_info.velocity;
	vm_NormalizeVector(&velocity_norm);
	vector pos = obj->pos - (velocity_norm * (obj->size / 2));

	if (obj->movement_type == MT_PHYSICS && (OBJECT_OUTSIDE(obj) && (ps_rand() % 3) == 0) || (ps_rand() % 3) == 0)
		CreateFireball(&pos, BLACK_SMOKE_INDEX, obj->roomnum, VISUAL_FIREBALL);

	float size_scalar = obj->size / 7.0;

	size_scalar = std::max(1.0f, size_scalar);
	size_scalar = std::min(4.0f, size_scalar);

	// Create an explosion that follows every now and then
	if ((ps_rand() % 3) == 0)
	{
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
				}
			}
		}
	}
}
