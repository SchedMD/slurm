/*****************************************************************************\
 *  proctrack.h - Process tracking kernel extension definitions for AIX. 
 *  Keep track of process ancestry with respect to SLURM jobs.
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.gov>
 *  UCRL-CODE-2002-040.
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

#ifndef _PROCTRACK_H_
#define _PROCTRACK_H_

#include <sys/types.h>

/*
 * proctrack_job_reg_self - Associate the calling process with a
 *	job ID (pointed to by job_id_ptr).
 * RET 0 on success, -1 on error
 */
extern int proctrack_job_reg_self(int *job_id_ptr);

/*
 * proctrack_job_reg_pid - Associate a process id (pointed to by pid_ptr)
 *	with a job ID (pointed to by job_id_ptr).
 * RET 0 on success, -1 on error
 */
extern int proctrack_job_reg_pid(int *job_id_ptr, int *pid_ptr);

/*
 * proctrack_job_unreg - Unregister a job.
 * RET 0 on success, -1 on error (it is an error to unregister a job that still
 *  containes processes).
 */
extern int proctrack_job_unreg(int *job_id_ptr);

/*
 * proctrack_job_kill - Signal all processes (known pids) of a job
 * by sending it the signal specified.
 * RET 0 on success, -1 on error
 */
extern int proctrack_job_kill(int *job_id_ptr, int *signal_ptr);

/*
 * proctrack_get_job_id - Return the job id associated with a given process,
 *	if such an association exists.
 * RET the job id, or zero if the process does not exist or is not in a job.
 */
extern uint32_t proctrack_get_job_id(int *pid_ptr);

/*
 * proctrack_version - Return the version number of the
 *	proctrack kernel extension.
 */
extern uint32_t proctrack_version(void);

/*
 * proctrack_get_pids returns an array of process ids for the given
 * job_id.  The array of pids is returned in the array pointed to
 * by the pid_array_ptr parameter.  The caller is responsible for 
 * allocating and freeing the memory for the array pointed to by
 * pid_array_ptr.  pid_array_len is an integer representing
 * the number of pids that can be held by the pid_array_ptr array.
 * 
 * Upon successful completion, returns the the number of pids found in the
 * specified job.  Note that this number MAY be larger than the 
 * number pointed to by pid_array_len, in which case caller knows that
 * the pid_array_ptr array is truncated.  The caller will want to allocate
 * a longer array and try again.
 *
 * On error returns -1 and sets errno.
 */
extern int proctrack_get_pids(uint32_t job_id, int pid_array_len,
			      int32_t *pid_array_ptr);

/*
 * proctrack_get_all_pids returns two arrays.  The first array lists
 * every process that proctrack is currently tracking, and the second
 * array contains the job ID for each process.  The array of pids is
 * returned in the array pointed to by the pid_array_ptr parameter, and the
 * array of job IDs is returned in the array pointed to by the
 * jid_array_ptr.  The caller is responsible for  allocating and freeing
 * the memory for both arrays.  array_len is an integer representing
 * the number of pids that can be held by the pid_array_ptr array.
 * 
 * Upon successful completion, returns the the number of pids and job IDs
 * written to the arrays.  Note that this number MAY be larger than the 
 * number pointed to by pid_array_len, in which case caller knows that
 * the arrays were not large enough to hold all of the pids and job IDs.
 * The caller will want to allocate a longer array and try again.
 *
 * On error returns -1 and sets errno.
 */
extern int proctrack_get_all_pids(int array_len,
				  int32_t *pid_array_ptr,
				  uint32_t *jid_array_ptr);

/*
 * For debugging only:
 *
 * If proctrackext was compiled with the _LDEBUG, this call will
 * have the kernel extension dumps its internal records into its
 * log file.
 *
 * Otherwise, does nothing.
 */
extern void proctrack_dump_records(void);

#endif
