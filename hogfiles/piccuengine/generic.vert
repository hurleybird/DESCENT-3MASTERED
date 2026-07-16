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
uniform int retained_mode;
uniform mat4 retained_transform;
uniform mat4 retained_modelview;
uniform mat4 retained_current_world;
uniform mat4 retained_previous_world;
uniform vec2 retained_uv_offset;
uniform vec3 retained_base_color;
uniform int retained_lighting_mode;
uniform int retained_vertex_alpha;
uniform float retained_alpha_scale;
uniform int retained_effect_mode;
uniform vec3 retained_fog_plane;
uniform float retained_fog_distance;
uniform float retained_fog_eye_distance;
uniform float retained_fog_depth;

out vec4 outcolor;
out vec4 outnormal;
out vec4 out_motion_world_position;
out vec4 out_motion_previous_world_position;
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

void main()
{
	if (retained_mode != 0)
	{
		vec4 local_position = vec4(position, 1.0);
		vec3 view_position = vec3(0.0);
		#if defined(USE_FOG)
			vec3 retained_gl_view_position = (retained_modelview * local_position).xyz;
			view_position = vec3(retained_gl_view_position.xy, -retained_gl_view_position.z);
		#else
			if (retained_effect_mode != 0)
			{
				vec3 retained_gl_view_position = (retained_modelview * local_position).xyz;
				view_position = vec3(retained_gl_view_position.xy, -retained_gl_view_position.z);
			}
		#endif
		// Legacy g3 points use a positive forward Z, while the GL model-view
		// matrix uses the conventional negative forward Z.  Keep the retained
		// calculations in the coordinate system expected by the legacy fog
		// equations.
		gl_Position = retained_transform * local_position;
		vec3 vertex_color = retained_base_color;
		if (retained_lighting_mode == 1)
		{
			vec3 light_direction = normalize(phong_light_direction);
			float light = clamp((-dot(light_direction, normalize(normal.xyz)) + 1.0) * 0.5, 0.0, 1.0);
			vertex_color *= light;
		}
		float vertex_alpha = retained_vertex_alpha != 0 ? color.a : 1.0;
		if (retained_effect_mode == 1 || retained_effect_mode == 2)
		{
			float magnitude = view_position.z;
			if (retained_effect_mode == 2)
			{
				float distance_to_plane = dot(view_position, retained_fog_plane) + retained_fog_distance;
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
		outcolor = vec4(vertex_color, vertex_alpha * retained_alpha_scale);
		outnormal = retained_lighting_mode == 2 ? vec4(normal.xyz, 1.0) : vec4(0.0, 0.0, 0.0, -1.0);
		out_motion_world_position = retained_current_world * local_position;
		out_motion_previous_world_position = retained_previous_world * local_position;
		#if defined(USE_SPECULAR)
			out_field_specular_centers[0] = vec4(0.0);
			out_field_specular_centers[1] = vec4(0.0);
			out_field_specular_centers[2] = vec4(0.0);
			out_field_specular_centers[3] = vec4(0.0);
			out_field_specular_colors[0] = vec4(0.0);
			out_field_specular_colors[1] = vec4(0.0);
			out_field_specular_colors[2] = vec4(0.0);
			out_field_specular_colors[3] = vec4(0.0);
		#endif
		#if defined(USE_TEXTURING)
			outuv = vec3(uv.xy + retained_uv_offset, 1.0);
			#if defined(USE_LIGHTMAP)
				outuv2 = vec3(uv2.xy, 1.0);
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
	#if defined(USE_SPECULAR)
		outnormal = normal;
	#else
		outnormal = normal;
	#endif
	out_motion_world_position = motion_world_position;
	out_motion_previous_world_position = motion_previous_world_position;
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
