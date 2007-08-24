/*****************************************************************************\
 *  jobacct_common.c - implementation-independent job accounting logging 
 *  functions
 *****************************************************************************
 *  Copyright (C) 2003 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jay Windley <jwindley@lnxi.com>, Morris Jette <jette1@llnl.com>
 *  UCRL-CODE-226842.
 *  
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
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

#include "src/common/jobacct_common.h"

List task_list = NULL;
pthread_mutex_t jobacct_lock = PTHREAD_MUTEX_INITIALIZER;
uint32_t cont_id = (uint32_t)NO_VAL;
bool pgid_plugin = false;

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

extern jobacct_job_rec_t *jobacct_init_job_rec(jobacct_header_t header)
{
	jobacct_job_rec_t *job = xmalloc(sizeof(jobacct_job_rec_t));
	memcpy(&job->header, &header, sizeof(jobacct_header_t));
	memset(&job->rusage, 0, sizeof(struct rusage));
	memset(&job->sacct, 0, sizeof(sacct_t));
	job->sacct.min_cpu = (float)NO_VAL;
	job->job_start_seen = 0;
	job->job_step_seen = 0;
	job->job_terminated_seen = 0;
	job->jobnum_superseded = 0;
	job->jobname = NULL;
	job->status = JOB_PENDING;
	job->nodes = NULL;
	job->jobname = NULL;
	job->exitcode = 0;
	job->priority = 0;
	job->ntasks = 0;
	job->ncpus = 0;
	job->elapsed = 0;
	job->tot_cpu_sec = 0;
	job->tot_cpu_usec = 0;
	job->steps = list_create(jobacct_destroy_step);
	job->nodes = NULL;
	job->track_steps = 0;
	job->account = NULL;
	job->requid = -1;

      	return job;
}

extern jobacct_step_rec_t *jobacct_init_step_rec(jobacct_header_t header)
{
	jobacct_step_rec_t *step = xmalloc(sizeof(jobacct_job_rec_t));
	memcpy(&step->header, &header, sizeof(jobacct_header_t));
	memset(&step->rusage, 0, sizeof(struct rusage));
	memset(&step->sacct, 0, sizeof(sacct_t));
	step->stepnum = (uint32_t)NO_VAL;
	step->nodes = NULL;
	step->stepname = NULL;
	step->status = NO_VAL;
	step->exitcode = NO_VAL;
	step->ntasks = (uint32_t)NO_VAL;
	step->ncpus = (uint32_t)NO_VAL;
	step->elapsed = (uint32_t)NO_VAL;
	step->tot_cpu_sec = (uint32_t)NO_VAL;
	step->tot_cpu_usec = (uint32_t)NO_VAL;
	step->account = NULL;
	step->requid = -1;

	return step;
}

extern jobacctinfo_t *create_jobacctinfo(jobacct_id_t *jobacct_id);
{
	jobacctinfo_t *jobacct = xmalloc(sizeof(jobacctinfo_t));
	
	if(!jobacct_id) {
		jobacct_id_t temp_id;
		temp_id.taskid = (uint16_t)NO_VAL;
		temp_id.nodeid = (uint32_t)NO_VAL;
		jobacct_id = &temp_id;
	}

	jobacct->rusage.ru_utime.tv_sec = 0;
	jobacct->rusage.ru_utime.tv_usec = 0;
	jobacct->rusage.ru_stime.tv_sec = 0;
	jobacct->rusage.ru_stime.tv_usec = 0;
	jobacct->rusage.ru_maxrss = 0;
	jobacct->rusage.ru_ixrss = 0;
	jobacct->rusage.ru_idrss = 0;
	jobacct->rusage.ru_isrss = 0;
	jobacct->rusage.ru_minflt = 0;
	jobacct->rusage.ru_majflt = 0;
	jobacct->rusage.ru_nswap = 0;
	jobacct->rusage.ru_inblock = 0;
	jobacct->rusage.ru_oublock = 0;
	jobacct->rusage.ru_msgsnd = 0;
	jobacct->rusage.ru_msgrcv = 0;
	jobacct->rusage.ru_nsignals = 0;
	jobacct->rusage.ru_nvcsw = 0;
	jobacct->rusage.ru_nivcsw = 0;

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

	return jobacct;
}

extern void free_jobacct_header(void *object)
{
	if(header) {
		xfree(header->partition);
		xfree(header->blockid);
	}
}

extern void destroy_jobacct_job_rec(void *object)
{
	jobacct_job_rec_t *job = (jobacct_job_rec_t *)object;
	if (job) {
		if(job->steps)
			list_destroy(job->steps);
		jobacct_destroy_header(&job->header);
		xfree(job->jobname);
		xfree(job->account);
		xfree(job->nodes);
		xfree(job);
	}
}

extern void destroy_jobacct_step_rec(void *object)
{
	jobacct_step_rec_t *step = (jobacct_step_rec_t *)object;
	if (step) {
		jobacct_destroy_header(&step->header);
		xfree(step->stepname);
		xfree(step->nodes);
		xfree(step->account);
		xfree(step);
	}
}

extern void destroy_jobacctinfo(jobacctinfo_t *jobacct);
{
	xfree(jobacct);
	jobacct = NULL;
}

extern void aggregate_jobacctinfo(jobacctinfo_t *dest, jobacctinfo_t *from);
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

	/* sum up all rusage stuff */
	dest->rusage.ru_utime.tv_sec	+= from->rusage.ru_utime.tv_sec;
	dest->rusage.ru_utime.tv_usec	+= from->rusage.ru_utime.tv_usec;
	while (dest->rusage.ru_utime.tv_usec >= 1E6) {
		dest->rusage.ru_utime.tv_sec++;
		dest->rusage.ru_utime.tv_usec -= 1E6;
	}
	dest->rusage.ru_stime.tv_sec	+= from->rusage.ru_stime.tv_sec;
	dest->rusage.ru_stime.tv_usec	+= from->rusage.ru_stime.tv_usec;
	while (dest->rusage.ru_stime.tv_usec >= 1E6) {
		dest->rusage.ru_stime.tv_sec++;
		dest->rusage.ru_stime.tv_usec -= 1E6;
	}

	dest->rusage.ru_maxrss		+= from->rusage.ru_maxrss;
	dest->rusage.ru_ixrss		+= from->rusage.ru_ixrss;
	dest->rusage.ru_idrss		+= from->rusage.ru_idrss;
	dest->rusage.ru_isrss		+= from->rusage.ru_isrss;
	dest->rusage.ru_minflt		+= from->rusage.ru_minflt;
	dest->rusage.ru_majflt		+= from->rusage.ru_majflt;
	dest->rusage.ru_nswap		+= from->rusage.ru_nswap;
	dest->rusage.ru_inblock		+= from->rusage.ru_inblock;
	dest->rusage.ru_oublock		+= from->rusage.ru_oublock;
	dest->rusage.ru_msgsnd		+= from->rusage.ru_msgsnd;
	dest->rusage.ru_msgrcv		+= from->rusage.ru_msgrcv;
	dest->rusage.ru_nsignals	+= from->rusage.ru_nsignals;
	dest->rusage.ru_nvcsw		+= from->rusage.ru_nvcsw;
	dest->rusage.ru_nivcsw		+= from->rusage.ru_nivcsw;
	slurm_mutex_unlock(&jobacct_lock);	
}

