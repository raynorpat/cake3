// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_item.c
 *
 * Functions that the bot uses to determine item pickups
 *****************************************************************************/

#include "ai_main.h"
#include "ai_vars.h"
#include "ai_pickup.h"

#include "ai_client.h"
#include "ai_dodge.h"
#include "ai_entity.h"
#include "ai_goal.h"
#include "ai_item.h"
#include "ai_level.h"
#include "ai_path.h"
#include "ai_region.h"
#include "ai_resource.h"
#include "ai_visible.h"


#ifdef DEBUG_AI

// The maximum number of item debug frames a bot will cache
#define MAX_ITEM_FRAMES 12

// A chain of item pickups the bot has the option of selecting for item pickups
typedef struct chain_option_s
{
	int             chain_size;	// The number of clusters in the chain
	item_cluster_t *chain[MAX_PICKUPS];	// The chain of clusters considered
	item_cluster_t *cluster;	// The first cluster of the chain, or NULL for a length 0 chain
	float           rating;		// How highly this item pickup chain was rated
} chain_option_t;

// One frame of item prediction data
typedef struct item_frame_s
{
	chain_option_t  option[MAX_CLUSTERS_CONSIDER];	// All options the bot had for item pickups this frame
	int             num_options;	// The number of options the bot had
	int             selected;	// Which option the bot selected
	float           time;		// Timestamp at which this frame was computed
	vec3_t          loc;		// Where the bot was located
} item_frame_t;

// All the item debug data for one bot
typedef struct item_debug_s
{
	item_frame_t    frame[MAX_ITEM_FRAMES];	// The frames of item debug data
	int             num_frames;	// The number of debug frames being tracked
} bot_item_debug_t;

// Debug information for all bots
bot_item_debug_t bot_item_debug[MAX_CLIENTS];
#endif


// A structure defining different rates of encountering teammates and enemies
typedef struct encounter_rates_s
{
	float           see_teammate;	// Chance of seeing a teammate
	float           see_enemy;	// Chance of seeing an enemy (and therefore the chance of attacking)
	float           enemy_attack;	// Chance of being attacked by an enemy times expected number of enemies
} encounter_rates_t;

// The information describing how the bot understands a particular
// item cluster pickup option.
typedef struct cluster_option_s
{
	item_cluster_t *cluster;	// The cluster the bot has the option of selecting
	float           max_respawn;	// Number of seconds for which bot knows this cluster's
	// respawn status, or 0.0 if the bot isn't timing the respawn.
	float           soonest_respawn;	// The soonest the bot knows an item in this cluster could respawn
	float           from_start;	// The travel time to this cluster from the bot's starting location
	float           to_goal;	// The travel time from this cluster to the bot's goal
	encounter_rates_t rates;	// Chance of encountering players near this cluster
	region_t      **neighbors;	// Which regions neighbor this option on the way to the goal
	int             num_neighbors;	// The size of the neighbor array
	qboolean        selected;	// True if this was the first cluster in last frame's chain
} cluster_option_t;

// General information the bot uses to determine the optimal item cluster to
// pick up.  This structure exists to easy the interface between the item selection
// setup and inner loop.  (It's cleaner and faster to send one pointer than fifteen
// data values.)
typedef struct bot_item_info_s
{
	int             area;		// Bot's starting area
	resource_state_t rs;		// Bot's starting resource state
	play_info_t     pi;			// Bot's play state information
	float           time;		// Time to travel directly to main goal (or -1 for no goal)

	region_t       *start_region;	// Region the bot is currently in
	region_t       *end_region;	// Region, if any, the bot's goal lies in

	encounter_rates_t start_rates;	// Encounter rates in bot's current region
	encounter_rates_t end_rates;	// Encounter rates in bot's mail goal's region
	// NOTE: This will match start_rates if the bot lacks a goal

	int             teammates;	// Number of teammates in the game other than the bot
	int             enemies;	// Number of enemies in the game
	qboolean        nearby;		// True if nearby enemies will probably stay nearby

	cluster_option_t consider[MAX_CLUSTERS_CONSIDER];	// Which clusters the bot is considering
	int             num_consider;	// Number of clusters the bot is considering

#ifdef DEBUG_AI
	item_frame_t   *frame;		// Store data describing why the bot did or didn't select
	// certain clusters in this item reasoning frame
#endif
} bot_item_info_t;


#ifdef DEBUG_AI

/*
=========================
CompareClusterChainOption

Compare a cluster pointer to the first cluster of a
pickup chain, sorted ascending by first cluster pointer.
=========================
*/
int QDECL CompareClusterChainOption(const void *a, const void *b)
{
	const item_cluster_t *cluster = *((const item_cluster_t **)a);
	const chain_option_t *option = (const chain_option_t *)b;

	return cluster - option->cluster;
}

/*
==============
FrameOptionAdd

Add another option considered to this item debug frame.
The option's relative effectiveness is "rating".  The
"selected" flag is true if this option is the currently
selected one (ie. maximum rating so far).
==============
*/
void FrameOptionAdd(item_frame_t * frame, item_cluster_t ** chain, int chain_size, float rating, qboolean selected)
{
	int             index;
	qboolean        insert;
	chain_option_t *option;
	item_cluster_t *cluster;

	// Only add options to real frames
	if(!frame)
		return;

	// Look up which item selection option this chain represents
	cluster = (chain_size ? chain[0] : NULL);

	// Find the address of this option's record in the frame data,
	// making room for a new record if necessary
	option = bsearch_ins(&cluster, frame->option, &frame->num_options,
						 MAX_CLUSTERS_CONSIDER, sizeof(chain_option_t), CompareClusterChainOption, &insert);

	// Don't add data if the array ran out of space
	//
	// NOTE: This should not occur.
	if(!option)
		return;

	// Don't update options that are less effective than previously discovered reasons
	// to visit the same cluster first
	if(!insert && rating <= option->rating)
		return;

	// Determine the index of this option's record in the frame of data
	index = option - frame->option;

	// Ammend the index of the last selected option if it changed
	if(insert && frame->selected >= index)
		frame->selected++;

	// Add or update the selection option as necessary
	option->chain_size = chain_size;
	memcpy(&option->chain, chain, chain_size * sizeof(item_cluster_t *));
	option->cluster = cluster;
	option->rating = rating;

	// Mark this entry as the selected option if it is
	if(selected)
		frame->selected = index;
}

