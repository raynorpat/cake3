// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_order.c
 *
 * Functions that the bot uses to process orders for new goals
 *****************************************************************************/

#include "ai_main.h"
#include "ai_vars.h"
#include "ai_order.h"

#include "ai_chat.h"
#include "ai_client.h"
#include "ai_command.h"
#include "ai_entity.h"
#include "ai_goal.h"
#include "ai_region.h"
#include "ai_level.h"
#include "ai_self.h"
#include "ai_team.h"
#include "ai_waypoint.h"


// For the voice chats
#include "../../ui/menudef.h"


// Default times that a given order lasts for, in seconds
#define ORDER_TIME_HELP				60
#define ORDER_TIME_ACCOMPANY		600
#define ORDER_TIME_DEFEND			600
#define ORDER_TIME_CAMP				600
#define ORDER_TIME_PATROL			600
#define ORDER_TIME_LEAD				600
#define ORDER_TIME_ITEM				60
#define	ORDER_TIME_ATTACK			180
#define ORDER_TIME_ASSAULT			600
#define ORDER_TIME_HARVEST			120
#define ORDER_TIME_GETFLAG			600
#define ORDER_TIME_RETURNFLAG		180


/*
======================
BotOrderShouldAnnounce
======================
*/
qboolean BotOrderShouldAnnounce(bot_state_t * bs)
{
	return (bs->order_message_time && bs->order_message_time < bs->command_time);
}

/*
=====================
BotOrderAnnounceStart
=====================
*/
void BotOrderAnnounceStart(bot_state_t * bs, char *msg_type, gentity_t * recipient, char *arg, char *voicechat)
{
	// If no argument is specified, use the receipient's name
	// NOTE: A NULL could also be supplied if the chat message has no
	// arguments, but in that case passing in the unused name won't hurt.
	if(!arg && recipient)
		arg = SimplifyName(EntityNameFast(recipient));

	// Create some kind of "yes, I will do that" message
	Bot_InitialChat(bs, msg_type, arg, NULL);

	// Some messages go to one person and others to the team at large
	if(recipient)
	{
		trap_BotEnterChat(bs->cs, recipient->s.number, CHAT_TELL);
		BotVoiceChatOnly(bs, recipient->s.number, voicechat);
		BotCommandAction(bs, ACTION_AFFIRMATIVE);
	}
	else
	{
		trap_BotEnterChat(bs->cs, 0, CHAT_TEAM);
		BotVoiceChatOnly(bs, -1, voicechat);
	}

	// Remember that this announcement was made
	bs->order_message_time = 0;
}

/*
=============
BotOrderReset
=============
*/
void BotOrderReset(bot_state_t * bs)
{
	// Different orders have different cleanup cases
	switch (bs->order_type)
	{
			// Reset associated entity values
		case ORDER_HELP:
			bs->help_teammate = NULL;
			break;
		case ORDER_ACCOMPANY:
			bs->accompany_teammate = NULL;
			break;
		case ORDER_ATTACK:
			bs->order_enemy = NULL;
			break;

			// Don't actually reset any values-- the bot has no order
		case ORDER_NONE:
			return;
	}

	// Stop leading teammates, since the bot was probably leading them for the past order
	bs->lead_teammate = NULL;

	// Reset the order
	bs->order_type = ORDER_NONE;
	bs->order_time = 0;

	// Invalidate the goal sieve so this order will get removed from the sieve
	bs->goal_sieve_valid = qfalse;
}

/*
=====================
BotOrderAnnounceReset
=====================
*/
void BotOrderAnnounceReset(bot_state_t * bs, char *msg_type, gentity_t * recipient, char *arg)
{
	// Do nothing if the bot doesn't have an order
	if(bs->order_type == ORDER_NONE)
		return;

	// Give additional refusal actions if the bot hasn't accepted yet
	if(bs->order_message_time)
	{
		BotCommandAction(bs, ACTION_NEGATIVE);
		BotVoiceChat(bs, bs->order_requester->s.number, VOICECHAT_NO);
	}

	// If no argument is specified, use the receipient's name
	// NOTE: A NULL could also be supplied if the chat message has no
	// arguments, but in that case passing in the unused name won't hurt.
	if(!arg && recipient)
		arg = SimplifyName(EntityNameFast(recipient));

	// Either send to a specific player or the whole team (recipient < 0)
	Bot_InitialChat(bs, msg_type, arg, NULL);
	if(recipient)
	{
		trap_BotEnterChat(bs->cs, recipient->s.number, CHAT_TELL);
		BotVoiceChatOnly(bs, -1, VOICECHAT_ONPATROL);
	}
	else
	{
		trap_BotEnterChat(bs->cs, 0, CHAT_TEAM);
	}

	// Reset the order
	BotOrderReset(bs);

}

