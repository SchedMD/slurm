/*****************************************************************************\
 *  job_functions.c - Interface to functions dealing with jobs in the database.
 ******************************************************************************
 *  Copyright (C) 2017 SchedMD LLC
 *  Written by Danny Auble da@schedmd.com, et. al.
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

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "slurm/slurmdb.h"

#include "src/interfaces/accounting_storage.h"
#include "src/interfaces/jobcomp.h"

/*
 * modify existing job in the accounting system
 * IN:  slurmdb_job_cond_t *job_cond
 * IN:  slurmdb_job_rec_t *job
 * RET: List containing (char *'s) else NULL on error
 */
extern List slurmdb_job_modify(void *db_conn,
			       slurmdb_job_cond_t *job_cond,
			       slurmdb_job_rec_t *job)
{
	if (db_api_uid == -1)
		db_api_uid = getuid();

	return acct_storage_g_modify_job(db_conn, db_api_uid, job_cond, job);
}

/*
 * get info from the storage
 * returns List of slurmdb_job_rec_t *
 * note List needs to be freed when called
 */
extern List slurmdb_jobs_get(void *db_conn, slurmdb_job_cond_t *job_cond)
{
	if (db_api_uid == -1)
		db_api_uid = getuid();

	return jobacct_storage_g_get_jobs_cond(db_conn, db_api_uid, job_cond);
}

/*
 * Fix runaway jobs
 * IN: jobs, a list of all the runaway jobs
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int slurmdb_jobs_fix_runaway(void *db_conn, List jobs)
{
	if (db_api_uid == -1)
		db_api_uid = getuid();

	return acct_storage_g_fix_runaway_jobs(db_conn, db_api_uid, jobs);
}

/* initialization of job completion logging */
extern int slurmdb_jobcomp_init(void)
{
	return jobcomp_g_init();
}

/* terminate pthreads and free, general clean-up for termination */
extern int slurmdb_jobcomp_fini(void)
{
	return jobcomp_g_fini();
}
/*
 * get info from the storage
 * returns List of jobcomp_job_rec_t *
 * note List needs to be freed when called
 */
extern List slurmdb_jobcomp_jobs_get(slurmdb_job_cond_t *job_cond)
{
	return jobcomp_g_get_jobs(job_cond);
}
