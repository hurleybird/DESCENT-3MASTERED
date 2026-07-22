/*
* Descent 3
* Copyright (C) 2024 Parallax Software
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "pserror.h"
#include "pstypes.h"
#include "fireball.h"
#include "weather.h"
#include "viseffect.h"
#include "object.h"
#include "terrain.h"
#include "room.h"
#include "game.h"
#include "config.h"
#include "soundload.h"
#include "hlsoundlib.h"
#include "sounds.h"
#include "args.h"
#include "findintersection.h"
#include "fireball_external.h"
#include "weapon.h"

#include <stdlib.h>
#include <math.h>
#include <vector>

#include "psrand.h"

weather Weather = { 0 };
static std::vector<enhanced_snow_particle> Enhanced_snow_particles;

int ThunderA_sound_handle = -1;
int ThunderB_sound_handle = -1;

static void ApplyEnhancedWeatherGravity(vector* pos, vector* velocity, float strength_scale);
static bool FindEnhancedWeatherSurfaceHit(const vector* start, const vector* end,
	vector* hit_pos, vector* hit_normal, int* hit_room);

// resets the weather so there is nothing happening
void ResetWeather()
{
	Weather.flags = 0;
	Weather.last_lightning_evaluation_time = 0;
	Enhanced_snow_particles.clear();
}

// Makes droplets appear on the windshield, plus makes rain fall in the distance
void DoRainEffect()
{
	float droplet_ages[8] = {};
	const int viewer_index = OBJNUM(Viewer_object);
	const int droplet_events = Get60HzVisualEventAges(viewer_index,
		Viewer_object->handle, VIS60_WEATHER_DROPLETS, droplet_ages, 8);

	// See how many droplets to create on the windshield
	// This is dependant on how fast the player is moving forward
	int randval = 1 + ((1.0 - Weather.rain_intensity_scalar) * MAX_RAIN_INTENSITY);

	if (randval < 20)
		randval = 20;

	vector vel = Viewer_object->mtype.phys_info.velocity;
	float mag = vm_GetMagnitudeFast(&vel);
	vel /= mag;

	float scalar = vm_DotProduct(&vel, &Viewer_object->orient.fvec);

	vector upvec = { 0,1.0,0 };

	scalar *= (1 + (mag / 100));

	randval -= scalar * 10;

	if (randval < 2)
		randval = 2;
	else if (randval > 80)
		randval = 80;

	if ((upvec * Viewer_object->orient.uvec) < 0)
		randval = 8000; // Make sure rain does fall upwards

	for (int event = 0; event < droplet_events; ++event)
	{
		if (Viewer_object->type != OBJ_PLAYER || !OBJECT_OUTSIDE(Viewer_object) ||
			(ps_rand() % randval) != 0)
			continue;

		// Put some droplets on the windshield
		vector pos = { 0,0,0 };

		pos.x = ((ps_rand() % 1000) - 500) / 250.0;
		pos.y = ((ps_rand() % 1000) - 500) / 350.0;
		pos.z = 3.0;

		int visnum = VisEffectCreate(VIS_FIREBALL, RAINDROP_INDEX, Viewer_object->roomnum, &pos);
		if (visnum >= 0)
		{
			vis_effect* vis = &VisEffects[visnum];
			float life = .4 + ((ps_rand() % 500) / 1000.0);
			vis->lifeleft = life;
			vis->lifetime = life;
			vis->creation_time -= droplet_ages[event];
			vis->lifeleft -= droplet_ages[event];
			vis->size = .01 + ((ps_rand() % 500) / 3000.0);

			if (vis->size > .05)
			{
				Sound_system.Play2dSound(SOUND_RAINDROP);
			}

			vis->flags |= VF_WINDSHIELD_EFFECT | VF_CLOSE_SCREEN_EFFECT;
		}
	}

	// Create some rain in the distance

	if (OBJECT_OUTSIDE(Viewer_object))
	{
		PSRand rain_rand;
		rain_rand.seed(Get60HzVisualNoise((uint32_t)Viewer_object->handle, 4));
		int num = 20 + (rain_rand() % 15);
		angvec angs;
		int i;

		vm_ExtractAnglesFromMatrix(&angs, &Viewer_object->orient);
		matrix mat;
		vm_AnglesToMatrix(&mat, 0, angs.h, 0);

		for (i = 0; i < num; i++)
		{
			vector pos = Viewer_object->pos;

			float z = ((rain_rand() % 1000) / 1000.0) * 700;
			float x = (((rain_rand() % 1000) - 500) / 500.0) * 300;
			float y = (((rain_rand() % 1000) - 500) / 500.0) * 200;

			pos += x * mat.rvec;
			pos += y * mat.uvec;
			pos += z * mat.fvec;

			// Create falling rain
			vector rain_velocity = { 0.0f, -620.0f, 0.0f };
			ApplyEnhancedWeatherGravity(&pos, &rain_velocity, 2.0f);
			int visnum = VisEffectCreate(VIS_FIREBALL, FADING_LINE_INDEX, Viewer_object->roomnum, &pos);
			if (visnum >= 0)
			{
				vis_effect* vis = &VisEffects[visnum];
				float life = .001f;
				vis->lifeleft = life;
				vis->lifetime = life;
				vis->lighting_color = GR_RGB16(200, 200, 255);
				vis->end_pos = pos;
				vector streak = rain_velocity;
				if (vm_NormalizeVectorFast(&streak) <= 0.0f)
					streak = { 0.0f, -1.0f, 0.0f };
				vis->end_pos -= streak * 20.0f;
				vis->flags |= VF_WINDSHIELD_EFFECT;
				vis->pos -= (Viewer_object->mtype.phys_info.velocity / 2);
			}
		}

		float puddle_ages[8] = {};
		const int puddle_events = Get60HzVisualEventAges(viewer_index,
			Viewer_object->handle, VIS60_WEATHER_PUDDLES, puddle_ages, 8);
		for (int event = 0; event < puddle_events; ++event)
		{
			const int puddle_count = num / 2;
			for (i = 0; i < puddle_count; i++)
			{
				vector pos = Viewer_object->pos;

				float z = ((ps_rand() % 1000) / 1000.0) * 700;
				float x = (((ps_rand() % 1000) - 500) / 500.0) * 300;
				float y = (((ps_rand() % 1000) - 500) / 500.0) * 200;

				pos += x * mat.rvec;
				pos += y * mat.uvec;
				pos += z * mat.fvec;

				if (pos.z < 0 || pos.z >= TERRAIN_DEPTH * TERRAIN_SIZE)
					continue;
				if (pos.x < 0 || pos.x >= TERRAIN_WIDTH * TERRAIN_SIZE)
					continue;

				// Create puddle drops on the terrain or, with enhanced weather,
				// on visible polymodels that intercept the same fall path.
				vector norm;
				float ypos = GetTerrainGroundPoint(&pos, &norm);
				vector hit_pos, hit_norm;
				int hit_room = Viewer_object->roomnum;
				vector fall_start = pos;
				fall_start.y += 96.0f;
				vector fall_end = pos;
				fall_end.y = ypos - 4.0f;
				const bool hit_weather_surface = FindEnhancedWeatherSurfaceHit(&fall_start,
					&fall_end, &hit_pos, &hit_norm, &hit_room);
				if (hit_weather_surface)
				{
					pos = hit_pos + hit_norm * 0.03f;
					norm = hit_norm;
				}
				else
				pos.y = ypos;
				int visnum = VisEffectCreate(VIS_FIREBALL, PUDDLEDROP_INDEX,
					hit_weather_surface ? hit_room : Viewer_object->roomnum, &pos);
				if (visnum >= 0)
				{
					vis_effect* vis = &VisEffects[visnum];
					float life = .2f;
					float size = .7 + ((ps_rand() % 10) / 20.0);
					vis->lifeleft = life - puddle_ages[event];
					vis->lifetime = life;
					vis->creation_time -= puddle_ages[event];
					vis->end_pos = norm;
					vis->flags |= VF_PLANAR;
				}
			}
		}
	}
}

static float SnowRandomUnit(PSRand& random)
{
	return (random() & 0x7fff) / 32767.0f;
}

static float SnowClampIntensity(float intensity)
{
	// The original renderer ignored the scalar, and some old content consequently
	// enables snow with a zero scalar. Keep those levels snowy instead of silently
	// changing their presentation.
	if (intensity <= 0.01f)
		return 1.0f;
	if (intensity >= 1.0f)
		return 1.0f;
	return intensity;
}

static void ApplyEnhancedWeatherGravity(vector* pos, vector* velocity, float strength_scale)
{
	if (!Render_enhanced_weather || !pos || !velocity)
		return;

	for (int i = 0; i <= Highest_object_index; ++i)
	{
		object* obj = &Objects[i];
		if (obj->type != OBJ_FIREBALL || obj->id != GRAVITY_FIELD_INDEX)
			continue;
		if (obj->flags & OF_DEAD)
			continue;

		const float radius = obj->ctype.blast_info.max_size * 2.2f;
		if (radius <= 0.0f)
			continue;

		vector delta = obj->pos - *pos;
		const float dist = vm_GetMagnitudeFast(&delta);
		if (dist <= 0.01f || dist >= radius)
			continue;

		delta /= dist;
		const float falloff = 1.0f - (dist / radius);
		const float acceleration = 760.0f * falloff * falloff * strength_scale;
		*velocity += delta * (acceleration * Frametime);
	}
}

static bool FindEnhancedWeatherSurfaceHit(const vector* start, const vector* end,
	vector* hit_pos, vector* hit_normal, int* hit_room)
{
	if (!Render_enhanced_weather || !Viewer_object || !start || !end)
		return false;

	vector query_start = *start;
	vector query_end = *end;
	fvi_query fq = {};
	fvi_info hit_info = {};
	fq.p0 = &query_start;
	fq.p1 = &query_end;
	fq.startroom = Viewer_object->roomnum;
	fq.rad = 0.0f;
	fq.thisobjnum = -1;
	fq.ignore_obj_list = NULL;
	fq.flags = FQ_CHECK_OBJS | FQ_IGNORE_TERRAIN | FQ_IGNORE_WEAPONS |
		FQ_IGNORE_POWERUPS | FQ_OBJ_BACKFACE | FQ_BACKFACE | FQ_NO_RELINK;

	const int fate = fvi_FindIntersection(&fq, &hit_info);
	if (fate == HIT_WALL || fate == HIT_BACKFACE || fate == HIT_CORNER_WALL ||
		fate == HIT_EDGE_WALL || fate == HIT_FACE_WALL)
	{
		if (hit_pos)
			*hit_pos = hit_info.hit_pnt;
		if (hit_normal)
		{
			*hit_normal = hit_info.hit_wallnorm[0];
			if (vm_NormalizeVectorFast(hit_normal) <= 0.0f)
				*hit_normal = { 0.0f, 1.0f, 0.0f };
		}
		if (hit_room)
			*hit_room = hit_info.hit_room >= 0 ? hit_info.hit_room : Viewer_object->roomnum;
		return true;
	}
	if (fate != HIT_OBJECT && fate != HIT_SPHERE_2_POLY_OBJECT)
		return false;

	const int num_hits = hit_info.num_hits > 0 ? hit_info.num_hits : 1;
	for (int i = 0; i < num_hits && i < MAX_HITS; ++i)
	{
		const int objnum = hit_info.hit_object[i];
		if (objnum < 0 || objnum > Highest_object_index)
			continue;
		object* obj = &Objects[objnum];
		if (!(obj->flags & OF_POLYGON_OBJECT))
			continue;
		if (obj->flags & OF_DEAD)
			continue;

		if (hit_pos)
			*hit_pos = hit_info.hit_face_pnt[i];
		if (hit_normal)
		{
			*hit_normal = hit_info.hit_wallnorm[i];
			if (vm_NormalizeVectorFast(hit_normal) <= 0.0f)
				*hit_normal = { 0.0f, 1.0f, 0.0f };
		}
		if (hit_room)
			*hit_room = obj->roomnum;
		return true;
	}

	return false;
}

static void MoveEnhancedSnowParticle(enhanced_snow_particle& particle)
{
	particle.lifeleft -= Frametime;
	if (particle.lifeleft <= 0.0f)
	{
		particle.flags |= 1;
		return;
	}

	if (!(particle.flags & 2))
	{
		const float time_live = Gametime - particle.creation_time;
		const float phase = particle.phase + time_live * particle.flutter_frequency;
		const float flutter_speed = sinf(phase) * (3.0f + particle.size * 5.25f);
		vector frame_velocity = particle.velocity;
		frame_velocity.x += particle.ground_data.x * flutter_speed;
		frame_velocity.z += particle.ground_data.z * flutter_speed;
		frame_velocity.y += cosf(phase * 0.71f) * 0.8f;
		ApplyEnhancedWeatherGravity(&particle.pos, &frame_velocity, 1.0f);
		const vector previous_pos = particle.pos;
		particle.pos += frame_velocity * Frametime;

		if (particle.pos.y - particle.ground_data.y < 96.0f)
		{
			vector ground_normal;
			const float ground_y = GetTerrainGroundPoint(&particle.pos, &ground_normal);
			particle.ground_data.y = ground_y;
			vector hit_pos, hit_normal;
			int hit_room;
			if (FindEnhancedWeatherSurfaceHit(&previous_pos, &particle.pos,
				&hit_pos, &hit_normal, &hit_room))
			{
				particle.pos = hit_pos + hit_normal * 0.04f;
				particle.velocity = hit_normal;
				particle.flags |= 2;
				if (particle.lifeleft > 0.32f)
					particle.lifeleft = 0.32f;
			}
			else
			if (particle.pos.y <= ground_y + 0.12f)
			{
				particle.pos.y = ground_y + 0.04f;
				particle.velocity = ground_normal;
				particle.flags |= 2;
				if (particle.lifeleft > 0.32f)
					particle.lifeleft = 0.32f;
			}
		}
	}

	if (Viewer_object)
	{
		const vector delta = particle.pos - Viewer_object->pos;
		const float horizontal_distance_squared = delta.x * delta.x + delta.z * delta.z;
		vector view_forward = Viewer_object->orient.fvec;
		view_forward.y = 0.0f;
		vm_NormalizeVector(&view_forward);
		const float forward_distance = delta.x * view_forward.x + delta.z * view_forward.z;
		if (horizontal_distance_squared > 390.0f * 390.0f ||
			delta.y < -150.0f || delta.y > 195.0f || forward_distance < -90.0f)
		{
			particle.flags |= 1;
		}
	}
}

static void MoveEnhancedSnowParticles()
{
	for (size_t i = 0; i < Enhanced_snow_particles.size(); ++i)
		MoveEnhancedSnowParticle(Enhanced_snow_particles[i]);

	size_t write = 0;
	for (size_t read = 0; read < Enhanced_snow_particles.size(); ++read)
	{
		if (Enhanced_snow_particles[read].flags & 1)
			continue;
		if (write != read)
			Enhanced_snow_particles[write] = Enhanced_snow_particles[read];
		write++;
	}
	Enhanced_snow_particles.resize(write);
}

static int CreateEnhancedSnow(float age, PSRand& random, int available_slots)
{
	const float intensity = SnowClampIntensity(Weather.snow_intensity_scalar);
	int num = 14 + (random() % 10);
	if (num > available_slots)
		num = available_slots;
	if (num <= 0)
		return 0;

	// Place new flakes in a world-aligned volume rather than a box attached to
	// the current view. A modest velocity lead keeps a fast ship from outrunning
	// the storm while every existing flake remains fixed in world space.
	vector center = Viewer_object->pos;
	vector viewer_velocity = Viewer_object->mtype.phys_info.velocity;
	float viewer_speed = vm_GetMagnitudeFast(&viewer_velocity);
	if (viewer_speed > 0.01f)
	{
		float lead_time = 0.35f;
		if (viewer_speed * lead_time > 80.0f)
			lead_time = 80.0f / viewer_speed;
		center += viewer_velocity * lead_time;
	}

	const float wind_angle = Gametime * 0.075f;
	const vector prevailing_wind = {
		cosf(wind_angle) * (4.0f + intensity * 4.0f),
		0.0f,
		sinf(wind_angle * 0.83f) * (3.0f + intensity * 3.0f)
	};
	angvec view_angles;
	vm_ExtractAnglesFromMatrix(&view_angles, &Viewer_object->orient);
	matrix spawn_orient;
	vm_AnglesToMatrix(&spawn_orient, 0, view_angles.h, 0);

	int created = 0;
	for (int i = 0; i < num; ++i)
	{
		const float depth_roll = SnowRandomUnit(random);
		const float forward = 12.0f + depth_roll * depth_roll * 320.0f;
		const float half_width = 42.0f + forward * 0.34f;
		const float lateral = (SnowRandomUnit(random) * 2.0f - 1.0f) * half_width;
		const float vertical = -35.0f + SnowRandomUnit(random) * 175.0f;
		vector pos = center;
		pos += spawn_orient.rvec * lateral;
		pos += spawn_orient.uvec * vertical;
		pos += spawn_orient.fvec * forward;

		if (pos.z < 0 || pos.z >= TERRAIN_DEPTH * TERRAIN_SIZE ||
			pos.x < 0 || pos.x >= TERRAIN_WIDTH * TERRAIN_SIZE)
		{
			continue;
		}

		vector ground_normal;
		const float ground_y = GetTerrainGroundPoint(&pos, &ground_normal);
		if (pos.y < ground_y + 8.0f)
			pos.y = ground_y + 8.0f + SnowRandomUnit(random) * 70.0f;

		enhanced_snow_particle particle = {};
		particle.pos = pos;
		const float size_roll = SnowRandomUnit(random);
		if (size_roll < 0.82f)
			particle.size = 0.25f + SnowRandomUnit(random) * 0.40f;
		else
			particle.size = 0.65f + SnowRandomUnit(random) * 0.35f;

		const float life = 4.5f + SnowRandomUnit(random) * 3.0f;
		particle.lifetime = life;
		particle.lifeleft = life - age;
		particle.creation_time = Gametime - age;

		particle.velocity = prevailing_wind;
		particle.velocity.x += (SnowRandomUnit(random) - 0.5f) * 9.0f;
		particle.velocity.z += (SnowRandomUnit(random) - 0.5f) * 9.0f;
		particle.velocity.y = -(18.0f + SnowRandomUnit(random) * 28.0f);

		const float flutter_angle = SnowRandomUnit(random) * 6.28318530718f;
		particle.ground_data.x = cosf(flutter_angle);
		particle.ground_data.y = ground_y;
		particle.ground_data.z = sinf(flutter_angle);
		particle.phase = SnowRandomUnit(random) * 6.28318530718f;
		particle.flutter_frequency = 1.35f + SnowRandomUnit(random) * 3.6f;
		particle.variant = (ubyte)(random() & 3);

		const int tint = 205 + (random() % 35);
		particle.lighting_color = GR_RGB16(tint, tint + 3, 255);
		particle.pos += particle.velocity * age;
		Enhanced_snow_particles.push_back(particle);
		created++;
	}

	return created;
}

static void DoLegacySnowEffect()
{
	if (OBJECT_OUTSIDE(Viewer_object))
	{
		float snow_ages[8] = {};
		const int snow_events = Get60HzVisualEventAges(OBJNUM(Viewer_object),
			Viewer_object->handle, VIS60_WEATHER_SNOW, snow_ages, 8);
		for (int event = 0; event < snow_events; ++event)
		{
			int num = 20 + (ps_rand() % 15);

			if (Weather.snowflakes_to_create + num > 250)
			{
				num = 250 - Weather.snowflakes_to_create;

				if (num < 1)
				{
					Weather.snowflakes_to_create = 0;
					return;
				}
			}

			matrix mat = Viewer_object->orient;

			for (int i = 0; i < num; i++)
			{
				vector pos = Viewer_object->pos;

				float z = ((ps_rand() % 1000) / 1000.0) * 300;
				float x = (((ps_rand() % 1000) - 500) / 500.0) * 200;
				int y = (ps_rand() % 80);

				pos += x * mat.rvec;
				pos += y * mat.uvec;
				pos += z * mat.fvec;

				if (pos.z < 0 || pos.z >= TERRAIN_DEPTH * TERRAIN_SIZE)
					continue;
				if (pos.x < 0 || pos.x >= TERRAIN_WIDTH * TERRAIN_SIZE)
					continue;

				vector down_vec = { 0,-30,0 };
				int visnum = VisEffectCreate(VIS_FIREBALL, SNOWFLAKE_INDEX, Viewer_object->roomnum, &pos);
				if (visnum >= 0)
				{
					vis_effect* vis = &VisEffects[visnum];
					float life = 1.5 + ((ps_rand() % 100) / 100.0);
					vis->lifeleft = life;
					vis->lifetime = life;
					vis->creation_time -= snow_ages[event];
					vis->lighting_color = GR_RGB16(200, 200, (ps_rand() % 50) + 200);
					vis->size = ((ps_rand() % 1000) / 1000.0) + .5;

					vis->flags |= VF_WINDSHIELD_EFFECT | VF_USES_LIFELEFT | VF_CLOSE_SCREEN_EFFECT;

					vis->velocity = down_vec;
					vis->pos += vis->velocity * snow_ages[event];
					vis->lifeleft -= snow_ages[event];
				}
			}
		}
	}

	Weather.snowflakes_to_create = 0;
}

// Creates snow for this frame
void DoSnowEffect()
{
	if (!Render_enhanced_weather)
	{
		Enhanced_snow_particles.clear();
		DoLegacySnowEffect();
		return;
	}

	if (OBJECT_OUTSIDE(Viewer_object))
	{
		MoveEnhancedSnowParticles();
		const float intensity = SnowClampIntensity(Weather.snow_intensity_scalar);
		const int desired_flakes = (int)(220.0f + intensity * 292.0f);
		int missing_flakes = desired_flakes - (int)Enhanced_snow_particles.size();
		if (missing_flakes < 0)
			missing_flakes = 0;

		float snow_ages[8] = {};
		const int snow_events = Get60HzVisualEventAges(OBJNUM(Viewer_object),
			Viewer_object->handle, VIS60_WEATHER_SNOW, snow_ages, 8);
		for (int event = 0; event < snow_events; ++event)
		{
			if (missing_flakes <= 0)
				break;
			PSRand snow_random(Get60HzVisualNoise((uint32_t)Viewer_object->handle,
				0x50u + (uint32_t)event));
			const int created = CreateEnhancedSnow(snow_ages[event], snow_random, missing_flakes);
			missing_flakes -= created;
		}
	}
	else
	{
		Enhanced_snow_particles.clear();
	}

	Weather.snowflakes_to_create = 0;
}

const enhanced_snow_particle* GetEnhancedSnowParticles(int* count)
{
	if (count)
		*count = (int)Enhanced_snow_particles.size();
	return Enhanced_snow_particles.empty() ? NULL : Enhanced_snow_particles.data();
}

// does all the weather stuff that is going to be done for this frame
void DoWeatherForFrame()
{
	int i;
	int hear_thunder = 0;
	static const bool force_snow = FindArg("-force-snow") != 0;

	if (OBJECT_OUTSIDE(Player_object))
		hear_thunder = 1;
	else
	{
		int roomnum = Player_object->roomnum;
		room* rp = &Rooms[roomnum];

		for (i = 0; i < rp->num_portals && hear_thunder == 0; i++)
		{
			if (rp->portals[i].croom == -1)
				hear_thunder = 1;
			else if (Rooms[rp->portals[i].croom].flags & RF_EXTERNAL)
				hear_thunder = 1;

		}
	}

	if (Weather.flags & WEATHER_FLAGS_RAIN)
		DoRainEffect();

	if ((Weather.flags & WEATHER_FLAGS_SNOW) || force_snow)
		DoSnowEffect();

	if (Weather.flags & WEATHER_FLAGS_LIGHTNING)
	{
		if ((Gametime - Weather.last_lightning_evaluation_time) > Weather.lightning_interval_time)
		{
			// Check to see if we should draw some lightning
			Weather.last_lightning_evaluation_time = Gametime;
			ASSERT(Weather.lightning_rand_value > 0);
			if (ps_rand() <= Weather.lightning_rand_value)
			{
				// Start the lightning sequence
				Weather.lightning_sequence = 1;

				if ((ps_rand() % 7) == 0 && hear_thunder)
					Sound_system.Play2dSound(SOUND_LIGHTNING);
			}

			if (hear_thunder && (ps_rand() % (Weather.lightning_rand_value * 3)) == 0)
			{
				if (ThunderA_sound_handle == -1)
					ThunderA_sound_handle = FindSoundName("ThunderA");
				if (ThunderB_sound_handle == -1)
					ThunderB_sound_handle = FindSoundName("ThunderB");

				if (ps_rand() % 2)
					Sound_system.Play2dSound(ThunderA_sound_handle);
				else
					Sound_system.Play2dSound(ThunderB_sound_handle);
			}
		}
	}
}

// Sets the state of the rain to on or off, plus sets the intensity of the rain (0 to 1)
void SetRainState(int on, float intensity)
{
	if (on)
	{
		Weather.flags |= WEATHER_FLAGS_RAIN;
		Weather.rain_intensity_scalar = intensity;
	}
	else
	{
		Weather.flags &= ~WEATHER_FLAGS_RAIN;
	}
}

// Sets the state of the rain to on or off, plus sets the intensity of the rain (0 to 1)
void SetSnowState(int on, float intensity)
{
	if (on)
	{
		Weather.flags |= WEATHER_FLAGS_SNOW;
		Weather.snow_intensity_scalar = intensity;
	}
	else
	{
		Weather.flags &= ~WEATHER_FLAGS_SNOW;
	}
}

// Sets the state of lightning to on or off, plus allows the setting of how often to check
// for lightning and the randomness at which lightning happens
void SetLightningState(int on, float interval_time, int randval)
{
	if (on)
	{
		Weather.flags |= WEATHER_FLAGS_LIGHTNING;
		Weather.lightning_sequence = 0;
		Weather.lightning_rand_value = randval;
		Weather.lightning_interval_time = interval_time;
	}
	else
	{
		Weather.flags &= ~WEATHER_FLAGS_LIGHTNING;
	}
}
