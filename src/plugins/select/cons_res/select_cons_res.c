/*****************************************************************************\
 *  select_cons_res.c - node selection plugin supporting consumable 
 *  resources policies.
 *
 *  The following example below illustrates how four jobs are allocated
 *  across a cluster using when a processor consumable resource approach.
 * 
 *  The example cluster is composed of 4 nodes (10 cpus in total):
 *  linux01 (with 2 processors), 
 *  linux02 (with 2 processors), 
 *  linux03 (with 2 processors), and
 *  linux04 (with 4 processors). 
 *
 *  The four jobs are the following: 
 *  1. srun -n 4 -N 4  sleep 120 &
 *  2. srun -n 3 -N 3 sleep 120 &
 *  3. srun -n 1 sleep 120 &
 *  4. srun -n 3 sleep 120 &
 *  The user launches them in the same order as listed above.
 * 
 *  Using a processor consumable resource approach we get the following
 *  job allocation and scheduling:
 * 
 *  The output of squeue shows that we have 3 out of the 4 jobs allocated
 *  and running. This is a 2 running job increase over the default SLURM
 *  approach.
 * 
 *  Job 2, Job 3, and Job 4 are now running concurrently on the cluster.
 * 
 *  [<snip>]# squeue
 *  JOBID PARTITION     NAME     USER  ST       TIME  NODES NODELIST(REASON)
 *     5       lsf    sleep     root  PD       0:00      1 (Resources)
 *     2       lsf    sleep     root   R       0:13      4 linux[01-04]
 *     3       lsf    sleep     root   R       0:09      3 linux[01-03]
 *     4       lsf    sleep     root   R       0:05      1 linux04
 *  [<snip>]#
 * 
 *  Once Job 2 finishes, Job 5, which was pending, is allocated
 *  available resources and is then running as illustrated below:
 * 
 *  [<snip>]# squeue4
 *   JOBID PARTITION     NAME     USER  ST       TIME  NODES NODELIST(REASON)
 *     3       lsf    sleep     root   R       1:58      3 linux[01-03]
 *     4       lsf    sleep     root   R       1:54      1 linux04
 *     5       lsf    sleep     root   R       0:02      3 linux[01-03]
 *  [<snip>]#
 * 
 *  Job 3, Job 4, and Job 5 are now running concurrently on the cluster.
 * 
 *  [<snip>]#  squeue4
 *  JOBID PARTITION     NAME     USER  ST       TIME  NODES NODELIST(REASON)
 *     5       lsf    sleep     root   R       1:52      3 xc14n[13-15]
 *  [<snip>]#
 *
 * The advantage of the consumable resource scheduling policy is that
 * the job throughput can increase dramatically.
 *
 *  $Id$
 *
 *****************************************************************************
 *  Copyright (C) 2005-2006 Hewlett-Packard Development Company, L.P.
 *  Written by Susanne M. Balle <susanne.balle@hp.com>, who borrowed heavily
 *  from select/linear 
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
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/node_select.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/slurm_resource_info.h"
#include "src/slurmctld/slurmctld.h"

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
const char plugin_name[] =
    "Consumable Resources (CR) Node Selection plugin";
const char plugin_type[] = "select/cons_res";
const uint32_t plugin_version = 90;

/* node_cr_record keeps track of the resources within a node which 
 * have been reserved by already scheduled jobs. 
 */
struct node_cr_record {
	struct node_record *node_ptr;	/* ptr to the node that own these resources */
	uint32_t alloc_lps;	/* cpu count reserved by already scheduled jobs */
	uint32_t alloc_sockets;	/* socket count reserved by already scheduled jobs */
	uint32_t *alloc_cores;	/* core count reserved by already scheduled jobs */
	uint32_t alloc_memory;	/* real memory reserved by already scheduled jobs */
	struct node_cr_record *node_next;/* next entry with same hash index */
};

#define CR_JOB_STATE_SUSPENDED 1

struct select_cr_job {
	uint32_t job_id;	/* job ID, default set by SLURM        */
	uint16_t state;		/* job state information               */
	int nprocs;		/* --nprocs=n,      -n n               */
	int nhosts;		/* number of hosts allocated to job    */
	char **host;		/* hostname vector                     */
	int *cpus;		/* number of processors on each host   */
	int *alloc_lps;	        /* number of allocated threads/lps on each host */
	int *alloc_sockets;	/* number of allocated sockets on each host */
	int **alloc_cores;	/* Allocated cores per socket on each host */
	int *alloc_memory;      /* number of allocated MB of real memory on each host */
	int max_sockets;
	int max_cores;
	int max_threads;
	int ntasks_per_node;
	int ntasks_per_socket;
	int ntasks_per_core;
	int cpus_per_task;      
	bitstr_t *node_bitmap;	/* bitmap of nodes allocated to job    */
};

select_type_plugin_info_t cr_type = CR_CPU; /* cr_type is overwritten in init() */

static struct node_cr_record *select_node_ptr; /* Array of node_cr_record. One
						  entry for each node in the cluster */
static int select_node_cnt;
static struct node_cr_record **cr_node_hash_table = NULL; /* node_cr_record hash table */

static uint16_t select_fast_schedule;

List select_cr_job_list = NULL; /* List of select_cr_job(s) that are still active */

#if(0)
/* 
 * _cr_dump_hash - print the cr_node_hash_table contents, used for debugging
 *	or analysis of hash technique. See _hash_table in slurmctld/node_mgr.c 
 * global: select_node_ptr    - table of node_cr_record
 *         cr_node_hash_table - table of hash indices
 * Inspired from _dump_hash() in slurmctld/node_mgr.c
 */
static void _cr_dump_hash (void) 
{
	int i, inx;
	struct node_cr_record *this_node_ptr;

	if (cr_node_hash_table == NULL)
		return;
	for (i = 0; i < select_node_cnt; i++) {
		this_node_ptr = cr_node_hash_table[i];
		while (this_node_ptr) {
		        inx = this_node_ptr - select_node_ptr;
			verbose("node_hash[%d]:%d", i, inx);
			this_node_ptr = this_node_ptr->node_next;
		}
	}
}

#endif

/* 
 * _cr_hash_index - return a hash table index for the given node name 
 * IN name = the node's name
 * RET the hash table index
 * Inspired from _hash_index(char *name) in slurmctld/node_mgr.c 
 */
static int _cr_hash_index (const char *name) 
{
	int index = 0;
	int j;

	if ((select_node_cnt == 0)
	||  (name == NULL))
		return 0;	/* degenerate case */

	/* Multiply each character by its numerical position in the
	 * name string to add a bit of entropy, because host names such
	 * as cluster[0001-1000] can cause excessive index collisions.
	 */
	for (j = 1; *name; name++, j++)
		index += (int)*name * j;
	index %= select_node_cnt;
	
	return index;
}

/*
 * _build_cr_node_hash_table - build a hash table of the node_cr_record entries. 
 * global: select_node_ptr    - table of node_cr_record 
 *         cr_node_hash_table - table of hash indices
 * NOTE: manages memory for cr_node_hash_table
 * Inspired from rehash_nodes() in slurmctld/node_mgr.c
 */
void _build_cr_node_hash_table (void)
{
	int i, inx;

	xfree (cr_node_hash_table);
	cr_node_hash_table = xmalloc (sizeof (struct node_cr_record *) * 
				select_node_cnt);

	for (i = 0; i < select_node_cnt; i++) {
		if (strlen (select_node_ptr[i].node_ptr->name) == 0)
			continue;	/* vestigial record */
		inx = _cr_hash_index (select_node_ptr[i].node_ptr->name);
		select_node_ptr[i].node_next = cr_node_hash_table[inx];
		cr_node_hash_table[inx] = &select_node_ptr[i];
	}

#if(0)
	_cr_dump_hash();
#endif
	return;
}

