// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_resource.c
 *
 * Functions used to predict the effectiveness of a specific resource state
 *****************************************************************************/

#include "ai_main.h"
#include "ai_vars.h"
#include "ai_resource.h"

#include "ai_accuracy.h"
#include "ai_client.h"
#include "ai_item.h"
#include "ai_weapon.h"


// The maximum ammo a player can hold.  This is from Add_Ammo() in g_item.c
#define AMMO_MAX 200

// Estimations of how valuable different items are versus not picking the item up
float           item_value[MAX_ITEM_TYPES];

// Respawn times
//
// NOTE: These were copied from g_items.c.  Why aren't they in a header file?
#define	RESPAWN_ARMOR		25
#define	RESPAWN_HEALTH		35
#define	RESPAWN_AMMO		40
#define	RESPAWN_HOLDABLE	60
#define	RESPAWN_MEGAHEALTH	35
#define	RESPAWN_POWERUP		120

// Picking up some items may require post processing that should only be done once
#define RS_ITEM_PICKUP 		0x01	// An item could be picked up
#define RS_ITEM_HEALTH		0x02	// Health or armor value changed
#define RS_ITEM_WEAPON		0x04	// Weapon or ammo value changed
#define RS_ITEM_HEALTHMOD	0x08	// Health modification state recomputation needed
#define RS_ITEM_DAMAGEMOD	0x10	// Damage modification state recomputation needed

// The different kinds of typical player states
enum
{
	DEFAULT_PLAYER_SPAWNED,		// A player who just spawned in
	DEFAULT_PLAYER_POWERED,		// A player who is really powered up
	DEFAULT_PLAYER_WOUNDED,		// A player who is pretty wounded from a battle
	DEFAULT_PLAYER_AVERAGE,		// A typical player in the game-- not too weak or too strong

	NUM_DEFAULT_PLAYERS
};

// A player will almost never live longer than this many seconds
#define LIFE_EXPECTANCY_MAX 600.0

// Assume players will never need to pick up an item faster than this often
#define PICKUP_TIME_MINIMUM 5.0

// What percent of players fit into each catagory
//
// NOTE: These should total 1.0
float           default_distribution[] = {
	0.20,						// DEFAULT_PLAYER_SPAWNED
	0.30,						// DEFAULT_PLAYER_POWERED
	0.10,						// DEFAULT_PLAYER_WOUNDED
	0.40,						// DEFAULT_PLAYER_AVERAGE
};

// Starting health of each kind of player
float           default_health[] = {
	125.0,						// DEFAULT_PLAYER_SPAWNED
	100.0,						// DEFAULT_PLAYER_POWERED
	60.0,						// DEFAULT_PLAYER_WOUNDED
	100.0,						// DEFAULT_PLAYER_AVERAGE
};

// Starting armor of each kind of player
float           default_armor[] = {
	0.0,						// DEFAULT_PLAYER_SPAWNED
	100.0,						// DEFAULT_PLAYER_POWERED
	0.0,						// DEFAULT_PLAYER_WOUNDED
	25.0,						// DEFAULT_PLAYER_AVERAGE
};

// Each kind of player has this many weapon pickups worth of ammo (0 for no weapons)
//
// NOTE: These values are intentionally integers, not floats, because they are
// used as exponents for pow_int().
int             default_weapons[] = {
	0,							// DEFAULT_PLAYER_SPAWNED
	6,							// DEFAULT_PLAYER_POWERED
	4,							// DEFAULT_PLAYER_WOUNDED
	2,							// DEFAULT_PLAYER_AVERAGE
};

// Each kind of player has this many boxes of of ammo
float           default_ammo[] = {
	0.0,						// DEFAULT_PLAYER_SPAWNED
	6.0,						// DEFAULT_PLAYER_POWERED
	0.0,						// DEFAULT_PLAYER_WOUNDED
	5.0,						// DEFAULT_PLAYER_AVERAGE
};


/*
==============
BotItemUtility

Returns what percent of the item's maximum utility the
bot would get if the bot picked up the item right now.
1.0 means full utility, 0.0 meaning no use and the game
will probably prevent the bot from picking up the item
even if it tries.

NOTE: This is an analog version of BG_CanItemBeGrabbed()
==============
*/
float BotItemUtility(bot_state_t * bs, gentity_t * ent)
{
	int             weapon, provide, cur, received, stat_max;
	gitem_t        *item;
	const playerState_t *ps;

	// Cache the item structure and bot's player state and for easier access
	item = ent->item;
	ps = bs->ps;

	// Each item has its own pickup rules
	//
	// NOTE: Most items follow the rule of "provides this many units up
	// to a certain maximum".  As such, that typical computation is done
	// after this block.  Executers of this block must compute the
	// maximum value of the statistics (stat_max) and how much the player
	// currently has (cur).  If the item has special pickup rules, such as
	// weapons, the block of code can also compute how many units the item
	// provides.  Otherwise the block of code after this statement will
	// compute it using the rule just described.  Of course, items like
	// powerups and flags have their own special rules and directly return
	// 0.0 or 1.0 as appropriate.
	received = -1;
	provide = (ent->count ? ent->count : item->quantity);
	switch (item->giType)
	{
		case IT_WEAPON:
			// The weapon is very useful if the player doesn't have it
			weapon = item->giTag;
			if(!(ps->stats[STAT_WEAPONS] & (1 << weapon)))
				return 1.0;

			// The weapon is not useful at all if the player has infinite ammo
			cur = ps->ammo[weapon];
			if(cur < 0)
				return 0.0;

			// Players can have at most this much ammo
			stat_max = 200;

			// The player gets this much ammo from the pickup
			if(cur >= stat_max)
				received = 0;
			else if(cur < provide)
				received = provide - cur;
			else
				received = 1;
			break;

		case IT_AMMO:
			// The ammo is not useful if the player has an infinte amount
			weapon = item->giTag;
			cur = ps->ammo[weapon];
			if(cur < 0)
				return 0.0;

			// Players can have at most this much ammo
			stat_max = 200;
			break;

		case IT_ARMOR:
			// Compute the player's maximum allowed armor
#ifdef MISSIONPACK
			// Scouts cannot wear armor
			if(bg_itemlist[ps->stats[STAT_PERSISTANT_POWERUP]].giTag == PW_SCOUT)
				return 0.0;

			// Guards have higher max health which messes up the maximum armor computation
			if(bg_itemlist[ps->stats[STAT_PERSISTANT_POWERUP]].giTag == PW_GUARD)
				stat_max = ps->stats[STAT_MAX_HEALTH];
			else
#endif
				stat_max = ps->stats[STAT_MAX_HEALTH] * 2;

			// The player currently has this much armor
			cur = ps->stats[STAT_ARMOR];
			break;

		case IT_HEALTH:
#ifdef MISSIONPACK
			// Guards have one constant (and large) maximum health
			if(bg_itemlist[ps->stats[STAT_PERSISTANT_POWERUP]].giTag == PW_GUARD)
				stat_max = ps->stats[STAT_MAX_HEALTH];
			else
#endif
				// +5 and +100 health adds to double the player's stated maximum.
				// All others add to the normal maximum.
			if(provide == 5 || provide == 100)
				stat_max = ps->stats[STAT_MAX_HEALTH] * 2;
			else
				stat_max = ps->stats[STAT_MAX_HEALTH];

			// The player has this much health
			cur = ps->stats[STAT_HEALTH];
			break;

		case IT_POWERUP:
			// Always useful
			return 1.0;

		case IT_HOLDABLE:
			// Useful only if the player doesn't have one already
			return (ps->stats[STAT_HOLDABLE_ITEM] ? 0.0 : 1.0);

#ifdef MISSIONPACK
		case IT_PERSISTANT_POWERUP:
			// Players cannot have more than one persistant powerup
			if(ps->stats[STAT_PERSISTANT_POWERUP])
				return 0.0;

			// Players can't pickup the powerups for the opposite team
			if((ent->generic1 & 2) && (ps->persistant[PERS_TEAM] != TEAM_RED) ||
			   (ent->generic1 & 4) && (ps->persistant[PERS_TEAM] != TEAM_BLUE))
				return 0.0;

			// This item is useful to the player
			return 1.0;
#endif

		case IT_TEAM:
#ifdef MISSIONPACK
			// One flag CTF has its own set of pickup rules
			if(g_gametype.integer == GT_1FCTF)
			{

				// The neutral flag can always be picked up
				if(item->giTag == PW_NEUTRALFLAG)
					return 1.0;

				// You "pickup" the opposing flag by taking the neutral flag to it
				if((ps->powerups[PW_NEUTRALFLAG]) &&
				   ((ps->persistant[PERS_TEAM] == TEAM_RED && item->giTag == PW_BLUEFLAG) ||
					(ps->persistant[PERS_TEAM] == TEAM_BLUE && item->giTag == PW_REDFLAG)))
				{
					return 1.0;
				}
			}

			// The skulls can always be picked up
			if(g_gametype.integer == GT_HARVESTER)
				return 1.0;
#endif

			// CTF has its own set of pickup rules as well
			if(g_gametype.integer == GT_CTF)
			{
				// Players can always pickup the enemy flag.  They can also pick up
				// their own flag when it was dropped.  You can only "pick up" your
				// own flag when you are bringing the enemy flag to it as a capture.
				if(ps->persistant[PERS_TEAM] == TEAM_RED)
				{
					if(item->giTag == PW_BLUEFLAG ||
					   (item->giTag == PW_REDFLAG && ent->s.modelindex2) ||
					   (item->giTag == PW_REDFLAG && ps->powerups[PW_BLUEFLAG]))
					{
						return 1.0;
					}
				}
				else if(ps->persistant[PERS_TEAM] == TEAM_BLUE)
				{
					if(item->giTag == PW_REDFLAG ||
					   (item->giTag == PW_BLUEFLAG && ent->s.modelindex2) ||
					   (item->giTag == PW_BLUEFLAG && ps->powerups[PW_REDFLAG]))
					{
						return 1.0;
					}
				}
			}

			// Probably some teamplay related item that isn't designed for pickup
			return 0.0;

		default:
			// Whatever it is, you can't pick it up
			return 0.0;
	}

	// The player can't use the item if they are already at the maximum
	if(cur >= stat_max)
		return 0.0;

	// Items that don't provide anything are similarly useless
	if(provide <= 0)
		return 0.0;

	// Determine how much the player will receive if that has not
	// been computed yet
	if(received < 0)
	{
		if(cur + provide > stat_max)
			received = stat_max - cur;
		else
			received = provide;
	}

	// The item's usefulness is proportionate to how much of the
	// maximum provided the player actually receives
	return received / (float)provide;
}

