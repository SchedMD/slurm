/*****************************************************************************\
 *  bg_record_functions.c - header for creating blocks in a static environment.
 *
 *  $Id: bg_record_functions.c 12954 2008-01-04 20:37:49Z da $
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
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

#include "bg_core.h"
#include "bg_dynamic_block.h"

#include "src/common/uid.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/slurmctld/trigger_mgr.h"
#include "src/slurmctld/locks.h"

/* some local functions */
static int _set_block_nodes_accounting(bg_record_t *bg_record, char *reason);
static void _addto_mp_list(bg_record_t *bg_record,
			   uint16_t *start, uint16_t *end);
static int _ba_mp_cmpf_inc(void *r1, void *r2);
static void _set_block_avail(bg_record_t *bg_record);

extern void print_bg_record(bg_record_t* bg_record)
{
	char *conn_type;
	if (!bg_record) {
		error("print_bg_record, record given is null");
		return;
	}
	conn_type = conn_type_string_full(bg_record->conn_type);
#if _DEBUG
	info(" bg_record: ");
	if (bg_record->bg_block_id)
		info("\tbg_block_id: %s", bg_record->bg_block_id);
	info("\tnodes: %s", bg_record->mp_str);
	info("\tsize: %d MPs %u Nodes %d cpus",
	     bg_record->mp_count,
	     bg_record->cnode_cnt,
	     bg_record->cpu_cnt);
	info("\tgeo: %ux%ux%u", bg_record->geo[X], bg_record->geo[Y],
	     bg_record->geo[Z]);
	info("\tconn_type: %s", conn_type);
#ifdef HAVE_BGL
	info("\tnode_use: %s", node_use_string(bg_record->node_use));
#endif
	if (bg_record->mp_bitmap) {
		char bitstring[BITSIZE];
		bit_fmt(bitstring, BITSIZE, bg_record->mp_bitmap);
		info("\tbitmap: %s", bitstring);
	}
#else
	{
		char tmp_char[256];
		format_node_name(bg_record, tmp_char, sizeof(tmp_char));
		info("Record: BlockID:%s Nodes:%s Conn:%s",
		     bg_record->bg_block_id, tmp_char,
		     conn_type);
	}
#endif
	xfree(conn_type);
}

extern void destroy_bg_record(void *object)
{
	bg_record_t* bg_record = (bg_record_t*) object;

	if (bg_record) {
		bg_record->magic = 0;
		if (bg_record->ba_mp_list) {
			list_destroy(bg_record->ba_mp_list);
			bg_record->ba_mp_list = NULL;
		}
		xfree(bg_record->bg_block_id);
		xfree(bg_record->blrtsimage);
		xfree(bg_record->ionode_str);
		FREE_NULL_BITMAP(bg_record->ionode_bitmap);

		if (bg_record->job_list) {
			list_destroy(bg_record->job_list);
			bg_record->job_list = NULL;
		}

		xfree(bg_record->linuximage);
		xfree(bg_record->mloaderimage);
		xfree(bg_record->mp_str);
		FREE_NULL_BITMAP(bg_record->mp_bitmap);
		xfree(bg_record->ramdiskimage);
		xfree(bg_record->reason);

		xfree(bg_record);
	}
}

extern void process_nodes(bg_record_t *bg_record, bool startup)
{
	int j=0;
	int diff=0;
	int largest_diff=-1;
	uint16_t best_start[SYSTEM_DIMENSIONS];
	uint16_t start[SYSTEM_DIMENSIONS];
	uint16_t end[SYSTEM_DIMENSIONS];
	bool start_set=0;
	ListIterator itr;
	ba_mp_t* ba_mp = NULL;
	int dim;
	static char tmp_char[SYSTEM_DIMENSIONS+1],
		tmp_char2[SYSTEM_DIMENSIONS+1];
	static int *cluster_dims = NULL;

	if (!cluster_dims) {
		/* do some initing that only needs to happen once. */
		cluster_dims = select_g_ba_get_dims();
		memset(tmp_char, 0, sizeof(tmp_char));
		memset(tmp_char2, 0, sizeof(tmp_char2));
	}

	if (!bg_record->ba_mp_list || !list_count(bg_record->ba_mp_list)) {
		char *nodes = bg_record->mp_str;

		if (!bg_record->ba_mp_list)
			bg_record->ba_mp_list = list_create(destroy_ba_mp);

		memset(&best_start, 0, sizeof(best_start));
		//bg_record->mp_count = 0;
		if ((bg_record->conn_type[0] >= SELECT_SMALL) && (!startup))
			error("process_nodes: "
			      "We shouldn't be here there could be some "
			      "badness if we use this logic %s",
			      bg_record->mp_str);
		while (nodes[j] != '\0') {
			int mid = j   + SYSTEM_DIMENSIONS + 1;
			int fin = mid + SYSTEM_DIMENSIONS + 1;
			if (((nodes[j] == '[')   || (nodes[j] == ','))   &&
			    ((nodes[mid] == 'x') || (nodes[mid] == '-')) &&
			    ((nodes[fin] == ']') || (nodes[fin] == ','))) {
				j++;	/* Skip leading '[' or ',' */
				for (dim = 0; dim < SYSTEM_DIMENSIONS;
				     dim++, j++)
					start[dim] = select_char2coord(
						nodes[j]);
				j++;	/* Skip middle 'x' or '-' */
				for (dim = 0; dim < SYSTEM_DIMENSIONS;
				     dim++, j++)
					end[dim] = select_char2coord(nodes[j]);
				diff = end[0]-start[0];
				_addto_mp_list(bg_record, start, end);
			} else if ((nodes[j] >= '0'&& nodes[j] <= '9')
				   || (nodes[j] >= 'A' && nodes[j] <= 'Z')) {
				for (dim = 0; dim < SYSTEM_DIMENSIONS;
				     dim++, j++)
					start[dim] = select_char2coord(
						nodes[j]);
				diff = 0;
				_addto_mp_list(bg_record, start, start);
			} else {
				j++;
				continue;
			}

			if (diff > largest_diff) {
				largest_diff = diff;
				memcpy(best_start, start, sizeof(best_start));

				if (bg_conf->slurm_debug_level
				    >= LOG_LEVEL_DEBUG3) {
					for (dim = 0;
					     dim < SYSTEM_DIMENSIONS;
					     dim++)
						tmp_char[dim] =	alpha_num[
							best_start[dim]];
					debug3("process_nodes: start is now %s",
					       tmp_char);
				}
			}
			if (bg_record->mp_str[j] != ',')
				break;

		}
		if (largest_diff == -1)
			fatal("No hostnames given here");

		memcpy(bg_record->start, best_start, sizeof(bg_record->start));
		start_set = 1;
		if (bg_conf->slurm_debug_level >= LOG_LEVEL_DEBUG3) {
			for (dim = 0; dim < SYSTEM_DIMENSIONS; dim++) {
				tmp_char[dim] = alpha_num[best_start[dim]];
				tmp_char2[dim] =
					alpha_num[bg_record->start[dim]];
			}
			debug3("process_nodes: start is %s %s",
			       tmp_char, tmp_char2);
		}
	}

	memset(bg_record->geo, 0, sizeof(bg_record->geo));
	for (dim = 0; dim < SYSTEM_DIMENSIONS; dim++) {
		end[dim] = (int16_t)-1;
		if (!start_set)
			bg_record->start[dim] = HOSTLIST_BASE;
	}

	list_sort(bg_record->ba_mp_list, (ListCmpF) _ba_mp_cmpf_inc);

	FREE_NULL_BITMAP(bg_record->mp_bitmap);
	bg_record->mp_bitmap = bit_alloc(node_record_count);
	bg_record->mp_count = 0;
	itr = list_iterator_create(bg_record->ba_mp_list);
	while ((ba_mp = list_next(itr))) {
		if (!ba_mp->used)
			continue;
		bg_record->mp_count++;
		debug3("process_nodes: %s is included in this block",
		       ba_mp->coord_str);

		for (dim = 0; dim < SYSTEM_DIMENSIONS; dim++) {
			if (ba_mp->coord[dim] > (int16_t)end[dim]) {
				bg_record->geo[dim]++;
				end[dim] = ba_mp->coord[dim];
			}
			if (!start_set && (ba_mp->coord[dim] <
					   (int16_t)bg_record->start[dim]))
				bg_record->start[dim] =	ba_mp->coord[dim];
		}
		bit_set(bg_record->mp_bitmap, ba_mp->index);
	}
	list_iterator_destroy(itr);
	if (bg_conf->slurm_debug_level >= LOG_LEVEL_DEBUG3) {
		for (dim = 0; dim < SYSTEM_DIMENSIONS; dim++) {
			tmp_char[dim] = alpha_num[bg_record->geo[dim]];
			tmp_char2[dim] = alpha_num[bg_record->start[dim]];
		}
		debug3("process_nodes: geo = %s mp count is %d start is %s",
		       tmp_char, bg_record->mp_count, tmp_char2);
	}
	/* This check is for sub midplane systems to figure out what
	   the largest block can be.
	*/
	for (dim = 0; dim < SYSTEM_DIMENSIONS; dim++) {
		if (cluster_dims[dim] > 1)
			break;
	}
	if (dim < SYSTEM_DIMENSIONS) {
		/* means we have more than 1 midplane */
		for (dim = 0; dim < SYSTEM_DIMENSIONS; dim++) {
			if (bg_record->geo[dim] != cluster_dims[dim])
				break;
		}
		if (dim == SYSTEM_DIMENSIONS)
			bg_record->full_block = 1;
	} else if (bg_record->cnode_cnt == bg_conf->mp_cnode_cnt)
		bg_record->full_block = 1;

	return;
}

