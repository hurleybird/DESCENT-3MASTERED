#ifndef PICCU_TERRAIN_EMITTER_GLSL
#define PICCU_TERRAIN_EMITTER_GLSL

const float TERRAIN_MIN_Z=0.000001;
const int TERRAIN_CLIP_MAX_VERTICES=8;
const int TERRAIN_CLIP_LEFT=1;
const int TERRAIN_CLIP_RIGHT=2;
const int TERRAIN_CLIP_BOTTOM=4;
const int TERRAIN_CLIP_TOP=8;
const int TERRAIN_CLIP_BEHIND=128;

struct TerrainCell { uvec4 packed; vec4 height; };
struct TerrainWork { uint cell_index; int source_texture; uint source_flags; uint full_draw_order; };
struct TerrainBatch {
    int source_texture; uint texture_layer; uint first_work_item; uint work_item_count;
    uint first_output_vertex; uint output_vertex_capacity;
    uint indirect_command_index; uint reserved0;
};
struct TerrainView {
    vec4 terrain_row0; vec4 terrain_x_step; vec4 terrain_z_step;
    vec4 terrain_y_step; vec4 projection_center_half_size;
    vec4 viewport_size_inv_size; vec4 clip_scale;
};
struct BaseVertex { vec3 position; uint rgba8; vec2 uv0; vec2 uv1; };
struct TerrainPayload {
    vec4 world_q; uint packed_pages; uint reserved0; uint reserved1; uint reserved2;
};
struct ClipVertex { vec3 world; vec3 rotated; vec2 base_uv; vec2 lightmap_uv; };

layout(std430,set=0,binding=0) readonly buffer TerrainCells { TerrainCell terrain_cells[]; };
layout(std430,set=0,binding=1) readonly buffer TerrainWorkItems { TerrainWork terrain_work[]; };
layout(std430,set=0,binding=2) readonly buffer TerrainBatches { TerrainBatch terrain_batches[]; };
layout(std430,set=0,binding=3) readonly buffer TerrainViews { TerrainView terrain_views[]; };
// One uvec2 per work item: x is the classify count and y is the stable scan
// offset.  Keeping scratch in one descriptor preserves the frozen eight-SSBO
// device profile without requiring scalar block layout.
layout(std430,set=0,binding=4) buffer TerrainScratch { uvec2 terrain_scratch[]; };
layout(std430,set=0,binding=5) buffer TerrainBaseOutput { BaseVertex terrain_base_output[]; };
layout(std430,set=0,binding=6) buffer TerrainPayloadOutput { TerrainPayload terrain_payload_output[]; };
layout(std430,set=0,binding=7) buffer TerrainIndirectOutput { uvec4 terrain_indirect_output[]; };

layout(push_constant) uniform TerrainPush {
    uint work_item_count; uint batch_count; uint output_vertex_capacity; uint flags;
} terrain_push;

vec3 RotatePoint(TerrainView view,vec3 world){
    return view.terrain_row0.xyz+view.terrain_x_step.xyz*world.x+
        view.terrain_y_step.xyz*world.y+view.terrain_z_step.xyz*world.z;
}

vec3 CellPosition(uint segment,vec4 height,uint corner){
    uint cx=segment&255u;uint cz=segment>>8u;uint x=cx;uint z=cz;float y=height.w;
    if(corner==0u){z=cz+1u;y=height.x;}
    else if(corner==1u){x=cx+1u;z=cz+1u;y=height.y;}
    else if(corner==2u){x=cx+1u;y=height.z;}
    return vec3(float(x)*16.0,y,float(z)*16.0);
}

vec2 BaseUv(uint segment,uint rotation,uint corner){
    uint cx=segment&255u;uint cz=segment>>8u;
    float subx=float(cx&7u);float subz=float(7u-(cz&7u));
    if(corner==1u||corner==2u)subx+=1.0;
    if(corner==2u||corner==3u)subz+=1.0;
    float x=subx*0.125;float y=subz*0.125;float tile=float(rotation>>4u);
    uint rotator=rotation&15u;vec2 uv;
    if(rotator==1u)uv=vec2(1.0-y,x);
    else if(rotator==2u)uv=vec2(1.0-x,1.0-y);
    else if(rotator==3u)uv=vec2(y,1.0-x);
    else uv=vec2(x,y);
    return uv*tile;
}

