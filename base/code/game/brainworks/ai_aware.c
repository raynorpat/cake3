// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_aware.c
 *
 * Functions that the bot uses to get/set information for the enemy awareness
 * system
 *****************************************************************************/

#include "ai_main.h"
#include "ai_vars.h"
#include "ai_aware.h"

#include "ai_client.h"
#include "ai_entity.h"


/*
==================
CompareEntityAware

Compare an entity pointer to an awareness record.
=====================
*/
int QDECL CompareEntityAware(const void *ent, const void *aware)
{
	const gentity_t **ent_p = (const gentity_t **)ent;
	const bot_aware_t *aware_p = (const bot_aware_t *)aware;

	return (*ent_p - aware_p->ent);
}

/*
=================
BotAwarenessReset

Resets the bot's awareness.  Useful when the
bot respawns, teleports, and so on.
=================
*/
void BotAwarenessReset(bot_state_t * bs)
{
	int             max_aware;

	// Determine the number of entities the bot will actually remain aware of
	max_aware = (MAX_AWARE_ENTITIES - 5 + bs->settings.skill);
	if(max_aware > MAX_AWARE_ENTITIES)
		max_aware = MAX_AWARE_ENTITIES;
	else if(max_aware < MAX_AWARE_ENTITIES / 2)
		max_aware = MAX_AWARE_ENTITIES / 2;

	// Set up the timed value list for awareness
	tvl_setup(&bs->aware, max_aware, sizeof(bot_aware_t), bs->aware_record,
			  bs->aware_timeout, bs->aware_value, CompareEntityAware);
}

/*
======================
BotBestAwarenessEntity

Returns a pointer to the highest rated entity in the
bot's awareness list, or NULL if no such entity exists.
======================
*/
gentity_t      *BotBestAwarenessEntity(bot_state_t * bs)
{
	bot_aware_t    *aware;

	// Search for the highest valued entry.
	// In case of ties, prefer the bot's aim enemy if possible.
	//
	// FIXME: Technically this function call should ignore enemies that the bot hasn't
	// reacted to being aware of.  The best way of doing this is probably to set the
	// score to 0 until awareness occurs.  In reality, this isn't the biggest deal
	// in the world since this function is just used to determine the goal enemy
	// (who the bot moves towards), and doing that a few tenths of a second faster
	// won't be noticed.
	aware = tvl_highest_value(&bs->aware, &bs->aim_enemy);
	if(!aware)
		return NULL;
	return aware->ent;
}

/*
====================
BotAwarenessOfEntity

Returns the bot's awareness information regarding the
given entity if the bot is aware of it, or NULL if not.
====================
*/
bot_aware_t    *BotAwarenessOfEntity(bot_state_t * bs, gentity_t * ent)
{
	bot_aware_t    *aware;

	// Fail if the bot doesn't know about this entity
	aware = (bot_aware_t *) tvl_search(&bs->aware, &ent);
	if(!aware)
		return NULL;

	// Also fail if the bot hasn't consciously processed the event that
	// made this entity notable
	if(bs->command_time < aware->first_noted + bs->react_time)
		return NULL;

	// The bot is aware of this entity
	return aware;
}

/*
================
BotSightedEntity

Returns true if the bot has sighted the
enemy and false if not.
================
*/
qboolean BotSightedEntity(bot_state_t * bs, gentity_t * ent)
{
	bot_aware_t    *aware;

	// Fail if the bot isn't aware of the entity
	aware = BotAwarenessOfEntity(bs, ent);
	if(!aware)
		return qfalse;

	// Fail if the entity isn't currently sighted by the bot
	if(aware->sighted <= 0)
		return qfalse;

	// Fail if the entity hasn't been sighted long enough for the bot to react
	if(bs->command_time < aware->sighted + bs->react_time)
		return qtrue;

	// The bot has sighted this target
	return qtrue;
}

#ifdef DEBUG_AI
/*
======================
BotPrintAwarenessTrack
======================
*/
void BotPrintAwarenessTrack(tvl_t * tvl, int index, void *bs)
{
	bot_aware_t    *aware;

	// Print output stating the target was detected
	aware = (bot_aware_t *) tvl_data(tvl, index);
	BotAI_Print(PRT_MESSAGE, "%s: Awareness: Tracking %s\n",
				EntityNameFast(((bot_state_t *) bs)->ent), EntityNameFast(aware->ent));
}

/*
=====================
BotPrintAwarenessLoss
=====================
*/
void BotPrintAwarenessLoss(tvl_t * tvl, int index, void *bs)
{
	bot_aware_t    *aware;

	// Print output stating the target was lost
	aware = (bot_aware_t *) tvl_data(tvl, index);
	BotAI_Print(PRT_MESSAGE, "%s: Awareness: Lost track of %s\n",
				EntityNameFast(((bot_state_t *) bs)->ent), EntityNameFast(aware->ent));
}
#endif

