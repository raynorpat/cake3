// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_predict.c
 *
 * Functions the bot uses to predict motion in entities
 *****************************************************************************/

#include "ai_main.h"
#include "ai_vars.h"
#include "ai_predict.h"

#include "ai_command.h"
#include "ai_entity.h"
#include "ai_motion.h"

// This file provides access to some of the acceleration constants
// used in player movement.
#include "../bg_local.h"

// The granularity of predictions in the server never exceeds this value.
//
// NOTE: See Pmove() in bg_pmove.c for more information.
#define PREDICT_GRANULARITY_MAX 0.066

// The maximum number of prediction frames that should be run per prediction
//
// NOTE: Adhering to this maximum may cause prediction granularities above
// PREDICT_GRANULARITY_MAX.
//
// NOTE: Increasing this value will notably decrease prediction errors.
// Of course, what is a reasonable amount of processing power to spend
// on predictions depends on many factors this code cannot know-- such as
// the number of bots and the processor's speed.
#define PREDICT_FRAMES_MAX 10


#ifdef DEBUG_AI

// A structure to record past predictions so they can be tested against
// their actual values
typedef struct predict_debug_s
{
	gentity_t      *ent;		// Entity which was predicted
	float           time_lapse;	// Amount of time ahead of the prediction
	motion_state_t  motion;		// The predicted motion state, including timestamps
} predict_debug_t;

// Maximum number of prediction entries
#define DEBUG_PREDICT_MAX 40

// Cached predictions from older states
predict_debug_t predict_debug_list[DEBUG_PREDICT_MAX];
int             predict_debug_size = 0;

// Last time a prediction was automatically generated
float           last_predict_time = 0.0;

#endif


/*
=================
BotAIPredictReset

Resets any data used in predictions.
=================
*/
void BotAIPredictReset(void)
{
#ifdef DEBUG_AI
	// Reset the predicition test cache
	predict_debug_size = 0;
	last_predict_time = 0.0;
#endif
}

/*
=============================
EntityMotionPredictTrajectory

Predicts an entity's motion state after "time" seconds
of applying its current trajectory.
=============================
*/
void EntityMotionPredictTrajectory(gentity_t * ent, motion_state_t * motion, float time)
{
	float           end_time;
	vec3_t          shift;
	trajectory_t    tr;

	// Construct a trajectory record for this entity using as much motion
	// state data as possible
	memcpy(&tr, &ent->s.pos, sizeof(trajectory_t));

	// Update the position and velocity
	//
	// FIXME: This might cause weird efforts with strange trajectories like TR_SINE
	VectorCopy(motion->origin, tr.trBase);
	VectorCopy(motion->velocity, tr.trDelta);

	// Evaluate the entity's trajectory at the requested time
	end_time = motion->time + time;
	BG_EvaluateTrajectory(&tr, end_time, motion->origin);
	BG_EvaluateTrajectoryDelta(&tr, end_time, motion->velocity);

	// Shift the absolute bounding boxes accordingly
	VectorSubtract(motion->origin, tr.trBase, shift);
	VectorAdd(motion->absmin, shift, motion->absmin);
	VectorAdd(motion->absmax, shift, motion->absmax);

	// Increment the time
	motion->time += time;
}

/*
===================================
EntityMotionPredictVelocityFriction

Apply "time" seconds of friction to
the motion state's velocity.

NOTE: This function is based on PM_Friction()
in bg_pmove.c.
===================================
*/
void EntityMotionPredictVelocityFriction(gentity_t * ent, motion_state_t * motion, float time)
{
	float           speed, friction, speed_remainder;
	vec3_t          velocity;

	// Compute the motion state speed, ignoring slopes for entities walking on ground
	VectorCopy(motion->velocity, velocity);
	if(motion->physics.walking)
		velocity[2] = 0.0;
	speed = VectorLength(velocity);

	// Slow moving entities have no need for friction
	//
	// NOTE: This check also prevents division by zero later
	//
	// NOTE: The < 1 is not a bug.  It should not be == 0.0, according to the PM_Friction()
	if(speed < 1)
	{
		// Just set the XY velocity to zero in this case (but NOT the Z velocity)
		motion->velocity[0] = 0.0;
		motion->velocity[1] = 0.0;
		return;
	}

	// Compute the cumulative deceleration due to friction in one second
	friction = 0.0;

	// Ground resistance
	if((motion->water_level <= 1) && (motion->physics.walking) && (!motion->physics.knockback))
	{
		friction += pm_friction * (pm_stopspeed > speed ? pm_stopspeed : speed);
	}

	// Water resistance
	if(motion->water_level)
	{
		friction += pm_waterfriction * motion->water_level * speed;
	}

	// Flying players have resistance too so they don't keep moving forever
	if(motion->physics.type == PHYS_FLIGHT)
		friction += pm_flightfriction * speed;

	// Compute the percentage of speed remaining after one frame's worth of friction
	speed_remainder = 1.0 - (friction * time / speed);
	if(speed_remainder < 0.0)
		speed_remainder = 0.0;

	// Apply the friction deceleration
	VectorScale(motion->velocity, speed_remainder, motion->velocity);
}

