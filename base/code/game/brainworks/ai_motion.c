// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_motion.c
 *
 * Functions the bot uses to detect motion in entities
 *****************************************************************************/

#include "ai_main.h"
#include "ai_vars.h"
#include "ai_motion.h"

#include "ai_entity.h"
#include "ai_predict.h"


// The maximum number motion states that will be stored in the history
//
// NOTE: Because these structures are used in large arrays, increasing this
// value will greatly increase the amount of memory allocated.
#define MAX_MOTION_HISTORY 12

// The last few frames of motion data for something (probably an entity)
typedef struct motion_history_s
{
	int             size;		// Number of entries in ring buffer (probably MAX_MOTION_HISTORY)
	int             oldest;		// Ring buffer index for oldest entry
	int             newest;		// Ring buffer index for newest entry

	motion_state_t  motion[MAX_MOTION_HISTORY];	// Ring buffer of past motion data
	float           server_time[MAX_MOTION_HISTORY];	// Ring buffer of estimated server time when
	// the associated motion state was processed
} motion_history_t;


// Motion history data for all players
motion_history_t player_motion_history[MAX_CLIENTS];


/*
===================
EntityMotionHistory

Returns a point to the entity's motion history data
if it exists or NULL if not
===================
*/
motion_history_t *EntityMotionHistory(gentity_t * ent)
{
	int             index;

	// Ignore invalid entities
	if(!ent || !ent->inuse)
		return NULL;

	// Check if the entity has a valid array entry
	index = ent - g_entities;
	if(index < 0 || index >= MAX_CLIENTS)
		return NULL;

	// Return the entity's motion history information
	return &player_motion_history[index];
}

/*
======================
EntityMotionUpdateRate

Returns an estimate of how many milliseconds
elapse between each of the entity's updates.
======================
*/
float EntityMotionUpdateRate(gentity_t * ent)
{
	int             extra_frames;
	float           time_change;
	motion_history_t *history;

	// Synchronized entities have a fixed update rate
	if(EntityUpdatesSynchronous(ent))
		return SERVER_FRAME_DURATION;

	// If no history data exists, guess at the update rate
	history = EntityMotionHistory(ent);
	if(!history)
		return SERVER_FRAME_DURATION;

	// Also guess if there isn't enough data
	extra_frames = history->size - 1;
	if(extra_frames <= 0)
		return SERVER_FRAME_DURATION;

	// Compute the average time elapsed per update since the oldest frame
	time_change = history->motion[history->newest].time - history->motion[history->oldest].time;

	// The update rate is the ratio between the number of additional frames
	// and the time elapsed between these frames
	return time_change / (float)extra_frames;
}

/*
=================================
EntityMotionStateUpdateCachedData

Whenever the details of an entity's motion state
changes, it's possible that some of the cached
data like water level and physics will change.
Those values must get recomputed.

NOTE: While this function just updates the
physics data, it does not recompute it from
scratch.  Some older physics information goes
into recomputing newer physics information (in
particular, the walking flag).  When setting up
a motion state for the very first time, make
sure the physics state has been reset as well
(or at least appropriately reset this flag).
=================================
*/
void EntityMotionStateUpdateCachedData(gentity_t * ent, motion_state_t * motion)
{
	qboolean        was_walking;

	// Recompute the water level based on the current motion state
	motion->water_level = EntityWaterLevel(ent, motion->origin, motion->crouch);

	// Remember if the entity was walking before this frame
	was_walking = motion->physics.walking;

	// Recompute the physics
	//
	// NOTE: This function call depends on the previously recomputed water level
	EntityPhysics(ent, &motion->physics, motion->origin, motion->mins, motion->maxs,
				  motion->velocity, motion->water_level, motion->flight,
				  ((motion->move_flags & PMF_TIME_KNOCKBACK) && (motion->time < motion->move_time)));

	// Check if the entity just took a hard landing on a ground surface
	//
	// NOTE: The velocity check is based on code in PM_GroundTrace() in bg_pmove.c.
	if((!was_walking) && (motion->physics.walking) && (motion->velocity[2] < -200.0))
	{
		// Make it hard to move for a bit
		motion->move_flags |= PMF_TIME_LAND;
		motion->move_time = motion->time + 0.025;
	}
}

