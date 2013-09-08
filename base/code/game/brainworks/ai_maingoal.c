// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_maingoal.c
 *
 * Functions the bot uses to select a primary goal for this frame
 *****************************************************************************/

#include "ai_main.h"
#include "ai_vars.h"
#include "ai_maingoal.h"

#include "ai_chat.h"
#include "ai_client.h"
#include "ai_command.h"
#include "ai_dodge.h"
#include "ai_entity.h"
#include "ai_goal.h"
#include "ai_item.h"
#include "ai_level.h"
#include "ai_order.h"
#include "ai_path.h"
#include "ai_self.h"
#include "ai_team.h"
#include "ai_visible.h"
#include "ai_waypoint.h"


// For the voice chats
#include "../../ui/menudef.h"


/*
==============
BotSetGoalType
==============
*/
void BotSetGoalType(bot_state_t * bs, int type)
{
#ifdef DEBUG_AI
	const char     *action;
#endif

	// Don't set anything if nothing changed
	if((bs->goal_type == type) &&
	   (bs->goal_entity == bs->goal.entitynum) && (bs->goal_area == bs->goal.areanum || bs->goal.entitynum >= 0))
		return;

	// Record the distinguishing information
	bs->goal_type = type;
	bs->goal_entity = bs->goal.entitynum;
	bs->goal_area = bs->goal.areanum;

	// Update the goal value
	//
	// FIXME: Is this value still used and needed?  Even if it's not used now,
	// should it stay in case it's useful in the future?
	switch (type)
	{
		case GOAL_AIR:
			bs->goal_value = GOAL_VALUE_CRITICAL;
			break;
		case GOAL_LEAD:
			bs->goal_value = GOAL_VALUE_MEDIUM;
			break;
		case GOAL_CAPTURE:
			bs->goal_value = GOAL_VALUE_HIGH;
			break;
		case GOAL_CAPTURE_WAIT:
			bs->goal_value = GOAL_VALUE_LOW;
			break;
		case GOAL_ATTACK_CHOICE:
			bs->goal_value = GOAL_VALUE_LOW;
			break;
		case GOAL_ATTACK_ORDER:
			bs->goal_value = GOAL_VALUE_MEDIUM;
			break;
		case GOAL_HELP_CHOICE:
			bs->goal_value = GOAL_VALUE_LOW;
			break;
		case GOAL_HELP_ORDER:
			bs->goal_value = GOAL_VALUE_MEDIUM;
			break;
		case GOAL_ACCOMPANY_CHOICE:
			bs->goal_value = GOAL_VALUE_MEDIUM;
			break;
		case GOAL_ACCOMPANY_ORDER:
			bs->goal_value = GOAL_VALUE_HIGH;
			break;
		case GOAL_DEFEND_CHOICE:
			bs->goal_value = GOAL_VALUE_LOW;
			break;
		case GOAL_DEFEND_ORDER:
			bs->goal_value = GOAL_VALUE_LOW;
			break;
		case GOAL_PATROL:
			bs->goal_value = GOAL_VALUE_VERYLOW;
			break;
		case GOAL_INSPECT_CHOICE:
			bs->goal_value = GOAL_VALUE_VERYLOW;
			break;
		case GOAL_INSPECT_ORDER:
			bs->goal_value = GOAL_VALUE_MEDIUM;
			break;
		case GOAL_CAMP_CHOICE:
			bs->goal_value = GOAL_VALUE_VERYLOW;
			break;
		case GOAL_CAMP_ORDER:
			bs->goal_value = GOAL_VALUE_VERYLOW;
			break;
		case GOAL_GETFLAG_CHOICE:
			bs->goal_value = GOAL_VALUE_HIGH;
			break;
		case GOAL_GETFLAG_ORDER:
			bs->goal_value = GOAL_VALUE_HIGH;
			break;
		case GOAL_RETURNFLAG_CHOICE:
			bs->goal_value = GOAL_VALUE_CRITICAL;
			break;
		case GOAL_RETURNFLAG_ORDER:
			bs->goal_value = GOAL_VALUE_CRITICAL;
			break;
		case GOAL_ASSAULT_CHOICE:
			bs->goal_value = GOAL_VALUE_LOW;
			break;
		case GOAL_ASSAULT_ORDER:
			bs->goal_value = GOAL_VALUE_LOW;
			break;
		case GOAL_HARVEST_CHOICE:
			bs->goal_value = GOAL_VALUE_LOW;
			break;
		case GOAL_HARVEST_ORDER:
			bs->goal_value = GOAL_VALUE_LOW;
			break;

		default:
		case GOAL_NONE:
			bs->goal_value = GOAL_VALUE_NONE;
			break;
	}

	// Reject orders if the bot chose to do something else interesting
	// NOTE: This list intentionally lacks GOAL_ATTACK_CHOICE.  Just because the
	// bot chose to attack someone doesn't mean it's bailing on the original order.
	switch (type)
	{
		case GOAL_CAPTURE:
		case GOAL_CAPTURE_WAIT:
		case GOAL_HELP_CHOICE:
		case GOAL_ACCOMPANY_CHOICE:
		case GOAL_INSPECT_CHOICE:
		case GOAL_CAMP_CHOICE:
		case GOAL_GETFLAG_CHOICE:
		case GOAL_RETURNFLAG_CHOICE:
		case GOAL_ASSAULT_CHOICE:
		case GOAL_HARVEST_CHOICE:
			BotOrderAnnounceReset(bs, "reject_order_choice", bs->order_requester, NULL);
			break;
	}

#ifdef DEBUG_AI
	// Only announce goal changes when debugging them
	if(!(bs->debug_flags & BOT_DEBUG_INFO_GOAL))
		return;

	// Name the bot's current goal type
	switch (type)
	{
		case GOAL_NONE:
			action = "Nothing";
			break;
		case GOAL_AIR:
			action = "Air";
			break;
		case GOAL_LEAD:
			action = "Lead";
			break;
		case GOAL_CAPTURE:
			action = "Capture";
			break;
		case GOAL_CAPTURE_WAIT:
			action = "Waiting to Capture";
			break;
		case GOAL_ATTACK_CHOICE:
			action = "Attack choice";
			break;
		case GOAL_ATTACK_ORDER:
			action = "Attack order";
			break;
		case GOAL_HELP_CHOICE:
			action = "Help choice";
			break;
		case GOAL_HELP_ORDER:
			action = "Help order";
			break;
		case GOAL_ACCOMPANY_CHOICE:
			action = "Accompany choice";
			break;
		case GOAL_ACCOMPANY_ORDER:
			action = "Accompany order";
			break;
		case GOAL_DEFEND_CHOICE:
			action = "Defend choice";
			break;
		case GOAL_DEFEND_ORDER:
			action = "Defend order";
			break;
		case GOAL_PATROL:
			action = "Patrol";
			break;
		case GOAL_INSPECT_CHOICE:
			action = "Inspect choice";
			break;
		case GOAL_INSPECT_ORDER:
			action = "Inspect order";
			break;
		case GOAL_CAMP_CHOICE:
			action = "Camp choice";
			break;
		case GOAL_CAMP_ORDER:
			action = "Camp order";
			break;
		case GOAL_GETFLAG_CHOICE:
			action = "Get flag choice";
			break;
		case GOAL_GETFLAG_ORDER:
			action = "Get flag order";
			break;
		case GOAL_RETURNFLAG_CHOICE:
			action = "Return flag choice";
			break;
		case GOAL_RETURNFLAG_ORDER:
			action = "Return flag order";
			break;
		case GOAL_ASSAULT_CHOICE:
			action = "Assault choice";
			break;
		case GOAL_ASSAULT_ORDER:
			action = "Assault order";
			break;
		case GOAL_HARVEST_CHOICE:
			action = "Harvest choice";
			break;
		case GOAL_HARVEST_ORDER:
			action = "Harvest order";
			break;

		default:
			action = "UNKNOWN";
			break;
	}

	// State what the bot is doing
	BotAI_Print(PRT_MESSAGE, "%s: Main Goal: %s: %s\n", EntityNameFast(bs->ent), action, GoalNameFast(&bs->goal));
#endif
}

