/*****************************************************************************\
 *  ba_common.c
 *
 *****************************************************************************
 *  Copyright (C) 2011 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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
#include "bg_node_info.h"
#ifndef HAVE_BG_L_P
# include "ba_bgq/block_allocator.h"
#else
# include "ba/block_allocator.h"
#endif

#define DISPLAY_FULL_DIM 1

#if (SYSTEM_DIMENSIONS == 1)
int cluster_dims = 1;
int cluster_base = 10;
#else
int cluster_dims = 3;
int cluster_base = 36;
#endif
uint32_t cluster_flags = 0;
uint16_t ba_deny_pass = 0;

ba_geo_combos_t geo_combos[LONGEST_BGQ_DIM_LEN];

bool ba_initialized = false;
uint64_t ba_debug_flags = 0;
int DIM_SIZE[HIGHEST_DIMENSIONS];
bitstr_t *ba_main_mp_bitmap = NULL;
pthread_mutex_t ba_system_mutex = PTHREAD_MUTEX_INITIALIZER;

static bool _check_deny_pass(int dim, uint16_t *deny_pass)
{
	uint16_t check = 0;

	/* return true by default */
	if (!deny_pass)
		return true;

	switch (dim) {
	case A:
		check = PASS_DENY_A;
		break;
	case X:
		check = PASS_DENY_X;
		break;
	case Y:
		check = PASS_DENY_Y;
		break;
	case Z:
		check = PASS_DENY_Z;
		break;
	default:
		error("unknown dim %d", dim);
		return 1;
		break;
	}

	if (*deny_pass & check)
		return 1;
	return 0;
}

static void _pack_ba_connection(ba_connection_t *ba_connection,
				Buf buffer, uint16_t protocol_version)
{
	int dim;
	for (dim=0; dim<SYSTEM_DIMENSIONS; dim++)
		pack16(ba_connection->mp_tar[dim], buffer);
	pack16(ba_connection->port_tar, buffer);
	pack16(ba_connection->used, buffer);
}

static int _unpack_ba_connection(ba_connection_t *ba_connection,
				 Buf buffer, uint16_t protocol_version)
{
	int dim;

	for (dim=0; dim<SYSTEM_DIMENSIONS; dim++)
		safe_unpack16(&ba_connection->mp_tar[dim], buffer);
	safe_unpack16(&ba_connection->port_tar, buffer);
	safe_unpack16(&ba_connection->used, buffer);

	return SLURM_SUCCESS;
unpack_error:
	return SLURM_ERROR;
}

static void _pack_ba_switch(ba_switch_t *ba_switch,
			    Buf buffer, uint16_t protocol_version)
{
	int i;

	if ((cluster_flags & CLUSTER_FLAG_BGL)
	    || (cluster_flags & CLUSTER_FLAG_BGP)) {
		for (i=0; i< NUM_PORTS_PER_NODE; i++) {
			_pack_ba_connection(&ba_switch->int_wire[i],
					    buffer, protocol_version);
			_pack_ba_connection(&ba_switch->ext_wire[i],
					    buffer, protocol_version);
		}
	}
	pack16(ba_switch->usage, buffer);
}

