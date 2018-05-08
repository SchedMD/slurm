/*****************************************************************************\
 *  select_cons_tres.c - Resource selection plugin supporting Trackable
 *  RESources (TRES) policies.
 *****************************************************************************
 *  Copyright (C) 2018 SchedMD LLC
 *  Derived in large part from select/cons_res plugin
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

#include "config.h"

#define _GNU_SOURCE

#include <inttypes.h>
#include <string.h>

#include "src/common/slurm_xlator.h"
#include "src/common/assoc_mgr.h"
#include "src/common/slurm_selecttype_info.h"
#include "src/common/xstring.h"
#include "select_cons_tres.h"
#include "job_test.h"

#define _DEBUG 1	/* Enables module specific debugging */
#define NODEINFO_MAGIC 0x8a5d

/*
 * These symbols are defined here so when we link with something other
 * than the slurmctld we will have these symbols defined. They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
slurm_ctl_conf_t slurmctld_conf __attribute__((weak_import));
struct node_record *node_record_table_ptr __attribute__((weak_import));
List part_list __attribute__((weak_import));
List job_list __attribute__((weak_import));
int node_record_count __attribute__((weak_import));
time_t last_node_update __attribute__((weak_import));
struct switch_record *switch_record_table __attribute__((weak_import));
int switch_record_cnt __attribute__((weak_import));
bitstr_t *avail_node_bitmap __attribute__((weak_import));
bitstr_t *idle_node_bitmap __attribute__((weak_import));
uint16_t *cr_node_num_cores __attribute__((weak_import));
uint32_t *cr_node_cores_offset __attribute__((weak_import));
int slurmctld_tres_cnt __attribute__((weak_import)) = 0;
#else
slurm_ctl_conf_t slurmctld_conf;
struct node_record *node_record_table_ptr;
List part_list;
List job_list;
int node_record_count;
time_t last_node_update;
struct switch_record *switch_record_table;
int switch_record_cnt;
bitstr_t *avail_node_bitmap;
bitstr_t *idle_node_bitmap;
uint16_t *cr_node_num_cores;
uint32_t *cr_node_cores_offset;
int slurmctld_tres_cnt = 0;
#endif

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "select" for SLURM node selection) and <method>
 * is a description of how this plugin satisfies that application.  SLURM will
 * only load select plugins if the plugin_type string has a
 * prefix of "select/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[] = "Trackable RESources (TRES) Selection plugin";
const char plugin_type[] = "select/cons_tres";
const uint32_t plugin_id      = SELECT_PLUGIN_CONS_TRES;
const uint32_t plugin_version = SLURM_VERSION_NUMBER;
const uint32_t pstate_version = 7;	/* version control on saved state */

/* Global variables */
bool       backfill_busy_nodes	= false;
uint16_t   cr_type		= CR_CPU; /* cr_type is overwritten in init() */
int        gang_mode		= -1;
bool       have_dragonfly	= false;
bool       pack_serial_at_end	= false;
bool       preempt_by_part	= false;
bool       preempt_by_qos	= false;
int        preempt_reorder_cnt	= 1;
bool       preempt_strict_order = false;
uint16_t   priority_flags	= 0;
uint64_t   select_debug_flags	= 0;
uint16_t   select_fast_schedule	= 0;
int        select_node_cnt	= 0;
struct node_res_record *select_node_record	= NULL;
struct node_use_record *select_node_usage	= NULL;
struct part_res_record *select_part_record	= NULL;
bool       select_state_initializing = true;
bool       spec_cores_first	= false;
bitstr_t **spec_core_res	= NULL;
bool       topo_optional	= false;

/* Local variables */
static int  bf_window_scale	= 0;

/* Local functions */
static int _add_job_to_res(struct job_record *job_ptr, int action);
static bitstr_t *_array_to_core_bitmap(bitstr_t **core_res);
static bitstr_t **_core_bitmap_to_array(bitstr_t *core_bitmap);
static struct multi_core_data * _create_default_mc(void);
static void _create_part_data(void);
static inline void _dump_nodes(void);
static inline void _dump_parts(struct part_res_record *p_ptr);
static uint16_t _get_job_node_req(struct job_record *job_ptr);
static char *_node_state_str(uint16_t node_state);
static bitstr_t *_pick_first_cores(bitstr_t *avail_node_bitmap,
				   uint32_t node_cnt, uint32_t *core_cnt,
				   bitstr_t ***exc_cores);
static bitstr_t *_sequential_pick(bitstr_t *avail_node_bitmap,
				  uint32_t node_cnt, uint32_t *core_cnt,
				  bitstr_t ***exc_cores);
static int  _sort_part_prio(void *x, void *y);
static void _spec_core_filter(bitstr_t **avail_cores);

/*
 * allocate resources to the given job
 * - add 'struct job_resources' resources to 'struct part_res_record'
 * - add job's memory requirements to 'struct node_res_record'
 *
 * if action = 0 then add cores, memory + GRES (starting new job)
 * if action = 1 then add memory + GRES (adding suspended job)
 * if action = 2 then only add cores (suspended job is resumed)
 */
static int _add_job_to_res(struct job_record *job_ptr, int action)
{
	struct job_resources *job = job_ptr->job_resrcs;
	struct node_record *node_ptr;
	struct part_res_record *p_ptr;
	List gres_list;
	int i, i_first, i_last, n;
	bitstr_t *core_bitmap;

	if (!job || !job->core_bitmap) {
		error("cons_tres: %s: job %u has no job_resrcs info",
		      __func__, job_ptr->job_id);
		return SLURM_ERROR;
	}

	debug3("cons_tres: %s: job:%u action:%d ", __func__, job_ptr->job_id,
	       action);

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE)
		log_job_resources(job_ptr->job_id, job);

	i_first = bit_ffs(job->node_bitmap);
	if (i_first == -1)
		i_last = -2;
	else
		i_last = bit_fls(job->node_bitmap);
	for (i = i_first, n = -1; i <= i_last; i++) {
		if (!bit_test(job->node_bitmap, i))
			continue;
		n++;
		if (job->cpus[n] == 0)
			continue;  /* node removed by job resize */

		node_ptr = select_node_record[i].node_ptr;
		if (action != 2) {
			if (select_node_usage[i].gres_list)
				gres_list = select_node_usage[i].gres_list;
			else
				gres_list = node_ptr->gres_list;
			core_bitmap = copy_job_resources_node(job, n);
			gres_plugin_job_alloc(job_ptr->gres_list, gres_list,
					      job->nhosts, n, job->cpus[n],
					      job_ptr->job_id, node_ptr->name,
					      core_bitmap);
			gres_plugin_node_state_log(gres_list, node_ptr->name);
			FREE_NULL_BITMAP(core_bitmap);
		}

		if (action != 2) {
			if (job->memory_allocated[n] == 0)
				continue;	/* node lost by job resizing */
			select_node_usage[i].alloc_memory +=
				job->memory_allocated[n];
			if ((select_node_usage[i].alloc_memory >
			     select_node_record[i].real_memory)) {
				error("cons_tres: %s: node %s memory is "
				      "overallocated (%"PRIu64") for job %u",
				      __func__, node_ptr->name,
				      select_node_usage[i].alloc_memory,
				      job_ptr->job_id);
			}
		}
		if ((powercap_get_cluster_current_cap() != 0) &&
		    (which_power_layout() == 2)) {
			adapt_layouts(job, job_ptr->details->cpu_freq_max, n,
				      node_ptr->name, true);
		}
	}
	

	/* add cores */
	if (action != 1) {
		for (p_ptr = select_part_record; p_ptr; p_ptr = p_ptr->next) {
			if (p_ptr->part_ptr == job_ptr->part_ptr)
				break;
		}
		if (!p_ptr) {
			char *part_name;
			if (job_ptr->part_ptr)
				part_name = job_ptr->part_ptr->name;
			else
				part_name = job_ptr->partition;
			error("cons_tres: %s: could not find partition %s",
			      __func__, part_name);
			return SLURM_ERROR;
		}
		if (!p_ptr->row) {
			p_ptr->row = xmalloc(p_ptr->num_rows *
					     sizeof(struct part_row_data));
		}

		/* find a row to add this job */
		for (i = 0; i < p_ptr->num_rows; i++) {
			if (!can_job_fit_in_row(job, &(p_ptr->row[i])))
				continue;
			debug3("cons_tres: %s: adding job %u to part %s row %u",
			       __func__, job_ptr->job_id,
			       p_ptr->part_ptr->name, i);
			add_job_to_row(job, &(p_ptr->row[i]));
			break;
		}
		if (i >= p_ptr->num_rows) {
			/*
			 * Job started or resumed and it's allocated resources
			 * are already in use by some other job. Typically due
			 * to manually resuming a job.
			 */
			error("cons_tres: %s: job overflow: "
			      "could not find idle resources for job %u",
			      __func__, job_ptr->job_id);
			/* No row available to record this job */
		}
		/* update the node state */
		for (i = i_first, n = -1; i <= i_last; i++) {
			if (bit_test(job->node_bitmap, i)) {
				n++;
				if (job->cpus[n] == 0)
					continue;  /* node lost by job resize */
				select_node_usage[i].node_state +=
					job->node_req;
			}
		}
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			info("DEBUG: %s (after):", __func__);
			dump_parts(p_ptr);
		}
	}

	return SLURM_SUCCESS;
}

