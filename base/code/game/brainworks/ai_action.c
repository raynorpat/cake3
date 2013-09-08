// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_action.c
 *
 * Functions the bot uses to take different actions
 *****************************************************************************/

#include "ai_main.h"
#include "ai_vars.h"
#include "ai_action.h"

#include "ai_aim.h"
#include "ai_aware.h"
#include "ai_chat.h"
#include "ai_client.h"
#include "ai_command.h"
#include "ai_dodge.h"
#include "ai_entity.h"
#include "ai_goal.h"
#include "ai_maingoal.h"
#include "ai_move.h"
#include "ai_pickup.h"
#include "ai_scan.h"
#include "ai_self.h"
#include "ai_team.h"
#include "ai_use.h"
#include "ai_view.h"
#include "ai_weapon.h"


// These flags determine the "Action Function Conditions" which must be
// met to execute an action function

#define AFC_NONE		0x0000	// No conditions to meet
#define AFC_NOGAME		0x0001	// Bot is an observer or in intermission
#define AFC_INGAME		0x0002	// Bot is neither an observer nor in intermission
#define AFC_ALIVE		0x0004	// Bot is alive
#define AFC_DEAD		0x0008	// Bot is dead
#define AFC_CHAT		0x0010	// Bot is chatting (not playing)
#define AFC_PLAY		0x0020	// Bot is playing (not chatting) (could be a bot waiting to respawn)
#define AFC_REFLEX		0x0040	// Reflex time is positive (do reflexive thought)
#define AFC_LOGIC		0x0080	// Logic time is positive (do logical thought)
#define AFC_RFXLGC		0x0100	// Either running a reflex frame or logical frame (or both)

// Action functions accept a standard set of inputs, but it's safer
// to put them in one structure.  If the number of inputs drastically
// increases (a possibility with having many functions to call), it's
// much easier to change this structure than every function syntax.
// It's also faster to pass in one pointer than ten argument copies.
//
// NOTE: See the header comment for BotActions() for a detailed discussion
// of the different elapsed time values.
typedef struct bot_action_args_s
{
	int             conditions;	// Bitmask of AFCs the bot currently meets

	float           ai_elapsed;	// Time elapsed since the last AI frame was run (subconscious)
	float           game_elapsed;	// Time elapsed since the game state last changed (reflexive)
	float           logic_elapsed;	// Time elapsed since the bot last did a logical thought frame (logical)

	bot_moveresult_t moveresult;	// Result of bot's attempted movement this frame
} bot_action_args_t;

// Each action function uses this following interface
typedef void    bot_action_func_t(bot_state_t * bs, bot_action_args_t * args);


/*
=====================
BotActionCommandReset
=====================
*/
void BotActionCommandReset(bot_state_t * bs, bot_action_args_t * args)
{
	// Commit to recomputing all inputs for this frame
	trap_EA_ResetInput(bs->client);
	memset(&bs->cmd, 0, sizeof(usercmd_t));
}

/*
=========================
BotActionCommandTimestamp
=========================
*/
void BotActionCommandTimestamp(bot_state_t * bs, bot_action_args_t * args)
{
	// Update the bot's command timestamp (the estimated time at which the
	// server will process the bot's next commands)
	//
	// NOTE: It would be nice if bots always computed commands for the current time,
	// ai_time_ms.  However, the server code forces all bots to run synchronized with
	// the server, running in 50 millisecond intervals.  No matter what command time
	// is provided here, G_RunClient() in g_active.c always forces the bot's command
	// time to be the next level update time.  So all the bot command processing
	// decides on commands assuming this is their timestamp.
	bs->cmd.serverTime = server_time_ms + SERVER_FRAME_DURATION_MS;
	bs->command_time = bs->cmd.serverTime * 0.001;
}

/*
=============
BotActionText
=============
*/
void BotActionText(bot_state_t * bs, bot_action_args_t * args)
{
	// Process all commands from the server
	BotCheckServerCommands(bs);

	// Process console message input
	BotCheckConsoleMessages(bs);
}

