#version 450 core

uniform sampler2DArray colortexture;
uniform sampler2DArray lightmaptexture;
uniform int terrain_dynamic_light_count[4];
uniform vec3 terrain_dynamic_light_positions[32];
uniform vec3 terrain_dynamic_light_colors[32];
uniform float terrain_dynamic_light_radii[32];
uniform vec3 terrain_dynamic_light_directions[32];
uniform float terrain_dynamic_light_dot_ranges[32];
uniform int terrain_dynamic_light_directional[32];

layout(std140) uniform TerrainFogBlock
{
	vec4 color;
	float start_dist;
	float end_dist;
} fog;

in vec4 outcolor;
in vec3 outuv;
in vec3 outuv2;
in vec3 outworld;
in float outdepth;
flat in int outlmpage;
flat in int outtexpage;

layout(location = 0) out vec4 color;
layout(location = 2) out vec4 post_mask;
layout(location = 3) out float ao_class;

vec3 ApplyDynamicLightmapLighting(vec3 lightmap_color)
{
	vec3 face_normal = vec3(0.0, 1.0, 0.0);
	vec3 dynamic_color = vec3(0.0);
	int lightmap_page = clamp(outlmpage, 0, 3);
	int dynamic_light_count = terrain_dynamic_light_count[lightmap_page];
	int light_base = lightmap_page * 8;
	if (dynamic_light_count == 0)
		return lightmap_color;

	for (int i = 0; i < 8; i++)
	{
		if (i >= dynamic_light_count)
			break;

		int light_index = light_base + i;
		vec3 light_delta = outworld - terrain_dynamic_light_positions[light_index];
		float radius = max(terrain_dynamic_light_radii[light_index], 0.0001);
		float distance = length(light_delta);
		vec3 light_vector = (distance > 0.0001) ? light_delta / distance : face_normal;
		float scalar = 1.0 - (distance / radius);
		if (scalar <= 0.0)
			continue;

		if (terrain_dynamic_light_directional[light_index] != 0)
		{
			vec3 light_direction = normalize(terrain_dynamic_light_directions[light_index]);
			float direction_dot = dot(light_vector, light_direction);
			float dot_range = terrain_dynamic_light_dot_ranges[light_index];
			if (direction_dot < dot_range)
				continue;

			scalar *= (direction_dot - dot_range) / max(1.0 - dot_range, 0.0001);
		}

		dynamic_color += terrain_dynamic_light_colors[light_index] * scalar;
	}

	return clamp(lightmap_color + dynamic_color, vec3(0.0), vec3(1.0));
}

void main()
{
	vec4 basecolor = texture(colortexture, vec3(outuv.xy / outuv.z, float(max(outtexpage, 0))));
	vec4 lmcolor = texture(lightmaptexture, vec3(outuv2.xy / outuv2.z, float(clamp(outlmpage, 0, 3))));
	lmcolor.rgb = ApplyDynamicLightmapLighting(lmcolor.rgb);
	vec4 litcolor = basecolor * lmcolor * outcolor;
	float fog_start = clamp(1.0 - (1.0 / max(fog.start_dist, 0.0001)), 0.0, 1.0);
	float fog_end = clamp(1.0 - (1.0 / max(fog.end_dist, 0.0001)), 0.0, 1.0);
	float fog_amount = clamp((outdepth - fog_start) / max(fog_end - fog_start, 0.0001), 0.0, 1.0);
	color = vec4(mix(litcolor.rgb, fog.color.rgb, fog_amount), litcolor.a);
	post_mask = vec4(0.0, fog_amount, 0.0, 1.0);
	ao_class = 1.0 / 255.0;
}
