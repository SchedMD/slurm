/*****************************************************************************\
 *  ba_common.c
 *
 *****************************************************************************
 *  Copyright (C) 2011 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#include "ba_common.h"

#if (SYSTEM_DIMENSIONS == 1)
int cluster_dims = 1;
int cluster_base = 10;
#else
int cluster_dims = 3;
int cluster_base = 36;
#endif
uint32_t cluster_flags = 0;
uint16_t ba_deny_pass = 0;

bool ba_initialized = false;
uint32_t ba_debug_flags = 0;

static int _coord(char coord)
{
	if ((coord >= '0') && (coord <= '9'))
		return (coord - '0');
	if ((coord >= 'A') && (coord <= 'Z'))
		return (coord - 'A') + 10;
	return -1;
}

/*
 * Increment a geometry index array, return false after reaching the last entry
 */
static bool _incr_geo(int *geo, ba_geo_system_t *my_geo_system)
{
	int dim, i;

	for (dim = my_geo_system->dim_count - 1; dim >= 0; dim--) {
		if (geo[dim] < my_geo_system->dim_size[dim]) {
			geo[dim]++;
			for (i = dim + 1; i < my_geo_system->dim_count; i++)
				geo[i] = 1;
			return true;
		}
	}

	return false;
}

/* Translate a multi-dimension coordinate (3-D, 4-D, 5-D, etc.) into a 1-D
 * offset in the cnode* bitmap */
static void _ba_node_xlate_to_1d(int *offset_1d, int *full_offset,
				   ba_geo_system_t *my_geo_system)
{
	int i, map_offset;

	xassert(offset_1d);
	xassert(full_offset);
	map_offset = full_offset[0];
	for (i = 1; i < my_geo_system->dim_count; i++) {
		map_offset *= my_geo_system->dim_size[i];
		map_offset += full_offset[i];
	}
	*offset_1d = map_offset;
}

static void _internal_removable_set_mps(int level, int *start,
					int *end, int *coords, bool mark)
{
	ba_mp_t *curr_mp;

	if (level > cluster_dims)
		return;

	if (level < cluster_dims) {
		for (coords[level] = start[level];
		     coords[level] <= end[level];
		     coords[level]++) {
			/* handle the outter dims here */
			_internal_removable_set_mps(
				level+1, start, end, coords, mark);
		}
		return;
	}
	curr_mp = coord2ba_mp(coords);
	if (mark) {
		//info("can't use %s", curr_mp->coord_str);
		curr_mp->used |= BA_MP_USED_TEMP;
	} else {
		curr_mp->used &= (~BA_MP_USED_TEMP);
	}
}

/**
 * Initialize internal structures by either reading previous block
 * configurations from a file or by running the graph solver.
 *
 * IN: node_info_msg_t * can be null,
 *     should be from slurm_load_node().
 *
 * return: void.
 */
