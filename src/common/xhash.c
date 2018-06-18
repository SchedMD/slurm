/*****************************************************************************\
 *  xtree.c - functions used for hash table manament
 *****************************************************************************
 *  Copyright (C) 2012 CEA/DAM/DIF
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

#include "src/common/xhash.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/uthash/uthash.h"

#if 0
/* undefine default allocators */
#undef uthash_malloc
#undef uthash_free

/* re-define them using slurm's ones */
#define uthash_malloc(sz) xmalloc(sz)
#define uthash_free(ptr, sz) xfree(ptr)
#endif

/*
 * FIXME:
 * A pre-malloced array of xhash_item_t could be associated to
 * the xhash_table in order to speed up the xhash_add function.
 * The default array size could be something like 10% of the
 * provided table_size (hash table are commonly defined larger
 * than necessary to avoid shared keys and usage of linked list)
 */

typedef struct xhash_item_st {
	void*		item;    /* user item                               */
	const char*	key;     /* cached key calculated by user function, */
                                 /* needed by uthash                        */
	uint32_t	keysize; /* cached key size                         */
	UT_hash_handle	hh;      /* make this structure hashable by uthash  */
} xhash_item_t;

struct xhash_st {
	uint32_t		count;    /* user items count                */
	xhash_freefunc_t	freefunc; /* function used to free items     */
	xhash_item_t*		ht;       /* hash table                      */
	xhash_idfunc_t		identify; /* function returning a unique str
					     key */
};

xhash_t *xhash_init(xhash_idfunc_t idfunc, xhash_freefunc_t freefunc)
{
	xhash_t* table = NULL;
	if (!idfunc)
		return NULL;
	table = (xhash_t*)xmalloc(sizeof(xhash_t));
	table->ht = NULL; /* required by uthash */
	table->count = 0;
	table->identify = idfunc;
	table->freefunc = freefunc;
	return table;
}

static xhash_item_t* xhash_find(xhash_t* table, const char* key)
{
	xhash_item_t* hash_item = NULL;
	uint32_t      key_size  = 0;

	if (!table || !key)
		return NULL;
	key_size = strlen(key);
	HASH_FIND(hh, table->ht, key, key_size, hash_item);
	return hash_item;
}

void* xhash_get(xhash_t* table, const char* key)
{
	xhash_item_t* item = xhash_find(table, key);
	if (!item)
		return NULL;
	return item->item;
}

void* xhash_add(xhash_t* table, void* item)
{
	xhash_item_t* hash_item = NULL;
	if (!table || !item)
		return NULL;
	hash_item          = (xhash_item_t*)xmalloc(sizeof(xhash_item_t));
	hash_item->item    = item;
	hash_item->key     = table->identify(item);
	hash_item->keysize = strlen(hash_item->key);
	HASH_ADD_KEYPTR(hh, table->ht, hash_item->key,
			hash_item->keysize, hash_item);
	++table->count;
	return hash_item->item;
}

void* xhash_pop(xhash_t* table, const char* key)
{
	void* item_item;
	xhash_item_t* item = xhash_find(table, key);
	if (!item)
		return NULL;
	item_item = item->item;
	HASH_DELETE(hh, table->ht, item);
	xfree(item);
	--table->count;
	return item_item;
}

void xhash_delete(xhash_t* table, const char* key)
{
	if (!table || !key)
		return;
	void* item_item = xhash_pop(table, key);
	if (table->freefunc)
		table->freefunc(item_item);
}

uint32_t xhash_count(xhash_t* table)
{
	if (!table)
		return 0;
	return table->count;
}

void xhash_walk(xhash_t* table,
		void (*callback)(void* item, void* arg),
		void* arg)
{
	xhash_item_t* current_item = NULL;
	xhash_item_t* tmp = NULL;
	if (!table || !callback)
		return;
	HASH_ITER(hh, table->ht, current_item, tmp) {
		  callback(current_item->item, arg);
	}
}

void xhash_clear(xhash_t* table)
{
	xhash_item_t* current_item = NULL;
	xhash_item_t* tmp = NULL;

	if (!table)
		return;
	HASH_ITER(hh, table->ht, current_item, tmp) {
		  HASH_DEL(table->ht, current_item);
		  if (table->freefunc)
			  table->freefunc(current_item->item);
		  xfree(current_item);
	}

	table->count = 0;
}

void xhash_free_ptr(xhash_t** table)
{
	if (!table || !*table)
		return;
	xhash_clear(*table);
	xfree(*table);
}
