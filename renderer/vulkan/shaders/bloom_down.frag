#version 460
#extension GL_GOOGLE_include_directive : require

#include "post_uniforms.glsl"

layout(set = 0, binding = 2) uniform texture2D source_image;
layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

vec3 SampleWithStep(vec2 uv, vec2 step_size, float x, float y)
{
    return texture(sampler2D(source_image, post_samplers[4]),
        uv + step_size * vec2(x, y)).rgb;
}

void main()
{
    vec2 step_size = 1.0 / vec2(textureSize(source_image, 0));
    vec3 h0 = SampleWithStep(in_uv, step_size, -1.0,  1.0);
    vec3 h1 = SampleWithStep(in_uv, step_size,  1.0,  1.0);
    vec3 h2 = SampleWithStep(in_uv, step_size, -1.0, -1.0);
    vec3 h3 = SampleWithStep(in_uv, step_size,  1.0, -1.0);
    vec3 l0 = SampleWithStep(in_uv, step_size, -2.0,  2.0);
    vec3 l1 = SampleWithStep(in_uv, step_size,  0.0,  2.0);
    vec3 l2 = SampleWithStep(in_uv, step_size,  2.0,  2.0);
    vec3 l3 = SampleWithStep(in_uv, step_size, -2.0,  0.0);
    vec3 l4 = SampleWithStep(in_uv, step_size,  0.0,  0.0);
    vec3 l5 = SampleWithStep(in_uv, step_size,  2.0,  0.0);
    vec3 l6 = SampleWithStep(in_uv, step_size, -2.0, -2.0);
    vec3 l7 = SampleWithStep(in_uv, step_size,  0.0, -2.0);
    vec3 l8 = SampleWithStep(in_uv, step_size,  2.0, -2.0);
    out_color = vec4((h0 + h1 + h2 + h3) * 0.125 +
        (l0 + l1 + l2 + l3 + l4 + l5 + l6 + l7 + l8) * 0.0555555,
        1.0);
}