extern void ba_init(node_info_msg_t *node_info_ptr, bool sanity_check)
{
	node_info_t *node_ptr = NULL;
	int number, count;
	char *numeric = NULL;
	int i, j, k;
	slurm_conf_node_t **ptr_array;
	int coords[HIGHEST_DIMENSIONS];
	char *p = '\0';
	int num_cpus = 0;
	int real_dims[HIGHEST_DIMENSIONS];
	int dims[HIGHEST_DIMENSIONS];
	char dim_str[HIGHEST_DIMENSIONS+1];

	/* We only need to initialize once, so return if already done so. */
	if (ba_initialized)
		return;

	cluster_dims = slurmdb_setup_cluster_dims();
	cluster_flags = slurmdb_setup_cluster_flags();
	set_ba_debug_flags(slurm_get_debug_flags());

	bridge_init("");

	memset(coords, 0, sizeof(coords));
	memset(dims, 0, sizeof(dims));
	memset(real_dims, 0, sizeof(real_dims));
	memset(dim_str, 0, sizeof(dim_str));
	/* cluster_dims is already set up off of working_cluster_rec */
	if (cluster_dims == 1) {
		if (node_info_ptr) {
			real_dims[0] = dims[0] = node_info_ptr->record_count;
			for (i=1; i<cluster_dims; i++)
				real_dims[i] = dims[i] = 1;
			num_cpus = node_info_ptr->record_count;
		}
		goto setup_done;
	} else if (working_cluster_rec && working_cluster_rec->dim_size) {
		for(i=0; i<cluster_dims; i++) {
			real_dims[i] = dims[i] =
				working_cluster_rec->dim_size[i];
		}
		goto setup_done;
	}


	if (node_info_ptr) {
		for (i = 0; i < (int)node_info_ptr->record_count; i++) {
			node_ptr = &node_info_ptr->node_array[i];
			number = 0;

			if (!node_ptr->name) {
				memset(dims, 0, sizeof(dims));
				goto node_info_error;
			}

			numeric = node_ptr->name;
			while (numeric) {
				if (numeric[0] < '0' || numeric[0] > 'D'
				    || (numeric[0] > '9'
					&& numeric[0] < 'A')) {
					numeric++;
					continue;
				}
				number = xstrntol(numeric, &p, cluster_dims,
						  cluster_base);
				break;
			}
			hostlist_parse_int_to_array(
				number, coords, cluster_dims, cluster_base);

			memcpy(dims, coords, sizeof(dims));
		}
		for (j=0; j<cluster_dims; j++) {
			dims[j]++;
			/* this will probably be reset below */
			real_dims[j] = dims[j];
		}
		num_cpus = node_info_ptr->record_count;
	}
node_info_error:
	for (j=0; j<cluster_dims; j++)
		if (!dims[j])
			break;

	if (j < cluster_dims) {
		debug("Setting dimensions from slurm.conf file");
		count = slurm_conf_nodename_array(&ptr_array);
		if (count == 0)
			fatal("No NodeName information available!");

		for (i = 0; i < count; i++) {
			char *nodes = ptr_array[i]->nodenames;
			j = 0;
			while (nodes[j] != '\0') {
				int mid = j   + cluster_dims + 1;
				int fin = mid + cluster_dims + 1;

				if (((nodes[j] == '[') || (nodes[j] == ','))
				    && ((nodes[mid] == 'x')
					|| (nodes[mid] == '-'))
				    && ((nodes[fin] == ']')
					|| (nodes[fin] == ',')))
					j = mid + 1; /* goto the mid
						      * and skip it */
				else if ((nodes[j] >= '0' && nodes[j] <= '9')
					 || (nodes[j] >= 'A'
					     && nodes[j] <= 'Z')) {
					/* suppose to be blank, just
					   making sure this is the
					   correct alpha num
					*/
				} else {
					j++;
					continue;
				}

				for (k = 0; k < cluster_dims; k++, j++)
					dims[k] = MAX(dims[k],
						      _coord(nodes[j]));
				if (nodes[j] != ',')
					break;
			}

			/* j = 0; */
			/* while (node->nodenames[j] != '\0') { */
			/* 	if ((node->nodenames[j] == '[' */
			/* 	     || node->nodenames[j] == ',') */
			/* 	    && (node->nodenames[j+10] == ']' */
			/* 		|| node->nodenames[j+10] == ',') */
			/* 	    && (node->nodenames[j+5] == 'x' */
			/* 		|| node->nodenames[j+5] == '-')) { */
			/* 		j+=6; */
			/* 	} else if ((node->nodenames[j] >= '0' */
			/* 		    && node->nodenames[j] <= '9') */
			/* 		   || (node->nodenames[j] >= 'A' */
			/* 		       && node->nodenames[j] <= 'Z')) { */
			/* 		/\* suppose to be blank, just */
			/* 		   making sure this is the */
			/* 		   correct alpha num */
			/* 		*\/ */
			/* 	} else { */
			/* 		j++; */
			/* 		continue; */
			/* 	} */
			/* 	number = xstrntol(node->nodenames + j, */
			/* 			  &p, cluster_dims, */
			/* 			  cluster_base); */
			/* 	hostlist_parse_int_to_array( */
			/* 		number, coords, cluster_dims, */
			/* 		cluster_base); */
			/* 	j += 4; */

			/* 	for(k=0; k<cluster_dims; k++) */
			/* 		dims[k] = MAX(dims[k], */
			/* 				  coords[k]); */

			/* 	if (node->nodenames[j] != ',') */
			/* 		break; */
			/* } */
		}

		for (j=0; j<cluster_dims; j++)
			if (dims[j])
				break;

		if (j >= cluster_dims)
			info("are you sure you only have 1 midplane? %s",
			     ptr_array[i]->nodenames);

		for (j=0; j<cluster_dims; j++) {
			dims[j]++;
			/* this will probably be reset below */
			real_dims[j] = dims[j];
		}
	}

	/* sanity check.  We can only request part of the system, but
	   we don't want to allow more than we have. */
	if (sanity_check) {
		verbose("Attempting to contact MMCS");
		if (bridge_get_size(real_dims) == SLURM_SUCCESS) {
			char real_dim_str[cluster_dims+1];
			memset(real_dim_str, 0, sizeof(real_dim_str));
			for (i=0; i<cluster_dims; i++) {
				dim_str[i] = alpha_num[dims[i]];
				real_dim_str[i] = alpha_num[real_dims[i]];
			}
			verbose("BlueGene configured with %s midplanes",
				real_dim_str);
			for (i=0; i<cluster_dims; i++)
				if (dims[i] > real_dims[i])
					fatal("You requested a %s system, "
					      "but we only have a "
					      "system of %s.  "
					      "Change your slurm.conf.",
					      dim_str, real_dim_str);
		}
	}

setup_done:
	if (cluster_dims == 1) {
		if (!dims[0]) {
			debug("Setting default system dimensions");
			real_dims[0] = dims[0] = 100;
			for (i=1; i<cluster_dims; i++)
				real_dims[i] = dims[i] = 1;
		}
	} else {
		for (i=0; i<cluster_dims; i++)
			dim_str[i] = alpha_num[dims[i]];
		debug("We are using %s of the system.", dim_str);
	}

	if (!num_cpus) {
		num_cpus = 1;
		for(i=0; i<cluster_dims; i++)
			num_cpus *= dims[i];
	}

	ba_create_system(num_cpus, real_dims, dims);

	bridge_setup_system();

	ba_initialized = true;
	init_grid(node_info_ptr);
}


