// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_scan.c
 *
 * Functions that the bot uses to scan its surroundings
 *****************************************************************************/

#include "ai_main.h"
#include "ai_vars.h"
#include "ai_scan.h"

#include "ai_accuracy.h"
#include "ai_aware.h"
#include "ai_client.h"
#include "ai_command.h"
#include "ai_dodge.h"
#include "ai_entity.h"
#include "ai_item.h"
#include "ai_pickup.h"
#include "ai_self.h"
#include "ai_visible.h"
#include "ai_weapon.h"


// A set of temporary variables used by the bot when scanning nearby entities.
typedef struct bot_scan_s
{
	// Attack state
	qboolean        attacking;	// True if the bot is currently in the process of attacking

	// Aim enemy selection
	gentity_t      *aim_enemy;	// The bot's current choice for best aim enemy
	float           aim_rating;	// The rating of aim_enemy
	combat_zone_t   aim_zone;	// The combat zone of aim_enemy

	// Nearby targets
	int             nearby_teammates;	// Number of teammates near the bot, not counting the bot itself
	int             nearby_enemies;	// Number of visible enemies near the bot
	float           enemy_score;	// Highest score value of a nearby enemy

	// Carrier tracking
	gentity_t      *team_carrier;	// Entity of the highest rated visible team carrier
	float           team_carrier_rating;	// Rating of the highest rated visible team carrier
	gentity_t      *enemy_carrier;	// Entity of the highest rated visible enemy carrier
	float           enemy_carrier_rating;	// Rating of the highest rated visible enemy carrier

	// Dodge information
	int             last_num_missile_dodge;	// Number of missiles tracked last frame
} bot_scan_t;

// Temporary information about a scanned entity.  This information is
// retained so that different parts of the scanning algorithms can use
// the same data but only calculate it once.
typedef struct entity_scan_s
{
	gentity_t      *ent;		// The entity this information refers to

	vec3_t          dir;		// Direction vector from bs->now.origin to ent->r.currentOrigin
	qboolean        dir_set;	// True if dir has been set and false if not
	int             invisible;	// 1 if the entity is invisible, 0 if not, and -1 if uncalculated
	float           line_of_sight;	// Percentage entity is visible in line-of-sight (between 0 and 1), or -1 if uncalculated
	float           fov_dot;	// Dot product of the vector between the entity and the bot's forward, or -2 if uncalculated
	// NOTE: acos(fov_dot) is the angle between bot's forward vector and the entity
	float           rating;		// The entity's rating as per EntityRating(), or -1 if uncalculated
	float           kill_value;	// Number of points its worth to kill this entity
} entity_scan_t;

/*
===============
EntityScanReset

Reset data in the temporary entity_scan_t structure
===============
*/
void EntityScanReset(entity_scan_t * ent_scan, gentity_t * ent)
{
	ent_scan->ent = ent;

	ent_scan->dir_set = qfalse;
	ent_scan->invisible = -1;
	ent_scan->line_of_sight = -1;
	ent_scan->fov_dot = -2;
	ent_scan->rating = -1;
}

/*
===================
EntityScanDirection

Compute the direction vector from the bot to the scanned entity.
===================
*/
void EntityScanDirection(entity_scan_t * ent_scan, bot_state_t * bs, vec3_t dir)
{
	if(!ent_scan->dir_set)
	{
		VectorSubtract(ent_scan->ent->r.currentOrigin, bs->now.origin, ent_scan->dir);
		ent_scan->dir_set = qtrue;
	}
	VectorCopy(ent_scan->dir, dir);
}

/*
===================
EntityScanInvisible

Test if the scanned entity is invisible.
===================
*/
qboolean EntityScanInvisible(entity_scan_t * ent_scan)
{
	if(ent_scan->invisible < 0)
		ent_scan->invisible = EntityIsInvisible(ent_scan->ent);
	return ent_scan->invisible;
}

/*
=====================
EntityScanLineOfSight
=====================
*/
float EntityScanLineOfSight(entity_scan_t * ent_scan, bot_state_t * bs)
{
	if(ent_scan->line_of_sight < 0.0)
		ent_scan->line_of_sight = BotEntityVisible(bs, ent_scan->ent);
	return ent_scan->line_of_sight;
}

/*
========================
EntityScanFieldOfViewDot
========================
*/
float EntityScanFieldOfViewDot(entity_scan_t * ent_scan, bot_state_t * bs)
{
	vec3_t          dir;

	if(ent_scan->fov_dot < -1.0)
	{
		EntityScanDirection(ent_scan, bs, dir);
		VectorNormalize(dir);
		ent_scan->fov_dot = DotProduct(dir, bs->forward);
	}

	return ent_scan->fov_dot;
}

/*
===================
EntityScanKillValue
===================
*/
float EntityScanKillValue(entity_scan_t * ent_scan)
{
	if(ent_scan->kill_value < 0)
		ent_scan->kill_value = EntityKillValue(ent_scan->ent);
	return ent_scan->kill_value;
}