/*
=====================================
EntityMotionPredictVelocityAccelerate

Accelerates the entity's motion velocity
for "time" seconds, trying to reach the desired
movement direction and speed ("desired_dir"
and "desired_speed").

NOTE: This function is based on PM_Accelerate
in bg_pmove.c.

NOTE: The values in PM_Accelerate() in bg_pmove.c
are not actually based on real physics equations.
For example, what the original code calls "accelspeed"
("speed_change") is actually in units of m^2/s^2,
not m/s^2, and a value of m^3/s^2 modities the
velocity which should be in m/s.  As such, it's
extremely difficult to appropriately name the
variables.

FIXME: PM_Accelerate() should be rewritten to allow
any input initial velocity value so this code can
just call it directly.  Right now it accesses the
static global pm->ps->velocity instead of letting
"velocity" be an input.
=====================================
*/
void EntityMotionPredictVelocityAccelerate(gentity_t * ent, motion_state_t * motion, float time,
										   vec3_t desired_dir, float desired_speed)
{
	float           accel, max_speed_change, speed_change;

	// Determine how fast the entity can accelerate
	switch (motion->physics.type)
	{
		case PHYS_FLIGHT:
			accel = pm_flyaccelerate;
			break;
		case PHYS_WATER:
			accel = pm_wateraccelerate;
			break;

		case PHYS_GRAVITY:
			accel = pm_airaccelerate;
			break;

		default:
		case PHYS_GROUND:
			accel = (motion->physics.knockback ? pm_airaccelerate : pm_accelerate);
			break;
	}

	// Compute the maximum allowed speed change, making it easier to change
	// speeds to directions similar to the current velocity than those
	// that are different
	max_speed_change = desired_speed - DotProduct(motion->velocity, desired_dir);
	if(max_speed_change <= 0)
		return;

	// Compute the actual speed change to apply
	speed_change = accel * desired_speed * time;
	if(speed_change > max_speed_change)
		speed_change = max_speed_change;

	// Apply the acceleration
	VectorMA(motion->velocity, speed_change, desired_dir, motion->velocity);
}

/*
================================
EntityMotionPredictVelocitySlope

Modifies the entity's motion velocity
if it's standing on a surface, since the
surface exerts a normal force on the
velocity which could sheer or negate it.

NOTE: This function is based on portions
of PM_WalkMove(), PM_FlyMove(), PM_AirMove(),
and PM_WaterMove() in bg_pmove.c.
================================
*/
void EntityMotionPredictVelocitySlope(gentity_t * ent, motion_state_t * motion)
{
	float           speed;

	// If no ground surface exists, no force will be exerted
	if(VectorCompare(motion->physics.ground, vec3_origin))
		return;

	// Ground surfaces never exert forces on flying entities (unless the entity
	// moves into the surface, but that is handled by the slide movement case)
	if(motion->physics.type == PHYS_FLIGHT)
		return;

	// When using water physics, exit if the motion is away from the ground surface
	//
	// NOTE: This check doesn't apply in other cases, even though it's possible for
	// an entity to be moving upwards (for example, from a jump).  This may or may
	// not be a bug in the bg_pmove.c code.
	if((motion->physics.type == PHYS_WATER) && (DotProduct(motion->velocity, motion->physics.ground) >= 0.0))
	{
		return;
	}

	// Preserve the initial speed so it can (possibly) be restored after
	// the velocity is sheered
	if(motion->physics.type != PHYS_GRAVITY)
		speed = VectorLength(motion->velocity);

	// Sheer the velocity along the ground plane
	PM_ClipVelocity(motion->velocity, motion->physics.ground, motion->velocity, OVERCLIP);

	// Restore the initial velocity except when gravity physics are applied
	//
	// NOTE: Again, this is based on bg_pmove.c code, and there may or may not
	// be a bug in that code.
	if(motion->physics.type != PHYS_GRAVITY)
	{
		VectorNormalize(motion->velocity);
		VectorScale(motion->velocity, speed, motion->velocity);
	}
}

