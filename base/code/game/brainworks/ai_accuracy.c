// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_accuracy.c
 *
 * Functions the bot uses to estimate its combat accuracy
 *****************************************************************************/

#include "ai_main.h"
#include "ai_vars.h"
#include "ai_accuracy.h"

#include "ai_weapon.h"


// Mapping from zone ids to distances and pitches
float           dist_zone_center[ZCD_NUM_IDS] = { ZCD_NEAR, ZCD_MID, ZCD_FAR, ZCD_VERYFAR };
float           pitch_zone_center[ZCP_NUM_IDS] = { -ZCP_LOW, 0, ZCP_LOW };

// Default accuracy statistics for each weapon
bot_accuracy_t  acc_default_weapon[WP_NUM_WEAPONS];

// Default accuracy statistics for each weapon in each combat zone
bot_accuracy_t  acc_default_weap_zone[WP_NUM_WEAPONS][ZCD_NUM_IDS][ZCP_NUM_IDS];


/*
==============
AccuracyCreate

Fills out the inputted accuracy record with the
inputted data.  If this code were written in C++,
this would be a constructor.
==============
*/
void AccuracyCreate(bot_accuracy_t * acc, int weapon, float shots,
					float direct_hits, float splash_hits, float total_splash_damage,
					float actual_fire_time, float potential_fire_time)
{
	weapon_stats_t *ws;

	// Initialize shot data
	ws = &weapon_stats[weapon];
	acc->shots = shots;

	// Some weapons create multiple shots each time the weapon is
	// fired, so the fire time must be divided between each shot
	//
	// NOTE: This intentionally ignores the potential of hasted reload times.
	acc->time = shots * ws->reload / ws->shots;

	// Initialize hits and damage as appropriate
	//
	// NOTE: Direct damage is computed from the weapon stats, but splash damage
	// must be supplied by the caller.  If a mod creates a weapon which has variable
	// damage (often but not always a bad game design idea), then this function will
	// need to be reworked to accept a total direct damage argument as well.
	acc->direct.hits = direct_hits;
	acc->direct.damage = direct_hits * ws->damage;
	acc->splash.hits = splash_hits;
	acc->splash.damage = total_splash_damage;
	acc->attack_rate.actual = actual_fire_time;
	acc->attack_rate.potential = potential_fire_time;
}

/*
=============
AccuracyTally

Add the data from the input accuracy record to
the total record.
=============
*/
void AccuracyTally(bot_accuracy_t * total, bot_accuracy_t * acc)
{
	total->shots += acc->shots;
	total->time += acc->time;
	total->direct.hits += acc->direct.hits;
	total->direct.damage += acc->direct.damage;
	total->splash.hits += acc->splash.hits;
	total->splash.damage += acc->splash.damage;
	total->attack_rate.actual += acc->attack_rate.actual;
	total->attack_rate.potential += acc->attack_rate.potential;
}

/*
=============
AccuracyScale

Scale the data in one accuracy record by a floating
point value and the result in result.  Result may point
to the input accuracy record.  Returns a pointer
to the result record.
=============
*/
bot_accuracy_t *AccuracyScale(bot_accuracy_t * acc, float scale, bot_accuracy_t * result)
{
	result->shots = scale * acc->shots;
	result->time = scale * acc->time;
	result->direct.hits = scale * acc->direct.hits;
	result->direct.damage = scale * acc->direct.damage;
	result->splash.hits = scale * acc->splash.hits;
	result->splash.damage = scale * acc->splash.damage;
	result->attack_rate.actual = scale * acc->attack_rate.actual;
	result->attack_rate.potential = scale * acc->attack_rate.potential;

	return result;
}

/*
===================
AccuracyZoneAverage

Given a weapon and combat zone description, computes the accuracy
record that's the weighted average as described by that zone and
stores it (result).  Uses the bot's known accuracy data if a non-null
bot pointer was supplied.  Otherwise uses the default accuracy data.
Returns a pointer to the result structure if you're into that kind of thing.

NOTE: This is one of the few functions that works even with a null
bot state pointer.  That kind of uniqueness makes me think something is
wrong and the function should be structured differently.  However, the
compiler really doesn't like passing in arguments like
  bs->acc_weap_zone[weapon]
because of how that array is internally stored.
===================
*/
bot_accuracy_t *AccuracyZoneAverage(bot_state_t * bs, int weapon, combat_zone_t * zone, bot_accuracy_t * result)
{
	int             i;
	zone_center_t  *center;
	bot_accuracy_t  scaled;

	// Reset the contents of the output accuracy before tallying data from zone centers
	memset(result, 0, sizeof(bot_accuracy_t));

	// Use a portion from each zone center accuracy record
	for(i = 0; i < zone->num_centers; i++)
	{
		// Look up the accuracy record associated with the current combat zone center
		center = &zone->center[i];

		// Add a fraction of this accuracy record to the output total
		if(bs)
			AccuracyScale(&bs->acc_weap_zone[weapon][center->dist][center->pitch], zone->weight[i], &scaled);
		else
			AccuracyScale(&acc_default_weap_zone[weapon][center->dist][center->pitch], zone->weight[i], &scaled);
		AccuracyTally(result, &scaled);
	}

	// Make functional programmers happy
	return result;
}

