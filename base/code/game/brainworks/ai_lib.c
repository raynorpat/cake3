// Some portions Copyright (C) 1999-2000 Id Software, Inc.
// All other portions Copyright (C) 2002-2007 Ted Vessenes

/*****************************************************************************
 * ai_lib.h
 *
 * Implementations of some common library-style functions used in the AI code
 *****************************************************************************/

#include "../g_local.h"
#include "ai_lib.h"

// FIXME: This entire file is better placed in something like q_math.c.  I left this
// stuff in the ai section because Brainworks code should not edit any file outside
// of ai_*.[ch] (and maybe g_bot.c if necessary).  However, this is a good thing for
// someone at id software to move should this code ever get integrated into the main
// code base.  The interfaces could solve many other problems as well.


// NOTE: In each entry structure, "key" is the first element (structure offset 0).
// Because of this, it's possible to compare between packaged and unpackaged
// versions of the data.  For example, consider this code:
//
// int a = 1;
// entry_int_t b = { 2 };
//
// if (CompareEntryInt(&a, &b) == ) { ... }
//
// The compiler turns this:
//   ((entry_int_t *) pa)->key  (interpretted as an integer)
// Into this:
//   *(pa+0)  (interpretted as an integer)
// Which is the original value of a.


/*
===========
CompareVoid

Compares two uncasted pointers.
===========
*/
int QDECL CompareVoid(const void *a, const void *b)
{
	// NOTE: (void *) pointers cannot be subtracted because
	// their referenced data has unknown length, so these
	// pointers are casted to (char *) pointers which have
	// known data length.
	return (const char *)a - (const char *)b;
}

/*
===============
CompareVoidList

Compares two entries in a list of pointers for qsort()
and bsearch() such that lower addressed pointers occur
before higher addressed pointers in the list.

NOTE: Because the pointers have type (void *) (or are
at least castable to this), this list has type (void **).
qsort() and bsearch() pass in array pointers, not raw
data values, so the input "a" and "b" values actually
have base type (void **), NOT (void *).  They must be
dereferenced to access their pointer value contents.
===============
*/
int QDECL CompareVoidList(const void *a, const void *b)
{
	const void     *va = *(const void **)a;
	const void     *vb = *(const void **)b;

	return CompareVoid(va, vb);
}

/*
===============
CompareEntryInt

Compares two integer key list entries for quick sort such
that the list will be sorted from lowest to highest.
===============
*/
int QDECL CompareEntryInt(const void *a, const void *b)
{
	const entry_int_t *pa = (const entry_int_t *)a;
	const entry_int_t *pb = (const entry_int_t *)b;

	return (pa->key - pb->key);
}

/*
======================
CompareEntryIntReverse

Compares two integer key list entries for quick sort such
that the list will be sorted from highest to lowest.
======================
*/
int QDECL CompareEntryIntReverse(const void *a, const void *b)
{
	const entry_int_t *pa = (const entry_int_t *)a;
	const entry_int_t *pb = (const entry_int_t *)b;

	return (pb->key - pa->key);
}

/*
=================
CompareEntryFloat

Compares two float key list entries for quick sort such
that the list will be sorted from lowest to highest.
=================
*/
int QDECL CompareEntryFloat(const void *a, const void *b)
{
	float           diff;

	const entry_float_t *pa = (const entry_float_t *)a;
	const entry_float_t *pb = (const entry_float_t *)b;

	diff = (pa->key - pb->key);
	if(diff > 0)
		return 1;
	else if(diff < 0)
		return -1;
	else
		return 0;
}

/*
========================
CompareEntryFloatReverse

Compares two float key list entries for quick sort such
that the list will be sorted from highest to lowest.
========================
*/
int QDECL CompareEntryFloatReverse(const void *a, const void *b)
{
	float           diff;

	const entry_float_t *pa = (const entry_float_t *)a;
	const entry_float_t *pb = (const entry_float_t *)b;

	diff = (pa->key - pb->key);
	if(diff > 0)
		return -1;
	else if(diff < 0)
		return 1;
	else
		return 0;
}

/*
===========================
CompareEntryStringSensitive

Case sensitive compares two string key list entries for quick
sort such that the list will be sorted in alphabetical ascending order.
===========================
*/
int QDECL CompareEntryStringSensitive(const void *a, const void *b)
{
	const entry_string_t *pa = (const entry_string_t *)a;
	const entry_string_t *pb = (const entry_string_t *)b;

	return strcmp(pa->key, pb->key);
}

/*
=============================
CompareEntryStringInsensitive

Case insensitive compares two string key list entries for quick
sort such that the list will be sorted in alphabetical ascending order.
=============================
*/
int QDECL CompareEntryStringInsensitive(const void *a, const void *b)
{
	const entry_string_t *pa = (const entry_string_t *)a;
	const entry_string_t *pb = (const entry_string_t *)b;

	return Q_stricmp(pa->key, pb->key);
}

/*
=================================
CompareStringEntryStringSensitive

Case sensitive compares an input string (type char *) to a list
entry whose key is a string pointer (type entry_string_t *) for
bsearch().
=================================
*/
int QDECL CompareStringEntryStringSensitive(const void *key, const void *entry)
{
	const char     *key_string = (const char *)key;
	const entry_string_t *entry_string = (const entry_string_t *)entry;

	return strcmp(key_string, entry_string->key);
}

/*
===================================
CompareStringEntryStringInsensitive

Case insensitive compares an input string (type char *) to a list
entry whose key is a string pointer (type entry_string_t *) for
bsearch().
===================================
*/
int QDECL CompareStringEntryStringInsensitive(const void *key, const void *entry)
{
	const char     *key_string = (const char *)key;
	const entry_string_t *entry_string = (const entry_string_t *)entry;

	return Q_stricmp(key_string, entry_string->key);
}

/*
===========
interpolate

Given starting and ending values and a weighting between
them (0.0 means use all start, 1.0 means all end), returns
the linear interpolation between these two values.  If
weight is less than 0.0 or greater than 1.0, the function
will compute the linear extrapolation as well.

For example, if start is 10, end is 20, and weight is 0.3,
this function returns 13 = (10*.7) + (20*.3)
===========
*/
float interpolate(float start, float end, float weight)
{
	return start * (1.0 - weight) + end * weight;
}

/*
=============
first_set_bit

Returns the index of the first set bit in an integer bitmap.
For example, if the bitmap is 24 (0001 1000) it returns 3,
the index of the 8 bit.  If the bitmap is 25 (0001 1001) it
returns 0, the index of the 1 bit.  Returns -1 if all bits
are 0 (no bits are set).

Runtime is log(sizeof(int)).
=============
*/
int first_set_bit(unsigned int bitmap)
{
	int             index, size;
	unsigned int    mask;

	// Fail if no bits are set
	if(bitmap == 0x00000000)
		return -1;

	// The first set bit is at least this far into the bitmap
	index = 0;

	// Analyse this many bits of the bitmap
	size = MM_PAGE_SIZE;

	// This bitmask covers "size" bits of the bitmap.
	//
	// NOTE: You can think of this as 0x00000000 - 1 = 0xFFFFFFFF
	mask = -1;

	// Repeatedly search the the lower half of the bitmap for a
	// set bit.  If none is found, search the upper half.  Terminate
	// when there is only one bit left (which must be set).  This
	// is analogous to a binary search.
	//
	// NOTE: This algorithm assumes 2^n = MM_PAGE_SIZE =
	// sizeof(int) * BITS_PER_BYTE.  I cannot think of an architecture
	// for which this isn't true, but it's worth noting.  If for some
	// reason that's not the case, this algorithm must be slightly
	// modified to handle the "recurse to a half with odd size" case.
	while(size > 1)
	{
		// Restrict search to one half of the bitmap or the other
		//
		// NOTE: It's important the mask is an unsigned integer because
		// if it were signed, the >> operation would sign extend the
		// last bit.
		size >>= 1;
		mask >>= size;

		// Search the upper half if the lower half has no set bits ...
		if((bitmap & mask) == 0x0000)
		{
			index += size;
			bitmap >>= size;
		}
		// ... Otherwise continue searching the lower half (no updates needed)
	}

	// This is the index of the first set bit
	return index;
}

/*
=======
pow_int

Power of a floating point base to an integer power
=======
*/
float pow_int(register float base, register int exp)
{
	register float  result = 1.0;
	register int    invert;

	// Always return zero if the base is zero
	// NOTE: This isn't mathematically correct if the exponent is non-positive.
	// The function should technically generate an error.  In the interest of
	// avoiding exceptions, these checks are left to the caller.
	if(base == 0.0)
		return 0.0;

	// Remember if the result should be inverted due to negative exponents
	invert = (exp < 0.0);
	if(invert)
		exp = -exp;

	// Multiply the result by the appropriate (2^n)th powers of the base
	while(exp)
	{
		if(exp & 0x1)
			result *= base;
		base *= base;
		exp >>= 1;
	}

	// Invert the result if necessary and return it
	if(invert)
		result = 1.0 / result;
	return result;
}

