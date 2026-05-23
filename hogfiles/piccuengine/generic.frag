//the preprocessor definitions, including #version, are automatically applied
//at compile time for the generic shader

layout(std140) uniform TerrainFogBlock
{
	vec4 color;
	float start_dist;
	float end_dist;
} fog;

#if defined(USE_TEXTURING)
uniform sampler2D colortexture;
#if defined(USE_LIGHTMAP)
uniform sampler2D lightmaptexture;
#endif
#if defined(USE_SPECULAR)
uniform sampler2D specularmasktexture;
#endif
#endif

uniform int phong_enabled;
uniform vec3 phong_light_direction;
uniform int dynamic_light_count;
uniform vec3 dynamic_face_normal;
uniform vec3 dynamic_light_positions[8];
uniform vec3 dynamic_light_colors[8];
uniform float dynamic_light_radii[8];
uniform float dynamic_light_falloffs[8];
uniform vec3 dynamic_light_directions[8];
uniform float dynamic_light_dot_ranges[8];
uniform int dynamic_light_directional[8];
uniform int per_pixel_specular_enabled;
uniform float ao_suppression;
uniform float bloom_suppression;
uniform int ao_class_value;
uniform float ao_weight_value;
uniform int ao_capture_weight_mode;
uniform int post_mask_use_luminance;
uniform int cockpit_backing_enabled;
uniform float cockpit_backing_alpha;
uniform float cockpit_backing_darkness;
uniform int cockpit_scanlines_enabled;
uniform float cockpit_scanline_strength;
uniform float cockpit_scanline_spacing;
uniform float cockpit_scanline_thickness;
uniform float cockpit_scanline_phase;
uniform int motion_vector_mode;
uniform mat4 motion_vector_current_view_projection;
uniform mat4 motion_vector_previous_view_projection;
uniform int motion_vector_has_previous;
uniform int motion_vector_payload_type;
uniform uint motion_vector_object_id;
uniform int soft_particle_enabled;
uniform sampler2D soft_particle_depth;
uniform vec2 soft_particle_screen_size;
uniform float soft_particle_depth_range;

in vec4 outcolor;
in vec4 outnormal;
in vec4 out_motion_world_position;
in vec4 out_motion_previous_world_position;
#if defined(USE_TEXTURING)
in vec3 outuv;
#if defined(USE_LIGHTMAP)
in vec3 outuv2;
#endif
#endif
#if defined(USE_FOG)
in vec3 outpt;
#endif

layout(location = 0) out vec4 color;
layout(location = 1) out vec2 velocity;
layout(location = 2) out vec4 post_mask;
layout(location = 3) out float ao_class;
layout(location = 4) out uint motion_object_id;

#if defined(USE_SPECULAR)
struct specular
{
	vec4 bright_center;
	vec4 color;
};

layout(std140) uniform SpecularBlock
{
	int num_specular;
	int exponent;
	float strength;
	float lightmap_mix;
	float alpha_strength;
	float pad0;
	float debug_tint;
	float debug_authored;
	specular speculars[4];
} specular_data;

vec3 SpecularFromIncident(vec3 view_position, vec3 normal, vec3 incident, vec3 light_color, float scalar)
{
	float distance = length(incident);
	if (distance <= 0.0001 || scalar <= 0.0)
		return vec3(0.0);

	vec3 incident_norm = incident / distance;
	vec3 view_vec = normalize(-view_position);
	vec3 reflected = reflect(incident_norm, normal);
	float dotp = max(dot(view_vec, reflected), 0.0);
	if (dotp <= 0.0)
		return vec3(0.0);

	return pow(dotp, specular_data.exponent) * light_color * scalar;
}

