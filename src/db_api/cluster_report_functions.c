/*****************************************************************************\
 *  cluster_report_functions.c - Interface to functions dealing with cluster
 *                               reports.
 ******************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble da@llnl.gov, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/slurmdb_defs.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/xstring.h"

typedef enum {
	CLUSTER_REPORT_UA,
	CLUSTER_REPORT_AU,
	CLUSTER_REPORT_UW,
	CLUSTER_REPORT_WU
} cluster_report_t;

static void _process_ua(List user_list, slurmdb_association_rec_t *assoc)
{
	ListIterator itr = NULL;
	slurmdb_report_user_rec_t *slurmdb_report_user = NULL;
	slurmdb_accounting_rec_t *accting = NULL;

	/* make sure we add all associations to this
	   user rec because we could have some in
	   partitions which would create another
	   record otherwise
	*/
	itr = list_iterator_create(user_list);
	while((slurmdb_report_user = list_next(itr))) {
		if (!strcmp(slurmdb_report_user->name, assoc->user)
		   && !strcmp(slurmdb_report_user->acct, assoc->acct))
			break;
	}
	list_iterator_destroy(itr);

	if (!slurmdb_report_user) {
		struct passwd *passwd_ptr = NULL;
		uid_t uid = NO_VAL;
		passwd_ptr = getpwnam(assoc->user);
		if (passwd_ptr)
			uid = passwd_ptr->pw_uid;
		/* In this report we are using the slurmdb_report user
		   structure to store the information we want
		   since it is already available and will do
		   pretty much what we want.
		*/
		slurmdb_report_user =
			xmalloc(sizeof(slurmdb_report_user_rec_t));
		slurmdb_report_user->name = xstrdup(assoc->user);
		slurmdb_report_user->uid = uid;
		slurmdb_report_user->acct = xstrdup(assoc->acct);

		list_append(user_list, slurmdb_report_user);
	}
	/* get the amount of time this assoc used
	   during the time we are looking at */
	itr = list_iterator_create(assoc->accounting_list);
	while((accting = list_next(itr))) {
		slurmdb_report_user->cpu_secs +=
			(uint64_t)accting->alloc_secs;
		slurmdb_report_user->consumed_energy +=
			(uint64_t)accting->consumed_energy;
	}
	list_iterator_destroy(itr);
}

static void _process_au(List assoc_list, slurmdb_association_rec_t *assoc)
{
	slurmdb_report_assoc_rec_t *slurmdb_report_assoc =
		xmalloc(sizeof(slurmdb_report_assoc_rec_t));
	ListIterator itr = NULL;
	slurmdb_accounting_rec_t *accting = NULL;

	list_append(assoc_list, slurmdb_report_assoc);

	slurmdb_report_assoc->acct = xstrdup(assoc->acct);
	slurmdb_report_assoc->cluster = xstrdup(assoc->cluster);
	slurmdb_report_assoc->parent_acct = xstrdup(assoc->parent_acct);
	slurmdb_report_assoc->user = xstrdup(assoc->user);

	/* get the amount of time this assoc used
	   during the time we are looking at */
	itr = list_iterator_create(assoc->accounting_list);
	while((accting = list_next(itr))) {
		slurmdb_report_assoc->cpu_secs +=
			(uint64_t)accting->alloc_secs;
		slurmdb_report_assoc->consumed_energy +=
			(uint64_t)accting->consumed_energy;
	}
	list_iterator_destroy(itr);

}

static void _process_uw(List user_list, slurmdb_wckey_rec_t *wckey)
{
	ListIterator itr = NULL;
	slurmdb_report_user_rec_t *slurmdb_report_user = NULL;
	slurmdb_accounting_rec_t *accting = NULL;
	struct passwd *passwd_ptr = NULL;
	uid_t uid = NO_VAL;

	passwd_ptr = getpwnam(wckey->user);
	if (passwd_ptr)
		uid = passwd_ptr->pw_uid;
	/* In this report we are using the slurmdb_report user
	   structure to store the information we want
	   since it is already available and will do
	   pretty much what we want.
	*/
	slurmdb_report_user =
		xmalloc(sizeof(slurmdb_report_user_rec_t));
	slurmdb_report_user->name = xstrdup(wckey->user);
	slurmdb_report_user->uid = uid;
	slurmdb_report_user->acct = xstrdup(wckey->name);

	list_append(user_list, slurmdb_report_user);

	/* get the amount of time this wckey used
	   during the time we are looking at */
	itr = list_iterator_create(wckey->accounting_list);
	while((accting = list_next(itr))) {
		slurmdb_report_user->cpu_secs +=
			(uint64_t)accting->alloc_secs;
		slurmdb_report_user->consumed_energy +=
			(uint64_t)accting->consumed_energy;
	}
	list_iterator_destroy(itr);
}

