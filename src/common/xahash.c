/*****************************************************************************\
 *  xahash.c - functions used for arbitrary hash table
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

/*
 * Fixed size xahash_t is designed to have all operations with a cost of O(1).
 * This requires a good bit of design to achieve the goal.
 *
 * Fixed size hash table: xahash_table_t
 * The fixed count xahash_table_t does 1 xmalloc() call unless there are hash
 * collisions. Then it does 1 xmalloc() per each insert with a hash collision.
 *
 * This is the memory layout of each hash table:
 *
 * |----------------------------|
 * | hash table header          | xahash_table_t* and xahash_table_header_t*
 * |                            | body of xahash_table_header_t
 * |----------------------------|
 * | state blob                 | _get_state()
 * |                            | xahash_table_header_t->state_blob_bytes
 * |----------------------------|
 * | |----------------|         | _get_fentry(index), fentry_header_t*
 * | | entry header   |         |
 * | |----------------| @ index | _get_fentry_blob(fentry_header_t*)
 * | | entry blob     |         |
 * | |----------------|         | _get_fentry(index,depth) + xahash_table_header_t->bytes_per_entry_blob
 * |----------------------------|
 *
 * Each entry fails down into a linked list on a hash collision:
 *
 *                       <-------------- xmalloc() per entry --->
 * |----------------|    |----------------|    |----------------|
 * | entry header   |    | entry header   |    | entry header   |
 * |           next*| -> |           next*| -> |            NULL|
 * |----------------|    |----------------|    |----------------|
 * | entry blob     |    | entry blob     |    | entry blob     |
 * |----------------|    |----------------|    |----------------|
 *
 * Hash collisions are slow and break the O(1) design and should be avoided.
 *
 */

#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/timers.h"
#include "src/common/xahash.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"

/* Macro to only include the code inside of ONLY_DEBUG if !NDEBUG */
#ifndef NDEBUG
#define ONLY_DEBUG(...) __VA_ARGS__
#else
#define ONLY_DEBUG(...)
#endif

#define HASH_TABLE_MAGIC 0x131e1aff
#define HASH_FENTRY_MAGIC(index) (((uint32_t) 0xffff0000) ^ ((uint32_t) index))

typedef enum {
	HT_FLAG_STATE_MASK = 0xff,
	HT_FLAG_INVALID = 0,
	HT_FLAG_FIXED_SIZE = SLURM_BIT(0), /* hash table is expected to be fixed and use linked list of entries for hash collisions */
	HT_FLAG_DYNAMIC_SIZE = SLURM_BIT(1), /* hash table is expected to resize as needed */
	HT_FLAG_INVALID_MAX,
} hash_flags_t;

typedef struct {
	ONLY_DEBUG(int magic); /* HASH_TABLE_MAGIC  */
	hash_flags_t flags;
	xahash_func_t hash_func;
	xahash_match_func_t match_func;
	const char *match_func_string;
	xahash_on_insert_func_t on_insert_func;
	const char *on_insert_func_string;
	xahash_on_free_func_t on_free_func;
	const char *on_free_func_string;
	size_t state_blob_bytes;
	size_t bytes_per_entry_blob;
	union {
		struct {
			size_t count;
		} fixed;
	} type;
} xahash_table_header_t;

typedef enum {
	FENTRY_FLAG_INVALID = 0,
	FENTRY_FLAG_UNSET = SLURM_BIT(0), /* entry is not populated */
	FENTRY_FLAG_SET = SLURM_BIT(1), /* entry is populated */
	FENTRY_FLAG_INVALID_MAX
} fentry_flags_t;

/* Fixed size entry struct */
typedef struct fentry_header_s fentry_header_t;
struct fentry_header_s {
	ONLY_DEBUG(int magic); /* HASH_FENTRY_MAGIC % index */
	fentry_flags_t flags;
	/* next entry in linked list */
	fentry_header_t *next;
};

