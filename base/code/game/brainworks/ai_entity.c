// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_entity.c
 *
 * Functions that the bot uses to get information about an entity
 *****************************************************************************/

#include "ai_main.h"
#include "ai_vars.h"
#include "ai_entity.h"

#include "ai_level.h"
#include "ai_resource.h"

// This file provides access to some information needed for ground traces
#include "../bg_local.h"


/*
============
SimplifyName

Modifies the contents of an inputted name to remove
crazy punctuation, clan tags, and the like.  Always
returns the input name pointer (which could be NULL).

NOTE: String buffers inputted to this function must
have at least 9 characters in them.  If the name gets
reduced to an empty string, the bot needs 8 characters
for "nameless" or "asciiman", plus one for the
terminating NUL.
============
*/
char           *SimplifyName(char *name)
{
	size_t          length;
	char           *source, *dest, *open_tag, *close_tag, tag_match;
	char           *clan_tags = "[]<>(){}==--";

	// Ignore NULL input strings
	if(!name)
		return name;

	// Check for idiots with no name
	length = strlen(name);
	if(!length)
	{
		Q_strncpyz(name, "nameless", 9);
		return name;
	}

	// Remove high ascii and space characters
	for(source = &name[0], dest = source; *source; source++, dest++)
	{
		// Ignore high ascii and spaces
		if((*source & 0x80) || (*source == ' '))
		{
			dest--;
			length--;
		}
		// Copy the character to its new home if necessary
		else if(dest != source)
			*dest = *source;
	}
	*dest = '\0';

	// Scan the name for matching clan tags (eg. {x}) and remove their contents
	source = &name[0];
	while(*source)
	{
		// Look up the corresponding closing tag if this letter is an opening tag
		open_tag = strchr(clan_tags, *source);
		if(!open_tag)
		{
			source++;
			continue;
		}
		tag_match = clan_tags[(open_tag - clan_tags) ^ 0x1];

		// Search for the matching close tag in the remainder of the string
		close_tag = strchr(source + 1, tag_match);
		if(!close_tag)
		{
			source++;
			continue;
		}

		// Just remove the tags themselves if they enclose the entire name
		if((source == name) && (close_tag == &name[length - 1]))
		{
			// Shift the enclosed string back one character, ignoring the last character
			length -= 2;
			memmove(name, name + 1, length);
			name[length] = '\0';
			continue;
		}

		// Copy everything after the closing tag on top of the current character,
		// including the terminating NUL character
		length -= (++close_tag - source);
		memmove(source, close_tag, length + 1);
		continue;
	}

	// Remove all non-letter, non-number, non-underscore characters
	for(source = &name[0], dest = source; *source; source++, dest++)
	{
		// Ignore the character if not an alphabetical character, a number, or an underscore
		if(!Q_isalpha(*source) && !(*source >= '0' && *source <= '9') && !(*source == '_'))
		{
			dest--;
			length--;
			continue;
		}

		// Copy the character to its new home if necessary
		if(dest != source)
			*dest = *source;

		// Translate upper case characters to lower case
		if(Q_isupper(*dest))
			*dest += 'a' - 'A';
	}
	*dest = '\0';

	// If no characters were left, make fun of their name
	if(!length)
	{
		Q_strncpyz(name, "asciiman", 9);
		return name;
	}

	// Remove "Mr" prefix, assuming it is a prefix
	if((length > 2) && (name[0] == 'm') && (name[1] == 'r'))
	{
		length -= 2;
		memmove(name, name + 2, length + 1);	// +1 copies the terminating NUL
	}

	// Return a pointer to the name to help functional programmers
	return name;
}

