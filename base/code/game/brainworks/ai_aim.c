// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_aim.c
 *
 * Functions that the bot uses to aim
 *****************************************************************************/

#include "ai_main.h"
#include "ai_vars.h"
#include "ai_aim.h"

#include "ai_attack.h"
#include "ai_aware.h"
#include "ai_command.h"
#include "ai_entity.h"
#include "ai_level.h"
#include "ai_view.h"
#include "ai_weapon.h"


/*
=============
BotSetAimType

Sets the bot's current aiming type.  Returns
true if the bot selected a different target
from last frame and false if the target was the
same.

For the bot to consider two targets the same, the
aim types from last frame and this frame must match.
If an aim entity pointer is specified, that must also
match.  If an aim location was specified (non-NULL)
and no enemy pointer was specified (NULL), the old and
new locations must be very close.
=============
*/
qboolean BotSetAimType(bot_state_t * bs, int type, gentity_t * ent, vec3_t loc)
{
	// If the type didn't change, it's possible no update is needed
	if(bs->aim_type == type)
	{
		// If an entity was specified, only look for changes in entities
		if(ent)
		{
			if(bs->aim_ent == ent)
				return qfalse;
		}

		// Otherwise look for significant changes in location
		else if(loc)
		{
			if(DistanceSquared(bs->aim_loc, loc) < Square(8.0))
				return qfalse;
		}

		// If neither was specified, then certainly nothing has changed
		else
		{
			return qfalse;
		}
	}

	// Save the new type, location, and entity
	bs->aim_type = type;
	bs->aim_ent = ent;
	if(loc)
		VectorCopy(loc, bs->aim_loc);

#ifdef DEBUG_AI
	// Print aim change information if requested
	if(bs->debug_flags & BOT_DEBUG_INFO_AIM)
	{
		char           *target, *subtarget;

		subtarget = NULL;
		switch (type)
		{
			case AIM_ACTIVATOR:
				target = "shot activated button";
				break;
			case AIM_JUMP:
				target = "jump direction";
				break;
			case AIM_ENEMY:
				target = "enemy";
				break;
			case AIM_KAMIKAZE:
				target = "kamikaze body";
				break;
			case AIM_MINE:
				target = "mine";
				break;
			case AIM_MAPOBJECT:
				target = "map object";
				break;
			case AIM_SWIM:
				target = "swim direction";
				break;
			case AIM_FACEENTITY:
				target = "facing entity";
				break;
			case AIM_MOVEMENT:
				target = "movement aim hint";
				break;
			case AIM_AWARE:
				target = "awareness trigger";
				break;
			case AIM_STRAFEJUMP:
				target = "strafe jumping";
				break;
			case AIM_GOAL:
				target = "goal";
				break;
			case AIM_SEARCH:
				target = "searching";
				break;

			default:
				target = "unknown";
				break;
		}

		// Select the appropriate output format and print the message
		if(ent)
			BotAI_Print(PRT_MESSAGE, "%s: Aim target: %s - %s\n", EntityNameFast(bs->ent), target, EntityNameFast(ent));
		else if(loc)
			BotAI_Print(PRT_MESSAGE, "%s: Aim target: %s - (%.f, %.f, %.f)\n",
						EntityNameFast(bs->ent), target, loc[0], loc[1], loc[2]);
		else
			BotAI_Print(PRT_MESSAGE, "%s: Aim target: %s\n", EntityNameFast(bs->ent), target);
	}
#endif

	// Return true because the aim type changed
	return qtrue;
}