/*
===============
BotActionNogame
===============
*/
void BotActionNogame(bot_state_t * bs, bot_action_args_t * args)
{
	// Differentiate between spectating and intermission
	if(BotInIntermission(bs))
	{
		// Do end-of-level chatter when entering the intermission state
		if(bs->ai_state != AIS_INTERMISSION)
			BotChatEndLevel(bs);

		bs->ai_state = AIS_INTERMISSION;
	}
	else
	{
		bs->ai_state = AIS_OBSERVER;
	}
}

/*
=============
BotActionSelf
=============
*/
void BotActionSelf(bot_state_t * bs, bot_action_args_t * args)
{
	int             weapon;
	vec3_t          view;

	// Recompute the bot's forward vector, used for field-of-view checks
	ViewAnglesReal(bs->view_now, view);
	AngleVectors(view, bs->forward, NULL, NULL);

	// Determine which weapons the bot has available for sustained use (>= 2 seconds)
	bs->weapons_available = 0;
	for(weapon = WP_NONE + 1; weapon < WP_NUM_WEAPONS; weapon++)
	{
		if(BotHasWeapon(bs, weapon, 2.0 / weapon_stats[weapon].reload))
			bs->weapons_available |= (1 << weapon);
	}

#ifdef DEBUG_AI
	// When forced to use a specific weapon, none of the others are available
	if(bs->use_weapon > WP_NONE && bs->use_weapon < WP_NUM_WEAPONS)
		bs->weapons_available = (1 << bs->use_weapon);
#endif

	// Update the bot's current motion state
	BotMotionUpdate(bs);
}

/*
=================
BotActionAccuracy
=================
*/
void BotActionAccuracy(bot_state_t * bs, bot_action_args_t * args)
{
	// Updating accuracy tracking
	//
	// NOTE: This must occur before BotScan(), which can update bs->aim_enemy
	//
	// NOTE: This must occur before BotStateDead(), which deactivates further
	// hitscan accuracy updates after the bot is dead (so the bot only
	// processes accuracy for frame during which it last alive).
	BotAccuracyUpdate(bs);
}

/*
=============
BotActionScan
=============
*/
void BotActionScan(bot_state_t * bs, bot_action_args_t * args)
{
	// Scan the surroundings for new stuff
	//
	// NOTE: The bot must everything every server frame.  This is because
	// temporary entities can get freed after one server frame (about 50ms).
	// Since the bot think time is usually larger than this (100ms), the bot
	// would miss scanning important events if it only scanned every logical
	// thought frame.  In fact, player generated and predicted events could
	// be generated even between server frames, so some things must be scanned
	// at every possible opportunity.  See the comment by SCAN_CONTINUAL in
	// ai_scan.h for more information
	if(args->game_elapsed)
		BotScan(bs, SCAN_ALL);
	else
		BotScan(bs, SCAN_CONTINUAL);
}

/*
==================
BotActionAwareness
==================
*/
void BotActionAwareness(bot_state_t * bs, bot_action_args_t * args)
{
	// Clean out stale values in the awareness engine
	BotAwarenessUpdate(bs);
}