static const struct {
	const char *str;
	xahash_foreach_control_t value;
} foreach_control_strings[] = {
	{ "INVALID", XAHASH_FOREACH_INVALID },
	{ "CONTINUE", XAHASH_FOREACH_CONT },
	{ "STOP", XAHASH_FOREACH_STOP },
	{ "FAIL", XAHASH_FOREACH_FAIL },
	{ "INVALID", XAHASH_FOREACH_INVALID_MAX },
};

static void *_get_state(const xahash_table_t *ht);
static fentry_header_t *_get_fentry(xahash_table_t *ht,
				    xahash_table_header_t *hth, int index);

/* Macro place holders for sanity checks */
#define _is_debug() (false)
#define DEF_DEBUG_TIMER
#define START_DEBUG_TIMER
#define END_DEBUG_TIMER
#define _check_magic(...)
#define _check_fentry_bounds(...)
#define _check_fentry_magic(...)

static bool _is_fixed(const xahash_table_t *ht,
		      const xahash_table_header_t *hth)
{
	_check_magic(ht);

	return ((hth->flags & HT_FLAG_STATE_MASK) == HT_FLAG_FIXED_SIZE);
}

static int _fixed_hash_to_index(const xahash_table_t *ht,
				const xahash_table_header_t *hth,
				const xahash_hash_t hash)
{
	xassert(_is_fixed(ht, hth));

	return hash % hth->type.fixed.count;
}

static void *_get_state(const xahash_table_t *ht)
{
	return ((void *) ht) + sizeof(xahash_table_header_t);
}

static xahash_table_header_t *_get_header(xahash_table_t *ht)
{
	xahash_table_header_t *hth = (void *) ht;

	xassert(hth->magic == HASH_TABLE_MAGIC);

	return hth;
}

static const xahash_table_header_t *_get_header_const(const xahash_table_t *ht)
{
	return _get_header((xahash_table_t *) ht);
}

/*
 * Get pointer to entry based on index
 * WARNING: magic is not checked
 * WARNING: entry may not be set
 */
static fentry_header_t *_get_fentry(xahash_table_t *ht,
				    xahash_table_header_t *hth, int index)
{
	fentry_header_t *fe;
	void *start_ht = ht;
	void *end_hth = start_ht + sizeof(*hth);
	void *end_state_blob = end_hth + hth->state_blob_bytes;
	const size_t bytes_per_fentry =
		(sizeof(fentry_header_t) + hth->bytes_per_entry_blob);

	xassert(index >= 0);
	xassert(index < hth->type.fixed.count);

	fe = end_state_blob + (bytes_per_fentry * index);

	_check_fentry_bounds(ht, hth, fe);

	return fe;
}

static bool _is_fentry_set(const xahash_table_t *ht,
			   const xahash_table_header_t *hth,
			   const fentry_header_t *fe)
{
	_check_fentry_magic(ht, hth, fe, -1, -1);

	return (fe->flags & FENTRY_FLAG_SET);
}

/* WARNING: does not call _check_fentry_magic() */
static void *_get_fentry_blob_ptr(const xahash_table_t *ht,
				  const xahash_table_header_t *hth,
				  const fentry_header_t *fe)
{
	void *ptr;

	_check_fentry_magic(ht, hth, fe, -1, -1);

	ptr = ((void *) fe) + sizeof(*fe);

	/* verify pointer is in bounds of the ht's malloc() */
	xassert(ptr);
	xassert((void *) ptr > (void *) fe);
	xassert((void *) ptr >= ((void *) fe) + sizeof(*fe));

	return ptr;
}

static void *_get_fentry_blob(const xahash_table_t *ht,
			      const xahash_table_header_t *hth,
			      const fentry_header_t *fe)
{
	void *blob;

	xassert(fe);
	_check_fentry_magic(ht, hth, fe, -1, -1);
	xassert(_is_fentry_set(ht, hth, fe));

	if (!fe || !_is_fentry_set(ht, hth, fe))
		return NULL;

	blob = _get_fentry_blob_ptr(ht, hth, fe);

	xassert(blob);

	return blob;
}

