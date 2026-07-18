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

#include "render.h"
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <queue>
#include <thread>
#include <unordered_map>
#include <vector>
#include "descent.h"
#include "3d.h"
#include "mono.h"
#include "gametexture.h"
#include "texture.h"
#include "vclip.h"
#include "program.h"
#include "game.h"
#include "renderobject.h"
#include "door.h"
#include "terrain.h"
#include "renderer.h"
#include "room.h"
#include "lighting.h"
#include "lightmap.h"
#include "limits.h"
#include "lightmap_info.h"
#include "viseffect.h"
#include "weapon.h"
#include "fireball.h"
#include "scorch.h"
#include "findintersection.h"
#include "special_face.h"
#include "BOA.h"
#include "config.h"
#include "gameloop.h"
#include "doorway.h"
#include "TelComAutoMap.h"
#include "postrender.h"
#include "mem.h"
#include "psrand.h"
#include "player.h"
#include "args.h"
#include "newrender.h"
#include "retained_room.h"
#ifdef EDITOR
#include "editor\d3edit.h"
#endif
#include "../renderer/gl_mesh.h"

static int Faces_rendered = 0;

static bool TextureNameContainsNoCase(const char* name, const char* needle)
{
	if (!name || !needle || !needle[0])
		return false;

	size_t needle_len = strlen(needle);
	for (const char* p = name; *p; p++)
	{
		if (!strnicmp(p, needle, needle_len))
			return true;
	}

	return false;
}

static bool TextureLooksLikeMineRock(int tmap)
{
	if (tmap < 0 || tmap >= MAX_TEXTURES || !GameTextures[tmap].used)
		return false;

	if (GameTextures[tmap].flags & TF_RUBBLE)
		return true;

	return TextureNameContainsNoCase(GameTextures[tmap].name, "rock") ||
		TextureNameContainsNoCase(GameTextures[tmap].name, "rubble");
}

static bool RenderObjectHasWeaponStreamer(const object* objp)
{
	return objp && objp->type == OBJ_WEAPON && (Weapons[objp->id].flags & WF_STREAMER);
}

static int RoomFaceAOClass(room* rp, face* fp)
{
	if (!rp || !fp || (rp->flags & RF_EXTERNAL))
		return RENDERER_AO_CLASS_DEFAULT;

	return TextureLooksLikeMineRock(fp->tmap) ?
		RENDERER_AO_CLASS_MINE_ROCK : RENDERER_AO_CLASS_MINE;
}

extern double GetFPS();
extern ubyte Outline_release_mode;
//3d point for each vertex for use during rendering a room
ubyte Room_clips[MAX_VERTS_PER_ROOM];		// used for face culling
//default face reflectivity
float Face_reflectivity = 0.5;
//the position of the viewer - valid while a frame is being rendered
static vector Viewer_eye;
static matrix Viewer_orient;
int Viewer_roomnum;
int Flag_automap, Called_from_terrain;
// Fog zone variables
float Fog_zone_start = FLT_MAX, Fog_zone_end = FLT_MAX;
int Must_render_terrain;
int Global_buffer_index;
int No_render_windows_hack = -1;
constexpr float WALL_PULSE_INCREMENT = .01f;

//Variables for various debugging features
#ifndef _DEBUG
#define In_editor_mode 0
#define Outline_lightmaps 0
#define Outline_alpha 0
#define Render_floating_triggers 0
#define Use_software_zbuffer		0
#define Render_all_external_rooms 0
#define Render_portals 0
#define Render_one_room_only 0
#define Render_inside_only 0
#define Shell_render_flag 0
#else		//ifdef _DEBUG
// If true, draw white outline for each polygon
int Render_portals = 0;
ubyte Outline_mode = 0;
ubyte Shell_render_flag = 0;
bool Outline_alpha = 0;
bool Outline_lightmaps = 0;
bool Render_floating_triggers = 0;
bool Use_software_zbuffer = 0;
bool Lighting_on = 1;
bool Render_all_external_rooms = 0;
bool In_editor_mode = 0;
bool Render_one_room_only = 0;
bool Render_inside_only = 0;
#endif	//ifdef _DEBUG

#ifndef RELEASE
int Mine_depth;
#endif

#ifndef EDITOR
#define Search_lightmaps 0
#else		//ifndef EDITOR
//Vars for find_seg_side_face()
static int Search_lightmaps = 0;		// true if searching for a lightmap
int found_lightmap;
#endif	//ifndef EDITOR

// Prototypes
void RenderRoomObjects(room* rp);

//The current window width & height (valid while rendering)
static int Render_width, Render_height;
int   Clear_window_color = -1;
int   Clear_window = 2;         // 1 = Clear whole background window, 2 = clear view portals into rest of world, 0 = no clear

constexpr int MAX_RENDER_ROOMS = 100;
char Rooms_visited[MAX_ROOMS + MAX_PALETTE_ROOMS];
int Facing_visited[MAX_ROOMS + MAX_PALETTE_ROOMS];
// For keeping track of portal recursion
ubyte Room_depth_list[MAX_ROOMS + MAX_PALETTE_ROOMS];
short Render_list[MAX_RENDER_ROOMS];
short External_room_list[MAX_ROOMS];
int N_external_rooms;

// For rendering specular faces
constexpr int MAX_SPECULAR_FACES = 2500;
short Specular_faces[MAX_SPECULAR_FACES];
int Num_specular_faces_to_render = 0;
int Num_real_specular_faces_to_render = 0;	// Non-invisible specular faces

struct smooth_spec_vert
{
	float r, g, b;
	int used;
};
smooth_spec_vert Smooth_verts[MAX_VERTS_PER_ROOM];

// For scorch rendering
ushort Scorches_to_render[MAX_FACES_PER_ROOM];
int Num_scorches_to_render = 0;

// For rendering volumetric fog
fog_portal_data Fog_portal_data[MAX_FOGGED_ROOMS_PER_FRAME];
int Num_fogged_rooms_this_frame = 0;
float Room_light_val = 0;
int Room_fog_plane_check = 0;
float Room_fog_distance = 0;
float Room_fog_eye_distance = 0;
vector Room_fog_plane, Room_fog_portal_vert;
short Fog_faces[MAX_FACES_PER_ROOM];
bool Fog_face_retained[MAX_FACES_PER_ROOM];
ubyte Fog_face_clip_codes[MAX_FACES_PER_ROOM];
int Num_fog_faces_to_render = 0;
static bool Room_material_fog_active = false;

struct deferred_fog_face
{
	int roomnum;
	short facenum;
	bool retained;
	ubyte clip_codes;
};

static std::vector<deferred_fog_face> Deferred_fog_ao_faces;

constexpr int MAX_EXTERNAL_ROOMS = 100;
vector External_room_corners[MAX_EXTERNAL_ROOMS][8];
ubyte External_room_codes[MAX_EXTERNAL_ROOMS];
ubyte External_room_project_net[MAX_EXTERNAL_ROOMS];

// For light glows
constexpr int MAX_LIGHT_GLOWS = 100;
constexpr int LGF_USED = 1;
constexpr int LGF_INCREASING = 2;
constexpr int LGF_FAST = 4;

struct light_glow
{
	short roomnum;
	short facenum;
	float size;
	vector center;
	float scalar;
	ubyte flags;
};

light_glow LightGlows[MAX_LIGHT_GLOWS];
light_glow LightGlowsThisFrame[MAX_LIGHT_GLOWS];

int FastCoronas = 0;
int Num_glows = 0, Num_glows_this_frame = 0;
// For sorting our textures in state limited environments
state_limited_element State_elements[MAX_STATE_ELEMENTS];

// For terrain portals
int Terrain_portal_left, Terrain_portal_right, Terrain_portal_top, Terrain_portal_bottom;

// For deformation effect
vector Global_alter_vec = { 19,-19,19 };

// For detail stuff (mirrors, specular,etc)
bool Render_mirror_for_room = false;
int Mirror_room;
int Num_mirrored_rooms;
short Mirrored_room_list[MAX_ROOMS];
ubyte Mirrored_room_checked[MAX_ROOMS];
short Mirror_rooms[MAX_ROOMS];
int Num_mirror_rooms = 0;

bool Render_use_newrender = false; //debug

//used during rendering as count of items in render_list[]
int N_render_rooms;
int first_terminal_room;
#define round(a)  (int((a) + 0.5f))

//
//  UTILITY FUNCS
//
//Determines if a face renders
//Parameters:	rp - pointer to room that contains the face
//					fp - pointer to the face in question
static inline bool FaceIsRenderable(room* rp, face* fp)
{
	//Check for a floating trigger, which doesn't get rendered
	if ((fp->flags & FF_FLOATING_TRIG) && (!In_editor_mode || !Render_floating_triggers))
		return 0;
	//Check for face that's part of a portal
	if (fp->portal_num != -1)
	{
		if (rp->portals[fp->portal_num].flags & PF_RENDER_FACES)
			return true;
		if (rp->flags & RF_FOG && !In_editor_mode)
			return true;
		return 0;
	}

	//Nothing special, so face renders
	return true;
}

struct SplitSpecularTexturePair
{
	int base_tmap;
	int spec_tmap;
	int base_bm;
	int spec_bm;
	bool valid;
};

static bool SplitSpecularTexturePathEnabled()
{
	return (Render_split_specular_textures || Render_per_pixel_field_static_specular) &&
		Render_preferred_state.per_pixel_lighting &&
		UseHardware && rend_CanUseNewrender();
}

static SplitSpecularTexturePair ComputeSplitSpecularTexturePair(int tmap)
{
	SplitSpecularTexturePair pair = { -1, -1, -1, -1, false };
	if (tmap < 0 || tmap >= MAX_TEXTURES || !GameTextures[tmap].used)
		return pair;
	if (GameTextures[tmap].flags & (TF_ANIMATED | TF_PROCEDURAL))
		return pair;

	char base_name[PAGENAME_LEN];
	char spec_name[PAGENAME_LEN];
	strncpy(base_name, GameTextures[tmap].name, sizeof(base_name));
	base_name[sizeof(base_name) - 1] = '\0';
	strncpy(spec_name, GameTextures[tmap].name, sizeof(spec_name));
	spec_name[sizeof(spec_name) - 1] = '\0';

	const size_t name_len = strlen(GameTextures[tmap].name);
	if (name_len == 0)
		return pair;

	if (GameTextures[tmap].name[name_len - 1] == 'S' || GameTextures[tmap].name[name_len - 1] == 's')
	{
		base_name[name_len - 1] = '\0';
	}
	else
	{
		if (name_len + 1 >= sizeof(spec_name))
			return pair;
		spec_name[name_len] = 'S';
		spec_name[name_len + 1] = '\0';
	}

	int base_tmap = FindTextureName(base_name);
	int spec_tmap = FindTextureName(spec_name);
	if (base_tmap < 0 || spec_tmap < 0 || base_tmap == spec_tmap)
		return pair;
	if ((GameTextures[base_tmap].flags | GameTextures[spec_tmap].flags) & (TF_ANIMATED | TF_PROCEDURAL))
		return pair;

	int base_bm = GetTextureBitmap(base_tmap, 0);
	int spec_bm = GetTextureBitmap(spec_tmap, 0);
	if (base_bm < 0 || spec_bm < 0)
		return pair;
	if (bm_format(base_bm) != BITMAP_FORMAT_1555 || bm_format(spec_bm) != BITMAP_FORMAT_4444)
		return pair;

	pair.base_tmap = base_tmap;
	pair.spec_tmap = spec_tmap;
	pair.base_bm = base_bm;
	pair.spec_bm = spec_bm;
	pair.valid = true;
	return pair;
}

static SplitSpecularTexturePair Split_specular_cached_pairs[MAX_TEXTURES];
static ubyte Split_specular_cached_pair_valid[MAX_TEXTURES] = {};

static void ResetSplitSpecularTexturePairCache()
{
	memset(Split_specular_cached_pairs, 0, sizeof(Split_specular_cached_pairs));
	memset(Split_specular_cached_pair_valid, 0, sizeof(Split_specular_cached_pair_valid));
}

static SplitSpecularTexturePair FindSplitSpecularTexturePair(int tmap)
{
	SplitSpecularTexturePair empty_pair = { -1, -1, -1, -1, false };
	if (tmap < 0 || tmap >= MAX_TEXTURES)
		return empty_pair;

	if (!Split_specular_cached_pair_valid[tmap])
	{
		Split_specular_cached_pairs[tmap] = ComputeSplitSpecularTexturePair(tmap);
		Split_specular_cached_pair_valid[tmap] = 1;
	}

	return Split_specular_cached_pairs[tmap];
}

static bool FaceCanUseSplitSpecularTextures(face* fp, SplitSpecularTexturePair* out_pair = nullptr)
{
	if (!SplitSpecularTexturePathEnabled())
		return false;
	if ((fp->flags & FF_DESTROYED) && (GameTextures[fp->tmap].flags & TF_DESTROYABLE))
		return false;

	SplitSpecularTexturePair pair = FindSplitSpecularTexturePair(fp->tmap);
	if (!pair.valid)
		return false;

	if (out_pair)
		*out_pair = pair;
	return true;
}

static int BaseTextureTmapForFace(face* fp)
{
	SplitSpecularTexturePair pair;
	if (FaceCanUseSplitSpecularTextures(fp, &pair))
		return pair.base_tmap;
	return fp->tmap;
}

static int SpecularTextureTmapForFace(face* fp)
{
	SplitSpecularTexturePair pair;
	if (FaceCanUseSplitSpecularTextures(fp, &pair))
		return pair.spec_tmap;
	return fp->tmap;
}

static int BaseBitmapHandleForFace(face* fp)
{
	if ((fp->flags & FF_DESTROYED) && (GameTextures[fp->tmap].flags & TF_DESTROYABLE))
		return GetTextureBitmap(GameTextures[fp->tmap].destroy_handle, 0);

	SplitSpecularTexturePair pair;
	if (FaceCanUseSplitSpecularTextures(fp, &pair))
		return pair.base_bm;

	return GetTextureBitmap(fp->tmap, 0);
}

//Determines if a face draws with alpha blending
//Parameters:	fp - pointer to the face in question
//					bm_handle - the handle for the bitmap for this frame, or -1 if don't care about transparence
//Returns:		bitmask describing the alpha blending for the face
//					the return bits are the ATF_ flags in renderer.h
static inline int GetFaceAlpha(face* fp, int bm_handle)
{
	int ret = AT_ALWAYS;
	int alpha_tmap = BaseTextureTmapForFace(fp);
	if (GameTextures[alpha_tmap].flags & TF_SATURATE)
	{
		ret = AT_SATURATE_TEXTURE;
	}
	else
	{
		//Check the face's texture for an alpha value
		if (GameTextures[alpha_tmap].alpha < 1.0)
			ret |= ATF_CONSTANT;

		//Check for transparency
		if (bm_handle >= 0 && GameBitmaps[bm_handle].format != BITMAP_FORMAT_4444 &&
			GameTextures[alpha_tmap].flags & TF_TMAP2)
			ret |= ATF_TEXTURE;
	}
	return ret;
}

//Determine if you should render through a portal
//Parameters:	rp - the room the portal is in
//					pp - the portal we're checking
//Returns:		true if you should render the room to which the portal connects
inline bool RenderPastPortal(room* rp, portal* pp)
{
	//If we don't render the portal's faces, then we see through it
	if (!(pp->flags & PF_RENDER_FACES))
		return 1;

	//Check if the face's texture has transparency
	face* fp = &rp->faces[pp->portal_face];
	if (GameTextures[fp->tmap].flags & TF_PROCEDURAL)
		return 1;
	int bm_handle = BaseBitmapHandleForFace(fp);
	if (GetFaceAlpha(fp, bm_handle))
		return 1;	  	//Face has alpha or transparency, so we can see through it
	else
		return 0;		//Not transparent, so no render past
}


#ifdef EDITOR
#define CROSS_WIDTH  8.0
#define CROSS_HEIGHT 8.0
#define	CURFACE_COLOR		GR_RGB( 255, 255,   0)
#define	CUREDGE_COLOR		GR_RGB(   0, 255,   0)
#define	MARKEDFACE_COLOR	GR_RGB(   0, 255, 255)
#define	MARKEDEDGE_COLOR	GR_RGB(   0, 150, 150)
#define	PLACED_COLOR		GR_RGB( 255,   0, 255)
//Draw outline for current edge & vertex
void OutlineCurrentFace(room* rp, int facenum, int edgenum, int vertnum, ddgr_color face_color, ddgr_color edge_color)
{
	face* fp = &rp->faces[facenum];
	g3Point p0, p1;
	ubyte c0, c1;
	int v;
	for (v = 0; v < fp->num_verts; v++) {
		c0 = g3_RotatePoint(&p0, &rp->verts[fp->face_verts[v]]);
		c1 = g3_RotatePoint(&p1, &rp->verts[fp->face_verts[(v + 1) % fp->num_verts]]);
		if (!(c0 & c1)) {      //both not off screen?
			//Draw current edge in green
			g3_DrawLine((v == edgenum) ? edge_color : face_color, &p0, &p1);
		}
		if ((v == vertnum) && (c0 == 0)) {
			//Draw a little cross at the current vert
			g3_ProjectPoint(&p0);	  //make sure projected
			rend_SetFlatColor(edge_color);
			rend_DrawLine(p0.p3_sx - CROSS_WIDTH, p0.p3_sy, p0.p3_sx, p0.p3_sy - CROSS_HEIGHT);
			rend_DrawLine(p0.p3_sx, p0.p3_sy - CROSS_HEIGHT, p0.p3_sx + CROSS_WIDTH, p0.p3_sy);
			rend_DrawLine(p0.p3_sx + CROSS_WIDTH, p0.p3_sy, p0.p3_sx, p0.p3_sy + CROSS_HEIGHT);
			rend_DrawLine(p0.p3_sx, p0.p3_sy + CROSS_HEIGHT, p0.p3_sx - CROSS_WIDTH, p0.p3_sy);
		}
	}
	// Draw upper left cross	
	if (Outline_lightmaps && (rp->faces[facenum].flags & FF_LIGHTMAP))
	{
		ASSERT(rp->faces[facenum].lmi_handle != BAD_LMI_INDEX);

		p0.p3_flags = 0;
		c0 = g3_RotatePoint(&p0, &LightmapInfo[rp->faces[facenum].lmi_handle].upper_left);
		if (!c0)
		{
			//Draw a little cross at the current vert
			g3_ProjectPoint(&p0);	  //make sure projected
			rend_SetFlatColor(GR_RGB(255, 0, 0));
			rend_DrawLine(p0.p3_sx - CROSS_WIDTH, p0.p3_sy, p0.p3_sx, p0.p3_sy - CROSS_HEIGHT);
			rend_DrawLine(p0.p3_sx, p0.p3_sy - CROSS_HEIGHT, p0.p3_sx + CROSS_WIDTH, p0.p3_sy);
			rend_DrawLine(p0.p3_sx + CROSS_WIDTH, p0.p3_sy, p0.p3_sx, p0.p3_sy + CROSS_HEIGHT);
			rend_DrawLine(p0.p3_sx, p0.p3_sy + CROSS_HEIGHT, p0.p3_sx - CROSS_WIDTH, p0.p3_sy);
		}
	}
}

//	Draw a room rotated and placed in space
static void DrawPlacedRoomFace(room* rp, vector* rotpoint, matrix* rotmat, vector* placepoint, int facenum, int color)
{
	face* fp = &rp->faces[facenum];

	g3Point p0, p1;
	ubyte c0, c1;
	int v;
	for (v = 0; v < fp->num_verts; v++)
	{
		vector tv;

		tv = (rp->verts[fp->face_verts[v]] - *rotpoint) * *rotmat + *placepoint;
		c0 = g3_RotatePoint(&p0, &tv);

		tv = (rp->verts[fp->face_verts[(v + 1) % fp->num_verts]] - *rotpoint) * *rotmat + *placepoint;
		c1 = g3_RotatePoint(&p1, &tv);

		if (!(c0 & c1))       //both not off screen?
			g3_DrawLine(color, &p0, &p1);
	}
}
#endif	//ifdef EDITOR

struct clip_wnd
{
	float left, top, right, bot;
};

inline int clip2d(g3Point* pnt, clip_wnd* wnd)
{
	int ret = 0;
	if (pnt->p3_codes & CC_BEHIND)
		return CC_BEHIND;

	if (pnt->p3_sx < wnd->left)
		ret |= CC_OFF_LEFT;
	if (pnt->p3_sx > wnd->right)
		ret |= CC_OFF_RIGHT;
	if (pnt->p3_sy < wnd->top)
		ret |= CC_OFF_TOP;
	if (pnt->p3_sy > wnd->bot)
		ret |= CC_OFF_BOT;
	return ret;
}

// Returns true if a line intersects another line
inline bool LineIntersectsLine(g3Point* ls, g3Point* le, float x1, float y1, float x2, float y2)
{
	float num = ((ls->p3_sy - y1) * (x2 - x1)) - ((ls->p3_sx - x1) * (y2 - y1));
	float denom = ((le->p3_sx - ls->p3_sx) * (y2 - y1)) - ((le->p3_sy - ls->p3_sy) * (x2 - x1));
	float r = num / denom;
	if (r >= 0.0 && r <= 1.0)
		return true;

	num = ((ls->p3_sy - y1) * (le->p3_sx - ls->p3_sx)) - ((ls->p3_sx - x1) * (le->p3_sy - ls->p3_sy));
	denom = ((le->p3_sx - ls->p3_sx) * (y2 - y1)) - ((le->p3_sy - ls->p3_sy) * (x2 - x1));
	float s = num / denom;
	if (s >= 0.0 && s <= 1.0)
		return true;
	return false;
}

// Returns true if a face intersects the passed in portal in any way
inline bool FaceIntersectsPortal(room* rp, face* fp, clip_wnd* wnd)
{
	g3Codes cc;

	cc.cc_or = 0;
	cc.cc_and = 0xff;
	for (int i = 0; i < fp->num_verts; i++)
	{
		cc.cc_or |= Room_clips[fp->face_verts[i]];
		cc.cc_and &= Room_clips[fp->face_verts[i]];
	}
	if (cc.cc_and)
		return false;		// completely outside
	if (!cc.cc_or)
		return true;		// completely inside

	// Now we must do a check
	for (int i = 0; i < fp->num_verts; i++)
	{
		g3Point* p1 = &World_point_buffer[rp->wpb_index + fp->face_verts[i]];
		g3Point* p2 = &World_point_buffer[rp->wpb_index + fp->face_verts[(i + 1) % fp->num_verts]];
		if (LineIntersectsLine(p1, p2, wnd->left, wnd->top, wnd->right, wnd->top))
			return true;
		if (LineIntersectsLine(p1, p2, wnd->right, wnd->top, wnd->right, wnd->bot))
			return true;
		if (LineIntersectsLine(p1, p2, wnd->right, wnd->bot, wnd->left, wnd->bot))
			return true;
		if (LineIntersectsLine(p1, p2, wnd->left, wnd->bot, wnd->left, wnd->top))
			return true;
	}
	return false;
}

// Sets the status of a glow light
void SetGlowStatus(int roomnum, int facenum, vector* center, float size, int fast)
{
	int first = 1;
	int first_free = -1;
	int done = 0;
	int count = 0;
	int found = 0;
	for (int i = 0; i < MAX_LIGHT_GLOWS && !done; i++)
	{
		if (count >= Num_glows)
		{
			if (first_free == -1)
				first_free = i;
			done = 1;
			continue;
		}
		if (LightGlows[i].flags & LGF_USED)
		{
			count++;
			if (LightGlows[i].roomnum == roomnum && LightGlows[i].facenum == facenum)
			{
				found = 1;
				LightGlows[i].flags |= LGF_INCREASING;
				done = 1;
			}
		}
		else
		{
			if (first)
			{
				first_free = i;
				first = 0;
			}
		}
	}

	if (!found)	// couldn't find it - is it a new one?
	{
		if (first_free == -1)	// no free slots
			return;
		LightGlows[first_free].flags = LGF_USED | LGF_INCREASING;
		if (fast)
			LightGlows[first_free].flags |= LGF_FAST;
		LightGlows[first_free].roomnum = roomnum;
		LightGlows[first_free].facenum = facenum;
		LightGlows[first_free].scalar = 0;
		LightGlows[first_free].size = size;
		LightGlows[first_free].center = *center;
		Num_glows++;
	}
}

// Takes a min,max vector and makes a surrounding cube from it
void MakePointsFromMinMax(vector* corners, vector* minp, vector* maxp)
{
	corners[0].x = minp->x;
	corners[0].y = maxp->y;
	corners[0].z = minp->z;
	corners[1].x = maxp->x;
	corners[1].y = maxp->y;
	corners[1].z = minp->z;
	corners[2].x = maxp->x;
	corners[2].y = minp->y;
	corners[2].z = minp->z;
	corners[3].x = minp->x;
	corners[3].y = minp->y;
	corners[3].z = minp->z;
	corners[4].x = minp->x;
	corners[4].y = maxp->y;
	corners[4].z = maxp->z;
	corners[5].x = maxp->x;
	corners[5].y = maxp->y;
	corners[5].z = maxp->z;
	corners[6].x = maxp->x;
	corners[6].y = minp->y;
	corners[6].z = maxp->z;
	corners[7].x = minp->x;
	corners[7].y = minp->y;
	corners[7].z = maxp->z;
}

// Rotates all the points in a room
void RotateRoomPoints(room* rp, vector* world_vecs)
{
	static PSRand legacy_jitter_rand;
	// Jig the vertices a bit if being deformed
	if (Viewer_object->effect_info && (Viewer_object->effect_info->type_flags & EF_DEFORM))
	{
		RetainedRoomSetDeformation(rp, legacy_jitter_rand.get_state(),
			&Global_alter_vec, Viewer_object->effect_info->deform_range *
			Viewer_object->effect_info->deform_time);
		for (int i = 0; i < rp->num_verts; i++)
		{
			vector vec = world_vecs[i];
			float val = ((legacy_jitter_rand() % 1000) - 500.0) / 500.0;
			val *= Viewer_object->effect_info->deform_time;
			vec += Global_alter_vec * (Viewer_object->effect_info->deform_range * val);

			g3_RotatePoint(&World_point_buffer[rp->wpb_index + i], &vec);
			g3_ProjectPoint(&World_point_buffer[rp->wpb_index + i]);
		}
	}
	else
	{
		RetainedRoomClearDeformation(rp);
		for (int i = 0; i < rp->num_verts; i++)
		{
			g3_RotatePoint(&World_point_buffer[rp->wpb_index + i], &world_vecs[i]);
			g3_ProjectPoint(&World_point_buffer[rp->wpb_index + i]);
		}
	}
}

// Given a vector, reflects that vector off of a mirror vector
// Useful for specular and other reflective surfaces
void ReflectRay(vector* dest, vector* src, vector* mirror_norm)
{
	*dest = *src;
	float d = *dest * *mirror_norm;
	vector upvec = d * *mirror_norm;
	*dest -= (2.0f * upvec);
}

// This is needed for small view cameras
// It clears the facing array so that it is recomputed
void ResetFacings()
{
	memset(Facing_visited, 0, sizeof(int) * (Highest_room_index + 1));
}

// Marks all the faces facing us as drawable
void MarkFacingFaces(int roomnum, vector* world_verts)
{
	room* rp = &Rooms[roomnum];
	face* fp;
	vector tvec;
	if (Facing_visited[roomnum] == FrameCount)
		return;
	Facing_visited[roomnum] = FrameCount;

	fp = &rp->faces[0];
	// Go through and mark all non facing faces
	if (Render_mirror_for_room)
	{
		room* mirror_rp = &Rooms[Mirror_room];
		face* mirror_fp = &mirror_rp->faces[mirror_rp->mirror_face];
		for (int t = 0; t < rp->num_faces; t++, fp++)
		{
			vector incident_norm;
			ReflectRay(&incident_norm, &fp->normal, &mirror_fp->normal);

			tvec = Viewer_eye - world_verts[fp->face_verts[0]];
			if ((tvec * incident_norm) <= 0)
				fp->flags |= FF_NOT_FACING;
		}
	}
	else
	{
		for (int i = 0; i < rp->num_faces; i++, fp++)
		{
			tvec = Viewer_eye - world_verts[fp->face_verts[0]];
			if ((tvec * fp->normal) <= 0)
				fp->flags |= FF_NOT_FACING;
		}
	}
}

// Returns true if the external room is visible from the passed in portal
int ExternalRoomVisibleFromPortal(int index, clip_wnd* wnd)
{
	int i;
	ubyte code = 0xff;
	g3Point pnt;
	// This is a stupid hack to prevent really large buildings from popping in and out of view
	if (External_room_project_net[index])
		return 1;

	for (i = 0; i < 8; i++)
	{
		pnt.p3_sx = External_room_corners[index][i].x;
		pnt.p3_sy = External_room_corners[index][i].y;
		pnt.p3_codes = 0;
		code &= clip2d(&pnt, wnd);
	}

	if (code)
		return 0;		// building can't be seen from this portal
	return 1;
}

