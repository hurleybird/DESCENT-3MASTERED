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
#include "gl_local.h"
#include "game.h"
#include "gameloop.h"
#include "config.h"
#include "HardwareInternal.h"

#include <algorithm>
#include <cstddef>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <vector>

//The number of vertex attributes the legacy code used.
constexpr int NUM_LEGACY_VERTEX_ATTRIBS = 15;
//The vertex format carries several optional payloads; size the draw buffer by bytes so new payloads do not
//quietly turn the persistent mapping into an oversized allocation.
constexpr size_t DRAW_BUFFER_TARGET_BYTES = 64u * 1024u * 1024u;
constexpr int MIN_VERTS_PER_BUFFER = 65536;
constexpr float PIXEL_MOTION_BLUR_REFERENCE_FRAME_TIME = 1.0f / 60.0f;
constexpr unsigned int GL4_MOTION_OBJECT_LEGACY_BLUR_MASK = 0x80000000u;
constexpr GLuint GL4_ROOM_FOG_PORTAL_BINDING = 5;
constexpr GLuint GL4_PER_PIXEL_LIGHTMAP_BINDING = 6;

static int GL4DrawBufferVertexCapacity()
{
	size_t capacity = std::max((size_t)MIN_VERTS_PER_BUFFER,
		DRAW_BUFFER_TARGET_BYTES / sizeof(gl_vertex));
	capacity -= capacity % GL4_DRAW_BUFFER_SYNC_POINT_COUNT;
	return (int)capacity;
}

static size_t GL4DrawBufferByteSize()
{
	return (size_t)GL4DrawBufferVertexCapacity() * sizeof(gl_vertex);
}

static float GL4DepthFromEyeZ(float z)
{
	const float clamped_z = std::max(z, 0.0001f);
	return std::max(0.f, std::min(1.0f, 1.0f - (1.0f / clamped_z)));
}

static float GL4SoftParticleDepthFromPoint(const g3Point* pnt)
{
	return GL4DepthFromEyeZ(pnt->p3_z + Z_bias);
}

static vector GL4ViewSpacePositionFromPoint(const g3Point* pnt)
{
	vector view_position = pnt->p3_vecPreRot - View_position;
	return view_position * Unscaled_matrix;
}

static vector GL4ViewSpaceNormal(vector normal)
{
	normal = normal * Unscaled_matrix;
	if (vm_NormalizeVectorFast(&normal) <= 0.0f)
		normal = { 0, 0, 1 };
	return normal;
}

static vector GL4ViewSpaceSpecularNormal(const g3Point* pnt, vector fallback_normal)
{
	if (!pnt->p3_specular_normal_valid)
		return GL4ViewSpaceNormal(fallback_normal);

	vector normal = pnt->p3_specular_normal * Unscaled_matrix;
	if (vm_NormalizeVectorFast(&normal) <= 0.0f)
		return { 0, 0, 0 };
	return normal;
}

static vector GL4ViewSpacePosition(vector position)
{
	position -= View_position;
	return position * Unscaled_matrix;
}

static void GL4SetFieldSpecularVertexPayload(gl_vertex& vert, const g3Point* pnt, bool per_pixel_specular_draw)
{
	for (int i = 0; i < MAX_SPECULARS; i++)
	{
		vert.field_specular_center[i] = {};
		vert.field_specular_color[i] = {};
	}

	if (!per_pixel_specular_draw || !pnt->p3_specular_field_valid)
		return;

	const float payloadw = 1.0f / (pnt->p3_z + Z_bias);
	const int count = std::min((int)pnt->p3_specular_field_count, MAX_SPECULARS);
	for (int i = 0; i < count; i++)
	{
		vector view_center = GL4ViewSpacePosition(pnt->p3_specular_field_centers[i]);
		vert.field_specular_center[i].x = view_center.x * payloadw;
		vert.field_specular_center[i].y = view_center.y * payloadw;
		vert.field_specular_center[i].z = view_center.z * payloadw;
		vert.field_specular_center[i].w = payloadw;
		vert.field_specular_color[i].x = pnt->p3_specular_field_colors[i].x;
		vert.field_specular_color[i].y = pnt->p3_specular_field_colors[i].y;
		vert.field_specular_color[i].z = pnt->p3_specular_field_colors[i].z;
		vert.field_specular_color[i].w = 1.0f;
	}
}

static bool GL4DrawTargetIsFramebuffer(GLuint framebuffer)
{
	GLint current_draw = 0;
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &current_draw);
	return (GLuint)current_draw == framebuffer;
}

static void GL4UseSceneDrawBuffersWithoutAOClass(bool include_motion_vectors, bool include_motion_object_ids)
{
	const GLenum draw_buffers[5] =
	{
		GL_COLOR_ATTACHMENT0,
		static_cast<GLenum>(include_motion_vectors ? GL_COLOR_ATTACHMENT1 : GL_NONE),
		GL_COLOR_ATTACHMENT2,
		GL_NONE,
		static_cast<GLenum>(include_motion_object_ids ? GL_COLOR_ATTACHMENT4 : GL_NONE)
	};
	glDrawBuffers(include_motion_object_ids ? 5 : 4, draw_buffers);
	GL_ConfigurePostMaskBlend();
	glColorMaski(2, GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
}

static void GL4UseSceneDrawBuffersForCurrentDraw(bool include_motion_vectors, bool include_post_mask,
	bool include_ao_class, bool include_motion_object_ids)
{
	const GLenum draw_buffers[5] =
	{
		GL_COLOR_ATTACHMENT0,
		static_cast<GLenum>(include_motion_vectors ? GL_COLOR_ATTACHMENT1 : GL_NONE),
		static_cast<GLenum>(include_post_mask ? GL_COLOR_ATTACHMENT2 : GL_NONE),
		GL_NONE,
		static_cast<GLenum>(include_motion_object_ids ? GL_COLOR_ATTACHMENT4 : GL_NONE)
	};
	glDrawBuffers(include_motion_object_ids ? 5 : 4, draw_buffers);
	GL_ConfigurePostMaskBlend();
	glColorMaski(2, GL_TRUE, GL_TRUE, GL_TRUE, include_ao_class ? GL_TRUE : GL_FALSE);
}

static void GL4UseSceneColorDrawBuffer()
{
	const GLenum draw_buffer = GL_COLOR_ATTACHMENT0;
	glDrawBuffers(1, &draw_buffer);
	glReadBuffer(GL_COLOR_ATTACHMENT0);
}

bool GL4Renderer::UsesExactRoomFogMultiply() const
{
	return room_fog_enabled && !room_fog_overlay &&
		(OpenGL_state.cur_alpha_type == AT_LIGHTMAP_BLEND ||
		 OpenGL_state.cur_alpha_type == AT_LIGHTMAP_BLEND_VERTEX);
}

bool GL4Renderer::CurrentDrawNeedsPostMask(bool include_ao_class) const
{
	if (include_ao_class || ao_suppression_draw_value > 0.0f ||
		bloom_suppression_draw_value > 0.0f || room_fog_overlay)
	{
		return true;
	}

	if (!room_fog_enabled)
		return false;

	// Additive light and destination-multiply materials are attenuated toward
	// their neutral contribution by room fog; they do not cover the opaque
	// surface consumed by deferred AO. Their post-mask output is therefore zero.
	switch (OpenGL_state.cur_alpha_type)
	{
	case AT_SPECULAR:
	case AT_SATURATE_TEXTURE:
	case AT_SATURATE_VERTEX:
	case AT_SATURATE_CONSTANT_VERTEX:
	case AT_SATURATE_TEXTURE_VERTEX:
	case AT_LIGHTMAP_BLEND_SATURATE:
	case AT_LIGHTMAP_BLEND:
	case AT_LIGHTMAP_BLEND_VERTEX:
		return false;
	default:
		return true;
	}
}

void GL4Renderer::SetCurrentFogCompositeMode(int mode)
{
	if (lastdrawshader >= 0 &&
		drawshader_fog_composite_mode_uniforms[lastdrawshader] != -1)
	{
		glUniform1i(drawshader_fog_composite_mode_uniforms[lastdrawshader], mode);
	}
}

void GL4Renderer::DrawRoomFogMultiplyCorrection(GLenum primitive, GLint first,
	GLsizei count)
{
	// The fogged destination is D = B*T + F*(1-T). A destination-multiplying
	// material M belongs on the surface, so the exact result is
	// D*M + F*(1-T)*(1-M). The first draw supplies D*M; this color-only pass
	// restores the fog term without another depth copy or resolve.
	GL4UseSceneColorDrawBuffer();
	SetCurrentFogCompositeMode(5);
	glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
	rend_RecordDrawCall(draw_call_category);
	glDrawArrays(primitive, first, count);
	glBlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	SetCurrentFogCompositeMode(4);
}

static void GL4BuildOrtho(float* mat, float left, float right, float bottom, float top, float znear, float zfar)
{
	memset(mat, 0, sizeof(float[16]));
	mat[0] = 2 / (right - left);
	mat[5] = 2 / (top - bottom);
	mat[10] = -2 / (zfar - znear);
	mat[12] = -((right + left) / (right - left));
	mat[13] = -((top + bottom) / (top - bottom));
	mat[14] = -((zfar + znear) / (zfar - znear));
	mat[15] = 1;
}

static void GL4TransformWorldToClip(const float* mat, const vector& world_pos, vec4_array& clip)
{
	const float x = world_pos.x;
	const float y = world_pos.y;
	const float z = world_pos.z;
	clip.x = mat[0] * x + mat[4] * y + mat[8] * z + mat[12];
	clip.y = mat[1] * x + mat[5] * y + mat[9] * z + mat[13];
	clip.z = mat[2] * x + mat[6] * y + mat[10] * z + mat[14];
	clip.w = mat[3] * x + mat[7] * y + mat[11] * z + mat[15];
}

static float GL4FontBatchIdentity[16] =
{
	1, 0, 0, 0,
	0, 1, 0, 0,
	0, 0, 1, 0,
	0, 0, 0, 1
};

static char* GL4ReadShaderFile(const char* filename, const char* context)
{
	CFILE* cf = cfopen(filename, "rb");
	if (!cf)
		Error("%s: Couldn't open shader source file %s!", context, filename);
	int len = cfilelength(cf);
	char* body = new char[len + 1];
	if (cf_ReadBytes((ubyte*)body, len, cf) != len)
		Error("%s: Failure reading %s!", context, filename);
	body[len] = '\0';
	cfclose(cf);
	return body;
}

static int GL4FontBatchIndexForAlpha(int alpha_type)
{
	return alpha_type == AT_SATURATE_TEXTURE ? 1 : 0;
}

void GL4Renderer::UseDrawVAO()
{
	glBindVertexArray(drawvao);
}

void GL4Renderer::RestoreLegacy()
{
	glBindVertexArray(drawvao);
}

bool GL4Renderer::PixelMotionVectorConsumerActive() const
{
	const bool ao_temporal_vectors =
		OpenGL_preferred_state.ao_enabled &&
		(OpenGL_preferred_state.ao_temporal_blend > 0.0f ||
			OpenGL_preferred_state.ao_temporal_debug_preview);
	const bool new_motion_blur_vectors =
		OpenGL_preferred_state.combined_motion_blur &&
		(OpenGL_preferred_state.pixel_motion_blur_strength > 0.0f ||
			OpenGL_preferred_state.pixel_motion_blur_legacy_object_strength > 0.0f);
	return ao_temporal_vectors || new_motion_blur_vectors;
}

bool GL4Renderer::MotionVectorTargetEnabled() const
{
	return PixelMotionVectorConsumerActive() && motion_vectors.velocity_texture != 0;
}

bool GL4Renderer::PixelMotionVectorModeEnabled() const
{
	return MotionVectorTargetEnabled();
}

bool GL4Renderer::MotionVectorsFrozen() const
{
	return Game_paused;
}

bool GL4Renderer::MotionVectorWritesEnabled() const
{
	return PixelMotionVectorModeEnabled() && !motion_vectors_capture_locked && !MotionVectorsFrozen();
}

bool GL4Renderer::CurrentDrawWritesPixelMotionVectors() const
{
	if (motion_vector_write_suppression_depth > 0)
		return false;

	if (!PixelMotionVectorModeEnabled() || MotionVectorsFrozen() ||
		post_present_pending_swap || OpenGL_state.cur_zbuffer_state == 0)
		return false;

	if (!motion_object_active)
		return false;

	const bool cockpit_draw = rend_Get3DDrawCallCategory() == RENDERER_DRAW_CALL_3D_COCKPIT;
	if (motion_vectors_capture_locked && !cockpit_draw)
		return false;

	const bool opaque_polyobject_or_cockpit_vertex_alpha =
		(ao_class_draw_value == RENDERER_AO_CLASS_POLYOBJECT || cockpit_draw) &&
		OpenGL_state.cur_alpha == 255;
	const bool object_attached_motion_alpha =
		motion_object_force_capture ||
		(ao_class_draw_value == RENDERER_AO_CLASS_POLYOBJECT && motion_object_active);

	switch (OpenGL_state.cur_alpha_type)
	{
	case AT_ALWAYS:
	case AT_TEXTURE:
		return true;
	case AT_CONSTANT_TEXTURE:
	case AT_CONSTANT:
		return OpenGL_state.cur_alpha == 255 || object_attached_motion_alpha;
	case AT_TEXTURE_VERTEX:
		return opaque_polyobject_or_cockpit_vertex_alpha || object_attached_motion_alpha;
	case AT_CONSTANT_VERTEX:
		return (opaque_polyobject_or_cockpit_vertex_alpha && OpenGL_state.cur_texture_type == TT_FLAT) ||
			object_attached_motion_alpha;
	case AT_VERTEX:
	case AT_SATURATE_TEXTURE:
	case AT_SATURATE_VERTEX:
	case AT_SATURATE_CONSTANT_VERTEX:
	case AT_SATURATE_TEXTURE_VERTEX:
		return object_attached_motion_alpha;
	default:
		return false;
	}
}

bool GL4Renderer::CurrentDrawWritesMotionObjectId() const
{
	return CurrentDrawWritesPixelMotionVectors() && motion_object_id != 0 &&
		!cockpit_motion_object_active;
}

bool GL4Renderer::CurrentDrawIsLateCockpitPixelMotionVectorDraw() const
{
	return PixelMotionVectorModeEnabled() &&
		OpenGL_state.cur_zbuffer_state != 0 &&
		motion_vectors_capture_locked &&
		rend_Get3DDrawCallCategory() == RENDERER_DRAW_CALL_3D_COCKPIT;
}

bool GL4Renderer::CurrentDrawUsesPixelMotionTarget() const
{
	return CurrentDrawWritesPixelMotionVectors();
}

void GL4Renderer::ApplyPixelMotionBlur(int supersampling_factor)
{
	float scene_strength = std::max(0.0f,
		std::min(OpenGL_preferred_state.pixel_motion_blur_strength, 4.0f));
	float legacy_object_strength = std::max(0.0f,
		std::min(OpenGL_preferred_state.pixel_motion_blur_legacy_object_strength, 4.0f));
	const float afterburner_factor = std::max(0.0f, std::min(Render_afterburner_visual_factor, 1.0f));
	const float afterburner_blur_multiplier = std::max(0.0f,
		std::min(OpenGL_preferred_state.afterburner_pixel_blur_multiplier, 4.0f));
	const float afterburner_blur_scale = 1.0f + (afterburner_factor * afterburner_blur_multiplier);
	scene_strength *= afterburner_blur_scale;
	legacy_object_strength *= afterburner_blur_scale;
	if ((scene_strength <= 0.0f && legacy_object_strength <= 0.0f) ||
		motionblurshader.Handle() == 0 || post_present_framebuffer.Handle() == 0 ||
		!MotionVectorTargetEnabled())
	{
		return;
	}

	if (motion_vectors.width == 0 || motion_vectors.height == 0)
		return;
	GLuint depth_texture = bloom_source_valid && bloom_source_framebuffer.Handle() != 0 ?
		bloom_source_framebuffer.DepthTextureForRead() :
		framebuffers[framebuffer_current_draw].DepthTextureForRead();
	const bool has_dynamic_velocity = motion_vectors_dirty;
	if (scene_strength <= 0.0f && !has_dynamic_velocity)
		return;
	GLuint velocity_texture = has_dynamic_velocity ?
		motion_vectors.TextureForRead(framebuffers[framebuffer_current_draw].Handle()) : 0;
	GLuint object_id_texture = has_dynamic_velocity ?
		motion_vectors.ObjectIdTextureForRead(framebuffers[framebuffer_current_draw].Handle()) : 0;
	if (has_dynamic_velocity && (velocity_texture == 0 || object_id_texture == 0))
		return;
	if (supersampling_factor < 1)
		supersampling_factor = 1;
	const int framebuffer_logical_bottom_offset =
		framebuffer_logical_height - OpenGL_state.screen_height - framebuffer_logical_offset_y;
	const float source_width = (float)motion_vectors.width;
	const float source_height = (float)motion_vectors.height;
	const float uv_origin_x = (float)(framebuffer_logical_offset_x * supersampling_factor) / source_width;
	const float uv_origin_y = (float)(framebuffer_logical_bottom_offset * supersampling_factor) / source_height;
	const float uv_scale_x = (float)(OpenGL_state.screen_width * supersampling_factor) / source_width;
	const float uv_scale_y = (float)(OpenGL_state.screen_height * supersampling_factor) / source_height;

	motion_blur_framebuffer.Update(OpenGL_state.screen_width, OpenGL_state.screen_height, 0);
	if (motion_blur_framebuffer.Handle() == 0)
		return;

	motionblurshader.Use();
	if (motionblur_color_source != -1)
		glUniform1i(motionblur_color_source, 0);
	if (motionblur_velocity_source != -1)
		glUniform1i(motionblur_velocity_source, 1);
	if (motionblur_depth_source != -1)
		glUniform1i(motionblur_depth_source, 3);
	if (motionblur_object_id_source != -1)
		glUniform1i(motionblur_object_id_source, 4);
	if (motionblur_velocity_uv_origin != -1)
		glUniform2f(motionblur_velocity_uv_origin, uv_origin_x, uv_origin_y);
	if (motionblur_velocity_uv_scale != -1)
		glUniform2f(motionblur_velocity_uv_scale, uv_scale_x, uv_scale_y);
	float frame_time = Frametime;
	if (frame_time < 0.001f)
		frame_time = PIXEL_MOTION_BLUR_REFERENCE_FRAME_TIME;
	float frame_scale = PIXEL_MOTION_BLUR_REFERENCE_FRAME_TIME / frame_time;
	frame_scale = std::max(0.25f, std::min(frame_scale, 4.0f));
	if (motionblur_strength != -1)
		glUniform1f(motionblur_strength, scene_strength * frame_scale);
	if (motionblur_legacy_object_strength != -1)
		glUniform1f(motionblur_legacy_object_strength, legacy_object_strength * frame_scale);
	const float center_suppression = std::max(0.0f,
		std::min(OpenGL_preferred_state.pixel_motion_blur_center_suppression, 1.0f));
	const float legacy_center_suppression = std::max(0.0f,
		std::min(OpenGL_preferred_state.pixel_motion_blur_legacy_object_center_suppression, 1.0f));
	if (motionblur_center_suppression != -1)
		glUniform1f(motionblur_center_suppression,
			center_suppression);
	if (motionblur_legacy_object_center_suppression != -1)
		glUniform1f(motionblur_legacy_object_center_suppression,
			legacy_center_suppression);
	if (motionblur_sample_count != -1)
	{
		int samples = OpenGL_preferred_state.pixel_motion_blur_samples;
		if (samples < 3)
			samples = 3;
		if (samples > 17)
			samples = 17;
		glUniform1i(motionblur_sample_count, samples);
	}
	const bool use_frozen_static_motion = MotionVectorsFrozen() && frozen_static_motion_valid;
	const float* reconstruction_projection = use_frozen_static_motion ? frozen_static_motion_projection :
		(captured_scene_projection_valid ? captured_scene_projection : current_projection);
	const float* reconstruction_inverse_modelview = use_frozen_static_motion ?
		frozen_static_motion_inverse_modelview :
		(captured_scene_inverse_modelview_valid ? captured_scene_inverse_modelview : current_inverse_modelview);
	const float* reconstruction_previous_view_projection = use_frozen_static_motion ?
		frozen_static_motion_previous_view_projection : previous_view_projection;
	const bool has_reconstruction_matrices = use_frozen_static_motion ||
		(captured_scene_projection_valid && captured_scene_inverse_modelview_valid) ||
		(have_current_projection && have_current_inverse_modelview);
	const bool has_static_reconstruction = depth_texture != 0 &&
		(use_frozen_static_motion || have_previous_view_projection) &&
		has_reconstruction_matrices;
	if (motionblur_current_projection != -1)
		glUniformMatrix4fv(motionblur_current_projection, 1, GL_FALSE, reconstruction_projection);
	if (motionblur_current_inverse_modelview != -1)
		glUniformMatrix4fv(motionblur_current_inverse_modelview, 1, GL_FALSE, reconstruction_inverse_modelview);
	if (motionblur_previous_view_projection != -1)
		glUniformMatrix4fv(motionblur_previous_view_projection, 1, GL_FALSE,
			reconstruction_previous_view_projection);
	if (motionblur_has_static_reconstruction != -1)
		glUniform1i(motionblur_has_static_reconstruction, has_static_reconstruction ? 1 : 0);
	if (motionblur_has_dynamic_velocity != -1)
		glUniform1i(motionblur_has_dynamic_velocity, has_dynamic_velocity ? 1 : 0);
	if (motionblur_player_translation_delta != -1)
	{
		vector player_translation_delta = {};
		rend_GetMotionBlurPlayerTranslationDelta(&player_translation_delta);
		glUniform3f(motionblur_player_translation_delta,
			player_translation_delta.x, player_translation_delta.y, player_translation_delta.z);
	}

	rend_ClearBoundTextures();
	GL_BindFramebufferTexture(post_present_framebuffer.ColorTextureForRead(), 0, GL_LINEAR);
	if (velocity_texture != 0)
		GL_BindFramebufferTexture(velocity_texture, 1, GL_NEAREST);
	if (depth_texture != 0)
		GL_BindFramebufferTexture(depth_texture, 3, GL_NEAREST);
	if (object_id_texture != 0)
		GL_BindFramebufferTexture(object_id_texture, 4, GL_NEAREST);
	GL_DrawFramebufferQuad(motion_blur_framebuffer.Handle(), 0, 0,
		motion_blur_framebuffer.Width(), motion_blur_framebuffer.Height());
	motion_blur_framebuffer.BlitToRaw(post_present_framebuffer.Handle(), 0, 0,
		post_present_framebuffer.Width(), post_present_framebuffer.Height(), GL_NEAREST);
}

void GL4Renderer::DrawMotionVectorDebugPreview(int supersampling_factor)
{
	renderer_motion_vector_debug_sample debug_sample = {};
	debug_sample.mode = PixelMotionVectorConsumerActive() ?
		RENDERER_MOTION_VECTOR_PIXEL : RENDERER_MOTION_VECTOR_OFF;

	if (!OpenGL_preferred_state.motion_vector_debug_preview || Game_mode == GM_NONE ||
		!MotionVectorTargetEnabled() || motionvectordebugshader.Handle() == 0 ||
		post_present_framebuffer.Handle() == 0)
	{
		rend_SetMotionVectorDebugSample(nullptr);
		return;
	}

	GLuint velocity_texture = motion_vectors.TextureForRead(framebuffers[framebuffer_current_draw].Handle());
	if (velocity_texture == 0 || motion_vectors.width == 0 || motion_vectors.height == 0)
	{
		rend_SetMotionVectorDebugSample(&debug_sample);
		return;
	}
	GLuint depth_texture = bloom_source_valid && bloom_source_framebuffer.Handle() != 0 ?
		bloom_source_framebuffer.DepthTextureForRead() :
		framebuffers[framebuffer_current_draw].DepthTextureForRead();
	GLuint object_id_texture = motion_vectors.ObjectIdTextureForRead(framebuffers[framebuffer_current_draw].Handle());

	if (supersampling_factor < 1)
		supersampling_factor = 1;
	const int framebuffer_logical_bottom_offset =
		framebuffer_logical_height - OpenGL_state.screen_height - framebuffer_logical_offset_y;
	const float source_width = (float)motion_vectors.width;
	const float source_height = (float)motion_vectors.height;
	const float uv_origin_x = (float)(framebuffer_logical_offset_x * supersampling_factor) / source_width;
	const float uv_origin_y = (float)(framebuffer_logical_bottom_offset * supersampling_factor) / source_height;
	const float uv_scale_x = (float)(OpenGL_state.screen_width * supersampling_factor) / source_width;
	const float uv_scale_y = (float)(OpenGL_state.screen_height * supersampling_factor) / source_height;
	const int center_x = std::max(0, std::min((int)motion_vectors.width - 1,
		(int)((uv_origin_x + uv_scale_x * 0.5f) * source_width)));
	const int center_y = std::max(0, std::min((int)motion_vectors.height - 1,
		(int)((uv_origin_y + uv_scale_y * 0.5f) * source_height)));

	GLint old_read = 0;
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &old_read);
	GLint old_read_buffer = GL_COLOR_ATTACHMENT0;
	glGetIntegerv(GL_READ_BUFFER, &old_read_buffer);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, motion_vectors.resolve_framebuffer);
	glReadBuffer(GL_COLOR_ATTACHMENT0);
	float center_velocity[2] = {};
	glReadPixels(center_x, center_y, 1, 1, GL_RG, GL_FLOAT, center_velocity);
	float max_velocity[2] = {};
	float max_mag_squared = 0.0f;
	int max_x = center_x;
	int max_y = center_y;
	const int sample_columns = 9;
	const int sample_rows = 7;
	for (int row = 0; row < sample_rows; row++)
	{
		for (int column = 0; column < sample_columns; column++)
		{
			const float u = ((float)column + 0.5f) / (float)sample_columns;
			const float v = ((float)row + 0.5f) / (float)sample_rows;
			const int sample_x = std::max(0, std::min((int)motion_vectors.width - 1,
				(int)((uv_origin_x + uv_scale_x * u) * source_width)));
			const int sample_y = std::max(0, std::min((int)motion_vectors.height - 1,
				(int)((uv_origin_y + uv_scale_y * v) * source_height)));
			float velocity[2] = {};
			glReadPixels(sample_x, sample_y, 1, 1, GL_RG, GL_FLOAT, velocity);
			const float mag_squared = velocity[0] * velocity[0] + velocity[1] * velocity[1];
			if (mag_squared > max_mag_squared)
			{
				max_mag_squared = mag_squared;
				max_velocity[0] = velocity[0];
				max_velocity[1] = velocity[1];
				max_x = sample_x;
				max_y = sample_y;
			}
		}
	}
	glBindFramebuffer(GL_READ_FRAMEBUFFER, old_read);
	glReadBuffer(old_read_buffer);
	debug_sample.valid = true;
	debug_sample.x = center_x;
	debug_sample.y = center_y;
	debug_sample.width = (int)motion_vectors.width;
	debug_sample.height = (int)motion_vectors.height;
	debug_sample.vx = center_velocity[0];
	debug_sample.vy = center_velocity[1];
	debug_sample.max_x = max_x;
	debug_sample.max_y = max_y;
	debug_sample.max_vx = max_velocity[0];
	debug_sample.max_vy = max_velocity[1];
	debug_sample.max_mag = max_mag_squared;

	rend_SetMotionVectorDebugSample(&debug_sample);

	motionvectordebugshader.Use();
	if (motionvectordebug_velocity_source != -1)
		glUniform1i(motionvectordebug_velocity_source, 0);
	if (motionvectordebug_depth_source != -1)
		glUniform1i(motionvectordebug_depth_source, 1);
	if (motionvectordebug_object_id_source != -1)
		glUniform1i(motionvectordebug_object_id_source, 2);
	if (motionvectordebug_uv_origin != -1)
		glUniform2f(motionvectordebug_uv_origin, uv_origin_x, uv_origin_y);
	if (motionvectordebug_uv_scale != -1)
		glUniform2f(motionvectordebug_uv_scale, uv_scale_x, uv_scale_y);
	if (motionvectordebug_screen_size != -1)
		glUniform2f(motionvectordebug_screen_size, (float)OpenGL_state.screen_width, (float)OpenGL_state.screen_height);
	const bool use_frozen_static_motion = MotionVectorsFrozen() && frozen_static_motion_valid;
	const float* reconstruction_projection = use_frozen_static_motion ? frozen_static_motion_projection :
		(captured_scene_projection_valid ? captured_scene_projection : current_projection);
	const float* reconstruction_inverse_modelview = use_frozen_static_motion ?
		frozen_static_motion_inverse_modelview :
		(captured_scene_inverse_modelview_valid ? captured_scene_inverse_modelview : current_inverse_modelview);
	const float* reconstruction_previous_view_projection = use_frozen_static_motion ?
		frozen_static_motion_previous_view_projection : previous_view_projection;
	const bool has_reconstruction_matrices = use_frozen_static_motion ||
		(captured_scene_projection_valid && captured_scene_inverse_modelview_valid) ||
		(have_current_projection && have_current_inverse_modelview);
	const bool has_static_reconstruction = depth_texture != 0 &&
		(use_frozen_static_motion || have_previous_view_projection) &&
		has_reconstruction_matrices;
	if (motionvectordebug_current_projection != -1)
		glUniformMatrix4fv(motionvectordebug_current_projection, 1, GL_FALSE, reconstruction_projection);
	if (motionvectordebug_current_inverse_modelview != -1)
		glUniformMatrix4fv(motionvectordebug_current_inverse_modelview, 1, GL_FALSE,
			reconstruction_inverse_modelview);
	if (motionvectordebug_previous_view_projection != -1)
		glUniformMatrix4fv(motionvectordebug_previous_view_projection, 1, GL_FALSE,
			reconstruction_previous_view_projection);
	if (motionvectordebug_has_static_reconstruction != -1)
		glUniform1i(motionvectordebug_has_static_reconstruction, has_static_reconstruction ? 1 : 0);
	if (motionvectordebug_has_dynamic_velocity != -1)
		glUniform1i(motionvectordebug_has_dynamic_velocity, motion_vectors_dirty ? 1 : 0);

	rend_ClearBoundTextures();
	GL_BindFramebufferTexture(velocity_texture, 0, GL_NEAREST);
	if (depth_texture != 0)
		GL_BindFramebufferTexture(depth_texture, 1, GL_NEAREST);
	if (object_id_texture != 0)
		GL_BindFramebufferTexture(object_id_texture, 2, GL_NEAREST);

	const GLboolean blend_was_enabled = glIsEnabled(GL_BLEND);
	const GLboolean depth_was_enabled = glIsEnabled(GL_DEPTH_TEST);
	const GLboolean cull_was_enabled = glIsEnabled(GL_CULL_FACE);
	const GLboolean scissor_was_enabled = glIsEnabled(GL_SCISSOR_TEST);
	GLboolean color_mask[4];
	GLboolean depth_mask;
	glGetBooleanv(GL_COLOR_WRITEMASK, color_mask);
	glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
	GLint old_src_rgb = GL_ONE;
	GLint old_dst_rgb = GL_ZERO;
	GLint old_src_alpha = GL_ONE;
	GLint old_dst_alpha = GL_ZERO;
	glGetIntegerv(GL_BLEND_SRC_RGB, &old_src_rgb);
	glGetIntegerv(GL_BLEND_DST_RGB, &old_dst_rgb);
	glGetIntegerv(GL_BLEND_SRC_ALPHA, &old_src_alpha);
	glGetIntegerv(GL_BLEND_DST_ALPHA, &old_dst_alpha);
	GLint old_viewport[4];
	glGetIntegerv(GL_VIEWPORT, old_viewport);
	GLint old_draw = 0;
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &old_draw);

	glBindVertexArray(GL_GetFramebufferVAO());
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, post_present_framebuffer.Handle());
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	glViewport(0, 0, post_present_framebuffer.Width(), post_present_framebuffer.Height());
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_SCISSOR_TEST);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask(GL_FALSE);
	rend_RecordDrawCall(RENDERER_DRAW_CALL_POSTPROCESS);
	glDrawArrays(GL_TRIANGLES, 0, 3);

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, old_draw);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, old_read);
	glReadBuffer(old_read_buffer);
	glViewport(old_viewport[0], old_viewport[1], old_viewport[2], old_viewport[3]);
	glBlendFuncSeparate(old_src_rgb, old_dst_rgb, old_src_alpha, old_dst_alpha);
	if (blend_was_enabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
	if (depth_was_enabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
	if (cull_was_enabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
	if (scissor_was_enabled) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
	glColorMask(color_mask[0], color_mask[1], color_mask[2], color_mask[3]);
	glDepthMask(depth_mask);
	rend_ClearBoundTextures();
	RestoreLegacy();
}

void GL4Renderer::UseSceneDrawBuffers()
{
	post_protection_mask.UseSceneDrawBuffers(framebuffers[framebuffer_current_draw].Handle(), false, false);
}

static void GL4SetDrawVertexAttributes()
{
	//attrib 0: position
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(gl_vertex), 0);

	//attrib 1: color
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(gl_vertex), (const void*)offsetof(gl_vertex, color));

	//attrib 2: uv
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(gl_vertex), (const void*)offsetof(gl_vertex, tex_coord));

	//attrib 3: uv 2
	glEnableVertexAttribArray(3);
	glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(gl_vertex), (const void*)offsetof(gl_vertex, tex_coord2));

	//attrib 4: per-pixel lighting normal
	glEnableVertexAttribArray(4);
	glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(gl_vertex), (const void*)offsetof(gl_vertex, normal));

	//attrib 5: perspective-correct world position for pixel motion vectors
	glEnableVertexAttribArray(5);
	glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(gl_vertex), (const void*)offsetof(gl_vertex, motion_world_position));

	//attrib 6: perspective-correct previous world position for moving pixel motion vectors
	glEnableVertexAttribArray(6);
	glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, sizeof(gl_vertex), (const void*)offsetof(gl_vertex, motion_previous_world_position));

	//attribs 7-14: perspective-correct per-vertex field static specular sources
	for (int i = 0; i < MAX_SPECULARS; i++)
	{
		glEnableVertexAttribArray(7 + i);
		glVertexAttribPointer(7 + i, 4, GL_FLOAT, GL_FALSE, sizeof(gl_vertex),
			(const void*)offsetof(gl_vertex, field_specular_center[i]));
		glEnableVertexAttribArray(11 + i);
		glVertexAttribPointer(11 + i, 4, GL_FLOAT, GL_FALSE, sizeof(gl_vertex),
			(const void*)offsetof(gl_vertex, field_specular_color[i]));
	}
}