/*
===============
BotSetAimAngles

Set the bot's intended aim angles to the look at the input
view angles.  Since the place the bot selects to look at
might not always equal the ideal (input) angles, this function
will change "aim_angles" to match the bot's selected aim
angles.
===============
*/
void BotSetAimAngles(bot_state_t * bs, int aim_type, vec3_t aim_angles)
{
	qboolean        reset;

	// Check if the aiming type changed (requiring an aim state reset)
	reset = BotSetAimType(bs, aim_type, NULL, NULL);

	// Update the intended view interpolation array with the new view angles
	//
	// NOTE: This function changes "aim_angles" to match the selected angles.
	BotViewIdealUpdate(bs, aim_angles, NULL, NULL, (reset ? -1 : 0));
}

/*
========================
BotAimPlayerChangeDetect
========================
*/
int BotAimPlayerChangeDetect(bot_state_t * bs, gclient_t * client)
{
	float           angle_similarity;
	vec3_t          move_dir;

	// Look up the target's current movement direction
	ClientViewDir(client, move_dir);

	// Compare the current direction to the last known direction
	angle_similarity = DotProduct(move_dir, bs->aim_enemy_move_dir);

	// Save the target's current movement direction
	VectorCopy(move_dir, bs->aim_enemy_move_dir);

	// Detect no change if the angles are relatively similar
	if(angle_similarity > cos(DEG2RAD(30.0)))
		return 0x0000;

	// Detect a change on all axies
	return ((1 << YAW) | (1 << PITCH));
}


/*
=====================
BotAimLocChangeDetect

Detects which axies of a bot's angular view state had
notable changes since the past update, given the world
location the bot wants to aim at.  Returns a bitmap
of the changes.  The i'th bit is 1 if the i'th axis
changed and 0 if not.
=====================
*/
int BotAimLocChangeDetect(bot_state_t * bs, vec3_t aim_loc)
{
	int             i;
	float           time_change;
	vec3_t          dir, aim_angles, new_speeds;

	// When looking for view changes, the bot needs to ignore any changes
	// incurred by its own movement (since that is easily predictable).
	// This is done by computing the view angles the bot would have had
	// if it's hadn't moved since the last update.
	//
	// NOTE: The "eye_last_aim" location value will always be initialized
	// since it is first initialized in the case when a reset occurs.
	// (The reset case sets this value but doesn't read it.)
	VectorSubtract(aim_loc, bs->eye_last_aim, dir);
	VectorToAngles(dir, aim_angles);
	aim_angles[PITCH] = AngleNormalize180(aim_angles[PITCH]);
	aim_angles[YAW] = AngleNormalize180(aim_angles[YAW]);

	// Compute the new view speeds ignoring bot movement
	for(i = PITCH; i <= YAW; i++)
	{
		// Determine how much time has changed since the last update
		time_change = bs->command_time - bs->view_ideal_next[i].time;

		// Assume no change in speed for updates in the same frame
		//
		// NOTE: Yes, it's possible a change occurred but actually
		// tracking it is way more effort than it's worth.
		if(time_change <= 0.0)
			new_speeds[i] = bs->view_ideal_speeds_fixed[i];

		// Otherwise compute the speed differentially
		else
			new_speeds[i] = AngleDelta(aim_angles[i], bs->view_ideal_next[i].angle.real) / time_change;
	}

	// Store the last recorded view speeds so they can be used next frame
	VectorCopy(new_speeds, bs->view_ideal_speeds_fixed);

	// If a reset occurred last frame, don't detect any changes because
	// last frame's speeds were probably inaccurate ...
	if(server_time <= bs->view_ideal_reset_time)
		return 0x0000;

	// ... Otherwise check for view changes generated by speed changes
	else
		return ViewSpeedsChanged(bs->view_ideal_speeds_fixed, new_speeds);
}