/*
================
FrameOptionPrint

Prints out the data for one frame of
item pickup options.
================
*/
void FrameOptionPrint(item_frame_t * frame)
{
	int             option_index, chain_entry;
	item_cluster_t *cluster;
	chain_option_t *option;

	// Look up a pointer to the cluster selected this frame
	cluster = frame->option[frame->selected].cluster;

	// Print which cluster was selected
	G_Printf("  %.3f: ^3%s^7 (bot nearest ^2%s^7)\n",
			 frame->time, ClusterName(cluster), RegionName(LevelNearestRegion(frame->loc)));

	// Print each cluster chain considered this frame
	for(option_index = 0; option_index < frame->num_options; option_index++)
	{
		option = &frame->option[option_index];

		// Print the cluster chain's name
		G_Printf("    ");
		if(option->chain_size)
		{
			// Print each cluster in the chain
			for(chain_entry = 0; chain_entry < option->chain_size; chain_entry++)
			{
				// Comma separate names after the initial cluster
				if(chain_entry)
					G_Printf(", ");

				// Print this cluster's name
				G_Printf(ClusterName(option->chain[chain_entry]));
			}
		}
		else
		{
			G_Printf(EntityNameFast(NULL));
		}

		// Print the rating information for this cluster
		G_Printf(": %.5f\n", option->rating);
	}
}

/*
=================
BotItemDebugPrint

Prints the item debug data if anything
interesting was detected.
=================
*/
void BotItemDebugPrint(bot_state_t * bs, bot_item_debug_t * data)
{
	int             i, switches;
	item_cluster_t *last_cluster, *cluster;
	item_frame_t   *frame;

	// Never print out data that hasn't been collected
	if(data->num_frames <= 0)
		return;

	// Count how many times the bot switched its selected item
	//
	// NOTE: This intentionally counts the start of the frame as one
	// additional "switch" from an unknown cluster selection.  The strange
	// pointer initialization guarantees last_cluster will differ from any
	// cluster pointer, even the null option (no item pickup).
	switches = 0;
	last_cluster = (void *)-1;
	for(i = 0; i < data->num_frames; i++)
	{
		// Look up a pointer to the cluster selected this frame
		frame = &data->frame[i];
		cluster = frame->option[frame->selected].cluster;

		// Note a switch if this refers to a different cluster than before
		if(last_cluster != cluster)
		{
			last_cluster = cluster;
			switches++;
		}
	}

	// Don't print if the item selection switched less than 75% of the frames
	if(switches < data->num_frames * 0.75)
		return;

	// State why the data is being outputted
	G_Printf("%.3f %s: Item Reason: ^2Detected %i item switches in %i selections^7\n",
			 server_time, EntityNameFast(bs->ent), switches, data->num_frames);

	// Print data for each frame in the buffer
	for(i = 0; i < data->num_frames; i++)
		FrameOptionPrint(&data->frame[i]);
}

/*
=================
BotItemDebugReset

Output the item debug data for one bot and then reset it.
================
*/
void BotItemDebugReset(bot_state_t * bs, bot_item_debug_t * data)
{
	// Possibly print the data
	BotItemDebugPrint(bs, data);

	// Reset the frame data
	data->num_frames = 0;
}

/*
=====================
BotItemDebugNextFrame

Get a pointer to the next item debug frame the bot should use
to track item selections.  The frame will be prepared to have
data added to it via FrameOptionAdd().  This function might opt
to print out data from its frame cache if the cache gets reset
and an interestingly large amount of data was collected.
=====================
*/
item_frame_t   *BotItemDebugNextFrame(bot_state_t * bs)
{
	bot_item_debug_t *data;
	item_frame_t   *frame;

	// Do not use a debug frame if the bot isn't debugging this data
	if(!(bs->debug_flags & BOT_DEBUG_INFO_ITEM_REASON))
		return NULL;

	// Look up this bot's item debug data
	data = &bot_item_debug[bs->entitynum];

	// Reset the data and start a new round of data collection if necessary
	if(data->num_frames >= MAX_ITEM_FRAMES)
		BotItemDebugReset(bs, data);

	// Prepare the next frame of data
	frame = &data->frame[data->num_frames++];
	frame->num_options = 0;
	frame->selected = 0;
	frame->time = server_time;
	VectorCopy(bs->ps->origin, frame->loc);

	// Hand it off to the caller
	return frame;
}

#endif

/*
============
BotItemReset
============
*/
void BotItemReset(bot_state_t * bs)
{
	bs->item_setup = qfalse;
}

/*
============
BotItemSetup

Returns true if the related item data
could be setup and false if not.
============
*/
qboolean BotItemSetup(bot_state_t * bs)
{
	int             i, max_timed;

	// Always fail if the level's items haven't been setup
	if(!CanProcessItems())
		return qfalse;

	// Only setup bot item data and statistics if necessary
	if(bs->item_setup)
		return qtrue;
	bs->item_setup = qtrue;

	// Determine how many items the bot can time
	max_timed = floor(bs->settings.skill) - 2;
	if(max_timed < 0)
		max_timed = 0;
	else if(max_timed > MAX_TIMED)
		max_timed = MAX_TIMED;

	// Setup the list of timed item clusters
	tvl_setup(&bs->timed_items, max_timed, sizeof(item_cluster_t *), bs->timed_item_cluster,
			  bs->timed_item_timeout, bs->timed_item_value, CompareVoidList);

	// These values are seeded with data to avoid division by zero
	// checks and to give tabula rasa bots starting information
	bs->deaths = 2;
	bs->damage_received = 200 * bs->deaths;
	bs->kills = 2;
	bs->damage_dealt = 200 * bs->kills;

	// Assume the bot received initial damage at a rate of 10 points per second
	bs->enemy_attack_time = bs->damage_received / 10.0;

	return qtrue;
}

/*
=======================
ClusterSpawnedItemCount

Count the number of items in a cluster that are
currently spawned in
=======================
*/
int ClusterSpawnedItemCount(item_cluster_t * cluster)
{
	int             item_count;
	item_link_t    *item;

	// Non-clusters obviously have zero items in them
	if(!cluster)
		return 0;

	// Count the number of items in the cluster that have spawned in
	item_count = 0;
	for(item = cluster->start; item; item = item->next_near)
	{
		if(item->ent->r.contents & CONTENTS_TRIGGER)
			item_count++;
	}

	return item_count;
}

