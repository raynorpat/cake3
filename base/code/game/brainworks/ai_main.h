// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_main.h
 *
 * Includes for the primary AI front-end
 *****************************************************************************/


// Always gain access to these files
#include "../g_local.h"

#include "botlib.h"
#include "be_aas.h"
#include "be_ea.h"
#include "be_ai_char.h"
#include "be_ai_chat.h"
#include "be_ai_goal.h"
#include "be_ai_move.h"

#include "chars.h"				//characteristics
#include "inv.h"				//indexes into the inventory
#include "syn.h"				//synonyms
#include "match.h"				//string matching types and vars

#include "ai_lib.h"



////
////  Engine defined values
////

// Copied from the aas file header
#define PRESENCE_NONE				1
#define PRESENCE_NORMAL				2
#define PRESENCE_CROUCH				4

// NOTE: These are used for route prediction
#define AREACONTENTS_MOVER				1024
#define AREACONTENTS_MODELNUMSHIFT		24
#define AREACONTENTS_MAXMODELNUM		0xFF
#define AREACONTENTS_MODELNUM			(AREACONTENTS_MAXMODELNUM << AREACONTENTS_MODELNUMSHIFT)

#define MAX_CHARACTERISTIC_PATH		144


// NOTE: This value is not defined anywhere in the code, but the engine is pretty
// consistant about running the game server (and therefore running bot think frames)
// every 50 milliseconds.  If this code is ported to some other engine with a
// different frame duration, this value will need to get changed.
#define SERVER_FRAME_DURATION .05
#define SERVER_FRAME_DURATION_MS 50
#define SERVER_FRAMES_PER_SEC 20.0


// NOTE: Technically this isn't defined by the engine; it's defined in g_client.c.
// For reasons not known to mortals, the original coders decided no other files would
// ever want to know the default player bounding boxes.  This AI code respectfully
// disagrees.
extern vec3_t   playerMins, playerMaxs;



////
////  Functions
////

// NOTE: The functions in ai_main.c that are called by the rest of the
// code (g_*.c) are prototyped in g_local.h.  These declarations are
// only for functions that are called exclusively from ai_*.c.
void QDECL      BotAI_Print(int type, char *fmt, ...);



////
////  Setup
////

// Predeclare and typedef the bot state for structures that must have
// a pointer to the bot state, such as function pointers elements.
typedef struct bot_state_s bot_state_t;



////
////  Debug
////

// When defined, debug support is compiled into the build
#define DEBUG_AI

#ifdef DEBUG_AI

// Debugging flags that change bot behavior or provide output when turned on

	// Informative
#define BOT_DEBUG_INFO_ACCSTATS			0x00000001	// Cumulative weapon accuracy statistics
#define BOT_DEBUG_INFO_ACCURACY			0x00000002	// Tracking of weapon accuracy
#define BOT_DEBUG_INFO_AIM				0x00000004	// Aim selection
#define BOT_DEBUG_INFO_AWARENESS		0x00000008	// Awareness of enemy entities
#define BOT_DEBUG_INFO_DODGE			0x00000010	// Dodge selection
#define BOT_DEBUG_INFO_ENEMY			0x00000020	// Enemy selection
#define BOT_DEBUG_INFO_FIRESTATS		0x00000040	// Cumulative weapon fire rate statistics
#define BOT_DEBUG_INFO_GOAL				0x00000080	// Goal selection
#define BOT_DEBUG_INFO_ITEM				0x00000100	// Item selection
#define BOT_DEBUG_INFO_ITEM_REASON		0x00000200	// Reason for item selection
#define BOT_DEBUG_INFO_PATH				0x00000400	// Path planning for obstacle avoidance
#define BOT_DEBUG_INFO_SCAN				0x00000800	// Scanning of surrounding entities
#define BOT_DEBUG_INFO_TIMED_ITEM		0x00001000	// Timed item selection and tracking
#define BOT_DEBUG_INFO_WEAPON			0x00002000	// Weapon selection
#define BOT_DEBUG_INFO_SHOOT			0x00004000	// Whether and why the bot shoots

	// Behavioral
#define BOT_DEBUG_MAKE_DODGE_STOP		0x00008000	// Stop dodging when moving
#define BOT_DEBUG_MAKE_ITEM_STOP		0x00010000	// Stop selecting items to pick up
#define BOT_DEBUG_MAKE_MOVE_STOP		0x00020000	// Stop moving
#define BOT_DEBUG_MAKE_SHOOT_ALWAYS		0x00040000	// Always shoot
#define BOT_DEBUG_MAKE_SHOOT_STOP		0x00080000	// Stop shooting
#define BOT_DEBUG_MAKE_SKILL_STANDARD	0x00100000	// Fix weapon skill and accuracy
#define BOT_DEBUG_MAKE_STRAFEJUMP_STOP	0x00200000	// Stop strafe jumping when moving
#define BOT_DEBUG_MAKE_VIEW_FLAWLESS	0x00400000	// Incur no error when changing view
#define BOT_DEBUG_MAKE_VIEW_PERFECT		0x00800000	// Always look in the ideal direction

#endif



////
////  Action States
////

// Possible AI States for the bot
enum
{
	AIS_NONE = 0,				// No recorded state

	AIS_INTERMISSION,			// Bot is in intermission
	AIS_OBSERVER,				// Bot is an observer
	AIS_DEAD,					// Bot is dead
	AIS_ALIVE,					// Bot is alive in the game
};



////
////  Awareness
////

// The number of different attackable entities the bot is aware of
#define MAX_AWARE_ENTITIES	12

// A description of how aware the bot is of an entity
typedef struct bot_aware_s
{
	gentity_t      *ent;		// The entity the bot is aware of
	float           first_noted;	// Time the bot became aware of this entity
	float           sighted;	// Time the bot first sighted this entity, or -1 if the bot does not
	// currently have this entity in their line of sight and field of view.
} bot_aware_t;




////
////  Items and Regions
////

// Linked list node for item entities.  Useful for two kinds
// of lists-- other nearby items (for clusters) and items with
// the same name (for fast name searches).
typedef struct item_link_s
{
	gentity_t      *ent;		// The current item entity in the list
	struct item_link_s *next_near;	// Next item in the same cluster or NULL for end of list
	struct item_link_s *next_name;	// Next item with the same name or NULL for end of list
	float           contribution;	// What percentage of the cluster's value this item accounts for
	int             area;		// Entity's current area-- might need runtime updating
} item_link_t;

// Predeclare regions so clusters can reference them
struct region_s;

