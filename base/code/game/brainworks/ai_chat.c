// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_chat.c
 *
 * Functions that the bot uses to chat with other players
 *****************************************************************************/

#include "ai_main.h"
#include "ai_vars.h"
#include "ai_chat.h"

#include "ai_client.h"
#include "ai_command.h"
#include "ai_entity.h"
#include "ai_level.h"
#include "ai_self.h"
#include "ai_weapon.h"


#define TIME_BETWEENCHATTING	25


/*
===========
BotChatTime
===========
*/
float BotChatTime(bot_state_t * bs)
{
	return 2.0 / (bs->settings.skill ? (float)bs->settings.skill : 1.0);
}

/*
===============
Bot_InitialChat
===============
*/
void QDECL Bot_InitialChat(bot_state_t * bs, char *type, ...)
{
	int             i, mcontext;
	va_list         ap;
	char           *p;
	char           *vars[MAX_MATCHVARIABLES];

	memset(vars, 0, sizeof(vars));
	va_start(ap, type);
	p = va_arg(ap, char *);

	for(i = 0; i < MAX_MATCHVARIABLES; i++)
	{
		if(!p)
			break;
		vars[i] = p;
		p = va_arg(ap, char *);
	}
	va_end(ap);

	mcontext = BotSynonymContext(bs);

	trap_BotInitialChat(bs->cs, type, mcontext, vars[0], vars[1], vars[2], vars[3], vars[4], vars[5], vars[6], vars[7]);
}

/*
=================
Bot_SetupChatInfo
=================
*/
void Bot_SetupChatInfo(bot_state_t * bs, float delay, int style, int client)
{
	// Remember the time of the last known chat the server will process
	bs->last_chat_time = bs->command_time;

	// Cache delayed messages for later ...
	if(delay > 0)
	{
		bs->chat_style = style;
		bs->chat_client = client;
		bs->chat_time = bs->command_time + delay;
	}

	// ... Send non-delayed messages immediately
	else
	{
		trap_BotEnterChat(bs->cs, client, style);
	}
}

/*
===================
BotRandomWeaponName
===================
*/
char           *BotRandomWeaponName(void)
{
	int             weapon;

	// Select a random weapon.  The -1 and +1 parts make sure 0 is never
	// selected, which is WP_NONE.
	weapon = (rand() % (WP_NUM_WEAPONS - 1)) + 1;

	if(weapon == WP_GRAPPLING_HOOK)
		weapon = WP_BFG;

	return WeaponName(weapon);
}

/*
===================
BotSafeChatPosition
===================
*/
qboolean BotSafeChatPosition(bot_state_t * bs)
{
	vec3_t          point, start, end, mins, maxs;
	trace_t         trace;

	// If the bot is dead all positions are valid
	if(BotIsDead(bs))
		return qtrue;

	// Never start chatting with a powerup
	if(bs->ps->powerups[PW_QUAD] ||
	   bs->ps->powerups[PW_HASTE] ||
	   bs->ps->powerups[PW_INVIS] || bs->ps->powerups[PW_REGEN] || bs->ps->powerups[PW_FLIGHT] || bs->ps->powerups[PW_BATTLESUIT])
		return qfalse;

	// Do not chat if under water
	if(bs->now.water_level >= 2)
		return qfalse;

	// Do not chat if in lava or slime
	VectorCopy(bs->now.origin, point);
	point[2] -= 24;
	if(trap_PointContents(point, bs->entitynum) & (CONTENTS_LAVA | CONTENTS_SLIME))
		return qfalse;

	// Must be standing on the world entity
	VectorCopy(bs->now.origin, start);
	VectorCopy(bs->now.origin, end);
	start[2] += 1;
	end[2] -= 10;
	trap_AAS_PresenceTypeBoundingBox(PRESENCE_CROUCH, mins, maxs);
	trap_Trace(&trace, start, mins, maxs, end, bs->client, MASK_SOLID);
	if(trace.entityNum != ENTITYNUM_WORLD)
		return qfalse;

	// The bot is in a position where it can chat
	return qtrue;
}

/*
================
BotWillingToChat

Returns true if it's reasonable for the bot to chat.
================
*/
qboolean BotWillingToChat(bot_state_t * bs)
{
	// Don't chat if the server turned it off
	if(bot_nochat.integer)
		return qfalse;

	// Don't chat too often
	if(bs->command_time < bs->last_chat_time + TIME_BETWEENCHATTING)
		return qfalse;

	// Don't chat again if already talking
	if(bs->chat_time)
		return qfalse;

	// Never chat in tournament mode
	if(gametype == GT_TOURNAMENT)
		return qfalse;

	// Don't chat if no one else is connected
	if(BotIsAlone(bs))
		return qfalse;

	return qtrue;
}

