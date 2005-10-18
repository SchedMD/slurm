/*****************************************************************************\
 *  select_cons_res.c - node selection plugin supporting consumable 
 *  resources policies. The current version only support processors as 
 *  consumable resources.
 *  We expect to be able to support additional resources as part of future work.
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
 *
 *****************************************************************************
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
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
#include "src/slurmctld/slurmctld.h"

#define __SELECT_CR_DEBUG 0

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

/* node_used_resources keeps track of the resources within a node that 
 * have been reserved by already scheduled jobs. 
 */
struct node_resource_table {
	struct node_record *node_ptr;	/* ptr to the node that own these resources */
	uint32_t used_cpus;	/* cpu count reserved by already scheduled jobs */
};

struct select_cr_job {
	uint32_t job_id;	/* job ID, default set by SLURM        */
	int nprocs;		/* --nprocs=n,      -n n               */
	int nhosts;		/* number of hosts allocated to job    */
	char **host;		/* hostname vector                     */
	int *cpus;		/* number of processors on each host   */
	int *ntask;		/* number of tasks to run on each host */
	bitstr_t *node_bitmap;	/* bitmap of nodes allocated to job    */
};

static struct node_resource_table *select_node_ptr;
static int select_node_cnt;
static uint16_t select_fast_schedule;
List select_cr_job_list;	/* List of select_cr_job(s) that are still active */

/* To effectively deal with heterogeneous nodes, we fake a cyclic
 * distribution to figure out how many tasks go on each node and then
 * make those assignments in a block fashion. 
 *
 * This routine is a slightly modified "copy" of the routine
 * _dist_block in src/srun/job.c. We do not need to assign tasks to
 * job->hostid[] and job->tids[][] at this point so the distribution/assigned 
 * tasks per node is the same for cyclic and block. 
 *
 * For the consumable resources support we need to determine the task
 * layout schema at this point. 
*/
static void _cr_dist(struct select_cr_job *job)
{
	int i, j, taskid = 0;
	bool over_subscribe = false;

	/* figure out how many tasks go to each node */
	for (j = 0; (taskid < job->nprocs); j++) {	/* cycle counter */
		bool space_remaining = false;
		for (i = 0; ((i < job->nhosts) && (taskid < job->nprocs));
		     i++) {
			if ((j < job->cpus[i]) || over_subscribe) {
				taskid++;
				job->ntask[i]++;
				if ((j + 1) < job->cpus[i])
					space_remaining = true;
			}
		}
		if (!space_remaining)
			over_subscribe = true;
	}
}

/* User has specified the --exclusive flag on the srun command line
 * which means that the job should use only dedicated nodes.  In this
 * case we do not need to compute the number of tasks on each nodes
 * since it should be set yo the number of cpus.
 */
static void _cr_exclusive_dist(struct select_cr_job *job)
{
	int i;

	for (i = 0; (i < job->nhosts); i++)
		job->ntask[i] = job->cpus[i];
}

/* xfree a select_cr_job job */
static void _xfree_select_cr_job(struct select_cr_job *job)
{
	xfree(job->host);
	xfree(job->cpus);
	xfree(job->ntask);
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
	while ((old_job =
		(struct select_cr_job *) list_next(iterator)) != NULL) {
		if (old_job->job_id != job_id)
			continue;
		list_remove(iterator);	/* Delete record for JobId job_id */
		_xfree_select_cr_job(old_job);	/* xfree job structure */
		break;
	}

	list_iterator_destroy(iterator);
	list_append(select_cr_job_list, new_job);
	debug3
	    (" cons_res: _append_to_job_list job_id %d to list. list_count %d ",
	     job_id, list_count(select_cr_job_list));
}

/*
 * _count_cr_cpus - report how many cpus are available with the identified nodes 
 */
