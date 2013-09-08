// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_accuracy.h
 *
 * Includes used for bot accuracy estimate
 *****************************************************************************/

extern float    dist_zone_center[ZCD_NUM_IDS];
extern float    pitch_zone_center[ZCP_NUM_IDS];

void            AccuracyCreate(bot_accuracy_t * acc, int weapon, float shots,
							   float direct_hits, float splash_hits, float total_splash_damage,
							   float actual_fire_time, float potential_fire_time);
void            CombatZoneCreate(combat_zone_t * zone, float dist, float pitch);
void            CombatZoneInvert(combat_zone_t * source, combat_zone_t * inverted);
void            BotAccuracyRecord(bot_state_t * bs, bot_accuracy_t * acc, int weapon, combat_zone_t * zone);
void            BotAccuracyRead(bot_state_t * bs, bot_accuracy_t * acc, int weapon, combat_zone_t * zone);
void            BotAccuracyReset(bot_state_t * bs);
void            BotAccuracyUpdate(bot_state_t * bs);
float           BotAttackRate(bot_state_t * bs, bot_accuracy_t * acc);
void            AccuracySetup(void);