/*
=============
BaseItemValue

Returns the value of an item to an average player in the
game over not picking up the item.  Note that this will
never be the value of the item to the bot that's making
item selection decisions-- it's just the value to someone
else.  (And by extension, the value in taking the item
so someone else can't have it.)

Returns -1 if the item is not present on the level.
=============
*/
float BaseItemValue(gitem_t * item)
{
	int             index;

	// Invalid items are obviously not present
	index = item - bg_itemlist;
	if(index < 0 || index > MAX_ITEM_TYPES)
		return -1;

	// Return this item's value
	//
	// NOTE: This could still be -1 if that item is not present
	return item_value[index];
}

/*
===============
BaseItemRespawn
===============
*/
int BaseItemRespawn(gitem_t * item)
{
	switch (item->giType)
	{
		case IT_ARMOR:
			return RESPAWN_ARMOR;
		case IT_AMMO:
			return RESPAWN_AMMO;
		case IT_HOLDABLE:
			return RESPAWN_HOLDABLE;
		case IT_POWERUP:
			return RESPAWN_POWERUP;
		case IT_HEALTH:
			return (item->quantity == 100 ? RESPAWN_MEGAHEALTH : RESPAWN_HEALTH);
		case IT_WEAPON:
			return (gametype == GT_TEAM ? g_weaponTeamRespawn.integer : g_weaponRespawn.integer);
		default:
			return 0.0;
	}
}

/*
===========
ItemRespawn

Compute the longest possible respawn delay for this item.

NOTE: Part of this code is based on Touch_Item() in g_items.c
===========
*/
float ItemRespawn(gentity_t * ent)
{
	float           respawn;

	// Dropped items do not respawn
	if(ent->flags & FL_DROPPED_ITEM)
		return 0;

	// Items with negative wait do not respawn
	if(ent->wait < 0)
		return 0;

	// When set, the wait value overrides the default respawn
	respawn = (ent->wait ? ent->wait : BaseItemRespawn(ent->item));

	// The random field can make the item take longer to respawn
	// (Or take less time, but this function only cares about max delay)
	if(ent->random > 0.0)
		respawn += ent->random;

	// This is the maximum possible respawn delay in seconds
	return respawn;
}

/*
===================
ItemUtilityDuration

Estimate how long it will take for a player to get all
the utility out of this item, assuming typical conditions.
Returns zero if this value is not estimatable.
===================
*/
float ItemUtilityDuration(gitem_t * item)
{
	int             quantity = item->quantity;

	// Each kind of item is used in different ways, so its usefulness is exhausted at different times
	switch (item->giType)
	{
			// Estimate how much time must elapse before that much damage is taken
		case IT_ARMOR:
		case IT_HEALTH:
			return quantity / (damage_per_second_typical * ENCOUNTER_RATE_DEFAULT);

			// Estimate how much time must elapsed before that much ammo will be shot
		case IT_AMMO:
		case IT_WEAPON:
			return quantity * weapon_stats[item->giTag].reload / ENCOUNTER_RATE_DEFAULT;

			// This one is easy: the powerup is useless when it runs out of time
		case IT_POWERUP:
			return quantity;

			// There's just no good way of estimating when these kinds of items will be "done"
		case IT_HOLDABLE:
#ifdef MISSIONPACK
		case IT_PERSISTANT_POWERUP:
#endif
		default:
			return 0.0;
	}
}

/*
==========
ItemPickup

Determines how often a player wants to
pickup a particular kind of item.
==========
*/
float ItemPickup(gitem_t * item)
{
	float           respawn, duration, pickup;

	// The item respawns this often
	respawn = BaseItemRespawn(item);

	// It takes this long before a player has used the resources from this item
	duration = ItemUtilityDuration(item);

	// Players want to pick up an item as often as it's useful, but no more
	// often than the item could possibly be there
	pickup = (respawn > duration ? respawn : duration);

	// Sanity check how frequently a player could even bother to pick something up
	return (pickup < PICKUP_TIME_MINIMUM ? PICKUP_TIME_MINIMUM : pickup);
}

/*
===============
ResourcePowerup

Check if the resource state will have
the powerup at the requested time
===============
*/
qboolean ResourcePowerup(resource_state_t * rs, int powerup, float time)
{
	return (rs->powerup[powerup] < 0) || (time < rs->powerup[powerup]);
}

/*
===================
HealthArmorToDamage

Determine how much damage is required to reduce
the health and armor pair to zero health.
===================
*/
float HealthArmorToDamage(float health, float armor)
{
	float           max_armor;

	// Compute the most armor that would get absorbed protecting "health" hitpoints
	max_armor = health * (ARMOR_PROTECTION / (1.0 - ARMOR_PROTECTION));

	// Determine how much the armor contibutes towards the player's total health
	if(armor < max_armor)
		return health + armor;
	else
		return health + max_armor;
}

/*
=========================
ResourceHealthChangeScore

The player effectively gains points when gaining health
and loses points when losing health because other players
can gain points for killing the player.
=========================
*/
void ResourceHealthChangeScore(resource_state_t * rs, float old_health, float old_armor)
{
	float           damage_change;

	// Compute the difference in the amount of damage required to kill this player
	damage_change = HealthArmorToDamage(rs->health, rs->armor) - HealthArmorToDamage(old_health, old_armor);

	// Determine what percentage of a player death (ie. point) this health change
	// represents.  This value, combined with the chance that killing this bot would
	// give a point to the leader, is the score value of the health gained or loss.
	rs->score += damage_change * rs->pi->deaths_per_damage * rs->pi->leader_point_share;
}

/*
=================
ResourceScoreRate
=================
*/
float ResourceScoreRate(resource_state_t * rs)
{
	return (rs->time > 0.0 ? rs->score / rs->time : 0.0);
}

/*
===============
PlayInfoFromBot

Fill out play statistics using bot state data data.
===============
*/
void PlayInfoFromBot(play_info_t * pi, bot_state_t * bs)
{
	int             opponents, weapon;
	float           survive_chance;
	entry_float_int_t rate_sort[WP_NUM_WEAPONS];
	bot_accuracy_t  acc;
	weapon_stats_t *ws;

	// The bot's player state
	pi->ps = bs->ps;

	// Compute percentage of opponent's points held by the opposing leader
	//
	// NOTE: This is another way of saying, "If someone gets a point from
	// killing this player, what is the chance it will make it harder for
	// the bot to take first place?"
	//
	// FIXME: It might be nice to do this for real rather than estimate
	// equal point percentage for all sides.  However the computations
	// can get complicated, especially in situations of multiple opposing
	// teams (eg. 3 or more teams), each with more than one player.  Things
	// also get murky with negative scores.  And there isn't a one to one
	// correspondance between points and killing opponents in all game play
	// modes.
	opponents = LevelNumTeams() - 1;
	if(opponents < 1)
		opponents = 1;
	pi->leader_point_share = 1.0 / opponents;

	// Bots always have 100 maximum health, since they never have a handicap
	pi->max_health = 100;

	// Average damage damage bot has received per second spent under enemy attack
	pi->received = bs->damage_received / bs->enemy_attack_time;

	// Determine the damage needed the bot needs to kill a player and vice versa
	pi->deaths_per_damage = (float)bs->deaths / bs->damage_received;
	pi->kills_per_damage = (float)bs->kills / bs->damage_dealt;

	// Determine expected reload and damage rates for each weapon
	for(weapon = 0; weapon < WP_NUM_WEAPONS; weapon++)
	{
		// Extract average damage dealt per weapon fire
		// NOTE: ws->shots is the number of shots per firing.  acc.shots is the
		// total number of shots recorded.
		ws = &weapon_stats[weapon];
		BotAccuracyRead(bs, &acc, weapon, NULL);
		pi->dealt[weapon] = (acc.direct.damage + acc.splash.damage) * ws->shots / acc.shots;

		// Compute how frequently the bot fires this weapon when in combat
		pi->reload[weapon] = ws->reload * BotAttackRate(bs, &acc);

		// Compute the chance a firing of this weapon will not be the killing shot
		survive_chance = 1.0 - (pi->kills_per_damage * pi->dealt[weapon]);
		if(survive_chance < 0.1)
			survive_chance = 0.1;

		// Only count reload time when not scoring the killing hit
		//
		// NOTE: This is an oversimplification.  As you can see, the percent of
		// firings that are kills depends on the weapon damage, which depends on
		// which damage modifiers the bot has picked up (eg. quad damage).  This code
		// determines a fixed preference ordering for the weapons, but in reality
		// that will change if the bot picks up quad damage.  The reload time should
		// also be sanity bounded by the total opportunities the bot has to fire
		// in a prediction time segment, since the reload time of a killing blow
		// does matter when you have another opponent to attack.  To be mathematically
		// correct, all of these calculations should be done in the inner loop, not
		// at the loop start, but this is done to save processing time.
		//
		// As an example of how this matters, consider what happens to the railgun
		// when you have Quad damage.  Every hit becomes a killing blow, meaning
		// that with 70% accuracy, the enemy will die in an average of .64 seconds,
		// despite having a base 66 * .7 = 46 DPS.  Meanwhile, with 40% lightning gun
		// accuracy (base 160 * .4 = 64 DPS), it will take 1.04 seconds to deal the
		// average 200 damage needed for a kill.  That makes the quad damage railgun
		// significantly better than the quad lightning gun, even though the normal
		// damage lightning gun is just comperable.  The weapon order needs to be
		// calculated at runtime to make this code mathematically correct.
		//
		// The summary is that damage per second isn't the whole story, so this code
		// uses modified reload times to fix the problem.
		pi->reload[weapon] *= survive_chance;

#ifdef DEBUG_AI
		// When forced to use a specific weapon, ignore all other weapons by setting
		// their damage rate to zero
		if(bs->use_weapon > WP_NONE && bs->use_weapon < WP_NUM_WEAPONS && bs->use_weapon != weapon)
		{
			pi->dealt[weapon] = 0;
		}
#endif

		// Also store the damage per second rate in the weapon damage rate sorting array
		rate_sort[weapon].key = pi->dealt[weapon] / pi->reload[weapon];
		rate_sort[weapon].value = weapon;
	}

	// Sort the weapons by damage rate
	qsort(rate_sort, WP_NUM_WEAPONS, sizeof(entry_float_int_t), CompareEntryFloatReverse);
	for(weapon = 0; weapon < WP_NUM_WEAPONS; weapon++)
		pi->weapon_order[weapon] = rate_sort[weapon].value;
}

