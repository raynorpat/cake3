// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_weapon.c
 *
 * Functions that the bot uses for shooting and selecting weapons
 *****************************************************************************/

#include "ai_main.h"
#include "ai_vars.h"
#include "ai_weapon.h"

#include "ai_accuracy.h"
#include "ai_client.h"
#include "ai_entity.h"
#include "ai_self.h"
#include "ai_view.h"


// The median expected damage per second of all weapons defined in the game.
float           damage_per_second_typical;

// The time it takes to switch weapons
//
// NOTE: This time is based on PM_BeginWeaponChange() and PM_FinishWeaponChange()
// in bg_pmove.c.  It is the sum of 200 ms to put down the weapon and 250 ms to
// bring up a new one.
#define WEAPON_SWITCH_TIME 0.45

// Imported from g_weapon.c
#define	MACHINEGUN_DAMAGE			7
#define	MACHINEGUN_TEAM_DAMAGE		5
#define NUM_NAILSHOTS 				15

// Implied by ClientSpawn() in g_client.c
#define	MACHINEGUN_START_AMMO		100
#define	MACHINEGUN_START_TEAM_AMMO	50


// NOTE: The order of this array depends on the order of the weapon_t enumeration in
// bg_public.h
//
// NOTE: The reload times are based on the PM_Weapon() function in bg_pmove.c, but
// are tracked in seconds instead of milliseconds.
//
// NOTE: The damage, splash, and shot values are based on the functions in g_weapon.c
// and g_missile.c
//
// NOTE: All the ranges add 14 unit to them because CalcMuzzlePoint() in g_weapon.c
// starts each trace 14 units out from the attacking player.  They also add an extra
// 21 units (about 15 * sqrt(2)) to compensate for the target player's bounding box,
// as defined by playerMins and playerMaxes in g_client.c.  This is a total of 35
// extra units for every ranged weapon.

