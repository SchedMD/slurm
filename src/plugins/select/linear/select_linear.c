/*****************************************************************************\
 *  select_linear.c - node selection plugin for simple one-dimensional
 *  address space. Selects nodes for a job so as to minimize the number
 *  of sets of consecutive nodes using a best-fit algorithm.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010 SchedMD <https://www.schedmd.com>.
 *  Copyright (C) 2014 Silicon Graphics International Corp. All rights reserved.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#define _GNU_SOURCE

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/slurm_xlator.h"	/* Must be first */
#include "src/common/assoc_mgr.h"
#include "src/common/gres.h"
#include "src/common/job_resources.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/parse_time.h"
#include "src/common/select.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_topology.h"
#include "src/common/slurm_resource_info.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/gres_ctld.h"
#include "src/slurmctld/preempt.h"
#include "src/slurmctld/proc_req.h"
#include "src/plugins/select/linear/select_linear.h"

#define NO_SHARE_LIMIT	0xfffe
#define NODEINFO_MAGIC	0x82ad
#define RUN_JOB_INCR	16
#define SELECT_DEBUG	0

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
extern switch_record_t *switch_record_table __attribute__((weak_import));
extern int switch_record_cnt __attribute__((weak_import));
extern slurmctld_config_t slurmctld_config __attribute__((weak_import));

extern int hypercube_dimensions __attribute__((weak_import));
extern struct hypercube_switch *hypercube_switch_table __attribute__((weak_import));
extern int hypercube_switch_cnt __attribute__((weak_import));
extern struct hypercube_switch ***hypercube_switches __attribute__((weak_import));

#else
slurm_conf_t slurm_conf;
node_record_t **node_record_table_ptr;
List part_list;
List job_list;
int node_record_count;
time_t last_node_update;
switch_record_t *switch_record_table;
int switch_record_cnt;
slurmctld_config_t slurmctld_config;

int hypercube_dimensions;
struct hypercube_switch *hypercube_switch_table;
int hypercube_switch_cnt;
struct hypercube_switch ***hypercube_switches;
#endif

struct select_nodeinfo {
	uint16_t magic;		/* magic number */
	uint16_t alloc_cpus;
	uint64_t alloc_memory;
	char    *tres_alloc_fmt_str;	/* formatted str of allocated tres */
	double   tres_alloc_weighted;	/* weighted number of tres allocated. */
};

static int  _add_job_to_nodes(struct cr_record *cr_ptr, job_record_t *job_ptr,
			      char *pre_err, int suspended);
static void _add_run_job(struct cr_record *cr_ptr, uint32_t job_id);
static void _add_tot_job(struct cr_record *cr_ptr, uint32_t job_id);
static void _build_select_struct(job_record_t *job_ptr, bitstr_t *bitmap);
static int  _cr_job_list_sort(void *x, void *y);
static job_resources_t *_create_job_resources(int node_cnt);
static int _decr_node_job_cnt(int node_inx, job_record_t *job_ptr,
			      char *pre_err);
static void _dump_node_cr(struct cr_record *cr_ptr);
static struct cr_record *_dup_cr(struct cr_record *cr_ptr);
static int  _find_job_mate(job_record_t *job_ptr, bitstr_t *bitmap,
			   uint32_t min_nodes, uint32_t max_nodes,
			   uint32_t req_nodes);
static void _free_cr(struct cr_record *cr_ptr);
static int _get_avail_cpus(job_record_t *job_ptr, int index);
static uint16_t _get_total_cpus(int index);
static void _init_node_cr(void);
static int _job_count_bitmap(struct cr_record *cr_ptr,
			     job_record_t *job_ptr,
			     bitstr_t * bitmap, bitstr_t * jobmap,
			     int run_job_cnt, int tot_job_cnt, uint16_t mode);
static int _job_expand(job_record_t *from_job_ptr, job_record_t *to_job_ptr);
static int _job_test(job_record_t *job_ptr, bitstr_t *bitmap,
		     uint32_t min_nodes, uint32_t max_nodes,
		     uint32_t req_nodes);
static int _job_test_dfly(job_record_t *job_ptr, bitstr_t *bitmap,
			  uint32_t min_nodes, uint32_t max_nodes,
			  uint32_t req_nodes);
static int _job_test_hypercube(job_record_t *job_ptr, bitstr_t *bitmap,
			 uint32_t min_nodes, uint32_t max_nodes,
			 uint32_t req_nodes);
static int _job_test_topo(job_record_t *job_ptr, bitstr_t *bitmap,
			  uint32_t min_nodes, uint32_t max_nodes,
			  uint32_t req_nodes);
static bool _rem_run_job(struct cr_record *cr_ptr, uint32_t job_id);
static bool _rem_tot_job(struct cr_record *cr_ptr, uint32_t job_id);
static int _rm_job_from_nodes(struct cr_record *cr_ptr,
			      job_record_t *job_ptr, char *pre_err,
			      bool remove_all, bool job_fini);
static int _rm_job_from_one_node(job_record_t *job_ptr, node_record_t *node_ptr,
				 char *pre_err);
static int _run_now(job_record_t *job_ptr, bitstr_t *bitmap,
		    uint32_t min_nodes, uint32_t max_nodes,
		    int max_share, uint32_t req_nodes,
		    List preemptee_candidates,
		    List *preemptee_job_list);
static int _sort_usable_nodes_dec(void *, void *);
static bool _test_run_job(struct cr_record *cr_ptr, uint32_t job_id);
static bool _test_tot_job(struct cr_record *cr_ptr, uint32_t job_id);
static int _test_only(job_record_t *job_ptr, bitstr_t *bitmap,
		      uint32_t min_nodes, uint32_t max_nodes,
		      uint32_t req_nodes, int max_share);
static int _will_run_test(job_record_t *job_ptr, bitstr_t *bitmap,
			  uint32_t min_nodes, uint32_t max_nodes,
			  int max_share, uint32_t req_nodes,
			  List preemptee_candidates,
			  List *preemptee_job_list);

extern select_nodeinfo_t *select_p_select_nodeinfo_alloc();
extern int select_p_select_nodeinfo_free(select_nodeinfo_t *nodeinfo);

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
const char plugin_name[]       	= "Linear node selection plugin";
const char plugin_type[]       	= "select/linear";
const uint32_t plugin_id	= SELECT_PLUGIN_LINEAR;
const uint32_t plugin_version	= SLURM_VERSION_NUMBER;

static uint16_t cr_type;
static bool have_dragonfly = false;
static bool topo_optional = false;

/* Record of resources consumed on each node including job details */
static struct cr_record *cr_ptr = NULL;
static pthread_mutex_t cr_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Add job id to record of jobs running on this node */
static void _add_run_job(struct cr_record *cr_ptr, uint32_t job_id)
{
	int i;

	if (cr_ptr->run_job_ids == NULL) {	/* create new array */
		cr_ptr->run_job_len = RUN_JOB_INCR;
		cr_ptr->run_job_ids = xcalloc(cr_ptr->run_job_len,
					      sizeof(uint32_t));
		cr_ptr->run_job_ids[0] = job_id;
		return;
	}

	for (i=0; i<cr_ptr->run_job_len; i++) {
		if (cr_ptr->run_job_ids[i])
			continue;
		/* fill in hole */
		cr_ptr->run_job_ids[i] = job_id;
		return;
	}

	/* expand array and add to end */
	cr_ptr->run_job_len += RUN_JOB_INCR;
	xrealloc(cr_ptr->run_job_ids, sizeof(uint32_t) * cr_ptr->run_job_len);
	cr_ptr->run_job_ids[i] = job_id;
}

/* Add job id to record of jobs running or suspended on this node */
static void _add_tot_job(struct cr_record *cr_ptr, uint32_t job_id)
{
	int i;

	if (cr_ptr->tot_job_ids == NULL) {	/* create new array */
		cr_ptr->tot_job_len = RUN_JOB_INCR;
		cr_ptr->tot_job_ids = xcalloc(cr_ptr->tot_job_len,
					      sizeof(uint32_t));
		cr_ptr->tot_job_ids[0] = job_id;
		return;
	}

	for (i=0; i<cr_ptr->tot_job_len; i++) {
		if (cr_ptr->tot_job_ids[i])
			continue;
		/* fill in hole */
		cr_ptr->tot_job_ids[i] = job_id;
		return;
	}

	/* expand array and add to end */
	cr_ptr->tot_job_len += RUN_JOB_INCR;
	xrealloc(cr_ptr->tot_job_ids, sizeof(uint32_t) * cr_ptr->tot_job_len);
	cr_ptr->tot_job_ids[i] = job_id;
}

static bool _ck_run_job(struct cr_record *cr_ptr, uint32_t job_id,
			bool clear_it)
{
	int i;
	bool rc = false;

	if ((cr_ptr->run_job_ids == NULL) || (cr_ptr->run_job_len == 0))
		return rc;

	for (i=0; i<cr_ptr->run_job_len; i++) {
		if (cr_ptr->run_job_ids[i] != job_id)
			continue;
		if (clear_it)
			cr_ptr->run_job_ids[i] = 0;
		rc = true;
	}
	return rc;
}

/* Remove job id from record of jobs running,
 * RET true if successful, false if the job was not running */
static bool _rem_run_job(struct cr_record *cr_ptr, uint32_t job_id)
{
	return _ck_run_job(cr_ptr, job_id, true);
}

/* Test for job id in record of jobs running,
 * RET true if successful, false if the job was not running */
static bool _test_run_job(struct cr_record *cr_ptr, uint32_t job_id)
{
	return _ck_run_job(cr_ptr, job_id, false);
}

static bool _ck_tot_job(struct cr_record *cr_ptr, uint32_t job_id,
			bool clear_it)
{
	int i;
	bool rc = false;

	if ((cr_ptr->tot_job_ids == NULL) || (cr_ptr->tot_job_len == 0))
		return rc;

	for (i=0; i<cr_ptr->tot_job_len; i++) {
		if (cr_ptr->tot_job_ids[i] != job_id)
			continue;
		if (clear_it)
			cr_ptr->tot_job_ids[i] = 0;
		rc = true;
	}
	return rc;
}
/* Remove job id from record of jobs running or suspended,
 * RET true if successful, false if the job was not found */
static bool _rem_tot_job(struct cr_record *cr_ptr, uint32_t job_id)
{
	return _ck_tot_job(cr_ptr, job_id, true);
}

/* Test for job id in record of jobs running or suspended,
 * RET true if successful, false if the job was not found */
static bool _test_tot_job(struct cr_record *cr_ptr, uint32_t job_id)
{
	return _ck_tot_job(cr_ptr, job_id, false);
}

static bool _enough_nodes(int avail_nodes, int rem_nodes,
			  uint32_t min_nodes, uint32_t req_nodes)
{
	int needed_nodes;

	if (req_nodes > min_nodes)
		needed_nodes = rem_nodes + min_nodes - req_nodes;
	else
		needed_nodes = rem_nodes;

	return(avail_nodes >= needed_nodes);
}

/*
 * _get_avail_cpus - Get the number of "available" cpus on a node
 *	given this number given the number of cpus_per_task and
 *	maximum sockets, cores, threads.  Note that the value of
 *	cpus is the lowest-level logical processor (LLLP).
 * IN job_ptr - pointer to job being scheduled
 * IN index - index of node's configuration information in node_record_table_ptr
 */
static int _get_avail_cpus(job_record_t *job_ptr, int index)
{
	node_record_t *node_ptr;
	int avail_cpus;
	uint16_t cpus_per_task = 1;
	uint16_t ntasks_per_node = 0, ntasks_per_core;
	multi_core_data_t *mc_ptr = NULL;

	if (job_ptr->details == NULL)
		return (uint16_t) 0;

	if (job_ptr->details->cpus_per_task)
		cpus_per_task = job_ptr->details->cpus_per_task;
	if (job_ptr->details->ntasks_per_node)
		ntasks_per_node = job_ptr->details->ntasks_per_node;
	if ((mc_ptr = job_ptr->details->mc_ptr))
		ntasks_per_core   = mc_ptr->ntasks_per_core;
	else
		ntasks_per_core   = 0;

	node_ptr = node_record_table_ptr[index];

#if SELECT_DEBUG
	info("host:%s HW_ cpus_per_node:%u boards_per_node:%u "
	     "sockets_per_boards:%u cores_per_socket:%u thread_per_core:%u ",
	     node_ptr->name, node_ptr->cpus, node_ptr->boards,
	     node_ptr->tot_sockets / node_ptr->boards, node_ptr->cores,
	     node_ptr->threads);
#endif

	avail_cpus = adjust_cpus_nppcu(ntasks_per_core, cpus_per_task,
				       node_ptr->tot_cores, node_ptr->cpus);
	if (ntasks_per_node > 0)
		avail_cpus = MIN(avail_cpus, ntasks_per_node * cpus_per_task);
#if SELECT_DEBUG
	debug("avail_cpus index %d = %u (out of boards_per_node:%u "
	      "sockets_per_boards:%u cores_per_socket:%u thread_per_core:%u)",
	      index, avail_cpus, node_ptr->boards,
	      node_ptr->tot_sockets / node_ptr->boards, node_ptr->cores,
	      node_ptr->threads);
#endif
	return(avail_cpus);
}

/*
 * _get_total_cpus - Get the total number of cpus on a node
 *	Note that the value of cpus is the lowest-level logical
 *	processor (LLLP).
 * IN index - index of node's configuration information in node_record_table_ptr
 */