vec2 LightmapUv(uint segment,uint corner){
    uint cx=segment&255u;uint cz=segment>>8u;
    vec2 uv=vec2(float(cx&127u)*0.0078125,
        float(128u-((cz&127u)+1u))*0.0078125);
    if(corner==1u||corner==2u)uv.x+=0.0078125;
    if(corner==2u||corner==3u)uv.y+=0.0078125;
    return uv;
}

int ClipCode(TerrainView view,ClipVertex vertex){
    int code=0;
    if(vertex.rotated.x < -vertex.rotated.z*view.clip_scale.x)code|=TERRAIN_CLIP_LEFT;
    if(vertex.rotated.x >  vertex.rotated.z*view.clip_scale.y)code|=TERRAIN_CLIP_RIGHT;
    if(vertex.rotated.y < -vertex.rotated.z*view.clip_scale.z)code|=TERRAIN_CLIP_BOTTOM;
    if(vertex.rotated.y >  vertex.rotated.z*view.clip_scale.w)code|=TERRAIN_CLIP_TOP;
    if(vertex.rotated.z<0.0)code|=TERRAIN_CLIP_BEHIND;
    return code;
}

void ClipCodes(TerrainView view,ClipVertex poly[TERRAIN_CLIP_MAX_VERTICES],
    int count,out int code_or,out int code_and){
    code_or=0;code_and=255;
    for(int i=0;i<count;++i){int code=ClipCode(view,poly[i]);code_or|=code;code_and&=code;}
}

float PlaneDistance(TerrainView view,ClipVertex vertex,int plane){
    if(plane==TERRAIN_CLIP_LEFT)return vertex.rotated.x+vertex.rotated.z*view.clip_scale.x;
    if(plane==TERRAIN_CLIP_RIGHT)return vertex.rotated.z*view.clip_scale.y-vertex.rotated.x;
    if(plane==TERRAIN_CLIP_BOTTOM)return vertex.rotated.y+vertex.rotated.z*view.clip_scale.z;
    return vertex.rotated.z*view.clip_scale.w-vertex.rotated.y;
}

ClipVertex MixVertex(ClipVertex a,ClipVertex b,float t){
    ClipVertex result;result.world=mix(a.world,b.world,t);
    result.rotated=mix(a.rotated,b.rotated,t);result.base_uv=mix(a.base_uv,b.base_uv,t);
    result.lightmap_uv=mix(a.lightmap_uv,b.lightmap_uv,t);return result;
}

ClipVertex IntersectPlane(TerrainView view,ClipVertex inside_vertex,
    ClipVertex outside_vertex,int plane){
    float inside_distance=PlaneDistance(view,inside_vertex,plane);
    float outside_distance=PlaneDistance(view,outside_vertex,plane);
    float denominator=inside_distance-outside_distance;
    float t=abs(denominator)>0.000001?inside_distance/denominator:0.0;
    return MixVertex(inside_vertex,outside_vertex,clamp(t,0.0,1.0));
}

void ClipAppend(inout ClipVertex poly[TERRAIN_CLIP_MAX_VERTICES],
    inout int count,ClipVertex vertex){if(count<TERRAIN_CLIP_MAX_VERTICES)poly[count++]=vertex;}

void ClipToPlane(TerrainView view,
    inout ClipVertex poly[TERRAIN_CLIP_MAX_VERTICES],inout int count,int plane){
    ClipVertex output_poly[TERRAIN_CLIP_MAX_VERTICES];int output_count=0;
    ClipVertex previous=poly[count-1];bool previous_inside=PlaneDistance(view,previous,plane)>=0.0;
    for(int i=0;i<count;++i){ClipVertex current=poly[i];
        bool current_inside=PlaneDistance(view,current,plane)>=0.0;
        if(current_inside){if(!previous_inside)ClipAppend(output_poly,output_count,
            IntersectPlane(view,current,previous,plane));ClipAppend(output_poly,output_count,current);}
        else if(previous_inside)ClipAppend(output_poly,output_count,
            IntersectPlane(view,previous,current,plane));
        previous=current;previous_inside=current_inside;}
    count=output_count;for(int i=0;i<TERRAIN_CLIP_MAX_VERTICES;++i)if(i<count)poly[i]=output_poly[i];
}

