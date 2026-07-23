/*
* DESCENT 3MASTERED
* Copyright (C) 2024 Parallax Software
* Copyright (C) 2024 SaladBadger
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
#include <string.h>
#include "gl_local.h"
#include "gameloop.h"

#ifndef GL_UNSIGNED_SHORT_5_5_5_1
#define GL_UNSIGNED_SHORT_5_5_5_1 0x8034
#endif

#ifndef GL_UNSIGNED_SHORT_4_4_4_4
#define GL_UNSIGNED_SHORT_4_4_4_4 0x8033
#endif

ushort* OpenGL_bitmap_remap = NULL;
ushort* OpenGL_lightmap_remap = NULL;
ubyte* OpenGL_bitmap_states = NULL;
ubyte* OpenGL_lightmap_states = NULL;

unsigned int opengl_last_upload_res = 0;
uint* opengl_Upload_data = NULL;
uint* opengl_Translate_table = NULL;
uint* opengl_4444_translate_table = NULL;

ushort* opengl_packed_Upload_data = NULL;
ushort* opengl_packed_Translate_table = NULL;
ushort* opengl_packed_4444_translate_table = NULL;

//Texture list
GLuint texture_name_list[10000];
int Cur_texture_object_num = 1;
int Last_texel_unit_set = -1;

	int OpenGL_last_bound[4];
int OpenGL_sets_this_frame[10];
int OpenGL_uploads;

bool OpenGL_cache_initted;

void GL4Renderer::InitImages()
{
	memset(texture_name_list, 0, sizeof(texture_name_list));
	Cur_texture_object_num = 1;
	Last_texel_unit_set = -1;
}

void GL4Renderer::FreeImages()
{
	uint* delete_list = (uint*)mem_malloc(Cur_texture_object_num * sizeof(int));
	ASSERT(delete_list);
	for (int i = 1; i < Cur_texture_object_num; i++)
		delete_list[i] = i;

	if (Cur_texture_object_num > 1)
		glDeleteTextures(Cur_texture_object_num, (const uint*)delete_list);

	mem_free(delete_list);
}

void GL4Renderer::DestroyRetainedRoomLightmaps()
{
	glDeleteTextures(RETAINED_ROOM_LIGHTMAP_ARRAY_COUNT,
		retained_room_lightmap_arrays);
	memset(retained_room_lightmap_arrays, 0,
		sizeof(retained_room_lightmap_arrays));
	retained_room_lightmap_handles.clear();
	retained_room_lightmap_pages.clear();
	retained_room_lightmaps_ready = false;
}

void GL4Renderer::UploadRetainedRoomLightmap(int lightmap_handle)
{
	if (lightmap_handle < 0 ||
		lightmap_handle >= (int)retained_room_lightmap_pages.size())
		return;
	const int page = retained_room_lightmap_pages[lightmap_handle];
	if (page < 0)
		return;
	const int bucket = (page >> 16) & 0x7f;
	const int layer = page & 0x0000ffff;
	const int size = 2 << bucket;
	if (bucket < 0 || bucket >= RETAINED_ROOM_LIGHTMAP_ARRAY_COUNT ||
		retained_room_lightmap_arrays[bucket] == 0)
		return;

	SetUploadBufferSize(size, size);
	const int width = lm_w(lightmap_handle);
	const int height = lm_h(lightmap_handle);
	const ushort* source = lm_data(lightmap_handle);
	const int saved_last_texel_unit = Last_texel_unit_set;
	const int saved_active_unit =
		saved_last_texel_unit >= 0 ? saved_last_texel_unit : 0;
	glActiveTexture(GL_TEXTURE3 + bucket);
	glBindTexture(GL_TEXTURE_2D_ARRAY, retained_room_lightmap_arrays[bucket]);
	if (OpenGL_packed_pixels)
	{
		memset(opengl_packed_Upload_data, 0, size * size * sizeof(ushort));
		ushort* destination = opengl_packed_Upload_data;
		for (int y = 0; y < height; y++)
		{
			for (int x = 0; x < width; x++)
				destination[y * size + x] =
					opengl_packed_Translate_table[source[y * width + x]];
		}
		glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer,
			size, size, 1, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1,
			opengl_packed_Upload_data);
	}
	else
	{
		memset(opengl_Upload_data, 0, size * size * sizeof(uint));
		uint* destination = opengl_Upload_data;
		for (int y = 0; y < height; y++)
		{
			for (int x = 0; x < width; x++)
				destination[y * size + x] =
					opengl_Translate_table[source[y * width + x]];
		}
		glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer,
			size, size, 1, GL_RGBA, GL_UNSIGNED_BYTE,
			opengl_Upload_data);
	}
	glActiveTexture(GL_TEXTURE0 + saved_active_unit);
	Last_texel_unit_set = saved_active_unit;
	OpenGL_uploads++;
}

bool GL4Renderer::PrepareRetainedRoomLightmaps(const int* lightmap_handles,
	int count)
{
	DestroyRetainedRoomLightmaps();
	if (!OpenGL_cache_initted || !lightmap_handles || count <= 0)
		return false;

	// Room geometry is no longer the only retained consumer. Lightmapped
	// polymodels historically rebound a 2D lightmap for nearly every face; make
	// every live level lightmap addressable through the same arrays so those
	// models can batch by their real material instead of by lightmap page.
	std::vector<int> retained_lightmap_handles;
	retained_lightmap_handles.reserve(count + 128);
	std::vector<ubyte> included(MAX_LIGHTMAPS, 0);
	auto include_lightmap = [&](int handle)
	{
		if (handle < 0 || handle >= MAX_LIGHTMAPS || included[handle] ||
			!GameLightmaps[handle].used || (GameLightmaps[handle].flags & LF_WRAP))
			return;
		included[handle] = 1;
		retained_lightmap_handles.push_back(handle);
	};
	for (int index = 0; index < count; index++)
		include_lightmap(lightmap_handles[index]);
	for (int handle = 0; handle < MAX_LIGHTMAPS; handle++)
		include_lightmap(handle);
	lightmap_handles = retained_lightmap_handles.data();
	count = (int)retained_lightmap_handles.size();

	int highest_handle = -1;
	int layer_counts[RETAINED_ROOM_LIGHTMAP_ARRAY_COUNT] = {};
	for (int index = 0; index < count; index++)
	{
		const int handle = lightmap_handles[index];
		if (handle < 0 || handle >= MAX_LIGHTMAPS ||
			!GameLightmaps[handle].used || (GameLightmaps[handle].flags & LF_WRAP))
			continue;
		const int size = GameLightmaps[handle].square_res;
		int bucket = -1;
		for (int candidate = 0; candidate < RETAINED_ROOM_LIGHTMAP_ARRAY_COUNT;
			candidate++)
		{
			if (size == (2 << candidate))
			{
				bucket = candidate;
				break;
			}
		}
		if (bucket < 0)
			continue;
		highest_handle = std::max(highest_handle, handle);
		layer_counts[bucket]++;
	}
	if (highest_handle < 0)
		return false;

	retained_room_lightmap_pages.assign(highest_handle + 1, -1);
	int next_layers[RETAINED_ROOM_LIGHTMAP_ARRAY_COUNT] = {};
	for (int index = 0; index < count; index++)
	{
		const int handle = lightmap_handles[index];
		if (handle < 0 || handle > highest_handle ||
			retained_room_lightmap_pages[handle] >= 0 ||
			!GameLightmaps[handle].used || (GameLightmaps[handle].flags & LF_WRAP))
			continue;
		const int size = GameLightmaps[handle].square_res;
		int bucket = -1;
		for (int candidate = 0; candidate < RETAINED_ROOM_LIGHTMAP_ARRAY_COUNT;
			candidate++)
		{
			if (size == (2 << candidate))
			{
				bucket = candidate;
				break;
			}
		}
		if (bucket < 0)
			continue;
		const int layer = next_layers[bucket]++;
		retained_room_lightmap_pages[handle] = (bucket << 16) | layer;
		retained_room_lightmap_handles.push_back(handle);
	}

	glGenTextures(RETAINED_ROOM_LIGHTMAP_ARRAY_COUNT,
		retained_room_lightmap_arrays);
	for (int bucket = 0; bucket < RETAINED_ROOM_LIGHTMAP_ARRAY_COUNT; bucket++)
	{
		if (layer_counts[bucket] <= 0)
			continue;
		const int size = 2 << bucket;
		glActiveTexture(GL_TEXTURE3 + bucket);
		glBindTexture(GL_TEXTURE_2D_ARRAY,
			retained_room_lightmap_arrays[bucket]);
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 0);
		glTexImage3D(GL_TEXTURE_2D_ARRAY, 0,
			OpenGL_packed_pixels ? GL_RGB5_A1 : GL_RGBA8,
			size, size, layer_counts[bucket], 0, GL_RGBA,
			OpenGL_packed_pixels ? GL_UNSIGNED_SHORT_5_5_5_1 : GL_UNSIGNED_BYTE,
			nullptr);
	}
	for (int handle : retained_room_lightmap_handles)
	{
		UploadRetainedRoomLightmap(handle);
		GameLightmaps[handle].flags &= ~(LF_CHANGED | LF_BRAND_NEW);
	}
	glActiveTexture(GL_TEXTURE0);
	Last_texel_unit_set = 0;

	retained_room_lightmaps_ready = true;
	AutomatedCaptureLog("retained room lightmap arrays ready handles=%d",
		(int)retained_room_lightmap_handles.size());
	return true;
}

int GL4Renderer::GetRetainedRoomLightmapPage(int lightmap_handle) const
{
	if (!retained_room_lightmaps_ready || lightmap_handle < 0 ||
		lightmap_handle >= (int)retained_room_lightmap_pages.size())
		return -1;
	return retained_room_lightmap_pages[lightmap_handle];
}

void GL4Renderer::RefreshRetainedRoomLightmaps()
{
	if (!retained_room_lightmaps_ready)
		return;
	for (int handle : retained_room_lightmap_handles)
	{
		if (!(GameLightmaps[handle].flags & (LF_CHANGED | LF_BRAND_NEW)))
			continue;
		MakeBitmapCurrent(handle, MAP_TYPE_LIGHTMAP, 1);
		MakeWrapTypeCurrent(handle, MAP_TYPE_LIGHTMAP, 1);
		MakeFilterTypeCurrent(handle, MAP_TYPE_LIGHTMAP, 1);
		UploadRetainedRoomLightmap(handle);
	}
	for (int bucket = 0; bucket < RETAINED_ROOM_LIGHTMAP_ARRAY_COUNT; bucket++)
	{
		if (retained_room_lightmap_arrays[bucket] == 0)
			continue;
		glActiveTexture(GL_TEXTURE3 + bucket);
		glBindTexture(GL_TEXTURE_2D_ARRAY,
			retained_room_lightmap_arrays[bucket]);
	}
	glActiveTexture(GL_TEXTURE0);
	Last_texel_unit_set = 0;
}

int GL4Renderer::MakeTextureObject(int tn, bool wrap)
{
	int num = Cur_texture_object_num;

	Cur_texture_object_num++;

	if (texture_name_list[num] == 0)
		glGenTextures(1, &texture_name_list[num]);

	if (UseMultitexture && Last_texel_unit_set != tn)
	{
		glActiveTexture(GL_TEXTURE0 + tn);
		Last_texel_unit_set = tn;
	}

	num = texture_name_list[num];

	glBindTexture(GL_TEXTURE_2D, num);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 2);

	GLenum wraptype = wrap ? GL_REPEAT : GL_CLAMP_TO_EDGE;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wraptype);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wraptype);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, NUM_MIP_LEVELS - 1);

	//glTexEnvf (GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);

	CHECK_ERROR(2);

	return num;
}

extern bool Force_one_texture;

// Utilizes a LRU cacheing scheme to select/upload textures the opengl driver
int GL4Renderer::MakeBitmapCurrent(int handle, int map_type, int tn)
{
	int w, h;
	int texnum;

	if (map_type == MAP_TYPE_LIGHTMAP)
	{
		w = GameLightmaps[handle].square_res;
		h = GameLightmaps[handle].square_res;
	}
	else
	{
		if (Force_one_texture)
		{
			handle = 0;
		}

		w = bm_w(handle, 0);
		h = bm_h(handle, 0);
	}

	// See if the bitmaps is already in the cache
	if (map_type == MAP_TYPE_LIGHTMAP)
	{
		if (OpenGL_lightmap_remap[handle] == 65535)
		{
			texnum = MakeTextureObject(tn, false);
			SET_WRAP_STATE(OpenGL_lightmap_states[handle], WT_CLAMP);
			SET_FILTER_STATE(OpenGL_lightmap_states[handle], 0);
			OpenGL_lightmap_remap[handle] = texnum;
			TranslateBitmapToOpenGL(texnum, handle, map_type, 0, tn);
		}
		else
		{
			texnum = OpenGL_lightmap_remap[handle];
			if (GameLightmaps[handle].flags & LF_CHANGED)
				TranslateBitmapToOpenGL(texnum, handle, map_type, 1, tn);
		}
	}
	else
	{
		if (OpenGL_bitmap_remap[handle] == 65535)
		{
			texnum = MakeTextureObject(tn, true);
			SET_WRAP_STATE(OpenGL_bitmap_states[handle], WT_WRAP);
			SET_FILTER_STATE(OpenGL_bitmap_states[handle], 0);
			OpenGL_bitmap_remap[handle] = texnum;
			TranslateBitmapToOpenGL(texnum, handle, map_type, 0, tn);
		}
		else
		{
			texnum = OpenGL_bitmap_remap[handle];
			if (GameBitmaps[handle].flags & BF_CHANGED)
			{
				TranslateBitmapToOpenGL(texnum, handle, map_type, 1, tn);
			}
		}
	}

	if (OpenGL_last_bound[tn] != texnum)
	{
		if (UseMultitexture && Last_texel_unit_set != tn)
		{
			glActiveTexture(GL_TEXTURE0 + tn);
			Last_texel_unit_set = tn;
		}

		glBindTexture(GL_TEXTURE_2D, texnum);
		OpenGL_last_bound[tn] = texnum;
		OpenGL_sets_this_frame[0]++;
	}

	CHECK_ERROR(7);

	return 1;
}

// Sets up an appropriate wrap type for the current bound texture
void GL4Renderer::MakeWrapTypeCurrent(int handle, int map_type, int tn)
{
	int uwrap;
	wrap_type dest_wrap;

	if (map_type == MAP_TYPE_LIGHTMAP && (GameLightmaps[handle].flags & LF_WRAP))
		dest_wrap = WT_WRAP;
	else if (tn == 1)
		dest_wrap = WT_CLAMP;
	else
		dest_wrap = OpenGL_state.cur_wrap_type;

	if (map_type == MAP_TYPE_LIGHTMAP)
		uwrap = GET_WRAP_STATE(OpenGL_lightmap_states[handle]);
	else
		uwrap = GET_WRAP_STATE(OpenGL_bitmap_states[handle]);

	if (uwrap == dest_wrap)
		return;

	if (UseMultitexture && Last_texel_unit_set != tn)
	{
		glActiveTexture(GL_TEXTURE0 + tn);
		Last_texel_unit_set = tn;
	}

	OpenGL_sets_this_frame[1]++;

	if (dest_wrap == WT_CLAMP)
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	}
	else if (dest_wrap == WT_WRAP_V)
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	}
	else
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	}

	if (map_type == MAP_TYPE_LIGHTMAP)
	{
		SET_WRAP_STATE(OpenGL_lightmap_states[handle], dest_wrap);
	}
	else
	{
		SET_WRAP_STATE(OpenGL_bitmap_states[handle], dest_wrap);
	}

	CHECK_ERROR(8);
}

// Chooses the correct filter type for the currently bound texture
void GL4Renderer::MakeFilterTypeCurrent(int handle, int map_type, int tn)
{
	int magf, mmip, anisotropy_state;
	sbyte dest_filter, dest_mip;
	int dest_anisotropy_state = 0;

	if (map_type == MAP_TYPE_LIGHTMAP)
	{
		magf = GET_FILTER_STATE(OpenGL_lightmap_states[handle]);
		mmip = GET_MIP_STATE(OpenGL_lightmap_states[handle]);
		anisotropy_state = GET_ANISOTROPY_STATE(OpenGL_lightmap_states[handle]);
		dest_filter = 1;
		dest_mip = 0;
	}
	else
	{
		magf = GET_FILTER_STATE(OpenGL_bitmap_states[handle]);
		mmip = GET_MIP_STATE(OpenGL_bitmap_states[handle]);
		anisotropy_state = GET_ANISOTROPY_STATE(OpenGL_bitmap_states[handle]);
		dest_filter = OpenGL_preferred_state.filtering;
		if (!OpenGL_state.cur_bilinear_state)
			dest_filter = 0;
		dest_mip = OpenGL_preferred_state.mipping;
		if (!OpenGL_state.cur_mip_state || !bm_mipped(handle))
			dest_mip = 0;
		if (dest_mip)
		{
			const int requested = std::max(1, (int)OpenGL_preferred_state.anisotropy);
			const int effective = std::min(requested, GetMaxAnisotropy());
			while ((1 << dest_anisotropy_state) < effective)
				dest_anisotropy_state++;
		}
	}

	if (UseMultitexture && Last_texel_unit_set != tn)
	{
		glActiveTexture(GL_TEXTURE0 + tn);
		Last_texel_unit_set = tn;
	}

	if (magf == dest_filter && mmip == dest_mip && anisotropy_state == dest_anisotropy_state)
		return;

	GLenum mag_filter = dest_filter ? GL_LINEAR : GL_NEAREST;
	GLenum min_filter;
	if (dest_mip)
	{
		min_filter = dest_filter ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST;
		//This is a bit hacky, this should be set once at texture creation.
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, NUM_MIP_LEVELS - 1);
	}
	else
	{
		min_filter = dest_filter ? GL_LINEAR : GL_NEAREST;
	}

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_filter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);
	if (OpenGL_max_anisotropy > 1.0f)
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
			(float)(1 << dest_anisotropy_state));

	OpenGL_sets_this_frame[2]++;

	if (map_type == MAP_TYPE_LIGHTMAP)
	{
		SET_FILTER_STATE(OpenGL_lightmap_states[handle], dest_filter);
		SET_MIP_STATE(OpenGL_lightmap_states[handle], dest_mip);
		SET_ANISOTROPY_STATE(OpenGL_lightmap_states[handle], dest_anisotropy_state);
	}
	else
	{
		SET_FILTER_STATE(OpenGL_bitmap_states[handle], dest_filter);
		SET_MIP_STATE(OpenGL_bitmap_states[handle], dest_mip);
		SET_ANISOTROPY_STATE(OpenGL_bitmap_states[handle], dest_anisotropy_state);
	}

	CHECK_ERROR(9);
}

int GL4Renderer::InitCache()
{
	OpenGL_bitmap_remap = (ushort*)mem_malloc(MAX_BITMAPS * 2);
	ASSERT(OpenGL_bitmap_remap);
	OpenGL_lightmap_remap = (ushort*)mem_malloc(MAX_LIGHTMAPS * 2);
	ASSERT(OpenGL_lightmap_remap);

	OpenGL_bitmap_states = (ubyte*)mem_malloc(MAX_BITMAPS);
	ASSERT(OpenGL_bitmap_states);
	OpenGL_lightmap_states = (ubyte*)mem_malloc(MAX_LIGHTMAPS);
	ASSERT(OpenGL_lightmap_states);

	Cur_texture_object_num = 1;

	// Setup textures and cacheing
	int i;
	for (i = 0; i < MAX_BITMAPS; i++)
	{
		OpenGL_bitmap_remap[i] = 65535;
		OpenGL_bitmap_states[i] = 255;
		GameBitmaps[i].flags |= BF_CHANGED | BF_BRAND_NEW;
	}

	for (i = 0; i < MAX_LIGHTMAPS; i++)
	{
		OpenGL_lightmap_remap[i] = 65535;
		OpenGL_lightmap_states[i] = 255;
		GameLightmaps[i].flags |= LF_CHANGED | LF_BRAND_NEW;
	}

	/*glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	if (UseMultitexture)
	{
		glActiveTexture(GL_TEXTURE0 + 1);
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		glActiveTexture(GL_TEXTURE0 + 0);
	}*/

	CHECK_ERROR(3);

	OpenGL_cache_initted = true;
	return 1;
}

