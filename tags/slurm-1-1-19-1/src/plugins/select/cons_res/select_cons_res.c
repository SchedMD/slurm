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

#include <fcntl.h>
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

#define SELECT_CR_DEBUG 0

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
const uint32_t pstate_version = 1;	/* version control on saved state */

/* node_cr_record keeps track of the resources within a node which 
 * have been reserved by already scheduled jobs. 
 */
struct node_cr_record {
	struct node_record *node_ptr;	/* ptr to the node that own these resources */
	char *name;		/* reference copy of node_ptr name */
	uint32_t used_cpus;	/* cpu count reserved by already scheduled jobs */
	struct node_cr_record *node_next;/* next entry with same hash index */
};

#define CR_JOB_STATE_SUSPENDED 1

struct select_cr_job {
	uint32_t job_id;	/* job ID, default set by SLURM        */
	uint16_t state;		/* job state information               */
	int32_t nprocs;		/* --nprocs=n,      -n n               */
	int32_t nhosts;		/* number of hosts allocated to job    */
	char **host;		/* hostname vector                     */
	int32_t *cpus;		/* number of processors on each host   */
	int32_t *ntask;		/* number of tasks to run on each host */
	bitstr_t *node_bitmap;	/* bitmap of nodes allocated to job    */
};

/* Array of node_cr_record. One entry for each node in the cluster */
static struct node_cr_record *select_node_ptr = NULL;
static int select_node_cnt = 0;
static struct node_cr_record **cr_node_hash_table = NULL;

/* Restored node_cr_records - used by select_p_state_restore/node_init */
static struct node_cr_record *prev_select_node_ptr = NULL;
static int prev_select_node_cnt = 0;

static uint16_t select_fast_schedule;

List select_cr_job_list;	/* List of select_cr_job(s) that are still active */

#if SELECT_CR_DEBUG
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

#if SELECT_CR_DEBUG
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
			        debug3(" cons_res _find_cr_node_record: hash %s",  name);
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
 * since it should be set to the number of cpus.
 */
static void _cr_exclusive_dist(struct select_cr_job *job)
{
	int i;

	for (i = 0; (i < job->nhosts); i++)
		job->ntask[i] = job->cpus[i];
}

/* xfree an array of node_cr_record */
static void _xfree_select_nodes(struct node_cr_record *ptr, int select_node_cnt)
{
        xfree(ptr);
}

/* xfree a select_cr_job job */
static void _xfree_select_cr_job(struct select_cr_job *job)
{
	if (job == NULL)
		return;

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

	if (select_cr_job_list == NULL) {
		return;
	}

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
	int rc = SLURM_SUCCESS, i, nodes;
	struct select_cr_job *job = NULL;
	int job_id;
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

			this_node->used_cpus -= job->ntask[i];
			if (this_node->used_cpus < 0) {
			        error(" used_cpus < 0 %d on %s",
				        this_node->used_cpus,
				        this_node->node_ptr->name);
				this_node->used_cpus = 0;
				rc = SLURM_ERROR;  
				goto out;
			}
		}
	     out:
		list_remove(iterator);
		_xfree_select_cr_job(job);
		break;
	}
	list_iterator_destroy(iterator);

	debug3(" cons_res: _clear_select_jobinfo Job_id %u: "
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

	verbose("%s loaded ", plugin_name);
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
	int32_t nhosts = job->nhosts;
	if (nhosts < 0) {
		nhosts = 0;
	}

	pack32(job->job_id, buffer);
	pack16(job->state, buffer);
	pack32((uint32_t)job->nprocs, buffer);
	pack32((uint32_t)job->nhosts, buffer);
	packstr_array(job->host, nhosts, buffer);
	pack32_array((uint32_t*)job->cpus, nhosts, buffer);
	pack32_array((uint32_t*)job->ntask, nhosts, buffer);
	pack_bit_fmt(job->node_bitmap, buffer);
	pack16((uint16_t) _bitstr_bits(job->node_bitmap), buffer);

	return 0;
}