/*
============
BotSetAimLoc

Just like BotSetAimAngles(), except it accepts an
input location (and optional reference location)
instead of angles.  Similarly updates "aim_loc"
to refer to (an estimate of) the in-game location
the bot actually decided to aim at, since the
selected aim location might not always equal the
ideal location (the inputted "aim_loc").

The aim type, aim entity, and aim location arguments are
used to check if the bot's aim reason changed.  See
BotSetAimType() for more information.  The summary is
that "aim_type" is required, but either or both of "aim_ent"
and "aim_loc" may be NULL.

"ref_loc" is the nearest visible reference location for
the inputted aim location.  If "ref_loc" is NULL, it is
assumed the reference location is the aim location itself.

"aim_speed" is the speed of the aim location, or NULL if it
was not computed (probably stationary).
============
*/
void BotSetAimLoc(bot_state_t * bs, int aim_type, gentity_t * aim_ent, vec3_t aim_loc, vec3_t aim_speed, vec3_t ref_loc)
{
	int             i, changes;
	float           dist, inv_time;
	vec3_t          dir, aim_angles, aim_angle_speeds, ref_angles;
	vec3_t          next_loc, next_angles;

	// If the aiming type generated a reset, note that ...
	// NOTE: A negative change code (0xFFFF) means reset all axies
	if(BotSetAimType(bs, aim_type, aim_ent, aim_loc))
		changes = -1;

	// ... For player targets, only change when their movement changes ...
	else if(aim_ent && aim_ent->client)
		changes = BotAimPlayerChangeDetect(bs, aim_ent->client);

	// ... Otherwise check which view axies detected changes
	else
		changes = BotAimLocChangeDetect(bs, aim_loc);

	// Record the location of the last eye position used when aiming at
	// a location
	VectorCopy(bs->eye_future, bs->eye_last_aim);

	// Translate the aim location to angles
	VectorSubtract(aim_loc, bs->eye_future, dir);
	dist = VectorNormalize(dir);
	VectorToAngles(dir, aim_angles);
	aim_angles[PITCH] = AngleNormalize180(aim_angles[PITCH]);
	aim_angles[YAW] = AngleNormalize180(aim_angles[YAW]);

	// Translate the aim speed data to spherical coordinates if they were provided
	if(aim_speed)
	{
		// Compute cartesian coordinates of the aim location one server frame later
		VectorMA(aim_loc, SERVER_FRAME_DURATION, aim_speed, next_loc);

		// Project those coordinates onto the bot's view sphere
		VectorSubtract(next_loc, bs->eye_future, dir);
		VectorToAngles(dir, next_angles);

		// Compute angular speed from the angular displacement
		for(i = PITCH; i <= ROLL; i++)
			aim_angle_speeds[i] = AngleDelta(next_angles[i], aim_angles[i]) * SERVER_FRAMES_PER_SEC;
	}
	else
	{
		// Assume aim location is stationary
		VectorClear(aim_angle_speeds);
	}

	// Setup reference data if it was provided
	if(ref_loc)
	{
		// Translate the reference location to reference angles
		VectorSubtract(ref_loc, bs->eye_future, dir);
		VectorToAngles(dir, ref_angles);
		ref_angles[PITCH] = AngleNormalize180(ref_angles[PITCH]);
		ref_angles[YAW] = AngleNormalize180(ref_angles[YAW]);
	}
	else
	{
		// Assume the aim target is its own reference
		VectorCopy(aim_angles, ref_angles);
	}

	// Use these angles for the desired aim state
	//
	// NOTE: This function changes "aim_angles" to match the selected angles.
	//
	// FIXME: I believe some of the functions using this function actually
	// uses entity data that hasn't been appropriately predicted ahead to
	// the bot's next command time (eg. BotAimFaceEntity()).  So in theory
	// the aiming values could be a few milliseconds off.  I believe the
	// correct solution is to force prediction for all entities, but this
	// in turn requires a better estimation of what the entity's timestamp
	// will be when the server will execute the bot's next command.  Thankfully
	// the attack aim selection algorithm does this at best it can, but it seems
	// like that functionality should be extended to cover all aiming situations
	// based on a mobile entity's position.  It's not clear where or how to
	// do this, however.
	BotViewIdealUpdate(bs, aim_angles, aim_angle_speeds, ref_angles, changes);

	// Compute the direction vector for the selected aim angles
	AngleVectors(aim_angles, dir, NULL, NULL);

	// Estimate the selected aim location based on the selected angles
	//
	// NOTE: This code assumes the selected location is just as far away as the
	// ideal one.  This is a fair assumption, but still-- just an assumption.
	VectorMA(bs->eye_future, dist, dir, aim_loc);
}

