//the preprocessor definitions, including #version, are automatically applied
//at compile time for the generic shader

layout(std140) uniform CommonBlock
{
	mat4 projection;
	mat4 modelview;
} commons;

layout(location = 0) in vec3 position;
layout(location = 1) in vec4 color;
layout(location = 2) in vec3 uv;
layout(location = 3) in vec3 uv2;
layout(location = 4) in vec4 normal;
layout(location = 5) in vec4 motion_world_position;
layout(location = 6) in vec4 motion_previous_world_position;
layout(location = 15) in int retained_source_vertex;
#if defined(USE_SPECULAR)
layout(location = 7) in vec4 field_specular_center0;
layout(location = 8) in vec4 field_specular_center1;
layout(location = 9) in vec4 field_specular_center2;
layout(location = 10) in vec4 field_specular_center3;
layout(location = 11) in vec4 field_specular_color0;
layout(location = 12) in vec4 field_specular_color1;
layout(location = 13) in vec4 field_specular_color2;
layout(location = 14) in vec4 field_specular_color3;
#endif

uniform int phong_enabled;
uniform vec3 phong_light_direction;
uniform int dynamic_light_count;
uniform int retained_mode;
uniform mat4 retained_transform;
uniform mat4 retained_modelview;
uniform mat4 retained_current_world;
uniform mat4 retained_previous_world;
uniform vec2 retained_uv_offset;
uniform vec2 retained_uv2_scale;
uniform vec3 retained_base_color;
uniform float retained_depth_bias;
uniform int retained_legacy_depth;
uniform int retained_lighting_mode;
uniform int retained_vertex_alpha;
uniform float retained_alpha_scale;
uniform int retained_effect_mode;
uniform float retained_effect_alpha_scale;
uniform vec3 retained_fog_plane;
uniform float retained_fog_distance;
uniform float retained_fog_eye_distance;
uniform float retained_fog_depth;
uniform vec3 retained_specular_view_position;
uniform vec3 retained_specular_light_position;
uniform float retained_specular_scalar;
uniform int retained_specular_smooth;
uniform int retained_deform_enabled;
uniform int retained_deform_mode;
uniform uint retained_deform_seed;
uniform float retained_deform_range;
uniform vec3 retained_deform_direction;
uniform int retained_custom_clip_enabled;
uniform vec3 retained_custom_clip_point;
uniform vec3 retained_custom_clip_plane;
uniform vec3 retained_custom_clip_scale;
uniform int retained_near_clip_enabled;
uniform int retained_far_clip_enabled;
uniform float retained_far_clip_z;
uniform int retained_per_pixel_specular_payload;

out vec4 outcolor;
// Legacy g3 draws submit already-projected vertices, so vertex alpha is
// interpolated affinely in screen space. Retained effect draws use a normal
// perspective projection and need a separate noperspective payload to retain
// that contract without changing ordinary retained lighting interpolation.
noperspective out float out_retained_effect_alpha;
out vec4 outnormal;
out vec4 out_motion_world_position;
out vec4 out_motion_previous_world_position;
out vec4 out_room_fog_world_position;
#if defined(USE_SPECULAR)
out vec4 out_field_specular_centers[4];
out vec4 out_field_specular_colors[4];
#endif
#if defined(USE_TEXTURING)
out vec3 outuv;
#if defined(USE_LIGHTMAP)
out vec3 outuv2;
#endif
#endif
#if defined(USE_FOG)
out vec3 outpt;
#endif

uint AdvanceVisualRandom(uint state, uint delta)
{
	// Jump ahead in the engine's 32-bit LCG in O(log vertex index).  Unsigned
	// overflow is the required modulo-2^32 arithmetic.
	uint accumulated_multiplier = 1u;
	uint accumulated_increment = 0u;
	uint current_multiplier = 214013u;
	uint current_increment = 2531011u;
	while (delta != 0u)
	{
		if ((delta & 1u) != 0u)
		{
			accumulated_multiplier *= current_multiplier;
			accumulated_increment = accumulated_increment * current_multiplier + current_increment;
		}
		current_increment = (current_multiplier + 1u) * current_increment;
		current_multiplier *= current_multiplier;
		delta >>= 1u;
	}
	return accumulated_multiplier * state + accumulated_increment;
}

vec3 RetainedDeformedPosition()
{
	if (retained_deform_enabled == 0)
		return position;
	uint state = AdvanceVisualRandom(retained_deform_seed, uint(retained_source_vertex) + 1u);
	int random_value = int((state >> 16u) & 0x7fffu);
	float signed_value = float((random_value % 1000) - 500) / 500.0;
	if (retained_deform_mode == 2)
		return position + retained_deform_direction * (retained_deform_range * signed_value);
	return position * (1.0 + retained_deform_range * signed_value);
}