/*
====================
EntityMotionStateNow

Fill out a frame containing the entity's
current motion data
====================
*/
void EntityMotionStateNow(gentity_t * ent, motion_state_t * motion)
{
	// Clear the motion state if the caller supplied an invalid entity
	if(!ent || !ent->inuse)
	{
		memset(motion, 0, sizeof(motion_state_t));
		return;
	}

	// The entity's perceived update time
	motion->time = EntityTimestamp(ent);

	// Position, global, and local bounding box
	EntityCenterAllBounds(ent, motion->origin, motion->absmin, motion->absmax, motion->mins, motion->maxs);

	// Velocity
	if(ent->client)
	{
		VectorCopy(ent->client->ps.velocity, motion->velocity);
	}
	else
	{
		// Velocities only matter for non-stationary entities
		//
		// NOTE: Yes, this is important.  There are some stationary game
		// entities with non-zero velocities (like the q3tourney6 disco
		// ball).  If a stationary entity has a bad velocity, the bot
		// will try to predict its movement and its aim will miss horribly.
		if(ent->s.pos.trType == TR_STATIONARY)
			VectorClear(motion->velocity);
		else
			VectorCopy(ent->s.pos.trDelta, motion->velocity);
	}

	// Miscellaneous values
	motion->clip_mask = EntityClipMask(ent);
	motion->flags = ent->s.eFlags;
	motion->crouch = EntityCrouchingNow(ent);
	if(ent->client)
	{
		motion->flight = (ent->client->ps.powerups[PW_FLIGHT] != 0);
		motion->max_speed = ent->client->ps.speed;
		motion->move_flags = (ent->client->ps.pm_flags & PMF_ALL_TIMES);
		if((motion->move_flags) && (ent->client->ps.pm_time > 0))
			motion->move_time = motion->time + ent->client->ps.pm_time * 0.001;
		else
			motion->move_time = 0.0;
	}
	else
	{
		motion->flight = qfalse;
		motion->max_speed = 0.0;
		motion->move_flags = 0x0000;
		motion->move_time = 0.0;
	}

	// Movement commands and view angles
	if(ent->client)
	{
		motion->forward_move = ent->client->pers.cmd.forwardmove;
		motion->right_move = ent->client->pers.cmd.rightmove;
		motion->up_move = ent->client->pers.cmd.upmove;
		VectorCopy(ent->client->ps.viewangles, motion->view);
	}
	else
	{
		motion->forward_move = 0;
		motion->right_move = 0;
		motion->up_move = 0;
		VectorClear(motion->view);
	}

	// The physics walking flag must be pre-seeded with entity data
	motion->physics.walking = (ent->s.groundEntityNum != ENTITYNUM_NONE);

	// Update the cached data once the rest of the data has been setup
	EntityMotionStateUpdateCachedData(ent, motion);
}

