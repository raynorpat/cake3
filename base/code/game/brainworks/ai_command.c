// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_command.c
 *
 * Functions the bot uses to process commands
 *****************************************************************************/

#include "ai_main.h"
#include "ai_vars.h"
#include "ai_command.h"

#include "ai_client.h"
#include "ai_chat.h"
#include "ai_entity.h"
#include "ai_level.h"
#include "ai_order.h"
#include "ai_subteam.h"
#include "ai_team.h"


// This file provides access to some of the acceleration constants
// used in player movement.
#include "../bg_local.h"


// For the voice chats
#include "../../ui/menudef.h"


/*
=======
stristr
=======
*/
char           *stristr(char *str, char *charset)
{
	int             i;

	while(*str)
	{
		for(i = 0; charset[i] && str[i]; i++)
		{
			if(toupper(charset[i]) != toupper(str[i]))
				break;
		}

		if(!charset[i])
			return str;
		str++;
	}
	return NULL;
}

/*
================
BotCommandAction

The bot will send the specified action in the next command.

NOTE: The action is one of ACTION_*, listed in botlib.h.

NOTE: All of the movement actions, including walking, jumping,
and crouching, will be ignored.  It is NOT the responsibility
of this function to manipulate bot movement.  See ai_move.c
for that.
================
*/
void BotCommandAction(bot_state_t * bs, int action)
{
	usercmd_t      *cmd;

	// Inform the engine of the action
	trap_EA_Action(bs->client, action);

	// Look up the bot's next command structure
	cmd = &bs->cmd;

	// Set the appropriate command button for this action
	if(action & ACTION_RESPAWN)
		cmd->buttons |= BUTTON_ATTACK;
	if(action & ACTION_ATTACK)
		cmd->buttons |= BUTTON_ATTACK;
	if(action & ACTION_TALK)
		cmd->buttons |= BUTTON_TALK;
	if(action & ACTION_GESTURE)
		cmd->buttons |= BUTTON_GESTURE;
	if(action & ACTION_USE)
		cmd->buttons |= BUTTON_USE_HOLDABLE;
	if(action & ACTION_AFFIRMATIVE)
		cmd->buttons |= BUTTON_AFFIRMATIVE;
	if(action & ACTION_NEGATIVE)
		cmd->buttons |= BUTTON_NEGATIVE;
	if(action & ACTION_GETFLAG)
		cmd->buttons |= BUTTON_GETFLAG;
	if(action & ACTION_GUARDBASE)
		cmd->buttons |= BUTTON_GUARDBASE;
	if(action & ACTION_PATROL)
		cmd->buttons |= BUTTON_PATROL;
	if(action & ACTION_FOLLOWME)
		cmd->buttons |= BUTTON_FOLLOWME;
}

/*
================
BotCommandWeapon

Set the bot's user command weapon
================
*/
void BotCommandWeapon(bot_state_t * bs, int weapon)
{
	// Use the requested weapon
	trap_EA_SelectWeapon(bs->client, weapon);
	bs->cmd.weapon = weapon;
}

/*
===================
MoveCmdToViewAngles

Given a movement command, computes the
floating point view angles requested by
that command, given the inputted delta
angles for that entity.  The computed angles
are stored in "view"

NOTE: Remember that the deltas (ps->delta_angles)
have the same type as the command angles-- they are
stored as integers, not floating points.

NOTE: This code is based on PM_UpdateViewAngles()
in bg_pmove.c.
===================
*/
void MoveCmdToViewAngles(usercmd_t * cmd, int *delta, vec3_t view)
{
	int             short_angle;

	// The pitch angle has extra boundary checks
	short_angle = cmd->angles[PITCH] + delta[PITCH];
	if(short_angle > 16000)
		short_angle = 16000;
	else if(short_angle < -16000)
		short_angle = -16000;
	view[PITCH] = SHORT2ANGLE(short_angle);

	// Translate the yaw and roll angles from shorts to floats
	view[YAW] = SHORT2ANGLE(cmd->angles[YAW] + delta[YAW]);
	view[ROLL] = SHORT2ANGLE(cmd->angles[ROLL] + delta[ROLL]);

	// Make sure the angles are bounded in the standard manner
	//
	// NOTE: If the rest of the code is written well, this shouldn't
	// matter.  Bounding in [0,360) should be just as good as [-180,+180).
	// But better safe than sorry.
	view[PITCH] = AngleNormalize180(view[PITCH]);
	view[YAW] = AngleNormalize180(view[YAW]);
	view[ROLL] = AngleNormalize180(view[ROLL]);
}

