// All portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_command.h
 *
 * Includes used for processing commands
 *****************************************************************************/

void            BotCommandAction(bot_state_t * bs, int action);
void            BotCommandWeapon(bot_state_t * bs, int weapon);
void            MoveCmdToViewAngles(usercmd_t * cmd, int *delta, vec3_t view);
void            ViewAnglesToMoveAxies(vec3_t view, vec3_t * axies, int physics);
qboolean        MoveCmdViewToDir(usercmd_t * cmd, vec3_t view, vec3_t move_dir, int physics);
void            ClientViewDir(gclient_t * client, vec3_t dir);
float           MoveCmdToDesiredDir(usercmd_t * cmd, vec3_t * axies, physics_t * physics,
									float max_speed, float water_level, vec3_t move_dir);
void            BotCommandMove(bot_state_t * bs, vec3_t move_dir, float speed_rate, int jump_crouch);
void            BotCommandView(bot_state_t * bs, vec3_t view);
qboolean        BotAddresseeMatch(bot_state_t * bs, bot_match_t * match);
qboolean        BotMatchMessage(bot_state_t * bs, char *message);
