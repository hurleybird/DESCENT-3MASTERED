#version 460
#extension GL_GOOGLE_include_directive : require
#include "shared/world_abi.glsl"

layout(location=0) in vec3 in_position;
layout(location=1) in uint in_rgba8;
layout(location=2) in vec2 in_uv0;
layout(location=3) in vec2 in_uv1;
layout(location=0) out vec4 out_color;
layout(location=1) out vec3 out_uv0_q;
layout(location=2) out vec3 out_uv1_q;
layout(location=3) flat out uint out_draw_index;
layout(location=4) noperspective out vec4 out_primary_q;
layout(location=5) out vec2 out_velocity;
layout(location=6) noperspective out float out_mapped_depth;
layout(location=7) out vec4 out_primary_smooth;
layout(location=8) out vec4 out_field_centers[4];
layout(location=12) noperspective out vec4 out_field_colors[4];
layout(location=16) flat out uint out_terrain_pages;

layout(constant_id=0) const uint DEPTH_INTERPRETATION = 0u;

void main() {
    uint draw_index = world_push.draw_header_base;
    DrawHeader header = draw_headers[draw_index];
    uint geometry_mode = header.flags & DRAW_GEOMETRY_MODE_MASK;
    // One logical T2 draw owns all compute-emitted indirect commands.  Unlike
    // ordinary indirect runs, those commands share the base header.
    if (geometry_mode != DRAW_GEOMETRY_T2) {
        draw_index += gl_DrawID;
        header = draw_headers[draw_index];
        geometry_mode = header.flags & DRAW_GEOMETRY_MODE_MASK;
    }
    ShaderState state = shader_states[header.state_index];
    uint payload_vertex_index = geometry_mode == DRAW_GEOMETRY_T2 ?
        uint(gl_VertexIndex) : uint(gl_VertexIndex - int(state.vertex_index_base));
    Transform transform = transforms[header.transform_index];
    vec4 local = vec4(in_position, 1.0);
    vec4 world = transform.current_model * local;
    vec4 clip;
    if (geometry_mode == DRAW_GEOMETRY_T0)
        clip = MapT0Position(in_position, state, header.flags, DEPTH_INTERPRETATION);
    else if (geometry_mode == DRAW_GEOMETRY_T2)
        clip = vec4(in_position, 1.0);
    else {
        vec4 eye = frame_view.view * world;
        float zv = -eye.z;
        vec4 projected = frame_view.projection * eye;
        float depth = clamp(1.0 - 1.0 / max(zv, 0.0001), 0.0, 1.0);
        clip = vec4(projected.x, -projected.y, depth * zv, zv);
    }
    gl_Position = clip;
    out_color = unpackUnorm4x8(in_rgba8);
    out_draw_index = draw_index;
    out_terrain_pages = 0u;
    if (geometry_mode == DRAW_GEOMETRY_T2) {
        uint payload = header.vertex_payload_offset + payload_vertex_index * 8u;
        vec4 world_q = LoadPayloadVec4(payload);
        float q = world_q.w;
        out_uv0_q = vec3(in_uv0, q);
        out_uv1_q = vec3(in_uv1, q);
        out_primary_q = world_q;
        out_primary_smooth = world_q;
        out_velocity = vec2(0.0);
        out_mapped_depth = in_position.z;
        out_terrain_pages = PayloadUint(payload + 4u);
        for (uint field = 0u; field < 4u; ++field) {
            out_field_centers[field] = vec4(0.0);
            out_field_colors[field] = vec4(0.0);
        }
        return;
    }
    if ((header.flags & DRAW_HAS_PERSPECTIVE_PAYLOAD) != 0u) {
        uint payload = header.vertex_payload_offset + payload_vertex_index * 4u;
        vec4 packed = LoadPayloadVec4(payload);
        out_uv0_q = vec3(packed.xy, packed.w);
        out_uv1_q = vec3(packed.z, in_uv1.y * packed.w, packed.w);
    } else {
        out_uv0_q = vec3(in_uv0, 1.0);
        out_uv1_q = vec3(in_uv1, 1.0);
    }
    if ((header.flags & DRAW_HAS_SPECULAR_PAYLOAD) != 0u) {
        uint payload = header.specular_payload_offset + payload_vertex_index * 36u;
        out_primary_q = LoadPayloadVec4(payload);
        for (uint field=0u;field<4u;++field) {
            out_field_centers[field]=LoadPayloadVec4(payload+4u+field*4u);
            out_field_colors[field]=LoadPayloadVec4(payload+20u+field*4u);
        }
    } else {
        out_primary_q = vec4(world.xyz, 1.0);
        for(uint field=0u;field<4u;++field){out_field_centers[field]=vec4(0.0);out_field_colors[field]=vec4(0.0);}
    }
    out_primary_smooth = ((state.state_flags2 >> 4u) & 3u) == 2u ?
        out_primary_q : vec4(world.xyz, 1.0);
    out_velocity = vec2(0.0);
    if ((header.flags & DRAW_HAS_MOTION_PAYLOAD) != 0u) {
        uint payload = header.motion_payload_offset + payload_vertex_index * 8u;
        vec4 current_world = LoadPayloadVec4(payload);
        vec4 previous_world = LoadPayloadVec4(payload + 4u);
        if (abs(current_world.w) > 0.000001 &&
            (((state.state_flags2 >> 4u) & 3u) == 1u ||
             (state.shader_flags & SHADER_GENERIC_FOG) != 0u))
            out_primary_smooth = current_world;
        if (current_world.w > 0.0 && previous_world.w > 0.0) {
            vec4 current_clip = frame_view.view_projection * vec4(current_world.xyz, 1.0);
            vec4 previous_clip = frame_view.previous_view_projection * vec4(previous_world.xyz, 1.0);
            out_velocity = (current_clip.xy / max(abs(current_clip.w), 0.00001) -
                previous_clip.xy / max(abs(previous_clip.w), 0.00001)) * 0.5;
        }
    }
    out_mapped_depth = clip.z / max(abs(clip.w), 0.000001);
}
