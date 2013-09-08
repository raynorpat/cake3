// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_self.c
 *
 * Functions that the bot uses to get/set basic information about itself
 *****************************************************************************/

#include "ai_main.h"
#include "ai_vars.h"
#include "ai_self.h"

#include "ai_accuracy.h"
#include "ai_client.h"
#include "ai_entity.h"
#include "ai_level.h"
#include "ai_order.h"
#include "ai_pickup.h"
#include "ai_waypoint.h"
#include "ai_weapon.h"


/*
==============
BotSetUserInfo
==============
*/
void BotSetUserInfo(bot_state_t * bs, char *key, char *value)
{
	char            userinfo[MAX_INFO_STRING];

	trap_GetUserinfo(bs->client, userinfo, sizeof(userinfo));
	Info_SetValueForKey(userinfo, key, value);
	trap_SetUserinfo(bs->client, userinfo);
	ClientUserinfoChanged(bs->client);
}

/*
============
BotIsCarrier
============
*/
qboolean BotIsCarrier(bot_state_t * bs)
{
	switch (gametype)
	{
		case GT_CTF:
			return (bs->ps->powerups[PW_REDFLAG] || bs->ps->powerups[PW_BLUEFLAG]);
#ifdef MISSIONPACK
		case GT_1FCTF:
			return (bs->ps->powerups[PW_NEUTRALFLAG]);
		case GT_HARVESTER:
			return (bs->ps->generic1 > 0);
#endif
	}

	return qfalse;
}

/*
=======
BotTeam
=======
*/
int BotTeam(bot_state_t * bs)
{
	return bs->ent->client->sess.sessionTeam;
}

/*
===========
BotTeamBase
===========
*/
int BotTeamBase(bot_state_t * bs)
{
	// Some game types don't have bases defined
	if(!(game_style & GS_BASE))
		return -1;

	switch (BotTeam(bs))
	{
		case TEAM_RED:
			return RED_BASE;
		case TEAM_BLUE:
			return BLUE_BASE;
		default:
			return -1;
	}
}

/*
============
BotEnemyBase
============
*/
int BotEnemyBase(bot_state_t * bs)
{
	// Some game types don't have bases defined
	if(!(game_style & GS_BASE))
		return -1;

	switch (BotTeam(bs))
	{
		case TEAM_RED:
			return BLUE_BASE;
		case TEAM_BLUE:
			return RED_BASE;
		default:
			return -1;
	}
}

/*
==============
BotCaptureBase

Where the bot can take an item to capture it for poins
==============
*/
int BotCaptureBase(bot_state_t * bs)
{
	// Some game types don't have bases defined
	if(!(game_style & GS_BASE) || !(game_style & GS_CARRIER))
		return -1;

	// Different gametypes have different kinds of captures
	switch (gametype)
	{
			// CTF Flags are returned at their opposing base
		case GT_CTF:
			if(bs->ps->powerups[PW_REDFLAG])
				return BLUE_BASE;
			if(bs->ps->powerups[PW_BLUEFLAG])
				return RED_BASE;
			break;

#ifdef MISSIONPACK
			// Capture the flag at the enemy base
		case GT_1FCTF:
			if(bs->ps->powerups[PW_NEUTRALFLAG])
				return BotEnemyBase(bs);
			break;

			// Capture heads at the enemy base
		case GT_HARVESTER:
			if(bs->ps->generic1 > 0)
				return BotEnemyBase(bs);
			break;
#endif
	}

	// No base exists that the bot can capture at
	return -1;
}

/*
============
BotBothBases
============
*/
void BotBothBases(bot_state_t * bs, int *us, int *them)
{
	// Some game types don't have bases defined
	if(!(game_style & GS_BASE))
	{
		*us = -1;
		*them = -1;
	}
	else if(BotTeam(bs) == TEAM_RED)
	{
		*us = RED_BASE;
		*them = BLUE_BASE;
	}
	else
	{
		*us = BLUE_BASE;
		*them = RED_BASE;
	}
}

