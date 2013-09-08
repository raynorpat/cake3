// All portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_move.h
 *
 * Includes used for bot movement algorithms
 *****************************************************************************/

char           *MoveName(int move);
void            BotMoveSetup(bot_state_t * bs);
qboolean        BotMovementAxies(bot_state_t * bs, vec3_t * axis);
qboolean        BotTestMove(bot_state_t * bs, vec3_t dir);
void            BotMoveSelect(bot_state_t * bs, bot_moveresult_t * moveresult);
void            BotMoveModifierUpdate(bot_state_t * bs);
void            BotMoveProcess(bot_state_t * bs);
