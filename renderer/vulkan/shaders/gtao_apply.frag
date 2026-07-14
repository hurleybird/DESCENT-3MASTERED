#version 460
#extension GL_GOOGLE_include_directive : require

#include "post_uniforms.glsl"

layout(set = 0, binding = 2) uniform texture2D authored_color_image;
layout(set = 0, binding = 3) uniform texture2D ao_image;
layout(set = 0, binding = 4) uniform texture2D suppression_image;
layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main()
{
    vec2 ao_uv = PostAoUv(in_uv);
    vec4 ao_depth = texture(sampler2D(ao_image, post_samplers[1]), ao_uv);
    int debug_channel = post.integer_params.y;
    float ao = debug_channel == 2 ? ao_depth.z : ao_depth.x;
    ao = clamp(ao, 0.0, 1.0);
    if (PostFeature(POST_HAS_SUPPRESSION_MASK))
    {
        float suppression = clamp(texture(
            sampler2D(suppression_image, post_samplers[1]), ao_uv).r,
            0.0, 1.0);
        ao = mix(ao, 1.0, suppression);
    }
    // GL4 passes 1.0 for AO-only debug preview and the configured intensity
    // for normal composition. Keep diagnostic values unwarped so paired
    // captures expose the raw AO/depth signal.
    float intensity = debug_channel != 0 ? 1.0 :
        post.ao_max_radius_neg_inv_radius2_bias_intensity.w;
    ao = pow(ao, intensity);

    if (debug_channel != 0)
    {
        out_color = vec4(vec3(ao), 1.0);
        return;
    }
    vec4 authored = texture(
        sampler2D(authored_color_image, post_samplers[0]), PostPrimaryUv(in_uv));
    out_color = vec4(authored.rgb * ao, authored.a);
}
