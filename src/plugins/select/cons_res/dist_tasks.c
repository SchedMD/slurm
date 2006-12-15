/*****************************************************************************\
 *  dist_tasks - Assign task count to {socket,core,thread} or CPU
 *               resources
 *
 *  $Id: dist_tasks.c,v 1.3 2006/10/31 19:31:31 palermo Exp $
 ***************************************************************************** 
 *  Copyright (C) 2006 Hewlett-Packard Development Company, L.P.
 *  Written by Susanne M. Balle, <susanne.balle@hp.com>
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
 * IN job_ptr - pointer to job being scheduled
 */
int compute_c_b_task_dist(struct select_cr_job *job, 	    
			  const select_type_plugin_info_t cr_type,
			  const uint16_t fast_schedule)
{
	int i, j, rc = SLURM_SUCCESS;
	uint16_t avail_cpus = 0, cpus, sockets, cores, threads;
	bool over_subscribe = false;
	uint32_t taskid = 0, last_taskid, maxtasks = job->nprocs;

	for (j = 0; (taskid < maxtasks); j++) {	/* cycle counter */
		bool space_remaining = false;
		last_taskid = taskid;
		for (i = 0; 
		     ((i < job->nhosts) && (taskid < maxtasks)); i++) {
			struct node_cr_record *this_node;
			this_node = find_cr_node_record (job->host[i]);
			if (this_node == NULL) {
				error(" cons_res: could not find node %s",
				      job->host[i]);
				return SLURM_ERROR;
			}

			switch(cr_type) {
			case CR_MEMORY:
				if (fast_schedule) {
					avail_cpus = this_node->node_ptr->config_ptr->cpus;
				} else {
					avail_cpus = this_node->node_ptr->cpus;
				}
			case CR_CPU:
			case CR_CPU_MEMORY:
				if (fast_schedule) {
					avail_cpus = this_node->node_ptr->config_ptr->cpus;
				} else {
					avail_cpus = this_node->node_ptr->cpus;
				}
				avail_cpus -= this_node->alloc_lps;
				break;
			case CR_SOCKET:
			case CR_SOCKET_MEMORY:
			{
				uint16_t alloc_sockets = 0;
				uint16_t alloc_lps     = 0;
				get_resources_this_node(&cpus, &sockets, 
							&cores, &threads, 
							this_node, &alloc_sockets, 
							&alloc_lps, &job->job_id);
				
				avail_cpus = slurm_get_avail_procs(job->max_sockets, 
								   job->max_cores, 
								   job->max_threads,
								   job->min_sockets,
								   job->min_cores, 
								   job->cpus_per_task, 
								   job->ntasks_per_node, 
								   job->ntasks_per_socket, 
								   job->ntasks_per_core, 
								   &cpus,
								   &sockets,
								   &cores,
								   &threads,
								   alloc_sockets,
								   this_node->alloc_cores,
								   alloc_lps,
								   cr_type,
								   job->job_id,
								   this_node->node_ptr->name);
				break;
			}
			case CR_CORE:
			case CR_CORE_MEMORY:
			{
				uint16_t alloc_sockets = 0;
				uint16_t alloc_lps     = 0;
				get_resources_this_node(&cpus, &sockets, 
							&cores, &threads, 
							this_node, &alloc_sockets,
							&alloc_lps, &job->job_id);
				
				avail_cpus = slurm_get_avail_procs(job->max_sockets, 
								   job->max_cores, 
								   job->max_threads, 
								   job->min_sockets,
								   job->min_cores,
								   job->cpus_per_task,
								   job->ntasks_per_node,
								   job->ntasks_per_socket,
								   job->ntasks_per_core, 
								   &cpus,
								   &sockets,
								   &cores,
								   &threads,
								   alloc_sockets,
								   this_node->alloc_cores,
								   alloc_lps,
								   cr_type,
								   job->job_id,
								   this_node->node_ptr->name);
				break;
			}
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
		if (last_taskid == taskid) {
			/* avoid infinite loop */
			fatal("compute_c_b_task_dist failure");
		}
	}

#if (CR_DEBUG)	
	for (i = 0; i < job->nhosts; i++) {
		info("cons_res _c_b_task_dist %u host %s nprocs %u maxtasks %u alloc_ lps %u", 
		     job->job_id, job->host[i], job->nprocs, 
		     maxtasks, job->alloc_lps[i]);
	}
#endif	

	return rc;
}

/*  _job_assign_tasks: Assign tasks to hardware for block and cyclic
 *  distributions */
void _job_assign_tasks(struct select_cr_job *job, 
		       struct node_cr_record *this_cr_node,
		       const uint16_t usable_threads, 
		       const uint16_t usable_cores, 
		       const uint16_t usable_sockets,
		       const int job_index, 
		       const uint32_t maxtasks,
		       const select_type_plugin_info_t cr_type) 
{
	int i, j;
	uint16_t nsockets = this_cr_node->node_ptr->sockets;
	uint16_t acores, avail_cores[nsockets];
	uint16_t asockets, avail_sockets[nsockets];
	uint32_t taskcount = 0, last_taskcount;
	uint16_t ncores = 0, total = 0;

	debug3("job_assign_task %u s_ m %u u %u c_ u %u min %u"
	       " t_ u %u min %u task %u ", 
	       job->job_id, job->min_sockets, usable_sockets, 
	       job->min_cores, usable_cores, job->min_threads, 
	       usable_threads, maxtasks);

	for (i=0; i < nsockets; i++) {
		avail_cores[i]   = 0;
		avail_sockets[i] = 0;
	}

	total = 0;
	asockets = 0;
	for (i=0; i<nsockets; i++) {
		if ((total >= maxtasks) && (asockets >= job->min_sockets)) {
			break;
		}
		if (this_cr_node->node_ptr->cores <=
		    this_cr_node->alloc_cores[i]) {
			continue;
		}
		acores = this_cr_node->node_ptr->cores - 
			this_cr_node->alloc_cores[i];
		if (usable_cores <= acores) {
			ncores = usable_cores;
		} else if (job->min_cores <= acores) {
			ncores = job->min_cores;
		} else {
			ncores = 0;
		}
		if (ncores > 0) {
			avail_cores[i]   = ncores;
			avail_sockets[i] = 1;
			total += ncores*usable_threads;
			asockets++;
		}
	}
	
	if (asockets == 0) {
		/* Should never get here but just in case */
		error("cons_res: %u Zero sockets satisfy"
		      " request -B %u:%u: Using alternative strategy",
		      job->job_id, job->min_sockets, job->min_cores);
		for (i=0; i < nsockets; i++) {
			if (this_cr_node->node_ptr->cores <=
			    this_cr_node->alloc_cores[i])
				continue;
			acores = this_cr_node->node_ptr->cores - 
				this_cr_node->alloc_cores[i];
			avail_cores[i]   = acores;
			avail_sockets[i] = 1;
		}
	}
	
	if (asockets < job->min_sockets) {
		error("cons_res: %u maxtasks %u Cannot satisfy"
		      " request -B %u:%u: Using -B %u:%u",
		      job->job_id, maxtasks, job->min_sockets, 
		      job->min_cores, asockets, job->min_cores);
	}

	for (i=0; taskcount<maxtasks; i++) {
		last_taskcount = taskcount;
		for (j=0; ((j<nsockets) && (taskcount<maxtasks)); j++) {
			asockets = avail_sockets[j];
			if (asockets == 0)
				continue;
			if (avail_cores[j] == 0)
				continue;
			if (i == 0)
				job->alloc_sockets[job_index]++;
			if (i<avail_cores[j])
				job->alloc_cores[job_index][j]++;
			taskcount++;
		}
		if (last_taskcount == taskcount) {
			/* Avoid possible infinite loop on error */
			fatal("_job_assign_tasks failure");
		}
	}
}

/*  _job_assign_tasks: Assign tasks to hardware for block and cyclic
 *  distributions */
void _job_assign_tasks_plane(struct select_cr_job *job, 
			     struct node_cr_record *this_cr_node,
			     const uint16_t usable_threads, 
			     const uint16_t usable_cores, 
			     const uint16_t usable_sockets,
			     const int job_index, 
			     const uint32_t maxtasks,
			     const uint16_t plane_size,
			     const select_type_plugin_info_t cr_type) 
{
	int s, l, m, i, j;
	uint16_t nsockets = this_cr_node->node_ptr->sockets;
	uint16_t avail_cores[nsockets];
	uint16_t avail_sockets[nsockets];
	uint32_t taskcount, last_taskcount;
	uint16_t total, ncores, acores, isocket;
	uint16_t core_index, thread_index, ucores;
	uint16_t max_plane_size = 0;
	int last_socket_index = -1;

	debug3("job_assign_task %u _plane_ s_ m %u u %u c_ u %u"
	       " min %u t_ u %u min %u task %u", 
	       job->job_id, job->min_sockets, usable_sockets, 
	       job->min_cores, usable_cores, job->min_threads, 
	       usable_threads, maxtasks);
	
	for (i=0; i < nsockets; i++) {
		avail_cores[i]   = 0;
		avail_sockets[i] = 0;
	}
	
	total = 0;
	isocket = 0;
	for (i=0; i<nsockets; i++) {
		if ((total >= maxtasks) && (isocket >= job->min_sockets)) {
			break;
		}
		/* sockets with the required available core count */
		if (this_cr_node->node_ptr->cores <=
		    this_cr_node->alloc_cores[i]) {
			continue;
		}
		acores = this_cr_node->node_ptr->cores - 
			this_cr_node->alloc_cores[i];
		if (plane_size <= acores) {
			ncores = plane_size;
		} else if (usable_cores <= acores) {
			ncores = usable_cores;
		} else if (job->min_cores <= acores) {
			ncores = job->min_cores;
		} else {
			ncores = 0;
		}
		if (ncores > 0) {
			avail_cores[i]   = ncores;
			avail_sockets[i] = 1;
			total += ncores*usable_threads;
			isocket++;
		}
	}
	
	if (isocket == 0) {
		/* Should never get here but just in case */
		error("cons_res: %u Zero sockets satisfy request"
		      " -B %u:%u: Using alternative strategy",
		      job->job_id, job->min_sockets, job->min_cores);
		for (i=0; i < nsockets; i++) {
			if (this_cr_node->node_ptr->cores <=
			    this_cr_node->alloc_cores[i])
				continue;
			acores = this_cr_node->node_ptr->cores - 
				this_cr_node->alloc_cores[i];
			avail_cores[i]   = acores;
			avail_sockets[i] = 1;
		}
	}
	
	if (isocket < job->min_sockets)
		error("cons_res: %u maxtasks %d Cannot satisfy"
		      " request -B %u:%u: Using -B %u:%u",
		      job->job_id, maxtasks, job->min_sockets, 
		      job->min_cores, isocket, job->min_cores);
	
	last_socket_index = -1;
	taskcount = 0;
	for (j=0; taskcount<maxtasks; j++) {
		last_taskcount = taskcount;
		for (s=0; ((s<nsockets) && (taskcount<maxtasks)); 
		     s++) {
			if (avail_sockets[s] == 0)
				continue;
			ucores = avail_cores[s];
			max_plane_size = 
				(plane_size > ucores) 
				? plane_size : ucores;
			for (m=0; ((m<max_plane_size) 
				   && (taskcount<maxtasks)); 
			     m++) {
				core_index = m%ucores;
				if(m > ucores) 
					continue;
				for(l=0; ((l<usable_threads) 
					  && (taskcount<maxtasks)); 
				    l++) {
					thread_index =
						l%usable_threads;
					if(thread_index > usable_threads)
						continue;
					if (last_socket_index != s) {
						job->alloc_sockets [job_index]++;
						last_socket_index = s;
					}
					if ((l == 0) && (m < ucores)) {
						if (job->alloc_cores[job_index][s] 
						    < this_cr_node->node_ptr->cores) {
							job->alloc_cores[job_index][s]++;
						}
					}
					taskcount++;
				}
			}
		}
		if (last_taskcount == taskcount) {
			/* avoid possible infinite loop on error */
			fatal("job_assign_task failure");
		}
	}
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
int cr_dist(struct select_cr_job *job, int cyclic,
	    const select_type_plugin_info_t cr_type,
	    const uint16_t fast_schedule)
{
#if(CR_DEBUG)
    	int i;
#endif
	int j, rc = SLURM_SUCCESS; 
	uint32_t taskcount = 0;
	uint32_t maxtasks  = job->nprocs;
	int host_index;
	uint16_t usable_cpus = 0;
	uint16_t usable_sockets = 0, usable_cores = 0, usable_threads = 0;
	int last_socket_index = -1;
	int last_core_index = -1;
	int job_index = -1;

	int error_code = compute_c_b_task_dist(job, cr_type, fast_schedule);
	if (error_code != SLURM_SUCCESS) {
		error(" Error in compute_c_b_task_dist");
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
		uint16_t alloc_sockets = 0;
		uint16_t alloc_lps     = 0;
		uint16_t avail_cpus    = 0;
		if (bit_test(job->node_bitmap, host_index) == 0)
			continue;
		job_index++;
		
		this_cr_node = find_cr_node_record(
			node_record_table_ptr[host_index].name);
		if (this_cr_node == NULL) {
			error(" cons_res: could not find node %s", 
			      node_record_table_ptr[host_index].name);
			return SLURM_ERROR;
		}

		get_resources_this_node(&usable_cpus,  &usable_sockets, 
					&usable_cores, &usable_threads, 
					this_cr_node,  &alloc_sockets, 
					&alloc_lps, &job->job_id);
		
		avail_cpus = slurm_get_avail_procs(job->max_sockets,
						   job->max_cores,
						   job->max_threads,
						   job->min_sockets,
						   job->min_cores,
						   job->cpus_per_task,
						   job->ntasks_per_node,
						   job->ntasks_per_socket,
						   job->ntasks_per_core,
						   &usable_cpus,
						   &usable_sockets,
						   &usable_cores,
						   &usable_threads,
						   alloc_sockets,
						   this_cr_node->alloc_cores,
						   alloc_lps,
						   cr_type,
						   job->job_id,
						   this_cr_node->node_ptr->name);
		
#if(CR_DEBUG)
		info("cons_res: _cr_dist %u avail_s %u _c %u _t %u"
		     " alloc_s %d lps %u",
		     job->job_id, usable_sockets, usable_cores, 
		     usable_threads,
		     alloc_sockets, alloc_lps);
		if ((cr_type == CR_CORE) || (cr_type == CR_CORE_MEMORY))
			for(i=0; i<usable_sockets;i++)
				info("cons_res: _cr_dist alloc_cores %d = %u", 
				     i, this_cr_node->alloc_cores[i]);
#endif		
		
		if (avail_cpus == 0) {
			error(" cons_res: %d no available cpus on node %s "
			      " s %u c %u t %u", 
			      job->job_id, node_record_table_ptr[host_index].name,
			      usable_sockets, usable_cores, usable_threads);
		}
		
		maxtasks = job->alloc_lps[job_index];
		job->alloc_sockets[job_index] = 0;
		if ((cr_type == CR_CORE) || (cr_type == CR_CORE_MEMORY)) {
			for (j = 0; 
			     j < node_record_table_ptr[host_index].cores; 
			     j++)
				job->alloc_cores[job_index][j] = 0;
		}		
		
		if ((cr_type == CR_CORE) || (cr_type == CR_CORE_MEMORY)) {
			_job_assign_tasks(job, this_cr_node, 
					  usable_threads, usable_cores, 
					  usable_sockets, job_index, 
					  maxtasks, cr_type);
		} else if (cyclic == 0) { /* block lllp distribution */
			/* CR _SOCKET or CR_SOCKET_MEMORY */
			int s, c, t;
			last_socket_index = -1;	
			taskcount = 0;
			for (s=0; 
			     s < usable_sockets; 
			     s++) {
				last_core_index = -1;	
				if (maxtasks <= taskcount)
					continue;
				for (c=0; 
				     c < usable_cores; 
				     c++) {
					if (maxtasks <= taskcount)
						continue;
					for (t=0; 
					     t < usable_threads; t++) {
						if (maxtasks <= taskcount) 
							continue;
						if (last_socket_index != s) {
							job->alloc_sockets[job_index]++;
							last_socket_index = s;
						}
						taskcount++;
					}
				}
			}
		} else if (cyclic == 1) { /* cyclic lllp distribution */
			/* CR_SOCKET or CR_SOCKET_MEMORY */
			int s, c, t;
			int last_socket_index = 0;
			taskcount = 0;
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
						if (last_socket_index == 0) {
							job->alloc_sockets[job_index]++;
							if(s == (usable_sockets-1))
								last_socket_index = 1;
						}
						taskcount++;
					}
				}
			}
		}
		
#if(CR_DEBUG)
		info("cons_res _cr_dist %u host %d %s alloc_ "
		     "sockets %u lps %u", 
		     job->job_id, host_index,  this_cr_node->node_ptr->name, 
		     job->alloc_sockets[job_index], job->alloc_lps[job_index]);
		if ((cr_type == CR_CORE) || (cr_type == CR_CORE_MEMORY))
			for(i=0; i<usable_sockets;i++)
				info("cons_res _cr_dist: %u alloc_cores[%d][%d] = %u", 
				     job->job_id, i, job_index, 
				     job->alloc_cores[job_index][i]);
#endif
	}
	return rc;
}

