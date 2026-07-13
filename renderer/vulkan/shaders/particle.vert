#version 460
#extension GL_GOOGLE_include_directive : require
#include "shared/world_abi.glsl"
layout(location=0) in vec3 in_position; layout(location=1) in uint in_rgba8; layout(location=2) in vec2 in_uv0; layout(location=3) in vec2 in_uv1;
layout(location=0) out vec4 out_color; layout(location=1) out vec2 out_uv; layout(location=2) flat out uint out_draw_index;
layout(constant_id=0) const uint DEPTH_INTERPRETATION = 0u;
void main(){ uint i=world_push.draw_header_base+gl_DrawID;DrawHeader h=draw_headers[i];ShaderState s=shader_states[h.state_index];uint payload_vertex_index=uint(gl_VertexIndex-int(s.vertex_index_base));gl_Position=MapT0Position(in_position,s,h.flags,DEPTH_INTERPRETATION);out_color=unpackUnorm4x8(in_rgba8);if((h.flags&DRAW_HAS_PERSPECTIVE_PAYLOAD)!=0u){vec4 p=LoadPayloadVec4(h.vertex_payload_offset+payload_vertex_index*4u);out_uv=p.xy/max(abs(p.w),0.000001);}else out_uv=in_uv0;out_draw_index=i; }
