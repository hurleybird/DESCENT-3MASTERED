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
layout(location = 5) in vec2 motion_velocity;
layout(location = 6) in vec4 motion_world_position;

uniform int phong_enabled;

out vec4 outcolor;
out vec4 outnormal;
out vec2 out_motion_velocity;
out vec4 out_motion_world_position;
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
	outnormal = normal;
	out_motion_velocity = motion_velocity;
	out_motion_world_position = motion_world_position;
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
