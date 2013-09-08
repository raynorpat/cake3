// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_item.c
 *
 * Functions used to process items and the regions they define
 *****************************************************************************/

#include "ai_main.h"
#include "ai_vars.h"
#include "ai_item.h"

#include "ai_client.h"
#include "ai_entity.h"
#include "ai_level.h"
#include "ai_region.h"
#include "ai_resource.h"


// Number of server frames before items can be setup (or 0 if already setup)
int             item_setup_counter = 5;

// The maximum number of respawning items in one level the bots can comprehend
#define MAX_ITEMS 256

// Linked list entries for each non-dropped item on the level.  Array is sorted by entity pointer.
item_link_t     level_items[MAX_ITEMS];
int             num_items = 0;


// A pair whose key is a pointer to an item name and whose
// value is a pointer to an item_link_t with matching name.
// If any other items with the same name exist in the level,
// a pointer to one of them will be stored in the next_name
// value of the starting link, and so on, forming a linked list
// of items with the same name.
typedef struct name_entlink_pair_s
{
	char           *name;
	item_link_t    *start;
} name_entlink_pair_t;

// Sorted list of all items grouped by name
name_entlink_pair_t level_item_names[MAX_ITEM_TYPES];
int             num_item_names = 0;


// Sorted list of all item clusters in the level
#define MAX_CLUSTERS_STATIC MAX_REGIONS
item_cluster_t  clusters_static[MAX_CLUSTERS_STATIC];
int             num_clusters_static = 0;

// Clusters resting on movers are tracked separately because they move during
// the game.  Their location must be rechecked before item pickup.  Also, they
// cannot be used as centers of regions because the region tree must be static.
// Please don't make levels with lots of items on movers-- it makes my life suck.
#define MAX_CLUSTERS_MOBILE	32
item_cluster_t  clusters_mobile[MAX_CLUSTERS_MOBILE];
int             num_clusters_mobile = 0;

// Items that are dropped during the game are both detected and tracked at
// runtime, so they need their own storage space.  (In contrast, mobile
// clusters are detected at startup but tracked in runtime.)  Each dropped
// item will have its own cluster.
//
// NOTE: It's possible to arrange the dropped items into their own clusters
// of multiple items, but it's not clear this will make item pickup that
// much better.  Also, cluster formation requires O(N^2) time, where N is
// the number of dropped items on the level.  So the only time the clusters
// would really occur is when the algorithm would take a lot of time.
#define MAX_DROPPED_ITEMS	48
item_link_t     level_dropped_items[MAX_DROPPED_ITEMS];
item_cluster_t  clusters_dropped[MAX_DROPPED_ITEMS];

// The access of dropped item clusters is handled by a memory manager,
// because the code requires a lot of constant time access to the next
// unused cluster entry.
#define DROPPED_ITEM_PAGES ((MAX_DROPPED_ITEMS + MM_PAGE_SIZE-1) / MM_PAGE_SIZE)
mem_page_t      dropped_item_pages[DROPPED_ITEM_PAGES];
mem_manager_t   dropped_item_mm;

// A hash map of the dropped items currently on the level.  The map keys
// are item entities and the values are clusters from the clusters_dropped
// array.
//
// NOTE: These clusters are stored in a map so that the code can get near
// constant time access to them.  This is important because dropped items
// could get picked up during any server frame, and the AI code needs to
// know immediately for each item whether it is still in the game or not.
#define DROPPED_ITEM_MAP_CAPACITY (MAX_DROPPED_ITEMS * 4 / 3)
map_entry_t     dropped_item_entries[DROPPED_ITEM_MAP_CAPACITY];
map_t           dropped_item_map;


// List of all reasonably important item clusters in a level
item_cluster_t *important_items[MAX_REGIONS];
int             num_important_items = 0;
float           important_item_total_value = 0.0;

// An estimate of the average value of a cluster pickup
float           pickup_value_average = 0.0;


/*
===============
CanProcessItems

Check if its safe to access items
===============
*/
qboolean CanProcessItems(void)
{
	return (item_setup_counter <= 0);
}