/*
==============
BotActionAlive
==============
*/
void BotActionAlive(bot_state_t * bs, bot_action_args_t * args)
{
	int             i;
	vec3_t          view;

	// Setup some stuff if the bot just entered the active state
	if(bs->ai_state != AIS_ALIVE)
	{
		// Reset weapon information
		bs->weapon = bs->ps->weapon;
		bs->melee_time = 0;
		bs->weapon_rate = 1.0;
		bs->fire_choice = qfalse;
		bs->fire_start_time = 0;
		bs->fire_stop_time = 0;

		// Reset miscellaneous commands
		//
		// NOTE: This is necessary because the bot's command structure
		// might not have been reset since the last server frame ran (see
		// BotActionCommandReset() and its execution conditions).  So it's
		// possible some of the bot's decisions from last frame (eg. fire)
		// will have carried over into this game.  Of particular interest
		// is the respawn flag, which is interpretted as BUTTON_ATTACK.
		// So if these buttons weren't reset, a dead bot that sent a respawn
		// (attack) last frame will accidently send another BUTTON_ATTACK
		// once it respawns, which would make it fire.
		//
		// NOTE: It would be really nice if these flags could be deactivated
		// in the bot stated, but no inverse to the function trap_EA_Action()
		// exists.  See BotCommandAction() for more information on setting
		// and synchronizing these actions.
		bs->cmd.buttons = 0x0000;

		// Look up the actual view angles
		//
		// NOTE: The actual view angles are setup each frame by BotActions()
		ViewAnglesReal(bs->view_now, view);

		// Reset the current view state and the ideal view state pair,
		// using the current view angles
		ViewReset(bs->view_now, view);
		ViewReset(bs->view_ideal_last, view);
		ViewReset(bs->view_ideal_next, view);
		bs->view_ideal_reset_time = bs->command_time;
		bs->view_ideal_error_time = server_time;
		bs->view_actual_error_time = server_time;

		// Reset last frame's health and armor
		bs->last_health = bs->ps->stats[STAT_HEALTH];
		bs->last_armor = bs->ps->stats[STAT_ARMOR];

#ifdef DEBUG_AI
		// Spawn the bot with all weapons if requested
		if(bs->use_weapon < WP_NONE)
		{
			// Give the bot one of each weapon except the BFG
			//
			// FIXME: Perhaps this code should analyze the weapons on the level
			// and just give the bot one of each of those instead.  Something
			// similar to this is done on startup by ai_resource.c, so clearly
			// it's possible to determine which weapons are present and which
			// are not.
			for(i = 0; i < WP_NUM_WEAPONS; i++)
			{
				// Ignore the obviously overpowered BFG
				if(i == WP_BFG)
					continue;

				// Give the bot the weapon, and ammo if necessary
				bs->ps->stats[STAT_WEAPONS] |= (1 << i);
				if(bs->ps->ammo[i] >= 0)
					bs->ps->ammo[i] = 200;
			}
		}
#endif

		// Remember that the bot entered the alive state
		bs->ai_state = AIS_ALIVE;
	}

	// Remember when the bot last breathed
	if((bs->ps->powerups[PW_BATTLESUIT]) || (bs->now.water_level <= 1))
	{
		bs->last_air_time = server_time;
	}

	// Check if the bot just teleported
	if((bs->ps->eFlags ^ bs->last_eFlags) & EF_TELEPORT_BIT)
	{
		// The bot teleported this frame
		bs->teleport_time = server_time;

		// Reset the bot's last view state error correction
		bs->view_actual_error_time = server_time;
	}
	bs->last_eFlags = bs->ps->eFlags;
}

/*
=============
BotActionDead
=============
*/
void BotActionDead(bot_state_t * bs, bot_action_args_t * args)
{
	// If the bot has already entered the dead state, there is nothing to process
	if(bs->ai_state == AIS_DEAD)
		return;

	// Reset the move state if one exists
	if(bs->ms)
	{
		trap_BotResetMoveState(bs->ms);
		trap_BotResetAvoidReach(bs->ms);
	}

	// Reset attack enemy
	BotAimEnemySet(bs, NULL, NULL);

	// Reset goal information
	BotGoalReset(bs);

	// Possibly create death chatter
	BotChatDeath(bs);

	// Determine when to respawn-- better bots respawn sooner
	//
	// NOTE: It's impossible for any player to respawn sooner than 1.7 seconds
	// after death.  See player_die in g_combat.c for more information.
	bs->respawn_time = bs->command_time + 1.5;
	bs->respawn_time += random() * 1.5 / (bs->settings.skill > 1 ? bs->settings.skill : 1);

	// Remember that the bot entered the dead state
	bs->ai_state = AIS_DEAD;
}