/*
================
BotChatEnterGame
================
*/
qboolean BotChatEnterGame(bot_state_t * bs)
{
	// Only do the enter game chat once
	if(bs->chat_enter_game)
		return qfalse;
	bs->chat_enter_game = qtrue;

	// Don't chat if it's been too long since the bot entered the game
	if(bs->command_time > bs->enter_game_time + 8.0)
		return qfalse;

	if((!bot_fastchat.integer) && (random() > trap_Characteristic_BFloat(bs->character, CHARACTERISTIC_CHAT_ENTEREXITGAME, 0, 1)))
	{
		return qfalse;
	}

	Bot_InitialChat(bs, "game_enter", SimplifyName(EntityNameFast(bs->ent)),	// 0
					BotRandomOpponentName(bs),	// 1
					"[invalid var]",	// 2
					"[invalid var]",	// 3
					LevelMapTitle(),	// 4
					NULL);

	Bot_SetupChatInfo(bs, 0, CHAT_ALL, bs->client);

	return qtrue;
}

/*
===============
BotChatExitGame
===============
*/
void BotChatExitGame(bot_state_t * bs)
{
	if(!BotWillingToChat(bs))
		return;

	// Don't chat in teamplay
	if(game_style & GS_TEAM)
		return;

	if(!bot_fastchat.integer)
	{
		if(random() > trap_Characteristic_BFloat(bs->character, CHARACTERISTIC_CHAT_ENTEREXITGAME, 0, 1))
			return;
	}

	Bot_InitialChat(bs, "game_exit", SimplifyName(EntityNameFast(bs->ent)),	// 0
					BotRandomOpponentName(bs),	// 1
					"[invalid var]",	// 2
					"[invalid var]",	// 3
					LevelMapTitle(),	// 4
					NULL);

	Bot_SetupChatInfo(bs, 0, CHAT_ALL, bs->client);
}

/*
======================
BotWantsStartLevelChat

Returns true if the bot decides to chat because of they are in a new level
======================
*/
qboolean BotWantsStartLevelChat(bot_state_t * bs)
{
	// Only do start of level chat when the bot just left the intermission state
	return (bs->ai_state == AIS_INTERMISSION);
}

/*
=================
BotChatStartLevel
=================
*/
qboolean BotChatStartLevel(bot_state_t * bs)
{
	if(!BotWantsStartLevelChat(bs))
		return qfalse;

	if(!bot_fastchat.integer)
	{
		if(random() > trap_Characteristic_BFloat(bs->character, CHARACTERISTIC_CHAT_STARTENDLEVEL, 0, 1))
			return qfalse;
	}

	Bot_InitialChat(bs, "level_start", SimplifyName(EntityNameFast(bs->ent)),	// 0
					NULL);
	Bot_SetupChatInfo(bs, BotChatTime(bs), CHAT_ALL, bs->client);

	return qtrue;
}

/*
===============
BotChatEndLevel
===============
*/
void BotChatEndLevel(bot_state_t * bs)
{
	if(!BotWillingToChat(bs))
		return;

	if(BotIsObserver(bs))
		return;

	// Teamplay
	if(game_style & GS_TEAM)
	{
		if(BotIsFirstInRankings(bs))
			trap_EA_Command(bs->client, "vtaunt");

		return;
	}
	if(!bot_fastchat.integer)
	{
		if(random() > trap_Characteristic_BFloat(bs->character, CHARACTERISTIC_CHAT_STARTENDLEVEL, 0, 1))
			return;
	}

	if(BotIsFirstInRankings(bs))
	{
		Bot_InitialChat(bs, "level_end_victory", SimplifyName(EntityNameFast(bs->ent)),	// 0
						BotRandomOpponentName(bs),	// 1
						"[invalid var]",	// 2
						BotLastClientInRankings(),	// 3
						LevelMapTitle(),	// 4
						NULL);
	}
	else if(BotIsLastInRankings(bs))
	{
		Bot_InitialChat(bs, "level_end_lose", SimplifyName(EntityNameFast(bs->ent)),	// 0
						BotRandomOpponentName(bs),	// 1
						BotFirstClientInRankings(),	// 2
						"[invalid var]",	// 3
						LevelMapTitle(),	// 4
						NULL);
	}
	else
	{
		Bot_InitialChat(bs, "level_end", SimplifyName(EntityNameFast(bs->ent)),	// 0
						BotRandomOpponentName(bs),	// 1
						BotFirstClientInRankings(),	// 2
						BotLastClientInRankings(),	// 3
						LevelMapTitle(),	// 4
						NULL);
	}

	Bot_SetupChatInfo(bs, 0, CHAT_ALL, 0);
}

