#version 460
#extension GL_GOOGLE_include_directive : require
#include "shared/world_abi.glsl"

layout(location=0) noperspective in vec4 in_color;
layout(location=1) in vec3 in_uv0_q;
layout(location=2) in vec3 in_uv1_q;
layout(location=3) flat in uint in_draw_index;
layout(location=4) noperspective in vec4 in_primary_q;
layout(location=5) in vec2 in_velocity;
layout(location=6) noperspective in float in_mapped_depth;
layout(location=7) in vec4 in_primary_smooth;
layout(location=8) in vec4 in_field_centers[4];
layout(location=12) noperspective in vec4 in_field_colors[4];
layout(location=16) flat in uint in_terrain_pages;
layout(location=0) out vec4 out_color;
layout(location=1) out vec2 out_velocity;
layout(location=2) out vec2 out_protection;
layout(location=3) out float out_ao_class;
layout(location=4) out uint out_object_id;

float DirectionalAttenuation(DynamicLight light, vec3 light_vector) {
    if (light.specular_and_flags.y < 0.5) return 1.0;
    float direction_dot = dot(light_vector, normalize(light.direction_dot_range.xyz));
    if (direction_dot < light.direction_dot_range.w) return 0.0;
    return (direction_dot - light.direction_dot_range.w) /
        max(1.0 - light.direction_dot_range.w, 0.0001);
}

vec3 ApplyDynamicLighting(ShaderState state, vec3 position, vec3 base_light) {
    vec3 sum = vec3(0.0);
    for (uint i=0u; i<state.dynamic_light_count; ++i) {
        DynamicLight light = dynamic_lights[state.dynamic_light_first+i];
        vec3 delta = position-light.position_radius.xyz;
        float distance = length(delta);
        float radius = max(light.position_radius.w, 0.0001);
        float scalar = 1.0-distance/radius;
        if (scalar <= 0.0) continue;
        vec3 direction = distance > 0.0001 ? delta/distance : normalize(state.light_direction.xyz);
        scalar = pow(scalar, max(light.color_falloff.w,0.0001)) * DirectionalAttenuation(light,direction);
        sum += light.color_falloff.rgb*scalar;
    }
    return clamp(base_light+sum,vec3(0.0),vec3(1.0));
}

vec3 ApplyTerrainDynamicLighting(ShaderState state, WorldAux aux,
    uint lightmap_page, vec3 position, vec3 base_light) {
    vec3 dynamic_color = vec3(0.0);
    uint page = min(lightmap_page, 3u);
    uint page_range = aux.indices[page];
    uint count = min(page_range & 255u, 8u);
    uint page_offset = page_range >> 8u;
    for (uint i = 0u; i < count; ++i) {
        uint local_index = page_offset + i;
        if (local_index >= state.dynamic_light_count) break;
        DynamicLight light = dynamic_lights[state.dynamic_light_first + local_index];
        vec3 delta = position - light.position_radius.xyz;
        float radius = max(light.position_radius.w, 0.0001);
        float distance = length(delta);
        vec3 light_vector = distance > 0.0001 ?
            delta / distance : vec3(0.0, 1.0, 0.0);
        float scalar = 1.0 - distance / radius;
        if (scalar <= 0.0) continue;
        scalar *= DirectionalAttenuation(light, light_vector);
        dynamic_color += light.color_falloff.rgb * scalar;
    }
    return clamp(base_light + dynamic_color, vec3(0.0), vec3(1.0));
}

vec4 SampleTerrainBaseTexture(Material material, vec2 uv, uint layer) {
    vec2 tile_origin = floor(uv);
    vec2 tile_uv = uv - tile_origin;
    bvec2 upper_edge = bvec2(
        tile_uv.x <= 0.000001 && uv.x > 0.0,
        tile_uv.y <= 0.000001 && uv.y > 0.0);
    tile_uv = mix(tile_uv, vec2(1.0), upper_edge);
    ivec3 image_size = WorldArrayTextureSize(material.image2d_array.x,
        material.sampler_index.x, 0);
    vec2 texel_inset = 0.5 / vec2(max(image_size.xy, ivec2(1)));
    tile_uv = clamp(tile_uv, texel_inset, vec2(1.0) - texel_inset);
    return SampleWorldArrayGrad(material.image2d_array.x,
        material.sampler_index.x,
        vec3(tile_origin + tile_uv, float(layer)), dFdx(uv), dFdy(uv));
}