/*
================
BotActionRespawn
================
*/
void BotActionRespawn(bot_state_t * bs, bot_action_args_t * args)
{
	// Make sure the bot is ready to respawn
	if(bs->command_time < bs->respawn_time)
		return;

	// Time to respawn
	BotCommandAction(bs, ACTION_RESPAWN);

	// Force the bot to print its chat message if necessary
	if(bs->chat_time)
		bs->chat_time = bs->command_time;
}

/*
=============
BotActionTeam
=============
*/
void BotActionTeam(bot_state_t * bs, bot_action_args_t * args)
{
#ifdef MISSIONPACK
	// Change task preferences if necessary
	BotUpdateTaskPreference(bs);
#endif

	// Make sure the bot has a valid team leader
	BotCheckLeader(bs);

	// Send commands if this bot is the team leader
	BotTeamAI(bs);
}

/*
=================
BotActionChatType
=================
*/
void BotActionChatType(bot_state_t * bs, bot_action_args_t * args)
{
	// If done typing, print the talk message and exit type mode
	if(bs->chat_time <= bs->command_time)
	{
		trap_BotEnterChat(bs->cs, bs->chat_client, bs->chat_style);
		bs->chat_time = 0;
		return;
	}

	// Put up chat icon
	BotCommandAction(bs, ACTION_TALK);

	// Possibly bitch at opponents for attacking the bot while talking
	if(bs->damaged)
		BotChatHitTalking(bs);

	// If the bot notices some enemies, finish typing faster
	if(bs->aim_enemy || bs->goal_enemy)
	{
		if(bs->chat_time > bs->command_time + 0.1)
			bs->chat_time = bs->command_time + 0.1;
	}
}

/*
=====================
BotActionChatGenerate
=====================
*/
void BotActionChatGenerate(bot_state_t * bs, bot_action_args_t * args)
{
	// Possibly generate innane chatter
	BotChatIngame(bs);
}

/*
==================
BotActionMoveSetup
==================
*/
void BotActionMoveSetup(bot_state_t * bs, bot_action_args_t * args)
{
	// Setup the bot's movement characteristics for this frame
	BotMoveSetup(bs);
}

/*
=============
BotActionGoal
=============
*/
void BotActionGoal(bot_state_t * bs, bot_action_args_t * args)
{
	// Never update goals while the bot is in air (and can't fly).  The
	// bot can't seriously change its movement now anyway, so there's no
	// sense deciding anything until it lands.
	if(bs->now.physics.type == PHYS_GRAVITY)
		return;

	// Select and execute goals
	BotMainGoal(bs);
	BotItemGoal(bs);
}

/*
===================
BotActionMoveSelect
===================
*/
void BotActionMoveSelect(bot_state_t * bs, bot_action_args_t * args)
{
	// Basic movement
	//
	// NOTE: The move result is cached in the arguments so aiming can use it
	BotMoveSelect(bs, &args->moveresult);
}

/*
======================
BotActionMoveModifiers
======================
*/
void BotActionMoveModifiers(bot_state_t * bs, bot_action_args_t * args)
{
	// Determine how the bot is allowed to modify its movement (eg. dodge, strafe jump, etc.)
	BotMoveModifierUpdate(bs);
}

/*
==================
BotActionAimSelect
==================
*/
void BotActionAimSelect(bot_state_t * bs, bot_action_args_t * args)
{
	// Aim somewhere
	BotAimSelect(bs, &args->moveresult);
}

/*
==============
BotActionDodge
==============
*/
void BotActionDodge(bot_state_t * bs, bot_action_args_t * args)
{
	// Dodge if necessary
	BotDodgeMovement(bs);
}