/**
 * destroy all the internal (global) data structs.
 */
extern void ba_fini()
{
	if (!ba_initialized){
		return;
	}

	bridge_fini();
	ba_destroy_system();
	ba_initialized = false;

//	debug3("pa system destroyed");
}

extern void destroy_ba_mp(void *ptr)
{
	ba_mp_t *ba_mp = (ba_mp_t *)ptr;
	if (ba_mp) {
		xfree(ba_mp->loc);
		xfree(ba_mp);
	}
}

extern ba_mp_t *str2ba_mp(char *coords)
{
	int coord[cluster_dims];
	int len, dim;
	int number;
	char *p = '\0';
	static int *dims = NULL;

	if (!dims)
		dims = select_g_ba_get_dims();

	if (!coords)
		return NULL;
	len = strlen(coords) - cluster_dims;
	if (len < 0)
		return NULL;
	number = xstrntol(coords + len, &p, cluster_dims, cluster_base);

	hostlist_parse_int_to_array(number, coord, cluster_dims, cluster_base);

	for (dim=0; dim<cluster_dims; dim++)
		if (coord[dim] > dims[dim])
			break;
	if (dim < cluster_dims) {
		char tmp_char[cluster_dims+1];
		memset(tmp_char, 0, sizeof(tmp_char));
		for (dim=0; dim<cluster_dims; dim++)
			tmp_char[dim] = alpha_num[dims[dim]];
		error("This location %s is not possible in our system %s",
		      coords, tmp_char);
		return NULL;
	}

	return coord2ba_mp(coord);
}

extern void ba_setup_mp(ba_mp_t *ba_mp, bool track_down_mps)
{
	int i;
	uint16_t node_base_state = ba_mp->state & NODE_STATE_BASE;

	if (!track_down_mps ||((node_base_state != NODE_STATE_DOWN)
			       && !(ba_mp->state & NODE_STATE_DRAIN)))
		ba_mp->used = BA_MP_USED_FALSE;

	for (i=0; i<cluster_dims; i++){
#ifdef HAVE_BG_L_P
		int j;
		for(j=0;j<NUM_PORTS_PER_NODE;j++) {
			ba_mp->axis_switch[i].int_wire[j].used = 0;
			if (i!=0) {
				if (j==3 || j==4)
					ba_mp->axis_switch[i].int_wire[j].
						used = 1;
			}
			ba_mp->axis_switch[i].int_wire[j].port_tar = j;
		}
#endif
		ba_mp->axis_switch[i].usage = BG_SWITCH_NONE;
		ba_mp->alter_switch[i].usage = BG_SWITCH_NONE;
	}
}