static uint16_t _get_total_cpus(int index)
{
	node_record_t *node_ptr = node_record_table_ptr[index];
	return node_ptr->config_ptr->cpus;
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

/* Build the full job_resources_t *structure for a job based upon the nodes
 *	allocated to it (the bitmap) and the job's memory requirement */
static void _build_select_struct(job_record_t *job_ptr, bitstr_t *bitmap)
{
	int i, j, k;
	int first_bit, last_bit;
	uint32_t node_cpus, total_cpus = 0, node_cnt;
	uint64_t job_memory_cpu = 0, job_memory_node = 0, min_mem = 0;
	job_resources_t *job_resrcs_ptr;

	if (job_ptr->details->pn_min_memory  && (cr_type & CR_MEMORY)) {
		if (job_ptr->details->pn_min_memory & MEM_PER_CPU)
			job_memory_cpu = job_ptr->details->pn_min_memory &
				(~MEM_PER_CPU);
		else
			job_memory_node = job_ptr->details->pn_min_memory;
	}

	if (job_ptr->job_resrcs)	/* Old struct due to job requeue */
		free_job_resources(&job_ptr->job_resrcs);

	node_cnt = bit_set_count(bitmap);
	job_ptr->job_resrcs = job_resrcs_ptr = _create_job_resources(node_cnt);
	job_resrcs_ptr->node_bitmap = bit_copy(bitmap);
	job_resrcs_ptr->nodes = bitmap2node_name(bitmap);
	job_resrcs_ptr->ncpus = job_ptr->total_cpus;
	if (build_job_resources(job_resrcs_ptr))
		error("_build_select_struct: build_job_resources: %m");

	first_bit = bit_ffs(bitmap);
	last_bit  = bit_fls(bitmap);
	if (last_bit == -1)
		last_bit = -2;	/* no bits set */
	for (i = first_bit, j = 0, k = -1; i <= last_bit; i++) {
		if (!bit_test(bitmap, i))
			continue;
		node_cpus = _get_total_cpus(i);
		job_resrcs_ptr->cpus[j] = node_cpus;
		if ((k == -1) ||
		    (job_resrcs_ptr->cpu_array_value[k] != node_cpus)) {
			job_resrcs_ptr->cpu_array_cnt++;
			job_resrcs_ptr->cpu_array_reps[++k] = 1;
			job_resrcs_ptr->cpu_array_value[k] = node_cpus;
		} else
			job_resrcs_ptr->cpu_array_reps[k]++;
		total_cpus += node_cpus;

		if (job_memory_node) {
			job_resrcs_ptr->memory_allocated[j] = job_memory_node;
		} else if (job_memory_cpu) {
			job_resrcs_ptr->memory_allocated[j] =
				job_memory_cpu * node_cpus;
		} else if (cr_type & CR_MEMORY) {
			job_resrcs_ptr->memory_allocated[j] =
				node_record_table_ptr[i]->config_ptr->
				real_memory;
			if (!min_mem ||
			    (min_mem > job_resrcs_ptr->memory_allocated[j])) {
				min_mem = job_resrcs_ptr->memory_allocated[j];
			}
		}

		if (set_job_resources_node(job_resrcs_ptr, j)) {
			error("_build_select_struct: set_job_resources_node: "
			      "%m");
		}
		j++;
	}
	if (cr_type & CR_MEMORY && !job_ptr->details->pn_min_memory)
		job_ptr->details->pn_min_memory = min_mem;

	if (job_resrcs_ptr->ncpus != total_cpus) {
		error("_build_select_struct: ncpus mismatch %u != %u",
		      job_resrcs_ptr->ncpus, total_cpus);
	}
}

/*
 * Set the bits in 'jobmap' that correspond to bits in the 'bitmap'
 * that are running 'run_job_cnt' jobs or less, and clear the rest.
 */
static int _job_count_bitmap(struct cr_record *cr_ptr,
			     job_record_t *job_ptr,
			     bitstr_t * bitmap, bitstr_t * jobmap,
			     int run_job_cnt, int tot_job_cnt, uint16_t mode)
{
	int i, i_first, i_last;
	int count = 0, total_jobs, total_run_jobs;
	struct part_cr_record *part_cr_ptr;
	node_record_t *node_ptr;
	uint64_t job_memory_cpu = 0, job_memory_node = 0;
	uint64_t alloc_mem = 0, job_mem = 0, avail_mem = 0;
	uint32_t cpu_cnt, gres_cpus, gres_cores;
	int core_start_bit, core_end_bit, cpus_per_core;
	List gres_list;
	bool use_total_gres = true;

	xassert(cr_ptr);
	xassert(cr_ptr->nodes);
	if (mode != SELECT_MODE_TEST_ONLY) {
		use_total_gres = false;
		if (job_ptr->details->pn_min_memory  &&
		    (cr_type & CR_MEMORY)) {
			if (job_ptr->details->pn_min_memory & MEM_PER_CPU) {
				job_memory_cpu = job_ptr->details->pn_min_memory
					& (~MEM_PER_CPU);
			} else {
				job_memory_node = job_ptr->details->
					pn_min_memory;
			}
		}
	}

	i_first = bit_ffs(bitmap);
	i_last  = bit_fls(bitmap);
	if (i_first == -1)	/* job has no nodes */
		i_last = -2;
	for (i = i_first; i <= i_last; i++) {
		if (!bit_test(bitmap, i)) {
			bit_clear(jobmap, i);
			continue;
		}

		node_ptr = node_record_table_ptr[i];
		cpu_cnt = node_ptr->config_ptr->cpus;

		if (cr_ptr->nodes[i].gres_list)
			gres_list = cr_ptr->nodes[i].gres_list;
		else
			gres_list = node_ptr->gres_list;
		core_start_bit = cr_get_coremap_offset(i);
		core_end_bit   = cr_get_coremap_offset(i+1) - 1;
		cpus_per_core  = cpu_cnt / (core_end_bit - core_start_bit + 1);
		gres_cores = gres_job_test(job_ptr->gres_list_req,
						  gres_list, use_total_gres,
						  NULL, core_start_bit,
						  core_end_bit, job_ptr->job_id,
						  node_ptr->name, false);
		gres_cpus = gres_cores;
		if (gres_cpus != NO_VAL) {
			gres_cpus *= cpus_per_core;
			if ((gres_cpus < cpu_cnt) ||
			    (gres_cpus < job_ptr->details->ntasks_per_node) ||
			    ((job_ptr->details->cpus_per_task > 1) &&
			     (gres_cpus < job_ptr->details->cpus_per_task))) {
				bit_clear(jobmap, i);
				continue;
			}
		}

		if (mode == SELECT_MODE_TEST_ONLY) {
			bit_set(jobmap, i);
			count++;
			continue;	/* No need to test other resources */
		}

		if (!job_memory_cpu && !job_memory_node &&
		    (cr_type & CR_MEMORY))
			job_memory_node = node_ptr->config_ptr->real_memory;

		if (job_memory_cpu || job_memory_node) {
			alloc_mem = cr_ptr->nodes[i].alloc_memory;
			avail_mem = node_ptr->config_ptr->real_memory;
			if (job_memory_cpu)
				job_mem = job_memory_cpu * cpu_cnt;
			else
				job_mem = job_memory_node;
			avail_mem -= node_ptr->mem_spec_limit;
			if ((alloc_mem + job_mem) > avail_mem) {
				bit_clear(jobmap, i);
				continue;
			}
		}

		if ((mode != SELECT_MODE_TEST_ONLY) &&
		    (cr_ptr->nodes[i].exclusive_cnt != 0)) {
			/* already reserved by some exclusive job */
			bit_clear(jobmap, i);
			continue;
		}

		total_jobs = 0;
		total_run_jobs = 0;
		part_cr_ptr = cr_ptr->nodes[i].parts;
		while (part_cr_ptr) {
			total_run_jobs += part_cr_ptr->run_job_cnt;
			total_jobs     += part_cr_ptr->tot_job_cnt;
			part_cr_ptr = part_cr_ptr->next;
		}
		if ((total_run_jobs <= run_job_cnt) &&
		    (total_jobs     <= tot_job_cnt)) {
			bit_set(jobmap, i);
			count++;
		} else {
			bit_clear(jobmap, i);
		}

	}
	return count;
}

/* _find_job_mate - does most of the real work for select_p_job_test(),
 *	in trying to find a suitable job to mate this one with. This is
 *	a pretty simple algorithm now, but could try to match the job
 *	with multiple jobs that add up to the proper size or a single
 *	job plus a few idle nodes. */
static int _find_job_mate(job_record_t *job_ptr, bitstr_t *bitmap,
			  uint32_t min_nodes, uint32_t max_nodes,
			  uint32_t req_nodes)
{
	ListIterator job_iterator;
	job_record_t *job_scan_ptr;
	int rc = EINVAL;

	job_iterator = list_iterator_create(job_list);
	while ((job_scan_ptr = list_next(job_iterator))) {
		if ((!IS_JOB_RUNNING(job_scan_ptr))			||
		    (job_scan_ptr->node_cnt   != req_nodes)		||
		    (job_scan_ptr->total_cpus <
		     job_ptr->details->min_cpus)			||
		    (!bit_super_set(job_scan_ptr->node_bitmap, bitmap)))
			continue;
		if (job_scan_ptr->details && job_ptr->details &&
		    (job_scan_ptr->details->contiguous !=
		     job_ptr->details->contiguous))
			continue;

		if (job_ptr->details->req_node_bitmap &&
		    (!bit_super_set(job_ptr->details->req_node_bitmap,
				    job_scan_ptr->node_bitmap)))
			continue;	/* Required nodes missing from job */

		if (job_ptr->details->exc_node_bitmap &&
		    bit_overlap_any(job_ptr->details->exc_node_bitmap,
				    job_scan_ptr->node_bitmap))
			continue;	/* Excluded nodes in this job */

		bit_and(bitmap, job_scan_ptr->node_bitmap);
		job_ptr->total_cpus = job_scan_ptr->total_cpus;
		rc = SLURM_SUCCESS;
		break;
	}
	list_iterator_destroy(job_iterator);
	return rc;
}

/* _job_test - does most of the real work for select_p_job_test(), which
 *	pretty much just handles load-leveling and max_share logic */
static int _job_test(job_record_t *job_ptr, bitstr_t *bitmap,
		     uint32_t min_nodes, uint32_t max_nodes,
		     uint32_t req_nodes)
{
	int i, error_code = EINVAL, sufficient;
	int *consec_nodes;	/* how many nodes we can add from this
				 * consecutive set of nodes */
	int *consec_cpus;	/* how many nodes we can add from this
				 * consecutive set of nodes */
	int *consec_start;	/* where this consecutive set starts (index) */
	int *consec_end;	/* where this consecutive set ends (index) */
	int *consec_req;	/* are nodes from this set required
				 * (in req_bitmap) */
	int consec_index, consec_size;
	int rem_cpus, rem_nodes;	/* remaining resources desired */
	int best_fit_nodes, best_fit_cpus, best_fit_req;
	int best_fit_location = 0, best_fit_sufficient;
	int avail_cpus, total_cpus = 0;
	int *avail_cpu_cnt;	/* Available CPU count on each node */
	int first_cpu_cnt = 0, total_node_cnt = 0, low_cpu_cnt = 99999;
	bool heterogeneous = false; /* Nodes have heterogeneous CPU counts */

	if (bit_set_count(bitmap) < min_nodes)
		return error_code;

	if ((job_ptr->details->req_node_bitmap) &&
	    (!bit_super_set(job_ptr->details->req_node_bitmap, bitmap)))
		return error_code;

	if (hypercube_switch_table && hypercube_switch_cnt) {
		/* Optimized resource selection based on hypercube topology */
		return _job_test_hypercube(job_ptr, bitmap,
					   min_nodes, max_nodes, req_nodes);
	}

	if (switch_record_cnt && switch_record_table &&
	    ((topo_optional == false) || job_ptr->req_switch)) {
		/* Perform optimized resource selection based upon topology */
		if (have_dragonfly) {
			return _job_test_dfly(job_ptr, bitmap,
					      min_nodes, max_nodes, req_nodes);
		} else {
			return _job_test_topo(job_ptr, bitmap,
					      min_nodes, max_nodes, req_nodes);
		}
	}

	consec_index = 0;
	consec_size  = 50;	/* start allocation for 50 sets of
				 * consecutive nodes */
	consec_cpus  = xcalloc(consec_size, sizeof(int));
	consec_nodes = xcalloc(consec_size, sizeof(int));
	consec_start = xcalloc(consec_size, sizeof(int));
	consec_end   = xcalloc(consec_size, sizeof(int));
	consec_req   = xcalloc(consec_size, sizeof(int));


	/* Build table with information about sets of consecutive nodes */
	consec_cpus[consec_index] = consec_nodes[consec_index] = 0;
	consec_req[consec_index] = -1;	/* no required nodes here by default */
	rem_cpus = job_ptr->details->min_cpus;
	if (req_nodes > min_nodes)
		rem_nodes = req_nodes;
	else
		rem_nodes = min_nodes;

	avail_cpu_cnt = xcalloc(node_record_count, sizeof(int));
	for (i = 0; i < node_record_count; i++) {
		if (bit_test(bitmap, i)) {
			avail_cpu_cnt[i] = _get_avail_cpus(job_ptr, i);
			if (++total_node_cnt == 1)
				first_cpu_cnt = avail_cpu_cnt[i];
			else if (first_cpu_cnt != avail_cpu_cnt[i])
				heterogeneous = true;
			low_cpu_cnt = MIN(low_cpu_cnt, avail_cpu_cnt[i]);

			if (consec_nodes[consec_index] == 0)
				consec_start[consec_index] = i;
			avail_cpus = avail_cpu_cnt[i];
			if (job_ptr->details->req_node_bitmap	&&
			    (max_nodes > 0)			&&
			    bit_test(job_ptr->details->req_node_bitmap, i)) {
				if (consec_req[consec_index] == -1) {
					/* first required node in set */
					consec_req[consec_index] = i;
				}
				rem_nodes--;
				max_nodes--;
				rem_cpus   -= avail_cpus;
				total_cpus += _get_total_cpus(i);
			} else {	 /* node not required (yet) */
				bit_clear(bitmap, i);
				consec_cpus[consec_index] += avail_cpus;
				consec_nodes[consec_index]++;
			}
		} else if (consec_nodes[consec_index] == 0) {
			consec_req[consec_index] = -1;
			/* already picked up any required nodes */
			/* re-use this record */
		} else {
			consec_end[consec_index] = i - 1;
			if (++consec_index >= consec_size) {
				consec_size *= 2;
				xrealloc(consec_cpus,
					 sizeof(int) * consec_size);
				xrealloc(consec_nodes,
					 sizeof(int) * consec_size);
				xrealloc(consec_start,
					 sizeof(int) * consec_size);
				xrealloc(consec_end,
					 sizeof(int) * consec_size);
				xrealloc(consec_req,
					 sizeof(int) * consec_size);
			}
			consec_cpus[consec_index] = 0;
			consec_nodes[consec_index] = 0;
			consec_req[consec_index] = -1;
		}
	}
	if (consec_nodes[consec_index] != 0)
		consec_end[consec_index++] = i - 1;

#if SELECT_DEBUG
	/* don't compile this, it slows things down too much */
	debug3("rem_cpus=%d, rem_nodes=%d", rem_cpus, rem_nodes);
	for (i = 0; i < consec_index; i++) {
		if (consec_req[i] != -1)
			debug3("start=%s, end=%s, nodes=%d, cpus=%d, req=%s",
			       node_record_table_ptr[consec_start[i]]->name,
			       node_record_table_ptr[consec_end[i]]->name,
			       consec_nodes[i], consec_cpus[i],
			       node_record_table_ptr[consec_req[i]]->name);
		else
			debug3("start=%s, end=%s, nodes=%d, cpus=%d",
			       node_record_table_ptr[consec_start[i]]->name,
			       node_record_table_ptr[consec_end[i]]->name,
			       consec_nodes[i], consec_cpus[i]);
	}
#endif

	if (heterogeneous && (rem_cpus > (low_cpu_cnt * rem_nodes))) {
		while ((max_nodes > 0) &&
		       ((rem_nodes > 0) || (rem_cpus > 0))) {
			int high_cpu_cnt = 0, high_cpu_inx = -1;
			for (i = 0; i < node_record_count; i++) {
				if (high_cpu_cnt > avail_cpu_cnt[i])
					continue;
				if (bit_test(bitmap, i))
					continue;
				high_cpu_cnt = avail_cpu_cnt[i];
				high_cpu_inx = i;
			}
			if (high_cpu_inx == -1)
				break;	/* No more available CPUs */
			bit_set(bitmap, high_cpu_inx);
			rem_nodes--;
			max_nodes--;
			avail_cpus = avail_cpu_cnt[high_cpu_inx];
			rem_cpus   -= avail_cpus;
			total_cpus +=_get_total_cpus(high_cpu_inx);
			avail_cpu_cnt[high_cpu_inx] = 0;
		}
	} else {
		heterogeneous = false;
	}

	/* accumulate nodes from these sets of consecutive nodes until */
	/*   sufficient resources have been accumulated */
	while (consec_index && (max_nodes > 0) && !heterogeneous) {
		best_fit_cpus = best_fit_nodes = best_fit_sufficient = 0;
		best_fit_req = -1;	/* first required node, -1 if none */
		for (i = 0; i < consec_index; i++) {
			if (consec_nodes[i] == 0)
				continue;	/* no usable nodes here */

			if (job_ptr->details->contiguous &&
			    job_ptr->details->req_node_bitmap &&
			    (consec_req[i] == -1))
				continue;	/* no required nodes here */

			sufficient = (consec_cpus[i] >= rem_cpus) &&
				_enough_nodes(consec_nodes[i], rem_nodes,
					      min_nodes, req_nodes);

			/* if first possibility OR */
			/* contains required nodes OR */
			/* first set large enough for request OR */
			/* tightest fit (less resource waste) OR */
			/* nothing yet large enough, but this is biggest */
			if ((best_fit_nodes == 0) ||
			    ((best_fit_req == -1) && (consec_req[i] != -1))  ||
			    (sufficient && (best_fit_sufficient == 0))       ||
			    (sufficient && (consec_cpus[i] < best_fit_cpus)) ||
			    ((sufficient == 0) &&
			     (consec_cpus[i] > best_fit_cpus))) {
				best_fit_cpus = consec_cpus[i];
				best_fit_nodes = consec_nodes[i];
				best_fit_location = i;
				best_fit_req = consec_req[i];
				best_fit_sufficient = sufficient;
			}

			if (job_ptr->details->contiguous &&
			    job_ptr->details->req_node_bitmap) {
				/* Must wait for all required nodes to be
				 * in a single consecutive block */
				int j, other_blocks = 0;
				for (j = (i+1); j < consec_index; j++) {
					if (consec_req[j] != -1) {
						other_blocks = 1;
						break;
					}
				}
				if (other_blocks) {
					best_fit_nodes = 0;
					break;
				}
			}
		}
		if (best_fit_nodes == 0)
			break;
		if (job_ptr->details->contiguous &&
		     ((best_fit_cpus < rem_cpus) ||
		     (!_enough_nodes(best_fit_nodes, rem_nodes,
				     min_nodes, req_nodes))))
			break;	/* no hole large enough */
		if (best_fit_req != -1) {
			/* This collection of nodes includes required ones
			 * select nodes from this set, first working up
			 * then down from the required nodes */
			for (i = best_fit_req;
			     i <= consec_end[best_fit_location]; i++) {
				if ((max_nodes <= 0) ||
				    ((rem_nodes <= 0) && (rem_cpus <= 0)))
					break;
				if (bit_test(bitmap, i))
					continue;
				bit_set(bitmap, i);
				rem_nodes--;
				max_nodes--;
				avail_cpus = avail_cpu_cnt[i];
				rem_cpus   -= avail_cpus;
				total_cpus += _get_total_cpus(i);
			}
			for (i = (best_fit_req - 1);
			     i >= consec_start[best_fit_location]; i--) {
				if ((max_nodes <= 0) ||
				    ((rem_nodes <= 0) && (rem_cpus <= 0)))
					break;
				if (bit_test(bitmap, i))
					continue;
				bit_set(bitmap, i);
				rem_nodes--;
				max_nodes--;
				avail_cpus = avail_cpu_cnt[i];
				rem_cpus   -= avail_cpus;
				total_cpus += _get_total_cpus(i);
			}
		} else {
			for (i = consec_start[best_fit_location];
			     i <= consec_end[best_fit_location]; i++) {
				if ((max_nodes <= 0) ||
				    ((rem_nodes <= 0) && (rem_cpus <= 0)))
					break;
				if (bit_test(bitmap, i))
					continue;
				bit_set(bitmap, i);
				rem_nodes--;
				max_nodes--;
				avail_cpus = avail_cpu_cnt[i];
				rem_cpus   -= avail_cpus;
				total_cpus += _get_total_cpus(i);
			}
		}

		if (job_ptr->details->contiguous ||
		    ((rem_nodes <= 0) && (rem_cpus <= 0))) {
			error_code = SLURM_SUCCESS;
			break;
		}
		consec_cpus[best_fit_location] = 0;
		consec_nodes[best_fit_location] = 0;
	}

	if (error_code && (rem_cpus <= 0) &&
	    _enough_nodes(0, rem_nodes, min_nodes, req_nodes)) {
		error_code = SLURM_SUCCESS;
	}
	if (error_code == SLURM_SUCCESS) {
		/* job's total_cpus is needed for SELECT_MODE_WILL_RUN */
		job_ptr->total_cpus = total_cpus;
	}

	xfree(avail_cpu_cnt);
	xfree(consec_cpus);
	xfree(consec_nodes);
	xfree(consec_start);
	xfree(consec_end);
	xfree(consec_req);
	return error_code;
}

/*
 * Compute the variance on the passed sufficient cluster and see if it's the
 * best we've seen so far
 */
static void
_hypercube_update_variance(
	int dim, int dir, int start_index, int end_index,
	int node_count, int max_nodes,
	int leftover_nodes, int64_t summed_squares,
	int64_t squared_sums, int * min_curve, int * min_direction,
	int * min_start_index, int * min_neighbors,
	int * min_extra_nodes, int64_t * min_variance)
{
//XXX use actual node count?
	int64_t variance = summed_squares -
		squared_sums * squared_sums / node_count;

	/* Don't calculate if we've used too many nodes */
	if (0 > max_nodes)
		return;

	if ((variance < *min_variance) ||
	    ((variance == *min_variance) &&
	     (leftover_nodes <= *min_extra_nodes))) {
		int begin = start_index - dir;
		int end = end_index + dir;
		int neighbors = 0;

		if (0 > begin) {
			begin = hypercube_switch_cnt - 1;
		} else if (hypercube_switch_cnt >= begin) {
			begin = 0;
		}

		if (0 > end) {
			end = hypercube_switch_cnt - 1;
		} else if (hypercube_switch_cnt >= end) {
			end = 0;
		}

		if (begin != end_index) {
			neighbors += hypercube_switches[dim][begin]->avail_cnt;
		}
		if (end != start_index && begin != end) {
			neighbors += hypercube_switches[dim][end]->avail_cnt;
		}

		/*
		 * Update if variance is lowest found or if variance
		 * == lowest variance found and there are less extra
		 * nodes on the switch or if variances and extra nodes
		 * are the same but there are less neighboring nodes
		 */
		if ((variance < *min_variance) ||
		    ((variance == *min_variance) &&
		     (leftover_nodes < *min_extra_nodes)) ||
		    ((variance == *min_variance) &&
		     (leftover_nodes == *min_extra_nodes) &&
		     (neighbors < *min_neighbors))) {
			*min_variance = variance;
			*min_start_index = start_index;
			*min_extra_nodes = leftover_nodes;
			*min_neighbors = neighbors;
			*min_direction = dir;
			*min_curve = dim;
		}
	}
}

/*
 * We're passed a cluster (start_index, end_index) and asked to add another
 * switch with its nodes if we don't already have enough. As an experiment we
 * try adding switches to the left, but we otherwise add to the right.
 */
static void _hypercube_add_nodes(job_record_t *job_ptr, bitstr_t *avail_bitmap,
				 int dim, int32_t start_index,
				 int32_t *end_index, int node_count,
				 int32_t max_nodes, int32_t rem_nodes,
				 int32_t rem_cpus, int leftover_nodes,
				 bitstr_t *bitmap, int64_t *distance_offset,
				 int64_t summed_squares, int64_t squared_sums,
				 int *min_curve, int *min_direction,
				 int *min_start_index, int32_t *min_neighbors,
				 int32_t *min_extra_nodes,
				 int64_t *min_variance)
{
	bitstr_t * tmp_bitmap;
	int32_t l_start_index = *end_index;
	int32_t l_end_index = start_index;
	int32_t l_temp_max_nodes = max_nodes;
	int32_t l_temp_rem_nodes = rem_nodes;
	int32_t l_temp_rem_cpus = rem_cpus;
	int64_t l_summed_squares = summed_squares;
	int64_t l_squared_sums = squared_sums;
	int32_t l_leftover_nodes = 0;
	int32_t l_distance_offset = 0;
	int32_t l_distance;

	/* Don't need to add any more nodes */
	if (leftover_nodes ||
	    (0 >= rem_nodes && 0 >= rem_cpus)) {
		return;
	}

	tmp_bitmap = bit_copy(bitmap);

	/*
	 * Create a temporary right-sided cluster and try sliding the left edge
	 * further left until we have enough nodes.
	 */
	while (0 <= l_temp_max_nodes &&
		(0 < l_temp_rem_nodes || 0 < l_temp_rem_cpus)) {
		int cnt, n, new_nodes = 0;

		l_end_index--;
		if (l_end_index < 0) { /* Handle wrap-around */
			l_end_index = hypercube_switch_cnt - 1;
			l_distance_offset = -1 *
				hypercube_switches[dim]
				[hypercube_switch_cnt - 1]->distance[dim];
		}

		/* Add nodes from the switch until we've hit our limits */
		cnt = hypercube_switches[dim][l_end_index]->node_cnt;
		for (n = 0; n < cnt; n++) {
			int node = hypercube_switches[dim]
				[l_end_index]->node_index[n];

			if (!bit_test(avail_bitmap, node) ||
			    bit_test(tmp_bitmap, node)) {
				continue;
			}

			/* The node is unused and available, add it */
			new_nodes++;
			bit_set(tmp_bitmap, node);
			l_temp_max_nodes--;
			l_temp_rem_nodes--;
			l_temp_rem_cpus -= _get_avail_cpus(job_ptr, node);

			/* Have we hit our limits? */
			if ((0 > l_temp_max_nodes) ||
			    (0 >= l_temp_rem_nodes && 0 >= l_temp_rem_cpus)) {
				break;
			}
		}

		/* Factor in the distance for the new nodes */
		l_distance = l_distance_offset +
			hypercube_switches[dim][l_end_index]->distance[dim];
		l_summed_squares += new_nodes * l_distance * l_distance;
		l_squared_sums += new_nodes * l_distance;
		l_leftover_nodes = hypercube_switches[dim][l_end_index]->avail_cnt -
			new_nodes;
	}

	bit_free(tmp_bitmap);

	/* Let's see how good this right-sided cluster is */
	_hypercube_update_variance(
		dim, -1, l_start_index, l_end_index, node_count, l_temp_max_nodes,
		l_leftover_nodes, l_summed_squares, l_squared_sums,
		min_curve, min_direction, min_start_index, min_neighbors,
		min_extra_nodes, min_variance);

	/*
	 * OK, we're back to working on the left-sided cluster. Move the right
	 * side further right to pick up a new switch and add more of the needed
	 * nodes.
	 */
	(*end_index)++;
	if (*end_index == hypercube_switch_cnt) { /* Handle wrap-around */
		*end_index = 0;
		*distance_offset =
			hypercube_switches[dim][hypercube_switch_cnt - 1]->
			distance[dim];
	}
}

/* Variance based best-fit cluster algorithm:
 * 	 Loop through all of the Hilbert Curves that were created.
 * Each Hilbert Curve is essentially a particular ordering of all the
 * switches in the network. For each Hilbert Curve, the algorithm loops
 * through all clusters of neighboring nodes, but only tests clusters that
 * either have their leftmost switch completed saturated (all available
 * nodes for the switch are in the cluster) or their rightmost switch
 * completed saturated, also called left-saturated clusters and
 * right-saturated clusters. The algorithm starts at the left (top) of
 * the table and works its way to the right (down).
 * 	  The algorithm starts by adding nodes from the switch at the top of
 * the table to the cluster. If after the nodes are added, the cluster
 * still needs more nodes, the algorithm continues down (right) the table
 * adding the number of nodes the next switch has available to the
 * cluster. It continues adding nodes until it has enough for the cluster.
 * If the cluster only needs 4 more nodes and the next rightmost switch
 * has 8 nodes available, the cluster only adds the 4 needed nodes to the
 * cluster: called adding a partial. When the algorithm moves to the next
 * cluster, it will pick up where it left off and will add the remaining 4
 * nodes on the switch before moving to the next switch in the table.
 * 	  Once the algorithm has added enough nodes to the cluster, it
 * computes the variance for the cluster of nodes. If this cluster is
 * the best-fit cluster found so far, it saves the cluster's information.
 * To move on to testing the next cluster, first it removes all the nodes
 * from the leftmost switch. Then the algorithm repeats the process of
 * adding nodes to the cluster from rightmost switch until it has enough.
 * 		If the rightside of the cluster reaches the bottom of the table,
 * then it loops back around to the top most switch in the table and
 * continues. The algorithm continues until the leftmost switch of the
 * cluster has reached the end of the table. At which point it knows
 * that it has tested all possible left-saturated clusters.
 * 	  Although this algorithm could be run in reverse order on the table
 * in order to test all right-saturated clusters, it would result in
 * redundant calculations. Instead, all right-saturated clusters are
 * tested during the node adding process of the left-saturated clusters
 * algorithm. While running the left-saturated clusters algorithm
 * described above, anytime nodes are added from the rightmost switch
 * resulting in all the available nodes from that switch being in the
 * cluster and the cluster still needs more nodes, then create a temporary
 * cluster equal to the current cluster.
 * 	  Use this temporary cluster to test the right-saturated cluster
 * starting at the rightmost switch in the cluster and moving left (up the
 * table). Since the temporary cluster needs more nodes, add nodes by
 * moving up/left in the table (rather than right, like is done for the
 * left-saturated clusters). Once the cluster has enough nodes, calculate
 * its variance and remember it if it is the best fit cluster found so far.
 * Then erase the temporary cluster and continue with the original cluster
 * where the algorithm left off. By doing this right-saturated clusters
 * calcution everytime the rightmost switch of a cluster is fully added,
 * the algorithm tests every possible right-saturated cluster.
 *
 * 	  Equation used to calculate the variance of a cluster:
 * Variance = sum(x^2) - sum(x)^2/num(x), where sum(x) is the sum of all
 * values of x, and num(x) is the number of x values summed
 *
 * *** Important Note: There isn't actually a 'cluster' struct, but rather
 * a cluster is described by its necesary characteristics including:
 * start_index, end_index, summed_squares, squared_sums, and rem_nodes ***
 */
static void _explore_hypercube(job_record_t *job_ptr, bitstr_t *avail_bitmap,
			       const int64_t *req_summed_squares,
			       const int64_t *req_squared_sums,
			       const int max_nodes, const int rem_nodes,
			       const int rem_cpus, const int node_count,
			       int *min_start_index, int *min_direction,
			       int *min_curve)
{
	bitstr_t * tmp_bitmap = bit_alloc(bit_size(avail_bitmap));
	int64_t min_variance = INT64_MAX;
	int32_t min_extra_nodes = INT32_MAX;
	int32_t min_neighbors = INT32_MAX;
	int dim;

	/* Check each dimension for the best cluster of nodes */
	for (dim = 0; dim < hypercube_dimensions; dim++) {
		int64_t summed_squares = req_summed_squares[dim];
		int64_t squared_sums = req_squared_sums[dim];
		int64_t distance, distance_offset = 0;
		int32_t start_index = 0, end_index;
		int32_t temp_rem_nodes = rem_nodes;
		int32_t temp_rem_cpus = rem_cpus;
		int32_t temp_max_nodes = max_nodes;

		/* If this curve wasn't set up then skip it */
		if (hypercube_switch_table[0].distance[dim] == 0) {continue;}

		/* Move to first switch with available nodes */
		while (hypercube_switches[dim][start_index]->avail_cnt == 0) {
			start_index++;
		}
		end_index = start_index;
		bit_clear_all(tmp_bitmap);

		/* Test every switch to see if it's the best starting point */
		while ((start_index < hypercube_switch_cnt) &&
		       (start_index >= 0)) {
			int leftover_nodes = 0;

			/*
			 * Add new nodes to cluster. If next switch has more nodes
			 * then needed, only add nodes needed. This is called
			 * adding a partial.
			 */
			while ((0 <= temp_max_nodes) &&
				(0 < temp_rem_nodes || 0 < temp_rem_cpus)) {
				int cnt = hypercube_switches[dim][end_index]->
					node_cnt;
				int fn = hypercube_switches[dim][end_index]->
					node_index[0];
				int ln = hypercube_switches[dim][end_index]->
					node_index[cnt - 1];
				int new_nodes = 0;
				int n;

				/* Add free nodes from the switch */
				for (n = 0; n < cnt; n++) {
					int node = hypercube_switches[dim]
						[end_index]->node_index[n];

					if (!bit_test(avail_bitmap, node) ||
					    bit_test(tmp_bitmap, node)) {
						continue;
					}

					/* Unused and available, add it */
					new_nodes++;
					bit_set(tmp_bitmap, node);
					temp_max_nodes--;
					temp_rem_nodes--;
					temp_rem_cpus -= _get_avail_cpus(
						job_ptr, node);

					/* Do we have enough resources? */
					if ((0 > temp_max_nodes) ||
					    (0 >= temp_rem_nodes &&
					     0 >= temp_rem_cpus)) {
						break;
					}
				}

				/*
				 * Calculate the inputs to the variance for the
				 * current cluster
				 */
				distance = hypercube_switches[dim]
					[end_index]->distance[dim] + distance_offset;
				summed_squares += new_nodes * distance * distance;
				squared_sums += new_nodes * distance;
				leftover_nodes = hypercube_switches[dim][end_index]->
					avail_cnt -
					bit_set_count_range(tmp_bitmap, fn, ln + 1);

				/* Add nodes from an additional switch */
				_hypercube_add_nodes(
					job_ptr, avail_bitmap,
					dim, start_index, &end_index, node_count,
					temp_max_nodes, temp_rem_nodes, temp_rem_cpus,
					leftover_nodes, tmp_bitmap,
					&distance_offset, summed_squares,
					squared_sums, min_curve, min_direction,
					min_start_index, &min_neighbors,
					&min_extra_nodes, &min_variance);
			}

			/* Check to see if this is the lowest variance so far */
			_hypercube_update_variance(
				dim, 1, start_index, end_index, node_count,
				temp_max_nodes,
				leftover_nodes, summed_squares, squared_sums,
				min_curve, min_direction, min_start_index,
				&min_neighbors, &min_extra_nodes, &min_variance);

			/*
			 * We're updating our indices to slide right and have a
			 * new leftmost switch. Remove the nodes from the current
			 * leftmost switch.
			 */
			while ((temp_rem_nodes <= 0) &&
				(start_index < hypercube_switch_cnt) &&
				(start_index >= 0)) {
				int cnt = hypercube_switches[dim][start_index]->
					node_cnt;
				int used = MIN(
					rem_nodes,
					hypercube_switches[dim][start_index]->
					avail_cnt);
				int n;

				if (hypercube_switches[dim][start_index]->
				    avail_cnt == 0) {
					if (start_index == end_index)
						end_index++;
					start_index++;
					continue;
				}

				distance = hypercube_switches[dim][start_index]->
					distance[dim];
				summed_squares -= distance * distance * used;
				squared_sums -= distance * used;

				/* Free the nodes we added on this switch */
				for (n = 0; n < cnt; n++) {
					int node = hypercube_switches[dim]
						[start_index]->node_index[n];

					if (!bit_test(tmp_bitmap, node))
						continue;

					bit_clear(tmp_bitmap, node);
					temp_max_nodes++;
					temp_rem_nodes++;
					temp_rem_cpus += _get_avail_cpus(job_ptr,
									 node);
				}

				if (start_index == end_index)
					end_index++;
				start_index++;
			}

			/*
			 * If the cluster had holes with switches
			 * completely allocated to other jobs, keep sliding
			 * right until we find a switch with free nodes
			 */
			while ((start_index < hypercube_switch_cnt) &&
				(hypercube_switches[dim][start_index]->
				 avail_cnt == 0)) {
				if (start_index == end_index)
					end_index++;
				start_index++;
			}
		}
	}

	bit_free(tmp_bitmap);
}

/* a hypercube topology version of _job_test -
 * does most of the real work for select_p_job_test(), which
 *	pretty much just handles load-leveling and max_share logic */
static int _job_test_hypercube(job_record_t *job_ptr, bitstr_t *bitmap,
			       uint32_t min_nodes, uint32_t max_nodes,
			       uint32_t req_nodes)
{
	int i, rc = EINVAL;
	int32_t rem_cpus, rem_nodes, node_count = 0, total_cpus = 0;
	int32_t alloc_nodes = 0;
	int64_t *req_summed_squares = xcalloc(hypercube_dimensions,
					      sizeof(int64_t));
	int64_t *req_squared_sums = xcalloc(hypercube_dimensions,
					    sizeof(int64_t));
	bitstr_t *req_nodes_bitmap = NULL;
	bitstr_t *avail_bitmap = NULL;
	int32_t cur_node_index = -1, node_counter = 0, switch_index;
	int32_t min_start_index = -1, min_direction = 1234, min_curve = 4321;

	for (i = 0; i < hypercube_dimensions; i++) {
		req_summed_squares[i] = 0;
		req_squared_sums[i] = 0;
	}

	rem_cpus = job_ptr->details->min_cpus;
	node_count = rem_nodes = MAX(req_nodes, min_nodes);

	/* Give up now if there aren't enough hosts */
	if (bit_set_count(bitmap) < rem_nodes)
		goto fini;

	/* Grab all of the required nodes if there are any */
	if (job_ptr->details->req_node_bitmap) {
		req_nodes_bitmap = bit_copy(job_ptr->details->req_node_bitmap);

		// set avail_bitmap to all available nodes except the required nodes
		// set bitmap to just the required nodes
		avail_bitmap = bit_copy(req_nodes_bitmap);
		bit_not(avail_bitmap);
		bit_and(avail_bitmap, bitmap );
		bit_copybits(bitmap, req_nodes_bitmap);

		i = bit_set_count(req_nodes_bitmap);
		if (i > (int)max_nodes) {
			info("%pJ requires more nodes than currently available (%d>%u)",
			     job_ptr, i, max_nodes);
			FREE_NULL_BITMAP(req_nodes_bitmap);
			FREE_NULL_BITMAP(avail_bitmap);
			xfree(req_squared_sums);
			xfree(req_summed_squares);
			return EINVAL;
		}
		rem_nodes -= i;
		alloc_nodes += i;
	} else { // if there are no required nodes, update bitmaps accordingly
		avail_bitmap = bit_copy(bitmap);
		bit_nclear(bitmap, 0, node_record_count - 1);
	}

	/* Calculate node availability for each switch */
	for (i = 0; i < hypercube_switch_cnt; i++) {
		const int node_idx = hypercube_switch_table[i].node_index[0];
		const int cnt = hypercube_switch_table[i].node_cnt;

		/* Add all the nodes on this switch */
		hypercube_switch_table[i].avail_cnt = bit_set_count_range(
			avail_bitmap, node_idx, node_idx + cnt);

		/* If the switch has nodes that are required, loop through them */
		if (req_nodes_bitmap && (hypercube_switch_table[i].avail_cnt != 0) &&
		    (bit_set_count_range(
			     req_nodes_bitmap, node_idx, node_idx + cnt) > 0)) {
			int j;

			/* Check each node on the switch */
			for (j = 0; j < cnt; j++) {
				int idx = hypercube_switch_table[i].node_index[j];

				/* If is req'd, add cpus and distance to calulations */
				if (bit_test(req_nodes_bitmap, idx)) {
					int k;

					rem_cpus   -= _get_avail_cpus(job_ptr, idx);
					total_cpus += _get_total_cpus(idx);

					/*
					 * Add the required nodes data to the
					 * variance calculations
					 */
					for (k = 0; k < hypercube_dimensions; k++) {
						int distance = hypercube_switch_table[i].
							distance[k];

						req_summed_squares[k] += distance *
							distance;
						req_squared_sums[k] += distance;
					}
				}
			}
		}
	}

	// check to see if no more nodes need to be added to the job
	if ((alloc_nodes >= max_nodes) ||
	    ((rem_nodes <= 0) && (rem_cpus <= 0))) {
		goto fini;
	}

	/* Find the best starting switch and traversal path to get nodes from */
	if (alloc_nodes < max_nodes)
		i = max_nodes - alloc_nodes;
	else
		i = 0;
	_explore_hypercube(job_ptr, avail_bitmap, req_summed_squares,
			   req_squared_sums, i, rem_nodes, rem_cpus,
			   node_count, &min_start_index, &min_direction,
			   &min_curve);
	if (-1 == min_start_index)
		goto fini;

	/*
	 * Assigns nodes from the best cluster to the job. Starts at the start
	 * index switch and keeps adding available nodes until it has as many
	 * as it needs
	 */
	switch_index = min_start_index;
	node_counter = 0;
	while ((alloc_nodes < max_nodes) &&
	       ((rem_nodes > 0) || (rem_cpus > 0))) {
		int node_index;

		/* If we used up all the nodes in a switch, move to the next */
		if (node_counter ==
		    hypercube_switches[min_curve][switch_index]->avail_cnt) {
			node_counter = 0;
			cur_node_index = -1;
			do {
				// min_direction == 1 moves up the table
				// min_direction == -1 moves down the table
				switch_index += min_direction;
				if (switch_index == hypercube_switch_cnt) {
					switch_index = 0;
				} else if (switch_index == -1) {
					switch_index = hypercube_switch_cnt - 1;
				} else if (switch_index == min_start_index) {
					goto fini;
				}
			} while (hypercube_switches[min_curve][switch_index]->
				  avail_cnt == 0);
		}

		/* Find the next usable node in the switch */
		do {
			cur_node_index++;
			node_index = hypercube_switches[min_curve][switch_index]->
				node_index[cur_node_index];
		} while (false == bit_test(avail_bitmap, node_index));

		/* Allocate the CPUs from the node */
		bit_set(bitmap, node_index);
		rem_cpus   -= _get_avail_cpus(job_ptr, node_index);
		total_cpus += _get_total_cpus(node_index);

		rem_nodes--;
		alloc_nodes++;
		node_counter++;
	}
fini:
	/* If we allocated sufficient CPUs and nodes, we were successful */
	if ((rem_cpus <= 0) && (bit_set_count(bitmap) >= min_nodes)) {
		rc = SLURM_SUCCESS;
		/* Job's total_cpus is needed for SELECT_MODE_WILL_RUN */
		job_ptr->total_cpus = total_cpus;
	} else {
		rc = EINVAL;
		if (alloc_nodes > max_nodes) {
			info("%pJ requires more nodes than allowed",
			     job_ptr);
		}
	}

	xfree(req_squared_sums);
	xfree(req_summed_squares);
	FREE_NULL_BITMAP(req_nodes_bitmap);
	FREE_NULL_BITMAP(avail_bitmap);

	return rc;
}


/*
 * _job_test_dfly - A dragonfly topology aware version of _job_test()
 * NOTE: The logic here is almost identical to that of _eval_nodes_dfly() in
 *       select/cons_res/job_test.c. Any bug found here is probably also there.
 */
static int _job_test_dfly(job_record_t *job_ptr, bitstr_t *bitmap,
			  uint32_t min_nodes, uint32_t max_nodes,
			  uint32_t req_nodes)
{
	bitstr_t **switches_bitmap;		/* nodes on this switch */
	int       *switches_cpu_cnt;		/* total CPUs on switch */
	uint32_t  *switches_node_cnt;		/* total nodes on switch */
	uint32_t  *switches_node_use;		/* nodes from switch used */

	bitstr_t  *req_nodes_bitmap   = NULL;
	int rem_cpus;			/* remaining resources desired */
	int avail_cpus, total_cpus = 0;
	uint32_t want_nodes, alloc_nodes = 0;
	int i, j, rc = SLURM_SUCCESS;
	int best_fit_inx, first, last;
	int best_fit_nodes, best_fit_cpus;
	int best_fit_location = 0;
	bool sufficient;
	long time_waiting = 0;
	int leaf_switch_count = 0;	/* Count of leaf node switches used */

	if (job_ptr->req_switch > 1) {
		/* Maximum leaf switch count >1 probably makes no sense */
		info("%s: Resetting %pJ leaf switch count from %u to 0",
		     __func__, job_ptr, job_ptr->req_switch);
		job_ptr->req_switch = 0;
	}
	if (job_ptr->req_switch) {
		time_t     time_now;
		time_now = time(NULL);
		if (job_ptr->wait4switch_start == 0)
			job_ptr->wait4switch_start = time_now;
		time_waiting = time_now - job_ptr->wait4switch_start;
	}

	rem_cpus = job_ptr->details->min_cpus;
	if (req_nodes > min_nodes)
		want_nodes = req_nodes;
	else
		want_nodes = min_nodes;

	/* Construct a set of switch array entries,
	 * use the same indexes as switch_record_table in slurmctld */
	switches_bitmap   = xcalloc(switch_record_cnt, sizeof(bitstr_t *));
	switches_cpu_cnt  = xcalloc(switch_record_cnt, sizeof(int));
	switches_node_cnt = xcalloc(switch_record_cnt, sizeof(uint32_t));
	switches_node_use = xcalloc(switch_record_cnt, sizeof(int));
	if (job_ptr->details->req_node_bitmap) {
		req_nodes_bitmap = bit_copy(job_ptr->details->req_node_bitmap);
		i = bit_set_count(req_nodes_bitmap);
		if (i > (int)max_nodes) {
			info("%pJ requires more nodes than currently available (%u>%u)",
			     job_ptr, i, max_nodes);
			rc = EINVAL;
			goto fini;
		}
	}

	/* phase 1: make availability bitmaps for switches */
	sufficient = false;
	for (i = 0; i < switch_record_cnt; i++) {
		switches_bitmap[i] = bit_copy(switch_record_table[i].
					      node_bitmap);
		bit_and(switches_bitmap[i], bitmap);
		if (req_nodes_bitmap &&
		    !bit_super_set(req_nodes_bitmap, switches_bitmap[i]))
			switches_node_cnt[i] = 0;
		else {
			switches_node_cnt[i] = bit_set_count(switches_bitmap[i]);
			sufficient = true;
		}
	}
	bit_nclear(bitmap, 0, node_record_count - 1);

#if SELECT_DEBUG
	/* Don't compile this, it slows things down too much */
	for (i = 0; i < switch_record_cnt; i++) {
		char *node_names = NULL;
		if (switches_node_cnt[i])
			node_names = bitmap2node_name(switches_bitmap[i]);
		debug("switch=%s nodes=%u:%s speed=%u",
		      switch_record_table[i].name,
		      switches_node_cnt[i], node_names,
		      switch_record_table[i].link_speed);
		xfree(node_names);
	}
#endif

	/* check if requested nodes are available */
	if (!sufficient) {
		info("%pJ requires nodes not available on any switch",
		     job_ptr);
		rc = EINVAL;
		goto fini;
	}

	/* phase 2: calculate CPU resources for each switch */
	for (i = 0; i < node_record_count; i++) {
		avail_cpus = _get_avail_cpus(job_ptr, i);
		for (j = 0; j < switch_record_cnt; j++) {
			if (bit_test(switches_bitmap[j], i)) {
				switches_cpu_cnt[j] += avail_cpus;
			}
		}
	}

	/* phase 3 */
	/* Determine lowest level switch satisfying request with best fit */
	best_fit_inx = -1;
	for (j = 0; j < switch_record_cnt; j++) {
#if SELECT_DEBUG
		debug5("checking switch %d: nodes %u cpus %d", j,
		       switches_node_cnt[j], switches_cpu_cnt[j]);
#endif
		if ((switches_cpu_cnt[j]  < rem_cpus) ||
		    (switches_node_cnt[j] < min_nodes))
			continue;
		if ((best_fit_inx == -1) ||
		    (switch_record_table[j].level <
		     switch_record_table[best_fit_inx].level) ||
		    ((switch_record_table[j].level ==
		      switch_record_table[best_fit_inx].level) &&
		     (switches_node_cnt[j] < switches_node_cnt[best_fit_inx])))
			best_fit_inx = j;
	}
	if (best_fit_inx == -1) {
		debug("%s: could not find resources for %pJ",
		      __func__, job_ptr);
		rc = EINVAL;
		goto fini;
	}

	/* phase 4: select resources from already allocated leaves */
	/* Identify usable leafs (within higher switch having best fit) */
	for (j = 0; j < switch_record_cnt; j++) {
		if ((switch_record_table[j].level > 0) ||
		    (!bit_super_set(switches_bitmap[j],
				    switches_bitmap[best_fit_inx]))) {
			switches_node_cnt[j] = 0;
		} else if (req_nodes_bitmap) {
			/* we have subnodes count zeroed yet so count them */
			switches_node_cnt[j] = bit_set_count(switches_bitmap[j]);
		}
	}
	/* set already allocated nodes and gather additional resources */
	if (req_nodes_bitmap) {
		/* Accumulate specific required resources, if any */
		for (j = 0; j < switch_record_cnt; j++) {
			if (alloc_nodes > max_nodes)
				break;
			if (switches_node_cnt[j] == 0 ||
			    bit_overlap_any(req_nodes_bitmap,
					    switches_bitmap[j]) == 0)
				continue;

			/* Use nodes from this leaf */
			first = bit_ffs(switches_bitmap[j]);
			if (first < 0) {
				switches_node_cnt[j] = 0;
				continue;
			}
			last  = bit_fls(switches_bitmap[j]);
			for (i = first; i <= last; i++) {
				if (!bit_test(switches_bitmap[j], i))
					continue;
				if (!bit_test(req_nodes_bitmap, i)) {
					/* node wasn't requested */
					continue;
				}

				bit_clear(switches_bitmap[j], i);
				switches_node_cnt[j]--;
				avail_cpus = _get_avail_cpus(job_ptr, i);
				switches_cpu_cnt[j] -= avail_cpus;

				if (bit_test(bitmap, i)) {
					/* node on multiple leaf switches
					 * and already selected */
					continue;
				}

				switches_node_use[j]++;
				bit_set(bitmap, i);
				alloc_nodes++;
				rem_cpus -= avail_cpus;
				total_cpus += _get_total_cpus(i);
			}
		}
	}

	/* phase 5 */
	/* Select resources from leafs on a best-fit or round-robin basis */
	while ((alloc_nodes <= max_nodes) &&
	       ((alloc_nodes < want_nodes) || (rem_cpus > 0))) {
		best_fit_cpus = best_fit_nodes = 0;
		for (j = 0; j < switch_record_cnt; j++) {
			if (switches_node_cnt[j] == 0)
				continue;
			/* If multiple leaf switches must be used, prefer use
			 * of leaf switches with fewest number of idle CPUs.
			 * This results in more leaf switches being used and
			 * achieves better network bandwidth. */
			if ((best_fit_nodes == 0) ||
			    (switches_node_use[best_fit_location] >
			     switches_node_use[j]) ||
			    ((switches_node_use[best_fit_location] ==
			      switches_node_use[j]) &&
			     (switches_cpu_cnt[j] < best_fit_cpus))) {
				best_fit_cpus =  switches_cpu_cnt[j];
				best_fit_nodes = switches_node_cnt[j];
				best_fit_location = j;
			}
		}
#if SELECT_DEBUG
		info("%s: found switch %d for allocation: nodes %d cpus %d "
		       "allocated %u", __func__, best_fit_location,
		       best_fit_nodes, best_fit_cpus, alloc_nodes);
#endif
		if (best_fit_nodes == 0)
			break;

		/* Use select nodes from this leaf */
		first = bit_ffs(switches_bitmap[best_fit_location]);
		if (first < 0) {
			switches_node_cnt[best_fit_location] = 0;
			continue;
		}
		last  = bit_fls(switches_bitmap[best_fit_location]);
		for (i = first; i <= last; i++) {
			if (!bit_test(switches_bitmap[best_fit_location], i))
				continue;

			if (bit_test(bitmap, i)) {
				/* node on multiple leaf switches
				 * and already selected */
				continue;
			}

			bit_clear(switches_bitmap[best_fit_location], i);
			switches_node_cnt[best_fit_location]--;
			switches_node_use[best_fit_location]++;
			bit_set(bitmap, i);
			alloc_nodes++;
			j = _get_avail_cpus(job_ptr, i);
			switches_cpu_cnt[best_fit_location] -= j;
			rem_cpus -= j;
			total_cpus += _get_total_cpus(i);
			break;
		}
		leaf_switch_count++;
		if (job_ptr->req_switch > 0) {
			if (time_waiting >= job_ptr->wait4switch) {
				job_ptr->best_switch = true;
				debug3("%pJ Waited %ld sec for switches use=%d",
					job_ptr, time_waiting,
					leaf_switch_count);
			} else if (leaf_switch_count > job_ptr->req_switch) {
				/* Allocation is for more than requested number
				 * of switches */
				job_ptr->best_switch = false;
				debug3("%pJ waited %ld sec for switches=%u found=%d wait %u",
					job_ptr, time_waiting,
					job_ptr->req_switch,
					leaf_switch_count,
					job_ptr->wait4switch);
			} else {
				job_ptr->best_switch = true;
			}
		}
	}
	if ((alloc_nodes <= max_nodes) && (rem_cpus <= 0) &&
	    (alloc_nodes >= min_nodes)) {
		rc = SLURM_SUCCESS;
	} else
		rc = EINVAL;

fini:	if (rc == SLURM_SUCCESS) {
		/* Job's total_cpus is needed for SELECT_MODE_WILL_RUN */
		job_ptr->total_cpus = total_cpus;
	} else if (alloc_nodes > max_nodes)
		info("%pJ requires more nodes than allowed",
		     job_ptr);
	FREE_NULL_BITMAP(req_nodes_bitmap);
	for (i = 0; i < switch_record_cnt; i++)
		FREE_NULL_BITMAP(switches_bitmap[i]);
	xfree(switches_bitmap);
	xfree(switches_cpu_cnt);
	xfree(switches_node_cnt);
	xfree(switches_node_use);

	return rc;
}


/*
 * _job_test_topo - A topology aware version of _job_test()
 * NOTE: The logic here is almost identical to that of _eval_nodes_topo() in
 *       select/cons_res/job_test.c. Any bug found here is probably also there.
 */
static int _job_test_topo(job_record_t *job_ptr, bitstr_t *bitmap,
			  uint32_t min_nodes, uint32_t max_nodes,
			  uint32_t req_nodes)
{
	bitstr_t **switches_bitmap;		/* nodes on this switch */
	int       *switches_cpu_cnt;		/* total CPUs on switch */
	uint32_t  *switches_node_cnt;		/* total nodes on switch */
	int       *switches_required;		/* set if has required node */

	bitstr_t  *req_nodes_bitmap   = NULL;
	int rem_cpus;			/* remaining resources desired */
	int avail_cpus, total_cpus = 0;
	uint32_t want_nodes, alloc_nodes = 0;
	int i, j, rc = SLURM_SUCCESS;
	int best_fit_inx, first, last;
	int best_fit_nodes, best_fit_cpus;
	int best_fit_location = 0;
	bool sufficient;
	long time_waiting = 0;
	int leaf_switch_count = 0;	/* Count of leaf node switches used */

	if (job_ptr->req_switch) {
		time_t     time_now;
		time_now = time(NULL);
		if (job_ptr->wait4switch_start == 0)
			job_ptr->wait4switch_start = time_now;
		time_waiting = time_now - job_ptr->wait4switch_start;
	}

	rem_cpus = job_ptr->details->min_cpus;
	if (req_nodes > min_nodes)
		want_nodes = req_nodes;
	else
		want_nodes = min_nodes;

	/* Construct a set of switch array entries,
	 * use the same indexes as switch_record_table in slurmctld */
	switches_bitmap   = xcalloc(switch_record_cnt, sizeof(bitstr_t *));
	switches_cpu_cnt  = xcalloc(switch_record_cnt, sizeof(int));
	switches_node_cnt = xcalloc(switch_record_cnt, sizeof(uint32_t));
	switches_required = xcalloc(switch_record_cnt, sizeof(int));
	if (job_ptr->details->req_node_bitmap) {
		req_nodes_bitmap = bit_copy(job_ptr->details->req_node_bitmap);
		i = bit_set_count(req_nodes_bitmap);
		if (i > (int)max_nodes) {
			info("%pJ requires more nodes than currently available (%u>%u)",
			     job_ptr, i, max_nodes);
			rc = EINVAL;
			goto fini;
		}
	}

	/* phase 1: make availability bitmaps for switches */
	sufficient = false;
	for (i=0; i<switch_record_cnt; i++) {
		switches_bitmap[i] = bit_copy(switch_record_table[i].
					      node_bitmap);
		bit_and(switches_bitmap[i], bitmap);
		if (req_nodes_bitmap &&
		    !bit_super_set(req_nodes_bitmap, switches_bitmap[i]))
			switches_node_cnt[i] = 0;
		else {
			switches_node_cnt[i] = bit_set_count(switches_bitmap[i]);
			sufficient = true;
		}
	}
	bit_nclear(bitmap, 0, node_record_count - 1);

#if SELECT_DEBUG
	/* Don't compile this, it slows things down too much */
	for (i=0; i<switch_record_cnt; i++) {
		char *node_names = NULL;
		if (switches_node_cnt[i])
			node_names = bitmap2node_name(switches_bitmap[i]);
		debug("switch=%s nodes=%u:%s speed=%u",
		      switch_record_table[i].name,
		      switches_node_cnt[i], node_names,
		      switch_record_table[i].link_speed);
		xfree(node_names);
	}
#endif

	/* check if requested nodes are available */
	if (!sufficient) {
		info("%pJ requires nodes not available on any switch",
		     job_ptr);
		rc = EINVAL;
		goto fini;
	}

	/* phase 2: accumulate all cpu resources for each switch */
	for (i = 0; i < node_record_count; i++) {
		avail_cpus = _get_avail_cpus(job_ptr, i);
		for (j=0; j<switch_record_cnt; j++) {
			if (bit_test(switches_bitmap[j], i)) {
				switches_cpu_cnt[j] += avail_cpus;
			}
		}
	}

	/* phase 3 */
	/* Determine lowest level switch satisfying request with best fit */
	best_fit_inx = -1;
	for (j = 0; j < switch_record_cnt; j++) {
#if SELECT_DEBUG
		debug5("checking switch %d: nodes %u cpus %d", j,
		       switches_node_cnt[j], switches_cpu_cnt[j]);
#endif
		if ((switches_cpu_cnt[j]  < rem_cpus) ||
		    (switches_node_cnt[j] < min_nodes))
			continue;
		if ((best_fit_inx == -1) ||
		    (switch_record_table[j].level <
		     switch_record_table[best_fit_inx].level) ||
		    ((switch_record_table[j].level ==
		      switch_record_table[best_fit_inx].level) &&
		     (switches_node_cnt[j] < switches_node_cnt[best_fit_inx])))
			best_fit_inx = j;
	}
	if (best_fit_inx == -1) {
		debug("%s: could not find resources for %pJ",
		      __func__, job_ptr);
		rc = EINVAL;
		goto fini;
	}

	/* phase 4: select resources from already allocated leaves */
	/* Identify usable leafs (within higher switch having best fit) */
	for (j = 0; j < switch_record_cnt; j++) {
		if ((switch_record_table[j].level > 0) ||
		    (!bit_super_set(switches_bitmap[j],
				    switches_bitmap[best_fit_inx]))) {
			switches_node_cnt[j] = 0;
		} else if (req_nodes_bitmap) {
			/* we have subnodes count zeroed yet so count them */
			switches_node_cnt[j] = bit_set_count(switches_bitmap[j]);
		}
	}
	/* set already allocated nodes and gather additional resources */
	if (req_nodes_bitmap) {
		/* Accumulate specific required resources, if any */
		for (j = 0; j < switch_record_cnt; j++) {
			if (alloc_nodes > max_nodes)
				break;
			if (switches_node_cnt[j] == 0 ||
			    bit_overlap_any(req_nodes_bitmap,
					    switches_bitmap[j]) == 0)
				continue;

			/* Use nodes from this leaf */
			first = bit_ffs(switches_bitmap[j]);
			if (first < 0) {
				switches_node_cnt[j] = 0;
				continue;
			}
			last  = bit_fls(switches_bitmap[j]);
			for (i=first; i<=last; i++) {
				if (!bit_test(switches_bitmap[j], i))
					continue;
				if (!bit_test(req_nodes_bitmap, i)) {
					/* node wasn't requested */
					continue;
				}

				bit_clear(switches_bitmap[j], i);
				switches_node_cnt[j]--;
				avail_cpus = _get_avail_cpus(job_ptr, i);
				switches_cpu_cnt[j] -= avail_cpus;

				if (bit_test(bitmap, i)) {
					/* node on multiple leaf switches
					 * and already selected */
					continue;
				}

				switches_required[j] = 1;
				bit_set(bitmap, i);
				alloc_nodes++;
				rem_cpus -= avail_cpus;
				total_cpus += _get_total_cpus(i);
			}
		}
		/* Accumulate additional resources from leafs that
		 * contain required nodes */
		for (j=0; j<switch_record_cnt; j++) {
			if ((alloc_nodes > max_nodes) ||
			    ((alloc_nodes >= want_nodes) && (rem_cpus <= 0)))
				break;
			if (switches_required[j] == 0)
				continue;

			/* Use nodes from this leaf */
			first = bit_ffs(switches_bitmap[j]);
			if (first < 0) {
				switches_node_cnt[j] = 0;
				continue;
			}
			last  = bit_fls(switches_bitmap[j]);
			for (i=first; i<=last; i++) {
				if (!bit_test(switches_bitmap[j], i))
					continue;

				/* there is no need here to reset anything
				   for switch j as we disable it after cycle
				   by setting switches_node_cnt[j] to 0 */
				if (bit_test(bitmap, i)) {
					/* node on multiple leaf switches
					 * and already selected */
					continue;
				}

				bit_set(bitmap, i);
				alloc_nodes++;
				rem_cpus -= _get_avail_cpus(job_ptr, i);
				total_cpus += _get_total_cpus(i);
				if ((alloc_nodes > max_nodes) ||
				    ((alloc_nodes >= want_nodes) &&
				     (rem_cpus <= 0)))
					break;
			}
			switches_node_cnt[j] = 0; /* it's used up */
		}
	}

	/* phase 5 */
	/* Select resources from these leafs on a best-fit basis */
	/* Compute best-switch nodes available array */
	while ((alloc_nodes <= max_nodes) &&
	       ((alloc_nodes < want_nodes) || (rem_cpus > 0))) {
		best_fit_cpus = best_fit_nodes = 0;
		for (j=0; j<switch_record_cnt; j++) {
			if (switches_node_cnt[j] == 0)
				continue;
			/* If multiple leaf switches must be used, prefer use
			 * of leaf switches with fewest number of idle CPUs.
			 * This results in more leaf switches being used and
			 * achieves better network bandwidth. */
			if ((best_fit_nodes == 0) ||
			    (!switches_required[best_fit_location] &&
			     switches_required[j]) ||
			    (switches_cpu_cnt[j] < best_fit_cpus)) {
				best_fit_cpus =  switches_cpu_cnt[j];
				best_fit_nodes = switches_node_cnt[j];
				best_fit_location = j;
			}
		}
#if SELECT_DEBUG
		debug("%s: found switch %d for allocation: nodes %d cpus %d "
		       "allocated %u", __func__, best_fit_location,
		       best_fit_nodes, best_fit_cpus, alloc_nodes);
#endif
		if (best_fit_nodes == 0)
			break;

		/* Use select nodes from this leaf */
		first = bit_ffs(switches_bitmap[best_fit_location]);
		if (first < 0) {
			switches_node_cnt[best_fit_location] = 0;
			continue;
		}
		last  = bit_fls(switches_bitmap[best_fit_location]);
		for (i=first; i<=last; i++) {
			if (!bit_test(switches_bitmap[best_fit_location], i))
				continue;

			if (bit_test(bitmap, i)) {
				/* node on multiple leaf switches
				 * and already selected */
				continue;
			}

			switches_required[best_fit_location] = 1;
			bit_set(bitmap, i);
			alloc_nodes++;
			rem_cpus -= _get_avail_cpus(job_ptr, i);
			total_cpus += _get_total_cpus(i);
			if ((alloc_nodes > max_nodes) ||
			    ((alloc_nodes >= want_nodes) && (rem_cpus <= 0)))
				break;
		}
		switches_node_cnt[best_fit_location] = 0;
		leaf_switch_count++;
		if (job_ptr->req_switch > 0) {
			if (time_waiting >= job_ptr->wait4switch) {
				job_ptr->best_switch = true;
				debug3("%pJ Waited %ld sec for switches use=%d",
					job_ptr, time_waiting,
					leaf_switch_count);
			} else if (leaf_switch_count > job_ptr->req_switch) {
				/* Allocation is for more than requested number
				 * of switches */
				job_ptr->best_switch = false;
				debug3("%pJ waited %ld sec for switches=%u found=%d wait %u",
					job_ptr, time_waiting,
					job_ptr->req_switch,
					leaf_switch_count,
					job_ptr->wait4switch);
			} else {
				job_ptr->best_switch = true;
			}
		}
	}
	if ((alloc_nodes <= max_nodes) && (rem_cpus <= 0) &&
	    (alloc_nodes >= min_nodes)) {
		rc = SLURM_SUCCESS;
	} else
		rc = EINVAL;

fini:	if (rc == SLURM_SUCCESS) {
		/* Job's total_cpus is needed for SELECT_MODE_WILL_RUN */
		job_ptr->total_cpus = total_cpus;
	} else if (alloc_nodes > max_nodes)
		info("%pJ requires more nodes than allowed", job_ptr);
	FREE_NULL_BITMAP(req_nodes_bitmap);
	for (i=0; i<switch_record_cnt; i++)
		FREE_NULL_BITMAP(switches_bitmap[i]);
	xfree(switches_bitmap);
	xfree(switches_cpu_cnt);
	xfree(switches_node_cnt);
	xfree(switches_required);

	return rc;
}


/*
 * deallocate resources that were assigned to this job
 *
 * if remove_all = false: the job has been suspended, so just deallocate CPUs
 * if remove_all = true: deallocate all resources
 */
static int _rm_job_from_nodes(struct cr_record *cr_ptr, job_record_t *job_ptr,
			      char *pre_err, bool remove_all, bool job_fini)
{
	int i, i_first, i_last, node_offset, rc = SLURM_SUCCESS;
	struct part_cr_record *part_cr_ptr;
	job_resources_t *job_resrcs_ptr;
	uint64_t job_memory = 0, job_memory_cpu = 0, job_memory_node = 0;
	bool exclusive, is_job_running;
	uint16_t cpu_cnt;
	node_record_t *node_ptr;
	bool old_job = false;

	if (cr_ptr == NULL) {
		error("%s: cr_ptr not initialized", pre_err);
		return SLURM_ERROR;
	}

	if (_rem_tot_job(cr_ptr, job_ptr->job_id) == 0) {
		info("%s: %pJ has no resources allocated",
		     plugin_type, job_ptr);
		return SLURM_ERROR;
	}

	if (job_ptr->start_time < slurmctld_config.boot_time)
		old_job = true;

	if (remove_all && job_ptr->details &&
	    job_ptr->details->pn_min_memory && (cr_type & CR_MEMORY)) {
		if (job_ptr->details->pn_min_memory & MEM_PER_CPU) {
			job_memory_cpu = job_ptr->details->pn_min_memory &
				(~MEM_PER_CPU);
		} else
			job_memory_node = job_ptr->details->pn_min_memory;
	}

	if ((job_resrcs_ptr = job_ptr->job_resrcs) == NULL) {
		error("%pJ lacks a job_resources struct", job_ptr);
		return SLURM_ERROR;
	}

	is_job_running = _rem_run_job(cr_ptr, job_ptr->job_id);
	exclusive = (job_ptr->details->share_res == 0);
	i_first = bit_ffs(job_resrcs_ptr->node_bitmap);
	i_last  = bit_fls(job_resrcs_ptr->node_bitmap);
	if (i_first == -1)	/* job has no nodes */
		i_last = -2;
	node_offset = -1;
	for (i = i_first; i <= i_last; i++) {
		if (!bit_test(job_resrcs_ptr->node_bitmap, i))
			continue;
		node_offset++;
		if (!job_ptr->node_bitmap || !bit_test(job_ptr->node_bitmap, i))
			continue;

		node_ptr = node_record_table_ptr[i];
		cpu_cnt = node_ptr->config_ptr->cpus;
		if (job_memory_cpu)
			job_memory = job_memory_cpu * cpu_cnt;
		else if (job_memory_node)
			job_memory = job_memory_node;
		else if (cr_type & CR_MEMORY)
			job_memory = node_ptr->config_ptr->real_memory;

		if (cr_ptr->nodes[i].alloc_memory >= job_memory)
			cr_ptr->nodes[i].alloc_memory -= job_memory;
		else {
			debug("%s: memory underflow for node %s",
			      pre_err, node_ptr->name);
			cr_ptr->nodes[i].alloc_memory = 0;
		}

		if (remove_all) {
			List job_gres_list;
			List node_gres_list;

			if (cr_ptr->nodes[i].gres_list)
				node_gres_list = cr_ptr->nodes[i].gres_list;
			else
				node_gres_list = node_ptr->gres_list;

			/* Dealloc from allocated GRES if not testing */
			if (job_fini)
				job_gres_list = job_ptr->gres_list_alloc;
			else
				job_gres_list = job_ptr->gres_list_req;

			gres_ctld_job_dealloc(job_gres_list, node_gres_list,
					      node_offset, job_ptr->job_id,
					      node_ptr->name, old_job, false);
			gres_node_state_log(node_gres_list, node_ptr->name);
		}

		if (exclusive) {
			if (cr_ptr->nodes[i].exclusive_cnt)
				cr_ptr->nodes[i].exclusive_cnt--;
			else {
				error("%s: exclusive_cnt underflow for "
				      "node %s", pre_err, node_ptr->name);
			}
		}

		part_cr_ptr = cr_ptr->nodes[i].parts;
		while (part_cr_ptr) {
			if (part_cr_ptr->part_ptr != job_ptr->part_ptr) {
				part_cr_ptr = part_cr_ptr->next;
				continue;
			}
			if (!is_job_running)
				/* cancelled job already suspended */;
			else if (part_cr_ptr->run_job_cnt > 0)
				part_cr_ptr->run_job_cnt--;
			else {
				error("%s: run_job_cnt underflow for node %s",
				      pre_err, node_ptr->name);
			}
			if (remove_all) {
				if (part_cr_ptr->tot_job_cnt > 0)
					part_cr_ptr->tot_job_cnt--;
				else {
					error("%s: tot_job_cnt underflow "
					      "for node %s",
					      pre_err, node_ptr->name);
				}
				if ((part_cr_ptr->tot_job_cnt == 0) &&
				    (part_cr_ptr->run_job_cnt)) {
					part_cr_ptr->run_job_cnt = 0;
					error("%s: run_job_cnt out of sync "
					      "for node %s",
					      pre_err, node_ptr->name);
				}
			}
			break;
		}
		if (part_cr_ptr == NULL) {
			if (job_ptr->part_nodes_missing) {
				;
			} else if (job_ptr->part_ptr) {
				info("%s: %pJ and its partition %s no longer contain node %s",
				     pre_err, job_ptr,
				     job_ptr->partition, node_ptr->name);
			} else {
				info("%s: %pJ has no pointer to partition %s and node %s",
				     pre_err, job_ptr,
				     job_ptr->partition, node_ptr->name);
			}
			job_ptr->part_nodes_missing = true;
			rc = SLURM_ERROR;
		}
	}

	return rc;
}

/* Move all resources from one job to another */
static int _job_expand(job_record_t *from_job_ptr, job_record_t *to_job_ptr)
{
	int i, node_cnt, rc = SLURM_SUCCESS;
	job_resources_t *from_job_resrcs_ptr, *to_job_resrcs_ptr,
		        *new_job_resrcs_ptr;
	bool from_node_used, to_node_used;
	int from_node_offset, to_node_offset, new_node_offset;
	int first_bit, last_bit;
	bitstr_t *tmp_bitmap, *tmp_bitmap2;

	xassert(from_job_ptr);
	xassert(to_job_ptr);
	if (cr_ptr == NULL) {
		error("%s: cr_ptr not initialized", plugin_type);
		return SLURM_ERROR;
	}

	if (from_job_ptr->job_id == to_job_ptr->job_id) {
		error("%s: attempt to merge %pJ with self",
		      plugin_type, from_job_ptr);
		return SLURM_ERROR;
	}
	if (_test_tot_job(cr_ptr, from_job_ptr->job_id) == 0) {
		info("%s: %pJ has no resources allocated",
		     plugin_type, from_job_ptr);
		return SLURM_ERROR;
	}
	if (_test_tot_job(cr_ptr, to_job_ptr->job_id) == 0) {
		info("%s: %pJ has no resources allocated",
		     plugin_type, to_job_ptr);
		return SLURM_ERROR;
	}

	from_job_resrcs_ptr = from_job_ptr->job_resrcs;
	if ((from_job_resrcs_ptr == NULL) ||
	    (from_job_resrcs_ptr->cpus == NULL) ||
	    (from_job_resrcs_ptr->node_bitmap == NULL)) {
		error("%s: %pJ lacks a job_resources struct",
		      plugin_type, from_job_ptr);
		return SLURM_ERROR;
	}
	to_job_resrcs_ptr = to_job_ptr->job_resrcs;
	if ((to_job_resrcs_ptr == NULL) ||
	    (to_job_resrcs_ptr->cpus == NULL) ||
	    (to_job_resrcs_ptr->node_bitmap == NULL)) {
		error("%s: %pJ lacks a job_resources struct",
		      plugin_type, to_job_ptr);
		return SLURM_ERROR;
	}

	(void) _rm_job_from_nodes(cr_ptr, from_job_ptr, "select_p_job_expand",
				  true, true);
	(void) _rm_job_from_nodes(cr_ptr, to_job_ptr,   "select_p_job_expand",
				  true, true);

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
	build_job_resources(new_job_resrcs_ptr);
	to_job_ptr->total_cpus = 0;

	first_bit = MIN(bit_ffs(from_job_resrcs_ptr->node_bitmap),
			bit_ffs(to_job_resrcs_ptr->node_bitmap));
	last_bit  = MAX(bit_fls(from_job_resrcs_ptr->node_bitmap),
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
			/* Merge alloc info from both "from" and "to" jobs,
			 * leave "from" job with no allocated CPUs or memory */
			new_job_resrcs_ptr->cpus[new_node_offset] =
				from_job_resrcs_ptr->cpus[from_node_offset];
			from_job_resrcs_ptr->cpus[from_node_offset] = 0;
			/* new_job_resrcs_ptr->cpus_used[new_node_offset] =
				from_job_resrcs_ptr->
				cpus_used[from_node_offset]; Should be 0 */
			new_job_resrcs_ptr->memory_allocated[new_node_offset] =
				from_job_resrcs_ptr->
				memory_allocated[from_node_offset];
			/* new_job_resrcs_ptr->memory_used[new_node_offset] =
				from_job_resrcs_ptr->
				memory_used[from_node_offset]; Should be 0 */
			job_resources_bits_copy(new_job_resrcs_ptr,
						new_node_offset,
						from_job_resrcs_ptr,
						from_node_offset);
		}
		if (to_node_used) {
			/* Merge alloc info from both "from" and "to" jobs */

			/* DO NOT double count the allocated CPUs in partition
			 * with Shared nodes */
			new_job_resrcs_ptr->cpus[new_node_offset] =
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
		}

		to_job_ptr->total_cpus += new_job_resrcs_ptr->
					  cpus[new_node_offset];
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
	if (to_job_ptr->details) {
		to_job_ptr->details->min_cpus = to_job_ptr->total_cpus;
		to_job_ptr->details->max_cpus = to_job_ptr->total_cpus;
	}
	from_job_ptr->total_cpus   = 0;
	from_job_resrcs_ptr->ncpus = 0;
	if (from_job_ptr->details) {
		from_job_ptr->details->min_cpus = 0;
		from_job_ptr->details->max_cpus = 0;
	}

	from_job_ptr->total_nodes   = 0;
	from_job_resrcs_ptr->nhosts = 0;
	from_job_ptr->node_cnt      = 0;
	if (from_job_ptr->details)
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

	_add_job_to_nodes(cr_ptr, to_job_ptr, "select_p_job_expand", 1);

	return rc;
}

/* Decrement a partitions running and total job counts as needed to enforce the
 * limit of jobs per node per partition (the partition's Shared=# parameter) */
static int _decr_node_job_cnt(int node_inx, job_record_t *job_ptr,
			      char *pre_err)
{
	node_record_t *node_ptr = node_record_table_ptr[node_inx];
	struct part_cr_record *part_cr_ptr;
	bool exclusive = false, is_job_running;

	if (job_ptr->details)
		exclusive = (job_ptr->details->share_res == 0);
	if (exclusive) {
		if (cr_ptr->nodes[node_inx].exclusive_cnt)
			cr_ptr->nodes[node_inx].exclusive_cnt--;
		else {
			error("%s: exclusive_cnt underflow for node %s",
			      pre_err, node_ptr->name);
		}
	}

	is_job_running = _test_run_job(cr_ptr, job_ptr->job_id);
	part_cr_ptr = cr_ptr->nodes[node_inx].parts;
	while (part_cr_ptr) {
		if (part_cr_ptr->part_ptr != job_ptr->part_ptr) {
			part_cr_ptr = part_cr_ptr->next;
			continue;
		}
		if (!is_job_running)
			/* cancelled job already suspended */;
		else if (part_cr_ptr->run_job_cnt > 0)
			part_cr_ptr->run_job_cnt--;
		else {
			error("%s: run_job_cnt underflow for node %s",
			      pre_err, node_ptr->name);
		}
		if (part_cr_ptr->tot_job_cnt > 0)
			part_cr_ptr->tot_job_cnt--;
		else {
			error("%s: tot_job_cnt underflow for node %s",
			      pre_err, node_ptr->name);
		}
		if ((part_cr_ptr->tot_job_cnt == 0) &&
		    (part_cr_ptr->run_job_cnt)) {
			part_cr_ptr->run_job_cnt = 0;
			error("%s: run_job_cnt out of sync for node %s",
			      pre_err, node_ptr->name);
		}
		return SLURM_SUCCESS;
	}

	if (job_ptr->part_ptr) {
		error("%s: Could not find partition %s for node %s",
		      pre_err, job_ptr->part_ptr->name, node_ptr->name);
	} else {
		error("%s: no partition ptr given for %pJ and node %s",
		      pre_err, job_ptr, node_ptr->name);
	}
	return SLURM_ERROR;
}

/*
 * deallocate resources that were assigned to this job on one node
 */
static int _rm_job_from_one_node(job_record_t *job_ptr, node_record_t *node_ptr,
				 char *pre_err)
{
	int i, node_inx, node_offset;
	job_resources_t *job_resrcs_ptr;
	uint64_t job_memory = 0, job_memory_cpu = 0, job_memory_node = 0;
	int first_bit;
	uint16_t cpu_cnt;
	List node_gres_list;
	bool old_job = false;

	if (cr_ptr == NULL) {
		error("%s: cr_ptr not initialized", pre_err);
		return SLURM_ERROR;
	}

	if (_test_tot_job(cr_ptr, job_ptr->job_id) == 0) {
		info("%s: %pJ has no resources allocated",
		     plugin_type, job_ptr);
		return SLURM_ERROR;
	}

	if (job_ptr->details &&
	    job_ptr->details->pn_min_memory && (cr_type & CR_MEMORY)) {
		if (job_ptr->details->pn_min_memory & MEM_PER_CPU) {
			job_memory_cpu = job_ptr->details->pn_min_memory &
				(~MEM_PER_CPU);
		} else
			job_memory_node = job_ptr->details->pn_min_memory;
	}
	if ((job_ptr->job_resrcs == NULL) ||
	    (job_ptr->job_resrcs->cpus == NULL)) {
		error("%pJ lacks a job_resources struct", job_ptr);
		return SLURM_ERROR;
	}
	job_resrcs_ptr = job_ptr->job_resrcs;
	node_inx = node_ptr->index;
	if (!bit_test(job_resrcs_ptr->node_bitmap, node_inx)) {
		error("%pJ allocated nodes (%s) which have been removed from slurm.conf",
		      job_ptr, node_ptr->name);
		return SLURM_ERROR;
	}
	first_bit = bit_ffs(job_resrcs_ptr->node_bitmap);
	node_offset = -1;
	for (i = first_bit; i <= node_inx; i++) {
		if (!bit_test(job_resrcs_ptr->node_bitmap, i))
			continue;
		node_offset++;
	}
	if (job_resrcs_ptr->cpus[node_offset] == 0) {
		error("duplicate relinquish of node %s by %pJ",
		      node_ptr->name, job_ptr);
		return SLURM_ERROR;
	}

	extract_job_resources_node(job_resrcs_ptr, node_offset);

	if (job_ptr->start_time < slurmctld_config.boot_time)
		old_job = true;
	cpu_cnt = node_ptr->config_ptr->cpus;
	if (job_memory_cpu)
		job_memory = job_memory_cpu * cpu_cnt;
	else if (job_memory_node)
		job_memory = job_memory_node;
	else if (cr_type & CR_MEMORY)
		job_memory = node_ptr->config_ptr->real_memory;

	if (cr_ptr->nodes[node_inx].alloc_memory >= job_memory)
		cr_ptr->nodes[node_inx].alloc_memory -= job_memory;
	else {
		cr_ptr->nodes[node_inx].alloc_memory = 0;
		error("%s: memory underflow for node %s",
		      pre_err, node_ptr->name);
	}

	if (cr_ptr->nodes[node_inx].gres_list)
		node_gres_list = cr_ptr->nodes[node_inx].gres_list;
	else
		node_gres_list = node_ptr->gres_list;
	gres_ctld_job_dealloc(job_ptr->gres_list_alloc,
			      node_gres_list, node_offset,
			      job_ptr->job_id, node_ptr->name, old_job, true);
	gres_node_state_log(node_gres_list, node_ptr->name);

	return _decr_node_job_cnt(node_inx, job_ptr, pre_err);
}

/*
 * allocate resources to the given job
 *
 * if alloc_all = 0: the job has been suspended, so just re-allocate CPUs
 * if alloc_all = 1: allocate all resources (CPUs and memory)
 */
static int _add_job_to_nodes(struct cr_record *cr_ptr,
			     job_record_t *job_ptr, char *pre_err,
			     int alloc_all)
{
	int i, i_first, i_last, node_cnt, node_offset, rc = SLURM_SUCCESS;
	bool exclusive;
	struct part_cr_record *part_cr_ptr;
	job_resources_t *job_resrcs_ptr;
	uint64_t job_memory_cpu = 0, job_memory_node = 0;
	uint16_t cpu_cnt;
	node_record_t *node_ptr;
	List gres_list;
	bool new_alloc = true;

	if (cr_ptr == NULL) {
		error("%s: cr_ptr not initialized", pre_err);
		return SLURM_ERROR;
	}

	if (alloc_all && job_ptr->details &&
	    job_ptr->details->pn_min_memory && (cr_type & CR_MEMORY)) {
		if (job_ptr->details->pn_min_memory & MEM_PER_CPU) {
			job_memory_cpu = job_ptr->details->pn_min_memory &
				(~MEM_PER_CPU);
		} else
			job_memory_node = job_ptr->details->pn_min_memory;
	}
	if ((job_resrcs_ptr = job_ptr->job_resrcs) == NULL) {
		error("%pJ lacks a job_resources struct", job_ptr);
		return SLURM_ERROR;
	}

	exclusive = (job_ptr->details->share_res == 0);
	if (alloc_all)
		_add_run_job(cr_ptr, job_ptr->job_id);
	_add_tot_job(cr_ptr, job_ptr->job_id);

	i_first = bit_ffs(job_resrcs_ptr->node_bitmap);
	i_last  = bit_fls(job_resrcs_ptr->node_bitmap);
	node_cnt = bit_set_count(job_resrcs_ptr->node_bitmap);
	if (i_first == -1)	/* job has no nodes */
		i_last = -2;
	node_offset = -1;

	if (job_ptr->gres_list_alloc)
		new_alloc = false;

	for (i = i_first; i <= i_last; i++) {
		if (!bit_test(job_resrcs_ptr->node_bitmap, i))
			continue;
		node_offset++;
		if (!bit_test(job_ptr->node_bitmap, i))
			continue;

		node_ptr = node_record_table_ptr[i];
		cpu_cnt = node_ptr->config_ptr->cpus;

		if (job_memory_cpu) {
			cr_ptr->nodes[i].alloc_memory += job_memory_cpu *
				cpu_cnt;
		} else if (job_memory_node) {
			cr_ptr->nodes[i].alloc_memory += job_memory_node;
		} else if (cr_type & CR_MEMORY) {
			cr_ptr->nodes[i].alloc_memory +=
				node_ptr->config_ptr->real_memory;
		}

		if (alloc_all) {
			if (cr_ptr->nodes[i].gres_list)
				gres_list = cr_ptr->nodes[i].gres_list;
			else
				gres_list = node_ptr->gres_list;
			gres_ctld_job_alloc(job_ptr->gres_list_req,
					    &job_ptr->gres_list_alloc,
					    gres_list, node_cnt, i, node_offset,
					    job_ptr->job_id, node_ptr->name,
					    NULL, new_alloc);
			gres_node_state_log(gres_list, node_ptr->name);
		}

		if (exclusive)
			cr_ptr->nodes[i].exclusive_cnt++;

		part_cr_ptr = cr_ptr->nodes[i].parts;
		while (part_cr_ptr) {
			if (part_cr_ptr->part_ptr != job_ptr->part_ptr) {
				part_cr_ptr = part_cr_ptr->next;
				continue;
			}
			if (alloc_all)
				part_cr_ptr->run_job_cnt++;
			part_cr_ptr->tot_job_cnt++;
			break;
		}
		if (part_cr_ptr == NULL) {
			info("%s: %pJ could not find partition %s for node %s",
			     pre_err, job_ptr, job_ptr->partition,
			     node_ptr->name);
			job_ptr->part_nodes_missing = true;
			rc = SLURM_ERROR;
		}
	}

	if (alloc_all) {
		gres_ctld_job_build_details(job_ptr->gres_list_alloc,
					    &job_ptr->gres_detail_cnt,
					    &job_ptr->gres_detail_str,
					    &job_ptr->gres_used);
	}
	return rc;
}

static void _free_cr(struct cr_record *cr_ptr)
{
	int i;
	struct part_cr_record *part_cr_ptr1, *part_cr_ptr2;

	if (cr_ptr == NULL)
		return;

	for (i = 0; i < node_record_count; i++) {
		part_cr_ptr1 = cr_ptr->nodes[i].parts;
		while (part_cr_ptr1) {
			part_cr_ptr2 = part_cr_ptr1->next;
			xfree(part_cr_ptr1);
			part_cr_ptr1 = part_cr_ptr2;
		}
		FREE_NULL_LIST(cr_ptr->nodes[i].gres_list);
	}
	xfree(cr_ptr->nodes);
	xfree(cr_ptr->run_job_ids);
	xfree(cr_ptr->tot_job_ids);
	xfree(cr_ptr);
}

static void _dump_node_cr(struct cr_record *cr_ptr)
{
#if SELECT_DEBUG
	int i;
	struct part_cr_record *part_cr_ptr;
	node_record_t *node_ptr;
	List gres_list;

	if ((cr_ptr == NULL) || (cr_ptr->nodes == NULL))
		return;

	for (i = 0; i < cr_ptr->run_job_len; i++) {
		if (cr_ptr->run_job_ids[i])
			info("Running JobId=%u", cr_ptr->run_job_ids[i]);
	}
	for (i = 0; i < cr_ptr->tot_job_len; i++) {
		if (cr_ptr->tot_job_ids[i])
			info("Alloc JobId=%u", cr_ptr->tot_job_ids[i]);
	}

	for (i = 0; (node_ptr = next_node(&i)); i++) {
		info("Node:%s exclusive_cnt:%u alloc_mem:%"PRIu64"",
		     node_ptr->name,
		     cr_ptr->nodes[node_ptr->index].exclusive_cnt,
		     cr_ptr->nodes[node_ptr->index].alloc_memory);

		part_cr_ptr = cr_ptr->nodes[node_ptr->index].parts;
		while (part_cr_ptr) {
			info("  Part:%s run:%u tot:%u",
			     part_cr_ptr->part_ptr->name,
			     part_cr_ptr->run_job_cnt,
			     part_cr_ptr->tot_job_cnt);
			part_cr_ptr = part_cr_ptr->next;
		}

		if (cr_ptr->nodes[node_ptr->index].gres_list)
			gres_list = cr_ptr->nodes[node_ptr->index].gres_list;
		else
			gres_list = node_ptr->gres_list;
		if (gres_list)
			gres_node_state_log(gres_list, node_ptr->name);
	}
#endif
}

static struct cr_record *_dup_cr(struct cr_record *cr_ptr)
{
	int i;
	struct cr_record *new_cr_ptr;
	struct part_cr_record *part_cr_ptr, *new_part_cr_ptr;
	node_record_t *node_ptr;
	List gres_list;

	if (cr_ptr == NULL)
		return NULL;

	new_cr_ptr = xmalloc(sizeof(struct cr_record));
	new_cr_ptr->run_job_len = cr_ptr->run_job_len;
	i = sizeof(uint32_t) * cr_ptr->run_job_len;
	new_cr_ptr->run_job_ids = xmalloc(i);
	memcpy(new_cr_ptr->run_job_ids, cr_ptr->run_job_ids, i);
	new_cr_ptr->tot_job_len = cr_ptr->tot_job_len;
	i = sizeof(uint32_t) * cr_ptr->tot_job_len;
	new_cr_ptr->tot_job_ids = xmalloc(i);
	memcpy(new_cr_ptr->tot_job_ids, cr_ptr->tot_job_ids, i);

	new_cr_ptr->nodes = xcalloc(node_record_count,
				    sizeof(struct node_cr_record));
	for (i = 0; (node_ptr = next_node(&i)); i++) {
		new_cr_ptr->nodes[node_ptr->index].alloc_memory =
			cr_ptr->nodes[node_ptr->index].alloc_memory;
		new_cr_ptr->nodes[node_ptr->index].exclusive_cnt =
			cr_ptr->nodes[node_ptr->index].exclusive_cnt;

		part_cr_ptr = cr_ptr->nodes[node_ptr->index].parts;
		while (part_cr_ptr) {
			new_part_cr_ptr =
				xmalloc(sizeof(struct part_cr_record));
			new_part_cr_ptr->part_ptr    = part_cr_ptr->part_ptr;
			new_part_cr_ptr->run_job_cnt = part_cr_ptr->run_job_cnt;
			new_part_cr_ptr->tot_job_cnt = part_cr_ptr->tot_job_cnt;
			new_part_cr_ptr->next =
				new_cr_ptr->nodes[node_ptr->index]. parts;
			new_cr_ptr->nodes[node_ptr->index].parts =
				new_part_cr_ptr;
			part_cr_ptr = part_cr_ptr->next;
		}

		if (cr_ptr->nodes[node_ptr->index].gres_list)
			gres_list = cr_ptr->nodes[node_ptr->index].gres_list;
		else
			gres_list = node_ptr->gres_list;
		new_cr_ptr->nodes[node_ptr->index].gres_list =
			gres_node_state_list_dup(gres_list);
	}
	return new_cr_ptr;
}

static void _init_node_cr(void)
{
	part_record_t *part_ptr;
	struct part_cr_record *part_cr_ptr;
	job_resources_t *job_resrcs_ptr;
	node_record_t *node_ptr;
	ListIterator part_iterator;
	job_record_t *job_ptr;
	ListIterator job_iterator;
	uint64_t job_memory_cpu, job_memory_node;
	int exclusive, i, i_first, i_last, node_offset;

	if (cr_ptr)
		return;

	cr_ptr = xmalloc(sizeof(struct cr_record));
	cr_ptr->nodes = xcalloc(node_record_count,
				sizeof(struct node_cr_record));

	/* build partition records */
	part_iterator = list_iterator_create(part_list);
	while ((part_ptr = list_next(part_iterator))) {
		for (i = 0; i < node_record_count; i++) {
			if (part_ptr->node_bitmap == NULL)
				break;
			if (!bit_test(part_ptr->node_bitmap, i))
				continue;
			part_cr_ptr = xmalloc(sizeof(struct part_cr_record));
			part_cr_ptr->next = cr_ptr->nodes[i].parts;
			part_cr_ptr->part_ptr = part_ptr;
			cr_ptr->nodes[i].parts = part_cr_ptr;
		}

	}
	list_iterator_destroy(part_iterator);

	/* Clear existing node Gres allocations */
	for (i = 0; (node_ptr = next_node(&i)); i++) {
		gres_node_state_dealloc_all(node_ptr->gres_list);
	}

	/* record running and suspended jobs in node_cr_records */
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = list_next(job_iterator))) {
		bool new_alloc = true;
		if (!IS_JOB_RUNNING(job_ptr) && !IS_JOB_SUSPENDED(job_ptr))
			continue;
		if ((job_resrcs_ptr = job_ptr->job_resrcs) == NULL) {
			error("%pJ lacks a job_resources struct",
			      job_ptr);
			continue;
		}
		if (IS_JOB_RUNNING(job_ptr) ||
		    (IS_JOB_SUSPENDED(job_ptr) && (job_ptr->priority != 0)))
			_add_run_job(cr_ptr, job_ptr->job_id);
		_add_tot_job(cr_ptr, job_ptr->job_id);

		job_memory_cpu  = 0;
		job_memory_node = 0;
		if (job_ptr->details && job_ptr->details->pn_min_memory &&
		    (cr_type & CR_MEMORY)) {
			if (job_ptr->details->pn_min_memory & MEM_PER_CPU) {
				job_memory_cpu = job_ptr->details->
					pn_min_memory &
					(~MEM_PER_CPU);
			} else {
				job_memory_node = job_ptr->details->
					pn_min_memory;
			}
		}

		/* Use job_resrcs_ptr->node_bitmap rather than
		 * job_ptr->node_bitmap which can have DOWN nodes
		 * cleared from the bitmap */
		if (job_resrcs_ptr->node_bitmap == NULL)
			continue;

		if (job_ptr->details)
			exclusive = (job_ptr->details->share_res == 0);
		else
			exclusive = 0;
		node_offset = -1;
		i_first = bit_ffs(job_resrcs_ptr->node_bitmap);
		i_last  = bit_fls(job_resrcs_ptr->node_bitmap);
		if (i_first == -1)
			i_last = -2;

		if (job_ptr->gres_list_alloc)
			new_alloc = false;

		for (i = i_first; i <= i_last; i++) {
			if (!bit_test(job_resrcs_ptr->node_bitmap, i))
				continue;
			node_offset++;
			if (!bit_test(job_ptr->node_bitmap, i))
				continue; /* node already released */
			node_ptr = node_record_table_ptr[i];
			if (exclusive)
				cr_ptr->nodes[i].exclusive_cnt++;
			if (job_memory_cpu == 0) {
				if (!job_memory_node && (cr_type & CR_MEMORY))
					job_memory_node = node_ptr->config_ptr->
						real_memory;
				cr_ptr->nodes[i].alloc_memory +=
					job_memory_node;
			} else {
				cr_ptr->nodes[i].alloc_memory +=
					job_memory_cpu *
					node_record_table_ptr[i]->
					config_ptr->cpus;
			}

			if (bit_test(job_ptr->node_bitmap, i)) {
				gres_ctld_job_alloc(job_ptr->gres_list_req,
						    &job_ptr->gres_list_alloc,
						    node_ptr->gres_list,
						    job_resrcs_ptr->nhosts,
						    i, node_offset,
						    job_ptr->job_id,
						    node_ptr->name,
						    NULL, new_alloc);
			}

			part_cr_ptr = cr_ptr->nodes[i].parts;
			while (part_cr_ptr) {
				if (part_cr_ptr->part_ptr !=
				    job_ptr->part_ptr) {
					part_cr_ptr = part_cr_ptr->next;
					continue;
				}
				if (IS_JOB_RUNNING(job_ptr) ||
				    (IS_JOB_SUSPENDED(job_ptr) &&
				     (job_ptr->priority != 0))) {
					/* Running or being gang scheduled */
					part_cr_ptr->run_job_cnt++;
				}
				part_cr_ptr->tot_job_cnt++;
				break;
			}
			if (part_cr_ptr == NULL) {
				info("%s: %pJ could not find partition %s for node %s",
				     __func__, job_ptr, job_ptr->partition,
				     node_ptr->name);
				job_ptr->part_nodes_missing = true;
			}
		}
	}
	list_iterator_destroy(job_iterator);
	_dump_node_cr(cr_ptr);
}

