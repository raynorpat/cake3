// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_goal.c
 *
 * Functions that the bot uses to handle goals
 *****************************************************************************/

#include "ai_main.h"
#include "ai_vars.h"
#include "ai_goal.h"

#include "ai_client.h"
#include "ai_entity.h"
#include "ai_item.h"
#include "ai_level.h"
#include "ai_self.h"
#include "ai_team.h"
#include "ai_waypoint.h"


/*
========
GoalName
========
*/
void GoalName(bot_goal_t * goal, char *name, size_t size)
{
	// Check for invalid goals
	if(!goal->areanum)
	{
		Q_strncpyz(name, "NO GOAL", size);
		return;
	}

	// Try to name the goal after the goal's entity
	if(goal->entitynum >= 0)
	{
		EntityName(&g_entities[goal->entitynum], name, size);
		return;
	}

	// Use the coordinates of the goal as its name
	Com_sprintf(name, size, "(%1.0f, %1.0f, %1.0f)", goal->origin[0], goal->origin[1], goal->origin[2]);
}

/*
============
GoalNameFast
============
*/
char           *GoalNameFast(bot_goal_t * goal)
{
	static int      index = 0;
	static char     name_cache[2][MAX_NETNAME];
	char           *name;

	// Select the appropriate pointer to use this frame
	name = name_cache[index & 0x1];
	index ^= 0x1;

	// Store the input entity's name in the local cache,
	GoalName(goal, name, MAX_NETNAME);

	// Give the caller access to this name
	return name;
}

/*
==========
GoalPlayer

Returns a pointer to the player entity that
defines a goal or NULL if the goal's entity
is not a player.
==========
*/
gentity_t      *GoalPlayer(bot_goal_t * goal)
{
	if(goal->entitynum < 0 || goal->entitynum >= MAX_CLIENTS)
		return NULL;
	else
		return &g_entities[goal->entitynum];
}

/*
=========
GoalReset
=========
*/
void GoalReset(bot_goal_t * goal)
{
	memset(goal, 0, sizeof(bot_goal_t));
	goal->entitynum = -1;
}

/*
================
GoalLocationArea
================
*/
qboolean GoalLocationArea(bot_goal_t * goal, vec3_t origin, int area)
{
	GoalReset(goal);

	VectorCopy(origin, goal->origin);
	VectorSet(goal->mins, -8, -8, -8);
	VectorSet(goal->maxs, 8, 8, 8);

	goal->areanum = area;

	return (area > 0);
}

/*
============
GoalLocation

Makes/Updates a goal object out of a position vector

Returns true if a goal was created/updated
============
*/
qboolean GoalLocation(bot_goal_t * goal, vec3_t origin)
{
	// Set up the goal using the location's area
	return GoalLocationArea(goal, origin, LevelAreaPoint(origin));
}

/*
==============
GoalEntityArea
==============
*/
qboolean GoalEntityArea(bot_goal_t * goal, gentity_t * ent, int area)
{
	// Try to make the goal based on the entity's location and area
	if(!GoalLocationArea(goal, ent->r.currentOrigin, area))
		return qfalse;

	// Include the entity index
	goal->entitynum = ent->s.number;
	return qtrue;
}

/*
==========
GoalEntity

Makes/Updates a goal object out of a
gentity_t object if the entity is valid.

Returns true if a goal was created/updated.
==========
*/
qboolean GoalEntity(bot_goal_t * goal, gentity_t * ent)
{
	// Set up the goal using the entity's area
	return GoalEntityArea(goal, ent, LevelAreaEntity(ent));
}

/*
============
GoalFromName

Makes/Updates a goal object from a name (usually
an item name but possibly one of the bot's waypoints).

Returns true if a goal was created/updated.
============
*/
qboolean GoalFromName(bot_goal_t * goal, char *goalname, bot_state_t * bs)
{
	gentity_t      *ent;
	bot_waypoint_t *cp;

	// Look for a item matching that name near the bot
	ent = NearestNamedItem(goalname, bs->now.origin);
	if(ent && GoalEntity(goal, ent))
		return qtrue;

	// Search for a waypoint with matching name
	cp = BotFindWaypoint(bs, goalname);
	if(cp)
	{
		memcpy(goal, &cp->goal, sizeof(bot_goal_t));
		return qtrue;
	}

	// No such goal could be found
	return qfalse;
}
