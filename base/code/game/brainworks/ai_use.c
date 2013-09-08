// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_use.c
 *
 * Functions that help the bot use special abilities (such as holdable items)
 *****************************************************************************/

#include "ai_main.h"
#include "ai_vars.h"
#include "ai_use.h"

#include "ai_client.h"
#include "ai_command.h"
#include "ai_entity.h"
#include "ai_self.h"


#ifdef MISSIONPACK
/*
==============
BotUseKamikaze
==============
*/
#define KAMIKAZE_DIST		1024

void BotUseKamikaze(bot_state_t * bs)
{
	trace_t         trace;

	// Obviously this only applies to bots with the kamikaze
	if(bs->ps->stats[STAT_HOLDABLE_ITEM] != MODELINDEX_KAMIKAZE)
		return;

	// Carriers never use the kamikaze
	if(BotIsCarrier(bs))
		return;

	// Don't use it if you don't have a target
	if(!bs->aim_enemy)
		return;

	// Never use kamikaze if the team flag carrier is visible
	if(bs->team_carrier && (DistanceSquared(bs->team_carrier->r.currentOrigin, bs->now.origin) < Square(KAMIKAZE_DIST)))
	{
		return;
	}

	if(bs->enemy_carrier && (DistanceSquared(bs->enemy_carrier->r.currentOrigin, bs->now.origin) < Square(KAMIKAZE_DIST)))
	{
		BotCommandAction(bs, ACTION_USE);
		return;
	}

	// Use the kamikaze if the bot is aiming at the obelisk and close
	if((gametype == GT_OBELISK) &&
	   (!bs->aim_enemy->client) && (DistanceSquared(bs->aim_enemy->r.currentOrigin, bs->now.origin) < Square(200)))
	{
		BotCommandAction(bs, ACTION_USE);
		return;
	}

	if(bs->nearby_enemies > 2 && bs->nearby_enemies > bs->nearby_teammates + 1)
	{
		BotCommandAction(bs, ACTION_USE);
		return;
	}
}

/*
=====================
BotUseInvulnerability
=====================
*/
void BotUseInvulnerability(bot_state_t * bs)
{
	vec3_t          target;
	trace_t         trace;

	// Never use the invulnerability in some situations
	if(bs->ps->stats[STAT_HOLDABLE_ITEM] != MODELINDEX_INVULNERABILITY)
		return;
	if(BotIsCarrier(bs))
		return;

	// Never use invulnerability if an enemy carrier is visible
	if((game_style & GS_CARRIER) && bs->enemy_carrier)
		return;

	// Don't use it if you don't have a target
	if(!bs->aim_enemy)
		return;

	// Use the invulnerability if the bot is aiming at the obelisk and close
	if((gametype == GT_OBELISK) &&
	   (!bs->aim_enemy->client) && (DistanceSquared(bs->aim_enemy->r.currentOrigin, bs->now.origin) < Square(200)))
	{
		BotCommandAction(bs, ACTION_USE);
	}

	// Also use it if the bot is pretty hurt
	else if(EntityHealth(bs->ent) < 50)
		BotCommandAction(bs, ACTION_USE);
}
#endif

/*
======
BotUse
======
*/
void BotUse(bot_state_t * bs)
{
	// Don't use any special abilities if the bot isn't in danger
	if(!bs->aim_enemy && !bs->goal_enemy)
		return;

	if(bs->ps->stats[STAT_HEALTH] < 40 && bs->ps->stats[STAT_HOLDABLE_ITEM] == MODELINDEX_TELEPORTER && !BotIsCarrier(bs))
	{
		BotCommandAction(bs, ACTION_USE);
	}
	else if(bs->ps->stats[STAT_HEALTH] < 60 && bs->ps->stats[STAT_HOLDABLE_ITEM] == MODELINDEX_MEDKIT)
	{
		BotCommandAction(bs, ACTION_USE);
	}

#ifdef MISSIONPACK
	BotUseKamikaze(bs);
	BotUseInvulnerability(bs);
#endif
}
