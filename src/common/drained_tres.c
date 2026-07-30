/*****************************************************************************\
 *  drained_tres.c - track what is drained and why
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include <ctype.h>
#include <limits.h>

#include "src/common/drained_tres.h"

#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/*
 * Ceiling on an index read out of a record, so that a record arriving from
 * another node cannot size the bitmap below. No node has anywhere near this
 * many TRES.
 */
#define MAX_DRAINED_TRES_INDEX (1024 * 1024)

/*
 * State for rendering drained-TRES records into a DrainedTRES string. Indexed
 * records are held in groups until a record arrives that belongs to no group
 * (a different TRES, or one reported by count or UUID), because a TRES
 * drained for an earlier reason can still turn up after one drained later.
 */
typedef struct {
	list_t *groups; /* drain_group_t, in reported order */
	char *name; /* TRES the held groups belong to */
	char *str; /* string built so far */
} drained_tres_fmt_t;

typedef struct {
	buf_t *buffer;
	uint16_t protocol_version;
} pack_drain_count_args_t;

extern void drained_tres_group_delete(void *x)
{
	drain_group_t *group = x;

	if (!group)
		return;

	FREE_NULL_LIST(group->ids);
	FREE_NULL_BITMAP(group->index_bits);
	xfree(group->reason);
	xfree(group);
}

static int _find_drain_group_by_reason(void *x, void *arg)
{
	drain_group_t *group = x;

	return !xstrcmp(group->reason, (char *) arg);
}

static int _find_drain_id(void *x, void *arg)
{
	return !xstrcmp((char *) x, (char *) arg);
}

static int _find_drain_group_by_id(void *x, void *arg)
{
	drain_group_t *group = x;

	if (!group->ids)
		return 0;

	return list_find_first(group->ids, _find_drain_id, arg) ? 1 : 0;
}

extern bool drained_tres_group_empty(drain_group_t *group)
{
	if (group->index_bits && bit_set_count(group->index_bits))
		return false;

	return !group->ids || !list_count(group->ids);
}

extern int drained_tres_sort_groups(void *x, void *y)
{
	drain_group_t *group1 = *(drain_group_t **) x;
	drain_group_t *group2 = *(drain_group_t **) y;
	int first1 = group1->index_bits ? bit_ffs(group1->index_bits) : -1;
	int first2 = group2->index_bits ? bit_ffs(group2->index_bits) : -1;

	/*
	 * Sorting keeps what is reported from the groups independent of the
	 * drain history that produced them, and the reason tie-break keeps it
	 * stable. A group with no indexed TRES has nothing to sort on; put it
	 * last.
	 */
	if (first1 < 0)
		first1 = NO_VAL16;
	if (first2 < 0)
		first2 = NO_VAL16;

	if (first1 != first2)
		return (first1 < first2) ? -1 : 1;

	return xstrcmp(group1->reason, group2->reason);
}

/* Drop the list once nothing is left in it, its no-drain state. */
static void _drain_groups_prune(list_t **groups)
{
	if (*groups && !list_count(*groups))
		FREE_NULL_LIST(*groups);
}

/* Drop an index from the group holding it, and the group once it is empty. */
static int _clear_group_index(void *x, void *arg)
{
	drain_group_t *group = x;
	int inx = *(int *) arg;

	if (!group->index_bits || (inx >= bit_size(group->index_bits)) ||
	    !bit_test(group->index_bits, inx))
		return 0;

	bit_clear(group->index_bits, inx);

	return drained_tres_group_empty(group);
}

extern void drained_tres_group_clear(list_t **groups, int inx)
{
	if (!*groups || (inx < 0))
		return;

	list_delete_all(*groups, _clear_group_index, &inx);

	_drain_groups_prune(groups);
}

extern drain_group_t *drained_tres_group_get(list_t **groups,
					     const char *reason)
{
	drain_group_t *group;

	if (!*groups)
		*groups = list_create(drained_tres_group_delete);

	group = list_find_first(*groups, _find_drain_group_by_reason,
				(char *) reason);
	if (!group) {
		group = xmalloc(sizeof(*group));
		group->reason = xstrdup(reason);
		list_append(*groups, group);
	}

	return group;
}

