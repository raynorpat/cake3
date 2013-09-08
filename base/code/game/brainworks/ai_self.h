// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_self.h
 *
 * Includes used for accessing/setting data about the bot itself
 *****************************************************************************/

void            BotSetUserInfo(bot_state_t * bs, char *key, char *value);
qboolean        BotIsCarrier(bot_state_t * bs);
int             BotTeam(bot_state_t * bs);
int             BotTeamBase(bot_state_t * bs);
int             BotEnemyBase(bot_state_t * bs);
int             BotCaptureBase(bot_state_t * bs);
void            BotBothBases(bot_state_t * bs, int *us, int *them);
int             BotSynonymContext(bot_state_t * bs);
qboolean        BotIsDead(bot_state_t * bs);
qboolean        BotIsObserver(bot_state_t * bs);
qboolean        BotInIntermission(bot_state_t * bs);
qboolean        BotShouldRocketJump(bot_state_t * bs);
int             BotEnemyHealth(bot_state_t * bs);
void            BotEnemyHealthSet(bot_state_t * bs, int health);
void            BotAimEnemySet(bot_state_t * bs, gentity_t * enemy, combat_zone_t * zone);
void            BotSetInfoConfigString(bot_state_t * bs);
void            BotCheckServerCommands(bot_state_t * bs);
void            BotReactionLoad(bot_state_t * bs);
void            BotInitialize(bot_state_t * bs);
void            BotResetState(bot_state_t * bs);
