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

#include "pstring.h"
#include "hud.h"
#include "grtext.h"
#include "gamefont.h"
#include "renderer.h"
#include "pserror.h"
#include "player.h"
#include "game.h"
#include "weapon.h"
#include "gametexture.h"
#include "stringtable.h"
#include "ship.h"
#include "config.h"
#include "multi.h"
#include "render.h"

#include <stdio.h>
#include <stdarg.h>
#include <algorithm>

//////////////////////////////////////////////////////////////////////////////
//	Data

//renders the description for the current inventory item
void RenderHUDInventory(tHUDItem* item);

//renders the shield rating for the ship
void RenderHUDShieldValue(tHUDItem* item);

//renders the energy rating for the ship
void RenderHUDEnergyValue(tHUDItem* item);

//	draws the afterburner hud gauge.
void RenderHUDAfterburner(tHUDItem* item);

//	draws the primary weapon current in.
void RenderHUDPrimary(tHUDItem* item);

//	draw secondary weapon current in.
void RenderHUDSecondary(tHUDItem* item);

//	renders ship status.
void RenderHUDShipStatus(tHUDItem* item);

//	renders warnings like system failures or missile locks.
void RenderHUDWarnings(tHUDItem* item);

// render hud countermeasures
void RenderHUDCountermeasures(tHUDItem* item);

//	returns the weapon's icon.
inline int get_weapon_icon(int player, int type)
{
	player_weapon* pw = &Players[player].weapon[type];
	weapon* wpn = GetWeaponFromIndex(player, pw->index);

	if (wpn)
	{
		int bmp = GetTextureBitmap(wpn->icon_handle, 0);
		if (bmp > 0)
			return bmp;
	}

	return HUD_resources.wpn_bmp;
}

void RenderHUDTextFlagsNoFormat(int flags, ddgr_color col, ubyte alpha, int sat_count, int x, int y, char* str);

enum tHUDTextAlign
{
	HUD_TEXT_LEFT,
	HUD_TEXT_CENTER,
	HUD_TEXT_RIGHT
};

static bool HUD_text_pass_active = false;
static int HUD_text_pass_item_type = 0;
static int HUD_top_left_text_anchor_priority = 0;
static bool HUD_top_left_text_anchor_valid = false;
static int HUD_top_left_text_anchor_x = 0;
static int HUD_top_left_text_anchor_y = 0;
static int HUD_top_left_text_anchor_bottom = 0;

void RenderHUDBeginTextPass()
{
	HUD_text_pass_active = true;
	HUD_text_pass_item_type = 0;
	HUD_top_left_text_anchor_priority = 0;
	HUD_top_left_text_anchor_valid = false;
	HUD_top_left_text_anchor_bottom = 0;
}

void RenderHUDEndTextPass()
{
	HUD_text_pass_active = false;
	HUD_text_pass_item_type = 0;
	HUD_top_left_text_anchor_priority = 0;
}

void RenderHUDSetTextPassItemType(int item_type)
{
	HUD_text_pass_item_type = item_type;
}

bool RenderHUDGetTopLeftTextAnchor(int* x, int* y)
{
	if (!HUD_top_left_text_anchor_valid)
		return false;

	if (x)
		*x = HUD_top_left_text_anchor_x;
	if (y)
		*y = HUD_top_left_text_anchor_y;

	return true;
}

bool RenderHUDGetTopLeftTextBottom(int* bottom)
{
	if (!HUD_top_left_text_anchor_valid)
		return false;

	if (bottom)
		*bottom = HUD_top_left_text_anchor_bottom;

	return true;
}

//////////////////////////////////////////////////////////////////////////////
//	Hud item display routines.

void HudDisplayRouter(tHUDItem* item)
{
	grtext_SetAlpha(192);
	grtext_SetFlags(0);

	switch (item->type)
	{
	case HUD_ITEM_PRIMARY:
		RenderHUDPrimary(item);
		break;

	case HUD_ITEM_SECONDARY:
		RenderHUDSecondary(item);
		break;

	case HUD_ITEM_SHIELD:
		RenderHUDShieldValue(item);
		break;

	case HUD_ITEM_ENERGY:
		RenderHUDEnergyValue(item);
		break;

	case HUD_ITEM_AFTERBURNER:
		RenderHUDAfterburner(item);
		break;

	case HUD_ITEM_INVENTORY:
		RenderHUDInventory(item);
		break;

	case HUD_ITEM_SHIPSTATUS:
		RenderHUDShipStatus(item);
		break;

	case HUD_ITEM_WARNINGS:
		RenderHUDWarnings(item);
		break;

	case HUD_ITEM_CNTRMEASURE:
		RenderHUDCountermeasures(item);
		break;

	case HUD_ITEM_CUSTOMTEXT:
		if (item->data.text)
			RenderHUDText(item->color, item->alpha, item->saturation_count, item->x, item->y, item->data.text);
		break;

	case HUD_ITEM_CUSTOMTEXT2:
		if (item->data.text)
			RenderHUDTextFlagsNoFormat(0, item->color, item->alpha, item->saturation_count, 2, 240, item->data.text);
		break;

	case HUD_ITEM_CUSTOMIMAGE:
		break;

	default:
		Int3();
	}
}

