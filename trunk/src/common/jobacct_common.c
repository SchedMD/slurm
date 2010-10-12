/*****************************************************************************\
 *  jobacct_common.c - common functions for almost all jobacct plugins.
 *****************************************************************************
 *
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
 *  Written by Danny Auble, <da@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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
 *
 *  This file is patterned after jobcomp_linux.c, written by Morris Jette and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/

#include "jobacct_common.h"

/*
** Define slurm-specific aliases for use by plugins, see slurm_xlator.h
** for details.
 */
strong_alias(jobacct_common_unpack, slurm_jobacct_common_unpack);
strong_alias(jobacct_common_free_jobacct, slurm_jobacct_common_free_jobacct);

pthread_mutex_t jobacct_lock = PTHREAD_MUTEX_INITIALIZER;
uint32_t jobacct_job_id     = 0;
uint32_t jobacct_step_id    = 0;
uint32_t jobacct_mem_limit  = 0;
uint32_t jobacct_vmem_limit = 0;
uint32_t mult = 1000;

static void _pack_jobacct_id(jobacct_id_t *jobacct_id,
			     uint16_t rpc_version, Buf buffer)
{
	pack32((uint32_t)jobacct_id->nodeid, buffer);
	pack16((uint16_t)jobacct_id->taskid, buffer);
}

static int _unpack_jobacct_id(jobacct_id_t *jobacct_id,
			      uint16_t rpc_version, Buf buffer)
{
	safe_unpack32(&jobacct_id->nodeid, buffer);
	safe_unpack16(&jobacct_id->taskid, buffer);
	return SLURM_SUCCESS;
unpack_error:
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
		debug("jobacct_g_set_setinfo data_type %d invalid", type);
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
		debug("jobacct_g_set_setinfo data_type %d invalid", type);
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

extern void jobacct_common_2_stats(slurmdb_stats_t *stats,
				   struct jobacctinfo *jobacct)
{
	xassert(jobacct);
	xassert(stats);
	slurm_mutex_lock(&jobacct_lock);
	stats->vsize_max = jobacct->max_vsize;
	stats->vsize_max_nodeid = jobacct->max_vsize_id.nodeid;
	stats->vsize_max_taskid = jobacct->max_vsize_id.taskid;
	stats->vsize_ave = (double)jobacct->tot_vsize;
	stats->rss_max = jobacct->max_rss;
	stats->rss_max_nodeid = jobacct->max_rss_id.nodeid;
	stats->rss_max_taskid = jobacct->max_rss_id.taskid;
	stats->rss_ave = (double)jobacct->tot_rss;
	stats->pages_max = jobacct->max_pages;
	stats->pages_max_nodeid = jobacct->max_pages_id.nodeid;
	stats->pages_max_taskid = jobacct->max_pages_id.taskid;
	stats->pages_ave = (double)jobacct->tot_pages;
	stats->cpu_min = jobacct->min_cpu;
	stats->cpu_min_nodeid = jobacct->min_cpu_id.nodeid;
	stats->cpu_min_taskid = jobacct->min_cpu_id.taskid;
	stats->cpu_ave = (double)jobacct->tot_cpu;
	slurm_mutex_unlock(&jobacct_lock);
}

extern void jobacct_common_pack(struct jobacctinfo *jobacct,
				uint16_t rpc_version, Buf buffer)
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
	_pack_jobacct_id(&jobacct->max_vsize_id, rpc_version, buffer);
	_pack_jobacct_id(&jobacct->max_rss_id, rpc_version, buffer);
	_pack_jobacct_id(&jobacct->max_pages_id, rpc_version, buffer);
	_pack_jobacct_id(&jobacct->min_cpu_id, rpc_version, buffer);
	slurm_mutex_unlock(&jobacct_lock);
}

/* you need to xfree this */
extern int jobacct_common_unpack(struct jobacctinfo **jobacct,
				 uint16_t rpc_version, Buf buffer)
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
	if(_unpack_jobacct_id(&(*jobacct)->max_vsize_id, rpc_version, buffer)
	   != SLURM_SUCCESS)
		goto unpack_error;
	if(_unpack_jobacct_id(&(*jobacct)->max_rss_id, rpc_version, buffer)
	   != SLURM_SUCCESS)
		goto unpack_error;
	if(_unpack_jobacct_id(&(*jobacct)->max_pages_id, rpc_version, buffer)
	   != SLURM_SUCCESS)
		goto unpack_error;
	if(_unpack_jobacct_id(&(*jobacct)->min_cpu_id, rpc_version, buffer)
	   != SLURM_SUCCESS)
		goto unpack_error;

	return SLURM_SUCCESS;

unpack_error:
	xfree(*jobacct);
       	return SLURM_ERROR;
}

extern int jobacct_common_set_mem_limit(uint32_t job_id, uint32_t step_id,
					uint32_t mem_limit)
{
	if ((job_id == 0) || (mem_limit == 0)) {
		error("jobacct_common_set_mem_limit: jobid:%u mem_limit:%u",
		      job_id, mem_limit);
		return SLURM_ERROR;
	}

	jobacct_job_id      = job_id;
	jobacct_step_id     = step_id;
	jobacct_mem_limit   = mem_limit * 1024;	/* MB to KB */
	jobacct_vmem_limit  = jobacct_mem_limit;
	jobacct_vmem_limit *= (slurm_get_vsize_factor() / 100.0);
	return SLURM_SUCCESS;
}

extern int jobacct_common_add_task(pid_t pid, jobacct_id_t *jobacct_id,
				   List task_list)
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

extern struct jobacctinfo *jobacct_common_stat_task(pid_t pid, List task_list)
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

extern struct jobacctinfo *jobacct_common_remove_task(pid_t pid, List task_list)
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
		debug2("pid(%d) not being watched in jobacct!", pid);
	}
error:
	slurm_mutex_unlock(&jobacct_lock);
	return jobacct;
}