/*
==========
EntityName

Returns the input "name" pointer, setup with a name
if a useful name could be determined.

If the "simplify" flag is specified, the name
will be stripped of stupid things like clan
tags, punctuation, strange capitalization, and
other w31rdn355.
==========
*/
char           *EntityName(gentity_t * ent, char *name, size_t size)
{
	// Make sure a valid entity pointer was specified
	if(!ent)
	{
		Q_strncpyz(name, "NONE", size);
		return name;
	}

	// If the entity is invalid, create a default name for it
	if(!ent->inuse)
	{
		Q_strncpyz(name, "INVALID", size);
		return name;
	}

	// Check players first because they are they most common name lookups
	if(ent->client)
	{
		Q_strncpyz(name, ent->client->pers.netname, size);
		Q_CleanStr(name);
		return name;
	}

	// Look up names of different in-game objects
	switch (ent->s.eType)
	{
		case ET_ITEM:
			Q_strncpyz(name, ent->item->pickup_name, size);
			return name;

		case ET_TEAM:
			if(!strcmp(ent->classname, "team_redobelisk"))
				Q_strncpyz(name, "Red Obelisk", size);
			else if(!strcmp(ent->classname, "team_blueobelisk"))
				Q_strncpyz(name, "Blue Obelisk", size);
			else if(!strcmp(ent->classname, "team_neutralobelisk"))
				Q_strncpyz(name, "Neutral Obelisk", size);
			else
				break;
			return name;
	}

	// The item's name is not determinable, so just use the class name
	Q_strncpyz(name, ent->classname, size);
	return name;
}

/*
==============
EntityNameFast

Returns a pointer to a staticly allocated name
string for the inputted entity.  Be sure to use
the return value as soon as possible.
==============
*/
char           *EntityNameFast(gentity_t * ent)
{
	static int      index = 0;
	static char     name_cache[4][MAX_NETNAME];

	// Select the appropriate pointer to use this frame
	index = (index + 1) & 0x3;

	// Store the input entity's name in the local cache
	return EntityName(ent, name_cache[index], MAX_NETNAME);
}

/*
========================
EntityUpdatesSynchronous

Returns true if the entity state updates are
synchronized to the server frame and false if
it updates asynchronously.

NOTE: Entities that update with the server
update by ascending entity number.
========================
*/
qboolean EntityUpdatesSynchronous(gentity_t * ent)
{
	// Non-players (such as movers and missiles) always update synchronously
	if(!ent || !ent->inuse || !ent->client)
		return qtrue;

	// Bots always update during server frames
	if(ent->r.svFlags & SVF_BOT)
		return qtrue;

	// Clients are always synchronized if the server requests it
	if(g_synchronousClients.integer)
		return qtrue;

	// Human clients update asynchronously
	return qfalse;
}

/*
===============
EntityTimestamp

Returns the estimated time at which the entity's
bounds and location were last updated.

NOTE: If the input entity is NULL, the timestamp
for the current AI frame is returned.
===============
*/
float EntityTimestamp(gentity_t * ent)
{
	// Non-entities are synchronized with the server
	if(!ent)
		return server_time;

	// Player bounds and coordinates are set at a specific command time
	//
	// NOTE: This code uses ent->client->ps.commandTime instead of
	// ent->client->pers.cmd.serverTime because the serverTime value is
	// updated as soon a client sends in a command, but commandTime isn't
	// updated until the server actually processes that command.  For
	// unsynchronized clients (ie. humans), these values will always be
	// equal because commands are processed as soon as they are received.
	// But for synchronized clients (ie. bots), a command will wait,
	// pending processing, until the server update, at which point
	// commandTime will finally be updated.
	//
	// NOTE: These values are a bit misnamed.  Technically the server time is
	// the time at which the client told the server to process its command.
	// So yes, it's the server's time, but it's relative to the client's
	// perception of the server.  This value doesn't have to be related to
	// things like server_time (although the server includes some code to
	// sanity check this value to prevent speed hacks).  And strangely, the
	// command time is the time the server last processed a command, not the
	// timestamp of the client's last sent command.
	if(ent->client)
		return ent->client->ps.commandTime * .001;

	// All other entities are updated with the server frames
	return server_time;
}

/*
=================
EntityWorldBounds

Computes an entity's bounding box in real world coodinates.
=================
*/
void EntityWorldBounds(gentity_t * ent, vec3_t mins, vec3_t maxs)
{
	// Compute the entity's bounding box in absolute space
	//
	// NOTE: ent->r.absmin is not necessarily ent->r.currentOrigin + ent->r.mins,
	// due to rounding issues.  ent->r.absmin is less accurate, but it's what the
	// server uses for stuff like trap_EntitiesInBox(), so we have to use it here
	// as well.  It seems that trap_Trace() uses ent->r.mins, however, but this
	// theory is currently unconfirmed.
	VectorCopy(ent->r.absmin, mins);
	VectorCopy(ent->r.absmax, maxs);
}

