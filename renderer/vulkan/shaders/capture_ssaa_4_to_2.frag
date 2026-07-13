#version 460
#extension GL_GOOGLE_include_directive : require

#include "post_uniforms.glsl"
#include "post_math.glsl"

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 2) uniform texture2D single_sample_texture;
layout(set = 0, binding = 3) uniform texture2D msaa_resolved_texture;

void main()
{
    vec2 source_uv = PostPrimaryUv(in_uv);
    vec4 filtered;
    if (post.frame_branch.y == POST_SOURCE_MSAA_RESOLVED_2D)
        filtered = PostWronskiFilterAtUv(msaa_resolved_texture, source_uv);
    else
        filtered = PostWronskiFilterAtUv(single_sample_texture, source_uv);
    out_color = PostQuantizeRgba8(filtered);
}
