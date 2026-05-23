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
