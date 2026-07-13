#version 460
#extension GL_GOOGLE_include_directive : require

#include "post_uniforms.glsl"

layout(set = 0, binding = 2) uniform texture2DMS source_depth;

void main()
{
    ivec2 pixel = clamp(ivec2(gl_FragCoord.xy), ivec2(0),
        textureSize(source_depth) - ivec2(1));
    gl_FragDepth = texelFetch(source_depth, pixel, 0).r;
}
