// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_item.h
 *
 * Includes used for item and region processing
 *****************************************************************************/

// Relatively important item clusters on the level, sorted by cluster pointer
extern item_cluster_t *important_items[MAX_REGIONS];
extern int      num_important_items;
extern float    important_item_total_value;

// The average value of a picking up a cluster of items
extern float    pickup_value_average;

#ifdef DEBUG_AI
void            PrintCluster(item_cluster_t * cluster, int indent);
void            PrintClusterList(item_cluster_t * clusters, int num_clusters, char *list_name);
#endif

qboolean        CanProcessItems(void);
const char     *ClusterName(const void *cluster);
item_cluster_t *DroppedItemCluster(gentity_t * ent);
int             ItemArea(gentity_t * ent);
gentity_t      *NearestNamedItem(char *name, vec3_t location);
void            LevelItemReset(void);
void            LevelItemSetup(void);
void            LevelItemUpdate(void);
