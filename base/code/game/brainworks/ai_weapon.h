// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_weapon.h
 *
 * Includes used for bot weapon algorithms
 *****************************************************************************/

// The median expected damage per second of all weapons in the game
extern float    damage_per_second_typical;

// Description of each weapon in the game
extern weapon_stats_t weapon_stats[WP_NUM_WEAPONS];

char           *WeaponName(int weapon);
char           *WeaponNameForMeansOfDeath(int means_of_death);
int             WeaponFromName(char *name);
int             WeaponPerceivedMaxRange(int weapon);
qboolean        WeaponInRange(int weapon, float dist);
qboolean        WeaponCareless(int weapon);
void            LevelWeaponSetup(void);
void            LevelWeaponUpdateGametype();
qboolean        BotWeaponChanging(bot_state_t * bs);
qboolean        BotWeaponReady(bot_state_t * bs);
qboolean        BotHasWeapon(bot_state_t * bs, int weapon, int ammo);
int             BotMineDisarmWeapon(bot_state_t * bs);
int             BotActivateWeapon(bot_state_t * bs);
float           BotDamageRate(bot_state_t * bs, unsigned int weapon_list, combat_zone_t * zone, qboolean splash);
int             BotTargetWeapon(bot_state_t * bs);