static void _process_wu(List assoc_list, slurmdb_wckey_rec_t *wckey)
{
	slurmdb_report_assoc_rec_t *slurmdb_report_assoc = NULL,
		*parent_assoc = NULL;
	ListIterator itr = NULL;
	slurmdb_accounting_rec_t *accting = NULL;

	/* find the parent */
	itr = list_iterator_create(assoc_list);
	while((parent_assoc = list_next(itr))) {
		if (!parent_assoc->user
		   && !strcmp(parent_assoc->acct, wckey->name))
			break;
	}
	list_iterator_destroy(itr);
	if (!parent_assoc) {
		parent_assoc = xmalloc(sizeof(slurmdb_report_assoc_rec_t));

		list_append(assoc_list,
			    parent_assoc);
		parent_assoc->acct = xstrdup(wckey->name);
	}

	/* now add one for the user */
	slurmdb_report_assoc = xmalloc(sizeof(slurmdb_report_assoc_rec_t));
	list_append(assoc_list, slurmdb_report_assoc);

	slurmdb_report_assoc->acct = xstrdup(wckey->name);
	slurmdb_report_assoc->user = xstrdup(wckey->user);

	/* get the amount of time this wckey used
	   during the time we are looking at */
	itr = list_iterator_create(wckey->accounting_list);
	while((accting = list_next(itr))) {
		slurmdb_report_assoc->cpu_secs +=
			(uint64_t)accting->alloc_secs;
		parent_assoc->cpu_secs +=
			(uint64_t)accting->alloc_secs;
		slurmdb_report_assoc->consumed_energy +=
			(uint64_t)accting->consumed_energy;
		parent_assoc->consumed_energy +=
			(uint64_t)accting->consumed_energy;
	}
	list_iterator_destroy(itr);
}

static void _process_assoc_type(
	ListIterator itr,
	slurmdb_report_cluster_rec_t *slurmdb_report_cluster,
	char *cluster_name,
	cluster_report_t type)
{
	slurmdb_association_rec_t *assoc = NULL;

	/* now add the associations of interest here by user */
	while((assoc = list_next(itr))) {
		if (!assoc->accounting_list
		   || !list_count(assoc->accounting_list)
		   || ((type == CLUSTER_REPORT_UA) && !assoc->user)) {
			list_delete_item(itr);
			continue;
		}

		if (strcmp(cluster_name, assoc->cluster))
			continue;

		if (type == CLUSTER_REPORT_UA)
			_process_ua(slurmdb_report_cluster->user_list,
				    assoc);
		else if (type == CLUSTER_REPORT_AU)
			_process_au(slurmdb_report_cluster->assoc_list,
				    assoc);

		list_delete_item(itr);
	}
}

static void _process_wckey_type(
	ListIterator itr,
	slurmdb_report_cluster_rec_t *slurmdb_report_cluster,
	char *cluster_name,
	cluster_report_t type)
{
	slurmdb_wckey_rec_t *wckey = NULL;

	/* now add the wckeyiations of interest here by user */
	while((wckey = list_next(itr))) {
		if (!wckey->accounting_list
		   || !list_count(wckey->accounting_list)
		   || ((type == CLUSTER_REPORT_UW) && !wckey->user)) {
			list_delete_item(itr);
			continue;
		}

		if (strcmp(cluster_name, wckey->cluster))
			continue;

		if (type == CLUSTER_REPORT_UW)
			_process_uw(slurmdb_report_cluster->user_list,
				    wckey);
		else if (type == CLUSTER_REPORT_WU)
			_process_wu(slurmdb_report_cluster->assoc_list,
				    wckey);

		list_delete_item(itr);
	}
}