// The bot's notion of a cluster of items
//
// FIXME: Perhaps each item cluster could have its own text description
// of the item with context modifying adjectives (eg. "Upper Armor Shard Cluster").
typedef struct item_cluster_s
{
	item_link_t    *start;		// List of items contained in this cluster, or NULL for no list
	item_link_t    *center;		// The item in "start" linked list closest to the cluster's center
	float           value;		// How much more valuable this cluster is than an average cluster (or 0 for no more valuable)
	float           respawn_delay;	// Longest time it takes any item in this cluster to respawn (0 for non-respawning clusters)
	struct region_s *region;	// The region this cluster is currently in, or NULL for no region
} item_cluster_t;

// Items within this distance of each other belong to the same cluster
#define CLUSTER_RANGE	160


// Maximum number of regions a level can be divided into
#define MAX_REGIONS 128

// A region is defined by a static cluster.  It also has a list of
// the N nearest neighbors, plus a list of any dynamic cluster that
// happen to be near it for this frame.  The dynamic cluster list
// will change during runtime.  It includes both item clusters on
// movers (from the clusters_mobile) and dropped item (from
// clusters_dropped).
#define MAX_REGION_NEIGHBORS	12
#define MAX_REGION_DYNAMIC		3
typedef struct region_s
{
	item_cluster_t *cluster;	// Static cluster that defines this region's center

	struct region_s *local_neighbor[MAX_REGION_NEIGHBORS];	// Regions nearest to this one (including itself)
	struct region_s *path_neighbor[MAX_REGIONS][MAX_REGION_NEIGHBORS];
	// Regions nearest the path from this region to the indexed destination region
	int             visible;	// Bitmap of which local neighbors are visible from this region

	item_cluster_t *dynamic[MAX_REGION_DYNAMIC];	// Dynamic clusters in this region
	int             num_dynamic;	// Number of nearby dynamic clusters
} region_t;

// How frequently the bot forces a recomputation of nearby items. The items
// will never get recomputed faster than once per bot_thinktime, however.
#define ITEM_RECOMPUTE_DELAY 0.20

// Bots can track at most this many different timed item clusters
#define MAX_TIMED	3

// Bots will consider at most this many different clusters per frame
//
// NOTE: The +1 is for last frame's cluster, which is always considered even
// if it is neither timed nor a neighbor.
//
// NOTE: The *2 assumes one dynamic cluster per static cluster, which should
// be far more than will actually occur.
#define MAX_CLUSTERS_CONSIDER (1 + (MAX_TIMED) + (MAX_REGION_NEIGHBORS)*2)

// The maximum number of item pickups a bot will consider before going to the
// main goal.
//
// NOTE: If there are I items on the level, computing all subsets of P pickups
// will be proportionate to I^P.  As a result, the maximum number of pickups
// should be small.
#define MAX_PICKUPS 3



////
////  Traffic Statistics
////

// Statistical history data used to predict things
typedef struct history_s
{
	float           actual;		// Actual number of times an event occurred
	float           potential;	// Potential chances the event had to occur
} history_t;

// Average data from up to this many neighboring regions of a point when
// computing traffic statistics.
//
// NOTE: Increasing this value makes the estimation function of a location in space
// less disconnected, but also increases computation time.  The increase is logarithmic for
// small values and approaches a linear increase in time as the value increases to the
// number of regions actually in the level.
#define TRAFFIC_NEIGHBORS 4


// Under typical circumstances, a bot is this likely to see at least one other player
#define ENCOUNTER_RATE_DEFAULT 0.30



////
////  Resource Information
////

// The number of different types of items in the game.
// This value must be at least bg_numItems-1 (from bg_misc.c).  It
// would be nice to access bg_numItems directly, but doing so makes
// it impossible to statically allocate arrays of this size.
// bg_numItems-1 is 36 for Vanilla Quake 3 and 52 for Team Arena.
#define MAX_ITEM_TYPES	64

// Constants defining time during which a player's estimated health loss
// (or gain-- from regen, etc.) is defined by a simple two-piece linear
// equation (with the break at 100 health).
typedef struct health_modify_state_s
{
	float           time;		// Time when this state no longer applies, or -1 for forever

	float           damage_factor;	// Factor that modifies the base attacker damage rate
	float           health_low;	// Health gained/lost per second when health is no more than 100
	float           health_high;	// Health gained/lost per second when health is greater than 100
} health_modify_state_t;

// The number of health modification states the player resource state
// needs equals one plus the number of timed powerups a player could have
// which change the state.  These powerups are Regeneration (changes
// health_low/health_high), Battlesuit and Invisibility (changes damage
// factor).
#define MAX_HEALTH_MODIFY	4

// Constants describing changes in the player's ability to deal damage
typedef struct damage_modify_state_s
{
	float           time;		// Time when this state no longer applies, or -1 for forever

	float           damage_factor;	// Factor that modifies the base damage rate
	float           fire_factor;	// Factor that modifies how quickly the player can fire
#ifdef MISSIONPACK
	qboolean        ammo_regen;	// True if the player regenerates ammo
#endif
} damage_modify_state_t;

// The number of damage modification states the player resource state
// needs equals one plus the number of timed powerups a player could have
// which change the state.  These powerups are Quad Damage and Haste.
#define MAX_DAMAGE_MODIFY	3


// Play statistics and other bits of game information used to evaluate resources
//
// NOTE: These values are constant during resource evaluation, in contrast to
// the rest of resource state information which changes during prediction.
typedef struct play_info_s
{
	playerState_t  *ps;			// The player's state data (or NULL for no data)
	float           leader_point_share;	// Percentage of points held by opponents that the point leader has
	int             max_health;	// Maximum health the player can normally obtain

	float           received;	// Expected damage received per second of enemy attack
	float           reload[WP_NUM_WEAPONS];	// Expected time between shots with weapon while bot is in combat
	float           dealt[WP_NUM_WEAPONS];	// Expected damage dealt per firing of weapon
	int             weapon_order[WP_NUM_WEAPONS];	// Weapon ids sorted by descending damage_dealt[weapon] value

	float           deaths_per_damage;	// Percentage of a death per point of damage dealt to the player
	float           kills_per_damage;	// Percentage of a kill earned per damage dealt to an enemy
} play_info_t;