// Checks to see what faces intersect the passed in portal
void MarkFacesForRendering(int roomnum, clip_wnd* wnd)
{
	room* rp = &Rooms[roomnum];
	MarkFacingFaces(roomnum, rp->verts);

	// Rotate all the points in this room	
	if (rp->wpb_index == -1)
	{
		rp->wpb_index = Global_buffer_index;

		RotateRoomPoints(rp, rp->verts);

		Global_buffer_index += rp->num_verts;
	}

	if (rp->flags & RF_DOOR)
	{
		for (int i = 0; i < rp->num_faces; i++)
			rp->faces[i].flags |= FF_VISIBLE;
	}
	else
	{
		// If this room contains a mirror, just mark all faces as visible
		// Else go through and figure out which ones are visible from the current portal
		if (rp->mirror_face == -1 || !Detail_settings.Mirrored_surfaces)
		{
			// Do pointer dereferencing instead of array lookup for speed reasons
			g3Point* pnt = &World_point_buffer[rp->wpb_index];
			for (int i = 0; i < rp->num_verts; i++, pnt++)
			{
				Room_clips[i] = clip2d(pnt, wnd);
			}
			face* fp = &rp->faces[0];
			for (int i = 0; i < rp->num_faces; i++, fp++)
			{
				if (fp->flags & (FF_NOT_FACING | FF_VISIBLE))
					continue;			// this face is a backface

				if (FaceIntersectsPortal(rp, fp, wnd))
					fp->flags |= FF_VISIBLE;
			}
		}
		else
		{
			if (rp->flags & RF_MIRROR_VISIBLE)	// If this room is already mirror, just return
				return;
			g3Point* pnt = &World_point_buffer[rp->wpb_index];
			for (int i = 0; i < rp->num_verts; i++, pnt++)
			{
				Room_clips[i] = clip2d(pnt, wnd);
			}
			int done = 0;
			face* fp;
			for (int i = 0; i < rp->num_mirror_faces && !done; i++)
			{
				fp = &rp->faces[rp->mirror_faces_list[i]];
				if (FaceIntersectsPortal(rp, fp, wnd))
				{
					rp->flags |= RF_MIRROR_VISIBLE;
					Mirror_rooms[Num_mirror_rooms++] = roomnum;
					done = 1;
				}
			}

			if (rp->flags & RF_MIRROR_VISIBLE)	// Mirror is visible, just mark all faces as visible
			{
				fp = &rp->faces[0];
				for (int i = 0; i < rp->num_faces; i++, fp++)
				{
					fp->flags |= FF_VISIBLE;
				}
			}
			else
			{
				fp = &rp->faces[0];
				for (int i = 0; i < rp->num_faces; i++, fp++)
				{
					if (fp->flags & (FF_NOT_FACING | FF_VISIBLE))
						continue;			// this face is a backface

					if (FaceIntersectsPortal(rp, fp, wnd))
						fp->flags |= FF_VISIBLE;
				}
			}
		}
	}

	// Mark objects for rendering
	for (int objnum = rp->objects; (objnum != -1); objnum = Objects[objnum].next)
	{
		object* obj = &Objects[objnum];
		ubyte anded = 0xff;
		g3Point pnts[8];
		vector vecs[8];
		ubyte code;
		if (rp->flags & RF_DOOR)	// Render all objects in a door room
		{
			obj->flags |= OF_SAFE_TO_RENDER;
			continue;
		}
		if (rp->flags & RF_MIRROR_VISIBLE)	// Render all objects if this mirror is visible
		{
			obj->flags |= OF_SAFE_TO_RENDER;
			continue;
		}

		MakePointsFromMinMax(vecs, &obj->min_xyz, &obj->max_xyz);
		for (int i = 0; i < 8; i++)
		{
			g3_RotatePoint(&pnts[i], &vecs[i]);
			g3_ProjectPoint(&pnts[i]);
			code = clip2d(&pnts[i], wnd);
			anded &= code;
			if (pnts[i].p3_codes & CC_BEHIND)
				anded = 0;
		}

		if (!anded)
		{
			// Object is visible
			obj->flags |= OF_SAFE_TO_RENDER;
		}
	}

}
// Prerotates all external room points and caches them
void RotateAllExternalRooms()
{
	// Build the external room list if needed
	if (N_external_rooms == -1)
	{
		// Set up our z wall
		float zclip = (Detail_settings.Terrain_render_distance) * Matrix_scale.z;
		g3_SetFarClipZ(zclip);
		N_external_rooms = 0;

		for (int i = 0; i <= Highest_room_index; i++)
		{
			if ((Rooms[i].flags & RF_EXTERNAL) && Rooms[i].used)
			{
				External_room_list[N_external_rooms++] = i;
			}
		}
		ASSERT(N_external_rooms < MAX_EXTERNAL_ROOMS);		// Get Jason if hit this
		// Rotate all the points
		vector corners[8];
		for (int i = 0; i < N_external_rooms; i++)
		{
			int roomnum = External_room_list[i];
			room* rp = &Rooms[roomnum];
			MakePointsFromMinMax(corners, &rp->min_xyz, &rp->max_xyz);

			ubyte andbyte = 0xff;
			g3Point pnt;
			External_room_codes[i] = 0xff;
			External_room_project_net[i] = 0;
			bool behind = 0;
			bool infront = 0;
			for (int t = 0; t < 8; t++)
			{
				g3_RotatePoint(&pnt, &corners[t]);
				External_room_codes[i] &= pnt.p3_codes;
				if (pnt.p3_codes & CC_BEHIND)
					behind = true;
				else
					infront = true;
				pnt.p3_codes &= ~CC_BEHIND;
				g3_ProjectPoint(&pnt);
				External_room_corners[i][t].x = pnt.p3_sx;
				External_room_corners[i][t].y = pnt.p3_sy;
			}
			if (infront && behind)
			{
				External_room_codes[i] = 0;
				External_room_project_net[i] = 1;
			}
			else
			{
				if (behind)
				{
					External_room_codes[i] = CC_BEHIND;
				}
			}
		}
	}
}

void CheckFogPortalExtents(int roomnum, int portalnum)
{
	room* rp = &Rooms[roomnum];
	ASSERT(rp->flags & RF_FOG);

	int found_room = -1;
	for (int i = 0; i < Num_fogged_rooms_this_frame; ++i)
	{
		if (Fog_portal_data[i].roomnum != roomnum)
			continue;

		found_room = i;
		break;
	}

	if (found_room == -1)
	{
		// Couldn't find this room in our list, so make a new one
		if (Num_fogged_rooms_this_frame >= MAX_FOGGED_ROOMS_PER_FRAME)
		{
			mprintf((0, "Too many fogged rooms in view cone!\n"));
			return;
		}

		found_room = Num_fogged_rooms_this_frame++;
		Fog_portal_data[found_room].close_face = NULL;
		Fog_portal_data[found_room].close_dist = 10000000.0f;
		Fog_portal_data[found_room].roomnum = roomnum;
	}

	// get the portal face
	int fn = rp->portals[portalnum].portal_face;
	face* fp = &rp->faces[fn];

	// calculate the plane equation for the portal
	vector* vec = &rp->verts[fp->face_verts[0]];
	float distance = -vm_DotProduct(&fp->normal, vec);

	// calculate the distance from the camera to the portal
	distance = vm_DotProduct(&fp->normal, &Viewer_eye) + distance;
	float compare_distance = std::fabs(distance);
	if (compare_distance < Fog_portal_data[found_room].close_dist)
	{
		// this portal is closer to the camera than the previous
		Fog_portal_data[found_room].close_dist = compare_distance;
		Fog_portal_data[found_room].close_face = fp;
	}
}

g3Point Combined_portal_points[MAX_VERTS_PER_FACE * 5];
void BuildRoomListSub(int start_room_num, clip_wnd* wnd, int depth)
{
	room* rp = &Rooms[start_room_num];
	int i, t;
	if (Render_portals)
	{
		rend_SetTextureType(TT_FLAT);
		rend_SetAlphaType(AT_CONSTANT);
		rend_SetAlphaValue(255);
		rend_SetFlatColor(GR_RGB(255, 255, 255));

		rend_DrawLine(wnd->left, wnd->top, wnd->right, wnd->top);
		rend_DrawLine(wnd->right, wnd->top, wnd->right, wnd->bot);
		rend_DrawLine(wnd->right, wnd->bot, wnd->left, wnd->bot);
		rend_DrawLine(wnd->left, wnd->bot, wnd->left, wnd->top);
	}

	if (!Rooms_visited[start_room_num])
		Render_list[N_render_rooms++] = start_room_num;

	ASSERT(N_render_rooms < MAX_RENDER_ROOMS);
#ifdef _DEBUG
	Mine_depth++;
#endif

	Rooms_visited[start_room_num] = 1;
	Room_depth_list[start_room_num] = depth;

	//If this room is a closed (non-seethrough) door, don't check any of its portals, 
	//...UNLESS this is the first room we're looking at (meaning the viewer is in this room)
	if ((rp->flags & RF_DOOR) && (DoorwayGetPosition(rp) == 0.0) && !(Doors[rp->doorway_data->doornum].flags & DF_SEETHROUGH))
		if (depth != 0)
			return;

	//Check all the portals for this room
	for (t = 0; t < rp->num_portals; t++)
	{
		portal* pp = &rp->portals[t];
		int croom = pp->croom;
		ASSERT(croom >= 0);

		// If we are an external room portalizing into another external room, then skip!
		if ((rp->flags & RF_EXTERNAL) && (Rooms[croom].flags & RF_EXTERNAL))
			continue;

		//Check if we can see through this portal, and if not, skip it
		if (!RenderPastPortal(rp, pp))
			continue;

		// If this portal has been visited, skip it
		if (Room_depth_list[croom] < Room_depth_list[start_room_num])
			continue;

		//Deal with external portals differently
		int external_door_hack = 0;
		if (rp->flags & RF_EXTERNAL && Rooms[croom].flags & RF_DOOR)
			external_door_hack = 1;

		//Get pointer to this portal's face
		face* fp = &rp->faces[pp->portal_face];

		//See if portal is facing toward us
		if (!external_door_hack && !(pp->flags & PF_COMBINED))
		{
			vector check_v = Viewer_eye - rp->verts[fp->face_verts[0]];
			if (check_v * fp->normal <= 0)
			{
				//not facing us
				continue;
			}
		}

		g3Codes cc;
		cc.cc_or = 0; cc.cc_and = 0xff;
		int nv = fp->num_verts;

		//Code the face points
		// If this is a combined portal, then do that 
		if ((pp->flags & PF_COMBINED) && !(Rooms[croom].flags & RF_FOG))
		{
			// If this isn't the portal-combine master, then skip it
			if (pp->combine_master != t)
				continue;
			int num_points = 0;
			for (i = 0; i < rp->num_portals; i++)
			{
				if (((rp->portals[i].flags & PF_COMBINED) == 0) || rp->portals[i].combine_master != t)
					continue;

				int k;

				face* this_fp = &rp->faces[rp->portals[i].portal_face];
				vector check_v;
				check_v = Viewer_eye - rp->verts[this_fp->face_verts[0]];
				if (check_v * this_fp->normal <= 0)		//not facing us
					continue;

				g3Codes combine_cc;
				combine_cc.cc_or = 0; combine_cc.cc_and = 0xff;
				ASSERT((num_points + this_fp->num_verts) < (MAX_VERTS_PER_FACE * 5));

				// First we must rotate and clip this polygon
				for (k = 0; k < this_fp->num_verts; k++)
				{
					ubyte c = g3_RotatePoint(&Combined_portal_points[num_points + k], &rp->verts[this_fp->face_verts[k]]);
					combine_cc.cc_or |= c;
					combine_cc.cc_and &= c;
				}
				if (combine_cc.cc_and)
				{
					continue;	// clipped away!
				}
				else if (combine_cc.cc_or)
				{
					g3Point* pointlist[MAX_VERTS_PER_FACE];
					for (k = 0; k < this_fp->num_verts; k++)
					{
						pointlist[k] = &Combined_portal_points[num_points + k];
						ASSERT(!(pointlist[k]->p3_flags & PF_TEMP_POINT));
					}

					//If portal not all on screen, must clip it
					int combine_nv = this_fp->num_verts;
					g3Point** pl = g3_ClipPolygon(pointlist, &combine_nv, &combine_cc);
					if (combine_cc.cc_and)
					{
						g3_FreeTempPoints(pl, combine_nv);
					}
					else
					{
						// save the clipped points
						g3Point temp_points[MAX_VERTS_PER_FACE];
						for (k = 0; k < combine_nv; k++)
						{
							temp_points[k] = *pl[k];
							temp_points[k].p3_flags &= ~PF_TEMP_POINT;
						}

						// release the temp points
						g3_FreeTempPoints(pl, combine_nv);

						// put the points back in the buffer
						// NOTE: we have to do it like this because pl points to
						// Combined_portal_points pointers
						for (k = 0; k < combine_nv; k++)
						{
							Combined_portal_points[k + num_points] = temp_points[k];
						}

						num_points += combine_nv;
					}
				}
				else
				{
					// No clipping needed, face is fully on screen
					num_points += this_fp->num_verts;
				}
			}
			if (num_points == 0)
				continue;

			// Now, figure out a min/max for these points
			g3Point four_points[4];
			for (i = 0; i < num_points; i++)
			{
				g3_ProjectPoint(&Combined_portal_points[i]);
			}
			//[ISB] disgusting hack, sometimes these aren't initialized. 
			int left = 0, top = 0, right = 0, bottom = 0;
			clip_wnd combine_wnd;
			combine_wnd.right = combine_wnd.bot = 0.0;
			combine_wnd.left = Render_width;
			combine_wnd.top = Render_height;
			//make new clip window
			for (i = 0; i < num_points; i++)
			{
				float x = Combined_portal_points[i].p3_sx, y = Combined_portal_points[i].p3_sy;
				if (x < combine_wnd.left)
				{
					combine_wnd.left = x;
					left = i;
				}
				if (x > combine_wnd.right)
				{
					combine_wnd.right = x;
					right = i;
				}
				if (y < combine_wnd.top)
				{
					combine_wnd.top = y;
					top = i;
				}
				if (y > combine_wnd.bot)
				{
					combine_wnd.bot = y;
					bottom = i;
				}
			}
			// Now harvest these points
			four_points[0] = Combined_portal_points[left];
			four_points[1] = Combined_portal_points[top];
			four_points[2] = Combined_portal_points[right];
			four_points[3] = Combined_portal_points[bottom];
			for (i = 0; i < 4; i++)
			{
				Combined_portal_points[i] = four_points[i];
				Combined_portal_points[i].p3_flags &= ~(PF_PROJECTED | PF_TEMP_POINT);
				ubyte c = Combined_portal_points[i].p3_codes;
				cc.cc_and &= c;
				cc.cc_or |= c;
			}
			nv = 4;
		}
		else
		{
			for (i = 0; i < nv; i++)
			{
				g3_RotatePoint(&Combined_portal_points[i], &rp->verts[fp->face_verts[i]]);

				ubyte c = Combined_portal_points[i].p3_codes;
				cc.cc_and &= c;
				cc.cc_or |= c;
			}
		}
		//If points are on screen, see if they're in the clip window
		if (cc.cc_and == 0 || external_door_hack)
		{
			bool clipped = 0;
			g3Point* pointlist[MAX_VERTS_PER_FACE], ** pl = pointlist;
			for (i = 0; i < nv; i++)
				pointlist[i] = &Combined_portal_points[i];
			//If portal not all on screen, must clip it
			if (cc.cc_or)
			{
				pl = g3_ClipPolygon(pl, &nv, &cc);
				clipped = 1;
			}
			cc.cc_and = 0xff;
			for (i = 0; i < nv; i++)
			{
				g3_ProjectPoint(pl[i]);
				cc.cc_and &= clip2d(pl[i], wnd);
			}
			// Make sure it didn't get clipped away
			if (cc.cc_and == 0 || external_door_hack)
			{
				clip_wnd new_wnd;
				new_wnd.right = new_wnd.bot = 0.0;
				new_wnd.left = Render_width;
				new_wnd.top = Render_height;
				//make new clip window
				for (i = 0; i < nv; i++)
				{
					float x = pl[i]->p3_sx, y = pl[i]->p3_sy;
					if (x < new_wnd.left)
						new_wnd.left = x;
					if (x > new_wnd.right)
						new_wnd.right = x;
					if (y < new_wnd.top)
						new_wnd.top = y;
					if (y > new_wnd.bot)
						new_wnd.bot = y;
				}
				// If this room is fogged, see if this portal is closest
				if (Rooms[croom].flags & RF_FOG)
				{
					CheckFogPortalExtents(croom, pp->cportal);
				}
				//Combine the two windows
				new_wnd.left = std::max(wnd->left, new_wnd.left);
				new_wnd.right = std::min(wnd->right, new_wnd.right);
				new_wnd.top = std::max(wnd->top, new_wnd.top);
				new_wnd.bot = std::min(wnd->bot, new_wnd.bot);
				if (clipped)
				{		//Free up temp points
					g3_FreeTempPoints(pl, nv);
					clipped = 0;
				}
				if (Rooms[croom].flags & RF_EXTERNAL)
				{
					if (!Called_from_terrain)
					{
						Must_render_terrain = 1;
						RotateAllExternalRooms();
						// For this external portal, we must check to see what external
						// rooms are visible from here
						for (i = 0; i < N_external_rooms; i++)
						{
							if (External_room_codes[i])
							{
								continue;
							}
							if (External_room_list[i] != croom)
							{
								// If this portal has been visited, skip it
								if (Room_depth_list[External_room_list[i]] < Room_depth_list[start_room_num])
								{
									continue;
								}
								if (!ExternalRoomVisibleFromPortal(i, &new_wnd))
								{
									continue;
								}
							}
							MarkFacesForRendering(External_room_list[i], &new_wnd);
							BuildRoomListSub(External_room_list[i], &new_wnd, depth + 1);
							Room_depth_list[External_room_list[i]] = 255;
						}
						//Combine the two windows
						Terrain_portal_left = std::min(new_wnd.left, (float)Terrain_portal_left);
						Terrain_portal_right = std::max(new_wnd.right, (float)Terrain_portal_right);
						Terrain_portal_top = std::min(new_wnd.top, (float)Terrain_portal_top);
						Terrain_portal_bottom = std::max(new_wnd.bot, (float)Terrain_portal_bottom);
					}
				}
				else
				{
					MarkFacesForRendering(croom, &new_wnd);
					BuildRoomListSub(croom, &new_wnd, depth + 1);
					Room_depth_list[croom] = 255;
				}
			}
			if (clipped)		//Free up temp points
				g3_FreeTempPoints(pl, nv);
		}
	}
}

//compare function for room z sort
float Room_z_depth[MAX_ROOMS + MAX_PALETTE_ROOMS];
static int Room_z_sort_func(const short* a, const short* b)
{
	float az, bz;
	az = Room_z_depth[*a];
	bz = Room_z_depth[*b];
	if (az < bz)
		return 1;
	else if (az > bz)
		return -1;
	else
		return 0;
}

//build a list of rooms to be rendered
//fills in Render_list & N_render_rooms
void BuildRoomList(int start_room_num)
{
	clip_wnd wnd;
	room* rp = &Rooms[start_room_num];
	//For now, render all connected rooms
	for (int i = 0; i <= Highest_room_index; i++)
	{
		Rooms_visited[i] = 0;
		Room_depth_list[i] = 255;
		Rooms[i].wpb_index = -1;
	}
#ifdef EDITOR
	Rooms_visited[start_room_num] = 0;		//take care of rooms in the room palette
	Room_depth_list[start_room_num] = 0;
#endif
	N_external_rooms = -1;
	N_render_rooms = 0;
	Global_buffer_index = 0;
	// Mark all the faces in our start room as renderable
	for (int i = 0; i < rp->num_faces; i++)
		rp->faces[i].flags |= FF_VISIBLE;

	MarkFacingFaces(start_room_num, rp->verts);
	// Enable mirror if there is one
	if (rp->mirror_face != -1 && Detail_settings.Mirrored_surfaces && !(rp->faces[rp->mirror_face].flags & FF_NOT_FACING))
	{
		rp->flags |= RF_MIRROR_VISIBLE;
		Mirror_rooms[Num_mirror_rooms++] = start_room_num;
	}

	// Get our points rotated, and update the global point list
	rp->wpb_index = Global_buffer_index;

	RotateRoomPoints(rp, rp->verts);

	Global_buffer_index += rp->num_verts;

	// Mark all objects in this room as visible
	for (int objnum = rp->objects; (objnum != -1); objnum = Objects[objnum].next)
		Objects[objnum].flags |= OF_SAFE_TO_RENDER;

	//Initial clip window is whole screen
	wnd.left = wnd.top = 0.0;
	wnd.right = Render_width;
	wnd.bot = Render_height;
	BuildRoomListSub(start_room_num, &wnd, 0);
	//mprintf((0,"N_render_rooms = %d ",N_render_rooms));
#ifdef EDITOR
//Add all external rooms to render list if that flag set
	if (Editor_view_mode == VM_MINE && In_editor_mode)
	{
		if (Render_all_external_rooms) {
			int i;
			room* rp;
			for (i = 0, rp = Rooms; i <= Highest_room_index; i++, rp++) {
				if (rp->used && (rp->flags & RF_EXTERNAL))
				{
					for (int t = 0; t < rp->num_faces; t++)
						rp->faces[t].flags |= FF_VISIBLE;
					MarkFacingFaces(i, rp->verts);

					if (!Rooms_visited[i])
						Render_list[N_render_rooms++] = i;
					Rooms_visited[i] = 1;
				}
			}
		}
	}
#endif
}

#ifdef EDITOR
// Returns 1 if x,y is inside the given polygon, else 0
int point_in_poly(int nv, g3Point* p, float x, float y)
{
	int i, j, c = 0;
	for (i = 0, j = nv - 1; i < nv; j = i++)
	{
		if ((((p[i].p3_sy <= y) && (y < p[j].p3_sy)) || ((p[j].p3_sy <= y) && (y < p[i].p3_sy))) &&
			(x < (p[j].p3_sx - p[i].p3_sx) * (y - p[i].p3_sy) / (p[j].p3_sy - p[i].p3_sy) + p[i].p3_sx))
			c = !c;
	}

	return c;
}

#define STEPSIZE		.01f
#define STEPSIZE_MIN	.1f
void RenderFloatingTrig(room* rp, face* fp)\
{
	if (!Render_floating_triggers)
		return;
	vector leftvec, rightvec;
	vector left, right;
	vector left_step, right_step;
	int n_steps, i, j;
	g3Point p3;
	float stepsize;
	left = rp->verts[fp->face_verts[0]];
	right = rp->verts[fp->face_verts[1]];
	g3_RotatePoint(&p3, &left);
	stepsize = STEPSIZE * p3.p3_z;
	if (stepsize < STEPSIZE_MIN)
		stepsize = STEPSIZE_MIN;
	leftvec = rp->verts[fp->face_verts[3]] - rp->verts[fp->face_verts[0]];
	rightvec = rp->verts[fp->face_verts[2]] - rp->verts[fp->face_verts[1]];
	n_steps = (vm_GetMagnitude(&leftvec) / stepsize + 0.5) + 1;
	left_step = leftvec / n_steps;
	right_step = rightvec / n_steps;
	for (i = 0; i < n_steps; i++) {
		vector p;
		g3Point p3;
		vector crossvec, cross_step;
		int n_crosssteps;

		crossvec = right - left;
		n_crosssteps = (vm_GetMagnitude(&crossvec) / stepsize + 0.5) + 1;
		cross_step = crossvec / n_steps;
		p = left;
		for (j = 0; j < n_crosssteps; j++) 
		{
			if (g3_RotatePoint(&p3, &p) == 0) 
			{
				//on screen
				g3_ProjectPoint(&p3);
				rend_SetPixel(GR_RGB(255, 100, 100), p3.p3_sx, p3.p3_sy);
			}
			p += cross_step;
		}
		left += left_step;
		right += right_step;
	}
	}
#endif	//ifdef EDITOR

float Specular_scalars[4] = { 1.0f,.66f,.33f,.25f };
static bool FaceHasSmoothSpecularNormals(face* fp)
{
	return fp->special_handle != BAD_SPECIAL_FACE_INDEX &&
		(SpecialFaces[fp->special_handle].flags & SFF_SPEC_SMOOTH) &&
		SpecialFaces[fp->special_handle].vertnorms != nullptr;
}

static bool UseSmoothSpecularForFace(face* fp)
{
	return (GameTextures[fp->tmap].flags & TF_SMOOTH_SPECULAR) ||
		Render_preferred_state.per_pixel_lighting;
}

static void AddSpecularContribution(const vector& view_vec, vector incident_norm, const vector& normal,
	int material_type, float scalar_scale, float cr, float cg, float cb, float& rv, float& gv, float& bv)
{
	if (vm_NormalizeVectorFast(&incident_norm) <= 0.0f)
		return;

	float d = incident_norm * normal;
	vector upvec = d * normal;
	incident_norm -= (2 * upvec);
	float dotp = view_vec * incident_norm;
	if (dotp > 1.0f)
		dotp = 1.0f;

	if (dotp <= 0.0f)
		return;

	int index = (float)(MAX_SPECULAR_INCREMENTS - 1) * dotp;
	float scalar = Specular_tables[material_type][index] * scalar_scale;
	rv = std::min(1.0f, rv + (scalar * cr));
	gv = std::min(1.0f, gv + (scalar * cg));
	bv = std::min(1.0f, bv + (scalar * cb));
}

static void AddDynamicSpecularContribution(const renderer_per_pixel_light& light, const vector& vertex_pos,
	const vector& view_vec, const vector& normal, int material_type, float& rv, float& gv, float& bv)
{
	vector light_pos = { light.position[0], light.position[1], light.position[2] };
	if (light.has_specular_position)
	{
		light_pos.x = light.specular_position[0];
		light_pos.y = light.specular_position[1];
		light_pos.z = light.specular_position[2];
	}
	vector light_delta = vertex_pos - light_pos;
	float distance = vm_NormalizeVectorFast(&light_delta);
	if (distance <= 0.0f)
		return;

	float radius = light.has_specular_position ? std::max(light.radius, light.specular_radius) : light.radius;
	if (light.headlight && light.has_specular_position)
	{
		vector diffuse_pos = { light.position[0], light.position[1], light.position[2] };
		vector specular_pos = { light.specular_position[0], light.specular_position[1], light.specular_position[2] };
		vector throw_delta = diffuse_pos - specular_pos;
		float throw_distance = vm_GetMagnitudeFast(&throw_delta);
		radius = std::max(light.radius,
			throw_distance + std::max(light.specular_radius - throw_distance, light.radius * 4.0f));
	}
	radius = std::max(radius, 0.0001f);
	float scalar = 1.0f - (distance / radius);
	if (scalar <= 0.0f)
		return;

	scalar = powf(scalar, std::max(light.falloff, 0.0001f));
	if (light.directional)
	{
		vector light_dir = { light.direction[0], light.direction[1], light.direction[2] };
		if (vm_NormalizeVectorFast(&light_dir) <= 0.0f)
			return;

		float direction_dot = light_delta * light_dir;
		if (direction_dot < light.dot_range)
			return;

		scalar *= (direction_dot - light.dot_range) / std::max(1.0f - light.dot_range, 0.0001f);
	}

	const float specular_tuning = light.headlight ? Render_per_pixel_headlight_specular_strength :
		Render_per_pixel_dynamic_specular_strength;
	const float specular_strength = (light.specular_scalar > 0.0f ? light.specular_scalar : 1.0f) *
		specular_tuning;
	AddSpecularContribution(view_vec, light_delta, normal, material_type, scalar * specular_strength, light.color[0],
		light.color[1], light.color[2], rv, gv, bv);
}

static int SpecularMaterialType(face* fp)
{
	int spec_tmap = SpecularTextureTmapForFace(fp);
	if (GameTextures[spec_tmap].flags & TF_PLASTIC)
		return 1;
	if (GameTextures[spec_tmap].flags & TF_MARBLE)
		return 2;
	return 0;
}

static int SpecularMaterialExponent(int material_type)
{
	if (material_type == 1)
		return 14;
	if (material_type == 2)
		return 4;
	return 6;
}

static int TunedSpecularMaterialExponent(int material_type)
{
	float exponent = (float)SpecularMaterialExponent(material_type) *
		ConfigNormalizePerPixelSpecularSharpness(Render_per_pixel_specular_sharpness);
	if (exponent < 1.0f)
		exponent = 1.0f;
	if (exponent > 128.0f)
		exponent = 128.0f;
	return (int)(exponent + 0.5f);
}

static vector SpecularWorldToViewPosition(const vector& world_position)
{
	vector view_position = world_position - Viewer_eye;
	return view_position * Viewer_orient;
}

static vector SpecularWorldToViewDirection(const vector& world_direction)
{
	vector view_direction = world_direction * Viewer_orient;
	vm_NormalizeVectorFast(&view_direction);
	return view_direction;
}

static int SpecularBitmapHandle(face* fp)
{
	if ((fp->flags & FF_DESTROYED) && (GameTextures[fp->tmap].flags & TF_DESTROYABLE))
		return GetTextureBitmap(GameTextures[fp->tmap].destroy_handle, 0);

	SplitSpecularTexturePair pair;
	if (FaceCanUseSplitSpecularTextures(fp, &pair))
		return pair.spec_bm;

	return GetTextureBitmap(fp->tmap, 0);
}

static bool SpecularFaceHasSplitTexturePair(face* fp)
{
	return FaceCanUseSplitSpecularTextures(fp);
}

static bool SpecularTextureHasMask(face* fp)
{
	int bm_handle = SpecularBitmapHandle(fp);
	return bm_handle >= 0 && bm_format(bm_handle) == BITMAP_FORMAT_4444;
}

static bool SpecularFaceHasLocalSources(face* fp)
{
	if (fp->special_handle == BAD_SPECIAL_FACE_INDEX)
		return false;

	special_face* sf = &SpecialFaces[fp->special_handle];
	for (int i = 0; i < sf->num; i++)
	{
		if (sf->spec_instance[i].bright_color != 0)
			return true;
	}
	return false;
}

static bool SpecularFaceHasAuthoredPath(room* rp, face* fp)
{
	return fp->special_handle != BAD_SPECIAL_FACE_INDEX || (rp->flags & RF_EXTERNAL);
}

static bool SpecularCanUseSplitTextureFace(room* rp, face* fp)
{
	if (!SplitSpecularTexturePathEnabled())
		return false;
	if (rp->flags & RF_EXTERNAL)
		return false;
	if (fp->lmi_handle == BAD_LMI_INDEX || !(fp->flags & FF_LIGHTMAP))
		return false;
	if (!SpecularFaceHasSplitTexturePair(fp))
		return false;
	if (!SpecularTextureHasMask(fp))
		return false;

	return GetFaceAlpha(fp, SpecularBitmapHandle(fp)) == AT_ALWAYS;
}

static bool SpecularCanUseFieldStaticFace(room* rp, face* fp)
{
	if (!Render_per_pixel_field_static_specular)
		return false;
	if (!Render_preferred_state.per_pixel_lighting || !UseHardware || !rend_CanUseNewrender())
		return false;
	if (rp->flags & RF_EXTERNAL)
		return false;
	if (fp->lmi_handle == BAD_LMI_INDEX || !(fp->flags & FF_LIGHTMAP))
		return false;
	if (!SpecularTextureHasMask(fp))
		return false;

	return GetFaceAlpha(fp, SpecularBitmapHandle(fp)) == AT_ALWAYS;
}

