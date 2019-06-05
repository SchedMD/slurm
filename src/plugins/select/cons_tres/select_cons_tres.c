/*****************************************************************************\
 *  select_cons_tres.c - Resource selection plugin supporting Trackable
 *  RESources (TRES) policies.
 *****************************************************************************
 *  Copyright (C) 2018 SchedMD LLC
 *  Derived in large part from select/cons_res plugin
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

#define _GNU_SOURCE

#include <inttypes.h>
#include <string.h>

#include "src/common/slurm_xlator.h"
#include "src/common/assoc_mgr.h"
#include "src/common/slurm_selecttype_info.h"
#include "src/common/xstring.h"
#include "select_cons_tres.h"
#include "job_test.h"

#define _DEBUG 0	/* Enables module specific debugging */
#define NODEINFO_MAGIC 0x8a5d

/*
 * These symbols are defined here so when we link with something other
 * than the slurmctld we will have these symbols defined. They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
extern slurmctld_config_t slurmctld_config __attribute__((weak_import));
extern slurm_ctl_conf_t slurmctld_conf __attribute__((weak_import));
extern struct node_record *node_record_table_ptr __attribute__((weak_import));
extern List part_list __attribute__((weak_import));
extern List job_list __attribute__((weak_import));
extern int node_record_count __attribute__((weak_import));
extern time_t last_node_update __attribute__((weak_import));
extern struct switch_record *switch_record_table __attribute__((weak_import));
extern int switch_record_cnt __attribute__((weak_import));
extern bitstr_t *avail_node_bitmap __attribute__((weak_import));
extern uint16_t *cr_node_num_cores __attribute__((weak_import));
extern uint32_t *cr_node_cores_offset __attribute__((weak_import));
extern int slurmctld_tres_cnt __attribute__((weak_import));
#else
slurmctld_config_t slurmctld_config;
slurm_ctl_conf_t slurmctld_conf;
struct node_record *node_record_table_ptr;
List part_list;
List job_list;
int node_record_count;
time_t last_node_update;
struct switch_record *switch_record_table;
int switch_record_cnt;
bitstr_t *avail_node_bitmap;
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
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "select" for Slurm node selection) and <method>
 * is a description of how this plugin satisfies that application.  Slurm will
 * only load select plugins if the plugin_type string has a
 * prefix of "select/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[] = "Trackable RESources (TRES) Selection plugin";
const char *plugin_type = "select/cons_tres";
const uint32_t plugin_id      = SELECT_PLUGIN_CONS_TRES;
const uint32_t plugin_version = SLURM_VERSION_NUMBER;
const uint32_t pstate_version = 7;	/* version control on saved state */

struct select_nodeinfo {
	uint16_t magic;		/* magic number */
	uint16_t alloc_cpus;
	uint64_t alloc_memory;
	uint64_t *tres_alloc_cnt;	/* array of tres counts allocated.
					   NOT PACKED */
	char     *tres_alloc_fmt_str;	/* formatted str of allocated tres */
	double    tres_alloc_weighted;	/* weighted number of tres allocated. */
};

/* Global variables */
bool       backfill_busy_nodes	= false;
int        bf_window_scale	= 0;
uint16_t   cr_type		= CR_CPU; /* cr_type is overwritten in init() */
uint64_t   def_cpu_per_gpu	= 0;
uint64_t   def_mem_per_gpu	= 0;
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

/* Global functions */
extern select_nodeinfo_t *select_p_select_nodeinfo_alloc(void);
extern int select_p_select_nodeinfo_free(select_nodeinfo_t *nodeinfo);

/* Local functions */
static int _add_job_to_res(struct job_record *job_ptr, int action);
static bitstr_t *_array_to_core_bitmap(bitstr_t **core_res);
static bitstr_t **_core_bitmap_to_array(bitstr_t *core_bitmap);
static struct multi_core_data * _create_default_mc(void);
static void _create_part_data(void);
static inline void _dump_nodes(void);
static inline void _dump_parts(struct part_res_record *p_ptr);
static uint16_t _get_job_node_req(struct job_record *job_ptr);
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
 * if action = 1 then add memory + GRES (adding suspended job at restart)
 * if action = 2 then only add cores (suspended job is resumed)
 *
 * See also: rm_job_res() in job_test.c
 */
