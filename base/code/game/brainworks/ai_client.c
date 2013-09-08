// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_client.c
 *
 * Functions that the bot uses to get information about a client
 *****************************************************************************/

#include "ai_main.h"
#include "ai_vars.h"
#include "ai_client.h"

#include "ai_chat.h"
#include "ai_entity.h"
#include "ai_level.h"
#include "ai_pickup.h"
#include "ai_self.h"


// Last known logical frame duration in milliseconds
int             client_think_time_ms = 0;

// Number of bots in the game
int             bots_connected = 0;

// Number of players on each team
int             team_count[TEAM_NUM_TEAMS];

// Number of distinct competing teams
int             num_teams = 2;

// Areas of all players in the game, computed and cached each frame
int             player_area[MAX_CLIENTS];

// Last known minimum and maxium reaction times
float           last_reaction_min = 0.0;
float           last_reaction_max = 0.0;


/*
=====================
LevelBotThinkSchedule

Make sure all bots have their logical thought processing
evently distributed, to avoid a jerky, stilted feeling.
=====================
*/
void LevelBotThinkSchedule(void)
{
	int             i, bot, connected;
	bot_state_t    *bs;

	// Count how many bots are connected and in use
	connected = 0;
	for(i = 0, bs = bot_states[i]; i < MAX_CLIENTS; i++, bs = bot_states[i])
	{
		if(bs && bs->inuse)
			connected++;
	}

	// Try to evenly space when these bots next think
	bot = 0;
	for(i = 0, bs = bot_states[i]; i < MAX_CLIENTS; i++, bs = bot_states[i])
	{
		// Ignore unused bot states
		if(!bs || !bs->inuse)
			continue;

		// Evenly space each bot's starting think time offset
		// throughout the entire think time interval
		bs->logic_time_ms = (bot_thinktime.integer * bot++) / connected;
	}
}

/*
====================
LevelUpdateThinkTime

Check for any changes in the logical frame
thought duration variable and do updates
if necessary.
====================
*/
void LevelUpdateThinkTime(void)
{
	// Reasonably bound the bot logical think time
	if(bot_thinktime.integer < SERVER_FRAME_DURATION_MS)
		trap_Cvar_Set("bot_thinktime", va("%i", SERVER_FRAME_DURATION_MS));
	else if(bot_thinktime.integer > 200)
		trap_Cvar_Set("bot_thinktime", "200");

	// If the bot think time changed, reschedule the prefered think intervals
	if(bot_thinktime.integer != client_think_time_ms)
	{
		LevelBotThinkSchedule();
		client_think_time_ms = bot_thinktime.integer;
	}
}

/*
=================
LevelCountPlayers

Check if any players dis/connected or switched teams
=================
*/
void LevelCountPlayers(void)
{
	int             i;
	gentity_t      *ent;

	// Initialize all team catagories
	for(i = 0; i < TEAM_NUM_TEAMS; i++)
		team_count[i] = 0;

	// Count the total players in each team catagory
	for(i = 0; i < MAX_CLIENTS; i++)
	{
		// Check if this client is connected
		ent = &g_entities[i];
		if(!ent->inuse || !ent->client)
			continue;

		// Make sure they have a valid team
		if(ent->client->sess.sessionTeam < 0 || ent->client->sess.sessionTeam >= TEAM_NUM_TEAMS)
			continue;

		// Record another member in their team
		team_count[ent->client->sess.sessionTeam]++;
	}

	// Now count the number of different teams
	switch (gametype)
	{
			// In many game modes, everyone is on their own team
		default:
		case GT_FFA:
		case GT_TOURNAMENT:
		case GT_SINGLE_PLAYER:
			num_teams = team_count[TEAM_FREE];
			break;

			// All the teamplay modes (currently) support exactly two teams
		case GT_TEAM:
		case GT_CTF:
#ifdef MISSIONPACK
		case GT_OBELISK:
		case GT_HARVESTER:
		case GT_1FCTF:
#endif
			num_teams = 2;
			break;
	}

	// Sanity check teams count-- there are always at least two sides, even if
	// one side lacks players
	if(num_teams < 2)
		num_teams = 2;
}