/*
=================
BotSynonymContext
=================
*/
int BotSynonymContext(bot_state_t * bs)
{
	int             context;

	context = CONTEXT_NORMAL | CONTEXT_NEARBYITEM | CONTEXT_NAMES;

	if(game_style & GS_FLAG)
	{
		if(BotTeam(bs) == TEAM_RED)
			context |= CONTEXT_CTFREDTEAM;
		else
			context |= CONTEXT_CTFBLUETEAM;
	}

#ifdef MISSIONPACK
	else if(gametype == GT_OBELISK)
	{
		if(BotTeam(bs) == TEAM_RED)
			context |= CONTEXT_OBELISKREDTEAM;
		else
			context |= CONTEXT_OBELISKBLUETEAM;
	}

	else if(gametype == GT_HARVESTER)
	{
		if(BotTeam(bs) == TEAM_RED)
			context |= CONTEXT_HARVESTERREDTEAM;
		else
			context |= CONTEXT_HARVESTERBLUETEAM;
	}
#endif

	return context;
}

/*
=========
BotIsDead
=========
*/
qboolean BotIsDead(bot_state_t * bs)
{
	return (bs->ps->pm_type == PM_DEAD);
}

/*
=============
BotIsObserver
=============
*/
qboolean BotIsObserver(bot_state_t * bs)
{
	// Double checked for accuracy!
	if(bs->ps->pm_type == PM_SPECTATOR)
		return qtrue;

	return (BotTeam(bs) == TEAM_SPECTATOR);
}

/*
=================
BotInIntermission
=================
*/
qboolean BotInIntermission(bot_state_t * bs)
{
	if(level.intermissiontime)
		return qtrue;

	return (bs->ps->pm_type == PM_FREEZE || bs->ps->pm_type == PM_INTERMISSION);
}

/*
===================
BotShouldRocketJump
===================
*/
qboolean BotShouldRocketJump(bot_state_t * bs)
{
	float           rocketjumper;

	// Don't rocket jump if the server turned it off for bots
	if(!bot_rocketjump.integer)
		return qfalse;

	// The bot must have a rocket launcher with sufficient ammo
	if(!BotHasWeapon(bs, WP_ROCKET_LAUNCHER, 3))
		return qfalse;

	// Damage related checks don't matter if the bot has a battle suit
	if(!bs->ps->powerups[PW_BATTLESUIT])
	{
		// Rocket jumping with the Quad is too painful
		if(bs->ps->powerups[PW_QUAD])
			return qfalse;

		// Don't jump if the bot is too hurt
		if(EntityHealth(bs->ent) < 100)
			return qfalse;
	}
	// Be willing to rocket jump if the bot likes doing so
	rocketjumper = trap_Characteristic_BFloat(bs->character, CHARACTERISTIC_WEAPONJUMPING, 0, 1);
	return (rocketjumper >= 0.5);
}

/*
==============
BotEnemyHealth

Returns the bot's estimate of the aim enemy's health, as defined as
"the amount of damage the bot must deal to kill this enemy".
==============
*/
int BotEnemyHealth(bot_state_t * bs)
{
	int             health;

	// Unskilled bots don't track health at all
	if(!bs->aim_enemy || bs->settings.skill <= 1)
		return 125;

	// Very skilled bots are so good at tracking health that "It's Almost Like They Cheat!(tm)"
	// Also, there is enough feedback for non-player targets that it's okay to give the absolute
	// health value.
	if(bs->settings.skill >= 4 || !bs->aim_enemy->client)
		health = EntityHealth(bs->aim_enemy);

	// Average skilled bots rely on pain sounds
	else
		health = bs->enemy_health;


	// Battlesuit prevents half damage, so that's like having double health
	if(bs->aim_enemy->s.powerups & (1 << PW_BATTLESUIT))
		health *= 2;

	// Just in case
	if(health <= 0)
		health = 1;
	return health;
}

/*
=================
BotEnemyHealthSet
=================
*/
void BotEnemyHealthSet(bot_state_t * bs, int health)
{
	// Actual health value is coarsened, since human players don't have this precise information
	bs->enemy_health = (health / 25) * 25;
}

