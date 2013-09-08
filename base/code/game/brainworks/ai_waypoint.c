// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_waypoint.c
 *
 * Functions that the bot uses for waypoints
 *****************************************************************************/

#include "ai_main.h"
#include "ai_vars.h"
#include "ai_waypoint.h"

#include "ai_chat.h"
#include "ai_goal.h"
#include "ai_level.h"


// Patrol flags
#define PATROL_LOOP			0x01
#define PATROL_REVERSE		0x02
#define PATROL_BACK			0x04

#define MAX_WAYPOINTS		128

bot_waypoint_t  botai_waypoints[MAX_WAYPOINTS];
bot_waypoint_t *botai_freewaypoints;


/*
==================
LevelInitWaypoints
==================
*/
void LevelInitWaypoints(void)
{
	int             i;

	botai_freewaypoints = NULL;
	for(i = 0; i < MAX_WAYPOINTS; i++)
	{
		botai_waypoints[i].next = botai_freewaypoints;
		botai_freewaypoints = &botai_waypoints[i];
	}
}

/*
=================
BotCreateWaypoint

Creates a new waypoint with the inputted name.
This function's caller must set up the waypoint's goal.
=================
*/
bot_waypoint_t *BotCreateWaypoint(char *name)
{
	bot_waypoint_t *wp;

	// Make sure a new waypoint can be allocated
	wp = botai_freewaypoints;
	if(!wp)
	{
		BotAI_Print(PRT_WARNING, "BotCreateWaypoint: Out of waypoints\n");
		return NULL;
	}
	botai_freewaypoints = botai_freewaypoints->next;

	// Set all waypoint information except the goal
	Q_strncpyz(wp->name, name, sizeof(wp->name));
	wp->next = NULL;
	wp->prev = NULL;
	return wp;
}

/*
===============
BotFindWaypoint
===============
*/
bot_waypoint_t *BotFindWaypoint(bot_state_t * bs, char *name)
{
	bot_waypoint_t *wp;

	// Search for a checkpoint with matching name
	for(wp = bs->checkpoints; wp; wp = wp->next)
	{
		if(!Q_stricmp(wp->name, name))
			return wp;
	}

	// No such waypoint was found
	return NULL;
}

/*
================
BotFreeWaypoints
================
*/
void BotFreeWaypoints(bot_waypoint_t * wp)
{
	bot_waypoint_t *nextwp;

	for(; wp; wp = nextwp)
	{
		nextwp = wp->next;
		wp->next = botai_freewaypoints;
		botai_freewaypoints = wp;
	}
}

/*
========================
BotMatch_PatrolWaypoints
========================
*/
qboolean BotMatch_PatrolWaypoints(bot_state_t * bs, bot_match_t * match, gentity_t * sender)
{
	char            keyarea[MAX_MESSAGE_SIZE];
	int             flags;
	bot_waypoint_t *new_wp, *last_wp, *new_patrol;
	bot_match_t     keyareamatch;
	bot_goal_t      goal;
	qboolean        success;

	// Initialize new patrol to a zero length path
	last_wp = new_patrol = NULL;
	flags = 0;
	success = qfalse;

	// Match successive waypoints in the patrol path
	trap_BotMatchVariable(match, KEYAREA, keyarea, MAX_MESSAGE_SIZE);
	while(1)
	{
		// Fail if the bot can't match the area name
		if(!trap_BotFindMatch(keyarea, &keyareamatch, MTCONTEXT_PATROLKEYAREA))
		{
			trap_EA_SayTeam(bs->client, "What did you say?");
			break;
		}

		// Fail if the bot can't find the requested area
		trap_BotMatchVariable(&keyareamatch, KEYAREA, keyarea, MAX_MESSAGE_SIZE);
		if(!GoalFromName(&goal, keyarea, bs))
			break;

		// Try to create a new waypoint
		new_wp = BotCreateWaypoint(keyarea);
		if(new_wp == NULL)
			break;

		// Copy the matched goal to the waypoint
		memcpy(&new_wp->goal, &goal, sizeof(bot_goal_t));

		// Insert waypoint into patrol point list
		new_wp->next = NULL;
		if(last_wp)
		{
			last_wp->next = new_wp;
			new_wp->prev = last_wp;
		}
		else
		{
			// First waypoint in list
			new_patrol = new_wp;
			new_wp->prev = NULL;
		}
		last_wp = new_wp;

		// Check for waypoint message completion
		if(keyareamatch.subtype & ST_REVERSE)
		{
			success = qtrue;
			flags = PATROL_REVERSE;
			break;
		}

		if(keyareamatch.subtype & ST_BACK)
		{
			success = qtrue;
			flags = PATROL_LOOP;
			break;
		}

		if(!(keyareamatch.subtype & ST_MORE))
		{
			success = qtrue;
			flags = PATROL_LOOP;
			break;
		}

		trap_BotMatchVariable(&keyareamatch, MORE, keyarea, MAX_MESSAGE_SIZE);
	}

	// Make sure the bot has at least two patrol points
	if(success && (!new_patrol || !new_patrol->next))
	{
		trap_EA_SayTeam(bs->client, "I need more key points to patrol\n");
		success = qfalse;
	}

	// Check for message match failure
	if(!success)
	{
		BotFreeWaypoints(new_patrol);
		return qfalse;
	}

	// Free old waypoints and use new waypoints
	BotFreeWaypoints(bs->patrol);
	bs->patrol = new_patrol;
	bs->next_patrol = bs->patrol;
	bs->patrol_flags = flags;

	return qtrue;
}