/*
=======================
EntityCenterWorldBounds

Computes an entity's center origin in real space.  Also computes
the world bounding box for that entity.  For some reason, movers
always have zeroed ent->r.currentOrigin and ent->s.origin values,
but their mins and maxs are set to the real space coordinates (what
one would expect absmin and absmax to be set to).  This means their
centers must be manually computed this way.  Their locally oriented
bounding boxes can be extracted from this information as well--
see EntityCenterAllBounds() for more information.

NOTE: The way this function is written, if a standard game entity
with valid origin and "normal" bounding box, it will still return
an origin at the center of the bounding box.  This will not
necessarily be the entity's current origin, ent->r.currentOrigin.
=======================
*/
void EntityCenterWorldBounds(gentity_t * ent, vec3_t center, vec3_t mins, vec3_t maxs)
{
	// Compute the entity's bounding box in absolute space
	EntityWorldBounds(ent, mins, maxs);

	// Use the current origin if possible
	if((mins[0] <= ent->r.currentOrigin[0]) && (ent->r.currentOrigin[0] <= maxs[0]) &&
	   (mins[1] <= ent->r.currentOrigin[1]) && (ent->r.currentOrigin[1] <= maxs[1]) &&
	   (mins[2] <= ent->r.currentOrigin[2]) && (ent->r.currentOrigin[2] <= maxs[2]))
	{
		VectorCopy(ent->r.currentOrigin, center);
	}

	// Otherwise compute the center of the bounding box
	else
	{
		VectorAdd(mins, maxs, center);
		VectorScale(center, 0.5, center);
	}
}

/*
============
EntityCenter

Just like EntityCenterWorldBounds() except it only
uses the entity's center.
============
*/
void EntityCenter(gentity_t * ent, vec3_t center)
{
	vec3_t          mins, maxs;

	// Compute the center and ignore the bounds
	EntityCenterWorldBounds(ent, center, mins, maxs);
}

/*
=====================
EntityCenterAllBounds

Computes the entity's center and both global and locally
oriented bounding boxs (as opposed to EntityCenterWorldBounds()
which only computes the world oriented bounding box).

NOTE: This code intentionally tries to use ent->r.mins and
ent->r.maxs when possible for the local minimums.  Remember
that ent->r.mins + ent->r.currentOrigin rarely equals
ent->r.absmin, which is what EntityWorldBounds() uses.
The Quake 3 code really does have two different kinds of
bounding boxes, and uses the world box in some situations
and the local box in others.  This is of course complicated
by movers which do not have a local box, but the AI code must
infer what it would be set to when necessary.  Some entities
also have no center origin but use their local minimums and
maximums as the world bounding box.  It's a huge mess and this
code is stuck having to deal with it.
=====================
*/
void EntityCenterAllBounds(gentity_t * ent, vec3_t center,
						   vec3_t world_mins, vec3_t world_maxs, vec3_t local_mins, vec3_t local_maxs)
{
	qboolean        derive;

	// Compute the center and world bounds
	EntityCenterWorldBounds(ent, center, world_mins, world_maxs);

	// Determine whether the entity's specified local bounding box
	// (ent->r.mins and maxs) can be used, or whether it must be derivec
	// from the world bounding box
	derive = qfalse;

	// The box must be derived if the bounding box wasn't actually set
	if(VectorCompare(ent->r.mins, vec3_origin) && VectorCompare(ent->r.maxs, vec3_origin))
	{
		derive = qtrue;
	}

	// Bounding boxes that don't contain the origin are probably hacked
	// implementations of a global box for an entity at center zero.  In
	// that case, the local bounding box must also be derived.
	else if(ent->r.mins[0] > 0 || ent->r.maxs[0] < 0 ||
			ent->r.mins[1] > 0 || ent->r.maxs[1] < 0 || ent->r.mins[2] > 0 || ent->r.maxs[2] < 0)
	{
		derive = qtrue;
	}

	// Derive the local bounding box if necessary ...
	if(derive)
	{
		VectorSubtract(world_mins, center, local_mins);
		VectorSubtract(world_maxs, center, local_maxs);
	}

	// ... Otherwise use the entity's specified local bounding box.
	else
	{
		VectorCopy(ent->r.mins, local_mins);
		VectorCopy(ent->r.maxs, local_maxs);
	}
}