// A set of reasons that players can easily modify (such as health, ammo,
// and the chance of encountering an enemy).  This structure is used to
// predict how many points the bot will earn in a given situation.  By
// hypothesising the impact of picking up items on score, bots can determine
// and pickup the best item for their particular game situation.
//
// NOTE: The time, health, and ammo values all use floating points because
// precise estimates are needed.  For example, if a bot can either shoot 1
// or 2 rail gun shots, the expected number of rail shots is 1.5.  In that
// case, the railgun ammo must have 1.5 shots subtracted from it.
typedef struct resource_state_s
{
	// Constant information
	play_info_t    *pi;			// Play information and statistics

	// How long the player has to live
	float           health;		// Health total
	float           armor;		// Armor total
	                health_modify_state_t	// List of health change data sorted by time
	                health_mod[MAX_HEALTH_MODIFY];	// NOTE: The last entry should have timeout -1

	// How much damage the player can deal
	int             weapons;	// Bit array of weapons the player has
	float           ammo[WP_NUM_WEAPONS];	// Ammo remaining for each weapon
	                damage_modify_state_t	// List of damage change data sorted by time
	                damage_mod[MAX_DAMAGE_MODIFY];	// NOTE: The last entry should have timeout -1
	int             first_weapon_order;	// First index in pi->weapon_order[] whose weapon has ammo

	// Items that modify the effectiveness of other things in the resource state
	int             holdable;	// A holdable item (such as the Personal Teleporter)
	float           powerup[PW_NUM_POWERUPS];	// Timeouts for each powerup (-1 if it lasts forever)
	float           carry_value;	// Value of precious items the player carries (like a flag)
#ifdef MISSIONPACK
	int             persistant;	// The persistant powerup the player has, or 0 for none
#endif

	// How well the player is playing the game
	float           score;		// Estimated score gained since resource state started
	float           time;		// Estimated time spend during resource state extrapolation
} resource_state_t;

// Roughly how many points performing a different task is worth
#define VALUE_FRAG		1.0		// Killing a standard enemy
#define VALUE_SKULL		1.0		// Picking up a skull
#define VALUE_FLAG		7.0		// Picking up the enemy flag
#define VALUE_OBELISK	15.0	// Destroying the enemy obelisk



////
////  Weapon Statistics
////

// ID Tags for distance zone centers
typedef enum
{
	ZCD_ID_NONE = -1,			// No distance zone
	ZCD_ID_NEAR = 0,			// Near the bot
	ZCD_ID_MID,					// A reasonable distance
	ZCD_ID_FAR,					// Far from the bot
	ZCD_ID_VERYFAR,				// Very far from the bot

	ZCD_NUM_IDS					// Number of distance zone centers accuracy tracking is broken into
} zone_center_dist;

// Distance zone centers
#define ZCD_NEAR 	 192.0
#define ZCD_MID  	 384.0
#define ZCD_FAR  	 768.0
#define ZCD_VERYFAR	1280.0

// ID Tags for pitch zone centers
typedef enum
{
	ZCP_ID_NONE = -1,			// No pitch zone
	ZCP_ID_HIGH = 0,			// Notably above the bot
	ZCP_ID_LEVEL,				// At the bot's current level
	ZCP_ID_LOW,					// Notably below the bot

	ZCP_NUM_IDS					// Number of pitch zone centers accuracy tracking is broken into
} zone_center_pitch;

// The relative pitch angle associated with ZCP_ID_LOW
//
// NOTE: The pitch for ZCP_ID_HIGH is the negative
// of this and ZCP_ID_LEVEL is always 0.
//
// NOTE: Yes, a positive pitch value points down in this
// coordinate system.
#define ZCP_LOW   30.0

// Combat zones define themselves in relation to static center points
typedef struct zone_center_s
{
	zone_center_dist dist;		// ID Tag for distance center point
	zone_center_pitch pitch;	// ID Tag for pitch center point
} zone_center_t;

// Each combat zone is a weighted average of up to four nearby zone centers
#define MAX_ZONE_CENTERS 4

typedef struct combat_zone_s
{
	int             num_centers;	// Number of zone centers to average between
	zone_center_t   center[MAX_ZONE_CENTERS];	// List of zone centers to average between
	float           weight[MAX_ZONE_CENTERS];	// Weights of each zone center (should sum to 1.0)
	float           dist;		// Zone's distance from bot
	float           pitch;		// Bot's view pitch when aiming at this zone
} combat_zone_t;

// NOTE: These structures use floating points instead of integers because the data
// used to update them is extrapolated from similar (but not identical) data.

// Statistical damage data used to predict average damage under a certain condition
typedef struct damage_stats_s
{
	float           hits;		// Total number of hits scored
	float           damage;		// Total damage dealt when hitting with "hits"
} hits_damage_t;

// Data used to track bot's accuracy with a particular weapon in a particular combat situation
typedef struct bot_accuracy_s
{
	float           shots;		// Total number of shots taken
	float           time;		// Total time (in seconds) spent firing these shots
	hits_damage_t   direct;		// Statistics about direct hits
	hits_damage_t   splash;		// Statistics about splash hits
	history_t       attack_rate;	// Potential and actual seconds of fire time taken
} bot_accuracy_t;

// How much padding to add to accuracy records without a lot of data
#define ACCURACY_DEFAULT_TIME		8.0

// Maximum number of proximity mines and other missiles the bot can track
#define MAX_PROXMINES				32
#define MAX_MISSILE_DODGE			32

// Data the bot uses to track missiles it should dodge
typedef struct missile_dodge_s
{
	gentity_t      *bolt;		// Pointer to the missile to dodge
	vec3_t          pos;		// Current location of missile
	vec3_t          vel;		// Velocity vector of missile
	vec3_t          dir;		// Direction vector (normalized velocity)
	float           speed;		// Speed of missile (length of velocity)
} missile_dodge_t;

// Data the bot uses to track whether a fired missile was a hit or a miss
typedef struct bot_missile_shot_s
{
	gentity_t      *bolt;		// The missile that was fired
	int             weapon;		// The missile type (duplicated in case "bolt" is freed)
	combat_zone_t   zone;		// The enemy's zone when the missile was fired
} bot_missile_shot_t;

#define MAX_MISSILE_SHOT 64		// Track at most this many of the bot's own missiles-- includes prox mines



////
////  Goals
////

// This is the maximum number of different goals a bot will consider in its
// goal sieve.  Note that the ordered and non-ordered version of a goal take
// two separate sieve entries.  This is because they will probably have
// different priorities and definitely have different arguments.
#define MAX_GOALS 24

// Waypoints (used for patrolling and possibly other things)
typedef struct bot_waypoint_s
{
	int             inuse;
	char            name[32];
	bot_goal_t      goal;
	struct bot_waypoint_s *next, *prev;
} bot_waypoint_t;

// Different base locations have different indicies
typedef enum
{
	RED_BASE,
	BLUE_BASE,
	MID_BASE,

	NUM_BASES
} base_t;

// A function the bot calls to check if it wants to perform a specific goal
// (function dependant).  Returns the type of goal selected (see goal_type_t
// in ai_maingoal.c), or 0 (GOAL_NONE) if no goal was selected.  If a real
// goal type was returned, the function is responsible for setting up the
// selected goal in the input "goal" argument.
typedef int     goal_func_t(bot_state_t * bs, bot_goal_t * goal);

