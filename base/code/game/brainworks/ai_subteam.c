// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_subteam.c
 *
 * Functions that the bot uses to manage subteams
 *****************************************************************************/

#include "ai_main.h"
#include "ai_vars.h"
#include "ai_subteam.h"

#include "ai_chat.h"


/*
====================
BotMatch_JoinSubteam
====================
*/
void BotMatch_JoinSubteam(bot_state_t * bs, bot_match_t * match, gentity_t * sender)
{
	// Set the bot's subteam name
	trap_BotMatchVariable(match, TEAMNAME, bs->subteam, sizeof(bs->subteam));

	// Make sure the string is properly terminated
	bs->subteam[sizeof(bs->subteam) - 1] = '\0';

	// Inform the sender that the bot has joined this subteam
	Bot_InitialChat(bs, "joinedteam", bs->subteam, NULL);
	trap_BotEnterChat(bs->cs, sender->s.number, CHAT_TELL);
}

/*
=====================
BotMatch_LeaveSubteam
=====================
*/
void BotMatch_LeaveSubteam(bot_state_t * bs, bot_match_t * match, gentity_t * sender)
{
	// Do nothing if the bot isn't on any subteam
	if(!strlen(bs->subteam))
		return;

	// Inform the sender that the bot has left this subteam
	Bot_InitialChat(bs, "leftteam", bs->subteam, NULL);
	trap_BotEnterChat(bs->cs, sender->s.number, CHAT_TELL);

	// Reset the subteam name
	strcpy(bs->subteam, "");
}

/*
==================
BotMatch_WhichTeam
==================
*/
void BotMatch_WhichTeam(bot_state_t * bs, bot_match_t * match)
{
	// State which team the bot is in, if any
	if(strlen(bs->subteam))
		Bot_InitialChat(bs, "inteam", bs->subteam, NULL);
	else
		Bot_InitialChat(bs, "noteam", NULL);
	trap_BotEnterChat(bs->cs, bs->client, CHAT_TEAM);
}

/*
=======================
BotMatch_FormationSpace
=======================
*/
void BotMatch_FormationSpace(bot_state_t * bs, bot_match_t * match)
{
	char            number[MAX_MESSAGE_SIZE];

	// Determine the spacing distance in meters
	trap_BotMatchVariable(match, NUMBER, number, MAX_MESSAGE_SIZE);
	bs->formation_dist = 32 * atof(number);

	// Scale the distance by the appropriate units if specified
	if(match->subtype & ST_FEET)
		bs->formation_dist *= 0.3048;

	// Reasonably bound the formation distance
	if(bs->formation_dist < 48 || bs->formation_dist > 500)
		bs->formation_dist = 32 * 3.5;
}

/*
================
BotMatch_Subteam
================
*/
qboolean BotMatch_Subteam(bot_state_t * bs, bot_match_t * match, gentity_t * sender)
{
	// Check information and simple request messages
	switch (match->type)
	{
		case MSG_JOINSUBTEAM:
			BotMatch_JoinSubteam(bs, match, sender);
			break;
		case MSG_LEAVESUBTEAM:
			BotMatch_LeaveSubteam(bs, match, sender);
			break;
		case MSG_WHICHTEAM:
			BotMatch_WhichTeam(bs, match);
			break;
		case MSG_FORMATIONSPACE:
			BotMatch_FormationSpace(bs, match);
			break;

		case MSG_DOFORMATION:
		case MSG_WAIT:
			break;

		case MSG_CREATENEWFORMATION:
		case MSG_FORMATIONPOSITION:
			trap_EA_SayTeam(bs->client, "The part of my brain to create formations has been damaged");
			break;

		default:
			return qfalse;
	}

	return qtrue;
}