/*
==============
BotCommandView

Set the bot's user command view angles
==============
*/
void BotCommandView(bot_state_t * bs, vec3_t view)
{
	int             angle;
	usercmd_t      *cmd;

	// Set this data as bot input
	//
	// NOTE: This just sets data used in the bot_input_t structure, accessed by
	// the trap_EA_GetInput() command.  The view data isn't actually used from
	// that command though.  This code just sets the data for safety should another
	// programmer want to use that structure.
	trap_EA_View(bs->client, view);

	// Set the view angles for the bot's command
	cmd = &bs->cmd;
	for(angle = PITCH; angle <= ROLL; angle++)
		cmd->angles[angle] = (short)(ANGLE2SHORT(view[angle]) - bs->ps->delta_angles[angle]);

	// Extract the view angles this command will send to the server-- due to
	// short rounding, this will probably differ from the inputted view angles
	MoveCmdToViewAngles(cmd, bs->ps->delta_angles, bs->now.view);
}

/*
=====================
ViewAnglesToMoveAxies

Given a set of view angles and a physics type, computes
the movement axis unit vectors relative to that view.
=====================
*/
void ViewAnglesToMoveAxies(vec3_t view, vec3_t * axies, int physics)
{
	vec3_t          move_angles;

	// Compute the angles used for movement
	move_angles[YAW] = view[YAW];
	move_angles[ROLL] = 0.0;
	if(physics == PHYS_WATER || physics == PHYS_FLIGHT)
		move_angles[PITCH] = view[PITCH];
	else
		move_angles[PITCH] = 0.0;

	// Compute the axies for these movement angles
	AngleVectors(move_angles, axies[0], axies[1], axies[2]);
}