/*
================
EntityScanRating
================
*/
float EntityScanRating(entity_scan_t * ent_scan)
{
	if(ent_scan->rating < 0)
		ent_scan->rating = EntityRating(ent_scan->ent);
	return ent_scan->rating;
}

/*
============
BotScanEvent
============
*/
void BotScanEvent(bot_state_t * bs, gentity_t * ent, int event, int param)
{
	char            buf[128];

	// Strip out the sequence differentiation bits
	event &= ~EV_EVENT_BITS;

	// Process the event by type
	switch (event)
	{
			// Guess who died today!...
		case EV_OBITUARY:
		{
			gentity_t      *target, *attacker;
			int             mod;

			target = &g_entities[ent->s.otherEntityNum];
			attacker = &g_entities[ent->s.otherEntityNum2];
			mod = param;

			// If this was the bot that died, track that information
			if(target == bs->ent)
			{
				bs->bot_death_type = mod;
				bs->last_killed_by = attacker;

				// Record if it's a message about this bot suiciding
				bs->bot_suicide = (target == attacker ||
								   target == &g_entities[ENTITYNUM_NONE] || target == &g_entities[ENTITYNUM_WORLD]);

				bs->deaths++;
			}

			// If this bot killed the player who died, track different information
			else if(attacker == bs->ent)
			{
				bs->killed_player = target;
				bs->killed_player_time = server_time;
				bs->killed_player_type = mod;
				bs->kills++;
			}

			// Check if the player was someone who suicided when we tried to kill them
			else if((target == attacker) && (attacker == bs->aim_enemy || attacker == bs->goal_enemy))
			{
				bs->suicide_enemy = attacker;
				bs->suicide_enemy_time = server_time;
			}

			break;
		}

		case EV_GLOBAL_TEAM_SOUND:
			break;

		case EV_PLAYER_TELEPORT_IN:
			// Track when and where enemies teleport in
			if(ent->client && BotEnemyTeam(bs, ent))
			{
				bs->teleport_enemy = ent;
				bs->teleport_enemy_time = server_time;
				VectorCopy(ent->r.currentOrigin, bs->teleport_enemy_origin);
			}
			break;

		case EV_GENERAL_SOUND:
			// The bot doesn't care about general sounds on other players
			if(ent != bs->ent)
				break;

			if(param < 0 || param > MAX_SOUNDS)
			{
				BotAI_Print(PRT_ERROR, "EV_GENERAL_SOUND: eventParm (%d) out of range\n", param);
				break;
			}

			// If the bot is falling down a pit and has a teleporter, it should teleport
			trap_GetConfigstring(CS_SOUNDS + param, buf, sizeof(buf));
			if((bs->ps->stats[STAT_HOLDABLE_ITEM] == MODELINDEX_TELEPORTER) && (!strcmp(buf, "*falling1.wav")))
				BotCommandAction(bs, ACTION_USE);
			break;

			// It's worth noting if an important item respawned
			// NOTE: The event is on a temporary entity centered on the item,
			// not on the item entity itself
		case EV_GLOBAL_SOUND:
			if(param < 0 || param > MAX_SOUNDS)
			{
				BotAI_Print(PRT_ERROR, "EV_GLOBAL_SOUND: eventParm (%d) out of range\n", param);
				break;
			}
			trap_GetConfigstring(CS_SOUNDS + param, buf, sizeof(buf));
#ifdef MISSIONPACK
			if(!strcmp(buf, "sound/items/kamikazerespawn.wav"))
				BotTimeClusterLoc(bs, ent->r.currentOrigin);
			else
#endif
			if(!strcmp(buf, "sound/items/poweruprespawn.wav"))
				BotTimeClusterLoc(bs, ent->r.currentOrigin);
			break;

			// Consider timing items the bot just heard respawn
		case EV_ITEM_RESPAWN:
			BotTimeClusterLoc(bs, ent->r.currentOrigin);
			break;

			// Time the respawn of important powerups that were picked up
		case EV_GLOBAL_ITEM_PICKUP:
			BotTimeClusterLoc(bs, ent->r.currentOrigin);
			BotAwareTrackEntity(bs, ent, 512, -1);
			break;

			// Soft events that only the best bots take note of
		case EV_FOOTSTEP:
		case EV_SWIM:
		case EV_STEP_4:
		case EV_STEP_8:
		case EV_STEP_12:
		case EV_STEP_16:
			BotAwareTrackEntity(bs, ent, 128, -1);
			break;

			// Semi-soft stuff
		case EV_PAIN:
			if(bs->aim_enemy && ent == bs->aim_enemy)
				BotEnemyHealthSet(bs, param);
			BotAwareTrackEntity(bs, ent, 512, -1);
			break;

		case EV_ITEM_PICKUP:
			BotTimeClusterLoc(bs, ent->r.currentOrigin);
			BotAwareTrackEntity(bs, ent, 512, -1);
			break;

		case EV_FOOTSTEP_METAL:
		case EV_CHANGE_WEAPON:
		case EV_FOOTWADE:
			BotAwareTrackEntity(bs, ent, 512, -1);
			break;

			// Reasonably loud
		case EV_FALL_SHORT:
		case EV_JUMP:
		case EV_NOAMMO:
			BotAwareTrackEntity(bs, ent, 1024, -1);
			break;


			// Beam weapons are pretty easy to trace back to their owners
		case EV_RAILTRAIL:
		case EV_LIGHTNINGBOLT:
			BotAwareTrackEntity(bs, ent, 1024, -1);
			break;

			// Bullet weapons require a little more effort
		case EV_BULLET_HIT_FLESH:
		case EV_BULLET_HIT_WALL:
		case EV_SHOTGUN:
			BotAwareTrackEntity(bs, ent, 256, -1);
			break;

			// Very loud!
		case EV_FOOTSPLASH:
		case EV_FALL_MEDIUM:
		case EV_FALL_FAR:
		case EV_TAUNT:
		case EV_WATER_TOUCH:
		case EV_WATER_LEAVE:
		case EV_WATER_UNDER:
		case EV_WATER_CLEAR:
		case EV_JUMP_PAD:
		case EV_FIRE_WEAPON:
			BotAwareTrackEntity(bs, ent, 1024, -1);
			break;

			// Item use is pretty loud
		case EV_USE_ITEM0:
		case EV_USE_ITEM1:
		case EV_USE_ITEM2:
		case EV_USE_ITEM3:
		case EV_USE_ITEM4:
		case EV_USE_ITEM5:
		case EV_USE_ITEM6:
		case EV_USE_ITEM7:
		case EV_USE_ITEM8:
		case EV_USE_ITEM9:
		case EV_USE_ITEM10:
		case EV_USE_ITEM11:
		case EV_USE_ITEM12:
		case EV_USE_ITEM13:
		case EV_USE_ITEM14:
			BotAwareTrackEntity(bs, ent, 512, -1);
			break;
	}
}

