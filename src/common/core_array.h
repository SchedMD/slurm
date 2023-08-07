/*****************************************************************************\
 *  core_array.h - Handle functions dealing with core_arrays.
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

#ifndef _CORE_ARRAY_H
#define _CORE_ARRAY_H

#include "src/common/bitstring.h"

/*
 * Build an empty array of bitmaps, one per node
 * Use free_core_array() to release returned memory
 */
extern bitstr_t **build_core_array(void);

/* Clear all elements of an array of bitmaps, one per node */
extern void clear_core_array(bitstr_t **core_array);

/*
 * Copy an array of bitmaps, one per node
 * Use free_core_array() to release returned memory
 */
extern bitstr_t **copy_core_array(bitstr_t **core_array);

/*
 * Return count of set bits in array of bitmaps, one per node
 */
extern int count_core_array_set(bitstr_t **core_array);

/*
 * Set row_bitmap1 to core_array1 & core_array2
 */
extern void core_array_and(bitstr_t **core_array1, bitstr_t **core_array2);

/*
 * Set row_bitmap1 to row_bitmap1 & !row_bitmap2
 * In other words, any bit set in row_bitmap2 is cleared from row_bitmap1
 */
extern void core_array_and_not(bitstr_t **core_array1, bitstr_t **core_array2);

/*
 * Set core_array to ~core_array
 */
extern void core_array_not(bitstr_t **core_array);

/*
 * Set row_bitmap1 to core_array1 | core_array2
 */
extern void core_array_or(bitstr_t **core_array1, bitstr_t **core_array2);

/* Free an array of bitmaps, one per node */
extern void free_core_array(bitstr_t ***core_array);

/* Enable detailed logging of cr_dist() node and per-node core bitmaps */
extern void core_array_log(char *loc, bitstr_t *node_map, bitstr_t **core_map);

/* Translate per-node core bitmap array to system-wide core bitmap */
extern bitstr_t *core_array_to_bitmap(bitstr_t **core_array);

/* Translate system-wide core bitmap to per-node core bitmap array */
extern bitstr_t **core_bitmap_to_array(bitstr_t *core_bitmap);

#endif /* _CORE_ARRAY_H */