/*
============
BotChatDeath
============
*/
void BotChatDeath(bot_state_t * bs)
{
	char           *name;

	// Assume the bot won't say something
	bs->chat_time = 0;

	if(!BotWillingToChat(bs))
		return;

	if(!bot_fastchat.integer)
	{
		if(random() > trap_Characteristic_BFloat(bs->character, CHARACTERISTIC_CHAT_DEATH, 0, 1))
			return;
	}

	if(bs->last_killed_by && bs->last_killed_by->client)
		name = SimplifyName(EntityNameFast(bs->last_killed_by));
	else
		name = "[world]";

	if(game_style & GS_TEAM)
	{
		if(BotSameTeam(bs, bs->last_killed_by))
		{
			if(bs->last_killed_by == bs->ent)
				return;
			Bot_InitialChat(bs, "death_teammate", name, NULL);
			Bot_SetupChatInfo(bs, .5, CHAT_TEAM, 0);
		}
		else
		{
			trap_EA_Command(bs->client, "vtaunt");
			return;
		}
	}
	else
	{
		if(bs->bot_death_type == MOD_WATER)
			Bot_InitialChat(bs, "death_drown", BotRandomOpponentName(bs), NULL);
		else if(bs->bot_death_type == MOD_SLIME)
			Bot_InitialChat(bs, "death_slime", BotRandomOpponentName(bs), NULL);
		else if(bs->bot_death_type == MOD_LAVA)
			Bot_InitialChat(bs, "death_lava", BotRandomOpponentName(bs), NULL);
		else if(bs->bot_death_type == MOD_FALLING)
			Bot_InitialChat(bs, "death_cratered", BotRandomOpponentName(bs), NULL);
		else if(bs->bot_suicide ||	//all other suicides by own weapon
				bs->bot_death_type == MOD_CRUSH ||
				bs->bot_death_type == MOD_SUICIDE ||
				bs->bot_death_type == MOD_TARGET_LASER ||
				bs->bot_death_type == MOD_TRIGGER_HURT || bs->bot_death_type == MOD_UNKNOWN)
			Bot_InitialChat(bs, "death_suicide", BotRandomOpponentName(bs), NULL);
		else if(bs->bot_death_type == MOD_TELEFRAG)
			Bot_InitialChat(bs, "death_telefrag", name, NULL);
#ifdef MISSIONPACK
		else if(bs->bot_death_type == MOD_KAMIKAZE && trap_BotNumInitialChats(bs->cs, "death_kamikaze"))
			Bot_InitialChat(bs, "death_kamikaze", name, NULL);
#endif
		else
		{
			if((bs->bot_death_type == MOD_GAUNTLET ||
				bs->bot_death_type == MOD_RAILGUN ||
				bs->bot_death_type == MOD_BFG || bs->bot_death_type == MOD_BFG_SPLASH) && random() < 0.5)
			{

				if(bs->bot_death_type == MOD_GAUNTLET)
					Bot_InitialChat(bs, "death_gauntlet", name,	// 0
									WeaponNameForMeansOfDeath(bs->bot_death_type),	// 1
									NULL);
				else if(bs->bot_death_type == MOD_RAILGUN)
					Bot_InitialChat(bs, "death_rail", name,	// 0
									WeaponNameForMeansOfDeath(bs->bot_death_type),	// 1
									NULL);
				else
					Bot_InitialChat(bs, "death_bfg", name,	// 0
									WeaponNameForMeansOfDeath(bs->bot_death_type),	// 1
									NULL);
			}
			//choose between insult and praise
			else if(random() < trap_Characteristic_BFloat(bs->character, CHARACTERISTIC_CHAT_INSULT, 0, 1))
			{
				Bot_InitialChat(bs, "death_insult", name,	// 0
								WeaponNameForMeansOfDeath(bs->bot_death_type),	// 1
								NULL);
			}
			else
			{
				Bot_InitialChat(bs, "death_praise", name,	// 0
								WeaponNameForMeansOfDeath(bs->bot_death_type),	// 1
								NULL);
			}
		}
		Bot_SetupChatInfo(bs, .5, CHAT_ALL, 0);
	}
}

/*
=================
BotChatHitTalking
=================
*/
void BotChatHitTalking(bot_state_t * bs)
{
	float           delay, rnd;

	// Only talk if willing and someone recently hurt the bot
	if(!BotWillingToChat(bs))
		return;
	if(!bs->last_hurt_client)
		return;

	// The bot might not want to chat all the time
	if((!bot_fastchat.integer) &&
	   (2 * random() > trap_Characteristic_BFloat(bs->character, CHARACTERISTIC_CHAT_HITTALKING, 0, 1)))
	{
		return;
	}

	// Don't waste time typing when the bot is in danger
	if(!BotSafeChatPosition(bs))
		return;

	// Change old message to a "Don't hit me when I'm talking" message
	Bot_InitialChat(bs, "hit_talking",
					SimplifyName(EntityNameFast(bs->last_hurt_client)),
					WeaponNameForMeansOfDeath(bs->ent->client->lasthurt_mod), NULL);

	// Chat new message almost immediately
	delay = bs->chat_time - bs->command_time;
	if(delay < 0)
		delay = 0;
	else if(delay > 0.1)
		delay = 0.1;
	Bot_SetupChatInfo(bs, delay, CHAT_ALL, 0);
}

