#pragma once

#include "polymodel.h"

bool RetainedPolymodelEnabled();
bool RetainedPolymodelCanDrawBaseFace(poly_model *pm, bsp_info *sm, int facenum);
bool RetainedPolymodelCanSkipPointRotation(poly_model *pm, bsp_info *sm);
bool RetainedPolymodelDrawFaces(poly_model *pm, bsp_info *sm, const int *facenums, int count,
	float u_offset, float v_offset, const vector *base_color);
bool RetainedPolymodelDrawFogFaces(poly_model *pm, bsp_info *sm, const int *facenums, int count,
	const vector *fog_plane, float fog_distance, float fog_eye_distance, float fog_depth,
	bool use_fog_plane);
void RetainedPolymodelInvalidateModel(int model_num);
