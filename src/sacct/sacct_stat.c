/*****************************************************************************\
 *  sacct_stat.c - stat slurmd for percise job information
 *
 *  $Id: options.c 7541 2006-03-18 01:44:58Z da $
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>.
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

#include "sacct.h"
#include <pthread.h>
#include "src/common/slurm_jobacct.h"
#include "src/common/forward.h"
#include "src/common/slurm_auth.h"

pthread_mutex_t stat_mutex = PTHREAD_MUTEX_INITIALIZER;	
pthread_cond_t stat_cond = PTHREAD_COND_INITIALIZER;
step_rec_t step;
	
int thr_finished = 0;
	
void *_stat_thread(void *args);
int _sacct_query(resource_allocation_response_msg_t *job, uint32_t step_id);
int _process_results();

void *_stat_thread(void *args)
{
	slurm_msg_t *msg = (slurm_msg_t *) args;
	slurm_msg_t resp_msg;
	stat_jobacct_msg_t *jobacct_msg = NULL;
	ListIterator itr;
	ListIterator data_itr;
	List ret_list = NULL;
	sacct_t temp_sacct;
	sacct_t temp_sacct2;
	ret_types_t *ret_type = NULL;
	ret_data_info_t *ret_data_info = NULL;
	int rc = SLURM_SUCCESS;
	int ntasks = 0;

	memset(&temp_sacct, 0, sizeof(sacct_t));
	temp_sacct.min_cpu = (float)NO_VAL;
	memset(&temp_sacct2, 0, sizeof(sacct_t));
	temp_sacct2.min_cpu = (float)NO_VAL;
	
	ret_list = slurm_send_recv_node_msg(msg, 
					    &resp_msg, 
					    msg->forward.timeout);
	if (!ret_list) {
		error("got an error no list returned");
		goto cleanup;
	}
	g_slurm_auth_destroy(resp_msg.auth_cred);

	switch (resp_msg.msg_type) {
	case MESSAGE_STAT_JOBACCT:
		jobacct_msg = (stat_jobacct_msg_t *)resp_msg.data;
		if(jobacct_msg) {
			debug2("got it back for job %d %d tasks", 
			     jobacct_msg->job_id,
			     jobacct_msg->num_tasks);
			jobacct_g_2_sacct(&temp_sacct, jobacct_msg->jobacct);
			ntasks = jobacct_msg->num_tasks;
			slurm_free_stat_jobacct_msg(jobacct_msg);
		} else {
			error("No Jobacct message returned!");
		}
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);	
		error("there was an error with the request rc = %s", 
		      slurm_strerror(rc));
		break;
	default:
		rc = SLURM_UNEXPECTED_MSG_ERROR;
		break;
	}

	itr = list_iterator_create(ret_list);		
	while((ret_type = list_next(itr)) != NULL) {
		switch (ret_type->type) {
		case MESSAGE_STAT_JOBACCT:
			data_itr = 
				list_iterator_create(ret_type->ret_data_list);
			while((ret_data_info = list_next(data_itr)) != NULL) {
				jobacct_msg = (stat_jobacct_msg_t *)
					ret_data_info->data;
				if(jobacct_msg) {
					debug2("got it back for job %d", 
					       jobacct_msg->job_id);
					jobacct_g_2_sacct(
						&temp_sacct2, 
						jobacct_msg->jobacct);
					ntasks += jobacct_msg->num_tasks;
					slurm_free_stat_jobacct_msg(
						jobacct_msg);
					aggregate_sacct(&temp_sacct, 
							&temp_sacct2);
				}
			}
			break;
		case RESPONSE_SLURM_RC:
			rc = ret_type->msg_rc;
			error("there was an error with the request rc = %s", 
			      slurm_strerror(rc));
			break;
		default:
			rc = ret_type->msg_rc;
			error("unknown return given %d rc = %s", 
			      ret_type->type, slurm_strerror(rc));
			break;
		}
	}
	list_iterator_destroy(itr);
	list_destroy(ret_list);

	pthread_mutex_lock(&stat_mutex);
	aggregate_sacct(&step.sacct, &temp_sacct);
	step.ntasks += ntasks;		
	pthread_mutex_unlock(&stat_mutex);
cleanup:
	
	pthread_mutex_lock(&stat_mutex);
	thr_finished++;
	pthread_cond_signal(&stat_cond);
	pthread_mutex_unlock(&stat_mutex);
	return NULL;
}

int _sacct_query(resource_allocation_response_msg_t *job, uint32_t step_id)
{
	slurm_msg_t *msg_array_ptr;
	stat_jobacct_msg_t r;
	int i;
	int *span = set_span(job->node_cnt, 0);
	forward_t forward;
	int thr_count = 0;
	
	debug("getting the stat of job %d on %d nodes", 
	      job->job_id, job->node_cnt);

	memset(&step.sacct, 0, sizeof(sacct_t));
	step.sacct.min_cpu = (float)NO_VAL;
	step.header.jobnum = job->job_id;
	step.header.partition = NULL;
	step.header.blockid = NULL;
	step.stepnum = step_id;
	step.nodes = job->node_list;
	step.stepname = NULL;
	step.status = JOB_RUNNING;
	step.ntasks = 0;
	msg_array_ptr = xmalloc(sizeof(slurm_msg_t) * job->node_cnt);
	
	/* Common message contents */
	r.job_id      = job->job_id;
	r.step_id     = step_id;
	r.jobacct     = jobacct_g_alloc((uint16_t)NO_VAL);

	forward.cnt = job->node_cnt;
	/* we need this for forwarding, but not really anything else, so 
	   this can be set to any sting as long as there are the same 
	   number as hosts we are going to */
	forward.name = xmalloc(sizeof(char) * (MAX_SLURM_NAME * forward.cnt));
	for(i=0; i < forward.cnt; i++) {
		strncpy(&forward.name[i*MAX_SLURM_NAME], "-", MAX_SLURM_NAME);
	}
	forward.addr = job->node_addr;
	forward.node_id = NULL;
	forward.timeout = 5000;
	
	thr_count = 0;
	for (i = 0; i < job->node_cnt; i++) {
		pthread_attr_t attr;
		pthread_t threadid;
		slurm_msg_t *m = &msg_array_ptr[thr_count];
		
		m->srun_node_id    = 0;
		m->msg_type        = MESSAGE_STAT_JOBACCT;
		m->data            = &r;
		m->ret_list = NULL;
		m->orig_addr.sin_addr.s_addr = 0;
		
		memcpy(&m->address, 
		       &job->node_addr[i], 
		       sizeof(slurm_addr));
		
		forward_set(&m->forward,
			    span[thr_count],
			    &i,
			    &forward);
		
		slurm_attr_init(&attr);
		if (pthread_attr_setdetachstate(&attr,
						PTHREAD_CREATE_DETACHED))
			error("pthread_attr_setdetachstate error %m");
		
		if(pthread_create(&threadid,
				   &attr,
				   _stat_thread,
				   (void *) m)) {
			error("pthread_create error %m");
			exit(0);
		}
		slurm_attr_destroy(&attr);
		thr_count++;
	}
	xfree(span);
	xfree(forward.name);
	if (!thr_count) {
		fatal("No threads created!! exiting");
	}
	slurm_mutex_lock(&stat_mutex);
	while(thr_count > thr_finished) {
		pthread_cond_wait(&stat_cond, &stat_mutex);
	}
	slurm_mutex_unlock(&stat_mutex);
	
	pthread_cond_destroy(&stat_cond);
	slurm_mutex_destroy(&stat_mutex);
	if(step.ntasks) {
		step.sacct.ave_rss *= 1024;
		step.sacct.max_rss *= 1024;
		step.sacct.ave_vsize *= 1024;
		step.sacct.max_vsize *= 1024;

		step.sacct.ave_cpu /= step.ntasks;
		step.sacct.ave_cpu /= 100;
		step.sacct.min_cpu /= 100;
		step.sacct.ave_rss /= step.ntasks;
		step.sacct.ave_vsize /= step.ntasks;
		step.sacct.ave_pages /= step.ntasks;
	}
	xfree(msg_array_ptr);
	jobacct_g_free(r.jobacct);	
	return SLURM_SUCCESS;
}