void GL4Renderer::FreeCache()
{
	if (OpenGL_cache_initted)
	{
		mem_free(OpenGL_lightmap_remap);
		mem_free(OpenGL_bitmap_remap);
		mem_free(OpenGL_lightmap_states);
		mem_free(OpenGL_bitmap_states);
		OpenGL_cache_initted = false;
	}
}

// Resets the texture cache
void GL4Renderer::ResetCache()
{
	// ResetCache rebuilds the legacy texture remap tables but does not delete
	// the GL texture objects. Bindless room handles therefore remain valid, and
	// keeping them resident avoids silently falling back after loading-screen
	// framebuffer copies reset the legacy cache.
	if (OpenGL_cache_initted)
	{
		mem_free(OpenGL_lightmap_remap);
		mem_free(OpenGL_bitmap_remap);
		mem_free(OpenGL_lightmap_states);
		mem_free(OpenGL_bitmap_states);
		OpenGL_cache_initted = false;
	}

	InitCache();
}

void GL4Renderer::FreeUploadBuffers()
{
	if (OpenGL_packed_pixels)
	{
		if (opengl_packed_Upload_data)
		{
			mem_free(opengl_packed_Upload_data);
		}
		if (opengl_packed_Translate_table)
		{
			mem_free(opengl_packed_Translate_table);
		}
		if (opengl_packed_4444_translate_table)
		{
			mem_free(opengl_packed_4444_translate_table);
		}
		opengl_packed_Upload_data = NULL;
		opengl_packed_Translate_table = NULL;
		opengl_packed_4444_translate_table = NULL;
	}
	else
	{
		if (opengl_Upload_data)
			mem_free(opengl_Upload_data);
		if (opengl_Translate_table)
			mem_free(opengl_Translate_table);
		if (opengl_4444_translate_table)
			mem_free(opengl_4444_translate_table);
		opengl_Upload_data = NULL;
		opengl_Translate_table = NULL;
		opengl_4444_translate_table = NULL;
	}
	opengl_last_upload_res = 0;
}