/*
============================
EntityMotionPredictMoveSlide

Predicts the entity's motion state "time" seconds
in the future using simple sliding movement using
the requested physics (so the entity is stopped by
walls and obstacles).  The motion state is modified
to match the future prediction.

The function true if the slide prediction didn't
encounter any obstacles and false if some kind of
corrections were needed

NOTE: This function is based on PM_SlideMove() in
bg_slidemove.c.
============================
*/
#define MAX_FORCES 5
qboolean EntityMotionPredictMoveSlide(gentity_t * ent, motion_state_t * motion, float time)
{
	int             tests, first, second, third;
	int             entnum, num_forces;
	float           gravity_loss, time_predicted, speed;
	vec3_t          slide_end, intersect_dir;
	vec3_t          start_velocity, final_velocity, attempted_velocity, attempted_final_velocity;
	vec3_t          force[MAX_FORCES];
	qboolean        on_ground, use_gravity, no_obstacles;
	trace_t         trace;

	// Sanity check the time
	if(time <= 0.0)
		return qtrue;

	// Check if a valid ground normal exists
	on_ground = !VectorCompare(motion->physics.ground, vec3_origin);

	// Cache the entity's number and trace mask
	entnum = ent - g_entities;

	// The velocity should not change (except for gravity) when a movement
	// flag timer is active
	//
	// NOTE: Don't shoot me; I'm just the messenger.  This "feature" is
	// dutifully copied from bg_slidemove.c.  I hate it just as much as you.
	VectorCopy(motion->velocity, start_velocity);

	// Some setup is needed when applying gravity physics
	use_gravity = (motion->physics.type == PHYS_GRAVITY);
	if(use_gravity)
	{
		// Compute the velocity loss due to gravity for the full prediction time
		gravity_loss = g_gravity.value * time;

		// Compute what the velocity will be after a full "time" seconds of
		// prediction.  The motion state's velocity will be set to this after
		// prediction is completed.
		//
		// NOTE: Even though this function might not execute all "time" seconds
		// of prediction, the returned velocity always applies the full gravitational
		// acceleration.  Technically this is a bug in PM_SlideMove(), and this
		// code just happens to be bug compliant.
		VectorSet(final_velocity, motion->velocity[0], motion->velocity[1], motion->velocity[2] - gravity_loss);

		// Only half of the gravitation deceleration applies when computing the
		// motion state position changes
		motion->velocity[2] -= gravity_loss * 0.5;

		// Account for gravitational acceleration in the fixed starting velocity
		start_velocity[2] = final_velocity[2];

		// Clip the gravity accelerated velocity along the ground surface if one exists.
		// (It will probably be very steep if it does.)
		if(on_ground)
			PM_ClipVelocity(motion->velocity, motion->physics.ground, motion->velocity, OVERCLIP);
	}

	// Initialize the list of resistant force normals
	num_forces = 0;

	// If a ground surface exists, it exerts a resistant force
	if(on_ground)
	{
		// NOTE: This is a macro so don't try to simplify this code by
		// putting the increment inside the macro-- the code will increment
		// by 3 instead of by 1.
		VectorCopy(motion->physics.ground, force[num_forces]);
		num_forces++;
	}

	// Add the current velocity as a resistant force normal
	VectorCopy(motion->velocity, force[num_forces]);
	VectorNormalize(force[num_forces++]);

	// No obstacles have yet been encountered
	no_obstacles = qtrue;

	// Try moving forward while there is still time to be processed, but
	// cap the number of tests that will be made-- this guarantees that the
	// loop will terminate in a reasonable amount of time.
	for(tests = 4; tests > 0 && time > 0.0; tests--)
	{
		// Compute the slide movement endpoint if no obstacles were in the way
		VectorMA(motion->origin, time, motion->velocity, slide_end);

		// Check for obstacles in this path
		trap_Trace(&trace, motion->origin, motion->mins, motion->maxs, slide_end, entnum, motion->clip_mask);

		// When the entity is stuck in a solid, give them some special help
		if(trace.allsolid)
		{
			// Prevent falling damage from accruing
			motion->velocity[2] = 0.0;

			// Abort the prediction
			motion->time += time;
			return qfalse;
		}

		// Update some values if any movement was predicted
		if(trace.fraction > 0.0)
		{
			// Account for the amount of time predicted
			time_predicted = time * trace.fraction;
			time -= time_predicted;
			motion->time += time_predicted;

			// Adjust the end position
			VectorCopy(trace.endpos, motion->origin);

			// Just quit now if all the time was successfully predicted
			if(time <= 0.0 || trace.fraction >= 1.0)
				break;
		}

		// An obstacle exerting a force was encountered
		no_obstacles = qfalse;

		// Abort out if the maximum number of forces would be exceeded
		//
		// NOTE: Yes, I understand this isn't the best place to put this check.
		// Technically this check should occur after the duplicate force check,
		// but the code this is based on has this check here, so the prediction
		// must do the same.
		if(num_forces >= MAX_FORCES)
		{
			VectorClear(motion->velocity);

			motion->time += time;
			return qfalse;
		}

		// Check if the contacted surface was previously encountered
		for(first = 0; first < num_forces; first++)
		{
			if(DotProduct(trace.plane.normal, force[first]) > 0.99)
				break;
		}

		// If the surface was in fact hit before, just nudge the velocity
		// a bit by the surface normal force and try again
		if(first < num_forces)
		{
			VectorAdd(motion->velocity, trace.plane.normal, motion->velocity);
			continue;
		}

		// Add this new surface normal force to the force normal list
		VectorCopy(trace.plane.normal, force[num_forces]);
		num_forces++;

		// Find a plane that the current velocity hits
		for(first = 0; first < num_forces; first++)
		{
			// Ignore forces that clearly do not oppose the velocity
			if(DotProduct(force[first], motion->velocity) >= 0.1)
				continue;

			// Clip the velocity along the plane normal to the force
			PM_ClipVelocity(motion->velocity, force[first], attempted_velocity, OVERCLIP);
			if(use_gravity)
				PM_ClipVelocity(final_velocity, force[first], attempted_final_velocity, OVERCLIP);

			// Try to find a second force that opposes the attempted velocity
			for(second = 0; second < num_forces; second++)
			{
				// Ignore forces that have already been processed
				if(first == second)
					continue;

				// Ignore forces that clearly do not oppose the velocity
				if(DotProduct(force[second], attempted_velocity) >= 0.1)
					continue;

				// Also clip the velocity against the plane normal to this force
				PM_ClipVelocity(attempted_velocity, force[second], attempted_velocity, OVERCLIP);
				if(use_gravity)
					PM_ClipVelocity(attempted_final_velocity, force[second], attempted_final_velocity, OVERCLIP);

				// Ignore this force if the effect it had on the velocity was that
				// it counteracted the first force
				if(DotProduct(attempted_velocity, force[first]) >= 0.0)
					continue;

				// Compute the direction of the line intersection of the planes
				// of these forces
				CrossProduct(force[first], force[second], intersect_dir);
				VectorNormalize(intersect_dir);

				// Project the velocity onto the intersection
				speed = DotProduct(intersect_dir, motion->velocity);
				VectorScale(intersect_dir, speed, attempted_velocity);

				if(use_gravity)
				{
					speed = DotProduct(intersect_dir, final_velocity);
					VectorScale(intersect_dir, speed, attempted_final_velocity);
				}

				// Test if a third force also obstructs the movement
				for(third = 0; third < num_forces; third++)
				{
					// Ignore forces that have already been processed
					if((first == third) || (second == third))
						continue;

					// Ignore forces that clearly do not oppose the velocity
					if(DotProduct(force[third], attempted_velocity) >= 0.1)
						continue;

					// A three-way force interesction generates a corner, so stop
					VectorClear(motion->velocity);
					motion->time += time;
					return time;
				}
			}

			// Try another move with the newly skewed velocity
			VectorCopy(attempted_velocity, motion->velocity);
			if(use_gravity)
				VectorCopy(attempted_final_velocity, final_velocity);
			break;
		}
	}

	// Account for any unpredicted time
	if(time > 0.0)
		motion->time += time;

	// After all movement has been done, use the final velocity with the full
	// amount of gravity added if that value was computed
	if(use_gravity)
		VectorCopy(final_velocity, motion->velocity);

	// When the motion state had a movement timer set, do not change the
	// starting velocity at all (except for gravity)
	//
	// FIXME: There is a FIXME in bg_slidemove.c asking whether this is
	// the right thing to do or not.  Let me tell you, this is *NOT* the
	// right thing to do.  That code should get changed, and this should
	// be changed to reflect it.
	if(motion->time < motion->move_time)
		VectorCopy(start_velocity, motion->velocity);

	// In form the caller of whether or not the slide prediction was obstacle-free
	return no_obstacles;
}

