/*****************************************************************************\
 *  as_mysql_problems.c - functions for finding out problems in the
 *                     associations and other places in the database.
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#include "as_mysql_problems.h"
#include "src/common/uid.h"

static int _setup_assoc_cond_limits(
	slurmdb_assoc_cond_t *assoc_cond,
	char **extra, bool user_query)
{
	int set = 0;
	ListIterator itr = NULL;
	char *object = NULL;

	xstrfmtcat(*extra, "where deleted=0");

	if (!assoc_cond)
		return 0;

	if (assoc_cond->acct_list && list_count(assoc_cond->acct_list)) {
		set = 0;
		xstrcat(*extra, " && (");
		itr = list_iterator_create(assoc_cond->acct_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "acct='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if (assoc_cond->user_list && list_count(assoc_cond->user_list)) {
		set = 0;
		xstrcat(*extra, " && (");
		itr = list_iterator_create(assoc_cond->user_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "user='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	} else if (user_query) {
		/* we want all the users, but no non-user associations */
		set = 1;
		xstrcat(*extra, " && (user!='')");
	}

	if (assoc_cond->partition_list
	    && list_count(assoc_cond->partition_list)) {
		set = 0;
		xstrcat(*extra, " && (");
		itr = list_iterator_create(assoc_cond->partition_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "`partition`='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	return set;
}


extern int as_mysql_acct_no_assocs(mysql_conn_t *mysql_conn,
				   slurmdb_assoc_cond_t *assoc_cond,
				   List ret_list)
{
	int rc = SLURM_SUCCESS;
	char *query = NULL;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	List use_cluster_list = as_mysql_cluster_list;
	ListIterator itr = NULL;
	char *cluster_name = NULL;

	xassert(ret_list);

	query = xstrdup_printf("select name from %s where deleted=0",
			       acct_table);
	if (assoc_cond &&
	    assoc_cond->acct_list && list_count(assoc_cond->acct_list)) {
		int set = 0;
		ListIterator itr = NULL;
		char *object = NULL;
		xstrcat(query, " && (");
		itr = list_iterator_create(assoc_cond->acct_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(query, " || ");
			xstrfmtcat(query, "name='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(query, ")");
	}

	if (!(result = mysql_db_query_ret(
		      mysql_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	if (assoc_cond &&
	    assoc_cond->cluster_list && list_count(assoc_cond->cluster_list))
		use_cluster_list = assoc_cond->cluster_list;
	else
		slurm_mutex_lock(&as_mysql_cluster_list_lock);

	itr = list_iterator_create(use_cluster_list);
	while ((row = mysql_fetch_row(result))) {
		MYSQL_RES *result2 = NULL;
		int cnt = 0;
		slurmdb_assoc_rec_t *assoc = NULL;

		/* See if we have at least 1 association in the system */
		while ((cluster_name = list_next(itr))) {
			if (query)
				xstrcat(query, " union ");
			xstrfmtcat(query,
				   "select distinct id_assoc from \"%s_%s\" "
				   "where deleted=0 && "
				   "acct='%s'",
				   cluster_name, assoc_table, row[0]);
		}
		list_iterator_reset(itr);
		if (query)
			xstrcat(query, " limit 1");
		if (!(result2 = mysql_db_query_ret(
			      mysql_conn, query, 0))) {
			xfree(query);
			rc = SLURM_ERROR;
			break;
		}
		xfree(query);

		cnt = mysql_num_rows(result2);
		mysql_free_result(result2);

		if (cnt)
			continue;

		assoc =	xmalloc(sizeof(slurmdb_assoc_rec_t));
		list_append(ret_list, assoc);

		assoc->id = SLURMDB_PROBLEM_ACCT_NO_ASSOC;
		assoc->acct = xstrdup(row[0]);
	}
	mysql_free_result(result);

	list_iterator_destroy(itr);
	if (use_cluster_list == as_mysql_cluster_list)
		slurm_mutex_unlock(&as_mysql_cluster_list_lock);

	return rc;
}

extern int as_mysql_acct_no_users(mysql_conn_t *mysql_conn,
				  slurmdb_assoc_cond_t *assoc_cond,
				  List ret_list)
{
	int rc = SLURM_SUCCESS;
	char *query = NULL, *tmp = NULL;
	char *extra = NULL;
	int i = 0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	List use_cluster_list = as_mysql_cluster_list;
	ListIterator itr = NULL;
	char *cluster_name;

	xassert(ret_list);

	_setup_assoc_cond_limits(assoc_cond, &extra, 0);

	/* if this changes you will need to edit the corresponding enum */
	char *assoc_req_inx[] = {
		"id_assoc",
		"user",
		"acct",
		"`partition`",
		"parent_acct",
	};
	enum {
		ASSOC_REQ_ID,
		ASSOC_REQ_USER,
		ASSOC_REQ_ACCT,
		ASSOC_REQ_PART,
		ASSOC_REQ_PARENT,
		ASSOC_REQ_COUNT
	};

	xfree(tmp);
	xstrfmtcat(tmp, "%s", assoc_req_inx[i]);
	for(i=1; i<ASSOC_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", assoc_req_inx[i]);
	}

	if (assoc_cond &&
	    assoc_cond->cluster_list && list_count(assoc_cond->cluster_list))
		use_cluster_list = assoc_cond->cluster_list;
	else
		slurm_mutex_lock(&as_mysql_cluster_list_lock);

	itr = list_iterator_create(use_cluster_list);
	while ((cluster_name = list_next(itr))) {
		/* only get the account associations */
		if (query)
			xstrcat(query, " union ");
		xstrfmtcat(query, "select distinct %s, '%s' as cluster "
			   "from \"%s_%s\" %s && user='' && lft=(rgt-1) ",
			   tmp, cluster_name, cluster_name,
			   assoc_table, extra);
	}
	list_iterator_destroy(itr);
	if (use_cluster_list == as_mysql_cluster_list)
		slurm_mutex_unlock(&as_mysql_cluster_list_lock);

	if (query)
		xstrcat(query, " order by cluster, acct;");

	xfree(tmp);
	xfree(extra);
	DB_DEBUG(DB_QUERY, mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(
		      mysql_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	while ((row = mysql_fetch_row(result))) {
		slurmdb_assoc_rec_t *assoc =
			xmalloc(sizeof(slurmdb_assoc_rec_t));

		list_append(ret_list, assoc);

		assoc->id = SLURMDB_PROBLEM_ACCT_NO_USERS;

		if (row[ASSOC_REQ_USER][0])
			assoc->user = xstrdup(row[ASSOC_REQ_USER]);
		assoc->acct = xstrdup(row[ASSOC_REQ_ACCT]);
		assoc->cluster = xstrdup(row[ASSOC_REQ_COUNT]);

		if (row[ASSOC_REQ_PARENT][0])
			assoc->parent_acct = xstrdup(row[ASSOC_REQ_PARENT]);

		if (row[ASSOC_REQ_PART][0])
			assoc->partition = xstrdup(row[ASSOC_REQ_PART]);
	}
	mysql_free_result(result);

	return rc;
}

extern int as_mysql_user_no_assocs_or_no_uid(
	mysql_conn_t *mysql_conn,
	slurmdb_assoc_cond_t *assoc_cond,
	List ret_list)
{
	int rc = SLURM_SUCCESS;
	char *query = NULL;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	List use_cluster_list = as_mysql_cluster_list;
	ListIterator itr = NULL;
	char *cluster_name = NULL;

	xassert(ret_list);

	query = xstrdup_printf("select name from %s where deleted=0",
			       user_table);
	if (assoc_cond &&
	    assoc_cond->user_list && list_count(assoc_cond->user_list)) {
		int set = 0;
		char *object = NULL;
		xstrcat(query, " && (");
		itr = list_iterator_create(assoc_cond->user_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(query, " || ");
			xstrfmtcat(query, "name='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(query, ")");
	}

	if (!(result = mysql_db_query_ret(
		      mysql_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	if (assoc_cond &&
	    assoc_cond->cluster_list && list_count(assoc_cond->cluster_list))
		use_cluster_list = assoc_cond->cluster_list;
	else
		slurm_mutex_lock(&as_mysql_cluster_list_lock);

	itr = list_iterator_create(use_cluster_list);
	while ((row = mysql_fetch_row(result))) {
		MYSQL_RES *result2 = NULL;
		int cnt = 0;
		slurmdb_assoc_rec_t *assoc = NULL;
		uid_t pw_uid;

		if (uid_from_string (row[0], &pw_uid) < 0) {
			assoc =	xmalloc(sizeof(slurmdb_assoc_rec_t));
			list_append(ret_list, assoc);

			assoc->id = SLURMDB_PROBLEM_USER_NO_UID;
			assoc->user = xstrdup(row[0]);

			continue;
		}

		/* See if we have at least 1 association in the system */
		while ((cluster_name = list_next(itr))) {
			if (query)
				xstrcat(query, " union ");
			xstrfmtcat(query,
				   "select distinct id_assoc from \"%s_%s\" "
				   "where deleted=0 && "
				   "user='%s'",
				   cluster_name, assoc_table, row[0]);
		}
		list_iterator_reset(itr);

		if (query)
			xstrcat(query, " limit 1");

		if (!(result2 = mysql_db_query_ret(
			      mysql_conn, query, 0))) {
			xfree(query);
			rc = SLURM_ERROR;
			break;
		}
		xfree(query);

		cnt = mysql_num_rows(result2);
		mysql_free_result(result2);

		if (cnt)
			continue;

		assoc =	xmalloc(sizeof(slurmdb_assoc_rec_t));
		list_append(ret_list, assoc);

		assoc->id = SLURMDB_PROBLEM_USER_NO_ASSOC;
		assoc->user = xstrdup(row[0]);
	}
	mysql_free_result(result);

	list_iterator_destroy(itr);
	if (use_cluster_list == as_mysql_cluster_list)
		slurm_mutex_unlock(&as_mysql_cluster_list_lock);

	return rc;
}