void GL4Renderer::SetUploadBufferSize(int width, int height)
{
	if ((width * height) <= opengl_last_upload_res)
		return;

	FreeUploadBuffers();

	if (OpenGL_packed_pixels)
	{
		opengl_packed_Upload_data = (ushort*)mem_malloc(width * height * 2);
		opengl_packed_Translate_table = (ushort*)mem_malloc(65536 * 2);
		opengl_packed_4444_translate_table = (ushort*)mem_malloc(65536 * 2);

		ASSERT(opengl_packed_Upload_data);
		ASSERT(opengl_packed_Translate_table);
		ASSERT(opengl_packed_4444_translate_table);

		mprintf((0, "Building packed OpenGL translate table...\n"));

		for (int i = 0; i < 65536; i++)
		{
			int r = (i >> 10) & 0x1f;
			int g = (i >> 5) & 0x1f;
			int b = i & 0x1f;

#ifdef BRIGHTNESS_HACK
			r *= BRIGHTNESS_HACK;
			g *= BRIGHTNESS_HACK;
			b *= BRIGHTNESS_HACK;
			if (r > 0x1F) r = 0x1F;
			if (g > 0x1F) g = 0x1F;
			if (b > 0x1F) b = 0x1F;
#endif

			ushort pix;

			if (!(i & OPAQUE_FLAG))
			{
				pix = 0;
			}
			else
			{
				pix = (r << 11) | (g << 6) | (b << 1) | 1;
			}

			opengl_packed_Translate_table[i] = pix;

			// 4444 table
			int a = (i >> 12) & 0xf;
			r = (i >> 8) & 0xf;
			g = (i >> 4) & 0xf;
			b = i & 0xf;

			pix = (r << 12) | (g << 8) | (b << 4) | a;
			opengl_packed_4444_translate_table[i] = pix;
		}
	}
	else
	{
		opengl_Upload_data = (uint*)mem_malloc(width * height * 4);
		opengl_Translate_table = (uint*)mem_malloc(65536 * 4);
		opengl_4444_translate_table = (uint*)mem_malloc(65536 * 4);

		ASSERT(opengl_Upload_data);
		ASSERT(opengl_Translate_table);
		ASSERT(opengl_4444_translate_table);

		mprintf((0, "Building OpenGL translate table...\n"));

		for (int i = 0; i < 65536; i++)
		{
			uint pix;
			int r = (i >> 10) & 0x1f;
			int g = (i >> 5) & 0x1f;
			int b = i & 0x1f;

#ifdef BRIGHTNESS_HACK
			r *= BRIGHTNESS_HACK;
			g *= BRIGHTNESS_HACK;
			b *= BRIGHTNESS_HACK;
			if (r > 0x1F) r = 0x1F;
			if (g > 0x1F) g = 0x1F;
			if (b > 0x1F) b = 0x1F;
#endif

			float fr = (float)r / 31.0f;
			float fg = (float)g / 31.0f;
			float fb = (float)b / 31.0f;

			r = 255 * fr;
			g = 255 * fg;
			b = 255 * fb;

			if (!(i & OPAQUE_FLAG))
			{
				pix = 0;
			}
			else
			{
				pix = (255 << 24) | (b << 16) | (g << 8) | (r);
			}

			opengl_Translate_table[i] = pix;

			// Do 4444
			int a = (i >> 12) & 0xf;
			r = (i >> 8) & 0xf;
			g = (i >> 4) & 0xf;
			b = i & 0xf;

			float fa = (float)a / 15.0f;
			fr = (float)r / 15.0f;
			fg = (float)g / 15.0f;
			fb = (float)b / 15.0f;

			a = 255 * fa;
			r = 255 * fr;
			g = 255 * fg;
			b = 255 * fb;

			pix = (a << 24) | (b << 16) | (g << 8) | (r);

			opengl_4444_translate_table[i] = pix;
		}
	}

	opengl_last_upload_res = width * height;
}