/*
================
ZoneCenterWeight

Given an input distance to a target and a sorted list of
zone centers (either distances or pitch angles, whose list
indicies equal their zone ids), this function determines
which two centers the input distance lies between.  It
also computes how closely the input distance is weighted
towards the first center (so the weighting for the second
center will be 1.0 minus this weight).  If the input
distance is less than the first list value or greater than
the last list value, the weight value is 1.0 and the
second center id will be -1 (no center).
================
*/
void ZoneCenterWeight(float value, float *centers, int num_centers, int *first_id, int *second_id, float *weight)
{
	int             index;
	qboolean        found;
	float          *match;

	// Check where this distance would fall in the sorted distance list--
	// The value lies between index-1 and index.
	//
	// NOTE: Index will equal num_centers if the value is greater than
	// the last distance in the center list.
	found = bsearch_addr(&value, centers, num_centers, sizeof(float), CompareEntryFloat, (void *)&match);
	index = match - centers;

	// If an exact match wasn't found and the index is internal to the list's
	// interval, compute the weighting between the two nearest centers
	if(index > 0 && index < num_centers && !found)
	{
		*first_id = index - 1;
		*second_id = index;
		*weight = (centers[index] - value) / (centers[index] - centers[index - 1]);
	}

	// Otherwise just use the nearest (or matching) interval
	else
	{
		// For indicies off the end the list, use the last valid list index
		if(index >= num_centers)
			index = num_centers - 1;

		// Just use one center id with a weight of 1.0
		*first_id = index;
		*second_id = -1;
		*weight = 1.0;
	}
}

/*
================
CombatZoneCreate

Given an input distance and pitch, creates a combat zone
description, interpolated from nearby combat zone centers.
================
*/
void CombatZoneCreate(combat_zone_t * zone, float dist, float pitch)
{
	int             centers;
	int             dist_id_start, dist_id_end, pitch_id_start, pitch_id_end;
	float           dist_weight, pitch_weight;

	// Set the input distance and pitch
	zone->dist = dist;
	zone->pitch = pitch;

	// Determine which distance and pitch zone centers to
	// average between and their weights
	ZoneCenterWeight(dist, dist_zone_center, ZCD_NUM_IDS, &dist_id_start, &dist_id_end, &dist_weight);
	ZoneCenterWeight(pitch, pitch_zone_center, ZCP_NUM_IDS, &pitch_id_start, &pitch_id_end, &pitch_weight);

	// Create entry for the first center
	centers = 0;
	zone->center[centers].dist = dist_id_start;
	zone->center[centers].pitch = pitch_id_start;
	zone->weight[centers++] = dist_weight * pitch_weight;

	// Create a second dist entry if necessary
	if(dist_id_end != ZCD_ID_NONE)
	{
		zone->center[centers].dist = dist_id_end;
		zone->center[centers].pitch = pitch_id_start;
		zone->weight[centers++] = (1.0 - dist_weight) * pitch_weight;
	}

	// Create a second pitch entry if necessary
	if(pitch_id_end != ZCP_ID_NONE)
	{
		zone->center[centers].dist = dist_id_start;
		zone->center[centers].pitch = pitch_id_end;
		zone->weight[centers++] = dist_weight * (1.0 - pitch_weight);
	}

	// Add the fourth dist end / pitch end entry if necessary
	if(dist_id_end != ZCD_ID_NONE && pitch_id_end != ZCP_ID_NONE)
	{
		zone->center[centers].dist = dist_id_end;
		zone->center[centers].pitch = pitch_id_end;
		zone->weight[centers++] = (1.0 - dist_weight) * (1.0 - pitch_weight);
	}

	// Save the actual number of centers in the zone average
	zone->num_centers = centers;
}

/*
================
CombatZoneInvert

Think of a combat zone as a description of a target
relative to a player's position.  This function
inverts that description so that it describes the
player's position relative to the target.  It's
permitted for "source" and "inverted" to point to
the same structure.
================
*/
void CombatZoneInvert(combat_zone_t * source, combat_zone_t * inverted)
{
	int             i;
	zone_center_pitch *pitch;

	// Most of the data remains unchanged
	if(inverted != source)
		memcpy(inverted, source, sizeof(combat_zone_t));

	// Invert the pitch value and zones
	inverted->pitch = -inverted->pitch;
	for(i = 0; i < inverted->num_centers; i++)
	{
		pitch = &inverted->center[i].pitch;
		if(*pitch == ZCP_ID_LOW)
			*pitch = ZCP_ID_HIGH;
		else if(*pitch == ZCP_ID_HIGH)
			*pitch = ZCP_ID_LOW;
	}
}

