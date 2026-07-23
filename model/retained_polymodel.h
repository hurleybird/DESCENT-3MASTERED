#pragma once

#include "polymodel.h"

bool RetainedPolymodelEnabled();
void RetainedPolymodelPrecache(int model_num);
bool RetainedPolymodelCanDrawBaseFace(poly_model *pm, bsp_info *sm, int facenum);
bool RetainedPolymodelCanSkipPointRotation(poly_model *pm, bsp_info *sm);
bool RetainedPolymodelStraddlesEyePlane(bsp_info *sm);
void RetainedPolymodelPrepareSubmodel(poly_model *pm, bsp_info *sm, bool advance_visual_random);
bool RetainedPolymodelDrawFaces(poly_model *pm, bsp_info *sm, const int *facenums, int count,
	float u_offset, float v_offset, const vector *base_color,
	bool retained_lightmap_arrays, bool retained_dynamic_lightmaps);
bool RetainedPolymodelDrawFogFaces(poly_model *pm, bsp_info *sm, const int *facenums, int count,
	const vector *fog_plane, float fog_distance, float fog_eye_distance, float fog_depth,
	bool use_fog_plane);
bool RetainedPolymodelDrawSpecularFaces(poly_model *pm, bsp_info *sm, const int *facenums, int count,
	const vector *view_position, const vector *light_position, float scalar, bool smooth);
void RetainedPolymodelInvalidateLightmapObject(lightmap_object *lightmap);
void RetainedPolymodelInvalidateModel(int model_num);