/* Translate system-wide core bitmap to per-node core bitmap array */
static bitstr_t **_core_bitmap_to_array(bitstr_t *core_bitmap)
{
	bitstr_t **core_array = NULL;
	int i, i_first, i_last, j, c;
	int node_inx, last_node_inx = 0, core_offset;
#if _DEBUG
	char tmp[128];
#endif

	if (!core_bitmap)
		return core_array;

#if _DEBUG
	bit_fmt(tmp, sizeof(tmp), core_bitmap);
	error("cons_tres: %s: IN core bitmap %s",__func__, tmp);
#endif

	i_first = bit_ffs(core_bitmap);
	if (i_first == -1)
		return core_array;
	i_last = bit_fls(core_bitmap);
	core_array = build_core_array();
	for (i = i_first; i <= i_last; i++) {
		if (!bit_test(core_bitmap, i))
			continue;
		for (j = last_node_inx; j < select_node_cnt; j++) {
			if (i < select_node_record[j].cume_cores) {
				node_inx = j;
				break;
			}
		}
		if (j >= select_node_cnt) {
			bit_fmt(tmp, sizeof(tmp), core_bitmap);
			error("cons_tres: %s: error translating core bitmap %s",
			      __func__, tmp);
			break;
		}
		/* Copy all core bitmaps for this node here */
		core_array[node_inx] =
			bit_alloc(select_node_record[node_inx].tot_cores);
		core_offset = select_node_record[node_inx].cume_cores -
			      select_node_record[node_inx].tot_cores;
		for (c = 0; c < select_node_record[node_inx].tot_cores; c++) {
			if (bit_test(core_bitmap, core_offset + c))
				bit_set(core_array[node_inx], c);
		}
	}

#if _DEBUG
	for (i = 0; i < select_node_cnt; i++) {
		if (!core_array[i])
			continue;
		bit_fmt(tmp, sizeof(tmp), core_array[i]);
		error("cons_tres: %s: OUT core bitmap[%d] %s", __func__,
		      i, tmp);
	}
#endif

	return core_array;
}

/* Translate per-node core bitmap array to system-wide core bitmap */
static bitstr_t *_array_to_core_bitmap(bitstr_t **core_array)
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
	for (i = 0; i < select_node_cnt; i++) {
		if (!core_array[i])
			continue;
		bit_fmt(tmp, sizeof(tmp), core_array[i]);
		error("cons_tres: %s: OUT core bitmap[%d] %s", __func__,
		      i, tmp);
	}
#endif

	core_bitmap =
		bit_alloc(select_node_record[select_node_cnt-1].cume_cores);
	for (i = 0; i < select_node_cnt; i++) {
		if (!core_array[i])
			continue;
		core_offset = select_node_record[i].cume_cores -
			      select_node_record[i].tot_cores;
		for (c = 0; c < select_node_record[i].tot_cores; c++) {
			if (bit_test(core_array[i], c))
				bit_set(core_bitmap, core_offset + c);
		}
	}

#if _DEBUG
	bit_fmt(tmp, sizeof(tmp), core_bitmap);
	error("cons_tres: %s: IN core bitmap %s",__func__, tmp);
#endif

	return core_bitmap;
}

static struct multi_core_data * _create_default_mc(void)
{
	struct multi_core_data *mc_ptr;
	mc_ptr = xmalloc(sizeof(struct multi_core_data));
	mc_ptr->sockets_per_node = NO_VAL16;
	mc_ptr->cores_per_socket = NO_VAL16;
	mc_ptr->threads_per_core = NO_VAL16;
	/*
	 * Other fields initialized to zero by xmalloc:
	 * mc_ptr->ntasks_per_socket = 0;
	 * mc_ptr->ntasks_per_core   = 0;
	 * mc_ptr->plane_size        = 0;
	 */
	return mc_ptr;
}

/* (re)create the global select_part_record array */
static void _create_part_data(void)
{
	List part_rec_list = NULL;
	ListIterator part_iterator;
	struct part_record *p_ptr;
	struct part_res_record *this_ptr, *last_ptr = NULL;
	int num_parts;

	cr_destroy_part_data(select_part_record);
	select_part_record = NULL;

	num_parts = list_count(part_list);
	if (!num_parts)
		return;
	info("cons_tres: preparing for %d partitions", num_parts);

	part_rec_list = list_create(NULL);
	part_iterator = list_iterator_create(part_list);
	while ((p_ptr = (struct part_record *) list_next(part_iterator))) {
		this_ptr = xmalloc(sizeof(struct part_res_record));
		this_ptr->part_ptr = p_ptr;
		this_ptr->num_rows = p_ptr->max_share;
		if (this_ptr->num_rows & SHARED_FORCE)
			this_ptr->num_rows &= (~SHARED_FORCE);
		if (preempt_by_qos)	/* Add row for QOS preemption */
			this_ptr->num_rows++;
		/* SHARED=EXCLUSIVE sets max_share = 0 */
		if (this_ptr->num_rows < 1)
			this_ptr->num_rows = 1;
		/* we'll leave the 'row' array blank for now */
		this_ptr->row = NULL;
		list_append(part_rec_list, this_ptr);
	}
	list_iterator_destroy(part_iterator);

	/* Sort the select_part_records by priority */
	list_sort(part_rec_list, _sort_part_prio);
	part_iterator = list_iterator_create(part_rec_list);
	while ((this_ptr = (struct part_res_record *)list_next(part_iterator))){
		if (last_ptr)
			last_ptr->next = this_ptr;
		else
			select_part_record = this_ptr;
		last_ptr = this_ptr;
	}
	list_iterator_destroy(part_iterator);
	list_destroy(part_rec_list);
}