/*
================
BotWantsKillChat

Returns true if the bot decides to chat because of a kill
================
*/
qboolean BotWantsKillChat(bot_state_t * bs)
{
	float           rnd;

	// Don't announce kills that happened more than a second ago
	if(bs->killed_player_time + 1.0 < bs->command_time)
		return qfalse;

	// The bot might not want to chat all the time
	if((!bot_fastchat.integer) && (random() > trap_Characteristic_BFloat(bs->character, CHARACTERISTIC_CHAT_KILL, 0, 1)))
	{
		return qfalse;
	}

	// Don't be a smart-ass when you accidently kill yourself
	if(bs->killed_player == bs->ent)
		return qfalse;

	// Bot wants to say or do something because of getting a kill
	return qtrue;
}

/*
===================
BotChatKillTeammate
===================
*/
qboolean BotChatKillTeammate(bot_state_t * bs)
{
	// Check if the bot wants to chat about the kill
	if(!BotWantsKillChat(bs))
		return qfalse;

	// Only say something if the teammate died
	if(!BotSameTeam(bs, bs->killed_player))
		return qfalse;

	// Apologize to teammates
	Bot_InitialChat(bs, "kill_teammate", SimplifyName(EntityNameFast(bs->killed_player)), NULL);
	Bot_SetupChatInfo(bs, BotChatTime(bs), CHAT_TEAM, 0);

	return qtrue;
}

/*
===========
BotChatKill
===========
*/
qboolean BotChatKill(bot_state_t * bs)
{
	char           *type;

	// Check if the bot wants to chat about the kill
	if(!BotWantsKillChat(bs))
		return qfalse;

	// Determine which kind of chat message to give
	switch (bs->killed_player_type)
	{
		case MOD_GAUNTLET:
			type = "kill_gauntlet";
			break;
		case MOD_RAILGUN:
			type = "kill_rail";
			break;
		case MOD_TELEFRAG:
			type = "kill_telefrag";
			break;

		default:
#ifdef MISSIONPACK
			if(bs->killed_player_type == MOD_KAMIKAZE && trap_BotNumInitialChats(bs->cs, "kill_kamikaze"))
			{
				type = "kill_kamikaze";
				break;
			}
#endif
			// Choose between insult and praise at random
			type = (random() < trap_Characteristic_BFloat(bs->character, CHARACTERISTIC_CHAT_INSULT, 0, 1)
					? "kill_insult" : "kill_praise");
			break;
	}


	// Send the appropriate message
	Bot_InitialChat(bs, type, SimplifyName(EntityNameFast(bs->killed_player)), NULL);
	Bot_SetupChatInfo(bs, BotChatTime(bs), CHAT_ALL, 0);

	return qtrue;
}

/*
===================
BotChatEnemySuicide
===================
*/
qboolean BotChatEnemySuicide(bot_state_t * bs)
{
	// Don't say anything if no player suicided in the past second
	if(!bs->suicide_enemy)
		return qfalse;
	if(bs->suicide_enemy_time + 1.0 < bs->command_time)
		return qfalse;

	// Don't always give messages at every opportunity
	if((!bot_fastchat.integer) && (random() > trap_Characteristic_BFloat(bs->character, CHARACTERISTIC_CHAT_KILL, 0, 1)))
	{
		return qfalse;
	}

	// Send the chat message
	Bot_InitialChat(bs, "enemy_suicide", SimplifyName(EntityNameFast(bs->suicide_enemy)), NULL);
	Bot_SetupChatInfo(bs, BotChatTime(bs), CHAT_ALL, 0);

	return qtrue;
}

/*
=================
BotChatHitNoDeath
=================
*/
qboolean BotChatHitNoDeath(bot_state_t * bs)
{
	// Only give this message if the bot was hurt last frame
	if(bs->damaged)
		return qfalse;

	// Don't chat if the bot doesn't know who hit them
	if(!bs->last_hurt_client)
		return qfalse;

	// Don't always give messages at every opportunity
	if((!bot_fastchat.integer) && (random() > trap_Characteristic_BFloat(bs->character, CHARACTERISTIC_CHAT_HITNODEATH, 0, 1)))
	{
		return qfalse;
	}

	// Send the chat message
	Bot_InitialChat(bs, "hit_nodeath",
					EntityNameFast(bs->last_hurt_client), WeaponNameForMeansOfDeath(bs->ent->client->lasthurt_mod), NULL);
	Bot_SetupChatInfo(bs, BotChatTime(bs), CHAT_ALL, 0);

	return qtrue;
}

