// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_attack.c
 *
 * Functions that the bot uses to determine how to attack a target
 *****************************************************************************/

#include "ai_main.h"
#include "ai_vars.h"
#include "ai_attack.h"

#include "ai_client.h"
#include "ai_command.h"
#include "ai_entity.h"
#include "ai_goal.h"
#include "ai_level.h"
#include "ai_motion.h"
#include "ai_predict.h"
#include "ai_view.h"
#include "ai_visible.h"
#include "ai_weapon.h"


#ifdef DEBUG_AI
// A colored description of a hit status
char           *PrintStringHitStatus(qboolean hit)
{
	return (hit ? "^1Hit^7" : "^2Miss^7");
}
#endif

/*
=================
BotAttackLeadTime

Estimates how much lead time will be necessary for
the bot to hit the a target with the inputted weapon
that is offset "origin" from the bot's current position
with velocity "velocity".  "lag" is the amount of time
the target will move before the bot's shot gets processed
by the server.

NOTE: This time is just an estimate.  Guaranteeing a
perfect shot requires an arbitrarily large number
of enemy prediction extrapolations, and even one
such function call is processor intense.
=================
*/
float BotAttackLeadTime(bot_state_t * bs, int weapon, float lag, vec3_t origin, vec3_t velocity)
{
	float           speed, speed_square;
	float           a, b, c, disc, time;
	vec3_t          offset;

	// Very stupid bots never lead their shots
	if(bs->weapon_char_skill[weapon] < 0.3)
		return lag;

	// No lead is necessary for instant hit weapons
	speed = weapon_stats[weapon].speed;
	if(speed <= 0.0)
		return lag;

	// Compute the initial target displacement
	VectorSubtract(origin, bs->future.origin, offset);

	// Assume the bot is at the origin and the target's relative (starting)
	// position is p ("offset").  Also assume the target is moving linearly
	// with constant velocity v ("velocity"), and the missile the bot shoots
	// moves at speed s ("speed").  The missile doesn't start shooting until
	// time l ("lag").
	//
	// The target's position at time t will be p + t*v, and the distance the
	// missile will have travelled at time t is s*(t-l).  The objective of this
	// function is to find the time t such that |p + t*v| = s*(t-l).  It's easier
	// to avoid the square roots though, so this equation is simpler to solve:
	//
	//   |p + t*v|^2 - (s*(t-l))^2 = 0
	//
	// This is just:
	//
	//   [ Dot(p,p) + Dot(p,v)*2*t + Dot(v,v)*t^2 ] - [ s^2 * (t-l)^2 ] = 0
	//   [ Dot(p,p) + Dot(p,v)*2*t + Dot(v,v)*t^2 ] - [ s^2*t^2 - 2*s^2*l*t + s^2*l^2 ] = 0
	//
	// Or:
	//
	//   t^2 * [ Dot(v,v) - s^2 ] + t * 2 * [ Dot(p,v) + s^2*l] + [ Dot(p,p) - s^2*l^2] = 0
	//
	// So using the standard quadratic form of at^2 + bt + c = 0:
	//
	//   a = Dot(v,v) - s^2
	//   b = 2 * (Dot(p,v) + s^2*l)
	//   c = Dot(p,p) - s^2*l^2
	//   disc = b^2 - 4ac
	//
	// And the solution for t depends on what these coefficients are:
	//
	//   a = 0, b = 0, c = 0: Any value of t
	//   a = 0, b = 0, c != 0: No solutions
	//   a = 0, b != 0: t = -c/b
	//   a != 0, disc < 0: No solutions
	//   a != 0, disc >= 0: t = min( [-b +/- sqrt(dist) ] / 2a)
	//
	// Of course, sometimes the solutions require time < lag (ie. the only
	// way to hit is if time ran backwards).  In these cases, the solution to
	// this equation is essentially undefined.
	//
	// NOTE: Originally this function just returned the amount of time it
	// would take for the missile to contact the target if it were stationary.
	// However, testing has shown that older estimate is not good enough,
	// especially when the target is moving orthogonal to the view direction
	// between the bot and the target.  This newer estimate is much better,
	// even though it's just based on simple linear extrapolation.

	// Cache the speed's square since its used a lot
	speed_square = Square(speed);

	// Compute the terms of the quadratic formula to solve
	a = (DotProduct(velocity, velocity) - speed_square);
	b = 2 * (DotProduct(offset, velocity) + speed_square * lag);
	c = (DotProduct(offset, offset) - speed_square * Square(lag));

	// Try for the standard quadratic solution ...
	if(!a)
	{
		// Solutions only exist when the discriminant is non-negative
		disc = Square(b) - 4 * a * c;
		if(disc >= 0.0)
		{
			// Cache the root of the discriminant
			disc = sqrt(disc);

			// Use the sooner time if allowed ...
			time = (-b - disc) / (2 * a);
			if(time >= lag)
				return time;

			// ... Otherwise try to use the later time
			time = (-b + disc) / (2 * a);
			if(time >= lag)
				return time;
		}

		// No well defined solutions exist
	}


	// .. Otherwise try a simple linear solution ...
	else if(!b)
	{
		// Use the linear time solution if that time hasn't passed
		time = -c / b;
		if(time >= lag)
			return time;

		// No well defined solutions exist
	}

	// ... As a last resort use the time to reach the target's current position
	time = VectorLength(offset) / speed;
	return (time >= lag ? time : lag);
}