static bool SpecularShouldQueueFace(room* rp, face* fp)
{
	if (!Detail_settings.Specular_lighting)
		return false;
	if (SpecularCanUseFieldStaticFace(rp, fp))
		return true;
	if ((GameTextures[fp->tmap].flags & TF_SPECULAR) && SpecularFaceHasAuthoredPath(rp, fp))
		return true;
	if (SpecularCanUseSplitTextureFace(rp, fp))
		return true;
	return false;
}

static bool SpecularBlockAlreadyHasSource(const SpecularBlock& specblock, const vector& view_center, ushort color)
{
	const float r = (float)((color >> 10) & 0x1f) / 31.0f;
	const float g = (float)((color >> 5) & 0x1f) / 31.0f;
	const float b = (float)(color & 0x1f) / 31.0f;
	for (int i = 0; i < specblock.num_speculars; i++)
	{
		const float dx = specblock.speculars[i].bright_center[0] - view_center.x;
		const float dy = specblock.speculars[i].bright_center[1] - view_center.y;
		const float dz = specblock.speculars[i].bright_center[2] - view_center.z;
		if ((dx * dx + dy * dy + dz * dz) > 0.0001f)
			continue;
		if (fabs(specblock.speculars[i].color[0] - r * Render_per_pixel_static_specular_strength) < 0.001f &&
			fabs(specblock.speculars[i].color[1] - g * Render_per_pixel_static_specular_strength) < 0.001f &&
			fabs(specblock.speculars[i].color[2] - b * Render_per_pixel_static_specular_strength) < 0.001f)
		{
			return true;
		}
	}
	return false;
}

static void SpecularAddStaticSource(SpecularBlock& specblock, const specular_instance& source)
{
	if (source.bright_color == 0 || specblock.num_speculars >= MAX_SPECULARS)
		return;

	vector view_center = SpecularWorldToViewPosition(source.bright_center);
	if (SpecularBlockAlreadyHasSource(specblock, view_center, source.bright_color))
		return;

	int index = specblock.num_speculars++;
	ushort color = source.bright_color;
	specblock.speculars[index].bright_center[0] = view_center.x;
	specblock.speculars[index].bright_center[1] = view_center.y;
	specblock.speculars[index].bright_center[2] = view_center.z;
	specblock.speculars[index].bright_center[3] = 1.0f;
	specblock.speculars[index].color[0] =
		((float)((color >> 10) & 0x1f) / 31.0f) * Render_per_pixel_static_specular_strength;
	specblock.speculars[index].color[1] =
		((float)((color >> 5) & 0x1f) / 31.0f) * Render_per_pixel_static_specular_strength;
	specblock.speculars[index].color[2] =
		((float)(color & 0x1f) / 31.0f) * Render_per_pixel_static_specular_strength;
	specblock.speculars[index].color[3] = 1.0f;
}

static void SpecularAddFaceStaticSources(SpecularBlock& specblock, face* fp)
{
	if (fp->special_handle == BAD_SPECIAL_FACE_INDEX)
		return;

	special_face* sf = &SpecialFaces[fp->special_handle];
	for (int t = 0; t < sf->num && specblock.num_speculars < MAX_SPECULARS; t++)
		SpecularAddStaticSource(specblock, sf->spec_instance[t]);
}

struct PrecomputedSpecularSourceSet
{
	int count;
	specular_instance sources[MAX_SPECULARS];
};

struct PrecomputedSpecularFaceSources
{
	PrecomputedSpecularSourceSet exact;
	PrecomputedSpecularSourceSet split;
	PrecomputedSpecularSourceSet field;
	std::vector<PrecomputedSpecularSourceSet> field_vertices;
	std::vector<vector> normals;
};

struct SpecularFaceBasis
{
	vector center;
	vector normal;
	vector tangent;
	vector bitangent;
	bool valid;
};

struct SpecularFaceInfo
{
	int room_index;
	int face_index;
	int face_info_index;
	int exact_tmap;
	int split_tmap;
	SpecularFaceBasis basis;
	float field_weight;
};

struct SpecularFaceAdjacency
{
	int face_info_index;
	float base_cost;
};

struct SpecularDonorFace
{
	int room_index;
	int face_index;
	int face_info_index;
	int exact_tmap;
	int split_tmap;
	SpecularFaceBasis basis;
	int source_count;
	specular_instance sources[MAX_SPECULARS];
};

struct SpecularFloodNode
{
	float cost;
	int face_info_index;

	bool operator<(const SpecularFloodNode& other) const
	{
		return cost > other.cost;
	}
};

struct SpecularResolvedDonor
{
	int donor_index;
	float cost;
	float weight;
};

struct SpecularFieldSample
{
	vector position;
	vector direction;
	vector normal;
	float distance;
	float r;
	float g;
	float b;
	float weight;
};

struct SpecularFieldLobe
{
	vector position_sum;
	vector direction_sum;
	vector normal_sum;
	float distance_sum;
	float r_sum;
	float g_sum;
	float b_sum;
	float weight;
};

struct SpecularFieldCell
{
	SpecularFieldLobe lobes[MAX_SPECULARS];
};

struct SpecularSpatialField
{
	vector min_bound;
	vector max_bound;
	float cell_size;
	int x_cells;
	int y_cells;
	int z_cells;
	bool sparse;
	std::unordered_map<int, SpecularFieldCell> cells;
	std::vector<int> populated_cell_indices;
	SpecularFieldCell global;
};

static std::vector<std::vector<PrecomputedSpecularFaceSources>> Precomputed_specular_sources;
static int Precomputed_specular_highest_room = -1;

static vector SpecularFaceCenter(room* rp, face* fp)
{
	vector center = { 0, 0, 0 };
	for (int vn = 0; vn < fp->num_verts; vn++)
		center += rp->verts[fp->face_verts[vn]];
	if (fp->num_verts > 0)
		center /= (float)fp->num_verts;
	return center;
}

static int SplitSpecularSourceMatchTmap(int tmap)
{
	SplitSpecularTexturePair pair = FindSplitSpecularTexturePair(tmap);
	return pair.valid ? pair.base_tmap : tmap;
}

static bool SpecularTextureHasOwnOrSplitMask(face* fp)
{
	int bm_handle = GetTextureBitmap(fp->tmap, 0);
	if (bm_handle >= 0 && bm_format(bm_handle) == BITMAP_FORMAT_4444)
		return true;

	return FindSplitSpecularTexturePair(fp->tmap).valid;
}

static float SpecularClamp(float value, float low, float high)
{
	return std::max(low, std::min(value, high));
}

static bool SpecularFaceCanReceivePpxSources(room* rp, face* fp)
{
	if (!rp->used || (rp->flags & RF_EXTERNAL))
		return false;
	if (fp->lmi_handle == BAD_LMI_INDEX || !(fp->flags & FF_LIGHTMAP))
		return false;
	if (!SpecularTextureHasOwnOrSplitMask(fp))
		return false;
	return true;
}

static vector SpecularAnyFaceTangent(room* rp, face* fp, const vector& normal)
{
	for (int vn = 1; vn < fp->num_verts; vn++)
	{
		vector tangent = rp->verts[fp->face_verts[vn]] - rp->verts[fp->face_verts[0]];
		tangent -= normal * (tangent * normal);
		if (vm_NormalizeVector(&tangent) > 0.0001f)
			return tangent;
	}

	vector tangent = { 1.0f, 0.0f, 0.0f };
	if (fabs(tangent * normal) > 0.95f)
		tangent = { 0.0f, 1.0f, 0.0f };
	tangent -= normal * (tangent * normal);
	vm_NormalizeVector(&tangent);
	return tangent;
}

static SpecularFaceBasis SpecularBuildFaceBasis(room* rp, face* fp)
{
	SpecularFaceBasis basis = {};
	basis.center = SpecularFaceCenter(rp, fp);
	basis.normal = fp->normal;
	if (vm_NormalizeVector(&basis.normal) <= 0.0001f)
		basis.normal = { 0.0f, 0.0f, 1.0f };

	basis.tangent = { 0.0f, 0.0f, 0.0f };
	vector uv_bitangent = { 0.0f, 0.0f, 0.0f };
	for (int vn = 2; vn < fp->num_verts; vn++)
	{
		const vector& p0 = rp->verts[fp->face_verts[0]];
		const vector& p1 = rp->verts[fp->face_verts[vn - 1]];
		const vector& p2 = rp->verts[fp->face_verts[vn]];
		vector edge1 = p1 - p0;
		vector edge2 = p2 - p0;
		const float du1 = fp->face_uvls[vn - 1].u - fp->face_uvls[0].u;
		const float dv1 = fp->face_uvls[vn - 1].v - fp->face_uvls[0].v;
		const float du2 = fp->face_uvls[vn].u - fp->face_uvls[0].u;
		const float dv2 = fp->face_uvls[vn].v - fp->face_uvls[0].v;
		const float determinant = du1 * dv2 - du2 * dv1;
		if (fabs(determinant) <= 0.000001f)
			continue;

		const float inv_det = 1.0f / determinant;
		basis.tangent = (edge1 * dv2 - edge2 * dv1) * inv_det;
		uv_bitangent = (edge2 * du1 - edge1 * du2) * inv_det;
		basis.tangent -= basis.normal * (basis.tangent * basis.normal);
		if (vm_NormalizeVector(&basis.tangent) > 0.0001f)
			break;
	}

	if (vm_GetMagnitude(&basis.tangent) <= 0.0001f)
		basis.tangent = SpecularAnyFaceTangent(rp, fp, basis.normal);

	basis.bitangent = basis.normal ^ basis.tangent;
	if (vm_NormalizeVector(&basis.bitangent) <= 0.0001f)
		basis.bitangent = { 0.0f, 1.0f, 0.0f };
	if (vm_GetMagnitude(&uv_bitangent) > 0.0001f && (basis.bitangent * uv_bitangent) < 0.0f)
		basis.bitangent *= -1.0f;

	basis.valid = true;
	return basis;
}

static int SpecularCopyFaceSources(face* fp, specular_instance* sources)
{
	if (fp->special_handle == BAD_SPECIAL_FACE_INDEX)
		return 0;

	int count = 0;
	special_face* sf = &SpecialFaces[fp->special_handle];
	for (int i = 0; i < sf->num && count < MAX_SPECULARS; i++)
	{
		if (sf->spec_instance[i].bright_color == 0)
			continue;
		sources[count++] = sf->spec_instance[i];
	}
	return count;
}

static bool SpecularFacesShareEdge(face* a, face* b)
{
	for (int ai = 0; ai < a->num_verts; ai++)
	{
		const int a0 = a->face_verts[ai];
		const int a1 = a->face_verts[(ai + 1) % a->num_verts];
		for (int bi = 0; bi < b->num_verts; bi++)
		{
			const int b0 = b->face_verts[bi];
			const int b1 = b->face_verts[(bi + 1) % b->num_verts];
			if ((a0 == b1 && a1 == b0) || (a0 == b0 && a1 == b1))
				return true;
		}
	}
	return false;
}

static bool SpecularFacesShareVertex(face* a, face* b)
{
	for (int ai = 0; ai < a->num_verts; ai++)
	{
		for (int bi = 0; bi < b->num_verts; bi++)
		{
			if (a->face_verts[ai] == b->face_verts[bi])
				return true;
		}
	}
	return false;
}

static void SpecularAddFaceAdjacency(std::vector<std::vector<SpecularFaceAdjacency>>& adjacency,
	int a, int b, float base_cost)
{
	adjacency[a].push_back({ b, base_cost });
	adjacency[b].push_back({ a, base_cost });
}

static float SpecularFaceDistance(const SpecularFaceBasis& a, const SpecularFaceBasis& b)
{
	vector delta = a.center - b.center;
	return vm_GetMagnitude(&delta);
}

static float SpecularTransitionCost(const SpecularFaceInfo& from, const SpecularFaceInfo& to,
	const SpecularFaceAdjacency& edge)
{
	const float distance = SpecularFaceDistance(from.basis, to.basis);
	const float normal_dot = SpecularClamp(from.basis.normal * to.basis.normal, -1.0f, 1.0f);
	const float angle_penalty = (1.0f - std::max(normal_dot, 0.0f)) * 8.0f;
	return std::max(distance, 0.05f) * (1.0f + angle_penalty) + edge.base_cost;
}

static float SpecularDonorCompatibilityCost(const SpecularDonorFace& donor,
	const SpecularFaceInfo& target)
{
	const float distance = SpecularFaceDistance(donor.basis, target.basis);
	const float normal_dot = SpecularClamp(donor.basis.normal * target.basis.normal, -1.0f, 1.0f);
	if (normal_dot < -0.5f)
		return std::numeric_limits<float>::max();

	const float angle_penalty = (1.0f - std::max(normal_dot, 0.0f)) * 10.0f;
	return std::max(distance, 0.05f) * (1.0f + angle_penalty) + angle_penalty;
}

static vector SpecularTransformSourcePosition(const SpecularFaceBasis& donor_basis,
	const SpecularFaceBasis& target_basis, const vector& source_position)
{
	vector offset = source_position - donor_basis.center;
	const float tangent_offset = offset * donor_basis.tangent;
	const float bitangent_offset = offset * donor_basis.bitangent;
	const float normal_offset = offset * donor_basis.normal;

	vector transformed = target_basis.center +
		target_basis.tangent * tangent_offset +
		target_basis.bitangent * bitangent_offset +
		target_basis.normal * normal_offset;

	const float normal_dot = SpecularClamp(donor_basis.normal * target_basis.normal, -1.0f, 1.0f);
	if (normal_dot > 0.985f)
		return source_position;
	if (normal_dot > 0.8f)
	{
		const float transformed_weight = (0.985f - normal_dot) / (0.985f - 0.8f);
		return source_position * (1.0f - transformed_weight) + transformed * transformed_weight;
	}

	return transformed;
}

static void SpecularUnpackColor(ushort color, float& r, float& g, float& b)
{
	r = (float)((color >> 10) & 0x1f) / 31.0f;
	g = (float)((color >> 5) & 0x1f) / 31.0f;
	b = (float)(color & 0x1f) / 31.0f;
}

static ushort SpecularPackColor(float r, float g, float b)
{
	const int ri = (int)(SpecularClamp(r, 0.0f, 1.0f) * 31.0f + 0.5f);
	const int gi = (int)(SpecularClamp(g, 0.0f, 1.0f) * 31.0f + 0.5f);
	const int bi = (int)(SpecularClamp(b, 0.0f, 1.0f) * 31.0f + 0.5f);
	return (ushort)((ri << 10) | (gi << 5) | bi);
}

static float SpecularFaceArea(room* rp, face* fp)
{
	float area = 0.0f;
	if (fp->num_verts < 3)
		return 1.0f;

	const vector& p0 = rp->verts[fp->face_verts[0]];
	for (int vn = 2; vn < fp->num_verts; vn++)
	{
		vector edge1 = rp->verts[fp->face_verts[vn - 1]] - p0;
		vector edge2 = rp->verts[fp->face_verts[vn]] - p0;
		vector cross = edge1 ^ edge2;
		area += vm_GetMagnitude(&cross) * 0.5f;
	}
	return std::max(area, 1.0f);
}

static vector SpecularFieldNormalizeOr(const vector& value, const vector& fallback)
{
	vector result = value;
	if (vm_NormalizeVector(&result) <= 0.0001f)
		return fallback;
	return result;
}

static int SpecularFieldIndex(const SpecularSpatialField& field, int x, int y, int z)
{
	return (z * field.y_cells + y) * field.x_cells + x;
}

static SpecularFieldCell& SpecularFieldEnsureCell(SpecularSpatialField& field, int x, int y, int z)
{
	const int index = SpecularFieldIndex(field, x, y, z);
	auto result = field.cells.emplace(index, SpecularFieldCell{});
	if (result.second)
		field.populated_cell_indices.push_back(index);
	return result.first->second;
}

static void SpecularFieldPopulateDenseCells(SpecularSpatialField& field)
{
	field.cells.reserve(field.x_cells * field.y_cells * field.z_cells);
	field.populated_cell_indices.reserve(field.x_cells * field.y_cells * field.z_cells);
	for (int z = 0; z < field.z_cells; z++)
	{
		for (int y = 0; y < field.y_cells; y++)
		{
			for (int x = 0; x < field.x_cells; x++)
				SpecularFieldEnsureCell(field, x, y, z);
		}
	}
}

static const SpecularFieldCell* SpecularFieldFindCell(const SpecularSpatialField& field, int x, int y, int z)
{
	auto iter = field.cells.find(SpecularFieldIndex(field, x, y, z));
	return iter == field.cells.end() ? nullptr : &iter->second;
}

static int SpecularFieldClampCell(int value, int max_value)
{
	if (value < 0)
		return 0;
	if (value >= max_value)
		return max_value - 1;
	return value;
}

static void SpecularFieldCellForPosition(const SpecularSpatialField& field, const vector& position,
	int& x, int& y, int& z)
{
	x = SpecularFieldClampCell((int)floor((position.x - field.min_bound.x) / field.cell_size), field.x_cells);
	y = SpecularFieldClampCell((int)floor((position.y - field.min_bound.y) / field.cell_size), field.y_cells);
	z = SpecularFieldClampCell((int)floor((position.z - field.min_bound.z) / field.cell_size), field.z_cells);
}

static vector SpecularFieldCellCenter(const SpecularSpatialField& field, int x, int y, int z)
{
	vector center;
	center.x = field.min_bound.x + ((float)x + 0.5f) * field.cell_size;
	center.y = field.min_bound.y + ((float)y + 0.5f) * field.cell_size;
	center.z = field.min_bound.z + ((float)z + 0.5f) * field.cell_size;
	return center;
}

static vector SpecularFieldCellCenterFromIndex(const SpecularSpatialField& field, int index)
{
	const int x = index % field.x_cells;
	const int yz = index / field.x_cells;
	const int y = yz % field.y_cells;
	const int z = yz / field.y_cells;
	return SpecularFieldCellCenter(field, x, y, z);
}

static float SpecularFieldSmoothKernel(float distance, float radius)
{
	if (radius <= 0.0f)
		return 0.0f;
	const float t = SpecularClamp(distance / radius, 0.0f, 1.0f);
	const float smooth = t * t * (3.0f - 2.0f * t);
	const float weight = 1.0f - smooth;
	return weight * weight;
}

static bool SpecularFieldHasLobes(const SpecularFieldCell& cell)
{
	for (int i = 0; i < MAX_SPECULARS; i++)
	{
		if (cell.lobes[i].weight > 0.0f)
			return true;
	}
	return false;
}

static void SpecularFieldAddWeightedSample(SpecularFieldCell& cell, const SpecularFieldSample& sample,
	float weight)
{
	if (weight <= 0.0f)
		return;

	int best_lobe = -1;
	int weakest_lobe = 0;
	float best_score = -10.0f;
	float weakest_weight = cell.lobes[0].weight;
	for (int i = 0; i < MAX_SPECULARS; i++)
	{
		SpecularFieldLobe& lobe = cell.lobes[i];
		if (lobe.weight <= 0.0f)
		{
			best_lobe = i;
			break;
		}

		vector lobe_direction = SpecularFieldNormalizeOr(lobe.direction_sum, sample.direction);
		vector lobe_normal = SpecularFieldNormalizeOr(lobe.normal_sum, sample.normal);
		const float direction_dot = SpecularClamp(lobe_direction * sample.direction, -1.0f, 1.0f);
		const float normal_dot = SpecularClamp(lobe_normal * sample.normal, -1.0f, 1.0f);
		const float score = direction_dot * 0.75f + normal_dot * 0.25f;
		if (score > best_score)
		{
			best_score = score;
			best_lobe = i;
		}
		if (lobe.weight < weakest_weight)
		{
			weakest_weight = lobe.weight;
			weakest_lobe = i;
		}
	}

	if (best_lobe < 0)
		best_lobe = weakest_lobe;
	if (best_score < 0.25f && cell.lobes[best_lobe].weight > 0.0f && weight > weakest_weight)
	{
		best_lobe = weakest_lobe;
		cell.lobes[best_lobe] = {};
	}

	SpecularFieldLobe& lobe = cell.lobes[best_lobe];
	lobe.position_sum += sample.position * weight;
	lobe.direction_sum += sample.direction * weight;
	lobe.normal_sum += sample.normal * weight;
	lobe.distance_sum += sample.distance * weight;
	lobe.r_sum += sample.r * weight;
	lobe.g_sum += sample.g * weight;
	lobe.b_sum += sample.b * weight;
	lobe.weight += weight;
}

static void SpecularBuildSpatialField(SpecularSpatialField& field, const std::vector<SpecularFaceInfo>& faces,
	const std::vector<SpecularFieldSample>& samples)
{
	field = {};
	if (faces.empty())
		return;

	field.min_bound = faces[0].basis.center;
	field.max_bound = faces[0].basis.center;
	for (const SpecularFaceInfo& info : faces)
	{
		const vector& center = info.basis.center;
		field.min_bound.x = std::min(field.min_bound.x, center.x);
		field.min_bound.y = std::min(field.min_bound.y, center.y);
		field.min_bound.z = std::min(field.min_bound.z, center.z);
		field.max_bound.x = std::max(field.max_bound.x, center.x);
		field.max_bound.y = std::max(field.max_bound.y, center.y);
		field.max_bound.z = std::max(field.max_bound.z, center.z);
	}

	vector extent = field.max_bound - field.min_bound;
	const float max_extent = std::max(extent.x, std::max(extent.y, extent.z));
	const float field_resolution =
		ConfigNormalizePerPixelSpecularFieldResolution(Render_per_pixel_specular_field_resolution);
	field.sparse = Render_per_pixel_sparse_specular_field;
	field.cell_size = SpecularClamp(max_extent / field_resolution, 6.0f, 120.0f);
	if (field.cell_size <= 0.0f)
		field.cell_size = 20.0f;

	const int max_axis_cells = field.sparse ? 256 : 48;
	bool dense_cap_applied = false;
	if (!field.sparse)
	{
		const int requested_x_cells = (int)ceil(std::max(extent.x, 1.0f) / field.cell_size) + 3;
		const int requested_y_cells = (int)ceil(std::max(extent.y, 1.0f) / field.cell_size) + 3;
		const int requested_z_cells = (int)ceil(std::max(extent.z, 1.0f) / field.cell_size) + 3;
		if (requested_x_cells > max_axis_cells ||
			requested_y_cells > max_axis_cells ||
			requested_z_cells > max_axis_cells)
		{
			const float capped_resolution = (float)(max_axis_cells - 3);
			field.cell_size = SpecularClamp(max_extent / capped_resolution, 6.0f, 120.0f);
			dense_cap_applied = true;
		}
	}

	field.x_cells = SpecularFieldClampCell((int)ceil(std::max(extent.x, 1.0f) / field.cell_size), max_axis_cells) + 1;
	field.y_cells = SpecularFieldClampCell((int)ceil(std::max(extent.y, 1.0f) / field.cell_size), max_axis_cells) + 1;
	field.z_cells = SpecularFieldClampCell((int)ceil(std::max(extent.z, 1.0f) / field.cell_size), max_axis_cells) + 1;
	field.min_bound.x -= field.cell_size;
	field.min_bound.y -= field.cell_size;
	field.min_bound.z -= field.cell_size;
	field.x_cells = std::max(1, std::min(field.x_cells + 2, max_axis_cells));
	field.y_cells = std::max(1, std::min(field.y_cells + 2, max_axis_cells));
	field.z_cells = std::max(1, std::min(field.z_cells + 2, max_axis_cells));
	if (field.sparse)
	{
		field.cells.reserve((faces.size() + samples.size()) * 8);
		field.populated_cell_indices.reserve((faces.size() + samples.size()) * 8);
	}
	else
	{
		if (dense_cap_applied)
		{
			mprintf((0, "Warning: dense PPX specular field capped to %dx%dx%d cells; enable Sparse field for full %.1f resolution.\n",
				field.x_cells, field.y_cells, field.z_cells, field_resolution));
		}
		SpecularFieldPopulateDenseCells(field);
	}

	for (const SpecularFieldSample& sample : samples)
	{
		SpecularFieldAddWeightedSample(field.global, sample, sample.weight);

		int cx, cy, cz;
		SpecularFieldCellForPosition(field, sample.position, cx, cy, cz);
		for (int z = std::max(0, cz - 1); z <= std::min(field.z_cells - 1, cz + 1); z++)
		{
			for (int y = std::max(0, cy - 1); y <= std::min(field.y_cells - 1, cy + 1); y++)
			{
				for (int x = std::max(0, cx - 1); x <= std::min(field.x_cells - 1, cx + 1); x++)
				{
					vector delta = SpecularFieldCellCenter(field, x, y, z) - sample.position;
					const float distance = vm_GetMagnitude(&delta);
					const float falloff = std::max(0.0f, 1.0f - distance / (field.cell_size * 1.75f));
					if (falloff <= 0.0f)
						continue;
					SpecularFieldAddWeightedSample(SpecularFieldEnsureCell(field, x, y, z),
						sample, sample.weight * falloff * falloff);
				}
			}
		}
	}
}

static void SpecularFieldAddLobeToOutput(SpecularFieldCell& output, const SpecularFieldLobe& source,
	const vector& target_normal, float base_weight)
{
	if (source.weight <= 0.0f || base_weight <= 0.0f)
		return;

	SpecularFieldSample sample = {};
	sample.position = source.position_sum / source.weight;
	sample.direction = SpecularFieldNormalizeOr(source.direction_sum, target_normal);
	sample.normal = SpecularFieldNormalizeOr(source.normal_sum, target_normal);
	sample.distance = source.distance_sum / source.weight;
	sample.r = source.r_sum / source.weight;
	sample.g = source.g_sum / source.weight;
	sample.b = source.b_sum / source.weight;

	const float normal_dot = SpecularClamp(sample.normal * target_normal, -1.0f, 1.0f);
	if (normal_dot < -0.2f)
		return;

	const float normal_weight = 0.12f + std::max(normal_dot, 0.0f) * std::max(normal_dot, 0.0f);
	SpecularFieldAddWeightedSample(output, sample, base_weight * normal_weight);
}

static void SpecularBuildFieldSourceSet(PrecomputedSpecularSourceSet& set, const SpecularFaceInfo& target,
	const SpecularSpatialField& field, const vector& target_position, const vector& target_normal)
{
	set.count = 0;
	memset(set.sources, 0, sizeof(set.sources));
	if (field.cells.empty() || !SpecularFieldHasLobes(field.global))
		return;

	SpecularFieldCell output = {};
	const float query_radius =
		ConfigNormalizePerPixelSpecularFieldSampleDistance(Render_per_pixel_specular_field_sample_distance);

	auto add_cell_to_output = [&](int cell_index, const SpecularFieldCell& cell) {
		vector delta = SpecularFieldCellCenterFromIndex(field, cell_index) - target_position;
		const float distance = vm_GetMagnitude(&delta);
		const float spatial_weight = SpecularFieldSmoothKernel(distance, query_radius);
		if (spatial_weight <= 0.0f)
			return;
		for (int lobe_index = 0; lobe_index < MAX_SPECULARS; lobe_index++)
			SpecularFieldAddLobeToOutput(output, cell.lobes[lobe_index], target_normal, spatial_weight);
	};

	if (field.sparse)
	{
		for (int cell_index : field.populated_cell_indices)
		{
			const auto cell_iter = field.cells.find(cell_index);
			if (cell_iter == field.cells.end())
				continue;

			add_cell_to_output(cell_index, cell_iter->second);
		}
	}
	else
	{
		int cx, cy, cz;
		SpecularFieldCellForPosition(field, target_position, cx, cy, cz);
		const int query_radius_cells = std::max(1,
			std::min(24, (int)ceil(query_radius / std::max(field.cell_size, 0.001f))));
		for (int z = std::max(0, cz - query_radius_cells);
			z <= std::min(field.z_cells - 1, cz + query_radius_cells); z++)
		{
			for (int y = std::max(0, cy - query_radius_cells);
				y <= std::min(field.y_cells - 1, cy + query_radius_cells); y++)
			{
				for (int x = std::max(0, cx - query_radius_cells);
					x <= std::min(field.x_cells - 1, cx + query_radius_cells); x++)
				{
					const SpecularFieldCell* cell = SpecularFieldFindCell(field, x, y, z);
					if (cell == nullptr)
						continue;

					add_cell_to_output(SpecularFieldIndex(field, x, y, z), *cell);
				}
			}
		}
	}

	for (int lobe_index = 0; lobe_index < MAX_SPECULARS; lobe_index++)
		SpecularFieldAddLobeToOutput(output, field.global.lobes[lobe_index], target_normal, 0.08f);

	for (int a = 0; a < MAX_SPECULARS - 1; a++)
	{
		for (int b = a + 1; b < MAX_SPECULARS; b++)
		{
			if (output.lobes[b].weight > output.lobes[a].weight)
				std::swap(output.lobes[a], output.lobes[b]);
		}
	}

	const float strongest_lobe_weight = output.lobes[0].weight;
	for (int lobe_index = 0; lobe_index < MAX_SPECULARS && set.count < MAX_SPECULARS; lobe_index++)
	{
		const SpecularFieldLobe& lobe = output.lobes[lobe_index];
		if (lobe.weight <= 0.0f)
			continue;

		const vector source_anchor = lobe.position_sum / lobe.weight;
		vector direction = SpecularFieldNormalizeOr(lobe.direction_sum, target_normal);
		const float distance = SpecularClamp(lobe.distance_sum / lobe.weight, 40.0f, 800.0f);
		const float lobe_strength = strongest_lobe_weight > 0.0f ?
			sqrt(SpecularClamp(lobe.weight / strongest_lobe_weight, 0.0f, 1.0f)) : 1.0f;
		specular_instance resolved = {};
		resolved.bright_center = source_anchor + direction * distance;
		resolved.bright_color = SpecularPackColor((lobe.r_sum / lobe.weight) * lobe_strength,
			(lobe.g_sum / lobe.weight) * lobe_strength,
			(lobe.b_sum / lobe.weight) * lobe_strength);
		if (resolved.bright_color != 0)
			set.sources[set.count++] = resolved;
	}
}