/*
===================
BotScanPlayerEvents
===================
*/
void BotScanPlayerEvents(bot_state_t * bs, gentity_t * ent)
{
	int             event, param, time, entitynum;

	// Read event data from the player
	event = ent->s.event;
	param = ent->s.eventParm;
	time = ent->eventTime;

	// Do not process this event if its type (including sequence bits) and
	// time haven't changed since events on this entity were last processed
	entitynum = ent - g_entities;
	if(bs->last_event_type[entitynum] == event && bs->last_event_time[entitynum] == time)
		return;
	bs->last_event_type[entitynum] = event;
	bs->last_event_time[entitynum] = time;

	// Process events for this player
	BotScanEvent(bs, ent, event, param);
}

/*
======================
BotScanNonplayerEvents
======================
*/
void BotScanNonplayerEvents(bot_state_t * bs, gentity_t * ent)
{
	int             event, param, time;

	// Some events are stored in temporary event objects
	if(ent->s.eType > ET_EVENTS)
	{
		event = ent->s.eType - ET_EVENTS;
		param = ent->s.eventParm;	// NOTE: This is probably neither set nor used
		time = ent->eventTime;

		// Some temporary events mimic predictable player events
		// NOTE: These events aren't sent to the player they act on
		if(ent->s.eFlags & EF_PLAYER_EVENT)
			ent = &g_entities[ent->s.otherEntityNum];
	}
	// Other events are stored directly on the entity
	else
	{
		event = ent->s.event;
		param = ent->s.eventParm;
		time = ent->eventTime;
	}

	// Don't process events from old frames-- the bot should have processed them already
	if(time != server_time)
		return;

	// Process events for this object
	BotScanEvent(bs, ent, event, param);
}

#ifdef MISSIONPACK
/*
======================
BotScanForKamikazeBody
======================
*/
void BotScanForKamikazeBody(bot_state_t * bs, gentity_t * ent)
{
	// NOTE: Dead bodies don't have ent->s.eType set

	// Ignore entities without the kamikaze and entities that aren't dead
	if(!(ent->s.eFlags & EF_KAMIKAZE))
		return;
	if(!(ent->s.eFlags & EF_DEAD))
		return;

	// Record this as a possible body to gib (to prevent the kamikaze explosion)
	bs->kamikaze_body = ent;
}
#endif

/*
===============
BotScanForCount

If the entity is in line of sight, increment a counter.
===============
*/
void BotScanForCount(bot_state_t * bs, entity_scan_t * ent_scan, int *counter)
{
	if(EntityScanLineOfSight(ent_scan, bs) > 0.0)
		(*counter)++;
}

/*
=================
BotScanForCarrier

If the entity is a team carrier, consider tracking it.
=================
*/
void BotScanForCarrier(bot_state_t * bs, entity_scan_t * ent_scan, gentity_t ** best_carrier, float *best_rating)
{
	float           rating;

	// Don't bother if this entity isn't a carrier
	if(!EntityIsCarrier(ent_scan->ent))
		return;

	// Check if this entity has a better rating than the previous carrier
	rating = EntityScanRating(ent_scan);
	if(rating <= *best_rating)
		return;

	// Make sure the entity is in line-of-sight
	if(EntityScanLineOfSight(ent_scan, bs) <= 0.0)
		return;

	// Use this entity as the best carrier in its catagory
	*best_carrier = ent_scan->ent;
	*best_rating = rating;
}