// Different types of goals.  Each goal type is uniquely associated by a name
// describing its purpose (eg. "Chose to camp" is different from "Ordered to camp").
//
// NOTE: These types do not have a one-to-one correlation with goal_func_t functions.
// A goal_func_t may set up any kind of goal it deems necessary.  Remember, the
// return value of a goal_func_t is the type of goal it created.  Of course, most of
// these functions do only create one type of goal.
typedef enum
{
	GOAL_NONE = 0,
	GOAL_AIR,
	GOAL_LEAD,
	GOAL_CAPTURE,
	GOAL_CAPTURE_WAIT,
	GOAL_ATTACK_CHOICE,
	GOAL_ATTACK_ORDER,
	GOAL_HELP_CHOICE,
	GOAL_HELP_ORDER,
	GOAL_ACCOMPANY_CHOICE,
	GOAL_ACCOMPANY_ORDER,
	GOAL_DEFEND_CHOICE,
	GOAL_DEFEND_ORDER,
	GOAL_PATROL,
	GOAL_INSPECT_CHOICE,
	GOAL_INSPECT_ORDER,
	GOAL_CAMP_CHOICE,
	GOAL_CAMP_ORDER,
	GOAL_GETFLAG_CHOICE,
	GOAL_GETFLAG_ORDER,
	GOAL_RETURNFLAG_CHOICE,
	GOAL_RETURNFLAG_ORDER,
	GOAL_ASSAULT_CHOICE,
	GOAL_ASSAULT_ORDER,
	GOAL_HARVEST_CHOICE,
	GOAL_HARVEST_ORDER,
} goal_type_t;

// Different values of goals.  Each goal type is associated with exactly one of
// these values.  Values are in points per second and estimate the number of points
// the bot gains per second for performing a goal.  Alternatively, they are the
// points the bot loses per second for doing something other than the main goal.
// These values are used by the item pickup code-- the higher the value, the less
// likely the bot will stray from its path to get a nearby item.
#define GOAL_VALUE_NONE		0	// Nothing important
#define GOAL_VALUE_VERYLOW	(1.0/40)	// Something passive like defending
#define GOAL_VALUE_LOW		(1.0/20)	// Something active but unimportant like attacking an enemy
#define GOAL_VALUE_MEDIUM	(1.0/12)	// Something important like getting the flag
#define GOAL_VALUE_HIGH		(1.0/ 8)	// Something really important like capturing the flag
#define GOAL_VALUE_CRITICAL	(1.0/ 4)	// Something pressing like killing the flag carrier

// The types of orders a bot can receive
typedef enum
{
	ORDER_NONE = 0,				// No order
	ORDER_ATTACK,				// Move in position to attack a player
	ORDER_GETFLAG,				// Get the flag
	ORDER_RETURNFLAG,			// Return the flag
	ORDER_HARVEST,				// Harvest skulls
	ORDER_ASSAULT,				// Attack the enemy base
	ORDER_HELP,					// Help a teammate fight
	ORDER_ACCOMPANY,			// Accompany a teammate to a destination
	ORDER_DEFEND,				// Defend a key area
	ORDER_CAMP,					// Ordered to camp somewhere
	ORDER_PATROL,				// Patrol some waypoints
	ORDER_ITEM,					// Get an item

	MAX_ORDERS
} order_type_t;



////
////  Path Planning
////

// Predeclare the obstacle and activator structures so they can point to each other
struct ai_obstacle_s;
struct ai_activator_s;

// Maximum number of obstacles an activator can activate;
// Also maximum number of activators an obstacle can have.
#define MAX_LINKS 8

// Maximum number of activator relays that can activate the same target id
#define MAX_RELAY 4

// Maximum number of areas an obstacle is allowed to block
#define MAX_BLOCK_AREAS 24

// Information about an obstacle in the game
// NOTE: Most doors are always considered open for the
// purposes of routing and will not use this structure.
typedef struct ai_obstacle_s
{
	gentity_t      *ent;		// Pointer to obstacle's game entity

	struct ai_activator_s *activator[MAX_LINKS];	// Activators that can unblock this obstacle
	int             num_activators;	// Number of activators in list

	int             block_area[MAX_BLOCK_AREAS];	// List of areas this obstacle blocks
	int             num_block_areas;	// Number of areas this obstacle blocks
	qboolean        block;		// True if obstacle is blocking these areas
} ai_obstacle_t;

// Information about something that activates an obstacle (such as a button)
typedef struct ai_activator_s
{
	gentity_t      *ent;		// Pointer to activator's game entity
	qboolean        shoot;		// True if activator must be shot to activate
	bot_goal_t      goal;		// Where to move to use the activator

	struct ai_obstacle_s *obstacle[MAX_LINKS];	// Obstacles this activator unblocks
	int             num_obstacles;	// Number of obstacles in list
} ai_activator_t;

// A list of obstacles the bot encounters on a path (including sub-paths to
// deactivate obstacles on the main path).
#define MAX_PATH_OBSTACLES 16
typedef struct path_obstacle_list_t
{
	int             num_obstacles;	// Number of obstacles encountered
	ai_obstacle_t  *obstacle[MAX_PATH_OBSTACLES];	// List sorted by model index of obstacles
	qboolean        blocked[MAX_PATH_OBSTACLES];	// True if obstacle is currently blocking movement
} path_obstacle_list_t;

// Information about a path the bot predicted towards a goal
// NOTE: The goal that created this path prediction is not saved;
// only its origin and area are saved.
typedef struct bot_path_s
{
	float           time;		// Time the bot should next predict a path to its goal
	int             start_area;	// The starting area of the path
	int             end_area;	// The end area of the path
	vec3_t          end_origin;	// The end location of the path
	bot_goal_t     *subgoal;	// An activator subgoal that helps complete this path (or NULL for no activation required)
	qboolean        shoot;		// True if the bot should shoot at the subgaol
	path_obstacle_list_t obstacles;	// List of obstacles the expects to encounter in the path
} bot_path_t;



////
////  Aiming
////

// Aim types
typedef enum
{
	AIM_NONE = 0,

	AIM_ACTIVATOR,				// Aim at activator goal
	AIM_JUMP,					// Aim in direction of jump
	AIM_ENEMY,					// Aim at enemy
	AIM_KAMIKAZE,				// Aim at kamikaze body
	AIM_MINE,					// Aim at mines
	AIM_MAPOBJECT,				// Aim at a map object (eg. q3tourney6 disco ball)
	AIM_SWIM,					// Aim in direction of swimming
	AIM_FACEENTITY,				// Aim at some specified entity
	AIM_MOVEMENT,				// Aim in direction suggested by movement code
	AIM_AWARE,					// Aim at an awareness trigger location
	AIM_STRAFEJUMP,				// Aim in a direction to make strafe jumping work
	AIM_GOAL,					// Aim at a goal
	AIM_SEARCH,					// Aim around strategically, searching for targets
} aimtype_t;