static int _find_job (void *x, void *key)
{
	job_record_t *job_ptr = (job_record_t *) x;
	if (job_ptr == (job_record_t *) key)
		return 1;
	return 0;
}

static bool _is_preemptable(job_record_t *job_ptr, List preemptee_candidates)
{
	if (!preemptee_candidates)
		return false;
	if (list_find_first(preemptee_candidates, _find_job, job_ptr))
		return true;
	return false;
}

/* Determine if a job can ever run */
static int _test_only(job_record_t *job_ptr, bitstr_t *bitmap,
		      uint32_t min_nodes, uint32_t max_nodes,
		      uint32_t req_nodes, int max_share)
{
	bitstr_t *orig_map;
	int i, rc = SLURM_ERROR;
	uint64_t save_mem;

	orig_map = bit_copy(bitmap);

	/* Try to run with currently available nodes */
	i = _job_count_bitmap(cr_ptr, job_ptr, orig_map, bitmap,
			      NO_SHARE_LIMIT, NO_SHARE_LIMIT,
			      SELECT_MODE_TEST_ONLY);
	if (i >= min_nodes) {
		save_mem = job_ptr->details->pn_min_memory;
		job_ptr->details->pn_min_memory = 0;
		rc = _job_test(job_ptr, bitmap, min_nodes,
			       max_nodes, req_nodes);
		job_ptr->details->pn_min_memory = save_mem;
	}
	FREE_NULL_BITMAP(orig_map);

	return rc;
}