/* 
 * _find_cr_node_record - find a record for node with specified name
 * input: name - name of the desired node 
 * output: return pointer to node record or NULL if not found
 * global: select_node_ptr - pointer to global select_node_ptr
 *         cr_node_hash_table - table of hash indecies
 * Inspired from find_node_record (char *name) in slurmctld/node_mgr.c 
 */
struct node_cr_record * 
_find_cr_node_record (const char *name) 
{
	int i;

	if ((name == NULL)
	||  (name[0] == '\0')) {
		info("_find_cr_node_record passed NULL name");
		return NULL;
	}

	/* try to find via hash table, if it exists */
	if (cr_node_hash_table) {
		struct node_cr_record *this_node;

		i = _cr_hash_index (name);
		this_node = cr_node_hash_table[i];
		while (this_node) {
			xassert(this_node->node_ptr->magic == NODE_MAGIC);
			if (strncmp(this_node->node_ptr->name, name, MAX_SLURM_NAME) == 0) {
				return this_node;
			}
			this_node = this_node->node_next;
		}
		error ("_find_cr_node_record: lookup failure using hashtable for %s", 
                        name);
	} 

	/* revert to sequential search */
	else {
		for (i = 0; i < select_node_cnt; i++) {
		        if (strcmp (name, select_node_ptr[i].node_ptr->name) == 0) {
			        debug3("cons_res _find_cr_node_record: linear %s",  name);
				return (&select_node_ptr[i]);
			}
		}
		error ("_find_cr_node_record: lookup failure with linear search for %s", 
                        name);
	}
	error ("_find_cr_node_record: lookup failure with both method %s", name);
	return (struct node_cr_record *) NULL;
}

static void _get_resources_this_node(int *cpus, 
				     int *sockets, 
				     int *cores,
				     int *threads, 
				     struct node_cr_record *this_cr_node,
				     int *alloc_sockets, 
				     int *alloc_lps,
				     unsigned int *jobid)
{
	if (select_fast_schedule) {
		*cpus    = this_cr_node->node_ptr->config_ptr->cpus;
		*sockets = this_cr_node->node_ptr->config_ptr->sockets;
		*cores   = this_cr_node->node_ptr->config_ptr->cores;
		*threads = this_cr_node->node_ptr->config_ptr->threads;
	} else {
		*cpus    = this_cr_node->node_ptr->cpus;
		*sockets = this_cr_node->node_ptr->sockets;
		*cores   = this_cr_node->node_ptr->cores;
		*threads = this_cr_node->node_ptr->threads;
	}
	*alloc_sockets  = this_cr_node->alloc_sockets;
	*alloc_lps      = this_cr_node->alloc_lps;
#if(0)
	info("cons_res %d _get_resources host %s HW_ cpus %d sockets %d cores %d threads %d ", 
	       *jobid, this_cr_node->node_ptr->name,
	       *cpus, *sockets, *cores, *threads);
	info("cons_res %d _get_resources host %s Alloc_ sockets %d lps %d ", 
	       *jobid, this_cr_node->node_ptr->name, 
	       *alloc_sockets, *alloc_lps);
#endif
}

/*
 * _get_avail_memory returns the amount of available real memory in MB
 * for this node.
 */
static int _get_avail_memory(int index, int all_available) 
{
	int avail_memory = 0;
	struct node_cr_record *this_cr_node;
	
	if (select_fast_schedule) {
		avail_memory = select_node_ptr[index].node_ptr->config_ptr->real_memory;
	} else {
		avail_memory = select_node_ptr[index].node_ptr->real_memory;
	}
	
	if (all_available) 
		return avail_memory;
	
	this_cr_node = _find_cr_node_record (select_node_ptr[index].node_ptr->name);
	if (this_cr_node == NULL) {
		error(" cons_res: could not find node %s", 
		      select_node_ptr[index].node_ptr->name);
		avail_memory = 0;
		return avail_memory;
	}
	avail_memory -= this_cr_node->alloc_memory;
	
	return(avail_memory);
}

/*
 * _get_avail_lps - Get the number of "available" cpus on a node
 *	given the number of cpus_per_task and
 *	maximum sockets, cores, threads.  Note that the value of
 *	cpus is the lowest-level logical processor (LLLP).
 * IN job_ptr - pointer to job being scheduled
 * IN index - index of node's configuration information in select_node_ptr
 */
static int _get_avail_lps(struct job_record *job_ptr, 
			  const int index, 
			  const bool all_available)
{
	int avail_cpus, cpus_per_task = 0;
	int max_sockets = 0, max_cores = 0, max_threads = 0;
	int ntasks_per_node = 0, ntasks_per_socket = 0, ntasks_per_core = 0;
	int cpus, sockets, cores, threads;
	int alloc_sockets = 0, alloc_lps     = 0;
	struct node_cr_record *this_cr_node;
	
	if (job_ptr->details && job_ptr->details->cpus_per_task)
		cpus_per_task = job_ptr->details->cpus_per_task;
	if (job_ptr->details && job_ptr->details->max_sockets)
		max_sockets = job_ptr->details->max_sockets;
	if (job_ptr->details && job_ptr->details->max_cores)
		max_cores = job_ptr->details->max_cores;
	if (job_ptr->details && job_ptr->details->max_threads)
		max_threads = job_ptr->details->max_threads;

	this_cr_node = _find_cr_node_record (select_node_ptr[index].node_ptr->name);
	if (this_cr_node == NULL) {
		error(" cons_res: could not find node %s", 
		      select_node_ptr[index].node_ptr->name);
		avail_cpus = 0;
		return avail_cpus;
	}
	_get_resources_this_node(&cpus, &sockets, &cores, &threads, 
				 this_cr_node, &alloc_sockets, 
				 &alloc_lps, &job_ptr->job_id);
	if (all_available) {
		alloc_sockets = 0;
		alloc_lps     = 0;
	}

	avail_cpus = slurm_get_avail_procs(max_sockets,
					   max_cores,
					   max_threads,
					   cpus_per_task,
					   ntasks_per_node,
					   ntasks_per_socket,
					   ntasks_per_core,
					   &cpus, &sockets, &cores,
					   &threads, alloc_sockets,
					   alloc_lps, cr_type);
	return(avail_cpus);
}		

/* _compute_task_c_b_task_dist - compute the number of tasks on each
 * of the node for the cyclic and block distribution. We need to do
 * this in the case of consumable resources so that we have an exact
 * count for the needed hardware resources which will be used later to
 * update the different used resources per node structures.
 *
 * The most common case is when we have more resources than needed. In
 * that case we just "take" what we need and "release" the remaining
 * resources for other jobs. In the case where we oversubscribe the
 * CPUs/Logical processors resources we keep the initial set of
 * resources.
 *
 * IN job_ptr - pointer to job being scheduled
 */