static inline void _dump_nodes(void)
{
#if _DEBUG
	struct node_record *node_ptr;
	List gres_list;
	int i;

	for (i = 0; i < select_node_cnt; i++) {
		node_ptr = select_node_record[i].node_ptr;
		info("Node:%s Boards:%u SocketsPerBoard:%u CoresPerSocket:%u "
		     "ThreadsPerCore:%u TotalCores:%u CumeCores:%u TotalCPUs:%u "
		     "PUsPerCore:%u AvailMem:%"PRIu64" AllocMem:%"PRIu64" State:%s(%d)",
		     node_ptr->name,
		     select_node_record[i].boards,
		     select_node_record[i].sockets,
		     select_node_record[i].cores,
		     select_node_record[i].threads,
		     select_node_record[i].tot_cores,
		     select_node_record[i].cume_cores,
		     select_node_record[i].cpus,
		     select_node_record[i].vpus,
		     select_node_record[i].real_memory,
		     select_node_usage[i].alloc_memory,
		     _node_state_str(select_node_usage[i].node_state),
		     select_node_usage[i].node_state);

		if (select_node_usage[i].gres_list)
			gres_list = select_node_usage[i].gres_list;
		else
			gres_list = node_ptr->gres_list;
		if (gres_list)
			gres_plugin_node_state_log(gres_list, node_ptr->name);
	}
#endif
}

static inline void _dump_parts(struct part_res_record *p_ptr)
{
#if _DEBUG
	/* dump partition data */
	for (; p_ptr; p_ptr = p_ptr->next) {
		dump_parts(p_ptr);
	}
#endif
}

/*
 * Determine the node requirements for the job:
 * - does the job need exclusive nodes? (NODE_CR_RESERVED)
 * - run on overcommitted/time-sliced resources? (NODE_CR_AVAILABLE)
 * - run on dedicated cores (NODE_CR_ONE_ROW)
 */
static uint16_t _get_job_node_req(struct job_record *job_ptr)
{
	int max_share = job_ptr->part_ptr->max_share;

	if (max_share == 0)		    /* Partition Shared=EXCLUSIVE */
		return NODE_CR_RESERVED;

	/* Partition is Shared=FORCE */
	if (max_share & SHARED_FORCE)
		return NODE_CR_AVAILABLE;

	if ((max_share > 1) && (job_ptr->details->share_res == 1)) {
		/* part allows sharing, and the user has requested it */
		return NODE_CR_AVAILABLE;
	}

	return NODE_CR_ONE_ROW;
}

static char *_node_state_str(uint16_t node_state)
{
	if (node_state >= NODE_CR_RESERVED)
		return "reserved";	/* Exclusive allocation */
	if (node_state >= NODE_CR_ONE_ROW)
		return "one_row";	/* Dedicated core for this partition */
	return "available";		/* Idle or in-use (shared) */
}

/*
 * Select resources for advanced reservation
 * avail_node_bitmap IN - Available nodes
 * node_cnt IN - required node count
 * core_cnt IN - required core count
 * exc_cores IN/OUT - Cores to AVOID using on input, selected cores on output
 * RET selected nodes
 */
static bitstr_t *_pick_first_cores(bitstr_t *avail_node_bitmap,
				   uint32_t node_cnt, uint32_t *core_cnt,
				   bitstr_t ***exc_cores)
{
#if _DEBUG
	char tmp[128];
#endif
	bitstr_t **avail_cores, **local_cores = NULL, **tmp_cores;
	bitstr_t *picked_node_bitmap = NULL;
	bitstr_t *tmp_core_bitmap;
	int c, c_cnt, i, i_first, i_last;
	int local_node_offset = 0;
	bool fini = false;

	if (!core_cnt || (core_cnt[0] == 0))
		return picked_node_bitmap;

	if (*exc_cores == NULL) {	/* Exclude no cores by default */
#if _DEBUG
		bit_fmt(tmp, sizeof(tmp), avail_node_bitmap);
		info("%s: avail_nodes:%s", __func__, tmp);
		info("%s: exc_cores: NULL", __func__);
#endif
		c = select_node_record[select_node_cnt-1].cume_cores;
		tmp_core_bitmap = bit_alloc(c);
		bit_not(tmp_core_bitmap);
		avail_cores = _core_bitmap_to_array(tmp_core_bitmap);
		local_cores = avail_cores;
		FREE_NULL_BITMAP(tmp_core_bitmap);
	} else {
#if _DEBUG
		tmp_cores = *exc_cores;
		bit_fmt(tmp, sizeof(tmp), avail_node_bitmap);
		info("%s: avail_nodes:%s", __func__, tmp);
		for (i = 0; i < select_node_cnt; i++) {
			if (!tmp_cores[i])
				continue;
			bit_fmt(tmp, sizeof(tmp), tmp_cores[i]);
			info("%s: exc_cores[%d]: %s", __func__, i, tmp);
		}
#endif
		/*
		 * Insure all nodes in avail_node_bitmap are represented
		 * in exc_cores. For now include ALL nodes.
		 */
		c = select_node_record[select_node_cnt-1].cume_cores;
		tmp_core_bitmap = bit_alloc(c);
		bit_not(tmp_core_bitmap);
		avail_cores = _core_bitmap_to_array(tmp_core_bitmap);
		FREE_NULL_BITMAP(tmp_core_bitmap);
		core_array_and_not(avail_cores, *exc_cores);
	}

	_spec_core_filter(avail_cores);
//FIXME: exclude allocated cores, NOT currently enforced in cons_res, add it here

	picked_node_bitmap = bit_alloc(select_node_cnt);
	i_first = bit_ffs(avail_node_bitmap);
	if (i_first != -1)
		i_last = bit_fls(avail_node_bitmap);
	else
		i_last = -2;
	for (i = i_first; i <= i_last; i++) {
		if (!avail_cores[i] || !bit_test(avail_node_bitmap, i))
			continue;
		if (fini ||
		    (bit_set_count(avail_cores[i]) <
		     core_cnt[local_node_offset])) {
			FREE_NULL_BITMAP(avail_cores[i]);
			continue;
		}
		bit_set(picked_node_bitmap, i);
		c_cnt = 0;
		for (c = 0; c < select_node_record[i].tot_cores; c++) {
			if (!bit_test(avail_cores[i], c))
				continue;
			if (++c_cnt > core_cnt[local_node_offset])
				bit_clear(avail_cores[i], c);
		}
		if (core_cnt[++local_node_offset] == 0)
			fini = true;
	}

	if (!fini) {
		info("cons_tres: %s: reservation request can not be satisfied",
		     __func__);
		FREE_NULL_BITMAP(picked_node_bitmap);
		free_core_array(&local_cores);
	} else {
		*exc_cores = avail_cores;
#if _DEBUG
		for (i = 0; i < select_node_cnt; i++) {
			if (!avail_cores[i])
				continue;
			bit_fmt(tmp, sizeof(tmp), avail_cores[i]);
			error("%s: selected cores[%d] %s", __func__, i, tmp);
		}
#endif
	}

	return picked_node_bitmap;
}

