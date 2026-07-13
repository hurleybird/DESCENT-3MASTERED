#version 460
#extension GL_GOOGLE_include_directive : require

#include "post_uniforms.glsl"

layout(set = 0, binding = 2) uniform texture2D post_present_image;
layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main()
{
    // The swapchain is UNORM, never _SRGB. The post image is already encoded.
    out_color = texture(sampler2D(post_present_image, post_samplers[0]),
        PostPrimaryUv(in_uv));
}
