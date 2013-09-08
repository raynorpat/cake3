// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_vars.c
 *
 * Functions to manage bot variables
 *****************************************************************************/

#include "ai_main.h"
#include "ai_vars.h"

#include "ai_client.h"
#include "ai_view.h"
#include "ai_weapon.h"


// NOTE: These variables do not use cvars because cvars can be updated during
// gameplay.  These variables are fixed whenever the game is reloaded.
int             gametype;
int             game_style;
int             maxclients;

// Bot states
bot_state_t    *bot_states[MAX_CLIENTS];

// Timestamp of the last executed server frame in seconds and milliseconds
//
// NOTE: server_time_ms is just a copy of level.time.  It's only purpose is
// to standardize the time interface, at least as far as the AI code is concerned.
float           server_time;
int             server_time_ms;

// Timestamps for the current AI frame in seconds and milliseconds
//
// NOTE: These timestamps are only loosely related to other timestamps used
// in the code.  Avoid using these whenever possible.
float           ai_time;
int             ai_time_ms;


// Default bot skill
vmCvar_t        g_spSkill;

// Variables for internal information
vmCvar_t        bot_thinktime;
vmCvar_t        bot_memorydump;
vmCvar_t        bot_saveroutingcache;

// Variables generating extra information
vmCvar_t        bot_report;
vmCvar_t        bot_testsolid;
vmCvar_t        bot_testclusters;

// Variables modifying chat behavior
vmCvar_t        bot_fastchat;
vmCvar_t        bot_nochat;
vmCvar_t        bot_testrchat;

// Variables modifying movement behavior
vmCvar_t        bot_grapple;	// False if bots should never grapple
vmCvar_t        bot_rocketjump;	// False if bots should never rocket jump

// Variables modifying dodging
vmCvar_t        bot_dodge_rate;	// Percent of time to spend dodging when going somewhere and being attacked
vmCvar_t        bot_dodge_min;	// Minimum amount of time to continue dodging in one direction
vmCvar_t        bot_dodge_max;	// Maximum amount of time to continue dodging in one direction

// Variables modifying perception
vmCvar_t        bot_lag_min;	// The minimum amount of lag a bot can have relative to other targets

// Variables modifying item selection
vmCvar_t        bot_item_path_neighbor_weight;	// The weighting between starting and ending regions when computing path

										// neighbors of an item cluster.  0.0 means only consider the start; 1.0
										// means only consider the end.
vmCvar_t        bot_item_predict_time_min;	// The bot will predict for at least this many seconds in its final

										// location when considering a set of item pickups
vmCvar_t        bot_item_change_penalty_time;	// Estimate item pickup will take this much extra time when selecting

										// a different item from last frame.  (Changing movement direction
										// requires extra acceleration and deceleration the travel time estimates
vmCvar_t        bot_item_change_penalty_factor;	// Only select a new cluster if it's this many times as valuable

										// as the currently selected cluster
vmCvar_t        bot_item_autopickup_time;	// Always pickup any item this many seconds or closer


// Variables modifying awareness
vmCvar_t        bot_aware_duration;	// How many seconds the most aware bot remains aware of things
vmCvar_t        bot_aware_skill_factor;	// The least aware bot's awareness is this many times as good as the best
vmCvar_t        bot_aware_refresh_factor;	// Bot may be this many times further away from a target it's already

										// aware of and still refresh its awareness.

// Variables modifying reaction time
vmCvar_t        bot_reaction_min;	// The fastest a bot will start reacting to a change
vmCvar_t        bot_reaction_max;	// The slowest a bot will start reacting to a change

// Variables modifying the focus of the bot's view
vmCvar_t        bot_view_focus_head_dist;	// Bot focuses on the heads of player targets closer than this distance
vmCvar_t        bot_view_focus_body_dist;	// Bot focuses on the bodies of player targets farther than this distance

// Variables modifying the ideal view state's behavior
// NOTE: Changes in bot_view_ideal_error_min/max don't seem to have much effect
vmCvar_t        bot_view_ideal_error_min;	// Minimum ideal view error value as a percentage of target's velocity
vmCvar_t        bot_view_ideal_error_max;	// Maximum ideal view error value as a percentage of target's velocity
vmCvar_t        bot_view_ideal_correct_factor;	// Multiplied by bot's reaction time to produce time to delay between ideal view corrections