static void SpecularRunSourceFlood(const std::vector<SpecularFaceInfo>& faces,
	const std::vector<std::vector<SpecularFaceAdjacency>>& adjacency,
	const std::vector<SpecularDonorFace>& donors, bool split_match,
	std::vector<float>& costs, std::vector<int>& donor_indices)
{
	costs.assign(faces.size(), std::numeric_limits<float>::max());
	donor_indices.assign(faces.size(), -1);

	std::priority_queue<SpecularFloodNode> queue;
	for (int donor_index = 0; donor_index < (int)donors.size(); donor_index++)
	{
		const SpecularDonorFace& donor = donors[donor_index];
		const int face_info_index = donor.face_info_index;
		if (face_info_index < 0 || face_info_index >= (int)faces.size())
			continue;

		costs[face_info_index] = 0.0f;
		donor_indices[face_info_index] = donor_index;
		queue.push({ 0.0f, face_info_index });
	}

	while (!queue.empty())
	{
		const SpecularFloodNode node = queue.top();
		queue.pop();
		if (node.cost != costs[node.face_info_index])
			continue;

		const SpecularFaceInfo& current = faces[node.face_info_index];
		const int current_family = split_match ? current.split_tmap : current.exact_tmap;
		const int source_donor = donor_indices[node.face_info_index];
		if (source_donor < 0)
			continue;

		for (const SpecularFaceAdjacency& edge : adjacency[node.face_info_index])
		{
			const SpecularFaceInfo& next = faces[edge.face_info_index];
			const int next_family = split_match ? next.split_tmap : next.exact_tmap;
			if (next_family != current_family)
				continue;

			const float next_cost = node.cost + SpecularTransitionCost(current, next, edge);
			if (next_cost >= costs[edge.face_info_index])
				continue;

			costs[edge.face_info_index] = next_cost;
			donor_indices[edge.face_info_index] = source_donor;
			queue.push({ next_cost, edge.face_info_index });
		}
	}
}

static bool SpecularResolvedDonorAlreadySelected(const std::vector<SpecularResolvedDonor>& selected,
	int donor_index)
{
	for (const SpecularResolvedDonor& donor : selected)
	{
		if (donor.donor_index == donor_index)
			return true;
	}
	return false;
}

static bool SpecularResolvedDonorIsDiverse(const std::vector<SpecularResolvedDonor>& selected,
	const std::vector<SpecularDonorFace>& donors, const SpecularDonorFace& candidate,
	const SpecularFaceInfo& target)
{
	if (selected.empty())
		return true;

	vector target_delta = candidate.basis.center - target.basis.center;
	const float target_distance = std::max(vm_GetMagnitude(&target_delta), 1.0f);
	for (const SpecularResolvedDonor& selected_donor : selected)
	{
		vector donor_delta = candidate.basis.center - donors[selected_donor.donor_index].basis.center;
		if (vm_GetMagnitude(&donor_delta) > target_distance * 0.65f)
			return true;
	}
	return false;
}

static void SpecularSelectResolvedDonors(const SpecularFaceInfo& target,
	const std::vector<SpecularDonorFace>& donors, int family, bool split_match,
	int flood_donor, float flood_cost, std::vector<SpecularResolvedDonor>& selected)
{
	selected.clear();
	if (flood_donor >= 0 && flood_donor < (int)donors.size())
	{
		const float cost = std::max(flood_cost, SpecularDonorCompatibilityCost(donors[flood_donor], target));
		selected.push_back({ flood_donor, cost, 1.0f / (1.0f + cost) });
	}

	std::vector<SpecularResolvedDonor> candidates;
	for (int donor_index = 0; donor_index < (int)donors.size(); donor_index++)
	{
		const SpecularDonorFace& donor = donors[donor_index];
		if ((split_match ? donor.split_tmap : donor.exact_tmap) != family)
			continue;
		if (donor.room_index == target.room_index && donor.face_index == target.face_index)
			continue;
		if (SpecularResolvedDonorAlreadySelected(selected, donor_index))
			continue;

		const float cost = SpecularDonorCompatibilityCost(donor, target);
		if (cost == std::numeric_limits<float>::max())
			continue;
		candidates.push_back({ donor_index, cost, 1.0f / (1.0f + cost) });
	}

	std::sort(candidates.begin(), candidates.end(),
		[](const SpecularResolvedDonor& a, const SpecularResolvedDonor& b) {
			return a.cost < b.cost;
		});

	const float best_cost = !selected.empty() ? selected[0].cost :
		(!candidates.empty() ? candidates[0].cost : std::numeric_limits<float>::max());
	for (const SpecularResolvedDonor& candidate : candidates)
	{
		if (selected.size() >= 3)
			break;
		if (candidate.cost > best_cost * 2.25f + 4.0f)
			break;
		if (!SpecularResolvedDonorIsDiverse(selected, donors, donors[candidate.donor_index], target) &&
			!selected.empty() && candidate.cost >= selected[0].cost)
			continue;
		selected.push_back(candidate);
	}
}

static void SpecularBuildResolvedSourceSet(PrecomputedSpecularSourceSet& set,
	const SpecularFaceInfo& target, const std::vector<SpecularDonorFace>& donors,
	const std::vector<SpecularResolvedDonor>& selected)
{
	set.count = 0;
	memset(set.sources, 0, sizeof(set.sources));
	if (selected.empty())
		return;

	for (int source_slot = 0; source_slot < MAX_SPECULARS; source_slot++)
	{
		vector center = { 0.0f, 0.0f, 0.0f };
		float r = 0.0f, g = 0.0f, b = 0.0f;
		float total_weight = 0.0f;

		for (const SpecularResolvedDonor& selected_donor : selected)
		{
			const SpecularDonorFace& donor = donors[selected_donor.donor_index];
			if (source_slot >= donor.source_count)
				continue;

			const specular_instance& source = donor.sources[source_slot];
			if (source.bright_color == 0)
				continue;

			const vector transformed_center =
				SpecularTransformSourcePosition(donor.basis, target.basis, source.bright_center);
			const float weight = selected_donor.weight;
			center += transformed_center * weight;

			float sr, sg, sb;
			SpecularUnpackColor(source.bright_color, sr, sg, sb);
			r += sr * weight;
			g += sg * weight;
			b += sb * weight;
			total_weight += weight;
		}

		if (total_weight <= 0.0f)
			continue;

		specular_instance resolved = {};
		resolved.bright_center = center / total_weight;
		resolved.bright_color = SpecularPackColor(r / total_weight, g / total_weight, b / total_weight);
		if (resolved.bright_color != 0 && set.count < MAX_SPECULARS)
			set.sources[set.count++] = resolved;
	}
}

static void SpecularBuildResolvedNormalsForFace(PrecomputedSpecularFaceSources& cached,
	const SpecularFaceInfo& target, const std::vector<std::vector<SpecularFaceAdjacency>>& adjacency,
	const std::vector<SpecularFaceInfo>& faces)
{
	room* rp = &Rooms[target.room_index];
	face* fp = &rp->faces[target.face_index];
	cached.normals.clear();
	cached.normals.resize(fp->num_verts);

	for (int vn = 0; vn < fp->num_verts; vn++)
	{
		vector base_normal = target.basis.normal;
		if (FaceHasSmoothSpecularNormals(fp))
			base_normal = SpecialFaces[fp->special_handle].vertnorms[vn];
		if (vm_NormalizeVector(&base_normal) <= 0.0001f)
			base_normal = target.basis.normal;

		vector normal_sum = base_normal;
		float total_weight = 1.0f;
		const int vert_index = fp->face_verts[vn];

		for (const SpecularFaceAdjacency& edge : adjacency[target.face_info_index])
		{
			const SpecularFaceInfo& neighbor_info = faces[edge.face_info_index];
			room* neighbor_room = &Rooms[neighbor_info.room_index];
			face* neighbor_face = &neighbor_room->faces[neighbor_info.face_index];
			if (neighbor_info.split_tmap != target.split_tmap)
				continue;
			if (neighbor_room != rp)
				continue;

			bool shares_vertex = false;
			vector neighbor_normal = neighbor_info.basis.normal;
			for (int nv = 0; nv < neighbor_face->num_verts; nv++)
			{
				if (neighbor_face->face_verts[nv] == vert_index)
				{
					shares_vertex = true;
					if (FaceHasSmoothSpecularNormals(neighbor_face))
						neighbor_normal = SpecialFaces[neighbor_face->special_handle].vertnorms[nv];
					break;
				}
			}
			if (!shares_vertex)
				continue;

			if (vm_NormalizeVector(&neighbor_normal) <= 0.0001f)
				neighbor_normal = neighbor_info.basis.normal;

			const float normal_dot = SpecularClamp(target.basis.normal * neighbor_normal, -1.0f, 1.0f);
			if (normal_dot < 0.35f)
				continue;

			const float weight = normal_dot * normal_dot;
			normal_sum += neighbor_normal * weight;
			total_weight += weight;
		}

		normal_sum /= total_weight;
		if (vm_NormalizeVector(&normal_sum) <= 0.0001f)
			normal_sum = target.basis.normal;
		cached.normals[vn] = normal_sum;
	}
}

void PrecomputeMineSpecularSources()
{
	ResetSplitSpecularTexturePairCache();
	Precomputed_specular_sources.clear();
	Precomputed_specular_highest_room = Highest_room_index;

	if (Highest_room_index < 0)
		return;

	Precomputed_specular_sources.resize(Highest_room_index + 1);
	std::vector<SpecularFaceInfo> faces;
	std::vector<std::vector<int>> face_lookup(Highest_room_index + 1);
	std::vector<SpecularDonorFace> donors;
	std::vector<SpecularFieldSample> field_samples;

	for (int room_index = 0; room_index <= Highest_room_index; room_index++)
	{
		room* rp = &Rooms[room_index];
		if (!rp->used)
			continue;

		Precomputed_specular_sources[room_index].resize(rp->num_faces);
		face_lookup[room_index].assign(rp->num_faces, -1);
		if (rp->flags & RF_EXTERNAL)
			continue;

		for (int face_index = 0; face_index < rp->num_faces; face_index++)
		{
			face* fp = &rp->faces[face_index];
			if (!SpecularFaceCanReceivePpxSources(rp, fp))
				continue;

			SpecularFaceInfo info = {};
			info.room_index = room_index;
			info.face_index = face_index;
			info.face_info_index = (int)faces.size();
			info.exact_tmap = fp->tmap;
			info.split_tmap = SplitSpecularSourceMatchTmap(fp->tmap);
			info.basis = SpecularBuildFaceBasis(rp, fp);
			info.field_weight = std::max(0.25f, sqrt(SpecularFaceArea(rp, fp)));
			face_lookup[room_index][face_index] = info.face_info_index;
			faces.push_back(info);

			specular_instance sources[MAX_SPECULARS] = {};
			const int source_count = SpecularCopyFaceSources(fp, sources);
			if (source_count > 0)
			{
				SpecularDonorFace donor = {};
				donor.room_index = room_index;
				donor.face_index = face_index;
				donor.face_info_index = face_lookup[room_index][face_index];
				donor.exact_tmap = info.exact_tmap;
				donor.split_tmap = info.split_tmap;
				donor.basis = info.basis;
				donor.source_count = source_count;
				for (int i = 0; i < source_count; i++)
					donor.sources[i] = sources[i];
				donors.push_back(donor);

				const float face_weight = info.field_weight;
				for (int i = 0; i < source_count; i++)
				{
					vector direction = sources[i].bright_center - info.basis.center;
					const float source_distance = vm_NormalizeVector(&direction);
					if (source_distance <= 0.1f)
						continue;

					float r, g, b;
					SpecularUnpackColor(sources[i].bright_color, r, g, b);
					const float intensity = (r + g + b) / 3.0f;
					SpecularFieldSample sample = {};
					sample.position = info.basis.center;
					sample.direction = direction;
					sample.normal = info.basis.normal;
					sample.distance = source_distance;
					sample.r = r;
					sample.g = g;
					sample.b = b;
					sample.weight = face_weight * (0.25f + intensity);
					field_samples.push_back(sample);
				}
			}
		}
	}

	std::vector<std::vector<SpecularFaceAdjacency>> adjacency(faces.size());
	for (int room_index = 0; room_index <= Highest_room_index; room_index++)
	{
		room* rp = &Rooms[room_index];
		if (!rp->used || (rp->flags & RF_EXTERNAL) || room_index >= (int)face_lookup.size())
			continue;

		for (int a = 0; a < rp->num_faces; a++)
		{
			const int a_info = face_lookup[room_index][a];
			if (a_info < 0)
				continue;
			for (int b = a + 1; b < rp->num_faces; b++)
			{
				const int b_info = face_lookup[room_index][b];
				if (b_info < 0)
					continue;

				if (SpecularFacesShareEdge(&rp->faces[a], &rp->faces[b]))
					SpecularAddFaceAdjacency(adjacency, a_info, b_info, 0.0f);
				else if (SpecularFacesShareVertex(&rp->faces[a], &rp->faces[b]))
					SpecularAddFaceAdjacency(adjacency, a_info, b_info, 3.5f);
			}
		}
	}

	std::vector<float> exact_costs;
	std::vector<int> exact_donors;
	std::vector<float> split_costs;
	std::vector<int> split_donors;
	SpecularRunSourceFlood(faces, adjacency, donors, false, exact_costs, exact_donors);
	SpecularRunSourceFlood(faces, adjacency, donors, true, split_costs, split_donors);

	SpecularSpatialField spatial_field;
	SpecularBuildSpatialField(spatial_field, faces, field_samples);

	auto build_precomputed_face_sources = [&](int face_info_index)
	{
		const SpecularFaceInfo& info = faces[face_info_index];
		PrecomputedSpecularFaceSources& cached =
			Precomputed_specular_sources[info.room_index][info.face_index];

		std::vector<SpecularResolvedDonor> selected;
		SpecularSelectResolvedDonors(info, donors, info.exact_tmap, false,
			exact_donors[face_info_index], exact_costs[face_info_index], selected);
		SpecularBuildResolvedSourceSet(cached.exact, info, donors, selected);

		SpecularSelectResolvedDonors(info, donors, info.split_tmap, true,
			split_donors[face_info_index], split_costs[face_info_index], selected);
		SpecularBuildResolvedSourceSet(cached.split, info, donors, selected);

		SpecularBuildResolvedNormalsForFace(cached, info, adjacency, faces);
	};

	const unsigned int hardware_threads = std::thread::hardware_concurrency();
	const int thread_count = std::max(1, std::min((int)faces.size(),
		std::min(hardware_threads > 0 ? (int)hardware_threads : 2, 16)));
	if (thread_count <= 1 || faces.size() < 32)
	{
		for (int face_info_index = 0; face_info_index < (int)faces.size(); face_info_index++)
			build_precomputed_face_sources(face_info_index);
	}
	else
	{
		std::atomic<int> next_face_info_index(0);
		std::vector<std::thread> workers;
		workers.reserve(thread_count);
		for (int thread_index = 0; thread_index < thread_count; thread_index++)
		{
			workers.emplace_back([&]() {
				for (;;)
				{
					const int face_info_index = next_face_info_index.fetch_add(1);
					if (face_info_index >= (int)faces.size())
						break;
					build_precomputed_face_sources(face_info_index);
				}
			});
		}
		for (std::thread& worker : workers)
			worker.join();
	}

	auto build_precomputed_face_field = [&](int face_info_index)
	{
		const SpecularFaceInfo& info = faces[face_info_index];
		PrecomputedSpecularFaceSources& cached =
			Precomputed_specular_sources[info.room_index][info.face_index];

		cached.field = {};
		SpecularBuildFieldSourceSet(cached.field, info, spatial_field, info.basis.center, info.basis.normal);

		room* rp = &Rooms[info.room_index];
		face* fp = &rp->faces[info.face_index];
		cached.field_vertices.clear();
		cached.field_vertices.resize(fp->num_verts);
		for (int vn = 0; vn < fp->num_verts; vn++)
		{
			vector vertex_normal = info.basis.normal;
			if (vn < (int)cached.normals.size())
				vertex_normal = cached.normals[vn];
			else if (FaceHasSmoothSpecularNormals(fp))
				vertex_normal = SpecialFaces[fp->special_handle].vertnorms[vn];
			if (vm_NormalizeVector(&vertex_normal) <= 0.0001f)
				vertex_normal = info.basis.normal;

			SpecularBuildFieldSourceSet(cached.field_vertices[vn], info, spatial_field,
				rp->verts[fp->face_verts[vn]], vertex_normal);
		}
	};

	if (thread_count <= 1 || faces.size() < 32)
	{
		for (int face_info_index = 0; face_info_index < (int)faces.size(); face_info_index++)
			build_precomputed_face_field(face_info_index);
	}
	else
	{
		std::atomic<int> next_face_info_index(0);
		std::vector<std::thread> workers;
		workers.reserve(thread_count);
		for (int thread_index = 0; thread_index < thread_count; thread_index++)
		{
			workers.emplace_back([&]() {
				for (;;)
				{
					const int face_info_index = next_face_info_index.fetch_add(1);
					if (face_info_index >= (int)faces.size())
						break;
					build_precomputed_face_field(face_info_index);
				}
			});
		}
		for (std::thread& worker : workers)
			worker.join();
	}
}

static void SpecularAddPrecomputedSourceSet(SpecularBlock& specblock, const PrecomputedSpecularSourceSet& set)
{
	for (int i = 0; i < set.count && specblock.num_speculars < MAX_SPECULARS; i++)
		SpecularAddStaticSource(specblock, set.sources[i]);
}

int GetPrecomputedMineSpecularSourceCount(int roomnum, int facenum, bool split_match)
{
	if (roomnum < 0 || roomnum > Precomputed_specular_highest_room ||
		roomnum >= (int)Precomputed_specular_sources.size())
		return 0;
	if (facenum < 0 || facenum >= (int)Precomputed_specular_sources[roomnum].size())
		return 0;

	const PrecomputedSpecularFaceSources& cached = Precomputed_specular_sources[roomnum][facenum];
	return split_match ? cached.split.count : cached.exact.count;
}

int GetPrecomputedMineSpecularFieldSourceCount(int roomnum, int facenum)
{
	if (roomnum < 0 || roomnum > Precomputed_specular_highest_room ||
		roomnum >= (int)Precomputed_specular_sources.size())
		return 0;
	if (facenum < 0 || facenum >= (int)Precomputed_specular_sources[roomnum].size())
		return 0;

	const PrecomputedSpecularFaceSources& cached = Precomputed_specular_sources[roomnum][facenum];
	int count = cached.field.count;
	for (const PrecomputedSpecularSourceSet& vertex_set : cached.field_vertices)
		count = std::max(count, vertex_set.count);
	return count;
}

int GetPrecomputedMineSpecularNormalCount(int roomnum, int facenum)
{
	if (roomnum < 0 || roomnum > Precomputed_specular_highest_room ||
		roomnum >= (int)Precomputed_specular_sources.size())
		return 0;
	if (facenum < 0 || facenum >= (int)Precomputed_specular_sources[roomnum].size())
		return 0;

	return (int)Precomputed_specular_sources[roomnum][facenum].normals.size();
}

static const std::vector<vector>* SpecularGetResolvedNormals(room* rp, int face_index)
{
	int room_index = rp - Rooms;
	if (room_index < 0 || room_index > Precomputed_specular_highest_room ||
		room_index >= (int)Precomputed_specular_sources.size())
		return nullptr;
	if (face_index < 0 || face_index >= (int)Precomputed_specular_sources[room_index].size())
		return nullptr;

	const std::vector<vector>& normals = Precomputed_specular_sources[room_index][face_index].normals;
	return normals.empty() ? nullptr : &normals;
}

static bool SpecularSetFieldStaticSourceCount(room* rp, int current_face_index, SpecularBlock& specblock)
{
	if (!Render_per_pixel_field_static_specular)
		return false;

	int room_index = rp - Rooms;
	if (room_index < 0 || room_index > Precomputed_specular_highest_room ||
		room_index >= (int)Precomputed_specular_sources.size())
		return false;
	if (current_face_index < 0 ||
		current_face_index >= (int)Precomputed_specular_sources[room_index].size())
		return false;

	const PrecomputedSpecularFaceSources& cached =
		Precomputed_specular_sources[room_index][current_face_index];
	int count = cached.field.count;
	for (const PrecomputedSpecularSourceSet& vertex_set : cached.field_vertices)
		count = std::max(count, vertex_set.count);
	specblock.num_speculars = std::min(count, MAX_SPECULARS);
	return specblock.num_speculars > 0;
}

static bool SpecularAddPrecomputedTextureStaticSources(room* rp, int current_face_index,
	SpecularBlock& specblock)
{
	if (!Render_split_specular_textures)
		return false;

	int room_index = rp - Rooms;
	if (room_index < 0 || room_index > Precomputed_specular_highest_room ||
		room_index >= (int)Precomputed_specular_sources.size())
		return false;
	if (current_face_index < 0 ||
		current_face_index >= (int)Precomputed_specular_sources[room_index].size())
		return false;

	const PrecomputedSpecularFaceSources& cached =
		Precomputed_specular_sources[room_index][current_face_index];

	if (Render_split_specular_textures && cached.split.count > 0)
	{
		SpecularAddPrecomputedSourceSet(specblock, cached.split);
		return specblock.num_speculars > 0;
	}

	return false;
}

static void SpecularBuildStaticBlockSources(room* rp, int current_face_index, SpecularBlock& specblock)
{
	face* fp = &rp->faces[current_face_index];
	specblock.num_speculars = 0;

	if (rp->flags & RF_EXTERNAL)
	{
		vector view_center = SpecularWorldToViewPosition(Terrain_sky.satellite_vectors[0]);
		specblock.num_speculars = 1;
		specblock.speculars[0].bright_center[0] = view_center.x;
		specblock.speculars[0].bright_center[1] = view_center.y;
		specblock.speculars[0].bright_center[2] = view_center.z;
		specblock.speculars[0].bright_center[3] = 1.0f;
		specblock.speculars[0].color[0] = Render_per_pixel_static_specular_strength;
		specblock.speculars[0].color[1] = Render_per_pixel_static_specular_strength;
		specblock.speculars[0].color[2] = Render_per_pixel_static_specular_strength;
		specblock.speculars[0].color[3] = 1.0f;
		return;
	}

	if (Render_per_pixel_field_static_specular)
	{
		if (!Render_per_pixel_field_missing_only_static_specular ||
			fp->special_handle == BAD_SPECIAL_FACE_INDEX)
		{
			SpecularSetFieldStaticSourceCount(rp, current_face_index, specblock);
		}
		else
		{
			SpecularAddFaceStaticSources(specblock, fp);
		}
		return;
	}

	if (SpecularAddPrecomputedTextureStaticSources(rp, current_face_index, specblock))
		return;

	SpecularAddFaceStaticSources(specblock, fp);
}

static void PrepareSpecularDynamicLight(renderer_per_pixel_light& light)
{
	vector world_position = { light.position[0], light.position[1], light.position[2] };
	vector view_position = SpecularWorldToViewPosition(world_position);
	light.position[0] = view_position.x;
	light.position[1] = view_position.y;
	light.position[2] = view_position.z;
	if (light.has_specular_position)
	{
		vector specular_position = { light.specular_position[0], light.specular_position[1],
			light.specular_position[2] };
		vector view_specular_position = SpecularWorldToViewPosition(specular_position);
		light.specular_position[0] = view_specular_position.x;
		light.specular_position[1] = view_specular_position.y;
		light.specular_position[2] = view_specular_position.z;
	}

	float strength = light.headlight ?
		Render_per_pixel_headlight_specular_strength :
		Render_per_pixel_dynamic_specular_strength;
	if (light.specular_scalar <= 0.0f)
		light.specular_scalar = 1.0f;
	light.specular_scalar *= strength;
	if (light.directional)
	{
		vector world_direction = { light.direction[0], light.direction[1], light.direction[2] };
		vector view_direction = SpecularWorldToViewDirection(world_direction);
		light.direction[0] = view_direction.x;
		light.direction[1] = view_direction.y;
		light.direction[2] = view_direction.z;
	}
}

static bool UseFieldStaticSpecularForFace(face* fp)
{
	return Render_per_pixel_field_static_specular &&
		(!Render_per_pixel_field_missing_only_static_specular ||
		 fp->special_handle == BAD_SPECIAL_FACE_INDEX);
}

static bool BuildPerPixelSpecularState(room* rp, int face_index, SpecularBlock& specblock,
	renderer_per_pixel_light* dynamic_lights, int& dynamic_light_count)
{
	face* fp = &rp->faces[face_index];
	const int material_type = SpecularMaterialType(fp);

	specblock = {};
	specblock.exponent = TunedSpecularMaterialExponent(material_type);
	specblock.strength = Render_per_pixel_specular_strength *
		GameTextures[SpecularTextureTmapForFace(fp)].reflectivity * 1.5f;
	specblock.lightmap_mix = Render_per_pixel_specular_lightmap_mix;
	specblock.alpha_strength = Render_per_pixel_specular_alpha_strength;
	specblock.pad0 = 0.0f;
	specblock.debug_tint = 0.0f;
	specblock.debug_authored = SpecularFaceHasLocalSources(fp) ? 1.0f : 0.0f;

	SpecularBuildStaticBlockSources(rp, face_index, specblock);
	if (UseFieldStaticSpecularForFace(fp))
	{
		specblock.pad0 = 1.0f;
	}

	dynamic_light_count = GetPerPixelLightmapLights(fp->lmi_handle, dynamic_lights,
		RENDERER_MAX_PER_PIXEL_DYNAMIC_LIGHTS);
	for (int t = 0; t < dynamic_light_count; t++)
	{
		PrepareSpecularDynamicLight(dynamic_lights[t]);
	}

	return specblock.strength > 0.0f && (specblock.num_speculars > 0 || dynamic_light_count > 0);
}

static void SetSpecularNormalsForFace(room* rp, face* fp, g3Point** pointlist)
{
	const bool has_smooth_normals = FaceHasSmoothSpecularNormals(fp);
	const int face_index = fp - rp->faces;
	const std::vector<vector>* resolved_normals = has_smooth_normals ? nullptr :
		SpecularGetResolvedNormals(rp, face_index);
	for (int vn = 0; vn < fp->num_verts; vn++)
	{
		g3Point* p = pointlist[vn];
		if (resolved_normals != nullptr && vn < (int)resolved_normals->size())
		{
			p->p3_specular_normal = (*resolved_normals)[vn];
		}
		else if (has_smooth_normals)
		{
			p->p3_specular_normal = SpecialFaces[fp->special_handle].vertnorms[vn];
		}
		else
		{
			p->p3_specular_normal = fp->normal;
		}
		p->p3_specular_normal_valid = 1;
	}
}

static void ClearFieldSpecularSourcesForPoint(g3Point* p)
{
	p->p3_specular_field_valid = 0;
	p->p3_specular_field_count = 0;
	for (int i = 0; i < G3_MAX_SPECULAR_FIELD_SOURCES; i++)
	{
		p->p3_specular_field_centers[i] = { 0.0f, 0.0f, 0.0f };
		p->p3_specular_field_colors[i] = { 0.0f, 0.0f, 0.0f };
	}
}

static void SetFieldSpecularSourcesForFace(room* rp, face* fp, g3Point** pointlist)
{
	for (int vn = 0; vn < fp->num_verts; vn++)
		ClearFieldSpecularSourcesForPoint(pointlist[vn]);

	if (!UseFieldStaticSpecularForFace(fp))
		return;

	int room_index = rp - Rooms;
	const int face_index = fp - rp->faces;
	if (room_index < 0 || room_index > Precomputed_specular_highest_room ||
		room_index >= (int)Precomputed_specular_sources.size())
		return;
	if (face_index < 0 || face_index >= (int)Precomputed_specular_sources[room_index].size())
		return;

	const PrecomputedSpecularFaceSources& cached =
		Precomputed_specular_sources[room_index][face_index];
	if ((int)cached.field_vertices.size() < fp->num_verts)
		return;

	for (int vn = 0; vn < fp->num_verts; vn++)
	{
		g3Point* p = pointlist[vn];
		const PrecomputedSpecularSourceSet& vertex_sources = cached.field_vertices[vn];
		p->p3_specular_field_valid = 1;
		p->p3_specular_field_count = (ubyte)std::min(vertex_sources.count, MAX_SPECULARS);
		for (int i = 0; i < p->p3_specular_field_count; i++)
		{
			p->p3_specular_field_centers[i] = vertex_sources.sources[i].bright_center;
			float r, g, b;
			SpecularUnpackColor(vertex_sources.sources[i].bright_color, r, g, b);
			p->p3_specular_field_colors[i] = {
				r * Render_per_pixel_static_specular_strength,
				g * Render_per_pixel_static_specular_strength,
				b * Render_per_pixel_static_specular_strength
			};
		}
	}
}

void PopulateRetainedRoomSpecularVertices(room* rp, int facenum,
	RetainedRoomSpecularVertex* vertices, int count)
{
	if (!rp || !vertices || facenum < 0 || facenum >= rp->num_faces)
		return;
	face* fp = &rp->faces[facenum];
	count = std::min(count, (int)fp->num_verts);
	const bool has_smooth_normals = FaceHasSmoothSpecularNormals(fp);
	const std::vector<vector>* resolved_normals = has_smooth_normals ? nullptr :
		SpecularGetResolvedNormals(rp, facenum);
	for (int vn = 0; vn < count; vn++)
	{
		if (resolved_normals && vn < (int)resolved_normals->size())
			vertices[vn].base.normal = (*resolved_normals)[vn];
		else if (has_smooth_normals)
			vertices[vn].base.normal = SpecialFaces[fp->special_handle].vertnorms[vn];
		else
			vertices[vn].base.normal = fp->normal;
	}

	const int room_index = (int)(rp - Rooms);
	if (room_index < 0 || room_index > Precomputed_specular_highest_room ||
		room_index >= (int)Precomputed_specular_sources.size() ||
		facenum >= (int)Precomputed_specular_sources[room_index].size())
	{
		return;
	}
	const PrecomputedSpecularFaceSources& cached =
		Precomputed_specular_sources[room_index][facenum];
	if ((int)cached.field_vertices.size() < count)
		return;
	for (int vn = 0; vn < count; vn++)
	{
		const PrecomputedSpecularSourceSet& sources = cached.field_vertices[vn];
		const int source_count = std::min(sources.count, MAX_SPECULARS);
		for (int i = 0; i < source_count; i++)
		{
			const specular_instance& source = sources.sources[i];
			vertices[vn].field_specular_center[i][0] = source.bright_center.x;
			vertices[vn].field_specular_center[i][1] = source.bright_center.y;
			vertices[vn].field_specular_center[i][2] = source.bright_center.z;
			vertices[vn].field_specular_center[i][3] = 1.0f;
			float r, g, b;
			SpecularUnpackColor(source.bright_color, r, g, b);
			vertices[vn].field_specular_color[i][0] =
				r * Render_per_pixel_static_specular_strength;
			vertices[vn].field_specular_color[i][1] =
				g * Render_per_pixel_static_specular_strength;
			vertices[vn].field_specular_color[i][2] =
				b * Render_per_pixel_static_specular_strength;
			vertices[vn].field_specular_color[i][3] = 1.0f;
		}
	}
}

