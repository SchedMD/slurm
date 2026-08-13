/*****************************************************************************\
 *  drained_tres.h - track what is drained and why
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

#ifndef _HAVE_DRAINED_TRES_H
#define _HAVE_DRAINED_TRES_H

#include <stdbool.h>

#include "src/common/bitstring.h"
#include "src/common/list.h"
#include "src/common/pack.h"

/*
 * One set of TRES drained for the same reason. Everything that can be
 * drained keeps a list of these rather than a reason per TRES: the count
 * of distinct reasons is what actually varies, and it is normally one.
 *
 * A TRES is named by index, or by string where it has an identity that
 * outlives its index. A group can hold both: a drained GRES device is tracked
 * by bit and by UUID, and keeping the two in one group is what stores its
 * reason once rather than twice.
 */
typedef struct {
	char *reason; /* why these TRES were drained */
	bitstr_t *index_bits; /* index-named TRES, or NULL */
	list_t *ids; /* string-named TRES, or NULL */
} drain_group_t;

/*
 * How many of a count-only TRES are drained, and why. name is the GRES type,
 * or NULL for an untyped GRES and for memory. count 0 means nothing is
 * drained and reason is NULL.
 */
typedef struct {
	char *name;
	uint64_t count;
	char *reason;
} drain_count_t;

/*
 * list_create() destructor for one drain_group_t.
 * IN x - drain_group_t to free
 */
extern void drained_tres_group_delete(void *x);

/*
 * True when the group holds no TRES of any kind and can be discarded.
 * IN group - group to test
 * RET true when the group is empty
 */
extern bool drained_tres_group_empty(drain_group_t *group);

/*
 * Order groups by their lowest indexed TRES, then by reason.
 * IN x - first drain_group_t
 * IN y - second drain_group_t
 * RET list sort order, negative when x sorts before y
 */
extern int drained_tres_sort_groups(void *x, void *y);

/*
 * drained_tres_group_get - the group in *groups holding reason, appending one
 *	if this is the first TRES drained for it. Creates the list as needed.
 * IN/OUT groups - group list, created when NULL
 * IN reason - reason the group is keyed by
 * RET the group, owned by the list
 */
extern drain_group_t *drained_tres_group_get(list_t **groups,
					     const char *reason);

/*
 * drained_tres_group_set - record that the TRES at index inx of an index space
 *	cnt TRES units wide was drained for reason, moving it out of whatever
 *	group previously held it. A TRES drained without a reason is left
 *	in no group, which is how every reader already reads "no reason".
 * IN/OUT groups - group list, created when NULL
 * IN inx - index of the TRES to move
 * IN cnt - width of the index space
 * IN reason - why it is drained, or NULL for no reason
 */
extern void drained_tres_group_set(list_t **groups, int inx, int cnt,
				   const char *reason);

/*
 * As drained_tres_group_set(), for a caller that already resolved the group.
 * IN/OUT groups - group list the TRES is removed from
 * IN group - group that receives the TRES
 * IN inx - index of the TRES to move
 * IN cnt - width of the index space
 */
extern void drained_tres_group_set_in(list_t **groups, drain_group_t *group,
				      int inx, int cnt);

/*
 * drained_tres_group_clear - drop the TRES at index inx from whichever group
 *	holds it, discarding a group that is left holding nothing.
 * IN/OUT groups - group list
 * IN inx - index of the TRES to drop
 */
extern void drained_tres_group_clear(list_t **groups, int inx);

/*
 * drained_tres_group_reason - the reason the TRES at index inx was drained, or
 *	NULL when it is not drained or was drained without one. The string is
 *	owned by the group; the caller borrows it.
 * IN groups - group list, may be NULL
 * IN inx - index to look up
 * RET borrowed reason, or NULL
 */
extern char *drained_tres_group_reason(list_t *groups, int inx);

/*
 * drained_tres_groups_resize - resize every group's bitmap to cnt, discarding
 *	groups that are left holding nothing. A group still holding a
 *	string-named TRES survives a resize that empties its bitmap. Used when
 *	a TRES unit count and the restored drain state that covers it disagree.
 * IN/OUT groups - group list
 * IN cnt - new bitmap width
 */
extern void drained_tres_groups_resize(list_t **groups, int cnt);

/*
 * drained_tres_group_by_id - the group holding string-named TRES id, or NULL.
 * IN groups - group list, may be NULL
 * IN id - string name to look up
 * RET the group, or NULL. The caller borrows it.
 */
extern drain_group_t *drained_tres_group_by_id(list_t *groups, const char *id);