bool GL4Renderer::InitPersistentDrawBuffer(size_t size)
{
	size_t vertex_capacity = size / sizeof(gl_vertex);
	vertex_capacity -= vertex_capacity % GL4_DRAW_BUFFER_SYNC_POINT_COUNT;
	if (vertex_capacity < GL4_DRAW_BUFFER_SYNC_POINT_COUNT)
		return false;
	size = vertex_capacity * sizeof(gl_vertex);

	ResetPersistentDrawBufferSyncs();
	drawbuffer_vertex_capacity = 0;

	//Due to names becoming immutable when using buffer storage,
	//need to recycle buffers by explicitly deleting the old one. OpenGL maintains its lifetime until it is done
	if (drawbuffer != 0)
	{
		glBindBuffer(GL_ARRAY_BUFFER, drawbuffer);
		if (drawbuffermap != nullptr)
			glUnmapBuffer(GL_ARRAY_BUFFER);
		glDeleteBuffers(1, &drawbuffer);
		drawbuffer = 0;
		drawbuffermap = nullptr;
	}

	glGenBuffers(1, &drawbuffer);
	glBindBuffer(GL_ARRAY_BUFFER, drawbuffer);

	glBufferStorage(GL_ARRAY_BUFFER, size, nullptr, GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
	drawbuffer_vertex_capacity = (GLuint)vertex_capacity;
	drawbuffer_vertices_per_sync_point = drawbuffer_vertex_capacity /
		GL4_DRAW_BUFFER_SYNC_POINT_COUNT;
	GLenum err = glGetError();
	if (err == GL_NO_ERROR)
	{
		GL4SetDrawVertexAttributes();
		drawbuffermap = glMapBufferRange(GL_ARRAY_BUFFER, 0, size,
			GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
		err = glGetError();
	}

	if (err == GL_NO_ERROR && drawbuffermap != nullptr)
		return true;

#ifdef _DEBUG
	Int3();
#endif
	if (drawbuffer != 0)
	{
		glBindBuffer(GL_ARRAY_BUFFER, drawbuffer);
		if (drawbuffermap != nullptr)
			glUnmapBuffer(GL_ARRAY_BUFFER);
		glDeleteBuffers(1, &drawbuffer);
	}

	drawbuffer = 0;
	drawbuffermap = nullptr;
	drawbuffer_vertex_capacity = 0;
	OpenGL_buffer_storage_enabled = false;

	glGenBuffers(1, &drawbuffer);
	glBindBuffer(GL_ARRAY_BUFFER, drawbuffer);
	glBufferData(GL_ARRAY_BUFFER, size, nullptr, GL_STREAM_DRAW);
	drawbuffer_vertex_capacity = (GLuint)vertex_capacity;
	GL4SetDrawVertexAttributes();
	return false;
}

void GL4Renderer::ResetPersistentDrawBufferSyncs()
{
	for (GLuint i = 0; i < GL4_DRAW_BUFFER_SYNC_POINT_COUNT; i++)
	{
		if (drawbuffer_sync_points[i] != nullptr && glDeleteSync != nullptr)
			glDeleteSync(drawbuffer_sync_points[i]);
		drawbuffer_sync_points[i] = nullptr;
	}
	drawbuffer_vertices_per_sync_point = 0;
	drawbuffer_used_sync_point = 0;
	drawbuffer_available_sync_point = GL4_DRAW_BUFFER_SYNC_POINT_COUNT;
	nextcommittedvertex = 0;
}

void GL4Renderer::DestroyPersistentDrawBuffer()
{
	ResetPersistentDrawBufferSyncs();
	drawbuffer_vertex_capacity = 0;
	if (drawbuffer)
	{
		glBindBuffer(GL_ARRAY_BUFFER, drawbuffer);
		if (drawbuffermap != nullptr)
			glUnmapBuffer(GL_ARRAY_BUFFER);
		glDeleteBuffers(1, &drawbuffer);
		drawbuffer = 0;
		drawbuffermap = nullptr;
	}
}

void GL4Renderer::WaitForPersistentDrawBufferSync(GLsync& sync)
{
	if (sync == nullptr)
	{
		// This is only reachable if fence creation failed or the checkpoint bookkeeping is broken.
		// Preserve correctness rather than allowing the CPU to overwrite vertices still in use.
		glFinish();
		return;
	}

	const GLenum wait_result = glClientWaitSync(sync, GL_SYNC_FLUSH_COMMANDS_BIT,
		GL_TIMEOUT_IGNORED);
	if (wait_result == GL_WAIT_FAILED)
		glFinish();
	if (glDeleteSync != nullptr)
		glDeleteSync(sync);
	sync = nullptr;
}

void GL4Renderer::AddPersistentDrawBufferSyncsForOffset(GLuint offset)
{
	if (drawbuffer_vertices_per_sync_point == 0)
		return;

	const GLuint end_sync_point = std::min(offset / drawbuffer_vertices_per_sync_point,
		GL4_DRAW_BUFFER_SYNC_POINT_COUNT);
	while (drawbuffer_used_sync_point < end_sync_point)
	{
		GLsync& sync = drawbuffer_sync_points[drawbuffer_used_sync_point];
		if (sync != nullptr)
			WaitForPersistentDrawBufferSync(sync);
		sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
		drawbuffer_used_sync_point++;
	}
}

void GL4Renderer::EnsurePersistentDrawBufferSyncsWaitedForOffset(GLuint offset)
{
	if (drawbuffer_vertices_per_sync_point == 0)
		return;

	const GLuint64 rounded_offset = (GLuint64)offset +
		drawbuffer_vertices_per_sync_point - 1;
	const GLuint end_sync_point = std::min(
		(GLuint)(rounded_offset / drawbuffer_vertices_per_sync_point),
		GL4_DRAW_BUFFER_SYNC_POINT_COUNT);
	while (drawbuffer_available_sync_point < end_sync_point)
	{
		WaitForPersistentDrawBufferSync(
			drawbuffer_sync_points[drawbuffer_available_sync_point]);
		drawbuffer_available_sync_point++;
	}
}

int GL4Renderer::CopyVertices(int numvertices)
{
	return CopyVertices(GL_vertices, numvertices);
}

int GL4Renderer::CopyVertices(const gl_vertex* vertices, int numvertices)
{
	const GLuint vertex_capacity = drawbuffer_vertex_capacity != 0 ?
		drawbuffer_vertex_capacity : (GLuint)GL4DrawBufferVertexCapacity();
	const GLuint requested_vertices = numvertices > 0 ? (GLuint)numvertices : 0;
	if (OpenGL_buffer_storage_enabled)
	{
		if (drawbuffermap == nullptr || drawbuffer_vertices_per_sync_point == 0 ||
			requested_vertices > vertex_capacity)
		{
			GLuint required_capacity = std::max(requested_vertices,
				(GLuint)GL4DrawBufferVertexCapacity());
			const GLuint remainder = required_capacity % GL4_DRAW_BUFFER_SYNC_POINT_COUNT;
			if (remainder != 0)
				required_capacity += GL4_DRAW_BUFFER_SYNC_POINT_COUNT - remainder;
			const size_t buffersize = (size_t)required_capacity * sizeof(gl_vertex);
			if (drawbuffer != 0)
				glFinish();
			if (!InitPersistentDrawBuffer(buffersize))
				return CopyVertices(vertices, numvertices);
			return CopyVertices(vertices, numvertices);
		}

		AddPersistentDrawBufferSyncsForOffset(nextcommittedvertex);
		if ((GLuint64)nextcommittedvertex + requested_vertices > vertex_capacity)
		{
			// Retire any checkpoints from the preceding lap before reusing their slots,
			// fence all work submitted for this lap, then wait only for the range being reused.
			EnsurePersistentDrawBufferSyncsWaitedForOffset(vertex_capacity);
			AddPersistentDrawBufferSyncsForOffset(vertex_capacity);
			nextcommittedvertex = 0;
			WaitForPersistentDrawBufferSync(drawbuffer_sync_points[0]);
			drawbuffer_available_sync_point = 1;
			drawbuffer_used_sync_point = 0;
		}
		EnsurePersistentDrawBufferSyncsWaitedForOffset(
			nextcommittedvertex + requested_vertices);

		glBindBuffer(GL_ARRAY_BUFFER, drawbuffer);
		int startoffset = nextcommittedvertex;
		void* dataptr = (void*)((uintptr_t)drawbuffermap + startoffset * sizeof(gl_vertex));
		memcpy(dataptr, vertices, numvertices * sizeof(gl_vertex));

		nextcommittedvertex += numvertices;

		return startoffset;
	}
	else
	{
		glBindBuffer(GL_ARRAY_BUFFER, drawbuffer);
		if (nextcommittedvertex + requested_vertices > vertex_capacity)
		{
			size_t buffersize = GL4DrawBufferByteSize();
			if (requested_vertices > vertex_capacity)
				buffersize = (size_t)numvertices * sizeof(gl_vertex);
			glBufferData(GL_ARRAY_BUFFER, buffersize, nullptr, GL_STREAM_DRAW);
			drawbuffer_vertex_capacity = (GLuint)(buffersize / sizeof(gl_vertex));
			nextcommittedvertex = 0;
		}

		int startoffset = nextcommittedvertex;

		void* dataptr = glMapBufferRange(GL_ARRAY_BUFFER, startoffset * sizeof(gl_vertex), numvertices * sizeof(gl_vertex), GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
		if (dataptr != nullptr)
		{
			memcpy(dataptr, vertices, numvertices * sizeof(gl_vertex));
			glUnmapBuffer(GL_ARRAY_BUFFER);
		}
		else
		{
			glBufferSubData(GL_ARRAY_BUFFER, startoffset * sizeof(gl_vertex),
				numvertices * sizeof(gl_vertex), vertices);
		}

		nextcommittedvertex += numvertices;

		return startoffset;
	}
}

void GL4Renderer::BuildDrawVertex(gl_vertex& vert, const g3Point* pnt, float xscalar, float yscalar,
	ubyte fr, ubyte fg, ubyte fb)
{
	vert.normal.x = 0.0f;
	vert.normal.y = 0.0f;
	vert.normal.z = 0.0f;
	vert.normal.w = -1.0f;
	const bool per_pixel_specular_draw = OpenGL_state.cur_alpha_type == AT_SPECULAR &&
		OpenGL_preferred_state.per_pixel_lighting;

	float alpha = Alpha_multiplier * OpenGL_Alpha_factor;
	if (OpenGL_state.cur_alpha_type & ATF_VERTEX)
		alpha = pnt->p3_a * Alpha_multiplier * OpenGL_Alpha_factor;

	if (OpenGL_state.cur_light_state != LS_NONE)
	{
		if (OpenGL_state.cur_light_state == LS_FLAT_GOURAUD)
		{
			vert.color.r = fr;
			vert.color.g = fg;
			vert.color.b = fb;
			vert.color.a = (ubyte)alpha;
		}
		else
		{
			if (OpenGL_state.cur_color_model == CM_MONO)
			{
				vert.color.r = pnt->p3_l * 255;
				vert.color.g = pnt->p3_l * 255;
				vert.color.b = pnt->p3_l * 255;
				vert.color.a = (ubyte)alpha;
			}
			else
			{
				vert.color.r = pnt->p3_r * 255;
				vert.color.g = pnt->p3_g * 255;
				vert.color.b = pnt->p3_b * 255;
				vert.color.a = (ubyte)alpha;
			}
		}
	}
	else
	{
		if (OpenGL_state.cur_texture_type != 0)
		{
			vert.color.r = 255;
			vert.color.g = 255;
			vert.color.b = 255;
			vert.color.a = (ubyte)alpha;
		}
		else
		{
			vert.color.r = fr;
			vert.color.g = fg;
			vert.color.b = fb;
			vert.color.a = (ubyte)alpha;
		}
	}

	if (OpenGL_state.cur_texture_type != 0)
	{
		float texw = 1.0 / (pnt->p3_z + Z_bias);
		vert.tex_coord.s = pnt->p3_u * texw;
		vert.tex_coord.t = pnt->p3_v * texw;
		vert.tex_coord.w = texw;

		if (Overlay_type != OT_NONE)
		{
			vert.tex_coord2.s = pnt->p3_u2 * xscalar * texw;
			vert.tex_coord2.t = pnt->p3_v2 * yscalar * texw;
			vert.tex_coord2.w = texw;
		}
	}

	if (per_pixel_specular_draw)
	{
		vector specular_normal = GL4ViewSpaceSpecularNormal(pnt, per_pixel_dynamic_face_normal);
		vert.normal.x = specular_normal.x;
		vert.normal.y = specular_normal.y;
		vert.normal.z = specular_normal.z;
		vert.normal.w = 1.0f;
	}
	else if (OpenGL_state.cur_light_state == LS_PHONG || per_pixel_dynamic_light_count > 0)
	{
		float payloadw = 1.0f / (pnt->p3_z + Z_bias);
		vert.normal.x = pnt->p3_vecPreRot.x * payloadw;
		vert.normal.y = pnt->p3_vecPreRot.y * payloadw;
		vert.normal.z = pnt->p3_vecPreRot.z * payloadw;
		vert.normal.w = payloadw;
	}

	if (soft_particle_draw_enabled)
		vert.normal.w = GL4SoftParticleDepthFromPoint(pnt);

	vert.vert.x = pnt->p3_sx;
	vert.vert.y = pnt->p3_sy;
	vert.motion_world_position.x = 0.0f;
	vert.motion_world_position.y = 0.0f;
	vert.motion_world_position.z = 0.0f;
	vert.motion_world_position.w = 0.0f;
	vert.motion_previous_world_position.x = 0.0f;
	vert.motion_previous_world_position.y = 0.0f;
	vert.motion_previous_world_position.z = 0.0f;
	vert.motion_previous_world_position.w = 0.0f;
	if (per_pixel_specular_draw)
	{
		float payloadw = 1.0f / (pnt->p3_z + Z_bias);
		vector view_position = GL4ViewSpacePositionFromPoint(pnt);
		vert.motion_world_position.x = view_position.x * payloadw;
		vert.motion_world_position.y = view_position.y * payloadw;
		vert.motion_world_position.z = view_position.z * payloadw;
		vert.motion_world_position.w = payloadw;
	}
	else if (CurrentDrawWritesPixelMotionVectors() && !motion_object_active)
	{
		float payloadw = 1.0f / (pnt->p3_z + Z_bias);
		vert.motion_world_position.x = pnt->p3_vecPreRot.x * payloadw;
		vert.motion_world_position.y = pnt->p3_vecPreRot.y * payloadw;
		vert.motion_world_position.z = pnt->p3_vecPreRot.z * payloadw;
		vert.motion_world_position.w = payloadw;
	}
	else if (CurrentDrawWritesPixelMotionVectors() && cockpit_motion_object_active &&
		pnt->p3_motion_world_valid && pnt->p3_motion_prev_world_valid &&
		have_cockpit_previous_view_projection)
	{
		vec4_array current_clip = {};
		vec4_array previous_clip = {};
		GL4TransformWorldToClip(current_view_projection, pnt->p3_motion_world_pos, current_clip);
		GL4TransformWorldToClip(cockpit_previous_view_projection, pnt->p3_motion_prev_world_pos, previous_clip);
		if (current_clip.w > 0.00001f && previous_clip.w > 0.00001f)
		{
			float payloadw = 1.0f / (pnt->p3_z + Z_bias);
			vert.motion_world_position.x = (current_clip.x / current_clip.w) * payloadw;
			vert.motion_world_position.y = (current_clip.y / current_clip.w) * payloadw;
			vert.motion_world_position.z = 0.0f;
			vert.motion_world_position.w = payloadw;
			vert.motion_previous_world_position.x = (previous_clip.x / previous_clip.w) * payloadw;
			vert.motion_previous_world_position.y = (previous_clip.y / previous_clip.w) * payloadw;
			vert.motion_previous_world_position.z = 0.0f;
			vert.motion_previous_world_position.w = payloadw;
		}
	}
	else if (CurrentDrawWritesPixelMotionVectors() && motion_object_active &&
		pnt->p3_motion_world_valid)
	{
		float payloadw = 1.0f / (pnt->p3_z + Z_bias);
		vert.motion_world_position.x = pnt->p3_motion_world_pos.x * payloadw;
		vert.motion_world_position.y = pnt->p3_motion_world_pos.y * payloadw;
		vert.motion_world_position.z = pnt->p3_motion_world_pos.z * payloadw;
		vert.motion_world_position.w = payloadw;
		if (pnt->p3_motion_prev_world_valid)
		{
			vert.motion_previous_world_position.x = pnt->p3_motion_prev_world_pos.x * payloadw;
			vert.motion_previous_world_position.y = pnt->p3_motion_prev_world_pos.y * payloadw;
			vert.motion_previous_world_position.z = pnt->p3_motion_prev_world_pos.z * payloadw;
			vert.motion_previous_world_position.w = payloadw;
		}
	}
	if (room_fog_enabled)
	{
		float payloadw = 1.0f / (pnt->p3_z + Z_bias);
		vert.motion_previous_world_position.x = pnt->p3_vecPreRot.x * payloadw;
		vert.motion_previous_world_position.y = pnt->p3_vecPreRot.y * payloadw;
		vert.motion_previous_world_position.z = pnt->p3_vecPreRot.z * payloadw;
		vert.motion_previous_world_position.w = payloadw;
	}

	float z = GL4DepthFromEyeZ(pnt->p3_z + Z_bias);
	vert.vert.z = -z;
	GL4SetFieldSpecularVertexPayload(vert, pnt, per_pixel_specular_draw);
}

void GL4Renderer::SetFontBatchFullscreenDrawState(GLint old_viewport[4])
{
	glGetIntegerv(GL_VIEWPORT, old_viewport);

	float projection[16];
	GL4BuildOrtho(projection, 0, (float)OpenGL_state.screen_width, (float)OpenGL_state.screen_height, 0, 0, 1);
	UpdateLegacyBlock(projection, GL4FontBatchIdentity);

	if (GL4DrawTargetIsFramebuffer(post_present_framebuffer.Handle()) || !framebuffer_ok)
	{
		glViewport(0, 0, OpenGL_state.screen_width, OpenGL_state.screen_height);
	}
	else
	{
		glViewport(ScaledX(0), FramebufferHeight() - ScaledY(OpenGL_state.screen_height),
			ScaledW(OpenGL_state.screen_width), ScaledH(OpenGL_state.screen_height));
	}
}

void GL4Renderer::RestoreFontBatchDrawState(const GLint old_viewport[4])
{
	int clip_width = OpenGL_state.clip_x2 - OpenGL_state.clip_x1;
	int clip_height = OpenGL_state.clip_y2 - OpenGL_state.clip_y1;
	if (clip_width <= 0)
		clip_width = OpenGL_state.screen_width;
	if (clip_height <= 0)
		clip_height = OpenGL_state.screen_height;

	float projection[16];
	GL4BuildOrtho(projection, 0, (float)clip_width, (float)clip_height, 0, 0, 1);
	UpdateLegacyBlock(projection, GL4FontBatchIdentity);
	glViewport(old_viewport[0], old_viewport[1], old_viewport[2], old_viewport[3]);
}

bool GL4Renderer::FontBatchHasVertices() const
{
	return !font_batch_vertices[0].empty() || !font_batch_vertices[1].empty();
}

void GL4Renderer::ClearFontBatchVertices()
{
	font_batch_vertices[0].clear();
	font_batch_vertices[1].clear();
}

void GL4Renderer::FlushFontBatchVertices(int batch_index)
{
	if (batch_index < 0 || batch_index >= 2 || font_batch_vertices[batch_index].empty())
		return;

	if (batch_index == 1)
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	else
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

	float old_ao_suppression = ao_suppression_draw_value;
	float old_bloom_suppression = bloom_suppression_draw_value;
	if (ao_suppression_draw_value != 1.0f || bloom_suppression_draw_value != 1.0f)
		legacy_draw_uniforms_dirty = true;
	ao_suppression_draw_value = 1.0f;
	bloom_suppression_draw_value = 1.0f;
	post_protection_mask_dirty = true;

	fontshader.Use();
	if (font_texture_array != 0)
	{
		if (UseMultitexture && Last_texel_unit_set != 0)
		{
			glActiveTexture(GL_TEXTURE0);
			Last_texel_unit_set = 0;
		}
		glBindTexture(GL_TEXTURE_2D_ARRAY, font_texture_array);
	}

	const int offset = CopyVertices(font_batch_vertices[batch_index].data(), (int)font_batch_vertices[batch_index].size());
	const bool suppress_ao_class_write = framebuffer_ok &&
		OpenGL_state.cur_zbuffer_state == 0 &&
		GL4DrawTargetIsFramebuffer(framebuffers[framebuffer_current_draw].Handle());
	if (suppress_ao_class_write)
		GL4UseSceneDrawBuffersWithoutAOClass(false, false);
	rend_RecordDrawCall(RENDERER_DRAW_CALL_FONT);
	glDrawArrays(GL_TRIANGLES, offset, (GLsizei)font_batch_vertices[batch_index].size());
	if (suppress_ao_class_write)
		UseSceneDrawBuffers();

	font_batch_vertices[batch_index].clear();

	if (ao_suppression_draw_value != old_ao_suppression || bloom_suppression_draw_value != old_bloom_suppression)
		legacy_draw_uniforms_dirty = true;
	ao_suppression_draw_value = old_ao_suppression;
	bloom_suppression_draw_value = old_bloom_suppression;
	ShaderProgram::ClearBinding();
}

void GL4Renderer::FlushFontBatch()
{
	if (!FontBatchHasVertices())
		return;

	GLint old_viewport[4] = {};
	GLboolean depth_test_was_enabled = glIsEnabled(GL_DEPTH_TEST);
	GLboolean blend_was_enabled = glIsEnabled(GL_BLEND);
	GLint blend_src_rgb = GL_ONE;
	GLint blend_dst_rgb = GL_ZERO;
	GLint blend_src_alpha = GL_ONE;
	GLint blend_dst_alpha = GL_ZERO;
	glGetIntegerv(GL_BLEND_SRC_RGB, &blend_src_rgb);
	glGetIntegerv(GL_BLEND_DST_RGB, &blend_dst_rgb);
	glGetIntegerv(GL_BLEND_SRC_ALPHA, &blend_src_alpha);
	glGetIntegerv(GL_BLEND_DST_ALPHA, &blend_dst_alpha);

	SetFontBatchFullscreenDrawState(old_viewport);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);

	FlushFontBatchVertices(0);
	FlushFontBatchVertices(1);

	if (depth_test_was_enabled)
		glEnable(GL_DEPTH_TEST);
	else
		glDisable(GL_DEPTH_TEST);
	if (blend_was_enabled)
		glEnable(GL_BLEND);
	else
		glDisable(GL_BLEND);
	glBlendFuncSeparate(blend_src_rgb, blend_dst_rgb, blend_src_alpha, blend_dst_alpha);
	RestoreFontBatchDrawState(old_viewport);

	CHECK_ERROR(10);
}

void GL4Renderer::UploadFontTextureLayer(int layer, int bm_handle)
{
	if (layer < 0 || bm_handle < 0)
		return;

	const int w = bm_w(bm_handle, 0);
	const int h = bm_h(bm_handle, 0);
	ushort* bm_ptr = bm_data(bm_handle, 0);
	if (w <= 0 || h <= 0 || bm_ptr == nullptr)
		return;

	SetUploadBufferSize(w, h);
	if (bm_format(bm_handle) == BITMAP_FORMAT_4444)
	{
		for (int i = 0; i < w * h; i++)
			opengl_Upload_data[i] = opengl_4444_translate_table[bm_ptr[i]];
	}
	else
	{
		for (int i = 0; i < w * h; i++)
			opengl_Upload_data[i] = opengl_Translate_table[bm_ptr[i]];
	}

	if (UseMultitexture && Last_texel_unit_set != 0)
	{
		glActiveTexture(GL_TEXTURE0);
		Last_texel_unit_set = 0;
	}
	glBindTexture(GL_TEXTURE_2D_ARRAY, font_texture_array);
	glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, w, h, 1, GL_RGBA, GL_UNSIGNED_BYTE, opengl_Upload_data);
	GameBitmaps[bm_handle].flags &= ~(BF_CHANGED | BF_BRAND_NEW);
	OpenGL_uploads++;
}

int GL4Renderer::GetFontTextureLayer(int bm_handle)
{
	if (bm_handle < 0)
		return -1;

	const int w = bm_w(bm_handle, 0);
	const int h = bm_h(bm_handle, 0);
	if (w <= 0 || h <= 0)
		return -1;

	if (font_texture_array != 0 &&
		(font_texture_array_width != w || font_texture_array_height != h) &&
		FontBatchHasVertices())
	{
		FlushFontBatch();
	}

	if (font_texture_array == 0 || font_texture_array_width != w || font_texture_array_height != h)
	{
		if (font_texture_array != 0)
			glDeleteTextures(1, &font_texture_array);

		GLint max_layers = 0;
		glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &max_layers);
		font_texture_array_layers = std::max(1, std::min(max_layers > 0 ? max_layers : 256, 256));
		font_texture_array_width = w;
		font_texture_array_height = h;
		font_texture_array_handles.assign(font_texture_array_layers, -1);

		glGenTextures(1, &font_texture_array);
		glBindTexture(GL_TEXTURE_2D_ARRAY, font_texture_array);
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 0);
		glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, w, h, font_texture_array_layers, 0,
			GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	}

	for (int layer = 0; layer < (int)font_texture_array_handles.size(); layer++)
	{
		if (font_texture_array_handles[layer] == bm_handle)
		{
			if (GameBitmaps[bm_handle].flags & (BF_CHANGED | BF_BRAND_NEW))
				UploadFontTextureLayer(layer, bm_handle);
			return layer;
		}
	}

	for (int layer = 0; layer < (int)font_texture_array_handles.size(); layer++)
	{
		if (font_texture_array_handles[layer] == -1)
		{
			font_texture_array_handles[layer] = bm_handle;
			UploadFontTextureLayer(layer, bm_handle);
			return layer;
		}
	}

	return -1;
}

