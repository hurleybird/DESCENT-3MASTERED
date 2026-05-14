#version 330 core

layout(std140) uniform CommonBlock
{
	mat4 projection;
	mat4 modelview;
} commons;

layout(location = 0) in vec3 position;
layout(location = 2) in vec3 normal;
layout(location = 4) in vec2 uv;
layout(location = 5) in vec2 uv2;

out vec2 outuv;
out vec2 outuv2;
out vec3 outworld;
out vec3 outnormal;
out float outeye_z;

const float TERRAIN_NEAR_Z = 0.05;
const float TERRAIN_FAR_Z = 60000.0;

void main()
{
	vec4 temp = commons.modelview * vec4(position, 1.0);
	float clip_c = -((TERRAIN_FAR_Z + TERRAIN_NEAR_Z) / (TERRAIN_FAR_Z - TERRAIN_NEAR_Z));
	float clip_d = -((2.0 * TERRAIN_FAR_Z * TERRAIN_NEAR_Z) / (TERRAIN_FAR_Z - TERRAIN_NEAR_Z));
	gl_Position = vec4(commons.projection[0][0] * temp.x,
		commons.projection[1][1] * temp.y,
		(clip_c * temp.z) + clip_d,
		-temp.z);
	outuv = uv;
	outuv2 = uv2;
	outworld = position;
	outnormal = normal;
	outeye_z = max(-temp.z, 0.0001);
}