/*
==========
HashEntity

A hash function for entity pointers
==========
*/
int HashEntity(const void *ent)
{
	// Multiplying by a semi-large prime can help disperse storage
	// in the hash table if entrys are often indexed in a sequential
	// fashion.
	return ((const gentity_t *)ent - g_entities) * 1009;
}

/*
======================
CompareEntityListEntry

Compare an entity pointer with the entity
stored in an entity linked list entry.
======================
*/
int QDECL CompareEntityListEntry(const void *a, const void *b)
{
	const gentity_t *ent = (const gentity_t *)a;
	const item_link_t *entry = (const item_link_t *)b;

	return (ent - entry->ent);
}

/*
=====================
CompareStringItemName

Compare a string pointer with the name
field value of a name-entlink pair.
=====================
*/
int QDECL CompareStringItemName(const void *a, const void *b)
{
	const char     *string = (const char *)a;
	const name_entlink_pair_t *pair = (const name_entlink_pair_t *)b;

	return Q_stricmp(string, pair->name);
}

/*
=======================
CompareItemClusterValue

Compare two array entries which are pointers
to item clusters according to the item
cluster value, such that higher value clusters
occur earlier in the list.  NULL clusters are
compared such that they always occur last in the
list.

NOTE: Because the entries are pointers, and
qsort() expects a pointer to an array element,
these inputs actually have type (item_cluster_t **).
==============================
*/
int QDECL CompareItemClusterValue(const void *a, const void *b)
{
	float           diff;

	const item_cluster_t *ca = *(item_cluster_t **) a;
	const item_cluster_t *cb = *(item_cluster_t **) b;

	// Check for null cluster pointers
	if(!a)
		return 1;
	else if(!b)
		return -1;

	// A positive value difference means "a" is more valuable than "b",
	// so it should occur first in the list, so a negative value is returned
	diff = ca->value - cb->value;
	if(diff > 0)
		return -1;
	else if(diff < 0)
		return 1;
	else
		return 0;
}

/*
===========
ClusterName

Looks up a simple name for a (possibly void* casted) cluster
===========
*/
const char     *ClusterName(const void *cluster)
{
	if(cluster == NULL)
		return EntityNameFast(NULL);
	return EntityNameFast(((const item_cluster_t *)cluster)->center->ent);
}

/*
==================
DroppedItemCluster

Returns the item cluster that handles the inputted
dropped item.  Returns NULL if this item is not a
dropped item, or if no cluster is handling this item.
==================
*/
item_cluster_t *DroppedItemCluster(gentity_t * ent)
{
	// Fail if the entity isn't a dropped item
	//
	// FIXME: It might be a good idea to store all clusters in the same
	// map rather than keeping the non-dropped items and clusters in
	// their own separate array.
	if(!ent || !(ent->flags & FL_DROPPED_ITEM))
		return NULL;

	// Look up which cluster in the dropped item map handles this item
	return (item_cluster_t *) map_get(&dropped_item_map, ent);
}

/*
========
ItemArea

Returns the cached area of an entity (possibly area 0,
meaning no area).
========
*/
int ItemArea(gentity_t * ent)
{
	item_link_t    *item;
	item_cluster_t *cluster;

	// Confirm that the entity is an item
	if(!ent || ent->s.eType != ET_ITEM)
		return 0;

	// Dropped items are stored in a special table ...
	if(ent->flags & FL_DROPPED_ITEM)
	{
		// Ignore items that are already tracked
		cluster = (item_cluster_t *) map_get(&dropped_item_map, ent);
		item = (cluster ? cluster->center : NULL);
	}

	// ... Normal items are stored in a sorted list
	else
	{
		item = bsearch(ent, level_items, num_items, sizeof(item_link_t), CompareEntityListEntry);
	}

	// Return the cached area if the item was found
	if(!item)
		return 0;
	return item->area;
}