/*
==========================
ResourceComputeFirstWeapon
==========================
*/
void ResourceComputeFirstWeapon(resource_state_t * rs)
{
	int             weapon;

	// Determine the highest damage rate weapon with ammo
	for(rs->first_weapon_order = 0; rs->first_weapon_order < WP_NUM_WEAPONS; rs->first_weapon_order++)
	{
		// Use this weapon if the player has the weapon with ammo
		weapon = rs->pi->weapon_order[rs->first_weapon_order];
		if((rs->ammo[weapon]) && (rs->weapons & (1 << weapon)))
			break;
	}
}

/*
==========================
ResourceSortPowerupTimeout

Sort an input list of powerup times by timeouts,
removing duplicate time entries. Returns the
actual number of entries in the timeout array.

NOTE: The size of the time array should be at least
num_powers+1, because an additional -1 entry will be
added to the end of timeout array, indicating an
unbounded time interval.
==========================
*/
int ResourceSortPowerupTimeout(resource_state_t * rs, int *power_id, int num_powers, float *time)
{
	int             i, time_count, duplicates;

	// Determine when powerups affecting the health mod states run out
	time_count = 0;
	for(i = 0; i < num_powers; i++)
	{
		if(rs->time < rs->powerup[power_id[i]])
			time[time_count++] = rs->powerup[power_id[i]];
	}

	// Sort the list by ascending time and then remove duplicate times
	qsort(time, time_count, sizeof(float), CompareEntryFloat);
	duplicates = 0;
	for(i = 1; i < time_count; i++)
	{
		// Ignore duplicates; copy new entries over holes left by duplicates
		if(time[i] == time[i - 1 - duplicates])
			duplicates++;
		else if(duplicates)
			time[i - duplicates] = time[i];
	}
	time_count -= duplicates;

	// Add a final entry for the remainder of the time
	time[time_count++] = -1;
	return time_count;
}

/*
========================
ResourceComputeHealthMod
========================
*/
void ResourceComputeHealthMod(resource_state_t * rs)
{
	int             i, time_count;
	float           start_time, time[MAX_HEALTH_MODIFY];
	int             power_ids[MAX_HEALTH_MODIFY - 1] = { PW_INVIS, PW_BATTLESUIT, PW_REGEN };
	health_modify_state_t *hm;

	// Determine when powerups affecting the health mod states run out
	time_count = ResourceSortPowerupTimeout(rs, power_ids, MAX_HEALTH_MODIFY - 1, time);

	// Filling out the health modification states starting with the current time
	for(i = 0, start_time = rs->time; i < time_count; start_time = time[i++])
	{
		// This record lasts until this time
		hm = &rs->health_mod[i];
		hm->time = time[i];

		// Modify the damage factor for invisibility (harder to hit) and suit (take less damage)
		// NOTE: Even though the suit only prevents half direct damage, it also prevents
		// all splash damage, making weapons like rockets totally ineffective against it.
		hm->damage_factor = 1.0;
		if(ResourcePowerup(rs, PW_INVIS, start_time))
			hm->damage_factor *= .4;
		if(ResourcePowerup(rs, PW_BATTLESUIT, start_time))
			hm->damage_factor *= .35;

		// Having regeneration or guard will change the health gain rate
		if(ResourcePowerup(rs, PW_REGEN, start_time))
		{
			hm->health_low = 15.0;
			hm->health_high = 5.0;
#ifdef MISSIONPACK
		}
		else if(rs->persistant == PW_GUARD)
		{
			hm->health_low = 15.0;
			hm->health_high = 0.0;
#endif
		}
		else
		{
			hm->health_low = 0.0;
			hm->health_high = -1.0;
		}
	}
}

/*
========================
ResourceComputeDamageMod
========================
*/
void ResourceComputeDamageMod(resource_state_t * rs)
{
	int             i, time_count;
	float           start_time, time[MAX_DAMAGE_MODIFY];
	int             power_ids[MAX_DAMAGE_MODIFY - 1] = { PW_QUAD, PW_HASTE };
	damage_modify_state_t *dm;

	// Determine when powerups affecting the health mod states run out
#ifdef MISSIONPACK
	// NOTE: Haste is ignored if the player has a superior weapon reload powerup
	if(rs->persistant == PW_SCOUT || rs->persistant == PW_AMMOREGEN)
		time_count = ResourceSortPowerupTimeout(rs, power_ids, MAX_DAMAGE_MODIFY - 2, time);
	else
#endif
		time_count = ResourceSortPowerupTimeout(rs, power_ids, MAX_DAMAGE_MODIFY - 1, time);

	// Filling out the damage modification states starting with the current time
	for(i = 0, start_time = rs->time; i < time_count; start_time = time[i++])
	{
		// This record lasts until this time
		dm = &rs->damage_mod[i];
		dm->time = time[i];

		// Quad damage and the doubler increase the player's damage factor
		dm->damage_factor = 1.0;
		if(ResourcePowerup(rs, PW_QUAD, start_time))
			dm->damage_factor *= g_quadfactor.value;
#ifdef MISSIONPACK
		if(rs->persistant == PW_DOUBLER)
			dm->damage_factor *= 2.0;
#endif

		// Haste, Scout, and Ammo Regen increase the player's fire rate
#ifdef MISSIONPACK
		dm->ammo_regen = qfalse;
		if(rs->persistant == PW_SCOUT)
			dm->fire_factor = 1.5;
		else if(rs->persistant == PW_AMMOREGEN)
		{
			dm->fire_factor = 1.3;
			dm->ammo_regen = qtrue;
		}
		else
#endif
		if(ResourcePowerup(rs, PW_HASTE, start_time))
			dm->fire_factor = 1.3;
		else
			dm->fire_factor = 1.0;
	}
}

/*
==================
ResourceFromPlayer

Fill out a resource state structure using data from a
player entity and play statistics for the player.
==================
*/
void ResourceFromPlayer(resource_state_t * rs, gentity_t * ent, play_info_t * pi)
{
	int             i;
	playerState_t  *ps;

	// Save the play information
	rs->pi = pi;

	// Read most values from the player state
	ps = &ent->client->ps;
	rs->health = ps->stats[STAT_HEALTH];
	rs->armor = ps->stats[STAT_ARMOR];
	rs->holdable = ps->stats[STAT_HOLDABLE_ITEM];
	rs->weapons = ps->stats[STAT_WEAPONS];

	// Copy the ammo values
	// NOTE: This can't use memcpy() because rs->ammo[i] is a floating point
	// and ps->ammo[i] is an int.
	for(i = 0; i < WP_NUM_WEAPONS; i++)
		rs->ammo[i] = ps->ammo[i];

	// Copy powerup times in millseconds to prediction offset time in seconds
	for(i = 0; i < PW_NUM_POWERUPS; i++)
	{
		// Convert most powerups from millisecond to second time
		//
		// NOTE: server_time_ms should be the same as level.time, but
		// this code refers directly to level.time because it's compare
		// values in a playerState_t object, a concepted defined and
		// updated by the server.
		if(ps->powerups[i] > level.time)
			rs->powerup[i] = ps->powerups[i] * 0.001 - server_time;

		// Flags use INT_MAX instead of -1 for "lasts forever"
		else if(ps->powerups[i] == INT_MAX)
			rs->powerup[i] = -1;

		// Persistant powerups use level.time instead of -1 for "lasts forever"
		else if(ps->powerups[i] == level.time)
			rs->powerup[i] = (i >= PW_SCOUT && i <= PW_AMMOREGEN ? -1 : 0);

		// This is what should be used
		else if(ps->powerups[i] < 0)
			rs->powerup[i] = -1;

		// And of course, maybe the player doesn't have the powerup
		else
			rs->powerup[i] = 0;
	}

	// Check if the player has a flag
	if(rs->powerup[PW_REDFLAG] || rs->powerup[PW_BLUEFLAG])
		rs->carry_value = VALUE_FLAG;

#ifdef MISSIONPACK
	// Check for the neutral flag as well
	else if(rs->powerup[PW_NEUTRALFLAG])
		rs->carry_value = VALUE_FLAG;
#endif

	// Not carrying any valuable flags
	else
		rs->carry_value = 0;

#ifdef MISSIONPACK
	// Check if the player has a persistant powerup
	rs->persistant = PW_NONE;
	for(i = PW_SCOUT; i <= PW_AMMOREGEN; i++)
	{
		if(rs->powerup[i])
		{
			rs->persistant = i;
			break;
		}
	}

	// Add carry value for skulls when playing harvester
	if(gametype == GT_HARVESTER)
		rs->carry_value += ps->generic1 * VALUE_SKULL;
#endif

	// This resource state hasn't been extrapolated in the future
	rs->time = 0;

	// The score value is used to determine how scores change for different
	// extrapolated states.  So the actual initialization value doesn't
	// matter as long as it's always initialized to the same value.
	rs->score = 0;

	// Do initial computations for some values
	ResourceComputeFirstWeapon(rs);
	ResourceComputeHealthMod(rs);
	ResourceComputeDamageMod(rs);

}