static int _unpack_ba_switch(ba_switch_t *ba_switch,
			     Buf buffer, uint16_t protocol_version)
{
	int i;

	if ((cluster_flags & CLUSTER_FLAG_BGL)
	    || (cluster_flags & CLUSTER_FLAG_BGP)) {
		for (i=0; i< NUM_PORTS_PER_NODE; i++) {
			if (_unpack_ba_connection(&ba_switch->int_wire[i],
						 buffer, protocol_version)
			   != SLURM_SUCCESS)
				goto unpack_error;
			if (_unpack_ba_connection(&ba_switch->ext_wire[i],
						 buffer, protocol_version)
			   != SLURM_SUCCESS)
				goto unpack_error;
		}
	}
	safe_unpack16(&ba_switch->usage, buffer);
	return SLURM_SUCCESS;

unpack_error:
	return SLURM_ERROR;
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

#if DISPLAY_FULL_DIM
/* Translate a 1-D offset in the cnode bitmap to a multi-dimension
 * coordinate (3-D, 4-D, 5-D, etc.) */
static void _ba_node_xlate_from_1d(int offset_1d, int *full_offset,
				   ba_geo_system_t *my_system_geo)
{
	int i, map_offset;

	xassert(full_offset);
	map_offset = offset_1d;
	for (i = 0; i < my_system_geo->dim_count; i++) {
		full_offset[i] = map_offset % my_system_geo->dim_size[i];
		map_offset /= my_system_geo->dim_size[i];
	}
}
#endif

static int _ba_node_map_set_range_internal(int level, uint16_t *coords,
					   int *start_offset, int *end_offset,
					   bitstr_t *node_bitmap,
					   ba_geo_system_t *my_geo_system)
{
	xassert(my_geo_system);

	if (level > my_geo_system->dim_count)
		return -1;

	if (level < my_geo_system->dim_count) {

		if (start_offset[level] > my_geo_system->dim_size[level]
		    || end_offset[level] > my_geo_system->dim_size[level])
			return -1;

		for (coords[level] = start_offset[level];
		     coords[level] <= end_offset[level];
		     coords[level]++) {
			/* handle the outter dims here */
			if (_ba_node_map_set_range_internal(
				    level+1, coords,
				    start_offset, end_offset,
				    node_bitmap, my_geo_system) == -1)
				return -1;
		}
		return 1;
	}

	ba_node_map_set(node_bitmap, coords, my_geo_system);
	return 1;
}

static ba_geo_combos_t *_build_geo_bitmap_arrays(int size)
{
	int i, j;
	ba_geo_combos_t *combos;
	int gap_start, max_gap_start;
	int gap_count, gap_len, max_gap_len;

	xassert(size > 0);
	combos = &geo_combos[size-1];
	combos->elem_count = (1 << size) - 1;
	combos->gap_count       = xmalloc(sizeof(int) * combos->elem_count);
	combos->has_wrap        = xmalloc(sizeof(bool) * combos->elem_count);
	combos->set_count_array = xmalloc(sizeof(int) * combos->elem_count);
	combos->set_bits_array  = xmalloc(sizeof(bitstr_t *) *
					  combos->elem_count);
	combos->start_coord = xmalloc(sizeof(uint16_t *) * combos->elem_count);
	combos->block_size  = xmalloc(sizeof(uint16_t *) * combos->elem_count);

	for (i = 1; i <= combos->elem_count; i++) {
		bool some_bit_set = false, some_gap_set = false;
		combos->set_bits_array[i-1] = bit_alloc(size);

		gap_count = 0;
		gap_start = -1;
		max_gap_start = -1;
		gap_len = 0;
		max_gap_len = 0;
		for (j = 0; j < size; j++) {
			if (((i >> j) & 0x1) == 0) {
				if (gap_len++ == 0) {
					gap_count++;
					gap_start = j;
				}
				if (some_bit_set)  /* ignore leading gap */
					some_gap_set = true;
				continue;
			}
			if (gap_len > max_gap_len) {
				max_gap_len = gap_len;
				max_gap_start = gap_start;
			}
			gap_len = 0;
			bit_set(combos->set_bits_array[i-1], j);
			combos->set_count_array[i-1]++;
			if (some_bit_set && some_gap_set)
				combos->has_wrap[i-1] = true;
			some_bit_set = true;
		}
		if (gap_len) {	/* test for wrap in gap */
			for (j = 0; j < size; j++) {
				if (bit_test(combos->set_bits_array[i-1], j))
					break;
				if (j == 0)
					gap_count--;
				gap_len++;
			}
			if (gap_len >= max_gap_len) {
				max_gap_len = gap_len;
				max_gap_start = gap_start;
			}
		}

		if (max_gap_len == 0) {
			combos->start_coord[i-1] = 0;
		} else {
			combos->start_coord[i-1] = (max_gap_start +
						    max_gap_len) % size;
		}
		combos->block_size[i-1] = size - max_gap_len;
		combos->gap_count[i-1]  = gap_count;
	}

#if 0
	info("geometry size=%d", size);
	for (i = 0; i < combos->elem_count; i++) {
		char buf[64];
		bit_fmt(buf, sizeof(buf), combos->set_bits_array[i]);
		info("cnt:%d bits:%10s start_coord:%u block_size:%u "
		     "gap_count:%d has_wrap:%d",
		     combos->set_count_array[i], buf,
		     combos->start_coord[i], combos->block_size[i],
		     combos->gap_count[i], (int)combos->has_wrap[i]);
	}
	info("\n\n");
#endif

	return combos;
}

static void _free_geo_bitmap_arrays(void)
{
	int i, j;
	ba_geo_combos_t *combos;

	for (i = 1; i <= LONGEST_BGQ_DIM_LEN; i++) {
		combos = &geo_combos[i-1];
		for (j = 0; j < combos->elem_count; j++) {
			if (combos->set_bits_array[j])
				bit_free(combos->set_bits_array[j]);
		}
		xfree(combos->gap_count);
		xfree(combos->has_wrap);
		xfree(combos->set_count_array);
		xfree(combos->set_bits_array);
		xfree(combos->start_coord);
		xfree(combos->block_size);
	}
}

/* Find the next element in the geo_combinations array in a given dimension
 * that contains req_bit_cnt elements to use. Return -1 if none found. */
static int _find_next_geo_inx(ba_geo_combos_t *geo_combo_ptr,
			      int last_inx, uint16_t req_bit_cnt,
			      bool deny_pass, bool deny_wrap)
{
	while (++last_inx < geo_combo_ptr->elem_count) {
		if ((req_bit_cnt == geo_combo_ptr->set_count_array[last_inx])&&
		    (!deny_pass || (geo_combo_ptr->gap_count[last_inx] < 2)) &&
		    (!deny_wrap || !geo_combo_ptr->has_wrap[last_inx]))
			return last_inx;
	}
	return -1;
}

/* Determine if a specific set of elements in each dimension is available.
 * Return a bitmap of that set of elements if free, NULL otherwise. */
static bitstr_t * _test_geo(bitstr_t *node_bitmap,
			    ba_geo_system_t *my_geo_system,
			    ba_geo_combos_t **geo_array, int *geo_array_inx)
{
	int i;
	bitstr_t *alloc_node_bitmap;
	uint16_t offset[my_geo_system->dim_count];

	alloc_node_bitmap = bit_alloc(my_geo_system->total_size);
	memset(offset, 0, sizeof(offset));
	while (1) {
		/* Test if this coordinate is required in every dimension */
		for (i = 0; i < my_geo_system->dim_count; i++) {
			if (!bit_test(geo_array[i]->
				      set_bits_array[geo_array_inx[i]],
				      offset[i]))
				break;	/* not needed */
		}
		/* Test if this coordinate is available for use */
		if (i >= my_geo_system->dim_count) {
			if (ba_node_map_test(
				    node_bitmap, offset, my_geo_system))
				break;	/* not available */
			/* Set it in our bitmap for this job */
			ba_node_map_set(alloc_node_bitmap, offset,
					my_geo_system);
		}
		/* Go to next coordinate */
		for (i = 0; i < my_geo_system->dim_count; i++) {
			if (++offset[i] < my_geo_system->dim_size[i])
				break;
			offset[i] = 0;
		}
		if (i >= my_geo_system->dim_count) {
			/* All bits in every dimension tested */
			return alloc_node_bitmap;
		}
	}
	bit_free(alloc_node_bitmap);
	return NULL;
}

/* Attempt to place an allocation of a specific required geomemtry (geo_req)
 * into a bitmap of available resources (node_bitmap). The resource allocation
 * may contain gaps in multiple dimensions. */
static int _geo_test_maps(bitstr_t *node_bitmap,
			  bitstr_t **alloc_node_bitmap,
			  ba_geo_table_t *geo_req, int *attempt_cnt,
			  ba_geo_system_t *my_geo_system, uint16_t *deny_pass,
			  uint16_t *start_pos, int *scan_offset,
			  bool deny_wrap)
{
	int i, current_offset = -1;
	ba_geo_combos_t *geo_array[my_geo_system->dim_count];
	int geo_array_inx[my_geo_system->dim_count];
	bool dim_deny_pass;

	for (i = 0; i < my_geo_system->dim_count; i++) {
		if (my_geo_system->dim_size[i] > LONGEST_BGQ_DIM_LEN) {
			error("System geometry specification larger than "
			      "configured LONGEST_BGQ_DIM_LEN. Increase "
			      "LONGEST_BGQ_DIM_LEN (%d)", LONGEST_BGQ_DIM_LEN);
			return SLURM_ERROR;
		}
		dim_deny_pass = _check_deny_pass(i, deny_pass);

		geo_array[i] = &geo_combos[my_geo_system->dim_size[i] - 1];
		geo_array_inx[i] = _find_next_geo_inx(geo_array[i], -1,
						      geo_req->geometry[i],
						      dim_deny_pass,
						      deny_wrap);
		if (geo_array_inx[i] == -1) {
			error("Request to allocate %u nodes in dimension %d, "
			      "which only has %d elements",
			      geo_req->geometry[i], i,
			      my_geo_system->dim_size[i]);
			return SLURM_ERROR;
		}
	}

	*alloc_node_bitmap = (bitstr_t *) NULL;
	while (1) {
		current_offset++;
		if (!scan_offset || (current_offset >= *scan_offset)) {
			(*attempt_cnt)++;
			*alloc_node_bitmap = _test_geo(node_bitmap,
						       my_geo_system,
						       geo_array,
						       geo_array_inx);
			if (*alloc_node_bitmap)
				break;
		}

		/* Increment offsets */
		for (i = 0; i < my_geo_system->dim_count; i++) {
			dim_deny_pass = _check_deny_pass(i, deny_pass);
			geo_array_inx[i] = _find_next_geo_inx(geo_array[i],
							geo_array_inx[i],
						     	geo_req->geometry[i],
						     	dim_deny_pass,
						     	deny_wrap);
			if (geo_array_inx[i] != -1)
				break;
			geo_array_inx[i] = _find_next_geo_inx(geo_array[i], -1,
							geo_req->geometry[i],
						     	dim_deny_pass,
						     	deny_wrap);
		}
		if (i >= my_geo_system->dim_count)
			return SLURM_ERROR;
	}

	if (start_pos) {
		for (i = 0; i < my_geo_system->dim_count; i++) {
			start_pos[i] = geo_array[i]->
				       start_coord[geo_array_inx[i]];
		}
	}
	if (scan_offset)
		*scan_offset = current_offset + 1;
	return SLURM_SUCCESS;
}

static void _internal_removable_set_mps(int level, bitstr_t *bitmap,
					uint16_t *coords, bool mark,
					bool except)
{
	ba_mp_t *curr_mp;
	int is_set;

	if (level > cluster_dims)
		return;

	if (level < cluster_dims) {
		for (coords[level] = 0;
		     coords[level] < DIM_SIZE[level];
		     coords[level]++) {
			/* handle the outer dims here */
			_internal_removable_set_mps(
				level+1, bitmap, coords, mark, except);
		}
		return;
	}

	slurm_mutex_lock(&ba_system_mutex);
	if (!(curr_mp = coord2ba_mp(coords))) {
		slurm_mutex_unlock(&ba_system_mutex);
		return;
	}
	if (bitmap)
		is_set = bit_test(bitmap, curr_mp->index);
	if (!bitmap || (is_set && !except) || (!is_set && except)) {
		if (mark) {
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
				info("can't use %s", curr_mp->coord_str);
			curr_mp->used |= BA_MP_USED_TEMP;
			bit_set(ba_main_mp_bitmap, curr_mp->ba_geo_index);
		} else {
			curr_mp->used &= (~BA_MP_USED_TEMP);
			if (curr_mp->used == BA_MP_USED_FALSE)
				bit_clear(ba_main_mp_bitmap,
					  curr_mp->ba_geo_index);
		}
	}
	slurm_mutex_unlock(&ba_system_mutex);
}

static void _internal_reset_ba_system(int level, uint16_t *coords,
				      bool track_down_mps)
{
	ba_mp_t *curr_mp;

	if (level > cluster_dims)
		return;

	if (level < cluster_dims) {
		for (coords[level] = 0;
		     coords[level] < DIM_SIZE[level];
		     coords[level]++) {
			/* handle the outer dims here */
			_internal_reset_ba_system(
				level+1, coords, track_down_mps);
		}
		return;
	}
	slurm_mutex_lock(&ba_system_mutex);
	if (!(curr_mp = coord2ba_mp(coords))) {
		slurm_mutex_unlock(&ba_system_mutex);
		return;
	}
	ba_setup_mp(curr_mp, track_down_mps, false);
	bit_clear(ba_main_mp_bitmap, curr_mp->ba_geo_index);
	slurm_mutex_unlock(&ba_system_mutex);
}

#if defined HAVE_BG_FILES
/* ba_system_mutex should be locked before calling. */
static ba_mp_t *_internal_loc2ba_mp(int level, uint16_t *coords,
				    const char *check)
{
	ba_mp_t *curr_mp = NULL;

	if (!check || (level > cluster_dims))
		return NULL;

	if (level < cluster_dims) {
		for (coords[level] = 0;
		     coords[level] < DIM_SIZE[level];
		     coords[level]++) {
			/* handle the outer dims here */
			if ((curr_mp = _internal_loc2ba_mp(
				     level+1, coords, check)))
				break;
		}
		return curr_mp;
	}

	curr_mp = coord2ba_mp(coords);
	if (!curr_mp)
		return NULL;
	if (strcasecmp(check, curr_mp->loc))
		curr_mp = NULL;

	return curr_mp;
}
#endif

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
	int real_dims[HIGHEST_DIMENSIONS];
	char dim_str[HIGHEST_DIMENSIONS+1];

	/* We only need to initialize once, so return if already done so. */
	if (ba_initialized)
		return;

	cluster_dims = slurmdb_setup_cluster_dims();
	cluster_flags = slurmdb_setup_cluster_flags();
	set_ba_debug_flags(slurm_get_debug_flags());
	if (bg_recover != NOT_FROM_CONTROLLER)
		bridge_init("");

	memset(coords, 0, sizeof(coords));
	memset(DIM_SIZE, 0, sizeof(DIM_SIZE));
	memset(real_dims, 0, sizeof(real_dims));
	memset(dim_str, 0, sizeof(dim_str));
	/* cluster_dims is already set up off of working_cluster_rec */
	if (cluster_dims == 1) {
		if (node_info_ptr) {
			real_dims[0] = DIM_SIZE[0]
				= node_info_ptr->record_count;
			for (i=1; i<cluster_dims; i++)
				real_dims[i] = DIM_SIZE[i] = 1;
		}
		goto setup_done;
	} else if (working_cluster_rec && working_cluster_rec->dim_size) {
		for(i=0; i<cluster_dims; i++) {
			real_dims[i] = DIM_SIZE[i] =
				working_cluster_rec->dim_size[i];
		}
		goto setup_done;
	}


	if (node_info_ptr) {
		for (i = 0; i < (int)node_info_ptr->record_count; i++) {
			node_ptr = &node_info_ptr->node_array[i];
			number = 0;

			if (!node_ptr->name) {
				memset(DIM_SIZE, 0, sizeof(DIM_SIZE));
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
				number = xstrntol(numeric, NULL, cluster_dims,
						  cluster_base);
				break;
			}
			hostlist_parse_int_to_array(
				number, coords, cluster_dims, cluster_base);
			memcpy(DIM_SIZE, coords, sizeof(DIM_SIZE));
		}
		for (j=0; j<cluster_dims; j++) {
			DIM_SIZE[j]++;
			/* this will probably be reset below */
			real_dims[j] = DIM_SIZE[j];
		}
	}
node_info_error:
	for (j=0; j<cluster_dims; j++)
		if (!DIM_SIZE[j])
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
					DIM_SIZE[k] = MAX(DIM_SIZE[k],
							  select_char2coord(
								  nodes[j]));
				if (nodes[j] != ',')
					break;
			}
		}

		for (j=0; j<cluster_dims; j++)
			if (DIM_SIZE[j])
				break;

		if (j >= cluster_dims)
			info("are you sure you only have 1 midplane? %s",
			     ptr_array[0]->nodenames);

		for (j=0; j<cluster_dims; j++) {
			DIM_SIZE[j]++;
			/* this will probably be reset below */
			real_dims[j] = DIM_SIZE[j];
		}
	}

	/* sanity check.  We can only request part of the system, but
	   we don't want to allow more than we have. */
	if (sanity_check && (bg_recover != NOT_FROM_CONTROLLER)) {
		verbose("Attempting to contact MMCS");
		if (bridge_get_size(real_dims) == SLURM_SUCCESS) {
			char real_dim_str[cluster_dims+1];
			memset(real_dim_str, 0, sizeof(real_dim_str));
			for (i=0; i<cluster_dims; i++) {
				dim_str[i] = alpha_num[DIM_SIZE[i]];
				real_dim_str[i] = alpha_num[real_dims[i]];
			}
			verbose("BlueGene configured with %s midplanes",
				real_dim_str);
			for (i=0; i<cluster_dims; i++)
				if (DIM_SIZE[i] > real_dims[i])
					fatal("You requested a %s system, "
					      "but we only have a "
					      "system of %s.  "
					      "Change your slurm.conf.",
					      dim_str, real_dim_str);
		}
	}