/*
=============
LevelNumTeams

Returns the number of teams competing in the game
=============
*/
int LevelNumTeams(void)
{
	return num_teams;
}

/*
=======================
LevelCacheReactionTimes

Each bot's reaction time based on some value between
the variables bot_reaction_min and bot_reaction_max.
So when one of those variables changes, all bot reaction
times must get recomputed.
=======================
*/
void LevelCacheReactionTimes(void)
{
	int             bot_index;
	bot_state_t    *bs;

	// Check if no updates are required
	if((last_reaction_min == bot_reaction_min.value) && (last_reaction_max == bot_reaction_max.value))
	{
		return;
	}

	// Store the last known reaction times
	last_reaction_min = bot_reaction_min.value;
	last_reaction_max = bot_reaction_max.value;

	// Recompute each bot's reaction time
	for(bs = bot_states[bot_index = 0]; bot_index < MAX_CLIENTS; bs = bot_states[++bot_index])
	{
		// Ignore unused bot states
		if(!bs || !bs->inuse || !bs->ent || !bs->ent->inuse)
			continue;

		// Reload this bot's reaction time
		BotReactionLoad(bs);
	}
}

/*
==========
BotEnemies

The number of clients in the game who are the bot's enemy
==========
*/
int BotEnemies(bot_state_t * bs)
{
	switch (bs->ent->client->sess.sessionTeam)
	{
		case TEAM_FREE:
			return team_count[TEAM_FREE] - 1;
		case TEAM_RED:
			return team_count[TEAM_FREE] + team_count[TEAM_BLUE];
		case TEAM_BLUE:
			return team_count[TEAM_FREE] + team_count[TEAM_RED];

		default:
		case TEAM_SPECTATOR:
			return 0;
	}
}

/*
============
BotTeammates

The number of clients in the game who are on the
bot's team, not counting the bot itself.
============
*/
int BotTeammates(bot_state_t * bs)
{
	switch (bs->ent->client->sess.sessionTeam)
	{
		case TEAM_FREE:
			return 0;
		case TEAM_RED:
			return team_count[TEAM_RED] - 1;
		case TEAM_BLUE:
			return team_count[TEAM_BLUE] - 1;

		default:
		case TEAM_SPECTATOR:
			return 0;
	}
}

/*
=====================
LevelPlayerAreasReset
=====================
*/
void LevelPlayerAreasReset(void)
{
	memset(player_area, 0, sizeof(player_area));
}

/*
======================
LevelPlayerAreasUpdate

FIXME: Not all areas are navigatable with TFL_DEFAULT.
When the bot jumps off a ledge, it's well within the
realm of probability that it will pass through some
areas that it cannot navigate into or out of.  This
will result in the bot selecting no goal and no item
for that frame.  This isn't a big deal because the
bots (apparently) don't move in air anyway, and the
bot will quickly enter a ground area from which they
can navigate.  So technically the check that only
updates for non-zero areas could be expanded to exclude
these other non-navigatable areas.

In theory, the item pickup code (which predicts travel
times to a wide variety of areas) could check if all
tested routes out of an area were unnavigatable.  If
that were the case, the bot's current area could get
added to a list of areas to avoid.  (Actually, it would
be an array of boolean values where "true" means "don't
update the bot's area to this value", and is initialized
to all falses except 0 which is true.)

That said, this is an awful lot of trouble to patch an
apparent issue with the internal engine, and there
doesn't seem to be a real payoff for doing so.  So this
fix has not been implemented.  But if a large bug
occurs from the bot entering non-navigatable areas,
it's relatively easy (for the processor, not the code)
to prune out these areas in real-time.
======================
*/
void LevelPlayerAreasUpdate(void)
{
	int             i, area;
	gentity_t      *ent;

	// Update the areas of all connected players
	for(i = 0, ent = &g_entities[0]; i < MAX_CLIENTS; i++, ent++)
	{
		// Non-players always have area zero
		if(!ent->inuse ||
		   !ent->client || ent->client->pers.connected != CON_CONNECTED || ent->client->sess.sessionTeam == TEAM_SPECTATOR)
		{
			player_area[i] = 0;
			continue;
		}

		// Update the cached area if the player's area could be determined
		area = LevelAreaPoint(ent->client->ps.origin);
		if(area)
			player_area[i] = area;
	}
}