/*
===================
MoveCmdToDesiredDir

Given a movement command, computes the desired
direction and speed of that movement, projected
onto the ground plane (if any).  The unitized
direction is stored in the "dir" vector and the
speed (magnitude) is the function's return value.

"axies" are the forward, right, and up axial direction
vectors for movement.  Note that the up direction
vector isn't actually used.

"physics" is the physics description that
applies to this command set.

"max_speed" is the entity's maximum movement speed.
That is, it's the velocity in units per second if
the command's forward move value is 127.

"water_level" is the entity's water level (1 for
wading, 2 for chest deep, 3 for immersed, 0 for no
water).  Presumably if the water level is 2 or higher,
the physics type will be PHYS_WATER, although this
isn't guaranteed.

FIXME: This function's interface could be simplified
if just a motion state pointer was passed in instead
of the user command, axies, maximum speed, and water
level.  The downside is that the user command and
axies would either need to be recomputed each call
or stored in the motion state.  One of these options
uses more processing time that necessary and the other
uses more memory than necessary.  Still, it might be
in the interest of cleanliness to spend more time or
memory for this purpose.  Or it might not.
===================
*/
float MoveCmdToDesiredDir(usercmd_t * cmd, vec3_t * axies, physics_t * physics,
						  float max_speed, float water_level, vec3_t move_dir)
{
	int             i, max_cmd_speed;
	float           scale, move_speed;
	float           wade_drag, max_wade_speed;
	vec3_t          forward, right;

	// The axial directions may need to be modified (projected onto the XY
	// plane), so this local copy guarantees the inputted vectors will
	// remain unchanged
	VectorCopy(axies[0], forward);
	VectorCopy(axies[1], right);

	// Compute the speed conversion factor from the movement commands to the actual speed
	//
	// NOTE: This code is based on PM_CmdScale() from bg_pmove.c.
	max_cmd_speed = fabs(cmd->forwardmove);
	if(max_cmd_speed < fabs(cmd->rightmove))
		max_cmd_speed = fabs(cmd->rightmove);
	if(max_cmd_speed < fabs(cmd->upmove))
		max_cmd_speed = fabs(cmd->upmove);

	if(max_cmd_speed > 0)
		scale = (max_speed * max_cmd_speed) /
			(127.0 * sqrt(Square(cmd->forwardmove) + Square(cmd->rightmove) + Square(cmd->upmove)));
	else
		scale = 0;

	// Compute the velocity vector this set of movement commands translates to
	//
	// NOTE: This code is based in part on PM_WalkMove(), PM_WaterMove(),
	// PM_AirMove(), and PM_FlyMove() from from bg_pmove.c.

	// Air movement and ground movement require some special setup
	if((physics->type == PHYS_GRAVITY) || (physics->type == PHYS_GROUND))
	{
		// When doing air movement and normal walking, project the movement on the X-Y plane
		forward[2] = 0.0;
		right[2] = 0.0;

		// When moving on the ground, project movement onto the angle of the ground plane
		//
		// NOTE: This is one of the few functions in bg_pmove.c that can be called
		// by outside functions.
		//
		// NOTE: It *IS* correct that these clippings occur before the
		// renormalization required by projecting the forward and right
		// vectors to the X-Y plane.  This may be a bug in bg_pmove.c, but
		// even in that case, this code is bug compliant.
		//
		// NOTE: It is intentional that this code checks specifically for
		// ground physics, even though there could be a (very steep) ground
		// normal in the gravity case.  This strikes me as a bug in the
		// bg_pmove.c code but...  Again, this code is just bug compliant.
		if(physics->type == PHYS_GROUND)
		{
			PM_ClipVelocity(forward, physics->ground, forward, OVERCLIP);
			PM_ClipVelocity(right, physics->ground, right, OVERCLIP);
		}

		// These direction vectors must be normalized again
		VectorNormalize(forward);
		VectorNormalize(right);
	}

	// The entity can sink when in water
	if((scale <= 0) && (physics->type == PHYS_WATER))
	{
		VectorSet(move_dir, 0, 0, -1);
		move_speed = 60.0;
	}

	// Otherwise compute the velocity normally
	else
	{
		// Project movement commands in each axis direction
		for(i = 0; i < 3; i++)
			move_dir[i] = forward[i] * cmd->forwardmove + right[i] * cmd->rightmove;

		// Also apply the upward movement if the entity's situation allows it
		if((physics->type == PHYS_WATER) || (physics->type == PHYS_FLIGHT))
			move_dir[2] += cmd->upmove;

		// Compute the movement speed in that direction and normalize the velocity
		//
		// NOTE: Technically it would be more precise to just normalize the movement
		// direction and then multiply the movement speed by the scale.  Unfortunately,
		// the server code does the operation like this, and the rest of the physics
		// depends on this imprecision, so it must be copied here as well.
		VectorScale(move_dir, scale, move_dir);
		move_speed = VectorNormalize(move_dir);
	}

	// The player has a lower top speed when in water
	if((physics->type == PHYS_WATER) && (move_speed > max_speed * pm_swimScale))
	{
		move_speed = max_speed * pm_swimScale;
	}

	// Crouching on the ground also lowers top speed
	if((cmd->upmove < 0) && (physics->type == PHYS_GROUND) && (move_speed > max_speed * pm_duckScale))
	{
		move_speed = max_speed * pm_duckScale;
	}

	// Wading in water lowers top speed
	if((water_level > 0) && (physics->type == PHYS_GROUND))
	{
		// Compute the percentage of maximum speed lost from wading in water
		wade_drag = (1.0 - pm_swimScale) * (water_level / 3.0);

		// Compute the maximum speed allowed due to water drag
		max_wade_speed = max_speed * (1.0 - wade_drag);

		// Lower the top speed if it exceeds this new maximum speed
		if(move_speed > max_wade_speed)
			move_speed = max_wade_speed;
	}

	// Return the computed movement speed
	return move_speed;
}

