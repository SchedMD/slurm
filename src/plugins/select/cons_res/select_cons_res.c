/*****************************************************************************\
 *  select_cons_res.c - node selection plugin supporting consumable 
 *  resources policies.
 *
 *  $Id$
 *****************************************************************************\
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

#include "select_cons_res.h"
#include "dist_tasks.h"

#if(0)
#define CR_DEBUG 1
#endif

#if 0
#define CR_DEBUG 1
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
const uint32_t pstate_version = 3;	/* version control on saved state */

#define CR_JOB_STATE_SUSPENDED 1

select_type_plugin_info_t cr_type = CR_CPU; /* cr_type is overwritten in init() */

/* Array of node_cr_record. One entry for each node in the cluster */
static struct node_cr_record *select_node_ptr = NULL;
static int select_node_cnt = 0;
static struct node_cr_record **cr_node_hash_table = NULL;
static time_t last_cr_update_time;
static pthread_mutex_t cr_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Restored node_cr_records - used by select_p_state_restore/node_init */
static struct node_cr_record *prev_select_node_ptr = NULL;
static int prev_select_node_cnt = 0;

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
 * find_cr_node_record - find a record for node with specified name
 * input: name - name of the desired node 
 * output: return pointer to node record or NULL if not found
 * global: select_node_ptr - pointer to global select_node_ptr
 *         cr_node_hash_table - table of hash indecies
 * Inspired from find_node_record (char *name) in slurmctld/node_mgr.c 
 */
struct node_cr_record * find_cr_node_record (const char *name) 
{
	int i;

	if ((name == NULL)
	||  (name[0] == '\0')) {
		info("find_cr_node_record passed NULL name");
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
		error ("find_cr_node_record: lookup failure using hashtable for %s", 
                        name);
	} 

	/* revert to sequential search */
	else {
		for (i = 0; i < select_node_cnt; i++) {
		        if (strcmp (name, select_node_ptr[i].node_ptr->name) == 0) {
			        debug3("cons_res find_cr_node_record: linear %s",  name);
				return (&select_node_ptr[i]);
			}
		}
		error ("find_cr_node_record: lookup failure with linear search for %s", 
                        name);
	}
	error ("find_cr_node_record: lookup failure with both method %s", name);
	return (struct node_cr_record *) NULL;
}

void chk_resize_node(struct node_cr_record *node, uint16_t sockets)
{
	if ((node->alloc_cores == NULL) ||
			(sockets > node->num_sockets)) {
		debug3("cons_res: increasing node %s num_sockets from %u to %u",
			node->node_ptr->name, node->num_sockets, sockets);
	    	xrealloc(node->alloc_cores, sockets * sizeof(uint16_t));
		/* NOTE: xrealloc zero fills added memory */
		node->num_sockets = sockets;
	}
}

void chk_resize_job(struct select_cr_job *job, uint16_t node_id, uint16_t sockets)
{
	if ((job->alloc_cores[node_id] == NULL) ||
	    		(sockets > job->num_sockets[node_id])) {
		debug3("cons_res: increasing job %u node %u num_sockets from %u to %u",
			job->job_id, node_id, job->num_sockets[node_id], sockets);
	    	xrealloc(job->alloc_cores[node_id], sockets * sizeof(uint16_t));
		/* NOTE: xrealloc zero fills added memory */
		job->num_sockets[node_id] = sockets;
	}
}

void get_resources_this_node(uint16_t *cpus, 
			     uint16_t *sockets, 
			     uint16_t *cores,
			     uint16_t *threads, 
			     struct node_cr_record *this_cr_node,
			     uint16_t *alloc_sockets, 
			     uint16_t *alloc_lps,
			     uint32_t *jobid)
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

	debug3("cons_res %u _get_resources host %s HW_ "
	       "cpus %u sockets %u cores %u threads %u ", 
	       *jobid, this_cr_node->node_ptr->name,
	       *cpus, *sockets, *cores, *threads);
	debug3("cons_res %u _get_resources host %s Alloc_ sockets %u lps %u", 
	       *jobid, this_cr_node->node_ptr->name, 
	       *alloc_sockets, *alloc_lps);
}

/*
 * _get_avail_memory returns the amount of available real memory in MB
 * for this node.
 */
