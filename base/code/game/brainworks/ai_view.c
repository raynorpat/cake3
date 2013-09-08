// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_view.c
 *
 * Functions that the bot uses to modify its view angles
 *****************************************************************************/

#include "ai_main.h"
#include "ai_vars.h"
#include "ai_view.h"

#include "ai_command.h"
#include "ai_entity.h"

// Number of seconds that must elapse after a change before the bot can
// detect another change
//
// NOTE: This value looks similar to bs->react_time, the basic reaction time
// which is in the .1 to .3 second range.  It is NOT used the same way, however.
// This value is used to smooth potential rapid changes in the view state, so
// the bot doesn't accidentally detect many more changes than actually occurred.
// Lowering this value for higher skilled bots would actually make them perform
// worse if their reaction time were low enough.  This isn't "reaction" in the
// sense of the word used elsewhere in this code.
//
// FIXME: Should this be a runtime modifiable variable?
#define VIEW_CHANGE_REACT_TIME 0.2


/*
================
BotViewReactTime

Returns the amount of time it takes the bot to
react in view changes.
================
*/
float BotViewReactTime(bot_state_t * bs)
{
#ifdef DEBUG_AI
	// Perfect and flawless aiming bots immediately react to any changes
	if(bs->debug_flags & (BOT_DEBUG_MAKE_VIEW_PERFECT | BOT_DEBUG_MAKE_VIEW_FLAWLESS))
		return 0.0;
#endif

	// Use the current reaction time
	return bs->react_time;
}

/*
=============
ViewAxisReset

Reset a view state axis.  The starting view
state position is "angle".  Speeds and errors
are all reset to zero.
=============
*/
void ViewAxisReset(view_axis_t * view, float angle)
{
	// Use the inputted angle for position with no error
	view->angle.real = angle;
	view->angle.error = angle;

	// Assume zero velocity, but the actual velocity is completely unknown
	view->speed.real = 0.0;
	view->speed.error = 0.0;

	// No error factor and default difficulty
	view->max_error_factor = 0.0;
	view->error_factor = 0.0;

	// Timestamp the data now
	view->time = server_time;
}

/*
=========
ViewReset

Reset all axies in a view state array to use
the inputted angles with no error and no speeds.
=========
*/
void ViewReset(view_axis_t * view, vec3_t angles)
{
	int             i;

	// Reset each view axis independantly
	for(i = PITCH; i <= YAW; i++)
		ViewAxisReset(&view[i], angles[i]);
}

/*
==============
ViewAnglesReal

A view axis array's real angles.
==============
*/
void ViewAnglesReal(view_axis_t * view, vec3_t angles)
{
	int             i;

	// Extract the pitch and yaw angles
	for(i = PITCH; i <= YAW; i++)
		angles[i] = view[i].angle.real;

	// Reset the roll angle just to be safe
	angles[ROLL] = 0.0;
}

/*
===================
ViewAnglesPerceived

A view axis array's angles as the bot perceives them.
(ie. with error values included)
===================
*/
void ViewAnglesPerceived(view_axis_t * view, vec3_t angles)
{
	int             i;

	// Extract each axis' angle independantly
	for(i = PITCH; i <= YAW; i++)
		angles[i] = view[i].angle.error;

	// Reset the roll angle just to be safe
	angles[ROLL] = 0.0;
}