/*
=================
ResourceAddWeapon

NOTE: This code is based on Pickup_Weapon() from g_item.c
=================
*/
int ResourceAddWeapon(resource_state_t * rs, gentity_t * ent)
{
	int             weapon, weapon_bit, quantity;
	float          *ammo;
	qboolean        had_weapon, had_ammo;

	// Add the weapon to the player's list of weapons
	weapon = ent->item->giTag;
	weapon_bit = (1 << weapon);
	had_weapon = (rs->weapons & weapon_bit);
	rs->weapons |= weapon_bit;

	// Some weapons don't add any ammo
	if(ent->count < 0)
		return RS_ITEM_PICKUP;

	// Determine the most ammo gained from picking up this weapon
	quantity = (ent->count ? ent->count : ent->item->quantity);

	// Non-dropped items in non-teamplay mode usually add less ammo
	ammo = &rs->ammo[weapon];
	had_ammo = *ammo;
	if(!(ent->flags & FL_DROPPED_ITEM) && (gametype != GT_TEAM))
	{
		// If not at the minumum, add enough to reach the minimum ...
		if(*ammo < quantity)
			quantity -= *ammo;

		// ... Otherwise just add one more shot
		else
			quantity = 1;
	}

	// Add ammo up to the maximum
	*ammo += quantity;
	if(*ammo >= AMMO_MAX)
		*ammo = AMMO_MAX;
	return ((had_weapon && had_ammo) ? RS_ITEM_PICKUP : RS_ITEM_PICKUP | RS_ITEM_WEAPON);
}

/*
===============
ResourceAddAmmo

NOTE: This code is based on Pickup_Ammo() from g_item.c
===============
*/
int ResourceAddAmmo(resource_state_t * rs, gentity_t * ent)
{
	float          *ammo;
	qboolean        had_ammo;

	// Don't pickup ammo if the player can't carry it
	ammo = &rs->ammo[ent->item->giTag];
	if(*ammo >= 200)
		return 0;
	had_ammo = *ammo;

	// Add ammo up to the maximum
	*ammo += (ent->count ? ent->count : ent->item->quantity);
	if(*ammo >= AMMO_MAX)
		*ammo = AMMO_MAX;
	return (had_ammo ? RS_ITEM_PICKUP : RS_ITEM_PICKUP | RS_ITEM_WEAPON);
}

/*
================
ResourceAddArmor

NOTE: This code is based on Pickup_Armor() from g_item.c
================
*/
int ResourceAddArmor(resource_state_t * rs, gentity_t * ent)
{
	int             max_armor;

	// Don't grab the armor if the player's armor is at the maximum
	max_armor = rs->pi->max_health * 2;
	if(rs->armor >= max_armor)
		return 0;

#ifdef MISSIONPACK
	// Scouts cannot wear armor
	if(rs->persistant == PW_SCOUT)
		return 0;
#endif

	// Add armor up to the maximum
	rs->armor += (ent->count ? ent->count : ent->item->quantity);
	if(rs->armor > max_armor)
		rs->armor = max_armor;
	return RS_ITEM_PICKUP | RS_ITEM_HEALTH;
}

/*
=================
ResourceAddHealth

NOTE: This code is based on Pickup_Health() from g_item.c
=================
*/
int ResourceAddHealth(resource_state_t * rs, gentity_t * ent)
{
	int             quantity, max_health;

	// Determine how much health this item adds
	quantity = (ent->count ? ent->count : ent->item->quantity);

	// Small health balls and megahealth can be picked up to a maximum of 200
	// In team arena, the Guard powerup lets players pickup all health to a max of 200
	if(quantity == 5 || quantity == 100
#ifdef MISSIONPACK
	   || rs->persistant == PW_GUARD
#endif
		)
		max_health = rs->pi->max_health * 2;
	else
		max_health = rs->pi->max_health;

	// Don't grab the health item if the player's health is at the maximum
	if(rs->health >= max_health)
		return 0;

	// Add health up to the maximum
	rs->health += quantity;
	if(rs->health > max_health)
		rs->health = max_health;
	return RS_ITEM_PICKUP | RS_ITEM_HEALTH;
}

/*
==================
ResourceAddPowerup

NOTE: This code is based on Pickup_Powerup() from g_item.c
==================
*/
int ResourceAddPowerup(resource_state_t * rs, gentity_t * ent)
{
	int             duration;
	float          *powerup;

	// Do not pick up powerups if the player already permanently has that powerup
	powerup = &rs->powerup[ent->item->giTag];
	if(*powerup < 0)
		return 0;

	// Determine how long the powerup will last in seconds
	duration = (ent->count ? ent->count : ent->item->quantity);

	// Either set the powerup forever (until next death) or add to the timer
	if(duration < 0)
		*powerup = -1;
	else if(*powerup < rs->time)
		*powerup = rs->time + duration;
	else
		*powerup += duration;

	// Some powerups require health or damage modification state recomputations
	if(ent->item->giTag == PW_INVIS || ent->item->giTag == PW_REGEN || ent->item->giTag == PW_BATTLESUIT)
		return RS_ITEM_PICKUP | RS_ITEM_HEALTHMOD;
	else if(ent->item->giTag == PW_QUAD || ent->item->giTag == PW_HASTE)
		return RS_ITEM_PICKUP | RS_ITEM_DAMAGEMOD;
	else
		return RS_ITEM_PICKUP;
}

/*
===============
ResourceAddTeam

NOTE: This code is based on Pickup_Team() from g_team.c
===============
*/
int ResourceAddTeam(resource_state_t * rs, gentity_t * ent)
{
	int             player_team, item_team;

	// If the resource state doesn't have an associated team, fail the pickup
	if(!rs->pi->ps)
		return 0;
	player_team = rs->pi->ps->persistant[PERS_TEAM];

#ifdef MISSIONPACK
	// Only skulls can be picked up in harvester
	if(gametype == GT_HARVESTER)
	{
		// Award points for carrying enemy skulls over time (since it takes time
		// to capture them).
		if(ent->spawnflags != player_team)
			rs->carry_value += VALUE_SKULL;

		// Give a one-shot bonus for getting team skulls in either case
		rs->score += VALUE_SKULL;

		return RS_ITEM_PICKUP;
	}
#endif

	// Only check for flag pickups in flag game styles
	if(!(game_style & GS_FLAG))
		return 0;

	// Determine which team owns the flag
	item_team = EntityTeam(ent);
	if(item_team == TEAM_SPECTATOR)
		return 0;

	// Flags not owned by the player's team are added to the resource state
	if(EntityTeam(ent) != player_team)
	{
		rs->powerup[ent->item->giTag] = -1;

		// Provide both a one-shot score reward and a continual reward for holding the flag
		rs->carry_value = VALUE_FLAG;
		rs->score += VALUE_FLAG;

		return RS_ITEM_PICKUP;
	}

	// Flags at the player's own base can't be picked up
	if(!(ent->flags & FL_DROPPED_ITEM))
		return 0;

	// Team flags out in the level provide a one-shot score reward for returning them
	rs->score += VALUE_FLAG;
	return RS_ITEM_PICKUP;
}

/*
===================
ResourceAddHoldable

NOTE: This code is based on Pickup_Holdable() from g_item.c
===================
*/
int ResourceAddHoldable(resource_state_t * rs, gentity_t * ent)
{
	int             holdable;

	// Players can't pick up a holdable item if they have one already
	if(rs->holdable)
		return 0;

	// Determining when holdable items give extra points is tough, but
	// they are pretty much always useful, so just award a few points now.
	holdable = ent->item->giTag;
	switch (holdable)
	{
		case HI_TELEPORTER:
			rs->score += .5;
			break;
		case HI_MEDKIT:
			rs->score += .6;
			break;
		case HI_KAMIKAZE:
			rs->score += .9;
			break;
		case HI_PORTAL:
			break;				// Bots can't use this anyway
		case HI_INVULNERABILITY:
			rs->score += 1.0;
			break;
	}

	// Record the holdable item
	rs->holdable = holdable;
	return RS_ITEM_PICKUP;
}

