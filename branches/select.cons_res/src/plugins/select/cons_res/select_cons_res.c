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
const char plugin_name[] = "Consumable Resources (CR) Node Selection plugin";
const char plugin_type[] = "select/cons_res";
const uint32_t plugin_version = 90;
const uint32_t pstate_version = 6;	/* version control on saved state */

#define CR_JOB_ALLOCATED_CPUS  0x1
#define CR_JOB_ALLOCATED_MEM   0x2

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
static uint32_t last_verified_job_id = 0;
/* verify the job list after every CR_VERIFY_JOB_CYCLE jobs have finished */
#define CR_VERIFY_JOB_CYCLE 2000

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

	if ((select_node_cnt == 0) || (name == NULL))
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
static void _build_cr_node_hash_table (void)
{
	int i, inx;

	xfree (cr_node_hash_table);
	cr_node_hash_table = xmalloc (sizeof (struct node_cr_record *) * 
				select_node_cnt);

	for (i = 0; i < select_node_cnt; i++) {
		if (select_node_ptr[i].node_ptr->name[0] == '\0')
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
extern struct node_cr_record * find_cr_node_record (const char *name) 
{
	int i;

	if ((name == NULL) || (name[0] == '\0')) {
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
			if (strncmp(this_node->node_ptr->name, name, 
				    MAX_SLURM_NAME) == 0) {
				return this_node;
			}
			this_node = this_node->node_next;
		}
		error ("find_cr_node_record: lookup failure using hashtable "
			"for %s", name);
	} 

	/* revert to sequential search */
	else {
		for (i = 0; i < select_node_cnt; i++) {
		        if (strcmp (name, select_node_ptr[i].node_ptr->name) == 0) {
			        debug3("cons_res find_cr_node_record: linear %s",  
					name);
				return (&select_node_ptr[i]);
			}
		}
		error ("find_cr_node_record: lookup failure with linear search "
			"for %s", name);
	}
	error ("find_cr_node_record: lookup failure with both method %s", name);
	return (struct node_cr_record *) NULL;
}

static void _destroy_node_part_array(struct node_cr_record *this_cr_node)
{
	struct part_cr_record *p_ptr;

	if (!this_cr_node)
		return;
	for (p_ptr = this_cr_node->parts; p_ptr; p_ptr = p_ptr->next) {
		xfree(p_ptr->part_name);
		xfree(p_ptr->alloc_cores);
	}
	xfree(this_cr_node->parts);
	this_cr_node->parts = NULL;
}

static void _create_node_part_array(struct node_cr_record *this_cr_node)
{
	struct node_record *node_ptr;
	struct part_cr_record *p_ptr;
	int i;

	if (!this_cr_node)
		return;
	node_ptr = this_cr_node->node_ptr;

	if (this_cr_node->parts) {
		_destroy_node_part_array(this_cr_node);
		this_cr_node->parts = NULL;
	}

	if (node_ptr->part_cnt < 1)
		return;
	this_cr_node->parts = xmalloc(sizeof(struct part_cr_record) *
	        		      node_ptr->part_cnt);
	for (i = 0; i < node_ptr->part_cnt; i++) {
		p_ptr		 = &(this_cr_node->parts[i]);
		p_ptr->part_name = xstrdup(node_ptr->part_pptr[i]->name);
		p_ptr->num_rows  = node_ptr->part_pptr[i]->max_share;
		if (p_ptr->num_rows & SHARED_FORCE)
			p_ptr->num_rows &= (~SHARED_FORCE);
		/* SHARED=EXCLUSIVE sets max_share = 0 */
		if (p_ptr->num_rows < 1)
			p_ptr->num_rows = 1;
#if (CR_DEBUG)
		info("cons_res: _create_node_part_array: part %s  num_rows %d",
		     p_ptr->part_name, p_ptr->num_rows);
#endif
		p_ptr->alloc_cores = xmalloc(sizeof(uint16_t) *
		        		     this_cr_node->num_sockets *
					     p_ptr->num_rows);
		if (i+1 < node_ptr->part_cnt)
			p_ptr->next = &(this_cr_node->parts[i+1]);
		else
			p_ptr->next = NULL;
	}

}

extern struct part_cr_record *get_cr_part_ptr(struct node_cr_record *this_node,
					      const char *part_name)
{
	struct part_cr_record *p_ptr;

	if (part_name == NULL)
		return NULL;

	if (!this_node->parts)
		_create_node_part_array(this_node);

	for (p_ptr = this_node->parts; p_ptr; p_ptr = p_ptr->next) {
		if (strcmp(p_ptr->part_name, part_name) == 0)
			return p_ptr;
	}
	error("cons_res: could not find partition %s", part_name);
	return NULL;
}

static void _chk_resize_node(struct node_cr_record *node, uint16_t sockets)
{
	struct part_cr_record *p_ptr;

	/* This just resizes alloc_cores based on a potential change to
	 * the number of sockets on this node (if fast_schedule = 0?).
	 * Any changes to the number of partition rows will be caught
	 * and adjusted in select_p_reconfigure() */

	if (sockets > node->num_sockets) {
		debug3("cons_res: increasing node %s num_sockets %u to %u",
			node->node_ptr->name, node->num_sockets, sockets);
		for (p_ptr = node->parts; p_ptr; p_ptr = p_ptr->next) {
			xrealloc(p_ptr->alloc_cores,
				 sockets * p_ptr->num_rows * sizeof(uint16_t));
			/* NOTE: xrealloc zero fills added memory */
		}
		node->num_sockets = sockets;
	}
}

static void _chk_resize_job(struct select_cr_job *job, uint16_t node_id, 
			    uint16_t sockets)
{
	if ((job->alloc_cores[node_id] == NULL) ||
	    		(sockets > job->num_sockets[node_id])) {
		debug3("cons_res: increasing job %u node %u "
			"num_sockets from %u to %u",
			job->job_id, node_id, 
			job->num_sockets[node_id], sockets);
	    	xrealloc(job->alloc_cores[node_id], sockets * sizeof(uint16_t));
		/* NOTE: xrealloc zero fills added memory */
		job->num_sockets[node_id] = sockets;
	}
}

extern void get_resources_this_node(uint16_t *cpus, uint16_t *sockets, 
				    uint16_t *cores, uint16_t *threads, 
				    struct node_cr_record *this_cr_node,
				    uint32_t jobid)
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

	debug3("cons_res %u _get_resources host %s HW_ "
	       "cpus %u sockets %u cores %u threads %u ", 
	       jobid, this_cr_node->node_ptr->name,
	       *cpus, *sockets, *cores, *threads);
}

/* _get_cpu_data
 * determine the number of available free cores/cpus/sockets
 * IN - p_ptr:       pointer to a node's part_cr_record for a specific partition
 * IN - num_sockets: number of sockets on this node
 * IN - max_cpus:    the total number of cores/cpus/sockets on this node
 * OUT- row_index:   the row index from which the returned value was obtained
 *                   (if -1 then nothing is allocated in this partition)
 * OUT- free_row:    the row index of an unallocated row (if -1 then all rows
 *                   contain allocated cores)
 * RETURN - the maximum number of free cores/cpus/sockets found in the given
 *          row_index (if 0 then node is full; if 'max_cpus' then node is free)
 */
static uint16_t _get_cpu_data (struct part_cr_record *p_ptr, int num_sockets,
			       uint16_t max_cpus, int *row_index, int *free_row)
{
	int i, j, index;
	uint16_t alloc_count = 0;
	bool counting_sockets = 0;
	if ((cr_type == CR_SOCKET) || (cr_type == CR_SOCKET_MEMORY))
		counting_sockets = 1;
 
 	*free_row = -1;
	*row_index = -1;

	for (i = 0, index = 0; i < p_ptr->num_rows; i++) {
		uint16_t cpu_count = 0;
		uint16_t socket_count = 0;
		for (j = 0; j < num_sockets; j++, index++) {
			if (p_ptr->alloc_cores[index]) {
				socket_count++;
				cpu_count += p_ptr->alloc_cores[index];
			}
		}
		if (socket_count > 0) {
			if (counting_sockets) {
				if ((alloc_count == 0) ||
				    (socket_count < alloc_count)) {
					alloc_count = socket_count;
					*row_index = i;
				}
			} else {
				if ((alloc_count == 0) ||
				    (cpu_count < alloc_count)) {
					alloc_count = cpu_count;
					*row_index = i;
				}
			}
		} 
		else if (*free_row < 0) {
			*free_row = i;
		}
	}
	return max_cpus - alloc_count;
}

/*
 * _get_task_count - Given the job requirements, compute the number of tasks
 *                   this node can run
 *
 * IN job_ptr - pointer to job being scheduled
 * IN index - index of node's configuration information in select_node_ptr
 */
