// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_main.c
 *
 * The AI front-end
 *****************************************************************************/

#include "ai_main.h"
#include "ai_vars.h"

#include "ai_attack.h"
#include "ai_client.h"
#include "ai_entity.h"
#include "ai_item.h"
#include "ai_level.h"
#include "ai_motion.h"
#include "ai_path.h"
#include "ai_region.h"
#include "ai_scan.h"
#include "ai_self.h"
#include "ai_team.h"
#include "ai_weapon.h"


/*
===========
BotAI_Print
===========
*/
void QDECL BotAI_Print(int type, char *fmt, ...)
{
	char            str[2048];
	va_list         ap;

	va_start(ap, fmt);
	vsprintf(str, fmt, ap);
	va_end(ap);

	switch (type)
	{
		case PRT_MESSAGE:
		{
			G_Printf("%s", str);
			break;
		}
		case PRT_WARNING:
		{
			G_Printf(S_COLOR_YELLOW "Warning: %s", str);
			break;
		}
		case PRT_ERROR:
		{
			G_Printf(S_COLOR_RED "Error: %s", str);
			break;
		}
		case PRT_FATAL:
		{
			G_Printf(S_COLOR_RED "Fatal: %s", str);
			break;
		}
		case PRT_EXIT:
		{
			G_Error(S_COLOR_RED "Exit: %s", str);
			break;
		}
		default:
		{
			G_Printf("unknown print type\n");
			break;
		}
	}
}

/*
==========================
BotUpdateInfoConfigStrings
==========================
*/
void BotUpdateInfoConfigStrings(void)
{
	int             i;
	bot_state_t    *bs;

	for(i = 0; i < maxclients && i < MAX_CLIENTS; i++)
	{
		// Ignore bots that aren't in use
		bs = bot_states[i];
		if(!bs || !bs->inuse || !bs->ent || !bs->ent->inuse)
			continue;

		// Set the configuration for that bot
		BotSetInfoConfigString(bs);
	}
}

/*
==========
BotAISetup
==========
*/
int BotAISetup(int restart)
{
	// Reset the time tracking variables
	server_time_ms = level.time;
	server_time = server_time_ms * 0.001;
	ai_time_ms = level.time;
	ai_time = ai_time_ms * 0.001;

	// Reload all of the variables associated with the AI
	BotAIVariableSetup();

	// Reset the motion tracking system
	BotAIMotionReset();

	// Initialize player areas
	LevelPlayerAreasReset();

	// Don't reset most data when restarting for tournament mode or map_reset
	if(restart)
		return qtrue;

	// Initialize the bot states
	memset(bot_states, 0, sizeof(bot_states));

	// Initialize the AI library
	return (LevelLibrarySetup() == BLERR_NOERROR);
}

/*
=============
BotAIShutdown
=============
*/
int BotAIShutdown(int restart)
{
	int             i;

	// This is true when the game is just restarted for a tournament
	if(restart)
	{
		// Shutdown all the bots
		for(i = 0; i < MAX_CLIENTS; i++)
		{
			if(bot_states[i] && bot_states[i]->inuse)
				BotAIShutdownClient(bot_states[i]->client, restart);
		}

		// Don't shutdown the bot library
	}
	else
	{
		// Reset the library
		trap_BotLibShutdown();
	}

	return qtrue;
}

/*
===============
BotAIStartFrame
===============
*/
int BotAIStartFrame(int time)
{
	int             i;
	float           server_elapsed, ai_elapsed;

	// Determine how much the game state advanced since the last AI Frame
	server_elapsed = (level.time - server_time_ms) * 0.001;
	server_time_ms = level.time;
	server_time = server_time_ms * 0.001;

	// Determine the amount of time elapsed since the last AI frame ran
	ai_elapsed = (time - ai_time_ms) * 0.001;
	ai_time_ms = time;
	ai_time = ai_time_ms * 0.001;

	// Reload local bot cvars
	LevelUpdateVariables();

	// Spawn bots if the minimum number of players has not been met
	G_CheckBotSpawn();

	// Update the motion tracking system
	//
	// NOTE: Motion tracking must get updated even when the server hasn't
	// advanced because clients can update their position asynchronously.
	BotAIMotionUpdate();

	// Try to setup some stuff if the game state has advanced
	if(server_elapsed)
	{
		// Make sure the precomputed path data is set up
		LevelPathSetup();

		// Make sure the item tables are set up
		LevelItemSetup();

		// Make sure the base goals and flags are set up
		LevelBaseSetup();
	}

	// Have bots report their tasks if requested
	if(bot_report.integer)
	{
		BotTeamplayReport();
		trap_Cvar_Set("bot_report", "0");
		BotUpdateInfoConfigStrings();
	}

	// Update the AI Engine's entity library every time the server updates
	if(server_elapsed)
		LevelLibraryUpdate();

	// Make sure the Area Awareness system is setup, or the bots can't navigate
	if(!trap_AAS_Initialized())
		return qfalse;

	// Recount the number of players on each team
	LevelCountPlayers();

	// Extra processing is needed whenever the game state changes
	if(server_elapsed)
	{
		// Precompute the area number of each client
		LevelPlayerAreasUpdate();

		// Track the traffic the players generate the level's item regions
		LevelPlayerRegionUpdate();

		// Update the lists and areas of all dynamic items, including dropped items
		LevelItemUpdate();

		// Scan the level for changes in flag locations
		LevelFlagScan();
	}

	// Determine which actions each bot should take
	for(i = 0; i < MAX_CLIENTS; i++)
		BotActions(bot_states[i], ai_elapsed, server_elapsed);

	return qtrue;
}