/*
====================
BotActionMoveProcess
====================
*/
void BotActionMoveProcess(bot_state_t * bs, bot_action_args_t * args)
{
	// Package the desired movements into commands the server understands
	BotMoveProcess(bs);
}

/*
============
BotActionUse
============
*/
void BotActionUse(bot_state_t * bs, bot_action_args_t * args)
{
	// Use abilities (such as holdable items)
	BotUse(bs);
}

/*
===================
BotActionViewUpdate
===================
*/
void BotActionViewUpdate(bot_state_t * bs, bot_action_args_t * args)
{
	// Change the bot's current view angles to converge towards its intended view
	BotViewUpdate(bs);
}

/*
====================
BotActionViewProcess
====================
*/
void BotActionViewProcess(bot_state_t * bs, bot_action_args_t * args)
{
	// Package the bot's current view angles in the user command structure
	BotViewProcess(bs);
}

/*
===================
BotActionFireUpdate
===================
*/
void BotActionFireUpdate(bot_state_t * bs, bot_action_args_t * args)
{
	// Decide if the bot should start firing for a little while
	BotAttackFireUpdate(bs);
}

/*
===================
BotActionFireWeapon
===================
*/
void BotActionFireWeapon(bot_state_t * bs, bot_action_args_t * args)
{
	// Continue firing if the bot decided to fire recently
	BotAttackFireWeapon(bs);
}

/*
====================
BotActionCommandSend
====================
*/
void BotActionCommandSend(bot_state_t * bs, bot_action_args_t * args)
{
	// Send the bot's command
	trap_BotUserCommand(bs->client, &bs->cmd);

	// Record the timestamp of the last sent command
	bs->last_command_time_ms = bs->cmd.serverTime;
}



// An action is a pair containing an action function and a list of conditions
// that must be met for it to execute
typedef struct bot_action_s
{
	bot_action_func_t *func;	// The action function to execute
	int             conditions;	// Bitmap of AFCs that must be met
} bot_action_t;

