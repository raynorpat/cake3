// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_path.c
 *
 * Functions that the bot uses to plan paths to a specified goal
 *****************************************************************************/

#include "ai_main.h"
#include "ai_vars.h"
#include "ai_path.h"

#include "ai_client.h"
#include "ai_entity.h"
#include "ai_goal.h"
#include "ai_level.h"
#include "ai_weapon.h"


// Set to true if the obstacles and activators for path navigation have been set up already
qboolean        paths_setup = qfalse;

// List of all the obstacles in the game, sorted by model (obstacle->ent->s.modelindex)
#define MAX_OBSTACLES 256
ai_obstacle_t   level_obstacles[MAX_OBSTACLES];
int             num_obstacles;

// List of activators in the game
// NOTE: This list is NOT sorted.  Directly accessing it should not be needed.  Once
// initial setup is done, all required accesses can be done through the obstacle list.
#define MAX_ACTIVATORS 256
ai_activator_t  level_activators[MAX_ACTIVATORS];
int             num_activators;

// Mapping of all activators and relay targets that can trigger a specific target id
typedef struct activatable_target_s
{
	char           *target;		// The target these activators and relays trigger
	int             task_id;	// Id of the last recursion task to use this structure-- This is
	// used to prevent infinite recursion if relay targets form a loop.
	ai_activator_t *activator[MAX_LINKS];	// List of activators that trigger this target
	int             num_activators;	// Number of activator entries
	char           *relay[MAX_RELAY];	// List of targets that directly trigger this target
	int             num_relays;	// Number of relay entries

} activatable_target_t;

/*
==============
LevelPathReset
==============
*/
void LevelPathReset(void)
{
	paths_setup = qfalse;
	num_obstacles = 0;
	num_activators = 0;
}

/*
============
CanPlanPaths

Bots can plan paths if they are setup and
both obstacles (to plan around) and
activators (to unblock obstacles) exist.
============
*/
qboolean CanPlanPaths(void)
{
	return (paths_setup && num_obstacles && num_activators);
}

/*
==============================
CompareStringActivatableTarget

Compares an input string with an activatable target's key
==============================
*/
int QDECL CompareStringActivatableTarget(const void *string, const void *target)
{
	return strcmp((char *)string, ((activatable_target_t *) target)->target);
}

/*
=========================
CompareActivatableTargets

Compares two activatable target structures to each other.
=========================
*/
int QDECL CompareActivatableTargets(const void *a, const void *b)
{
	activatable_target_t *pa = (activatable_target_t *) a;
	activatable_target_t *pb = (activatable_target_t *) b;

	return strcmp(pa->target, pb->target);
}

/*
=======================
CompareIntObstacleModel

Compares an integer to an obstacle's entity's model index
=======================
*/
int QDECL CompareIntObstacleModel(const void *int_model, const void *obstacle)
{
	return *((int *)int_model) - ((ai_obstacle_t *) obstacle)->ent->s.modelindex;
}

/*
=====================
CompareObstacleModels

Compares two obstacles by their entity's model index
=====================
*/
int QDECL CompareObstacleModels(const void *a, const void *b)
{
	gentity_t      *ent_a = ((ai_obstacle_t *) a)->ent;
	gentity_t      *ent_b = ((ai_obstacle_t *) b)->ent;

	return ent_a->s.modelindex - ent_b->s.modelindex;
}

/*
=====================
LinkObstacleActivator

Attempt to link an obstacle and activator together.
=====================
*/
qboolean LinkObstacleActivator(ai_obstacle_t * obstacle, ai_activator_t * activator)
{
	// Make sure the obstacle and activator exist
	if(!obstacle || !activator)
		return qfalse;

	// Make sure the obstacle has room for another activator
	if(obstacle->num_activators >= MAX_LINKS)
	{
		BotAI_Print(PRT_WARNING, "Ignoring link between %s *%i and %s *%i because "
					"the obstacle has too many other activators.\n",
					obstacle->ent->classname, obstacle->ent->s.modelindex,
					activator->ent->classname, activator->ent->s.modelindex);
		return qfalse;
	}

	// Make sure the activator has room for another obstacle
	if(activator->num_obstacles >= MAX_LINKS)
	{
		BotAI_Print(PRT_WARNING, "Ignoring link between %s *%i and %s *%i because "
					"the activator has too many other obstacles.\n",
					obstacle->ent->classname, obstacle->ent->s.modelindex,
					activator->ent->classname, activator->ent->s.modelindex);
		return qfalse;
	}

#ifdef DEBUG_AI
	// Announce the setup of a new path connection
	if(bot_debug_path.integer)
	{
		if(obstacle->ent == activator->ent)
			BotAI_Print(PRT_MESSAGE, "Linked %s *%i to itself as a shootable obstacle\n",
						obstacle->ent->classname, obstacle->ent->s.modelindex);
		else
			BotAI_Print(PRT_MESSAGE, "Linked %s *%i (%s) to %s *%i (%s)\n",
						obstacle->ent->classname, obstacle->ent->s.modelindex, obstacle->ent->name,
						activator->ent->classname, activator->ent->s.modelindex, activator->ent->target);
	}
#endif

	// Add a link between the obstacle and the activator
	obstacle->activator[obstacle->num_activators++] = activator;
	activator->obstacle[activator->num_obstacles++] = obstacle;
	return qtrue;
}

