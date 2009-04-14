/*****************************************************************************\
 *  dist_tasks - Assign task count to {socket,core,thread} or CPU
 *               resources
 *
 *  $Id: dist_tasks.c,v 1.3 2006/10/31 19:31:31 palermo Exp $
 ***************************************************************************** 
 *  Copyright (C) 2006 Hewlett-Packard Development Company, L.P.
 *  Written by Susanne M. Balle, <susanne.balle@hp.com>
 *  LLNL-CODE-402394.
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

#include "select_cons_res.h"
#include "dist_tasks.h"

#if (0)
#define CR_DEBUG 1
#endif

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
 * IN/OUT job_ptr - pointer to job being scheduled. The per-node
 *                  job->alloc_cpus array is computed here.
 *
 */
int compute_c_b_task_dist(struct select_cr_job *job)
{
	int i, j, rc = SLURM_SUCCESS;
	bool over_commit = false;
	bool over_subscribe = false;
	uint32_t taskid = 0, last_taskid, maxtasks = job->nprocs;

	if (job->job_ptr->details && job->job_ptr->details->overcommit)
		over_commit = true;

	for (j = 0; (taskid < maxtasks); j++) {	/* cycle counter */
		bool space_remaining = false;
		last_taskid = taskid;
		for (i = 0; ((i < job->nhosts) && (taskid < maxtasks)); i++) {
			if ((j < job->cpus[i]) || over_subscribe) {
				taskid++;
				if ((job->alloc_cpus[i] == 0) ||
				    (!over_commit))
					job->alloc_cpus[i]++;
				if ((j + 1) < job->cpus[i])
					space_remaining = true;
			}
		}
		if (!space_remaining)
			over_subscribe = true;
		if (last_taskid == taskid) {
			/* avoid infinite loop */
			error("compute_c_b_task_dist failure");
			rc = SLURM_ERROR;
			break;
		}
	}

#if (CR_DEBUG)	
	for (i = 0; i < job->nhosts; i++) {
		info("cons_res _c_b_task_dist %u host_index %d nprocs %u "
		     "maxtasks %u cpus %u alloc_cpus %u", 
		     job->job_id, i, job->nprocs, 
		     maxtasks, job->cpus[i], job->alloc_cpus[i]);
	}
#endif	

	return rc;
}

/* scan all rows looking for the best fit, and return the offset */
static int _find_offset(struct select_cr_job *job, const int job_index,
			uint16_t cores, uint16_t sockets, uint32_t maxcores,
			const select_type_plugin_info_t cr_type,
			struct node_cr_record *this_cr_node)
{
	struct part_cr_record *p_ptr;
	int i, j, index, offset, skip;
	uint16_t acores, asockets, freecpus, last_freecpus = 0;
	struct multi_core_data *mc_ptr;

	p_ptr = get_cr_part_ptr(this_cr_node, job->job_ptr->part_ptr);
	if (p_ptr == NULL)
		abort();
	mc_ptr = job->job_ptr->details->mc_ptr;

	index = -1;
	for (i = 0; i < p_ptr->num_rows; i++) {
		acores = 0;
		asockets = 0;
		skip = 0;
		offset = i * this_cr_node->sockets;
		for (j = 0; j < this_cr_node->sockets; j++) {
			if ((cores - p_ptr->alloc_cores[offset+j]) <
							mc_ptr->min_cores) {
				/* count the number of unusable sockets */
				skip++;
				acores += cores;
			} else { 
				acores += p_ptr->alloc_cores[offset+j];
			}
			if (p_ptr->alloc_cores[offset+j])
				asockets++;
		}
		/* make sure we have the required number of usable sockets */
		if (skip && ((sockets - skip) < mc_ptr->min_sockets))
			continue;
		/* CR_SOCKET needs UNALLOCATED sockets */
		if ((cr_type == CR_SOCKET) || (cr_type == CR_SOCKET_MEMORY)) {
			if (sockets - asockets < mc_ptr->min_sockets)
				continue;
		}

		freecpus = (cores * sockets) - acores;
		if (freecpus < maxcores)
			continue;

		if (index < 0) {
			index = i;
			last_freecpus = freecpus;
		}
		if (freecpus < last_freecpus) {
			index = i;
			last_freecpus = freecpus;
		}
	}
	if (index < 0) {
		/* This may happen if a node has fewer nodes than
		 * configured and FastSchedule=2 */
		error("job_assign_task: failure in computing offset");
		index = 0;
	}