/*
 * NOTE: This function does not do a mutex lock so if you are copying the
 * main bg_list you need to lock 'block_state_mutex' before calling
 */
extern List copy_bg_list(List in_list)
{
	bg_record_t *bg_record = NULL;
	bg_record_t *new_record = NULL;
	List out_list = list_create(destroy_bg_record);
	ListIterator itr = list_iterator_create(in_list);

	while ((bg_record = (bg_record_t *) list_next(itr))) {
		if (bg_record->magic != BLOCK_MAGIC) {
			error("trying to copy a bad record");
			continue;
		}
		/* we don't care about blocks being destroyed and the
		 * job is gone */
		if (bg_record->destroy
		    && (!bg_record->job_ptr
			&& (!bg_record->job_list
			    || !list_count(bg_record->job_list))))
			continue;

		new_record = xmalloc(sizeof(bg_record_t));
		new_record->original = bg_record;
		copy_bg_record(bg_record, new_record);
		list_append(out_list, new_record);
	}

	list_iterator_destroy(itr);

	return out_list;
}

extern void copy_bg_record(bg_record_t *fir_record, bg_record_t *sec_record)
{
	int i;
	ListIterator itr = NULL;
	ba_mp_t *ba_mp = NULL, *new_ba_mp = NULL;

	if (!fir_record || !sec_record) {
		error("copy_bg_record: "
		      "given a null for either first record or second record");
		return;
	}

	xfree(sec_record->bg_block_id);
	sec_record->action = fir_record->action;
	sec_record->bg_block_id = xstrdup(fir_record->bg_block_id);

	if (sec_record->ba_mp_list)
		list_destroy(sec_record->ba_mp_list);
	sec_record->ba_mp_list = list_create(destroy_ba_mp);
	if (fir_record->ba_mp_list) {
		itr = list_iterator_create(fir_record->ba_mp_list);
		while ((ba_mp = list_next(itr))) {
			new_ba_mp = ba_copy_mp(ba_mp);

			if (ba_mp->cnode_bitmap)
				new_ba_mp->cnode_bitmap =
					bit_copy(ba_mp->cnode_bitmap);
			if (ba_mp->cnode_err_bitmap)
				new_ba_mp->cnode_err_bitmap =
					bit_copy(ba_mp->cnode_err_bitmap);
			if (ba_mp->cnode_usable_bitmap)
				new_ba_mp->cnode_usable_bitmap =
					bit_copy(ba_mp->cnode_usable_bitmap);

			list_append(sec_record->ba_mp_list, new_ba_mp);
		}
		list_iterator_destroy(itr);
	}

	FREE_NULL_BITMAP(sec_record->mp_bitmap);
	if (fir_record->mp_bitmap
	    && !(sec_record->mp_bitmap = bit_copy(fir_record->mp_bitmap))) {
		error("Unable to copy bitmap for %s", fir_record->mp_str);
		sec_record->mp_bitmap = NULL;
	}

	sec_record->boot_state = fir_record->boot_state;
	sec_record->boot_count = fir_record->boot_count;

	sec_record->cnode_cnt = fir_record->cnode_cnt;
	sec_record->cnode_err_cnt = fir_record->cnode_err_cnt;

	memcpy(sec_record->conn_type, fir_record->conn_type,
	       sizeof(sec_record->conn_type));
	sec_record->cpu_cnt = fir_record->cpu_cnt;
	sec_record->destroy = fir_record->destroy;
	sec_record->err_ratio = fir_record->err_ratio;
	sec_record->free_cnt = fir_record->free_cnt;
	sec_record->full_block = fir_record->full_block;

	for (i=0;i<SYSTEM_DIMENSIONS;i++) {
		sec_record->geo[i] = fir_record->geo[i];
		sec_record->start[i] = fir_record->start[i];
	}

	for (i=0;i<HIGHEST_DIMENSIONS;i++)
		sec_record->start_small[i] = fir_record->start_small[i];

	xfree(sec_record->ionode_str);
	sec_record->ionode_str = xstrdup(fir_record->ionode_str);

	FREE_NULL_BITMAP(sec_record->ionode_bitmap);
	if (fir_record->ionode_bitmap
	    && (sec_record->ionode_bitmap
		= bit_copy(fir_record->ionode_bitmap)) == NULL) {
		error("Unable to copy ionode_bitmap for %s",
		      fir_record->mp_str);
		sec_record->ionode_bitmap = NULL;
	}

	if (sec_record->job_list) {
		list_destroy(sec_record->job_list);
		sec_record->job_list = NULL;
	}

	if (fir_record->job_list) {
		struct job_record *job_ptr;
		sec_record->job_list = list_create(NULL);
		itr = list_iterator_create(fir_record->job_list);
		while ((job_ptr = list_next(itr))) {
			if (job_ptr->magic != JOB_MAGIC) {
				error("copy_bg_record: bad job magic, "
				      "this should never happen");
				list_delete_item(itr);
				continue;
			}

			list_append(sec_record->job_list, job_ptr);
		}
		list_iterator_destroy(itr);
	}
	sec_record->job_ptr = fir_record->job_ptr;
	sec_record->job_running = fir_record->job_running;

	sec_record->magic = fir_record->magic;

	xfree(sec_record->blrtsimage);
	sec_record->blrtsimage = xstrdup(fir_record->blrtsimage);

	xfree(sec_record->linuximage);
	sec_record->linuximage = xstrdup(fir_record->linuximage);

	xfree(sec_record->mloaderimage);
	sec_record->mloaderimage = xstrdup(fir_record->mloaderimage);

	xfree(sec_record->ramdiskimage);
	sec_record->ramdiskimage = xstrdup(fir_record->ramdiskimage);

	sec_record->modifying = fir_record->modifying;

	sec_record->mp_count = fir_record->mp_count;

	xfree(sec_record->mp_str);
	sec_record->mp_str = xstrdup(fir_record->mp_str);

#ifdef HAVE_BGL
	sec_record->node_use = fir_record->node_use;
#endif
	/* Don't set the original, only in bg_copy_list does it happen
	 * for a reason. */
	/* sec_record->original = fir_record; */

	xfree(sec_record->reason);
	sec_record->reason = xstrdup(fir_record->reason);

	sec_record->state = fir_record->state;
}

/*
 * Comparator used for sorting blocks smallest to largest
 *
 * returns: -1: rec_a > rec_b   0: rec_a == rec_b   1: rec_a < rec_b
 *
 */
extern int bg_record_cmpf_inc(void *r1, void *r2)
{
	bg_record_t *rec_a = *(bg_record_t **)r1;
	bg_record_t *rec_b = *(bg_record_t **)r2;
	int size_a;
	int size_b;

	size_a = rec_a->cnode_cnt;
	size_b = rec_b->cnode_cnt;

	/* We only look at this if we are ordering blocks larger than
	 * a midplane, order of ionodes is how we order otherwise. */
	if ((size_a >= bg_conf->mp_cnode_cnt)
	    || (size_b >= bg_conf->mp_cnode_cnt)) {
		if (size_a < size_b)
			return -1;
		else if (size_a > size_b)
			return 1;
	}

	if (rec_a->mp_str && rec_b->mp_str) {
		size_a = strcmp(rec_a->mp_str, rec_b->mp_str);
		if (size_a < 0)
			return -1;
		else if (size_a > 0)
			return 1;
	}

	if (!rec_a->ionode_bitmap || !rec_b->ionode_bitmap)
		return 0;

	if (bit_ffs(rec_a->ionode_bitmap) < bit_ffs(rec_b->ionode_bitmap))
		return -1;
	else
		return 1;

	return 0;
}

/*
 * Comparator used for sorting blocks from earliest available to lastest
 * This will return the fullest shared midplane blocks first
 * regardless if it is the completely available sooner or not.
 * returns: -1: rec_a < rec_b   0: rec_a == rec_b   1: rec_a > rec_b
 *
 */
