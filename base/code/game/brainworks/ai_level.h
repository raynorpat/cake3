// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_level.h
 *
 * Includes for accessing level information
 *****************************************************************************/

extern bot_goal_t bases[NUM_BASES];
extern gentity_t *flags[NUM_BASES];

qboolean        LevelLibrarySetup(void);
void            LevelLibraryUpdate(void);
char           *LevelMapTitle(void);
void            BotMapScripts(bot_state_t * bs);
int             LevelAreaPoint(vec3_t origin);
int             LevelAreaLocPoint(vec3_t origin, vec3_t location, float start_height, float end_height);
int             LevelAreaEntity(gentity_t * ent);
float           LevelTravelTime(int start_area, vec3_t start_loc, int end_area, vec3_t end_loc, int tfl);
void            LevelEnableRoutingArea(int area);
void            LevelDisableRoutingArea(int area);
qboolean        LevelAreasNearby(int start_area, vec3_t start_origin, int end_area);
int             LevelBaseTravelTime(int from_base, int to_base);
void            LevelBaseSetup(void);
void            LevelFlagScan(void);