// Variables modifying the actual view state's behavior
vmCvar_t        bot_view_actual_accel_min;	// Minimum actual view acceleration in degrees per second
vmCvar_t        bot_view_actual_accel_max;	// Maximum actual view acceleration in degrees per second
vmCvar_t        bot_view_actual_error_min;	// Minimum actual view error value as a percentage of velocity change
vmCvar_t        bot_view_actual_error_max;	// Maximum actual view error value as a percentage of velocity change
vmCvar_t        bot_view_actual_correct_factor;	// Multiplied by bot's reaction time to produce time to delay between actual view corrections

// Variables defining how the bot attacks
vmCvar_t        bot_attack_careless_reload;	// Bots are careless when firing weapons with reload times no greater than this value
vmCvar_t        bot_attack_careless_factor;	// Bots scale targets' bounding boxes by this percent when aiming carelessly
vmCvar_t        bot_attack_careful_factor_min;	// The best bots scale targets' bounding boxes by this percent when aiming carefully
vmCvar_t        bot_attack_careful_factor_max;	// The worst bots scale targets' bounding boxes by this percent when aiming carefully
vmCvar_t        bot_attack_continue_factor;	// Once a bot stops attacking, it continues firing for this many times their reaction time
vmCvar_t        bot_attack_lead_time_full;	// Bots will lead the full distance when the amount of time they need to lead is no more than this
vmCvar_t        bot_attack_lead_time_scale;	// The percentage of time beyond bot_attack_lead_time_full that the bot actually leads

#ifdef DEBUG_AI
// Variables generating debug output
vmCvar_t        bot_debug_path;	// Describe obstacle and path planning setup during start up
vmCvar_t        bot_debug_item;	// Describe item region setup during start up
vmCvar_t        bot_debug_predict_time;	// The amount of time ahead to test predicted player movement
#endif


/*
==================
BotAIVariableSetup
==================
*/
void BotAIVariableSetup(void)
{
	trap_Cvar_Register(&bot_thinktime, "bot_thinktime", "100", CVAR_CHEAT);
	trap_Cvar_Register(&bot_memorydump, "bot_memorydump", "0", CVAR_CHEAT);
	trap_Cvar_Register(&bot_saveroutingcache, "bot_saveroutingcache", "0", CVAR_CHEAT);
	trap_Cvar_Register(&bot_report, "bot_report", "0", CVAR_CHEAT);
	trap_Cvar_Register(&bot_testsolid, "bot_testsolid", "0", CVAR_CHEAT);
	trap_Cvar_Register(&bot_testclusters, "bot_testclusters", "0", CVAR_CHEAT);
}

/*
================
LevelSetGametype

Sets the gametype value and some
other values which depend on it.
================
*/
void LevelSetGametype(int type)
{
	// Only record changes
	if(type == gametype)
		return;

	// Set the type
	gametype = type;

	// Create a bitmask of game information
	switch (gametype)
	{
		default:
		case GT_FFA:
		case GT_TOURNAMENT:
		case GT_SINGLE_PLAYER:
			game_style = 0x0000;
			break;

		case GT_TEAM:
			game_style = GS_TEAM;
			break;

#ifdef MISSIONPACK
		case GT_OBELISK:
			game_style = (GS_TEAM | GS_BASE | GS_DESTROY);
			break;

		case GT_HARVESTER:
			game_style = (GS_TEAM | GS_BASE | GS_CARRIER);
			break;

		case GT_1FCTF:
#endif
		case GT_CTF:
			game_style = (GS_TEAM | GS_BASE | GS_CARRIER | GS_FLAG);
			break;
	}

	// Some weapons work differently in different game modes
	LevelWeaponUpdateGametype();
}