static fentry_header_t *_init_fentry(xahash_table_t *ht,
				     xahash_table_header_t *hth,
				     fentry_header_t *fe, bool is_new,
				     const int index, const int depth)
{
	if (is_new) {
		log_flag(DATA, "%s: [hashtable@0x%"PRIxPTR"] initializing fentry[%d][%d]@0x%"PRIxPTR,
			 __func__, (uintptr_t) ht, index, depth,
			 (uintptr_t) fe);
	} else {
		_check_fentry_magic(ht, hth, fe, index, depth);
		log_flag(DATA, "%s: [hashtable@0x%"PRIxPTR"] reinitializing fentry[%d][%d]@0x%"PRIxPTR,
			 __func__, (uintptr_t) ht, index, depth,
			 (uintptr_t) fe);
	}

	*fe = (fentry_header_t) {
		ONLY_DEBUG(.magic = HASH_FENTRY_MAGIC(index),)
		.flags = FENTRY_FLAG_UNSET,
	};

	_check_fentry_magic(ht, hth, fe, index, depth);

	ONLY_DEBUG(memset(_get_fentry_blob_ptr(ht, hth, fe), 0,
			  hth->bytes_per_entry_blob));

	_check_fentry_magic(ht, hth, fe, index, depth);

	return fe;
}

static xahash_table_t *_new_fixed_table(xahash_func_t hash_func,
					const char *hash_func_string,
					xahash_match_func_t match_func,
					const char *match_func_string,
					xahash_on_insert_func_t on_insert_func,
					const char *on_insert_func_string,
					xahash_on_free_func_t on_free_func,
					const char *on_free_func_string,
					const size_t state_bytes,
					const size_t bytes_per_entry,
					const size_t fixed_table_size)
{
	size_t bytes;
	xahash_table_t *ht;
	xahash_table_header_t *hth;

	xassert(fixed_table_size > 0);
	xassert(bytes_per_entry > 0);

	bytes = sizeof(*hth) + state_bytes +
		((sizeof(fentry_header_t) + bytes_per_entry) *
		 fixed_table_size);

	log_flag(DATA, "%s: initializing fixed xahash_table_t with fixed %zu entries and %zu bytes per entry and %zu state bytes for %zu bytes total. Callbacks: hash_func=%s()@0x%"PRIxPTR" match_func=%s()@0x%"PRIxPTR" on_insert_func=%s()@0x%"PRIxPTR" on_free_func=%s()@0x%"PRIxPTR,
		__func__, fixed_table_size, bytes_per_entry, state_bytes, bytes,
		hash_func_string, (uintptr_t) hash_func, match_func_string,
		(uintptr_t) match_func, on_insert_func_string,
		(uintptr_t) on_insert_func, on_free_func_string,
		(uintptr_t) on_free_func);

	hth = ht = xmalloc_nz(bytes);
	*hth = (xahash_table_header_t){
		ONLY_DEBUG(.magic = HASH_TABLE_MAGIC,)
		.flags = HT_FLAG_FIXED_SIZE,
		.hash_func = hash_func,
		.match_func = match_func,
		.match_func_string = match_func_string,
		.on_insert_func = on_insert_func,
		.on_insert_func_string = on_insert_func_string,
		.on_free_func = on_free_func,
		.on_free_func_string = on_free_func_string,
		.state_blob_bytes = state_bytes,
		.bytes_per_entry_blob = bytes_per_entry,
		.type.fixed = {
			.count = fixed_table_size,
		}
	};

	for (int i = 0; i < hth->type.fixed.count; i++)
		(void) _init_fentry(ht, hth, _get_fentry(ht, hth, i), true, i,
				    0);

	ONLY_DEBUG(memset(_get_state(ht), 0, state_bytes));

	return ht;
}

