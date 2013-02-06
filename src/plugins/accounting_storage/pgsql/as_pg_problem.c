/*****************************************************************************\
 *  as_pg_problem.c - accounting interface to pgsql - problems related 
 *  functions.
 *
 *  $Id: as_pg_problem.c 13061 2008-01-22 21:23:56Z da $
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
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

#include "as_pg_common.h"


/* get problem accounts without associations */
static int
_get_acct_no_assocs(pgsql_conn_t *pg_conn, slurmdb_association_cond_t *assoc_q,
		    List ret_list)
{
	DEF_VARS;
	int rc = SLURM_SUCCESS;

	xassert(ret_list);

	query = xstrdup_printf("SELECT name FROM %s WHERE deleted=0",
			       acct_table);
	if (assoc_q)
		concat_cond_list(assoc_q->acct_list, NULL, "name", &query);

	result = DEF_QUERY_RET;
	if (!result)
		return SLURM_ERROR;

	FOR_EACH_ROW {
		PGresult *result2;
		slurmdb_association_rec_t *assoc = NULL;
		FOR_EACH_CLUSTER(assoc_q->cluster_list) {
			if (query)
				xstrcat(query, " UNION ");
			xstrfmtcat(query,
				   "SELECT id_assoc FROM %s.%s WHERE "
				   "deleted=0 AND acct='%s'",
				   cluster_name, assoc_table, ROW(0));
		} END_EACH_CLUSTER;
		xstrcat(query, " LIMIT 1;");
		result2 = DEF_QUERY_RET;
		if (!result2) {
			rc = SLURM_ERROR;
			break;
		}
		if (PQntuples(result2) == 0) {
			assoc =	xmalloc(sizeof(slurmdb_association_rec_t));
			list_append(ret_list, assoc);
			assoc->id = SLURMDB_PROBLEM_ACCT_NO_ASSOC;
			assoc->acct = xstrdup(ROW(0));
		}
		PQclear(result2);
	} END_EACH_ROW;
	PQclear(result);

	return rc;
}

/* get problem account without users */
static int
_get_acct_no_users(pgsql_conn_t *pg_conn, slurmdb_association_cond_t *assoc_q,
		   List ret_list)
{
	DEF_VARS;
	int rc = SLURM_SUCCESS;
	char *cond = NULL;
	char *ga_fields = "id_assoc,user_name,acct, partition, parent_acct";
	enum {
		F_ID,
		F_USER,
		F_ACCT,
		F_PART,
		F_PARENT,
		F_COUNT
	};

	xassert(ret_list);
	
	if (assoc_q)
		concat_cond_list(assoc_q->acct_list, NULL, "acct", &cond);

	FOR_EACH_CLUSTER(assoc_q->cluster_list) {
		/* only get the account associations without children assoc */
		if (query)
			xstrcat(query, " UNION ");
		xstrfmtcat(query, "SELECT DISTINCT %s, '%s' AS cluster "
			   "FROM %s.%s WHERE deleted=0 AND user_name='' "
			   "AND lft=(rgt-1) %s", ga_fields, cluster_name,
			   cluster_name, assoc_table, cond ?: "");
	} END_EACH_CLUSTER;
	xfree(cond);
	xstrcat(query, " ORDER BY cluster, acct;");
	result = DEF_QUERY_RET;
	if (!result)
		return SLURM_ERROR;

	FOR_EACH_ROW {
		slurmdb_association_rec_t *assoc =
			xmalloc(sizeof(slurmdb_association_rec_t));
		list_append(ret_list, assoc);
		assoc->id = SLURMDB_PROBLEM_ACCT_NO_USERS;
/* 		if (ROW(F_USER)[0]) */
/* 			assoc->user = xstrdup(ROW(F_USER)); */
		assoc->acct = xstrdup(ROW(F_ACCT));
		assoc->cluster = xstrdup(ROW(F_COUNT));
		if (ROW(F_PARENT)[0])
			assoc->parent_acct = xstrdup(ROW(F_PARENT));
/* 		if (ROW(F_PART)[0]) */
/* 			assoc->partition = xstrdup(ROW(F_PART)); */
	} END_EACH_ROW;
	PQclear(result);
	return rc;
}


/* get problem users without assoc or uid */
static int
_get_user_no_assocs_or_no_uid(pgsql_conn_t *pg_conn,
			      slurmdb_association_cond_t *assoc_q,
			      List ret_list)
{
	DEF_VARS;
	int rc = SLURM_SUCCESS;

	xassert(ret_list);

	query = xstrdup_printf("SELECT name FROM %s WHERE deleted=0",
			       user_table);
	if (assoc_q)
		concat_cond_list(assoc_q->user_list, NULL, "name", &query);

	result = DEF_QUERY_RET;
	if (!result)
		return SLURM_ERROR;

	FOR_EACH_ROW {
		PGresult *result2 = NULL;
		slurmdb_association_rec_t *assoc = NULL;
		uid_t pw_uid;
		char *name = ROW(0);

		if (uid_from_string (name, &pw_uid) < 0) {
			assoc =	xmalloc(sizeof(slurmdb_association_rec_t));
			list_append(ret_list, assoc);
			assoc->id = SLURMDB_PROBLEM_USER_NO_UID;
			assoc->user = xstrdup(name);
			continue;
		}

		FOR_EACH_CLUSTER(assoc_q->cluster_list) {
			if (query)
				xstrcat(query, " UNION ");
			xstrfmtcat(query, "SELECT id_assoc FROM %s.%s WHERE "
				   "deleted=0 AND user_name='%s' ",
				   cluster_name, assoc_table, name);
		} END_EACH_CLUSTER;
		xstrcat(query, " LIMIT 1;");
		result2 = DEF_QUERY_RET;
		if (!result2) {
			rc = SLURM_ERROR;
			break;
		}
		if (PQntuples(result2) == 0) {
			assoc =	xmalloc(sizeof(slurmdb_association_rec_t));
			list_append(ret_list, assoc);
			assoc->id = SLURMDB_PROBLEM_USER_NO_ASSOC;
			assoc->user = xstrdup(name);
		}
		PQclear(result2);
	} END_EACH_ROW;
	PQclear(result);
	return rc;
}

/*
 * as_pg_get_problems - get problems in accouting data
 *
 * IN pg_conn: database connection
 * IN uid: user performing the get operation
 * IN assoc_q: associations to check
 * RET: list of problems
 */
extern List
as_pg_get_problems(pgsql_conn_t *pg_conn, uid_t uid,
		   slurmdb_association_cond_t *assoc_q)
{
	List ret_list = NULL;

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return NULL;

	ret_list = list_create(slurmdb_destroy_association_rec);

	if (_get_acct_no_assocs(pg_conn, assoc_q, ret_list)
	   != SLURM_SUCCESS)
		goto end_it;

	if (_get_acct_no_users(pg_conn, assoc_q, ret_list)
	   != SLURM_SUCCESS)
		goto end_it;

	if (_get_user_no_assocs_or_no_uid(pg_conn, assoc_q, ret_list)
	   != SLURM_SUCCESS)
		goto end_it;

end_it:
	return ret_list;
}