extern void drained_tres_group_set_in(list_t **groups, drain_group_t *group,
				      int inx, int cnt)
{
	if (!group) {
		drained_tres_group_clear(groups, inx);
		return;
	}

	if ((inx < 0) || (inx >= cnt))
		return;

	if (group->index_bits && (inx < bit_size(group->index_bits)) &&
	    bit_test(group->index_bits, inx))
		return;

	/* A TRES belongs to one group only, so leave the old one first. */
	drained_tres_group_clear(groups, inx);

	if (!group->index_bits)
		group->index_bits = bit_alloc(cnt);
	else if (bit_size(group->index_bits) < cnt)
		bit_realloc(group->index_bits, cnt);
	bit_set(group->index_bits, inx);
}

extern void drained_tres_group_set(list_t **groups, int inx, int cnt,
				   const char *reason)
{
	/* Checked before the group is resolved, so none is left behind empty. */
	if ((inx < 0) || (inx >= cnt))
		return;

	if (!reason || !reason[0]) {
		drained_tres_group_clear(groups, inx);
		return;
	}

	/*
	 * The target group is resolved before the TRES leaves its old one, so
	 * reason may alias the string that old group owns.
	 */
	drained_tres_group_set_in(groups,
				  drained_tres_group_get(groups, reason), inx,
				  cnt);
}

static int _find_drain_group_by_index(void *x, void *arg)
{
	drain_group_t *group = x;
	int inx = *(int *) arg;

	return group->index_bits && (inx < bit_size(group->index_bits)) &&
	       bit_test(group->index_bits, inx);
}

extern char *drained_tres_group_reason(list_t *groups, int inx)
{
	drain_group_t *group;

	if (!groups || (inx < 0))
		return NULL;

	group = list_find_first(groups, _find_drain_group_by_index, &inx);

	return group ? group->reason : NULL;
}

static int _resize_group(void *x, void *arg)
{
	drain_group_t *group = x;
	int cnt = *(int *) arg;

	if (!group->index_bits)
		return 0;

	if (!cnt)
		FREE_NULL_BITMAP(group->index_bits);
	else
		bit_realloc(group->index_bits, cnt);

	return drained_tres_group_empty(group);
}

extern void drained_tres_groups_resize(list_t **groups, int cnt)
{
	if (!*groups)
		return;

	list_delete_all(*groups, _resize_group, &cnt);

	_drain_groups_prune(groups);
}

extern drain_group_t *drained_tres_group_by_id(list_t *groups, const char *id)
{
	if (!groups || !id)
		return NULL;

	return list_find_first(groups, _find_drain_group_by_id, (char *) id);
}

static int _clear_group_id(void *x, void *arg)
{
	drain_group_t *group = x;

	if (!group->ids || !list_delete_first(group->ids, _find_drain_id, arg))
		return 0;

	return drained_tres_group_empty(group);
}

extern void drained_tres_group_id_clear(list_t **groups, const char *id)
{
	if (!*groups || !id)
		return;

	list_delete_all(*groups, _clear_group_id, (char *) id);

	_drain_groups_prune(groups);
}

extern void drained_tres_group_id_set(list_t **groups, const char *id,
				      const char *reason)
{
	drain_group_t *group;

	if (!id)
		return;

	if (!reason || !reason[0]) {
		drained_tres_group_id_clear(groups, id);
		return;
	}

	group = drained_tres_group_get(groups, reason);

	if (group->ids &&
	    list_find_first(group->ids, _find_drain_id, (char *) id))
		return;

	drained_tres_group_id_clear(groups, id);

	if (!group->ids)
		group->ids = list_create(xfree_ptr);
	list_append(group->ids, xstrdup(id));
}

static int _pack_drain_id(void *x, void *arg)
{
	char *id = x;
	buf_t *buffer = arg;

	packstr(id, buffer);

	return 0;
}

static int _pack_drain_group(void *x, void *arg)
{
	drain_group_t *group = x;
	buf_t *buffer = arg;

	pack32(group->ids ? list_count(group->ids) : 0, buffer);
	if (group->ids)
		list_for_each(group->ids, _pack_drain_id, buffer);
	pack_bit_str_hex(group->index_bits, buffer);
	packstr(group->reason, buffer);

	return 0;
}