#ifdef MISSIONPACK
/*
============================
ResourceAddPersistantPowerup

NOTE: This code is based on Pickup_PersistantPowerup() from g_item.c
============================
*/
int ResourceAddPersistantPowerup(resource_state_t * rs, gentity_t * ent)
{
	int             persistant;

	// Don't pickup powerups if the player already has one
	if(rs->persistant)
		return 0;

	// Set the persistant powerup and powerup array entry
	rs->persistant = ent->item->giTag;
	rs->powerup[rs->persistant] = -1;

	// Some powerups modify stats when they are picked up
	if(rs->persistant == PW_GUARD)
	{
		rs->health = 200;
		return RS_ITEM_PICKUP | RS_ITEM_HEALTH | RS_ITEM_HEALTHMOD;
	}
	else if(rs->persistant == PW_SCOUT)
	{
		rs->armor = 0;
		return RS_ITEM_PICKUP | RS_ITEM_HEALTH;
	}
	else if(rs->persistant == PW_DOUBLER)
	{
		return RS_ITEM_PICKUP | RS_ITEM_DAMAGEMOD;
	}

	return RS_ITEM_PICKUP;
}
#endif

/*
===============
ResourceAddItem

Attempt to add the specified item to the resource
state.  Returns a set of flag describing what portions
of the resource state changed.  See RS_* flags for
more information.

NOTE: This code is based on Touch_Item() from g_item.c
===============
*/
int ResourceAddItem(resource_state_t * rs, gentity_t * ent)
{
	// If a player state was specified, only add the item if the player could
	// pick the item right now
	//
	// NOTE: Technically it's possible that a player couldn't pick up the
	// item now, but would be able to pick it up while travelling en-route to
	// the item.  For example, if health is 100, picking up a 50 health ball
	// could be good to keep it from the opponent if the bot thinks it will
	// take at least one damage in transit.  Unfortunately, this estimated
	// damage is just that-- an estimate.  It might not happen.  If this check
	// is not in place, bots will sometimes continually select items they can't
	// actually pick up.  It's safest just to let the bot demonstrate its need
	// for the item and then grab it.
	//
	// FIXME: Does this need an inuse check?
	if(rs->pi->ps && !BG_CanItemBeGrabbed(gametype, &ent->s, rs->pi->ps))
		return 0x00;

	// Each type of item has a different rule for being added to the resource state
	switch (ent->item->giType)
	{
		case IT_WEAPON:
			return ResourceAddWeapon(rs, ent);
		case IT_AMMO:
			return ResourceAddAmmo(rs, ent);
		case IT_ARMOR:
			return ResourceAddArmor(rs, ent);
		case IT_HEALTH:
			return ResourceAddHealth(rs, ent);
		case IT_POWERUP:
			return ResourceAddPowerup(rs, ent);
		case IT_TEAM:
			return ResourceAddTeam(rs, ent);
		case IT_HOLDABLE:
			return ResourceAddHoldable(rs, ent);
#ifdef MISSIONPACK
		case IT_PERSISTANT_POWERUP:
			return ResourceAddPersistantPowerup(rs, ent);
#endif

		default:
			return 0x00;
	}
}

/*
==================
ResourceItemChange

Recomputes some internal data, if necessary, after
picking up an item changes the resource state.
"result" is a bitmap of what changes occurred.  The
"old_health" and "old_armor" values refer to the
health and armor before the item pickup.

Returns true if an item was picked up and false if not.
==================
*/
qboolean ResourceItemChange(resource_state_t * rs, int result, float old_health, float old_armor)
{
	// Increase the score when the damage required to kill the player increases
	if(result & RS_ITEM_HEALTH)
		ResourceHealthChangeScore(rs, old_health, old_armor);

	// Recompute the first available weapon if needed
	if(result & RS_ITEM_WEAPON)
		ResourceComputeFirstWeapon(rs);

	// Recompute the health and damage modification lists when necessary
	if(result & RS_ITEM_HEALTHMOD)
		ResourceComputeHealthMod(rs);
	if(result & RS_ITEM_DAMAGEMOD)
		ResourceComputeDamageMod(rs);

	// Return true if any items can be picked up
	return (result & RS_ITEM_PICKUP ? qtrue : qfalse);
}

/*
==================
ResourceAddCluster

Add items in a cluster to a resource state.  Only adds the items that
will respawn in the specified number of seconds.  Use 0 to only add
items that are currently respawned.  Returns true if any items were
picked up.

NOTE: Sometimes the order in which items are picked up changes the
final resource state values.  In other words, item pickups are not
necessarily commutitive.  For example, a player at 75 health who
gets 50 health and then megahealth will have 200 health, but getting
megahealth first will result in 175 health.  Similarly, picking up
an ammo box before a weapon can provide less ammo than picking up the
weapon first.  The items in the cluster are added in the order they
are stored in the cluster!  If it's important for the player to pick
up the items in a certain order, they should be stored in the cluster
in that order.  Also, it will be the caller's responsibility to
encourage or enforce the pickup of items in that order.  This function
is only responsible for modifying the resource states.  It cannot
guarantee a player will actually pick up the items in the expected order.
==================
*/
qboolean ResourceAddCluster(resource_state_t * rs, item_cluster_t * cluster, float time, float see_teammate, float see_enemy)
{
	int             flags, result, mstime, respawn, opportunities;
	float           health, armor;
	float           value, total_value, score_scalar;
	float           team_pickup, enemy_pickup, no_pickup;
	item_link_t    *item;
	gentity_t      *ent;

	// Dead players do not pick up items
	if(rs->health <= 0)
		return qfalse;

	// Changes in armor and health affect the player's score
	health = rs->health;
	armor = rs->armor;

	// Convert the time to milliseconds, the time scale of the base game
	mstime = time * 1000;

	// Add each cluster item in order
	result = 0;
	total_value = 0.0;
	for(item = cluster->start; item; item = item->next_near)
	{
		// Ignore non-items
		//
		// NOTE: This check exists because bots could still be tracking clusters
		// of dropped items that were picked up last frame.  In theory these
		// items could get stripped out of the clusters, but it is such an
		// infrequent corner case that it's not worth the extra processing effort
		// to do so.
		ent = item->ent;
		if(!ent->inuse)
			continue;

		// Check for items that aren't spawned in
		if(!(ent->r.contents & CONTENTS_TRIGGER))
		{
			// Ignore items that won't respawn or won't respawn in time
			if((ent->think != RespawnItem) || (level.time + mstime < ent->nextthink))
			{
				continue;
			}
		}

		// Add this item to the resource state
		flags = ResourceAddItem(rs, ent);
		result |= flags;

		// Don't award points for keeping an item from the enemy if:
		// The item doesn't respawn -or-
		// The item has no value -or-
		// No other players will pick it up
		respawn = ItemRespawn(ent);
		value = BaseItemValue(ent->item);
		if(!(flags & RS_ITEM_PICKUP) || (respawn <= 0) || (value <= 0) || (!see_enemy && !see_teammate))
			continue;

		// Account for the additional implicit value of picking up this cluster
		total_value += value;
	}

	// Only award or deduct points for taking clusters more valuable than
	// the average pickup
	if(total_value > pickup_value_average)
	{
		// Only award or deduct for the additional value above the average pickup
		total_value -= pickup_value_average;

		// Compute the score scalar for picking up the item.  This is essentially the
		// chance an enemy will pick up the item minus the chance a teammate will pick
		// up the item-- the player gains score for keeping items from enemies and
		// loses score for stealing items from teammates.

		// Assume players that are nearby always want the item, if only to take
		// it from others
		team_pickup = see_teammate;
		enemy_pickup = see_enemy;

		// Assume it takes about 4 seconds to move between regions.  Every 4
		// seconds, each player has a new opportunity to pick up an item.
		// Therefore, picking up this item causes respawn time / 4 missed
		// opportunities to grab it.
		opportunities = respawn / 4;

		// Every opportunity frame (4 seconds), there is a chance a teammate or an
		// enemy will pick up the item.  The score is scaled by zero if both types
		// want to pick up the item (since it will go to either side with equal
		// probability).  Points are gained if only the enemy would want it and
		// lost if only the teammates would want it.  If neither side wants it, the
		// calculation recurses for another frame.
		//
		// So assume the chance an enemy will grab the item each frame is e, and
		// the chance a teammate will grab the item is t.  Set E = 1-e, T = 1-t,
		// the chances that no enemy or no teammate will pick up the item,
		// respectively.  The scale value for pickup frame 0 is:
		//   S(0) = e*T - t*E = e(1-t) - t(1-e) = e - et - t + et = (e-t)
		//
		// For frame 1, only modify the scale if no one picked up the item in the
		// previous frame, which happens E*T percent of the time.  So:
		//   S(1) = S(0) + ET * S(0)
		//
		// Similarly, for frame x:
		//   S(x) = S(x-1) + ET^x * S(0)
		//        = S(0) * (Sum(i=0->x) (ET^i)
		// Using geometric series, this simplifies to:
		//        = (e-t) * (ET^(x+1) - 1) / (ET - 1)
		//
		// For reference:
		//   e = enemy_pickup
		//   t = team_pickup
		//   E = 1 - enemy_pickup
		//   T = 1 - team_pickup
		//   x = opportunities
		//  ET = no_pickup
		//
		// To avoid division by zero, the code must check when ET = 1.
		// Since E*T = (1-e)*(1-t), this happens when both e and t are 0,
		// hence the last check at the start of this if-block

		// Cache the chance neither side will pick up the item for a frame (aka. E*T)
		no_pickup = (1.0 - team_pickup) * (1.0 - enemy_pickup);

		// Determine how much to scale the base score for the item
		score_scalar = (enemy_pickup - team_pickup) * (pow_int(no_pickup, opportunities + 1) - 1) / (no_pickup - 1);

		// Award or deduct points for keeping the item from other players
		rs->score += score_scalar * total_value;
	}

	// Modify the resource state after items changed the state
	return ResourceItemChange(rs, result, health, armor);
}