bool ClipTriangle(TerrainView view,ClipVertex input_poly[3],
    out ClipVertex output_poly[TERRAIN_CLIP_MAX_VERTICES],out int output_count){
    output_count=3;for(int i=0;i<3;++i)output_poly[i]=input_poly[i];
    int code_or=0;int code_and=255;ClipCodes(view,output_poly,output_count,code_or,code_and);
    if(code_and!=0)return false;
    const int planes[4]=int[4](TERRAIN_CLIP_LEFT,TERRAIN_CLIP_RIGHT,
        TERRAIN_CLIP_BOTTOM,TERRAIN_CLIP_TOP);
    for(int p=0;p<4;++p)if((code_or&planes[p])!=0){
        ClipToPlane(view,output_poly,output_count,planes[p]);
        ClipCodes(view,output_poly,output_count,code_or,code_and);
        if(output_count<3||code_and!=0)return false;}
    return (code_or&TERRAIN_CLIP_BEHIND)==0;
}

bool CellOutside(TerrainView view,vec3 r0,vec3 r1,vec3 r2,vec3 r3){
    if(r0.z<0.0&&r1.z<0.0&&r2.z<0.0&&r3.z<0.0)return true;
    if(r0.x < -r0.z*view.clip_scale.x&&r1.x < -r1.z*view.clip_scale.x&&
       r2.x < -r2.z*view.clip_scale.x&&r3.x < -r3.z*view.clip_scale.x)return true;
    if(r0.x > r0.z*view.clip_scale.y&&r1.x > r1.z*view.clip_scale.y&&
       r2.x > r2.z*view.clip_scale.y&&r3.x > r3.z*view.clip_scale.y)return true;
    if(r0.y < -r0.z*view.clip_scale.z&&r1.y < -r1.z*view.clip_scale.z&&
       r2.y < -r2.z*view.clip_scale.z&&r3.y < -r3.z*view.clip_scale.z)return true;
    if(r0.y > r0.z*view.clip_scale.w&&r1.y > r1.z*view.clip_scale.w&&
       r2.y > r2.z*view.clip_scale.w&&r3.y > r3.z*view.clip_scale.w)return true;
    return false;
}

uint TriangleCount(TerrainView view,vec3 world0,vec3 rotated0,vec3 world1,
    vec3 rotated1,vec3 world2,vec3 rotated2,vec2 uv0,vec2 uv1,vec2 uv2,
    vec2 lm0,vec2 lm1,vec2 lm2){
    vec3 facing_normal=cross(rotated1-rotated0,rotated2-rotated1);
    if(dot(facing_normal,rotated1)>=0.0)return 0u;
    ClipVertex input_poly[3];
    input_poly[0]=ClipVertex(world0,rotated0,uv0,lm0);
    input_poly[1]=ClipVertex(world1,rotated1,uv1,lm1);
    input_poly[2]=ClipVertex(world2,rotated2,uv2,lm2);
    ClipVertex output_poly[TERRAIN_CLIP_MAX_VERTICES];int output_count=0;
    if(!ClipTriangle(view,input_poly,output_poly,output_count))return 0u;
    return output_count>=3?uint(output_count-2)*3u:0u;
}

uint ClassifyWorkItem(uint work_index){
    TerrainWork work=terrain_work[work_index];TerrainCell cell=terrain_cells[work.cell_index];
    TerrainView view=terrain_views[0];uint segment=cell.packed.x;uint rotation=cell.packed.y&255u;
    vec3 w0=CellPosition(segment,cell.height,0u);vec3 w1=CellPosition(segment,cell.height,1u);
    vec3 w2=CellPosition(segment,cell.height,2u);vec3 w3=CellPosition(segment,cell.height,3u);
    vec3 r0=RotatePoint(view,w0);vec3 r1=RotatePoint(view,w1);
    vec3 r2=RotatePoint(view,w2);vec3 r3=RotatePoint(view,w3);
    if(CellOutside(view,r0,r1,r2,r3))return 0u;
    vec2 uv0=BaseUv(segment,rotation,0u);vec2 uv1=BaseUv(segment,rotation,1u);
    vec2 uv2=BaseUv(segment,rotation,2u);vec2 uv3=BaseUv(segment,rotation,3u);
    vec2 lm0=LightmapUv(segment,0u);vec2 lm1=LightmapUv(segment,1u);
    vec2 lm2=LightmapUv(segment,2u);vec2 lm3=LightmapUv(segment,3u);
    return TriangleCount(view,w0,r0,w1,r1,w3,r3,uv0,uv1,uv3,lm0,lm1,lm3)+
        TriangleCount(view,w3,r3,w1,r1,w2,r2,uv3,uv1,uv2,lm3,lm1,lm2);
}

