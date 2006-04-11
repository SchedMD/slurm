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

extern int common_init_struct(struct jobacctinfo *jobacct)
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
	jobacct->max_psize = 0;
	
	return SLURM_SUCCESS;
}

extern struct jobacctinfo *common_alloc()
{
	struct jobacctinfo *jobacct = xmalloc(sizeof(struct jobacctinfo));
	return jobacct;
}

extern int common_free(struct jobacctinfo *jobacct)
{
	xfree(jobacct);
	return SLURM_SUCCESS;
}

extern int common_setinfo(struct jobacctinfo *jobacct, 
			  enum jobacct_data_type type, void *data)
{
	int rc = SLURM_SUCCESS;
	int *temp = (int *)data;
	uint32_t *uint32 = (uint32_t *) data;
	struct rusage *rusage = (struct rusage *) data;
	struct jobacctinfo *send = (struct jobacctinfo *) data;

	switch (type) {
	case JOBACCT_DATA_TOTAL:
		memcpy(jobacct, send, sizeof(struct jobacctinfo));
		break;
	case JOBACCT_DATA_PIPE:
		safe_write((int)*temp, jobacct, sizeof(struct jobacctinfo));
		break;
	case JOBACCT_DATA_RUSAGE:
		memcpy(&jobacct->rusage, rusage, sizeof(struct rusage));
		break;
	case JOBACCT_DATA_PSIZE:
		jobacct->max_psize = *uint32;
		break;
	case JOBACCT_DATA_VSIZE:
		jobacct->max_vsize = *uint32;
		break;
	default:
		debug("jobacct_g_set_setinfo data_type %d invalid", 
		      type);
	}

	return rc;
rwfail:
	return SLURM_ERROR;
	
}

extern int common_getinfo(struct jobacctinfo *jobacct, 
			  enum jobacct_data_type type, void *data)
{
	int rc = SLURM_SUCCESS;
	int *temp = (int *)data;
	uint32_t *uint32 = (uint32_t *) data;
	struct rusage *rusage = (struct rusage *) data;
	struct jobacctinfo *send = (struct jobacctinfo *) data;

	switch (type) {
	case JOBACCT_DATA_TOTAL:
		memcpy(send, jobacct, sizeof(struct jobacctinfo));
		break;
	case JOBACCT_DATA_PIPE:
		safe_read((int)*temp, jobacct, sizeof(struct jobacctinfo));
		break;
	case JOBACCT_DATA_RUSAGE:
		memcpy(rusage, &jobacct->rusage, sizeof(struct rusage));
		break;
	case JOBACCT_DATA_PSIZE:
		*uint32 = jobacct->max_psize;
		break;
	case JOBACCT_DATA_VSIZE:
		*uint32 = jobacct->max_vsize;
		break;
	default:
		debug("jobacct_g_set_setinfo data_type %d invalid", 
		      type);
	}

	return rc;
rwfail:
	return SLURM_ERROR;

}

extern void common_aggregate(struct jobacctinfo *dest, 
			     struct jobacctinfo *from)
{
	dest->max_psize = MAX(dest->max_psize, from->max_psize);
	dest->max_vsize = MAX(dest->max_vsize, from->max_vsize);
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
}

extern void common_pack(struct jobacctinfo *jobacct, Buf buffer)
{
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
	pack32((uint32_t)jobacct->max_psize, buffer);
}

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
	safe_unpack32(&(*jobacct)->max_psize, buffer);
	return SLURM_SUCCESS;

      unpack_error:
	xfree(*jobacct);
       	return SLURM_ERROR;
}
