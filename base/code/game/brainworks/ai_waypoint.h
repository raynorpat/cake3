// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_waypoint.h
 *
 * Includes used for bot waypoint setup and use
 *****************************************************************************/

void            LevelInitWaypoints(void);
bot_waypoint_t *BotFindWaypoint(bot_state_t * bs, char *name);
void            BotFreeWaypoints(bot_waypoint_t * wp);
qboolean        BotMatch_PatrolWaypoints(bot_state_t * bs, bot_match_t * match, gentity_t * sender);
void            BotMatch_CheckPoint(bot_state_t * bs, bot_match_t * match, gentity_t * sender);
bot_goal_t     *BotNextPatrolPoint(bot_state_t * bs);