/*
 * Sort the usable_node element to put jobs in the correct
 * preemption order.
 */
static int _sort_usable_nodes_dec(void *j1, void *j2)
{
	job_record_t *job_a = *(job_record_t **)j1;
	job_record_t *job_b = *(job_record_t **)j2;

	if (job_a->details->usable_nodes > job_b->details->usable_nodes)
		return -1;
	else if (job_a->details->usable_nodes < job_b->details->usable_nodes)
		return 1;

	return 0;
}

/* Allocate resources for a job now, if possible */
static int _run_now(job_record_t *job_ptr, bitstr_t *bitmap,
		    uint32_t min_nodes, uint32_t max_nodes,
		    int max_share, uint32_t req_nodes,
		    List preemptee_candidates,
		    List *preemptee_job_list)
{

	bitstr_t *orig_map;
	int max_run_job, j, sus_jobs, rc = EINVAL, prev_cnt = -1;
	job_record_t *tmp_job_ptr;
	ListIterator job_iterator, preemptee_iterator;
	struct cr_record *exp_cr;
	uint16_t pass_count = 0;

	orig_map = bit_copy(bitmap);

	for (max_run_job=0; ((max_run_job<max_share) && (rc != SLURM_SUCCESS));
	     max_run_job++) {
		bool last_iteration = (max_run_job == (max_share - 1));
		for (sus_jobs=0; ((sus_jobs<5) && (rc != SLURM_SUCCESS));
		     sus_jobs+=4) {
			if (last_iteration)
				sus_jobs = NO_SHARE_LIMIT;
			j = _job_count_bitmap(cr_ptr, job_ptr,
					      orig_map, bitmap,
					      max_run_job,
					      max_run_job + sus_jobs,
					      SELECT_MODE_RUN_NOW);
#if SELECT_DEBUG
			{
				char *node_list = bitmap2node_name(bitmap);
				info("%s: %pJ iter:%d cnt:%d nodes:%s",
				     __func__, job_ptr, max_run_job, j,
				     node_list);
				xfree(node_list);
			}
#endif
			if ((j == prev_cnt) || (j < min_nodes))
				continue;
			prev_cnt = j;
			if (max_run_job > 0) {
				/* We need to share. Try to find
				 * suitable job to share nodes with */
				rc = _find_job_mate(job_ptr, bitmap,
						    min_nodes,
						    max_nodes, req_nodes);
				if (rc == SLURM_SUCCESS)
					break;
			}
			rc = _job_test(job_ptr, bitmap, min_nodes, max_nodes,
				       req_nodes);
		}
	}

top:	if ((rc != SLURM_SUCCESS) && preemptee_candidates &&
	    (exp_cr = _dup_cr(cr_ptr))) {
		/* Remove all preemptable jobs from simulated environment */
		job_iterator = list_iterator_create(preemptee_candidates);
		while ((tmp_job_ptr = list_next(job_iterator))) {
			bool remove_all = false;
			uint16_t mode;

			if (!IS_JOB_RUNNING(tmp_job_ptr) &&
			    !IS_JOB_SUSPENDED(tmp_job_ptr))
				continue;
			mode = slurm_job_preempt_mode(tmp_job_ptr);
			if ((mode == PREEMPT_MODE_REQUEUE)    ||
			    (mode == PREEMPT_MODE_CANCEL))
				remove_all = true;
			/* Remove preemptable job now */
			_rm_job_from_nodes(exp_cr, tmp_job_ptr, "_run_now",
					   remove_all, false);
			j = _job_count_bitmap(exp_cr, job_ptr,
					      orig_map, bitmap,
					      (max_share - 1),
					      NO_SHARE_LIMIT,
					      SELECT_MODE_RUN_NOW);
			tmp_job_ptr->details->usable_nodes =
				bit_overlap(bitmap, tmp_job_ptr->node_bitmap);
			if (j < min_nodes)
				continue;
			rc = _job_test(job_ptr, bitmap, min_nodes,
				       max_nodes, req_nodes);
			/*
			 * If successful, set the last job's usable count to a
			 * large value so that it will be first after sorting.
			 * Note: usable_count is only used for sorting purposes
			 */
			if (rc == SLURM_SUCCESS) {
				if (pass_count++ ||
				    (list_count(preemptee_candidates) == 1))
					break;
				tmp_job_ptr->details->usable_nodes = 9999;
				while ((tmp_job_ptr = list_next(job_iterator))) {
					tmp_job_ptr->details->usable_nodes = 0;
				}
				list_sort(preemptee_candidates,
					  (ListCmpF)_sort_usable_nodes_dec);
				rc = EINVAL;
				list_iterator_destroy(job_iterator);
				_free_cr(exp_cr);
				goto top;
			}
		}
		list_iterator_destroy(job_iterator);

		if ((rc == SLURM_SUCCESS) && preemptee_job_list &&
		    preemptee_candidates) {
			/* Build list of preemptee jobs whose resources are
			 * actually used */
			if (*preemptee_job_list == NULL) {
				*preemptee_job_list = list_create(NULL);
			}
			preemptee_iterator = list_iterator_create(
				preemptee_candidates);
			while ((tmp_job_ptr = list_next(preemptee_iterator))) {
				if (bit_overlap_any(bitmap,
						    tmp_job_ptr->
							node_bitmap) == 0)
					continue;
				if (tmp_job_ptr->details->usable_nodes == 0)
					continue;
				list_append(*preemptee_job_list,
					    tmp_job_ptr);
			}
			list_iterator_destroy(preemptee_iterator);
		}
		_free_cr(exp_cr);
	}
	if (rc == SLURM_SUCCESS)
		_build_select_struct(job_ptr, bitmap);
	FREE_NULL_BITMAP(orig_map);

	return rc;
}