//////////////////////////////////////////////////////////////////////////////
//	Hud item functions.

//renders the description for the current inventory item
void RenderHUDInventory(tHUDItem* item)
{
	tInvenList ilist[MAX_UNIQUE_INVEN_ITEMS];
	int cur_sel;
	int count = Players[Player_num].inventory.GetInventoryItemList(ilist, MAX_UNIQUE_INVEN_ITEMS, &cur_sel);
	if (!count)
		return;

	int y;
	//[ISB] uninitialized aaaaa
	float img_w = 0;
	int line_gap = (Hud_aspect_y >= 3.0f) ? 1 : 0;

	if (cur_sel != -1)
	{
		ASSERT(cur_sel < MAX_UNIQUE_INVEN_ITEMS);

		//render currently selected item
		if (ilist[cur_sel].hud_name)
		{
			img_w = bm_w(HUD_resources.arrow_bmp, 0) * HUD_ARROW_SCALE;

			RenderHUDQuad(item->x, item->y + 2, img_w, bm_h(HUD_resources.arrow_bmp, 0) * HUD_ARROW_SCALE, 0, 0, 1, 1, HUD_resources.arrow_bmp, item->alpha, item->saturation_count);

			if (ilist[cur_sel].amount > 1)
				RenderHUDText(item->color, item->alpha, item->saturation_count, item->x + img_w, item->y, "%s (%d)", ilist[cur_sel].hud_name, ilist[cur_sel].amount);
			else
				RenderHUDText(item->color, item->alpha, item->saturation_count, item->x + img_w, item->y, "%s", ilist[cur_sel].hud_name);
		}
	}

	// render non usable list.
	if (cur_sel != -1 && ilist[cur_sel].hud_name)
		y = item->y + RenderHUDGetTextHeight(ilist[cur_sel].hud_name) + 6 + line_gap;
	else
		y = item->y + 16 + line_gap;

	for (int i = 0; i < count; i++)
	{
		if (cur_sel == i)
			continue;
		if (ilist[i].selectable)
			continue;
		if (!ilist[i].hud_name)
			continue;

		if (ilist[i].amount > 1)
			RenderHUDText(item->color, item->alpha, item->saturation_count, item->x + img_w, y, "%s (%d)", ilist[i].hud_name, ilist[i].amount);
		else
			RenderHUDText(item->color, item->alpha, item->saturation_count, item->x + img_w, y, "%s", ilist[i].hud_name);

		y += RenderHUDGetTextHeight(ilist[i].hud_name) + line_gap;
	}

	/*
	Inventory *inventory = &Players[Player_num].inventory;

	if(inventory->Size())	//make sure there is at least one item in the inventory
	{
		int size, y;
		int current_inv_pos = inventory->GetPos();
		float img_w = bm_w(HUD_resources.arrow_bmp,0)*HUD_ARROW_SCALE;
		char *name = inventory->GetPosName();

		if(name && inventory->IsUsable()) {
			RenderHUDQuad(item->x, item->y+2, img_w, bm_h(HUD_resources.arrow_bmp,0)*HUD_ARROW_SCALE, 0,0,1,1,HUD_resources.arrow_bmp,
				item->alpha, item->saturation_count);
			int count = inventory->GetPosCount();
			if(count>1)
				RenderHUDText(item->color, item->alpha, item->saturation_count, item->x+img_w, item->y, "%s (%d)", name,count);
			else
				RenderHUDText(item->color, item->alpha, item->saturation_count, item->x+img_w, item->y, "%s", name);
		}


	// render non usable list.
		inventory->ResetPos();
		size = inventory->Size();
		if (name) {
			y = item->y + RenderHUDGetTextHeight(name) + 6;
		}
		else {
			y = item->y + 16;
		}
		while (size)
		{
			if (!inventory->IsUsable()) {
				name = inventory->GetPosName();
				if(name) {
					int count = inventory->GetPosCount();
					if(count>1) {
						RenderHUDText(item->color, item->alpha, item->saturation_count, item->x+img_w, y, "%s (%d)", name,count);
					}
					else {
						RenderHUDText(item->color, item->alpha, item->saturation_count, item->x+img_w, y, "%s", name);
					}
					y+= RenderHUDGetTextHeight(name);
				}
			}
			inventory->NextPos(true);
			size--;
		}

	// restore inventory position
		inventory->GotoPos(current_inv_pos);
	}
	*/
}