weapon_stats_t  weapon_stats[WP_NUM_WEAPONS] = {
	// WP_NONE
	{
	 "No Weapon",				// Name
	 SERVER_FRAME_DURATION,		// Reload time
	 1,							// Shots per fire
	 0,							// Damage per direct hit
	 0,							// Damage per splash hit
	 0,							// Splash radius
	 0,							// Speed, or 0 for instant hit
	 0,							// Range, or 0 for no range
	 0,							// Spread angle
	 WSF_NONE,					// Flags
	 0,							// Starting ammo, or 0 for not a starting weapon
	 0.5,						// Accuracy
	 },

	// WP_GAUNTLET
	{
	 "Gauntlet",				// Name
	 .4,						// Reload time
	 1,							// Shots per fire
	 25,						// Damage per direct hit
	 0,							// Damage per splash hit
	 0,							// Splash radius
	 0,							// Speed, or 0 for instant hit
	 32 + 35,					// Range, or 0 for no range
	 0,							// Spread angle in degrees
	 WSF_MELEE,					// Flags
	 -1,						// Starting ammo, or 0 for not a starting weapon
	 0.5,						// Accuracy
	 },

	// WP_MACHINEGUN
	// NOTE: Sometimes the damage value changes
	// See LevelWeaponUpdateGametype() for more details
	// NOTE: Spread value is atan(MACHINEGUN_SPREAD / 8192)
	// See Bullet_Fire() in g_weapon.c for more details
	{
	 "Machinegun",				// Name
	 .1,						// Reload time
	 1,							// Shots per fire
	 MACHINEGUN_DAMAGE,			// Damage per direct hit
	 0,							// Damage per splash hit
	 0,							// Splash radius
	 0,							// Speed, or 0 for instant hit
	 0,							// Range, or 0 for no range
	 1.4,						// Spread angle in degrees
	 WSF_NONE,					// Flags
	 MACHINEGUN_START_AMMO,		// Starting ammo, or 0 for not a starting weapon
	 0.5,						// Accuracy
	 },

	// WP_SHOTGUN
	// NOTE: Spread value is atan(DEFAULT_SHOTGUN_SPREAD / 8192)
	// See ShotgunPattern() in g_weapon.c for more details
	{
	 "Shotgun",					// Name
	 1.0,						// Reload time
	 DEFAULT_SHOTGUN_COUNT,		// Shots per fire
	 10,						// Damage per direct hit
	 0,							// Damage per splash hit
	 0,							// Splash radius
	 0,							// Speed, or 0 for instant hit
	 0,							// Range, or 0 for no range
	 4.9,						// Spread angle in degrees
	 WSF_NONE,					// Flags
	 0,							// Starting ammo, or 0 for not a starting weapon
	 0.5,						// Accuracy
	 },

	// WP_GRENADE_LAUNCHER
	// NOTE: These are launched at an angle, so they do not correct views
	// NOTE: Technically the grenades can go further than their range, but it's hard to do so
	// and the grenades really shouldn't be used then anyway
	{
	 "Grenade Launcher",		// Name
	 .8,						// Reload time
	 1,							// Shots per fire
	 100,						// Damage per direct hit
	 100,						// Damage per splash hit
	 150,						// Splash radius
	 700,						// Speed, or 0 for instant hit
	 512 + 35,					// Range, or 0 for no range
	 0,							// Spread angle in degrees
	 WSF_DELAY,					// Flags
	 0,							// Starting ammo, or 0 for not a starting weapon
	 0.5,						// Accuracy
	 },

	// WP_ROCKET_LAUNCHER
	{
	 "Rocket Launcher",			// Name
	 .8,						// Reload time
	 1,							// Shots per fire
	 100,						// Damage per direct hit
	 100,						// Damage per splash hit
	 120,						// Splash radius
	 900,						// Speed, or 0 for instant hit
	 0,							// Range, or 0 for no range
	 0,							// Spread angle in degrees
	 WSF_NONE,					// Flags
	 0,							// Starting ammo, or 0 for not a starting weapon
	 0.5,						// Accuracy
	 },

	// WP_LIGHTNING
	{
	 "Lightning Gun",			// Name
	 .05,						// Reload time
	 1,							// Shots per fire
	 8,							// Damage per direct hit
	 0,							// Damage per splash hit
	 0,							// Splash radius
	 0,							// Speed, or 0 for instant hit
	 LIGHTNING_RANGE + 35,		// Range, or 0 for no range
	 0,							// Spread angle in degrees
	 WSF_NONE,					// Flags
	 0,							// Starting ammo, or 0 for not a starting weapon
	 0.5,						// Accuracy
	 },

	// WP_RAILGUN
	{
	 "Railgun",					// Name
	 1.5,						// Reload time
	 1,							// Shots per fire
	 100,						// Damage per direct hit
	 0,							// Damage per splash hit
	 0,							// Splash radius
	 0,							// Speed, or 0 for instant hit
	 0,							// Range, or 0 for no range
	 0,							// Spread angle in degrees
	 WSF_NONE,					// Flags
	 0,							// Starting ammo, or 0 for not a starting weapon
	 0.5,						// Accuracy
	 },

	// WP_PLASMAGUN
	{
	 "Plasma Gun",				// Name
	 .1,						// Reload time
	 1,							// Shots per fire
	 20,						// Damage per direct hit
	 15,						// Damage per splash hit
	 20,						// Splash radius
	 2000,						// Speed, or 0 for instant hit
	 0,							// Range, or 0 for no range
	 0,							// Spread angle in degrees
	 WSF_NONE,					// Flags
	 0,							// Starting ammo, or 0 for not a starting weapon
	 0.5,						// Accuracy
	 },

	// WP_BFG
	{
	 "BFG10K",					// Name
	 .2,						// Reload time
	 1,							// Shots per fire
	 100,						// Damage per direct hit
	 100,						// Damage per splash hit
	 120,						// Splash radius
	 2000,						// Speed, or 0 for instant hit
	 0,							// Range, or 0 for no range
	 0,							// Spread angle in degrees
	 WSF_NONE,					// Flags
	 0,							// Starting ammo, or 0 for not a starting weapon
	 0.5,						// Accuracy
	 },

	// WP_GRAPPLING_HOOK
	{
	 "Grappling Hook",			// Name
	 .4,						// Reload time
	 1,							// Shots per fire
	 0,							// Damage per direct hit
	 0,							// Damage per splash hit
	 0,							// Splash radius
	 800,						// Speed, or 0 for instant hit
	 0,							// Range, or 0 for no range
	 0,							// Spread angle in degrees
	 WSF_NONE,					// Flags
	 0,							// Starting ammo, or 0 for not a starting weapon
	 0.5,						// Accuracy
	 },

#ifdef MISSIONPACK
	// WP_NAILGUN
	// NOTE: Actual speed is random: 555 + random() * 1800
	// See fire_nail() in g_missile.c for more details
	{
	 "Nailgun",					// Name
	 1.0,						// Reload time
	 NUM_NAILSHOTS,				// Shots per fire
	 20,						// Damage per direct hit
	 0,							// Splash radius
	 0,							// Damage per splash hit
	 1455,						// Speed, or 0 for instant hit
	 0,							// Range, or 0 for no range
	 0,							// Spread angle in degrees
	 WSF_NONE,					// Flags
	 0,							// Starting ammo, or 0 for not a starting weapon
	 0.5,						// Accuracy
	 },

	// WP_PROX_LAUNCHER
	// NOTE: These are launched at an angle, so they do not correct views
	// NOTE: Technically the mines can go further than their range, but it's hard to do so
	// and the mines really shouldn't be used then anyway
	{
	 "Proximity Launcher",		// Name
	 .8,						// Reload time
	 1,							// Shots per fire
	 100,						// Damage per direct hit
	 100,						// Damage per splash hit
	 150,						// Splash radius
	 700,						// Speed, or 0 for instant hit
	 512 + 35,					// Range, or 0 for no range
	 0,							// Spread angle in degrees
	 WSF_DELAY,					// Flags
	 0,							// Starting ammo, or 0 for not a starting weapon
	 0.5,						// Accuracy
	 },

	// WP_CHAINGUN
	// NOTE: Spread value is atan(CHAINGUN_SPREAD / 8192)
	// See Bullet_Fire() in g_weapon.c for more details
	{
	 "Chaingun",				// Name
	 .03,						// Reload time
	 1,							// Shots per fire
	 7,							// Damage per direct hit
	 0,							// Damage per splash hit
	 0,							// Splash radius
	 0,							// Speed, or 0 for instant hit
	 0,							// Range, or 0 for no range
	 4.2,						// Spread angle in degrees
	 WSF_NONE,					// Flags
	 0,							// Starting ammo, or 0 for not a starting weapon
	 0.5,						// Accuracy
	 },
#endif
};

