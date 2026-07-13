#version 460
#extension GL_GOOGLE_include_directive : require

#include "post_uniforms.glsl"

layout(set = 0, binding = 2) uniform texture2D velocity_image;
layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec2 out_velocity;

void main()
{
    out_velocity = texture(sampler2D(velocity_image, post_samplers[0]),
        PostVelocityUv(in_uv)).xy;
}