/*
============
BotGoalReset
============
*/
void BotGoalReset(bot_state_t * bs)
{
	GoalReset(&bs->goal);
	BotPathReset(&bs->main_path);
	BotSetGoalType(bs, GOAL_NONE);
}

/*
================
BotSetTeamStatus
================
*/
void BotSetTeamStatus(bot_state_t * bs, int task)
{
#ifdef MISSIONPACK
	// Delta compress task updates
	if(task == bs->team_task)
		return;

	bs->team_task = task;
	BotSetUserInfo(bs, "teamtask", va("%d", task));
#endif
}

/*
================
BotGoalAirChoice
================
*/
int BotGoalAirChoice(bot_state_t * bs, bot_goal_t * goal)
{
	vec3_t          end, mins = { -15, -15, -2 }, maxs =
	{
	15, 15, 2};
	trace_t         trace;
	bot_goal_t     *subgoal;

	// Don't look for air if the bot has had air in the past few seconds
	if(bs->command_time < bs->last_air_time + 6.0)
		return GOAL_NONE;

	// Get a new air goal if the bot has none or the old goal is more than a second old
	if((!bs->air_goal.areanum) || (bs->air_goal_time + 1.0 < bs->command_time))
	{
		// Find the ceiling above the bot
		VectorCopy(bs->now.origin, end);
		end[2] += 1024;
		trap_Trace(&trace, bs->now.origin, mins, maxs, end, bs->entitynum, MASK_DEADSOLID);

		// Now look back downwards until finding the water
		VectorCopy(trace.endpos, end);
		trap_Trace(&trace, end, mins, maxs, bs->now.origin, bs->entitynum, MASK_WATER);

		// If that surface couldn't be found, fail
		if(trace.fraction <= 0)
			return GOAL_NONE;

		// Make a goal at that endpoint
		GoalLocation(&bs->air_goal, trace.endpos);
		bs->air_goal_time = bs->command_time;
	}

	// Use the air goal as the current goal location if possible
	if(!BotPathPlan(bs, &bs->main_path, &bs->air_goal, goal))
		return GOAL_NONE;
	return GOAL_AIR;
}

/*
================
BotGoalLeadOrder
================
*/
int BotGoalLeadOrder(bot_state_t * bs, bot_goal_t * goal)
{
	qboolean        follow_msg;
	int             goal_type;

	// Don't lead if there's no valid teammate to lead
	// (But in that case, why is this in the sieve?...)
	if(!bs->lead_teammate)
		return GOAL_NONE;

	// If the lead time runs out or the teammate changed teams, stop leading
	if((bs->lead_time < bs->command_time) || !BotSameTeam(bs, bs->lead_teammate))
	{
		BotLeadReset(bs);
		return GOAL_NONE;
	}

	// Check if the bot wants to announce a "follow me" command-- always gives one
	// at the beginning but only gives it later if the target has trouble following.
	follow_msg = bs->lead_announce;

	// Check if the teammate is visible
	if(BotEntityVisibleFast(bs, bs->lead_teammate))
		bs->lead_visible_time = bs->command_time;

	// If the teammate hasn't been seen in the past three seconds, go look for them
	if(bs->lead_visible_time < bs->command_time - 3.0)
	{
		// Tell the target to follow the bot
		follow_msg = qtrue;

		// The bot should go back to the team mate if possible
		GoalEntity(goal, bs->lead_teammate);
		goal_type = GOAL_LEAD;
	}

	// Pester the teammate if they aren't following well enough
	else if(DistanceSquared(bs->now.origin, bs->lead_teammate->r.currentOrigin) > Square(512.0))
	{
		// Tell the target to follow the bot
		follow_msg = qtrue;

		// Tell the aiming engine to look at the bot if the target is visible now
		if(bs->lead_visible_time == bs->command_time)
			bs->face_entity = bs->lead_teammate;

		// Stand still
		GoalReset(goal);
		goal_type = GOAL_LEAD;
	}

	// Continue towards the real goal
	else
	{
		goal_type = GOAL_NONE;
	}

	// If a goal was selected, make sure the bot can reach it
	if(goal_type && !BotPathPlan(bs, &bs->main_path, goal, goal))
		goal_type = GOAL_NONE;

	// Give the "follow me" message if requested and it's time to do so
	if(follow_msg && bs->lead_message_time < bs->command_time)
	{
		// Send the message
		Bot_InitialChat(bs, "followme", SimplifyName(EntityNameFast(bs->lead_teammate)), NULL);
		trap_BotEnterChat(bs->cs, bs->lead_teammate->s.number, CHAT_TELL);

		// The bot doesn't need to force an announcement because it's done so once
		bs->lead_announce = qfalse;

		// Don't give this message again for a while
		bs->lead_message_time = bs->command_time + 20.0;
	}

	// Return the type of goal used
	return goal_type;
}

/*
===========
BotGoToBase
===========
*/
qboolean BotGoToBase(bot_state_t * bs, bot_goal_t * goal, int base)
{
	// Make sure a valid base was specified
	if(base < 0)
		return qfalse;

	// Move towards the base if possible
	if(!BotPathPlan(bs, &bs->main_path, &bases[base], goal))
		return qfalse;

	// Set the appropriate team task
	BotSetTeamStatus(bs, (base == BotTeamBase(bs) ? TEAMTASK_DEFENSE : TEAMTASK_OFFENSE));
	return qtrue;
}

