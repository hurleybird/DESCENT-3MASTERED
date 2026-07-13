#version 460
#extension GL_GOOGLE_include_directive : require
#include "shared/world_abi.glsl"

layout(location=0) noperspective out vec4 out_color;
layout(location=1) flat out uint out_draw_index;
layout(constant_id=0) const uint DEPTH_INTERPRETATION = 1u;

vec3 LoadPosition(uint base_word) {
    return vec3(PayloadFloat(base_word),PayloadFloat(base_word+1u),
        PayloadFloat(base_word+2u));
}

void main() {
    uint draw_index=world_push.draw_header_base+gl_DrawID;
    DrawHeader header=draw_headers[draw_index];
    ShaderState state=shader_states[header.state_index];
    uint record=header.vertex_payload_offset+uint(gl_InstanceIndex)*20u;
    vec3 p0=LoadPosition(record);
    vec3 p1=LoadPosition(record+8u);
    float half_width=max(PayloadFloat(record+16u),0.5);
    vec2 delta=p1.xy-p0.xy;
    float length_delta=length(delta);
    vec2 tangent=length_delta>0.00001?delta/length_delta:vec2(1.0,0.0);
    vec2 normal=vec2(-tangent.y,tangent.x)*half_width;
    uint corner=uint(gl_VertexIndex)%6u;
    bool endpoint_one=corner==1u||corner==2u||corner==4u;
    bool positive_side=corner==2u||corner==4u||corner==5u;
    vec3 position=endpoint_one?p1:p0;
    position.xy+=positive_side?normal:-normal;
    gl_Position=MapT0Position(position,state,header.flags,DEPTH_INTERPRETATION);
    uint rgba=PayloadUint(record+(endpoint_one?11u:3u));
    out_color=unpackUnorm4x8(rgba);
    out_draw_index=draw_index;
}