/*
===============
BotScanForEnemy

The bot might choose this as the enemy to aim at and/or move towards.
===============
*/
void BotScanForEnemy(bot_state_t * bs, entity_scan_t * ent_scan, bot_scan_t * scan)
{
	float           visibility, fov, dist, score, rating;
	float           bot_damage_rate, enemy_damage_rate;
	vec3_t          dir, angles;
	gentity_t      *ent;
	combat_zone_t   zone, zone_inverse;
	bot_aware_t    *aware;
	qboolean        in_fov, enemy_splash, attacking_target;

	// Cache the entity being scanned
	ent = ent_scan->ent;

	// Neither aim at nor become visually aware of occluded targets
	visibility = EntityScanLineOfSight(ent_scan, bs);
	if(visibility <= 0.0)
	{
		// If the bot was aware of this target, note that it's no longer sighted
		aware = BotAwarenessOfEntity(bs, ent);
		if(aware)
			aware->sighted = -1;

		return;
	}

	// The bot's field of vision is fixed at 90 degrees (+/- 45 degrees)
	fov = DEG2RAD(45.0);


	// The target the bot is currently focusing on has will be noticed from
	// further away
	dist = (ent == bs->aim_enemy ? 4096.0 : 1536.0);

	// Attacking targets are easier to see; invisible non-attacking targets are harder
	if(ent->client && ent->client->ps.eFlags & EF_FIRING)
	{
		dist *= 1.5;
	}
	else if(EntityScanInvisible(ent_scan))
	{
		fov *= 0.6;
		dist *= 0.25;
	}

	// Test if the entity is in the bot's field of view
	in_fov = (EntityScanFieldOfViewDot(ent_scan, bs) >= cos(fov));

	// If the entity is in the bot's field of view, try to become aware of it
	if(in_fov)
	{
		// Reduced visibility decreases the ability to become aware but not
		// the ability to maintain awareness
		BotAwareTrackEntity(bs, ent, dist * visibility, dist);
	}

	// Only aim at the enemy if the bot actually became aware of it
	aware = BotAwarenessOfEntity(bs, ent);
	if(!aware)
		return;

	// If the entity isn't currently in sight, note that ...
	if(!in_fov)
		aware->sighted = -1.0;
	// ... Also note when the entity was first sighted if that occurred now
	else if(aware->sighted <= 0.0)
		aware->sighted = bs->command_time;

	// If the bot chose to fire at an enemy last frame and that enemy is around
	// to be attacked this frame as well, don't select a different aim target
	if(scan->attacking && scan->aim_enemy && scan->aim_enemy == bs->aim_enemy && scan->aim_enemy != ent)
	{
		return;
	}

	// Assume the enemy can receive splash damage
	enemy_splash = qtrue;

	// Enemy players require some extra processing
	if(ent->client)
	{
		// If this player is more valuable than previous enemies,
		// remember this so the bot will prefer to stay in this area
		score = EntityScanKillValue(ent_scan);
		if(score > scan->enemy_score)
			scan->enemy_score = score;

		// Some bots won't select talking players
		if((!bs->chat_attack) && (ent->client->ps.eFlags & EF_TALK))
			return;

		// Don't select players who just teleported in and haven't moved much
		if((bs->teleport_enemy == ent) &&
		   (bs->teleport_enemy_time > server_time - 0.5) &&
		   (DistanceSquared(bs->teleport_enemy_origin, ent->r.currentOrigin) < Square(70.0)))
		{
			return;
		}

		// Test if this player can actually receive splash damage
		enemy_splash = !ent->client->ps.powerups[PW_BATTLESUIT];
	}

	// Create a combat zone describing the target's location
	EntityScanDirection(ent_scan, bs, dir);
	VectorToAngles(dir, angles);
	CombatZoneCreate(&zone, VectorLength(dir), AngleNormalize180(angles[PITCH]));

	// Determine the bot's expected damage rate per second for that zone
	bot_damage_rate = BotDamageRate(bs, bs->weapons_available, &zone, enemy_splash);

	// Estimate the enemy's expected damage rate against the bot
	enemy_damage_rate = 0;
	if(ent->client)
	{
		// Compute the damage rate the enemy would do if it were as skilled as the bot
		CombatZoneInvert(&zone, &zone_inverse);
		enemy_damage_rate = BotDamageRate(bs, (1 << ent->client->ps.weapon), &zone_inverse, !bs->ps->powerups[PW_BATTLESUIT]);

		// Account for damage multiplier effects
		//
		// NOTE: These are not factored in for the bot because they would scale all
		// potential ratings by the same amount anyway.
		if(ent->client->ps.powerups[PW_QUAD])
			enemy_damage_rate *= g_quadfactor.value;
#ifdef MISSIONPACK
		if(ent->client->ps.powerups[PW_DOUBLER])
			damage_factor *= 2;
#endif
	}

	// Don't avoid an enemy with a poor damage rate-- only prefer enemies with
	// high damage rates
	if(enemy_damage_rate < bot_damage_rate)
		enemy_damage_rate = bot_damage_rate;

	// Scale the rating by the bot's expected damage per second for that zone to
	// determine the amount of points gained per second of attack on this enemy.
	// Also factor in this target's ability to damage the bot, since attacking
	// dangerous players prevents the bot's death.
	rating = EntityScanRating(ent_scan) * bot_damage_rate * enemy_damage_rate;

	// Test if the bot is in the process of attacking this target
	attacking_target = (scan->attacking && bs->aim_enemy && bs->aim_enemy == ent);

	// Don't select this enemy if it's rating is worse than the previous aim
	// enemy and the bot doesn't need to keep attacking this target.
	if(rating <= scan->aim_rating && !attacking_target)
		return;

	// Update the scan structure with new aim enemy information
	scan->aim_enemy = ent;
	scan->aim_rating = rating;
	memcpy(&scan->aim_zone, &zone, sizeof(combat_zone_t));
}