extern int bg_record_sort_aval_inc(void *r1, void *r2)
{
	bg_record_t* rec_a = *(bg_record_t **)r1;
	bg_record_t* rec_b = *(bg_record_t **)r2;

	if ((rec_a->job_running == BLOCK_ERROR_STATE)
	    && (rec_b->job_running != BLOCK_ERROR_STATE))
		return 1;
	else if ((rec_a->job_running != BLOCK_ERROR_STATE)
		 && (rec_b->job_running == BLOCK_ERROR_STATE))
		return -1;

	if (!rec_a->avail_set)
		_set_block_avail(rec_a);

	if (!rec_b->avail_set)
		_set_block_avail(rec_b);

	/* Don't use this check below.  It will mess up preemption by
	   sending this smaller block to the back of the list just
	   because it is fully used.
	*/
	/* if (!rec_a->avail_cnode_cnt && rec_b->avail_cnode_cnt) */
	/* 	return 1; */
	/* else if (rec_a->avail_cnode_cnt && !rec_b->avail_cnode_cnt) */
	/* 	return -1; */

	if (rec_a->job_list && rec_b->job_list) {
		/* we only want to use this sort on 1 midplane blocks
		   that are used for sharing
		*/
		if (rec_a->avail_cnode_cnt > rec_b->avail_cnode_cnt)
			return 1;
		else if (rec_a->avail_cnode_cnt < rec_b->avail_cnode_cnt)
			return -1;
	}

	if (rec_a->avail_job_end > rec_b->avail_job_end)
		return 1;
	else if (rec_a->avail_job_end < rec_b->avail_job_end)
		return -1;

	/* if (!job_ptr_a && job_ptr_b) */
	/* 	return -1; */
	/* else if (job_ptr_a && !job_ptr_b) */
	/* 	return 1; */
	/* else if (job_ptr_a && job_ptr_b) { */
	/* 	if (job_ptr_a->end_time > job_ptr_b->end_time) */
	/* 		return 1; */
	/* 	else if (job_ptr_a->end_time < job_ptr_b->end_time) */
	/* 		return -1; */
	/* } */

	return bg_record_cmpf_inc(&rec_a, &rec_b);
}

/*
 * Comparator used for sorting blocks from earliest available to lastest
 * based primarily when the last job is available.
 * returns: -1: rec_a < rec_b   0: rec_a == rec_b   1: rec_a > rec_b
 *
 */
extern int bg_record_sort_aval_time_inc(void *r1, void *r2)
{
	bg_record_t* rec_a = *(bg_record_t **)r1;
	bg_record_t* rec_b = *(bg_record_t **)r2;

	if ((rec_a->job_running == BLOCK_ERROR_STATE)
	    && (rec_b->job_running != BLOCK_ERROR_STATE))
		return 1;
	else if ((rec_a->job_running != BLOCK_ERROR_STATE)
		 && (rec_b->job_running == BLOCK_ERROR_STATE))
		return -1;

	if (!rec_a->avail_set)
		_set_block_avail(rec_a);

	if (!rec_b->avail_set)
		_set_block_avail(rec_b);

	/* Don't use this check below.  It will mess up preemption by
	   sending this smaller block to the back of the list just
	   because it is fully used.
	*/
	/* if (!rec_a->avail_cnode_cnt && rec_b->avail_cnode_cnt) */
	/* 	return 1; */
	/* else if (rec_a->avail_cnode_cnt && !rec_b->avail_cnode_cnt) */
	/* 	return -1; */


	if (rec_a->avail_job_end > rec_b->avail_job_end)
		return 1;
	else if (rec_a->avail_job_end < rec_b->avail_job_end)
		return -1;

	if (rec_a->job_list && rec_b->job_list) {
		/* we only want to use this sort on 1 midplane blocks
		   that are used for sharing
		*/
		if (rec_a->avail_cnode_cnt > rec_b->avail_cnode_cnt)
			return 1;
		else if (rec_a->avail_cnode_cnt < rec_b->avail_cnode_cnt)
			return -1;
	}

	/* if (!job_ptr_a && job_ptr_b) */
	/* 	return -1; */
	/* else if (job_ptr_a && !job_ptr_b) */
	/* 	return 1; */
	/* else if (job_ptr_a && job_ptr_b) { */
	/* 	if (job_ptr_a->end_time > job_ptr_b->end_time) */
	/* 		return 1; */
	/* 	else if (job_ptr_a->end_time < job_ptr_b->end_time) */
	/* 		return -1; */
	/* } */

	return bg_record_cmpf_inc(&rec_a, &rec_b);
}

/* set up structures needed for sub block jobs. */
extern void setup_subblock_structs(bg_record_t *bg_record)
{
	ba_mp_t *ba_mp;

	xassert(bg_record);

	if (!bg_conf->sub_blocks || bg_record->mp_count != 1)
		return;

	xassert(bg_record->ba_mp_list);

	ba_mp = list_peek(bg_record->ba_mp_list);
	xassert(ba_mp);

	/* This will be a list containing jobs running on this
	   block */
	if (!bg_record->job_list)
		bg_record->job_list = list_create(NULL);

	/* Create these now so we can deal with error
	   cnodes if/when they happen.  Since this is
	   the easiest place to figure it out for
	   blocks that don't use the entire block */
	FREE_NULL_BITMAP(ba_mp->cnode_bitmap);
	if ((ba_mp->cnode_bitmap =
	     ba_create_ba_mp_cnode_bitmap(bg_record))) {
		FREE_NULL_BITMAP(ba_mp->cnode_err_bitmap);
		FREE_NULL_BITMAP(ba_mp->cnode_usable_bitmap);
		ba_mp->cnode_err_bitmap =
			bit_alloc(bg_conf->mp_cnode_cnt);
		ba_mp->cnode_usable_bitmap =
			bit_copy(ba_mp->cnode_bitmap);
	}
}

/* Try to requeue job running on block and put block in an error state.
 * block_state_mutex and slurmctld must be unlocked before calling this.
 */
extern void requeue_and_error(bg_record_t *bg_record, char *reason)
{

	int rc;
	List kill_job_list = NULL;
	kill_job_struct_t *freeit;

	slurm_mutex_lock(&block_state_mutex);
	if (bg_record->magic != BLOCK_MAGIC) {
		error("requeue_and_error: magic was bad");
		slurm_mutex_unlock(&block_state_mutex);
		return;
	}

	if (bg_record->job_running > NO_JOB_RUNNING) {
		kill_job_list = bg_status_create_kill_job_list();

		freeit = xmalloc(sizeof(kill_job_struct_t));
		freeit->jobid = bg_record->job_running;
		list_push(kill_job_list, freeit);
	} else if (bg_record->job_list) {
		ListIterator itr = list_iterator_create(bg_record->job_list);
		struct job_record *job_ptr;
		while ((job_ptr = list_next(itr))) {
			if (!kill_job_list)
				kill_job_list =
					bg_status_create_kill_job_list();
			freeit = xmalloc(sizeof(kill_job_struct_t));
			freeit->jobid = job_ptr->job_id;
			list_push(kill_job_list, freeit);
		}
		list_iterator_destroy(itr);
	}

	rc = block_ptr_exist_in_list(bg_lists->main, bg_record);
	slurm_mutex_unlock(&block_state_mutex);

	if (kill_job_list) {
		bg_status_process_kill_job_list(kill_job_list, JOB_FAILED, 0);
		list_destroy(kill_job_list);
	}

	if (rc)
		put_block_in_error_state(bg_record, reason);
	else
		error("requeue_and_error: block disappeared");
	return;
}

/* block_state_mutex must be locked before calling this. */
extern int add_bg_record(List records, List *used_nodes,
			 select_ba_request_t *blockreq,
			 bool no_check, bitoff_t io_start)
{
	bg_record_t *bg_record = NULL;
	ba_mp_t *ba_mp = NULL;
	ListIterator itr;
	int i, len;
	char *conn_type = NULL;

	xassert(bg_conf->slurm_user_name);

	if (!records) {
		fatal("add_bg_record: no records list given");
	}
	bg_record = (bg_record_t*) xmalloc(sizeof(bg_record_t));

	bg_record->magic = BLOCK_MAGIC;

	if (used_nodes && *used_nodes) {
#ifdef HAVE_BGQ
		bg_record->ba_mp_list = *used_nodes;
		*used_nodes = NULL;
#else
		bg_record->ba_mp_list = list_create(destroy_ba_mp);
		if (copy_node_path(*used_nodes, &bg_record->ba_mp_list)
		    == SLURM_ERROR)
			error("add_bg_record: "
			      "couldn't copy the path for the allocation");
#endif
	} else
		bg_record->ba_mp_list = list_create(destroy_ba_mp);

	/* bg_record->boot_state = 0; 	Implicit */
	bg_record->state = BG_BLOCK_FREE;
#ifdef HAVE_BGL
	if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK) {
		conn_type = conn_type_string_full(blockreq->conn_type);
		info("add_bg_record: asking for %s %d %d %s",
		     blockreq->save_name, blockreq->small32, blockreq->small128,
		     conn_type);
		xfree(conn_type);
	}
#else
	if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK) {
		conn_type = conn_type_string_full(blockreq->conn_type);
		info("add_bg_record: asking for %s %d %d %d %d %d %s",
		     blockreq->save_name, blockreq->small256,
		     blockreq->small128, blockreq->small64,
		     blockreq->small32, blockreq->small16,
		     conn_type);
		xfree(conn_type);
	}
#endif
	/* Set the bitmap blank here if it is a full node we don't
	   want anything set we also don't want the bg_record->ionode_str set.
	*/
	bg_record->ionode_bitmap = bit_alloc(bg_conf->ionodes_per_mp);

	len = strlen(blockreq->save_name);
	i=0;
	while (i<len
	       && blockreq->save_name[i] != '['
	       && (blockreq->save_name[i] < '0' || blockreq->save_name[i] > 'Z'
		   || (blockreq->save_name[i] > '9'
		       && blockreq->save_name[i] < 'A')))
		i++;

	if (i<len) {
		len -= i;

		len += strlen(bg_conf->slurm_node_prefix)+1;
		bg_record->mp_str = xmalloc(len);
		snprintf(bg_record->mp_str, len, "%s%s",
			 bg_conf->slurm_node_prefix, blockreq->save_name+i);
	} else
		fatal("add_bg_record: MPs=%s is in a weird format",
		      blockreq->save_name);

	process_nodes(bg_record, false);