/*
================
MoveCmdViewToDir

This function translates a movement command and
client view angles into an intended movement
direction.  (For example, if the client in question
is facing north and their move command says move
left, their movement vector will point west.)

The "physics" argument will force the direction to the
X-Y plane unless the physics allow movement in the Z axis
as well (for water and fight).

The movement direction will be normalized, unless
the target is stationary (in which case it will be
zero).  Returns true if the command translated
to a direction and false if not.
================
*/
qboolean MoveCmdViewToDir(usercmd_t * cmd, vec3_t view, vec3_t move_dir, int physics)
{
	int             i;
	vec3_t          axies[3];

	// Translate the view angles to a set of direction axies
	ViewAnglesToMoveAxies(view, axies, physics);

	// Apply the command directions given these movement axies
	for(i = 0; i < 3; i++)
	{
		move_dir[i] = axies[0][i] * cmd->forwardmove + axies[1][i] * cmd->rightmove;
	}

	// Also apply the upward movement if not restricted to planar movement
	//
	// NOTE: Yes, this is the correct way of computing it.  "Jumping" while in water
	// looking slightly upwards will move you up even more.
	if(physics == PHYS_WATER || physics == PHYS_FLIGHT)
		move_dir[2] += cmd->upmove;

	// Normalize the direction
	return (VectorNormalize(move_dir) > 0.0);
}

/*
=============
ClientViewDir

Given a client, extracts the last known normalized
movement direction they selected.
=============
*/
void ClientViewDir(gclient_t * client, vec3_t dir)
{
	// FIXME: Perhaps this code should look up the client's actual physics and use that
	// instead.  Determining the current physics that apply is a reasonably painful
	// thing, however, and ground is almost always correct for the purpose of this function.
	MoveCmdViewToDir(&client->pers.cmd, client->ps.viewangles, dir, PHYS_GROUND);
}

/*
==================
MoveDirToCmdNormal

This function translates the requested movement
direction to movement commands for typical movement.

"axies" are the unit vector movement axies (forward, right,
and up) for the entity's current heading.

"speed_rate" is a number between 0.0 and 1.0 representing
what percentage of maximum speed the entity wants to move.

"jump_crouch" is a movement jump or crouch style.  See MJC_*
in ai_main.h for more information.
==================
*/
void MoveDirToCmdNormal(vec3_t move_dir, usercmd_t * cmd, vec3_t * axies, float speed_rate, int jump_crouch)
{
	float           max, scale;
	vec3_t          dir;

	// Compute a direction vector (not necessarily normalized) for movement
	//
	// NOTE: The up move value computation doesn't need a dot product like
	// the forward and right movement values because it's essentially taking
	// a dot product against the up vector (0, 0, 1).  This is a bit strange
	// since forward and right need not be embedded in the X-Y plane, but it
	// is how the server code process it.  (See PM_WaterMove() and PM_FlyMove()
	// in bg_pmove.c for more information.)
	dir[0] = DotProduct(axies[0], move_dir);
	dir[1] = DotProduct(axies[1], move_dir);
	dir[2] = move_dir[2];

	// Determine the magnitude of the greatest axial change, for reasons that
	// will shortly become understandable (although not acceptable)
	max = fabs(dir[0]);
	if(max < fabs(dir[1]))
		max = fabs(dir[1]);
	if(max < fabs(dir[2]))
		max = fabs(dir[2]);

	// Compute the movement command values associated with this vector
	//
	// NOTE: This code is partially an inverse of PM_CmdScale() from bg_pmove.c.
	//
	// NOTE: PM_CmdScale() scales the player's speed to move at:
	//
	//   bs->ps->speed * max(forward_move, right_move, up_move) / 127
	//
	// So if the bot wants to move slower than bs->ps->speed, the maximum value
	// must be set to 127 * desired_speed / bs->ps->speed, and all other movement
	// values must be set proportionate to the first value.  It's not terrible
	// difficult to do this, but it's confusing.  It shouldn't be this hard to
	// compute movement directions and speeds.

	if(max > 0)
	{
		scale = 127 * speed_rate / max;
		cmd->forwardmove = floor(scale * dir[0] + .5);
		cmd->rightmove = floor(scale * dir[1] + .5);
		cmd->upmove = floor(scale * dir[2] + .5);
	}
	else
	{
		cmd->forwardmove = 0;
		cmd->rightmove = 0;
		cmd->upmove = 0;
	}

	// Check for jumping, crouching, and other forms of Z axis movement (eg. swimming)
	//
	// NOTE: Because of how PM_CmdScale() works, jumping actually causes the bot to
	// move slower when moving forward.  Even when the full 127 units of forward movement
	// are sent (speed g_speed), if a player jumps, the forward speed gets scaled back
	// to g_speed / sqrt(2).  The code is intended to stop the bot from moving sqrt(2) when
	// moving both forward and right at top speed, but it has the side effect of slowing
	// forward movement when jump is sent (even though sending 127 units for jump doesn't
	// actually create 127 units of upward velocity).  This may or may not be considered a
	// bug, but it's definitely unintuitive behavior.  It's actually impossible for the
	// player movement code to process an additional forward velocity of g_speed in the
	// same frame that the bot jumps, even though this client side code may request it.
	// So rather than try to adjust the movement commands to compensate for the speed loss
	// in some situations, it's best just to suck up the loss always.
	//
	// FIXME: Clearly this code is victim of some very poor design.  The player move
	// code simply should not do this.  It's not clear how it should be fixed, but the
	// current code is obviously a horrible mess.  This must not be.
	if(jump_crouch > 0)
		cmd->upmove = 127;
	else if(jump_crouch < 0)
		cmd->upmove = -127;
}

