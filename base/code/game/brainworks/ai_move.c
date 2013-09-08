// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_move.c
 *
 * Functions that the bot uses to move
 *****************************************************************************/

#include "ai_main.h"
#include "ai_vars.h"
#include "ai_move.h"

#include "ai_accuracy.h"
#include "ai_client.h"
#include "ai_entity.h"
#include "ai_goal.h"
#include "ai_level.h"
#include "ai_motion.h"
#include "ai_path.h"
#include "ai_scan.h"
#include "ai_self.h"
#include "ai_view.h"
#include "ai_visible.h"
#include "ai_weapon.h"

// This file provides access to some of the acceleration constants
// used in player movement.
#include "../bg_local.h"


// How far to predict ahead for local movement instructions
#define LOCAL_NAVIGATION_AREAS 32	// 32 areas
#define LOCAL_NAVIGATION_TIME 200	// 2.00 seconds


/*
========
MoveName
========
*/
char           *MoveName(int move)
{
	// Check for common move names
	// NOTE: This does not include up and down move directions
	switch (move)
	{
		case MOVE_STILL:
			return "still";

		case MOVE_FORWARD:
			return "forward";
		case MOVE_BACKWARD:
			return "backward";
		case MOVE_RIGHT:
			return "right";
		case MOVE_LEFT:
			return "left";

		case MOVE_FORWARD | MOVE_RIGHT:
			return "forward-right";
		case MOVE_FORWARD | MOVE_LEFT:
			return "forward-left";
		case MOVE_BACKWARD | MOVE_RIGHT:
			return "backward-right";
		case MOVE_BACKWARD | MOVE_LEFT:
			return "backward-left";
	}

	// Probably a misformed move bitmap
	return "unknown";
}

/*
============
BotMoveSetup

Setup basic information needed
to select bot movement.
============
*/
void BotMoveSetup(bot_state_t * bs)
{
	// Basic travel flags
	bs->travel_flags = TFL_DEFAULT;
	if(bot_grapple.integer)
		bs->travel_flags |= TFL_GRAPPLEHOOK;
	if(EntityInLavaOrSlime(bs->ent))
		bs->travel_flags |= TFL_LAVA | TFL_SLIME;

	// Let bots rocket jump if they want to
	if(bs->aim_enemy && BotShouldRocketJump(bs))
		bs->travel_flags |= TFL_ROCKETJUMP;

	// Some maps have special movement setup information
	BotMapScripts(bs);
}

/*
================
BotMovementAxies

Extract the bot's forward/right/up movement axies.
Returns true if the bot is actually moving.
================
*/
qboolean BotMovementAxies(bot_state_t * bs, vec3_t * axis)
{
	vec3_t          angles;
	bot_input_t     bi;

	// Extract bot's forward movement destination, if any
	//
	// NOTE: This function must directly retrieve the movement input
	// instead of getting it from the variables setup by BotMoveProcess()
	// because this function is used by movement modifiers (like dodging)
	// to change movement before the data is officially processed.
	trap_EA_GetInput(bs->client, 0.0, &bi);

	// If there is no destination, use the bot's view heading as "forward"
	if(VectorCompare(bi.dir, vec3_origin))
	{
		ViewAnglesReal(bs->view_now, angles);
		AngleVectors(angles, axis[0], axis[1], axis[2]);
		return qfalse;
	}

	// Use the movement direction as "forward"
	VectorCopy(bi.dir, axis[0]);

	// Create a "right" vector that always has a zero Z component
	if(axis[0][0] == 0 && axis[0][1] == 0)
	{
		VectorSet(axis[1], 0, 1, 0);
	}
	else
	{
		VectorSet(axis[1], -axis[0][1], axis[0][0], 0);
		VectorNormalize(axis[1]);
	}

	// "Up" vector must be perpendicular to "forward" and "right" vectors
	CrossProduct(axis[0], axis[1], axis[2]);

	// Return true because the bot has a target direction
	return qtrue;
}

/*
=================
BotMoveInitialize

Initialize the bot's movement state in
preparation for trap_BotMoveToGoal().
=================
*/
void BotMoveInitialize(bot_state_t * bs)
{
	bot_initmove_t  initmove;

	// Set up the movement intialization structure
	memset(&initmove, 0, sizeof(bot_initmove_t));
	VectorCopy(bs->now.origin, initmove.origin);
	VectorCopy(bs->now.velocity, initmove.velocity);
	ViewAnglesReal(bs->view_now, initmove.viewangles);
	VectorClear(initmove.viewoffset);
	initmove.viewoffset[2] += bs->ps->viewheight;
	initmove.entitynum = bs->entitynum;
	initmove.client = bs->client;

	// Compute how much time has elapsed since the bot's last movement initialization
	if(bs->last_move_time > 0.0)
		initmove.thinktime = bs->command_time - bs->last_move_time;
	else
		initmove.thinktime = 0.0;
	bs->last_move_time = bs->command_time;

	// Check if the bot is standing on ground
	if(EntityOnGroundNow(bs->ent))
		initmove.or_moveflags |= MFL_ONGROUND;

	// Check if the bot is being teleported
	if((bs->ps->pm_flags & PMF_TIME_KNOCKBACK) && (bs->ps->pm_time > 0))
		initmove.or_moveflags |= MFL_TELEPORTED;

	// Check if the bot is jumping in water
	if((bs->ps->pm_flags & PMF_TIME_WATERJUMP) && (bs->ps->pm_time > 0))
		initmove.or_moveflags |= MFL_WATERJUMP;

	// Select the crouch or standing bounding box as appropriate
	if(EntityCrouchingNow(bs->ent))
		initmove.presencetype = PRESENCE_CROUCH;
	else
		initmove.presencetype = PRESENCE_NORMAL;

	// Initialize the movement state
	trap_BotInitMoveState(bs->ms, &initmove);
}

