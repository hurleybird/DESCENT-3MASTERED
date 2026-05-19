#version 450 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec4 color;
layout(location = 2) in vec3 world_position;
layout(location = 3) in int material_pages;
layout(location = 4) in vec2 uv;
layout(location = 5) in vec2 uv2;
layout(location = 6) in vec2 uv_payload;

out vec4 outcolor;
out vec3 outuv;
out vec3 outuv2;
out vec4 outworld;
out float outdepth;
flat out int outlmpage;
flat out int outtexpage;

void main()
{
	gl_Position = vec4(position, 1.0);
	outcolor = color;
	outuv = vec3(uv, uv_payload.x);
	outuv2 = vec3(uv2, uv_payload.x);
	outworld = vec4(world_position, uv_payload.x);
	outdepth = uv_payload.y;
	outlmpage = material_pages & 255;
	outtexpage = material_pages >> 8;
}
