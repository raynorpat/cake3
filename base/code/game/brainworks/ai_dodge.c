// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_dodge.c
 *
 * Functions that the bot uses to randomly dodge
 *****************************************************************************/

#include "ai_main.h"
#include "ai_vars.h"
#include "ai_dodge.h"

#include "ai_accuracy.h"
#include "ai_move.h"
#include "ai_weapon.h"


// Predict world state this many seconds ahead
#define DODGE_LOOKAHEAD .7

// Number of dodges to consider when seeing incoming missiles
#define NUM_BEST_DIRS 3


/*
================
CompareDodgeInfo

When comparing two dodge rating structures, this function
returns -1 if A is less dangerous than B, 0 if they are
equally dangerous, and 1 if A is more dangerous than B.
When used as a comparator for qsort(), the list will be
sorted from least dangerous to most dangerous.
================
*/
int QDECL CompareDodgeInfo(const void *a, const void *b)
{
	const dodge_info_t *pa = (const dodge_info_t *)a;
	const dodge_info_t *pb = (const dodge_info_t *)b;

	// If one dodge takes more damage than the either, it is more dangerous
	if(pa->damage < pb->damage)
		return -1;
	else if(pa->damage > pb->damage)
		return 1;

	// If one heading is closer to an incoming shot (higher value), it is more dangerous
	if(pa->heading < pb->heading)
		return -1;
	else if(pa->heading > pb->heading)
		return 1;

	// The dodges are equally dangerous (or more likely, not dangerous at all)
	return 0;
}

/*
===========
DodgeVector

Compute a normalized dodge direction from a dodge mask and an axis
===========
*/
void DodgeVector(vec3_t dir, vec3_t * axis, int dodge)
{
	int             axies_used;

	// Start with a zero vector but assume all three axies are used
	VectorSet(dir, 0, 0, 0);
	axies_used = 3;

	// Dodge forward or backward
	if(dodge & MOVE_FORWARD)
		VectorAdd(dir, axis[0], dir);
	else if(dodge & MOVE_BACKWARD)
		VectorSubtract(dir, axis[0], dir);
	else
		axies_used--;

	// Dodge left or right
	if(dodge & MOVE_RIGHT)
		VectorAdd(dir, axis[1], dir);
	else if(dodge & MOVE_LEFT)
		VectorSubtract(dir, axis[1], dir);
	else
		axies_used--;

	// Dodge up or down
	if(dodge & MOVE_UP)
		VectorAdd(dir, axis[2], dir);
	else if(dodge & MOVE_DOWN)
		VectorSubtract(dir, axis[2], dir);
	else
		axies_used--;

	// Normalize the direction vector if more than one axis was used
	if(axies_used > 1)
		VectorNormalize(dir);
}

/*
===============
BotDodgeMissile

Evaluate how dangerous a missile makes a given location
in terms of both splash damage proximity and missile heading.

The missile splash damage value is added to the dodge info's
damage rating.  If this missile is better aimed than previous
missiles (as noted by info->heading), then the new heading's
dot product is saved as well.  Lower dot product headings
(closest to a right angle) are prefered over higher dot products
(closest to colinear with missile path).
===============
*/
void BotDodgeMissile(bot_state_t * bs, dodge_info_t * di, vec3_t dodge_vel, float dodge_time,
					 vec3_t dodge_loc, float missile_time, missile_dodge_t * md)
{
	float           dist, dodge_dist, stand_dist, radius, heading;
	vec3_t          pos, vel;

	// Determine the closest the missile gets while the bot dodges
	//
	// NOTE: Technically this code could use bs->future.origin if a
	// BotMotionFutureUpdate() check had been run, but that could cause
	// an extra prediction every frame, which is a lot of processor
	// time.  It's not clear that the bot's dodging substatially improves
	// from this change either, so the current origin is used instead.
	VectorSubtract(md->pos, bs->now.origin, pos);
	VectorSubtract(md->vel, dodge_vel, vel);
	dodge_dist = trajectory_closest_origin_dist(pos, vel, 0.0, dodge_time);

	// Determine the closest the missile gets after the bot dodges
	//
	// NOTE: The velocity doesn't need translation when the bot is stopped
	VectorSubtract(md->pos, dodge_loc, pos);
	stand_dist = trajectory_closest_origin_dist(pos, md->vel, dodge_time, missile_time);

	// Estimate the damage potential of the missile using the closest distance
	dist = (dodge_dist < stand_dist ? dodge_dist : stand_dist);
	radius = md->bolt->splashRadius;
	if(dist < radius)
		di->damage += md->bolt->splashDamage * (1 - dist / radius);

	// Determine how well this missile is currently aimed at the bot's dodge endpoint;
	// Use absolute value to make right angle dodges the most appealing
	VectorNormalize(pos);
	heading = fabs(DotProduct(md->dir, pos));

	// If this heading is aimed worse (higher absolute dot product), record that fact
	if(di->heading < heading)
		di->heading = heading;
}

