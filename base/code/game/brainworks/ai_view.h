// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_view.h
 *
 * Includes used to modify bot view angles
 *****************************************************************************/

void            ViewReset(view_axis_t * view, vec3_t angles);
void            ViewAnglesReal(view_axis_t * view, vec3_t angles);
void            ViewAnglesPerceived(view_axis_t * view, vec3_t angles);
void            BotViewCorrectIdeal(bot_state_t * bs);
void            BotViewCorrectActual(bot_state_t * bs);
int             ViewSpeedsChanged(vec3_t old_speed, vec3_t new_speed);
void            BotViewIdealUpdate(bot_state_t * bs, vec3_t view_angles, vec3_t view_speeds, vec3_t ref_angles, int changes);
void            BotViewUpdate(bot_state_t * bs);
void            BotViewProcess(bot_state_t * bs);
