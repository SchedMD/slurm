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
#include "src/common/xstring.h"

#include "src/slurmctld/gres_ctld.h"

#include "select_cons_tres.h"
#include "job_test.h"
#include "dist_tasks.h"

#define _DEBUG 0	/* Enables module specific debugging */
#define NODEINFO_MAGIC 0x8a5d

/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
extern slurm_conf_t slurm_conf __attribute__((weak_import));
extern node_record_t **node_record_table_ptr __attribute__((weak_import));
extern List part_list __attribute__((weak_import));
extern List job_list __attribute__((weak_import));
extern int node_record_count __attribute__((weak_import));
extern time_t last_node_update __attribute__((weak_import));
extern bitstr_t *avail_node_bitmap __attribute__((weak_import));
extern int slurmctld_tres_cnt __attribute__((weak_import));
extern slurmctld_config_t slurmctld_config __attribute__((weak_import));
extern bitstr_t *idle_node_bitmap __attribute__((weak_import));
extern list_t *cluster_license_list __attribute__((weak_import));
#else
slurm_conf_t slurm_conf;
node_record_t **node_record_table_ptr;
List part_list;
List job_list;
int node_record_count;
time_t last_node_update;
bitstr_t *avail_node_bitmap;
int slurmctld_tres_cnt = 0;
slurmctld_config_t slurmctld_config;
bitstr_t *idle_node_bitmap;
list_t *cluster_license_list;
#endif

/* init common global variables */
bool     backfill_busy_nodes  = false;
int      bf_window_scale      = 0;
bool     gang_mode            = false;
bool     preempt_by_part      = false;
bool     preempt_by_qos       = false;
bool     spec_cores_first     = false;

struct select_nodeinfo {
	uint16_t magic;		/* magic number */
	uint16_t alloc_cpus;
	uint64_t alloc_memory;
	uint64_t *tres_alloc_cnt;	/* array of tres counts allocated.
					   NOT PACKED */
	char     *tres_alloc_fmt_str;	/* formatted str of allocated tres */
	double    tres_alloc_weighted;	/* weighted number of tres allocated. */
};

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
const char plugin_type[] = "select/cons_tres";
const uint32_t plugin_id      = SELECT_PLUGIN_CONS_TRES;
const uint32_t plugin_version = SLURM_VERSION_NUMBER;
const uint32_t pstate_version = 7;	/* version control on saved state */
const uint16_t nodeinfo_magic = 0x8a5d;

/* Global variables */

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

/*
 * Check if there are no allocatable sockets with the given configuration due to
 * core specialization.
 * NOTE: this assumes the caller already checked CR_SOCKET is configured and
 *	 AllowSpecResourceUsage=NO.
 */