/*
================
NearestNamedItem

Find the item matching "name" (a gitem_t pickup_name)
which is nearest to "location".  Returns a pointer to
the closest entity, or NULL if no such items could be found.
================
*/
gentity_t      *NearestNamedItem(char *name, vec3_t location)
{
	float           dist, closest_dist;
	gentity_t      *closest_ent;
	item_link_t    *item;
	name_entlink_pair_t *name_pair;

	// Check if any copies of this item exist
	name_pair = (name_entlink_pair_t *)
		bsearch(name, level_item_names, num_item_names, sizeof(name_entlink_pair_t), CompareStringItemName);
	if(!name_pair)
		return NULL;

	// Find the closest item in the list
	closest_ent = NULL;
	closest_dist = -1;
	for(item = name_pair->start; item; item = item->next_name)
	{
		// Ignore this item if a closer one has been found
		dist = DistanceSquared(item->ent->r.currentOrigin, location);
		if(dist > closest_dist && closest_dist >= 0.0)
			continue;

		// Record this item as the closest one
		closest_dist = dist;
		closest_ent = item->ent;
	}

	// Return whichever item was the closest
	return closest_ent;
}

#ifdef DEBUG_AI
/*
============
PrintCluster
============
*/
void PrintCluster(item_cluster_t * cluster, int indent)
{
	int             i;
	char            tab[32];
	item_link_t    *item;

	// Compute initial spacing tab
	for(i = 0; i < 2 * indent; i++)
		tab[i] = ' ';
	tab[i] = '\0';

	// Check for NULL clusters
	if(!cluster)
	{
		G_Printf("%sCluster NULL\n", tab);
		return;
	}

	// Print basic information about the cluster's center
	G_Printf("%sCluster: Location (%.f, %.f, %.f), Area %i, Value: %f\n", tab,
			 cluster->center->ent->r.currentOrigin[0],
			 cluster->center->ent->r.currentOrigin[1],
			 cluster->center->ent->r.currentOrigin[2], cluster->center->area, cluster->value);

	// Print information about each item in the cluster
	for(item = cluster->start; item; item = item->next_near)
	{
		G_Printf("%s  %s (area %i)%s\n", tab,
				 item->ent->item->pickup_name, item->area, (item == cluster->center ? " (center)" : ""));
	}
}

/*
================
PrintClusterList
================
*/
void PrintClusterList(item_cluster_t * clusters, int num_clusters, char *list_name)
{
	int             i;

	// Print a different message when no clusters were found
	if(num_clusters <= 0)
	{
		G_Printf("No %s clusters found\n", list_name);
		return;
	}

	// Print out the contents of each cluster
	G_Printf("Contents of %i %s cluster%s:\n", num_clusters, list_name, (num_clusters > 1 ? "s" : ""));
	for(i = 0; i < num_clusters; i++)
		PrintCluster(&clusters[i], 1);
}
#endif

/*
===============
ClustersAddItem

Add the inputted item to a list of clusters, possibly merging
or creating clusters if necessary.  Returns false if the item
could not be added because the clusters ran out of space.

The input item should have a NULL "next" pointer.
===============
*/
qboolean ClustersAddItem(item_cluster_t * clusters, int *num_clusters, int max_clusters, item_link_t * new_item)
{
	int             i;
	item_cluster_t *cluster;
	item_link_t    *cluster_item, **cluster_insert;
	gentity_t      *new_ent, *new_mover;

	// Determine which mover, if any, this item is on
	new_ent = new_item->ent;
	new_mover = EntityOnMoverNow(new_ent);

	// The new item has not yet been added to any cluster
	cluster_insert = NULL;

	// Search all clusters for an item within range of the input item
	i = 0;
	while(i < *num_clusters)
	{
		// Search all items within the current cluster for a match
		cluster = &clusters[i];
		for(cluster_item = cluster->start; cluster_item; cluster_item = cluster_item->next_near)
		{
			// Ignore items outside of the maximum cluster range
			if(DistanceSquared(cluster_item->ent->r.currentOrigin, new_ent->r.currentOrigin) > Square(CLUSTER_RANGE))
				continue;

			// All entities in a cluster must share the same ground entity (if any)
			if(EntityOnMoverNow(cluster_item->ent) != new_mover)
				continue;

			// Either add the item to this cluster or merge the old and new clusters
			break;
		}

		// If no nearby item was found, continue processing the next cluster
		if(!cluster_item)
		{
			i++;
			continue;
		}

		// Add this item to the current cluster if there is no previous cluster to add it to
		if(!cluster_insert)
		{
			// Add to the start of the list
			new_item->next_near = cluster->start;
			cluster->start = new_item;

			// Process the next cluster
			i++;
		}

		// Otherwise merge the clusters
		else
		{
			// Copy this cluster to the end of the last cluster
			*cluster_insert = cluster->start;

			// Copy the last cluster to this location and reduce the number of clusters
			cluster->start = clusters[--(*num_clusters)].start;
		}

		// Concatenate any future clusters at the end of this cluster
		while(cluster_item->next_near)
			cluster_item = cluster_item->next_near;
		cluster_insert = &cluster_item->next_near;
	}

	// If the item was added to at least one cluster, succeed
	if(cluster_insert)
		return qtrue;

	// Fail if a new cluster cannot be allocated for this item
	if(*num_clusters >= max_clusters)
		return qfalse;

	// Add the item to a new cluster
	clusters[*num_clusters].start = new_item;
	clusters[(*num_clusters)++].center = NULL;
	new_item->next_near = NULL;	// Just to be safe
	return qtrue;
}