/*
==============
BotAimEnemySet

Sets the bot's aim enemy to the inputted enemy and
copies the inputted combat zone description for that
enemy.  If enemy is NULL, the bot's combat zone is
instead reset to a default zone (and the input zone,
which may be NULL, is ignored).  "sighted" is the
time at which the target was first sighted, or -1 if
the target is not currently in line of sight.

NOTE: The combat zone will get copied over even when
the input enemy is the bot's current enemy, but
additional fields will get reset when the input enemy
is a change.  So it's important to call this function
when either the aim enemy or the enemy's combat zone
changes.
==============
*/
void BotAimEnemySet(bot_state_t * bs, gentity_t * enemy, combat_zone_t * zone)
{
	// If the aim enemy changed, update some related data
	if(bs->aim_enemy != enemy)
	{
		// Store the new enemy and their estimate health
		bs->aim_enemy = enemy;
		bs->enemy_health = 125;

		// Look up their last known movement decision
		if(enemy && enemy->client)
			ClientViewDir(enemy->client, bs->aim_enemy_move_dir);

#ifdef DEBUG_AI
		if(bs->debug_flags & BOT_DEBUG_INFO_ENEMY)
			BotAI_Print(PRT_MESSAGE, "%s: Aim Enemy: %s\n", EntityNameFast(bs->ent), EntityNameFast(bs->aim_enemy));
#endif
	}

	// Update the enemy's combat zone if an enemy exists; otherwise use the last
	// enemy's zone as the default.
	//
	// NOTE: It's likely the bot will make shots after it kills an enemy because
	// the bots continue firing for a few milliseconds after they decide to stop.
	// This code will reset the aim enemy as soon as the target dies, but shots
	// will occur afterwards (and probably miss).  Those misses should get applied
	// to the combat zone the enemy was in at the time of attack decision.
	if(enemy)
		memcpy(&bs->aim_zone, zone, sizeof(combat_zone_t));
}

/*
======================
BotSetInfoConfigString
======================
*/
void BotSetInfoConfigString(bot_state_t * bs)
{
	char           *leader, *action, carrying[32], *cs;
	bot_goal_t     *goal;
	gentity_t      *ent;

	leader = (bs->ent == bs->leader ? "L" : " ");

	strcpy(carrying, "  ");
	if(BotIsCarrier(bs))
	{
#ifdef MISSIONPACK
		if(gametype == GT_HARVESTER)
			Com_sprintf(carrying, sizeof(carrying), "%2d", bs->ps->generic1);
		else
#endif
			strcpy(carrying, "F ");
	}

	ent = NULL;
	goal = NULL;
	switch (bs->order_type)
	{
		case ORDER_ATTACK:
			action = "attacking";
			ent = bs->order_enemy;
			break;
		case ORDER_HELP:
			action = "helping";
			ent = bs->help_teammate;
			break;
		case ORDER_ACCOMPANY:
			action = "accompanying";
			ent = bs->accompany_teammate;
			break;

		case ORDER_DEFEND:
			action = "defending";
			goal = &bs->defend_goal;
			break;
		case ORDER_ITEM:
			action = "getting item";
			goal = &bs->inspect_goal;
			break;

		case ORDER_GETFLAG:
			action = "getting the flag";
			break;
		case ORDER_RETURNFLAG:
			action = "returning the flag";
			break;
		case ORDER_HARVEST:
			action = "harvesting";
			break;
		case ORDER_ASSAULT:
			action = "assaulting the enemy base";
			break;
		case ORDER_CAMP:
			action = "camping";
			break;
		case ORDER_PATROL:
			action = "patrolling";
			break;

		default:
		case ORDER_NONE:
			action = "roaming";
			break;
	}

	if(ent)
		cs = va("l\\%s\\c\\%s\\a\\%s %s", leader, carrying, action, SimplifyName(EntityNameFast(ent)));
	else if(goal)
		cs = va("l\\%s\\c\\%s\\a\\%s %s", leader, carrying, action, GoalNameFast(goal));
	else
		cs = va("l\\%s\\c\\%s\\a\\%s", leader, carrying, action);

	trap_SetConfigstring(CS_BOTINFO + bs->client, cs);
}