/*
===========================
EntityMotionPredictMoveStep

Predicts the entity's motion state "time" seconds
in the future using movement that checks for steps
to move up or down and otherwise uses sliding
physics.  The motion state is modified to match the
future prediction.

NOTE: This function is based on PM_SlideMove() in
bg_slidemove.c.
===========================
*/
void EntityMotionPredictMoveStep(gentity_t * ent, motion_state_t * motion, float time)
{
	int             entnum;
	float           motion_time, max_step_size;
	vec3_t          origin, velocity, step_end;
	trace_t         trace;

	// The "standing still on the ground" case is relatively common so check for it
	if((VectorCompare(motion->velocity, vec3_origin)) && (motion->physics.type == PHYS_GROUND))
	{
		// Obviously nothing to predict so just account for the time
		motion->time += time;
		return;
	}

	// Cache the motion state's starting position, velocity, and time(s)
	// in case more than one slide move prediction is needed
	VectorCopy(motion->origin, origin);
	VectorCopy(motion->velocity, velocity);
	motion_time = motion->time;

	// The vast majority of the time nothing interesting happens from slide movement
	if(EntityMotionPredictMoveSlide(ent, motion, time))
		return;

	// Cache the entity number and trace mask of this entity
	entnum = ent - g_entities;

	// The entity might not even try to step up when it hit an obstacle if the
	// entity was moving upwards at the time
	if(motion->velocity[2] > 0.0)
	{
		// Look for a steppable surface below the entity's starting position
		VectorSet(step_end, origin[0], origin[1], origin[2] - STEPSIZE);
		trap_Trace(&trace, origin, motion->mins, motion->maxs, step_end, entnum, motion->clip_mask);

		// If no ground was found or only steep ground, do not try to step up
		//
		// NOTE: The 0.7 is supposed to be MIN_WALK_NORMAL, but the original code
		// in PM_StepSlideMove() in bg_slidemove.c hardcodes the value.  So this
		// code is just bug compliant.
		if((trace.fraction >= 1.0) || (trace.plane.normal[2] < 0.7))
		{
			return;
		}
	}

	// Determine the highest step up the entity could take
	VectorSet(step_end, origin[0], origin[1], origin[2] + STEPSIZE);
	trap_Trace(&trace, origin, motion->mins, motion->maxs, step_end, entnum, motion->clip_mask);
	if(trace.allsolid)
		return;
	max_step_size = trace.endpos[2] - origin[2];

	// Restore the motion state to its original state, but in the stepped up position
	VectorCopy(trace.endpos, motion->origin);
	VectorCopy(velocity, motion->velocity);
	motion->time = motion_time;

	// Do slide movement from the stepped up position
	EntityMotionPredictMoveSlide(ent, motion, time);

	// Find the step surface below the entity's new position
	VectorSet(step_end, motion->origin[0], motion->origin[1], motion->origin[2] - max_step_size);
	trap_Trace(&trace, motion->origin, motion->mins, motion->maxs, step_end, entnum, motion->clip_mask);

	// Force the entity back down as much of the step height taken as allowed
	if(!trace.allsolid)
		VectorCopy(trace.endpos, motion->origin);

	// Clip the motion velocity to the ground surface if one was found
	if(trace.fraction < 1.0)
		PM_ClipVelocity(motion->velocity, trace.plane.normal, motion->velocity, OVERCLIP);
}