void GL4Renderer::DestroyFontBatchResources()
{
	ClearFontBatchVertices();
	font_texture_array_handles.clear();
	if (font_texture_array != 0)
	{
		glDeleteTextures(1, &font_texture_array);
		font_texture_array = 0;
	}
	font_texture_array_width = 0;
	font_texture_array_height = 0;
	font_texture_array_layers = 0;
}

void GL4Renderer::SetDrawDefaults()
{
	//Init shaders
	char* genericVertexBody = GL4ReadShaderFile("generic.vert", "opengl_SetDrawDefaults");
	char* genericFragBody = GL4ReadShaderFile("generic.frag", "opengl_SetDrawDefaults");

	//Without fog
	//No texturing
	drawshaders[0].AttachSourcePreprocess(genericVertexBody, genericFragBody, false, false, false, false);
	//Textured
	drawshaders[1].AttachSourcePreprocess(genericVertexBody, genericFragBody, true, false, false, false);
	//Textured and lightmapped
	drawshaders[2].AttachSourcePreprocess(genericVertexBody, genericFragBody, true, true, false, false);
	//Specular.
	drawshaders[3].AttachSourcePreprocess(genericVertexBody, genericFragBody, true, true, true, false);

	//With fog
	//No texturing
	drawshaders[4].AttachSourcePreprocess(genericVertexBody, genericFragBody, false, false, false, true);
	//Textured
	drawshaders[5].AttachSourcePreprocess(genericVertexBody, genericFragBody, true, false, false, true);
	//Textured and lightmapped
	drawshaders[6].AttachSourcePreprocess(genericVertexBody, genericFragBody, true, true, false, true);
	//Specular.
	drawshaders[7].AttachSourcePreprocess(genericVertexBody, genericFragBody, true, true, true, true);
	for (int i = 0; i < 8; i++)
	{
		drawshader_phong_enabled_uniforms[i] = drawshaders[i].FindUniform("phong_enabled");
		drawshader_light_direction_uniforms[i] = drawshaders[i].FindUniform("phong_light_direction");
		drawshader_dynamic_count_uniforms[i] = drawshaders[i].FindUniform("dynamic_light_count");
		drawshader_dynamic_face_normal_uniforms[i] = drawshaders[i].FindUniform("dynamic_face_normal");
		drawshader_dynamic_positions_uniforms[i] = drawshaders[i].FindUniform("dynamic_light_positions[0]");
		drawshader_dynamic_colors_uniforms[i] = drawshaders[i].FindUniform("dynamic_light_colors[0]");
		drawshader_dynamic_radii_uniforms[i] = drawshaders[i].FindUniform("dynamic_light_radii[0]");
		drawshader_dynamic_falloffs_uniforms[i] = drawshaders[i].FindUniform("dynamic_light_falloffs[0]");
		drawshader_dynamic_directions_uniforms[i] = drawshaders[i].FindUniform("dynamic_light_directions[0]");
		drawshader_dynamic_dot_ranges_uniforms[i] = drawshaders[i].FindUniform("dynamic_light_dot_ranges[0]");
		drawshader_dynamic_directional_uniforms[i] = drawshaders[i].FindUniform("dynamic_light_directional[0]");
		drawshader_per_pixel_specular_enabled_uniforms[i] = drawshaders[i].FindUniform("per_pixel_specular_enabled");
		drawshader_ao_suppression_uniforms[i] = drawshaders[i].FindUniform("ao_suppression");
		drawshader_bloom_suppression_uniforms[i] = drawshaders[i].FindUniform("bloom_suppression");
		drawshader_ao_class_uniforms[i] = drawshaders[i].FindUniform("ao_class_value");
		drawshader_ao_weight_uniforms[i] = drawshaders[i].FindUniform("ao_weight_value");
		drawshader_ao_capture_weight_mode_uniforms[i] = drawshaders[i].FindUniform("ao_capture_weight_mode");
		drawshader_post_mask_blend_mode_uniforms[i] = drawshaders[i].FindUniform("post_mask_blend_mode");
		drawshader_fast_additive_bitmap_uniforms[i] = drawshaders[i].FindUniform("fast_additive_bitmap");
		drawshader_fast_retained_room_base_uniforms[i] = drawshaders[i].FindUniform("fast_retained_room_base");
		drawshader_retained_room_lightmap_arrays_uniforms[i] =
			drawshaders[i].FindUniform("retained_room_lightmap_arrays");
		drawshader_retained_dynamic_lightmaps_uniforms[i] =
			drawshaders[i].FindUniform("retained_dynamic_lightmaps");
		drawshader_cockpit_backing_enabled_uniforms[i] = drawshaders[i].FindUniform("cockpit_backing_enabled");
		drawshader_cockpit_backing_alpha_uniforms[i] = drawshaders[i].FindUniform("cockpit_backing_alpha");
		drawshader_cockpit_backing_darkness_uniforms[i] = drawshaders[i].FindUniform("cockpit_backing_darkness");
		drawshader_cockpit_scanlines_enabled_uniforms[i] = drawshaders[i].FindUniform("cockpit_scanlines_enabled");
		drawshader_cockpit_scanline_strength_uniforms[i] = drawshaders[i].FindUniform("cockpit_scanline_strength");
		drawshader_cockpit_scanline_spacing_uniforms[i] = drawshaders[i].FindUniform("cockpit_scanline_spacing");
		drawshader_cockpit_scanline_thickness_uniforms[i] = drawshaders[i].FindUniform("cockpit_scanline_thickness");
		drawshader_cockpit_scanline_phase_uniforms[i] = drawshaders[i].FindUniform("cockpit_scanline_phase");
		drawshader_motion_vector_mode_uniforms[i] = drawshaders[i].FindUniform("motion_vector_mode");
		drawshader_motion_vector_current_view_projection_uniforms[i] = drawshaders[i].FindUniform("motion_vector_current_view_projection");
		drawshader_motion_vector_previous_view_projection_uniforms[i] = drawshaders[i].FindUniform("motion_vector_previous_view_projection");
		drawshader_motion_vector_has_previous_uniforms[i] = drawshaders[i].FindUniform("motion_vector_has_previous");
		drawshader_motion_vector_payload_type_uniforms[i] = drawshaders[i].FindUniform("motion_vector_payload_type");
		drawshader_motion_vector_object_id_uniforms[i] = drawshaders[i].FindUniform("motion_vector_object_id");
		drawshader_soft_particle_enabled_uniforms[i] = drawshaders[i].FindUniform("soft_particle_enabled");
		drawshader_soft_particle_screen_size_uniforms[i] = drawshaders[i].FindUniform("soft_particle_screen_size");
		drawshader_soft_particle_depth_range_uniforms[i] = drawshaders[i].FindUniform("soft_particle_depth_range");
		drawshader_retained_mode_uniforms[i] = drawshaders[i].FindUniform("retained_mode");
		drawshader_retained_transform_uniforms[i] = drawshaders[i].FindUniform("retained_transform");
		drawshader_retained_modelview_uniforms[i] = drawshaders[i].FindUniform("retained_modelview");
		drawshader_retained_current_world_uniforms[i] = drawshaders[i].FindUniform("retained_current_world");
		drawshader_retained_previous_world_uniforms[i] = drawshaders[i].FindUniform("retained_previous_world");
		drawshader_retained_uv_offset_uniforms[i] = drawshaders[i].FindUniform("retained_uv_offset");
		drawshader_retained_uv2_scale_uniforms[i] = drawshaders[i].FindUniform("retained_uv2_scale");
		drawshader_retained_base_color_uniforms[i] = drawshaders[i].FindUniform("retained_base_color");
		drawshader_retained_depth_bias_uniforms[i] = drawshaders[i].FindUniform("retained_depth_bias");
		drawshader_retained_legacy_depth_uniforms[i] = drawshaders[i].FindUniform("retained_legacy_depth");
		drawshader_retained_legacy_world_projection_uniforms[i] = drawshaders[i].FindUniform("retained_legacy_world_projection");
		drawshader_retained_legacy_view_position_uniforms[i] = drawshaders[i].FindUniform("retained_legacy_view_position");
		drawshader_retained_legacy_view_right_uniforms[i] = drawshaders[i].FindUniform("retained_legacy_view_right");
		drawshader_retained_legacy_view_up_uniforms[i] = drawshaders[i].FindUniform("retained_legacy_view_up");
		drawshader_retained_legacy_view_forward_uniforms[i] = drawshaders[i].FindUniform("retained_legacy_view_forward");
		drawshader_retained_legacy_viewport_scale_uniforms[i] = drawshaders[i].FindUniform("retained_legacy_viewport_scale");
		drawshader_retained_legacy_viewport_center_uniforms[i] = drawshaders[i].FindUniform("retained_legacy_viewport_center");
		drawshader_retained_lighting_mode_uniforms[i] = drawshaders[i].FindUniform("retained_lighting_mode");
		drawshader_retained_vertex_alpha_uniforms[i] = drawshaders[i].FindUniform("retained_vertex_alpha");
		drawshader_retained_alpha_scale_uniforms[i] = drawshaders[i].FindUniform("retained_alpha_scale");
		drawshader_retained_effect_mode_uniforms[i] = drawshaders[i].FindUniform("retained_effect_mode");
		drawshader_retained_effect_alpha_scale_uniforms[i] = drawshaders[i].FindUniform("retained_effect_alpha_scale");
		drawshader_retained_fog_plane_uniforms[i] = drawshaders[i].FindUniform("retained_fog_plane");
		drawshader_retained_fog_distance_uniforms[i] = drawshaders[i].FindUniform("retained_fog_distance");
		drawshader_retained_fog_eye_distance_uniforms[i] = drawshaders[i].FindUniform("retained_fog_eye_distance");
		drawshader_retained_fog_depth_uniforms[i] = drawshaders[i].FindUniform("retained_fog_depth");
		drawshader_retained_specular_view_position_uniforms[i] = drawshaders[i].FindUniform("retained_specular_view_position");
		drawshader_retained_specular_light_position_uniforms[i] = drawshaders[i].FindUniform("retained_specular_light_position");
		drawshader_retained_specular_scalar_uniforms[i] = drawshaders[i].FindUniform("retained_specular_scalar");
		drawshader_retained_specular_smooth_uniforms[i] = drawshaders[i].FindUniform("retained_specular_smooth");
		drawshader_retained_deform_enabled_uniforms[i] = drawshaders[i].FindUniform("retained_deform_enabled");
		drawshader_retained_deform_mode_uniforms[i] = drawshaders[i].FindUniform("retained_deform_mode");
		drawshader_retained_deform_seed_uniforms[i] = drawshaders[i].FindUniform("retained_deform_seed");
		drawshader_retained_deform_range_uniforms[i] = drawshaders[i].FindUniform("retained_deform_range");
		drawshader_retained_deform_direction_uniforms[i] = drawshaders[i].FindUniform("retained_deform_direction");
		drawshader_retained_custom_clip_enabled_uniforms[i] = drawshaders[i].FindUniform("retained_custom_clip_enabled");
		drawshader_retained_custom_clip_point_uniforms[i] = drawshaders[i].FindUniform("retained_custom_clip_point");
		drawshader_retained_custom_clip_plane_uniforms[i] = drawshaders[i].FindUniform("retained_custom_clip_plane");
		drawshader_retained_custom_clip_scale_uniforms[i] = drawshaders[i].FindUniform("retained_custom_clip_scale");
		drawshader_retained_near_clip_enabled_uniforms[i] = drawshaders[i].FindUniform("retained_near_clip_enabled");
		drawshader_retained_far_clip_enabled_uniforms[i] = drawshaders[i].FindUniform("retained_far_clip_enabled");
		drawshader_retained_far_clip_z_uniforms[i] = drawshaders[i].FindUniform("retained_far_clip_z");
		drawshader_retained_per_pixel_specular_payload_uniforms[i] = drawshaders[i].FindUniform("retained_per_pixel_specular_payload");
		drawshader_room_fog_enabled_uniforms[i] = drawshaders[i].FindUniform("room_fog_enabled");
		drawshader_room_fog_viewer_inside_uniforms[i] = drawshaders[i].FindUniform("room_fog_viewer_inside");
		drawshader_room_fog_viewer_position_uniforms[i] = drawshaders[i].FindUniform("room_fog_viewer_position");
		drawshader_room_fog_viewer_forward_uniforms[i] = drawshaders[i].FindUniform("room_fog_viewer_forward");
		drawshader_room_fog_color_uniforms[i] = drawshaders[i].FindUniform("room_fog_color");
		drawshader_room_fog_depth_uniforms[i] = drawshaders[i].FindUniform("room_fog_depth");
		drawshader_room_fog_intensity_uniforms[i] = drawshaders[i].FindUniform("room_fog_intensity");
		drawshader_room_fog_entry_map_enabled_uniforms[i] =
			drawshaders[i].FindUniform("room_fog_entry_map_enabled");
		drawshader_room_fog_entry_origin_uniforms[i] =
			drawshaders[i].FindUniform("room_fog_entry_origin");
		drawshader_room_fog_entry_size_uniforms[i] =
			drawshaders[i].FindUniform("room_fog_entry_size");
		drawshader_fog_composite_mode_uniforms[i] = drawshaders[i].FindUniform("fog_composite_mode");
	}

	lastdrawshader = -1;

	delete[] genericVertexBody;
	delete[] genericFragBody;

	//Init VAO and vertex state
	glGenVertexArrays(1, &drawvao);
	glBindVertexArray(drawvao);

	//Init draw buffers
	size_t buffersize = GL4DrawBufferByteSize();
	OpenGL_buffer_storage_enabled = OpenGL_buffer_storage_enabled &&
		glBufferStorage != nullptr && glFenceSync != nullptr &&
		glClientWaitSync != nullptr && glDeleteSync != nullptr;
	if (OpenGL_buffer_storage_enabled)
	{
		InitPersistentDrawBuffer(buffersize);
	}
	else
	{
		glGenBuffers(1, &drawbuffer);
		glBindBuffer(GL_ARRAY_BUFFER, drawbuffer);
		glBufferData(GL_ARRAY_BUFFER, buffersize, nullptr, GL_STREAM_DRAW);
		drawbuffer_vertex_capacity = (GLuint)(buffersize / sizeof(gl_vertex));

		GL4SetDrawVertexAttributes();
	}

	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

GLuint GL4Renderer::PrepareSoftParticleDepthTexture()
{
	if (!framebuffer_ok)
		return 0;

	Framebuffer& source = framebuffers[framebuffer_current_draw];
	if (source.Handle() == 0 || source.Width() == 0 || source.Height() == 0 ||
		!GL4DrawTargetIsFramebuffer(source.Handle()))
	{
		return 0;
	}

	if (soft_particle_depth_copy_valid &&
		soft_particle_depth_source_framebuffer == source.Handle())
	{
		return soft_particle_depth_texture;
	}

	GLint old_read = 0;
	GLint old_draw = 0;
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &old_read);
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &old_draw);

	// The scene framebuffer already owns a lazily resolved single-sample depth
	// texture.  Once resolved after opaque rendering it remains the immutable
	// soft-particle snapshot: late translucent depth writes deliberately do not
	// invalidate it.  Copying that texture into a second full-resolution depth
	// framebuffer here caused an avoidable pipeline flush and a second 4K depth
	// transfer in the middle of post-rendering.
	GLuint resolved_depth = source.DepthTextureForRead();

	glBindFramebuffer(GL_READ_FRAMEBUFFER, old_read);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, old_draw);

	soft_particle_depth_copy_valid = true;
	soft_particle_depth_source_framebuffer = source.Handle();
	soft_particle_depth_texture = resolved_depth;

	return resolved_depth;
}