/*
===================
BotMatch_CheckPoint
===================
*/
void BotMatch_CheckPoint(bot_state_t * bs, bot_match_t * match, gentity_t * sender)
{
	int             area;
	char            buf[MAX_MESSAGE_SIZE];
	vec3_t          position;
	bot_waypoint_t *cp;
	qboolean        addressed;

	// The bot only confirms checkpoints if the messages were directly sent to this bot
	addressed = BotAddresseeMatch(bs, match);

	// Determine the checkpoint's location and area
	trap_BotMatchVariable(match, POSITION, buf, MAX_MESSAGE_SIZE);
	VectorClear(position);
	sscanf(buf, "%f %f %f", &position[0], &position[1], &position[2]);
	position[2] += 0.5;
	area = LevelAreaPoint(position);

	// Check for invalid checkpoints
	if(!area)
	{
		// Complain about an invalid checkpoint
		if(addressed)
		{
			Bot_InitialChat(bs, "checkpoint_invalid", NULL);
			trap_BotEnterChat(bs->cs, sender->s.number, CHAT_TELL);
		}

		return;
	}

	// If the bot has another checkpoint with this name, delete it
	trap_BotMatchVariable(match, NAME, buf, MAX_MESSAGE_SIZE);
	cp = BotFindWaypoint(bs, buf);
	if(cp)
	{
		if(cp->next)
			cp->next->prev = cp->prev;

		if(cp->prev)
			cp->prev->next = cp->next;
		else
			bs->checkpoints = cp->next;

		cp->inuse = qfalse;
	}

	// Create a new checkpoint
	cp = BotCreateWaypoint(buf);
	if(!cp)
		return;

	// Construct the waypoint goal
	GoalLocationArea(&cp->goal, position, area);

	// Add the checkpoint to the bot's checkpoint list
	cp->next = bs->checkpoints;
	if(bs->checkpoints)
		bs->checkpoints->prev = cp;
	bs->checkpoints = cp;

	// Confirm creation of the checkpoint
	if(addressed)
	{
		Bot_InitialChat(bs, "checkpoint_confirm", cp->name, GoalNameFast(&cp->goal), NULL);
		trap_BotEnterChat(bs->cs, sender->s.number, CHAT_TELL);
	}
}

/*
==================
BotNextPatrolPoint
==================
*/
bot_goal_t     *BotNextPatrolPoint(bot_state_t * bs)
{
	// Select the next checkpoint if the bot reached one checkpoint
	if(trap_BotTouchingGoal(bs->now.origin, &bs->next_patrol->goal))
	{
		// When patrolling backwards, always go to the previous point if possible,
		// or resume patrolling forward
		if(bs->patrol_flags & PATROL_BACK)
		{
			if(bs->next_patrol->prev)
			{
				bs->next_patrol = bs->next_patrol->prev;
			}
			else
			{
				bs->next_patrol = bs->next_patrol->next;
				bs->patrol_flags &= ~PATROL_BACK;
			}
		}

		// When patrolling forward, move to the next point if possible.  When not
		// possible either reverse the patrol or loop back the beginning as necessary.
		else
		{
			if(bs->next_patrol->next)
			{
				bs->next_patrol = bs->next_patrol->next;
			}
			else if(bs->patrol_flags & PATROL_REVERSE)
			{
				bs->next_patrol = bs->next_patrol->prev;
				bs->patrol_flags |= PATROL_BACK;
			}
			else
			{
				bs->next_patrol = bs->patrol;
			}
		}
	}

	// Return the current patrol point
	return &bs->next_patrol->goal;
}
