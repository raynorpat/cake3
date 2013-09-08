// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_level.c
 *
 * Functions that the bot uses to get information about the level
 *****************************************************************************/

#include "ai_main.h"
#include "ai_vars.h"
#include "ai_level.h"

#include "ai_client.h"
#include "ai_goal.h"
#include "ai_item.h"
#include "ai_region.h"
#include "ai_path.h"
#include "ai_self.h"
#include "ai_vars.h"
#include "ai_waypoint.h"


// True if the bases have been setup
qboolean        base_setup = qfalse;

// Goals representing each base in-game (might have areanum 0 if not set)
bot_goal_t      bases[NUM_BASES];

// Entities associated with these bases (might be NULL)
gentity_t      *base_ents[NUM_BASES];

// The current entity location of the flags (might be NULL)
gentity_t      *flags[NUM_BASES];

// Cached values from LevelTravelTime() of travel times between every set of bases.
int             base_travel_time[NUM_BASES][NUM_BASES];


/*
==========
BotTestAAS
==========
*/
void BotTestAAS(vec3_t origin)
{
	int             areanum;
	aas_areainfo_t  info;

	trap_Cvar_Update(&bot_testsolid);
	trap_Cvar_Update(&bot_testclusters);

	if(!bot_testsolid.integer && !bot_testclusters.integer)
		return;

	if(!trap_AAS_Initialized())
		return;

	areanum = LevelAreaPoint(origin);
	if(areanum)
	{
		if(bot_testsolid.integer)
		{
			BotAI_Print(PRT_MESSAGE, "\remtpy area");
		}
		else
		{
			trap_AAS_AreaInfo(areanum, &info);
			BotAI_Print(PRT_MESSAGE, "\rarea %d, cluster %d       ", areanum, info.cluster);
		}
	}

	else
	{
		if(bot_testsolid.integer)
			BotAI_Print(PRT_MESSAGE, "\r^1SOLID area");
		else
			BotAI_Print(PRT_MESSAGE, "\r^1Solid!                              ");
	}
}