static uint16_t _get_task_count(struct job_record *job_ptr, const int index, 
				const bool all_available, bool try_partial_idle,
				enum node_cr_state job_node_req)
{
	uint16_t numtasks, cpus_per_task = 0;
	uint16_t max_sockets = 0, max_cores = 0, max_threads = 0;
	uint16_t min_sockets = 0, min_cores = 0, min_threads = 0;
	uint16_t ntasks_per_node = 0, ntasks_per_socket = 0, ntasks_per_core = 0;
	uint16_t i, cpus, sockets, cores, threads, *alloc_cores = NULL;
	struct node_cr_record *this_node;
	struct part_cr_record *p_ptr;
	struct multi_core_data *mc_ptr = NULL;

	cpus_per_task   = job_ptr->details->cpus_per_task;
	ntasks_per_node = job_ptr->details->ntasks_per_node;

	if (!job_ptr->details->mc_ptr)
		job_ptr->details->mc_ptr = create_default_mc();
	mc_ptr = job_ptr->details->mc_ptr;
	min_sockets = mc_ptr->min_sockets;
	max_sockets = mc_ptr->max_sockets;
	min_cores   = mc_ptr->min_cores;
	max_cores   = mc_ptr->max_cores;
	min_threads = mc_ptr->min_threads;
	max_threads = mc_ptr->max_threads;
	ntasks_per_socket = mc_ptr->ntasks_per_socket;
	ntasks_per_core   = mc_ptr->ntasks_per_core;

	this_node = &(select_node_ptr[index]);
	get_resources_this_node(&cpus, &sockets, &cores, &threads, 
				this_node, job_ptr->job_id);

	_chk_resize_node(this_node, sockets);
	alloc_cores = xmalloc(sockets * sizeof(uint16_t));
	/* array is zero filled by xmalloc() */

	if (!all_available) {
		p_ptr = get_cr_part_ptr(this_node, job_ptr->partition);
		if (p_ptr) {
			if (job_node_req == NODE_CR_ONE_ROW) {
				/* need to scan over all partitions with
				 * num_rows = 1 */
				for (p_ptr = this_node->parts; p_ptr;
				     p_ptr = p_ptr->next) {
					if (p_ptr->num_rows > 1)
						continue;
					for (i = 0; i < sockets; i++) {
					    if ((cr_type == CR_SOCKET) ||
						(cr_type == CR_SOCKET_MEMORY)) {
						if (p_ptr->alloc_cores[i])
							alloc_cores[i] = cores;
					    } else {
						alloc_cores[i] =
							p_ptr->alloc_cores[i];
					    }
					}
				}
			} else {
				/* job_node_req == EXCLUSIVE | AVAILABLE
				 * if EXCLUSIVE, then node *should* be free and
				 * this code should fall through with
				 * alloc_cores all set to zero.
				 * if AVAILABLE then scan partition rows based
				 * on 'try_partial_idle' setting. Note that
				 * if 'try_partial_idle' is FALSE then this
				 * code should use a 'free' row and this is
				 * where a new row will first be evaluated.
				 */
				uint16_t count, max_cpus;
				int alloc_row, free_row;

				max_cpus = cpus;
				if ((cr_type == CR_SOCKET) ||
				    (cr_type == CR_SOCKET_MEMORY))
					max_cpus = sockets;
				if ((cr_type == CR_CORE) ||
				    (cr_type == CR_CORE_MEMORY))
					max_cpus = cores * sockets;

				count = _get_cpu_data(p_ptr, sockets, max_cpus,
						      &alloc_row, &free_row);
				if ((count == 0) && (free_row == -1)) {
					/* node is completely allocated */
					xfree(alloc_cores);
					return 0;
				}
				if ((free_row == -1) && (!try_partial_idle)) {
					/* no free rows, so partial idle is
					 * all that is left! */
					try_partial_idle = 1;
				}
				if (try_partial_idle && (alloc_row > -1)) {
					alloc_row *= sockets;
					for (i = 0; i < sockets; i++)
						alloc_cores[i] =
						p_ptr->alloc_cores[alloc_row+i];
				}
			}
		}
	}
#if (CR_DEBUG)
	for (i = 0; i < sockets; i+=2) {
		info("cons_res: _get_task_count: %s alloc_cores[%d]=%d, [%d]=%d",
		     this_node->node_ptr->name, i, alloc_cores[i],
		     i+1, alloc_cores[i+1]);
	}
#endif

	numtasks = slurm_get_avail_procs(max_sockets, max_cores, max_threads,
					 min_sockets, min_cores,
					 cpus_per_task,
					 ntasks_per_node,
					 ntasks_per_socket,
					 ntasks_per_core,
					 &cpus, &sockets, &cores,
					 &threads, alloc_cores, 
					 cr_type, job_ptr->job_id,
					 this_node->node_ptr->name);
#if (CR_DEBUG)
	info("cons_res: _get_task_count computed a_tasks %d s %d c %d "
		"t %d on %s for job %d",
		numtasks, sockets, cores, 
		threads, this_node->node_ptr->name, job_ptr->job_id);
#endif
	xfree(alloc_cores);
	return(numtasks);
}		