/*
===================
LevelSetupVariables
===================
*/
void LevelSetupVariables(void)
{
	LevelSetGametype(trap_Cvar_VariableIntegerValue("g_gametype"));
	maxclients = trap_Cvar_VariableIntegerValue("sv_maxclients");

	trap_Cvar_Register(&g_spSkill, "g_spSkill", "3", 0);

	trap_Cvar_Register(&bot_fastchat, "bot_fastchat", "0", 0);
	trap_Cvar_Register(&bot_nochat, "bot_nochat", "0", 0);
	trap_Cvar_Register(&bot_testrchat, "bot_testrchat", "0", 0);

	trap_Cvar_Register(&bot_grapple, "bot_grapple", "0", CVAR_CHEAT);
	trap_Cvar_Register(&bot_rocketjump, "bot_rocketjump", "1", CVAR_CHEAT);

	trap_Cvar_Register(&bot_dodge_rate, "bot_dodge_rate", "0.35", CVAR_CHEAT);
	trap_Cvar_Register(&bot_dodge_min, "bot_dodge_min", "0.60", CVAR_CHEAT);
	trap_Cvar_Register(&bot_dodge_max, "bot_dodge_max", "1.00", CVAR_CHEAT);

	trap_Cvar_Register(&bot_lag_min, "bot_lag_min", "0.050", CVAR_CHEAT);

	trap_Cvar_Register(&bot_item_path_neighbor_weight, "bot_item_path_neighbor_weight", "0.35", CVAR_CHEAT);
	trap_Cvar_Register(&bot_item_predict_time_min, "bot_item_predict_time_min", "20.0", CVAR_CHEAT);
	trap_Cvar_Register(&bot_item_change_penalty_time, "bot_item_change_penalty_time", "1.0", CVAR_CHEAT);
	trap_Cvar_Register(&bot_item_change_penalty_factor, "bot_item_change_penalty_factor", "1.2", CVAR_CHEAT);
	trap_Cvar_Register(&bot_item_autopickup_time, "bot_item_autopickup_time", "1.0", CVAR_CHEAT);

	trap_Cvar_Register(&bot_aware_duration, "bot_aware_duration", "5.0", CVAR_CHEAT);
	trap_Cvar_Register(&bot_aware_skill_factor, "bot_aware_skill_factor", "0.5", CVAR_CHEAT);
	trap_Cvar_Register(&bot_aware_refresh_factor, "bot_aware_refresh_factor", "2.0", CVAR_CHEAT);

	// All of these constants are very touchy.  Modify at your own risk!
	// NOTE: Changing reaction time and acceleration min/max (how fast the
	// bot moves its virtual mouse) will have the biggest impact on accuracy.
	// Reaction time
	trap_Cvar_Register(&bot_reaction_min, "bot_reaction_min", "0.120", CVAR_CHEAT);
	trap_Cvar_Register(&bot_reaction_max, "bot_reaction_max", "0.280", CVAR_CHEAT);

	// View focus
	trap_Cvar_Register(&bot_view_focus_head_dist, "bot_view_focus_head_dist", "256.0", CVAR_CHEAT);
	trap_Cvar_Register(&bot_view_focus_body_dist, "bot_view_focus_body_dist", "512.0", CVAR_CHEAT);

	// Ideal view modification
	trap_Cvar_Register(&bot_view_ideal_error_min, "bot_view_ideal_error_min", "0.0", CVAR_CHEAT);
	trap_Cvar_Register(&bot_view_ideal_error_max, "bot_view_ideal_error_max", "0.5", CVAR_CHEAT);
	trap_Cvar_Register(&bot_view_ideal_correct_factor, "bot_view_ideal_correct_factor", "3.0", CVAR_CHEAT);

	// Actual view modification
	trap_Cvar_Register(&bot_view_actual_accel_min, "bot_view_actual_accel_min", "800.0", CVAR_CHEAT);
	trap_Cvar_Register(&bot_view_actual_accel_max, "bot_view_actual_accel_max", "1500.0", CVAR_CHEAT);
	trap_Cvar_Register(&bot_view_actual_error_min, "bot_view_actual_error_min", "0.00", CVAR_CHEAT);
	trap_Cvar_Register(&bot_view_actual_error_max, "bot_view_actual_error_max", "1.00", CVAR_CHEAT);
	trap_Cvar_Register(&bot_view_actual_correct_factor, "bot_view_actual_correct_factor", "1.0", CVAR_CHEAT);

	// Attack
	trap_Cvar_Register(&bot_attack_careless_reload, "bot_attack_careless_reload", "0.5", CVAR_CHEAT);
	trap_Cvar_Register(&bot_attack_careless_factor, "bot_attack_careless_factor", "5.0", CVAR_CHEAT);
	trap_Cvar_Register(&bot_attack_careful_factor_min, "bot_attack_careful_factor_min", "1.0", CVAR_CHEAT);
	trap_Cvar_Register(&bot_attack_careful_factor_max, "bot_attack_careful_factor_max", "2.0", CVAR_CHEAT);
	trap_Cvar_Register(&bot_attack_continue_factor, "bot_attack_continue_factor", "2.5", CVAR_CHEAT);
	trap_Cvar_Register(&bot_attack_lead_time_full, "bot_attack_lead_time_full", "0.50", CVAR_CHEAT);
	trap_Cvar_Register(&bot_attack_lead_time_scale, "bot_attack_lead_time_scale", "0.20", CVAR_CHEAT);

#ifdef DEBUG_AI
	// Turn these before startup to see precomputed data structure information
	trap_Cvar_Register(&bot_debug_path, "bot_debug_path", "0", CVAR_CHEAT);
	trap_Cvar_Register(&bot_debug_item, "bot_debug_item", "0", CVAR_CHEAT);
	trap_Cvar_Register(&bot_debug_predict_time, "bot_debug_predict_time", "0.0", CVAR_CHEAT);
#endif
}