/*
===========
BotTestMove

Test if the bot can safely move in the specified
direction without falling off a ledge

NOTE: This function is crude because the internal
engine doesn't support this functionality like it
should.  As such, this function has glaring flaws.
For example, bots might not want to move up or
down stairs, thinking they will fall down ledges
or hit walls.

FIXME: Add this functionality to the internal
engine, so the bot can test if it's "safe" to
move in a direction, where "safe" means the bot
won't fall off any ledges it can't walk back from
by moving in the opposite direction.
===========
*/
qboolean BotTestMove(bot_state_t * bs, vec3_t dir)
{
	vec3_t          end, ground;
	trace_t         trace;

	// Stationary movement always succeeds
	if(VectorCompare(dir, vec3_origin))
		return qtrue;

	// Determine where the move would end up
	VectorMA(bs->now.origin, 96.0, dir, end);

	// Do not move there if the move hits a wall
	trap_Trace(&trace, bs->now.origin, bs->now.mins, bs->now.maxs, end, bs->entitynum, bs->now.clip_mask);
	if(trace.fraction < 1.0)
		return qfalse;

	// Do not move off ledges, into pits, and so on
	VectorSet(ground, end[0], end[1], end[2] - 64.0);
	trap_Trace(&trace, end, bs->now.mins, bs->now.maxs, ground, bs->entitynum, bs->now.clip_mask);
	if(trace.fraction >= 1.0)
		return qfalse;

	// The move appears acceptable
	return qtrue;
}

/*
================
BotMoveDirection
================
*/
qboolean BotMoveDirection(bot_state_t * bs, bot_moveresult_t * moveresult, vec3_t dir, int speed, int movetype)
{
	qboolean        avoidspot;

	// Attempt to move in that direction
	if(!trap_BotMoveInDirection(bs->ms, dir, speed, movetype))
		return qfalse;

	// Manually setup the moveresult when movement was successful
	avoidspot = (moveresult->flags & MOVERESULT_BLOCKEDBYAVOIDSPOT);
	memset(moveresult, 0, sizeof(bot_moveresult_t));
	moveresult->failure = qfalse;
	VectorCopy(dir, moveresult->movedir);
	if(avoidspot)
		moveresult->flags = MOVERESULT_BLOCKEDBYAVOIDSPOT;

	return qtrue;
}

/*
===================
BotCheckMoveFailure
===================
*/
#define AVOID_RIGHT		0x01	// Avoid obstacles by stepping to the right
void BotCheckMoveFailure(bot_state_t * bs, bot_moveresult_t * moveresult)
{
	vec3_t          forward, sideways, end, angles, up = { 0, 0, 1 };
	vec3_t          mins = { -16, -16, -24 };	// Crouch bounding box mins
	vec3_t          maxs = { 16, 16, 16 + 1 };	// Crouch bounding box maxs
	trace_t         trace;

	// Only unblock if necessary
	if(!moveresult->failure || !moveresult->blocked)
		return;

	// Move at random if the bot is stuck in a solid area
	if(moveresult->type == RESULTTYPE_INSOLIDAREA)
	{
		float           angle;

		angle = random() * 2 * M_PI;
		VectorSet(forward, sin(angle), cos(angle), 0);

		BotMoveDirection(bs, moveresult, forward, 400, MOVE_WALK);
		return;
	}

	// Request path reprediction next frame for all paths
	BotPathReset(&bs->main_path);
	BotPathReset(&bs->item_path);

	// Compute the horizontal projection of the movement vector if possible
	VectorSet(forward, moveresult->movedir[0], moveresult->movedir[1], 0);
	if(VectorNormalize(forward) < 0.1)
	{
		VectorSet(angles, 0, 360 * random(), 0);
		AngleVectors(angles, forward, NULL, NULL);
	}

	// Check if the bot could crouch through a tunnel
	VectorMA(bs->now.origin, 32, forward, end);
	VectorSet(mins, -16, -16, -24);
	VectorSet(maxs, 16, 16, 16 + 1);
	trap_Trace(&trace, bs->now.origin, mins, maxs, end, bs->entitynum, bs->now.clip_mask);
	if(trace.fraction >= 1.0)
	{
		// Also check that the player's normal bounding box would be blocked
		maxs[2] = 32 + 1;
		trap_Trace(&trace, bs->now.origin, mins, maxs, end, bs->entitynum, bs->now.clip_mask);

		// If the bot needed to crouch forward and it could do so the problem was addressed
		if((trace.fraction < 1.0) && BotMoveDirection(bs, moveresult, forward, 400, MOVE_CROUCH))
		{
			return;
		}
	}

	// Determine which direction to try sidestepping first
	CrossProduct(forward, up, sideways);
	if(bs->avoid_method & AVOID_RIGHT)
		VectorNegate(sideways, sideways);

	// Try to sidestep in that direction
	if(BotMoveDirection(bs, moveresult, sideways, 400, MOVE_WALK))
		return;

	// Next time the bot is blocked, try stepping in the other direction first
	bs->avoid_method ^= AVOID_RIGHT;

	// Try sidestepping in the other direction
	VectorNegate(sideways, sideways);
	BotMoveDirection(bs, moveresult, sideways, 400, MOVE_WALK);
}

