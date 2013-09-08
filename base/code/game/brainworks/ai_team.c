// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_team.c
 *
 * Functions that the bot uses to manage team interactions
 *****************************************************************************/

#include "ai_main.h"
#include "ai_vars.h"
#include "ai_team.h"

#include "ai_client.h"
#include "ai_chat.h"
#include "ai_command.h"
#include "ai_entity.h"
#include "ai_level.h"
#include "ai_self.h"
#include "ai_waypoint.h"


// For the voice chats
#include "../../ui/menudef.h"


// Different kinds of team strategies
#define STRATEGY_AGGRESSIVE				0x01


// This boolean value is true if the (human only?) client requested to be a leader
// By default, it is false for any connected players
qboolean        could_be_leader[MAX_CLIENTS];

// Ctf task preferences for a client
typedef struct bot_ctftaskpreference_s
{
	char            name[36];
	int             preference;
} bot_ctftaskpreference_t;

bot_ctftaskpreference_t ctftaskpreferences[MAX_CLIENTS];


/*
===============
BotReportStatus
===============
*/
void BotReportStatus(bot_state_t * bs)
{
	char           *leader, flagstatus[32];

	leader = (bs->ent == bs->leader ? "L" : " ");

	if(BotIsCarrier(bs))
	{
#ifdef MISSIONPACK
		if(gametype == GT_HARVESTER)
		{
			Com_sprintf(flagstatus, sizeof(flagstatus),
						(BotTeam(bs) == TEAM_RED ? S_COLOR_RED "%2d" : S_COLOR_BLUE "%2d"), bs->ps->generic1);
		}
		else
#endif
			strcpy(flagstatus, (BotTeam(bs) == TEAM_RED ? S_COLOR_RED "F " : S_COLOR_BLUE "F "));

	}
	else
	{
		strcpy(flagstatus, "  ");
	}

	BotAI_Print(PRT_MESSAGE, "%-20s%s%s\n", EntityNameFast(bs->ent), leader, flagstatus);
}

/*
=================
BotTeamplayReport
=================
*/
void BotTeamplayReport(void)
{
	int             i;
	bot_state_t    *bs;

	// Print all red team members
	BotAI_Print(PRT_MESSAGE, S_COLOR_RED "RED\n");
	for(i = 0; i < maxclients && i < MAX_CLIENTS; i++)
	{
		// Ignore all bots not on the red team
		bs = bot_states[i];
		if(!bs || !bs->inuse || EntityTeam(bs->ent) != TEAM_RED)
			continue;

		// Print the status of that bot
		BotReportStatus(bs);
	}

	// Print all blue team members
	BotAI_Print(PRT_MESSAGE, S_COLOR_BLUE "BLUE\n");
	for(i = 0; i < maxclients && i < MAX_CLIENTS; i++)
	{
		// Ignore all bots not on the blue team
		bs = bot_states[i];
		if(!bs || !bs->inuse || EntityTeam(bs->ent) != TEAM_BLUE)
			continue;

		// Print the status of that bot
		BotReportStatus(bs);
	}
}

/*
=================
BotPreferAttacker
=================
*/
qboolean BotPreferAttacker(bot_state_t * bs)
{
	return (bs->team_preference & TASKPREF_ATTACKER);
}

/*
=================
BotPreferDefender
=================
*/
qboolean BotPreferDefender(bot_state_t * bs)
{
	return (bs->team_preference & TASKPREF_DEFENDER);
}

#ifdef MISSIONPACK
/*
=======================
BotUpdateTaskPreference

Check if the bot's own task preferences have changed
=======================
*/
void BotUpdateTaskPreference(bot_state_t * bs)
{
	int             task, leader;

	// Only select offense or defense style when in a game mode with bases
	if(!(game_style & GS_BASE))
		return;

	// Prefer offense if the bot has the kamikaze or invulnerability
	if(bs->ps->stats[STAT_HOLDABLE_ITEM] == MODELINDEX_KAMIKAZE)
		task = TASKPREF_ATTACKER;
	else if(bs->ps->stats[STAT_HOLDABLE_ITEM] == MODELINDEX_INVULNERABILITY)
		task = TASKPREF_ATTACKER;

	// Persistant powerups give reasons to attack or defend
	else if(bs->ps->powerups[PW_SCOUT])
		task = TASKPREF_ATTACKER;
	else if(bs->ps->powerups[PW_GUARD])
		task = TASKPREF_ATTACKER;
	else if(bs->ps->powerups[PW_DOUBLER])
		task = TASKPREF_DEFENDER;
	else if(bs->ps->powerups[PW_AMMOREGEN])
		task = TASKPREF_DEFENDER;
	else
		return;

	// Only annoucne the task preference if it's a change
	if(bs->team_preference & task)
		return;

	// Lookup the team leader, or send to -1 (whole team) if no leader
	leader = (bs->leader ? bs->leader->s.number : -1);

	// Update attackers and defenders accordingly
	if(task & TASKPREF_ATTACKER)
	{
		BotVoiceChat(bs, leader, VOICECHAT_WANTONOFFENSE);
		bs->team_preference |= TASKPREF_ATTACKER;
		bs->team_preference &= ~TASKPREF_DEFENDER;
	}
	else
	{
		BotVoiceChat(bs, leader, VOICECHAT_WANTONDEFENSE);
		bs->team_preference |= TASKPREF_DEFENDER;
		bs->team_preference &= ~TASKPREF_ATTACKER;
	}
}
#endif