/*
 * copy info from a ba_mp, a direct memcpy of the ba_mp_t
 *
 * IN ba_mp: mp to be copied
 * Returned ba_mp_t *: copied info must be freed with destroy_ba_mp
 */
extern ba_mp_t *ba_copy_mp(ba_mp_t *ba_mp)
{
	ba_mp_t *new_ba_mp = (ba_mp_t *)xmalloc(sizeof(ba_mp_t));

	memcpy(new_ba_mp, ba_mp, sizeof(ba_mp_t));
	new_ba_mp->loc = xstrdup(ba_mp->loc);
	/* we have to set this or we would be pointing to the original */
	memset(new_ba_mp->next_mp, 0, sizeof(new_ba_mp->next_mp));

	return new_ba_mp;
}

/*
 * Print a linked list of geo_table_t entries.
 * IN geo_ptr - first geo_table entry to print
 * IN header - message header
 * IN my_geo_system - system geometry specification
 */
extern int ba_geo_list_print(ba_geo_table_t *geo_ptr, char *header,
			     ba_geo_system_t *my_geo_system)
{
	int i;
	char dim_buf[64], full_buf[128];

	full_buf[0] = '\0';
	for (i = 0; i < my_geo_system->dim_count; i++) {
		snprintf(dim_buf, sizeof(dim_buf), "%2u ",
			 geo_ptr->geometry[i]);
		strcat(full_buf, dim_buf);
	}
	snprintf(dim_buf, sizeof(dim_buf), ": size:%u : full_dim_cnt:%u",
		 geo_ptr->size, geo_ptr->full_dim_cnt);
	strcat(full_buf, dim_buf);
	info("%s%s", header, full_buf);

	return 0;
}

/*
 * Print the contents of all geo_table_t entries.
 */
extern void ba_print_geo_table(ba_geo_system_t *my_geo_system)
{
	int i;
	ba_geo_table_t *geo_ptr;

	xassert(my_geo_system->geo_table_ptr);
	for (i = 1; i <= my_geo_system->total_size; i++) {
		geo_ptr = my_geo_system->geo_table_ptr[i];
		while (geo_ptr) {
			ba_geo_list_print(geo_ptr, "", my_geo_system);
			geo_ptr = geo_ptr->next_ptr;
		}
	}
}

extern void ba_create_geo_table(ba_geo_system_t *my_geo_system)
{
	ba_geo_table_t *geo_ptr;
	int dim, inx[my_geo_system->dim_count], product;
	struct ba_geo_table **last_pptr;

	if (my_geo_system->geo_table_ptr)
		return;

	xassert(my_geo_system->dim_count);
	my_geo_system->total_size = 1;
	for (dim = 0; dim < my_geo_system->dim_count; dim++) {
		if (my_geo_system->dim_size[dim] < 1)
			fatal("dim_size[%d]= %d", dim,
			      my_geo_system->dim_size[dim]);
		my_geo_system->total_size *= my_geo_system->dim_size[dim];
		inx[dim] = 1;
	}

	my_geo_system->geo_table_ptr = xmalloc(sizeof(ba_geo_table_t *) *
					       (my_geo_system->total_size+1));

	do {
		/* Store new value */
		geo_ptr = xmalloc(sizeof(ba_geo_table_t));
		geo_ptr->geometry = xmalloc(sizeof(uint16_t) *
					    my_geo_system->dim_count);
		product = 1;
		for (dim = 0; dim < my_geo_system->dim_count; dim++) {
			geo_ptr->geometry[dim] = inx[dim];
			product *= inx[dim];
			if (inx[dim] == my_geo_system->dim_size[dim])
				geo_ptr->full_dim_cnt++;
		}
		geo_ptr->size = product;
		xassert(product <= my_geo_system->total_size);
		my_geo_system->geo_table_size++;
		/* Insert record into linked list so that geometries
		 * with full dimensions appear first */
		last_pptr = &my_geo_system->geo_table_ptr[product];
		while ((*last_pptr) &&
		       ((*last_pptr)->full_dim_cnt > geo_ptr->full_dim_cnt)) {
			last_pptr = &((*last_pptr)->next_ptr);
		}
		geo_ptr->next_ptr = *last_pptr;
		*last_pptr = geo_ptr;
	} while (_incr_geo(inx, my_geo_system));   /* Generate next geometry */
}

