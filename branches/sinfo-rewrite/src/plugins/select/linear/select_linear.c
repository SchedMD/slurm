/*****************************************************************************\
 *  select_linear.c - node selection plugin for simple one-dimensional 
 *  address space. Selects nodes for a job so as to minimize the number 
 *  of sets of consecutive nodes using a best-fit algorithm.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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
#  if HAVE_STDINT_H
#    include <stdint.h>
#  endif
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  endif
#endif

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/node_select.h"
#include "src/common/parse_time.h"
#include "src/common/select_job_res.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_resource_info.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"

#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/proc_req.h"
#include "src/plugins/select/linear/select_linear.h"

#define SELECT_DEBUG	0
#define NO_SHARE_LIMIT	0xfffe

#define NODEINFO_MAGIC 0x82ad

/* This means we aren't linking with the slurmctld.  Since the symbols
 * are defined there just define them here so we can link to the
 * plugin.
 */
#ifndef slurmctld_conf
slurm_ctl_conf_t slurmctld_conf;
struct node_record *node_record_table_ptr;
List part_list;	
List job_list;	
int node_record_count;
time_t last_node_update;
struct switch_record *switch_record_table; 
int switch_record_cnt;
#endif

struct select_nodeinfo {
	uint16_t magic;		/* magic number */
	uint16_t alloc_cpus;
};

static int  _add_job_to_nodes(struct node_cr_record *node_cr_ptr,
			      struct job_record *job_ptr, char *pre_err, 
			      int suspended);
static void _build_select_struct(struct job_record *job_ptr, bitstr_t *bitmap);
static void _cr_job_list_del(void *x);
static int  _cr_job_list_sort(void *x, void *y);
static void _dump_node_cr(struct node_cr_record *node_cr_ptr);
static struct node_cr_record *_dup_node_cr(struct node_cr_record *node_cr_ptr);
static int  _find_job_mate(struct job_record *job_ptr, bitstr_t *bitmap,
			   uint32_t min_nodes, uint32_t max_nodes,
			   uint32_t req_nodes);
static void _free_node_cr(struct node_cr_record *node_cr_ptr);
static void _init_node_cr(void);
static int _job_count_bitmap(struct node_cr_record *node_cr_ptr,
			     struct job_record *job_ptr, 
			     bitstr_t * bitmap, bitstr_t * jobmap, 
			     int run_job_cnt, int tot_job_cnt);
static int _job_test(struct job_record *job_ptr, bitstr_t *bitmap,
		     uint32_t min_nodes, uint32_t max_nodes, 
		     uint32_t req_nodes);
static int _job_test_topo(struct job_record *job_ptr, bitstr_t *bitmap,
			  uint32_t min_nodes, uint32_t max_nodes, 
			  uint32_t req_nodes);
static int _rm_job_from_nodes(struct node_cr_record *node_cr_ptr,
			      struct job_record *job_ptr, char *pre_err, 
			      int remove_all);
static int _will_run_test(struct job_record *job_ptr, bitstr_t *bitmap,
			  uint32_t min_nodes, uint32_t max_nodes, 
			  int max_share, uint32_t req_nodes);

extern select_nodeinfo_t *select_p_select_nodeinfo_alloc(uint32_t size);
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
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum versions for their plugins as the node selection API matures.
 */
const char plugin_name[]       	= "Linear node selection plugin";
const char plugin_type[]       	= "select/linear";
const uint32_t plugin_version	= 90;

static struct node_record *select_node_ptr = NULL;
static int select_node_cnt = 0;
static uint16_t select_fast_schedule;
static uint16_t cr_type;
static bool cr_priority_test      = false;
static bool cr_priority_selection = false;

static struct node_cr_record *node_cr_ptr = NULL;
static pthread_mutex_t cr_mutex = PTHREAD_MUTEX_INITIALIZER;
static List step_cr_list = NULL;

#ifdef HAVE_XCPU
#define XCPU_POLL_TIME 120
static pthread_t xcpu_thread = 0;
static pthread_mutex_t thread_flag_mutex = PTHREAD_MUTEX_INITIALIZER;
static int agent_fini = 0;

static void *xcpu_agent(void *args)
{
	int i;
	static time_t last_xcpu_test;
	char reason[12], clone_path[128], down_node_list[512];
	struct stat buf;
	time_t now;

	last_xcpu_test = time(NULL) + XCPU_POLL_TIME;
	while (!agent_fini) {
		now = time(NULL);

		if (difftime(now, last_xcpu_test) >= XCPU_POLL_TIME) {
			debug3("Running XCPU node state test");
			down_node_list[0] = '\0';

			for (i=0; i<select_node_cnt; i++) {
				snprintf(clone_path, sizeof(clone_path), 
					"%s/%s/xcpu/clone", XCPU_DIR, 
					select_node_ptr[i].name);
				if (stat(clone_path, &buf) == 0)
					continue;
				error("stat %s: %m", clone_path);
				if ((strlen(select_node_ptr[i].name) +
				     strlen(down_node_list) + 2) <
				    sizeof(down_node_list)) {
					if (down_node_list[0] != '\0')
						strcat(down_node_list,",");
					strcat(down_node_list, 
						select_node_ptr[i].name);
				} else
					error("down_node_list overflow");
			}
			if (down_node_list[0]) {
				char time_str[32];
				slurm_make_time_str(&now, time_str, 	
					sizeof(time_str));
				snprintf(reason, sizeof(reason),
					"select_linear: Can not stat XCPU "
					"[SLURM@%s]", time_str);
				slurm_drain_nodes(down_node_list, reason);
			}
			last_xcpu_test = now;
		}

		sleep(1);
	}
	return NULL;
}

static int _init_status_pthread(void)
{
	pthread_attr_t attr;

	slurm_mutex_lock( &thread_flag_mutex );
	if ( xcpu_thread ) {
		debug2("XCPU thread already running, not starting another");
		slurm_mutex_unlock( &thread_flag_mutex );
		return SLURM_ERROR;
	}

	slurm_attr_init( &attr );
	pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
	pthread_create( &xcpu_thread, &attr, xcpu_agent, NULL);
	slurm_mutex_unlock( &thread_flag_mutex );
	slurm_attr_destroy( &attr );

	return SLURM_SUCCESS;
}

static int _fini_status_pthread(void)
{
	int i, rc = SLURM_SUCCESS;

	slurm_mutex_lock( &thread_flag_mutex );
	if ( xcpu_thread ) {
		agent_fini = 1;
		for (i=0; i<4; i++) {
			sleep(1);
			if (pthread_kill(xcpu_thread, 0)) {
				xcpu_thread = 0;
				break;
			}
		}
		if ( xcpu_thread ) {
			error("could not kill XCPU agent thread");
			rc = SLURM_ERROR;
		}
	}
	slurm_mutex_unlock( &thread_flag_mutex );
	return rc;
}
#endif