extern xahash_table_t *xahash_new_table_funcname(
	xahash_func_t hash_func, const char *hash_func_string,
	xahash_match_func_t match_func, const char *match_func_string,
	xahash_on_insert_func_t on_insert_func,
	const char *on_insert_func_string, xahash_on_free_func_t on_free_func,
	const char *on_free_func_string, const size_t state_bytes,
	const size_t bytes_per_entry, const size_t fixed_table_size)
{
	if (fixed_table_size > 0)
		return _new_fixed_table(hash_func, hash_func_string, match_func,
					match_func_string, on_insert_func,
					on_insert_func_string, on_free_func,
					on_free_func_string, state_bytes,
					bytes_per_entry, fixed_table_size);

	/* TODO: dynamic sizing not implmented yet */
	fatal_abort("should never execute");
}

static void _free_fentry(xahash_table_t *ht, xahash_table_header_t *hth,
			 int index, int depth, fentry_header_t *fe,
			 fentry_header_t *parent)
{
	fentry_header_t *next = fe->next;

	if (next) {
		_check_fentry_magic(ht, hth, fe->next, index, (depth + 1));
		xassert(parent != next);
		xassert(next != fe);
	}

	if (hth->on_free_func && _is_fentry_set(ht, hth, fe)) {
		log_flag_hex(DATA, _get_fentry_blob_ptr(ht, hth, fe),
			     hth->bytes_per_entry_blob,
			     "%s: [hashtable@0x%"PRIxPTR"] calling %s()@0x%"PRIxPTR" for fentry[%d][%d]@0x%"PRIxPTR,
			     __func__, (uintptr_t) ht, hth->on_free_func_string,
			     (uintptr_t) hth->on_free_func, index, depth,
			     (uintptr_t) fe);

		hth->on_free_func(_get_fentry_blob(ht, hth, fe),
				  _get_state(ht));
	}

	if (parent) {
		log_flag_hex(DATA, _get_fentry_blob_ptr(ht, hth, fe),
			     hth->bytes_per_entry_blob,
			     "%s: [hashtable@0x%"PRIxPTR"] dropping linked fentry[%d][%d]@0x%"PRIxPTR" -> fentry[%d][%d]@0x%"PRIxPTR,
			     __func__, (uintptr_t) ht, index, (depth - 1),
			     (uintptr_t) parent, index, depth, (uintptr_t) fe);

		xassert(parent->next == fe);
		xassert(depth > 0);
		_check_fentry_magic(ht, hth, parent, index, (depth - 1));

		/* unlink fe from list */
		parent->next = next;

		ONLY_DEBUG(fe->magic = ~HASH_FENTRY_MAGIC(index));
		xfree(fe);

		_check_fentry_magic(ht, hth, parent, index, (depth - 1));
		if (next)
			_check_fentry_magic(ht, hth, next, index, depth);
	} else {
		xassert(!depth);

		log_flag_hex(DATA, _get_fentry_blob_ptr(ht, hth, fe),
			     hth->bytes_per_entry_blob,
			     "%s: [hashtable@0x%"PRIxPTR"] releasing fentry[%d][%d]@0x%"PRIxPTR,
			     __func__, (uintptr_t) ht, index, depth,
			     (uintptr_t) fe);

		(void) _init_fentry(ht, hth, fe, false, index, depth);
		fe->next = next;
	}
}

static void _free_fixed_table_members(xahash_table_t *ht,
				      xahash_table_header_t *hth)
{
	for (int i = 0; i < hth->type.fixed.count; i++) {
		fentry_header_t *fe = _get_fentry(ht, hth, i);

		_check_fentry_magic(ht, hth, fe, i, 0);

		while (fe->next) {
			_check_fentry_magic(ht, hth, fe, i, 1);
			_free_fentry(ht, hth, i, 1, fe->next, fe);
		}

		_free_fentry(ht, hth, i, 0, fe, NULL);
	}
}

extern void xahash_free_table(xahash_table_t *ht)
{
	xahash_table_header_t *hth;

	_check_magic(ht);
	if (!ht)
		return;

	hth = _get_header(ht);

	log_flag(DATA, "%s: [hashtable@0x%"PRIxPTR"] request free hashtable",
		 __func__, (uintptr_t) ht);

	if (_is_fixed(ht, hth))
		_free_fixed_table_members(ht, hth);

	ONLY_DEBUG(hth->magic = ~HASH_TABLE_MAGIC);
	xfree(ht);
}