#ifdef HAVE_BGL
	bg_record->node_use = SELECT_COPROCESSOR_MODE;
#endif
	memcpy(bg_record->conn_type, blockreq->conn_type,
	       sizeof(bg_record->conn_type));

	bg_record->cpu_cnt = bg_conf->cpus_per_mp * bg_record->mp_count;
	bg_record->cnode_cnt = bg_conf->mp_cnode_cnt * bg_record->mp_count;
	bg_record->job_running = NO_JOB_RUNNING;

#ifdef HAVE_BGL
	if (blockreq->blrtsimage)
		bg_record->blrtsimage = xstrdup(blockreq->blrtsimage);
	else
		bg_record->blrtsimage = xstrdup(bg_conf->default_blrtsimage);
#endif

#ifdef HAVE_BG_L_P
	if (blockreq->linuximage)
		bg_record->linuximage = xstrdup(blockreq->linuximage);
	else
		bg_record->linuximage = xstrdup(bg_conf->default_linuximage);
	if (blockreq->ramdiskimage)
		bg_record->ramdiskimage = xstrdup(blockreq->ramdiskimage);
	else
		bg_record->ramdiskimage =
			xstrdup(bg_conf->default_ramdiskimage);
#endif
	if (blockreq->mloaderimage)
		bg_record->mloaderimage = xstrdup(blockreq->mloaderimage);
	else
		bg_record->mloaderimage =
			xstrdup(bg_conf->default_mloaderimage);

#ifdef HAVE_BGQ
	/* The start is always right, for blocks larger than 1, from
	   the blockreq so don't take chances. */
	if (bg_record->mp_count > 1)
		memcpy(bg_record->start, blockreq->start,
		       sizeof(bg_record->start));
#endif

	if (bg_record->conn_type[0] < SELECT_SMALL) {
		/* this needs to be an append so we keep things in the
		   order we got them, they will be sorted later */
		list_append(records, bg_record);
		/* this isn't a correct list so we need to set it later for
		   now we just used it to be the mp number */
		if (!used_nodes) {
			debug4("add_bg_record: "
			       "we didn't get a request list so we are "
			       "destroying this mp list");
			list_destroy(bg_record->ba_mp_list);
			bg_record->ba_mp_list = NULL;
		} else
			setup_subblock_structs(bg_record);
	} else {
		List ba_mp_list = NULL;

		if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
			info("add_bg_record: adding a small block");
		if (no_check)
			goto no_check;
		/* if the ionode cnt for small32 is 0 then don't
		   allow a sub quarter allocation
		*/
		if (bg_conf->nodecard_ionode_cnt < 2) {
			if (!bg_conf->nodecard_ionode_cnt && blockreq->small32)
				fatal("add_bg_record: "
				      "There is an error in your "
				      "bluegene.conf file.\n"
				      "Can't create a 32 node block with "
				      "IonodesPerMP=%u. (Try setting it "
				      "to at least 16)",
				      bg_conf->ionodes_per_mp);
#ifdef HAVE_BGP
			if (blockreq->small16)
				fatal("add_bg_record: "
				      "There is an error in your "
				      "bluegene.conf file.\n"
				      "Can't create a 16 node block with "
				      "IonodesPerMP=%u. (Try setting it to "
				      "at least 32)",
				      bg_conf->ionodes_per_mp);
#endif

#ifndef HAVE_BGL
			if ((bg_conf->io_ratio < 0.5) && blockreq->small64)
				fatal("add_bg_record: "
				      "There is an error in your "
				      "bluegene.conf file.\n"
				      "Can't create a 64 node block with "
				      "IonodesPerMP=%u. (Try setting it "
				      "to at least 8)",
				      bg_conf->ionodes_per_mp);
#endif
		}

#ifdef HAVE_BGL
		if (blockreq->small32==0 && blockreq->small128==0) {
			info("add_bg_record: "
			     "No specs given for this small block, "
			     "I am spliting this block into 4 128CnBlocks");
			blockreq->small128=4;
		}

		i = (blockreq->small32*bg_conf->nodecard_cnode_cnt) +
			(blockreq->small128*bg_conf->quarter_cnode_cnt);
		if (i != bg_conf->mp_cnode_cnt)
			fatal("add_bg_record: "
			      "There is an error in your bluegene.conf file.\n"
			      "I am unable to request %d nodes consisting of "
			      "%u 32CnBlocks and\n%u 128CnBlocks in one "
			      "midplane with %u nodes.",
			      i, blockreq->small32, blockreq->small128,
			      bg_conf->mp_cnode_cnt);
#else
		if (!blockreq->small16 && !blockreq->small32
		    && !blockreq->small64 && !blockreq->small128
		    && !blockreq->small256) {
			info("add_bg_record: "
			     "No specs given for this small block, "
			     "I am spliting this block into 2 256CnBlocks");
			blockreq->small256=2;
		}

		i = (blockreq->small16*16)
			+ (blockreq->small32*32)
			+ (blockreq->small64*64)
			+ (blockreq->small128*128)
			+ (blockreq->small256*256);
		if (i != bg_conf->mp_cnode_cnt)
			fatal("add_bg_record: "
			      "There is an error in your bluegene.conf file.\n"
			      "I am unable to request %d nodes consisting of "
			      "%u 16CNBlocks, %u 32CNBlocks,\n"
			      "%u 64CNBlocks, %u 128CNBlocks, "
			      "and %u 256CNBlocks\n"
			      "in one midplane with %u nodes.",
			      i, blockreq->small16, blockreq->small32,
			      blockreq->small64, blockreq->small128,
			      blockreq->small256, bg_conf->mp_cnode_cnt);
#endif
	no_check:
		/* Automatically create 2-way split if
		 * conn_type == SELECT_SMALL in bluegene.conf
		 * Here we go through each node listed and do the same thing
		 * for each node.
		 */
		ba_mp_list = bg_record->ba_mp_list;
		bg_record->ba_mp_list = list_create(NULL);
		itr = list_iterator_create(ba_mp_list);
		while ((ba_mp = list_next(itr)) != NULL) {
			xfree(bg_record->mp_str);
			bg_record->mp_str = xstrdup_printf(
				"%s%s",
				bg_conf->slurm_node_prefix,
				ba_mp->coord_str);
			list_append(bg_record->ba_mp_list, ba_mp);
			handle_small_record_request(records, blockreq,
						    bg_record, io_start);
			list_flush(bg_record->ba_mp_list);
		}
		list_iterator_destroy(itr);
		destroy_bg_record(bg_record);
		list_destroy(ba_mp_list);
	}

	return SLURM_SUCCESS;
}

extern int handle_small_record_request(List records,
				       select_ba_request_t *blockreq,
				       bg_record_t *bg_record, bitoff_t start)
{
	bitstr_t *ionodes = bit_alloc(bg_conf->ionodes_per_mp);
	int i=0, ionode_cnt = 0;
	bg_record_t *found_record = NULL;

	xassert(records);
	xassert(blockreq);
	xassert(bg_record);

	xassert(start >= 0);
	xassert(start < bg_conf->ionodes_per_mp);

#ifndef HAVE_BGL
	for(i=0; i<blockreq->small16; i++) {
		bit_nset(ionodes, start, start);
		found_record = create_small_record(bg_record, ionodes, 16);
		/* this needs to be an append so we
		   keep things in the order we got
		   them, they will be sorted later */
		list_append(records, found_record);
		bit_nclear(ionodes, start, start);
		start++;
	}
#endif
	if ((ionode_cnt = bg_conf->nodecard_ionode_cnt))
		ionode_cnt--;
	for(i=0; i<blockreq->small32; i++) {
		bit_nset(ionodes, start, start+ionode_cnt);
		found_record = create_small_record(bg_record, ionodes, 32);
		/* this needs to be an append so we
		   keep things in the order we got
		   them, they will be sorted later */
		list_append(records, found_record);
		bit_nclear(ionodes, start, start+ionode_cnt);
		start+=ionode_cnt+1;
	}

#ifndef HAVE_BGL
	if ((ionode_cnt = bg_conf->nodecard_ionode_cnt * 2))
		ionode_cnt--;
	for(i=0; i<blockreq->small64; i++) {
		bit_nset(ionodes, start, start+ionode_cnt);
		found_record = create_small_record(bg_record, ionodes, 64);
		/* this needs to be an append so we
		   keep things in the order we got
		   them, they will be sorted later */
		list_append(records, found_record);
		bit_nclear(ionodes, start, start+ionode_cnt);
		start+=ionode_cnt+1;
	}
#endif
	if ((ionode_cnt = bg_conf->quarter_ionode_cnt))
		ionode_cnt--;
	for(i=0; i<blockreq->small128; i++) {
		bit_nset(ionodes, start, start+ionode_cnt);
		found_record = create_small_record(bg_record, ionodes, 128);
		/* this needs to be an append so we
		   keep things in the order we got
		   them, they will be sorted later */
		list_append(records, found_record);
		bit_nclear(ionodes, start, start+ionode_cnt);
		start+=ionode_cnt+1;
	}

#ifndef HAVE_BGL
	if ((ionode_cnt = bg_conf->quarter_ionode_cnt * 2))
		ionode_cnt--;
	for(i=0; i<blockreq->small256; i++) {
		bit_nset(ionodes, start, start+ionode_cnt);
		found_record = create_small_record(bg_record, ionodes, 256);
		/* this needs to be an append so we
		   keep things in the order we got
		   them, they will be sorted later */
		list_append(records, found_record);
		bit_nclear(ionodes, start, start+ionode_cnt);
		start+=ionode_cnt+1;
	}
#endif

	FREE_NULL_BITMAP(ionodes);

	return SLURM_SUCCESS;
}

