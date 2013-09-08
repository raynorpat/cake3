// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_motion.h
 *
 * Includes used to detect entity motion
 *****************************************************************************/

float           EntityMotionUpdateRate(gentity_t * ent);
void            EntityMotionStateUpdateCachedData(gentity_t * ent, motion_state_t * motion);
void            EntityMotionStateNow(gentity_t * ent, motion_state_t * motion);
void            EntityMotionStateInterpolate(gentity_t * ent, motion_state_t * a, motion_state_t * b,
											 float time, motion_state_t * result);
void            EntityMotionStateTime(gentity_t * ent, motion_state_t * motion, float time);
void            BotAIMotionReset(void);
void            BotAIMotionUpdate(void);
float           BotEntityMotionLagged(bot_state_t * bs, gentity_t * ent, float lag, motion_state_t * state);
void            BotMotionUpdate(bot_state_t * bs);