/*
====================
BotGoalCaptureChoice

NOTE: There is no order equivalent to this goal case because
it's completely context sensitive.  If you have a capturable
item, you should rush to the base to capture it.  If not,
there's no point in rushing.  Hence there's no need for an
order case for rushing to the base.
====================
*/
int BotGoalCaptureChoice(bot_state_t * bs, bot_goal_t * goal)
{
	int             capture_base;

	// If not carrying flag/skulls/whatever, don't rush to the base
	if(!BotIsCarrier(bs))
		return GOAL_NONE;

	// In normal capture the flag, don't capture if the bot has no teammates
	// and the flag the enemy team wants (our flag) isn't at the base.
	if((gametype == GT_CTF) && (!BotTeammates(bs)) && (bs->their_target_flag_status != FS_AT_HOME))
		return GOAL_NONE;

	// Determine where to take the capturable object
	capture_base = BotCaptureBase(bs);
	if(capture_base < 0)
		return GOAL_NONE;

#ifdef MISSIONPACK
	// In harvester, the bot might want to collect more heads before capturing
	if(gametype == GT_HARVESTER)
	{
		int             num_heads;
		float           time_to_base, time_to_mid, time_from_mid_to_base, head_respawn_rate;

		// The bot decides whether to capture its carried heads using this algorithm:
		//
		// Estimate the points gained per second capturing now:
		//   - Current Heads / Travel Time to Enemy Base
		//
		// Estimate the points gained per second by waiting for another head:
		//   - Current Heads + 1 / (Travel Time to Level Center +
		//                          (Estimated Head Spawn Time / 2) +
		//                          Travel Time to Enemy Base from Center)
		//
		// If capturing now provides more points per second than capturing later, the
		// bot leaves to capture heads.

		// Lookup how many heads the bot currently has
		num_heads = bs->ent->s.generic1;

		// Look up different travel times
		time_to_base = EntityGoalTravelTime(bs->ent, &bases[capture_base], bs->travel_flags);
		time_to_mid = EntityGoalTravelTime(bs->ent, &bases[MID_BASE], bs->travel_flags);
		time_from_mid_to_base = LevelBaseTravelTime(MID_BASE, capture_base);

		// If any of these travel times are invalid, do not try to capture
		if(time_to_base < 0 || time_to_mid < 0 || time_from_mid_to_base < 0)
			return GOAL_NONE;

		// Estimate how much time elapses between skull dispenses
		//
		// FIXME: Estimate this data value from the enemy team's death rate instead
		// of using this constant value.
		head_respawn_rate = 10.0;

		// If collecting heads is more efficient, do not go to capture
		// NOTE: Cross-multiplying the sides of this inequality removes two divides.
		if((num_heads) * (time_to_mid + head_respawn_rate * .5 + time_from_mid_to_base) < (num_heads + 1) * (time_to_base))
			return GOAL_NONE;
	}
#endif

	// Try rushing to the appropriate base to capture this object
	if(!BotGoToBase(bs, goal, capture_base))
		return GOAL_NONE;

	// Manually reset any "get the flag" or "harvest" order the bot received
	// NOTE: This is necessary because setting the GOAL_CAPTURE order type will cause
	// the bot to reject their previous order, but players would get confused if the
	// bot "rejected" the "grab the flag" order because the bot already grabbed the
	// flag.  Of course, a rejection message is still needed for every other order type.
	if(bs->order_type == ORDER_GETFLAG || bs->order_type == ORDER_HARVEST)
		BotOrderReset(bs);

	// In normal CTF, if the home flag is missing and the bot is close enough, treat
	// this as a "waiting to capture" goal instead of "actively trying to capture.
	if((gametype == GT_CTF) &&
	   (bs->their_target_flag_status != FS_AT_HOME) && (DistanceSquared(bs->now.origin, goal->origin) < Square(1024.0)))
		return GOAL_CAPTURE_WAIT;

	// Rush back to the base with as few distractions as possible
	return GOAL_CAPTURE;
}

/*
===============
BotAttackEntity
===============
*/
qboolean BotAttackEntity(bot_state_t * bs, bot_goal_t * goal, gentity_t * ent)
{
	// Verify that the target is a valid target
	if(!BotEnemyTeam(bs, ent))
		return qfalse;

	// Assume the bot knows this entity's location
	if(!GoalEntity(goal, ent))
		return qfalse;

	// Make sure the bot can plan a path to the goal
	if(!BotPathPlan(bs, &bs->main_path, goal, goal))
		return qfalse;

	BotSetTeamStatus(bs, TEAMTASK_PATROL);
	return qtrue;
}

/*
===================
BotGoalAttackChoice
===================
*/
int BotGoalAttackChoice(bot_state_t * bs, bot_goal_t * goal)
{
	// This value was pre-computed when the bot scanned its surroundings for enemies
	if(!bs->goal_enemy)
		return GOAL_NONE;

	// The enemy is either in the awareness engine, so the bot either heard or saw
	// the, possibly both-- It's okay for the bot to know the enemy's exact location.
	if(!BotAttackEntity(bs, goal, bs->goal_enemy))
		return GOAL_NONE;

	return GOAL_ATTACK_CHOICE;
}

/*
==================
BotGoalAttackOrder
==================
*/
int BotGoalAttackOrder(bot_state_t * bs, bot_goal_t * goal)
{
	gentity_t      *ent;

	// Make sure the enemy exists
	if(!bs->order_enemy)
		return GOAL_NONE;

	// Check if this enemy is dead
	if(bs->killed_player == bs->order_enemy || bs->order_enemy->client->ps.pm_type == PM_DEAD)
	{
		BotOrderAnnounceReset(bs, "kill_done", bs->order_requester, SimplifyName(EntityNameFast(bs->order_enemy)));
		return GOAL_NONE;
	}

	// Check for goal timeout
	if(bs->order_time < bs->command_time)
	{
		BotOrderReset(bs);
		return GOAL_NONE;
	}

	// Cheat and directly move towards that player-- we can rationalize this by
	// assuming that at least ONE of our teammates has seen this player recently
	// and would have told us where to find our target.
	if(!BotAttackEntity(bs, goal, bs->order_enemy))
	{
		BotOrderAnnounceReset(bs, "reject_order_unable", bs->order_requester, NULL);
		return GOAL_NONE;
	}

	// Possibly announce start of the attack goal
	if(BotOrderShouldAnnounce(bs))
		BotOrderAnnounceStart(bs, "kill_start", bs->order_requester, NULL, VOICECHAT_YES);

	return GOAL_ATTACK_ORDER;
}

/*
=============
BotHelpPlayer
=============
*/
qboolean BotHelpPlayer(bot_state_t * bs, bot_goal_t * goal)
{
	// Verify that the teammate is still a valid choice
	if(!BotSameTeam(bs, bs->help_teammate))
		return qfalse;

	// Don't help teammates the bot hasn't seen in a while
	if(bs->command_time > bs->help_notseen + 5.0)
		return qfalse;

	// Check if that player is visible
	if(BotEntityVisibleFast(bs, bs->help_teammate))
	{
		// If close, don't bother getting any closer-- this goal is done
		if(DistanceSquared(bs->help_teammate->r.currentOrigin, bs->now.origin) < Square(100))
			return qfalse;
	}
	else
	{
		// Record the last time the bot noticed that player wasn't visible
		bs->help_notseen = bs->command_time;
	}

	// Move towards that player
	GoalEntity(goal, bs->help_teammate);

	// Make sure the bot can plan a path to the goal
	if(!BotPathPlan(bs, &bs->main_path, goal, goal))
		return qfalse;

	BotSetTeamStatus(bs, TEAMTASK_PATROL);
	return qtrue;
}

/*
=================
BotGoalHelpChoice

NOTE: This function is not linked in
anywhere because it's not implemented.
=================
*/
int BotGoalHelpChoice(bot_state_t * bs, bot_goal_t * goal)
{
	// FIXME: It might be nice to have this feature, especially for team deathmatch
	return GOAL_NONE;
}

/*
================
BotGoalHelpOrder
================
*/
int BotGoalHelpOrder(bot_state_t * bs, bot_goal_t * goal)
{
	// Check if the bot can help the requested teammate
	if(!bs->help_teammate || bs->order_time < bs->command_time || !BotHelpPlayer(bs, goal))
	{
		// The order is completed-- continue checking for other goals.
		// Note the lack of goal completion announcement as with other orders.
		BotOrderReset(bs);
		return GOAL_NONE;
	}

	// Possibly announce start of order acceptance
	if(BotOrderShouldAnnounce(bs))
		BotOrderAnnounceStart(bs, "help_start", bs->order_requester,
							  SimplifyName(EntityNameFast(bs->help_teammate)), VOICECHAT_YES);

	return GOAL_HELP_ORDER;
}