/*
==================
BotTeamLeaderStart
==================
*/
void BotTeamLeaderStart(bot_state_t * bs, gentity_t * leader)
{
	// Make sure a valid leader was specified
	if(!leader || !BotSameTeam(bs, leader))
		return;

	// Consider this player the leader for now
	bs->leader = leader;
	could_be_leader[leader->s.number] = qtrue;
}

/*
=================
BotTeamLeaderStop
=================
*/
void BotTeamLeaderStop(bot_state_t * bs, gentity_t * leader)
{
	// Make sure a valid leader was specified
	if(!leader)
		return;

	// If the bot thought this was the leader, assume there is no leader
	if(bs->leader == leader)
		bs->leader = NULL;
	could_be_leader[leader->s.number] = qfalse;
}

/*
==============
BotCheckLeader

Confirm that the team leader is still
connected and on the bot's team.
==============
*/
void BotCheckLeader(bot_state_t * bs)
{
	if(bs->leader && !BotSameTeam(bs, bs->leader))
		BotTeamLeaderStop(bs, bs->leader);
}

/*
============================
BotGetTeammateTaskPreference
============================
*/
int BotGetTeammateTaskPreference(bot_state_t * bs, gentity_t * teammate)
{
	if(!ctftaskpreferences[teammate->s.number].preference)
		return 0;

	if(Q_stricmp(EntityNameFast(teammate), ctftaskpreferences[teammate->s.number].name))
		return 0;

	return ctftaskpreferences[teammate->s.number].preference;
}

/*
===========================
BotUpdateTeammatePreference

Copies (value & mask) over (preferences & mask) but
preserves (preferences & ~mask)
===========================
*/
void BotUpdateTeammatePreference(bot_state_t * bs, gentity_t * teammate, int mask, int value)
{
	int             teammate_num;
	int             preference = 0x0000;
	char            teammate_name[MAX_MESSAGE_SIZE];

	// Preferences must be matched to a specific player name
	teammate_num = teammate->s.number;
	EntityName(teammate, teammate_name, sizeof(teammate_name));
	if(ctftaskpreferences[teammate_num].preference != 0x0000 && !Q_stricmp(teammate_name, ctftaskpreferences[teammate_num].name))
	{
		preference = ctftaskpreferences[teammate_num].preference;
	}

	// Change the preference value
	ctftaskpreferences[teammate_num].preference = (value & mask) | (preference & ~mask);

	// Update the preference name
	strcpy(ctftaskpreferences[teammate_num].name, teammate_name);

	// Acknowledge this change
	Bot_InitialChat(bs, "keepinmind", SimplifyName(teammate_name), NULL);
	trap_BotEnterChat(bs->cs, teammate_num, CHAT_TELL);
	BotVoiceChatOnly(bs, teammate_num, VOICECHAT_YES);
	BotCommandAction(bs, ACTION_AFFIRMATIVE);
}

/*
========================
BotSetTeammatePreference
========================
*/
void BotSetTeammatePreference(bot_state_t * bs, gentity_t * teammate, int pref)
{
	BotUpdateTeammatePreference(bs, teammate, (TASKPREF_ATTACKER | TASKPREF_DEFENDER), pref);
}

/*
=================
BotSplitTeammates

This function determines the optimal number of teammates to allocate for
a task.  The inputs are the total number of teammates and ideal task
percentages.  The outputs are the actual (integral) number of teammates
to allocate to each task.

For example, suppose the input task weights are [.6, .2, .1] and the
number of teammates is 6.  This means task #0 should have 60% of the
6 players, task #1 gets 20%, and task #2 gets 10%.  Note that these
percentages need not add up to 100%.  Also note that the first tasks
are considered the more important than the later tasks, so the split
rounds up to guarantee that at least 60% of the players are on task 0.

In this example, we want to allocate .6 * 6 = 3.6 players to task #0,
which gets rounded up to 4 players.  This leaves 2 players for the
remaining tasks.  We want .2 * 6 = 1.2 players for task #1, so this
is rounded up to 2 players, leaving 0 players for task #2.

But if 8 players were on the team, 4.8 => 5 players would get assigned
to task #0, 1.6 => 2 players would get assigned to task #1, and
.8 => 1 player would get assigned to task #2.

These player allocations are stored in the players array.  Note that
"weights" and "sizes" should both be arrays of length "num_tasks".

Note that it is the responsibility of the calling function to determine
which N players to assign to each task.  This function only provides
the ideal integral splitting for an arbitrary number of tasks.
=================
*/
void BotSplitTeammates(int num_teammates, int num_tasks, const float *weights, int *counts)
{
	int             i, free_teammates, task_size;

	// Allocate teammates starting with the first tasks
	free_teammates = num_teammates;	// Number of teammates not allocated to tasks
	for(i = 0; i < num_tasks; i++)
	{
		// Determine ideal number of teammates for the task, but never allocate
		// more players than available
		task_size = ceil(weights[i] * num_teammates);
		if(task_size > free_teammates)
			task_size = free_teammates;

		// Record this value and decrement the number of available teammates
		counts[i] = task_size;
		free_teammates -= task_size;
	}
}