/*
==================
BotWantsRandomChat

Returns true if the bot decides to randomly chat something
==================
*/
qboolean BotWantsRandomChat(bot_state_t * bs)
{
	float           chat_rate;

	// Don't randomly chat when doing something important
	if(EntityIsCarrier(bs->ent) || bs->help_teammate || bs->accompany_teammate)
		return qfalse;

	// At least be reasonable about how often the bot chatters, even when fast chat is on
	if(random() > 0.01)
		return qfalse;

	// Always send random chat if chat test mode is on
	if(bot_fastchat.integer)
		return qtrue;

	// Determine how often the bot wants to really wants to randomly chat
	chat_rate = 0.25 * trap_Characteristic_BFloat(bs->character, CHARACTERISTIC_CHAT_RANDOM, 0, 1);

	// Chat that portion of the time
	return (random() < chat_rate);
}

/*
=============
BotChatRandom
=============
*/
qboolean BotChatRandom(bot_state_t * bs)
{
	char           *style, *name;

	if(!BotWantsRandomChat(bs))
		return qfalse;

	// Determine chat style
	if(random() < trap_Characteristic_BFloat(bs->character, CHARACTERISTIC_CHAT_MISC, 0, 1))
		style = "random_misc";
	else
		style = "random_insult";

	// Don't accidently insult a teammate (including yourself)
	if(!BotSameTeam(bs, bs->killed_player))
		name = BotRandomOpponentName(bs);
	else
		name = SimplifyName(EntityNameFast(bs->killed_player));

	// Fill out the random chat message
	//
	// NOTE: that this chat happens immediately-- we can't have bots stopping at
	// random times in the game just to prove their idiocy.  At the very
	// least, they should have the decency to continue playing the game.
	Bot_InitialChat(bs, style, BotRandomOpponentName(bs),	// 0
					name,		// 1
					"[invalid var]",	// 2
					"[invalid var]",	// 3
					LevelMapTitle(),	// 4
					BotRandomWeaponName(),	// 5
					NULL);
	Bot_SetupChatInfo(bs, BotChatTime(bs), CHAT_ALL, 0);

	return qtrue;
}

/*
=================
BotWantsReplyChat

Bot might eliza reply to a chat message.  Returns true if
the bot should give a response.
=================
*/
qboolean BotWantsReplyChat(bot_state_t * bs)
{
	float           chat_reply;

	if(!BotWillingToChat(bs))
		return qfalse;

	if(!BotSafeChatPosition(bs))
		return qfalse;

	if(game_style & GS_TEAM)
		return qfalse;

	if(random() < .75)
		return qfalse;

	chat_reply = trap_Characteristic_BFloat(bs->character, CHARACTERISTIC_CHAT_REPLY, 0, 1);
	if(random() > chat_reply)
		return qfalse;

	return qtrue;
}

/*
=============
BotChatIngame

Test if the bot should talk about random in-game stuff that
happened (killed someone, got killed, etc.)  Returns true if
the bot decided to chat about something.
=============
*/
void BotChatIngame(bot_state_t * bs)
{
	// Potentially gesture if the bot recently killed someone
	if(bs->command_time < bs->killed_player_time + 2.0)
	{
		if(random() < 0.05)
			BotCommandAction(bs, ACTION_GESTURE);
	}

	// Make sure the bot wants to chat
	if(!BotWillingToChat(bs))
		return;

	// When not in teamplay mode, check for a few taunts
	if(!(game_style & GS_TEAM))
	{
		// Getting a kill, starting a level can cause a taunt.  Also just at random.
		if(BotWantsKillChat(bs) || BotWantsStartLevelChat(bs) || BotWantsRandomChat(bs))
			trap_EA_Command(bs->client, "vtaunt");
	}

	// Don't chat when in combat
	if(bs->aim_enemy || bs->goal_enemy)
		return;

	// Only chat from safe level locations
	if(!BotSafeChatPosition(bs))
		return;

	// In teamplay, only state important stuff (like "sorry for shooting you with friendly fire on")
	if(game_style & GS_TEAM)
	{
		if(BotChatKillTeammate(bs))
			return;

		return;
	}

	// Initial "Hello" from joining the game
	if(BotChatEnterGame(bs))
		return;

	// Chatter when the bot starts a new level
	if(BotChatStartLevel(bs))
		return;

	// Check for kills
	if(BotChatKill(bs))
		return;

	// Check for enemy suicides
	if(BotChatEnemySuicide(bs))
		return;

	// Check if the bot got hit (but didn't die)
	if(BotChatHitNoDeath(bs))
		return;

	// Check if the bot wants to chat about something random
	if(BotChatRandom(bs))
		return;
}