////
////  View States
////

// A structure defining an actual data value how the bot perceives it
typedef struct data_perceive_s
{
	float           real;		// The actual data value
	float           error;		// "real + error" is the perceived value of "real"
} data_perceive_t;

// One axis of a view state
typedef struct view_axis_s
{
	data_perceive_t angle;		// Actual and perceived view angle in degrees
	data_perceive_t speed;		// Actual and perceived view speed in degrees per second

	float           time;		// Timestamp when this data was generated

	float           error_factor;	// When error is added to this view state axis, it is
	// proportional to this factor (which will presumably be
	// reselected every so often).
	//
	// NOTE: Some view state algorithms treat this factor as
	// a scalar of view velocity while others treat it as a
	// scalar of acceleration (which eventually turns into a
	// velocity scalar).  Determining exactly what this factor
	// represents is up to the discretion of the function
	// modifying a view axis.  This is a unitless value, so it
	// can conceivably be applied in many ways.

	float           max_error_factor;	// The maximum the absolute value of error_factor can be.
} view_axis_t;



////
////  Motion
////

// Physics types
typedef enum
{
	PHYS_UNKNOWN = 0,			// An unknown type of physics

	PHYS_TRAJECTORY,			// Non-player trajectory physics
	PHYS_GRAVITY,				// Air physics with gravity applied
	PHYS_GROUND,				// Ground-based physics
	PHYS_WATER,					// Swimming in water physics
	PHYS_FLIGHT,				// Flight powerup physics
} physics_type_t;

// Everything necessary to compute local physics decisions
typedef struct physics_s
{
	int             type;		// Type of physics (see PHYS_*)

	vec3_t          ground;		// Ground normal, or (0,0,0) for no ground

	qboolean        knockback;	// True if knockback physics should be applied (from taking
	// a hit or from standing on a slick surface)

	qboolean        walking;	// True if on walkable ground and false if not
	//
	// NOTE: This is analogous to the pml.walking flag, which
	// is in turn equivalent to testing if:
	//   groundEntityNum != ENTITYNUM_NONE
	//
	// NOTE: Walking on the ground includes cases such as an
	// entity with the flight powerup flying over the ground,
	// not just typical ground movement situations.
	//
	// NOTE: A ground normal could exist even if this flag is
	// false-- maybe the ground is just really steep.
} physics_t;

// One frame of an entity's motion data at an instant in time
typedef struct motion_state_s
{
	float           time;		// Timestamp the entity claims this data refers to
	//
	// NOTE: This is not necessarily the time the server actually
	// processed (or will process) this motion state.  For example,
	// human clients are updated asynchronously with the server.
	// Their update times are allowed to lag behind or get slightly
	// ahead of the server time.  The time separation shouldn't be
	// too large (since that would be a speed hack), but it still exists.
	//
	// See usercmd_t.serverTime and playerState_t.commandTime
	// usage in ClientThink_real() in g_active.c for more information.

	vec3_t          origin;		// Position
	vec3_t          velocity;	// Speed

	vec3_t          mins;		// Local bounding box minimums
	vec3_t          maxs;		// Local bounding box maximums

	vec3_t          absmin;		// Global bounding box minimums
	vec3_t          absmax;		// Global bounding box maximums


	int             clip_mask;	// Clipping mask the server uses for this entity's movement
	int             flags;		// Flags for this entity (see EF_* in bg_public.h for more information)
	qboolean        crouch;		// True if the entity is crouching and false if not
	qboolean        flight;		// True if the entity can fly
	float           max_speed;	// Entity's maximum speed (or 0.0 for non-moving entities)

	int             move_flags;	// Flags modifying the enemy's movement characteristics for a
	// period of time.  (See PMF_TIME_* in bg_public.h for more
	// information
	//
	// NOTE: This variable is related to ps->pm_flags

	float           move_time;	// Time (relative to "time") at which the movement flags expire
	//
	// NOTE: This variable is related to ps->pm_time

	char            forward_move;	// Entity's last usercmd_t.forwardmove
	char            right_move;	// Entity's last usercmd_t.rightmove
	char            up_move;	// Entity's last usercmd_t.upmove
	vec3_t          view;		// Entity's command view angles

	int             water_level;	// Entity's water level, computed from motion state data
	physics_t       physics;	// Entity's current physics, computed from motion state data

} motion_state_t;



////
////  Movement
////

// These bitfields combine to describe the actual direction of a bot's move
#define MOVE_STILL		0x0000
#define MOVE_FORWARD	0x0001
#define MOVE_BACKWARD	0x0002
#define MOVE_RIGHT		0x0004
#define MOVE_LEFT		0x0008
#define MOVE_UP			0x0010
#define MOVE_DOWN		0x0020

// Movement modification styles
#define MM_WALK			0x0001	// Use the move state's suggested movement speed
#define MM_JUMP			0x0002	// Jump in the bs->jump_dir direction
#define MM_SWIMUP		0x0004	// Keep swimming up to keep the bot's head above water
#define MM_STRAFEJUMP	0x0008	// Strafejump forward for extra speed
#define MM_DODGE		0x0010	// Move semi-randomly forward

// Movement jump or crouch style
#define MJC_STRAFEJUMP	2
#define MJC_NAVJUMP		1
#define MJC_NONE		0
#define MJC_CROUCH		-1



////
////  Dodging
////

// A structure describing how effective a dodge is
typedef struct dodge_info_s
{
	int             dodge;		// Dodge description bitmask-- see MOVE_XXX in ai_main.h for more information
	vec3_t          dir;		// Direction of dodge
	float           damage;		// Expected damage received from dodging in that direction
	float           heading;	// Estimate of how well the direction avoids incoming missiles
	// (Lower means the dodge is better at avoiding shots.)
} dodge_info_t;



////
////  Weapon Descriptions
////

// A description of a weapon
//
// NOTE: The "correct_root" and "accuracy" fields are computed from other values
// in the structure and then cached.  They do not need to be correct when statically
// initialized.
typedef struct weapon_stats_s
{
	char           *name;		// Name of the weapon
	float           reload;		// Time in seconds weapon takes to reload
	int             shots;		// Number of shots the weapon sends each time it fires
	float           damage;		// Damage from a direct hit
	float           splash_damage;	// Damage per splash hit at closest range, or 0 for no splash
	float           radius;		// Radius of splash damage, or 0 for no splash
	float           speed;		// Speed at which shot travels, or 0 for instant hit
	float           range;		// Maximum distance shot can travel, or 0 for no maximum
	float           spread;		// Maximum degrees of spread this weapon's firing has
	int             flags;		// Flags describing weapon behavior-- see WSF_* for more info
	int             start_ammo;	// How much ammo a spawned player starts with of this type if
	// new players start with this weapon, or 0 if not.

	float           accuracy;	// How likely a shot from the weapon is to hit
} weapon_stats_t;

