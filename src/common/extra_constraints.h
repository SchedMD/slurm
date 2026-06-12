/*****************************************************************************\
 *  extra_constraints.h
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

#ifndef _EXTRA_CONSTRAINTS_H
#define _EXTRA_CONSTRAINTS_H

#include "src/common/data.h"

typedef enum {
	OP_NONE = 0,
	OP_CHILD_AND, /* Accept both '&' and ',' */
	OP_CHILD_AND_COMMA, /* During parsing, this is automatically converted
			     * to OP_CHILD_AND */
	OP_CHILD_OR,
	OP_LEAF_EQ,
	OP_LEAF_NE,
	OP_LEAF_GT,
	OP_LEAF_GTE,
	OP_LEAF_LT,
	OP_LEAF_LTE,
} op_t;

typedef struct elem elem_t;
struct elem {
	op_t operator;
	elem_t **children;
	int num_children;
	int curr_max_children;
	char *key;
	char *value;
};

/*
 * This function returns a heap allocated string that represents the tree in a
 * human readable format. This should primarily be used for debugging.
 * The return value must be free'd with xfree.
 */
extern char *extra_constraints_2str(elem_t *el);

extern bool extra_constraints_enabled(void);
extern void extra_constraints_free_null(elem_t **el);
#define FREE_NULL_EXTRA_CONSTRAINTS(el) \
	extra_constraints_free_null(&el)

/*
 * Parse an extra constraint into a tree. The tree should only be accessed by
 * functions in this file.
 *
 * IN extra - constraints to be parsed
 * OUT head - head of tree if parsing is successful. This should be free'd by
 *            calling FREE_NULL_EXTRA_CONSTRAINTS.
 *
 * Return SLURM_SUCCESS if parsing is successful or disabled.
 * Return ESLURM_INVALID_EXTRA if parsing failed.
 */
extern int extra_constraints_parse(char *extra, elem_t **head);

/*
 * Enable or disable extra constraints parsing.
 */
extern void extra_constraints_set_parsing(bool set);

/*
 * Return true if one of the following conditions are met:
 * - Extra constraints parsing is disabled
 * - No extra constraints are given (head == NULL)
 * - The constraints given in the tree "head" are satisfied by "data"
 * Otherwise return false.
 */
extern bool extra_constraints_test(elem_t *head, data_t *data);

#endif /* _EXTRA_CONSTRAINTS_H */