/*
==========================
RemoveColorEscapeSequences
==========================
*/
void RemoveColorEscapeSequences(char *text)
{
	int             i, l;

	l = 0;
	for(i = 0; text[i]; i++)
	{
		if(Q_IsColorString(&text[i]))
		{
			i++;
			continue;
		}
		if(text[i] > 0x7E)
			continue;
		text[l++] = text[i];
	}
	text[l] = '\0';
}

/*
======================
BotCheckServerCommands
======================
*/
void BotCheckServerCommands(bot_state_t * bs)
{
	char            buf[1024], *args;

	while(trap_BotGetServerCommand(bs->client, buf, sizeof(buf)))
	{
		//have buf point to the command and args to the command arguments
		args = strchr(buf, ' ');
		if(!args)
			continue;
		*args++ = '\0';

		//remove color espace sequences from the arguments
		RemoveColorEscapeSequences(args);

		if(!Q_stricmp(buf, "cp "))
		{						/*CenterPrintf */
		}
		else if(!Q_stricmp(buf, "cs"))
		{						/*ConfigStringModified */
		}
		else if(!Q_stricmp(buf, "print"))
		{
			//remove first and last quote from the chat message
			memmove(args, args + 1, strlen(args));
			args[strlen(args) - 1] = '\0';
			trap_BotQueueConsoleMessage(bs->cs, CMS_NORMAL, args);
		}
		else if(!Q_stricmp(buf, "chat"))
		{
			//remove first and last quote from the chat message
			memmove(args, args + 1, strlen(args));
			args[strlen(args) - 1] = '\0';
			trap_BotQueueConsoleMessage(bs->cs, CMS_CHAT, args);
		}
		else if(!Q_stricmp(buf, "tchat"))
		{
			//remove first and last quote from the chat message
			memmove(args, args + 1, strlen(args));
			args[strlen(args) - 1] = '\0';
			trap_BotQueueConsoleMessage(bs->cs, CMS_CHAT, args);
		}
#ifdef MISSIONPACK
		else if(!Q_stricmp(buf, "vchat"))
		{
			BotVoiceChatCommand(bs, SAY_ALL, args);
		}
		else if(!Q_stricmp(buf, "vtchat"))
		{
			BotVoiceChatCommand(bs, SAY_TEAM, args);
		}
		else if(!Q_stricmp(buf, "vtell"))
		{
			BotVoiceChatCommand(bs, SAY_TELL, args);
		}
#endif
		else if(!Q_stricmp(buf, "scores"))
		{						/*FIXME: parse scores? */
		}
		else if(!Q_stricmp(buf, "clientLevelShot"))
		{						/*ignore */
		}
	}
}

