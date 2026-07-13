#version 460
#extension GL_GOOGLE_include_directive : require
#include "shared/world_abi.glsl"
layout(location=0) in vec4 in_color;
layout(location=1) in vec2 in_uv;
layout(location=2) flat in uint in_draw_index;
layout(location=0) out vec4 out_color;
layout(location=1) out vec2 out_velocity;
layout(location=2) out vec2 out_protection;
layout(location=3) out float out_ao_class;
layout(location=4) out uint out_object_id;
void main() {
    DrawHeader h = draw_headers[in_draw_index]; ShaderState s = shader_states[h.state_index]; Material m = materials[h.material_index];
    vec4 c = in_color; if ((s.shader_flags & SHADER_TEXTURED) != 0u) c *= SampleWorld2D(m.image2d.x, m.sampler_index.x, in_uv);
    out_color=c; out_velocity=vec2(0); float a=clamp(c.a,0.0,1.0); if((s.shader_flags&SHADER_LUMINANCE_POST_MASK)!=0u)a*=clamp(max(max(c.r,c.g),c.b),0.0,1.0); float p=clamp(1.0-pow(1.0-a,3.0),0.0,1.0); out_protection=vec2(s.post_values.x*p,s.post_values.y*p); out_ao_class=float(min(s.ao_class,255u))/255.0; out_object_id=0u;
}