static inline bool _cr_priority_selection_enabled(void)
{
	if (!cr_priority_test) {
		char *sched_type = slurm_get_sched_type();
		if (strcmp(sched_type, "sched/gang") == 0)
			cr_priority_selection = true;
		xfree(sched_type);
		cr_priority_test = true;
	}
	return cr_priority_selection;
	
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
 * IN index - index of node's configuration information in select_node_ptr
 */
static uint16_t _get_avail_cpus(struct job_record *job_ptr, int index)
{
	struct node_record *node_ptr;
	uint16_t avail_cpus;
	uint16_t cpus, sockets, cores, threads;
	uint16_t cpus_per_task = 1;
	uint16_t ntasks_per_node = 0, ntasks_per_socket = 0, ntasks_per_core = 0;
	uint16_t max_sockets = 0xffff, max_cores = 0xffff, max_threads = 0xffff;
	multi_core_data_t *mc_ptr = NULL;
	int min_sockets = 0, min_cores = 0;

	if (job_ptr->details == NULL)
		return (uint16_t) 0;

	if (job_ptr->details->cpus_per_task)
		cpus_per_task = job_ptr->details->cpus_per_task;
	if (job_ptr->details->ntasks_per_node)
		ntasks_per_node = job_ptr->details->ntasks_per_node;
	if ((mc_ptr = job_ptr->details->mc_ptr)) {
		max_sockets       = mc_ptr->max_sockets;
		max_cores         = mc_ptr->max_cores;
		max_threads       = mc_ptr->max_threads;
		ntasks_per_socket = mc_ptr->ntasks_per_socket;
		ntasks_per_core   = mc_ptr->ntasks_per_core;
	}

	node_ptr = &(select_node_ptr[index]);
	if (select_fast_schedule) { /* don't bother checking each node */
		cpus    = node_ptr->config_ptr->cpus;
		sockets = node_ptr->config_ptr->sockets;
		cores   = node_ptr->config_ptr->cores;
		threads = node_ptr->config_ptr->threads;
	} else {
		cpus    = node_ptr->cpus;
		sockets = node_ptr->sockets;
		cores   = node_ptr->cores;
		threads = node_ptr->threads;
	}

#if 0
	info("host %s User_ sockets %u cores %u threads %u ", 
	     node_ptr->name, max_sockets, max_cores, max_threads);

	info("host %s HW_ cpus %u sockets %u cores %u threads %u ", 
	     node_ptr->name, cpus, sockets, cores, threads);
#endif

	avail_cpus = slurm_get_avail_procs(
			max_sockets, max_cores, max_threads, 
			min_sockets, min_cores, cpus_per_task,
			ntasks_per_node, ntasks_per_socket, ntasks_per_core,
	    		&cpus, &sockets, &cores, &threads, NULL, 
			SELECT_TYPE_INFO_NONE,
			job_ptr->job_id, node_ptr->name);

#if 0
	debug3("avail_cpus index %d = %d (out of %d %d %d %d)",
				index, avail_cpus,
				cpus, sockets, cores, threads);
#endif
	return(avail_cpus);
}

/* Build the full select_job_res_t structure for a job based upon the nodes
 *	allocated to it (the bitmap) and the job's memory requirement */
static void _build_select_struct(struct job_record *job_ptr, bitstr_t *bitmap)
{
	int i, j, k;
	int first_bit, last_bit;
	uint32_t node_cpus, total_cpus = 0, node_cnt;
	struct node_record *node_ptr;
	uint32_t job_memory_cpu = 0, job_memory_node = 0;
	bool memory_info = false;
	select_job_res_t select_ptr;

	if (job_ptr->details->job_min_memory  && (cr_type == CR_MEMORY)) {
		if (job_ptr->details->job_min_memory & MEM_PER_CPU) {
			job_memory_cpu = job_ptr->details->job_min_memory &
					 (~MEM_PER_CPU);
			memory_info = true;
		} else {
			job_memory_node = job_ptr->details->job_min_memory;
			memory_info = true;
		}
	}

	if (job_ptr->select_job) {
		/* Due to job requeue */
		free_select_job_res(&job_ptr->select_job);
	}

	node_cnt = bit_set_count(bitmap);
	job_ptr->select_job = select_ptr = create_select_job_res();
	select_ptr->cpu_array_reps = xmalloc(sizeof(uint32_t) * node_cnt);
	select_ptr->cpu_array_value = xmalloc(sizeof(uint16_t) * node_cnt);
	select_ptr->cpus = xmalloc(sizeof(uint16_t) * node_cnt);
	select_ptr->cpus_used = xmalloc(sizeof(uint16_t) * node_cnt);
	select_ptr->memory_allocated = xmalloc(sizeof(uint32_t) * node_cnt);
	select_ptr->memory_used = xmalloc(sizeof(uint32_t) * node_cnt);
	select_ptr->nhosts = node_cnt;
	select_ptr->node_bitmap = bit_copy(bitmap);
	if (select_ptr->node_bitmap == NULL)
		fatal("bit_copy malloc failure");
	select_ptr->nprocs = job_ptr->total_procs;
	if (build_select_job_res(select_ptr, (void *)select_node_ptr,
				 select_fast_schedule))
		error("_build_select_struct: build_select_job_res: %m");

	first_bit = bit_ffs(bitmap);
	last_bit  = bit_fls(bitmap);
	for (i=first_bit, j=0, k=-1; ((i<=last_bit) && (first_bit>=0)); i++) {
		if (!bit_test(bitmap, i))
			continue;
		node_ptr = &(select_node_ptr[i]);
		if (select_fast_schedule)
			node_cpus = node_ptr->config_ptr->cpus;
		else
			node_cpus = node_ptr->cpus;
		select_ptr->cpus[j] = node_cpus;
		if ((k == -1) || 
		    (select_ptr->cpu_array_value[k] != node_cpus)) {
			select_ptr->cpu_array_cnt++;
			select_ptr->cpu_array_reps[++k] = 1;
			select_ptr->cpu_array_value[k] = node_cpus;
		} else
			select_ptr->cpu_array_reps[k]++;
		total_cpus += node_cpus;

		if (!memory_info)
			;
		else if (job_memory_node)
			select_ptr->memory_allocated[j] = job_memory_node;
		else if (job_memory_cpu) {
			select_ptr->memory_allocated[j] = 
					job_memory_cpu * node_cpus;
		}

		if (set_select_job_res_node(select_ptr, j)) {
			error("_build_select_struct: set_select_job_res_node: "
			      "%m");
		}
		j++;
	}
	if (select_ptr->nprocs != total_cpus) {
		error("_build_select_struct: nprocs mismatch %u != %u",
		      select_ptr->nprocs, total_cpus);
	}
}

/*
 * Set the bits in 'jobmap' that correspond to bits in the 'bitmap'
 * that are running 'run_job_cnt' jobs or less, and clear the rest.
 */
static int _job_count_bitmap(struct node_cr_record *node_cr_ptr,
			     struct job_record *job_ptr, 
			     bitstr_t * bitmap, bitstr_t * jobmap, 
			     int run_job_cnt, int tot_job_cnt)
{
	int i, count = 0, total_jobs, total_run_jobs;
	struct part_cr_record *part_cr_ptr;
	uint32_t job_memory_cpu = 0, job_memory_node = 0;
	bool exclusive;

	xassert(node_cr_ptr);

	/* Jobs submitted to a partition with 
	 * Shared=FORCE:1 may share resources with jobs in other partitions
	 * Shared=NO  may not share resources with jobs in other partitions */
	if (run_job_cnt || (job_ptr->part_ptr->max_share & SHARED_FORCE))
		exclusive = false;
	else
		exclusive = true;

	if (job_ptr->details->job_min_memory  && (cr_type == CR_MEMORY)) {
		if (job_ptr->details->job_min_memory & MEM_PER_CPU) {
			job_memory_cpu = job_ptr->details->job_min_memory &
					 (~MEM_PER_CPU);
		} else
			job_memory_node = job_ptr->details->job_min_memory;
	}

	for (i = 0; i < node_record_count; i++) {
		if (!bit_test(bitmap, i)) {
			bit_clear(jobmap, i);
			continue;
		}
		if (job_memory_cpu || job_memory_node) {
			uint32_t alloc_mem, job_mem, avail_mem;
			alloc_mem = node_cr_ptr[i].alloc_memory;
			if (select_fast_schedule) {
				avail_mem = node_record_table_ptr[i].
					    config_ptr->real_memory;
				if (job_memory_cpu) {
					job_mem = job_memory_cpu *
						  node_record_table_ptr[i].
						  config_ptr->cpus;
				} else
					job_mem = job_memory_node;
			} else {
				avail_mem = node_record_table_ptr[i].
					    real_memory;
				if (job_memory_cpu) {
					job_mem = job_memory_cpu *
						  node_record_table_ptr[i].
						  cpus;
				} else
					job_mem = job_memory_node;
			}
			if ((alloc_mem + job_mem) >avail_mem) {
				bit_clear(jobmap, i);
				continue;
			}
		}

		if ((run_job_cnt != NO_SHARE_LIMIT) &&
		    (!_cr_priority_selection_enabled()) &&
		    (node_cr_ptr[i].exclusive_jobid != 0)) {
			/* already reserved by some exclusive job */
			bit_clear(jobmap, i);
			continue;
		}

		if (_cr_priority_selection_enabled()) {
			/* clear this node if any higher-priority
			 * partitions have existing allocations */
			total_jobs = 0;
			part_cr_ptr = node_cr_ptr[i].parts;
			for( ;part_cr_ptr; part_cr_ptr = part_cr_ptr->next) {
				if (part_cr_ptr->part_ptr->priority <=
				    job_ptr->part_ptr->priority)
					continue;
				total_jobs += part_cr_ptr->tot_job_cnt;
			}
			if ((run_job_cnt != NO_SHARE_LIMIT) &&
			    (total_jobs > 0)) {
				bit_clear(jobmap, i);
				continue;
			}
			/* if not sharing, then check with other partitions
			 * of equal priority. Otherwise, load-balance within
			 * the local partition */
			total_jobs = 0;
			total_run_jobs = 0;
			part_cr_ptr = node_cr_ptr[i].parts;
			for( ; part_cr_ptr; part_cr_ptr = part_cr_ptr->next) {
				if (part_cr_ptr->part_ptr->priority !=
				    job_ptr->part_ptr->priority)
					continue;
				if (!job_ptr->details->shared) {
					total_run_jobs +=
						      part_cr_ptr->run_job_cnt;
					total_jobs += part_cr_ptr->tot_job_cnt;
					continue;
				}
				if (part_cr_ptr->part_ptr == job_ptr->part_ptr){
					total_run_jobs +=
						      part_cr_ptr->run_job_cnt;
					total_jobs += part_cr_ptr->tot_job_cnt;
					break;
				}
			}
			if ((total_run_jobs <= run_job_cnt) &&
			    (total_jobs     <= tot_job_cnt)) {
				bit_set(jobmap, i);
				count++;
			} else {
				bit_clear(jobmap, i);
			}
			continue;
		}

		total_jobs = 0;
		total_run_jobs = 0;
		part_cr_ptr = node_cr_ptr[i].parts;
		while (part_cr_ptr) {
			if (exclusive) {      /* count jobs in all partitions */
				total_run_jobs += part_cr_ptr->run_job_cnt;
				total_jobs     += part_cr_ptr->tot_job_cnt;
			} else if (part_cr_ptr->part_ptr == job_ptr->part_ptr) {
				total_run_jobs += part_cr_ptr->run_job_cnt;
				total_jobs     += part_cr_ptr->tot_job_cnt; 
				break;
			}
			part_cr_ptr = part_cr_ptr->next;
		}
		if ((run_job_cnt != 0) && (part_cr_ptr == NULL)) {
			error("_job_count_bitmap: could not find "
				"partition %s for node %s",
				job_ptr->part_ptr->name,
				node_record_table_ptr[i].name);
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
static int _find_job_mate(struct job_record *job_ptr, bitstr_t *bitmap,
			  uint32_t min_nodes, uint32_t max_nodes,
			  uint32_t req_nodes)
{
	ListIterator job_iterator;
	struct job_record *job_scan_ptr;
	int rc = EINVAL;

	job_iterator = list_iterator_create(job_list);
	while ((job_scan_ptr = (struct job_record *) list_next(job_iterator))) {
		if ((!IS_JOB_RUNNING(job_scan_ptr))			||
		    (job_scan_ptr->node_cnt   != req_nodes)		||
		    (job_scan_ptr->total_procs < job_ptr->num_procs)	||
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
		    (bit_overlap(job_ptr->details->exc_node_bitmap,
				 job_scan_ptr->node_bitmap) != 0))
			continue;	/* Excluded nodes in this job */

		bit_and(bitmap, job_scan_ptr->node_bitmap);
		job_ptr->total_procs = job_scan_ptr->total_procs;
		rc = SLURM_SUCCESS;
		break;
	}
	list_iterator_destroy(job_iterator);
	return rc;
}

/* _job_test - does most of the real work for select_p_job_test(), which 
 *	pretty much just handles load-leveling and max_share logic */
static int _job_test(struct job_record *job_ptr, bitstr_t *bitmap,
		     uint32_t min_nodes, uint32_t max_nodes, 
		     uint32_t req_nodes)
{
	int i, index, error_code = EINVAL, sufficient;
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
	int avail_cpus, alloc_cpus = 0;

	if (bit_set_count(bitmap) < min_nodes)
		return error_code;

	if ((job_ptr->details->req_node_bitmap) &&
	    (!bit_super_set(job_ptr->details->req_node_bitmap, bitmap)))
		return error_code;

	if (switch_record_cnt && switch_record_table) {
		/* Perform optimized resource selection based upon topology */
		return _job_test_topo(job_ptr, bitmap, 
				      min_nodes, max_nodes, req_nodes);
	}

	consec_index = 0;
	consec_size  = 50;	/* start allocation for 50 sets of 
				 * consecutive nodes */
	consec_cpus  = xmalloc(sizeof(int) * consec_size);
	consec_nodes = xmalloc(sizeof(int) * consec_size);
	consec_start = xmalloc(sizeof(int) * consec_size);
	consec_end   = xmalloc(sizeof(int) * consec_size);
	consec_req   = xmalloc(sizeof(int) * consec_size);


	/* Build table with information about sets of consecutive nodes */
	consec_cpus[consec_index] = consec_nodes[consec_index] = 0;
	consec_req[consec_index] = -1;	/* no required nodes here by default */
	rem_cpus = job_ptr->num_procs;
	if (req_nodes > min_nodes)
		rem_nodes = req_nodes;
	else
		rem_nodes = min_nodes;

	for (index = 0; index < select_node_cnt; index++) {
		if (bit_test(bitmap, index)) {
			if (consec_nodes[consec_index] == 0)
				consec_start[consec_index] = index;

			avail_cpus = _get_avail_cpus(job_ptr, index);

			if (job_ptr->details->req_node_bitmap
			&&  bit_test(job_ptr->details->req_node_bitmap, index)
			&&  (max_nodes > 0)) {
				if (consec_req[consec_index] == -1) {
					/* first required node in set */
					consec_req[consec_index] = index;
				}
				rem_cpus   -= avail_cpus;
				alloc_cpus += avail_cpus;
				rem_nodes--;
				max_nodes--;
			} else {	 /* node not required (yet) */
				bit_clear(bitmap, index); 
				consec_cpus[consec_index] += avail_cpus;
				consec_nodes[consec_index]++;
			}
		} else if (consec_nodes[consec_index] == 0) {
			consec_req[consec_index] = -1;
			/* already picked up any required nodes */
			/* re-use this record */
		} else {
			consec_end[consec_index] = index - 1;
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
		consec_end[consec_index++] = index - 1;

#if SELECT_DEBUG
	/* don't compile this, it slows things down too much */
	debug3("rem_cpus=%d, rem_nodes=%d", rem_cpus, rem_nodes);
	for (i = 0; i < consec_index; i++) {
		if (consec_req[i] != -1)
			debug3
			    ("start=%s, end=%s, nodes=%d, cpus=%d, req=%s",
			     select_node_ptr[consec_start[i]].name,
			     select_node_ptr[consec_end[i]].name,
			     consec_nodes[i], consec_cpus[i],
			     select_node_ptr[consec_req[i]].name);
		else
			debug3("start=%s, end=%s, nodes=%d, cpus=%d",
			       select_node_ptr[consec_start[i]].name,
			       select_node_ptr[consec_end[i]].name,
			       consec_nodes[i], consec_cpus[i]);
	}
#endif

	/* accumulate nodes from these sets of consecutive nodes until */
	/*   sufficient resources have been accumulated */
	while (consec_index && (max_nodes > 0)) {
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
			    ((best_fit_req == -1) && (consec_req[i] != -1)) ||
			    (sufficient && (best_fit_sufficient == 0)) ||
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
				if ((max_nodes <= 0)
				||  ((rem_nodes <= 0) && (rem_cpus <= 0)))
					break;
				if (bit_test(bitmap, i))
					continue;
				bit_set(bitmap, i);
				rem_nodes--;
				max_nodes--;
				avail_cpus = _get_avail_cpus(job_ptr, i);
				rem_cpus   -= avail_cpus;
				alloc_cpus += avail_cpus;
			}
			for (i = (best_fit_req - 1);
			     i >= consec_start[best_fit_location]; i--) {
				if ((max_nodes <= 0)
				||  ((rem_nodes <= 0) && (rem_cpus <= 0)))
					break;
				if (bit_test(bitmap, i)) 
					continue;
				bit_set(bitmap, i);
				rem_nodes--;
				max_nodes--;
				avail_cpus = _get_avail_cpus(job_ptr, i);
				rem_cpus   -= avail_cpus;
				alloc_cpus += avail_cpus;
			}
		} else {
			for (i = consec_start[best_fit_location];
			     i <= consec_end[best_fit_location]; i++) {
				if ((max_nodes <= 0)
				||  ((rem_nodes <= 0) && (rem_cpus <= 0)))
					break;
				if (bit_test(bitmap, i))
					continue;
				bit_set(bitmap, i);
				rem_nodes--;
				max_nodes--;
				avail_cpus = _get_avail_cpus(job_ptr, i);
				rem_cpus   -= avail_cpus;
				alloc_cpus += avail_cpus;
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

	if (error_code && (rem_cpus <= 0)
	&&  _enough_nodes(0, rem_nodes, min_nodes, req_nodes)) {
		error_code = SLURM_SUCCESS;
	}
	if (error_code == SLURM_SUCCESS) {
		/* job's total_procs is needed for SELECT_MODE_WILL_RUN */
		job_ptr->total_procs = alloc_cpus;
	}

	xfree(consec_cpus);
	xfree(consec_nodes);
	xfree(consec_start);
	xfree(consec_end);
	xfree(consec_req);
	return error_code;
}

/*
 * _job_test_topo - A topology aware version of _job_test()
 * NOTE: The logic here is almost identical to that of _eval_nodes_topo() in
 *       select/cons_res/job_test.c. Any bug found here is probably also there.
 */
static int _job_test_topo(struct job_record *job_ptr, bitstr_t *bitmap,
			  uint32_t min_nodes, uint32_t max_nodes, 
			  uint32_t req_nodes)
{
	bitstr_t **switches_bitmap;		/* nodes on this switch */
	int       *switches_cpu_cnt;		/* total CPUs on switch */
	int       *switches_node_cnt;		/* total nodes on switch */
	int       *switches_required;		/* set if has required node */

	bitstr_t  *avail_nodes_bitmap = NULL;	/* nodes on any switch */
	bitstr_t  *req_nodes_bitmap   = NULL;
	int rem_cpus, rem_nodes;	/* remaining resources desired */
	int avail_cpus, alloc_cpus = 0;
	int i, j, rc = SLURM_SUCCESS;
	int best_fit_inx, first, last;
	int best_fit_nodes, best_fit_cpus;
	int best_fit_location = 0, best_fit_sufficient;
	bool sufficient;

	rem_cpus = job_ptr->num_procs;
	if (req_nodes > min_nodes)
		rem_nodes = req_nodes;
	else
		rem_nodes = min_nodes;

	if (job_ptr->details->req_node_bitmap) {
		req_nodes_bitmap = bit_copy(job_ptr->details->req_node_bitmap);
		i = bit_set_count(req_nodes_bitmap);
		if (i > max_nodes) {
			info("job %u requires more nodes than currently "
			     "available (%u>%u)",
			     job_ptr->job_id, i, max_nodes);
			rc = EINVAL;
			goto fini;
		}
	}

	/* Construct a set of switch array entries, 
	 * use the same indexes as switch_record_table in slurmctld */
	switches_bitmap   = xmalloc(sizeof(bitstr_t *) * switch_record_cnt);
	switches_cpu_cnt  = xmalloc(sizeof(int)        * switch_record_cnt);
	switches_node_cnt = xmalloc(sizeof(int)        * switch_record_cnt);
	switches_required = xmalloc(sizeof(int)        * switch_record_cnt);
	avail_nodes_bitmap = bit_alloc(node_record_count);
	for (i=0; i<switch_record_cnt; i++) {
		switches_bitmap[i] = bit_copy(switch_record_table[i].
					      node_bitmap);
		bit_and(switches_bitmap[i], bitmap);
		bit_or(avail_nodes_bitmap, switches_bitmap[i]);
		switches_node_cnt[i] = bit_set_count(switches_bitmap[i]);
		if (req_nodes_bitmap &&
		    bit_overlap(req_nodes_bitmap, switches_bitmap[i])) {
			switches_required[i] = 1;
		}
	}
	bit_nclear(bitmap, 0, node_record_count - 1);

#if SELECT_DEBUG
	/* Don't compile this, it slows things down too much */
	for (i=0; i<switch_record_cnt; i++) {
		char *node_names = NULL;
		if (switches_node_cnt[i])
			node_names = bitmap2node_name(switches_bitmap[i]);
		debug("switch=%s nodes=%u:%s required:%u speed=%u",
		      switch_record_table[i].name,
		      switches_node_cnt[i], node_names,
		      switches_required[i],
		      switch_record_table[i].link_speed);
		xfree(node_names);
	}
#endif

	if (req_nodes_bitmap &&
	    (!bit_super_set(req_nodes_bitmap, avail_nodes_bitmap))) {
		info("job %u requires nodes not available on any switch",
		     job_ptr->job_id);
		rc = EINVAL;
		goto fini;
	}

	if (req_nodes_bitmap) {
		/* Accumulate specific required resources, if any */
		first = bit_ffs(req_nodes_bitmap);
		last  = bit_fls(req_nodes_bitmap);
		for (i=first; ((i<=last) && (first>=0)); i++) {
			if (!bit_test(req_nodes_bitmap, i))
				continue;
			if (max_nodes <= 0) {
				info("job %u requires nodes than allowed",
				     job_ptr->job_id);
				rc = EINVAL;
				goto fini;
			}
			bit_set(bitmap, i);
			bit_clear(avail_nodes_bitmap, i);
			rem_nodes--;
			max_nodes--;
			avail_cpus = _get_avail_cpus(job_ptr, i);
			rem_cpus   -= avail_cpus;
			alloc_cpus += avail_cpus;
			for (j=0; j<switch_record_cnt; j++) {
				if (!bit_test(switches_bitmap[j], i))
					continue;
				bit_clear(switches_bitmap[j], i);
				switches_node_cnt[j]--;
			}
		}
		if ((rem_nodes <= 0) && (rem_cpus <= 0))
			goto fini;

		/* Accumulate additional resources from leafs that
		 * contain required nodes */
		for (j=0; j<switch_record_cnt; j++) {
			if ((switch_record_table[j].level != 0) ||
			    (switches_node_cnt[j] == 0) ||
			    (switches_required[j] == 0)) {
				continue;
			}
			while ((max_nodes > 0) &&
			       ((rem_nodes > 0) || (rem_cpus > 0))) {
				i = bit_ffs(switches_bitmap[j]);
				if (i == -1)
					break;
				bit_clear(switches_bitmap[j], i);
				switches_node_cnt[j]--;
				if (bit_test(bitmap, i)) {
					/* node on multiple leaf switches
					 * and already selected */
					continue;
				}
				bit_set(bitmap, i);
				bit_clear(avail_nodes_bitmap, i);
				rem_nodes--;
				max_nodes--;
				avail_cpus = _get_avail_cpus(job_ptr, i);
				rem_cpus   -= avail_cpus;
				alloc_cpus += avail_cpus;
			}
		}
		if ((rem_nodes <= 0) && (rem_cpus <= 0))
			goto fini;

		/* Update bitmaps and node counts for higher-level switches */
		for (j=0; j<switch_record_cnt; j++) {
			if (switches_node_cnt[j] == 0)
				continue;
			first = bit_ffs(switches_bitmap[j]);
			if (first < 0)
				continue;
			last  = bit_fls(switches_bitmap[j]);
			for (i=first; i<=last; i++) {
				if (!bit_test(switches_bitmap[j], i))
					continue;
				if (!bit_test(avail_nodes_bitmap, i)) {
					/* cleared from lower level */
					bit_clear(switches_bitmap[j], i);
					switches_node_cnt[j]--;
				} else {
					switches_cpu_cnt[j] += 
						_get_avail_cpus(job_ptr, i);
				}
			}
		}
	} else {
		/* No specific required nodes, calculate CPU counts */
		for (j=0; j<switch_record_cnt; j++) {
			first = bit_ffs(switches_bitmap[j]);
			if (first < 0)
				continue;
			last  = bit_fls(switches_bitmap[j]);
			for (i=first; i<=last; i++) {
				if (!bit_test(switches_bitmap[j], i))
					continue;
				switches_cpu_cnt[j] += 
					_get_avail_cpus(job_ptr, i);
			}
		}
	}

	/* Determine lowest level switch satifying request with best fit */
	best_fit_inx = -1;
	for (j=0; j<switch_record_cnt; j++) {
		if ((switches_cpu_cnt[j]  < rem_cpus) ||
		    (!_enough_nodes(switches_node_cnt[j], rem_nodes,
				    min_nodes, req_nodes)))
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
		error("job %u: best_fit topology failure", job_ptr->job_id);
		rc = EINVAL;
		goto fini;
	}
	bit_and(avail_nodes_bitmap, switches_bitmap[best_fit_inx]);

	/* Identify usable leafs (within higher switch having best fit) */
	for (j=0; j<switch_record_cnt; j++) {
		if ((switch_record_table[j].level != 0) ||
		    (!bit_super_set(switches_bitmap[j], 
				    switches_bitmap[best_fit_inx]))) {
			switches_node_cnt[j] = 0;
		}
	}

	/* Select resources from these leafs on a best-fit basis */
	while ((max_nodes > 0) && ((rem_nodes > 0) || (rem_cpus > 0))) {
		best_fit_cpus = best_fit_nodes = best_fit_sufficient = 0;
		for (j=0; j<switch_record_cnt; j++) {
			if (switches_node_cnt[j] == 0)
				continue;
			sufficient = (switches_cpu_cnt[j] >= rem_cpus) &&
				     _enough_nodes(switches_node_cnt[j], 
						   rem_nodes, min_nodes, 
						   req_nodes);
			/* If first possibility OR */
			/* first set large enough for request OR */
			/* tightest fit (less resource waste) OR */
			/* nothing yet large enough, but this is biggest */
			if ((best_fit_nodes == 0) ||	
			    (sufficient && (best_fit_sufficient == 0)) ||
			    (sufficient && 
			     (switches_cpu_cnt[j] < best_fit_cpus)) ||
			    ((sufficient == 0) && 
			     (switches_cpu_cnt[j] > best_fit_cpus))) {
				best_fit_cpus =  switches_cpu_cnt[j];
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
			avail_cpus = _get_avail_cpus(job_ptr, i);
			switches_cpu_cnt[best_fit_location] -= avail_cpus;

			if (bit_test(bitmap, i)) {
				/* node on multiple leaf switches
				 * and already selected */
				continue;
			}

			bit_set(bitmap, i);
			rem_nodes--;
			max_nodes--;
			rem_cpus   -= avail_cpus;
			alloc_cpus += avail_cpus;
			if ((max_nodes <= 0) || 
			    ((rem_nodes <= 0) && (rem_cpus <= 0)))
				break;
		}
		switches_node_cnt[best_fit_location] = 0;
	}
	if ((rem_cpus <= 0) && 
	    _enough_nodes(0, rem_nodes, min_nodes, req_nodes)) {
		rc = SLURM_SUCCESS;
	} else
		rc = EINVAL;

 fini:	if (rc == SLURM_SUCCESS) {
		/* Job's total_procs is needed for SELECT_MODE_WILL_RUN */
		job_ptr->total_procs = alloc_cpus;
	}
	FREE_NULL_BITMAP(avail_nodes_bitmap);
	FREE_NULL_BITMAP(req_nodes_bitmap);
	for (i=0; i<switch_record_cnt; i++)
		bit_free(switches_bitmap[i]);
	xfree(switches_bitmap);
	xfree(switches_cpu_cnt);
	xfree(switches_node_cnt);
	xfree(switches_required);

	return rc;
}


/*
 * deallocate resources that were assigned to this job 
 *
 * if remove_all = 0: the job has been suspended, so just deallocate CPUs
 * if remove_all = 1: deallocate all resources
 */
static int _rm_job_from_nodes(struct node_cr_record *node_cr_ptr,
			      struct job_record *job_ptr, char *pre_err, 
			      int remove_all)
{
	int i, i_first, i_last, rc = SLURM_SUCCESS;
	struct part_cr_record *part_cr_ptr;
	select_job_res_t select_ptr;
	uint32_t job_memory, job_memory_cpu = 0, job_memory_node = 0;

	if (node_cr_ptr == NULL) {
		error("%s: node_cr_ptr not initialized", pre_err);
		return SLURM_ERROR;
	}

	if (remove_all && job_ptr->details && 
	    job_ptr->details->job_min_memory && (cr_type == CR_MEMORY)) {
		if (job_ptr->details->job_min_memory & MEM_PER_CPU) {
			job_memory_cpu = job_ptr->details->job_min_memory &
					 (~MEM_PER_CPU);
		} else
			job_memory_node = job_ptr->details->job_min_memory;
	}

	if ((select_ptr = job_ptr->select_job) == NULL) {
		error("job %u lacks a select_job_res struct",
		      job_ptr->job_id);
		return SLURM_ERROR;
	}
	i_first = bit_ffs(select_ptr->node_bitmap);
	i_last  = bit_fls(select_ptr->node_bitmap);
	if (i_first < 0) {
		error("job %u allocated nodes which have been removed "
		      "from slurm.conf", job_ptr->job_id);
		return SLURM_ERROR;
	}
	for (i = i_first; i <= i_last; i++) {
		if (bit_test(select_ptr->node_bitmap, i) == 0)
			continue;
		if (job_memory_cpu == 0)
			job_memory = job_memory_node;
		else if (select_fast_schedule) {
			job_memory = job_memory_cpu *
				     node_record_table_ptr[i].
				     config_ptr->cpus;
		} else {
			job_memory = job_memory_cpu *
				     node_record_table_ptr[i].cpus;
		}
		if (node_cr_ptr[i].alloc_memory >= job_memory)
			node_cr_ptr[i].alloc_memory -= job_memory;
		else {
			node_cr_ptr[i].alloc_memory = 0;
			error("%s: memory underflow for node %s",
				pre_err, node_record_table_ptr[i].name);
		}
		if (node_cr_ptr[i].exclusive_jobid == job_ptr->job_id)
			node_cr_ptr[i].exclusive_jobid = 0;
		part_cr_ptr = node_cr_ptr[i].parts;
		while (part_cr_ptr) {
			if (part_cr_ptr->part_ptr != job_ptr->part_ptr) {
				part_cr_ptr = part_cr_ptr->next;
				continue;
			}
			if (part_cr_ptr->run_job_cnt > 0)
				part_cr_ptr->run_job_cnt--;
			else {
				error("%s: run_job_cnt underflow for node %s",
					pre_err, node_record_table_ptr[i].name);
			}
			if (remove_all) {
				if (part_cr_ptr->tot_job_cnt > 0)
					part_cr_ptr->tot_job_cnt--;
				else {
					error("%s: tot_job_cnt underflow "
						"for node %s", pre_err,
						node_record_table_ptr[i].name);
				}
				if ((part_cr_ptr->tot_job_cnt == 0) &&
				    (part_cr_ptr->run_job_cnt)) {
					part_cr_ptr->run_job_cnt = 0;
					error("%s: run_job_count out of sync "
						"for node %s", pre_err,
						node_record_table_ptr[i].name);
				}
			}
			break;
		}
		if (part_cr_ptr == NULL) {
			if(job_ptr->part_ptr)
				error("%s: could not find partition "
				      "%s for node %s",
				      pre_err, job_ptr->part_ptr->name,
				      node_record_table_ptr[i].name);
			else
				error("%s: no partition ptr given for "
				      "job %u and node %s",
				      pre_err, job_ptr->job_id,
				      node_record_table_ptr[i].name);
				
			rc = SLURM_ERROR;
		}
	}

	return rc;
}

/*
 * allocate resources to the given job
 *
 * if alloc_all = 0: the job has been suspended, so just re-allocate CPUs
 * if alloc_all = 1: allocate all resources (CPUs and memory)
 */
static int _add_job_to_nodes(struct node_cr_record *node_cr_ptr,
			     struct job_record *job_ptr, char *pre_err, 
			     int alloc_all)
{
	int i, i_first, i_last, rc = SLURM_SUCCESS, exclusive = 0;
	struct part_cr_record *part_cr_ptr;
	select_job_res_t select_ptr;
	uint32_t job_memory_cpu = 0, job_memory_node = 0;

	if (node_cr_ptr == NULL) {
		error("%s: node_cr_ptr not initialized", pre_err);
		return SLURM_ERROR;
	}

	if (alloc_all && job_ptr->details && 
	    job_ptr->details->job_min_memory && (cr_type == CR_MEMORY)) {
		if (job_ptr->details->job_min_memory & MEM_PER_CPU) {
			job_memory_cpu = job_ptr->details->job_min_memory &
					 (~MEM_PER_CPU);
		} else
			job_memory_node = job_ptr->details->job_min_memory;
	}

	if (job_ptr->details->shared == 0)
		exclusive = 1;

	if ((select_ptr = job_ptr->select_job) == NULL) {
		error("job %u lacks a select_job_res struct",
		      job_ptr->job_id);
		return SLURM_ERROR;
	}
	i_first = bit_ffs(select_ptr->node_bitmap);
	i_last  = bit_fls(select_ptr->node_bitmap);
	for (i=i_first; ((i<=i_last) && (i_first>=0)); i++) {
		if (bit_test(select_ptr->node_bitmap, i) == 0)
			continue;
		if (job_memory_cpu == 0)
			node_cr_ptr[i].alloc_memory += job_memory_node;
		else if (select_fast_schedule) {
			node_cr_ptr[i].alloc_memory += 
					job_memory_cpu *
					node_record_table_ptr[i].
					config_ptr->cpus;
		} else {
			node_cr_ptr[i].alloc_memory += 
					job_memory_cpu *
					node_record_table_ptr[i].cpus;
		}
		if (exclusive) {
			if (node_cr_ptr[i].exclusive_jobid) {
				error("select/linear: conflicting exclusive "
				      "jobs %u and %u on %s",
				      job_ptr->job_id, 
				      node_cr_ptr[i].exclusive_jobid,
				      node_record_table_ptr[i].name);
			}
			node_cr_ptr[i].exclusive_jobid = job_ptr->job_id;
		}

		part_cr_ptr = node_cr_ptr[i].parts;
		while (part_cr_ptr) {
			if (part_cr_ptr->part_ptr != job_ptr->part_ptr) {
				part_cr_ptr = part_cr_ptr->next;
				continue;
			}
			if (alloc_all)
				part_cr_ptr->tot_job_cnt++;
			part_cr_ptr->run_job_cnt++;
			break;
		}
		if (part_cr_ptr == NULL) {
			error("%s: could not find partition %s for node %s",
				pre_err, job_ptr->part_ptr->name,
				node_record_table_ptr[i].name);
			rc = SLURM_ERROR;
		}
	}

	return rc;
}

static void _free_node_cr(struct node_cr_record *node_cr_ptr)
{
	int i;
	struct part_cr_record *part_cr_ptr1, *part_cr_ptr2;

	if (node_cr_ptr == NULL)
		return;

	for (i = 0; i < select_node_cnt; i++) {
		part_cr_ptr1 = node_cr_ptr[i].parts;
		while (part_cr_ptr1) {
			part_cr_ptr2 = part_cr_ptr1->next;
			xfree(part_cr_ptr1);
			part_cr_ptr1 = part_cr_ptr2;
		}
	}
	xfree(node_cr_ptr);
}

static inline void _dump_node_cr(struct node_cr_record *node_cr_ptr)
{
#if SELECT_DEBUG
	int i;
	struct part_cr_record *part_cr_ptr;

	if (node_cr_ptr == NULL)
		return;

	for (i = 0; i < select_node_cnt; i++) {
		info("Node:%s exclusive:%u alloc_mem:%u", 
			node_record_table_ptr[i].name,
			node_cr_ptr[i].exclusive_jobid,
			node_cr_ptr[i].alloc_memory);

		part_cr_ptr = node_cr_ptr[i].parts;
		while (part_cr_ptr) {
			info("  Part:%s run:%u tot:%u", 
				part_cr_ptr->part_ptr->name,
				part_cr_ptr->run_job_cnt,
				part_cr_ptr->tot_job_cnt);
			part_cr_ptr = part_cr_ptr->next;
		}
	}
#endif
}

static struct node_cr_record *_dup_node_cr(struct node_cr_record *node_cr_ptr)
{
	int i;
	struct node_cr_record *new_node_cr_ptr;
	struct part_cr_record *part_cr_ptr, *new_part_cr_ptr;

	if (node_cr_ptr == NULL)
		return NULL;

	new_node_cr_ptr = xmalloc(select_node_cnt * 
				  sizeof(struct node_cr_record));

	for (i = 0; i < select_node_cnt; i++) {
		new_node_cr_ptr[i].alloc_memory = node_cr_ptr[i].alloc_memory;
		new_node_cr_ptr[i].exclusive_jobid = 
				node_cr_ptr[i].exclusive_jobid;
		part_cr_ptr = node_cr_ptr[i].parts;
		while (part_cr_ptr) {
			new_part_cr_ptr = xmalloc(sizeof(struct part_cr_record));
			new_part_cr_ptr->part_ptr    = part_cr_ptr->part_ptr;
			new_part_cr_ptr->run_job_cnt = part_cr_ptr->run_job_cnt;
			new_part_cr_ptr->tot_job_cnt = part_cr_ptr->tot_job_cnt;
			new_part_cr_ptr->next 	     = new_node_cr_ptr[i].parts;
			new_node_cr_ptr[i].parts     = new_part_cr_ptr;
			part_cr_ptr = part_cr_ptr->next;
		}
	}
	return new_node_cr_ptr;
}

static void _init_node_cr(void)
{
	struct part_record *part_ptr;
	struct part_cr_record *part_cr_ptr;
	select_job_res_t select_ptr;
	ListIterator part_iterator;
	struct job_record *job_ptr;
	ListIterator job_iterator;
	uint32_t job_memory_cpu, job_memory_node;
	int exclusive, i, i_first, i_last;

	if (node_cr_ptr)
		return;

	node_cr_ptr = xmalloc(select_node_cnt * sizeof(struct node_cr_record));

	/* build partition records */
	part_iterator = list_iterator_create(part_list);
	while ((part_ptr = (struct part_record *) list_next(part_iterator))) {
		for (i = 0; i < select_node_cnt; i++) {
			if (part_ptr->node_bitmap == NULL)
				break;
			if (!bit_test(part_ptr->node_bitmap, i))
				continue;
			part_cr_ptr = xmalloc(sizeof(struct part_cr_record));
			part_cr_ptr->next = node_cr_ptr[i].parts;
			part_cr_ptr->part_ptr = part_ptr;
			node_cr_ptr[i].parts = part_cr_ptr;
		}
		
	}
	list_iterator_destroy(part_iterator);

	/* record running and suspended jobs in node_cr_records */
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (!IS_JOB_RUNNING(job_ptr) && !IS_JOB_SUSPENDED(job_ptr))
			continue;
		if ((select_ptr = job_ptr->select_job) == NULL) {
			error("job %u lacks a select_job_res struct",
			      job_ptr->job_id);
			continue;
		}

		job_memory_cpu  = 0;
		job_memory_node = 0;
		if (job_ptr->details && 
		    job_ptr->details->job_min_memory 
		    && (cr_type == CR_MEMORY)) {
			if (job_ptr->details->job_min_memory & MEM_PER_CPU) {
				job_memory_cpu = job_ptr->details->
						 job_min_memory &
						 (~MEM_PER_CPU);
			} else {
				job_memory_node = job_ptr->details->
						  job_min_memory;
			}
		}
		if (job_ptr->details->shared == 0)
			exclusive = 1;
		else
			exclusive = 0;

		/* Use select_ptr->node_bitmap rather than job_ptr->node_bitmap
		 * which can have DOWN nodes cleared from the bitmap */
		if (select_ptr->node_bitmap == NULL)
			continue;
		i_first = bit_ffs(select_ptr->node_bitmap);
		i_last  = bit_fls(select_ptr->node_bitmap);
		for (i=i_first; ((i<=i_last) && (i_first>=0)); i++) {
			if (!bit_test(select_ptr->node_bitmap, i))
				continue;
			if (exclusive) {
				if (node_cr_ptr[i].exclusive_jobid) {
					error("select/linear: conflicting "
				 	      "exclusive jobs %u and %u on %s",
				 	      job_ptr->job_id, 
				 	      node_cr_ptr[i].exclusive_jobid,
				 	      node_record_table_ptr[i].name);
				}
				node_cr_ptr[i].exclusive_jobid = job_ptr->job_id;
			}
			if (job_memory_cpu == 0)
				node_cr_ptr[i].alloc_memory += job_memory_node;
			else if (select_fast_schedule) {
				node_cr_ptr[i].alloc_memory += 
						job_memory_cpu *
						node_record_table_ptr[i].
						config_ptr->cpus;
			} else {
				node_cr_ptr[i].alloc_memory += 
						job_memory_cpu *
						node_record_table_ptr[i].cpus;
			}
			part_cr_ptr = node_cr_ptr[i].parts;
			while (part_cr_ptr) {
				if (part_cr_ptr->part_ptr != job_ptr->part_ptr) {
					part_cr_ptr = part_cr_ptr->next;
					continue;
				}
				part_cr_ptr->tot_job_cnt++;
				if (IS_JOB_RUNNING(job_ptr))
					part_cr_ptr->run_job_cnt++;
				break;
			}
			if (part_cr_ptr == NULL) {
				error("_init_node_cr: could not find "
					"partition %s for node %s",
					job_ptr->part_ptr->name,
					node_record_table_ptr[i].name);
			}
		}
	}
	list_iterator_destroy(job_iterator);
	_dump_node_cr(node_cr_ptr);
}

/* Determine where and when the job at job_ptr can begin execution by updating 
 * a scratch node_cr_record structure to reflect each job terminating at the 
 * end of its time limit and use this to show where and when the job at job_ptr 
 * will begin execution. Used by Moab for backfill scheduling. */
static int _will_run_test(struct job_record *job_ptr, bitstr_t *bitmap,
			  uint32_t min_nodes, uint32_t max_nodes, 
			  int max_share, uint32_t req_nodes)
{
	struct node_cr_record *exp_node_cr;
	struct job_record *tmp_job_ptr, **tmp_job_pptr;
	List cr_job_list;
	ListIterator job_iterator;
	bitstr_t *orig_map;
	int i, rc = SLURM_ERROR;
	int max_run_jobs = max_share - 1;	/* exclude this job */
	time_t now = time(NULL);

	orig_map = bit_copy(bitmap);

	/* Try to run with currently available nodes */
	i = _job_count_bitmap(node_cr_ptr, job_ptr, orig_map, bitmap, 
			      max_run_jobs, NO_SHARE_LIMIT);
	if (i >= min_nodes) {
		rc = _job_test(job_ptr, bitmap, min_nodes, max_nodes, 
			       req_nodes);
		if (rc == SLURM_SUCCESS) {
			bit_free(orig_map);
			job_ptr->start_time = time(NULL);
			return SLURM_SUCCESS;
		}
	}

	/* Job is still pending. Simulate termination of jobs one at a time 
	 * to determine when and where the job can start. */
	exp_node_cr = _dup_node_cr(node_cr_ptr);
	if (exp_node_cr == NULL) {
		bit_free(orig_map);
		return SLURM_ERROR;
	}

	/* Build list of running jobs */
	cr_job_list = list_create(_cr_job_list_del);
	if (!cr_job_list)
		fatal("list_create: memory allocation failure");
	job_iterator = list_iterator_create(job_list);
	while ((tmp_job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (!IS_JOB_RUNNING(tmp_job_ptr))
			continue;
		if (tmp_job_ptr->end_time == 0) {
			error("Job %u has zero end_time", tmp_job_ptr->job_id);
			continue;
		}
		tmp_job_pptr = xmalloc(sizeof(struct job_record *));
		*tmp_job_pptr = tmp_job_ptr;
		list_append(cr_job_list, tmp_job_pptr);
	}
	list_iterator_destroy(job_iterator);
	list_sort(cr_job_list, _cr_job_list_sort);

	/* Remove the running jobs one at a time from exp_node_cr and try
	 * scheduling the pending job after each one */
	job_iterator = list_iterator_create(cr_job_list);
	while ((tmp_job_pptr = (struct job_record **) list_next(job_iterator))) {
		tmp_job_ptr = *tmp_job_pptr;
		_rm_job_from_nodes(exp_node_cr, tmp_job_ptr,
				   "_will_run_test", 1);
		i = _job_count_bitmap(exp_node_cr, job_ptr, orig_map, bitmap, 
				      max_run_jobs, NO_SHARE_LIMIT);
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
	list_destroy(cr_job_list);
	_free_node_cr(exp_node_cr);
	bit_free(orig_map);
	return rc;
}

static void _cr_job_list_del(void *x)
{
	xfree(x);
}

static int  _cr_job_list_sort(void *x, void *y)
{
	struct job_record **job1_pptr = (struct job_record **) x;
	struct job_record **job2_pptr = (struct job_record **) y;
	return (int) difftime(job1_pptr[0]->end_time, job2_pptr[0]->end_time);
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	int rc = SLURM_SUCCESS;
#ifdef HAVE_XCPU
	rc = _init_status_pthread();
#endif
#ifdef HAVE_BG
	error("%s is incompatable with BlueGene", plugin_name);
	fatal("Use SelectType=select/bluegene");
#endif
	cr_type = (select_type_plugin_info_t)
			slurmctld_conf.select_type_param;
	return rc;
}

extern int fini ( void )
{
	int rc = SLURM_SUCCESS;
#ifdef HAVE_XCPU
	rc = _fini_status_pthread();
#endif
	slurm_mutex_lock(&cr_mutex);
	_free_node_cr(node_cr_ptr);
	node_cr_ptr = NULL;
	if (step_cr_list)
		list_destroy(step_cr_list);
	step_cr_list = NULL;
	slurm_mutex_unlock(&cr_mutex);
	return rc;
}

/*
 * The remainder of this file implements the standard SLURM 
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

extern int select_p_job_init(List job_list)
{
	return SLURM_SUCCESS;
}

extern int select_p_node_init(struct node_record *node_ptr, int node_cnt)
{
	if (node_ptr == NULL) {
		error("select_p_node_init: node_ptr == NULL");
		return SLURM_ERROR;
	}

	if (node_cnt < 0) {
		error("select_p_node_init: node_cnt < 0");
		return SLURM_ERROR;
	}

	/* NOTE: We free the consumable resources info here, but
	 * can't rebuild it since the partition and node structures
	 * have not yet had node bitmaps reset. */
	slurm_mutex_lock(&cr_mutex);
	_free_node_cr(node_cr_ptr);
	node_cr_ptr = NULL;
	if (step_cr_list)
		list_destroy(step_cr_list);
	step_cr_list = NULL;
	slurm_mutex_unlock(&cr_mutex);

	select_node_ptr = node_ptr;
	select_node_cnt = node_cnt;
	select_fast_schedule = slurm_get_fast_schedule();

	return SLURM_SUCCESS;
}

extern int select_p_block_init(List part_list)
{
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
 * RET zero on success, EINVAL otherwise
 * globals (passed via select_p_node_init): 
 *	node_record_count - count of nodes configured
 *	node_record_table_ptr - pointer to global node table
 * NOTE: the job information that is considered for scheduling includes:
 *	req_node_bitmap: bitmap of specific nodes required by the job
 *	contiguous: allocated nodes must be sequentially located
 *	num_procs: minimum number of processors required by the job
 * NOTE: bitmap must be a superset of the job's required at the time that 
 *	select_p_job_test is called
 */
extern int select_p_job_test(struct job_record *job_ptr, bitstr_t *bitmap,
			     uint32_t min_nodes, uint32_t max_nodes, 
			     uint32_t req_nodes, int mode)
{
	bitstr_t *orig_map;
	int max_run_job, j, sus_jobs, rc = EINVAL, prev_cnt = -1;
	int min_share = 0, max_share = 0;
	uint32_t save_mem = 0;

	xassert(bitmap);
	if (job_ptr->details == NULL)
		return EINVAL;

	slurm_mutex_lock(&cr_mutex);
	if (node_cr_ptr == NULL) {
		_init_node_cr();
		if (node_cr_ptr == NULL) {
			slurm_mutex_unlock(&cr_mutex);
			error("select_p_job_test: node_cr_ptr not initialized");
			return SLURM_ERROR;
		}
	}

	if (bit_set_count(bitmap) < min_nodes) {
		slurm_mutex_unlock(&cr_mutex);
		return EINVAL;
	}

	if (mode != SELECT_MODE_TEST_ONLY) {
		if (job_ptr->details->shared) {
			max_share = job_ptr->part_ptr->max_share & 
					~SHARED_FORCE;
		} else	/* ((shared == 0) || (shared == (uint16_t) NO_VAL)) */
			max_share = 1;
	}

	if (mode == SELECT_MODE_WILL_RUN) {
		rc = _will_run_test(job_ptr, bitmap, min_nodes, max_nodes,
				    max_share, req_nodes);
		slurm_mutex_unlock(&cr_mutex);
		return rc;
	} else if (mode == SELECT_MODE_TEST_ONLY) {
		min_share = NO_SHARE_LIMIT;
		max_share = min_share + 1;
		save_mem = job_ptr->details->job_min_memory;
		job_ptr->details->job_min_memory = 0;
	}

	debug3("select/linear: job_test: job %u max_share %d avail nodes %u",
		job_ptr->job_id, max_share, bit_set_count(bitmap));
	orig_map = bit_copy(bitmap);
	for (max_run_job=min_share; max_run_job<max_share; max_run_job++) {
		bool last_iteration = (max_run_job == (max_share -1));
		for (sus_jobs=0; ((sus_jobs<5) && (rc != SLURM_SUCCESS)); 
		     sus_jobs++) {
			if (last_iteration)
				sus_jobs = NO_SHARE_LIMIT;
			j = _job_count_bitmap(node_cr_ptr, job_ptr, 
					      orig_map, bitmap, 
					      max_run_job, 
					      max_run_job + sus_jobs);
			debug3("select/linear: job_test: found %d nodes for %u",
				j, job_ptr->job_id);
			if ((j == prev_cnt) || (j < min_nodes))
				continue;
			prev_cnt = j;
			if ((mode == SELECT_MODE_RUN_NOW)
			    && (max_run_job > 0)) {
				/* We need to share. Try to find 
				 * suitable job to share nodes with */
				rc = _find_job_mate(job_ptr, bitmap, min_nodes, 
						    max_nodes, req_nodes);
				if (rc == SLURM_SUCCESS)
					break;
			}
			rc = _job_test(job_ptr, bitmap, min_nodes, max_nodes, 
				       req_nodes);
			if (rc == SLURM_SUCCESS)
				break;
			continue;
		}
	}
	bit_free(orig_map);
	slurm_mutex_unlock(&cr_mutex);
	if ((rc == SLURM_SUCCESS) && (mode == SELECT_MODE_RUN_NOW))
		_build_select_struct(job_ptr, bitmap);
	if (save_mem)
		job_ptr->details->job_min_memory = save_mem;
	return rc;
}

/*
 * select_p_job_list_test - Given a list of select_will_run_t's in
 *	accending priority order we will see if we can start and
 *	finish all the jobs without increasing the start times of the
 *	jobs specified and fill in the est_start of requests with no
 *	est_start.  If you are looking to see if one job will ever run
 *	then use select_p_job_test instead.
 * IN/OUT req_list - list of select_will_run_t's in asscending
 *	             priority order on success of placement fill in
 *	             est_start of request with time.
 * RET zero on success, EINVAL otherwise
 */
extern int select_p_job_list_test(List req_list)
{
	/* not currently supported */
	return EINVAL;
}

extern int select_p_job_begin(struct job_record *job_ptr)
{
	int rc = SLURM_SUCCESS;
#ifdef HAVE_XCPU
	int i;
	char clone_path[128];

	xassert(job_ptr);
	xassert(job_ptr->node_bitmap);

	for (i=0; i<select_node_cnt; i++) {
		if (bit_test(job_ptr->node_bitmap, i) == 0)
			continue;
		snprintf(clone_path, sizeof(clone_path), 
			"%s/%s/xcpu/clone", XCPU_DIR, 
			select_node_ptr[i].name);
		if (chown(clone_path, (uid_t)job_ptr->user_id, 
				(gid_t)job_ptr->group_id)) {
			error("chown %s: %m", clone_path);
			rc = SLURM_ERROR;
		} else {
			debug("chown %s to %u", clone_path, 
				job_ptr->user_id);
		}
	}
#endif
	slurm_mutex_lock(&cr_mutex);
	if (node_cr_ptr == NULL)
		_init_node_cr();
	_add_job_to_nodes(node_cr_ptr, job_ptr, "select_p_job_begin", 1);
	slurm_mutex_unlock(&cr_mutex);
	return rc;
}

extern int select_p_job_ready(struct job_record *job_ptr)
{
	if (!IS_JOB_RUNNING(job_ptr))
		return 0;

	return 1;
}

extern int select_p_job_fini(struct job_record *job_ptr)
{
	int rc = SLURM_SUCCESS;
#ifdef HAVE_XCPU
	int i;
	char clone_path[128];

	for (i=0; i<select_node_cnt; i++) {
		if (bit_test(job_ptr->node_bitmap, i) == 0)
			continue;
		snprintf(clone_path, sizeof(clone_path), 
			"%s/%s/xcpu/clone", XCPU_DIR, 
			select_node_ptr[i].name);
		if (chown(clone_path, (uid_t)0, (gid_t)0)) {
			error("chown %s: %m", clone_path);
			rc = SLURM_ERROR;
		} else {
			debug("chown %s to 0", clone_path);
		}
	}
#endif
	slurm_mutex_lock(&cr_mutex);
	if (node_cr_ptr == NULL)
		_init_node_cr();
	_rm_job_from_nodes(node_cr_ptr, job_ptr, "select_p_job_fini", 1);
	slurm_mutex_unlock(&cr_mutex);
	return rc;
}

extern int select_p_job_suspend(struct job_record *job_ptr)
{
	slurm_mutex_lock(&cr_mutex);
	if (node_cr_ptr == NULL)
		_init_node_cr();
	_rm_job_from_nodes(node_cr_ptr, job_ptr, "select_p_job_suspend", 0);
	slurm_mutex_unlock(&cr_mutex);
	return SLURM_SUCCESS;
}

extern int select_p_job_resume(struct job_record *job_ptr)
{
	slurm_mutex_lock(&cr_mutex);
	if (node_cr_ptr == NULL)
		_init_node_cr();
	_add_job_to_nodes(node_cr_ptr, job_ptr, "select_p_job_resume", 0);
	slurm_mutex_unlock(&cr_mutex);
	return SLURM_SUCCESS;
}

extern int select_p_pack_select_info(time_t last_query_time, Buf *buffer_ptr)
{
	/* This function is always invalid on normal Linux clusters */
	return SLURM_ERROR;
}

extern int select_p_select_nodeinfo_pack(select_nodeinfo_t *nodeinfo, 
					 Buf buffer)
{
	pack16(nodeinfo->alloc_cpus, buffer);

	return SLURM_SUCCESS;
}

extern int select_p_select_nodeinfo_unpack(select_nodeinfo_t **nodeinfo, 
					   Buf buffer)
{
	select_nodeinfo_t *nodeinfo_ptr = NULL;

	nodeinfo_ptr = select_p_select_nodeinfo_alloc(NO_VAL);
	*nodeinfo = nodeinfo_ptr;

	safe_unpack16(&nodeinfo_ptr->alloc_cpus, buffer);

	return SLURM_SUCCESS;

unpack_error:
	error("select_nodeinfo_unpack: error unpacking here");
	select_p_select_nodeinfo_free(nodeinfo_ptr);
	*nodeinfo = NULL;

	return SLURM_ERROR;
}

extern select_nodeinfo_t *select_p_select_nodeinfo_alloc(uint32_t size)
{
	select_nodeinfo_t *nodeinfo = xmalloc(sizeof(struct select_nodeinfo));

	nodeinfo->magic = NODEINFO_MAGIC;

	return nodeinfo;
}

extern int select_p_select_nodeinfo_free(select_nodeinfo_t *nodeinfo)
{
	if(nodeinfo) {
		if (nodeinfo->magic != NODEINFO_MAGIC) {
			error("select_p_select_nodeinfo_free: "
			      "nodeinfo magic bad");
			return EINVAL;
		} 
		nodeinfo->magic = 0;
		xfree(nodeinfo);
	}
	return SLURM_SUCCESS;
}

extern int select_p_select_nodeinfo_set_all(time_t last_query_time)
{
	struct node_record *node_ptr = NULL;
	int i=0;
	static time_t last_set_all = 0;

	/* only set this once when the last_node_update is newer than
	   the last time we set things up. */
	if(last_set_all && (last_node_update < last_set_all)) {
		debug2("Node select info for set all hasn't "
		       "changed since %d", 
		       last_set_all);
		return SLURM_NO_CHANGE_IN_DATA;	
	}
	last_set_all = last_node_update;

	for (i=0; i<node_record_count; i++) {
		node_ptr = &(node_record_table_ptr[i]);

		if ((node_ptr->node_state & NODE_STATE_COMPLETING) ||
		    (node_ptr->node_state == NODE_STATE_ALLOCATED)) {
			if (slurmctld_conf.fast_schedule)
				node_ptr->select_nodeinfo->alloc_cpus = 
					node_ptr->config_ptr->cpus;
			else
				node_ptr->select_nodeinfo->alloc_cpus = 
					node_ptr->cpus;
		}
	}

	return SLURM_SUCCESS;
}

extern int select_p_select_nodeinfo_set(struct job_record *job_ptr)
{
	xassert(job_ptr);
	
	slurm_mutex_lock(&cr_mutex);
	if (node_cr_ptr == NULL)
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

	if (nodeinfo == NULL) {
		error("get_nodeinfo: nodeinfo not set");
		return SLURM_ERROR;
	}
	
	if (nodeinfo->magic != NODEINFO_MAGIC) {
		error("get_nodeinfo: jobinfo magic bad");
		return SLURM_ERROR;
	}

	switch (dinfo) {
	case SELECT_NODEDATA_SUBGRP_SIZE:
		*uint16 = 0;
		break;
	case SELECT_NODEDATA_SUBCNT:
		if(state == NODE_STATE_ALLOCATED)
			*uint16 = nodeinfo->alloc_cpus;
		break;
	default:
		error("Unsupported option %d for get_nodeinfo.", dinfo);
		rc = SLURM_ERROR;
		break;
	}	
	return rc;
}

extern select_jobinfo_t *select_p_select_jobinfo_alloc()
{
	return SLURM_SUCCESS;
}

extern int select_p_select_jobinfo_set(select_jobinfo_t *jobinfo,
				       enum select_jobdata_type data_type,
				       void *data)
{
	return SLURM_SUCCESS;
}

extern int select_p_select_jobinfo_get (select_jobinfo_t *jobinfo,
				 enum select_jobdata_type data_type, void *data)
{
	return SLURM_SUCCESS;
}

extern select_jobinfo_t *select_p_select_jobinfo_copy(select_jobinfo_t *jobinfo)
{
	return NULL;
}

extern int select_p_select_jobinfo_free  (select_jobinfo_t *jobinfo)
{
	return SLURM_SUCCESS;
}

extern int  select_p_select_jobinfo_pack(select_jobinfo_t *jobinfo, Buf buffer)
{
	return SLURM_SUCCESS;
}

extern int  select_p_select_jobinfo_unpack(select_jobinfo_t **jobinfo,
					   Buf buffer)
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

extern int select_p_update_block (update_part_msg_t *part_desc_ptr)
{
	return SLURM_SUCCESS;
}

extern int select_p_update_sub_node (update_part_msg_t *part_desc_ptr)
{
	return SLURM_SUCCESS;
}

extern int select_p_get_info_from_plugin (enum select_jobdata_type info,
					  struct job_record *job_ptr,
					  void *data)
{
	return SLURM_SUCCESS;
}

extern int select_p_update_node_state (int index, uint16_t state)
{
	return SLURM_SUCCESS;
}

extern int select_p_alter_node_cnt(enum select_node_cnt type, void *data)
{
	return SLURM_SUCCESS;
}

extern int select_p_reconfigure(void)
{
	slurm_mutex_lock(&cr_mutex);
	_free_node_cr(node_cr_ptr);
	node_cr_ptr = NULL;
	if (step_cr_list)
		list_destroy(step_cr_list);
	step_cr_list = NULL;
	_init_node_cr();
	slurm_mutex_unlock(&cr_mutex);

	return SLURM_SUCCESS;
}