/*
============================
EntityMotionStateInterpolate

Interpolates two motion states "a" and "b"
at time "time" and stores their result in
"result".  By default, all digital values will
be read from the newer entry, since they cannot
be interpolated.  However, if the input time
favors an interpolation using only the older state
for the analog values, the digital values will
be read from that state as well.

NOTE: It is permitted for "result", "a", and
"b" to all point to the same structures.
============================
*/
void EntityMotionStateInterpolate(gentity_t * ent, motion_state_t * a, motion_state_t * b, float time, motion_state_t * result)
{
	int             i;
	float           weight, comp_weight, newer_view, older_view;
	vec3_t          temp;
	motion_state_t *older, *newer;

	// Determine which state is older and which is newer
	if(a->time < b->time)
	{
		older = a;
		newer = b;
	}
	else if(b->time < a->time)
	{
		older = b;
		newer = a;
	}

	// If they have the same timestamp, prefer the first input
	else
	{
		memcpy(result, a, sizeof(motion_state_t));
		return;
	}

	// Check for timestamps exceeding the input time boundaries
	if(time <= older->time)
	{
		memcpy(result, older, sizeof(motion_state_t));
		return;
	}
	if(time >= newer->time)
	{
		memcpy(result, newer, sizeof(motion_state_t));
		return;
	}

	// When teleporting, only use data from the newer entry
	//
	// NOTE: In this case, the resultant motion state will not
	// have the requested timestamp, because that data could not
	// be computed with enough certainty.
	if((newer->flags ^ older->flags) & EF_TELEPORT_BIT)
	{
		memcpy(result, newer, sizeof(motion_state_t));
		return;
	}

	// Determine the interpolation weight between the two entries
	// (1.0 means use all of the newer entry, 0.0 means use all of
	// the older)
	//
	// NOTE: When teleporting, only use data from the newer entry
	//
	// NOTE: This won't divide by zero because the code tested earlier
	// that the newer time was greater than the older time.
	weight = (time - older->time) / (newer->time - older->time);
	comp_weight = 1.0 - weight;

	// Interpolate the time
	result->time = weight * newer->time + comp_weight * older->time;

	// Also interpolate position and velocity
	//
	// FIXME: Technically this is incorrect, because a linear change in
	// velocity would not generate a linear change in position.  Also, the
	// client code uses a linear interpolation of position, so inverting
	// that to compute the velocity change that would generate such a
	// position translation is a, ah... difficult endeavour.
	VectorScale(newer->origin, weight, temp);
	VectorMA(temp, comp_weight, older->origin, result->origin);
	VectorScale(newer->velocity, weight, temp);
	VectorMA(temp, comp_weight, older->velocity, result->velocity);

	// Interpolate user command view angles, making sure the angles
	// "turn" the right way
	for(i = PITCH; i <= ROLL; i++)
	{
		newer_view = AngleNormalize360(newer->view[i]);
		older_view = AngleNormalize360(older->view[i]);
		if(newer_view - older_view > 180)
			newer_view -= 360;
		if(older_view - newer_view > 180)
			older_view -= 360;

		result->view[i] = weight * newer_view + comp_weight * older_view;
		result->view[i] = AngleNormalize180(result->view[i]);
	}

	// Some data changes are digital, not analog, so they are not interpolated--
	// they are always copied from the first motion state

	// Bounding boxes
	VectorCopy(newer->mins, result->mins);
	VectorCopy(newer->maxs, result->maxs);
	VectorCopy(newer->absmin, result->absmin);
	VectorCopy(newer->absmax, result->absmax);

	// Miscellaneous values
	//
	// NOTE: It IS possible to interpolate the movement time and flags, but that
	// operation isn't well defined, and it's not clear doing so would help that
	// much.
	result->clip_mask = newer->clip_mask;
	result->flags = newer->flags;
	result->crouch = newer->crouch;
	result->flight = newer->flight;
	result->max_speed = newer->max_speed;
	result->move_flags = newer->move_flags;
	result->move_time = newer->move_time;

	// User commands
	result->forward_move = newer->forward_move;
	result->right_move = newer->right_move;
	result->up_move = newer->up_move;

	// Pre-seed the walking flag with the older motion state's flag
	result->physics.walking = older->physics.walking;

	// Update the cached data once the rest of the data has been setup
	EntityMotionStateUpdateCachedData(ent, result);
}

/*
=====================
EntityMotionStateTime

Tries to fill out the motion state record with
the entity's motion information at the specified
time.

NOTE: "time" is relative to the entity's perception
(ie. EntityTimestamp() and motion->time), not to
the server's time (ie. server_time).

NOTE: Use EntityMotionStateNow() for the entity's
motion state at the current point in time.
=====================
*/
void EntityMotionStateTime(gentity_t * ent, motion_state_t * motion, float time)
{
	int             index, end_index;
	float           time_shift, weight;
	motion_state_t *older, *newer;
	motion_history_t *history;

	// If no data exists in this entity's historical record, use current data instead
	history = EntityMotionHistory(ent);
	if(!history || history->size <= 0)
	{
		EntityMotionStateNow(ent, motion);
		return;
	}

	// FIXME: It's possible to write this code using binary search, splitting the ring
	// buffer into two sorted lists and searching the appropriate one.  It also seems
	// like an awful lot of extra code to speed up something that probably isn't the
	// execution bottleneck anyway.

	// Setup the initial pair of motion states to check
	//
	// NOTE: Newer refers to an unused entry when size = 1.  The code will implicitly
	// check this case during the while() loop.  Of course, the older entry is guaranteed
	// to exist because size > 0.
	//
	// NOTE: "index" refers to the index of the newer motion state, not the older.
	index = history->oldest;
	older = &history->motion[index];
	index = (index + 1) % MAX_MOTION_HISTORY;
	newer = &history->motion[index];

	// If the ideal motion time isn't newer than the oldest known entry,
	// use the oldest entry instead
	if(time <= older->time)
	{
		memcpy(motion, older, sizeof(motion_state_t));
		return;
	}

	// Stop searching when the index would refer to an invalid pair
	//
	// NOTE: end_index won't equal history->oldest when history->size < MAX_MOTION_HISTORY
	end_index = (history->newest + 1) % MAX_MOTION_HISTORY;

	// Search the historical motion data for two near matches, given the known constraint
	// that the ideal time is later than the entity timestamp of the pair's older entry
	while(index != end_index)
	{
		// If the ideal time is no later than the newer entry, interpolate this
		// pair of motion states
		//
		// NOTE: The +1e-5 guarantees that slight floating point errors won't
		// give this test a false positive when in fact the values are equal
		if(time <= newer->time + 1e-5)
		{
			EntityMotionStateInterpolate(ent, newer, older, time, motion);
			return;
		}

		// Check the next pair
		older = newer;
		index = (index + 1) % MAX_MOTION_HISTORY;
		newer = &history->motion[index];
	}

	// The requested time is too recent for the historical motion data
	// so use the newest data instead
	//
	// NOTE: Technically it's possible that this entry isn't actually
	// the newest data.  Maybe the data from EntityMotionStateNow()
	// would be newer,  But that will never occur unless this function
	// is called before the BotAIMotionUpdate() call for the frame.
	memcpy(motion, &history->motion[history->newest], sizeof(motion_state_t));
}