void GL4Renderer::InvalidateSoftParticleDepthTexture()
{
	soft_particle_depth_copy_valid = false;
	soft_particle_depth_source_framebuffer = 0;
	soft_particle_depth_texture = 0;
}

void GL4Renderer::NotifyDepthBufferWrite()
{
	if (depth_snapshot_invalidation_enabled && framebuffer_ok &&
		OpenGL_state.cur_zbuffer_state != 0 && depth_write_enabled &&
		GL4DrawTargetIsFramebuffer(framebuffers[framebuffer_current_draw].Handle()))
	{
		framebuffers[framebuffer_current_draw].MarkDepthDirty();
		InvalidateSoftParticleDepthTexture();
	}
}

void GL4Renderer::SelectDrawShader()
{
	int shader_index = 0;

	const bool specular_shader = OpenGL_state.cur_alpha_type == AT_SPECULAR;

	if (OpenGL_state.cur_fog_state)
	{
		post_protection_mask_dirty = true;
		if (specular_shader)
			shader_index = 7;
		else if (OpenGL_state.cur_texture_quality == 0)
			shader_index = 4;
		else if (OpenGL_state.cur_texture_quality != 0)
		{
			if (Overlay_type != OT_NONE)
				shader_index = 6;
			else
				shader_index = 5;
		}
	}
	else
	{
		if (specular_shader)
			shader_index = 3;
		else if (OpenGL_state.cur_texture_quality == 0)
			shader_index = 0;
		else if (OpenGL_state.cur_texture_quality != 0)
		{
			if (Overlay_type != OT_NONE)
				shader_index = 2;
			else
				shader_index = 1;
		}
	}
	const bool shader_changed = shader_index != lastdrawshader;
	drawshaders[shader_index].Use();
	if (!shader_changed && !legacy_draw_uniforms_dirty && !CurrentDrawUsesPixelMotionTarget() &&
		!CurrentDrawWritesMotionObjectId() && !soft_particle_draw_enabled)
		return;

	if (drawshader_ao_suppression_uniforms[shader_index] != -1)
		glUniform1f(drawshader_ao_suppression_uniforms[shader_index], ao_suppression_draw_value);
	if (drawshader_bloom_suppression_uniforms[shader_index] != -1)
		glUniform1f(drawshader_bloom_suppression_uniforms[shader_index], bloom_suppression_draw_value);
	if (drawshader_ao_class_uniforms[shader_index] != -1)
		glUniform1i(drawshader_ao_class_uniforms[shader_index], ao_class_draw_value);
	if (drawshader_ao_weight_uniforms[shader_index] != -1)
		glUniform1f(drawshader_ao_weight_uniforms[shader_index], ao_weight_draw_value);
	if (drawshader_ao_capture_weight_mode_uniforms[shader_index] != -1)
		glUniform1i(drawshader_ao_capture_weight_mode_uniforms[shader_index], 0);
	int post_mask_blend_mode = 0;
	if (OpenGL_state.cur_alpha_type == AT_LIGHTMAP_BLEND ||
		OpenGL_state.cur_alpha_type == AT_LIGHTMAP_BLEND_VERTEX)
	{
		post_mask_blend_mode = 2;
	}
	else if (OpenGL_state.cur_alpha_type == AT_SATURATE_TEXTURE ||
		OpenGL_state.cur_alpha_type == AT_SATURATE_VERTEX ||
		OpenGL_state.cur_alpha_type == AT_SATURATE_CONSTANT_VERTEX ||
		OpenGL_state.cur_alpha_type == AT_SATURATE_TEXTURE_VERTEX ||
		OpenGL_state.cur_alpha_type == AT_LIGHTMAP_BLEND_SATURATE)
	{
		post_mask_blend_mode = 1;
	}
	if (drawshader_post_mask_blend_mode_uniforms[shader_index] != -1)
	{
		glUniform1i(drawshader_post_mask_blend_mode_uniforms[shader_index], post_mask_blend_mode);
	}
	if (drawshader_fast_additive_bitmap_uniforms[shader_index] != -1)
	{
		const bool fast_additive_bitmap =
			(shader_index == 1 || shader_index == 5) &&
			post_mask_blend_mode == 1 &&
			!retained_draw_active && !room_fog_overlay &&
			OpenGL_state.cur_light_state != LS_PHONG &&
			cockpit_backing_effect.enabled == 0 &&
			ao_suppression_draw_value == 0.0f && bloom_suppression_draw_value == 0.0f &&
			!CurrentDrawWritesPixelMotionVectors() && !CurrentDrawWritesMotionObjectId();
		glUniform1i(drawshader_fast_additive_bitmap_uniforms[shader_index],
			fast_additive_bitmap ? 1 : 0);
	}
	if (drawshader_fast_retained_room_base_uniforms[shader_index] != -1)
		glUniform1i(drawshader_fast_retained_room_base_uniforms[shader_index], 0);
	if (drawshader_retained_room_lightmap_arrays_uniforms[shader_index] != -1)
		glUniform1i(drawshader_retained_room_lightmap_arrays_uniforms[shader_index], 0);
	if (drawshader_retained_dynamic_lightmaps_uniforms[shader_index] != -1)
		glUniform1i(drawshader_retained_dynamic_lightmaps_uniforms[shader_index], 0);
	if (drawshader_cockpit_backing_enabled_uniforms[shader_index] != -1)
		glUniform1i(drawshader_cockpit_backing_enabled_uniforms[shader_index], cockpit_backing_effect.enabled);
	if (drawshader_cockpit_backing_alpha_uniforms[shader_index] != -1)
		glUniform1f(drawshader_cockpit_backing_alpha_uniforms[shader_index], cockpit_backing_effect.alpha);
	if (drawshader_cockpit_backing_darkness_uniforms[shader_index] != -1)
		glUniform1f(drawshader_cockpit_backing_darkness_uniforms[shader_index], cockpit_backing_effect.darkness);
	if (drawshader_cockpit_scanlines_enabled_uniforms[shader_index] != -1)
		glUniform1i(drawshader_cockpit_scanlines_enabled_uniforms[shader_index], cockpit_backing_effect.scanlines_enabled);
	if (drawshader_cockpit_scanline_strength_uniforms[shader_index] != -1)
		glUniform1f(drawshader_cockpit_scanline_strength_uniforms[shader_index], cockpit_backing_effect.scanline_strength);
	if (drawshader_cockpit_scanline_spacing_uniforms[shader_index] != -1)
		glUniform1f(drawshader_cockpit_scanline_spacing_uniforms[shader_index], cockpit_backing_effect.scanline_spacing);
	if (drawshader_cockpit_scanline_thickness_uniforms[shader_index] != -1)
		glUniform1f(drawshader_cockpit_scanline_thickness_uniforms[shader_index], cockpit_backing_effect.scanline_thickness);
	if (drawshader_cockpit_scanline_phase_uniforms[shader_index] != -1)
		glUniform1f(drawshader_cockpit_scanline_phase_uniforms[shader_index], cockpit_backing_effect.scanline_phase);
	if (drawshader_motion_vector_mode_uniforms[shader_index] != -1)
		glUniform1i(drawshader_motion_vector_mode_uniforms[shader_index],
			CurrentDrawWritesPixelMotionVectors() ? RENDERER_MOTION_VECTOR_PIXEL : RENDERER_MOTION_VECTOR_OFF);
	if (drawshader_motion_vector_current_view_projection_uniforms[shader_index] != -1)
		glUniformMatrix4fv(drawshader_motion_vector_current_view_projection_uniforms[shader_index], 1, GL_FALSE, current_view_projection);
	if (drawshader_motion_vector_previous_view_projection_uniforms[shader_index] != -1)
		glUniformMatrix4fv(drawshader_motion_vector_previous_view_projection_uniforms[shader_index], 1, GL_FALSE, previous_view_projection);
	const bool motion_has_previous_view_projection = cockpit_motion_object_active ?
		have_cockpit_previous_view_projection : have_previous_view_projection;
	if (drawshader_motion_vector_has_previous_uniforms[shader_index] != -1)
		glUniform1i(drawshader_motion_vector_has_previous_uniforms[shader_index],
			motion_has_previous_view_projection ? 1 : 0);
	if (drawshader_motion_vector_payload_type_uniforms[shader_index] != -1)
		glUniform1i(drawshader_motion_vector_payload_type_uniforms[shader_index],
			cockpit_motion_object_active ? 1 : 0);
	if (drawshader_motion_vector_object_id_uniforms[shader_index] != -1)
		glUniform1ui(drawshader_motion_vector_object_id_uniforms[shader_index],
			CurrentDrawWritesMotionObjectId() ? motion_object_id : 0u);
	if (drawshader_soft_particle_enabled_uniforms[shader_index] != -1)
	{
		GLuint soft_depth = soft_particle_draw_enabled ? PrepareSoftParticleDepthTexture() : 0;
		glUniform1i(drawshader_soft_particle_enabled_uniforms[shader_index],
			soft_depth != 0 ? 1 : 0);
		if (soft_depth != 0)
		{
			if (drawshader_soft_particle_screen_size_uniforms[shader_index] != -1)
				glUniform2f(drawshader_soft_particle_screen_size_uniforms[shader_index],
					(float)framebuffers[framebuffer_current_draw].Width(),
					(float)framebuffers[framebuffer_current_draw].Height());
			if (drawshader_soft_particle_depth_range_uniforms[shader_index] != -1)
				glUniform1f(drawshader_soft_particle_depth_range_uniforms[shader_index],
					soft_particle_depth_range);
			GL_BindFramebufferTexture(soft_depth, 2, GL_NEAREST);
			glActiveTexture(GL_TEXTURE0);
			Last_texel_unit_set = 0;
		}
	}
	if (drawshader_room_fog_enabled_uniforms[shader_index] != -1)
	{
		glUniform1i(drawshader_room_fog_enabled_uniforms[shader_index], room_fog_enabled ? 1 : 0);
		glUniform1i(drawshader_room_fog_viewer_inside_uniforms[shader_index], room_fog_viewer_inside ? 1 : 0);
		glUniform3fv(drawshader_room_fog_viewer_position_uniforms[shader_index], 1, room_fog_viewer_position);
		glUniform3fv(drawshader_room_fog_viewer_forward_uniforms[shader_index], 1, room_fog_viewer_forward);
		glUniform3fv(drawshader_room_fog_color_uniforms[shader_index], 1, room_fog_color);
		glUniform1f(drawshader_room_fog_depth_uniforms[shader_index], room_fog_depth);
		glUniform1f(drawshader_room_fog_intensity_uniforms[shader_index], room_fog_intensity);
		if (drawshader_room_fog_entry_map_enabled_uniforms[shader_index] != -1)
		{
			glUniform1i(drawshader_room_fog_entry_map_enabled_uniforms[shader_index],
				room_fog_entry_map_enabled ? 1 : 0);
			if (drawshader_room_fog_entry_origin_uniforms[shader_index] != -1)
				glUniform2iv(drawshader_room_fog_entry_origin_uniforms[shader_index], 1,
					room_fog_entry_origin);
			if (drawshader_room_fog_entry_size_uniforms[shader_index] != -1)
				glUniform2iv(drawshader_room_fog_entry_size_uniforms[shader_index], 1,
					room_fog_entry_size);
			if (room_fog_entry_map_enabled && room_fog_entry_texture != 0)
			{
				GL_BindFramebufferTexture(room_fog_entry_texture, 10, GL_NEAREST);
				glActiveTexture(GL_TEXTURE0);
				Last_texel_unit_set = 0;
			}
		}
		int fog_composite_mode = 0;
		if (room_fog_overlay)
		{
			fog_composite_mode = 2;
		}
		else
		{
			switch (OpenGL_state.cur_alpha_type)
			{
			case AT_SPECULAR:
			case AT_SATURATE_TEXTURE:
			case AT_SATURATE_VERTEX:
			case AT_SATURATE_CONSTANT_VERTEX:
			case AT_SATURATE_TEXTURE_VERTEX:
			case AT_LIGHTMAP_BLEND_SATURATE:
				fog_composite_mode = 1;
				break;
			case AT_LIGHTMAP_BLEND:
			case AT_LIGHTMAP_BLEND_VERTEX:
				fog_composite_mode = 3;
				break;
			default:
				break;
			}
		}
		glUniform1i(drawshader_fog_composite_mode_uniforms[shader_index],
			fog_composite_mode);
	}

	const bool phong_enabled = OpenGL_state.cur_light_state == LS_PHONG;
	if (drawshader_phong_enabled_uniforms[shader_index] != -1)
		glUniform1i(drawshader_phong_enabled_uniforms[shader_index], phong_enabled ? 1 : 0);
	if (phong_enabled && drawshader_light_direction_uniforms[shader_index] != -1)
		glUniform3f(drawshader_light_direction_uniforms[shader_index], per_pixel_light_direction.x,
			per_pixel_light_direction.y, per_pixel_light_direction.z);
	if (drawshader_per_pixel_specular_enabled_uniforms[shader_index] != -1)
	{
		const int shader_specular_mode =
			OpenGL_state.cur_alpha_type == AT_SPECULAR && OpenGL_preferred_state.per_pixel_lighting;
		glUniform1i(drawshader_per_pixel_specular_enabled_uniforms[shader_index],
			shader_specular_mode);
	}

	drawshaders[shader_index].ApplyDynamicLighting(per_pixel_dynamic_light_count,
		&per_pixel_dynamic_face_normal.x, &per_pixel_dynamic_positions[0][0],
		&per_pixel_dynamic_colors[0][0], per_pixel_dynamic_radii,
		&per_pixel_dynamic_specular_positions[0][0], per_pixel_dynamic_specular_radii,
		per_pixel_dynamic_specular_scalars, per_pixel_dynamic_falloffs,
		&per_pixel_dynamic_directions[0][0], per_pixel_dynamic_dot_ranges,
		per_pixel_dynamic_directional);

	lastdrawshader = shader_index;
	legacy_draw_uniforms_dirty = false;
}

