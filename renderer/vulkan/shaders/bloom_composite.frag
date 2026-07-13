#version 460
#extension GL_GOOGLE_include_directive : require

#include "post_uniforms.glsl"

layout(set = 0, binding = 2) uniform texture2D authored_color_image;
layout(set = 0, binding = 3) uniform texture2D bloom_image;
layout(set = 0, binding = 4) uniform texture2D protection_image;
layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main()
{
    vec2 bloom_uv = PostPrimaryUv(in_uv);
    vec2 scene_uv = PostSceneUv(in_uv);
    vec4 source_color = texture(
        sampler2D(authored_color_image, post_samplers[0]), scene_uv);
    vec3 bloom_color = texture(
        sampler2D(bloom_image, post_samplers[4]), bloom_uv).rgb *
        post.bloom_gamma_threshold_intensity_spread.z;
    float scene_mask = PostFeature(POST_USE_ALPHA_MASK) ?
        1.0 - clamp(source_color.a, 0.0, 1.0) : 1.0;
    if (PostFeature(POST_USE_PROTECTION_MASK))
    {
        ivec2 mask_size = textureSize(protection_image, 0);
        ivec2 mask_pixel = ivec2(clamp(bloom_uv, vec2(0.0),
            vec2(0.999999)) * vec2(mask_size));
        float protection = texelFetch(
            sampler2D(protection_image, post_samplers[5]),
            mask_pixel, 0).g;
        scene_mask *= 1.0 - clamp(protection, 0.0, 1.0);
    }
    float gamma_value = post.bloom_gamma_threshold_intensity_spread.x;
    out_color = vec4(pow(max(source_color.rgb +
        bloom_color * scene_mask, vec3(0.0)), vec3(gamma_value)), 1.0);
}