/*
========================
EntityMotionPredictFrame

Predicts one frame of an entity's motion, lasting "time",
given the entity's current motion state.  The new position,
velocity, timestamps, and global bounding box are stored
in the inputted motion state.

"cmd" is a simplified version of the entity's last user
command, containing descriptions of the last forward, right,
and up movement commands.

"ground_axies" represent the forward, right and up axies of
movement for ground movement.  "air_axies" are the air
counterparts (which in particular, allow the forward movement
axis to have a height).

This function returns the number of seconds actually predicted
(which will be zero if an error occurred).
========================
*/
float EntityMotionPredictFrame(gentity_t * ent, motion_state_t * motion, float time,
							   usercmd_t * cmd, vec3_t * ground_axies, vec3_t * air_axies)
{
	float           desired_speed;
	vec3_t          desired_dir;
	vec3_t          start, shift;

	// Guarantee that the entity has a legal type of physics
	if((motion->physics.type != PHYS_GROUND) &&
	   (motion->physics.type != PHYS_GRAVITY) && (motion->physics.type != PHYS_WATER) && (motion->physics.type != PHYS_FLIGHT))
	{
		return 0.0;
	}

	// Check for acceleration and physics style changes from jumping
	if((motion->up_move >= 10) && (motion->physics.type == PHYS_GROUND))
	{
		motion->physics.type = PHYS_GRAVITY;
		motion->physics.walking = qfalse;
		VectorClear(motion->physics.ground);

		motion->velocity[2] = JUMP_VELOCITY;
	}

	// Cache the starting position so the actual position shift can be detected
	VectorCopy(motion->origin, start);

	// Prediction velocity loss due to friction
	EntityMotionPredictVelocityFriction(ent, motion, time);

	// Compute the desired movement speed and direction
	if((motion->physics.type == PHYS_FLIGHT) || (motion->physics.type == PHYS_WATER))
		desired_speed = MoveCmdToDesiredDir(cmd, air_axies, &motion->physics,
											motion->max_speed, motion->water_level, desired_dir);
	else
		desired_speed = MoveCmdToDesiredDir(cmd, ground_axies, &motion->physics,
											motion->max_speed, motion->water_level, desired_dir);

	// Predict acceleration from trying to reach the desired move direction and speed
	EntityMotionPredictVelocityAccelerate(ent, motion, time, desired_dir, desired_speed);

	// Predict velocity changes due to standing on sloped surfaces
	EntityMotionPredictVelocitySlope(ent, motion);

	// Predict movement given the new velocity
	//
	// NOTE: Obviously entities can't walk up steps while they are at least
	// chest deep in water, so only simple slide movement is checked for
	// water physics.
	//
	// NOTE: No movement is applied to stationary entities on the ground.  It
	// turns out that trying to apply this can generate minor changes in a the
	// entity's Z position coordinate, which explains why the server's version
	// of the code doesn't do this either
	if(motion->physics.type == PHYS_WATER)
	{
		EntityMotionPredictMoveSlide(ent, motion, time);
	}
	else if(motion->physics.type != PHYS_GROUND || motion->velocity[0] || motion->velocity[1])
	{
		EntityMotionPredictMoveStep(ent, motion, time);
	}

	// Adjust the absolute bounding boxes by the motion origin's shift
	VectorSubtract(motion->origin, start, shift);
	VectorAdd(motion->absmin, shift, motion->absmin);
	VectorAdd(motion->absmax, shift, motion->absmax);

	// Snap the velocity after all movement has been applied
	SnapVector(motion->velocity);

	// Recompute the motion state's potentially cached data (such as physics and water)
	EntityMotionStateUpdateCachedData(ent, motion);

	// The full block of time was estimated
	return time;
}

