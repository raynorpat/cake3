// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_order.h
 *
 * Includes for accessing bot order functions
 *****************************************************************************/

qboolean        BotOrderShouldAnnounce(bot_state_t * bs);
void            BotOrderAnnounceStart(bot_state_t * bs, char *msg_type, gentity_t * recipient, char *arg, char *voicechat);
void            BotOrderReset(bot_state_t * bs);
void            BotOrderAnnounceReset(bot_state_t * bs, char *msg_type, gentity_t * recipient, char *arg);
void            BotLeadReset(bot_state_t * bs);
qboolean        BotMatch_Order(bot_state_t * bs, bot_match_t * match, gentity_t * sender);
qboolean        BotVoiceChatCommand(bot_state_t * bs, int mode, char *voiceChat);
