#version 460
#extension GL_GOOGLE_include_directive : require

#include "post_uniforms.glsl"

layout(set = 0, binding = 2) uniform texture2D velocity_image;
layout(set = 0, binding = 3) uniform texture2D depth_image;
layout(set = 0, binding = 4) uniform utexture2D object_id_image;
layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

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

vec2 ResolveVelocity(vec2 canonical_uv, vec2 velocity_uv)
{
    if (!PostFeature(POST_HAS_DYNAMIC_VELOCITY))
        return ReconstructStaticVelocity(canonical_uv);
    uint object_id = texture(usampler2D(object_id_image,
        post_samplers[5]), velocity_uv).r;
    if (object_id != 0u)
        return PostLegacyVelocityToTopLeft(texture(
            sampler2D(velocity_image, post_samplers[0]), velocity_uv).xy);
    return ReconstructStaticVelocity(canonical_uv);
}

void main()
{
    vec2 velocity = ResolveVelocity(in_uv, PostVelocityUv(in_uv));
    vec2 pixels = velocity * post.screen_size_inv_size.xy;
    vec2 signed_visualization = sign(pixels) *
        (1.0 - exp(-abs(pixels) * 4.0));
    float magnitude = 1.0 - exp(-length(pixels) * 4.0);
    vec3 directional = vec3(max(signed_visualization.x, 0.0),
        max(-signed_visualization.x, 0.0), abs(signed_visualization.y));
    out_color = vec4(max(directional, vec3(magnitude * 0.12)),
        clamp(magnitude * 0.85, 0.0, 0.85));
}