/*
===========================
rotate_vector_toward_vector

Rotate the source vector at most angle degrees toward
the target vector and save the result in dest.  source
and target must be unit vectors.  It is permitted for
the dest vector pointer to equal the source vector pointer.

Returns true if the destination vector was successfully
rotated and false if this was not possible (because source
and target were colinear).  If it returns false, the source
vector will be copied into the destination vector.
===========================
*/
qboolean rotate_vector_toward_vector(vec3_t source, float angle, vec3_t target, vec3_t dest)
{
	float           cosine, sine;
	vec3_t          cross, normal;

	// Never rotate further than the target vector
	angle = DEG2RAD(angle);
	cosine = cos(angle);
	if(cosine <= DotProduct(source, target))
	{
		VectorCopy(target, dest);
		return qtrue;
	}

	// Compute a vector coplanar with source and target that is normal to source
	// (and on the "target" side of the half-plane).
	CrossProduct(source, target, cross);
	CrossProduct(cross, source, normal);

	// If source and target were colinear, the target must be the negative of
	// the source.  No well defined rotation exists, so don't rotate at all.
	if(VectorCompare(normal, vec3_origin))
	{
		VectorCopy(source, dest);
		return qfalse;
	}

	// Rotate the source vector angle degrees towards the coplanar normal
	sine = sin(angle);
	dest[0] = cosine * source[0] + sine * normal[0];
	dest[1] = cosine * source[1] + sine * normal[1];
	dest[2] = cosine * source[2] + sine * normal[2];
	return qtrue;
}

/*
========
mm_setup

Setup a memory manager with the inputted data block,
width of a data entry in the block, total number of
blocks, and inputted page structure to manage the
data block.  After setup completes, it will initialize
the structure with mm_reset().

NOTE: If num_pages * page->size < num_data (ie. not
enough pages were provided to manage the entire data
block), num_data will be appropriately reduced.  The
caller is responsible for supplying enough pages to
handle the data block.
========
*/
void mm_setup(mem_manager_t * mm, void *block, int width, int num_data, mem_page_t * pages, int num_pages)
{
	int             pages_needed;

	// Determine the number of pages required to manage the whole data block
	pages_needed = (num_data + MM_PAGE_SIZE - 1) / MM_PAGE_SIZE;

	// Reduce either the number of pages or the data block size if
	// non-matching numbers were supplied
	if(num_pages < pages_needed)
		num_data = num_pages * MM_PAGE_SIZE;
	else if(num_pages > pages_needed)
		num_pages = pages_needed;

	// Save the inputted values in the memory manager
	mm->block = block;
	mm->width = width;
	mm->num_data = num_data;
	mm->pages = pages;
	mm->num_pages = num_pages;

	// Reset the memory page structure to the state of no allocated data
	mm_reset(mm);
}

/*
========
mm_reset

Resets a memory manager that has already been
setup by mm_setup().

NOTE: The contents of the data block will not be
initialized to 0.
========
*/
void mm_reset(mem_manager_t * mm)
{
	int             i, size, entries;
	mem_page_t     *page, **link_to_page;

	// Each page has the same size
	size = MM_PAGE_SIZE;

	// The first link to page data is the head of the linked list of pages
	link_to_page = &mm->first;

	// Reset each page's data
	for(i = 0; i < mm->num_pages; i++)
	{
		// Configure this page to handle data at the next offset
		page = &mm->pages[i];
		page->offset = i * size;

		// Compute how many entries this page actually handles
		if(page->offset + size <= mm->num_data)
			entries = size;
		else if(page->offset <= mm->num_data)
			entries = mm->num_data - page->offset;
		else
			entries = 0;

		// If this page only handles some of the entries, mark those available
		//
		// NOTE: Suppose for example 3 entries were being handled.  This
		// operation computes 0000 1000 and then turns that into 0000 0111,
		// which essentially sets the first three bits on and leaves the
		// others off
		if(entries < size)
			page->available = (1 << entries) - 1;

		// Otherwise mark all entries available
		//
		// NOTE: This is really 0-1.  Technically the above code would produce
		// the same result from (1 << MM_PAGE_SIZE) - 1.  Mathematically the
		// code wants to compute (1 0000 ... 0000 - 1) = 1111 ... 1111 but
		// (1 << MM_PAGE_SIZE) actually equals 0.  The result is the same more
		// by coincidence than anything else.  Also, this code is faster and is
		// far more likely to execute, so it's a worthwhile speed optimization
		// as well.
		else
			page->available = -1;

		// Make the last link point to this page and schedule this
		// page's link to be updated next if any entries are available
		if(page->available)
		{
			*link_to_page = page;
			link_to_page = &page->next;
		}
	}

	// There are no more pages to process, so terminate the last link
	*link_to_page = NULL;
}

/*
=============
mm_data_index

Given a pointer from the manager's data block, returns that
data's index.  Returns -1 if the manager doesn't manage this
pointer.

NOTE: This does NOT mean data == mm->block[index]!  It means
data == mm->block[index * mm->width] (assuming the data pointer
was correctly aligned).  But you shouldn't be directly accessing
the data block anyway, and if you try to the compiler will bitch
at you for not casting the (void *) array to (char *).  The index
is primarily used for checking the data's allocation in the page
structure.
=============
*/
int mm_data_index(mem_manager_t * mm, void *data)
{
	int             index;

	// Compute the data's index into the main data block
	index = ((char *)data - (char *)mm->block) / mm->width;

	// Return the index if it's valid; fail otherwise
	if(index >= 0 && index < mm->num_data)
		return index;
	else
		return -1;
}

/*
===========
mm_page_get

Gets the page that handles the data with the given global index.
(The data's index into the page is data_index - page->offset.)
Returns NULL if the data index is not handled by any page (ie.
out of bounds pointer).
===========
*/
mem_page_t     *mm_page_get(mem_manager_t * mm, int index)
{
	int             page_index;

	// Fail if an invalid data index was given
	if(index < 0)
		return NULL;

	// Look up this page's index and make sure it's in bounds
	page_index = index / MM_PAGE_SIZE;
	if(page_index >= mm->num_pages)
		return NULL;

	// Return the associated page
	return &mm->pages[page_index];
}

/*
===========
mm_data_get

Get the pointer to the data record in the block
associated with the inputted data index.

NOTE: It is the caller's responsibility to guarantee
"index" is valid (ie. 0 <= index < mm->num_data).

NOTE: This will return the data pointer even if
the data block is not in use.
===========
*/
void           *mm_data_get(mem_manager_t * mm, int index)
{
	// Give the caller their data
	//
	// NOTE: The data block is cast into (char *) because the width
	// of a record is in units of characters, but (void *) has
	// unknown width.
	return ((char *)mm->block) + (mm->width * index);
}

/*
======
mm_new

Obtain a pointer to a new (previously unused)
data record in the data block.  The new record
will be tagged as in use so that it will never
be returned again by a future mm_new() call
until the data has been freed with mm_delete().
Returns NULL if there are no unallocated data
records to use.

NOTE: The retrieved data segment will NOT be
initialized.
======
*/
void           *mm_new(mem_manager_t * mm)
{
	int             entry;
	mem_page_t     *page;

	// Search through all pages that aren't full
	while(mm->first)
	{
		// Find the first unused entry in this page (if any)
		page = mm->first;
		entry = first_set_bit(page->available);
		if(entry >= 0)
		{
			// Tag the entry as unavailable
			page->available &= ~(1 << entry);

			// The caller can use this data
			return mm_data_get(mm, page->offset + entry);
		}

		// This page is full; remove it and check the next page
		mm->first = page->next;
		page->next = NULL;
	}

	// No available data records remain
	//
	// NOTE: Don't think this can't happen to you!  Check for this case!
	return NULL;
}

/*
=========
mm_delete

Notifies the memory manager that the inputted
data record is no longer in use and can be
supplied to a future caller of mm_new().
=========
*/
void mm_delete(mem_manager_t * mm, void *data)
{
	int             index;
	mem_page_t     *page;

	// Compute the data's index into the main block
	index = mm_data_index(mm, data);
	if(index < 0)
		return;

	// Find the page that handles this data record
	page = mm_page_get(mm, index);
	if(page == NULL)
		return;

	// Compute the record's index in the page
	index -= page->offset;

	// Mark that record as available
	page->available |= (1 << index);

	// Add the page to the list of pages to search for
	// allocation if it was previously removed
	if(!page->next)
	{
		page->next = mm->first;
		mm->first = page;
	}
}