// An ordered list of all actions the bot could execute and their conditions
//
// NOTE: Do *NOT* change the order of functions in this list unless you know
// exactly what you are doing.  There are a lot of dependancies between these
// function calls.
bot_action_t    bot_actions[] = {
	// These three (command reset and timestamp update, and text processing)
	// must occur before everything else.
	// - Obviously updating the timestamp must occur after the command is reset.
	//   The timestamp update must occur either when a reset occurs or when the
	//   server time changes, so that's every reflexive and logical frame.
	// - Text processing has no dependancies with command updating.
	{BotActionCommandReset, AFC_LOGIC},	// Reset commands bot will send
	{BotActionCommandTimestamp, AFC_RFXLGC},	// Update the next command's timestamp
	{BotActionText, AFC_REFLEX},	// Read console text and messages

	// Bots not actually in the game have very little logic
	{BotActionNogame, AFC_NOGAME | AFC_REFLEX},	// Spectator chatter

	// - Self must occur first, updating the bot's position in the world.
	// - Accuracy requires last frame's enemy zone data (bs->aim_zone), so
	//   it must occur  before scanning, which updates the enemy aim zone.
	// - It's possible for player events to occur even when the world state
	//   has not changed, so scanning must always run (see the comment in
	//   BotActionScan() for more information).
	// - The awareness data can get changed by scanning, so it should not
	//   (but can) be processed until scanning has finished.
	{BotActionSelf, AFC_INGAME | AFC_ALIVE | AFC_REFLEX},	// Process changes to the bot
	{BotActionAccuracy, AFC_INGAME | AFC_REFLEX},	// Process new accuracy data
	{BotActionScan, AFC_INGAME},	// Scan for new events
	{BotActionAwareness, AFC_INGAME | AFC_REFLEX},	// Update what the bot is aware of

	// - The team organization logic can happen at almost any time
	{BotActionTeam, AFC_INGAME | AFC_LOGIC},	// Team organization decisions

	// - The alive state sets up everything used for ingame logic and
	//   should therefore occur before it.
	// - The dead state resets weapon information which could be (but
	//   currently isn't) used by the accuracy data, so it should
	//   occur after that.
	// - The dead state reset and respawn are here with alive for duality.
	{BotActionAlive, AFC_INGAME | AFC_ALIVE | AFC_REFLEX},	// Reset stuff when the bot respawns
	{BotActionDead, AFC_INGAME | AFC_DEAD | AFC_REFLEX},	// Reset stuff when the bot dies
	{BotActionRespawn, AFC_INGAME | AFC_DEAD | AFC_LOGIC},	// Respawn when necessary

	// These three can occur in any order
	// - Chat typing must occur after the dead action and the chat generation
	//   action, both of which can set up and/or modify chat messages.
	{BotActionChatGenerate, AFC_INGAME | AFC_PLAY | AFC_LOGIC},	// Generate innane chatter
	{BotActionChatType, AFC_INGAME | AFC_CHAT | AFC_LOGIC},	// Delay while typing the message

	// - Movement setup must be run before any goal processing, and is used
	//   both by reflexive and logical frame processing functions.
	// - Goals must be done before movement-- the bot has to know where to go
	// - Movement modifiers can only modify movement once it has been created.
	//   They must get run every frame, however, to check for minute changes
	//   that require immediate attention (for example, determining whether
	//   strafe jumping is now acceptable, since the bot just touched ground).
	// - Aim selection must occur after movement, since the aiming might be
	//   needed for strafe jumping.  It must occur every reflexive frame as
	//   well because when the bot's location changes, its desired aim angles
	//   to aim at a specific point can change as well.  This means that
	//   movement selection must also be run every reflexive frame.
	// - Dodging must occur after aiming because the bot cannot dodge if
	//   it decided to strafe jump instead.
	{BotActionMoveSetup, AFC_INGAME | AFC_ALIVE | AFC_PLAY | AFC_RFXLGC},	// Setup movement characteristics
	{BotActionGoal, AFC_INGAME | AFC_ALIVE | AFC_PLAY | AFC_LOGIC},	// Select main and item goals
	{BotActionMoveSelect, AFC_INGAME | AFC_ALIVE | AFC_PLAY | AFC_RFXLGC},	// Determine direction to move
	{BotActionMoveModifiers, AFC_INGAME | AFC_ALIVE | AFC_PLAY | AFC_RFXLGC},	// Modify movement direction
	{BotActionAimSelect, AFC_INGAME | AFC_ALIVE | AFC_PLAY | AFC_RFXLGC},	// Select aim direction
	{BotActionDodge, AFC_INGAME | AFC_ALIVE | AFC_PLAY | AFC_RFXLGC},	// Select dodge direction

	// - Using powers can occur at almost any time before sending commands.
	//   It is put late in the code in case some other modification needs
	//   to do extra processing to decide when to use player powers.
	{BotActionUse, AFC_INGAME | AFC_ALIVE | AFC_PLAY | AFC_LOGIC},	// Use powers and items

	// - The aim view state must updated and then repackaged in the command
	//   structure every server frame, and whenever the bot changes aim selection
	// - Movement processing must occur after all aiming and movement modifiers
	//   (including dodging) have been run.  It also uses the processed view
	//   angles, so it must occur after the view processing.
	// - The test if the bot wants to fire must occur after the aim update,
	//   or the bot will constantly make its fire decisions based on last
	//   frame's aiming (causing a lot of misses for weapons like railgun).
	//   It also must occur after the movement processing, since the server
	//   moves the bot before letting it shoot, so the bot must know how it's
	//   movement will affect its starting fire location.
	// - The actual firing must occur after deciding whether or not to fire
	//   for the next few milliseconds from attack check.  This occurs every
	//   frame because it must occur bot in every logical frame (ie. when the
	//   bot decides to shoot) and in every reflexive frame (ie. when the bot
	//   continues to shoot because it shot last frame).  The actual code is
	//   small so executing it in every AI frame isn't a big speed loss.
	//   And if it is, the code could manually check to only execute during
	//   logical and reflexive frames.
	{BotActionViewUpdate, AFC_INGAME | AFC_ALIVE | AFC_PLAY | AFC_RFXLGC},	// Process view angles
	{BotActionViewProcess, AFC_INGAME | AFC_ALIVE | AFC_PLAY | AFC_RFXLGC},	// Process view angles
	{BotActionMoveProcess, AFC_INGAME | AFC_ALIVE | AFC_PLAY | AFC_RFXLGC},	// Process movement commands
	{BotActionFireUpdate, AFC_INGAME | AFC_ALIVE | AFC_PLAY | AFC_RFXLGC},	// Check if weapon should fire
	{BotActionFireWeapon, AFC_INGAME | AFC_ALIVE | AFC_PLAY | AFC_RFXLGC},	// Fire weapon if necessary

	// - Obviously sending commands must be done last.  Note that it
	//   is done every frame (ie. sending updates) even though command
	//   resets are only done every logical thought frame.
	{BotActionCommandSend, AFC_NONE},	// Send updated commands

	// - End of list marker
	{0, 0}
};


