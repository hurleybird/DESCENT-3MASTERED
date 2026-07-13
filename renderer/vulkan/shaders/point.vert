#version 460
#extension GL_GOOGLE_include_directive : require
#include "shared/world_abi.glsl"

layout(location=0) noperspective out vec4 out_color;
layout(location=1) flat out uint out_draw_index;
layout(constant_id=0) const uint DEPTH_INTERPRETATION = 1u;

void main() {
    uint draw_index=world_push.draw_header_base+gl_DrawID;
    DrawHeader header=draw_headers[draw_index];
    ShaderState state=shader_states[header.state_index];
    uint record=header.vertex_payload_offset+uint(gl_InstanceIndex)*12u;
    vec3 center=vec3(PayloadFloat(record),PayloadFloat(record+1u),
        PayloadFloat(record+2u));
    float half_size=max(PayloadFloat(record+8u),0.5);
    const vec2 corners[6]=vec2[6](vec2(-1,-1),vec2(1,-1),vec2(1,1),
        vec2(-1,-1),vec2(1,1),vec2(-1,1));
    center.xy+=corners[uint(gl_VertexIndex)%6u]*half_size;
    gl_Position=MapT0Position(center,state,header.flags,DEPTH_INTERPRETATION);
    out_color=unpackUnorm4x8(PayloadUint(record+3u));
    out_draw_index=draw_index;
}
