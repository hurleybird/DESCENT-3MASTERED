#version 460
#extension GL_GOOGLE_include_directive : require

#include "post_uniforms.glsl"

layout(set = 0, binding = 2) uniform texture2D current_level_image;
layout(set = 0, binding = 3) uniform texture2D smaller_level_image;
layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

vec3 Blur3x3(vec2 uv, vec2 scale)
{
    const float weights[9] = float[9](
        0.0625, 0.125, 0.0625,
        0.125, 0.25, 0.125,
        0.0625, 0.125, 0.0625);
    vec3 sum = vec3(0.0);
    int index = 0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x, ++index)
            sum += texture(sampler2D(smaller_level_image, post_samplers[4]),
                uv + vec2(x, y) * scale).rgb * weights[index];
    }
    return sum;
}

void main()
{
    float blur_scale = 1.5;
    vec3 current_sample = texture(
        sampler2D(current_level_image, post_samplers[4]), in_uv).rgb;
    vec2 upsample_scale = vec2(blur_scale) /
        vec2(textureSize(smaller_level_image, 0));
    vec3 upsampled = Blur3x3(in_uv, upsample_scale);
    out_color = vec4(mix(current_sample, upsampled,
        post.bloom_gamma_threshold_intensity_spread.w), 1.0);
}