static int _add_job_to_res(struct job_record *job_ptr, int action)
{
	struct job_resources *job = job_ptr->job_resrcs;
	struct node_record *node_ptr;
	struct part_res_record *p_ptr;
	List node_gres_list;
	int i, i_first, i_last, n;
	bitstr_t *core_bitmap;

	if (!job || !job->core_bitmap) {
		error("%s: %s: %pJ has no job_resrcs info",
		      plugin_type, __func__, job_ptr);
		return SLURM_ERROR;
	}

	debug3("%s: %s: %pJ action:%d ", plugin_type, __func__, job_ptr,
	       action);

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE)
		log_job_resources(job_ptr);

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
				node_gres_list = select_node_usage[i].gres_list;
			else
				node_gres_list = node_ptr->gres_list;
			core_bitmap = copy_job_resources_node(job, n);
			gres_plugin_job_alloc(job_ptr->gres_list,
					      node_gres_list, job->nhosts,
					      i, n, job_ptr->job_id,
					      node_ptr->name, core_bitmap,
					      job_ptr->user_id);
			gres_plugin_node_state_log(node_gres_list,
						   node_ptr->name);
			FREE_NULL_BITMAP(core_bitmap);
		}

		if (action != 2) {
			if (job->memory_allocated[n] == 0)
				continue;	/* node lost by job resizing */
			select_node_usage[i].alloc_memory +=
				job->memory_allocated[n];
			if ((select_node_usage[i].alloc_memory >
			     select_node_record[i].real_memory)) {
				error("%s: %s: node %s memory is "
				      "overallocated (%"PRIu64") for %pJ",
				      plugin_type, __func__, node_ptr->name,
				      select_node_usage[i].alloc_memory,
				      job_ptr);
			}
		}
		if ((powercap_get_cluster_current_cap() != 0) &&
		    (which_power_layout() == 2)) {
			adapt_layouts(job, job_ptr->details->cpu_freq_max, n,
				      node_ptr->name, true);
		}
	}
	
	if (action != 2) {
		gres_build_job_details(job_ptr->gres_list,
				       &job_ptr->gres_detail_cnt,
				       &job_ptr->gres_detail_str);
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
			error("%s: %s: could not find partition %s",
			      plugin_type, __func__, part_name);
			return SLURM_ERROR;
		}
		if (!p_ptr->row) {
			p_ptr->row = xcalloc(p_ptr->num_rows,
					     sizeof(struct part_row_data));
		}

		/* find a row to add this job */
		for (i = 0; i < p_ptr->num_rows; i++) {
			if (!can_job_fit_in_row(job, &(p_ptr->row[i])))
				continue;
			debug3("%s: %s: adding %pJ to part %s row %u",
			      	plugin_type, __func__, job_ptr,
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
			error("%s: %s: job overflow: "
			      "could not find idle resources for %pJ",
			      plugin_type, __func__, job_ptr);
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
	char tmp[128];

	if (!core_bitmap)
		return core_array;

#if _DEBUG
	bit_fmt(tmp, sizeof(tmp), core_bitmap);
	error("%s: %s: IN core bitmap %s", plugin_type, __func__, tmp);
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
			error("%s: %s: error translating core bitmap %s",
			      plugin_type, __func__, tmp);
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
		error("%s: %s: OUT core bitmap[%d] %s", plugin_type, __func__,
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
		error("%s: %s: OUT core bitmap[%d] %s", plugin_type, __func__,
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
	error("%s: %s: IN core bitmap %s", plugin_type, __func__, tmp);
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
	/* Other fields initialized to zero by xmalloc */

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
	info("%s: preparing for %d partitions", plugin_type, num_parts);

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

#if _DEBUG
static char *_node_state_str(uint16_t node_state)
{
	if (node_state >= NODE_CR_RESERVED)
		return "reserved";	/* Exclusive allocation */
	if (node_state >= NODE_CR_ONE_ROW)
		return "one_row";	/* Dedicated core for this partition */
	return "available";		/* Idle or in-use (shared) */
}
#endif

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
	bitstr_t **tmp_cores;
#endif
	bitstr_t **avail_cores, **local_cores = NULL;
	bitstr_t *picked_node_bitmap = NULL;
	bitstr_t *tmp_core_bitmap;
	int c, c_cnt, i;
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
		 * Ensure all nodes in avail_node_bitmap are represented
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

	picked_node_bitmap = bit_alloc(select_node_cnt);
	for (i = 0; i < node_record_count; i++) {
		if (fini ||
		    !avail_cores[i] ||
		    !bit_test(avail_node_bitmap, i) ||
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
		info("%s: %s: reservation request can not be satisfied",
		     plugin_type, __func__);
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
	bitstr_t **tmp_cores;
#endif
	bitstr_t **avail_cores, **local_cores = NULL;
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
		info("%s: %s: Reservations requires %d cores (%u each on %u nodes, plus %d)",
		     plugin_type, __func__, total_core_cnt, cores_per_node,
		     node_cnt, extra_cores_needed);
	} else if (single_core_cnt) {
		info("%s: %s: Reservations requires %d cores total",
		     plugin_type, __func__, total_core_cnt);
	} else if (core_cnt && core_cnt[0]) {
		info("%s: %s: Reservations requires %d cores with %d cores on first node",
		     plugin_type, __func__, total_core_cnt, core_cnt[0]);
	} else {
		info("%s: %s: Reservations requires %u nodes total",
		     plugin_type, __func__, node_cnt);
	}
#endif

	picked_node_bitmap = bit_alloc(select_node_cnt);
	if (core_cnt) { /* Reservation is using partial nodes */
		debug2("%s: %s: Reservation is using partial nodes",
		       plugin_type, __func__);
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
			 * Ensure all nodes in avail_node_bitmap are represented
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
			c_cnt = 0;
			for (c = 0; c < select_node_record[i].tot_cores; c++) {
				if (!bit_test(avail_cores[i], c))
					continue;
				if (c_cnt >= c_target)
					bit_clear(avail_cores[i], c);
				else
					c_cnt++;
			}
			if (c_cnt) {
				bit_set(picked_node_bitmap, i);
				node_cnt--;
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
			info("%s: %s: reservation request can not be satisfied",
			     plugin_type, __func__);
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
			info("%s: %s: Reservation request can not be satisfied",
			     plugin_type, __func__);
			FREE_NULL_BITMAP(picked_node_bitmap);
		} else {
			bit_fmt(str, sizeof(str), picked_node_bitmap);
			debug2("%s: %s: Sequential pick using nodemap: %s",
			       plugin_type, __func__, str);
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
		verbose("%s loaded with argument %u", plugin_type, cr_type);

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
		info("%s shutting down ...", plugin_type);
	else
		verbose("%s shutting down ...", plugin_type);
	cr_destroy_node_data(select_node_usage, select_node_record);
	select_node_record = NULL;
	select_node_usage = NULL;
	cr_destroy_part_data(select_part_record);
	select_part_record = NULL;
	free_core_array(&spec_core_res);
	cr_fini_global_core_data();

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

	info("%s: %s", plugin_type, __func__);
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
	if (xstrcasestr(sched_params, "preempt_strict_order"))
		preempt_strict_order = true;
	else
		preempt_strict_order = false;
	if ((tmp_ptr = xstrcasestr(sched_params, "preempt_reorder_count="))) {
		preempt_reorder_cnt = atoi(tmp_ptr + 22);
		if (preempt_reorder_cnt < 0) {
			error("Invalid SchedulerParameters preempt_reorder_count: %d",
			      preempt_reorder_cnt);
			preempt_reorder_cnt = 1;	/* Use default value */
		}
	}
        if ((tmp_ptr = xstrcasestr(sched_params, "bf_window_linear="))) {
		bf_window_scale = atoi(tmp_ptr + 17);
		if (bf_window_scale <= 0) {
			error("Invalid SchedulerParameters bf_window_linear: %d",
			      bf_window_scale);
			bf_window_scale = 0;		/* Use default value */
		}
	} else
		bf_window_scale = 0;

	if (xstrcasestr(sched_params, "pack_serial_at_end"))
		pack_serial_at_end = true;
	else
		pack_serial_at_end = false;
	if (xstrcasestr(sched_params, "spec_cores_first"))
		spec_cores_first = true;
	else
		spec_cores_first = false;
	if (xstrcasestr(sched_params, "bf_busy_nodes"))
		backfill_busy_nodes = true;
	else
		backfill_busy_nodes = false;
	xfree(sched_params);

	preempt_type = slurm_get_preempt_type();
	preempt_by_part = false;
	preempt_by_qos = false;
	if (preempt_type) {
		if (xstrcasestr(preempt_type, "partition"))
			preempt_by_part = true;
		if (xstrcasestr(preempt_type, "qos"))
			preempt_by_qos = true;
		xfree(preempt_type);
	}

	/* initial global core data structures */
	select_state_initializing = true;
	select_fast_schedule = slurm_get_fast_schedule();
	cr_init_global_core_data(node_ptr, node_cnt, select_fast_schedule);

	cr_destroy_node_data(select_node_usage, select_node_record);
	select_node_cnt = node_cnt;
	select_node_record = xcalloc(node_cnt,
				     sizeof(struct node_res_record));
	select_node_usage  = xcalloc(node_cnt,
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
		select_node_record[i].tot_sockets =
				select_node_record[i].boards *
				select_node_record[i].sockets;
		select_node_record[i].tot_cores =
				select_node_record[i].tot_sockets *
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
	debug2("%s: %s: evaluating %pJ", plugin_type, __func__, job_ptr);
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
			error("%s: %s: IN exc_cores[%d] %s", plugin_type,
			      __func__, i, tmp);
		}
	}
#endif

	if (slurm_get_use_spec_resources() == 0)
		job_ptr->details->core_spec = NO_VAL16;
	if ((job_ptr->details->core_spec != NO_VAL16) &&
	    (job_ptr->details->whole_node != 1)) {
		info("%s: %s: Setting Exclusive mode for %pJ with CoreSpec=%u",
		      plugin_type, __func__, job_ptr,
		      job_ptr->details->core_spec);
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
		info("%s: %s: %pJ node_mode:%s alloc_mode:%s",
		     plugin_type, __func__, job_ptr, node_mode, alloc_mode);
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
		info("%s: %s: node_list:%s exc_cores:%s", plugin_type, __func__,
		     node_list, core_list);
		xfree(node_list);
		xfree(core_list);
		info("%s: %s: nodes: min:%u max:%u requested:%u avail:%u",
		     plugin_type, __func__, min_nodes, max_nodes, req_nodes,
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
		/* Should never get here */
		error("%s: %s: Mode %d is invalid", plugin_type, __func__, mode);
		free_core_array(&exc_cores);
		return EINVAL;
	}

	if ((select_debug_flags & DEBUG_FLAG_CPU_BIND) ||
	    (select_debug_flags & DEBUG_FLAG_SELECT_TYPE)) {
		if (job_ptr->job_resrcs) {
			if (rc != SLURM_SUCCESS) {
				info("%s: %s: error:%s", plugin_type, __func__,
				     slurm_strerror(rc));
			}
			log_job_resources(job_ptr);
			gres_plugin_job_state_log(job_ptr->gres_list,
						  job_ptr->job_id);
		} else {
			info("%s: %s: no job_resources info for %pJ rc=%d",
			     plugin_type, __func__, job_ptr, rc);
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

static void _dump_job_res(struct job_resources *job)
{
	char str[64];

	if (job->core_bitmap)
		bit_fmt(str, sizeof(str), job->core_bitmap);
	else
		sprintf(str, "[no core_bitmap]");
	info("DEBUG: Dump job_resources: nhosts %u core_bitmap %s",
	     job->nhosts, str);
}

extern int select_p_job_resized(struct job_record *job_ptr,
				struct node_record *node_ptr)
{
	struct part_res_record *part_record_ptr = select_part_record;
	struct node_use_record *node_usage = select_node_usage;
	struct job_resources *job = job_ptr->job_resrcs;
	struct part_res_record *p_ptr;
	int i, i_first, i_last, node_inx, n;
	List gres_list;
	bool old_job = false;

	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);
	if (!job || !job->core_bitmap) {
		error("%s: %s: %pJ has no job_resrcs info",
		      plugin_type, __func__, job_ptr);
		return SLURM_ERROR;
	}

	debug3("%s: %s: %pJ node %s",
	       plugin_type, __func__, job_ptr, node_ptr->name);
	if (job_ptr->start_time < slurmctld_config.boot_time)
		old_job = true;
	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE)
		_dump_job_res(job);

	/* subtract memory */
	node_inx  = node_ptr - node_record_table_ptr;
	i_first = bit_ffs(job->node_bitmap);
	if (i_first >= 0)
		i_last  = bit_fls(job->node_bitmap);
	else
		i_last = i_first - 1;
	for (i = i_first, n = 0; i <= i_last; i++) {
		if (!bit_test(job->node_bitmap, i))
			continue;
		if (i != node_inx) {
			n++;
			continue;
		}

		if (job->cpus[n] == 0) {
			info("%s: %s: attempt to remove node %s from %pJ again",
			     plugin_type, __func__, node_ptr->name, job_ptr);
			return SLURM_SUCCESS;
		}

		if (node_usage[i].gres_list)
			gres_list = node_usage[i].gres_list;
		else
			gres_list = node_ptr->gres_list;
		gres_plugin_job_dealloc(job_ptr->gres_list, gres_list, n,
					job_ptr->job_id, node_ptr->name,
					old_job, job_ptr->user_id, true);
		gres_plugin_node_state_log(gres_list, node_ptr->name);

		if (node_usage[i].alloc_memory < job->memory_allocated[n]) {
			error("%s: %s: node %s memory is underallocated "
			      "(%"PRIu64"-%"PRIu64") for %pJ", plugin_type,
			      __func__, node_ptr->name,
			      node_usage[i].alloc_memory,
			      job->memory_allocated[n], job_ptr);
			node_usage[i].alloc_memory = 0;
		} else
			node_usage[i].alloc_memory -= job->memory_allocated[n];

		extract_job_resources_node(job, n);

		break;
	}

	if (IS_JOB_SUSPENDED(job_ptr))
		return SLURM_SUCCESS;	/* No cores allocated to the job now */

	/* subtract cores, reconstruct rows with remaining jobs */
	if (!job_ptr->part_ptr) {
		error("%s: %s: removed %pJ does not have a partition assigned",
		      plugin_type, __func__, job_ptr);
		return SLURM_ERROR;
	}

	for (p_ptr = part_record_ptr; p_ptr; p_ptr = p_ptr->next) {
		if (p_ptr->part_ptr == job_ptr->part_ptr)
			break;
	}
	if (!p_ptr) {
		error("%s: %s: removed %pJ could not find part %s",
		      plugin_type, __func__, job_ptr, job_ptr->part_ptr->name);
		return SLURM_ERROR;
	}

	if (!p_ptr->row)
		return SLURM_SUCCESS;

	/* look for the job in the partition's job_list */
	n = 0;
	for (i = 0; i < p_ptr->num_rows; i++) {
		uint32_t j;
		for (j = 0; j < p_ptr->row[i].num_jobs; j++) {
			if (p_ptr->row[i].job_list[j] != job)
				continue;
			debug3("%s: %s: found %pJ in part %s row %u",
			       plugin_type, __func__, job_ptr,
			       p_ptr->part_ptr->name, i);
			/* found job - we're done, don't actually remove */
			n = 1;
			i = p_ptr->num_rows;
			break;
		}
	}
	if (n == 0) {
		error("%s: %s: could not find %pJ in partition %s",
		      plugin_type, __func__, job_ptr, p_ptr->part_ptr->name);
		return SLURM_ERROR;
	}


	/* some node of job removed from core-bitmap, so rebuild core bitmaps */
	build_row_bitmaps(p_ptr, NULL);

	/*
	 * Adjust the node_state of the node removed from this job.
	 * If all cores are now available, set node_state = NODE_CR_AVAILABLE
	 */
	if (node_usage[node_inx].node_state >= job->node_req) {
		node_usage[node_inx].node_state -= job->node_req;
	} else {
		error("%s: %s: node_state miscount", plugin_type, __func__);
		node_usage[node_inx].node_state = NODE_CR_AVAILABLE;
	}

	return SLURM_SUCCESS;
}

static job_resources_t *_create_job_resources(int node_cnt)
{
	job_resources_t *job_resrcs_ptr;

	job_resrcs_ptr = create_job_resources();
	job_resrcs_ptr->cpu_array_reps = xcalloc(node_cnt, sizeof(uint32_t));
	job_resrcs_ptr->cpu_array_value = xcalloc(node_cnt, sizeof(uint16_t));
	job_resrcs_ptr->cpus = xcalloc(node_cnt, sizeof(uint16_t));
	job_resrcs_ptr->cpus_used = xcalloc(node_cnt, sizeof(uint16_t));
	job_resrcs_ptr->memory_allocated = xcalloc(node_cnt, sizeof(uint64_t));
	job_resrcs_ptr->memory_used = xcalloc(node_cnt, sizeof(uint64_t));
	job_resrcs_ptr->nhosts = node_cnt;
	return job_resrcs_ptr;
}

extern int select_p_job_expand(struct job_record *from_job_ptr,
			       struct job_record *to_job_ptr)
{
	job_resources_t *from_job_resrcs_ptr, *to_job_resrcs_ptr,
		        *new_job_resrcs_ptr;
	struct node_record *node_ptr;
	int first_bit, last_bit, i, node_cnt;
	bool from_node_used, to_node_used;
	int from_node_offset, to_node_offset, new_node_offset;
	bitstr_t *tmp_bitmap, *tmp_bitmap2;

	xassert(from_job_ptr);
	xassert(from_job_ptr->details);
	xassert(to_job_ptr);
	xassert(to_job_ptr->details);

	if (from_job_ptr->job_id == to_job_ptr->job_id) {
		error("%s: %s: attempt to merge %pJ with self",
		      plugin_type, __func__, from_job_ptr);
		return SLURM_ERROR;
	}

	from_job_resrcs_ptr = from_job_ptr->job_resrcs;
	if ((from_job_resrcs_ptr == NULL) ||
	    (from_job_resrcs_ptr->cpus == NULL) ||
	    (from_job_resrcs_ptr->core_bitmap == NULL) ||
	    (from_job_resrcs_ptr->node_bitmap == NULL)) {
		error("%s: %s: %pJ lacks a job_resources struct",
		      plugin_type, __func__, from_job_ptr);
		return SLURM_ERROR;
	}
	to_job_resrcs_ptr = to_job_ptr->job_resrcs;
	if ((to_job_resrcs_ptr == NULL) ||
	    (to_job_resrcs_ptr->cpus == NULL) ||
	    (to_job_resrcs_ptr->core_bitmap == NULL) ||
	    (to_job_resrcs_ptr->node_bitmap == NULL)) {
		error("%s: %s: %pJ lacks a job_resources struct",
		      plugin_type, __func__, to_job_ptr);
		return SLURM_ERROR;
	}

	if (to_job_ptr->gres_list) {	/* Can't reset gres/mps fields today */
		error("%s: %s: %pJ has allocated GRES",
		      plugin_type, __func__, to_job_ptr);
		return SLURM_ERROR;
	}
	if (from_job_ptr->gres_list) {	/* Can't reset gres/mps fields today */
		error("%s: %s: %pJ has allocated GRES",
		      plugin_type, __func__, from_job_ptr);
		return SLURM_ERROR;
	}

	(void) rm_job_res(select_part_record, select_node_usage, from_job_ptr,
			  0, true);
	(void) rm_job_res(select_part_record, select_node_usage, to_job_ptr, 0,
			  true);

	if (to_job_resrcs_ptr->core_bitmap_used) {
		i = bit_size(to_job_resrcs_ptr->core_bitmap_used);
		bit_nclear(to_job_resrcs_ptr->core_bitmap_used, 0, i-1);
	}

	tmp_bitmap = bit_copy(to_job_resrcs_ptr->node_bitmap);
	bit_or(tmp_bitmap, from_job_resrcs_ptr->node_bitmap);
	tmp_bitmap2 = bit_copy(to_job_ptr->node_bitmap);
	bit_or(tmp_bitmap2, from_job_ptr->node_bitmap);
	bit_and(tmp_bitmap, tmp_bitmap2);
	bit_free(tmp_bitmap2);
	node_cnt = bit_set_count(tmp_bitmap);

	new_job_resrcs_ptr = _create_job_resources(node_cnt);
	new_job_resrcs_ptr->ncpus = from_job_resrcs_ptr->ncpus +
				    to_job_resrcs_ptr->ncpus;
	new_job_resrcs_ptr->node_req = to_job_resrcs_ptr->node_req;
	new_job_resrcs_ptr->node_bitmap = tmp_bitmap;
	new_job_resrcs_ptr->nodes = bitmap2node_name(new_job_resrcs_ptr->
						     node_bitmap);
	new_job_resrcs_ptr->whole_node = to_job_resrcs_ptr->whole_node;
	build_job_resources(new_job_resrcs_ptr, node_record_table_ptr,
			    select_fast_schedule);
	xfree(to_job_ptr->node_addr);
	to_job_ptr->node_addr = xcalloc(node_cnt, sizeof(slurm_addr_t));
	to_job_ptr->total_cpus = 0;

	first_bit = MIN(bit_ffs(from_job_resrcs_ptr->node_bitmap),
			bit_ffs(to_job_resrcs_ptr->node_bitmap));
	last_bit =  MAX(bit_fls(from_job_resrcs_ptr->node_bitmap),
			bit_fls(to_job_resrcs_ptr->node_bitmap));
	from_node_offset = to_node_offset = new_node_offset = -1;
	for (i = first_bit; i <= last_bit; i++) {
		from_node_used = to_node_used = false;
		if (bit_test(from_job_resrcs_ptr->node_bitmap, i)) {
			from_node_used = bit_test(from_job_ptr->node_bitmap,i);
			from_node_offset++;
		}
		if (bit_test(to_job_resrcs_ptr->node_bitmap, i)) {
			to_node_used = bit_test(to_job_ptr->node_bitmap, i);
			to_node_offset++;
		}
		if (!from_node_used && !to_node_used)
			continue;
		new_node_offset++;
		node_ptr = node_record_table_ptr + i;
		memcpy(&to_job_ptr->node_addr[new_node_offset],
                       &node_ptr->slurm_addr, sizeof(slurm_addr_t));
		if (from_node_used) {
			/*
			 * Merge alloc info from both "from" and "to" jobs,
			 * leave "from" job with no allocated CPUs or memory
			 *
			 * The following fields should be zero:
			 * from_job_resrcs_ptr->cpus_used[from_node_offset]
			 * from_job_resrcs_ptr->memory_used[from_node_offset];
			 */
			new_job_resrcs_ptr->cpus[new_node_offset] =
				from_job_resrcs_ptr->cpus[from_node_offset];
			from_job_resrcs_ptr->cpus[from_node_offset] = 0;
			new_job_resrcs_ptr->memory_allocated[new_node_offset] =
				from_job_resrcs_ptr->
				memory_allocated[from_node_offset];
			job_resources_bits_copy(new_job_resrcs_ptr,
						new_node_offset,
						from_job_resrcs_ptr,
						from_node_offset);
		}
		if (to_node_used) {
			/*
			 * Merge alloc info from both "from" and "to" jobs
			 *
			 * DO NOT double count the allocated CPUs in partition
			 * with Shared nodes
			 */
			new_job_resrcs_ptr->cpus[new_node_offset] +=
				to_job_resrcs_ptr->cpus[to_node_offset];
			new_job_resrcs_ptr->cpus_used[new_node_offset] +=
				to_job_resrcs_ptr->cpus_used[to_node_offset];
			new_job_resrcs_ptr->memory_allocated[new_node_offset]+=
				to_job_resrcs_ptr->
				memory_allocated[to_node_offset];
			new_job_resrcs_ptr->memory_used[new_node_offset] +=
				to_job_resrcs_ptr->memory_used[to_node_offset];
			job_resources_bits_copy(new_job_resrcs_ptr,
						new_node_offset,
						to_job_resrcs_ptr,
						to_node_offset);
			if (from_node_used) {
				/* Adjust CPU count for shared CPUs */
				int from_core_cnt, to_core_cnt, new_core_cnt;
				from_core_cnt = count_job_resources_node(
							from_job_resrcs_ptr,
							from_node_offset);
				to_core_cnt = count_job_resources_node(
							to_job_resrcs_ptr,
							to_node_offset);
				new_core_cnt = count_job_resources_node(
							new_job_resrcs_ptr,
							new_node_offset);
				if ((from_core_cnt + to_core_cnt) !=
				    new_core_cnt) {
					new_job_resrcs_ptr->
						cpus[new_node_offset] *=
						new_core_cnt;
					new_job_resrcs_ptr->
						cpus[new_node_offset] /=
						(from_core_cnt + to_core_cnt);
				}
			}
		}
		if (to_job_ptr->details->whole_node == 1) {
			to_job_ptr->total_cpus += select_node_record[i].cpus;
		} else {
			to_job_ptr->total_cpus += new_job_resrcs_ptr->
						  cpus[new_node_offset];
		}
	}
	build_job_resources_cpu_array(new_job_resrcs_ptr);
	gres_plugin_job_merge(from_job_ptr->gres_list,
			      from_job_resrcs_ptr->node_bitmap,
			      to_job_ptr->gres_list,
			      to_job_resrcs_ptr->node_bitmap);

	/* Now swap data: "new" -> "to" and clear "from" */
	free_job_resources(&to_job_ptr->job_resrcs);
	to_job_ptr->job_resrcs = new_job_resrcs_ptr;

	to_job_ptr->cpu_cnt = to_job_ptr->total_cpus;
	to_job_ptr->details->min_cpus = to_job_ptr->total_cpus;
	to_job_ptr->details->max_cpus = to_job_ptr->total_cpus;
	from_job_ptr->total_cpus   = 0;
	from_job_resrcs_ptr->ncpus = 0;
	from_job_ptr->details->min_cpus = 0;
	from_job_ptr->details->max_cpus = 0;

	from_job_ptr->total_nodes   = 0;
	from_job_resrcs_ptr->nhosts = 0;
	from_job_ptr->node_cnt      = 0;
	from_job_ptr->details->min_nodes = 0;
	to_job_ptr->total_nodes     = new_job_resrcs_ptr->nhosts;
	to_job_ptr->node_cnt        = new_job_resrcs_ptr->nhosts;

	bit_or(to_job_ptr->node_bitmap, from_job_ptr->node_bitmap);
	bit_nclear(from_job_ptr->node_bitmap, 0, (node_record_count - 1));
	bit_nclear(from_job_resrcs_ptr->node_bitmap, 0,
		   (node_record_count - 1));

	xfree(to_job_ptr->nodes);
	to_job_ptr->nodes = xstrdup(new_job_resrcs_ptr->nodes);
	xfree(from_job_ptr->nodes);
	from_job_ptr->nodes = xstrdup("");
	xfree(from_job_resrcs_ptr->nodes);
	from_job_resrcs_ptr->nodes = xstrdup("");

	(void) _add_job_to_res(to_job_ptr, 0);

	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern int select_p_job_signal(struct job_record *job_ptr, int signal)
{
	return SLURM_SUCCESS;
}

extern int select_p_job_mem_confirm(struct job_record *job_ptr)
{
	int i_first, i_last, i, offset;
	uint64_t avail_mem, lowest_mem = 0;

	xassert(job_ptr);

	if (((job_ptr->bit_flags & NODE_MEM_CALC) == 0) ||
	    (select_fast_schedule != 0))
		return SLURM_SUCCESS;
	if ((job_ptr->details == NULL) ||
	    (job_ptr->job_resrcs == NULL) ||
	    (job_ptr->job_resrcs->node_bitmap == NULL) ||
	    (job_ptr->job_resrcs->memory_allocated == NULL))
		return SLURM_ERROR;
	i_first = bit_ffs(job_ptr->job_resrcs->node_bitmap);
	if (i_first >= 0)
		i_last = bit_fls(job_ptr->job_resrcs->node_bitmap);
	else
		i_last = i_first - 1;
	for (i = i_first, offset = 0; i <= i_last; i++) {
		if (!bit_test(job_ptr->job_resrcs->node_bitmap, i))
			continue;
		avail_mem = select_node_record[i].real_memory -
			    select_node_record[i].mem_spec_limit;
		job_ptr->job_resrcs->memory_allocated[offset] = avail_mem;
		select_node_usage[i].alloc_memory = avail_mem;
		if ((offset == 0) || (lowest_mem > avail_mem))
			lowest_mem = avail_mem;
		offset++;
	}
	job_ptr->details->pn_min_memory = lowest_mem;

	return SLURM_SUCCESS;
}

extern int select_p_job_fini(struct job_record *job_ptr)
{
	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);
	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE)
		info("%s: %s: %pJ", plugin_type, __func__, job_ptr);

	rm_job_res(select_part_record, select_node_usage, job_ptr, 0, true);

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

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		if (indf_susp)
			info("%s: %s: %pJ indf_susp", plugin_type, __func__,
			     job_ptr);
		else
			info("%s: %s: %pJ", plugin_type, __func__, job_ptr);
	}
	if (!indf_susp)
		return SLURM_SUCCESS;

	return rm_job_res(select_part_record, select_node_usage, job_ptr, 2,
			  false);
}

/* See NOTE with select_p_job_suspend() above */
extern int select_p_job_resume(struct job_record *job_ptr, bool indf_susp)
{
	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);
	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		if (indf_susp)
			info("%s: %s: %pJ indf_susp", plugin_type, __func__,
			     job_ptr);
		else
			info("%s: %s: %pJ", plugin_type, __func__, job_ptr);
	}
	if (!indf_susp)
		return SLURM_SUCCESS;

	return _add_job_to_res(job_ptr, 2);
}


extern bitstr_t *select_p_step_pick_nodes(struct job_record *job_ptr,
					  select_jobinfo_t *jobinfo,
					  uint32_t node_count,
					  bitstr_t **avail_nodes)
{
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

extern int select_p_select_nodeinfo_pack(select_nodeinfo_t *nodeinfo,
					 Buf buffer,
					 uint16_t protocol_version)
{
	select_nodeinfo_t *nodeinfo_empty = NULL;

	if (!nodeinfo) {
		/*
		 * We should never get here,
		 * but avoid abort with bad data structures
		 */
		error("%s: nodeinfo is NULL", __func__);
		nodeinfo_empty = xmalloc(sizeof(select_nodeinfo_t));
		nodeinfo = nodeinfo_empty;
	}

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack16(nodeinfo->alloc_cpus, buffer);
		pack64(nodeinfo->alloc_memory, buffer);
		packstr(nodeinfo->tres_alloc_fmt_str, buffer);
		packdouble(nodeinfo->tres_alloc_weighted, buffer);
	}
	xfree(nodeinfo_empty);

	return SLURM_SUCCESS;
}

extern int select_p_select_nodeinfo_unpack(select_nodeinfo_t **nodeinfo,
					   Buf buffer,
					   uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	select_nodeinfo_t *nodeinfo_ptr = NULL;

	nodeinfo_ptr = select_p_select_nodeinfo_alloc();
	*nodeinfo = nodeinfo_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack16(&nodeinfo_ptr->alloc_cpus, buffer);
		safe_unpack64(&nodeinfo_ptr->alloc_memory, buffer);
		safe_unpackstr_xmalloc(&nodeinfo_ptr->tres_alloc_fmt_str,
				       &uint32_tmp, buffer);
		safe_unpackdouble(&nodeinfo_ptr->tres_alloc_weighted, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	error("%s: error unpacking here", __func__);
	select_p_select_nodeinfo_free(nodeinfo_ptr);
	*nodeinfo = NULL;

	return SLURM_ERROR;
}

extern select_nodeinfo_t *select_p_select_nodeinfo_alloc(void)
{
	select_nodeinfo_t *nodeinfo = xmalloc(sizeof(struct select_nodeinfo));

	nodeinfo->magic = NODEINFO_MAGIC;

	return nodeinfo;
}

extern int select_p_select_nodeinfo_free(select_nodeinfo_t *nodeinfo)
{
	if (nodeinfo) {
		if (nodeinfo->magic != NODEINFO_MAGIC) {
			error("%s: nodeinfo magic bad", __func__);
			return EINVAL;
		}
		xfree(nodeinfo->tres_alloc_cnt);
		xfree(nodeinfo->tres_alloc_fmt_str);
		xfree(nodeinfo);
	}
	return SLURM_SUCCESS;
}

extern int select_p_select_nodeinfo_set_all(void)
{
	static time_t last_set_all = 0;
	struct part_res_record *p_ptr;
	struct node_record *node_ptr = NULL;
	int i, n;
	uint32_t alloc_cpus, alloc_cores, node_cores, node_cpus, node_threads;
	uint32_t node_boards, node_sockets, total_node_cores;
	bitstr_t **alloc_core_bitmap = NULL;
	List gres_list;

	/*
	 * only set this once when the last_node_update is newer than
	 * the last time we set things up.
	 */
	if (last_set_all && (last_node_update < last_set_all)) {
		debug2("%s: Node data hasn't changed since %ld", __func__,
		       (long)last_set_all);
		return SLURM_NO_CHANGE_IN_DATA;
	}
	last_set_all = last_node_update;

	/*
	 * Build core bitmap array representing all cores allocated to all
	 * active jobs (running or preempted jobs)
	 */
	for (p_ptr = select_part_record; p_ptr; p_ptr = p_ptr->next) {
		if (!p_ptr->row)
			continue;
		for (i = 0; i < p_ptr->num_rows; i++) {
			if (!p_ptr->row[i].row_bitmap)
				continue;
			if (!alloc_core_bitmap) {
				alloc_core_bitmap =
				 	copy_core_array(
						p_ptr->row[i].row_bitmap);
			} else {
				core_array_or(alloc_core_bitmap,
					      p_ptr->row[i].row_bitmap);
			}
		}
	}

	for (n = 0, node_ptr = node_record_table_ptr;
	     n < select_node_cnt; n++, node_ptr++) {
		select_nodeinfo_t *nodeinfo = NULL;
		/*
		 * We have to use the '_g_' here to make sure we get the
		 * correct data to work on.  i.e. select/cray calls this plugin
		 * and has it's own struct.
		 */
		select_g_select_nodeinfo_get(node_ptr->select_nodeinfo,
					     SELECT_NODEDATA_PTR, 0,
					     (void *)&nodeinfo);
		if (!nodeinfo) {
			error("%s: no nodeinfo returned from structure",
			      __func__);
			continue;
		}

		if (slurmctld_conf.fast_schedule) {
			node_boards  = node_ptr->config_ptr->boards;
			node_sockets = node_ptr->config_ptr->sockets;
			node_cores   = node_ptr->config_ptr->cores;
			node_cpus    = node_ptr->config_ptr->cpus;
			node_threads = node_ptr->config_ptr->threads;
		} else {
			node_boards  = node_ptr->boards;
			node_sockets = node_ptr->sockets;
			node_cores   = node_ptr->cores;
			node_cpus    = node_ptr->cpus;
			node_threads = node_ptr->threads;
		}
		total_node_cores = node_boards * node_sockets * node_cores;

		if (alloc_core_bitmap && alloc_core_bitmap[n])
			alloc_cores = bit_set_count(alloc_core_bitmap[n]);
		else
			alloc_cores = 0;

		/*
		 * Administrator could resume suspended jobs and oversubscribe
		 * cores, avoid reporting more cores in use than configured
		 */
		if (alloc_cores > total_node_cores)
			alloc_cpus = total_node_cores;
		else
			alloc_cpus = alloc_cores;

		/*
		 * The minimum allocatable unit may a core, so scale by thread
		 * count up to the proper CPU count as needed
		 */
		if (total_node_cores < node_cpus)
			alloc_cpus *= node_threads;
		nodeinfo->alloc_cpus = alloc_cpus;

		if (select_node_record) {
			nodeinfo->alloc_memory =
				select_node_usage[n].alloc_memory;
		} else {
			nodeinfo->alloc_memory = 0;
		}

		/* Build allocated TRES info */
		if (!nodeinfo->tres_alloc_cnt)
			nodeinfo->tres_alloc_cnt = xcalloc(slurmctld_tres_cnt,
							   sizeof(uint64_t));
		nodeinfo->tres_alloc_cnt[TRES_ARRAY_CPU] = alloc_cpus;
		nodeinfo->tres_alloc_cnt[TRES_ARRAY_MEM] =
			nodeinfo->alloc_memory;
		if (select_node_usage[n].gres_list)
			gres_list = select_node_usage[n].gres_list;
		else
			gres_list = node_ptr->gres_list;
		gres_set_node_tres_cnt(gres_list, nodeinfo->tres_alloc_cnt,
				       false);

		xfree(nodeinfo->tres_alloc_fmt_str);
		nodeinfo->tres_alloc_fmt_str =
			assoc_mgr_make_tres_str_from_array(
						nodeinfo->tres_alloc_cnt,
						TRES_STR_CONVERT_UNITS, false);
		nodeinfo->tres_alloc_weighted =
			assoc_mgr_tres_weighted(nodeinfo->tres_alloc_cnt,
					node_ptr->config_ptr->tres_weights,
					priority_flags, false);
	}
	free_core_array(&alloc_core_bitmap);

	return SLURM_SUCCESS;
}

extern int select_p_select_nodeinfo_set(struct job_record *job_ptr)
{
	int rc;

	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);
#if _DEBUG
	info("%s: %pJ", __func__, job_ptr);
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
	int rc = SLURM_SUCCESS;
	uint16_t *uint16 = (uint16_t *) data;
	uint64_t *uint64 = (uint64_t *) data;
	char **tmp_char = (char **) data;
	double *tmp_double = (double *) data;
	select_nodeinfo_t **select_nodeinfo = (select_nodeinfo_t **) data;

	if (nodeinfo == NULL) {
		error("%s: nodeinfo not set", __func__);
		return SLURM_ERROR;
	}

	if (nodeinfo->magic != NODEINFO_MAGIC) {
		error("%s: jobinfo magic bad", __func__);
		return SLURM_ERROR;
	}

	switch (dinfo) {
	case SELECT_NODEDATA_SUBCNT:
		if (state == NODE_STATE_ALLOCATED)
			*uint16 = nodeinfo->alloc_cpus;
		else
			*uint16 = 0;
		break;
	case SELECT_NODEDATA_PTR:
		*select_nodeinfo = nodeinfo;
		break;
	case SELECT_NODEDATA_MEM_ALLOC:
		*uint64 = nodeinfo->alloc_memory;
		break;
	case SELECT_NODEDATA_TRES_ALLOC_FMT_STR:
		*tmp_char = xstrdup(nodeinfo->tres_alloc_fmt_str);
		break;
	case SELECT_NODEDATA_TRES_ALLOC_WEIGHTED:
		*tmp_double = nodeinfo->tres_alloc_weighted;
		break;
	default:
		error("%s: Unsupported option %d", __func__, dinfo);
		rc = SLURM_ERROR;
		break;
	}
	return rc;
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
	case SELECT_SINGLE_JOB_TEST:
		*tmp_32 = 1;
		break;
	default:
		error("%s: info type %d invalid", __func__, info);
		rc = SLURM_ERROR;
		break;
	}
	return rc;
}

extern int select_p_update_node_config(int index)
{
	if (index >= select_node_cnt) {
		error("%s: index too large (%d > %d)", __func__, index,
		      select_node_cnt);
		return SLURM_ERROR;
	}

	/*
	 * Socket and core count can be changed when KNL node reboots in a
	 * different NUMA configuration
	 */
	if ((select_fast_schedule == 1) &&
	    (select_node_record[index].sockets !=
	     select_node_record[index].node_ptr->config_ptr->sockets) &&
	    (select_node_record[index].cores !=
	     select_node_record[index].node_ptr->config_ptr->cores) &&
	    ((select_node_record[index].sockets *
	      select_node_record[index].cores) ==
	     (select_node_record[index].node_ptr->sockets *
	      select_node_record[index].node_ptr->cores))) {
		select_node_record[index].cores =
			select_node_record[index].node_ptr->config_ptr->cores;
		select_node_record[index].sockets =
			select_node_record[index].node_ptr->config_ptr->sockets;
		select_node_record[index].tot_sockets =
			select_node_record[index].boards *
			select_node_record[index].sockets;
	}

	if (select_fast_schedule)
		return SLURM_SUCCESS;

	select_node_record[index].real_memory =
		select_node_record[index].node_ptr->real_memory;
	select_node_record[index].mem_spec_limit =
		select_node_record[index].node_ptr->mem_spec_limit;

	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern int select_p_update_node_state(struct node_record *node_ptr)
{
	return SLURM_SUCCESS;
}

extern int select_p_reconfigure(void)
{
	ListIterator job_iterator;
	struct job_record *job_ptr;
	int cleaning_job_cnt = 0, rc = SLURM_SUCCESS, run_time;
	time_t now = time(NULL);

	info("%s: select_p_reconfigure", plugin_type);
	select_debug_flags = slurm_get_debug_flags();
	def_cpu_per_gpu = 0;
	def_mem_per_gpu = 0;
	if (slurmctld_conf.job_defaults_list) {
		def_cpu_per_gpu = get_def_cpu_per_gpu(
					slurmctld_conf.job_defaults_list);
		def_mem_per_gpu = get_def_mem_per_gpu(
					slurmctld_conf.job_defaults_list);
	}

	rc = select_p_node_init(node_record_table_ptr, node_record_count);
	if (rc != SLURM_SUCCESS)
		return rc;

	/* reload job data */
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (IS_JOB_RUNNING(job_ptr)) {
			/* add the job */
			_add_job_to_res(job_ptr, 0);
		} else if (IS_JOB_SUSPENDED(job_ptr)) {
			/* add the job in a suspended state */
			if (job_ptr->priority == 0)
				(void) _add_job_to_res(job_ptr, 1);
			else	/* Gang schedule suspend */
				(void) _add_job_to_res(job_ptr, 0);
		} else if (job_cleaning(job_ptr)) {
			cleaning_job_cnt++;
			run_time = (int) difftime(now, job_ptr->end_time);
			if (run_time >= 300) {
				info("%pJ NHC hung for %d secs, releasing "
				     "resources now, may underflow later)",
				     job_ptr, run_time);
				/*
				 * If/when NHC completes, it will release
				 * resources that are not marked as allocated
				 * to this job without line below.
				 */
				//_add_job_to_res(job_ptr, 0);
				uint16_t released = 1;
				select_g_select_jobinfo_set(
					               job_ptr->select_jobinfo,
					               SELECT_JOBDATA_RELEASED,
					               &released);
			} else {
				_add_job_to_res(job_ptr, 0);
			}
		}
	}
	list_iterator_destroy(job_iterator);
	select_state_initializing = false;

	if (cleaning_job_cnt) {
		info("%d jobs are in cleaning state (running Node Health Check)",
		     cleaning_job_cnt);
	}

	return SLURM_SUCCESS;
}

extern bitstr_t *select_p_resv_test(resv_desc_msg_t *resv_desc_ptr,
				    uint32_t node_cnt,
				    bitstr_t *avail_node_bitmap,
				    bitstr_t **core_bitmap)
{
	bitstr_t **switches_bitmap;		/* nodes on this switch */
	bitstr_t ***switches_core_bitmap;	/* cores on this switch */
	int       *switches_core_cnt;		/* total cores on switch */
	int       *switches_node_cnt;		/* total nodes on switch */
	int       *switches_required;		/* set if has required node */

	bitstr_t *avail_nodes_bitmap = NULL;	/* nodes on any switch */
	bitstr_t *picked_node_bitmap;
	uint32_t *core_cnt, flags;
	bitstr_t **exc_core_bitmap = NULL, **picked_core_bitmap;
	int32_t prev_rem_cores, rem_cores = 0, rem_cores_save, rem_nodes;
	uint32_t cores_per_node = 1;	/* Minimum cores per node to consider */
	bool aggr_core_cnt = false, clear_core, sufficient;
	int c, i, i_first, i_last, j, k, n;
	int best_fit_inx, best_fit_nodes;
	int best_fit_location = 0, best_fit_sufficient;

	xassert(avail_node_bitmap);
	xassert(resv_desc_ptr);

	/*
	 * FIXME: core_bitmap is a full-system core bitmap to be replaced
	 * with a set of per-node bitmaps in a future release of Slurm
	 */
	if (core_bitmap)
		exc_core_bitmap = _core_bitmap_to_array(*core_bitmap);

	core_cnt = resv_desc_ptr->core_cnt;
	flags = resv_desc_ptr->flags;

	if ((flags & RESERVE_FLAG_FIRST_CORES) && core_cnt) {
		/* Reservation request with "Flags=first_cores CoreCnt=#" */
		avail_nodes_bitmap = _pick_first_cores(avail_node_bitmap,
						       node_cnt, core_cnt,
						       &exc_core_bitmap);
		if (avail_nodes_bitmap && core_bitmap && exc_core_bitmap) {
			FREE_NULL_BITMAP(*core_bitmap);
			*core_bitmap = _array_to_core_bitmap(exc_core_bitmap);
		}
		free_core_array(&exc_core_bitmap);
		return avail_nodes_bitmap;
	}

	/* When reservation includes a nodelist we use _sequential_pick code */
	if (!switch_record_cnt || !switch_record_table || !node_cnt)  {
		/* Reservation request with "Nodes=* [CoreCnt=#]" */
		avail_nodes_bitmap = _sequential_pick(avail_node_bitmap,
						      node_cnt, core_cnt,
						      &exc_core_bitmap);
		if (avail_nodes_bitmap && core_bitmap && exc_core_bitmap) {
			FREE_NULL_BITMAP(*core_bitmap);
			*core_bitmap = _array_to_core_bitmap(exc_core_bitmap);
		}
		free_core_array(&exc_core_bitmap);
		return avail_nodes_bitmap;
	}

	/* Use topology state information */
	if (bit_set_count(avail_node_bitmap) < node_cnt) {
		free_core_array(&exc_core_bitmap);
		return NULL;
	}

	if (core_cnt && spec_core_res) {
		if (!exc_core_bitmap)
			exc_core_bitmap = build_core_array();
		core_array_or(exc_core_bitmap, spec_core_res);
	}

	rem_nodes = node_cnt;
	if (core_cnt && core_cnt[1]) {	/* Array of core counts */
		for (j = 0; core_cnt[j]; j++) {
			rem_cores += core_cnt[j];
			if (j == 0)
				cores_per_node = core_cnt[j];
			else if (cores_per_node > core_cnt[j])
				cores_per_node = core_cnt[j];
		}
	} else if (core_cnt) {		/* Aggregate core count */
		rem_cores = core_cnt[0];
		cores_per_node = core_cnt[0] / MAX(node_cnt, 1);
		aggr_core_cnt = true;
	} else if (cr_node_num_cores)
		cores_per_node = cr_node_num_cores[0];
	else
		cores_per_node = 1;
	rem_cores_save = rem_cores;

	/*
	 * Construct a set of switch array entries,
	 * use the same indexes as switch_record_table in slurmctld
	 */
	switches_bitmap = xcalloc(switch_record_cnt, sizeof(bitstr_t *));
	switches_core_bitmap = xcalloc(switch_record_cnt, sizeof(bitstr_t **));
	switches_core_cnt = xcalloc(switch_record_cnt, sizeof(int));
	switches_node_cnt = xcalloc(switch_record_cnt, sizeof(int));
	switches_required = xcalloc(switch_record_cnt, sizeof(int));

	for (i = 0; i < switch_record_cnt; i++) {
		switches_bitmap[i] =
			bit_copy(switch_record_table[i].node_bitmap);
		bit_and(switches_bitmap[i], avail_node_bitmap);
		switches_node_cnt[i] = bit_set_count(switches_bitmap[i]);
		switches_core_bitmap[i] = mark_avail_cores(switches_bitmap[i],
							   NO_VAL16);
		if (exc_core_bitmap) {
			core_array_and_not(switches_core_bitmap[i],
					   exc_core_bitmap);
		}
		switches_core_cnt[i] =
			count_core_array_set(switches_core_bitmap[i]);
		debug2("switch:%d nodes:%d cores:%d",
		       i, switches_node_cnt[i], switches_core_cnt[i]);
	}

	/* Remove nodes with fewer available cores than needed */
	if (core_cnt) {
		n = 0;
		for (j = 0; j < switch_record_cnt; j++) {
			i_first = bit_ffs(switches_bitmap[j]);
			if (i_first >= 0)
				i_last = bit_fls(switches_bitmap[j]);
			else
				i_last = i_first - 1;
			for (i = i_first; i <= i_last; i++) {
				if (!bit_test(switches_bitmap[j], i))
					continue;
				c = select_node_record[i].tot_cores;
				if (exc_core_bitmap && exc_core_bitmap[i])
					c -= bit_set_count(exc_core_bitmap[i]);
				clear_core = false;
				if (aggr_core_cnt && (c < cores_per_node)) {
					clear_core = true;
				} else if (aggr_core_cnt) {
					;
				} else if (c < core_cnt[n]) {
					clear_core = true;
				} else if (core_cnt[n]) {
					n++;
				}
				if (!clear_core)
					continue;
				for (k = 0; k < switch_record_cnt; k++) {
					if (!switches_bitmap[k] ||
					    !bit_test(switches_bitmap[k], i))
						continue;
					bit_clear(switches_bitmap[k], i);
					switches_node_cnt[k]--;
					switches_core_cnt[k] -= c;
				}
			}
		}
	}

#if SELECT_DEBUG
	/* Don't compile this, it slows things down too much */
	for (i = 0; i < switch_record_cnt; i++) {
		char *node_names = NULL;
		if (switches_node_cnt[i])
			node_names = bitmap2node_name(switches_bitmap[i]);
		info("switch=%s nodes=%u:%s cores:%d required:%u speed=%u",
		     switch_record_table[i].name,
		     switches_node_cnt[i], node_names,
		     switches_core_cnt[i], switches_required[i],
		     switch_record_table[i].link_speed);
		xfree(node_names);
	}
#endif

	/* Determine lowest level switch satisfying request with best fit */
	best_fit_inx = -1;
	for (j = 0; j < switch_record_cnt; j++) {
		if ((switches_node_cnt[j] < rem_nodes) ||
		    (core_cnt && (switches_core_cnt[j] < rem_cores)))
			continue;
		if ((best_fit_inx == -1) ||
		    (switch_record_table[j].level <
		     switch_record_table[best_fit_inx].level) ||
		    ((switch_record_table[j].level ==
		      switch_record_table[best_fit_inx].level) &&
		     (switches_node_cnt[j] < switches_node_cnt[best_fit_inx])))
			/* We should use core count by switch here as well */
			best_fit_inx = j;
	}
	if (best_fit_inx == -1) {
		debug("%s: could not find resources for reservation", __func__);
		goto fini;
	}

	/* Identify usable leafs (within higher switch having best fit) */
	for (j = 0; j < switch_record_cnt; j++) {
		if ((switch_record_table[j].level != 0) ||
		    (!bit_super_set(switches_bitmap[j],
				    switches_bitmap[best_fit_inx]))) {
			switches_node_cnt[j] = 0;
		}
	}

	/* Select resources from these leafs on a best-fit basis */
	avail_nodes_bitmap = bit_alloc(node_record_count);
	while (rem_nodes > 0) {
		best_fit_nodes = best_fit_sufficient = 0;
		for (j = 0; j < switch_record_cnt; j++) {
			if (switches_node_cnt[j] == 0)
				continue;
			if (core_cnt) {
				sufficient =
					(switches_node_cnt[j] >= rem_nodes) &&
					(switches_core_cnt[j] >= rem_cores);
			} else
				sufficient = switches_node_cnt[j] >= rem_nodes;
			/*
			 * If first possibility OR
			 * first set large enough for request OR
			 * tightest fit (less resource waste) OR
			 * nothing yet large enough, but this is biggest
			 */
			if ((best_fit_nodes == 0) ||
			    (sufficient && (best_fit_sufficient == 0)) ||
			    (sufficient &&
			     (switches_node_cnt[j] < best_fit_nodes)) ||
			    ((sufficient == 0) &&
			     (switches_node_cnt[j] > best_fit_nodes))) {
				best_fit_nodes = switches_node_cnt[j];
				best_fit_location = j;
				best_fit_sufficient = sufficient;
			}
		}
		if (best_fit_nodes == 0)
			break;
		/* Use select nodes from this leaf */
		i_first = bit_ffs(switches_bitmap[best_fit_location]);
		if (i_first >= 0)
			i_last = bit_fls(switches_bitmap[best_fit_location]);
		else
			i_last = i_first - 1;
		for (i = i_first; i <= i_last; i++) {
			if (!bit_test(switches_bitmap[best_fit_location], i))
				continue;
			bit_clear(switches_bitmap[best_fit_location], i);
			switches_node_cnt[best_fit_location]--;

			if (bit_test(avail_nodes_bitmap, i)) {
				/*
				 * node on multiple leaf switches
				 * and already selected
				 */
				continue;
			}
			c = select_node_record[i].tot_cores;
			if (exc_core_bitmap && exc_core_bitmap[i])
				c -= bit_set_count(exc_core_bitmap[i]);
			if (c < cores_per_node)
				continue;
			debug2("Using node %d with %d cores available", i, c);
			bit_set(avail_nodes_bitmap, i);
			rem_cores -= c;
			if (--rem_nodes <= 0)
				break;
		}
		switches_node_cnt[best_fit_location] = 0;
	}

	if ((rem_nodes > 0) || (rem_cores > 0))	/* insufficient resources */
		FREE_NULL_BITMAP(avail_nodes_bitmap);

fini:	for (i = 0; i < switch_record_cnt; i++) {
		FREE_NULL_BITMAP(switches_bitmap[i]);
		free_core_array(&switches_core_bitmap[i]);
	}
	xfree(switches_bitmap);
	xfree(switches_core_bitmap);
	xfree(switches_core_cnt);
	xfree(switches_node_cnt);
	xfree(switches_required);

	if (avail_nodes_bitmap && core_cnt) {
		/* Reservation is using partial nodes */
		picked_node_bitmap = bit_alloc(bit_size(avail_node_bitmap));
		picked_core_bitmap = build_core_array();

		rem_cores = rem_cores_save;
		n = 0;
		prev_rem_cores = -1;
		while (rem_cores) {
			int avail_cores_in_node, inx, i;

			inx = bit_ffs(avail_nodes_bitmap);
			if ((inx < 0) && aggr_core_cnt && (rem_cores > 0) &&
			    (rem_cores != prev_rem_cores)) {
				/*
				 * Make another pass over nodes to reach
				 * requested aggregate core count
				 */
				bit_or(avail_nodes_bitmap, picked_node_bitmap);
				inx = bit_ffs(avail_nodes_bitmap);
				prev_rem_cores = rem_cores;
				cores_per_node = 1;
			}
			if (inx < 0)
				break;

			debug2("Using node inx:%d cores_per_node:%d rem_cores:%u",
			       inx, cores_per_node, rem_cores);

			/* Clear this node from the initial available bitmap */
			bit_clear(avail_nodes_bitmap, inx);

			if (cr_node_num_cores[inx] < cores_per_node)
				continue;

			avail_cores_in_node = select_node_record[inx].tot_cores;
			if (exc_core_bitmap && exc_core_bitmap[inx]) {
				avail_cores_in_node -=
					bit_set_count(exc_core_bitmap[inx]);
			}
			debug2("Node inx:%d has %d available cores", inx,
			       avail_cores_in_node);
			if (avail_cores_in_node < cores_per_node)
				continue;

			avail_cores_in_node = 0;
			if (!picked_core_bitmap[inx]) {
				picked_core_bitmap[inx] =
					bit_alloc(cr_node_num_cores[inx]);
			}
			for (i = 0; i < cr_node_num_cores[inx]; i++) {
				if ((!exc_core_bitmap ||
				     !exc_core_bitmap[inx] ||
				     !bit_test(exc_core_bitmap[inx], i)) &&
				    !bit_test(picked_core_bitmap[inx], i)) {
					bit_set(picked_core_bitmap[inx], i);
					rem_cores--;
					avail_cores_in_node++;
				}
				if (rem_cores == 0)
					break;
				if (aggr_core_cnt &&
				    (avail_cores_in_node >= cores_per_node))
					break;
				if (!aggr_core_cnt &&
				    (avail_cores_in_node >= core_cnt[n]))
					break;
			}

			/* Add this node to the final node bitmap */
			if (avail_cores_in_node)
				bit_set(picked_node_bitmap, inx);
			n++;
		}
		FREE_NULL_BITMAP(avail_nodes_bitmap);
		free_core_array(&exc_core_bitmap);

		if (rem_cores) {
			info("reservation request can not be satisfied");
			FREE_NULL_BITMAP(picked_node_bitmap);
			picked_node_bitmap = NULL;
		} else {
			*core_bitmap =
				_array_to_core_bitmap(picked_core_bitmap);
		}
		free_core_array(&picked_core_bitmap);
		return picked_node_bitmap;
	}
	free_core_array(&exc_core_bitmap);

	return avail_nodes_bitmap;
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
	struct node_record *node_ptr;

	info("part:%s rows:%u prio:%u ", p_ptr->part_ptr->name, p_ptr->num_rows,
	     p_ptr->part_ptr->priority_tier);

	if (!p_ptr->row)
		return;

	for (r = 0; r < p_ptr->num_rows; r++) {
		char str[64]; /* print first 64 bits of bitmaps */
		char *sep = "", *tmp = NULL;
		int max_nodes_rep = 4;	/* max 4 allocated nodes to report */
		for (n = 0; n < select_node_cnt; n++) {
			if (!p_ptr->row[r].row_bitmap ||
			    !p_ptr->row[r].row_bitmap[n] ||
			    !bit_set_count(p_ptr->row[r].row_bitmap[n]))
				continue;
			node_ptr = node_record_table_ptr + n;
			bit_fmt(str, sizeof(str), p_ptr->row[r].row_bitmap[n]);
			xstrfmtcat(tmp, "%salloc_cores[%s]:%s",
				   sep, node_ptr->name, str);
			sep = ",";
			if (--max_nodes_rep == 0)
				break;
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

	a = xcalloc(p_ptr->num_rows, sizeof(uint32_t));
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
		for (j = i + 1; j < p_ptr->num_rows; j++) {
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
