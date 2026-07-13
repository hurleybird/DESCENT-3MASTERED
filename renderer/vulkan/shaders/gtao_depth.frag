#version 460
#extension GL_GOOGLE_include_directive : require

#include "post_uniforms.glsl"

layout(set = 0, binding = 2) uniform texture2D depth_image;
layout(set = 0, binding = 3) uniform texture2D ao_class_image;
layout(location = 0) out vec4 out_depth_weight;

struct DepthWeight
{
    float depth;
    float ao_weight;
};

float FetchDepth(ivec2 pixel)
{
    ivec2 input_size = ivec2(post.screen_size_inv_size.xy);
    pixel = clamp(pixel, ivec2(0), input_size - ivec2(1));
    return texelFetch(sampler2D(depth_image, post_samplers[6]), pixel, 0).r;
}

float AOClassWeight(float encoded_class)
{
    float value = floor(encoded_class * 255.0 + 0.5);
    if (value == 1.0)
        return clamp(post.ao_class_weights.x, 0.0, 1.0);
    if (value == 2.0)
        return clamp(post.ao_class_weights.y, 0.0, 1.0);
    if (value == 3.0)
        return clamp(post.ao_class_weights.z, 0.0, 1.0);
    if (value == 4.0)
        return clamp(post.ao_class_weights.w, 0.0, 1.0);
    return 1.0;
}

DepthWeight FetchDepthWeight(ivec2 pixel)
{
    DepthWeight value;
    value.depth = FetchDepth(pixel);
    value.ao_weight = 1.0;
    if (PostFeature(POST_HAS_AO_CLASS))
    {
        vec2 uv = (vec2(pixel) + vec2(0.5)) * post.screen_size_inv_size.zw;
        float stored_value = texture(
            sampler2D(ao_class_image, post_samplers[5]), uv).r;
        value.ao_weight = PostFeature(POST_AO_WEIGHT_IS_DIRECT) ?
            clamp(stored_value, 0.0, 1.0) : AOClassWeight(stored_value);
    }
    return value;
}

DepthWeight MinDepthWeight(DepthWeight left, DepthWeight right)
{
    return right.depth < left.depth ? right : left;
}

void main()
{
    ivec2 ao_pixel = ivec2(gl_FragCoord.xy);
    ivec2 input_size = ivec2(post.screen_size_inv_size.xy);
    vec2 src_center = (vec2(ao_pixel) + vec2(0.5)) *
        (post.screen_size_inv_size.xy / post.ao_screen_size_inv_size.xy);
    ivec2 src_origin = ivec2(floor(src_center - vec2(0.5)));
    ivec2 p00 = clamp(src_origin, ivec2(0), input_size - ivec2(1));
    ivec2 p10 = clamp(src_origin + ivec2(1, 0), ivec2(0), input_size - ivec2(1));
    ivec2 p01 = clamp(src_origin + ivec2(0, 1), ivec2(0), input_size - ivec2(1));
    ivec2 p11 = clamp(src_origin + ivec2(1, 1), ivec2(0), input_size - ivec2(1));
    DepthWeight best = FetchDepthWeight(p00);
    best = MinDepthWeight(best, FetchDepthWeight(p10));
    best = MinDepthWeight(best, FetchDepthWeight(p01));
    best = MinDepthWeight(best, FetchDepthWeight(p11));
    out_depth_weight = vec4(best.depth, best.ao_weight, 0.0, 1.0);
}