setup_done:
	if (cluster_dims == 1) {
		if (!DIM_SIZE[0]) {
			debug("Setting default system dimensions");
			real_dims[0] = DIM_SIZE[0] = 100;
			for (i=1; i<cluster_dims; i++)
				real_dims[i] = DIM_SIZE[i] = 1;
		}
	} else {
		for (i=0; i<cluster_dims; i++)
			dim_str[i] = alpha_num[DIM_SIZE[i]];
		debug("We are using %s of the system.", dim_str);
	}

	ba_initialized = true;

	if (bg_recover != NOT_FROM_CONTROLLER)
		ba_setup_wires();
}


/**
 * destroy all the internal (global) data structs.
 */
extern void ba_fini(void)
{
	if (!ba_initialized)
		return;

	if (bg_recover != NOT_FROM_CONTROLLER) {
		bridge_fini();
		ba_destroy_system();
		_free_geo_bitmap_arrays();
	}

	if (ba_main_mp_bitmap)
		FREE_NULL_BITMAP(ba_main_mp_bitmap);

	ba_initialized = false;

//	debug3("pa system destroyed");
}

extern void ba_setup_wires(void)
{
	int num_mps, i;
	static bool wires_setup = 0;

	if (!ba_initialized || wires_setup)
		return;

	wires_setup = 1;

	num_mps = 1;
	for (i=0; i<cluster_dims; i++)
		num_mps *= DIM_SIZE[i];

	ba_main_mp_bitmap = bit_alloc(num_mps);

	ba_create_system();
	bridge_setup_system();

	for (i = 1; i <= LONGEST_BGQ_DIM_LEN; i++)
		_build_geo_bitmap_arrays(i);
}