/*
==============================
trajectory_closest_origin_time

Given an input trajectories (position and velocity
pair), returns the time at which this trajectory is
closest to the origin.  This time could be negative.

NOTE: To determine when two trajectories are closest
to each other, translate one trajectory to be with
reference to the other. In other words, call this
function with position (pos_a - pos_b) and velocity
(vel_a - vel_b).

NOTE: The minimum distance will be:
  VectorMA(pos, time, vel, result)
  VectorLength(result)
=============================
*/
float trajectory_closest_origin_time(vec3_t pos, vec3_t vel)
{
	// The derivation of this formula is pretty simple
	return -DotProduct(pos, vel) / DotProduct(vel, vel);
}

/*
==============================
trajectory_closest_origin_dist

Given a trajectory and time boundaries, returns the
closest the trajectory comes to the origin (bounded
by the time interval).
==============================
*/
float trajectory_closest_origin_dist(vec3_t pos, vec3_t vel, float start_time, float end_time)
{
	float           time;
	vec3_t          result;

	// Determine the closest time
	time = trajectory_closest_origin_time(pos, vel);
	if(time < start_time)
		time = start_time;
	else if(time > end_time)
		time = end_time;

	// Return the distance at that time
	VectorMA(pos, time, vel, result);
	return VectorLength(result);
}

/*
===================
nearest_bound_point

Computes the point on a bounding box exterior (offset to real
world locations) nearest to the input location and stores it
in "edge".

NOTE: This code was based on the code in G_RadiusDamage()
in g_combat.c.

FIXME: Move that code to a common library file to reduce
duplicated code.
===================
*/
void nearest_bound_point(vec3_t loc, vec3_t mins, vec3_t maxs, vec3_t edge)
{
	int             i;

	// Compute the offset vector between the bounding box and the input location
	for(i = 0; i < 3; i++)
	{
		if(loc[i] < mins[i])
			edge[i] = mins[i];
		else if(loc[i] > maxs[i])
			edge[i] = maxs[i];
		else
			edge[i] = loc[i];
	}
}

/*
====================
point_bound_distance

Returns the distance between the input location and the
inputted bounding box (offset to real world locations).
====================
*/
float point_bound_distance(vec3_t loc, vec3_t mins, vec3_t maxs)
{
	vec3_t          edge;

	// Compute the nearest point on the bounding box's edge
	nearest_bound_point(loc, mins, maxs, edge);

	// Return the distance from the location to this point
	return Distance(loc, edge);
}

/*
============================
point_bound_distance_squared

Returns the squared distance between the input location and
the inputted bounding box (offset to real world locations).
============================
*/
float point_bound_distance_squared(vec3_t loc, vec3_t mins, vec3_t maxs)
{
	vec3_t          edge;

	// Compute the nearest point on the bounding box's edge
	nearest_bound_point(loc, mins, maxs, edge);

	// Return the distance from the location to this point
	return DistanceSquared(loc, edge);
}

/*
=========
trace_box

Test if a ray starting at "pos" and heading in "dir" (not
necessarily unitized) intersects the axis-aligned bounding
box defined by "mins" and "maxs".

This function return a bit mask code describing the trace results:

  TRACE_HIT:   (0x0001)
    Ray contacts bounding box inside.

  TRACE_ENTER: (0x0002)
    Ray enters the bounding box.  If "enter" is non-NULL, its
    contents will be set to the enter intersection point.

  TRACE_EXIT:  (0x0004)
    Ray exits the bounding box.  If "exit" is non-NULL, its
    contents will be set to the exit intersection point.

So every ray that misses the bounding box will return 0.  Most
rays that intersect the box return TRACE_HIT|TRACE_ENTER|TRACE_EXIT.
But a ray starting inside the box will return TRACE_HIT|TRACE_EXIT.
Of course, a ray with direction (0,0,0) that starts inside the
box will only return TRACE_HIT.

NOTE: The TRACE_ENTER flag will always be returned when the ray
enters the bounding box, even when the "enter" input is NULL.
Similar comments apply for TRACE_EXIT and "exit".

NOTE: "mins" and "maxs" for this function refer to the bounding
box whose intersection is being tested, unlike trap_Trace().
The trap_Trace() function's "mins" and "maxs" inputs refer to
the bounding box on the trace ray.  This function doesn't officially
support rays of non-zero width.  But if you really need to trace
against an arbitrary bounding box with a non-zero width ray, you
can artificially increase the bounding box's dimensions by the ray's
width-- the intersection check and returned points will be correct.

FIXME: Perhaps writing a wrapper function that provides an
interface identical to trap_Trace() would be helpful.  Or perhaps
it would be overkill.
=========
*/
int trace_box(vec3_t pos, vec3_t dir, vec3_t mins, vec3_t maxs, vec3_t enter, vec3_t exit)
{
	int             i, result;
	float           axis_enter, axis_exit, max_enter, min_exit, swap_tmp;

	// This algorithm comprehends the bounding box as three pairs of
	// axis-aligned planes.  It tests when the ray crosses the first
	// axis-aligned plane (enters) and the second plane (exits).  If the ray
	// crosses all three entrance planes before crossing any of the three
	// exit planes, the ray contacts the bounding box.  Otherwise the ray
	// misses the box.  So this is equivalent to tracking the last possible
	// entrance point and first possible exit point.  If the last entrance
	// point occurs later in the ray than the first exit point, the ray misses
	// the box.
	//
	// For this reason, the entrance and exit points are stored as floating
	// point numbers-- a scalar of "dir" offset from "pos".  So entrance
	// point "X" refers to three-space location pos + dir*X.  The ray hits the
	// box if max_enter < min_exit and misses when min_exit < max_enter.
	//
	// Since rays are unidirectional, negative scalars refer to points behind
	// the ray. If an exit point is negative, then the entire bounding box is
	// behind the ray, so the ray does not contact the box.  When the entrance
	// point is negative but the exit point is positive, the ray starts inside
	// the bounding box.
	//
	// NOTE: This is the slab algorithm designed by Kay and Kayjia.


	// Since this algorithm only cares about positive ray scalars (the actual
	// negative value of an intersection behind the ray doesn't matter), -1
	// is used for the uninitialized enter and exit contact points
	max_enter = -1;
	min_exit = -1;

	// Find the maximum enter and the minimum exit intersections for
	// all dimensions
	for(i = 0; i < 3; i++)
	{
		// Rays parallel to the bounding planes never intersect the planes--
		// the ray is completely inside or completely outside the planar slab.
		if(dir[i] == 0)
		{
			// If the starting location is below the lower plane or above
			// the upper plane, the ray cannot intersect the box
			if(pos[i] < mins[i] || pos[i] > maxs[i])
				return 0;

			// The ray is always inside this axis' bounding planes, so the
			// ray could intersect the box.  But the ray neither enters nor
			// exits the box in this plane, so max_enter and min_exit should
			// not (and cannot) be updated.
			continue;
		}

		// Determine where the ray intersects the entrance and exit planes
		// for this axis
		//
		// FIXME: If this function needed to be faster, it could be rewritten
		// to use a floating point multiply instead of a divide, only requiring
		// one divide at the end.  But I think the code would look a lot more
		// confusing, so it should only be done if profiling says this function
		// is a bottleneck-- highly unlikely but possible.
		axis_enter = (mins[i] - pos[i]) / dir[i];
		axis_exit = (maxs[i] - pos[i]) / dir[i];

		// The enter intersection for this pair of planes must occur before
		// the exit, so swap them if necessary
		if(axis_exit < axis_enter)
		{
			swap_tmp = axis_exit;
			axis_exit = axis_enter;
			axis_enter = swap_tmp;
		}

		// The bounding box is behind the ray if the exit point is negative
		if(axis_exit < 0)
			return 0;

		// Update the maximum enter and minimum exit points
		if(max_enter < axis_enter)
			max_enter = axis_enter;
		if(min_exit > axis_exit || min_exit < 0)
			min_exit = axis_exit;

		// If the minimum exit point occurs before the maximum enter point,
		// the ray missed
		if(min_exit < max_enter)
			return 0;
	}

	// The ray intersects the bounding box
	result = TRACE_HIT;

	// Check if the ray enters the bounding box
	if(max_enter >= 0)
	{
		result |= TRACE_ENTER;
		if(enter)
			VectorMA(pos, max_enter, dir, enter);
	}

	// Check if the ray exits the bounding box
	if(min_exit >= 0)
	{
		result |= TRACE_EXIT;
		if(exit)
			VectorMA(pos, min_exit, dir, exit);
	}

	// Give the caller a description of how the ray intersects the bounding box
	return result;
}

