/*****************************************************************************\
 *  select_linear.c - node selection plugin for simple one-dimensional 
 *  address space. Selects nodes for a job so as to minimize the number 
 *  of sets of consecutive nodes using a best-fit algorithm.
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2004-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-226842.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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
#include "src/common/slurm_protocol_api.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"

#include "src/common/slurm_resource_info.h"

#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/proc_req.h"

#define SELECT_DEBUG 0

static int _job_count_bitmap(bitstr_t * bitmap, bitstr_t * jobmap, int job_cnt);
static int _job_test(struct job_record *job_ptr, bitstr_t *bitmap,
			uint32_t min_nodes, uint32_t max_nodes, 
			uint32_t req_nodes);
static int _find_job_mate(struct job_record *job_ptr, bitstr_t *bitmap,
			uint32_t min_nodes, uint32_t max_nodes,
			uint32_t req_nodes);
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
		debug2("XCPU thread already running, not starting "
			"another");
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
	int i, rc=SLURM_SUCCESS;

	slurm_mutex_lock( &thread_flag_mutex );
	if ( xcpu_thread ) {
		agent_fini = 1;
		for (i=0; i<4; i++) {
			if (pthread_kill(xcpu_thread, 0)) {
				xcpu_thread = 0;
				break;
			}
			sleep(1);
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

static bool 
_enough_nodes(int avail_nodes, int rem_nodes, 
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
	fatal("%s is incompatable with Blue Gene", plugin_name);
#endif
	return rc;
}

extern int fini ( void )
{
	int rc = SLURM_SUCCESS;
#ifdef HAVE_XCPU
	rc = _fini_status_pthread();
#endif
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

	if (job_ptr->details) {
		if (job_ptr->details->cpus_per_task)
			cpus_per_task = job_ptr->details->cpus_per_task;
		if (job_ptr->details->ntasks_per_node)
			ntasks_per_node = job_ptr->details->ntasks_per_node;
		mc_ptr = job_ptr->details->mc_ptr;
	}
	if (mc_ptr) {
		max_sockets       = job_ptr->details->mc_ptr->max_sockets;
		max_cores         = job_ptr->details->mc_ptr->max_cores;
		max_threads       = job_ptr->details->mc_ptr->max_threads;
		ntasks_per_socket = job_ptr->details->mc_ptr->ntasks_per_socket;
		ntasks_per_core   = job_ptr->details->mc_ptr->ntasks_per_core;
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

/*
 * select_p_job_test - Given a specification of scheduling requirements, 
 *	identify the nodes which "best" satisfy the request.
 * 	"best" is defined as either single set of consecutive nodes satisfying 
 *	the request and leaving the minimum number of unused nodes OR 
 *	the fewest number of consecutive node sets
 * IN job_ptr - pointer to job being scheduled
 * IN/OUT bitmap - usable nodes are set on input, nodes not required to 
 *	satisfy the request are cleared, other left set
 * IN min_nodes - minimum count of nodes
 * IN req_nodes - requested (or desired) count of nodes
 * IN max_nodes - maximum count of nodes (0==don't care)
 * IN test_only - if true, only test if ever could run, not necessarily now,
 *	not used in this implementation of plugin
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
			uint32_t req_nodes, bool test_only)
{
	multi_core_data_t *mc_ptr = job_ptr->details->mc_ptr;
	bitstr_t *tmp_map;
	int i, j, rc = EINVAL, prev_cnt = -1;
	int min_share, max_share;

	xassert(bitmap);
	if (mc_ptr) {
		debug3("job min-[max]: -N %u-[%u]:%u-[%u]:%u-[%u]:%u-[%u]",
			job_ptr->details->min_nodes, 
			job_ptr->details->max_nodes,
			mc_ptr->min_sockets, mc_ptr->max_sockets,
			mc_ptr->min_cores,   mc_ptr->max_cores,
			mc_ptr->min_threads, mc_ptr->max_threads);
		debug3("job ntasks-per: -node=%u -socket=%u -core=%u",
			job_ptr->details->ntasks_per_node,
			mc_ptr->ntasks_per_socket, mc_ptr->ntasks_per_core);
	}

	if (bit_set_count(bitmap) < min_nodes)
		return EINVAL;
	if (test_only)
		min_share = max_share = 999;
	else {
		min_share = 0;
		if (job_ptr->details->shared) {
			max_share = job_ptr->part_ptr->max_share & 
					~SHARED_FORCE;
			max_share = MAX(1, max_share);
		} else
			max_share = 1;
	}

	tmp_map = bit_copy(bitmap);
	for (i=min_share; i<max_share; i++) {
		j = _job_count_bitmap(bitmap, tmp_map, i);
		if ((j == prev_cnt) || (j < min_nodes))
			continue;
		prev_cnt = j;
		if ((!test_only) && (i > 0)) {
			/* We need to share, try to suitable job
			 * to share nodes with */
			rc = _find_job_mate(job_ptr, tmp_map, 
					    min_nodes, max_nodes, req_nodes);
			if (rc == SLURM_SUCCESS) {
				bit_and(bitmap, tmp_map);
				break;
			}
		}
		rc = _job_test(job_ptr, tmp_map, min_nodes, max_nodes, 
			       req_nodes);
		if (rc == SLURM_SUCCESS) {
			bit_and(bitmap, tmp_map);
			break;
		}
		continue;
	}
	bit_free(tmp_map);
	return rc;
}