/*
==========================
BotTargetBoundingBoxExpand

Expands the attack target's bounding box by the percent inputted
by "scale".  For example, if scale is 1.5, the new box
will be 1.5x the size of the old one.  This function only increases
the two axies most perpendicular to offset vector between the target
and the bot.  It also never expands in the Z direction if the target
can't move in that axis.

This function essentially creates an "error space" of potential
locations the target might end up in, so the bot can shoot if its
shots would hit anything in that range.  It is used by rapid fire
weapons where careful aiming is not needed.

NOTE: Do not use this function to shrink bounding boxes (scale < 1).
Doing so could cause the bot's shot location to be located outside
of the bounding box.  This means the bot could be perfectly lined
up with its shot location, but it would never shoot because that
location wouldn't be inside the box.  Use BotTargetBoundingBoxShrink()
to shrink boxes.
==========================
*/
void BotTargetBoundingBoxExpand(bot_state_t * bs, vec3_t offset, float scale, vec3_t mins, vec3_t maxs)
{
	int             axis, colinear_axis;
	vec3_t          magnitude;

	// Extract the locally oriented bounding box
	VectorCopy(bs->attack.motion.mins, mins);
	VectorCopy(bs->attack.motion.maxs, maxs);

	// Never shrink the bounding box
	if(scale <= 1.0)
		return;

	// Compute the relative axial magnitudes of the direction to the target
	VectorCopy(offset, magnitude);
	magnitude[0] = fabs(magnitude[0]);
	magnitude[1] = fabs(magnitude[1]);
	magnitude[2] = fabs(magnitude[2]);

	// Determine which axis is most colinear with the target's direction
	if(magnitude[0] > magnitude[1] && magnitude[0] > magnitude[2])
		colinear_axis = 0;
	else if(magnitude[1] > magnitude[2])
		colinear_axis = 1;
	else
		colinear_axis = 2;

	// Magnify the bounding box about the shot location by this factor
	for(axis = 0; axis < 3; axis++)
	{
		// Don't scale the axis most colinear with the direction to the target
		//
		// NOTE: This is the axis that most expands towards the bot.  So an expansion
		// in this axis creates the least change in view when projected onto the
		// bot's view sphere.  Also, it is the axis most likely to create an extended
		// bounding box that contains the bot (which would cause the bot to fire
		// no matter what).  This code makes it nearly impossible for the expanded
		// bounding box to contain the bot.
		if(axis == colinear_axis)
			continue;

		// Don't expand the Z axis if the target can't control its vertical movement
		if(axis == 2 && bs->attack.motion.physics.type != PHYS_WATER && bs->attack.motion.physics.type != PHYS_FLIGHT)
		{
			continue;
		}

		// Scale the bounding box by the appropriate factor
		mins[axis] *= scale;
		maxs[axis] *= scale;
	}
}

/*
==========================
BotTargetBoundingBoxShrink

Shrinks the attack target's bounding box by the percent inputted by
"scale".  For example, if scale is 0.9, the new box will be 0.9x the
size of the old one.  This function always shrinks all three axies,
unlike its expansion counterpart

This function essentially compensates for a margin of error, guaranteeing
that any shot landing on this bounding box will hit the target (assuming
the target moved in the predicted manner).  This compensates for any minor
prediction errors.  It is used by slow fire weapons where careful aiming
is not required.

NOTE: Do not use this function to expand bounding boxes (scale > 1).
Doing so would expand all axies, which could cause the expanded box to
contain the bot's fire location.  In such a case, the bot would always
think it's shot would hit no matter where it aimed.  Use
BotTargetBoundingBoxExpand() to expand boxes.

FIXME: Perhaps the functions could be merged together into a
BotTargetBoundingBoxScale() function and the input value of scale
could arbitrate which of these functions gets called.
==========================
*/
void BotTargetBoundingBoxShrink(bot_state_t * bs, float scale, vec3_t mins, vec3_t maxs)
{
	int             axis;
	float           radius, center, new_min, new_max;
	vec3_t          shot_offset;

	// Extract the target's current bounding box
	VectorCopy(bs->attack.motion.mins, mins);
	VectorCopy(bs->attack.motion.maxs, maxs);

	// Never expand the bounding box
	if(scale >= 1.0 || scale < 0.0)
		return;

	// Compute the attack location's local coordinates in the bounding box
	VectorSubtract(bs->attack.shot_loc, bs->attack.motion.origin, shot_offset);

	// Shrink each axis in turn
	for(axis = 0; axis < 3; axis++)
	{
		// Compute the radius and center of this axis of the bounding box
		radius = (maxs[axis] - mins[axis]) * 0.5;
		center = mins[axis] + radius;

		// Compute the shrunken bounds about the center
		radius *= scale;
		new_min = center - radius;
		new_max = center + radius;

		// Shrink the bounds of this axis provided it would not exclude
		// the shot location, accounting for a small margin
		if(shot_offset[axis] >= new_min + 1)
			mins[axis] = new_min;
		if(shot_offset[axis] <= new_max - 1)
			maxs[axis] = new_max;
	}
}