/*
===============
BotMoveTeammate

Tries to move towards (or away from) a player that is
guaranteed to be an teammate.  Returns true if the bot
has handled movement towards the player and false if
the bot should use standard approach movement code.
===============
*/
qboolean BotMoveTeammate(bot_state_t * bs, gentity_t * ent)
{
	// Stop (true) if the bot is inside formation distance
	return (DistanceSquared(bs->now.origin, ent->r.currentOrigin) <= Square(bs->formation_dist));
}

/*
=================
BotEnemyCanEscape

Test if an enemy can easily escape from the
bot's line of sight in the given direction.
=================
*/
qboolean BotEnemyCanEscape(bot_state_t * bs, gentity_t * ent, vec3_t dir)
{
	vec3_t          end;
	trace_t         trace;

	// Determine the endpoint of the escape direction
	VectorMA(ent->r.currentOrigin, 256.0, dir, end);
	trap_Trace(&trace, ent->r.currentOrigin, NULL, NULL, end, ent->s.number, MASK_SOLID);
	VectorCopy(trace.endpos, end);

	// The enemy can escape in this direction if the bot can't see the end point
	trap_Trace(&trace, bs->eye_now, NULL, NULL, end, bs->entitynum, MASK_SOLID);
	return (trace.fraction < 1.0);
}

/*
===================
BotEnemyDamageRatio

This function computes how quickly an enemy in the specified
combat zone can damage the bot and vice versa and returns
the ratio between these values.  A ratio above 1.0 means the
enemy deals damage faster than bot.  Presumably the bot will
try to minimize this ratio.  This function returns -1 if the
bot's damage rate in the zone is zero (ie. when the ratio would
divide by zero).  This could happen when the bot's only available
weapon is out of range for the current zone, for example.

The enemy_weapons argument is a bitmask list of which weapon(s)
the enemy is allowed to use in these zones, analogous to
bs->weapons_available.  The enemy is allowed to use weapon 'i'
only when (enemy_weapons | (1 << i)) is true.

The splash arguments are true when the bot and enemy can receive
splash damage.
===================
*/
float BotEnemyDamageRatio(bot_state_t * bs, qboolean bot_splash, combat_zone_t * enemy_zone,
						  unsigned int enemy_weapons, qboolean enemy_splash)
{
	float           bot_rate, enemy_rate;
	combat_zone_t   bot_zone;

	// Estimate the bot's ability to damage to the enemy
	bot_rate = BotDamageRate(bs, bs->weapons_available, enemy_zone, enemy_splash);
	if(bot_rate <= 0.0)
		return -1;

	// Generate the inverse combat zone, representing the bot's location relative
	// to the enemy
	//
	// NOTE: The bot assumes the enemy is as good with the weapon as the bot is.
	// This is because A) even good players make this mistake and B) having each bot
	// track each enemy's weapon statistics would take a phenomenal amount of data.
	CombatZoneInvert(enemy_zone, &bot_zone);

	// Estimate the enemy's ability to damage the bot
	enemy_rate = BotDamageRate(bs, enemy_weapons, &bot_zone, bot_splash);
	if(enemy_rate <= 0.0)
		return 0.0;

	// Return the ratio of the damage rates-- lower is better for the bot
	return enemy_rate / bot_rate;
}