extern void pack_jobacctinfo(jobacctinfo_t *jobacct, Buf buffer);
{
	int i=0;

	if(!jobacct) {
		for(i=0; i<26; i++)
			pack32((uint32_t) 0, buffer);
		for(i=0; i<4; i++)
			pack16((uint16_t) 0, buffer);
		return;
	} 
	slurm_mutex_lock(&jobacct_lock);
	pack32((uint32_t)jobacct->rusage.ru_utime.tv_sec, buffer);
	pack32((uint32_t)jobacct->rusage.ru_utime.tv_usec, buffer);
	pack32((uint32_t)jobacct->rusage.ru_stime.tv_sec, buffer);
	pack32((uint32_t)jobacct->rusage.ru_stime.tv_usec, buffer);
	pack32((uint32_t)jobacct->rusage.ru_maxrss, buffer);
	pack32((uint32_t)jobacct->rusage.ru_ixrss, buffer);
	pack32((uint32_t)jobacct->rusage.ru_idrss, buffer);
	pack32((uint32_t)jobacct->rusage.ru_isrss, buffer);
	pack32((uint32_t)jobacct->rusage.ru_minflt, buffer);
	pack32((uint32_t)jobacct->rusage.ru_majflt, buffer);
	pack32((uint32_t)jobacct->rusage.ru_nswap, buffer);
	pack32((uint32_t)jobacct->rusage.ru_inblock, buffer);
	pack32((uint32_t)jobacct->rusage.ru_oublock, buffer);
	pack32((uint32_t)jobacct->rusage.ru_msgsnd, buffer);
	pack32((uint32_t)jobacct->rusage.ru_msgrcv, buffer);
	pack32((uint32_t)jobacct->rusage.ru_nsignals, buffer);
	pack32((uint32_t)jobacct->rusage.ru_nvcsw, buffer);
	pack32((uint32_t)jobacct->rusage.ru_nivcsw, buffer);
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
extern int unpack_jobacctinfo(jobacctinfo_t **jobacct, Buf buffer);
{
	uint32_t uint32_tmp;
	*jobacct = xmalloc(sizeof(jobacctinfo_t));
	safe_unpack32(&uint32_tmp, buffer);
	(*jobacct)->rusage.ru_utime.tv_sec = uint32_tmp;
	safe_unpack32(&uint32_tmp, buffer);
	(*jobacct)->rusage.ru_utime.tv_usec = uint32_tmp;
	safe_unpack32(&uint32_tmp, buffer);
	(*jobacct)->rusage.ru_stime.tv_sec = uint32_tmp;
	safe_unpack32(&uint32_tmp, buffer);
	(*jobacct)->rusage.ru_stime.tv_usec = uint32_tmp;
	safe_unpack32(&uint32_tmp, buffer);
	(*jobacct)->rusage.ru_maxrss = uint32_tmp;
	safe_unpack32(&uint32_tmp, buffer);
	(*jobacct)->rusage.ru_ixrss = uint32_tmp;
	safe_unpack32(&uint32_tmp, buffer);
	(*jobacct)->rusage.ru_idrss = uint32_tmp;
	safe_unpack32(&uint32_tmp, buffer);
	(*jobacct)->rusage.ru_isrss = uint32_tmp;
	safe_unpack32(&uint32_tmp, buffer);
	(*jobacct)->rusage.ru_minflt = uint32_tmp;
	safe_unpack32(&uint32_tmp, buffer);
	(*jobacct)->rusage.ru_majflt = uint32_tmp;
	safe_unpack32(&uint32_tmp, buffer);
	(*jobacct)->rusage.ru_nswap = uint32_tmp;
	safe_unpack32(&uint32_tmp, buffer);
	(*jobacct)->rusage.ru_inblock = uint32_tmp;
	safe_unpack32(&uint32_tmp, buffer);
	(*jobacct)->rusage.ru_oublock = uint32_tmp;
	safe_unpack32(&uint32_tmp, buffer);
	(*jobacct)->rusage.ru_msgsnd = uint32_tmp;
	safe_unpack32(&uint32_tmp, buffer);
	(*jobacct)->rusage.ru_msgrcv = uint32_tmp;
	safe_unpack32(&uint32_tmp, buffer);
	(*jobacct)->rusage.ru_nsignals = uint32_tmp;
	safe_unpack32(&uint32_tmp, buffer);
	(*jobacct)->rusage.ru_nvcsw = uint32_tmp;
	safe_unpack32(&uint32_tmp, buffer);
	(*jobacct)->rusage.ru_nivcsw = uint32_tmp;
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

extern int jobacct_set_proctrack_container_id(uint32_t id)
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

extern int jobacct_add_task(pid_t pid, jobacct_id_t *jobacct_id)
{
	jobacctinfo_t *jobacct = create_jobacctinfo(jobacct_id);
	
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
	destroy_jobacctinfo(jobacct);
	return SLURM_ERROR;
}

extern jobacctinfo_t *jobacct_stat_task(pid_t pid)
{
	jobacctinfo_t *jobacct = NULL;
	jobacctinfo_t *ret_jobacct = NULL;
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
	ret_jobacct = xmalloc(sizeof(jobacctinfo_t));
	memcpy(ret_jobacct, jobacct, sizeof(jobacctinfo_t));
error:
	slurm_mutex_unlock(&jobacct_lock);
	return ret_jobacct;
}

extern jobacctinfo_t *jobacct_remove_task(pid_t pid)
{
	jobacctinfo_t *jobacct = NULL;
	jobacctinfo_t *ret_jobacct = NULL;
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
		ret_jobacct = xmalloc(sizeof(jobacctinfo_t));
		memcpy(ret_jobacct, jobacct, sizeof(jobacctinfo_t));
		destroy_jobacctinfo(jobacct);
	} else {
		error("pid(%d) not being watched in jobacct!", pid);
	}
error:
	slurm_mutex_unlock(&jobacct_lock);
	return ret_jobacct;
}