/*
================
BotSortTeammates

NOTE: This function does not include the team's flag carrier
================
*/
int BotSortTeammates(bot_state_t * bs, gentity_t ** teammates, int maxteammates)
{
	int             i, current_teammate, preference, base, our_base, their_base;
	int             num_teammates, num_defenders, num_roamers, num_attackers;
	entry_float_gentity_t defenders[MAX_CLIENTS], roamers[MAX_CLIENTS], attackers[MAX_CLIENTS];
	entry_float_gentity_t *entry;
	gentity_t      *ent;

	// Initialize arrays
	num_teammates = 0;
	num_defenders = 0;
	num_roamers = 0;
	num_attackers = 0;

	// Determine which base is which
	BotBothBases(bs, &our_base, &their_base);
	if(our_base < 0 || their_base < 0)
		return 0;

	// Get a list of all teammates to include in teammate list
	for(i = 0, ent = &g_entities[0]; i < maxclients && i < MAX_CLIENTS; i++, ent++)
	{
		// Only track connected players on the same team
		if(!BotSameTeam(bs, ent))
			continue;

		// Don't track flag carriers-- we shouldn't give orders to them anyway
		if(bs->our_target_flag == ent)
			continue;

		// Determine which catagory this teammate belongs to
		preference = BotGetTeammateTaskPreference(bs, ent);
		if(preference & TASKPREF_DEFENDER)
		{
			entry = &defenders[num_defenders++];
			base = our_base;
		}
		else if(preference & TASKPREF_ATTACKER)
		{
			entry = &attackers[num_attackers++];
			base = their_base;
		}
		else
		{
			entry = &roamers[num_roamers++];
			base = our_base;
		}

		// Entities in each catagory are sorted by travel time to their ideal location
		entry->key = EntityGoalTravelTime(ent, &bases[base], TFL_DEFAULT);
		entry->value = ent;

		// Stop scanning clients when we receive too many teammates
		if(++num_teammates >= maxteammates)
			break;
	}

	// Sort each teammate list by travel time
	// NOTE: Attackers are sorted such that those furthest from the enemy base are first
	// in the sorted list and are therefore less likely to be chosen as an attacker.
	qsort(defenders, num_defenders, sizeof(entry_float_gentity_t), CompareEntryFloat);
	qsort(attackers, num_attackers, sizeof(entry_float_gentity_t), CompareEntryFloatReverse);
	qsort(roamers, num_roamers, sizeof(entry_float_gentity_t), CompareEntryFloat);

	// Extract the client numbers from the lists and store them in the caller's array
	current_teammate = 0;

	// Store defenders at the front, with those closest to the home base first
	for(i = 0; i < num_defenders; i++)
		teammates[current_teammate++] = defenders[i].value;

	// Store roamers in the middle, with those closest to the home base earlier
	for(i = 0; i < num_roamers; i++)
		teammates[current_teammate++] = roamers[i].value;

	// Store attackers at the end, with those closest to the enemy base last
	for(i = 0; i < num_attackers; i++)
		teammates[current_teammate++] = attackers[i].value;

	return num_teammates;
}

/*
=====================
BotSayTeamOrderAlways
=====================
*/
void BotSayTeamOrderAlways(bot_state_t * bs, int toclient)
{
	char            teamchat[MAX_MESSAGE_SIZE];
	char            buf[MAX_MESSAGE_SIZE];

	// Handle messages to other players in the standard fashion
	if(bs->client != toclient)
	{
		trap_BotEnterChat(bs->cs, toclient, CHAT_TELL);
		return;
	}

	// For messages to the bot itself, just put the message directly in the console queue
	trap_BotGetChatMessage(bs->cs, buf, sizeof(buf));
	Com_sprintf(teamchat, sizeof(teamchat), EC "(%s" EC ")" EC ": %s", EntityNameFast(bs->ent), buf);
	trap_BotQueueConsoleMessage(bs->cs, CMS_CHAT, teamchat);
}

/*
===============
BotSayTeamOrder
===============
*/
void BotSayTeamOrder(bot_state_t * bs, int toclient)
{
#ifdef MISSIONPACK
	// voice chats only
	char            buf[MAX_MESSAGE_SIZE];

	trap_BotGetChatMessage(bs->cs, buf, sizeof(buf));
#else
	BotSayTeamOrderAlways(bs, toclient);
#endif
}

/*
============
BotVoiceChat
============
*/
void BotVoiceChat(bot_state_t * bs, int toclient, char *voicechat)
{
#ifdef MISSIONPACK
	if(toclient == -1)
		// voice only say team
		trap_EA_Command(bs->client, va("vsay_team %s", voicechat));
	else
		// voice only tell single player
		trap_EA_Command(bs->client, va("vtell %d %s", toclient, voicechat));
#endif
}

/*
================
BotVoiceChatOnly
================
*/
void BotVoiceChatOnly(bot_state_t * bs, int toclient, char *voicechat)
{
#ifdef MISSIONPACK
	if(toclient == -1)
		// voice only say team
		trap_EA_Command(bs->client, va("vosay_team %s", voicechat));
	else
		// voice only tell single player
		trap_EA_Command(bs->client, va("votell %d %s", toclient, voicechat));
#endif
}

/*
====================
BotSayVoiceTeamOrder
====================
*/
void BotSayVoiceTeamOrder(bot_state_t * bs, int toclient, char *voicechat)
{
#ifdef MISSIONPACK
	BotVoiceChat(bs, toclient, voicechat);
#endif
}

/*
================
BotOrder_GetFlag
================
*/
void BotOrder_GetFlag(bot_state_t * bs, gentity_t * client)
{
	Bot_InitialChat(bs, "cmd_getflag", EntityNameFast(client), NULL);

	BotSayTeamOrder(bs, client->s.number);
	BotSayVoiceTeamOrder(bs, client->s.number, VOICECHAT_GETFLAG);
}

/*
===================
BotOrder_ReturnFlag
===================
*/
void BotOrder_ReturnFlag(bot_state_t * bs, gentity_t * client)
{
	Bot_InitialChat(bs, "cmd_returnflag", EntityNameFast(client), NULL);

	BotSayTeamOrder(bs, client->s.number);
	BotSayVoiceTeamOrder(bs, client->s.number, VOICECHAT_RETURNFLAG);
}