/*
==============
EntityClipMask

Returns the clipping mask that should be
used for the entity's collision traces.
==============
*/
int EntityClipMask(gentity_t * ent)
{
	int             clip_mask;

	// Look up the base clipping mask
	clip_mask = ent->clipmask;

	// Bots have additional clipping masks, even though the server doesn't record
	// that fact in ent->clipmask
	if(ent->r.svFlags & SVF_BOT)
		clip_mask |= CONTENTS_BOTCLIP;

	// Pass the clip mask onto the caller
	return clip_mask;
}

/*
==============
EntityOnGround

Tests if an entity with the inputted position, velocity,
and bounding box would be on the ground.  Performs a ground
trace similar to PM_GroundTrace() in bg_pmove.c.  Returns
true if the entity in this position would be standing on
ground and false if not.

"ground_normal" will be filed out with the normal of the ground
surface that was found (and zeroed if no ground was found).
Note that a ground normal might be found and setup even if this
function returns false.  This is because entities are considered
to be "in the air" when they stand on very steep slopes and such.
This argument is not optional.  (Ie. it should not be NULL.)

If a ground surface was detected, "ground_flags" will be filled
with the flags of the ground surface will be stored in this
variable. If no ground was detected, it will be reset to 0x0000.

NOTE: The entity pointer is just used for basic static
information needed for the trace such as entity number.
All dynamic information (position, bounding box, etc.)
must be supplied in the call syntax.

NOTE: Velocity may be NULL instead of vec3_origin for
entities without a velocity.
==============
*/
qboolean EntityOnGround(gentity_t * ent, vec3_t origin,
						vec3_t mins, vec3_t maxs, vec3_t velocity, vec3_t ground_normal, int *ground_flags)
{
	vec3_t          ground;
	trace_t         trace;
	qboolean        ground_touch;

	// Trace down to the ground
	VectorSet(ground, origin[0], origin[1], origin[2] - 0.25);
	trap_Trace(&trace, origin, mins, maxs, ground, ent->s.number, EntityClipMask(ent));

	// If the trace started in a solid, the server can't find a ground plane,
	// so the entity is considered to be not on the ground
	if(trace.allsolid)
	{
		VectorClear(ground_normal);
		*ground_flags = 0x0000;
		return qfalse;
	}

	// Store the ground normal and flags
	ground_touch = (trace.fraction < 1.0);
	if(ground_touch)
	{
		VectorCopy(trace.plane.normal, ground_normal);
		*ground_flags = trace.surfaceFlags;
	}
	else
	{
		VectorClear(ground_normal);
		*ground_flags = 0x0000;
	}

	// Entities that are moving up in the air are never on the ground
	//
	// NOTE: This dot product checks that the velocity is sufficiently
	// different from the planar slope (since entities can have a positive
	// Z velocity while on the ground of a sloped surface).
	//
	// FIXME: Technically the velocity comparison should be reversed if
	// gravity is negative, but the server doesn't check that either.
	if((velocity) && (velocity[2] > 0.0) && (DotProduct(velocity, ground_normal) > 10))
	{
		return qfalse;
	}

	// The entity is on the ground if ground was detected that wasn't too
	// steep and considered to be in the air otherwise
	return ((ground_touch) && (ground_normal[2] >= MIN_WALK_NORMAL));
}

/*
=================
EntityOnGroundNow

NOTE: This function is much faster than the
general case, which uses a ground trace.
=================
*/
qboolean EntityOnGroundNow(gentity_t * ent)
{
	return (ent->s.groundEntityNum != ENTITYNUM_NONE);
}

/*
==================
EntityCrouchingNow

NOTE: The entity can crouch even when not on the ground
==================
*/
qboolean EntityCrouchingNow(gentity_t * ent)
{
	return (ent->client && ent->client->ps.pm_flags & PMF_DUCKED);
}