extern void *xahash_get_state_ptr(xahash_table_t *ht)
{
	DEF_DEBUG_TIMER;
	xahash_table_header_t *hth = (void *) ht;
	void *ptr;

	START_DEBUG_TIMER;
	_check_magic(ht);

	ptr = _get_state(ht);

	log_flag(DATA, "%s: [hashtable@0x%"PRIxPTR"] request table state=0x%"PRIxPTR"[%zu]",
		 __func__, (uintptr_t) ht, (uintptr_t) ptr,
		 hth->state_blob_bytes);

	END_DEBUG_TIMER;
	return ptr;
}

static bool _match_fixed_entry(xahash_table_t *ht, xahash_table_header_t *hth,
			       const xahash_hash_t hash, const void *key,
			       const size_t key_bytes, fentry_header_t *fe,
			       const int index, const int depth)
{
	_check_fentry_magic(ht, hth, fe, index, depth);

	if (!_is_fentry_set(ht, hth, fe)) {
		log_flag(DATA, "%s: [hashtable@0x%"PRIxPTR"] skip unset fentry[%d][%d]@0x%"PRIxPTR" != 0x%"PRIxPTR"[%zu]=#0x%x",
				 __func__, (uintptr_t) ht, index, depth,
				 (uintptr_t) fe, (uintptr_t) key, key_bytes,
				 hash);
		return false;
	}

	if (hth->match_func(_get_fentry_blob(ht, hth, fe), key, key_bytes,
			    _get_state(ht))) {
		log_flag(DATA, "%s: [hashtable@0x%"PRIxPTR"] %s()@0x%"PRIxPTR"=true accepted fentry[%d][%d]@0x%"PRIxPTR" == 0x%"PRIxPTR"[%zu]=#0x%x",
			 __func__, (uintptr_t) ht, hth->match_func_string,
			 (uintptr_t) hth->match_func, index, depth,
			 (uintptr_t) fe, (uintptr_t) key, key_bytes, hash);
		return true;
	} else {
		log_flag(DATA, "%s: [hashtable@0x%"PRIxPTR"] %s()@0x%"PRIxPTR"=false rejected fentry[%d][%d]@0x%"PRIxPTR" != 0x%"PRIxPTR"[%zu]=#0x%x",
			 __func__, (uintptr_t) ht, hth->match_func_string,
			 (uintptr_t) hth->match_func, index, depth,
			 (uintptr_t) fe, (uintptr_t) key, key_bytes, hash);
		return false;
	}
}

static fentry_header_t *_find_fixed_entry(xahash_table_t *ht,
					  xahash_table_header_t *hth,
					  const xahash_hash_t hash,
					  const void *key,
					  const size_t key_bytes)
{
	const int index = _fixed_hash_to_index(ht, hth, hash);
	fentry_header_t *fe = _get_fentry(ht, hth, index);
	int depth = 0;

	_check_fentry_magic(ht, hth, fe, index, depth);

	do {
		if (_match_fixed_entry(ht, hth, hash, key, key_bytes, fe, index,
				       depth))
			return fe;

		depth++;
	} while ((fe = fe->next));

	log_flag(DATA, "%s: [hashtable@0x%"PRIxPTR"] unable to find fentry for 0x%"PRIxPTR"[%zu]=#0x%x",
			 __func__, (uintptr_t) ht, (uintptr_t) key, key_bytes,
			 hash);

	return NULL;
}

static const fentry_header_t *_find_fixed_entry_const(
	const xahash_table_t *ht, const xahash_table_header_t *hth,
	const void *key, const size_t key_bytes)
{
	const xahash_hash_t hash =
		hth->hash_func(key, key_bytes, _get_state(ht));

	/* de-constify and then return as const */

	return _find_fixed_entry((xahash_table_t *) ht,
				 (xahash_table_header_t *) hth, hash, key,
				 key_bytes);
}