extern void drained_tres_groups_pack(list_t *groups, uint16_t protocol_version,
				     buf_t *buffer)
{
	if (protocol_version >= SLURM_26_11_PROTOCOL_VERSION) {
		pack32(groups ? list_count(groups) : 0, buffer);

		if (!groups)
			return;

		list_for_each(groups, _pack_drain_group, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
	}
}

extern int drained_tres_groups_unpack(list_t **groups,
				      uint16_t protocol_version, buf_t *buffer)
{
	uint32_t cnt = 0, id_cnt = 0;
	char *id = NULL;

	if (protocol_version >= SLURM_26_11_PROTOCOL_VERSION) {
		safe_unpack32(&cnt, buffer);
		if (!cnt)
			return SLURM_SUCCESS;

		*groups = list_create(drained_tres_group_delete);

		for (uint32_t i = 0; i < cnt; i++) {
			drain_group_t *group = xmalloc(sizeof(*group));

			list_append(*groups, group);
			safe_unpack32(&id_cnt, buffer);
			for (uint32_t j = 0; j < id_cnt; j++) {
				safe_unpackstr(&id, buffer);
				if (!id)
					goto unpack_error;
				if (!group->ids)
					group->ids = list_create(xfree_ptr);
				list_append(group->ids, id);
				id = NULL;
			}
			unpack_bit_str_hex(&group->index_bits, buffer);
			safe_unpackstr(&group->reason, buffer);

			/* A group exists only to carry a reason. */
			if (!group->reason)
				goto unpack_error;

			/*
			 * A group holding nothing carries no TRES, so it can
			 * only have come from a truncated or hand-edited state
			 * file.
			 */
			if (drained_tres_group_empty(group))
				goto unpack_error;
		}
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	xfree(id);
	FREE_NULL_LIST(*groups);
	return SLURM_ERROR;
}

extern void drained_tres_count_clear(drain_count_t *cnt)
{
	if (!cnt)
		return;

	xfree(cnt->name);
	xfree(cnt->reason);
	cnt->count = 0;
}

extern void drained_tres_count_delete(void *x)
{
	drained_tres_count_clear(x);
	xfree(x);
}

extern void drained_tres_count_set(drain_count_t *cnt, uint64_t count,
				   const char *reason, bool replace_reason)
{
	if (!cnt)
		return;

	/*
	 * Zero drops the strings. A list pool is deleted instead of being
	 * left at zero, so this path is the embedded pool.
	 */
	if (!count) {
		xfree(cnt->name);
		xfree(cnt->reason);
		cnt->count = 0;
		return;
	}

	/* A pool that was empty records the reason of this drain. */
	if (!cnt->count)
		replace_reason = true;

	cnt->count = count;

	if (!replace_reason)
		return;
	if (!xstrcmp(cnt->reason, reason))
		return;

	xfree(cnt->reason);
	cnt->reason = xstrdup(reason);
}

static int _find_drain_count(void *x, void *arg)
{
	drain_count_t *cnt = x;

	return !xstrcmp(cnt->name, arg);
}

extern drain_count_t *drained_tres_count_find(list_t *counts, const char *name)
{
	if (!counts)
		return NULL;

	return list_find_first(counts, _find_drain_count, (char *) name);
}

static void _drain_counts_prune(list_t **counts)
{
	if (*counts && !list_count(*counts))
		FREE_NULL_LIST(*counts);
}

extern void drained_tres_count_list_set(list_t **counts, const char *name,
					uint64_t count, const char *reason,
					bool replace_reason)
{
	drain_count_t *cnt;

	if (!counts)
		return;

	cnt = drained_tres_count_find(*counts, name);
	if (!count) {
		if (!cnt)
			return;
		list_delete_ptr(*counts, cnt);
		_drain_counts_prune(counts);
		return;
	}

	if (!cnt) {
		cnt = xmalloc(sizeof(*cnt));
		cnt->name = xstrdup(name);
		if (!*counts)
			*counts = list_create(drained_tres_count_delete);
		list_append(*counts, cnt);
		replace_reason = true;
	}

	drained_tres_count_set(cnt, count, reason, replace_reason);
}

extern void drained_tres_count_pack(drain_count_t *cnt,
				    uint16_t protocol_version, buf_t *buffer)
{
	if (protocol_version >= SLURM_26_11_PROTOCOL_VERSION) {
		pack64(cnt->count, buffer);
		packstr(cnt->name, buffer);
		packstr(cnt->reason, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
	}
}

extern int drained_tres_count_unpack(drain_count_t *cnt,
				     uint16_t protocol_version, buf_t *buffer)
{
	if (!cnt)
		return SLURM_ERROR;

	drained_tres_count_clear(cnt);
	if (protocol_version >= SLURM_26_11_PROTOCOL_VERSION) {
		safe_unpack64(&cnt->count, buffer);
		safe_unpackstr(&cnt->name, buffer);
		safe_unpackstr(&cnt->reason, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	/* A drained count carries a reason. An empty pool carries neither. */
	if (cnt->count && !cnt->reason)
		goto unpack_error;
	if (!cnt->count && (cnt->name || cnt->reason))
		goto unpack_error;

	return SLURM_SUCCESS;

unpack_error:
	drained_tres_count_clear(cnt);
	return SLURM_ERROR;
}

static int _pack_drain_count(void *x, void *arg)
{
	pack_drain_count_args_t *args = arg;

	drained_tres_count_pack(x, args->protocol_version, args->buffer);
	return 0;
}

extern void drained_tres_counts_pack(list_t *counts, uint16_t protocol_version,
				     buf_t *buffer)
{
	pack_drain_count_args_t args = {
		.buffer = buffer,
		.protocol_version = protocol_version,
	};

	if (protocol_version >= SLURM_26_11_PROTOCOL_VERSION) {
		pack32(counts ? list_count(counts) : 0, buffer);

		if (counts)
			list_for_each(counts, _pack_drain_count, &args);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
	}
}

extern int drained_tres_counts_unpack(list_t **counts,
				      uint16_t protocol_version, buf_t *buffer)
{
	uint32_t cnt = 0;

	if (protocol_version >= SLURM_26_11_PROTOCOL_VERSION) {
		safe_unpack32(&cnt, buffer);
		if (!cnt)
			return SLURM_SUCCESS;

		*counts = list_create(drained_tres_count_delete);

		for (uint32_t i = 0; i < cnt; i++) {
			drain_count_t *rec = xmalloc(sizeof(*rec));

			list_append(*counts, rec);
			if (drained_tres_count_unpack(rec, protocol_version,
						      buffer) != SLURM_SUCCESS)
				goto unpack_error;
			/* A list pool exists only while it names a count. */
			if (!rec->name || !rec->count || !rec->reason)
				goto unpack_error;
		}
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	FREE_NULL_LIST(*counts);
	return SLURM_ERROR;
}

extern void drained_tres_rec_add(list_t *recs, const char *tres_name,
				 const char *index, const char *uuid,
				 uint64_t count, const char *reason)
{
	drained_tres_t *rec = xmalloc(sizeof(*rec));

	rec->tres_name = xstrdup(tres_name);
	rec->index = xstrdup(index);
	rec->uuid = xstrdup(uuid);
	rec->count = count;
	rec->reason = xstrdup(reason);

	list_append(recs, rec);
}

extern void drained_tres_delete(void *x)
{
	drained_tres_t *rec = x;

	if (!rec)
		return;

	xfree(rec->index);
	xfree(rec->reason);
	xfree(rec->tres_name);
	xfree(rec->uuid);
	xfree(rec);
}

extern void drained_tres_pack(void *in, uint16_t protocol_version,
			      buf_t *buffer)
{
	drained_tres_t *object = in;

	packbool(object, buffer);
	if (!object)
		return;

	pack64(object->count, buffer);
	packstr(object->index, buffer);
	packstr(object->reason, buffer);
	packstr(object->tres_name, buffer);
	packstr(object->uuid, buffer);
}

extern int drained_tres_unpack(void **out, uint16_t protocol_version,
			       buf_t *buffer)
{
	drained_tres_t *object = NULL;
	bool exists;

	safe_unpackbool(&exists, buffer);
	if (!exists) {
		*out = NULL;
		return SLURM_SUCCESS;
	}

	object = xmalloc(sizeof(*object));
	safe_unpack64(&object->count, buffer);
	safe_unpackstr(&object->index, buffer);
	safe_unpackstr(&object->reason, buffer);
	safe_unpackstr(&object->tres_name, buffer);
	safe_unpackstr(&object->uuid, buffer);

	/* Every record names its TRES; the formatters rely on it. */
	if (!object->tres_name)
		goto unpack_error;

	*out = object;
	return SLURM_SUCCESS;

unpack_error:
	drained_tres_delete(object);
	*out = NULL;
	return SLURM_ERROR;
}

/*
 * The name a drained TRES is reported under: the TRES name without its "gres/"
 * prefix. Indexed entries drop the type as well, because TRES of
 * different types drained for the same reason are reported as one entry and
 * the type cannot then be attributed to any single index.
 */
static char *_drained_tres_fmt_name(const char *tres_name, bool keep_type)
{
	char *name, *sep;

	if (!xstrncmp(tres_name, "gres/", 5))
		name = xstrdup(tres_name + 5);
	else
		name = xstrdup(tres_name);

	if (!keep_type && (sep = xstrchr(name, ':')))
		*sep = '\0';

	return name;
}

extern void drained_tres_index_add(bitstr_t **index, const char *str)
{
	const char *pos = str;

	while (pos && *pos) {
		long first, last;
		char *end;

		if (!isdigit((int) *pos)) {
			pos++;
			continue;
		}

		first = last = strtol(pos, &end, 10);
		if (*end == '-')
			last = strtol(end + 1, &end, 10);
		pos = end;

		if ((first < 0) || (last < first) ||
		    (last >= MAX_DRAINED_TRES_INDEX))
			continue;

		if (!*index)
			*index = bit_alloc(last + 1);
		else if (last >= bit_size(*index))
			bit_realloc(*index, last + 1);

		bit_nset(*index, first, last);
	}
}

static int _foreach_drained_tres_group(void *x, void *arg)
{
	drain_group_t *group = x;
	drained_tres_fmt_t *fmt = arg;
	char *tmp;

	/* An index string with no usable range left no bitmap. */
	if (!group->index_bits)
		return 0;

	tmp = bit_fmt_full(group->index_bits);

	xstrfmtcat(fmt->str, "%s%s(IDX:%s)(%s)", fmt->str ? "," : "", fmt->name,
		   tmp, group->reason);
	xfree(tmp);

	return 0;
}

/* Emit the held groups, if any, and drop what they were held for. */
static void _drained_tres_fmt_flush(drained_tres_fmt_t *fmt)
{
	if (!fmt->groups)
		return;

	(void) list_for_each(fmt->groups, _foreach_drained_tres_group, fmt);

	FREE_NULL_LIST(fmt->groups);
	xfree(fmt->name);
}

static void _drained_tres_fmt_rec(drained_tres_fmt_t *fmt, drained_tres_t *rec)
{
	char *reason = rec->reason ? rec->reason : "no reason";
	drain_group_t *group;
	char *name;

	/*
	 * A count covers every drained TRES of its type at once, so the
	 * type stays on the name and there is nothing to group.
	 */
	if (rec->count != NO_VAL64) {
		_drained_tres_fmt_flush(fmt);
		name = _drained_tres_fmt_name(rec->tres_name, true);
		xstrfmtcat(fmt->str, "%s%s:%" PRIu64 "(%s)",
			   fmt->str ? "," : "", name, rec->count, reason);
		xfree(name);
		return;
	}

	name = _drained_tres_fmt_name(rec->tres_name, false);

	/*
	 * No index. An absent device keeps its saved index and is listed
	 * with the other indexed devices. A record with neither an index
	 * nor a count is not a drained line.
	 */
	if (!rec->index || !rec->index[0]) {
		xfree(name);
		return;
	}

	if (fmt->groups && xstrcmp(fmt->name, name))
		_drained_tres_fmt_flush(fmt);

	if (!fmt->groups)
		fmt->name = name;
	else
		xfree(name);

	group = drained_tres_group_get(&fmt->groups, reason);

	drained_tres_index_add(&group->index_bits, rec->index);
}

static int _foreach_drained_tres_fmt_rec(void *x, void *arg)
{
	_drained_tres_fmt_rec(arg, x);

	return 0;
}

extern char *drained_tres_recs_to_str(list_t *recs)
{
	drained_tres_fmt_t fmt = { 0 };

	if (!recs)
		return NULL;

	(void) list_for_each(recs, _foreach_drained_tres_fmt_rec, &fmt);

	_drained_tres_fmt_flush(&fmt);

	return fmt.str;
}