/*
=====================
BotAttackTargetBounds

Computes the globally aligned bounding box the bot
should use to attack the target described in bs->attack.
This bounding box will be larger than the actual box
for carelessly fired weapons to take advantage of the
low cost of missing.  The box is smaller for careful
firing, to make absolutely sure the weapon is lined up.
=====================
*/
void BotAttackTargetBounds(bot_state_t * bs, vec3_t absmin, vec3_t absmax)
{
	float           scale;
	vec3_t          offset;

	// Compute how much to scale the bounding box for this weapon style
	if(WeaponCareless(bs->ps->weapon))
	{
		scale = bot_attack_careless_factor.value;
	}
	else
	{
		// Higher accuracy bots use a smaller scaling (are more careful) when aiming carefully
		scale = interpolate(bot_attack_careful_factor_max.value, bot_attack_careful_factor_min.value, bs->aim_accuracy);
	}

	// Expand or contract the bounding box as necessary
	if(scale > 1.0)
	{
		// Compute the offset from the bot's eye to the selected shot location and then expand
		VectorSubtract(bs->attack.shot_loc, bs->eye_future, offset);
		BotTargetBoundingBoxExpand(bs, offset, scale, absmin, absmax);
	}
	else
	{
		// Compute the shrunken bounding box
		BotTargetBoundingBoxShrink(bs, scale, absmin, absmax);
	}

	// Convert the bounds from relative to global orientation
	VectorAdd(absmin, bs->attack.motion.origin, absmin);
	VectorAdd(absmax, bs->attack.motion.origin, absmax);
}

/*
================
BotAttackPredict

Predicts the attack target's motion, adjusting for things like
lag and the time it will take for missile shots to reach the
target.  This function assumes the bot will shoot at the target
with weapon number "weapon".  The target's motion state,
bs->attack.motion, is lagged "lag" seconds behind the time at
which the server will process the bot's next command.

If prediction is needed, this function will adjust the motion state
contents accordingly, along with the attack state's shot location.
(It's almost guaranteed prediction will be needed for lag compensation,
not to mention for missile shots.)

Returns true if a new aim target was was selected and that
location can be seen (ie. shot at).  Returns false all other
times, including when the aim target didn't change.

NOTE: Just because the center of a player's body is occluded
doesn't mean the bot can't hit them.  For example, if a player is
dodging past an area that covers the upper half of their body but
not their feet, the bot will still try blast shots under the
overhang.
================
*/
qboolean BotAttackPredict(bot_state_t * bs, int weapon, float lag)
{
	float           weapon_speed, lead_time;
	vec3_t          old_origin, shift;
	trace_t         trace;

	// Compute the ideal lead time for this shot prediction
	lead_time = BotAttackLeadTime(bs, weapon, lag, bs->attack.motion.origin, bs->attack.motion.velocity);

	// Potentially modify the prediction time if the predicted target
	// has reasonable control over its movement (ie. they can dodge)
	if(bs->attack.motion.physics.type == PHYS_GROUND ||
	   bs->attack.motion.physics.type == PHYS_WATER || bs->attack.motion.physics.type == PHYS_FLIGHT)
	{
		// Only predict ahead a portion of the time when the prediction would be
		// for a fairly long time period-- the target will probably change movement
		// by then anyway, so a shot lead that much ahead will almost definitely miss.
		if(lead_time > bot_attack_lead_time_full.value)
		{
			// Full credit for the first "lead time full" seconds, scaled credit
			// for the remaining seconds
			lead_time = bot_attack_lead_time_full.value +
				bot_attack_lead_time_scale.value * (lead_time - bot_attack_lead_time_full.value);
		}
	}

	// Predict the target's motion state at the estimated time of contact and
	// compute the amount the target will shift
	VectorCopy(bs->attack.motion.origin, old_origin);
	EntityMotionPredict(bs->attack.ent, &bs->attack.motion, lead_time);
	VectorSubtract(bs->attack.motion.origin, old_origin, shift);

	// Offset the intended shot location by the prediction shift
	VectorAdd(shift, bs->attack.shot_loc, bs->attack.shot_loc);

	// Test if the predicted location is visible by the bot
	trap_Trace(&trace, bs->eye_future, NULL, NULL, bs->attack.motion.origin, bs->entitynum, MASK_SOLID);
	return (trace.fraction >= 1.0 || trace.entityNum == bs->attack.ent->s.number);
}

/*
====================
BotCanAimWeaponFloor

Test if it's possible for the bot to aim its weapon
at an unknown floor spot for a blast damage shot.
====================
*/
qboolean BotCanAimWeaponFloor(bot_state_t * bs, int weapon)
{
	weapon_stats_t *ws;

	// Only somewhat skilled bots can aim at the floor
	if(bs->weapon_char_skill[weapon] < 0.5)
		return qfalse;

	// Only do ground shots for weapons with a reasonably large blast radius
	ws = &weapon_stats[weapon];
	if(ws->radius < 75)
		return qfalse;

	// Don't do floor shots with delayed blast weapons
	if(ws->flags & WSF_DELAY)
		return qfalse;

	// Blast shots are permitted in theory
	// NOTE: Other functions (like BotAttackFloor) must still check
	// if a blast shot is possible for a specific floor location.
	return qtrue;
}

