/* 2012-06-25
{|BEGINHEADER|}
{|ENDHEADER|}
*/

#ifndef __XHASH_EJ2ORE_INC__
#define __XHASH_EJ2ORE_INC__

#include <stdint.h>

/** Opaque definition of the hash table */
struct xhash_st;
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

/** @returns an item from a key searching through the hash table. NULL if not
 * found.
 */
void* xhash_get(xhash_t* table, const char* key);

/** Initialize the hash table.
 *
 * @param idfunc is used to calculate a string unique identifier from a user
 *               item.
 *
 * @returns the newly allocated hash table. Must be freed with xhash_free.
 */
xhash_t* xhash_init(xhash_idfunc_t idfunc,
		    xhash_hashfunc_t hashfunc, /* Currently: should be NULL */
		    uint32_t table_size);      /* Currently: unused         */

/** Add an item to the hash table.
 * @param table is the hash table you want to add the item to.
 * @param item is the user item to add. It has to be initialized in order for
 *             the idfunc function to be able to calculate the final unique
 *             key string associated with it.
 * @returns item or NULL in case of error.
 */
void* xhash_add(xhash_t* table, void* item);

/** Remove an item associated with key from the hash table item is if found,
 * but do not free the item memory itself (the user is responsible for
 * managing item's memory).
 */
void xhash_delete(xhash_t* table, const char* key);

/** @returns the number of items stored in the hash table */
uint32_t xhash_count(xhash_t* table);

/** apply callback to each item contained in the hash table */
void xhash_walk(xhash_t* table,
        void (*callback)(void* item, void* arg),
        void* arg);

/** This function frees the hash table, but does not free its stored items,
 * you can use xhash_walk to perform a free operation on all items if wanted.
 * @parameter table is the hash table to free. The table pointer is invalid
 *                  after this call.
 */
void xhash_free(xhash_t* table);

#endif