static void _check_allocatable_sockets(node_record_t *node_ptr)
{
	if (node_ptr->cpu_spec_list != NULL) {
		bool socket_without_spec_cores = false;
		bitstr_t *cpu_spec_bitmap = bit_alloc(node_ptr->cpus);
		int cpu_socket = node_ptr->cores * node_ptr->threads;

		bit_unfmt(cpu_spec_bitmap, node_ptr->cpu_spec_list);
		for (int i = 0; i < node_ptr->tot_sockets; i++) {
			if (!bit_set_count_range(cpu_spec_bitmap,
						 i * cpu_socket,
						 (i + 1) * cpu_socket)) {
				socket_without_spec_cores = true;
				break;
			}
		}
		FREE_NULL_BITMAP(cpu_spec_bitmap);
		if (!socket_without_spec_cores)
			fatal("NodeName=%s configuration doesn't allow to run jobs. SelectTypeParameteres=CR_Socket and CPUSpecList=%s uses cores from all sockets while AllowSpecResourcesUsage=NO, which makes the node non-usable. Please fix your slurm.conf",
			      node_ptr->name, node_ptr->cpu_spec_list);
	} else if (node_ptr->core_spec_cnt >
		   ((node_ptr->tot_sockets - 1) * node_ptr->cores))
		fatal("NodeName=%s configuration doesn't allow to run jobs. SelectTypeParameteres=CR_Socket and CoreSpecCount=%d uses cores from all sockets while AllowSpecResourcesUsage=NO, which makes the node non-usable. Please fix your slurm.conf",
		      node_ptr->name, node_ptr->core_spec_cnt);
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init(void)
{
	if (slurm_conf.preempt_mode & PREEMPT_MODE_GANG)
		gang_mode = true;
	else
		gang_mode = false;

	verbose("%s loaded", plugin_type);

	return SLURM_SUCCESS;
}

extern int fini(void)
{
	if (slurm_conf.debug_flags & DEBUG_FLAG_SELECT_TYPE)
		info("%s shutting down ...", plugin_type);
	else
		verbose("%s shutting down ...", plugin_type);

	node_data_destroy(select_node_usage);
	select_node_usage = NULL;
	part_data_destroy_res(select_part_record);
	select_part_record = NULL;
	cr_fini_global_core_data();

	return SLURM_SUCCESS;
}

extern int select_p_state_save(char *dir_name)
{
	/* nothing to save */
	return SLURM_SUCCESS;
}

/* This is Part 2 of a 4-part procedure which can be found in
 * src/slurmctld/read_config.c. See select_p_node_init for the
 * whole story.
 */
extern int select_p_state_restore(char *dir_name)
{
	/* nothing to restore */
	return SLURM_SUCCESS;
}

/* This is Part 3 of a 4-part procedure which can be found in
 * src/slurmctld/read_config.c. See select_p_node_init for the
 * whole story.
 */
extern int select_p_job_init(List job_list)
{
	/* nothing to initialize for jobs */
	return SLURM_SUCCESS;
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
extern int select_p_node_init(void)
{
	char *preempt_type, *tmp_ptr;
	int i;
	node_record_t *node_ptr;

	if (!slurm_conf.select_type_param) {
		info("%s SelectTypeParameters not specified, using default value: CR_Core_Memory",
		     plugin_type);
		slurm_conf.select_type_param = (CR_CORE | CR_MEMORY);
	}
	if (!(slurm_conf.select_type_param & (CR_CPU | CR_CORE | CR_SOCKET))) {
		fatal("Invalid SelectTypeParameters: %s (%u), "
		      "You need at least CR_(CPU|CORE|SOCKET)*",
		      select_type_param_string(slurm_conf.select_type_param),
		      slurm_conf.select_type_param);
	}

	preempt_for_licenses = false;
	if (xstrcasestr(slurm_conf.preempt_params, "reclaim_licenses"))
		preempt_for_licenses = true;

	preempt_strict_order = false;
	if (xstrcasestr(slurm_conf.preempt_params, "strict_order") ||
	    xstrcasestr(slurm_conf.sched_params, "preempt_strict_order"))
		preempt_strict_order = true;

	preempt_reorder_cnt = 1;
	if ((tmp_ptr = xstrcasestr(slurm_conf.preempt_params,
				   "reorder_count=")))
		preempt_reorder_cnt = atoi(tmp_ptr + 14);
	else if ((tmp_ptr = xstrcasestr(slurm_conf.sched_params,
					"preempt_reorder_count=")))
		preempt_reorder_cnt = atoi(tmp_ptr + 22);
	if (preempt_reorder_cnt < 0) {
		error("Invalid PreemptParameters reorder_count: %d",
		      preempt_reorder_cnt);
		preempt_reorder_cnt = 1;	/* Use default value */
	}

	if ((tmp_ptr = xstrcasestr(slurm_conf.sched_params,
				   "bf_window_linear="))) {
		bf_window_scale = atoi(tmp_ptr + 17);
		if (bf_window_scale <= 0) {
			error("Invalid SchedulerParameters bf_window_linear: %d",
			      bf_window_scale);
			bf_window_scale = 0;		/* Use default value */
		}
	} else
		bf_window_scale = 0;

	if (xstrcasestr(slurm_conf.sched_params, "spec_cores_first"))
		spec_cores_first = true;
	else
		spec_cores_first = false;
	if (xstrcasestr(slurm_conf.sched_params, "bf_busy_nodes"))
		backfill_busy_nodes = true;
	else
		backfill_busy_nodes = false;

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
	cr_init_global_core_data(node_record_table_ptr, node_record_count);

	node_data_destroy(select_node_usage);

	select_node_usage  = xcalloc(node_record_count,
				     sizeof(node_use_record_t));

	for (i = 0; (node_ptr = next_node(&i)); i++) {
		if ((slurm_conf.select_type_param & CR_SOCKET) &&
		    (slurm_conf.conf_flags & CTL_CONF_ASRU) == 0)
			_check_allocatable_sockets(node_ptr);

		select_node_usage[node_ptr->index].node_state =
			NODE_CR_AVAILABLE;
		gres_node_state_dealloc_all(node_ptr->gres_list);
	}

	part_data_create_array();
	node_data_dump();

	return SLURM_SUCCESS;
}

/*
 * select_p_job_test - Given a specification of scheduling requirements,
 *	identify the nodes which "best" satisfy the request.
 *	"best" is defined as either a minimal number of consecutive nodes
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
 * IN resv_exc_ptr - Various TRES which the job can NOT use.
 * RET zero on success, EINVAL otherwise
 */
extern int select_p_job_test(job_record_t *job_ptr, bitstr_t *node_bitmap,
			     uint32_t min_nodes, uint32_t max_nodes,
			     uint32_t req_nodes, uint16_t mode,
			     List preemptee_candidates,
			     List *preemptee_job_list,
			     resv_exc_t *resv_exc_ptr)
{
	int rc;

	xassert(node_bitmap);
	xassert(resv_exc_ptr);

	debug2("evaluating %pJ", job_ptr);
	if (!job_ptr->details)
		return EINVAL;

#if _DEBUG
	if (resv_exc_ptr->exc_cores) {
		int i;
		char tmp[128];
		for (i = 0; i < next_node(&i); i++) {
			if (!resv_exc_ptr->exc_cores[i])
				continue;
			bit_fmt(tmp, sizeof(tmp), resv_exc_ptr->exc_cores[i]);
			error("IN exc_cores[%d] %s", i, tmp);
		}
	}
#endif

	rc = job_test(job_ptr, node_bitmap, min_nodes, max_nodes,
		      req_nodes, mode, preemptee_candidates,
		      preemptee_job_list, resv_exc_ptr);
	return rc;
}

extern int select_p_job_begin(job_record_t *job_ptr)
{
	return SLURM_SUCCESS;
}

extern int select_p_job_ready(job_record_t *job_ptr)
{
	node_record_t *node_ptr;

	if (!IS_JOB_RUNNING(job_ptr) && !IS_JOB_SUSPENDED(job_ptr)) {
		/* Gang scheduling might suspend job immediately */
		return 0;
	}

	if (!job_ptr->node_bitmap)
		return READY_NODE_STATE;
	for (int i = 0; (node_ptr = next_node_bitmap(job_ptr->node_bitmap, &i));
	     i++) {
		if (IS_NODE_POWERED_DOWN(node_ptr) ||
		    IS_NODE_POWERING_UP(node_ptr))
			return 0;
	}

	return READY_NODE_STATE;
}

extern int select_p_job_expand(job_record_t *from_job_ptr,
			       job_record_t *to_job_ptr)
{
	job_resources_t *from_job_resrcs_ptr, *to_job_resrcs_ptr,
		*new_job_resrcs_ptr;
	int first_bit, last_bit, i, node_cnt;
	bool from_node_used, to_node_used;
	int from_node_offset, to_node_offset, new_node_offset;
	bitstr_t *tmp_bitmap, *tmp_bitmap2;

	xassert(from_job_ptr);
	xassert(from_job_ptr->details);
	xassert(to_job_ptr);
	xassert(to_job_ptr->details);

	if (from_job_ptr->job_id == to_job_ptr->job_id) {
		error("attempt to merge %pJ with self",
		      from_job_ptr);
		return SLURM_ERROR;
	}

	from_job_resrcs_ptr = from_job_ptr->job_resrcs;
	if ((from_job_resrcs_ptr == NULL) ||
	    (from_job_resrcs_ptr->cpus == NULL) ||
	    (from_job_resrcs_ptr->core_bitmap == NULL) ||
	    (from_job_resrcs_ptr->node_bitmap == NULL)) {
		error("%pJ lacks a job_resources struct",
		      from_job_ptr);
		return SLURM_ERROR;
	}
	to_job_resrcs_ptr = to_job_ptr->job_resrcs;
	if ((to_job_resrcs_ptr == NULL) ||
	    (to_job_resrcs_ptr->cpus == NULL) ||
	    (to_job_resrcs_ptr->core_bitmap == NULL) ||
	    (to_job_resrcs_ptr->node_bitmap == NULL)) {
		error("%pJ lacks a job_resources struct",
		      to_job_ptr);
		return SLURM_ERROR;
	}

	if (to_job_ptr->gres_list_req) {
		/* Can't reset gres/mps fields today */
		error("%pJ has allocated GRES", to_job_ptr);
		return SLURM_ERROR;
	}
	if (from_job_ptr->gres_list_req) {
		/* Can't reset gres/mps fields today */
		error("%pJ has allocated GRES", from_job_ptr);
		return SLURM_ERROR;
	}

	(void) job_res_rm_job(select_part_record, select_node_usage, NULL,
			      from_job_ptr, JOB_RES_ACTION_NORMAL, NULL);
	(void) job_res_rm_job(select_part_record, select_node_usage, NULL,
			      to_job_ptr, JOB_RES_ACTION_NORMAL, NULL);

	if (to_job_resrcs_ptr->core_bitmap_used)
		bit_clear_all(to_job_resrcs_ptr->core_bitmap_used);

	tmp_bitmap = bit_copy(to_job_resrcs_ptr->node_bitmap);
	bit_or(tmp_bitmap, from_job_resrcs_ptr->node_bitmap);
	tmp_bitmap2 = bit_copy(to_job_ptr->node_bitmap);
	bit_or(tmp_bitmap2, from_job_ptr->node_bitmap);
	bit_and(tmp_bitmap, tmp_bitmap2);
	FREE_NULL_BITMAP(tmp_bitmap2);
	node_cnt = bit_set_count(tmp_bitmap);

	new_job_resrcs_ptr = _create_job_resources(node_cnt);
	new_job_resrcs_ptr->ncpus = from_job_resrcs_ptr->ncpus +
		to_job_resrcs_ptr->ncpus;
	new_job_resrcs_ptr->node_req = to_job_resrcs_ptr->node_req;
	new_job_resrcs_ptr->node_bitmap = tmp_bitmap;
	new_job_resrcs_ptr->nodes = bitmap2node_name(new_job_resrcs_ptr->
						     node_bitmap);
	new_job_resrcs_ptr->whole_node = to_job_resrcs_ptr->whole_node;
	new_job_resrcs_ptr->threads_per_core =
		to_job_resrcs_ptr->threads_per_core;
	new_job_resrcs_ptr->cr_type = to_job_resrcs_ptr->cr_type;

	build_job_resources(new_job_resrcs_ptr);
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
			to_job_ptr->total_cpus +=
				node_record_table_ptr[i]->cpus_efctv;
		} else {
			to_job_ptr->total_cpus += new_job_resrcs_ptr->
				cpus[new_node_offset];
		}
	}
	build_job_resources_cpu_array(new_job_resrcs_ptr);
	gres_ctld_job_merge(from_job_ptr->gres_list_req,
			    from_job_resrcs_ptr->node_bitmap,
			    to_job_ptr->gres_list_req,
			    to_job_resrcs_ptr->node_bitmap);
	/* copy the allocated gres */
	gres_ctld_job_merge(from_job_ptr->gres_list_alloc,
			    from_job_resrcs_ptr->node_bitmap,
			    to_job_ptr->gres_list_alloc,
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
	bit_clear_all(from_job_ptr->node_bitmap);
	bit_clear_all(from_job_resrcs_ptr->node_bitmap);

	xfree(to_job_ptr->nodes);
	to_job_ptr->nodes = xstrdup(new_job_resrcs_ptr->nodes);
	xfree(from_job_ptr->nodes);
	from_job_ptr->nodes = xstrdup("");
	xfree(from_job_resrcs_ptr->nodes);
	from_job_resrcs_ptr->nodes = xstrdup("");

	(void) job_res_add_job(to_job_ptr, JOB_RES_ACTION_NORMAL);

	return SLURM_SUCCESS;
}

extern int select_p_job_resized(job_record_t *job_ptr, node_record_t *node_ptr)
{
	part_res_record_t *part_record_ptr = select_part_record;
	node_use_record_t *node_usage = select_node_usage;
	struct job_resources *job = job_ptr->job_resrcs;
	part_res_record_t *p_ptr;
	int i, n;
	List gres_list;
	bool old_job = false;

	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);

	if (!job || !job->core_bitmap) {
		error("%pJ has no job_resrcs info",
		      job_ptr);
		return SLURM_ERROR;
	}

	debug3("%pJ node %s",
	       job_ptr, node_ptr->name);
	if (job_ptr->start_time < slurmctld_config.boot_time)
		old_job = true;
	if (slurm_conf.debug_flags & DEBUG_FLAG_SELECT_TYPE)
		_dump_job_res(job);

	/* subtract memory */
	for (i = 0, n = 0; next_node_bitmap(job->node_bitmap, &i); i++) {
		if (i != node_ptr->index) {
			n++;
			continue;
		}

		if (job->cpus[n] == 0) {
			info("attempt to remove node %s from %pJ again",
			     node_ptr->name, job_ptr);
			return SLURM_SUCCESS;
		}

		if (node_usage[i].gres_list)
			gres_list = node_usage[i].gres_list;
		else
			gres_list = node_ptr->gres_list;
		gres_ctld_job_dealloc(job_ptr->gres_list_alloc, gres_list, n,
				      job_ptr->job_id, node_ptr->name,
				      old_job, true);
		gres_node_state_log(gres_list, node_ptr->name);

		if (node_usage[i].alloc_memory < job->memory_allocated[n]) {
			error("node %s memory is underallocated (%"PRIu64"-%"PRIu64") for %pJ",
			      node_ptr->name,
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
		error("removed %pJ does not have a partition assigned",
		      job_ptr);
		return SLURM_ERROR;
	}

	for (p_ptr = part_record_ptr; p_ptr; p_ptr = p_ptr->next) {
		if (p_ptr->part_ptr == job_ptr->part_ptr)
			break;
	}
	if (!p_ptr) {
		error("removed %pJ could not find part %s",
		      job_ptr, job_ptr->part_ptr->name);
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
			debug3("found %pJ in part %s row %u",
			       job_ptr,
			       p_ptr->part_ptr->name, i);
			/* found job - we're done, don't actually remove */
			n = 1;
			i = p_ptr->num_rows;
			break;
		}
	}
	if (n == 0) {
		error("could not find %pJ in partition %s",
		      job_ptr, p_ptr->part_ptr->name);
		return SLURM_ERROR;
	}


	/* some node of job removed from core-bitmap, so rebuild core bitmaps */
	part_data_build_row_bitmaps(p_ptr, NULL);

	/*
	 * Adjust the node_state of the node removed from this job.
	 * If all cores are now available, set node_state = NODE_CR_AVAILABLE
	 */
	if (node_usage[node_ptr->index].node_state >= job->node_req) {
		node_usage[node_ptr->index].node_state -= job->node_req;
	} else {
		error("node_state miscount");
		node_usage[node_ptr->index].node_state = NODE_CR_AVAILABLE;
	}

	return SLURM_SUCCESS;
}

extern int select_p_job_fini(job_record_t *job_ptr)
{
	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);

	log_flag(SELECT_TYPE, "%pJ", job_ptr);

	job_res_rm_job(select_part_record, select_node_usage, NULL,
		       job_ptr, JOB_RES_ACTION_NORMAL, NULL);

	return SLURM_SUCCESS;
}