void WriteVertex(TerrainView view,uint index,uint texture_page,uint lightmap_page,
    ClipVertex vertex){
    if(index>=terrain_push.output_vertex_capacity)return;
    float eye_z=max(vertex.rotated.z,TERRAIN_MIN_Z);float q=1.0/eye_z;
    float depth=clamp(1.0-q,0.0,1.0);
    vec2 screen=vec2(view.projection_center_half_size.x+
        vertex.rotated.x*view.projection_center_half_size.z*q,
        view.projection_center_half_size.y-
        vertex.rotated.y*view.projection_center_half_size.w*q);
    vec2 ndc=screen*2.0/max(view.viewport_size_inv_size.xy,vec2(1.0))-vec2(1.0);
    BaseVertex base;base.position=vec3(ndc,depth);base.rgba8=0xffffffffu;
    base.uv0=vertex.base_uv*q;base.uv1=vertex.lightmap_uv*q;
    TerrainPayload payload;payload.world_q=vec4(vertex.world*q,q);
    payload.packed_pages=(texture_page<<8u)|(lightmap_page&255u);
    payload.reserved0=0u;payload.reserved1=0u;payload.reserved2=0u;
    terrain_base_output[index]=base;terrain_payload_output[index]=payload;
}

uint EmitTriangle(TerrainView view,uint vertex_base,uint texture_page,
    uint lightmap_page,vec3 world0,vec3 rotated0,vec3 world1,vec3 rotated1,
    vec3 world2,vec3 rotated2,vec2 uv0,vec2 uv1,vec2 uv2,
    vec2 lm0,vec2 lm1,vec2 lm2){
    vec3 facing_normal=cross(rotated1-rotated0,rotated2-rotated1);
    if(dot(facing_normal,rotated1)>=0.0)return 0u;
    ClipVertex input_poly[3];input_poly[0]=ClipVertex(world0,rotated0,uv0,lm0);
    input_poly[1]=ClipVertex(world1,rotated1,uv1,lm1);
    input_poly[2]=ClipVertex(world2,rotated2,uv2,lm2);
    ClipVertex poly[TERRAIN_CLIP_MAX_VERTICES];int count=0;
    if(!ClipTriangle(view,input_poly,poly,count))return 0u;
    uint cursor=0u;for(int i=1;i<count-1;++i){
        WriteVertex(view,vertex_base+cursor,texture_page,lightmap_page,poly[0]);
        WriteVertex(view,vertex_base+cursor+1u,texture_page,lightmap_page,poly[i]);
        WriteVertex(view,vertex_base+cursor+2u,texture_page,lightmap_page,poly[i+1]);cursor+=3u;}
    return cursor;
}

void EmitWorkItem(uint work_index){
    TerrainWork work=terrain_work[work_index];TerrainCell cell=terrain_cells[work.cell_index];
    TerrainView view=terrain_views[0];uint segment=cell.packed.x;uint rotation=cell.packed.y&255u;
    uint lightmap_page=(cell.packed.y>>8u)&255u;uint texture_page=cell.packed.y>>16u;
    vec3 w0=CellPosition(segment,cell.height,0u);vec3 w1=CellPosition(segment,cell.height,1u);
    vec3 w2=CellPosition(segment,cell.height,2u);vec3 w3=CellPosition(segment,cell.height,3u);
    vec3 r0=RotatePoint(view,w0);vec3 r1=RotatePoint(view,w1);
    vec3 r2=RotatePoint(view,w2);vec3 r3=RotatePoint(view,w3);
    if(CellOutside(view,r0,r1,r2,r3))return;
    vec2 uv0=BaseUv(segment,rotation,0u);vec2 uv1=BaseUv(segment,rotation,1u);
    vec2 uv2=BaseUv(segment,rotation,2u);vec2 uv3=BaseUv(segment,rotation,3u);
    vec2 lm0=LightmapUv(segment,0u);vec2 lm1=LightmapUv(segment,1u);
    vec2 lm2=LightmapUv(segment,2u);vec2 lm3=LightmapUv(segment,3u);
    uint cursor=terrain_scratch[work_index].y;
    cursor+=EmitTriangle(view,cursor,texture_page,lightmap_page,
        w0,r0,w1,r1,w3,r3,uv0,uv1,uv3,lm0,lm1,lm3);
    EmitTriangle(view,cursor,texture_page,lightmap_page,
        w3,r3,w1,r1,w2,r2,uv3,uv1,uv2,lm3,lm1,lm2);
}

#endif