void PopulateRetainedRoomSpecularPoints(room* rp, int facenum,
	const int* corners, g3Point** points, int count)
{
	if (!rp || !corners || !points || facenum < 0 || facenum >= rp->num_faces)
		return;
	face* fp = &rp->faces[facenum];
	const bool has_smooth_normals = FaceHasSmoothSpecularNormals(fp);
	const std::vector<vector>* resolved_normals = has_smooth_normals ? nullptr :
		SpecularGetResolvedNormals(rp, facenum);
	const int room_index = (int)(rp - Rooms);
	const PrecomputedSpecularFaceSources* cached = nullptr;
	if (room_index >= 0 && room_index <= Precomputed_specular_highest_room &&
		room_index < (int)Precomputed_specular_sources.size() &&
		facenum < (int)Precomputed_specular_sources[room_index].size())
	{
		cached = &Precomputed_specular_sources[room_index][facenum];
	}

	for (int i = 0; i < count; i++)
	{
		const int corner = corners[i];
		g3Point* point = points[i];
		if (corner < 0 || corner >= fp->num_verts || !point)
			continue;
		if (resolved_normals && corner < (int)resolved_normals->size())
			point->p3_specular_normal = (*resolved_normals)[corner];
		else if (has_smooth_normals)
			point->p3_specular_normal = SpecialFaces[fp->special_handle].vertnorms[corner];
		else
			point->p3_specular_normal = fp->normal;
		point->p3_specular_normal_valid = 1;
		point->p3_specular_field_valid = 0;
		point->p3_specular_field_count = 0;
		if (!cached || corner >= (int)cached->field_vertices.size())
			continue;

		const PrecomputedSpecularSourceSet& sources = cached->field_vertices[corner];
		point->p3_specular_field_valid = 1;
		point->p3_specular_field_count = (ubyte)std::min(sources.count, MAX_SPECULARS);
		for (int source_index = 0; source_index < point->p3_specular_field_count;
			source_index++)
		{
			const specular_instance& source = sources.sources[source_index];
			point->p3_specular_field_centers[source_index] = source.bright_center;
			float r, g, b;
			SpecularUnpackColor(source.bright_color, r, g, b);
			point->p3_specular_field_colors[source_index] = {
				r * Render_per_pixel_static_specular_strength,
				g * Render_per_pixel_static_specular_strength,
				b * Render_per_pixel_static_specular_strength
			};
		}
	}
}

void RenderSpecularFacesFlat(room* rp)
{
	static int first = 1;
	static float lm_red[32], lm_green[32], lm_blue[32];
	int num_smooth_faces = 0;
	int num_smooth_used = 0;
	ushort smooth_faces[MAX_FACES_PER_ROOM];
	ushort smooth_used[MAX_VERTS_PER_ROOM];
	ASSERT(Num_specular_faces_to_render > 0);

	if (Num_real_specular_faces_to_render == 0)
	{
		for (int i = 0; i < Num_specular_faces_to_render; i++)
		{
			face* fp = &rp->faces[Specular_faces[i]];
			fp->flags &= ~FF_SPEC_INVISIBLE;
		}
		return;
	}
	if (first)
	{
		first = 0;
		for (int i = 0; i < 32; i++)
		{
			lm_red[i] = (float)i / 31.0;
			lm_green[i] = (float)i / 31.0;
			lm_blue[i] = (float)i / 31.0;
		}
		for (int i = 0; i < MAX_VERTS_PER_ROOM; i++)
		{
			Smooth_verts[i].used = 0;
		}
	}

	g3Point* pointlist[MAX_VERTS_PER_FACE];
	g3Point  pointbuffer[MAX_VERTS_PER_FACE];
	rend_SetOverlayType(OT_NONE);
	rend_SetTextureType(TT_FLAT);
	rend_SetLighting(LS_GOURAUD);
	rend_SetColorModel(CM_RGB);
	rend_SetAlphaType(AT_SPECULAR);
	rend_SetZBufferWriteMask(0);

	const bool per_pixel_shader_specular = Render_preferred_state.per_pixel_lighting &&
		UseHardware && rend_CanUseNewrender();
	if (per_pixel_shader_specular)
	{
		for (int i = 0; i < Num_specular_faces_to_render; i++)
		{
			int face_index = Specular_faces[i];
			face* fp = &rp->faces[face_index];
			if (fp->flags & FF_SPEC_INVISIBLE)
			{
				fp->flags &= ~(FF_SPEC_INVISIBLE);
				continue;
			}
			if (fp->lmi_handle == 65535)
				continue;

			int bm_handle = SpecularBitmapHandle(fp);
			if (!SpecularTextureHasMask(fp))
				continue;

			SpecularBlock specblock = {};
			renderer_per_pixel_light dynamic_lights[RENDERER_MAX_PER_PIXEL_DYNAMIC_LIGHTS];
			int dynamic_light_count = 0;
			if (!BuildPerPixelSpecularState(rp, face_index, specblock, dynamic_lights, dynamic_light_count))
				continue;

			for (int vn = 0; vn < fp->num_verts; vn++)
			{
				int vertnum = fp->face_verts[vn];
				pointbuffer[vn] = World_point_buffer[rp->wpb_index + vertnum];
				g3Point* p = &pointbuffer[vn];
				pointlist[vn] = p;
				p->p3_uvl.u = fp->face_uvls[vn].u;
				p->p3_uvl.v = fp->face_uvls[vn].v;
				p->p3_uvl.u2 = fp->face_uvls[vn].u2;
				p->p3_uvl.v2 = fp->face_uvls[vn].v2;
				p->p3_a = 1.0f;
				p->p3_r = 1.0f;
				p->p3_g = 1.0f;
				p->p3_b = 1.0f;
				p->p3_flags |= PF_RGBA | PF_UV | PF_UV2;
			}
			SetSpecularNormalsForFace(rp, fp, pointlist);
			SetFieldSpecularSourcesForFace(rp, fp, pointlist);
			ubyte specular_clip_codes = 0;
			for (int vn = 0; vn < fp->num_verts; vn++)
				specular_clip_codes |= pointlist[vn]->p3_codes;

			rend_UpdateSpecular(&specblock);
			rend_SetOverlayType(OT_BLEND);
			rend_SetOverlayMap(LightmapInfo[fp->lmi_handle].lm_handle);
			rend_SetPerPixelDynamicLighting(dynamic_light_count > 0 ? &fp->normal : nullptr,
				dynamic_light_count, dynamic_light_count > 0 ? dynamic_lights : nullptr);

			bool retained_drawn = false;
			if (!In_editor_mode && RetainedRoomCanDrawBaseFace(rp, face_index))
			{
				rend_BindBitmap(bm_handle);
				rend_BindLightmap(LightmapInfo[fp->lmi_handle].lm_handle);
				retained_drawn = RetainedRoomDrawFaces(rp, &face_index, 1,
					bm_handle, 0.0f, 0.0f, 1.0f,
					LightmapInfo[fp->lmi_handle].lm_handle,
					specular_clip_codes, true);
			}
			if (!retained_drawn)
			{
				if (fp->flags & FF_TRIANGULATED)
					g3_SetTriangulationTest(1);
				g3_DrawPoly(fp->num_verts, pointlist, bm_handle);
				if (fp->flags & FF_TRIANGULATED)
					g3_SetTriangulationTest(0);
			}
		}

		rend_SetPerPixelDynamicLighting(nullptr, 0, nullptr);
		rend_SetOverlayType(OT_NONE);
		rend_SetZBufferWriteMask(1);
		return;
	}

	for (int i = 0; i < Num_specular_faces_to_render; i++)
	{
		face* fp = &rp->faces[Specular_faces[i]];
		const bool smooth_specular = UseSmoothSpecularForFace(fp);
		const bool has_smooth_normals = FaceHasSmoothSpecularNormals(fp);
		const bool use_smooth_normals = has_smooth_normals &&
			UseSmoothSpecularForFace(fp);

		int material_type = SpecularMaterialType(fp);
		int bm_handle = SpecularBitmapHandle(fp);
		if (bm_format(bm_handle) != BITMAP_FORMAT_4444)
			continue;

		int lm_handle;
		ushort* data;
		int w, h;

		if (fp->lmi_handle == 65535)
		{
			// this face shouldn't be rendered during the specular pass...skip it
			continue;
		}

		renderer_per_pixel_light dynamic_lights[RENDERER_MAX_PER_PIXEL_DYNAMIC_LIGHTS];
		int dynamic_light_count = 0;
		if (Render_preferred_state.per_pixel_lighting && UseHardware && rend_CanUseNewrender())
		{
			dynamic_light_count = GetPerPixelLightmapLights(fp->lmi_handle, dynamic_lights,
				RENDERER_MAX_PER_PIXEL_DYNAMIC_LIGHTS);
		}

		lm_handle = LightmapInfo[fp->lmi_handle].lm_handle;
		data = (ushort*)lm_data(lm_handle);
		w = lm_w(lm_handle);
		h = lm_h(lm_handle);

		for (int vn = 0; vn < fp->num_verts; vn++)
		{
			float rv = 0, gv = 0, bv = 0;
			float scalar = 0;

			float u = fp->face_uvls[vn].u2 * w;
			float v = fp->face_uvls[vn].v2 * h;
			int int_u = u;
			int int_v = v;
			ushort texel = data[int_v * w + int_u];
			int r = (texel >> 10) & 0x1f;
			int g = (texel >> 5) & 0x1f;
			int b = (texel) & 0x1f;
			float vr = lm_red[r];
			float vg = lm_green[g];
			float vb = lm_blue[b];

			vector subvec = Viewer_eye - rp->verts[fp->face_verts[vn]];
			vm_NormalizeVectorFast(&subvec);
			const vector& spec_normal = use_smooth_normals ? SpecialFaces[fp->special_handle].vertnorms[vn] : fp->normal;
			if (!(rp->flags & RF_EXTERNAL))
			{
				int limit = SpecialFaces[fp->special_handle].num;
				int spec_index = limit - 1;
				assert(limit <= 4);

				for (int t = 0; t < limit; t++)
				{
					// Use regular static lighting
					ushort color = SpecialFaces[fp->special_handle].spec_instance[t].bright_color;
					if (color == 0)
						continue;
					vector incident_norm = rp->verts[fp->face_verts[vn]] - SpecialFaces[fp->special_handle].spec_instance[t].bright_center;
					float spec_scalar = Specular_scalars[t];

					float cr = (float)((color >> 10) & 0x1f) / 31.0f;
					float cg = (float)((color >> 5) & 0x1f) / 31.0f;
					float cb = (float)(color & 0x1f) / 31.0f;
					AddSpecularContribution(subvec, incident_norm, spec_normal, material_type, spec_scalar,
						cr, cg, cb, rv, gv, bv);
				}
			}
			else
			{
				vector incident_norm = rp->verts[fp->face_verts[vn]] - Terrain_sky.satellite_vectors[0];
				vm_NormalizeVectorFast(&incident_norm);

				float d = incident_norm * fp->normal;
				vector upvec = d * fp->normal;
				incident_norm -= (2 * upvec);
				float dotp = subvec * incident_norm;
				if (dotp > 1)
					dotp = 1;

				if (dotp > 0)
				{
					int index = ((float)(MAX_SPECULAR_INCREMENTS - 1) * dotp);
					scalar = Specular_tables[material_type][index];
					rv = scalar;
					gv = scalar;
					bv = scalar;
				}
			}
			for (int t = 0; t < dynamic_light_count; t++)
			{
				AddDynamicSpecularContribution(dynamic_lights[t], rp->verts[fp->face_verts[vn]], subvec,
					spec_normal, material_type, rv, gv, bv);
			}
			// Finally, brighten these value up a bit
			if (smooth_specular)
			{
				rv = rv * vr;
				gv = gv * vg;
				bv = bv * vb;
				if (Smooth_verts[fp->face_verts[vn]].used == 0)
				{
					Smooth_verts[fp->face_verts[vn]].used = 1;
					Smooth_verts[fp->face_verts[vn]].r = rv;
					Smooth_verts[fp->face_verts[vn]].g = gv;
					Smooth_verts[fp->face_verts[vn]].b = bv;
					smooth_used[num_smooth_used++] = fp->face_verts[vn];
				}
				else
				{
					Smooth_verts[fp->face_verts[vn]].r += rv;
					Smooth_verts[fp->face_verts[vn]].g += gv;
					Smooth_verts[fp->face_verts[vn]].b += bv;
				}
			}
			else
			{
				rv = std::min(1.0f, rv * vr * 4.0f);
				gv = std::min(1.0f, gv * vg * 4.0f);
				bv = std::min(1.0f, bv * vb * 4.0f);
				pointbuffer[vn] = World_point_buffer[rp->wpb_index + fp->face_verts[vn]];
				g3Point* p = &pointbuffer[vn];
				pointlist[vn] = p;
				p->p3_uvl.u = fp->face_uvls[vn].u;
				p->p3_uvl.v = fp->face_uvls[vn].v;
				p->p3_a = 1.0;
				p->p3_r = rv;
				p->p3_g = gv;
				p->p3_b = bv;
				p->p3_flags |= PF_RGBA | PF_UV;
			}
		}
		if (smooth_specular)
		{
			smooth_faces[num_smooth_faces] = fp - rp->faces;
			num_smooth_faces++;
		}
		else
		{
			if (fp->flags & FF_TRIANGULATED)
				g3_SetTriangulationTest(1);

			g3_DrawPoly(fp->num_verts, pointlist, bm_handle);

			if (fp->flags & FF_TRIANGULATED)
				g3_SetTriangulationTest(0);
		}
	}
	// Now draw smooth specular faces
	for (int i = 0; i < num_smooth_faces; i++)
	{
		face* fp = &rp->faces[smooth_faces[i]];
		if (fp->flags & FF_SPEC_INVISIBLE)
		{
			fp->flags &= ~(FF_SPEC_INVISIBLE);
			continue;
		}
		int bm_handle = SpecularBitmapHandle(fp);
		if (bm_format(bm_handle) != BITMAP_FORMAT_4444)
			continue;
		float reflect = GameTextures[SpecularTextureTmapForFace(fp)].reflectivity * 1.5;
		for (int vn = 0; vn < fp->num_verts; vn++)
		{
			pointbuffer[vn] = World_point_buffer[rp->wpb_index + fp->face_verts[vn]];
			g3Point* p = &pointbuffer[vn];
			pointlist[vn] = p;
			p->p3_uvl.u = fp->face_uvls[vn].u;
			p->p3_uvl.v = fp->face_uvls[vn].v;
			p->p3_a = 1.0;
			p->p3_r = std::min(1.0f, Smooth_verts[fp->face_verts[vn]].r * reflect);
			p->p3_g = std::min(1.0f, Smooth_verts[fp->face_verts[vn]].g * reflect);
			p->p3_b = std::min(1.0f, Smooth_verts[fp->face_verts[vn]].b * reflect);
			p->p3_flags |= PF_RGBA | PF_UV;
		}

		if (fp->flags & FF_TRIANGULATED)
			g3_SetTriangulationTest(1);

		g3_DrawPoly(fp->num_verts, pointlist, bm_handle);

		if (fp->flags & FF_TRIANGULATED)
			g3_SetTriangulationTest(0);
	}
	for (int i = 0; i < num_smooth_used; i++)
	{
		Smooth_verts[smooth_used[i]].used = 0;
	}
	rend_SetZBufferWriteMask(1);
}

// Adds a specular face to draw after the mine has been drawn
void UpdateSpecularFace(room* rp, face* fp)
{
	/*if (!(rp->flags & RF_EXTERNAL)
	{
		int handle=GetSpecularLightmapForFace (&Viewer_eye,rp,fp);
		if (handle<0)
			return;
	}*/
	int n = Num_specular_faces_to_render;
	if (n >= MAX_SPECULAR_FACES)
		return;
	Specular_faces[n] = fp - rp->faces;
	Num_specular_faces_to_render++;
	if (!(fp->flags & FF_SPEC_INVISIBLE))
		Num_real_specular_faces_to_render++;
}

#if _DEBUG
bool Fog_disabled = 0;
#else
#define Fog_disabled 0
#endif

// Adds a specular face to draw after the mine has been drawn
void UpdateFogFace(room* rp, face* fp, bool retained, ubyte clip_codes)
{
	if (Fog_disabled || !Detail_settings.Fog_enabled)
		return;
	if (Room_material_fog_active &&
		(fp->portal_num < 0 ||
		 (rp->portals[fp->portal_num].flags & PF_RENDER_FACES)))
	{
		return;
	}
	Fog_faces[Num_fog_faces_to_render] = fp - rp->faces;
	Fog_face_retained[Num_fog_faces_to_render] = retained;
	Fog_face_clip_codes[Num_fog_faces_to_render] = clip_codes;
	Num_fog_faces_to_render++;
}

bool BeginRoomMaterialFog(room* rp, const vector* eye, int viewer_room,
	float intensity)
{
	if (!rp || !eye || !(rp->flags & RF_FOG) || !Detail_settings.Fog_enabled ||
		!UseHardware || !rend_CanUseNewrender() || In_editor_mode ||
		rp->fog_depth <= 0.0f)
	{
		return false;
	}

	const face* mirror_face = nullptr;
	const vector* mirror_point = nullptr;
	if (Render_mirror_for_room)
	{
		if (Mirror_room < 0 || Mirror_room > Highest_room_index ||
			!Rooms[Mirror_room].used || Rooms[Mirror_room].mirror_face < 0 ||
			Rooms[Mirror_room].mirror_face >= Rooms[Mirror_room].num_faces)
		{
			return false;
		}
		mirror_face = &Rooms[Mirror_room].faces[Rooms[Mirror_room].mirror_face];
		if (!mirror_face->face_verts || mirror_face->num_verts < 3)
			return false;
		mirror_point = &Rooms[Mirror_room].verts[mirror_face->face_verts[0]];
	}

	auto fog_world_position = [&](const vector& source)
	{
		if (!mirror_face)
			return source;
		const float distance = vm_DotProduct(&source, &mirror_face->normal) -
			vm_DotProduct(mirror_point, &mirror_face->normal);
		return source - (mirror_face->normal * (distance * 2.0f));
	};

	thread_local std::vector<renderer_room_fog_triangle> portal_triangles;
	portal_triangles.clear();
	for (int portal_index = 0; portal_index < rp->num_portals; portal_index++)
	{
		// Use the same material/door visibility decision as room traversal: a
		// transparent rendered portal remains an opening, while an opaque one is
		// a terminating material surface rather than a fog boundary.
		if (!RenderPastPortal(rp, &rp->portals[portal_index]))
			continue;
		const int facenum = rp->portals[portal_index].portal_face;
		if (facenum < 0 || facenum >= rp->num_faces)
			continue;
		const face* fp = &rp->faces[facenum];
		if (!fp->face_verts || fp->num_verts < 3)
			continue;
		const vector a = fog_world_position(rp->verts[fp->face_verts[0]]);
		for (int triangle = 0; triangle < fp->num_verts - 2; triangle++)
		{
			const vector b = fog_world_position(rp->verts[fp->face_verts[triangle + 1]]);
			const vector c = fog_world_position(rp->verts[fp->face_verts[triangle + 2]]);
			renderer_room_fog_triangle item = {};
			item.a[0] = a.x; item.a[1] = a.y; item.a[2] = a.z;
			item.b[0] = b.x; item.b[1] = b.y; item.b[2] = b.z;
			item.c[0] = c.x; item.c[1] = c.y; item.c[2] = c.z;
			portal_triangles.push_back(item);
		}
	}

	renderer_room_fog_state state = {};
	state.enabled = true;
	state.viewer_inside = !Render_mirror_for_room && viewer_room == (int)(rp - Rooms);
	state.viewer_position[0] = eye->x;
	state.viewer_position[1] = eye->y;
	state.viewer_position[2] = eye->z;
	state.viewer_forward[0] = Viewer_orient.fvec.x;
	state.viewer_forward[1] = Viewer_orient.fvec.y;
	state.viewer_forward[2] = Viewer_orient.fvec.z;
	state.color[0] = rp->fog_r;
	state.color[1] = rp->fog_g;
	state.color[2] = rp->fog_b;
	state.depth = rp->fog_depth;
	state.intensity = intensity;
	state.triangles = portal_triangles.empty() ? nullptr : portal_triangles.data();
	state.triangle_count = (int)portal_triangles.size();
	Room_material_fog_active = rend_SetRoomFogState(&state);
	return Room_material_fog_active;
}

bool BeginCurrentViewRoomMaterialFog(room* rp, float intensity)
{
	return BeginRoomMaterialFog(rp, &Viewer_eye, Viewer_roomnum, intensity);
}

bool BeginRoomnumMaterialFog(int roomnum, float intensity)
{
	if (ROOMNUM_OUTSIDE(roomnum) || roomnum < 0 || roomnum > Highest_room_index ||
		!Rooms[roomnum].used)
	{
		return false;
	}

	return BeginCurrentViewRoomMaterialFog(&Rooms[roomnum], intensity);
}

void EndRoomMaterialFog()
{
	if (!Room_material_fog_active)
		return;
	renderer_room_fog_state state = {};
	rend_SetRoomFogState(&state);
	Room_material_fog_active = false;
}

bool RoomMaterialFogActive()
{
	return Room_material_fog_active;
}

RoomMaterialFogScope::RoomMaterialFogScope(int roomnum, float intensity)
{
	if (!RoomMaterialFogActive())
		owns_state = BeginRoomnumMaterialFog(roomnum, intensity);
}

RoomMaterialFogScope::~RoomMaterialFogScope()
{
	if (owns_state)
		EndRoomMaterialFog();
}

static void QueueDeferredFogAOFaces(room* rp)
{
	if (!Render_preferred_state.ao_enabled)
		return;

	int roomnum = rp - Rooms;
	for (int i = 0; i < Num_fog_faces_to_render; i++)
		Deferred_fog_ao_faces.push_back({ roomnum, Fog_faces[i], Fog_face_retained[i],
			Fog_face_clip_codes[i] });
}

struct RoomFogBatchedFace
{
	int nv;
	g3Point point_storage[MAX_VERTS_PER_FACE];
	g3Point* pointlist[MAX_VERTS_PER_FACE];

	void RefreshPointList()
	{
		for (int i = 0; i < nv; i++)
			pointlist[i] = &point_storage[i];
	}
};

static void FlushRoomFogFaceBatch(room* rp, std::vector<RoomFogBatchedFace>& faces,
	std::vector<int>& retained_faces, ubyte& retained_clip_codes)
{
	if (faces.empty() && retained_faces.empty())
		return;

	if (!retained_faces.empty())
	{
		// These faces use the identical retained mesh in both passes. A units-only
		// bias avoids slope discontinuities along their shared edges.
		rend_SetCoplanarPolygonOffset(0.0001f);
		RetainedRoomDrawFogFaces(rp, retained_faces.data(), (int)retained_faces.size(),
			&Room_fog_plane, Room_fog_distance, Room_fog_eye_distance, rp->fog_depth,
			Room_fog_plane_check != 1, &Viewer_eye, &Viewer_orient,
			retained_clip_codes);
		retained_faces.clear();
		retained_clip_codes = 0;
	}

	if (!faces.empty())
	{
		// These faces use the same projected legacy stream in both passes. Keep
		// the bias units-only here as well so adjacent slopes cannot form seams.
		rend_SetCoplanarPolygonOffset(0.0001f);
		std::vector<renderer_poly_batch_item> items(faces.size());
		for (size_t face_index = 0; face_index < faces.size(); face_index++)
		{
			faces[face_index].RefreshPointList();
			items[face_index].pointlist = faces[face_index].pointlist;
			items[face_index].nv = faces[face_index].nv;
		}

		rend_DrawPolygon3DBatch(0, items.data(), (int)items.size(), MAP_TYPE_BITMAP);
		faces.clear();
	}
}

// Render a fog layer on top of a face
void RenderFogFaces(room* rp, bool suppress_ao)
{
	g3Point* pointlist[MAX_VERTS_PER_FACE];
	g3Point  pointbuffer[MAX_VERTS_PER_FACE];
	std::vector<RoomFogBatchedFace> batched_faces;
	std::vector<int> retained_faces;
	ubyte retained_clip_codes = 0;
	const bool batch_fog = UseHardware && rend_CanUseNewrender();
	if (Room_material_fog_active)
		rend_SetRoomFogOverlay(1);
	rend_SetOverlayType(OT_NONE);
	rend_SetTextureType(TT_FLAT);
	rend_SetLighting(LS_NONE);
	rend_SetColorModel(CM_MONO);
	rend_SetAlphaType(AT_VERTEX);
	rend_SetAlphaValue(255);
	rend_SetZBufferWriteMask(0);

	rend_SetFlatColor(GR_RGB((int)(rp->fog_r * 255.0), (int)(rp->fog_g * 255.0), (int)(rp->fog_b * 255.0)));
	if (suppress_ao)
		rend_SetAOSuppression(1.0f);
	for (int i = 0; i < Num_fog_faces_to_render; i++)
	{
		face* fp = &rp->faces[Fog_faces[i]];
		for (int vn = 0; vn < fp->num_verts; vn++)
		{
			pointbuffer[vn] = World_point_buffer[rp->wpb_index + fp->face_verts[vn]];
			g3Point* p = &pointbuffer[vn];
			pointlist[vn] = p;

			float mag = 1.f; //[ISB] Initialize this so we don't get junk data, but I can't tell if we're supposed to get here with Room_fog_plane_check==-1

			if (Room_fog_plane_check == 0)
			{
				// Outside of the room
				vector* vec = &rp->verts[fp->face_verts[vn]];
				// Now we must generate the split point. This is simply
				// an equation in the form Origin + t*Direction

				float dist = (*vec * Room_fog_plane) + Room_fog_distance;

				vector subvec = *vec - Viewer_eye;
				float t = Room_fog_eye_distance / (Room_fog_eye_distance - dist);
				vector portal_point = Viewer_eye + (t * subvec);

				float eye_distance = -(vm_DotProduct(&Viewer_orient.fvec, &portal_point));
				mag = vm_DotProduct(&Viewer_orient.fvec, vec) + eye_distance;
			}
			else if (Room_fog_plane_check == 1)
			{
				// In the room, distance from 
				vector* vec = &rp->verts[fp->face_verts[vn]];
				mag = vm_DotProduct(&Room_fog_plane, vec) + Room_fog_distance;
			}

			float scalar = mag / rp->fog_depth;
			if (scalar > 1)
				scalar = 1;
			if (scalar < 0)
				scalar = 0;
			p->p3_a = scalar * Room_light_val;

			p->p3_flags |= PF_RGBA;
		}
		if (batch_fog && Fog_face_retained[i] &&
			RetainedRoomCanDrawBaseFace(rp, Fog_faces[i]))
		{
			const ubyte face_clip_codes = Fog_face_clip_codes[i];
			if (!retained_faces.empty() && retained_clip_codes != face_clip_codes)
				FlushRoomFogFaceBatch(rp, batched_faces, retained_faces,
					retained_clip_codes);
			retained_clip_codes = face_clip_codes;
			retained_faces.push_back(Fog_faces[i]);
			continue;
		}
		if (batch_fog && !(fp->flags & FF_TRIANGULATED))
		{
			RoomFogBatchedFace batched_face = {};
			batched_face.nv = fp->num_verts;
			for (int vn = 0; vn < fp->num_verts; vn++)
				batched_face.point_storage[vn] = pointbuffer[vn];
			batched_faces.push_back(batched_face);
			continue;
		}

		FlushRoomFogFaceBatch(rp, batched_faces, retained_faces, retained_clip_codes);
		rend_SetCoplanarPolygonOffset(0.0001f);
		if (fp->flags & FF_TRIANGULATED)
			g3_SetTriangulationTest(1);
		g3_DrawPoly(fp->num_verts, pointlist, 0);
		if (fp->flags & FF_TRIANGULATED)
			g3_SetTriangulationTest(0);
	}

	FlushRoomFogFaceBatch(rp, batched_faces, retained_faces, retained_clip_codes);
	if (suppress_ao)
		rend_SetAOSuppression(0.0f);
	rend_SetCoplanarPolygonOffset(0);
	rend_SetZBufferWriteMask(1);
	if (Room_material_fog_active)
		rend_SetRoomFogOverlay(0);
}

static void RenderDeferredFogAOSuppression()
{
	if (Deferred_fog_ao_faces.empty())
		return;

	PERF_MARKER_SCOPE("RenderMine.DeferredFogAO");

	int old_num_fog_faces = Num_fog_faces_to_render;
	short old_fog_faces[MAX_FACES_PER_ROOM];
	bool old_fog_face_retained[MAX_FACES_PER_ROOM];
	ubyte old_fog_face_clip_codes[MAX_FACES_PER_ROOM];
	for (int i = 0; i < old_num_fog_faces && i < MAX_FACES_PER_ROOM; i++)
	{
		old_fog_faces[i] = Fog_faces[i];
		old_fog_face_retained[i] = Fog_face_retained[i];
		old_fog_face_clip_codes[i] = Fog_face_clip_codes[i];
	}

	int current_roomnum = -1;
	Num_fog_faces_to_render = 0;

	auto flush_room = [&]()
	{
		if (current_roomnum < 0 || Num_fog_faces_to_render <= 0)
			return;

		room* rp = &Rooms[current_roomnum];
		int old_wpb_index = rp->wpb_index;
		rp->wpb_index = 0;
		RotateRoomPoints(rp, rp->verts);
		SetupRoomFog(rp, &Viewer_eye, &Viewer_orient, Viewer_roomnum);

		rend_SetPostMaskOnly(1);
		RenderFogFaces(rp, true);
		rend_SetPostMaskOnly(0);

		rp->wpb_index = old_wpb_index;
		Num_fog_faces_to_render = 0;
	};

	for (size_t i = 0; i < Deferred_fog_ao_faces.size(); i++)
	{
		const deferred_fog_face& item = Deferred_fog_ao_faces[i];
		if (item.roomnum != current_roomnum || Num_fog_faces_to_render >= MAX_FACES_PER_ROOM)
		{
			flush_room();
			current_roomnum = item.roomnum;
		}
		Fog_faces[Num_fog_faces_to_render] = item.facenum;
		Fog_face_retained[Num_fog_faces_to_render] = item.retained;
		Fog_face_clip_codes[Num_fog_faces_to_render] = item.clip_codes;
		Num_fog_faces_to_render++;
	}

	flush_room();
	Deferred_fog_ao_faces.clear();

	Num_fog_faces_to_render = old_num_fog_faces;
	for (int i = 0; i < old_num_fog_faces && i < MAX_FACES_PER_ROOM; i++)
	{
		Fog_faces[i] = old_fog_faces[i];
		Fog_face_retained[i] = old_fog_face_retained[i];
		Fog_face_clip_codes[i] = old_fog_face_clip_codes[i];
	}
}