/*
===============
BotMoveEnemyDir

Returns the ideal direction the bot should
move to get into combat with the enemy--
MOVE_FORWARD, MOVE_BACKWARD, or MOVE_STILL.
The backward direction vector will be computed
by this function as well if MOVE_BACKWARD is
returned.  (No vector for forward is needed
because moving forward is handled by the standard
goal movement code).
===============
*/
int BotMoveEnemyDir(bot_state_t * bs, gentity_t * ent, vec3_t backward)
{
	int             i;
	unsigned int    enemy_weapons;
	qboolean        bot_splash, enemy_splash;
	float           range, dist, pitch, ratio;
	float           best_dist, best_ratio;
	vec3_t          dir, angles, forward, right, left, forward_right, forward_left;
	vec3_t          up = { 0, 0, 1 };
	combat_zone_t   zone;
	weapon_stats_t *ws;

	// Compute the direction vector towards the enemy and cache zone definition data
	VectorSubtract(ent->r.currentOrigin, bs->now.origin, dir);
	dist = VectorNormalize(dir);
	VectorToAngles(dir, angles);
	pitch = AngleNormalize180(angles[PITCH]);

	// If the bot's selected weapon isn't sufficiently in range, charge forward
	ws = &weapon_stats[bs->weapon];
	if((ws->range) && (dist < ws->range * .8))
		return MOVE_FORWARD;

	// Always charge forward for melee weapons as well
	if(ws->flags & WSF_MELEE)
		return MOVE_FORWARD;

	// Don't get too close to the enemy
	if(dist < 184.0)
		return MOVE_BACKWARD;

	// Compute a sideways direction vector which is embedded in the X-Y plane (horizontal)
	//
	// NOTE: If no such vector exists, the player is either directly above or below the bot.
	// In either case, assume the enemy can easily escape, so move towards them
	CrossProduct(up, dir, right);
	if(VectorCompare(right, vec3_origin))
		return MOVE_FORWARD;
	VectorNormalize(right);

	// Compute forward and backward vectors embedded in the X-Y plane
	//
	// NOTE: This is also the projection of "dir" onto the X-Y plane, but the
	// cross product computation is faster than dividing by a square root.
	CrossProduct(right, up, forward);
	VectorNegate(forward, backward);

	// If the weapon has a blast radius, stay outside of that radius
	if(dist * 0.8 < ws->radius)
		return MOVE_BACKWARD;

	// Compute potential escape directions
	//
	// NOTE: Because forward is the direction from the bot to enemy, when the enemy
	// moves in that direction, they move further away from the bot.
	VectorNegate(right, left);
	VectorAdd(forward, right, forward_right);
	VectorAdd(forward, left, forward_left);
	VectorScale(forward_right, M_SQRT1_2, forward_right);
	VectorScale(forward_left, M_SQRT1_2, forward_left);

	// If any of these directions provide escape for the enemy, move towards the enemy
	if(BotEnemyCanEscape(bs, ent, right) ||
	   BotEnemyCanEscape(bs, ent, left) ||
	   BotEnemyCanEscape(bs, ent, forward_right) ||
	   BotEnemyCanEscape(bs, ent, forward_left) || BotEnemyCanEscape(bs, ent, forward))
	{
		return MOVE_FORWARD;
	}

	// Assume the enemy never switches weapons (or conversely, create a
	// situation where the enemy must waste time switching weapons)
	enemy_weapons = (1 << ent->client->ps.weapon);

	// Check if the bot and the enemy can receive splash damage
	bot_splash = !bs->ps->powerups[PW_BATTLESUIT];
	enemy_splash = !ent->client->ps.powerups[PW_BATTLESUIT];

	// By default, assume the best combat zone is the current zone;
	// use the cached aim enemy zone if possible
	if(bs->aim_enemy == ent)
	{
		best_ratio = BotEnemyDamageRatio(bs, bot_splash, &bs->aim_zone, enemy_weapons, enemy_splash);
		best_dist = bs->aim_zone.dist;
	}
	else
	{
		CombatZoneCreate(&zone, dist, pitch);
		best_ratio = BotEnemyDamageRatio(bs, bot_splash, &zone, enemy_weapons, enemy_splash);
		best_dist = dist;
	}

	// Test the damage ratio at different distances
	for(i = 0; i < ZCD_NUM_IDS; i++)
	{
		// Estimate how effective this zone is for combat with the enemy
		//
		// NOTE: The height is scaled proportionate to the distance change.
		CombatZoneCreate(&zone, dist_zone_center[i], pitch);
		ratio = BotEnemyDamageRatio(bs, bot_splash, &zone, enemy_weapons, enemy_splash);

		// Ignore zones from which the bot cannot damage the enemy
		if(ratio < 0)
			continue;

		// Use this zone as the ideal distance if the ratio improved.  In
		// other words, find a zone where the enemy's weapon is "below average"
		// according to the bot's understanding of the weapon's effectiveness.
		if(ratio < best_ratio || best_ratio < 0)
		{
			best_ratio = ratio;
			best_dist = dist_zone_center[i];
		}
	}

	// Stand still if in or near the ideal distance
	if(fabs(best_dist - dist) < 32.0)
		return MOVE_STILL;

	// Move closer to the enemy if the ideal fighting distance is closer
	if(dist < best_dist)
		return MOVE_FORWARD;

	// Otherwise try to back up
	return MOVE_BACKWARD;
}

/*
============
BotMoveEnemy

Tries to move towards (or away from) a player that is
guaranteed to be an enemy.  Returns true if the bot
has handled movement towards the player and false if
the bot should use standard approach movement code.
============
*/
qboolean BotMoveEnemy(bot_state_t * bs, gentity_t * ent, bot_moveresult_t * moveresult)
{
	int             dir;
	vec3_t          backward;

	// Determine the ideal direction to move to fight with this enemy
	dir = BotMoveEnemyDir(bs, ent, backward);

	// Make sure moving backward is actually possible
	if(dir == MOVE_BACKWARD)
	{
		// Move backward if able
		if(BotTestMove(bs, backward))
		{
			BotMoveDirection(bs, moveresult, backward, 400, MOVE_WALK);
			bs->dodge_chance = bot_dodge_rate.value;
			return qtrue;
		}

		// Stand still instead
		dir = MOVE_STILL;
		bs->dodge_chance = 1.0;
	}

	// To stand still, no more processing is needed (so true is returned);
	// To move forward, let the normal movement code move the bot (so false is returned)
	return (dir == MOVE_STILL);
}

/*
=============
BotMovePlayer

Returns true if the bot has handled movement towards
the player in the goal (if any) and false if the bot
should use standard goal approach movement code.
=============
*/
qboolean BotMovePlayer(bot_state_t * bs, bot_moveresult_t * moveresult)
{
	gentity_t      *ent;

	// Only do player movement for clients
	ent = GoalPlayer(&bs->goal);
	if(!ent)
		return qfalse;

	// If the player isn't in a good line of sight, do normal goal movement
	if(!BotEntityVisibleFast(bs, ent))
		return qfalse;

	// How to move towards an enemy (probably bs->goal_enemy) is a complicated decision
	if(BotEnemyTeam(bs, ent))
		return BotMoveEnemy(bs, ent, moveresult);

	// Maintain formation distance when moving towards teammates
	else if(BotSameTeam(bs, ent))
		return BotMoveTeammate(bs, ent);

#ifdef DEBUG_AI
	// This can only happen if a bot accidently chooses a spectator as a goal
	BotAI_Print(PRT_WARNING, "Bot %s (client %i) selected spectator %s (client %i) as a goal.\n",
				EntityNameFast(bs->ent), bs->client, EntityNameFast(ent), bs->goal.entitynum);
#endif

	// The bot should stay put
	return qtrue;
}

