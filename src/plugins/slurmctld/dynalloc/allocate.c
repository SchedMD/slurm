/*****************************************************************************\
 *  allocate.c  - dynamic resource allocation
 *****************************************************************************
 *  Copyright (C) 2012-2013 Los Alamos National Security, LLC.
 *  Written by Jimmy Cao <Jimmy.Cao@emc.com>, Ralph Castain <rhc@open-mpi.org>
 *  All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <signal.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/log.h"
#include "src/common/bitstring.h"
#include "src/common/node_conf.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/node_select.h"

#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/node_scheduler.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/state_save.h"
#include "src/slurmctld/port_mgr.h"

#include "allocate.h"
#include "info.h"
#include "constants.h"
#include "job_ports_list.h"


static int _get_nodelist_optional(uint16_t request_node_num,
				  const char *node_range_list,
				  char *final_req_node_list);

static int _get_nodelist_mandatory(uint16_t request_node_num,
				   const char *node_range_list,
				   char *final_req_node_list);

static int _get_tasks_per_node(
			const resource_allocation_response_msg_t *alloc,
		  	const job_desc_msg_t *desc, char *tasks_per_node);

static char *_uint16_array_to_str_xmalloc(int array_len,
								const uint16_t *array);

static int _setup_job_desc_msg(uint32_t np, uint32_t request_node_num,
			       char *node_range_list, const char *flag,
			       time_t timeout, const char *cpu_bind,
			       uint32_t mem_per_cpu, job_desc_msg_t *job_desc_msg);

/**
 *	select n nodes from the given node_range_list.
 *
 *	optional means trying best to allocate node from
 *	node_range_list, allocation should include all nodes
 *	in the given list that are currently available. If
 *	that isn't enough to meet the request_node_num,
 * 	then take any other nodes that are available to
 * 	fill out the requested number.
 *
 *	IN:
 *		request_node_num: requested node number
 *		node_range_list: specified node range to select from
 *	OUT Parameter:
 *		final_req_node_list
 *	RET OUT
 *		-1 if requested node number is larger than available
 *		0  successful, final_req_node_list is returned
 */
static int _get_nodelist_optional(uint16_t request_node_num,
				  const char *node_range_list,
				  char *final_req_node_list)
{
	hostlist_t avail_hl_system = NULL;  //available hostlist in slurm
	hostlist_t avail_hl_pool = NULL;    //available hostlist in the given node pool
	hostlist_t hostlist = NULL;
	char *avail_pool_range = NULL;
	int avail_pool_num;
	int extra_needed_num;
	char *subset = NULL;
	char *hostname = NULL;
	char *tmp = NULL;
	int i;

	/* get all available hostlist in SLURM system */
	avail_hl_system = get_available_host_list_system_m();

	if (request_node_num > slurm_hostlist_count(avail_hl_system)){
		slurm_hostlist_destroy(avail_hl_system);
		return SLURM_FAILURE;
	}

	avail_hl_pool = choose_available_from_node_list_m(node_range_list);
	avail_pool_range = slurm_hostlist_ranged_string_malloc(avail_hl_pool);
	avail_pool_num = slurm_hostlist_count(avail_hl_pool);

	if (request_node_num <= avail_pool_num) {
		subset = get_hostlist_subset_m(avail_pool_range,request_node_num);
		strcpy(final_req_node_list, subset);
		free(subset);
	} else { /* avail_pool_num < reqeust_node_num <= avail_node_num_system */
		hostlist = slurm_hostlist_create(avail_pool_range);
		extra_needed_num = request_node_num - avail_pool_num;

		for (i = 0; i < extra_needed_num; ) {
			hostname = slurm_hostlist_shift(avail_hl_system);
			if (slurm_hostlist_find(hostlist, hostname) == -1) {
				slurm_hostlist_push_host(hostlist, hostname);
				i++;
			}
			free(hostname);
		}

		tmp = slurm_hostlist_ranged_string_xmalloc(hostlist);
		strcpy(final_req_node_list, tmp);
		xfree(tmp);
		slurm_hostlist_destroy(hostlist);
	}

	free(avail_pool_range);
	slurm_hostlist_destroy(avail_hl_system);
	slurm_hostlist_destroy(avail_hl_pool);

	return SLURM_SUCCESS;
}