//renders the shield rating for the ship
void RenderHUDShieldValue(tHUDItem* item)
{
	const int SHIELD_NUM_X = 86, SHIELD_NUM_Y = 14;
	const int SHIELD_IMG_X = 21, SHIELD_IMG_Y = 57;
	const int WARNING_IMG_X = 110, WARNING_IMG_Y = 8;
	float alpha_mod = (Objects[Players[Player_num].objnum].shields) / (float)INITIAL_SHIELDS;

	if (alpha_mod > 1.0f)
		alpha_mod = 1.0f;

	if (item->stat & STAT_GRAPHICAL)
	{
		// GRAPHICAL VERSION
		//	display text and warning dot
		int img = (int)ceil((1.0f - alpha_mod - 0.1f) * NUM_SHIELD_GAUGE_FRAMES);
		float grscalex = (item->grscalex != 0.0f) ? item->grscalex : HUD_SHIELD_SCALE;
		float grscaley = (item->grscaley != 0.0f) ? item->grscaley : HUD_SHIELD_SCALE;

		RenderHUDText(item->color, item->alpha, item->saturation_count, item->x + SHIELD_NUM_X, item->y + SHIELD_NUM_Y,
			"%03d", (int)Objects[Players[Player_num].objnum].shields);

		if (alpha_mod <= 0.20f)
		{
			RenderHUDQuad(item->x + WARNING_IMG_X, item->y + WARNING_IMG_Y,
				bm_w(HUD_resources.dot_bmp, 0) * HUD_DOT_SCALE, bm_h(HUD_resources.dot_bmp, 0) * HUD_DOT_SCALE,
				0, 0, 1, 1,
				HUD_resources.dot_bmp, item->alpha, 1);
		}

		if (img < NUM_SHIELD_GAUGE_FRAMES)
		{
			if (img < 0)
				img = 0;
			img = HUD_resources.shield_bmp[img];
			RenderHUDQuad(item->x + SHIELD_IMG_X, item->y + SHIELD_IMG_Y,
				bm_w(img, 0) * grscalex, bm_h(img, 0) * grscaley,
				0, 0, 1, 1, img,
				item->alpha, item->saturation_count);
		}
	}
	else if (item->stat & STAT_SPECIAL)
	{
		RenderHUDText(item->color, item->alpha, item->saturation_count, item->x, item->y,
			"%03d", (int)Objects[Players[Player_num].objnum].shields);

		if (alpha_mod <= 0.20f)
		{
			RenderHUDQuad(item->x + 30, item->y - 10,
				bm_w(HUD_resources.dot_bmp, 0) * HUD_DOT_SCALE, bm_h(HUD_resources.dot_bmp, 0) * HUD_DOT_SCALE,
				0, 0, 1, 1,
				HUD_resources.dot_bmp, item->alpha, 1);
		}
	}
	else
	{
		// TEXT VERSION
		RenderHUDText(item->tcolor, item->alpha, (alpha_mod <= .20f) ? item->saturation_count + 1 : item->saturation_count,
			item->tx, item->ty, "%s: %03d", TXT_HUD_SHIELDS, (int)Objects[Players[Player_num].objnum].shields);
	}
}


//renders the energy rating for the ship
void RenderHUDEnergyValue(tHUDItem* item)
{
	float normalized_energy = (float)Players[Player_num].energy / (float)INITIAL_ENERGY;

	// cap off energy to 1.0 normalized 
	if (normalized_energy > 1.0f)
		normalized_energy = 1.0f;

	//	display text and warning dot
	if (item->stat & STAT_GRAPHICAL)
	{
		const int ENERGY_NUM_X = 15, ENERGY_NUM_Y = 14;
		//	int ENERGY_IMG_X = 3, ENERGY_IMG_Y = 74;
		//	int ENERGY_IMG_X2 = 119;
		const int WARNING_IMG_X = 6, WARNING_IMG_Y = 8;
		float grscalex = (item->grscalex != 0.0f) ? item->grscalex : HUD_ENERGY_SCALE;
		float grscaley = (item->grscaley != 0.0f) ? item->grscaley : HUD_ENERGY_SCALE;

		float img_w = bm_w(HUD_resources.energy_bmp, 0) * grscalex;
		float img_h = bm_h(HUD_resources.energy_bmp, 0) * grscaley;
		int img_energy_h;

		RenderHUDText(item->color, item->alpha, item->saturation_count, item->x + ENERGY_NUM_X, item->y + ENERGY_NUM_Y,
			"%03d", (int)Players[Player_num].energy);

		if (normalized_energy <= 0.20f)
		{
			RenderHUDQuad(item->x + WARNING_IMG_X, item->y + WARNING_IMG_Y,
				bm_w(HUD_resources.dot_bmp, 0) * HUD_DOT_SCALE, bm_h(HUD_resources.dot_bmp, 0) * HUD_DOT_SCALE,
				0, 0, 1, 1,
				HUD_resources.dot_bmp, item->alpha, 1);
		}

		img_energy_h = (int)floor((normalized_energy * img_h) + 0.5f);

		//	draw energy gauge, showing how much energy below 100% you have. draw energy spent faded.
		RenderHUDQuad(item->x + item->xa, item->y + item->ya,
			img_w, img_h - img_energy_h, 0, 0, 1.0, 1.0f - normalized_energy,
			HUD_resources.energy_bmp, item->alpha / 4, 0);

		RenderHUDQuad(item->x + item->xb - img_w, item->y + item->yb,
			img_w, img_h - img_energy_h, 1.0, 0, 0, 1.0f - normalized_energy,
			HUD_resources.energy_bmp, item->alpha / 4, 0);

		RenderHUDQuad(item->x + item->xa, item->y + item->ya + (int)(img_h - img_energy_h),
			img_w, img_energy_h,
			0, 1.0f - normalized_energy, 1, 1,
			HUD_resources.energy_bmp, item->alpha, item->saturation_count);

		RenderHUDQuad(item->x + item->xb - img_w, item->y + item->yb + (int)(img_h - img_energy_h),
			img_w, img_energy_h,
			1, 1.0f - normalized_energy, 0, 1,
			HUD_resources.energy_bmp, item->alpha, item->saturation_count);
	}
	else if (item->stat & STAT_SPECIAL)
	{
		RenderHUDText(item->color, item->alpha, item->saturation_count, item->x, item->y,
			"%03d", (int)Players[Player_num].energy);

		if (normalized_energy <= 0.20f)
		{
			RenderHUDQuad(item->x - 10, item->y - 10,
				bm_w(HUD_resources.dot_bmp, 0) * HUD_DOT_SCALE, bm_h(HUD_resources.dot_bmp, 0) * HUD_DOT_SCALE,
				0, 0, 1, 1,
				HUD_resources.dot_bmp, item->alpha, 1);
		}
	}
	else
	{
		// TEXT VERSION
		RenderHUDText(item->tcolor, item->alpha, (normalized_energy <= .20f) ? item->saturation_count + 1 : item->saturation_count,
			item->tx, item->ty, "%s: %03d", TXT_HUD_ENERGY, (int)Players[Player_num].energy);
	}
}