/*
=========================
LevelAddObstacleActivator

Adds all activators for target id "target" to "obstacle".
Recursively calls itself to check for indirection through
activator relays.
=========================
*/
void LevelAddObstacleActivator(ai_obstacle_t * obstacle, activatable_target_t * activator_targets,
							   int num_activator_targets, int task_id, char *target)
{
	int             i;
	activatable_target_t *target_entry;
	ai_activator_t *activator;

	// Check if any activators for this target exist
	target_entry = bsearch(target, activator_targets, num_activator_targets,
						   sizeof(activatable_target_t), CompareStringActivatableTarget);
	if(!target_entry)
		return;

	// Never process the same entry twice during the same task-- prevents infinite loops
	if(target_entry->task_id == task_id)
		return;
	target_entry->task_id = task_id;

	// Add links between each activator in the list and the input obstacle
	for(i = 0; i < target_entry->num_activators; i++)
		LinkObstacleActivator(obstacle, target_entry->activator[i]);

	// Recursively process all indirect relay targets
	for(i = 0; i < target_entry->num_relays; i++)
	{
		LevelAddObstacleActivator(obstacle, activator_targets, num_activator_targets, task_id, target_entry->relay[i]);
	}
}

/*
===========
NewObstacle
===========
*/
ai_obstacle_t  *NewObstacle(gentity_t * ent)
{
	int             i, discard, areas[MAX_BLOCK_AREAS * 2], num_areas;
	vec3_t          mins, maxs;
	aas_areainfo_t  areainfo;
	ai_obstacle_t  *obstacle;

	// Never track standard doors as obstacles
	if(!strcmp(ent->classname, "func_door") && !ent->health && !ent->name)
		return NULL;

	// Make sure there is enough space to track this obstacle
	if(num_obstacles >= MAX_OBSTACLES)
	{
		BotAI_Print(PRT_WARNING, "Activator entity (%s, model %i, targetname \"%s\") will "
					"be ignored because too many obstacles were found.\n", ent->classname, ent->s.modelindex, ent->name);
		return NULL;
	}

	// Set up some basic information about the obstacle
	obstacle = &level_obstacles[num_obstacles++];
	obstacle->ent = ent;
	obstacle->num_activators = 0;

	// Determine which areas the obstacle blocks
	EntityWorldBounds(ent, mins, maxs);
	num_areas = trap_AAS_BBoxAreas(mins, maxs, areas, MAX_BLOCK_AREAS * 2);

	// Determine the minimum number of areas which must get discarded
	discard = num_areas - MAX_BLOCK_AREAS;
	if(discard < 0)
		discard = 0;

	// Store these areas in the activate goal
	obstacle->num_block_areas = 0;
	for(i = 0; i < num_areas; i++)
	{
		// Ignore areas without a mover
		trap_AAS_AreaInfo(areas[i], &areainfo);
		if(!(areainfo.contents & AREACONTENTS_MOVER))
		{
			discard--;
			continue;
		}

		// If this area isn't reachable and some areas must get discarded, discard it
		if(discard > 0 && trap_AAS_AreaReachability(areas[i]))
		{
			discard--;
			continue;
		}

		// Store this area
		obstacle->block_area[obstacle->num_block_areas++] = areas[i];

		// Stop storing if the array is full
		if(obstacle->num_block_areas >= MAX_BLOCK_AREAS)
			break;
	}

#ifdef DEBUG_AI
	// Inform the user about the newly created obstacle
	if(bot_debug_path.integer)
	{
		BotAI_Print(PRT_MESSAGE, "Created obstacle for %s *%i activated by %s\n",
					obstacle->ent->classname, obstacle->ent->s.modelindex,
					(obstacle->ent->name ? obstacle->ent->name : "itself"));
	}
#endif

	return obstacle;
}