/*
===================
DataPerceiveCorrect

Given an offset error value (difference from actual value to
perceived value), this function applies one frame of error
correction and returns the new error offset.  If the error
value is "e", this function selects a new error value from
the interval (-e, +e) with uniform distribution.

My research implies that this is pretty similar to how humans
correct small error values when they have narrowed an unknown
variable down to a small enough range.  Humans seem to use
the following algorithm for refining estimates.  For example,
finding the dictionary page that contains a given word.

1) Make a reasonable estimate given the human's understanding
of the situation.  For example, even though W is the 23rd letter
of the alphabet, humans don't look at the (23/26) * MAX_PAGES
page of the dictionary when looking up W words, simply because
they know the X-Y-Z sections are so short.  This indexing is
similar to the Interpolation Search algorithm.  This result is
compared to the actual value (ie. is the guess too high or too
low?) and this value is fixed as either an upper or lower
bound.  In other words, you mark this page with your finger.

2) Possibly make one or two more reasonable estimates to determine
both an upper and lower bound that the data must reside in.  At
this point the human knows that lower < value < upper.  He or she
knows the precise values of "lower" and "upper" but NOT of "value".

3) Pick a random value in the interval (lower, upper) and compare
it to "value".  This selection replaces either the lower or upper
bound as necessary.  Repeat until the selection equals "value".

This might seem unintuitive, but humans don't actually use binary
search to narrow down their errors when the range gets sufficiently
small.  Perhaps it takes too much time to roughly estimate the
middle.  In practice people will flip through maybe 10 pages at
a time, or 1 page at a time, or just pick something and see.  It
will take more iterations to converge than binary search would but--
and this is crucial-- it takes less time overall than computing the
midpoint at each iteration.

To be precise, however, humans don't pick a value in the interval
(lower, upper) if their last guess was "lower".  They will almost
always pick a value in (j*lower, k*upper), 1 >= j >= k >= 0.  From
my testing of what appears most realistic, j = 1 and k < 1.  When
k = 1 and the absolute value of the error is small (ie. the bot is
almost aimed correctly), the bot will pathologically attempt to
correct its miniscule error, but half the time will incur a sign
change in the error.  A sign change means the bot must do a relatively
large amount of aim acceleration and deceleration so that it's
aiming in roughly the same place, but at a different speed.  This
creates some extremely jittery aiming, even against stationary targets.

So this code assumes k=1 and j=0.  In other words, if the correct
value is C and the error delta is E, the new value is selected from
the interval (C, C+E) or (C+E, C) depending on the sign of E.  But the
sign of the error will never change.  I haven't tested this, but I
suspect the theoretically optimal value of j is greater than zero,
maybe around 0.3.

Of course if the code were implemented with j>0, there are other
problems.  In particular, it needs to track both the upper and lower
boundaries, which correlate with two previous "guesses", or error
values.  So if the last guess was off by -e and the last guess that
was too positive was f, the next correction would select from (-e, j*f).
So the first problem is that two errors must be tracked.

The other problem is that even such a selection is not what humans
do.  Lets say the old boundaries were (-10, +20*j) where j = 0.3 and
the human guesses -1.  Humans can generally tell that -1 is pretty
darn close to the actual value of 0, so it's unlikely they would
check from a uniform distribution between (-1, +6) for the next
iteration.  The next value would probably be in the (-1, +3) range.
Of course if they guessed -7, it's reasonable to expect they would
still check (-7, +6).  Humans implicitly modify both boundaries on
each guess.

So even if I wrote this algorithm to handle j > 0, I would simplify
things such that the same error value would be used for both interval
boundaries.  In other words, an error delta of E<0 would be corrected
to (-E, +E*j) and E>0 would go to (-E*j, +E).

NOTE: Functions like this really make me wish C had the inline
keyword defined.  It's one line but it demands a wealth of
comments describing why that one line is correct.
===================
*/
float DataPerceiveCorrect(float estimate_offset)
{
	// Pick a new value in (0, +error) or (-error, 0)
	//
	// NOTE: It doesn't matter what the sign of error is; the random
	// function will preserve it.
	return random() * estimate_offset;
}

/*
===========
ViewCorrect

Applies one correction to each axis in a view state.  See
DataPerceiveCorrect() for more information on the actual
correction algorithm.  The more times this function is called,
the more correct the estimate becomes.
===========
*/
void ViewCorrect(view_axis_t * view)
{
	int             i;
	float           offset;

	// Independantly converge each view axis' error
	for(i = PITCH; i <= YAW; i++)
	{
		// Correct the offset between the error and real angles, then
		// convert back to non-offset values
		offset = AngleDelta(view[i].angle.error, view[i].angle.real);
		view[i].angle.error = AngleNormalize180(view[i].angle.real + DataPerceiveCorrect(offset));

		// Correct the offset between the error and real speeds
		offset = view[i].speed.error - view[i].speed.real;
		view[i].speed.error = view[i].speed.real + DataPerceiveCorrect(offset);
	}
}

/*
=======================
BotViewIdealErrorSelect

Recomputes the amount of error to apply to ideal
view choices for the next block of time.
=======================
*/
void BotViewIdealErrorSelect(bot_state_t * bs)
{
	int             i;
	float           max_error;

	// Compute the maximum percentage of additional error allowed for the ideal view state
	max_error = bot_view_ideal_error_max.value -
		bs->aim_accuracy * (bot_view_ideal_error_max.value - bot_view_ideal_error_min.value);
	if(max_error < 0.0)
		max_error = 0.0;

#ifdef DEBUG_AI
	// Perfect and flawless aiming means the bot never makes any view errors
	if(bs->debug_flags & (BOT_DEBUG_MAKE_VIEW_PERFECT | BOT_DEBUG_MAKE_VIEW_FLAWLESS))
		max_error = 0.0;
#endif

	// Select new error factors for this reaction frame
	for(i = PITCH; i <= YAW; i++)
	{
		// Save the maximum allowed error factor
		bs->view_ideal_last[i].max_error_factor = bs->view_ideal_next[i].max_error_factor = max_error;

		// The ideal view state must use the same random error factor for both frames
		//
		// NOTE: The last frame's error factor currently isn't used, but it could be.
		// It's updated at the same time given the logic, "This is what the error factor
		// would have been if a notable ideal view shift hadn't occurred."
		bs->view_ideal_last[i].error_factor = bs->view_ideal_next[i].error_factor = crandom() * max_error;
	}
}