/*
====================
BotRecomputeItemGoal

Check if the bot needs to recompute its item subgoal
====================
*/
qboolean BotRecomputeItemGoal(bot_state_t * bs, int damage)
{
	int             item_count;

	// Recompute if the timer expired
	if(bs->item_time <= bs->command_time)
		return qtrue;

	// Recompute if the amount of damage the bot can sustain has significantly decreased
	if(damage < bs->item_bot_damage - 25)
		return qtrue;

	// If the bot selected an item before, check if the item was picked up
	if(bs->num_item_clusters)
	{
		// If the bot's item entity doesn't match the cluster, perhaps a
		// dropped item was created or destroyed, meaning the bot's current
		// cluster points to the wrong dropped item information.  It's possible
		// to check if the dropped entity still exists, but recomputing isn't
		// *that* processor intense.  It's also safer.
		if(bs->item_centers[0] != bs->item_clusters[0]->center->ent)
			return qtrue;

		// Recompute if the cluster has fewer spawned items than before
		item_count = ClusterSpawnedItemCount(bs->item_clusters[0]);
		if(item_count < bs->item_cluster_count)
			return qtrue;

		// Save the new item count in case it increased
		bs->item_cluster_count = item_count;
	}

	// Recompute if the main goal area significantly changed
	if(bs->goal.areanum != bs->item_maingoal_area)
		return qtrue;

	// Use the cached item goal (if any)
	return qfalse;
}

/*
===================
BotEncounterRateLoc

Determine encounter traffic rates for a location, including the
amount of time the bot will have to attack and be attacked.  The
rates are filled out in the inputted "rates" structure.  Returns
a pointer to the region nearest the inputted point, or NULL if
an error occurred.

"nearby" is true if this function should use the bot's nearby player
information when estimating how many enemies and teammates the bot
will see.  For example, perhaps the region is in the bot's line of
sight and the bot expects these enemies to stay around to attack.  In
this case, the bot will incorporate can information about the current
number of nearby teammates and enemies when computing these rates.
===================
*/
region_t       *BotEncounterRateLoc(bot_state_t * bs, vec3_t loc, qboolean nearby,
									int teammates, int enemies, encounter_rates_t * rates)
{
	int             known_teammates, known_enemies, unknown_teammates, unknown_enemies;
	float           teammate_seen_rate, enemy_seen_rate;
	region_t       *region;
	history_t       teammate_traffic, enemy_traffic;

	// Look up the traffic statistics for the bot at this location and
	// the nearest region to that point
	region = BotTrafficData(bs, loc, &teammate_traffic, &enemy_traffic);
	if(!region)
		return NULL;

	// Compute the chance that any one particular enemy would be encountered
	enemy_seen_rate = enemy_traffic.actual / enemy_traffic.potential;

	// Check if local information can be used to help determine the bot's attack rate
	if(nearby)
	{
		known_teammates = bs->nearby_teammates;
		known_enemies = bs->nearby_enemies;
		unknown_teammates = teammates - known_teammates;
		unknown_enemies = enemies - known_enemies;
	}
	else
	{
		known_teammates = 0;
		known_enemies = 0;
		unknown_teammates = teammates;
		unknown_enemies = enemies;
	}

	// Determine the chance of seeing an enemy
	if(known_enemies)
		rates->see_enemy = 1.0;
	else
		rates->see_enemy = 1.0 - pow_int(1.0 - enemy_seen_rate, unknown_enemies);

	// The chance of seeing teammates and being attacked in return differs in teamplay
	if(game_style & GS_TEAM)
	{
		// Compute the chance the bot will encounter any one teammate other than itself
		teammate_seen_rate = teammate_traffic.actual / teammate_traffic.potential;

		// Compute the chance of seeing a teammate
		if(known_teammates)
			rates->see_teammate = 1.0;
		else
			rates->see_teammate = 1.0 - pow_int(1.0 - teammate_seen_rate, unknown_teammates);

		// This is the total attack rate of all enemies the bot might encounter
		// NOTE: This is the attack rate distributed among all teammates, not
		// necessarily the attacks directed solely at the bot
		rates->enemy_attack = enemy_seen_rate * unknown_enemies + known_enemies;

		// Teammates will draw enemy fire if the bot isn't a carrier
		// NOTE: +1 includes the bot
		if(!EntityIsCarrier(bs->ent))
			rates->enemy_attack /= teammate_seen_rate * unknown_teammates + known_teammates + 1;
	}
	else
	{
		// The bot has no teammates
		rates->see_teammate = 0.0;

		// In free for all modes, enemies attack each other with equal probability,
		// so the chance of being attacked equals the chance of attacking someone else
		rates->enemy_attack = rates->see_enemy;
	}

	// Here's the nearest region
	return region;
}

#ifdef DEBUG_AI
/*
=================
BotPrintItemTrack
=================
*/
void BotPrintItemTrack(tvl_t * tvl, int index, void *bs)
{
	item_cluster_t *cluster;

	// Print output stating the item is tracked
	cluster = *((item_cluster_t **) tvl_data(tvl, index));
	BotAI_Print(PRT_MESSAGE, "%s: Timed Item: Tracking %s\n", EntityNameFast(((bot_state_t *) bs)->ent), ClusterName(cluster));
}

/*
================
BotPrintItemLoss
================
*/
void BotPrintItemLoss(tvl_t * tvl, int index, void *bs)
{
	item_cluster_t *cluster;

	// Print output stating the item is no longer tracked
	cluster = *((item_cluster_t **) tvl_data(tvl, index));
	BotAI_Print(PRT_MESSAGE, "%s: Timed Item: Lost track of %s\n",
				EntityNameFast(((bot_state_t *) bs)->ent), ClusterName(cluster));
}
#endif