/*
============
ClusterSetup

Setup several things for new clusters:
 - Sorts items according to ideal pickup order (eg. weapons before ammo)
 - Computes base value of all items in the cluster
 - Computes longest respawn delay between pickups for items in the cluster
 - Determines which item is closest to the cluster's center

NOTE: Theoretically this function should make 5h and
100h items get picked up after 25h and 50h items, but
it's just not worth the trouble, because those items
almost never appear in the same clusters.  Also, the
amount of health lost from picking them up in the
wrong order is usually pretty small.
============
*/
void ClusterSetup(item_cluster_t * cluster)
{
	float           respawn_delay, value, max_value, min_value;
	float           total_weight, scale, dist, closest_dist;
	vec3_t          centroid;
	item_link_t    *item, **item_ptr, *closest_item;

	// Ignore clusters without any items (Internal Error?)
	if(!cluster->start)
		return;

	// Search through all items in the cluster, except the first (since the first
	// item is always 'sorted' with regards to only itself).
	item_ptr = &cluster->start->next_near;
	while(*item_ptr)
	{
		// Ignore non-weapons
		item = *item_ptr;
		if(item->ent->item->giType != IT_WEAPON)
		{
			item_ptr = &item->next_near;
			continue;
		}

		// Move weapons to the start of the cluster
		*item_ptr = item->next_near;
		item->next_near = cluster->start;
		cluster->start = item;
	}

	// Search all items in the cluster for longest respawn time,
	// total value, and highest value
	//
	// NOTE: Technically this scoring method isn't be accurate.
	// Sometimes multiple items have positive interactions (Weapon +
	// Matching Ammo), and other times negative interactions (three
	// red armors in the same cluster aren't any better than two).  The
	// correct valuation isn't worth the time, however, because this
	// estimate is really close most of the time (and usually exact).
	cluster->respawn_delay = 0.0;
	cluster->value = 0.0;
	max_value = -1;
	for(item = cluster->start; item; item = item->next_near)
	{
		// Ignore dropped items
		if(item->ent->flags & FL_DROPPED_ITEM)
			continue;

		// Ignore items that aren't present on the level
		//
		// NOTE: This should never occur
		value = BaseItemValue(item->ent->item);
		if(value < 0.0)
			continue;

		// Note if this is the most valuable item so far
		if(max_value < value)
			max_value = value;

		// Check for increase in maximum respawn delay
		respawn_delay = ItemRespawn(item->ent);
		if(cluster->respawn_delay < respawn_delay)
			cluster->respawn_delay = respawn_delay;

		// Track the cluster's total value when all items are present (a
		// rough estimate of how valuable it is to time the cluster's respawn)
		cluster->value += value;
	}

	// Compute each item's contribution to the cluster's value as a whole
	for(item = cluster->start; item; item = item->next_near)
	{
		// Assume this item does not contribute
		item->contribution = 0.0;

		// If the cluster has no value, no item in it contributes at all
		if(cluster->value <= 0.0)
			break;

		// Ignore dropped items; their value is not consistant
		if(item->ent->flags & FL_DROPPED_ITEM)
			break;

		// Compute the contribution from this item to the cluster
		item->contribution = BaseItemValue(item->ent->item) / cluster->value;
	}

	// The maximum value is used to weight item values when computing the
	// cluster's center.  But some items have zero value (like flags).  So
	// just to be safe, make a fake value if all items have zero base value,.
	if(max_value <= 0.0)
		max_value = 1.0;

	// Each item contributes at least a small part of the maximum value
	min_value = max_value * .1;

	// Aggregate the centers of each item in the cluster, weighted by the base item values
	VectorSet(centroid, 0, 0, 0);
	total_weight = 0.0;
	for(item = cluster->start; item; item = item->next_near)
	{
		// Determine the item's relative value for this cluster
		value = BaseItemValue(item->ent->item);
		if(value < 0.0)
			continue;
		if(value < min_value)
			value = min_value;

		// Weight the item's center according to its item value
		VectorMA(centroid, value, item->ent->r.currentOrigin, centroid);
		total_weight += value;
	}

	// This should never occur, but it's best to be safe
	if(!total_weight)
	{
		cluster->center = NULL;
		return;
	}

	// Translate the aggregate to the centroid
	scale = 1.0 / total_weight;
	VectorScale(centroid, scale, centroid);

	// Determine which item is closest to the centroid
	closest_item = NULL;
	closest_dist = -1;
	for(item = cluster->start; item; item = item->next_near)
	{
		dist = DistanceSquared(centroid, item->ent->r.currentOrigin);
		if(closest_dist < 0 || dist < closest_dist)
		{
			closest_item = item;
			closest_dist = dist;
		}
	}

	// Store that item as the center of the cluster
	cluster->center = closest_item;
}