/*
===============
BotOrder_Defend
===============
*/
void BotOrder_Defend(bot_state_t * bs, gentity_t * client)
{
	Bot_InitialChat(bs, "cmd_defendbase", EntityNameFast(client), NULL);

	BotSayTeamOrder(bs, client->s.number);
	BotSayVoiceTeamOrder(bs, client->s.number, VOICECHAT_DEFEND);
}

/*
================
BotOrder_Assault
================
*/
void BotOrder_Assault(bot_state_t * bs, gentity_t * client)
{
	Bot_InitialChat(bs, "cmd_attackenemybase", EntityNameFast(client), NULL);

	BotSayTeamOrder(bs, client->s.number);
	BotSayVoiceTeamOrder(bs, client->s.number, VOICECHAT_OFFENSE);
}

/*
================
BotOrder_Harvest
================
*/
void BotOrder_Harvest(bot_state_t * bs, gentity_t * client)
{
	Bot_InitialChat(bs, "cmd_harvest", EntityNameFast(client), NULL);

	BotSayTeamOrder(bs, client->s.number);
	BotSayVoiceTeamOrder(bs, client->s.number, VOICECHAT_OFFENSE);
}

/*
==================
BotOrder_Accompany

NOTE: If there is no one to accompany, the bot will
try to get the flag instead
==================
*/
void BotOrder_Accompany(bot_state_t * bs, gentity_t * client)
{
	// If the bot's target flag is missing, try returning the flag instead
	// (This happens in one-flag CTF)
	if(bs->our_target_flag == FS_MISSING)
	{
		BotOrder_ReturnFlag(bs, client);
		return;
	}

	// If there is no flag carrier to help, default to getting the flag
	if(bs->our_target_flag_status != FS_CARRIER)
	{
		BotOrder_GetFlag(bs, client);
		return;
	}

	// Different messages for whether or not the ordering bot is the carrier
	if(bs->our_target_flag == bs->ent)
	{
		Bot_InitialChat(bs, "cmd_accompanyme", EntityNameFast(client), NULL);
		BotSayTeamOrder(bs, client->s.number);
		BotSayVoiceTeamOrder(bs, client->s.number, VOICECHAT_FOLLOWME);
	}
	else
	{
		Bot_InitialChat(bs, "cmd_accompany", EntityNameFast(client), EntityNameFast(bs->our_target_flag), NULL);
		BotSayTeamOrder(bs, client->s.number);
		BotSayVoiceTeamOrder(bs, client->s.number, VOICECHAT_FOLLOWFLAGCARRIER);
	}
}

/*
==============
BotCreateGroup
==============
*/
void BotCreateGroup(bot_state_t * bs, gentity_t ** teammates, int groupsize)
{
	int             i;
	char            leader[MAX_NETNAME];

	// The others in the group will follow the teammates[0]
	for(i = 1; i < groupsize; i++)
	{
		if(teammates[0] == bs->ent)
			Bot_InitialChat(bs, "cmd_accompanyme", EntityNameFast(teammates[i]), NULL);
		else
			Bot_InitialChat(bs, "cmd_accompany", EntityNameFast(teammates[i]), leader, NULL);
		BotSayTeamOrderAlways(bs, teammates[i]->s.number);
	}
}

/*
=============
BotTeamOrders

FIXME: Perhaps orders should include defending item
clusters with high base value.  See ai_resource.h
for more information
=============
*/
void BotTeamOrders(bot_state_t * bs, gentity_t ** teammates, int num_teammates)
{
	int             i, group_size;

	// Give orders again in two minutes
	bs->give_orders_time = bs->command_time + 120.0;

	// Create team groups whose sizes depends on the number of team members
	switch (num_teammates)
	{
		case 1:
			break;
		case 2:
			// The two players won't necessarily stay together
			break;
		case 3:
			// Have one pair of teammates and another free roam
			BotCreateGroup(bs, teammates, 2);
			break;
		case 4:
			BotCreateGroup(bs, teammates, 2);	// Group of 2
			BotCreateGroup(bs, &teammates[2], 2);	// Group of 2
			break;
		case 5:
			BotCreateGroup(bs, teammates, 2);	// Group of 2
			BotCreateGroup(bs, &teammates[2], 3);	// Group of 3
			break;
		default:
			// Divide the teammates into pairs if there aren't too many of them
			if(num_teammates <= 10)
			{
				i = 0;
				while(i < num_teammates)
				{
					// Make a pair unless exactly three players are left
					switch (num_teammates - i)
					{
						case 1:
							group_size = 1;
							break;	// This shouldn't occur
						case 3:
							group_size = 3;
							break;
						default:
							group_size = 2;
							break;
					}

					// Allocate the next players for this group
					BotCreateGroup(bs, &teammates[i], group_size);
					i += group_size;
				}
			}
			break;
	}
}