/*
============
BotAimTarget
============
*/
qboolean BotAimTarget(bot_state_t * bs, int aim_type, gentity_t * ent, int weapon)
{
	vec3_t          shot_loc, error;
	float           sighted;
	bot_aware_t    *aware;

	// Don't aim too early if the bot just teleported
	if((bs->teleport_time > 0) && (bs->command_time < bs->react_time + bs->teleport_time))
	{
		return qfalse;
	}

	// Find out when the bot first sighted the target, if that information is known
	aware = BotAwarenessOfEntity(bs, ent);
	sighted = (aware ? aware->sighted : 0.0);

	// Estimate where the target will be so the bot can attack it
	//
	// NOTE: This fills out data in the bs->attack structure.
	if(!BotAttackSelect(bs, ent, weapon, sighted))
		return qfalse;

	// Try to aim there
	VectorCopy(bs->attack.shot_loc, shot_loc);
	BotSetAimLoc(bs, aim_type, bs->attack.ent, shot_loc, bs->attack.motion.velocity, bs->attack.reference);

	// The displacement between the selected (new) and ideal (old) location is the error
	VectorSubtract(shot_loc, bs->attack.shot_loc, error);

	// Translate the intended attack coordinates by selection error
	BotAttackAddError(bs, error);

	// The bot successfully aimed at the target
	return qtrue;
}

/*
===============
BotAimActivator
===============
*/
qboolean BotAimActivator(bot_state_t * bs, bot_path_t * path)
{
	gentity_t      *ent;

	// Only aim if the bot has an obstacle activator subgoal that must be shot
	if(!path->subgoal || !path->shoot)
		return qfalse;

	// Try to aim at the activator target
	return BotAimTarget(bs, AIM_ACTIVATOR, &g_entities[path->subgoal->entitynum], BotActivateWeapon(bs));
}

/*
==========
BotAimJump
==========
*/
qboolean BotAimJump(bot_state_t * bs)
{
	vec3_t          angles;

	// Must plan on doing a movement related jump
	if(!(bs->move_modifiers & MM_JUMP))
		return qfalse;

	// Aiming at the requested jump angles
	//
	// NOTE: Looking in the direction of jumps isn't required, but humans do it
	// for extra safety and precision, so the bots do it as well.
	VectorToAngles(bs->jump_dir, angles);
	BotSetAimAngles(bs, AIM_JUMP, angles);
	return qtrue;
}

/*
===========
BotAimEnemy
===========
*/
qboolean BotAimEnemy(bot_state_t * bs)
{
	// Try aiming at the aim enemy
	if(BotAimTarget(bs, AIM_ENEMY, bs->aim_enemy, bs->weapon))
		return qtrue;

	// Aiming at the goal enemy is another option
	if(BotAimTarget(bs, AIM_ENEMY, bs->goal_enemy, bs->weapon))
		return qtrue;

	// The bot could not aim at any enemies
	return qfalse;
}

#ifdef MISSIONPACK
/*
==============
BotAimKamikaze
==============
*/
qboolean BotAimKamikaze(bot_state_t * bs)
{
	// Attack the kamikaze body if it exists and the bot has a weapon for it
	return BotAimTarget(bs, AIM_KAMIKAZE, bs->kamikaze_body, BotActivateWeapon(bs));
}