// Weapon stats flags
#define WSF_NONE  0x0000		// No flags
#define WSF_MELEE 0x0001		// Set if the weapon only triggers when close to an enemy
#define WSF_DELAY 0x0002		// Set if missile detonation is delayed on contact with a wall

// Description of damage dealt to a catagory of targets
typedef struct damage_catagory_s
{
	int             hits;		// Total number of hits dealt to entities in this catagory
	float           total;		// Total damage dealt to catagory
	float           max;		// Maximum damage dealt to an instance in the catagory
} damage_catagory_t;

// Damage analysis reporting structure for things that damage
// more than one thing at the same time (like blast damage)
typedef struct damage_multi_s
{
	damage_catagory_t all;		// Damage dealt to all entities
	damage_catagory_t enemy;	// Damage dealt to enemies
	damage_catagory_t team;		// Damage dealt to teammates
} damage_multi_t;



////
////  Attacking
////

// Description of what the bot is attacking and how it will attack them
typedef struct bot_attack_state_s
{
	gentity_t      *ent;		// Entity the bot is attacking

	vec3_t          shot_loc;	// World location the bot should shoot at to hit this entity
	// NOTE: This is not necessarily the same as the entity's origin
	vec3_t          reference;	// Visual reference point for where the entity currently is

	motion_state_t  motion;		// The entity's motion state extrapolated to the time of shot
	// impact

	float           sighted;	// Time when the bot first sighted this target, or -1 if the
	// target hasn't been sighted yet.  The value is 0 if it's not
	// known whether the target has been sighted, and the bot will
	// assume the target has been sighted for a long time in this case.
} bot_attack_state_t;



////
////  The Bot State
////

struct bot_state_s
{

	//
	// General information
	//
	// References to and usage of game data
	// NOTE: client and entitynum will probably be the same, but they are used
	// for different things.
	qboolean        inuse;		// True if this state is used by a bot client
	gentity_t      *ent;		// The bot's entity object.  DO NOT MODIFY THIS!
	playerState_t  *ps;			// Cached &bs->ent->client->ps.  DO NOT MODIFY THIS!
	int             client;		// Client number of the bot (used for client commands)
	int             entitynum;	// Entity number of the bot (used for traces)

	// Settings and characterization
	bot_settings_t  settings;	// Several bot settings, including skill and character data
	int             character;	// The bot's character id
	float           react_time;	// How long it takes the bot to react to an event

	// Logic processing
	int             setup_count;	// Number of think frames the bot must wait before being
	// setup (and able to do normal logic), or 0 if already setup.
	int             logic_time_ms;	// How much time has accrued since the bot last ran conscious logic
	int             ai_state;	// Primary state of AI (eg. observer, ingame, dead, intermission)
	float           respawn_time;	// First time after death when bot will try to respawn

	// Client command
	usercmd_t       cmd;		// The command the bot will send this frame
	float           command_time;	// The command's timestamp (cmd.serverTime) in seconds
	int             last_command_time_ms;	// Timestamp in milliseconds of the last movement frame the
	// bot knew the server processed (based on ps->commandTime)


	// Cached values used to detect changes in entity/client/player state
	int             last_eFlags;	// Copy of ps->eFlags
	int             last_damageEvent;	// Copy of ps->damageEvent
	gentity_t      *last_hurt_client;	// Copy based on client->lasthurt_client
	int             last_hit_count;	// Copy of ps->persistant[PERS_HITS]

#ifdef DEBUG_AI
	// Debug information
	int             debug_flags;	// Bitmap of BOT_DEBUG_XXX flags this bot has turned on
	int             use_weapon;	// Weapon bot is forced to use
#endif


	//
	// Scanning
	//
	// General information
	float           last_target_scan_time;	// The last time the bot scanned for targets

	// Events
	int             last_event_type[MAX_CLIENTS];	// Event type the bot last processed from the indexed client
	int             last_event_time[MAX_CLIENTS];	// Timestamp of the last event the bot last processed from the indexed client

	// Entities
	int             nearby_enemies;	// Number of nearby enemies.
	int             nearby_teammates;	// Number of nearby teammates.  Doesn't count the bot itself, so it could be zero.
	gentity_t      *team_carrier;	// Entity of a visible team carrier
	gentity_t      *enemy_carrier;	// Entity of a visible enemy carrier
	float           enemy_score;	// Highest score for killing a nearby enemy (or 1.0 by default)

	// Awareness
	tvl_t           aware;		// Timed value list of entities the bot is aware of
	bot_aware_t    *aware_record[MAX_AWARE_ENTITIES];	// Records of entities the bot is aware of
	float           aware_timeout[MAX_AWARE_ENTITIES];	// The expiration times of the awareness entries
	float           aware_value[MAX_AWARE_ENTITIES];	// The cached ratings of each awareness entry
	vec3_t          aware_location;	// A location to look at for something of interest
	float           aware_location_time;	// Time at which awareness location times out
	qboolean        damaged;	// True if bot was damaged this turn
	qboolean        chat_attack;	// True if the bot will attack a chatting player


	//
	// Chatting
	//
	// General Chat information
	int             cs;			// Engine chat state id
	int             chat_style;	// Style of a chat the bot has queued up
	int             chat_client;	// Some queued chat messages have a client argument
	float           chat_time;	// Time the bot should give its next chat, or 0 for no queued chat
	float           last_chat_time;	// Time the bot last selected a chat

	// Entering the game
	qboolean        chat_enter_game;	// True if the bot has given a chat message for entering the game
	float           enter_game_time;	// Time when the bot first entered the game

	// Bot death
	int             bot_death_type;	// How the bot last died (see meansOfDeath_t in bg_public.h)
	qboolean        bot_suicide;	// True if the bot last died from its own weapon fire
	gentity_t      *last_killed_by;	// Player that last killed this bot

	// Enemy death
	gentity_t      *killed_player;	// The player the bot last killed
	float           killed_player_time;	// Time the bot killed killed_player
	int             killed_player_type;	// How the bot killed killed_player (see meansOfDeath_t)
	gentity_t      *suicide_enemy;	// Enemy player who last suicided
	float           suicide_enemy_time;	// Time when chat_suicide_enemy last suicided


	//
	// Teamplay
	//
	// Preferences
	int             team_preference;	// Team task preference (offense or defense)
#ifdef MISSIONPACK
	int             team_task;	// The bot's currently selected team task (see TEAMTASK_XXX)
#endif

	// Leadership negotiation
	gentity_t      *leader;		// The bot's team leader, or NULL for no leader
	float           leader_ask_time;	// When the bot asked to who the team leader was
	float           leader_become_time;	// When the bot will volunteer to become the team leader

