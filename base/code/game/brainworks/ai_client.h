// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_client.h
 *
 * Includes used for accessing client data
 *****************************************************************************/

// Number of bots currently in the game
extern int      bots_connected;

void            LevelBotThinkSchedule(void);
void            LevelCountPlayers(void);
int             LevelNumTeams(void);
void            LevelCacheReactionTimes(void);
int             BotEnemies(bot_state_t * bs);
int             BotTeammates(bot_state_t * bs);
void            LevelPlayerAreasReset(void);
void            LevelPlayerAreasUpdate(void);
int             PlayerArea(gentity_t * ent);
char           *ClientSkin(int client, char *skin, int size);
gentity_t      *TeammateFromName(bot_state_t * bs, char *name);
gentity_t      *EnemyFromName(bot_state_t * bs, char *name);
qboolean        BotSameTeam(bot_state_t * bs, gentity_t * ent);
qboolean        BotEnemyTeam(bot_state_t * bs, gentity_t * ent);
qboolean        BotChaseEnemy(bot_state_t * bs, gentity_t * ent);
qboolean        BotIsAlone(bot_state_t * bs);
qboolean        BotIsFirstInRankings(bot_state_t * bs);
qboolean        BotIsLastInRankings(bot_state_t * bs);
char           *BotFirstClientInRankings(void);
char           *BotLastClientInRankings(void);
char           *BotRandomOpponentName(bot_state_t * bs);

// NOTE: ai_client.c also includes the functions BotAISetupClient() and
// BotAIShutdownClient(), but those are called from the server code and
// defined by the header file g_local.h.