/**
 *	select n nodes from the given node_range_list
 *
 *	mandatory means all nodes must be allocated
 *	from node_range_list
 *
 *	IN:
 *		request_node_num: requested node number
 *		node_range_list: specified node range to select from
 *	OUT Parameter:
 *		final_req_node_list
 *	RET OUT
 *		-1 if requested node number is larger than available
 *		0  successful, final_req_node_list is returned
 */
static int _get_nodelist_mandatory(uint16_t request_node_num,
				   const char *node_range_list,
				   char *final_req_node_list)
{
	hostlist_t avail_hl = NULL;
	char *avail_node_range = NULL;
	char *subset = NULL;
	int rc;

	/* select n (request_node_num) available nodes from node_range_list */
	avail_hl = choose_available_from_node_list_m(node_range_list);
	avail_node_range = slurm_hostlist_ranged_string_malloc(avail_hl);

	if (request_node_num <= slurm_hostlist_count(avail_hl)) {
		subset = get_hostlist_subset_m(avail_node_range, request_node_num);
		strcpy(final_req_node_list, subset);

		free(subset);
		rc = SLURM_SUCCESS;
	} else {
		rc = SLURM_FAILURE;
	}

	free(avail_node_range);
	slurm_hostlist_destroy(avail_hl);
	return rc;
}

/*
 * Note: the return should be xfree(str)
 */
static char* _uint16_array_to_str_xmalloc(int array_len,
								const uint16_t *array)
{
	int i;
	int previous = 0;
	char *sep = ",";  /* seperator */
	char *str = xstrdup("");

	if (NULL == array)
		return str;

	for (i = 0; i < array_len; i++) {
		if ((i+1 < array_len)
		    && (array[i] == array[i+1])) {
				previous++;
				continue;
		}

		if (i == array_len-1) /* last time through loop */
			sep = "";
		if (0 < previous) {
			xstrfmtcat(str, "%u(x%u)%s",
				   array[i], previous+1, sep);
		} else {
			xstrfmtcat(str, "%u%s", array[i], sep);
		}
		previous = 0;
	}

	return str;
}

/**
 *	get tasks_per_nodes
 *
 *	IN:
 *		alloc: resource allocation response
 *		desc: job resource requirement
 *	OUT Parameter:
 *		tasks_per_node
 *	RET OUT
 *		-1 if failed
 *		0  successful, tasks_per_node is returned
 */
static int _get_tasks_per_node(
			const resource_allocation_response_msg_t *alloc,
		  	const job_desc_msg_t *desc, char *tasks_per_node)
{
	uint32_t num_tasks = desc->num_tasks;
	slurm_step_layout_t *step_layout = NULL;
	uint32_t node_cnt = alloc->node_cnt;
	char *tmp = NULL;
	int i;

	/* If no tasks were given we will figure it out here
	 * by totalling up the cpus and then dividing by the
	 * number of cpus per task */
	if (NO_VAL == num_tasks) {
		num_tasks = 0;
		for (i = 0; i < alloc->num_cpu_groups; i++) {
			num_tasks += alloc->cpu_count_reps[i]
				* alloc->cpus_per_node[i];
		}
		if ((int)desc->cpus_per_task > 1
		   && desc->cpus_per_task != (uint16_t)NO_VAL)
			num_tasks /= desc->cpus_per_task;
	}

	if (!(step_layout = slurm_step_layout_create(alloc->node_list,
							alloc->cpus_per_node,
							alloc->cpu_count_reps,
							node_cnt,
							num_tasks,
							desc->cpus_per_task,
							desc->task_dist,
							desc->plane_size)))
		return SLURM_FAILURE;

	tmp = _uint16_array_to_str_xmalloc(step_layout->node_cnt, step_layout->tasks);

	slurm_step_layout_destroy(step_layout);

	if (NULL != tmp)
		strcpy(tasks_per_node, tmp);

	xfree(tmp);
	return SLURM_SUCCESS;
}