/*
============
BotScanEnemy
============
*/
void BotScanEnemy(bot_state_t * bs, gentity_t * ent, bot_scan_t * scan)
{
	entity_scan_t   ent_scan;

	EntityScanReset(&ent_scan, ent);

	// Track basic information about visible player enemies
	if(!EntityScanInvisible(&ent_scan))
	{
		BotScanForCount(bs, &ent_scan, &scan->nearby_enemies);
		BotScanForCarrier(bs, &ent_scan, &scan->enemy_carrier, &scan->enemy_carrier_rating);
	}

	// Check if bot should attack and/or move towards this enemy
	BotScanForEnemy(bs, &ent_scan, scan);
}

/*
===============
BotScanTeammate
===============
*/
void BotScanTeammate(bot_state_t * bs, gentity_t * ent, bot_scan_t * scan)
{
	entity_scan_t   ent_scan;

	EntityScanReset(&ent_scan, ent);

	// Scan the teammates for different reasons
	if(bs->ent != ent)
		BotScanForCount(bs, &ent_scan, &scan->nearby_teammates);
	BotScanForCarrier(bs, &ent_scan, &scan->team_carrier, &scan->team_carrier_rating);
}

/*
=============
BotScanPlayer
=============
*/
void BotScanPlayer(bot_state_t * bs, gentity_t * ent, bot_scan_t * scan)
{
	// Ignore non-living players
	if(!EntityIsAlive(ent))
		return;

	// Different scans apply for teammates and enemies
	if(BotEnemyTeam(bs, ent))
		BotScanEnemy(bs, ent, scan);
	else if(BotSameTeam(bs, ent))
		BotScanTeammate(bs, ent, scan);
}

/*
=====================
BotNoteStoppedMissile
=====================
*/
void BotNoteStoppedMissile(bot_state_t * bs, gentity_t * bolt)
{
	// Do nothing if missile is still moving
	if((bolt->s.pos.trType != TR_STATIONARY) &&
	   (bolt->s.pos.trType != TR_LINEAR_STOP || level.time < bolt->s.pos.trTime + bolt->s.pos.trDuration))
		return;

	// Avoid the missile
	trap_BotAddAvoidSpot(bs->ms, bolt->s.pos.trBase, 160, AVOID_ALWAYS);
}

/*
==============
BotNoteMissile
==============
*/
void BotNoteMissile(bot_state_t * bs, gentity_t * bolt)
{
	float           air_time;
	vec3_t          to_bot;
	missile_dodge_t *md;

	// If missile is stopped, avoid that spot (eg. stopped grenade)
	BotNoteStoppedMissile(bs, bolt);

	// Compute how long this missile has been in the air
	air_time = (level.time - bolt->s.pos.trTime) * 0.001;

	// Seeing missiles triggers awareness of attacker
	BotAwareTrackEntity(bs, &g_entities[bolt->r.ownerNum], 1024, 1024);

	// If the bot is skilled enough, consider dodging this missile
	if(bs->settings.skill <= 2)
		return;

	// Higher skilled bots notice the missiles sooner
	if(bs->settings.skill <= 3 && air_time < 0.35)
		return;
	if(bs->settings.skill <= 4 && air_time < 0.10)
		return;

	// Only dodge missiles with straight trajectories
	// FIXME: In theory the dodge code could be outfitted to handle this.
	// It would require a good deal more effort though.
	if(bolt->s.pos.trType != TR_LINEAR)
		return;

	// Check if we have space to record this missile for dodging purposes
	if(bs->num_missile_dodge >= MAX_MISSILE_DODGE)
		return;

	// If the bot tracks this missile, it will be stored in this record
	md = &bs->missile_dodge[bs->num_missile_dodge];

	// Extract trajectory information for this missile
	BG_EvaluateTrajectory(&bolt->s.pos, server_time, md->pos);
	VectorCopy(bolt->s.pos.trDelta, md->vel);
	VectorCopy(bolt->s.pos.trDelta, md->dir);
	md->speed = VectorNormalize(md->dir);

	// Ignore missiles that aren't pointing close to the bot and aren't nearby
	VectorSubtract(bs->now.origin, md->pos, to_bot);
	VectorNormalize(to_bot);
	if(DotProduct(to_bot, md->dir) < cos(DEG2RAD(50)) && Square(bolt->splashRadius) < DistanceSquared(md->pos, bs->now.origin))
		return;

	// Try to dodge this missile
	md->bolt = bolt;
	bs->num_missile_dodge++;
}