/*
======================
MoveDirToCmdStrafejump

This function tries to translate the requested movement
direction to movement commands for strafe jumping.
Returns true if it was setup and false if not

"velocity" is the entity's current velocity.
"physics" is the type of entity uses.
======================
*/
qboolean MoveDirToCmdStrafejump(vec3_t move_dir, usercmd_t * cmd, vec3_t velocity, int physics)
{
	float           cross_product;

	// Never strafe jump without a valid movement direction
	if(!move_dir[0] && !move_dir[1])
		return qfalse;

	// This is the Z-component of the cross product of the current velocity
	// and ideal velocity-- this value is positive when velocity aims to the
	// right of the intended movement direction.
	cross_product = (move_dir[0] * velocity[1]) - (move_dir[1] * velocity[0]);

	// Strafe so that the current velocity converges to the ideal velocity
	//
	// NOTE: This helps stop strafejump drift
	cmd->rightmove = (cross_product < 0 ? 127 : -127);

	// Always move forward
	cmd->forwardmove = 127;

	// Jump if on the ground
	//
	// NOTE: In some circumstances, crouching while strafe jumping can make the
	// client go faster (down stairs, for example), but such techniques are
	// beyond the scope of this code
	cmd->upmove = (physics == PHYS_GROUND ? 127 : 0);

	// The command state was setup for strafe jump movement
	return qtrue;
}

/*
==============
BotCommandMove

Given a requested movement unitized movement direction
and a speed rate between 0.0 (stopped) and 1.0 (full speed),
and the movement angles already in the bot's command
state bs->cmd, computes the movement commands that
will move the bot in the requested manner and stores them
in bs->cmd.

"move_modifiers" are the movement modifiers that should
be applied.  (Most importantly, this defines whether
strafejumping or walking should be turned on or off.)

The "jump_crouch" argument is the requested movement
jump or crouch styled (see the MJC_* values in ai_main.h).
Positive numbers mean jumping for different reasons,
negative means crouching, and zero means no preference.

NOTE: Because this command depends on the view commands
being processed, in general the BotCommandView() function
should be called first (unless the user command structure
has its view angles filled out by some other means).
==============
*/
void BotCommandMove(bot_state_t * bs, vec3_t move_dir, float speed_rate, int jump_crouch)
{
	vec3_t          view, move_axies[3];
	usercmd_t      *cmd;

	// Look up the command pointer for convienence
	cmd = &bs->cmd;

	// Use normal movement unless the bot wants to strafe jump and succeeds
	if(jump_crouch != MJC_STRAFEJUMP || !MoveDirToCmdStrafejump(move_dir, cmd, bs->now.velocity, bs->now.physics.type))
	{
		// Get the movement axies, based on command view angles
		MoveCmdToViewAngles(cmd, bs->ps->delta_angles, view);
		ViewAnglesToMoveAxies(view, move_axies, bs->now.physics.type);

		// Use normal movement
		MoveDirToCmdNormal(move_dir, cmd, move_axies, speed_rate, jump_crouch);
	}

	// Check if the bot can legally activate the walking flag (which makes movement silent)
	//
	// NOTE: This code is based on PmoveSingle() in bg_pmove.c
	if((abs(cmd->forwardmove) <= 64) && (abs(cmd->rightmove) <= 64))
	{
		BotCommandAction(bs, ACTION_WALK);
	}

	// Update the bot's current motion state with the new movement commands
	bs->now.forward_move = cmd->forwardmove;
	bs->now.right_move = cmd->rightmove;
	bs->now.up_move = cmd->upmove;
}