/* User has specified the --exclusive flag on the srun command line
 * which means that the job should use only dedicated nodes.  In this
 * case we do not need to compute the number of tasks on each nodes
 * since it should be set to the number of cpus.
 */
int cr_exclusive_dist(struct select_cr_job *job,
		      const select_type_plugin_info_t cr_type)
{
	int i, j;
	int host_index = 0;

	for (i = 0; i < node_record_count; i++) {
		if (bit_test(job->node_bitmap, i) == 0)
			continue;
		job->alloc_lps[host_index] = node_record_table_ptr[i].cpus;
		job->alloc_sockets[host_index] = 
			node_record_table_ptr[i].sockets; 
		if ((cr_type == CR_CORE) || (cr_type == CR_CORE_MEMORY)) {
			for (j = 0; j < node_record_table_ptr[i].sockets; j++)
				job->alloc_cores[host_index][j] = 
					node_record_table_ptr[i].cores; 
		}
		host_index++;
	}
	return SLURM_SUCCESS;
}

int cr_plane_dist(struct select_cr_job *job, 
		  const uint16_t plane_size,
		  const select_type_plugin_info_t cr_type)
{
	uint32_t maxtasks    = job->nprocs;
	uint16_t num_hosts   = job->nhosts;
	int i, j, k, s, m, l, host_index;
	uint16_t usable_cpus, usable_sockets, usable_cores, usable_threads;
	uint32_t taskcount = 0, last_taskcount;
	int last_socket_index = -1;
	int job_index = -1;
	bool count_done = false;

	debug3("cons_res _cr_plane_dist plane_size %u ", plane_size);
	debug3("cons_res _cr_plane_dist  maxtasks %u num_hosts %u",
	       maxtasks, num_hosts);

	if (plane_size <= 0) {
		error("Error in _cr_plane_dist");
		return SLURM_ERROR;
	}
	
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
				job->alloc_lps[i]++;
			}
		}
		if (last_taskcount == taskcount) {
			/* avoid possible infinite loop on error */
			fatal("cr_plane_dist failure");
		}
	}