vec3 ApplyPerPixelSpecular(vec3 lightmap_color)
{
	const float[4] weights = float[4](1.0, 0.66, 0.33, 0.25);
	vec3 view_position = out_motion_world_position.xyz / max(out_motion_world_position.w, 0.0001);
	vec3 normal = normalize(outnormal.xyz);
	vec3 specular_color = vec3(0.0);

	if (specular_data.pad0 > 0.5 && specular_data.num_specular > 0)
	{
		specular_color += SpecularFromIncident(view_position, normal, vec3(0.0, 0.0, -1.0),
			specular_data.speculars[0].color.xyz, 1.0);
	}
	else
	{
		for (int i = 0; i < 4; i++)
		{
			if (i >= specular_data.num_specular)
				break;

			vec3 light_position = specular_data.speculars[i].bright_center.xyz;
			specular_color += SpecularFromIncident(view_position, normal, view_position - light_position,
				specular_data.speculars[i].color.xyz, weights[i]);
		}
	}

	for (int i = 0; i < 8; i++)
	{
		if (i >= dynamic_light_count)
			break;

		vec3 light_position = dynamic_light_positions[i];
		vec3 incident = view_position - light_position;
		float radius = max(dynamic_light_radii[i], 0.0001);
		float distance = length(incident);
		if (distance <= 0.0001)
			continue;

		vec3 incident_norm = incident / distance;
		float scalar = 1.0 - (distance / radius);
		if (scalar <= 0.0)
			continue;
		scalar = pow(scalar, max(dynamic_light_falloffs[i], 0.0001));

		if (dynamic_light_directional[i] != 0)
		{
			vec3 light_direction = normalize(dynamic_light_directions[i]);
			float direction_dot = dot(incident_norm, light_direction);
			float dot_range = dynamic_light_dot_ranges[i];
			if (direction_dot < dot_range)
				continue;

			scalar *= (direction_dot - dot_range) / max(1.0 - dot_range, 0.0001);
		}

		specular_color += SpecularFromIncident(view_position, normal, incident,
			dynamic_light_colors[i], scalar);
	}

	vec3 lightmap_factor = mix(vec3(1.0), clamp(lightmap_color, vec3(0.0), vec3(1.0)),
		clamp(specular_data.lightmap_mix, 0.0, 1.0));
	return clamp(specular_color * lightmap_factor * specular_data.strength, vec3(0.0), vec3(1.0));
}
#endif

vec4 ApplyPhongLighting(vec4 source_color)
{
	if (phong_enabled == 0)
		return source_color;

	vec3 normal = normalize(outnormal.xyz / max(outnormal.w, 0.0001));
	vec3 light_direction = normalize(phong_light_direction);
	float light = clamp((-dot(light_direction, normal) + 1.0) * 0.5, 0.0, 1.0);
	return vec4(source_color.rgb * light, source_color.a);
}

vec3 ApplyDynamicLightmapLighting(vec3 lightmap_color)
{
	if (dynamic_light_count == 0)
		return lightmap_color;

	vec3 world_position = outnormal.xyz / max(outnormal.w, 0.0001);
	vec3 face_normal = normalize(dynamic_face_normal);
	vec3 dynamic_color = vec3(0.0);

	for (int i = 0; i < 8; i++)
	{
		if (i >= dynamic_light_count)
			break;

		vec3 light_delta = world_position - dynamic_light_positions[i];
		float radius = max(dynamic_light_radii[i], 0.0001);
		float distance = length(light_delta);
		vec3 light_vector = (distance > 0.0001) ? light_delta / distance : face_normal;
		float scalar = 1.0 - (distance / radius);
		if (scalar <= 0.0)
			continue;
		scalar = pow(scalar, max(dynamic_light_falloffs[i], 0.0001));

		if (dynamic_light_directional[i] != 0)
		{
			vec3 light_direction = normalize(dynamic_light_directions[i]);
			float direction_dot = dot(light_vector, light_direction);
			float dot_range = dynamic_light_dot_ranges[i];
			if (direction_dot < dot_range)
				continue;

			scalar *= (direction_dot - dot_range) / max(1.0 - dot_range, 0.0001);
		}

		dynamic_color += dynamic_light_colors[i] * scalar;
	}

	return clamp(lightmap_color + dynamic_color, vec3(0.0), vec3(1.0));
}

#if defined(USE_SPECULAR)
vec3 ApplyDynamicLightmapLightingFromView(vec3 lightmap_color, vec3 view_position)
{
	if (dynamic_light_count == 0)
		return lightmap_color;

	vec3 dynamic_color = vec3(0.0);

	for (int i = 0; i < 8; i++)
	{
		if (i >= dynamic_light_count)
			break;

		vec3 light_delta = view_position - dynamic_light_positions[i];
		float radius = max(dynamic_light_radii[i], 0.0001);
		float distance = length(light_delta);
		vec3 light_vector = (distance > 0.0001) ? light_delta / distance : vec3(0.0, 0.0, 1.0);
		float scalar = 1.0 - (distance / radius);
		if (scalar <= 0.0)
			continue;
		scalar = pow(scalar, max(dynamic_light_falloffs[i], 0.0001));

		if (dynamic_light_directional[i] != 0)
		{
			vec3 light_direction = normalize(dynamic_light_directions[i]);
			float direction_dot = dot(light_vector, light_direction);
			float dot_range = dynamic_light_dot_ranges[i];
			if (direction_dot < dot_range)
				continue;

			scalar *= (direction_dot - dot_range) / max(1.0 - dot_range, 0.0001);
		}

		dynamic_color += dynamic_light_colors[i] * scalar;
	}

	return clamp(lightmap_color + dynamic_color, vec3(0.0), vec3(1.0));
}
#endif