extern int format_node_name(bg_record_t *bg_record, char *buf, int buf_size)
{
	if (bg_record->ionode_str) {
		snprintf(buf, buf_size, "%s[%s]",
			 bg_record->mp_str,
			 bg_record->ionode_str);
	} else {
		snprintf(buf, buf_size, "%s", bg_record->mp_str);
	}
	return SLURM_SUCCESS;
}

/*
 * This could potentially lock the node lock in the slurmctld with
 * slurm_drain_node, or slurm_fail_job so if slurmctld_locked is called we
 * will call the functions without locking the locks again.
 */
extern int down_nodecard(char *mp_name, bitoff_t io_start,
			 bool slurmctld_locked, char *reason)
{
	List requests = NULL;
	List delete_list = NULL, pass_list = NULL, kill_list = NULL;
	ListIterator itr = NULL;
	bg_record_t *bg_record = NULL, *found_record = NULL,
		tmp_record, *error_bg_record = NULL;
	bg_record_t *smallest_bg_record = NULL;
	struct node_record *node_ptr = NULL;
	int mp_bit = 0;
	bool has_pass = 0;
	static int io_cnt = NO_VAL;
	static int create_size = NO_VAL;
	static select_ba_request_t blockreq;
	int rc = SLURM_SUCCESS;
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK };

	xassert(mp_name);

	if (!reason)
		reason = "select_bluegene: nodecard down";

	if (io_cnt == NO_VAL) {
		io_cnt = 1;
		/* Translate 1 nodecard count to ionode count */
		if ((io_cnt *= bg_conf->io_ratio))
			io_cnt--;

		/* make sure we create something that is able to be
		   created */
		if (bg_conf->smallest_block < bg_conf->nodecard_cnode_cnt)
			create_size = bg_conf->nodecard_cnode_cnt;
		else
			create_size = bg_conf->smallest_block;
	}

	node_ptr = find_node_record(mp_name);
	if (!node_ptr) {
		error ("down_sub_node_blocks: invalid node specified '%s'",
		       mp_name);
		return EINVAL;
	}

	/* this is here for sanity check to make sure we don't core on
	   these bits when we set them below. */
	if (io_start >= bg_conf->ionodes_per_mp
	    || (io_start+io_cnt) >= bg_conf->ionodes_per_mp) {
		debug("io %d-%d not configured on this "
		      "system, only %d ionodes per midplane",
		      io_start, io_start+io_cnt, bg_conf->ionodes_per_mp);
		return EINVAL;
	}
	mp_bit = (node_ptr - node_record_table_ptr);

	memset(&blockreq, 0, sizeof(select_ba_request_t));

	blockreq.conn_type[0] = SELECT_SMALL;
	blockreq.save_name = mp_name;

	debug3("here setting node %d of %d and ionodes %d-%d of %d",
	       mp_bit, node_record_count, io_start,
	       io_start+io_cnt, bg_conf->ionodes_per_mp);

	memset(&tmp_record, 0, sizeof(bg_record_t));
	tmp_record.mp_count = 1;
	tmp_record.cnode_cnt = bg_conf->nodecard_cnode_cnt;
	tmp_record.mp_bitmap = bit_alloc(node_record_count);
	bit_set(tmp_record.mp_bitmap, mp_bit);

	tmp_record.ionode_bitmap = bit_alloc(bg_conf->ionodes_per_mp);
	bit_nset(tmp_record.ionode_bitmap, io_start, io_start+io_cnt);

	/* To avoid deadlock we always must lock the slurmctld before
	   the block_state_mutex.
	*/
	if (!slurmctld_locked)
		lock_slurmctld(job_write_lock);
	slurm_mutex_lock(&block_state_mutex);
	itr = list_iterator_create(bg_lists->main);
	while ((bg_record = list_next(itr))) {
		if (bg_record->destroy)
			continue;

		if (!bit_test(bg_record->mp_bitmap, mp_bit)
#ifndef HAVE_BG_L_P
		    /* In BGQ if a nodeboard goes down you can no
		       longer use any block using that nodeboard in a
		       passthrough, so we need to remove it.
		    */
		    && !(has_pass = block_mp_passthrough(bg_record, mp_bit))
#endif
			)
			continue;

		if (!has_pass && !blocks_overlap(bg_record, &tmp_record))
			continue;

		if (bg_record->job_running > NO_JOB_RUNNING) {
			bg_status_add_job_kill_list(bg_record->job_ptr,
						    &kill_list);
		} else if (bg_record->job_list) {
			ListIterator job_itr = list_iterator_create(
				bg_record->job_list);
			struct job_record *job_ptr;
			while ((job_ptr = list_next(job_itr)))
				bg_status_add_job_kill_list(job_ptr,
							    &kill_list);
			list_iterator_destroy(job_itr);
		}
		/* If Running Dynamic mode and the block is
		   smaller than the create size just continue on.
		*/
		if (bg_conf->layout_mode == LAYOUT_DYNAMIC) {
			if (bg_record->cnode_cnt < create_size) {
				if (!delete_list)
					delete_list = list_create(NULL);
				list_append(delete_list, bg_record);
				continue;
			} else if (has_pass) {
				/* Set it up so the passthrough blocks
				   get removed since they are no
				   longer valid.
				*/
				if (!pass_list)
					pass_list = list_create(NULL);
				list_append(pass_list, bg_record);
				continue;
			}
		} else if (has_pass) /* on non-dynamic systems this
					block doesn't really mean
					anything we just needed to
					fail the job (which was
					probably already failed).
				     */
			continue;

		/* keep track of the smallest size that is at least
		   the size of create_size. */
		if (!smallest_bg_record ||
		    (smallest_bg_record->cnode_cnt > bg_record->cnode_cnt))
			smallest_bg_record = bg_record;
	}
	list_iterator_destroy(itr);

	/* We cannot unlock block_state_mutex here until we are done
	 * with smallest_bg_record.
	 */

	if (bg_conf->layout_mode != LAYOUT_DYNAMIC) {
		debug3("running non-dynamic mode");
		/* This should never happen, but just in case... */
		if (delete_list) {
			list_destroy(delete_list);
			delete_list = NULL;
		}
		/* If we found a block that is smaller or equal to a
		   midplane we will just mark it in an error state as
		   opposed to draining the node.
		*/
		if (smallest_bg_record
		    && (smallest_bg_record->cnode_cnt < bg_conf->mp_cnode_cnt)){
			if (smallest_bg_record->state & BG_BLOCK_ERROR_FLAG) {
				rc = SLURM_NO_CHANGE_IN_DATA;
				slurm_mutex_unlock(&block_state_mutex);
				goto cleanup;
			}

			slurm_mutex_unlock(&block_state_mutex);
			error_bg_record = smallest_bg_record;
			goto cleanup;
		}

		slurm_mutex_unlock(&block_state_mutex);
		debug("No block under 1 midplane available for this nodecard.  "
		      "Draining the whole node.");

		/* the slurmctld is always locked here */
		if (!node_already_down(mp_name))
			drain_nodes(mp_name, reason,
				    slurm_get_slurm_user_id());
		rc = SLURM_SUCCESS;
		goto cleanup;
	}

	/* below is only for Dynamic mode */

	if (delete_list) {
		int cnt_set = 0;
		bitstr_t *iobitmap = bit_alloc(bg_conf->ionodes_per_mp);
		itr = list_iterator_create(delete_list);
		while ((bg_record = list_next(itr))) {
			debug2("combining smaller than nodecard "
			       "dynamic block %s",
			       bg_record->bg_block_id);
			bit_or(iobitmap, bg_record->ionode_bitmap);
			cnt_set++;
		}
		list_iterator_destroy(itr);
		list_destroy(delete_list);
		delete_list = NULL;

		if (!cnt_set) {
			FREE_NULL_BITMAP(iobitmap);
			rc = SLURM_ERROR;
			slurm_mutex_unlock(&block_state_mutex);
			goto cleanup;
		}
		/* set the start to be the same as the start of the
		   ionode_bitmap.  If no ionodes set (not a small
		   block) set io_start = 0. */
		if ((io_start = bit_ffs(iobitmap)) == -1) {
			io_start = 0;
			if (create_size > bg_conf->nodecard_cnode_cnt)
				blockreq.small128 = 4;
			else
				blockreq.small32 = 16;
		} else if (create_size <= bg_conf->nodecard_cnode_cnt)
			blockreq.small32 = 1;
		else
			/* this should never happen */
			blockreq.small128 = 1;

		FREE_NULL_BITMAP(iobitmap);
	} else if (smallest_bg_record) {
		debug2("smallest dynamic block is %s",
		       smallest_bg_record->bg_block_id);

		if (smallest_bg_record->cnode_cnt == create_size) {
			slurm_mutex_unlock(&block_state_mutex);
			error_bg_record = smallest_bg_record;
			goto cleanup;
		}

		/* If the block is bigger than the asked for error we
		   need to resume it to keep accounting correct.
		*/
		if (smallest_bg_record->state & BG_BLOCK_ERROR_FLAG)
			resume_block(smallest_bg_record);

		if (create_size > smallest_bg_record->cnode_cnt) {
			/* we should never get here.  This means we
			 * have a create_size that is bigger than a
			 * block that is already made.
			 */
			slurm_mutex_unlock(&block_state_mutex);
			error_bg_record = smallest_bg_record;
			goto cleanup;
		}
		debug3("node count is %d", smallest_bg_record->cnode_cnt);
		switch(smallest_bg_record->cnode_cnt) {
#ifndef HAVE_BGL
		case 64:
			blockreq.small32 = 2;
			break;
		case 256:
			blockreq.small32 = 8;
			break;
#endif
		case 128:
			blockreq.small32 = 4;
			break;
		case 512:
		default:
			blockreq.small32 = 16;
			break;
		}

		if (create_size != bg_conf->nodecard_cnode_cnt) {
			blockreq.small128 = blockreq.small32 / 4;
			blockreq.small32 = 0;
		}

		if ((io_start =
		     bit_ffs(smallest_bg_record->ionode_bitmap)) == -1)
			/* set the start to be the same as the start of the
			   ionode_bitmap.  If no ionodes set (not a small
			   block) set io_start = 0. */
			io_start = 0;
	} else {
		switch(create_size) {
#ifndef HAVE_BGL
		case 64:
			blockreq.small64 = 8;
			break;
		case 256:
			blockreq.small256 = 2;
#endif
		case 32:
			blockreq.small32 = 16;
			break;
		case 128:
			blockreq.small128 = 4;
			break;
		case 512:
			slurm_mutex_unlock(&block_state_mutex);
			/* the slurmctld is always locked here */
			if (!node_already_down(mp_name))
				drain_nodes(mp_name, reason,
					    slurm_get_slurm_user_id());
			rc = SLURM_SUCCESS;
			goto cleanup;
			break;
		default:
			error("Unknown create size of %d", create_size);
			break;
		}
		/* since we don't have a block in this midplane
		   we need to start at the beginning. */
		io_start = 0;
		/* we also need a bg_block to pretend to be the
		   smallest block that takes up the entire midplane. */
	}

	/* Here we need to add blocks that take up nodecards on this
	   midplane.  Since Slurm only keeps track of midplanes
	   natively this is the only want to handle this case.
	*/
	requests = list_create(destroy_bg_record);
	add_bg_record(requests, NULL, &blockreq, 1, io_start);

	if (bg_conf->sub_blocks
	    && (!smallest_bg_record
		|| smallest_bg_record->cnode_cnt == bg_conf->mp_cnode_cnt)) {
		bg_record_t *rem_record = NULL;
		memset(&blockreq, 0, sizeof(select_ba_request_t));
		blockreq.conn_type[0] = SELECT_SMALL;
		blockreq.save_name = mp_name;
		blockreq.small256 = 2;
		add_bg_record(requests, NULL, &blockreq, 1, io_start);

		itr = list_iterator_create(requests);
		while ((bg_record = list_next(itr))) {
			if (bit_overlap(bg_record->ionode_bitmap,
					tmp_record.ionode_bitmap)) {
				if (bg_record->cnode_cnt == 256) {
					print_bg_record(bg_record);
					rem_record = bg_record;
					list_remove(itr);
					break;
				}
			}
		}
		if (!rem_record) {
			/* this should never happen */
			error("down_nodecard: something bad happened "
			      "with creation of 256 block");
		} else {
			list_iterator_reset(itr);
			while ((bg_record = list_next(itr))) {
				if (bg_record->cnode_cnt == 256)
					continue;
				if (!bit_overlap(bg_record->ionode_bitmap,
						 rem_record->ionode_bitmap)) {
					print_bg_record(bg_record);
					list_delete_item(itr);
				}
			}
			destroy_bg_record(rem_record);
		}
		list_iterator_destroy(itr);
	}

	if (pass_list) {
		delete_list = pass_list;
		pass_list = NULL;
	} else
		delete_list = list_create(NULL);
	while ((bg_record = list_pop(requests))) {
		itr = list_iterator_create(bg_lists->main);
		while ((found_record = list_next(itr))) {
			if (found_record->destroy)
				continue;
			if (!blocks_overlap(bg_record, found_record))
				continue;
			list_push(delete_list, found_record);
		}
		list_iterator_destroy(itr);

		/* we need to add this record since it doesn't exist */
		if (bridge_block_create(bg_record) == SLURM_ERROR) {
			destroy_bg_record(bg_record);
			error("down_sub_node_blocks: "
			      "unable to configure block in api");
			continue;
		}

		debug("adding block %s to fill in small blocks "
		      "around bad nodecards",
		      bg_record->bg_block_id);
		print_bg_record(bg_record);
		list_append(bg_lists->main, bg_record);
		if (bit_overlap(bg_record->ionode_bitmap,
				tmp_record.ionode_bitmap)) {
			/* here we know the error block doesn't exist
			   so just set the state here */
			error_bg_record = bg_record;
		}
	}
	list_destroy(requests);

	sort_bg_record_inc_size(bg_lists->main);
	last_bg_update = time(NULL);
	slurm_mutex_unlock(&block_state_mutex);

