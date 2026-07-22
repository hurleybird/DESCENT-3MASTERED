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

#include <stdlib.h>
#include <math.h>

#include "psrand.h"

weather Weather = { 0 };

int ThunderA_sound_handle = -1;
int ThunderB_sound_handle = -1;


// resets the weather so there is nothing happening
void ResetWeather()
{
	Weather.flags = 0;
	Weather.last_lightning_evaluation_time = 0;
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
			int visnum = VisEffectCreate(VIS_FIREBALL, FADING_LINE_INDEX, Viewer_object->roomnum, &pos);
			if (visnum >= 0)
			{
				vis_effect* vis = &VisEffects[visnum];
				float life = .001f;
				vis->lifeleft = life;
				vis->lifetime = life;
				vis->lighting_color = GR_RGB16(200, 200, 255);
				vis->end_pos = pos;
				vis->end_pos.y += 20;
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

				// Create puddle drops on the terrain.
				vector norm;
				float ypos = GetTerrainGroundPoint(&pos, &norm);
				pos.y = ypos;
				int visnum = VisEffectCreate(VIS_FIREBALL, PUDDLEDROP_INDEX, Viewer_object->roomnum, &pos);
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

		int visnum = VisEffectCreate(VIS_FIREBALL, SNOWFLAKE_INDEX,
			Viewer_object->roomnum, &pos);
		if (visnum < 0)
			continue;

		vis_effect* vis = &VisEffects[visnum];
		const float size_roll = SnowRandomUnit(random);
		if (size_roll < 0.82f)
			vis->size = 0.25f + SnowRandomUnit(random) * 0.40f;
		else
			vis->size = 0.65f + SnowRandomUnit(random) * 0.35f;

		const float life = 4.5f + SnowRandomUnit(random) * 3.0f;
		vis->lifetime = life;
		vis->lifeleft = life - age;
		vis->creation_time -= age;
		vis->flags |= VF_USES_LIFELEFT | VF_NO_Z_ADJUST | VF_ENHANCED_SNOW;

		vis->velocity = prevailing_wind;
		vis->velocity.x += (SnowRandomUnit(random) - 0.5f) * 6.0f;
		vis->velocity.z += (SnowRandomUnit(random) - 0.5f) * 6.0f;
		vis->velocity.y = -(18.0f + SnowRandomUnit(random) * 28.0f);

		const float flutter_angle = SnowRandomUnit(random) * 6.28318530718f;
		vis->end_pos.x = cosf(flutter_angle);
		vis->end_pos.y = ground_y;
		vis->end_pos.z = sinf(flutter_angle);
		vis->mass = SnowRandomUnit(random) * 6.28318530718f; // flutter phase
		vis->drag = 1.35f + SnowRandomUnit(random) * 2.4f;   // flutter frequency
		vis->custom_handle = (short)(random() & 3); // soft flake silhouette

		const int tint = 205 + (random() % 35);
		vis->lighting_color = GR_RGB16(tint, tint + 3, 255);
		vis->pos += vis->velocity * age;
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
	if (!Render_enhanced_snow)
	{
		DoLegacySnowEffect();
		return;
	}

	if (OBJECT_OUTSIDE(Viewer_object))
	{
		const float intensity = SnowClampIntensity(Weather.snow_intensity_scalar);
		const int desired_flakes = (int)(55.0f + intensity * 65.0f);
		int missing_flakes = desired_flakes - Weather.snowflakes_to_create;
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

	Weather.snowflakes_to_create = 0;
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