/*
 * Determine where and when the job at job_ptr can begin execution by updating
 * a scratch cr_record structure to reflect each job terminating at the
 * end of its time limit and use this to show where and when the job at job_ptr
 * will begin execution. Used by Slurm's sched/backfill plugin.
 */
static int _will_run_test(job_record_t *job_ptr, bitstr_t *bitmap,
			  uint32_t min_nodes, uint32_t max_nodes,
			  int max_share, uint32_t req_nodes,
			  List preemptee_candidates,
			  List *preemptee_job_list)
{
	struct cr_record *exp_cr;
	job_record_t *tmp_job_ptr;
	List cr_job_list;
	ListIterator job_iterator, preemptee_iterator;
	bitstr_t *orig_map;
	int i, max_run_jobs, rc = SLURM_ERROR;
	time_t now = time(NULL);

	max_run_jobs = MAX((max_share - 1), 1);	/* exclude this job */
	orig_map = bit_copy(bitmap);

	/* Try to run with currently available nodes */
	i = _job_count_bitmap(cr_ptr, job_ptr, orig_map, bitmap,
			      max_run_jobs, NO_SHARE_LIMIT,
			      SELECT_MODE_WILL_RUN);
	if (i >= min_nodes) {
		rc = _job_test(job_ptr, bitmap, min_nodes, max_nodes,
			       req_nodes);
		if (rc == SLURM_SUCCESS) {
			FREE_NULL_BITMAP(orig_map);
			job_ptr->start_time = time(NULL);
			return SLURM_SUCCESS;
		}
	}

	/* Job is still pending. Simulate termination of jobs one at a time
	 * to determine when and where the job can start. */
	exp_cr = _dup_cr(cr_ptr);
	if (exp_cr == NULL) {
		FREE_NULL_BITMAP(orig_map);
		return SLURM_ERROR;
	}

	/* Build list of running and suspended jobs */
	cr_job_list = list_create(NULL);
	job_iterator = list_iterator_create(job_list);
	while ((tmp_job_ptr = list_next(job_iterator))) {
		if (!IS_JOB_RUNNING(tmp_job_ptr) &&
		    !IS_JOB_SUSPENDED(tmp_job_ptr))
			continue;
		if (tmp_job_ptr->end_time == 0) {
			error("%s: Active %pJ has zero end_time",
			      __func__, tmp_job_ptr);
			continue;
		}
		if (tmp_job_ptr->node_bitmap == NULL) {
			error("%s: %pJ has NULL node_bitmap",
			      __func__, tmp_job_ptr);
			continue;
		}
		if (!_is_preemptable(tmp_job_ptr, preemptee_candidates)) {
			/* Queue job for later removal from data structures */
			list_append(cr_job_list, tmp_job_ptr);
		} else {
			uint16_t mode = slurm_job_preempt_mode(tmp_job_ptr);
			bool remove_all = false;
			if ((mode == PREEMPT_MODE_REQUEUE)    ||
			    (mode == PREEMPT_MODE_CANCEL))
				remove_all = true;
			/* Remove preemptable job now */
			_rm_job_from_nodes(exp_cr, tmp_job_ptr,
					   "_will_run_test", remove_all, false);
		}
	}
	list_iterator_destroy(job_iterator);

	/* Test with all preemptable jobs gone */
	if (preemptee_candidates) {
		i = _job_count_bitmap(exp_cr, job_ptr, orig_map, bitmap,
				      max_run_jobs, NO_SHARE_LIMIT,
				      SELECT_MODE_RUN_NOW);
		if (i >= min_nodes) {
			rc = _job_test(job_ptr, bitmap, min_nodes, max_nodes,
				       req_nodes);
			if (rc == SLURM_SUCCESS) {
				/* Actual start time will actually be later
				 * than "now", but return "now" for backfill
				 * scheduler to initiate preemption. */
				job_ptr->start_time = now;
			}
		}
	}

	/* Remove the running jobs one at a time from exp_node_cr and try
	 * scheduling the pending job after each one */
	if ((rc != SLURM_SUCCESS) &&
	    ((job_ptr->bit_flags & TEST_NOW_ONLY) == 0)) {
		list_sort(cr_job_list, _cr_job_list_sort);
		job_iterator = list_iterator_create(cr_job_list);
		while ((tmp_job_ptr = list_next(job_iterator))) {
			_rm_job_from_nodes(exp_cr, tmp_job_ptr,
					   "_will_run_test", true, false);
			i = _job_count_bitmap(exp_cr, job_ptr, orig_map,
					      bitmap, max_run_jobs,
					      NO_SHARE_LIMIT,
					      SELECT_MODE_RUN_NOW);
			if (i < min_nodes)
				continue;
			rc = _job_test(job_ptr, bitmap, min_nodes, max_nodes,
				       req_nodes);
			if (rc != SLURM_SUCCESS)
				continue;
			if (tmp_job_ptr->end_time <= now)
				job_ptr->start_time = now + 1;
			else
				job_ptr->start_time = tmp_job_ptr->end_time;
			break;
		}
		list_iterator_destroy(job_iterator);
	}

	if ((rc == SLURM_SUCCESS) && preemptee_job_list &&
	    preemptee_candidates) {
		/* Build list of preemptee jobs whose resources are
		 * actually used. List returned even if not killed
		 * in selected plugin, but by Moab or something else. */
		if (*preemptee_job_list == NULL) {
			*preemptee_job_list = list_create(NULL);
		}
		preemptee_iterator =list_iterator_create(preemptee_candidates);
		while ((tmp_job_ptr = list_next(preemptee_iterator))) {
			if (bit_overlap_any(bitmap,
					    tmp_job_ptr->node_bitmap) == 0)
				continue;

			list_append(*preemptee_job_list, tmp_job_ptr);
		}
		list_iterator_destroy(preemptee_iterator);
	}

	FREE_NULL_LIST(cr_job_list);
	_free_cr(exp_cr);
	FREE_NULL_BITMAP(orig_map);
	return rc;
}