/*
========================
BotWeaponExtraReloadTime

Returns the amount of additional time the bot's weapon
will have to wait to reload, beyond what the bot was
expecting last AI frame.  The time is returned in seconds.
========================
*/
float BotWeaponExtraReloadTime(bot_state_t * bs)
{
	int             est_reload_ms, next_reload_ms;
	float           extra_reload_time;

	// If the weapon is already reloaded, there is no additional time to be detected
	if(bs->ps->weaponTime <= 0)
		return 0.0;

	// If the bot thought the weapon was reloaded last frame, it also thought it
	// would be reloaded this frame
	if(bs->last_reload_delay_ms <= 0)
		est_reload_ms = server_time_ms + bs->last_reload_delay_ms;
	else
		est_reload_ms = bs->last_command_time_ms + bs->last_reload_delay_ms;

	// Figure out when the weapon will actually reload
	next_reload_ms = server_time_ms + bs->ps->weaponTime;

	// Check how much additional reload time the bot's weapon accrued since the last update
	extra_reload_time = (next_reload_ms - est_reload_ms) * 0.001;
	if(extra_reload_time <= 0.0)
		return 0.0;

	return extra_reload_time;
}

/*
=================
BotWeaponFireTime

Computes the additional amount of time spent firing beyond what
the bot was expecting last AI frame.  Also computes the amount of
time that could have been spent firing.  The times are computed
in seconds.
=================
*/
void BotWeaponFireTime(bot_state_t * bs, history_t * fire_time)
{
	int             weapon;

	// Determine how much time has elapsed since the last analysis
	//
	// NOTE: This bound check is not redundant.  The analysis time
	// will refer to a point in the future when the bot analyzes the
	// reload time for a shot.  A 1 second reload incurred at time T
	// will cause this code to finish its analysis for time T+1.
	fire_time->potential = server_time - bs->weapon_analysis_time;
	if(fire_time->potential < 0.0)
		fire_time->potential = 0.0;

	// Determine how much additional weapon reload time has not
	// been accounted for
	fire_time->actual = BotWeaponExtraReloadTime(bs);

	// If the bot incurred a reload longer than the actual amount of
	// time elapsed, consider all that time analyzed
	if(fire_time->potential < fire_time->actual)
		fire_time->potential = fire_time->actual;

	// Find out what weapon the bot used last server frame
	//
	// NOTE: Accuracy data must check the bot's current weapon (bs->ps->weapon),
	// not the bot's selected weapon (bs->weapon).
	weapon = bs->ps->weapon;
	if(weapon <= WP_NONE || weapon >= WP_NUM_WEAPONS)
		weapon = WP_NONE;

	// Incur fire time for melee weapons that didn't officially reload
	// (since melee weapons don't reload unless they hit), assuming
	// the weapon was actually firing
	//
	// NOTE: This intentially checks the bot's currently equipped weapon,
	// not the selected weapon (which is bs->weapon)
	if(fire_time->actual < fire_time->potential &&
	   (weapon_stats[weapon].flags & WSF_MELEE) && (bs->ent->client->pers.cmd.buttons & BUTTON_ATTACK))
	{
		fire_time->actual = fire_time->potential;
	}

	// Account for the time analyzed
	bs->weapon_analysis_time += fire_time->potential;

	// There are a number of reasons this reload time is not fire time:
	//
	// - The bot is not holding a real weapon
	// - The bot is changing weapons
	// - The bot wasn't aiming at an enemy
	// - The bot died
	// - Ignore perceived shots when the bot just used an item
	//
	// See the NOTEs below for more detailed descriptions of what bugs
	// in the server require this logic.
	if((weapon == WP_NONE) ||
	   (BotWeaponChanging(bs)) || (!bs->aim_enemy) || (BotIsDead(bs)) || (bs->ps->pm_flags & PMF_USE_ITEM_HELD))
	{
		fire_time->actual = 0.0;
		fire_time->potential = 0.0;
	}

	// Never update when dead:
	//
	// NOTE: It's theoretically possible to record accuracy data when the
	// bot is dead.  If the bot dies the same frame it tried to shoot,
	// it will be dead now, but still might have gotten a shot off (and
	// maybe hit).  Unfortunately, a bug in the Quake 3 server code prevents
	// this from being easily done.
	//
	// This is because Quake 3 updates each player in order, first moving
	// the player and then shooting.  So if a bot with client number 1 is
	// killed by client 0, the bot won't get to shoot-- it will be dead
	// before it's ClientThink() executes.  (And because of another bug in
	// PM_Weapon() in bg_pmove.c, the bot's ps->weaponTime value will not
	// decrease, so it appears as if another shot has been taken by the
	// dead bot.)  But if the bot is killed by client 2, the bot will still
	// have gotten the shot off, and in theory the accuracy data could be
	// tracked.  Some extra effort is needed, however, because the bot's
	// current weapon (bs->ps->weapon) will be set to WP_NONE, even though
	// the shot would have been made with the old weapon, presumably
	// bs->weapon.  But most of the time bs->ps->weapon should be used
	// (see below).
	//
	// Unfortunately, the only way to track this is to check who killed
	// the bot and compare client numbers.  And there's still the issue
	// of ps->weaponTime not being effectively updated.  (Usually the bug
	// causes a 50ms accrual difference, but this value could be larger
	// if the server was processor lagged, so the value is unpredictable.)
	//
	// NOTE: These issues only apply to instant hit weapons, which can only
	// be detected by their reload times.  The bot tracks accuracy data for
	// missiles in BotTrackMissileShot() in ai_scan.c.
	//
	// FIXME: The game server should really reset ent->ps.weaponTime when a
	// player dies.


	// Ignore extra reload time when the bot uses an item
	//
	// NOTE: A bug in PM_Weapon() in bg_pmove.c causes the code to not decay
	// bs->ps->weaponTime if the player uses an item.  This is perceived by the
	// AI code as an time increase, which could be inappropriately be read as a
	// shot.  As such, all shots read when the bot uses an item are ignored.
	// Incidently, players are not allowed to use a holdable item and shoot at
	// the same time
	//
	// FIXME: Someone at id Software should fix this server bug.
}