/*
==========
PlayerArea

Returns the cached area of an entity that is
guaranteed to be a player in the game.
==========
*/
int PlayerArea(gentity_t * ent)
{
	return player_area[ent - g_entities];
}


/*
==========
ClientSkin

NOTE: This function is not used
==========
*/
char           *ClientSkin(int client, char *skin, int size)
{
	char            buf[MAX_INFO_STRING];

	if(client < 0 || client >= MAX_CLIENTS)
	{
		BotAI_Print(PRT_ERROR, "ClientSkin: client out of range\n");
		return "[client out of range]";
	}
	trap_GetConfigstring(CS_PLAYERS + client, buf, sizeof(buf));
	strncpy(skin, Info_ValueForKey(buf, "model"), size - 1);
	skin[size - 1] = '\0';
	return skin;
}

/*
================
TeammateFromName
================
*/
gentity_t      *TeammateFromName(bot_state_t * bs, char *name)
{
	int             i;
	char            player_name[MAX_INFO_STRING];
	gentity_t      *ent;

	// Search all teammates for an entity with matching name
	for(i = 0, ent = &g_entities[0]; i < maxclients && i < MAX_CLIENTS; i++, ent++)
	{
		if(!ent->inuse)
			continue;

		if(!BotSameTeam(bs, ent))
			continue;

		Q_strncpyz(player_name, ent->client->pers.netname, sizeof(player_name));
		Q_CleanStr(player_name);

		if(!Q_stricmp(player_name, name))
			return ent;
	}

	// No matching teammate was found
	return NULL;
}

/*
=============
EnemyFromName
=============
*/
gentity_t      *EnemyFromName(bot_state_t * bs, char *name)
{
	int             i;
	char            player_name[MAX_INFO_STRING];
	gentity_t      *ent;

	// Search all enemies for an entity with matching name
	for(i = 0, ent = &g_entities[0]; i < maxclients && i < MAX_CLIENTS; i++, ent++)
	{
		if(!ent->inuse)
			continue;

		if(!BotEnemyTeam(bs, ent))
			continue;

		Q_strncpyz(player_name, ent->client->pers.netname, sizeof(player_name));
		Q_CleanStr(player_name);

		if(!Q_stricmp(player_name, name))
			return ent;
	}

	// No matching enemy was found
	return NULL;
}

/*
===========
BotSameTeam

NOTE: This is *NOT* the same as !BotEnemyTeam().  This function only returns
true when the requested client is on the same team.
===========
*/
qboolean BotSameTeam(bot_state_t * bs, gentity_t * ent)
{
	int             team;

	// You are always on your own team
	if(bs->ent == ent)
		return qtrue;

	// Check if the entities are on the same team
	return (bs->ent->client->sess.sessionTeam != TEAM_FREE && bs->ent->client->sess.sessionTeam == EntityTeam(ent));
}

/*
============
BotEnemyTeam

NOTE: This is *NOT* the same as !BotSameTeam().  This function has special
spectator checks (since spectators are not enemies, even if their teams differ).
============
*/
qboolean BotEnemyTeam(bot_state_t * bs, gentity_t * ent)
{
	int             entity_team;

	// You are never your enemy
	if(bs->ent == ent)
		return qfalse;

	// Spectators are never enemies
	entity_team = EntityTeam(ent);
	if(entity_team == TEAM_SPECTATOR)
		return qfalse;

	// Check if the teams differ, or the bot is on no one's team
	return (bs->ent->client->sess.sessionTeam == TEAM_FREE || bs->ent->client->sess.sessionTeam != entity_team);
}

/*
=============
BotChaseEnemy

Check if the bot would have to chase to catch this enemy
(ie. the enemy probably wants to escape the bot).
=============
*/
qboolean BotChaseEnemy(bot_state_t * bs, gentity_t * ent)
{
	// Assume the player will run if they have a larger kill value
	return (EntityKillValue(bs->ent) < EntityKillValue(ent));
}

