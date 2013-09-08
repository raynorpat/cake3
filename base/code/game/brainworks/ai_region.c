// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_region.c
 *
 * Functions used to process the regions defined by items
 *****************************************************************************/

#include "ai_main.h"
#include "ai_vars.h"
#include "ai_region.h"

#include "ai_client.h"
#include "ai_entity.h"
#include "ai_item.h"
#include "ai_level.h"
#include "ai_resource.h"


// List and tree representation of all regions in the level.
//
// NOTE: Each static cluster has its own region, so num_regions
// and num_clusters_static (see ai_item.c) should always be equal.
region_t        region_list[MAX_REGIONS];
octree_node_t   region_tree[MAX_REGIONS];
int             num_regions = 0;

// Cached table of travel times from each region to each region.
float           region_times[MAX_REGIONS][MAX_REGIONS];

// Historical traffic information near each region (item) in the level
// for members of each team.
history_t       region_traffic[MAX_REGIONS][TEAM_NUM_TEAMS];

// Which region each player is currently in, or -1 for no region.
int             player_region[MAX_CLIENTS];


/*
==========
RegionName

Looks up a simple name for a (possibly void* casted) region
==========
*/
const char     *RegionName(const void *region)
{
	if(region == NULL)
		return EntityNameFast(NULL);
	return ClusterName(((const region_t *)region)->cluster);
}

#ifdef DEBUG_AI
/*
===========
PrintRegion
===========
*/
void PrintRegion(region_t * regions, int index, int indent)
{
	int             i;
	char            tab[32];
	region_t       *region;

	// Compute initial spacing tab
	for(i = 0; i < 2 * indent; i++)
		tab[i] = ' ';
	tab[i] = '\0';

	// Check for NULL regions
	if(index < 0)
	{
		G_Printf("%sRegion NULL\n", tab);
		return;
	}

	// Print basic information about the region
	region = &regions[index];
	G_Printf("%sRegion %i:\n", tab, index);
	PrintCluster(region->cluster, indent + 1);

	// Print a list of region neighbors
	G_Printf("%s  Local Neighbors:", tab);
	for(i = 0; i < MAX_REGION_NEIGHBORS; i++)
	{
		if(region->local_neighbor[i])
			G_Printf(" #%i", region->local_neighbor[i] - regions);
		else
			break;
	}
	G_Printf("\n");
}
#endif

/*
==============
RegionLocation

Looks up the location vector of (possibly void* casted) region.

NOTE: This regions's cluster's center entity MUST have a static
location or very bad things will happen!
==============
*/
const float    *RegionLocation(const void *region)
{
	return ((const region_t *)region)->cluster->center->ent->r.currentOrigin;
}

/*
=================
CanProcessRegions

Check if its safe to access items and regions
=================
*/
qboolean CanProcessRegions(void)
{
	// Region processing must wait for item setup to complete
	return CanProcessItems();
}

/*
================
LevelRegionIndex

Given a region, returns its region index, or
-1 if the input was a region not in the region
list (possibly NULL).
================
*/
int LevelRegionIndex(const region_t * region)
{
	int             index;

	// Check for out of bounds region pointers
	index = region - region_list;
	if(index < 0 || index >= num_regions)
		return -1;

	// The region appears to be valid
	return index;
}

/*
==================
LevelNearestRegion

Returns a pointer to the region nearest the input
point.  Returns NULL if no region was found.
==================
*/
region_t       *LevelNearestRegion(vec3_t point)
{
	region_t       *region;

	// Wait until the regions in the level are set up
	if(!CanProcessRegions())
		return NULL;

	// Determine which region the point is nearest
	region = octree_neighbor(point, region_tree, RegionLocation);
	if(!region)
		return NULL;

	// Return the region
	return region;
}

/*
===================
LevelNearestRegions

Finds the closest regions to the point, up to a maximum
of "max_neighbors".  Fills out the regions found and their
distances in the inputted arrays, and returns the number
of neighbors actually found (possibly 0).
===================
*/
int LevelNearestRegions(vec3_t point, int max_neighbors, region_t * neighbors[], float dists[])
{
	int             i, num_neighbors;

	// Wait until the regions in the level are set up
	if(!CanProcessRegions())
		return 0;

	// Find the closest neighbors and the square of their distances
	num_neighbors = octree_neighbors(point, region_tree, RegionLocation, max_neighbors, neighbors, dists);

	// Compute the actual distances
	for(i = 0; i < num_neighbors; i++)
		dists[i] = sqrt(dists[i]);

	// Let the caller know how many neighbors were found
	return num_neighbors;
}