static int  _cr_job_list_sort(void *x, void *y)
{
	job_record_t *job1_ptr = *(job_record_t **) x;
	job_record_t *job2_ptr = *(job_record_t **) y;
	return (int) SLURM_DIFFTIME(job1_ptr->end_time, job2_ptr->end_time);
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	int rc = SLURM_SUCCESS;

	cr_type = slurm_conf.select_type_param;
	if (cr_type)
		verbose("%s loaded with argument %u", plugin_name, cr_type);

	if (xstrcasestr(slurm_conf.topology_param, "dragonfly"))
		have_dragonfly = true;
	if (xstrcasestr(slurm_conf.topology_param, "TopoOptional"))
		topo_optional = true;

	return rc;
}

extern int fini ( void )
{
	int rc = SLURM_SUCCESS;

	cr_fini_global_core_data();
	slurm_mutex_lock(&cr_mutex);
	_free_cr(cr_ptr);
	cr_ptr = NULL;
	slurm_mutex_unlock(&cr_mutex);
	return rc;
}

/*
 * The remainder of this file implements the standard Slurm
 * node selection API.
 */

extern int select_p_state_save(char *dir_name)
{
	return SLURM_SUCCESS;
}

extern int select_p_state_restore(char *dir_name)
{
	return SLURM_SUCCESS;
}