/*
===========
BotUseOrder
===========
*/
void BotUseOrder(bot_state_t * bs, gentity_t * requester, int order_type, int time)
{
	// Setup basic order information
	bs->order_type = order_type;
	bs->order_requester = requester;
	bs->order_time = bs->command_time + time;

	// Send an acknowledgement messsage at this time
	bs->order_message_time = bs->command_time + 0.5 + 1.5 * random();

	// Invalidate the goal sieve so this goal case will get added to the sieve
	bs->goal_sieve_valid = qfalse;
}

/*
===============
BotLeadTeammate
===============
*/
void BotLeadTeammate(bot_state_t * bs, gentity_t * requester, gentity_t * teammate)
{
	// Setup the leading information
	bs->lead_requester = requester->s.number;
	bs->lead_teammate = teammate;
	bs->lead_time = bs->command_time + ORDER_TIME_LEAD;
	bs->lead_visible_time = 0;
	bs->lead_announce = qtrue;
	bs->lead_message_time = bs->command_time + 0.5 + 1.5 * random();

	// Invalidate the goal sieve so this lead case will get added to the sieve
	bs->goal_sieve_valid = qfalse;
}

/*
============
BotLeadReset
============
*/
void BotLeadReset(bot_state_t * bs)
{
	char           *teammate;

	// Don't double-reset
	if(!bs->lead_teammate)
		return;

	// Inform the person we're leading that we've stopped leading them
	teammate = SimplifyName(EntityNameFast(bs->lead_teammate));
	Bot_InitialChat(bs, "lead_stop", teammate, NULL);
	trap_BotEnterChat(bs->cs, bs->lead_teammate->s.number, CHAT_TELL);

	// Inform the requester as well, if those are different people
	if((bs->lead_requester != bs->lead_teammate->s.number) && (bs->lead_requester >= 0))
	{
		Bot_InitialChat(bs, "lead_stop", teammate, NULL);
		trap_BotEnterChat(bs->cs, bs->lead_requester, CHAT_TELL);
	}

	// Reset the lead information
	bs->lead_teammate = NULL;

	// Invalidate the goal sieve so this lead case will get removed from the sieve
	bs->goal_sieve_valid = qfalse;
}

/*
============
BotMatchTime
============
*/
float BotMatchTime(bot_match_t * match)
{
	bot_match_t     timematch;
	char            timestring[MAX_MESSAGE_SIZE];

	// Use the default if the match didn't contain a time string.
	if(!(match->subtype & ST_TIME))
		return 0;

	// Get some information on that time string or use the default
	trap_BotMatchVariable(match, TIME, timestring, MAX_MESSAGE_SIZE);
	if(!trap_BotFindMatch(timestring, &timematch, MTCONTEXT_TIME))
		return 0;

	// Check for various general time lengths
	if(timematch.type == MSG_FOREVER)
		return 99999999.0f;
	if(timematch.type == MSG_FORAWHILE)
		return 10 * 60;			// 10 minutes
	if(timematch.type == MSG_FORALONGTIME)
		return 30 * 60;			// 30 minutes

	// Check for specific times
	trap_BotMatchVariable(&timematch, TIME, timestring, MAX_MESSAGE_SIZE);
	if(timematch.type == MSG_MINUTES)
		return atof(timestring) * 60;
	if(timematch.type == MSG_SECONDS)
		return atof(timestring);

	// Return the default time
	return 0;
}

/*
===============
BotUseOrderHelp
===============
*/
void BotUseOrderHelp(bot_state_t * bs, gentity_t * requester, gentity_t * teammate, float time)
{
	// Don't help yourself
	if(bs->ent == teammate)
		return;

	// Don't help enemies
	if(BotEnemyTeam(bs, teammate))
		return;

	// Setup the order
	if(time <= 0)
		time = ORDER_TIME_HELP;
	BotUseOrder(bs, requester, ORDER_HELP, time);

	// Remember this client as the teammate to help
	bs->help_teammate = teammate;

	// Assume that teammate isn't visible now
	bs->help_notseen = bs->command_time;
}

