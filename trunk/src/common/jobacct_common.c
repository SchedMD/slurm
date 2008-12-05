/*****************************************************************************\
 *  jobacct_common.c - common functions for almost all jobacct plugins.
 *****************************************************************************
 *
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
 *  Written by Danny Auble, <da@llnl.gov>
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
 *
 *  This file is patterned after jobcomp_linux.c, written by Morris Jette and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/

#include "jobacct_common.h"

bool jobacct_shutdown = false;
bool jobacct_suspended = false;
List task_list = NULL;
pthread_mutex_t jobacct_lock = PTHREAD_MUTEX_INITIALIZER;
uint32_t cont_id = (uint32_t)NO_VAL;
uint32_t acct_job_id = 0;
uint32_t job_mem_limit = 0;
bool pgid_plugin = false;
uint32_t mult = 1000;

static void _pack_jobacct_id(jobacct_id_t *jobacct_id, Buf buffer)
{
	pack32((uint32_t)jobacct_id->nodeid, buffer);
	pack16((uint16_t)jobacct_id->taskid, buffer);
}

static int _unpack_jobacct_id(jobacct_id_t *jobacct_id, Buf buffer)
{
	safe_unpack32(&jobacct_id->nodeid, buffer);
	safe_unpack16(&jobacct_id->taskid, buffer);
	return SLURM_SUCCESS;
unpack_error:
	return SLURM_ERROR;
}

static void _pack_sacct(sacct_t *sacct, Buf buffer)
{
	int i=0;
	uint32_t temp;

	if(!sacct) {
		for(i=0; i<8; i++)
			pack32((uint32_t) 0, buffer);

		for(i=0; i<4; i++) {	/* _pack_jobacct_id() */
			pack32((uint32_t) 0, buffer);
			pack16((uint16_t) 0, buffer);
		}
		return;
	} 

	pack32(sacct->max_vsize, buffer);
	temp = sacct->ave_vsize * mult;
	pack32(temp, buffer);
	pack32(sacct->max_rss, buffer);
	temp = (uint32_t)sacct->ave_rss * mult;
	pack32(temp, buffer);
	pack32(sacct->max_pages, buffer);
	temp = (uint32_t)sacct->ave_pages * mult;
	pack32(temp, buffer);
	temp = (uint32_t)sacct->min_cpu * mult;
	pack32(temp, buffer);
	temp = (uint32_t)sacct->ave_cpu * mult;
	pack32(temp, buffer);

	_pack_jobacct_id(&sacct->max_vsize_id, buffer);
	_pack_jobacct_id(&sacct->max_rss_id, buffer);
	_pack_jobacct_id(&sacct->max_pages_id, buffer);
	_pack_jobacct_id(&sacct->min_cpu_id, buffer);
}

/* you need to xfree this */
static int _unpack_sacct(sacct_t *sacct, Buf buffer)
{
	/* this is here to handle the floats since it appears sending
	 * in a float with a typecast returns incorrect information
	 */
	uint32_t temp;

	safe_unpack32(&sacct->max_vsize, buffer);
	safe_unpack32(&temp, buffer);
	sacct->ave_vsize = temp / mult;
	safe_unpack32(&sacct->max_rss, buffer);
	safe_unpack32(&temp, buffer);
	sacct->ave_rss = temp / mult;
	safe_unpack32(&sacct->max_pages, buffer);
	safe_unpack32(&temp, buffer);
	sacct->ave_pages = temp / mult;
	safe_unpack32(&temp, buffer);
	sacct->min_cpu = temp / mult;
	safe_unpack32(&temp, buffer);
	sacct->ave_cpu = temp / mult;
	if(_unpack_jobacct_id(&sacct->max_vsize_id, buffer) != SLURM_SUCCESS)
		goto unpack_error;
	if(_unpack_jobacct_id(&sacct->max_rss_id, buffer) != SLURM_SUCCESS)
		goto unpack_error;
	if(_unpack_jobacct_id(&sacct->max_pages_id, buffer) != SLURM_SUCCESS)
		goto unpack_error;
	if(_unpack_jobacct_id(&sacct->min_cpu_id, buffer) != SLURM_SUCCESS)
		goto unpack_error;

	return SLURM_SUCCESS;

unpack_error:
	sacct = NULL;
       	return SLURM_ERROR;
}
extern jobacct_job_rec_t *create_jobacct_job_rec()
{
	jobacct_job_rec_t *job = xmalloc(sizeof(jobacct_job_rec_t));
	memset(&job->sacct, 0, sizeof(sacct_t));
	job->sacct.min_cpu = (float)NO_VAL;
	job->state = JOB_PENDING;
	job->steps = list_create(destroy_jobacct_step_rec);
	job->requid = -1;
	job->lft = (uint32_t)NO_VAL;

      	return job;
}