/*
==============
map_initialize

Initialize a map using the inputted data block and maximum capacity.
The map will determine if keys are equal using the inputted comparator
(returns 0 for equal and non-zero for non-equal). The ideal data index
for a given key is determined by the inputted hashing function.

The table contents will be completed cleared during initialization
as well.

NOTE: If this were C++, this would be a constructor.  Actually if
this were C++, this code wouldn't exist because maps have already
been written in the libraries, but that's another story.

NOTE: Because this C environment does not support dynamic memory
allocation, this implementation of a map does not have any
functionality to grow when the table reaches a certain capacity.
As a result, you probably want the maximum capacity to be double
the expected maximum number of entries in the map, to ensure
relative sparsity (and thus retain fast lookups).
==============
*/
void map_initialize(map_t * map, map_entry_t * table, int capacity, cmp_t * compare, map_hash_t * hash)
{
	// Save the configuration varaibles
	map->table = table;
	map->capacity = capacity;
	map->compare = compare;
	map->hash = hash;

	// Reset the table
	map->size = 0;
	memset(map->table, 0, sizeof(map_entry_t) * map->capacity);
}

/*
========
map_hash

Hash an inputted key into its preferred storage index.
========
*/
int map_hash(map_t * map, void *key)
{
	return map->hash(key) % map->capacity;
}

/*
=======
map_get

Get the data value associated with the inputted key.
Returns NULL if key was not found in the table.
=======
*/
void           *map_get(map_t * map, void *key)
{
	int             preferred, index;
	map_entry_t    *entry;

	// Determine where this data wants to reside
	preferred = map_hash(map, key);

	// Iterate through the table looking for a match
	index = preferred;
	do
	{
		// The key was not found if this entry is empty
		entry = &map->table[index];
		if(!entry->value)
			return NULL;

		// Pass on the correct data if the this entry's key matches the request
		if(map->compare(key, entry->key) == 0)
			return entry->value;

		// Check the cyclically next entry
		if(++index >= map->capacity)
			index = 0;

		// Continue searching if the next index hasn't been searched yet
	} while(index != preferred);

	// The table is full and the requested key was not found
	return NULL;
}

/*
=======
map_set

Sets the inputted key to have the inputted data value.
If "value" is NULL, the entry will be removed from the
table instead.  Returns a non-zero value if the entry
was successfully set and 0 if an error occurred.  (This
probably means the table is already at capacity so a
new entry cannot be added.)
=======
*/
int map_set(map_t * map, void *key, void *value)
{
	int             preferred, index, deleted;
	int             dlp, pli, ild;
	map_entry_t    *entry;

	// Determine where this data wants to reside
	preferred = map_hash(map, key);

	// Search the table for the data's actual location, skipping
	// used entries with keys that differ from the input
	index = preferred;
	entry = &map->table[index];
	while(entry->value && map->compare(key, entry->key) != 0)
	{
		// Check the cyclically next entry
		if(++index >= map->capacity)
			index = 0;
		entry = &map->table[index];

		// Terminate if the entry was not found, failing if the caller
		// wanted to add data
		if(index == preferred)
			return (value == NULL);
	}

	// Extra work must be done when inserting new data
	if(!entry->value)
	{
		// It's easy to remove a key that doesn't exist
		if(!value)
			return 1;

		// Save this entry's key
		entry->key = key;
		map->size++;
	}

	// Updating a data value is simple enough
	if(value)
	{
		entry->value = value;
		return 1;
	}

	// Delete this entry the caller requested
	entry->key = NULL;
	entry->value = NULL;
	deleted = index;

	// Look for the next entry that could be moved to location "deleted",
	// stopping when an empty record is found
	do
	{
		// Check the next entry in the sequence
		if(++index >= map->capacity)
			index = 0;
		entry = &map->table[index];

		// Stop searching when an empty record is found
		if(!entry->value)
			break;

		// Look up this record's ideal storage location
		preferred = map_hash(map, entry->key);

		// Determine how the deleted entry's holelocation, this record's
		// prefered location, and the current record index are laid out.
		dlp = (deleted < preferred);
		pli = (preferred <= index);	// Yes, this <= is correct.  Think about it.
		ild = (index < deleted);

		// Look for another record if this record's preferred location
		// occurs after the last deleted record.  (In this case the
		// record cannot be safely moved to the hole created by the
		// deleted record.)
		//
		// NOTE: This check is a bit complicated due to the cyclic
		// nature of the storage array.
		if((dlp && pli) || (pli && ild) || (ild && dlp))
			continue;

		// Move this record to the deleted record's hole location
		map->table[deleted].key = entry->key;
		map->table[deleted].value = entry->value;

		// Delete this record
		entry->key = NULL;
		entry->value = NULL;
		deleted = index;

		// Continue the search for a record to move into the new deletion hole
	} while(1);

	// The data was successfully deleted
	map->size--;
	return 0;
}

/*
================
map_iter_refresh

This function confirms that the inputted entry of
the given map contains actual table data.  If it
doesn't, it checks the next table entry and so on
until it finds one.  It returns the next entry in
the table that contains data (ideally "entry"),
or NULL if the table contains no more data.  Please
don't directly modify the entry's key or value
pointers.  Use map_set() to change stuff.

In practice an outside caller would use this function
when iterating through the map and deleting entries.
If an entry isn't deleted, a call to map_iter_next()
returns the next entry to process.  But if the entry
is deleted, a call to map_iter_refresh() on the current
entry must be used because a later entry in the table
might get copied over the deleted entry location.
(Check the code for map_set() for algorithm details.)

NOTE: Deleting entries from a map during iteration is
dangerous business because deletion can reorder the
map.  It can even cause double processing.  For example,
consider the following table of 4 elements in a hash
table with capacity 6:

AB--CD

Suppose entry B and D both want to be located where C
is right now.  And suppose code iterated through the
entries trying to decide what to delete, and that C is
the only entry that will be deleted this pass.  The
code will process and skip A and B.  When it gets to C
it will delete it and the map_set() function will move
entry B to C's location, so the table will now be:

A---DB

If the code blindly skips to the next entry using
map_iter_next() instead of map_iter_refresh(), it will
forget to process entry D.  And in any case, entry
B will be checked twice.

This is not to say you should never write code like this.
Just make sure you know what you are doing.  This is a
tool; use it for good and not evil.
================
*/
map_entry_t    *map_iter_refresh(map_t * map, map_entry_t * entry)
{
	map_entry_t    *final, *filled;

	// Don't search beyond the final data slot of the table
	final = &map->table[map->capacity - 1];

	// Fail if the inputted entry is out of bounds
	if(entry < map->table || entry > final)
		return NULL;

	// Search for a a filled entry
	for(filled = entry; filled <= final; filled++)
	{
		// Inform the caller if another entry with data was found
		if(filled->value)
			return filled;
	}

	// There are no more valid entries in the table
	return NULL;
}

/*
==============
map_iter_first

Obtain the first entry in the table for iteration purposes.
Returns NULL if the table has no entries.  See map_iter_refresh()
for more information.
==============
*/
map_entry_t    *map_iter_first(map_t * map)
{
	// Search for the first table entry
	return map_iter_refresh(map, &map->table[0]);
}

/*
=============
map_iter_next

Given a table entry, iterate to the next table entry and
return it.  Returns NULL if the table has no more entries.
See map_iter_refresh() for more information.
=============
*/
map_entry_t    *map_iter_next(map_t * map, map_entry_t * entry)
{
	// Get the next valid entry after this one
	return map_iter_refresh(map, entry + 1);
}

/*
============
bsearch_addr - Binary Search with convergence address for failed searches

Does a binary search for a requested element.  Returns true if the
element was found and false if not.

If match is non-NULL, its contents is set to:
- A pointer to the matched element's location if key was found in the array
- A pointer to the key's sorted insert location if the key was not found
============
*/
int bsearch_addr(register const void *key, const void *list,
				 size_t list_size, register size_t entry_size, register cmp_t * compare, register void **match)
{
	register const char *start_entry;
	register const void *mid_entry;
	register size_t end_offset;
	register int    comp_result;

	// Iterate until there are no elements between the high and low list endpoints
	start_entry = list;
	end_offset = list_size;
	while(end_offset)
	{
		// Lookup the midpoint offset and entry pointer
		mid_entry = start_entry + ((end_offset >> 1) * entry_size);

		// Check for a match between the middle entry and the requested key
		comp_result = (*compare) (key, mid_entry);
		if(comp_result == 0)
		{
			if(match)
				*match = (void *)mid_entry;
			return 1;
		}

		// If searching the upper half, increase the starting pointer
		// and account for a possibly smaller upper half-interval.
		if(comp_result > 0)
		{
			start_entry = (char *)mid_entry + entry_size;
			end_offset--;
		}
		end_offset >>= 1;
	}

	// Key was not found in list, but if it were, it
	// would be at the address the pointers converged to.
	if(match)
		*match = (void *)start_entry;
	return 0;
}