#ifdef MISSIONPACK
/*
===============
BotNoteProxMine
===============
*/
void BotNoteProxMine(bot_state_t * bs, gentity_t * bolt)
{
	// Don't bother trying to deactive if the bot doesn't have a weapon for it
	if(!BotMineDisarmWeapon(bs))
		return;

	if(bs->num_proxmines >= MAX_PROXMINES)
		return;

	bs->proxmines[bs->num_proxmines++] = bolt;
}
#endif

/*
========================
CompareEntityMissileShot

The first argument must be a pointer to a gentity_t and
the second must be a pointer to bot_missile_shot_t.
========================
*/
int QDECL CompareEntityMissileShot(const void *ent, const void *shot)
{
	return ((gentity_t *) ent) - ((bot_missile_shot_t *) shot)->bolt;
}

/*
===================
BotTrackMissileShot
===================
*/
void BotTrackMissileShot(bot_state_t * bs, gentity_t * bolt)
{
	bot_missile_shot_t *shot;
	qboolean        insert;

	// Only update if the bot could have attacked an enemy last frame
	if(!BotEnemyTeam(bs, bs->attack.ent))
		return;

	// Check if the missile is tracked in the array or could be
	shot = (bot_missile_shot_t *)
		bsearch_ins(bolt, bs->own_missiles,
					&bs->num_own_missiles, MAX_MISSILE_SHOT, sizeof(bot_missile_shot_t), CompareEntityMissileShot, &insert);

	// If an array entry was returned and it's not an insert,
	// the bot already knows about this missile
	if(shot && !insert)
		return;

	// Only add the new missile to the array if requested to do so
	if(!insert)
		return;

	// Write missile entry into the empty slot
	shot->bolt = bolt;
	shot->weapon = bolt->s.weapon;
	memcpy(&shot->zone, &bs->aim_zone, sizeof(combat_zone_t));
}

/*
==============
BotScanMissile

Scan an entity which was confirmed to be a missile.
==============
*/
void BotScanMissile(bot_state_t * bs, gentity_t * bolt)
{
	int             bot_team;

	// If this is the bot's own missile, track it for accuracy calculations
	if(bolt->r.ownerNum == bs->client)
	{
		BotTrackMissileShot(bs, bolt);
		return;
	}

	// Don't process missiles from teammates
	bot_team = BotTeam(bs);
	if(bot_team == TEAM_RED || bot_team == TEAM_BLUE)
	{
		// Determining which team shot the missile is really painful!
		int             shooter_team = g_entities[bolt->r.ownerNum].client->sess.sessionTeam;

		if(bot_team == shooter_team)
			return;
	}

#ifdef MISSIONPACK
	// Proximity mines can be heard, so process them before vision checks
	if(bolt->s.weapon == WP_PROX_LAUNCHER)
		BotNoteProxMine(bs, bolt);
#endif

	// Make sure missile is in field-of-view
	if(!BotTargetInFieldOfVision(bs, bolt->r.currentOrigin, 90))
		return;

	// Make sure missile is in line-of-sight
	if(!BotEntityVisibleFast(bs, bolt))
		return;

	// Note the missile for avoidance purposes
	BotNoteMissile(bs, bolt);
}

/*
===================
BotScanDestructable
===================
*/
void BotScanDestructable(bot_state_t * bs, gentity_t * ent, bot_scan_t * scan)
{
	entity_scan_t   ent_scan;

	// Only target this entity if it's actually a destructable object
	if(!ent->takedamage || !ent->health)
		return;

	// Only attack entities that are enemies of the bot (ie. not shootable buttons)
	if(!BotEnemyTeam(bs, ent))
		return;

	// Consider this as an aim enemy
	EntityScanReset(&ent_scan, ent);
	BotScanForEnemy(bs, &ent_scan, scan);
}

/*
=============
BotScanEntity
=============
*/
void BotScanEntity(bot_state_t * bs, gentity_t * ent, bot_scan_t * scan, int scan_mode)
{
	// Scan for events on players and non-players
	if(ent->client)
	{
		if(scan_mode & SCAN_PLAYER_EVENT)
			BotScanPlayerEvents(bs, ent);
	}
	else
	{
		if(scan_mode & SCAN_NONPLAYER_EVENT)
			BotScanNonplayerEvents(bs, ent);
	}

#ifdef MISSIONPACK
	// Check for dead bodies with the kamikaze effect which should be gibbed
	if(scan_mode & SCAN_MISSILE)
		BotScanForKamikazeBody(bs, ent);
#endif

	// Scan type-specific entity information
	switch (ent->s.eType)
	{
		case ET_PLAYER:
			if(scan_mode & SCAN_TARGET)
				BotScanPlayer(bs, ent, scan);
			break;

			// NOTE: Gibbed players and spectators have type ET_INVISIBLE, not ET_PLAYER.
		case ET_INVISIBLE:
			break;

			// NOTE: Missiles get scanned as destructables, in case some mod has
			// destructable missiles.  Having tried it, it's a horrible gameplay
			// idea.  But it's not the AI's job to critique such decisions.
		case ET_MISSILE:
			if(scan_mode & SCAN_MISSILE)
				BotScanMissile(bs, ent);
		default:
			if(scan_mode & SCAN_TARGET)
				BotScanDestructable(bs, ent, scan);
			break;
	}
}

