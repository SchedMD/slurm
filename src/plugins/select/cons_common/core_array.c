/*****************************************************************************\
 *  core_array.c - Handle functions dealing with core_arrays.
 *****************************************************************************
 *  Copyright (C) 2019 SchedMD LLC
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

#include "cons_common.h"
#include "src/common/xstring.h"

/*
 * Build an empty array of bitmaps, one per node
 * Use free_core_array() to release returned memory
 */
extern bitstr_t **build_core_array(void)
{
	xassert(core_array_size);

	return xcalloc(core_array_size, sizeof(bitstr_t *));
}

/* Clear all elements of an array of bitmaps, one per node */
extern void clear_core_array(bitstr_t **core_array)
{
	int n;

	if (!core_array)
		return;
	for (n = 0; n < core_array_size; n++) {
		if (core_array[n])
			bit_clear_all(core_array[n]);
	}
}

/*
 * Copy an array of bitmaps, one per node
 * Use free_core_array() to release returned memory
 */
extern bitstr_t **copy_core_array(bitstr_t **core_array)
{
	bitstr_t **core_array2 = NULL;
	int n;

	if (core_array) {
		core_array2 = xmalloc(sizeof(bitstr_t *) * core_array_size);
		for (n = 0; n < core_array_size; n++) {
			if (core_array[n])
				core_array2[n] = bit_copy(core_array[n]);
		}
	}
	return core_array2;
}

/*
 * Return count of set bits in array of bitmaps, one per node
 */
extern int count_core_array_set(bitstr_t **core_array)
{
	int count = 0, n;

	if (!core_array)
		return count;
	for (n = 0; n < core_array_size; n++) {
		if (core_array[n])
			count += bit_set_count(core_array[n]);
	}
	return count;
}

/*
 * Set core_array to ~core_array
 */
extern void core_array_not(bitstr_t **core_array)
{
	if (!core_array)
		return;
	for (int n = 0; n < core_array_size; n++) {
		if (core_array[n])
			bit_not(core_array[n]);
	}
	return;
}

/*
 * Set row_bitmap1 to core_array1 & core_array2
 */
extern void core_array_and(bitstr_t **core_array1, bitstr_t **core_array2)
{
	int n, s1, s2;
	for (n = 0; n < core_array_size; n++) {
		if (core_array1[n] && core_array2[n]) {
			s1 = bit_size(core_array1[n]);
			s2 = bit_size(core_array2[n]);
			if (s1 > s2)
				bit_realloc(core_array2[n], s1);
			else if (s1 < s2)
				bit_realloc(core_array1[n], s2);
			bit_and(core_array1[n], core_array2[n]);
		} else
			FREE_NULL_BITMAP(core_array1[n]);
	}
}

/*
 * Set row_bitmap1 to row_bitmap1 & !row_bitmap2
 * In other words, any bit set in row_bitmap2 is cleared from row_bitmap1
 */
extern void core_array_and_not(bitstr_t **core_array1, bitstr_t **core_array2)
{
	int n, s1, s2;
	for (n = 0; n < core_array_size; n++) {
		if (core_array1[n] && core_array2[n]) {
			s1 = bit_size(core_array1[n]);
			s2 = bit_size(core_array2[n]);
			if (s1 > s2)
				bit_realloc(core_array2[n], s1);
			else if (s1 < s2)
				bit_realloc(core_array1[n], s2);
			bit_and_not(core_array1[n], core_array2[n]);
		}
	}
}

/*
 * Set row_bitmap1 to core_array1 | core_array2
 */
extern void core_array_or(bitstr_t **core_array1, bitstr_t **core_array2)
{
	int n, s1, s2;
	for (n = 0; n < core_array_size; n++) {
		if (core_array1[n] && core_array2[n]) {
			s1 = bit_size(core_array1[n]);
			s2 = bit_size(core_array2[n]);
			if (s1 > s2)
				bit_realloc(core_array2[n], s1);
			else if (s1 < s2)
				bit_realloc(core_array1[n], s2);
			bit_or(core_array1[n], core_array2[n]);
		} else if (core_array2[n])
			core_array1[n] = bit_copy(core_array2[n]);
	}
}

/* Free an array of bitmaps, one per node */
extern void free_core_array(bitstr_t ***core_array)
{
	bitstr_t **core_array2 = *core_array;
	int n;

	if (core_array2) {
		for (n = 0; n < core_array_size; n++)
			FREE_NULL_BITMAP(core_array2[n]);
		xfree(core_array2);
		*core_array = NULL;
	}
}