/* NOTE: This function is not called with gang scheduling because it
 * needs to track how many jobs are running or suspended on each node.
 * This sum is compared with the partition's OverSubscribe parameter */
extern int select_p_job_suspend(job_record_t *job_ptr, bool indf_susp)
{
	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);

	if (indf_susp)
		log_flag(SELECT_TYPE, "%pJ indf_susp",
			 job_ptr);
	else
		log_flag(SELECT_TYPE, "%pJ",
			 job_ptr);

	if (!indf_susp)
		return SLURM_SUCCESS;

	return job_res_rm_job(select_part_record, select_node_usage, NULL,
			      job_ptr, JOB_RES_ACTION_RESUME, NULL);
}

/* See NOTE with select_p_job_suspend() above */
extern int select_p_job_resume(job_record_t *job_ptr, bool indf_susp)
{
	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);

	if (indf_susp)
		log_flag(SELECT_TYPE, "%pJ indf_susp",
			 job_ptr);
	else
		log_flag(SELECT_TYPE, "%pJ",
			 job_ptr);

	if (!indf_susp)
		return SLURM_SUCCESS;

	return job_res_add_job(job_ptr, JOB_RES_ACTION_RESUME);
}

extern bitstr_t *select_p_step_pick_nodes(job_record_t *job_ptr,
					  select_jobinfo_t *jobinfo,
					  uint32_t node_count,
					  bitstr_t **avail_nodes)
{
	return NULL;
}