/*
==================
BotAccompanyPlayer
==================
*/
qboolean BotAccompanyPlayer(bot_state_t * bs, bot_goal_t * goal)
{
	gentity_t      *ent;

	// Verify that the teammate is still a valid choice
	if(!BotSameTeam(bs, bs->accompany_teammate))
		return qfalse;

	// Check if the bot notices the teammate now
	if(BotEntityVisibleFast(bs, bs->accompany_teammate))
		bs->accompany_seen = bs->command_time;

	// Don't accompany teammates the bot hasn't seen recently
	if(bs->command_time > bs->accompany_seen + 15.0)
		return qfalse;

	// Make sure the bot can plan a path to the teammate
	GoalEntity(goal, bs->accompany_teammate);
	if(!BotPathPlan(bs, &bs->main_path, goal, goal))
		return qfalse;

	// Update the bot's task
	BotSetTeamStatus(bs, (EntityIsCarrier(bs->accompany_teammate) ? TEAMTASK_ESCORT : TEAMTASK_FOLLOW));
	return qtrue;
}

/*
======================
BotGoalAccompanyChoice
======================
*/
int BotGoalAccompanyChoice(bot_state_t * bs, bot_goal_t * goal)
{
	qboolean        announce;

	// Never accompany if the bot is a carrier (to prevent two carriers accompanying
	// each other in harvester, or from a flag carrier accidently accompanying itself).
	if(EntityIsCarrier(bs->ent))
		return GOAL_NONE;

	// If the bot is accompanying someone, make sure they still needs accompaniment
	// FIXME: Should bots accompanying carriers stop accompaniment once the carrier
	// gets close to the base?  Should their goal value decrease at least?
	if(bs->accompany_teammate && !EntityIsCarrier(bs->accompany_teammate))
		bs->accompany_teammate = NULL;

	// If the bot isn't accompanying anyone, consider doing so
	announce = qfalse;
	if(!bs->accompany_teammate)
	{
		// Only accompany team carriers
		if(!bs->team_carrier)
			return GOAL_NONE;

		// Start following them-- pretend the bot has seen the teammate
		// (although they still know where the teammate must be)
		bs->accompany_teammate = bs->team_carrier;
		bs->accompany_seen = bs->command_time;
		bs->announce_arrive = qfalse;
		bs->formation_dist = 3.5 * 32;	//3.5 meter

		// Tell them that we're accompanying them if things work out
		announce = qtrue;
	}

	// Try to accompany that player
	if(!BotAccompanyPlayer(bs, goal))
	{
		bs->accompany_teammate = NULL;
		return GOAL_NONE;
	}

	// Things worked out (at least for a frame), so give them the message
	if(announce)
		BotVoiceChat(bs, bs->accompany_teammate->s.number, VOICECHAT_ONFOLLOW);

	return GOAL_ACCOMPANY_CHOICE;
}

/*
=====================
BotGoalAccompanyOrder
=====================
*/
int BotGoalAccompanyOrder(bot_state_t * bs, bot_goal_t * goal)
{
	// The order is done if there is no teammate to accompany
	if(!bs->accompany_teammate)
	{
		BotOrderReset(bs);
		return GOAL_NONE;
	}

	// Check if the order timed out
	if(bs->order_time < bs->command_time)
	{
		BotOrderAnnounceReset(bs, "accompany_stop", bs->accompany_teammate, NULL);
		return GOAL_NONE;
	}

	// Try to use the goal
	if(!BotAccompanyPlayer(bs, goal))
	{
		// The bot must not have seen the target for a while
		BotOrderAnnounceReset(bs, "accompany_cannotfind", bs->accompany_teammate, NULL);
		return GOAL_NONE;
	}

	// Possibly announce start of order acceptance
	if(BotOrderShouldAnnounce(bs))
		BotOrderAnnounceStart(bs, "accompany_start", bs->order_requester,
							  SimplifyName(EntityNameFast(bs->accompany_teammate)), VOICECHAT_YES);

	// If the bot has arrived and it hasn't announced this yet, do so.
	// Note that the bot will stand stand still (no goal) when it's close enough.
	if(!goal->areanum && bs->announce_arrive)
	{
		BotCommandAction(bs, ACTION_GESTURE);
		Bot_InitialChat(bs, "accompany_arrive", SimplifyName(EntityNameFast(bs->accompany_teammate)), NULL);
		trap_BotEnterChat(bs->cs, bs->accompany_teammate->s.number, CHAT_TELL);
		bs->announce_arrive = qfalse;
	}

	return GOAL_ACCOMPANY_ORDER;
}

/*
=================
BotDefendLocation

The type input is the type of goal to use if the inputted
defense location is used as the main goal.

NOTE: This function actually returns a selected
goal type, not a boolean like most other goal
setup functions.
=================
*/
int BotDefendLocation(bot_state_t * bs, bot_goal_t * goal, bot_goal_t * location, int type)
{
	int             attack;

	// Check for invalid location goals
	if(!location->areanum)
		return GOAL_NONE;

	// The bot might move towards enemies within the defense area
	if(bs->goal_enemy && DistanceSquared(bs->goal_enemy->r.currentOrigin, location->origin) < Square(1280.0))
	{
		// Try to attack nearby enemies
		attack = BotGoalAttackChoice(bs, goal);
		if(attack)
		{
			BotSetTeamStatus(bs, TEAMTASK_DEFENSE);
			return attack;
		}
	}

	// Fail if no path can be planned back to the defense location
	if(!BotPathPlan(bs, &bs->main_path, location, goal))
		return GOAL_NONE;

	// Inform the teammates that the bot is on defense
	BotSetTeamStatus(bs, TEAMTASK_DEFENSE);
	return type;
}

/*
===================
BotGoalDefendChoice
===================
*/
int BotGoalDefendChoice(bot_state_t * bs, bot_goal_t * goal)
{
	int             base, type;

	// Try defending the bot's base
	base = BotTeamBase(bs);
	if(base < 0)
		type = GOAL_NONE;
	else
		type = BotDefendLocation(bs, goal, &bases[base], GOAL_DEFEND_CHOICE);

	// Don't defend if doing so is impossible
	if(!type)
		return GOAL_NONE;

	return type;
}

/*
==================
BotGoalDefendOrder
==================
*/
int BotGoalDefendOrder(bot_state_t * bs, bot_goal_t * goal)
{
	int             type;

	// If the order hasn't timed out, try to defend the requested area
	if(bs->order_time < bs->command_time)
		type = GOAL_NONE;
	else
		type = BotDefendLocation(bs, goal, &bs->defend_goal, GOAL_DEFEND_ORDER);

	// Stop defending if requested
	if(!type)
	{
		BotOrderAnnounceReset(bs, "defend_stop", NULL, GoalNameFast(&bs->defend_goal));
		return GOAL_NONE;
	}

	// Possibly announce start of order acceptance
	if(BotOrderShouldAnnounce(bs))
		BotOrderAnnounceStart(bs, "defend_start", bs->order_requester, GoalNameFast(&bs->defend_goal), VOICECHAT_YES);
	return type;
}