/*
 * Free memory allocated by ba_create_geo_table().
 * IN my_geo_system - System geometry specification.
 */
extern void ba_free_geo_table(ba_geo_system_t *my_geo_system)
{
	ba_geo_table_t *geo_ptr, *next_ptr;
	int i;

	for (i = 0; i <= my_geo_system->total_size; i++) {
		geo_ptr = my_geo_system->geo_table_ptr[i];
		my_geo_system->geo_table_ptr[i] = NULL;
		while (geo_ptr) {
			next_ptr = geo_ptr->next_ptr;
			xfree(geo_ptr->geometry);
			xfree(geo_ptr);
			geo_ptr = next_ptr;
		}
	}
	my_geo_system->geo_table_size = 0;
	xfree(my_geo_system->geo_table_ptr);
}

/*
 * Allocate a multi-dimensional node bitmap. Use ba_node_map_free() to free
 * IN my_geo_system - system geometry specification
 */
extern bitstr_t *ba_node_map_alloc(ba_geo_system_t *my_geo_system)
{
	bitstr_t *cnode_map = bit_alloc(my_geo_system->total_size);
	if (cnode_map == NULL)
		fatal("bit_alloc: malloc failure");
	return cnode_map;
}

/*
 * Free a node map created by ba_node_map_alloc()
 * IN node_bitmap - bitmap of currently allocated nodes
 * IN my_geo_system - system geometry specification
 */
extern void ba_node_map_free(bitstr_t *node_bitmap,
			     ba_geo_system_t *my_geo_system)
{
	xassert(bit_size(node_bitmap) == my_geo_system->total_size);
	FREE_NULL_BITMAP(node_bitmap);
}

/*
 * Set the contents of the specified position in the bitmap
 * IN node_bitmap - bitmap of currently allocated nodes
 * IN full_offset - N-dimension zero-origin offset to test
 * IN my_geo_system - system geometry specification
 */
extern void ba_node_map_set(bitstr_t *node_bitmap, int *full_offset,
			    ba_geo_system_t *my_geo_system)
{
	int offset_1d;

	_ba_node_xlate_to_1d(&offset_1d, full_offset, my_geo_system);
	bit_set(node_bitmap, offset_1d);
}

/*
 * Return the contents of the specified position in the bitmap
 * IN node_bitmap - bitmap of currently allocated nodes
 * IN full_offset - N-dimension zero-origin offset to test
 * IN my_geo_system - system geometry specification
 */
extern int ba_node_map_test(bitstr_t *node_bitmap, int *full_offset,
			    ba_geo_system_t *my_geo_system)
{
	int offset_1d;

	_ba_node_xlate_to_1d(&offset_1d, full_offset, my_geo_system);
	return bit_test(node_bitmap, offset_1d);
}

/*
 * Add a new allocation's node bitmap to that of the currently
 *	allocated bitmap
 * IN/OUT node_bitmap - bitmap of currently allocated nodes
 * IN alloc_bitmap - bitmap of nodes to be added fromtonode_bitmap
 * IN my_geo_system - system geometry specification
 */
extern void ba_node_map_add(bitstr_t *node_bitmap, bitstr_t *alloc_bitmap,
			    ba_geo_system_t *my_geo_system)
{
	xassert(bit_size(node_bitmap) == my_geo_system->total_size);
	xassert(bit_size(alloc_bitmap) == my_geo_system->total_size);
	bit_or(node_bitmap, alloc_bitmap);
}