/*
================
EntityOnMoverNow

Returns the entity pointer for the mover this entity
is standing on, or NULL if not standing on a mover.
================
*/
gentity_t      *EntityOnMoverNow(gentity_t * ent)
{
	gentity_t      *ground;

	// NOTE: The check for entity 0 makes this code is bug-compatable
	// with the bug in FinishSpawningItem() in g_item.c where floating
	// items do not get their ground entity numbers initialized.
	if(!EntityOnGroundNow(ent) || ent->s.groundEntityNum == ENTITYNUM_WORLD || ent->s.groundEntityNum == 0)
	{
		return NULL;
	}

	// Technically the ground entity number should only be set to a "real" entity
	// if that entity is a mover, but it's best to check anyway
	ground = &g_entities[ent->s.groundEntityNum];
	if(ground->s.eType != ET_MOVER)
		return NULL;
	return ground;
}

/*
================
EntityWaterLevel

Returns the water level the inputted entity would
have if it were at the specified origin.  "crouch"
is true if the entity is crouching and false if not.

Recall that 0 means not in water at all, 1 means
wading, 2 means swimming with head above water, and
3 means swimming with head under water.
================
*/
int EntityWaterLevel(gentity_t * ent, vec3_t origin, qboolean crouch)
{
	int             entnum, contents, viewheight, mid_body, above_head;
	vec3_t          point;

	// Non-player entities should not have a water level
	if(!ent->client)
		return 0;

	// Cache the entity's number
	entnum = ent - g_entities;

	// Check if the entity's feet are touching any water
	VectorSet(point, origin[0], origin[1], origin[2] + MINS_Z + 1);
	contents = trap_PointContents(point, entnum);
	if(!(contents & MASK_WATER))
		return 0;

	// Compute some test heights for water immersion
	viewheight = (crouch ? CROUCH_VIEWHEIGHT : DEFAULT_VIEWHEIGHT);
	above_head = viewheight - MINS_Z;
	mid_body = above_head / 2;

	// Test if the center of the body isn't in water
	point[2] = origin[2] + MINS_Z + mid_body;
	contents = trap_PointContents(point, entnum);
	if(!(contents & MASK_WATER))
		return 1;

	// Test if at least the entity's head is clear of water
	point[2] = origin[2] + MINS_Z + above_head;
	contents = trap_PointContents(point, entnum);
	if(!(contents & MASK_WATER))
		return 2;

	// The entity is immersed in water
	return 3;
}

/*
===================
EntityInLavaOrSlime
===================
*/
qboolean EntityInLavaOrSlime(gentity_t * ent)
{
	vec3_t          feet;

	// Non-clients are never in danger this way
	if(!ent->client)
		return qfalse;

	// Check for lava right at the feet
	VectorSet(feet, ent->client->ps.origin[0], ent->client->ps.origin[1], ent->client->ps.origin[2] + MINS_Z + 1);
	return (trap_AAS_PointContents(feet) & (CONTENTS_LAVA | CONTENTS_SLIME));
}

/*
=============
EntityPhysics

Determines what kind of physics apply to this entity,
such as ground or water physics.  Fills out "physics"
structure describing these physics.

"origin", "mins", "maxs", and "velocity" describe the
location, speed, and bounding box for which physics should
be evaluated.  "water_level" is the entity's water level at
this location.  "flight" is true if the entity is flying
and "knockback" is true if the entity should have knockback
physics instead of ground physics (for example, when the
PMF_TIME_KNOCKBACK flag is set).

If the entity is resting on the ground, the normal of the
ground surface detected will be stored in "ground_normal".
Otherwise, the vector will be zeroed.
=============
*/
void EntityPhysics(gentity_t * ent, physics_t * physics,
				   vec3_t origin, vec3_t mins, vec3_t maxs, vec3_t velocity, int water_level, qboolean flight, qboolean knockback)
{
	int             ground_flags;

	// Default knockback style to the specified type
	physics->knockback = knockback;

	// Check if the entity is on relatively stable ground
	physics->walking = EntityOnGround(ent, origin, mins, maxs, velocity, physics->ground, &ground_flags);

	// Activate the knockback flag if the ground surface is slick
	if(ground_flags & SURF_SLICK)
		physics->knockback = qtrue;

	// Non-clients use the standard trajectory style movement
	if(!ent->client)
		physics->type = PHYS_TRAJECTORY;

	// Flying physics supercedes all other types
	else if(flight)
		physics->type = PHYS_FLIGHT;

	// Water physics have the next highest priority
	else if(water_level >= 2)
		physics->type = PHYS_WATER;

	// Entities walking on the ground use normal ground physics
	else if(physics->walking)
		physics->type = PHYS_GROUND;

	// The entity is falling in the air
	else
		physics->type = PHYS_GRAVITY;
}

