// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_predict.h
 *
 * Includes used to predict entity motion
 *****************************************************************************/

void            BotAIPredictReset(void);
void            EntityMotionPredict(gentity_t * ent, motion_state_t * motion, float time);
void            BotMotionFutureUpdate(bot_state_t * bs);

#ifdef DEBUG_AI
void            PredictDebugEntityAdd(gentity_t * ent, float time_lapse, motion_state_t * motion);
void            PredictDebugEntityNow(gentity_t * ent);
void            PredictDebugCheck(void);
#endif