static int _cr_unpack_job(struct select_cr_job *job, Buf buffer)
{
    	uint16_t len16;
    	uint32_t len32;
	int32_t nhosts = 0;
	char *bit_fmt = NULL;
	uint16_t bit_cnt; 

	safe_unpack32(&job->job_id, buffer);
	safe_unpack16(&job->state, buffer);
	safe_unpack32((uint32_t*)&job->nprocs, buffer);
	safe_unpack32((uint32_t*)&job->nhosts, buffer);
	nhosts = job->nhosts;
	if (nhosts < 0) {
		nhosts = 0;
	}
	safe_unpackstr_array(&job->host, &len16, buffer);
	safe_unpack32_array((uint32_t**)&job->cpus, &len32, buffer);
	safe_unpack32_array((uint32_t**)&job->ntask, &len32, buffer);
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
	char *file_name;

	info("cons_res: select_p_state_save");

	/*** create the state file ***/
        file_name = xstrdup(dir_name);
        xstrcat(file_name, "/cons_res_state");
        (void) unlink(file_name);
        state_fd = creat (file_name, 0600);
        if (state_fd < 0) {
                error ("Can't save state, error creating file %s",
                        file_name);
                error_code = SLURM_ERROR;
		return error_code;
	}

	buffer = init_buf(1024);

	/*** record the plugin type ***/
	packstr((char*)plugin_type, buffer);
	pack32(plugin_version, buffer);
	pack32(pstate_version, buffer);

	/*** pack the select_cr_job array ***/
	if (select_cr_job_list) {
		job_iterator = list_iterator_create(select_cr_job_list);
		while ((job = (struct select_cr_job *) list_next(job_iterator))) {
			pack32(job->job_id, buffer);
			_cr_pack_job(job, buffer);
		}
		list_iterator_destroy(job_iterator);
	}
	pack32((uint32_t)(-1), buffer);		/* mark end of jobs */

	/*** pack the node_cr_record array ***/
	pack32((uint32_t)select_node_cnt, buffer);
	for (i = 0; i < select_node_cnt; i++) {
		/*** don't save select_node_ptr[i].node_ptr ***/
		packstr((char*)select_node_ptr[i].node_ptr->name, buffer);
		pack32(select_node_ptr[i].used_cpus, buffer);
	}

	/*** close the state file ***/
	_cr_write_state_buffer(state_fd, buffer);
	close (state_fd);

        xfree(file_name);

        if (buffer)
                free_buf(buffer);

	return error_code;
}

