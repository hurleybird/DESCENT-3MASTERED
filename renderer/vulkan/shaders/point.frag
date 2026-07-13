#version 460
#extension GL_GOOGLE_include_directive : require
#include "shared/world_abi.glsl"
layout(location=0) in vec4 in_color; layout(location=1) flat in uint in_draw_index;
layout(location=0) out vec4 out_color; layout(location=1) out vec2 out_velocity; layout(location=2) out vec2 out_protection; layout(location=3) out float out_ao_class; layout(location=4) out uint out_object_id;
void main(){ShaderState s=shader_states[draw_headers[in_draw_index].state_index];out_color=in_color;out_velocity=vec2(0);out_protection=vec2(0);out_ao_class=float(s.ao_class)/255.0;out_object_id=0u;}