/*
===================
BotViewCorrectIdeal

Correct the bot's understand of where it
should ideally aim.  See ViewCorrect()
for more information.
===================
*/
void BotViewCorrectIdeal(bot_state_t * bs)
{
	int             i, corrections;
	float           delay, elapsed;

	// Determine how long to wait between corrections
	delay = bs->react_time * bot_view_ideal_correct_factor.value;
	if(delay <= 0.0)
		delay = 0.100;

	// Determine how many corrections to apply
	//
	// NOTE: The error update time is set to the last time the update
	// was applied, not necessarily the server time.
	elapsed = server_time - bs->view_ideal_error_time;
	corrections = elapsed / delay;
	if(corrections <= 0)
		return;
	bs->view_ideal_error_time += corrections * delay;

	// Both the old and new ideal aim states must get corrected so the bot
	// estimates its aim based on what its last ideal aim state would have
	// been at the current time.
	for(i = 0; i < corrections; i++)
	{
		ViewCorrect(bs->view_ideal_last);
		ViewCorrect(bs->view_ideal_next);
	}

	// Select new error values because enough time has passed since last selection
	BotViewIdealErrorSelect(bs);
}

/*
=======================
BotViewActualErrorSelect

Recomputes the amount of error to apply to actual
view choices for the next block of time.
=======================
*/
void BotViewActualErrorSelect(bot_state_t * bs)
{
	int             i;
	float           max_error;

	// Compute the maximum percentage of additional error allowed for the actual view state
	max_error = bot_view_actual_error_max.value -
		bs->aim_accuracy * (bot_view_actual_error_max.value - bot_view_actual_error_min.value);
	if(max_error < 0.0)
		max_error = 0.0;

#ifdef DEBUG_AI
	// Perfect and flawless aiming means the bot never makes any view errors
	if(bs->debug_flags & (BOT_DEBUG_MAKE_VIEW_PERFECT | BOT_DEBUG_MAKE_VIEW_FLAWLESS))
		max_error = 0.0;
#endif

	// Select a new error factor for each axis of the bot's actual view changes
	for(i = PITCH; i <= YAW; i++)
	{
		bs->view_now[i].max_error_factor = max_error;
		bs->view_now[i].error_factor = crandom() * max_error;
	}
}

/*
====================
BotViewCorrectActual

Correct the bot's understanding of where
it's actually aiming.  See ViewCorrect()
for more information.
====================
*/
void BotViewCorrectActual(bot_state_t * bs)
{
	int             i, corrections;
	float           delay, elapsed;

	// Determine how long to wait between corrections
	delay = bs->react_time * bot_view_actual_correct_factor.value;
	if(delay <= 0.0)
		delay = 0.100;

	// Determine how many corrections to apply
	//
	// NOTE: The error update time is set to the last time the update
	elapsed = server_time - bs->view_actual_error_time;
	corrections = elapsed / delay;
	if(corrections <= 0)
		return;
	bs->view_actual_error_time += corrections * delay;

	// Correct the bot's actual view
	for(i = 0; i < corrections; i++)
		ViewCorrect(bs->view_now);

	// Select new error values because enough time has passed since last selection
	BotViewActualErrorSelect(bs);
}

/*
=================
ViewSpeedsChanged

This function accepts inputs of the old angular
speeds for pitch and yaw and updated new speeds.
It checks for each axis whether a significant speed
change occurred and returns a bitmap of the results.
In particular, (1<<PITCH) will be true if change was
detected and false if not.  The (1<<YAW) bit is set
similarly.
=================
*/
int ViewSpeedsChanged(vec3_t old_speed, vec3_t new_speed)
{
	int             i, changes;

	// No changes have been detected so far
	changes = 0x0000;

	// Check each view axis in turn
	for(i = PITCH; i <= YAW; i++)
	{
		// Detect change if the speed changed to or from zero
		//
		// NOTE: This sign test is written to ensure that change won't
		// accidently be detected when both the old and new speeds are zero.
		if((old_speed[i] > 0.0) ^ (new_speed[i] > 0.0) || (old_speed[i] == 0.0) ^ (new_speed[i] == 0.0))
		{
			changes |= (1 << i);
		}
	}

	// Return the bitmap of detected changes
	return changes;
}