/*
 * drained_tres_group_id_set - as drained_tres_group_set(), for a
 *	string-named TRES.
 * IN/OUT groups - group list, created when NULL
 * IN id - string name of the TRES
 * IN reason - why it is drained
 */
extern void drained_tres_group_id_set(list_t **groups, const char *id,
				      const char *reason);

/*
 * drained_tres_group_id_clear - drained_tres_group_clear() for a
 *	string-named TRES.
 * IN/OUT groups - group list
 * IN id - string name of the TRES to drop
 */
extern void drained_tres_group_id_clear(list_t **groups, const char *id);

/*
 * drained_tres_groups_pack/unpack - the group list as a count followed by each
 *	group in struct order: string-named TRES, index-named TRES, reason.
 *	Unpack leaves *groups NULL when the list is empty.
 * IN groups - group list to pack, may be NULL
 * IN protocol_version - version to pack for
 * IN buffer - buffer to pack into
 */
extern void drained_tres_groups_pack(list_t *groups, uint16_t protocol_version,
				     buf_t *buffer);

/*
 * Unpack a group list packed by drained_tres_groups_pack().
 * OUT groups - restored list, NULL when the list is empty
 * IN protocol_version - version to unpack
 * IN buffer - buffer to unpack from
 * RET SLURM_SUCCESS or an error
 */
extern int drained_tres_groups_unpack(list_t **groups,
				      uint16_t protocol_version, buf_t *buffer);

/*
 * Free the strings in an embedded drain_count_t and zero its count.
 * IN/OUT cnt - pool to clear
 */
extern void drained_tres_count_clear(drain_count_t *cnt);

/*
 * list_create() destructor for one heap drain_count_t.
 * IN x - drain_count_t to free
 */
extern void drained_tres_count_delete(void *x);

/*
 * drained_tres_count_set - set an embedded pool's count. A count of 0 clears
 *	it. A pool that was empty records reason. replace_reason replaces the
 *	reason of a pool that already holds a count.
 * IN/OUT cnt - pool to set
 * IN count - drained count, 0 to clear the pool
 * IN reason - why it is drained
 * IN replace_reason - replace the reason of a pool that already has a count
 */
extern void drained_tres_count_set(drain_count_t *cnt, uint64_t count,
				   const char *reason, bool replace_reason);

/*
 * The pool named name, or NULL. The caller borrows it.
 * IN counts - pool list, may be NULL
 * IN name - pool name to find
 * RET the pool, or NULL
 */
extern drain_count_t *drained_tres_count_find(list_t *counts, const char *name);

/*
 * drained_tres_count_list_set - as drained_tres_count_set(), for the pool
 *	named name in *counts. A count of 0 deletes that pool.
 * IN/OUT counts - pool list, created when NULL
 * IN name - pool name
 * IN count - drained count, 0 to delete the pool
 * IN reason - why it is drained
 * IN replace_reason - replace the reason of a pool that already has a count
 */
extern void drained_tres_count_list_set(list_t **counts, const char *name,
					uint64_t count, const char *reason,
					bool replace_reason);

/*
 * Pack one embedded pool in struct order: count, name, reason.
 * IN cnt - pool to pack
 * IN protocol_version - version to pack for
 * IN buffer - buffer to pack into
 */
extern void drained_tres_count_pack(drain_count_t *cnt,
				    uint16_t protocol_version, buf_t *buffer);

/*
 * Unpack one embedded pool packed by drained_tres_count_pack().
 * OUT cnt - pool to fill
 * IN protocol_version - version to unpack
 * IN buffer - buffer to unpack from
 * RET SLURM_SUCCESS or an error
 */
extern int drained_tres_count_unpack(drain_count_t *cnt,
				     uint16_t protocol_version, buf_t *buffer);

/*
 * Pack a list of drain_count_t.
 * IN counts - pool list, may be NULL
 * IN protocol_version - version to pack for
 * IN buffer - buffer to pack into
 */
extern void drained_tres_counts_pack(list_t *counts, uint16_t protocol_version,
				     buf_t *buffer);

/*
 * Unpack a list packed by drained_tres_counts_pack().
 * OUT counts - restored list, NULL when the list is empty
 * IN protocol_version - version to unpack
 * IN buffer - buffer to unpack from
 * RET SLURM_SUCCESS or an error
 */
extern int drained_tres_counts_unpack(list_t **counts,
				      uint16_t protocol_version, buf_t *buffer);

#endif /* !_HAVE_DRAINED_TRES_H */