// Takes nv vertices and draws the 3D polygon defined by those vertices.
// Uses bitmap "handle" as a texture
void GL4Renderer::DrawPolygon3D(int handle, g3Point** p, int nv, int map_type)
{
	g3Point* pnt;
	int i;
	ubyte fr, fg, fb;

	ASSERT(nv < 100);

	float one_over_square_res = 1;
	float xscalar = 1;
	float yscalar = 1;

	SelectDrawShader();

	if (OpenGL_state.cur_light_state == LS_FLAT_GOURAUD || OpenGL_state.cur_texture_type == 0)
	{
		fr = GR_COLOR_RED(OpenGL_state.cur_color);
		fg = GR_COLOR_GREEN(OpenGL_state.cur_color);
		fb = GR_COLOR_BLUE(OpenGL_state.cur_color);
	}

	if (UseMultitexture)
	{
		SetMultitextureBlendMode(false);
	}

	if (OpenGL_state.cur_texture_quality != 0)
	{
		// make sure our bitmap is ready to be drawn
		MakeBitmapCurrent(handle, map_type, 0);
		MakeWrapTypeCurrent(handle, map_type, 0);
		MakeFilterTypeCurrent(handle, map_type, 0);

		if (Overlay_type != OT_NONE)
		{
			one_over_square_res = 1.0 / GameLightmaps[Overlay_map].square_res;
			xscalar = (float)GameLightmaps[Overlay_map].width * one_over_square_res;
			yscalar = (float)GameLightmaps[Overlay_map].height * one_over_square_res;
			// make sure our bitmap is ready to be drawn
			MakeBitmapCurrent(Overlay_map, MAP_TYPE_LIGHTMAP, 1);
			MakeWrapTypeCurrent(Overlay_map, MAP_TYPE_LIGHTMAP, 1);
			MakeFilterTypeCurrent(Overlay_map, MAP_TYPE_LIGHTMAP, 1);
		}

	}

	float alpha = Alpha_multiplier * OpenGL_Alpha_factor;
	const bool per_pixel_specular_draw = OpenGL_state.cur_alpha_type == AT_SPECULAR &&
		OpenGL_preferred_state.per_pixel_lighting;

	gl_vertex* vertp = GL_vertices;

	// Specify our coordinates
	for (i = 0; i < nv; i++, vertp++)
	{
		pnt = p[i];

		vertp->normal.x = 0.0f;
		vertp->normal.y = 0.0f;
		vertp->normal.z = 0.0f;
		vertp->normal.w = -1.0f;

		if (OpenGL_state.cur_alpha_type & ATF_VERTEX)
		{
			alpha = pnt->p3_a * Alpha_multiplier * OpenGL_Alpha_factor;
		}

		// If we have a lighting model, apply the correct lighting!
		if (OpenGL_state.cur_light_state != LS_NONE)
		{
			if (OpenGL_state.cur_light_state == LS_FLAT_GOURAUD)
			{
				vertp->color.r = fr;
				vertp->color.g = fg;
				vertp->color.b = fb;
				vertp->color.a = (ubyte)alpha;
			}
			else
			{
				// Do lighting based on intesity (MONO) or colored (RGB)
				if (OpenGL_state.cur_color_model == CM_MONO)
				{
					vertp->color.r = pnt->p3_l * 255;
					vertp->color.g = pnt->p3_l * 255;
					vertp->color.b = pnt->p3_l * 255;
					vertp->color.a = (ubyte)alpha;
				}
				else
				{
					vertp->color.r = pnt->p3_r * 255;
					vertp->color.g = pnt->p3_g * 255;
					vertp->color.b = pnt->p3_b * 255;
					vertp->color.a = (ubyte)alpha;
				}
			}
		}
		else
		{
			if (OpenGL_state.cur_texture_type != 0)
			{
				vertp->color.r = 255;
				vertp->color.g = 255;
				vertp->color.b = 255;
				vertp->color.a = (ubyte)alpha;
			}
			else
			{
				vertp->color.r = fr;
				vertp->color.g = fg;
				vertp->color.b = fb;
				vertp->color.a = (ubyte)alpha;
			}
		}

		if (OpenGL_state.cur_texture_type != 0)
		{
			// Texture this polygon!
			float texw = 1.0 / (pnt->p3_z + Z_bias);
			vertp->tex_coord.s = pnt->p3_u * texw;
			vertp->tex_coord.t = pnt->p3_v * texw;
			vertp->tex_coord.w = texw;

			if (Overlay_type != OT_NONE)
			{
				vertp->tex_coord2.s = pnt->p3_u2 * xscalar * texw;
				vertp->tex_coord2.t = pnt->p3_v2 * yscalar * texw;
				vertp->tex_coord2.w = texw;
			}
		}

		if (per_pixel_specular_draw)
		{
			vector specular_normal = GL4ViewSpaceSpecularNormal(pnt, per_pixel_dynamic_face_normal);
			vertp->normal.x = specular_normal.x;
			vertp->normal.y = specular_normal.y;
			vertp->normal.z = specular_normal.z;
			vertp->normal.w = 1.0f;
		}
		else if (OpenGL_state.cur_light_state == LS_PHONG || per_pixel_dynamic_light_count > 0)
		{
			float payloadw = 1.0f / (pnt->p3_z + Z_bias);
			vertp->normal.x = pnt->p3_vecPreRot.x * payloadw;
			vertp->normal.y = pnt->p3_vecPreRot.y * payloadw;
			vertp->normal.z = pnt->p3_vecPreRot.z * payloadw;
			vertp->normal.w = payloadw;
		}

		if (soft_particle_draw_enabled)
			vertp->normal.w = GL4SoftParticleDepthFromPoint(pnt);

		// Finally, specify a vertex
		vertp->vert.x = pnt->p3_sx;
		vertp->vert.y = pnt->p3_sy;
		vertp->motion_world_position.x = 0.0f;
		vertp->motion_world_position.y = 0.0f;
		vertp->motion_world_position.z = 0.0f;
		vertp->motion_world_position.w = 0.0f;
		vertp->motion_previous_world_position.x = 0.0f;
		vertp->motion_previous_world_position.y = 0.0f;
		vertp->motion_previous_world_position.z = 0.0f;
		vertp->motion_previous_world_position.w = 0.0f;
		if (per_pixel_specular_draw)
		{
			float payloadw = 1.0f / (pnt->p3_z + Z_bias);
			vector view_position = GL4ViewSpacePositionFromPoint(pnt);
			vertp->motion_world_position.x = view_position.x * payloadw;
			vertp->motion_world_position.y = view_position.y * payloadw;
			vertp->motion_world_position.z = view_position.z * payloadw;
			vertp->motion_world_position.w = payloadw;
		}
		else if (CurrentDrawWritesPixelMotionVectors() && !motion_object_active)
		{
			float payloadw = 1.0f / (pnt->p3_z + Z_bias);
			vertp->motion_world_position.x = pnt->p3_vecPreRot.x * payloadw;
			vertp->motion_world_position.y = pnt->p3_vecPreRot.y * payloadw;
			vertp->motion_world_position.z = pnt->p3_vecPreRot.z * payloadw;
			vertp->motion_world_position.w = payloadw;
		}
		else if (CurrentDrawWritesPixelMotionVectors() && cockpit_motion_object_active &&
			pnt->p3_motion_world_valid && pnt->p3_motion_prev_world_valid &&
			have_cockpit_previous_view_projection)
		{
			vec4_array current_clip = {};
			vec4_array previous_clip = {};
			GL4TransformWorldToClip(current_view_projection, pnt->p3_motion_world_pos, current_clip);
			GL4TransformWorldToClip(cockpit_previous_view_projection, pnt->p3_motion_prev_world_pos, previous_clip);
			if (current_clip.w > 0.00001f && previous_clip.w > 0.00001f)
			{
				float payloadw = 1.0f / (pnt->p3_z + Z_bias);
				vertp->motion_world_position.x = (current_clip.x / current_clip.w) * payloadw;
				vertp->motion_world_position.y = (current_clip.y / current_clip.w) * payloadw;
				vertp->motion_world_position.z = 0.0f;
				vertp->motion_world_position.w = payloadw;
				vertp->motion_previous_world_position.x = (previous_clip.x / previous_clip.w) * payloadw;
				vertp->motion_previous_world_position.y = (previous_clip.y / previous_clip.w) * payloadw;
				vertp->motion_previous_world_position.z = 0.0f;
				vertp->motion_previous_world_position.w = payloadw;
			}
		}
		else if (CurrentDrawWritesPixelMotionVectors() && motion_object_active &&
			pnt->p3_motion_world_valid)
		{
			float payloadw = 1.0f / (pnt->p3_z + Z_bias);
			vertp->motion_world_position.x = pnt->p3_motion_world_pos.x * payloadw;
			vertp->motion_world_position.y = pnt->p3_motion_world_pos.y * payloadw;
			vertp->motion_world_position.z = pnt->p3_motion_world_pos.z * payloadw;
			vertp->motion_world_position.w = payloadw;
			if (pnt->p3_motion_prev_world_valid)
			{
				vertp->motion_previous_world_position.x = pnt->p3_motion_prev_world_pos.x * payloadw;
				vertp->motion_previous_world_position.y = pnt->p3_motion_prev_world_pos.y * payloadw;
				vertp->motion_previous_world_position.z = pnt->p3_motion_prev_world_pos.z * payloadw;
				vertp->motion_previous_world_position.w = payloadw;
			}
		}
		if (room_fog_enabled)
		{
			float payloadw = 1.0f / (pnt->p3_z + Z_bias);
			vertp->motion_previous_world_position.x = pnt->p3_vecPreRot.x * payloadw;
			vertp->motion_previous_world_position.y = pnt->p3_vecPreRot.y * payloadw;
			vertp->motion_previous_world_position.z = pnt->p3_vecPreRot.z * payloadw;
			vertp->motion_previous_world_position.w = payloadw;
		}

		float z = GL4DepthFromEyeZ(pnt->p3_z + Z_bias);
		vertp->vert.z = -z;
		GL4SetFieldSpecularVertexPayload(*vertp, pnt, per_pixel_specular_draw);
	}

	// And draw!
	int offset = CopyVertices(nv);
	const bool drawing_to_scene = framebuffer_ok &&
		GL4DrawTargetIsFramebuffer(framebuffers[framebuffer_current_draw].Handle());
	const bool include_motion_vectors = CurrentDrawUsesPixelMotionTarget();
	const bool include_motion_object_ids = CurrentDrawWritesMotionObjectId();
	const bool force_motion_vector_draw_buffer = CurrentDrawIsLateCockpitPixelMotionVectorDraw();
	const bool include_ao_class = !cockpit_scene_frame_active &&
		OpenGL_state.cur_zbuffer_state != 0 && depth_write_enabled;
	const bool include_post_mask = CurrentDrawNeedsPostMask(include_ao_class);
	const bool override_draw_buffers = drawing_to_scene &&
		(cockpit_scene_frame_active || force_motion_vector_draw_buffer ||
		 PixelMotionVectorModeEnabled() || !include_post_mask || !include_ao_class);
	if (override_draw_buffers)
	{
		if (cockpit_scene_frame_active)
			GL4UseSceneColorDrawBuffer();
		else
			GL4UseSceneDrawBuffersForCurrentDraw(include_motion_vectors, include_post_mask,
				include_ao_class,
				include_motion_object_ids);
	}
	const bool exact_room_fog_multiply = drawing_to_scene && UsesExactRoomFogMultiply();
	if (exact_room_fog_multiply)
		SetCurrentFogCompositeMode(4);
	rend_RecordDrawCall(draw_call_category);
	glDrawArrays(GL_TRIANGLE_FAN, offset, nv);
	if (exact_room_fog_multiply)
	{
		DrawRoomFogMultiplyCorrection(GL_TRIANGLE_FAN, offset, nv);
		if (override_draw_buffers)
		{
			if (cockpit_scene_frame_active)
				GL4UseSceneColorDrawBuffer();
			else
				GL4UseSceneDrawBuffersForCurrentDraw(include_motion_vectors,
					include_post_mask, include_ao_class, include_motion_object_ids);
		}
		else
		{
			UseSceneDrawBuffers();
		}
	}
	NotifyDepthBufferWrite();
	if (include_motion_vectors || include_motion_object_ids)
	{
		motion_vectors_dirty = true;
		motion_vectors.MarkDirty();
	}
	if (override_draw_buffers)
	{
		if (cockpit_scene_frame_active)
			GL4UseSceneColorDrawBuffer();
		else
			UseSceneDrawBuffers();
	}
	OpenGL_polys_drawn++;
	OpenGL_verts_processed += nv;

	CHECK_ERROR(10);
}

void GL4Renderer::DrawPolygon3DBatch(int handle, const renderer_poly_batch_item *items, int count, int map_type)
{
	if (!items || count <= 0)
		return;

	ubyte fr = 0, fg = 0, fb = 0;
	float one_over_square_res = 1;
	float xscalar = 1;
	float yscalar = 1;

	SelectDrawShader();

	if (OpenGL_state.cur_light_state == LS_FLAT_GOURAUD || OpenGL_state.cur_texture_type == 0)
	{
		fr = GR_COLOR_RED(OpenGL_state.cur_color);
		fg = GR_COLOR_GREEN(OpenGL_state.cur_color);
		fb = GR_COLOR_BLUE(OpenGL_state.cur_color);
	}

	if (UseMultitexture)
	{
		SetMultitextureBlendMode(false);
	}

	if (OpenGL_state.cur_texture_quality != 0)
	{
		MakeBitmapCurrent(handle, map_type, 0);
		MakeWrapTypeCurrent(handle, map_type, 0);
		MakeFilterTypeCurrent(handle, map_type, 0);

		if (Overlay_type != OT_NONE)
		{
			one_over_square_res = 1.0f / GameLightmaps[Overlay_map].square_res;
			xscalar = (float)GameLightmaps[Overlay_map].width * one_over_square_res;
			yscalar = (float)GameLightmaps[Overlay_map].height * one_over_square_res;
			MakeBitmapCurrent(Overlay_map, MAP_TYPE_LIGHTMAP, 1);
			MakeWrapTypeCurrent(Overlay_map, MAP_TYPE_LIGHTMAP, 1);
			MakeFilterTypeCurrent(Overlay_map, MAP_TYPE_LIGHTMAP, 1);
		}

	}

	int triangle_vertices = 0;
	int original_vertices = 0;
	for (int i = 0; i < count; i++)
	{
		if (items[i].nv >= 3 && items[i].nv < 100)
		{
			triangle_vertices += (items[i].nv - 2) * 3;
			original_vertices += items[i].nv;
		}
	}

	if (triangle_vertices <= 0)
		return;

	static std::vector<gl_vertex> vertices;
	vertices.clear();
	if (vertices.capacity() < (size_t)triangle_vertices)
		vertices.reserve(triangle_vertices);

	int polygons_drawn = 0;
	for (int i = 0; i < count; i++)
	{
		const renderer_poly_batch_item& item = items[i];
		if (item.nv < 3 || item.nv >= 100)
			continue;

		gl_vertex face_vertices[100];
		for (int v = 0; v < item.nv; v++)
			BuildDrawVertex(face_vertices[v], item.pointlist[v], xscalar, yscalar, fr, fg, fb);

		for (int v = 0; v < item.nv - 2; v++)
		{
			const int indices[3] = {0, v + 1, v + 2};
			for (int corner = 0; corner < 3; corner++)
				vertices.push_back(face_vertices[indices[corner]]);
		}
		polygons_drawn++;
	}

	if (vertices.empty())
		return;

	int offset = CopyVertices(vertices.data(), (int)vertices.size());
	const bool drawing_to_scene = framebuffer_ok &&
		GL4DrawTargetIsFramebuffer(framebuffers[framebuffer_current_draw].Handle());
	const bool include_motion_vectors = CurrentDrawUsesPixelMotionTarget();
	const bool include_motion_object_ids = CurrentDrawWritesMotionObjectId();
	const bool force_motion_vector_draw_buffer = CurrentDrawIsLateCockpitPixelMotionVectorDraw();
	const bool include_ao_class = !cockpit_scene_frame_active &&
		OpenGL_state.cur_zbuffer_state != 0 && depth_write_enabled;
	const bool include_post_mask = CurrentDrawNeedsPostMask(include_ao_class);
	const bool override_draw_buffers = drawing_to_scene &&
		(cockpit_scene_frame_active || force_motion_vector_draw_buffer ||
		 PixelMotionVectorModeEnabled() || !include_post_mask || !include_ao_class);
	if (override_draw_buffers)
	{
		if (cockpit_scene_frame_active)
			GL4UseSceneColorDrawBuffer();
		else
			GL4UseSceneDrawBuffersForCurrentDraw(include_motion_vectors, include_post_mask,
				include_ao_class,
				include_motion_object_ids);
	}
	const bool exact_room_fog_multiply = drawing_to_scene && UsesExactRoomFogMultiply();
	if (exact_room_fog_multiply)
		SetCurrentFogCompositeMode(4);
	rend_RecordDrawCall(draw_call_category);
	glDrawArrays(GL_TRIANGLES, offset, (GLsizei)vertices.size());
	if (exact_room_fog_multiply)
	{
		DrawRoomFogMultiplyCorrection(GL_TRIANGLES, offset,
			(GLsizei)vertices.size());
		if (override_draw_buffers)
		{
			if (cockpit_scene_frame_active)
				GL4UseSceneColorDrawBuffer();
			else
				GL4UseSceneDrawBuffersForCurrentDraw(include_motion_vectors,
					include_post_mask, include_ao_class, include_motion_object_ids);
		}
		else
		{
			UseSceneDrawBuffers();
		}
	}
	NotifyDepthBufferWrite();
	if (include_motion_vectors || include_motion_object_ids)
	{
		motion_vectors_dirty = true;
		motion_vectors.MarkDirty();
	}
	if (override_draw_buffers)
	{
		if (cockpit_scene_frame_active)
			GL4UseSceneColorDrawBuffer();
		else
			UseSceneDrawBuffers();
	}
	OpenGL_polys_drawn += polygons_drawn;
	OpenGL_verts_processed += original_vertices;

	CHECK_ERROR(10);
}

