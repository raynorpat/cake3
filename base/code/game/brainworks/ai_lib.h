// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_lib.h
 *
 * Includes for common library-style functions used in the AI code
 *****************************************************************************/

typedef int cmp_t(const void *, const void *);

// FIXME: These should be in q_shared.h but aren't for some reason
#ifndef M_SQRT2
#define M_SQRT2    1.4142135623730950488f
#endif

#ifndef M_SQRT1_2
#define M_SQRT1_2  -.70710678118654752440f
#endif

#define BITS_PER_BYTE 8

// A list of the contents of a vector, useful in printf statements
#define VectorList(x) (x)[0], (x)[1], (x)[2]


// Sorted lists are differentiated by their sorting key value.  For example,
// there are integer lists, sorted by a number, and string lists, sorted
// alphabetically by their string contents.  These are the base key classes
// for the lists.  Think of them as the minimum data structure required to
// create a sorted list.  If C supported class inheritance, these would be
// base classes from which other list entries would derive themselves.

// Generic list entry with an integer key
typedef struct
{
	int             key;
} entry_int_t;

// Generic list entry with a floating point key
typedef struct
{
	float           key;
} entry_float_t;

// Generic list entry with a string key
//
// NOTE: The key contents are allocated elsewhere, so technically
// the key in this structure will have type char ** when passed
// into functions using entry_string_t *.
typedef struct
{
	char           *key;
} entry_string_t;


// Some advanced list types, with key/value pairs.

// Entry with integer key and associated integer value
typedef struct
{
	int             key;
	int             value;
} entry_int_int_t;

// Entry with integer key and associated gentity_t pointer value
typedef struct
{
	int             key;
	struct gentity_s *value;
} entry_int_gentity_t;

// Entry with floating point key and associated vector value
typedef struct
{
	float           key;
	vec3_t          value;
} entry_float_vec3_t;

// Entry with floating point key and associated integer
typedef struct
{
	float           key;
	int             value;
} entry_float_int_t;

// Entry with floating point key and associated gentity_t pointer value
typedef struct
{
	float           key;
	struct gentity_s *value;
} entry_float_gentity_t;

// Entry with string key and associated integer value
typedef struct
{
	char           *key;
	int             value;
} entry_string_int_t;

// Entry with string key and associated floating point value
typedef struct
{
	char           *key;
	float           value;
} entry_string_float_t;

// Entry with unknown data type key and associated value
typedef struct
{
	void           *key;
	void           *value;
} entry_void_void_t;


// Comparator declartions
int QDECL       CompareVoid(const void *a, const void *b);
int QDECL       CompareVoidList(const void *a, const void *b);
int QDECL       CompareEntryInt(const void *a, const void *b);
int QDECL       CompareEntryIntReverse(const void *a, const void *b);
int QDECL       CompareEntryFloat(const void *a, const void *b);
int QDECL       CompareEntryFloatReverse(const void *a, const void *b);
int QDECL       CompareEntryStringSensitive(const void *a, const void *b);
int QDECL       CompareEntryStringInsensitive(const void *a, const void *b);
int QDECL       CompareStringEntryStringSensitive(const void *key, const void *entry);
int QDECL       CompareStringEntryStringInsensitive(const void *key, const void *entry);

// Math
float           interpolate(float start, float end, float weight);
int             first_set_bit(unsigned int bitmap);
float           pow_int(float base, int exp);
qboolean        rotate_vector_toward_vector(vec3_t source, float angle, vec3_t target, vec3_t dest);

// Memory management data structures

// Bookkeeping structure for a page of memory
typedef struct mem_page_s
{
	int             offset;		// The index of this page's 0 entry into the data block
	unsigned int    available;	// Bitmap of which entries are available to allocate
	struct mem_page_s *next;	// Link to next memory page from which to allocate
} mem_page_t;

#define MM_PAGE_SIZE (sizeof(unsigned int) * BITS_PER_BYTE)

