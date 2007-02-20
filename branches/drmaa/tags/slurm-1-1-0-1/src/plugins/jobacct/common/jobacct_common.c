/*****************************************************************************\
 *  jobacct_common.c - common functions for almost all jobacct plugins.
 *****************************************************************************
 *
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
 *  Written by Danny Auble, <da@llnl.gov>
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
 *
 *  This file is patterned after jobcomp_linux.c, written by Morris Jette and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/

#include "jobacct_common.h"

extern int common_init_struct(struct jobacctinfo *jobacct, uint16_t tid)
{
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
	jobacct->max_vsize_task = tid;
	jobacct->tot_vsize = 0;
	jobacct->max_rss = 0;
	jobacct->max_rss_task = tid;
	jobacct->tot_rss = 0;
	jobacct->max_pages = 0;
	jobacct->max_pages_task = tid;
	jobacct->tot_pages = 0;
	jobacct->min_cpu = (uint32_t)NO_VAL;
	jobacct->min_cpu_task = tid;
	jobacct->tot_cpu = 0;
	
	return SLURM_SUCCESS;
}

extern struct jobacctinfo *common_alloc_jobacct(uint32_t tid)
{
	struct jobacctinfo *jobacct = xmalloc(sizeof(struct jobacctinfo));
	common_init_struct(jobacct, tid);
	return jobacct;
}

extern void common_free_jobacct(void *object)
{
	struct jobacctinfo *jobacct = (struct jobacctinfo *)object;
	xfree(jobacct);
}

extern int common_setinfo(struct jobacctinfo *jobacct, 
			  enum jobacct_data_type type, void *data)
{
	int rc = SLURM_SUCCESS;
	int *fd = (int *)data;
	uint32_t *uint32 = (uint32_t *) data;
	uint16_t *uint16 = (uint16_t *) data;
	struct rusage *rusage = (struct rusage *) data;
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
		memcpy(&jobacct->rusage, rusage, sizeof(struct rusage));
		break;
	case JOBACCT_DATA_MAX_RSS:
		jobacct->max_rss = *uint32;
		break;
	case JOBACCT_DATA_MAX_RSS_TASK:
		jobacct->max_rss_task = *uint16;
		break;
	case JOBACCT_DATA_TOT_RSS:
		jobacct->tot_rss = *uint32;
		break;
	case JOBACCT_DATA_MAX_VSIZE:
		jobacct->max_vsize = *uint32;
		break;
	case JOBACCT_DATA_MAX_VSIZE_TASK:
		jobacct->max_vsize_task = *uint16;
		break;
	case JOBACCT_DATA_TOT_VSIZE:
		jobacct->tot_vsize = *uint32;
		break;
	case JOBACCT_DATA_MAX_PAGES:
		jobacct->max_pages = *uint32;
		break;
	case JOBACCT_DATA_MAX_PAGES_TASK:
		jobacct->max_pages_task = *uint16;
		break;
	case JOBACCT_DATA_TOT_PAGES:
		jobacct->tot_pages = *uint32;
		break;
	case JOBACCT_DATA_MIN_CPU:
		jobacct->min_cpu = *uint32;
		break;
	case JOBACCT_DATA_MIN_CPU_TASK:
		jobacct->min_cpu_task = *uint16;
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

extern int common_getinfo(struct jobacctinfo *jobacct, 
			  enum jobacct_data_type type, void *data)
{
	int rc = SLURM_SUCCESS;
	int *fd = (int *)data;
	uint32_t *uint32 = (uint32_t *) data;
	uint16_t *uint16 = (uint16_t *) data;
	struct rusage *rusage = (struct rusage *) data;
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
		memcpy(rusage, &jobacct->rusage, sizeof(struct rusage));
		break;
	case JOBACCT_DATA_MAX_RSS:
		*uint32 = jobacct->max_rss;
		break;
	case JOBACCT_DATA_MAX_RSS_TASK:
		*uint16 = jobacct->max_rss_task;
		break;
	case JOBACCT_DATA_TOT_RSS:
		*uint32 = jobacct->tot_rss;
		break;
	case JOBACCT_DATA_MAX_VSIZE:
		*uint32 = jobacct->max_vsize;
		break;
	case JOBACCT_DATA_MAX_VSIZE_TASK:
		*uint16 = jobacct->max_vsize_task;
		break;
	case JOBACCT_DATA_TOT_VSIZE:
		*uint32 = jobacct->tot_vsize;
		break;
	case JOBACCT_DATA_MAX_PAGES:
		*uint32 = jobacct->max_pages;
		break;
	case JOBACCT_DATA_MAX_PAGES_TASK:
		*uint16 = jobacct->max_pages_task;
		break;
	case JOBACCT_DATA_TOT_PAGES:
		*uint32 = jobacct->tot_pages;
		break;
	case JOBACCT_DATA_MIN_CPU:
		*uint32 = jobacct->min_cpu;
		break;
	case JOBACCT_DATA_MIN_CPU_TASK:
		*uint16 = jobacct->min_cpu_task;
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

extern void common_aggregate(struct jobacctinfo *dest, 
			     struct jobacctinfo *from)
{
	xassert(dest);
	xassert(from);

	slurm_mutex_lock(&jobacct_lock);
	if(dest->max_vsize < from->max_vsize) {
		dest->max_vsize = from->max_vsize;
		dest->max_vsize_task = from->max_vsize_task;
	}
	dest->tot_vsize += from->tot_vsize;
	
	if(dest->max_rss < from->max_rss) {
		dest->max_rss = from->max_rss;
		dest->max_rss_task = from->max_rss_task;
	}
	dest->tot_rss += from->tot_rss;
	
	if(dest->max_pages < from->max_pages) {
		dest->max_pages = from->max_pages;
		dest->max_pages_task = from->max_pages_task;
	}
	dest->tot_pages += from->tot_pages;
	if((dest->min_cpu > from->min_cpu) 
	   || (dest->min_cpu == (uint32_t)NO_VAL)) {
		if(from->min_cpu == (uint32_t)NO_VAL)
			from->min_cpu = 0;
		dest->min_cpu = from->min_cpu;
		dest->min_cpu_task = from->min_cpu_task;
	}
	dest->tot_cpu += from->tot_cpu;
		
	if(dest->max_vsize_task == (uint16_t)NO_VAL)
		dest->max_vsize_task = from->max_vsize_task;

	if(dest->max_rss_task == (uint16_t)NO_VAL)
		dest->max_rss_task = from->max_rss_task;

	if(dest->max_pages_task == (uint16_t)NO_VAL)
		dest->max_pages_task = from->max_pages_task;

	if(dest->min_cpu_task == (uint16_t)NO_VAL)
		dest->min_cpu_task = from->min_cpu_task;

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

extern void common_2_sacct(sacct_t *sacct, struct jobacctinfo *jobacct)
{
	xassert(jobacct);
	xassert(sacct);
	slurm_mutex_lock(&jobacct_lock);
	sacct->max_vsize = jobacct->max_vsize;
	sacct->max_vsize_task = jobacct->max_vsize_task;
	sacct->ave_vsize = jobacct->tot_vsize;
	sacct->max_rss = jobacct->max_rss;
	sacct->max_rss_task = jobacct->max_rss_task;
	sacct->ave_rss = jobacct->tot_rss;
	sacct->max_pages = jobacct->max_pages;
	sacct->max_pages_task = jobacct->max_pages_task;
	sacct->ave_pages = jobacct->tot_pages;
	sacct->min_cpu = jobacct->min_cpu;
	sacct->min_cpu_task = jobacct->min_cpu_task;
	sacct->ave_cpu = jobacct->tot_cpu;
	slurm_mutex_unlock(&jobacct_lock);
}

extern void common_pack(struct jobacctinfo *jobacct, Buf buffer)
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
	pack16((uint16_t)jobacct->max_vsize_task, buffer);
	pack16((uint16_t)jobacct->max_rss_task, buffer);
	pack16((uint16_t)jobacct->max_pages_task, buffer);
	pack16((uint16_t)jobacct->min_cpu_task, buffer);
	slurm_mutex_unlock(&jobacct_lock);
}

/* you need to xfree this */
extern int common_unpack(struct jobacctinfo **jobacct, Buf buffer)
{
	uint32_t uint32_tmp;
	*jobacct = xmalloc(sizeof(struct jobacctinfo));
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
	safe_unpack16(&(*jobacct)->max_vsize_task, buffer);
	safe_unpack16(&(*jobacct)->max_rss_task, buffer);
	safe_unpack16(&(*jobacct)->max_pages_task, buffer);
	safe_unpack16(&(*jobacct)->min_cpu_task, buffer);
	return SLURM_SUCCESS;

      unpack_error:
	xfree(*jobacct);
       	return SLURM_ERROR;
}
