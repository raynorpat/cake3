// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_aware.h
 *
 * Includes used to interface the enemy awareness system
 *****************************************************************************/

void            BotAwarenessReset(bot_state_t * bs);
gentity_t      *BotBestAwarenessEntity(bot_state_t * bs);
bot_aware_t    *BotAwarenessOfEntity(bot_state_t * bs, gentity_t * ent);
qboolean        BotSightedEntity(bot_state_t * bs, gentity_t * ent);
void            BotAwarenessUpdate(bot_state_t * bs);
qboolean        BotAwareTrackEntity(bot_state_t * bs, gentity_t * ent, float event_radius, float refresh_radius);
qboolean        BotAwarenessLocation(bot_state_t * bs, vec3_t origin);
