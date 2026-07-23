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
#endif

uniform int phong_enabled;
uniform vec3 phong_light_direction;
uniform int dynamic_light_count;
uniform vec3 dynamic_face_normal;
uniform vec3 dynamic_light_positions[8];
uniform vec3 dynamic_light_colors[8];
uniform float dynamic_light_radii[8];
uniform vec3 dynamic_light_specular_positions[8];
uniform float dynamic_light_specular_radii[8];
uniform float dynamic_light_specular_scalars[8];
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
// 0: ordinary alpha coverage, 1: additive visible contribution,
// 2: destination-multiplying material.
uniform int post_mask_blend_mode;
uniform int fast_additive_bitmap;

layout(std140, binding = 5) uniform RetainedDrawBlock
{
	mat4 retained_transform;
	mat4 retained_modelview;
	mat4 retained_current_world;
	mat4 retained_previous_world;
	vec4 retained_base_color_depth_bias;
	vec4 retained_uv_offset_uv2_scale;
	vec4 retained_legacy_view_position_packed;
	vec4 retained_legacy_view_right_packed;
	vec4 retained_legacy_view_up_packed;
	vec4 retained_legacy_view_forward_packed;
	vec4 retained_legacy_viewport_packed;
	vec4 retained_alpha_effect_packed;
	vec4 retained_fog_plane_depth;
	vec4 retained_specular_view_scalar;
	vec4 retained_specular_light_range;
	vec4 retained_deform_direction_far_clip;
	vec4 retained_custom_clip_point_packed;
	vec4 retained_custom_clip_plane_packed;
	vec4 retained_custom_clip_scale_packed;
	vec4 retained_phong_direction_packed;
	ivec4 retained_modes0;
	ivec4 retained_modes1;
	ivec4 retained_modes2;
	ivec4 retained_modes3;
	uvec4 retained_deform_seed_packed;
};

#define retained_mode retained_modes0.x
#define retained_effect_mode retained_modes1.y
#define retained_per_pixel_specular_payload retained_modes3.x
#define fast_retained_room_base retained_modes3.y
#define retained_room_lightmap_arrays retained_modes3.z
#define retained_dynamic_lightmaps retained_modes3.w
uniform sampler2DArray retained_room_lightmaps_2;
uniform sampler2DArray retained_room_lightmaps_4;
uniform sampler2DArray retained_room_lightmaps_8;
uniform sampler2DArray retained_room_lightmaps_16;
uniform sampler2DArray retained_room_lightmaps_32;
uniform sampler2DArray retained_room_lightmaps_64;
uniform sampler2DArray retained_room_lightmaps_128;
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

layout(std430, binding = 6) readonly buffer PerPixelLightmapBuffer
{
	int retained_dynamic_lookup[65536];
	vec4 retained_dynamic_lights[];
};

uniform int room_fog_enabled;
uniform int room_fog_viewer_inside;
uniform vec3 room_fog_viewer_position;
uniform vec3 room_fog_viewer_forward;
uniform vec3 room_fog_color;
uniform float room_fog_depth;
uniform float room_fog_intensity;
uniform sampler2D room_fog_entry_depth;
uniform int room_fog_entry_map_enabled;
uniform ivec2 room_fog_entry_origin;
uniform ivec2 room_fog_entry_size;
// 0: base material, 1: additive light/specular, 2: portal cap,
// 3: approximate multiplicative overlay fallback, 4: raw multiplier pass,
// 5: exact additive fog correction for a multiplier already applied to the base.
uniform int fog_composite_mode;

in vec4 outcolor;
noperspective in float out_retained_effect_alpha;
in vec4 outnormal;
in vec4 out_motion_world_position;
in vec4 out_motion_previous_world_position;
in vec4 out_room_fog_world_position;
flat in int out_retained_lightmap_handle;
flat in int out_retained_ao_class;
flat in int out_retained_lightmap_info;
flat in vec3 out_retained_face_normal;
#if defined(USE_SPECULAR)
in vec4 out_field_specular_centers[4];
in vec4 out_field_specular_colors[4];
#endif

