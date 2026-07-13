#version 460
#extension GL_GOOGLE_include_directive : require

#include "post_uniforms.glsl"

layout(set = 0, binding = 2) uniform texture2D scene_color_image;
layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main()
{
    ivec2 source_size = textureSize(scene_color_image, 0);
    ivec2 source_pixel = ivec2(
        clamp(PostPrimaryUv(in_uv), vec2(0.0), vec2(0.999999)) *
        vec2(source_size));
    out_color = texelFetch(
        sampler2D(scene_color_image, post_samplers[0]),
        clamp(source_pixel, ivec2(0), source_size - ivec2(1)),
        0);
}