// List of all common weapon aliases where keys are alias names
// and values are the associated weapon index
entry_string_int_t weapon_aliases[] = {
	// Gauntlet
	{"Gauntlet", WP_GAUNTLET},
	{"Glove", WP_GAUNTLET},

	// Machinegun
	{"Machinegun", WP_MACHINEGUN},
	{"mg", WP_MACHINEGUN},

	// Shotgun
	{"Shotgun", WP_SHOTGUN},
	{"Shotty", WP_SHOTGUN},
	{"sg", WP_SHOTGUN},

	// Grenade Launcher
	{"Grenade Launcher", WP_GRENADE_LAUNCHER},
	{"GrenadeLauncher", WP_GRENADE_LAUNCHER},
	{"Grenades", WP_GRENADE_LAUNCHER},
	{"Grenade", WP_GRENADE_LAUNCHER},
	{"Pills", WP_GRENADE_LAUNCHER},
	{"gl", WP_GRENADE_LAUNCHER},

	// Rocket Launcher
	{"Rocket Launcher", WP_ROCKET_LAUNCHER},
	{"RocketLauncher", WP_ROCKET_LAUNCHER},
	{"Rockets", WP_ROCKET_LAUNCHER},
	{"Rocket", WP_ROCKET_LAUNCHER},
	{"Rocks", WP_ROCKET_LAUNCHER},
	{"rl", WP_ROCKET_LAUNCHER},

	// Lightning Gun
	{"Lightning Gun", WP_LIGHTNING},
	{"LightningGun", WP_LIGHTNING},
	{"Lightning", WP_LIGHTNING},
	{"Shaft", WP_LIGHTNING},
	{"lg", WP_LIGHTNING},

	// Railgun
	{"Railgun", WP_RAILGUN},
	{"Rail", WP_RAILGUN},
	{"rg", WP_RAILGUN},

	// Plasma Gun
	{"Plasma Gun", WP_PLASMAGUN},
	{"PlasmaGun", WP_PLASMAGUN},
	{"Plasma", WP_PLASMAGUN},
	{"Spam-o-matic", WP_PLASMAGUN},
	{"pg", WP_PLASMAGUN},

	// BFG
	{"BFG10K", WP_BFG},
	{"BFG", WP_BFG},
	{"BurlyProtector", WP_BFG},
	{"Sprite", WP_BFG},

	// Grappling Hook
	{"Grappling Hook", WP_GRAPPLING_HOOK},
	{"Grapple", WP_GRAPPLING_HOOK},
	{"Hook", WP_GRAPPLING_HOOK},
	{"gh", WP_GRAPPLING_HOOK},

#ifdef MISSIONPACK
	// Nailgun
	{"Nailgun", WP_NAILGUN},
	{"ng", WP_NAILGUN},

	// Proximity Mine Launcher
	{"Proximity Launcher", WP_PROX_LAUNCHER},
	{"ProximityLauncher", WP_PROX_LAUNCHER},
	{"Prox Mine Launcher", WP_PROX_LAUNCHER},
	{"ProxMineLauncher", WP_PROX_LAUNCHER},
	{"Prox Launcher", WP_PROX_LAUNCHER},
	{"ProxLauncher", WP_PROX_LAUNCHER},
	{"ProxMines", WP_PROX_LAUNCHER},
	{"Mines", WP_PROX_LAUNCHER},

	// Chaingun
	{"Chaingun", WP_CHAINGUN},
	{"cg", WP_CHAINGUN},
#endif

	// End-of-List Entry
	{"", WP_NONE}
};

// Number of weapon aliases (once the list has been sorted by alias name),
// or -1 if the list is unsorted and uncounted.
int             num_weapon_aliases = -1;

/*
==========
WeaponName
==========
*/
char           *WeaponName(int weapon)
{
	if(weapon < 0 || weapon >= WP_NUM_WEAPONS)
		return "UNKNOWN WEAPON";

	return weapon_stats[weapon].name;
}