/*
===================
ActivatorGoalButton
===================
*/
qboolean ActivatorGoalButton(ai_activator_t * activator)
{
	int             i, area;
	float           width;
	vec3_t          center, mins, maxs;
	vec3_t          surface, outside, player_mins, player_maxs;
	gentity_t      *button;

	// The bot must move to an area in front of the button, not onto the button itself
	button = activator->ent;

	// Look up the button's center and absolute world coordinates
	EntityCenterWorldBounds(button, center, mins, maxs);

	// Compute the bounding box corner, edge, or face on the surface (ie. opposite
	// the movement direction, since buttons move into the attached wall-- check
	// mins when movement is positive and maxs when movement is negative.)
	//
	// NOTE: Because of the move direction scaling at angles which aren't
	// perpendicular to the X or Y axis, the surface point might not be
	// on the bounding box's surface.  Technically it's a surface point of
	// the largest elipsoid contained in the bounding box.
	for(i = 0; i < 3; i++)
	{
		if(button->movedir[i] > 0.00001)
			surface[i] = mins[i];
		else if(button->movedir[i] < -0.00001)
			surface[i] = maxs[i];
		else
			surface[i] = center[i];
	}

	// Compute the location of a point outside the button (offset by half of the
	// player's bounding box) from which the player can most easily touch or shoot
	// the button.  The bot will head towards the area containing this point.
	VectorCopy(surface, outside);
	trap_AAS_PresenceTypeBoundingBox(PRESENCE_CROUCH, player_mins, player_maxs);
	for(i = 0; i < 3; i++)
	{
		if(button->movedir[i] > 0.00001)
			outside[i] -= player_maxs[i];
		else if(button->movedir[i] < -0.00001)
			outside[i] -= player_mins[i];
	}

	// Find what area the outside location is in
	area = LevelAreaLocPoint(outside, outside, 0, -1024);
	if(!area)
		return qfalse;

	// Pushable buttons need a more precise goal location
	if(!button->takedamage)
	{
		GoalLocationArea(&activator->goal, center, area);
		VectorSubtract(mins, center, activator->goal.mins);
		VectorSubtract(maxs, center, activator->goal.maxs);
		activator->shoot = qfalse;
	}

	// Shootable buttons are often not reachable by foot, so the bot must
	// aim for a nearby point instead.
	else
	{
		GoalLocationArea(&activator->goal, outside, area);
		activator->shoot = qtrue;
	}

	// Use the button entity as the goal's entity
	activator->goal.entitynum = button->s.number;
	return qtrue;
}

/*
====================
ActivatorGoalTrigger
====================
*/
qboolean ActivatorGoalTrigger(ai_activator_t * activator)
{
	vec3_t          center;

	// Look up entity's center
	EntityCenter(activator->ent, center);

	// Make a goal around the trigger if possible
	if(!GoalLocation(&activator->goal, center))
		return qfalse;
	activator->shoot = qfalse;
	activator->goal.entitynum = activator->ent - g_entities;
	return qtrue;
}

/*
==========================
ActivatorGoalShootObstacle
==========================
*/
qboolean ActivatorGoalShootObstacle(ai_activator_t * activator)
{
	vec3_t          center;

	// Look up entity center
	EntityCenter(activator->ent, center);

	// Make a shoot goal at the obstacle's center if possible
	if(!GoalLocation(&activator->goal, center))
		return qfalse;
	activator->shoot = qtrue;
	activator->goal.entitynum = activator->ent - g_entities;
	return qfalse;
}