#if defined ( Q3_VM )
/*
=======
bsearch - Binary Search

NOTE: This should be in bg_lib.c because it's a standard library function.

NOTE: This function should already be defined for native builds,
and that version will certainly run faster than this qvm version.
=======
*/
void           *bsearch(const void *key, const void *list, size_t list_size, size_t entry_size, cmp_t * compare)
{
	void           *match;

	// Return the matching pointer if it was found, or else just return NULL
	if(bsearch_addr(key, list, list_size, entry_size, compare, &match))
		return match;
	else
		return NULL;
}
#endif

/*
===========
bsearch_ins - Binary search that clears space for an entry to be
added if it the key wasn't found.

Returns a pointer to the element's location in the list if the
key was found, and a pointer to the location the element should
be added if the key wasn't found.  If the key wasn't found in
the list and the list is already at maximum size, NULL is returned.

The contents of the "insert" integer point is set to 1 if the
returned pointer refers to an insert location.  "list_size" will
also be incremented in this case.  "insert" is set to 0 if the
pointer is not an insert location (either the element was already
in the list or a NULL pointer was returned).
===========
*/
void           *bsearch_ins(const void *key, void *list,
							size_t * list_size, size_t max_list_size, size_t entry_size, cmp_t * compare, int *insert)
{
	size_t          list_bytes, match_offset;
	void           *match;

	// If the key is in the list already, return its address
	if(bsearch_addr(key, list, *list_size, entry_size, compare, &match))
	{
		*insert = 0;
		return match;
	}

	// Return NULL if the list can't grow any more
	if(max_list_size <= *list_size)
	{
		*insert = 0;
		return NULL;
	}

	// Determine (in bytes) the old size of the list and the matched address' offset
	list_bytes = entry_size * (*list_size)++;
	match_offset = (char *)match - (char *)list;

	// If not inserting at the end, shift the list contents after the match point by one entry
	// NOTE: When adding to the end of the array, match_offset == list_bytes, so 0 bytes are moved
	memmove((char *)match + entry_size, match, list_bytes - match_offset);
	*insert = 1;
	return match;
}

/*
=========
tvl_reset

Reset the contents of a setup timed value list
=========
*/
void tvl_reset(tvl_t * tvl)
{
	// Initialize other values
	tvl->size = 0;
	tvl->min_value = 0;
	tvl->min_value_index = -1;
	tvl->min_timeout = -1;
}

/*
=========
tvl_setup

Setup a timed value list using the input arrays for
data entries, timeouts, and entry values.  The data
entries each have width "entry_size".  A comparator
for data entries is required for sorting and searching.

NOTE: This implies that search keys must have the same
data type as data entries.
=========
*/
void tvl_setup(tvl_t * tvl, size_t max_size, size_t entry_size, void *data, float *timeout, float *value, cmp_t compare)
{
	// Setup the static arrays, sizes, and comparator
	tvl->max_size = max_size;
	tvl->entry_size = entry_size;
	tvl->data = data;
	tvl->timeout = timeout;
	tvl->value = value;
	tvl->compare = compare;

	// Reset the list to its initial state (empty)
	tvl_reset(tvl);
}

/*
========
tvl_data

Returns a pointer offset from the tvl's data array
which points to the requested element.
========
*/
void           *tvl_data(tvl_t * tvl, int index)
{
	return (char *)tvl->data + index * tvl->entry_size;
}

/*
=================
tvl_highest_value

Search the list for the entry with highest value and
returns a pointer to that entry.  In the case of ties,
entries matching the "prefer" input are prefered.
Returns NULL if no entries were found in the list.
=================
*/
void           *tvl_highest_value(tvl_t * tvl, void *prefer)
{
	int             i, offset;
	void           *entry, *best_entry;
	float           highest_value;

	// Search for the highest value
	best_entry = NULL;
	highest_value = -1;
	for(i = 0; i < tvl->size; i++)
	{
		// This is the new highest value if one of these is true:
		// - No previous high exists
		// - Its value is strictly higher than the previous high
		// - Its value is equal to the previous high, but it matches the preference
		entry = tvl_data(tvl, i);
		if(!(best_entry) || (tvl->value[i] > highest_value) || (tvl->value[i] == highest_value && !tvl->compare(prefer, entry)))
		{
			best_entry = entry;
			highest_value = tvl->value[i];
		}
	}

	return best_entry;
}

/*
==========
tvl_search

Search the timed value list for the existance of an inputted
data pointer.  Returns a pointer to the data[] entry if found
and NULL if not.
==========
*/
void           *tvl_search(tvl_t * tvl, void *entry)
{
	return bsearch(entry, tvl->data, tvl->size, tvl->entry_size, tvl->compare);
}

/*
==============
tvl_data_index

Search the timed value list for the existance of an inputted
data pointer.  Returns the data's entry in the timed value
list if found (between 0 and tvl->size-1) and -1 if not found.
==============
*/
int tvl_data_index(tvl_t * tvl, void *entry)
{
	void           *data_offset;

	// Search for the pointer offset into tvl->data that matches the input data pointers
	data_offset = tvl_search(tvl, entry);
	if(!data_offset)
		return -1;
	return ((char *)data_offset - (char *)tvl->data) / tvl->entry_size;
}

/*
================
tvl_data_timeout

Search the timed value list for the existance of an inputted
data pointer.  Returns the data's timeout if the data was
found and -1 if the data was not found.
================
*/
float tvl_data_timeout(tvl_t * tvl, void *entry)
{
	int             index;

	// Look up the index associated with the input pointer and return its timeout
	index = tvl_data_index(tvl, entry);
	if(index < 0)
		return -1;
	return tvl->timeout[index];
}

/*
===============
tvl_update_mins

Recompute the minimum value entry and timeout time.

NOTE: This function only needs to be called after
removing entries from the list.  When only adding to
the list, it's trivial to update these manually.
===============
*/
void tvl_update_mins(tvl_t * tvl)
{
	int             i;

	// Reset the initial values and timeouts
	tvl->min_value = 0;
	tvl->min_value_index = -1;
	tvl->min_timeout = -1;

	// Search for the entries with the actual minimums of these values
	for(i = 0; i < tvl->size; i++)
	{
		// Check for a new minimum value
		if(tvl->value[i] < tvl->min_value || tvl->min_value_index < 0)
		{
			tvl->min_value = tvl->value[i];
			tvl->min_value_index = i;
		}

		// Check for a new minimum timeout
		if(tvl->timeout[i] < tvl->min_timeout || tvl->min_timeout < 0)
			tvl->min_timeout = tvl->timeout[i];
	}
}

/*
===============
tvl_update_time

Updated the timed value list for a time increment,
removing expired data values.  Returns the number
of entries that expired.

NOTE: The "delete_handler" is called on every
entry that times out from the list.  See the
comments by the tvl_entry_handler_t typedef in
ai_lib.h for more information.
===============
*/
int tvl_update_time(tvl_t * tvl, float time, tvl_entry_handler_t delete_handler, void *arg)
{
	int             i, deleted;

	// Only update if there are expired values to remove
	if(time <= tvl->min_timeout)
		return 0;

	// Search the list for expired data values
	deleted = 0;
	for(i = 0; i < tvl->size; i++)
	{
		// Check for expired entries
		if(tvl->timeout[i] < time)
		{
			// Call the entry destruction handler if requested
			if(delete_handler)
				delete_handler(tvl, i, arg);

			// Increment the counter so other entries will be copied over this one
			deleted++;
			continue;
		}

		// Copy this entry to its new array location if necessary
		if(deleted)
		{
			memcpy(tvl_data(tvl, i - deleted), tvl_data(tvl, i), tvl->entry_size);
			tvl->timeout[i - deleted] = tvl->timeout[i];
			tvl->value[i - deleted] = tvl->value[i];
		}
	}

	// Shorten the list size and recompute list minimums if anything was deleted
	if(deleted)
	{
		tvl->size -= deleted;
		tvl_update_mins(tvl);
	}

	return deleted;
}