static int _count_cr_cpus(unsigned *bitmap, int sum)
{
	int rc = SLURM_SUCCESS, i;
	sum = 0;

	for (i = 0; i < node_record_count; i++) {
		int allocated_cpus;
		if (bit_test(bitmap, i) != 1)
			continue;
		allocated_cpus = 0;
		rc = select_g_get_select_nodeinfo(&node_record_table_ptr
						  [i], SELECT_CR_USED_CPUS,
						  &allocated_cpus);
		if (rc != SLURM_SUCCESS) {
			error(" cons_res: Invalid Node reference %s ",
			      node_record_table_ptr[i].name);
			return rc;
		}

		if (slurmctld_conf.fast_schedule) {
			sum += node_record_table_ptr[i].config_ptr->cpus -
			    allocated_cpus;
		} else {
			sum +=
			    node_record_table_ptr[i].cpus - allocated_cpus;
		}
	}

	return rc;
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
						  [i], SELECT_CR_USED_CPUS,
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
	int rc = SLURM_SUCCESS, i, j;
	struct select_cr_job *job = NULL;
	int job_id;
	ListIterator iterator;

	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);

	if (list_count(select_cr_job_list) == 0)
		return rc;

	job_id = job_ptr->job_id;
	iterator = list_iterator_create(select_cr_job_list);
	while ((job =
		(struct select_cr_job *) list_next(iterator)) != NULL) {
		if (job->job_id != job_id)
			continue;
		for (i = 0; i < job->nhosts; i++) {
			for (j = 0; j < select_node_cnt; j++) {
				if (!bit_test(job->node_bitmap, j))
					continue;
				if (strcmp
				    (select_node_ptr[j].node_ptr->name,
				     job->host[i]) != 0)
					continue;
				select_node_ptr[j].used_cpus -=
				    job->ntask[i];
				if (select_node_ptr[j].used_cpus < 0) {
					error(" used_cpus < 0 %d on %s",
					      select_node_ptr[j].used_cpus,
					      select_node_ptr[j].node_ptr->
					      name);
					rc = SLURM_ERROR;
					list_remove(iterator);
					_xfree_select_cr_job(job);
					goto cleanup;
				}
			}
		}
		list_remove(iterator);
		_xfree_select_cr_job(job);
		goto cleanup;
	}

      cleanup:
	list_iterator_destroy(iterator);

	debug3
	    (" cons_res: _clear_select_jobinfo Job_id %u: list_count: %d",
	     job_ptr->job_id, list_count(select_cr_job_list));

	return rc;
}

static bool
_enough_nodes(int avail_nodes, int rem_nodes, int min_nodes, int max_nodes)
{
	int needed_nodes;

	if (max_nodes)
		needed_nodes = rem_nodes + min_nodes - max_nodes;
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
	verbose("%s loaded ", plugin_name);
	return SLURM_SUCCESS;
}

extern int fini(void)
{
	_clear_job_list();
	list_destroy(select_cr_job_list);
	select_cr_job_list = NULL;
	xfree(select_node_ptr);
	select_node_ptr = NULL;
	select_node_cnt = -1;
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
	select_cr_job_list = list_create(NULL);

	return SLURM_SUCCESS;
}

extern int select_p_node_init(struct node_record *node_ptr, int node_cnt)
{
	int i = 0;

	if (node_ptr == NULL) {
		error("select_p_node_init: node_ptr == NULL");
		return SLURM_ERROR;
	}

	if (node_cnt < 0) {
		error("select_p_node_init: node_cnt < 0");
		return SLURM_ERROR;
	}

	select_node_cnt = node_cnt;
	select_node_ptr =
	    xmalloc(sizeof(struct node_resource_table) *
		    (select_node_cnt));
	if (!select_node_ptr) {
		error("select_node_ptr == NULL");
		return SLURM_ERROR;
	}

	for (i = 0; i < select_node_cnt; i++) {
		select_node_ptr[i].node_ptr = &node_ptr[i];
		select_node_ptr[i].used_cpus = 0;
	}
	select_fast_schedule = slurm_get_fast_schedule();

	return SLURM_SUCCESS;
}