/*
==================
BotWeaponCharsLoad

Load and store the weapon characteristics for
the specified weapon.
==================
*/
void BotWeaponCharsLoad(bot_state_t * bs, int weapon)
{
	int             acc_char, skill_char;

#ifdef DEBUG_AI
	// Use standardized accuracies and weapon skills if requested
	if(bs->debug_flags & BOT_DEBUG_MAKE_SKILL_STANDARD)
	{
		switch ((int)(bs->settings.skill + 0.5))
		{
			default:
			case 5:
				bs->weapon_char_acc[weapon] = 1.0;
				bs->weapon_char_skill[weapon] = 1.0;
				break;
			case 4:
				bs->weapon_char_acc[weapon] = 0.65;
				bs->weapon_char_skill[weapon] = 0.65;
				break;
			case 3:
				bs->weapon_char_acc[weapon] = 0.40;
				bs->weapon_char_skill[weapon] = 0.40;
				break;
			case 2:
				bs->weapon_char_acc[weapon] = 0.25;
				bs->weapon_char_skill[weapon] = 0.25;
				break;
			case 1:
				bs->weapon_char_acc[weapon] = 0.12;
				bs->weapon_char_skill[weapon] = 0.12;
				break;
		}
		return;
	}
#endif

	// Most weapons have different accuracies and skill characteristics
	switch (weapon)
	{
		case WP_MACHINEGUN:
			acc_char = CHARACTERISTIC_AIM_ACCURACY_MACHINEGUN;
			skill_char = CHARACTERISTIC_AIM_SKILL;
			break;

		case WP_SHOTGUN:
			acc_char = CHARACTERISTIC_AIM_ACCURACY_SHOTGUN;
			skill_char = CHARACTERISTIC_AIM_SKILL;
			break;

		case WP_GRENADE_LAUNCHER:
			acc_char = CHARACTERISTIC_AIM_ACCURACY_GRENADELAUNCHER;
			skill_char = CHARACTERISTIC_AIM_SKILL_GRENADELAUNCHER;
			break;

		case WP_ROCKET_LAUNCHER:
			acc_char = CHARACTERISTIC_AIM_ACCURACY_ROCKETLAUNCHER;
			skill_char = CHARACTERISTIC_AIM_SKILL_ROCKETLAUNCHER;
			break;

		case WP_LIGHTNING:
			acc_char = CHARACTERISTIC_AIM_ACCURACY_LIGHTNING;
			skill_char = CHARACTERISTIC_AIM_SKILL;
			break;

		case WP_RAILGUN:
			acc_char = CHARACTERISTIC_AIM_ACCURACY_RAILGUN;
			skill_char = CHARACTERISTIC_AIM_SKILL;
			break;

		case WP_PLASMAGUN:
			acc_char = CHARACTERISTIC_AIM_ACCURACY_PLASMAGUN;
			skill_char = CHARACTERISTIC_AIM_SKILL_PLASMAGUN;
			break;

		case WP_BFG:
			acc_char = CHARACTERISTIC_AIM_ACCURACY_BFG10K;
			skill_char = CHARACTERISTIC_AIM_SKILL_BFG10K;
			break;

		default:
			acc_char = CHARACTERISTIC_AIM_ACCURACY;
			skill_char = CHARACTERISTIC_AIM_SKILL;
			break;
	}

	// Lookup reasonably bounded accuracy and skill values
	//
	// NOTE: For reference, the bot files list different skill values for
	// level 3, 4, and 5 bots.  Level 1 and 2 skills are computed as a
	// factor of level 3 skill.  The bot skill values are generally
	// interpolated to the following ranges:
	//   5: 0.75 to 1.00
	//   4: 0.40 to 0.90
	//   3: 0.25 to 0.60
	//   2: 0.15 to 0.36
	//   2: 0.07 to 0.18
	bs->weapon_char_acc[weapon] = trap_Characteristic_BFloat(bs->character, acc_char, 0.1, 1);
	bs->weapon_char_skill[weapon] = trap_Characteristic_BFloat(bs->character, skill_char, 0.1, 1);

	// Skill 1-3 bots have identical characteristics, so this code
	// must manually decrease the accuracies of lower skilled bots
	// NOTE: The original code set handicaps instead of scaling these values
	if(bs->settings.skill <= 1)
	{
		bs->weapon_char_acc[weapon] *= 0.30;
		bs->weapon_char_skill[weapon] *= 0.30;
	}
	else if(bs->settings.skill <= 2)
	{
		bs->weapon_char_acc[weapon] *= 0.60;
		bs->weapon_char_skill[weapon] *= 0.60;
	}
}

/*
===============
BotReactionLoad

(Re)load the bot's reaction times.
===============
*/
void BotReactionLoad(bot_state_t * bs)
{
	float           reaction_char;

	// The reaction time characteristic needs some serious massaging.  This
	// value is between 0 and 5 and originally represented how long the bot
	// would wait before firing at a target.  Now it's just the measure of
	// how long it takes the bot to start start reacting to any change it
	// notices, and is primarily used in aiming.
	//
	// NOTE: Actual reaction times could be between 0 and 5, but that range
	// is clearly unreasonable.  This code translates the reaction time to
	// a value between 0.0 and 1.0, and then scales it between the minimum
	// and maximum reaction times.
	//
	// NOTE: Low values here are good and correspond to lower reaction times.
	// This is not a skill value.
	reaction_char = 0.2 * trap_Characteristic_BFloat(bs->character, CHARACTERISTIC_REACTIONTIME, 0.0, 5.0);

	// Scale the reaction characteristic from [0, 1] to [min, max]
	bs->react_time = interpolate(bot_reaction_min.value, bot_reaction_max.value, reaction_char);
}

