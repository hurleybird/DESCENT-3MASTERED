#version 460
#extension GL_GOOGLE_include_directive : require

#include "post_uniforms.glsl"

layout(set = 0, binding = 2) uniform texture2D cockpit_image;
layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main()
{
    // The pipeline uses GL4's ONE, ONE_MINUS_SRC_ALPHA factors while loading
    // PostPresent as the destination attachment.
    out_color = texture(sampler2D(cockpit_image, post_samplers[1]),
        PostPrimaryUv(in_uv));
}