/*
==========
BotAimMine
==========
*/
qboolean BotAimMine(bot_state_t * bs, bot_moveresult_t * moveresult)
{
	int             i;
	float           dist, bestdist;
	gentity_t      *mine, *bestmine;

	// If movement was blocked by a mine, spend at most 5 seconds deactivating it
	if(moveresult->flags & MOVERESULT_BLOCKEDBYAVOIDSPOT)
		bs->mine_deactivate_time = bs->command_time + 5.0;

	// Stop looking for mines to deactivate after some time of not seeing any
	if(bs->mine_deactivate_time < bs->command_time)
		return qfalse;

	// Search for the best (closest) mine to deactivate
	bestdist = Square(300.0);
	bestmine = NULL;
	for(i = 0; i < bs->num_proxmines; i++)
	{
		// Use this mine if it's closer than the current closest mine
		dist = DistanceSquared(bs->proxmines[i]->r.currentOrigin, bs->eye_future);
		if(dist < bestdist)
		{
			bestdist = dist;
			bestmine = mine;
		}
	}

	// Attack the mine if it exists and the bot has a weapon for it
	return BotAimTarget(bs, AIM_MINE, bestmine, BotMineDisarmWeapon(bs));
}
#endif

/*
=========
BotAimMap

NOTE: Because of how the activator and obstacle stuff has
been reworked in ai_route.c, it's technically feasible to
analyze the map for activators like the disco ball on
q3tourney6 and determine when shooting it is advantageous.
Granted, it would be an awful lot of work, but it's possible.
And doing so would make it possible to create any map with
a similar trigger, and the bots would use the same logic
to process it.
=========
*/
qboolean BotAimMap(bot_state_t * bs)
{
	int             i;
	gentity_t      *ent, *ball;
	vec3_t          mins = { 700, 204, 672 }, maxs =
	{
	964, 468, 680};
	vec3_t          ball_center = { 304, 352, 920 };
	qboolean        shoot;
	trace_t         trace;

	// Only interesting aim map script is for q3tourney6 disco ball
	if(Q_stricmp(LevelMapTitle(), "q3tourney6"))
		return qfalse;

	// Never shoot the disco ball when the bot is below the crusher
	if((bs->now.origin[0] > mins[0] && bs->now.origin[0] < maxs[0]) &&
	   (bs->now.origin[1] > mins[1] && bs->now.origin[1] < maxs[1]) && (bs->now.origin[2] < mins[2]))
	{
		return qfalse;
	}

	// Extract the entity number for the disco ball
	//
	// FIXME: It sucks that the entity must get extracted this way because this
	// this code doesn't have access to it.  Yet another reason to rewrite the
	// code to read this information from the activators.  Or better yet, just
	// stop using buttons on maps with bots.  I don't believe buttons and activators
	// contribute to the player's gameplay experience.  Certainly not at the cost
	// of bots that cannot handle them well.
	trap_Trace(&trace, bs->eye_future, NULL, NULL, ball_center, bs->entitynum, MASK_SOLID);
	if(DistanceSquared(trace.endpos, ball_center) > Square(48.0))
		return qfalse;
	if(trace.entityNum == ENTITYNUM_WORLD || trace.entityNum == ENTITYNUM_NONE)
		return qfalse;
	// NOTE: This should never occur
	if(trace.fraction >= 1.0)
		return qfalse;
	ball = &g_entities[trace.entityNum];

	// If an enemy is below this bounding box, consinder shooting the button
	shoot = qfalse;
	for(i = 0; i < maxclients && i < MAX_CLIENTS; i++)
	{
		// Only play attention to living players
		ent = &g_entities[i];
		if(!EntityIsAlive(ent))
			continue;

		// Check if this player is in the crush-zone
		if((ent->r.currentOrigin[0] > mins[0] && ent->r.currentOrigin[0] < maxs[0]) &&
		   (ent->r.currentOrigin[1] > mins[1] && ent->r.currentOrigin[1] < maxs[1]) && (ent->r.currentOrigin[2] < mins[2]))
		{
			// Don't shoot if there's a teammate below the crusher
			if(BotSameTeam(bs, ent))
				return qfalse;

			// Be willing to crush the enemy if no teammates would get killed
			shoot = qtrue;
		}
	}

	// Don't shoot if there's no enemy to hit
	if(!shoot)
		return qfalse;

	// Try to aim at the disco ball
	//
	// FIXME: It's possible the bot will select a ranged activation weapon (like the
	// lighting gun) and be unable to actually shoot the target, while the bot
	// actually possesses a weapon that could hit the target for real.  Of course
	// q3tourney6 only has access to three weapons, all with unlimited range.
	return BotAimTarget(bs, AIM_MAPOBJECT, ball, BotActivateWeapon(bs));
}