/*
=========================
WeaponNameForMeansOfDeath
=========================
*/
char           *WeaponNameForMeansOfDeath(int means_of_death)
{
	// Translate the means of death into into a weapon name
	switch (means_of_death)
	{
		case MOD_SHOTGUN:
			return WeaponName(WP_SHOTGUN);
		case MOD_GAUNTLET:
			return WeaponName(WP_GAUNTLET);
		case MOD_MACHINEGUN:
			return WeaponName(WP_MACHINEGUN);
		case MOD_GRENADE:
		case MOD_GRENADE_SPLASH:
			return WeaponName(WP_GRENADE_LAUNCHER);
		case MOD_ROCKET:
		case MOD_ROCKET_SPLASH:
			return WeaponName(WP_ROCKET_LAUNCHER);
		case MOD_PLASMA:
		case MOD_PLASMA_SPLASH:
			return WeaponName(WP_PLASMAGUN);
		case MOD_RAILGUN:
			return WeaponName(WP_RAILGUN);
		case MOD_LIGHTNING:
			return WeaponName(WP_LIGHTNING);
		case MOD_BFG:
		case MOD_BFG_SPLASH:
			return WeaponName(WP_BFG);
#ifdef MISSIONPACK
		case MOD_NAIL:
			return WeaponName(WP_NAILGUN);
		case MOD_CHAINGUN:
			return WeaponName(WP_CHAINGUN);
		case MOD_PROXIMITY_MINE:
			return WeaponName(WP_PROX_LAUNCHER);
		case MOD_KAMIKAZE:
			return "Kamikaze";
		case MOD_JUICED:
			return "Prox mine";
#endif
		case MOD_GRAPPLE:
			return WeaponName(WP_GRAPPLING_HOOK);

			// Force consistant error message
		default:
			return WeaponName(-1);
	}
}

/*
==============
WeaponFromName

Translates a name to a weapon index, using a variety
of abbreviations and aliases.  If the string is a number
that is a well defined weapon index, that weapon index
is returned.  If no match is found, the function returns
WP_NONE.
==============
*/
int WeaponFromName(char *name)
{
	int             weapon;
	entry_string_int_t *alias;

	// Setup the weapon alias lookup table if necessary
	if(num_weapon_aliases < 0)
	{
		// Count the number of alias entries
		//
		// NOTE: This count does not include the termination entry
		num_weapon_aliases = 0;
		while(weapon_aliases[num_weapon_aliases].value != WP_NONE)
			num_weapon_aliases++;

		// Sort the table by alias name
		qsort(weapon_aliases, num_weapon_aliases, sizeof(entry_string_int_t), CompareEntryStringInsensitive);
	}

	// Search for a matching name
	alias = bsearch(name, weapon_aliases, num_weapon_aliases, sizeof(entry_string_int_t), CompareStringEntryStringInsensitive);
	if(alias)
		return alias->value;

	// If the name lookup failed, check for a stringified number
	weapon = atoi(name);
	if((weapon >= WP_NONE) && (weapon < WP_NUM_WEAPONS))
		return weapon;

	// No matching weapon was found
	return WP_NONE;
}

/*
=======================
WeaponPerceivedMaxRange

Bots think weapons can shoot up
to this far away
=======================
*/
int WeaponPerceivedMaxRange(int weapon)
{
	float           range;

	// The weapon is in range for rangeless weapons and distances
	// that are just barely outside the actual range (or closer)
	range = weapon_stats[weapon].range;

	// Cap the range of weapons with infinite range
	if(range <= 0.0)
		return 8192.0;

	// Bots will try to attack a little beyond the attack range,
	// in case the target decides to move a bit closer
	return range * 1.05;
}

/*
=============
WeaponInRange

Check if a weapon appears to be
in range for a given distance.
=============
*/
qboolean WeaponInRange(int weapon, float dist)
{
	return (dist < WeaponPerceivedMaxRange(weapon));
}

/*
===========
WeaponBlast

Determine how much blast damage the weapon deals
to a target the specified distance away.
===========
*/
float WeaponBlast(int weapon, float dist)
{
	weapon_stats_t *ws;

	// Only check for weapons with blast radius
	//
	// NOTE: This is essentially a division by zero sanity check.
	ws = &weapon_stats[weapon];
	if(ws->radius <= 0)
		return 0;

	// Damage is only dealt when the target is inside the blast radius threshold
	if(ws->radius <= dist)
		return 0;

	// Compute blast damage for this distance
	return ws->splash_damage * (1.0 - dist / ws->radius);
}

/*
==============
WeaponCareless

Returns false if this is a weapon the bot should carefully aim
and true if the bot should not be careful when attacking with it.

NOTE: Carefulness is determined soley by the weapon's reload time.  The
faster the weapon reloads, the less it matters whether or not the shot
misses.  It's good to shoot more often in these situations "just in case".
But for long reloads, it's really important that the shot is precisely
accurate.

NOTE: Humans seem to have two kinds of firing modes.  For careful
weapons, they "click once" for one shot, trying to line up each shot
perfectly.  For careless weapons, they "click and hold", just trying to
get the gun in the same general area and hoping some of the shots hit.
==============
*/
qboolean WeaponCareless(int weapon)
{
	weapon_stats_t *ws;

	// Sanity check the requested weapon
	if(weapon < 0 || weapon >= WP_NUM_WEAPONS)
		return qtrue;
	ws = &weapon_stats[weapon];

	// Test if the weapon should be aimed carelessly
	return (ws->reload <= bot_attack_careless_reload.value);
}

