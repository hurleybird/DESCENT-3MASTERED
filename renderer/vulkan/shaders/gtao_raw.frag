#version 460
#extension GL_GOOGLE_include_directive : require

#include "post_uniforms.glsl"

layout(set = 0, binding = 2) uniform texture2D depth_weight_image;
layout(set = 0, binding = 3) uniform texture2D noise_image;
layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_ao;

const float PI = 3.14159265358979323846;

float LinearizeDepth(float depth)
{
    float z_n = 2.0 * depth - 1.0;
    float near_z = post.near_far_radius_radius_pixels.x;
    float far_z = post.near_far_radius_radius_pixels.y;
    return (2.0 * near_z * far_z) /
        (far_z + near_z - z_n * (far_z - near_z));
}

vec2 LegacyToCanonicalUv(vec2 legacy_uv)
{
    return vec2(legacy_uv.x, 1.0 - legacy_uv.y);
}

vec3 FetchViewPosition(vec2 legacy_uv)
{
    vec2 canonical_uv = LegacyToCanonicalUv(legacy_uv);
    vec2 depth_uv = clamp(canonical_uv, vec2(0.0),
        vec2(1.0) - post.ao_screen_size_inv_size.zw * 0.5);
    float depth = LinearizeDepth(texture(
        sampler2D(depth_weight_image, post_samplers[6]), depth_uv).r);
    vec3 position;
    position.xy = (legacy_uv * post.projection_info.xy +
        post.projection_info.zw) * depth;
    position.z = depth;
    return position;
}

vec3 MinDiff(vec3 position, vec3 right_or_bottom, vec3 left_or_top)
{
    vec3 first = right_or_bottom - position;
    vec3 second = position - left_or_top;
    return dot(first, first) < dot(second, second) ? first : second;
}

vec3 ReconstructNormal(vec2 uv, vec3 position)
{
    vec2 delta = post.ao_screen_size_inv_size.zw;
    vec3 right = FetchViewPosition(uv + vec2(delta.x, 0.0));
    vec3 left = FetchViewPosition(uv - vec2(delta.x, 0.0));
    vec3 top = FetchViewPosition(uv + vec2(0.0, delta.y));
    vec3 bottom = FetchViewPosition(uv - vec2(0.0, delta.y));

    return -normalize(cross(MinDiff(position, right, left),
        MinDiff(position, top, bottom)));
}

float Falloff(float distance_squared)
{
    return distance_squared *
        post.ao_max_radius_neg_inv_radius2_bias_intensity.y + 1.0;
}

float FetchAoWeight(vec2 legacy_uv)
{
    vec2 depth_uv = clamp(LegacyToCanonicalUv(legacy_uv), vec2(0.0),
        vec2(1.0) - post.ao_screen_size_inv_size.zw * 0.5);
    return clamp(texture(sampler2D(depth_weight_image, post_samplers[6]),
        depth_uv).g, 0.0, 1.0);
}

float ComputeHorizon(vec2 legacy_uv, vec3 position, vec3 normal,
    vec2 direction, float step_pixels, float jitter, float side)
{
    float ray_pixels = jitter * step_pixels + 1.0;
    float horizon = 0.0;
    float average = 0.0;
    int steps = max(int(post.sample_counts.w), 1);
    for (int step_index = 0; step_index < steps; ++step_index)
    {
        vec2 sample_uv = legacy_uv + round(ray_pixels * direction * side) *
            post.ao_screen_size_inv_size.zw;
        vec3 sample_position = FetchViewPosition(sample_uv);
        float sample_weight = FetchAoWeight(sample_uv);
        vec3 offset = sample_position - position;
        float distance_squared = dot(offset, offset);
        float normal_dot = dot(normal, offset) *
            inversesqrt(max(distance_squared, 1e-6));
        float sample_horizon = max(normal_dot -
            post.ao_max_radius_neg_inv_radius2_bias_intensity.z, 0.0) *
            clamp(Falloff(distance_squared), 0.0, 1.0);
        sample_horizon *= sample_weight;
        horizon = max(horizon, sample_horizon);
        average += sample_horizon;
        ray_pixels += step_pixels;
    }
    average /= max(float(steps), 1.0);
    return mix(average, horizon, 0.75);
}

vec2 FetchNoise(vec2 legacy_screen_position)
{
    return texture(sampler2D(noise_image, post_samplers[2]),
        legacy_screen_position / 4.0).rg;
}

void main()
{
    vec2 legacy_uv = PostTopLeftToLegacyUv(in_uv);
    vec3 position = FetchViewPosition(legacy_uv);
    float far_z = post.near_far_radius_radius_pixels.y;
    float depth_for_blur = clamp(position.z / max(far_z, 1e-4), 0.0, 1.0);
    if (position.z >= far_z * 0.999)
    {
        out_ao = vec4(1.0, depth_for_blur, 0.0, 1.0);
        return;
    }

    vec3 normal = ReconstructNormal(legacy_uv, position);
    int steps = max(int(post.sample_counts.w), 1);
    float step_pixels = min(post.near_far_radius_radius_pixels.w /
        max(position.z, 1e-3),
        post.ao_max_radius_neg_inv_radius2_bias_intensity.x);
    step_pixels /= float(steps + 1);

    vec2 screen_position = legacy_uv * post.ao_screen_size_inv_size.xy -
        post.noise_origin_jitter.xy;
    vec2 random_value = fract(FetchNoise(screen_position) +
        post.noise_origin_jitter.zw);

    float ao = 0.0;
    int gtao_directions = max((int(post.sample_counts.z) + 1) / 2, 1);
    float alpha = PI / float(gtao_directions);
    for (int direction_index = 0;
        direction_index < gtao_directions; ++direction_index)
    {
        float angle = alpha * (float(direction_index) + random_value.x);
        vec2 direction = vec2(cos(angle), sin(angle));
        float jitter = fract(random_value.y +
            float(direction_index) * 0.61803398875);
        ao += ComputeHorizon(legacy_uv, position, normal, direction,
            step_pixels, jitter, 1.0);
        ao += ComputeHorizon(legacy_uv, position, normal, direction,
            step_pixels, jitter, -1.0);
    }
    ao /= max(float(gtao_directions) * 2.0, 1.0);
    ao /= max(1.0 -
        post.ao_max_radius_neg_inv_radius2_bias_intensity.z, 0.01);
    ao = clamp(1.0 - ao, 0.0, 1.0);
    out_ao = vec4(ao, depth_for_blur, 0.0, 1.0);
}
