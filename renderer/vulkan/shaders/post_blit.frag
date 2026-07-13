#version 460
#extension GL_GOOGLE_include_directive : require

#include "post_uniforms.glsl"

layout(set = 0, binding = 2) uniform texture2D source_image;
layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main()
{
    vec4 color = texture(sampler2D(source_image, post_samplers[0]),
        PostPrimaryUv(in_uv));
    float display_gamma = post.bloom_gamma_threshold_intensity_spread.x;
    if (display_gamma != 1.0)
        color.rgb = pow(max(color.rgb, vec3(0.0)), vec3(display_gamma));
    out_color = color;
}