/*
===================
EntityMotionPredict

Predicts an entity's motion "time" seconds in the future, given
the entity's current motion state.  The new position, velocity,
timestamps, and global bounding box are stored in the inputted
motion state.
===================
*/
void EntityMotionPredict(gentity_t * ent, motion_state_t * motion, float time)
{
	float           granularity, min_granularity, estimated;
	vec3_t          view, ground_axies[3], air_axies[3];
	usercmd_t       cmd;

	// Apply simple trajectory prediction for non-clients
	//
	// NOTE: This is the PHYS_TRAJECTORY case.
	if(!ent->client)
	{
		EntityMotionPredictTrajectory(ent, motion, time);
		return;
	}

	// Cache the ground and air movement axies
	ViewAnglesToMoveAxies(motion->view, ground_axies, PHYS_GROUND);
	ViewAnglesToMoveAxies(motion->view, air_axies, PHYS_FLIGHT);

	// Create a simple user command from the motion movement commands
	memset(&cmd, 0, sizeof(usercmd_t));
	cmd.forwardmove = motion->forward_move;
	cmd.rightmove = motion->right_move;
	cmd.upmove = motion->up_move;

	// The ideal prediction granularity is the entity's update rate
	granularity = EntityMotionUpdateRate(ent);

	// Physics granularity never exceeds this value
	if(granularity > PREDICT_GRANULARITY_MAX)
		granularity = PREDICT_GRANULARITY_MAX;

	// Determine the minimum granularity required to process the requested interval
	// in a reasonable amount of processor time
	min_granularity = time / PREDICT_FRAMES_MAX;
	if(granularity < min_granularity)
		granularity = min_granularity;

	// Independantly predict each frame of motion
	while(time > 0.0)
	{
		// Reduce the granularity of the final frame if necessary
		if(granularity > time)
			granularity = time;

		// Try to predict one frame
		estimated = EntityMotionPredictFrame(ent, motion, granularity, &cmd, ground_axies, air_axies);

		// Abort if an error occurred
		if(estimated < granularity)
			break;

		// Another block of time was estimated
		time -= estimated;
	}
}

