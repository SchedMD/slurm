/*****************************************************************************\
 *  src/common/reverse_tree.c
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher J. Morrone <morrone2@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *  Portions copyright (C) 2014 Institute of Semiconductor Physics
 *                     Siberian Branch of Russian Academy of Science
 *  Written by Artem Polyakov <artpol84@gmail.com>.
 *  All rights reserved.
 *  Portions copyright (C) 2017 Mellanox Technologies.
 *  Written by Artem Polyakov <artpol84@gmail.com>.
 *  All rights reserved.
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

#include <stdio.h>
#include <stdlib.h>

static inline int int_pow(int num, int power)
{
	int res;
	int i;

	if (power == 0)
		res = 1;
	else if (power == 1)
		res = num;
	else {
		res = num;
		for (i = 1; i < power; i++)
			res *= num;
	}

	return res;
}

static inline int geometric_series(int width, int depth)
{
	/*
	 * the main idea behind this formula is the following math base:
	 * a^n - b^n = (a-b)*(a^(n-1) + b*a^(n-2) + b^2*a^(n-3) ... +b^(n-1))
	 * if we set a = 1, b = w (width) we will turn it to:
	 *
	 *        1 - w^n = (1-w)*(1 + w + w^2 + ... +w^(n-1))     (1)
	 *        C = (1 + w + w^2 + ... +w^(n-1))                 (2)
	 *
	 * is perfectly a number of children in the tree with width w.
	 * So we can calculate C from (1):
	 *
	 *        1 - w^n = (1-w)*C =>  C = (1-w^n)/(1-w)          (3)
	 *
	 * (2) takes (n-1) sums and (n-2) multiplication (or (n-2) exponentiations).
	 * (3) takes n+1 multiplications (or one exponentiation), 2 subtractions,
	 * 1 division.
	 * However if more optimal exponentiation algorithm is used like this
	 * (https://en.wikipedia.org/wiki/Exponentiation_by_squaring) number of
	 * multiplications will be O(log(n)).
	 * In case of (2) we will still need all the intermediate powers which
	 * doesn't allow to benefit from efficient exponentiation.
	 * As it is now - int_pow(x, n) is a primitive O(n) algorithm.
	 *
	 * w = 1 is a special case that will lead to divide by 0.
	 * as C (2) corresponds to a full number of nodes in the tree including
	 * the root - we need to return analog in the w=1 tree which is
	 * (depth+1).
	 */
	return (width == 1) ?
		(depth+1) : (1 - (int_pow(width, (depth+1)))) / (1 - width);
}

static inline int dep(int total, int width)
{
	int i;
	int x = 0;

	for (i = 1; x < total-1; i++) {
		x += int_pow(width, i);
	}

	return i-1;
}

static int search_tree(int id, int node, int max_children, int width,
		       int *parent_id, int *next_max_children, int *depth)
{
	int current, next, next_children;
	int i;

	*depth = *depth + 1;
	current = node + 1;
	next_children = (max_children / width) - 1;

	if (id == current) {
		*parent_id = node;
		*next_max_children = next_children;
		return 1;
	}

	for (i = 1; i <= width; i++) {
		next = current + next_children + 1;
		if (id == next) {
			*parent_id = node;
			*next_max_children = next_children;
			return 1;
		}
		if (id > current && id < next) {
			return search_tree(id, current, next_children, width,
					   parent_id, next_max_children,
					   depth);
		}
		current = next;
	}
	*parent_id = -1;
	*next_max_children = -1;
	return 0;
}

void
reverse_tree_info(int rank, int num_nodes, int width,
		  int *parent, int *num_children,
		  int *depth, int *max_depth)
{
	int max_children;
	int p, c;

	/* sanity check */
	if (rank >= num_nodes) {
		*parent = -1;
		*num_children = -1;
		*depth = -1;
		*max_depth = -1;
		return;
	}

	/*
	 * If width is more than nodes total, then don't bother trying to
	 * figure out the tree as there isn't a tree. All nodes just directly
	 * talk to the controller.
	 */
	if (width > num_nodes) {
		*parent = -1;
		*num_children = 0;
		*depth = 0;	/* not used currently */
		*max_depth = 0;	/* not used currently */
		return;
	}

	*max_depth = dep(num_nodes, width);
	if (rank == 0) {
		*parent = -1;
		*num_children = num_nodes - 1;
		*depth = 0;
		return;
	}

	max_children = geometric_series(width, *max_depth);
	*depth = 0;
	search_tree(rank, 0, max_children, width, &p, &c, depth);

	if ((rank + c) >= num_nodes)
		c = num_nodes - rank - 1;

	*parent = p;
	*num_children = c;
	return;
}

int reverse_tree_direct_children(int rank, int num_nodes, int width,
				 int depth, int *children)
{
	int current, child_distance;
	int max_depth, sub_depth, max_rank_children;
	int i;

	/* no children if tree is disabled */
	if (width > num_nodes)
		return 0;

	max_depth = dep(num_nodes, width);
	sub_depth = max_depth - depth;
	if( sub_depth == 0 ){
		return 0;
	}
	max_rank_children = geometric_series(width, sub_depth);
	current = rank + 1;
	child_distance = (max_rank_children / width);
	for (i = 0; i < width && current < num_nodes; i++) {
		children[i] = current;
		current += child_distance;
	}
	return i;
}
