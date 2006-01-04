/*****************************************************************************\
 *  srun_comm.c - srun communications
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-217948.
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
#endif

#include <string.h>

#include "src/common/node_select.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/slurmctld.h"

#define SRUN_LAUNCH_MSG 0

/* Launch the srun request. Note that retry is always zero since 
 * we don't want to clog the system up with messages destined for 
 * defunct srun processes 
 */ 
static void _srun_agent_launch(slurm_addr *addr, char *host,
		slurm_msg_type_t type, void *msg_args)
{
	agent_arg_t *agent_args = xmalloc(sizeof(agent_arg_t));

	agent_args->node_count = 1;
	agent_args->retry      = 0;
	agent_args->slurm_addr = addr;
	agent_args->node_names = xmalloc(MAX_NAME_LEN);
	strncpy(agent_args->node_names, host, MAX_NAME_LEN);
	agent_args->msg_type   = type;
	agent_args->msg_args   = msg_args;
	agent_queue_request(agent_args);
}

/*
 * srun_allocate - notify srun of a resource allocation
 * IN job_id - id of the job allocated resource
 */
extern void srun_allocate (uint32_t job_id)
{
	struct job_record *job_ptr = find_job_record (job_id);

	xassert(job_ptr);
	if (job_ptr->port && job_ptr->host && job_ptr->host[0]) {
		slurm_addr * addr;
		resource_allocation_response_msg_t *msg_arg;

		addr = xmalloc(sizeof(struct sockaddr_in));
		slurm_set_addr(addr, job_ptr->port, job_ptr->host);
		msg_arg = xmalloc(sizeof(resource_allocation_response_msg_t));
		msg_arg->job_id 	= job_ptr->job_id;
		msg_arg->node_list	= xstrdup(job_ptr->nodes);
		msg_arg->num_cpu_groups	= job_ptr->num_cpu_groups;
		msg_arg->cpus_per_node  = xmalloc(sizeof(uint32_t) *
				job_ptr->num_cpu_groups);
		memcpy(msg_arg->cpus_per_node, job_ptr->cpus_per_node,
				(sizeof(uint32_t) * job_ptr->num_cpu_groups));
		msg_arg->cpu_count_reps  = xmalloc(sizeof(uint32_t) *
				job_ptr->num_cpu_groups);
		memcpy(msg_arg->cpu_count_reps, job_ptr->cpu_count_reps,
				(sizeof(uint32_t) * job_ptr->num_cpu_groups));
		msg_arg->node_cnt	= job_ptr->node_cnt;
		msg_arg->node_addr      = xmalloc(sizeof (slurm_addr) *
				job_ptr->node_cnt);
		memcpy(msg_arg->node_addr, job_ptr->node_addr,
				(sizeof(slurm_addr) * job_ptr->node_cnt));
		msg_arg->select_jobinfo = select_g_copy_jobinfo(
				job_ptr->select_jobinfo);
		msg_arg->error_code	= SLURM_SUCCESS;
		_srun_agent_launch(addr, job_ptr->host, 
				RESPONSE_RESOURCE_ALLOCATION, msg_arg);
	}
}

/*
 * srun_node_fail - notify srun of a node's failure
 * IN job_id    - id of job to notify
 * IN node_name - name of failed node
 */
extern void srun_node_fail (uint32_t job_id, char *node_name)
{
	struct node_record *node_ptr;
	struct job_record *job_ptr = find_job_record (job_id);
	int bit_position;
	slurm_addr * addr;
	srun_node_fail_msg_t *msg_arg;
	ListIterator step_iterator;
	struct step_record *step_ptr;

	xassert(job_ptr);
	xassert(node_name);
	if (job_ptr->job_state != JOB_RUNNING)
		return;
	if ((node_ptr = find_node_record(node_name)) == NULL)
		return;
	bit_position = node_ptr - node_record_table_ptr;

	if (job_ptr->port && job_ptr->host && job_ptr->host[0]) {
		addr = xmalloc(sizeof(struct sockaddr_in));
		slurm_set_addr(addr, job_ptr->port, job_ptr->host);
		msg_arg = xmalloc(sizeof(srun_node_fail_msg_t));
		msg_arg->job_id   = job_id;
		msg_arg->step_id  = NO_VAL;
		msg_arg->nodelist = xstrdup(node_name);
		_srun_agent_launch(addr, job_ptr->host, SRUN_NODE_FAIL, 
				msg_arg);
	}


	step_iterator = list_iterator_create(job_ptr->step_list);
	while ((step_ptr = (struct step_record *) list_next(step_iterator))) {
		if (!bit_test(step_ptr->step_node_bitmap, bit_position))
			continue;	/* job step not on this node */
		if ( (step_ptr->port    == 0)    || 
		     (step_ptr->host    == NULL) ||
		     (step_ptr->batch_step)      ||
		     (step_ptr->host[0] == '\0') )
			continue;
		addr = xmalloc(sizeof(struct sockaddr_in));
		slurm_set_addr(addr, step_ptr->port, step_ptr->host);
		msg_arg = xmalloc(sizeof(srun_node_fail_msg_t));
		msg_arg->job_id   = job_ptr->job_id;
		msg_arg->step_id  = step_ptr->step_id;
		msg_arg->nodelist = xstrdup(node_name);
		_srun_agent_launch(addr, step_ptr->host, SRUN_NODE_FAIL, 
				msg_arg);
	}	
	list_iterator_destroy(step_iterator);
}

