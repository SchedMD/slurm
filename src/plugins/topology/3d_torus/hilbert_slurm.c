/*****************************************************************************\
 *  hilbert_slurm.c - Reorder the node records to place them into order
 *	on a Hilbert curve so that the resource allocation problem in
 *	N-dimensions can be reduced to a 1-dimension problem
 *****************************************************************************
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
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

#include "config.h"

#include "src/plugins/topology/3d_torus/hilbert.h"
#include "src/slurmctld/slurmctld.h"
#include "src/interfaces/select.h"

#define _DEBUG 0

/* Using the node record table, generate a Hilbert integer for each node
 * based upon its coordinates and sort the records in that order. This must
 * be called once, immediately after reading the slurm.conf file. */
extern void nodes_to_hilbert_curve(void)
{
	static bool first_run = true;
	int coord_inx, i, j, k, max_coord = 0;
	int *coords;
	node_record_t *node_ptr;
	coord_t hilbert[3];
	int dims = 3;
#if 	(SYSTEM_DIMENSIONS != 3)
		fatal("current logic only supports 3-dimensions");
#endif	/* SYSTEM_DIMENSIONS != 3) */

	/* We can only re-order the nodes once at slurmctld startup.
	 * After that time, many bitmaps are created based upon the
	 * index of each node name in the array. */
	if (!first_run)
		return;

	/* Get the coordinates for each node based upon its numeric suffix */
	coords = xmalloc(sizeof(int) * node_record_count * dims);
	for (i = 0, coord_inx = 0; (node_ptr = next_node(&i)); i++) {
		j = strlen(node_ptr->name);
		if (j < dims) {
			fatal("hostname %s lacks numeric %d dimension suffix",
			      node_ptr->name, dims);
		}
		for (k=dims; k; k--) {
			coords[coord_inx] = select_char2coord(
				node_ptr->name[j-k]);
			if (coords[coord_inx] < 0) {
				fatal("hostname %s lacks valid numeric suffix",
				      node_ptr->name);
			}
			max_coord = MAX(max_coord, coords[coord_inx]);
			coord_inx++;	/* Don't put into MAX macro */
		}
	}
	if (max_coord > 31) {
		fatal("maximum node coordinate exceeds system limit (%d>32)",
		      max_coord);
	}

	/* Generate each node's Hilbert integer */
	for (i = 0, coord_inx = 0; (node_ptr = next_node(&i)); i++) {
		for (j=0; j<dims; j++)
			hilbert[j] = coords[coord_inx++];
		AxestoTranspose(hilbert, 5, dims);

		/* A variation on the below calculation would be required here
		 * for other dimension counts */
		node_ptr->node_rank =
			((hilbert[0]>>4 & 1) << 14) +
			((hilbert[1]>>4 & 1) << 13) +
			((hilbert[2]>>4 & 1) << 12) +
			((hilbert[0]>>3 & 1) << 11) +
			((hilbert[1]>>3 & 1) << 10) +
			((hilbert[2]>>3 & 1) <<  9) +
			((hilbert[0]>>2 & 1) <<  8) +
			((hilbert[1]>>2 & 1) <<  7) +
			((hilbert[2]>>2 & 1) <<  6) +
			((hilbert[0]>>1 & 1) <<  5) +
			((hilbert[1]>>1 & 1) <<  4) +
			((hilbert[2]>>1 & 1) <<  3) +
			((hilbert[0]>>0 & 1) <<  2) +
			((hilbert[1]>>0 & 1) <<  1) +
			((hilbert[2]>>0 & 1) <<  0);
	}
}