extern int select_p_part_init(List part_list)
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
 * IN max_nodes - maximum count of nodes (0==don't care)
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
			     int min_nodes, int max_nodes)
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
	int rem_cpus, rem_nodes;	/* remaining resources required */
	int best_fit_nodes, best_fit_cpus, best_fit_req;
	int best_fit_location = 0, best_fit_sufficient;

	/* Determine if consumable resources (of processors) is
	 * enabled. In some cases, select_p_job_test is called to see
	 * if a job can run if all the available resources were
	 * available to the job. We therefore need to be able to
	 * disable consumable resources (of processors). In the case
	 * where consumable resources (of processors) is disabled the
	 * code flow is similar to the select/linear plug-in.
	 */
	int cr_enabled = job_ptr->cr_enabled;

	xassert(bitmap);

	debug3
	    (" cons_res plug-in: Job_id %u min %d max nodes %d cr_enabled %d host %s ",
	     job_ptr->job_id, min_nodes, max_nodes, cr_enabled,
	     bitmap2node_name(bitmap));

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
	rem_cpus = job_ptr->num_procs;
	if (max_nodes)
		rem_nodes = max_nodes;
	else
		rem_nodes = min_nodes;
	for (index = 0; index < select_node_cnt; index++) {
		if (bit_test(bitmap, index)) {
			int allocated_cpus;
			if (consec_nodes[consec_index] == 0)
				consec_start[consec_index] = index;
			allocated_cpus = 0;
			if (cr_enabled) {
				error_code =
				    select_g_get_select_nodeinfo
				    (select_node_ptr[index].node_ptr,
				     SELECT_CR_USED_CPUS, &allocated_cpus);
				if (error_code != SLURM_SUCCESS)
					goto cleanup;
			}
			if (select_fast_schedule)
				/* don't bother checking each node */
				i = select_node_ptr[index].node_ptr->
				    config_ptr->cpus - allocated_cpus;
			else
				i = select_node_ptr[index].node_ptr->cpus -
				    allocated_cpus;
			if (job_ptr->details->req_node_bitmap &&
			    bit_test(job_ptr->details->req_node_bitmap,
				     index)) {
				if (consec_req[consec_index] == -1)
					/* first required node in set */
					consec_req[consec_index] = index;
				rem_cpus -= i;
				rem_nodes--;
			} else {	/* node not required (yet) */
				bit_clear(bitmap, index);
				consec_cpus[consec_index] += i;
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
	while (consec_index) {
		best_fit_cpus = best_fit_nodes = best_fit_sufficient = 0;
		best_fit_req = -1;	/* first required node, -1 if none */
		for (i = 0; i < consec_index; i++) {
			if (consec_nodes[i] == 0)
				continue;
			sufficient = ((consec_nodes[i] >= rem_nodes)
				      && (consec_cpus[i] >= rem_cpus));

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
				     min_nodes, max_nodes))))
			break;	/* no hole large enough */
		if (best_fit_req != -1) {
			/* This collection of nodes includes required ones
			 * select nodes from this set, first working up
			 * then down from the required nodes */
			for (i = best_fit_req;
			     i <= consec_end[best_fit_location]; i++) {
				int allocated_cpus;
				if ((rem_nodes <= 0) && (rem_cpus <= 0))
					break;
				if (bit_test(bitmap, i))
					continue;
				bit_set(bitmap, i);
				rem_nodes--;
				allocated_cpus = 0;
				if (cr_enabled) {
					error_code =
					    select_g_get_select_nodeinfo
					    (select_node_ptr[i].node_ptr,
					     SELECT_CR_USED_CPUS,
					     &allocated_cpus);
					if (error_code != SLURM_SUCCESS)
						goto cleanup;
				}
				if (select_fast_schedule)
					rem_cpus -=
					    select_node_ptr[i].node_ptr->
					    config_ptr->cpus -
					    allocated_cpus;
				else
					rem_cpus -=
					    select_node_ptr[i].node_ptr->
					    cpus - allocated_cpus;
			}
			for (i = (best_fit_req - 1);
			     i >= consec_start[best_fit_location]; i--) {
				int allocated_cpus;
				if ((rem_nodes <= 0) && (rem_cpus <= 0))
					break;
				/* if (bit_test(bitmap, i)) 
				   continue;  cleared above earlier */
				bit_set(bitmap, i);
				rem_nodes--;

				allocated_cpus = 0;
				if (cr_enabled) {
					error_code =
					    select_g_get_select_nodeinfo
					    (select_node_ptr[i].node_ptr,
					     SELECT_CR_USED_CPUS,
					     &allocated_cpus);
					if (error_code != SLURM_SUCCESS)
						goto cleanup;
				}

				if (select_fast_schedule)
					rem_cpus -=
					    select_node_ptr[i].node_ptr->
					    config_ptr->cpus -
					    allocated_cpus;
				else
					rem_cpus -=
					    select_node_ptr[i].node_ptr->
					    cpus - allocated_cpus;
			}
		} else {
			for (i = consec_start[best_fit_location];
			     i <= consec_end[best_fit_location]; i++) {
				int allocated_cpus;
				if ((rem_nodes <= 0) && (rem_cpus <= 0))
					break;
				if (bit_test(bitmap, i))
					continue;
				bit_set(bitmap, i);
				rem_nodes--;

				allocated_cpus = 0;
				if (cr_enabled) {
					error_code =
					    select_g_get_select_nodeinfo
					    (select_node_ptr[i].node_ptr,
					     SELECT_CR_USED_CPUS,
					     &allocated_cpus);
					if (error_code != SLURM_SUCCESS)
						goto cleanup;
				}

				if (select_fast_schedule)
					rem_cpus -=
					    select_node_ptr[i].node_ptr->
					    config_ptr->cpus -
					    allocated_cpus;
				else
					rem_cpus -=
					    select_node_ptr[i].node_ptr->
					    cpus - allocated_cpus;
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
	    ((max_nodes == 0) || ((max_nodes - rem_nodes) >= min_nodes)))
		error_code = SLURM_SUCCESS;

	if (error_code != SLURM_SUCCESS)
		goto cleanup;

	if (cr_enabled) {
		int jobid, job_nodecnt, j;
		bitoff_t size;
		static struct select_cr_job *job;
		job = xmalloc(sizeof(struct select_cr_job));
		jobid = job_ptr->job_id;
		job->job_id = jobid;
		job_nodecnt = bit_set_count(bitmap);
		job->nhosts = job_nodecnt;
		job->nprocs = job_ptr->num_procs;

		size = bit_size(bitmap);
		job->node_bitmap = (bitstr_t *) bit_alloc(size);
		if (job->node_bitmap == NULL)
			fatal("bit_alloc malloc failure");
		for (i = 0; i < size; i++) {
			if (!bit_test(bitmap, i))
				continue;
			bit_set(job->node_bitmap, i);
		}

		job->host =
		    (char **) xmalloc(job->nhosts * sizeof(char *));
		job->cpus = (int *) xmalloc(job->nhosts * sizeof(int));

		j = 0;
		for (i = 0; i < node_record_count; i++) {
			if (bit_test(bitmap, i) == 0)
				continue;
			job->host[j] = node_record_table_ptr[i].name;
			job->cpus[j] = node_record_table_ptr[i].cpus;
			j++;
		}

		/* Build number of tasks for each hosts */
		job->ntask = (int *) xmalloc(job->nhosts * sizeof(int));

		if (job_ptr->details->exclusive) {
			/* Nodes need to be allocated in dedicated
			   mode. User has specified the --exclusive
			   switch */
			_cr_exclusive_dist(job);
		} else {
			/* Determine the number of cpus per node needed for
			   this tasks */
			_cr_dist(job);
		}

#if (__SELECT_CR_DEBUG)
		for (i = 0; i < job->nhosts; i++)
			debug3(" cons_res: after _cr_dist host %s cpus %u",
			       job->host[i], job->ntask[i]);
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
		error
		    (" Error for %u in select/cons_res:_clear_select_jobinfo",
		     job_ptr->job_id);
	}

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
				      enum select_data_info info,
				      void *data)
{
	int rc = SLURM_SUCCESS, i;

	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);

	xassert(node_ptr);
	xassert(node_ptr->magic == NODE_MAGIC);

	switch (info) {
	case SELECT_CR_CPU_COUNT:
		{
			uint32_t *tmp_32 = (uint32_t *) data;
			rc = _count_cr_cpus(job_ptr->details->
					    req_node_bitmap, *tmp_32);
			if (rc != SLURM_SUCCESS)
				return rc;
			break;
		}
	case SELECT_CR_USABLE_CPUS:
		{
			uint32_t *tmp_32 = (uint32_t *) data;
			struct select_cr_job *job = NULL;
			ListIterator iterator =
			    list_iterator_create(select_cr_job_list);
			while ((job =
				(struct select_cr_job *)
				list_next(iterator)) != NULL) {
				if (job->job_id != job_ptr->job_id)
					continue;
				for (i = 0; i < job->nhosts; i++) {
					if (strcmp
					    (node_ptr->name,
					     job->host[i]) == 0) {
#if (__SELECT_CR_DEBUG)
						debug3
						    (" cons_res: get_extra_jobinfo job_id %u %s tasks %d ",
						     job->job_id,
						     job->host[i],
						     job->ntask[i]);
#endif
						*tmp_32 = job->ntask[i];
						goto cleanup;
					}
				}
			}
		      cleanup:
			list_iterator_destroy(iterator);
			break;
		}
	default:
		error("select_g_get_extra_jobinfo info %d invalid", info);
		rc = SLURM_ERROR;
		break;
	}

	return rc;
}