vec2 TerrainMotionVector(vec3 world_position) {
    if ((frame_view.history_target_flags.x &
        FRAME_HAS_PREVIOUS_VIEW_PROJECTION) == 0u)
        return vec2(0.0);
    vec4 current_clip = frame_view.view_projection * vec4(world_position, 1.0);
    vec4 previous_clip = frame_view.previous_view_projection *
        vec4(world_position, 1.0);
    if (current_clip.w <= 0.00001 || previous_clip.w <= 0.00001)
        return vec2(0.0);
    return (current_clip.xy / current_clip.w -
        previous_clip.xy / previous_clip.w) * 0.5;
}

float TerrainFogAmount(WorldAux aux, float mapped_depth) {
    float fog_start = clamp(1.0 - 1.0 / max(aux.params.x, 0.0001), 0.0, 1.0);
    float fog_end = clamp(1.0 - 1.0 / max(aux.params.y, 0.0001), 0.0, 1.0);
    return clamp((mapped_depth - fog_start) /
        max(fog_end - fog_start, 0.0001), 0.0, 1.0);
}

vec3 SpecularFromIncident(vec3 view_position, vec3 normal, vec3 incident,
    vec3 light_color, float scalar, int exponent) {
    float distance=length(incident); if(distance<=0.0001||scalar<=0.0)return vec3(0.0);
    vec3 reflected=reflect(incident/distance,normal);
    float dotp=max(dot(normalize(-view_position),reflected),0.0);
    return dotp<=0.0?vec3(0.0):pow(dotp,float(exponent))*light_color*scalar;
}

vec3 ApplySpecular(ShaderState state, vec3 position, vec3 normal,
    vec3 lightmap_color, float mask_alpha) {
    SpecularBlock block=specular_blocks[state.specular_block_index];
    const float weights[4]=float[4](1.0,0.66,0.33,0.25);
    vec3 sum=vec3(0.0);
    for(int i=0;i<4&&i<block.count;++i){
        vec3 center=block.field_mode>0.5?in_field_centers[i].xyz/max(abs(in_field_centers[i].w),0.0001):block.sources[i].center.xyz;
        vec3 source_color=block.field_mode>0.5?in_field_colors[i].rgb:block.sources[i].color.rgb;
        float source_weight=block.field_mode>0.5?1.0:weights[i];
        sum+=SpecularFromIncident(position,normal,position-center,source_color,source_weight,block.exponent);
    }
    for(uint i=0u;i<state.dynamic_light_count;++i){
        DynamicLight light=dynamic_lights[state.dynamic_light_first+i];
        vec3 incident=position-light.specular_position_radius.xyz;
        float distance=length(incident);float radius=max(light.specular_position_radius.w,0.0001);
        float scalar=1.0-distance/radius;if(scalar<=0.0||distance<=0.0001)continue;
        scalar=pow(scalar,max(light.color_falloff.w,0.0001))*DirectionalAttenuation(light,incident/distance);
        sum+=SpecularFromIncident(position,normal,incident,light.color_falloff.rgb,
            scalar*light.specular_and_flags.x,block.exponent);
    }
    vec3 lm=mix(vec3(1.0),clamp(lightmap_color,vec3(0),vec3(1)),clamp(block.lightmap_mix,0.0,1.0));
    return clamp(sum*lm*block.strength*mask_alpha,vec3(0),vec3(1));
}

float RoomFogAmount(WorldAux aux, vec3 view_position) {
    float magnitude;
    if(aux.params.w>0.5){
        vec4 plane=transpose(frame_view.inverse_modelview)*aux.fog_plane;
        float normal_length=max(length(plane.xyz),0.0001);
        plane=vec4(plane.xyz/normal_length,plane.w/normal_length);
        float distance=dot(view_position,plane.xyz)+plane.w;
        float denominator=plane.w-distance;
        if(abs(denominator)<0.0001)denominator=denominator<0.0?-0.0001:0.0001;
        float t=plane.w/denominator;
        vec3 portal_point=view_position*t;
        magnitude=step(0.0,distance)*max(0.0,-(view_position.z-portal_point.z));
    }else magnitude=-view_position.z;
    return clamp(magnitude/max(aux.params.x,0.0001),0.0,1.0);
}