/*
=======================
BotAwareTestEntityAlive
=======================
*/
int BotAwareTestEntityAlive(tvl_t * tvl, int index, void *bs)
{
	bot_aware_t    *aware;

	// Extract a pointer to the awareness record
	aware = (bot_aware_t *) tvl_data(tvl, index);

	// Ignore non-entities
	//
	// NOTE: This case should never execute
	if(!aware->ent)
		return qfalse;

	// Disconnected players are not alive
	if(aware->ent->client && aware->ent->client->pers.connected != CON_CONNECTED)
		return qfalse;

	// Spectators don't count either
	if(EntityTeam(aware->ent) == TEAM_SPECTATOR)
		return qfalse;

	// The entity is not alive if it has no health
	if(aware->ent->health <= 0)
		return qfalse;

	// The entity is alive
	return qtrue;
}

/*
==================
BotAwarenessUpdate
==================
*/
void BotAwarenessUpdate(bot_state_t * bs)
{
	// Update the list timestamp, possibly printing out which entries were removed
#ifdef DEBUG_AI
	if(bs->debug_flags & BOT_DEBUG_INFO_AWARENESS)
		tvl_update_time(&bs->aware, bs->command_time, BotPrintAwarenessLoss, bs);
	else
#endif
		tvl_update_time(&bs->aware, bs->command_time, 0, NULL);

	// Remove dead players and spectators from the list
#ifdef DEBUG_AI
	if(bs->debug_flags & BOT_DEBUG_INFO_AWARENESS)
		tvl_update_test(&bs->aware, BotAwareTestEntityAlive, BotPrintAwarenessLoss, bs);
	else
#endif
		tvl_update_test(&bs->aware, BotAwareTestEntityAlive, 0, bs);

	// This check is fast and might make someone's life easier
	if(bs->aware_location_time < bs->command_time)
		bs->aware_location_time = 0;
}

/*
===================
BotAwareTrackEntity

Track this entity in the awareness engine if it's an enemy
Returns true if the entity was tracked and false if not.

If an alertness 1.0 bot is "event_radius" units or closer to the
source of the event, the bot will become aware of the entity.
Otherwise it ignores it.  Less aware bots have a smaller radius, down to
  bot_aware_skill_factor * event_radius
for alertness 0.0 bots.  Of course, the distance is ignored when the
bot is already aware of the target.

"refresh_radius" is the event radius used when the bot is already
aware of the entity.  If this value is less than the event radius
(eg. -1), the default radius of
  bot_aware_refresh_factor * event_radius
is used.
===================
*/
qboolean BotAwareTrackEntity(bot_state_t * bs, gentity_t * ent, float event_radius, float refresh_radius)
{
	int             index;
	float           aware_factor, skill_weight, timeout;
	bot_aware_t     aware;

	// Only become aware of enemies
	if(!BotEnemyTeam(bs, ent))
		return qfalse;

	// Determine how long the bot will stay aware of this target if it notices it
	// and how close the bot needs to be to the event to notice it
	aware_factor = trap_Characteristic_BFloat(bs->character, CHARACTERISTIC_ALERTNESS, 0, 1);
	skill_weight = interpolate(bot_aware_skill_factor.value, 1.0, aware_factor);
	timeout = bs->command_time + skill_weight * bot_aware_duration.value;
	event_radius *= skill_weight;

	// If the bot is already aware, use the expanded event radius
	index = tvl_data_index(&bs->aware, &ent);
	if(index >= 0)
	{
		// Use the default refresh radius unless a different one was supplied
		if(refresh_radius < event_radius)
			event_radius *= bot_aware_refresh_factor.value;
		else
			event_radius = refresh_radius;
	}

	// Ignore enemies too far away to be noticed
	if(DistanceSquared(bs->now.origin, ent->r.currentOrigin) > Square(event_radius))
		return qfalse;

	// Just refresh awareness if the bot was already aware
	if(index >= 0)
		return tvl_update_entry(&bs->aware, index, timeout, EntityRating(ent));

	// The bot noticed this enemy right now, but cannot (yet) confirm sighting
	aware.ent = ent;
	aware.first_noted = bs->command_time;
	aware.sighted = -1;

	// Try to add the entity to the list
#ifdef DEBUG_AI
	if(bs->debug_flags & BOT_DEBUG_INFO_AWARENESS)
		index = tvl_add(&bs->aware, &aware, timeout, EntityRating(ent), BotPrintAwarenessTrack, BotPrintAwarenessLoss, bs);
	else
#endif
		index = tvl_add(&bs->aware, &aware, timeout, EntityRating(ent), 0, 0, NULL);

	// Succeed if the entity could be added
	return (index >= 0);
}

/*
====================
BotAwarenessLocation

Something triggered the the bot to be more aware for a while.

Origin is the in-game coordinates of the triggering event.
Returns true if the location was recorded and false if not.
====================
*/
qboolean BotAwarenessLocation(bot_state_t * bs, vec3_t origin)
{
	// Record the trigger location and expiration time
	VectorCopy(origin, bs->aware_location);
	bs->aware_location_time = bs->command_time + bot_aware_duration.value;

	return qtrue;
}