static void *_find_fixed_entry_blob(const xahash_table_t *ht,
				    const xahash_table_header_t *hth,
				    const void *key, const size_t key_bytes)
{
	const fentry_header_t *fe =
		_find_fixed_entry_const(ht, hth, key, key_bytes);

	if (!fe || !_is_fentry_set(ht, hth, fe))
		return NULL;

	return _get_fentry_blob(ht, hth, fe);
}

extern void *xahash_find_entry(const xahash_table_t *ht, const void *key,
			       const size_t key_bytes)
{
	DEF_DEBUG_TIMER;
	const xahash_table_header_t *hth;
	void *ptr = NULL;

	_check_magic(ht);

	if (!ht || !key || !key_bytes)
		return NULL;

	START_DEBUG_TIMER;
	hth = _get_header_const(ht);

	log_flag(DATA, "%s: [hashtable@0x%"PRIxPTR"] request find entry for 0x%"PRIxPTR"[%zu]=#0x%x",
		 __func__, (uintptr_t) ht, (uintptr_t) key, key_bytes,
		 hth->hash_func(key, key_bytes, _get_state(ht)));

	if (_is_fixed(ht, hth))
		ptr = _find_fixed_entry_blob(ht, hth, key, key_bytes);
	else
		fatal_abort("should never execute");

	END_DEBUG_TIMER;
	return ptr;
}

static fentry_header_t *_append_fentry(xahash_table_t *ht,
				       xahash_table_header_t *hth,
				       fentry_header_t *fe,
				       const xahash_hash_t hash,
				       const int index, int *depth_ptr)
{
	xassert(_is_fentry_set(ht, hth, fe));

	/* Find unset entry if one is already in chain */
	while (fe) {
		if (!_is_fentry_set(ht, hth, fe)) {
			/* found empty entry so return it instead */
			return fe;
		}

		if (!fe->next) {
			size_t bytes = sizeof(*fe);
			bytes += hth->bytes_per_entry_blob;
			fe->next = xmalloc_nz(bytes);

			log_flag(DATA, "%s: [hashtable@0x%"PRIxPTR"] new linked fentry[%d][%d]@0x%"PRIxPTR" -> fentry[%d][%d]@0x%"PRIxPTR"=#0x%x",
				 __func__, (uintptr_t) ht, index, *depth_ptr,
				 (uintptr_t) fe, index, (*depth_ptr + 1),
				 (uintptr_t) fe->next, hash);

			(*depth_ptr)++;
			return _init_fentry(ht, hth, fe->next, true, index,
					    *depth_ptr);
		}

		fe = fe->next;
		(*depth_ptr)++;
	}

	fatal_abort("should never execute");
}

static void *_insert_fixed_entry(xahash_table_t *ht, xahash_table_header_t *hth,
				 const void *key, const size_t key_bytes)
{
	int depth = -1, index = -1;
	const xahash_hash_t hash =
		hth->hash_func(key, key_bytes, _get_state(ht));
	fentry_header_t *fe = _find_fixed_entry(ht, hth, hash, key, key_bytes);

	if (fe) {
		xassert(_is_fentry_set(ht, hth, fe));

		log_flag_hex(DATA, _get_fentry_blob_ptr(ht, hth, fe),
			     hth->bytes_per_entry_blob,
			     "%s: [hashtable@0x%"PRIxPTR"] ignoring duplicate insert on existing fentry@0x%"PRIxPTR,
			     __func__, (uintptr_t) ht, (uintptr_t) fe);

		return _get_fentry_blob(ht, hth, fe);
	}

	/* not found: find and place a new entry */
	index = _fixed_hash_to_index(ht, hth, hash);
	depth = 0;
	fe = _get_fentry(ht, hth, index);
	_check_fentry_magic(ht, hth, fe, index, depth);

	if (_is_fentry_set(ht, hth, fe)) {
		/* need to add to linked list */
		fe = _append_fentry(ht, hth, fe, hash, index, &depth);
	}

	/* check for clobbering */
	xassert(fe->flags == FENTRY_FLAG_UNSET);

	fe->flags = FENTRY_FLAG_SET;

	if (hth->on_insert_func) {
		hth->on_insert_func(_get_fentry_blob(ht, hth, fe), key,
				    key_bytes, _get_state(ht));

		log_flag_hex(DATA, _get_fentry_blob_ptr(ht, hth, fe),
			     hth->bytes_per_entry_blob,
			     "%s: [hashtable@0x%"PRIxPTR"] inserted after %s()@0x%"PRIxPTR" for fentry[%d][%d]@0x%"PRIxPTR"=#0x%x",
			     __func__, (uintptr_t) ht,
			     hth->on_insert_func_string,
			     (uintptr_t) hth->on_insert_func, index, depth,
			     (uintptr_t) fe, hash);
	} else {
		log_flag(DATA,
			 "%s: [hashtable@0x%" PRIxPTR
			 "] inserted fentry[%d][%d]@0x%" PRIxPTR "=#0x%x",
			 __func__, (uintptr_t) ht, index, depth, (uintptr_t) fe,
			 hash);
	}

	return _get_fentry_blob(ht, hth, fe);
}

