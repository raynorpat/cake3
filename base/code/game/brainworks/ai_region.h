// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_region.h
 *
 * Includes used for region processing
 *****************************************************************************/

// Item pickup regions the level is divided into
extern region_t region_list[MAX_REGIONS];
extern int      num_regions;

// Which item pickup regions players have been in and are currently in
extern history_t region_traffic[MAX_REGIONS][TEAM_NUM_TEAMS];
extern int      player_region[MAX_CLIENTS];

#ifdef DEBUG_AI
void            PrintRegion(region_t * regions, int index, int indent);
#endif

const char     *RegionName(const void *region);
const float    *RegionLocation(const void *region);
qboolean        CanProcessRegions(void);
int             LevelRegionIndex(const region_t * region);
region_t       *LevelNearestRegion(vec3_t point);
int             LevelNearestRegionIndex(vec3_t point);
const char     *LevelNearestRegionName(vec3_t point);
float           LevelRegionTravelTime(region_t * from, region_t * to);
region_t      **LevelRegionNeighborList(region_t * from, region_t * to);
int             LevelNeighborListSize(region_t ** neighbors);
qboolean        LevelRegionIsNeighbor(region_t * region, region_t ** neighbors, int num_neighbors);
void            LevelRegionReset(void);
void            LevelRegionResetDynamic(void);
void            LevelRegionSetup(item_cluster_t * clusters, int num_clusters);
void            LevelPlayerRegionUpdate(void);
region_t       *BotTrafficData(bot_state_t * bs, vec3_t loc, history_t * teammate, history_t * enemy);
