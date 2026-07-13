#version 460
#extension GL_GOOGLE_include_directive : require

#include "post_uniforms.glsl"

layout(set = 0, binding = 2) uniform texture2D ao_image;
layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_ao;

void main()
{
    vec2 center = texture(sampler2D(ao_image, post_samplers[0]), in_uv).xy;
    float center_ao = center.x;
    float center_depth = center.y;
    int kernel_radius = post.integer_params.x;
    if (kernel_radius <= 0)
    {
        out_ao = vec4(center_ao, center_depth, 0.0, 1.0);
        return;
    }

    float sigma = float(kernel_radius) * 0.5;
    float falloff = 1.0 / (2.0 * sigma * sigma);
    float total_ao = center_ao;
    float total_weight = 1.0;
    for (int radius = 1; radius <= 20; ++radius)
    {
        if (radius > kernel_radius)
            break;
        vec2 positive_uv = in_uv + float(radius) *
            post.blur_delta_sharpness_reserved.xy;
        vec2 negative_uv = in_uv - float(radius) *
            post.blur_delta_sharpness_reserved.xy;
        vec2 positive = texture(sampler2D(ao_image, post_samplers[0]),
            positive_uv).xy;
        vec2 negative = texture(sampler2D(ao_image, post_samplers[0]),
            negative_uv).xy;
        float positive_delta = (center_depth - positive.y) *
            post.blur_delta_sharpness_reserved.z;
        float negative_delta = (center_depth - negative.y) *
            post.blur_delta_sharpness_reserved.z;
        float positive_weight = exp2(-float(radius * radius) * falloff -
            positive_delta * positive_delta);
        float negative_weight = exp2(-float(radius * radius) * falloff -
            negative_delta * negative_delta);
        total_ao += positive.x * positive_weight + negative.x * negative_weight;
        total_weight += positive_weight + negative_weight;
    }
    total_ao /= max(total_weight, 1e-6);
    out_ao = vec4(total_ao, center_depth, 0.0, 1.0);
}