/*
 * Remove a terminating allocation's node bitmap from that of the currently
 *	allocated bitmap
 * IN/OUT node_bitmap - bitmap of currently allocated nodes
 * IN alloc_bitmap - bitmap of nodes to be removed from node_bitmap
 * IN my_geo_system - system geometry specification
 */
extern void ba_node_map_rm(bitstr_t *node_bitmap, bitstr_t *alloc_bitmap,
			   ba_geo_system_t *my_geo_system)
{
	xassert(bit_size(node_bitmap) == my_geo_system->total_size);
	xassert(bit_size(alloc_bitmap) == my_geo_system->total_size);
	bit_not(alloc_bitmap);
	bit_and(node_bitmap, alloc_bitmap);
	bit_not(alloc_bitmap);
}

/*
 * Print the contents of a node map created by ba_node_map_alloc() or
 *	ba_geo_test_all(). Output may be in one-dimension or more depending
 *	upon configuration.
 * IN node_bitmap - bitmap representing current system state, bits are set
 *                  for currently allocated nodes
 * IN my_geo_system - system geometry specification
 */
extern void ba_node_map_print(bitstr_t *node_bitmap,
			      ba_geo_system_t *my_geo_system)
{
#if DISPLAY_1D
{
	char out_buf[256];
	bit_fmt(out_buf, sizeof(out_buf), node_bitmap);
	info("%s", out_buf);
}
#endif
#if DISPLAY_FULL_DIM
{
	int i, j, offset[my_geo_system->dim_count];

	xassert(node_bitmap);
	xassert(bit_size(node_bitmap) == my_geo_system->total_size);

	for (i = 0; i < my_geo_system->total_size; i++) {
		if (bit_test(node_bitmap, i)) {
			char dim_buf[16], full_buf[64];
			full_buf[0] = '\0';
			_ba_node_xlate_from_1d(i, offset, my_geo_system);
			for (j = 0; j < my_geo_system->dim_count; j++) {
				snprintf(dim_buf, sizeof(dim_buf), "%2d ",
					 offset[j]);
				strcat(full_buf, dim_buf);
			}
			info("%s", full_buf);
		}
	}
}
#endif
}

/*
 * Attempt to place a new allocation into an existing node state.
 * Do not rotate or change the requested geometry, but do attempt to place
 * it using all possible starting locations.
 *
 * IN node_bitmap - bitmap representing current system state, bits are set
 *                  for currently allocated nodes
 * OUT alloc_node_bitmap - bitmap representing where to place the allocation
 *                         set only if RET == SLURM_SUCCESS
 * IN geo_req - geometry required for the new allocation
 * OUT attempt_cnt - number of job placements attempted
 * IN my_geo_system - system geometry specification
 * RET - SLURM_SUCCESS if allocation can be made, otherwise SLURM_ERROR
 */
