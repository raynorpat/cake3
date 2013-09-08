// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_goal.h
 *
 * Includes used for bot goal algorithms
 *****************************************************************************/

void            GoalName(bot_goal_t * goal, char *name, size_t size);
char           *GoalNameFast(bot_goal_t * goal);
gentity_t      *GoalPlayer(bot_goal_t * goal);
void            GoalReset(bot_goal_t * goal);
qboolean        GoalLocationArea(bot_goal_t * goal, vec3_t origin, int area);
qboolean        GoalLocation(bot_goal_t * goal, vec3_t origin);
qboolean        GoalEntityArea(bot_goal_t * goal, gentity_t * ent, int area);
qboolean        GoalEntity(bot_goal_t * goal, gentity_t * ent);
qboolean        GoalFromName(bot_goal_t * goal, char *goalname, bot_state_t * bs);