/*
===================
ViewInterpolateAxis

Given a view axis interplation pair ("last" and "next),
this function computes the interpolated view state at
the given input time "view_time" and stores the axis
information in "view".  The "reaction_time" is the
amount of time it takes to fully react to the change.
Past this many seconds, the interpolation will equal
the new axis.

NOTE: It's okay for the view time to refer to a time
in the past.  In that case, some data may be extrapolated
back in time.
===================
*/
void ViewInterpolateAxis(view_axis_t * last, view_axis_t * next, view_axis_t * view, float view_time, float reaction_time)
{
	float           adjust_time, elapsed_time;
	float           predictability, unpredictability;
	float           last_error_offset, next_error_offset;
	float           start, end;

	// Sanity check the reaction time
	if(reaction_time < 0.0)
		reaction_time = 0.0;

	// This is the amount of time the bot had to adjust to the last acceleration
	adjust_time = view_time - last->time;

	// This is how much time has passed since the current intended view updated
	elapsed_time = view_time - next->time;

	// Determine how well the view state has adjusted to the last velocity change
	if(adjust_time >= reaction_time)
		predictability = 1.0;
	else if(adjust_time <= 0.0)
		predictability = 0.0;
	else
		predictability = adjust_time / reaction_time;
	unpredictability = 1.0 - predictability;

	// Compute where the bot would want to aim at this time for the old and new view states
	start = last->angle.real + adjust_time * last->speed.real;
	end = next->angle.real + elapsed_time * next->speed.real;

	// Make sure the angle "turns" in the right direction (ie. -180 <= end-start <= 180)
	start = AngleNormalize360(start);
	end = AngleNormalize360(end);
	if(end - start > 180)
		end -= 360;
	if(start - end > 180)
		start -= 360;

	// Shift intended angle closer to the next angles for predicted motion
	// and closer to the last angles for unpredicted motion.
	view->angle.real = predictability * end + unpredictability * start;
	view->angle.real = AngleNormalize180(view->angle.real);

	// Translate the error angles to offsets from the real angles
	last_error_offset = AngleDelta(last->angle.error, start);
	next_error_offset = AngleDelta(next->angle.error, end);

	// Interpolate the position errors, offset from the actual position
	view->angle.error = view->angle.real + predictability * next_error_offset + unpredictability * last_error_offset;
	view->angle.error = AngleNormalize180(view->angle.error);

	// Average the speeds together
	view->speed.real = predictability * next->speed.real + unpredictability * last->speed.real;
	view->speed.error = predictability * next->speed.error + unpredictability * last->speed.error;

	// Interpolate maximum error
	view->error_factor = predictability * next->max_error_factor + unpredictability * last->max_error_factor;

	// Interpolate error factor
	view->error_factor = predictability * next->error_factor + unpredictability * last->error_factor;

	// Set the view axis' timestamp to the interpolation time
	view->time = view_time;
}