extern int ba_geo_test_all(bitstr_t *node_bitmap,
			   bitstr_t **alloc_node_bitmap,
			   ba_geo_table_t *geo_req, int *attempt_cnt,
			   ba_geo_system_t *my_geo_system)
{
	int rc = SLURM_ERROR;
	int i, j;
	int start_offset[my_geo_system->dim_count];
	int next_offset[my_geo_system->dim_count];
	int tmp_offset[my_geo_system->dim_count];
	bitstr_t *new_bitmap;

	xassert(node_bitmap);
	xassert(alloc_node_bitmap);
	xassert(geo_req);
	xassert(attempt_cnt);

	*attempt_cnt = 0;
	/* Start at location 00000 and move through all starting locations */
	memset(start_offset, 0, sizeof(start_offset));

	for (i = 0; i < my_geo_system->total_size; i++) {
		(*attempt_cnt)++;
		memset(tmp_offset, 0, sizeof(tmp_offset));
		while (1) {
			/* Compute location of next entry on the grid */
			for (j = 0; j < my_geo_system->dim_count; j++) {
				next_offset[j] = start_offset[j] +
						 tmp_offset[j];
				next_offset[j] %= my_geo_system->dim_size[j];
			}

			/* Test that point on the grid */
			if (ba_node_map_test(node_bitmap, next_offset,
					     my_geo_system))
				break;

			/* Increment tmp_offset */
			for (j = 0; j < my_geo_system->dim_count; j++) {
				tmp_offset[j]++;
				if (tmp_offset[j] < geo_req->geometry[j])
					break;
				tmp_offset[j] = 0;
			}
			if (j >= my_geo_system->dim_count) {
				rc = SLURM_SUCCESS;
				break;
			}
		}
		if (rc == SLURM_SUCCESS)
			break;

		/* Move to next starting location */
		for (j = 0; j < my_geo_system->dim_count; j++) {
			if (geo_req->geometry[j] == my_geo_system->dim_size[j])
				continue;	/* full axis used */
			if (++start_offset[j] < my_geo_system->dim_size[j])
				break;		/* sucess */
			start_offset[j] = 0;	/* move to next dimension */
		}
		if (j >= my_geo_system->dim_count)
			return rc;		/* end of starting locations */
	}

	new_bitmap = ba_node_map_alloc(my_geo_system);
	memset(tmp_offset, 0, sizeof(tmp_offset));
	while (1) {
		/* Compute location of next entry on the grid */
		for (j = 0; j < my_geo_system->dim_count; j++) {
			next_offset[j] = start_offset[j] + tmp_offset[j];
			if (next_offset[j] >= my_geo_system->dim_size[j])
				next_offset[j] -= my_geo_system->dim_size[j];
		}

		ba_node_map_set(new_bitmap, next_offset, my_geo_system);

		/* Increment tmp_offset */
		for (j = 0; j < my_geo_system->dim_count; j++) {
			tmp_offset[j]++;
			if (tmp_offset[j] < geo_req->geometry[j])
				break;
			tmp_offset[j] = 0;
		}
		if (j >= my_geo_system->dim_count) {
			rc = SLURM_SUCCESS;
			break;
		}
	}
	*alloc_node_bitmap = new_bitmap;

	return rc;
}

/*
 * Used to set all midplanes in a special used state except the ones
 * we are able to use in a new allocation.
 *
 * IN: hostlist of midplanes we do not want
 * RET: SLURM_SUCCESS on success, or SLURM_ERROR on error
 *
 * Note: Need to call ba_reset_all_removed_mps before starting another
 * allocation attempt after
 */
extern int ba_set_removable_mps(char *mps)
{
	int j=0;
	int dim;
	int start[cluster_dims];
        int end[cluster_dims];
	int coords[cluster_dims];

	if (!mps)
		return SLURM_ERROR;

	memset(coords, 0, sizeof(coords));

	while (mps[j] != '\0') {
		int mid = j   + cluster_dims + 1;
		int fin = mid + cluster_dims + 1;
		if (((mps[j] == '[') || (mps[j] == ','))
		    && ((mps[mid] == 'x') || (mps[mid] == '-'))
		    && ((mps[fin] == ']') || (mps[fin] == ','))) {
			/* static char start_char[SYSTEM_DIMENSIONS+1], */
			/* 	end_char[SYSTEM_DIMENSIONS+1]; */

			j++;	/* Skip leading '[' or ',' */
			for (dim = 0; dim < SYSTEM_DIMENSIONS; dim++, j++)
				start[dim] = _coord(mps[j]);
			j++;	/* Skip middle 'x' or '-' */
			for (dim = 0; dim < SYSTEM_DIMENSIONS; dim++, j++)
				end[dim] = _coord(mps[j]);
			/* for (dim = 0; dim<SYSTEM_DIMENSIONS; dim++) { */
			/* 	start_char[dim] = alpha_num[start[dim]]; */
			/* 	end_char[dim] = alpha_num[end[dim]]; */
			/* } */
			/* start_char[dim] = '\0'; */
			/* end_char[dim] = '\0'; */
			/* info("_internal_removable_set_mps: setting %s x %s", */
			/*      start_char, end_char); */

			_internal_removable_set_mps(0, start, end, coords, 1);

			if (mps[j] != ',')
				break;
			j--;
		} else if ((mps[j] >= '0' && mps[j] <= '9')
			   || (mps[j] >= 'A' && mps[j] <= 'D')) {
			ba_mp_t *curr_mp;
			for (dim = 0; dim < SYSTEM_DIMENSIONS; dim++, j++)
				start[dim] = _coord(mps[j]);

			curr_mp = coord2ba_mp(start);
			curr_mp->used |= BA_MP_USED_TEMP;

			if (mps[j] != ',')
				break;
			j--;
		}
		j++;
	}
 	return SLURM_SUCCESS;
}