/* Unused for this plugin */
extern int select_p_step_start(step_record_t *step_ptr)
{
	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern int select_p_step_finish(step_record_t *step_ptr, bool killing_step)
{
	return SLURM_SUCCESS;
}

extern int select_p_select_nodeinfo_pack(select_nodeinfo_t *nodeinfo,
					 buf_t *buffer,
					 uint16_t protocol_version)
{
	select_nodeinfo_t *nodeinfo_empty = NULL;

	if (!nodeinfo) {
		/*
		 * We should never get here,
		 * but avoid abort with bad data structures
		 */
		error("nodeinfo is NULL");
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

extern select_nodeinfo_t *select_p_select_nodeinfo_alloc(void)
{
	select_nodeinfo_t *nodeinfo = xmalloc(sizeof(select_nodeinfo_t));

	nodeinfo->magic = nodeinfo_magic;

	return nodeinfo;
}

extern int select_p_select_nodeinfo_free(select_nodeinfo_t *nodeinfo)
{
	if (nodeinfo) {
		if (nodeinfo->magic != nodeinfo_magic) {
			error("nodeinfo magic bad");
			return EINVAL;
		}
		xfree(nodeinfo->tres_alloc_cnt);
		xfree(nodeinfo->tres_alloc_fmt_str);
		xfree(nodeinfo);
	}
	return SLURM_SUCCESS;
}

extern int select_p_select_nodeinfo_unpack(select_nodeinfo_t **nodeinfo,
					   buf_t *buffer,
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
	error("error unpacking here");
	select_p_select_nodeinfo_free(nodeinfo_ptr);
	*nodeinfo = NULL;

	return SLURM_ERROR;
}

extern int select_p_select_nodeinfo_set_all(void)
{
	static time_t last_set_all = 0;
	part_res_record_t *p_ptr;
	node_record_t *node_ptr = NULL;
	int i, n;
	uint32_t alloc_cpus, alloc_cores, total_node_cores, efctv_node_cores;
	bitstr_t **alloc_core_bitmap = NULL;
	List gres_list;

	/*
	 * only set this once when the last_node_update is newer than
	 * the last time we set things up.
	 */
	if (last_set_all && (last_node_update < last_set_all)) {
		debug2("Node data hasn't changed since %ld",
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

	for (n = 0; (node_ptr = next_node(&n)); n++) {
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
			error("no nodeinfo returned from structure");
			continue;
		}

		if (alloc_core_bitmap && alloc_core_bitmap[n])
			alloc_cores = bit_set_count(alloc_core_bitmap[n]);
		else
			alloc_cores = 0;

		total_node_cores = node_ptr->tot_cores;
		efctv_node_cores = total_node_cores - node_ptr->core_spec_cnt;

		/*
		 * Avoid reporting more cores in use than configured.
		 *
		 * This could happen if an administrator resumes suspended jobs
		 * and thus oversubscribes cores.
		 *
		 * Or, if a job requests specialized CPUs (with --core-spec or
		 * --thread-spec), then --exclusive is implied, so all the CPUs
		 *  on the node are allocated (even if the job does not have
		 *  access to all of those CPUs). However, specialized CPUs are
		 *  not counted in configured CPUs, so we need to subtract
		 *  those from allocated CPUs.
		 */
		if (alloc_cores > efctv_node_cores)
			alloc_cpus = efctv_node_cores;
		else
			alloc_cpus = alloc_cores;

		/*
		 * The minimum allocatable unit may a core, so scale by thread
		 * count up to the proper CPU count as needed
		 */
		if (total_node_cores < node_ptr->cpus)
			alloc_cpus *= node_ptr->threads;
		nodeinfo->alloc_cpus = alloc_cpus;

		nodeinfo->alloc_memory = select_node_usage[n].alloc_memory;

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
		gres_ctld_set_node_tres_cnt(gres_list, nodeinfo->tres_alloc_cnt,
					    false);

		xfree(nodeinfo->tres_alloc_fmt_str);
		nodeinfo->tres_alloc_fmt_str =
			assoc_mgr_make_tres_str_from_array(
				nodeinfo->tres_alloc_cnt,
				TRES_STR_CONVERT_UNITS, false);
		nodeinfo->tres_alloc_weighted =
			assoc_mgr_tres_weighted(nodeinfo->tres_alloc_cnt,
						node_ptr->config_ptr->tres_weights,
						slurm_conf.priority_flags, false);
	}
	free_core_array(&alloc_core_bitmap);

	return SLURM_SUCCESS;
}

extern int select_p_select_nodeinfo_set(job_record_t *job_ptr)
{
	int rc;

	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);

	if (IS_JOB_RUNNING(job_ptr))
		rc = job_res_add_job(job_ptr, JOB_RES_ACTION_NORMAL);
	else if (IS_JOB_SUSPENDED(job_ptr)) {
		if (job_ptr->priority == 0)
			rc = job_res_add_job(job_ptr, JOB_RES_ACTION_SUSPEND);
		else	/* Gang schedule suspend */
			rc = job_res_add_job(job_ptr, JOB_RES_ACTION_NORMAL);
	} else
		return SLURM_SUCCESS;

	gres_job_state_log(job_ptr->gres_list_req, job_ptr->job_id);

	if ((slurm_conf.debug_flags & DEBUG_FLAG_GRES) &&
	    job_ptr->gres_list_alloc)
		info("Alloc GRES");
	gres_job_state_log(job_ptr->gres_list_alloc, job_ptr->job_id);

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
		error("nodeinfo not set");
		return SLURM_ERROR;
	}

	if (nodeinfo->magic != nodeinfo_magic) {
		error("jobinfo magic bad");
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
		error("Unsupported option %d", dinfo);
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
extern int select_p_select_jobinfo_pack(select_jobinfo_t *jobinfo,
					buf_t *buffer,
					uint16_t protocol_version)
{
	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern int select_p_select_jobinfo_unpack(select_jobinfo_t *jobinfo,
					  buf_t *buffer,
					  uint16_t protocol_version)
{
	return SLURM_SUCCESS;
}

extern int select_p_get_info_from_plugin(enum select_plugindata_info info,
					 job_record_t *job_ptr,
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
		error("info type %d invalid", info);
		rc = SLURM_ERROR;
		break;
	}
	return rc;
}

extern int select_p_reconfigure(void)
{
	ListIterator job_iterator;
	job_record_t *job_ptr;
	int rc = SLURM_SUCCESS;

	info("%s: reconfigure", plugin_type);

	def_cpu_per_gpu = 0;
	def_mem_per_gpu = 0;
	if (slurm_conf.job_defaults_list) {
		def_cpu_per_gpu = cons_helpers_get_def_cpu_per_gpu(
			slurm_conf.job_defaults_list);
		def_mem_per_gpu = cons_helpers_get_def_mem_per_gpu(
			slurm_conf.job_defaults_list);
	}

	rc = select_p_node_init();
	if (rc != SLURM_SUCCESS)
		return rc;

	/* reload job data */
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = list_next(job_iterator))) {
		if (IS_JOB_RUNNING(job_ptr)) {
			/* add the job */
			job_res_add_job(job_ptr, JOB_RES_ACTION_NORMAL);
		} else if (IS_JOB_SUSPENDED(job_ptr)) {
			/* add the job in a suspended state */
			if (job_ptr->priority == 0)
				(void) job_res_add_job(job_ptr,
						       JOB_RES_ACTION_SUSPEND);
			else	/* Gang schedule suspend */
				(void) job_res_add_job(job_ptr,
						       JOB_RES_ACTION_NORMAL);
		}
	}
	list_iterator_destroy(job_iterator);
	select_state_initializing = false;

	return SLURM_SUCCESS;
}