float SoftParticleEyeDepth(float depth)
{
	return 1.0 / max(1.0 - clamp(depth, 0.0, 0.9999), 0.0001);
}

void main()
{
	velocity = vec2(0.0);
	motion_object_id = 0u;
	if (motion_vector_mode == 2)
	{
		motion_object_id = motion_vector_object_id;
		if (motion_vector_payload_type == 1)
		{
			if (motion_vector_has_previous != 0 &&
				abs(out_motion_world_position.w) > 0.00001 &&
				abs(out_motion_previous_world_position.w) > 0.00001)
			{
				vec2 current_ndc = out_motion_world_position.xy / out_motion_world_position.w;
				vec2 previous_ndc = out_motion_previous_world_position.xy / out_motion_previous_world_position.w;
				velocity = (current_ndc - previous_ndc) * 0.5;
			}
		}
		else if (motion_vector_has_previous != 0 && abs(out_motion_world_position.w) > 0.00001)
		{
			vec3 world_position = out_motion_world_position.xyz / out_motion_world_position.w;
			vec3 previous_world_position = world_position;
			if (abs(out_motion_previous_world_position.w) > 0.00001)
				previous_world_position = out_motion_previous_world_position.xyz / out_motion_previous_world_position.w;
			vec4 current_clip = motion_vector_current_view_projection * vec4(world_position, 1.0);
			vec4 previous_clip = motion_vector_previous_view_projection * vec4(previous_world_position, 1.0);
			if (current_clip.w > 0.00001 && previous_clip.w > 0.00001)
			{
				vec2 current_ndc = current_clip.xy / current_clip.w;
				vec2 previous_ndc = previous_clip.xy / previous_clip.w;
				velocity = (current_ndc - previous_ndc) * 0.5;
			}
		}
	}
	vec4 vertex_color = ApplyPhongLighting(outcolor);
	if (ao_capture_weight_mode != 0)
	{
		float ao_weight = clamp(ao_weight_value * (1.0 - ao_suppression), 0.0, 1.0);
		if (ao_weight <= 0.0)
			discard;
		color = vec4(ao_weight, ao_weight, ao_weight, 1.0);
		post_mask = vec4(0.0, 0.0, 0.0, 1.0);
		ao_class = ao_weight;
		return;
	}

	float ao_mask = 0.0;
	float bloom_mask = 0.0;

	#if defined(USE_SPECULAR)
		vec4 base_color = texture(colortexture, outuv.xy / outuv.z);
		if (specular_data.debug_tint > 0.5)
		{
			float debug_mask = per_pixel_specular_enabled == 2 ?
				texture(specularmasktexture, outuv.xy / outuv.z).a : base_color.a;
			float mask_alpha = (debug_mask > 0.001) ?
				clamp(debug_mask * 3.0, 0.35, 0.9) : 0.0;
			vec3 tint_color = (specular_data.debug_authored > 0.5) ?
				vec3(1.0, 0.85, 0.0) : vec3(0.0, 1.0, 0.0);
			color = vec4(tint_color, mask_alpha);
		}
		else if (per_pixel_specular_enabled != 0)
		{
			vec3 lightmap_color = texture(lightmaptexture, outuv2.xy / outuv2.z).rgb;
			vec3 specular_color = ApplyPerPixelSpecular(lightmap_color);
			float mask_alpha = base_color.a;
			if (per_pixel_specular_enabled == 2)
			{
				mask_alpha = texture(specularmasktexture, outuv.xy / outuv.z).a;
				vec3 view_position = out_motion_world_position.xyz / max(out_motion_world_position.w, 0.0001);
				lightmap_color = ApplyDynamicLightmapLightingFromView(lightmap_color, view_position);
				vec3 lit_base = base_color.rgb * lightmap_color * vertex_color.rgb;
				float specular_alpha = clamp(mask_alpha * specular_data.alpha_strength, 0.0, 1.0);
				color = vec4(clamp(lit_base + specular_color * specular_alpha, vec3(0.0), vec3(1.0)),
					vertex_color.a);
			}
			else
			{
				float specular_alpha = clamp(mask_alpha * specular_data.alpha_strength, 0.0, 1.0);
				color = vec4(specular_color, clamp(specular_alpha * vertex_color.a, 0.0, 1.0));
			}
		}
		else
		{
			color = vec4(vertex_color.rgb, base_color.a * vertex_color.a);
		}
	#elif defined(USE_TEXTURING) && defined(USE_LIGHTMAP)
		vec4 lightmap_color = texture(lightmaptexture, outuv2.xy / outuv2.z);
		lightmap_color.rgb = ApplyDynamicLightmapLighting(lightmap_color.rgb);
		color = texture(colortexture, outuv.xy / outuv.z) * lightmap_color * vertex_color;
	#elif defined(USE_TEXTURING)
		color = texture(colortexture, outuv.xy / outuv.z) * vertex_color;
	#else
		color = vertex_color;
	#endif
	if (cockpit_backing_enabled != 0)
	{
		float darkness = clamp(cockpit_backing_darkness, 0.0, 1.0);
		if (cockpit_scanlines_enabled != 0 && cockpit_scanline_strength > 0.0)
		{
			float spacing = max(cockpit_scanline_spacing, 1.0);
			float row = mod(floor(gl_FragCoord.y + cockpit_scanline_phase), spacing);
			float stripe = step(row / spacing, clamp(cockpit_scanline_thickness, 0.0, 1.0));
			darkness = clamp(darkness + stripe * cockpit_scanline_strength, 0.0, 1.0);
		}
		color = vec4(vec3(0.0), clamp(cockpit_backing_alpha, 0.0, 1.0) * darkness);
	}
	if (soft_particle_enabled != 0 && color.a > 0.0 &&
		soft_particle_screen_size.x > 0.0 && soft_particle_screen_size.y > 0.0)
	{
		ivec2 depth_size = max(ivec2(soft_particle_screen_size), ivec2(1));
		ivec2 depth_pixel = clamp(ivec2(gl_FragCoord.xy), ivec2(0), depth_size - ivec2(1));
		float scene_depth = texelFetch(soft_particle_depth, depth_pixel, 0).r;
		if (scene_depth < 0.9999)
		{
			float scene_eye = SoftParticleEyeDepth(scene_depth);
			float particle_depth = outnormal.w >= 0.0 ? outnormal.w : gl_FragCoord.z;
			float particle_eye = SoftParticleEyeDepth(particle_depth);
			float fade = clamp((scene_eye - particle_eye) / max(soft_particle_depth_range, 0.0001), 0.0, 1.0);
			color.a *= fade;
		}
	}
	float suppression_alpha = clamp(color.a, 0.0, 1.0);
	if (post_mask_use_luminance != 0)
	{
		float visible = max(max(color.r, color.g), color.b);
		suppression_alpha *= clamp(visible, 0.0, 1.0);
	}
	ao_mask = clamp(ao_suppression * (1.0 - pow(1.0 - suppression_alpha, 3.0)), 0.0, 1.0);
	bloom_mask = clamp(bloom_suppression * (1.0 - pow(1.0 - suppression_alpha, 3.0)), 0.0, 1.0);
	
	#if defined(USE_FOG)
		float fog_start = clamp(1.0 - (1.0 / max(fog.start_dist, 0.0001)), 0.0, 1.0);
		float fog_end = clamp(1.0 - (1.0 / max(fog.end_dist, 0.0001)), 0.0, 1.0);
		float fog_depth = clamp(-outpt.z, 0.0, 1.0);
		float mag = clamp((fog_depth - fog_start) / max(fog_end - fog_start, 0.0001), 0.0, 1.0);
		color = vec4(mix(color.rgb, fog.color.rgb, mag), color.a);
		bloom_mask = max(bloom_mask, mag);
	#endif
	if (motion_vector_mode == 2 && color.a <= 0.001)
	{
		velocity = vec2(0.0);
		motion_object_id = 0u;
	}
	post_mask = vec4(ao_mask, bloom_mask, 0.0, 1.0);
	ao_class = float(clamp(ao_class_value, 0, 255)) / 255.0;
}