/*
====================
ResourceModifyHealth

Apply health loss to the player for a set period of time while
taking into account the rules of a health modification state.
The player loses "damage_rate" health per second from enemy
attacks.  Powerups such as regeneration could offset this.

Returns the actual number of seconds during which health was
modified.  (If the health would be modified to zero, a number
less than the input time would be returned.)
====================
*/
float ResourceModifyHealth(resource_state_t * rs, health_modify_state_t * hm, float max_time, float damage_rate)
{
	register float  time, loss_rate, loss;
	register qboolean start_health_below;

	// Determine the starting health loss rate and which side of the threshold its on
	start_health_below = (rs->health <= rs->pi->max_health);
	loss_rate = damage_rate - (start_health_below ? hm->health_low : hm->health_high);

	// The number of seconds remaining to be processed
	time = max_time;

	// Check for health values that cross the maximum health threshold
	if((loss_rate) && ((start_health_below) ^ (loss_rate > 0.0)))
	{
		// Determine the time remaining when the health value crosses the threshold
		time -= (rs->health - rs->pi->max_health) / loss_rate;

		// If the health value won't reach maximum in time, use the interpolated health
		if(time <= 0.0)
		{
			rs->health -= loss_rate * max_time;
			return max_time;
		}

		// The health is the maximum with "time" seconds of processing left
		rs->health = rs->pi->max_health;

		// If the loss rate on the other side of the threshold has the opposite sign,
		// the health value will stay converged at the maximum health threshold.
		loss_rate = damage_rate - (start_health_below ? hm->health_high : hm->health_low);
		if((start_health_below) ^ (loss_rate < 0.0))
			return max_time;
	}

	// Determine the most health that will be lost
	loss = loss_rate * time;

	// Check for possible player death
	if(loss > rs->health)
	{
		// The time of death equals the maximum allowed prediction time minus
		// the length of time that would have been spent reducing the health
		// total below zero
		time = max_time - (1.0 - rs->health / loss) * time;

		// Reset the health after the actual time has been computed
		rs->health = 0.0;
		return time;
	}

	// Remove the loss, capping the health at double maximum (for negative losses, aka. gains)
	rs->health -= loss;
	if(rs->health > rs->pi->max_health * 2)
		rs->health = rs->pi->max_health * 2;
	return max_time;
}

/*
=========================
ResourceModifyHealthArmor

Apply damage to the player for a set period of time while taking
into account the rules of a health modification state.  The
player receives "damage_rate" damage per second from enemy
attacks, although the health modification state could decrease
this value (for example, from the battlesuit).

Returns the actual number of seconds during which the health
and armor were modified.  (If the health would be modified to
zero, a number less than the input time would be returned.)
=========================
*/
float ResourceModifyHealthArmor(resource_state_t * rs, health_modify_state_t * hm, float max_time, float damage_rate)
{
	float           armor_rate, armor_time, armor_time_max, time;

	// Scale the damage rate according to health modification rules
	damage_rate *= hm->damage_factor;

	// Apply all the damage to health if the player isn't protected by armor or
	// in the extremely unlikely case that the damage rate is non-positive
	if(rs->armor <= 0 || damage_rate <= 0)
	{
		// Estimate the health state max_time seconds in the future
		time = ResourceModifyHealth(rs, hm, max_time, damage_rate);

		// Decay armor above the maximum if necessary
		if(rs->armor > rs->pi->max_health)
		{
			armor_time_max = (rs->armor - rs->pi->max_health);
			rs->armor -= (time < armor_time_max ? time : armor_time_max);
		}

		// Return the actual time spent processing health changes
		return time;
	}

	// Determine when the player's armor will decrease to the maximum and 0,
	// taking into account the -1 armor per second loss for armor above max
	armor_rate = damage_rate * ARMOR_PROTECTION;
	if(rs->armor <= rs->pi->max_health)
	{
		armor_time_max = 0.0;
		armor_time = rs->armor / armor_rate;
	}
	else
	{
		armor_time_max = (rs->armor - rs->pi->max_health) / (armor_rate + 1);
		armor_time = armor_time_max + rs->pi->max_health / armor_rate;
	}

	// Modify the health while armor protected first and record the actual amount
	// of time spent doing so
	time = (max_time <= armor_time ? max_time : armor_time);
	time = ResourceModifyHealth(rs, hm, time, damage_rate * (1.0 - ARMOR_PROTECTION));

	// If the attack won't reduce the armor to zero (either by player death or
	// insufficient attack time on the player), compute the actual armor value
	// and exit.
	//
	// NOTE: This code still needs to properly update the armor value because
	// this just represents predicted death, not actual death.  Players that
	// think they will die can still predict after estimated death, to see how
	// things will work out if they "beat the odds".  In this case, the player
	// still needs to know it's expected remaining armor if it ever picks up
	// enough health to become alive again.
	if(max_time <= armor_time || rs->health <= 0.0)
	{
		// Decrement armor for actual time attacked and armor decay
		rs->armor -= armor_rate * time;
		rs->armor -= (time < armor_time_max ? time : armor_time_max);

		return time;
	}

	// Modify the health now that the armor has been completely destroyed
	rs->armor = 0.0;
	time += ResourceModifyHealth(rs, hm, max_time - time, damage_rate);

	// NOTE: The input max_time is returned if the player didn't die to avoid potential
	// floating point rounding errors.  Time could be slightly less (or more) than max_time.
	return (rs->health <= 0.0 ? time : max_time);
}

/*
==================
ResourceFireWeapon

Fire a weapon over the course of *time seconds.  "consume_rate"
defines how much ammo is consumed per second.  The weapon is
fired until either *time seconds have expired or the "threshold"
ammo value is reached.  rs->ammo[weapon] is modified to reflect
the actual amount of ammo lost.  The *damage value is increased
by damage_rate damage per second actually fired.  The actual
time spent is then decremented from *time.  If *time is zero,
the function returns true (stop processing).  Otherwise false is
returned.

NOTE: If the consumption rate is negative, ammo is
actually gained (such as from the Ammo Regen powerup).
In this case, the threshold is an upper bound.

NOTE: The starting ammo total should be on the proper side of the
threshold, or negative consumption times will be computed.  It's
not hard to put the right check inside this function, but the
check is only needed in one case that almost never gets called.
So the check was moved out for speed reasons.
==================
*/
qboolean ResourceFireWeapon(resource_state_t * rs, int weapon,
							float *time, float consume_rate, int threshold, float *damage, float damage_rate)
{
	float           spent_time, converge_time;

	// The whole time can be spent if no ammo is consumed
	//
	// NOTE: There is a very, very small but non-zero chance this will occur
	if(!consume_rate)
	{
		spent_time = *time;
	}
	else
	{
		// Determine how much time is needed for the ammo to converge to the threshold
		converge_time = (rs->ammo[weapon] - threshold) / consume_rate;
		spent_time = (converge_time < *time ? converge_time : *time);
	}

	// Consume the required amount of ammo
	rs->ammo[weapon] -= consume_rate * spent_time;

	// Estimate damage dealt
	*damage += damage_rate * spent_time;

	// Decrease the amount of time left to process
	*time -= spent_time;

	// Inform the caller if they have finished processing
	return (*time <= 0.0);
}

/*
====================
ResourceModifyDamage

Compute how much damage the player deals for "time" seconds,
"player_rate" percent of which are used for attacking, using
a set of rules that govern how the player can deal damage.

Returns the amount of damage the player will deal.
====================
*/
float ResourceModifyDamage(resource_state_t * rs, damage_modify_state_t * dm, float time, float player_rate)
{
	int             weapon;
	float           attack_rate, damage, damage_rate, consume_rate, regen_consume_rate;

#ifdef MISSIONPACK
	// NOTE: These tables are derived from ClientTimerActions() in g_active.c
	const static int ammo_regen_max[WP_NUM_WEAPONS] = {
		0,						// WP_NONE
		0,						// WP_GAUNTLET
		50,						// WP_MACHINEGUN
		10,						// WP_SHOTGUN
		10,						// WP_GRENADE_LAUNCHER
		10,						// WP_ROCKET_LAUNCHER
		50,						// WP_LIGHTNING
		10,						// WP_RAILGUN
		50,						// WP_PLASMAGUN
		10,						// WP_BFG
		0,						// WP_GRAPPLING_HOOK
		10,						// WP_NAILGUN
		5,						// WP_PROX_LAUNCHER
		100,					// WP_CHAINGUN
	};

	const static float ammo_regen_rate[WP_NUM_WEAPONS] = {
		0.0,					// WP_NONE
		0.0,					// WP_GAUNTLET
		4.0,					// WP_MACHINEGUN
		0.6667,					// WP_SHOTGUN
		0.5,					// WP_GRENADE_LAUNCHER
		0.5714,					// WP_ROCKET_LAUNCHER
		3.3333,					// WP_LIGHTNING
		0.5714,					// WP_RAILGUN
		3.3333,					// WP_PLASMAGUN
		0.25,					// WP_BFG
		0.0,					// WP_GRAPPLING_HOOK
		0.8,					// WP_NAILGUN
		0.5,					// WP_PROX_LAUNCHER
		5.0,					// WP_CHAINGUN
	};
#endif

	// Compute the percentage of time the player will spend in combat, able to attack
	attack_rate = player_rate * dm->fire_factor;

	// Loop over weapons sorted by damage rate
	damage = 0.0;

	for(;						// rs->first_weapon_order is already initialized
		rs->first_weapon_order < WP_NUM_WEAPONS; rs->first_weapon_order++)
	{
		// Ignore this weapon if the player doesn't have it or ammo for it
		weapon = rs->pi->weapon_order[rs->first_weapon_order];
		if(!(rs->ammo[weapon]) || !(rs->weapons & (1 << weapon)))
			continue;

		// Compute the expected damage the weapon will deal per second
		damage_rate = attack_rate * rs->pi->dealt[weapon] / rs->pi->reload[weapon];

		// Spend the remaining time firing this weapon if it has no ammo
		if(rs->ammo[weapon] < 0.0)
		{
			damage += damage_rate * time;
			break;
		}

		// Determine the amount of ammo consumed per second
		consume_rate = attack_rate / rs->pi->reload[weapon];

#ifdef MISSIONPACK
		// The ammo consumption rate changes when ammo regeneration is active
		if(dm->ammo_regen)
		{
			// Fire the weapon until reaching the ammo regen max if above the maximum
			if(rs->ammo[weapon] > ammo_regen_max[weapon] &&
			   ResourceFireWeapon(rs, weapon, &time, consume_rate, ammo_regen_max[weapon], &damage, damage_rate))
			{
				break;
			}

			// Determine how fast ammo will be consumed while regenerating
			consume_rate -= ammo_regen_rate[weapon];

			// The ammo converges to the regen max if the consumption rate is now negative
			if(consume_rate < 0.0)
			{
				// Treat any remaining time as additional damage
				if(ResourceFireWeapon(rs, weapon, &time, consume_rate, ammo_regen_max[weapon], &damage, damage_rate))
				{
					damage += damage_rate * time;
				}
				break;
			}

			// Converge to zero at the reduced (but still positive) consumption rate
			// using the block of code below
		}
#endif

		// Consume ammo until the firing time finishes or the ammo runs out
		if(ResourceFireWeapon(rs, weapon, &time, consume_rate, 0, &damage, damage_rate))
			break;
	}

	// Return the total damage scaled by the player's damage factor
	return damage * dm->damage_factor;
}