/*
============
NewActivator
============
*/
ai_activator_t *NewActivator(gentity_t * ent, qboolean(*goal_setup) (ai_activator_t * activator))
{
	ai_activator_t *activator;

	// Make sure another activator entry can be allocated
	if(num_activators >= MAX_ACTIVATORS)
	{
		BotAI_Print(PRT_WARNING, "Ignoring %s *%i activator entity because too many activators "
					"were found.\n", ent->classname, ent->s.modelindex);
		return NULL;
	}
	activator = &level_activators[num_activators++];

	// Set up some basic information about the activator
	activator->ent = ent;
	activator->num_obstacles = 0;

	// Create the activator goal (or delete this activator and fail)
	if(!goal_setup(activator))
	{
		BotAI_Print(PRT_WARNING, "Ignoring %s *%i activator entity because a goal to activate "
					"it could not be created.\n", ent->classname, ent->s.modelindex);
		num_activators--;
		return NULL;
	}

#ifdef DEBUG_AI
	// Inform the user about the newly created activator
	if(bot_debug_path.integer)
	{
		BotAI_Print(PRT_MESSAGE, "Created activator for %s *%i activating %s\n",
					activator->ent->classname, activator->ent->s.modelindex,
					(activator->ent->target ? activator->ent->target : "itself"));
	}
#endif

	// Return the completed activator entry
	return activator;
}

/*
===================
ActivatorSetupRelay
===================
*/
qboolean ActivatorSetupRelay(gentity_t * ent, activatable_target_t * entry)
{
	// Make sure there is space for this entry
	if(entry->num_relays >= MAX_LINKS)
	{
		BotAI_Print(PRT_WARNING, "Relay entity mapping from \"%s\" to \"%s\" will be ignored "
					"because too many relays linking to \"%s\" were found.\n", ent->name, ent->target, ent->target);
		return qfalse;
	}

	// The relay must have a valid target name to be triggered
	if(!ent->name || !ent->name[0])
	{
		BotAI_Print(PRT_WARNING, "Relay entity mapping to \"%s\" will be ignored because it does "
					"not have a valid source \"targetname\".\n", ent->target);
		return qfalse;
	}

	// Copy the relay's target to the next relay map entry
	entry->relay[entry->num_relays++] = ent->name;
	return qtrue;
}

/*
====================
ActivatorSetupButton
====================
*/
qboolean ActivatorSetupButton(gentity_t * ent, activatable_target_t * entry)
{
	ai_activator_t *activator;

	// Make sure there is space for this entry
	if(entry->num_activators >= MAX_LINKS)
	{
		BotAI_Print(PRT_WARNING, "Button entity activating \"%s\" will be ignored "
					"because too many activators for \"%s\" were found.\n", ent->name, ent->name);
		return qfalse;
	}

	// Try to create a button activator
	activator = NewActivator(ent, ActivatorGoalButton);
	if(!activator)
		return qfalse;

	// Add the activator to the activatable target list
	entry->activator[entry->num_activators++] = activator;
	return qtrue;
}

/*
=====================
ActivatorSetupTrigger
=====================
*/
qboolean ActivatorSetupTrigger(gentity_t * ent, activatable_target_t * entry)
{
	ai_activator_t *activator;

	// Make sure there is space for this entry
	if(entry->num_activators >= MAX_LINKS)
	{
		BotAI_Print(PRT_WARNING, "Trigger entity activating \"%s\" will be ignored "
					"because too many activators for \"%s\" were found.\n", ent->name, ent->name);
		return qfalse;
	}

	// Try to create a trigger activator
	activator = NewActivator(ent, ActivatorGoalTrigger);
	if(!activator)
		return qfalse;

	// Add the activator to the activatable target list
	entry->activator[entry->num_activators++] = activator;
	return qtrue;
}

// Maximum number of different target tags allocated for activators and obstacles
#define MAX_ACTIVATOR_TARGETS 128