static List _process_util_by_report(void *db_conn, char *calling_name,
				    void *cond, cluster_report_t type)
{	ListIterator itr = NULL;
	ListIterator type_itr = NULL;
	slurmdb_cluster_cond_t cluster_cond;
	List type_list = NULL;
	List cluster_list = NULL;
	List first_list = NULL;
	slurmdb_cluster_rec_t *cluster = NULL;
	slurmdb_report_cluster_rec_t *slurmdb_report_cluster = NULL;
	time_t start_time, end_time;

	int exit_code = 0;

	uid_t my_uid = getuid();
	List ret_list = list_create(slurmdb_destroy_report_cluster_rec);

	slurmdb_init_cluster_cond(&cluster_cond, 0);

	cluster_cond.with_deleted = 1;
	cluster_cond.with_usage = 1;
	if ((type == CLUSTER_REPORT_UA) || (type == CLUSTER_REPORT_AU)) {
		start_time = ((slurmdb_association_cond_t *)cond)->usage_start;
		end_time = ((slurmdb_association_cond_t *)cond)->usage_end;

		cluster_cond.cluster_list =
			((slurmdb_association_cond_t *)cond)->cluster_list;
	} else if ((type == CLUSTER_REPORT_UW) || (type == CLUSTER_REPORT_WU)) {
		start_time = ((slurmdb_wckey_cond_t *)cond)->usage_start;
		end_time = ((slurmdb_wckey_cond_t *)cond)->usage_end;

		cluster_cond.cluster_list =
			((slurmdb_wckey_cond_t *)cond)->cluster_list;
	} else {
		error("unknown report type %d", type);
		return NULL;
	}

	/* This needs to be done on some systems to make sure
	   cluster_cond isn't messed.  This has happened on some 64
	   bit machines and this is here to be on the safe side.
	*/
	slurmdb_report_set_start_end_time(&start_time, &end_time);
	cluster_cond.usage_end = end_time;
	cluster_cond.usage_start = start_time;


	cluster_list = acct_storage_g_get_clusters(
		db_conn, my_uid, &cluster_cond);

	if (!cluster_list) {
		exit_code=1;
		fprintf(stderr, "%s: Problem with cluster query.\n",
			calling_name);
		goto end_it;
	}

	if ((type == CLUSTER_REPORT_UA) || (type == CLUSTER_REPORT_AU)) {
		((slurmdb_association_cond_t *)cond)->usage_start = start_time;
		((slurmdb_association_cond_t *)cond)->usage_end = end_time;
		type_list = acct_storage_g_get_associations(
			db_conn, my_uid, cond);
	} else if ((type == CLUSTER_REPORT_UW) || (type == CLUSTER_REPORT_WU)) {
		((slurmdb_wckey_cond_t *)cond)->usage_start = start_time;
		((slurmdb_wckey_cond_t *)cond)->usage_end = end_time;
		type_list = acct_storage_g_get_wckeys(
			db_conn, my_uid, cond);
	}

	if (!type_list) {
		exit_code=1;
		fprintf(stderr, "%s: Problem with get query.\n", calling_name);
		goto end_it;
	}

	if ((type == CLUSTER_REPORT_UA) || (type == CLUSTER_REPORT_AU)) {
		first_list = type_list;
		type_list = slurmdb_get_hierarchical_sorted_assoc_list(
			first_list);
	}

	/* set up the structures for easy retrieval later */
	itr = list_iterator_create(cluster_list);
	type_itr = list_iterator_create(type_list);
	while((cluster = list_next(itr))) {
		/* check to see if this cluster is around during the
		   time we are looking at */
		if (!cluster->accounting_list
		   || !list_count(cluster->accounting_list))
			continue;

		slurmdb_report_cluster = slurmdb_cluster_rec_2_report(cluster);

		list_append(ret_list, slurmdb_report_cluster);

		if ((type == CLUSTER_REPORT_UA) || (type == CLUSTER_REPORT_UW))
			slurmdb_report_cluster->user_list =
				list_create(slurmdb_destroy_report_user_rec);
		else if ((type == CLUSTER_REPORT_AU)
			|| (type == CLUSTER_REPORT_WU))
			slurmdb_report_cluster->assoc_list =
				list_create(slurmdb_destroy_report_assoc_rec);

		if ((type == CLUSTER_REPORT_UA) || (type == CLUSTER_REPORT_AU))
			_process_assoc_type(type_itr, slurmdb_report_cluster,
					    cluster->name, type);
		else if ((type == CLUSTER_REPORT_UW)
			|| (type == CLUSTER_REPORT_WU))
			_process_wckey_type(type_itr, slurmdb_report_cluster,
					    cluster->name, type);
		list_iterator_reset(type_itr);
	}
	list_iterator_destroy(type_itr);
	list_iterator_destroy(itr);

end_it:

	if (type_list) {
		list_destroy(type_list);
		type_list = NULL;
	}

	if (first_list) {
		list_destroy(first_list);
		first_list = NULL;
	}

	if (cluster_list) {
		list_destroy(cluster_list);
		cluster_list = NULL;
	}

	if (exit_code) {
		if (ret_list) {
			list_destroy(ret_list);
			ret_list = NULL;
		}
	}

	return ret_list;
}


extern List slurmdb_report_cluster_account_by_user(void *db_conn,
	slurmdb_association_cond_t *assoc_cond)
{
	return _process_util_by_report(db_conn,
				       "slurmdb_report_cluster_account_by_user",
				       assoc_cond, CLUSTER_REPORT_AU);
}

extern List slurmdb_report_cluster_user_by_account(void *db_conn,
	slurmdb_association_cond_t *assoc_cond)
{
	return _process_util_by_report(db_conn,
				       "slurmdb_report_cluster_user_by_account",
				       assoc_cond, CLUSTER_REPORT_UA);
}

extern List slurmdb_report_cluster_wckey_by_user(void *db_conn,
	slurmdb_wckey_cond_t *wckey_cond)
{
	return _process_util_by_report(db_conn,
				       "slurmdb_report_cluster_wckey_by_user",
				       wckey_cond, CLUSTER_REPORT_WU);
}

extern List slurmdb_report_cluster_user_by_wckey(void *db_conn,
	slurmdb_wckey_cond_t *wckey_cond)
{
	return _process_util_by_report(db_conn,
				       "slurmdb_report_cluster_user_by_wckey",
				       wckey_cond, CLUSTER_REPORT_UW);
}