/*
=======================
BotCheckConsoleMessages
=======================
*/
void BotCheckConsoleMessages(bot_state_t * bs)
{
	char            botname[MAX_NETNAME], message[MAX_MESSAGE_SIZE], netname[MAX_NETNAME], *ptr;
	float           chat_reply;
	int             context, handle;
	bot_consolemessage_t m;
	bot_match_t     match;
	qboolean        chat_message;

	// Look up the bot's name
	EntityName(bs->ent, botname, sizeof(botname));

	// Loop over all pending messages, removing them as they are processed
	for(handle = trap_BotNextConsoleMessage(bs->cs, &m);
		handle; trap_BotRemoveConsoleMessage(bs->cs, handle), handle = trap_BotNextConsoleMessage(bs->cs, &m))
	{
		// If the chat state isn't flooded, the bot will read them slowly
		if(trap_BotNumConsoleMessages(bs->cs) < 10)
		{
			// If it is a chat message, the bot spends some time to read it
			if((m.type == CMS_CHAT) && (m.time > bs->command_time - (1.0 + random())))
				break;
		}

		// Check if this is a chat message that demands a reply
		chat_message = (m.type == CMS_CHAT && trap_BotFindMatch(m.message, &match, MTCONTEXT_REPLYCHAT));

		// Neither unify white spaces nor replace synonyms in the sender name
		// if this is a chat messages
		ptr = m.message;
		if(chat_message)
			ptr += match.variables[MESSAGE].offset;

		// Cleanup the white spaces and replace synonyms in the message
		trap_UnifyWhiteSpaces(ptr);
		context = BotSynonymContext(bs);
		trap_BotReplaceSynonyms(ptr, context);

		// If this message matches something the bot was looking for, process it and continue
		if(BotMatchMessage(bs, m.message))
			continue;

		// Never do Eliza-style responses when chatting is turned off
		if(bot_nochat.integer)
			continue;

		// Ignore the message if it can't be matched as a reply chat message
		if(!chat_message)
			continue;

		// Never ese eliza chat responses with team messages
		if(match.subtype & ST_TEAM)
			continue;

		// Ignore messages from the bot itself
		trap_BotMatchVariable(&match, NETNAME, netname, sizeof(netname));
		if(!Q_stricmp(netname, botname))
			continue;

		// Extract the message
		trap_BotMatchVariable(&match, MESSAGE, message, sizeof(message));
		trap_UnifyWhiteSpaces(message);

		// Look for possible eliza chat replies
#ifdef DEBUG_AI
		trap_Cvar_Update(&bot_testrchat);
		if(bot_testrchat.integer)
		{
			trap_BotLibVarSet("bot_testrchat", "1");
			if(trap_BotReplyChat(bs->cs, message, context, CONTEXT_REPLY, NULL, NULL, NULL, NULL, NULL, NULL, botname, netname))
			{
				BotAI_Print(PRT_MESSAGE, "------------------------\n");
			}
			else
			{
				BotAI_Print(PRT_MESSAGE, "**** no valid reply ****\n");
			}
		}
		else
#endif
		if(BotWantsReplyChat(bs))
		{
			if(trap_BotReplyChat(bs->cs, message, context, CONTEXT_REPLY, NULL, NULL, NULL, NULL, NULL, NULL, botname, netname))
			{
				Bot_SetupChatInfo(bs, BotChatTime(bs), CHAT_ALL, 0);
				//EA_Say(bs->client, bs->cs.chatmessage);

				// Remove the console message and stop processing messages
				trap_BotRemoveConsoleMessage(bs->cs, handle);
				break;
			}
		}
	}
}