//	draws the afterburner hud gauge.
void RenderHUDAfterburner(tHUDItem* item)
{
	float val = (Players[Player_num].afterburn_time_left / AFTERBURN_TIME);
	char str[8];

	if (item->stat & STAT_GRAPHICAL)
	{
		const int BURNER_NUM_X = 24;
		float grscalex = (item->grscalex != 0.0f) ? item->grscalex : HUD_BURN_SCALE;
		float grscaley = (item->grscaley != 0.0f) ? item->grscaley : HUD_BURN_SCALE;
		float img_w = bm_w(HUD_resources.afterburn_bmp, 0) * grscalex;
		float img_h = bm_h(HUD_resources.afterburn_bmp, 0) * grscaley;
		int img_burn_w = (int)floor((val * img_w) + 0.5f);

		//	draw afterburn spent.
		RenderHUDQuad(item->x + item->xa + img_burn_w, item->y + item->ya,
			img_w - img_burn_w, img_h, val, 0, 1, 1,
			HUD_resources.afterburn_bmp, item->alpha / 4, 0);

		//	draw afterburn left
		RenderHUDQuad(item->x + item->xa, item->y + item->ya,
			img_burn_w, img_h, 0, 0, val, 1,
			HUD_resources.afterburn_bmp, item->alpha, item->saturation_count);

		sprintf(str, "%d%%", (int)(val * 100.0f));
		RenderHUDText(item->color, item->alpha, item->saturation_count, item->x + BURNER_NUM_X, item->y, str);
	}
	else if (item->stat & STAT_SPECIAL)
	{
		sprintf(str, "%d%%", (int)(val * 100.0f));
		RenderHUDText(item->color, item->alpha, item->saturation_count, item->x, item->y, str);
	}
	else
	{
		// TEXT VERSION
		RenderHUDText(item->tcolor, item->alpha, (val <= .30f) ? item->saturation_count + 1 : item->saturation_count,
			item->tx, item->ty, "%s: %d%%", TXT_HUD_AFTERBURNER, (int)(val * 100.0f));
	}
}


//	draws the primary weapon current in.
void RenderHUDPrimary(tHUDItem* item)
{
	int index = Players[Player_num].weapon[PW_PRIMARY].index;
	ship* ship = &Ships[Players[Player_num].ship_index];
	otype_wb_info* wb = &ship->static_wb[index];
	const char* text = TXT(Static_weapon_names_msg[index]);
	float txt_w = (int)(grtext_GetLineWidth(text) / Hud_aspect_x) + 4;
	char ammo_string[10];

	if (!wb)
		return;

	if (wb->ammo_usage)
	{
		int ammo = Players[Player_num].weapon_ammo[index];
		if (ship->fire_flags[index] & SFF_TENTHS)
			sprintf(ammo_string, "%d.%d", ammo / 10, ammo % 10);
		else
			sprintf(ammo_string, "%d", ammo);
	}

	if (item->stat & STAT_GRAPHICAL)
	{
		const int WPN_INFO_W = 100;
		float grscalex = (item->grscalex != 0.0f) ? item->grscalex : HUD_WPN_SCALE;
		float grscaley = (item->grscaley != 0.0f) ? item->grscaley : HUD_WPN_SCALE;

		float img_w = bm_w(HUD_resources.wpn_bmp, 0) * grscalex;
		int icon;

		int y = item->y;

		if (wb->ammo_usage) {
			int w2 = (int)(grtext_GetLineWidth("00000") / Hud_aspect_x) + 2;
			RenderHUDText(item->color, item->alpha, item->saturation_count, item->x + WPN_INFO_W - img_w - w2, y + 13,
				ammo_string);
		}
		else
			y += 13 / 2;		//If just one line, move it down

		RenderHUDText(item->color, item->alpha, item->saturation_count, item->x + WPN_INFO_W - img_w - txt_w, y,
			"%s", text);

		icon = get_weapon_icon(Player_num, PW_PRIMARY);

		if (icon != HUD_resources.wpn_bmp)
		{
			RenderHUDQuad(item->x + WPN_INFO_W - img_w, item->y, img_w, bm_h(icon, 0) * grscaley, 0, 0, 1, 1,
				icon, item->alpha, item->saturation_count);
		}
	}
	else if (item->stat & STAT_SPECIAL)
	{
	}
	else
	{
		// TEXT VERSION
		if (wb->ammo_usage)
		{
			RenderHUDText(item->tcolor, item->alpha, item->saturation_count, item->tx, item->ty,
				"%s %s", text, ammo_string);
		}
		else
		{
			RenderHUDText(item->tcolor, item->alpha, item->saturation_count, item->tx, item->ty,
				"%s", text);
		}
	}
}