extern void *xahash_insert_entry(xahash_table_t *ht, const void *key,
				 const size_t key_bytes)
{
	DEF_DEBUG_TIMER;
	xahash_table_header_t *hth;
	void *ptr = NULL;

	xassert(key);
	xassert(key_bytes > 0);
	_check_magic(ht);
	if (!ht || !key || !key_bytes)
		return NULL;

	START_DEBUG_TIMER;
	hth = _get_header(ht);

	log_flag(DATA, "%s: [hashtable@0x%"PRIxPTR"] request insert entry for 0x%"PRIxPTR"[%zu]=#0x%x",
		 __func__, (uintptr_t) ht, (uintptr_t) key, key_bytes,
		 hth->hash_func(key, key_bytes, _get_state(ht)));

	if (_is_fixed(ht, hth))
		ptr = _insert_fixed_entry(ht, hth, key, key_bytes);
	else
	fatal_abort("should never execute");

	END_DEBUG_TIMER;

	return ptr;
}

static bool _find_and_free_fentry(xahash_table_t *ht,
				  xahash_table_header_t *hth, const void *key,
				  const size_t key_bytes)
{
	const xahash_hash_t hash =
		hth->hash_func(key, key_bytes, _get_state(ht));
	const int index = _fixed_hash_to_index(ht, hth, hash);
	fentry_header_t *fe = _get_fentry(ht, hth, index);
	fentry_header_t *parent = NULL;
	int depth = 0;

	_check_fentry_magic(ht, hth, fe, index, -1);

	while (fe) {
		if (_is_fentry_set(ht, hth, fe)) {
			if (hth->match_func(_get_fentry_blob(ht, hth, fe), key,
					    key_bytes,  _get_state(ht))) {
				log_flag(DATA, "%s: [hashtable@0x%"PRIxPTR"] matched fentry[%d][%d]@0x%"PRIxPTR" == 0x%"PRIxPTR"[%zu]=#0x%x",
					 __func__, (uintptr_t) ht, index, depth,
					 (uintptr_t) fe, (uintptr_t) key,
					 key_bytes, hash);
				break;
			} else {
				log_flag(DATA, "%s: [hashtable@0x%"PRIxPTR"] match_func rejected fentry[%d][%d]@0x%"PRIxPTR" != 0x%"PRIxPTR"[%zu]=#0x%x",
					 __func__, (uintptr_t) ht, index, depth,
					 (uintptr_t) fe, (uintptr_t) key,
					 key_bytes, hash);
			}
		}

		parent = fe;
		fe = fe->next;
		depth++;
	}

	if (!fe)
		return false;

	_free_fentry(ht, hth, index, depth, fe, parent);
	return true;
}