extern jobacct_step_rec_t *create_jobacct_step_rec()
{
	jobacct_step_rec_t *step = xmalloc(sizeof(jobacct_job_rec_t));
	memset(&step->sacct, 0, sizeof(sacct_t));
	step->stepid = (uint32_t)NO_VAL;
	step->state = NO_VAL;
	step->exitcode = NO_VAL;
	step->ncpus = (uint32_t)NO_VAL;
	step->elapsed = (uint32_t)NO_VAL;
	step->tot_cpu_sec = (uint32_t)NO_VAL;
	step->tot_cpu_usec = (uint32_t)NO_VAL;
	step->requid = -1;

	return step;
}

extern void destroy_jobacct_job_rec(void *object)
{
	jobacct_job_rec_t *job = (jobacct_job_rec_t *)object;
	if (job) {
		xfree(job->account);
		xfree(job->blockid);
		xfree(job->cluster);
		xfree(job->jobname);
		xfree(job->partition);
		xfree(job->nodes);
		if(job->steps)
			list_destroy(job->steps);
		xfree(job->user);
		xfree(job->wckey);
		xfree(job);
	}
}

extern void destroy_jobacct_step_rec(void *object)
{
	jobacct_step_rec_t *step = (jobacct_step_rec_t *)object;
	if (step) {
		xfree(step->nodes);
		xfree(step->stepname);
		xfree(step);
	}
}

extern void destroy_jobacct_selected_step(void *object)
{
	jobacct_selected_step_t *step = (jobacct_selected_step_t *)object;
	if (step) {
		xfree(step);
	}
}

 
extern void pack_jobacct_job_rec(void *object, uint16_t rpc_version, Buf buffer)
{
	jobacct_job_rec_t *job = (jobacct_job_rec_t *)object;
	ListIterator itr = NULL;
	jobacct_step_rec_t *step = NULL;
	uint32_t count = 0;
	double tmp_prio;
	uint32_t pack_prio;

	if(rpc_version >= 4) {
		pack32(job->alloc_cpus, buffer);
		pack32(job->associd, buffer);
		packstr(job->account, buffer);
		packstr(job->blockid, buffer);
		packstr(job->cluster, buffer);
		pack32(job->elapsed, buffer);
		pack_time(job->eligible, buffer);
		pack_time(job->end, buffer);
		pack32(job->exitcode, buffer);
		pack32(job->gid, buffer);
		pack32(job->jobid, buffer);
		packstr(job->jobname, buffer);
		pack32(job->lft, buffer);
		packstr(job->partition, buffer);
		packstr(job->nodes, buffer);

		tmp_prio = job->priority + 200;
		tmp_prio *= 1000000;
		pack_prio = (uint32_t)tmp_prio;
		
		pack32(pack_prio, buffer);

		pack16(job->qos, buffer);
		pack32(job->req_cpus, buffer);
		pack32(job->requid, buffer);
		_pack_sacct(&job->sacct, buffer);
		pack32(job->show_full, buffer);
		pack_time(job->start, buffer);
		pack16((uint16_t)job->state, buffer);
		if(job->steps)
			count = list_count(job->steps);
		pack32(count, buffer);
		if(count) {
			itr = list_iterator_create(job->steps);
			while((step = list_next(itr))) {
				pack_jobacct_step_rec(step, rpc_version,
						      buffer);
			}
			list_iterator_destroy(itr);
		}
		pack_time(job->submit, buffer);
		pack32(job->suspended, buffer);
		pack32(job->sys_cpu_sec, buffer);
		pack32(job->sys_cpu_usec, buffer);
		pack32(job->tot_cpu_sec, buffer);
		pack32(job->tot_cpu_usec, buffer);
		pack16(job->track_steps, buffer);
		pack32(job->uid, buffer);
		packstr(job->user, buffer);
		pack32(job->user_cpu_sec, buffer);
		pack32(job->user_cpu_usec, buffer);
		packstr(job->wckey, buffer); /* added for rpc_version 4 */
		pack32(job->wckeyid, buffer); /* added for rpc_version 4 */
	} else {
		pack32(job->alloc_cpus, buffer);
		pack32(job->associd, buffer);
		packstr(job->account, buffer);
		packstr(job->blockid, buffer);
		packstr(job->cluster, buffer);
		pack32(job->elapsed, buffer);
		pack_time(job->eligible, buffer);
		pack_time(job->end, buffer);
		pack32(job->exitcode, buffer);
		pack32(job->gid, buffer);
		pack32(job->jobid, buffer);
		packstr(job->jobname, buffer);
		pack32(job->lft, buffer);
		packstr(job->partition, buffer);
		packstr(job->nodes, buffer);
		pack32(job->priority, buffer);
		pack16(job->qos, buffer);
		pack32(job->req_cpus, buffer);
		pack32(job->requid, buffer);
		_pack_sacct(&job->sacct, buffer);
		pack32(job->show_full, buffer);
		pack_time(job->start, buffer);
		pack16((uint16_t)job->state, buffer);
		if(job->steps)
			count = list_count(job->steps);
		pack32(count, buffer);
		if(count) {
			itr = list_iterator_create(job->steps);
			while((step = list_next(itr))) {
				pack_jobacct_step_rec(step, rpc_version,
						      buffer);
			}
			list_iterator_destroy(itr);
		}
		pack_time(job->submit, buffer);
		pack32(job->suspended, buffer);
		pack32(job->sys_cpu_sec, buffer);
		pack32(job->sys_cpu_usec, buffer);
		pack32(job->tot_cpu_sec, buffer);
		pack32(job->tot_cpu_usec, buffer);
		pack16(job->track_steps, buffer);
		pack32(job->uid, buffer);
		packstr(job->user, buffer);
		pack32(job->user_cpu_sec, buffer);
		pack32(job->user_cpu_usec, buffer);
	}
}

