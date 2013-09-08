// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_visible.h
 *
 * Includes used for bot visibility tests
 *****************************************************************************/

qboolean        BotTargetInFieldOfVision(bot_state_t * bs, vec3_t target, float fov);
qboolean        BotGoalVisible(bot_state_t * bs, bot_goal_t * goal);
qboolean        BotEntityVisibleFast(bot_state_t * bs, gentity_t * ent);
qboolean        BotEntityVisible(bot_state_t * bs, gentity_t * ent);
float           BotEntityVisibleCenter(bot_state_t * bs, gentity_t * ent, vec3_t eye, vec3_t center);