/*
==================
MotionHistoryReset

Resets one historical set of motion states.
==================
*/
void MotionHistoryReset(motion_history_t * history)
{
	// Confirm that the caller provided a valid history record
	if(!history)
		return;

	// Invalidate all entries in the ring buffer
	history->size = 0;

	// Start adding new entries (added to oldest) at the front of the array
	//
	// NOTE: Any index in the array is an acceptable starting point, but
	// the data must still get initialized the very first time.
	history->oldest = 0;

	// Set the newest index appropriately, even though technically there is
	// no "newest" entry (and neither an "oldest" entry).
	//
	// NOTE: To be mathematically correct, the newest value must be
	// -1 % MAX_MOTION_HISTORY.  This is because size = (newest - oldest) + 1
	// Since size = 0 and oldest = 0:
	//
	//   0 = (newest - 0) + 1
	//   0 = newest + 1
	//   -1 = newest = MAX_MOTION_HISTORY - 1
	//
	// Properly setting this value probably doesn't matter, but it's good to
	// be correct, just in case.
	history->newest = MAX_MOTION_HISTORY - 1;
}

/*
================
BotAIMotionReset

Resets all the historical motion data used in
motion tracking.
================
*/
void BotAIMotionReset(void)
{
	int             i;

	// Reset each motion history
	//
	// NOTE: This function does not call EntityMotionHistory() because
	// it doesn't matter whether or not the entity associated with that index
	// is in use-- all motion states must get reset in any case.
	for(i = 0; i < MAX_CLIENTS; i++)
		MotionHistoryReset(&player_motion_history[i]);

	// Also reset predicition data
	BotAIPredictReset();
}

/*
=========================
EntityMotionHistoryUpdate

Attempts to update a motion history using additional
data from an entity.
=========================
*/
void EntityMotionHistoryUpdate(gentity_t * ent)
{
	float           command_time;
	motion_history_t *history;
	motion_state_t *newest;

	// Look up this entity's motion history
	history = EntityMotionHistory(ent);

	// Ignore entities that aren't in use or aren't connected players
	if(!history)
		return;

	// Reset data from any spectator (ie. uninteresting entity) or unconnected player
	if((EntityTeam(ent) == TEAM_SPECTATOR) || (ent->client && (ent->client->pers.connected != CON_CONNECTED)))
	{
		MotionHistoryReset(history);
		return;
	}

	// Extract the newest motion entry
	if(history->size > 0)
		newest = &history->motion[history->newest];
	else
		newest = NULL;

	// Skip entities whose next command hasn't been processed by the server
	//
	// NOTE: This check includes bots, whose commands are processed each server frame
	command_time = EntityTimestamp(ent);
	if(newest && command_time <= newest->time)
		return;

	// Overwrite the next buffer entry seqentially with the new state information--
	// either this record hasn't been used yet or it's the oldest record
	history->newest = (history->newest + 1) % MAX_MOTION_HISTORY;

	// Either the buffer will grow or there is a new oldest entry
	if(history->size < MAX_MOTION_HISTORY)
		history->size++;
	else
		history->oldest = (history->oldest + 1) % MAX_MOTION_HISTORY;

	// Fill out the newest state of motion data with basic player state information
	EntityMotionStateNow(ent, &history->motion[history->newest]);

	// Estimate the actual time the server executed the last update on this entity
	//
	// NOTE: This timestamp is not known for sure because some entities are not
	// updated synchronously with the server.  (In particular, human clients.)
	// These are processed when the server receives a GAME_CLIENT_THINK command,
	// and that command is not timestamped.  So the timestamps of asynchronous
	// updates are estimated using the current AI frame time.
	//
	// See vmMain() in g_main.c and ClientThink() in g_active.c for more information.
	history->server_time[history->newest] = (EntityUpdatesSynchronous(ent) ? server_time : ai_time);

#ifdef DEBUG_AI
	// Possibly test prediction of this entity
	PredictDebugEntityNow(ent);
#endif

}