/*
=============
BotMoveSelect

Compute the movement direction that makes the
bot move towards its selected goal.
=============
*/
void BotMoveSelect(bot_state_t * bs, bot_moveresult_t * moveresult)
{
	// Clear the movement result object
	memset(moveresult, 0, sizeof(bot_moveresult_t));

	// Reset the reachability avoidances if the bot's movement destination
	// changed or if the bot has no movement destination
	if((bs->move_area != bs->goal.areanum) || (!bs->goal.areanum))
	{
		trap_BotResetAvoidReach(bs->ms);
		bs->move_area = bs->goal.areanum;
	}

	// Initialize the engine's notion of the bot's motion
	BotMoveInitialize(bs);
	memset(moveresult, 0, sizeof(bot_moveresult_t));

#ifdef DEBUG_AI
	// Don't go anywhere if the bot is supposed to stop moving
	if(bs->debug_flags & BOT_DEBUG_MAKE_MOVE_STOP)
		return;
#endif

	// If the bot has no real goal, no movement is necessary
	//
	// NOTE: The bot can still dodge
	if(!bs->goal.areanum)
	{
		bs->dodge_chance = 1.0;
		return;
	}

	// When the goal is a player, don't just move as close as possible to them
	if(BotMovePlayer(bs, moveresult))
		return;

	// Spend some of the time dodging and the rest of the time moving to the goal
	bs->dodge_chance = bot_dodge_rate.value;

	// Move as close as possible to the goal
	//
	// NOTE: This system call takes between 15us and 50us, so it should be called
	// as little as possible
	trap_BotMoveToGoal(moveresult, bs->ms, &bs->goal, bs->travel_flags);

	// Modify the movement direction if the initial result failed to compute
	BotCheckMoveFailure(bs, moveresult);
}

/*
==============
BotRouteSwimUp
==============
*/
int BotRouteSwimUp(bot_state_t * bs)
{
	gentity_t      *ent;

	// Always swim up for air if the bot isn't going anywhere
	if(!bs->goal.areanum)
		return MM_SWIMUP;

	// Don't swim up when moving towards a stationary (non-player) goal
	ent = GoalPlayer(&bs->goal);
	if(!ent)
		return 0;

	// Only muck with the swim directions when fighting enemies
	if(!BotEnemyTeam(bs, ent))
		return 0;

	// Head directly towards enemies that will probably run from the bot
	if(BotChaseEnemy(bs, ent))
		return 0;

	// Head directly for enemies not in line-of-sight
	if(!BotEntityVisibleFast(bs, ent))
		return 0;

	// Keep swimming for air while fighting this enemy
	return MM_SWIMUP;
}

/*
============
BotRouteJump

Patch bugs in the movement engine for jumping

NOTE: This function may invalidate the contents of *route.

NOTE: There is another bug with TFL_WATERJUMP that cannot be
fixed here.  You can see it on q3dm12 when the bot tries to
swim out of the water to the BFG room.  The movement engine
overestimates the maximum height allowed for TFL_WATERJUMP
and the bot gets stuck because of it.
============
*/
int BotRouteJump(bot_state_t * bs, aas_predictroute_t * route)
{
	int             start_area;
	vec3_t          ledge_start, floor_start, start_dir, end_dir;

	// Never modify anything when already in the air (either during the jump
	// or before it)
	if(!EntityOnGroundNow(bs->ent))
		return 0;

	// When jumping, move directly towards the jump landing point
	if(route->endtravelflags & TFL_JUMP)
	{
		// Plan another route to determine the landing point from the route's
		// old stop point (where the jump started).  This route stops when the
		// bot stops jumping.
		start_area = route->endarea;
		VectorCopy(route->endpos, bs->jump_start);
		memset(route, 0, sizeof(aas_predictroute_t));
		trap_AAS_PredictRoute(route,
							  start_area, bs->jump_start,
							  bs->goal.areanum,
							  bs->travel_flags,
							  LOCAL_NAVIGATION_AREAS, LOCAL_NAVIGATION_TIME, RSE_USETRAVELTYPE, 0, ~TFL_JUMP, 0);

		// Force movement towards this endpoint if the endpoint represents
		// the jump landing point and isn't too close to the bot
		VectorSubtract(route->endpos, bs->now.origin, bs->jump_dir);
		bs->jump_dir[2] = 0;
		if((route->stopevent & RSE_USETRAVELTYPE) && (VectorNormalize(bs->jump_dir) > 24.0))
		{
			// Assume the edge is perpendicular to the vector between the start and end points
			VectorSubtract(route->endpos, bs->jump_start, bs->jump_edge);
			bs->jump_edge[2] = 0;
			VectorNormalize(bs->jump_edge);

			// Also reset the backup flag if the bot didn't navigation jump last frame
			if(!(bs->move_modifiers & MM_JUMP))
				bs->jump_backup = qfalse;

			// Use navigation jumping
			return MM_JUMP;
		}
	}

	// When walking off ledges, walking may be necessary to hit the bottom ledge
	else if(route->endtravelflags & TFL_WALKOFFLEDGE)
	{
		// Plan another route to determine the route's intended destination.
		// This route allows any kind of movement style.
		//
		// NOTE: For some reason the engine stops prediction as soon as the ledge
		// jump ends, no matter what the travel style is, so two predictions are needed
		start_area = route->endarea;
		VectorCopy(route->endpos, ledge_start);
		memset(route, 0, sizeof(aas_predictroute_t));
		trap_AAS_PredictRoute(route,
							  start_area, ledge_start,
							  bs->goal.areanum,
							  bs->travel_flags, LOCAL_NAVIGATION_AREAS, LOCAL_NAVIGATION_TIME, RSE_NONE, 0, 0, 0);
		start_area = route->endarea;
		VectorCopy(route->endpos, floor_start);
		memset(route, 0, sizeof(aas_predictroute_t));
		trap_AAS_PredictRoute(route,
							  start_area, floor_start,
							  bs->goal.areanum,
							  bs->travel_flags,
							  LOCAL_NAVIGATION_AREAS,
							  LOCAL_NAVIGATION_TIME, RSE_USETRAVELTYPE, 0, ~(TFL_WALK | TFL_AIR | TFL_WATER | TFL_FLIGHT), 0);

		// Compute the direction the bot wants to move before and after walking
		// off the ledge.  The Z axis is ignored for obvious reasons.
		VectorSubtract(ledge_start, bs->now.origin, start_dir);
		VectorSubtract(route->endpos, ledge_start, end_dir);
		start_dir[2] = end_dir[2] = 0;

		// Don't bother starting to walk if not that close to the jump point
		if(VectorNormalize(start_dir) > 32.0)
			return 0;

		// If either direction vector is zero, walk just to be safe
		if(VectorCompare(start_dir, vec3_origin) || VectorCompare(end_dir, vec3_origin))
			return MM_WALK;

		// Also walk if the end destination is almost directly below the jump point
		if(VectorNormalize(end_dir) <= 32.0)
			return MM_WALK;

		// Walk slowly if the final destination isn't in the bot's direction of travel
		if(DotProduct(start_dir, end_dir) < cos(DEG2RAD(30.0)))
			return MM_WALK;

		// It's okay to run off this ledge
	}

	// No movement modifiers are needed for jumping over or off ledges
	return 0;
}

