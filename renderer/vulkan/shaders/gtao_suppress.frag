#version 460
#extension GL_GOOGLE_include_directive : require

#include "post_uniforms.glsl"

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 2) uniform texture2D protection_mask_texture;
layout(set = 0, binding = 3) uniform texture2D authored_color_texture;

bool SourceVisibleUv(ivec2 pixel, out vec2 uv)
{
    vec2 input_screen_size = post.screen_size_inv_size.xy;
    if (!PostFeature(POST_SOURCE_VISIBLE_RECT))
    {
        uv = (vec2(pixel) + vec2(0.5)) / input_screen_size;
        return true;
    }

    vec2 local = vec2(pixel) - post.source_visible_origin_size.xy;
    vec2 source_visible_size = post.source_visible_origin_size.zw;
    if (local.x < 0.0 || local.y < 0.0 ||
        local.x >= source_visible_size.x ||
        local.y >= source_visible_size.y)
    {
        uv = vec2(0.0);
        return false;
    }
    uv = (local + vec2(0.5)) / source_visible_size;
    return true;
}

float MaskAtPixel(ivec2 pixel)
{
    ivec2 input_size = ivec2(post.screen_size_inv_size.xy);
    pixel = clamp(pixel, ivec2(0), input_size - ivec2(1));
    vec2 uv;
    if (!SourceVisibleUv(pixel, uv))
        return 0.0;

    ivec2 mask_size = textureSize(protection_mask_texture, 0);
    ivec2 mask_pixel = clamp(
        ivec2(uv * vec2(mask_size)), ivec2(0), mask_size - ivec2(1));
    return texelFetch(
        sampler2D(protection_mask_texture, post_samplers[0]),
        mask_pixel,
        0).r;
}

float BloomAmountFromColor(vec3 color)
{
    float gamma = post.bloom_gamma_threshold_intensity_spread.x;
    float bloom_threshold = post.bloom_gamma_threshold_intensity_spread.y;
    vec3 display_color = pow(max(color, vec3(0.0)), vec3(gamma));
    float brightness = max(max(display_color.r, display_color.g), display_color.b);
    float range = max(1.0 - bloom_threshold, 0.0001);
    return clamp((brightness - bloom_threshold) / range, 0.0, 1.0);
}

float BloomAtPixel(ivec2 pixel)
{
    ivec2 input_size = ivec2(post.screen_size_inv_size.xy);
    pixel = clamp(pixel, ivec2(0), input_size - ivec2(1));
    vec2 uv;
    if (!SourceVisibleUv(pixel, uv))
        return 0.0;

    ivec2 color_size = textureSize(authored_color_texture, 0);
    ivec2 color_pixel = clamp(
        ivec2(uv * vec2(color_size)), ivec2(0), color_size - ivec2(1));
    vec3 color = texelFetch(
        sampler2D(authored_color_texture, post_samplers[0]),
        color_pixel,
        0).rgb;
    return BloomAmountFromColor(color);
}

void SourceBlock(out ivec2 source_min, out ivec2 source_max)
{
    ivec2 ao_pixel = ivec2(gl_FragCoord.xy);
    vec2 input_screen_size = post.screen_size_inv_size.xy;
    vec2 ao_screen_size = post.ao_screen_size_inv_size.xy;
    ivec2 input_size = ivec2(input_screen_size);
    vec2 source_min_f = floor(
        vec2(ao_pixel) * input_screen_size / ao_screen_size);
    vec2 source_max_f = ceil(
        vec2(ao_pixel + ivec2(1)) * input_screen_size / ao_screen_size);
    source_min = clamp(
        ivec2(source_min_f), ivec2(0), input_size - ivec2(1));
    source_max = clamp(
        max(ivec2(source_max_f), source_min + ivec2(1)),
        ivec2(1),
        input_size);
}

float MaskAmount()
{
    if (!PostFeature(POST_HAS_MASK))
        return 0.0;

    ivec2 source_min;
    ivec2 source_max;
    SourceBlock(source_min, source_max);
    float mask = 0.0;
    float count = 0.0;
    for (int y = 0; y < 8; ++y)
    {
        int py = source_min.y + y;
        if (py >= source_max.y)
            break;
        for (int x = 0; x < 8; ++x)
        {
            int px = source_min.x + x;
            if (px >= source_max.x)
                break;
            mask += MaskAtPixel(ivec2(px, py));
            count += 1.0;
        }
    }
    return mask / max(count, 1.0);
}

float BloomAmount()
{
    if (!PostFeature(POST_USE_BLOOM_MASK))
        return 0.0;

    ivec2 source_min;
    ivec2 source_max;
    SourceBlock(source_min, source_max);
    float bloom = 0.0;
    for (int y = 0; y < 8; ++y)
    {
        int py = source_min.y + y;
        if (py >= source_max.y)
            break;
        for (int x = 0; x < 8; ++x)
        {
            int px = source_min.x + x;
            if (px >= source_max.x)
                break;
            bloom = max(bloom, BloomAtPixel(ivec2(px, py)));
        }
    }
    return bloom;
}

void main()
{
    float suppression = MaskAmount();
    if (PostFeature(POST_USE_BLOOM_MASK))
        suppression = max(suppression, BloomAmount());
    out_color = vec4(clamp(suppression, 0.0, 1.0), 0.0, 0.0, 1.0);
}