#if defined(USE_LIGHTMAP)
vec4 SampleLegacyLightmap(vec2 uv)
{
	if (retained_room_lightmap_arrays != 0)
	{
		if (out_retained_lightmap_handle < 0)
			return vec4(1.0);
		int bucket = (out_retained_lightmap_handle >> 16) & 0x7f;
		float layer = float(out_retained_lightmap_handle & 0x0000ffff);
		vec3 array_uv = vec3(uv, layer);
		if (bucket == 0) return texture(retained_room_lightmaps_2, array_uv);
		if (bucket == 1) return texture(retained_room_lightmaps_4, array_uv);
		if (bucket == 2) return texture(retained_room_lightmaps_8, array_uv);
		if (bucket == 3) return texture(retained_room_lightmaps_16, array_uv);
		if (bucket == 4) return texture(retained_room_lightmaps_32, array_uv);
		if (bucket == 5) return texture(retained_room_lightmaps_64, array_uv);
		return texture(retained_room_lightmaps_128, array_uv);
	}
	return texture(lightmaptexture, uv);
}
#endif
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

	return pow(dotp, float(specular_data.exponent)) * light_color * scalar;
}

vec3 ApplyPerPixelSpecular(vec3 lightmap_color)
{
	const float[4] weights = float[4](1.0, 0.66, 0.33, 0.25);
	vec3 view_position = out_motion_world_position.xyz / max(out_motion_world_position.w, 0.0001);
	vec3 raw_normal = outnormal.xyz;
	if (dot(raw_normal, raw_normal) <= 0.000001)
		return vec3(0.0);
	vec3 normal = normalize(raw_normal);
	vec3 specular_color = vec3(0.0);

	for (int i = 0; i < 4; i++)
	{
		if (i >= specular_data.num_specular)
			break;

		float source_weight = specular_data.pad0 > 0.5 ? 1.0 : weights[i];
		vec3 light_position = specular_data.pad0 > 0.5 ?
			(out_field_specular_centers[i].xyz / max(out_field_specular_centers[i].w, 0.0001)) :
			specular_data.speculars[i].bright_center.xyz;
		vec3 light_color = specular_data.pad0 > 0.5 ?
			out_field_specular_colors[i].xyz : specular_data.speculars[i].color.xyz;
		specular_color += SpecularFromIncident(view_position, normal, view_position - light_position,
			light_color, source_weight);
	}

	for (int i = 0; i < 8; i++)
	{
		if (i >= dynamic_light_count)
			break;

		vec3 light_position = dynamic_light_specular_positions[i];
		vec3 incident = view_position - light_position;
		float radius = max(dynamic_light_specular_radii[i], 0.0001);
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
			dynamic_light_colors[i], scalar * dynamic_light_specular_scalars[i]);
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
	int retained_lookup = 0;
	if (retained_dynamic_lightmaps != 0 && out_retained_lightmap_info >= 0)
		retained_lookup = retained_dynamic_lookup[out_retained_lightmap_info];
	int retained_count = retained_lookup & 15;
	int light_count = retained_count > 0 ? retained_count : dynamic_light_count;
	if (light_count == 0)
		return lightmap_color;

	vec3 world_position = outnormal.xyz / max(outnormal.w, 0.0001);
	vec3 face_normal = retained_count > 0 ? normalize(out_retained_face_normal) :
		normalize(dynamic_face_normal);
	vec3 dynamic_color = vec3(0.0);
	int retained_entry = (retained_lookup >> 4) - 1;

	for (int i = 0; i < 8; i++)
	{
		if (i >= light_count)
			break;

		vec3 light_position = dynamic_light_positions[i];
		vec3 light_color = dynamic_light_colors[i];
		float radius = dynamic_light_radii[i];
		float falloff = dynamic_light_falloffs[i];
		vec3 stored_direction = dynamic_light_directions[i];
		float stored_dot_range = dynamic_light_dot_ranges[i];
		bool directional = dynamic_light_directional[i] != 0;
		if (retained_count > 0)
		{
			int light_base = (retained_entry * 8 + i) * 3;
			vec4 position_radius = retained_dynamic_lights[light_base];
			vec4 color_falloff = retained_dynamic_lights[light_base + 1];
			vec4 direction_dot = retained_dynamic_lights[light_base + 2];
			light_position = position_radius.xyz;
			radius = position_radius.w;
			light_color = color_falloff.xyz;
			falloff = color_falloff.w;
			stored_direction = direction_dot.xyz;
			stored_dot_range = direction_dot.w;
			directional = direction_dot.w > -1.5;
		}

		vec3 light_delta = world_position - light_position;
		radius = max(radius, 0.0001);
		float distance = length(light_delta);
		vec3 light_vector = (distance > 0.0001) ? light_delta / distance : face_normal;
		float scalar = 1.0 - (distance / radius);
		if (scalar <= 0.0)
			continue;
		scalar = pow(scalar, max(falloff, 0.0001));

		if (directional)
		{
			vec3 light_direction = normalize(stored_direction);
			float direction_dot = dot(light_vector, light_direction);
			float dot_range = stored_dot_range;
			if (direction_dot < dot_range)
				continue;

			scalar *= (direction_dot - dot_range) / max(1.0 - dot_range, 0.0001);
		}

		dynamic_color += light_color * scalar;
	}

	return clamp(lightmap_color + dynamic_color, vec3(0.0), vec3(1.0));
}