/*
==========
BotAimSwim
==========
*/
qboolean BotAimSwim(bot_state_t * bs, bot_moveresult_t * moveresult)
{
	// Don't use swim aiming when not swimming
	if(!(moveresult->flags & MOVERESULT_SWIMVIEW))
		return qfalse;

	// Aim in the requested view direction
	BotSetAimAngles(bs, AIM_SWIM, moveresult->ideal_viewangles);
	return qtrue;
}

/*
================
BotAimFaceEntity
================
*/
qboolean BotAimFaceEntity(bot_state_t * bs)
{
	float           time;
	vec3_t          center;

	// Only aim at an entity if the bot requested to do so
	if(!bs->face_entity)
		return qfalse;

	// Aim at the entity's center
	EntityCenter(bs->face_entity, center);
	BotSetAimLoc(bs, AIM_FACEENTITY, bs->face_entity, center, NULL, NULL);
	return qtrue;
}

/*
==================
BotAimMovementView
==================
*/
qboolean BotAimMovementView(bot_state_t * bs, bot_moveresult_t * moveresult)
{
	// Only aim for movement if requested
	if(!(moveresult->flags & MOVERESULT_MOVEMENTVIEWSET))
		return qfalse;

	// Use an appropriate weapon if necessary
	if(moveresult->flags & MOVERESULT_MOVEMENTWEAPON)
		bs->weapon = moveresult->weapon;

	// Aim as requested for movement
	BotSetAimAngles(bs, AIM_MOVEMENT, moveresult->ideal_viewangles);
	return qtrue;
}

/*
===========
BotAimAware
===========
*/
qboolean BotAimAware(bot_state_t * bs)
{
	vec3_t          aim_loc;

	// Only process active awareness triggers
	if(bs->aware_location_time < bs->command_time)
		return qfalse;

	// Aim at awareness trigger location
	//
	// NOTE: This code sends in a copy of the location because the function
	// call might change the location's value
	VectorCopy(bs->aware_location, aim_loc);
	BotSetAimLoc(bs, AIM_AWARE, NULL, aim_loc, NULL, NULL);
	return qtrue;
}

/*
================
BotAimStrafejump
================
*/
qboolean BotAimStrafejump(bot_state_t * bs)
{
	vec3_t          angles;

	// Must not be restricted from strafe jumping
	if(!(bs->move_modifiers & MM_STRAFEJUMP))
		return qfalse;

	// Aiming at the requested strafe jump angles
	//
	// NOTE: Setting the aim angles might modify the input angles, so
	// a copy of the original angles is passed in.
	VectorCopy(bs->strafejump_angles, angles);
	BotSetAimAngles(bs, AIM_STRAFEJUMP, angles);
	return qtrue;
}