/*
===============
tvl_update_test

Updated the timed value list applying a test
function to each entry.  Keeps every entry that
passes the test and deletes every entry that fails
it (where pass is non-zero and fail is zero).
Returns the number of entries that expired.

NOTE: The same argument is passed both to the
test and the delete_handler.  Write your tests
and handlers accordingly.
===============
*/
int tvl_update_test(tvl_t * tvl, tvl_entry_test_t test, tvl_entry_handler_t delete_handler, void *arg)
{
	int             i, deleted;

	// Search the list for data values that fail the test
	deleted = 0;
	for(i = 0; i < tvl->size; i++)
	{
		// Check for expired entries
		if(!test(tvl, i, arg))
		{
			// Call the entry destruction handler if requested
			if(delete_handler)
				delete_handler(tvl, i, arg);

			// Increment the counter so other entries will be copied over this one
			deleted++;
			continue;
		}

		// Copy this entry to its new array location if necessary
		if(deleted)
		{
			memcpy(tvl_data(tvl, i - deleted), tvl_data(tvl, i), tvl->entry_size);
			tvl->timeout[i - deleted] = tvl->timeout[i];
			tvl->value[i - deleted] = tvl->value[i];
		}
	}

	// Shorten the list size and recompute list minimums if anything was deleted
	if(deleted)
	{
		tvl->size -= deleted;
		tvl_update_mins(tvl);
	}

	return deleted;
}

/*
================
tvl_update_entry

Update the timestamp of the data with the requested index
(presumably acquired from a tvl_data_index() call).  Returns
1 if the data was found and updated and 0 if not.

NOTE: Timeout values should be non-negative so they can
be distinguished from failed tvl_search_timeout() calls.
================
*/
int tvl_update_entry(tvl_t * tvl, int index, float timeout, float value)
{
	int             update;
	float           old_timeout, old_value;

	// Make sure the index refers to a valid entry
	if(index < 0 || index >= tvl->size)
		return 0;

	// Update the timeout and value
	old_timeout = tvl->timeout[index];
	tvl->timeout[index] = timeout;
	old_value = tvl->value[index];
	tvl->value[index] = value;

	// Determine if a full update is necessary to compute the minimum timeout
	update = 0;
	if(timeout < tvl->min_timeout)
		tvl->min_timeout = timeout;
	else if(tvl->timeout[index] == old_timeout)
		update = 1;

	// Check if an update is needed for the minimum value entry
	if(!update)
	{
		if(value < tvl->min_value)
		{
			tvl->min_value = value;
			tvl->min_value_index = index;
		}
		else if(tvl->value[index] == old_value)
		{
			update = 1;
		}
	}

	// Update everything if necessary
	if(update)
		tvl_update_mins(tvl);

	return 1;
}

/*
=======
tvl_add

Attempt to add a new entry to the list.  If the entry was
already in the list, the entry's timeout is updated.  Returns
the entry's index in the list, or -1 if not found and not added.

NOTE: Timeout values should be non-negative so they can
be distinguished from failed tvl_search_timeout() calls.

NOTE: The "insert_handler" is called on newly created entries
(as opposed to entries that are merely updated) and the
"delete_handler" is called on any entry which is displaced out
of the list.  Both handlers must use the same input argument.
See the comments by the tvl_entry_handler_t typedef in ai_lib.h
for more information.
=======
*/
int tvl_add(tvl_t * tvl, void *entry, float timeout, float value,
			tvl_entry_handler_t insert_handler, tvl_entry_handler_t delete_handler, void *arg)
{
	int             found, deleted, index, source, dest, length;
	void           *match;

	// Never add to degenerate lists
	if(tvl->max_size <= 0)
		return -1;

	// Ignore this entry if there's no space and it's less valuable than all other entries
	if((tvl->size >= tvl->max_size) && (value < tvl->min_value))
		return -1;

	// Find the address where the entry belongs in the array
	found = bsearch_addr(entry, tvl->data, tvl->size, tvl->entry_size, tvl->compare, &match);
	index = ((char *)match - (char *)tvl->data) / tvl->entry_size;

	// If the entry was found, just update its timestamp and value
	if(found)
	{
		tvl_update_entry(tvl, index, timeout, value);
		return index;
	}

	// Make room for the new entry at its matched location by memmove()'ing data:
	// Determine the starting index and shift direction for data and timeout shifts

	// If the array isn't at capacity, shift data towards the end and grow the array
	if(tvl->size < tvl->max_size)
	{
		source = index;
		dest = source + 1;
		length = tvl->size++ - index;
		deleted = 0;
	}
	// Move a subsection of the array over the lowest valued entry
	// Shift forward over lowest entry-- index does not change
	else if(index <= tvl->min_value_index)
	{
		source = index;
		dest = index + 1;
		length = tvl->min_value_index - index;
		deleted = 1;
	}
	// Shift backward over lowest entry-- this decrements the index
	else
	{
		// Decrement the index and match location because all list entries
		// above the deleted entries will have their index decreased by one
		--index;
		match = (char *)match - tvl->entry_size;

		source = tvl->min_value_index + 1;
		dest = tvl->min_value_index;
		length = index - tvl->min_value_index;
		deleted = 1;
	}

	// Handle the deleted entry if necessary
	if(delete_handler && deleted)
		delete_handler(tvl, tvl->min_value_index, arg);

	// Shift the pointer data and associated timeouts and values
	memmove(tvl_data(tvl, dest), tvl_data(tvl, source), length * tvl->entry_size);
	memmove(&tvl->timeout[dest], &tvl->timeout[source], length * sizeof(float));
	memmove(&tvl->value[dest], &tvl->value[source], length * sizeof(float));

	// Store the data in the new array entry
	memcpy(match, entry, tvl->entry_size);
	tvl->timeout[index] = timeout;
	tvl->value[index] = value;

	// If an array element was deleted, recompute the mins-- otherwise differentially update
	if(deleted)
	{
		tvl_update_mins(tvl);
	}
	else
	{
		// Check for a new minimum timeout
		if(timeout < tvl->min_timeout)
			tvl->min_timeout = timeout;

		// Check for a new minimum value-- the old minimum index might have
		// shifted by one even if this entry doesn't have the new minimum value
		if(tvl->min_value_index < 0 || value < tvl->min_value)
		{
			tvl->min_value = value;
			tvl->min_value_index = index;
		}
		else if(tvl->min_value_index >= index)
		{
			tvl->min_value_index++;
		}
	}

	// Call the insert handler if necessary
	if(insert_handler)
		insert_handler(tvl, index, arg);

	// Tell the caller where this data resides
	return index;
}

/*
================
octree_node_swap

Swaps the locations and data pointers of two octree nodes.

NOTE: This function does NOT change the sector pointers!
This function is designed used for octree creation,
before the sector pointers have been determined.
================
*/
void octree_node_swap(octree_node_t * a, octree_node_t * b)
{
	register void  *temp_data;

	// Swap data pointers
	temp_data = a->data;
	a->data = b->data;
	b->data = temp_data;
}

/*
===============
octree_assemble

Assembles a rearranges the elements in an unsorted array
of octree nodes into a tree.  The function returns the
root node of the tree.

NOTE: The root node of the tree will be swapped to be
the first element of the array.

NOTE: This function is slow: O(N^2).  The tree this
function returns should be optimally balanced (or
nearly so).  Optimal balance isn't that much better
than random insert for octrees, however.  But this
function still uses the optimal balancing because
the code which uses it only computes the tree once
on startup.  This means A) the initial computation
cost is pretty meaningless and B) accidently getting
a bad tree will cause slowdowns for the rest of the
game.  Feel free to use/write a different construction
function if your octree needs differ.
===============
*/
octree_node_t  *octree_assemble(octree_node_t * nodes, int num_nodes, const float *(data_location) (const void *))
{
	int             i, closest_node, sort_length;
	int             sector, front_sector, back_sector, front_size, back_size;
	float           scale, dist, closest_dist;
	const float    *point, *root_point;
	vec3_t          centroid;
	octree_node_t  *root, *sort_start, *node;

	// Empty array: Do nothing
	if(num_nodes <= 0)
		return NULL;

	// Leaf node case: Set all sector pointers to NULL
	if(num_nodes == 1)
		return nodes;

	// Compute the centroid of the nodes
	VectorSet(centroid, 0, 0, 0);
	for(i = 0; i < num_nodes; i++)
	{
		point = data_location(nodes[i].data);
		VectorAdd(centroid, point, centroid);
	}
	scale = 1.0 / num_nodes;
	VectorScale(centroid, scale, centroid);

	// Determine the node closest to the centroid
	closest_node = 0;
	point = data_location(nodes[0].data);
	closest_dist = DistanceSquared(centroid, point);
	for(i = 1; i < num_nodes; i++)
	{
		point = data_location(nodes[i].data);
		dist = DistanceSquared(centroid, point);
		if(dist < closest_dist)
		{
			closest_node = i;
			closest_dist = dist;
		}
	}

	// Move the closest node to the first array entry
	root = &nodes[0];			// Yes, this is meaningless, but it makes the code prettier
	octree_node_swap(root, &nodes[closest_node]);
	root_point = data_location(root->data);

	// Group all other nodes into contiguous blocks sharing the same sector.
	// This is done similarly to qsort(), so each pass can store one sector
	// at the front of the array and another sector at the end.  Therefore,
	// two sectors get processed per pass.  The sectors are sorted by ascending
	// sector index, but doing so isn't necessary.
	sort_start = &nodes[1];
	sort_length = num_nodes - 1;
	for(front_sector = 0, back_sector = 7 - front_sector;
		front_sector <= back_sector; front_sector++, back_sector = 7 - front_sector)
	{
		// Search for nodes with matching sectors
		front_size = 0;
		back_size = 0;
		for(i = 0; i < sort_length; i++)
		{
			// Sort nodes matching sector into array front or back as appropriate
			node = &sort_start[i];
			point = data_location(node->data);
			sector = octree_sector(root_point, point);

			if(sector == front_sector)
				octree_node_swap(node, &sort_start[front_size++]);
			else if(sector == back_sector)
				octree_node_swap(node, &sort_start[sort_length - ++back_size]);
		}

		// Recursively sort the sectors in the front and back sections of the array
		root->sector[front_sector] = octree_assemble(sort_start, front_size, data_location);
		root->sector[back_sector] = octree_assemble(&sort_start[sort_length - back_size], back_size, data_location);

		// Update the starting pointer and length for the next iteration
		sort_start += front_size;
		sort_length -= front_size + back_size;
	}

	// Return the root node to the function's caller
	return root;
}

