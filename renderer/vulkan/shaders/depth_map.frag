#version 460
#extension GL_GOOGLE_include_directive : require

#include "post_uniforms.glsl"

layout(set = 0, binding = 2) uniform texture2D source_depth;

void main()
{
    ivec2 destination_pixel = ivec2(gl_FragCoord.xy);
    vec2 source_extent = post.source_extent_inv_extent.xy;
    vec2 destination_extent = post.destination_extent_inv_extent.xy;
    ivec2 source_pixel = ivec2(floor((vec2(destination_pixel) + vec2(0.5)) *
        source_extent / max(destination_extent, vec2(1.0))));
    source_pixel = clamp(source_pixel, ivec2(0), textureSize(source_depth, 0) - ivec2(1));
    gl_FragDepth = texelFetch(sampler2D(source_depth, post_samplers[6]),
        source_pixel, 0).r;
}