/*
=================
LevelLibrarySetup

Initialize a ton of random data.  Returns
true if the AAS was setup properly and false
if an error occurred.
=================
*/
qboolean LevelLibrarySetup(void)
{
	char            buf[MAX_CHARACTERISTIC_PATH];

	// Set the maxclients and maxentities library variables before calling BotSetupLibrary
	trap_Cvar_VariableStringBuffer("sv_maxclients", buf, sizeof(buf));
	if(!strlen(buf))
		strcpy(buf, "8");
	trap_BotLibVarSet("maxclients", buf);
	Com_sprintf(buf, sizeof(buf), "%d", MAX_GENTITIES);
	trap_BotLibVarSet("maxentities", buf);

	// Bsp checksum
	trap_Cvar_VariableStringBuffer("sv_mapChecksum", buf, sizeof(buf));
	if(strlen(buf))
		trap_BotLibVarSet("sv_mapChecksum", buf);

	// Maximum number of aas links
	trap_Cvar_VariableStringBuffer("max_aaslinks", buf, sizeof(buf));
	if(strlen(buf))
		trap_BotLibVarSet("max_aaslinks", buf);

	// Maximum number of items in a level
	trap_Cvar_VariableStringBuffer("max_levelitems", buf, sizeof(buf));
	if(strlen(buf))
		trap_BotLibVarSet("max_levelitems", buf);

	// Game type
	trap_Cvar_VariableStringBuffer("g_gametype", buf, sizeof(buf));
	if(!strlen(buf))
		strcpy(buf, "0");
	trap_BotLibVarSet("g_gametype", buf);

	// Bot developer mode and log file
#ifdef DEBUG_AI
	trap_BotLibVarSet("bot_developer", bot_debug_path.string);
#else
	trap_BotLibVarSet("bot_developer", "0");
#endif
	trap_BotLibVarSet("log", buf);

	// No chatting
	trap_Cvar_VariableStringBuffer("bot_nochat", buf, sizeof(buf));
	if(strlen(buf))
		trap_BotLibVarSet("nochat", "0");

	// Visualize jump pads
	trap_Cvar_VariableStringBuffer("bot_visualizejumppads", buf, sizeof(buf));
	if(strlen(buf))
		trap_BotLibVarSet("bot_visualizejumppads", buf);

	// Forced clustering calculations
	trap_Cvar_VariableStringBuffer("bot_forceclustering", buf, sizeof(buf));
	if(strlen(buf))
		trap_BotLibVarSet("forceclustering", buf);

	// Forced reachability calculations
	trap_Cvar_VariableStringBuffer("bot_forcereachability", buf, sizeof(buf));
	if(strlen(buf))
		trap_BotLibVarSet("forcereachability", buf);

	// Force writing of AAS to file
	trap_Cvar_VariableStringBuffer("bot_forcewrite", buf, sizeof(buf));
	if(strlen(buf))
		trap_BotLibVarSet("forcewrite", buf);

	// No AAS optimization
	trap_Cvar_VariableStringBuffer("bot_aasoptimize", buf, sizeof(buf));
	if(strlen(buf))
		trap_BotLibVarSet("aasoptimize", buf);

	trap_Cvar_VariableStringBuffer("bot_saveroutingcache", buf, sizeof(buf));
	if(strlen(buf))
		trap_BotLibVarSet("saveroutingcache", buf);

	// Reload instead of cache bot character files
	trap_Cvar_VariableStringBuffer("bot_reloadcharacters", buf, sizeof(buf));
	if(!strlen(buf))
		strcpy(buf, "0");
	trap_BotLibVarSet("bot_reloadcharacters", buf);

	// Base directory
	trap_Cvar_VariableStringBuffer("fs_basepath", buf, sizeof(buf));
	if(strlen(buf))
		trap_BotLibVarSet("basedir", buf);

	// Game directory
	trap_Cvar_VariableStringBuffer("fs_game", buf, sizeof(buf));
	if(strlen(buf))
		trap_BotLibVarSet("gamedir", buf);

	// Cd directory
	trap_Cvar_VariableStringBuffer("fs_cdpath", buf, sizeof(buf));
	if(strlen(buf))
		trap_BotLibVarSet("cddir", buf);

#ifdef MISSIONPACK
	trap_BotLibDefine("MISSIONPACK");
#endif

	// Setup the bot library
	return trap_BotLibSetup();
}

/*
==================
LevelLibraryUpdate

Updates the AI engine's understanding of entities
in the world.

NOTE: Currently this is only needed for navigation
around mobile level objects (eg. moving platforms).
In the past it was needed for the AIs primative item
pickup however.
==================
*/
void LevelLibraryUpdate(void)
{
	int             i;
	gentity_t      *ent;
	bot_entitystate_t state;
	qboolean        valid;

	// Start the library frame, or fail out of this function
	trap_BotLibStartFrame(ai_time);
	if(!trap_AAS_Initialized())
		return;

	//update entities in the botlib
	for(i = 0; i < MAX_GENTITIES; i++)
	{
		ent = &g_entities[i];

		// Check if the entity is valid for inclusion in the library
		valid = (ent->inuse		// In use
				 && ent->r.linked	// In the game
				 && !(ent->r.svFlags & SVF_NOCLIENT)	// Available to clients
				 && (ent->s.eType != ET_MISSILE ||	// Not a missile
					 ent->s.weapon == WP_GRAPPLING_HOOK)	// ... except for grapples
				 && ent->s.eType <= ET_EVENTS	// Not an event-only entity
#ifdef MISSIONPACK
				 && strcmp(ent->classname, "proxmine_trigger")	// Not a proximity mine trigger
#endif
			);

		// Only update movers
		//
		// NOTE: Comment out this block if you want the engine to know
		// about more entities than just movers.
		if(ent->s.eType != ET_MOVER)
			valid = qfalse;

		// Do a null update for untracted entities
		if(!valid)
		{
			trap_BotLibUpdateEntity(i, NULL);
			continue;
		}

		// Create latest entry
		//
		// NOTE: The library uses a strange form of entity structure
		memset(&state, 0, sizeof(bot_entitystate_t));

		VectorCopy(ent->r.currentOrigin, state.origin);
		if(i < MAX_CLIENTS)
			VectorCopy(ent->s.apos.trBase, state.angles);
		else
			VectorCopy(ent->r.currentAngles, state.angles);

		VectorCopy(ent->s.origin2, state.old_origin);
		VectorCopy(ent->r.mins, state.mins);
		VectorCopy(ent->r.maxs, state.maxs);
		state.type = ent->s.eType;
		state.flags = ent->s.eFlags;

		if(ent->r.bmodel)
			state.solid = SOLID_BSP;
		else
			state.solid = SOLID_BBOX;

		state.groundent = ent->s.groundEntityNum;
		state.modelindex = ent->s.modelindex;
		state.modelindex2 = ent->s.modelindex2;
		state.frame = ent->s.frame;
		state.event = ent->s.event;
		state.eventParm = ent->s.eventParm;
		state.powerups = ent->s.powerups;
		state.legsAnim = ent->s.legsAnim;
		state.torsoAnim = ent->s.torsoAnim;
		state.weapon = ent->s.weapon;

		// Update that entry in the library
		trap_BotLibUpdateEntity(i, &state);
	}
}