extern int unpack_jobacct_job_rec(void **job, uint16_t rpc_version, Buf buffer)
{
	jobacct_job_rec_t *job_ptr = xmalloc(sizeof(jobacct_job_rec_t));
	int i = 0;
	jobacct_step_rec_t *step = NULL;
	uint32_t count = 0;
	uint32_t uint32_tmp;
	uint16_t uint16_tmp;
	double tmp_prio;

	*job = job_ptr;

	if(rpc_version >= 4) {
		safe_unpack32(&job_ptr->alloc_cpus, buffer);
		safe_unpack32(&job_ptr->associd, buffer);
		safe_unpackstr_xmalloc(&job_ptr->account, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_ptr->blockid, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_ptr->cluster, &uint32_tmp, buffer);
		safe_unpack32(&job_ptr->elapsed, buffer);
		safe_unpack_time(&job_ptr->eligible, buffer);
		safe_unpack_time(&job_ptr->end, buffer);
		safe_unpack32(&uint32_tmp, buffer);
		job_ptr->exitcode = (int32_t)uint32_tmp;
		safe_unpack32(&job_ptr->gid, buffer);
		safe_unpack32(&job_ptr->jobid, buffer);
		safe_unpackstr_xmalloc(&job_ptr->jobname, &uint32_tmp, buffer);
		safe_unpack32(&job_ptr->lft, buffer);
		safe_unpackstr_xmalloc(&job_ptr->partition, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&job_ptr->nodes, &uint32_tmp, buffer);
		safe_unpack32(&uint32_tmp, buffer);

		tmp_prio = (double)uint32_tmp / (double)1000000;
		tmp_prio -= (double)200;
		job_ptr->priority = (int32_t)tmp_prio;

		safe_unpack16(&job_ptr->qos, buffer);
		safe_unpack32(&job_ptr->req_cpus, buffer);
		safe_unpack32(&job_ptr->requid, buffer);
		_pack_sacct(&job_ptr->sacct, buffer);
		safe_unpack32(&job_ptr->show_full, buffer);
		safe_unpack_time(&job_ptr->start, buffer);
		safe_unpack16(&uint16_tmp, buffer);
		job_ptr->state = uint16_tmp;
		safe_unpack32(&count, buffer);

		job_ptr->steps = list_create(destroy_jobacct_step_rec);
		for(i=0; i<count; i++) {
			unpack_jobacct_step_rec(&step, rpc_version, buffer);
			if(step)
				list_append(job_ptr->steps, step);
		}

		safe_unpack_time(&job_ptr->submit, buffer);
		safe_unpack32(&job_ptr->suspended, buffer);
		safe_unpack32(&job_ptr->sys_cpu_sec, buffer);
		safe_unpack32(&job_ptr->sys_cpu_usec, buffer);
		safe_unpack32(&job_ptr->tot_cpu_sec, buffer);
		safe_unpack32(&job_ptr->tot_cpu_usec, buffer);
		safe_unpack16(&job_ptr->track_steps, buffer);
		safe_unpack32(&job_ptr->uid, buffer);
		safe_unpackstr_xmalloc(&job_ptr->user, &uint32_tmp, buffer);
		safe_unpack32(&job_ptr->user_cpu_sec, buffer);
		safe_unpack32(&job_ptr->user_cpu_usec, buffer);
		safe_unpackstr_xmalloc(&job_ptr->wckey, &uint32_tmp, buffer);
		safe_unpack32(&job_ptr->wckeyid, buffer);
	} else {
		safe_unpack32(&job_ptr->alloc_cpus, buffer);
		safe_unpack32(&job_ptr->associd, buffer);
		safe_unpackstr_xmalloc(&job_ptr->account, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_ptr->blockid, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_ptr->cluster, &uint32_tmp, buffer);
		safe_unpack32(&job_ptr->elapsed, buffer);
		safe_unpack_time(&job_ptr->eligible, buffer);
		safe_unpack_time(&job_ptr->end, buffer);
		safe_unpack32(&uint32_tmp, buffer);
		job_ptr->exitcode = (int32_t)uint32_tmp;
		safe_unpack32(&job_ptr->gid, buffer);
		safe_unpack32(&job_ptr->jobid, buffer);
		safe_unpackstr_xmalloc(&job_ptr->jobname, &uint32_tmp, buffer);
		safe_unpack32(&job_ptr->lft, buffer);
		safe_unpackstr_xmalloc(&job_ptr->partition, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&job_ptr->nodes, &uint32_tmp, buffer);
		safe_unpack32(&uint32_tmp, buffer);
		job_ptr->priority = (int32_t)uint32_tmp;
		safe_unpack16(&job_ptr->qos, buffer);
		safe_unpack32(&job_ptr->req_cpus, buffer);
		safe_unpack32(&job_ptr->requid, buffer);
		_pack_sacct(&job_ptr->sacct, buffer);
		safe_unpack32(&job_ptr->show_full, buffer);
		safe_unpack_time(&job_ptr->start, buffer);
		safe_unpack16(&uint16_tmp, buffer);
		job_ptr->state = uint16_tmp;
		safe_unpack32(&count, buffer);

		job_ptr->steps = list_create(destroy_jobacct_step_rec);
		for(i=0; i<count; i++) {
			unpack_jobacct_step_rec(&step, rpc_version, buffer);
			if(step)
				list_append(job_ptr->steps, step);
		}

		safe_unpack_time(&job_ptr->submit, buffer);
		safe_unpack32(&job_ptr->suspended, buffer);
		safe_unpack32(&job_ptr->sys_cpu_sec, buffer);
		safe_unpack32(&job_ptr->sys_cpu_usec, buffer);
		safe_unpack32(&job_ptr->tot_cpu_sec, buffer);
		safe_unpack32(&job_ptr->tot_cpu_usec, buffer);
		safe_unpack16(&job_ptr->track_steps, buffer);
		safe_unpack32(&job_ptr->uid, buffer);
		safe_unpackstr_xmalloc(&job_ptr->user, &uint32_tmp, buffer);
		safe_unpack32(&job_ptr->user_cpu_sec, buffer);
		safe_unpack32(&job_ptr->user_cpu_usec, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	destroy_jobacct_job_rec(job_ptr);
	*job = NULL;
	return SLURM_ERROR;
}
 
extern void pack_jobacct_step_rec(jobacct_step_rec_t *step, 
				  uint16_t rpc_version, Buf buffer)
{
	pack32(step->elapsed, buffer);
	pack_time(step->end, buffer);
	pack32((uint32_t)step->exitcode, buffer);
	pack32(step->jobid, buffer);
	pack32(step->ncpus, buffer);
        packstr(step->nodes, buffer);
	pack32(step->requid, buffer);
	_pack_sacct(&step->sacct, buffer);
	pack_time(step->start, buffer);
	pack16(step->state, buffer);
	pack32(step->stepid, buffer);	/* job's step number */
	packstr(step->stepname, buffer);
	pack32(step->suspended, buffer);
	pack32(step->sys_cpu_sec, buffer);
	pack32(step->sys_cpu_usec, buffer);
	pack32(step->tot_cpu_sec, buffer);
	pack32(step->tot_cpu_usec, buffer);
	pack32(step->user_cpu_sec, buffer);
	pack32(step->user_cpu_usec, buffer);
}

extern int unpack_jobacct_step_rec(jobacct_step_rec_t **step, 
				   uint16_t rpc_version, Buf buffer)
{
	uint32_t uint32_tmp;
	uint16_t uint16_tmp;
	jobacct_step_rec_t *step_ptr = xmalloc(sizeof(jobacct_step_rec_t));

	*step = step_ptr;

	safe_unpack32(&step_ptr->elapsed, buffer);
	safe_unpack_time(&step_ptr->end, buffer);
	safe_unpack32(&uint32_tmp, buffer);
	step_ptr->exitcode = (int32_t)uint32_tmp;
	safe_unpack32(&step_ptr->jobid, buffer);
	safe_unpack32(&step_ptr->ncpus, buffer);
        safe_unpackstr_xmalloc(&step_ptr->nodes, &uint32_tmp, buffer);
	safe_unpack32(&step_ptr->requid, buffer);
	_unpack_sacct(&step_ptr->sacct, buffer);
	safe_unpack_time(&step_ptr->start, buffer);
	safe_unpack16(&uint16_tmp, buffer);
	step_ptr->state = uint16_tmp;
	safe_unpack32(&step_ptr->stepid, buffer);	/* job's step number */
	safe_unpackstr_xmalloc(&step_ptr->stepname, &uint32_tmp, buffer);
	safe_unpack32(&step_ptr->suspended, buffer);
	safe_unpack32(&step_ptr->sys_cpu_sec, buffer);
	safe_unpack32(&step_ptr->sys_cpu_usec, buffer);
	safe_unpack32(&step_ptr->tot_cpu_sec, buffer);
	safe_unpack32(&step_ptr->tot_cpu_usec, buffer);
	safe_unpack32(&step_ptr->user_cpu_sec, buffer);
	safe_unpack32(&step_ptr->user_cpu_usec, buffer);

	return SLURM_SUCCESS;

unpack_error:
	destroy_jobacct_step_rec(step_ptr);
	*step = NULL;
	return SLURM_ERROR;
} 

extern void pack_jobacct_selected_step(jobacct_selected_step_t *step,
				       uint16_t rpc_version, Buf buffer)
{
	pack32(step->jobid, buffer);
	pack32(step->stepid, buffer);
}

extern int unpack_jobacct_selected_step(jobacct_selected_step_t **step,
					uint16_t rpc_version, Buf buffer)
{
	jobacct_selected_step_t *step_ptr =
		xmalloc(sizeof(jobacct_selected_step_t));
	
	*step = step_ptr;

	safe_unpack32(&step_ptr->jobid, buffer);
	safe_unpack32(&step_ptr->stepid, buffer);

	return SLURM_SUCCESS;

unpack_error:
	destroy_jobacct_selected_step(step_ptr);
	*step = NULL;
	return SLURM_ERROR;
}

extern int jobacct_common_init_struct(struct jobacctinfo *jobacct, 
				      jobacct_id_t *jobacct_id)
{
	if(!jobacct_id) {
		jobacct_id_t temp_id;
		temp_id.taskid = (uint16_t)NO_VAL;
		temp_id.nodeid = (uint32_t)NO_VAL;
		jobacct_id = &temp_id;
	}
	memset(jobacct, 0, sizeof(struct jobacctinfo));
	jobacct->sys_cpu_sec = 0;
	jobacct->sys_cpu_usec = 0;
	jobacct->user_cpu_sec = 0;
	jobacct->user_cpu_usec = 0;

	jobacct->max_vsize = 0;
	memcpy(&jobacct->max_vsize_id, jobacct_id, sizeof(jobacct_id_t));
	jobacct->tot_vsize = 0;
	jobacct->max_rss = 0;
	memcpy(&jobacct->max_rss_id, jobacct_id, sizeof(jobacct_id_t));
	jobacct->tot_rss = 0;
	jobacct->max_pages = 0;
	memcpy(&jobacct->max_pages_id, jobacct_id, sizeof(jobacct_id_t));
	jobacct->tot_pages = 0;
	jobacct->min_cpu = (uint32_t)NO_VAL;
	memcpy(&jobacct->min_cpu_id, jobacct_id, sizeof(jobacct_id_t));
	jobacct->tot_cpu = 0;
	
	return SLURM_SUCCESS;
}

extern struct jobacctinfo *jobacct_common_alloc_jobacct(
	jobacct_id_t *jobacct_id)
{
	struct jobacctinfo *jobacct = xmalloc(sizeof(struct jobacctinfo));
	jobacct_common_init_struct(jobacct, jobacct_id);
	return jobacct;
}

extern void jobacct_common_free_jobacct(void *object)
{
	struct jobacctinfo *jobacct = (struct jobacctinfo *)object;
	xfree(jobacct);
	jobacct = NULL;
}

extern int jobacct_common_setinfo(struct jobacctinfo *jobacct, 
				  enum jobacct_data_type type, void *data)
{
	int rc = SLURM_SUCCESS;
	int *fd = (int *)data;
	struct rusage *rusage = (struct rusage *)data;
	uint32_t *uint32 = (uint32_t *) data;
	jobacct_id_t *jobacct_id = (jobacct_id_t *) data;
	struct jobacctinfo *send = (struct jobacctinfo *) data;

	slurm_mutex_lock(&jobacct_lock);
	switch (type) {
	case JOBACCT_DATA_TOTAL:
		memcpy(jobacct, send, sizeof(struct jobacctinfo));
		break;
	case JOBACCT_DATA_PIPE:
		safe_write(*fd, jobacct, sizeof(struct jobacctinfo));
		break;
	case JOBACCT_DATA_RUSAGE:
		jobacct->user_cpu_sec = rusage->ru_utime.tv_sec;
		jobacct->user_cpu_usec = rusage->ru_utime.tv_usec;
		jobacct->sys_cpu_sec = rusage->ru_stime.tv_sec;
		jobacct->sys_cpu_usec = rusage->ru_stime.tv_usec;
		break;
	case JOBACCT_DATA_MAX_RSS:
		jobacct->max_rss = *uint32;
		break;
	case JOBACCT_DATA_MAX_RSS_ID:
		jobacct->max_rss_id = *jobacct_id;
		break;
	case JOBACCT_DATA_TOT_RSS:
		jobacct->tot_rss = *uint32;
		break;
	case JOBACCT_DATA_MAX_VSIZE:
		jobacct->max_vsize = *uint32;
		break;
	case JOBACCT_DATA_MAX_VSIZE_ID:
		jobacct->max_vsize_id = *jobacct_id;
		break;
	case JOBACCT_DATA_TOT_VSIZE:
		jobacct->tot_vsize = *uint32;
		break;
	case JOBACCT_DATA_MAX_PAGES:
		jobacct->max_pages = *uint32;
		break;
	case JOBACCT_DATA_MAX_PAGES_ID:
		jobacct->max_pages_id = *jobacct_id;
		break;
	case JOBACCT_DATA_TOT_PAGES:
		jobacct->tot_pages = *uint32;
		break;
	case JOBACCT_DATA_MIN_CPU:
		jobacct->min_cpu = *uint32;
		break;
	case JOBACCT_DATA_MIN_CPU_ID:
		jobacct->min_cpu_id = *jobacct_id;
		break;
	case JOBACCT_DATA_TOT_CPU:
		jobacct->tot_cpu = *uint32;
		break;
	default:
		debug("jobacct_g_set_setinfo data_type %d invalid", 
		      type);
	}
	slurm_mutex_unlock(&jobacct_lock);
	return rc;
rwfail:
	slurm_mutex_unlock(&jobacct_lock);
	return SLURM_ERROR;
	
}

extern int jobacct_common_getinfo(struct jobacctinfo *jobacct, 
				  enum jobacct_data_type type, void *data)
{
	int rc = SLURM_SUCCESS;
	int *fd = (int *)data;
	uint32_t *uint32 = (uint32_t *) data;
	jobacct_id_t *jobacct_id = (jobacct_id_t *) data;
	struct rusage *rusage = (struct rusage *)data;
	struct jobacctinfo *send = (struct jobacctinfo *) data;

	slurm_mutex_lock(&jobacct_lock);
	switch (type) {
	case JOBACCT_DATA_TOTAL:
		memcpy(send, jobacct, sizeof(struct jobacctinfo));
		break;
	case JOBACCT_DATA_PIPE:
		safe_read(*fd, jobacct, sizeof(struct jobacctinfo));
		break;
	case JOBACCT_DATA_RUSAGE:
		memset(rusage, 0, sizeof(struct rusage));
		rusage->ru_utime.tv_sec = jobacct->user_cpu_sec;
		rusage->ru_utime.tv_usec = jobacct->user_cpu_usec;
		rusage->ru_stime.tv_sec = jobacct->sys_cpu_sec;
		rusage->ru_stime.tv_usec = jobacct->sys_cpu_usec;
		break;
	case JOBACCT_DATA_MAX_RSS:
		*uint32 = jobacct->max_rss;
		break;
	case JOBACCT_DATA_MAX_RSS_ID:
		*jobacct_id = jobacct->max_rss_id;
		break;
	case JOBACCT_DATA_TOT_RSS:
		*uint32 = jobacct->tot_rss;
		break;
	case JOBACCT_DATA_MAX_VSIZE:
		*uint32 = jobacct->max_vsize;
		break;
	case JOBACCT_DATA_MAX_VSIZE_ID:
		*jobacct_id = jobacct->max_vsize_id;
		break;
	case JOBACCT_DATA_TOT_VSIZE:
		*uint32 = jobacct->tot_vsize;
		break;
	case JOBACCT_DATA_MAX_PAGES:
		*uint32 = jobacct->max_pages;
		break;
	case JOBACCT_DATA_MAX_PAGES_ID:
		*jobacct_id = jobacct->max_pages_id;
		break;
	case JOBACCT_DATA_TOT_PAGES:
		*uint32 = jobacct->tot_pages;
		break;
	case JOBACCT_DATA_MIN_CPU:
		*uint32 = jobacct->min_cpu;
		break;
	case JOBACCT_DATA_MIN_CPU_ID:
		*jobacct_id = jobacct->min_cpu_id;
		break;
	case JOBACCT_DATA_TOT_CPU:
		*uint32 = jobacct->tot_cpu;
		break;
	default:
		debug("jobacct_g_set_setinfo data_type %d invalid", 
		      type);
	}
	slurm_mutex_unlock(&jobacct_lock);
	return rc;
rwfail:
	slurm_mutex_unlock(&jobacct_lock);
	return SLURM_ERROR;

}

extern void jobacct_common_aggregate(struct jobacctinfo *dest, 
			     struct jobacctinfo *from)
{
	xassert(dest);
	xassert(from);

	slurm_mutex_lock(&jobacct_lock);
	if(dest->max_vsize < from->max_vsize) {
		dest->max_vsize = from->max_vsize;
		dest->max_vsize_id = from->max_vsize_id;
	}
	dest->tot_vsize += from->tot_vsize;
	
	if(dest->max_rss < from->max_rss) {
		dest->max_rss = from->max_rss;
		dest->max_rss_id = from->max_rss_id;
	}
	dest->tot_rss += from->tot_rss;
	
	if(dest->max_pages < from->max_pages) {
		dest->max_pages = from->max_pages;
		dest->max_pages_id = from->max_pages_id;
	}
	dest->tot_pages += from->tot_pages;
	if((dest->min_cpu > from->min_cpu) 
	   || (dest->min_cpu == (uint32_t)NO_VAL)) {
		if(from->min_cpu == (uint32_t)NO_VAL)
			from->min_cpu = 0;
		dest->min_cpu = from->min_cpu;
		dest->min_cpu_id = from->min_cpu_id;
	}
	dest->tot_cpu += from->tot_cpu;
		
	if(dest->max_vsize_id.taskid == (uint16_t)NO_VAL)
		dest->max_vsize_id = from->max_vsize_id;

	if(dest->max_rss_id.taskid == (uint16_t)NO_VAL)
		dest->max_rss_id = from->max_rss_id;

	if(dest->max_pages_id.taskid == (uint16_t)NO_VAL)
		dest->max_pages_id = from->max_pages_id;

	if(dest->min_cpu_id.taskid == (uint16_t)NO_VAL)
		dest->min_cpu_id = from->min_cpu_id;

	dest->user_cpu_sec	+= from->user_cpu_sec;
	dest->user_cpu_usec	+= from->user_cpu_usec;
	while (dest->user_cpu_usec >= 1E6) {
		dest->user_cpu_sec++;
		dest->user_cpu_usec -= 1E6;
	}
	dest->sys_cpu_sec	+= from->sys_cpu_sec;
	dest->sys_cpu_usec	+= from->sys_cpu_usec;
	while (dest->sys_cpu_usec >= 1E6) {
		dest->sys_cpu_sec++;
		dest->sys_cpu_usec -= 1E6;
	}

	slurm_mutex_unlock(&jobacct_lock);	
}

extern void jobacct_common_2_sacct(sacct_t *sacct, struct jobacctinfo *jobacct)
{
	xassert(jobacct);
	xassert(sacct);
	slurm_mutex_lock(&jobacct_lock);
	sacct->max_vsize = jobacct->max_vsize;
	sacct->max_vsize_id = jobacct->max_vsize_id;
	sacct->ave_vsize = jobacct->tot_vsize;
	sacct->max_rss = jobacct->max_rss;
	sacct->max_rss_id = jobacct->max_rss_id;
	sacct->ave_rss = jobacct->tot_rss;
	sacct->max_pages = jobacct->max_pages;
	sacct->max_pages_id = jobacct->max_pages_id;
	sacct->ave_pages = jobacct->tot_pages;
	sacct->min_cpu = jobacct->min_cpu;
	sacct->min_cpu_id = jobacct->min_cpu_id;
	sacct->ave_cpu = jobacct->tot_cpu;
	slurm_mutex_unlock(&jobacct_lock);
}

extern void jobacct_common_pack(struct jobacctinfo *jobacct, Buf buffer)
{
	int i=0;

	if(!jobacct) {
		for(i=0; i<16; i++)
			pack32((uint32_t) 0, buffer);
		for(i=0; i<4; i++)
			pack16((uint16_t) 0, buffer);
		return;
	} 
	slurm_mutex_lock(&jobacct_lock);
	pack32((uint32_t)jobacct->user_cpu_sec, buffer);
	pack32((uint32_t)jobacct->user_cpu_usec, buffer);
	pack32((uint32_t)jobacct->sys_cpu_sec, buffer);
	pack32((uint32_t)jobacct->sys_cpu_usec, buffer);
	pack32((uint32_t)jobacct->max_vsize, buffer);
	pack32((uint32_t)jobacct->tot_vsize, buffer);
	pack32((uint32_t)jobacct->max_rss, buffer);
	pack32((uint32_t)jobacct->tot_rss, buffer);
	pack32((uint32_t)jobacct->max_pages, buffer);
	pack32((uint32_t)jobacct->tot_pages, buffer);
	pack32((uint32_t)jobacct->min_cpu, buffer);
	pack32((uint32_t)jobacct->tot_cpu, buffer);
	_pack_jobacct_id(&jobacct->max_vsize_id, buffer);
	_pack_jobacct_id(&jobacct->max_rss_id, buffer);
	_pack_jobacct_id(&jobacct->max_pages_id, buffer);
	_pack_jobacct_id(&jobacct->min_cpu_id, buffer);
	slurm_mutex_unlock(&jobacct_lock);
}

/* you need to xfree this */
extern int jobacct_common_unpack(struct jobacctinfo **jobacct, Buf buffer)
{
	uint32_t uint32_tmp;
	*jobacct = xmalloc(sizeof(struct jobacctinfo));
	safe_unpack32(&uint32_tmp, buffer);
	(*jobacct)->user_cpu_sec = uint32_tmp;
	safe_unpack32(&uint32_tmp, buffer);
	(*jobacct)->user_cpu_usec = uint32_tmp;
	safe_unpack32(&uint32_tmp, buffer);
	(*jobacct)->sys_cpu_sec = uint32_tmp;
	safe_unpack32(&uint32_tmp, buffer);
	(*jobacct)->sys_cpu_usec = uint32_tmp;
	safe_unpack32(&(*jobacct)->max_vsize, buffer);
	safe_unpack32(&(*jobacct)->tot_vsize, buffer);
	safe_unpack32(&(*jobacct)->max_rss, buffer);
	safe_unpack32(&(*jobacct)->tot_rss, buffer);
	safe_unpack32(&(*jobacct)->max_pages, buffer);
	safe_unpack32(&(*jobacct)->tot_pages, buffer);
	safe_unpack32(&(*jobacct)->min_cpu, buffer);
	safe_unpack32(&(*jobacct)->tot_cpu, buffer);
	if(_unpack_jobacct_id(&(*jobacct)->max_vsize_id, buffer) 
	   != SLURM_SUCCESS)
		goto unpack_error;
	if(_unpack_jobacct_id(&(*jobacct)->max_rss_id, buffer)
	   != SLURM_SUCCESS)
		goto unpack_error;
	if(_unpack_jobacct_id(&(*jobacct)->max_pages_id, buffer)
	   != SLURM_SUCCESS)
		goto unpack_error;
	if(_unpack_jobacct_id(&(*jobacct)->min_cpu_id, buffer)
	   != SLURM_SUCCESS)
		goto unpack_error;

	return SLURM_SUCCESS;

unpack_error:
	xfree(*jobacct);
       	return SLURM_ERROR;
}

extern int jobacct_common_set_proctrack_container_id(uint32_t id)
{
	if(pgid_plugin)
		return SLURM_SUCCESS;

	if(cont_id != (uint32_t)NO_VAL) 
		info("Warning: jobacct: set_proctrack_container_id: "
		     "cont_id is already set to %d you are setting it to %d",
		     cont_id, id);
	if(id <= 0) {
		error("jobacct: set_proctrack_container_id: "
		      "I was given most likely an unset cont_id %d",
		      id);
		return SLURM_ERROR;
	}
	cont_id = id;

	return SLURM_SUCCESS;
}

extern int jobacct_common_set_mem_limit(uint32_t job_id, uint32_t mem_limit)
{
	if ((job_id == 0) || (mem_limit == 0)) {
		error("jobacct_common_set_mem_limit: jobid:%u mem_limit:%u",
		      job_id, mem_limit);
		return SLURM_ERROR;
	}

	acct_job_id   = job_id;
	job_mem_limit = mem_limit * 1024;	/* MB to KB */
	return SLURM_SUCCESS;
}

extern int jobacct_common_add_task(pid_t pid, jobacct_id_t *jobacct_id)
{
	struct jobacctinfo *jobacct = jobacct_common_alloc_jobacct(jobacct_id);
	
	slurm_mutex_lock(&jobacct_lock);
	if(pid <= 0) {
		error("invalid pid given (%d) for task acct", pid);
		goto error;
	} else if (!task_list) {
		error("no task list created!");
		goto error;
	}

	jobacct->pid = pid;
	jobacct->min_cpu = 0;
	debug2("adding task %u pid %d on node %u to jobacct", 
	       jobacct_id->taskid, pid, jobacct_id->nodeid);
	list_push(task_list, jobacct);
	slurm_mutex_unlock(&jobacct_lock);

	return SLURM_SUCCESS;
error:
	slurm_mutex_unlock(&jobacct_lock);
	jobacct_common_free_jobacct(jobacct);
	return SLURM_ERROR;
}

extern struct jobacctinfo *jobacct_common_stat_task(pid_t pid)
{
	struct jobacctinfo *jobacct = NULL;
	struct jobacctinfo *ret_jobacct = NULL;
	ListIterator itr = NULL;
	
	slurm_mutex_lock(&jobacct_lock);
	if (!task_list) {
		error("no task list created!");
		goto error;
	}

	itr = list_iterator_create(task_list);
	while((jobacct = list_next(itr))) { 
		if(jobacct->pid == pid)
			break;
	}
	list_iterator_destroy(itr);
	if (jobacct == NULL)
		goto error;
	ret_jobacct = xmalloc(sizeof(struct jobacctinfo));
	memcpy(ret_jobacct, jobacct, sizeof(struct jobacctinfo));
error:
	slurm_mutex_unlock(&jobacct_lock);
	return ret_jobacct;
}

extern struct jobacctinfo *jobacct_common_remove_task(pid_t pid)
{
	struct jobacctinfo *jobacct = NULL;

	ListIterator itr = NULL;

	slurm_mutex_lock(&jobacct_lock);
	if (!task_list) {
		error("no task list created!");
		goto error;
	}

	itr = list_iterator_create(task_list);
	while((jobacct = list_next(itr))) { 
		if(jobacct->pid == pid) {
			list_remove(itr);
			break;
		}
	}
	list_iterator_destroy(itr);
	if(jobacct) {
		debug2("removing task %u pid %d from jobacct", 
		       jobacct->max_vsize_id.taskid, jobacct->pid);
	} else {
		error("pid(%d) not being watched in jobacct!", pid);
	}
error:
	slurm_mutex_unlock(&jobacct_lock);
	return jobacct;
}

extern int jobacct_common_endpoll()
{
	jobacct_shutdown = true;

	return SLURM_SUCCESS;
}

extern void jobacct_common_suspend_poll()
{
	jobacct_suspended = true;
}

extern void jobacct_common_resume_poll()
{
	jobacct_suspended = false;
}

