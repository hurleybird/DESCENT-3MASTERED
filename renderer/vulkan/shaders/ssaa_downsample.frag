#version 460
#extension GL_GOOGLE_include_directive : require

#include "post_uniforms.glsl"
#include "post_math.glsl"

layout(set = 0, binding = 2) uniform texture2D source_image;
layout(location = 0) out vec4 out_color;

void main()
{
    ivec2 destination_pixel = ivec2(gl_FragCoord.xy) - post.integer_params.zw;
    out_color = PostWronskiFilter(source_image, destination_pixel);
}