/*
================
BotRouteCanDodge
================
*/
int BotRouteCanDodge(bot_state_t * bs, aas_predictroute_t * route)
{
	float           range;
	gentity_t      *ent;

#ifdef DEBUG_AI
	// Never dodge if dodging has been turned off
	if(bs->debug_flags & BOT_DEBUG_MAKE_DODGE_STOP)
		return 0;
#endif

	// Only bots who are skilled enough dodge
	if(bs->settings.skill < 3)
		return 0;

	// Always dodge if the bot isn't going anywhere
	if(!bs->goal.areanum)
		return MM_DODGE;

	// Bots in lava and slime should just get the hell out
	if(EntityInLavaOrSlime(bs->ent))
		return 0;

	// Don't dodge if the route was predicted and it crossed a mover
	if((route) && (route->stopevent & RSE_ENTERCONTENTS) && (route->endcontents & AREACONTENTS_MOVER))
		return 0;

	// FIXME: Also prevent dodging when the bot is traveling on a moving platform (func bob)

	// Dodging is permitted
	return MM_DODGE;
}

/*
=====================
BotRouteCanStrafeJump
=====================
*/
int BotRouteCanStrafeJump(bot_state_t * bs, aas_predictroute_t * route, int time)
{
	vec3_t          momentum, forward, end_forward, end_momentum, ground;
	trace_t         trace;

	// NOTE: This bounding box is used to check for possible corners
	// that could stop the bot while strafe jumping.  Therefore, it is
	// slightly extended in the X and Y axies for safety, and the Z
	// boundary is signficantly higher (so the bot won't clip its head
	// on a low archway).  These are based on playerMins and playerMaxs
	// in g_client.c.  Incidently, the height of a player's jump is 45
	// units, not 40, given JUMP_VELOCITY 270 and DEFAULT_GRAVITY 800.
	vec3_t          mins = { -15 - 4, -15 - 4, -24 }, maxs =
	{
	15 + 4, 15 + 4, 32 + 40};

#ifdef DEBUG_AI
	// Never strafe jump when it's been deactivated
	if(bs->debug_flags & BOT_DEBUG_MAKE_STRAFEJUMP_STOP)
		return 0;
#endif

	// Only skilled enough bots strafe jump
	if(bs->settings.skill < 3)
		return 0;

	// If the bot will safely reach the goal soon, don't strafe jump
	if(route->stopevent == RSE_NONE && route->time < time)
		return 0;

	// What can be done depends a lot on the physics state
	switch (bs->now.physics.type)
	{
			// When in the air, the bot continues doing what it did before
		case PHYS_GRAVITY:
			return (bs->move_modifiers & MM_STRAFEJUMP);

			// When on the ground, continue checking if strafe jumping is allowed
		case PHYS_GROUND:
			break;

			// By default, strafe jumping is not allowed
		default:
			return 0;
	}

	// The bot must have some forward momentum to strafe jump
	VectorSet(momentum, bs->now.velocity[0], bs->now.velocity[1], 0);
	if(VectorLengthSquared(momentum) <= Square(g_speed.value * .75))
		return 0;

	// Don't strafe jump if the route turns soon
	VectorSubtract(route->endpos, bs->now.origin, forward);
	forward[2] = 0.0;
	VectorNormalize(forward);
	VectorNormalize(momentum);
	if(DotProduct(forward, momentum) < cos(DEG2RAD(15.0)))
		return 0;

	// Test that the path the bot wants to move in is safe from clipping on any corners
	VectorMA(bs->now.origin, 96.0, forward, end_forward);
	trap_Trace(&trace, bs->now.origin, mins, maxs, end_forward, bs->entitynum, MASK_SOLID);
	if(trace.fraction < 1.0)
		return 0;

	// Also test that the bot's current direction of travel is safe from clipping corners
	VectorMA(bs->now.origin, 96.0, momentum, end_momentum);
	trap_Trace(&trace, bs->now.origin, mins, maxs, end_momentum, bs->entitynum, MASK_SOLID);
	if(trace.fraction < 1.0)
		return 0;

	// Find the ground below the end location where the bot will probably end
	VectorSet(ground, end_momentum[0], end_momentum[1], end_momentum[2] - 96.0);
	trap_Trace(&trace, end_momentum, mins, maxs, ground, bs->entitynum, MASK_SOLID);
	VectorCopy(trace.endpos, ground);

	// Don't strafejump if there's too much of a drop-- just too dangerous
	if(trace.fraction >= 1.0)
		return 0;

	// Don't strafe jump if there is a barrier between the end location
	// and where the bot wants to be.
	//
	// NOTE: If this case executes, it's probably because the bot is walking
	// along a ledge.
	trap_Trace(&trace, ground, mins, maxs, end_forward, bs->entitynum, MASK_SOLID);
	if(trace.fraction < 1.0)
		return 0;

	// Store the ideal strafe jumping angles in case the aim code decides to strafe jump
	VectorToAngles(forward, bs->strafejump_angles);
	bs->strafejump_angles[YAW] = AngleNormalize180(bs->strafejump_angles[YAW]);
	bs->strafejump_angles[PITCH] = AngleNormalize180(bs->strafejump_angles[PITCH]);
	return MM_STRAFEJUMP;
}

