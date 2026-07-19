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
#include "difficulty.h"

difficulty_profile Singleplayer_difficulty = {
	DIFFICULTY_ROOKIE,
	DIFFICULTY_ROOKIE,
	DIFFICULTY_ROOKIE,
	DIFFICULTY_ROOKIE
};

difficulty_profile Multiplayer_difficulty = {
	DIFFICULTY_HOTSHOT,
	DIFFICULTY_HOTSHOT,
	DIFFICULTY_HOTSHOT,
	DIFFICULTY_HOTSHOT
};

static ubyte ClampDifficulty(ubyte level)
{
	return level < MAX_DIFFICULTY_LEVELS ? level : DIFFICULTY_HOTSHOT;
}

void DifficultySetSingleplayer(ubyte level)
{
	level = ClampDifficulty(level);
	Singleplayer_difficulty.enemy_ai = level;
	Singleplayer_difficulty.enemy_speed = level;
	Singleplayer_difficulty.enemy_hp = level;
	Singleplayer_difficulty.resources = level;
}

void DifficultySetSingleplayerProfile(const difficulty_profile &profile)
{
	Singleplayer_difficulty.enemy_ai = ClampDifficulty(profile.enemy_ai);
	Singleplayer_difficulty.enemy_speed = ClampDifficulty(profile.enemy_speed);
	Singleplayer_difficulty.enemy_hp = ClampDifficulty(profile.enemy_hp);
	Singleplayer_difficulty.resources = ClampDifficulty(profile.resources);
}

void DifficultySetMultiplayer(ubyte level)
{
	level = ClampDifficulty(level);
	Multiplayer_difficulty.enemy_ai = level;
	Multiplayer_difficulty.enemy_speed = level;
	Multiplayer_difficulty.enemy_hp = level;
	Multiplayer_difficulty.resources = level;
}

void DifficultySetMultiplayerProfile(const difficulty_profile &profile)
{
	Multiplayer_difficulty.enemy_ai = ClampDifficulty(profile.enemy_ai);
	Multiplayer_difficulty.enemy_speed = ClampDifficulty(profile.enemy_speed);
	Multiplayer_difficulty.enemy_hp = ClampDifficulty(profile.enemy_hp);
	Multiplayer_difficulty.resources = ClampDifficulty(profile.resources);
}

bool DifficultyProfileIsUniform(const difficulty_profile &profile)
{
	return profile.enemy_ai == profile.enemy_speed &&
		profile.enemy_ai == profile.enemy_hp &&
		profile.enemy_ai == profile.resources;
}

ubyte DifficultyProfileLegacyLevel(const difficulty_profile &profile)
{
	return ClampDifficulty(profile.enemy_hp);
}

// Notes:
//
// 1.  Don't mess with target leading (other than algorithm type) as it will make robots
//     turn strangely (or at least never scale it up beyond 1.0)

float Diff_ai_dodge_percent[5] = {0.04f, 0.10f, 1.00f, 1.00f, 1.50f};
float Diff_ai_dodge_speed[5]   = {0.20f, 0.30f, 1.00f, 1.25f, 1.50f};
float Diff_ai_speed[5]         = {0.70f, 0.80f, 1.00f, 1.10f, 1.20f};
float Diff_ai_rotspeed[5]      = {0.70f, 0.80f, 1.00f, 1.10f, 1.20f};
float Diff_ai_circle_dist[5]   = {1.10f, 1.00f, 1.00f, 1.00f, 1.00f};
float Diff_ai_vis_dist[5]      = {0.80f, 0.90f, 1.00f, 1.10f, 1.20f};
float Diff_player_damage[5]    = {0.30f, 0.60f, 1.00f, 1.50f, 2.00f};
float Diff_ai_weapon_speed[5]  = {0.50f, 0.75f, 1.00f, 1.20f, 1.40f};
float Diff_homing_strength[5]  = {0.20f, 0.70f, 1.00f, 1.20f, 1.40f};
float Diff_robot_damage[5]     = {2.75f, 1.50f, 1.00f, 0.80f, 0.60f};
float Diff_general_scalar[5]   = {2.50f, 1.75f, 1.00f, 0.75f, 0.50f};
float Diff_general_inv_scalar[5] = {0.50f, 0.75f, 1.00f, 1.75f, 2.50f};
float Diff_shield_energy_scalar[5] = {2.25f, 1.5f, 1.0f, 0.75f, 0.5f};
float Diff_ai_turret_speed[5] = {0.6f, 0.7f, 1.0f, 1.0f, 1.0f};
float Diff_ai_min_fire_spread[5] = {.30f, .15f, 0.0f, 0.0f, 0.0f};