int _process_results()
{
	print_fields(JOBSTEP, &step);
	return SLURM_SUCCESS;
}

int sacct_stat(uint32_t jobid, uint32_t stepid)
{
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;
	stat_jobacct_msg_t req;
	resource_allocation_response_msg_t *job = NULL;
	int rc = SLURM_SUCCESS;

	debug("requesting info for job %u.%u", jobid, stepid);
	req.job_id = jobid;
	req.step_id = stepid;
	req.jobacct = jobacct_g_alloc((uint16_t)NO_VAL);
	req_msg.msg_type = MESSAGE_STAT_JOBACCT;
	req_msg.data     = &req;
	
	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg) < 0) {
		jobacct_g_free(req.jobacct);
		return SLURM_ERROR;
	}
		
	jobacct_g_free(req.jobacct);
	
	switch (resp_msg.msg_type) {
	case RESPONSE_RESOURCE_ALLOCATION:
		job = (resource_allocation_response_msg_t *)resp_msg.data;
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);	
		printf("problem getting job: %s\n", slurm_strerror(rc));
		slurm_seterrno_ret(rc);
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}
		
	if(!job) {
		error("didn't get the job record rc = %s", slurm_strerror(rc));
		return rc;
	}

	_sacct_query(job, stepid);
	slurm_free_resource_allocation_response_msg(job);	
	
	_process_results();
	return rc;
}
