#version 460
#extension GL_GOOGLE_include_directive : require

#include "post_uniforms.glsl"

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 2) uniform texture2D scene_texture;
layout(set = 0, binding = 3) uniform texture2D bloom_texture;
layout(set = 0, binding = 4) uniform texture2D protection_mask_texture;

void main()
{
    vec2 bloom_uv = PostPrimaryUv(in_uv);
    vec2 scene_uv = PostSecondaryUv(in_uv);
    vec3 scene_color = texture(
        sampler2D(scene_texture, post_samplers[0]), scene_uv).rgb;
    vec3 bloom_color = texture(
        sampler2D(bloom_texture, post_samplers[4]), bloom_uv).rgb;

    float scene_mask = 1.0;
    if (PostFeature(POST_USE_PROTECTION_MASK))
    {
        ivec2 mask_size = textureSize(protection_mask_texture, 0);
        ivec2 mask_pixel = ivec2(
            clamp(bloom_uv, vec2(0.0), vec2(0.999999)) *
            vec2(mask_size));
        float protection = texelFetch(
            sampler2D(protection_mask_texture, post_samplers[5]),
            mask_pixel,
            0).g;
        scene_mask *= 1.0 - clamp(protection, 0.0, 1.0);
    }

    float gamma = post.bloom_gamma_threshold_intensity_spread.x;
    float intensity = post.bloom_gamma_threshold_intensity_spread.z;
    vec3 composite = scene_color + bloom_color * intensity * scene_mask;
    out_color = vec4(pow(max(composite, vec3(0.0)), vec3(gamma)), 1.0);
}
