/*****************************************************************************\
 *  xahash.h - functions used for arbitrary hash table
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
 *
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifndef _XAHASH_H
#define _XAHASH_H

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>

#include "src/common/macros.h"

/*
 * Arbitrary hash table:
 *	Meant to not be limited to hashed cstrings
 */

typedef void xahash_table_t;
typedef uint32_t xahash_hash_t;

/*
 * Create hash for a given key
 * IN key - pointer to key
 * IN key_bytes - Number of bytes in key
 * IN state - arbitrary bytes to hand to function for state tracking
 * RET hash for given key
 * WARNING: returned hash should be unique for a given key as much as possible
 */
typedef xahash_hash_t (*xahash_func_t)(const void *key, const size_t key_bytes,
				       void *state);

/*
 * Match hash table pointer to a given key
 * IN entry - pointer to bytes for entry in hash table
 * 	Pointer is only guaranteed to be valid until the next operation on the
 * 	table. Place pointers in the entry's bytes instead of using the entry
 * 	pointer for logic.
 * IN key - pointer to key
 * IN key_bytes - Number of bytes in key
 * IN state - arbitrary bytes to hand to function for state tracking
 * RET true if entry matches key
 * WARNING: only 1 entry must ever match to key at a time
 */
typedef bool (*xahash_match_func_t)(void *entry, const void *key,
				    const size_t key_bytes, void *state);

/*
 * On insert of new key into hash table.
 * IN entry - pointer to bytes for entry in hash table
 * 	Pointer is only guaranteed to be valid until the next operation on the
 * 	table. Place pointers in the entry's bytes instead of using the entry
 * 	pointer for logic.
 * IN key - pointer to key
 * IN key_bytes - Number of bytes in key
 * IN state - arbitrary bytes to hand to function for state tracking
 */
typedef void (*xahash_on_insert_func_t)(void *entry, const void *key,
					const size_t key_bytes, void *state);

typedef enum {
	XAHASH_FOREACH_INVALID = 0,
	XAHASH_FOREACH_CONT, /* continue for each processing */
	XAHASH_FOREACH_STOP, /* stop for each processing */
	XAHASH_FOREACH_FAIL, /* stop for each processing due to failure */
	XAHASH_FOREACH_INVALID_MAX /* assertion only value on max value */
} xahash_foreach_control_t;

/*
 * Callback for when walking each entry in hashtable
 * IN entry - pointer to bytes for entry in hash table
 * 	Pointer is only guaranteed to be valid until the next operation on the
 * 	table. Place pointers in the entry's bytes instead of using the entry
 * 	pointer for logic.
 * IN state - arbitrary bytes to hand to function for state tracking
 * IN arg - arbitrary pointer from caller
 * RET control walking hashtable
 */
typedef xahash_foreach_control_t (*xahash_foreach_func_t)(void *entry,
							  void *state,
							  void *arg);

/*
 * Free anything releated to entry's bytes
 * IN entry - pointer to bytes for entry in hash table
 * IN state - arbitrary bytes to hand to function for state tracking
 */
typedef void (*xahash_on_free_func_t)(void *entry, void *state);

/*
 * Create arbitrary hash table
 *
 * IN hash_func - hash function to be used during operations
 * IN match_func - function to verify key matches to an entry
 * IN on_insert_func - function called every time a new entry is inserted
 * IN on_free_func - function called every time an entry is released
 * IN state_bytes - Arbitrary number of bytes to store as state
 * 	bytes held as state are only modified by client.
 * 	hash table tracks state independently of these bytes.
 * 	Bytes are provided to avoid needing an xmalloc() per table.
 * IN bytes_per_entry - Arbitrary number of bytes needed per entry
 * 	bytes for each entry are only modified by client.
 * 	hash table tracks state independently of these bytes.
 * 	Bytes are provided to avoid needing an xmalloc() per entry.
 * IN fixed_table_size - Fixed number of entries in hash table or
 *	0 for dynamic sizing (TODO: NOT IMPLEMENTED YET)
 * RET new hash table pointer to call other xhahash_*().
 *	Must be released by calling FREE_NULL_XAHASH_TABLE().
 *
 */