float SoftParticleEyeDepth(float depth)
{
	return 1.0 / max(1.0 - clamp(depth, 0.0, 0.9999), 0.0001);
}

float RoomFogAmount(vec3 world_position)
{
	vec3 segment = world_position - room_fog_viewer_position;
	float segment_length = length(segment);
	if (segment_length <= 0.0001)
		return 0.0;

	// The original room fog density is measured along camera-forward depth,
	// rather than Euclidean ray length. Preserve that authored presentation in
	// the per-fragment volume path so oblique walls do not become over-fogged.
	float fog_length = max(dot(segment, room_fog_viewer_forward), 0.0);
	if (room_fog_viewer_inside == 0)
	{
		if (room_fog_entry_map_enabled == 0)
			return 0.0;
		ivec2 fragment_pixel = ivec2(gl_FragCoord.xy);
		if (any(lessThan(fragment_pixel, room_fog_entry_origin)) ||
			any(greaterThanEqual(fragment_pixel,
				room_fog_entry_origin + room_fog_entry_size)))
		{
			return 0.0;
		}
		ivec2 entry_size = textureSize(room_fog_entry_depth, 0);
		ivec2 entry_pixel = clamp(fragment_pixel - room_fog_entry_origin,
			ivec2(0), entry_size - ivec2(1));
		float entry_depth = texelFetch(room_fog_entry_depth, entry_pixel, 0).r;
		if (entry_depth > 1.0e20)
			return 0.0;
		fog_length = max(fog_length - entry_depth, 0.0);
	}

	return clamp((fog_length / max(room_fog_depth, 0.0001)) *
		room_fog_intensity, 0.0, 1.0);
}