/*
==================
BotGoalPatrolOrder
==================
*/
int BotGoalPatrolOrder(bot_state_t * bs, bot_goal_t * goal)
{
	int             type;
	char            waypoint_names[MAX_MESSAGE_SIZE];
	bot_waypoint_t *wp;

	// Only patrol if the bot has patrol points and hasn't run out of time
	if(!bs->next_patrol || bs->order_time < bs->command_time)
	{
		BotOrderAnnounceReset(bs, "patrol_stop", bs->order_requester, NULL);
		return GOAL_NONE;
	}

	// Announce the start of patrolling if necessary
	if(BotOrderShouldAnnounce(bs))
	{
		// Create the patrol description string
		waypoint_names[0] = '\0';
		for(wp = bs->patrol; wp != NULL; wp = wp->next)
		{
			Q_strcat(waypoint_names, sizeof(waypoint_names), wp->name);
			if(wp->next)
				Q_strcat(waypoint_names, sizeof(waypoint_names), " to ");
		}

		// Announce the start of patrolling
		BotOrderAnnounceStart(bs, "patrol_start", bs->order_requester, waypoint_names, VOICECHAT_YES);
	}

	// Since the whole point of patrolling is to find enemies, attack them if possible
	type = BotGoalAttackChoice(bs, goal);
	if(type)
	{
		// Tell the bot's teammates that the bot is patrolling
		BotSetTeamStatus(bs, TEAMTASK_PATROL);
		return type;
	}

	// Move to the next patrol waypoint
	if(!BotPathPlan(bs, &bs->main_path, BotNextPatrolPoint(bs), goal))
		return GOAL_NONE;

	// Tell the bot's teammates that the bot is patrolling
	BotSetTeamStatus(bs, TEAMTASK_PATROL);
	return GOAL_PATROL;
}

/*
====================
BotChooseItemInspect

Finds a valuable item for the bot to inspect for nearby
enemies (other than the last selected inspection entity)
and uses it for bs->inspect_goal.  Returns true if the
bot successfully found and chose an item to inspect and false
if not.  In the success case, the this code also sets up
the appropriate timers for the goal.

If "exclude" is non-NULL, that item cluster will definitely
not be chosen as the inspection goal.
====================
*/
qboolean BotChooseItemInspect(bot_state_t * bs, item_cluster_t * exclude)
{
	int             i, num_items;
	float           total_value;
	item_cluster_t *items[MAX_REGIONS], **excluded_entry;
	float           value;

	// Create a local copy of the important items
	memcpy(items, important_items, sizeof(item_cluster_t *) * num_important_items);
	num_items = num_important_items;
	total_value = important_item_total_value;

	// Check if the excluded cluster is in the list
	if(exclude)
	{
		// Find the array entry that includes the excluded pointer, if any
		excluded_entry = bsearch(&exclude, items, num_items, sizeof(item_cluster_t *), CompareVoidList);

		// Overwrite the excluded entry with the last list entry, if the entry was found
		if(excluded_entry)
		{
			total_value -= (*excluded_entry)->value;
			*excluded_entry = items[--num_items];
		}
	}

	// Fail if no valuable items could be found
	if(!num_items || total_value <= 0.0)
	{
		bs->inspect_cluster = NULL;
		return qfalse;
	}

	// Select a random value from the maximum total value
	value = random() * total_value;

	// Find the item cluster associated with that value
	for(i = num_items - 1; (i) && (items[i]->value < value); i--)
	{
		value -= items[i]->value;
	}

	// Create a goal for the cluster's center item if possible
	if(!GoalEntity(&bs->inspect_goal, items[i]->center->ent))
	{
		bs->inspect_cluster = NULL;
		return qfalse;
	}

	// Finish setting up the selected cluster and put a sanity timeout on it
	bs->inspect_cluster = items[i];
	bs->inspect_time_end = bs->command_time + 30.0;
	return qtrue;
}

/*
==============
BotItemInspect
==============
*/
qboolean BotItemInspect(bot_state_t * bs, bot_goal_t * goal, bot_goal_t * inspect)
{
	// Check for invalid inspection goals
	if(!inspect->areanum)
		return qfalse;

	// Move towards the item if possible
	if(!BotPathPlan(bs, &bs->main_path, inspect, goal))
		return qfalse;

	// Item inspections (and pickups) fall under the patrol task catagory
	BotSetTeamStatus(bs, TEAMTASK_PATROL);
	return qtrue;
}

/*
=================
BotGoalItemChoice
=================
*/
int BotGoalItemChoice(bot_state_t * bs, bot_goal_t * goal)
{
	float           end_time;

	// Test if the bot had an inspection goal it was recently working on
	if((bs->inspect_cluster) && (bs->command_time - 3.0 < bs->inspect_time_last))
	{
		// Pick a different item to inspect if the inspection timer has exired
		if(bs->inspect_time_end <= bs->command_time)
		{
			// Fail if no imporant inspection item other than the last cluster could be found
			if(!BotChooseItemInspect(bs, bs->inspect_cluster))
				return GOAL_NONE;
		}

		// Accelerate the inspection time out timer if the bot has reached the item
		else if(DistanceSquared(bs->inspect_cluster->center->ent->r.currentOrigin, bs->now.origin) < Square(128.0))
		{
			// Only change the timer if it would make it timeout faster
			end_time = bs->command_time + 0.5;
			if(bs->inspect_time_end > end_time)
				bs->inspect_time_end = end_time;
		}
	}
	else
	{
		// Pick an important item to inspect if possible
		if(!BotChooseItemInspect(bs, NULL))
			return GOAL_NONE;
	}

	// Use the bot's item inspection goal if possible
	if(!BotItemInspect(bs, goal, &bs->inspect_goal))
		return GOAL_NONE;

	// Remember that the bot chose to inspect an item at this time
	bs->inspect_time_last = bs->command_time;
	return GOAL_INSPECT_CHOICE;
}

/*
================
BotGoalItemOrder
================
*/
int BotGoalItemOrder(bot_state_t * bs, bot_goal_t * goal)
{
	vec3_t          dir;
	gentity_t      *item;

	// Stop after some time or if the goal is now invalid
	if((bs->order_time < bs->command_time) || !BotItemInspect(bs, goal, &bs->inspect_goal))
		return GOAL_NONE;

	// Finish the order if the bot reached the item pickup location
	if(trap_BotTouchingGoal(bs->now.origin, &bs->inspect_goal))
	{
		BotOrderAnnounceReset(bs, "getitem_gotit", bs->order_requester, GoalNameFast(&bs->inspect_goal));
		return GOAL_NONE;
	}

	// Terminate the order if the item hasn't respawned and is visible
	item = &g_entities[bs->inspect_goal.entitynum];
	if(!(item->r.contents & CONTENTS_TRIGGER) &&
	   BotTargetInFieldOfVision(bs, item->r.currentOrigin, 90) && BotEntityVisibleFast(bs, item))
	{
		BotOrderAnnounceReset(bs, "getitem_notthere", bs->order_requester, GoalNameFast(&bs->inspect_goal));
		return GOAL_NONE;
	}

	// Possibly announce start of item pickup goal
	if(BotOrderShouldAnnounce(bs))
		BotOrderAnnounceStart(bs, "getitem_start", bs->order_requester, GoalNameFast(&bs->inspect_goal), VOICECHAT_YES);

	return GOAL_INSPECT_ORDER;
}