/*
==============
BotTimeCluster

Tries to time the respawn of the inputted cluster.
Returns true if the cluster will be tracked and
false if not.

NOTE: It is the caller's responsibility to
guarantee the bot has enough information to
determine when the item will respawn (be it from
line of sight with an already respawned item,
respawn sounds, or even teamplay messages.)
==============
*/
qboolean BotTimeCluster(bot_state_t * bs, item_cluster_t * cluster)
{
	float           respawn_time;
	qboolean        tracked;

	// Only track real clusters that respawn (eg. no dropped items, etc.)
	if(!cluster || !cluster->respawn_delay)
		return qfalse;

	// Determine when the bot should lose track of the cluster's timing
	//
	// NOTE: The +5 time guarantees there won't be boundary issues with comparators.  Also,
	// it's unlikely the item will get picked up sooner than a second after respawn, and
	// even if it does, all respawn times are at least five seconds, so it's reasonable
	// for the bot to know the items respawn up to this time.
	respawn_time = bs->command_time + cluster->respawn_delay + 5.0;

	// Try adding the cluster to the bot's timed value list of timed items
#ifdef DEBUG_AI
	if(bs->debug_flags & BOT_DEBUG_INFO_TIMED_ITEM)
		tracked = tvl_add(&bs->timed_items, &cluster, respawn_time, cluster->value, BotPrintItemTrack, BotPrintItemLoss, bs) >= 0;
	else
#endif
		tracked = tvl_add(&bs->timed_items, &cluster, respawn_time, cluster->value, 0, 0, NULL) >= 0;

	// Request an immediate item selection evaluation if the bot's timing state was updated
	if(tracked)
		bs->item_time = bs->command_time;

	return tracked;
}

/*
=================
BotTimeClusterLoc

Looks up a cluster closest to an input location
(where perhaps a respawn or pickup event was heard)
and tries to time the cluster's respawn.  Returns
true if the cluster was timed and false if not.
=================
*/
qboolean BotTimeClusterLoc(bot_state_t * bs, vec3_t loc)
{
	int             i;
	float           dist, nearest_dist;
	region_t       *region;
	item_cluster_t *cluster, *nearest;

	// Find the region nearest the input location
	region = LevelNearestRegion(loc);
	if(!region)
		return qfalse;

	// Find the cluster in the region whose center is closest to the location
	nearest = region->cluster;
	nearest_dist = DistanceSquared(bs->now.origin, nearest->center->ent->r.currentOrigin);
	for(i = 0; i < region->num_dynamic; i++)
	{
		// Only consider clusters that respawn
		cluster = region->dynamic[i];
		if(!cluster->respawn_delay)
			continue;

		// Ignore clusters that are too far away
		dist = DistanceSquared(bs->now.origin, cluster->center->ent->r.currentOrigin);
		if(dist > nearest_dist)
			continue;

		// This is currently the nearest cluster
		nearest = cluster;
		nearest_dist = dist;
	}

	// Try to time the nearest cluster
	return BotTimeCluster(bs, nearest);
}

/*
========================
BotItemClusterMaxRespawn

Given a cluster, determines for how many seconds the bot
will know the cluster's respawn state..  If the bot is not
timing this item, returns 0 (ie. only present data).
========================
*/
float BotItemClusterMaxRespawn(bot_state_t * bs, item_cluster_t * cluster)
{
	int             timed_index;
	float           max_respawn;

	// Check for clusters that aren't timed
	timed_index = tvl_data_index(&bs->timed_items, &cluster);
	if(timed_index < 0)
		return 0.0;

	// Return this cluster's timing data
	max_respawn = bs->timed_item_timeout[timed_index] - bs->command_time;
	if(max_respawn < 0.0)
		max_respawn = 0.0;
	return max_respawn;
}

/*
=========================
BotItemClusterOptionSetup

Setup the cluster for consideration as a potential
item pickup.  Returns false if there is no reason
to ever consider the cluster this frame (and the
cluster should be pruned out early).  Returns true
otherwise.
=========================
*/
qboolean BotItemClusterOptionSetup(bot_state_t * bs, cluster_option_t * option, bot_item_info_t * info)
{
	int             pickup_count, respawn, soonest_respawn, cluster_area;
	float          *cluster_loc;
	item_link_t    *item;
	gentity_t      *ent;
	bot_goal_t      cluster_goal;
	qboolean        visible;

	// Test which items in the cluster the bot can immediately pick up
	pickup_count = 0;
	for(item = option->cluster->start; item; item = item->next_near)
	{
		// Count the number of items in the cluster the bot can pick up
		//
		// NOTE: The inuse check is necessary because the item could have been
		// from a dropped item cluster that was picked up last frame (which
		// deallocates the entity).  This check guarantees the item is still
		// around on the level.
		if(item->ent->inuse && BG_CanItemBeGrabbed(g_gametype.integer, &item->ent->s, bs->ps))
			pickup_count++;
	}

	// Ignore clusters that contain no items the bot can immediately pick up
	if(!pickup_count)
		return qfalse;

	// Determine the soonest an item in this cluster will respawn
	//
	// NOTE: This time is in milliseconds
	//
	// FIXME: This code could be sped up if it were broken into max_respawn > 0
	// and max_respawn == 0 cases, since the zero case is far more likely and
	// somewhat shorter.  It's not clear if the speed improvement is worth
	// the increased code complexity, however.
	soonest_respawn = -1;
	for(item = option->cluster->start; item; item = item->next_near)
	{
		// Check for items that have already respawned
		ent = item->ent;
		if(ent->r.contents & CONTENTS_TRIGGER)
		{
			soonest_respawn = 0;
			break;
		}

		// Ignore items that won't respawn
		if(ent->think != RespawnItem)
			continue;

		// Use this respawn time if it's sooner than the previous one
		respawn = ent->nextthink - level.time;
		if(respawn < 0)
			respawn = 0;
		if(soonest_respawn < 0 || respawn < soonest_respawn)
			soonest_respawn = respawn;
	}

	// Ignore this cluster if no items will ever respawn
	if(soonest_respawn < 0)
		return qfalse;

	// Compute how many seconds of the bot's respawn timing remain (if any)
	option->max_respawn = BotItemClusterMaxRespawn(bs, option->cluster);

	// Ignore this cluster if the bot doesn't know when any items will respawn
	option->soonest_respawn = soonest_respawn * .001;
	if(option->max_respawn < option->soonest_respawn)
		return qfalse;

	// If the cluster is visible, encounter rates change and the cluster might get timed
	visible = BotEntityVisible(bs, option->cluster->center->ent);

	// Try timing the cluster if its visible and respawned
	if(visible && soonest_respawn == 0.0)
		BotTimeCluster(bs, option->cluster);

	// Determine travel time from the start to the cluster
	cluster_area = option->cluster->center->area;
	cluster_loc = option->cluster->center->ent->r.currentOrigin;
	option->from_start = LevelTravelTime(info->area, bs->now.origin, cluster_area, cluster_loc, bs->travel_flags);
	if(option->from_start < 0.0)
		return qfalse;

	// Determine travel time from the cluster to the end goal, if one exists
	if(info->time >= 0.0)
	{
		// Determine travel time from the cluster to the main goal
		option->to_goal = LevelTravelTime(cluster_area, cluster_loc, bs->goal.areanum, bs->goal.origin, bs->travel_flags);

		// Fail if picking up this item makes it impossible to reach the main goal
		if(option->to_goal < 0)
			return qfalse;
	}
	else
	{
		// No goal exists to travel to
		option->to_goal = -1.0;
	}

	// Look up or compute the expected attack rates in the cluster's region
	// and the nearest region
	if(!BotEncounterRateLoc(bs, option->cluster->center->ent->r.currentOrigin,
							info->nearby && visible, info->teammates, info->enemies, &option->rates))
		return qfalse;

	// Get the list of neighbors from this option to the end
	option->neighbors = LevelRegionNeighborList(option->cluster->region, info->end_region);
	option->num_neighbors = LevelNeighborListSize(option->neighbors);

	// Test if this cluster was last frame's first selected cluster (and that
	// cluster is still valid)
	option->selected = (bs->num_item_clusters &&
						option->cluster == bs->item_clusters[0] && option->cluster->center->ent == bs->item_centers[0]);

	// Consider this cluster for pickups
	return qtrue;
}