/*
=====================
BotInterbreedEndMatch

NOTE: This function only exists because it's called by a function
in g_main.c.  There's no more need for fuzzy logic interbreeding
because the item pickup code was completely rewritten.
=====================
*/
void BotInterbreedEndMatch()
{
}


#ifdef DEBUG_AI

// Table of debug entries where keys are the flag name and values are
// the flag mask
//
// NOTE: These names must not contain any whitespace.
//
// NOTE: This table is sorted alphabetically so it can be searched
// with bsearch().
//
// FIXME: Currently this structure only supports debug flags.  It's
// possible to extend the interface to accomodate more complicated
// scenarios, such as setting integers or floating points.  Currently
// there is only one such debug code (use_weapon) which is handled
// through special cases.  It's not worth the effort of writing all
// the handling code for just it, but that could change in the future.
#define NUM_BOT_DEBUG_ENTRIES 24
entry_string_int_t bot_debug_entries[NUM_BOT_DEBUG_ENTRIES] = {
	{"info_accstats", BOT_DEBUG_INFO_ACCSTATS},
	{"info_accuracy", BOT_DEBUG_INFO_ACCURACY},
	{"info_aim", BOT_DEBUG_INFO_AIM},
	{"info_awareness", BOT_DEBUG_INFO_AWARENESS},
	{"info_dodge", BOT_DEBUG_INFO_DODGE},
	{"info_enemy", BOT_DEBUG_INFO_ENEMY},
	{"info_firestats", BOT_DEBUG_INFO_FIRESTATS},
	{"info_goal", BOT_DEBUG_INFO_GOAL},
	{"info_item", BOT_DEBUG_INFO_ITEM},
	{"info_item_reason", BOT_DEBUG_INFO_ITEM_REASON},
	{"info_path", BOT_DEBUG_INFO_PATH},
	{"info_scan", BOT_DEBUG_INFO_SCAN},
	{"info_shoot", BOT_DEBUG_INFO_SHOOT},
	{"info_timed_item", BOT_DEBUG_INFO_TIMED_ITEM},
	{"info_weapon", BOT_DEBUG_INFO_WEAPON},

	{"make_dodge_stop", BOT_DEBUG_MAKE_DODGE_STOP},
	{"make_item_stop", BOT_DEBUG_MAKE_ITEM_STOP},
	{"make_move_stop", BOT_DEBUG_MAKE_MOVE_STOP},
	{"make_shoot_always", BOT_DEBUG_MAKE_SHOOT_ALWAYS},
	{"make_shoot_stop", BOT_DEBUG_MAKE_SHOOT_STOP},
	{"make_skill_standard", BOT_DEBUG_MAKE_SKILL_STANDARD},
	{"make_strafejump_stop", BOT_DEBUG_MAKE_STRAFEJUMP_STOP},
	{"make_view_flawless", BOT_DEBUG_MAKE_VIEW_FLAWLESS},
	{"make_view_perfect", BOT_DEBUG_MAKE_VIEW_PERFECT},
};

#endif