/*
==============
BotCreateDodge

Try to create a new dodge information entry to
a dodge info list for the inputted direction.
Returns NULL if it could not be created (perhaps
the dodge would make the bot fall into a pit) and
returns a pointer to the new dodge entry otherwise.
When this function returns a valid pointer, it sets
up the contents of that structure.  The contents
of num_dodges will be incremented as well.

"axis" represents the bot's axies of movement.  If
"moving" is true, the forward axis points towards
the bot's destination.  Otherwise the forward axis
refers to the bot's forward view.

NOTE: The first entry in the dodge list (ie. the first
time this function is called per list) must be for
dodge direction bs->dodge.

FIXME: This "feature" feels inellegant.  Can it
be made more ellegant somehow?
==============
*/
dodge_info_t   *BotCreateDodge(bot_state_t * bs, vec3_t * axis, qboolean moving,
							   dodge_info_t * dodges, int *num_dodges, int dodge)
{
	int             i;
	float           missile_time, dodge_time;
	vec3_t          dodge_loc, dodge_vel;
	vec3_t          missile_loc, missile_dir;
	missile_dodge_t *md;
	dodge_info_t   *di;
	trace_t         trace;

	// Only add the bot's previously selected dodge when stored first
	if(bs->dodge == dodge && *num_dodges)
		return (dodges[0].dodge == dodge ? &dodges[0] : NULL);

	// If a new dodge should be created, it will be stored in this record
	di = &dodges[*num_dodges];
	di->dodge = dodge;

	// Check if it's safe to move in the requested dodge direction
	// NOTE: The forward movement vector is always safe
	DodgeVector(di->dir, axis, dodge);
	if(!(dodge == MOVE_FORWARD && moving) && !BotTestMove(bs, di->dir))
		return NULL;

	// Assume this dodge does not create a dangerous situation
	di->damage = 0.0;
	di->heading = 0.0;

	// Predict the missile for at most this many seconds
	missile_time = DODGE_LOOKAHEAD;

	// Estimate how many seconds the bot could dodge before hitting an
	// obstacle and the location of this obstacle
	//
	// NOTE: dodge_loc is the location at the end of dodging and dodge_vel
	// is the velocity while dodging, so the two values are associated with
	// two different trajectories.
	VectorScale(di->dir, g_speed.value, dodge_vel);
	VectorMA(bs->now.origin, missile_time, dodge_vel, dodge_loc);
	trap_Trace(&trace, bs->now.origin, bs->now.mins, bs->now.maxs, dodge_loc, bs->entitynum, bs->now.clip_mask);
	VectorCopy(trace.endpos, dodge_loc);
	dodge_time = trace.fraction * missile_time;

	// Estimate the danger of this dodge from each nearby missile
	for(i = 0; i < bs->num_missile_dodge; i++)
		BotDodgeMissile(bs, di, dodge_vel, dodge_time, dodge_loc, missile_time, &bs->missile_dodge[i]);

	// A new dodge entry was created
	(*num_dodges)++;
	return di;
}

/*
===========
BotSetDodge
===========
*/
void BotSetDodge(bot_state_t * bs, int dodge)
{
#ifdef DEBUG_AI
	// Announce changes in the dodge state if requested
	if((bs->dodge != dodge) && (bs->debug_flags & BOT_DEBUG_INFO_DODGE))
	{
		BotAI_Print(PRT_MESSAGE, "%s (%.2f): Dodge direction: %s\n", EntityNameFast(bs->ent), bs->command_time, MoveName(dodge));
	}
#endif

	// Remember the current dodge direction
	bs->dodge = dodge;
}

/*
===========
BotUseDodge
===========
*/
qboolean BotUseDodge(bot_state_t * bs, dodge_info_t * di)
{
	vec3_t          dir;

	// Try to walk in that direction
	if(!trap_BotMoveInDirection(bs->ms, di->dir, 400, MOVE_WALK))
		return qfalse;

	// Change the bot's dodge direction
	BotSetDodge(bs, di->dodge);
	return qtrue;
}