// Takes our 16bit format and converts it into the memory scheme that OpenGL wants
void GL4Renderer::TranslateBitmapToOpenGL(int texnum, int bm_handle, int map_type, int replace, int tn)
{
	ushort* bm_ptr;

	int w, h;
	int size;

	if (UseMultitexture && Last_texel_unit_set != tn)
	{
		glActiveTexture(GL_TEXTURE0 + tn);
		Last_texel_unit_set = tn;
	}

	if (map_type == MAP_TYPE_LIGHTMAP)
	{
		if (GameLightmaps[bm_handle].flags & LF_BRAND_NEW)
			replace = 0;

		bm_ptr = lm_data(bm_handle);
		GameLightmaps[bm_handle].flags &= ~(LF_CHANGED | LF_BRAND_NEW);

		w = lm_w(bm_handle);
		h = lm_h(bm_handle);
		size = GameLightmaps[bm_handle].square_res;
	}
	else
	{
		if (GameBitmaps[bm_handle].flags & BF_BRAND_NEW)
			replace = 0;

		bm_ptr = bm_data(bm_handle, 0);
		GameBitmaps[bm_handle].flags &= ~(BF_CHANGED | BF_BRAND_NEW);
		w = bm_w(bm_handle, 0);
		h = bm_h(bm_handle, 0);
		size = w;
	}

	if (OpenGL_last_bound[tn] != texnum)
	{
		glBindTexture(GL_TEXTURE_2D, texnum);
		OpenGL_sets_this_frame[0]++;
		OpenGL_last_bound[tn] = texnum;
	}

	// Optional modern assets can retain their original RGBA8 source alongside
	// the legacy 16-bit bitmap. Upload that source directly so the bitmap
	// compatibility layer does not quantize it to 1555 first.
	const ubyte *truecolor_data = map_type == MAP_TYPE_BITMAP ? bm_data_truecolor(bm_handle) : NULL;
	if (truecolor_data)
	{
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		if (replace)
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, truecolor_data);
		else
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, truecolor_data);

		if (bm_mipped(bm_handle))
		{
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, NUM_MIP_LEVELS - 1);
			glGenerateMipmap(GL_TEXTURE_2D);
		}
		else
		{
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
		}
		glPixelStorei(GL_UNPACK_ALIGNMENT, 2);

		CHECK_ERROR(6);
		OpenGL_uploads++;
		return;
	}

	SetUploadBufferSize(w, h);

	int i;

	if (OpenGL_packed_pixels)
	{
		if (map_type == MAP_TYPE_LIGHTMAP)
		{
			ushort* left_data = (ushort*)opengl_packed_Upload_data;
			int bm_left = 0;

			for (int i = 0; i < h; i++, left_data += size, bm_left += w)
			{
				ushort* dest_data = left_data;
				for (int t = 0; t < w; t++)
				{
					*dest_data++ = opengl_packed_Translate_table[bm_ptr[bm_left + t]];
				}
			}

			if (replace)
			{
				glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size, size, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, opengl_packed_Upload_data);
			}
			else
			{
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB5_A1, size, size, 0, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, opengl_packed_Upload_data);
			}
		}
		else
		{
			int limit = 0;

			if (bm_mipped(bm_handle))
			{
				limit = NUM_MIP_LEVELS;
			}
			else
			{
				limit = 1;
			}

			for (int m = 0; m < limit; m++)
			{
				if (m < NUM_MIP_LEVELS)
				{
					bm_ptr = bm_data(bm_handle, m);
					w = bm_w(bm_handle, m);
					h = bm_h(bm_handle, m);
				}
				else
				{
					bm_ptr = bm_data(bm_handle, NUM_MIP_LEVELS - 1);
					w = bm_w(bm_handle, NUM_MIP_LEVELS - 1);
					h = bm_h(bm_handle, NUM_MIP_LEVELS - 1);

					w >>= m - (NUM_MIP_LEVELS - 1);
					h >>= m - (NUM_MIP_LEVELS - 1);

					if ((w < 1) || (h < 1))
						continue;

				}

				if (bm_format(bm_handle) == BITMAP_FORMAT_4444)
				{
					// Do 4444
					for (i = 0; i < w * h; i++)
						opengl_packed_Upload_data[i] = opengl_packed_4444_translate_table[bm_ptr[i]];

					if (replace)
					{
						glTexSubImage2D(GL_TEXTURE_2D, m, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, opengl_packed_Upload_data);
					}
					else
					{
						glTexImage2D(GL_TEXTURE_2D, m, GL_RGBA4, w, h, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, opengl_packed_Upload_data);
					}
				}
				else
				{
					// Do 1555
					for (i = 0; i < w * h; i++)
					{
						opengl_packed_Upload_data[i] = opengl_packed_Translate_table[bm_ptr[i]];
					}

					if (replace)
					{
						glTexSubImage2D(GL_TEXTURE_2D, m, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, opengl_packed_Upload_data);
					}
					else
					{
						glTexImage2D(GL_TEXTURE_2D, m, GL_RGB5_A1, w, h, 0, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, opengl_packed_Upload_data);
					}
				}
			}
		}
	}
	else
	{
		if (map_type == MAP_TYPE_LIGHTMAP)
		{
			uint* left_data = (uint*)opengl_Upload_data;
			int bm_left = 0;

			for (int i = 0; i < h; i++, left_data += size, bm_left += w)
			{
				uint* dest_data = left_data;
				for (int t = 0; t < w; t++)
				{
					*dest_data++ = opengl_Translate_table[bm_ptr[bm_left + t]];
				}
			}
			if (size > 0)
			{
				if (replace)
				{
					glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, opengl_Upload_data);
				}
				else
				{
					glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, opengl_Upload_data);
				}
			}
		}
		else
		{
			int limit = 0;

			if (bm_mipped(bm_handle))
			{
				limit = NUM_MIP_LEVELS;
			}
			else
			{
				limit = 1;
			}

			for (int m = 0; m < limit; m++)
			{
				bm_ptr = bm_data(bm_handle, m);
				w = bm_w(bm_handle, m);
				h = bm_h(bm_handle, m);

				if (bm_format(bm_handle) == BITMAP_FORMAT_4444)
				{
					// Do 4444
					for (i = 0; i < w * h; i++)
						opengl_Upload_data[i] = opengl_4444_translate_table[bm_ptr[i]];
				}
				else
				{
					// Do 1555

					for (i = 0; i < w * h; i++)
						opengl_Upload_data[i] = opengl_Translate_table[bm_ptr[i]];
				}

				//rcg06262000 my if wrapper.
				if ((w > 0) && (h > 0))
				{
					if (replace)
					{
						glTexSubImage2D(GL_TEXTURE_2D, m, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, opengl_Upload_data);
					}
					else
					{
						glTexImage2D(GL_TEXTURE_2D, m, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, opengl_Upload_data);
					}
				}
			}
		}
	}

	//mprintf ((1,"Doing slow upload to opengl!\n"));

	if (map_type == MAP_TYPE_LIGHTMAP)
	{
		GameLightmaps[bm_handle].flags &= ~LF_LIMITS;
	}

	CHECK_ERROR(6);
	OpenGL_uploads++;
}