/*
=============
LevelMapTitle
=============
*/
char           *LevelMapTitle(void)
{
	char            info[1024];
	static char     mapname[128];

	trap_GetServerinfo(info, sizeof(info));
	Q_strncpyz(mapname, Info_ValueForKey(info, "mapname"), sizeof(mapname));

	return mapname;
}

/*
=============
BotMapScripts
=============
*/
void BotMapScripts(bot_state_t * bs)
{
	char           *mapname;

	// NOTE: Never use the func_bobbing in q3tourney6 or mpq3tourney6.
	// FIXME: Is this the fast moving mover to the megahealth or the crusher
	// above the BFG?  In any case, what a stupid hack.  Can the activator
	// code be rewritten so this check isn't necessary?
	mapname = LevelMapTitle();
	if(!Q_stricmp(mapname, "q3tourney6") || !Q_stricmp(mapname, "mpq3tourney6"))
	{
		bs->travel_flags &= ~TFL_FUNCBOB;
	}
}

#define NUM_TRACE_AREAS 16
/*
==============
LevelAreaPoint
==============
*/
int LevelAreaPoint(vec3_t origin)
{
	int             i, area, num_areas, areas[NUM_TRACE_AREAS];
	vec3_t          start, end;

	// Check if this point's area reaches more than zero other areas
	area = trap_AAS_PointAreaNum(origin);
	if(trap_AAS_AreaReachability(area))
		return area;

	// Trace below the point looking for an area with positive reachability count
	// Also look slightly above the entity to account for objects embedded in floors
	VectorSet(start, origin[0], origin[1], origin[2] + 48);
	VectorSet(end, origin[0], origin[1], origin[2] - 48);
	num_areas = trap_AAS_TraceAreas(start, end, areas, NULL, NUM_TRACE_AREAS);

	// Find the closest area with positive reachability
	for(i = 0; i < num_areas; i++)
	{
		if(trap_AAS_AreaReachability(areas[i]))
			return areas[i];
	}

	// Either return the first area found or area 0 (no area)
	return (num_areas ? areas[0] : 0);
}

/*
=================
LevelAreaLocPoint

Looks up the navigation area for a point like LevelAreaPoint()
does, but it also sets the location a point that is guaranteed
to be in that area in "location".  Nothing is set if no area
(area 0) is returned.

NOTE: It's permitted for origin and location to point to the
same vector.
=================
*/
int LevelAreaLocPoint(vec3_t origin, vec3_t location, float start_height, float end_height)
{
	int             i, num_areas, areas[NUM_TRACE_AREAS];
	vec3_t          start, end, points[NUM_TRACE_AREAS];

	// Trace below the point looking for an area with positive reachability count
	VectorSet(start, origin[0], origin[1], origin[2] + start_height);
	VectorSet(end, origin[0], origin[1], origin[2] + end_height);
	num_areas = trap_AAS_TraceAreas(start, end, areas, points, NUM_TRACE_AREAS);

	// Find the closest area with positive reachability
	for(i = 0; i < num_areas; i++)
	{
		if(trap_AAS_AreaReachability(areas[i]))
		{
			VectorCopy(points[i], location);
			return areas[i];
		}
	}

	// If any valid areas were found, use the first one
	if(num_areas)
	{
		VectorCopy(points[0], location);
		return areas[0];
	}

	// Fail
	return 0;
}