bool GL4Renderer::DrawWeatherQuadBatch(int handle, const renderer_weather_quad *items, int count, int map_type)
{
	if (!items || count <= 0 || OpenGL_state.cur_texture_quality == 0)
		return false;

	SelectDrawShader();
	MakeBitmapCurrent(handle, map_type, 0);
	MakeWrapTypeCurrent(handle, map_type, 0);
	MakeFilterTypeCurrent(handle, map_type, 0);

	static std::vector<gl_vertex> vertices;
	vertices.clear();
	if (vertices.capacity() < (size_t)count * 6)
		vertices.reserve((size_t)count * 6);

	const auto transform_to_view = [](const vector& world) {
		vector view = world - View_position;
		return view * Unscaled_matrix;
	};

	const auto append_vertex = [&](const vector& view, float u, float v,
		const renderer_weather_quad& item) {
		const float z_for_payload = std::max(view.z + Z_bias + item.depth_bias, 0.0001f);
		const float texw = 1.0f / z_for_payload;
		gl_vertex vertex = {};
		vertex.vert.x = Window_cx + view.x * (Window_w2 / view.z);
		vertex.vert.y = Window_cy - view.y * (Window_h2 / view.z);
		vertex.vert.z = -GL4DepthFromEyeZ(z_for_payload);
		vertex.color.r = (ubyte)std::max(0.0f, std::min(255.0f, item.r * 255.0f));
		vertex.color.g = (ubyte)std::max(0.0f, std::min(255.0f, item.g * 255.0f));
		vertex.color.b = (ubyte)std::max(0.0f, std::min(255.0f, item.b * 255.0f));
		vertex.color.a = (ubyte)std::max(0.0f,
			std::min(255.0f, item.a * Alpha_multiplier * OpenGL_Alpha_factor));
		vertex.tex_coord.s = u * texw;
		vertex.tex_coord.t = v * texw;
		vertex.tex_coord.w = texw;
		if (soft_particle_draw_enabled)
			vertex.normal.w = GL4DepthFromEyeZ(view.z + Z_bias + item.depth_bias);
		vertices.push_back(vertex);
	};

	for (int i = 0; i < count; i++)
	{
		const renderer_weather_quad& item = items[i];
		const vector item_pos = { item.pos[0], item.pos[1], item.pos[2] };
		if (item.a <= 0.0f || item.width <= 0.0f || item.height <= 0.0f)
			continue;

		vector corners[4];
		if (item.planar)
		{
			vector normal = { item.plane_normal[0], item.plane_normal[1], item.plane_normal[2] };
			if (vm_NormalizeVectorFast(&normal) <= 0.0f)
				normal = { 0.0f, 1.0f, 0.0f };
			matrix plane_matrix;
			vm_VectorToMatrix(&plane_matrix, &normal, NULL, NULL);
			vm_TransposeMatrix(&plane_matrix);
			const float c = cosf(item.rotation);
			const float s = sinf(item.rotation);
			vector local[4];
			local[0] = { -item.width * c - item.height * s, item.width * s - item.height * c, 0.0f };
			local[1] = { item.width * c - item.height * s, -item.width * s - item.height * c, 0.0f };
			local[2] = { item.width * c + item.height * s, -item.width * s + item.height * c, 0.0f };
			local[3] = { -item.width * c + item.height * s, item.width * s + item.height * c, 0.0f };
			for (int c = 0; c < 4; c++)
			{
				vector world_offset;
				vm_MatrixMulVector(&world_offset, &local[c], &plane_matrix);
				corners[c] = transform_to_view(item_pos + world_offset);
				if (item.legacy_g3_projection)
				{
					corners[c].x *= Matrix_scale.x;
					corners[c].y *= Matrix_scale.y;
				}
			}
		}
		else
		{
			vector center = transform_to_view(item_pos);
			if (item.legacy_g3_projection)
			{
				center.x *= Matrix_scale.x;
				center.y *= Matrix_scale.y;
			}
			if (center.z <= 0.0001f)
				continue;
			const float radius = std::max(item.width, item.height) * 1.5f;
			if (center.z > Detail_settings.Terrain_render_distance * Matrix_scale.z ||
				fabsf(center.x) - radius > center.z || fabsf(center.y) - radius > center.z)
			{
				continue;
			}

			const float c = cosf(item.rotation);
			const float s = sinf(item.rotation);
			const vector local[4] = {
				{ -item.width * c - item.height * s, item.width * s - item.height * c, 0.0f },
				{ item.width * c - item.height * s, -item.width * s - item.height * c, 0.0f },
				{ item.width * c + item.height * s, -item.width * s + item.height * c, 0.0f },
				{ -item.width * c + item.height * s, item.width * s + item.height * c, 0.0f }
			};
			for (int cidx = 0; cidx < 4; cidx++)
			{
				corners[cidx] = center;
				corners[cidx].x += local[cidx].x * Matrix_scale.x;
				corners[cidx].y += local[cidx].y * Matrix_scale.y;
			}
		}

		bool reject = false;
		for (int c = 0; c < 4; c++)
		{
			if (corners[c].z <= 0.0001f)
			{
				reject = true;
				break;
			}
		}
		if (reject)
			continue;

		const float u[4] = { item.u0, item.u1, item.u1, item.u0 };
		const float v[4] = { item.v0, item.v0, item.v1, item.v1 };
		const int indices[6] = { 0, 1, 2, 0, 2, 3 };
		for (int t = 0; t < 6; t++)
		{
			const int idx = indices[t];
			append_vertex(corners[idx], u[idx], v[idx], item);
		}
	}

	if (vertices.empty())
		return true;

	const int offset = CopyVertices(vertices.data(), (int)vertices.size());
	const bool drawing_to_scene = framebuffer_ok &&
		GL4DrawTargetIsFramebuffer(framebuffers[framebuffer_current_draw].Handle());
	const bool include_motion_vectors = CurrentDrawUsesPixelMotionTarget();
	const bool include_motion_object_ids = CurrentDrawWritesMotionObjectId();
	const bool include_ao_class = !cockpit_scene_frame_active &&
		OpenGL_state.cur_zbuffer_state != 0 && depth_write_enabled;
	const bool include_post_mask = CurrentDrawNeedsPostMask(include_ao_class);
	const bool override_draw_buffers = drawing_to_scene &&
		(PixelMotionVectorModeEnabled() || !include_post_mask || !include_ao_class);
	if (override_draw_buffers)
		GL4UseSceneDrawBuffersForCurrentDraw(include_motion_vectors, include_post_mask,
			include_ao_class, include_motion_object_ids);
	rend_RecordDrawCall(draw_call_category);
	glDrawArrays(GL_TRIANGLES, offset, (GLsizei)vertices.size());
	NotifyDepthBufferWrite();
	if (include_motion_vectors || include_motion_object_ids)
	{
		motion_vectors_dirty = true;
		motion_vectors.MarkDirty();
	}
	if (override_draw_buffers)
		UseSceneDrawBuffers();
	OpenGL_polys_drawn += (int)vertices.size() / 6;
	OpenGL_verts_processed += (int)vertices.size();

	CHECK_ERROR(12);
	return true;
}

bool GL4Renderer::BeginRetainedPolymodelDraw(const renderer_retained_polymodel_draw *draw)
{
	if (!draw || retained_draw_active)
		return false;

	SelectDrawShader();
	const int shader_index = lastdrawshader;
	if (shader_index < 0 || drawshader_retained_mode_uniforms[shader_index] == -1)
		return false;

	float base_color[3] = { draw->base_color[0], draw->base_color[1], draw->base_color[2] };
	if (OpenGL_state.cur_light_state == LS_FLAT_GOURAUD || OpenGL_state.cur_texture_type == TT_FLAT)
	{
		base_color[0] = GR_COLOR_RED(OpenGL_state.cur_color) / 255.0f;
		base_color[1] = GR_COLOR_GREEN(OpenGL_state.cur_color) / 255.0f;
		base_color[2] = GR_COLOR_BLUE(OpenGL_state.cur_color) / 255.0f;
	}
	else if (OpenGL_state.cur_light_state == LS_NONE)
	{
		base_color[0] = base_color[1] = base_color[2] = 1.0f;
	}

	int lighting_mode = 0;
	if (OpenGL_state.cur_light_state == LS_GOURAUD)
		lighting_mode = 1;
	else if (OpenGL_state.cur_light_state == LS_PHONG)
		lighting_mode = 2;
	if (draw->lighting_mode_override >= 0)
		lighting_mode = draw->lighting_mode_override;

	glUniform1i(drawshader_retained_mode_uniforms[shader_index], 1);
	glUniformMatrix4fv(drawshader_retained_transform_uniforms[shader_index], 1, GL_FALSE, draw->transform);
	glUniformMatrix4fv(drawshader_retained_modelview_uniforms[shader_index], 1, GL_FALSE, draw->modelview);
	glUniformMatrix4fv(drawshader_retained_current_world_uniforms[shader_index], 1, GL_FALSE, draw->current_world);
	glUniformMatrix4fv(drawshader_retained_previous_world_uniforms[shader_index], 1, GL_FALSE, draw->previous_world);
	glUniform2f(drawshader_retained_uv_offset_uniforms[shader_index], draw->u_offset, draw->v_offset);
	glUniform2fv(drawshader_retained_uv2_scale_uniforms[shader_index], 1, draw->uv2_scale);
	glUniform3fv(drawshader_retained_base_color_uniforms[shader_index], 1, base_color);
	glUniform1f(drawshader_retained_depth_bias_uniforms[shader_index], draw->depth_bias);
	glUniform1i(drawshader_retained_legacy_depth_uniforms[shader_index], draw->legacy_depth ? 1 : 0);
	glUniform1i(drawshader_retained_legacy_world_projection_uniforms[shader_index],
		draw->legacy_world_projection ? 1 : 0);
	if (draw->legacy_world_projection)
	{
		glUniform3fv(drawshader_retained_legacy_view_position_uniforms[shader_index], 1,
			draw->legacy_view_position);
		glUniform3fv(drawshader_retained_legacy_view_right_uniforms[shader_index], 1,
			draw->legacy_view_right);
		glUniform3fv(drawshader_retained_legacy_view_up_uniforms[shader_index], 1,
			draw->legacy_view_up);
		glUniform3fv(drawshader_retained_legacy_view_forward_uniforms[shader_index], 1,
			draw->legacy_view_forward);
		glUniform2fv(drawshader_retained_legacy_viewport_scale_uniforms[shader_index], 1,
			draw->legacy_viewport_scale);
		glUniform2fv(drawshader_retained_legacy_viewport_center_uniforms[shader_index], 1,
			draw->legacy_viewport_center);
	}
	glUniform1i(drawshader_retained_lighting_mode_uniforms[shader_index], lighting_mode);
	if (lighting_mode == 1 && drawshader_light_direction_uniforms[shader_index] != -1)
	{
		glUniform3f(drawshader_light_direction_uniforms[shader_index],
			per_pixel_light_direction.x, per_pixel_light_direction.y,
			per_pixel_light_direction.z);
	}
	glUniform1i(drawshader_retained_vertex_alpha_uniforms[shader_index],
		(OpenGL_state.cur_alpha_type & ATF_VERTEX) != 0 ? 1 : 0);
	glUniform1f(drawshader_retained_alpha_scale_uniforms[shader_index],
		Alpha_multiplier * OpenGL_Alpha_factor / 255.0f);
	glUniform1i(drawshader_retained_effect_mode_uniforms[shader_index], draw->effect_mode);
	glUniform1f(drawshader_retained_effect_alpha_scale_uniforms[shader_index], draw->effect_alpha_scale);
	if (draw->effect_mode == 1 || draw->effect_mode == 2 ||
		draw->effect_mode == 4 || draw->effect_mode == 5)
	{
		glUniform3fv(drawshader_retained_fog_plane_uniforms[shader_index], 1, draw->fog_plane);
		glUniform1f(drawshader_retained_fog_distance_uniforms[shader_index], draw->fog_distance);
		glUniform1f(drawshader_retained_fog_eye_distance_uniforms[shader_index], draw->fog_eye_distance);
		glUniform1f(drawshader_retained_fog_depth_uniforms[shader_index], draw->fog_depth);
	}
	if (draw->effect_mode == 3)
	{
		glUniform3fv(drawshader_retained_specular_view_position_uniforms[shader_index], 1,
			draw->specular_view_position);
		glUniform3fv(drawshader_retained_specular_light_position_uniforms[shader_index], 1,
			draw->specular_light_position);
		glUniform1f(drawshader_retained_specular_scalar_uniforms[shader_index], draw->specular_scalar);
		glUniform1i(drawshader_retained_specular_smooth_uniforms[shader_index],
			draw->specular_smooth ? 1 : 0);
	}
	glUniform1i(drawshader_retained_deform_enabled_uniforms[shader_index], draw->deform_enabled ? 1 : 0);
	if (draw->deform_enabled)
	{
		glUniform1i(drawshader_retained_deform_mode_uniforms[shader_index], draw->deform_mode);
		glUniform1ui(drawshader_retained_deform_seed_uniforms[shader_index], draw->deform_seed);
		glUniform1f(drawshader_retained_deform_range_uniforms[shader_index], draw->deform_range);
		glUniform3fv(drawshader_retained_deform_direction_uniforms[shader_index], 1,
			draw->deform_direction);
	}
	glUniform1i(drawshader_retained_custom_clip_enabled_uniforms[shader_index],
		draw->custom_clip_enabled ? 1 : 0);
	if (draw->custom_clip_enabled)
	{
		glUniform3fv(drawshader_retained_custom_clip_point_uniforms[shader_index], 1,
			draw->custom_clip_point);
		glUniform3fv(drawshader_retained_custom_clip_plane_uniforms[shader_index], 1,
			draw->custom_clip_plane);
		glUniform3fv(drawshader_retained_custom_clip_scale_uniforms[shader_index], 1,
			draw->custom_clip_scale);
	}
	glUniform1i(drawshader_retained_near_clip_enabled_uniforms[shader_index],
		draw->near_clip_enabled ? 1 : 0);
	glUniform1i(drawshader_retained_far_clip_enabled_uniforms[shader_index],
		draw->far_clip_enabled ? 1 : 0);
	if (draw->far_clip_enabled)
		glUniform1f(drawshader_retained_far_clip_z_uniforms[shader_index], draw->far_clip_z);
	glUniform1i(drawshader_retained_per_pixel_specular_payload_uniforms[shader_index],
		draw->per_pixel_specular_payload ? 1 : 0);
	if (drawshader_fast_retained_room_base_uniforms[shader_index] != -1)
		glUniform1i(drawshader_fast_retained_room_base_uniforms[shader_index],
			draw->fast_room_base ? 1 : 0);
	const bool use_retained_room_lightmap_arrays =
		draw->retained_room_lightmap_arrays && retained_room_lightmaps_ready;
	if (drawshader_retained_room_lightmap_arrays_uniforms[shader_index] != -1)
		glUniform1i(drawshader_retained_room_lightmap_arrays_uniforms[shader_index],
			use_retained_room_lightmap_arrays ? 1 : 0);
	const bool use_retained_dynamic_lightmaps = draw->retained_dynamic_lightmaps &&
		per_pixel_lightmap_buffer_ready && per_pixel_lightmap_buffer_index >= 0;
	if (drawshader_retained_dynamic_lightmaps_uniforms[shader_index] != -1)
		glUniform1i(drawshader_retained_dynamic_lightmaps_uniforms[shader_index],
			use_retained_dynamic_lightmaps ? 1 : 0);
	if (use_retained_dynamic_lightmaps)
	{
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, GL4_PER_PIXEL_LIGHTMAP_BINDING,
			per_pixel_lightmap_buffers[per_pixel_lightmap_buffer_index]);
	}
	if (draw->custom_clip_enabled)
	{
		glEnable(GL_CLIP_DISTANCE0);
		retained_custom_clip_active = true;
	}
	if (draw->near_clip_enabled)
	{
		glEnable(GL_CLIP_DISTANCE1);
		retained_near_clip_active = true;
	}
	if (draw->far_clip_enabled)
	{
		glEnable(GL_CLIP_DISTANCE2);
		retained_far_clip_active = true;
	}

	retained_include_motion_vectors = CurrentDrawUsesPixelMotionTarget();
	retained_include_motion_object_ids = CurrentDrawWritesMotionObjectId();
	if (drawshader_motion_vector_payload_type_uniforms[shader_index] != -1)
		glUniform1i(drawshader_motion_vector_payload_type_uniforms[shader_index], 0);
	if (drawshader_motion_vector_previous_view_projection_uniforms[shader_index] != -1 && cockpit_motion_object_active)
		glUniformMatrix4fv(drawshader_motion_vector_previous_view_projection_uniforms[shader_index], 1,
			GL_FALSE, cockpit_previous_view_projection);
	const bool has_previous_view = cockpit_motion_object_active ?
		have_cockpit_previous_view_projection : have_previous_view_projection;
	if (drawshader_motion_vector_has_previous_uniforms[shader_index] != -1)
		glUniform1i(drawshader_motion_vector_has_previous_uniforms[shader_index],
			draw->has_previous && has_previous_view ? 1 : 0);

	const bool drawing_to_scene = framebuffer_ok &&
		GL4DrawTargetIsFramebuffer(framebuffers[framebuffer_current_draw].Handle());
	const bool force_motion_vector_draw_buffer = CurrentDrawIsLateCockpitPixelMotionVectorDraw();
	const bool include_ao_class = !cockpit_scene_frame_active &&
		OpenGL_state.cur_zbuffer_state != 0 && depth_write_enabled;
	const bool include_post_mask = CurrentDrawNeedsPostMask(include_ao_class);
	retained_override_draw_buffers = drawing_to_scene &&
		(cockpit_scene_frame_active || force_motion_vector_draw_buffer ||
		 PixelMotionVectorModeEnabled() || !include_post_mask || !include_ao_class);
	if (retained_override_draw_buffers)
	{
		if (cockpit_scene_frame_active)
			GL4UseSceneColorDrawBuffer();
		else
			GL4UseSceneDrawBuffersForCurrentDraw(retained_include_motion_vectors,
				include_post_mask, include_ao_class, retained_include_motion_object_ids);
	}
	OpenGL_polys_drawn += draw->polygon_count;
	OpenGL_verts_processed += draw->vertex_count;

	retained_draw_active = true;
	return true;
}

void GL4Renderer::EndRetainedPolymodelDraw()
{
	if (!retained_draw_active)
		return;

	if (lastdrawshader >= 0 && drawshader_retained_mode_uniforms[lastdrawshader] != -1)
		glUniform1i(drawshader_retained_mode_uniforms[lastdrawshader], 0);
	if (lastdrawshader >= 0 && drawshader_fast_retained_room_base_uniforms[lastdrawshader] != -1)
		glUniform1i(drawshader_fast_retained_room_base_uniforms[lastdrawshader], 0);
	if (lastdrawshader >= 0 && drawshader_retained_room_lightmap_arrays_uniforms[lastdrawshader] != -1)
		glUniform1i(drawshader_retained_room_lightmap_arrays_uniforms[lastdrawshader], 0);
	if (lastdrawshader >= 0 && drawshader_retained_dynamic_lightmaps_uniforms[lastdrawshader] != -1)
		glUniform1i(drawshader_retained_dynamic_lightmaps_uniforms[lastdrawshader], 0);
	if (retained_custom_clip_active)
	{
		glDisable(GL_CLIP_DISTANCE0);
		retained_custom_clip_active = false;
	}
	if (retained_near_clip_active)
	{
		glDisable(GL_CLIP_DISTANCE1);
		retained_near_clip_active = false;
	}
	if (retained_far_clip_active)
	{
		glDisable(GL_CLIP_DISTANCE2);
		retained_far_clip_active = false;
	}
	if (retained_include_motion_vectors || retained_include_motion_object_ids)
	{
		motion_vectors_dirty = true;
		motion_vectors.MarkDirty();
	}
	if (retained_override_draw_buffers)
	{
		if (cockpit_scene_frame_active)
			GL4UseSceneColorDrawBuffer();
		else
			UseSceneDrawBuffers();
	}

	retained_draw_active = false;
	retained_override_draw_buffers = false;
	retained_include_motion_vectors = false;
	retained_include_motion_object_ids = false;
	legacy_draw_uniforms_dirty = true;
}

static uint64_t GL4RoomFogHashBytes(uint64_t hash, const void* data, size_t size)
{
	const auto* bytes = static_cast<const unsigned char*>(data);
	for (size_t i = 0; i < size; i++)
	{
		hash ^= bytes[i];
		hash *= UINT64_C(1099511628211);
	}
	return hash;
}