/*
====================
ViewInterpAxisUpdate

Update one axis of an interpolated view state pair with
the new inputted location "angle".  "speed" is an estimate
of the view location's speed.  This speed is only used if
the actual speed cannot be computed differentially. "displace"
is the angular distance from angle to the location's reference
point."view_time" is the timestamp of the inputted angle,
"reaction_time" is the amount of time it takes the bot to
fully adjust to a change in view states. The "changed" integer
is zero if there is nothing unexpected about this update,
positive if the target changed in an unexpected way, and
negative if a reset occurred (ie. the bot decided to aim at a
totally different target from last frame).

In addition to differentially computed angular speed and
updating position, this function also resets or updates
the error values for position and speed.  The initial ideal
error (ie. selected view location) is proportional to the
displacement between the view angles and the nearest
reference point (generally a target moving towards the ideal
location). In other words, The further the ideal view
location is from a reference point, the harder it is to
judge.  When not reset, the error position is incremented by
error speed times time.

Similarly, while the ideal view angles have an associated
angular speed, the selected view location has an estimated
(or error) speed.  Just as the bot needs to aim at position A
and chooses to aim at A', the aim position A is moving at
speed S and the bot thinks that A' is moving at speed S'.
The error in speed is proportionate to the view location's
estimated speed, just like the error in position is
proportiate to the reference point's displacement.  When not
reset, the error speed remains constant (at least until the
next error correction).

Recall that the intended view state is actually two
view states, an old one and a new one.  These states
are interpolated together using their view_axis->time
values.  Whenever a noticable speed change is detected,
the current interpolated view state is cached in the
old view state, and the updated view state is saved in
the new state.

The view state will also be cached if the intended view
requires a reset (for example, when the purpose for aiming
changes).  During a reset, the speeds are also reset.

NOTE: This function differentially computes angular speed.
If a significant change occurs, the currently understood
view state overwrites the old "last" state, and is timestamped
at the current data time.  This means that if this function
is called twice with the same data time stamp, and the last
state gets updated, the differential speed for the second
function call might be impossible to compute.  It's possible
to fix this by caching the data of course, but it's not
clear if that's really worth the space and effort.  It
seems simpler to decide on exactly one point to view each
data frame and view it once.  If this paradigm changes, however,
this code might need to be updated.  At least, the view state
could have better results for rapidly accelerating targets.
====================
*/
void ViewInterpAxisUpdate(view_axis_t * last, view_axis_t * next,
						  float angle, float speed, float displace, float view_time, float reaction_time, int changed)
{
	float           time_change, max_offset, offset;

	// Speeds can't be caluclated during resets since the view axis
	// only has one data point, so estimate using the supplied angular speed
	if(changed < 0)
	{
		time_change = 0.0;
	}

	// Otherwise compute the speed differentially
	else
	{
		// Determine how much time has changed since the last update
		//
		// FIXME: Time elapsed should never be negative, but it's good
		// to be safe.  Should we flag an error if this block executes?
		time_change = view_time - next->time;
		if(time_change < 0.0)
			time_change = 0.0;

		// Sometimes updates aren't necessary
		//
		// NOTE: This block also covers instantaneous changes in position.
		// It still uses the last known speed in that case, since it's as
		// good as anything else, and probably within 5% of the actual speed.
		// Actually computing the speed for real zero time change case
		// requires caching a third view state and will only very rarely make
		// a difference (since most of the time only the sign of the speed is
		// required, and the speed will be computed for real in the next frame.)
		// Of course, doing so is impossible in the negative time change case,
		// although technically that case should never occur.
		if(!time_change)
			speed = next->speed.real;

		// Otherwise compute the differential speed
		else
			speed = AngleDelta(angle, next->angle.real) / time_change;
	}

	// If the view axis changed, cache the predicted view state at its last known time
	// and then update the error angle and speed.
	//
	// NOTE: Technically the data's timestamp isn't known; the last server frame timestamp
	// is just used as an estimate.  If this code had access to an entity pointer, the
	// EntityTimestamp() function would be a bit more correct.  But this code just knows
	// about angles.  (Really just "angle".)  There might not even BE an entity that the
	// bot is looking at.
	if(changed)
	{
		// Cache the last predicted view state before the change
		ViewInterpolateAxis(last, next, last, server_time, reaction_time);

		// Position error is proportional to the displacement between
		// the view and reference angles
		next->angle.error = AngleNormalize180(angle + next->error_factor * displace);

		// Speed error is proportionate to the view position's angular speed
		//
		// NOTE: This speed is probably the estimation of the reference point's
		// angular speed.
		next->speed.error = speed * (1 + next->error_factor);
	}

	// Otherwise just extrapolate the error position for this timestamp
	else
	{
		// Linearly extrapolate the position error at this time
		//
		// NOTE: Technically this won't be correct because a projection of a line
		// in cartesian coordinates onto spherical coordinates won't produce a
		// linear velocity on the unit sphere.  But if the time change is small
		// it should be close enough.
		next->angle.error += next->speed.error * time_change;

		// Determine the maximum allowed offset between the real and error positions
		max_offset = fabs(next->max_error_factor * displace);

		// Bound the error position if it is outside this margin
		//
		// NOTE: Because of the minor error in linear extrapolation, this check
		// is necessary to adjust the selection error in cases the view does not
		// reset.  For example, if the bot is aiming with an instant hit weapon
		// at a target, their selected position (error) should always coincide
		// with the reference, so the maximum allowed error is zero.  But the
		// linear extrapolation makes it clear that this might not be the case.
		// The error must be corrected.
		offset = AngleDelta(next->angle.error, next->angle.real);
		if(offset > max_offset)
			next->angle.error = AngleNormalize180(next->angle.real + max_offset);
		else if(offset < -max_offset)
			next->angle.error = AngleNormalize180(next->angle.real - max_offset);

		// Determine the maximum offset between real and error speeds
		max_offset = fabs(next->max_error_factor * speed);

		// Bound the speed if necessary
		offset = next->speed.error - next->speed.real;
		if(offset > max_offset)
			next->speed.error = next->speed.real + max_offset;
		else if(offset < -max_offset)
			next->speed.error = next->speed.real - max_offset;
	}

	// Update the lastest view state angle ...
	next->angle.real = AngleNormalize180(angle);

	// ... And speed
	next->speed.real = speed;

	// Set the timestamp
	next->time = view_time;
}

/*
===============
BotViewIdealNow

Computes the bot's ideal view state for the next server
frame by interpolating the last and next ideal view
states.  The interpolated ideal view state is saved in
the inputted view axis array "ideal".
===============
*/
void BotViewIdealNow(bot_state_t * bs, view_axis_t * ideal)
{
	int             i;
	float           reaction_time;

	// Interpolate each axis independantly for the time of the bot's next command
	reaction_time = BotViewReactTime(bs);
	for(i = PITCH; i <= YAW; i++)
	{
		ViewInterpolateAxis(&bs->view_ideal_last[i], &bs->view_ideal_next[i], &ideal[i], bs->command_time, reaction_time);
	}
}