	return index * this_cr_node->sockets;
}

/*  _job_assign_tasks: Assign tasks to hardware for block and cyclic
 *  distributions */
static int _job_assign_tasks(struct select_cr_job *job, 
			struct node_cr_record *this_cr_node,
			const int job_index, 
			const select_type_plugin_info_t cr_type,
			const int cyclic) 
{
	int i, j, rc = SLURM_SUCCESS;
	uint16_t cores, cpus, sockets, threads;
	uint16_t usable_cores, usable_sockets, usable_threads;
	uint16_t *avail_cores = NULL;
	uint32_t corecount, last_corecount;
	uint16_t asockets, offset, total;
	uint32_t maxcores, reqcores, maxtasks = job->alloc_cpus[job_index];
	struct part_cr_record *p_ptr;
	struct multi_core_data *mc_ptr;
	
	p_ptr = get_cr_part_ptr(this_cr_node, job->job_ptr->part_ptr);
	if (p_ptr == NULL)
		return SLURM_ERROR;

	if ((job->job_ptr == NULL) || (job->job_ptr->details == NULL)) {
		/* This should never happen */
		error("cons_res: job %u has no details", job->job_id);
		return SLURM_ERROR;
	}
	if (!job->job_ptr->details->mc_ptr)
		job->job_ptr->details->mc_ptr = create_default_mc();
	mc_ptr = job->job_ptr->details->mc_ptr;

	/* get hardware info for this node */	
	get_resources_this_node(&cpus,  &sockets, &cores, &threads, 
				this_cr_node, job->job_id);

	/* compute any job limits */	
	usable_sockets = MIN(mc_ptr->max_sockets, sockets);
	usable_cores   = MIN(mc_ptr->max_cores,   cores);
	usable_threads = MIN(mc_ptr->max_threads, threads);

	/* determine the number of required cores. When multiple threads
	 * are available, the maxtasks value may not reflect the requested
	 * core count, which is what we are seeking here. */
	if (job->job_ptr->details->overcommit) {
		maxcores = 1;
		reqcores = 1;
	} else {
		maxcores = maxtasks / usable_threads;
		while ((maxcores * usable_threads) < maxtasks)
			maxcores++;
		reqcores = mc_ptr->min_cores * mc_ptr->min_sockets;
		if (maxcores < reqcores)
			maxcores = reqcores;
	}

	offset = _find_offset(job, job_index, cores, sockets, maxcores, cr_type,
			      this_cr_node);
	job->node_offset[job_index] = offset;

	debug3("job_assign_task %u s_ min %u u %u c_ min %u u %u"
	       " t_ min %u u %u task %u core %u offset %u", 
	       job->job_id, mc_ptr->min_sockets, usable_sockets, 
	       mc_ptr->min_cores, usable_cores, mc_ptr->min_threads, 
	       usable_threads, maxtasks, maxcores, offset);

	avail_cores = xmalloc(sizeof(uint16_t) * sockets);
	/* initialized to zero by xmalloc */

	total = 0;
	asockets = 0;
	for (i = 0; i < sockets; i++) {
		if ((total >= maxcores) && (asockets >= mc_ptr->min_sockets)) {
			break;
		}
		if (this_cr_node->cores <= p_ptr->alloc_cores[offset+i]) {
			continue;
		}
		/* for CR_SOCKET, we only want to allocate empty sockets */
		if ((cr_type == CR_SOCKET || cr_type == CR_SOCKET_MEMORY) &&
		    (p_ptr->alloc_cores[offset+i] > 0))
			continue;
		avail_cores[i] = this_cr_node->cores - 
				 p_ptr->alloc_cores[offset+i];
		if (usable_cores <= avail_cores[i]) {
			avail_cores[i] = usable_cores;
		} else if (mc_ptr->min_cores > avail_cores[i]) {
			avail_cores[i] = 0;
		}
		if (avail_cores[i] > 0) {
			total += avail_cores[i];
			asockets++;
		}
	}
	
#if(CR_DEBUG)
    	for (i = 0; i < sockets; i+=2) {
		info("cons_res: assign_task: avail_cores[%d]=%u, [%d]=%u", i,
		     avail_cores[i], i+1, avail_cores[i+1]);
	}
#endif
	if (asockets == 0) {
		/* Should never get here but just in case */
		error("cons_res: %u Zero sockets satisfy"
		      " request -B %u:%u: Using alternative strategy",
		      job->job_id, mc_ptr->min_sockets, mc_ptr->min_cores);
		for (i = 0; i < sockets; i++) {
			if (this_cr_node->cores <= p_ptr->alloc_cores[offset+i])
				continue;
			avail_cores[i] = this_cr_node->cores - 
				p_ptr->alloc_cores[offset+i];
		}
	}
	
	if (asockets < mc_ptr->min_sockets) {
		error("cons_res: %u maxcores %u Cannot satisfy"
		      " request -B %u:%u: Using -B %u:%u",
		      job->job_id, maxcores, mc_ptr->min_sockets, 
		      mc_ptr->min_cores, asockets, mc_ptr->min_cores);
	}

	corecount = 0;
	if (cyclic) {
		/* distribute tasks cyclically across the sockets */
		for (i=1; corecount<maxcores; i++) {
			last_corecount = corecount;
			for (j=0; ((j<sockets) && (corecount<maxcores)); j++) {
				if (avail_cores[j] == 0)
					continue;
				if (i<=avail_cores[j]) {
					job->alloc_cores[job_index][j]++;
					corecount++;
				}
			}
			if (last_corecount == corecount) {
				/* Avoid possible infinite loop on error */
				error("_job_assign_tasks failure");
				rc = SLURM_ERROR;
				goto fini;
			}
		}
	} else {
		/* distribute tasks in blocks across the sockets */
		for (j=0; ((j<sockets) && (corecount<maxcores)); j++) {
			last_corecount = corecount;
			if (avail_cores[j] == 0)
				continue;
			for (i = 0; (i < avail_cores[j]) && 
				    (corecount<maxcores); i++) {
				job->alloc_cores[job_index][j]++;
				corecount++;
			}
			if (last_corecount == corecount) {
				/* Avoid possible infinite loop on error */
				error("_job_assign_tasks failure");
				rc = SLURM_ERROR;
				goto fini;
			}
		}
	}
 fini:	xfree(avail_cores);
	return rc;
}