/*
===============
BotAccuracyRead

Reads accuracy data for the weapon and zone pair.  The
weapon argument must be specified (ie. not WP_NONE), but
the zone argument may be ommited (ie. NULL).  The read data
is stored in the inputted accuracy record, and the data
is padded with extra default data if not enough real
information has been collected.
===============
*/
void BotAccuracyRead(bot_state_t * bs, bot_accuracy_t * acc, int weapon, combat_zone_t * zone)
{
	float           time;
	bot_accuracy_t  default_acc;

	// Average over the specified zones if zone data was supplied ...
	if(zone)
		AccuracyZoneAverage(bs, weapon, zone, acc);
	// ... Otherwise just read the appropriate weapon data
	else
		memcpy(acc, &bs->acc_weapon[weapon], sizeof(bot_accuracy_t));

	// Check if default data must get added
	time = ACCURACY_DEFAULT_TIME - acc->time;
	if(time <= 0.0)
		return;

	// The default data only applies for weapons in range;
	// Add time but no extra hits or damage for out of range weapons
	if(!WeaponInRange(weapon, zone->dist))
	{
		acc->time = time;
		return;
	}

	// Use a portion of each default accuracy zone center if specified ...
	if(zone)
		AccuracyZoneAverage(NULL, weapon, zone, &default_acc);
	// .. Otherwise just read the default weapon data
	else
		memcpy(&default_acc, &acc_default_weapon[weapon], sizeof(bot_accuracy_t));

	// Add that many seconds of default data to the input record
	AccuracyScale(&default_acc, time, &default_acc);
	AccuracyTally(acc, &default_acc);
}

#ifdef DEBUG_AI
/*
==================
PrintWeaponAccInfo
==================
*/
void PrintWeaponAccInfo(bot_state_t * bs, int weapon)
{
	int             pitch, dist;
	float           actual, potential, hit_damage;
	float           actual_error, potential_error;
	bot_accuracy_t *acc;
	char           *level_name;

	// Print a nice header explaining the table layout
	G_Printf("%.2f %s %s ^4Accuracy^7:  Near,  Mid,  Far, Very Far\n", server_time, EntityNameFast(bs->ent), WeaponName(weapon));

	// Compute and print out the actual percentage of potential damage dealt
	// for each pitch and distance zone center
	hit_damage = weapon_stats[weapon].damage;
	actual_error = potential_error = 0.0;
	for(pitch = 0; pitch < ZCP_NUM_IDS; pitch++)
	{
		// Get a name for this level
		switch (pitch)
		{
			default:
			case ZCP_ID_LOW:
				level_name = "  Low";
				break;
			case ZCP_ID_LEVEL:
				level_name = "Level";
				break;
			case ZCP_ID_HIGH:
				level_name = " High";
				break;
		}

		// Print the level name and each distance accuracy for that level
		G_Printf(" %s:", level_name);
		for(dist = 0; dist < ZCD_NUM_IDS; dist++)
		{
			// Determine the actual damage done and the maximum potential damage
			acc = &bs->acc_weap_zone[weapon][dist][pitch];
			actual = acc->direct.damage + acc->splash.damage;
			potential = acc->shots * hit_damage;

			// Check for accuracy errors
			if((potential > 0) && (actual / potential > 1.0 + 1e-5))
			{
				actual_error = actual;
				potential_error = potential;
			}

			// Print accuracy data, avoiding divides by zero
			if(potential > 0)
				G_Printf(" %2.f%%", 100 * actual / potential);
			else
				G_Printf(" ??%%");

			// Also print the time spent acquiring the data and a seaparator
			G_Printf(" (%3.2f)", acc->time);
			if(dist < ZCD_NUM_IDS - 1)
				G_Printf(", ");
		}
		G_Printf("\n");
	}

	// Display an error message if appropriate
	if(actual_error > potential_error)
		G_Printf("  ^1WARNING: Actual damage ^2(%f)^1 exceeds potential damage ^2(%f)^7\n", actual_error, potential_error);

	G_Printf("\n");
}