/*
==================
BotViewIdealUpdate

Update the bot's ideal view state pair with new inputted
angles.  "view_speeds" is the estimated angular speed of
"view_angles".  "ref_angles" is the visual reference angles
nearest to the ideal view angles of "view_angles".

The "change" value is a bitmap that defines which axies have
detected change in some sort.  The (1<<PITCH) bit will be
set if the pitch axis had change, for example.  More
serious processing gets done when changes are detected in
the view states.  If the change value is negative (ie.
sign bit is 1), the update will be considered a reset.
A reset occurs when the bot chooses to aim at a completely
different target, and differs from normal changes (ie.
the enemy dodging) in that the bot expects a reset but
doesn't expect the normal changes.

After updating the view pair, this function then updates
the ideal view state's error values.  These error values
represent where the bot actually selects to view (whereas the
inputs and "real" values represent where it should ideally
view).  The selected angles are then stored in "view_angles",
so the caller has easy access to the selected view angles.

FIXME: Technically it's hard to detect an invisible target
in the first place, but once the target is detected, it's
easy to react to its changes.  This code simply isn't set up
to deal with this, however.  Whenever an invisible target
makes any change in motion, the bot will lose track of it
again.  In theory this should get fixed.
==================
*/
void BotViewIdealUpdate(bot_state_t * bs, vec3_t view_angles, vec3_t view_speeds, vec3_t ref_angles, int changes)
{
	int             i, change;
	view_axis_t     ideal[2];

	// Assume the aim location is stationary if no speeds were supplied
	if(!view_speeds)
		view_speeds = vec3_origin;

	// Assume the requested view state is the reference if no reference was supplied
	if(!ref_angles)
		ref_angles = view_angles;

	// Remember that a view reset occurred this command frame if necessary ...
	if(changes < 0)
	{
		// This is when a reset last occurred
		bs->view_ideal_reset_time = bs->command_time;

		// Reset the error correction time to the last processed server frame
		bs->view_ideal_error_time = server_time;
	}

	// ... Otherwise ignore changes that occurred too soon after another change
	else
	{
		for(i = PITCH; i <= YAW; i++)
		{
			if(bs->command_time - bs->view_ideal_last[i].time < VIEW_CHANGE_REACT_TIME)
				changes &= ~(1 << i);
		}
	}

	// Update the interpolated view state independantly for each axis
	for(i = PITCH; i <= YAW; i++)
	{
		// Determine what kind of change occurred on this axis (1 for unpredicted
		// change, -1 for view reset, 0 for no change)
		if(changes < 0)
			change = -1;
		else if(changes & (1 << i))
			change = 1;
		else
			change = 0;

		// Update one axis of the view state
		ViewInterpAxisUpdate(&bs->view_ideal_last[i],
							 &bs->view_ideal_next[i],
							 view_angles[i],
							 view_speeds[i],
							 AngleDelta(view_angles[i], ref_angles[i]), bs->command_time, BotViewReactTime(bs), change);
	}

	// Provide the caller with the selected view angles
	BotViewIdealNow(bs, ideal);
	ViewAnglesPerceived(ideal, view_angles);
}