extern int select_p_get_select_nodeinfo(struct node_record *node_ptr,
					enum select_data_info info,
					void *data)
{
	int rc = SLURM_SUCCESS, i;
	int incr = -1;

	xassert(node_ptr);
	xassert(node_ptr->magic == NODE_MAGIC);

	switch (info) {
	case SELECT_CR_USED_CPUS:
		for (i = 0; i < select_node_cnt; i++)
			if (strcmp
			    (select_node_ptr[i].node_ptr->name,
			     node_ptr->name) == 0) {
				incr = i;
				break;
			}

		if (incr >= 0) {
			uint32_t *tmp_32 = (uint32_t *) data;
			*tmp_32 = select_node_ptr[incr].used_cpus;
		} else {
			error
			    ("select_g_get_select_nodeinfo: no node record match ");
			rc = SLURM_ERROR;
		}
		break;
	default:
		error("select_g_get_select_nodeinfo info %d invalid",
		      info);
		rc = SLURM_ERROR;
		break;
	}

	return rc;
}

extern int select_p_update_nodeinfo(struct job_record *job_ptr,
				    enum select_data_info info)
{
	int rc = SLURM_SUCCESS, i, j, job_id;

	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);
	job_id = job_ptr->job_id;

	switch (info) {
	case SELECT_CR_USED_CPUS:
		{
			struct select_cr_job *job = NULL;
			ListIterator iterator =
			    list_iterator_create(select_cr_job_list);
			while ((job =
				(struct select_cr_job *)
				list_next(iterator)) != NULL) {
				if (job->job_id != job_id)
					continue;
				for (i = 0; i < job->nhosts; i++) {
					for (j = 0; j < select_node_cnt;
					     j++) {
						if (bit_test
						    (job->node_bitmap,
						     j) == 0)
							continue;
						if (strcmp
						    (select_node_ptr[j].
						     node_ptr->name,
						     job->host[i]) == 0) {
							select_node_ptr[j].
							    used_cpus +=
							    job->ntask[i];
							continue;
						}
					}
				}
				goto cleanup;
			}

		      cleanup:
			list_iterator_destroy(iterator);
		}
		break;
	default:
		error("select_g_update_nodeinfo info %d invalid", info);
		rc = SLURM_ERROR;
		break;
	}
	return rc;
}

extern int select_p_get_info_from_plugin(enum select_data_info info,
					 void *data)
{
	int rc = SLURM_SUCCESS;

	switch (info) {
	case SELECT_CR_BITMAP:
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
			debug3(" cons_res synchronized CR bitmap %s ",
			       bitmap2node_name(*bitmap));
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

#undef __SELECT_CR_DEBUG