extern void free_internal_ba_mp(ba_mp_t *ba_mp)
{
	if (ba_mp) {
		FREE_NULL_BITMAP(ba_mp->cnode_bitmap);
		FREE_NULL_BITMAP(ba_mp->cnode_err_bitmap);
		FREE_NULL_BITMAP(ba_mp->cnode_usable_bitmap);
		xfree(ba_mp->loc);
		if (ba_mp->nodecard_loc) {
			int i;
			for (i=0; i<bg_conf->mp_nodecard_cnt; i++)
				xfree(ba_mp->nodecard_loc[i]);
			xfree(ba_mp->nodecard_loc);
		}

	}
}

extern void destroy_ba_mp(void *ptr)
{
	ba_mp_t *ba_mp = (ba_mp_t *)ptr;
	if (ba_mp) {
		free_internal_ba_mp(ba_mp);
		xfree(ba_mp);
	}
}

extern void pack_ba_mp(ba_mp_t *ba_mp, Buf buffer, uint16_t protocol_version)
{
	int dim;

	xassert(ba_mp);
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		for (dim = 0; dim < SYSTEM_DIMENSIONS; dim++) {
			_pack_ba_switch(&ba_mp->axis_switch[dim], buffer,
					protocol_version);
			pack16(ba_mp->coord[dim], buffer);
			/* No need to pack the coord_str, we can figure that
			   out from the coords packed.
			*/
		}
		/* currently there is no need to pack
		 * ba_mp->cnode_bitmap */

		/* currently there is no need to pack
		 * ba_mp->cnode_err_bitmap */

		pack_bit_fmt(ba_mp->cnode_usable_bitmap, buffer);

		pack16(ba_mp->used, buffer);
		/* These are only used on the original, not in the
		   block ba_mp's.
		   ba_mp->alter_switch, ba_mp->index, ba_mp->loc,
		   ba_mp->next_mp, ba_mp->nodecard_loc,
		   ba_mp->prev_mp, ba_mp->state
		*/
	} else {
 		error("pack_ba_mp: protocol_version "
 		      "%hu not supported", protocol_version);
	}
}