/**
 *	after initing, setup job_desc_msg_t with specific requirements
 *
 *	IN:
 *		np: number of process to run
 *		request_node_num: the amount of requested node
 *		node_range_list: requested node pool
 *		flag: optional or mandatory
 *		timeout:
 *		cpu_bind: e.g., cores, sockets, threads
 *		mem_per_cpu: memory size per CPU (MB)
 *	OUT Parameter:
 *		job_desc_msg
 *	RET OUT
 *		-1 if failed
 *		0  successful, job_desc_msg is returned
 */
static int _setup_job_desc_msg(uint32_t np, uint32_t request_node_num,
			       char *node_range_list, const char *flag,
			       time_t timeout, const char *cpu_bind,
			       uint32_t mem_per_cpu, job_desc_msg_t *job_desc_msg)
{
	char final_req_node_list[SIZE] = "";
	int rc;
	hostlist_t hostlist = NULL;

	job_desc_msg->user_id = getuid();
	job_desc_msg->group_id = getgid();
	job_desc_msg->contiguous = 0;

	/* set np */
	if (0 != np) {
		job_desc_msg->num_tasks = np;
		job_desc_msg->min_cpus = np;
	}

	if (0 != request_node_num) {  /* N != 0 */
		if (0 != strlen(node_range_list)) {
			/* N != 0 && node_list != "", select nodes according to flag */
			if (0 == strcmp(flag, "mandatory")) {
				rc = _get_nodelist_mandatory(request_node_num,
						node_range_list, final_req_node_list);

				if (SLURM_SUCCESS == rc) {
					if (0 != strlen(final_req_node_list))
						job_desc_msg->req_nodes = final_req_node_list;
					else
						job_desc_msg->min_nodes = request_node_num;
				} else {
					error ("can not meet mandatory requirement");
					return SLURM_FAILURE;
				}
			} else { /* flag == "optional" */
				rc = _get_nodelist_optional(request_node_num,
									node_range_list, final_req_node_list);
				if (SLURM_SUCCESS == rc) {
					if (0 != strlen(final_req_node_list))
						job_desc_msg->req_nodes = final_req_node_list;
					else
						job_desc_msg->min_nodes = request_node_num;
				} else {
					job_desc_msg->min_nodes = request_node_num;
				}
			}
		} else {
			/* N != 0 && node_list == "" */
			job_desc_msg->min_nodes = request_node_num;
		}
	} else { /* N == 0 */
		if (0 != strlen(node_range_list)) {
			/* N == 0 && node_list != "" */
			if (0 == strcmp(flag, "optional")) {
				hostlist = slurm_hostlist_create(node_range_list);
				request_node_num = slurm_hostlist_count(hostlist);
				rc = _get_nodelist_optional(request_node_num,
									node_range_list, final_req_node_list);
				if (SLURM_SUCCESS == rc) {
					if (0 != strlen(final_req_node_list))
						job_desc_msg->req_nodes = final_req_node_list;
					else
						job_desc_msg->min_nodes = request_node_num;
				} else {
					job_desc_msg->min_nodes = request_node_num;
				}

				slurm_hostlist_destroy(hostlist);
			} else {  /* flag == "mandatory" */
				job_desc_msg->req_nodes = node_range_list;
			}
		}
		/* if N == 0 && node_list == "", do nothing */
	}

	/* for cgroup */
	if (mem_per_cpu > 0)
		job_desc_msg->pn_min_memory =  mem_per_cpu | MEM_PER_CPU;

	if (NULL != cpu_bind || 0 != strlen(cpu_bind)) {
		if(0 == strcmp(cpu_bind, "cores"))
			job_desc_msg->cpu_bind_type = CPU_BIND_TO_CORES;
		else if (0 == strcmp(cpu_bind, "sockets"))
			job_desc_msg->cpu_bind_type = CPU_BIND_TO_SOCKETS;
		else if (0 == strcmp(cpu_bind, "threads"))
			job_desc_msg->cpu_bind_type = CPU_BIND_TO_THREADS;
	}
	return SLURM_SUCCESS;
}