/*
===================
PrintWeaponFireInfo

NOTE: If this were C++, this function would be merged
with PrintWeaponAccInfo() and the call syntax would require
data accessor functions for potential and actual values.
I suppose you can do that in C as well, but it's much
more of a pain without a class-based infrastructure.
===================
*/
void PrintWeaponFireInfo(bot_state_t * bs, int weapon)
{
	int             pitch, dist;
	float           actual, potential;
	bot_accuracy_t *acc;
	char           *level_name;

	// Print a nice header explaining the table layout
	G_Printf("%.2f %s %s ^1Firing^7:  Near,  Mid,  Far, Very Far\n", server_time, EntityNameFast(bs->ent), WeaponName(weapon));

	// Compute and print out the actual percentage of potential firing time
	// for each pitch and distance zone center
	for(pitch = 0; pitch < ZCP_NUM_IDS; pitch++)
	{
		// Get a name for this level
		switch (pitch)
		{
			default:
			case ZCP_ID_LOW:
				level_name = "  Low";
				break;
			case ZCP_ID_LEVEL:
				level_name = "Level";
				break;
			case ZCP_ID_HIGH:
				level_name = " High";
				break;
		}

		// Print the level name and each distance accuracy for that level
		G_Printf(" %s:", level_name);
		for(dist = 0; dist < ZCD_NUM_IDS; dist++)
		{
			// Determine the actual time spent firing and the maximum potential fire time
			acc = &bs->acc_weap_zone[weapon][dist][pitch];
			actual = acc->attack_rate.actual;
			potential = acc->attack_rate.potential;

			// Print accuracy data, avoiding divides by zero
			if(potential > 0)
				G_Printf(" %2.f%%", 100 * actual / potential);
			else
				G_Printf(" ??%%");

			// Also print the time spent acquiring the data and a seaparator
			G_Printf(" (%3.2f)", potential);
			if(dist < ZCD_NUM_IDS - 1)
				G_Printf(", ");
		}
		G_Printf("\n");
	}

	G_Printf("\n");
}
#endif

/*
=================
BotAccuracyRecord

Record whether or not the bot hit an enemy when
it took a shot from the specified location.
=================
*/
void BotAccuracyRecord(bot_state_t * bs, bot_accuracy_t * acc, int weapon, combat_zone_t * zone)
{
	int             i;
	zone_center_t  *center;
	bot_accuracy_t  zone_acc;

#ifdef DEBUG_AI
	float           direct_acc, splash_acc, splash_damage, misses;
	char           *direct_name, *separate;
#endif

	// Add this to the total damage the bot has dealt
	bs->damage_dealt += acc->direct.damage + acc->splash.damage;

	// Add the record to the weapon aggregate total and the complete aggregate
	AccuracyTally(&bs->acc_weapon[weapon], acc);

	// Divide the record into a portion for each center in the combat zone
	for(i = 0; i < zone->num_centers; i++)
	{
		// Compute the portion allocated for this center
		center = &zone->center[i];
		AccuracyScale(acc, zone->weight[i], &zone_acc);

		// Add this portion to the specific instance total
		AccuracyTally(&bs->acc_weap_zone[weapon][center->dist][center->pitch], &zone_acc);
	}

#ifdef DEBUG_AI
	// Print accuracy statistics when requested
	if(bs->debug_flags & BOT_DEBUG_INFO_ACCSTATS)
		PrintWeaponAccInfo(bs, weapon);

	// Print firing statistics when requested
	if(bs->debug_flags & BOT_DEBUG_INFO_FIRESTATS)
		PrintWeaponFireInfo(bs, weapon);

	// Only give accuracy debug messages when requested
	if(!(bs->debug_flags & BOT_DEBUG_INFO_ACCURACY))
		return;

	// Print a description of the zone and weapon
	BotAI_Print(PRT_MESSAGE, "%s: Accuracy with %s (at %.f away, %.f %s): ",
				EntityNameFast(bs->ent), WeaponName(weapon),
				zone->dist, fabs(zone->pitch), (zone->pitch < 0.0 ? "below" : "above"));

	// Compute direct hit accuracy with weapon
	BotAccuracyRead(bs, &zone_acc, weapon, zone);
	if(zone_acc.shots)
		direct_acc = 100 * zone_acc.direct.hits / zone_acc.shots;
	else
		direct_acc = 0.0;

	// Print direct accuracy data
	G_Printf("%0.2f%% ", direct_acc);

	// Compute splash accuracy and average splash damage
	if(zone_acc.splash.hits)
	{
		splash_acc = 100 * zone_acc.splash.hits / zone_acc.shots;
		splash_damage = zone_acc.splash.damage / zone_acc.splash.hits;
	}
	else
	{
		splash_acc = 0;
		splash_damage = 0;
	}

	// Add splash accuracy data if necessary; qualify direct hits as "direct"
	// if splash hits are possible
	if(weapon_stats[weapon].radius || splash_acc)
	{
		G_Printf("direct, %0.2f%% splash (%.f avg damage) ", splash_acc, splash_damage);
		direct_name = "direct hit";
	}
	else
	{
		direct_name = "hit";
	}

	// Print a description of the input accuracy record
	G_Printf("(");
	separate = "";

	// Print the direct hits
	if(acc->direct.hits > 0)
	{
		G_Printf("%.f %s%s", acc->direct.hits, direct_name, (acc->direct.hits == 1.0 ? "" : "s"));
		separate = ", ";
	}

	// Print the splash hits
	if(acc->splash.hits > 0)
	{
		G_Printf("%s%.f splash hit%s", separate, acc->splash.hits, (acc->splash.hits == 1.0 ? "" : "s"));
		separate = ", ";
	}

	// Print the misses
	misses = acc->shots - (acc->direct.hits + acc->splash.hits);
	if(misses > 0)
		G_Printf("%s%.f miss%s", separate, misses, (misses == 1.0 ? "" : "es"));

	// Finish the line of printing
	G_Printf(")\n");
#endif
}