/*
=======================
ClusterPointPickupRates

Compute the total points per second and pickups
per second generated by the items in this cluster.
These rates are accumulated into *point_rate and
*pickup_rate respectively.

NOTE: The output values are purposely NOT initialized
to zero before adding the value.  This is so the same
pointers can be used to track total data from multiple
clusters.  It is the caller's responsibility to make
sure the values of *point_rate and *pickup_rate are
properly initialized.
=======================
*/
void ClusterPointPickupRates(item_cluster_t * cluster, float *point_rate, float *pickup_rate)
{
	float           pickup;
	item_link_t    *item;

	// Account for the points and pickups added from each item in the cluster
	for(item = cluster->start; item; item = item->next_near)
	{
		// Determine how often players want to pick up this item
		pickup = ItemPickup(item->ent->item);

		// This item adds this many points per second
		*point_rate += BaseItemValue(item->ent->item) / pickup;

		// It also contributes this many cluster respawns per second
		*pickup_rate += item->contribution / pickup;
	}
}

/*
==============
LevelItemReset

Reset all preprocessed data about items on the level.
==============
*/
void LevelItemReset(void)
{
	// Reset all regions that depend on these items
	LevelRegionReset();

	// Reset item data
	item_setup_counter = 5;		// See G_SpawnItem() in g_items.c for an explanation
	num_items = 0;
	num_item_names = 0;
	num_clusters_static = 0;
	num_clusters_mobile = 0;

	ItemValuesReset();
}