/*
==============
LevelPathSetup

NOTE: This function must get called after the first G_RunFrame()
(because some entity teams haven't finished spawning until their
first think() call).  Thankfully, the engine sends the first
GAME_RUN_FRAME before the first BOTAI_START_FRAME, but this isn't
a guaranteed feature of the engine.
==============
*/
void LevelPathSetup(void)
{
	int             i, insert, num_activator_targets;
	activatable_target_t activator_target[MAX_ACTIVATOR_TARGETS], *target;
	ai_obstacle_t  *obstacle;
	gentity_t      *ent;

	qboolean(*activator_setup) (gentity_t * activator, activatable_target_t * entry);

	// Do nothing if the activators have already been setup
	if(paths_setup)
		return;
	paths_setup = qtrue;

	// Search all game entities for obstacles and activators (including relays)
	num_obstacles = 0;
	num_activators = 0;
	num_activator_targets = 0;
	for(i = 0, ent = &g_entities[0]; i < level.numEntities; i++, ent++)
	{
		// Only scan valid entities with set classnames
		if(!ent->inuse || !ent->classname || !ent->classname[0])
			continue;

		// Check different kinds of activators:
		// Relay activators
		if(!strcmp(ent->classname, "target_relay") || !strcmp(ent->classname, "target_delay"))
			activator_setup = ActivatorSetupRelay;
		// Buttons
		else if(!strcmp(ent->classname, "func_button"))
			activator_setup = ActivatorSetupButton;
		// Trigger boxes
		else if(!strcmp(ent->classname, "trigger_multiple"))
			activator_setup = ActivatorSetupTrigger;
		// Not an activator
		else
			activator_setup = 0;

		// Set up the activator if necessary
		if(activator_setup)
		{
			// All activators need a valid target entry
			if(!ent->target || !ent->target[0])
				continue;

			// Look up the mapping of relays and activators for this activator's target id
			target = (activatable_target_t *)
				bsearch_ins(ent->target, activator_target, &num_activator_targets,
							MAX_ACTIVATOR_TARGETS, sizeof(activatable_target_t), CompareStringActivatableTarget, &insert);

			if(!target)
			{
				BotAI_Print(PRT_WARNING, "%s *%i activator entity activating \"%s\" will "
							"be ignored because too many different activator "
							"targets were found.\n", ent->classname, ent->s.modelindex, ent->target);
				continue;
			}

			// Create a new mapping entry if necessary
			if(insert)
			{
				target->target = ent->target;
				target->task_id = -1;	// No task has accessed this entry yet
				target->num_activators = 0;
				target->num_relays = 0;
			}

			// Setup the activator in its appropriate way
			activator_setup(ent, target);
			continue;
		}

		// An obstacle is defined as a:
		// - Mover that doesn't activate other targets -and-
		//   - Either is shootable -or-
		//   - Has a valid target name
		// NOTE: Most doors do not have a target name-- they automatically open
		if((ent->s.eType == ET_MOVER) &&
		   (!ent->target || !ent->target[0]) && (ent->takedamage || (ent->name && ent->name[0])))
		{
			NewObstacle(ent);
			continue;
		}
	}

	// Sort the obstacles by model index for fast runtime access
	qsort(level_obstacles, num_obstacles, sizeof(ai_obstacle_t), CompareObstacleModels);

	// Sort activator targets so obstacles can quickly determine how they are activated
	qsort(activator_target, num_activator_targets, sizeof(activatable_target_t), CompareActivatableTargets);

	// Create self-activation links for shootable obstacles
	for(i = 0, obstacle = &level_obstacles[0]; i < num_obstacles; i++, obstacle++)
	{
		if(obstacle->ent->takedamage)
			LinkObstacleActivator(obstacle, NewActivator(obstacle->ent, ActivatorGoalShootObstacle));
	}

	// Create intra-structure links between obstacles and activators
	for(i = 0; i < num_obstacles; i++)
	{
		LevelAddObstacleActivator(&level_obstacles[i], activator_target, num_activator_targets, i,
								  level_obstacles[i].ent->name);
	}

	// A setup completion message can make users feel a lot better
	BotAI_Print(PRT_MESSAGE, "Detected and set up %i obstacles and %i activators: "
				"Path activators will %sbe used\n", num_obstacles, num_activators, (CanPlanPaths()? "" : "not "));
}

/*
==================
ObstacleIsBlocking

Check if an obstacle is blocking its movement areas
==================
*/
qboolean ObstacleIsBlocking(ai_obstacle_t * obstacle)
{
	return ((obstacle->ent->moverState == MOVER_POS1) || (obstacle->ent->moverState == MOVER_2TO1));
}

/*
============
BotPathReset

Resets a path prediction and requests a reprediction.
============
*/
void BotPathReset(bot_path_t * path)
{
	// Request an update as soon as possible
	path->time = 0;

	// Reset information about the path
	path->start_area = 0;
	path->end_area = 0;
	path->subgoal = NULL;
	path->obstacles.num_obstacles = 0;
}