/*
==========
BotActions

Runs any appropriate actions for the given bot.  "ai_elapsed"
is the number of seconds passed since this function was last
called.  "game_elapsed" is the amount of that server_time has
increased (converted from milliseconds to seconds) since this
function was last called for this bot, or 0 if the level has
not been updated since the last action execution.

There are three kinds of actions a human takes:
 - Subconscious (eg. heartbeat)
 - Reflexive (eg. breathing)
 - Logical (eg. walking)

Subconscious actions are ones that can't really be
controlled.  They just happen automatically, and happen
at a very regular pace.  Reflexive actions are things
humans do without really thinking about them, but they
are conscious of them.  People don't really think about
breathing, but they can stop breathing if they choose
to do so.  Logical actions are anything that requires
conscious thought-- where a person wants to move, what
they say, and so on.

Unsurprisingly, the actions a bot must take fall under
similar catagories.  To a bot, subconscious actions are
actions that interface with the rest of the code.  This
includes things like processing where the bot should
look, what commands the bot actually sends to the server,
and so on.  Subconscious actions must execute as often
as possible-- once every time this function is called.

The bot's reflexive actions are reactions to the game
state.  For example, scanning for nearby enemies is a
reaction to the change in enemy locations in the level.
So reflexive actions must execute whenever the level
state changes.  In general, these reactions only need
to trigger once per level frame.  However, it's possible
that some reflexive actions will get processed a second
time if the logical thought changes the kind of reactions
the bot should have (eg. a fight or flight change).

A bot's logical decisions cover pretty much everything
else-- where the bot should move (goal selection), how
it should get there (move selection), and what it should
look at in the mean time (aim selection).  Weapon selection,
firing, and dodging are also part of the logical decisions.
In general, logic decisions require a lot of processing.
As such, they should not execute every AI Frame.  Instead,
logically processing is deferred until a specified amount
of time has accrued.  At that time, all of the logical
decisions will be made for the time past.  This time is
set by the bot_thinktime variable (which counts in
milliseconds).  It's not that useful to set this variable
less than the frame execution time (50 ms), and setting it
too high (maybe 200ms or higher) could create bots that feel
rather stiff.

To summarize bot actions:
 - Subconscious: Executes every AI Frame
 - Reflexive: Executes once per level frame
 - Logical: Executes once per set interval

When this function executes, it determines what kinds of
processing it will do.  Reflexive code will be processed
if "game_elapsed is non-zero".  Similarly, if logic_elapsed
is 0 no logical processing will be done.
==========
*/
void BotActions(bot_state_t * bs, float ai_elapsed, float game_elapsed)
{
	int             i, frames;
	bot_action_args_t args;

	// Make sure this is a valid bot
	if(!bs || !bs->inuse || !bs->ent || !bs->ent->inuse)
		return;

	// Make sure bot's client is connected
	if(bs->ent->client->pers.connected != CON_CONNECTED)
		return;

	// Set up action argument times
	args.ai_elapsed = ai_elapsed;
	args.game_elapsed = game_elapsed;

	// Accrue extra time spent not doing logical thought
	bs->logic_time_ms += floor(args.ai_elapsed * 1000 + 0.5);

	// Determine whether to spend time to spend on logic processing or not
	if(bs->logic_time_ms >= bot_thinktime.integer)
	{
		// Only count whole frames towards the logical think time
		frames = (int)(bs->logic_time_ms / bot_thinktime.integer);
		args.logic_elapsed = frames * bot_thinktime.integer * 0.001;
		bs->logic_time_ms -= frames * bot_thinktime.integer;
	}
	else
	{
		// Do not process a logical thought frame right now
		args.logic_elapsed = 0.0;
	}

	// Compute the action function conditions the bot meets
	args.conditions = AFC_NONE;

	// Check if the bot is in the game
	if(BotInIntermission(bs) || BotIsObserver(bs))
	{
		args.conditions |= AFC_NOGAME;
	}
	else
	{
		args.conditions |= AFC_INGAME;

		// Check if the bot is dead or alive
		if(BotIsDead(bs))
			args.conditions |= AFC_DEAD;
		else
			args.conditions |= AFC_ALIVE;

		// Check if the bot is chatting or playing (possibly waiting to respawn)
		if(bs->chat_time)
			args.conditions |= AFC_CHAT;
		else
			args.conditions |= AFC_PLAY;
	}

	// Check when logical and reflexive frames should get processed
	if(args.game_elapsed)
		args.conditions |= AFC_REFLEX;
	if(args.logic_elapsed)
		args.conditions |= AFC_LOGIC;

	if((args.conditions & AFC_REFLEX) || (args.conditions & AFC_LOGIC))
		args.conditions |= AFC_RFXLGC;

	// Add the delta angles to the bot's current view angles
	//
	// NOTE: The bot's angles must be tracked through bs->ps->delta_angles
	// changes, not bs->ps->viewangles, because multiple bot frames could
	// execute between single frames server frames (or vice versa).  It's
	// really important that bot's new view decisions are based on last frame's
	// view decision, not the server's last reception of such a decision.
	for(i = PITCH; i <= YAW; i++)
	{
		bs->view_now[i].angle.real = AngleNormalize180(bs->view_now[i].angle.real + SHORT2ANGLE(bs->ps->delta_angles[i]));
		bs->view_now[i].angle.error = AngleNormalize180(bs->view_now[i].angle.error + SHORT2ANGLE(bs->ps->delta_angles[i]));
	}

	// Run each action in the action list whose conditions were met
	//
	// FIXME: It's possible to update this logic with some precomputed flow
	// control stuff, like what a compiler would do.  For example, if one
	// condition is missed for reason X and the next five actions all require
	// reason X, the iterator could be incremented six times.  It's not really
	// worth the effort here, however, because this is an outer loop.  The
	// optimization wouldn't provide a noticable speed improvement.
	for(i = 0; bot_actions[i].func; i++)
	{
		// Skip action execution if any of the conditions are not met
		if(bot_actions[i].conditions & ~args.conditions)
			continue;

		// Execute this action
		bot_actions[i].func(bs, &args);
	}

	// Restore the old view angles
	for(i = PITCH; i <= YAW; i++)
	{
		bs->view_now[i].angle.real = AngleNormalize180(bs->view_now[i].angle.real - SHORT2ANGLE(bs->ps->delta_angles[i]));
		bs->view_now[i].angle.error = AngleNormalize180(bs->view_now[i].angle.error - SHORT2ANGLE(bs->ps->delta_angles[i]));
	}
}
