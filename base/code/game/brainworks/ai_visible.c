// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_aim.c
 *
 * Functions that the bot uses to test visibility
 *****************************************************************************/

#include "ai_main.h"
#include "ai_vars.h"
#include "ai_visible.h"

#include "ai_client.h"
#include "ai_entity.h"


/*
========================
BotTargetInFieldOfVision
========================
*/
qboolean BotTargetInFieldOfVision(bot_state_t * bs, vec3_t target, float fov)
{
	vec3_t          dir;

	// Determine the direction vector from the bot's eye to the target
	VectorSubtract(target, bs->eye_now, dir);
	VectorNormalize(dir);

	// Check if the angle between the forward and direction vectors is in range
	return (DotProduct(bs->forward, dir) >= cos(DEG2RAD(0.5 * fov)));
}

/*
==============
BotGoalVisible
==============
*/
qboolean BotGoalVisible(bot_state_t * bs, bot_goal_t * goal)
{
	trace_t         trace;

	// Check if there is a direct line of sight to the location,
	// or if the trace was stopped by the goal's entity (if any)
	trap_Trace(&trace, bs->eye_now, NULL, NULL, goal->origin, bs->entitynum, MASK_SOLID);
	return ((trace.fraction >= 1.0) || (trace.entityNum == goal->entitynum && goal->entitynum >= 0));
}

/*
====================
BotEntityVisibleFast

Does a fast check to determine if an entity is probably visible.
Only checks if the entity's center is in line-of-sight of the
eye location, so it won't catch cases where the entity is more
than 50% occluded but still visible (eg. halfway behind a pillar).

This code is guaranteed to take exactly one trace.  It's best
used when precision is not needed and the function is likely to
get called a fair number of times.  If precision is needed or
you have spare cycles, use BotEntityVisible or BotEntityVisibleCenter
instead.
====================
*/
qboolean BotEntityVisibleFast(bot_state_t * bs, gentity_t * ent)
{
	vec3_t          center;
	trace_t         trace;

	// Calculate the center of the entity
	EntityCenter(ent, center);

	// Slightly offset the center towards the trace start
	//
	// NOTE: This is very slightly offset from the target, since the target
	// could be an object without a bounding box embedded on a wall (such
	// as a proximity mine)
	center[0] = .99 * center[0] + .01 * bs->eye_now[0];
	center[1] = .99 * center[1] + .01 * bs->eye_now[1];
	center[2] = .99 * center[2] + .01 * bs->eye_now[2];

	// Check if there is a direct line of sight to that center
	trap_Trace(&trace, bs->eye_now, NULL, NULL, center, bs->entitynum, MASK_SOLID);
	return (trace.fraction >= 1 || trace.entityNum == ent->s.number);
}

// Indicies refering to minimum or maximum bounding box values
#define BOUND_MIN	0
#define BOUND_MAX	1

// Bit values of a zone code
#define ZONE_CODE_CENTER 0		// Eye is aligned inside this axis of the bounding box
#define ZONE_CODE_BELOW 1		// Eye coordinate is less than the minimum of the bounding box
#define ZONE_CODE_ABOVE 2		// Eye coordinate is greater than the maximum of the bounding box

// Bitmasks to extract which part of a zone code mask refer to X, Y, or Z offsets
// from the target bounding box
#define ZONE_MASK_X	(0x03 << 0)
#define ZONE_MASK_Y (0x03 << 2)
#define ZONE_MASK_Z (0x03 << 4)

// These defines describe index references that define general corners of a
// bounding box, using the boundary min/max/mid indexing.
//
// NOTE: The lack of parenthesis lets us abuse #define notation in array setup.
// Don't try this at home kids; we're trained professionals.

#define POINT_Xlow__Ylow__Zlow	{BOUND_MIN, BOUND_MIN, BOUND_MIN}
#define POINT_Xhigh_Ylow__Zlow	{BOUND_MAX, BOUND_MIN, BOUND_MIN}
#define POINT_Xlow__Yhigh_Zlow	{BOUND_MIN, BOUND_MAX, BOUND_MIN}
#define POINT_Xhigh_Yhigh_Zlow	{BOUND_MAX, BOUND_MAX, BOUND_MIN}
#define POINT_Xlow__Ylow__Zhigh	{BOUND_MIN, BOUND_MIN, BOUND_MAX}
#define POINT_Xhigh_Ylow__Zhigh	{BOUND_MAX, BOUND_MIN, BOUND_MAX}
#define POINT_Xlow__Yhigh_Zhigh	{BOUND_MIN, BOUND_MAX, BOUND_MAX}
#define POINT_Xhigh_Yhigh_Zhigh	{BOUND_MAX, BOUND_MAX, BOUND_MAX}