/*
=============
BotScanDamage

Checks if the bot was damaged last frame and
records the appropriate data (awareness state
and bs->damaged).
=============
*/
void BotScanDamage(bot_state_t * bs)
{
	int             change;

	// Record decreases in health total as damage for statistical purposes
	// NOTE: The -1 health and armor decays for values above 100 are ignored
	if(bs->last_health > 0)
	{
		// Check for changes in health...
		change = bs->last_health - bs->ps->stats[STAT_HEALTH];
		if((bs->last_health > 100 && change > 0) || (change > 1))
		{
			bs->damage_received += bs->last_health;
			if(bs->ps->stats[STAT_HEALTH] > 0)
				bs->damage_received -= bs->ps->stats[STAT_HEALTH];
		}

		// ... And in armor
		change = bs->last_armor - bs->ps->stats[STAT_ARMOR];
		if((bs->last_armor > 100 && change > 0) || (change > 1))
		{
			bs->damage_received += bs->last_armor;
			if(bs->ps->stats[STAT_ARMOR] > 0)
				bs->damage_received -= bs->ps->stats[STAT_ARMOR];
		}
	}

	// Save this frame's health and armor for next frame
	bs->last_health = bs->ps->stats[STAT_HEALTH];
	bs->last_armor = bs->ps->stats[STAT_ARMOR];

	// By default, assume no one damaged the bot this frame
	bs->last_hurt_client = NULL;

	// Determine whether the bot was damaged this frame
	bs->damaged = (bs->ps->damageEvent != bs->last_damageEvent) && (bs->ps->damageCount);

	// There is nothing to setup if the bot was not damaged
	if(!bs->damaged)
		return;
	bs->last_damageEvent = bs->ps->damageEvent;

	// If the damage wasn't directional, exit now
	if(bs->ps->damageYaw == 255 && bs->ps->damagePitch == 255)
		return;

	// If the bot hurt themselves, stop checking
	if(bs->ent->client->lasthurt_client == bs->client)
		return;

	// Become aware of the client that damaged the bot
	bs->last_hurt_client = &g_entities[bs->ent->client->lasthurt_client];
	BotAwareTrackEntity(bs, bs->last_hurt_client, 1024, 1024);
}

/*
=================
BotScanInitialize

Input scan_mode is a bitmask of requested scans on the bot.
As well as initializing some values, this function returns
a bitmask of which scans the bot will actually do this frame.
If all scans have been done previously this frame, this
function will return 0x0000 and the caller should early exit.
=================
*/
int BotScanInitialize(bot_state_t * bs, bot_scan_t * scan, int scan_mode)
{
	// Don't scan for targets when the bot is dead
	if(BotIsDead(bs))
		scan_mode &= ~SCAN_TARGET;

	// If no scans were requested, do nothing
	if(scan_mode == 0x0000)
		return scan_mode;

	// Some values are only initialized when scanning for missiles
	if(scan_mode & SCAN_MISSILE)
	{
		// Reset all avoid spots (probably a grenade that could explode soon)
		trap_BotAddAvoidSpot(bs->ms, vec3_origin, 0, AVOID_CLEAR);

		// Reset the list of all missiles the bot should dodge
		scan->last_num_missile_dodge = bs->num_missile_dodge;
		bs->num_missile_dodge = 0;

#ifdef MISSIONPACK
		// Reset the entity number of a kamikaze body to blow up
		bs->kamikaze_body = NULL;

		// Reset the list of nearby proximity mines
		bs->num_proxmines = 0;
#endif
	}

	// These values only apply when scanning targetable entities
	if(scan_mode & SCAN_TARGET)
	{
		// Determine if the bot is in the process of attacking or will be shortly.
		//
		// NOTE: The bot is attacking if it's scheduled to fire in the near future
		// but hasn't started yet, or also if it started firing and hasn't scheduled
		// a time to stop.
		scan->attacking = (bs->fire_start_time > 0) && (bs->command_time <= bs->fire_start_time || !bs->fire_stop_time);

		// Default characteristics used in the enemy scan
		scan->aim_enemy = NULL;
		scan->aim_rating = -1;

		scan->nearby_teammates = 0;
		scan->nearby_enemies = 0;
		scan->team_carrier = NULL;
		scan->team_carrier_rating = -1;
		scan->enemy_carrier = NULL;
		scan->enemy_carrier_rating = -1;

		scan->enemy_score = 1.0;
	}

	// Inform the caller what kind of scans are necessary
	return scan_mode;
}