/*
==========
BotIsAlone

Returns true if the bot is the only client connected
==========
*/
qboolean BotIsAlone(bot_state_t * bs)
{
	int             i;
	gentity_t      *ent;

	// Scan for other connected players
	for(i = 0; i < maxclients && i < MAX_CLIENTS; i++)
	{
		// Ignore the bot
		ent = &g_entities[i];
		if(bs->ent == ent)
			continue;

		// Ignore all spectators
		//
		// NOTE: Entities not in use also have team spectator-- obviously these
		// should be ignored as well.
		if(EntityTeam(ent) == TEAM_SPECTATOR)
			continue;

		// Another player is connected, so the bot is not alone
		return qfalse;
	}

	// The bot must be alone
	return qtrue;
}

/*
====================
BotIsFirstInRankings
====================
*/
qboolean BotIsFirstInRankings(bot_state_t * bs)
{
	int             i, score;
	gentity_t      *ent;

	// Scan for other connected players
	score = bs->ps->persistant[PERS_SCORE];
	for(i = 0; i < maxclients && i < MAX_CLIENTS; i++)
	{
		// Ignore non-players
		ent = &g_entities[i];
		if(!ent->client)
			continue;

		// Ignore all spectators
		//
		// NOTE: Entities not in use also have team spectator-- obviously these
		// should be ignored as well.
		if(EntityTeam(ent) == TEAM_SPECTATOR)
			continue;

		// If the bot has less points than this player, the bot is not first
		if(score < ent->client->ps.persistant[PERS_SCORE])
			return qfalse;
	}

	return qtrue;
}

/*
===================
BotIsLastInRankings
===================
*/
qboolean BotIsLastInRankings(bot_state_t * bs)
{
	int             i, score;
	gentity_t      *ent;

	// Scan for other connected players
	score = bs->ps->persistant[PERS_SCORE];
	for(i = 0; i < maxclients && i < MAX_CLIENTS; i++)
	{
		// Ignore non-players
		ent = &g_entities[i];
		if(!ent->client)
			continue;

		// Ignore all spectators
		//
		// NOTE: Entities not in use also have team spectator-- obviously these
		// should be ignored as well.
		if(EntityTeam(ent) == TEAM_SPECTATOR)
			continue;

		// If the bot has more points than this player, the bot is not last
		if(score > ent->client->ps.persistant[PERS_SCORE])
			return qfalse;
	}

	return qtrue;
}

/*
========================
BotFirstClientInRankings
========================
*/
char           *BotFirstClientInRankings(void)
{
	int             i, high_score;
	static char     name[MAX_NETNAME];
	gentity_t      *ent, *first;

	// Search for the highest score among all clients
	first = NULL;
	high_score = -999999;
	for(i = 0; i < maxclients && i < MAX_CLIENTS; i++)
	{
		// Ignore non-players
		ent = &g_entities[i];
		if(!ent->client)
			continue;

		// Ignore all spectators
		//
		// NOTE: Entities not in use also have team spectator-- obviously these
		// should be ignored as well.
		if(EntityTeam(ent) == TEAM_SPECTATOR)
			continue;

		// Check if this client has a higher score than any previous client
		if(!first || ent->client->ps.persistant[PERS_SCORE] > high_score)
		{
			first = ent;
			high_score = ent->client->ps.persistant[PERS_SCORE];
		}
	}

	return SimplifyName(EntityName(first, name, sizeof(name)));
}

/*
=======================
BotLastClientInRankings
=======================
*/
char           *BotLastClientInRankings(void)
{
	int             i, low_score;
	static char     name[MAX_NETNAME];
	gentity_t      *ent, *last;

	// Search for the lowest score among all clients
	last = NULL;
	low_score = 999999;
	for(i = 0; i < maxclients && i < MAX_CLIENTS; i++)
	{
		// Ignore non-players
		ent = &g_entities[i];
		if(!ent->client)
			continue;

		// Ignore all spectators
		//
		// NOTE: Entities not in use also have team spectator-- obviously these
		// should be ignored as well.
		if(EntityTeam(ent) == TEAM_SPECTATOR)
			continue;

		// Check if this client has a lower score than any previous client
		if(!last || ent->client->ps.persistant[PERS_SCORE] < low_score)
		{
			last = ent;
			low_score = ent->client->ps.persistant[PERS_SCORE];
		}
	}

	return SimplifyName(EntityName(last, name, sizeof(name)));
}