/*
 * Select resources for advanced reservation
 * avail_node_bitmap IN - Available nodes
 * node_cnt IN - required node count
 * core_cnt IN - required core count
 * exc_cores IN/OUT - Cores to AVOID using on input, selected cores on output
 * RET selected node bitmap
 */
static bitstr_t *_sequential_pick(bitstr_t *avail_node_bitmap,
				  uint32_t node_cnt, uint32_t *core_cnt,
				  bitstr_t ***exc_cores)
{
#if _DEBUG
	char tmp[128];
#endif
	bitstr_t **avail_cores, **local_cores = NULL, **tmp_cores;
	bitstr_t *picked_node_bitmap;
	char str[300];
	int cores_per_node = 0, extra_cores_needed = -1;
	int total_core_cnt = 0, local_node_offset = 0, num_nodes;
	bitstr_t *tmp_core_bitmap;
	int c, c_cnt, c_target, i;
	bool fini = false, single_core_cnt = false;

	/*
	 * We have these cases here:
	 *	1) node_cnt != 0 && core_cnt != NULL
	 *	2) node_cnt == 0 && core_cnt != NULL
	 *	3) node_cnt != 0 && core_cnt == NULL
	 *	4) node_cnt == 0 && core_cnt == NULL
	 */
	if (core_cnt) {
		num_nodes = bit_set_count(avail_node_bitmap);
		for (i = 0; (i < num_nodes) && core_cnt[i]; i++)
			total_core_cnt += core_cnt[i];
		if ((node_cnt > 1) && (i == 1)) {
			/* single core_cnt element applied across all nodes */
			cores_per_node = MAX((total_core_cnt / node_cnt), 1);
			extra_cores_needed = total_core_cnt -
					     (cores_per_node * node_cnt);
		} else if ((node_cnt == 0) && (i == 1)) {
			/*
			 * single core_cnt element applied across arbitrary
			 * node count
			 */
			single_core_cnt = true;
		}
	}
#if _DEBUG
	if (cores_per_node) {
		info("cons_tres: %s: Reservations requires %d cores (%u each on %u nodes, plus %d)",
		     __func__, total_core_cnt, cores_per_node, node_cnt,
		     extra_cores_needed);
	} else if (single_core_cnt) {
		info("cons_tres: %s: Reservations requires %d cores total",
		     __func__, total_core_cnt);
	} else if (core_cnt && core_cnt[0]) {
		info("cons_tres: %s: Reservations requires %d cores with %d cores on first node",
		     __func__, total_core_cnt, core_cnt[0]);
	} else {
		info("cons_tres: %s: Reservations requires %u nodes total",
		     __func__, node_cnt);
	}
#endif

	picked_node_bitmap = bit_alloc(select_node_cnt);
	if (core_cnt) { /* Reservation is using partial nodes */
		debug2("cons_tres: %s: Reservation is using partial nodes",
		       __func__);
		if (*exc_cores == NULL) {      /* Exclude no cores by default */
#if _DEBUG
			bit_fmt(tmp, sizeof(tmp), avail_node_bitmap);
			info("%s: avail_nodes:%s", __func__, tmp);
			info("%s: exc_cores: NULL", __func__);
#endif
			c = select_node_record[select_node_cnt-1].cume_cores;
			tmp_core_bitmap = bit_alloc(c);
			bit_not(tmp_core_bitmap);
			avail_cores = _core_bitmap_to_array(tmp_core_bitmap);
			local_cores = avail_cores;
			FREE_NULL_BITMAP(tmp_core_bitmap);
		} else {
#if _DEBUG
			tmp_cores = *exc_cores;
			bit_fmt(tmp, sizeof(tmp), avail_node_bitmap);
			info("%s: avail_nodes:%s", __func__, tmp);
			for (i = 0; i < select_node_cnt; i++) {
				if (!tmp_cores[i])
					continue;
				bit_fmt(tmp, sizeof(tmp), tmp_cores[i]);
				info("%s: exc_cores[%d]: %s", __func__, i, tmp);
			}
#endif
			/*
			 * Insure all nodes in avail_node_bitmap are represented
			 * in exc_cores. For now include ALL nodes.
			 */
			c = select_node_record[select_node_cnt-1].cume_cores;
			tmp_core_bitmap = bit_alloc(c);
			bit_not(tmp_core_bitmap);
			avail_cores = _core_bitmap_to_array(tmp_core_bitmap);
			FREE_NULL_BITMAP(tmp_core_bitmap);
			core_array_and_not(avail_cores, *exc_cores);
		}
		_spec_core_filter(avail_cores);
//FIXME: exclude allocated cores, NOT currently enforced in cons_res, add it here

		for (i = 0; i < select_node_cnt; i++) {
			if (fini || !avail_cores[i] ||
			    !bit_test(avail_node_bitmap, i)) {
				FREE_NULL_BITMAP(avail_cores[i]);
				continue;
			}
			c = bit_set_count(avail_cores[i]);
			if (cores_per_node) {
				if (c < cores_per_node)
					continue;	
				if ((c > cores_per_node) &&
				    (extra_cores_needed > 0)) {
					c_cnt = cores_per_node +
					        extra_cores_needed;
					if (c_cnt > c)
						c_target = c;
					else
						c_target = c_cnt;
					extra_cores_needed -= (c_target - c);
				} else {
					c_target = cores_per_node;
				}	
			} else if (single_core_cnt) {
				if (c > total_core_cnt)
					c_target = total_core_cnt;
				else
					c_target = c;
				total_core_cnt -= c_target;
			} else { /* !single_core_cnt */
				if (c < core_cnt[local_node_offset])
					continue;
				c_target = core_cnt[local_node_offset];
			}
			bit_set(picked_node_bitmap, i);
			node_cnt--;
			c_cnt = 0;
			for (c = 0; c < select_node_record[i].tot_cores; c++) {
				if (!bit_test(avail_cores[i], c))
					continue;
				if (c_cnt >= c_target)
					bit_clear(avail_cores[i], c);
				else
					c_cnt++;
			}
			if (cores_per_node) {		/* Test node count */
				if (node_cnt <= 0)
					fini = true;
			} else if (single_core_cnt) {	/* Test core count */
				if (total_core_cnt <= 0)
					fini = true;
			} else {		       /* Test core_cnt array */
				if (core_cnt[++local_node_offset] == 0)
					fini = true;
			}
		}

		if (!fini) {
			info("cons_tres: %s: reservation request can not be satisfied",
			     __func__);
			FREE_NULL_BITMAP(picked_node_bitmap);
			free_core_array(&local_cores);
		} else {
			*exc_cores = avail_cores;
		}
	} else { /* Reservation is using full nodes */
		while (node_cnt) {
			int inx;

			inx = bit_ffs(avail_node_bitmap);
			if (inx < 0)
				break;

			/* Add this node to the final node bitmap */
			bit_set(picked_node_bitmap, inx);
			node_cnt--;

			/* Clear this node from the initial available bitmap */
			bit_clear(avail_node_bitmap, inx);
		}

		if (node_cnt) {
			info("cons_tres: %s: Reservation request can not be satisfied",
			     __func__);
			FREE_NULL_BITMAP(picked_node_bitmap);
		} else {
			bit_fmt(str, sizeof(str), picked_node_bitmap);
			debug2("cons_tres: %s: Sequential pick using nodemap: %s",
			       __func__, str);
		}
	}

	return picked_node_bitmap;
}

