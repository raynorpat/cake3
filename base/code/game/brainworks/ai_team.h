// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_team.h
 *
 * Includes used for team goal selection and communication
 *****************************************************************************/

// Teamplay task preferences-- used both for the bot's self and by the team leader for teammates
#define TASKPREF_ROAMER					0x00	// No bits-- can be anything
#define TASKPREF_DEFENDER				0x01
#define TASKPREF_ATTACKER				0x02

// Flag status
typedef enum
{
	FS_MISSING = 0,
	FS_AT_HOME,
	FS_CARRIER,
	FS_DROPPED
} flag_status_t;

void            BotTeamplayReport(void);
qboolean        BotPreferAttacker(bot_state_t * bs);
qboolean        BotPreferDefender(bot_state_t * bs);

#ifdef MISSIONPACK
void            BotUpdateTaskPreference(bot_state_t * bs);
#endif
void            BotTeamLeaderStart(bot_state_t * bs, gentity_t * leader);
void            BotTeamLeaderStop(bot_state_t * bs, gentity_t * leader);
void            BotCheckLeader(bot_state_t * bs);
void            BotSetTeammatePreference(bot_state_t * bs, gentity_t * teammate, int pref);
void            BotVoiceChat(bot_state_t * bs, int toclient, char *voicechat);
void            BotVoiceChatOnly(bot_state_t * bs, int toclient, char *voicechat);
void            BotTeamAI(bot_state_t * bs);
qboolean        BotMatch_Team(bot_state_t * bs, bot_match_t * match, gentity_t * sender);
