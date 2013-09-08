// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_chat.h
 *
 * Includes used for bot communication
 *****************************************************************************/

void QDECL QDECL Bot_InitialChat(bot_state_t * bs, char *type, ...);
void            BotChatExitGame(bot_state_t * bs);
void            BotChatEndLevel(bot_state_t * bs);
void            BotChatDeath(bot_state_t * bs);
void            BotChatHitTalking(bot_state_t * bs);
void            BotChatIngame(bot_state_t * bs);
void            BotCheckConsoleMessages(bot_state_t * bs);
void            BotChatTest(bot_state_t * bs);