/*
=======================
LevelNearestRegionIndex

Returns the index of the nearest region to the
input point.  Returns -1 if no region was found.
=======================
*/
int LevelNearestRegionIndex(vec3_t point)
{
	return LevelRegionIndex(LevelNearestRegion(point));
}

/*
======================
LevelNearestRegionName

FIXME: It might be nice if this function only looked for regions
whose clusters were fairly valuable.  "Near the Red Armor" means
a lot more than "Near the Armor Shard".  Of course, another option
is precomputing highly descriptive names for every static cluster.
======================
*/
const char     *LevelNearestRegionName(vec3_t point)
{
	region_t       *region;

	// Look up the nearest region
	region = LevelNearestRegion(point);
	if(!region)
		return NULL;

	// Return the region's name
	return RegionName(region);
}

/*
===========================
LevelRegionTravelTimesSetup

Precompute and cache the travel times
between every pair of static clusters.
===========================
*/
void LevelRegionTravelTimesSetup(void)
{
	int             from, to;
	item_link_t    *start, *end;

	// Initialize the travel times
	for(from = 0; from < MAX_REGIONS; from++)
		for(to = 0; to < MAX_REGIONS; to++)
			region_times[from][to] = -1.0;

	// Compute travel times for regions that are present
	//
	// NOTE: This code is *VERY* slow because of how many times the travel times
	// are computed.  On a very large level with a slow processor, it could take
	// around a second and a half to compute.  That is why these computations
	// are done on startup and cached.
	for(from = 0; from < num_regions; from++)
	{
		// Compute times starting from this region
		start = region_list[from].cluster->center;

		// Check the travel times to all other regions
		for(to = 0; to < num_regions; to++)
		{
			// Might as well save a few computations
			if(from == to)
			{
				region_times[from][to] = 0.0;
				continue;
			}

			// Compute the travel time from the starting region to this region
			end = region_list[to].cluster->center;
			region_times[from][to] =
				LevelTravelTime(start->area, start->ent->r.currentOrigin, end->area, end->ent->r.currentOrigin, TFL_DEFAULT);
		}
	}
}

/*
=====================
LevelRegionTravelTime

Look up the precomputed travel time
between the specified pair of regions.

Returns -1 if the travel time between the
regions has not been computed (or, of course,
if travel between them is impossible).
=====================
*/
float LevelRegionTravelTime(region_t * from, region_t * to)
{
	int             from_index, to_index;

	// Look up the table indicies for these regions
	from_index = LevelRegionIndex(from);
	to_index = LevelRegionIndex(to);
	if(from_index < 0 || to_index < 0)
		return -1;

	// Return the data for that region pair
	return region_times[from_index][to_index];
}

/*
=======================
LevelRegionNeighborList

Returns the list of all regions that neighbor
the path between the inputted region and its
destination.  If no destination region is
specified, returns the list of neighbors that
are easily reachable from the source.

NOTE: Each region list contains at most
MAX_REGION_NEIGHBORS entries.  If the list
contains fewer entries than this, the list will
be NULL terminated.
=======================
*/
region_t      **LevelRegionNeighborList(region_t * from, region_t * to)
{
	int             to_index;

	// Use the local neighbor list for invalid destination
	to_index = LevelRegionIndex(to);
	if(to_index < 0 || to_index >= num_regions)
		return from->local_neighbor;

	// Look up the neighbor list for the requested path
	return from->path_neighbor[to_index];
}

/*
=====================
LevelNeighborListSize

Returns the length of the inputted neighbor list.
=====================
*/
int LevelNeighborListSize(region_t ** neighbors)
{
	int             num_neighbors;

	// Determine how many neighbors are in the list
	num_neighbors = MAX_REGION_NEIGHBORS;
	while(num_neighbors > 0 && !neighbors[num_neighbors - 1])
		num_neighbors--;
	return num_neighbors;
}