/*
=====================
BotMoveModifierUpdate
=====================
*/
void BotMoveModifierUpdate(bot_state_t * bs)
{
	int             allowed;
	aas_predictroute_t route;
	qboolean        swimming;

#ifdef DEBUG_AI
	// Never modify movement when the bot has stopped moving
	if(bs->debug_flags & BOT_DEBUG_MAKE_MOVE_STOP)
	{
		bs->move_modifiers = 0;
		return;
	}
#endif

	// Reset the bot's allowed movement styles for this frame
	allowed = 0;

	// Most movement modifiers don't apply when the bot is in water
	//
	// NOTE: It's not impossible to dodge in water.  In fact, the move code tries
	// water dodging if you let it.  It's just that movement in water is so slow
	// (for Quake 3) that dodging doesn't really help much.  What a player really
	// wants to do is leave the water as soon as possible.  Clearly players cannot
	// jump in water, however.
	if(bs->now.physics.type == PHYS_WATER)
	{
		// Check for the swim up modifier
		allowed |= BotRouteSwimUp(bs);
	}

	// Only predict the route if the bot is going somewhere
	else if(bs->goal.areanum)
	{
		// Check the next few movement steps, looking for difficult navigation areas:
		// Stop after a few seconds, a few areas, when reaching the end area, when crossing
		// a mover, or when using a non-standard travel type.
		trap_AAS_PredictRoute(&route,
							  LevelAreaEntity(bs->ent), bs->now.origin,
							  bs->goal.areanum,
							  bs->travel_flags,
							  LOCAL_NAVIGATION_AREAS,
							  LOCAL_NAVIGATION_TIME,
							  RSE_USETRAVELTYPE | RSE_ENTERCONTENTS,
							  AREACONTENTS_MOVER, ~(TFL_WALK | TFL_AIR | TFL_WATER | TFL_FLIGHT), 0);

		// Make some notes for how the bot should modify its movement when on a difficult route
		if(route.stopevent & RSE_USETRAVELTYPE)
			allowed |= BotRouteJump(bs, &route);

		// Otherwise check if standard movement modifiers are legal (strafe jumping and dodging)
		// when not moving through a mover, not in water or flying, not moving with a bobbing
		// platform, and not standing on a mover.
		//
		// NOTE: Standing on a mover is different from moving through a mover or on a bobbing
		// platform.  It may be that the bot is on a mover but moving off of it (in which
		// case the route wouldn't trigger the "enter contents mover" state).
		else if(!((route.stopevent & RSE_ENTERCONTENTS) &&
				  (route.endcontents & AREACONTENTS_MOVER)) &&
				!((route.stopevent & RSE_USETRAVELTYPE) &&
				  (route.endtravelflags & (TFL_SWIM | TFL_FUNCBOB))) && !(EntityOnMoverNow(bs->ent)))
		{
			allowed |= BotRouteCanDodge(bs, &route);
			allowed |= BotRouteCanStrafeJump(bs, &route, LOCAL_NAVIGATION_TIME);
		}
	}

	// Just try to dodge in place if the bot isn't going anywhere
	else
	{
		allowed |= BotRouteCanDodge(bs, NULL);
	}

	// Set the move modifiers allowed this frame
	//
	// NOTE: Other functions use bs->move_modifiers to check what movement was
	// done last frame, so the "allowed" value must be separated cached and
	// updated after all route analysis functions have finished executing.
	bs->move_modifiers = allowed;
}