static uint16_t _get_cpu_offset(struct select_cr_job *job, int index,
				struct node_cr_record *this_node)
{
	int i, set = 0;
	uint16_t cpus, sockets, cores, threads, besto = 0, offset = 0;
	struct part_cr_record *p_ptr;

	p_ptr = get_cr_part_ptr(this_node, job->job_ptr->part_ptr);
	if ((p_ptr == NULL) || (p_ptr->num_rows < 2))
		return offset;

	get_resources_this_node(&cpus, &sockets, &cores, &threads,
	        		this_node, job->job_id);
	/* scan all rows looking for the best row for job->alloc_cpus[index] */
	for (i = 0; i < p_ptr->num_rows; i++) {
		if ((cpus - p_ptr->alloc_cores[offset]) >=
						job->alloc_cpus[index]) {
			if (!set) {
				set = 1;
				besto = offset;
			}
			if (p_ptr->alloc_cores[offset] >
						p_ptr->alloc_cores[besto]) {
				besto = offset;
			}
		}
		offset += this_node->sockets;
	}
	return besto;
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
 * layout schema within slurmctld.
*/
extern int cr_dist(struct select_cr_job *job, int cyclic,
		   const select_type_plugin_info_t cr_type)
{
	int i, cr_cpu = 0, rc = SLURM_SUCCESS; 
	uint32_t taskcount = 0;
	int host_index;
	int job_index = -1;

	int error_code = compute_c_b_task_dist(job);
	if (error_code != SLURM_SUCCESS) {
		error(" Error in compute_c_b_task_dist");
		return error_code;
	}

	if ((cr_type == CR_CPU) || (cr_type == CR_MEMORY) ||
	    (cr_type == CR_CPU_MEMORY)) 
		cr_cpu = 1;

	for (host_index = 0; 
	     ((host_index < node_record_count) && (taskcount < job->nprocs));
	     host_index++) {
		struct node_cr_record *this_cr_node;

		if (bit_test(job->node_bitmap, host_index) == 0)
			continue;
		job_index++;
		
		if (select_node_ptr == NULL) {
			error("cons_res: select_node_ptr is NULL");
			return SLURM_ERROR;
		}
		this_cr_node = &select_node_ptr[host_index];
		
		if (job->cpus[job_index] == 0) {
			error("cons_res: %d no available cpus on node %s ",
			      job->job_id,
			      node_record_table_ptr[host_index].name);
			continue;
		}

		if (cr_cpu) {
			/* compute the offset */
			job->node_offset[job_index] =
				_get_cpu_offset(job, job_index, this_cr_node);
		} else {
			for (i = 0; i < job->num_sockets[job_index]; i++)
				job->alloc_cores[job_index][i] = 0;

			if (_job_assign_tasks(job, this_cr_node, job_index, 
					      cr_type, cyclic) != SLURM_SUCCESS)
				return SLURM_ERROR;
		}
#if(CR_DEBUG)
		info("cons_res _cr_dist %u host %d %s alloc_cpus %u", 
		     job->job_id, host_index, this_cr_node->node_ptr->name, 
		     job->alloc_cpus[job_index]);
		for(i=0; !cr_cpu && i<job->num_sockets[job_index];i+=2) {
			info("cons_res: _cr_dist: job %u " 
			     "alloc_cores[%d][%d]=%u, [%d][%d]=%u", 
			     job->job_id, 
			     job_index, i, job->alloc_cores[job_index][i], 
			     job_index, i+1, job->alloc_cores[job_index][i+1]);
		}
#endif
	}
	return rc;
}

/* User has specified the --exclusive flag on the srun command line
 * which means that the job should use only dedicated nodes.  In this
 * case we do not need to compute the number of tasks on each nodes
 * since it should be set to the number of cpus.
 */
extern int cr_exclusive_dist(struct select_cr_job *job,
		 	     const select_type_plugin_info_t cr_type)
{
	int i, j;
	int host_index = 0, get_cores = 0;

	if ((cr_type == CR_CORE)   || (cr_type == CR_CORE_MEMORY) ||
	    (cr_type == CR_SOCKET) || (cr_type == CR_SOCKET_MEMORY))
		get_cores = 1;

	if (select_fast_schedule) {
		struct config_record *config_ptr;
		for (i = 0; i < node_record_count; i++) {
			if (bit_test(job->node_bitmap, i) == 0)
				continue;
			config_ptr = node_record_table_ptr[i].config_ptr;
			job->alloc_cpus[host_index] = config_ptr->cpus;
			if (get_cores) {
				for (j=0; j<config_ptr->sockets; 
				     j++) {
					job->alloc_cores[host_index][j] = 
						config_ptr->cores;
				}
			}
			host_index++;
		}
	} else {
		for (i = 0; i < node_record_count; i++) {
			if (bit_test(job->node_bitmap, i) == 0)
				continue;
			job->alloc_cpus[host_index] = node_record_table_ptr[i].
						      cpus;
			if (get_cores) {
				for (j=0; j<node_record_table_ptr[i].sockets; 
				     j++) {
					job->alloc_cores[host_index][j] = 
						node_record_table_ptr[i].cores;
				}
			}
			host_index++;
		}
	}
	return SLURM_SUCCESS;
}

extern int cr_plane_dist(struct select_cr_job *job, 
			 const uint16_t plane_size,
			 const select_type_plugin_info_t cr_type)
{
	uint32_t maxtasks  = job->nprocs;
	uint32_t num_hosts = job->nhosts;
	int i, j, k, host_index, cr_cpu = 0;
	uint32_t taskcount = 0, last_taskcount;
	int job_index = -1;
	bool count_done = false;
	bool over_commit = false;

	debug3("cons_res _cr_plane_dist plane_size %u ", plane_size);
	debug3("cons_res _cr_plane_dist  maxtasks %u num_hosts %u",
	       maxtasks, num_hosts);

	if (plane_size <= 0) {
		error("Error in _cr_plane_dist");
		return SLURM_ERROR;
	}

	if (job->job_ptr->details && job->job_ptr->details->overcommit)
		over_commit = true;

	taskcount = 0;
	for (j=0; ((taskcount<maxtasks) && (!count_done)); j++) {
		last_taskcount = taskcount;
		for (i=0; 
		     (((i<num_hosts) && (taskcount<maxtasks)) && (!count_done));
		     i++) {
			for (k=0; ((k<plane_size) && (!count_done)); k++) {
				if (taskcount >= maxtasks) {
					count_done = true;
					break;
				}
				taskcount++;
				if ((job->alloc_cpus[i] == 0) ||
				    (!over_commit))
					job->alloc_cpus[i]++;
			}
		}
		if (last_taskcount == taskcount) {
			/* avoid possible infinite loop on error */
			error("cr_plane_dist failure");
			return SLURM_ERROR;
		}
	}

#if(CR_DEBUG)	
	for (i = 0; i < job->nhosts; i++) {
		info("cons_res _cr_plane_dist %u host_index %d alloc_cpus %u ", 
		     job->job_id, i, job->alloc_cpus[i]);
	}
#endif

	if ((cr_type == CR_CPU) || (cr_type == CR_MEMORY) ||
	    (cr_type == CR_CPU_MEMORY))
		cr_cpu = 1;

	taskcount = 0;
	for (host_index = 0; 
	     ((host_index < node_record_count) && (taskcount < job->nprocs));
	     host_index++) {
		struct node_cr_record *this_cr_node = NULL;

		if (bit_test(job->node_bitmap, host_index) == 0)
			continue;
		job_index++;

		if (select_node_ptr == NULL) {
			error("cons_res: select_node_ptr is NULL");
			return SLURM_ERROR;
		}
		this_cr_node = &select_node_ptr[host_index];
		
		if (job->cpus[job_index] == 0) {
			error("cons_res: no available cpus on node %s", 
			      node_record_table_ptr[host_index].name);
			continue;
		}

		if (cr_cpu) {
			/* compute the offset */
			job->node_offset[job_index] =
				_get_cpu_offset(job, job_index, this_cr_node);
		} else {
			for (j = 0; j < job->num_sockets[job_index]; j++)
				job->alloc_cores[job_index][j] = 0;

			if (_job_assign_tasks(job, this_cr_node, job_index, 
					      cr_type, 0) != SLURM_SUCCESS)
				return SLURM_ERROR;
		}
#if(CR_DEBUG)
		info("cons_res _cr_plane_dist %u host %d %s alloc_cpus %u", 
		     job->job_id, host_index, this_cr_node->node_ptr->name, 
		     job->alloc_cpus[job_index]);

		for (i = 0; !cr_cpu && i < this_cr_node->sockets; i++) {
			info("cons_res _cr_plane_dist %u host %d %s alloc_cores %u",
			     job->job_id, host_index,
			     this_cr_node->node_ptr->name,
			     job->alloc_cores[job_index][i]);
		}
#endif
		
	}

	return SLURM_SUCCESS;
}