/*
===============
BotScanComplete

Process the data in the scan state and save
relevant information in the bot state.
===============
*/
void BotScanComplete(bot_state_t * bs, bot_scan_t * scan, int scan_mode)
{
	float           attack_time;
	gentity_t      *goal_enemy;

	// Set all of the target information
	if(scan_mode & SCAN_TARGET)
	{
		// Check if any new missiles were detected
		bs->new_missile = (scan->last_num_missile_dodge < bs->num_missile_dodge);

		// Set the (possibly new) aim enemy and combat zone description
		BotAimEnemySet(bs, scan->aim_enemy, &scan->aim_zone);

#ifdef DEBUG_AI
		// Output changes in nearby player counts and carriers if requested
		if(bs->debug_flags & BOT_DEBUG_INFO_SCAN)
		{
			if(bs->nearby_teammates != scan->nearby_teammates)
				BotAI_Print(PRT_MESSAGE, "%s: Scan: %.1f Nearby Teammates\n", EntityNameFast(bs->ent), scan->nearby_teammates);

			if(bs->nearby_enemies != scan->nearby_enemies)
				BotAI_Print(PRT_MESSAGE, "%s: Scan: %.1f Nearby Enemies\n", EntityNameFast(bs->ent), scan->nearby_enemies);

			if(bs->team_carrier != scan->team_carrier)
				BotAI_Print(PRT_MESSAGE, "%s: Scan: Team Carrier %s\n",
							EntityNameFast(bs->ent), EntityNameFast(scan->team_carrier));
			if(bs->enemy_carrier != scan->enemy_carrier)
				BotAI_Print(PRT_MESSAGE, "%s: Scan: Enemy Carrier %s\n",
							EntityNameFast(bs->ent), EntityNameFast(scan->enemy_carrier));
		}
#endif

		// Save player count and carrier information
		bs->nearby_teammates = scan->nearby_teammates;
		bs->nearby_enemies = scan->nearby_enemies;
		bs->team_carrier = scan->team_carrier;
		bs->enemy_carrier = scan->enemy_carrier;
		bs->enemy_score = scan->enemy_score;

		// Check if the bot was damaged this frame (and if so, how much)
		BotScanDamage(bs);

		// If the bot has scanned for targets before, determine how
		// much enemy attack time has passed since the last scan
		if(bs->last_target_scan_time > 0.0)
		{
			// In non-teamplay modes, enemies can also attack each other,
			// so bots average at most one enemy attacking them.
			attack_time = bs->nearby_enemies;
			if(!(game_style & GS_TEAM) && bs->nearby_enemies > 1)
				attack_time = 1.0;

			// Scale by the number of seconds passed since the last target scan
			attack_time *= server_time - bs->last_target_scan_time;

			// Enemies will attack non-carrier teammates with equal probability
			if(!EntityIsCarrier(bs->ent))
				attack_time /= bs->nearby_teammates + 1;

			// Invisible players get attacked less
			if(EntityIsInvisible(bs->ent))
				attack_time *= 0.4;

			// Record the additional attack time that has passed
			bs->enemy_attack_time += attack_time;
		}
		bs->last_target_scan_time = server_time;
	}

	// If the awareness engine might have been updated, update the goal enemy.
	// The goal enemy is the highest rated entity in the awareness engine
	if(scan_mode & SCAN_AWARENESS)
	{
		// Update the goal enemy if that enemy changed
		goal_enemy = BotBestAwarenessEntity(bs);
		if(bs->goal_enemy != goal_enemy)
		{
			bs->goal_enemy = goal_enemy;

#ifdef DEBUG_AI
			if(bs->debug_flags & BOT_DEBUG_INFO_ENEMY)
				BotAI_Print(PRT_MESSAGE, "%s: Goal Enemy: %s\n", EntityNameFast(bs->ent), EntityNameFast(bs->goal_enemy));
#endif
		}
	}
}

/*
=======
BotScan
=======
*/
void BotScan(bot_state_t * bs, int scan_mode)
{
	int             sequence, entnum;
	bot_scan_t      scan;
	qboolean        player_only;

	// Prepare to scan if necessary
	scan_mode = BotScanInitialize(bs, &scan, scan_mode);
	if(!scan_mode)
		return;

	// Check if the scanning can be restricted to player entities
	if(game_style & GS_DESTROY)
		player_only = !(scan_mode & ~(SCAN_PLAYER_EVENT));
	else
		player_only = !(scan_mode & ~(SCAN_PLAYER_EVENT | SCAN_TARGET));

	// Parse through the bot's list of snapshot entities and scan each of them
	sequence = 0;
	if(player_only)
	{
		while((entnum = trap_BotGetSnapshotEntity(bs->client, sequence++)) >= 0 && (entnum < MAX_CLIENTS))
			BotScanEntity(bs, &g_entities[entnum], &scan, scan_mode);
	}
	else
	{
		while((entnum = trap_BotGetSnapshotEntity(bs->client, sequence++)) >= 0)
			BotScanEntity(bs, &g_entities[entnum], &scan, scan_mode);
	}

	// The bot's own entity is never in the snapshot, so scan it as well
	BotScanEntity(bs, bs->ent, &scan, scan_mode);

	// Complete the scanning
	BotScanComplete(bs, &scan, scan_mode);
}