bool GL4Renderer::PrepareRoomFogEntryMap(const renderer_room_fog_state& state)
{
	if (!framebuffer_ok || !have_current_view_projection || state.viewer_inside ||
		state.triangle_count <= 0 || !state.triangles)
	{
		return false;
	}

	Framebuffer& scene = framebuffers[framebuffer_current_draw];
	const int width = (int)scene.Width();
	const int height = (int)scene.Height();
	if (width <= 0 || height <= 0)
		return false;

	GLint viewport[4] = {};
	glGetIntegerv(GL_VIEWPORT, viewport);
	int entry_origin[2] = { viewport[0] + viewport[2], viewport[1] + viewport[3] };
	int entry_end[2] = { viewport[0], viewport[1] };
	bool full_viewport = false;
	for (int triangle_index = 0; triangle_index < state.triangle_count && !full_viewport;
		triangle_index++)
	{
		const renderer_room_fog_triangle& triangle = state.triangles[triangle_index];
		const float* corners[3] = { triangle.a, triangle.b, triangle.c };
		for (const float* corner : corners)
		{
			const vector world_position = { corner[0], corner[1], corner[2] };
			vec4_array clip = {};
			GL4TransformWorldToClip(current_view_projection, world_position, clip);
			if (clip.w <= 0.0001f || clip.z <= -clip.w)
			{
				// A portal crossing the eye or near plane can cover an arbitrarily
				// large portion of the view after clipping. Use the conservative bound.
				full_viewport = true;
				break;
			}
			const float screen_x = viewport[0] +
				((clip.x / clip.w) * 0.5f + 0.5f) * viewport[2];
			const float screen_y = viewport[1] +
				((clip.y / clip.w) * 0.5f + 0.5f) * viewport[3];
			entry_origin[0] = std::min(entry_origin[0], (int)std::floor(screen_x) - 2);
			entry_origin[1] = std::min(entry_origin[1], (int)std::floor(screen_y) - 2);
			entry_end[0] = std::max(entry_end[0], (int)std::ceil(screen_x) + 2);
			entry_end[1] = std::max(entry_end[1], (int)std::ceil(screen_y) + 2);
		}
	}
	if (full_viewport)
	{
		entry_origin[0] = viewport[0];
		entry_origin[1] = viewport[1];
		entry_end[0] = viewport[0] + viewport[2];
		entry_end[1] = viewport[1] + viewport[3];
	}
	entry_origin[0] = std::max(viewport[0], std::min(entry_origin[0], viewport[0] + viewport[2]));
	entry_origin[1] = std::max(viewport[1], std::min(entry_origin[1], viewport[1] + viewport[3]));
	entry_end[0] = std::max(viewport[0], std::min(entry_end[0], viewport[0] + viewport[2]));
	entry_end[1] = std::max(viewport[1], std::min(entry_end[1], viewport[1] + viewport[3]));
	const int entry_size[2] = {
		std::max(entry_end[0] - entry_origin[0], 0),
		std::max(entry_end[1] - entry_origin[1], 0)
	};
	if (entry_size[0] <= 0 || entry_size[1] <= 0)
		return false;
	uint64_t hash = UINT64_C(1469598103934665603);
	hash = GL4RoomFogHashBytes(hash, &width, sizeof(width));
	hash = GL4RoomFogHashBytes(hash, &height, sizeof(height));
	hash = GL4RoomFogHashBytes(hash, viewport, sizeof(viewport));
	hash = GL4RoomFogHashBytes(hash, current_view_projection, sizeof(current_view_projection));
	hash = GL4RoomFogHashBytes(hash, state.viewer_position, sizeof(state.viewer_position));
	hash = GL4RoomFogHashBytes(hash, state.viewer_forward, sizeof(state.viewer_forward));
	hash = GL4RoomFogHashBytes(hash, &state.triangle_count, sizeof(state.triangle_count));
	if (state.triangle_count > 0)
	{
		hash = GL4RoomFogHashBytes(hash, state.triangles,
			(size_t)state.triangle_count * sizeof(renderer_room_fog_triangle));
	}

	room_fog_entry_stamp++;
	for (RoomFogEntryCache& cache : room_fog_entry_cache)
	{
		if (cache.valid && cache.hash == hash &&
			cache.framebuffer.Width() == (uint32_t)entry_size[0] &&
			cache.framebuffer.Height() == (uint32_t)entry_size[1])
		{
			cache.last_used = room_fog_entry_stamp;
			room_fog_entry_texture = cache.framebuffer.ColorTextureForRead();
			memcpy(room_fog_entry_origin, cache.origin, sizeof(room_fog_entry_origin));
			memcpy(room_fog_entry_size, cache.size, sizeof(room_fog_entry_size));
			return room_fog_entry_texture != 0;
		}
	}

	RoomFogEntryCache* target = &room_fog_entry_cache[0];
	for (RoomFogEntryCache& cache : room_fog_entry_cache)
	{
		if (!cache.valid)
		{
			target = &cache;
			break;
		}
		if (cache.last_used < target->last_used)
			target = &cache;
	}

	PERF_MARKER_SCOPE("RoomFog.BuildEntryMap");
	GLint old_draw = 0;
	GLint old_read = 0;
	GLint old_vao = 0;
	GLint old_active_texture = GL_TEXTURE0;
	GLint old_scissor_box[4] = {};
	GLint old_blend_equation_rgb = GL_FUNC_ADD;
	GLint old_blend_equation_alpha = GL_FUNC_ADD;
	GLint old_blend_src_rgb = GL_ONE;
	GLint old_blend_dst_rgb = GL_ZERO;
	GLint old_blend_src_alpha = GL_ONE;
	GLint old_blend_dst_alpha = GL_ZERO;
	GLboolean old_color_mask[4] = {};
	const GLboolean blend_enabled = glIsEnabled(GL_BLEND);
	const GLboolean depth_enabled = glIsEnabled(GL_DEPTH_TEST);
	const GLboolean cull_enabled = glIsEnabled(GL_CULL_FACE);
	const GLboolean scissor_enabled = glIsEnabled(GL_SCISSOR_TEST);
	const GLboolean clip0_enabled = glIsEnabled(GL_CLIP_DISTANCE0);
	const GLboolean clip1_enabled = glIsEnabled(GL_CLIP_DISTANCE1);
	const GLboolean clip2_enabled = glIsEnabled(GL_CLIP_DISTANCE2);
	const GLboolean depth_clamp_enabled = glIsEnabled(GL_DEPTH_CLAMP);
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &old_draw);
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &old_read);
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &old_vao);
	glGetIntegerv(GL_ACTIVE_TEXTURE, &old_active_texture);
	glGetIntegerv(GL_SCISSOR_BOX, old_scissor_box);
	glGetIntegerv(GL_BLEND_EQUATION_RGB, &old_blend_equation_rgb);
	glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &old_blend_equation_alpha);
	glGetIntegerv(GL_BLEND_SRC_RGB, &old_blend_src_rgb);
	glGetIntegerv(GL_BLEND_DST_RGB, &old_blend_dst_rgb);
	glGetIntegerv(GL_BLEND_SRC_ALPHA, &old_blend_src_alpha);
	glGetIntegerv(GL_BLEND_DST_ALPHA, &old_blend_dst_alpha);
	glGetBooleanv(GL_COLOR_WRITEMASK, old_color_mask);

	glActiveTexture(GL_TEXTURE10);
	glBindTexture(GL_TEXTURE_2D, 0);
	target->framebuffer.Update(entry_size[0], entry_size[1], GL_R32F, GL_RED, GL_FLOAT);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target->framebuffer.Handle());
	glViewport(viewport[0] - entry_origin[0], viewport[1] - entry_origin[1],
		viewport[2], viewport[3]);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_CLIP_DISTANCE0);
	glDisable(GL_CLIP_DISTANCE1);
	glDisable(GL_CLIP_DISTANCE2);
	glEnable(GL_DEPTH_CLAMP);
	glEnable(GL_BLEND);
	glBlendEquation(GL_MIN);
	glBlendFunc(GL_ONE, GL_ONE);
	glColorMask(GL_TRUE, GL_FALSE, GL_FALSE, GL_FALSE);
	const GLfloat no_entry[4] = { FLT_MAX, 0.0f, 0.0f, 0.0f };
	glClearBufferfv(GL_COLOR, 0, no_entry);

	roomfogentryshader.Use();
	glUniformMatrix4fv(roomfogentry_view_projection, 1, GL_FALSE, current_view_projection);
	glUniform3fv(roomfogentry_viewer_position, 1, state.viewer_position);
	glUniform3fv(roomfogentry_viewer_forward, 1, state.viewer_forward);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, GL4_ROOM_FOG_PORTAL_BINDING,
		room_fog_portal_buffer);
	glBindVertexArray(GL_GetFramebufferVAO());
	glDrawArrays(GL_TRIANGLES, 0, state.triangle_count * 3);

	glColorMask(old_color_mask[0], old_color_mask[1], old_color_mask[2], old_color_mask[3]);
	glBlendEquationSeparate(old_blend_equation_rgb, old_blend_equation_alpha);
	glBlendFuncSeparate(old_blend_src_rgb, old_blend_dst_rgb,
		old_blend_src_alpha, old_blend_dst_alpha);
	if (blend_enabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
	if (depth_enabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
	if (cull_enabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
	if (scissor_enabled)
	{
		glEnable(GL_SCISSOR_TEST);
		glScissor(old_scissor_box[0], old_scissor_box[1], old_scissor_box[2], old_scissor_box[3]);
	}
	else glDisable(GL_SCISSOR_TEST);
	if (clip0_enabled) glEnable(GL_CLIP_DISTANCE0); else glDisable(GL_CLIP_DISTANCE0);
	if (clip1_enabled) glEnable(GL_CLIP_DISTANCE1); else glDisable(GL_CLIP_DISTANCE1);
	if (clip2_enabled) glEnable(GL_CLIP_DISTANCE2); else glDisable(GL_CLIP_DISTANCE2);
	if (depth_clamp_enabled) glEnable(GL_DEPTH_CLAMP); else glDisable(GL_DEPTH_CLAMP);
	glBindVertexArray(old_vao);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, old_draw);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, old_read);
	glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
	glActiveTexture(old_active_texture);
	ShaderProgram::ClearBinding();
	lastdrawshader = -1;
	legacy_draw_uniforms_dirty = true;

	target->hash = hash;
	target->last_used = room_fog_entry_stamp;
	memcpy(target->origin, entry_origin, sizeof(target->origin));
	memcpy(target->size, entry_size, sizeof(target->size));
	target->valid = true;
	room_fog_entry_texture = target->framebuffer.ColorTextureForRead();
	memcpy(room_fog_entry_origin, entry_origin, sizeof(room_fog_entry_origin));
	memcpy(room_fog_entry_size, entry_size, sizeof(room_fog_entry_size));
	return room_fog_entry_texture != 0;
}

bool GL4Renderer::SetRoomFogState(const renderer_room_fog_state *state)
{
	if (!state || !state->enabled)
	{
		room_fog_enabled = false;
		room_fog_overlay = false;
		room_fog_viewer_inside = false;
		room_fog_entry_map_enabled = false;
		room_fog_entry_texture = 0;
		memset(room_fog_entry_origin, 0, sizeof(room_fog_entry_origin));
		memset(room_fog_entry_size, 0, sizeof(room_fog_entry_size));
		legacy_draw_uniforms_dirty = true;
		return true;
	}
	if (state->depth <= 0.0f || state->triangle_count < 0 ||
		(state->triangle_count > 0 && !state->triangles))
	{
		return false;
	}

	room_fog_enabled = true;
	room_fog_viewer_inside = state->viewer_inside;
	memcpy(room_fog_viewer_position, state->viewer_position,
		sizeof(room_fog_viewer_position));
	memcpy(room_fog_viewer_forward, state->viewer_forward,
		sizeof(room_fog_viewer_forward));
	memcpy(room_fog_color, state->color, sizeof(room_fog_color));
	room_fog_depth = state->depth;
	room_fog_intensity = state->intensity;

	const bool portal_data_changed =
		room_fog_portal_cache.size() != (size_t)state->triangle_count ||
		(state->triangle_count > 0 &&
			memcmp(room_fog_portal_cache.data(), state->triangles,
				(size_t)state->triangle_count * sizeof(renderer_room_fog_triangle)) != 0);
	if (portal_data_changed)
	{
		if (state->triangle_count > 0)
		{
			room_fog_portal_cache.assign(state->triangles,
				state->triangles + state->triangle_count);
		}
		else
		{
			room_fog_portal_cache.clear();
		}
		if (room_fog_portal_buffer == 0)
			glGenBuffers(1, &room_fog_portal_buffer);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, room_fog_portal_buffer);
		const GLsizeiptr data_size = std::max<GLsizeiptr>(
			(GLsizeiptr)sizeof(renderer_room_fog_triangle),
			(GLsizeiptr)state->triangle_count * sizeof(renderer_room_fog_triangle));
		glBufferData(GL_SHADER_STORAGE_BUFFER, data_size,
			state->triangle_count > 0 ? state->triangles : nullptr, GL_STREAM_DRAW);
	}
	else if (room_fog_portal_buffer == 0)
	{
		glGenBuffers(1, &room_fog_portal_buffer);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, room_fog_portal_buffer);
		glBufferData(GL_SHADER_STORAGE_BUFFER,
			(GLsizeiptr)sizeof(renderer_room_fog_triangle), nullptr, GL_STREAM_DRAW);
	}
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, GL4_ROOM_FOG_PORTAL_BINDING,
		room_fog_portal_buffer);
	room_fog_entry_map_enabled = !state->viewer_inside && PrepareRoomFogEntryMap(*state);
	if (!state->viewer_inside && !room_fog_entry_map_enabled)
	{
		room_fog_enabled = false;
		room_fog_entry_texture = 0;
		legacy_draw_uniforms_dirty = true;
		return false;
	}
	legacy_draw_uniforms_dirty = true;
	return true;
}

void GL4Renderer::SetRoomFogOverlay(int state)
{
	room_fog_overlay = room_fog_enabled && state != 0;
	legacy_draw_uniforms_dirty = true;
}

// Takes nv vertices and draws the 2D polygon defined by those vertices.
// Uses bitmap "handle" as a texture
void GL4Renderer::DrawPolygon2D(int handle, g3Point** p, int nv)
{
	ASSERT(nv < 100);
	ASSERT(Overlay_type == OT_NONE);

	renderer_draw_call_category old_category = draw_call_category;
	if (draw_call_category == RENDERER_DRAW_CALL_3D)
		draw_call_category = RENDERER_DRAW_CALL_2D;
	DrawPolygon3D(handle, p, nv, MAP_TYPE_BITMAP);
	draw_call_category = old_category;
}

void GL4Renderer::BeginMotionObject(int object_handle, int motion_object_flags)
{
	const bool cockpit_draw = rend_Get3DDrawCallCategory() == RENDERER_DRAW_CALL_3D_COCKPIT;
	cockpit_motion_object_active = false;
	motion_object_force_capture = false;
	motion_object_id = 0;
	motion_object_active = object_handle >= 0 && framebuffer_ok &&
		!post_present_pending_swap &&
		(!motion_vectors_capture_locked || cockpit_draw) &&
		PixelMotionVectorConsumerActive() &&
		motion_vectors.velocity_texture != 0;
	if (motion_object_active && cockpit_draw)
		cockpit_motion_object_active = true;
	else if (motion_object_active)
	{
		motion_object_force_capture = (motion_object_flags & RENDERER_MOTION_OBJECT_FORCE_CAPTURE) != 0;
		motion_object_id = ((unsigned int)object_handle + 1u) &
			~GL4_MOTION_OBJECT_LEGACY_BLUR_MASK;
		if (motion_object_flags & RENDERER_MOTION_OBJECT_LEGACY_BLUR)
			motion_object_id |= GL4_MOTION_OBJECT_LEGACY_BLUR_MASK;
	}
}

void GL4Renderer::EndMotionObject()
{
	if (cockpit_motion_object_active && have_current_view_projection)
	{
		memcpy(cockpit_previous_view_projection, current_view_projection, sizeof(cockpit_previous_view_projection));
		have_cockpit_previous_view_projection = true;
	}
	motion_object_active = false;
	cockpit_motion_object_active = false;
	motion_object_force_capture = false;
	motion_object_id = 0;
}

void GL4Renderer::SuspendMotionVectorWrites()
{
	motion_vector_write_suppression_depth++;
	if (PixelMotionVectorModeEnabled())
		legacy_draw_uniforms_dirty = true;
}

void GL4Renderer::ResumeMotionVectorWrites()
{
	if (motion_vector_write_suppression_depth > 0)
		motion_vector_write_suppression_depth--;
	if (PixelMotionVectorModeEnabled())
		legacy_draw_uniforms_dirty = true;
}

void GL4Renderer::FillMotionVectorRegion(int object_handle)
{
	FlushFontBatch();

	if (!PixelMotionVectorModeEnabled() || MotionVectorsFrozen() ||
		post_present_pending_swap || !framebuffer_ok || motion_vectors.velocity_texture == 0)
	{
		return;
	}

	int x = ScaledX(OpenGL_state.clip_x1);
	int y = FramebufferHeight() - ScaledY(OpenGL_state.clip_y2);
	int w = ScaledW(OpenGL_state.clip_x2 - OpenGL_state.clip_x1);
	int h = ScaledH(OpenGL_state.clip_y2 - OpenGL_state.clip_y1);
	if (w <= 0 || h <= 0)
		return;

	GLint old_draw = 0;
	GLint old_read = 0;
	GLint old_draw_buffer = 0;
	GLint old_read_buffer = 0;
	GLint old_scissor_box[4] = {};
	GLboolean scissor_was_enabled = glIsEnabled(GL_SCISSOR_TEST);
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &old_draw);
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &old_read);
	glGetIntegerv(GL_DRAW_BUFFER, &old_draw_buffer);
	glGetIntegerv(GL_READ_BUFFER, &old_read_buffer);
	glGetIntegerv(GL_SCISSOR_BOX, old_scissor_box);

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffers[framebuffer_current_draw].Handle());
	glEnable(GL_SCISSOR_TEST);
	glScissor(x, y, w, h);

	GLenum draw_buffer = GL_COLOR_ATTACHMENT1;
	glDrawBuffers(1, &draw_buffer);
	const float zero_velocity[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	glClearBufferfv(GL_COLOR, 0, zero_velocity);

	draw_buffer = GL_COLOR_ATTACHMENT4;
	glDrawBuffers(1, &draw_buffer);
	unsigned int object_id = ((unsigned int)object_handle + 1u) &
		~GL4_MOTION_OBJECT_LEGACY_BLUR_MASK;
	if (object_id == 0)
		object_id = 1;
	const GLuint fill_id[4] = { object_id, 0u, 0u, 0u };
	glClearBufferuiv(GL_COLOR, 0, fill_id);

	motion_vectors_dirty = true;
	motion_vectors.MarkDirty();

	if (scissor_was_enabled)
	{
		glScissor(old_scissor_box[0], old_scissor_box[1], old_scissor_box[2], old_scissor_box[3]);
	}
	else
	{
		glDisable(GL_SCISSOR_TEST);
	}
	glBindFramebuffer(GL_READ_FRAMEBUFFER, old_read);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, old_draw);
	if ((GLuint)old_draw == framebuffers[framebuffer_current_draw].Handle())
		UseSceneDrawBuffers();
	else
		glDrawBuffer(old_draw_buffer);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, old_read);
	glReadBuffer(old_read_buffer);
}

bool GL4Renderer::GetMotionVectorSample(const vector *current_world, const vector *previous_world,
	float *current_u, float *current_v, float *velocity_u, float *velocity_v)
{
	if (!current_world || !previous_world || !current_u || !current_v || !velocity_u || !velocity_v ||
		!have_current_view_projection || !have_previous_view_projection)
	{
		return false;
	}

	vec4_array current_clip = {};
	vec4_array previous_clip = {};
	GL4TransformWorldToClip(current_view_projection, *current_world, current_clip);
	GL4TransformWorldToClip(previous_view_projection, *previous_world, previous_clip);
	if (current_clip.w <= 0.00001f || previous_clip.w <= 0.00001f)
		return false;

	const float current_ndc_x = current_clip.x / current_clip.w;
	const float current_ndc_y = current_clip.y / current_clip.w;
	const float previous_ndc_x = previous_clip.x / previous_clip.w;
	const float previous_ndc_y = previous_clip.y / previous_clip.w;
	*current_u = current_ndc_x * 0.5f + 0.5f;
	*current_v = current_ndc_y * 0.5f + 0.5f;
	*velocity_u = (current_ndc_x - previous_ndc_x) * 0.5f;
	*velocity_v = (current_ndc_y - previous_ndc_y) * 0.5f;
	return true;
}

// draws a scaled 2d bitmap to our buffer
void GL4Renderer::DrawScaledBitmap(int x1, int y1, int x2, int y2,
	int bm, float u0, float v0, float u1, float v1, int color, float* alphas)
{
	g3Point* ptr_pnts[4];
	g3Point pnts[4];
	float r, g, b;
	if (color != -1)
	{
		r = GR_COLOR_RED(color) / 255.0;
		g = GR_COLOR_GREEN(color) / 255.0;
		b = GR_COLOR_BLUE(color) / 255.0;
	}
	for (int i = 0; i < 4; i++)
	{
		if (color == -1)
			pnts[i].p3_l = 1.0;
		else
		{
			pnts[i].p3_r = r;
			pnts[i].p3_g = g;
			pnts[i].p3_b = b;
		}
		if (alphas)
		{
			pnts[i].p3_a = alphas[i];
		}

		pnts[i].p3_z = 1.0f;
		pnts[i].p3_flags = PF_PROJECTED;
		pnts[i].p3_motion_world_valid = 0;
		pnts[i].p3_motion_prev_world_valid = 0;
		pnts[i].p3_vecPreRot.x = 0.0f;
		pnts[i].p3_vecPreRot.y = 0.0f;
		pnts[i].p3_vecPreRot.z = 0.0f;
	}

	pnts[0].p3_sx = x1;
	pnts[0].p3_sy = y1;
	pnts[0].p3_u = u0;
	pnts[0].p3_v = v0;
	pnts[1].p3_sx = x2;
	pnts[1].p3_sy = y1;
	pnts[1].p3_u = u1;
	pnts[1].p3_v = v0;
	pnts[2].p3_sx = x2;
	pnts[2].p3_sy = y2;
	pnts[2].p3_u = u1;
	pnts[2].p3_v = v1;
	pnts[3].p3_sx = x1;
	pnts[3].p3_sy = y2;
	pnts[3].p3_u = u0;
	pnts[3].p3_v = v1;
	ptr_pnts[0] = &pnts[0];
	ptr_pnts[1] = &pnts[1];
	ptr_pnts[2] = &pnts[2];
	ptr_pnts[3] = &pnts[3];
	SetTextureType(TT_LINEAR);
	DrawPolygon2D(bm, ptr_pnts, 4);
}

void GL4Renderer::DrawScaledBitmapWithZ(int x1, int y1, int x2, int y2,
	int bm, float u0, float v0, float u1, float v1, float zval, int color, float* alphas,
	const vector* world_position)
{
	g3Point* ptr_pnts[4];
	g3Point pnts[4];
	float r, g, b;

	if (color != -1)
	{
		r = GR_COLOR_RED(color) / 255.0;
		g = GR_COLOR_GREEN(color) / 255.0;
		b = GR_COLOR_BLUE(color) / 255.0;
	}

	for (int i = 0; i < 4; i++)
	{
		if (color == -1)
			pnts[i].p3_l = 1.0;
		else
		{
			pnts[i].p3_r = r;
			pnts[i].p3_g = g;
			pnts[i].p3_b = b;
		}

		if (alphas)
		{
			pnts[i].p3_a = alphas[i];
		}

		pnts[i].p3_z = zval;
		pnts[i].p3_flags = PF_PROJECTED;
		pnts[i].p3_motion_world_valid = 0;
		pnts[i].p3_motion_prev_world_valid = 0;
		pnts[i].p3_vecPreRot = world_position ? *world_position : vector{0.0f, 0.0f, 0.0f};
	}



	pnts[0].p3_sx = x1;
	pnts[0].p3_sy = y1;
	pnts[0].p3_u = u0;
	pnts[0].p3_v = v0;

	pnts[1].p3_sx = x2;
	pnts[1].p3_sy = y1;
	pnts[1].p3_u = u1;
	pnts[1].p3_v = v0;

	pnts[2].p3_sx = x2;
	pnts[2].p3_sy = y2;
	pnts[2].p3_u = u1;
	pnts[2].p3_v = v1;

	pnts[3].p3_sx = x1;
	pnts[3].p3_sy = y2;
	pnts[3].p3_u = u0;
	pnts[3].p3_v = v1;

	// World-space fog needs the actual camera-facing quad, not one position
	// repeated at all four corners. Reconstruct each corner from the projected
	// coordinates and eye-space depth after screen clipping has been applied.
	if (world_position)
	{
		for (int i = 0; i < 4; i++)
		{
			pnts[i].p3_vec.x = ((pnts[i].p3_sx - Window_cx) / Window_w2) * zval;
			pnts[i].p3_vec.y = -((pnts[i].p3_sy - Window_cy) / Window_h2) * zval;
			pnts[i].p3_vec.z = zval;
			g3_SetPointPreRotFromView(&pnts[i]);
		}
	}

	ptr_pnts[0] = &pnts[0];
	ptr_pnts[1] = &pnts[1];
	ptr_pnts[2] = &pnts[2];
	ptr_pnts[3] = &pnts[3];

	SetTextureType(TT_LINEAR);
	DrawPolygon3D(bm, ptr_pnts, 4);
}