static int _compute_c_b_task_dist(struct select_cr_job *job)
{
	int i, j, taskid = 0, rc = SLURM_SUCCESS;
	int avail_cpus = 0, cpus, sockets, cores, threads;
	bool over_subscribe = false;
	
	for (j = 0; (taskid < job->nprocs); j++) {	/* cycle counter */
		bool space_remaining = false;
		for (i = 0; 
		     ((i < job->nhosts) && (taskid < job->nprocs)); i++) {
			struct node_cr_record *this_node;
			this_node = _find_cr_node_record (job->host[i]);
			if (this_node == NULL) {
				error(" cons_res: could not find node %s",
				      job->host[i]);
				return SLURM_ERROR;
			}

			switch(cr_type) {
			case CR_MEMORY:
				if (select_fast_schedule) {
					avail_cpus = this_node->node_ptr->config_ptr->cpus;
				} else {
					avail_cpus = this_node->node_ptr->cpus;
				}
			case CR_CPU:
			case CR_CPU_MEMORY:
				if (select_fast_schedule) {
					avail_cpus = this_node->node_ptr->config_ptr->cpus;
				} else {
					avail_cpus = this_node->node_ptr->cpus;
				}
				avail_cpus -= this_node->alloc_lps;
				break;
			case CR_SOCKET:
			case CR_SOCKET_MEMORY:
			{
				int alloc_sockets = 0;
				int alloc_lps     = 0;
				_get_resources_this_node(&cpus, &sockets, 
							 &cores, &threads, 
							 this_node, &alloc_sockets, 
							 &alloc_lps, &job->job_id);

				avail_cpus = slurm_get_avail_procs(job->max_sockets, 
								   job->max_cores, 
								   job->max_threads, 
								   job->cpus_per_task, 
								   job->ntasks_per_node, 
								   job->ntasks_per_socket, 
								   job->ntasks_per_core, 
								   &cpus,
								   &sockets,
								   &cores,
								   &threads,
								   alloc_sockets,
								   alloc_lps,
								   cr_type);
				break;
			}
			case CR_CORE:
			case CR_CORE_MEMORY:
				/* Not implemented yet */
				break;
			default:
				/* We should never get in here. If we
                                   do it is a bug */
				error (" cr_type not recognized ");
				return SLURM_ERROR;
				break;
			}
			if ((j < avail_cpus) || over_subscribe) {
				taskid++;
				job->alloc_lps[i]++;
				if ((j + 1) < avail_cpus)
					space_remaining = true;
			}
		}
		if (!space_remaining)
			over_subscribe = true;
	}
	
	/* remove debugging */
	for (i = 0; i < job->nhosts; i++) {
		debug3("cons_res _c_b_task_dist %u host %s alloc_ lps %d ", 
		       job->job_id, job->host[i],  job->alloc_lps[i]);
	}
	
	return rc;
}

/* To effectively deal with heterogeneous nodes, we fake a cyclic
 * distribution to figure out how many cpus are needed on each node.
 *
 * This routine is a slightly modified "version" of the routine
 * _task_layout_block in src/common/dist_tasks.c. We do not need to
 * assign tasks to job->hostid[] and job->tids[][] at this point so
 * the cpu allocation is the same for cyclic and block.
 *
 * For the consumable resources support we need to determine what
 * "node/CPU/Core/thread"-tuplets will be allocated for a given job.
 * In the past we assumed that we only allocated on task per CPU (at
 * that point the lowest level of logical processor) and didn't allow
 * the use of overcommit. We have change this philosophy and are now
 * allowing people to overcommit their resources and expect the system
 * administrator to enable the task/affinity plug-in which will then
 * bind all of a job's tasks to its allocated resources thereby
 * avoiding interference between co-allocated running jobs.
 *
 * In the consumable resources environment we need to determine the
 * CPU or core layout schema within slurmctld.
*/
static int _cr_dist(struct select_cr_job *job, const int cyclic)
{
	int  rc = SLURM_SUCCESS; 
	int taskcount = 0;
	int maxtasks  = job->nprocs;
	int host_index;
	int usable_cpus = 0;
	int usable_sockets = 0, usable_cores = 0, usable_threads = 0;
	int last_socket_index = -1;
	int last_core_index = -1;
	int job_index = -1;

	int error_code = _compute_c_b_task_dist(job);
	if (error_code != SLURM_SUCCESS) {
		error(" Error in _compute_c_b_task_dist");
		return error_code;
	}

	if ((cr_type == CR_CPU) 
	    || (cr_type == CR_MEMORY) 
	    || (cr_type == CR_CPU_MEMORY)) 
		return SLURM_SUCCESS;

	for (host_index = 0; 
	     ((host_index < node_record_count) && (taskcount < job->nprocs));
	     host_index++) {
		struct node_cr_record *this_cr_node;
		int alloc_sockets = 0;
		int alloc_lps     = 0;
		int avail_cpus    = 0;
		if (bit_test(job->node_bitmap, host_index) == 0)
			continue;
		job_index++;

		this_cr_node = _find_cr_node_record(
			node_record_table_ptr[host_index].name);
		if (this_cr_node == NULL) {
			error(" cons_res: could not find node %s", 
			      node_record_table_ptr[host_index].name);
			return SLURM_ERROR;
		}

		_get_resources_this_node(&usable_cpus, &usable_sockets, 
					 &usable_cores, &usable_threads, 
					 this_cr_node,  &alloc_sockets, 
					 &alloc_lps, &job->job_id);

		avail_cpus = slurm_get_avail_procs(job->max_sockets,
						   job->max_cores,
						   job->max_threads,
						   job->cpus_per_task,
						   job->ntasks_per_node,
						   job->ntasks_per_socket,
						   job->ntasks_per_core,
						   &usable_cpus,
						   &usable_sockets,
						   &usable_cores,
						   &usable_threads,
						   alloc_sockets,
						   alloc_lps,
						   cr_type);
#if(0)		
		info("_cr_dist %u avail_s %d _c %d _t %d alloc_s %d alloc_lps %d ",
		     job->job_id, usable_sockets, usable_cores, usable_threads,
		     alloc_sockets, alloc_lps);
#endif
		if (avail_cpus == 0) {
			error(" cons_res: no available cpus on node %s", 
			      node_record_table_ptr[host_index].name);
		}
		maxtasks = job->alloc_lps[job_index];
		taskcount = 0; 
		job->alloc_sockets[job_index] = 0;

		if (cyclic == 0) { /* block lllp distribution */
			int s, c, t;
			last_socket_index = -1;	
			for (s=0; 
			     s < usable_sockets; s++) {
				last_core_index = -1;	
				if (maxtasks <= taskcount)
					continue;
				for (c=0; 
				     c < usable_cores; c++) {
					if (maxtasks <= taskcount)
						continue;
					for (t=0; 
					     t < usable_threads; t++) {
						if (maxtasks <= taskcount) 
							continue;
						if (last_socket_index != s) {
							job->alloc_sockets[job_index]++;
#if(0)
							info("block jid %u s %d c %d t %d tc %d", 
							     job->job_id, s, c, t, 
							     taskcount);
#endif
							last_socket_index = s;
						}
						taskcount++;
					}
				}
			}
		} else if (cyclic == 1) { /* cyclic lllp distribution */
			int s, c, t;
			int max_s = 0;	
			for (t=0; 
			     t < usable_threads; t++) {
				if (maxtasks <= taskcount)
					continue;
				for (c=0; 
				     c < usable_cores; c++) {
					if (maxtasks <= taskcount)
						continue;
					for (s=0;
					     s < usable_sockets; s++) {
						if (maxtasks <= taskcount)
							continue;
						if (max_s == 0) {
							job->alloc_sockets[job_index]++;
							if(s == (usable_sockets-1))
								max_s = 1;
						}
						taskcount++;
#if(0)
						info("cyclic jid %u s %d c %d t %d tc %d", 
						     job->job_id, s, c, t, taskcount);
#endif
					}
				}
			}
		}
#if(0)
		info("cons_res _cr_dist %u cyclic %d host %d %s alloc_ "
		     "sockets %d lps %d ", 
		     job->job_id, cyclic, host_index,  this_cr_node->node_ptr->name, 
		     job->alloc_sockets[job_index], job->alloc_lps[job_index]);
#endif
	}
	return rc;
}

/* User has specified the --exclusive flag on the srun command line
 * which means that the job should use only dedicated nodes.  In this
 * case we do not need to compute the number of tasks on each nodes
 * since it should be set to the number of cpus.
 */
static int _cr_exclusive_dist(struct select_cr_job *job)
{
	int i;
	int host_index = 0;

	for (i = 0; i < node_record_count; i++) {
		if (bit_test(job->node_bitmap, i) == 0)
			continue;
		job->alloc_lps[host_index] = node_record_table_ptr[i].cpus;
		job->alloc_sockets[host_index] = 
			node_record_table_ptr[i].sockets; 
		host_index++;
	}
	return SLURM_SUCCESS;
}