extern int unpack_ba_mp(ba_mp_t **ba_mp_pptr,
			Buf buffer, uint16_t protocol_version)
{
	int dim;
	ba_mp_t *orig_mp = NULL;
	ba_mp_t *ba_mp = xmalloc(sizeof(ba_mp_t));
	char *bit_char;
	uint32_t uint32_tmp;

	*ba_mp_pptr = ba_mp;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		for (dim = 0; dim < SYSTEM_DIMENSIONS; dim++) {
			if (_unpack_ba_switch(&ba_mp->axis_switch[dim], buffer,
					      protocol_version)
			    != SLURM_SUCCESS)
				goto unpack_error;
			safe_unpack16(&ba_mp->coord[dim], buffer);
			ba_mp->coord_str[dim] = alpha_num[ba_mp->coord[dim]];
		}
		ba_mp->coord_str[dim] = '\0';

		safe_unpackstr_xmalloc(&bit_char, &uint32_tmp, buffer);
		if (bit_char) {
			ba_mp->cnode_usable_bitmap =
				bit_alloc(bg_conf->mp_cnode_cnt);
			bit_unfmt(ba_mp->cnode_usable_bitmap, bit_char);
			xfree(bit_char);
			ba_mp->cnode_bitmap =
				bit_copy(ba_mp->cnode_usable_bitmap);
		}
		safe_unpack16(&ba_mp->used, buffer);

		/* Since index could of changed here we will go figure
		 * it out again. */
		slurm_mutex_lock(&ba_system_mutex);
		if (!(orig_mp = coord2ba_mp(ba_mp->coord))) {
			slurm_mutex_unlock(&ba_system_mutex);
			goto unpack_error;
		}
		ba_mp->index = orig_mp->index;
		ba_mp->ba_geo_index = orig_mp->ba_geo_index;
		slurm_mutex_unlock(&ba_system_mutex);
	} else {
 		error("unpack_ba_mp: protocol_version "
 		      "%hu not supported", protocol_version);
	}
	return SLURM_SUCCESS;

unpack_error:
	destroy_ba_mp(ba_mp);
	*ba_mp_pptr = NULL;
	return SLURM_ERROR;
}

/* If used in the bluegene plugin this ba_system_mutex must be
 * locked. Don't work about it in programs like smap.
 */
extern ba_mp_t *str2ba_mp(const char *coords)
{
	uint16_t coord[cluster_dims];
	int len, dim;

	if (!coords)
		return NULL;
	len = strlen(coords) - cluster_dims;
	if (len < 0)
		return NULL;

	for (dim = 0; dim < cluster_dims; dim++, len++) {
		coord[dim] = select_char2coord(coords[len]);
		if (coord[dim] > DIM_SIZE[dim])
			break;
	}

	if (dim < cluster_dims) {
		char tmp_char[cluster_dims+1];
		memset(tmp_char, 0, sizeof(tmp_char));
		for (dim=0; dim<cluster_dims; dim++)
			tmp_char[dim] = alpha_num[DIM_SIZE[dim]];
		error("This location %s is not possible in our system %s",
		      coords, tmp_char);
		return NULL;
	}

	if (bridge_setup_system() != SLURM_SUCCESS)
		return NULL;

	return coord2ba_mp(coord);
}

/*
 * find a base blocks bg location (rack/midplane)
 * If used in the bluegene plugin this ba_system_mutex must be
 * locked. Don't work about it in programs like smap.
 */
