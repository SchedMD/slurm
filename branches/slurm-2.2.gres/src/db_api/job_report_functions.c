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

#include <stdlib.h>

#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include <slurm/slurmdb.h>

#include "src/common/slurm_accounting_storage.h"
#include "src/common/xstring.h"

static List _process_grouped_report(void *db_conn,
	slurmdb_job_cond_t *job_cond, List grouping_list,
	bool flat_view, bool wckey_type)
{
	int exit_code = 0;
	void *object = NULL;

	ListIterator itr = NULL;
	ListIterator cluster_itr = NULL;
	ListIterator local_itr = NULL;
	ListIterator acct_itr = NULL;
	ListIterator group_itr = NULL;


	slurmdb_job_rec_t *job = NULL;
	slurmdb_report_cluster_grouping_t *cluster_group = NULL;
	slurmdb_report_acct_grouping_t *acct_group = NULL;
	slurmdb_report_job_grouping_t *job_group = NULL;

	List job_list = NULL;
	List cluster_list = NULL;
	List object_list = NULL;

	List tmp_acct_list = NULL;
	bool destroy_job_cond = 0;
	bool destroy_grouping_list = 0;

	uid_t my_uid = getuid();

	/* we don't want to actually query by accounts in the jobs
	   here since we may be looking for sub accounts of a specific
	   account.
	*/
	if(!job_cond) {
		destroy_job_cond = 1;
		job_cond = xmalloc(sizeof(slurmdb_job_cond_t));
	}
	if(!grouping_list) {
		destroy_grouping_list = 1;
		grouping_list = list_create(slurm_destroy_char);
		slurm_addto_char_list(grouping_list, "50,250,500,1000");
	}
	tmp_acct_list = job_cond->acct_list;
	job_cond->acct_list = NULL;

	job_list = jobacct_storage_g_get_jobs_cond(db_conn, my_uid, job_cond);
	job_cond->acct_list = tmp_acct_list;
	tmp_acct_list = NULL;

	if(!job_list) {
		exit_code=1;
		fprintf(stderr, " Problem with job query.\n");
		goto end_it;
	}

	cluster_list = list_create(slurmdb_destroy_report_cluster_grouping);

	cluster_itr = list_iterator_create(cluster_list);
	group_itr = list_iterator_create(grouping_list);

	if(flat_view)
		goto no_objects;

	if(wckey_type) {
		slurmdb_wckey_cond_t wckey_cond;
		memset(&wckey_cond, 0, sizeof(slurmdb_wckey_cond_t));
		wckey_cond.name_list = job_cond->wckey_list;
		wckey_cond.cluster_list = job_cond->cluster_list;

		object_list = acct_storage_g_get_wckeys(db_conn, my_uid,
							&wckey_cond);
	} else {
		slurmdb_association_cond_t assoc_cond;
		memset(&assoc_cond, 0, sizeof(slurmdb_association_cond_t));
		assoc_cond.id_list = job_cond->associd_list;
		assoc_cond.cluster_list = job_cond->cluster_list;
		/* don't limit associations to having the partition_list */
		//assoc_cond.partition_list = job_cond->partition_list;
		if(!job_cond->acct_list || !list_count(job_cond->acct_list)) {
			if(job_cond->acct_list)
				list_destroy(job_cond->acct_list);
			job_cond->acct_list = list_create(NULL);
			list_append(job_cond->acct_list, "root");
		}
		assoc_cond.parent_acct_list = job_cond->acct_list;
		object_list = acct_storage_g_get_associations(db_conn, my_uid,
							      &assoc_cond);
	}

	if(!object_list) {
		debug2(" No join list given.\n");
		goto no_objects;
	}

	itr = list_iterator_create(object_list);
	while((object = list_next(itr))) {
		char *cluster = NULL, *name = NULL;
		slurmdb_wckey_rec_t *wckey = (slurmdb_wckey_rec_t *)object;
		slurmdb_association_rec_t *assoc =
			(slurmdb_association_rec_t *)object;

		if(wckey_type) {
			cluster = wckey->cluster;
			name = wckey->name;
		} else {
			cluster = assoc->cluster;
			name = assoc->acct;
		}

		while((cluster_group = list_next(cluster_itr))) {
			if(!strcmp(cluster, cluster_group->cluster))
				break;
		}
		if(!cluster_group) {
			cluster_group = xmalloc(
				sizeof(slurmdb_report_cluster_grouping_t));
			cluster_group->cluster = xstrdup(cluster);
			cluster_group->acct_list = list_create(
				slurmdb_destroy_report_acct_grouping);
			list_append(cluster_list, cluster_group);
		}

		acct_itr = list_iterator_create(cluster_group->acct_list);
		while((acct_group = list_next(acct_itr))) {
			if(!strcmp(name, acct_group->acct))
				break;
		}
		list_iterator_destroy(acct_itr);

		if(!acct_group) {
			uint32_t last_size = 0;
			char *group = NULL;
			acct_group = xmalloc(
				sizeof(slurmdb_report_acct_grouping_t));
			acct_group->acct = xstrdup(name);
			if(wckey_type)
				acct_group->lft = wckey->id;
			else {
				acct_group->lft = assoc->lft;
				acct_group->rgt = assoc->rgt;
			}
			acct_group->groups = list_create(
				slurmdb_destroy_report_job_grouping);
			list_append(cluster_group->acct_list, acct_group);
			while((group = list_next(group_itr))) {
				job_group = xmalloc(
					sizeof(slurmdb_report_job_grouping_t));
				job_group->jobs = list_create(NULL);
				job_group->min_size = last_size;
				last_size = atoi(group);
				job_group->max_size = last_size-1;
				list_append(acct_group->groups, job_group);
			}
			if(last_size) {
				job_group = xmalloc(
					sizeof(slurmdb_report_job_grouping_t));
				job_group->jobs = list_create(NULL);
				job_group->min_size = last_size;
				job_group->max_size = INFINITE;
				list_append(acct_group->groups, job_group);
			}
			list_iterator_reset(group_itr);
		}
		list_iterator_reset(cluster_itr);
	}
	list_iterator_destroy(itr);
no_objects:
	itr = list_iterator_create(job_list);

	while((job = list_next(itr))) {
		char *local_cluster = "UNKNOWN";
		char *local_account = "UNKNOWN";

		if(!job->elapsed) {
			/* here we don't care about jobs that didn't
			 * really run here */
			continue;
		}
		if(job->cluster)
			local_cluster = job->cluster;
		if(job->account)
			local_account = job->account;

		list_iterator_reset(cluster_itr);
		while((cluster_group = list_next(cluster_itr))) {
			if(!strcmp(local_cluster, cluster_group->cluster))
				break;
		}
		if(!cluster_group) {
			/* here we are only looking for groups that
			 * were added with the associations above
			 */
			if(!flat_view)
				continue;
			cluster_group = xmalloc(
				sizeof(slurmdb_report_cluster_grouping_t));
			cluster_group->cluster = xstrdup(local_cluster);
			cluster_group->acct_list = list_create(
				slurmdb_destroy_report_acct_grouping);
			list_append(cluster_list, cluster_group);
		}

		acct_itr = list_iterator_create(cluster_group->acct_list);
		while((acct_group = list_next(acct_itr))) {
			if(wckey_type) {
				if(!strcmp(job->wckey, acct_group->acct))
					break;
				continue;
			}

			if(!flat_view
			   && (acct_group->lft != (uint32_t)NO_VAL)
			   && (job->lft != (uint32_t)NO_VAL)) {
				/* keep separate since we don't want
				 * to so a strcmp if we don't have to
				 */
				if(job->lft > acct_group->lft
				   && job->lft < acct_group->rgt)
					break;
			} else if(!strcmp(acct_group->acct, local_account))
				break;
		}
		list_iterator_destroy(acct_itr);

		if(!acct_group) {
			char *group = NULL;
			uint32_t last_size = 0;
			/* here we are only looking for groups that
			 * were added with the associations above
			 */
			if(!flat_view)
				continue;

			acct_group = xmalloc(
				sizeof(slurmdb_report_acct_grouping_t));
			acct_group->acct = xstrdup(local_account);
			acct_group->groups = list_create(
				slurmdb_destroy_report_job_grouping);
			list_append(cluster_group->acct_list, acct_group);

			while((group = list_next(group_itr))) {
				job_group = xmalloc(
					sizeof(slurmdb_report_job_grouping_t));
				job_group->jobs = list_create(NULL);
				job_group->min_size = last_size;
				last_size = atoi(group);
				job_group->max_size = last_size-1;
				list_append(acct_group->groups, job_group);
			}
			if(last_size) {
				job_group = xmalloc(
					sizeof(slurmdb_report_job_grouping_t));
				job_group->jobs = list_create(NULL);
				job_group->min_size = last_size;
				job_group->max_size = INFINITE;
				list_append(acct_group->groups, job_group);
			}
			list_iterator_reset(group_itr);
		}

		local_itr = list_iterator_create(acct_group->groups);
		while((job_group = list_next(local_itr))) {
			uint64_t total_secs = 0;
			if((job->alloc_cpus < job_group->min_size)
			   || (job->alloc_cpus > job_group->max_size))
				continue;
			list_append(job_group->jobs, job);
			job_group->count++;
			total_secs = (uint64_t)job->elapsed
				* (uint64_t)job->alloc_cpus;
			job_group->cpu_secs += total_secs;
			acct_group->cpu_secs += total_secs;
			cluster_group->cpu_secs += total_secs;
		}
		list_iterator_destroy(local_itr);
	}
	list_iterator_destroy(itr);
	list_iterator_destroy(group_itr);
	list_iterator_destroy(cluster_itr);

end_it:
	if(object_list)
		list_destroy(object_list);

	if(destroy_job_cond)
		slurmdb_destroy_job_cond(job_cond);

	if(destroy_grouping_list && grouping_list)
		list_destroy(grouping_list);

	if(exit_code) {
		if(cluster_list) {
			list_destroy(cluster_list);
			cluster_list = NULL;
		}
	}

	return cluster_list;
}


extern List slurmdb_report_job_sizes_grouped_by_top_account(void *db_conn,
	slurmdb_job_cond_t *job_cond, List grouping_list, bool flat_view)
{
	return _process_grouped_report(db_conn, job_cond, grouping_list,
				       flat_view, 0);
}

extern List slurmdb_report_job_sizes_grouped_by_wckey(void *db_conn,
	slurmdb_job_cond_t *job_cond, List grouping_list)
{
	return _process_grouped_report(db_conn, job_cond, grouping_list, 0, 1);
}