/*
==========
BotAIDebug

This function modifies all kinds of settings used to
debug bot behavior.  The input entity is the client
who requested the command change, presumably the
human actually playing the game.
==========
*/
void BotAIDebug(void)
{

#ifdef DEBUG_AI

	int             bot_index, field_index, args, flag;
	char            name[MAX_TOKEN_CHARS];
	char            arg[MAX_TOKEN_CHARS], *field;
	bot_state_t    *bs;
	entry_string_int_t *debug_entry;
	qboolean        all, processed;

	// This command is cheat protected
	if(!g_cheats.integer)
	{
		G_Printf("Cheats are not enabled on this server.\n");
		return;
	}

	// If no arguments were supplied (other than the initial command), print command help
	args = trap_Argc();
	if(args <= 1)
	{
		// Print the basic explanation
		G_Printf("Usage: ai_debug <bot name | all> [[+/-]flag] ...\n"
				 "\n"
				 "If a name is specified, the command applies to all bots matching\n"
				 "that name.  If \"all\" is specified, it applies to all bots instead.\n"
				 "Flags may be preceded by a + or -, which forces the flag on or off.\n"
				 "If no identifier is supplied, the flag is instead toggled.  This\n"
				 "command will also list all flags turned on for each matching bot, even\n"
				 "if no flags were supplied.  The following flags are supported:\n\n");

		// Print list of supported flags
		for(field_index = 0; field_index < NUM_BOT_DEBUG_ENTRIES; field_index++)
			G_Printf("  %s\n", bot_debug_entries[field_index].key);


		// Explain the "use_weapon" syntax
		G_Printf("  use_weapon:<weapon name | all>\n"
				 "\n"
				 "The \"use_weapon\" field is a weapon name (no spaces) or number, not a\n"
				 "flag.  If a real weapon is given, the bot will be given that weapon with\n"
				 "unlimited ammunition and always use it.  If \"all\" is given, the bot\n"
				 "will be given every weapon (except the BFG) with a sizable but limited\n"
				 "ammo supply.  Setting this to 0 turns it off.\n");

		return;
	}

	// Determine which bot will have its information modified or listed
	trap_Argv(1, name, sizeof(name));

	// Check if this command applies to all bots
	all = (Q_stricmp(name, "all") == 0);

	// This flag is true if any processing as done
	processed = qfalse;

	// Apply this command to all bots matching the command criteria
	for(bs = bot_states[bot_index = 0]; bot_index < MAX_CLIENTS; bs = bot_states[++bot_index])
	{
		// Ignore unused bot states
		if(!bs || !bs->inuse || !bs->ent || !bs->ent->inuse)
			continue;

		// Ignore this bot if its name doesn't match and not processing all bots
		if((!all) && (Q_stricmp(name, EntityNameFast(bs->ent)) != 0))
		{
			continue;
		}

		// The name matched somehow
		processed = qtrue;

		// Modify each requested field
		//
		// FIXME: In the "all" case, this code will execute trap_Argc() more
		// times than necessary.  In theory, this code could be rewritten at
		// great cost to only do this once (and hence run faster).
		for(field_index = 2; field_index < args; field_index++)
		{
			// Look up the next argument in the list
			trap_Argv(field_index, arg, sizeof(arg));

			// Check for "use_weapon" syntax
			if(Q_stricmpn(arg, "use_weapon:", 11) == 0)
			{
				// Check for using all weapons
				field = &arg[11];
				if(Q_stricmp(field, "all") == 0)
				{
					bs->use_weapon = -1;
					continue;
				}

				// Translate the weapon name to its associated index
				bs->use_weapon = WeaponFromName(field);
				continue;
			}

			// Skip past the optional starting + or - to get the debug field name
			//
			// NOTE: The + or - is still preserved in arg[0].  If arg[0] is neither
			// of these, the bit will be flipped instead of set to the specified state.
			field = arg;
			if(field[0] == '+' || field[0] == '-')
				field++;

			// Find the debug table entry for this field name
			debug_entry = bsearch(field, bot_debug_entries, NUM_BOT_DEBUG_ENTRIES,
								  sizeof(entry_string_int_t), CompareStringEntryStringInsensitive);
			if(!debug_entry)
			{
				G_Printf("Unknown debug flag: '%s'\n", field);
				continue;
			}


			// Turn on, off, or flip the associated flag
			flag = debug_entry->value;
			if(arg[0] == '+')
				bs->debug_flags |= flag;
			else if(arg[0] == '-')
				bs->debug_flags &= ~flag;
			else
				bs->debug_flags ^= flag;
		}

		// Print out the bot's field data
		G_Printf("%s debug status:\n", EntityNameFast(bs->ent));
		for(field_index = 0; field_index < NUM_BOT_DEBUG_ENTRIES; field_index++)
		{
			// Ignore flags that aren't set
			debug_entry = &bot_debug_entries[field_index];
			if(!(bs->debug_flags & debug_entry->value))
				continue;

			// Print out the set flag
			G_Printf("  %s\n", debug_entry->key);
		}

		// Print the weapon the bot is forced to use if necessary
		if(bs->use_weapon < WP_NONE)
			G_Printf("  use_weapon: All\n");
		else if(bs->use_weapon > WP_NONE && bs->use_weapon < WP_NUM_WEAPONS)
			G_Printf("  use_weapon: %s (%i)\n", WeaponName(bs->use_weapon), bs->use_weapon);
	}

	// Complain about a bad name if necessary
	if(!processed)
		G_Printf("Unknown bot: '%s'\n", name);

#else

	// Tell the user that realtime debugging isn't supported in this compile
	G_Printf("Real-time bot debugging was not compiled into this game server build.\n");
	return;

#endif
}