void GL4Renderer::ChangeChunkedBitmap(int bm_handle, chunked_bitmap* chunk)
{
	int bw = bm_w(bm_handle, 0);
	int bh = bm_h(bm_handle, 0);

	//determine optimal size of the square bitmaps
	float fopt = 128.0f;
	int iopt;

	//find the smallest dimension and base off that
	int smallest = std::min(bw, bh);

	if (smallest <= 32)
		fopt = 32;
	else
		if (smallest <= 64)
			fopt = 64;
		else
			fopt = 128;

	iopt = (int)fopt;

	// Get how many pieces we need across and down
	float temp = bw / fopt;
	int how_many_across = temp;
	if ((temp - how_many_across) > 0)
		how_many_across++;

	temp = bh / fopt;
	int how_many_down = temp;
	if ((temp - how_many_down) > 0)
		how_many_down++;

	ASSERT(how_many_across > 0);
	ASSERT(how_many_down > 0);

	// Now go through our big bitmap and partition it into pieces
	ushort* src_data = bm_data(bm_handle, 0);
	ushort* sdata;
	ushort* ddata;

	int shift;
	switch (iopt)
	{
	case 32:
		shift = 5;
		break;
	case 64:
		shift = 6;
		break;
	case 128:
		shift = 7;
		break;
	default:
		Int3(); //Get Jeff
		break;
	}
	int maxx, maxy;
	int windex, hindex;
	int s_y, s_x, d_y, d_x;

	for (hindex = 0; hindex < how_many_down; hindex++)
	{
		for (windex = 0; windex < how_many_across; windex++)
		{
			//loop through the chunks
			//find end x and y
			if (windex < how_many_across - 1)
				maxx = iopt;
			else
				maxx = bw - (windex << shift);
			if (hindex < how_many_down - 1)
				maxy = iopt;
			else
				maxy = bh - (hindex << shift);

			//find the starting source x and y
			s_x = (windex << shift);
			s_y = (hindex << shift);

			//get the pointers pointing to the right spot
			ddata = bm_data(chunk->bm_array[hindex * how_many_across + windex], 0);
			GameBitmaps[chunk->bm_array[hindex * how_many_across + windex]].flags |= BF_CHANGED;
			sdata = &src_data[s_y * bw + s_x];

			//copy the data
			for (d_y = 0; d_y < maxy; d_y++)
			{
				for (d_x = 0; d_x < maxx; d_x++)
				{
					ddata[d_x] = sdata[d_x];
				}//end for d_x
				sdata += bw;
				ddata += iopt;
			}//end for d_y

		}//end for windex
	}//end for hindex
}