// A dynamic memory manager
//
// NOTE: Yes, this programming environment was designed to never need
// malloc() or new().  It turns out there are some situations where
// you really do need to allocate a block of memory and track its use
// so you don't overwrite it, primarily with associative arrays (hash
// tables).  It's a bit of a shame this code even needs to exist, but
// so it does.  Technically this code doesn't handle memory allocation;
// all the data must be reserved up front.  It just manages the
// allocated memory.  Callers can ask for an unused record and quickly
// receive one.
typedef struct mem_manager_s
{
	void           *block;		// Block of data being managed
	int             width;		// Width in bytes of one data segment of the block
	int             num_data;	// Number of data segments in the block

	mem_page_t     *pages;		// Array of memory pages that manage data
	int             num_pages;	// Number of memory pages
	//
	// NOTE: This should equal ceil(size/sizeof(int))
	mem_page_t     *first;		// First memory page to check when allocating data
} mem_manager_t;

// Memory management functions
void            mm_setup(mem_manager_t * mm, void *block, int width, int num_data, mem_page_t * pages, int num_pages);
void            mm_reset(mem_manager_t * mm);
void           *mm_new(mem_manager_t * mm);
void            mm_delete(mem_manager_t * mm, void *data);

// Physics
float           trajectory_closest_origin_time(vec3_t pos, vec3_t vel);
float           trajectory_closest_origin_dist(vec3_t pos, vec3_t vel, float start_time, float end_time);

// Bounding boxes
void            nearest_bound_point(vec3_t loc, vec3_t mins, vec3_t maxs, vec3_t edge);
float           point_bound_distance(vec3_t loc, vec3_t mins, vec3_t maxs);
float           point_bound_distance_squared(vec3_t loc, vec3_t mins, vec3_t maxs);

// Ray-tracing
#define TRACE_HIT	0x0001
#define TRACE_ENTER	0x0002
#define TRACE_EXIT	0x0004
int             trace_box(vec3_t pos, vec3_t dir, vec3_t mins, vec3_t maxs, vec3_t enter, vec3_t exit);

// Map data structures
//
// NOTE: Ideally this code wouldn't be necessary.  If the code were
// written in a language like C++, it would be better to use the
// standard library implementation of maps, sets, and other tables.

// One entry in a map
//
// NOTE: The data pointer will be NULL when the entry is not in use.
typedef entry_void_void_t map_entry_t;

// A hashing function that can be applied to a key
//
// NOTE: The final result should be moduloed by the map's current capacity
// by whatever function decides to call one of these functions.
typedef int     map_hash_t(const void *);

// A map (associative array or "hash table") of data
typedef struct
{
	map_entry_t    *table;		// A table of the map's entries
	int             capacity;	// Maximum number of usable entries in the table
	int             size;		// Number of entries currently in use
	cmp_t          *compare;	// A function to determine if two keys are the same or not
	map_hash_t     *hash;		// A function to hash input keys to their desired storage index
} map_t;

// Map manipulation functions
void            map_initialize(map_t * map, map_entry_t * table, int capacity, cmp_t * compare, map_hash_t * hash);
void           *map_get(map_t * map, void *key);
int             map_set(map_t * map, void *key, void *value);
map_entry_t    *map_iter_refresh(map_t * map, map_entry_t * entry);
map_entry_t    *map_iter_first(map_t * map);
map_entry_t    *map_iter_next(map_t * map, map_entry_t * entry);


// Searching
//
// NOTE: bsearch() should be in bg_lib.h because it's a standard library function
int             bsearch_addr(const void *key, const void *list, size_t list_size, size_t entry_size, cmp_t * compare,
							 void **match);
#if defined ( Q3_VM )
void           *bsearch(const void *match, const void *list, size_t list_size, size_t entry_size, cmp_t * compare);
#endif
void           *bsearch_ins(const void *key, void *list, size_t * list_size, size_t max_list_size, size_t entry_size,
							cmp_t * compare, int *insert);

