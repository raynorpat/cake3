// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_path.h
 *
 * Includes used to help bots plan paths
 *****************************************************************************/

void            LevelPathReset(void);
void            LevelPathSetup(void);
void            BotPathReset(bot_path_t * path);
void            BotPathUpdate(bot_state_t * bs, bot_path_t * path);
qboolean        BotPathPlan(bot_state_t * bs, bot_path_t * path, bot_goal_t * objective, bot_goal_t * destination);