/*
=====================
BotMotionFutureUpdate

This function updates the bot's prediction of
its motion state for the upcoming server frame
(bs->future) if necessary.  "Necessary" means
it hasn't been updated yet or critical information
has changed since it was last updated (ie. the
bot changed its commands.)

NOTE: You may wonder why the bot even needs a
perception of its own position next server frame.
It turns out that there are some significant
problems with parallax view if the bot doesn't
reference future motion state data when deciding
some important features like attacking (although
that isn't the only code that benefits from this).
Since the server processes movement data before
shot data, parallax view issues generate a 10% to
20% drop in weapon accuracies.  This code makes it
possible to correct that error.
=====================
*/
void BotMotionFutureUpdate(bot_state_t * bs)
{
	// The future motion state does not need to be updated (has already been) if:
	//
	// - The prediction time is less than a millisecond off from anticipated
	// - The predicted view command matches the current view command
	// - The predicted movement commands match the current movement commands
	if((fabs(bs->future.time - bs->command_time) < .001) &&
	   (VectorCompare(bs->now.view, bs->future.view)) &&
	   (bs->now.forward_move == bs->future.forward_move) &&
	   (bs->now.right_move == bs->future.right_move) && (bs->now.up_move == bs->future.up_move))
	{
		return;
	}

	// One of the previous conditions must have failed-- either the prediction is
	// out of date or the bot is sending new commands-- so the bot must make a new
	// future prediction

	// Start with the current motion state
	memcpy(&bs->future, &bs->now, sizeof(motion_state_t));

	// Predict that state at the next server frame
	EntityMotionPredict(bs->ent, &bs->future, bs->command_time - bs->now.time);


	// Compute the eye position in the future motion state
	//
	// FIXME: Move viewheight and eye coordinates into the motion state.
	VectorCopy(bs->future.origin, bs->eye_future);
	SnapVector(bs->eye_future);
	bs->eye_future[2] += bs->ps->viewheight;
}

#ifdef DEBUG_AI
/*
=====================
PredictDebugEntityAdd

Adds a new motion state prediction to the
cached list so the prediction can be
tested later.
=====================
*/
void PredictDebugEntityAdd(gentity_t * ent, float time_lapse, motion_state_t * motion)
{
	predict_debug_t *predicted;

	// Don't exceed the maximum allowed array size
	if(predict_debug_size >= DEBUG_PREDICT_MAX)
		return;

	// Look up the next prediction state
	predicted = &predict_debug_list[predict_debug_size++];

	// Fill out the prediction data
	predicted->ent = ent;
	predicted->time_lapse = time_lapse;
	memcpy(&predicted->motion, motion, sizeof(motion_state_t));
}

