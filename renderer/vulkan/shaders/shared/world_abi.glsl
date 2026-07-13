#ifndef PICCU_VULKAN_WORLD_ABI_GLSL
#define PICCU_VULKAN_WORLD_ABI_GLSL

#extension GL_EXT_nonuniform_qualifier : require

const uint SHADER_TEXTURED = 1u << 0;
const uint SHADER_LIGHTMAPPED = 1u << 1;
const uint SHADER_GENERIC_FOG = 1u << 2;
const uint SHADER_PHONG = 1u << 3;
const uint SHADER_DYNAMIC_LIGHTS = 1u << 4;
const uint SHADER_PER_PIXEL_SPECULAR = 1u << 5;
const uint SHADER_SPECULAR_MASK = 1u << 6;
const uint SHADER_FIELD_SPECULAR = 1u << 7;
const uint SHADER_SOFT_PARTICLE = 1u << 8;
const uint SHADER_LUMINANCE_POST_MASK = 1u << 9;
const uint SHADER_POST_MASK_ONLY = 1u << 10;
const uint SHADER_MOTION_WRITE = 1u << 11;
const uint SHADER_OBJECT_ID_WRITE = 1u << 12;
const uint SHADER_COCKPIT = 1u << 13;
const uint SHADER_TERRAIN = 1u << 14;
const uint SHADER_SPECIAL_TEXTURE_FLAT = 1u << 15;
const uint SHADER_AO_CAPTURE_WEIGHT = 1u << 16;
const uint SHADER_TERRAIN_FOG_BLOOM_SUPPRESSION = 1u << 17;
const uint DRAW_GEOMETRY_MODE_MASK = 3u;
const uint DRAW_GEOMETRY_T0 = 0u;
const uint DRAW_GEOMETRY_T1 = 1u;
const uint DRAW_GEOMETRY_T2 = 2u;
const uint DRAW_TARGET_ABSOLUTE = 1u << 2;
const uint DRAW_HAS_PERSPECTIVE_PAYLOAD = 1u << 3;
const uint DRAW_HAS_MOTION_PAYLOAD = 1u << 4;
const uint DRAW_HAS_SPECULAR_PAYLOAD = 1u << 5;
const uint FRAME_HAS_PREVIOUS_VIEW_PROJECTION = 1u << 0;

struct DrawHeader {
    uint state_index; uint material_index; uint transform_index; uint flags;
    uint vertex_payload_offset; uint motion_payload_offset;
    uint specular_payload_offset; uint room_or_terrain_index;
};
struct ShaderState {
    uint shader_flags; uint texture_type; uint overlay_type; uint lighting_color_model;
    uint alpha_type; uint alpha_value; uint blend_class; uint draw_classification;
    float alpha_factor; float z_bias; float fog_near_mapped; float fog_far_mapped;
    vec4 flat_color; vec4 fog_color; vec4 light_direction; vec4 post_values;
    uint dynamic_light_first; uint dynamic_light_count; uint specular_block_index; uint motion_object_id;
    uint motion_flags; uint ao_class; uint state_flags2; uint reserved0;
};
struct Material { uvec4 image2d; uvec4 image2d_array; uvec4 sampler_index; vec4 uv_params; };
struct Transform { mat4 current_model; mat4 previous_model; };
struct DynamicLight { vec4 position_radius; vec4 color_falloff; vec4 direction_dot_range; vec4 specular_position_radius; vec4 specular_and_flags; };
struct SpecularDef { vec4 center; vec4 color; };
struct SpecularBlock { int count; int exponent; float strength; float lightmap_mix; float alpha_strength; float field_mode; float debug_tint; float debug_authored; SpecularDef sources[4]; };
struct WorldAux { vec4 fog_color; vec4 fog_plane; vec4 params; uvec4 indices; };

