#version 460
#extension GL_GOOGLE_include_directive : require

#include "post_uniforms.glsl"

layout(set = 0, binding = 2) uniform utexture2DMS source_image;
layout(location = 0) out uint out_value;

void main()
{
    ivec2 pixel = clamp(ivec2(gl_FragCoord.xy), ivec2(0),
        textureSize(source_image) - ivec2(1));
    out_value = texelFetch(source_image, pixel, 0).r;
}