/*
================
LevelWeaponSetup

Sets up any required weapon data,
such as the accuracy estimations.
================
*/
void LevelWeaponSetup(void)
{
	int             i, num_weapons;
	float           min_width, width, enemy_angle, damage, damage_per_second[WP_NUM_WEAPONS];
	weapon_stats_t *ws;

	// Find the minimum possible width of an enemy
	min_width = -1;
	for(i = 0; i < 3; i++)
	{
		// Compute the width in this dimension
		width = playerMaxs[i] - playerMins[i];

		// Use it if it's smaller than the previous option
		if(min_width < 0 || width < min_width)
			min_width = width;
	}

	// Determine how many degrees of view space an enemy takes up at a
	// typical distance from the bot
	//
	// NOTE: The minimum width is divided by two because generally the bot will
	// aim towards the center of the enemy.  This computes the angle between two
	// rays from the bot: from the bot to the center of the enemy and the bot to
	// the edge of the enemy.  This angle is then doubled to compute the full
	// angular width of the enemy.
	//
	// NOTE: The "sort of far away" distance is used because spread on weapons
	// make them better up close.  As a result, enemies consciously try to stay
	// far away from spread weapons to make them worse.  Assuming the enemy will
	// stay close would make the weapon seem better than it is, and really isn't
	// a reasonable assumption.
	enemy_angle = 2 * RAD2DEG(atan2(min_width / 2, (ZCD_MID + ZCD_FAR) / 2));

	// Compute correction roots and accuracies for all weapons
	num_weapons = 0;
	for(i = WP_NONE + 1; i < WP_NUM_WEAPONS; i++)
	{
		// Cache the correction root first
		ws = &weapon_stats[i];

		// FIXME: Bots still don't want to touch the railgun using this estimate.
		// The problem is that DPS is not a proper model for the railgun, since
		// the total damage it deals at time T is of the for acc * (1 + T/Reload),
		// not acc * T/Reload.  It's a flaw with the entire weapon selection
		// system, which should be based on TTD (Time to Death) not DPS (Damage
		// per second).

		// Assume near perfect accuracy
		ws->accuracy = 0.95;

		// Weapons without large blast radius miss more
		if(ws->radius < 100)
			ws->accuracy *= .8 + .2 * (ws->radius / 100.0);

		// Missile weapons miss more than instant hit weapons
		if(ws->speed && ws->speed < 2500)
			ws->accuracy *= .5 + .5 * (ws->speed / 2500.0);

		// Short range weapons can suck at times
		if(ws->range && ws->range < 768.0)
			ws->accuracy *= ws->range / 768.0;

		// Weapons with spread larger than the typical enemy width will sometimes
		// miss even if the attacker is aimed perfectly
		if(ws->spread > enemy_angle)
			ws->accuracy *= enemy_angle / ws->spread;

		// Weapons fired carelessly obviously incur lower accuracy
		if(WeaponCareless(i))
			ws->accuracy *= 0.4;

		// This weapon averages this much damage per second of fire
		damage = (ws->damage > ws->splash_damage ? ws->damage : ws->splash_damage);
		damage *= ws->accuracy * ws->shots / ws->reload;

		// Track this weapon's damage per second if it's a damaging weapon
		if(damage > 0)
			damage_per_second[num_weapons++] = damage;
	}

	// Determine the median damage dealt by all available weapons
	qsort(damage_per_second, num_weapons, sizeof(float), CompareEntryFloat);
	damage_per_second_typical = damage_per_second[num_weapons / 2];

	// Setup accuracy statistics
	AccuracySetup();
}

/*
=========================
LevelWeaponUpdateGametype

NOTE: I'm really not happy that the server does this-- changes
the machinegun damage based on the game type.  My prefered fix
would be to make the machinegun always deal 5 damage per bullet,
but start with more ammo in free-for-all.  That, of course, is
a game design fix, which is beyond the scope of this AI.
=========================
*/
void LevelWeaponUpdateGametype()
{
	if(gametype == GT_TEAM)
	{
		weapon_stats[WP_MACHINEGUN].damage = MACHINEGUN_TEAM_DAMAGE;
		weapon_stats[WP_MACHINEGUN].start_ammo = MACHINEGUN_START_TEAM_AMMO;
	}
	else
	{
		weapon_stats[WP_MACHINEGUN].damage = MACHINEGUN_DAMAGE;
		weapon_stats[WP_MACHINEGUN].start_ammo = MACHINEGUN_START_AMMO;
	}
}

/*
=================
BotWeaponChanging

Returns true if the bot is currently changing weapons.
=================
*/
qboolean BotWeaponChanging(bot_state_t * bs)
{
	return (bs->ps->weaponstate == WEAPON_DROPPING || bs->ps->weaponstate == WEAPON_RAISING);
}

/*
==============
BotWeaponReady

Returns true if the bot will be able to fire its
equipped weapon as of bs->command_time and false if not.

NOTE: This code doesn't check (bs->ps->weaponTime <= 0)
because the server decreases weaponReady before deciding
whether the player's weapon should fire.
==============
*/
qboolean BotWeaponReady(bot_state_t * bs)
{
	// Can't attack if the weapon won't finish doing something by next frame
	if(bs->ps->weaponTime - SERVER_FRAME_DURATION > 0)
		return qfalse;

	// Can't fire when the bot is changing weapons
	//
	// NOTE: This check means that the this function could return true if
	// called at one point in processing and false later in AI processing,
	// if the weapon selection code changes the weapon.  This shouldn't
	// cause any problems, but... if it does, it's the caller's problem,
	// not this function's.
	if(bs->ps->weapon != bs->cmd.weapon)
		return qfalse;

	// The bot can't start firing if they just finished the dropping
	// or raising weapon states (ie. is in the middle of changing weapons)
	if(BotWeaponChanging(bs))
		return qfalse;

	// The weapon can fire next command frame
	return qtrue;
}