/*
=====================
BotBlastShotCanDamage

Test if a blast shot originating from "origin" impacting
at "blast" with blast radius "radius" can damage the
target "ent" which is expected to be at world bounding box
"absmin" / "absmax".
=====================
*/
qboolean BotBlastShotCanDamage(bot_state_t * bs, gentity_t * ent, vec3_t absmin, vec3_t absmax,
							   vec3_t origin, vec3_t blast, float radius)
{
	float           dist;
	vec3_t          mins, maxs, contact, start;
	trace_t         trace;

	// It's faster to work in squared distances
	radius = Square(radius);

	// Check how close the blast would explode to the bot
	//
	// FIXME: Technically to be accurate, this code should test against the
	// bot's position after the next command frame.  Doing so may require an
	// unnecessary amount of processing for minimal gains, however.
	EntityWorldBounds(bs->ent, mins, maxs);
	dist = point_bound_distance_squared(blast, mins, maxs);

	// Fail if the blast location is too close to the bot
	if(dist < radius * Square(0.9))
		return qfalse;

	// Determine how close the blast would explode to the target's bounding box
	nearest_bound_point(blast, absmin, absmax, contact);

	// Fail if the real impact location wouldn't sufficiently damage the target
	if(DistanceSquared(blast, contact) > radius * Square(0.8))
		return qfalse;

	// Slightly offset the impact location from the solid it contacted, so the
	// contact point isn't embedded in a solid object.
	//
	// NOTE: This prevents the trace from starting in a solid
	start[0] = .99 * blast[0] + .01 * origin[0];
	start[1] = .99 * blast[1] + .01 * origin[1];
	start[2] = .99 * blast[2] + .01 * origin[2];

	// The blast won't damage the target if there isn't a direct line from the
	// impact point to the bounding box
	//
	// NOTE: The actual damage check is a bit more lenient-- see CanDamage()
	// in g_combat.c for more information.
	//
	// NOTE: Some mods and games (like Rocket Arena) allow blast damage through
	// walls and floors.  Those mods should remove this line-of-sight check.
	trap_Trace(&trace, start, NULL, NULL, contact, ent->s.number, MASK_SHOT);
	if(trace.fraction < 1.0)
		return qfalse;

	// The blast shot should damage the target
	return qtrue;
}

/*
==============
BotAttackFloor

Determines a possible ground location (below bs->attack.loc)
to shoot at for weapons with blast radius.  If a new location
can be found (and seen), the location is stored in bs->attack.loc
and the function returns true.  Otherwise, bs->attack.loc remains
unchanged and the function returns false.
==============
*/
qboolean BotAttackFloor(bot_state_t * bs, int weapon)
{
	float           radius;
	vec3_t          end, ground, dir, shot_loc;
	trace_t         trace;

	// Only try this if the bot could possibly see the floor below the entity
	radius = weapon_stats[weapon].radius;
	if(bs->eye_future[2] < bs->attack.motion.absmin[2] - radius)
		return qfalse;

	// If the target will still still be in the air, don't bother with a ground blast shot
	VectorSet(end, bs->attack.motion.origin[0], bs->attack.motion.origin[1], bs->attack.motion.origin[2] - radius);
	trap_Trace(&trace, bs->attack.motion.origin,
			   bs->attack.motion.mins, bs->attack.motion.maxs, end, bs->attack.ent->s.number, MASK_SOLID);
	if(trace.fraction >= 1.0)
		return qfalse;

	// Compute where a shot aimed at the optimal ground location would land
	VectorSet(ground, trace.endpos[0], trace.endpos[1], trace.endpos[2] + bs->attack.motion.mins[2]);
	trap_Trace(&trace, bs->eye_future, NULL, NULL, ground, bs->entitynum, MASK_SOLID);
	VectorCopy(trace.endpos, shot_loc);

	// Don't aim there if the shot won't damage the target
	//
	// NOTE: The server uses the world bounding box to compute blast
	// damage, not the local bounding box.
	if(!BotBlastShotCanDamage(bs, bs->attack.ent,
							  bs->attack.motion.absmin, bs->attack.motion.absmax, bs->eye_future, shot_loc, radius))
	{
		return qfalse;
	}

	// Aim at the ground
	VectorCopy(shot_loc, bs->attack.shot_loc);

	// The bot's reference point is now the floor location, not the target
	//
	// FIXME: It's not immediately clear what point should be the reference point.
	// The point on the floor is a fixed point the eye can track onto.  This is
	// in contrast to the leading case, where the brain selects an arbitrary point
	// in space, where the only reference is the target itself.  Of course, this
	// floor point is still just an arbitrary floor point.  But technically the only
	// clearly identifia able reference point is the the floor directly below the
	// target.  The selected reference point should be one of these (the floor shot
	// location itself or the floor below the target's current location).  Selecting
	// the floor below the target will penalize the shot just as much as aiming out
	// in clear space, but using the floor point itself as the reference won't
	// penalize the potential leading enough.  This is a sign to me that the concept
	// of a "reference point" alone isn't enough.  The code also needs to know how easy
	// it is for the eye to track onto the selected location offset from the reference.
	// (Keep in mind that the reference point is defined as the place the eye has
	// no difficulty tracking onto.)  This code opts to use the actual shot location
	// as the reference point.  This could make the shots a little too good, but
	// blast locations are more of an art than a science, so I believe it's better
	// for them to be too good than not good enough.  That said, this section of
	// the aiming model should be refined.
	//
	// FIXME: Perhaps the correct reference point to use is the bottom of the target's
	// bounding box (ie. feet).
	VectorCopy(shot_loc, bs->attack.reference);

	// Successfully aimed at the floor
	return qtrue;
}