// We can compose pairs of points into edges as well.  Again, this is very
// dangerous if you don't know what the code does.  The edge in question is
// the edge between the two specified spaces, and is defined by its two
// end point corners
  // X-Y plane intersection edges
#define EDGE_Xlow__Ylow		POINT_Xlow__Ylow__Zlow,  POINT_Xlow__Ylow__Zhigh
#define EDGE_Xlow__Yhigh	POINT_Xlow__Yhigh_Zlow,  POINT_Xlow__Yhigh_Zhigh
#define EDGE_Xhigh_Ylow		POINT_Xhigh_Ylow__Zlow,  POINT_Xhigh_Ylow__Zhigh
#define EDGE_Xhigh_Yhigh	POINT_Xhigh_Yhigh_Zlow,  POINT_Xhigh_Yhigh_Zhigh
  // X-Z plane intersection edges
#define EDGE_Xlow__Zlow		POINT_Xlow__Ylow__Zlow,  POINT_Xlow__Yhigh_Zlow
#define EDGE_Xlow__Zhigh	POINT_Xlow__Ylow__Zhigh, POINT_Xlow__Yhigh_Zhigh
#define EDGE_Xhigh_Zlow		POINT_Xhigh_Ylow__Zlow,  POINT_Xhigh_Yhigh_Zlow
#define EDGE_Xhigh_Zhigh	POINT_Xhigh_Ylow__Zhigh, POINT_Xhigh_Yhigh_Zhigh
  // Y-Z plane intersection edges
#define EDGE_Ylow__Zlow		POINT_Xlow__Ylow__Zlow,  POINT_Xhigh_Ylow__Zlow
#define EDGE_Ylow__Zhigh	POINT_Xlow__Ylow__Zhigh, POINT_Xhigh_Ylow__Zhigh
#define EDGE_Yhigh_Zlow		POINT_Xlow__Yhigh_Zlow,  POINT_Xhigh_Yhigh_Zlow
#define EDGE_Yhigh_Zhigh	POINT_Xlow__Yhigh_Zhigh, POINT_Xhigh_Yhigh_Zhigh

// And of course, planes can be composed of edges.
#define PLANE_Xlow			EDGE_Xlow__Ylow,  EDGE_Xlow__Yhigh
#define PLANE_Xhigh			EDGE_Xhigh_Ylow,  EDGE_Xhigh_Yhigh
#define PLANE_Ylow			EDGE_Xlow__Ylow,  EDGE_Xhigh_Ylow
#define PLANE_Yhigh			EDGE_Xlow__Yhigh, EDGE_Xhigh_Yhigh
#define PLANE_Zlow			EDGE_Xlow__Zlow,  EDGE_Xhigh_Zlow
#define PLANE_Zhigh			EDGE_Xlow__Zhigh, EDGE_Xhigh_Zhigh

// Some cases in this table should never get executed, but they still need to
// get initialized.  In the interest of safety, these four points define the
// tetrahedron inside the bounding box with greatest volume, so they should be
// most likely to actually detect the entity's visibility.
#define ERROR_SAFETY	POINT_Xlow__Ylow__Zlow, POINT_Xlow__Yhigh_Zhigh, \
						POINT_Xhigh_Yhigh_Zlow, POINT_Xhigh_Ylow__Zhigh

// The area surrounding the bounding box can be broken up into 27 different
// regions, as a 3x3x3 space (including the box's interior as one region).
// The first step is to determine which region the eye vector is in.
// A zone code for a given dimension is:
//   0x0 if the eye value is between the min and max of the bounding box
//   0x1 if the eye value is less than the box's minimum
//   0x2 if the eye value is greater than the box's maximum
//   0x3 if an internal error occurred
//
// Zone mask is a composite bitmask of these codes.  Bits 0 and 1 are for
// the X dimension, bits 2 and 3 for the Y dimension, and bits 4 and 5 for
// the Z dimension.  (See ZONE_MASK_X, _Y, and _Z.)
//
// This mask is used in a lookup table that returns the points to test for
// visibility.  The first and fourth entries should be diagonally opposite
// to each other.  The reason is that the lookup code has a speed optimization
// where if these two points on the entity are occluded by the same wall,
// it skips the other two checks.  Hence it's very important that an object
// blocking both of these points would necessarily cover a large portion of
// the bounding box.  Corner cases could be defined but are instead compressed
// to their nearest edge case.  Here is the lookup table:

#define MAX_ZONE_POINTS 41		// Last good value is 101000 == 0x28 == 40
#define MAX_SCAN_POINTS 4
const static int zone_points[MAX_ZONE_POINTS][MAX_SCAN_POINTS][3] = {
	{ERROR_SAFETY},				// 00 00 00: Bounding box interior
	{PLANE_Xlow},				// 00 00 01: Lower X plane
	{PLANE_Xhigh},				// 00 00 10: Higher X plane
	{ERROR_SAFETY},				// 00 00 11: UNDEFINED

	{PLANE_Ylow},				// 00 01 00: Lower Y plane
	{EDGE_Xlow__Yhigh, EDGE_Xhigh_Ylow},	// 00 01 01: Edge of Lower X, Lower Y
	{EDGE_Xhigh_Yhigh, EDGE_Xlow__Ylow},	// 00 01 10: Edge of Higher X, Lower Y
	{ERROR_SAFETY},				// 00 01 11: UNDEFINED

	{PLANE_Yhigh},				// 00 10 00: Higher Y plane
	{EDGE_Xhigh_Yhigh, EDGE_Xlow__Ylow},	// 00 10 01: Edge of Lower X, Higher Y
	{EDGE_Xlow__Yhigh, EDGE_Xhigh_Ylow},	// 00 10 10: Edge of Higher X, Higher Y
	{ERROR_SAFETY},				// 00 10 11: UNDEFINED

	{ERROR_SAFETY},				// 00 11 00: UNDEFINED
	{ERROR_SAFETY},				// 00 11 01: UNDEFINED
	{ERROR_SAFETY},				// 00 11 10: UNDEFINED
	{ERROR_SAFETY},				// 00 11 11: UNDEFINED

	{PLANE_Zlow},				// 01 00 00: Lower Z plane
	{EDGE_Xlow__Zhigh, EDGE_Xhigh_Zlow},	// 01 00 01: Edge of Lower X, Lower Z
	{EDGE_Xhigh_Zhigh, EDGE_Xlow__Zlow},	// 01 00 10: Edge of Higher X, Lower Z
	{ERROR_SAFETY},				// 01 00 11: UNDEFINED

	{EDGE_Ylow__Zhigh, EDGE_Yhigh_Zlow},	// 01 01 00: Edge of Lower Y, Lower Z
	{ERROR_SAFETY},				// 01 01 01: Corner of Lower X, Lower Y, Lower Z
	{ERROR_SAFETY},				// 01 01 10: Corner of Higher X, Lower Y, Lower Z
	{ERROR_SAFETY},				// 01 01 11: UNDEFINED

	{EDGE_Yhigh_Zhigh, EDGE_Ylow__Zlow},	// 01 10 00: Edge of Higher Y, Lower Z
	{ERROR_SAFETY},				// 01 10 01: Corner of Lower X, Higher Y, Lower Z
	{ERROR_SAFETY},				// 01 10 10: Corner of Higher X, Higher Y, Lower Z
	{ERROR_SAFETY},				// 01 10 11: UNDEFINED

	{ERROR_SAFETY},				// 01 11 00: UNDEFINED
	{ERROR_SAFETY},				// 01 11 01: UNDEFINED
	{ERROR_SAFETY},				// 01 11 10: UNDEFINED
	{ERROR_SAFETY},				// 01 11 11: UNDEFINED

	{PLANE_Zhigh},				// 10 00 00: Higher Z plane
	{EDGE_Xhigh_Zhigh, EDGE_Xlow__Zlow},	// 10 00 01: Edge of Lower X, Higher Z
	{EDGE_Xlow__Zhigh, EDGE_Xhigh_Zlow},	// 10 00 10: Edge of Higher X, Higher Z
	{ERROR_SAFETY},				// 10 00 11: UNDEFINED

	{EDGE_Yhigh_Zhigh, EDGE_Ylow__Zlow},	// 10 01 00: Edge of Lower Y, Higher Z
	{ERROR_SAFETY},				// 10 01 01: Corner of Lower X, Lower Y, Higher Z
	{ERROR_SAFETY},				// 10 01 10: Corner of Higher X, Lower Y, Higher Z
	{ERROR_SAFETY},				// 10 01 11: UNDEFINED

	{EDGE_Ylow__Zhigh, EDGE_Yhigh_Zlow},	// 10 10 00: Edge of Higher Y, Higher Z

};

#define SCAN_BOUND_FAIL 0
#define SCAN_BOUND_PASS 1
#define SCAN_BOUND_INSIDE 2