// This code defines a generalized interface for a sorted list of
// points-to-data (aka. entries) with the following constraints:
//  - Each entry has a non-negative timeout.  When the current time passes an entry's
//    timeout, the entry is removed from the list.
//  - Each entry has a score value, evaluated with the value() function pointer
//  - Entries are always added to the list with a specified timeout value.  If the
//    entry is already in the list, its timeout is updated.  Otherwise, the entry
//    is only added to the list if the list isn't full or this entry's score is
//    greater than the lowest scored entry currently in the array (which is removed).
//  - The entries in the array are sorted by data pointer for fast access.

// This is the timed valued list structure.
// NOTE: The data[] and timeout[] array pointers must be set by whoever
// creates this structure.  Ideally no dynamic memory allocation is used,
// but if it is, managing the memory is the caller's responsibility.
typedef struct timed_value_list_s
{
	size_t          max_size;	// Maximum number of list entry
	size_t          size;		// Current number of list entry
	size_t          entry_size;	// Size in bytes of one list entry

	void           *data;		// Pointer to array (of size max_size) of entry
	float          *timeout;	// Time at which data[] value with associated index should be deleted
	float          *value;		// Value of each data[] value with associated index
	cmp_t          *compare;	// Comparator for pointers to data[] entries

	float           min_value;	// Lowest score value among all data[] members
	int             min_value_index;	// Index of data[] member with value min_value

	float           min_timeout;	// First time at which a data[] entry will expire
} timed_value_list_t;
typedef timed_value_list_t tvl_t;

// Some timed value list functions allow entry handler functions which are
// called at appropriate times (such as during insertion, or right before deletion).
// All such handler functions have this syntax.
//
// The first argument of the handler function is a pointer to the tvl, and the
// second argument is the current index of the entry (although that index might not
// be correct after the code does further processing-- for example, if the entry
// will be deleted).  The third argument is an optional pointer to other data the
// handler might need.  This essentially simulates anonymous subroutines (aka.
// lambda functions) which the C syntax doesn't support for some unknown reason.
//
// Whenever a function includes a handler and argument, they are always optional.
// Set the handler to 0 and the argument to NULL if no handlers should be used.
typedef void    tvl_entry_handler_t(tvl_t * tvl, int index, void *arg);
typedef int     tvl_entry_test_t(tvl_t * tvl, int index, void *arg);

// Setup
void            tvl_reset(tvl_t * tvl);
void            tvl_setup(tvl_t * tvl, size_t max_size, size_t entry_size,
						  void *data, float *timeout, float *value, cmp_t compare);

// Searching
void           *tvl_data(tvl_t * tvl, int index);
void           *tvl_highest_value(tvl_t * tvl, void *prefer);
void           *tvl_search(tvl_t * tvl, void *entry);
int             tvl_data_index(tvl_t * tvl, void *entry);
float           tvl_data_timeout(tvl_t * tvl, void *entry);

// Updating
void            tvl_update_mins(tvl_t * tvl);
int             tvl_update_time(tvl_t * tvl, float time, tvl_entry_handler_t delete_handler, void *arg);
int             tvl_update_test(tvl_t * tvl, tvl_entry_test_t test, tvl_entry_handler_t delete_handler, void *arg);
int             tvl_update_entry(tvl_t * tvl, int index, float timeout, float value);

// Insertion
int             tvl_add(tvl_t * tvl, void *entry, float timeout, float value,
						tvl_entry_handler_t insert_handler, tvl_entry_handler_t delete_handler, void *arg);


// This code defines an interface for octrees.  Octrees are the three dimensional
// analogy of binary trees (which are one dimensional).  It's a way of sorting
// three dimensional data for fast access (both nearest neighbor and point
// presence/absense verification).  In theory, Voronoi diagrams would be faster
// for the nearest neighbor case.  However, they are extremely painful to maintain
// and don't actually give speed gains until the data structures become very large
// (since the amount of data stored in a Voronoi node is much larger than an
// octree node).
//
// Each octree is made up of nodes.  Each node contains a point, a pointer to the
// data associated with that point, and a pointer to the root child-node in each
// of the 8 sectors this node divides 3-space into.
//
// Each sector has an identifying number between 0 and 7, computed the following
// way.  The first bit refers to the X axis, the second bit for the Y axis, and
// the third bit for the Z axis.  A sector's axis bit is 0 if that region contains
// points less than or equal to the dividing point's value and 1 if the sector has
// points greater than the dividing point's value.
//
// For example, suppose the node's dividing point is (0, 0, 0).  Then the point
// (1, 5, 9) would be in sector 7, where:
//   7 = ( ((1>0) << 0) | ((5>0) << 1) | ((9>0) << 2) )
// On the other hand, the point (-1, 1, -1) would be in sector 2.