/*
================
BotAccuracyReset

Reset the bot's accuracy tracking.  This should probably only be done
when a bot is loaded, or else bots will lose otherwise good statistical
information.  But if the statistics somehow become meaningless, it might
be worth resetting them or toning them down somehow.
================
*/
void BotAccuracyReset(bot_state_t * bs)
{
	// Reset all of the bot's accuracy tracking data
	bs->weapon_analysis_time = server_time;
	memset(&bs->acc_weap_zone, 0, sizeof(bs->acc_weap_zone));
	memset(&bs->acc_weapon, 0, sizeof(bs->acc_weapon));
}

/*
========================
BotAccuracyUpdateMissile

Returns the number of hit counter ticks
attributable to missile fire
========================
*/
int BotAccuracyUpdateMissile(bot_state_t * bs)
{
	int             i, exploded, event, hits;
	bot_missile_shot_t *shot;
	gentity_t      *bolt, *target;
	bot_accuracy_t  acc;
	damage_multi_t  blast;
	qboolean        enemy_target, team_target;

	// Number of missiles that exploded and won't be tracked after this frame
	exploded = 0;

	// The number of hit counter ticks attributable to missiles this frame
	hits = 0;

	// Loop through list looking for any exploded missiles
	for(i = 0; i < bs->num_own_missiles; i++)
	{
		// If this missile is not valid, remove it from the list
		//
		// NOTE: This occurs when the missile contacts a sky plane.  It doesn't
		// blow up; it's just immediately deleted.
		shot = &bs->own_missiles[i];
		bolt = shot->bolt;
		if(!bolt->inuse || bolt->r.ownerNum != bs->client)
		{

			// Record this shot as a complete miss
			AccuracyCreate(&acc, shot->weapon, 1, 0, 0, 0, 0, 0);
			BotAccuracyRecord(bs, &acc, shot->weapon, &shot->zone);

			// Remove it from the list
			exploded++;
			continue;
		}

		// If the missile hasn't exploded yet, continue tracking it for later
		event = bolt->s.event & ~EV_EVENT_BITS;
		if(event != EV_MISSILE_HIT && event != EV_MISSILE_MISS && event != EV_MISSILE_MISS_METAL)
		{
			// Move this valid record to the proper list position
			if(exploded)
				memcpy(&bs->own_missiles[i - exploded], shot, sizeof(bot_missile_shot_t));
			continue;
		}

		// Check whom, if anyone, this missile directly hit
		//
		// NOTE: The server code does not provide enough information to determine
		// when the missile directly hits a non-player target, like an Obelisk in
		// Overload.  This means the blast damage code will not know to ignore such
		// a target, so direct hits on the object will be tracked as splash damage,
		// which could cause issues with weapon selection if the bot's enemy has
		// something that prevents splash damage (like the battle suit).  It also
		// causes issues with missiles whose blast damage doesn't equal their
		// direct damage (like Plasma).  See G_MissileImpact() in g_missile.c for
		// more information.
		if(event == EV_MISSILE_HIT)
			target = &g_entities[bolt->s.otherEntityNum];
		else
			target = NULL;

		// Determine if this target is an enemy or a teammate
		enemy_target = BotEnemyTeam(bs, target);
		team_target = BotSameTeam(bs, target);

		// Estimate the amount of blast damage this missile dealt (and blast hits scored)
		BotBlastDamage(bs, shot->weapon, bolt->r.currentOrigin, &blast, target);

		// Adjust the hit counter for direct hits...
		if(enemy_target)
			hits++;
		else if(g_friendlyFire.integer && team_target)
			hits--;

		// ... and for blast hits
		hits += blast.enemy.hits;
		hits -= blast.team.hits;


		// Determine the most damage this missile dealt to a single enemy
		//
		// NOTE: Even though multiple hits are tracked by the server's hit
		// tally counter (see blast.enemy.hits), this code only tracks the
		// most damaging shot for the purpose of accuracy records.  This
		// avoids any potential issues that could occur if the total damage
		// dealt is greater than 100% of potential damage against a single
		// target.

		// First check for direct hits on an enemy
		if(BotEnemyTeam(bs, target))
			AccuracyCreate(&acc, shot->weapon, 1, 1, 0, 0, 0, 0);

		// Check for enemy blast damage as well
		else if(blast.enemy.hits)
			AccuracyCreate(&acc, shot->weapon, 1, 0, 1, blast.enemy.max, 0, 0);

		// The shot completely missed enemies
		else
			AccuracyCreate(&acc, shot->weapon, 1, 0, 0, 0, 0, 0);

		// Record this shot and remove it from the list
		BotAccuracyRecord(bs, &acc, shot->weapon, &shot->zone);
		exploded++;
	}

	// Record the new (possibly lower) number of tracked missiles
	bs->num_own_missiles -= exploded;

	// Return the number of hits actually caused by missiles
	return hits;
}

