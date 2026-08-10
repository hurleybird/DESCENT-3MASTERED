#version 450 core

layout(std140) uniform CommonBlock
{
	mat4 projection;
	mat4 modelview;
} commons;

layout(location = 0) in uvec4 cell_packed;
layout(location = 1) in vec4 cell_height;

uniform vec4 terrain_world_clip_planes[4];

out vec4 outcolor;
out vec3 outuv;
out vec3 outuv2;
out vec4 outworld;
flat out int outlmpage;
flat out int outtexpage;

// The engine's projection maps its clockwise world-space terrain winding to
// OpenGL's counter-clockwise window-space front face.  Submit that winding
// directly so ordinary back-face culling matches the legacy visibility test.
const uint terrain_corners[6] = uint[6](0u, 3u, 1u, 3u, 2u, 1u);

vec3 CellPosition(uint segment, vec4 height, uint corner)
{
	uint cell_x = segment & 255u;
	uint cell_z = segment >> 8u;
	uint x = cell_x;
	uint z = cell_z;
	float y = height.w;

	if (corner == 0u)
	{
		z = cell_z + 1u;
		y = height.x;
	}
	else if (corner == 1u)
	{
		x = cell_x + 1u;
		z = cell_z + 1u;
		y = height.y;
	}
	else if (corner == 2u)
	{
		x = cell_x + 1u;
		y = height.z;
	}

	return vec3(float(x) * 16.0, y, float(z) * 16.0);
}

float TerrainFacing(uint segment, vec4 height, uint triangle)
{
	uint corner0 = triangle == 0u ? 0u : 3u;
	uint corner1 = 1u;
	uint corner2 = triangle == 0u ? 3u : 2u;
	vec3 view0 = (commons.modelview *
		vec4(CellPosition(segment, height, corner0), 1.0)).xyz;
	vec3 view1 = (commons.modelview *
		vec4(CellPosition(segment, height, corner1), 1.0)).xyz;
	vec3 view2 = (commons.modelview *
		vec4(CellPosition(segment, height, corner2), 1.0)).xyz;
	return dot(cross(view1 - view0, view2 - view1), view1);
}

vec2 BaseUv(uint segment, uint rotation, uint corner)
{
	uint cell_x = segment & 255u;
	uint cell_z = segment >> 8u;
	float sub_x = float(cell_x & 7u);
	float sub_z = float(7u - (cell_z & 7u));

	if (corner == 1u || corner == 2u)
		sub_x += 1.0;
	if (corner == 2u || corner == 3u)
		sub_z += 1.0;

	float x = sub_x * 0.125;
	float y = sub_z * 0.125;
	float tile = float(rotation >> 4u);
	uint orientation = rotation & 15u;

	vec2 uv;
	if (orientation == 1u)
		uv = vec2(1.0 - y, x);
	else if (orientation == 2u)
		uv = vec2(1.0 - x, 1.0 - y);
	else if (orientation == 3u)
		uv = vec2(y, 1.0 - x);
	else
		uv = vec2(x, y);

	return uv * tile;
}

vec2 LightmapUv(uint segment, uint corner)
{
	uint cell_x = segment & 255u;
	uint cell_z = segment >> 8u;
	vec2 uv = vec2(float(cell_x & 127u) * 0.0078125,
		float(128u - ((cell_z & 127u) + 1u)) * 0.0078125);

	if (corner == 1u || corner == 2u)
		uv.x += 0.0078125;
	if (corner == 2u || corner == 3u)
		uv.y += 0.0078125;
	return uv;
}

void main()
{
	uint corner = terrain_corners[gl_VertexID];
	uint segment = cell_packed.x;
	uint rotation = cell_packed.y & 255u;
	uint clip_plane_id = cell_packed.z >> 16u;
	vec3 world_position = CellPosition(segment, cell_height, corner);
	// Match the original terrain renderer's view-space face test.  A native
	// cull distance rejects the complete triangle before rasterization while
	// avoiding screen-space winding ambiguities for cells crossing the eye
	// plane (the case that otherwise opens near-camera terrain holes).
	// CommonBlock's view transform changes handedness relative to the engine's
	// g3 rotated coordinates, so the corresponding visible sign is positive.
	uint triangle = uint(gl_VertexID) / 3u;
	bool visible = TerrainFacing(segment, cell_height, triangle) > 0.0;
	gl_CullDistance[0] = visible ? 1.0 : -1.0;

	vec4 clip_position =
		commons.projection * commons.modelview * vec4(world_position, 1.0);
	float raw_eye_z = clip_position.w;
	float eye_z = max(raw_eye_z, 0.01);
	// Clip cells at a small positive eye plane before perspective division.
	// Reject geometry crossing behind the eye before perspective division;
	// this prevents either holes or unbounded triangles when the camera is
	// very close to the heightfield.
	gl_ClipDistance[1] = raw_eye_z - 0.01;
	// Preserve the existing infinite-far depth contract exactly so terrain and
	// room geometry continue to share depth without seams or z-fighting.
	clip_position.z = raw_eye_z - 2.0;
	gl_Position = clip_position;

	if (clip_plane_id > 0u && clip_plane_id <= 4u)
	{
		vec4 plane = terrain_world_clip_planes[clip_plane_id - 1u];
		gl_ClipDistance[0] = dot(world_position, plane.xyz) + plane.w + 0.005;
	}
	else
	{
		gl_ClipDistance[0] = 1.0;
	}

	outcolor = vec4(1.0);
	outuv = vec3(BaseUv(segment, rotation, corner), 1.0);
	outuv2 = vec3(LightmapUv(segment, corner), 1.0);
	outworld = vec4(world_position, 1.0);
	outlmpage = int((cell_packed.y >> 8u) & 255u);
	outtexpage = int(cell_packed.y >> 16u);
}