/* Enable detailed logging of cr_dist() node and per-node core bitmaps */
extern void core_array_log(char *loc, bitstr_t *node_map, bitstr_t **core_map)
{
	char tmp[100];

	if (!(slurm_conf.debug_flags & DEBUG_FLAG_SELECT_TYPE))
		return;

	verbose("%s", loc);

	if (node_map) {
		char *node_list = bitmap2node_name(node_map);
		verbose("node_list:%s", node_list);
		xfree(node_list);
	}

	if (core_map) {
		char *core_list = NULL;
		char *sep = "";

		for (int i = 0; i < core_array_size; i++) {
			if (!core_map[i] || (bit_ffs(core_map[i]) == -1))
				continue;
			bit_fmt(tmp, sizeof(tmp), core_map[i]);
			xstrfmtcat(core_list, "%snode[%d]:%s", sep, i, tmp);
			sep = ",";
		}
		verbose("core_list:%s", core_list);
		xfree(core_list);
	}
}

/* Translate per-node core bitmap array to system-wide core bitmap */
extern bitstr_t *core_array_to_bitmap(bitstr_t **core_array)
{
	bitstr_t *core_bitmap = NULL;
	int i;
	int c, core_offset;
#if _DEBUG
	char tmp[128];
#endif

	if (!core_array)
		return core_bitmap;

#if _DEBUG
	for (i = 0; i < core_array_size; i++) {
		if (!core_array[i])
			continue;
		bit_fmt(tmp, sizeof(tmp), core_array[i]);
		error("OUT core bitmap[%d] %s",
		      i, tmp);
	}
#endif

	if (!is_cons_tres) {
		core_bitmap = *core_array;
		*core_array = NULL;
		return core_bitmap;
	}

	core_bitmap = bit_alloc(cr_get_coremap_offset(node_record_count));
	for (i = 0; i < core_array_size; i++) {
		if (!core_array[i])
			continue;
		core_offset = cr_get_coremap_offset(i);
		for (c = 0; c < node_record_table_ptr[i]->tot_cores; c++) {
			if (bit_test(core_array[i], c))
				bit_set(core_bitmap, core_offset + c);
		}
	}

#if _DEBUG
	bit_fmt(tmp, sizeof(tmp), core_bitmap);
	error("IN core bitmap %s", tmp);
#endif

	return core_bitmap;
}

/* Translate system-wide core bitmap to per-node core bitmap array */
extern bitstr_t **core_bitmap_to_array(bitstr_t *core_bitmap)
{
	bitstr_t **core_array = NULL;
	int i, i_first, i_last, j, c;
	int node_inx = 0, core_offset;
	char tmp[128];

	if (!core_bitmap)
		return core_array;

#if _DEBUG
	bit_fmt(tmp, sizeof(tmp), core_bitmap);
	error("IN core bitmap %s", tmp);
#endif

	i_first = bit_ffs(core_bitmap);
	if (i_first == -1)
		return core_array;

	core_array = build_core_array();

	if (!is_cons_tres) {
		*core_array = bit_copy(core_bitmap);
		return core_array;
	}

	i_last = bit_fls(core_bitmap);

	for (i = i_first; i <= i_last; i++) {
		if (!bit_test(core_bitmap, i))
			continue;
		for (j = node_inx; next_node(&j); j++) {
			if (i < cr_get_coremap_offset(j+1)) {
				node_inx = j;
				i = cr_get_coremap_offset(j+1) - 1;
				break;
			}
		}
		if (j >= node_record_count) {
			bit_fmt(tmp, sizeof(tmp), core_bitmap);
			error("error translating core bitmap %s",
			      tmp);
			break;
		}
		/* Copy all core bitmaps for this node here */
		core_array[node_inx] =
			bit_alloc(node_record_table_ptr[node_inx]->tot_cores);
		core_offset = cr_get_coremap_offset(node_inx);
		for (c = 0; c < node_record_table_ptr[node_inx]->tot_cores;
		     c++) {
			if (bit_test(core_bitmap, core_offset + c))
				bit_set(core_array[node_inx], c);
		}
		node_inx++;
	}

#if _DEBUG
	for (i = 0; i < core_array_size; i++) {
		if (!core_array[i])
			continue;
		bit_fmt(tmp, sizeof(tmp), core_array[i]);
		error("OUT core bitmap[%d] %s",
		      i, tmp);
	}
#endif

	return core_array;
}