/*
========================
ResourcePredictEncounter

Predict how a resource state will change from spending time
under specified encounter circumstances.  "time" is the
additional amount of time to predict ahead (so the ending
rs->time value will be rs->time + time, unless the player has
a speed powerup reducing travel time).  "player_rate" is the
percentage of "time" that the player has to attack enemies.
"enemy_rate" is the percentage of "time" that enemies will
spend attacking the player.  "score" is the number of points
the player will earn for attacking the most valuable enemy
(enemies?) in this region.

Players that are predicted to die will not score points after
their predicted death, but the prediction will still continue.
(After all, the prediction could be wrong-- maybe the player
won't die then, and they'll have an opportunity to pickup
health.)
========================
*/
void ResourcePredictEncounter(resource_state_t * rs, float time, float score, float player_rate, float enemy_rate)
{
	int             i;
	float           haste_time, end_time, live_time, damage_time;
	float           health, armor, receive_rate, damage;
	health_modify_state_t *hm;
	damage_modify_state_t *dm;

	// Determine the actual amount of time spent in this encounter
#ifdef MISSIONPACK
	if(rs->persistant == PW_SCOUT)
		time /= 1.5;
	else
#endif
	if(rs->powerup[PW_HASTE] < 0)
		time /= 1.3;
	else if(rs->powerup[PW_HASTE] > rs->time)
	{
		// Computing the total time when haste runs out in-transit is a little tricky
		haste_time = rs->powerup[PW_HASTE] - rs->time;
		if(haste_time * 1.3 > time)
			time /= 1.3;
		else
			time -= haste_time * 0.3;
	}

	// Changes in armor and health affect the player's score
	health = rs->health;
	armor = rs->armor;

	// Compute the base damage received per real second without defensive powerups
	receive_rate = rs->pi->received * enemy_rate;

	// Determine how long the player will stay alive in this encounter.
	// NOTE: This is either when the player's health reaches zero or "time"
	end_time = rs->time + time;
	live_time = rs->time;
	for(i = 0; i < MAX_HEALTH_MODIFY; i++)
	{
		// Always terminate the loop when processing the last state
		hm = &rs->health_mod[i];
		if(hm->time < 0 || end_time <= hm->time)
		{
			live_time += ResourceModifyHealthArmor(rs, hm, end_time - live_time, receive_rate);
			break;
		}

		// Apply this state's rules and continue unless the player died
		live_time += ResourceModifyHealthArmor(rs, hm, hm->time - live_time, receive_rate);
		if(rs->health <= 0.0)
			break;
	}

	// Award (or probably demerit) points due to health and armor changes
	ResourceHealthChangeScore(rs, health, armor);

	// Award points for every ten seconds of carrying valuable items (like a flag)
	rs->score += (live_time - rs->time) * rs->carry_value * 0.1;

	// Estimate how much damage the player will deal while it is alive
	damage = 0.0;
	for(i = 0; i < MAX_DAMAGE_MODIFY && rs->time < live_time; i++)
	{
		// Process the next damage modification state and update the resource state time
		dm = &rs->damage_mod[i];
		damage_time = (live_time <= dm->time || dm->time < 0 ? live_time : dm->time);
		damage += ResourceModifyDamage(rs, dm, damage_time - rs->time, player_rate);
		rs->time = damage_time;
	}

	// Award points for damage dealt
	rs->score += score * damage * rs->pi->kills_per_damage;

	// Set the resource timestamp to the end of prediction, which will be later
	// than its current setting if the player died during prediction.
	rs->time = end_time;

	// Incrementing the time can invalidate health and damage modification states
	//
	// FIXME: It might be faster just to find the first valid state and memmove()
	// all of the remaining states back to index 0.  Or it might be slower, because
	// in most situations, there will only be one valid modification state remaining,
	// causing an unnecessary memmove() of extra data.
	if(rs->health_mod[0].time >= 0.0 && rs->health_mod[0].time <= rs->time)
		ResourceComputeHealthMod(rs);
	if(rs->damage_mod[0].time >= 0.0 && rs->damage_mod[0].time <= rs->time)
		ResourceComputeDamageMod(rs);
}

/*
===============
ItemValuesReset
===============
*/
void ItemValuesReset(void)
{
	int             i;

	// Assume no items are present on the level
	for(i = 0; i < MAX_ITEM_TYPES; i++)
		item_value[i] = -1;
}