/*
===============
LevelAreaEntity
===============
*/
int LevelAreaEntity(gentity_t * ent)
{
	// Make absolutely sure this pointer is valid
	if(ent < &g_entities[0])
		return 0;
	if(!ent->inuse)
		return 0;

	// Use cached player data if possible
	if(ent->s.number < MAX_CLIENTS)
		return PlayerArea(ent);

	// Use cached item data for normal items if possible
	if(ent->s.eType == ET_ITEM)
		return ItemArea(ent);

	// Look up the point location
	return LevelAreaPoint(ent->r.currentOrigin);
}

/*
===============
LevelTravelTime

Estimates the time it will take to travel from the starting
area and location to the ending area and location.  If the
ending location is NULL, the travel time to any spot in the
end area will be returned instead.  The travel path only uses
the specified travel flags (tfl).  Returned travel times are
in seconds (not milliseconds or hundredths of a second).  The
time -1 will be returned if the path is unroutable with the
given travel flags.

NOTE: For some unfathomable reason, the engine has no interface
for estimating travel times between two locations.  It can only
estimate travel from a starting area and location to an ending
area.  If this travel time is used, ignoring the ending location,
the travel estimates will always be a little too fast (small).
This causes minor problems when estimating long distances, but
the problem magnifies itself when estimating short distances.

For example, suppose the estimate is too fast by 2 seconds.
There isn't a big difference between 10 and 12 seconds of travel
(17% off), but the difference between .1 and 2.1 seconds is
dangerously bad (95% off).

The best patch I can think of for this engine inadequacy is to
estimate where the route contacts the ending area using the
trap_AAS_PredictRoute() system call and then computing the
remaining distance (and thereby time) from the route endpoint
and the ending location.  The trap_AAS_PredictRoute() is
dangerous, however.  It doesn't always work properly and has
some bugs with its time estimates.  It's also really slow.
Unfortunately, its the only way to get accurate estimates.  The
function is only used for short estimates to save most of the
overhead.

Another idea I tried was putting a lower bound on the travel
time by the direct distance between the two input points,
divided by the top movement speed.  Unfortunately, this estimate
is inaccurate when level elements increase player speed.  In
other words, jump pads and teleporters mess up the estimates.

The real solution is to crack open the engine and fix the
travel time estimation function to estimate to a point, not
just an area.  Alternatively, fixing and speed optimizing the
route prediction would be appreciated.
===============
*/
float LevelTravelTime(int start_area, vec3_t start_loc, int end_area, vec3_t end_loc, int tfl)
{
	int             time_cs;
	float           time;
	aas_predictroute_t route;

	// Use the distance between the locations when estimating travel in the same area
	if(start_area == end_area)
		return Distance(start_loc, end_loc) / (g_speed.value > 0 ? g_speed.value : 320.0);

	// Estimate the travel time to the end area
	//
	// NOTE: This function returns the travel time in centiseconds!  Yes,
	// that's hundredths of a second!  Also, for some unknown reason, it
	// returns 0 for unroutable travel instead of -1.  When the starting
	// and ending area match, it returns 1.  I don't know about you, but
	// if I designed this function, a travel time of "0" implies you are
	// already there and "1" means you'll have to spend 1 unit of time
	// to reach the destination.  An impossible time of "-1" would be
	// associated with the impossible travel action.
	time_cs = trap_AAS_AreaTravelTimeToGoalArea(start_area, start_loc, end_area, tfl);
	if(!time_cs)
		return -1;

	// Convert travel time from centiseconds to seconds
	time = time_cs * .01;

	// Short routes (less than 2 seconds) need more precise times
	//
	// NOTE: trap_AAS_PredictRoute() incorrectly processes its time values.  It's
	// supposed to accept the maximum time in centiseconds, but the value can
	// sometimes be high.  Interpretting the time value in milliseconds can get an
	// estimate on the correct order of magnitude, but it's still clearly wrong.
	// In any case, the time value is multiplied by 10 for safety's sake.
	//
	// NOTE: Long routes need precise times as well, but not as badly.  Unfortunately,
	// in addition to being buggy, trap_AAS_PredictRoute() is horrendously slow,
	// especially for long routes.  But even for short routes, the runtime is a factor
	// of 6 times slower!  This seems to be the price of accurate estimates, but
	// somehow I think it shouldn't be so expensive.  I'm sure it's possible to
	// optimize the engine code so the point-to-area and point-to-point travel
	// estimates have nearly equivalent run times.
	if(time < 2.0)
	{
		// Predict where the route from the starting location enters the ending area
		trap_AAS_PredictRoute(&route, start_area, start_loc, end_area, tfl, 32, time * 100 * 10, 0, 0, 0, 0);

		// Add the extra distance from the route endpoint to the destination if possible
		if(!(route.stopevent & RSE_NOROUTE) && (route.endarea == end_area))
			time += Distance(route.endpos, end_loc) / (g_speed.value > 0 ? g_speed.value : 320.0);
	}

	// Return the final time estimate
	return time;
}

