// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_scan.h
 *
 * Includes used for scanning for nearby enemies, teammates, and so on
 *****************************************************************************/

#define SCAN_PLAYER_EVENT		0x01	// Process events on nearby players
#define SCAN_NONPLAYER_EVENT	0x02	// Process events on nearby non-player entities,
										// including audial awareness
#define SCAN_TARGET				0x04	// Scan anything that someone might consider a target
										// (teammates and players, plus destructable objects).
										// Check for aim enemies, visual awareness, carriers,
										// and a count of nearby players.  Also check if the bot
										// was damaged and update all region traffic information
										// based on the targets the bot saw.
#define SCAN_MISSILE			0x08	// Scan for missiles launched this frame, as well as
										// kamikaze bodys to blow up.

// Everything needed for the awareness engine (which is used to select goal enemies)
#define SCAN_AWARENESS	(SCAN_PLAYER_EVENT|SCAN_NONPLAYER_EVENT|SCAN_TARGET)

// These scans can never be avoided, even when called multiple times per game frame
// NOTE: The reason player events can never be avoided is that sometimes the ClientThink()
// code adds an event after the game frame processing and first AI frame processes.
// If the code doesn't scan, important things like player footsteps can be forgotten.
#define SCAN_CONTINUAL	(SCAN_PLAYER_EVENT)

// Everything that makes up scanning
#define SCAN_ALL		(SCAN_PLAYER_EVENT|SCAN_NONPLAYER_EVENT|SCAN_TARGET|SCAN_MISSILE)

void            BotScan(bot_state_t * bs, int scan_mode);
