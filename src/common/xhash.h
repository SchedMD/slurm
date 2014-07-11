/*****************************************************************************\
 *  xtree.h - functions used for hash table manament
 *****************************************************************************
 *  Copyright (C) 2012 CEA/DAM/DIF
 *  Copyright (C) 2013 SchedMD LLC. Written by David Bigagli
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifndef __XHASH_EJ2ORE_INC__
#define __XHASH_EJ2ORE_INC__

#include <stdint.h>
#include <pthread.h>

/** Opaque definition of the hash table */
typedef struct xhash_st xhash_t;

/**
  * This function will be used to generate unique identifier from a
  * stored item by returning a string.
  * Beware that string conflict cause an item to be unfindable by the
  * hash table.
  *
  * @param item takes one of the items stored by the lists in the hash
  *             table.
  * @returns an unique identifier used for making hashes.
  */
typedef const char* (*xhash_idfunc_t)(void* item);

/**
  * @param id is the unique identifier an item can be identified with.
  * @param hashes_count is the number of hashes contained in the hash
  *                     table and the function must return an index in
  *                     range [0 to hashes_count-1].
  * @returns a hash used as an index for storing the item identified by
  *          the given id.
  */

/* Currently not implementable with uthash */
typedef unsigned (*xhash_hashfunc_t)(unsigned hashes_count, const char* id);

/** This type of function is used to free data inserted into xhash table */
typedef void (*xhash_freefunc_t)(void* item);

/** Initialize the hash table.
 *
 * @param idfunc is used to calculate a string unique identifier from a user
 *               item.
 * @param freefunc is used to free data insterted to the xhash table, use NULL
 *		   to bypass it.
 *
 * @returns the newly allocated hash table. Must be freed with xhash_free.
 */
xhash_t* xhash_init(xhash_idfunc_t idfunc,
		    xhash_freefunc_t freefunc,
		    xhash_hashfunc_t hashfunc, /* Currently: should be NULL */
		    uint32_t table_size);      /* Currently: unused         */

/** @returns an item from a key searching through the hash table. NULL if not
 * found.
 */
void* xhash_get(xhash_t* table, const char* key);

/** Add an item to the hash table.
 * @param table is the hash table you want to add the item to.
 * @param item is the user item to add. It has to be initialized in order for
 *             the idfunc function to be able to calculate the final unique
 *             key string associated with it.
 * @returns item or NULL in case of error.
 */
void* xhash_add(xhash_t* table, void* item);

/** Remove an item associated with a key from the hash table but does not free
 * memory associated with the item even if freefunc was not null at init time.
 * @returns the removed item value.
 */
void* xhash_pop(xhash_t* table, const char* key);

/** Remove an item associated with a key from the hash table.
 * If found and freefunc at init time was not null, free the item's memory.
 */
void xhash_delete(xhash_t* table, const char* key);

/** @returns the number of items stored in the hash table */
uint32_t xhash_count(xhash_t* table);

/** apply callback to each item contained in the hash table */
void xhash_walk(xhash_t* table,
        void (*callback)(void* item, void* arg),
        void* arg);

/** This function frees the hash table items. It frees items too if the
 * freefunc was not null in the xhash_init function.
 */
void xhash_clear(xhash_t* table);

/** This function frees the hash table, clearing it beforehand.
 * @parameter table is the hash table to free. The table pointer is invalid
 *                  after this call.
 */
void xhash_free(xhash_t* table);

/* String hash table using the pjw hashing algorithm
 * and chaining conflict resolution.
 * Includes a double linked list implementation.
 */
struct hash_entry {
	struct hash_entry *forw;
	struct hash_entry *back;
	char *key;
	void *data;
};

struct hash_tab {
	uint32_t size;
	uint32_t num_ents;
	struct list_ **lists;
};

struct list_ {
    struct list_ *forw;
    struct list_ *back;
    uint32_t num_ents;
    char *name;
};

#define LIST_NUM_ENTS(L) ((L)->num_ents)

/* Double link list implementation is
 * part of the hash library.
 */
extern struct list_ *list_make_(const char *);
extern int  list_insert_(struct list_ *,
			 struct list_ *,
			 struct list_ *);
extern int list_push_(struct list_ *,
		      struct list_ *);
extern int list_enque_(struct list_ *,
		       struct list_ *);
extern struct list_ *list_rm_(struct list_ *,
			      struct list_ *);
struct list_ *list_pop_(struct list_ *);
extern struct list_ *list_deque_(struct list_ *);
extern void list_free_(struct list_ *, void (*f)(void *));

/* Hash table interface.
 */
extern struct hash_tab *hash_make(uint32_t);
extern int hash_install(struct hash_tab *, const char *, void *);
extern void *hash_lookup(struct hash_tab *, const char *);
extern void *hash_remove(struct hash_tab *, const char *);
extern void hash_free(struct hash_tab *, void (*f)(char *key, void *data));


#endif

