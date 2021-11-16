/*****************************************************************************\
 *  slurm_jobcomp.h - implementation-independent job completion logging
 *  API definitions
 *****************************************************************************
 *  Copyright (C) 2003 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.com> et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifndef __SLURM_JOBCOMP_H__
#define __SLURM_JOBCOMP_H__

#include <inttypes.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "src/slurmctld/slurmctld.h"
#include "src/common/slurm_accounting_storage.h"

typedef struct {
	uint32_t jobid;
	char *partition;
	char *start_time;
	char *end_time;
	time_t elapsed_time;
	uint32_t uid;
	char *uid_name;
	uint32_t gid;
	char *gid_name;
	uint32_t node_cnt;
	uint32_t proc_cnt;
	char *nodelist;
	char *jobname;
	char *state;
	char *timelimit;
	char *blockid;
	char *connection;
	char *reboot;
	char *rotate;
	uint32_t max_procs;
	char *geo;
	char *bg_start_point;
	char *work_dir;
	char *resv_name;
	char *tres_fmt_req_str;
	char *account;
	char *qos_name;
	char *wckey;
	char *cluster;
	char *submit_time;
	char *eligible_time;
	char *derived_ec;
	char *exit_code;
} jobcomp_job_rec_t;

extern void jobcomp_destroy_job(void *object);

/* initialization of job completion logging */
extern int jobcomp_g_init(char *jobcomp_loc);

/* terminate pthreads and free, general clean-up for termination */
extern int jobcomp_g_fini(void);

/* write record of a job's completion */
extern int jobcomp_g_write(job_record_t *job_ptr);

/*
 * get info from the storage
 * returns List of jobcomp_job_rec_t *
 * note List needs to be freed when called
 */
extern List jobcomp_g_get_jobs(slurmdb_job_cond_t *job_cond);

/* set the location based on JobCompLoc */
extern int jobcomp_g_set_location(char *jobcomp_loc);

#endif /*__SLURM_JOBCOMP_H__*/