/*
=================
ItemValuesCompute
=================
*/
void ItemValuesCompute(item_link_t * items, int num_items)
{
	int             i, j, opponents, item_index, respawn;
	int             weapon_frequency[WP_NUM_WEAPONS], num_weapons;
	int             ammo_frequency[WP_NUM_WEAPONS], num_ammo;
	float           score_change, score[NUM_DEFAULT_PLAYERS];
	float           survive_chance, total_point_rate, total_respawn_rate, average_value;
	float           weight, boxes, pickup, hasWeapon;
	gitem_t        *item;
	weapon_stats_t *ws;
	entry_float_int_t rate_sort[WP_NUM_WEAPONS];
	play_info_t     pi;
	resource_state_t player[NUM_DEFAULT_PLAYERS], rs, *base_rs;
	gentity_t       generic_entity;
	qboolean        isWeapon, isAmmo;

	// Record what items are present on the level with a 0 value (for
	// present but unknown value) instead of -1 (for missing item).
	// Also count the level's weapon and ammo box distributions.
	ItemValuesReset();
	num_weapons = 0;
	memset(weapon_frequency, 0, sizeof(weapon_frequency));
	num_ammo = 0;
	memset(ammo_frequency, 0, sizeof(ammo_frequency));
	for(i = 0; i < num_items; i++)
	{
		// This item is present on the level
		item = items[i].ent->item;
		item_value[item - bg_itemlist] = 0.0;

		// Note instances of weapons and ammo
		switch (item->giType)
		{
			case IT_WEAPON:
				weapon_frequency[item->giTag]++;
				num_weapons++;
				break;

			case IT_AMMO:
				ammo_frequency[item->giTag]++;
				num_ammo++;
				break;
		}
	}

	// In the extremely unlikely event that a level has no weapons, this
	// is the easiest way to avoid division by zero problems.  This solution
	// means each non-existant weapon provides 0% of the potential weapon
	// pickups, by the way.
	if(!num_weapons)
		num_weapons = 1;

	// Default play information statistics
	pi.ps = NULL;
	pi.max_health = 100;
	pi.received = 10.0;
	pi.deaths_per_damage = 1.0 / 150;
	pi.kills_per_damage = 1.0 / 150;

	// Assume all opponents are equally good at killing this player
	opponents = LevelNumTeams() - 1;
	if(opponents < 1)
		opponents = 1;
	pi.leader_point_share = 1.0 / opponents;

	// Determine expected reload and damage rates for each weapon
	for(i = 0; i < WP_NUM_WEAPONS; i++)
	{
		// Estimate how often a player fires this weapon in combat
		ws = &weapon_stats[i];
		pi.reload[i] = ws->reload * 0.7;

		// Estimate how much damage the weapon deals per firing
		pi.dealt[i] = ws->accuracy * ws->damage * ws->shots;

		// Estimate the chance a firing of this weapon will not be the killing shot
		survive_chance = 1.0 - (pi.kills_per_damage * pi.dealt[i]);
		if(survive_chance < 0.1)
			survive_chance = 0.1;

		// Only count reload time when not scoring the killing hit
		pi.reload[i] *= survive_chance;

		// Also store the damage per second rate in the weapon damage rate sorting array
		rate_sort[i].key = pi.dealt[i] / pi.reload[i];
		rate_sort[i].value = i;
	}

	// Sort the weapons by damage rate
	qsort(rate_sort, WP_NUM_WEAPONS, sizeof(entry_float_int_t), CompareEntryFloatReverse);
	for(i = 0; i < WP_NUM_WEAPONS; i++)
		pi.weapon_order[i] = rate_sort[i].value;

	// Set shared values for resource states
	for(i = 0; i < NUM_DEFAULT_PLAYERS; i++)
	{
		// Set values which are constant for all default resource states
		base_rs = &player[i];
		base_rs->pi = &pi;

		// No powerups, holdable items, flags, or otherwise interesting items
		memset(base_rs->powerup, 0, sizeof(base_rs->powerup));
		base_rs->holdable = 0;
		base_rs->carry_value = 0.0;
#ifdef MISSIONPACK
		base_rs->persistant = PW_NONE;
#endif

		// Some weapons are supplied to everyone
		memset(base_rs->ammo, 0, sizeof(base_rs->ammo));
		for(j = 1; j < WP_NUM_WEAPONS; j++)
		{
			// Ignore weapons players that are not given to spawned players
			if(!weapon_stats[j].start_ammo)
				continue;

			// Give all players this weapon and that much ammo
			base_rs->weapons |= (1 << j);
			base_rs->ammo[i] = weapon_stats[j].start_ammo;
		}

		// Start processing at time zero and score zero
		base_rs->time = 0;
		base_rs->score = 0;
	}

	// Load information for each style of player
	for(i = 0; i < NUM_DEFAULT_PLAYERS; i++)
	{
		// The player's health and armor
		player[i].health = default_health[i];
		player[i].armor = default_armor[i];

		// Players that haven't picked up any weapons don't need their starting
		// ammo adjusted
		if(!default_weapons[i])
			continue;

		// Compute how much to scale the starting ammo
		weight = default_weapons[i] / (float)default_weapons[DEFAULT_PLAYER_POWERED];

		// Deplete starting weapon ammo proportionate to what a powered player would have
		for(j = 0; j < WP_NUM_WEAPONS; j++)
		{
			if(player[i].ammo[j] > 0)
				player[i].ammo[j] *= weight;
		}
	}

	// Give weapons and ammo to non-spawn players if those items are on the level
	for(i = 0; i < bg_numItems; i++)
	{
		// Ignore non-weapon, non-ammo items
		item = &bg_itemlist[i];
		isWeapon = (item->giType == IT_WEAPON);
		isAmmo = (item->giType == IT_AMMO);
		if(!isWeapon && !isAmmo)
			continue;

		// Ignore items not present on the level
		if(item_value[i] < 0.0)
			continue;

		// Compute the weighting of the player's total ammo pickups are of this weapon or
		// ammo box's type
		if(isWeapon)
			weight = weapon_frequency[item->giTag] / (float)num_weapons;
		else
			weight = ammo_frequency[item->giTag] / (float)num_ammo;

		// Each style of player gets a different amount of ammo from the weapon
		for(j = 0; j < NUM_DEFAULT_PLAYERS; j++)
		{
			// Give the weapon to players that have weapons
			if(isWeapon && default_weapons[j] > 0)
				player[j].weapons |= (1 << item->giTag);

			// Compute the number of box pickups of this kind of weapon or ammo this player gets
			if(isWeapon)
				boxes = default_weapons[j] * weight;
			else
				boxes = default_ammo[j] * weight;

			// Give the player the ammo from having this item
			if(player[j].ammo[item->giTag] >= 0)
				player[j].ammo[item->giTag] += item->quantity * boxes;
		}
	}

	// Make sure no weapon has more than the maximum allowed ammo
	for(i = 0; i < NUM_DEFAULT_PLAYERS; i++)
	{
		for(j = 1; j < WP_NUM_WEAPONS; j++)
		{
			if(player[i].ammo[j] > AMMO_MAX)
				player[i].ammo[j] = AMMO_MAX;
		}
	}

	// Precompute some data structures
	for(i = 0; i < NUM_DEFAULT_PLAYERS; i++)
	{
		base_rs = &player[i];
		ResourceComputeFirstWeapon(base_rs);
		ResourceComputeHealthMod(base_rs);
		ResourceComputeDamageMod(base_rs);
	}

	// Compute the base score values when no items are picked up
	for(i = 0; i < NUM_DEFAULT_PLAYERS; i++)
	{
		// Compute the total points the player earns while alive
		memcpy(&rs, &player[i], sizeof(resource_state_t));
		ResourcePredictEncounter(&rs, LIFE_EXPECTANCY_MAX, 1.0, ENCOUNTER_RATE_DEFAULT, ENCOUNTER_RATE_DEFAULT);
		score[i] = rs.score;
	}

	// Create a generic item instance for each item type
	//
	// NOTE: Only the values required by ResourceAddItem() are filled out
	memset(&generic_entity, 0, sizeof(gentity_t));
	generic_entity.count = 0;
	generic_entity.flags = 0;

	// Compute base score values for each item type on the level (compared to
	// not picking up the item)
	for(i = 1; i < bg_numItems; i++)
	{
		// Assume the item has no resource type and does not produce points
		respawn = 0;
		pickup = 0.0;

		// Ignore items not present on the level
		if(item_value[i] < 0.0)
			continue;

		// Determine the item's default respawn time.
		//
		// NOTE: Items that don't respawn (like flags) are ignored because
		// they are not really campable, and do not continually add resources
		// to the game that affect the scores of players.
		item = &bg_itemlist[i];
		respawn = BaseItemRespawn(item);
		if(respawn <= 0)
			continue;

		// Holdable items are also not valued because of how the holdable item game
		// mechanic works in Quake 3.  Each item does a very different thing, players
		// only get one, and it's not clear when a player wants one.  It's certainly
		// not clear when an opponent wants one.  That said, there is nothing
		// algorithmically wrong with valuing holdables (and letting bots time their
		// respawns).  It's just that in Quake 3, doing so has little value.
		if(item->giType == IT_HOLDABLE)
			continue;

		// Create a generic instance of this item
		generic_entity.item = item;
		generic_entity.s.modelindex = i;

		// When processing ammo, cache the percentage of weapon pickups on the
		// level that let players spend this kind of ammo
		isAmmo = (item->giType == IT_AMMO);
		if(isAmmo)
			weight = weapon_frequency[item->giTag] / (float)num_weapons;

		// Each default player values the item differently.  Average the value
		// to each kind of player together to find the real item value.
		//
		// NOTE: You could argue that the item would most likely go to whomever
		// could use it the most, since they'd go through the most effort to pick
		// it up (meaning the maximum would make the most sense).  However,
		// everyone wants to grab the item just to take it from whomever really
		// wants it.  The average estimate isn't perfect but it's closer to reality.
		for(j = 0; j < NUM_DEFAULT_PLAYERS; j++)
		{
			// Compute the player's score for the same duration after picking up the item
			memcpy(&rs, &player[j], sizeof(resource_state_t));
			ResourceItemChange(&rs, ResourceAddItem(&rs, &generic_entity), player[j].health, player[j].armor);
			ResourcePredictEncounter(&rs, LIFE_EXPECTANCY_MAX, 1.0, ENCOUNTER_RATE_DEFAULT, ENCOUNTER_RATE_DEFAULT);

			// The value of the item to the player is the number of additional
			// points it gives the player over not picking it up
			//
			// NOTE: Technically this estimate is a bit high, because it will
			// always cost the player time to pickup the item, and the quantity
			// and value of that time is unknown.
			score_change = rs.score - score[j];

			// If the item is really that bad, assume this player will avoid it
			if(score_change < 0)
				continue;

			// Score adjust ammo, since players might not have the associated weapon
			// (even though the code assumes all non-respawn players have all weapons
			// for prediction's sake).
			//
			// FIXME: Yes, this is a hack.  Technically the code should handle a
			// probabilitist weapon value (eg. having .58 rocket launchers means
			// a 58% chance of having the rocket launcher and a 42% chance of not
			// having it) rather than simple digital true/false.  If that change is
			// even possible to code, it would be a lot of work, and it's not clear
			// it's worth the effort.
			if(isAmmo)
			{
				// Compute the chance this player will have the weapon to use this ammo
				//
				// NOTE: Even players without any weapon pickups can always use the
				// weapons with which they spawn.
				if(default_weapons[j])
					hasWeapon = 1.0 - pow_int(1.0 - weight, default_weapons[j]);
				else
					hasWeapon = (weapon_stats[item->giTag].start_ammo ? 1.0 : 0.0);

				// Only award ammo points for the percentage of the time the player has the
				// weapon to spend this ammo.
				score_change *= hasWeapon;
			}

			// Account for this player's contribute to the item's valuation
			item_value[i] += score_change * default_distribution[j];
		}

		// Estimate how often this item will be picked up
		pickup = ItemPickup(item);

		// If an item respawns faster than players want to pick it up, points are
		// only "lost" if someone else wanted to pick it up while the item was still
		// gone.  The rest of the time no points are lost, so scale the differential
		// value accordingly
		if(respawn < pickup)
			item_value[i] *= respawn / pickup;
	}

#ifdef DEBUG_AI
	// Output debug information if requested
	if(bot_debug_item.integer)
	{
		// Print out the base item value header
		BotAI_Print(PRT_MESSAGE, "Base Item Values:\n");

		// Print out the base value of each item
		for(i = 1; i < bg_numItems; i++)
		{
			// Ignore non-items
			item = &bg_itemlist[i];
			if(!item->pickup_name)
				continue;

			// Ignore items that aren't on the level
			if(item_value[i] < 0)
				continue;

			// Print out the item's value
			BotAI_Print(PRT_MESSAGE, "  %s: %f\n", item->pickup_name, item_value[i]);
		}
	}
#endif
}