/**
 *	select n nodes from the given node_range_list through rpc
 *
 *  if (flag == mandatory), all requested nodes must be allocated
 *  from node_list; else if (flag == optional), try best to allocate
 *  node from node_list, and the allocation should include all
 *  nodes in the given list that are currently available. If that
 *  isn't enough to meet the node_num_request, then take any other
 *  nodes that are available to fill out the requested number.
 *
 *	IN:
 *		np: number of process to run
 *		request_node_num: requested node number
 *		node_range_list: specified node range to select from
 *		flag: optional or mandatory
 *		timeout: timeout
 *		cpu_bind：e.g., cores, threads, sockets
 *		mem_per_cpu: memory size per CPU (MB)
 *	OUT Parameter:
 *		jobid: slurm jobid
 *		reponse_node_list:
 *		tasks_per_node: like 4(x2) 3,2
 *	RET OUT:
 *		-1 if requested node number is larger than available or timeout
 *		0  successful
 */
int allocate_node_rpc(uint32_t np, uint32_t request_node_num,
		      char *node_range_list, const char *flag,
		      time_t timeout, const char *cpu_bind,
		      uint32_t mem_per_cpu, uint32_t resv_port_cnt,
		      uint32_t *slurm_jobid, char *reponse_node_list,
		      char *tasks_per_node, char *resv_ports)
{
	job_desc_msg_t job_desc_msg;
	resource_allocation_response_msg_t *job_alloc_resp_msg = NULL;
	struct job_record *job_ptr = NULL;
	struct step_record step;
	uid_t uid = getuid();
	int rc, i;

	slurm_init_job_desc_msg (&job_desc_msg);
	rc = _setup_job_desc_msg(np, request_node_num, node_range_list, flag,
					 timeout, cpu_bind, mem_per_cpu, &job_desc_msg);
	if (rc)
		return SLURM_FAILURE;

	job_alloc_resp_msg = slurm_allocate_resources_blocking(&job_desc_msg,
							       timeout, NULL);
	if (!job_alloc_resp_msg) {
		error("allocate failure, timeout or request too many nodes");
		return SLURM_FAILURE;
	}

	/* OUT: slurm_jobid, reponse_node_list, tasks_per_node */
	*slurm_jobid = job_alloc_resp_msg->job_id;
	strcpy(reponse_node_list, job_alloc_resp_msg->node_list);
	_get_tasks_per_node(job_alloc_resp_msg, &job_desc_msg, tasks_per_node);

	info("allocate [ node_list = %s ] to [ job_id = %u ]",
	     job_alloc_resp_msg->node_list, job_alloc_resp_msg->job_id);

	/* free the allocated resource msg */
	slurm_free_resource_allocation_response_msg(job_alloc_resp_msg);

	job_ptr = find_job_record(job_alloc_resp_msg->job_id);
	/**************************\
	 * 		resv port 		  *
	\**************************/
	if (0 == resv_port_cnt)
		resv_port_cnt = 1;
	step.resv_port_cnt = resv_port_cnt;
	step.job_ptr = job_ptr;
	step.step_node_bitmap = job_ptr->node_bitmap;
	rc = resv_port_alloc(&step);
	if (SLURM_SUCCESS != rc) {
		cancel_job(job_ptr->job_id, uid);
		xfree(step.resv_ports);
		xfree(step.resv_port_array);
		return SLURM_FAILURE;
	}
	strcpy(resv_ports, step.resv_ports);
	for (i = 0; i < step.resv_port_cnt; i++) {
		info("reserved ports %s for job %u : resv_port_array[%d]=%u",
				step.resv_ports, step.job_ptr->job_id,
				i, step.resv_port_array[i]);
	}

	/* keep slurm_jobid and resv_port_array in a List
	 * for future release port */
	append_job_ports_item(job_ptr->job_id, step.resv_port_cnt,
			step.resv_ports, step.resv_port_array);

	xfree(step.resv_ports);
	xfree(step.resv_port_array);

#if 0
	//kill the job, release the resource, just for test
	if (slurm_kill_job(job_alloc_resp_msg->job_id, SIGKILL, 0)) {
		 error ("ERROR: kill job %d\n", slurm_get_errno());
		 return SLURM_FAILURE;
	}
#endif

	return SLURM_SUCCESS;
}