/*
 * Note the initialization of job records, issued upon restart of
 * slurmctld and used to synchronize any job state.
 */
extern int select_p_job_init(List job_list_arg)
{
	return SLURM_SUCCESS;
}

extern int select_p_node_init()
{
	/* NOTE: We free the consumable resources info here, but
	 * can't rebuild it since the partition and node structures
	 * have not yet had node bitmaps reset. */
	slurm_mutex_lock(&cr_mutex);
	_free_cr(cr_ptr);
	cr_ptr = NULL;

	cr_init_global_core_data(node_record_table_ptr, node_record_count);
	slurm_mutex_unlock(&cr_mutex);

	return SLURM_SUCCESS;
}

/*
 * select_p_job_test - Given a specification of scheduling requirements,
 *	identify the nodes which "best" satisfy the request.
 * 	"best" is defined as either single set of consecutive nodes satisfying
 *	the request and leaving the minimum number of unused nodes OR
 *	the fewest number of consecutive node sets
 * IN/OUT job_ptr - pointer to job being considered for initiation,
 *                  set's start_time when job expected to start
 * IN/OUT bitmap - usable nodes are set on input, nodes not required to
 *	satisfy the request are cleared, other left set
 * IN min_nodes - minimum count of nodes
 * IN req_nodes - requested (or desired) count of nodes
 * IN max_nodes - maximum count of nodes
 * IN mode - SELECT_MODE_RUN_NOW: try to schedule job now
 *           SELECT_MODE_TEST_ONLY: test if job can ever run
 *           SELECT_MODE_WILL_RUN: determine when and where job can run
 * IN preemptee_candidates - List of pointers to jobs which can be preempted.
 * IN/OUT preemptee_job_list - Pointer to list of job pointers. These are the
 *		jobs to be preempted to initiate the pending job. Not set
 *		if mode=SELECT_MODE_TEST_ONLY or input pointer is NULL.
 * IN exc_core_bitmap - bitmap of cores being reserved.
 * RET zero on success, EINVAL otherwise
 * globals (passed via select_p_node_init):
 *	node_record_count - count of nodes configured
 *	node_record_table_ptr - pointer to global node table
 * NOTE: the job information that is considered for scheduling includes:
 *	req_node_bitmap: bitmap of specific nodes required by the job
 *	contiguous: allocated nodes must be sequentially located
 *	num_cpus: minimum number of processors required by the job
 * NOTE: bitmap must be a superset of the job's required at the time that
 *	select_p_job_test is called
 */