/*
===================
BotMoveDirJumpCheck

If the bot needs to jump as part of its navigation,
this function will handle any necessary changes.

It returns true if the bot should start jumping
and false if not.  The inputted move direction vector
might be reversed if the bot senses it needs to
back up to get a running start.
===================
*/
qboolean BotMoveDirJumpCheck(bot_state_t * bs, vec3_t move_dir)
{
	float           speed, edge_dist;
	vec3_t          velocity, to_edge;
	qboolean        jump;

	// Only process movement jumps when necessary
	if(!(bs->move_modifiers & MM_JUMP))
		return qfalse;

	// Head for the jump direction
	VectorCopy(bs->jump_dir, move_dir);

	// Don't jump until this block of code says so
	jump = qfalse;

	// Extract the bot's current lateral velocity and speed relative to the jump direction
	VectorSet(velocity, bs->now.velocity[0], bs->now.velocity[1], 0);
	speed = DotProduct(move_dir, velocity);

	// Compute the distance from the bot to the jump edge
	VectorSubtract(bs->jump_start, bs->now.origin, to_edge);
	edge_dist = fabs(DotProduct(bs->jump_edge, to_edge));

	// When moving backwards to get space for a running jump, keep moving backwards
	// until at top speed.  This guarantees the original location can be reached at
	// top speed as well.
	if(bs->jump_backup)
	{
		// Move forward again if moving fast enough or far away; otherwise keep backing up
		if((-speed > g_speed.value * .95) || (edge_dist > 96.0))
			bs->jump_backup = qfalse;
		else
			VectorNegate(move_dir, move_dir);
	}

	// Check if the bot is close to reaching the edge (the plane
	// perpendicular to bs->jump_dir offset through bs->jump_start)
	else if(edge_dist <= 32.0)
	{
		// Jump if the bot is moving fast enough in the right direction;
		// otherwise backup
		if((speed > g_speed.value * .85) && (speed > VectorLength(velocity) * cos(DEG2RAD(5.0))))
		{
			jump = qtrue;
		}
		else
		{
			bs->jump_backup = qtrue;
			VectorNegate(move_dir, move_dir);
		}
	}

	// Tell the caller whether or not the bot needs to jump now
	return jump;
}

/*
==============
BotMoveProcess

This function packages the bot's requested move
data into a syntax easily understandable by the
server.  It also provides a sane interface (ie.
spherical coordinates) for other bits of bot code
that need to know how the bot plans on moving.
==============
*/
void BotMoveProcess(bot_state_t * bs)
{
	int             mm, jump_crouch;
	float           speed_rate;
	vec3_t          move_dir;
	bot_input_t     bi;

	// The walk, jump, and swim up movement modifiers always apply if permitted
	mm = (bs->move_modifiers & (MM_WALK | MM_JUMP | MM_SWIMUP));

	// Use strafe jumping if permitted and the bot is aiming correctly
	if(bs->aim_type == AIM_STRAFEJUMP)
		mm |= (bs->move_modifiers & MM_STRAFEJUMP);

	// Retrieve the bot's input and output structures
	//
	// NOTE: This code only cares about the movement data
	trap_EA_GetInput(bs->client, 0.0, &bi);

	// Sometimes the engine tells the bot to jump at bad times.  However, you can't
	// strip out all of the requested jumps because some of them ARE important.
	// For safetly sake, however, it's best to strip out requested jumps when the
	// bot is on a moving platform.  That's just dangerous, and the engine doesn't
	// know the bot's current velocity, so it can't possibly tell if the jump will
	// make the bot fall off (and usually it does).
	if(EntityOnMoverNow(bs->ent))
		bi.actionflags &= ~ACTION_JUMP;

	// Extract the requested movement direction
	VectorCopy(bi.dir, move_dir);

	// Compute the requested speed rate-- 1.0 means full speed, 0.0 means no speed
	//
	// NOTE: bi.speed is in the range [0, 400] and refers to movement speeds in the
	// interval [0, g_speed].  g_speed is usually 320 though, not 400.  So bi.speed 200
	// means the bot should move at speed 160.  This is further complicated by the
	// user command speeds, which should be in the interval [-127, +127], with the
	// interval [0, 127] roughly (but not perfectly) mapping to [0, g_speed].  See
	// PM_CmdScale() in bg_pmove.c for more information on this imperfect mapping.
	// Essentially the movement interface between clients and servers is far, far
	// too complicated and messy.  So this code just translates from [0, 400] to
	// [0, 1] and lets the remaining code determine how to remap that interval.
	//
	// NOTE: It's probably impossible for the bot input data to send speeds higher
	// than 400 or less than 0, but it's best to check just to be safe.  Very
	// strange things would happen if that ever occurred and these checks weren't
	// in place.
	if(bi.speed <= 0)
		speed_rate = 0.0;
	else if((mm & MM_WALK) && (bi.speed < 400))
		speed_rate = bi.speed / 400;
	else
		speed_rate = 1.0;

	// Skill 1 bots don't move while their weapon is reloading, unless the
	// weapon is a melee weapon (eg. gauntlet)
	//
	// NOTE: They are still allowed to jump
	if((bs->settings.skill <= 1) && (bs->ps->weaponTime > 0.0) && !(weapon_stats[bs->ps->weapon].flags & WSF_MELEE))
	{
		speed_rate = 0.0;
	}

	// Check if the bot needs to jump or crouch this frame
	//
	// NOTE: The BotMoveDirJumpCheck() function call can modify the movement direction

	// Always strafejump if requested
	if(mm & MM_STRAFEJUMP)
		jump_crouch = MJC_STRAFEJUMP;

	// Jump if navigation jumping requires it
	else if(BotMoveDirJumpCheck(bs, move_dir))
		jump_crouch = MJC_NAVJUMP;

	// "Jumping" in water swims up for air and such
	else if(mm & MM_SWIMUP)
		jump_crouch = MJC_NAVJUMP;

	// If the bot will navigation jump in the future but doesn't want to now, do nothing
	else if(mm & MM_JUMP)
		jump_crouch = MJC_NONE;

	// If a jump or crouch was requested by the movement engine, listen to it
	else if(bi.actionflags & ACTION_JUMP)
		jump_crouch = MJC_NAVJUMP;
	else if(bi.actionflags & ACTION_CROUCH)
		jump_crouch = MJC_CROUCH;

	// Otherwise do nothing
	else
		jump_crouch = MJC_NONE;

	// Setup the movement commands according to these preferences
	BotCommandMove(bs, move_dir, speed_rate, jump_crouch);
}
