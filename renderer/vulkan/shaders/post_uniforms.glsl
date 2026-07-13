#ifndef PICCU_POST_UNIFORMS_GLSL
#define PICCU_POST_UNIFORMS_GLSL

#extension GL_EXT_samplerless_texture_functions : require

// Must remain byte-for-byte layout compatible with PostPassUniforms.
layout(std140, set = 0, binding = 0) uniform PostPassUniformBlock
{
    mat4 current_projection;
    mat4 current_inverse_modelview;
    mat4 previous_view_projection;
    vec4 source_extent_inv_extent;
    vec4 destination_extent_inv_extent;
    vec4 visible_origin_size;
    vec4 source_visible_origin_size;
    vec4 uv_origin_scale;
    vec4 secondary_uv_origin_scale;
    vec4 velocity_uv_origin_scale;
    vec4 scene_uv_origin_scale;
    vec4 ao_uv_origin_scale;
    vec4 alpha_mask_uv_origin_scale;
    vec4 screen_size_inv_size;
    vec4 ao_screen_size_inv_size;
    vec4 projection_info;
    vec4 near_far_radius_radius_pixels;
    vec4 noise_origin_jitter;
    vec4 blur_delta_sharpness_reserved;
    vec4 bloom_gamma_threshold_intensity_spread;
    vec4 ao_max_radius_neg_inv_radius2_bias_intensity;
    vec4 ao_class_weights;
    vec4 temporal_blend_depth_velocity_frame_time;
    vec4 motion_strength_legacy_object_centers;
    vec4 motion_legacy_frame_sphere_density_exponent;
    vec4 motion_periphery_combined_strength_sphere_density;
    vec4 motion_afterburner_exponent_fov_pixel_scalar;
    uvec4 sample_counts;
    uvec4 feature_flags;
    ivec4 integer_params;
    uvec4 frame_branch;
} post;

layout(set = 0, binding = 1) uniform sampler post_samplers[7];

const uint POST_HISTORY_VALID = 1u << 0;
const uint POST_HAS_DYNAMIC_VELOCITY = 1u << 1;
const uint POST_HAS_STATIC_RECONSTRUCTION = 1u << 2;
const uint POST_HAS_AO_CLASS = 1u << 3;
const uint POST_AO_WEIGHT_IS_DIRECT = 1u << 4;
const uint POST_HAS_MASK = 1u << 5;
const uint POST_USE_BLOOM_MASK = 1u << 6;
const uint POST_USE_DEPTH_MASK = 1u << 7;
const uint POST_USE_PROTECTION_MASK = 1u << 8;
const uint POST_USE_ALPHA_OCCLUSION_MASK = 1u << 9;
const uint POST_USE_VISIBLE_RECT = 1u << 10;
const uint POST_HAS_SUPPRESSION_MASK = 1u << 11;
const uint POST_USE_ALPHA_MASK = 1u << 12;
const uint POST_SOURCE_VISIBLE_RECT = 1u << 13;
const uint POST_DEBUG_TEMPORAL = 1u << 14;
const uint POST_PAUSED_OR_FROZEN = 1u << 15;

// PostUniformSourceSelector. For sampled capture/resolve phases,
// frame_branch.y names the live optional image binding explicitly.
const uint POST_SOURCE_PRIMARY_2D = 2u;
const uint POST_SOURCE_MSAA_RESOLVED_2D = 3u;
const uint POST_SOURCE_SSAA_INTERMEDIATE_2X_2D = 4u;

bool PostFeature(uint bit_value)
{
    return (post.feature_flags.x & bit_value) != 0u;
}

vec2 PostPrimaryUv(vec2 base_uv)
{
    return post.uv_origin_scale.xy + base_uv * post.uv_origin_scale.zw;
}

vec2 PostSecondaryUv(vec2 base_uv)
{
    return post.secondary_uv_origin_scale.xy +
        base_uv * post.secondary_uv_origin_scale.zw;
}

vec2 PostVelocityUv(vec2 base_uv)
{
    return post.velocity_uv_origin_scale.xy +
        base_uv * post.velocity_uv_origin_scale.zw;
}

vec2 PostSceneUv(vec2 base_uv)
{
    return post.scene_uv_origin_scale.xy +
        base_uv * post.scene_uv_origin_scale.zw;
}

vec2 PostAoUv(vec2 base_uv)
{
    return post.ao_uv_origin_scale.xy + base_uv * post.ao_uv_origin_scale.zw;
}

vec2 PostAlphaMaskUv(vec2 base_uv)
{
    return post.alpha_mask_uv_origin_scale.xy +
        base_uv * post.alpha_mask_uv_origin_scale.zw;
}

// Post textures and gl_FragCoord use canonical top-left coordinates. Current
// projection matrices and stored motion vectors retain GL4's bottom-left
// convention and cross that boundary only through these helpers.
vec2 PostTopLeftToLegacyUv(vec2 uv)
{
    return vec2(uv.x, 1.0 - uv.y);
}

vec2 PostLegacyVelocityToTopLeft(vec2 velocity)
{
    return vec2(velocity.x, -velocity.y);
}

#endif
