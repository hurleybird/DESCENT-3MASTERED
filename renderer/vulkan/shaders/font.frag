#version 460
#extension GL_GOOGLE_include_directive : require
#include "shared/world_abi.glsl"
layout(location=0) in vec4 in_color; layout(location=1) in vec2 in_uv; layout(location=2) flat in uint in_layer; layout(location=3) flat in uint in_draw_index;
layout(location=0) out vec4 out_color; layout(location=1) out vec2 out_velocity; layout(location=2) out vec2 out_protection; layout(location=3) out float out_ao_class; layout(location=4) out uint out_object_id;
void main(){ DrawHeader h=draw_headers[in_draw_index]; ShaderState s=shader_states[h.state_index]; Material m=materials[h.material_index]; vec4 c=SampleWorldArray(m.image2d_array.x,m.sampler_index.x,vec3(in_uv,float(in_layer)))*in_color; c.a*=s.alpha_factor; float p=clamp(1.0-pow(1.0-clamp(c.a,0.0,1.0),3.0),0.0,1.0); out_color=c;out_velocity=vec2(0);out_protection=vec2(p);out_ao_class=1.0;out_object_id=0u; }