/*
===================
BotAttackNotVisible
===================
*/
qboolean BotAttackNotVisible(bot_state_t * bs, int weapon)
{
	int             area;
	bot_goal_t      goal;
	trace_t         trace;

	// Determine the enemy's current area
	area = LevelAreaEntity(bs->attack.ent);
	if(!area)
		return qfalse;

	// Find out where the enemy would first become visible if moving towards the bot
	GoalEntity(&goal, bs->ent);
	if(!trap_BotPredictVisiblePosition(bs->attack.ent->r.currentOrigin, area, &goal, TFL_DEFAULT, bs->attack.motion.origin))
		return qfalse;

	// Try to aim at the floot below this point
	return BotAttackFloor(bs, weapon);
}

/*
===============
BotAttackTarget

Sets up the inputted attack state for bs->attack.ent.  In
other words, this function determines what bs->attack.loc
should be.  If it's not possible to attack the inputted entity,
the function returns qfalse.
===============
*/
qboolean BotAttackTarget(bot_state_t * bs, int weapon)
{
	float           lag;
	qboolean        visible, blast;
	gentity_t      *ent;

	// Don't aim at entities that don't exist
	ent = bs->attack.ent;
	if(!ent)
		return qfalse;

	// There are special checks when attacking non-player targets
	if(!ent->client)
	{
#ifdef MISSIONPACK
		// Don't attack an obelisk that hasn't respawned yet
		if((ent->s.eType == ET_TEAM) && (ent->activator) && (ent->activator->s.frame == 2))
		{
			return qfalse;
		}
#endif
	}

	// Make sure the bot has predicted its motion state for the upcoming server frame
	BotMotionFutureUpdate(bs);

	// Look up the target's motion state, lagged by a constant amount of time
	//
	// NOTE: This motion state won't necessarily represent target target's position
	// "lag" seconds from the current time.  All this code needs to know is that it
	// must predict "lag" seconds ahead to determine the target's position when the
	// bot will next execute a command.  See BotEntityMotionLagged() in ai_motion.c
	// for more information.
	//
	// NOTE: The "bot_lag_min" variable tries to provide a minimum amount of lag the
	// bots will have against all other players.  When aiming at bots with lower
	// client number, however, it is impossible for the lag to be any less than one
	// server frame (50 ms).  See BotEntityLatency() in ai_motion.c for more
	// information.
	lag = BotEntityMotionLagged(bs, ent, bot_lag_min.value, &bs->attack.motion);

	// Check if and where the target is visible
	visible = (BotEntityVisibleCenter(bs, ent, bs->eye_future, bs->attack.reference) > 0.0);

	// Use the target's center as the reference point if the entity is occluded
	if(!visible)
		VectorCopy(bs->attack.motion.origin, bs->attack.reference);

	// Use the visual reference point as the shot location by default
	VectorCopy(bs->attack.reference, bs->attack.shot_loc);

	// Predict where the bot should actually aim to hit the enemy;
	// "visible" is true if the (possibly new) location is visible
	if(BotAttackPredict(bs, weapon, lag))
		visible = qtrue;

	// Don't attack targets that are clearly out of range
	if(!WeaponInRange(weapon, Distance(bs->eye_future, bs->attack.shot_loc)))
		return qfalse;

	// Check if floor blast shots are permitted with the bot's current weapon and skill
	blast = BotCanAimWeaponFloor(bs, weapon);

	// Aim at the floor if possible
	if(blast && BotAttackFloor(bs, weapon))
		return qtrue;

	// Aiming at the center of the predicted location is an acceptable option
	if(visible)
		return qtrue;

	// As a last resort, shoot the nearby floor to hit with blast damage
	if(blast && BotAttackNotVisible(bs, weapon))
		return qtrue;

	// The bot could not effectively aim at this target
	return qfalse;
}

/*
===============
BotAttackSelect

Select a real world location for the bot to aim at
for the next server frame (ie. time bs->command_time)
in order to attack with the specified weapon.  Also
decide if the bot should do blast shots, predicted
shots (for missiles), and so on.

If the shot could not be properly setup, the function
returns false.  Otherwise it sets up bs->attack and
returns true.
===============
*/
qboolean BotAttackSelect(bot_state_t * bs, gentity_t * ent, int weapon, float sighted)
{
	// Ignore invalid weapons
	if(weapon <= WP_NONE && weapon >= WP_NUM_WEAPONS)
		return qfalse;

	// Aim at the entity if possible
	bs->attack.ent = ent;
	if(!BotAttackTarget(bs, weapon))
	{
		bs->attack.ent = NULL;
		return qfalse;
	}

	// Use this weapon and it's accuracy and skill values
	bs->weapon = weapon;
	bs->aim_accuracy = bs->weapon_char_acc[weapon];
	bs->aim_skill = bs->weapon_char_skill[weapon];

	// Record when the bot first saw this target so it won't attack it too soon
	bs->attack.sighted = sighted;

	return qtrue;
}

/*
=================
BotAttackAddError

The BotAttackSelect() function determines the ideal
place to attack, but the bot might have some kind of
selection error (especially for weapons requiring
lead).  This function incorporates the selection error
into the bot's attack state.
=================
*/
void BotAttackAddError(bot_state_t * bs, vec3_t error)
{
	// All world-based coordinates are offset by the error
	// (except the reference, which by definition cannot have error)
	VectorAdd(bs->attack.shot_loc, error, bs->attack.shot_loc);
	VectorAdd(bs->attack.motion.origin, error, bs->attack.motion.origin);
	VectorAdd(bs->attack.motion.absmin, error, bs->attack.motion.absmin);
	VectorAdd(bs->attack.motion.absmax, error, bs->attack.motion.absmax);
}