extern ba_mp_t *loc2ba_mp(const char* mp_id)
{
#if defined HAVE_BG_FILES
	char *check = NULL;
	ba_mp_t *ba_mp = NULL;
	uint16_t coords[SYSTEM_DIMENSIONS];

	if (bridge_setup_system() == -1)
		return NULL;

	check = xstrdup(mp_id);
	/* with BGP they changed the names of the rack midplane action from
	 * R000 to R00-M0 so we now support both formats for each of the
	 * systems */
#ifdef HAVE_BGL
	if (check[3] == '-') {
		if (check[5]) {
			check[3] = check[5];
			check[4] = '\0';
		}
	}

	if ((check[1] < '0' || check[1] > '9')
	    || (check[2] < '0' || check[2] > '9')
	    || (check[3] < '0' || check[3] > '9')) {
		error("%s is not a valid Rack-Midplane (i.e. R000)", mp_id);
		goto cleanup;
	}

#else
	if (check[3] != '-') {
		xfree(check);
		check = xstrdup_printf("R%c%c-M%c",
				       mp_id[1], mp_id[2], mp_id[3]);
	}

	if ((select_char2coord(check[1]) == -1)
	    || (select_char2coord(check[2]) == -1)
	    || (select_char2coord(check[5]) == -1)) {
		error("%s is not a valid Rack-Midplane (i.e. R00-M0)", mp_id);
		goto cleanup;
	}
#endif

	ba_mp = _internal_loc2ba_mp(0, coords, check);
cleanup:
	xfree(check);

	return ba_mp;
#else
	return NULL;
#endif
}

extern void ba_setup_mp(ba_mp_t *ba_mp, bool track_down_mps, bool wrap_it)
{
	int i;
	uint16_t node_base_state = ba_mp->state & NODE_STATE_BASE;

	if (!track_down_mps || ((node_base_state != NODE_STATE_DOWN)
				&& !(ba_mp->state & NODE_STATE_DRAIN)))
		ba_mp->used = BA_MP_USED_FALSE;

	for (i=0; i<cluster_dims; i++) {
		bool set_error = 0;
#ifdef HAVE_BG_L_P
		int j;
		for (j=0;j<NUM_PORTS_PER_NODE;j++) {
			ba_mp->axis_switch[i].int_wire[j].used = 0;
			if (i!=0) {
				if (j==3 || j==4)
					ba_mp->axis_switch[i].int_wire[j].
						used = 1;
			}
			ba_mp->axis_switch[i].int_wire[j].port_tar = j;
		}
#endif
		if (ba_mp->axis_switch[i].usage & BG_SWITCH_CABLE_ERROR)
			set_error = 1;

		if (wrap_it)
			ba_mp->axis_switch[i].usage = BG_SWITCH_WRAPPED;
		else
			ba_mp->axis_switch[i].usage = BG_SWITCH_NONE;

		if (set_error) {
			if (track_down_mps)
				ba_mp->axis_switch[i].usage
					|= BG_SWITCH_CABLE_ERROR_FULL;
			else
				ba_mp->axis_switch[i].usage
					|= BG_SWITCH_CABLE_ERROR;
		}
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

	/* we have to set this or we would be pointing to the original */
	memset(new_ba_mp->next_mp, 0, sizeof(new_ba_mp->next_mp));
	/* we have to set this or we would be pointing to the original */
	memset(new_ba_mp->prev_mp, 0, sizeof(new_ba_mp->prev_mp));
	/* These are only used on the original as well. */
	new_ba_mp->nodecard_loc = NULL;
	new_ba_mp->loc = NULL;
	new_ba_mp->cnode_bitmap = NULL;
	new_ba_mp->cnode_err_bitmap = NULL;
	new_ba_mp->cnode_usable_bitmap = NULL;

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
	snprintf(dim_buf, sizeof(dim_buf),
		 ": size:%u : full_dim_cnt:%u passthru_cnt:%u",
		 geo_ptr->size, geo_ptr->full_dim_cnt, geo_ptr->passthru_cnt);
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

extern void ba_create_geo_table(ba_geo_system_t *my_geo_system,
				bool avoid_three)
{
	ba_geo_table_t *geo_ptr;
	int dim, inx[my_geo_system->dim_count], passthru, product;
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
		bool found_three = 0;
		/* Store new value */
		geo_ptr = xmalloc(sizeof(ba_geo_table_t));
		geo_ptr->geometry = xmalloc(sizeof(uint16_t) *
					    my_geo_system->dim_count);
		product = 1;
		for (dim = 0; dim < my_geo_system->dim_count; dim++) {
			if (avoid_three && (inx[dim] == 3)) {
				found_three = 1;
				goto next_geo;
			}
			geo_ptr->geometry[dim] = inx[dim];
			product *= inx[dim];
			passthru = my_geo_system->dim_size[dim] - inx[dim];
			if (passthru == 0)
				geo_ptr->full_dim_cnt++;
			else if ((passthru > 1) && (inx[dim] > 1))
				geo_ptr->passthru_cnt += passthru;
		}
		geo_ptr->size = product;
		xassert(product <= my_geo_system->total_size);
		my_geo_system->geo_table_size++;
		/* Insert record into linked list so that geometries
		 * with full dimensions appear first */
		last_pptr = &my_geo_system->geo_table_ptr[product];
		while (*last_pptr) {
			if (geo_ptr->full_dim_cnt > (*last_pptr)->full_dim_cnt)
				break;
			if ((geo_ptr->full_dim_cnt ==
			     (*last_pptr)->full_dim_cnt) &&
			    (geo_ptr->passthru_cnt <
			     (*last_pptr)->passthru_cnt))
				break;
			last_pptr = &((*last_pptr)->next_ptr);
		}
		geo_ptr->next_ptr = *last_pptr;
		*last_pptr = geo_ptr;
	next_geo:
		if (found_three) {
			xfree(geo_ptr->geometry);
			xfree(geo_ptr);
		}
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
 * IN/OUT node_bitmap - bitmap of currently allocated nodes
 * IN full_offset - N-dimension zero-origin offset to set
 * IN my_geo_system - system geometry specification
 */
extern void ba_node_map_set(bitstr_t *node_bitmap, uint16_t *full_offset,
			    ba_geo_system_t *my_geo_system)
{
	bit_set(node_bitmap, ba_node_xlate_to_1d(full_offset, my_geo_system));
}

/*
 * Set the contents of the specified position in the bitmap
 * IN/OUT node_bitmap - bitmap of currently allocated nodes
 * IN start_offset - N-dimension zero-origin offset to start setting at
 * IN end_offset - N-dimension zero-origin offset to start setting at
 * IN my_geo_system - system geometry specification
 */
extern int ba_node_map_set_range(bitstr_t *node_bitmap,
				 int *start_offset, int *end_offset,
				 ba_geo_system_t *my_geo_system)
{
	uint16_t coords[HIGHEST_DIMENSIONS] = {};

	return _ba_node_map_set_range_internal(
		0, coords, start_offset, end_offset,
		node_bitmap, my_geo_system);
}

/*
 * Return the contents of the specified position in the bitmap
 * IN node_bitmap - bitmap of currently allocated nodes
 * IN full_offset - N-dimension zero-origin offset to test
 * IN my_geo_system - system geometry specification
 */
extern int ba_node_map_test(bitstr_t *node_bitmap, uint16_t *full_offset,
			    ba_geo_system_t *my_geo_system)
{
	return bit_test(node_bitmap,
			ba_node_xlate_to_1d(full_offset, my_geo_system));
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
			info("%s   inx:%d", full_buf, i);
		}
	}
}
#endif
}