#define xahash_new_table(hash_func, match_func, on_insert_func, on_free_func, \
			 state_bytes, bytes_per_entry, fixed_table_size)      \
	xahash_new_table_funcname(hash_func, XSTRINGIFY(hash_func), \
				  match_func, XSTRINGIFY(match_func), \
				  on_insert_func, XSTRINGIFY(on_insert_func), \
				  on_free_func, XSTRINGIFY(on_free_func), \
				  state_bytes, bytes_per_entry, \
				  fixed_table_size)

extern xahash_table_t *xahash_new_table_funcname(xahash_func_t hash_func,
	const char *hash_func_string, xahash_match_func_t match_func,
	const char *match_func_string, xahash_on_insert_func_t on_insert_func,
	const char *on_insert_func_string, xahash_on_free_func_t on_free_func,
	const char *on_free_func_string, const size_t state_bytes,
	const size_t bytes_per_entry, const size_t fixed_table_size);

/*
 * Release arbitrary hash table
 * WARNING: call FREE_NULL_XAHASH_TABLE() instead
 */
extern void xahash_free_table(xahash_table_t *ht);

#define FREE_NULL_XAHASH_TABLE(_X)             \
	do {                                   \
		if (_X)                        \
			xahash_free_table(_X); \
		_X = NULL;                     \
	} while (0)

/*
 * Get pointer to arbirtarty state held in hashtable
 * N
 * OTE: re-entrant w/r to ht. Not thread safe.
 */
extern void *xahash_get_state_ptr(xahash_table_t *ht);

/*
 * Get entry from hash table (but don't insert if not found)
 * NOTE: re-entrant w/r to ht. Not thread safe.
 * IN ht - hashtable ptr created by init_xahash_table()
 * IN key - key to identify entry
 * IN key_bytes - Number of bytes in key
 * RET pointer to entry bytes for use.
 * 	Pointer is only guaranteed to be valid until the next operation on the
 * 	table. Place pointers in the entry's bytes instead of using the entry
 * 	pointer for logic.
 */
extern void *xahash_find_entry(const xahash_table_t *ht, const void *key,
			       const size_t key_bytes);

/*
 * Get entry from hash table and insert if not found
 * NOTE: re-entrant w/r to ht. Not thread safe.
 * IN ht - hashtable ptr created by init_xahash_table()
 * IN key - key to identify entry
 * IN key_bytes - Number of bytes in key
 * RET pointer to entry bytes for use
 * 	Pointer is only guaranteed to be valid until the next operation on the
 * 	table. Place pointers in the entry's bytes instead of using the entry
 * 	pointer for logic.
 * WARNING: Do not assume entry bytes has been zeroed
 */
extern void *xahash_insert_entry(xahash_table_t *ht, const void *key,
				 const size_t key_bytes);

/*
 * Walk every entry in hashtable
 * NOTE: re-entrant w/r to ht. Not thread safe.
 * IN ht - hashtable ptr created by init_xahash_table()
 * IN callback - function to call on every entry
 * IN arg - arbitrary pointer to hand to callback function
 * RET number of entries walked, negative on failure
 */
#define xahash_foreach_entry(ht, callback, arg) \
	xahash_foreach_entry_funcname(ht, callback, XSTRINGIFY(callback), arg)

extern int xahash_foreach_entry_funcname(xahash_table_t *ht,
					 xahash_foreach_func_t callback,
					 const char *callback_string,
					 void *arg);

/*
 * Release hash entry for key
 * NOTE: re-entrant w/r to ht. Not thread safe.
 * IN ht - hashtable ptr created by init_xahash_table()
 * IN key - key to identify entry
 * IN key_bytes - Number of bytes in key
 * RET true if found and released or false if not found
 */
extern bool xahash_free_entry(xahash_table_t *ht, const void *key,
			      const size_t key_bytes);

#endif /* _XAHASH_H */