/*
=====================
BotChooseCampLocation

Choose a camping location the bot can reach in
the specified number of seconds.  If such a
location can be found, it is stored in bs->camp_goal
and true is returned.  Returns false otherwise.

NOTE: This function will always return false
because of an unknown bug.  See internal
comments for more information.
=====================
*/
qboolean BotChooseCampLocation(bot_state_t * bs, float max_time)
{
	int             camp_spot_id;
	float           time, best_time;
	bot_goal_t      goal;

	// Find the closest camp spot
	//
	// FIXME: trap_BotGetNextcampSpotGoal() never returns a valid goal.
	// Perhaps this is because all AAS library updates were removed except
	// the initial call for setup purposes.  Or maybe for some other reason.
	// It's not a huge deal because bots should never camp locations, only
	// items.  But this function would be useful for games without in-game
	// resources to pickup, such as Counterstrike and Rocket Arena.
	best_time = max_time;
	for(camp_spot_id = trap_BotGetNextCampSpotGoal(0, &goal);
		camp_spot_id; camp_spot_id = trap_BotGetNextCampSpotGoal(camp_spot_id, &goal))
	{
		// Only use this camp spot if it's closer than the last one
		time = EntityGoalTravelTime(bs->ent, &goal, bs->travel_flags);
		if(time < 0 || best_time < time)
			continue;

		// Save this camp spot information
		best_time = time;
		memcpy(&bs->camp_goal, &goal, sizeof(bot_goal_t));
		bs->camp_goal.entitynum = -1;
	}

	// Return true if a camp location was found
	if(best_time < max_time)
		return qtrue;

	// Reset the camp goal and fail
	GoalReset(&bs->camp_goal);
	return qfalse;
}

/*
===============
BotCampLocation
===============
*/
int BotCampLocation(bot_state_t * bs, bot_goal_t * goal, bot_goal_t * location)
{
	// Check for invalid location goals
	if(!location->areanum)
		return qfalse;

	// Since the whole point of camping is to find enemies, stop camping when you find someone
	if(bs->aim_enemy)
		return qfalse;

	// Move towards the camp location
	if(!BotPathPlan(bs, &bs->main_path, location, goal))
		return qfalse;

	// Set the task to camping
	BotSetTeamStatus(bs, TEAMTASK_CAMP);
	return qtrue;
}

/*
=================
BotGoalCampChoice
=================
*/
int BotGoalCampChoice(bot_state_t * bs, bot_goal_t * goal)
{
	float           camper;

	// If the bot hasn't camped in the past few seconds or the camp ending
	// timer expired, pick a new camp location (possibly none).
	if((bs->last_camp_time < bs->command_time - 3.0) || (bs->end_camp_time <= bs->command_time))
	{
		// Look up the bot's preference to camp
		camper = trap_Characteristic_BFloat(bs->character, CHARACTERISTIC_CAMPER, 0, 1);

		// Don't camp if the bot just finished camping, don't camp too often,
		// and certainly don't camp if the bot can't find a camp spot.
		if((bs->camp_goal.areanum) || (camper < random()) || (!BotChooseCampLocation(bs, 5)))
		{
			// The next time the bot should end "not camping" and consider camping again
			bs->end_camp_time = bs->command_time + 20.0 + 5.0 * crandom();
			return GOAL_NONE;
		}

		// The next time the bot should end or change its camping location
		bs->end_camp_time = bs->command_time + 10.0 + 5.0 * crandom() + 10.0 * camper;
	}

	// Use the bot's camp goal if it's defined
	if(!BotCampLocation(bs, goal, &bs->camp_goal))
		return GOAL_NONE;

	// Remember that the bot chose to camp at this time
	bs->last_camp_time = bs->command_time;

	return GOAL_CAMP_CHOICE;
}

/*
================
BotGoalCampOrder
================
*/
int BotGoalCampOrder(bot_state_t * bs, bot_goal_t * goal)
{
	// If the order hasn't timed out, try camping
	if((bs->order_time < bs->command_time) || (!BotCampLocation(bs, goal, &bs->camp_goal)))
	{
		// The goal must have failed so reset the order
		BotOrderAnnounceReset(bs, "camp_stop", NULL, NULL);
		return GOAL_NONE;
	}

	// Possibly announce start of order acceptance
	if(BotOrderShouldAnnounce(bs))
		BotOrderAnnounceStart(bs, "camp_start", bs->order_requester, NULL, VOICECHAT_YES);

	// If the bot has arrived and it hasn't announced this yet, do so.
	// Note that the bot will stand stand still (no goal) when it's close enough.
	if(!goal->areanum && bs->announce_arrive)
	{
		Bot_InitialChat(bs, "camp_arrive", SimplifyName(EntityNameFast(bs->order_requester)), NULL);
		trap_BotEnterChat(bs->cs, bs->order_requester->s.number, CHAT_TELL);
		BotVoiceChatOnly(bs, bs->order_requester->s.number, VOICECHAT_INPOSITION);
		bs->announce_arrive = qfalse;
	}

	return GOAL_CAMP_ORDER;
}

/*
==========
BotGetFlag
==========
*/
qboolean BotGetFlag(bot_state_t * bs, bot_goal_t * goal)
{
	bot_goal_t     *flag;

	// The bot can only get the flag if a flag entity is on the map
	if(bs->our_target_flag_status != FS_AT_HOME && bs->our_target_flag_status != FS_DROPPED)
		return qfalse;

	// Assume the bot knows where the flag is-- even if the flag was
	// dropped, the team carrier should have told the bot where it is.
	if(!GoalEntity(goal, bs->our_target_flag))
		return qfalse;

	// Plan a path to that flag
	if(!BotPathPlan(bs, &bs->main_path, goal, goal))
		return qfalse;

	BotSetTeamStatus(bs, TEAMTASK_OFFENSE);

	return qtrue;
}

/*
====================
BotGoalGetFlagChoice
====================
*/
int BotGoalGetFlagChoice(bot_state_t * bs, bot_goal_t * goal)
{
	// Try to get the flag
	if(!BotGetFlag(bs, goal))
		return GOAL_NONE;

	return GOAL_GETFLAG_CHOICE;
}

/*
===================
BotGoalGetFlagOrder
===================
*/
int BotGoalGetFlagOrder(bot_state_t * bs, bot_goal_t * goal)
{
	// Check for goal timeout
	if(bs->order_time < bs->command_time)
	{
		BotOrderReset(bs);
		return GOAL_NONE;
	}

	// Try the goal
	if(!BotGetFlag(bs, goal))
	{
		BotOrderAnnounceReset(bs, "reject_order_unable", bs->order_requester, NULL);
		return GOAL_NONE;
	}

	// Possibly announce start of order acceptance
	if(BotOrderShouldAnnounce(bs))
		BotOrderAnnounceStart(bs, "captureflag_start", NULL, NULL, VOICECHAT_ONGETFLAG);

	return GOAL_GETFLAG_ORDER;
}

/*
=============
BotReturnFlag
=============
*/
qboolean BotReturnFlag(bot_state_t * bs, bot_goal_t * goal)
{
	// The bot can only get the flag if the flag entity is on the map
	if(bs->their_target_flag_status == FS_MISSING)
		return qfalse;

	// Don't return the flag if it's already at home
	if(bs->their_target_flag_status == FS_AT_HOME)
		return qfalse;

	// Assume the bot knows where the flag is-- someone was present when the
	// flag was dropped or someone recently saw the enemy flag carrier
	if(!GoalEntity(goal, bs->their_target_flag))
		return qfalse;

	// Only return the flag if the bot can reach that location
	if(!BotPathPlan(bs, &bs->main_path, goal, goal))
		return qfalse;

	BotSetTeamStatus(bs, TEAMTASK_RETRIEVE);
	return qtrue;
}