/*
===========
BotChatTest
===========
*/
void BotChatTest(bot_state_t * bs)
{
	char           *name, *weap;
	char            enemy_name[MAX_NETNAME];
	int             num, i;

	name = SimplifyName(EntityNameFast(bs->ent));

	num = trap_BotNumInitialChats(bs->cs, "game_enter");
	for(i = 0; i < num; i++)
	{
		Bot_InitialChat(bs, "game_enter", name,	// 0
						BotRandomOpponentName(bs),	// 1
						"[invalid var]",	// 2
						"[invalid var]",	// 3
						LevelMapTitle(),	// 4
						NULL);
		trap_BotEnterChat(bs->cs, 0, CHAT_ALL);
	}
	num = trap_BotNumInitialChats(bs->cs, "game_exit");
	for(i = 0; i < num; i++)
	{
		Bot_InitialChat(bs, "game_exit", name,	// 0
						BotRandomOpponentName(bs),	// 1
						"[invalid var]",	// 2
						"[invalid var]",	// 3
						LevelMapTitle(),	// 4
						NULL);
		trap_BotEnterChat(bs->cs, 0, CHAT_ALL);
	}
	num = trap_BotNumInitialChats(bs->cs, "level_start");
	for(i = 0; i < num; i++)
	{
		Bot_InitialChat(bs, "level_start", name,	// 0
						NULL);
		trap_BotEnterChat(bs->cs, 0, CHAT_ALL);
	}
	num = trap_BotNumInitialChats(bs->cs, "level_end_victory");
	for(i = 0; i < num; i++)
	{
		Bot_InitialChat(bs, "level_end_victory", name,	// 0
						BotRandomOpponentName(bs),	// 1
						BotFirstClientInRankings(),	// 2
						BotLastClientInRankings(),	// 3
						LevelMapTitle(),	// 4
						NULL);
		trap_BotEnterChat(bs->cs, 0, CHAT_ALL);
	}
	num = trap_BotNumInitialChats(bs->cs, "level_end_lose");
	for(i = 0; i < num; i++)
	{
		Bot_InitialChat(bs, "level_end_lose", name,	// 0
						BotRandomOpponentName(bs),	// 1
						BotFirstClientInRankings(),	// 2
						BotLastClientInRankings(),	// 3
						LevelMapTitle(),	// 4
						NULL);
		trap_BotEnterChat(bs->cs, 0, CHAT_ALL);
	}
	num = trap_BotNumInitialChats(bs->cs, "level_end");
	for(i = 0; i < num; i++)
	{
		Bot_InitialChat(bs, "level_end", name,	// 0
						BotRandomOpponentName(bs),	// 1
						BotFirstClientInRankings(),	// 2
						BotLastClientInRankings(),	// 3
						LevelMapTitle(),	// 4
						NULL);
		trap_BotEnterChat(bs->cs, 0, CHAT_ALL);
	}

	name = SimplifyName(EntityNameFast(bs->last_killed_by));

	num = trap_BotNumInitialChats(bs->cs, "death_drown");
	for(i = 0; i < num; i++)
	{
		//
		Bot_InitialChat(bs, "death_drown", name, NULL);
		trap_BotEnterChat(bs->cs, 0, CHAT_ALL);
	}

	num = trap_BotNumInitialChats(bs->cs, "death_slime");
	for(i = 0; i < num; i++)
	{
		Bot_InitialChat(bs, "death_slime", name, NULL);
		trap_BotEnterChat(bs->cs, 0, CHAT_ALL);
	}

	num = trap_BotNumInitialChats(bs->cs, "death_lava");
	for(i = 0; i < num; i++)
	{
		Bot_InitialChat(bs, "death_lava", name, NULL);
		trap_BotEnterChat(bs->cs, 0, CHAT_ALL);
	}

	num = trap_BotNumInitialChats(bs->cs, "death_cratered");
	for(i = 0; i < num; i++)
	{
		Bot_InitialChat(bs, "death_cratered", name, NULL);
		trap_BotEnterChat(bs->cs, 0, CHAT_ALL);
	}

	num = trap_BotNumInitialChats(bs->cs, "death_suicide");
	for(i = 0; i < num; i++)
	{
		Bot_InitialChat(bs, "death_suicide", name, NULL);
		trap_BotEnterChat(bs->cs, 0, CHAT_ALL);
	}

	num = trap_BotNumInitialChats(bs->cs, "death_telefrag");
	for(i = 0; i < num; i++)
	{
		Bot_InitialChat(bs, "death_telefrag", name, NULL);
		trap_BotEnterChat(bs->cs, 0, CHAT_ALL);
	}

	num = trap_BotNumInitialChats(bs->cs, "death_gauntlet");
	for(i = 0; i < num; i++)
	{
		Bot_InitialChat(bs, "death_gauntlet", name,	// 0
						WeaponNameForMeansOfDeath(bs->bot_death_type),	// 1
						NULL);
		trap_BotEnterChat(bs->cs, 0, CHAT_ALL);
	}

	num = trap_BotNumInitialChats(bs->cs, "death_rail");
	for(i = 0; i < num; i++)
	{
		Bot_InitialChat(bs, "death_rail", name,	// 0
						WeaponNameForMeansOfDeath(bs->bot_death_type),	// 1
						NULL);
		trap_BotEnterChat(bs->cs, 0, CHAT_ALL);
	}

	num = trap_BotNumInitialChats(bs->cs, "death_bfg");
	for(i = 0; i < num; i++)
	{
		Bot_InitialChat(bs, "death_bfg", name,	// 0
						WeaponNameForMeansOfDeath(bs->bot_death_type),	// 1
						NULL);
		trap_BotEnterChat(bs->cs, 0, CHAT_ALL);
	}

	num = trap_BotNumInitialChats(bs->cs, "death_insult");
	for(i = 0; i < num; i++)
	{
		Bot_InitialChat(bs, "death_insult", name,	// 0
						WeaponNameForMeansOfDeath(bs->bot_death_type),	// 1
						NULL);
		trap_BotEnterChat(bs->cs, 0, CHAT_ALL);
	}

	num = trap_BotNumInitialChats(bs->cs, "death_praise");
	for(i = 0; i < num; i++)
	{
		Bot_InitialChat(bs, "death_praise", name,	// 0
						WeaponNameForMeansOfDeath(bs->bot_death_type),	// 1
						NULL);
		trap_BotEnterChat(bs->cs, 0, CHAT_ALL);
	}

	name = SimplifyName(EntityNameFast(bs->killed_player));

	num = trap_BotNumInitialChats(bs->cs, "kill_gauntlet");
	for(i = 0; i < num; i++)
	{
		//
		Bot_InitialChat(bs, "kill_gauntlet", name, NULL);
		trap_BotEnterChat(bs->cs, 0, CHAT_ALL);
	}

	num = trap_BotNumInitialChats(bs->cs, "kill_rail");
	for(i = 0; i < num; i++)
	{
		Bot_InitialChat(bs, "kill_rail", name, NULL);
		trap_BotEnterChat(bs->cs, 0, CHAT_ALL);
	}

	num = trap_BotNumInitialChats(bs->cs, "kill_telefrag");
	for(i = 0; i < num; i++)
	{
		Bot_InitialChat(bs, "kill_telefrag", name, NULL);
		trap_BotEnterChat(bs->cs, 0, CHAT_ALL);
	}

	num = trap_BotNumInitialChats(bs->cs, "kill_insult");
	for(i = 0; i < num; i++)
	{
		Bot_InitialChat(bs, "kill_insult", name, NULL);
		trap_BotEnterChat(bs->cs, 0, CHAT_ALL);
	}

	num = trap_BotNumInitialChats(bs->cs, "kill_praise");
	for(i = 0; i < num; i++)
	{
		Bot_InitialChat(bs, "kill_praise", name, NULL);
		trap_BotEnterChat(bs->cs, 0, CHAT_ALL);
	}

	num = trap_BotNumInitialChats(bs->cs, "enemy_suicide");
	for(i = 0; i < num; i++)
	{
		Bot_InitialChat(bs, "enemy_suicide", name, NULL);
		trap_BotEnterChat(bs->cs, 0, CHAT_ALL);
	}

	name = SimplifyName(EntityNameFast(&g_entities[bs->ent->client->lasthurt_client]));
	weap = WeaponNameForMeansOfDeath(bs->ent->client->lasthurt_mod);

	num = trap_BotNumInitialChats(bs->cs, "hit_talking");
	for(i = 0; i < num; i++)
	{
		Bot_InitialChat(bs, "hit_talking", name, weap, NULL);
		trap_BotEnterChat(bs->cs, 0, CHAT_ALL);
	}

	num = trap_BotNumInitialChats(bs->cs, "hit_nodeath");
	for(i = 0; i < num; i++)
	{
		Bot_InitialChat(bs, "hit_nodeath", name, weap, NULL);
		trap_BotEnterChat(bs->cs, 0, CHAT_ALL);
	}

	num = trap_BotNumInitialChats(bs->cs, "hit_nokill");
	for(i = 0; i < num; i++)
	{
		Bot_InitialChat(bs, "hit_nokill", name, weap, NULL);
		trap_BotEnterChat(bs->cs, 0, CHAT_ALL);
	}

	if(bs->killed_player == bs->ent)
		Q_strncpyz(enemy_name, BotRandomOpponentName(bs), sizeof(enemy_name));
	else
		SimplifyName(EntityName(bs->killed_player, enemy_name, sizeof(enemy_name)));

	num = trap_BotNumInitialChats(bs->cs, "random_misc");
	for(i = 0; i < num; i++)
	{
		Bot_InitialChat(bs, "random_misc", BotRandomOpponentName(bs),	// 0
						enemy_name,	// 1
						"[invalid var]",	// 2
						"[invalid var]",	// 3
						LevelMapTitle(),	// 4
						BotRandomWeaponName(),	// 5
						NULL);
		trap_BotEnterChat(bs->cs, 0, CHAT_ALL);
	}

	num = trap_BotNumInitialChats(bs->cs, "random_insult");
	for(i = 0; i < num; i++)
	{
		Bot_InitialChat(bs, "random_insult", BotRandomOpponentName(bs),	// 0
						enemy_name,	// 1
						"[invalid var]",	// 2
						"[invalid var]",	// 3
						LevelMapTitle(),	// 4
						BotRandomWeaponName(),	// 5
						NULL);
		trap_BotEnterChat(bs->cs, 0, CHAT_ALL);
	}
}
