/*****************************************************************************\
 *  jobacct_common.c - common functions for almost all jobacct plugins.
 *****************************************************************************
 *
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
 *  Written by Danny Auble, <da@llnl.gov>
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
 *
 *  This file is patterned after jobcomp_linux.c, written by Morris Jette and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/

#include "jobacct_common.h"

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

extern int common_init_struct(struct jobacctinfo *jobacct, 
			      jobacct_id_t *jobacct_id)
{
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
	
	return SLURM_SUCCESS;
}

extern struct jobacctinfo *common_alloc_jobacct(jobacct_id_t *jobacct_id)
{
	struct jobacctinfo *jobacct = xmalloc(sizeof(struct jobacctinfo));
	common_init_struct(jobacct, jobacct_id);
	return jobacct;
}

extern void common_free_jobacct(void *object)
{
	struct jobacctinfo *jobacct = (struct jobacctinfo *)object;
	xfree(jobacct);
	jobacct = NULL;
}

extern int common_setinfo(struct jobacctinfo *jobacct, 
			  enum jobacct_data_type type, void *data)
{
	int rc = SLURM_SUCCESS;
	int *fd = (int *)data;
	uint32_t *uint32 = (uint32_t *) data;
	jobacct_id_t *jobacct_id = (jobacct_id_t *) data;
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

extern int common_getinfo(struct jobacctinfo *jobacct, 
			  enum jobacct_data_type type, void *data)
{
	int rc = SLURM_SUCCESS;
	int *fd = (int *)data;
	uint32_t *uint32 = (uint32_t *) data;
	jobacct_id_t *jobacct_id = (jobacct_id_t *) data;
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

extern void common_aggregate(struct jobacctinfo *dest, 
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
	_pack_jobacct_id(&jobacct->max_vsize_id, buffer);
	_pack_jobacct_id(&jobacct->max_rss_id, buffer);
	_pack_jobacct_id(&jobacct->max_pages_id, buffer);
	_pack_jobacct_id(&jobacct->min_cpu_id, buffer);
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