/*
=====================
BotAttackDefendOrders

Splits the teammates list into two groups-- attackers and defenders.
The "attack_weight" argument defines what percentage will be allocated
towards attacking, while the remainder are allocated towards defending.
If "favor_attack" is true, the groups will be rounded in favor of
attacks (so "attack_weight" is the minimum percentage of attackers).
If it's false, the groups are rounded towards defenders (making
"attack_weight" the maximum percentage of attackers).

"attack_order" and "defend_order" are functions which give appropriate
attack or defend orders to a given teammate.

NOTE: The teammate list will not include any team carriers.
=====================
*/
void BotAttackDefendOrders(bot_state_t * bs, gentity_t ** teammates, int num_teammates,
						   float attack_weight, qboolean favor_attack,
						   void (attack_order) (bot_state_t * bs, gentity_t * client),
						   void (defend_order) (bot_state_t * bs, gentity_t * client))
{
	int             i, attack_index, defend_index, attackers, defenders;
	int             task_counts[2];
	float           task_weights[2];

	// Determine the task array indicies for attacking and defending
	if(favor_attack)
	{
		attack_index = 0;
		defend_index = 1;
	}
	else
	{
		defend_index = 0;
		attack_index = 1;
	}

	// NOTE: We could do this instead, but I don't think it's worth the confusing syntax
	//attack_index = !favor_attack;
	//defend_index = favor_attack;

	// Setup the appropriate weights
	task_weights[attack_index] = attack_weight;
	task_weights[defend_index] = 1.0 - attack_weight;

	// Get the optimal split with rounding
	BotSplitTeammates(num_teammates, 2, task_weights, task_counts);
	attackers = task_counts[attack_index];
	defenders = task_counts[defend_index];

	// Order defenders and attackers appropriately
	for(i = 0; i < defenders; i++)
		defend_order(bs, teammates[i]);
	for(i = num_teammates - 1; i + attackers >= num_teammates; i--)
		attack_order(bs, teammates[i]);
}

/*
===============================
BotCTFOrders_BothFlagsNotAtBase
===============================
*/
void BotCTFOrders_BothFlagsNotAtBase(bot_state_t * bs, gentity_t ** teammates, int num_teammates)
{
	// Send most teammates to return the flag but have a few escort the carrier
	BotAttackDefendOrders(bs, teammates, num_teammates, .4, qfalse, BotOrder_Accompany, BotOrder_ReturnFlag);
}

/*
=============================
BotCTFOrders_OurFlagNotAtBase
=============================
*/
void BotCTFOrders_OurFlagNotAtBase(bot_state_t * bs, gentity_t ** teammates, int num_teammates)
{
	// Send most people to retreive the flag and have others try to kill the enemy carrier
	BotAttackDefendOrders(bs, teammates, num_teammates,
						  (bs->team_strategy & STRATEGY_AGGRESSIVE ? .7 : .5), qtrue, BotOrder_GetFlag, BotOrder_ReturnFlag);
}

/*
===============================
BotCTFOrders_EnemyFlagNotAtBase
===============================
*/
void BotCTFOrders_EnemyFlagNotAtBase(bot_state_t * bs, gentity_t ** teammates, int num_teammates)
{
	// A fairly even split between escorting our flag carrier and defending our flag
	BotAttackDefendOrders(bs, teammates, num_teammates, .4, qfalse, BotOrder_Accompany, BotOrder_Defend);
}

/*
============================
BotCTFOrders_BothFlagsAtBase
============================
*/
void BotCTFOrders_BothFlagsAtBase(bot_state_t * bs, gentity_t ** teammates, int num_teammates)
{
	// Either aggressively grab the enemy flag or lock down the home base,
	// depending on the CTF strategy
	BotAttackDefendOrders(bs, teammates, num_teammates,
						  (bs->team_strategy & STRATEGY_AGGRESSIVE ? .6 : .3), qtrue, BotOrder_GetFlag, BotOrder_Defend);
}

/*
============
BotCTFOrders
============
*/
void BotCTFOrders(bot_state_t * bs, gentity_t ** teammates, int num_teammates)
{
	// Don't give orders until a specific event occurs
	bs->give_orders_time = 0;

	// Different orders depending on flag status
	if(bs->their_target_flag_status == FS_AT_HOME)
	{
		if(bs->our_target_flag_status == FS_AT_HOME)
			BotCTFOrders_BothFlagsAtBase(bs, teammates, num_teammates);
		else
			BotCTFOrders_EnemyFlagNotAtBase(bs, teammates, num_teammates);
	}
	else
	{
		if(bs->our_target_flag_status == FS_AT_HOME)
			BotCTFOrders_OurFlagNotAtBase(bs, teammates, num_teammates);
		else
			BotCTFOrders_BothFlagsNotAtBase(bs, teammates, num_teammates);
	}
}

#ifdef MISSIONPACK

/*
===========================
Bot1FCTFOrders_FlagAtCenter
===========================
*/
void Bot1FCTFOrders_FlagAtCenter(bot_state_t * bs, gentity_t ** teammates, int num_teammates)
{
	// Ironically, this code has the same messages and weights as the standard CTF case,
	// although players will interpret the "get the flag" orders differently
	BotCTFOrders_BothFlagsAtBase(bs, teammates, num_teammates);
}

/*
==========================
Bot1FCTFOrders_TeamHasFlag
==========================
*/
void Bot1FCTFOrders_TeamHasFlag(bot_state_t * bs, gentity_t ** teammates, int num_teammates)
{
	qboolean        favor_attacks;

	// Leave a very small contingent to defend, using most teammates to escort
	BotAttackDefendOrders(bs, teammates, num_teammates,
						  ((bs->team_strategy & STRATEGY_AGGRESSIVE) ? .8 : .7), qtrue, BotOrder_Accompany, BotOrder_Defend);
}

/*
===========================
Bot1FCTFOrders_EnemyHasFlag
===========================
*/
void Bot1FCTFOrders_EnemyHasFlag(bot_state_t * bs, gentity_t ** teammates, int num_teammates)
{
	// The enemy will come to our base anyway, so favor defense;
	// always send at least one person to soften up the enemy carrier, however
	BotAttackDefendOrders(bs, teammates, num_teammates,
						  ((bs->team_strategy & STRATEGY_AGGRESSIVE) ? .4 : .2), qtrue, BotOrder_GetFlag, BotOrder_Defend);
}