/*
=======================
BotAccuracyUpdateWeapon

Returns the number of hit counter ticks
attributable to instant hit weapon fire.

"fire_time" is the actual and potential seconds
of firing time the bot had since it last processed
accuracies.

"hits" is the number of unaccounted hits
the bot detected last frame.
=======================
*/
void BotAccuracyUpdateWeapon(bot_state_t * bs, history_t * fire_time, int hits)
{
	int             fires, shots, weapon;
	float           reload_rate;
	bot_accuracy_t  acc;
	weapon_stats_t *ws;

	// Do nothing if no actual opportunity to attack occurred
	if(fire_time->potential <= 0.0)
		return;

	// Only compute accuracy data for instant hit weapons.
	// Missile weapons aren't tracked here-- their accuracies can only be
	// updated once the missile explodes.
	weapon = bs->ps->weapon;
	ws = &weapon_stats[weapon];
	if(!ws->speed)
	{
		// Look up the weapon's reload rate relative to the bot
		//
		// NOTE: The division by zero check shouldn't be necessary, but why take chances?
		reload_rate = ws->reload;
		if(bs->weapon_rate > 0.0)
			reload_rate /= bs->weapon_rate;

		// Check how many times the bot's weapon shot
		//
		// NOTE: This is intentially a floating point value.  Because of the dark
		// voodoo that governs the (non-)firing of melee weapons, the accuracy data
		// must track partial firings of the weapon whenever the fire button is held
		// down but no target has been hit.
		fires = fire_time->actual / reload_rate;
		shots = fires * ws->shots;

		// Sanity-bound the number of hits the bot detected to the number of shots taken
		// Any extra hits must be from other sources, like telefrags.
		//
		// NOTE: It's possible for a railgun to damage two players in one shot,
		// but for the purposes of accuracy data, the bot never expects a shot to
		// deal more than 100% of the total damage possible against one target.
		if(hits > shots)
			hits = shots;

		// FIXME: If any instant-hit weapons dealt blast damage, code should be
		// inserted here to estimate that using BotBlastDamage().  Unfortunately,
		// there's currently no way to determine where an instant hit blast shot
		// exploded.  (You can't use traces because it's possible it exploded on
		// an entity that was killed by the blast.  It gets even worse when
		// shooting a weapon with spread.)  So if anyone adds code for instant
		// hit blast damage weapons, its their responsibility to define the
		// interface that communicates the blast location to the client.  Once that
		// interface is defined, code can be written to estimate blast damage.
	}
	else
	{
		// Track no shots or hits for missile weapons
		hits = 0;
		shots = 0;
	}


	// Record enemy information in the accuracy data structure
	AccuracyCreate(&acc, weapon, shots, hits, 0, 0, fire_time->actual, fire_time->potential);
	BotAccuracyRecord(bs, &acc, weapon, &bs->aim_zone);
}

/*
=================
BotAccuracyUpdate

This function processes the bot's missile and hitscan
fire data to track the bot's attack hits and misses.

The fundamental theme of this function (and the functions
it calls) is that correctly determining hits and misses
is almost impossible.  The server infrastructure simply
doesn't allow for it.  In fact, even determining whether
or not the bot's weapon fired is difficult.

The server uses the bs->ps->persistant[PERS_HITS] tally
counter to send damage ticks to the client.  When this
value is incremented, the client plays a *DING*.  It's
also decremented when a teammate is hurt.  So for example
if a bot uses the Kamikaze and damages one teammate and
one enemy, the tally counter will get -1 for the teammate
and +1 for the enemy, which will read as zero change.  So
there's no possible way for the bot to determine how many
hits were actually scored.  Similar problems can occur
when friendly fire is on, with a missile blast damaging
an enemy and an opponent.

There are other issues with missile fire.  Suppose the bot
fires a grenade and then switches to the machinegun.  It
shot last frame and hears a *DING*-- is that from the grenade
exploding or the machinegun shot?  It's very difficult to
determine.

And there are other issues as well, not described here.  If
you see anything in these functions that might not give
accurate data, rest assured that it bothers me too.  I do
the best I can, but I cannot modify the client/server
infrastructure.
=================
*/
void BotAccuracyUpdate(bot_state_t * bs)
{
	int             hits;
	history_t       fire_time;

	// Compute the potential and actual amount of fire time accrued since the last update
	BotWeaponFireTime(bs, &fire_time);

	// Check if the bot hit anything this frame
	hits = bs->ps->persistant[PERS_HITS] - bs->last_hit_count;

	// Process accuracy data from missiles, accounting for each hit caused
	// by missiles
	hits -= BotAccuracyUpdateMissile(bs);

	// Process weapon firing accuracy data (primarily instant hit weapons),
	// given the estimated number of hits this turn from instant hit weapons.
	//
	// NOTE: Technically this is just all hits that did not come from
	// missiles.  This hit count could also include things like kamikaze
	// and telefrag damage.
	BotAccuracyUpdateWeapon(bs, &fire_time, hits);

	// Update hit counter
	bs->last_hit_count = bs->ps->persistant[PERS_HITS];

	// Deduce what the weapon reload time should be when the next command is processed
	if(bs->ps->weaponTime <= 0)
		bs->last_reload_delay_ms = bs->ps->weaponTime;
	else
		bs->last_reload_delay_ms = bs->ps->weaponTime - (bs->cmd.serverTime - server_time_ms);

	// Determine how fast the bot's weapon will reload for next frame's shots
	//
	// NOTE: This is done after the accuracy updates because the server code
	// makes the players shoot before picking up items.  So if a player
	// picks up haste and shoots in the same frame, the shot made during that
	// frame will have the increased haste reload rate.  Conversely, for the
	// last frame of haste, the shots made during that frame will reload
	// faster, even though the haste will have worn off before this code
	// executes.  That's why it's important to cache the bot's weapon reload
	// rate for next frame
	//
	// NOTE: See PM_Weapon() in bg_pmove.c for more information
#ifdef MISSIONPACK
	if(bs->ps->powerups[PW_SCOUT])
		bs->weapon_rate = 1.5;
	else if(bs->ps->powerups[PW_AMMOREGEN])
		bs->weapon_rate = 1.3;
	else
#endif
	if(bs->ps->powerups[PW_HASTE])
		bs->weapon_rate = 1.3;
	else
		bs->weapon_rate = 1.0;
}