/*
==============
ViewAxisModify

This function uses the "angle" and "speed" pair as a description of what
a view state should change to.  "skill" represents how good the view state
owner (ie. bot) is at aiming

This function then updates the axis of the inputted view axis "view" (for
example, bs->view_now-- where the bot is actually aiming) in a human-like
manner so that the view state represents where the bot is aiming at time
"time".  Each view state has both a location (angles) and velocity (speeds).
It's the bot's job to change its view state to match the selected view state,
both in angles and speed.  It's this function's job to determine how much to
accelerate or decelerate the aim velocity.

It is NOT ths function's job to determine where the bot intends to aim.
See BotAimSelect() in ai_aim.c for more information on that.  This
function just translates the selected view state to an actual view state.

While this function is used to change the bot's actual aim location, it
can also be used to predict the bot's aim a few milliseconds in the future.

The bot must determine the fastest way to change its view position and
velocity to match the target's angle and speed.  It turns out that
the fastest way to do this is to spend some initial period of time "Ta"
accelerating to reach the convergence location and some later period of
time "Td" decelerating to converge at the optimal speed.  All time after
that will be spent in zero acceleration, matching the target's speed and
position (at least until the target changes speed in a later frame).

It's worth noting that the initial acceleration might not be in the
direction of the target's starting position.  For example, suppose the
view state's starting angle and speed are both 0. Also suppose the
target's view angle is -10 (ten degrees to the left of the crosshair),
and speed is 50 (each frame the view anglemoves to the right by fifty
degrees).  If the acceleration is 5, the bot will clearly have to start
accelerating to the right (accel at +5, not -5).  Incidently, Td, the
time spent decelerating, will be 0 in this case.

Assume this starting information:

Pb: Initial position (angle) of bot
Pt: Initial position of target
P:  Pt - Pb (position difference, normalized to +/-180 degrees)
Vb: Initial velocity (angle change per second) of bot
Vt: Velocity of target, assumed to be constant
V:  Vt - Vb (velocity difference)
C:  Absolute value of maximum allowed change to Vb per second

The next objective of this function is to determine:

A:  The actual acceleration (either +C or -C)
Ta: Amount of time to spend accelerating (time spent adding A to Vb)
Td: Amount of time to spend decelerating (time spent subtracting A from Vb)

Using some basic algebra and a little calculus, the solutions to Ta
and Td are:

Td = sqrt(V^2 / 2 + A*P) / fabs(A)
Ta = V/A + Td

Determining A is a little trickier, but essentially A is selected so
that the square root has a well defined (non-imaginary) root.  This
table is the easiest way of determining of A should equal +C or -C

V^2 > 2C|P|? | V > 0? | P > 0? | A:
------------------------------------
     Yes     |  Yes   |  Yes   | +C
     Yes     |  Yes   |   No   | +C
------------------------------------
     Yes     |   No   |  Yes   | -C
     Yes     |   No   |   No   | -C
------------------------------------
      No     |  Yes   |  Yes   | +C
      No     |   No   |  Yes   | +C
------------------------------------
      No     |  Yes   |   No   | -C
      No     |   No   |   No   | -C

After the acceleration direction and times have been computed, the bot
must determine its new speed and corresponding angle.  Remember, the
first Ta seconds are spent accelerating the aim velocity, the next Td
seconds are spent decelerating, and any time after that is spent in
constant velocity.  Of course, the bot never spends more than "time"
seconds changing its view, so most of the time the bot will only
accelerate.

Of course, most bot don't have perfect aim.  Just because a bot
understands how it wants to accelerate its aim speed doesn't mean it
will do so correctly.  Every so often a bot will select an error factor
"e" representing how inaccurate its aiming will be for the next fraction
of a second.

When the bot tries to accelerate its view by A, the actual acceleration is:

  A * (1 + e)

So the velocity change will be:

  (Ta-Td)*A*(1 + e)

The bot thinks its velocity change will be this, however:

  (Ta-Td)*A

The change in velocity error is:

  -(Ta-Td)*A*e

(This value is negated because adding this value to the actual velocity
yields the bots estimation of its velocity).

Similarly, the bot's actual view angle won't match the estimated view
location, although computing it is much more difficult.  Remember, there
are three periods of different acceleration.  Given Ta seconds of
acceleration at rate A, Td seconds of acceleration at rate -A, Tc seconds
of 0 acceleration, and initial aim velocity V, the change in aim angle
is defined by:

  [Ta*V + Ta^2*A/2] + [Td*V + Td*(2Ta-Td)*A/2] + [Tc*V + Tc*(Ta-Td)*A]

Which can be rewritten as:

  AngChange(V,A) = V * (Ta+Td+Tc) + A * [Ta^2 + Td*(2Ta-Td) + 2Tc*(Ta-Td)] / 2

The coefficient of V equals T, the total input time, by definition.
It's useful to cache the coefficient of A for readability and speed,
so assign it to T'.

  T' = [Ta^2 + Td*(2Ta-Td) + 2Tc*(Ta-Td)] / 2
  AngChange(V,A) = V*T + A*T'

The actual view angle (new Pb) is:

  Pb + AngChange(Vb, A*(1+e))
  Pb + Vb*T + A*(1+e)*T'

Suppose that that Pe is the error position value (Pe is where the bot
currently thinks its aiming).  Ve is the analogous velocity error value.
This is the formula for the bot's new perception of its view angle (new Pe):

  Pe + AngChange(Ve, A)
  Pe + Ve*T + A*T'

And for the record, this is the velocity value (new Vb):

  Vb + (Ta-Td)*A*(1 + e)

Here is the the perceived velocity (new Ve):

  Ve + (Ta-Td)*A
==============
*/
void ViewAxisModify(view_axis_t * view, float angle, float speed, float time, float skill)
{
	float           max_accel, time_delta, error_factor;
	float           accel, ang_diff, vel_diff;
	float           accel_time, decel_time, const_time, accel_decel_diff, accel_coefficient;

	// Compute the maximum view velocity acc/deceleration from aim skill
	// (in deg / sec^2)
	max_accel = (1.0 - skill) * bot_view_actual_accel_min.value + (skill) * bot_view_actual_accel_max.value;

	// Put a reasonable cap so bots can always aim a little, even in the case
	// of user error
	if(max_accel < 100.0)
		max_accel = 100.0;

	// Compute the time differential between the old (current) view state and the
	// new (desired) view state
	time_delta = time - view->time;

	// Avoid doing wasted work when no extra time has elapsed
	if(time_delta == 0.0)
		return;

	// Compute the angle and velocity differences between the target view and current bot view
	ang_diff = AngleDelta(angle, view->angle.error);
	vel_diff = speed - view->speed.error;

	// Determine whether the acceleration velocity (for accel_time) is positive or negative
	// NOTE: See table in function comments for more information
	if(Square(vel_diff) >= 2 * fabs(ang_diff) * max_accel)
		accel = (vel_diff >= 0 ? max_accel : -max_accel);
	else
		accel = (ang_diff >= 0 ? max_accel : -max_accel);

	// Determine the most time the velocity could accelerate and decelerate
	// NOTE: See equations in function comments for more information
	decel_time = sqrt(Square(vel_diff) * .5 + accel * ang_diff) / max_accel;
	accel_time = vel_diff / accel + decel_time;

	// Determine how long the velocity will actually accelerate, decelerate, and remain constant
	if(accel_time > time_delta)
	{
		// The bot only has time to accelerate
		accel_time = time_delta;
		decel_time = 0;
		const_time = 0;
	}
	else if(accel_time + decel_time > time_delta)
	{
		// The bot won't have time to fully decelerate
		decel_time = time_delta - accel_time;
		const_time = 0;
	}
	else
	{
		// All remaining time will be spent in constant velocity
		const_time = time_delta - (accel_time + decel_time);
	}

	// Apply the acceleration with error factor
	//
	// NOTE: See the comments in the function description for a full
	// derivation of these values.

	// Precompute the time difference between acceleration and deceleration
	// as well as the acceleration coefficient
	accel_decel_diff = accel_time - decel_time;
	accel_coefficient = (Square(accel_time) +
						 decel_time * (accel_time + accel_decel_diff) + 2 * const_time * accel_decel_diff) * (.5);

	// Extract the bot's error factor for the next few hundred milliseconds
	error_factor = view->error_factor;

	// Update the angle and its error value
	//
	// NOTE: Positions are updated before velocities updating because the position
	// equations assume velocity and velocity error refer to the initial values,
	// not the updated ones.
	view->angle.real += view->speed.real * time_delta + accel * accel_coefficient * (1 + error_factor);
	view->angle.real = AngleNormalize180(view->angle.real);
	view->angle.error += view->speed.error * time_delta + accel * accel_coefficient;
	view->angle.error = AngleNormalize180(view->angle.error);

	// Update the velocity and its error value
	view->speed.real += accel * accel_decel_diff * (1 + error_factor);
	view->speed.error += accel * accel_decel_diff;

	// Update the view state timestamp
	view->time = time;
}