/*
 * give a hostlist version of the contents of a node map created by
 *	ba_node_map_alloc() or
 *	ba_geo_test_all(). Output may be in one-dimension or more depending
 *	upon configuration.
 * IN node_bitmap - bitmap representing current system state, bits are set
 *                  for currently allocated nodes
 * IN my_geo_system - system geometry specification
 * OUT char * - needs to be xfreed from caller.
 */
extern char *ba_node_map_ranged_hostlist(bitstr_t *node_bitmap,
					 ba_geo_system_t *my_geo_system)
{
#if DISPLAY_1D
{
	char out_buf[256];
	bit_fmt(out_buf, sizeof(out_buf), node_bitmap);
	return xstrdup(out_buf);
}
#endif
#if DISPLAY_FULL_DIM
{
	int i, j, offset[my_geo_system->dim_count];
	hostlist_t hl = NULL;
	char *ret_str = NULL;

	xassert(node_bitmap);
	xassert(bit_size(node_bitmap) == my_geo_system->total_size);

	for (i = 0; i < my_geo_system->total_size; i++) {
		if (bit_test(node_bitmap, i)) {
			char dim_buf[my_geo_system->dim_count+1];

			_ba_node_xlate_from_1d(i, offset, my_geo_system);
			for (j = 0; j < my_geo_system->dim_count; j++) {
				dim_buf[j] = alpha_num[offset[j]];
			}
			dim_buf[j] = '\0';
			/* info("pushing %s", dim_buf); */
			if (hl)
				hostlist_push_host_dims(
					hl, dim_buf, my_geo_system->dim_count);
			else
				hl = hostlist_create_dims(
					dim_buf, my_geo_system->dim_count);
		}
	}
	if (hl) {
		ret_str = hostlist_ranged_string_xmalloc_dims(
			hl, my_geo_system->dim_count, 0);
		/* info("ret is %s", ret_str); */
		hostlist_destroy(hl);
		hl = NULL;
	}
	return ret_str;
}
#endif
}

/*
 * Attempt to place a new allocation into an existing node state.
 * Do not rotate or change the requested geometry, but do attempt to place
 * it using all possible starting locations.
 *
 * IN node_bitmap - bitmap representing current system state, bits are set
 *		for currently allocated nodes
 * OUT alloc_node_bitmap - bitmap representing where to place the allocation
 *		set only if RET == SLURM_SUCCESS
 * IN geo_req - geometry required for the new allocation
 * OUT attempt_cnt - number of job placements attempted
 * IN my_geo_system - system geometry specification
 * IN deny_pass - if set, then do not allow gaps in a specific dimension, any
 *		gap applies to all elements at that position in that dimension,
 *		one value per dimension, default value prevents gaps in any
 *		dimension
 * IN/OUT start_pos - input is pointer to array having same size as
 *		dimension count or NULL. Set to starting coordinates of
 *		the allocation in each dimension.
 * IN/OUT scan_offset - Location in search table from which to continue
 *		searching for resources. Initial value should be zero. If the
 *		allocation selected by the algorithm is not acceptable, call
 *		the function repeatedly with the previous output value of
 *		scan_offset
 * IN deny_wrap - If set then do not permit the allocation to wrap (i.e. in
 *		a dimension with a count of 4, 3 does not connect to 0)
 * RET - SLURM_SUCCESS if allocation can be made, otherwise SLURM_ERROR
 */
extern int ba_geo_test_all(bitstr_t *node_bitmap,
			   bitstr_t **alloc_node_bitmap,
			   ba_geo_table_t *geo_req, int *attempt_cnt,
			   ba_geo_system_t *my_geo_system, uint16_t *deny_pass,
			   uint16_t *start_pos, int *scan_offset,
			   bool deny_wrap)
{
	int rc;

	xassert(node_bitmap);
	xassert(alloc_node_bitmap);
	xassert(geo_req);
	xassert(attempt_cnt);

	*attempt_cnt = 0;
	rc = _geo_test_maps(node_bitmap, alloc_node_bitmap, geo_req,
			    attempt_cnt, my_geo_system, deny_pass,
			    start_pos, scan_offset, deny_wrap);

	return rc;
}