/*
====================
BotUseOrderAccompany
====================
*/
void BotUseOrderAccompany(bot_state_t * bs, gentity_t * requester, gentity_t * teammate, float time)
{
	gentity_t      *ent;

	// Don't accompany yourself
	if(bs->ent == teammate)
		return;

	// Only accompany teammates
	if(!BotSameTeam(bs, teammate))
		return;

	// Setup the order
	if(time <= 0)
		time = ORDER_TIME_ACCOMPANY;
	BotUseOrder(bs, requester, ORDER_ACCOMPANY, time);

	// Remember this client as the teammate to help
	bs->accompany_teammate = teammate;

	// Assume that teammate is visible now
	bs->accompany_seen = bs->command_time;

	// Stay a reasonable distance away
	bs->formation_dist = 3.5 * 32;	//3.5 meter

	// Tell the teammate when the bot first sees them
	bs->announce_arrive = qtrue;
}

/*
=================
BotUseOrderDefend
=================
*/
void BotUseOrderDefend(bot_state_t * bs, gentity_t * requester, bot_goal_t * goal, float time)
{
	// Setup the order
	if(time <= 0)
		time = ORDER_TIME_DEFEND;
	BotUseOrder(bs, requester, ORDER_DEFEND, time);

	// Copy the goal
	memcpy(&bs->defend_goal, goal, sizeof(bot_goal_t));
}

/*
===============
BotUseOrderItem
===============
*/
void BotUseOrderItem(bot_state_t * bs, gentity_t * requester, bot_goal_t * goal, float time)
{
	// Setup the order
	if(time <= 0)
		time = ORDER_TIME_ITEM;
	BotUseOrder(bs, requester, ORDER_ITEM, time);

	// Copy the goal
	memcpy(&bs->inspect_goal, goal, sizeof(bot_goal_t));
}

/*
===============
BotUseOrderCamp
===============
*/
void BotUseOrderCamp(bot_state_t * bs, gentity_t * requester, bot_goal_t * goal, float time)
{
	// Setup the order
	if(time <= 0)
		time = ORDER_TIME_CAMP;
	BotUseOrder(bs, requester, ORDER_CAMP, time);

	// Copy the goal
	memcpy(&bs->camp_goal, goal, sizeof(bot_goal_t));

	// Announce when the bot reaches the camp location
	bs->announce_arrive = qtrue;
}

/*
=================
BotUseOrderAttack
=================
*/
void BotUseOrderAttack(bot_state_t * bs, gentity_t * requester, gentity_t * enemy, float time)
{
	// Only attack enemies
	if(!BotEnemyTeam(bs, enemy))
		return;

	// Setup the order
	if(time <= 0)
		time = ORDER_TIME_ATTACK;
	BotUseOrder(bs, requester, ORDER_ATTACK, time);

	// Target this enemy
	bs->order_enemy = enemy;
}

/*
==================
BotUseOrderGetFlag
==================
*/
void BotUseOrderGetFlag(bot_state_t * bs, gentity_t * requester)
{
	// Only do this in capture the flag variants
	if(!(game_style & GS_FLAG))
		return;

	BotUseOrder(bs, requester, ORDER_GETFLAG, ORDER_TIME_GETFLAG);
}

/*
==================
BotUseOrderAssault
==================
*/
void BotUseOrderAssault(bot_state_t * bs, gentity_t * requester)
{
	if(!(game_style & GS_BASE))
		return;

	// Use different orders for CTF than other teamplay mods
	if(gametype == GT_CTF)
		BotUseOrderGetFlag(bs, requester);
	else
		BotUseOrder(bs, requester, ORDER_ASSAULT, ORDER_TIME_ASSAULT);
}

#ifdef MISSIONPACK
/*
==================
BotUseOrderHarvest
==================
*/
void BotUseOrderHarvest(bot_state_t * bs, gentity_t * requester)
{
	if(gametype == GT_HARVESTER)
		return;

	// Setup the order
	BotUseOrder(bs, requester, ORDER_HARVEST, ORDER_TIME_HARVEST);
}
#endif

/*
=====================
BotUseOrderReturnFlag
=====================
*/
void BotUseOrderReturnFlag(bot_state_t * bs, gentity_t * requester)
{
	// Only do this in capture the flag
	if(!(game_style & GS_FLAG))
		return;

	// Setup the order
	BotUseOrder(bs, requester, ORDER_RETURNFLAG, ORDER_TIME_RETURNFLAG);
}

