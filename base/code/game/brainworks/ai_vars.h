// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_vars.h
 *
 * Includes for accessing bot variables
 *****************************************************************************/

extern int      gametype;
extern int      game_style;
extern int      maxclients;

// Different styles of gameplay
#define GS_TEAM		0x0001		// Game has teamplay
#define GS_BASE		0x0002		// Game has bases of some kind
#define GS_CARRIER	0x0004		// Game supports carriers of some kind
#define GS_FLAG		0x0008		// Game includes flags
#define GS_DESTROY	0x0010		// Game includes destructable objects

extern bot_state_t *bot_states[MAX_CLIENTS];

extern float    server_time;
extern int      server_time_ms;

extern float    ai_time;
extern int      ai_time_ms;

extern vmCvar_t g_spSkill;

extern vmCvar_t bot_thinktime;
extern vmCvar_t bot_memorydump;
extern vmCvar_t bot_saveroutingcache;

extern vmCvar_t bot_report;
extern vmCvar_t bot_testsolid;
extern vmCvar_t bot_testclusters;

extern vmCvar_t bot_fastchat;
extern vmCvar_t bot_nochat;
extern vmCvar_t bot_testrchat;

extern vmCvar_t bot_grapple;
extern vmCvar_t bot_rocketjump;

extern vmCvar_t bot_dodge_rate;
extern vmCvar_t bot_dodge_min;
extern vmCvar_t bot_dodge_max;

extern vmCvar_t bot_lag_min;

extern vmCvar_t bot_item_path_neighbor_weight;
extern vmCvar_t bot_item_predict_time_min;
extern vmCvar_t bot_item_change_penalty_time;
extern vmCvar_t bot_item_change_penalty_factor;
extern vmCvar_t bot_item_autopickup_time;

extern vmCvar_t bot_aware_duration;
extern vmCvar_t bot_aware_skill_factor;
extern vmCvar_t bot_aware_refresh_factor;

extern vmCvar_t bot_reaction_min;
extern vmCvar_t bot_reaction_max;

extern vmCvar_t bot_view_focus_head_dist;
extern vmCvar_t bot_view_focus_body_dist;

extern vmCvar_t bot_view_ideal_error_min;
extern vmCvar_t bot_view_ideal_error_max;
extern vmCvar_t bot_view_ideal_correct_factor;

extern vmCvar_t bot_view_actual_accel_min;
extern vmCvar_t bot_view_actual_accel_max;
extern vmCvar_t bot_view_actual_error_min;
extern vmCvar_t bot_view_actual_error_max;
extern vmCvar_t bot_view_actual_correct_factor;

extern vmCvar_t bot_attack_careless_reload;
extern vmCvar_t bot_attack_careless_factor;
extern vmCvar_t bot_attack_careful_factor_min;
extern vmCvar_t bot_attack_careful_factor_max;
extern vmCvar_t bot_attack_continue_factor;
extern vmCvar_t bot_attack_lead_time_full;
extern vmCvar_t bot_attack_lead_time_scale;

#ifdef DEBUG_AI

extern vmCvar_t bot_debug_path;
extern vmCvar_t bot_debug_item;
extern vmCvar_t bot_debug_predict_time;

#endif



void            BotAIVariableSetup(void);
void            LevelSetupVariables(void);
void            LevelUpdateVariables(void);