/* xfree an array of node_cr_record */
static void _xfree_select_nodes(struct node_cr_record *ptr, int count)
{
	int i;
	
	if (ptr == NULL)
		return;

	for (i = 0; i < count; i++) {
		xfree(ptr[i].name);
		_destroy_node_part_array(&(ptr[i]));
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
	xfree(job->alloc_cpus);	
	xfree(job->node_offset);	
	xfree(job->alloc_memory);
	if ((cr_type == CR_CORE)   || (cr_type == CR_CORE_MEMORY) ||
	    (cr_type == CR_SOCKET) || (cr_type == CR_SOCKET_MEMORY)) {
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

static void _verify_select_job_list(uint32_t job_id)
{
	ListIterator job_iterator;
	struct select_cr_job *job;

	if (list_count(select_cr_job_list) < 1) {
		last_verified_job_id = job_id;
		return;
	}
	if ((job_id > last_verified_job_id) &&
	    (job_id < (last_verified_job_id + CR_VERIFY_JOB_CYCLE))) {
		return;
	}

	last_verified_job_id = job_id;
	slurm_mutex_lock(&cr_mutex);
	job_iterator = list_iterator_create(select_cr_job_list);
	while ((job = (struct select_cr_job *) list_next(job_iterator))) {
		if (find_job_record(job->job_id) == NULL) {
			list_remove(job_iterator);
			debug2("cons_res: _verify_job_list: removing "
				"nonexistent job %u", job->job_id);
			_xfree_select_cr_job(job);
		}
	}
	list_iterator_destroy(job_iterator);
	slurm_mutex_unlock(&cr_mutex);	
	last_cr_update_time = time(NULL);
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

/* find the maximum number of idle cpus from all partitions */
static uint16_t _count_idle_cpus(struct node_cr_record *this_node)
{
	struct part_cr_record *p_ptr;
	int i, j, index, idlecpus;
	uint16_t cpus, sockets, cores, threads;

	if (this_node->node_state == NODE_CR_RESERVED)
		return (uint16_t) 0;

	get_resources_this_node(&cpus, &sockets, &cores, &threads, 
				this_node, 0);

	if (!this_node->parts)
		return cpus;

	idlecpus = cpus;
	if (this_node->node_state == NODE_CR_ONE_ROW) {
		/* check single-row partitions for idle CPUs */
		for (p_ptr = this_node->parts; p_ptr; p_ptr = p_ptr->next) {
			if (p_ptr->num_rows > 1)
				continue;
			for (i = 0; i < this_node->num_sockets; i++) {
				if ((cr_type == CR_SOCKET) ||
				    (cr_type == CR_SOCKET_MEMORY)) {
				 	if (p_ptr->alloc_cores[i])
						idlecpus -= cores;
				} else {
					idlecpus -= p_ptr->alloc_cores[i];
				}
			}
			if (idlecpus < 1)
				return (uint16_t) 0;
		}
		return (uint16_t) idlecpus;
	}

	if (this_node->node_state == NODE_CR_AVAILABLE) {
		/* check all partitions for idle CPUs */
		int tmpcpus, max_idle = 0;
		for (p_ptr = this_node->parts; p_ptr; p_ptr = p_ptr->next) {
			for (i = 0, index = 0; i < p_ptr->num_rows; i++) {
				tmpcpus = idlecpus;
				for (j = 0;
				     j < this_node->num_sockets;
				     j++, index++) {
				 	if ((cr_type == CR_SOCKET) ||
				 	    (cr_type == CR_SOCKET_MEMORY)) {
						if (p_ptr->alloc_cores[i])
							tmpcpus -= cores;
					} else {
						tmpcpus -= p_ptr->
							   alloc_cores[index];
					}
				}
				if (tmpcpus > max_idle) {
					max_idle = tmpcpus;
					if (max_idle == idlecpus)
						break;
				}
			}
			if (max_idle == idlecpus)
				break;
		}
		if (this_node->parts)
			idlecpus = max_idle;
	}
	return (uint16_t) idlecpus;
}

static int _synchronize_bitmaps(bitstr_t ** partially_idle_bitmap)
{
	int rc = SLURM_SUCCESS, i, idlecpus;
	bitstr_t *bitmap = bit_alloc(bit_size(avail_node_bitmap));

	debug3("cons_res: Synch size avail %d size idle %d ",
	       bit_size(avail_node_bitmap), bit_size(idle_node_bitmap));

	for (i = 0; i < node_record_count; i++) {
		if (bit_test(avail_node_bitmap, i) != 1)
			continue;

		if (bit_test(idle_node_bitmap, i) == 1) {
			bit_set(bitmap, i);
			continue;
		}
		
		idlecpus = _count_idle_cpus(&(select_node_ptr[i]));
		if (idlecpus)
			bit_set(bitmap, i);
	}

	*partially_idle_bitmap = bitmap;
	if (rc != SLURM_SUCCESS)
		FREE_NULL_BITMAP(bitmap);
	return rc;
}

/* allocate resources to the given job
 *
 * if suspend = 0 then fully add job
 * if suspend = 1 then only add memory
 */
static int _add_job_to_nodes(struct select_cr_job *job, char *pre_err,
			     int suspend)
{
	int i, j, rc = SLURM_SUCCESS;
	uint16_t add_memory = 0;
	uint16_t memset = job->state & CR_JOB_ALLOCATED_MEM;
	uint16_t cpuset = job->state & CR_JOB_ALLOCATED_CPUS;

	if (memset && cpuset)
		return rc;
	if (!memset &&
	    ((cr_type == CR_CORE_MEMORY) || (cr_type == CR_CPU_MEMORY) ||
	     (cr_type == CR_MEMORY) || (cr_type == CR_SOCKET_MEMORY))) {
		job->state |= CR_JOB_ALLOCATED_MEM;
		add_memory = 1;
	}
	if (!cpuset && !suspend)
		job->state |= CR_JOB_ALLOCATED_CPUS;

	for (i = 0; i < job->nhosts; i++) {
		struct node_cr_record *this_node;
		struct part_cr_record *p_ptr;
		uint16_t offset = 0;
		
		this_node = find_cr_node_record (job->host[i]);
		if (this_node == NULL) {
			error("%s: could not find node %s", pre_err,
				job->host[i]);
			rc = SLURM_ERROR;
			continue;
		}
		/* Update this node's allocated resources, starting with
		 * memory (if applicable) */
		
		if (add_memory) {
			this_node->alloc_memory += job->alloc_memory[i];
		}

		if (cpuset || suspend)
			continue;

		this_node->node_state = job->node_req;
		
		_chk_resize_node(this_node, this_node->node_ptr->sockets);
		p_ptr = get_cr_part_ptr(this_node, job->job_ptr->partition);
		if (p_ptr == NULL)
			continue;

		/* The offset could be invalid if the sysadmin reduced the
		 * number of shared rows after this job was allocated. In
		 * this case, we *should* attempt to place this job in
		 * other rows. However, this may be futile if they are all
		 * currently full.
		 * For now, we're going to be lazy and simply NOT "allocate"
		 * this job on the node(s) (hey - you get what you pay for). ;-)
		 * This just means that we will not be accounting for this
		 * job when determining available space for future jobs,
		 * which is relatively harmless (hey, there was space when
		 * this job was first scheduled - if the sysadmin doesn't
		 * like it, then (s)he can terminate the job). ;-)
		 * Note that we are still "allocating" memory for this job
		 * (if requested). 
		 */
		offset = job->node_offset[i];
		if (offset > (this_node->num_sockets * (p_ptr->num_rows - 1))) {
			rc = SLURM_ERROR;
			continue;
		}

		switch (cr_type) {
		case CR_SOCKET_MEMORY:
		case CR_SOCKET:
		case CR_CORE_MEMORY:
		case CR_CORE:
			_chk_resize_job(job, i, this_node->num_sockets);
			for (j = 0; j < this_node->num_sockets; j++) {
				p_ptr->alloc_cores[offset+j] +=
							job->alloc_cores[i][j];
				if (p_ptr->alloc_cores[offset+j] >
						this_node->node_ptr->cores)
					error("%s: Job %u Host %s offset %u "
					      "too many allocated "
					      "cores %u for socket %d",
					      pre_err, job->job_id,
					      this_node->node_ptr->name, offset,
					      p_ptr->alloc_cores[offset+j], j);
			}
			break;
		case CR_CPU_MEMORY:
		case CR_CPU:
			/* "CPU" count is stored in the first "core" */
			p_ptr->alloc_cores[offset] += job->alloc_cpus[i];
			break;
		default:
			break;
		}

		/* Remove debug only */
		debug3("cons_res: %s: Job %u (+) node %s alloc_mem %u state %d",
			pre_err, job->job_id, this_node->name,
			this_node->alloc_memory, this_node->node_state);
		debug3("cons_res: %s: Job %u (+) alloc_ cpus %u offset %u mem %u",
			pre_err, job->job_id, job->alloc_cpus[i],
			job->node_offset[i], job->alloc_memory[i]);
		for (j = 0; j < this_node->num_sockets; j++)
			debug3("cons_res: %s: Job %u (+) node %s alloc_cores[%d] %u",
				pre_err, job->job_id, this_node->name, 
				j, p_ptr->alloc_cores[offset+j]);
	}
	last_cr_update_time = time(NULL);
	return rc;
}

/* deallocate resources that were assigned to this job 
 *
 * if remove_all = 1: deallocate all resources
 * if remove_all = 0: the job has been suspended, so just deallocate CPUs
 */
static int _rm_job_from_nodes(struct select_cr_job *job, char *pre_err,
			      int remove_all)
{
	int i, j, k, rc = SLURM_SUCCESS;

	uint16_t memset = job->state & CR_JOB_ALLOCATED_MEM;
	uint16_t cpuset = job->state & CR_JOB_ALLOCATED_CPUS;
	uint16_t remove_memory = 0;

	if (!memset && !cpuset)
		return rc;
	if (!cpuset && !remove_all)
		return rc;
	if (memset && remove_all &&
	    ((cr_type == CR_CORE_MEMORY) || (cr_type == CR_CPU_MEMORY) ||
	     (cr_type == CR_MEMORY) || (cr_type == CR_SOCKET_MEMORY))) {
	 	remove_memory = 1;
		job->state &= ~CR_JOB_ALLOCATED_MEM;
	}
	if (cpuset)
	 	job->state &= ~CR_JOB_ALLOCATED_CPUS;
	
	for (i = 0; i < job->nhosts; i++) {
		struct node_cr_record *this_node;
		struct part_cr_record *p_ptr;
		uint16_t offset;
		
		this_node = find_cr_node_record(job->host[i]);
		if (this_node == NULL) {
			error("%s: could not find node %s in job %d",
			      pre_err, job->host[i], job->job_id);
			rc = SLURM_ERROR; 
			continue;
		}

		/* Update this nodes allocated resources, beginning with
		 * memory (if applicable) */
		if (remove_memory) {
			if (this_node->alloc_memory >= job->alloc_memory[i])
				this_node->alloc_memory -= job->alloc_memory[i];
			else {
				error("%: alloc_memory underflow on %s",
				      pre_err, this_node->node_ptr->name);
				this_node->alloc_memory = 0;
				rc = SLURM_ERROR;  
			}
		}
		
		if (!cpuset)
			continue;
		
		_chk_resize_node(this_node, this_node->node_ptr->sockets);
		p_ptr = get_cr_part_ptr(this_node, job->job_ptr->partition);
		if (p_ptr == NULL)
			continue;

		/* If the offset is no longer valid, then the job was never
		 * "allocated" on these cores (see add_job_to_nodes).
		 * Therefore just continue. */
		offset = job->node_offset[i];
		if (offset > (this_node->num_sockets * (p_ptr->num_rows - 1))) {
			rc = SLURM_ERROR;
			continue;
		}
		
		switch(cr_type) {
		case CR_SOCKET_MEMORY:
		case CR_SOCKET:
		case CR_CORE_MEMORY:
		case CR_CORE:
			_chk_resize_job(job, i, this_node->num_sockets);
			for (j = 0; j < this_node->num_sockets; j++) {
				if (p_ptr->alloc_cores[offset+j] >= 
						job->alloc_cores[i][j])
					p_ptr->alloc_cores[offset+j] -= 
							job->alloc_cores[i][j];
				else {
					error("%s: alloc_cores underflow on %s",
					      pre_err, this_node->name);
					p_ptr->alloc_cores[offset+j] = 0;
					rc = SLURM_ERROR;
				}
			}
			break;
		case CR_CPU_MEMORY:
		case CR_CPU:
			/* CPU count is stored in the first "core" */
			if (p_ptr->alloc_cores[offset] >= job->alloc_cpus[i])
				p_ptr->alloc_cores[offset] -=
							job->alloc_cpus[i];
			else {
				error("%s: CPU underflow (%u - %u) on %s",
				      pre_err, p_ptr->alloc_cores[offset],
				      job->alloc_cpus[i], this_node->name);
				p_ptr->alloc_cores[offset] = 0;
				rc = SLURM_ERROR;  
			}
			break;
		default:
			break;
		}

		/* if all cores are available, set NODE_CR_AVAILABLE */
		if (this_node->node_state != NODE_CR_AVAILABLE) {
			/* need to scan all partitions */
			struct part_cr_record *pptr;
			int count = 0;
			for (pptr = this_node->parts; pptr; pptr = pptr->next) {
				/* just need to check single row partitions */
				if (pptr->num_rows > 1)
					continue;
				k = pptr->num_rows * this_node->num_sockets;
				for (j = 0; j < k; j++) {
					count += p_ptr->alloc_cores[j];
				}
				if (count)
					break;
			}
			if (count == 0)
				this_node->node_state = NODE_CR_AVAILABLE;
		}

		debug3("%s: Job %u (-) node %s alloc_mem %u offset %d",
			pre_err, job->job_id, this_node->node_ptr->name,
			this_node->alloc_memory, offset);
		for (j = 0; j < this_node->num_sockets; j++)
			debug3("cons_res: %s: Job %u (-) node %s alloc_cores[%d] %u",
				pre_err, job->job_id, this_node->name, 
				j, p_ptr->alloc_cores[offset+j]);
	}
	last_cr_update_time = time(NULL);
	return rc;
}

static bool _enough_nodes(int avail_nodes, int rem_nodes, 
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
	fatal("%s is incompatible with XCPU use", plugin_name);
#endif
#ifdef HAVE_BG
	fatal("%s is incompatable with Blue Gene", plugin_name);
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
	uint32_t nhosts = job->nhosts;

	pack32(job->job_id, buffer);
	pack16(job->state, buffer);
	pack32(job->nprocs, buffer);
	pack32(job->nhosts, buffer);
	pack16(job->node_req, buffer);

	packstr_array(job->host, nhosts, buffer);
	pack16_array(job->cpus, nhosts, buffer);
	pack16_array(job->alloc_cpus, nhosts, buffer);
	pack16_array(job->node_offset, nhosts, buffer);

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

	pack_bit_fmt(job->node_bitmap, buffer);
	pack16(_bitstr_bits(job->node_bitmap), buffer);

	return 0;
}

static int _cr_unpack_job(struct select_cr_job *job, Buf buffer)
{
    	int i;
    	uint16_t have_alloc_cores;
    	uint32_t len32;
	uint32_t nhosts = 0;
	char *bit_fmt = NULL;
	uint16_t bit_cnt; 

	safe_unpack32(&job->job_id, buffer);
	safe_unpack16(&job->state, buffer);
	safe_unpack32(&job->nprocs, buffer);
	safe_unpack32(&job->nhosts, buffer);
	safe_unpack16(&bit_cnt, buffer);
	nhosts = job->nhosts;
	job->node_req = bit_cnt;
	
	safe_unpackstr_array(&job->host, &len32, buffer);
	if (len32 != nhosts) {
		error("cons_res unpack_job: expected %u hosts, saw %u",
			nhosts, len32);
		goto unpack_error;
	}

	safe_unpack16_array(&job->cpus, &len32, buffer);
	safe_unpack16_array(&job->alloc_cpus, &len32, buffer);
	safe_unpack16_array(&job->node_offset, &len32, buffer);

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

	safe_unpackstr_xmalloc(&bit_fmt, &len32, buffer);
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
	struct select_cr_job *job = NULL;
	Buf buffer = NULL;
	int state_fd, i;
	uint16_t job_cnt;
	char *file_name = NULL;
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
                error("Can't save state, error creating file %s", file_name);
		xfree(file_name);
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
		pack16(select_node_ptr[i].num_sockets, buffer);
		/*** don't bother packing allocated resources ***/
		/*** they will be recovered from the job data ***/
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
	int i, tmp, prev_i;

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
		
		/* set alloc_memory/cores to 0, and let
		 * select_p_update_nodeinfo to recover the current info
		 * from jobs (update_nodeinfo is called from reset_job_bitmaps) */
		select_node_ptr[i].alloc_memory = 0;
		select_node_ptr[i].node_state = NODE_CR_AVAILABLE;
		/* recreate to ensure that everything is zero'd out */
		_create_node_part_array(&(select_node_ptr[i]));
		_chk_resize_node(&(select_node_ptr[i]),
			    prev_select_node_ptr[prev_i].num_sockets);
	}

	/* Release any previous node data */
	_xfree_select_nodes(prev_select_node_ptr, prev_select_node_cnt);
	prev_select_node_ptr = NULL;
	prev_select_node_cnt = 0;
}

/* This is Part 2 of a 4-part procedure which can be found in
 * src/slurmctld/read_config.c. See select_p_node_init for the
 * whole story.
 */
extern int select_p_state_restore(char *dir_name)
{
	int error_code = SLURM_SUCCESS;
	int state_fd, i;
	char *file_name = NULL;
	struct select_cr_job *job;
	Buf buffer = NULL;
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
	safe_unpackstr_xmalloc(&restore_plugin_type, &len32, buffer);
	safe_unpack32(&restore_plugin_version, buffer);
	safe_unpack16(&restore_plugin_crtype,  buffer);
	safe_unpack32(&restore_pstate_version, buffer);

	if ((strcmp(restore_plugin_type, plugin_type) != 0) ||
	    (restore_plugin_version != plugin_version) ||
	    (restore_plugin_crtype  != cr_type) ||
	    (restore_pstate_version != pstate_version)) { 
		error ("Can't restore state, state version mismatch: "
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
		job->job_ptr = find_job_record(job->job_id);
		if (job->job_ptr) {
			list_append(select_cr_job_list, job);
			debug2("recovered cons_res job data for job %u", 
				job->job_id);
		} else {
			error("recovered cons_res job data for unexistent job %u", 
				job->job_id);
			_xfree_select_cr_job(job);
		}
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
		/*** don't restore prev_select_node_ptr[i].node_ptr ***/
		safe_unpackstr_xmalloc(&(prev_select_node_ptr[i].name), 
				       &len32, buffer);
		safe_unpack16(&prev_select_node_ptr[i].num_sockets, buffer);
		prev_select_node_ptr[i].node_ptr     = NULL;
		prev_select_node_ptr[i].node_state   = NODE_CR_AVAILABLE;
		prev_select_node_ptr[i].alloc_memory = 0;
		prev_select_node_ptr[i].parts        = NULL;
		prev_select_node_ptr[i].node_next    = NULL;
		/*** there's no resource data to unpack  -  ***/
		/*** it will be recovered from the job data ***/
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

/* This is Part 3 of a 4-part procedure which can be found in
 * src/slurmctld/read_config.c. See select_p_node_init for the
 * whole story.
 */
extern int select_p_job_init(List job_list)
{
	info("cons_res: select_p_job_init");

    	if (!select_cr_job_list) {
		select_cr_job_list = list_create(NULL);
	}

	/* Note: select_cr_job_list restored in select_p_state_restore */

	return SLURM_SUCCESS;
}

/* This is Part 1 of a 4-part procedure which can be found in
 * src/slurmctld/read_config.c. The whole story goes like this:
 *
 * Step 1: select_g_node_init       : initializes 'select_node_ptr' global array
 *                                    sets node_ptr, node_name, and num_sockets
 * Step 2: select_g_state_restore   : IFF a cons_res state file exists:
 *                                    loads global 'select_cr_job_list' with
 *                                    saved job data
 *                                    also loads 'prev_select_node_ptr' global
 *                                    array with saved node_name and num_sockets
 * Step 3: select_g_job_init        : creates global 'select_cr_job_list' if
 *                                    nothing was recovered from state file.
 * Step 4: select_g_update_nodeinfo : called from reset_job_bitmaps() with each
 *                                    valid recovered job_ptr AND from
 *                                    select_nodes(), this procedure adds job
 *                                    data to the 'select_node_ptr' global array
 */
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
		select_node_ptr[i].num_sockets = node_ptr[i].sockets;
		select_node_ptr[i].node_state = NODE_CR_AVAILABLE;
		select_node_ptr[i].alloc_memory = 0;
		select_node_ptr[i].parts = NULL;
		_create_node_part_array(&(select_node_ptr[i]));
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

/* return the number of tasks that the given
 * job can run on the indexed node */
static int _get_task_cnt(struct job_record *job_ptr, const int node_index,
			 int *task_cnt, int *freq, int size)
{
	int i, pos, tasks;
	uint16_t * layout_ptr = NULL;

	layout_ptr = job_ptr->details->req_node_layout;

	pos = 0;
	for (i = 0; i < size; i++) {
		if (pos+freq[i] > node_index)
			break;
		pos += freq[i];
	}
	tasks = task_cnt[i];
	if (layout_ptr && bit_test(job_ptr->details->req_node_bitmap, i)) {
		pos = bit_get_pos_num(job_ptr->details->req_node_bitmap, i);
		tasks = MIN(tasks, layout_ptr[pos]);
	} else if (layout_ptr) {
		tasks = 0; /* should not happen? */
	}
	return tasks;
}

static int _eval_nodes(struct job_record *job_ptr, bitstr_t * bitmap,
		       uint32_t min_nodes, uint32_t max_nodes,
		       uint32_t req_nodes, int *task_cnt, int *freq, 
		       int array_size)
{
	int i, f, index, error_code = SLURM_ERROR;
	int *consec_nodes;	/* how many nodes we can add from this 
				 * consecutive set of nodes */
	int *consec_cpus;	/* how many nodes we can add from this 
				 * consecutive set of nodes */
	int *consec_start;	/* where this consecutive set starts (index) */
	int *consec_end;	/* where this consecutive set ends (index) */
	int *consec_req;	/* are nodes from this set required 
				 * (in req_bitmap) */
	int consec_index, consec_size, sufficient;
	int rem_cpus, rem_nodes;	/* remaining resources desired */
	int best_fit_nodes, best_fit_cpus, best_fit_req;
	int best_fit_sufficient, best_fit_index = 0;
	int avail_cpus, ll;	/* ll = layout array index */
	struct multi_core_data *mc_ptr = NULL;
	uint16_t * layout_ptr = NULL;

	xassert(bitmap);
	
	if (bit_set_count(bitmap) < min_nodes)
		return error_code;

	layout_ptr = job_ptr->details->req_node_layout;
	mc_ptr = job_ptr->details->mc_ptr;

	consec_size = 50;	/* start allocation for 50 sets of 
				 * consecutive nodes */
	consec_cpus  = xmalloc(sizeof(int) * consec_size);
	consec_nodes = xmalloc(sizeof(int) * consec_size);
	consec_start = xmalloc(sizeof(int) * consec_size);
	consec_end   = xmalloc(sizeof(int) * consec_size);
	consec_req   = xmalloc(sizeof(int) * consec_size);

	/* Build table with information about sets of consecutive nodes */
	consec_index = 0;
	consec_cpus[consec_index] = consec_nodes[consec_index] = 0;
	consec_req[consec_index] = -1;	/* no required nodes here by default */

	rem_cpus = job_ptr->num_procs;
	if (req_nodes > min_nodes)
		rem_nodes = req_nodes;
	else
		rem_nodes = min_nodes;

	i = 0;
	f = 0;
	for (index = 0, ll = -1; index < select_node_cnt; index++, f++) {
		if (f >= freq[i]) {
			f = 0;
			i++;
		}
		bool required_node = false;
		if (job_ptr->details->req_node_bitmap) {
			required_node =
				bit_test(job_ptr->details->req_node_bitmap,
					 index);
		}
		if (layout_ptr && required_node)
			ll++;
		if (bit_test(bitmap, index)) {
			if (consec_nodes[consec_index] == 0)
				consec_start[consec_index] = index;
			avail_cpus = task_cnt[i];
			if (layout_ptr && required_node){
				avail_cpus = MIN(avail_cpus, layout_ptr[ll]);
			} else if (layout_ptr) {
				avail_cpus = 0; /* should not happen? */
			}
			if ((max_nodes > 0) && required_node) {
				if (consec_req[consec_index] == -1) {
					/* first required node in set */
					consec_req[consec_index] = index;
				}
				rem_cpus -= avail_cpus;
				rem_nodes--;
				/* leaving bitmap set, decrement max limit */
				max_nodes--;
			} else {	/* node not selected (yet) */
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
				xrealloc(consec_cpus, sizeof(int)*consec_size);
				xrealloc(consec_nodes, sizeof(int)*consec_size);
				xrealloc(consec_start, sizeof(int)*consec_size);
				xrealloc(consec_end, sizeof(int)*consec_size);
				xrealloc(consec_req, sizeof(int)*consec_size);
			}
			consec_cpus[consec_index] = 0;
			consec_nodes[consec_index] = 0;
			consec_req[consec_index] = -1;
		}
	}
	if (consec_nodes[consec_index] != 0)
		consec_end[consec_index++] = index - 1;
	
	for (i = 0; i < consec_index; i++) {
		debug3("cons_res: eval_nodes: %d consec c=%d n=%d b=%d e=%d r=%d",
			i, consec_cpus[i], consec_nodes[i], consec_start[i],
			consec_end[i], consec_req[i]);
	}
	
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
			    ((best_fit_req == -1) && (consec_req[i] != -1)) ||
			    (sufficient && (best_fit_sufficient == 0)) ||
			    (sufficient && (consec_cpus[i] < best_fit_cpus)) ||
			    (!sufficient && (consec_cpus[i] > best_fit_cpus))) {
				best_fit_cpus = consec_cpus[i];
				best_fit_nodes = consec_nodes[i];
				best_fit_index = i;
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
			     i <= consec_end[best_fit_index]; i++) {
				if ((max_nodes <= 0)
				    ||  ((rem_nodes <= 0) && (rem_cpus <= 0)))
					break;
				if (bit_test(bitmap, i))
					continue;
				bit_set(bitmap, i);
				rem_nodes--;
				max_nodes--;
				avail_cpus = _get_task_cnt(job_ptr, i,
							   task_cnt, freq,
							   array_size);
				rem_cpus -= avail_cpus;
			}
			for (i = (best_fit_req - 1);
			     i >= consec_start[best_fit_index]; i--) {
				if ((max_nodes <= 0)
				    ||  ((rem_nodes <= 0) && (rem_cpus <= 0)))
					break;
				if (bit_test(bitmap, i)) 
					continue;
				avail_cpus = _get_task_cnt(job_ptr, i,
							   task_cnt, freq,
							   array_size);
				if(avail_cpus <= 0)
					continue;
				rem_cpus -= avail_cpus;
				bit_set(bitmap, i);
				rem_nodes--;
				max_nodes--;
			}
		} else {
			for (i = consec_start[best_fit_index];
			     i <= consec_end[best_fit_index]; i++) {
				if ((max_nodes <= 0)
				    || ((rem_nodes <= 0) && (rem_cpus <= 0)))
					break;
				if (bit_test(bitmap, i))
					continue;
				avail_cpus = _get_task_cnt(job_ptr, i,
							   task_cnt, freq,
							   array_size);
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
		consec_cpus[best_fit_index] = 0;
		consec_nodes[best_fit_index] = 0;
	}
	
	if (error_code && (rem_cpus <= 0)
	    && _enough_nodes(0, rem_nodes, min_nodes, req_nodes))
		error_code = SLURM_SUCCESS;

	xfree(consec_cpus);
	xfree(consec_nodes);
	xfree(consec_start);
	xfree(consec_end);
	xfree(consec_req);
	return error_code;
}

/* this is an intermediary step between select_p_job_test and _eval_nodes
 * to tackle the knapsack problem. This code incrementally removes nodes
 * with low task counts for the job and re-evaluates each result */
static int _select_nodes(struct job_record *job_ptr, bitstr_t * bitmap,
			 uint32_t min_nodes, uint32_t max_nodes, 
			 uint32_t req_nodes, int *task_cnt, int *freq, 
			 int array_size)
{
	int i, b, count, ec, most_tasks = 0;
	bitstr_t *origmap, *reqmap = NULL;

	/* allocated node count should never exceed num_procs, right? 
	 * if so, then this should be done earlier and max_nodes
	 * could be used to make this process more efficient (truncate
	 * # of available nodes when (# of idle nodes == max_nodes)*/
	if (max_nodes > job_ptr->num_procs)
		max_nodes = job_ptr->num_procs;

	origmap = bit_copy(bitmap);
	if (origmap == NULL)
		fatal("bit_copy malloc failure");

	ec = _eval_nodes(job_ptr, bitmap, min_nodes, max_nodes,
			 req_nodes, task_cnt, freq, array_size);

	if (ec == SLURM_SUCCESS) {
		bit_free(origmap);
		return ec;
	}

	/* This nodeset didn't work. To avoid a possible knapsack problem, 
	 * incrementally remove nodes with low task counts and retry */

	for (i = 0; i < array_size; i++) {
		if (task_cnt[i] > most_tasks)
			most_tasks = task_cnt[i];
	}

	if (job_ptr->details->req_node_bitmap)
		reqmap = job_ptr->details->req_node_bitmap;
	
	for (count = 0; count < most_tasks; count++) {
		int nochange = 1;
		bit_or(bitmap, origmap);
		for (i = 0, b = 0; i < array_size; i++) {
			if (task_cnt[i] != -1 && task_cnt[i] <= count) {
				int j = 0, x = b;
				for (; j < freq[i]; j++, x++) {
					if (!bit_test(bitmap, x))
						continue;
					if (reqmap && bit_test(reqmap, x)) {
						bit_free(origmap);
						return SLURM_ERROR;
					}
					nochange = 0;
					bit_clear(bitmap, x);
					bit_clear(origmap, x);
				}
			}
			b += freq[i];
		}
		if (nochange)
			continue;
		ec = _eval_nodes(job_ptr, bitmap, min_nodes, max_nodes,
				 req_nodes, task_cnt, freq, array_size);
		if (ec == SLURM_SUCCESS) {
			bit_free(origmap);
			return ec;
		}
	}
	bit_free(origmap);
	return ec;
}

/* test to see if any shared partitions are running jobs */
static int _is_node_sharing(struct node_cr_record *this_node)
{
	int i, size;
	struct part_cr_record *p_ptr = this_node->parts;
	for (; p_ptr; p_ptr = p_ptr->next) {
		if (p_ptr->num_rows < 2)
			continue;
		size = p_ptr->num_rows * this_node->num_sockets;
		for (i = 0; i < size; i++) {
			if (p_ptr->alloc_cores[i])
				return 1;
		}
	}
	return 0;

}

/* test to see if the given node has any jobs running on it */
static int _is_node_busy(struct node_cr_record *this_node)
{
	int i, size;
	struct part_cr_record *p_ptr = this_node->parts;
	for (; p_ptr; p_ptr = p_ptr->next) {
		size = p_ptr->num_rows * this_node->num_sockets;
		for (i = 0; i < size; i++) {
			if (p_ptr->alloc_cores[i])
				return 1;
		}
	}
	return 0;
}

/*
 * Determine which of these nodes are usable by this job
 *
 * Remove nodes from the bitmap that don't have enough memory to
 * support the job. Return SLURM_ERROR if a required node doesn't
 * have enough memory.
 *
 * if node_state = NODE_CR_RESERVED, clear bitmap (if node is required
 *                                   then should we return NODE_BUSY!?!)
 *
 * if node_state = NODE_CR_ONE_ROW, then this node can only be used by
 *                                  another NODE_CR_ONE_ROW job
 *
 * if node_state = NODE_CR_AVAILABLE AND:
 *  - job_node_req = NODE_CR_RESERVED, then we need idle nodes
 *  - job_node_req = NODE_CR_ONE_ROW, then we need idle or non-sharing nodes
 */
static int _verify_node_state(struct job_record *job_ptr, bitstr_t * bitmap,
			      enum node_cr_state job_node_req)
{
	int i, free_mem;

	for (i = 0; i < node_record_count; i++) {
		if (!bit_test(bitmap, i))
			continue;

		if (job_ptr->details->job_max_memory) {
			if (select_fast_schedule) {
				free_mem = select_node_ptr[i].node_ptr->
					config_ptr->real_memory;
			} else {
				free_mem = select_node_ptr[i].node_ptr->
					real_memory;
			}
			free_mem -= select_node_ptr[i].alloc_memory;
			if (free_mem < job_ptr->details->job_max_memory)
				goto clear_bit;
		}

		if (select_node_ptr[i].node_state == NODE_CR_RESERVED) {
			goto clear_bit;
		} else if (select_node_ptr[i].node_state == NODE_CR_ONE_ROW) {
			if ((job_node_req == NODE_CR_RESERVED) ||
			    (job_node_req == NODE_CR_AVAILABLE))
				goto clear_bit;
			/* cannot use this node if it is running jobs
			 * in sharing partitions */
			if ( _is_node_sharing(&(select_node_ptr[i])) )
				goto clear_bit;
		} else {	/* node_state = NODE_CR_AVAILABLE */
			if (job_node_req == NODE_CR_RESERVED) {
				if ( _is_node_busy(&(select_node_ptr[i])) )
					goto clear_bit;
			} else if (job_node_req == NODE_CR_ONE_ROW) {
				if ( _is_node_sharing(&(select_node_ptr[i])) )
					goto clear_bit;
			}
		}
		continue;	/* node is usable, test next node */

		/* This node is not usable by this job */
 clear_bit:	bit_clear(bitmap, i);
		if (job_ptr->details->req_node_bitmap &&
		    bit_test(job_ptr->details->req_node_bitmap, i))
			return SLURM_ERROR;

	}

	return SLURM_SUCCESS;
}

/* Determine the node requirements for the job:
 * - does the job need exclusive nodes? (NODE_CR_RESERVED)
 * - can the job run on shared nodes?   (NODE_CR_ONE_ROW)
 * - can the job run on overcommitted resources? (NODE_CR_AVAILABLE)
 */
static enum node_cr_state _get_job_node_req(struct job_record *job_ptr)
{
	int max_share = job_ptr->part_ptr->max_share;
	
	if (max_share == 0)
		return NODE_CR_RESERVED;
	
	if (max_share & SHARED_FORCE)
		return NODE_CR_AVAILABLE;

	/* Shared=NO or Shared=YES */
	if (job_ptr->details->shared == 0)
		/* user has requested exclusive nodes */
		return NODE_CR_RESERVED;
	if ((max_share > 1) && (job_ptr->details->shared == 1))
		/* part allows sharing, and
		 * the user has requested it */
		return NODE_CR_AVAILABLE;
	return NODE_CR_ONE_ROW;
}

static int _get_allocated_rows(struct job_record *job_ptr, int n,
			       enum node_cr_state job_node_req)
{
	struct part_cr_record *p_ptr;
	int i, j, rows = 0;
	
	p_ptr = get_cr_part_ptr(&(select_node_ptr[n]), job_ptr->partition);
	if (p_ptr == NULL)
		return rows;

	for (i = 0; i < p_ptr->num_rows; i++) {
		int offset = i * select_node_ptr[n].num_sockets;
		for (j = 0; j < select_node_ptr[n].num_sockets; j++){
			if (p_ptr->alloc_cores[offset+j]) {
				rows++;
				break;
			}
		}
	}
	return rows;
}

static int _load_arrays(struct job_record *job_ptr, bitstr_t *bitmap, 
			int **a_rows, int **s_tasks, int **a_tasks, 
			int **freq, bool test_only,
			enum node_cr_state job_node_req)
{
	int i, index = 0, size = 32;
	int *busy_rows, *shr_tasks, *all_tasks, *num_nodes;
	
	busy_rows = xmalloc (sizeof(int)*size); /* allocated rows */
	shr_tasks = xmalloc (sizeof(int)*size); /* max free cpus */
	all_tasks = xmalloc (sizeof(int)*size); /* all cpus */
	num_nodes = xmalloc (sizeof(int)*size); /* number of nodes */
	/* above arrays are all zero filled by xmalloc() */

	for (i = 0; i < select_node_cnt; i++) {
		if (bit_test(bitmap, i)) {
			int rows;
			uint16_t atasks, ptasks;
			rows = _get_allocated_rows(job_ptr, i, job_node_req);
			/* false = use free rows (if available) */
			atasks = _get_task_count(job_ptr, i, test_only, false,
						 job_node_req);
			if (test_only) {
				ptasks = atasks;
			} else {
				/* true = try using an already allocated row */
				ptasks = _get_task_count(job_ptr, i, test_only,
							 true, job_node_req);
			}
			if (rows   != busy_rows[index] ||
			    ptasks != shr_tasks[index] ||
			    atasks != all_tasks[index]) {
				if (num_nodes[index]) {
					index++;
					if (index >= size) {
						size *= 2;
						xrealloc(busy_rows,
							 sizeof(int)*size);
						xrealloc(shr_tasks,
							 sizeof(int)*size);
						xrealloc(all_tasks,
							 sizeof(int)*size);
						xrealloc(num_nodes,
							 sizeof(int)*size);
					}
					num_nodes[index] = 0;
				}
				busy_rows[index] = rows;
				shr_tasks[index] = ptasks;
				all_tasks[index] = atasks;
			}
		} else {
			if (busy_rows[index] != -1) {
				if (num_nodes[index] > 0) {
					index++;
					if (index >= size) {
						size *= 2;
						xrealloc(busy_rows,
							 sizeof(int)*size);
						xrealloc(shr_tasks,
							 sizeof(int)*size);
						xrealloc(all_tasks,
							 sizeof(int)*size);
						xrealloc(num_nodes,
							 sizeof(int)*size);
					}
					num_nodes[index] = 0;
				}
				busy_rows[index] = -1;
				shr_tasks[index]  = -1;
				all_tasks[index]  = -1;
			}
		}
		num_nodes[index]++;
	}
	/* array_index becomes "array size" */
	index++;

	for (i = 0; i < index; i++) {
		debug3("cons_res: i %d row %d ptasks %d atasks %d freq %d",
		     i, busy_rows[i], shr_tasks[i], all_tasks[i], num_nodes[i]);
	}

	*a_rows  = busy_rows;
	*s_tasks = shr_tasks;
	*a_tasks = all_tasks;
	*freq    = num_nodes;

	return index;
}

/*
 * select_p_job_test - Given a specification of scheduling requirements, 
 *	identify the nodes which "best" satisfy the request.
 * 	"best" is defined as either a minimal number of consecutive nodes
 *	or if sharing resources then sharing them with a job of similar size.
 * IN job_ptr - pointer to job being scheduled
 * IN/OUT bitmap - usable nodes are set on input, nodes not required to 
 *	satisfy the request are cleared, other left set
 * IN min_nodes - minimum count of nodes
 * IN req_nodes - requested (or desired) count of nodes
 * IN max_nodes - maximum count of nodes (0==don't care)
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
 * NOTE: bitmap must be a superset of req_nodes at the time that 
 *	select_p_job_test is called
 */
extern int select_p_job_test(struct job_record *job_ptr, bitstr_t * bitmap,
			     uint32_t min_nodes, uint32_t max_nodes, 
			     uint32_t req_nodes, int mode)
{
	int a, f, i, j, k, error_code, ll; /* ll = layout array index */
	struct multi_core_data *mc_ptr = NULL;
	static struct select_cr_job *job;
	uint16_t * layout_ptr = NULL;
	enum node_cr_state job_node_req;
	int  array_size;
	int *busy_rows, *sh_tasks, *al_tasks, *freq;
	bitstr_t *origmap, *reqmap = NULL;
	int row, rows, try;
	bool test_only;

	xassert(bitmap);

	if (mode == SELECT_MODE_TEST_ONLY)
		test_only = true;
	else if (mode == SELECT_MODE_RUN_NOW)
		test_only = false;
	else	/* SELECT_MODE_WILL_RUN */
		return EINVAL;	/* not yet supported */

	if (!job_ptr->details)
		return EINVAL;

	layout_ptr = job_ptr->details->req_node_layout;
	if (!job_ptr->details->mc_ptr)
		job_ptr->details->mc_ptr = create_default_mc();
	mc_ptr = job_ptr->details->mc_ptr;
	reqmap = job_ptr->details->req_node_bitmap;
	job_node_req = _get_job_node_req(job_ptr);

	debug3("cons_res: select_p_job_test: job %d node_req %d, test_only %d",
	       job_ptr->job_id, job_node_req, test_only);
	debug3("cons_res: select_p_job_test: min_n %u max_n %u req_n %u",
	       min_nodes, max_nodes, req_nodes);
	
	/* check node_state and update bitmap as necessary */
	if (!test_only) {
#if 0
		/* Done in slurmctld/node_scheduler.c: _pick_best_nodes() */
		if ((cr_type != CR_CORE_MEMORY) && (cr_type != CR_CPU_MEMORY) &&
		    (cr_type != CR_MEMORY) && (cr_type != CR_SOCKET_MEMORY))
			job_ptr->details->job_max_memory = 0;
#endif
		error_code = _verify_node_state(job_ptr, bitmap, job_node_req);
		if (error_code != SLURM_SUCCESS)
			return error_code;
	}

	/* This is the case if -O/--overcommit  is true */ 
	debug3("job_ptr->num_procs %u", job_ptr->num_procs);
	if (job_ptr->num_procs == job_ptr->details->min_nodes) {
		job_ptr->num_procs *= MAX(1, mc_ptr->min_threads);
		job_ptr->num_procs *= MAX(1, mc_ptr->min_cores);
		job_ptr->num_procs *= MAX(1, mc_ptr->min_sockets);
	}

	/* compute condensed arrays of node allocation data */
	array_size = _load_arrays(job_ptr, bitmap, &busy_rows, &sh_tasks,
				   &al_tasks, &freq, test_only, job_node_req);

	if (test_only) {
        	/* try with all nodes and all possible cpus */
		error_code = _select_nodes(job_ptr, bitmap, min_nodes,
					   max_nodes, req_nodes, al_tasks, freq,
					   array_size);
		xfree(busy_rows);
		xfree(sh_tasks);
		xfree(al_tasks);
		xfree(freq);
		return error_code;
	}

	origmap = bit_copy(bitmap);
	if (origmap == NULL)
		fatal("bit_copy malloc failure");

	error_code = SLURM_ERROR;
	rows = job_ptr->part_ptr->max_share & ~SHARED_FORCE;
	for (row = 1; row <= rows; row++) {

		/*
		 * first try : try "as is"
		 * second try: only add a row to nodes with no free cpus
		 * third try : add a row to nodes with some alloc cpus
		 */
		for (try = 0; try < 3; try++) {
			bit_or(bitmap, origmap);

			debug3("cons_res: cur row = %d, try = %d", row, try);

			for (i = 0, f = 0; i < array_size; i++) {

				/* Step 1:
				 * remove nodes from bitmap (unless required)
				 * who's busy_rows value is bigger than 'row'.
				 * Why? to enforce "least-loaded" over
				 *      "contiguous" */
				if ((busy_rows[i] > row) ||
				    (busy_rows[i] == row && sh_tasks[i] == 0)) {
					for (j = f; j < f+freq[i]; j++) {
						if (reqmap &&
						    bit_test(reqmap, j))
							continue;
						bit_clear(bitmap, j);
					}
				}
				f += freq[i];

				if (try == 0)
					continue;
				/* Step 2:
				 * set sh_tasks = al_tasks for nodes who's
				 *      busy_rows value is < 'row'.
				 * Why? to select a new row for these
				 *      nodes when appropriate */
				if ((busy_rows[i] == -1) || 
				    (busy_rows[i] >= row))
					continue;
				if (sh_tasks[i] == al_tasks[i])
					continue;
				if ((try == 1) && (sh_tasks[i] != 0))
					continue;
				sh_tasks[i] = al_tasks[i];
			}
			if (bit_set_count(bitmap) < min_nodes)
				break;

			for (i = 0; i < array_size; i++) {
				debug3("cons_res: i %d row %d stasks %d "
					"atasks %d freq %d",
					i, busy_rows[i], sh_tasks[i],
					al_tasks[i], freq[i]);
			}

			error_code = _select_nodes(job_ptr, bitmap, min_nodes,
						   max_nodes, req_nodes,
						   sh_tasks, freq, array_size);
			if (error_code == SLURM_SUCCESS)
				break;
		}
		if (error_code == SLURM_SUCCESS)
			break;
	}

	bit_free(origmap);
	if (error_code != SLURM_SUCCESS) {
		xfree(busy_rows);
		xfree(sh_tasks);
		xfree(al_tasks);
		xfree(freq);
		return error_code;
	}
	
	/* allocate the job and distribute the tasks appropriately */
	job = xmalloc(sizeof(struct select_cr_job));
	job->job_ptr = job_ptr;
	job->job_id = job_ptr->job_id;
	job->nhosts = bit_set_count(bitmap);
	job->nprocs = MAX(job_ptr->num_procs, job->nhosts);
	job->node_req  = job_node_req;

	job->node_bitmap = bit_copy(bitmap);
	if (job->node_bitmap == NULL)
		fatal("bit_copy malloc failure");

	job->host          = (char **)    xmalloc(job->nhosts * sizeof(char *));
	job->cpus          = (uint16_t *) xmalloc(job->nhosts * sizeof(uint16_t));
	job->alloc_cpus    = (uint16_t *) xmalloc(job->nhosts * sizeof(uint16_t));
	job->node_offset   = (uint16_t *) xmalloc(job->nhosts * sizeof(uint16_t));
	job->alloc_memory  = (uint32_t *) xmalloc(job->nhosts * sizeof(uint32_t));
	if ((cr_type == CR_CORE)   || (cr_type == CR_CORE_MEMORY) ||
	    (cr_type == CR_SOCKET) || (cr_type == CR_SOCKET_MEMORY)) {
		job->num_sockets   = (uint16_t *)  xmalloc(job->nhosts * 
							   sizeof(uint16_t));
		job->alloc_cores   = (uint16_t **) xmalloc(job->nhosts * 
							   sizeof(uint16_t *));
		for (i = 0; i < job->nhosts; i++) {
			job->num_sockets[i] = node_record_table_ptr[i].sockets;
			job->alloc_cores[i] = (uint16_t *) xmalloc(
				job->num_sockets[i] * sizeof(uint16_t));
		}
	}

	j = 0;
	a = 0;
	f = 0;
	row = 0; /* total up all available cpus for --overcommit scenarios */
	for (i = 0, ll = -1; i < node_record_count; i++, f++) {
		if (f >= freq[a]) {
			f = 0;
			a++;
		}
		if (layout_ptr
		    && bit_test(job_ptr->details->req_node_bitmap, i)) {
			ll++;
		}
		if (bit_test(bitmap, i) == 0)
			continue;
		if (j >= job->nhosts) {
			error("select_cons_res: job nhosts too small\n");
			break;
		}
		job->host[j] = xstrdup(node_record_table_ptr[i].name);
		job->cpus[j] = sh_tasks[a];
		row += sh_tasks[a];
		if (layout_ptr
		    && bit_test(job_ptr->details->req_node_bitmap, i)) {
			job->cpus[j] = MIN(job->cpus[j], layout_ptr[ll]);
		} else if (layout_ptr) {
			job->cpus[j] = 0;
		}
		job->alloc_cpus[j] = 0;
		job->alloc_memory[j] = job_ptr->details->job_max_memory; 
		if ((cr_type == CR_CORE) || (cr_type == CR_CORE_MEMORY)||
		    (cr_type == CR_SOCKET) || (cr_type == CR_SOCKET_MEMORY)) {
			_chk_resize_job(job, j, node_record_table_ptr[i].sockets);
			job->num_sockets[j] = node_record_table_ptr[i].sockets;
			for (k = 0; k < job->num_sockets[j]; k++)
				job->alloc_cores[j][k] = 0;
		}
		j++;
	}

	xfree(busy_rows);
	xfree(sh_tasks);
	xfree(al_tasks);
	xfree(freq);

	/* When 'srun --overcommit' is used, nprocs is set to a minimum value
	 * in order to allocate the appropriate number of nodes based on the
	 * job request.
	 * For cons_res, all available logical processors will be allocated on
	 * each allocated node in order to accommodate the overcommit request.
	 */
	if (job_ptr->details->overcommit)
		job->nprocs = MIN(row, job_ptr->details->num_tasks);

	if (job_ptr->details->shared == 0) {
		/* Nodes need to be allocated in dedicated
		   mode. User has specified the --exclusive switch */
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
			error_code = cr_dist(job, 0, cr_type); 
			break;
		case SLURM_DIST_BLOCK:
		case SLURM_DIST_CYCLIC:				
		case SLURM_DIST_BLOCK_CYCLIC:
		case SLURM_DIST_CYCLIC_CYCLIC:
		case SLURM_DIST_UNKNOWN:
			error_code = cr_dist(job, 1, cr_type); 
			break;
		case SLURM_DIST_PLANE:
			error_code = cr_plane_dist(job, mc_ptr->plane_size, cr_type); 
			break;
		case SLURM_DIST_ARBITRARY:
		default:
			error_code = compute_c_b_task_dist(job);
			if (error_code != SLURM_SUCCESS) {
				error(" Error in compute_c_b_task_dist");
			}
			break;
		}
	}
	if (error_code != SLURM_SUCCESS) {
		_xfree_select_cr_job(job);
		return error_code;
	}

	_append_to_job_list(job);
	last_cr_update_time = time(NULL);

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
	struct select_cr_job *job = NULL;
	ListIterator iterator;

	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);

	if (list_count(select_cr_job_list) == 0)
		return SLURM_SUCCESS;

	iterator = list_iterator_create(select_cr_job_list);
	while ((job = (struct select_cr_job *) list_next(iterator))) {
		if (job->job_id == job_ptr->job_id)
			break;
	}
	if (!job) {
		error("select_p_job_fini: could not find data for job %d",
			job_ptr->job_id);
		list_iterator_destroy(iterator);
		return SLURM_ERROR;
	}
	
	_rm_job_from_nodes(job, "select_p_job_fini", 1);

	slurm_mutex_lock(&cr_mutex);
	list_remove(iterator);
	slurm_mutex_unlock(&cr_mutex);
	_xfree_select_cr_job(job);
	list_iterator_destroy(iterator);

	debug3("cons_res: select_p_job_fini Job_id %u: list_count: %d",
		job_ptr->job_id, list_count(select_cr_job_list));

	_verify_select_job_list(job_ptr->job_id);
	last_cr_update_time = time(NULL);

	return SLURM_SUCCESS;
}

extern int select_p_job_suspend(struct job_record *job_ptr)
{
	ListIterator job_iterator;
	struct select_cr_job *job;
	int rc;
 
	xassert(job_ptr);
	xassert(select_cr_job_list);

	job_iterator = list_iterator_create(select_cr_job_list);
	if (job_iterator == NULL)
		fatal("list_iterator_create: %m");
	while ((job = (struct select_cr_job *) list_next(job_iterator))) {
		if (job->job_id == job_ptr->job_id)
			break;
	}
	list_iterator_destroy(job_iterator);

	if (!job)
		return ESLURM_INVALID_JOB_ID;

	rc = _rm_job_from_nodes(job, "select_p_job_suspend", 0);
	return SLURM_SUCCESS;
}

extern int select_p_job_resume(struct job_record *job_ptr)
{
	ListIterator job_iterator;
	struct select_cr_job *job;
	int rc;

	xassert(job_ptr);
	xassert(select_cr_job_list);

	job_iterator = list_iterator_create(select_cr_job_list);
	if (job_iterator == NULL)
		fatal("list_iterator_create: %m");

	while ((job = (struct select_cr_job *) list_next(job_iterator))) {
		if (job->job_id == job_ptr->job_id)
			break;
	}
	list_iterator_destroy(job_iterator);

	if (!job)
		return ESLURM_INVALID_JOB_ID;
	
	rc = _add_job_to_nodes(job, "select_p_job_resume", 0);
	return SLURM_SUCCESS;
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
	int rc = SLURM_SUCCESS, i;
	struct select_cr_job *job;
	ListIterator iterator;
	uint16_t *tmp_16 = (uint16_t *) data;

	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);

	switch (cr_info) {
	case SELECT_AVAIL_CPUS:
		job = NULL;
		iterator = list_iterator_create(select_cr_job_list);
		xassert(node_ptr);

		*tmp_16 = 0;
		while ((job = (struct select_cr_job *) list_next(iterator))) {
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
					*tmp_16 = job->alloc_cpus[i];
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
	int rc = SLURM_SUCCESS, i, j;
	struct node_cr_record *this_cr_node;
	struct part_cr_record *p_ptr;
	uint16_t *tmp_16;

	xassert(node_ptr);

	switch (dinfo) {
	case SELECT_ALLOC_CPUS: 
		tmp_16 = (uint16_t *) data;
		*tmp_16 = 0;
	        this_cr_node = find_cr_node_record (node_ptr->name);
		if (this_cr_node == NULL) {
		        error(" cons_res: could not find node %s",
			      node_ptr->name);
			rc = SLURM_ERROR;
			return rc;
		}
		/* determine the highest number of allocated cores from */
		/* all rows of all partitions */
		for (p_ptr = this_cr_node->parts; p_ptr; p_ptr = p_ptr->next) {
			i = 0;
			for (j = 0; j < p_ptr->num_rows; j++) {
				uint16_t tmp = 0;
				for (; i < this_cr_node->num_sockets; i++)
					tmp += p_ptr->alloc_cores[i] *
							node_ptr->threads;
				if (tmp > *tmp_16)
					*tmp_16 = tmp;
			}
		}
		break;
	default:
		error("select_g_get_select_nodeinfo info %d invalid", dinfo);
		rc = SLURM_ERROR;
		break;
	}
	return rc;
}

extern int select_p_update_nodeinfo(struct job_record *job_ptr)
{
	int rc = SLURM_SUCCESS;
	struct select_cr_job *job = NULL;
	ListIterator iterator;

	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);

	if ((job_ptr->job_state != JOB_RUNNING)
	&&  (job_ptr->job_state != JOB_SUSPENDED))
		return SLURM_SUCCESS;

	iterator = list_iterator_create(select_cr_job_list);
	while ((job = (struct select_cr_job *) list_next(iterator)) != NULL) {
		if (job->job_id == job_ptr->job_id)
			break;
	}
	list_iterator_destroy(iterator);
	
	if (!job)
		return SLURM_SUCCESS;
	
	rc = _add_job_to_nodes(job, "select_p_update_nodeinfo", 0);
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

extern int select_p_reconfigure(void)
{
	ListIterator job_iterator;
	struct select_cr_job *job;
	struct job_record *job_ptr;
	uint16_t addme;
	int rc, suspend;

	select_fast_schedule = slurm_get_fast_schedule();

	/* Refresh the select_node_ptr global array in case nodes
	 * have been added or removed. This procedure will clear all
	 * partition information and all allocated resource usage.
	 */
	rc = select_p_node_init(node_record_table_ptr, node_record_count);

	/* reload all of the allocated resource usage from job data */
	if (select_cr_job_list == NULL) {
	    	return SLURM_SUCCESS;
	}

	slurm_mutex_lock(&cr_mutex);
	job_iterator = list_iterator_create(select_cr_job_list);
	while ((job = (struct select_cr_job *) list_next(job_iterator))) {
		addme = suspend = 0;
		if ((job_ptr = find_job_record(job->job_id))) {
			if ((job_ptr->job_state != JOB_RUNNING) &&
			    (job_ptr->job_state != JOB_SUSPENDED))
				continue;
			if (job_ptr->job_state == JOB_SUSPENDED)
				suspend = 1;
		} else {
			/* stale job */
			list_remove(job_iterator);
			debug2("cons_res: select_p_reconfigure: removing "
				"nonexistent job %u", job->job_id);
			_xfree_select_cr_job(job);
		}
		if (job->state & CR_JOB_ALLOCATED_MEM) {
			job->state |= ~ CR_JOB_ALLOCATED_MEM;
			addme = 1;
		}
		if (job->state & CR_JOB_ALLOCATED_CPUS) {
			job->state |= ~ CR_JOB_ALLOCATED_CPUS;
			addme = 1;
		}
		if (addme)
			_add_job_to_nodes(job, "select_p_reconfigure", suspend);
		/* ignore any errors. partition and/or node config 
		 * may have changed while jobs remain running */
	}
	list_iterator_destroy(job_iterator);
	slurm_mutex_unlock(&cr_mutex);
	last_cr_update_time = time(NULL);

	return SLURM_SUCCESS;
}

extern struct multi_core_data * create_default_mc(void)
{
	struct multi_core_data *mc_ptr;
	mc_ptr = xmalloc(sizeof(struct multi_core_data));
	mc_ptr->min_sockets = 1;
	mc_ptr->max_sockets = 0xffff;
	mc_ptr->min_cores   = 1;
	mc_ptr->max_cores   = 0xffff;
	mc_ptr->min_threads = 1;
	mc_ptr->max_threads = 0xffff;
/*	mc_ptr is already initialized to zero */
/*	mc_ptr->ntasks_per_socket = 0; */
/*	mc_ptr->ntasks_per_core   = 0; */
/*	mc_ptr->plane_size        = 0; */
	return mc_ptr;
}