/*
===================
BotUpdatePrediction

Updates information about the bot's path prediction
===================
*/
void BotPathUpdate(bot_state_t * bs, bot_path_t * path)
{
	int             i;
	qboolean        blocked;
	path_obstacle_list_t *obstacles;

	// Only update if the bot could have a path
	if(!CanPlanPaths())
		return;

	// Never update non-paths
	if(!path->end_area)
		return;

	// Update the blocking state of each path obstacle
	obstacles = &path->obstacles;
	for(i = 0; i < obstacles->num_obstacles; i++)
	{
		// Force a path reprediction if an obstacle blocking state changed
		blocked = ObstacleIsBlocking(obstacles->obstacle[i]);
		if(blocked != obstacles->blocked[i])
		{
			BotPathReset(path);
			return;
		}

		// Save the current blocking state
		obstacles->blocked[i] = blocked;
	}

	// Reset the path if the the old start area can't be easily reached from the current position
	if(!LevelAreasNearby(LevelAreaEntity(bs->ent), bs->now.origin, path->start_area))
	{
		BotPathReset(path);
		return;
	}
}

/*
==========================
EnableObstacleRoutingAreas

Enables routing through the areas an obstacle blocks

NOTE: See warning in LevelEnableRoutingArea() function.
==========================
*/
void EnableObstacleRoutingAreas(ai_obstacle_t * obstacle)
{
	int             i;

	// Enable each area the obstacle blocks
	for(i = 0; i < obstacle->num_block_areas; i++)
		LevelEnableRoutingArea(obstacle->block_area[i]);
}

/*
===========================
DisableObstacleRoutingAreas

Disables routing through the areas an obstacle blocks

NOTE: See warning in LevelEnableRoutingArea() function.
===========================
*/
void DisableObstacleRoutingAreas(ai_obstacle_t * obstacle)
{
	int             i;

	// Disable each area the obstacle blocks
	for(i = 0; i < obstacle->num_block_areas; i++)
		LevelDisableRoutingArea(obstacle->block_area[i]);
}

/*
===============
BotPathObstacle

Traces a path between the start and end location and
returns a pointer to the first obstacle found on that
path, or NULL if no obstacle was found.

If an obstacle is found, the route location and area
which was blocked by the obstacle will be saved in
origin and area respectively.

The maximum number of areas to predict and travel time
to predict (in hundredths of a second) are stored in
max_areas and max_time respectively.  The number of
areas and time actually spent reaching the returned
obstacle will be decremented again max_areas and max_time.
===============
*/
ai_obstacle_t  *BotPathObstacle(bot_state_t * bs, int *max_areas, int *max_time, vec3_t origin, int *area, int end_area)
{
	int             model;
	aas_predictroute_t route;
	ai_obstacle_t  *obstacle;

	// Predict ahead until the area or time counters expire
	while(*max_areas > 0 && *max_time > 0)
	{
		// Search for the next upcoming obstacle (area with mover contents)
		memset(&route, 0, sizeof(aas_predictroute_t));
		trap_AAS_PredictRoute(&route, *area, origin, end_area, bs->travel_flags,
							  *max_areas, *max_time, RSE_ENTERCONTENTS, AREACONTENTS_MOVER, 0, 0);

		// If the path wasn't routable, exit
		// NOTE: To distinguish the "no route" case from the "no obstacles in route" case,
		// the caller could check if max_time decreased.  The way this function is
		// currently used, however, this is not necessary.
		if(route.stopevent & RSE_NOROUTE)
			return NULL;

		// Update the starting origin, area, and routing termination counters
		VectorCopy(route.endpos, origin);
		*area = route.endarea;
		*max_areas -= route.numareas;
		*max_time -= route.time;

		// Check if the route completed
		if(route.stopevent == RSE_NONE)
			return NULL;

		// If the route wasn't stopped by a mover, an internal error occurred
		// NOTE: This should never occur
		if(!(route.stopevent & RSE_ENTERCONTENTS) || !(route.endcontents & AREACONTENTS_MOVER))
			return NULL;

		// Check if the mover has a valid model number
		// NOTE: Extracting the obstacle's model like this only works with bspc 2.1 and higher
		model = (route.endcontents & AREACONTENTS_MODELNUM) >> AREACONTENTS_MODELNUMSHIFT;
		if(!model)
			return NULL;

		// Check if the model is associated with an activatable obstacle
		// NOTE: Unactivatable obstacles won't be found in the array.  The bot assumes
		// these obstacles (such as standard doors) will be automatically activated.
		obstacle = bsearch(&model, level_obstacles, num_obstacles, sizeof(ai_obstacle_t), CompareIntObstacleModel);
		if(!obstacle)
			continue;

		// Return this obstacle to the caller
		// NOTE: The obstacle might not be blocking the path right now
		return obstacle;
	}

	// No obstacle was found
	return NULL;
}