void main()
{
	velocity = vec2(0.0);
	motion_object_id = 0u;
	#if defined(USE_TEXTURING) && !defined(USE_LIGHTMAP)
	if (fast_additive_bitmap != 0)
	{
		color = texture(colortexture, outuv.xy / outuv.z) * outcolor;
		if (color.a <= 0.0)
			discard;

		if (soft_particle_enabled != 0 &&
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
				color.a *= clamp((scene_eye - particle_eye) /
					max(soft_particle_depth_range, 0.0001), 0.0, 1.0);
			}
		}

		if (room_fog_enabled != 0 && abs(out_room_fog_world_position.w) > 0.00001)
		{
			vec3 room_world_position = out_room_fog_world_position.xyz /
				out_room_fog_world_position.w;
			color.rgb *= 1.0 - RoomFogAmount(room_world_position);
		}
		#if defined(USE_FOG)
			float fog_start = clamp(1.0 - (1.0 / max(fog.start_dist, 0.0001)), 0.0, 1.0);
			float fog_end = clamp(1.0 - (1.0 / max(fog.end_dist, 0.0001)), 0.0, 1.0);
			float fog_depth = clamp(-outpt.z, 0.0, 1.0);
			float mag = clamp((fog_depth - fog_start) /
				max(fog_end - fog_start, 0.0001), 0.0, 1.0);
			color.rgb *= 1.0 - mag;
		#endif
		post_mask = vec4(0.0);
		return;
	}
	#endif
	#if defined(USE_TEXTURING)
	if (fast_retained_room_base != 0)
	{
		vec4 base_color = texture(colortexture, outuv.xy / outuv.z);
		#if defined(USE_LIGHTMAP)
			vec4 lightmap_color = SampleLegacyLightmap(outuv2.xy / outuv2.z);
			lightmap_color.rgb = ApplyDynamicLightmapLighting(lightmap_color.rgb);
			color = base_color * lightmap_color * outcolor;
		#else
			color = base_color * outcolor;
		#endif

		float room_amount = 0.0;
		if (room_fog_enabled != 0 && abs(out_room_fog_world_position.w) > 0.00001)
		{
			vec3 room_world_position = out_room_fog_world_position.xyz /
				out_room_fog_world_position.w;
			room_amount = RoomFogAmount(room_world_position);
			color.rgb = mix(color.rgb, room_fog_color, room_amount);
		}
		float bloom_mask = 0.0;
		#if defined(USE_FOG)
			float fog_start = clamp(1.0 - (1.0 / max(fog.start_dist, 0.0001)), 0.0, 1.0);
			float fog_end = clamp(1.0 - (1.0 / max(fog.end_dist, 0.0001)), 0.0, 1.0);
			float fog_depth = clamp(-outpt.z, 0.0, 1.0);
			float mag = clamp((fog_depth - fog_start) /
				max(fog_end - fog_start, 0.0001), 0.0, 1.0);
			color.rgb = mix(color.rgb, fog.color.rgb, mag);
			bloom_mask = mag;
		#endif
		float coverage = clamp(color.a, 0.0, 1.0);
		int room_ao_class = retained_room_lightmap_arrays != 0 ?
			out_retained_ao_class : ao_class_value;
		post_mask = vec4(room_amount * coverage, bloom_mask, 0.0,
			float(clamp(room_ao_class, 0, 255)) / 255.0);
		return;
	}
	#endif
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
	if (retained_effect_mode != 0)
		vertex_color.a = out_retained_effect_alpha;
	if (ao_capture_weight_mode != 0)
	{
		float ao_weight = clamp(ao_weight_value * (1.0 - ao_suppression), 0.0, 1.0);
		if (ao_weight <= 0.0)
			discard;
		color = vec4(ao_weight, ao_weight, ao_weight, 1.0);
		post_mask = vec4(0.0, 0.0, 0.0, ao_weight);
		return;
	}

	float ao_mask = 0.0;
	float bloom_mask = 0.0;
	float room_fog_amount = 0.0;

	#if defined(USE_SPECULAR)
		vec4 base_color = texture(colortexture, outuv.xy / outuv.z);
		if (specular_data.debug_tint > 0.5)
		{
			float debug_mask = base_color.a;
			float mask_alpha = (debug_mask > 0.001) ?
				clamp(debug_mask * 3.0, 0.35, 0.9) : 0.0;
			vec3 tint_color = (specular_data.debug_authored > 0.5) ?
				vec3(1.0, 0.85, 0.0) : vec3(0.0, 1.0, 0.0);
			color = vec4(tint_color, mask_alpha);
		}
		else if (per_pixel_specular_enabled != 0)
		{
			vec3 lightmap_color = SampleLegacyLightmap(outuv2.xy / outuv2.z).rgb;
			vec3 specular_color = ApplyPerPixelSpecular(lightmap_color);
			float specular_alpha = clamp(base_color.a * specular_data.alpha_strength, 0.0, 1.0);
			color = vec4(specular_color, clamp(specular_alpha * vertex_color.a, 0.0, 1.0));
		}
		else
		{
			color = vec4(vertex_color.rgb, base_color.a * vertex_color.a);
		}
	#elif defined(USE_TEXTURING) && defined(USE_LIGHTMAP)
		vec4 lightmap_color = SampleLegacyLightmap(outuv2.xy / outuv2.z);
		lightmap_color.rgb = ApplyDynamicLightmapLighting(lightmap_color.rgb);
		color = texture(colortexture, outuv.xy / outuv.z) * lightmap_color * vertex_color;
	#elif defined(USE_TEXTURING)
		color = texture(colortexture, outuv.xy / outuv.z) * vertex_color;
	#else
		color = vertex_color;
	#endif
	// Additive bitmap materials contribute nothing when texture/vertex alpha is
	// exactly zero.  Large radial glows and legacy thruster cones otherwise run
	// blending and post-mask writes across the transparent majority of every
	// billboard, which is especially costly with MSAA and SSAA combined.
	if (post_mask_blend_mode == 1 && color.a <= 0.0)
		discard;
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
			// Destination-multiply blending ignores source alpha for RGB. Its
			// transparent endpoint is a neutral multiplier, so fade the sampled
			// material toward white as well as maintaining the ordinary alpha path.
			if (post_mask_blend_mode == 2)
				color.rgb = mix(vec3(1.0), color.rgb, fade);
			color.a *= fade;
		}
	}
	if (room_fog_enabled != 0 && abs(out_room_fog_world_position.w) > 0.00001)
	{
		vec3 room_world_position = out_room_fog_world_position.xyz /
			out_room_fog_world_position.w;
		room_fog_amount = RoomFogAmount(room_world_position);
		if (fog_composite_mode == 2)
			color = vec4(room_fog_color, room_fog_amount);
		else if (fog_composite_mode == 1)
			color.rgb *= 1.0 - room_fog_amount;
		else if (fog_composite_mode == 3)
		{
			// Destination-multiplying materials (scorches, dark smoke, and
			// similar overlays) must converge to the neutral multiplier as the
			// underlying surface disappears into fog.
			color.rgb = mix(vec3(1.0), color.rgb, 1.0 - room_fog_amount);
			room_fog_amount = 0.0;
		}
		else if (fog_composite_mode == 4)
		{
			// The destination-multiply pass needs the unmodified material value.
			room_fog_amount = 0.0;
		}
		else if (fog_composite_mode == 5)
		{
			color.rgb = room_fog_color * room_fog_amount *
				(vec3(1.0) - color.rgb);
			color.a = 0.0;
			room_fog_amount = 0.0;
		}
		else
			color.rgb = mix(color.rgb, room_fog_color, room_fog_amount);
	}
	float suppression_alpha = clamp(color.a, 0.0, 1.0);
	if (post_mask_blend_mode == 1)
	{
		float visible = max(max(color.r, color.g), color.b);
		suppression_alpha *= clamp(visible, 0.0, 1.0);
	}
	// AO is composited against the opaque scene captured before translucent
	// effects.  The amount protected here must therefore be the fragment's
	// actual coverage.  Amplifying low alpha values makes nearly transparent
	// bitmap borders suppress AO in the shape of their backing quad.
	if (post_mask_blend_mode == 2)
	{
		// Destination-multiplying draws do not cover the opaque scene; they
		// scale it.  Encode the inverse multiplier so the deferred AO delta
		// is scaled by the same material instead of being cut out by a
		// meaningless source alpha channel.  These materials are normally
		// grayscale; luminance is the least-biased scalar for colored data.
		float multiplier = clamp(dot(color.rgb, vec3(0.2126, 0.7152, 0.0722)), 0.0, 1.0);
		ao_mask = clamp(ao_suppression * (1.0 - multiplier), 0.0, 1.0);
	}
	else
		ao_mask = clamp(ao_suppression * suppression_alpha, 0.0, 1.0);
	// Fog only replaces destination color for the base-material and portal-cap
	// composites.  Additive lights are merely attenuated by fog, while
	// multiplicative overlays converge toward their neutral multiplier; neither
	// operation covers the destination for the deferred AO composite.  Weight
	// alpha-blended base materials by their actual coverage so transparent texels
	// cannot stamp a rectangular fog mask into the scene.
	if (fog_composite_mode == 0)
		ao_mask = max(ao_mask, room_fog_amount * suppression_alpha);
	else if (fog_composite_mode == 2)
		ao_mask = max(ao_mask, room_fog_amount);
	bloom_mask = clamp(bloom_suppression * (1.0 - pow(1.0 - suppression_alpha, 3.0)), 0.0, 1.0);
	
	#if defined(USE_FOG)
		float fog_start = clamp(1.0 - (1.0 / max(fog.start_dist, 0.0001)), 0.0, 1.0);
		float fog_end = clamp(1.0 - (1.0 / max(fog.end_dist, 0.0001)), 0.0, 1.0);
		float fog_depth = clamp(-outpt.z, 0.0, 1.0);
		float mag = clamp((fog_depth - fog_start) / max(fog_end - fog_start, 0.0001), 0.0, 1.0);
		if (fog_composite_mode == 1)
			color.rgb *= 1.0 - mag;
		else if (fog_composite_mode == 3)
			color.rgb = mix(vec3(1.0), color.rgb, 1.0 - mag);
		else
			color.rgb = mix(color.rgb, fog.color.rgb, mag);
		bloom_mask = max(bloom_mask, mag);
	#endif
	if (motion_vector_mode == 2 && color.a <= 0.001)
	{
		velocity = vec2(0.0);
		motion_object_id = 0u;
	}
	post_mask = vec4(ao_mask, bloom_mask, 0.0, float(clamp(ao_class_value, 0, 255)) / 255.0);
}