/*
================
EntityPhysicsNow

Determines the type of physics apply to this entity
right now.  See EntityPhysics() for more information.

NOTE: This function ignores the more technical
physics information like the ground normal.
================
*/
int EntityPhysicsNow(gentity_t * ent)
{
	physics_t       physics;

	// Client entities prove more information
	if(ent->client)
	{
		EntityPhysics(ent, &physics,
					  ent->r.currentOrigin, ent->r.mins, ent->r.maxs,
					  ent->client->ps.velocity, ent->waterlevel,
					  ent->client->ps.powerups[PW_FLIGHT], (ent->client->ps.pm_flags & PMF_TIME_KNOCKBACK));
	}
	else
	{
		EntityPhysics(ent, &physics, ent->r.currentOrigin, ent->r.mins, ent->r.maxs, NULL, 0, qfalse, qfalse);
	}

	// Return the detected physics type
	return physics.type;
}

/*
==========
EntityTeam

TEAM_RED and TEAM_BLUE mean the entity belongs to that particular team
(eg. a red team player or the red flag).  TEAM_FREE means that all other
entities are equally opposed to that entity (eg. the neutral flag in one
flag CTF).  TEAM_SPECTATOR means that no entity is opposed to that entity
(eg. a rocket launcher).
==========
*/
int EntityTeam(gentity_t * ent)
{
	// Non-entities and movers (like shootable buttons) are on no one's team
	if(!ent || !ent->inuse || ent->s.eType == ET_MOVER)
		return TEAM_SPECTATOR;

	// Check client teams
	if(ent->client)
		return ent->client->sess.sessionTeam;

	// Check for items with identifiable teams
	if(ent->item && ent->item->giType == IT_TEAM)
	{
		switch (ent->item->giTag)
		{
			case PW_REDFLAG:
				return TEAM_RED;
			case PW_BLUEFLAG:
				return TEAM_BLUE;
			case PW_NEUTRALFLAG:
				return TEAM_FREE;
		}
	}

	// Check for obelisks by class name
#ifdef MISSIONPACK
	if(!Q_stricmp(ent->classname, "team_redobelisk"))
		return TEAM_RED;
	if(!Q_stricmp(ent->classname, "team_blueobelisk"))
		return TEAM_BLUE;
	if(!Q_stricmp(ent->classname, "team_neutralobelisk"))
		return TEAM_FREE;
#endif

	// The entity's team is unknown
	return TEAM_SPECTATOR;
}

/*
=============
EntityIsAlive
=============
*/
qboolean EntityIsAlive(gentity_t * ent)
{
	return (ent->inuse && ent->client && ent->client->ps.pm_type == PM_NORMAL);
}

/*
===============
EntityIsCarrier
===============
*/
qboolean EntityIsCarrier(gentity_t * ent)
{
#ifdef MISSIONPACK
	return (ent->client) && ((ent->s.powerups & ((1 << PW_REDFLAG) |
												 (1 << PW_BLUEFLAG) |
												 (1 << PW_NEUTRALFLAG))) || (gametype == GT_HARVESTER && ent->s.generic1 > 0));
#else
	return (ent->client) && (ent->s.powerups & ((1 << PW_REDFLAG) | (1 << PW_BLUEFLAG)));
#endif
}

/*
=================
EntityIsInvisible
=================
*/
qboolean EntityIsInvisible(gentity_t * ent)
{
	// The flag is always visible, as are firing players
	return ((ent->s.powerups & (1 << PW_INVIS)) &&
			(ent->client) && !(ent->client->ps.eFlags & EF_FIRING) && !EntityIsCarrier(ent));
}

