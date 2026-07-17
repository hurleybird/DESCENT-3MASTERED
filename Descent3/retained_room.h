#pragma once

struct room;
struct vector;

bool RetainedRoomCanDrawBaseFace(room* rp, int facenum);
bool RetainedRoomDrawFaces(room* rp, const int* facenums, int count,
	float u_offset, float v_offset, float light_scalar, int lightmap_handle);
bool RetainedRoomDrawFogFaces(room* rp, const int* facenums, int count,
	const vector* fog_plane, float fog_distance, float fog_eye_distance,
	float fog_depth, bool use_fog_plane);
void RetainedRoomSetDeformation(room* rp, unsigned int seed,
	const vector* direction, float range);
void RetainedRoomClearDeformation(room* rp);
void RetainedRoomInvalidateAll();