/*
================
BotDodgeMovement

Bot randomly dodges but over time moves towards dir
with high probability.
================
*/
void BotDodgeMovement(bot_state_t * bs)
{
	int             i, num_dodges, choice;
	dodge_info_t    dodges[9], *di;
	vec3_t          axis[3];
	qboolean        moving;

	// Check if the bot is already restricted from dodging
	if(!(bs->move_modifiers & MM_DODGE))
		return;

	// Aiming for strafe jumping also interferes with dodging
	if(bs->aim_type == AIM_STRAFEJUMP)
		return;

	// Create the bot's forward/right/up movement axies.
	moving = BotMovementAxies(bs, axis);

	// Set up a dodge record for the bot's previous dodge direction (or non-dodge)
	num_dodges = 0;
	di = BotCreateDodge(bs, axis, moving, dodges, &num_dodges, bs->dodge);

	// Use the old dodge direction if:
	//  - Dodge is safe
	//  - Dodge timeout has not expired
	//  - No new missiles have been detected
	//  - Dodge avoids damage
	//  - Attempt to use dodge succeeded
	if(di && bs->command_time < bs->dodge_timeout && !bs->new_missile && !di->damage && BotUseDodge(bs, di))
		return;

	// Spend all the time dodging if the bot has nowhere to go
	if(!moving)
		bs->dodge_chance = 1.0;

	// Never dodge when using a melee weapon (eg. Gauntlet).
	else if(weapon_stats[bs->ps->weapon].flags & WSF_MELEE)
		bs->dodge_chance = 0.0;

	// Spend no time dodging if there is no enemy nearby (but still dodge incoming missiles)
	else if(!bs->aim_enemy)
		bs->dodge_chance = 0.0;

	// New selected dodge is selected this frame and will expire this many seconds
	bs->dodge_select = bs->command_time;
	bs->dodge_timeout = bs->command_time + interpolate(bot_dodge_min.value, bot_dodge_max.value, random());

	// If the bot does not want to move forward or cannot do so safely,
	// select a new dodge direction.  Otherwise decide at random.
	di = BotCreateDodge(bs, axis, moving, dodges, &num_dodges, MOVE_FORWARD);
	if(!di || di->damage || (random() < bs->dodge_chance))
	{
		// Create descriptions of the remaining dodges (if they are legal)
		//
		// NOTE: MOVE_STILL, the "dodge" of standing in place, is intentially
		// left off of this list.  It's not that standing still is always a
		// bad way of dodging shots, but it's very rarely the right choice.
		// Choosing when to dodge by standing still requires more than just
		// a random number generator.
		BotCreateDodge(bs, axis, moving, dodges, &num_dodges, MOVE_BACKWARD);
		BotCreateDodge(bs, axis, moving, dodges, &num_dodges, MOVE_RIGHT);
		BotCreateDodge(bs, axis, moving, dodges, &num_dodges, MOVE_LEFT);
		BotCreateDodge(bs, axis, moving, dodges, &num_dodges, MOVE_FORWARD | MOVE_RIGHT);
		BotCreateDodge(bs, axis, moving, dodges, &num_dodges, MOVE_FORWARD | MOVE_LEFT);
		BotCreateDodge(bs, axis, moving, dodges, &num_dodges, MOVE_BACKWARD | MOVE_RIGHT);
		BotCreateDodge(bs, axis, moving, dodges, &num_dodges, MOVE_BACKWARD | MOVE_LEFT);

		// In the extremely unlikely event that no potential dodges exist, move forward
		if(!num_dodges)
		{
			BotSetDodge(bs, MOVE_FORWARD);
			return;
		}

		// Sort the potential dodges from least dangerous to most dangerous
		qsort(dodges, num_dodges, sizeof(dodge_info_t), CompareDodgeInfo);

		// Ignore all dodges that don't avoid as much damage as the best dodge
		for(i = 1; i < num_dodges; i++)
		{
			if(dodges[0].damage < dodges[i].damage)
				break;
		}

		// The first unacceptable index equals the number of acceptable dodges
		num_dodges = i;

		// If more than NUM_BEST_DIRS choices exist, only use the NUM_BEST_DIRS best
		// (but keep all dodges that tie with last place)
		if(NUM_BEST_DIRS < num_dodges)
		{
			di = &dodges[NUM_BEST_DIRS - 1];
			for(i = NUM_BEST_DIRS; i < num_dodges; i++)
			{
				if(CompareDodgeInfo(di, &dodges[i]) > 0)
					break;
			}

			// The first unacceptable index equals the number of acceptable dodges
			num_dodges = i;
		}

		// Select dodges at random until a legal dodge is found
		while(num_dodges)
		{
			// Choose a new dodge at random
			choice = rand() % num_dodges;
			di = &dodges[choice];

			// Use this dodge if possible
			if(BotUseDodge(bs, di))
				return;

			// Remove the dodge from the list and try again
			memcpy(di, &dodges[--num_dodges], sizeof(dodge_info_t));
		}
	}

	// By default, keep moving forward
	// NOTE: No movement command is needed because the bot is already moving forward
	BotSetDodge(bs, MOVE_FORWARD);
}