static int _sort_part_prio(void *x, void *y)
{
	struct part_res_record *part1 = *(struct part_res_record **) x;
	struct part_res_record *part2 = *(struct part_res_record **) y;

	if (part1->part_ptr->priority_tier > part2->part_ptr->priority_tier)
		return -1;
	if (part1->part_ptr->priority_tier < part2->part_ptr->priority_tier)
		return 1;
	return 0;
}

/* Clear from avail_cores all specialized cores */
static void _spec_core_filter(bitstr_t **avail_cores)
{
	if (!spec_core_res)
		return;	/* No specialized cores */

	xassert(avail_cores);
	core_array_and_not(avail_cores, spec_core_res);
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init(void)
{
	char *topo_param;

	cr_type = slurmctld_conf.select_type_param;
	if (cr_type)
		verbose("%s loaded with argument %u", plugin_name, cr_type);

	select_debug_flags = slurm_get_debug_flags();

	if (slurm_get_preempt_mode() & PREEMPT_MODE_GANG)
		gang_mode = 1;
	else
		gang_mode = 0;

	topo_param = slurm_get_topology_param();
	if (topo_param) {
		if (xstrcasestr(topo_param, "dragonfly"))
			have_dragonfly = true;
		if (xstrcasestr(topo_param, "TopoOptional"))
			topo_optional = true;
		xfree(topo_param);
	}

	priority_flags = slurm_get_priority_flags();

	return SLURM_SUCCESS;
}

extern int fini(void)
{
	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE)
		info("%s shutting down ...", plugin_name);
	else
		verbose("%s shutting down ...", plugin_name);
	cr_destroy_node_data(select_node_usage, select_node_record);
	select_node_record = NULL;
	select_node_usage = NULL;
	cr_destroy_part_data(select_part_record);
	select_part_record = NULL;
	free_core_array(&spec_core_res);

	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern int select_p_state_save(char *dir_name)
{
	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern int select_p_state_restore(char *dir_name)
{
	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern int select_p_job_init(List job_list)
{
	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern bool select_p_node_ranking(struct node_record *node_ptr, int node_cnt)
{
	return false;
}

/* This is Part 1 of a 4-part procedure which can be found in
 * src/slurmctld/read_config.c. The whole story goes like this:
 *
 * Step 1: select_g_node_init          : initializes the global node arrays
 * Step 2: select_g_state_restore      : NO-OP - nothing to restore
 * Step 3: select_g_job_init           : NO-OP - nothing to initialize
 * Step 4: select_g_select_nodeinfo_set: called from reset_job_bitmaps() with
 *                                       each valid recovered job_ptr AND from
 *                                       select_nodes(), this procedure adds
 *                                       job data to the 'select_part_record'
 *                                       global array
 */
extern int select_p_node_init(struct node_record *node_ptr, int node_cnt)
{
	char *preempt_type, *sched_params, *tmp_ptr;
	uint32_t cume_cores = 0;
	int i;

	info("cons_tres: %s", __func__);
	if ((cr_type & (CR_CPU | CR_CORE | CR_SOCKET)) == 0) {
		fatal("Invalid SelectTypeParameters: %s (%u), "
		      "You need at least CR_(CPU|CORE|SOCKET)*",
		      select_type_param_string(cr_type), cr_type);
	}
	if (node_ptr == NULL) {
		error("select_p_node_init: node_ptr == NULL");
		return SLURM_ERROR;
	}
	if (node_cnt < 0) {
		error("select_p_node_init: node_cnt < 0");
		return SLURM_ERROR;
	}

	sched_params = slurm_get_sched_params();
	if (sched_params && strcasestr(sched_params, "preempt_strict_order"))
		preempt_strict_order = true;
	else
		preempt_strict_order = false;
	if (sched_params &&
	    (tmp_ptr = strcasestr(sched_params, "preempt_reorder_count="))) {
		preempt_reorder_cnt = atoi(tmp_ptr + 22);
		if (preempt_reorder_cnt < 0) {
			fatal("Invalid SchedulerParameters preempt_reorder_count: %d",
			      preempt_reorder_cnt);
		}
	}
        if (sched_params &&
            (tmp_ptr = strcasestr(sched_params, "bf_window_linear="))) {
		bf_window_scale = atoi(tmp_ptr + 17);
		if (bf_window_scale <= 0) {
			fatal("Invalid SchedulerParameters bf_window_linear: %d",
			      bf_window_scale);
		}
	} else
		bf_window_scale = 0;

	if (sched_params && strcasestr(sched_params, "pack_serial_at_end"))
		pack_serial_at_end = true;
	else
		pack_serial_at_end = false;
	if (sched_params && strcasestr(sched_params, "spec_cores_first"))
		spec_cores_first = true;
	else
		spec_cores_first = false;
	if (sched_params && strcasestr(sched_params, "bf_busy_nodes"))
		backfill_busy_nodes = true;
	else
		backfill_busy_nodes = false;
	xfree(sched_params);

	preempt_type = slurm_get_preempt_type();
	preempt_by_part = false;
	preempt_by_qos = false;
	if (preempt_type) {
		if (strcasestr(preempt_type, "partition"))
			preempt_by_part = true;
		if (strcasestr(preempt_type, "qos"))
			preempt_by_qos = true;
		xfree(preempt_type);
	}

	/* initial global core data structures */
	select_state_initializing = true;
	select_fast_schedule = slurm_get_fast_schedule();
	cr_init_global_core_data(node_ptr, node_cnt, select_fast_schedule);

	cr_destroy_node_data(select_node_usage, select_node_record);
	select_node_cnt = node_cnt;
	select_node_record = xmalloc(node_cnt *
				     sizeof(struct node_res_record));
	select_node_usage  = xmalloc(node_cnt *
				     sizeof(struct node_use_record));

	for (i = 0; i < select_node_cnt; i++) {
		select_node_record[i].node_ptr = &node_ptr[i];
		select_node_record[i].mem_spec_limit =
				node_ptr[i].mem_spec_limit;
		if (select_fast_schedule) {
			struct config_record *config_ptr;
			config_ptr = node_ptr[i].config_ptr;
			select_node_record[i].cpus    = config_ptr->cpus;
			select_node_record[i].boards  = config_ptr->boards;
			select_node_record[i].sockets = config_ptr->sockets;
			select_node_record[i].cores   = config_ptr->cores;
			select_node_record[i].threads = config_ptr->threads;
			select_node_record[i].vpus    = config_ptr->threads;
			select_node_record[i].real_memory =
					config_ptr->real_memory;
		} else {
			select_node_record[i].cpus    = node_ptr[i].cpus;
			select_node_record[i].boards  = node_ptr[i].boards;
			select_node_record[i].sockets = node_ptr[i].sockets;
			select_node_record[i].cores   = node_ptr[i].cores;
			select_node_record[i].threads = node_ptr[i].threads;
			select_node_record[i].vpus    = node_ptr[i].threads;
			select_node_record[i].real_memory =
					node_ptr[i].real_memory;
		}
		select_node_record[i].tot_cores =
				select_node_record[i].boards  *
				select_node_record[i].sockets *
				select_node_record[i].cores;
		cume_cores += select_node_record[i].tot_cores;
		select_node_record[i].cume_cores = cume_cores;
		if (select_node_record[i].tot_cores >=
		    select_node_record[i].cpus)
			select_node_record[i].vpus = 1;
		select_node_usage[i].node_state = NODE_CR_AVAILABLE;
		gres_plugin_node_state_dealloc_all(
				select_node_record[i].node_ptr->gres_list);
	}
	_create_part_data();
	_dump_nodes();

	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern int select_p_block_init(List part_list)
{
	return SLURM_SUCCESS;
}

/*
 * select_p_job_test - Given a specification of scheduling requirements,
 *	identify the nodes which "best" satisfy the request.
 * 	"best" is defined as either a minimal number of consecutive nodes
 *	or if sharing resources then sharing them with a job of similar size.
 * IN/OUT job_ptr - pointer to job being considered for initiation,
 *                  set's start_time when job expected to start
 * IN/OUT node_bitmap - usable nodes are set on input, nodes not required to
 *			satisfy the request are cleared, other left set
 * IN min_nodes - minimum count of nodes
 * IN max_nodes - maximum count of nodes (0==don't care)
 * IN req_nodes - requested (or desired) count of nodes
 * IN mode - SELECT_MODE_RUN_NOW   (0): try to schedule job now
 *           SELECT_MODE_TEST_ONLY (1): test if job can ever run
 *           SELECT_MODE_WILL_RUN  (2): determine when and where job can run
 * IN preemptee_candidates - List of pointers to jobs which can be preempted.
 * IN/OUT preemptee_job_list - Pointer to list of job pointers. These are the
 *		jobs to be preempted to initiate the pending job. Not set
 *		if mode==SELECT_MODE_TEST_ONLY or input pointer is NULL.
 * IN exc_core_bitmap - Cores to be excluded for use (in advanced reservation)
 * RET zero on success, EINVAL otherwise
 */
extern int select_p_job_test(struct job_record *job_ptr, bitstr_t *node_bitmap,
			     uint32_t min_nodes, uint32_t max_nodes,
			     uint32_t req_nodes, uint16_t mode,
			     List preemptee_candidates,
			     List *preemptee_job_list,
			     bitstr_t *exc_core_bitmap)
{
	int i, rc = EINVAL;
	uint16_t job_node_req;
	bitstr_t **exc_cores;
	char tmp[128];

	xassert(node_bitmap);
	debug2("cons_tres: %s: evaluating job %u", __func__, job_ptr->job_id);
	if (!job_ptr->details)
		return EINVAL;

	/*
	 * FIXME: exc_core_bitmap is a full-system core bitmap to be replaced
	 * with a set of per-node bitmaps in a future release of Slurm
	 */
	exc_cores = _core_bitmap_to_array(exc_core_bitmap);
#if _DEBUG
	if (exc_cores) {
		for (i = 0; i < select_node_cnt; i++) {
			if (!exc_cores[i])
				continue;
			bit_fmt(tmp, sizeof(tmp), exc_cores[i]);
			error("cons_tres: %s: IN exc_cores[%d] %s", __func__,
			      i, tmp);
		}
	}
#endif

	if (slurm_get_use_spec_resources() == 0)
		job_ptr->details->core_spec = NO_VAL16;
	if ((job_ptr->details->core_spec != NO_VAL16) &&
	    (job_ptr->details->whole_node != 1)) {
		info("cons_tres: %s: Setting Exclusive mode for job %u with CoreSpec=%u",
		      __func__, job_ptr->job_id, job_ptr->details->core_spec);
		job_ptr->details->whole_node = 1;
	}

	if (!job_ptr->details->mc_ptr)
		job_ptr->details->mc_ptr = _create_default_mc();
	job_node_req = _get_job_node_req(job_ptr);

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		char *node_mode = "Unknown", *alloc_mode = "Unknown";
		char *core_list = NULL, *node_list, *sep = "";
		if (job_node_req == NODE_CR_RESERVED)
			node_mode = "Exclusive";
		else if (job_node_req == NODE_CR_AVAILABLE)
			node_mode = "OverCommit";
		else if (job_node_req == NODE_CR_ONE_ROW)
			node_mode = "Normal";
		if (mode == SELECT_MODE_WILL_RUN)
			alloc_mode = "Will_Run";
		else if (mode == SELECT_MODE_TEST_ONLY)
			alloc_mode = "Test_Only";
		else if (mode == SELECT_MODE_RUN_NOW)
			alloc_mode = "Run_Now";
		info("cons_tres: %s: job_id:%u node_mode:%s alloc_mode:%s",
		     __func__, job_ptr->job_id, node_mode, alloc_mode);
		if (exc_cores) {
			for (i = 0; i < select_node_cnt; i++) {
				if (!exc_cores[i])
					continue;
				bit_fmt(tmp, sizeof(tmp), exc_cores[i]);
				xstrfmtcat(core_list, "%snode[%d]:%s", sep, i,
					   tmp);
				sep = ",";
			}
		} else {
			core_list = xstrdup("NONE");
		}
		node_list = bitmap2node_name(node_bitmap);
		info("cons_tres: %s: node_list:%s exc_cores:%s", __func__,
		     node_list, core_list);
		xfree(node_list);
		xfree(core_list);
		info("cons_tres: %s: nodes: min:%u max:%u requested:%u avail:%u",
		     __func__, min_nodes, max_nodes, req_nodes,
		     bit_set_count(node_bitmap));
		_dump_nodes();
		_dump_parts(select_part_record);
	}

	if (mode == SELECT_MODE_WILL_RUN) {
		rc = will_run_test(job_ptr, node_bitmap, min_nodes, max_nodes,
				   req_nodes, job_node_req,
				   preemptee_candidates, preemptee_job_list,
				   exc_cores);
	} else if (mode == SELECT_MODE_TEST_ONLY) {
		rc = test_only(job_ptr, node_bitmap, min_nodes, max_nodes,
			       req_nodes, job_node_req);
	} else if (mode == SELECT_MODE_RUN_NOW) {
		rc = run_now(job_ptr, node_bitmap, min_nodes, max_nodes,
			     req_nodes, job_node_req, preemptee_candidates,
			     preemptee_job_list, exc_cores);
	} else {
		fatal("cons_tres: %s: Mode %d is invalid", __func__, mode);
	}

	if ((select_debug_flags & DEBUG_FLAG_CPU_BIND) ||
	    (select_debug_flags & DEBUG_FLAG_SELECT_TYPE)) {
//FIXME: Expand log_job_resources() for TRES
		if (job_ptr->job_resrcs) {
			if (rc != SLURM_SUCCESS) {
				info("cons_tres: %s: error:%s", __func__,
				     slurm_strerror(rc));
			}
			log_job_resources(job_ptr->job_id, job_ptr->job_resrcs);
		} else {
			info("cons_tres: %s: no job_resources info for job %u rc=%d",
			     __func__, job_ptr->job_id, rc);
		}
	}
	free_core_array(&exc_cores);

	return rc;
}

/* Unused for this plugin */
extern int select_p_job_begin(struct job_record *job_ptr)
{
	return SLURM_SUCCESS;
}

/* Determine if allocated nodes are usable (powered up) */
extern int select_p_job_ready(struct job_record *job_ptr)
{
	int i, i_first, i_last;
	struct node_record *node_ptr;

	if (!IS_JOB_RUNNING(job_ptr) && !IS_JOB_SUSPENDED(job_ptr)) {
		/* Gang scheduling might suspend job immediately */
		return 0;
	}

	if ((job_ptr->node_bitmap == NULL) ||
	    ((i_first = bit_ffs(job_ptr->node_bitmap)) == -1))
		return READY_NODE_STATE;
	i_last  = bit_fls(job_ptr->node_bitmap);
	for (i = i_first; i <= i_last; i++) {
		if (bit_test(job_ptr->node_bitmap, i) == 0)
			continue;
		node_ptr = node_record_table_ptr + i;
		if (IS_NODE_POWER_SAVE(node_ptr) || IS_NODE_POWER_UP(node_ptr))
			return 0;
	}

	return READY_NODE_STATE;
}

extern int select_p_job_resized(struct job_record *job_ptr,
				struct node_record *node_ptr)
{
	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);

//FIXME: Add code here
	return SLURM_SUCCESS;
}

extern bool select_p_job_expand_allow(void)
{
	return true;
}

extern int select_p_job_expand(struct job_record *from_job_ptr,
			       struct job_record *to_job_ptr)
{
//FIXME: Add code here
	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern int select_p_job_signal(struct job_record *job_ptr, int signal)
{
	return SLURM_SUCCESS;
}

extern int select_p_job_mem_confirm(struct job_record *job_ptr)
{
//FIXME: Add code here
	return SLURM_SUCCESS;
}

extern int select_p_job_fini(struct job_record *job_ptr)
{
	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);
	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE)
		info("cons_tres: %s: job %u", __func__, job_ptr->job_id);

	rm_job_res(select_part_record, select_node_usage, job_ptr, 0);

	return SLURM_SUCCESS;
}

/*
 * NOTE: This function is not called with gang scheduling because it
 * needs to track how many jobs are running or suspended on each node.
 * This sum is compared with the partition's Shared parameter
 */
extern int select_p_job_suspend(struct job_record *job_ptr, bool indf_susp)
{
	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);
	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE)
		info("cons_tres: %s: job %u", __func__, job_ptr->job_id);

//FIXME: Add code here
	return SLURM_SUCCESS;
}