/*
=============
BotAttackRate

Estimate the percent of time in combat the bot
will fire the weapon associated with this accuracy
record (presumably corollated to a specific combat
zone and weapon).
=============
*/
float BotAttackRate(bot_state_t * bs, bot_accuracy_t * acc)
{
	// Compute the bot's attack rate with this weapon in this combat situation
	return acc->attack_rate.actual / acc->attack_rate.potential;
}

/*
=============
AccuracySetup

Resets default data for accuracy statistics
=============
*/
void AccuracySetup(void)
{
	int             weapon, dist_id, pitch_id;
	float           shots, direct_hits, splash_hits;
	float           direct_accuracy, splash_accuracy, splash_damage;
	float           base_attack_rate, pitch_attack_rate, attack_rate = 1.0f;
	float           actual_attack_time, potential_attack_time;
	float           range = 0.0f;
	weapon_stats_t *ws;
	qboolean        careless;

	// Initialize each weapon's accuracy
	for(weapon = WP_NONE + 1; weapon < WP_NUM_WEAPONS; weapon++)
	{
		// Estimate one second of weapon fire
		ws = &weapon_stats[weapon];
		shots = ws->shots / ws->reload;

		// Determine the weapon's accuracy for direct hits and splash
		if(ws->radius >= 100)
		{
			// NOTE: This averages to 1.0 when splash hits deal 50% damage
			direct_accuracy = ws->accuracy * 0.5;
			splash_accuracy = ws->accuracy * 1.0;
		}
		else
		{
			direct_accuracy = ws->accuracy;
			splash_accuracy = 0.0;
		}

		// Estimate how many hits would be scored in the specified period of time
		direct_hits = shots * direct_accuracy;
		splash_hits = shots * splash_accuracy;
		if(shots < direct_hits + splash_hits)
			splash_hits = shots - direct_hits;

		// Compute splash damage
		splash_damage = splash_hits * ws->splash_damage * .5;

		// Estimate the base percent of time the bot will attack with this weapon;
		// Careless fire weapons are naturally fired more often.
		careless = WeaponCareless(weapon);
		base_attack_rate = (careless ? 0.65 : 0.55);

		// Estimate the time spent to do one second of attacking
		actual_attack_time = 1.0;
		potential_attack_time = actual_attack_time / attack_rate;

		// Create a default accuracy record using this data
		AccuracyCreate(&acc_default_weapon[weapon],
					   weapon, shots, direct_hits, splash_hits, splash_damage, actual_attack_time, potential_attack_time);

		// Cache the weapon's perceived maximum range
		range = WeaponPerceivedMaxRange(range);

		// Create zone specific default accuracy data
		for(pitch_id = 0; pitch_id < ZCP_NUM_IDS; pitch_id++)
		{
			// Start with the base attack rate
			pitch_attack_rate = base_attack_rate;

			// Carefully fired slow missile weapons can be hard to aim
			if(!careless && ws->speed > 0 && ws->speed < 1200)
			{
				// Fire less when aiming high, and also when aiming low without
				// sufficient blast damage
				// NOTE: A negative pitch value refers to aiming above the horizon;
				// positive means aiming below.
				if((pitch_zone_center[pitch_id] <= -ZCP_LOW) ||
				   (pitch_zone_center[pitch_id] > -ZCP_LOW && (ws->splash_damage / ws->damage) < 0.5))
				{
					pitch_attack_rate *= 0.5;
				}
			}

			// Load the data for each distance zone
			for(dist_id = 0; dist_id < ZCD_NUM_IDS; dist_id++)
			{
				// The chance of firing drops drastically when out of range
				attack_rate = pitch_attack_rate;
				if(range < dist_zone_center[dist_id])
					attack_rate *= 0.2;

				// Estimate the time spent to do one second of attacking
				actual_attack_time = 1.0;
				potential_attack_time = actual_attack_time / attack_rate;

				// Create accuracy data for this specific zone
				// FIXME: It would be nice to compute better default values
				// for the weapon accuracies too, not just firing rates.
				AccuracyCreate(&acc_default_weap_zone[weapon][dist_id][pitch_id],
							   weapon, shots, direct_hits, splash_hits, splash_damage, actual_attack_time, potential_attack_time);

			}
		}
	}
}