/*
=====================
BotRandomOpponentName
=====================
*/
char           *BotRandomOpponentName(bot_state_t * bs)
{
	int             i, choice, num_opponents;
	char            buf[MAX_INFO_STRING];
	gentity_t      *opponents[MAX_CLIENTS];
	static int      index = 0;
	static char     name_cache[2][MAX_NETNAME];
	char           *name;

	// Make a list of the bot's enemies
	num_opponents = 0;
	opponents[0] = bs->ent;		// Just in case...
	for(i = 0; i < maxclients && i < MAX_CLIENTS; i++)
	{
		// Only select enemies
		if(!BotEnemyTeam(bs, &g_entities[i]))
			continue;

		opponents[num_opponents++] = &g_entities[i];
	}

	// Select the appropriate pointer to use this frame
	name = name_cache[index & 0x1];
	index ^= 0x1;

	// Look up the opponent's name
	choice = (num_opponents ? rand() % num_opponents : 0);
	return SimplifyName(EntityName(opponents[choice], name, MAX_NETNAME));
}

/*
==================
BotReadSessionData

This function is used to preserve bot session data
between level restarts, the bot entering/leaving 1v1
tournament mode, and so on.
==================
*/
void BotReadSessionData(bot_state_t * bs)
{
// NOTE: See FIXME in BotWriteSessionData()

/*
	char	s[MAX_STRING_CHARS];
	const char	*var;

	var = va( "botsession%i", bs->client );
	trap_Cvar_VariableStringBuffer( var, s, sizeof(s) );

	sscanf(s,
			"%i %i %i %i %i %i %i %i"
			" %f %f %f"
			" %f %f %f"
			" %f %f %f",
		&bs->lastgoal_decisionmaker,
		&bs->lastgoal_ltgtype,
		&bs->lastgoal_teammate,
		&bs->last_ordered_goal.areanum,
		&bs->last_ordered_goal.entitynum,
		&bs->last_ordered_goal.flags,
		&bs->last_ordered_goal.iteminfo,
		&bs->last_ordered_goal.number,
		&bs->last_ordered_goal.origin[0],
		&bs->last_ordered_goal.origin[1],
		&bs->last_ordered_goal.origin[2],
		&bs->last_ordered_goal.mins[0],
		&bs->last_ordered_goal.mins[1],
		&bs->last_ordered_goal.mins[2],
		&bs->last_ordered_goal.maxs[0],
		&bs->last_ordered_goal.maxs[1],
		&bs->last_ordered_goal.maxs[2]
		);
*/
}

/*
===================
BotWriteSessionData

This function is used to preserve bot session data
between level restarts, the bot entering/leaving 1v1
tournament mode, and so on.
===================
*/
void BotWriteSessionData(bot_state_t * bs)
{
// FIXME: These old values aren't meaningful, but it's probably a good
// idea to cache the weapon aiming statistics.  Doing so would require
// some tricky work, because the statistics use more than 500 different
// entries, which might cause some internal overflows.  Also, processing
// it is difficult because one single call to sscanf() won't work--
// repeated calls with strtok() are needed.  The old code is left here as
// a reference in case some other enterprising programming would like
// them as a reference.

/*
	const char	*s;
	const char	*var;

	s = va(
			"%i %i %i %i %i %i %i %i"
			" %f %f %f"
			" %f %f %f"
			" %f %f %f",
		bs->lastgoal_decisionmaker,
		bs->lastgoal_ltgtype,
		bs->lastgoal_teammate,
		bs->last_ordered_goal.areanum,
		bs->last_ordered_goal.entitynum,
		bs->last_ordered_goal.flags,
		bs->last_ordered_goal.iteminfo,
		bs->last_ordered_goal.number,
		bs->last_ordered_goal.origin[0],
		bs->last_ordered_goal.origin[1],
		bs->last_ordered_goal.origin[2],
		bs->last_ordered_goal.mins[0],
		bs->last_ordered_goal.mins[1],
		bs->last_ordered_goal.mins[2],
		bs->last_ordered_goal.maxs[0],
		bs->last_ordered_goal.maxs[1],
		bs->last_ordered_goal.maxs[2]
		);

	var = va( "botsession%i", bs->client );

	trap_Cvar_Set( var, s );
*/
}