/*
=======================
BotAttackCheckDirectHit

Test if a shot fired will score a direct hit
against the bot's attack target, given a
modified version of the target's bounding box.
=======================
*/
qboolean BotAttackCheckDirectHit(bot_state_t * bs, vec3_t muzzle, vec3_t forward, float range, vec3_t absmin, vec3_t absmax)
{
	int             result;
	vec3_t          dir, contact, end;
	trace_t         trace;

	// Fail if the shot wouldn't hit the target's bounding box
	//
	// NOTE: All this function does is test if the ray rooted at "muzzle" heading in
	// the "forward" direction will intersect the bounding box.  The test doesn't
	// check world geometry at all.
	result = trace_box(muzzle, forward, absmin, absmax, contact, NULL);
	if(!(result & TRACE_HIT))
		return qfalse;

	// If the target's bounding box contains the starting trace location, count it as a hit
	if(!(result & TRACE_ENTER))
		return qtrue;

	// Check where this shot would next contact a wall
	VectorMA(muzzle, range, forward, end);
	trap_Trace(&trace, muzzle, NULL, NULL, end, bs->entitynum, MASK_SOLID);

	// Consider this shot a hit if it hits the predicted bounding box before the wall
	if(DistanceSquared(muzzle, contact) < DistanceSquared(muzzle, trace.endpos))
	{
		return qtrue;
	}

	// The shot was blocked by a wall or other object
	return qfalse;
}

/*
=======================
BotAttackCheckSpreadHit

Test if the bot's attack target is contained in the
weapon's current cone of spread.  In other words,
find out if this shot is lined up as best it can be.
=======================
*/
qboolean BotAttackCheckSpreadHit(bot_state_t * bs, vec3_t muzzle, vec3_t forward)
{
	float           weapon_spread, target_spread, allowed_spread;
	float           dist, radius, inv_weight_total;
	vec3_t          cubic_radius, axis_weight, to_target;
	trace_t         trace;

	// Ignore weapons with no spread
	//
	// NOTE: Nothing bad will happen if this test is ommitted, since this
	// function degenerates to testing if the bot is perfectly lined up
	// with the center of the target.  That's extremely unlikely.  In addition,
	// The direct hit test is more generous, since it returns true for
	// contact anywhere with the bounding box, making this test superfluous.
	weapon_spread = weapon_stats[bs->weapon].spread;
	if(weapon_spread <= 0.0)
		return qfalse;
	weapon_spread = DEG2RAD(weapon_spread);

	// Compute the direction and distance to the target
	VectorSubtract(bs->attack.motion.origin, muzzle, to_target);
	dist = VectorNormalize(to_target);

	// Compute the cubic radius of each axis of the target's rectangular bounding box
	VectorSubtract(bs->attack.motion.maxs, bs->attack.motion.mins, cubic_radius);
	VectorScale(cubic_radius, 0.5, cubic_radius);

	// Compute a weighting of which axies are most visible to the bot.  (The axis with
	// greatest projection is most colinear with the direction to the bot, so that one
	// is least visible.  That's why this calculation inverts the values and renormalizes.)
	VectorSet(axis_weight, 1.0 - fabs(to_target[0]), 1.0 - fabs(to_target[1]), 1.0 - fabs(to_target[2]));
	inv_weight_total = 1.0 / (axis_weight[0] + axis_weight[1] + axis_weight[2]);
	VectorScale(axis_weight, inv_weight_total, axis_weight);

	// Estimate the radius of a bounding sphere that has roughly the same view area
	// as the project of the target's bounding box onto the bot's view sphere.  This
	// is computed as the average of the cubic radii weighted by the relative visibility
	// of that radius' axis.
	radius = DotProduct(cubic_radius, axis_weight);

	// Compute the spread in radians of this target's bounding sphere projected
	// onto the view sphere
	//
	// NOTE: Using atan2() really is the faster than the other alternatives.
	target_spread = atan2(radius, dist);

	// If the target's bounding box is larger than the weapon's spread, never make a
	// spread shot-- rely on the previous tests for the center of weapon aim connecting
	// with the target's bounding box.
	if(allowed_spread >= weapon_spread)
		return qfalse;

	// The bot's aim can differ this many radians from the center and still have the
	// weapon's spread completely contain the target's spread
	allowed_spread = weapon_spread - target_spread;

	// Fail if the target's center is displaced more than that much
	if(DotProduct(to_target, forward) < cos(allowed_spread))
		return qfalse;

	// Attack if there is a clear shot between the gun and the target
	trap_Trace(&trace, muzzle, NULL, NULL, bs->attack.motion.origin, bs->entitynum, MASK_SOLID);
	return (trace.fraction >= 1.0 || trace.entityNum == bs->attack.ent->s.number);
}