void main() {
    DrawHeader header=draw_headers[in_draw_index];
    ShaderState state=shader_states[header.state_index];
    Material material=materials[header.material_index];
    WorldAux aux=world_aux_records[header.room_or_terrain_index];
    bool is_terrain=(header.flags&DRAW_GEOMETRY_MODE_MASK)==DRAW_GEOMETRY_T2&&
        (state.shader_flags&SHADER_TERRAIN)!=0u;
    uint named_pipeline=uint(max(material.uv_params.z,0.0)+0.5);
    vec2 uv0=in_uv0_q.xy/max(abs(in_uv0_q.z),0.000001);
    vec2 uv1=in_uv1_q.xy/max(abs(in_uv1_q.z),0.000001);
    uint primary_kind=(state.state_flags2>>4u)&3u;
    vec3 normal_or_position=in_primary_q.xyz/max(abs(in_primary_q.w),0.0001);
    vec3 shading_position=in_primary_smooth.xyz/max(abs(in_primary_smooth.w),0.0001);
    vec4 vertex_color=in_color;
    if((state.shader_flags&SHADER_PHONG)!=0u&&primary_kind==1u){
        vec3 normal=normalize(normal_or_position);
        float light=clamp((-dot(normalize(state.light_direction.xyz),normal)+1.0)*0.5,0.0,1.0);
        vertex_color.rgb*=light;
    }
    if((state.shader_flags&SHADER_AO_CAPTURE_WEIGHT)!=0u){
        float weight=is_terrain?clamp(state.post_values.z,0.0,1.0):
            clamp(state.post_values.z*(1.0-state.post_values.x),0.0,1.0);
        if(!is_terrain&&weight<=0.0)discard;
        vec2 capture_velocity=vec2(0.0);
        if(is_terrain&&(state.shader_flags&SHADER_MOTION_WRITE)!=0u)
            capture_velocity=TerrainMotionVector(in_primary_q.xyz/
                max(in_primary_q.w,0.000001));
        out_color=vec4(weight,weight,weight,1.0);out_velocity=capture_velocity;out_protection=vec2(0);out_ao_class=weight;out_object_id=0u;return;
    }
    vec4 base=vec4(1.0);
    vec3 lightmap=vec3(1.0);
    float lightmap_alpha=1.0;
    if(is_terrain){
        vec2 terrain_uv0=in_uv0_q.xy/max(in_uv0_q.z,0.000001);
        vec2 terrain_uv1=in_uv1_q.xy/max(in_uv1_q.z,0.000001);
        uint lightmap_page=min(in_terrain_pages&255u,3u);
        uint texture_layer=in_terrain_pages>>8u;
        vec3 terrain_world=in_primary_q.xyz/max(in_primary_q.w,0.000001);
        base=SampleTerrainBaseTexture(material,terrain_uv0,texture_layer);
        vec4 terrain_lightmap=SampleWorldArray(material.image2d_array.y,
            material.sampler_index.y,vec3(terrain_uv1,float(lightmap_page)));
        lightmap=terrain_lightmap.rgb;
        lightmap_alpha=terrain_lightmap.a;
        if((state.shader_flags&SHADER_DYNAMIC_LIGHTS)!=0u)
            lightmap=ApplyTerrainDynamicLighting(state,aux,lightmap_page,
                terrain_world,lightmap);
    }else{
        if((state.shader_flags&SHADER_TEXTURED)!=0u)
            base=SampleWorld2D(material.image2d.x,material.sampler_index.x,uv0);
        if((state.shader_flags&SHADER_LIGHTMAPPED)!=0u)
            lightmap=SampleWorld2D(material.image2d.y,material.sampler_index.y,uv1).rgb;
        if((state.shader_flags&SHADER_DYNAMIC_LIGHTS)!=0u&&primary_kind==2u)
            lightmap=ApplyDynamicLighting(state,shading_position,lightmap);
    }
    vec4 color=base*vec4(lightmap,lightmap_alpha)*vertex_color;
    if((state.shader_flags&SHADER_SPECIAL_TEXTURE_FLAT)!=0u)color.rgb=state.flat_color.rgb;
    if((state.shader_flags&SHADER_PER_PIXEL_SPECULAR)!=0u&&primary_kind!=0u){
        vec3 normal=normalize(primary_kind==1u?normal_or_position:state.light_direction.xyz);
        float mask=base.a;
        if((state.shader_flags&SHADER_SPECULAR_MASK)!=0u)mask=SampleWorld2D(material.image2d.z,material.sampler_index.z,uv0).a;
        SpecularBlock block=specular_blocks[state.specular_block_index];
        vec3 spec=ApplySpecular(state,shading_position,normal,lightmap,clamp(mask*block.alpha_strength,0.0,1.0));
        if((state.shader_flags&SHADER_SPECULAR_MASK)!=0u)color.rgb=clamp(color.rgb+spec,vec3(0),vec3(1));
        else color=vec4(spec,clamp(mask*block.alpha_strength*vertex_color.a,0.0,1.0));
    }
    if((state.shader_flags&SHADER_SOFT_PARTICLE)!=0u&&color.a>0.0){
        vec2 screen_uv=gl_FragCoord.xy*frame_view.target_extent_inv_extent.zw;
        float scene_depth=SampleWorld2D(material.image2d.w,material.sampler_index.w,screen_uv).r;
        if(scene_depth<0.9999){float scene_eye=1.0/max(1.0-clamp(scene_depth,0.0,0.9999),0.0001);float particle_eye=1.0/max(1.0-clamp(in_mapped_depth,0.0,0.9999),0.0001);color.a*=clamp((scene_eye-particle_eye)/max(material.uv_params.w,0.0001),0.0,1.0);}
    }
    if((state.shader_flags&SHADER_COCKPIT)!=0u&&aux.params.x>0.5){
        float darkness=clamp(aux.params.z,0.0,1.0);
        if(aux.params.w>0.5&&aux.fog_color.x>0.0){
            float spacing=max(aux.fog_color.y,1.0);
            float row=mod(floor(gl_FragCoord.y+aux.fog_color.w),spacing);
            float stripe=step(row/spacing,clamp(aux.fog_color.z,0.0,1.0));
            darkness=clamp(darkness+stripe*aux.fog_color.x,0.0,1.0);
        }
        color=vec4(vec3(0.0),clamp(aux.params.y,0.0,1.0)*darkness);
    }
    float suppression_alpha=clamp(color.a,0.0,1.0);
    if((state.shader_flags&SHADER_LUMINANCE_POST_MASK)!=0u)suppression_alpha*=clamp(max(max(color.r,color.g),color.b),0.0,1.0);
    float cubic=1.0-pow(1.0-suppression_alpha,3.0);
    float ao_mask=clamp(state.post_values.x*cubic,0.0,1.0);
    float bloom_mask=clamp(state.post_values.y*cubic,0.0,1.0);
    if(is_terrain){ao_mask=0.0;bloom_mask=0.0;}
    bool named_room_fog=named_pipeline==2u||named_pipeline==4u||
        named_pipeline==6u||named_pipeline==7u;
    bool named_room_plain=named_pipeline==3u||named_pipeline==5u;
    if(named_room_plain)
        color.rgb*=aux.params.z;
    if(is_terrain&&(state.shader_flags&SHADER_GENERIC_FOG)!=0u){
        float mag=TerrainFogAmount(aux,in_mapped_depth);
        color.rgb=mix(color.rgb,aux.fog_color.rgb,mag);
        if((state.shader_flags&SHADER_TERRAIN_FOG_BLOOM_SUPPRESSION)!=0u)
            bloom_mask=max(bloom_mask,mag);
    }else if(named_room_fog||(state.shader_flags&SHADER_GENERIC_FOG)!=0u){
        float mag;
        if((named_room_fog||aux.params.x>0.0)&&
            (state.shader_flags&SHADER_COCKPIT)==0u){
            mag=RoomFogAmount(aux,shading_position);
            if(named_pipeline==7u)
                color=vec4(aux.fog_color.rgb,mag);
            else if(named_pipeline==4u||
                (state.shader_flags&SHADER_PER_PIXEL_SPECULAR)!=0u)
                color.rgb*=vec3((1.0-mag)*aux.params.z);
            else color.rgb=mix(color.rgb,aux.fog_color.rgb,mag)*aux.params.z;
        }else{
            mag=clamp((in_mapped_depth-state.fog_near_mapped)/max(state.fog_far_mapped-state.fog_near_mapped,0.0001),0.0,1.0);
            color.rgb=mix(color.rgb,state.fog_color.rgb,mag);
        }
        bloom_mask=max(bloom_mask,mag);
    }
    vec2 velocity=in_velocity;
    if(is_terrain&&(state.shader_flags&SHADER_MOTION_WRITE)!=0u)
        velocity=TerrainMotionVector(in_primary_q.xyz/max(in_primary_q.w,0.000001));
    out_color=(state.shader_flags&SHADER_POST_MASK_ONLY)!=0u?vec4(0.0):color;
    out_velocity=(state.shader_flags&SHADER_MOTION_WRITE)==0u?vec2(0):
        (is_terrain?velocity:(color.a>0.001?velocity:vec2(0)));
    out_protection=vec2(ao_mask,bloom_mask);
    out_ao_class=is_terrain?1.0/255.0:float(min(state.ao_class,255u))/255.0;
    out_object_id=!is_terrain&&
        (state.shader_flags&SHADER_OBJECT_ID_WRITE)!=0u&&color.a>0.001?
        state.motion_object_id:0u;
}