/*
=====================
LevelRegionIsNeighbor

Test if the inputted region is a neighbor of
the inputted neighbor list of the given size.
=====================
*/
qboolean LevelRegionIsNeighbor(region_t * region, region_t ** neighbors, int num_neighbors)
{
	// Test if the region exists in the neighbor list
	return (bsearch(&region, neighbors, num_neighbors, sizeof(region_t *), CompareVoidList) ? qtrue : qfalse);
}

/*
================
LevelRegionReset

Reset all preprocessed item region data
================
*/
void LevelRegionReset(void)
{
	// Reset region data
	num_regions = 0;
}

/*
=======================
LevelRegionResetDynamic

Reset which dynamic clusters each region tracks.
=======================
*/
void LevelRegionResetDynamic(void)
{
	int             i;

	for(i = 0; i < num_regions; i++)
		region_list[i].num_dynamic = 0;
}

/*
================
LevelRegionSetup

Setup the regions in the level based on the
supplied static item clusters.
================
*/
void LevelRegionSetup(item_cluster_t * clusters, int num_clusters)
{
	int             i, j, from, to, num_times;
	float           from_time, to_time, travel_weight, actual, potential;
	vec3_t          region_center, neighbor_center;
	trace_t         trace;
	region_t       *region, *neighbor, *start, *end, **neighbor_list;
	entry_float_int_t time_neighbor[MAX_REGIONS];	// Pair of travel time and region index

	// Initialize the item regions using the static item clusters
	for(num_regions = 0; num_regions < num_clusters && num_regions < MAX_REGIONS; num_regions++)
	{
		// Set up the region
		region = &region_list[num_regions];
		region->cluster = &clusters[num_regions];
		memset(region->local_neighbor, 0, sizeof(region->local_neighbor));
		memset(region->path_neighbor, 0, sizeof(region->path_neighbor));
		region->num_dynamic = 0;

		// Let the static cluster know which region it belongs to
		clusters[num_regions].region = region;

		// Store it in the unsorted tree
		region_tree[num_regions].data = region;
	}

	// Setup the travel times between every pair of regions
	LevelRegionTravelTimesSetup();

	// Sort the item regions into an octree for fast lookup of nearest cluster
	//
	// NOTE: The root of this tree will be moved to &region_tree[0]
	octree_assemble(region_tree, num_regions, RegionLocation);

	// Compute a list of nearest local neighbors for each region
	for(from = 0; from < num_regions; from++)
	{
		// Reset the timing list and setup the starting region and item
		num_times = 0;
		region = &region_list[from];
		neighbor_list = region->local_neighbor;

		// Load the travel times to all regions into the potential neighbor array
		for(i = 0; i < num_regions; i++)
		{
			// Compute the travel time from the base cluster to this cluster
			//
			// NOTE: This means each region is a neighbor of itself.
			travel_weight = LevelRegionTravelTime(region, &region_list[i]);
			if(travel_weight < 0.0)
				continue;

			// Save this time/region pair in the list
			time_neighbor[num_times].key = travel_weight;
			time_neighbor[num_times++].value = i;
		}

		// Sort the neighbor list by ascending travel time
		qsort(time_neighbor, num_times, sizeof(entry_float_int_t), CompareEntryFloat);

		// Save the nearest regions in this region's local neighbor list
		if(num_times > MAX_REGION_NEIGHBORS)
			num_times = MAX_REGION_NEIGHBORS;
		for(i = 0; i < num_times; i++)
			neighbor_list[i] = &region_list[time_neighbor[i].value];

		// Clear out the remaining entries if any
		while(i < MAX_REGION_NEIGHBORS)
			neighbor_list[i++] = NULL;

		// Resort the neighbor list by pointer for fast searching
		qsort(neighbor_list, num_times, sizeof(region_t *), CompareVoidList);

		// Compute the center of this region's location at bot body level
		VectorCopy(region->cluster->center->ent->r.currentOrigin, region_center);
		region_center[2] += DEFAULT_VIEWHEIGHT;

		// Check which neighboring regions are visible from this region
		region->visible = 0x0000;
		for(i = 0; i < num_times; i++)
		{
			// A region is always visible from itself
			neighbor = neighbor_list[i];
			if(region == neighbor)
			{
				region->visible |= (1 << i);
				continue;
			}

			// Check if the neighbor is visible from the base region and mark it if so
			trap_Trace(&trace, region_center, NULL, NULL, neighbor_center, ENTITYNUM_NONE, MASK_SOLID);
			if(trace.fraction >= 1.0)
				region->visible |= (1 << i);
		}
	}

	// Compute a list of neighbors encountered on the path from one cluster to another
	for(from = 0; from < num_regions; from++)
	{
		// Compute all path neighbors from this region
		start = &region_list[from];

		// Check all path destinations for this region
		for(to = 0; to < num_regions; to++)
		{
			// Check path neighbors to this destination
			end = &region_list[to];
			neighbor_list = start->path_neighbor[to];

			// Evaluate each waypoint on the path from the start to the end
			num_times = 0;
			for(i = 0; i < num_regions; i++)
			{
				// Consider this region as a neighbor
				region = &region_list[i];

				// Look up travel times from the start to the region and the region to the end
				from_time = LevelRegionTravelTime(start, region);
				to_time = LevelRegionTravelTime(region, end);

				// Ignore waypoints that make the path untraversable
				if(from_time < 0.0 || to_time < 0.0)
					continue;

				// Evaluate how close this cluster is to the path, weighting earlier clusters
				// more than later ones (since it's more efficient to get nearby clusters first)
				travel_weight = interpolate(from_time, to_time, bot_item_path_neighbor_weight.value);

				// Record how close this cluster was to the path
				time_neighbor[num_times].key = travel_weight;
				time_neighbor[num_times++].value = i;
			}

			// Sort the possible waypoints by proximity to the path
			qsort(time_neighbor, num_times, sizeof(entry_float_int_t), CompareEntryFloat);

			// Save the closets regions as neighbors of this path
			if(num_times > MAX_REGION_NEIGHBORS)
				num_times = MAX_REGION_NEIGHBORS;
			for(i = 0; i < num_times; i++)
				neighbor_list[i] = &region_list[time_neighbor[i].value];

			// Clear out the remaining entries if any
			while(i < MAX_REGION_NEIGHBORS)
				neighbor_list[i++] = NULL;

			// Resort the path neighbors by pointer for faster access
			qsort(neighbor_list, num_times, sizeof(region_t *), CompareVoidList);
		}
	}

	// Seed each region with five seconds of generic traffic data.  Assume
	// each region can see five players in five other regions.   Therefore,
	// the initial chance of seeing a player in a region is 5.0 / num_regions
	potential = 100.0;			// 20 frames per second for 5 seconds
	if(num_regions < 5)
		actual = potential;
	else
		actual = (potential * 5) / num_regions;

	// Separately initialize each region (area around a static item cluster)
	for(i = 0; i < num_regions; i++)
	{
		for(j = 0; j < TEAM_NUM_TEAMS; j++)
		{
			region_traffic[i][j].actual = actual;
			region_traffic[i][j].potential = potential;
		}
	}

#ifdef DEBUG_AI
	// Print out the item region octree if requested
	if(bot_debug_item.integer)
		octree_print(region_tree, RegionName);
#endif

	// Always print the region setup completion message
	G_Printf("Divided the level into %i regions.\n", num_regions);
}