/*
=============
BotInitialize

Initialize all internal data in the bot state
=============
*/
void BotInitialize(bot_state_t * bs)
{
	int             i;
	char            gender[MAX_CHARACTERISTIC_PATH];
	char            userinfo[MAX_INFO_STRING];

	// No valid last command time exists, but make the bot do its AI as if nothing
	// special has happened recently
	bs->last_command_time_ms = server_time_ms;

	// Set the team (red, blue, or free) when not in tournament mode
	if(g_gametype.integer != GT_TOURNAMENT)
		trap_EA_Command(bs->client, va("team %s", bs->settings.team));

	// Set the bot gender
	trap_Characteristic_String(bs->character, CHARACTERISTIC_GENDER, gender, sizeof(gender));
	trap_GetUserinfo(bs->client, userinfo, sizeof(userinfo));
	Info_SetValueForKey(userinfo, "sex", gender);
	trap_SetUserinfo(bs->client, userinfo);

	// Set the chat gender
	if(gender[0] == 'm' || gender[0] == 'M')
		trap_BotSetChatGender(bs->cs, CHAT_GENDERMALE);
	else if(gender[0] == 'f' || gender[0] == 'F')
		trap_BotSetChatGender(bs->cs, CHAT_GENDERFEMALE);
	else
		trap_BotSetChatGender(bs->cs, CHAT_GENDERLESS);

	// Set the chat name
	trap_BotSetChatName(bs->cs, EntityNameFast(bs->ent), bs->client);

#ifdef DEBUG_AI
	// Initialize debug settings
	bs->debug_flags = 0x00000000;
	bs->use_weapon = WP_NONE;
#endif

	// Load the skill and accuracy characteristics for each weapon
	for(i = 0; i < WP_NUM_WEAPONS; i++)
		BotWeaponCharsLoad(bs, i);

	// Load the bot's reaction times
	BotReactionLoad(bs);

	// Cache the chat attack characteristic, since it's used a lot
	bs->chat_attack = (.5 <= trap_Characteristic_BFloat(bs->character, CHARACTERISTIC_EASY_FRAGGER, 0, 1));

	// Initialize enemies
	BotAimEnemySet(bs, NULL, NULL);
	bs->goal_enemy = NULL;

	// Initialize the awareness engine
	BotAwarenessReset(bs);

	// Initialize the main goal
	BotGoalReset(bs);

	// Item tracking, timing, and statistics
	BotItemReset(bs);

	// Accuracy data for different aim zones
	BotAccuracyReset(bs);
}

/*
=============
BotResetState

Called when a bot enters the intermission or observer mode and
when the level is changed.
=============
*/
void BotResetState(bot_state_t * bs)
{
	int             client, entitynum, inuse;
	int             movestate, chatstate;
	bot_settings_t  settings;
	int             character;
	float           enter_game_time;

	// Only reset valid states
	if(!bs || !bs->inuse)
		return;

	// Save data that should not be reset
	memcpy(&settings, &bs->settings, sizeof(bot_settings_t));
	inuse = bs->inuse;
	client = bs->client;
	entitynum = bs->entitynum;
	character = bs->character;
	movestate = bs->ms;
	chatstate = bs->cs;
	enter_game_time = bs->enter_game_time;

	// Free checkpoints and patrol points
	BotFreeWaypoints(bs->checkpoints);
	BotFreeWaypoints(bs->patrol);

	// Reset the state
	memset(bs, 0, sizeof(bot_state_t));

	// Copy back some state stuff that should not be reset
	bs->ms = movestate;
	bs->cs = chatstate;
	bs->ent = &g_entities[client];
	bs->ps = &bs->ent->client->ps;
	memcpy(&bs->settings, &settings, sizeof(bot_settings_t));
	bs->inuse = inuse;
	bs->client = client;
	bs->entitynum = entitynum;
	bs->character = character;
	bs->enter_game_time = enter_game_time;

	// Reset the move state
	if(bs->ms)
	{
		trap_BotResetMoveState(bs->ms);
		trap_BotResetAvoidReach(bs->ms);
	}

	// Initialize internal bot data data, such as statistics and awareness
	BotInitialize(bs);
}