/*
==========
BotAimGoal
==========
*/
qboolean BotAimGoal(bot_state_t * bs, bot_goal_t * goal)
{
	vec3_t          target;

	// Only aim at real goals
	if(!goal->areanum)
		return qfalse;

	// Don't aim at goals that are really close-- find somewhere else to look
	if(DistanceSquared(bs->now.origin, goal->origin) < Square(384.0))
		return qfalse;

	// This function finds where you should look to move towards the goal.  It does
	// more than just look at the goal if the goal is in view.
	if(!trap_BotMovementViewTarget(bs->ms, goal, bs->travel_flags, 300, target))
		return qfalse;

	// This target location is usually a little too low
	target[2] += DEFAULT_VIEWHEIGHT;

	// Turn the direction vector into view angles
	BotSetAimLoc(bs, AIM_GOAL, &g_entities[goal->entitynum], target, NULL, NULL);
	return qtrue;
}

#define SEARCH_POINTS	16		// Scan this many points around the bot
#define SEARCH_CHOICES	3		// Only consider this many top choices
#define SEARCH_DIST		Square(384.0)	// Search targets must be at least this far away

/*
===============
BotSelectSearch

Finds a random location to look at in the bot's general area.
The caller may or may not use it as the official search target.
This function returns the distance squared to the worst search
target it considered using, or -1 if no targets were found.
===============
*/
float BotSelectSearch(bot_state_t * bs, vec3_t target)
{
	int             i, j, num_targets, choice;
	float           step, angle, point_dist;
	vec3_t          point, floor;
	entry_float_vec3_t targets[SEARCH_POINTS];
	trace_t         trace;

	// Select some locations starting at a random angle offset
	step = 2 * M_PI / SEARCH_POINTS;
	angle = 0;
	num_targets = 0;
	for(i = 0; i < SEARCH_POINTS; i++)
	{
		// Start at the bot view origin
		VectorCopy(bs->eye_future, point);

		// Try the next angle location
		angle += step;
		point[0] += 1024 * cos(angle);
		point[1] += 1024 * sin(angle);

		// Determine where this view direction contacts a wall
		trap_Trace(&trace, bs->eye_future, NULL, NULL, point, bs->entitynum, MASK_SOLID);

		// Ignore scan locations which are too close to the bot
		if(DistanceSquared(bs->eye_future, trace.endpos) < SEARCH_DIST)
			continue;

		// Select a target location slightly in front of the trace endpoint
		VectorSet(point,
				  trace.endpos[0] * .95 + bs->eye_future[0] * .05,
				  trace.endpos[1] * .95 + bs->eye_future[1] * .05, trace.endpos[2] * .95 + bs->eye_future[2] * .05);

		// Check if safe ground exists below this target location
		VectorSet(floor, point[0], point[1], point[2] - 1024);
		trap_Trace(&trace, point, NULL, NULL, floor, bs->entitynum, MASK_SOLID);
		if(trace.fraction >= 1.0)
			continue;

		// Ignore locations above dangerous areas
		trace.endpos[2] += 1.0;
		if(trap_PointContents(trace.endpos, bs->entitynum) & (CONTENTS_NODROP | CONTENTS_LAVA | CONTENTS_SLIME))
		{
			continue;
		}

		// Consider using this location, at about eyeline above the floor
		VectorSet(floor, trace.endpos[0], trace.endpos[1], trace.endpos[2] + 48);

		// Use the floor point if it can be seen-- otherwise use the original contact point
		trap_Trace(&trace, bs->eye_future, NULL, NULL, floor, bs->entitynum, MASK_SOLID);
		if(trace.fraction >= 1.0)
			VectorCopy(floor, targets[num_targets].value);
		else
			VectorCopy(point, targets[num_targets].value);

		// Also compute the distance to the target and increment the number of targets found
		targets[num_targets].key = DistanceSquared(targets[num_targets].value, bs->eye_future);
		num_targets++;
	}

	// Make sure at least one valid location was found
	if(!num_targets)
		return -1;

	// Sort the list by decreasing target distance (furthest vectors first in list)
	qsort(targets, num_targets, sizeof(entry_float_vec3_t), CompareEntryFloatReverse);

	// Select one of the best targets at random
	if(num_targets > SEARCH_CHOICES)
		num_targets = SEARCH_CHOICES;
	choice = rand() % num_targets;
	VectorCopy(targets[choice].value, target);

	// Return the distance to the worst target option (so the caller can check if
	// its old choice is significantly worse than these new choices).
	return targets[num_targets - 1].key;
}