/*
======================
BotMatch_HelpAccompany
======================
*/
void BotMatch_HelpAccompany(bot_state_t * bs, bot_match_t * match, gentity_t * sender)
{
	gentity_t      *teammate;
	char            teammate_name[MAX_MESSAGE_SIZE];
	bot_match_t     teammatematch;

	// Determine which teammate needs help
	trap_BotMatchVariable(match, TEAMMATE, teammate_name, sizeof(teammate_name));
	if(trap_BotFindMatch(teammate_name, &teammatematch, MTCONTEXT_TEAMMATE) && teammatematch.type != MSG_ME)
	{
		// If the message named a teammate other than this bot, help that teammate
		teammate = TeammateFromName(bs, teammate_name);

		// Ask for clarification if the bot couldn't match that name
		if(!teammate)
		{
			Bot_InitialChat(bs, "whois", teammate_name, NULL);
			trap_BotEnterChat(bs->cs, sender->s.number, CHAT_TELL);
			return;
		}
	}
	else
	{
		// Otherwise help the person who sent the message
		teammate = sender;
	}

	// Setup help and accompany requests differently
	if(match->type == MSG_HELP)
		BotUseOrderHelp(bs, sender, teammate, BotMatchTime(match));
	else
		BotUseOrderAccompany(bs, sender, teammate, BotMatchTime(match));
}

/*
======================
BotMatch_DefendKeyArea
======================
*/
void BotMatch_DefendKeyArea(bot_state_t * bs, bot_match_t * match, gentity_t * sender)
{
	char            itemname[MAX_MESSAGE_SIZE];
	bot_goal_t      goal;

	// Note that the defense area is named after an item
	trap_BotMatchVariable(match, KEYAREA, itemname, sizeof(itemname));
	if(!GoalFromName(&goal, itemname, bs))
	{
		Bot_InitialChat(bs, "cannotfind", itemname, NULL);
		trap_BotEnterChat(bs->cs, 0, CHAT_TEAM);
		return;
	}

	BotUseOrderDefend(bs, sender, &goal, BotMatchTime(match));
}

/*
================
BotMatch_GetItem
================
*/
void BotMatch_GetItem(bot_state_t * bs, bot_match_t * match, gentity_t * sender)
{
	char            itemname[MAX_MESSAGE_SIZE];
	bot_goal_t      goal;

	// Try to match the item name to a goal
	trap_BotMatchVariable(match, ITEM, itemname, sizeof(itemname));
	if(!GoalFromName(&goal, itemname, bs))
	{
		Bot_InitialChat(bs, "cannotfind", itemname, NULL);
		trap_BotEnterChat(bs->cs, 0, CHAT_TEAM);
		return;
	}

	BotUseOrderItem(bs, sender, &goal, BotMatchTime(match));
}

/*
=============
BotMatch_Camp
=============
*/
void BotMatch_Camp(bot_state_t * bs, bot_match_t * match, gentity_t * sender)
{
	char            itemname[MAX_MESSAGE_SIZE];
	bot_goal_t      goal;

	// Figure out where they're asking the bot to camp
	trap_BotMatchVariable(match, KEYAREA, itemname, sizeof(itemname));

	// Camp at the spot the bot is currently standing
	if(match->subtype & ST_THERE)
	{
		if(!GoalEntity(&goal, bs->ent))
			return;
	}

	// Try to camp where the requested player is
	else if(match->subtype & ST_HERE)
	{
		if(!GoalEntity(&goal, sender))
		{
			Bot_InitialChat(bs, "whereareyou", SimplifyName(EntityNameFast(sender)), NULL);
			trap_BotEnterChat(bs->cs, sender->s.number, CHAT_TELL);
			return;
		}
	}

	// Look for an item or other named location
	else if(!GoalFromName(&goal, itemname, bs))
	{
		Bot_InitialChat(bs, "cannotfind", itemname, NULL);
		trap_BotEnterChat(bs->cs, 0, CHAT_TEAM);
		return;
	}

	BotUseOrderCamp(bs, sender, &goal, BotMatchTime(match));
}

