// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_resource.h
 *
 * Includes used for resource state predictions
 *****************************************************************************/

float           BotItemUtility(bot_state_t * bs, gentity_t * ent);
float           BaseItemValue(gitem_t * item);
int             BaseItemRespawn(gitem_t * item);
float           ItemRespawn(gentity_t * ent);
float           ItemUtilityDuration(gitem_t * item);
float           HealthArmorToDamage(float health, float armor);
float           ResourceScoreRate(resource_state_t * rs);
void            PlayInfoFromBot(play_info_t * pi, bot_state_t * bs);
void            ResourceFromPlayer(resource_state_t * rs, gentity_t * ent, play_info_t * pi);
qboolean        ResourceAddCluster(resource_state_t * rs, item_cluster_t * cluster, float time,
								   float see_teammate, float see_enemy);
void            ResourcePredictEncounter(resource_state_t * rs, float time, float score, float player_rate, float enemy_rate);
void            ItemValuesReset(void);
void            ItemValuesCompute(item_link_t * items, int num_items);