// Count of the number of times an area has been disabled.  An area is only enabled
// if no obstacles have disabled it (ie. the disable counter is zero).
//
// NOTE: Very bad things will happen exceeds this many routing areas
byte            area_disable_count[16384];

/*
======================
LevelEnableRoutingArea

Enable the bot's routing (for travel time
and predict route) through a specified area

NOTE: Very bad things will happen if this function is not called
the same number of times as LevelDisableRoutingArea.  This is
because the engine's disable area routing flags apply to all bots.
If one bot forgets to enable routing through an area, it will
prevent all other bots from moving through that area.  Just be
very careful when using this function.
======================
*/
void LevelEnableRoutingArea(int area)
{
	// Only decrement the disable counter if the counter is positive
	if(area_disable_count[area] <= 0)
		return;

	// Only enable routing if the disable counter reaches zero
	if(--area_disable_count[area] == 0)
		trap_AAS_EnableRoutingArea(area, qtrue);
}

/*
======================
LevelDisableRoutingArea

Disable the bot's routing (for travel time
and predict route) through a specified area

NOTE: Very bad things will happen if this function is not called
the same number of times as LevelEnableRoutingArea.  This is
because the engine's disable area routing flags apply to all bots.
If one bot forgets to enable routing through an area, it will
prevent all other bots from moving through that area.  Just be
very careful when using this function.
======================
*/
void LevelDisableRoutingArea(int area)
{
	// Disable routing if the area wasn't already disabled,
	// but always increment the counter
	if(area_disable_count[area]++ == 0)
		trap_AAS_EnableRoutingArea(area, qfalse);
}

/*
================
LevelAreasNearby

Check if a player can easily travel from the start area to the end area

NOTE: This function is itself pretty slow.  It should only be used
when it's expected to save much more processing time (as with obstacle
route planning).
================
*/
#define NEARBY_AREAS 32			// 32 areas
#define NEARBY_TIME 200			// 2.00 seconds
qboolean LevelAreasNearby(int start_area, vec3_t start_origin, int end_area)
{
	aas_predictroute_t route;

	// Non-routable areas are always far from each other
	if(!start_area || !end_area)
		return qfalse;

	// Most of the time this will be true
	if(start_area == end_area)
		return qtrue;

	// Check if the end area is reachable within a few seconds and areas without hitting a mover
	trap_AAS_PredictRoute(&route, start_area, start_origin, end_area, TFL_DEFAULT,
						  NEARBY_AREAS, NEARBY_TIME, RSE_ENTERCONTENTS, AREACONTENTS_MOVER, 0, 0);

	// The areas are nearby if the route completed in less than the allocated areas and time
	return ((!route.stopevent) && (route.numareas < NEARBY_AREAS) && (route.time < NEARBY_TIME));
}