/* srun_ping - ping all srun commands that have not been heard from recently */
extern void srun_ping (void)
{
	ListIterator job_iterator;
	struct job_record *job_ptr;
	ListIterator step_iterator;
	struct step_record *step_ptr;
	slurm_addr * addr;
	time_t now = time(NULL);
	time_t old = now - (slurmctld_conf.inactive_limit / 2);
	srun_ping_msg_t *msg_arg;

	if (slurmctld_conf.inactive_limit == 0)
		return;		/* No limit, don't bother pinging */

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		xassert (job_ptr->magic == JOB_MAGIC);
		
		if (job_ptr->job_state != JOB_RUNNING)
			continue;
		if ( (job_ptr->time_last_active <= old) && job_ptr->port &&
			job_ptr->host && job_ptr->host[0] ) {
			addr = xmalloc(sizeof(struct sockaddr_in));
			slurm_set_addr(addr, job_ptr->port, job_ptr->host);
			msg_arg = xmalloc(sizeof(srun_ping_msg_t));
			msg_arg->job_id  = job_ptr->job_id;
			msg_arg->step_id = NO_VAL;
			_srun_agent_launch(addr, job_ptr->host, SRUN_PING, 
				msg_arg);
		}

		step_iterator = list_iterator_create(job_ptr->step_list);
		while ((step_ptr = (struct step_record *) 
				list_next(step_iterator))) {
			if ( (step_ptr->time_last_active > old) ||
			     (step_ptr->port    == 0)    || 
			     (step_ptr->host    == NULL) ||
			     (step_ptr->batch_step)      ||
			     (step_ptr->host[0] == '\0') )
				continue;
			debug3("sending message to host=%s, port=%u\n", 
			       step_ptr->host,
			       step_ptr->port);

			addr = xmalloc(sizeof(struct sockaddr_in));
			slurm_set_addr(addr, step_ptr->port, step_ptr->host);
			msg_arg = xmalloc(sizeof(srun_ping_msg_t));
			msg_arg->job_id  = job_ptr->job_id;
			msg_arg->step_id = step_ptr->step_id;
			_srun_agent_launch(addr, step_ptr->host, SRUN_PING, 
				msg_arg);
		}	
		list_iterator_destroy(step_iterator);
	}

	list_iterator_destroy(job_iterator);
}

/*
 * srun_timeout - notify srun of a job's imminent timeout
 * IN job_id  - if of job to notify
 * IN timeout - when job is scheduled to be killed
 */
extern void srun_timeout (uint32_t job_id, time_t timeout)
{
	struct job_record *job_ptr = find_job_record (job_id);
	slurm_addr * addr;
	srun_timeout_msg_t *msg_arg;
	ListIterator step_iterator;
	struct step_record *step_ptr;

	xassert(job_ptr);
	if (job_ptr->job_state != JOB_RUNNING)
		return;

	if (job_ptr->port && job_ptr->host && job_ptr->host[0]) {
		addr = xmalloc(sizeof(struct sockaddr_in));
		slurm_set_addr(addr, job_ptr->port, job_ptr->host);
		msg_arg = xmalloc(sizeof(srun_timeout_msg_t));
		msg_arg->job_id   = job_id;
		msg_arg->step_id  = NO_VAL;
		msg_arg->timeout = timeout;
		_srun_agent_launch(addr, job_ptr->host, SRUN_TIMEOUT, 
				msg_arg);
	}


	step_iterator = list_iterator_create(job_ptr->step_list);
	while ((step_ptr = (struct step_record *) list_next(step_iterator))) {
		if ( (step_ptr->port    == 0)    || 
		     (step_ptr->host    == NULL) ||
		     (step_ptr->batch_step)      ||
		     (step_ptr->host[0] == '\0') )
			continue;
		addr = xmalloc(sizeof(struct sockaddr_in));
		slurm_set_addr(addr, step_ptr->port, step_ptr->host);
		msg_arg = xmalloc(sizeof(srun_timeout_msg_t));
		msg_arg->job_id   = job_ptr->job_id;
		msg_arg->step_id  = step_ptr->step_id;
		msg_arg->timeout  = timeout;
		_srun_agent_launch(addr, step_ptr->host, SRUN_TIMEOUT, 
				msg_arg);
	}	
	list_iterator_destroy(step_iterator);
}

/*
 * srun_response - note that srun has responded
 * IN job_id  - id of job responding
 * IN step_id - id of step responding or NO_VAL if not a step
 */
extern void srun_response(uint32_t job_id, uint32_t step_id)
{
	struct job_record  *job_ptr = find_job_record (job_id);
	struct step_record *step_ptr;
	time_t now = time(NULL);

	if (job_ptr == NULL)
		return;
	job_ptr->time_last_active = now;

	if ((step_id != NO_VAL) &&
	    ((step_ptr = find_step_record(job_ptr, (uint16_t) step_id))))
		step_ptr->time_last_active = now;
}