/* See NOTE with select_p_job_suspend() above */
extern int select_p_job_resume(struct job_record *job_ptr, bool indf_susp)
{
	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);
	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE)
		info("cons_tres: %s: job %u", __func__, job_ptr->job_id);

//FIXME: Add code here
	return SLURM_SUCCESS;
}


extern bitstr_t *select_p_step_pick_nodes(struct job_record *job_ptr,
					  select_jobinfo_t *jobinfo,
					  uint32_t node_count,
					  bitstr_t **avail_nodes)
{
//FIXME: Add code here?
	return NULL;
}

/* Unused for this plugin */
extern int select_p_step_start(struct step_record *step_ptr)
{
	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern int select_p_step_finish(struct step_record *step_ptr, bool killing_step)
{
	return SLURM_SUCCESS;
}

/* This function is always invalid on normal Linux clusters */
extern int select_p_pack_select_info(time_t last_query_time,
				     uint16_t show_flags, Buf *buffer_ptr,
				     uint16_t protocol_version)
{
	return SLURM_ERROR;
}

extern int select_p_select_nodeinfo_pack(select_nodeinfo_t *nodeinfo,
					 Buf buffer,
					 uint16_t protocol_version)
{
//FIXME: Add code here
	return SLURM_SUCCESS;
}

extern int select_p_select_nodeinfo_unpack(select_nodeinfo_t **nodeinfo,
					   Buf buffer,
					   uint16_t protocol_version)
{
//FIXME: Add code here
	return SLURM_SUCCESS;
}

extern select_nodeinfo_t *select_p_select_nodeinfo_alloc(void)
{
//FIXME: Add code here
	return NULL;
}

extern int select_p_select_nodeinfo_free(select_nodeinfo_t *nodeinfo)
{
//FIXME: Add code here
	return SLURM_SUCCESS;
}

extern int select_p_select_nodeinfo_set_all(void)
{
//FIXME: Add code here
	return SLURM_SUCCESS;
}

extern int select_p_select_nodeinfo_set(struct job_record *job_ptr)
{
	int rc;

	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);
#if _DEBUG
	info("%s: job:%u", __func__, job_ptr->job_id);
#endif
	if (IS_JOB_RUNNING(job_ptr))
		rc = _add_job_to_res(job_ptr, 0);
	else if (IS_JOB_SUSPENDED(job_ptr)) {
		if (job_ptr->priority == 0)
			rc = _add_job_to_res(job_ptr, 1);
		else	/* Gang schedule suspend */
			rc = _add_job_to_res(job_ptr, 0);
	} else
		return SLURM_SUCCESS;
	gres_plugin_job_state_log(job_ptr->gres_list, job_ptr->job_id);

	return rc;
}