/*
=====================
PredictDebugEntityNow

If the prediction debug variable has
been turned on, this function will predict
the inputted entity's motion state in the
future and then add it to the list of
predictions to check.
=====================
*/
void PredictDebugEntityNow(gentity_t * ent)
{
	float           old_time;
	motion_state_t  motion;

	// Do not test any predictions if testing has been deactivated
	if(bot_debug_predict_time.value <= 0.0)
		return;

	// Do not test predictions on bots
	if(ent->r.svFlags & SVF_BOT)
		return;

	// Only test predictions every so often
	if(server_time < last_predict_time + 0.50)
		return;

	// Look up the latest motion state
	EntityMotionStateNow(ent, &motion);
	old_time = motion.time;
	EntityMotionPredict(ent, &motion, bot_debug_predict_time.value);

	// Store a copy of the predicted state so it can be accuracy tested later
	PredictDebugEntityAdd(ent, motion.time - old_time, &motion);

	// Remember the last time this kind of prediction was recorded
	last_predict_time = server_time;
}

/*
======================
PredictDebugCheckEntry

Checks one cached prediction entry to see
if the predicted time has occurred.  If so,
it compares the prediction to reality to see
how good the prediction was, and then returns
true.  Otherwise returns false.
======================
*/
qboolean PredictDebugCheckEntry(predict_debug_t * predicted)
{
	vec3_t          pos_error, vel_error;
	float           xy_err, z_err;
	motion_state_t  actual;

	// Look up the actual motion state at the predicted time
	EntityMotionStateTime(predicted->ent, &actual, predicted->motion.time);

	// If the predicted time is sufficiently larger than the actual time,
	// the predicted event has not yet occurred
	if(actual.time + 1e-5 < predicted->motion.time)
		return qfalse;

	// Compute the XY origin error and the Z error
	VectorSubtract(predicted->motion.origin, actual.origin, pos_error);
	z_err = pos_error[2];
	pos_error[2] = 0.0;
	xy_err = VectorLength(pos_error);

	// Compute the velocity error
	VectorSubtract(predicted->motion.velocity, actual.velocity, vel_error);

	// Print nothing (but successfully compare the entry) if no real error was detected
	//
	// NOTE: The velocity error accounts for up to two floating point rounding errors
	if((fabs(xy_err) <= 2.0) &&
	   (fabs(z_err) <= 2.0) && (fabs(vel_error[0]) <= 2.0) && (fabs(vel_error[1]) <= 2.0) && (fabs(vel_error[2]) <= 2.0))
	{
		return qtrue;
	}

	// Print out the origin prediction errors for this entity's state along
	// with amount of time predicted ahead
	G_Printf("%s %.3f (+%.3f) Error: Pos: XY: %.2f, Z: %.2f; Vel: (%.2f, %.2f, %.2f)\n",
			 EntityNameFast(predicted->ent), predicted->motion.time, predicted->time_lapse,
			 xy_err, z_err, vel_error[0], vel_error[1], vel_error[2]);

	// This prediction has been tested
	return qtrue;
}

/*
=================
PredictDebugCheck

Checks each entry in the prediction cache to see
if the predicted time occurs now.  If so, compares
the prediction against reality to see how incorrect
the prediction was.

NOTE: This function can only check predictions on
entities that keep a history of their motion data
(currently just players).
=================
*/
void PredictDebugCheck(void)
{
	int             i;
	predict_debug_t *predicted;

	// Search the prediction array for predictions whose reality has occurred
	i = 0;
	while(i < predict_debug_size)
	{
		// Look up the next entry to predict
		predicted = &predict_debug_list[i];

		// If prediction succeeded, copy the last list entry over this one ...
		if(PredictDebugCheckEntry(predicted))
			memcpy(predicted, &predict_debug_list[--predict_debug_size], sizeof(predict_debug_t));

		// ... Otherwise check the next entry
		else
			i++;
	}
}
#endif