/*
=============
BotMatch_Kill
=============
*/
void BotMatch_Kill(bot_state_t * bs, bot_match_t * match, gentity_t * sender)
{
	char            enemy_name[MAX_MESSAGE_SIZE];
	gentity_t      *enemy;

	// Figure out who we're supposed to kill
	trap_BotMatchVariable(match, ENEMY, enemy_name, sizeof(enemy_name));
	enemy = EnemyFromName(bs, enemy_name);

	// If the bot couldn't match the enemy, ask for more information
	if(!enemy)
	{
		Bot_InitialChat(bs, "whois", enemy_name, NULL);
		trap_BotEnterChat(bs->cs, sender->s.number, CHAT_TELL);
		return;
	}

	BotUseOrderAttack(bs, sender, enemy, BotMatchTime(match));
}

/*
===============
BotMatch_Patrol

NOTE: This function doesn't have its own special setup
function like the other order match functions.  That's
because patrol waypoint matching is so complicated that
this function is the only possible interface for creating
such an order.
===============
*/
void BotMatch_Patrol(bot_state_t * bs, bot_match_t * match, gentity_t * sender)
{
	float           time;

	// Match the patrol waypoints
	if(!BotMatch_PatrolWaypoints(bs, match, sender))
		return;

	// Determine time limit of order
	time = BotMatchTime(match);
	if(time <= 0)
		time = ORDER_TIME_PATROL;

	// Setup the order
	BotUseOrder(bs, sender, ORDER_PATROL, time);
}

/*
================
BotMatch_GetFlag
================
*/
void BotMatch_GetFlag(bot_state_t * bs, bot_match_t * match, gentity_t * sender)
{
	BotUseOrderGetFlag(bs, sender);
}

/*
========================
BotMatch_AttackEnemyBase
========================
*/
void BotMatch_AttackEnemyBase(bot_state_t * bs, bot_match_t * match, gentity_t * sender)
{
	BotUseOrderAssault(bs, sender);
}

#ifdef MISSIONPACK
/*
================
BotMatch_Harvest
================
*/
void BotMatch_Harvest(bot_state_t * bs, bot_match_t * match, gentity_t * sender)
{
	BotUseOrderHarvest(bs, sender);
}
#endif

/*
===================
BotMatch_ReturnFlag
===================
*/
void BotMatch_ReturnFlag(bot_state_t * bs, bot_match_t * match, gentity_t * sender)
{
	BotUseOrderReturnFlag(bs, sender);
}

/*
================
BotMatch_Dismiss
================
*/
void BotMatch_Dismiss(bot_state_t * bs, bot_match_t * match, gentity_t * sender)
{
	// Acknowledge the dismissal
	BotOrderAnnounceReset(bs, "dismissed", sender, NULL);
}

/*
========================
BotMatch_WhatAreYouDoing
========================
*/
void BotMatch_WhatAreYouDoing(bot_state_t * bs, bot_match_t * match, gentity_t * sender)
{
	char           *message_type;
	bot_goal_t     *goal;
	gentity_t      *ent;

	// Reset the possible argument styles
	goal = NULL;
	ent = NULL;

	// Match the message type and set the appropriate arguments
	switch (bs->order_type)
	{
			// Orders without an argument
		default:
		case ORDER_NONE:
			message_type = "roaming";
			break;
		case ORDER_GETFLAG:
			message_type = "capturingflag";
			break;
		case ORDER_RETURNFLAG:
			message_type = "returningflag";
			break;
		case ORDER_HARVEST:
			message_type = "harvesting";
			break;
		case ORDER_ASSAULT:
			message_type = "attackingenemybase";
			break;
		case ORDER_CAMP:
			message_type = "camping";
			break;
		case ORDER_PATROL:
			message_type = "patrolling";
			break;

			// Orders with a player argument
		case ORDER_ATTACK:
			message_type = "killing";
			ent = bs->order_enemy;
			break;
		case ORDER_HELP:
			message_type = "helping";
			ent = bs->help_teammate;
			break;
		case ORDER_ACCOMPANY:
			message_type = "accompanying";
			ent = bs->accompany_teammate;
			break;

			// Orders with a goal item argument
		case ORDER_ITEM:
			message_type = "gettingitem";
			goal = &bs->inspect_goal;
			break;
		case ORDER_DEFEND:
			message_type = "defending";
			goal = &bs->defend_goal;
			break;
	}

	// Send the chat message
	if(ent)
		Bot_InitialChat(bs, message_type, SimplifyName(EntityNameFast(ent)), NULL);
	else if(goal)
		Bot_InitialChat(bs, message_type, GoalNameFast(goal), NULL);
	else
		Bot_InitialChat(bs, message_type, NULL);

	trap_BotEnterChat(bs->cs, sender->s.number, CHAT_TELL);
}