extern int select_p_state_restore(char *dir_name)
{
	int error_code = SLURM_SUCCESS;
	int state_fd, i;
	char *file_name;
	struct select_cr_job *job;
	Buf buffer = NULL;
    	uint16_t len16;
	char *data = NULL;
	int data_size = 0;
	char *restore_plugin_type = NULL;
    	uint32_t restore_plugin_version = 0;
    	uint32_t restore_pstate_version = 0;
	int job_id = -1;

	info("cons_res: select_p_state_restore");

        file_name = xstrdup(dir_name);
        xstrcat(file_name, "/cons_res_state");
        state_fd = open (file_name, O_RDONLY);
        if (state_fd < 0) {
                error ("Can't restore state, error opening file %s",
                        file_name);
                error ("Starting cons_res with clean slate");
		return SLURM_SUCCESS;
	}

	error_code = _cr_read_state_buffer(state_fd, &data, &data_size);

	if (error_code != SLURM_SUCCESS) {
                error ("Can't restore state, error reading file %s",
                        file_name);
                error ("Starting cons_res with clean slate");
		xfree(data);
		return SLURM_SUCCESS;
	}

	buffer = create_buf (data, data_size);
	data = NULL;    /* now in buffer, don't xfree() */

	/*** retrieve the plugin type ***/
	safe_unpackstr_xmalloc(&restore_plugin_type, &len16, buffer);
	safe_unpack32(&restore_plugin_version, buffer);
	safe_unpack32(&restore_pstate_version, buffer);

	if ((strcmp(restore_plugin_type, plugin_type) != 0) ||
	    (restore_plugin_version != plugin_version) ||
	    (restore_pstate_version != pstate_version)) { 
                error ("Can't restore state, state version mismtach: "
			"saw %s/%d/%d, expected %s/%d/%d",
			restore_plugin_type,
			restore_plugin_version,
			restore_pstate_version,
			plugin_type,
			plugin_version,
			pstate_version);
                error ("Starting cons_res with clean slate");
		xfree(restore_plugin_type);
		if (buffer)
			free_buf(buffer);
		return SLURM_SUCCESS;
	}

	/*** unpack the select_cr_job array ***/
	_clear_job_list();
	if (select_cr_job_list) {
		list_destroy(select_cr_job_list);
		select_cr_job_list = NULL;
	}
	select_cr_job_list = list_create(NULL);

	safe_unpack32((uint32_t*)&job_id, buffer);
	while (job_id >= 0) {	/* unpack until end of job marker (-1) */
		job = xmalloc(sizeof(struct select_cr_job));
		_cr_unpack_job(job, buffer);
		list_append(select_cr_job_list, job);
		safe_unpack32((uint32_t*)&job_id, buffer);   /* next marker */
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
		prev_select_node_ptr[i].node_ptr = NULL;
		safe_unpackstr_xmalloc(&(prev_select_node_ptr[i].name), &len16, buffer);
		safe_unpack32(&prev_select_node_ptr[i].used_cpus, buffer);
	}

	/*** cleanup after restore ***/
        if (buffer)
                free_buf(buffer);
        xfree(restore_plugin_type);

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

	select_node_cnt = node_cnt;
	select_node_ptr = xmalloc(sizeof(struct node_cr_record) *
							select_node_cnt);

	for (i = 0; i < select_node_cnt; i++) {
		select_node_ptr[i].node_ptr = &node_ptr[i];
		select_node_ptr[i].name     = node_ptr[i].name;
		select_node_ptr[i].used_cpus = 0;

		/* Restore any previous node data */
		if (prev_select_node_ptr && (i < prev_select_node_cnt) &&
			(strcmp(prev_select_node_ptr[i].name,
				select_node_ptr[i].name) == 0)) {
			info("recovered cons_res node data for %s",
					    select_node_ptr[i].name);
		    	
			select_node_ptr[i].used_cpus
				= prev_select_node_ptr[i].used_cpus;
		}

	}

	/* Release any previous node data */
	_xfree_select_nodes(prev_select_node_ptr, prev_select_node_cnt);
	prev_select_node_ptr = NULL;
	prev_select_node_cnt = 0;

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
 * NOTE: bitmap must be a superset of the job's required at the time that 
 *	select_p_job_test is called
 */
extern int select_p_job_test(struct job_record *job_ptr, bitstr_t * bitmap,
			uint32_t min_nodes, uint32_t max_nodes, 
			uint32_t req_nodes, bool test_only)
{
	int i, index, error_code = EINVAL, rc, sufficient;
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

	xassert(bitmap);

	debug3(" cons_res plug-in: Job_id %u min %d max nodes %d "
		"test_only %d", job_ptr->job_id, min_nodes, 
		max_nodes, (int) test_only);

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

	if (req_nodes > min_nodes)
		rem_nodes = req_nodes;
	else
		rem_nodes = min_nodes;
	for (index = 0; index < select_node_cnt; index++) {
		if (bit_test(bitmap, index)) {
			int allocated_cpus;
			if (consec_nodes[consec_index] == 0)
				consec_start[consec_index] = index;
			allocated_cpus = 0;
			if (!test_only) {
				rc = select_g_get_select_nodeinfo
					(select_node_ptr[index].node_ptr,
					SELECT_CR_USED_CPUS, 
					&allocated_cpus);
				if (rc != SLURM_SUCCESS)
					goto cleanup;
			}
			if (select_fast_schedule)
				/* don't bother checking each node */
				i = select_node_ptr[index].node_ptr->
				    config_ptr->cpus - allocated_cpus;
			else
				i = select_node_ptr[index].node_ptr->cpus -
				    allocated_cpus;
			if (job_ptr->details->req_node_bitmap
			&&  bit_test(job_ptr->details->req_node_bitmap,
				     index)
			&&  (max_nodes > 0)) {
				if (consec_req[consec_index] == -1) {
					/* first required node in set */
					consec_req[consec_index] = index;
				}
				rem_cpus -= i;
				rem_nodes--;
				max_nodes--;
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
				int allocated_cpus;
				if ((max_nodes <= 0)
				||  ((rem_nodes <= 0) && (rem_cpus <= 0)))
					break;
				if (bit_test(bitmap, i))
					continue;
				bit_set(bitmap, i);
				rem_nodes--;
				max_nodes--;
				allocated_cpus = 0;
				if (!test_only) {
					rc = select_g_get_select_nodeinfo
					    (select_node_ptr[i].node_ptr,
					     SELECT_CR_USED_CPUS,
					     &allocated_cpus);
					if (rc != SLURM_SUCCESS)
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
				int allocated_cpus, avail_cpus;
				if ((max_nodes <= 0)
				||  ((rem_nodes <= 0) && (rem_cpus <= 0)))
					break;
				if (bit_test(bitmap, i)) 
					continue;

				if (!test_only) {
					rc = select_g_get_select_nodeinfo
					    (select_node_ptr[i].node_ptr,
					     SELECT_CR_USED_CPUS,
					     &allocated_cpus);
					if (rc != SLURM_SUCCESS)
						goto cleanup;
				} else {
					allocated_cpus = 0;
				}

				if (select_fast_schedule) {
					avail_cpus =
					    select_node_ptr[i].node_ptr->
					    config_ptr->cpus -
					    allocated_cpus;
				} else {
					avail_cpus =
					    select_node_ptr[i].node_ptr->
					    cpus - allocated_cpus;
				}
				if (avail_cpus <= 0)
					continue;
				rem_cpus -= avail_cpus;
				bit_set(bitmap, i);
				rem_nodes--;
				max_nodes--;
			}
		} else {
			for (i = consec_start[best_fit_location];
			     i <= consec_end[best_fit_location]; i++) {
				int allocated_cpus, avail_cpus;
				if ((max_nodes <= 0)
				|| ((rem_nodes <= 0) && (rem_cpus <= 0)))
					break;
				if (bit_test(bitmap, i))
					continue;

				if (!test_only) {
					rc = select_g_get_select_nodeinfo
					    (select_node_ptr[i].node_ptr,
					     SELECT_CR_USED_CPUS,
					     &allocated_cpus);
					if (rc != SLURM_SUCCESS)
						goto cleanup;
				} else {
					allocated_cpus = 0;
				}

				if (select_fast_schedule) {
					avail_cpus =
					    select_node_ptr[i].node_ptr->
					    config_ptr->cpus -
					    allocated_cpus;
				} else {
					avail_cpus =
					    select_node_ptr[i].node_ptr->
					    cpus - allocated_cpus;
				}
				if (avail_cpus <= 0)
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
		job->nprocs = MAX(job_ptr->num_procs,
			job_nodecnt);

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

#if SELECT_CR_DEBUG
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
			cnt += MIN(job->cpus[i], job->ntask[i]);
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
		fatal(" error for %u in select/cons_res: "
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
			this_node_ptr->used_cpus -= job->ntask[i];
			if (this_node_ptr->used_cpus < 0) {
			        error(" cons_res: used_cpus < 0 %d on %s",
				        this_node_ptr->used_cpus,
				        this_node_ptr->node_ptr->name);
				this_node_ptr->used_cpus = 0;
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
			this_node->used_cpus += job->ntask[i];
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
				      enum select_data_info info,
				      void *data)
{
	int rc = SLURM_SUCCESS, i;

	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);

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
			xassert(node_ptr);
			xassert(node_ptr->magic == NODE_MAGIC);

			while ((job =
				(struct select_cr_job *)
				list_next(iterator)) != NULL) {
				if (job->job_id != job_ptr->job_id)
					continue;
				for (i = 0; i < job->nhosts; i++) { 
					if (strcmp
					    (node_ptr->name,
					     job->host[i]) == 0) {
#if SELECT_CR_DEBUG
						verbose
						    (" cons_res: get_extra_jobinfo job_id %u %s tasks %d ",
						     job->job_id,
						     job->host[i],
						     job->ntask[i]);
#endif
						*tmp_32 = job->ntask[i];
						goto cleanup;
					}
				}
				error(" cons_res could not find %s", 
				        node_ptr->name); 
				rc = SLURM_ERROR;
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
	int rc = SLURM_SUCCESS;
	struct node_cr_record *this_cr_node;

	xassert(node_ptr);
	xassert(node_ptr->magic == NODE_MAGIC);

	switch (info) {
	case SELECT_CR_USED_CPUS:
	        this_cr_node = _find_cr_node_record (node_ptr->name);
		if (this_cr_node == NULL) {
		        error(" cons_res: could not find node %s",
			      node_ptr->name);
			rc = SLURM_ERROR;
			return rc;
		}
		uint32_t *tmp_32 = (uint32_t *) data;
		*tmp_32 = this_cr_node->used_cpus;
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
	int rc = SLURM_SUCCESS, i, job_id, nodes;
	struct select_cr_job *job = NULL;
	ListIterator iterator;

	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);
	job_id = job_ptr->job_id;

	switch (info) {
	case SELECT_CR_USED_CPUS:
		iterator = list_iterator_create(select_cr_job_list);
		while ((job = (struct select_cr_job *)
				list_next(iterator)) != NULL) {
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
 			        this_node->used_cpus += job->ntask[i];
			}
			break;
		}
	     cleanup:
		list_iterator_destroy(iterator);
		break;
	default:
		error("select_g_update_nodeinfo info %d invalid", info);
		rc = SLURM_ERROR;
		break;
	}
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

#undef __SELECT_CR_DEBUG