/*
===================
BotViewMakeFlawless

Makes the bot's view flawless.  (Change the ideal
and actual errors to match their real counterparts.)
===================
*/
void BotViewMakeFlawless(bot_state_t * bs)
{
	int             i;

	// Always use the ideal view state as the actual view
	for(i = PITCH; i <= YAW; i++)
	{
		// Remove the ideal error
		bs->view_ideal_next[i].angle.error = bs->view_ideal_next[i].angle.real;
		bs->view_ideal_next[i].speed.error = bs->view_ideal_next[i].speed.real;

		// Remove the actual error
		bs->view_now[i].angle.error = bs->view_now[i].angle.real;
		bs->view_now[i].speed.error = bs->view_now[i].speed.real;
	}

	// Errors were "corrected" this frame
	bs->view_ideal_error_time = server_time;
	bs->view_actual_error_time = server_time;
}

/*
==================
BotViewMakePerfect

Makes the bot's view perfect.  (Change the
actual view state to match the ideal view state.)
==================
*/
void BotViewMakePerfect(bot_state_t * bs)
{
	int             i;

	// Always use the ideal view state as the actual view
	for(i = PITCH; i <= YAW; i++)
	{
		// Copy the ideal angle and speed
		bs->view_now[i].angle.real = bs->view_ideal_next[i].angle.real;
		bs->view_now[i].speed.real = bs->view_ideal_next[i].speed.real;
	}

	// Remove all errors in the ideal and actual view states
	BotViewMakeFlawless(bs);
}

/*
=============
BotViewUpdate

Update the bot's view state.
=============
*/
void BotViewUpdate(bot_state_t * bs)
{
	int             i;
	view_axis_t     ideal[2];

#ifdef DEBUG_AI
	// Make the bot aim perfectly if requested
	if(bs->debug_flags & BOT_DEBUG_MAKE_VIEW_PERFECT)
	{
		BotViewMakePerfect(bs);
		return;
	}

	// Remove errors if flawless aim is requested
	if(bs->debug_flags & BOT_DEBUG_MAKE_VIEW_FLAWLESS)
		BotViewMakeFlawless(bs);
#endif

	// Correct ideal view errors and select new errors
	//
	// NOTE: The aiming code has already setup the ideal view location.
	BotViewCorrectIdeal(bs);

	// Correct errors in the understanding of the actual view state
	BotViewCorrectActual(bs);

	// Look up the bot's ideal view state at the next frame
	BotViewIdealNow(bs, ideal);

	// Modify each actual view axis independantly, based on its corresponding ideal view axis
	for(i = PITCH; i <= YAW; i++)
	{
		ViewAxisModify(&bs->view_now[i], ideal[i].angle.error, ideal[i].speed.error, ideal[i].time, bs->aim_skill);
	}
}

/*
==============
BotViewProcess

Process the bot's view state
==============
*/
void BotViewProcess(bot_state_t * bs)
{
	vec3_t          view;

	// Update the current view state
	BotViewUpdate(bs);

	// Extract the view angles
	ViewAnglesReal(bs->view_now, view);

	// Set this data as the bot's view command
	BotCommandView(bs, view);
}