/*
===================
BotMatch_LeadTheWay
===================
*/
void BotMatch_LeadTheWay(bot_state_t * bs, bot_match_t * match, gentity_t * sender)
{
	char            name[MAX_MESSAGE_SIZE];
	gentity_t      *teammate, *someone;

	// By default, lead the client who sent the message
	teammate = sender;

	// Possibly lead a specific teammate requested in the message
	if(match->subtype & ST_SOMEONE)
	{
		// Find out the person's name
		trap_BotMatchVariable(match, TEAMMATE, name, sizeof(name));
		someone = TeammateFromName(bs, name);
		if(!someone)
		{
			Bot_InitialChat(bs, "whois", name, NULL);
			trap_BotEnterChat(bs->cs, bs->client, CHAT_TEAM);
			return;
		}

		// Lead that person instead if they are a teammate and aren't the bot
		if(someone != bs->ent && BotSameTeam(bs, someone))
			teammate = someone;
	}

	BotLeadTeammate(bs, sender, teammate);
}

/*
====================
BotMatch_WhereAreYou
====================
*/
void BotMatch_WhereAreYou(bot_state_t * bs, bot_match_t * match, gentity_t * sender)
{
	float           red_time, blue_time;
	const char     *region_name, *color;
	char            netname[MAX_MESSAGE_SIZE];

	// Look up the name of the bot's region, if any
	region_name = LevelNearestRegionName(bs->now.origin);
	if(!region_name)
		return;

	// Possibly give the region a team base identifier
	// FIXME: It would be nice if LevelNearestRegionName() did this for us.
	color = NULL;
	if((game_style & GS_BASE) && (game_style & GS_TEAM))
	{
		// Determine how close the bot is to different bases
		red_time = EntityGoalTravelTime(bs->ent, &bases[RED_BASE], bs->travel_flags);
		blue_time = EntityGoalTravelTime(bs->ent, &bases[BLUE_BASE], bs->travel_flags);

		// If both bases are routable, look for a closer base
		if(red_time >= 0 && blue_time >= 0)
		{
			if(red_time < (red_time + blue_time) * 0.4)
				color = "red";
			else if(blue_time < (red_time + blue_time) * 0.4)
				color = "blue";
		}
		// Otherwise use one base if it's still routable
		else if(red_time >= 0)
			color = "red";
		else if(blue_time >= 0)
			color = "blue";
	}

	// Provide a base color identifier if possible
	if(color)
		Bot_InitialChat(bs, "teamlocation", region_name, color, NULL);
	else
		Bot_InitialChat(bs, "location", region_name, NULL);

	trap_BotEnterChat(bs->cs, sender->s.number, CHAT_TELL);
}

/*
================
BotMatch_Suicide
================
*/
void BotMatch_Suicide(bot_state_t * bs, bot_match_t * match, gentity_t * sender)
{
	char            netname[MAX_MESSAGE_SIZE];
	int             client;

	trap_EA_Command(bs->client, "kill");

	BotVoiceChat(bs, sender->s.number, VOICECHAT_TAUNT);
	BotCommandAction(bs, ACTION_AFFIRMATIVE);
}

/*
==============
BotMatch_Order

This function returns true if it successfully classified the message
match as an order-related command it could process.  Note that the
processing could fail, but the bot would still return true, as long
as the message type was correct.
==============
*/
qboolean BotMatch_Order(bot_state_t * bs, bot_match_t * match, gentity_t * sender)
{
	// A function to call to match a specific type of order message
	void            (*order_match) (bot_state_t * bs, bot_match_t * match, gentity_t * sender) = 0;

	// Figure out what kind of message this is
	switch (match->type)
	{
			// These messages create new orders
		case MSG_HELP:
		case MSG_ACCOMPANY:
			order_match = BotMatch_HelpAccompany;
			break;
		case MSG_KILL:
			order_match = BotMatch_Kill;
			break;
		case MSG_DEFENDKEYAREA:
			order_match = BotMatch_DefendKeyArea;
			break;
		case MSG_GETITEM:
			order_match = BotMatch_GetItem;
			break;
		case MSG_CAMP:
			order_match = BotMatch_Camp;
			break;
		case MSG_PATROL:
			order_match = BotMatch_Patrol;
			break;
		case MSG_GETFLAG:
			order_match = BotMatch_GetFlag;
			break;
		case MSG_ATTACKENEMYBASE:
			order_match = BotMatch_AttackEnemyBase;
			break;
#ifdef MISSIONPACK
		case MSG_HARVEST:
			order_match = BotMatch_Harvest;
			break;
#endif
		case MSG_RETURNFLAG:
			order_match = BotMatch_ReturnFlag;
			break;

			// These are other order-related messages
		case MSG_DISMISS:
			order_match = BotMatch_Dismiss;
			break;
		case MSG_WHATAREYOUDOING:
			order_match = BotMatch_WhatAreYouDoing;
			break;
		case MSG_LEADTHEWAY:
			order_match = BotMatch_LeadTheWay;
			break;
		case MSG_WHEREAREYOU:
			order_match = BotMatch_WhereAreYou;
			break;
		case MSG_SUICIDE:
			order_match = BotMatch_Suicide;
			break;

			// This message is pointless-- bots rush to bases whenever possible
		case MSG_RUSHBASE:
			return qtrue;

			// Maybe this wasn't an order message
		default:
			return qfalse;
	}

	// Process whatever information was in the order-related message match
	order_match(bs, match, sender);
	return qtrue;
}