static uint32_t _get_avail_memory(int index, int all_available) 
{
	uint32_t avail_memory = 0;
	struct node_cr_record *this_cr_node;
	
	if (select_fast_schedule) {
		avail_memory = select_node_ptr[index].node_ptr->config_ptr->real_memory;
	} else {
		avail_memory = select_node_ptr[index].node_ptr->real_memory;
	}
	
	if (all_available) 
		return avail_memory;
	
	this_cr_node = find_cr_node_record (select_node_ptr[index].node_ptr->name);
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
static uint16_t _get_avail_lps(struct job_record *job_ptr, 
			  const int index, 
			  const bool all_available)
{
	uint16_t avail_cpus, cpus_per_task = 0;
	uint16_t max_sockets = 0, max_cores = 0, max_threads = 0;
	uint16_t min_sockets = 0, min_cores = 0, min_threads = 0;
	uint16_t ntasks_per_node = 0, ntasks_per_socket = 0, ntasks_per_core = 0;
	uint16_t cpus, sockets, cores, threads;
	uint16_t alloc_sockets = 0, alloc_lps = 0;
	struct node_cr_record *this_cr_node;
	struct multi_core_data *mc_ptr = NULL;

	if (job_ptr->details) {
		cpus_per_task   = job_ptr->details->cpus_per_task;
		ntasks_per_node = job_ptr->details->ntasks_per_node;
		mc_ptr = job_ptr->details->mc_ptr;
	}
	if (mc_ptr) {
		min_sockets = mc_ptr->min_sockets;
		max_sockets = mc_ptr->max_sockets;
		min_cores   = mc_ptr->min_cores;
		max_cores   = mc_ptr->max_cores;
		min_threads = mc_ptr->min_threads;
		max_threads = mc_ptr->max_threads;
		ntasks_per_socket = mc_ptr->ntasks_per_socket;
		ntasks_per_core   = mc_ptr->ntasks_per_core;
	}

	this_cr_node = find_cr_node_record (select_node_ptr[index].node_ptr->name);
	if (this_cr_node == NULL) {
		error(" cons_res: could not find node %s", 
		      select_node_ptr[index].node_ptr->name);
		avail_cpus = 0;
		return avail_cpus;
	}
	get_resources_this_node(&cpus, &sockets, &cores, &threads, 
				this_cr_node, &alloc_sockets,
				&alloc_lps, &job_ptr->job_id);
	if (all_available) {
		alloc_sockets = 0;
		alloc_lps     = 0;
	}

	chk_resize_node(this_cr_node, sockets);
	avail_cpus = slurm_get_avail_procs(max_sockets,
					   max_cores,
					   max_threads,
					   min_sockets,
					   min_cores,
					   cpus_per_task,
					   ntasks_per_node,
					   ntasks_per_socket,
					   ntasks_per_core,
					   &cpus, &sockets, &cores,
					   &threads, alloc_sockets,
					   this_cr_node->alloc_cores, 
					   alloc_lps, cr_type, 
					   job_ptr->job_id,
					   this_cr_node->node_ptr->name);
	return(avail_cpus);
}		

/* xfree an array of node_cr_record */
static void _xfree_select_nodes(struct node_cr_record *ptr, int select_node_cnt)
{
	int i;
	
	if (ptr == NULL)
		return;

	for (i = 0; i < select_node_cnt; i++) {
		xfree(ptr[i].alloc_cores);
		xfree(ptr[i].name);
		ptr[i].num_sockets = 0;
	}
	xfree(ptr);
}

/* xfree a select_cr_job job */
static void _xfree_select_cr_job(struct select_cr_job *job)
{
	int i;
	
	if (job == NULL)
		return;

	if (job->host) {
		for (i=0; i<job->nhosts; i++)
			xfree(job->host[i]);
		xfree(job->host);
	}
	xfree(job->cpus);
	xfree(job->alloc_lps);	
	xfree(job->alloc_sockets);
	xfree(job->alloc_memory);
	if ((cr_type == CR_CORE) || (cr_type == CR_CORE_MEMORY)) {
		for (i = 0; i < job->nhosts; i++)
			xfree(job->alloc_cores[i]);
		xfree(job->alloc_cores);
		xfree(job->num_sockets);
	}
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

	if (select_cr_job_list == NULL) {
	    	return;
	}

	slurm_mutex_lock(&cr_mutex);
	job_iterator = list_iterator_create(select_cr_job_list);
	while ((job = (struct select_cr_job *) list_next(job_iterator))) {
		list_remove(job_iterator);
		_xfree_select_cr_job(job);
	}
	list_iterator_destroy(job_iterator);
	slurm_mutex_unlock(&cr_mutex);
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
	slurm_mutex_lock(&cr_mutex);
	while ((old_job = (struct select_cr_job *) list_next(iterator))) {
		if (old_job->job_id != job_id)
			continue;
		list_remove(iterator);	/* Delete record for JobId job_id */
		_xfree_select_cr_job(old_job);	/* xfree job structure */
		break;
	}

	list_iterator_destroy(iterator);
	list_append(select_cr_job_list, new_job);
	slurm_mutex_unlock(&cr_mutex);
	debug3 (" cons_res: _append_to_job_list job_id %u to list. "
		"list_count %d ", job_id, list_count(select_cr_job_list));
}

/*
 * _count_cpus - report how many cpus are available with the identified nodes 
 */
static void _count_cpus(unsigned *bitmap, uint16_t sum)
{
	int i, allocated_lps;
	sum = 0;

	for (i = 0; i < node_record_count; i++) {
		struct node_cr_record *this_node;
		allocated_lps = 0;
		if (bit_test(bitmap, i) != 1)
			continue;

		this_node = find_cr_node_record(node_record_table_ptr[i].name);
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
			int core_cnt = 0;
			chk_resize_node(this_node, this_node->node_ptr->sockets);
			for (i = 0; i < this_node->node_ptr->sockets; i++)
				core_cnt += this_node->alloc_cores[i];
			if (slurmctld_conf.fast_schedule) {
				sum += ((node_record_table_ptr[i].config_ptr->sockets
					 * node_record_table_ptr[i].config_ptr->cores)
					- core_cnt)
					* node_record_table_ptr[i].config_ptr->threads;
			} else {
				sum += ((node_record_table_ptr[i].sockets 
					 * node_record_table_ptr[i].cores) 
					- core_cnt)
					* node_record_table_ptr[i].threads;
			}
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
		uint16_t allocated_cpus;
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
	int rc = SLURM_SUCCESS, i, j, nodes, job_id;
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
			this_node = find_cr_node_record(job->host[i]);
			if (this_node == NULL) {
				error(" cons_res: could not find node %s",
				      job->host[i]);
				rc = SLURM_ERROR; 
				goto out;
			}
			
			/* Updating this node allocated resources */
			switch(cr_type) {
			case CR_SOCKET:
			case CR_SOCKET_MEMORY:
				if (this_node->alloc_lps >= job->alloc_lps[i])
					this_node->alloc_lps -= job->alloc_lps[i];
				else {
					error("alloc_lps underflow on %s",
					      this_node->node_ptr->name);
					rc = SLURM_ERROR;
				}
				if (this_node->alloc_sockets >= job->alloc_sockets[i])
					this_node->alloc_sockets -= job->alloc_sockets[i];
				else {
					error("alloc_sockets underflow on %s",
					      this_node->node_ptr->name);
					rc = SLURM_ERROR;
				}
				if (this_node->alloc_memory >= job->alloc_memory[i])
					this_node->alloc_memory -= job->alloc_memory[i];
				else {
					error("alloc_memory underflow on %s",
					      this_node->node_ptr->name);
					rc = SLURM_ERROR;  
				}
				if (rc == SLURM_ERROR) {
					this_node->alloc_lps = 0;
					this_node->alloc_sockets = 0;
					this_node->alloc_memory = 0;
					goto out;
				}
				break;
			case CR_CORE:
			case CR_CORE_MEMORY:
				if (this_node->alloc_lps >= job->alloc_lps[i])
					this_node->alloc_lps -= job->alloc_lps[i];
				else {
					error("alloc_lps underflow on %s",
					      this_node->node_ptr->name);
					rc = SLURM_ERROR;
				}
				chk_resize_node(this_node, this_node->node_ptr->sockets);
				chk_resize_job(job, i, this_node->num_sockets);
				for (j =0; j < this_node->num_sockets; j++) {
					if (this_node->alloc_cores[j] >= job->alloc_cores[i][j])
						this_node->alloc_cores[j] -= job->alloc_cores[i][j];
					else {
						error("alloc_cores underflow on %s",
						      this_node->node_ptr->name);
						rc = SLURM_ERROR;
					}
				}
				if (this_node->alloc_memory >= job->alloc_memory[i])
					this_node->alloc_memory -= job->alloc_memory[i];
				else {
					error("alloc_memory underflow on %s",
					      this_node->node_ptr->name);
					rc = SLURM_ERROR;  
				}
				if (rc == SLURM_ERROR) {
					this_node->alloc_lps = 0;
					for (j =0; j < this_node->num_sockets; j++) {
						this_node->alloc_cores[j] = 0;
					}
					this_node->alloc_memory = 0;
					goto out;
				}
				break;
			case CR_MEMORY:
				if (this_node->alloc_memory >= job->alloc_memory[i])
					this_node->alloc_memory -= job->alloc_memory[i];
				else {
					error("alloc_memory underflow on %s",
					      this_node->node_ptr->name);
					this_node->alloc_memory = 0;
					rc = SLURM_ERROR;  
					goto out;
				}
				break;
			case CR_CPU:
			case CR_CPU_MEMORY:
				if (this_node->alloc_lps >= job->alloc_lps[i])
					this_node->alloc_lps -= job->alloc_lps[i];
				else {
					error("alloc_lps underflow on %s",
					      this_node->node_ptr->name);
					this_node->alloc_lps = 0;
					rc = SLURM_ERROR;  
					goto out;
				}
				if (cr_type == CR_CPU)
					break;

				if (this_node->alloc_memory >= job->alloc_memory[i])
					this_node->alloc_memory -= job->alloc_memory[i];
				else {
					error("alloc_memory underflow on %s",
					      this_node->node_ptr->name);
					this_node->alloc_memory = 0;
					rc = SLURM_ERROR;  
					goto out;
				}
				break;
			default:
				break;
			}
#if(CR_DEBUG)
			info("cons_res %u _clear_select_jobinfo (-) node %s "
			     "alloc_ s %u lps %u",
			     job->job_id, this_node->node_ptr->name, 
			     this_node->alloc_sockets, 
			     this_node->alloc_lps);
			if ((cr_type == CR_CORE) || (cr_type == CR_CORE_MEMORY))
				for (j =0; j < this_node->num_sockets; j++)
					info("cons_res %u _clear_select_jobinfo (-) "
					     " node %s alloc_  c %u",
					     job->job_id, this_node->node_ptr->name, 
					     this_node->alloc_cores[j]);
#endif
		}
	out:
		slurm_mutex_lock(&cr_mutex);
		list_remove(iterator);
		slurm_mutex_unlock(&cr_mutex);
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
	if (select_cr_job_list) {
		list_destroy(select_cr_job_list);
		select_cr_job_list = NULL;
	}

	_xfree_select_nodes(select_node_ptr, select_node_cnt);
	select_node_ptr = NULL;
	select_node_cnt = 0;
	xfree(cr_node_hash_table);

	_xfree_select_nodes(prev_select_node_ptr, prev_select_node_cnt);
	prev_select_node_ptr = NULL;
	prev_select_node_cnt = 0;

	verbose("%s shutting down ...", plugin_name);
	return SLURM_SUCCESS;
}

/*
 * The remainder of this file implements the standard SLURM 
 * node selection API.
 */

static int _cr_write_state_buffer(int fd, Buf buffer)
{
	int error_code = SLURM_SUCCESS;
	char *buf  = get_buf_data(buffer);
	size_t len = get_buf_offset(buffer);
	while(1) {
		int wrote = write (fd, buf, len);
		if ((wrote < 0) && (errno == EINTR))
			continue;
		if (wrote == 0)
			break;
		if (wrote < 0) {
			error ("Can't save select/cons_res state: %m");
			error_code = SLURM_ERROR;
			break;   
		}
		buf += wrote;
		len -= wrote;
		if (len == 0) {
			break;
		}
		if (len <= 0) {
			error ("Can't save select/cons_res state: %m");
			error_code = SLURM_ERROR;
			break;   
		}
	}
	return error_code;
}

static int _cr_read_state_buffer(int fd, char **data_p, int *data_size_p)
{
	int error_code = SLURM_SUCCESS;
        int data_allocated = 0, data_read = 0, data_size = 0;
	char *data = NULL;
	int buffer_size = 1024;

        if (fd < 0) {
	    	error_code = SLURM_ERROR; 
                error("No fd for select/cons_res state recovery");
	}

	data_allocated = buffer_size;
	data = xmalloc(data_allocated);
	*data_p      = data;
	*data_size_p = data_size;
	while (1) {
		data_read = read (fd, &data[data_size],
				  buffer_size);
		if ((data_read < 0) && (errno == EINTR)) {
			continue;
		}
		if (data_read < 0) {
			error ("Read error recovering select/cons_res state");
			error_code = SLURM_ERROR;
			break;
		} else if (data_read == 0) {
			break;
		}
		data_size      += data_read;
		data_allocated += data_read;
		xrealloc(data, data_allocated);
		*data_p      = data;
		*data_size_p = data_size;
	}

	return error_code;
}

static int _cr_pack_job(struct select_cr_job *job, Buf buffer)
{
    	int i;
	uint16_t nhosts = job->nhosts;

	pack32(job->job_id, buffer);
	pack16(job->state, buffer);
	pack32(job->nprocs, buffer);
	pack16(job->nhosts, buffer);

	packstr_array(job->host, nhosts, buffer);
	pack16_array(job->cpus, nhosts, buffer);
	pack16_array(job->alloc_lps, nhosts, buffer);
	pack16_array(job->alloc_sockets, nhosts, buffer);

	if (job->alloc_cores) {
		pack16((uint16_t) 1, buffer);
		for (i = 0; i < nhosts; i++) {
			uint16_t nsockets = job->num_sockets[i];
			pack16(nsockets, buffer);
			pack16_array(job->alloc_cores[i], nsockets, buffer);
		}
	} else {
		pack16((uint16_t) 0, buffer);
	}
	pack32_array(job->alloc_memory, nhosts, buffer);

	pack16(job->max_sockets, buffer);
	pack16(job->max_cores, buffer);
	pack16(job->max_threads, buffer);
	pack16(job->min_sockets, buffer);
	pack16(job->min_cores, buffer);
	pack16(job->min_threads, buffer);
	pack16(job->ntasks_per_node, buffer);
	pack16(job->ntasks_per_socket, buffer);
	pack16(job->ntasks_per_core, buffer);
	pack16(job->cpus_per_task, buffer);

	pack_bit_fmt(job->node_bitmap, buffer);
	pack16(_bitstr_bits(job->node_bitmap), buffer);

	return 0;
}

static int _cr_unpack_job(struct select_cr_job *job, Buf buffer)
{
    	int i;
    	uint16_t len16, have_alloc_cores;
    	uint32_t len32;
	int32_t nhosts = 0;
	char *bit_fmt = NULL;
	uint16_t bit_cnt; 

	safe_unpack32(&job->job_id, buffer);
	safe_unpack16(&job->state, buffer);
	safe_unpack32(&job->nprocs, buffer);
	safe_unpack16(&job->nhosts, buffer);
	nhosts = job->nhosts;

	safe_unpackstr_array(&job->host, &len16, buffer);
	if (len16 != nhosts) {
		error("cons_res unpack_job: expected %u hosts, saw %u",
				nhosts, len16);
		goto unpack_error;
	}

	safe_unpack16_array(&job->cpus, &len32, buffer);
	safe_unpack16_array(&job->alloc_lps, &len32, buffer);
	safe_unpack16_array(&job->alloc_sockets, &len32, buffer);

	safe_unpack16(&have_alloc_cores, buffer);
	if (have_alloc_cores) {
		job->num_sockets = (uint16_t *) xmalloc(job->nhosts * 
				sizeof(uint16_t));
		job->alloc_cores = (uint16_t **) xmalloc(job->nhosts * 
				sizeof(uint16_t *));
		for (i = 0; i < nhosts; i++) {
			safe_unpack16(&job->num_sockets[i], buffer);
			safe_unpack16_array(&job->alloc_cores[i], &len32, buffer);
			if (len32 != job->num_sockets[i])
				goto unpack_error;
		}
	}
	safe_unpack32_array((uint32_t**)&job->alloc_memory, &len32, buffer);
	if (len32 != nhosts)
		 goto unpack_error;

	safe_unpack16(&job->max_sockets, buffer);
	safe_unpack16(&job->max_cores, buffer);
	safe_unpack16(&job->max_threads, buffer);
	safe_unpack16(&job->min_sockets, buffer);
	safe_unpack16(&job->min_cores, buffer);
	safe_unpack16(&job->min_threads, buffer);
	safe_unpack16(&job->ntasks_per_node, buffer);
	safe_unpack16(&job->ntasks_per_socket, buffer);
	safe_unpack16(&job->ntasks_per_core, buffer);
	safe_unpack16(&job->cpus_per_task, buffer);

	safe_unpackstr_xmalloc(&bit_fmt, &len16, buffer);
	safe_unpack16(&bit_cnt, buffer);
	if (bit_fmt) {
                job->node_bitmap = bit_alloc(bit_cnt);
                if (job->node_bitmap == NULL)
                        fatal("bit_alloc: %m");
                if (bit_unfmt(job->node_bitmap, bit_fmt)) {
                        error("error recovering exit_node_bitmap from %s",
                                bit_fmt);
                }
                xfree(bit_fmt);
	}
	return 0;

unpack_error:
	_xfree_select_cr_job(job);
	xfree(bit_fmt);
	return -1;
}

extern int select_p_state_save(char *dir_name)
{
	int error_code = SLURM_SUCCESS;
	ListIterator job_iterator;
	struct select_cr_job *job;
	Buf buffer = NULL;
	int state_fd, i;
	uint16_t job_cnt;
	char *file_name;
	static time_t last_save_time;

	if (last_save_time > last_cr_update_time)
		return SLURM_SUCCESS;

	debug3("cons_res: select_p_state_save");

	/*** create the state file ***/
        file_name = xstrdup(dir_name);
        xstrcat(file_name, "/cons_res_state");
        (void) unlink(file_name);
        state_fd = creat (file_name, 0600);
        if (state_fd < 0) {
                error ("Can't save state, error creating file %s",
                        file_name);
                return SLURM_ERROR;
	}

	buffer = init_buf(1024);

	/*** record the plugin type ***/
	packstr((char*)plugin_type, buffer);
	pack32(plugin_version, buffer);
	pack16(cr_type,        buffer);
	pack32(pstate_version, buffer);

	slurm_mutex_lock(&cr_mutex);
	/*** pack the select_cr_job array ***/
	if (select_cr_job_list) {
		job_cnt = list_count(select_cr_job_list);
		pack16(job_cnt, buffer);
		job_iterator = list_iterator_create(select_cr_job_list);
		while ((job = (struct select_cr_job *) list_next(job_iterator))) {
			_cr_pack_job(job, buffer);
		}
		list_iterator_destroy(job_iterator);
	} else
		pack16((uint16_t) 0, buffer);	/* job count */

	/*** pack the node_cr_record array ***/
	pack32((uint32_t)select_node_cnt, buffer);
	for (i = 0; i < select_node_cnt; i++) {
		/*** don't save select_node_ptr[i].node_ptr ***/
		packstr((char*)select_node_ptr[i].node_ptr->name, buffer);
		pack16(select_node_ptr[i].alloc_lps, buffer);
		pack16(select_node_ptr[i].alloc_sockets, buffer);
		pack32(select_node_ptr[i].alloc_memory, buffer);
		pack16(select_node_ptr[i].num_sockets, buffer);
		if (select_node_ptr[i].alloc_cores) {
			uint16_t nsockets = select_node_ptr[i].num_sockets;
			pack16((uint16_t) 1, buffer);
			pack16_array(select_node_ptr[i].alloc_cores,
							nsockets, buffer);
		} else {
			pack16((uint16_t) 0, buffer);
		}
	}
	slurm_mutex_unlock(&cr_mutex);

	/*** close the state file ***/
	error_code = _cr_write_state_buffer(state_fd, buffer);
	if (error_code == SLURM_SUCCESS)
		last_save_time = time(NULL);
	close (state_fd);
	xfree(file_name);
	if (buffer)
		free_buf(buffer);

	return error_code;
}


/* _cr_find_prev_node
 *	Return the index in the previous node list for the host
 *	with the given name.  The previous index matched is used
 *	as a starting point in to achieve O(1) performance when
 *	matching node data in sequence between two identical lists
 *	of hosts
 */
static int _cr_find_prev_node(char *name, int prev_i)
{
    	int i, cnt = 0;
    	if (prev_i < 0) {
	    	prev_i = -1;
	}

	/* scan forward from previous index for a match */
	for (i = prev_i + 1; i < prev_select_node_cnt; i++) {
		cnt++;
		if (strcmp(name, prev_select_node_ptr[i].name) == 0) {
			debug3("_cr_find_prev_node fwd: %d %d cmp", i, cnt);
		    	return i;
		}
	}

	/* if not found, scan from beginning to previous index for a match */
	for (i = 0; i < MIN(prev_i + 1, prev_select_node_cnt); i++) {
		cnt++;
		if (strcmp(name, prev_select_node_ptr[i].name) == 0) {
			debug3("_cr_find_prev_node beg: %d %d cmp", i, cnt);
		    	return i;
		}
	}

	debug3("_cr_find_prev_node none: %d %d cmp", -1, cnt);
	return -1;	/* no match found */
}

static void _cr_restore_node_data(void)
{
	int i, j, tmp, prev_i;

    	if ((select_node_ptr == NULL) || (select_node_cnt <= 0)) {
	    	/* can't restore, nodes not yet initialized */
		/* will attempt restore later in select_p_node_init */
	    	return;
	}

    	if ((prev_select_node_ptr == NULL) || (prev_select_node_cnt <= 0)) {
	    	/* can't restore, node restore data not present */
		/* will attempt restore later in select_p_state_restore */
	    	return;
	}

	prev_i = -1;		/* index of previous matched node */
	for (i = 0; i < select_node_cnt; i++) {
		tmp = _cr_find_prev_node(select_node_ptr[i].name, prev_i);
		if (tmp < 0) {		/* not found in prev node list */
		    	continue;	/* skip update for this node */
		}
		prev_i = tmp;	/* found a match */

		debug2("recovered cons_res node data for %s",
				    select_node_ptr[i].name);
		
		select_node_ptr[i].alloc_lps
			= prev_select_node_ptr[prev_i].alloc_lps;
		select_node_ptr[i].alloc_sockets
			= prev_select_node_ptr[prev_i].alloc_sockets;
		select_node_ptr[i].alloc_memory
			= prev_select_node_ptr[prev_i].alloc_memory;
		if (select_node_ptr[i].alloc_cores &&
			prev_select_node_ptr[prev_i].alloc_cores) {
			chk_resize_node(&(select_node_ptr[i]),
			    prev_select_node_ptr[prev_i].num_sockets);
			select_node_ptr[i].num_sockets = 
			    prev_select_node_ptr[prev_i].num_sockets;
			for (j = 0; j < select_node_ptr[i].num_sockets; j++) {
				select_node_ptr[i].alloc_cores[j]
					 = prev_select_node_ptr[prev_i].alloc_cores[j];
			}
		}
	}

	/* Release any previous node data */
	_xfree_select_nodes(prev_select_node_ptr, prev_select_node_cnt);
	prev_select_node_ptr = NULL;
	prev_select_node_cnt = 0;
}

extern int select_p_state_restore(char *dir_name)
{
	int error_code = SLURM_SUCCESS;
	int state_fd, i;
	char *file_name = NULL;
	struct select_cr_job *job;
	Buf buffer = NULL;
	uint16_t len16;
	uint32_t len32;
	char *data = NULL;
	int data_size = 0;
	char *restore_plugin_type = NULL;
	uint32_t restore_plugin_version = 0;
	uint16_t restore_plugin_crtype  = 0;
	uint32_t restore_pstate_version = 0;
	uint16_t job_cnt;

	info("cons_res: select_p_state_restore");

	if (!dir_name) {
		info("Starting cons_res with clean slate");
		return SLURM_SUCCESS;
	}
	file_name = xstrdup(dir_name);
	xstrcat(file_name, "/cons_res_state");
	state_fd = open (file_name, O_RDONLY);
	if (state_fd < 0) {
		error ("Can't restore state, error opening file %s",
			file_name);
		error ("Starting cons_res with clean slate");
		xfree(file_name);
		return SLURM_SUCCESS;
	}

	error_code = _cr_read_state_buffer(state_fd, &data, &data_size);

	if (error_code != SLURM_SUCCESS) {
		error ("Can't restore state, error reading file %s",
			file_name);
		error ("Starting cons_res with clean slate");
		xfree(data);
		xfree(file_name);
		return SLURM_SUCCESS;
	}

	buffer = create_buf (data, data_size);
	data = NULL;    /* now in buffer, don't xfree() */

	/*** retrieve the plugin type ***/
	safe_unpackstr_xmalloc(&restore_plugin_type, &len16, buffer);
	safe_unpack32(&restore_plugin_version, buffer);
	safe_unpack16(&restore_plugin_crtype,  buffer);
	safe_unpack32(&restore_pstate_version, buffer);

	if ((strcmp(restore_plugin_type, plugin_type) != 0) ||
	    (restore_plugin_version != plugin_version) ||
	    (restore_plugin_crtype  != cr_type) ||
	    (restore_pstate_version != pstate_version)) { 
		error ("Can't restore state, state version mismtach: "
			"saw %s/%u/%u/%u, expected %s/%u/%u/%u",
			restore_plugin_type,
			restore_plugin_version,
			restore_plugin_crtype,
			restore_pstate_version,
			plugin_type,
			plugin_version,
			cr_type,
			pstate_version);
		error ("Starting cons_res with clean slate");
		xfree(restore_plugin_type);
		if (buffer)
			free_buf(buffer);
		xfree(file_name);
		return SLURM_SUCCESS;
	}

	/*** unpack the select_cr_job array ***/
	_clear_job_list();
	if (select_cr_job_list) {
		list_destroy(select_cr_job_list);
		select_cr_job_list = NULL;
	}
	select_cr_job_list = list_create(NULL);

	safe_unpack16(&job_cnt, buffer);
	for (i=0; i<job_cnt; i++) {
		job = xmalloc(sizeof(struct select_cr_job));
		if (_cr_unpack_job(job, buffer) != 0)
			goto unpack_error;
		list_append(select_cr_job_list, job);
		debug2("recovered cons_res job data for job %u", job->job_id);
	}

	/*** unpack the node_cr_record array ***/
	if (prev_select_node_ptr) {	/* clear any existing data */
		_xfree_select_nodes(prev_select_node_ptr, prev_select_node_cnt);
		prev_select_node_ptr = NULL;
		prev_select_node_cnt = 0;
	}
	safe_unpack32((uint32_t*)&prev_select_node_cnt, buffer);
	prev_select_node_ptr = xmalloc(sizeof(struct node_cr_record) *
							(prev_select_node_cnt));
	for (i = 0; i < prev_select_node_cnt; i++) {
		uint16_t have_alloc_cores = 0;
		/*** don't restore prev_select_node_ptr[i].node_ptr ***/
		safe_unpackstr_xmalloc(&(prev_select_node_ptr[i].name), 
				       &len16, buffer);
		safe_unpack16(&prev_select_node_ptr[i].alloc_lps, buffer);
		safe_unpack16(&prev_select_node_ptr[i].alloc_sockets, buffer);
		safe_unpack32(&prev_select_node_ptr[i].alloc_memory, buffer);
		safe_unpack16(&prev_select_node_ptr[i].num_sockets, buffer);
		safe_unpack16(&have_alloc_cores, buffer);
		if (have_alloc_cores) {
			safe_unpack16_array(
			    	&prev_select_node_ptr[i].alloc_cores,
				&len32, buffer);
		}
	}

	/*** cleanup after restore ***/
        if (buffer)
                free_buf(buffer);
        xfree(restore_plugin_type);
	xfree(file_name);

	_cr_restore_node_data();	/* if nodes already initialized */

	return SLURM_SUCCESS;

unpack_error:
        if (buffer)
                free_buf(buffer);
        xfree(restore_plugin_type);

	/* don't keep possibly invalid prev_select_node_ptr */
	_xfree_select_nodes(prev_select_node_ptr, prev_select_node_cnt);
	prev_select_node_ptr = NULL;
	prev_select_node_cnt = 0;

	error ("Can't restore state, error unpacking file %s", file_name);
	error ("Starting cons_res with clean slate");
	return SLURM_SUCCESS;
}

extern int select_p_job_init(List job_list)
{
	info("cons_res: select_p_job_init");

    	if (!select_cr_job_list) {
		select_cr_job_list = list_create(NULL);
	}

	/* Note: select_cr_job_list restored in select_p_state_restore */

	return SLURM_SUCCESS;
}

extern int select_p_node_init(struct node_record *node_ptr, int node_cnt)
{
	int i;

	info("cons_res: select_p_node_init");

	if (node_ptr == NULL) {
		error("select_g_node_init: node_ptr == NULL");
		return SLURM_ERROR;
	}

	if (node_cnt < 0) {
		error("select_g_node_init: node_cnt < 0");
		return SLURM_ERROR;
	}

	/* completely rebuild node data */
	_xfree_select_nodes(select_node_ptr, select_node_cnt);
	select_node_cnt = node_cnt;
	select_node_ptr = xmalloc(sizeof(struct node_cr_record) *
							select_node_cnt);

	for (i = 0; i < select_node_cnt; i++) {
		select_node_ptr[i].node_ptr = &node_ptr[i];
		select_node_ptr[i].name     = xstrdup(node_ptr[i].name);
		select_node_ptr[i].alloc_lps      = 0;
		select_node_ptr[i].alloc_sockets  = 0;
		select_node_ptr[i].alloc_memory   = 0;
		if ((cr_type == CR_CORE) || (cr_type == CR_CORE_MEMORY)) {
			info("select_g_node_init node:%s sockets:%u",
			     select_node_ptr[i].name, 
			     select_node_ptr[i].node_ptr->sockets);
			select_node_ptr[i].num_sockets =
			     select_node_ptr[i].node_ptr->sockets;
			select_node_ptr[i].alloc_cores    = 
				xmalloc(sizeof(int) * 
					select_node_ptr[i].num_sockets);
		}
	}

	_cr_restore_node_data();	/* if restore data present */

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
	uint16_t plane_size = 0;
	//int asockets, acores, athreads, acpus;
	bool all_avail = false;
	struct multi_core_data *mc_ptr = NULL;

	xassert(bitmap);

	consec_index = 0;
	consec_size = 50;	/* start allocation for 50 sets of 
				 * consecutive nodes */
	consec_cpus  = xmalloc(sizeof(int) * consec_size);
	consec_nodes = xmalloc(sizeof(int) * consec_size);
	consec_start = xmalloc(sizeof(int) * consec_size);
	consec_end   = xmalloc(sizeof(int) * consec_size);
	consec_req   = xmalloc(sizeof(int) * consec_size);

	/* Build table with information about sets of consecutive nodes */
	consec_cpus[consec_index] = consec_nodes[consec_index] = 0;
	consec_req[consec_index] = -1;	/* no required nodes here by default */

	if (job_ptr->details)
		mc_ptr = job_ptr->details->mc_ptr;

	/* This is the case if -O/--overcommit  is true */ 
	debug3("job_ptr->num_procs %u", job_ptr->num_procs);
	if (mc_ptr && (job_ptr->num_procs == job_ptr->details->min_nodes)) {
		job_ptr->num_procs *= MAX(1,mc_ptr->min_threads);
		job_ptr->num_procs *= MAX(1,mc_ptr->min_cores);
		job_ptr->num_procs *= MAX(1,mc_ptr->min_sockets);
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
		int jobid, job_nodecnt, j, k;
		bitoff_t size;
		static struct select_cr_job *job;
		job = xmalloc(sizeof(struct select_cr_job));
		jobid = job_ptr->job_id;
		job->job_id = jobid;
		job_nodecnt = bit_set_count(bitmap);
		job->nhosts = job_nodecnt;
		job->nprocs = MAX(job_ptr->num_procs, job_nodecnt);
		job->cpus_per_task = job_ptr->details->cpus_per_task;
		job->ntasks_per_node = job_ptr->details->ntasks_per_node;
		if (mc_ptr) {
			plane_size       = mc_ptr->plane_size;
			job->max_sockets = mc_ptr->max_sockets;
			job->max_cores   = mc_ptr->max_cores;
			job->max_threads = mc_ptr->max_threads;
			job->min_sockets = mc_ptr->min_sockets;
			job->min_cores   = mc_ptr->min_cores;
			job->min_threads = mc_ptr->min_threads;
			job->ntasks_per_socket = mc_ptr->ntasks_per_socket;
			job->ntasks_per_core   = mc_ptr->ntasks_per_core;
		} else {
			job->max_sockets = 0xffff;
			job->max_cores   = 0xffff;
			job->max_threads = 0xffff;
			job->min_sockets = 1;
			job->min_cores   = 1;
			job->min_threads = 1;
			job->ntasks_per_socket = 0;
			job->ntasks_per_core   = 0;
		}

		size = bit_size(bitmap);
		job->node_bitmap = (bitstr_t *) bit_alloc(size);
		if (job->node_bitmap == NULL)
			fatal("bit_alloc malloc failure");
		for (i = 0; i < size; i++) {
			if (!bit_test(bitmap, i))
				continue;
			bit_set(job->node_bitmap, i);
		}
		
		job->host          = (char **)    xmalloc(job->nhosts * sizeof(char *));
		job->cpus          = (uint16_t *) xmalloc(job->nhosts * sizeof(uint16_t));
		job->alloc_lps     = (uint16_t *) xmalloc(job->nhosts * sizeof(uint16_t));
		job->alloc_sockets = (uint16_t *) xmalloc(job->nhosts * sizeof(uint16_t));
		job->alloc_memory  = (uint32_t *) xmalloc(job->nhosts * sizeof(uint32_t));
		if ((cr_type == CR_CORE) || (cr_type == CR_CORE_MEMORY)) {
			job->num_sockets = (uint16_t *)  xmalloc(job->nhosts * sizeof(uint16_t));
			job->alloc_cores = (uint16_t **) xmalloc(job->nhosts * sizeof(uint16_t *));
			for (i = 0; i < job->nhosts; i++) {
				job->num_sockets[i] = 
					node_record_table_ptr[i].sockets;
				job->alloc_cores[i] = (uint16_t *) xmalloc(
					job->num_sockets[i] * sizeof(uint16_t));
			}
		}

		j = 0;
		for (i = 0; i < node_record_count; i++) {
			if (bit_test(bitmap, i) == 0)
				continue;
			if (j >= job->nhosts) {
				error("select_cons_res: job nhosts too small\n");
				break;
			}
			job->host[j] = xstrdup(node_record_table_ptr[i].name);
			job->cpus[j] = node_record_table_ptr[i].cpus;
			job->alloc_lps[j] = 0;
			job->alloc_sockets[j] = 0;
			job->alloc_memory[j] = job_ptr->details->job_max_memory; 
			if ((cr_type == CR_CORE) || (cr_type == CR_CORE_MEMORY)) {
				chk_resize_job(job, j, node_record_table_ptr[i].sockets);
				job->num_sockets[j] = node_record_table_ptr[i].sockets;
				for (k = 0; k < job->num_sockets[j]; k++)
					job->alloc_cores[j][k]   = 0;
			}
			j++;
		}

		if (job_ptr->details->shared == 0) {
			/* Nodes need to be allocated in dedicated
			   mode. User has specified the --exclusive
			   switch */
			error_code = cr_exclusive_dist(job, cr_type);
		} else {
			/* Determine the number of logical processors
			   per node needed for this job */
			/* Make sure below matches the layouts in
			 * lllp_distribution in
			 * plugins/task/affinity/dist_task.c */
			switch(job_ptr->details->task_dist) {
			case SLURM_DIST_BLOCK_BLOCK:
			case SLURM_DIST_CYCLIC_BLOCK:
				error_code = cr_dist(job, 0, 
						     cr_type,
						     select_fast_schedule); 
				break;
			case SLURM_DIST_BLOCK:
			case SLURM_DIST_CYCLIC:				
			case SLURM_DIST_BLOCK_CYCLIC:
			case SLURM_DIST_CYCLIC_CYCLIC:
			case SLURM_DIST_UNKNOWN:
				error_code = cr_dist(job, 1, 
						     cr_type,
						     select_fast_schedule); 
				break;
			case SLURM_DIST_PLANE:
				error_code = cr_plane_dist(job, 
							   plane_size,
							   cr_type); 
				break;
			case SLURM_DIST_ARBITRARY:
			default:
				error_code = compute_c_b_task_dist(job, 
								   cr_type,
								   select_fast_schedule);
				if (error_code != SLURM_SUCCESS) {
					error(" Error in compute_c_b_task_dist");
					return error_code;
				}
				break;
			}
		}
		if (error_code != SLURM_SUCCESS) {
			_xfree_select_cr_job(job);
			goto cleanup;
		}

		_append_to_job_list(job);
		last_cr_update_time = time(NULL);
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
	last_cr_update_time = time(NULL);
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
   		        this_node_ptr = find_cr_node_record (job->host[i]);
			if (this_node_ptr == NULL) {
				error(" cons_res: could not find node %s",
					job->host[i]);
				rc = SLURM_ERROR; 
				goto cleanup;
			}
			if (this_node_ptr->alloc_lps >= job->alloc_lps[i])
				this_node_ptr->alloc_lps -= job->alloc_lps[i];
			else {
			        error("cons_res: alloc_lps underflow on %s",
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
		        this_node = find_cr_node_record (job->host[i]);
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
	uint16_t *tmp_16 = (uint16_t *) data;

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
		struct multi_core_data *mc_ptr = job_ptr->details->mc_ptr;

		if (mc_ptr &&
		    ((job_ptr->details->cpus_per_task > 1) || 
		     (mc_ptr->max_sockets > 1)   ||
		     (mc_ptr->max_cores > 1)     ||
		     (mc_ptr->max_threads > 1))) {
			*tmp_16 = 0;
			for (i = 0; i < node_record_count; i++) {
				if (bit_test(job_ptr->details->req_node_bitmap, i) != 1)
					continue;
				*tmp_16 += _get_avail_lps(job_ptr, i, false);
			}
		} else {
			_count_cpus(job_ptr->details->
				    req_node_bitmap, *tmp_16);
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

		*tmp_16 = 0;
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
				case CR_MEMORY:
					*tmp_16 = node_ptr->cpus;
					break;
				case CR_SOCKET:
				case CR_SOCKET_MEMORY:
				case CR_CORE: 
				case CR_CORE_MEMORY: 
				case CR_CPU:
				case CR_CPU_MEMORY:
				default:
					*tmp_16 = job->alloc_lps[i];
					break;
				}
				goto cleanup;
			}
			error("cons_res could not find %s", node_ptr->name); 
			rc = SLURM_ERROR;
		}
		if (!job) {
			debug3("cons_res: job %u not active", job_ptr->job_id);
			*tmp_16 = 0;
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
	int rc = SLURM_SUCCESS, i;
	struct node_cr_record *this_cr_node;

	xassert(node_ptr);
	xassert(node_ptr->magic == NODE_MAGIC);

	switch (dinfo) {
	case SELECT_AVAIL_MEMORY:
	case SELECT_ALLOC_MEMORY: 
	{
		uint32_t *tmp_32 = (uint32_t *) data;

		*tmp_32 = 0;
		switch(cr_type) {
		case CR_MEMORY:
		case CR_SOCKET_MEMORY:
		case CR_CORE_MEMORY:
		case CR_CPU_MEMORY:
			this_cr_node = find_cr_node_record (node_ptr->name);
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
		uint16_t *tmp_16 = (uint16_t *) data;
		*tmp_16 = 0;
	        this_cr_node = find_cr_node_record (node_ptr->name);
		if (this_cr_node == NULL) {
		        error(" cons_res: could not find node %s",
			      node_ptr->name);
			rc = SLURM_ERROR;
			return rc;
		}
		switch(cr_type) {
		case CR_SOCKET:
		case CR_SOCKET_MEMORY:
			*tmp_16 = this_cr_node->alloc_sockets *
			        node_ptr->cores * node_ptr->threads;
			break;
		case CR_CORE:
		case CR_CORE_MEMORY:
			for (i = 0; i < this_cr_node->num_sockets; i++)  
				*tmp_16 += this_cr_node->alloc_cores[i] *
					node_ptr->threads;
			break;
		case CR_MEMORY:
		case CR_CPU:
		case CR_CPU_MEMORY:
		default:
			*tmp_16 = this_cr_node->alloc_lps;
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
	int rc = SLURM_SUCCESS, i, j, job_id, nodes;
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
			this_node = find_cr_node_record (job->host[i]);
			if (this_node == NULL) {
				error(" cons_res: could not find node %s",
				      job->host[i]);
				rc = SLURM_ERROR;
				goto cleanup;
			}
			/* Updating this node's allocated resources */
			switch (cr_type) {
			case CR_SOCKET_MEMORY:
				this_node->alloc_memory += job->alloc_memory[i];
			case CR_SOCKET:
				this_node->alloc_lps     += job->alloc_lps[i];
				this_node->alloc_sockets += job->alloc_sockets[i];
				if (this_node->alloc_sockets > this_node->node_ptr->sockets)
					error("Job %u Host %s too many allocated sockets %u",
					      job->job_id, this_node->node_ptr->name, 
					      this_node->alloc_sockets);
				this_node->alloc_memory += job->alloc_memory[i];
				break;
			case CR_CORE_MEMORY:
				this_node->alloc_memory += job->alloc_memory[i];
			case CR_CORE:
				this_node->alloc_lps   += job->alloc_lps[i];
				if (this_node->alloc_lps >  this_node->node_ptr->cpus)
					error("Job %u Host %s too many allocated lps %u",
					      job->job_id, this_node->node_ptr->name, 
					      this_node->alloc_lps);
				chk_resize_node(this_node, this_node->node_ptr->sockets);
				chk_resize_job(job, i, this_node->num_sockets);
				for (j = 0; j < this_node->num_sockets; j++)
					this_node->alloc_cores[j] += job->alloc_cores[i][j];
				for (j = 0; j < this_node->num_sockets; j++)
					if (this_node->alloc_cores[j] <= 
					    this_node->node_ptr->cores)
						continue;
					else
						error("Job %u Host %s too many allocated "
						      "cores %u for socket %d",
						      job->job_id, this_node->node_ptr->name, 
						      this_node->alloc_cores[j], j);
				break;
			case CR_CPU_MEMORY:
				this_node->alloc_memory += job->alloc_memory[i];
			case CR_CPU:
				this_node->alloc_lps     += job->alloc_lps[i];				
				break;
			case CR_MEMORY: 
				this_node->alloc_memory += job->alloc_memory[i];
				break;
			default:
				error("select_g_update_nodeinfo info %d invalid", cr_type);
				rc = SLURM_ERROR;
				break;
			}
#if(CR_DEBUG)
			/* Remove debug only */
			info("cons_res %u update_nodeinfo (+) node %s "
			     "alloc_ lps %u sockets %u mem %u",
			     job->job_id, this_node->node_ptr->name, this_node->alloc_lps, 
			     this_node->alloc_sockets, this_node->alloc_memory);
			if ((cr_type == CR_CORE) || (cr_type == CR_CORE_MEMORY))
				for (j = 0; j < this_node->num_sockets; j++)
					info("cons_res %u update_nodeinfo (+) "
					     "node %s alloc_ cores %u",
					     job->job_id, this_node->node_ptr->name, 
					     this_node->alloc_cores[j]);
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

extern int select_p_update_sub_node (update_part_msg_t *part_desc_ptr)
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

extern int select_p_update_node_state (int index, uint16_t state)
{
	return SLURM_SUCCESS;
}

extern int select_p_alter_node_cnt(enum select_node_cnt type, void *data)
{
	return SLURM_SUCCESS;
}