extern int select_p_job_test(job_record_t *job_ptr, bitstr_t *bitmap,
			     uint32_t min_nodes, uint32_t max_nodes,
			     uint32_t req_nodes, uint16_t mode,
			     List preemptee_candidates,
			     List *preemptee_job_list,
			     bitstr_t *exc_core_bitmap)
{
	int max_share = 0, rc = EINVAL;

	xassert(bitmap);
	if (job_ptr->details == NULL)
		return EINVAL;

	slurm_mutex_lock(&cr_mutex);
	if (cr_ptr == NULL) {
		_init_node_cr();
		if (cr_ptr == NULL) {
			slurm_mutex_unlock(&cr_mutex);
			error("select_p_job_test: cr_ptr not initialized");
			return SLURM_ERROR;
		}
	}

	if (bit_set_count(bitmap) < min_nodes) {
		slurm_mutex_unlock(&cr_mutex);
		return EINVAL;
	}

	if (job_ptr->details->core_spec != NO_VAL16) {
		verbose("%s: %pJ core_spec(%u) not supported",
			plugin_type, job_ptr, job_ptr->details->core_spec);
		job_ptr->details->core_spec = NO_VAL16;
	}

	if (job_ptr->details->share_res)
		max_share = job_ptr->part_ptr->max_share & ~SHARED_FORCE;
	else	/* ((shared == 0) || (shared == NO_VAL16)) */
		max_share = 1;

	if (mode == SELECT_MODE_WILL_RUN) {
		rc = _will_run_test(job_ptr, bitmap, min_nodes, max_nodes,
				    max_share, req_nodes,
				    preemptee_candidates, preemptee_job_list);
		if (!job_ptr->best_switch)
			rc = SLURM_ERROR;
	} else if (mode == SELECT_MODE_TEST_ONLY) {
		rc = _test_only(job_ptr, bitmap, min_nodes, max_nodes,
				req_nodes, max_share);
	} else if (mode == SELECT_MODE_RUN_NOW) {
		rc = _run_now(job_ptr, bitmap, min_nodes, max_nodes,
			      max_share, req_nodes,
			      preemptee_candidates, preemptee_job_list);
		if (!job_ptr->best_switch)
			rc = SLURM_ERROR;
	} else
		fatal("select_p_job_test: Mode %d is invalid", mode);

	slurm_mutex_unlock(&cr_mutex);

	return rc;
}

/*
 * Note initiation of job is about to begin. Called immediately
 * after select_p_job_test(). Executed from slurmctld.
 * IN job_ptr - pointer to job being initiated
 */
extern int select_p_job_begin(job_record_t *job_ptr)
{
	int rc = SLURM_SUCCESS;

	slurm_mutex_lock(&cr_mutex);
	if (cr_ptr == NULL)
		_init_node_cr();
	if (rc == SLURM_SUCCESS)
		rc = _add_job_to_nodes(cr_ptr, job_ptr, "select_p_job_begin", 1);

	gres_job_state_log(job_ptr->gres_list_req, job_ptr->job_id);

	if ((slurm_conf.debug_flags & DEBUG_FLAG_GRES) &&
	    job_ptr->gres_list_alloc)
		info("Alloc GRES");
	gres_job_state_log(job_ptr->gres_list_alloc, job_ptr->job_id);
	slurm_mutex_unlock(&cr_mutex);
	return rc;
}

/*
 * Determine if allocated nodes are usable (powered up)
 * IN job_ptr - pointer to job being tested
 * RET -1 on error, 1 if ready to execute, 0 otherwise
 */
extern int select_p_job_ready(job_record_t *job_ptr)
{
	int i, i_first, i_last;
	node_record_t *node_ptr;

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
		node_ptr = node_record_table_ptr[i];
		if (IS_NODE_POWERED_DOWN(node_ptr) ||
		    IS_NODE_POWERING_UP(node_ptr))
			return 0;
	}

	return READY_NODE_STATE;
}

extern int select_p_job_expand(job_record_t *from_job_ptr,
			       job_record_t *to_job_ptr)
{
	int rc;

	slurm_mutex_lock(&cr_mutex);
	if (cr_ptr == NULL)
		_init_node_cr();
	rc = _job_expand(from_job_ptr, to_job_ptr);
	slurm_mutex_unlock(&cr_mutex);
	return rc;
}

/*
 * Modify internal data structures for a job that has changed size
 *      Only support jobs shrinking now.
 * RET: 0 or an error code
 */
extern int select_p_job_resized(job_record_t *job_ptr, node_record_t *node_ptr)
{
	int rc = SLURM_SUCCESS;

	slurm_mutex_lock(&cr_mutex);
	if (cr_ptr == NULL)
		_init_node_cr();
	_rm_job_from_one_node(job_ptr, node_ptr, "select_p_job_resized");
	slurm_mutex_unlock(&cr_mutex);
	return rc;
}

extern int select_p_job_signal(job_record_t *job_ptr, int signal)
{
	return SLURM_SUCCESS;
}

/*
 * Note termination of job is starting. Executed from slurmctld.
 * IN job_ptr - pointer to job being terminated
 */
extern int select_p_job_fini(job_record_t *job_ptr)
{
	int rc = SLURM_SUCCESS;

	slurm_mutex_lock(&cr_mutex);
	if (cr_ptr == NULL)
		_init_node_cr();
	if (_rm_job_from_nodes(cr_ptr, job_ptr, "select_p_job_fini", true,
			       true) != SLURM_SUCCESS)
		rc = SLURM_ERROR;
	slurm_mutex_unlock(&cr_mutex);
	return rc;
}

/*
 * Suspend a job. Executed from slurmctld.
 * IN job_ptr - pointer to job being suspended
 * IN indf_susp - set if job is being suspended indefinitely by user
 *                or admin, otherwise suspended for gang scheduling
 * RET SLURM_SUCCESS or error code
 */
extern int select_p_job_suspend(job_record_t *job_ptr, bool indf_susp)
{
	int rc;

	if (!indf_susp)
		return SLURM_SUCCESS;

	slurm_mutex_lock(&cr_mutex);
	if (cr_ptr == NULL)
		_init_node_cr();
	rc = _rm_job_from_nodes(cr_ptr, job_ptr, "select_p_job_suspend", false,
				false);
	slurm_mutex_unlock(&cr_mutex);
	return rc;
}

/*
 * Resume a job. Executed from slurmctld.
 * IN job_ptr - pointer to job being resumed
 * IN indf_susp - set if job is being resumed from indefinite suspend by user
 *                or admin, otherwise resume from gang scheduling
 * RET SLURM_SUCCESS or error code
 */
extern int select_p_job_resume(job_record_t *job_ptr, bool indf_susp)
{
	int rc;

	if (!indf_susp)
		return SLURM_SUCCESS;

	slurm_mutex_lock(&cr_mutex);
	if (cr_ptr == NULL)
		_init_node_cr();
	rc = _add_job_to_nodes(cr_ptr, job_ptr, "select_p_job_resume", 0);
	slurm_mutex_unlock(&cr_mutex);
	return rc;
}

extern bitstr_t *select_p_step_pick_nodes(job_record_t *job_ptr,
					  select_jobinfo_t *jobinfo,
					  uint32_t node_count,
					  bitstr_t **avail_nodes)
{
	return NULL;
}

extern int select_p_step_start(step_record_t *step_ptr)
{
	return SLURM_SUCCESS;
}

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
	error("select_nodeinfo_unpack: error unpacking here");
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
			error("select_p_select_nodeinfo_free: "
			      "nodeinfo magic bad");
			return EINVAL;
		}
		nodeinfo->magic = 0;
		xfree(nodeinfo->tres_alloc_fmt_str);
		xfree(nodeinfo);
	}
	return SLURM_SUCCESS;
}

extern int select_p_select_nodeinfo_set_all(void)
{
	node_record_t *node_ptr = NULL;
	int n;
	static time_t last_set_all = 0;

	/* only set this once when the last_node_update is newer than
	 * the last time we set things up. */
	if (last_set_all && (last_node_update < last_set_all)) {
		debug2("Node select info for set all hasn't "
		       "changed since %ld",
		       (long)last_set_all);
		return SLURM_NO_CHANGE_IN_DATA;
	}
	last_set_all = last_node_update;

	for (n = 0; (node_ptr = next_node(&n)); n++) {
		select_nodeinfo_t *nodeinfo = NULL;
		/* We have to use the '_g_' here to make sure we get the
		 * correct data to work on.  i.e. cray calls this plugin
		 * from within select/cray which has it's own struct. */
		select_g_select_nodeinfo_get(node_ptr->select_nodeinfo,
					     SELECT_NODEDATA_PTR, 0,
					     (void *)&nodeinfo);
		if (!nodeinfo) {
			error("no nodeinfo returned from structure");
			continue;
		}

		xfree(nodeinfo->tres_alloc_fmt_str);
		if (IS_NODE_COMPLETING(node_ptr) || IS_NODE_ALLOCATED(node_ptr)) {
			nodeinfo->alloc_cpus = node_ptr->config_ptr->cpus;

			nodeinfo->tres_alloc_fmt_str =
				assoc_mgr_make_tres_str_from_array(
						node_ptr->tres_cnt,
						TRES_STR_CONVERT_UNITS, false);
			nodeinfo->tres_alloc_weighted =
				assoc_mgr_tres_weighted(
					node_ptr->tres_cnt,
					node_ptr->config_ptr->tres_weights,
					slurm_conf.priority_flags, false);
		} else {
			nodeinfo->alloc_cpus = 0;
			nodeinfo->tres_alloc_weighted = 0.0;
		}
		if (cr_ptr && cr_ptr->nodes) {
			nodeinfo->alloc_memory =
				cr_ptr->nodes[node_ptr->index].alloc_memory;
		} else {
			nodeinfo->alloc_memory = 0;
		}
	}

	return SLURM_SUCCESS;
}

extern int select_p_select_nodeinfo_set(job_record_t *job_ptr)
{
	xassert(job_ptr);

	slurm_mutex_lock(&cr_mutex);
	if (cr_ptr == NULL)
		_init_node_cr();
	slurm_mutex_unlock(&cr_mutex);

	return SLURM_SUCCESS;
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
		error("get_nodeinfo: nodeinfo not set");
		return SLURM_ERROR;
	}

	if (nodeinfo->magic != NODEINFO_MAGIC) {
		error("get_nodeinfo: nodeinfo magic bad");
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
		error("Unsupported option %d for get_nodeinfo.", dinfo);
		rc = SLURM_ERROR;
		break;
	}
	return rc;
}

/*
 * allocate storage for a select job credential
 * RET        - storage for a select job credential
 * NOTE: storage must be freed using select_p_select_jobinfo_free
 */
extern select_jobinfo_t *select_p_select_jobinfo_alloc(void)
{
	return NULL;
}

/*
 * fill in a previously allocated select job credential
 * IN/OUT jobinfo  - updated select job credential
 * IN data_type - type of data to enter into job credential
 * IN data - the data to enter into job credential
 */
extern int select_p_select_jobinfo_set(select_jobinfo_t *jobinfo,
				       enum select_jobdata_type data_type,
				       void *data)
{
	return SLURM_SUCCESS;
}

/*
 * get data from a select job credential
 * IN jobinfo  - updated select job credential
 * IN data_type - type of data to enter into job credential
 * OUT data - the data to get from job credential, caller must xfree
 *      data for data_type == SELECT_JOBDATA_PART_ID
 */
extern int select_p_select_jobinfo_get (select_jobinfo_t *jobinfo,
					enum select_jobdata_type data_type,
					void *data)
{
	return SLURM_ERROR;
}

/*
 * copy a select job credential
 * IN jobinfo - the select job credential to be copied
 * RET        - the copy or NULL on failure
 * NOTE: returned value must be freed using select_p_select_jobinfo_free
 */
extern select_jobinfo_t *select_p_select_jobinfo_copy(
	select_jobinfo_t *jobinfo)
{
	return NULL;
}

/*
 * free storage previously allocated for a select job credential
 * IN jobinfo  - the select job credential to be freed
 * RET         - slurm error code
 */
extern int select_p_select_jobinfo_free  (select_jobinfo_t *jobinfo)
{
	return SLURM_SUCCESS;
}

/*
 * pack a select job credential into a buffer in machine independent form
 * IN jobinfo  - the select job credential to be saved
 * OUT buffer  - buffer with select credential appended
 * IN protocol_version - slurm protocol version of client
 * RET         - slurm error code
 */
extern int select_p_select_jobinfo_pack(select_jobinfo_t *jobinfo,
					buf_t *buffer,
					uint16_t protocol_version)
{
	return SLURM_SUCCESS;
}

/*
 * unpack a select job credential from a buffer
 * OUT jobinfo - the select job credential read
 * IN  buffer  - buffer with select credential read from current pointer loc
 * IN protocol_version - slurm protocol version of client
 * RET         - slurm error code
 * NOTE: returned value must be freed using select_p_select_jobinfo_free
 */
extern int select_p_select_jobinfo_unpack(select_jobinfo_t **jobinfo,
					  buf_t *buffer,
					  uint16_t protocol_version)
{
	return SLURM_SUCCESS;
}

extern char *select_p_select_jobinfo_sprint(select_jobinfo_t *jobinfo,
					    char *buf, size_t size, int mode)
{
	if (buf && size) {
		buf[0] = '\0';
		return buf;
	} else
		return NULL;
}

extern char *select_p_select_jobinfo_xstrdup(select_jobinfo_t *jobinfo,
					     int mode)
{
	return NULL;
}

extern int select_p_get_info_from_plugin(enum select_plugindata_info dinfo,
					 job_record_t *job_ptr, void *data)
{
	return SLURM_SUCCESS;
}

extern int select_p_update_node_config (int index)
{
	return SLURM_SUCCESS;
}

extern int select_p_reconfigure(void)
{
	slurm_mutex_lock(&cr_mutex);
	_free_cr(cr_ptr);
	cr_ptr = NULL;
	_init_node_cr();
	slurm_mutex_unlock(&cr_mutex);

	return SLURM_SUCCESS;
}

extern bitstr_t * select_p_resv_test(resv_desc_msg_t *resv_desc_ptr,
				     uint32_t node_cnt,
				     bitstr_t *avail_bitmap,
				     bitstr_t **core_bitmap)
{
	bitstr_t **switches_bitmap;		/* nodes on this switch */
	int       *switches_cpu_cnt;		/* total CPUs on switch */
	int       *switches_node_cnt;		/* total nodes on switch */
	int       *switches_required;		/* set if has required node */

	bitstr_t  *avail_nodes_bitmap = NULL;	/* nodes on any switch */
	int rem_nodes;			/* remaining resources desired */
	int i, j;
	int best_fit_inx, first, last;
	int best_fit_nodes;
	int best_fit_location = 0;
	bool sufficient, best_fit_sufficient;

	xassert(avail_bitmap);
	xassert(resv_desc_ptr);

	if (!switch_record_cnt || !switch_record_table)
		return bit_pick_cnt(avail_bitmap, node_cnt);

	/* Use topology state information */
	if (bit_set_count(avail_bitmap) < node_cnt)
		return avail_nodes_bitmap;
	rem_nodes = node_cnt;

	/* Construct a set of switch array entries,
	 * use the same indexes as switch_record_table in slurmctld */
	switches_bitmap   = xcalloc(switch_record_cnt, sizeof(bitstr_t *));
	switches_cpu_cnt  = xcalloc(switch_record_cnt, sizeof(int));
	switches_node_cnt = xcalloc(switch_record_cnt, sizeof(int));
	switches_required = xcalloc(switch_record_cnt, sizeof(int));
	for (i=0; i<switch_record_cnt; i++) {
		switches_bitmap[i] = bit_copy(switch_record_table[i].
					      node_bitmap);
		bit_and(switches_bitmap[i], avail_bitmap);
		switches_node_cnt[i] = bit_set_count(switches_bitmap[i]);
	}

#if SELECT_DEBUG
	/* Don't compile this, it slows things down too much */
	for (i=0; i<switch_record_cnt; i++) {
		char *node_names = NULL;
		if (switches_node_cnt[i])
			node_names = bitmap2node_name(switches_bitmap[i]);
		info("switch=%s level=%d nodes=%u:%s required:%u speed=%u",
		     switch_record_table[i].name,
		     switch_record_table[i].level,
		     switches_node_cnt[i], node_names,
		     switches_required[i],
		     switch_record_table[i].link_speed);
		xfree(node_names);
	}
#endif

	/* Determine lowest level switch satisfying request with best fit */
	best_fit_inx = -1;
	for (j=0; j<switch_record_cnt; j++) {
		if (switches_node_cnt[j] < rem_nodes)
			continue;
		if ((best_fit_inx == -1) ||
		    (switch_record_table[j].level <
		     switch_record_table[best_fit_inx].level) ||
		    ((switch_record_table[j].level ==
		      switch_record_table[best_fit_inx].level) &&
		     (switches_node_cnt[j] < switches_node_cnt[best_fit_inx])))
			best_fit_inx = j;
	}
	if (best_fit_inx == -1) {
		debug("select_p_resv_test: could not find resources for "
		      "reservation");
		goto fini;
	}

	/* Identify usable leafs (within higher switch having best fit) */
	for (j=0; j<switch_record_cnt; j++) {
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
		for (j=0; j<switch_record_cnt; j++) {
			if (switches_node_cnt[j] == 0)
				continue;
			sufficient = (switches_node_cnt[j] >= rem_nodes);
			/* If first possibility OR */
			/* first set large enough for request OR */
			/* tightest fit (less resource waste) OR */
			/* nothing yet large enough, but this is biggest */
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
		first = bit_ffs(switches_bitmap[best_fit_location]);
		last  = bit_fls(switches_bitmap[best_fit_location]);
		for (i=first; ((i<=last) && (first>=0)); i++) {
			if (!bit_test(switches_bitmap[best_fit_location], i))
				continue;

			bit_clear(switches_bitmap[best_fit_location], i);
			switches_node_cnt[best_fit_location]--;

			if (bit_test(avail_nodes_bitmap, i)) {
				/* node on multiple leaf switches
				 * and already selected */
				continue;
			}

			bit_set(avail_nodes_bitmap, i);
			if (--rem_nodes <= 0)
				break;
		}
		switches_node_cnt[best_fit_location] = 0;
	}
	if (rem_nodes > 0)	/* insufficient resources */
		FREE_NULL_BITMAP(avail_nodes_bitmap);

fini:	for (i=0; i<switch_record_cnt; i++)
		FREE_NULL_BITMAP(switches_bitmap[i]);
	xfree(switches_bitmap);
	xfree(switches_cpu_cnt);
	xfree(switches_node_cnt);
	xfree(switches_required);

	return avail_nodes_bitmap;
}