/*
==============
LevelItemSetup

This setup function is definitely O(N^2).  It could take a while
(as in, almost a second) if used on massively huge levels like
mpterra1-3 on slow processors.  At least it's just a one time hit.
==============
*/
void LevelItemSetup(void)
{
	int             i, j, insert, avoid_area;
	float           points, pickups;
	vec3_t          floor;
	trace_t         trace;
	char           *name;
	gentity_t      *ent;
	item_link_t    *item;
	item_cluster_t *cluster;
	name_entlink_pair_t *name_pair;

	// No updating is necessary if the items are already setup
	if(CanProcessItems())
		return;

	// Several frames must expire before all the regions are setup
	// NOTE: See G_SpawnItem() in g_items.c for more information
	if(--item_setup_counter)
		return;

	// Make list of all standard spawned items in the level
	for(i = 0, ent = &g_entities[0]; i < level.numEntities; i++, ent++)
	{
		// Only track non-dropped items that are in-use
		if(!ent->inuse || ent->s.eType != ET_ITEM || (ent->flags & FL_DROPPED_ITEM))
			continue;

		// Make sure there is enough space to track this item
		if(num_items >= MAX_ITEMS)
		{
			BotAI_Print(PRT_WARNING, "Level exceeds maximum number of items (%i).  Bots "
						"might not recognize all items.\n", MAX_ITEMS);
			break;
		}

		// Track this item
		level_items[num_items].ent = ent;
		level_items[num_items].next_near = NULL;
		level_items[num_items].next_name = NULL;
		level_items[num_items++].area = LevelAreaPoint(ent->r.currentOrigin);
	}

	// Never pick up items on the BFG platform on this level-- it's WAY too
	// dangerous
	//
	// NOTE: Yes, this is a hack.  The AI code isn't set up to handle such
	// high risk, high reward situations.  But arguably those situations
	// don't represent very good level design either.
	//
	// NOTE: If you haven't figured it out, area 932 is the area under the crusher
	if(!Q_stricmp(LevelMapTitle(), "q3tourney6"))
		avoid_area = 932;
	else
		avoid_area = 0;

	// Create lists of items with the same name and clusters of nearby items
	for(i = 0; i < num_items; i++)
	{
		item = &level_items[i];

		// Only track items in routable areas
		//
		// NOTE: This call doesn't have an error message because items above
		// the void do not have areas-- it's legal to have such items, but the
		// navigation code cannot determine how to route the bots there.
		if(!item->area)
			continue;

		// Ignore items in the avoided area, if one exists
		if(item->area == avoid_area)
			continue;

		// Items suspended above the ground are not stored in clusters or name
		// lists-- bots only know they exist in concept for determining base values.
		//
		// NOTE: There is nothing in the item pickup code that prevents the pickup
		// of suspended items.  Rather, the navigation engine is unable to route
		// bots to some of these areas.  In some circumstances, the bot will just
		// dodge in place forever.  In other instances, the bot can get routed to
		// an area and be unable to move from the area.  In fact, the original Q3
		// code cheated and returned the area of a jump pad to reach these items
		// instead of the item's actual area.  And even then, the jump pad areas
		// were often wrong.  It's a shame that these items must get excluded, but
		// the inadequacies in the movement engine leave me no choice.
		ent = item->ent;
		VectorSet(floor, ent->r.currentOrigin[0], ent->r.currentOrigin[1], ent->r.currentOrigin[2] - 64);
		trap_Trace(&trace, ent->r.currentOrigin, NULL, NULL, floor, ENTITYNUM_NONE, MASK_SOLID);
		if(trace.fraction >= 1.0)
			continue;

		// Store this item in a list of items with the same name
		name = ent->item->pickup_name;
		name_pair = (name_entlink_pair_t *)
			bsearch_ins(name, level_item_names, &num_item_names, MAX_ITEM_TYPES,
						sizeof(name_entlink_pair_t), CompareStringItemName, &insert);
		if(name_pair)
		{
			if(insert)
			{
				name_pair->name = name;
				name_pair->start = item;
			}
			else
			{
				item->next_name = name_pair->start;
				name_pair->start = item;
			}
		}

		// Dynamic location items (ie. on a mover) have different clusters than static ones
		if(!EntityOnMoverNow(ent))
		{
			if(!ClustersAddItem(clusters_static, &num_clusters_static, MAX_CLUSTERS_STATIC, item))
				BotAI_Print(PRT_WARNING, "Item %s exceeds maximum number of "
							"static item clusters (%i)\n", ent->item->pickup_name, MAX_CLUSTERS_STATIC);
		}
		else
		{
			if(!ClustersAddItem(clusters_mobile, &num_clusters_mobile, MAX_CLUSTERS_MOBILE, item))
				BotAI_Print(PRT_WARNING, "Item %s exceeds maximum number of "
							"mobile item clusters (%i)\n", ent->item->pickup_name, MAX_CLUSTERS_MOBILE);
		}
	}

	// Compute the base values of each item in the level
	ItemValuesCompute(level_items, num_items);

	// Setup some internal cluster data for each static and dynamic cluster
	for(i = 0; i < num_clusters_static; i++)
		ClusterSetup(&clusters_static[i]);
	for(i = 0; i < num_clusters_mobile; i++)
		ClusterSetup(&clusters_mobile[i]);

	// Estimate the total points per second and cluster pickups per second
	// this level spawns
	points = 0.0;
	pickups = 0.0;
	for(i = 0; i < num_clusters_static; i++)
		ClusterPointPickupRates(&clusters_static[i], &points, &pickups);
	for(i = 0; i < num_clusters_mobile; i++)
		ClusterPointPickupRates(&clusters_mobile[i], &points, &pickups);

	// Estimate the average value of a cluster pickup on this level
	if(pickups > 0.0)
		pickup_value_average = points / pickups;
	else
		pickup_value_average = 0.0;

#ifdef DEBUG_AI
	// Tell the user how valuable a cluster is on average if requested
	if(bot_debug_item.integer)
		G_Printf("Average cluster pickup value: %.3f\n", pickup_value_average);
#endif

	// Initialize the important items using the static item clusters
	num_important_items = 0;
	important_item_total_value = 0.0;
	for(i = 0; i < num_clusters_static && i < MAX_REGIONS; i++)
	{
		// Each cluster's value is how much more valuable it is than an average pickup;
		// Ignore clusters with below average value
		cluster = &clusters_static[i];
		if(cluster->value < pickup_value_average)
		{
			cluster->value = 0.0;
			continue;
		}
		cluster->value -= pickup_value_average;

		// Consider this as another potential important item
		important_items[num_important_items++] = cluster;
		important_item_total_value += cluster->value;
	}

#ifdef DEBUG_AI
	// Output which clusters might be timed if requested
	if(bot_debug_item.integer)
	{
		// Announce the list of potentially timable clusters
		G_Printf("Clusters considerable for respawn timing:\n");

		// Print each cluster that could be timed
		for(i = 0; i < num_important_items; i++)
		{
			cluster = important_items[i];
			G_Printf("  %s (%.f, %.f, %.f) with additional value %.3f\n",
					 ClusterName(cluster), VectorList(cluster->center->ent->r.currentOrigin), cluster->value);
		}

		// Print out the contents of each cluster
		PrintClusterList(clusters_static, num_clusters_static, "static");
		PrintClusterList(clusters_mobile, num_clusters_mobile, "dynamic");
	}
#endif

	// Initialize the clusters that handle dropped items
	for(i = 0; i < MAX_DROPPED_ITEMS; i++)
	{
		// Setup each cluster to contain exactly one item
		item = &level_dropped_items[i];
		cluster = &clusters_dropped[i];

		// Setup the static item data
		//
		// NOTE: The actual item contained will be NULL while
		// the cluster isn't in use.
		item->ent = NULL;
		item->area = 0;
		item->next_near = NULL;
		item->next_name = NULL;

		// Permanently assign the item data to this cluster
		cluster->start = item;
		cluster->center = item;
		cluster->value = 0.0;
		cluster->respawn_delay = 0.0;
	}

	// Initialize the memory manager for acquiring dropped item clusters
	memset(dropped_item_pages, 0, sizeof(dropped_item_pages));
	mm_setup(&dropped_item_mm,
			 clusters_dropped, sizeof(item_cluster_t), MAX_DROPPED_ITEMS, dropped_item_pages, DROPPED_ITEM_PAGES);

	// Initialize the map of dropped items currently on the level
	map_initialize(&dropped_item_map, dropped_item_entries, DROPPED_ITEM_MAP_CAPACITY, CompareVoid, HashEntity);

	// A setup completion message can make users feel a lot better
	G_Printf("Detected %i items grouped into %i static clusters "
			 "and %i mobile clusters.\n", num_items, num_clusters_static, num_clusters_mobile);

	// Setup the regions on the level using the static clusters
	LevelRegionSetup(clusters_static, num_clusters_static);
}