/*
=======================
octree_neighbor_recurse
=======================
*/
void octree_neighbor_recurse(vec3_t point, octree_node_t * root,
							 const float *(data_location) (const void *),
							 int max_neighbors, void *neighbors[], float dists[], int *num_neighbors, int *furthest_neighbor)
{
	int             i, sector, subsector, sector_modify, order_code;
	float           sector_dist[8];
	const float    *center;

	// This table contains all possible optimal subsector search orderings.
	// In theory, the octree recursion should check subsectors closer to the
	// input point before subsectors that are further.
	//
	// The sector containing the point is searched first.  Then nearby
	// sectors are searched by xor'ing the appropriate bit(s) (see octree_sector()
	// in ai_lib.h for more information).  Each entry in this table represents
	// a set of bits to flip in the original sector's id to determine the next
	// sector to search.
	//
	// The distance squared from the input point to each axis is computed.  Then
	// four comparisons between these distances will distinguish the different
	// orderings.  The four comparisons are composed into a ordering code:
	//   0x1 - Set if X < Y
	//   0x2 - Set if Y < Z
	//   0x4 - Set if Z < X
	//   0x8 - Set if near axis + middle axis < far axis
	// NOTE: The 0x8 bit cannot be computed until the 0x1, 0x2, and 0x4 bits
	// are computed.
	static const int sector_order[16][8] = {
		{OT_0, OT_X, OT_Y, OT_XY, OT_Z, OT_XZ, OT_YZ, OT_XYZ},	//  0 = 0000: X < Y < Z < X (ERROR)

		{OT_0, OT_X, OT_Z, OT_Y, OT_XZ, OT_XY, OT_YZ, OT_XYZ},	//  1 = 0001: X < Z < Y < X+Z < X+Y < Y+Z < X+Y+Z
		{OT_0, OT_Y, OT_X, OT_Z, OT_XY, OT_YZ, OT_XZ, OT_XYZ},	//  2 = 0010: Y < X < Z < X+Y < Y+Z < X+Z < X+Y+Z
		{OT_0, OT_X, OT_Y, OT_Z, OT_XY, OT_XZ, OT_YZ, OT_XYZ},	//  3 = 0011: X < Y < Z < X+Y < X+Z < Y+Z < X+Y+Z
		{OT_0, OT_Z, OT_Y, OT_X, OT_YZ, OT_XZ, OT_XY, OT_XYZ},	//  4 = 0100: Z < Y < X < Y+Z < X+Z < X+Y < X+Y+Z
		{OT_0, OT_Z, OT_X, OT_Y, OT_XZ, OT_YZ, OT_XY, OT_XYZ},	//  5 = 0101: Z < X < Y < X+Z < Y+Z < X+Y < X+Y+Z
		{OT_0, OT_Y, OT_Z, OT_X, OT_YZ, OT_XY, OT_XZ, OT_XYZ},	//  6 = 0110: Y < Z < X < Y+Z < X+Y < X+Z < X+Y+Z

		{OT_0, OT_X, OT_Y, OT_XY, OT_Z, OT_XZ, OT_YZ, OT_XYZ},	//  7 = 0111: Z < Y < X < Z (ERROR)
		{OT_0, OT_X, OT_Y, OT_XY, OT_Z, OT_XZ, OT_YZ, OT_XYZ},	//  8 = 1000: X < Y < Z < X (ERROR)

		{OT_0, OT_X, OT_Z, OT_XZ, OT_Y, OT_XY, OT_YZ, OT_XYZ},	//  9 = 1001: X < Z < X+Z < Y < X+Y < Y+Z < X+Y+Z
		{OT_0, OT_Y, OT_X, OT_XY, OT_Z, OT_YZ, OT_XZ, OT_XYZ},	// 10 = 1010: Y < X < X+Y < Z < Y+Z < X+Z < X+Y+Z
		{OT_0, OT_X, OT_Y, OT_XY, OT_Z, OT_XZ, OT_YZ, OT_XYZ},	// 11 = 1011: X < Y < X+Y < Z < X+Z < Y+Z < X+Y+Z
		{OT_0, OT_Z, OT_Y, OT_YZ, OT_X, OT_XZ, OT_XY, OT_XYZ},	// 12 = 1100: Z < Y < Y+Z < X < X+Z < X+Y < X+Y+Z
		{OT_0, OT_Z, OT_X, OT_XZ, OT_Y, OT_YZ, OT_XY, OT_XYZ},	// 13 = 1101: Z < X < X+Z < Y < Y+Z < X+Y < X+Y+Z
		{OT_0, OT_Y, OT_Z, OT_YZ, OT_X, OT_XY, OT_XZ, OT_XYZ},	// 14 = 1110: Y < Z < Y+Z < X < X+Y < X+Z < X+Y+Z

		{OT_0, OT_X, OT_Y, OT_XY, OT_Z, OT_XZ, OT_YZ, OT_XYZ},	// 15 = 1111: Z < Y < X < Z (ERROR)
	};

	// Determine which subsector the target point resides in
	center = data_location(root->data);
	sector = octree_sector(center, point);

	// For each sector modifier "i", compute the distance to the "sector ^ i" sector
	// NOTE: OT_XYZ = 7 = 0111 is the same as the distance to the center node point.
	sector_dist[OT_0] = 0;

	sector_dist[OT_X] = Square(center[0] - point[0]);
	sector_dist[OT_Y] = Square(center[1] - point[1]);
	sector_dist[OT_Z] = Square(center[2] - point[2]);

	sector_dist[OT_XY] = sector_dist[OT_X] + sector_dist[OT_Y];
	sector_dist[OT_XZ] = sector_dist[OT_X] + sector_dist[OT_Z];
	sector_dist[OT_YZ] = sector_dist[OT_Y] + sector_dist[OT_Z];

	sector_dist[OT_XYZ] = sector_dist[OT_X] + sector_dist[OT_Y] + sector_dist[OT_Z];


	// Track this node as a neighbor if there's still space left
	if(*num_neighbors < max_neighbors)
	{
		// Add this to the next open slot
		i = (*num_neighbors)++;
		neighbors[i] = root->data;
		dists[i] = sector_dist[OT_XYZ];

		// Track this as the new furthest neighbor if that's now the case
		if((*furthest_neighbor < 0) || (dists[*furthest_neighbor] < dists[i]))
			*furthest_neighbor = i;
	}

	// Also track it as a neighbor if it's nearer than the furthest neighbor
	else if((*furthest_neighbor >= 0) && (sector_dist[OT_XYZ] < dists[*furthest_neighbor]))
	{
		// Replace the furthest neighbor
		i = *furthest_neighbor;
		neighbors[i] = root->data;
		dists[i] = sector_dist[OT_XYZ];

		// Find out which neighbor is now the furthest
		for(i = 0; i < *num_neighbors; i++)
			if(dists[*furthest_neighbor] < dists[i])
				*furthest_neighbor = i;
	}

	// Order the axies by proximity to the target point
	order_code = ((sector_dist[OT_X] < sector_dist[OT_Y]) << 0) |
		((sector_dist[OT_Y] < sector_dist[OT_Z]) << 1) | ((sector_dist[OT_Z] < sector_dist[OT_X]) << 2);

	// Determine whether the sum of the minimum and middle distances
	// is less than the maximum axis distance.  Because of how the
	// sector_order table is structured, entries 1, 2, and 3 for an
	// order code with an unset 0x8 bit refer to the axies of minimum,
	// middle, and maximum distances respectively.
	order_code |= (sector_dist[sector_order[order_code][1]] +
				   sector_dist[sector_order[order_code][2]] < sector_dist[sector_order[order_code][3]]) << 3;

	// Search the subsectors in the optimal search order for the closest neighbor
	for(i = 0; i < 8; i++)
	{
		// Compute the next nearest subsector index
		sector_modify = sector_order[order_code][i];
		subsector = sector ^ sector_modify;

		// If all points in the subsector are too far away and enough neigbors
		// have been found, stop processing
		//
		// NOTE: This check makes the algorithm have logarithmic expected
		// time instead of linear expected time.
		if((max_neighbors <= *num_neighbors) &&
		   (*furthest_neighbor > 0) && (dists[*furthest_neighbor] <= sector_dist[sector_modify]))
			break;

		// Only process the sector if it exists
		if(!root->sector[subsector])
			continue;

		// Recursively process this subsector
		octree_neighbor_recurse(point, root->sector[subsector], data_location,
								max_neighbors, neighbors, dists, num_neighbors, furthest_neighbor);
	}
}

