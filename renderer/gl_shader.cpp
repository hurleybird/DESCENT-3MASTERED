/*
* Descent 3MASTERED
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
#include <string>
#include <vector>
#include "CFILE.H"
#include "pserror.h"
#include "renderer.h"
#include "gl_local.h"
#include "gameloop.h"

constexpr int TERRAIN_FOG_COUNTER_MAX = 100;

GLuint commonbuffername;
GLuint legacycommonbuffername;
GLuint fogbuffername;
GLuint specularbuffername;
GLuint terrainfogbuffername;
int terrainfogcounter = 0;

ShaderProgram* lastshaderprog = nullptr;

constexpr int COMMON_BINDING = 0;
constexpr int LEGACY_BINDING = 1;
constexpr int SPECULAR_BINDING = 2;
constexpr int ROOM_BINDING = 3;
constexpr int TERRAIN_FOG_BINDING = 4;

static bool InvertMatrix4(const float m[16], float out[16])
{
	float inv[16];

	inv[0] = m[5] * m[10] * m[15] -
		m[5] * m[11] * m[14] -
		m[9] * m[6] * m[15] +
		m[9] * m[7] * m[14] +
		m[13] * m[6] * m[11] -
		m[13] * m[7] * m[10];

	inv[4] = -m[4] * m[10] * m[15] +
		m[4] * m[11] * m[14] +
		m[8] * m[6] * m[15] -
		m[8] * m[7] * m[14] -
		m[12] * m[6] * m[11] +
		m[12] * m[7] * m[10];

	inv[8] = m[4] * m[9] * m[15] -
		m[4] * m[11] * m[13] -
		m[8] * m[5] * m[15] +
		m[8] * m[7] * m[13] +
		m[12] * m[5] * m[11] -
		m[12] * m[7] * m[9];

	inv[12] = -m[4] * m[9] * m[14] +
		m[4] * m[10] * m[13] +
		m[8] * m[5] * m[14] -
		m[8] * m[6] * m[13] -
		m[12] * m[5] * m[10] +
		m[12] * m[6] * m[9];

	inv[1] = -m[1] * m[10] * m[15] +
		m[1] * m[11] * m[14] +
		m[9] * m[2] * m[15] -
		m[9] * m[3] * m[14] -
		m[13] * m[2] * m[11] +
		m[13] * m[3] * m[10];

	inv[5] = m[0] * m[10] * m[15] -
		m[0] * m[11] * m[14] -
		m[8] * m[2] * m[15] +
		m[8] * m[3] * m[14] +
		m[12] * m[2] * m[11] -
		m[12] * m[3] * m[10];

	inv[9] = -m[0] * m[9] * m[15] +
		m[0] * m[11] * m[13] +
		m[8] * m[1] * m[15] -
		m[8] * m[3] * m[13] -
		m[12] * m[1] * m[11] +
		m[12] * m[3] * m[9];

	inv[13] = m[0] * m[9] * m[14] -
		m[0] * m[10] * m[13] -
		m[8] * m[1] * m[14] +
		m[8] * m[2] * m[13] +
		m[12] * m[1] * m[10] -
		m[12] * m[2] * m[9];

	inv[2] = m[1] * m[6] * m[15] -
		m[1] * m[7] * m[14] -
		m[5] * m[2] * m[15] +
		m[5] * m[3] * m[14] +
		m[13] * m[2] * m[7] -
		m[13] * m[3] * m[6];

	inv[6] = -m[0] * m[6] * m[15] +
		m[0] * m[7] * m[14] +
		m[4] * m[2] * m[15] -
		m[4] * m[3] * m[14] -
		m[12] * m[2] * m[7] +
		m[12] * m[3] * m[6];

	inv[10] = m[0] * m[5] * m[15] -
		m[0] * m[7] * m[13] -
		m[4] * m[1] * m[15] +
		m[4] * m[3] * m[13] +
		m[12] * m[1] * m[7] -
		m[12] * m[3] * m[5];

	inv[14] = -m[0] * m[5] * m[14] +
		m[0] * m[6] * m[13] +
		m[4] * m[1] * m[14] -
		m[4] * m[2] * m[13] -
		m[12] * m[1] * m[6] +
		m[12] * m[2] * m[5];

	inv[3] = -m[1] * m[6] * m[11] +
		m[1] * m[7] * m[10] +
		m[5] * m[2] * m[11] -
		m[5] * m[3] * m[10] -
		m[9] * m[2] * m[7] +
		m[9] * m[3] * m[6];

	inv[7] = m[0] * m[6] * m[11] -
		m[0] * m[7] * m[10] -
		m[4] * m[2] * m[11] +
		m[4] * m[3] * m[10] +
		m[8] * m[2] * m[7] -
		m[8] * m[3] * m[6];

	inv[11] = -m[0] * m[5] * m[11] +
		m[0] * m[7] * m[9] +
		m[4] * m[1] * m[11] -
		m[4] * m[3] * m[9] -
		m[8] * m[1] * m[7] +
		m[8] * m[3] * m[5];

	inv[15] = m[0] * m[5] * m[10] -
		m[0] * m[6] * m[9] -
		m[4] * m[1] * m[10] +
		m[4] * m[2] * m[9] +
		m[8] * m[1] * m[6] -
		m[8] * m[2] * m[5];

	float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
	if (det == 0.0f)
		return false;

	det = 1.0f / det;
	for (int i = 0; i < 16; i++)
		out[i] = inv[i] * det;
	return true;
}

//Shader pipeline system.
// Contains the named shader definitions requested by retained GL4 rendering.
ShaderDefinition gl_shaderdefs[] =
{
	{"lightmap", SF_HASCOMMON, "lightmap.vert", "lightmap.frag"},
	{"lightmap_room", SF_HASCOMMON, "lightmap_room.vert", "lightmap_room.frag"},
	{"lightmapped_specular", SF_HASCOMMON | SF_HASSPECULAR, "lightmap_specular.vert", "lightmap_specular.frag"},
	{"lightmap_room_fog", SF_HASCOMMON | SF_HASROOM, "lightmap_room_fog.vert", "lightmap_room_fog.frag"},
	{"lightmap_room_specular_fog", SF_HASCOMMON | SF_HASROOM | SF_HASSPECULAR, "lightmap_room_specular_fog.vert", "lightmap_room_specular_fog.frag"},
	{"terrain_lightmap", SF_HASCOMMON, "terrain_lightmap.vert", "terrain_lightmap.frag"},
	{"terrain_lightmap_fog", SF_HASCOMMON, "terrain_lightmap_fog.vert", "terrain_lightmap_fog.frag"},
	{"terrain_retained", SF_HASCOMMON, "terrain_retained.vert", "terrain_retained.frag"},
	{"terrain_retained_fog", SF_HASCOMMON, "terrain_retained.vert", "terrain_retained_fog.frag"},
	{"unlit_room", SF_HASCOMMON, "unlit_room.vert", "unlit_room.frag"},
	{"unlit_room_fog", SF_HASCOMMON | SF_HASROOM, "unlit_room_fog.vert", "unlit_room_fog.frag"},
	{"fog_portal", SF_HASCOMMON | SF_HASROOM, "fog_portal.vert", "fog_portal.frag"},
};

#define NUM_SHADERDEFS sizeof(gl_shaderdefs) / sizeof(gl_shaderdefs[0])

ShaderProgram gl_shaderprogs[NUM_SHADERDEFS];

struct GL4PipelineWarmupVertex
{
	float attributes[15][4];
	int retained_source_vertex;
};

static void GL4SetPipelineWarmupVertex(GL4PipelineWarmupVertex& vertex,
	float x, float y)
{
	memset(&vertex, 0, sizeof(vertex));
	vertex.attributes[0][0] = x;
	vertex.attributes[0][1] = y;
	vertex.attributes[0][2] = 0.0f;
	vertex.attributes[0][3] = 1.0f;
	vertex.attributes[1][0] = 1.0f;
	vertex.attributes[1][1] = 1.0f;
	vertex.attributes[1][2] = 1.0f;
	vertex.attributes[1][3] = 1.0f;
	vertex.attributes[2][0] = 0.5f;
	vertex.attributes[2][1] = 0.5f;
	vertex.attributes[2][2] = 1.0f;
	vertex.attributes[3][0] = 0.5f;
	vertex.attributes[3][1] = 0.5f;
	vertex.attributes[3][2] = 1.0f;
	vertex.attributes[4][2] = 1.0f;
	vertex.attributes[4][3] = 1.0f;
	vertex.attributes[5][3] = 1.0f;
	vertex.attributes[6][3] = 1.0f;
	for (int attribute = 7; attribute < 15; attribute++)
		vertex.attributes[attribute][3] = 1.0f;
}

void GL4Renderer::InitShaders()
{
	lastshaderprog = nullptr;
	pipeline_warmup_next = 0;
	glGenBuffers(1, &commonbuffername);
	glBindBuffer(GL_COPY_WRITE_BUFFER, commonbuffername);
	glBufferData(GL_COPY_WRITE_BUFFER, sizeof(CommonBlock) * 35, nullptr, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_UNIFORM_BUFFER, COMMON_BINDING, commonbuffername);

#ifdef _DEBUG
	GLenum err = glGetError();
	if (err != GL_NO_ERROR)
		Int3();
#endif

	//The legacy common buffer uses the ortho matrix as a passthrough.
	glGenBuffers(1, &legacycommonbuffername);
	glBindBuffer(GL_COPY_WRITE_BUFFER, legacycommonbuffername);
	glBufferData(GL_COPY_WRITE_BUFFER, sizeof(CommonBlock), nullptr, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_UNIFORM_BUFFER, LEGACY_BINDING, legacycommonbuffername);

#ifdef _DEBUG
	err = glGetError();
	if (err != GL_NO_ERROR)
		Int3();
#endif

	glGenBuffers(1, &specularbuffername);
	glBindBuffer(GL_COPY_WRITE_BUFFER, specularbuffername);
	glBufferData(GL_COPY_WRITE_BUFFER, sizeof(SpecularBlock), nullptr, GL_STREAM_DRAW);
	glBindBufferBase(GL_UNIFORM_BUFFER, SPECULAR_BINDING, specularbuffername);

#ifdef _DEBUG
	err = glGetError();
	if (err != GL_NO_ERROR)
		Int3();
#endif

	glGenBuffers(1, &fogbuffername);
	glBindBuffer(GL_COPY_WRITE_BUFFER, fogbuffername);
	glBufferData(GL_COPY_WRITE_BUFFER, sizeof(RoomBlock) * 100, nullptr, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_UNIFORM_BUFFER, ROOM_BINDING, fogbuffername);

#ifdef _DEBUG
	err = glGetError();
	if (err != GL_NO_ERROR)
		Int3();
#endif

	glGenBuffers(1, &terrainfogbuffername);
	glBindBuffer(GL_COPY_WRITE_BUFFER, terrainfogbuffername);
	glBufferData(GL_COPY_WRITE_BUFFER, sizeof(TerrainFogBlock) * TERRAIN_FOG_COUNTER_MAX, nullptr, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_UNIFORM_BUFFER, TERRAIN_FOG_BINDING, terrainfogbuffername);

#ifdef _DEBUG
	err = glGetError();
	if (err != GL_NO_ERROR)
		Int3();
#endif
	terrainfogcounter = 0;

	//Init shader pipelines
	for (int i = 0; i < NUM_SHADERDEFS; i++)
	{
		gl_shaderprogs[i].AttachSourceFromDefiniton(gl_shaderdefs[i]);
	}
}

void GL4Renderer::WarmUpScenePipelines()
{
	const int pipeline_count = DRAW_SHADER_COUNT + (int)NUM_SHADERDEFS;
	if (pipeline_warmup_next >= pipeline_count)
		return;

	GLint old_draw_framebuffer = 0;
	GLint old_read_framebuffer = 0;
	GLint old_vertex_array = 0;
	GLint old_array_buffer = 0;
	GLint old_active_texture = 0;
	GLint old_viewport[4] = {};
	GLboolean blend_was_enabled = glIsEnabled(GL_BLEND);
	GLboolean cull_was_enabled = glIsEnabled(GL_CULL_FACE);
	GLboolean scissor_was_enabled = glIsEnabled(GL_SCISSOR_TEST);
	GLboolean depth_was_enabled = glIsEnabled(GL_DEPTH_TEST);
	GLboolean multisample_was_enabled = glIsEnabled(GL_MULTISAMPLE);
	GLboolean color_mask[4] = {};
	GLboolean depth_mask = GL_TRUE;
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &old_draw_framebuffer);
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &old_read_framebuffer);
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &old_vertex_array);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &old_array_buffer);
	glGetIntegerv(GL_ACTIVE_TEXTURE, &old_active_texture);
	glGetIntegerv(GL_VIEWPORT, old_viewport);
	glGetBooleanv(GL_COLOR_WRITEMASK, color_mask);
	glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);

	GLuint framebuffer = 0;
	GLuint color_textures[3] = {};
	GLuint object_id_texture = 0;
	GLuint depth_texture = 0;
	GLuint dummy_2d = 0;
	GLuint dummy_2d_array = 0;
	GLuint vertex_array = 0;
	GLuint vertex_buffer = 0;
	GLuint terrain_vertex_array = 0;
	GLuint terrain_vertex_buffer = 0;

	glGenFramebuffers(1, &framebuffer);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
	glGenTextures(3, color_textures);
	for (int attachment = 0; attachment < 3; attachment++)
	{
		glBindTexture(GL_TEXTURE_2D, color_textures[attachment]);
		glTexImage2D(GL_TEXTURE_2D, 0, attachment == 1 ? GL_RG16F : GL_RGBA8,
			4, 4, 0, attachment == 1 ? GL_RG : GL_RGBA,
			attachment == 1 ? GL_FLOAT : GL_UNSIGNED_BYTE, nullptr);
		glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + attachment,
			GL_TEXTURE_2D, color_textures[attachment], 0);
	}
	glGenTextures(1, &object_id_texture);
	glBindTexture(GL_TEXTURE_2D, object_id_texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, 4, 4, 0, GL_RED_INTEGER,
		GL_UNSIGNED_INT, nullptr);
	glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT4,
		GL_TEXTURE_2D, object_id_texture, 0);
	glGenTextures(1, &depth_texture);
	glBindTexture(GL_TEXTURE_2D, depth_texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, 4, 4, 0,
		GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
	glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
		GL_TEXTURE_2D, depth_texture, 0);
	const GLenum draw_buffers[5] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1,
		GL_COLOR_ATTACHMENT2, GL_NONE, GL_COLOR_ATTACHMENT4 };
	glDrawBuffers(5, draw_buffers);

	glGenTextures(1, &dummy_2d);
	glBindTexture(GL_TEXTURE_2D, dummy_2d);
	const unsigned int white_pixel = 0xffffffffu;
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA,
		GL_UNSIGNED_BYTE, &white_pixel);
	glGenTextures(1, &dummy_2d_array);
	glBindTexture(GL_TEXTURE_2D_ARRAY, dummy_2d_array);
	glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, 1, 1, 1, 0, GL_RGBA,
		GL_UNSIGNED_BYTE, &white_pixel);
	for (int unit = 0; unit <= 1; unit++)
	{
		glActiveTexture(GL_TEXTURE0 + unit);
		glBindTexture(GL_TEXTURE_2D, dummy_2d);
		glBindTexture(GL_TEXTURE_2D_ARRAY, dummy_2d_array);
	}

	GL4PipelineWarmupVertex vertices[3];
	GL4SetPipelineWarmupVertex(vertices[0], -1.0f, -1.0f);
	GL4SetPipelineWarmupVertex(vertices[1], 3.0f, -1.0f);
	GL4SetPipelineWarmupVertex(vertices[2], -1.0f, 3.0f);
	glGenVertexArrays(1, &vertex_array);
	glBindVertexArray(vertex_array);
	glGenBuffers(1, &vertex_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
	for (int attribute = 0; attribute < 15; attribute++)
	{
		glEnableVertexAttribArray(attribute);
		glVertexAttribPointer(attribute, 4, GL_FLOAT, GL_FALSE,
			sizeof(GL4PipelineWarmupVertex),
			(const void*)offsetof(GL4PipelineWarmupVertex, attributes[attribute]));
	}
	glEnableVertexAttribArray(15);
	glVertexAttribIPointer(15, 1, GL_INT, sizeof(GL4PipelineWarmupVertex),
		(const void*)offsetof(GL4PipelineWarmupVertex, retained_source_vertex));

	const float identity[16] = {
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1
	};
	CommonBlock common = {};
	memcpy(common.projection, identity, sizeof(identity));
	memcpy(common.modelview, identity, sizeof(identity));
	glBindBuffer(GL_COPY_WRITE_BUFFER, commonbuffername);
	glBufferSubData(GL_COPY_WRITE_BUFFER, 0, sizeof(common), &common);
	glBindBuffer(GL_COPY_WRITE_BUFFER, legacycommonbuffername);
	glBufferSubData(GL_COPY_WRITE_BUFFER, 0, sizeof(common), &common);
	SpecularBlock specular = {};
	specular.exponent = 4;
	specular.strength = 1.0f;
	glBindBuffer(GL_COPY_WRITE_BUFFER, specularbuffername);
	glBufferSubData(GL_COPY_WRITE_BUFFER, 0, sizeof(specular), &specular);
	RoomBlock room = {};
	room.brightness = 1.0f;
	room.fog_distance = 1.0f;
	room.fog_plane[2] = 1.0f;
	glBindBuffer(GL_COPY_WRITE_BUFFER, fogbuffername);
	glBufferSubData(GL_COPY_WRITE_BUFFER, 0, sizeof(room), &room);
	TerrainFogBlock terrain_fog = {};
	terrain_fog.end_dist = 1.0f;
	glBindBuffer(GL_COPY_WRITE_BUFFER, terrainfogbuffername);
	glBufferSubData(GL_COPY_WRITE_BUFFER, 0, sizeof(terrain_fog), &terrain_fog);
	glViewport(0, 0, 4, 4);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DEPTH_TEST);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask(GL_FALSE);

	if (pipeline_warmup_next < DRAW_SHADER_COUNT)
	{
		drawshaders[pipeline_warmup_next].Use();
		glDrawArrays(GL_TRIANGLES, 0, 3);
	}
	else
	{
		const int shader = pipeline_warmup_next - DRAW_SHADER_COUNT;
		if (shader == 7 || shader == 8)
		{
			// Retained terrain expands one packed cell into six vertices using
			// gl_VertexID, so exercise it with its actual input shape.
			struct TerrainWarmupCell
			{
				unsigned int packed[4];
				float height[4];
			};
			const TerrainWarmupCell terrain_cell = {
				{ 0, 0, 0, 0 },
				{ -1.0f, -1.0f, -1.0f, -1.0f }
			};
			glGenVertexArrays(1, &terrain_vertex_array);
			glBindVertexArray(terrain_vertex_array);
			glGenBuffers(1, &terrain_vertex_buffer);
			glBindBuffer(GL_ARRAY_BUFFER, terrain_vertex_buffer);
			glBufferData(GL_ARRAY_BUFFER, sizeof(terrain_cell), &terrain_cell, GL_STATIC_DRAW);
			glEnableVertexAttribArray(0);
			glVertexAttribIPointer(0, 4, GL_UNSIGNED_INT, sizeof(TerrainWarmupCell), nullptr);
			glEnableVertexAttribArray(1);
			glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(TerrainWarmupCell),
				(const void*)offsetof(TerrainWarmupCell, height));
			gl_shaderprogs[shader].Use();
			glDrawArraysInstanced(GL_TRIANGLES, 0, 6, 1);
		}
		else
		{
			gl_shaderprogs[shader].Use();
			glDrawArrays(GL_TRIANGLES, 0, 3);
		}
	}
	pipeline_warmup_next++;

	glDeleteBuffers(1, &terrain_vertex_buffer);
	glDeleteVertexArrays(1, &terrain_vertex_array);
	glDeleteBuffers(1, &vertex_buffer);
	glDeleteVertexArrays(1, &vertex_array);
	glDeleteTextures(1, &dummy_2d_array);
	glDeleteTextures(1, &dummy_2d);
	glDeleteTextures(1, &depth_texture);
	glDeleteTextures(1, &object_id_texture);
	glDeleteTextures(3, color_textures);
	glDeleteFramebuffers(1, &framebuffer);

	ShaderProgram::ClearBinding();
	lastdrawshader = -1;
	legacy_draw_uniforms_dirty = true;
	Last_texel_unit_set = -1;
	for (int unit = 0; unit < 4; unit++)
		OpenGL_last_bound[unit] = 9999999;
	glBindVertexArray(old_vertex_array);
	glBindBuffer(GL_ARRAY_BUFFER, old_array_buffer);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, old_draw_framebuffer);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, old_read_framebuffer);
	glViewport(old_viewport[0], old_viewport[1], old_viewport[2], old_viewport[3]);
	glActiveTexture(old_active_texture);
	if (blend_was_enabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
	if (cull_was_enabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
	if (scissor_was_enabled) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
	if (depth_was_enabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
	if (multisample_was_enabled) glEnable(GL_MULTISAMPLE); else glDisable(GL_MULTISAMPLE);
	glColorMask(color_mask[0], color_mask[1], color_mask[2], color_mask[3]);
	glDepthMask(depth_mask);
	SetViewport();
	if (pipeline_warmup_next == pipeline_count)
	{
		mprintf((0, "GL4 scene pipelines warmed up incrementally.\n"));
		AutomatedCaptureLog("GL4 scene pipelines warmed up incrementally count=%d",
			pipeline_count);
	}
}

uint32_t GL4Renderer::GetPipelineByName(const char* name)
{
	for (uint32_t i = 0; i < NUM_SHADERDEFS; i++)
	{
		if (!stricmp(gl_shaderdefs[i].name, name))
			return i;
	}
	return 0xFFFFFFFFu;
}

void GL4Renderer::BindPipeline(uint32_t handle)
{
	if (handle < NUM_SHADERDEFS)
	{
		if (strstr(gl_shaderdefs[handle].name, "fog") != nullptr)
			post_protection_mask_dirty = true;
		gl_shaderprogs[handle].Use();
		GLint ao_class_uniform = gl_shaderprogs[handle].FindUniform("ao_class_value");
		if (ao_class_uniform != -1)
			glUniform1i(ao_class_uniform, ao_class_draw_value);
		GLint ao_weight_uniform = gl_shaderprogs[handle].FindUniform("ao_weight_value");
		if (ao_weight_uniform != -1)
			glUniform1f(ao_weight_uniform, ao_weight_draw_value);
		GLint ao_capture_weight_mode_uniform = gl_shaderprogs[handle].FindUniform("ao_capture_weight_mode");
		if (ao_capture_weight_mode_uniform != -1)
			glUniform1i(ao_capture_weight_mode_uniform, 0);
		GLint motion_vector_mode_uniform = gl_shaderprogs[handle].FindUniform("motion_vector_mode");
		if (motion_vector_mode_uniform != -1)
			glUniform1i(motion_vector_mode_uniform,
				CurrentDrawWritesPixelMotionVectors() ? RENDERER_MOTION_VECTOR_PIXEL : RENDERER_MOTION_VECTOR_OFF);
		GLint motion_vector_current_view_projection_uniform =
			gl_shaderprogs[handle].FindUniform("motion_vector_current_view_projection");
		if (motion_vector_current_view_projection_uniform != -1)
			glUniformMatrix4fv(motion_vector_current_view_projection_uniform, 1, GL_FALSE, current_view_projection);
		GLint motion_vector_previous_view_projection_uniform =
			gl_shaderprogs[handle].FindUniform("motion_vector_previous_view_projection");
		if (motion_vector_previous_view_projection_uniform != -1)
			glUniformMatrix4fv(motion_vector_previous_view_projection_uniform, 1, GL_FALSE, previous_view_projection);
		GLint motion_vector_has_previous_uniform = gl_shaderprogs[handle].FindUniform("motion_vector_has_previous");
		if (motion_vector_has_previous_uniform != -1)
			glUniform1i(motion_vector_has_previous_uniform, have_previous_view_projection ? 1 : 0);
		GLint motion_vector_payload_type_uniform = gl_shaderprogs[handle].FindUniform("motion_vector_payload_type");
		if (motion_vector_payload_type_uniform != -1)
			glUniform1i(motion_vector_payload_type_uniform, 0);
		GLint motion_vector_object_id_uniform = gl_shaderprogs[handle].FindUniform("motion_vector_object_id");
		if (motion_vector_object_id_uniform != -1)
			glUniform1ui(motion_vector_object_id_uniform,
				CurrentDrawWritesMotionObjectId() ? motion_object_id : 0u);
	}
}

void GL4Renderer::UpdateCommon(float* projection, float* modelview, int depth)
{
	CommonBlock newblock;
	memcpy(newblock.projection, projection, sizeof(newblock.projection));
	memcpy(newblock.modelview, modelview, sizeof(newblock.modelview));

	//Cache the main-scene projection for AO. depth==0 is the regular world
	//pass; instance/portal draws use deeper slots which we don't care about
	//for ambient occlusion (it runs against the main framebuffer only).
	if (depth == 0)
	{
		memcpy(last_projection, projection, sizeof(last_projection));
		memcpy(current_projection, projection, sizeof(current_projection));
		have_current_projection = true;
		g3_Mat4Multiply(current_view_projection, projection, modelview);
		have_current_view_projection = true;
		have_current_inverse_view_projection = InvertMatrix4(current_view_projection, current_inverse_view_projection);
		have_current_inverse_modelview = InvertMatrix4(modelview, current_inverse_modelview);
	}

	glBindBuffer(GL_COPY_WRITE_BUFFER, commonbuffername);
	glBufferSubData(GL_COPY_WRITE_BUFFER, sizeof(CommonBlock) * depth, sizeof(CommonBlock), &newblock);

#ifdef _DEBUG
	GLenum err = glGetError();
	if (err != GL_NO_ERROR)
		Int3();
#endif

	SetCommonDepth(depth);
}

void GL4Renderer::SetCommonDepth(int depth)
{
	glBindBufferRange(GL_UNIFORM_BUFFER, COMMON_BINDING, commonbuffername, depth * sizeof(CommonBlock), sizeof(CommonBlock));
}

void GL4Renderer::UpdateSpecular(SpecularBlock* specularstate)
{
	glBindBuffer(GL_COPY_WRITE_BUFFER, specularbuffername);
	glBufferSubData(GL_COPY_WRITE_BUFFER, 0, sizeof(SpecularBlock), specularstate);

#ifdef _DEBUG
	GLenum err = glGetError();
	if (err != GL_NO_ERROR)
		Int3();
#endif
}

void GL4Renderer::UpdateFogBrightness(RoomBlock* roomstate, int numrooms)
{
	glBindBuffer(GL_COPY_WRITE_BUFFER, fogbuffername);
	glBufferSubData(GL_COPY_WRITE_BUFFER, 0, sizeof(RoomBlock) * numrooms, roomstate);

#ifdef _DEBUG
	GLenum err = glGetError();
	if (err != GL_NO_ERROR)
		Int3();
#endif
}

void GL4Renderer::SetCurrentRoomNum(int roomblocknum)
{
	glBindBufferRange(GL_UNIFORM_BUFFER, ROOM_BINDING, fogbuffername, roomblocknum * sizeof(RoomBlock), sizeof(RoomBlock));

#ifdef _DEBUG
	GLenum err = glGetError();
	if (err != GL_NO_ERROR)
		Int3();
#endif
}

void GL4Renderer::UpdateTerrainFog(float color[4], float start, float end)
{
	TerrainFogBlock block;
	memcpy(block.color, color, sizeof(float) * 3);
	block.start_dist = start;
	block.end_dist = end;

	glBindBuffer(GL_COPY_WRITE_BUFFER, terrainfogbuffername);
	terrainfogcounter++;
	if (terrainfogcounter == TERRAIN_FOG_COUNTER_MAX)
	{
		glBufferData(GL_COPY_WRITE_BUFFER, sizeof(TerrainFogBlock) * TERRAIN_FOG_COUNTER_MAX, nullptr, GL_DYNAMIC_DRAW);
		terrainfogcounter = 0;
	}
	
	glBufferSubData(GL_COPY_WRITE_BUFFER, terrainfogcounter * sizeof(TerrainFogBlock), sizeof(TerrainFogBlock), &block);
	glBindBufferRange(GL_UNIFORM_BUFFER, TERRAIN_FOG_BINDING, terrainfogbuffername, terrainfogcounter * sizeof(TerrainFogBlock), sizeof(TerrainFogBlock));
#ifdef _DEBUG
	GLenum err = glGetError();
	if (err != GL_NO_ERROR)
		Int3();
#endif
}

void GL4Renderer::UpdateLegacyBlock(float* projection, float* modelview)
{
	CommonBlock newblock;
	memcpy(newblock.projection, projection, sizeof(newblock.projection));
	memcpy(newblock.modelview, modelview, sizeof(newblock.modelview));

	glBindBuffer(GL_COPY_WRITE_BUFFER, legacycommonbuffername);
	glBufferSubData(GL_COPY_WRITE_BUFFER, 0, sizeof(CommonBlock), &newblock);

#ifdef _DEBUG
	GLenum err = glGetError();
	if (err != GL_NO_ERROR)
		Int3();
#endif
}

//ATM this is redundant, but it will support a preprocessor for #include if needed later. 
static GLuint CompileShaderFromFile(GLenum type, const char* filename)
{
	std::string str;

	CFILE* fp = cfopen(filename, "rb");
	if (!fp)
		Error("CompileShaderFromFile: Couldn't open source file %s!", filename);

	str.resize(cfilelength(fp));
	cf_ReadBytes((ubyte*)str.data(), str.size(), fp);
	cfclose(fp);

	GLuint name = glCreateShader(type);
	const char* strptr = str.c_str();
	glShaderSource(name, 1, &strptr, nullptr);
	glCompileShader(name);
	GLint status;
	glGetShaderiv(name, GL_COMPILE_STATUS, &status);
	if (status == GL_FALSE)
	{
		GLint length;
		glGetShaderiv(name, GL_INFO_LOG_LENGTH, &length);
		char* buf = new char[length];
		glGetShaderInfoLog(name, length, &length, buf);

		mprintf((1, "%s\n", buf));
		Error("CompileShaderFromFile: Failed to compile shader %s!\n%s", filename, buf);
	}

	return name;
}

static GLuint CompileShader(GLenum type, int numstrs, const char** src, GLint* lengths)
{
	GLuint name = glCreateShader(type);
	glShaderSource(name, numstrs, src, lengths);
	glCompileShader(name);
	GLint status;
	glGetShaderiv(name, GL_COMPILE_STATUS, &status);
	if (status == GL_FALSE)
	{
		GLint length;
		glGetShaderiv(name, GL_INFO_LOG_LENGTH, &length);
		char* buf = new char[length];
		glGetShaderInfoLog(name, length, &length, buf);

		mprintf((1, "%s\n", buf));
		Error("CompileShader: Failed to compile shader! This error message needs more context..\n%s", buf);
	}

	return name;
}

void ShaderProgram::CreateCommonBindings(int bindindex)
{
	Use();

	//Find colortexture
	GLint index = glGetUniformLocation(m_name, "colortexture");
	if (index != -1)
		glUniform1i(index, 0); //Set to GL_TEXTURE0

	//Find lightmaptexture
	index = glGetUniformLocation(m_name, "lightmaptexture");
	if (index != -1)
		glUniform1i(index, 1); //Set to GL_TEXTURE1

	index = glGetUniformLocation(m_name, "soft_particle_depth");
	if (index != -1)
		glUniform1i(index, 2); //Set to GL_TEXTURE2

	index = glGetUniformLocation(m_name, "room_fog_entry_depth");
	if (index != -1)
		glUniform1i(index, 10); //Retained room lightmap arrays occupy 3..9.

	const char* room_lightmap_arrays[7] = {
		"retained_room_lightmaps_2", "retained_room_lightmaps_4",
		"retained_room_lightmaps_8", "retained_room_lightmaps_16",
		"retained_room_lightmaps_32", "retained_room_lightmaps_64",
		"retained_room_lightmaps_128" };
	for (int array_index = 0; array_index < 7; array_index++)
	{
		index = glGetUniformLocation(m_name, room_lightmap_arrays[array_index]);
		if (index != -1)
			glUniform1i(index, 3 + array_index);
	}

	//Find CommonBlock
	GLuint uboindex = glGetUniformBlockIndex(m_name, "CommonBlock");
	if (uboindex != GL_INVALID_INDEX)
	{
		//Bind to GL_UNIFORM_BUFFER bindindex. This is so that "legacy" shaders can have the passthrough matricies. 
		glUniformBlockBinding(m_name, uboindex, bindindex);
	}

	//Find SpecularBlock
	uboindex = glGetUniformBlockIndex(m_name, "SpecularBlock");
	if (uboindex != GL_INVALID_INDEX)
	{
		glUniformBlockBinding(m_name, uboindex, SPECULAR_BINDING);
	}
	
	//Find RoomBlock
	uboindex = glGetUniformBlockIndex(m_name, "RoomBlock");
	if (uboindex != GL_INVALID_INDEX)
	{
		glUniformBlockBinding(m_name, uboindex, ROOM_BINDING);
	}

	//Find RoomBlock
	uboindex = glGetUniformBlockIndex(m_name, "TerrainFogBlock");
	if (uboindex != GL_INVALID_INDEX)
	{
		glUniformBlockBinding(m_name, uboindex, TERRAIN_FOG_BINDING);
	}

	m_dynamic_light_count = glGetUniformLocation(m_name, "dynamic_light_count");
	m_dynamic_face_normal = glGetUniformLocation(m_name, "dynamic_face_normal");
	m_dynamic_light_positions = glGetUniformLocation(m_name, "dynamic_light_positions[0]");
	m_dynamic_light_colors = glGetUniformLocation(m_name, "dynamic_light_colors[0]");
	m_dynamic_light_radii = glGetUniformLocation(m_name, "dynamic_light_radii[0]");
	m_dynamic_light_specular_positions = glGetUniformLocation(m_name, "dynamic_light_specular_positions[0]");
	m_dynamic_light_specular_radii = glGetUniformLocation(m_name, "dynamic_light_specular_radii[0]");
	m_dynamic_light_specular_scalars = glGetUniformLocation(m_name, "dynamic_light_specular_scalars[0]");
	m_dynamic_light_falloffs = glGetUniformLocation(m_name, "dynamic_light_falloffs[0]");
	m_dynamic_light_directions = glGetUniformLocation(m_name, "dynamic_light_directions[0]");
	m_dynamic_light_dot_ranges = glGetUniformLocation(m_name, "dynamic_light_dot_ranges[0]");
	m_dynamic_light_directional = glGetUniformLocation(m_name, "dynamic_light_directional[0]");
	m_last_dynamic_light_count = -1;
	m_last_dynamic_lighting_valid = false;

	ClearBinding();
}

void ShaderProgram::AttachSource(const char* vertexsource, const char* fragsource)
{
	GLint vertexsourcelen = strlen(vertexsource);
	GLint fragsourcelen = strlen(fragsource);
	GLuint vertexprog = CompileShader(GL_VERTEX_SHADER, 1, &vertexsource, &vertexsourcelen);
	GLuint fragmentprog = CompileShader(GL_FRAGMENT_SHADER, 1, &fragsource, &fragsourcelen);

	m_name = glCreateProgram();
	glAttachShader(m_name, vertexprog);
	glAttachShader(m_name, fragmentprog);
	glLinkProgram(m_name);
	GLint status;
	glGetProgramiv(m_name, GL_LINK_STATUS, &status);
	if (status == GL_FALSE)
	{
		GLint length;
		glGetProgramiv(m_name, GL_INFO_LOG_LENGTH, &length);
		char* buf = new char[length];
		glGetProgramInfoLog(m_name, length, &length, buf);

		Error("ShaderProgram::AttachSource: Failed to link program! This error message needs more context..\n%s", buf);
	}

	glDeleteShader(vertexprog);
	glDeleteShader(fragmentprog);

	CreateCommonBindings(COMMON_BINDING);
}

void ShaderProgram::AttachSourceFromDefiniton(ShaderDefinition& def)
{
	GLuint vertexprog = CompileShaderFromFile(GL_VERTEX_SHADER, def.vertex_filename);
	GLuint fragmentprog = CompileShaderFromFile(GL_FRAGMENT_SHADER, def.fragment_filename);

	m_name = glCreateProgram();
	glAttachShader(m_name, vertexprog);
	glAttachShader(m_name, fragmentprog);
	glLinkProgram(m_name);
	GLint status;
	glGetProgramiv(m_name, GL_LINK_STATUS, &status);
	if (status == GL_FALSE)
	{
		GLint length;
		glGetProgramiv(m_name, GL_INFO_LOG_LENGTH, &length);
		char* buf = new char[length];
		glGetProgramInfoLog(m_name, length, &length, buf);

		Error("ShaderProgram::AttachSource: Failed to link program! This error message needs more context..\n%s", buf);
	}

	glDeleteShader(vertexprog);
	glDeleteShader(fragmentprog);

	CreateCommonBindings(COMMON_BINDING);
}

void ShaderProgram::AttachSourcePreprocess(const char* vertexsource, const char* fragsource, bool textured, bool lightmapped, bool speculared, bool fogged)
{
	GLenum err = glGetError();
	if (err != GL_NO_ERROR)
		Int3();

	const char* vertexstrs[3];
	GLint vertexlens[3];
	const char* fragstrs[3];
	GLint fraglens[3];

	vertexstrs[0] = fragstrs[0] = "#version 450 core\n";
	vertexlens[0] = fraglens[0] = strlen(vertexstrs[0]);

	std::string preprocessorstr;
	if (textured)
		preprocessorstr.append("#define USE_TEXTURING\n");
	if (lightmapped)
		preprocessorstr.append("#define USE_LIGHTMAP\n");
	if (speculared)
		preprocessorstr.append("#define USE_SPECULAR\n");
	if (fogged)
		preprocessorstr.append("#define USE_FOG\n");

	vertexstrs[1] = fragstrs[1] = preprocessorstr.c_str();
	vertexlens[1] = fraglens[1] = preprocessorstr.size();

	vertexstrs[2] = vertexsource; vertexlens[2] = strlen(vertexsource);
	fragstrs[2] = fragsource; fraglens[2] = strlen(fragsource);

	GLuint vertexprog = CompileShader(GL_VERTEX_SHADER, 3, vertexstrs, vertexlens);
	GLuint fragmentprog = CompileShader(GL_FRAGMENT_SHADER, 3, fragstrs, fraglens);

	m_name = glCreateProgram();
	glAttachShader(m_name, vertexprog);
	glAttachShader(m_name, fragmentprog);
	glLinkProgram(m_name);
	GLint status;
	glGetProgramiv(m_name, GL_LINK_STATUS, &status);
	if (status == GL_FALSE)
	{
		GLint length;
		glGetProgramiv(m_name, GL_INFO_LOG_LENGTH, &length);
		char* buf = new char[length];
		glGetProgramInfoLog(m_name, length, &length, buf);

		Error("ShaderProgram::AttachSource: Failed to link program! This error message needs more context..\n%s", buf);
	}

	glDeleteShader(vertexprog);
	glDeleteShader(fragmentprog);

	//Always use the legacy block with these preprocessed shaders, for now.
	CreateCommonBindings(LEGACY_BINDING);
}

GLint ShaderProgram::FindUniform(const char* uniform)
{
	return glGetUniformLocation(m_name, uniform);
}

void ShaderProgram::Destroy()
{
	ClearBinding();
	glDeleteProgram(m_name);
	m_name = 0;
	m_last_dynamic_light_count = -1;
	m_last_dynamic_lighting_valid = false;
}

void ShaderProgram::Use()
{
	if (lastshaderprog != this)
	{
		lastshaderprog = this;
		glUseProgram(m_name);
	}
}

void ShaderProgram::ApplyDynamicLighting(int count, const float* face_normal, const GLfloat* positions,
	const GLfloat* colors, const GLfloat* radii, const GLfloat* specular_positions,
	const GLfloat* specular_radii, const GLfloat* specular_scalars, const GLfloat* falloffs, const GLfloat* directions,
	const GLfloat* dot_ranges, const GLint* directional)
{
	if (m_dynamic_light_count == -1)
		return;

	if (count <= 0)
	{
		if (m_last_dynamic_light_count == 0)
			return;

		glUniform1i(m_dynamic_light_count, 0);
		m_last_dynamic_light_count = 0;
		return;
	}
	DynamicLightingState state = {};
	const bool cacheable = count <= DYNAMIC_LIGHT_CACHE_CAPACITY;
	if (cacheable)
	{
		memcpy(state.face_normal, face_normal, sizeof(state.face_normal));
		memcpy(state.positions, positions, count * sizeof(state.positions[0]));
		memcpy(state.colors, colors, count * sizeof(state.colors[0]));
		memcpy(state.radii, radii, count * sizeof(state.radii[0]));
		memcpy(state.specular_positions, specular_positions,
			count * sizeof(state.specular_positions[0]));
		memcpy(state.specular_radii, specular_radii, count * sizeof(state.specular_radii[0]));
		memcpy(state.specular_scalars, specular_scalars,
			count * sizeof(state.specular_scalars[0]));
		memcpy(state.falloffs, falloffs, count * sizeof(state.falloffs[0]));
		memcpy(state.directions, directions, count * sizeof(state.directions[0]));
		memcpy(state.dot_ranges, dot_ranges, count * sizeof(state.dot_ranges[0]));
		memcpy(state.directional, directional, count * sizeof(state.directional[0]));
		if (m_last_dynamic_light_count == count && m_last_dynamic_lighting_valid &&
			memcmp(&m_last_dynamic_lighting, &state, sizeof(state)) == 0)
		{
			return;
		}
	}

	glUniform1i(m_dynamic_light_count, count);
	if (m_dynamic_face_normal != -1)
		glUniform3fv(m_dynamic_face_normal, 1, face_normal);
	if (m_dynamic_light_positions != -1)
		glUniform3fv(m_dynamic_light_positions, count, positions);
	if (m_dynamic_light_colors != -1)
		glUniform3fv(m_dynamic_light_colors, count, colors);
	if (m_dynamic_light_radii != -1)
		glUniform1fv(m_dynamic_light_radii, count, radii);
	if (m_dynamic_light_specular_positions != -1)
		glUniform3fv(m_dynamic_light_specular_positions, count, specular_positions);
	if (m_dynamic_light_specular_radii != -1)
		glUniform1fv(m_dynamic_light_specular_radii, count, specular_radii);
	if (m_dynamic_light_specular_scalars != -1)
		glUniform1fv(m_dynamic_light_specular_scalars, count, specular_scalars);
	if (m_dynamic_light_falloffs != -1)
		glUniform1fv(m_dynamic_light_falloffs, count, falloffs);
	if (m_dynamic_light_directions != -1)
		glUniform3fv(m_dynamic_light_directions, count, directions);
	if (m_dynamic_light_dot_ranges != -1)
		glUniform1fv(m_dynamic_light_dot_ranges, count, dot_ranges);
	if (m_dynamic_light_directional != -1)
		glUniform1iv(m_dynamic_light_directional, count, directional);

	m_last_dynamic_light_count = count;
	if (cacheable)
	{
		m_last_dynamic_lighting = state;
		m_last_dynamic_lighting_valid = true;
	}
	else
	{
		m_last_dynamic_lighting_valid = false;
	}
}

void ShaderProgram::ClearBinding()
{
	lastshaderprog = nullptr;
	glUseProgram(0);
}

ShaderProgram* ShaderProgram::Current()
{
	return lastshaderprog;
}