// Fills a rectangle on the display
void GL4Renderer::FillRect(ddgr_color color, int x1, int y1, int x2, int y2)
{
	captured_scene_draw_count_valid = false;

	int r = GR_COLOR_RED(color);
	int g = GR_COLOR_GREEN(color);
	int b = GR_COLOR_BLUE(color);

	int width = x2 - x1;
	int height = y2 - y1;

	x1 += OpenGL_state.clip_x1;
	y1 += OpenGL_state.clip_y1;

	glEnable(GL_SCISSOR_TEST);
	glScissor(ScaledX(x1), FramebufferHeight() - ScaledY(height + y1), ScaledW(width), ScaledH(height));
	glClearColor((float)r / 255.0, (float)g / 255.0, (float)b / 255.0, 0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	width = OpenGL_state.clip_x2 - OpenGL_state.clip_x1;
	height = OpenGL_state.clip_y2 - OpenGL_state.clip_y1;

	glScissor(ScaledX(OpenGL_state.clip_x1), FramebufferHeight() - ScaledY(OpenGL_state.clip_y1 + height), ScaledW(width), ScaledH(height));
	glDisable(GL_SCISSOR_TEST);
}

// Sets a pixel on the display
void GL4Renderer::SetPixel(ddgr_color color, int x, int y)
{
	ubyte r = (color >> 16 & 0xFF);
	ubyte g = (color >> 8 & 0xFF);
	ubyte b = (color & 0xFF);

	SelectDrawShader();

	GL_vertices[0].color.r = r;
	GL_vertices[0].color.g = g;
	GL_vertices[0].color.b = b;
	GL_vertices[0].color.a = 255;

	GL_vertices[0].vert.x = x;
	GL_vertices[0].vert.y = y;
	GL_vertices[0].vert.z = 0;
	GL_vertices[0].motion_world_position.x = 0.0f;
	GL_vertices[0].motion_world_position.y = 0.0f;
	GL_vertices[0].motion_world_position.z = 0.0f;
	GL_vertices[0].motion_world_position.w = 0.0f;
	GL_vertices[0].motion_previous_world_position.x = 0.0f;
	GL_vertices[0].motion_previous_world_position.y = 0.0f;
	GL_vertices[0].motion_previous_world_position.z = 0.0f;
	GL_vertices[0].motion_previous_world_position.w = 0.0f;

	//please do not call this function if you can avoid it.
	int offset = CopyVertices(1);
	GLfloat point_size = std::max(1.0f, std::min((GLfloat)SupersamplingFactor(), max_point_size));
	glPointSize(point_size);
	rend_RecordDrawCall(RENDERER_DRAW_CALL_PRIMITIVE);
	glDrawArrays(GL_POINTS, offset, 1);
	glPointSize(1.0f);
}

// Sets a pixel on the display
ddgr_color GL4Renderer::GetPixel(int x, int y)
{
	ddgr_color color[4];
	glReadPixels(ScaledX(x), FramebufferHeight() - 1 - ScaledY(y), 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, (GLvoid*)color);
	return color[0];
}

void GL4Renderer::FillCircle(ddgr_color col, int x, int y, int rad)
{
}

void GL4Renderer::DrawCircle(int x, int y, int rad)
{
}

// Sets up a font character to draw.  We draw our fonts as pieces of textures
void GL4Renderer::DrawFontCharacter(int bm_handle, int x1, int y1, int x2, int y2, float u, float v, float w, float h)
{
	const int batch_index = GL4FontBatchIndexForAlpha(OpenGL_state.cur_alpha_type);

	const int texture_layer = GetFontTextureLayer(bm_handle);
	if (texture_layer < 0)
		return;
	if (font_batch_vertices[batch_index].size() + 6 > 60000)
		FlushFontBatch();

	gl_vertex quad[4] = {};
	const ubyte fr = GR_COLOR_RED(OpenGL_state.cur_color);
	const ubyte fg = GR_COLOR_GREEN(OpenGL_state.cur_color);
	const ubyte fb = GR_COLOR_BLUE(OpenGL_state.cur_color);
	const float alpha = Alpha_multiplier * OpenGL_Alpha_factor;
	const float z = -std::max(0.f, std::min(1.0f, 1.0f - (1.0f / (1.0f + Z_bias))));
	const float offset_x = (float)OpenGL_state.clip_x1;
	const float offset_y = (float)OpenGL_state.clip_y1;

	for (int i = 0; i < 4; i++)
	{
		quad[i].color.r = fr;
		quad[i].color.g = fg;
		quad[i].color.b = fb;
		quad[i].color.a = (ubyte)alpha;
		quad[i].tex_coord.w = 1.0f;
		quad[i].tex_coord2.s = (float)texture_layer;
		quad[i].vert.z = z;
	}

	quad[0].vert.x = offset_x + (float)x1;
	quad[0].vert.y = offset_y + (float)y1;
	quad[0].tex_coord.s = u;
	quad[0].tex_coord.t = v;
	quad[1].vert.x = offset_x + (float)x2;
	quad[1].vert.y = offset_y + (float)y1;
	quad[1].tex_coord.s = u + w;
	quad[1].tex_coord.t = v;
	quad[2].vert.x = offset_x + (float)x2;
	quad[2].vert.y = offset_y + (float)y2;
	quad[2].tex_coord.s = u + w;
	quad[2].tex_coord.t = v + h;
	quad[3].vert.x = offset_x + (float)x1;
	quad[3].vert.y = offset_y + (float)y2;
	quad[3].tex_coord.s = u;
	quad[3].tex_coord.t = v + h;

	font_batch_vertices[batch_index].push_back(quad[0]);
	font_batch_vertices[batch_index].push_back(quad[1]);
	font_batch_vertices[batch_index].push_back(quad[2]);
	font_batch_vertices[batch_index].push_back(quad[0]);
	font_batch_vertices[batch_index].push_back(quad[2]);
	font_batch_vertices[batch_index].push_back(quad[3]);
}

void GL4Renderer::FlushTextLayer()
{
	FlushFontBatch();
}

// Draws a line
void GL4Renderer::DrawLine(int x1, int y1, int x2, int y2)
{
	sbyte atype;
	light_state ltype;
	texture_type ttype;
	int color = OpenGL_state.cur_color;

	ubyte r = GR_COLOR_RED(color);
	ubyte g = GR_COLOR_GREEN(color);
	ubyte b = GR_COLOR_BLUE(color);

	atype = OpenGL_state.cur_alpha_type;
	ltype = OpenGL_state.cur_light_state;
	ttype = OpenGL_state.cur_texture_type;

	SetAlphaType(AT_ALWAYS);
	SetLighting(LS_NONE);
	SetTextureType(TT_FLAT);

	SelectDrawShader();

	GL_vertices[0].color.r = r;
	GL_vertices[0].color.g = g;
	GL_vertices[0].color.b = b;
	GL_vertices[0].color.a = 255;
	GL_vertices[1].color = GL_vertices[0].color;

	//hack to avoid line clipping but this isn't working correctly yet, causes one corner to vanish.
	GL_vertices[0].vert.x = x1 + 1.f;
	GL_vertices[0].vert.y = y1 + 1.f;
	GL_vertices[0].vert.z = 0;
	GL_vertices[1].vert.x = x2 + 1.f;
	GL_vertices[1].vert.y = y2 + 1.f;
	GL_vertices[1].vert.z = 0;
	GL_vertices[0].motion_world_position.x = 0.0f;
	GL_vertices[0].motion_world_position.y = 0.0f;
	GL_vertices[0].motion_world_position.z = 0.0f;
	GL_vertices[0].motion_world_position.w = 0.0f;
	GL_vertices[1].motion_world_position.x = 0.0f;
	GL_vertices[1].motion_world_position.y = 0.0f;
	GL_vertices[1].motion_world_position.z = 0.0f;
	GL_vertices[1].motion_world_position.w = 0.0f;
	GL_vertices[0].motion_previous_world_position.x = 0.0f;
	GL_vertices[0].motion_previous_world_position.y = 0.0f;
	GL_vertices[0].motion_previous_world_position.z = 0.0f;
	GL_vertices[0].motion_previous_world_position.w = 0.0f;
	GL_vertices[1].motion_previous_world_position.x = 0.0f;
	GL_vertices[1].motion_previous_world_position.y = 0.0f;
	GL_vertices[1].motion_previous_world_position.z = 0.0f;
	GL_vertices[1].motion_previous_world_position.w = 0.0f;

	int offset = CopyVertices(2);
	GLfloat line_width = std::max(1.0f, std::min((GLfloat)SupersamplingFactor(), max_line_width));
	glLineWidth(line_width);
	rend_RecordDrawCall(RENDERER_DRAW_CALL_PRIMITIVE);
	glDrawArrays(GL_LINES, offset, 2);
	glLineWidth(1.0f);

	SetAlphaType(atype);
	SetLighting(ltype);
	SetTextureType(ttype);
}


// Sets the argb characteristics of the font characters.  color1 is the upper left and proceeds clockwise
void GL4Renderer::SetCharacterParameters(ddgr_color color1, ddgr_color color2, ddgr_color color3, ddgr_color color4)
{
	rend_FontRed[0] = (float)(GR_COLOR_RED(color1) / 255.0f);
	rend_FontRed[1] = (float)(GR_COLOR_RED(color2) / 255.0f);
	rend_FontRed[2] = (float)(GR_COLOR_RED(color3) / 255.0f);
	rend_FontRed[3] = (float)(GR_COLOR_RED(color4) / 255.0f);
	rend_FontGreen[0] = (float)(GR_COLOR_GREEN(color1) / 255.0f);
	rend_FontGreen[1] = (float)(GR_COLOR_GREEN(color2) / 255.0f);
	rend_FontGreen[2] = (float)(GR_COLOR_GREEN(color3) / 255.0f);
	rend_FontGreen[3] = (float)(GR_COLOR_GREEN(color4) / 255.0f);
	rend_FontBlue[0] = (float)(GR_COLOR_BLUE(color1) / 255.0f);
	rend_FontBlue[1] = (float)(GR_COLOR_BLUE(color2) / 255.0f);
	rend_FontBlue[2] = (float)(GR_COLOR_BLUE(color3) / 255.0f);
	rend_FontBlue[3] = (float)(GR_COLOR_BLUE(color4) / 255.0f);
	rend_FontAlpha[0] = (color1 >> 24) / 255.0f;
	rend_FontAlpha[1] = (color2 >> 24) / 255.0f;
	rend_FontAlpha[2] = (color3 >> 24) / 255.0f;
	rend_FontAlpha[3] = (color4 >> 24) / 255.0f;
}

// Turns on/off multitexture blending
void GL4Renderer::SetMultitextureBlendMode(bool state)
{
	if (OpenGL_multitexture_state == state)
		return;
	OpenGL_multitexture_state = state;
	if (state)
	{
		Last_texel_unit_set = 0;
	}
	else
	{
		Last_texel_unit_set = 0;
	}
}

// Draws a line using the states of the renderer
void GL4Renderer::DrawSpecialLine(g3Point* p0, g3Point* p1)
{
	ubyte fr, fg, fb, alpha;
	int i;

	fr = GR_COLOR_RED(OpenGL_state.cur_color);
	fg = GR_COLOR_GREEN(OpenGL_state.cur_color);
	fb = GR_COLOR_BLUE(OpenGL_state.cur_color);

	alpha = Alpha_multiplier * OpenGL_Alpha_factor;

	gl_vertex* vertp = GL_vertices;

	// And draw!
	for (i = 0; i < 2; i++, vertp++)
	{
		color_array* colorp = &vertp->color;

		g3Point* pnt = p0;

		if (i == 1)
			pnt = p1;

		if (OpenGL_state.cur_alpha_type & ATF_VERTEX)
			alpha = (ubyte)(pnt->p3_a * Alpha_multiplier * OpenGL_Alpha_factor);

		// If we have a lighting model, apply the correct lighting!
		if (OpenGL_state.cur_light_state != LS_NONE)
		{
			if (OpenGL_state.cur_light_state == LS_FLAT_GOURAUD)
			{
				colorp->r = fr; colorp->g = fg, colorp->b = fb; colorp->a = (ubyte)alpha;
			}
			else
			{
				// Do lighting based on intesity (MONO) or colored (RGB)
				if (OpenGL_state.cur_color_model == CM_MONO)
				{
					colorp->r = pnt->p3_uvl.l * 255; colorp->g = pnt->p3_uvl.l * 255; colorp->b = pnt->p3_uvl.l * 255; colorp->a = (ubyte)alpha;
				}
				else
				{
					colorp->r = pnt->p3_uvl.r * 255; colorp->g = pnt->p3_uvl.g * 255; colorp->b = pnt->p3_uvl.r * 255; colorp->a = (ubyte)alpha;
				}
			}
		}
		else
		{
			colorp->r = fr; colorp->g = fg, colorp->b = fb; colorp->a = (ubyte)alpha;
		}

		// Finally, specify a vertex
		float z = std::max(0., std::min(1.0, 1.0 - (1.0 / (pnt->p3_z + Z_bias))));

		vertp->vert.x = pnt->p3_sx; vertp->vert.y = pnt->p3_sy; vertp->vert.z = -z;
		if (soft_particle_draw_enabled)
			vertp->normal.w = GL4SoftParticleDepthFromPoint(pnt);
		vertp->motion_world_position.x = 0.0f;
		vertp->motion_world_position.y = 0.0f;
		vertp->motion_world_position.z = 0.0f;
		vertp->motion_world_position.w = 0.0f;
		vertp->motion_previous_world_position.x = 0.0f;
		vertp->motion_previous_world_position.y = 0.0f;
		vertp->motion_previous_world_position.z = 0.0f;
		vertp->motion_previous_world_position.w = 0.0f;
		//glVertex3f(pnt->p3_sx + x_add, pnt->p3_sy + y_add, -z);
	}

	SelectDrawShader();
	int offset = CopyVertices(2);
	GLfloat line_width = std::max(1.0f, std::min((GLfloat)SupersamplingFactor(), max_line_width));
	glLineWidth(line_width);
	rend_RecordDrawCall(RENDERER_DRAW_CALL_PRIMITIVE);
	glDrawArrays(GL_LINES, offset, 2);
	glLineWidth(1.0f);
}

void GL4Renderer::DrawSpecialLineBatch(const renderer_line_batch_item *items, int count)
{
	if (!items || count <= 0)
		return;

	ubyte fr = GR_COLOR_RED(OpenGL_state.cur_color);
	ubyte fg = GR_COLOR_GREEN(OpenGL_state.cur_color);
	ubyte fb = GR_COLOR_BLUE(OpenGL_state.cur_color);

	SelectDrawShader();

	static std::vector<gl_vertex> vertices;
	vertices.clear();
	if (vertices.capacity() < (size_t)count * 2)
		vertices.reserve((size_t)count * 2);

	for (int i = 0; i < count; i++)
	{
		g3Point* points[2] = { items[i].p0, items[i].p1 };
		for (int v = 0; v < 2; v++)
		{
			g3Point* pnt = points[v];
			if (!pnt)
				continue;

			gl_vertex vertex = {};
			color_array* colorp = &vertex.color;
			ubyte alpha = (ubyte)(Alpha_multiplier * OpenGL_Alpha_factor);
			if (OpenGL_state.cur_alpha_type & ATF_VERTEX)
				alpha = (ubyte)(pnt->p3_a * Alpha_multiplier * OpenGL_Alpha_factor);

			if (OpenGL_state.cur_light_state != LS_NONE)
			{
				if (OpenGL_state.cur_light_state == LS_FLAT_GOURAUD)
				{
					colorp->r = fr; colorp->g = fg; colorp->b = fb; colorp->a = alpha;
				}
				else if (OpenGL_state.cur_color_model == CM_MONO)
				{
					colorp->r = pnt->p3_uvl.l * 255; colorp->g = pnt->p3_uvl.l * 255;
					colorp->b = pnt->p3_uvl.l * 255; colorp->a = alpha;
				}
				else
				{
					colorp->r = pnt->p3_uvl.r * 255; colorp->g = pnt->p3_uvl.g * 255;
					colorp->b = pnt->p3_uvl.r * 255; colorp->a = alpha;
				}
			}
			else
			{
				colorp->r = fr; colorp->g = fg; colorp->b = fb; colorp->a = alpha;
			}

			float z = std::max(0., std::min(1.0, 1.0 - (1.0 / (pnt->p3_z + Z_bias))));
			vertex.vert.x = pnt->p3_sx;
			vertex.vert.y = pnt->p3_sy;
			vertex.vert.z = -z;
			if (soft_particle_draw_enabled)
				vertex.normal.w = GL4SoftParticleDepthFromPoint(pnt);
			vertex.motion_world_position.x = 0.0f;
			vertex.motion_world_position.y = 0.0f;
			vertex.motion_world_position.z = 0.0f;
			vertex.motion_world_position.w = 0.0f;
			vertex.motion_previous_world_position.x = 0.0f;
			vertex.motion_previous_world_position.y = 0.0f;
			vertex.motion_previous_world_position.z = 0.0f;
			vertex.motion_previous_world_position.w = 0.0f;
			vertices.push_back(vertex);
		}
	}

	if (vertices.empty())
		return;

	int offset = CopyVertices(vertices.data(), (int)vertices.size());
	GLfloat line_width = std::max(1.0f, std::min((GLfloat)SupersamplingFactor(), max_line_width));
	glLineWidth(line_width);
	rend_RecordDrawCall(RENDERER_DRAW_CALL_PRIMITIVE);
	glDrawArrays(GL_LINES, offset, (GLsizei)vertices.size());
	glLineWidth(1.0f);
}

//	given a chunked bitmap, renders it.
void GL4Renderer::DrawChunkedBitmap(chunked_bitmap* chunk, int x, int y, ubyte alpha)
{
	int* bm_array = chunk->bm_array;
	int w = chunk->w;
	int h = chunk->h;
	int piece_w = bm_w(bm_array[0], 0);
	int piece_h = bm_h(bm_array[0], 0);
	int screen_w, screen_h;
	int i, t;
	SetZBufferState(0);
	GetProjectionParameters(&screen_w, &screen_h);
	for (i = 0; i < h; i++)
	{
		for (t = 0; t < w; t++)
		{
			int dx = x + (piece_w * t);
			int dy = y + (piece_h * i);
			int dw, dh;
			if ((dx + piece_w) > screen_w)
				dw = piece_w - ((dx + piece_w) - screen_w);
			else
				dw = piece_w;
			if ((dy + piece_h) > screen_h)
				dh = piece_h - ((dy + piece_h) - screen_h);
			else
				dh = piece_h;

			float u2 = (float)dw / (float)piece_w;
			float v2 = (float)dh / (float)piece_h;
			DrawSimpleBitmap(bm_array[i * w + t], dx, dy);
		}
	}
	SetZBufferState(1);
}

//	given a chunked bitmap, renders it.scaled
void GL4Renderer::DrawScaledChunkedBitmap(chunked_bitmap* chunk, int x, int y, int neww, int newh, ubyte alpha)
{
	int* bm_array = chunk->bm_array;
	int w = chunk->w;
	int h = chunk->h;
	int piece_w;
	int piece_h;
	int screen_w, screen_h;
	int i, t;

	float scalew, scaleh;

	scalew = ((float)neww) / ((float)chunk->pw);
	scaleh = ((float)newh) / ((float)chunk->ph);
	piece_w = scalew * ((float)bm_w(bm_array[0], 0));
	piece_h = scaleh * ((float)bm_h(bm_array[0], 0));
	GetProjectionParameters(&screen_w, &screen_h);
	SetOverlayType(OT_NONE);
	SetLighting(LS_NONE);
	SetColorModel(CM_MONO);
	SetZBufferState(0);
	SetAlphaType(AT_CONSTANT_TEXTURE);
	SetAlphaValue(alpha);
	SetWrapType(WT_WRAP);
	for (i = 0; i < h; i++)
	{
		for (t = 0; t < w; t++)
		{
			int dx = x + (piece_w * t);
			int dy = y + (piece_h * i);
			int dw, dh;
			if ((dx + piece_w) > screen_w)
				dw = piece_w - ((dx + piece_w) - screen_w);
			else
				dw = piece_w;
			if ((dy + piece_h) > screen_h)
				dh = piece_h - ((dy + piece_h) - screen_h);
			else
				dh = piece_h;

			float u2 = (float)dw / (float)piece_w;
			float v2 = (float)dh / (float)piece_h;
			DrawScaledBitmap(dx, dy, dx + dw, dy + dh, bm_array[i * w + t], 0, 0, u2, v2);

		}
	}
	SetZBufferState(1);
}

// Draws a simple bitmap at the specified x,y location
void GL4Renderer::DrawSimpleBitmap(int bm_handle, int x, int y)
{
	SetAlphaType(AT_CONSTANT_TEXTURE);
	SetAlphaValue(255);
	SetLighting(LS_NONE);
	SetColorModel(CM_MONO);
	SetOverlayType(OT_NONE);
	SetFiltering(0);
	DrawScaledBitmap(x, y, x + bm_w(bm_handle, 0), y + bm_h(bm_handle, 0), bm_handle, 0, 0, 1, 1);
	SetFiltering(1);
}