/*
================
BotAISetupClient
================
*/
int BotAISetupClient(int client, struct bot_settings_s *settings, qboolean restart)
{
	char            filename[MAX_CHARACTERISTIC_PATH], name[MAX_CHARACTERISTIC_PATH];
	bot_state_t    *bs;
	int             errnum;

	// Acquire a bot state for this client
	if(!bot_states[client])
		bot_states[client] = G_Alloc(sizeof(bot_state_t));
	bs = bot_states[client];

	// Sanity check the requested bot state
	if(bs && bs->inuse)
	{
		BotAI_Print(PRT_FATAL, "BotAISetupClient: client %d already setup\n", client);
		return qfalse;
	}

	// Make sure the Area Awareness System was initialized
	if(!trap_AAS_Initialized())
	{
		BotAI_Print(PRT_FATAL, "AAS not initialized\n");
		return qfalse;
	}

	// Reset the bot state just in case
	memset(bs, 0, sizeof(bot_state_t));

	// Load the character data
	bs->character = trap_BotLoadCharacter(settings->characterfile, settings->skill);
	if(!bs->character)
	{
		BotAI_Print(PRT_FATAL, "Couldn't load skill %f from %s\n", settings->skill, settings->characterfile);
		return qfalse;
	}

	// Allocate and set up a chat state
	bs->cs = trap_BotAllocChatState();
	trap_Characteristic_String(bs->character, CHARACTERISTIC_CHAT_FILE, filename, sizeof(filename));
	trap_Characteristic_String(bs->character, CHARACTERISTIC_CHAT_NAME, name, sizeof(name));
	errnum = trap_BotLoadChatFile(bs->cs, filename, name);
	if(errnum != BLERR_NOERROR)
	{
		trap_BotFreeChatState(bs->cs);
		return qfalse;
	}

	// Save the input settings in the bot state
	memcpy(&bs->settings, settings, sizeof(bot_settings_t));

	bs->inuse = qtrue;
	bs->client = client;
	bs->entitynum = client;
	bs->enter_game_time = server_time;
	bs->ms = trap_BotAllocMoveState();
	bs->ent = &g_entities[client];
	bs->ps = &bs->ent->client->ps;

	// FIXME: The code here could be extended to cache all relevant values from the
	// trap_Characteristic_BInteger() function call.  Currently the code repeatedly
	// calls this function instead of looking up the values in a local cache.  Each
	// function call makes access take six times longer than a direct access in the
	// bot state.  Of course, this saves about 150 nanoseconds per call on a 1ghz
	// processor.  A rough estimate says this will save .15 milliseconds per second
	// of processing time.  I don't think creating this data cache is worth the
	// trouble anymore, but it's something that should have been implemented in the
	// initial design.

	// Initialize internal bot data data, such as statistics and awareness
	BotInitialize(bs);

	// Reschedule the when all bots think
	LevelBotThinkSchedule();

	// Load old session data if the bot client was saved through a level reset
	if(restart)
		BotReadSessionData(bs);

	// Test chatting if requested
	if(trap_Cvar_VariableIntegerValue("bot_testichat"))
	{
		trap_BotLibVarSet("bot_testichat", "1");
		BotChatTest(bs);
	}

	// The bot was successfully setup
	BotAI_Print(PRT_MESSAGE, "Successfully loaded Brainworks Bot\n");
	return qtrue;
}

/*
===================
BotAIShutdownClient
===================
*/
int BotAIShutdownClient(int client, qboolean restart)
{
	bot_state_t    *bs;

	bs = bot_states[client];
	if(!bs || !bs->inuse)
		return qfalse;

	if(restart)
		BotWriteSessionData(bs);

	BotChatExitGame(bs);

	// Free the move state, chat state, and character file
	trap_BotFreeMoveState(bs->ms);
	trap_BotFreeChatState(bs->cs);
	trap_BotFreeCharacter(bs->character);

	// Reset this state just in case
	memset(bs, 0, sizeof(bot_state_t));
	bs->inuse = qfalse;

	// There's one fewer bot
	return qtrue;
}