// MATT!  Change this function to sort by state once you change the scorch system!
void RenderScorchesForRoom(room* rp)
{
	if (!Detail_settings.Scorches_enabled)
		return;
	//Set alpha, transparency, & lighting for this face
	rend_SetAlphaType(AT_LIGHTMAP_BLEND);
	rend_SetAlphaValue(255);
	rend_SetLighting(LS_NONE);
	rend_SetColorModel(CM_MONO);
	rend_SetOverlayType(OT_NONE);
	rend_SetZBias(-.5);
	rend_SetZBufferWriteMask(0);

	//Select texture type
	rend_SetTextureType(TT_LINEAR);

	for (int i = 0; i < Num_scorches_to_render; i++)
		DrawScorches(ROOMNUM(rp), Scorches_to_render[i]);
	
	//Reset rendering states
	rend_SetZBias(0);
	rend_SetZBufferWriteMask(1);
}

struct RoomBatchedFace
{
	int nv;
	g3Point point_storage[MAX_VERTS_PER_FACE];
	g3Point* pointlist[MAX_VERTS_PER_FACE];

	void RefreshPointList()
	{
		for (int i = 0; i < nv; i++)
			pointlist[i] = &point_storage[i];
	}
};

struct RoomBaseFaceBatchKey
{
	int bitmap_handle;
	int overlay_map;
	ubyte overlay_type;
	sbyte alpha_type;
	ubyte alpha_value;
	color_model color_model_value;
	int ao_class;
	int dynamic_lightmap_lmi;
	ubyte clip_codes;
	float u_offset;
	float v_offset;
	float light_scalar;

	bool Equals(const RoomBaseFaceBatchKey& other) const
	{
		return bitmap_handle == other.bitmap_handle &&
			overlay_map == other.overlay_map &&
			overlay_type == other.overlay_type &&
			alpha_type == other.alpha_type &&
			alpha_value == other.alpha_value &&
			color_model_value == other.color_model_value &&
			ao_class == other.ao_class &&
			dynamic_lightmap_lmi == other.dynamic_lightmap_lmi &&
			clip_codes == other.clip_codes &&
			u_offset == other.u_offset &&
			v_offset == other.v_offset &&
			light_scalar == other.light_scalar;
	}
};

static constexpr ubyte RETAINED_ROOM_CLIP_CODES =
	CC_BEHIND | CC_OFF_FAR | CC_OFF_CUSTOM;

struct RoomBaseFaceBatch
{
	RoomBaseFaceBatchKey key;
	std::vector<RoomBatchedFace> faces;
	room* retained_room = nullptr;
	std::vector<int> retained_faces;
};

class RoomBaseFaceBatcher
{
public:
	void Add(const RoomBaseFaceBatchKey& key, const RoomBatchedFace& face)
	{
		for (size_t i = 0; i < m_batches.size(); i++)
		{
			if (m_batches[i].key.Equals(key))
			{
				m_batches[i].faces.push_back(face);
				return;
			}
		}

		RoomBaseFaceBatch batch;
		batch.key = key;
		batch.faces.push_back(face);
		m_batches.push_back(batch);
	}

	void AddRetained(const RoomBaseFaceBatchKey& key, room* rp, int facenum)
	{
		for (size_t i = 0; i < m_batches.size(); i++)
		{
			if (m_batches[i].key.Equals(key) &&
				(!m_batches[i].retained_room || m_batches[i].retained_room == rp))
			{
				m_batches[i].retained_room = rp;
				m_batches[i].retained_faces.push_back(facenum);
				return;
			}
		}

		RoomBaseFaceBatch batch;
		batch.key = key;
		batch.retained_room = rp;
		batch.retained_faces.push_back(facenum);
		m_batches.push_back(batch);
	}

	void Flush()
	{
		if (m_batches.empty())
			return;

		for (size_t i = 0; i < m_batches.size(); i++)
		{
			RoomBaseFaceBatch& batch = m_batches[i];
			if (batch.faces.empty() && batch.retained_faces.empty())
				continue;

			rend_SetAlphaType(batch.key.alpha_type);
			rend_SetAlphaValue(batch.key.alpha_value);
			rend_SetLighting(LS_GOURAUD);
			rend_SetColorModel(batch.key.color_model_value);
			rend_SetOverlayType(batch.key.overlay_type);
			if (batch.key.overlay_type != OT_NONE)
				rend_SetOverlayMap(batch.key.overlay_map);
			rend_SetTextureType(TT_PERSPECTIVE);
			rend_SetAOClass(batch.key.ao_class);

			if (!batch.retained_faces.empty())
			{
				rend_BindBitmap(batch.key.bitmap_handle);
				if (batch.key.overlay_type != OT_NONE)
					rend_BindLightmap(batch.key.overlay_map);
				renderer_per_pixel_light per_pixel_lights[RENDERER_MAX_PER_PIXEL_DYNAMIC_LIGHTS];
				int per_pixel_light_count = 0;
				if (batch.key.dynamic_lightmap_lmi >= 0)
				{
					per_pixel_light_count = GetPerPixelLightmapLights(
						(ushort)batch.key.dynamic_lightmap_lmi, per_pixel_lights,
						RENDERER_MAX_PER_PIXEL_DYNAMIC_LIGHTS);
					if (per_pixel_light_count > 0)
						rend_SetPerPixelDynamicLighting(
							&LightmapInfo[batch.key.dynamic_lightmap_lmi].normal,
							per_pixel_light_count, per_pixel_lights);
				}
				RetainedRoomDrawFaces(batch.retained_room, batch.retained_faces.data(),
					(int)batch.retained_faces.size(), batch.key.bitmap_handle,
					batch.key.u_offset,
					batch.key.v_offset, batch.key.light_scalar,
					batch.key.overlay_type != OT_NONE ? batch.key.overlay_map : -1,
					batch.key.clip_codes);
				if (per_pixel_light_count > 0)
					rend_SetPerPixelDynamicLighting(nullptr, 0, nullptr);
			}

			if (!batch.faces.empty())
			{
				std::vector<renderer_poly_batch_item> items(batch.faces.size());
				for (size_t face_index = 0; face_index < batch.faces.size(); face_index++)
				{
					batch.faces[face_index].RefreshPointList();
					items[face_index].pointlist = batch.faces[face_index].pointlist;
					items[face_index].nv = batch.faces[face_index].nv;
				}

				rend_DrawPolygon3DBatch(batch.key.bitmap_handle, items.data(),
					(int)items.size(), MAP_TYPE_BITMAP);
			}
		}

		rend_SetAOClass(RENDERER_AO_CLASS_DEFAULT);
		m_batches.clear();
	}

private:
	std::vector<RoomBaseFaceBatch> m_batches;
};

static void CopyRoomBatchPoints(RoomBatchedFace& batched_face, g3Point** pointlist, int nv)
{
	batched_face.nv = nv;
	for (int i = 0; i < nv; i++)
	{
		batched_face.point_storage[i] = *pointlist[i];
		batched_face.point_storage[i].p3_flags &= ~PF_TEMP_POINT;
		if (!(batched_face.point_storage[i].p3_flags & PF_PROJECTED))
			g3_ProjectPoint(&batched_face.point_storage[i]);
	}
}

static void AddBatchedRoomFaceSideEffects(room* rp, face* fp, int facenum,
	bool spec_face, bool retained, ubyte clip_codes)
{
	if (!Render_mirror_for_room && Rendering_main_view && fp->portal_num == -1 &&
		((fp->flags & FF_CORONA) || FastCoronas) && (fp->flags & FF_LIGHTMAP) && UseHardware &&
		(GameTextures[fp->tmap].flags & TF_LIGHT))
	{
		if (Num_glows_this_frame < MAX_LIGHT_GLOWS && Detail_settings.Coronas_enabled)
		{
			LightGlowsThisFrame[Num_glows_this_frame].roomnum = rp - Rooms;
			LightGlowsThisFrame[Num_glows_this_frame].facenum = facenum;
			Num_glows_this_frame++;
		}
	}

	if (!Render_mirror_for_room && spec_face)
		UpdateSpecularFace(rp, fp);

	if ((fp->flags & FF_SCORCHED) && !Render_mirror_for_room &&
		Num_scorches_to_render < MAX_FACES_PER_ROOM)
	{
		Scorches_to_render[Num_scorches_to_render++] = facenum;
	}

	if (!Render_mirror_for_room && !In_editor_mode && (rp->flags & RF_FOG))
		UpdateFogFace(rp, fp, retained, clip_codes);

	fp->renderframe = FrameCount % 256;
}

static bool TryBatchRoomBaseFace(room* rp, int facenum, RoomBaseFaceBatcher& batcher)
{
	if (In_editor_mode)
		return false;

	face* fp = &rp->faces[facenum];
	if (fp->num_verts < 3 || fp->num_verts > MAX_VERTS_PER_FACE)
		return false;

	if (No_render_windows_hack == 1 && fp->portal_num != -1)
		return false;

	const bool spec_face = SpecularShouldQueueFace(rp, fp);

	float uchange = 0;
	float vchange = 0;
	if (GameTextures[fp->tmap].slide_u != 0)
	{
		int int_time = Gametime / GameTextures[fp->tmap].slide_u;
		float norm_time = Gametime - (int_time * GameTextures[fp->tmap].slide_u);
		norm_time /= GameTextures[fp->tmap].slide_u;
		uchange = norm_time;
	}

	if (GameTextures[fp->tmap].slide_v != 0)
	{
		int int_time = Gametime / GameTextures[fp->tmap].slide_v;
		float norm_time = Gametime - (int_time * GameTextures[fp->tmap].slide_v);
		norm_time /= GameTextures[fp->tmap].slide_v;
		vchange = norm_time;
	}

	g3Codes face_cc;
	face_cc.cc_and = 0xff;
	face_cc.cc_or = 0;
	g3Point pointbuffer[MAX_VERTS_PER_FACE];
	g3Point* pointlist[MAX_VERTS_PER_FACE];

	fp->flags &= ~FF_TRIANGULATED;

	for (int vn = 0; vn < fp->num_verts; vn++)
	{
		pointbuffer[vn] = World_point_buffer[rp->wpb_index + fp->face_verts[vn]];
		g3Point* p = &pointbuffer[vn];
		pointlist[vn] = p;
		p->p3_uvl.u = fp->face_uvls[vn].u;
		p->p3_uvl.v = fp->face_uvls[vn].v;
		p->p3_uvl.u2 = fp->face_uvls[vn].u2;
		p->p3_uvl.v2 = fp->face_uvls[vn].v2;
		p->p3_flags |= PF_UV + PF_L + PF_UV2;
#ifndef RELEASE
		if (fp->flags & FF_LIGHTMAP)
			p->p3_uvl.l = Room_light_val;
		else
			p->p3_uvl.l = 1.0;
#else
		p->p3_uvl.l = Room_light_val;
#endif
		p->p3_uvl.u += uchange;
		p->p3_uvl.v += vchange;
		face_cc.cc_and &= p->p3_codes;
		face_cc.cc_or |= p->p3_codes;
	}

	if (face_cc.cc_and)
	{
		if (spec_face && UseSmoothSpecularForFace(fp))
		{
			fp->flags |= FF_SPEC_INVISIBLE;
			UpdateSpecularFace(rp, fp);
		}
		return true;
	}
	if (rp->flags & RF_FOG && fp->portal_num != -1 && !(rp->portals[fp->portal_num].flags & PF_RENDER_FACES))
		return false;

	if (NoLightmaps)
	{
		static bool initialized = false;
		static float lm_red[32], lm_green[32], lm_blue[32];
		if (!initialized)
		{
			initialized = true;
			for (int i = 0; i < 32; i++)
			{
				lm_red[i] = (float)i / 31.0;
				lm_green[i] = (float)i / 31.0;
				lm_blue[i] = (float)i / 31.0;
			}
		}

		if (fp->flags & FF_LIGHTMAP)
		{
			int lm_handle = LightmapInfo[fp->lmi_handle].lm_handle;
			ushort* data = (ushort*)lm_data(lm_handle);
			int w = lm_w(lm_handle);
			int h = lm_h(lm_handle);

			for (int i = 0; i < fp->num_verts; i++)
			{
				float u = fp->face_uvls[i].u2 * (w - 1);
				float v = fp->face_uvls[i].v2 * (h - 1);
				g3Point* p = &pointbuffer[i];
				int int_u = u;
				int int_v = v;
				ushort texel = data[int_v * w + int_u];
				int r = (texel >> 10) & 0x1f;
				int g = (texel >> 5) & 0x1f;
				int b = (texel) & 0x1f;
				p->p3_r = p->p3_l * lm_red[r];
				p->p3_g = p->p3_l * lm_green[g];
				p->p3_b = p->p3_l * lm_blue[b];
				p->p3_flags |= PF_RGBA;
			}
		}
		else
		{
			for (int i = 0; i < fp->num_verts; i++)
			{
				g3Point* p = &pointbuffer[i];
				p->p3_r = p->p3_l;
				p->p3_g = p->p3_l;
				p->p3_b = p->p3_l;
				p->p3_flags |= PF_RGBA;
			}
		}
	}

	int bm_handle;
	bm_handle = BaseBitmapHandleForFace(fp);
	ASSERT(bm_handle != -1);

	sbyte alpha_type = (sbyte)GetFaceAlpha(fp, bm_handle);
	if (alpha_type != AT_ALWAYS)
		return false;

	int dynamic_lightmap_lmi = -1;
	if (!NoLightmaps && (fp->flags & FF_LIGHTMAP) && Render_preferred_state.per_pixel_lighting &&
		UseHardware && rend_CanUseNewrender())
	{
		renderer_per_pixel_light per_pixel_lights[RENDERER_MAX_PER_PIXEL_DYNAMIC_LIGHTS];
		int per_pixel_light_count = GetPerPixelLightmapLights(fp->lmi_handle, per_pixel_lights,
			RENDERER_MAX_PER_PIXEL_DYNAMIC_LIGHTS);
		if (per_pixel_light_count > 0)
			dynamic_lightmap_lmi = fp->lmi_handle;
	}

	RoomBaseFaceBatchKey key = {};
	key.bitmap_handle = bm_handle;
	key.alpha_type = alpha_type;
	key.alpha_value = 255;
	key.color_model_value = NoLightmaps ? CM_RGB : CM_MONO;
	key.overlay_type = OT_NONE;
	key.overlay_map = 0;
	key.ao_class = RoomFaceAOClass(rp, fp);
	key.dynamic_lightmap_lmi = dynamic_lightmap_lmi;
	key.clip_codes = face_cc.cc_or & RETAINED_ROOM_CLIP_CODES;
	key.u_offset = uchange;
	key.v_offset = vchange;
	key.light_scalar = Room_light_val;

	if (fp->flags & FF_LIGHTMAP)
	{
		if (!(GameTextures[BaseTextureTmapForFace(fp)].flags & TF_SATURATE))
		{
			key.overlay_type = OT_BLEND;
			key.overlay_map = LightmapInfo[fp->lmi_handle].lm_handle;
		}
	}

	if (RetainedRoomCanDrawBaseFace(rp, facenum))
	{
		batcher.AddRetained(key, rp, facenum);
		AddBatchedRoomFaceSideEffects(rp, fp, facenum, spec_face, true,
			face_cc.cc_or & RETAINED_ROOM_CLIP_CODES);
		return true;
	}

	RoomBatchedFace batched_face;
	CopyRoomBatchPoints(batched_face, pointlist, fp->num_verts);
	batcher.Add(key, batched_face);
	AddBatchedRoomFaceSideEffects(rp, fp, facenum, spec_face, false,
		face_cc.cc_or & RETAINED_ROOM_CLIP_CODES);
	return true;
}

//Draw the specified face
//Parameters:	rp - pointer to the room the face is un
//				facenum - which face in the specified room
void RenderFace(room* rp, int facenum)
{
	int drawn = 0;
	face* fp = &rp->faces[facenum];
	g3Point* pointlist[MAX_VERTS_PER_FACE];
	g3Point  pointbuffer[MAX_VERTS_PER_FACE];
	renderer_per_pixel_light per_pixel_lights[RENDERER_MAX_PER_PIXEL_DYNAMIC_LIGHTS];
	float	uchange = 0, vchange = 0;
	ubyte	do_triangle_test = 0;
	int per_pixel_light_count = 0;
	g3Codes face_cc;
	static int first = 1;
	static float lm_red[32], lm_green[32], lm_blue[32];
	bool spec_face = 0;
	bool retained_base_face = false;
	bool retained_base_face_drawn = false;
	bool retained_geometry_eligible = false;
	face_cc.cc_and = 0xff;
	face_cc.cc_or = 0;
#ifdef EDITOR
	if (fp->flags & FF_FLOATING_TRIG)
	{
		RenderFloatingTrig(rp, fp);
		return;
	}
#endif
	// Clear triangulation flag
	fp->flags &= ~FF_TRIANGULATED;
	// check for render windows hack
	if (No_render_windows_hack == 1)
	{
		if (fp->portal_num != -1)
			return;
	}

	if (rp->flags & RF_TRIANGULATE)
		do_triangle_test = 1;

	if (!Render_mirror_for_room && SpecularShouldQueueFace(rp, fp))
		spec_face = 1;

	// Figure out if there is any texture sliding
	if (GameTextures[fp->tmap].slide_u != 0)
	{
		int int_time = Gametime / GameTextures[fp->tmap].slide_u;
		float norm_time = Gametime - (int_time * GameTextures[fp->tmap].slide_u);
		norm_time /= GameTextures[fp->tmap].slide_u;
		uchange = norm_time;
	}

	if (GameTextures[fp->tmap].slide_v != 0)
	{
		int int_time = Gametime / GameTextures[fp->tmap].slide_v;
		float norm_time = Gametime - (int_time * GameTextures[fp->tmap].slide_v);
		norm_time /= GameTextures[fp->tmap].slide_v;
		vchange = norm_time;
	}

	//Build list of points and UVLs for this face
	if (Render_mirror_for_room)	// If mirror room, order the vertices counter clockwise
	{
		for (int vn = 0; vn < fp->num_verts; vn++)
		{
			pointbuffer[vn] = World_point_buffer[rp->wpb_index + fp->face_verts[vn]];
			g3Point* p = &pointbuffer[vn];
			pointlist[(fp->num_verts - 1) - vn] = p;
			p->p3_uvl.u = fp->face_uvls[vn].u;
			p->p3_uvl.v = fp->face_uvls[vn].v;
			p->p3_uvl.u2 = fp->face_uvls[vn].u2;
			p->p3_uvl.v2 = fp->face_uvls[vn].v2;

			p->p3_flags |= PF_UV + PF_L + PF_UV2;	//has uv and l set
#ifndef RELEASE
			if (fp->flags & FF_LIGHTMAP)
				p->p3_uvl.l = Room_light_val;
			else
				p->p3_uvl.l = 1.0;
#else
			p->p3_uvl.l = Room_light_val;
#endif

			// do texture sliding
			p->p3_uvl.u += uchange;
			p->p3_uvl.v += vchange;
			face_cc.cc_and &= p->p3_codes;
			face_cc.cc_or |= p->p3_codes;
		}
	}
	else
	{
		for (int vn = 0; vn < fp->num_verts; vn++)
		{
			pointbuffer[vn] = World_point_buffer[rp->wpb_index + fp->face_verts[vn]];
			g3Point* p = &pointbuffer[vn];
			pointlist[vn] = p;
			p->p3_uvl.u = fp->face_uvls[vn].u;
			p->p3_uvl.v = fp->face_uvls[vn].v;
			p->p3_uvl.u2 = fp->face_uvls[vn].u2;
			p->p3_uvl.v2 = fp->face_uvls[vn].v2;

			p->p3_flags |= PF_UV + PF_L + PF_UV2;	//has uv and l set
#ifndef RELEASE
			if (fp->flags & FF_LIGHTMAP)
				p->p3_uvl.l = Room_light_val;
			else
				p->p3_uvl.l = 1.0;
#else
			p->p3_uvl.l = Room_light_val;
#endif

			// do texture sliding
			p->p3_uvl.u += uchange;
			p->p3_uvl.v += vchange;
			face_cc.cc_and &= p->p3_codes;
			face_cc.cc_or |= p->p3_codes;

		}
	}
	if (face_cc.cc_and)	// This entire face is off the screen
	{
		if (spec_face && UseSmoothSpecularForFace(fp))
		{
			fp->flags |= FF_SPEC_INVISIBLE;
			UpdateSpecularFace(rp, fp);
		}
		return;
	}

	// Do stupid gouraud shading for lightmap
	if (NoLightmaps)
	{
		if (first)
		{
			first = 0;
			for (int i = 0; i < 32; i++)
			{
				lm_red[i] = (float)i / 31.0;
				lm_green[i] = (float)i / 31.0;
				lm_blue[i] = (float)i / 31.0;
			}
		}

		if (fp->flags & FF_LIGHTMAP)
		{
			int lm_handle = LightmapInfo[fp->lmi_handle].lm_handle;
			ushort* data = (ushort*)lm_data(lm_handle);
			int w = lm_w(lm_handle);
			int h = lm_h(lm_handle);

			for (int i = 0; i < fp->num_verts; i++)
			{
				float u = fp->face_uvls[i].u2 * (w - 1);
				float v = fp->face_uvls[i].v2 * (h - 1);
				g3Point* p = &pointbuffer[i];
				int int_u = u;
				int int_v = v;
				ushort texel = data[int_v * w + int_u];
				int r = (texel >> 10) & 0x1f;
				int g = (texel >> 5) & 0x1f;
				int b = (texel) & 0x1f;
				p->p3_r = p->p3_l * lm_red[r];
				p->p3_g = p->p3_l * lm_green[g];
				p->p3_b = p->p3_l * lm_blue[b];
				p->p3_flags |= PF_RGBA;
			}
		}
		else
		{
			for (int i = 0; i < fp->num_verts; i++)
			{
				g3Point* p = &pointbuffer[i];
				p->p3_r = p->p3_l;
				p->p3_g = p->p3_l;
				p->p3_b = p->p3_l;
				p->p3_flags |= PF_RGBA;
			}
		}
	}
	//Get bitmap handle
	int bm_handle;
	bm_handle = BaseBitmapHandleForFace(fp);
	ASSERT(bm_handle != -1);

	//Set alpha, transparency, & lighting for this face
	rend_SetAlphaType(GetFaceAlpha(fp, bm_handle));

	// If this is a mirror face, and mirrors are turned off, then just make opaque
	if (!Detail_settings.Mirrored_surfaces && rp->mirror_face != -1 && rp->faces[rp->mirror_face].tmap == fp->tmap)
		rend_SetAlphaValue(255);
	else if (Render_mirror_for_room && rp->mirror_face != -1 && fp->tmap == rp->faces[rp->mirror_face].tmap && rp != &Rooms[Mirror_room])
		rend_SetAlphaValue(255);	// This prevents mirrors from rendering each other
	else
		rend_SetAlphaValue(GameTextures[BaseTextureTmapForFace(fp)].alpha * 255);

	rend_SetLighting(LS_GOURAUD);

	if (!NoLightmaps)
		rend_SetColorModel(CM_MONO);
	else
		rend_SetColorModel(CM_RGB);

	// Set lighting map
	if (fp->flags & FF_LIGHTMAP)
	{
		if (GameTextures[BaseTextureTmapForFace(fp)].flags & TF_SATURATE)
			rend_SetOverlayType(OT_NONE);
		else
			rend_SetOverlayType(OT_BLEND);
		rend_SetOverlayMap(LightmapInfo[fp->lmi_handle].lm_handle);
	}
	else
		rend_SetOverlayType(OT_NONE);

	//Select texture type
	rend_SetTextureType(TT_PERSPECTIVE);

	if (face_cc.cc_or) // Clip/project partly offscreen faces as triangles to avoid edge-of-screen UV warping.
		do_triangle_test = 1;

	if (do_triangle_test)
	{
		fp->flags |= FF_TRIANGULATED;
		g3_SetTriangulationTest(1);
	}
	retained_geometry_eligible = !In_editor_mode &&
		RetainedRoomCanDrawBaseFace(rp, facenum);

	// Do special fog stuff for portal faces
	if (rp->flags & RF_FOG && !In_editor_mode)
	{
		if (fp->portal_num != -1 && !(rp->portals[fp->portal_num].flags & PF_RENDER_FACES))
		{
			drawn = 1;
			retained_base_face_drawn = retained_geometry_eligible;
			goto draw_fog;
		}
	}

	//Draw the damn thing
	if (!NoLightmaps && (fp->flags & FF_LIGHTMAP) && Render_preferred_state.per_pixel_lighting && UseHardware && rend_CanUseNewrender())
	{
		per_pixel_light_count = GetPerPixelLightmapLights(fp->lmi_handle, per_pixel_lights,
			RENDERER_MAX_PER_PIXEL_DYNAMIC_LIGHTS);
		if (per_pixel_light_count > 0)
			rend_SetPerPixelDynamicLighting(&fp->normal, per_pixel_light_count, per_pixel_lights);
	}

	rend_SetAOClass(RoomFaceAOClass(rp, fp));
	retained_base_face = retained_geometry_eligible;
	if (retained_base_face)
	{
		rend_BindBitmap(bm_handle);
		if ((fp->flags & FF_LIGHTMAP) &&
			!(GameTextures[BaseTextureTmapForFace(fp)].flags & TF_SATURATE))
		{
			rend_BindLightmap(LightmapInfo[fp->lmi_handle].lm_handle);
		}
		const int retained_lightmap = (fp->flags & FF_LIGHTMAP) &&
			!(GameTextures[BaseTextureTmapForFace(fp)].flags & TF_SATURATE) ?
			LightmapInfo[fp->lmi_handle].lm_handle : -1;
		if (RetainedRoomDrawFaces(rp, &facenum, 1, bm_handle, uchange, vchange,
			Room_light_val, retained_lightmap,
			face_cc.cc_or & RETAINED_ROOM_CLIP_CODES,
			false))
		{
			drawn = 1;
			retained_base_face_drawn = true;
		}
		else
			drawn = g3_DrawPoly(fp->num_verts, pointlist, bm_handle, MAP_TYPE_BITMAP, &face_cc);
	}
	else
	{
		drawn = g3_DrawPoly(fp->num_verts, pointlist, bm_handle, MAP_TYPE_BITMAP, &face_cc);
	}
	rend_SetAOClass(RENDERER_AO_CLASS_DEFAULT);
	if (per_pixel_light_count > 0)
		rend_SetPerPixelDynamicLighting(nullptr, 0, nullptr);

	// Do light saturation
	if (!Render_mirror_for_room && Rendering_main_view && drawn && fp->portal_num == -1 && ((fp->flags & FF_CORONA) || FastCoronas) && (fp->flags & FF_LIGHTMAP) && UseHardware && (GameTextures[fp->tmap].flags & TF_LIGHT))
	{
		if (Num_glows_this_frame < MAX_LIGHT_GLOWS && Detail_settings.Coronas_enabled)
		{
			LightGlowsThisFrame[Num_glows_this_frame].roomnum = rp - Rooms;
			LightGlowsThisFrame[Num_glows_this_frame].facenum = facenum;
			Num_glows_this_frame++;
		}
	}
	// Draw a specular face
	if (!Render_mirror_for_room && spec_face)
	{
		if (drawn)
			UpdateSpecularFace(rp, fp);
		else
		{
			if (UseSmoothSpecularForFace(fp))
			{
				fp->flags |= FF_SPEC_INVISIBLE;
				UpdateSpecularFace(rp, fp);
			}
		}
	}

	//Draw scorches, if any
	if (drawn && fp->flags & FF_SCORCHED && !Render_mirror_for_room)
	{
		DrawScorches(ROOMNUM(rp), facenum);
	}

draw_fog:
	if (!Render_mirror_for_room && !In_editor_mode && drawn && (rp->flags & RF_FOG))
	{
		UpdateFogFace(rp, fp, retained_base_face_drawn,
			face_cc.cc_or & RETAINED_ROOM_CLIP_CODES);
	}

	if (do_triangle_test)
		g3_SetTriangulationTest(0);

	// Mark it as rendered
	if (drawn)
		fp->renderframe = FrameCount % 256;

	//if (Render_mirror_for_room && (GameTextures[fp->tmap].flags & TF_ALPHA))
		//rend_SetZBufferWriteMask (1);
#ifdef EDITOR
	if (OUTLINE_ON(OM_MINE))		//Outline the face
	{
		rend_SetTextureType(TT_FLAT);
		rend_SetAlphaType(AT_ALWAYS);
		rend_SetFlatColor(GR_RGB(255, 255, 255));

		if (UseHardware)
		{
			rend_SetZBias(-.1f);
			for (int i = 0; i < fp->num_verts; i++)
			{
				g3_DrawSpecialLine(pointlist[i], pointlist[(i + 1) % fp->num_verts]);
			}
			rend_SetZBias(0);
		}
		else
		{
			for (int i = 0; i < fp->num_verts; i++)
			{
				g3_DrawLine(GR_RGB(255, 255, 255), pointlist[i], pointlist[(i + 1) % fp->num_verts]);
			}
		}
		if ((fp->flags & FF_HAS_TRIGGER) && (fp->num_verts > 3)) {
			g3_DrawLine(CUREDGE_COLOR, pointlist[0], pointlist[2]);
			g3_DrawLine(CUREDGE_COLOR, pointlist[1], pointlist[3]);
		}
		if (fp->special_handle != BAD_SPECIAL_FACE_INDEX)
		{
			g3Point p1, p2;
			vector verts[MAX_VERTS_PER_FACE];
			vector center, end;
			for (int t = 0; t < fp->num_verts; t++)
				verts[t] = rp->verts[fp->face_verts[t]];
			vm_GetCentroid(&center, verts, fp->num_verts);
			vector subvec = SpecialFaces[fp->special_handle].spec_instance[0].bright_center - center;
			vm_NormalizeVectorFast(&subvec);
			end = center + subvec;
			g3_RotatePoint(&p1, &center);
			g3_RotatePoint(&p2, &end);
			g3_DrawLine(GR_RGB(255, 255, 255), &p1, &p2);
			/*for (t=0;t<fp->num_verts;t++)
			{
				end=rp->verts[fp->face_verts[t]]+SpecialFaces[fp->special_handle].vertnorms[t];
				g3_RotatePoint (&p1,&rp->verts[fp->face_verts[t]]);
				g3_RotatePoint (&p2,&end);
				g3_DrawLine(GR_RGB(255,255,255),&p1,&p2);
			}*/
		}
	}

	if (Outline_lightmaps)
	{
		rend_SetTextureType(TT_FLAT);
		rend_SetAlphaType(AT_ALWAYS);
		if (fp == &Curroomp->faces[Curface] && (fp->flags & FF_LIGHTMAP))
		{
			ASSERT(fp->lmi_handle != BAD_LMI_INDEX);

			lightmap_info* lmi = &LightmapInfo[fp->lmi_handle];
			ushort* src_data = (ushort*)lm_data(lmi->lm_handle);
			matrix facematrix;
			vector fvec = -lmi->normal;
			vm_VectorToMatrix(&facematrix, &fvec, NULL, NULL);
			vector rvec = facematrix.rvec * lmi->xspacing;
			vector uvec = facematrix.uvec * lmi->yspacing;
			vm_TransposeMatrix(&facematrix);
			int w = lm_w(lmi->lm_handle);
			int h = lm_h(lmi->lm_handle);
			for (int i = 0; i < w * h; i++)
			{
				int t;
				g3Point epoints[20];
				vector evec[20];
				int y = i / w;
				int x = i % w;
				evec[0] = lmi->upper_left - (y * uvec) + (x * rvec);
				g3_RotatePoint(&epoints[0], &evec[0]);
				pointlist[0] = &epoints[0];
				evec[1] = lmi->upper_left - (y * uvec) + ((x + 1) * rvec);
				g3_RotatePoint(&epoints[1], &evec[1]);
				pointlist[1] = &epoints[1];
				evec[2] = lmi->upper_left - ((y + 1) * uvec) + ((x + 1) * rvec);
				g3_RotatePoint(&epoints[2], &evec[2]);
				pointlist[2] = &epoints[2];
				evec[3] = lmi->upper_left - ((y + 1) * uvec) + (x * rvec);
				g3_RotatePoint(&epoints[3], &evec[3]);
				pointlist[3] = &epoints[3];

				if (!(src_data[y * w + x] & OPAQUE_FLAG))
				{
					for (t = 0; t < 4; t++)
						g3_DrawLine(GR_RGB(255, 0, 255), pointlist[t], pointlist[(t + 1) % 4]);
				}
				else
				{
					for (t = 0; t < 4; t++)
						g3_DrawLine(GR_RGB(255, 255, 255), pointlist[t], pointlist[(t + 1) % 4]);
				}

				if (Search_lightmaps)
				{
					for (t = 0; t < 4; t++)
						g3_ProjectPoint(&epoints[t]);

					if (point_in_poly(4, epoints, TSearch_x, TSearch_y))
					{
						found_lightmap = i;
						TSearch_found_type = TSEARCH_FOUND_MINE;
						TSearch_seg = ROOMNUM(rp);
						TSearch_face = facenum;
					}

				}
			}
		}
	}
#endif
}