/*
====================
LevelUpdateVariables

Updates any variables that may have
changed since last frame
====================
*/
void LevelUpdateVariables(void)
{
	// First reread a whole ton of variables
	trap_Cvar_Update(&bot_thinktime);
	trap_Cvar_Update(&bot_memorydump);
	trap_Cvar_Update(&bot_saveroutingcache);

	trap_Cvar_Update(&bot_fastchat);
	trap_Cvar_Update(&bot_nochat);
	trap_Cvar_Update(&bot_testrchat);

	trap_Cvar_Update(&bot_report);

	trap_Cvar_Update(&bot_grapple);
	trap_Cvar_Update(&bot_rocketjump);

	trap_Cvar_Update(&bot_dodge_rate);
	trap_Cvar_Update(&bot_dodge_min);
	trap_Cvar_Update(&bot_dodge_max);

	trap_Cvar_Update(&bot_lag_min);

	trap_Cvar_Update(&bot_item_path_neighbor_weight);
	trap_Cvar_Update(&bot_item_predict_time_min);
	trap_Cvar_Update(&bot_item_change_penalty_time);
	trap_Cvar_Update(&bot_item_change_penalty_factor);
	trap_Cvar_Update(&bot_item_autopickup_time);

	trap_Cvar_Update(&bot_aware_duration);
	trap_Cvar_Update(&bot_aware_skill_factor);
	trap_Cvar_Update(&bot_aware_refresh_factor);

	trap_Cvar_Update(&bot_reaction_min);
	trap_Cvar_Update(&bot_reaction_max);

	trap_Cvar_Update(&bot_view_focus_head_dist);
	trap_Cvar_Update(&bot_view_focus_body_dist);

	trap_Cvar_Update(&bot_view_ideal_error_min);
	trap_Cvar_Update(&bot_view_ideal_error_max);
	trap_Cvar_Update(&bot_view_ideal_correct_factor);

	trap_Cvar_Update(&bot_view_actual_accel_min);
	trap_Cvar_Update(&bot_view_actual_accel_max);
	trap_Cvar_Update(&bot_view_actual_error_min);
	trap_Cvar_Update(&bot_view_actual_error_max);
	trap_Cvar_Update(&bot_view_actual_correct_factor);

	trap_Cvar_Update(&bot_attack_careless_reload);
	trap_Cvar_Update(&bot_attack_careless_factor);
	trap_Cvar_Update(&bot_attack_careful_factor_min);
	trap_Cvar_Update(&bot_attack_careful_factor_max);
	trap_Cvar_Update(&bot_attack_continue_factor);
	trap_Cvar_Update(&bot_attack_lead_time_full);
	trap_Cvar_Update(&bot_attack_lead_time_scale);

#ifdef DEBUG_AI
	trap_Cvar_Update(&bot_debug_path);
	trap_Cvar_Update(&bot_debug_item);
	trap_Cvar_Update(&bot_debug_predict_time);
#endif

	// Handle some internal AI variable sets
	if(bot_memorydump.integer)
	{
		trap_BotLibVarSet("memorydump", "1");
		trap_Cvar_Set("bot_memorydump", "0");
	}
	if(bot_saveroutingcache.integer)
	{
		trap_BotLibVarSet("saveroutingcache", "1");
		trap_Cvar_Set("bot_saveroutingcache", "0");
	}

	// Handle anything required by think time changes
	LevelUpdateThinkTime();

	// Cache bot reaction times if the reaction time min or max changed
	LevelCacheReactionTimes();
}