/*
=========================
BotItemClusterSelectSetup

Precomputes all data used by the information
state for cluster selection.
=========================
*/
void BotItemClusterSelectSetup(bot_state_t * bs, bot_item_info_t * info)
{
	// Set up some basic information for item cluster processing
	PlayInfoFromBot(&info->pi, bs);
	ResourceFromPlayer(&info->rs, bs->ent, &info->pi);

	info->area = LevelAreaEntity(bs->ent);
	info->teammates = BotTeammates(bs);
	info->enemies = BotEnemies(bs);
	info->nearby = (bs->enemy_score <= 1.0);

#ifdef DEBUG_AI
	// Get the next item debug frame to use for recording data
	info->frame = BotItemDebugNextFrame(bs);
#endif

	// Even if no enemies are connected, play as if an enemy could connect at any time
	if(info->enemies < 1)
		info->enemies = 1;

	// Compute the attack rates in the bot's current region.
	//
	// NOTE: All enemies are assumed to stay nearby, at least at the start of
	// prediction while players haven't had time to move away even if they wanted to.
	info->start_region = BotEncounterRateLoc(bs, bs->ps->origin, qtrue, info->teammates, info->enemies, &info->start_rates);

	// Determine how long it will take to reach the goal in seconds
	info->time = LevelTravelTime(info->area, bs->now.origin, bs->goal.areanum, bs->goal.origin, bs->travel_flags);

	// Compute the attack rates at the destination region
	if(info->time >= 0.0)
	{
		// Cache the goal's encounter rates
		info->end_region =
			BotEncounterRateLoc(bs, bs->goal.origin, info->nearby && BotGoalVisible(bs, &bs->goal),
								info->teammates, info->enemies, &info->end_rates);
	}
	else
	{
		// Reset the end region information
		info->end_region = NULL;
		memcpy(&info->end_rates, &info->start_rates, sizeof(encounter_rates_t));
	}
}

/*
==========================
BotItemClusterOptionsSetup

Given a bot, determines which clusters it should consider
as possible pickup options and sets up other data related
to the possibilities (eg. travel times).
==========================
*/
void BotItemClusterOptionsSetup(bot_state_t * bs, bot_item_info_t * info)
{
	int             i, num_last_clusters, ignored, next_dynamic;
	item_cluster_t *cluster, *last_clusters[MAX_PICKUPS];
	cluster_option_t *option;
	region_t       *region, **neighbors;

	// Verify the integrity of last frame's selected clusters and
	// consider them if they are still valid
	info->num_consider = 0;
	num_last_clusters = 0;
	ignored = 0;
	for(i = 0; i < bs->num_item_clusters; i++)
	{
		// Check if the cluster became invalid since last frame
		//
		// NOTE: This should only occur if a dropped item was picked
		// up last frame and this cluster handles a dropped item.  It
		// does NOT necessarily mean this cluster was the dropped item
		// that was picked up.
		cluster = bs->item_clusters[i];
		if(bs->item_centers[i] != cluster->center->ent)
		{
			// Check if this data is being tracked by a new cluster
			//
			// NOTE: Only dropped items can have their data locations
			// shift between frames because of their storage in a map.
			cluster = DroppedItemCluster(bs->item_centers[i]);

			// If the data isn't tracked, remove it
			if(!cluster)
			{
				ignored++;
				continue;
			}

			// Update the new cluster pointer
			bs->item_clusters[i] = cluster;
		}

		// Move this cluster to its appropriate array position if any
		// entries have been deleted
		if(ignored)
		{
			bs->item_clusters[i - ignored] = bs->item_clusters[i];
			bs->item_centers[i - ignored] = bs->item_centers[i];
		}

		// Consider pickup of this cluster
		option = &info->consider[info->num_consider++];
		option->cluster = cluster;

		// Note that this cluster was considered because it was selected last frame
		last_clusters[num_last_clusters++] = cluster;
	}

	// Account for deleted entries
	bs->num_item_clusters -= ignored;

	// Sort these clusters so it's easy to test if a cluster is in this list
	qsort(last_clusters, num_last_clusters, sizeof(item_cluster_t *), CompareVoidList);

	// Evaluate all clusters whose respawn the bot is timing
	for(i = 0; i < bs->timed_items.size && info->num_consider < MAX_CLUSTERS_CONSIDER; i++)
	{
		// Don't double consider a cluster
		if(bsearch(&cluster, last_clusters, num_last_clusters, sizeof(item_cluster_t *), CompareVoidList))
		{
			continue;
		}

		// Consider this cluster
		option = &info->consider[info->num_consider++];
		option->cluster = bs->timed_item_cluster[i];
	}

	// Consider all clusters near the bot's current region
	//
	// NOTE: Each region is, obviously, a neighbor of itself
	neighbors = LevelRegionNeighborList(info->start_region, info->end_region);
	for(i = 0; i < MAX_REGION_NEIGHBORS && neighbors[i]; i++)
	{
		// Evaluate the static cluster and each dynamic cluster in this region
		region = neighbors[i];
		cluster = region->cluster;
		next_dynamic = 0;
		while(cluster && info->num_consider < MAX_CLUSTERS_CONSIDER)
		{
			// Evaluate this cluster if it wasn't a previously processed timed cluster
			// and it wasn't considered because it was used last pickup frame
			if(!bsearch(&cluster, last_clusters, num_last_clusters,
						sizeof(item_cluster_t *), CompareVoidList)
			   &&
			   !bsearch(&cluster, bs->timed_item_cluster, bs->timed_items.size,
						sizeof(item_cluster_t *), bs->timed_items.compare))
			{
				option = &info->consider[info->num_consider++];
				option->cluster = cluster;
			}

			// Select the next dynamic cluster in this region if it exists;
			// otherwise skip to the next region
			if(next_dynamic < region->num_dynamic)
				cluster = region->dynamic[next_dynamic++];
			else
				break;
		}
	}

	// Setup each cluster for consideration
	ignored = 0;
	for(i = 0; i < info->num_consider; i++)
	{
		// Ignore the cluster if setup fails
		if(!BotItemClusterOptionSetup(bs, &info->consider[i], info))
		{
			ignored++;
			continue;
		}

		// Copy this cluster to the correct list position if necessary
		if(ignored > 0)
			memcpy(&info->consider[i - ignored], &info->consider[i], sizeof(cluster_option_t));
	}
	info->num_consider -= ignored;
}

