// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_attack.h
 *
 * Includes used for determining how to attack targets
 *****************************************************************************/

qboolean        BotAttackSelect(bot_state_t * bs, gentity_t * ent, int weapon, float sighted);
void            BotAttackAddError(bot_state_t * bs, vec3_t error);
void            BotAttackFireUpdate(bot_state_t * bs);
void            BotAttackFireWeapon(bot_state_t * bs);