void main()
{
	if (retained_mode != 0)
	{
		vec4 local_position = vec4(RetainedDeformedPosition(), 1.0);
		vec3 view_position = vec3(0.0);
		#if defined(USE_FOG)
			vec3 retained_gl_view_position = (retained_modelview * local_position).xyz;
			view_position = vec3(retained_gl_view_position.xy, -retained_gl_view_position.z);
		#else
			if (retained_effect_mode != 0 || retained_custom_clip_enabled != 0 ||
				retained_near_clip_enabled != 0 || retained_far_clip_enabled != 0 ||
				retained_per_pixel_specular_payload != 0)
			{
				vec3 retained_gl_view_position = (retained_modelview * local_position).xyz;
				view_position = vec3(retained_gl_view_position.xy, -retained_gl_view_position.z);
			}
		#endif
		// gTransformModelView includes the legacy Z bias in its translation.
		// CPU g3 view coordinates and clip codes do not, so remove it from the
		// retained view-space payload used by fog, clipping, and specular lighting.
		view_position.z += retained_depth_bias;
		// Legacy g3 points use a positive forward Z, while the GL model-view
		// matrix uses the conventional negative forward Z.  Keep the retained
		// calculations in the coordinate system expected by the legacy fog
		// equations.
		gl_Position = retained_transform * local_position;
		// CPU-built legacy vertices use an infinite-far depth mapping (window
		// depth 1 - 1/z).  Retained legacy geometry must use the same mapping so
		// it shares a depth buffer exactly with immediate draws.
		if (retained_legacy_depth != 0)
		{
			float retained_eye_z = max(gl_Position.w + retained_depth_bias, 0.0001);
			float retained_depth_value = clamp(1.0 - (1.0 / retained_eye_z), 0.0, 1.0);
			gl_Position.z = (retained_depth_value * 2.0 - 1.0) * gl_Position.w;
		}
		if (retained_custom_clip_enabled != 0)
		{
			vec3 clip_scale = retained_custom_clip_scale;
			clip_scale.x = abs(clip_scale.x) < 0.000001 ? (clip_scale.x < 0.0 ? -0.000001 : 0.000001) : clip_scale.x;
			clip_scale.y = abs(clip_scale.y) < 0.000001 ? (clip_scale.y < 0.0 ? -0.000001 : 0.000001) : clip_scale.y;
			clip_scale.z = abs(clip_scale.z) < 0.000001 ? (clip_scale.z < 0.0 ? -0.000001 : 0.000001) : clip_scale.z;
			vec3 clip_delta = (view_position - retained_custom_clip_point) / clip_scale;
			// The CPU clipper retains points down to -0.005 from the plane.
			gl_ClipDistance[0] = dot(clip_delta, retained_custom_clip_plane) + 0.005;
		}
		else
		{
			gl_ClipDistance[0] = 1.0;
		}
		gl_ClipDistance[1] = retained_near_clip_enabled != 0 ? view_position.z : 1.0;
		gl_ClipDistance[2] = retained_far_clip_enabled != 0 ?
			retained_far_clip_z - view_position.z : 1.0;
		vec3 vertex_color = retained_base_color;
		if (retained_lighting_mode == 1)
		{
			// Match SetPolymodelGouraudPointLighting. Imported model normals and
			// light directions are already normalized by the model pipeline.
			float light = clamp((-dot(phong_light_direction, normal.xyz) + 1.0) * 0.5, 0.0, 1.0);
			vertex_color *= light;
		}
		float vertex_alpha = retained_vertex_alpha != 0 ? color.a : 1.0;
		if (retained_effect_mode == 3)
		{
			vec3 specular_normal = retained_specular_smooth != 0 ? normal.xyz : motion_world_position.xyz;
			vec3 to_view = normalize(retained_specular_view_position - local_position.xyz);
			vec3 incident = normalize(local_position.xyz - retained_specular_light_position);
			incident -= 2.0 * dot(incident, specular_normal) * specular_normal;
			float specular_dot = clamp(dot(to_view, incident), 0.0, 1.0);
			// Match the legacy 4096-entry x^4 table, including its integer lookup.
			float table_input = floor(specular_dot * 4095.0) / 4095.0;
			float table_square = table_input * table_input;
			vertex_alpha = table_square * table_square * retained_specular_scalar;
		}
		if (retained_effect_mode == 1 || retained_effect_mode == 2 ||
			retained_effect_mode == 4 || retained_effect_mode == 5)
		{
			float magnitude = view_position.z;
			if (retained_effect_mode == 5)
			{
				// Static rooms are retained directly in world coordinates.
				magnitude = dot(local_position.xyz, retained_fog_plane) + retained_fog_distance;
			}
			else if (retained_effect_mode == 2 || retained_effect_mode == 4)
			{
				float distance_to_plane = retained_effect_mode == 4 ?
					dot(local_position.xyz, retained_fog_plane) + retained_fog_distance :
					dot(view_position, retained_fog_plane) + retained_fog_distance;
				float denominator = retained_fog_eye_distance - distance_to_plane;
				if (abs(denominator) < 0.0001)
					magnitude = 0.0;
				else
				{
					float t = retained_fog_eye_distance / denominator;
					magnitude = max(view_position.z - (t * view_position).z, 0.0);
				}
			}
			vertex_alpha = clamp(magnitude / max(retained_fog_depth, 0.0001), 0.0, 1.0);
		}
		outcolor = vec4(vertex_color,
			vertex_alpha * retained_alpha_scale * retained_effect_alpha_scale);
		out_retained_effect_alpha = outcolor.a;
		vec4 retained_world_position = retained_current_world * local_position;
		out_room_fog_world_position = retained_world_position;
		// The legacy generic stream overloads this attribute: Phong draws carry a
		// normal, while dynamically-lightmapped draws carry perspective-correct
		// world position.  Retained geometry must make the same selection.
		if (retained_per_pixel_specular_payload != 0)
		{
			vec3 retained_view_normal = mat3(retained_modelview) * normal.xyz;
			outnormal = vec4(normalize(vec3(retained_view_normal.xy, -retained_view_normal.z)), 1.0);
			out_motion_world_position = vec4(view_position, 1.0);
		}
		else
		{
			outnormal = retained_lighting_mode == 2 ? vec4(normal.xyz, 1.0) :
				(dynamic_light_count > 0 ? retained_world_position : vec4(0.0, 0.0, 0.0, -1.0));
			out_motion_world_position = retained_world_position;
		}
		out_motion_previous_world_position = retained_previous_world * local_position;
		#if defined(USE_SPECULAR)
			for (int i = 0; i < 4; i++)
			{
				vec4 source = i == 0 ? field_specular_center0 :
					(i == 1 ? field_specular_center1 :
					(i == 2 ? field_specular_center2 : field_specular_center3));
				if (retained_per_pixel_specular_payload != 0 && source.w > 0.0)
				{
					vec3 retained_view_source = (retained_modelview * vec4(source.xyz, 1.0)).xyz;
					out_field_specular_centers[i] = vec4(retained_view_source.xy, -retained_view_source.z, 1.0);
					out_field_specular_colors[i] = i == 0 ? field_specular_color0 :
						(i == 1 ? field_specular_color1 :
						(i == 2 ? field_specular_color2 : field_specular_color3));
				}
				else
				{
					out_field_specular_centers[i] = vec4(0.0);
					out_field_specular_colors[i] = vec4(0.0);
				}
			}
		#endif
		#if defined(USE_TEXTURING)
			// Legacy g3 texture vertices use 1 / (z + Z_bias). Hardware
			// perspective interpolation otherwise supplies 1 / z, which subtly
			// shifts textures on biased custom submodels such as cockpit arms.
			float uv_perspective_scale = retained_legacy_depth != 0 ?
				gl_Position.w / max(gl_Position.w + retained_depth_bias, 0.0001) : 1.0;
			outuv = vec3((uv.xy + retained_uv_offset) * uv_perspective_scale,
				uv_perspective_scale);
			#if defined(USE_LIGHTMAP)
				outuv2 = vec3(uv2.xy * retained_uv2_scale * uv_perspective_scale,
					uv_perspective_scale);
			#endif
		#endif
		#if defined(USE_FOG)
			// The generic fog shader historically receives the non-linear depth
			// stored in a g3 vertex, not linear eye-space distance.  Supplying the
			// latter saturates every retained outdoor model to full terrain fog.
			float fog_depth = clamp(1.0 - (1.0 / max(view_position.z, 0.0001)), 0.0, 1.0);
			outpt = vec3(0.0, 0.0, -fog_depth);
		#endif
		return;
	}

	gl_Position = commons.projection * commons.modelview * vec4(position, 1.0);
	outcolor = color;
	out_retained_effect_alpha = color.a;
	#if defined(USE_SPECULAR)
		outnormal = normal;
	#else
		outnormal = normal;
	#endif
	out_motion_world_position = motion_world_position;
	out_motion_previous_world_position = motion_previous_world_position;
	// Static immediate room draws do not consume the per-vertex previous-motion
	// payload. The renderer uses it to carry the original world position when
	// local room fog is active, including CPU-clipped boundary triangles.
	out_room_fog_world_position = motion_previous_world_position;
	#if defined(USE_SPECULAR)
		out_field_specular_centers[0] = field_specular_center0;
		out_field_specular_centers[1] = field_specular_center1;
		out_field_specular_centers[2] = field_specular_center2;
		out_field_specular_centers[3] = field_specular_center3;
		out_field_specular_colors[0] = field_specular_color0;
		out_field_specular_colors[1] = field_specular_color1;
		out_field_specular_colors[2] = field_specular_color2;
		out_field_specular_colors[3] = field_specular_color3;
	#endif
	#if defined(USE_TEXTURING)
		outuv = uv;
		#if defined(USE_LIGHTMAP)
			outuv2 = uv2;
		#endif
	#endif
	#if defined(USE_FOG)
		outpt = position;
	#endif
}