/*
===================
BotNoPickupConsider

The bot considers the effectiveness of moving
directly to its main goal without picking up
any items on the way.  This is the "base case"
for item pickup.  Returns the score rate of
this option.
===================
*/
float BotNoPickupConsider(bot_state_t * bs, bot_item_info_t * info)
{
	resource_state_t rs;
	float           first_predict_time, second_predict_time;

	// Compute the base score rate differently when the bot has a real goal
	memcpy(&rs, &info->rs, sizeof(resource_state_t));
	if(info->time >= 0.0)
	{
		// Slightly penalize non-pickup if the bot chose to pick up an item last frame
		if(bs->num_item_clusters)
			info->time += bot_item_change_penalty_time.value;

		// Divide the path into two, half for the start and half for the end
		first_predict_time = info->time * 0.5;
		second_predict_time = first_predict_time;

		// Add extra prediction time to the end to meet the minium if required
		if(info->time < bot_item_predict_time_min.value)
			second_predict_time += bot_item_predict_time_min.value - info->time;

		// Predict the resource impact for each path segment
		ResourcePredictEncounter(&rs, first_predict_time, bs->enemy_score,
								 info->start_rates.see_enemy, info->start_rates.enemy_attack);
		ResourcePredictEncounter(&rs, second_predict_time, 1.0, info->end_rates.see_enemy, info->end_rates.see_enemy);
	}
	else
	{
		// Only predict the starting area
		ResourcePredictEncounter(&rs, bot_item_predict_time_min.value, bs->enemy_score,
								 info->start_rates.see_enemy, info->start_rates.enemy_attack);
	}

	// By default, the optimal choice is not to visit any item cluster
	return ResourceScoreRate(&rs);
}