/*
======================
BotAttackCheckBlastHit

Test if a shot fired will score a blast hit
against the bot's attack target, given a
modified version of the target's bounding box.
======================
*/
qboolean BotAttackCheckBlastHit(bot_state_t * bs, vec3_t muzzle, vec3_t forward, float range, vec3_t absmin, vec3_t absmax)
{
	float           radius;
	vec3_t          end;
	trace_t         trace;

	// If the weapon doesn't deal blast damage, fail immediately
	radius = weapon_stats[bs->ps->weapon].radius;
	if(!radius)
		return qfalse;

	// Check where this shot would next contact a wall
	VectorMA(muzzle, range, forward, end);
	trap_Trace(&trace, muzzle, NULL, NULL, end, bs->entitynum, MASK_SOLID);

	// Fail if the shot will not hit a solid wall
	if((trace.fraction >= 1.0) || (trace.surfaceFlags & SURF_NOIMPACT))
	{
		return qfalse;
	}

	// Test if a hit at this location will deal blast damage
	return BotBlastShotCanDamage(bs, bs->attack.ent, absmin, absmax, muzzle, trace.endpos, radius);
}

/*
=================
BotAttackCheckHit

Test if an attack now would hit the bot's target, as
described by bs->attack.  The test assumes the weapon
is aimed in the angles specified by "view".  The global
bounding box to use for hit tests is supplied as absmin
and absmax, which could differ from the target's actual
bounding box.

Returns true if the bot believes shooting is a good idea
and false if not.
=================
*/
qboolean BotAttackCheckHit(bot_state_t * bs, vec3_t view, vec3_t absmin, vec3_t absmax)
{
	float           range;
	vec3_t          forward, muzzle;
	weapon_stats_t *ws;

	// Extract the forward direction for this view
	AngleVectors(view, forward, NULL, NULL);

	// Compute the fire location (muzzle) from the bot's position next frame
	//
	// NOTE: This code is based in part on CalcMuzzlePointOrigin() from g_weapon.c.
	//
	// FIXME: CalcMuzzlePointOrigin() seems to try firing from last frame's
	// location but fails.  It uses ent->s.pos.trBase, which is a snapped
	// version of ps->origin.  This could be construed as a bug.  In either
	// case, it's structure and execution are questionable.
	VectorCopy(bs->eye_future, muzzle);
	VectorMA(muzzle, 14, forward, muzzle);
	SnapVector(muzzle);

	// Look up how far the bot thinks this weapon shoots
	range = WeaponPerceivedMaxRange(bs->ps->weapon);

	// Succeed if the bot can directly hit the target
	if(BotAttackCheckDirectHit(bs, muzzle, forward, range, absmin, absmax))
		return qtrue;

	// Succeed if the bot can't get a better shot when using a weapon with spread
	if(BotAttackCheckSpreadHit(bs, muzzle, forward))
		return qtrue;

	// Also succeed if the bot can damage the target with blast
	if(BotAttackCheckBlastHit(bs, muzzle, forward, range, absmin, absmax))
		return qtrue;

	// Fail because the potential shot would miss
	return qfalse;
}