extern int select_p_select_nodeinfo_get(select_nodeinfo_t *nodeinfo,
					enum select_nodedata_type dinfo,
					enum node_states state,
					void *data)
{
//FIXME: Add code here
	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern int select_p_select_jobinfo_alloc(void)
{
	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern int select_p_select_jobinfo_free(select_jobinfo_t *jobinfo)
{
	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern int select_p_select_jobinfo_set(select_jobinfo_t *jobinfo,
				       enum select_jobdata_type data_type,
				       void *data)
{
	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern int select_p_select_jobinfo_get(select_jobinfo_t *jobinfo,
				       enum select_jobdata_type data_type,
				       void *data)
{
	return SLURM_ERROR;
}

/* Unused for this plugin */
extern select_jobinfo_t *select_p_select_jobinfo_copy(select_jobinfo_t *jobinfo)
{
	return NULL;
}

/* Unused for this plugin */
extern int select_p_select_jobinfo_pack(select_jobinfo_t *jobinfo, Buf buffer,
					uint16_t protocol_version)
{
	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern int select_p_select_jobinfo_unpack(select_jobinfo_t *jobinfo,
					  Buf buffer,
					  uint16_t protocol_version)
{
	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern char *select_p_select_jobinfo_sprint(select_jobinfo_t *jobinfo,
					    char *buf, size_t size, int mode)
{
	if (buf && size) {
		buf[0] = '\0';
		return buf;
	}
	return NULL;
}

/* Unused for this plugin */
extern char *select_p_select_jobinfo_xstrdup(select_jobinfo_t *jobinfo,
					     int mode)
{
	return NULL;
}

/* Unused for this plugin */
extern int select_p_update_block(update_part_msg_t *part_desc_ptr)
{
	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern int select_p_update_sub_node (update_part_msg_t *part_desc_ptr)
{
	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern int select_p_fail_cnode(struct step_record *step_ptr)
{
	return SLURM_SUCCESS;
}

extern int select_p_get_info_from_plugin(enum select_plugindata_info info,
					 struct job_record *job_ptr,
					 void *data)
{
	int rc = SLURM_SUCCESS;
	uint32_t *tmp_32 = (uint32_t *) data;
	List *tmp_list = (List *) data;

	switch (info) {
	case SELECT_CR_PLUGIN:
		*tmp_32 = SELECT_TYPE_CONS_TRES;
		break;
	case SELECT_CONFIG_INFO:
		*tmp_list = NULL;
		break;
	default:
		error("%s: info type %d invalid", __func__, info);
		rc = SLURM_ERROR;
		break;
	}
	return rc;
}

extern int select_p_update_node_config (int index)
{
//FIXME: Add code here
	return SLURM_SUCCESS;
}

extern int select_p_update_node_state(struct node_record *node_ptr)
{
//FIXME: Add code here
	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern int select_p_alter_node_cnt(enum select_node_cnt type, void *data)
{
	return SLURM_SUCCESS;
}

extern int select_p_reconfigure(void)
{
//FIXME: Add code here
select_state_initializing = false;
	return SLURM_SUCCESS;
}

extern bitstr_t *select_p_resv_test(resv_desc_msg_t *resv_desc_ptr,
				    uint32_t node_cnt,
				    bitstr_t *avail_node_bitmap,
				    bitstr_t **core_bitmap)
{
	uint32_t *core_cnt, flags;
	bitstr_t **exc_cores = NULL;
	bitstr_t *picked_nodes = NULL;

	xassert(avail_node_bitmap);
	xassert(resv_desc_ptr);

	/*
	 * FIXME: core_bitmap is a full-system core bitmap to be replaced
	 * with a set of per-node bitmaps in a future release of Slurm
	 */
	if (core_bitmap)
		exc_cores = _core_bitmap_to_array(*core_bitmap);

	core_cnt = resv_desc_ptr->core_cnt;
	flags = resv_desc_ptr->flags;

	if ((flags & RESERVE_FLAG_FIRST_CORES) && core_cnt) {
		/* Reservation request with "Flags=first_cores CoreCnt=#" */
		picked_nodes = _pick_first_cores(avail_node_bitmap, node_cnt,
						 core_cnt, &exc_cores);
		if (picked_nodes && core_bitmap && exc_cores) {
			FREE_NULL_BITMAP(*core_bitmap);
			*core_bitmap = _array_to_core_bitmap(exc_cores);
		}
		free_core_array(&exc_cores);
		return picked_nodes;
	}

	/* When reservation includes a nodelist we use _sequential_pick code */
	if (!switch_record_cnt || !switch_record_table || !node_cnt)  {
		/* Reservation request with "Nodes=* [CoreCnt=#]" */
		picked_nodes = _sequential_pick(avail_node_bitmap, node_cnt,
						core_cnt, &exc_cores);
		if (picked_nodes && core_bitmap && exc_cores) {
			FREE_NULL_BITMAP(*core_bitmap);
			*core_bitmap = _array_to_core_bitmap(exc_cores);
		}
		free_core_array(&exc_cores);
		return picked_nodes;
	}

//FIXME: Add topology support logic here
	free_core_array(&exc_cores);

	return picked_nodes;
}

/* Unused for this plugin */
extern void select_p_ba_init(node_info_msg_t *node_info_ptr, bool sanity_check)
{
	return;
}

/* Unused for this plugin */
extern void select_p_ba_fini(void)
{
	return;
}

/* Unused for this plugin */
extern int *select_p_ba_get_dims(void)
{
	return NULL;
}

/* Unused for this plugin */
extern bitstr_t *select_p_ba_cnodelist2bitmap(char *cnodelist)
{
	return NULL;
}

/* Delete the given select_node_record and select_node_usage arrays */
extern void cr_destroy_node_data(struct node_use_record *node_usage,
				 struct node_res_record *node_data)
{
	int i;

	xfree(node_data);
	if (node_usage) {
		for (i = 0; i < select_node_cnt; i++) {
			FREE_NULL_LIST(node_usage[i].gres_list);
		}
		xfree(node_usage);
	}
}


/* Delete the given list of partition data */
extern void cr_destroy_part_data(struct part_res_record *this_ptr)
{
	while (this_ptr) {
		struct part_res_record *tmp = this_ptr;
		this_ptr = this_ptr->next;
		tmp->part_ptr = NULL;

		if (tmp->row) {
			cr_destroy_row_data(tmp->row, tmp->num_rows);
			tmp->row = NULL;
		}
		xfree(tmp);
	}
}


/* Delete the given partition row data */
extern void cr_destroy_row_data(struct part_row_data *row, uint16_t num_rows)
{
	uint32_t r, n;

	for (r = 0; r < num_rows; r++) {
		if (row[r].row_bitmap) {
			for (n = 0; n < select_node_cnt; n++)
				FREE_NULL_BITMAP(row[r].row_bitmap[n]);
			xfree(row[r].row_bitmap);
		}
		xfree(row[r].job_list);
	}
	xfree(row);
}

/* Log contents of partition structure */
extern void dump_parts(struct part_res_record *p_ptr)
{
	uint32_t n, r;

	info("part:%s rows:%u prio:%u ", p_ptr->part_ptr->name, p_ptr->num_rows,
	     p_ptr->part_ptr->priority_tier);

	if (!p_ptr->row)
		return;

	for (r = 0; r < p_ptr->num_rows; r++) {
		char str[64]; /* print first 64 bits of bitmaps */
		char *sep = "", *tmp = NULL;
		for (n = 0; n < MIN(4, select_node_cnt); n++) {
			if (p_ptr->row[r].row_bitmap &&
			    p_ptr->row[r].row_bitmap[n]) {
				bit_fmt(str, sizeof(str),
					p_ptr->row[r].row_bitmap[n]);
			} else {
				sprintf(str, "[none]");
			}
			xstrfmtcat(tmp, "%sbitmap[%u]:%s", sep, n, str);
			sep = ",";
		}
		info(" row:%u num_jobs:%u: %s", r, p_ptr->row[r].num_jobs, tmp);
		xfree(tmp);
	}
}

/* helper script for cr_sort_part_rows() */
static void _swap_rows(struct part_row_data *a, struct part_row_data *b)
{
	struct part_row_data tmprow;

	memcpy(&tmprow, a, sizeof(struct part_row_data));
	memcpy(a, b, sizeof(struct part_row_data));
	memcpy(b, &tmprow, sizeof(struct part_row_data));
}

/* sort the rows of a partition from "most allocated" to "least allocated" */
extern void cr_sort_part_rows(struct part_res_record *p_ptr)
{
	uint32_t i, j, b, n, r;
	uint32_t *a;

	if (!p_ptr->row)
		return;

	a = xmalloc(sizeof(uint32_t) * p_ptr->num_rows);
	for (r = 0; r < p_ptr->num_rows; r++) {
		if (!p_ptr->row[r].row_bitmap)
			continue;
		for (n = 0; n < select_node_cnt; n++) {
			if (!p_ptr->row[r].row_bitmap[n])
				continue;
			a[r] += bit_set_count(p_ptr->row[r].row_bitmap[n]);
		}
	}
	for (i = 0; i < p_ptr->num_rows; i++) {
		for (j = i+1; j < p_ptr->num_rows; j++) {
			if (a[j] > a[i]) {
				b = a[j];
				a[j] = a[i];
				a[i] = b;
				_swap_rows(&(p_ptr->row[i]), &(p_ptr->row[j]));
			}
		}
	}
	xfree(a);

	return;
}