/*
============
BotHasWeapon

Returns true if a bot has the specified
weapon and enough ammo to shoot it.
============
*/
qboolean BotHasWeapon(bot_state_t * bs, int weapon, int ammo)
{
	return ((bs->ps->stats[STAT_WEAPONS] & (1 << weapon)) && (bs->ps->ammo[weapon] >= ammo || bs->ps->ammo[weapon] < 0));
}

/*
===================
BotMineDisarmWeapon
===================
*/
int BotMineDisarmWeapon(bot_state_t * bs)
{
	if(BotHasWeapon(bs, WP_PLASMAGUN, 1))
		return WP_PLASMAGUN;

	if(BotHasWeapon(bs, WP_ROCKET_LAUNCHER, 1))
		return WP_ROCKET_LAUNCHER;

	if(BotHasWeapon(bs, WP_BFG, 1))
		return WP_BFG;

	return WP_NONE;
}

/*
=================
BotActivateWeapon

Pick a weapon the bot could use to
activate a shootable button.
=================
*/
int BotActivateWeapon(bot_state_t * bs)
{
	if(BotHasWeapon(bs, WP_MACHINEGUN, 1))
		return WP_MACHINEGUN;

	if(BotHasWeapon(bs, WP_SHOTGUN, 1))
		return WP_SHOTGUN;

	if(BotHasWeapon(bs, WP_PLASMAGUN, 1))
		return WP_PLASMAGUN;

	if(BotHasWeapon(bs, WP_LIGHTNING, 1))
		return WP_LIGHTNING;

#ifdef MISSIONPACK
	if(BotHasWeapon(bs, WP_CHAINGUN, 1))
		return WP_CHAINGUN;

	if(BotHasWeapon(bs, WP_NAILGUN, 1))
		return WP_NAILGUN;
#endif

	if(BotHasWeapon(bs, WP_RAILGUN, 1))
		return WP_RAILGUN;

	if(BotHasWeapon(bs, WP_ROCKET_LAUNCHER, 1))
		return WP_ROCKET_LAUNCHER;

	if(BotHasWeapon(bs, WP_BFG, 1))
		return WP_BFG;

	return bs->weapon;
}

/*
=============
BotDamageRate

Estimate how quickly the bot believes damage can be dealt per
millisecond to targets in the specified aim zone.  The weapon list
argument is a bitmask list of weapons that should be considered
for sustained attack in that zone.  (Weapon "i" is permitted if
(weapon_list & (1<<i)) is true.)  If the "splash" boolean value is
false and a weapon with splash damage is specified, the bot only
counts direct hits (presumably because the target has a battlesuit).

NOTE: A list of the bot's currently usable weapons is precomputed
and cached in bs->weapons_available.  See BotActionIngame() in
ai_action.c for more information.

NOTE: This rating does not count damage modifiers such as Quad
Damage or Doubler.
=============
*/
float BotDamageRate(bot_state_t * bs, unsigned int weapon_list, combat_zone_t * zone, qboolean splash)
{
	int             i, weapon;
	float           damage, rate, best_rate;
	bot_accuracy_t  acc;

	// Find the maximal damage rate among all allowed weapons
	best_rate = 0.0;
	for(weapon = WP_NONE + 1, weapon_list >>= weapon; weapon_list; weapon++, weapon_list >>= 1)
	{
		// Ignore unspecified weapons
		if(!(weapon_list & 0x1))
			continue;

		// Ignore weapons clearly out of range for the zone
		if(!WeaponInRange(weapon, zone->dist))
			continue;

		// Extract the accuracy data record for this weapon and zone if possible
		BotAccuracyRead(bs, &acc, weapon, zone);
		if(acc.time <= 0.0)
			continue;

		// Determine total damage the weapon inflicted while in this zone
		damage = acc.direct.damage;
		if(splash)
			damage += acc.splash.damage;

		// The damage rate equals total damage dealt divided by time spent firing
		rate = damage / acc.time;
		if(best_rate < rate)
			best_rate = rate;
	}

	// Return the optimal rate (possibly zero)
	return best_rate;
}