#if(CR_DEBUG)	
	for (i = 0; i < job->nhosts; i++) {
		info("cons_res _cr_plane_dist %u host %s alloc_ lps %u ", 
		     job->job_id, job->host[i],  job->alloc_lps[i]);
	}
#endif

	taskcount = 0;
	for (host_index = 0; 
	     ((host_index < node_record_count) && (taskcount < job->nprocs));
	     host_index++) {
		struct node_cr_record *this_cr_node = NULL;
		uint16_t alloc_sockets = 0;
		uint16_t alloc_lps     = 0;
		uint16_t avail_cpus    = 0;
		if (bit_test(job->node_bitmap, host_index) == 0)
			continue;
		job_index++;
		
		this_cr_node = find_cr_node_record(
			node_record_table_ptr[host_index].name);
		if (this_cr_node == NULL) {
			error("cons_res: could not find node %s", 
			      node_record_table_ptr[host_index].name);
			return SLURM_ERROR;
		}
		
		get_resources_this_node(&usable_cpus, &usable_sockets, 
					&usable_cores, &usable_threads, 
					this_cr_node,  &alloc_sockets, 
					&alloc_lps, &job->job_id);
		
		avail_cpus = slurm_get_avail_procs(job->max_sockets,
						   job->max_cores,
						   job->max_threads,
						   job->min_sockets,
						   job->min_cores,
						   job->cpus_per_task,
						   job->ntasks_per_node,
						   job->ntasks_per_socket,
						   job->ntasks_per_core,
						   &usable_cpus,
						   &usable_sockets,
						   &usable_cores,
						   &usable_threads,
						   alloc_sockets,
						   this_cr_node->alloc_cores,
						   alloc_lps,
						   cr_type,
						   job->job_id,
						   this_cr_node->node_ptr->name);

		if (avail_cpus == 0) {
			error(" cons_res: no available cpus on node %s", 
			      node_record_table_ptr[host_index].name);
		}

		job->alloc_sockets[job_index] = 0;
		if ((cr_type == CR_CORE) || (cr_type == CR_CORE_MEMORY)) {
			for (j = 0; 
			     j < node_record_table_ptr[host_index].cores; 
			     j++)
				job->alloc_cores[job_index][j] = 0;
		}	
		maxtasks = job->alloc_lps[job_index];

		if ((cr_type == CR_CORE) || (cr_type == CR_CORE_MEMORY)) {
			_job_assign_tasks_plane(job, this_cr_node, 
						usable_threads, 
						usable_cores, usable_sockets, 
						job_index, maxtasks,
						plane_size, cr_type);
		} else {
			/* CR _SOCKET or CR_SOCKET_MEMORY */
			int core_index;
			int thread_index;
			int max_plane_size;
			last_socket_index = -1;
			taskcount = 0;
			for (j=0; taskcount<maxtasks; j++) {
				last_taskcount = taskcount;
				for (s=0; ((s<usable_sockets) && (taskcount<maxtasks)); 
				     s++) {
					max_plane_size = 
						(plane_size > usable_cores) 
						? plane_size : usable_cores;
					for (m=0; ((m<max_plane_size) && 
					     (taskcount<maxtasks)); m++) {
						core_index = m % usable_cores;
						if(m > usable_cores) 
							continue;
						for(l=0; ((l<usable_threads) && 
						    (taskcount<maxtasks)); l++) {
							thread_index =
								l % usable_threads;
							if(thread_index > usable_threads)
								continue;
							if (last_socket_index != s) {
								job->alloc_sockets[job_index]++;
								last_socket_index = s;
							}
						}
					}
					taskcount++;
				}
				if (last_taskcount == taskcount) {
					/* avoid possible infinite loop on error */
					fatal("cr_plane_dist failure");
				}
			}
		}

#if(0)
		if ((cr_type == CR_CORE) || (cr_type == CR_CORE_MEMORY)) {
			job->alloc_lps[job_index] = 0;
			for (i = 0; i < job->alloc_sockets[job_index]; i++)
				job->alloc_lps[job_index] += 
					job->alloc_cores[job_index][i];
		}
#endif

#if(CR_DEBUG)
		info("cons_res _cr_plane_dist %u host %d %s alloc_ "
		     "s %u lps %u", 
		     job->job_id, host_index,  this_cr_node->node_ptr->name, 
		     job->alloc_sockets[job_index], job->alloc_lps[job_index]);
		int i = 0;
		if ((cr_type == CR_CORE) || (cr_type == CR_CORE_MEMORY)) {
			for (i = 0; i < this_cr_node->node_ptr->sockets; i++)
				info("cons_res _cr_plane_dist %u host %d "
				     "%s alloc_cores %u",
				     job->job_id, host_index, 
				     this_cr_node->node_ptr->name, 
				     job->alloc_cores[job_index][i]);
		}
#endif
		
	}

	return SLURM_SUCCESS;
}
