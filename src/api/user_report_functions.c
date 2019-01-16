/*****************************************************************************\
 *  user_report_functions.c - Interface to functions dealing with user reports.
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

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "slurm/slurmdb.h"

#include "src/common/slurmdb_defs.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/xstring.h"

extern List slurmdb_report_user_top_usage(void *db_conn,
					  slurmdb_user_cond_t *user_cond,
					  bool group_accounts)
{
	List cluster_list = NULL;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	ListIterator itr3 = NULL;
	ListIterator cluster_itr = NULL;
	slurmdb_cluster_cond_t cluster_cond;
	List user_list = NULL;
	List usage_cluster_list = NULL;
	char *object = NULL;
	int exit_code = 0;
	slurmdb_user_rec_t *user = NULL;
	slurmdb_cluster_rec_t *cluster = NULL;
	slurmdb_assoc_rec_t *assoc = NULL;
	slurmdb_report_user_rec_t *slurmdb_report_user = NULL;
	slurmdb_report_cluster_rec_t *slurmdb_report_cluster = NULL;
	uid_t my_uid = getuid();
	bool delete_user_cond = 0, delete_assoc_cond = 0,
		delete_cluster_list = 0;
	time_t start_time, end_time;

	if (!user_cond) {
		delete_user_cond = 1;
		user_cond = xmalloc(sizeof(slurmdb_user_cond_t));
	}

	if (!user_cond->assoc_cond) {
		delete_assoc_cond = 1;
		user_cond->assoc_cond =
			xmalloc(sizeof(slurmdb_assoc_cond_t));
	}

	if (!user_cond->assoc_cond->cluster_list) {
		delete_cluster_list = 1;
		user_cond->assoc_cond->cluster_list =
			list_create(slurm_destroy_char);
	}

	user_cond->with_deleted = 1;
	user_cond->with_assocs = 1;
	user_cond->assoc_cond->with_usage = 1;
	user_cond->assoc_cond->without_parent_info = 1;

	/* This needs to be done on some systems to make sure
	   assoc_cond isn't messed up.  This has happened on some 64
	   bit machines and this is here to be on the safe side.
	*/
	start_time = user_cond->assoc_cond->usage_start;
	end_time = user_cond->assoc_cond->usage_end;
	slurmdb_report_set_start_end_time(&start_time, &end_time);
	user_cond->assoc_cond->usage_start = start_time;
	user_cond->assoc_cond->usage_end = end_time;

	user_list = acct_storage_g_get_users(db_conn, my_uid, user_cond);
	if (!user_list) {
		exit_code=1;
		fprintf(stderr, " Problem with user query.\n");
		goto end_it;
	}

	/* We have to get the clusters here or we will be unable to
	   get the correct total time for the cluster if associations
	   are not enforced.
	*/
	slurmdb_init_cluster_cond(&cluster_cond, 0);
	cluster_cond.with_usage = 1;
	cluster_cond.with_deleted = 1;
	cluster_cond.usage_end = user_cond->assoc_cond->usage_end;
	cluster_cond.usage_start = user_cond->assoc_cond->usage_start;
	cluster_cond.cluster_list = user_cond->assoc_cond->cluster_list;

	usage_cluster_list = acct_storage_g_get_clusters(
		db_conn, my_uid, &cluster_cond);
	if (!usage_cluster_list) {
		exit_code=1;
		fprintf(stderr, " Problem with cluster query.\n");
		goto end_it;
	}

	cluster_list = list_create(slurmdb_destroy_report_cluster_rec);

	itr = list_iterator_create(usage_cluster_list);
	while((cluster = list_next(itr))) {
		/* check to see if this cluster is around during the
		   time we are looking at */
		if (!cluster->accounting_list
		   || !list_count(cluster->accounting_list))
			continue;

		slurmdb_report_cluster = slurmdb_cluster_rec_2_report(cluster);

		list_append(cluster_list, slurmdb_report_cluster);

		slurmdb_report_cluster->user_list =
			list_create(slurmdb_destroy_report_user_rec);
	}
	list_iterator_destroy(itr);
	FREE_NULL_LIST(usage_cluster_list);

	itr = list_iterator_create(user_list);
	cluster_itr = list_iterator_create(cluster_list);
	while((user = list_next(itr))) {
		struct passwd *passwd_ptr = NULL;
		if (!user->assoc_list || !list_count(user->assoc_list))
			continue;

		passwd_ptr = getpwnam(user->name);
		if (passwd_ptr)
			user->uid = passwd_ptr->pw_uid;
		else
			user->uid = NO_VAL;

		itr2 = list_iterator_create(user->assoc_list);
		while((assoc = list_next(itr2))) {

			if (!assoc->accounting_list
			   || !list_count(assoc->accounting_list))
				continue;

			while((slurmdb_report_cluster =
			       list_next(cluster_itr))) {
				if (!xstrcmp(slurmdb_report_cluster->name,
					     assoc->cluster)) {
					ListIterator user_itr = NULL;
					if (!group_accounts) {
						slurmdb_report_user = NULL;
						goto new_user;
					}
					user_itr = list_iterator_create(
						slurmdb_report_cluster->
						user_list);
					while((slurmdb_report_user
					       = list_next(user_itr))) {
						if (slurmdb_report_user->uid
						   != NO_VAL) {
							if (slurmdb_report_user->
							   uid
							   == user->uid)
								break;
						} else if (slurmdb_report_user->
							  name
							  && !xstrcasecmp(
								  slurmdb_report_user->
								  name,
								  user->name))
							break;
					}
					list_iterator_destroy(user_itr);
				new_user:
					if (!slurmdb_report_user) {
						slurmdb_report_user = xmalloc(
							sizeof
							(slurmdb_report_user_rec_t));
						slurmdb_report_user->name =
							xstrdup(assoc->user);
						slurmdb_report_user->uid =
							user->uid;
						slurmdb_report_user->acct_list =
							list_create
							(slurm_destroy_char);
						list_append(slurmdb_report_cluster->
							    user_list,
							    slurmdb_report_user);
					}
					break;
				}
			}
			if (!slurmdb_report_cluster) {
				error("This cluster '%s' hasn't "
				      "registered yet, but we have jobs "
				      "that ran?", assoc->cluster);
				slurmdb_report_cluster =
					xmalloc(sizeof(slurmdb_report_cluster_rec_t));
				list_append(cluster_list, slurmdb_report_cluster);

				slurmdb_report_cluster->name = xstrdup(assoc->cluster);
				slurmdb_report_cluster->user_list =
					list_create(slurmdb_destroy_report_user_rec);
				slurmdb_report_user =
					xmalloc(sizeof(slurmdb_report_user_rec_t));
				slurmdb_report_user->name = xstrdup(assoc->user);
				slurmdb_report_user->uid = user->uid;
				slurmdb_report_user->acct_list =
					list_create(slurm_destroy_char);
				list_append(slurmdb_report_cluster->user_list,
					    slurmdb_report_user);
			}
			list_iterator_reset(cluster_itr);

			itr3 = list_iterator_create(
				slurmdb_report_user->acct_list);
			while((object = list_next(itr3))) {
				if (!xstrcmp(object, assoc->acct))
					break;
			}
			list_iterator_destroy(itr3);

			if (!object)
				list_append(slurmdb_report_user->acct_list,
					    xstrdup(assoc->acct));
			slurmdb_transfer_acct_list_2_tres(
				assoc->accounting_list,
				&slurmdb_report_user->tres_list);
		}
		list_iterator_destroy(itr2);
	}
	list_iterator_destroy(itr);
	list_iterator_destroy(cluster_itr);

end_it:
	if (delete_cluster_list) {
		FREE_NULL_LIST(user_cond->assoc_cond->cluster_list);
		user_cond->assoc_cond->cluster_list = NULL;
	}

	if (delete_assoc_cond) {
		slurmdb_destroy_assoc_cond(user_cond->assoc_cond);
		user_cond->assoc_cond = NULL;
	}

	if (delete_user_cond) {
		slurmdb_destroy_user_cond(user_cond);
		user_cond = NULL;
	}

	FREE_NULL_LIST(user_list);

	if (exit_code) {
		FREE_NULL_LIST(cluster_list);
	}

	return cluster_list;
}