/*
=================
BotAddresseeMatch
=================
*/
qboolean BotAddresseeMatch(bot_state_t * bs, bot_match_t * match)
{
	int             teammates;
	char            addressee[MAX_MESSAGE_SIZE];
	char            name[MAX_MESSAGE_SIZE];
	char           *botname;
	bot_match_t     submatch;

	// If the message isn't addressed to anyone in particular, the bot may or may not react
	if(!(match->subtype & ST_ADDRESSED))
	{
		// If the message was only given to this bot, the bot definitely reacts
		submatch.type = 0;
		if(trap_BotFindMatch(match->string, &submatch, MTCONTEXT_REPLYCHAT) && submatch.type == MSG_CHATTELL)
			return qtrue;

		// The bot still might randomly react, though it's less likely with more teammates
		teammates = BotTeammates(bs);
		if(!teammates)
			return qtrue;
		return (random() <= 1.0 / teammates);
	}

	// Search for the bot's name in the message's addressee list
	botname = SimplifyName(EntityNameFast(bs->ent));
	trap_BotMatchVariable(match, ADDRESSEE, addressee, sizeof(addressee));
	while(trap_BotFindMatch(addressee, &submatch, MTCONTEXT_ADDRESSEE))
	{
		// If matching everyone on the team, automatically respond
		if(submatch.type == MSG_EVERYONE)
			return qtrue;

		// The bot responds if the next name matches its name or its subteam's name
		trap_BotMatchVariable(&submatch, TEAMMATE, name, sizeof(name));
		if(strlen(name))
		{
			if(stristr(botname, name))
				return qtrue;
			if(stristr(bs->subteam, name))
				return qtrue;
		}

		// Exit if this is the last name in the list
		if(submatch.type != MSG_MULTIPLENAMES)
			break;

		// Get the next name in the list
		trap_BotMatchVariable(&submatch, MORE, addressee, sizeof(addressee));
	}

	// The bot was not found in the specific addressee list
	return qfalse;
}

/*
===============
BotMatchMessage

This function returns if the message could be matched.  It does not
mean the bot actually processed the message.

NOTE: Death messages are sent as an EV_OBITUARY event, not actual console
messages.  As such, they are processed by BotCheckEvents() in ai_scan.c.
===============
*/
qboolean BotMatchMessage(bot_state_t * bs, char *message)
{
	bot_match_t     match;
	char            name[MAX_MESSAGE_SIZE];
	gentity_t      *sender;

	// Try to match this message as a CTF teamchat message
	match.type = 0;
	if(!trap_BotFindMatch(message, &match, MTCONTEXT_MISC | MTCONTEXT_INITIALTEAMCHAT | MTCONTEXT_CTF))
		return qfalse;

	// Ignore messages in deathmatch modes, but return true because it's a real message
	if(!(game_style & GS_TEAM))
		return qtrue;

	// Check if this message is a team management message
	trap_BotMatchVariable(&match, NETNAME, name, sizeof(name));
	sender = TeammateFromName(bs, name);
	if(BotMatch_Team(bs, &match, sender))
		return qtrue;

	// Ignore messages not from a teammate
	if(!sender)
	{
		Bot_InitialChat(bs, "whois", name, NULL);
		trap_BotEnterChat(bs->cs, bs->client, CHAT_TEAM);
		return qtrue;
	}

	// Ignore other messages if they aren't intended for this bot
	if(!BotAddresseeMatch(bs, &match))
		return qtrue;

	// Check if this message is an order
	if(BotMatch_Order(bs, &match, sender))
		return qtrue;

	// Check if this message is a subteam request
	if(BotMatch_Subteam(bs, &match, sender))
		return qtrue;

	// Still return true because the message matched-- our code just elected not to process it
	BotAI_Print(PRT_WARNING, "Unknown match type %i\n", match.type);
	return qtrue;
}