	// Leadership actions-- these only apply if the bot is the team leader
	qboolean        team_orders_sent;	// True if this bot has recently sent team orders
	int             last_teammates;	// Number of players on the bot's team last frame
	float           last_capture_time;	// Last time either team captured the flag
	int             team_strategy;	// Flags about the team's current strategy-- See STRATEGY_XXX's in ai_team.c
	float           give_orders_time;	// Time when bot will issue new team orders, or 0 for never

	// Flag location and status
	gentity_t      *our_target_flag;	// Entity that has the flag our team wants (Our carrier or their flag object)
	gentity_t      *their_target_flag;	// Entity that has the flag their team wants (Their carrier or our flag object)
	int             our_target_flag_status;	// The status of the flag their team wants to pickup (see FS_*)
	int             their_target_flag_status;	// The status of the flag our team wants to pickup (see FS_*)

	// Subteams
	char            subteam[32];	// Name of the subteam the bot belongs to, if any
	float           formation_dist;	// Spacing bot maintains from other subteam members


	//
	// Main Goal
	//
	// The goal sieve
	goal_func_t    *goal_sieve[MAX_GOALS];	// List of goal check functions in the goal sieve
	int             goal_sieve_size;	// Number of goal check functions in the goal sieve
	qboolean        goal_sieve_valid;	// True if the goal sieve has been computed
	float           goal_sieve_recompute_time;	// Time at which the bot should recompute the goal sieve,
	// or 0 if there are no future plans to recompute

	// General information
	bot_goal_t      goal;		// The last selected goal (main goal, item goal, or a subgoal for one of these)
	float           goal_value;	// Value in points per second of pursuiing the bot's main goal
	int             goal_type;	// Type of the bot's last selected main goal
	int             goal_entity;	// Entity of the bot's last selected main goal
	int             goal_area;	// Area of the bot's last selected main goal
	bot_path_t      main_path;	// Path prediction to the bot's main goal

	// Order information
	int             order_type;	// Type of goal bot was ordered to do
	gentity_t      *order_requester;	// The teammate who gave the bot this order
	float           order_time;	// Timeout time for current order
	float           order_message_time;	// Time the bot should acknowledge that it heard an order
	qboolean        announce_arrive;	// True if the bot should announce when it arrives
	// at an ordered goal start location
	// Air
	bot_goal_t      air_goal;	// The bot's last used air goal
	float           air_goal_time;	// The time at which air_goal was last computed
	float           last_air_time;	// The last time the bot breathed air

	// Enemy
	gentity_t      *goal_enemy;	// Enemy bot wants to get in a better position to attack

	// Help
	gentity_t      *help_teammate;	// Teammate the bot last chose to help (or was ordered to)
	int             help_notseen;	// Last time help_teammate wasn't visible

	// Accompany goal
	gentity_t      *accompany_teammate;	// Teammate the bot chose to accompany (or was ordered to)
	int             accompany_seen;	// Last time accompany_teammate was visible

	// Defend goal
	bot_goal_t      defend_goal;	// Location the bot wants to defend

	// Camping
	bot_goal_t      camp_goal;	// Location the bot should camp
	float           end_camp_time;	// When the bot chooses to camp on its own, it camps until this time
	float           last_camp_time;	// The last time the bot chose to camp on its own

	// Patrolling
	bot_waypoint_t *checkpoints;	// List of named checkpoints the bot is tracking
	// (created from MSG_CHECKPOINT messages)
	bot_waypoint_t *patrol;		// Ordered list of waypoints to visit in the patrol, or NULL for no patrol
	bot_waypoint_t *next_patrol;	// The next patrol waypoint the bot must reach
	int             patrol_flags;	// Flags describing aspects of the current patrol

	// Item inspection
	bot_goal_t      inspect_goal;	// The selected item inspection goal
	item_cluster_t *inspect_cluster;	// The item cluster the bot chose to inspect, or NULL if nothing was selected
	float           inspect_time_end;	// When the bot chooses to inspect on its own, it inspect until this time
	float           inspect_time_last;	// The last time the bot chose to inspect an item on its own

	// Attack order
	gentity_t      *order_enemy;	// An enemy the bot was ordered to kill

	// Leading teammates
	gentity_t      *lead_teammate;	// The teammate the bot is leading or NULL for no leading
	int             lead_requester;	// The teammate who requested the bot lead someone
	float           lead_time;	// The bot will lead lead_teammate until this time
	float           lead_message_time;	// The time at which the bot should acknowledge the lead request
	float           lead_visible_time;	// The last time lead_teammate was visible
	qboolean        lead_announce;	// True if the bot needs to tell lead_teammate that it's leading it



	//
	// Item Goal
	//
	// General information
	qboolean        item_setup;	// True if the bot has setup all data required for item pickups
	item_cluster_t *item_clusters[MAX_PICKUPS];	// The sequence of item clusters the bot wants to pickup
	gentity_t      *item_centers[MAX_PICKUPS];	// Center entities of item_clusters
	int             num_item_clusters;	// The number of item clusters in the bot's pickup sequence
	gentity_t      *item_ent;	// Last selected entity of item_cluster[0]


	// Item reselection
	float           item_time;	// Time the bot should next recompute item pickups
	int             item_cluster_count;	// Number of items in item_cluster[0] that are currently spawned in
	int             item_maingoal_area;	// The area the main goal was in when item_cluster was computed
	int             item_bot_damage;	// The amount of damage the bot could sustain when the bot last computed items

	// Path planning to goal
	bot_path_t      item_path;	// Path prediction to the bot's item goal

	// Timed item clusters
	tvl_t           timed_items;	// Timed value list of item clusters the bot is timing
	item_cluster_t *timed_item_cluster[MAX_TIMED];	// The cluster pointers the bot is timing
	float           timed_item_timeout[MAX_TIMED];	// The time at which the bot no longer knows when the cluster will respawn
	float           timed_item_value[MAX_TIMED];	// The cached cluster ratings of each timed cluster

	// Damage statistics
	int             damage_received;	// Total damage the bot has received
	int             deaths;		// Total number of times the bot has died
	int             damage_dealt;	// Total damage the bot has dealt
	int             kills;		// Total number of kills the bot has earned
	float           enemy_attack_time;	// Total number of seconds enemies have spent attacking the bot
	int             last_health;	// Bot's health last frame
	int             last_armor;	// Bot's armor last frame



	//
	// Movement
	//
	// General information
	int             ms;			// Engine move state id
	int             travel_flags;	// Travel flags the bot will use this frame
	int             move_modifiers;	// Bitmask of possible movement modifiers the bot could apply this
	// frame or did apply last frame-- see MM_XXX's
	float           last_move_time;	// Time at which the bot last queried the engine for a movement
	// direction to move towards the bot's goal
	int             avoid_method;	// Description of the bot's obstruction avoidance plans-- see AVOID_XXX's in ai_move.c
	int             move_area;	// Last area the bot tried to move towards
	motion_state_t  now;		// Motion state as understood by the current server status
	motion_state_t  future;		// Expected motion state after the server processes movement