/*
==================
ClusterAddToRegion
==================
*/
void ClusterAddToRegion(item_cluster_t * cluster)
{
	// Only add clusters with items in them
	if(!cluster->center || !cluster->center->ent)
		return;

	// Always set the cluster's current region
	cluster->region = LevelNearestRegion(cluster->center->ent->r.currentOrigin);

	// Track this cluster in the region's dynamic list if possible
	if(cluster->region && cluster->region->num_dynamic < MAX_REGION_DYNAMIC)
		cluster->region->dynamic[cluster->region->num_dynamic++] = cluster;
}

/*
=======================
LevelPlayerRegionUpdate

Updates the tracking of which player is in which
region, and the general traffic of which players
have been spotted in which regions of the level.
=======================
*/
void LevelPlayerRegionUpdate(void)
{
	int             player, neighbor, team, region_index, neighbor_index;
	int             num_players[TEAM_NUM_TEAMS];
	region_t       *region;
	gentity_t      *ent;

	// Update which region each player is currently in
	memset(num_players, 0, sizeof(num_players));
	for(player = 0; player < MAX_CLIENTS; player++)
	{
		// Ignore spectators (and other teamless entities)
		ent = &g_entities[player];
		team = EntityTeam(ent);
		if(team == TEAM_SPECTATOR)
		{
			player_region[player] = -1;
			continue;
		}

		// Found another player of this team
		num_players[team]++;

		// Compute this player's region
		region = LevelNearestRegion(ent->r.currentOrigin);
		player_region[player] = LevelRegionIndex(region);

		// Don't track players without a region
		if(player_region[player] < 0)
			continue;

		// Remember which regions from which this player was probably visible
		for(neighbor = 0; neighbor < MAX_REGION_NEIGHBORS && region->local_neighbor[neighbor]; neighbor++)
		{
			// Ignore neighbors that aren't visible from this region
			if(!(region->visible & (1 << neighbor)))
				continue;

			// Mark that this player was visible near this region
			neighbor_index = LevelRegionIndex(region->local_neighbor[neighbor]);
			region_traffic[neighbor_index][team].actual++;
		}
	}

	// Update the number of possible sightings of each team in each region
	for(region_index = 0; region_index < num_regions; region_index++)
		for(team = 0; team < TEAM_NUM_TEAMS; team++)
			region_traffic[region_index][team].potential += num_players[team];
}

