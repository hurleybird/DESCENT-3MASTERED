#version 460
#extension GL_GOOGLE_include_directive : require

#include "post_uniforms.glsl"

layout(set = 0, binding = 2) uniform texture2D color_image;
layout(set = 0, binding = 3) uniform texture2D depth_image;
layout(set = 0, binding = 4) uniform texture2D velocity_image;
layout(set = 0, binding = 5) uniform utexture2D object_id_image;
layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

const uint MOTION_OBJECT_LEGACY_BLUR_MASK = 0x80000000u;

vec2 ReconstructStaticVelocity(vec2 canonical_uv)
{
    if (!PostFeature(POST_HAS_STATIC_RECONSTRUCTION))
        return vec2(0.0);
    float depth = texture(sampler2D(depth_image, post_samplers[6]),
        canonical_uv).r;
    if (depth >= 0.999999)
        return vec2(0.0);
    vec2 legacy_uv = PostTopLeftToLegacyUv(canonical_uv);
    vec2 current_ndc = legacy_uv * 2.0 - vec2(1.0);
    float view_z = -1.0 / max(1.0 - depth, 0.00001);
    float view_x = -view_z * (current_ndc.x +
        post.current_projection[2][0]) / post.current_projection[0][0];
    float view_y = -view_z * (current_ndc.y +
        post.current_projection[2][1]) / post.current_projection[1][1];
    vec4 world = post.current_inverse_modelview *
        vec4(view_x, view_y, view_z, 1.0);
    vec4 previous_clip = post.previous_view_projection * world;
    if (previous_clip.w <= 0.00001)
        return vec2(0.0);
    vec2 previous_ndc = previous_clip.xy / previous_clip.w;
    return PostLegacyVelocityToTopLeft(
        (current_ndc - previous_ndc) * 0.5);
}

vec2 ResolveVelocity(vec2 canonical_uv, out float motion_strength,
    out float center_suppression)
{
    motion_strength = post.motion_strength_legacy_object_centers.x;
    center_suppression = post.motion_strength_legacy_object_centers.z;
    if (!PostFeature(POST_HAS_DYNAMIC_VELOCITY))
        return ReconstructStaticVelocity(canonical_uv);
    uint object_id = texture(usampler2D(object_id_image,
        post_samplers[5]), canonical_uv).r;
    if (object_id != 0u)
    {
        if ((object_id & MOTION_OBJECT_LEGACY_BLUR_MASK) != 0u)
        {
            motion_strength = post.motion_strength_legacy_object_centers.y;
            center_suppression = post.motion_strength_legacy_object_centers.w;
        }
        return PostLegacyVelocityToTopLeft(texture(
            sampler2D(velocity_image, post_samplers[0]), canonical_uv).xy);
    }
    return ReconstructStaticVelocity(canonical_uv);
}

void main()
{
    vec4 base = texture(sampler2D(color_image, post_samplers[1]), in_uv);
    vec2 velocity_uv = PostVelocityUv(in_uv);
    float motion_strength;
    float center_suppression;
    vec2 velocity = ResolveVelocity(velocity_uv,
        motion_strength, center_suppression);
    vec2 centered = in_uv * 2.0 - vec2(1.0);
    float periphery = smoothstep(0.15, 1.0, length(centered));
    float radial_strength = mix(1.0 -
        clamp(center_suppression, 0.0, 1.0), 1.0, periphery);
    vec2 blur = clamp(velocity * motion_strength * radial_strength,
        vec2(-0.16), vec2(0.16));
    if (motion_strength <= 0.0 || dot(blur, blur) < 0.0000000001)
    {
        out_color = base;
        return;
    }

    int samples = clamp(int(post.sample_counts.y), 3, 17);
    vec4 accumulated = vec4(0.0);
    for (int index = 0; index < 17; ++index)
    {
        if (index >= samples)
            break;
        float offset = float(index) / float(samples - 1) - 0.5;
        vec2 sample_uv = clamp(in_uv - blur * offset,
            vec2(0.0), vec2(1.0));
        accumulated += texture(sampler2D(color_image, post_samplers[1]),
            sample_uv);
    }
    out_color = accumulated / float(samples);
}