//	draw secondary weapon current in.
void RenderHUDSecondary(tHUDItem* item)
{
	int index = Players[Player_num].weapon[PW_SECONDARY].index;
	ship* ship = &Ships[Players[Player_num].ship_index];
	otype_wb_info* wb = &ship->static_wb[index];
	weapon* wpn = GetWeaponFromIndex(Player_num, index);
	const char* text = TXT(Static_weapon_names_msg[index]);
	float txt_w = (int)(grtext_GetLineWidth(text) / Hud_aspect_x) + 2;

	if (item->stat & STAT_GRAPHICAL)
	{
		int icon;
		float grscalex = (item->grscalex != 0.0f) ? item->grscalex : HUD_WPN_SCALE;
		float grscaley = (item->grscaley != 0.0f) ? item->grscaley : HUD_WPN_SCALE;

		float img_w = bm_w(HUD_resources.wpn_bmp, 0) * grscalex;

		RenderHUDText(item->color, item->alpha, item->saturation_count, item->x + img_w + 2, item->y,
			"%s", text);
		if (wb->ammo_usage)
		{
			RenderHUDText(item->color, item->alpha, item->saturation_count, item->x + img_w + 16, item->y + 13,
				"%d", Players[Player_num].weapon_ammo[Players[Player_num].weapon[PW_SECONDARY].index]);
		}

		icon = get_weapon_icon(Player_num, PW_SECONDARY);

		if (icon != HUD_resources.wpn_bmp)
		{
			RenderHUDQuad(item->x, item->y, img_w, bm_h(icon, 0) * grscaley, 0, 0, 1, 1,
				icon, item->alpha, item->saturation_count);
		}
	}
	else if (item->stat & STAT_SPECIAL)
	{

	}
	else
	{
		//	TEXT VERSION
		RenderHUDText(item->tcolor, item->alpha, item->saturation_count, item->tx, item->ty,
			"%s %d", text, Players[Player_num].weapon_ammo[Players[Player_num].weapon[PW_SECONDARY].index]);
	}
}


void RenderHUDShipStatus(tHUDItem* item)
{
	float clk_time_frame, inv_time_frame;
	ubyte clk_alpha, inv_alpha;

	//	render text status
	if (Objects[Players[Player_num].objnum].effect_info->type_flags & EF_CLOAKED)
	{
		clk_time_frame = Objects[Players[Player_num].objnum].effect_info->cloak_time;
		if (clk_time_frame < HUD_CLOAKEND_TIME)
			clk_alpha = 128 - 127 * FixCos(65536.0f * (clk_time_frame - (int)clk_time_frame));
		else
			clk_alpha = 0;
	}
	if (Players[Player_num].flags & PLAYER_FLAGS_INVULNERABLE)
	{
		// do invulnerablity animation.
		inv_time_frame = (Gametime - (int)Gametime);
		inv_alpha = (255 * inv_time_frame);
	}


	if (item->stat & STAT_GRAPHICAL)
	{
		const int STATUS_IMG_X = 0, STATUS_IMG_Y = 68;
		const int STATUS_TXT_X = 8;
		const int STATUS_TXT_Y = -1, STATUS_TXT_Y2 = 9;
		float grscalex = (item->grscalex != 0.0f) ? item->grscalex : HUD_SHIP_SCALE;
		float grscaley = (item->grscaley != 0.0f) ? item->grscaley : HUD_SHIP_SCALE;

		int x = item->x + STATUS_IMG_X;
		int y = item->y + STATUS_IMG_Y;
		int w = bm_w(HUD_resources.ship_bmp, 0) * grscalex;
		int h = bm_w(HUD_resources.ship_bmp, 0) * grscaley;

		if (Objects[Players[Player_num].objnum].effect_info->type_flags & EF_CLOAKED)
		{
			RenderHUDQuad(x, y, w, h, 0, 0, 1, 1, HUD_resources.ship_bmp, clk_alpha, item->saturation_count);
			RenderHUDText(item->color, 255 - clk_alpha, item->saturation_count, item->x + STATUS_TXT_X, item->y + STATUS_TXT_Y2, TXT_CLK);
		}
		else
		{
			// non cloaked ship
			RenderHUDQuad(x, y, w, h, 0, 0, 1, 1, HUD_resources.ship_bmp, item->alpha, item->saturation_count);
		}

		if (Players[Player_num].flags & PLAYER_FLAGS_INVULNERABLE)
		{
			x = x - (w * inv_time_frame * 0.5f);
			y = y - (h * inv_time_frame * 0.5f);

			RenderHUDQuad(x, y, (1.0f + inv_time_frame) * w, (1.0f + inv_time_frame) * h, 0, 0, 1, 1,
				HUD_resources.invpulse_bmp, inv_alpha, item->saturation_count);
			RenderHUDText(item->color, item->alpha, item->saturation_count, item->x + STATUS_TXT_X + 2, item->y + STATUS_TXT_Y,
				TXT_INV);
		}
	}
	else if (item->stat & STAT_SPECIAL)
	{

	}
	else
	{
		if (Players[Player_num].flags & PLAYER_FLAGS_INVULNERABLE)
			RenderHUDText(item->tcolor, 255 - clk_alpha, item->saturation_count, item->tx, item->ty, TXT_HUD_INVULN);
		if (Objects[Players[Player_num].objnum].effect_info->type_flags & EF_CLOAKED)
			RenderHUDText(item->tcolor, 255 - clk_alpha, item->saturation_count, item->tx, item->ty + 12, TXT_HUD_CLOAKED);
	}
}