/*
=======================
BotGoalReturnFlagChoice
=======================
*/
int BotGoalReturnFlagChoice(bot_state_t * bs, bot_goal_t * goal)
{
	// Try to return the flag
	if(!BotReturnFlag(bs, goal))
		return GOAL_NONE;

	return GOAL_RETURNFLAG_CHOICE;
}

/*
======================
BotGoalReturnFlagOrder
======================
*/
int BotGoalReturnFlagOrder(bot_state_t * bs, bot_goal_t * goal)
{
	// Check for goal timeout
	if(bs->order_time < bs->command_time)
	{
		BotOrderReset(bs);
		return GOAL_NONE;
	}

	// Try the goal
	if(!BotReturnFlag(bs, goal))
	{
		BotOrderAnnounceReset(bs, "reject_order_unable", bs->order_requester, NULL);
		return GOAL_NONE;
	}

	// Possibly announce start of order acceptance
	if(BotOrderShouldAnnounce(bs))
		BotOrderAnnounceStart(bs, "returnflag_start", NULL, NULL, VOICECHAT_ONRETURNFLAG);

	return GOAL_RETURNFLAG_ORDER;
}

/*
====================
BotGoalAssaultChoice
====================
*/
int BotGoalAssaultChoice(bot_state_t * bs, bot_goal_t * goal)
{
	// Try to attack the base
	if(!BotGoToBase(bs, goal, BotEnemyBase(bs)))
		return GOAL_NONE;

	return GOAL_ASSAULT_CHOICE;
}

/*
===================
BotGoalAssaultOrder
===================
*/
int BotGoalAssaultOrder(bot_state_t * bs, bot_goal_t * goal)
{
	// Check for goal timeout
	if(bs->order_time < bs->command_time)
	{
		BotOrderReset(bs);
		return GOAL_NONE;
	}

	// Try the goal
	if(!BotGoToBase(bs, goal, BotEnemyBase(bs)))
	{
		BotOrderAnnounceReset(bs, "reject_order_unable", bs->order_requester, NULL);
		return GOAL_NONE;
	}

	// Possibly announce start of order acceptance
	if(BotOrderShouldAnnounce(bs))
		BotOrderAnnounceStart(bs, "attackenemybase_start", NULL, NULL, VOICECHAT_ONOFFENSE);

	return GOAL_ASSAULT_ORDER;
}

#ifdef MISSIONPACK
/*
==========
BotHarvest
==========
*/
qboolean BotHarvest(bot_state_t * bs, bot_goal_t * goal)
{
	// Only possible in harvester mode
	if(gametype != GT_HARVESTER)
		return qfalse;

	// Head for the head vending machine
	return BotGoToBase(bs, goal, MID_BASE);
}

/*
====================
BotGoalHarvestChoice
====================
*/
int BotGoalHarvestChoice(bot_state_t * bs, bot_goal_t * goal)
{
	// Try to harvest skulls
	if(!BotHarvest(bs, goal))
		return GOAL_NONE;

	return GOAL_HARVEST_CHOICE;
}

/*
===================
BotGoalHarvestOrder
===================
*/
int BotGoalHarvestOrder(bot_state_t * bs, bot_goal_t * goal)
{
	// Check for goal timeout
	if(bs->order_time < bs->command_time)
	{
		BotOrderReset(bs);
		return GOAL_NONE;
	}

	// Try the goal
	if(!BotHarvest(bs, goal))
	{
		BotOrderAnnounceReset(bs, "reject_order_unable", bs->order_requester, NULL);
		return GOAL_NONE;
	}

	// Possibly announce start of order acceptance
	if(BotOrderShouldAnnounce(bs))
		BotOrderAnnounceStart(bs, "harvest_start", NULL, NULL, VOICECHAT_ONOFFENSE);

	return GOAL_HARVEST_ORDER;
}
#endif

/*
=======================
BotPreferOffenseChoices

Decide if the bot should prefer offensive oriented
goals or defense oriented goals this frame.

NOTE: Even the most defensive bot must be willing to try either offense or
defense, regardless of its task preference.  This is to prevent a team of
defensive bots from perpetually defending the base and never trying to win.
Of course, all of this is moot if the bot has a team leader to give orders.
=======================
*/
qboolean BotPreferOffenseChoices(bot_state_t * bs)
{
	int             our_base, their_base, our_time, their_time;
	float           threshold;

	// Recompute the goal sieve at a later time so the bot can switch tasks
	//
	// NOTE: The recomputation time is random to avoid all the bots on
	// the team calling this function at almost exactly the same time.
	// Spreading out the recomputations over several seconds is not only
	// more realistic, but easier on the processor.
	bs->goal_sieve_recompute_time = bs->command_time + 60.0 + random() * 30.0;

	// If the bot is alone, always go on the offense
	if(!BotTeammates(bs))
		return qtrue;

	// Slight weight aggressive tendancies, since it increases game pace
	threshold = .55;

	// Shift the threshold if the bot has a task preference
	if(BotPreferAttacker(bs))
		threshold += .15;
	if(BotPreferDefender(bs))
		threshold -= .15;

	// Shift the threshold if the bot is nearby an enemy base
	if(game_style & GS_BASE)
	{
		// Check if the bot is substantially closer to one base than the other
		BotBothBases(bs, &our_base, &their_base);
		our_time = EntityGoalTravelTime(bs->ent, &bases[our_base], bs->travel_flags);
		their_time = EntityGoalTravelTime(bs->ent, &bases[their_base], bs->travel_flags);

		// Prefer defense if in the nearest 40% of home base
		if(our_time * 3 < their_time * 2)
			threshold -= .15;
		// Prefer attack if in the nearest 40% of enemy base
		else if(their_time * 3 < our_time * 2)
			threshold += .15;
	}

	// Make a decision-- high thresholds make offensive choices more likely
	return (random() < threshold);
}

/*
===============
BotAddGoalCheck
===============
*/
void BotAddGoalCheck(bot_state_t * bs, goal_func_t * func)
{
	// Don't add the goal if the list ran out of space
	if(bs->goal_sieve_size >= MAX_GOALS)
		return;

	// Make sure the goal function exists
	if(!func)
		return;

	// Add the function to the list
	bs->goal_sieve[bs->goal_sieve_size++] = func;
}

/*
================
BotAddChoicesCTF

Add bot-selected choice goals for capture the flag
================
*/
void BotAddChoicesCTF(bot_state_t * bs)
{
	goal_func_t    *attack_func;
	goal_func_t    *defend_func;

	// For offense, either accompany the flag carrier or get the flag
	if(bs->our_target_flag_status == FS_CARRIER)
		attack_func = BotGoalAccompanyChoice;
	else
		attack_func = BotGoalGetFlagChoice;

	// Either defend the base if the flag is home or retrieve the flag if the enemy took it
	if(bs->their_target_flag_status == FS_AT_HOME)
		defend_func = BotGoalDefendChoice;
	else
		defend_func = BotGoalReturnFlagChoice;

	// Order these choices depending on the bot's offense versus defense preference
	if(BotPreferOffenseChoices(bs))
	{
		BotAddGoalCheck(bs, attack_func);
		BotAddGoalCheck(bs, defend_func);
	}
	else
	{
		BotAddGoalCheck(bs, defend_func);
		BotAddGoalCheck(bs, attack_func);
	}
}