/*
=========================
BotItemClusterSetConsider

Consider all possible subsets of clusters in "consider" to
pickup to determine the optimal pickup order.  The best
option and point value pair is saved in "info".
=========================
*/
void BotItemClusterSetConsider(bot_state_t * bs, bot_item_info_t * info)
{
	int             i, nearest, num_best_options;
	int             best_options[MAX_PICKUPS];
	float           time, initial_score, pickup_time, item_respawn_time;
	float           dir_change;
	float           score_rate, best_score_rate;
	resource_state_t rs[MAX_PICKUPS + 1], *this_rs, *last_rs, end_rs;
	item_cluster_t *pickup_chain[MAX_PICKUPS];
	item_link_t    *item;
	cluster_option_t *this_option, *last_option;
	encounter_rates_t *last_rates;
	index_subset_iter_t options;
	qboolean        selected;

	// Check for particularly close item clusters the bot can pick up
	nearest = -1;
	time = bot_item_autopickup_time.value;
	for(i = 0; i < info->num_consider; i++)
	{
		// Ignore the option if it's no closer than the closest option
		this_option = &info->consider[i];
		if(this_option->from_start >= time)
			continue;

		// Check if the cluster contains any items the bot can immediately pick up
		for(item = this_option->cluster->start; item; item = item->next_near)
		{
			// Ignore items that haven't respawned
			if(!(item->ent->r.contents & CONTENTS_TRIGGER))
				continue;

			// Don't automatically pickup items that are only marginally useful
			if(BotItemUtility(bs, item->ent) < 0.25)
				continue;

			// The cluster contains at least one item the bot can pickup
			break;
		}

		// Ignore the option if none of the items can be picked up right now
		if(!item)
			continue;

		// This is the closest cluster found so far
		nearest = i;
		time = this_option->from_start;
	}

	// Go directly to the closest cluster if one exists
	if(nearest >= 0)
	{
		// Record the best cluster pickup sequence
		bs->num_item_clusters = 1;
		bs->item_clusters[0] = info->consider[nearest].cluster;
		return;
	}

	// Consider the no pickup case
	num_best_options = 0;
	best_score_rate = BotNoPickupConsider(bs, info);

	// Iterate over all subsets of this size or less for this many indicies
	options.max_size = MAX_PICKUPS;
	options.range = info->num_consider;
	isi_start(&options);

	// The null state (no item pickups before goal) has been already been handled
	//
	// NOTE: See BotNoPickupConsider() for more information
	isi_next(&options);

	// Load the starting resource state
	memcpy(&rs[0], &info->rs, sizeof(resource_state_t));

	// Iterate over each possible cluster subset
	while(options.valid)
	{
		// Consider picking up this cluster next
		this_option = &info->consider[options.index[options.size - 1]];
		this_rs = &rs[options.size];

		// This is the last considered option
		//
		// NOTE: NULL means the previous option is the bot's starting position
		if(options.size > 1)
			last_option = &info->consider[options.index[options.size - 2]];
		else
			last_option = NULL;

		// Look up the encounter rates at the previous state and
		// the travel time to the current cluster
		if(last_option)
		{
			// Look up data for the previous cluster option
			time = LevelRegionTravelTime(last_option->cluster->region, this_option->cluster->region);
			last_rates = &last_option->rates;

			// Since the previous option was not at the bot's starting location,
			// just assume enemies near that location are worth the standard amount
			initial_score = 1.0;
		}
		else
		{
			// Look up data for the starting state
			time = this_option->from_start;
			last_rates = &info->start_rates;

			// Penalize the travel time to the first cluster if selecting it incurs
			// a change in the bot's travel plans right now
			//
			// NOTE: This accounts for the deceleration associated with changing
			// goals.  The path time estimation code only knows positions, not
			// velocities, and it does not take into account how current velocity
			// affects path movement.  This is just an estimate but it's better than
			// the "default" estimate of 0 seconds.
			time += bot_item_change_penalty_time.value;

			// Enemies near the start might be worth more points
			initial_score = bs->enemy_score;
		}

		// Preemptively skip the cluster if no item will respawn before the
		// bot gets there
		pickup_time = this_rs->time + time;
		if(pickup_time < this_option->soonest_respawn)
		{
			isi_skip(&options);
			continue;
		}

		// Skip clusters that aren't neighbors of the previous cluster, since
		// that travel path is guaranteed to be pretty long.
		if(last_option &&
		   !LevelRegionIsNeighbor(this_option->cluster->region, last_option->neighbors, last_option->num_neighbors))
		{
			isi_skip(&options);
			continue;
		}

		// Load the last good resource state into the next slot
		//
		// NOTE: Recall that slot 0 is for the starting state, so the computations
		// for the current pickup are stored in index (size-1)+1, which is just size.
		last_rs = this_rs - 1;
		memcpy(this_rs, last_rs, sizeof(resource_state_t));

		// Add items to the resource state that will respawn in this many seconds or less
		//
		// NOTE: This will probably be zero unless the bot is timing this cluster.
		if(pickup_time < this_option->max_respawn)
			item_respawn_time = pickup_time;
		else
			item_respawn_time = this_option->max_respawn;

		// Predict the bot's resource state along this path segment
		ResourcePredictEncounter(this_rs, time * .5, initial_score, last_rates->see_enemy, last_rates->enemy_attack);
		ResourcePredictEncounter(this_rs, time * .5, 1.0, this_option->rates.see_enemy, this_option->rates.enemy_attack);

		// Skip all pickup subsets that match the current state if no items in this
		// cluster option can be picked up by the time the bot gets there
		//
		// NOTE: This is different from checking if the items won't have respawned.
		// A health item could have respawned but the bot might be unable to pick it
		// up because the bot is at full health.
		if(!ResourceAddCluster(this_rs, this_option->cluster, item_respawn_time,
							   this_option->rates.see_teammate, this_option->rates.see_enemy))
		{
			isi_skip(&options);
			continue;
		}

		// Setup the state data for the option of the bot going directly
		// to the end goal from this option
		memcpy(&end_rs, this_rs, sizeof(resource_state_t));

		// The case of going to a goal is handled differently from the goalless
		// cases where the bot just stays at its last location
		if(this_option->to_goal < 0.0)
		{
			// Spend extra time predicting after the pickup finishes
			ResourcePredictEncounter(&end_rs, bot_item_predict_time_min.value, 1.0,
									 last_rates->see_enemy, last_rates->enemy_attack);
		}
		else
		{
			// Predict the half of the path at the last cluster's rates
			time = this_option->to_goal * 0.5;
			ResourcePredictEncounter(&end_rs, time, 1.0, last_rates->see_enemy, last_rates->enemy_attack);

			// Possibly pad the remaining time to make sure the state predicts long enough
			if(time < bot_item_predict_time_min.value)
				time = bot_item_predict_time_min.value;

			// Predict the remaining portion of the path at the goal's rates
			ResourcePredictEncounter(&end_rs, time, 1.0, info->end_rates.see_enemy, info->end_rates.enemy_attack);
		}

		// Look up this sequence's score rate
		score_rate = ResourceScoreRate(&end_rs);

		// Favor cluster chains that use the same initial cluster.  In other
		// words, don't change the first selected cluster unless there's a
		// clear reason to do so.
		if(info->consider[options.index[0]].selected)
			score_rate *= bot_item_change_penalty_factor.value;

		// Remember the first cluster if this option was better than the
		// best known rate
		selected = (best_score_rate < score_rate);
		if(selected)
		{
			// Save the new information
			num_best_options = options.size;
			memcpy(best_options, options.index, sizeof(best_options));
			best_score_rate = score_rate;
		}

#ifdef DEBUG_AI
		// Dereference the option indicies to the actual addresses of their clusters
		for(i = 0; i < options.size; i++)
			pickup_chain[i] = info->consider[options.index[i]].cluster;

		// Note that this option was considered
		FrameOptionAdd(info->frame, pickup_chain, options.size, score_rate, selected);
#endif

		// Evaluate the next option
		isi_next(&options);
	}

	// Record the best cluster pickup sequence
	bs->num_item_clusters = num_best_options;
	for(i = 0; i < num_best_options; i++)
		bs->item_clusters[i] = info->consider[best_options[i]].cluster;
}

/*
=================
BotGetItemCluster

Figure out which item cluster pickup the bot should take
given its main goal choice of "goal".

NOTE: It's possible that the cluster this function returns
is from a dropped item.  Dropped item cluster pointers are
not guaranteed to point to the same item entity between
frames.  If the caller of this function wants to store this
pointer between frames, it will need to handle the cluster
pointer coherency itself.

NOTE: I designed code to ensure all entities had the same
cluster pointers between frames.  Unfortunately, it was far,
far, far too complicated for the minimal benefit it gave.
=================
*/
void BotGetItemCluster(bot_state_t * bs)
{
	int             i;
	bot_item_info_t info;

	// Only do item pickups when the bot is in an item region
	//
	// NOTE: This isn't just for players on an itemless level;
	// it also counts dead players and spectators.
	if(player_region[bs->entitynum] < 0)
	{
		bs->num_item_clusters = 0;
		return;
	}

#ifdef DEBUG_AI
	// Don't pickup items if item pickup has been turned off
	if(bs->debug_flags & BOT_DEBUG_MAKE_ITEM_STOP)
	{
		bs->num_item_clusters = 0;
		return;
	}

	// Remove expired entries from the timed item list
	if(bs->debug_flags & BOT_DEBUG_INFO_TIMED_ITEM)
		tvl_update_time(&bs->timed_items, bs->command_time, BotPrintItemLoss, bs);
	else
#endif
		tvl_update_time(&bs->timed_items, bs->command_time, 0, NULL);

	// Setup the information used for considering clusters
	BotItemClusterSelectSetup(bs, &info);

	// Determine which clusters should be considered as potential options
	BotItemClusterOptionsSetup(bs, &info);

	// Consider all possible number of items to pickup before going to the goal
	BotItemClusterSetConsider(bs, &info);

	// Record the current cluster centers so a reuse in the item data
	// structure can be detected
	for(i = 0; i < bs->num_item_clusters; i++)
		bs->item_centers[i] = bs->item_clusters[i]->center->ent;

	// Count the number of items currently in the first selected cluster
	bs->item_cluster_count = ClusterSpawnedItemCount(bs->item_clusters[0]);
}