//	renders warnings like system failures or missile locks.
void RenderHUDWarnings(tHUDItem* item)
{
	object* playerobj = &Objects[Players[Player_num].objnum];

	if (item->stat & STAT_GRAPHICAL)
	{
		const int LOCK_IMG_X = 8, LOCK_IMG_Y = 2;
		const int ANTIGRAV_X = 8, ANTIGRAV_Y = 64;

		float img_w, img_h;
		float grscalex = (item->grscalex != 0.0f) ? item->grscalex : HUD_LOCK_SCALE;
		float grscaley = (item->grscaley != 0.0f) ? item->grscaley : HUD_LOCK_SCALE;


		if (Players[Player_num].last_homing_warning_sound_time == Gametime && Gametime > 0.0f)
		{
			//	draw graphical rep of missile lock warning.
			img_w = bm_w(HUD_resources.lock_bmp[0], 0) * grscalex;
			img_h = bm_h(HUD_resources.lock_bmp[1], 0) * grscaley;

			RenderHUDQuad(item->x + LOCK_IMG_X, item->y + LOCK_IMG_Y,
				img_w, img_h, 0, 0, 1, 1, HUD_resources.lock_bmp[0], item->alpha, item->saturation_count);
			RenderHUDQuad(item->x + LOCK_IMG_X + img_w, item->y + LOCK_IMG_Y,
				img_w, img_h, 0, 0, 1, 1, HUD_resources.lock_bmp[1], item->alpha, item->saturation_count);
		}

		if (playerobj->mtype.phys_info.flags & PF_GRAVITY)
		{
			//	draw graphical rep of missile lock warning.
			img_w = bm_w(HUD_resources.antigrav_bmp[0], 0) * grscalex;
			img_h = bm_h(HUD_resources.antigrav_bmp[1], 0) * grscaley;

			RenderHUDQuad(item->x + ANTIGRAV_X, item->y + ANTIGRAV_Y,
				img_w, img_h, 0, 0, 1, 1, HUD_resources.antigrav_bmp[0], item->alpha, item->saturation_count);
			RenderHUDQuad(item->x + ANTIGRAV_X + img_w, item->y + ANTIGRAV_Y,
				img_w, img_h, 0, 0, 1, 1, HUD_resources.antigrav_bmp[1], item->alpha, item->saturation_count);
		}
	}
	else
	{
		if (Players[Player_num].last_homing_warning_sound_time == Gametime)
			RenderHUDText(item->tcolor, item->alpha, item->saturation_count, item->tx, item->ty, TXT_HUDITM_LOCK);
		if (playerobj->mtype.phys_info.flags & PF_GRAVITY)
			RenderHUDText(item->tcolor, item->alpha, item->saturation_count, item->tx, item->ty + 12, TXT_HUDITM_ANTIGRAV);
	}
}


// render hud countermeasures
void RenderHUDCountermeasures(tHUDItem* item)
{
	tInvenList ilist[MAX_UNIQUE_INVEN_ITEMS];
	int cur_sel;
	int count = Players[Player_num].counter_measures.GetInventoryItemList(ilist, MAX_UNIQUE_INVEN_ITEMS, &cur_sel);
	if (!count)
		return;

	if (cur_sel != -1)
	{
		ASSERT(cur_sel < MAX_UNIQUE_INVEN_ITEMS);

		//render currently selected item
		if (ilist[cur_sel].hud_name)
		{
			if (item->stat & STAT_GRAPHICAL)
				RenderHUDText(item->color, item->alpha, item->saturation_count, item->x, item->y, "%s %d", ilist[cur_sel].hud_name, ilist[cur_sel].amount);
			else
				RenderHUDText(item->color, item->alpha, item->saturation_count, item->tx, item->ty, "%s %d", ilist[cur_sel].hud_name, ilist[cur_sel].amount);
		}
	}
}


