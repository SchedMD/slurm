/*****************************************************************************\
 *  job_report_functions.c - Interface to functions dealing with job reports.
 ******************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble da@llnl.gov, et. al.
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
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include <slurm/slurmdb.h>

#include "src/common/slurm_accounting_storage.h"

/* typedef struct { */
/* 	List jobs; /\* This should be a NULL destroy since we are just */
/* 		    * putting a pointer to a slurmdb_job_rect_t here */
/* 		    * not allocating any new memory *\/ */
/* 	uint32_t min_size; /\* smallest size of job in cpus here 0 if first *\/ */
/* 	uint32_t max_size; /\* largest size of job in cpus here INFINITE if */
/* 			    * last *\/ */
/* 	uint32_t count; /\* count of jobs *\/ */
/* 	uint64_t cpu_secs; /\* how many cpus secs taken up by this */
/* 			    * grouping *\/ */
/* } local_grouping_t; */

/* typedef struct { */
/* 	char *acct; /\*account name *\/ */
/* 	uint64_t cpu_secs; /\* how many cpus secs taken up by this */
/* 			    * acct *\/ */
/* 	List groups; /\* Names of what are being grouped char *'s*\/ */
/* 	uint32_t lft; */
/* 	uint32_t rgt; */
/* } acct_grouping_t; */

/* typedef struct { */
/* 	char *cluster; /\*cluster name *\/ */
/* 	uint64_t cpu_secs; /\* how many cpus secs taken up by this */
/* 			    * cluster *\/ */
/* 	List acct_list; /\* containing acct_grouping_t's *\/ */
/* } cluster_grouping_t; */


/* static void _destroy_local_grouping(void *object) */
/* { */
/* 	local_grouping_t *local_grouping = (local_grouping_t *)object; */
/* 	if(local_grouping) { */
/* 		list_destroy(local_grouping->jobs); */
/* 		xfree(local_grouping); */
/* 	} */
/* } */

/* static void _destroy_acct_grouping(void *object) */
/* { */
/* 	acct_grouping_t *acct_grouping = (acct_grouping_t *)object; */
/* 	if(acct_grouping) { */
/* 		xfree(acct_grouping->acct); */
/* 		if(acct_grouping->groups) */
/* 			list_destroy(acct_grouping->groups); */
/* 		xfree(acct_grouping); */
/* 	} */
/* } */

/* static void _destroy_cluster_grouping(void *object) */
/* { */
/* 	cluster_grouping_t *cluster_grouping = (cluster_grouping_t *)object; */
/* 	if(cluster_grouping) { */
/* 		xfree(cluster_grouping->cluster); */
/* 		if(cluster_grouping->acct_list) */
/* 			list_destroy(cluster_grouping->acct_list); */
/* 		xfree(cluster_grouping); */
/* 	} */
/* } */


/* extern List slurmdb_report_job_sizes_grouped_by_top_account( */
/* 	slurmdb_job_cond_t *job_cond, List grouping_list) */
/* { */

/* 	/\* we don't want to actually query by accounts in the jobs */
/* 	   here since we may be looking for sub accounts of a specific */
/* 	   account. */
/* 	*\/ */
/* 	tmp_acct_list = job_cond->acct_list; */
/* 	job_cond->acct_list = NULL; */
/* 	job_list = jobacct_storage_g_get_jobs_cond(db_conn, my_uid, job_cond); */
/* 	job_cond->acct_list = tmp_acct_list; */
/* 	tmp_acct_list = NULL; */

/* 	if(!job_list) { */
/* 		exit_code=1; */
/* 		fprintf(stderr, " Problem with job query.\n"); */
/* 		goto end_it; */
/* 	} */



/* } */

/* extern List slurmdb_report_job_sizes_grouped_by_wckey( */
/* 	slurmdb_job_cond_t *job_cond) */
/* { */

/* } */