/*
 * Set the bits in 'jobmap' that correspond to bits in the 'bitmap'
 * that are running 'job_cnt' jobs or less, and clear the rest.
 */
static int _job_count_bitmap(bitstr_t * bitmap, bitstr_t * jobmap, int job_cnt)
{
	int i, count = 0;
	bitoff_t size = bit_size(bitmap);

	for (i = 0; i < size; i++) {
		if (bit_test(bitmap, i) &&
		    (node_record_table_ptr[i].run_job_cnt <= job_cnt)) {
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

	job_iterator = list_iterator_create(job_list);
	while ((job_scan_ptr = (struct job_record *) list_next(job_iterator))) {
		if ((job_scan_ptr->part_ptr == job_ptr->part_ptr) &&
		    (job_scan_ptr->job_state == JOB_RUNNING) &&
		    (job_scan_ptr->node_cnt == req_nodes) &&
		    bit_super_set(job_scan_ptr->node_bitmap, bitmap)) {
			bit_and(bitmap, job_scan_ptr->node_bitmap);
			return SLURM_SUCCESS;
		}
	}
	list_iterator_destroy(job_iterator);
	return EINVAL;
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
	int avail_cpus;

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
				rem_cpus -= avail_cpus;
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
	/* don't compile this, slows things down too much */
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
				continue;
			sufficient = (consec_cpus[i] >= rem_cpus)
			&& _enough_nodes(consec_nodes[i], rem_nodes,
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
				rem_cpus -= avail_cpus;
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
				rem_cpus -= avail_cpus;
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
				rem_cpus -= avail_cpus;
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

	xfree(consec_cpus);
	xfree(consec_nodes);
	xfree(consec_start);
	xfree(consec_end);
	xfree(consec_req);
	return error_code;
}

extern int select_p_job_begin(struct job_record *job_ptr)
{
#ifdef HAVE_XCPU
	int i;
	int rc = SLURM_SUCCESS;
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
	return rc;
#else
	return SLURM_SUCCESS;
#endif
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
	return rc;
}

extern int select_p_job_suspend(struct job_record *job_ptr)
{
	return SLURM_SUCCESS;
}

extern int select_p_job_resume(struct job_record *job_ptr)
{
	return SLURM_SUCCESS;
}

extern int select_p_job_ready(struct job_record *job_ptr)
{
	if (job_ptr->job_state != JOB_RUNNING)
		return 0;

	return 1;
}

extern int select_p_pack_node_info(time_t last_query_time, Buf *buffer_ptr)
{
	/* This function is always invalid on normal Linux clusters */
	return SLURM_ERROR;
}

extern int select_p_get_select_nodeinfo (struct node_record *node_ptr, 
                                         enum select_data_info info,
                                         void *data)
{
       return SLURM_SUCCESS;
}

extern int select_p_update_nodeinfo (struct job_record *job_ptr)
{
       return SLURM_SUCCESS;
}

extern int select_p_update_block (update_part_msg_t *part_desc_ptr)
{
	return SLURM_SUCCESS;
}

extern int select_p_update_sub_node (update_part_msg_t *part_desc_ptr)
{
	return SLURM_SUCCESS;
}
extern int select_p_get_extra_jobinfo (struct node_record *node_ptr, 
                                       struct job_record *job_ptr, 
                                       enum select_data_info info,
                                       void *data)
{
	int rc = SLURM_SUCCESS;

	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);

	switch (info) {
	case SELECT_AVAIL_CPUS:
	{
		uint16_t *tmp_16 = (uint16_t *) data;

		if ((job_ptr->details->cpus_per_task > 1)
		||  (job_ptr->details->mc_ptr)) {
			int index = (node_ptr - node_record_table_ptr);
			*tmp_16 = _get_avail_cpus(job_ptr, index);
		} else {
			if (slurmctld_conf.fast_schedule) {
				*tmp_16 = node_ptr->config_ptr->cpus;
			} else {
				*tmp_16 = node_ptr->cpus;
			}
		}
		break;
	}
	default:
		error("select_g_get_extra_jobinfo info %d invalid", info);
		rc = SLURM_ERROR;
		break;
	}
	
	return rc;
}

extern int select_p_get_info_from_plugin (enum select_data_info info,
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
	return SLURM_SUCCESS;
}