//////////////////////////////////////////////////////////////////////////////
//	Hud rendering functions

static int HUDTextToScreenX(int x)
{
	return HUD_X(x);
}

static int HUDTextToScreenY(int y)
{
	return HUD_Y(y);
}

static int HUDTextScreenWidth(const char* string)
{
	return grtext_GetTextLineWidth(string);
}

static int HUDTextScreenHeight(const char* string)
{
	return grtext_GetHeight(string);
}

static int HUDTextVirtualWidth(const char* string)
{
	if (Hud_aspect_x <= 0.0f)
		return HUDTextScreenWidth(string);

	return (int)((HUDTextScreenWidth(string) / Hud_aspect_x) + 0.5f);
}

static int HUDTextVirtualHeight(const char* string)
{
	if (Hud_aspect_y <= 0.0f)
		return HUDTextScreenHeight(string);

	return (int)((HUDTextScreenHeight(string) / Hud_aspect_y) + 0.5f);
}

static void HUDTextApplyStyle(ddgr_color col, ubyte alpha, int sat_count)
{
	grtext_SetAlpha(alpha);
	grtext_SetFlags(sat_count ? GRTEXTFLAG_SATURATE : 0);
	grtext_SetColor(col);
}

static int HUDTextAnchorPriority()
{
	switch (HUD_text_pass_item_type)
	{
	case HUD_ITEM_PRIMARY:
		return 4;
	case HUD_ITEM_INVENTORY:
		return 3;
	case HUD_ITEM_SECONDARY:
		return 2;
	case HUD_ITEM_CNTRMEASURE:
		return 1;
	default:
		return 0;
	}
}

static void HUDTextRecordTopLeftAnchor(tHUDTextAlign align, int x, int y, int screen_bottom)
{
	if (!HUD_text_pass_active || align != HUD_TEXT_LEFT)
		return;

	if (x > (int)(DEFAULT_HUD_WIDTH / 2) || y > (int)(DEFAULT_HUD_HEIGHT / 2))
		return;

	int priority = HUDTextAnchorPriority();
	if (!priority)
		return;

	if (!HUD_top_left_text_anchor_valid || priority > HUD_top_left_text_anchor_priority ||
		(priority == HUD_top_left_text_anchor_priority &&
			(y < HUD_top_left_text_anchor_y || (y == HUD_top_left_text_anchor_y && x < HUD_top_left_text_anchor_x))))
	{
		HUD_top_left_text_anchor_valid = true;
		HUD_top_left_text_anchor_priority = priority;
		HUD_top_left_text_anchor_x = x;
		HUD_top_left_text_anchor_y = y;
		HUD_top_left_text_anchor_bottom = screen_bottom;
	}
	else if (priority == HUD_top_left_text_anchor_priority)
	{
		HUD_top_left_text_anchor_bottom = std::max(HUD_top_left_text_anchor_bottom, screen_bottom);
	}
}

static void RenderHUDTextNoFormatAligned(ddgr_color col, ubyte alpha, int sat_count, int x, int y, tHUDTextAlign align, const char* str)
{
	HUDTextApplyStyle(col, alpha, sat_count);

	int draw_x = HUDTextToScreenX(x);
	int draw_y = HUDTextToScreenY(y);

	HUDTextRecordTopLeftAnchor(align, x, y, draw_y + HUDTextScreenHeight(str));

	if (align == HUD_TEXT_CENTER)
		draw_x -= HUDTextScreenWidth(str) / 2;
	else if (align == HUD_TEXT_RIGHT)
		draw_x -= HUDTextScreenWidth(str);

	draw_x = std::max(0, draw_x);

	for (int i = 0; i < sat_count + 1; i++)
		grtext_Puts(draw_x, draw_y, str);
}

// returns scaled line width
int RenderHUDGetTextLineWidth(char* string)
{
	return HUDTextVirtualWidth(string);
}

// returns scaled text height
int RenderHUDGetTextHeight(char* string)
{
	return HUDTextVirtualHeight(string);
}

void RenderHUDQuad(int x, int y, int w, int h, float u0, float v0, float u1, float v1, int bm, ubyte alpha, int sat_count)
{
	rend_SetZBufferState(0);
	rend_SetTextureType(TT_LINEAR);
	rend_SetAlphaValue(alpha);
	rend_SetLighting(LS_NONE);
	rend_SetWrapType(WT_CLAMP);

	if (sat_count)
		rend_SetAlphaType(AT_SATURATE_TEXTURE);
	else
		rend_SetAlphaType(AT_CONSTANT_TEXTURE);


	x = HUD_X(x);
	y = HUD_Y(y);
	w = HUD_X(w);
	h = HUD_Y(h);

	for (int i = 0; i < sat_count + 1; i++)
		rend_DrawScaledBitmap(x, y, x + w, y + h, bm, u0, v0, u1, v1);

	rend_SetZBufferState(1);
}


