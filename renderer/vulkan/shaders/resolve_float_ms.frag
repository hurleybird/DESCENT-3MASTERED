#version 460
#extension GL_GOOGLE_include_directive : require

#include "post_uniforms.glsl"

layout(set = 0, binding = 2) uniform texture2DMS source_image;
layout(location = 0) out vec4 out_value;

void main()
{
    ivec2 pixel = clamp(ivec2(gl_FragCoord.xy), ivec2(0),
        textureSize(source_image) - ivec2(1));
    int samples = clamp(int(post.sample_counts.x), 1, 8);
    vec4 sum = vec4(0.0);
    for (int i = 0; i < 8; ++i)
    {
        if (i >= samples)
            break;
        sum += texelFetch(source_image, pixel, i);
    }
    out_value = sum / float(samples);
}