/*
====================
BotVoiceChat_GetFlag
====================
*/
void BotVoiceChat_GetFlag(bot_state_t * bs, gentity_t * sender)
{
	BotUseOrderGetFlag(bs, sender);
}

/*
====================
BotVoiceChat_Offense
====================
*/
void BotVoiceChat_Offense(bot_state_t * bs, gentity_t * sender)
{
	if(game_style & GS_FLAG)
	{
		BotUseOrderGetFlag(bs, sender);
		return;
	}

#ifdef MISSIONPACK
	if(gametype == GT_HARVESTER)
	{
		BotUseOrderHarvest(bs, sender);
		return;
	}

#endif

	BotUseOrderAssault(bs, sender);
}

/*
===================
BotVoiceChat_Defend
===================
*/
void BotVoiceChat_Defend(bot_state_t * bs, gentity_t * sender)
{
	// Can only defend when bases are present
	if(!(game_style & GS_BASE))
		return;

	BotUseOrderDefend(bs, sender, &bases[BotTeamBase(bs)], 0);
}

/*
=======================
BotVoiceChat_DefendFlag
=======================
*/
void BotVoiceChat_DefendFlag(bot_state_t * bs, gentity_t * sender)
{
	BotVoiceChat_Defend(bs, sender);
}

/*
===================
BotVoiceChat_Patrol
===================
*/
void BotVoiceChat_Patrol(bot_state_t * bs, gentity_t * sender)
{
	// Acknowledge the dismissal
	BotOrderAnnounceReset(bs, "dismissed", sender, NULL);
}

/*
=================
BotVoiceChat_Camp
=================
*/
void BotVoiceChat_Camp(bot_state_t * bs, gentity_t * sender)
{
	int             areanum;
	gentity_t      *ent;
	bot_goal_t      goal;

	// Assume the bot should camp at the requester's location
	if(!GoalEntity(&goal, sender))
	{
		Bot_InitialChat(bs, "whereareyou", SimplifyName(EntityNameFast(sender)), NULL);
		trap_BotEnterChat(bs->cs, sender->s.number, CHAT_TELL);
		return;
	}

	// Camp that location
	BotUseOrderCamp(bs, sender, &goal, 0);
}

/*
=====================
BotVoiceChat_FollowMe
=====================
*/
void BotVoiceChat_FollowMe(bot_state_t * bs, gentity_t * sender)
{
	// Accompany the requesting teammate
	BotUseOrderAccompany(bs, sender, sender, 0);
}

/*
==============================
BotVoiceChat_FollowFlagCarrier
==============================
*/
void BotVoiceChat_FollowFlagCarrier(bot_state_t * bs, gentity_t * sender)
{
	// Follow our team's flag carrier if one exists
	if(bs->our_target_flag_status == FS_CARRIER)
		BotUseOrderAccompany(bs, sender, bs->our_target_flag, 0);
}

/*
=======================
BotVoiceChat_ReturnFlag
=======================
*/
void BotVoiceChat_ReturnFlag(bot_state_t * bs, gentity_t * sender)
{
	BotUseOrderReturnFlag(bs, sender);
}

/*
========================
BotVoiceChat_StartLeader
========================
*/
void BotVoiceChat_StartLeader(bot_state_t * bs, gentity_t * sender)
{
	BotTeamLeaderStart(bs, sender);
}