extern bool xahash_free_entry(xahash_table_t *ht, const void *key,
			      const size_t key_bytes)
{
	DEF_DEBUG_TIMER;
	xahash_table_header_t *hth;
	bool rc = false;

	_check_magic(ht);
	xassert(key);
	xassert(key_bytes > 0);
	if (!ht || !key || !key_bytes)
		return false;

	START_DEBUG_TIMER;
	hth = _get_header(ht);

	log_flag(DATA, "%s: [hashtable@0x%"PRIxPTR"] request free entry for 0x%"PRIxPTR"[%zu]=#0x%x",
		 __func__, (uintptr_t) ht, (uintptr_t) key, key_bytes,
		 hth->hash_func(key, key_bytes, _get_state(ht)));

	if (_is_fixed(ht, hth)) {
		rc = _find_and_free_fentry(ht, hth, key, key_bytes);
	} else {
		fatal_abort("should never execute");
	}

	END_DEBUG_TIMER;
	return rc;
}

static const char *_foreach_control_string(xahash_foreach_control_t control)
{
	for (int i = 0; i < ARRAY_SIZE(foreach_control_strings); i++)
		if (foreach_control_strings[i].value == control)
			return foreach_control_strings[i].str;

	fatal_abort("should never execute");
}

static xahash_foreach_control_t _foreach_fentry_entry(
	xahash_table_t *ht, xahash_table_header_t *hth,
	xahash_foreach_func_t callback, const char *callback_string, void *arg,
	fentry_header_t *fe, const int index, const int depth)
{
	xahash_foreach_control_t rc;

	rc = callback(_get_fentry_blob(ht, hth, fe), _get_state(ht), arg);

	log_flag(DATA, "%s: [hashtable@0x%"PRIxPTR"] called after %s()@0x%"PRIxPTR"=%s for fentry[%d][%d]@0x%"PRIxPTR,
		 __func__, (uintptr_t) ht, callback_string,
		 (uintptr_t) callback, _foreach_control_string(rc), index,
		 depth, (uintptr_t) fe);

	return rc;
}

static int _foreach_fentry(xahash_table_t *ht, xahash_table_header_t *hth,
			   xahash_foreach_func_t callback,
			   const char *callback_string, void *arg)
{
	uint32_t count = 0;

	for (uint32_t i = 0; i < hth->type.fixed.count; i++) {
		uint32_t depth = 0;
		fentry_header_t *fe = _get_fentry(ht, hth, i);

		do {
			xahash_foreach_control_t rc;

			if (!_is_fentry_set(ht, hth, fe))
				continue;

			count++;

			rc = _foreach_fentry_entry(ht, hth, callback,
						   callback_string, arg, fe, i,
						   depth);

			xassert(rc > XAHASH_FOREACH_INVALID);
			xassert(rc < XAHASH_FOREACH_INVALID_MAX);

			switch (rc) {
			case XAHASH_FOREACH_CONT:
				/* do nothing */
				break;
			case XAHASH_FOREACH_STOP:
				return count;
			case XAHASH_FOREACH_FAIL:
				return (count * -1);
			case XAHASH_FOREACH_INVALID:
			case XAHASH_FOREACH_INVALID_MAX:
				fatal_abort("should never execute");
			}
		} while (depth++, (fe = fe->next));
	}

	return count;
}

extern int xahash_foreach_entry_funcname(xahash_table_t *ht,
					 xahash_foreach_func_t callback,
					 const char *callback_string, void *arg)
{
	DEF_DEBUG_TIMER;
	xahash_table_header_t *hth;
	int rc = 0;

	_check_magic(ht);
	if (!ht)
		return rc;

	START_DEBUG_TIMER;
	hth = _get_header(ht);

	log_flag(DATA, "%s: [hashtable@0x%"PRIxPTR"] request foreach func:%s()@0x%"PRIxPTR" arg:0x%"PRIxPTR,
		 __func__, (uintptr_t) ht, callback_string,
		 (uintptr_t) callback, (uintptr_t) arg);

	if (_is_fixed(ht, hth))
		rc = _foreach_fentry(ht, hth, callback, callback_string, arg);
	else
		fatal_abort("should never execute");

	END_DEBUG_TIMER;
	return rc;
}
