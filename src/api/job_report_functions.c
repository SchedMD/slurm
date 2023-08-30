/*****************************************************************************\
 *  job_report_functions.c - Interface to functions dealing with job reports.
 ******************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble da@llnl.gov, et. al.
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

#include <stdlib.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "slurm/slurmdb.h"

#include "src/interfaces/accounting_storage.h"
#include "src/common/xstring.h"

static int _sort_group_asc(void *v1, void *v2)
{
	char *group_a = *(char **)v1;
	char *group_b = *(char **)v2;
	int size_a = atoi(group_a);
	int size_b = atoi(group_b);

	if (size_a < size_b)
		return -1;
	else if (size_a > size_b)
		return 1;
	return 0;
}

static void _check_create_grouping(
	List cluster_list,  ListIterator group_itr,
	char *cluster, char *name, void *object,
	bool individual, bool wckey_type)
{
	ListIterator itr;
	slurmdb_wckey_rec_t *wckey = (slurmdb_wckey_rec_t *)object;
	slurmdb_assoc_rec_t *assoc = (slurmdb_assoc_rec_t *)object;
	slurmdb_report_cluster_grouping_t *cluster_group = NULL;
	slurmdb_report_acct_grouping_t *acct_group = NULL;
	slurmdb_report_job_grouping_t *job_group = NULL;

	itr = list_iterator_create(cluster_list);
	while((cluster_group = list_next(itr))) {
		if (!xstrcmp(cluster, cluster_group->cluster))
			break;
	}
	list_iterator_destroy(itr);

	if (!cluster_group) {
		cluster_group = xmalloc(
			sizeof(slurmdb_report_cluster_grouping_t));
		cluster_group->cluster = xstrdup(cluster);
		cluster_group->acct_list = list_create(
			slurmdb_destroy_report_acct_grouping);
		list_append(cluster_list, cluster_group);
	}

	itr = list_iterator_create(cluster_group->acct_list);
	while ((acct_group = list_next(itr))) {
		if (!xstrcmp(name, acct_group->acct))
			break;
	}
	list_iterator_destroy(itr);

	if (!acct_group) {
		uint32_t last_size = 0;
		char *group = NULL;
		acct_group = xmalloc(sizeof(slurmdb_report_acct_grouping_t));

		acct_group->acct = xstrdup(name);
		if (wckey_type)
			acct_group->lft = wckey->id;
		else {
			acct_group->lft = assoc->lft;
			acct_group->rgt = assoc->rgt;
		}
		acct_group->groups = list_create(
			slurmdb_destroy_report_job_grouping);
		list_append(cluster_group->acct_list, acct_group);
		while ((group = list_next(group_itr))) {
			job_group = xmalloc(
				sizeof(slurmdb_report_job_grouping_t));
			job_group->jobs = list_create(NULL);
			if (!individual)
				job_group->min_size = last_size;
			last_size = atoi(group);
			if (!individual)
				job_group->max_size = last_size-1;
			else
				job_group->min_size =
					job_group->max_size = last_size;
			list_append(acct_group->groups, job_group);
		}
		if (last_size && !individual) {
			job_group = xmalloc(
				sizeof(slurmdb_report_job_grouping_t));
			job_group->jobs = list_create(NULL);
			job_group->min_size = last_size;
			job_group->max_size = INFINITE;
			list_append(acct_group->groups, job_group);
		}
		list_iterator_reset(group_itr);
	}
}

/* FIXME: This only works for CPUS now */
static List _process_grouped_report(
	void *db_conn, slurmdb_job_cond_t *job_cond, List grouping_list,
	bool flat_view, bool wckey_type, bool both, bool acct_as_parent)
{
	int exit_code = 0;
	void *object = NULL, *object2 = NULL;

	ListIterator itr = NULL, itr2 = NULL;
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
	List object_list = NULL, object2_list = NULL;

	List tmp_acct_list = NULL;
	bool destroy_job_cond = 0;
	bool destroy_grouping_list = 0;
	bool individual = 0;
	uint32_t tres_id = TRES_CPU;

	uid_t my_uid = getuid();

	/* we don't want to actually query by accounts in the jobs
	   here since we may be looking for sub accounts of a specific
	   account.
	*/
	if (!job_cond) {
		destroy_job_cond = 1;
		job_cond = xmalloc(sizeof(slurmdb_job_cond_t));
	}
	if (!grouping_list) {
		destroy_grouping_list = 1;
		grouping_list = list_create(xfree_ptr);
		slurm_addto_char_list(grouping_list, "50,250,500,1000");
	}

	if (!flat_view) {
		tmp_acct_list = job_cond->acct_list;
		job_cond->acct_list = NULL;
	}
	job_cond->flags |= JOBCOND_FLAG_DUP;
	job_cond->db_flags = SLURMDB_JOB_FLAG_NOTSET;

	job_list = jobacct_storage_g_get_jobs_cond(db_conn, my_uid, job_cond);
	if (!flat_view) {
		job_cond->acct_list = tmp_acct_list;
		tmp_acct_list = NULL;
	}

	if (!job_list) {
		exit_code=1;
		fprintf(stderr, " Problem with job query.\n");
		goto end_it;
	}

	group_itr = list_iterator_create(grouping_list);
	/* make a group for each job size we find. */
	if (!list_count(grouping_list)) {
		char *group = NULL;

		individual = 1;
		itr = list_iterator_create(job_list);
		while ((job = list_next(itr))) {
			char *tmp = NULL;
			uint64_t count;

			if (!job->elapsed)
				continue;

			if ((count = slurmdb_find_tres_count_in_string(
				     job->tres_alloc_str, tres_id))
			    == INFINITE64)
				continue;

			tmp = xstrdup_printf("%"PRIu64, count);

			while ((group = list_next(group_itr))) {
				if (!xstrcmp(group, tmp)) {
					break;
				}
			}
			if (!group)
				list_append(grouping_list, tmp);
			else
				xfree(tmp);
			list_iterator_reset(group_itr);
		}
		list_iterator_destroy(itr);
		list_sort(grouping_list, (ListCmpF)_sort_group_asc);
	}

	cluster_list = list_create(slurmdb_destroy_report_cluster_grouping);

	cluster_itr = list_iterator_create(cluster_list);

	if (flat_view)
		goto no_objects;

	if (!wckey_type || both) {
		slurmdb_assoc_cond_t assoc_cond;
		memset(&assoc_cond, 0, sizeof(slurmdb_assoc_cond_t));
		assoc_cond.id_list = job_cond->associd_list;
		assoc_cond.cluster_list = job_cond->cluster_list;
		/* don't limit associations to having the partition_list */
		//assoc_cond.partition_list = job_cond->partition_list;
		if (acct_as_parent) {
			/*
			 * Behave like an 'ls', will ask for the subaccounts of
			 * the requested account.
			 */
			if (!job_cond->acct_list ||
			    !list_count(job_cond->acct_list)) {
				FREE_NULL_LIST(job_cond->acct_list);
				job_cond->acct_list = list_create(NULL);
				list_append(job_cond->acct_list, "root");
				assoc_cond.parent_acct_list =
					job_cond->acct_list;
			} else {
				assoc_cond.parent_acct_list =
					job_cond->acct_list;
			}
		} else {
			/* Ask strictly for these accounts. */
			if (job_cond->acct_list &&
			    list_count(job_cond->acct_list))
				assoc_cond.acct_list = job_cond->acct_list;
		}
		object_list = acct_storage_g_get_assocs(db_conn, my_uid,
							&assoc_cond);
	}

	if (wckey_type || both) {
		slurmdb_wckey_cond_t wckey_cond;
		memset(&wckey_cond, 0, sizeof(slurmdb_wckey_cond_t));
		wckey_cond.name_list = job_cond->wckey_list;
		wckey_cond.cluster_list = job_cond->cluster_list;

		object2_list = acct_storage_g_get_wckeys(db_conn, my_uid,
							 &wckey_cond);
		if (!object_list) {
			object_list = object2_list;
			object2_list = NULL;
		}
	}

	if (!object_list) {
		debug2(" No join list given.\n");
		goto no_objects;
	}

	itr = list_iterator_create(object_list);
	if (object2_list)
		itr2 = list_iterator_create(object2_list);
	while ((object = list_next(itr))) {
		char *cluster = NULL;
		slurmdb_wckey_rec_t *wckey = (slurmdb_wckey_rec_t *)object;
		slurmdb_assoc_rec_t *assoc =
			(slurmdb_assoc_rec_t *)object;
		if (!itr2) {
			char *name = NULL;
			if (wckey_type) {
				cluster = wckey->cluster;
				name = wckey->name;
			} else {
				cluster = assoc->cluster;
				name = assoc->acct;
			}
			_check_create_grouping(cluster_list, group_itr,
					       cluster, name, object,
					       individual, wckey_type);
			continue;
		}

		while ((object2 = list_next(itr2))) {
			slurmdb_wckey_rec_t *wckey2 =
				(slurmdb_wckey_rec_t *)object2;
			slurmdb_assoc_rec_t *assoc2 =
				(slurmdb_assoc_rec_t *)object2;
			char name[200];
			if (!wckey_type) {
				if (xstrcmp(assoc->cluster, wckey2->cluster))
					continue;
				cluster = assoc->cluster;
				snprintf(name, sizeof(name), "%s:%s",
					 assoc->acct, wckey2->name);
			} else {
				if (xstrcmp(wckey->cluster, assoc2->cluster))
					continue;
				cluster = wckey->cluster;
				snprintf(name, sizeof(name), "%s:%s",
					 wckey2->name, assoc->acct);
			}
			_check_create_grouping(cluster_list, group_itr,
					       cluster, name, object,
					       individual, wckey_type);
		}
		list_iterator_reset(itr2);
	}
	list_iterator_destroy(itr);
	if (itr2)
		list_iterator_destroy(itr2);

no_objects:
	itr = list_iterator_create(job_list);

	while((job = list_next(itr))) {
		char *local_cluster = "UNKNOWN";
		char tmp_acct[200];

		if (!job->elapsed) {
			/* here we don't care about jobs that didn't
			 * really run here */
			continue;
		}
		if (job->cluster)
			local_cluster = job->cluster;

		if (!wckey_type) {
			if (both && job->wckey) {
				snprintf(tmp_acct, sizeof(tmp_acct),
					 "%s:%s",
					 job->account,
					 job->wckey);
			} else {
				snprintf(tmp_acct, sizeof(tmp_acct),
					 "%s", job->account);
			}
		} else {
			if (both && job->account) {
				snprintf(tmp_acct, sizeof(tmp_acct),
					 "%s:%s",
					 job->wckey,
					 job->account);
			} else {
				snprintf(tmp_acct, sizeof(tmp_acct),
					 "%s", job->wckey);
			}
		}

		list_iterator_reset(cluster_itr);
		while((cluster_group = list_next(cluster_itr))) {
			if (!xstrcmp(local_cluster, cluster_group->cluster))
				break;
		}
		if (!cluster_group) {
			/* here we are only looking for groups that
			 * were added with the associations above
			 */
			if (!flat_view)
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
			if (wckey_type) {
				if (!xstrcmp(tmp_acct, acct_group->acct))
					break;
				continue;
			}

			if (!flat_view
			   && (acct_group->lft != NO_VAL)
			   && (job->lft != NO_VAL)) {
				/* keep separate since we don't want
				 * to so a xstrcmp if we don't have to
				 */
				if (job->lft > acct_group->lft
				    && job->lft < acct_group->rgt) {
					char *mywckey = NULL;
					if (!both)
						break;
					if (acct_group->acct) {
						if ((mywckey = strstr(
							     acct_group->acct,
							     ":")))
							mywckey++;
					}
					if (!job->wckey && !mywckey)
						break;
					else if (!mywckey || !job->wckey)
						continue;
					else if (!xstrcmp(mywckey, job->wckey))
						break;
				}
			} else if (!xstrcmp(acct_group->acct, tmp_acct))
				break;
		}
		list_iterator_destroy(acct_itr);

		if (!acct_group) {
			char *group = NULL;
			uint32_t last_size = 0;
			/* here we are only looking for groups that
			 * were added with the associations above
			 */
			if (!flat_view)
				continue;

			acct_group = xmalloc(
				sizeof(slurmdb_report_acct_grouping_t));
			acct_group->acct = xstrdup(tmp_acct);
			acct_group->groups = list_create(
				slurmdb_destroy_report_job_grouping);
			list_append(cluster_group->acct_list, acct_group);

			while((group = list_next(group_itr))) {
				job_group = xmalloc(
					sizeof(slurmdb_report_job_grouping_t));
				job_group->jobs = list_create(NULL);
				if (!individual)
					job_group->min_size = last_size;
				last_size = atoi(group);
				if (!individual)
					job_group->max_size = last_size-1;
				else
					job_group->min_size =
						job_group->max_size = last_size;
				list_append(acct_group->groups, job_group);
			}
			if (last_size && !individual) {
				job_group = xmalloc(
					sizeof(slurmdb_report_job_grouping_t));
				job_group->jobs = list_create(NULL);
				job_group->min_size = last_size;
				if (individual)
					job_group->max_size =
						job_group->min_size;
				else
					job_group->max_size = INFINITE;
				list_append(acct_group->groups, job_group);
			}
			list_iterator_reset(group_itr);
		}

		local_itr = list_iterator_create(acct_group->groups);
		while ((job_group = list_next(local_itr))) {
			uint64_t count;

			if (((count = slurmdb_find_tres_count_in_string(
				      job->tres_alloc_str, tres_id))
			     == INFINITE64) ||
			    (count < job_group->min_size) ||
			    (count > job_group->max_size))
				continue;

			list_append(job_group->jobs, job);
			job_group->count++;
			acct_group->count++;
			cluster_group->count++;

			slurmdb_transfer_tres_time(
				&job_group->tres_list, job->tres_alloc_str,
				job->elapsed);
			slurmdb_transfer_tres_time(
				&acct_group->tres_list, job->tres_alloc_str,
				job->elapsed);
			slurmdb_transfer_tres_time(
				&cluster_group->tres_list, job->tres_alloc_str,
				job->elapsed);
		}
		list_iterator_destroy(local_itr);
	}
	list_iterator_destroy(itr);
	list_iterator_destroy(group_itr);
	list_iterator_reset(cluster_itr);
	while ((cluster_group = list_next(cluster_itr))) {
		ListIterator acct_itr;
		if (!cluster_group->count) {
			list_delete_item(cluster_itr);
			continue;
		}
		acct_itr = list_iterator_create(cluster_group->acct_list);
		while ((acct_group = list_next(acct_itr))) {
			if (!acct_group->count) {
				list_delete_item(acct_itr);
				continue;
			}
		}
		list_iterator_destroy(acct_itr);
	}
	list_iterator_destroy(cluster_itr);

end_it:
	FREE_NULL_LIST(object_list);

	FREE_NULL_LIST(object2_list);

	if (destroy_job_cond)
		slurmdb_destroy_job_cond(job_cond);

	if (destroy_grouping_list)
		FREE_NULL_LIST(grouping_list);

	if (exit_code) {
		FREE_NULL_LIST(cluster_list);
	}

	return cluster_list;
}

extern List slurmdb_report_job_sizes_grouped_by_account(
	void *db_conn,  slurmdb_job_cond_t *job_cond, List grouping_list,
	bool flat_view, bool acct_as_parent)
{
	return _process_grouped_report(db_conn, job_cond, grouping_list,
				       flat_view, 0, 0, acct_as_parent);
}

extern List slurmdb_report_job_sizes_grouped_by_wckey(void *db_conn,
	slurmdb_job_cond_t *job_cond, List grouping_list)
{
	return _process_grouped_report(db_conn, job_cond, grouping_list,
				       0, 1, 0, false);
}

extern List slurmdb_report_job_sizes_grouped_by_account_then_wckey(
	void *db_conn, slurmdb_job_cond_t *job_cond,
	List grouping_list, bool flat_view, bool acct_as_parent)
{
	return _process_grouped_report(db_conn, job_cond, grouping_list,
				       flat_view, 0, 1, acct_as_parent);
}