static int _cr_plane_dist(struct select_cr_job *job, const int plane_size)
{
	int maxtasks    = job->nprocs;
	int num_hosts   = job->nhosts;
	int taskid      = 0;
	bool count_done = false;
	int next = 0, max_plane_size = 0;
	int i, j, k, l, m, host_index;
	int usable_cpus, usable_sockets, usable_cores, usable_threads;
	int taskcount=0, last_socket_index;
	int socket_index, core_index, thread_index;
	int job_index = -1;
	
	debug3("cons_res _cr_plane_dist plane_size %d ", plane_size);
	debug3("cons_res _cr_plane_dist  maxtasks %d num_hosts %d",
	       maxtasks, num_hosts);

	if (plane_size <= 0) {
		error(" Error in _cr_plane_dist");
		return SLURM_ERROR;
	}
	
	for (j=0; ((taskid<maxtasks) && (!count_done)); j++) {
		for (i=0; 
		     (((i<num_hosts) && (taskid<maxtasks)) && (!count_done));
		     i++) {
			for (k=0; ((k<plane_size) && (!count_done)); k++) {
				if (taskid >= maxtasks) {
					count_done = true;
					break;
				}
				taskid++;
				job->alloc_lps[i]++;
			}
		}
	}
#if(0)	
	for (i = 0; i < job->nhosts; i++) {
		info("cons_res _cr_plane_dist %u host %s alloc_ lps %d ", 
		     job->job_id, job->host[i],  job->alloc_lps[i]);
	}
#endif
	for (host_index = 0; 
	     ((host_index < node_record_count) && (taskcount < job->nprocs));
	     host_index++) {
		struct node_cr_record *this_cr_node = NULL;
		int alloc_sockets = 0;
		int alloc_lps     = 0;
		int avail_cpus    = 0;
		if (bit_test(job->node_bitmap, host_index) == 0)
			continue;
		job_index++;
		
		this_cr_node = _find_cr_node_record(
			node_record_table_ptr[host_index].name);
		if (this_cr_node == NULL) {
			error("cons_res: could not find node %s", 
			      node_record_table_ptr[host_index].name);
			return SLURM_ERROR;
		}
		
		_get_resources_this_node(&usable_cpus, &usable_sockets, 
					 &usable_cores, &usable_threads, 
					 this_cr_node,  &alloc_sockets, 
					 &alloc_lps, &job->job_id);
		
		avail_cpus = slurm_get_avail_procs(job->max_sockets,
						       job->max_cores,
						       job->max_threads,
						       job->cpus_per_task,
						       job->ntasks_per_node,
						       job->ntasks_per_socket,
						       job->ntasks_per_core,
						       &usable_cpus,
						       &usable_sockets,
						       &usable_cores,
						       &usable_threads,
						       alloc_sockets,
						       alloc_lps,
						       cr_type);
		if (avail_cpus == 0) {
			error(" cons_res: no available cpus on node %s", 
			      node_record_table_ptr[host_index].name);
		}

		maxtasks = job->alloc_lps[job_index];
		last_socket_index = -1;
		next = 0;
		for (j=0; next<maxtasks; j++) {
			for (socket_index=0; 
			     ((socket_index<usable_sockets) 
			      && (next<maxtasks)); 
			     socket_index++) {
				max_plane_size = 
					(plane_size > usable_cores) 
					? plane_size : usable_cores;
				for (m=0; 
				     ((m<max_plane_size) & (next<maxtasks));
				     m++) {
					core_index = m%usable_cores;
					if(m > usable_cores) 
						continue;
					for(l=0; 
					    ((l<usable_threads)
					     && (next<maxtasks));
					    l++) {
						thread_index =
							l%usable_threads;
						if(thread_index 
						   > usable_threads)
							continue;
						if (last_socket_index
						    != socket_index) {
							job->alloc_sockets
								[job_index]++;
							last_socket_index =
								socket_index;
						}
						next++;
					}
				}
			}
		}
#if(0)
		info("cons_res _cr_plane_dist %u host %d %s alloc_ "
		     "sockets %d lps %d ", 
		     job->job_id, host_index,  this_cr_node->node_ptr->name, 
		     job->alloc_sockets[job_index], job->alloc_lps[job_index]);
#endif
	}
	return SLURM_SUCCESS;
}

/* xfree a select_cr_job job */
static void _xfree_select_cr_job(struct select_cr_job *job)
{
	xfree(job->host);
	xfree(job->cpus);
	xfree(job->alloc_lps);
	xfree(job->alloc_sockets);
	xfree(job->alloc_memory);
	FREE_NULL_BITMAP(job->node_bitmap);
	xfree(job);
}

/* Free the select_cr_job_list list and the individual objects before
 * existing the plug-in.
 */
static void _clear_job_list(void)
{
	ListIterator job_iterator;
	struct select_cr_job *job;

	job_iterator = list_iterator_create(select_cr_job_list);
	while ((job = (struct select_cr_job *) list_next(job_iterator))) {
		list_remove(job_iterator);
		_xfree_select_cr_job(job);
	}

	list_iterator_destroy(job_iterator);
}

/* Append a specific select_cr_job to select_cr_job_list. If the
 * select_job already exists then it is deleted and re-added otherwise
 * it is just added to the list.
 */
static void _append_to_job_list(struct select_cr_job *new_job)
{
	int job_id = new_job->job_id;
	struct select_cr_job *old_job = NULL;

	ListIterator iterator = list_iterator_create(select_cr_job_list);
	while ((old_job = (struct select_cr_job *) list_next(iterator))) {
		if (old_job->job_id != job_id)
			continue;
		list_remove(iterator);	/* Delete record for JobId job_id */
		_xfree_select_cr_job(old_job);	/* xfree job structure */
		break;
	}

	list_iterator_destroy(iterator);
	list_append(select_cr_job_list, new_job);
	debug3 (" cons_res: _append_to_job_list job_id %d to list. "
		"list_count %d ", job_id, list_count(select_cr_job_list));
}

/*
 * _count_cpus - report how many cpus are available with the identified nodes 
 */
static void _count_cpus(unsigned *bitmap, int sum)
{
	int i, allocated_lps;
	sum = 0;

	for (i = 0; i < node_record_count; i++) {
		struct node_cr_record *this_node;
		allocated_lps = 0;
		if (bit_test(bitmap, i) != 1)
			continue;

		this_node = _find_cr_node_record(node_record_table_ptr[i].name);
		if (this_node == NULL) {
			error(" cons_res: Invalid Node reference %s ",
			      node_record_table_ptr[i].name);
			sum = 0;
			return;
		}

		switch(cr_type) {
		case CR_SOCKET:
		case CR_SOCKET_MEMORY:
			if (slurmctld_conf.fast_schedule) {
				sum += (node_record_table_ptr[i].config_ptr->sockets -
					this_node->alloc_sockets) * 
					node_record_table_ptr[i].config_ptr->cores *
					node_record_table_ptr[i].config_ptr->threads;
			} else {
				sum += (node_record_table_ptr[i].sockets - 
					this_node->alloc_sockets) 
					* node_record_table_ptr[i].cores
					* node_record_table_ptr[i].threads;
			}
			break;
		case CR_CORE:
		case CR_CORE_MEMORY:
		{
			/* FIXME */
			break;
		}
		case CR_MEMORY:
			if (slurmctld_conf.fast_schedule) {
				sum += node_record_table_ptr[i].config_ptr->cpus;
			} else {
				sum += node_record_table_ptr[i].cpus;
			}
			break;
		case CR_CPU:
		case CR_CPU_MEMORY:
		default:
			if (slurmctld_conf.fast_schedule) {
				sum += node_record_table_ptr[i].config_ptr->cpus -
					this_node->alloc_lps; 
			} else {
				sum += node_record_table_ptr[i].cpus -
					this_node->alloc_lps; 
			}
			break;
		}
	}
}