/*
==========================
Bot1FCTFOrders_DroppedFlag
==========================
*/
void Bot1FCTFOrders_DroppedFlag(bot_state_t * bs, gentity_t ** teammates, int num_teammates)
{
	// Leave some defense at home, but really try to pickup the dropped flag
	BotAttackDefendOrders(bs, teammates, num_teammates,
						  ((bs->team_strategy & STRATEGY_AGGRESSIVE) ? .7 : .5), qtrue, BotOrder_GetFlag, BotOrder_Defend);
}

/*
==============
Bot1FCTFOrders
==============
*/
void Bot1FCTFOrders(bot_state_t * bs, gentity_t ** teammates, int num_teammates)
{
	// Don't give orders until a specific event occurs
	bs->give_orders_time = 0;

	// Different orders based on flag status
	if(bs->our_target_flag_status == FS_CARRIER)
		Bot1FCTFOrders_TeamHasFlag(bs, teammates, num_teammates);
	else if(bs->their_target_flag_status == FS_CARRIER)
		Bot1FCTFOrders_EnemyHasFlag(bs, teammates, num_teammates);
	else if(bs->our_target_flag_status == FS_AT_HOME)
		Bot1FCTFOrders_FlagAtCenter(bs, teammates, num_teammates);
	else if(bs->our_target_flag_status == FS_DROPPED)
		Bot1FCTFOrders_DroppedFlag(bs, teammates, num_teammates);
}

/*
================
BotObeliskOrders
================
*/
void BotObeliskOrders(bot_state_t * bs, gentity_t ** teammates, int num_teammates)
{
	// Give new orders in 30 seconds
	bs->give_orders_time = bs->command_time + 30.0;

	// Generally send most of your team to attack, but still leave some defense
	BotAttackDefendOrders(bs, teammates, num_teammates,
						  ((bs->team_strategy & STRATEGY_AGGRESSIVE) ? .7 : .5), qtrue, BotOrder_Assault, BotOrder_Defend);
}

/*
==================
BotHarvesterOrders
==================
*/
void BotHarvesterOrders(bot_state_t * bs, gentity_t ** teammates, int num_teammates)
{
	int             i, attackers, defenders;
	int             task_counts[2];
	float           task_weights[2];

	// Give new orders in 30 seconds
	bs->give_orders_time = bs->command_time + 30.0;

	// Generally send most of your team to attack, but still leave some defense
	BotAttackDefendOrders(bs, teammates, num_teammates,
						  ((bs->team_strategy & STRATEGY_AGGRESSIVE) ? .7 : .5), qtrue, BotOrder_Harvest, BotOrder_Defend);
}
#endif

/*
===================
BotSetTeamOrderTime

Sets the time that the bot will next give team orders.
===================
*/
void BotSetTeamOrderTime(bot_state_t * bs, float time)
{
	// Only set the new time if it's sooner that the next message time
	time += bs->command_time;
	if(time < bs->give_orders_time || bs->give_orders_time == 0)
		bs->give_orders_time = time;
}

/*
=================
BotFindTeamLeader

Returns true if a team leader was found and false if not
=================
*/
qboolean BotFindTeamLeader(bot_state_t * bs)
{
	int             i;
	gentity_t      *ent;

	// If the leader is set, use that
	if(bs->leader)
		return qtrue;

	// Otherwise check all teammates for a human who claimed leadership
	for(i = 0, ent = &g_entities[0]; i < MAX_CLIENTS; i++, ent++)
	{
		// Client must be in use and not a bot
		if(!ent)
			continue;
		if(ent->r.svFlags & SVF_BOT)
			continue;

		// This player must be willing to be a team leader
		if(!could_be_leader[i])
			continue;

		// The player must be on the same team as the bot
		if(!BotSameTeam(bs, ent))
			continue;

		// Assume this player is the team leader (or at least *a* leader)
		bs->leader = ent;

		return qtrue;
	}

	// No leaders were found
	return qfalse;
}

/*
================
BotSetFlagStatus

Returns true if the flag status changed.
================
*/
qboolean BotSetFlagStatus(gentity_t * flag, gentity_t ** flag_record, int *status)
{
	// Only update if the new value differs from the recorded value
	if(*flag_record == flag)
		return qfalse;
	*flag_record = flag;

	// It's not algorithmically necessary to set these, but other pieces of
	// code becomes much cleaner when they only need to compare status defines
	if(!flag)
		*status = FS_MISSING;
	else if(flag->client)
		*status = FS_CARRIER;
	else if(flag->flags & FL_DROPPED_ITEM)
		*status = FS_DROPPED;
	else
		*status = FS_AT_HOME;
	return qtrue;
}