/*
====================
BotEntityVisualScans

Looks up MAX_SCAN_POINTS scan points in the zone_points
table to check for visibility of an entity given the
bot's eye location of "eye" and stores them in the "scans"
vector array.

Returns SCAN_BOUND_PASS if the scans were extracted,
SCAN_BOUND_INSIDE if the point is trivially visible (ie.
viewable inside the bounding box), and SCAN_BOUND_FAIL
if an error occurred.  The errors almost always occur when
the minimum point of a bounding box is greater than
the maximum point.
====================
*/
int BotEntityVisualScans(bot_state_t * bs, gentity_t * ent, vec3_t eye, vec3_t * scans)
{
	int             i, zone, zone_mask, nearest_axis;
	const int      *zone_scan;
	float           axial_dist, min_axial_dist;
	vec3_t          bound[2], center, dir;

	// Extract the entity's bounding box minimums and maximums
	EntityWorldBounds(ent, bound[BOUND_MIN], bound[BOUND_MAX]);

	// Which bounding box axis the eye is nearest to is not currently known
	nearest_axis = 0;
	min_axial_dist = -1;

	// Compute the zone mask for this point.  The zone mask encodes the corner,
	// edge, or face on the bounding box that the eye is closest to.
	zone_mask = 0x00;
	for(i = 0; i < 3; i++)
	{
		// Test for degenerate bounding boxes
		if(bound[BOUND_MIN][i] >= bound[BOUND_MAX][i])
			return SCAN_BOUND_FAIL;

		// Determine which zone this axis falls into and how far the eye
		// coordinate is from that axial boundary
		if(bs->eye_now[i] < bound[BOUND_MIN][i])
		{
			zone = ZONE_CODE_BELOW;
			axial_dist = bound[BOUND_MIN][i] - bs->eye_now[i];
		}
		else if(bs->eye_now[i] > bound[BOUND_MAX][i])
		{
			zone = ZONE_CODE_ABOVE;
			axial_dist = bs->eye_now[i] - bound[BOUND_MAX][i];
		}
		else
		{
			zone = ZONE_CODE_CENTER;
			axial_dist = 0.0;
		}

		// Add this zone code to the zone mask
		zone_mask |= (zone << (2 * i));

		// Squeeze or strech the axial distance to be relative to a unit cube
		axial_dist /= (bound[BOUND_MAX][i] - bound[BOUND_MIN][i]);

		// Record this axis as the current "closest to center" axis if that is the case
		if((min_axial_dist < 0) || (axial_dist < min_axial_dist))
		{
			nearest_axis = i;
			min_axial_dist = axial_dist;
		}
	}

	// If the closest visible point is a corner case, convert it to the nearest edge case
	if((zone_mask & ZONE_MASK_X) && (zone_mask & ZONE_MASK_Y) && (zone_mask & ZONE_MASK_Z))
	{
		zone_mask &= ~(0x03 << (2 * nearest_axis));
	}

	// There is nothing to scan if the viewpoint is inside the target's bounding box
	if(zone_mask == 0x00)
		return SCAN_BOUND_INSIDE;

	// This should not occur, but it's good to be safe
	if(zone_mask >= MAX_ZONE_POINTS)
		return SCAN_BOUND_FAIL;

	// The actual scan points used aren't on the edge of the box-- they are the centers
	// of the four of the eight octants.
	VectorAdd(bound[BOUND_MIN], bound[BOUND_MAX], center);
	VectorScale(center, 0.5, center);
	VectorSubtract(bound[BOUND_MIN], center, dir);
	VectorMA(center, 0.5, dir, bound[BOUND_MIN]);
	VectorSubtract(bound[BOUND_MAX], center, dir);
	VectorMA(center, 0.5, dir, bound[BOUND_MAX]);

	// Compute the scan point locations using the zone point indicies
	for(i = 0; i < MAX_SCAN_POINTS; i++)
	{
		// Lookup the actual location of the current scan point
		zone_scan = zone_points[zone_mask][i];
		scans[i][0] = bound[zone_scan[0]][0];
		scans[i][1] = bound[zone_scan[1]][1];
		scans[i][2] = bound[zone_scan[2]][2];
	}

	return SCAN_BOUND_PASS;
}

