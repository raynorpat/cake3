// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_pickup.h
 *
 * Includes used to determine item pickups
 *****************************************************************************/

void            BotItemReset(bot_state_t * bs);
qboolean        BotTimeCluster(bot_state_t * bs, item_cluster_t * cluster);
qboolean        BotTimeClusterLoc(bot_state_t * bs, vec3_t loc);
void            BotItemGoal(bot_state_t * bs);