/*
=================
BotAIMotionUpdate

Updates the historical motion data used in
motion tracking if any new data was found.
=================
*/
void BotAIMotionUpdate(void)
{
	int             i;

	// Update each player's motion state if necessary
	for(i = 0; i < MAX_CLIENTS; i++)
		EntityMotionHistoryUpdate(&g_entities[i]);

#ifdef DEBUG_AI
	// Check for motion state predictions whose reality has occurred
	//
	// NOTE: This occurs before updating because it's possible this
	PredictDebugCheck();
#endif
}

/*
================
BotEntityLatency

Returns the estimated amount of latency the bot has relative
to this entity.  The latency is the estimated amount of time
the entity will update before the server processes the bot's
next command.

FIXME: This code currently estimates the amount of latency
for humans in a fairly analog manner-- however much time is
left until the server updates is the amount the player will
update.  Unfortunately, updates are a very digital thing.
The player either updates in one big chunk or not.

For example, suppose a player pings at 80 ms (ie. one command
every 80 milliseconds) and the last command was received 10 ms
ago.  Now suppose there are 25 ms until the server updates
again.  It's a pretty safe assumption that this semi-lagged
player will update ZERO milliseconds before the server
processes the bot commands, not 25 ms.  These estimates are
pretty accurate for fast connections (especially local players)
though.  It's possible to apply some algorithm to statistically
analyse the standard deviation of the player's lag and get a
pretty good idea if the player will send a new frame between
now and the next update.  If an update would occur, the full
latency time of 80 ms (or whatever their average command time
differential is) would be returned.  Otherwise 0 is returned.
This would improve the bots' ability to track lagged players
connected to the server, especially those with pings greater
than twice SERVER_FRAME_DURATION.
================
*/
float BotEntityLatency(bot_state_t * bs, gentity_t * ent)
{
	float           motion_time_lapse, server_time_lapse, speed_ratio, latency;
	motion_history_t *history;
	motion_state_t *oldest, *newest;

	// It's easy to estimate the latency of entities that synchronously update
	if(EntityUpdatesSynchronous(ent))
	{
		// Since synchronized entities update in ascending order, entities that
		// update before the bot will process one extra server frame before the
		// bot does while entities that update after the bot will not
		if(ent < bs->ent)
			return SERVER_FRAME_DURATION;
		else
			return 0.0;
	}

	// Look up the entity's motion history if it's being tracked; otherwise
	// the latency is unestimatable
	//
	// NOTE: Currently only clients can update asynchronously, so this test
	// should never trigger.
	history = EntityMotionHistory(ent);
	if(!history)
		return 0.0;

	// The latency cannot be estimated if there are no motion states in the history
	//
	// NOTE: This should never occur because BotAILatencyUpdate() will (should)
	// always be called whenever this function could be called
	if(history->size <= 0)
		return 0.0;

	// Look up the oldest and newest motion records
	oldest = &history->motion[history->oldest];
	newest = &history->motion[history->newest];

	// Compute how fast the client is updating relative to the server
	//
	// NOTE: The game updates the client as fast or slow as the client requests,
	// given some modest bounds to prevent speed hacks.  (See ClientThink_real() in
	// g_active.c for more information.)  This ratio will probably never be exactly
	// 1.0, due to time estimation errors.  (See EntityMotionHistoryUpdate() for
	// more information.)  Having a particularly high ratio could mean the player
	// is cheating, or just that they're recovering from a bout of lag.  But even if
	// the player is cheating, it's not the job of the AI code to police it.  This
	// code simply teaches the bot to properly react to such cheating.
	//
	// FIXME: Lets be honest.  Players aren't actually using speed hacks.  Maybe
	// this code should just fix the speed_ratio at 1.0.  It's not clear which
	// would produce more accurate estimates.
	motion_time_lapse = newest->time - oldest->time;
	server_time_lapse = history->server_time[history->newest] - history->server_time[history->oldest];
	if(motion_time_lapse <= 0.0 || server_time_lapse <= 0.0)
		speed_ratio = 1.0;
	else
		speed_ratio = motion_time_lapse / server_time_lapse;

	// Compute how much time will elapse before the next server frame
	//
	// NOTE: Yes, it's possible for the current AI time to be greater than
	// even the next server frame which hasn't yet executed.
	latency = bs->command_time - ai_time;
	if(latency < 0.0)
		latency = 0.0;

	// Convert the latency from the server's time rate to the entity's time rate
	latency *= speed_ratio;

	// Return the estimated amount of time that entity will update by the next server
	// frame (ie. when the bot's next command is executed)
	return latency;
}