/*
================
BotEntityVisible

This function is a more precise version of BotEntityVisibleFast.
It checks four separate scan points around the entity, so it can
even detect entities which are partially hidden around corners
and so on.  As such, it's slower than BotEntityVisibleFast--
the worst case runtime is 4 traces.  However, for most entities
which are visible, it will succeed in 1 trace.

Use this function if the bot needs to detect partially covered
enemies, such as for enemy selection.
================
*/
qboolean BotEntityVisible(bot_state_t * bs, gentity_t * ent)
{
	int             i;
	vec3_t          scans[MAX_SCAN_POINTS];
	trace_t         trace;

	// Lookup the scan points for this entity from the given eye location
	switch (BotEntityVisualScans(bs, ent, bs->eye_now, scans))
	{
		default:
		case SCAN_BOUND_FAIL:
			return qfalse;
		case SCAN_BOUND_INSIDE:
			return qtrue;
		case SCAN_BOUND_PASS:
			break;
	}

	// Search the scan points for a hit
	for(i = 0; i < MAX_SCAN_POINTS; i++)
	{
		// Succeed if this trace hit
		trap_Trace(&trace, bs->eye_now, NULL, NULL, scans[i], bs->entitynum, MASK_SOLID);
		if((trace.fraction >= 1.0) || (trace.entityNum == ent->s.number))
			return qtrue;
	}

	// No hits were found
	return qfalse;
}

/*
======================
BotEntityVisibleCenter

This function is an expanded version of BotEntityVisible.  Its
purpose is to compute the center of the entity's visible area
(which isn't necessarily the center of the entity's bounding box).
It also returns a floating point estimate of how visible the
entity is (0.0 for completely hidden, 1.0 for completey visible).

This function also requires the input of exactly which eye coordinate
the bot should do its visibility test from.

This function's runtime is always four traces.  Only use this
function if you need detailed information about how visible an
entity is and what portion is visible.
======================
*/
float BotEntityVisibleCenter(bot_state_t * bs, gentity_t * ent, vec3_t eye, vec3_t center)
{
	int             i, hits;
	float           inv_hits, visibility;
	float           dist, head_percent, body_percent;
	vec3_t          scans[MAX_SCAN_POINTS], head;
	trace_t         trace;

	// Lookup the scan points for this entity from the given eye location
	switch (BotEntityVisualScans(bs, ent, eye, scans))
	{
		default:
		case SCAN_BOUND_FAIL:
			return 0.0;
		case SCAN_BOUND_INSIDE:
			EntityCenter(ent, center);
			return 1.0;
		case SCAN_BOUND_PASS:
			break;
	}

	// Check all of the scan points
	VectorClear(center);
	hits = 0;
	for(i = 0; i < MAX_SCAN_POINTS; i++)
	{
		// Check the next point if this scan point couldn't be seen
		trap_Trace(&trace, eye, NULL, NULL, scans[i], bs->entitynum, MASK_SOLID);
		if((trace.fraction < 1.0) && (trace.entityNum != ent->s.number))
			continue;

		// Record this scan point in the center aggregate
		VectorAdd(scans[i], center, center);
		hits++;
	}

	// Compute the entity's visibility
	visibility = hits / 4.0;

	// Fail if the entity was not visible
	if(visibility <= 0.0)
		return 0.0;

	// Compute the entity's center from the aggregate center total
	inv_hits = 1.0 / hits;
	VectorScale(center, inv_hits, center);

	// Fully visible players might have their center closer to their viewheight (eye
	// level with the bot) when they are relatively promiment in (close to) the bot's
	// field of view.
	//
	// NOTE: This guarantees that a bot at the same height as a target will aim
	// at the target with pitch angle zero.  Using the average body center will
	// cause the bot's aim to tip downwards when the bot gets close to the target,
	// since a target's center is below eye height.  This causes movement in a
	// second aim axis, which makes the bot more likely to miss.  It also doesn't
	// look like something a human would do.
	if((visibility >= 1.0) && (ent->client) && (bot_view_focus_head_dist.value < bot_view_focus_body_dist.value))
	{
		// Compute the interpolation percentages for head focus and body focus
		dist = Distance(bs->eye_now, ent->client->ps.origin);
		if(dist <= bot_view_focus_head_dist.value)
			head_percent = 1.0;
		else if(dist >= bot_view_focus_body_dist.value)
			head_percent = 0.0;
		else
			head_percent =
				(bot_view_focus_body_dist.value - dist) / (bot_view_focus_body_dist.value - bot_view_focus_head_dist.value);
		body_percent = 1.0 - head_percent;

		// Compute the target's head (eye) location
		VectorSet(head,
				  ent->client->ps.origin[0], ent->client->ps.origin[1], ent->client->ps.origin[2] + ent->client->ps.viewheight);

		// Interpolate the body point (current center) with the head location
		VectorScale(center, body_percent, center);
		VectorMA(center, head_percent, head, center);
	}

	// Return the entity's visibility
	return visibility;
}
