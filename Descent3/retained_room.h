#pragma once

struct room;
struct vector;
struct matrix;

bool RetainedRoomCanDrawBaseFace(room* rp, int facenum);
bool RetainedRoomDrawFaces(room* rp, const int* facenums, int count,
	int bitmap_handle, float u_offset, float v_offset, float light_scalar,
	int lightmap_handle,
	int clip_codes = 0, bool per_pixel_specular_payload = false);
bool RetainedRoomDrawFogFaces(room* rp, const int* facenums, int count,
	const vector* fog_plane, float fog_distance, float fog_eye_distance,
	float fog_depth, bool use_fog_plane, const vector* viewer_eye,
	const matrix* viewer_orient, int clip_codes = 0);
void RetainedRoomSetDeformation(room* rp, unsigned int seed,
	const vector* direction, float range);
void RetainedRoomClearDeformation(room* rp);
void RetainedRoomSetTransform(room* rp, const float transform[16]);
void RetainedRoomClearTransform(room* rp);
void RetainedRoomInvalidateAll();
void RetainedRoomPrecacheAll(bool include_specular);