static float face_depth[MAX_FACES_PER_ROOM];
//compare function for room face sort
static int room_face_sort_func(const short* a, const short* b)
{
	float az = face_depth[*a];
	float bz = face_depth[*b];
	if (az < bz)
		return -1;
	else if (az > bz)
		return 1;
	else
		return 0;
}

// Sets up fog if this room is fogged
void SetupRoomFog(room* rp, vector* eye, matrix* orient, int viewer_room)
{
	if ((rp->flags & RF_FOG) == 0)
		return;

	if (!Detail_settings.Fog_enabled)
	{
		// fog is disabled
		Room_fog_plane_check = -1;
		return;
	}

	if (viewer_room == (rp - Rooms))
	{
		// viewer is in the room
		vector* vec = eye;
		Room_fog_plane_check = 1;
		Room_fog_distance = -vm_DotProduct(&orient->fvec, vec);
		Room_fog_plane = orient->fvec;
		return;
	}

	// find the 'fogroom' number (we should have put it in here if we will render the room)
	int found_room = -1;
	for (int i = 0; i < Num_fogged_rooms_this_frame && found_room == -1; i++)
	{
		if (Fog_portal_data[i].roomnum == rp - Rooms)
		{
			found_room = i;
			break;
		}
	}

	if (found_room == -1 || Fog_portal_data[found_room].close_face == NULL)
	{
		// we won't be rendering this room
		Room_fog_plane_check = -1;
		return;
	}

	// Use the closest face
	face* close_face = Fog_portal_data[found_room].close_face;
	Room_fog_plane_check = 0;
	Room_fog_plane = close_face->normal;
	Room_fog_portal_vert = rp->verts[close_face->face_verts[0]];
	Room_fog_distance = -vm_DotProduct(&Room_fog_plane, &Room_fog_portal_vert);
	Room_fog_eye_distance = (*eye * Room_fog_plane) + Room_fog_distance;
}

//Renders the faces in a room without worrying about sorting.  Used in the game when Z-buffering is active
void RenderRoomUnsorted(room* rp)
{
	int rcount = 0;
	RoomBaseFaceBatcher base_face_batcher;
	const bool batch_room_faces = UseHardware && rend_CanUseNewrender();
	ASSERT(rp->num_faces <= MAX_FACES_PER_ROOM);

	// Rotate points in this room if need be
	if (rp->wpb_index == -1)
	{
		rp->wpb_index = Global_buffer_index;
		RotateRoomPoints(rp, rp->verts);

		Global_buffer_index += rp->num_verts;
	}

	// Sort portal faces if this is a fogged room
	if (rp->flags & RF_FOG)
		SetupRoomFog(rp, &Viewer_eye, &Viewer_orient, Viewer_roomnum);

	//Check for visible (non-backfacing) faces, & render
	for (int fn = 0; fn < rp->num_faces; fn++)
	{
		face* fp = &rp->faces[fn];
		int fogged_portal = 0;

		if (!(fp->flags & FF_VISIBLE) || (fp->flags & FF_NOT_FACING))
		{
			if (UseSmoothSpecularForFace(fp))
			{
				if (!Render_mirror_for_room && SpecularShouldQueueFace(rp, fp))
				{
					fp->flags |= FF_SPEC_INVISIBLE;
					UpdateSpecularFace(rp, fp);
				}

			}
			fp->flags &= ~(FF_NOT_FACING | FF_VISIBLE);
			continue;		// this guy shouldn't be rendered
		}

		// Clear visibility flags
		if (Render_mirror_for_room == false)
		{
			fp->flags &= ~(FF_VISIBLE | FF_NOT_FACING);
		}
		else
		{
			if (rp == &Rooms[Mirror_room])
			{
				if (rp->faces[fn].tmap == rp->faces[rp->mirror_face].tmap)
					continue;		// Don't render the mirror face if rendering the mirror
			}
		}

		if (!FaceIsRenderable(rp, fp))
			continue;	//skip this face

#ifdef EDITOR
		if (In_editor_mode)
		{
			if ((Shell_render_flag & SRF_NO_NON_SHELL) && (fp->flags & FF_NOT_SHELL))
				continue;
			if ((Shell_render_flag & SRF_NO_SHELL) && !(fp->flags & FF_NOT_SHELL))
				continue;
		}
#endif

		if (fp->portal_num != -1 && !(rp->portals[fp->portal_num].flags & PF_RENDER_FACES) && (rp->flags & RF_FOG))
		{
			fogged_portal = 1;
		}

		if (Render_mirror_for_room == false && (fogged_portal || (GetFaceAlpha(fp, -1) & (ATF_CONSTANT + ATF_VERTEX))))
		{
			// Place alpha faces into our postrender list
			if (Num_postrenders < MAX_POSTRENDERS)
			{
				face_depth[fn] = 0.0f;
				for (int vn = 0; vn < fp->num_verts; vn++)
					face_depth[fn] += World_point_buffer[rp->wpb_index + fp->face_verts[vn]].p3_z;
				
				Postrender_list[Num_postrenders].type = PRT_WALL;
				Postrender_list[Num_postrenders].roomnum = rp - Rooms;
				Postrender_list[Num_postrenders].facenum = fn;
				Postrender_list[Num_postrenders++].z = face_depth[fn] /= fp->num_verts;;
			}
		}
		else
		{
			if (batch_room_faces && TryBatchRoomBaseFace(rp, fn, base_face_batcher))
				continue;

			base_face_batcher.Flush();
			RenderFace(rp, fn);
		}
	}

	base_face_batcher.Flush();
}

// Figures out a scalar value to apply to all vertices in the room
void ComputeRoomPulseLight(room* rp)
{
	if (rp->pulse_time == 0 || In_editor_mode)
		Room_light_val = 1.0;
	else
	{
		float ptime = rp->pulse_time * WALL_PULSE_INCREMENT;
		float add_time = rp->pulse_offset * WALL_PULSE_INCREMENT;

		int int_time = (int)((Gametime + add_time) / (ptime * 2));
		float left_time = (Gametime + add_time) - (int_time * ptime * 2);
		float norm_time = left_time / (ptime);
		if (norm_time > 1)
			Room_light_val = 1 - (norm_time - 1.0);
		else
			Room_light_val = norm_time;
	}
	if (!In_editor_mode)
	{
		if (rp->flags & RF_STROBE)
		{
			int val = (Gametime * 10) + (rp - Rooms);
			if (val % 2)
				Room_light_val = 0;
		}
		if (rp->flags & RF_FLICKER)
		{
			if (Get60HzVisualNoise((uint32_t)(rp - Rooms), 3) & 1u)
				Room_light_val = 0;
		}
	}
}

#define CORONA_DIST_CUTOFF	5.0f
//Draws a glow around a light
void RenderSingleLightGlow(int index)
{
	room* rp = &Rooms[LightGlows[index].roomnum];
	face* fp = &rp->faces[LightGlows[index].facenum];
	texture* texp = &GameTextures[fp->tmap];
	int bm_handle = Fireballs[DEFAULT_CORONA_INDEX + texp->corona_type].bm_handle;

	// Get size of light	
	float size = LightGlows[index].size;
	vector center = LightGlows[index].center;

	// Get alpha of light
	vector tvec = Viewer_eye - rp->verts[fp->face_verts[0]];
	vm_NormalizeVectorFast(&tvec);
	float facing_scalar = (tvec * fp->normal) * 2;
	if (facing_scalar < 0)
		return;
	tvec = center - Viewer_eye;

	float dist = vm_GetMagnitudeFast(&tvec);
	if (dist < (size * CORONA_DIST_CUTOFF))
		return;
	if (dist < (size * (CORONA_DIST_CUTOFF + 15)))
	{
		float dist_scalar = ((dist - (size * CORONA_DIST_CUTOFF)) / (size * 15));
		facing_scalar *= dist_scalar;
	}

	facing_scalar *= LightGlows[index].scalar;
	facing_scalar = std::min(facing_scalar, 1.0f);
	// Take into effect pulsing
	ComputeRoomPulseLight(rp);
	facing_scalar *= Room_light_val;
	rend_SetAlphaValue(facing_scalar * .4 * 255);

	float maxc = std::max(texp->r, texp->g);
	maxc = std::max(texp->b, maxc);
	float r, g, b;
	if (maxc > 1.0)
	{
		r = texp->r / maxc;
		g = texp->g / maxc;
		b = texp->b / maxc;
	}
	else
	{
		r = texp->r;
		g = texp->g;
		b = texp->b;
	}

	if (LightGlows[index].flags & LGF_FAST)
	{
		rend_SetZBufferWriteMask(0);
		rend_SetZBias((-size / 2));
	}
	else
	{
		rend_SetZBufferState(0);
	}

	ddgr_color color = GR_RGB(r * 255, g * 255, b * 255);
	g3_DrawBitmap(&center, size, (size * bm_h(bm_handle, 0)) / bm_w(bm_handle, 0), bm_handle, color);
	if (LightGlows[index].flags & LGF_FAST)
	{
		rend_SetZBufferWriteMask(1);
		rend_SetZBias(0);
	}
	else
	{
		rend_SetZBufferState(1);
	}
}

// Figures out if we can see the center of a light face and adds it to our globa list
void CheckLightGlowsForRoom(room* rp)
{
	for (int i = 0; i < Num_glows_this_frame; i++)
	{
		// For each light, see if we can cast a vector to it
		face* fp = &rp->faces[LightGlowsThisFrame[i].facenum];
		vector verts[MAX_VERTS_PER_FACE];
		vector center;
		for (int t = 0; t < fp->num_verts; t++)
			verts[t] = rp->verts[fp->face_verts[t]];
		float size = sqrt(vm_GetCentroid(&center, verts, fp->num_verts));
		size *= 2;
		center += (fp->normal / 4);
		if (vm_VectorDistanceQuick(&center, &Viewer_eye) < (size * CORONA_DIST_CUTOFF))
			continue;

		// Check if we can see this light
		fvi_info hit_info;
		fvi_query fq;

		// shoot a ray from the light position to the current vertex
		if (FastCoronas)
		{
			if (rp->flags & RF_EXTERNAL)
			{
				SetGlowStatus(rp - Rooms, LightGlowsThisFrame[i].facenum, &center, size, FastCoronas);
				continue;
			}
			vector subvec = Viewer_eye - center;
			vm_NormalizeVectorFast(&subvec);
			subvec *= size;
			subvec += center;
			fq.p0 = &center;
			fq.p1 = &subvec;
			fq.startroom = rp - Rooms;
		}
		else
		{
			fq.p0 = &Viewer_eye;
			fq.p1 = &center;
			fq.startroom = Viewer_object->roomnum;
		}

		fq.rad = 0.0f;
		fq.flags = FQ_CHECK_OBJS | FQ_NO_RELINK | FQ_IGNORE_WEAPONS | FQ_ROBOTS_AS_SPHERE | FQ_PLAYERS_AS_SPHERE;
		fq.thisobjnum = -1;
		fq.ignore_obj_list = NULL;
		int fate = fvi_FindIntersection(&fq, &hit_info);
		if (fate != HIT_NONE)
			continue;
		SetGlowStatus(rp - Rooms, LightGlowsThisFrame[i].facenum, &center, size, FastCoronas);
	}
}

// Called before a frame starts to render - sets all of our light glows to decreasing
void PreUpdateAllLightGlows()
{
	int i, count;
	for (i = 0, count = 0; i < MAX_LIGHT_GLOWS && count < Num_glows; i++)
	{
		if (LightGlows[i].flags & LGF_USED)
		{
			count++;
			LightGlows[i].flags &= ~LGF_INCREASING;
		}
	}
}
// Called after a frame has been rendered - slowly morphs our light glows into nothing
void PostUpdateAllLightGlows()
{
	int i, count;
	for (i = 0, count = 0; i < MAX_LIGHT_GLOWS && count < Num_glows; i++)
	{
		if (LightGlows[i].flags & LGF_USED)
		{
			count++;
			if (LightGlows[i].flags & LGF_INCREASING)
			{
				LightGlows[i].scalar += (Frametime * 4);
				if (LightGlows[i].scalar > 1)
					LightGlows[i].scalar = 1;
			}
			else
			{
				LightGlows[i].scalar -= (Frametime * 4);
				if (LightGlows[i].scalar < 0)
				{
					LightGlows[i].scalar = 0;
					LightGlows[i].flags &= ~LGF_USED;
					Num_glows--;
					count--;
				}
			}
		}
	}

	ASSERT(Num_glows >= 0);
}

// Recursive function that mirrored rooms use
void BuildMirroredRoomListSub(int start_room_num, clip_wnd* wnd)
{
	room* rp = &Rooms[start_room_num];
	g3Point portal_points[MAX_VERTS_PER_FACE];
	if (!Mirrored_room_checked[start_room_num])
	{
		Mirrored_room_list[Num_mirrored_rooms++] = start_room_num;
	}
	Mirrored_room_checked[start_room_num] = 1;

	//If this room is a closed (non-seethrough) door, don't check any of its portals, 
	//...UNLESS this is the first room we're looking at (meaning the viewer is in this room)
	if ((rp->flags & RF_DOOR) && (DoorwayGetPosition(rp) == 0.0) && !(Doors[rp->doorway_data->doornum].flags & DF_SEETHROUGH))
		return;

	room* mirror_rp = &Rooms[Mirror_room];
	vector* mirror_vec = &mirror_rp->verts[mirror_rp->faces[mirror_rp->mirror_face].face_verts[0]];
	face* mirror_fp = &mirror_rp->faces[mirror_rp->mirror_face];
	vector* mirror_norm = &mirror_fp->normal;

	// This is how far the mirror face is from the normalized plane
	float mirror_dist = -(mirror_vec->x * mirror_norm->x + mirror_vec->y * mirror_norm->y + mirror_vec->z * mirror_norm->z);

	//Check all the portals for this room
	for (int t = 0; t < rp->num_portals; t++)
	{
		portal* pp = &rp->portals[t];
		int croom = pp->croom;
		ASSERT(croom >= 0);

		// If we are an external room portalizing into another external room, then skip!
		if ((rp->flags & RF_EXTERNAL) && (Rooms[croom].flags & RF_EXTERNAL))
			continue;

		//Check if we can see through this portal, and if not, skip it
		if (!RenderPastPortal(rp, pp))
			continue;

		// If this portal has been visited, skip it
		if (Mirrored_room_checked[croom])
			continue;

		//Deal with external portals differently
		int external_door_hack = 0;
		if (rp->flags & RF_EXTERNAL && Rooms[croom].flags & RF_DOOR)
			external_door_hack = 1;

		//Get pointer to this portal's face
		face* fp = &rp->faces[pp->portal_face];

		//See if portal is facing toward us
		if (!external_door_hack)
		{
			vector temp_vec;
			vector* vec = &rp->verts[fp->face_verts[0]];
			float dist_from_mirror = vec->x * mirror_norm->x + vec->y * mirror_norm->y + vec->z * mirror_norm->z + mirror_dist;

			// dest_vecs contains the point on the other side of the mirror (ie the reflected point)
			temp_vec = *vec - (*mirror_norm * (dist_from_mirror * 2));
			vector incident_norm;
			ReflectRay(&incident_norm, &fp->normal, &mirror_fp->normal);

			vector tvec = Viewer_eye - temp_vec;
			if ((tvec * incident_norm) <= 0)
				continue;	// not facing
		}

		g3Codes cc;
		cc.cc_or = 0; cc.cc_and = 0xff;
		int nv = fp->num_verts;

		//Code the face points
		for (int i = 0; i < nv; i++)
		{
			vector temp_vec;
			vector* vec = &rp->verts[fp->face_verts[i]];
			float dist_from_mirror = vec->x * mirror_norm->x + vec->y * mirror_norm->y + vec->z * mirror_norm->z + mirror_dist;

			// dest_vecs contains the point on the other side of the mirror (ie the reflected point)
			temp_vec = *vec - (*mirror_norm * (dist_from_mirror * 2));
			g3_RotatePoint(&portal_points[i], &temp_vec);

			ubyte c = portal_points[i].p3_codes;
			cc.cc_and &= c;
			cc.cc_or |= c;
		}

		//If points are on screen, see if they're in the clip window
		if (cc.cc_and == 0 || external_door_hack)
		{
			bool clipped = 0;
			g3Point* pointlist[MAX_VERTS_PER_FACE], ** pl = pointlist;
			for (int i = 0; i < nv; i++)
				pointlist[i] = &portal_points[i];

			//If portal not all on screen, must clip it
			if (cc.cc_or)
			{
				pl = g3_ClipPolygon(pl, &nv, &cc);
				clipped = 1;
			}
			cc.cc_and = 0xff;
			for (int i = 0; i < nv; i++)
			{
				g3_ProjectPoint(pl[i]);
				cc.cc_and &= clip2d(pl[i], wnd);
			}

			// Make sure it didn't get clipped away
			if (cc.cc_and == 0 || external_door_hack)
			{
				clip_wnd new_wnd;
				new_wnd.right = new_wnd.bot = 0.0;
				new_wnd.left = Render_width;
				new_wnd.top = Render_height;

				//make new clip window
				for (int i = 0; i < nv; i++)
				{
					float x = pl[i]->p3_sx, y = pl[i]->p3_sy;
					if (x < new_wnd.left)
						new_wnd.left = x;
					if (x > new_wnd.right)
						new_wnd.right = x;
					if (y < new_wnd.top)
						new_wnd.top = y;
					if (y > new_wnd.bot)
						new_wnd.bot = y;
				}

				//Combine the two windows
				new_wnd.left = __max(wnd->left, new_wnd.left);
				new_wnd.right = __min(wnd->right, new_wnd.right);
				new_wnd.top = __max(wnd->top, new_wnd.top);
				new_wnd.bot = __min(wnd->bot, new_wnd.bot);
				if (clipped)
				{		//Free up temp points
					g3_FreeTempPoints(pl, nv);
					clipped = 0;
				}
				BuildMirroredRoomListSub(croom, &new_wnd);
			}
			if (clipped)		//Free up temp points
				g3_FreeTempPoints(pl, nv);
		}
	}
}
// Goes through and builds a list of rooms that can be seen from a mirror
void BuildMirroredRoomList()
{
	room* rp = &Rooms[Mirror_room];
	clip_wnd wnd;
	Num_mirrored_rooms = 0;
	memset(Mirrored_room_checked, 0, MAX_ROOMS);
	Mirrored_room_checked[Mirror_room] = 1;
	Mirrored_room_list[Num_mirrored_rooms++] = Mirror_room;

	//Initial clip window is whole screen
	wnd.left = wnd.top = 0.0;
	wnd.right = Render_width;
	wnd.bot = Render_height;
	g3Point portal_points[MAX_VERTS_PER_FACE], temp_points[MAX_VERTS_PER_FACE * 2];
	g3Point* pointlist[MAX_VERTS_PER_FACE * 2], ** pl = pointlist;
	room* mirror_rp = &Rooms[Mirror_room];
	vector* mirror_vec = &mirror_rp->verts[mirror_rp->faces[mirror_rp->mirror_face].face_verts[0]];
	face* mirror_fp = &mirror_rp->faces[mirror_rp->mirror_face];
	vector* mirror_norm = &mirror_fp->normal;

	// This is how far the mirror face is from the normalized plane
	float mirror_dist = -(mirror_vec->x * mirror_norm->x + mirror_vec->y * mirror_norm->y + mirror_vec->z * mirror_norm->z);
	int total_points = 0;
	for (int t = 0; t < rp->num_mirror_faces; t++)
	{
		face* fp = &rp->faces[rp->mirror_faces_list[t]];
		int i;
		ASSERT(total_points + fp->num_verts <= MAX_VERTS_PER_FACE * 2);
		g3Codes cc;
		cc.cc_and = 0xff;
		cc.cc_or = 0;
		int nv = fp->num_verts;
		int clipped = 0;

		for (i = 0; i < fp->num_verts; i++)
		{
			vector temp_vec;
			vector* vec = &rp->verts[fp->face_verts[i]];
			float dist_from_mirror = vec->x * mirror_norm->x + vec->y * mirror_norm->y + vec->z * mirror_norm->z + mirror_dist;

			// dest_vecs contains the point on the other side of the mirror (ie the reflected point)
			temp_vec = *vec - (*mirror_norm * (dist_from_mirror * 2));
			g3_RotatePoint(&portal_points[i], &temp_vec);
			cc.cc_and &= portal_points[i].p3_codes;
			cc.cc_or |= portal_points[i].p3_codes;
			pointlist[i] = &portal_points[i];
		}

		// Clipped away	
		if (cc.cc_and)
			continue;

		if (cc.cc_or)
		{
			// Must clip
			pl = g3_ClipPolygon(pl, &nv, &cc);

			if (cc.cc_and)
			{
				g3_FreeTempPoints(pl, nv);
				continue;
			}
			else
			{
				for (i = 0; i < nv; i++)
				{
					temp_points[i + total_points] = *pl[i];
					temp_points[i + total_points].p3_flags &= ~PF_TEMP_POINT;
				}
				g3_FreeTempPoints(pl, nv);
			}
		}
		else
		{
			for (i = 0; i < nv; i++)
			{
				temp_points[i + total_points] = *pl[i];
			}
		}

		for (i = 0; i < nv; i++)
			g3_ProjectPoint(&temp_points[total_points + i]);

		total_points += nv;
	}

	//make new clip window
	clip_wnd new_wnd;
	new_wnd.right = new_wnd.bot = 0.0;
	new_wnd.left = Render_width;
	new_wnd.top = Render_height;

	for (int i = 0; i < total_points; i++)
	{
		float x = temp_points[i].p3_sx, y = temp_points[i].p3_sy;

		if (x < new_wnd.left)
			new_wnd.left = x;
		if (x > new_wnd.right)
			new_wnd.right = x;
		if (y < new_wnd.top)
			new_wnd.top = y;
		if (y > new_wnd.bot)
			new_wnd.bot = y;
	}

	new_wnd.left = std::max(wnd.left, new_wnd.left);
	new_wnd.right = std::min(wnd.right, new_wnd.right);
	new_wnd.top = std::max(wnd.top, new_wnd.top);
	new_wnd.bot = std::min(wnd.bot, new_wnd.bot);

	BuildMirroredRoomListSub(Mirror_room, &new_wnd);
}

vector mirror_dest_vecs[MAX_VERTS_PER_ROOM];
g3Point mirror_save_points[MAX_VERTS_PER_ROOM];
// Renders a mirror flipped about the mirrored plane
void RenderMirroredRoom(room* rp)
{
	if (Render_use_newrender)
		return; //Need new mirroring pipeline

	ushort save_flags[MAX_FACES_PER_ROOM];
	bool restore_index = true;
	int save_index = Global_buffer_index;

	// Save old rotated points
	if (rp->wpb_index == -1)
	{
		restore_index = false;
		rp->wpb_index = Global_buffer_index;
	}
	else
	{
		for (int i = 0; i < rp->num_verts; i++)
			mirror_save_points[i] = World_point_buffer[rp->wpb_index + i];
	}

	// Find facing faces for this mirror
	face* fp = &rp->faces[0];
	for (int i = 0; i < rp->num_faces; i++, fp++)
	{
		save_flags[i] = fp->flags;
		fp->flags &= ~FF_NOT_FACING;
		fp->flags |= FF_VISIBLE;
	}

	room* mirror_rp = &Rooms[Mirror_room];
	vector* mirror_vec = &mirror_rp->verts[mirror_rp->faces[mirror_rp->mirror_face].face_verts[0]];
	face* mirror_fp = &mirror_rp->faces[mirror_rp->mirror_face];
	vector* norm = &mirror_fp->normal;

	// This is how far the mirror face is from the normalized plane
	float mirror_dist = -(mirror_vec->x * norm->x + mirror_vec->y * norm->y + mirror_vec->z * norm->z);
	g3Plane mirror_plane(*norm, *mirror_vec);
	float retained_reflection[16];
	g3_GenerateReflect(mirror_plane, retained_reflection);
	RetainedRoomSetTransform(rp, retained_reflection);

	for (int i = 0; i < rp->num_verts; i++)
	{
		vector* vec = &rp->verts[i];
		float dist_from_mirror = vec->x * norm->x + vec->y * norm->y + vec->z * norm->z + mirror_dist;
		// dest_vecs contains the point on the other side of the mirror (ie the reflected point)
		mirror_dest_vecs[i] = *vec - (*norm * (dist_from_mirror * 2));
	}

	// Rotate our mirror points
	vector revnorm = -*norm;
	g3_SetCustomClipPlane(1, mirror_vec, &revnorm);

	RotateRoomPoints(rp, mirror_dest_vecs);

	// Mark facing faces
	int save_frame = Facing_visited[rp - Rooms];
	Facing_visited[rp - Rooms] = 0;

	MarkFacingFaces(rp - Rooms, mirror_dest_vecs);
	Facing_visited[rp - Rooms] = save_frame;
	// Render the mirror room
	rend_SetColorModel(CM_MONO);
	rend_SetLighting(LS_GOURAUD);
	rend_SetWrapType(WT_WRAP);
	const bool material_fog = BeginRoomMaterialFog(rp, &Viewer_eye,
		Viewer_roomnum, Room_light_val);
	RenderRoomUnsorted(rp);
	if (material_fog)
		EndRoomMaterialFog();

	if (restore_index == false)
	{
		rp->wpb_index = -1;
	}
	else
	{
		for (int i = 0; i < rp->num_verts; i++)
			World_point_buffer[rp->wpb_index + i] = mirror_save_points[i];
	}

	fp = &rp->faces[0];
	for (int i = 0; i < rp->num_faces; i++, fp++)
		fp->flags = save_flags[i];

	RenderRoomObjects(rp);
	rp->last_render_time = Gametime;
	g3_SetCustomClipPlane(0, NULL, NULL);
	RetainedRoomClearTransform(rp);
}