cleanup:
	if (kill_list) {
		bg_status_process_kill_job_list(kill_list, JOB_NODE_FAIL, 1);
		list_destroy(kill_list);
	}

	if (!slurmctld_locked)
		unlock_slurmctld(job_write_lock);
	FREE_NULL_BITMAP(tmp_record.mp_bitmap);
	FREE_NULL_BITMAP(tmp_record.ionode_bitmap);
	if (error_bg_record) {
		/* all locks must be released before going into
		 * put_block_in_error_state.
		 */
		if (slurmctld_locked)
			unlock_slurmctld(job_write_lock);
		rc = put_block_in_error_state(error_bg_record, reason);
		if (slurmctld_locked)
			lock_slurmctld(job_write_lock);
	}

	if (pass_list) {
		delete_list = pass_list;
		pass_list = NULL;
	}

	if (delete_list) {
		bool delete_it = 0;
		if (bg_conf->layout_mode == LAYOUT_DYNAMIC)
			delete_it = 1;
		free_block_list(NO_VAL, delete_list, delete_it, 0);
		list_destroy(delete_list);
		delete_list = NULL;
	}

	return rc;

}

extern int up_nodecard(char *mp_name, bitstr_t *ionode_bitmap)
{
	ListIterator itr = NULL;
	bg_record_t *bg_record = NULL;
	struct node_record *node_ptr = NULL;
	int mp_bit = 0;
	int ret = 0;

	xassert(mp_name);
	xassert(ionode_bitmap);

	node_ptr = find_node_record(mp_name);
	if (!node_ptr) {
		error ("down_sub_node_blocks: invalid node specified %s",
		       mp_name);
		return EINVAL;
	}
	mp_bit = (node_ptr - node_record_table_ptr);

	slurm_mutex_lock(&block_state_mutex);
	itr = list_iterator_create(bg_lists->main);
	while ((bg_record = list_next(itr))) {
		if (bg_record->job_running != BLOCK_ERROR_STATE)
			continue;
		if (!bit_test(bg_record->mp_bitmap, mp_bit))
			continue;

		if (!bit_overlap(bg_record->ionode_bitmap, ionode_bitmap)) {
			continue;
		}
		resume_block(bg_record);
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&block_state_mutex);

	/* FIX ME: This needs to call the opposite of
	   slurm_drain_nodes which does not yet exist.
	*/
	if ((ret = node_already_down(mp_name))) {
		/* means it was drained */
		if (ret == 2) {
			/* debug("node %s put back into service after " */
/* 			      "being in an error state", */
/* 			      mp_name); */
		}
	}

	return SLURM_SUCCESS;
}

/* block_state_mutex must be unlocked before calling this. */
extern int put_block_in_error_state(bg_record_t *bg_record, char *reason)
{
	xassert(bg_record);

	/* Only check this if the blocks are created, meaning this
	   isn't at startup.
	*/
	if (blocks_are_created) {
		/* Since we are putting this block in an error state we need
		   to wait for the job to be removed.  We don't really
		   need to free the block though since we may just
		   want it to be in an error state for some reason. */
		while ((bg_record->magic == BLOCK_MAGIC)
		       && ((bg_record->job_running > NO_JOB_RUNNING)
			   || (bg_record->job_list
			       && list_count(bg_record->job_list)))) {
			if (bg_record->job_running > NO_JOB_RUNNING)
				debug2("block %s is still running job %d",
				       bg_record->bg_block_id,
				       bg_record->job_running);
			else
				debug2("block %s is still running jobs",
				       bg_record->bg_block_id);

			sleep(1);
		}
	}

	slurm_mutex_lock(&block_state_mutex);
	if (!block_ptr_exist_in_list(bg_lists->main, bg_record)) {
		slurm_mutex_unlock(&block_state_mutex);
		error("while trying to put block in "
		      "error state it disappeared");
		return SLURM_ERROR;
	}

	/* we add the block to these lists so we don't try to schedule
	   on them. */
	if (!block_ptr_exist_in_list(bg_lists->job_running, bg_record)) {
		list_push(bg_lists->job_running, bg_record);
		num_unused_cpus -= bg_record->cpu_cnt;
	} else if (!(bg_record->state & BG_BLOCK_ERROR_FLAG)) {
		info("hey I was in the job_running table %d %d %s?",
		     list_count(bg_record->job_list), num_unused_cpus,
		     bg_block_state_string(bg_record->state));
		xassert(0);
	}

	if (!block_ptr_exist_in_list(bg_lists->booted, bg_record))
		list_push(bg_lists->booted, bg_record);

	bg_record->job_running = BLOCK_ERROR_STATE;
	bg_record->state |= BG_BLOCK_ERROR_FLAG;

	/* Only send if reason is set.  If it isn't set then
	   accounting should already know about this error state */
	if (reason) {
		info("Setting Block %s to ERROR state. (reason: '%s')",
		     bg_record->bg_block_id, reason);
		xfree(bg_record->reason);
		bg_record->reason = xstrdup(reason);
		_set_block_nodes_accounting(bg_record, reason);
	}

	last_bg_update = time(NULL);
	slurm_mutex_unlock(&block_state_mutex);

	trigger_block_error();
	return SLURM_SUCCESS;
}