layout(std140, set=0, binding=0) uniform FrameViewBlock {
    mat4 projection; mat4 view; mat4 view_projection; mat4 inverse_modelview;
    mat4 inverse_view_projection; mat4 previous_view_projection;
    mat4 cockpit_previous_view_projection; vec4 viewport_xywh;
    vec4 visible_origin_size; vec4 target_extent_inv_extent; uvec4 history_target_flags;
} frame_view;
layout(set=0, binding=1) uniform sampler world_samplers[32];
layout(set=1, binding=0) uniform texture2D float_images[];
layout(set=1, binding=1) uniform texture2DArray float_array_images[8];
layout(std430, set=2, binding=0) readonly buffer DrawHeaders { DrawHeader draw_headers[]; };
layout(std430, set=2, binding=1) readonly buffer ShaderStates { ShaderState shader_states[]; };
layout(std430, set=2, binding=2) readonly buffer Materials { Material materials[]; };
layout(std430, set=2, binding=3) readonly buffer Transforms { Transform transforms[]; };
layout(std430, set=2, binding=4) readonly buffer DynamicLights { DynamicLight dynamic_lights[]; };
layout(std430, set=2, binding=5) readonly buffer SpecularBlocks { SpecularBlock specular_blocks[]; };
layout(std430, set=2, binding=6) readonly buffer OptionalPayload { uint payload_words[]; };
layout(std430, set=2, binding=7) readonly buffer WorldAuxRecords { WorldAux world_aux_records[]; };

layout(push_constant) uniform WorldPush {
    uint draw_header_base; uint view_record_index; uint target_flags; uint payload_word_base;
} world_push;

vec4 SampleWorld2D(uint image_index, uint sampler_index, vec2 uv) {
    return texture(sampler2D(float_images[nonuniformEXT(image_index)],
        world_samplers[nonuniformEXT(sampler_index)]), uv);
}

vec4 SampleWorldArray(uint image_index, uint sampler_index, vec3 uv) {
    return texture(sampler2DArray(float_array_images[nonuniformEXT(image_index)],
        world_samplers[nonuniformEXT(sampler_index)]), uv);
}

vec4 SampleWorldArrayGrad(uint image_index, uint sampler_index, vec3 uv,
    vec2 gradient_x, vec2 gradient_y) {
    return textureGrad(sampler2DArray(
        float_array_images[nonuniformEXT(image_index)],
        world_samplers[nonuniformEXT(sampler_index)]), uv,
        gradient_x, gradient_y);
}

ivec3 WorldArrayTextureSize(uint image_index, uint sampler_index, int lod) {
    return textureSize(sampler2DArray(
        float_array_images[nonuniformEXT(image_index)],
        world_samplers[nonuniformEXT(sampler_index)]), lod);
}

uint PayloadUint(uint word) { return payload_words[world_push.payload_word_base + word]; }
float PayloadFloat(uint word) { return uintBitsToFloat(PayloadUint(word)); }

vec4 LoadPayloadVec4(uint word) {
    return vec4(PayloadFloat(word), PayloadFloat(word + 1u),
        PayloadFloat(word + 2u), PayloadFloat(word + 3u));
}

vec4 MapT0Position(vec3 position, ShaderState state, uint draw_flags,
    uint depth_interpretation) {
    vec2 origin = (draw_flags & DRAW_TARGET_ABSOLUTE) != 0u ? frame_view.viewport_xywh.xy : vec2(0.0);
    vec2 extent = (draw_flags & DRAW_TARGET_ABSOLUTE) != 0u ?
        vec2(frame_view.history_target_flags.zw) : frame_view.viewport_xywh.zw;
    vec2 ndc = 2.0 * (position.xy + origin) / max(extent, vec2(1.0)) - vec2(1.0);
    float depth = 0.0;
    if (depth_interpretation == 0u) {
        float biased_eye_z = position.z + state.z_bias;
        depth = clamp(1.0 - 1.0 / max(biased_eye_z, 0.0001), 0.0, 1.0);
    } else if (depth_interpretation == 1u) {
        depth = position.z;
    }
    return vec4(ndc, depth, 1.0);
}

#endif