/*
==============
BotTrafficData

Obtains the traffic data at the given point for teammates and enemies
of the given bot.  Returns a pointer to the region nearest that point,
or NULL if there are no defined regions.
==============
*/
region_t       *BotTrafficData(bot_state_t * bs, vec3_t loc, history_t * teammate, history_t * enemy)
{
	int             i, num_neighbors, team;
	float           dists[TRAFFIC_NEIGHBORS], weights[TRAFFIC_NEIGHBORS], weight_total, min_dist;
	region_t       *neighbors[TRAFFIC_NEIGHBORS], *closest;
	history_t      *traffic, *team_traffic, *enemy_traffic;
	static history_t no_traffic = { 0, 1 };	// Fudged traffic data for situations that
	// lack traffic (eg. teammates in free for all)

	// Find the closest regions to this point
	num_neighbors = LevelNearestRegions(loc, TRAFFIC_NEIGHBORS, neighbors, dists);

	// Setup nothing if no neighbors could be found
	if(num_neighbors < 0)
		return NULL;

	// Weight nearer regions more than further regions and find the nearest neighbor
	memset(weights, 0, sizeof(weights));
	weight_total = 0.0;
	min_dist = -1;
	closest = NULL;
	for(i = 0; i < num_neighbors; i++)
	{
		// If this point is the exact center of a region, use only that region's data
		if(dists[i] <= 0)
		{
			memset(weights, 0, sizeof(weights));
			weights[i] = 1.0;
			weight_total = weights[i];
			closest = neighbors[i];
			break;
		}

		// The weight is inversely proportional to the distance
		weights[i] = 1.0 / dists[i];
		weight_total += weights[i];

		// Test if this region is nearer than the other options
		if(!closest || dists[i] < min_dist)
		{
			closest = neighbors[i];
			min_dist = dists[i];
		}
	}

	// Normalize the sum of the weights to 1.0
	if(weight_total > 0.0)
	{
		for(i = 0; i < num_neighbors; i++)
			weights[i] /= weight_total;
	}

	// Find out what team the bot is on
	team = EntityTeam(bs->ent);

	// Add each region's data to the average
	memset(teammate, 0, sizeof(history_t));
	memset(enemy, 0, sizeof(history_t));
	for(i = 0; i < num_neighbors; i++)
	{
		// Look up the traffic data for this region
		traffic = region_traffic[LevelRegionIndex(neighbors[i])];

		// Organize the data as teammate traffic and enemy traffic
		switch (team)
		{
			case TEAM_RED:
				team_traffic = &traffic[TEAM_RED];
				enemy_traffic = &traffic[TEAM_BLUE];
				break;

			case TEAM_BLUE:
				team_traffic = &traffic[TEAM_BLUE];
				enemy_traffic = &traffic[TEAM_RED];
				break;

			case TEAM_FREE:
			default:
				team_traffic = &no_traffic;
				enemy_traffic = &traffic[TEAM_FREE];
				break;
		}

		// Track teammate traffic
		teammate->actual += weights[i] * team_traffic->actual;
		teammate->potential += weights[i] * team_traffic->potential;

		// Track enemy traffic
		enemy->actual += weights[i] * enemy_traffic->actual;
		enemy->potential += weights[i] * enemy_traffic->potential;
	}

	// Inform the caller of the closest neighbor
	return closest;
}