/*
=======================
BotVoiceChat_StopLeader
=======================
*/
void BotVoiceChat_StopLeader(bot_state_t * bs, gentity_t * sender)
{
	BotTeamLeaderStop(bs, sender);
}

/*
========================
BotVoiceChat_WhoIsLeader
========================
*/
void BotVoiceChat_WhoIsLeader(bot_state_t * bs, gentity_t * sender)
{
	if(!(game_style & GS_TEAM))
		return;

	// Check if this bot is the team leader
	if(bs->ent == bs->leader)
	{
		Bot_InitialChat(bs, "iamleader", NULL);
		trap_BotEnterChat(bs->cs, 0, CHAT_TEAM);
		BotVoiceChatOnly(bs, -1, VOICECHAT_STARTLEADER);
	}
}

/*
==========================
BotVoiceChat_WantOnDefense
==========================
*/
void BotVoiceChat_WantOnDefense(bot_state_t * bs, gentity_t * sender)
{
	BotSetTeammatePreference(bs, sender, TASKPREF_DEFENDER);
}

/*
==========================
BotVoiceChat_WantOnOffense
==========================
*/
void BotVoiceChat_WantOnOffense(bot_state_t * bs, gentity_t * sender)
{
	BotSetTeammatePreference(bs, sender, TASKPREF_ATTACKER);
}

// Table of voice messages to match
typedef struct voiceCommand_s
{
	char           *cmd;
	void            (*func) (bot_state_t * bs, gentity_t * sender);
} voiceCommand_t;

voiceCommand_t  voiceCommands[] = {
	{VOICECHAT_GETFLAG, BotVoiceChat_GetFlag},
	{VOICECHAT_OFFENSE, BotVoiceChat_Offense},
	{VOICECHAT_DEFEND, BotVoiceChat_Defend},
	{VOICECHAT_DEFENDFLAG, BotVoiceChat_DefendFlag},
	{VOICECHAT_PATROL, BotVoiceChat_Patrol},
	{VOICECHAT_CAMP, BotVoiceChat_Camp},
	{VOICECHAT_FOLLOWME, BotVoiceChat_FollowMe},
	{VOICECHAT_FOLLOWFLAGCARRIER, BotVoiceChat_FollowFlagCarrier},
	{VOICECHAT_RETURNFLAG, BotVoiceChat_ReturnFlag},
	{VOICECHAT_STARTLEADER, BotVoiceChat_StartLeader},
	{VOICECHAT_STOPLEADER, BotVoiceChat_StopLeader},
	{VOICECHAT_WHOISLEADER, BotVoiceChat_WhoIsLeader},
	{VOICECHAT_WANTONDEFENSE, BotVoiceChat_WantOnDefense},
	{VOICECHAT_WANTONOFFENSE, BotVoiceChat_WantOnOffense},
	{NULL, 0}
};

qboolean BotVoiceChatCommand(bot_state_t * bs, int mode, char *voiceChat)
{
	int             i, voiceOnly, clientNum, color;
	char           *ptr, buf[MAX_MESSAGE_SIZE], *cmd;
	gentity_t      *ent;

	// Only track these in teamplay modes
	if(!(game_style & GS_TEAM))
		return qfalse;

	// Ignore voice chats sent to everyone
	if(mode == SAY_ALL)
		return qfalse;

	Q_strncpyz(buf, voiceChat, sizeof(buf));
	cmd = buf;
	for(ptr = cmd; *cmd && *cmd > ' '; cmd++);
	while(*cmd && *cmd <= ' ')
		*cmd++ = '\0';
	voiceOnly = atoi(ptr);
	for(ptr = cmd; *cmd && *cmd > ' '; cmd++);
	while(*cmd && *cmd <= ' ')
		*cmd++ = '\0';
	clientNum = atoi(ptr);
	for(ptr = cmd; *cmd && *cmd > ' '; cmd++);
	while(*cmd && *cmd <= ' ')
		*cmd++ = '\0';
	color = atoi(ptr);

	if(clientNum < 0 || clientNum >= MAX_CLIENTS)
		return qfalse;

	ent = &g_entities[clientNum];
	if(!BotSameTeam(bs, ent))
		return qfalse;

	for(i = 0; voiceCommands[i].cmd; i++)
	{
		if(!Q_stricmp(cmd, voiceCommands[i].cmd))
		{
			voiceCommands[i].func(bs, ent);
			return qtrue;
		}
	}
	return qfalse;
}