/*
================
BotClusterSelect

Gets an appropriate cluster to move towards
and does all required setup for using it.
================
*/
void BotClusterSelect(bot_state_t * bs)
{
	int             damage;

	// Do not recompute the item goal unless necessary
	damage = HealthArmorToDamage(bs->ps->stats[STAT_HEALTH], bs->ps->stats[STAT_ARMOR]);
	if(!BotRecomputeItemGoal(bs, damage))
		return;

	// Remember the area of the current main goal (towards which the item goal was computed)
	bs->item_maingoal_area = bs->goal.areanum;

	// Compute the item goal again in a little bit
	bs->item_time = bs->command_time + ITEM_RECOMPUTE_DELAY;

	// The bot can currently sustain this much damage
	bs->item_bot_damage = damage;

	// Search for an item cluster to visit on the way to the main goal
	BotGetItemCluster(bs);
}

/*
====================
BotClusterItemSelect

Processes the bot's chain of selected clusters
to find which item in the chain the bot should
pickup first.  Returns a pointer to the selected
item link, or NULL if no valid pickup was found.
====================
*/
item_link_t    *BotClusterItemSelect(bot_state_t * bs)
{
	int             i;
	float           dist, nearest_dist;
	item_link_t    *item, *nearest_item;
	qboolean        respawned, nearest_respawned;

	// No items have currently been found
	nearest_item = NULL;
	nearest_dist = -1;
	nearest_respawned = qfalse;

	// Check the clusters in order for a potential item
	for(i = 0; i < bs->num_item_clusters; i++)
	{
		// Try to find an item in this cluster to pickup
		for(item = bs->item_clusters[0]->start; item; item = item->next_near)
		{
			// Always ignore items that the bot can't pickup right now
			//
			// NOTE: This function does not test the respawn status of the item.
			// It only tests whether the bot could pick up the item if it were there.
			if(!item->ent->inuse || !BG_CanItemBeGrabbed(g_gametype.integer, &item->ent->s, bs->ps))
				continue;

			// Determine this item's respawn state and distance
			respawned = (item->ent->r.contents & CONTENTS_TRIGGER);
			dist = DistanceSquared(item->ent->r.currentOrigin, bs->now.origin);

			// This item might get ignored if another option exists
			if(nearest_item)
			{
				// Ignore unspawned items if a spawned item exists
				if(nearest_respawned && !respawned)
					continue;

				// Ignore items of the same respawn status that are further away
				if(nearest_respawned == respawned && nearest_dist <= dist)
					continue;
			}

			// This is the best cluster item to pick up so far
			nearest_item = item;
			nearest_dist = dist;
			nearest_respawned = respawned;
		}

		// Head for the nearest item if one exists
		if(nearest_item)
			return nearest_item;
	}

	// No options were found
	return NULL;
}

/*
===========
BotItemGoal

Input goal is the location the bot wants to move towards.
If the bot decides to pick up an item on the way to that goal,
the item goal will overwrite the input goal.
===========
*/
void BotItemGoal(bot_state_t * bs)
{
	item_link_t    *item;
	bot_goal_t      item_goal;

#ifdef DEBUG_AI
	gentity_t      *last_cluster_item, *last_item;
#endif

	// Only select items if the bot's item data has been setup
	if(!BotItemSetup(bs))
		return;

	// Update path prediction information
	BotPathUpdate(bs, &bs->item_path);

#ifdef DEBUG_AI
	// Cache the old selected item to detect changes
	last_cluster_item = bs->item_centers[0];
	last_item = bs->item_ent;
#endif

	// Possibly select a new sequence of item clusters to visit
	BotClusterSelect(bs);

	// Determine which item in the cluster chain to pickup
	item = BotClusterItemSelect(bs);

	// Continue toward the main goal if no valid pickup items were found.
	//
	// NOTE: This will most likely occur when the pickup chain has length 0 (no
	// items selected for pickup.)  But it could also occur if the bot selected
	// a cluster where it couldn't pick up any of the items at this moment in
	// time.  In theory the resource prediction code should never do this, but
	// it's best to check anyway.  And if the prediction code selects such a
	// cluster, what does this code care?  This pickup code has the right to
	// accept or deny suggestions for which cluster to move towards.
	if(!item)
	{
		// Reset last selected item
		bs->item_ent = NULL;

#ifdef DEBUG_AI
		// Announce change to no item selection
		if((bs->debug_flags & BOT_DEBUG_INFO_ITEM) && (last_cluster_item != bs->item_centers[0] || last_item != bs->item_ent))
			BotAI_Print(PRT_MESSAGE, "%s: Item: Cluster: %s, Item: NONE\n",
						EntityNameFast(bs->ent), ClusterName(bs->item_clusters[0]));
#endif

		return;
	}
	bs->item_ent = item->ent;

#ifdef DEBUG_AI
	// Announce changes in item selection
	if((bs->debug_flags & BOT_DEBUG_INFO_ITEM) && (last_cluster_item != bs->item_centers[0] || last_item != bs->item_ent))
		BotAI_Print(PRT_MESSAGE, "%s: Item: Cluster: %s, Item: %s (%.f, %.f, %.f)\n",
					EntityNameFast(bs->ent), ClusterName(bs->item_clusters[0]),
					EntityNameFast(bs->item_ent),
					bs->item_ent->r.currentOrigin[0], bs->item_ent->r.currentOrigin[1], bs->item_ent->r.currentOrigin[2]);
#endif

	// Plan a route to the item if possible
	GoalEntityArea(&item_goal, bs->item_ent, item->area);
	BotPathPlan(bs, &bs->item_path, &item_goal, &bs->goal);
}