/*
 * Resets the virtual system to the pervious state before calling
 * removable_set_mps, or set_all_mps_except.
 */
extern int ba_reset_all_removed_mps()
{
	static int start[SYSTEM_DIMENSIONS];
	static int end[SYSTEM_DIMENSIONS];
	static int dim = 0;
	static int *dims = NULL;
	int coords[SYSTEM_DIMENSIONS];

	if (!dim) {
		dims = select_g_ba_get_dims();
		xassert(dims);
		memset(start, 0, sizeof(start));
		for (dim = 0; dim < SYSTEM_DIMENSIONS; dim++)
			end[dim] = dims[dim] - 1;
	}

	memset(coords, 0, sizeof(coords));
	_internal_removable_set_mps(0, start, end, coords, 0);

	return SLURM_SUCCESS;
}

/*
 * set the mp in the internal configuration as in, or not in use,
 * along with the current state of the mp.
 *
 * IN ba_mp: ba_mp_t to update state
 * IN state: new state of ba_mp_t
 */
extern void ba_update_mp_state(ba_mp_t *ba_mp, uint16_t state)
{
	uint16_t mp_base_state = state & NODE_STATE_BASE;
	uint16_t mp_flags = state & NODE_STATE_FLAGS;

	if (!ba_initialized){
		error("Error, configuration not initialized, "
		      "calling ba_init(NULL, 1)");
		ba_init(NULL, 1);
	}

	debug2("ba_update_mp_state: new state of [%s] is %s",
	       ba_mp->coord_str, node_state_string(state));

	/* basically set the mp as used */
	if ((mp_base_state == NODE_STATE_DOWN)
	    || (mp_flags & (NODE_STATE_DRAIN | NODE_STATE_FAIL)))
		ba_mp->used = BA_MP_USED_TRUE;
	else
		ba_mp->used = BA_MP_USED_FALSE;

	ba_mp->state = state;
}

/*
 * find a rack/midplace location
 */
extern char *find_mp_rack_mid(char* coords)
{
	ba_mp_t *curr_mp;

	if(!(curr_mp = str2ba_mp(coords)))
		return NULL;

	bridge_setup_system();

	return curr_mp->loc;
}

/* */
extern int validate_coord(uint16_t *coord)
{
	int dim, i;
	char coord_str[cluster_dims+1];
	char dim_str[cluster_dims+1];
	static int *dims = NULL;

	if (!dims)
		dims = select_g_ba_get_dims();
	xassert(!dims);

	for (dim=0; dim < cluster_dims; dim++) {
		if (coord[dim] >= dims[dim]) {
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP) {
				for (i=0; i<cluster_dims; i++) {
					coord_str[i] = alpha_num[coord[i]];
					dim_str[i] = alpha_num[dims[i]];
				}
				coord_str[i] = '\0';
				dim_str[i] = '\0';

				info("got coord %s greater than what "
				     "we are using %s", coord_str, dim_str);
			}
			return 0;
		}
	}

	return 1;
}

extern char *ba_switch_usage_str(uint16_t usage)
{
	switch (usage) {
	case BG_SWITCH_NONE:
		return "None";
	case BG_SWITCH_WRAPPED_PASS:
		return "WrappedPass";
	case BG_SWITCH_TORUS:
		return "FullTorus";
	case BG_SWITCH_PASS:
		return "Passthrough";
	case BG_SWITCH_WRAPPED:
		return "Wrapped";
	case (BG_SWITCH_OUT | BG_SWITCH_OUT_PASS):
		return "OutLeaving";
	case BG_SWITCH_OUT:
		return "Out";
	case (BG_SWITCH_IN | BG_SWITCH_IN_PASS):
		return "InComming";
	case BG_SWITCH_IN:
		return "In";
	default:
		error("unknown switch usage %u", usage);
		xassert(0);
		break;
	}
	return "unknown";
}

extern void set_ba_debug_flags(uint32_t debug_flags)
{
	ba_debug_flags = debug_flags;
}

