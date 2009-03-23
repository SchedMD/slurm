/*****************************************************************************\
 *  hilbert_slurm.c - Reorder the node records to place them into order
 *	on a Hilbert curve so that the resource allocation problem in 
 *	N-dimensions can be reduced to a 1-dimension problem
 *****************************************************************************
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "src/plugins/topology/3d_torus/hilbert.h"
#include "src/slurmctld/slurmctld.h"

static int _coord(char coord)
{
	if ((coord >= '0') && (coord <= '9'))
		return (coord - '0');
	if ((coord >= 'A') && (coord <= 'Z'))
		return (coord - 'A');
	return -1;
}

/* Using the node record table, generate a Hilbert integer for each node
 * based upon its coordinates and sort the records in that order. This must
 * be called once, immediately after reading the slurm.conf file. */
extern void nodes_to_hilbert_curve(void)
{
	int coord_inx, i, j, k, max_coord = 0, min_inx;
	uint32_t min_val;
	int *coords;
	struct node_record *node_ptr, *node_ptr2;
#ifdef HAVE_3D
	coord_t hilbert[3];
	int dims = 3;
#else
	coord_t hilbert[2];
	int dims = 2;
	fatal("current logic only supports 3-dimensions");
#endif	/* HAVE_3D */

	/* Get the coordinates for each node based upon its numeric suffix */
	coords = xmalloc(sizeof(int) * node_record_count * dims);
	for (i=0, coord_inx=0, node_ptr=node_record_table_ptr; 
	     i<node_record_count; i++, node_ptr++) {
		j = strlen(node_ptr->name);
		if (j < dims) {
			fatal("hostname %s lacks numeric %d dimension suffix",
			      node_ptr->name, dims);
		}
		for (k=dims; k; k--) {
			coords[coord_inx] = _coord(node_ptr->name[j-k]);
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
	for (i=0, coord_inx=0, node_ptr=node_record_table_ptr; 
	     i<node_record_count; i++, node_ptr++) {
		for (j=0; j<dims; j++)
			hilbert[j] = coords[coord_inx++];
		AxestoTranspose(hilbert, 5, dims);
#ifdef HAVE_3D
		node_ptr->hilbert_integer = 
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
#else
		/* A variation on the above calculation would be required here
		 * for other dimension counts */
#endif
	}

	/* Now we need to sort the node records. We only need to move a few
	 * fields since the others were all initialized to identical values */
	for (i=0; i<node_record_count; i++) {
		min_val = node_record_table_ptr[i].hilbert_integer;
		min_inx = i;
		for (j=(i+1); j<node_record_count; j++) {
			if (node_record_table_ptr[j].hilbert_integer < 
			    min_val) {
				min_val = node_record_table_ptr[j].
					  hilbert_integer;
				min_inx = j;
			}
		}
		if (min_inx != i) {	/* swap records */
			char *tmp_name;
			int tmp_val;
			node_ptr =  node_record_table_ptr + i;
			node_ptr2 = node_record_table_ptr + min_inx;

			tmp_name  = node_ptr->name;
			node_ptr->name  = node_ptr2->name;
			node_ptr2->name = tmp_name;

			tmp_name = node_ptr->comm_name;
			node_ptr->comm_name  = node_ptr2->comm_name;
			node_ptr2->comm_name = tmp_name;

			tmp_val = node_ptr->hilbert_integer;
			node_ptr->hilbert_integer  = node_ptr2->hilbert_integer;
			node_ptr2->hilbert_integer = tmp_val;
		}
	}

#if 0
	/* Log the results */
	for (i=0, node_ptr=node_record_table_ptr; i<node_record_count; 
	     i++, node_ptr++) {
		info("%s: %u", node_ptr->name, node_ptr->hilbert_integer);
	}
#endif
}