/* Translate a multi-dimension coordinate (3-D, 4-D, 5-D, etc.) into a 1-D
 * offset in the cnode* bitmap */
extern int ba_node_xlate_to_1d(uint16_t *full_offset,
			       ba_geo_system_t *my_geo_system)
{
	int i, map_offset;

	if (full_offset == NULL) {
		fatal("%s: full_offset is NULL", __func__);
		return SLURM_ERROR;
	}
	if (my_geo_system == NULL) {
		fatal("%s: my_geo_system is NULL", __func__);
		return SLURM_ERROR;
	}

	i = my_geo_system->dim_count - 1;
	map_offset = full_offset[i];
	for (i-- ; i >= 0; i--) {
		map_offset *= my_geo_system->dim_size[i];
		map_offset += full_offset[i];
	}
	return map_offset;
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
extern int ba_set_removable_mps(bitstr_t* bitmap, bool except)
{
	uint16_t coords[SYSTEM_DIMENSIONS];

	if (!bitmap)
		return SLURM_ERROR;

	/* return on empty sets */
	if (except) {
		if (bit_ffc(bitmap) == -1)
			return SLURM_SUCCESS;
	} else if (bit_ffs(bitmap) == -1)
		return SLURM_SUCCESS;

	_internal_removable_set_mps(0, bitmap, coords, 1, except);
	return SLURM_SUCCESS;
}

/*
 * Resets the virtual system to the pervious state before calling
 * removable_set_mps, or set_all_mps_except.
 */
extern int ba_reset_all_removed_mps(void)
{
	uint16_t coords[SYSTEM_DIMENSIONS];
	_internal_removable_set_mps(0, NULL, coords, 0, 0);
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
		ba_mp->used |= BA_MP_USED_TRUE;
	else
		ba_mp->used &= (~BA_MP_USED_TRUE);

	ba_mp->state = state;
}

/* */
extern int validate_coord(uint16_t *coord)
{
	int dim, i;
	char coord_str[cluster_dims+1];
	char dim_str[cluster_dims+1];

	for (dim=0; dim < cluster_dims; dim++) {
		if (coord[dim] >= DIM_SIZE[dim]) {
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP) {
				for (i=0; i<cluster_dims; i++) {
					coord_str[i] = alpha_num[coord[i]];
					dim_str[i] = alpha_num[DIM_SIZE[i]];
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
	bool error_set = (usage & BG_SWITCH_CABLE_ERROR);
	uint16_t local_usage = usage;

	if (error_set)
		local_usage &= (~BG_SWITCH_CABLE_ERROR_FULL);

	switch (local_usage) {
	case BG_SWITCH_NONE:
		if (error_set)
			return "ErrorOut";
		return "None";
	case BG_SWITCH_WRAPPED_PASS:
		if (error_set)
			return "WrappedPass,ErrorOut";
		return "WrappedPass";
	case BG_SWITCH_TORUS:
		if (error_set)
			return "FullTorus,ErrorOut";
		return "FullTorus";
	case BG_SWITCH_PASS:
		if (error_set)
			return "Passthrough,ErrorOut";
		return "Passthrough";
	case BG_SWITCH_WRAPPED:
		if (error_set)
			return "Wrapped,ErrorOut";
		return "Wrapped";
	case (BG_SWITCH_OUT | BG_SWITCH_OUT_PASS):
		if (error_set)
			return "OutLeaving,ErrorOut";
		return "OutLeaving";
	case BG_SWITCH_OUT:
		if (error_set)
			return "ErrorOut";
		return "Out";
	case (BG_SWITCH_IN | BG_SWITCH_IN_PASS):
		if (error_set)
			return "InComming,ErrorOut";
		return "InComming";
	case BG_SWITCH_IN:
		if (error_set)
			return "In,ErrorOut";
		return "In";
	default:
		error("unknown switch usage %u %u", usage, local_usage);
		xassert(0);
		break;
	}
	return "unknown";
}

extern void set_ba_debug_flags(uint64_t debug_flags)
{
	ba_debug_flags = debug_flags;
}

/*
 * Resets the virtual system to a virgin state.  If track_down_mps is set
 * then those midplanes are not set to idle, but kept in a down state.
 */
extern void reset_ba_system(bool track_down_mps)
{
	uint16_t coords[SYSTEM_DIMENSIONS];

	_internal_reset_ba_system(0, coords, track_down_mps);
}

extern char *ba_passthroughs_string(uint16_t passthrough)
{
	char *pass = NULL;

	if (passthrough & PASS_FOUND_A)
		xstrcat(pass, "A");
	if (passthrough & PASS_FOUND_X) {
		if (pass)
			xstrcat(pass, ",X");
		else
			xstrcat(pass, "X");
	}
	if (passthrough & PASS_FOUND_Y) {
		if (pass)
			xstrcat(pass, ",Y");
		else
			xstrcat(pass, "Y");
	}
	if (passthrough & PASS_FOUND_Z) {
		if (pass)
			xstrcat(pass, ",Z");
		else
			xstrcat(pass, "Z");
	}

	return pass;
}

/* This is defined here so we can get it on non-bluegene systems since
 * it is needed in pack/unpack functions, and bluegene.c isn't
 * compiled for non-bluegene machines, and it didn't make since to
 * compile the whole file just for this one function.
 */
extern char *give_geo(uint16_t *int_geo, int dims, bool with_sep)
{
	char *geo = NULL;
	int i;

	for (i=0; i<dims; i++) {
		if (geo && with_sep)
			xstrcat(geo, "x");
		xstrfmtcat(geo, "%c", alpha_num[int_geo[i]]);
	}
	return geo;
}