/**
 *	select n nodes from the given node_range_list directly through
 *	"job_allocate" in slurmctld/job_mgr.c
 *
 *  if (flag == mandatory), all requested nodes must be allocated
 *  from node_list; else if (flag == optional), try best to allocate
 *  node from node_list, and the allocation should include all
 *  nodes in the given list that are currently available. If that
 *  isn't enough to meet the node_num_request, then take any other
 *  nodes that are available to fill out the requested number.
 *
 *	IN:
 *		np: number of process to run
 *		request_node_num: requested node number
 *		node_range_list: specified node range to select from
 *		flag: optional or mandatory
 *		timeout: timeout
 *		cpu_bind: cpu bind type, e.g., cores, socket
 *		mem_per_cpu: memory size per cpu (MB)
 *	OUT Parameter:
 *		slurm_jobid: slurm jobid
 *		reponse_node_list:
 *		tasks_per_node: like 4(x2) 3,2
 *	RET OUT:
 *		-1 if requested node number is larger than available or timeout
 *		0  successful, final_req_node_list is returned
 */
int allocate_node(uint32_t np, uint32_t request_node_num,
		  char *node_range_list, const char *flag,
		  time_t timeout, const char *cpu_bind,
		  uint32_t mem_per_cpu, uint32_t resv_port_cnt,
		  uint32_t *slurm_jobid, char *reponse_node_list,
		  char *tasks_per_node, char *resv_ports)
{
	int rc, error_code, i;
	char *err_msg = NULL;
	resource_allocation_response_msg_t alloc_msg;
	job_desc_msg_t job_desc_msg;
	struct job_record *job_ptr = NULL;
	bool job_waiting = false;
	uid_t uid = getuid();
	struct step_record step;

	slurm_init_job_desc_msg (&job_desc_msg);
	rc = _setup_job_desc_msg(np, request_node_num, node_range_list, flag,
				 timeout, cpu_bind, mem_per_cpu, &job_desc_msg);

	if (rc)
		return SLURM_FAILURE;

	/* Locks: Read config, write job, write node, read partition */
	slurmctld_lock_t job_write_lock = {
			READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK };

	job_desc_msg.immediate = 0;
	rc = validate_job_create_req(&job_desc_msg, job_desc_msg.user_id,
				     &err_msg);
	if (rc) {
		error("invalid job request: %s", err_msg);
		xfree(err_msg);
		return SLURM_FAILURE;
	}

	lock_slurmctld(job_write_lock);
	error_code = job_allocate(&job_desc_msg, job_desc_msg.immediate,
				  false, //will run
				  NULL, // will_run_response_msg_t
				  true, //allocate
				  job_desc_msg.user_id, &job_ptr, NULL);
	unlock_slurmctld(job_write_lock);

	/* cleanup */
	xfree(job_desc_msg.partition);

	if ((error_code == ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE) ||
		(error_code == ESLURM_RESERVATION_NOT_USABLE) ||
		(error_code == ESLURM_QOS_THRES) ||
		(error_code == ESLURM_NODE_NOT_AVAIL) ||
		(error_code == ESLURM_JOB_HELD))
		job_waiting = true;

	if ((SLURM_SUCCESS == error_code) ||
	    ((0 == job_desc_msg.immediate) && job_waiting)) {
		xassert(job_ptr);

		/* note: allocated node list is in 'job_ptr->job_id' */
		/* not 'job_ptr->alloc_node' */

		if (0 < job_ptr->job_id && NULL == job_ptr->nodes) {
			/* job is pending, so cancel the job */
			cancel_job(job_ptr->job_id, uid);
			return SLURM_FAILURE;
		} else {  /* allocate successful */
			strcpy(reponse_node_list, job_ptr->nodes);
			*slurm_jobid = job_ptr->job_id;
			info("allocate [ allocated_node_list=%s ] to [ slurm_jobid=%u ]",
			     job_ptr->nodes, job_ptr->job_id);

			/* transform job_ptr to alloc_msg for further use */
			if (job_ptr->job_resrcs &&
			    job_ptr->job_resrcs->cpu_array_cnt) {
				alloc_msg.num_cpu_groups =
						job_ptr->job_resrcs->cpu_array_cnt;
				i = sizeof(uint32_t) * alloc_msg.num_cpu_groups;
				alloc_msg.cpu_count_reps = xmalloc(i);
				memcpy(alloc_msg.cpu_count_reps,
				       job_ptr->job_resrcs->cpu_array_reps, i);
				i = sizeof(uint16_t) * alloc_msg.num_cpu_groups;
				alloc_msg.cpus_per_node  = xmalloc(i);
				memcpy(alloc_msg.cpus_per_node,
				       job_ptr->job_resrcs->cpu_array_value, i);
			} else {
				alloc_msg.num_cpu_groups = 0;
				alloc_msg.cpu_count_reps = NULL;
				alloc_msg.cpus_per_node  = NULL;
			}
			alloc_msg.error_code     = error_code;
			alloc_msg.job_id         = job_ptr->job_id;
			alloc_msg.node_cnt       = job_ptr->node_cnt;
			alloc_msg.node_list      = xstrdup(job_ptr->nodes);
			alloc_msg.alias_list     = xstrdup(job_ptr->alias_list);
			alloc_msg.select_jobinfo =
					select_g_select_jobinfo_copy(job_ptr->select_jobinfo);
			if (job_ptr->details) {
					alloc_msg.pn_min_memory = job_ptr->details->
								  pn_min_memory;
			} else {
					alloc_msg.pn_min_memory = 0;
			}

			/* to get tasks_per_node */
			_get_tasks_per_node(&alloc_msg, &job_desc_msg,
					    tasks_per_node);

			/* cleanup */
			xfree(alloc_msg.cpu_count_reps);
			xfree(alloc_msg.cpus_per_node);
			xfree(alloc_msg.node_list);
			xfree(alloc_msg.alias_list);

			select_g_select_jobinfo_free(alloc_msg.select_jobinfo);
			schedule_job_save();	/* has own locks */
			schedule_node_save();	/* has own locks */

			/**************************\
			 * 		resv port 		  *
			\**************************/
			if (0 == resv_port_cnt)
				resv_port_cnt = 1;
			step.resv_port_cnt = resv_port_cnt;
			step.job_ptr = job_ptr;
			step.step_node_bitmap = job_ptr->node_bitmap;
			rc = resv_port_alloc(&step);
			if (SLURM_SUCCESS != rc) {
				cancel_job(job_ptr->job_id, uid);
				xfree(step.resv_ports);
				xfree(step.resv_port_array);
				return SLURM_FAILURE;
			}
			strcpy(resv_ports, step.resv_ports);
			for (i = 0; i < step.resv_port_cnt; i++) {
				info("reserved ports %s for job %u : resv_port_array[%d]=%u",
						step.resv_ports, step.job_ptr->job_id,
						i, step.resv_port_array[i]);
			}

			/* keep slurm_jobid and resv_port_array in a List */
			append_job_ports_item(job_ptr->job_id, step.resv_port_cnt,
					step.resv_ports, step.resv_port_array);

			xfree(step.resv_ports);
			xfree(step.resv_port_array);

#if 0
			/* only for test */
			cancel_job(job_ptr->job_id, uid);
#endif
			return SLURM_SUCCESS;
		}
	} else {
		return SLURM_FAILURE;
	}
}

/**
 *	cancel a job
 *
 *	IN:
 *		job_id: slurm jobid
 *		uid: user id
 *	OUT Parameter:
 *	RET OUT:
 *		-1 failed
 *		0  successful
 */
int cancel_job(uint32_t job_id, uid_t uid)
{
	int rc;
	/* Locks: Read config, write job, write node */
	slurmctld_lock_t job_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK };

	lock_slurmctld(job_write_lock);
	rc = job_signal(job_id, SIGKILL, 0, uid, false);
	unlock_slurmctld(job_write_lock);

	if (rc) { /* cancel failure */
		info("Signal %u JobId=%u by UID=%u: %s",
				SIGKILL, job_id, uid, slurm_strerror(rc));
		return SLURM_FAILURE;
	} else { /* cancel successful */
		info("sched: Cancel of JobId=%u by UID=%u", job_id, uid);
		slurmctld_diag_stats.jobs_canceled++;

		/* Below function provides its own locking */
		schedule_job_save();
		return SLURM_SUCCESS;
	}
}