/*
================
octree_neighbors

Determines the "max_neighbors" closest neighbors to the inputted
point.  Returns the actual number of neighbors found.  (This will be
the size of the tree if the tree has fewer points than num_neighbors.)
The "neighbors" array will be filled out with the data points of the
closests neighbors.  The squares of the distances to each of these
neighbors will be stored in the "dists" array.  These arguments are
NOT optional.

The "data_location" function, when applied to the data nodes,
returns a pointer to the coordinates of that data's location.
================
*/
int octree_neighbors(vec3_t point, octree_node_t * root,
					 const float *(data_location) (const void *), int max_neighbors, void *neighbors[], float dists[])
{
	int             furthest_neighbor, num_neighbors;

	// Check for trees with zero elements and null requests
	if(!root || max_neighbors <= 0)
		return 0;

	// Recursively search for nodes closer than the input minimum squared
	// distance (currently none)
	num_neighbors = 0;
	furthest_neighbor = -1;
	octree_neighbor_recurse(point, root, data_location, max_neighbors, neighbors, dists, &num_neighbors, &furthest_neighbor);

	// Return the number of neighbors actually foud
	return num_neighbors;
}

/*
===============
octree_neighbor

Returns NULL if no points are in the tree.  Otherwise
it returns a pointer to the octree data element whose
point is nearest to the input point.  The "data_location"
function, when applied to the data notes, returns a pointer
to the coordinates of that data's location.
===============
*/
void           *octree_neighbor(vec3_t point, octree_node_t * root, const float *(data_location) (const void *))
{
	float           dist;
	void           *nearest;

	// Find only the closest neighbor to this point; fail if nothing was found
	if(!octree_neighbors(point, root, data_location, 1, &nearest, &dist))
		return NULL;

	// Return whatever data was found
	return nearest;
}

/*
====================
octree_print_recurse
====================
*/
void octree_print_recurse(octree_node_t * root, const char *(data_name) (const void *), int indent)
{
	int             i, count;

	// Print the leading spaces
	for(i = 0; i < indent; i++)
		G_Printf("  ");

	// Print the root node name
	G_Printf("%s - Sectors Used:", data_name(root->data));

	// Print the node's list of used sectors
	count = 0;
	for(i = 0; i < 8; i++)
	{
		if(root->sector[i])
		{
			G_Printf(" %i", i);
			count++;
		}
	}
	G_Printf(" (%i total)\n", count);

	// Print contents of each sector
	indent++;
	for(i = 0; i < 8; i++)
		if(root->sector[i])
			octree_print_recurse(root->sector[i], data_name, indent);
}

/*
============
octree_print

Print out the contents of an octree
============
*/
void octree_print(octree_node_t * root, const char *(data_name) (const void *))
{
	octree_print_recurse(root, data_name, 0);
}


/*
========================
isi_index_exist_byte_bit

Look up the byte and bit values that access the
isi->exists[] value for testing whether a given
index exists in the iterator's current state.
========================
*/
void isi_index_exist_byte_bit(int index, int *byte, int *bit)
{
	*byte = index / sizeof(int);
	*bit = index % sizeof(int);
}

/*
================
isi_index_exists

Test whether a given index exists in
the iterator's current state.
================
*/
int isi_index_exists(index_subset_iter_t * isi, int index)
{
	int             byte, bit;

	// Look up which bit this index uses
	isi_index_exist_byte_bit(index, &byte, &bit);

	// Return that bit's truth value
	return (isi->exists[byte] & (1 << bit) ? 1 : 0);
}

/*
===================
isi_index_exist_set

Set an index as existing in the
iterator's current state.
===================
*/
void isi_index_exist_set(index_subset_iter_t * isi, int index)
{
	int             byte, bit;

	// Look up which bit this index uses
	isi_index_exist_byte_bit(index, &byte, &bit);

	// Set that bit
	isi->exists[byte] |= (1 << bit);
}

/*
===================
isi_index_exist_set

Set an index as not existing in
the iterator's current state.
===================
*/
void isi_index_exist_unset(index_subset_iter_t * isi, int index)
{
	int             byte, bit;

	// Look up which bit this index uses
	isi_index_exist_byte_bit(index, &byte, &bit);

	// Clear that bit
	isi->exists[byte] &= ~(1 << bit);
}

/*
=========
isi_start

Create the starting (null) subset for an index subset iterator.

NOTE: The inputted isi must be setup with its max_size (max number
of indicies per subset) and range (number of different index values).
=========
*/
void isi_start(index_subset_iter_t * isi)
{
	int             i;

	// Null iterators don't iterate
	if(!isi)
		return;

	// Clear the bitmap of which indicies exist
	memset(isi->exists, 0, sizeof(isi->exists));

	// Setup the initial state, the null set
	isi->size = 0;

	// The starting state has been setup
	isi->valid = 1;
}

/*
========
isi_next

Iterate to the next subset for an index subset iterator.
Returns true if this subset exists and false if not.
========
*/
int isi_next(index_subset_iter_t * isi)
{
	int             change;

	// Non-iterators don't iterate
	if(!isi || isi->max_size <= 0)
		return 0;

	// Increase the set size if possible ...
	if(isi->size < isi->max_size)
	{
		change = isi->size++;
		isi->index[change] = -1;
	}

	// .. Otherwise just update the last entry
	else
	{
		change = isi->size - 1;
		isi_index_exist_unset(isi, isi->index[change]);
	}

	// Increment indicies until a valid index is found
	isi->valid = 0;
	do
	{
		// Try the next index
		isi->index[change]++;

		// If that index doesn't exist, update the previous subset index instead
		if(isi->index[change] >= isi->range)
		{
			// Iteration is done when all indicies have been incremented to the maximum
			if(--isi->size == 0)
				break;

			// Update the previous index instead
			isi_index_exist_unset(isi, isi->index[--change]);
			continue;
		}

		// Try another value if this index is already in use
		if(isi_index_exists(isi, isi->index[change]))
			continue;

		// Keep this index
		isi_index_exist_set(isi, isi->index[change]);
		isi->valid = 1;
	}
	while(!isi->valid);

	// Let the caller know whether or not the next state existed
	return isi->valid;
}

/*
========
isi_skip

Skip over all subsets starting with the iterator's current
lexicagraphical state.  For example, isi_skip() on state
(0, 5, 2) will produce state (0, 5, 3) even if the maximum
subset size is 10.  Returns true if this subset exists and
false if not.
========
*/
int isi_skip(index_subset_iter_t * isi)
{
	int             old_max_size;

	// Don't let the iterator iterator to subsets deeper than its current state
	old_max_size = isi->max_size;
	isi->max_size = isi->size;

	// Find the next subset no deeper than this
	isi_next(isi);

	// Restor the correct maximum size
	isi->max_size = old_max_size;

	// Let the caller know whether or not the next subset existed
	return isi->valid;
}

/*
==========
isi_string

Create a string description of the
iterator's current set of indicies.
Returns the inputted string buffer.
==========
*/
char           *isi_string(index_subset_iter_t * isi, char *string, int max_length)
{
	int             i, length;
	char            index[32];

	// Load the opening bracket
	strcpy(string, "[");

	// Add the indicies
	for(i = 0; i < isi->size; i++)
	{
		// Separate the indicies after the first
		if(i > 0)
			Q_strcat(string, max_length, ", ");

		// Add this index to the list
		Com_sprintf(index, sizeof(index), "%i", isi->index[i]);
		Q_strcat(string, max_length, index);
	}

	// The closing bracket
	strcat(string, "]");

	// Make functional programmers happy
	return string;
}