/*
===============
EntityKillValue

Returns an estimate of how useful it is to kill this enemy.
===============
*/
float EntityKillValue(gentity_t * ent)
{
	float           value, bonus;

	// Unkillable things have no value
	if(!ent->takedamage || ent->health < 0)
		return 0.0;

	// Non-player objects have their own scoring system
	if(!ent->client)
	{
#ifdef MISSIONPACK
		// Obelisks are very valuable
		if(gametype == GT_OBELISK && ent->s.eType == ET_TEAM)
			return VALUE_OBELISK;
#endif

		// I have no idea what this entity could be, but don't get distracted by it
		return 0.2;
	}

	// Default: 1 point per kill
	value = VALUE_FRAG;

	// Killing carriers is more valuable
	if(EntityIsCarrier(ent))
	{
#ifdef MISSIONPACK
		if(gametype == GT_HARVESTER)
			// Bonuses depending on number of skulls carried
			bonus = VALUE_SKULL * ent->s.generic1;
		else
#endif
			// Some points for flag carriers
			bonus = VALUE_FLAG;

		// FIXME: Double the bonus when close to the capture spot?
		value += bonus;
	}

	return value;
}

/*
============
EntityHealth
============
*/
int EntityHealth(gentity_t * ent)
{
	int             health, max_armor, armor;

	// Player and non-player health totals are stored differently
	if(ent->client)
	{
		// Determine how much the armor contibutes towards the player's total health
		health = HealthArmorToDamage(ent->client->ps.stats[STAT_HEALTH], ent->client->ps.stats[STAT_ARMOR]);

		// Battlesuit prevents half damage, so that's like having double health
		if(ent->s.powerups & (1 << PW_BATTLESUIT))
			health *= 2;
	}
	else
	{
		health = ent->health;
	}

	// Just in case
	if(health <= 0)
		health = 1;
	return health;
}

/*
============
EntityRating

An estimate of how worthwhile it is for an enemy to spend
time attacking this entity.  Essentially it's Kill_Value / Health,
or a rating of how many points you get per unit of damage done.
============
*/
float EntityRating(gentity_t * ent)
{
	// And now, a mini-essay about computing the target's health value.  In
	// general, it's a bad idea for bots to cheat and directly lookup values
	// a player wouldn't have direct access to.  So the theoretically ideal
	// way of a bot tracking a target's health would be to estimate it from
	// pain sounds, items the player recently picked up, information from
	// teammates, the weapon they are using, the enemy's combat posture, and
	// so on.  This is an awful lot of work though, especially when we're
	// just using the health value for a really fuzzy heuristic.  So the bot
	// will just directly look up the player's health value and use that in
	// these equations.  Cheating by looking up "forbidden" information will
	// only create an unrealistic bot if that information is used in an
	// algorithm that needs precise data.
	//
	// The moral of the story: The programmer doesn't exist to serve the data
	// paradigm; the data paradigm exists to serve the programmer.

	return EntityKillValue(ent) / EntityHealth(ent);
}

/*
================
EntityTravelTime

This function estimates the travel time to an area and location
in seconds, returning -1 if the path is untraversable.

NOTE: See LevelTravelTime() for more information
================
*/
float EntityTravelTime(gentity_t * ent, int end_area, vec3_t end_loc, int tfl)
{
	int             start_area;
	float           time;
	float          *start_loc;

	// Determine where this entity is located
	start_area = LevelAreaEntity(ent);
	if(!start_area)
		return -1;

	// Use a client's most updated location if possible
	start_loc = (ent->client ? ent->client->ps.origin : ent->r.currentOrigin);

	// Estimate the travel time from the entity's area and location to the destination
	return LevelTravelTime(start_area, start_loc, end_area, end_loc, tfl);
}

/*
====================
EntityGoalTravelTime

Estimate the travel time from an entity to a goal.
====================
*/
float EntityGoalTravelTime(gentity_t * ent, bot_goal_t * goal, int tfl)
{
	return EntityTravelTime(ent, goal->areanum, goal->origin, tfl);
}