/*
=====================
BotEntityMotionLagged

This function attempts to find (or estimate) one state of an
entity's motion data such that the bot must predict exactly "lag"
seconds to estimate the entity's state at the time the bot next
executes a command.  In other words, this function generates
entity data that's lagged by a constant amount-- in particular,
by "lag" seconds.

This function returns the actual number of seconds the bot needs
to predict ahead to estimate the entity's state at the bot's next
command execution.  Ideally this will always be "lag" seconds, but
it's possible this code won't have enough old historical motion
data to do this.  The returned value could be as low as zero seconds,
if no historical data was kept and the bot's next command should
execute as soon as this AI frame finishes.

Conversely, it's also possible that the returned amount of lag
will be higher than the requested input.  This can occur when it
will take a while before the bot's next command is executed, and
the caller only requested a small amount of lag.  If the bot in such
a situation requests data on an entity that updates very quickly
(like a player with a low ping), there's a fixed amount of lag that
the bot simply cannot get around.

So the moral is that while this function tries to return "lag"
seconds, it might just do the best it can.  It's really important
for the caller to respect the return value of this function.
=====================
*/
float BotEntityMotionLagged(bot_state_t * bs, gentity_t * ent, float lag, motion_state_t * motion)
{
	float           min_lag, ent_time, ideal_time;

	// Sanity check the requested lag
	if(lag < 0.0)
		lag = 0.0;

	// Compute the minimum amount of latency this entity's motion has
	// relative to the bot
	min_lag = BotEntityLatency(bs, ent);

	// Estimate the entity's timestamp (from its own perception) when the bot will
	// execute its next command
	ent_time = EntityTimestamp(ent) + min_lag;

	// A motion state at this time will have the requested amount of lag
	ideal_time = ent_time - lag;

	// Attempt to find the entity's motion state at that time
	EntityMotionStateTime(ent, motion, ideal_time);

	// Return the actual latency between the retrieved motion state and
	// the next timestamp
	lag = ent_time - motion->time;
	return (lag > 0.0 ? lag : 0.0);
}

/*
===============
BotMotionUpdate

Updates the bot's understanding of its motion
state (bs->now) as the server last understood it.
Also forces an update of its anticipated motion
state for the upcoming server frame (bs->future).
===============
*/
void BotMotionUpdate(bot_state_t * bs)
{
	// Cache the bot's current motion state
	EntityMotionStateNow(bs->ent, &bs->now);

	// Compute the bot's current eye coordinates, used for scanning world data now
	//
	// NOTE: The server's understanding of the eye is snapped but the origin is not
	//
	// FIXME: Give the motion state an understanding of viewheight (which changes
	// while crouching) and also the eye position.  It's a more natural place for
	// it than here.
	VectorCopy(bs->now.origin, bs->eye_now);
	SnapVector(bs->eye_now);
	bs->eye_now[2] += bs->ps->viewheight;

	// Force the future motion state to get repredicted the next time
	// BotMotionFutureUpdate() is called
	bs->future.time = bs->now.time;
}