/*
===================
BotPredictGoalRoute

This function first checks if the bot can move to a requested input
goal (possibly deactivating obstacles to make the route feasible).
If "shoot" is true, it also checks if the bot can shoot the goal
with a non-gauntlet weapon.  If this is not possible, the function
returns false and permanently changes no other data values.  Otherwise
the function will return true.

When this function returns true, it also tries to compute a subgoal
the bot could directly move to without deactivating any obstacles.
Note that this subgoal could be a button that deactivates an obstacle
blocking the requested goal, or it could be the requested input goal
itself.  If such a subgoal exists, it will be saved in the "subgoal"
pointer.  The travel time from the bot to this subgoal will be
saved in "subgoal_time", and if the subgoal is an activator that
should be shot, true is stored in "subgoal_shoot".

It's worth noting that this function is guaranteed to compute a
reachable subgoal.  Subgoal might be NULL, but it will never be a
goal that is currently blocked by obstacles.  This property is very
important, since the function is recursive.

The encounter list is a list of obstacles the bot has encountered in
the process of this route prediction.  If BotPathObstacle() detects
an obstacle in this list, the bot won't try deactivating it a second time.
Every obstacle in this list is either deactivatable (in which case the
bot only needs to do it once) or not (in which case the bot fails).  But
there's never a reason to process it twice.
===================
*/
qboolean BotPredictGoalRoute(bot_state_t * bs, path_obstacle_list_t * encountered,
							 bot_goal_t * goal, qboolean shoot,
							 bot_goal_t ** subgoal, qboolean * subgoal_shoot, int *subgoal_time)
{
	int             i, max_areas, max_time, area;
	int             time, best_time, activator_time;
	qboolean        blocked, activatable, insert;
	qboolean        best_shoot, activator_shoot;
	bot_goal_t     *best_subgoal, *activator_subgoal;
	ai_obstacle_t  *obstacle, **encounter_entry;
	vec3_t          path_start;

	// Check for shootable goals the bot can't activate
	// NOTE: The BotActivateWeapon() check happens less than one call
	// per route prediction, so precomputing and caching the value is
	// actually slower in the average case.
	if(shoot && BotActivateWeapon(bs) == WP_GAUNTLET)
		return qfalse;

	// Make sure the goal is routable given current obstacles
	time = EntityGoalTravelTime(bs->ent, goal, bs->travel_flags);
	if(time < 0)
		return qfalse;

	// Start path prediction at the bot's location and area
	VectorCopy(bs->now.origin, path_start);
	area = LevelAreaEntity(bs->ent);

	// By default, assume no activating subgoal has been found and the path isn't blocked
	best_subgoal = NULL;
	blocked = qfalse;

	// Loop over all obstacles in the path, trying to deactivate them
	max_areas = 100;
	max_time = 10000;
	while((obstacle = BotPathObstacle(bs, &max_areas, &max_time, path_start, &area, goal->areanum)) != NULL)
	{
		// Check if the bot previously encountered this obstacle
		encounter_entry = bsearch_ins(obstacle, encountered->obstacle, &encountered->num_obstacles,
									  MAX_PATH_OBSTACLES, sizeof(ai_obstacle_t *), CompareObstacleModels, &insert);

		// If no list pointer was returned, the obstacle was not found and the array is full
		if(!encounter_entry)
			return qfalse;

		// If the obstacle was already encountered (and therefore deactivatable), ignore it
		if(!insert)
			continue;

		// Insert a pointer to the encountered obstacle in the encounter list
		*encounter_entry = obstacle;

		// Keep looking for more obstacles if this obstacle isn't blocking the path
		if(!ObstacleIsBlocking(obstacle))
			continue;

		// At least one obstacle blocks the path to the main goal
		blocked = qtrue;

		// Disable routing through the obstacle's blocking areas
		DisableObstacleRoutingAreas(obstacle);

		// Find a valid activator for this obstacle
		activatable = qfalse;
		for(i = 0; i < obstacle->num_activators; i++)
		{
			// If the prediction failed, continue checking other activators
			if(!BotPredictGoalRoute(bs, encountered,
									&obstacle->activator[i]->goal, obstacle->activator[i]->shoot,
									&activator_subgoal, &activator_shoot, &activator_time))
				continue;

			// Remember that the obstacle is activatable somehow
			activatable = qtrue;

			// Use the returned predicted goal if it's closer than the current goal
			if(activator_subgoal && (!best_subgoal || activator_time < best_time))
			{
				best_subgoal = activator_subgoal;
				best_shoot = activator_shoot;
				best_time = activator_time;
			}
		}

		// Enable routing through the obstacle's disabled areas
		EnableObstacleRoutingAreas(obstacle);

		// If the obstacle could not be activated, fail
		if(!activatable)
			return qfalse;
	}

	// If the bot was blocked by an obstacle, use the best activator
	if(blocked)
	{
		*subgoal = best_subgoal;
		*subgoal_shoot = best_shoot;
		*subgoal_time = best_time;
	}
	// Otherwise use the main goal as the subgoal
	else
	{
		*subgoal = goal;
		*subgoal_shoot = shoot;
		*subgoal_time = time;
	}

	return qtrue;
}