static int _synchronize_bitmaps(bitstr_t ** partially_idle_bitmap)
{
	int rc = SLURM_SUCCESS, i;
	bitstr_t *bitmap = bit_alloc(bit_size(avail_node_bitmap));

	debug3(" cons_res:  Synch size avail %d size idle %d ",
	       bit_size(avail_node_bitmap), bit_size(idle_node_bitmap));

	for (i = 0; i < node_record_count; i++) {
		int allocated_cpus;
		if (bit_test(avail_node_bitmap, i) != 1)
			continue;

		if (bit_test(idle_node_bitmap, i) == 1) {
			bit_set(bitmap, i);
			continue;
		}

		allocated_cpus = 0;
		rc = select_g_get_select_nodeinfo(&node_record_table_ptr
						  [i], SELECT_ALLOC_CPUS,
						  &allocated_cpus);
		if (rc != SLURM_SUCCESS) {
			error(" cons_res: Invalid Node reference %s",
			      node_record_table_ptr[i].name);
			goto cleanup;
		}

		if (allocated_cpus < node_record_table_ptr[i].cpus)
			bit_set(bitmap, i);
		else
			bit_clear(bitmap, i);
	}

	*partially_idle_bitmap = bitmap;
	if (rc == SLURM_SUCCESS)
		return rc;

      cleanup:
	FREE_NULL_BITMAP(bitmap);
	return rc;
}

static int _clear_select_jobinfo(struct job_record *job_ptr)
{
	int rc = SLURM_SUCCESS, i, nodes, job_id;
	struct select_cr_job *job = NULL;
	ListIterator iterator;

	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);

	if (list_count(select_cr_job_list) == 0)
		return rc;

	job_id = job_ptr->job_id;
	iterator = list_iterator_create(select_cr_job_list);
	while ((job = (struct select_cr_job *) list_next(iterator))) {
		if (job->job_id != job_id)
			continue;
		if (job->state & CR_JOB_STATE_SUSPENDED)
			nodes = 0;
		else
			nodes = job->nhosts;
		for (i = 0; i < nodes; i++) {
			struct node_cr_record *this_node;
			this_node = _find_cr_node_record(job->host[i]);
			if (this_node == NULL) {
				error(" cons_res: could not find node %s",
				      job->host[i]);
				rc = SLURM_ERROR; 
				goto out;
			}
			
			switch(cr_type) {
			case CR_SOCKET:
			case CR_SOCKET_MEMORY:
			case CR_CORE:
			case CR_CORE_MEMORY:
				/* Updating this node allocated resources */
				this_node->alloc_lps -= job->alloc_lps[i];
				this_node->alloc_sockets -= job->alloc_sockets[i];
				if ((this_node->alloc_lps < 0) || (this_node->alloc_sockets < 0)) {
					error(" alloc_lps < 0 %d on %s",
					      this_node->alloc_lps,
					      this_node->node_ptr->name);
					this_node->alloc_lps = 0;
					this_node->alloc_sockets = 0;
					rc = SLURM_ERROR;  
					goto out;
				}
				if ((cr_type == CR_SOCKET) || (cr_type == CR_CORE))
					break;
			case CR_MEMORY:
				this_node->alloc_memory -= job->alloc_memory[i];
				if (this_node->alloc_memory < 0) {
					error(" alloc_memory < 0 %d on %s",
					      this_node->alloc_memory,
					      this_node->node_ptr->name);
					this_node->alloc_memory = 0;
					rc = SLURM_ERROR;  
					goto out;
				}
				break;
			case CR_CPU:
			case CR_CPU_MEMORY:
				this_node->alloc_lps -= job->alloc_lps[i];
				if (this_node->alloc_lps < 0) {
					error(" alloc_lps < 0 %d on %s",
					      this_node->alloc_lps,
					      this_node->node_ptr->name);
					this_node->alloc_lps = 0;
					rc = SLURM_ERROR;  
					goto out;
				}
				if (cr_type == CR_CPU)
					break;

				this_node->alloc_memory -= job->alloc_memory[i];
				if (this_node->alloc_memory < 0) {
					error(" alloc_memory < 0 %d on %s",
					      this_node->alloc_memory,
					      this_node->node_ptr->name);
					this_node->alloc_memory = 0;
					rc = SLURM_ERROR;  
					goto out;
				}
				break;
			default:
				break;
			}
#if(1)
			info("cons_res %u _clear_select_jobinfo (-) node %s alloc_ lps %d sockets %d ",
			     job->job_id, this_node->node_ptr->name, this_node->alloc_lps, 
			     this_node->alloc_sockets);
#endif
		}
	out:
		list_remove(iterator);
		_xfree_select_cr_job(job);
		break;
	}
	list_iterator_destroy(iterator);

	debug3("cons_res: _clear_select_jobinfo Job_id %u: "
	       "list_count: %d", job_ptr->job_id, 
	       list_count(select_cr_job_list));

	return rc;
}

