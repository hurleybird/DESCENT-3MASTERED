#version 460
#extension GL_GOOGLE_include_directive : require

#include "post_uniforms.glsl"

layout(set = 0, binding = 2) uniform texture2D final_color_image;
layout(set = 0, binding = 3) uniform texture2D captured_scene_image;
layout(set = 0, binding = 4) uniform texture2D ao_scene_image;
layout(set = 0, binding = 5) uniform texture2D protection_image;
layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main()
{
    vec4 final_color = texture(
        sampler2D(final_color_image, post_samplers[0]), in_uv);
    vec2 scene_uv = PostSceneUv(in_uv);
    vec3 scene_color = texture(
        sampler2D(captured_scene_image, post_samplers[0]), scene_uv).rgb;
    vec3 ao_scene_color = texture(
        sampler2D(ao_scene_image, post_samplers[0]), scene_uv).rgb;
    float scene_mask = 1.0 - clamp(final_color.a, 0.0, 1.0);
    if (PostFeature(POST_USE_VISIBLE_RECT))
    {
        vec2 local = gl_FragCoord.xy - post.visible_origin_size.xy;
        if (local.x < 0.0 || local.y < 0.0 ||
            local.x >= post.visible_origin_size.z ||
            local.y >= post.visible_origin_size.w)
            scene_mask = 0.0;
    }
    if (PostFeature(POST_USE_PROTECTION_MASK))
    {
        ivec2 mask_size = textureSize(protection_image, 0);
        ivec2 mask_pixel = ivec2(clamp(in_uv, vec2(0.0),
            vec2(0.999999)) * vec2(mask_size));
        float protection = texelFetch(
            sampler2D(protection_image, post_samplers[5]),
            mask_pixel, 0).r;
        scene_mask *= 1.0 - clamp(protection, 0.0, 1.0);
    }
    out_color = vec4(max(final_color.rgb +
        (ao_scene_color - scene_color) * scene_mask, vec3(0.0)),
        final_color.a);
}