/*
===================
LevelBaseTravelTime
===================
*/
int LevelBaseTravelTime(int from_base, int to_base)
{
	if(from_base < 0 || from_base >= NUM_BASES || to_base < 0 || to_base >= NUM_BASES)
		return -1;

	return base_travel_time[from_base][to_base];
}

/*
==============
LevelBaseReset
==============
*/
void LevelBaseReset(void)
{
	int             i, j;

	// Initialize the base goals, entities, and travel times
	base_setup = qfalse;
	for(i = 0; i < NUM_BASES; i++)
	{
		GoalReset(&bases[i]);
		base_ents[i] = NULL;
		flags[i] = NULL;

		for(j = 0; j < NUM_BASES; j++)
			base_travel_time[i][j] = -1;
	}
}

/*
==============
LevelBaseSetup
==============
*/
void LevelBaseSetup(void)
{
	int             i, j, type;
	char           *spawn_name[NUM_BASES];
	char           *goal_name[NUM_BASES];
	gentity_t      *ent;

	if(base_setup)
		return;

	// The base entities (obelisks or flags) can be processed after item regions are setup
	if(!CanProcessItems())
		return;

	// Obviously bases can only be setup in game modes with bases in them
	if(!(game_style & GS_BASE))
		return;

	// It's okay to set up the bases now
	base_setup = qtrue;

	// The maps have different entities for CTF flag stands and Obelisk altars
	if(game_style & GS_FLAG)
	{
		spawn_name[0] = "team_CTF_redflag";
		spawn_name[1] = "team_CTF_blueflag";
		spawn_name[2] = "team_CTF_neutralflag";
		goal_name[0] = "Red Flag";
		goal_name[1] = "Blue Flag";
		goal_name[2] = "Neutral Flag";

		type = ET_ITEM;
	}
	else
	{
		spawn_name[0] = "team_redobelisk";
		spawn_name[1] = "team_blueobelisk";
		spawn_name[2] = "team_neutralobelisk";
		goal_name[0] = "Red Obelisk";
		goal_name[1] = "Blue Obelisk";
		goal_name[2] = "Neutral Obelisk";

		type = ET_TEAM;
	}

	// Scan for each entity in-game, if it exists
	for(ent = &g_entities[0]; ent < &g_entities[level.numEntities]; ent++)
	{
		// Only accept team items
		if(!ent->inuse || ent->s.eType != type)
			continue;

		// Ignored dropped items (unlikely case, but it's good to be safe)
		if(ent->flags & FL_DROPPED_ITEM)
			continue;

		// Search for a spawn name match
		for(i = 0; i < NUM_BASES; i++)
		{
			// Setup a base goal if the entity spawn name matchs
			if(!Q_stricmp(ent->classname, spawn_name[i]) && GoalEntity(&bases[i], ent))
			{
				base_ents[i] = ent;
				break;
			}
		}
	}

	// Setup the goals for items not found in game
	// NOTE: This code still creates the middle base for CTF and Overload
	// because the middle base is used for alternate route calculations,
	// even though no entity is spawned at that location.  Unfortunately
	// there will be no entity number on these goals, but hopefully things
	// will work out anyway.
	for(i = 0; i < NUM_BASES; i++)
	{
		// Ignore goals that have already been setup
		if(bases[i].areanum > 0)
			continue;

		// Try to access the base directly by name.  Unfortunately it won't
		// have an entity number.
		if(trap_BotGetLevelItemGoal(-1, goal_name[i], &bases[i]) >= 0)
		{
			bases[i].entitynum = -1;
			continue;
		}

		// Technically the mid base isn't necessary for CTF and Overload
		if(i == MID_BASE)
		{
			if(gametype == GT_CTF
#ifdef MISSIONPACK
			   || gametype == GT_OBELISK
#endif
				)
				continue;
		}

		// Complain that the base couldn't be found
		BotAI_Print(PRT_WARNING, "Could not locate %s\n", goal_name[i]);
	}

	// Compute and cache travel times between each pair of bases
	//
	// NOTE: These values might not be commutitive, so it's
	// important to compute the a->b and b->a cases separately.
	for(i = 0; i < NUM_BASES; i++)
	{
		for(j = 0; j < NUM_BASES; j++)
		{
			// Compute time from base i to base j
			base_travel_time[i][j] =
				LevelTravelTime(bases[i].areanum, bases[i].origin, bases[j].areanum, bases[j].origin, TFL_DEFAULT);
		}
	}
}