	// Strafe jumping
	vec3_t          strafejump_angles;	// Where the bot should aim during strafe jumping

	// Dodging
	int             dodge;		// Bitmap of bot's selected dodge-- see MOVE_XXX for more info
	float           dodge_chance;	// Maximum percent of time bot will spend dodging
	float           dodge_select;	// Time when bot selected its current dodge
	float           dodge_timeout;	// Time when bot should choose a new dodge direction
	missile_dodge_t missile_dodge[MAX_MISSILE_DODGE];	// Nearby missiles the bot wants to dodge
	int             num_missile_dodge;	// Number of missiles the bot wants to dodge
	qboolean        new_missile;	// True if a new missile was detected this frame

	// Patches for bugs in movement engine
	vec3_t          jump_start;	// Where the bot's required movement jump should start
	vec3_t          jump_dir;	// Where the bot's required movement jump should head
	vec3_t          jump_edge;	// Normal vector to the edge that contains jump_start
	qboolean        jump_backup;	// True if the bot should backup to get enough space to jump



	//
	// Aiming
	//
	// General aiming information
	vec3_t          eye_now;	// The bot's current view location
	vec3_t          eye_future;	// Estimate of the view location after the server processes movement
	vec3_t          eye_last_aim;	// The eye location used the last time the bot aimed at a point
	float           aim_accuracy;	// How few mistakes the bot makes aiming
	float           aim_skill;	// How well the bot corrects for aiming mistakes;
	// Also how complicated of an aim the bot will try

	// Changing aim direction
	float           teleport_time;	// Time the bot last teleported (bot won't aim if it recently teleported)

	// Aim change detection
	int             aim_type;	// Type of aiming bot did last frame
	gentity_t      *aim_ent;	// Entity the bot last aimed at
	vec3_t          aim_loc;	// Location the bot last aimed at

	// Enemy
	gentity_t      *teleport_enemy;	// Enemy that last teleported in nearby
	float           teleport_enemy_time;	// Time teleport_enemy teleported in
	vec3_t          teleport_enemy_origin;	// Location that teleport_enemy teleported to
	int             enemy_health;	// Estimate of enemy's health based on pain sounds
	gentity_t      *aim_enemy;	// Entity of enemy bot wants to aim at
	vec3_t          aim_enemy_move_dir;	// Last direction the aim enemy tried to move in
	combat_zone_t   aim_zone;	// Combat zone of aim enemy

	// Entity to face towards
	gentity_t      *face_entity;	// An entity the bot should try to face, or NULL for no entity

	// Searching (where the looks when there's nothing worth looking at)
	vec3_t          search_target;	// Bot's current search target location
	float           search_timeout;	// Select a new search target at this time

	// Deactivating dangerous objects
#ifdef MISSIONPACK
	gentity_t      *kamikaze_body;	// A kamikaze body the bot wants to blow up
	gentity_t      *proxmines[MAX_PROXMINES];	// A list of proximity mines to possibly blow up
	int             num_proxmines;	// The number of proximity mines to blow up
	float           mine_deactivate_time;	// Time the bot will stop trying to deactivate mines
#endif



	//
	// View States
	//
	// Actual View
	view_axis_t     view_now[2];	// Where the bot is aiming right now
	// (error represents where it thinks it's aiming)
	vec3_t          forward;	// Forward direction vector generated from the bot's actual view

	// Ideal View
	float           view_ideal_reset_time;	// Last time the ideal view was reset to something new
	vec3_t          view_ideal_speeds_fixed;	// What the ideal view speeds would have been last frame
	// had the bot not moved.  This variable helps the bot
	// ignore its own movement when checking  for view changes.
	view_axis_t     view_ideal_next[2];	// Next ideal view state
	// (error represents where the bot chose to aim)
	view_axis_t     view_ideal_last[2];	// Last ideal view state
	// (error represents where the bot chose to aim)

	// Error Management
	float           view_ideal_error_time;	// Last time when ideal view errors were updated
	float           view_actual_error_time;	// Last time when actual view errors were updated



	//
	// Attacking
	//
	// Current Weapon
	int             weapon;		// Weapon the bot wants to use
	unsigned int    weapons_available;	// Bitmap list of weapons the bot can use

	// Attack Target
	bot_attack_state_t attack;	// What the bot wants to attack and how it will do so

	// Firing
	qboolean        fire_choice;	// True if the bot wants to fire its weapon this frame
	float           fire_start_time;	// Time when the bot will start firing, or -1 for no firing
	float           fire_stop_time;	// Time when the bot will stop firing, or -1 for no change in state

	// Weapon efficiency statistics
	bot_accuracy_t  acc_weap_zone[WP_NUM_WEAPONS]	// Accuracy statistics for each weapon and aim zone
		[ZCD_NUM_IDS][ZCP_NUM_IDS];
	bot_accuracy_t  acc_weapon[WP_NUM_WEAPONS];	// Stats for each weapon (sum over all zones)

	float           weapon_analysis_time;	// The bot has analyzed the firing and accuracy of its weapon
	// up until this server time.
	//
	// NOTE: This will be a time in the future when the bot attacks.
	// If the bot fired at time T and incurred 1 second of reload,
	// then at the next server frame T + 0.05, the bot will have
	// analyzed through time T+1.

	bot_missile_shot_t own_missiles[MAX_MISSILE_SHOT];	// Information about missiles this bot shot.
	// used for accuracy tracking.  This list
	// is sorted by entity pointer.
	int             num_own_missiles;	// Number of missiles the bot is tracking

	int             last_reload_delay_ms;	// Number of milliseconds of weapon reload wait time (based on
	// ps->weaponTime) the bot had after the server processed its
	// last command (see last_command_time) and decreased
	// ps->weaponTime (but before any effects that increase this
	// value, such as shooting).
	//
	// NOTE: Since ps->weaponTime can be negative, so can this value.
	// See comments in BotAccuracyShotCountHitscan() in ai_weapon.c
	// for more information

	float           melee_time;	// Time in seconds when the bot's current melee weapon
	// started firing, or 0 if the weapon is not firing

	float           weapon_rate;	// How many times faster than normal this weapon reloads.
	// See PM_Weapon() in bg_pmove.c for more information.

	// Weapon accuracy and skill characteristics
	float           weapon_char_acc[WP_NUM_WEAPONS];	// Bot's accuracy rating characteristic for each weapon
	float           weapon_char_skill[WP_NUM_WEAPONS];	// Bot's skill rating characteristic for each weapon

};