/*
===============
BotTargetWeapon

Determine the best weapon for shooting a target
(presumably bs->aim_enemy).
===============
*/
int BotTargetWeapon(bot_state_t * bs)
{
	int             health, ammo, weapon, best_weapon;
	int             required_hits, expected_hits, required_fires;
	float           shot_hit_rate, attack_rate;
	float           damage_factor, reload_factor, reload;
	float           time, hits, damage, total_damage;
	float           damage_rate, best_damage_rate;
	qboolean        blast;

#ifdef DEBUG_AI
	float           old_damage_rate;
#endif
	bot_accuracy_t  acc;
	weapon_stats_t *ws;

	// Don't select anything new when changing weapons
	if(BotWeaponChanging(bs))
		return bs->weapon;

#ifdef DEBUG_AI
	// If the bot is forced to use a weapon, do so
	if((bs->use_weapon > WP_NONE) && (bs->use_weapon < WP_NUM_WEAPONS))
	{
		// Select the requested weapon
		weapon = bs->use_weapon;

		// Make sure the bot has the weapon and ammo for it
		//
		// NOTE: This code actually modifies something in the bot's player state.
		// Almost no other code in Brainworks does this.
		bs->ps->stats[STAT_WEAPONS] |= (1 << weapon);
		if(bs->ps->ammo[weapon] >= 0)
			bs->ps->ammo[weapon] = 200;

		return weapon;
	}
#endif

	// Estimate the enemy target's health
	health = BotEnemyHealth(bs);

	// Check if the target can receive blast damage
	if(bs->aim_enemy)
		blast = !(bs->aim_enemy->client->ps.powerups[PW_BATTLESUIT]);
	else
		blast = qtrue;

	// Check for powerups that could change the rate of reload or damage
	//
	// NOTE: Technically this code is incorrect because a powerup could run out
	// in the middle of attacking someone, but the bot assumes it lasts forever.
	// This bug doesn't seem to have a major impact on the final decision, however,
	// and it would take an awful lot of trouble to properly handle damage rates
	// and reload rates that change midway through combat.
	damage_factor = 1.0;
	reload_factor = 1.0;
	if(bs->ps->powerups[PW_QUAD])
		damage_factor *= g_quadfactor.value;
#ifdef MISSIONPACK
	if(bs->ps->powerups[PW_DOUBLER])
		damage_factor *= 2;

	if(bs->ps->powerups[PW_SCOUT])
		reload_factor /= 1.5;
	else if(bs->ps->powerups[PW_AMMOREGEN])
		reload_factor /= 1.3;
	else
#endif
	if(bs->ps->powerups[PW_HASTE])
		reload_factor /= 1.3;

#ifdef DEBUG_AI
	// Assume the current weapon deals 0 damage per second
	old_damage_rate = 0;
#endif

	// Check each weapon for possible use in this aim zone
	best_weapon = bs->weapon;
	best_damage_rate = 0;
	for(weapon = WP_NONE + 1; weapon < WP_NUM_WEAPONS; weapon++)
	{
		// Don't consider weapons the bot doesn't have
		if(!(bs->ps->stats[STAT_WEAPONS] & (1 << weapon)))
			continue;

		// Never consider weapons that have run out of ammo
		ammo = bs->ps->ammo[weapon];
		if(!ammo)
			continue;

		// Don't use weapons that are out of range
		if(!WeaponInRange(weapon, bs->aim_zone.dist))
		{
			// Because this weapon has ammo, use this as a default if necessary
			if(best_damage_rate <= 0)
				best_weapon = weapon;
			continue;
		}

		// Ignore blank accuracy records
		//
		// NOTE: This should never happen, but check just to be safe.
		BotAccuracyRead(bs, &acc, weapon, &bs->aim_zone);
		if(acc.shots <= 0)
			continue;

		// Estimate what percent of combat the bot will fire this weapon
		attack_rate = BotAttackRate(bs, &acc);

		// Start estimating how much time it will take to score the required number of hits
		//
		// NOTE: The bot will always have to wait at least one server frame for its attack
		// command to get processed.  This also prevents possible division by zero when
		// computing the weapon's damage rate.
		//
		// NOTE: The weapon time is tracked in milliseconds, not seconds
		time = bs->ps->weaponTime * .001 + SERVER_FRAME_DURATION;
		ws = &weapon_stats[weapon];

		// Include weapon switching time if the bot would have to switch to a new weapon
		if(weapon != bs->weapon)
			time += WEAPON_SWITCH_TIME;

		// Determine total hits and damage scored by this weapon from this combat location
		hits = acc.direct.hits;
		damage = acc.direct.damage;
		if(blast)
		{
			hits += acc.splash.hits;
			damage += acc.splash.damage;
		}
		if(!hits || !damage)
			continue;

		// Convert from total damage to expected damage per hit
		damage *= damage_factor / hits;

		// Compute how many hits this weapon needs to kill the opponent
		required_hits = ceil(health / damage);

		// Estimate the percent of this weapon's shots that hit
		shot_hit_rate = hits / acc.shots;

		// Estimate how many hits the bot can get before running out of ammo
		if(ammo > 0)
			expected_hits = ceil(ammo * ws->shots * shot_hit_rate);
		else
			expected_hits = required_hits;

		// The weapon reloads this fast
		reload = ws->reload * reload_factor;

		// Check if the bot has enough ammo to kill the enemy without switching weapons
		if(required_hits <= expected_hits)
		{
			// Calculate the number of shots required to get enough hits
			// and the number of weapon fires to unload this many shots
			//
			// NOTE: This is not equivalent to ceil(required_hits / (shot_hit_rate * ws->shots))
			required_fires = ceil(ceil(required_hits / shot_hit_rate) / ws->shots);

			// Consider the the total time required to fire this many shots.  Don't
			// count the last shot because the enemy will die before the weapon reloads.
			time += (reload * required_fires / attack_rate) - reload;

			// The bot will stop attacking once the enemy is dead
			expected_hits = required_hits;
		}

		// If not, the bot does the best it can
		else
		{
			// Plan on emptying the gun of its ammo
			time += reload * ammo / attack_rate;

			// After it runs out of ammo, it will have to switch weapons (possibly a seond time)
			time += WEAPON_SWITCH_TIME;
		}

		// Determine how much damage the bot will deal per unit of time with this weapon
		total_damage = expected_hits * damage;
		if(total_damage > health)
			total_damage = health;
		damage_rate = total_damage / time;

		// Slightly favor the current weapon to avoid rampant weapon switches
		//
		// NOTE: This is in addition to the natural favoritism of the current
		// weapon due to the time incurred changing weapons.  That penalty works
		// well for situations where the target is low on health, but isn't
		// sufficient when a weapon is really good in one situation and bad in
		// another.  Someone could exploit a bot by constantly moving in and out
		// of close range, making the bot want to switch between short and long
		// range weapons.  This threshold discourages weapon switching to a more
		// reasonable extent.
		if(weapon == bs->weapon)
		{
#ifdef DEBUG_AI
			// Save this value for posterity's sake
			old_damage_rate = damage_rate;
#endif

			// Encourage the bot to continue using this weapon
			damage_rate *= 1.1;
		}

		// Don't use this weapon if it has a worse damage rate than other considerations
		if(damage_rate < best_damage_rate)
			continue;

		// Consider this weapon
		best_weapon = weapon;
		best_damage_rate = damage_rate;
	}

#ifdef DEBUG_AI
	// Announce changes in weapon selection
	if((bs->weapon != best_weapon) && (bs->debug_flags & BOT_DEBUG_INFO_WEAPON))
	{
		BotAI_Print(PRT_MESSAGE,
					"%s: Weapon select: Using %s (%.0f/sec) instead of %s (%.0f/sec)\n",
					EntityNameFast(bs->ent), WeaponName(best_weapon), best_damage_rate, WeaponName(bs->weapon), old_damage_rate);
	}
#endif

	// Tell the caller what the best weapon for attacking was
	return best_weapon;
}