#ifdef MISSIONPACK
/*
==================
BotAddChoices1FCTF

Add bot-selected choice goals for one flag capture the flag
==================
*/
void BotAddChoices1FCTF(bot_state_t * bs)
{
	// Accompany the flag carrier if we have one (and it's not this bot)
	if(bs->our_target_flag_status == FS_CARRIER && bs->our_target_flag != bs->ent)
		BotAddGoalCheck(bs, BotGoalAccompanyChoice);

	// Different logic depending on the flag status
	switch (bs->our_target_flag_status)
	{
			// The enemy team has the flag
		case FS_MISSING:
			if(BotPreferOffenseChoices(bs))
			{
				BotAddGoalCheck(bs, BotGoalReturnFlagChoice);
				BotAddGoalCheck(bs, BotGoalDefendChoice);
			}
			else
			{
				BotAddGoalCheck(bs, BotGoalDefendChoice);
				BotAddGoalCheck(bs, BotGoalReturnFlagChoice);
			}

			// Our team has the flag
		case FS_CARRIER:
			BotAddGoalCheck(bs, BotGoalAssaultChoice);

			// The flag is somewhere on the level
		case FS_AT_HOME:
		case FS_DROPPED:
			if(BotPreferOffenseChoices(bs))
			{
				BotAddGoalCheck(bs, BotGoalGetFlagChoice);
				BotAddGoalCheck(bs, BotGoalDefendChoice);
			}
			else
			{
				BotAddGoalCheck(bs, BotGoalDefendChoice);
				BotAddGoalCheck(bs, BotGoalGetFlagChoice);
			}
			break;

			// This should never execute, but try everything just to be safe
		default:
			BotAddGoalCheck(bs, BotGoalGetFlagChoice);
			BotAddGoalCheck(bs, BotGoalReturnFlagChoice);
			BotAddGoalCheck(bs, BotGoalAssaultChoice);
			BotAddGoalCheck(bs, BotGoalDefendChoice);
			break;
	}
}

/*
======================
BotAddChoicesHarvester

Add bot-selected choice goals for harvester
======================
*/
void BotAddChoicesHarvester(bot_state_t * bs)
{
	BotAddGoalCheck(bs, BotGoalAccompanyChoice);

	if(BotPreferOffenseChoices(bs))
	{
		BotAddGoalCheck(bs, BotGoalHarvestChoice);
		BotAddGoalCheck(bs, BotGoalDefendChoice);
	}
	else
	{
		BotAddGoalCheck(bs, BotGoalDefendChoice);
		BotAddGoalCheck(bs, BotGoalHarvestChoice);
	}
}

/*
====================
BotAddChoicesObelisk

Add bot-selected choice goals for Overload
====================
*/
void BotAddChoicesObelisk(bot_state_t * bs)
{
	if(BotPreferOffenseChoices(bs))
	{
		BotAddGoalCheck(bs, BotGoalAssaultChoice);
		BotAddGoalCheck(bs, BotGoalDefendChoice);
	}
	else
	{
		BotAddGoalCheck(bs, BotGoalDefendChoice);
		BotAddGoalCheck(bs, BotGoalAssaultChoice);
	}
}
#endif

/*
=======================
BotComputeGoalCheckList
=======================
*/
void BotComputeGoalCheckList(bot_state_t * bs)
{
	// Don't recompute if the sieve is valid and no recompute was requested
	if((bs->goal_sieve_valid) && (!bs->goal_sieve_recompute_time || bs->goal_sieve_recompute_time > bs->command_time))
	{
		return;
	}

	// Assert that the sieve will be valid after this function call
	bs->goal_sieve_valid = qtrue;
	bs->goal_sieve_recompute_time = 0;

	// Reset the old list
	bs->goal_sieve_size = 0;

	// Getting air when underwater is always the highest priority
	BotAddGoalCheck(bs, BotGoalAirChoice);

	// Consider a capture goal if the game has carriable (capturable) objects
	if(game_style & GS_CARRIER)
	{
		// Add the goal if there are no flags, or if the bot is the flag carrier
		if(!(game_style & GS_FLAG) || EntityIsCarrier(bs->ent))
			BotAddGoalCheck(bs, BotGoalCaptureChoice);
	}

	// A lot of goal cases only apply in teamplay modes
	if(game_style & GS_TEAM)
	{
		// Leading teammates is just a check that preempts the "normal" goals
		if(bs->lead_teammate)
			BotAddGoalCheck(bs, BotGoalLeadOrder);

		// Orders are always more important than standard goals the bot might choose
		switch (bs->order_type)
		{
			case ORDER_HELP:
				BotAddGoalCheck(bs, BotGoalHelpOrder);
				break;
			case ORDER_ACCOMPANY:
				BotAddGoalCheck(bs, BotGoalAccompanyOrder);
				break;
			case ORDER_ITEM:
				BotAddGoalCheck(bs, BotGoalItemOrder);
				break;
			case ORDER_ATTACK:
				BotAddGoalCheck(bs, BotGoalAttackOrder);
				break;
			case ORDER_GETFLAG:
				BotAddGoalCheck(bs, BotGoalGetFlagOrder);
				break;
			case ORDER_RETURNFLAG:
				BotAddGoalCheck(bs, BotGoalReturnFlagOrder);
				break;
			case ORDER_DEFEND:
				BotAddGoalCheck(bs, BotGoalDefendOrder);
				break;
			case ORDER_CAMP:
				BotAddGoalCheck(bs, BotGoalCampOrder);
				break;
			case ORDER_PATROL:
				BotAddGoalCheck(bs, BotGoalPatrolOrder);
				break;
			case ORDER_ASSAULT:
				BotAddGoalCheck(bs, BotGoalAssaultOrder);
				break;
#ifdef MISSIONPACK
			case ORDER_HARVEST:
				BotAddGoalCheck(bs, BotGoalHarvestOrder);
				break;
#endif
		}
	}

	// The goals a bot might choose depend on on the game type
	switch (gametype)
	{
		case GT_CTF:
			BotAddChoicesCTF(bs);
			break;
#ifdef MISSIONPACK
		case GT_1FCTF:
			BotAddChoices1FCTF(bs);
			break;
		case GT_HARVESTER:
			BotAddChoicesHarvester(bs);
			break;
		case GT_OBELISK:
			BotAddChoicesObelisk(bs);
			break;
#endif
	}

	// Attacking a nearby enemy is a good default option
	BotAddGoalCheck(bs, BotGoalAttackChoice);

	// The bot should look for enemies near valuable items
	BotAddGoalCheck(bs, BotGoalItemChoice);

	// As a last resort, consider camping somewhere
	BotAddGoalCheck(bs, BotGoalCampChoice);
}

/*
===========
BotMainGoal
===========
*/
void BotMainGoal(bot_state_t * bs)
{
	int             i, result, type;
	qboolean        recompute;

	// There's currently no known entity to face
	// NOTE: This is currently used for the lead teammate goal but
	// could be used for other reasons
	bs->face_entity = NULL;

	// Update path prediction information
	BotPathUpdate(bs, &bs->main_path);

	// Compute the sieve if necessary
	BotComputeGoalCheckList(bs);

	// Search the goal sieve for an acceptable goal
	for(i = 0; i < bs->goal_sieve_size; i++)
	{
		// Try the goal
		type = bs->goal_sieve[i] (bs, &bs->goal);
		if(type != GOAL_NONE)
		{
			BotSetGoalType(bs, type);
			return;
		}
	}

	// If no acceptable goal was found, do nothing by default
	BotGoalReset(bs);
}