/*
===========
BotPathPlan

Plans a path to reach the "objective" goal.  If no such path
exists, the function returns false.  Otherwise, the function
returns true and the first step on the path is copied into
the "destination" goal pointer.  This first step may be the
input goal (if the path is not blocked) or a subgoal for an
activator that unblocks the path.

NOTE: "objective" and "destination" may point to the same goal object.
===========
*/
qboolean BotPathPlan(bot_state_t * bs, bot_path_t * path, bot_goal_t * objective, bot_goal_t * destination)
{
	int             i, time;
	qboolean        shoot;
	bot_goal_t     *subgoal;
	path_obstacle_list_t obstacles;

	// By default the path is okay if paths can't be planned
	if(!CanPlanPaths())
	{
		memcpy(destination, objective, sizeof(bot_goal_t));
		return qtrue;
	}

	// Neither predict nor update non-goals
	if(!objective->areanum)
	{
		memcpy(destination, objective, sizeof(bot_goal_t));
		BotPathReset(path);
		return qtrue;
	}

	// The path should be predicted if a new prediction was requested now or if
	// the new goal area can't easily be reached from the old goal location
	if(path->time <= bs->command_time || !LevelAreasNearby(path->end_area, path->end_origin, objective->areanum))
	{
#ifdef DEBUG_AI
		if(bs->debug_flags & BOT_DEBUG_INFO_PATH)
			BotAI_Print(PRT_MESSAGE, "%s: Path: Planning path from area %i to %i\n",
						EntityNameFast(bs->ent), LevelAreaEntity(bs->ent), objective->areanum);
#endif

		// If the bot can't reach the route, fail
		// NOTE: This code doesn't care how long it takes to reach the subgoal (&time)
		obstacles.num_obstacles = 0;
		if(!BotPredictGoalRoute(bs, &obstacles, objective, qfalse, &path->subgoal, &path->shoot, &time))
		{
#ifdef DEBUG_AI
			if(bs->debug_flags & BOT_DEBUG_INFO_PATH)
				BotAI_Print(PRT_MESSAGE, "%s: Path: No legal activation sequence found\n", EntityNameFast(bs->ent));
#endif

			return qfalse;
		}

#ifdef DEBUG_AI
		if(bs->debug_flags & BOT_DEBUG_INFO_PATH)
		{
			BotAI_Print(PRT_MESSAGE, "%s: Path: Found %i obstacles in path\n", EntityNameFast(bs->ent), obstacles.num_obstacles);

			if(obstacles.num_obstacles)
				BotAI_Print(PRT_MESSAGE, "%s: Path: Nearest unblocked activator (%s) is in area %i\n",
							EntityNameFast(bs->ent), (path->shoot ? "shoot" : "push"), path->subgoal->areanum);
		}
#endif

		// Save the initial block state of the encountered obstacles and store in bot state
		for(i = 0; i < obstacles.num_obstacles; i++)
			obstacles.blocked[i] = ObstacleIsBlocking(obstacles.obstacle[i]);
		memcpy(&path->obstacles, &obstacles, sizeof(path_obstacle_list_t));

		// Record the area and location of the last path the bot predicted
		path->start_area = LevelAreaEntity(bs->ent);
		path->end_area = objective->areanum;
		VectorCopy(objective->origin, path->end_origin);

		// Recompute in a little while
		path->time = bs->command_time + 5.0;
	}

	// Use either the subgoal or the objective as the destination as required
	memcpy(destination, (path->subgoal ? path->subgoal : objective), sizeof(bot_goal_t));

	// Head towards the (possibly new) destination
	return qtrue;
}