/*
==============
BotBlastDamage

Determine how much damage a blast from "weapon" detonating
at "center" deals, not dealing damage to entity "ignore".
The information is stored in the "blast" input argument.
==============
*/
void BotBlastDamage(bot_state_t * bs, int weapon, vec3_t center, damage_multi_t * blast, gentity_t * ignore)
{
	int             i, num_contacted;
	int             contacted[MAX_GENTITIES];
	float           radius, dist, damage;
	vec3_t          offset, mins, maxs;
	gentity_t      *ent;
	damage_catagory_t *group;

	// Reset the damage information
	memset(blast, 0, sizeof(damage_multi_t));

	// Only check for weapons with blast radius
	radius = weapon_stats[weapon].radius;
	if(radius <= 0)
		return;

	// Compute the bounding box containing all entities that could possibly be
	// damaged from the blast radius.
	VectorSet(offset, radius, radius, radius);
	VectorAdd(center, offset, maxs);
	VectorSubtract(center, offset, mins);

	// Get a list of all entities possibly within this bounding box
	num_contacted = trap_EntitiesInBox(mins, maxs, contacted, MAX_GENTITIES);

	// Estimate damage dealt to each nearby entity
	//
	// NOTE: This duplicates much of the code in G_RadiusDamage() in g_combat.c.
	for(i = 0; i < num_contacted; i++)
	{
		// Do not tracked the ignored entity
		ent = &g_entities[contacted[i]];
		if(ent == ignore)
			continue;

		// Check if the entity is on the enemy team
		//
		// NOTE: This includes damagable structures on the enemy team,
		// like the Obelisk in Overload.
		if(BotEnemyTeam(bs, ent))
			group = &blast->enemy;

		// Also check for players on the same team that the bot can damage.
		//
		// NOTE: This function purposely ignores self-damage.  It also
		// ignores damagable team structures, like the Obelisk in Overload
		// (which players can't damage even when friend fire is turned on).
		else if(g_friendlyFire.integer && ent->client && BotSameTeam(bs, ent) && bs->ent != ent)
			group = &blast->team;

		// Never count damage to neutrally aligned entities, even if they are
		// damagable (like shot-activated buttons)
		else
			continue;

		// Determine how close the blast shot was to the target's bounding box
		// (in real world coordinates)
		EntityWorldBounds(ent, mins, maxs);
		dist = point_bound_distance(center, mins, maxs);

		// Compute how much damage the blast would deal to entities at this distance
		damage = WeaponBlast(weapon, dist);

		// Update the specific catagory data...
		group->hits++;
		group->total += damage;
		if(group->max < damage)
			group->max = damage;

		// ... And the aggregate data
		blast->all.hits++;
		blast->all.total += damage;
		if(blast->all.max < damage)
			blast->all.max = damage;
	}
}