/*
===================
BotAttackFireUpdate

This function decides whether or not the bot should shoot,
given the information in bs->attack.  If so, the bs->fire_choice
value will be set, causing the bot to try shooting next frame.

NOTE: Generally the bot's intended view location should be
bs->attack.loc, but making the bot aim in the right place isn't
this function's purpose.  It just chooses whether or not
to fire the weapon.

NOTE: This function doesn't actually send the attack command.
It just sets a timer stating that the bot will continue to
fire until that timer expires.  The function that actually
makes the bot shoot is BotAttackFireWeapon().
===================
*/
void BotAttackFireUpdate(bot_state_t * bs)
{
	vec3_t          perceived_view;
	vec3_t          modified_absmin, modified_absmax;

	// By default, assume the bot will not choose to fire
	bs->fire_choice = qfalse;

	// Don't shoot if the bot hasn't loaded the requested weapon yet
	if(bs->ps->weapon != bs->weapon)
		return;

	// Don't shoot if the bot is out of ammo
	//
	// NOTE: Remember that ammo of -1 means unlimited ammo
	if(!bs->ps->ammo[bs->ps->weapon])
		return;

	// Don't shoot too soon if the bot just teleported
	if((bs->teleport_time > 0) && (bs->command_time - bs->teleport_time < bs->react_time))
	{
		return;
	}

#ifdef DEBUG_AI
	// Always shoot if the previous minimal requirements have been
	// met and the bot should always shoot
	if(bs->debug_flags & BOT_DEBUG_MAKE_SHOOT_ALWAYS)
	{
		bs->fire_choice = qtrue;
		return;
	}
#endif

	// Don't shoot if the bot has no target to attack
	if(!bs->attack.ent)
		return;

	// Don't shoot if the bot hasn't made visual contact with the target
	if(bs->attack.sighted < 0.0)
		return;

	// Don't shoot if the bot hasn't reacted to first making visual contact
	if(bs->command_time < bs->attack.sighted + bs->react_time)
		return;

	// Fail if the shot is obviously out of range
	if(!WeaponInRange(bs->ps->weapon, Distance(bs->eye_now, bs->attack.shot_loc)))
		return;

	// Confirm that the bot has the most current future prediction state
	BotMotionFutureUpdate(bs);

	// Look up the bot's perception of its current aim angles
	ViewAnglesPerceived(bs->view_now, perceived_view);

	// Compute the target's bounding box to use for fire decisions
	BotAttackTargetBounds(bs, modified_absmin, modified_absmax);

	// Check if the shot would hit the modified bounding box from the bot's perceived view
	bs->fire_choice = BotAttackCheckHit(bs, perceived_view, modified_absmin, modified_absmax);

#ifdef DEBUG_AI
	// Output reasoning behind the bot's fire selection
	if(bs->debug_flags & BOT_DEBUG_INFO_SHOOT)
	{
		vec3_t          actual_view;	// Where the bot is actually aiming
		vec3_t          actual_absmin;	// Target's global bounding box minimums
		vec3_t          actual_absmax;	// Target's global bounding box maximums
		qboolean        fire_corrected_view;	// Choice when bot correctly understands its view
		qboolean        fire_actual_bounds;	// Choice when bot correctly understands target position
		qboolean        fire_both;	// Chocie when bot correctly understands both

		// Look up the bot's actual aim angles
		ViewAnglesReal(bs->view_now, actual_view);

		// Look up the target's actual bounding box
		//
		// NOTE: This will differ from bs->attack.motion.absmin/max because those bounds
		// are snapped multiple times during prediction, resulting in data degredation.
		// Some algorithms require that degraded data, but not fire decision.
		VectorAdd(bs->attack.motion.origin, bs->attack.motion.mins, actual_absmin);
		VectorAdd(bs->attack.motion.origin, bs->attack.motion.maxs, actual_absmax);

		// Determine if the bot would have fired if it had more information
		fire_corrected_view = BotAttackCheckHit(bs, actual_view, modified_absmin, modified_absmax);
		fire_actual_bounds = BotAttackCheckHit(bs, perceived_view, actual_absmin, actual_absmax);
		fire_both = BotAttackCheckHit(bs, actual_view, actual_absmin, actual_absmax);

		// Output nothing if there were no discrepancies
		//
		// NOTE: Just because the bot made a poor choice doesn't mean
		// the AI made the wrong choice.
		if(bs->fire_choice == fire_corrected_view && bs->fire_choice == fire_actual_bounds && bs->fire_choice == fire_both)
		{
			return;
		}

		// State the bot's perception (and decision)
		G_Printf("%s: %.3f Fire decision mismatch\n  Bot expected %s\n",
				 EntityNameFast(bs->ent), bs->command_time, PrintStringHitStatus(bs->fire_choice));

		// State how the bot analyses the shot against the official bounding box
		G_Printf("  Bot expected %s against the actual target bounds\n", PrintStringHitStatus(fire_actual_bounds));

		// State what bot perceives with errors removed from view perception
		G_Printf("  Bot expected %s with correct view understanding\n", PrintStringHitStatus(fire_corrected_view));

		// State what would have occurred
		G_Printf("  Shot would %s\n", PrintStringHitStatus(fire_both));
	}
#endif
}

/*
===================
BotAttackFireWeapon
===================
*/
void BotAttackFireWeapon(bot_state_t * bs)
{
	// Assume the bot won't fire
	//
	// NOTE: This is necessary because this processing code can
	// get called more than once before the data gets sent to
	// the server.  If a previous decision decided to attack,
	// a later processing needs to be able to change that choice.
	bs->cmd.buttons &= ~BUTTON_ATTACK;

#ifdef DEBUG_AI
	// Don't shoot if bot shooting has been turned off
	if(bs->debug_flags & BOT_DEBUG_MAKE_SHOOT_STOP)
		return;
#endif

	// Reset the start and end firing timestamps if they have expired
	if(bs->fire_stop_time && bs->fire_stop_time <= bs->command_time)
	{
		bs->fire_start_time = 0;
		bs->fire_stop_time = 0;
	}

	// Handle the desire to fire if necessary ...
	if(bs->fire_choice)
	{
		// Schedule a time to start firing if this is a new decision
		if(!bs->fire_start_time)
			bs->fire_start_time = bs->command_time + bs->react_time;

		// Cancel any decision to stop firing
		bs->fire_stop_time = 0;
	}

	// ... Otherwise handle the desire not to fire
	else
	{
		// Schedule a time to stop firing if the bot chose to fire and hadn't
		// yet decided to stop
		if(!bs->fire_stop_time && bs->fire_start_time)
		{
			// Can't stop firing any sooner than the next command frame
			bs->fire_stop_time = bs->command_time;

			// For careless attack weapons ("click-and-hold" fire style),
			// continue firing for a little while longer.
			if(WeaponCareless(bs->ps->weapon))
				bs->fire_stop_time += bs->react_time * bot_attack_continue_factor.value;

			// Cancel the start of shooting if the bot's reactions are good enough
			if(bs->fire_stop_time <= bs->fire_start_time)
			{
				bs->fire_start_time = 0;
				bs->fire_stop_time = 0;
			}
		}
	}

	// Don't shoot if the bot isn't trying to attack
	if(!bs->fire_start_time)
		return;

	// Don't shoot if the bot hasn't had time to start attacking
	if(bs->command_time < bs->fire_start_time)
		return;

	// Don't shoot if the bot tried to stop attacking and had time to do so
	if(bs->fire_stop_time && bs->fire_stop_time <= bs->command_time)
		return;

	// Fire the weapon
	//
	// NOTE: The bot might could still the attack command before the weapon
	// reloads (much like how humans hold down the attack button when the
	// machinegun fires, even though the machinegun only reloads every other
	// frame.)
	BotCommandAction(bs, ACTION_ATTACK);
}