static bool
_enough_nodes(int avail_nodes, int rem_nodes, 
	      uint32_t min_nodes, uint32_t req_nodes)
{
	int needed_nodes;

	if (req_nodes > min_nodes)
		needed_nodes = rem_nodes + min_nodes - req_nodes;
	else
		needed_nodes = rem_nodes;

	return (avail_nodes >= needed_nodes);
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init(void)
{
#ifdef HAVE_XCPU
	error("%s presently incompatible with XCPU use", plugin_name);
	return SLURM_ERROR;
#endif

	cr_type = (select_type_plugin_info_t)
			slurmctld_conf.select_type_param;
	info("%s loaded with argument %d ", plugin_name, cr_type);

	return SLURM_SUCCESS;
}

extern int fini(void)
{
	_clear_job_list();
	if (select_cr_job_list);
		list_destroy(select_cr_job_list);
	select_cr_job_list = NULL;
	xfree(select_node_ptr);
	select_node_ptr = NULL;
	select_node_cnt = -1;
	xfree(cr_node_hash_table);

	verbose("%s shutting down ...", plugin_name);
	return SLURM_SUCCESS;
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
    	if (!select_cr_job_list) {
		select_cr_job_list = list_create(NULL);
	}

	return SLURM_SUCCESS;
}

extern int select_p_node_init(struct node_record *node_ptr, int node_cnt)
{
	int i;

	if (node_ptr == NULL) {
		error("select_g_node_init: node_ptr == NULL");
		return SLURM_ERROR;
	}

	if (node_cnt < 0) {
		error("select_g_node_init: node_cnt < 0");
		return SLURM_ERROR;
	}

	select_node_cnt = node_cnt;
	select_node_ptr =
	    xmalloc(sizeof(struct node_cr_record) *
		    (select_node_cnt));

	for (i = 0; i < select_node_cnt; i++) {
		select_node_ptr[i].node_ptr = &node_ptr[i];
		select_node_ptr[i].alloc_lps     = 0;
		select_node_ptr[i].alloc_sockets = 0;
		select_node_ptr[i].alloc_memory  = 0;
	}

	select_fast_schedule = slurm_get_fast_schedule();

	_build_cr_node_hash_table();

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
 * IN job_ptr - pointer to job being scheduled
 * IN/OUT bitmap - usable nodes are set on input, nodes not required to 
 *	satisfy the request are cleared, other left set
 * IN min_nodes - minimum count of nodes
 * IN req_nodes - requested (or desired) count of nodes
 * IN max_nodes - maximum count of nodes (0==don't care)
 * IN test_only - if true, only test if ever could run, not necessarily now,
 *	not used in this implementation
 * RET zero on success, EINVAL otherwise
 * globals (passed via select_p_node_init): 
 *	node_record_count - count of nodes configured
 *	node_record_table_ptr - pointer to global node table
 * NOTE: the job information that is considered for scheduling includes:
 *	req_node_bitmap: bitmap of specific nodes required by the job
 *	contiguous: allocated nodes must be sequentially located
 *	num_procs: minimum number of processors required by the job
 * NOTE: bitmap must be a superset of req_nodes at the time that 
 *	select_p_job_test is called
 */
extern int select_p_job_test(struct job_record *job_ptr, bitstr_t * bitmap,
			     uint32_t min_nodes, uint32_t max_nodes, 
			     uint32_t req_nodes, bool test_only)
{
	int i, index, error_code = SLURM_ERROR, sufficient;
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
	//int asockets, acores, athreads, acpus;
	bool all_avail = false;
	
	xassert(bitmap);

	consec_index = 0;
	consec_size = 50;	/* start allocation for 50 sets of 
				 * consecutive nodes */
	consec_cpus = xmalloc(sizeof(int) * consec_size);
	consec_nodes = xmalloc(sizeof(int) * consec_size);
	consec_start = xmalloc(sizeof(int) * consec_size);
	consec_end = xmalloc(sizeof(int) * consec_size);
	consec_req = xmalloc(sizeof(int) * consec_size);

	/* Build table with information about sets of consecutive nodes */
	consec_cpus[consec_index] = consec_nodes[consec_index] = 0;
	consec_req[consec_index] = -1;	/* no required nodes here by default */

        /* This is the case if -O/--overcommit  is true */ 
	debug3("job_ptr->num_procs %d", job_ptr->num_procs);
	if (job_ptr->num_procs == job_ptr->details->min_nodes) {
		job_ptr->num_procs *= MAX(1,job_ptr->details->min_threads);
		job_ptr->num_procs *= MAX(1,job_ptr->details->min_cores);
		job_ptr->num_procs *= MAX(1,job_ptr->details->min_sockets);
	}

	rem_cpus = job_ptr->num_procs;
	if (req_nodes > min_nodes)
		rem_nodes = req_nodes;
	else
		rem_nodes = min_nodes;
	for (index = 0; index < select_node_cnt; index++) {
		if (bit_test(bitmap, index)) {
			if (consec_nodes[consec_index] == 0)
				consec_start[consec_index] = index;
			if (!test_only)
				all_avail = false;
			else
				all_avail = true;
			avail_cpus = _get_avail_lps(job_ptr, index, all_avail);

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
			} else {	/* node not required (yet) */
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
	
	/* accumulate nodes from these sets of consecutive nodes until */
	/*   sufficient resources have been accumulated */
	while (consec_index && (max_nodes > 0)) {
		best_fit_cpus = best_fit_nodes = best_fit_sufficient = 0;
		best_fit_req = -1;	/* first required node, -1 if none */
		for (i = 0; i < consec_index; i++) {
			if (consec_nodes[i] == 0)
				continue;
			sufficient =  (consec_cpus[i] >= rem_cpus)
				&& _enough_nodes(consec_nodes[i], rem_nodes,
						 min_nodes, req_nodes);
			
			/* if first possibility OR */
			/* contains required nodes OR */
			/* first set large enough for request OR */
			/* tightest fit (less resource waste) OR */
			/* nothing yet large enough, but this is biggest */
			if ((best_fit_nodes == 0) ||
			    ((best_fit_req == -1) && (consec_req[i] != -1))
			    || (sufficient && (best_fit_sufficient == 0))
			    || (sufficient
				&& (consec_cpus[i] < best_fit_cpus))
			    || ((sufficient == 0)
				&& (consec_cpus[i] > best_fit_cpus))) {
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
				if (!test_only)
					all_avail = false;
				else
					all_avail = true;
				avail_cpus = _get_avail_lps(job_ptr, i, all_avail);
				rem_cpus -= avail_cpus;
			}
			for (i = (best_fit_req - 1);
			     i >= consec_start[best_fit_location]; i--) {
				if ((max_nodes <= 0)
				    ||  ((rem_nodes <= 0) && (rem_cpus <= 0)))
					break;
				if (bit_test(bitmap, i)) 
					continue;
				if (!test_only)
					all_avail = false;
				else
					all_avail = true;
				avail_cpus = _get_avail_lps(job_ptr, i, all_avail);
				if(avail_cpus <= 0)
					continue;
				rem_cpus -= avail_cpus;
				bit_set(bitmap, i);
				rem_nodes--;
				max_nodes--;
			}
		} else {
			for (i = consec_start[best_fit_location];
			     i <= consec_end[best_fit_location]; i++) {
				if ((max_nodes <= 0)
				    || ((rem_nodes <= 0) && (rem_cpus <= 0)))
					break;
				if (bit_test(bitmap, i))
					continue;
				if (!test_only)
					all_avail = false;
				else
					all_avail = true;
				avail_cpus = _get_avail_lps(job_ptr, i, all_avail);
				if(avail_cpus <= 0)
					continue;
				rem_cpus -= avail_cpus;
				bit_set(bitmap, i);
				rem_nodes--;
				max_nodes--;
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
	    && _enough_nodes(0, rem_nodes, min_nodes, req_nodes))
		error_code = SLURM_SUCCESS;

	if (error_code != SLURM_SUCCESS)
		goto cleanup;
	
	if (!test_only) {
		int jobid, job_nodecnt, j;
		bitoff_t size;
		static struct select_cr_job *job;
		job = xmalloc(sizeof(struct select_cr_job));
		jobid = job_ptr->job_id;
		job->job_id = jobid;
		job_nodecnt = bit_set_count(bitmap);
		job->nhosts = job_nodecnt;
		job->nprocs = MAX(job_ptr->num_procs, job_nodecnt);
		job->max_sockets = job_ptr->details->max_sockets;
		job->max_cores = job_ptr->details->max_cores;
		job->max_threads = job_ptr->details->max_threads;
		job->cpus_per_task = job_ptr->details->cpus_per_task;
		job->ntasks_per_node = job_ptr->details->ntasks_per_node;
		job->ntasks_per_socket = job_ptr->details->ntasks_per_socket;
		job->ntasks_per_core = job_ptr->details->ntasks_per_core;

		size = bit_size(bitmap);
		job->node_bitmap = (bitstr_t *) bit_alloc(size);
		if (job->node_bitmap == NULL)
			fatal("bit_alloc malloc failure");
		for (i = 0; i < size; i++) {
			if (!bit_test(bitmap, i))
				continue;
			bit_set(job->node_bitmap, i);
		}
		
		job->host = (char **) xmalloc(job->nhosts * sizeof(char *));
		job->cpus = (int *) xmalloc(job->nhosts * sizeof(int));

		/* Build number of needed lps for each hosts for this job */
		job->alloc_lps     = (int *) xmalloc(job->nhosts * sizeof(int));
		job->alloc_sockets = (int *) xmalloc(job->nhosts * sizeof(int));
		job->alloc_memory  = (int *) xmalloc(job->nhosts * sizeof(int));

		j = 0;
		for (i = 0; i < node_record_count; i++) {
			if (bit_test(bitmap, i) == 0)
				continue;
			job->host[j] = node_record_table_ptr[i].name;
			job->cpus[j] = node_record_table_ptr[i].cpus;
			job->alloc_memory[j] = job_ptr->details->job_max_memory; 
			job->alloc_lps[j]     = 0;
			job->alloc_sockets[j] = 0;
			j++;
		}
		
		/* check for error SMB Fixme */
		debug3("cons_res %u task_dist %d", job_ptr->job_id, job_ptr->details->task_dist);
		if (job_ptr->details->shared == 0) {
			/* Nodes need to be allocated in dedicated
			   mode. User has specified the --exclusive
			   switch */
			error_code = _cr_exclusive_dist(job);
		} else {
			/* Determine the number of logical processors
			   per node needed for this job */
                        /* Make sure below matches the layouts in
			 * lllp_distribution in
			 * plugins/task/affinity/dist_task.c */
			switch(job_ptr->details->task_dist) {
			case SLURM_DIST_BLOCK_BLOCK:
			case SLURM_DIST_CYCLIC_BLOCK:
				error_code = _cr_dist(job, 0);
				break;
			case SLURM_DIST_BLOCK:
			case SLURM_DIST_CYCLIC:				
			case SLURM_DIST_BLOCK_CYCLIC:
			case SLURM_DIST_CYCLIC_CYCLIC:
			case SLURM_DIST_UNKNOWN:
				error_code = _cr_dist(job, 1);
				break;
			case SLURM_DIST_PLANE:
				error_code = _cr_plane_dist(job, 
							    job_ptr->details->plane_size);
				break;
			case SLURM_DIST_ARBITRARY:
			default:
				error_code = _compute_c_b_task_dist(job);
				if (error_code != SLURM_SUCCESS) {
					error(" Error in _compute_c_b_task_dist");
					return error_code;
				}
				break;
			}
		}
		if (error_code != SLURM_SUCCESS)
			goto cleanup;
#if(0)
		/* debugging only Remove */
		for (i = 0; i < job->nhosts; i++) {
			debug3("cons_res: job: %u after _cr_dist host %s cpus %u alloc_lps %d alloc_sockets %d",
			       job->job_id, job->host[i], job->cpus[i], 
			       job->alloc_lps[i], job->alloc_sockets[i]);
		}
#endif

		_append_to_job_list(job);
	}
	
 cleanup:
	xfree(consec_cpus);
	xfree(consec_nodes);
	xfree(consec_start);
	xfree(consec_end);
	xfree(consec_req);
	return error_code;
}

extern int select_p_job_begin(struct job_record *job_ptr)
{
	ListIterator job_iterator;
	struct select_cr_job *job;
	uint32_t cnt = 0;
	int i;

	xassert(job_ptr);
	xassert(select_cr_job_list);

	/* set job's processor count (for accounting purposes) */
	job_iterator = list_iterator_create(select_cr_job_list);
	if (job_iterator == NULL)
		fatal("list_iterator_create: %m");
	while ((job = (struct select_cr_job *) list_next(job_iterator))) {
		if (job->job_id != job_ptr->job_id)
			continue;
		for (i=0; i<job->nhosts; i++)
			cnt += MIN(job->cpus[i], job->alloc_lps[i]);
		if (job_ptr->num_procs != cnt) {
			debug2("cons_res: reset num_procs for %u from "
			       "%u to %u", 
			       job_ptr->job_id, job_ptr->num_procs, cnt);
			job_ptr->num_procs = cnt;
		}
		break;
	}
	list_iterator_destroy(job_iterator);

	return SLURM_SUCCESS;
}

extern int select_p_job_ready(struct job_record *job_ptr)
{
	return SLURM_SUCCESS;
}

extern int select_p_job_fini(struct job_record *job_ptr)
{
	int rc = SLURM_SUCCESS;
	rc = _clear_select_jobinfo(job_ptr);
	if (rc != SLURM_SUCCESS) {
		error(" error for %u in select/cons_res: "
		      "_clear_select_jobinfo",
		      job_ptr->job_id);
	}

	return rc;
}

extern int select_p_job_suspend(struct job_record *job_ptr)
{
	ListIterator job_iterator;
	struct select_cr_job *job;
	int i, rc = ESLURM_INVALID_JOB_ID;
 
	xassert(job_ptr);
	xassert(select_cr_job_list);

	job_iterator = list_iterator_create(select_cr_job_list);
	if (job_iterator == NULL)
		fatal("list_iterator_create: %m");
	while ((job = (struct select_cr_job *) list_next(job_iterator))) {
		if (job->job_id != job_ptr->job_id)
			continue;
		if (job->state & CR_JOB_STATE_SUSPENDED) {
			error("select: job %u already suspended",
				job->job_id);
			break;
		}
		job->state |= CR_JOB_STATE_SUSPENDED;
		for (i = 0; i < job->nhosts; i++) {
		        struct node_cr_record *this_node_ptr;
   		        this_node_ptr = _find_cr_node_record (job->host[i]);
			if (this_node_ptr == NULL) {
				error(" cons_res: could not find node %s",
					job->host[i]);
				rc = SLURM_ERROR; 
				goto cleanup;
			}
			this_node_ptr->alloc_lps -= job->alloc_lps[i];
			if (this_node_ptr->alloc_lps < 0) {
			        error(" cons_res: alloc_lps < 0 %d on %s",
				        this_node_ptr->alloc_lps,
				        this_node_ptr->node_ptr->name);
				this_node_ptr->alloc_lps = 0;
				rc = SLURM_ERROR; 
				goto cleanup;
			}
		}
		rc = SLURM_SUCCESS;
		break;
	}
     cleanup:
	list_iterator_destroy(job_iterator);

	return rc;
}

extern int select_p_job_resume(struct job_record *job_ptr)
{
	ListIterator job_iterator;
	struct select_cr_job *job;
	int i, rc = ESLURM_INVALID_JOB_ID;

	xassert(job_ptr);
	xassert(select_cr_job_list);

	job_iterator = list_iterator_create(select_cr_job_list);
	if (job_iterator == NULL)
		fatal("list_iterator_create: %m");
	while ((job = (struct select_cr_job *) list_next(job_iterator))) {
		if (job->job_id != job_ptr->job_id)
			continue;
		if ((job->state & CR_JOB_STATE_SUSPENDED) == 0) {
			error("select: job %s not suspended",
				job->job_id);
			break;
		}
		job->state &= (~CR_JOB_STATE_SUSPENDED);
		for (i = 0; i < job->nhosts; i++) {
		        struct node_cr_record *this_node;
		        this_node = _find_cr_node_record (job->host[i]);
			if (this_node == NULL) {
			        error(" cons_res: could not find node %s",
				        job->host[i]);
				rc = SLURM_ERROR;
				goto cleanup;
			}
			this_node->alloc_lps += job->alloc_lps[i];
		}
		rc = SLURM_SUCCESS;
		break;
	}
     cleanup:
	list_iterator_destroy(job_iterator);

	return rc;
}

extern int select_p_pack_node_info(time_t last_query_time,
				   Buf * buffer_ptr)
{
	/* This function is always invalid on normal Linux clusters */
	return SLURM_ERROR;
}

extern int select_p_get_extra_jobinfo(struct node_record *node_ptr,
				      struct job_record *job_ptr,
				      enum select_data_info cr_info,
				      void *data)
{
	int rc = SLURM_SUCCESS, i, avail = 0;
	uint32_t *tmp_32 = (uint32_t *) data;

	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);

	switch (cr_info) {
	case SELECT_AVAIL_MEMORY:
	{
		switch(cr_type) {		
		case CR_MEMORY:	
		case CR_CPU_MEMORY:	
		case CR_SOCKET_MEMORY:	
		case CR_CORE_MEMORY:	
			*tmp_32 = 0;
			for (i = 0; i < node_record_count; i++) {
				if (bit_test(job_ptr->details->req_node_bitmap, i) != 1)
					continue;
				avail = _get_avail_memory(i, false);
				if (avail < 0) {
					rc = SLURM_ERROR;
					return rc;
				}
			}
			break;
		default:
			*tmp_32 = 0;
		}
		break;
	}
	case SELECT_CPU_COUNT:
	{
		if ((job_ptr->details->cpus_per_task > 1) || 
		    (job_ptr->details->max_sockets > 1) ||
		    (job_ptr->details->max_cores > 1) ||
		    (job_ptr->details->max_threads > 1)) {
			*tmp_32 = 0;
			for (i = 0; i < node_record_count; i++) {
				if (bit_test(job_ptr->details->req_node_bitmap, i) != 1)
					continue;
				*tmp_32 += _get_avail_lps(job_ptr, i, false);
			}
		} else {
			_count_cpus(job_ptr->details->
				    req_node_bitmap, *tmp_32);
		}
		break;
	}
	case SELECT_AVAIL_CPUS:
	{
		struct select_cr_job *job = NULL;
		ListIterator iterator =
			list_iterator_create(select_cr_job_list);
		xassert(node_ptr);
		xassert(node_ptr->magic == NODE_MAGIC);
		
		while ((job =
			(struct select_cr_job *) list_next(iterator)) != NULL) {
			if (job->job_id != job_ptr->job_id)
				continue;
			for (i = 0; i < job->nhosts; i++) { 
				if (strcmp(node_ptr->name, job->host[i]) != 0)
					continue;
				/* Usable and "allocated" resources for this 
				 * given job for a specific node --> based 
				 * on the output from _cr_dist */
				switch(cr_type) {
				case CR_SOCKET:
				case CR_SOCKET_MEMORY:
					/* Number of hardware resources allocated
					   for this job. This might be more than
					   what the job requires since we
					   only allocated whole sockets at
					   this level */
					*tmp_32 = job->alloc_lps[i];
					break;
				case CR_CORE: 
				case CR_CORE_MEMORY: 
					/* Not yet implemented */
					break;
				case CR_MEMORY:
					*tmp_32 = node_ptr->cpus;
					break;
				case CR_CPU:
				case CR_CPU_MEMORY:
				default:
					*tmp_32 = job->alloc_lps[i];
					break;
				}
				goto cleanup;
			}
			error("cons_res could not find %s", node_ptr->name); 
			rc = SLURM_ERROR;
		}
		if (!job) {
			debug3("cons_res: job %d not active", job_ptr->job_id);
		}
	     cleanup:
		list_iterator_destroy(iterator);
		break;
	}
	default:
		error("select_g_get_extra_jobinfo cr_info %d invalid", cr_info);
		rc = SLURM_ERROR;
		break;
	}
	
	return rc;
}

extern int select_p_get_select_nodeinfo(struct node_record *node_ptr,
					enum select_data_info dinfo,
					void *data)
{
	int rc = SLURM_SUCCESS;
	struct node_cr_record *this_cr_node;

	xassert(node_ptr);
	xassert(node_ptr->magic == NODE_MAGIC);

	switch (dinfo) {
	case SELECT_AVAIL_MEMORY:
	case SELECT_ALLOC_MEMORY: 
	{
		uint32_t *tmp_32 = (uint32_t *) data;
		switch(cr_type) {
		case CR_MEMORY:
		case CR_SOCKET_MEMORY:
		case CR_CORE_MEMORY:
		case CR_CPU_MEMORY:
			this_cr_node = _find_cr_node_record (node_ptr->name);
			if (this_cr_node == NULL) {
				error(" cons_res: could not find node %s",
				      node_ptr->name);
				rc = SLURM_ERROR;
				return rc;
			}
			if (dinfo == SELECT_ALLOC_MEMORY) {
				*tmp_32 = this_cr_node->alloc_memory;
			} else  
				*tmp_32 = 
					this_cr_node->node_ptr->real_memory - 
					this_cr_node->alloc_memory;
			break;
		default:
			*tmp_32 = 0;
			break;
		}
		break;
	}
	case SELECT_ALLOC_CPUS: 
	{
	        this_cr_node = _find_cr_node_record (node_ptr->name);
		if (this_cr_node == NULL) {
		        error(" cons_res: could not find node %s",
			      node_ptr->name);
			rc = SLURM_ERROR;
			return rc;
		}
		uint32_t *tmp_32 = (uint32_t *) data;
		switch(cr_type) {
		case CR_SOCKET:
		case CR_SOCKET_MEMORY:
			*tmp_32 = this_cr_node->alloc_sockets *
			        node_ptr->cores * node_ptr->threads;
			break;
		case CR_CORE:
		case CR_CORE_MEMORY:
                        /* FIXME */
			break;
		case CR_MEMORY:
			*tmp_32 = 0;
		case CR_CPU:
		case CR_CPU_MEMORY:
		default:
			*tmp_32 = this_cr_node->alloc_lps;
			break;
		}
		break;
	}
	default:
		error("select_g_get_select_nodeinfo info %d invalid", dinfo);
		rc = SLURM_ERROR;
		break;
	}
	return rc;
}

extern int select_p_update_nodeinfo(struct job_record *job_ptr)
{
	int rc = SLURM_SUCCESS, i, job_id, nodes;
	struct select_cr_job *job = NULL;
	ListIterator iterator;

	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);
	job_id = job_ptr->job_id;

	iterator = list_iterator_create(select_cr_job_list);
	while ((job = (struct select_cr_job *) list_next(iterator)) 
	       != NULL) {
		if (job->job_id != job_id)
			continue;
		if (job->state & CR_JOB_STATE_SUSPENDED)
			nodes = 0;
		else
			nodes = job->nhosts;
		for (i = 0; i < nodes; i++) {
			struct node_cr_record *this_node;
			this_node = _find_cr_node_record (job->host[i]);
			if (this_node == NULL) {
				error(" cons_res: could not find node %s",
				      job->host[i]);
				rc = SLURM_ERROR;
				goto cleanup;
			}
			switch (cr_type) {
			case CR_SOCKET:
			case CR_CORE:
			case CR_SOCKET_MEMORY:
			case CR_CORE_MEMORY:
				/* Updating this node's allocated resources */
				this_node->alloc_lps     += job->alloc_lps[i];
				this_node->alloc_sockets += job->alloc_sockets[i];
				if (this_node->alloc_sockets > this_node->node_ptr->sockets)
					error("Job %u Host %s too many allocated sockets %d",
					      job->job_id, this_node->node_ptr->name, 
					      this_node->alloc_sockets);
				if ((cr_type == CR_SOCKET) 
				    || (cr_type == CR_CORE))
					break;
			case CR_MEMORY: 
				this_node->alloc_memory += job->alloc_memory[i];
				break;
			case CR_CPU:
			case CR_CPU_MEMORY:
				this_node->alloc_lps     += job->alloc_lps[i];				
				if (cr_type == CR_CPU) 
					break;				
				this_node->alloc_memory += job->alloc_memory[i];
				break;
			default:
				error("select_g_update_nodeinfo info %d invalid", cr_type);
				rc = SLURM_ERROR;
				break;
			}
#if(1)
			/* Remove debug only */
			info("cons_res %u update_nodeinfo (+) node %s alloc_ lps %d sockets %d mem %d ",
			     job->job_id, this_node->node_ptr->name, this_node->alloc_lps, 
			     this_node->alloc_sockets, this_node->alloc_memory);
#endif
		}
	}
 cleanup:
	list_iterator_destroy(iterator);
	
	return rc;
}

extern int select_p_update_block (update_part_msg_t *part_desc_ptr)
{
	return SLURM_SUCCESS;
}

extern int select_p_get_info_from_plugin(enum select_data_info info,
					 void *data)
{
	int rc = SLURM_SUCCESS;

	switch (info) {
	case SELECT_BITMAP:
	{
		bitstr_t **bitmap = (bitstr_t **) data;
		bitstr_t *tmp_bitmap = NULL;
		
		rc = _synchronize_bitmaps(&tmp_bitmap);
		if (rc != SLURM_SUCCESS) {
			FREE_NULL_BITMAP(tmp_bitmap);
			return rc;
		}
		*bitmap = tmp_bitmap;	/* Ownership transfer, 
					 * Remember to free bitmap 
					 * using FREE_NULL_BITMAP(bitmap);*/
		tmp_bitmap = 0;
		break;
	}
	case SELECT_CR_PLUGIN:
	{
		uint32_t *tmp_32 = (uint32_t *) data;
		*tmp_32 = 1;
		break;
	}
	default:
		error("select_g_get_info_from_plugin info %d invalid",
		      info);
		rc = SLURM_ERROR;
		break;
	}
	return rc;
}

extern int select_p_alter_node_cnt(enum select_node_cnt type, void *data)
{
	return SLURM_SUCCESS;
}
