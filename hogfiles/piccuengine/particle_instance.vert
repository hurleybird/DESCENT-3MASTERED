// The preprocessor definitions, including #version, are automatically applied
// at compile time for the generic shader fragment stage.

layout(std140) uniform CommonBlock
{
	mat4 projection;
	mat4 modelview;
} commons;

layout(location = 0) in vec4 instance_screen_z;
layout(location = 1) in vec4 instance_size_rot;
layout(location = 2) in vec4 instance_uv;
layout(location = 3) in vec4 instance_color;

out vec4 outcolor;
out vec4 outnormal;
out vec4 out_motion_world_position;
out vec4 out_motion_previous_world_position;
#if defined(USE_TEXTURING)
out vec3 outuv;
#endif
#if defined(USE_FOG)
out vec3 outpt;
#endif

float DepthFromEyeZ(float z)
{
	float clamped_z = max(z, 0.0001);
	return clamp(1.0 - (1.0 / clamped_z), 0.0, 1.0);
}

void main()
{
	vec2 corner;
	vec2 uv;
	if (gl_VertexID == 0)
	{
		corner = vec2(-1.0, -1.0);
		uv = instance_uv.xy;
	}
	else if (gl_VertexID == 1)
	{
		corner = vec2(1.0, -1.0);
		uv = instance_uv.zy;
	}
	else if (gl_VertexID == 2)
	{
		corner = vec2(-1.0, 1.0);
		uv = instance_uv.xw;
	}
	else
	{
		corner = vec2(1.0, 1.0);
		uv = instance_uv.zw;
	}

	vec2 scaled = corner * instance_size_rot.xy;
	float s = instance_size_rot.z;
	float c = instance_size_rot.w;
	vec2 rotated = vec2(
		(scaled.x * c) - (scaled.y * s),
		(scaled.x * s) + (scaled.y * c));
	vec3 position = vec3(instance_screen_z.xy + rotated, -DepthFromEyeZ(instance_screen_z.w));

	gl_Position = commons.projection * commons.modelview * vec4(position, 1.0);
	outcolor = instance_color;
	outnormal = vec4(0.0, 0.0, 0.0, DepthFromEyeZ(instance_screen_z.w));
	out_motion_world_position = vec4(0.0);
	out_motion_previous_world_position = vec4(0.0);
#if defined(USE_TEXTURING)
	float texw = 1.0 / max(instance_screen_z.w, 0.0001);
	outuv = vec3(uv * texw, texw);
#endif
#if defined(USE_FOG)
	outpt = position;
#endif
}