#define MAX_SEARCH_TIME 1.5
#define MIN_SEARCH_TIME 1.0

/*
============
BotAimSearch
============
*/
qboolean BotAimSearch(bot_state_t * bs)
{
	float           dist;
	vec3_t          target;
	qboolean        new_target;
	trace_t         trace;

	// Consider a new search location if one can be found
	//
	// NOTE: If no targets can be found, it's unlikely the last aim target
	// is much better, so the bot should look for other places to aim.
	dist = BotSelectSearch(bs, target);
	if(dist < 0)
		return qfalse;

	// Always use this target when the old target times out
	if(bs->search_timeout <= bs->command_time)
	{
		new_target = qtrue;
	}

	// Also use the new target if the old target is too far away
	else
	{
		trap_Trace(&trace, bs->eye_future, NULL, NULL, bs->search_target, bs->entitynum, MASK_SOLID);
		new_target = (DistanceSquared(bs->eye_future, trace.endpos) < dist);
	}

	// Update the search target and timeout when changing targets
	if(new_target)
	{
		VectorCopy(target, bs->search_target);
		bs->search_timeout = bs->command_time + MIN_SEARCH_TIME + (MAX_SEARCH_TIME - MIN_SEARCH_TIME) * random();
	}

	// Look at the selected scan target
	//
	// NOTE: This code sends in a copy of the location because the function
	// call might change the location's value
	VectorCopy(bs->search_target, target);
	BotSetAimLoc(bs, AIM_SEARCH, NULL, target, NULL, NULL);
	return qtrue;
}

/*
============
BotAimRepeat
============
*/
qboolean BotAimRepeat(bot_state_t * bs)
{
	// Continue aiming where the bot wanted to aim last turn
	return qtrue;
}


/*
============
BotAimSelect
============
*/
void BotAimSelect(bot_state_t * bs, bot_moveresult_t * moveresult)
{
	int             i;

	// Assume perfect aim skill and accuracy
	bs->aim_skill = 1.0;
	bs->aim_accuracy = 1.0;

	// Assume the bot will not aim at an attack target
	bs->attack.ent = NULL;

	// By default, preselect a weapon to attack the bot's aim target
	//
	// NOTE: Other aim modes (like BotAimMovementView) may override
	// this weapon choice
	bs->weapon = BotTargetWeapon(bs);


	// The bot must know where it will be next server frame for aim modes
	// that aim at a location, or there will be parallax view problems.
	//
	// NOTE: The actual aim position will depend on how the bot moves,
	// which in turn depends on the bot's view angles for this frame, so
	// this value is at best an estimate.  It is, however, a very good
	// estimate.
	BotMotionFutureUpdate(bs);

	// Try aiming for these reasons, in priority order
	if(!BotAimActivator(bs, &bs->item_path) &&
	   !BotAimActivator(bs, &bs->main_path) && !BotAimJump(bs) && !BotAimMap(bs) && !BotAimEnemy(bs) &&
#ifdef MISSIONPACK
	   !BotAimKamikaze(bs) && !BotAimMine(bs, moveresult) &&
#endif
	   !BotAimSwim(bs, moveresult) &&
	   !BotAimFaceEntity(bs) &&
	   !BotAimMovementView(bs, moveresult) &&
	   !BotAimAware(bs) && !BotAimStrafejump(bs) && !BotAimGoal(bs, &bs->goal) && !BotAimSearch(bs))
	{
		// If all else fails, continue aiming in the same place as last turn
		BotAimRepeat(bs);
	}

	// Use the prefered weapon for this aim style
	BotCommandWeapon(bs, bs->weapon);
}