/*
===============
LevelItemUpdate

Update dynamic item clusters.
===============
*/
void LevelItemUpdate(void)
{
	int             i, area, avoid_area;
	gentity_t      *ent;
	item_link_t    *item;
	item_cluster_t *cluster;
	map_entry_t    *key_value;

	// Wait until the items in the level are set up
	if(!CanProcessItems())
		return;

	// Reset the dynamic cluster tracking in all regions
	LevelRegionResetDynamic();

	// Update the area of each item in each dynamic cluster
	for(i = 0; i < num_clusters_mobile; i++)
	{
		for(item = clusters_mobile[i].start; item; item = item->next_near)
		{
			// Update the cached area if a valid area could be determined
			area = LevelAreaPoint(item->ent->r.currentOrigin);
			if(area)
				item->area = area;
		}
	}

	// Never pick up items on the BFG platform on this level-- it's WAY too
	// dangerous
	//
	// NOTE: Yes, this is a hack.  The AI code isn't set up to handle such
	// high risk, high reward situations.  Arguably those situations don't
	// make for very good level design either though.
	//
	// NOTE: If you haven't figured it out, area 932 is the area under the crusher
	if(!Q_stricmp(LevelMapTitle(), "q3tourney6"))
		avoid_area = 932;
	else
		avoid_area = 0;

	// Add each mobile cluster to the appropriate region
	for(i = 0; i < num_clusters_mobile; i++)
		ClusterAddToRegion(&clusters_mobile[i]);

	// Remove invalidated entries from the dropped item map
	key_value = map_iter_first(&dropped_item_map);
	while(key_value)
	{
		// Dereference the key (entity) and value (cluster)
		ent = (gentity_t *) key_value->key;
		cluster = (item_cluster_t *) key_value->value;

		// Look up this item's area if it's still valid
		area = 0;
		if(ent->inuse && ent->s.eType == ET_ITEM && (ent->flags & FL_DROPPED_ITEM))
		{
			// Look up the area; make the avoided area unroutable
			area = LevelAreaPoint(ent->r.currentOrigin);
			if(area == avoid_area)
				area = 0;
		}

		// Remove items that the bots cannot route to (either because they are
		// gone or in an unroutable area)
		if(!area)
		{
			// Note that the cluster no longer tracks an entity
			cluster->center->ent = NULL;
			cluster->center->area = 0;

			// Free the allocated cluster data
			mm_delete(&dropped_item_mm, cluster);

			// Remove the entry from the map
			map_set(&dropped_item_map, ent, NULL);

			// Check the next map entry from this deleted spot forward
			key_value = map_iter_refresh(&dropped_item_map, key_value);
			continue;
		}

		// Update the item's area
		cluster->center->area = area;

		// Add this cluster to the appropriate nearby region
		ClusterAddToRegion(cluster);

		// Process the next entry in the map
		key_value = map_iter_next(&dropped_item_map, key_value);
	}

	// Add any untracked dropped items in the level
	for(i = 0, ent = &g_entities[0]; i < level.numEntities; i++, ent++)
	{
		// Search for dropped items that are in use
		if(!ent->inuse || ent->s.eType != ET_ITEM || !(ent->flags & FL_DROPPED_ITEM))
			continue;

		// Ignore items that are already tracked
		if(map_get(&dropped_item_map, ent))
			continue;

		// Only track dropped items in a navigatable area, excluding
		// the avoided area, if any
		area = LevelAreaPoint(ent->r.currentOrigin);
		if(!area || area == avoid_area)
			continue;

		// Allocate a new cluster to handle this dropped item
		//
		// NOTE: Don't process any more clusters if no more storage remains
		cluster = (item_cluster_t *) mm_new(&dropped_item_mm);
		if(!cluster)
			break;

		// Attempt to save the key and data to the map
		if(!map_set(&dropped_item_map, ent, cluster))
		{
			// Deallocate the cluster and fail out
			mm_delete(&dropped_item_mm, cluster);
			break;
		}

		// Make the cluster track this entity
		cluster->center->ent = ent;
		cluster->center->area = area;

		// Add this cluster to the appropriate nearby region
		ClusterAddToRegion(cluster);
	}
}