// Takes a bitmap and blits it to the screen using linear frame buffer stuff
// X and Y are the destination X,Y
void GL4Renderer::CopyBitmapToFramebuffer(int bm_handle, int x, int y)
{
	FlushFontBatch();

	ASSERT(opengl_Framebuffer_ready);

	if (opengl_Framebuffer_ready == 1)
	{
		bm_CreateChunkedBitmap(bm_handle, &opengl_Chunked_bitmap);
		opengl_Framebuffer_ready = 2;
	}
	else
	{
		ChangeChunkedBitmap(bm_handle, &opengl_Chunked_bitmap);
	}

	DrawChunkedBitmap(&opengl_Chunked_bitmap, 0, 0, 255);
}

// Gets a renderer ready for a framebuffer copy, or stops a framebuffer copy
void GL4Renderer::SetFrameBufferCopyState(bool state)
{
	FlushFontBatch();

	if (state)
	{
		ASSERT(opengl_Framebuffer_ready == 0);
		opengl_Framebuffer_ready = 1;
	}
	else
	{
		ASSERT(opengl_Framebuffer_ready != 0);
		opengl_Framebuffer_ready = 0;

		if (opengl_Framebuffer_ready == 2)
		{
			bm_DestroyChunkedBitmap(&opengl_Chunked_bitmap);
			ResetCache();
		}
	}
}

void GL4Renderer::BindBitmap(int handle)
{
	MakeBitmapCurrent(handle, MAP_TYPE_BITMAP, 0);
	MakeWrapTypeCurrent(handle, MAP_TYPE_BITMAP, 0);
	MakeFilterTypeCurrent(handle, MAP_TYPE_BITMAP, 0);
}

void GL4Renderer::BindLightmap(int handle)
{
	MakeBitmapCurrent(handle, MAP_TYPE_LIGHTMAP, 1);
	MakeWrapTypeCurrent(handle, MAP_TYPE_LIGHTMAP, 1);
	MakeFilterTypeCurrent(handle, MAP_TYPE_LIGHTMAP, 1);
}

void GL4Renderer::ClearBoundTextures()
{
	FlushFontBatch();

	OpenGL_last_bound[0] = -1;
	OpenGL_last_bound[1] = -1;
	OpenGL_last_bound[2] = -1;
	OpenGL_last_bound[3] = -1;
	Last_texel_unit_set = -1;
}
