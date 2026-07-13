#ifndef PICCU_POST_MATH_GLSL
#define PICCU_POST_MATH_GLSL

vec3 PostToDisplay(vec3 color)
{
    return pow(max(color, vec3(0.0)),
        vec3(post.bloom_gamma_threshold_intensity_spread.x));
}

vec3 PostFromDisplay(vec3 color)
{
    float gamma_value = post.bloom_gamma_threshold_intensity_spread.x;
    return pow(max(color, vec3(0.0)),
        vec3(1.0 / max(gamma_value, 0.0001)));
}

vec4 PostQuantizeRgba8(vec4 value)
{
    return round(clamp(value, 0.0, 1.0) * 255.0) / 255.0;
}

vec4 PostWronskiFilterAtUv(texture2D source, vec2 uv)
{
    const float d0 = 0.75777;
    const float d1 = 2.907;
    const float w0 = 0.37487566;
    const float w1 = -0.12487566;
    vec2 inv_source_size = 1.0 / vec2(textureSize(source, 0));
    vec2 uv_min = vec2(0.0);
    vec2 uv_max = vec2(1.0);
    if (post.source_visible_origin_size.z > 0.0 &&
        post.source_visible_origin_size.w > 0.0)
    {
        uv_min = (post.source_visible_origin_size.xy + vec2(0.5)) *
            inv_source_size;
        uv_max = (post.source_visible_origin_size.xy +
            post.source_visible_origin_size.zw - vec2(0.5)) * inv_source_size;
    }
    vec4 c0 = texture(sampler2D(source, post_samplers[1]), clamp(uv + vec2( d0,  d0) * inv_source_size, uv_min, uv_max));
    vec4 c1 = texture(sampler2D(source, post_samplers[1]), clamp(uv + vec2(-d0,  d0) * inv_source_size, uv_min, uv_max));
    vec4 c2 = texture(sampler2D(source, post_samplers[1]), clamp(uv + vec2( d0, -d0) * inv_source_size, uv_min, uv_max));
    vec4 c3 = texture(sampler2D(source, post_samplers[1]), clamp(uv + vec2(-d0, -d0) * inv_source_size, uv_min, uv_max));
    vec4 c4 = texture(sampler2D(source, post_samplers[1]), clamp(uv + vec2( d1,  0.0) * inv_source_size, uv_min, uv_max));
    vec4 c5 = texture(sampler2D(source, post_samplers[1]), clamp(uv + vec2(-d1,  0.0) * inv_source_size, uv_min, uv_max));
    vec4 c6 = texture(sampler2D(source, post_samplers[1]), clamp(uv + vec2( 0.0,  d1) * inv_source_size, uv_min, uv_max));
    vec4 c7 = texture(sampler2D(source, post_samplers[1]), clamp(uv + vec2( 0.0, -d1) * inv_source_size, uv_min, uv_max));
    vec3 average = (PostToDisplay(c0.rgb) + PostToDisplay(c1.rgb) +
        PostToDisplay(c2.rgb) + PostToDisplay(c3.rgb)) * w0 +
        (PostToDisplay(c4.rgb) + PostToDisplay(c5.rgb) +
         PostToDisplay(c6.rgb) + PostToDisplay(c7.rgb)) * w1;
    float alpha = (c0.a + c1.a + c2.a + c3.a) * w0 +
        (c4.a + c5.a + c6.a + c7.a) * w1;
    return vec4(PostFromDisplay(average), clamp(alpha, 0.0, 1.0));
}

vec4 PostWronskiFilter(texture2D source, ivec2 destination_pixel)
{
    vec2 inv_source_size = 1.0 / vec2(textureSize(source, 0));
    vec2 uv = (vec2(destination_pixel) * 2.0 + vec2(1.0)) *
        inv_source_size;
    return PostWronskiFilterAtUv(source, uv);
}

#endif