/* block_state_mutex should be locked before calling */
extern int resume_block(bg_record_t *bg_record)
{
	xassert(bg_record);

	if (bg_record->job_running > NO_JOB_RUNNING
	    || (bg_record->job_list && list_count(bg_record->job_list)))
		return SLURM_SUCCESS;

	if (bg_record->state & BG_BLOCK_ERROR_FLAG) {
		ba_mp_t *ba_mp;
		ListIterator itr;
		struct node_record *node_ptr;

		bg_record->state &= (~BG_BLOCK_ERROR_FLAG);
		info("Block %s put back into service after "
		     "being in an error state.",
		     bg_record->bg_block_id);

		/* Remove the block error message from each slurm node. */
		itr = list_iterator_create(bg_record->ba_mp_list);
		while ((ba_mp = list_next(itr))) {
			node_ptr = &node_record_table_ptr[ba_mp->index];
			if (node_ptr->reason
			    && !strncmp(node_ptr->reason, "update_block", 12))
				xfree(node_ptr->reason);
		}
		list_iterator_destroy(itr);
	}

	if (remove_from_bg_list(bg_lists->job_running, bg_record)
	    == SLURM_SUCCESS)
		num_unused_cpus += bg_record->cpu_cnt;

	if (bg_record->state != BG_BLOCK_INITED)
		remove_from_bg_list(bg_lists->booted, bg_record);
	else if (!block_ptr_exist_in_list(bg_lists->booted, bg_record))
		list_push(bg_lists->booted, bg_record);

	bg_record->job_running = NO_JOB_RUNNING;
	xfree(bg_record->reason);

	last_bg_update = time(NULL);
	_set_block_nodes_accounting(bg_record, NULL);

	return SLURM_SUCCESS;
}

/* block_state_mutex should be locked before calling this function */
extern int bg_reset_block(bg_record_t *bg_record, struct job_record *job_ptr)
{
	int rc = SLURM_SUCCESS;

	if (!bg_record) {
		error("bg_reset_block: No block given to reset");
		return SLURM_ERROR;
	}

	if (bg_record->job_list)
		ba_remove_job_in_block_job_list(bg_record, job_ptr);

	if ((bg_record->job_running > NO_JOB_RUNNING)
	    && (!bg_record->job_list || !list_count(bg_record->job_list))) {
#ifndef HAVE_BG_L_P
		/* Just in case the slurmctld wasn't up at the
		   time a step completion message came through
		   we will clear all the cnode_bitmaps of the
		   midplanes of this block. So we can use
		   those cnodes on the next job that uses this
		   block.
		*/
		ba_mp_t *ba_mp = NULL;
		ListIterator itr = list_iterator_create(bg_record->ba_mp_list);
		while ((ba_mp = list_next(itr))) {
			if (!ba_mp->used)
				continue;
			if (ba_mp->cnode_usable_bitmap) {
				FREE_NULL_BITMAP(ba_mp->cnode_bitmap);
				ba_mp->cnode_bitmap =
					bit_copy(ba_mp->cnode_usable_bitmap);
			} else if (ba_mp->cnode_bitmap)
				bit_nclear(ba_mp->cnode_bitmap, 0,
					   bit_size(ba_mp->cnode_bitmap)-1);
		}
		list_iterator_destroy(itr);
#endif
		bg_record->job_running = NO_JOB_RUNNING;
	}

	if (bg_record->job_ptr) {
		num_unused_cpus += bg_record->job_ptr->total_cpus;
		bg_record->job_ptr = NULL;
	}

	/* remove user from list */
	bridge_block_sync_users(bg_record);

	/* Don't reset these (boot_(state/count)), they will be
	   reset when state changes, and needs to outlast a job
	   allocation.
	*/
	/* bg_record->boot_state = 0; */
	/* bg_record->boot_count = 0; */

	last_bg_update = time(NULL);

	/* Only remove from the job_running list if
	   job_running == NO_JOB_RUNNING, since blocks in
	   error state could also be in this list and we don't
	   want to remove them.
	*/
	if (bg_record->job_running == NO_JOB_RUNNING
	    && (!bg_record->job_list || !list_count(bg_record->job_list))) {
		remove_from_bg_list(bg_lists->job_running, bg_record);

		/* At this point, no job is running on the block
		   anymore, so if there are any errors on it, free it
		   now.
		*/
		if (bg_record->cnode_err_cnt) {
			if (bg_conf->slurm_debug_flags
			    & DEBUG_FLAG_SELECT_TYPE)
				info("%s has %d in error",
				     bg_record->bg_block_id,
				     bg_record->cnode_err_cnt);
			bg_free_block(bg_record, 0, 1);
		}
	}

	if (!list_count(bg_lists->job_running)
	    && (num_unused_cpus != num_possible_unused_cpus)) {
		/* This should never happen, but if it does reset the
		   num_unused_cpus and go on your way.
		*/
		error("Hey we are here with no jobs and we have only "
		      "%d usuable cpus.  We should have %d!",
		      num_unused_cpus, num_possible_unused_cpus);
		//xassert(0);
		num_unused_cpus = num_possible_unused_cpus;
	}

	return rc;
}

/* block_state_mutex must be locked when coming in */
extern void bg_record_hw_failure(bg_record_t *bg_record, List *ret_kill_list)
{
	List	kill_list = NULL;
	ListIterator itr = NULL;
	slurmdb_qos_rec_t *qos_ptr = NULL;
	struct job_record *found_job_ptr;
	select_jobinfo_t *jobinfo;

	xassert(ret_kill_list);

	if (!bg_record) {
		error("bg_record_hw_failure: no block pointer");
		return;
	}

	/* Don't wait to reboot a bad, single midplane block if there
	 * are other jobs still running that have a preemptable qos
	 * that is in the RebootQOSList */
	if (!bg_conf->sub_blocks || !bg_conf->reboot_qos_bitmap
	    || (bit_ffs(bg_conf->reboot_qos_bitmap) == -1)
	    || (bg_record->mp_count > 1))
		return;

	/* Any block in these states can be ignored */
	if (bg_record->free_cnt
	    || ((!bg_record->err_ratio
		|| (bg_record->err_ratio < bg_conf->max_block_err))
		&& bg_record->action != BG_BLOCK_ACTION_FREE)
	    || !bg_record->job_list
	    || (list_count(bg_record->job_list) <= 1))
		return;

	/* Make sure all jobs still running in this bad block
	 * all have a preemptable qos */
	itr = list_iterator_create(bg_record->job_list);
	while ((found_job_ptr = list_next(itr))) {
		if (found_job_ptr->magic != JOB_MAGIC) {
			error("bg_record_hw_failure: "
			      "bad magic found when "
			      "looking at block %s",
			      bg_record->bg_block_id);
			list_delete_item(itr);
			continue;
		}

		jobinfo = found_job_ptr->select_jobinfo->data;

		if (jobinfo->cleaning || !IS_JOB_RUNNING(found_job_ptr))
			continue;

		qos_ptr = (slurmdb_qos_rec_t *)found_job_ptr->qos_ptr;
		if (qos_ptr) {
			/* If we ever get one that
			   isn't set correctly then we
			   just exit.
			*/
			if (!bit_test(bg_conf->reboot_qos_bitmap,
				      qos_ptr->id)) {
				if (kill_list) {
					list_destroy(kill_list);
					kill_list = NULL;
				}
				break;
			}
			if (!kill_list)
				kill_list = list_create(NULL);
			list_append(kill_list, found_job_ptr);
		}
	}
	list_iterator_destroy(itr);

	if (kill_list) {
		if (!*ret_kill_list) {
			*ret_kill_list = kill_list;
		} else {
			list_transfer(*ret_kill_list, kill_list);
			list_destroy(kill_list);
		}
		kill_list = NULL;
	}
	return;
}