/*
==============
BotUpdateFlags
==============
*/
void BotUpdateFlags(bot_state_t * bs)
{
	qboolean        our_change, their_change;
	gentity_t      *our_target_flag, *their_target_flag;

	// Only update flags in flag-based game modes
	if(!(game_style & GS_FLAG))
		return;

	// Update different flag game types differently
	switch (gametype)
	{
		case GT_CTF:
			// Access the current flag entity objects
			if(BotTeam(bs) == TEAM_RED)
			{
				our_target_flag = flags[BLUE_BASE];
				their_target_flag = flags[RED_BASE];
			}
			else
			{
				our_target_flag = flags[RED_BASE];
				their_target_flag = flags[BLUE_BASE];
			}
			break;

#ifdef MISSIONPACK
		case GT_1FCTF:
			// Check who has direct access to the flag (possibly both teams)
			if(!flags[MID_BASE]->client)
			{
				our_target_flag = flags[MID_BASE];
				their_target_flag = flags[MID_BASE];
			}
			else if(flags[MID_BASE]->client->sess.sessionTeam == BotTeam(bs))
			{
				our_target_flag = flags[MID_BASE];
				their_target_flag = NULL;
			}
			else
			{
				our_target_flag = NULL;
				their_target_flag = flags[MID_BASE];
			}
			break;
#endif

		default:
			return;
	}
	// Check if either of the flags changed status
	our_change = BotSetFlagStatus(our_target_flag, &bs->our_target_flag, &bs->our_target_flag_status);
	their_change = BotSetFlagStatus(their_target_flag, &bs->their_target_flag, &bs->their_target_flag_status);

	// If neither status changed, exit
	// NOTE: The check functions aren't inlined in this conditional test because
	// both functions have side effects which must execute each frame.
	if(!our_change && !their_change)
		return;

	// If this bot picked up the flag, announce it
	if(bs->our_target_flag == bs->ent)
		BotVoiceChat(bs, -1, VOICECHAT_IHAVEFLAG);

	// Invalidate the goal sieve and resend team orders
	bs->goal_sieve_valid = qfalse;
	bs->team_orders_sent = qfalse;
}

/*
=========
BotTeamAI
=========
*/
void BotTeamAI(bot_state_t * bs)
{
	int             num_teammates;
	float           decision_time;
	gentity_t      *teammates[MAX_CLIENTS];

	// Obviously this only applies to team game modes
	if(!(game_style & GS_TEAM))
		return;

	// When in a flag-based game mode, check for changes in flag status
	BotUpdateFlags(bs);

	// If we can't find a valid team leader, consider becoming the team leader ourselves.
	if(!BotFindTeamLeader(bs))
	{
		// Prepare to either ask who the team leader is or volunteer to become the leader
		if(!bs->leader_ask_time && !bs->leader_become_time)
		{
			// Perform the next decision at this time
			decision_time = bs->command_time + 5.0 + random() * 10.0;

			// If the bot has recently entered the game, ask who the
			// leader is; otherwise just become the leader at that time
			if(bs->enter_game_time + 10.0 > bs->command_time)
				bs->leader_ask_time = decision_time;
			else
				bs->leader_become_time = decision_time;
		}

		// Check if the bot should ask who the team leader is
		if(bs->leader_ask_time && bs->leader_ask_time < bs->command_time)
		{
			// Send the request
			Bot_InitialChat(bs, "whoisleader", NULL);
			trap_BotEnterChat(bs->cs, 0, CHAT_TEAM);

			// If no responses are received in 8 to 18 seconds,
			// the bot will volunteer to be the leader
			bs->leader_ask_time = 0;
			bs->leader_become_time = bs->command_time + 8.0 + random() * 10.0;
		}

		// Check if the bot should volunteer to become the team leader
		if(bs->leader_become_time && bs->leader_become_time < bs->command_time)
		{
			Bot_InitialChat(bs, "iamleader", NULL);
			trap_BotEnterChat(bs->cs, 0, CHAT_TEAM);
			BotSayVoiceTeamOrder(bs, -1, VOICECHAT_STARTLEADER);
			bs->leader = bs->ent;
			bs->leader_become_time = 0;

			// Choose a strategy at random
			bs->team_strategy = 0;
			if(rand() & 1)
				bs->team_strategy ^= STRATEGY_AGGRESSIVE;
		}

		return;
	}
	bs->leader_ask_time = 0;
	bs->leader_become_time = 0;

	// Only the team leader runs the team AI code
	if(bs->ent != bs->leader)
	{
		// Bots that aren't the leader plan on giving new orders in 2 seconds from present.
		// They only actually give the orders if they end up becoming the team leader.
		bs->give_orders_time = bs->command_time + 2.0;
		return;
	}

	// If the number of teammates changed, be willing to give new orders
	num_teammates = BotTeammates(bs);
	if(bs->last_teammates != num_teammates)
	{
		bs->team_orders_sent = qfalse;
		bs->last_teammates = num_teammates;
	}

	// Check if this bot should give new orders.  Reasons include:
	// - A teammate joined or left
	// - Someone requested new orders
	// - The CTF flag status changed
	if(!bs->team_orders_sent)
	{
		// Give new orders in between 1.5 and 3 seconds
		BotSetTeamOrderTime(bs, 1.5 + random() * 1.5);

		// The bot has acknowledged the request for new orders
		bs->team_orders_sent = qtrue;
	}

	// Check for CTF strategy changes every so often
	if(game_style & GS_FLAG)
	{
		// Only change strategies if we haven't captured a flag in the past 4 minutes
		if(bs->last_capture_time < bs->command_time - 240.0)
		{
			// Reset this timestamp so we don't constantly check it
			bs->last_capture_time = bs->command_time;

			//randomly change the CTF strategy
			if(random() < 0.4)
			{
				bs->team_strategy ^= STRATEGY_AGGRESSIVE;
				BotSetTeamOrderTime(bs, 1.5 + random() * 1.5);
			}
		}
	}

	// Don't give orders if they are shut off for some reason
	if(!bs->give_orders_time)
		return;

	// Don't give the orders until the delay has expired
	if(bs->give_orders_time > bs->command_time)
		return;

	// NOTE: num_teammates could differ from BotTeammates(bs) because this list
	// excluded team carriers.
	num_teammates = BotSortTeammates(bs, teammates, MAX_CLIENTS);

	// Give different orders depending on the game type
	switch (gametype)
	{
		case GT_TEAM:
			BotTeamOrders(bs, teammates, num_teammates);
			break;
		case GT_CTF:
			BotCTFOrders(bs, teammates, num_teammates);
			break;
#ifdef MISSIONPACK
		case GT_1FCTF:
			Bot1FCTFOrders(bs, teammates, num_teammates);
			break;
		case GT_OBELISK:
			BotObeliskOrders(bs, teammates, num_teammates);
			break;
		case GT_HARVESTER:
			BotHarvesterOrders(bs, teammates, num_teammates);
			break;
#endif
	}
}

