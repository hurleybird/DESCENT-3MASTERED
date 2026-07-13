#version 460
#extension GL_GOOGLE_include_directive : require

#include "post_uniforms.glsl"

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 2) uniform texture2D current_ao_texture;
layout(set = 0, binding = 3) uniform texture2D history_ao_texture;
layout(set = 0, binding = 4) uniform texture2D velocity_texture;
layout(set = 0, binding = 5) uniform utexture2D object_id_texture;
layout(set = 0, binding = 6) uniform texture2D depth_texture;

vec2 ReconstructStaticVelocity(vec2 source_uv)
{
    if (!PostFeature(POST_HAS_STATIC_RECONSTRUCTION))
        return vec2(0.0);

    // Captured matrices retain GL4's bottom-left convention. Sample the
    // canonical image at top-left UV, then cross the coordinate boundary for
    // projection math only.
    float depth = texture(sampler2D(depth_texture, post_samplers[6]), source_uv).r;
    if (depth >= 0.999999)
        return vec2(0.0);

    vec2 legacy_uv = PostTopLeftToLegacyUv(source_uv);
    vec2 current_ndc = legacy_uv * 2.0 - vec2(1.0);
    float view_z = -1.0 / max(1.0 - depth, 0.00001);
    float view_x = -view_z *
        (current_ndc.x + post.current_projection[2][0]) /
        post.current_projection[0][0];
    float view_y = -view_z *
        (current_ndc.y + post.current_projection[2][1]) /
        post.current_projection[1][1];
    vec4 world = post.current_inverse_modelview *
        vec4(view_x, view_y, view_z, 1.0);
    vec4 previous_clip = post.previous_view_projection * world;
    if (previous_clip.w <= 0.00001)
        return vec2(0.0);

    vec2 previous_ndc = previous_clip.xy / previous_clip.w;
    vec2 legacy_velocity = (current_ndc - previous_ndc) * 0.5;
    return PostLegacyVelocityToTopLeft(legacy_velocity);
}

vec2 ResolveVelocity(vec2 source_uv)
{
    vec2 velocity_uv = clamp(PostVelocityUv(source_uv), vec2(0.0), vec2(1.0));
    if (PostFeature(POST_HAS_DYNAMIC_VELOCITY))
    {
        uint object_id = texture(
            usampler2D(object_id_texture, post_samplers[5]), velocity_uv).r;
        if (object_id != 0u)
        {
            vec2 legacy_velocity = texture(
                sampler2D(velocity_texture, post_samplers[0]), velocity_uv).xy;
            return PostLegacyVelocityToTopLeft(legacy_velocity);
        }
    }
    return ReconstructStaticVelocity(source_uv);
}

float WeightFromDepth(float current_depth, float history_depth)
{
    float depth_reject =
        post.temporal_blend_depth_velocity_frame_time.y;
    if (depth_reject <= 0.0)
        return 0.0;
    float delta = abs(current_depth - history_depth);
    return 1.0 - smoothstep(depth_reject * 0.5, depth_reject, delta);
}

float WeightFromVelocity(vec2 velocity)
{
    float velocity_reject_pixels =
        post.temporal_blend_depth_velocity_frame_time.z;
    if (velocity_reject_pixels <= 0.0)
        return 1.0;
    vec2 source_size = post.screen_size_inv_size.xy;
    if (any(lessThanEqual(source_size, vec2(0.0))))
        source_size = post.source_extent_inv_extent.xy;
    float pixels = length(velocity * source_size);
    return 1.0 - smoothstep(
        velocity_reject_pixels * 0.5,
        velocity_reject_pixels,
        pixels);
}

void main()
{
    vec4 current = texture(
        sampler2D(current_ao_texture, post_samplers[0]), in_uv);
    float ao = clamp(current.x, 0.0, 1.0);
    float current_depth = current.y;
    float history_weight = 0.0;
    float history_blend = post.temporal_blend_depth_velocity_frame_time.x;

    if (PostFeature(POST_HISTORY_VALID) && history_blend > 0.0)
    {
        vec2 velocity = ResolveVelocity(in_uv);
        vec2 previous_uv = in_uv - velocity;
        if (all(greaterThanEqual(previous_uv, vec2(0.0))) &&
            all(lessThanEqual(previous_uv, vec2(1.0))))
        {
            vec4 history = texture(
                sampler2D(history_ao_texture, post_samplers[3]), previous_uv);
            float depth_weight = WeightFromDepth(current_depth, history.y);
            float velocity_weight = WeightFromVelocity(velocity);
            history_weight = clamp(
                history_blend * depth_weight * velocity_weight,
                0.0,
                0.995);
            ao = mix(ao, clamp(history.x, 0.0, 1.0), history_weight);
        }
    }

    out_color = vec4(ao, current_depth, history_weight, 1.0);
}