//	renders text, scaled, alphaed, saturated, 
void RenderHUDText(ddgr_color col, ubyte alpha, int sat_count, int x, int y, char* fmt, ...)
{
	va_list arglist;
	char buf[128];

	va_start(arglist, fmt);
	Pvsprintf(buf, 128, fmt, arglist);
	va_end(arglist);

	RenderHUDTextFlagsNoFormat(0, col, alpha, sat_count, x, y, buf);
}


//	renders text, scaled, alphaed, saturated, 
void RenderHUDTextFlagsNoFormat(int flags, ddgr_color col, ubyte alpha, int sat_count, int x, int y, char* str)
{
	tHUDTextAlign align = (flags & HUDTEXT_CENTERED) ? HUD_TEXT_CENTER : HUD_TEXT_LEFT;
	if (align == HUD_TEXT_CENTER)
		x = (int)(DEFAULT_HUD_WIDTH / 2);

	RenderHUDTextNoFormatAligned(col, alpha, sat_count, x, y, align, str);
}


//	renders screen-space text, alphaed, saturated.  Some reticle and training overlays pass screen coordinates here.
void RenderHUDTextFlags(int flags, ddgr_color col, ubyte alpha, int sat_count, int x, int y, char* fmt, ...)
{
	va_list arglist;
	char buf[128];

	va_start(arglist, fmt);
	Pvsprintf(buf, 128, fmt, arglist);
	va_end(arglist);

	HUDTextApplyStyle(col, alpha, sat_count);

	for (int i = 0; i < sat_count + 1; i++)
	{
		if (flags & HUDTEXT_CENTERED)
			grtext_CenteredPrintf(0, y, buf);
		else
			grtext_Puts(x, y, buf);
	}
}

static void RenderHUDRightAlignedText(ddgr_color col, ubyte alpha, int sat_count, int right_x, int y, char* str)
{
	RenderHUDTextNoFormatAligned(col, alpha, sat_count, right_x, y, HUD_TEXT_RIGHT, str);
}

//Show the score
void RenderHUDScore(tHUDItem* item)
{
	char buf[100];

	if (Game_mode & GM_MULTI)
	{
		if (!(Netgame.flags & NF_USE_ROBOTS))
			return;
	}

	sprintf(buf, "%s: %d", TXT_SCORE, Players[Player_num].score);
	int right_x = item->x;
	int y = item->y;
	int anchor_x, anchor_y;
	if (RenderHUDGetTopLeftTextAnchor(&anchor_x, &anchor_y))
	{
		right_x = (int)DEFAULT_HUD_WIDTH - anchor_x;
		y = anchor_y;
	}
	RenderHUDRightAlignedText(item->color, HUD_ALPHA, 0, right_x, y, buf);

	if (Score_added_timer > 0.0)
	{
		int text_height = RenderHUDGetTextHeight(buf);
		sprintf(buf, "%d", Score_added);
		ubyte alpha = std::min((double)HUD_ALPHA, HUD_ALPHA * 4 * Score_added_timer / SCORE_ADDED_TIME);
		RenderHUDRightAlignedText(item->color, alpha, 0, right_x, y + text_height, buf);
		Score_added_timer -= Frametime;
	}
}

extern float Osiris_TimerTimeRemaining(int handle);

//Show the timer
void RenderHUDTimer(tHUDItem* item)
{
	float time;
	int min, secs;
	char buf[100];

	time = Osiris_TimerTimeRemaining(item->data.timer_handle);

	//If no more time, kill item
	if (time < 0)
	{
		item->stat = 0;
		return;
	}

	//round up
	time = ceil(time);

	min = time / 60;
	secs = time - (60 * min);

	sprintf(buf, "T-%d:%02d", min, secs);

	int h = grfont_GetHeight(HUD_FONT);
	RenderHUDTextFlagsNoFormat(HUDTEXT_CENTERED, item->color, HUD_ALPHA, 0, 0, h * 4, buf);
}


////////////////////////////////////////////////////////////////////////////////////////	
void tDirtyRect::fill(ddgr_color col)
{
	int i;
	for (i = 2; i >= 0; i--)
	{
		rend_SetAlphaValue(255);
		if (r[i].l >= 0 && r[i].r >= 0)
			rend_FillRect(col, r[i].l, r[i].t, r[i].r, r[i].b);

		if (i > 0)
		{
			r[i].l = r[i - 1].l;
			r[i].t = r[i - 1].t;
			r[i].r = r[i - 1].r;
			r[i].b = r[i - 1].b;
		}
	}
	r[0].l = -1;
	r[0].t = -1;
	r[0].r = -1;
	r[0].b = -1;
}


void tDirtyRect::reset()
{
	int i;
	for (i = 0; i < 3; i++)
	{
		r[i].l = -1; r[i].t = -1; r[i].b = -1; r[i].r = -1;
	}
}