/* block_state_mutex must be unlocked when coming in */
extern void bg_record_post_hw_failure(
	List *kill_list, bool slurmctld_locked)
{
	slurmdb_qos_rec_t *qos_ptr = NULL;
	struct job_record *found_job_ptr;
	select_jobinfo_t *jobinfo;
	ListIterator itr;
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK };

	if (!*kill_list)
		return;

	if (!slurmctld_locked)
		lock_slurmctld(job_write_lock);

	/* The necessary conditions have been met.
	 * Now, kill or requeue the preemptable
	 * jobs */
	itr = list_iterator_create(*kill_list);
	/* Setting cleaning needs to be done before
	   bg_requeue_job is called or we could have
	   an issue where the jobs are requeued over and
	   over again.
	*/
	while ((found_job_ptr = list_next(itr))) {
		jobinfo = found_job_ptr->select_jobinfo->data;
		jobinfo->cleaning = 1;
	}
	list_iterator_reset(itr);
	while ((found_job_ptr = list_next(itr))) {
		qos_ptr = (slurmdb_qos_rec_t*)found_job_ptr->qos_ptr;

		debug("Attempting to requeue %s job %u due "
		      "to excessive node errors",
		      qos_ptr->name, found_job_ptr->job_id);
		bg_requeue_job(found_job_ptr->job_id, 0, 1,
			       JOB_NODE_FAIL, 1);
	}
	list_iterator_destroy(itr);
	list_destroy(*kill_list);
	*kill_list = NULL;
	if (!slurmctld_locked)
		unlock_slurmctld(job_write_lock);
}



/************************* local functions ***************************/

/* block_state_mutex should be locked before calling */
static int _check_all_blocks_error(int node_inx, time_t event_time,
				   char *reason)
{
	bg_record_t *bg_record = NULL;
	ListIterator itr = NULL;
	struct node_record send_node, *node_ptr;
	struct config_record config_rec;
	int total_cpus = 0;
	int rc = SLURM_SUCCESS;

	xassert(node_inx <= node_record_count);
	node_ptr = &node_record_table_ptr[node_inx];

	/* only do this if the node isn't in the DRAINED state.
	   DRAINING is ok */
	if (IS_NODE_DRAINED(node_ptr))
		return rc;

	memset(&send_node, 0, sizeof(struct node_record));
	memset(&config_rec, 0, sizeof(struct config_record));
	send_node.name = xstrdup(node_ptr->name);
	send_node.config_ptr = &config_rec;

	/* here we need to check if there are any other blocks on this
	   midplane and adjust things correctly */
	itr = list_iterator_create(bg_lists->main);
	while ((bg_record = list_next(itr))) {
		/* only look at other nodes in error state */
		if (!(bg_record->state & BG_BLOCK_ERROR_FLAG))
			continue;
		if (!bit_test(bg_record->mp_bitmap, node_inx))
			continue;
		if (bg_record->cpu_cnt >= bg_conf->cpus_per_mp) {
			total_cpus = bg_conf->cpus_per_mp;
			break;
		} else
			total_cpus += bg_record->cpu_cnt;
	}
	list_iterator_destroy(itr);

	send_node.cpus = total_cpus;
	config_rec.cpus = total_cpus;

	if (send_node.cpus) {
		if (!reason)
			reason = "update_block: setting partial node down.";

		if (!node_ptr->reason
		    || !strncmp(node_ptr->reason, "update_block", 12)) {
			xfree(node_ptr->reason);
			node_ptr->reason = xstrdup(reason);
			node_ptr->reason_time = event_time;
			node_ptr->reason_uid = slurm_get_slurm_user_id();
		}
		send_node.node_state = NODE_STATE_ERROR;
		rc = clusteracct_storage_g_node_down(acct_db_conn,
						     &send_node, event_time,
						     reason,
						     node_ptr->reason_uid);
	} else {
		send_node.node_state = NODE_STATE_IDLE;
		rc = clusteracct_storage_g_node_up(acct_db_conn,
						   &send_node, event_time);
	}

	xfree(send_node.name);

	return rc;
}


/* block_state_mutex should be locked before calling */
static int _set_block_nodes_accounting(bg_record_t *bg_record, char *reason)
{
	time_t now = time(NULL);
	int rc = SLURM_SUCCESS;
	int i = 0;

	for(i = 0; i < node_record_count; i++) {
		if (!bit_test(bg_record->mp_bitmap, i))
			continue;
		rc = _check_all_blocks_error(i, now, reason);
	}

	return rc;
}

static void _append_ba_mps(List my_list, int dim,
			   uint16_t *start, uint16_t *end, uint16_t *coords)
{
	ba_mp_t *curr_mp;

	if (dim > SYSTEM_DIMENSIONS)
		return;
	if (dim < SYSTEM_DIMENSIONS) {
		for (coords[dim] = start[dim];
		     coords[dim] <= end[dim];
		     coords[dim]++) {
			/* handle the outter dims here */
			_append_ba_mps(my_list, dim+1, start, end, coords);
		}
		return;
	}

	slurm_mutex_lock(&ba_system_mutex);
	curr_mp = ba_copy_mp(coord2ba_mp(coords));
	slurm_mutex_unlock(&ba_system_mutex);

	if (curr_mp) {
		curr_mp->used = 1;
		list_append(my_list, curr_mp);
	}
}

static void _addto_mp_list(bg_record_t *bg_record,
			   uint16_t *start, uint16_t *end)
{
	uint16_t coords[SYSTEM_DIMENSIONS];
	int dim;
	static int *cluster_dims = NULL;
	static char start_char[SYSTEM_DIMENSIONS+1],
		end_char[SYSTEM_DIMENSIONS+1],
		dim_char[SYSTEM_DIMENSIONS+1];

	if (!cluster_dims) {
		/* do some setup that only needs to happen once. */
		cluster_dims = select_g_ba_get_dims();
		memset(start_char, 0, sizeof(start_char));
		memset(end_char, 0, sizeof(end_char));
		memset(dim_char, 0, sizeof(dim_char));
		for (dim = 0; dim<SYSTEM_DIMENSIONS; dim++)
			dim_char[dim] = alpha_num[cluster_dims[dim]];
	}

	for (dim = 0; dim<SYSTEM_DIMENSIONS; dim++) {
		if ((int16_t)start[dim] < 0) {
			for (dim = 0; dim<SYSTEM_DIMENSIONS; dim++)
				start_char[dim] = alpha_num[start[dim]];
			fatal("bluegene.conf starting coordinate "
			      "is invalid: %s",
			      start_char);
		}

		if (end[dim] >= cluster_dims[dim]) {
			for (dim = 0; dim<SYSTEM_DIMENSIONS; dim++) {
				start_char[dim] = alpha_num[start[dim]];
				end_char[dim] = alpha_num[end[dim]];
				dim_char[dim] = alpha_num[cluster_dims[dim]];
			}
			fatal("bluegene.conf matrix size exceeds space "
			      "defined in "
			      "slurm.conf %sx%s => %s",
			      start_char, end_char, dim_char);
		}
	}
	if (bg_conf->slurm_debug_level >= LOG_LEVEL_DEBUG3) {
		for (dim = 0; dim<SYSTEM_DIMENSIONS; dim++) {
			start_char[dim] = alpha_num[start[dim]];
			end_char[dim] = alpha_num[end[dim]];
		}
		debug3("adding mps: %sx%s", start_char, end_char);
		debug3("slurm.conf:    %s", dim_char);
	}
	_append_ba_mps(bg_record->ba_mp_list, 0, start, end, coords);

}

static int _coord_cmpf_inc(uint16_t *coord_a, uint16_t *coord_b, int dim)
{
	if (dim >= SYSTEM_DIMENSIONS)
		return 0;
	else if (coord_a[dim] < coord_b[dim])
		return -1;
	else if (coord_a[dim] > coord_b[dim])
		return 1;

	return _coord_cmpf_inc(coord_a, coord_b, dim+1);

}

static int _ba_mp_cmpf_inc(void *r1, void *r2)
{
	ba_mp_t *mp_a = *(ba_mp_t **)r1;
	ba_mp_t *mp_b = *(ba_mp_t **)r2;

	int rc = _coord_cmpf_inc(mp_a->coord, mp_b->coord, 0);

	if (!rc) {
		error("You have the mp %s in the list twice",
		      mp_a->coord_str);
	}
	return rc;
}

static void _set_block_avail(bg_record_t *bg_record)
{
	bg_record->avail_set = true;
	if (bg_record->job_ptr) {
		bg_record->avail_cnode_cnt = 0;
		bg_record->avail_job_end = bg_record->job_ptr->end_time;
	} else if (bg_record->job_list) {
		struct job_record *job_ptr;
		ListIterator itr =
			list_iterator_create(bg_record->job_list);

		bg_record->avail_cnode_cnt = bg_record->cnode_cnt;
		while ((job_ptr = list_next(itr))) {
			select_jobinfo_t *jobinfo;
			if (job_ptr->magic != JOB_MAGIC) {
				error("_set_block_avail: bad job magic, "
				      "this should never happen");
				list_delete_item(itr);
				continue;
			}
			jobinfo = job_ptr->select_jobinfo->data;
			if (job_ptr->end_time > bg_record->avail_job_end)
				bg_record->avail_job_end =
					job_ptr->end_time;
			bg_record->avail_cnode_cnt -= jobinfo->cnode_cnt;
		}
		list_iterator_destroy(itr);
	} else {
		bg_record->avail_cnode_cnt = bg_record->cnode_cnt;
		bg_record->avail_job_end = 0;
	}
}

