#version 460
#extension GL_GOOGLE_include_directive : require

#include "post_uniforms.glsl"
#include "post_math.glsl"

layout(set = 0, binding = 2) uniform texture2D source_image;
layout(set = 0, binding = 3) uniform texture2D depth_image;
layout(set = 0, binding = 4) uniform texture2D protection_image;
layout(set = 0, binding = 5) uniform texture2D alpha_occlusion_image;
layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

float AlphaOcclusion(vec2 sample_uv)
{
    if (!PostFeature(POST_USE_ALPHA_OCCLUSION_MASK))
        return 0.0;
    vec2 local_uv = (sample_uv - post.alpha_mask_uv_origin_scale.xy) /
        max(post.alpha_mask_uv_origin_scale.zw, vec2(0.000001));
    if (any(lessThan(local_uv, vec2(0.0))) ||
        any(greaterThan(local_uv, vec2(1.0))))
        return 0.0;
    return clamp(texture(sampler2D(alpha_occlusion_image,
        post_samplers[1]), local_uv).a, 0.0, 1.0);
}

vec3 SampleBloom(vec2 uv, vec2 step_size, float x, float y)
{
    vec2 sample_uv = uv + step_size * vec2(x, y);
    if (PostFeature(POST_USE_DEPTH_MASK) && texture(
        sampler2D(depth_image, post_samplers[6]), sample_uv).r >= 0.99999)
        return vec3(0.0);
    float alpha_occlusion = AlphaOcclusion(sample_uv);
    if (alpha_occlusion >= 1.0)
        return vec3(0.0);

    float protection = 0.0;
    if (PostFeature(POST_USE_PROTECTION_MASK))
    {
        ivec2 mask_size = textureSize(protection_image, 0);
        ivec2 mask_pixel = ivec2(clamp(sample_uv, vec2(0.0),
            vec2(0.999999)) * vec2(mask_size));
        protection = clamp(texelFetch(
            sampler2D(protection_image, post_samplers[5]),
            mask_pixel, 0).g, 0.0, 1.0);
        if (protection >= 1.0)
            return vec3(0.0);
    }
    vec3 display_color = PostToDisplay(texture(
        sampler2D(source_image, post_samplers[1]), sample_uv).rgb);
    float brightness = max(max(display_color.r, display_color.g),
        display_color.b);
    float threshold_range = max(1.0 -
        post.bloom_gamma_threshold_intensity_spread.y, 0.0001);
    float amount = clamp((brightness -
        post.bloom_gamma_threshold_intensity_spread.y) /
        threshold_range, 0.0, 1.0);
    return display_color * amount * (1.0 - protection) *
        (1.0 - alpha_occlusion);
}

vec3 SmoothBloomDownsample(vec2 base_uv, vec2 step_size)
{
    vec3 h0 = SampleBloom(base_uv, step_size, -1.0,  1.0);
    vec3 h1 = SampleBloom(base_uv, step_size,  1.0,  1.0);
    vec3 h2 = SampleBloom(base_uv, step_size, -1.0, -1.0);
    vec3 h3 = SampleBloom(base_uv, step_size,  1.0, -1.0);
    vec3 l0 = SampleBloom(base_uv, step_size, -2.0,  2.0);
    vec3 l1 = SampleBloom(base_uv, step_size,  0.0,  2.0);
    vec3 l2 = SampleBloom(base_uv, step_size,  2.0,  2.0);
    vec3 l3 = SampleBloom(base_uv, step_size, -2.0,  0.0);
    vec3 l4 = SampleBloom(base_uv, step_size,  0.0,  0.0);
    vec3 l5 = SampleBloom(base_uv, step_size,  2.0,  0.0);
    vec3 l6 = SampleBloom(base_uv, step_size, -2.0, -2.0);
    vec3 l7 = SampleBloom(base_uv, step_size,  0.0, -2.0);
    vec3 l8 = SampleBloom(base_uv, step_size,  2.0, -2.0);
    return (h0 + h1 + h2 + h3) * 0.125 +
        (l0 + l1 + l2 + l3 + l4 + l5 + l6 + l7 + l8) * 0.0555555;
}

void main()
{
    vec2 step_size = 1.0 / vec2(textureSize(source_image, 0));
    out_color = vec4(PostFromDisplay(
        SmoothBloomDownsample(in_uv, step_size)), 1.0);
}