/*
============================
BotMatch_StartTeamLeaderShip
============================
*/
void BotMatch_StartTeamLeaderShip(bot_state_t * bs, bot_match_t * match, gentity_t * sender)
{
	char            teammate[MAX_MESSAGE_SIZE];

	// Check for a self-reflexive chat
	if(match->subtype & ST_I)
	{
		BotTeamLeaderStart(bs, sender);
		return;
	}

	// The chat is about another player
	trap_BotMatchVariable(match, TEAMMATE, teammate, sizeof(teammate));
	BotTeamLeaderStart(bs, TeammateFromName(bs, teammate));
}

/*
===========================
BotMatch_StopTeamLeaderShip
===========================
*/
void BotMatch_StopTeamLeaderShip(bot_state_t * bs, bot_match_t * match, gentity_t * sender)
{
	char            teammate[MAX_MESSAGE_SIZE];

	// Check for a self-reflexive chat
	if(match->subtype & ST_I)
	{
		BotTeamLeaderStop(bs, sender);
		return;
	}

	// The chat is about another player
	trap_BotMatchVariable(match, TEAMMATE, teammate, sizeof(teammate));
	BotTeamLeaderStop(bs, TeammateFromName(bs, teammate));
}

/*
========================
BotMatch_WhoIsTeamLeader
========================
*/
void BotMatch_WhoIsTeamLeader(bot_state_t * bs, bot_match_t * match)
{
	// The bot should tell the team if they are the team leader
	if(bs->ent == bs->leader)
		trap_EA_SayTeam(bs->client, "I'm the team leader\n");
}

/*
========================
BotMatch_WhatIsMyCommand
========================
*/
void BotMatch_WhatIsMyCommand(bot_state_t * bs, bot_match_t * match)
{
	// Only process this if the bot is the team leader
	if(bs->ent != bs->leader)
		return;

	// Acknowledge the teammate's request by sending new orders
	bs->team_orders_sent = qfalse;
}

/*
==================
BotMatch_NewLeader
==================
*/
void BotMatch_NewLeader(bot_state_t * bs, bot_match_t * match, gentity_t * sender)
{
	// Track the message's sender as the new team leader
	BotTeamLeaderStart(bs, sender);
}

/*
=======================
BotMatch_TaskPreference
=======================
*/
void BotMatch_TaskPreference(bot_state_t * bs, bot_match_t * match, gentity_t * sender)
{
	// Only process this message if the bot is the team leader
	if(bs->ent != bs->leader)
		return;

	// Update preferences accordingly
	switch (match->subtype)
	{
		case ST_DEFENDER:
			BotSetTeammatePreference(bs, sender, TASKPREF_DEFENDER);
			break;
		case ST_ATTACKER:
			BotSetTeammatePreference(bs, sender, TASKPREF_ATTACKER);
			break;
		case ST_ROAMER:
			BotSetTeammatePreference(bs, sender, TASKPREF_ROAMER);
			break;
	}
}

/*
==================
BotMatch_EnterGame
==================
*/
void BotMatch_EnterGame(bot_state_t * bs, bot_match_t * match)
{
	gentity_t      *teammate;
	char            name[MAX_NETNAME];

	// Search for enter game messages from teammates because it's
	// those players cannot be the team leader (at least right now)
	trap_BotMatchVariable(match, NETNAME, name, sizeof(name));
	teammate = TeammateFromName(bs, name);
	if(teammate)
		could_be_leader[teammate->s.number] = qfalse;
}

/*
============
BotMatch_CTF
============
*/
void BotMatch_CTF(bot_state_t * bs, bot_match_t * match)
{
	// Historically this function did part of the complicated processing
	// to determine the status of each flag.  This has been replaced
	// by a once-per-frame computation of the flag status for all bots.
	// The function stub will remain in case there is a good reason to
	// process a "Someone captured the red flag!" style message.
}

/*
=============
BotMatch_Team
=============
*/
qboolean BotMatch_Team(bot_state_t * bs, bot_match_t * match, gentity_t * sender)
{
	// Process messages to the team in general
	switch (match->type)
	{
		case MSG_CHECKPOINT:
			BotMatch_CheckPoint(bs, match, sender);
			return qtrue;
		case MSG_STARTTEAMLEADERSHIP:
			BotMatch_StartTeamLeaderShip(bs, match, sender);
			return qtrue;
		case MSG_STOPTEAMLEADERSHIP:
			BotMatch_StopTeamLeaderShip(bs, match, sender);
			return qtrue;
		case MSG_WHOISTEAMLAEDER:
			BotMatch_WhoIsTeamLeader(bs, match);
			return qtrue;
		case MSG_WHATISMYCOMMAND:
			BotMatch_WhatIsMyCommand(bs, match);
			return qtrue;
		case MSG_ENTERGAME:
			BotMatch_EnterGame(bs, match);
			return qtrue;
		case MSG_NEWLEADER:
			BotMatch_NewLeader(bs, match, sender);
			return qtrue;
		case MSG_CTF:
			BotMatch_CTF(bs, match);
			return qtrue;
		case MSG_TASKPREFERENCE:
			BotMatch_TaskPreference(bs, match, sender);
			return qtrue;
	}

	return qfalse;
}