/*
=============
LevelFindFlag

Scan the level to find the entity holding a requested flag
(either a flag stand, a dropped flag, or a client carrying the flag).
=============
*/
gentity_t      *LevelFindFlag(gentity_t * last_flag, gentity_t * base, char *classname, int powerup)
{
	gentity_t      *ent;

	// Check if last frame's entity still has the flag
	if(last_flag && last_flag->inuse)
	{
		if((last_flag->s.eType == ET_ITEM) &&
		   (last_flag->r.contents == CONTENTS_TRIGGER) && (!Q_stricmp(last_flag->classname, classname)))
			return last_flag;
		if((last_flag->client) && (last_flag->client->ps.powerups[powerup]))
			return last_flag;
	}

	// Check if the flag is at the base
	if(base && base->r.contents == CONTENTS_TRIGGER)
		return base;

	// Check for a client that picked up this flag
	for(ent = &g_entities[0]; ent < &g_entities[maxclients]; ent++)
	{
		if(ent->inuse && ent->client->ps.powerups[powerup])
			return ent;
	}

	// Continue scanning rest of entities for dropped flags
	while(ent < &g_entities[level.numEntities])
	{
		if((ent->s.eType == ET_ITEM) &&
		   (ent->flags & FL_DROPPED_ITEM) && (ent->r.contents == CONTENTS_TRIGGER) && (!Q_stricmp(ent->classname, classname)))
		{
			return ent;
		}

		ent++;
	}

	// The flag could not be found
	return NULL;
}

/*
=============
LevelFlagScan

Scan the level for the location of all flags.
=============
*/
void LevelFlagScan(void)
{
	// Only check in game modes with flags
	if(!(game_style & GS_FLAG))
		return;

	// Different flags are scanned in different game modes
	switch (gametype)
	{
		case GT_CTF:
			flags[RED_BASE] = LevelFindFlag(flags[RED_BASE], base_ents[RED_BASE], "team_CTF_redflag", PW_REDFLAG);
			flags[BLUE_BASE] = LevelFindFlag(flags[BLUE_BASE], base_ents[BLUE_BASE], "team_CTF_blueflag", PW_BLUEFLAG);
			break;

#ifdef MISSIONPACK
		case GT_1FCTF:
			flags[MID_BASE] = LevelFindFlag(flags[MID_BASE], base_ents[MID_BASE], "team_CTF_neutralflag", PW_NEUTRALFLAG);
			break;
#endif
	}
}

/*
============
BotAILoadMap
============
*/
int BotAILoadMap(int restart)
{
	int             i;
	vmCvar_t        mapname;

	// Setup the map name when changing levels
	if(!restart)
	{
		trap_Cvar_Register(&mapname, "mapname", "", CVAR_SERVERINFO | CVAR_ROM);
		trap_BotLibLoadMap(mapname.string);
	}

	// Reset the bot states
	for(i = 0; i < MAX_CLIENTS; i++)
		BotResetState(bot_states[i]);

	// Register the bot variables
	LevelSetupVariables();

	// Initialize weapon data
	LevelWeaponSetup();

	// Initialize the waypoint heap
	LevelInitWaypoints();

	// Reset all precomputed data associated with paths for obstacle deactivation--
	// They will get set up in the first AI Frame
	LevelPathReset();

	// Reset the region and item structures.  Like the paths, they will be
	// recomputed after a few frames
	LevelItemReset();

	// Reset the flag or obelisk locations.  Again, like paths and items, they will
	// be recomputed after a few frames
	LevelBaseReset();

	// This must be called at least once to set up the AAS
	trap_BotLibStartFrame(0.0);

	return qtrue;
}
