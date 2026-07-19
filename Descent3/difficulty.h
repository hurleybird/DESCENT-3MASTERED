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
#ifndef _DIFFICULTY_H_
#define _DIFFICULTY_H_

#include "game.h"
#include "pilot.h"
#include "difficulty_external.h"

extern ubyte ingame_difficulty;

extern difficulty_profile Singleplayer_difficulty;
extern difficulty_profile Multiplayer_difficulty;

void DifficultySetSingleplayer(ubyte level);
void DifficultySetSingleplayerProfile(const difficulty_profile &profile);
void DifficultySetMultiplayer(ubyte level);
void DifficultySetMultiplayerProfile(const difficulty_profile &profile);
bool DifficultyProfileIsUniform(const difficulty_profile &profile);
ubyte DifficultyProfileLegacyLevel(const difficulty_profile &profile);

// Single-player continues to use the pilot's monolithic setting.  A uniform
// multiplayer profile therefore executes precisely the same table lookups as
// before, while enhanced games can select the established dimensions
// independently.
#define DIFF_AI_LEVEL (((Game_mode & GM_MULTI)) ? Multiplayer_difficulty.enemy_ai : Singleplayer_difficulty.enemy_ai)
#define DIFF_SPEED_LEVEL (((Game_mode & GM_MULTI)) ? Multiplayer_difficulty.enemy_speed : Singleplayer_difficulty.enemy_speed)
#define DIFF_HP_LEVEL (((Game_mode & GM_MULTI)) ? Multiplayer_difficulty.enemy_hp : Singleplayer_difficulty.enemy_hp)
#define DIFF_RESOURCES_LEVEL (((Game_mode & GM_MULTI)) ? Multiplayer_difficulty.resources : Singleplayer_difficulty.resources)

// The scripting API has only one legacy difficulty value.  Enemy toughness is
// the least surprising representative for custom profiles (notably for bosses).
#define DIFF_LEVEL DIFF_HP_LEVEL

extern float Diff_ai_dodge_percent[5]; //
extern float Diff_ai_dodge_speed[5]; //
extern float Diff_ai_speed[5]; //
extern float Diff_ai_rotspeed[5]; //
extern float Diff_ai_circle_dist[5]; //
extern float Diff_ai_vis_dist[5]; //
extern float Diff_player_damage[5]; //
extern float Diff_ai_weapon_speed[5]; //
extern float Diff_homing_strength[5]; //
extern float Diff_robot_damage[5]; //
extern float Diff_general_scalar[5];	// Trainee = 2.50f HotShot = 1.0f
extern float Diff_general_inv_scalar[5];// Trainee = 0.50f HotShot = 1.0f
extern float Diff_shield_energy_scalar[5];	
extern float Diff_ai_turret_speed[5];
extern float Diff_ai_min_fire_spread[5];
#endif
