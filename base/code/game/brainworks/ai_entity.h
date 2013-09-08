// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_entity.h
 *
 * Includes for accessing entity information
 *****************************************************************************/

char           *SimplifyName(char *name);
char           *EntityName(gentity_t * ent, char *name, size_t size);
char           *EntityNameFast(gentity_t * ent);
qboolean        EntityUpdatesSynchronous(gentity_t * ent);
float           EntityTimestamp(gentity_t * ent);
void            EntityWorldBounds(gentity_t * ent, vec3_t mins, vec3_t maxs);
void            EntityCenter(gentity_t * ent, vec3_t center);
void            EntityCenterWorldBounds(gentity_t * ent, vec3_t center, vec3_t mins, vec3_t maxs);
void            EntityCenterAllBounds(gentity_t * ent, vec3_t center,
									  vec3_t world_mins, vec3_t world_maxs, vec3_t local_mins, vec3_t local_maxs);
int             EntityClipMask(gentity_t * ent);
qboolean        EntityOnGroundNow(gentity_t * ent);
qboolean        EntityCrouchingNow(gentity_t * ent);
gentity_t      *EntityOnMoverNow(gentity_t * ent);
int             EntityWaterLevel(gentity_t * ent, vec3_t origin, qboolean crouch);
qboolean        EntityInLavaOrSlime(gentity_t * ent);
void            EntityPhysics(gentity_t * ent, physics_t * physics,
							  vec3_t origin, vec3_t mins, vec3_t maxs, vec3_t velocity,
							  int water_level, qboolean flight, qboolean knockback);
int             EntityPhysicsNow(gentity_t * ent);
int             EntityTeam(gentity_t * ent);
qboolean        EntityIsAlive(gentity_t * ent);
qboolean        EntityIsCarrier(gentity_t * ent);
qboolean        EntityIsInvisible(gentity_t * ent);
float           EntityKillValue(gentity_t * ent);
int             EntityHealth(gentity_t * ent);
float           EntityRating(gentity_t * ent);
float           EntityTravelTime(gentity_t * ent, int end_area, vec3_t end_loc, int tfl);
float           EntityGoalTravelTime(gentity_t * ent, bot_goal_t * goal, int tfl);