// Renders a specific room.  If pos_offset is not NULL, adds that offset to each of the
// rooms vertices
void RenderRoom(room* rp)
{
	Num_glows_this_frame = 0;

	//Set up rendering states
	rend_SetColorModel(CM_MONO);
	rend_SetLighting(LS_GOURAUD);
	rend_SetWrapType(WT_WRAP);
	if (rp->used == 0)
	{
		Int3(); // Trying to draw a room that isn't in use!
		return;
	}

	// Figure out pulse lighting for room
	ComputeRoomPulseLight(rp);
	const bool material_fog = BeginRoomMaterialFog(rp, &Viewer_eye,
		Viewer_roomnum, Room_light_val);

	// Mark it visible for automap
	AutomapVisMap[rp - Rooms] = 1;

	RenderRoomUnsorted(rp);

	rp->last_render_time = Gametime;
	rp->flags &= ~RF_MIRROR_VISIBLE;

	CheckLightGlowsForRoom(rp);

	if (Num_scorches_to_render > 0)
	{
		RenderScorchesForRoom(rp);
		Num_scorches_to_render = 0;
	}

	if (Num_specular_faces_to_render > 0)
	{
		RenderSpecularFacesFlat(rp);
		Num_specular_faces_to_render = 0;
		Num_real_specular_faces_to_render = 0;
	}

	if (Num_fog_faces_to_render > 0)
	{
		QueueDeferredFogAOFaces(rp);
		RenderFogFaces(rp, false);
		Num_fog_faces_to_render = 0;
	}
	if (material_fog)
		EndRoomMaterialFog();
}

#define MAX_OBJECTS_PER_ROOM 2000
struct obj_sort_item
{
	int	vis_effect;
	int	objnum;
	float	dist;
};
obj_sort_item obj_sort_list[MAX_OBJECTS_PER_ROOM];

//Compare function for room face sort
static int obj_sort_func(const obj_sort_item* a, const obj_sort_item* b)
{
	if (a->dist < b->dist)
		return -1;
	else if (a->dist > b->dist)
		return 1;
	else
		return 0;
}

inline void IsRoomDynamicValid(room* rp, int x, int y, int z, float* r, float* g, float* b)
{
	int w = rp->volume_width;
	int h = rp->volume_height;
	int d = rp->volume_depth;

	ubyte color = rp->volume_lights[(z * w * h) + (y * w) + x];

	*r = (float)((color >> 5) / 7.0);
	*g = (float)((color >> 2) & 0x07) / 7.0;
	*b = (float)((float)(color & 0x03) / 3.0);
}

// Gets the dynamic light value for this position
void GetRoomDynamicScalar(vector* pos, room* rp, float* r, float* g, float* b)
{
	float front_values_r[10];
	float back_values_r[10];
	float front_values_g[10];
	float back_values_g[10];
	float front_values_b[10];
	float back_values_b[10];
	if (!rp->volume_lights)
	{
		*r = 1;
		*g = 1;
		*b = 1;
		return;
	}
	float fl_x = (pos->x - rp->min_xyz.x) / VOLUME_SPACING;
	float fl_y = (pos->y - rp->min_xyz.y) / VOLUME_SPACING;
	float fl_z = (pos->z - rp->min_xyz.z) / VOLUME_SPACING;
	fl_x = std::max(fl_x, 0.f);
	fl_y = std::max(fl_y, 0.f);
	fl_z = std::max(fl_z, 0.f);
	fl_x = std::min(fl_x, (float)(rp->volume_width - 1));
	fl_y = std::min(fl_y, (float)(rp->volume_height - 1));
	fl_z = std::min(fl_z, (float)(rp->volume_depth - 1));
	int int_x = fl_x;
	int int_y = fl_y;
	int int_z = fl_z;
	fl_x -= int_x;
	fl_y -= int_y;
	fl_z -= int_z;
	int next_x = int_x + 1;
	int next_y = int_y + 1;
	int next_z = int_z + 1;
	next_x = std::min(rp->volume_width - 1, next_x);
	next_y = std::min(rp->volume_height - 1, next_y);
	next_z = std::min(rp->volume_depth - 1, next_z);

	float left_norm_r, left_norm_g, left_norm_b;
	float right_norm_r, right_norm_g, right_norm_b;
	float front_norm_r, front_norm_g, front_norm_b;
	float back_norm_r, back_norm_g, back_norm_b;

	IsRoomDynamicValid(rp, int_x, int_y, int_z, &front_values_r[0], &front_values_g[0], &front_values_b[0]);
	IsRoomDynamicValid(rp, int_x, next_y, int_z, &front_values_r[1], &front_values_g[1], &front_values_b[1]);
	IsRoomDynamicValid(rp, next_x, next_y, int_z, &front_values_r[2], &front_values_g[2], &front_values_b[2]);
	IsRoomDynamicValid(rp, next_x, int_y, int_z, &front_values_r[3], &front_values_g[3], &front_values_b[3]);
	IsRoomDynamicValid(rp, int_x, int_y, next_z, &back_values_r[0], &back_values_g[0], &back_values_b[0]);
	IsRoomDynamicValid(rp, int_x, next_y, next_z, &back_values_r[1], &back_values_g[1], &back_values_b[1]);
	IsRoomDynamicValid(rp, next_x, next_y, next_z, &back_values_r[2], &back_values_g[2], &back_values_b[2]);
	IsRoomDynamicValid(rp, next_x, int_y, next_z, &back_values_r[3], &back_values_g[3], &back_values_b[3]);

	// Do front edge
	int left_out = 0;
	int right_out = 0;

	// Left edge
	left_norm_r = ((1 - fl_y) * front_values_r[0]) + (fl_y * front_values_r[1]);
	left_norm_g = ((1 - fl_y) * front_values_g[0]) + (fl_y * front_values_g[1]);
	left_norm_b = ((1 - fl_y) * front_values_b[0]) + (fl_y * front_values_b[1]);

	// Right edge
	right_norm_r = ((1 - fl_y) * front_values_r[3]) + (fl_y * front_values_r[2]);
	right_norm_g = ((1 - fl_y) * front_values_g[3]) + (fl_y * front_values_g[2]);
	right_norm_b = ((1 - fl_y) * front_values_b[3]) + (fl_y * front_values_b[2]);

	// Figure out front edge
	front_norm_r = ((1 - fl_x) * left_norm_r) + (fl_x * right_norm_r);
	front_norm_g = ((1 - fl_x) * left_norm_g) + (fl_x * right_norm_g);
	front_norm_b = ((1 - fl_x) * left_norm_b) + (fl_x * right_norm_b);

	// Do back edge
	left_norm_r = ((1 - fl_y) * back_values_r[0]) + (fl_y * back_values_r[1]);
	left_norm_g = ((1 - fl_y) * back_values_g[0]) + (fl_y * back_values_g[1]);
	left_norm_b = ((1 - fl_y) * back_values_b[0]) + (fl_y * back_values_b[1]);

	right_norm_r = ((1 - fl_y) * back_values_r[3]) + (fl_y * back_values_r[2]);
	right_norm_g = ((1 - fl_y) * back_values_g[3]) + (fl_y * back_values_g[2]);
	right_norm_b = ((1 - fl_y) * back_values_b[3]) + (fl_y * back_values_b[2]);
	back_norm_r = ((1 - fl_x) * left_norm_r) + (fl_x * right_norm_r);
	back_norm_g = ((1 - fl_x) * left_norm_g) + (fl_x * right_norm_g);
	back_norm_b = ((1 - fl_x) * left_norm_b) + (fl_x * right_norm_b);
	*r = ((1 - fl_z) * front_norm_r) + (fl_z * back_norm_r);
	*g = ((1 - fl_z) * front_norm_g) + (fl_z * back_norm_g);
	*b = ((1 - fl_z) * front_norm_b) + (fl_z * back_norm_b);

	// Factor in flickering
	ComputeRoomPulseLight(rp);
	(*r) *= Room_light_val;
	(*g) *= Room_light_val;
	(*b) *= Room_light_val;
}

ubyte Trick_type = 0;
//Render the objects and viseffects in a room.  Do a simple sort
void RenderRoomObjects(room* rp)
{
	int n_objs = 0;
	float zdist;
	if (!Render_mirror_for_room)
		return;	// This function only works for mirrors now

	//Add objects to sort list
	int objnum;
	for (objnum = rp->objects; (objnum != -1) && (n_objs < MAX_OBJECTS_PER_ROOM); objnum = Objects[objnum].next)
	{
		ASSERT(objnum != Objects[objnum].next);
		object* obj = &Objects[objnum];

		if (obj->render_type == RT_NONE)
			continue;

		if (obj == Viewer_object && !Render_mirror_for_room)
			continue;

		float size = obj->size;
		// Special case weapons with streamers
		if (obj->type == OBJ_WEAPON && (Weapons[obj->id].flags & WF_STREAMER))
			size = Weapons[obj->id].phys_info.velocity.z;

		// Check if object is trivially rejected
		bool isVisible = IsPointVisible(&obj->pos, size, &zdist) ? true : false;
		if (Render_mirror_for_room || (obj->type == OBJ_WEAPON && Weapons[obj->id].flags & WF_ELECTRICAL) || isVisible)
		{
			obj_sort_list[n_objs].vis_effect = 0;
			obj_sort_list[n_objs].objnum = objnum;
			obj_sort_list[n_objs].dist = zdist;
			n_objs++;
		}
	}

	//Add vis effects to sort list
	int visnum;
	for (visnum = rp->vis_effects; (visnum != -1) && (n_objs < MAX_OBJECTS_PER_ROOM); visnum = VisEffects[visnum].next)
	{
		ASSERT(visnum != VisEffects[visnum].next);
		if (VisEffects[visnum].type == VIS_NONE || VisEffects[visnum].flags & VF_DEAD)
			continue;

		bool pointIsVisible = IsPointVisible(&VisEffects[visnum].pos, VisEffects[visnum].size, &zdist) ? true : false;
		if (Render_mirror_for_room || pointIsVisible)
		{
			obj_sort_list[n_objs].vis_effect = 1;
			obj_sort_list[n_objs].objnum = visnum;
			obj_sort_list[n_objs].dist = zdist;
			n_objs++;
		}
	}

	ASSERT(objnum == -1);	//if not -1, ran out of space in render_order[]
	ASSERT(visnum == -1);	//if not -1, ran out of space in render_order[]

	//Sort the objects
	qsort(obj_sort_list, n_objs, sizeof(*obj_sort_list), (int (*)(const void*, const void*))obj_sort_func);
#ifdef _DEBUG
	bool save_polymodel_outline_mode = Polymodel_outline_mode;
	Polymodel_outline_mode = OUTLINE_ON(OM_OBJECTS);
#endif

	//Render the objects
	if (Render_mirror_for_room)
	{
		// Render the mirror stuff if present
		room* mirror_rp = &Rooms[Mirror_room];
		vector* mirror_vec = &mirror_rp->verts[mirror_rp->faces[mirror_rp->mirror_face].face_verts[0]];
		face* mirror_fp = &mirror_rp->faces[mirror_rp->mirror_face];
		vector* norm = &mirror_fp->normal;
		// This is how far the mirror face is from the normalized plane
		float mirror_dist = -(mirror_vec->x * norm->x + mirror_vec->y * norm->y + mirror_vec->z * norm->z);
		// Setup mirror matrix

		vector negz_vec = { 0,0,-1 };

		matrix mirror_matrix, inv_mirror_matrix, dest_matrix, negz_matrix;
		vm_VectorToMatrix(&mirror_matrix, norm, NULL, NULL);
		inv_mirror_matrix = mirror_matrix;
		vm_TransposeMatrix(&inv_mirror_matrix);
		vm_VectorToMatrix(&negz_matrix, &negz_vec, NULL, NULL);
		negz_matrix.rvec *= -1;

		for (int i = n_objs - 1; i >= 0; i--)
		{
			objnum = obj_sort_list[i].objnum;
			if (obj_sort_list[i].vis_effect)
			{
				FlushWeaponStreamerBatches();
				vis_effect* vis = &VisEffects[objnum];
				vector save_vec = vis->pos;
				vector save_end_vec = vis->end_pos;
				vector* vec = &vis->pos;
				vector* end_vec = &vis->end_pos;
				float dist_from_mirror = vec->x * norm->x + vec->y * norm->y + vec->z * norm->z + mirror_dist;
				// dest_vecs contains the point on the other side of the mirror (ie the reflected point)
				vis->pos = *vec - (*norm * (dist_from_mirror * 2));
				dist_from_mirror = end_vec->x * norm->x + end_vec->y * norm->y + end_vec->z * norm->z + mirror_dist;
				// dest_vecs contains the point on the other side of the mirror (ie the reflected point)
				vis->end_pos = *end_vec - (*norm * (dist_from_mirror * 2));

				DrawVisEffectMaybeBatched(vis);
				vis->pos = save_vec;
				vis->end_pos = save_end_vec;
			}
			else
			{
				FlushVisEffectBatches();
				object* objp = &Objects[objnum];
				if (!RenderObjectHasWeaponStreamer(objp))
					FlushWeaponStreamerBatches();
				if (objp->type == OBJ_POWERUP)
					ForceFlushVisEffectBatches();

				vector save_vec = objp->pos;
				matrix save_orient = objp->orient;
				matrix temp_mat;

				vector* vec = &objp->pos;
				float dist_from_mirror = vec->x * norm->x + vec->y * norm->y + vec->z * norm->z + mirror_dist;
				// dest_vecs contains the point on the other side of the mirror (ie the reflected point)
				objp->pos = *vec - (*norm * (dist_from_mirror * 2));
				// Check for rear view
				if (objp == Viewer_object && Viewer_object == Player_object && (Players[Player_num].flags & PLAYER_FLAGS_REARVIEW))
				{
					objp->orient.fvec = -objp->orient.fvec;
					objp->orient.rvec = -objp->orient.rvec;
				}
				// Get new orientation
				temp_mat = mirror_matrix * negz_matrix * inv_mirror_matrix;
				dest_matrix.rvec = objp->orient.rvec * temp_mat;
				dest_matrix.uvec = objp->orient.uvec * temp_mat;
				dest_matrix.fvec = objp->orient.fvec * temp_mat;
				objp->orient = dest_matrix;
				bool save_render = false;
				if (objp->flags & OF_SAFE_TO_RENDER)
					save_render = true;
				objp->flags |= OF_SAFE_TO_RENDER;


				RenderObject(objp);
				if (save_render)
					objp->flags |= OF_SAFE_TO_RENDER;
				else
					objp->flags &= ~OF_SAFE_TO_RENDER;
				objp->pos = save_vec;
				objp->orient = save_orient;
			}
		}
		ForceFlushVisEffectBatches();
		FlushWeaponStreamerBatches();
		return;
	}

	for (int i = n_objs - 1; i >= 0; i--)
	{
		objnum = obj_sort_list[i].objnum;
		if (obj_sort_list[i].vis_effect)
		{
			FlushWeaponStreamerBatches();
			DrawVisEffectMaybeBatched(&VisEffects[objnum]);
		}
		else
		{
			FlushVisEffectBatches();
			object* objp = &Objects[objnum];
			if (!RenderObjectHasWeaponStreamer(objp))
				FlushWeaponStreamerBatches();
			if (objp->type == OBJ_POWERUP)
				ForceFlushVisEffectBatches();
			if (objp == Viewer_object)
				continue;
			RenderObject(objp);
		}
	}
	ForceFlushVisEffectBatches();
	FlushWeaponStreamerBatches();
#ifdef _DEBUG
	Polymodel_outline_mode = save_polymodel_outline_mode;
#endif
}

// Either renders the objects in a room, or stuffs them into our postrender list
void CheckToRenderMineObjects(int roomnum)
{
	int index;
	float zdist;
	if (Render_mirror_for_room)
		return;
	for (index = Rooms[roomnum].objects; index != -1; index = Objects[index].next)
	{
		object* obj = &Objects[index];

		if (Objects[index].render_type == RT_NONE)
			continue;

		if (obj == Viewer_object)
			continue;

		// Don't draw piggybacked objects
		if (Viewer_object->type == OBJ_OBSERVER && index == Players[Viewer_object->id].piggy_objnum)
			continue;

		float size = Objects[index].size;
		// Special case weapons with streamers
		if (Objects[index].type == OBJ_WEAPON && (Weapons[Objects[index].id].flags & WF_STREAMER))
			size = Weapons[Objects[index].id].phys_info.velocity.z;

		// Check if object is trivially rejected
		int visible = IsPointVisible(&obj->pos, size, &zdist);		//calculate zdist
		if ((obj->type == OBJ_WEAPON && Weapons[obj->id].flags & WF_ELECTRICAL) || visible)
		{
			if (Num_postrenders < MAX_POSTRENDERS)
			{
				Postrender_list[Num_postrenders].type = PRT_OBJECT;
				Postrender_list[Num_postrenders].z = zdist;
				Postrender_list[Num_postrenders++].objnum = index;
			}
		}
	}
	// Now do viseffects
	for (index = Rooms[roomnum].vis_effects; index != -1; index = VisEffects[index].next)
	{
		if (VisEffects[index].type == VIS_NONE || VisEffects[index].flags & VF_DEAD)
			continue;

		if (IsPointVisible(&VisEffects[index].pos, VisEffects[index].size, &zdist))
		{
			if (Num_postrenders < MAX_POSTRENDERS)
			{
				Postrender_list[Num_postrenders].type = PRT_VISEFFECT;
				Postrender_list[Num_postrenders].z = zdist;
				Postrender_list[Num_postrenders++].visnum = index;
			}
		}
	}
}
// Renders all the mirrored rooms for this frame
void RenderMirrorRooms()
{
	if (!Detail_settings.Mirrored_surfaces)
		return;

	if (Num_mirror_rooms == 0)
		return;

	if (Render_use_newrender)
		return; 

	for (int i = 0; i < Num_mirror_rooms; i++)
	{
		room* rp = &Rooms[Mirror_rooms[i]];

		// Reset mirrors
		Render_mirror_for_room = false;
		bool do_mirror_face = false;
		rp->flags &= ~RF_MIRROR_VISIBLE;

		// Make sure its really ok to render this mirrored room
		if (rp->mirror_face != -1)
			do_mirror_face = true;
		if (rp->mirror_face >= rp->num_faces)
			do_mirror_face = false;
		if (do_mirror_face)
		{
			if (!(GameTextures[rp->faces[rp->mirror_face].tmap].flags & TF_ALPHA))
				do_mirror_face = false;
		}

		if (do_mirror_face)
		{
			int on_screen = 0;
			// See if any of the faces that share the mirror texture are on the screen
			for (int k = 0; k < rp->num_mirror_faces && on_screen == 0; k++)
			{
				face* fp = &rp->faces[rp->mirror_faces_list[k]];

				// See if this face is on screen
				int anded = 0xff;
				g3Point pnt;
				for (int t = 0; t < fp->num_verts; t++)
					anded &= g3_RotatePoint(&pnt, &rp->verts[fp->face_verts[t]]);
				if (!anded)
					on_screen = 1;
			}
			if (!on_screen)
				do_mirror_face = false;
		}
		if (do_mirror_face)	// This room has a mirror...render it first
		{
			Render_mirror_for_room = true;
			Mirror_room = rp - Rooms;

			BuildMirroredRoomList();
			rend_SuspendMotionVectorWrites();
			for (int t = Num_mirrored_rooms - 1; t >= 0; t--)
				RenderMirroredRoom(&Rooms[Mirrored_room_list[t]]);
			rend_ResumeMotionVectorWrites();
			Render_mirror_for_room = false;
		}
	}
	// Do z buffer trick
	rend_ClearZBuffer();
	rend_SetZBufferWriteMask(1);
	rend_SetZBufferState(1);

	// Draw mirror faces now
	for (int i = 0; i < Num_mirror_rooms; i++)
	{
		room* rp = &Rooms[Mirror_rooms[i]];
		face* fp = &rp->faces[rp->mirror_face];
		g3Point save_points[MAX_VERTS_PER_FACE];
		int save_index;

		// Now do the same for all faces in this room that share that texture of the mirror
		for (int k = 0; k < rp->num_faces; k++)
		{
			face* this_fp = &rp->faces[k];
			if (this_fp->tmap == fp->tmap)
			{
				int t;

				save_index = rp->wpb_index;
				for (t = 0; t < fp->num_verts; t++)
					save_points[t] = World_point_buffer[fp->face_verts[t]];
				DrawPostrenderFace(Mirror_rooms[i], rp->mirror_face, false);

				rp->wpb_index = save_index;
				for (t = 0; t < fp->num_verts; t++)
					World_point_buffer[fp->face_verts[t]] = save_points[t];
			}
		}
	}
}

//	Renders a room in just outline form
void RenderRoomOutline(room* rp)
{
	ddgr_color back_line_color, face_line_color;
	back_line_color = GR_RGB(100, 100, 100);
	face_line_color = GR_RGB(255, 255, 255);

	for (int fn = 0; fn < rp->num_faces; fn++)
	{
		face* fp = &rp->faces[fn];
		g3Point p0, p1;
		ubyte c0, c1;
		int v;
		ddgr_color color;

		for (v = 0; v < fp->num_verts; v++)
		{
			c0 = g3_RotatePoint(&p0, &rp->verts[fp->face_verts[v]]);
			c1 = g3_RotatePoint(&p1, &rp->verts[fp->face_verts[(v + 1) % fp->num_verts]]);
			if ((!(fp->flags & FF_VISIBLE)) || ((fp->flags & FF_NOT_FACING)))
			{
				//wouldn't normally be rendered
				color = back_line_color;
			}
			else
			{
				color = face_line_color;
			}
			if (!(c0 & c1)) //both not off screen?
			{
				//Draw current edge in green
				g3_DrawLine(color, &p0, &p1);
			}
		}
	}
}

//Draws the mine, starting at a the specified room
//The rendering surface must be set up, and g3_StartFrame() must have been called
//Parameters:	viewer_roomnum - what room the viewer is in
//					flag_automap - if true, flag segments as visited when rendered
//					called_from_terrain - set if calling this routine from the terrain renderer
void RenderMine(int viewer_roomnum, int flag_automap, int called_from_terrain)
{
	PERF_MARKER_SCOPE(called_from_terrain ? "RenderMine.FromTerrain" : "RenderMine.Main");
	renderer_3d_draw_call_scope room_draw_scope(RENDERER_DRAW_CALL_3D_ROOM);
	if (!called_from_terrain)
		Deferred_fog_ao_faces.clear();
#ifdef EDITOR
	In_editor_mode = (GetFunctionMode() == EDITOR_MODE);
#endif
	// check to see if we should render windows
	if (No_render_windows_hack == -1)
	{
		if (FindArg("-NoRenderWindows"))
			No_render_windows_hack = 1;
		else
			No_render_windows_hack = 0;
	}

	//Get the viewer eye so functions down the line can look at it
	g3_GetViewPosition(&Viewer_eye);
	g3_GetUnscaledMatrix(&Viewer_orient);

	//set these globals so functions down the line can look at them
	Viewer_roomnum = viewer_roomnum;
	Flag_automap = flag_automap;
	Called_from_terrain = called_from_terrain;

	//Assume no terrain
	Must_render_terrain = 0;

	if (Render_use_newrender)
	{
		PERF_MARKER_SCOPE("RenderMine.NewRender");
		NewRender_Render(Viewer_eye, Viewer_orient, viewer_roomnum);
		return;
	}

	//Get the width & height of the render window
	rend_GetProjectionParameters(&Render_width, &Render_height);
	if (!Called_from_terrain)
	{
		Terrain_portal_top = Render_height;
		Terrain_portal_bottom = 0;
		Terrain_portal_right = 0;
		Terrain_portal_left = Render_width;
	}

	//Build the list of visible rooms
	{
		PERF_MARKER_SCOPE("RenderMine.BuildRoomList");
		BuildRoomList(viewer_roomnum);		//fills in Render_list & N_render_segs
	}

	//If we determined that the terrain is visible, render it
	if (Must_render_terrain && !Called_from_terrain && !(In_editor_mode && Render_inside_only))
	{
		{
			PERF_MARKER_SCOPE("RenderMine.RenderTerrainPortal");
			RenderTerrain(1, Terrain_portal_left, Terrain_portal_top, Terrain_portal_right, Terrain_portal_bottom);
		}
		// Mark all room points to be rerotated due to terrain trashing our point list
		for (int i = 0; i <= Highest_room_index; i++)
		{
			Rooms[i].wpb_index = -1;
			Global_buffer_index = 0;
		}

		// Setup fog if needed
		g3_SetFarClipZ(VisibleTerrainZ);

		if (Terrain_sky.flags & TF_FOG)
		{
			rend_SetZValues(0, VisibleTerrainZ);
			rend_SetFogState(1);
			rend_SetFogBorders(VisibleTerrainZ * .85, VisibleTerrainZ);
			rend_SetFogColor(Terrain_sky.fog_color);
		}
		else
			rend_SetZValues(0, 5000);
	}

	// First render mirrored rooms
	{
		PERF_MARKER_SCOPE("RenderMine.RenderMirrorRooms");
		RenderMirrorRooms();
	}

	Num_mirror_rooms = 0;

	if (Render_use_newrender)
	{
	}
	else
	{
		PERF_MARKER_SCOPE("RenderMine.RenderRooms");
		//Render the list of rooms
		for (int nn = N_render_rooms - 1; nn >= 0; nn--)
		{
			int roomnum = Render_list[nn];
#ifdef _DEBUG
			if (In_editor_mode && Render_one_room_only && (roomnum != viewer_roomnum))
				continue;
#endif
			if (roomnum != -1)
			{
				if (Render_use_newrender)
				{
					RenderRoom(&Rooms[roomnum]);
				}
				else
				{
					ASSERT(Rooms_visited[roomnum] != 255);
					if (Outline_release_mode & 1) {
						RenderRoomOutline(&Rooms[roomnum]);
					}
					RenderRoom(&Rooms[roomnum]);
					Rooms_visited[roomnum] = (char)255;
					// Stuff objects into our postrender list
					CheckToRenderMineObjects(roomnum);
				}
			}
		}
	}

	RenderDeferredFogAOSuppression();

	rend_SetOverlayType(OT_NONE);	// turn off lightmap blending
	if (Must_render_terrain && !Called_from_terrain)
		rend_SetFogState(0);

#ifdef EDITOR
	if (OUTLINE_ON(OM_MINE))
	{
		OutlineCurrentFace(Curroomp, Curface, Curedge, Curvert, CURFACE_COLOR, CUREDGE_COLOR);
		if (Markedroomp)
			OutlineCurrentFace(Markedroomp, Markedface, Markededge, Markedvert, MARKEDFACE_COLOR, MARKEDEDGE_COLOR);
		if (Placed_room != -1)
			DrawPlacedRoomFace(&Rooms[Placed_room], &Placed_room_origin, &Placed_room_rotmat, &Placed_room_attachpoint, Placed_room_face, PLACED_COLOR);
	}
#endif
}

// Simply sets the number of glows to zero
void ResetLightGlows()
{
	Num_glows = 0;
	for (int i = 0; i < MAX_LIGHT_GLOWS; i++)
	{
		LightGlows[i].flags = 0;
	}
}

// Renders all the lights glows for this frame
void RenderLightGlows()
{
	// Render all the glows for this mine
	rend_SetColorModel(CM_RGB);
	rend_SetLighting(LS_GOURAUD);
	rend_SetTextureType(TT_LINEAR);
	rend_SetAlphaType(AT_SATURATE_TEXTURE);
	rend_SetOverlayType(OT_NONE);
	rend_SetFogState(0);
	rend_SetAOSuppression(1.0f);
	int count = 0;

	for (int i = 0; i < MAX_LIGHT_GLOWS && count < Num_glows; i++)
	{
		if (LightGlows[i].flags & LGF_USED)
		{
			RenderSingleLightGlow(i);
			count++;
		}
	}

	rend_SetAOSuppression(0.0f);
	rend_SetZBufferWriteMask(1);
	rend_SetZBufferState(1);
}

#define STATE_PUSH(val) {state_stack[state_stack_counter]=val; state_stack_counter++; ASSERT (state_stack_counter<2000);}
#define STATE_POP()	{state_stack_counter--; pop_val=state_stack[state_stack_counter];}
// Sorts our texture states using the quicksort algorithm
void SortStates(state_limited_element* state_array, int cellcount)
{
	state_limited_element v, t;
	int pop_val;
	int l = 0;
	int r = cellcount - 1;
	ushort state_stack_counter = 0;
	ushort state_stack[2000];

	while (1)
	{
		while (r > l)
		{
			int i = l - 1;
			int j = r;
			v = state_array[r];
			while (1)
			{
				while (state_array[++i].sort_key < v.sort_key)
					;
				while (state_array[--j].sort_key > v.sort_key)
					;
				if (i >= j)
					break;
				t = state_array[i];
				state_array[i] = state_array[j];
				state_array[j] = t;
			}
			t = state_array[i];
			state_array[i] = state_array[r];
			state_array[r] = t;

			if (i - l > r - i)
			{
				STATE_PUSH(l);
				STATE_PUSH(i - 1);
				l = i + 1;
			}
			else
			{
				STATE_PUSH(i + 1);
				STATE_PUSH(r);
				r = i - 1;
			}
		}
		if (!state_stack_counter)
			break;
		STATE_POP();
		r = pop_val;
		STATE_POP();
		l = pop_val;
	}
}

// Builds a list of mirror faces for each room and allocs memory accordingly
void ConsolidateMineMirrors()
{
	mprintf((0, "Consolidating mine mirrors!\n"));

	for (int i = 0; i < MAX_ROOMS; i++)
	{
		room* rp = &Rooms[i];
		if (!rp->used)
			continue;
		if (rp->mirror_faces_list)
		{
			mem_free(rp->mirror_faces_list);
			rp->mirror_faces_list = NULL;
			rp->num_mirror_faces = 0;
		}
		if (rp->mirror_face == -1)
			continue;

		// Count the number of faces that have the same texture as the mirror face
		int num_mirror_faces = 0;
		for (int t = 0; t < rp->num_faces; t++)
		{
			face* fp = &rp->faces[t];
			if (fp->tmap == rp->faces[rp->mirror_face].tmap)
				num_mirror_faces++;
		}
		if (num_mirror_faces == 0)
		{
			// No faces found?  Weird.
			rp->mirror_face = 0;
			continue;
		}

		rp->mirror_faces_list = (ushort*)mem_malloc(num_mirror_faces * sizeof(ushort));
		ASSERT(rp->mirror_faces_list);
		rp->num_mirror_faces = num_mirror_faces;

		// Now go through and fill in our list
		int count = 0;
		for (int t = 0; t < rp->num_faces; t++)
		{
			face* fp = &rp->faces[t];
			if (fp->tmap == rp->faces[rp->mirror_face].tmap)
				rp->mirror_faces_list[count++] = t;
		}
	}
}

// RenderBlankScreen
//	Renders a blank screen, to be used for UI callbacks to prevent Hall of mirrors with mouse cursor
void RenderBlankScreen(void)
{
	rend_ClearScreen(GR_BLACK);
}