// Determines which octree sector that base divides the input target resides in.
// NOTE: Base and target should have type vec3_t.
#define octree_sector(base, target) ( ( ((target)[0]>(base)[0]) << 0 ) | \
									  ( ((target)[1]>(base)[1]) << 1 ) | \
									  ( ((target)[2]>(base)[2]) << 2 ) )

// Bit codings for the different sector axies
#define OT_0	0				// No axies set
#define OT_X	1				// (1 << 0)
#define OT_Y	2				// (1 << 1)
#define OT_Z	4				// (1 << 2)
#define OT_XY	(OT_X|OT_Y)
#define OT_XZ	(OT_X|OT_Z)
#define OT_YZ	(OT_Y|OT_Z)
#define OT_XYZ	(OT_X|OT_Y|OT_Z)

// Octree node definition
typedef struct octree_node_s
{
	void           *data;		// The data of this node
	struct octree_node_s *sector[8];	// Root subnodes in each sector this node divides, or NULL
} octree_node_t;

// Octree creation
octree_node_t  *octree_assemble(octree_node_t * nodes, int num_nodes, const float *(data_origin) (const void *));

// Nearest neighbor
void           *octree_neighbor(vec3_t point, octree_node_t * root, const float *(data_location) (const void *));

// Print out the contents of the tree
void            octree_print(octree_node_t * root, const char *(data_name) (const void *));


// This code manages ordered index subset iterators.  Each iterator has a subset
// maximum size (max number of different indicies in the subset) and index range
// (number of different indicies available).  For example, a index subset iterator
// with max size 2 and range 4 would iterate over the following sets:
//
//   (0)
//   (0, 1)
//   (0, 2)
//   (0, 3)
//   (1)
//   (1, 0)
//   (1, 2)
//   (1, 3)
//   (2)
//   (2, 0)
//   (2, 1)
//   (2, 3)
//   (3)
//   (3, 0)
//   (3, 1)
//   (3, 2)
//
// Subsets are ordered, so (0, 1) differs from (1, 0).  But each index must be
// unique, so (1, 1) is not a legal subset.  The iterator will always iterate
// in lexicagraphical order.
//
// NOTE: The static boundaries are pretty lax.  You can quite easily create an
// iterator that will iterate for a God awful long time.  I hope you know what
// you are doing.

// Subsets can contain at most 64 different indicies
#define ISI_SIZE_MAX 64

// Indicies can range from 0 up to at most 1023
#define ISI_RANGE_MAX 1024

// Allocate this many integers to track the bitmap of which indicies are currently
// present in the iterator's current state.
#define ISI_EXIST_BITMAP_LENGTH ( ((ISI_RANGE_MAX) + sizeof(int)-1) / sizeof(int) )

// The index subset iterator
typedef struct index_subset_iter_s
{
	int             max_size;	// Iterate over subsets of up to this many indicies
	int             range;		// Each index must be less than this value

	int             index[ISI_SIZE_MAX];	// The current subset of indicies
	int             size;		// The size of the current subset
	int             exists[ISI_EXIST_BITMAP_LENGTH];	// Bitmap of which indicies exist in index[]
	int             valid;		// True if index[] contains valid indicies and false if not
} index_subset_iter_t;

// Produce the starting (null) iterated subset.
void            isi_start(index_subset_iter_t * isi);

// Iterate to the next subset.  Returns true if this exists and false if not.
int             isi_next(index_subset_iter_t * isi);

// Skip over all subsets starting with this iterator's current state,
// Returns true if this exists and false if not.
int             isi_skip(index_subset_iter_t * isi);

// Create a description string of the iterator's current state
char           *isi_string(index_subset_iter_t * isi, char *string, int max_length);
