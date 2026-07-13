#version 460
#extension GL_GOOGLE_include_directive : require

#include "post_uniforms.glsl"

layout(set = 0, binding = 2) uniform texture2DMS scene_color_image;
layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main()
{
    ivec2 source_size = textureSize(scene_color_image);
    ivec2 source_pixel = ivec2(
        clamp(PostPrimaryUv(in_uv), vec2(0.0), vec2(0.999999)) *
        vec2(source_size));
    source_pixel = clamp(
        source_pixel, ivec2(0), source_size - ivec2(1));
    int samples = clamp(int(post.sample_counts.x), 1, 8);
    vec4 sum = vec4(0.0);
    for (int index = 0; index < 8; ++index)
    {
        if (index >= samples)
            break;
        sum += texelFetch(scene_color_image, source_pixel, index);
    }
    out_color = sum / float(samples);
}
