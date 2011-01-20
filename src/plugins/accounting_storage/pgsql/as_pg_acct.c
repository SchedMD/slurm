/*****************************************************************************\
 *  as_pg_acct.c - accounting interface to pgsql - account related functions.
 *
 *  $Id: as_pg_acct.c 13061 2008-01-22 21:23:56Z da $
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#include "as_pg_common.h"

/* shared table, in schema "public" */
static char *acct_table_name = "acct_table";
char *acct_table = "public.acct_table";
static storage_field_t acct_table_fields[] = {
	{ "creation_time", "INTEGER NOT NULL" },
	{ "mod_time", "INTEGER DEFAULT 0 NOT NULL" },
	{ "deleted", "INTEGER DEFAULT 0" },
	{ "name", "TEXT NOT NULL" },
	{ "description", "TEXT NOT NULL" },
	{ "organization", "TEXT NOT NULL" },
	{ NULL, NULL}
};
static char *acct_table_constraints =
	","
	"PRIMARY KEY (name)"
	")";

static int
_create_function_add_acct(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION public.add_acct "
		"(rec %s) RETURNS VOID AS $$ "
		"BEGIN LOOP "
		"  BEGIN "
		"    INSERT INTO %s VALUES (rec.*); RETURN;"
		"  EXCEPTION WHEN UNIQUE_VIOLATION THEN"
		"    UPDATE %s SET "
		"      (deleted, mod_time, description, organization) = "
		"      (0, rec.mod_time, rec.description, rec.organization) "
		"      WHERE name=rec.name;"
		"    IF FOUND THEN RETURN; END IF;"
		"  END; "
		"END LOOP; END; $$ LANGUAGE PLPGSQL;",
		acct_table, acct_table,	acct_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _get_account_coords - fill in all the users that are coordinator for
 *  this account. Also fill in coordinators from parent accounts.
 */
static int
_get_account_coords(pgsql_conn_t *pg_conn, slurmdb_account_rec_t *acct)
{
	DEF_VARS;
	slurmdb_coord_rec_t *coord = NULL;

	if(!acct) {
		error("as/pg: _get_account_coords: account not given");
		return SLURM_ERROR;
	}

	if(!acct->coordinators)
		acct->coordinators = list_create(slurmdb_destroy_coord_rec);

	/* get direct coords */
	query = xstrdup_printf("SELECT user_name FROM %s "
			       "WHERE acct='%s' AND deleted=0",
			       acct_coord_table, acct->name);
	result = DEF_QUERY_RET;
	if(!result)
		return SLURM_ERROR;

	FOR_EACH_ROW {
		coord = xmalloc(sizeof(slurmdb_coord_rec_t));
		list_append(acct->coordinators, coord);
		coord->name = xstrdup(ROW(0));
		coord->direct = 1;
	} END_EACH_ROW;
	PQclear(result);

	/* get parent account coords */
	FOR_EACH_CLUSTER(NULL) {
		if (query)
			xstrcat(query, " UNION ");
		xstrfmtcat(query, "SELECT DISTINCT t0.user_name "
			   "FROM %s AS t0, %s.%s AS t1, %s.%s AS t2 "
			   "WHERE (t1.acct='%s' AND t1.user_name='' "
			   "  AND (t1.lft>t2.lft AND t1.rgt < t2.rgt)) "
			   "  AND t0.deleted=0 AND t0.acct=t2.acct "
			   "  AND t2.acct != '%s'",
			   acct_coord_table, cluster_name, assoc_table,
			   cluster_name, assoc_table, acct->name, acct->name);

	} END_EACH_CLUSTER;

	result = DEF_QUERY_RET;
	if(!result)
		return SLURM_ERROR;

	FOR_EACH_ROW {
		coord = xmalloc(sizeof(slurmdb_coord_rec_t));
		list_append(acct->coordinators, coord);
		coord->name = xstrdup(ROW(0));
		coord->direct = 0;
	} END_EACH_ROW;
	PQclear(result);
	return SLURM_SUCCESS;
}

/*
 * check_acct_tables - check account related tables and functions
 * IN pg_conn: database connection
 * IN user: database owner
 * RET: error code
 */
extern int
check_acct_tables(PGconn *db_conn)
{
	int rc;

	rc = check_table(db_conn, "public", acct_table_name, acct_table_fields,
			 acct_table_constraints);
	rc |= _create_function_add_acct(db_conn);
	return rc;
}

/*
 * as_pg_add_accts - add accounts
 *
 * IN pg_conn: database connection
 * IN uid: user performing the add operation
 * IN acct_list: accounts to add
 * RET: error code
 */
extern int
as_pg_add_accts(pgsql_conn_t *pg_conn, uint32_t uid, List acct_list)
{
	ListIterator itr = NULL;
	slurmdb_account_rec_t *object = NULL;
	List assoc_list = NULL;
	int rc = SLURM_SUCCESS;
	char *user_name = NULL, *query = NULL, *txn_query = NULL;
	char *rec = NULL, *info = NULL;
	time_t now = time(NULL);

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	assoc_list = list_create(slurmdb_destroy_association_rec);
	user_name = uid_to_string((uid_t) uid);

	itr = list_iterator_create(acct_list);
	while((object = list_next(itr))) {
		if(!object->name || !object->description
		   || !object->organization) {
			error("as/pg: add_accts: We need an account name, "
			      "description, and organization to add. %s %s %s",
			      object->name, object->description,
			      object->organization);
			rc = SLURM_ERROR;
			continue;
		}
		/* order of vals must match structure of acct_table */
		rec = xstrdup_printf("(%ld, %ld, 0, '%s', '%s', '%s')", now,
				     now, object->name, object->description,
				     object->organization);
		query = xstrdup_printf("SELECT public.add_acct(%s);", rec);
		xfree(rec);
		rc = DEF_QUERY_RET_RC;
		if(rc != SLURM_SUCCESS) {
			error("as/pg: couldn't add acct %s", object->name);
			continue;
		}

		info = xstrdup_printf("description='%s', organization='%s'",
				      object->description,
				      object->organization);
		if(txn_query)
			xstrfmtcat(txn_query,
				   ", (%ld, %u, '%s', '%s', $$%s$$)",
				   now, DBD_ADD_ACCOUNTS, object->name,
				   user_name, info);
		else
			xstrfmtcat(txn_query,
				   "INSERT INTO %s "
				   "(timestamp, action, name, actor, info) "
				   "VALUES (%ld, %u, '%s', '%s', $$%s$$)",
				   txn_table,
				   now, DBD_ADD_ACCOUNTS, object->name,
				   user_name, info);
		xfree(info);

		if(!object->assoc_list)
			continue;

		list_transfer(assoc_list, object->assoc_list);
	}
	list_iterator_destroy(itr);
	xfree(user_name);

	if(rc == SLURM_SUCCESS) {
		if(txn_query) {
			xstrcat(txn_query, ";");
			rc = pgsql_db_query(pg_conn->db_conn, txn_query);
			xfree(txn_query);
			if(rc != SLURM_SUCCESS) {
				error("as/pg: add_accts: couldn't add txn");
				rc = SLURM_SUCCESS;
			}
		}
	} else
		xfree(txn_query);

	if(rc == SLURM_SUCCESS && list_count(assoc_list)) {
		if(acct_storage_p_add_associations(pg_conn, uid, assoc_list)
		   != SLURM_SUCCESS) {
			error("as/pg: add_accts: problem adding account "
			      "associations");
			rc = SLURM_ERROR;
		}
	}
	list_destroy(assoc_list);

	return rc;
}

/*
 * as_pg_modify_accounts - modify accounts
 *
 * IN pg_conn: database connection
 * IN uid: user performing the modify operation
 * IN acct_cond: accounts to modify
 * IN acct: attribute of accounts after modification
 * RET: list of accounts modified
 */
extern List
as_pg_modify_accounts(pgsql_conn_t *pg_conn, uint32_t uid,
		      slurmdb_account_cond_t *acct_cond,
		      slurmdb_account_rec_t *acct)
{
	DEF_VARS;
	List ret_list = NULL;
	int rc = SLURM_SUCCESS;
	char *object = NULL, *user_name = NULL;
	char *vals = NULL, *cond = NULL, *name_char = NULL;
	time_t now = time(NULL);

	if(!acct_cond || !acct) {
		error("as/pg: modify_accounts: we need something to change");
		return NULL;
	}
	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return NULL;

	if(acct_cond->assoc_cond)
		concat_cond_list(acct_cond->assoc_cond->acct_list,
				 NULL, "name", &cond);
	concat_cond_list(acct_cond->description_list,
			 NULL, "description", &cond);
	concat_cond_list(acct_cond->organization_list,
			 NULL, "organization", &cond);
	if (!cond) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		error("as/pg: modify_accounts: no condition given");
		return NULL;
	}

	if(acct->description)
		xstrfmtcat(vals, ", description='%s'", acct->description);
	if(acct->organization)
		xstrfmtcat(vals, ", organization='%s'", acct->organization);
	if(!vals) {
		xfree(cond);
		errno = SLURM_NO_CHANGE_IN_DATA;
		error("as/pg: modify_accounts: no new values given");
		return NULL;
	}

	query = xstrdup_printf("SELECT name FROM %s WHERE deleted=0 %s;",
			       acct_table, cond);
	xfree(cond);
	result = DEF_QUERY_RET;
	if(!result) {
		xfree(vals);
		return NULL;
	}

	rc = 0;
	ret_list = list_create(slurm_destroy_char);
	FOR_EACH_ROW {
		object = xstrdup(ROW(0));
		list_append(ret_list, object);
		if(!rc) {
			xstrfmtcat(name_char, "(name='%s'", object);
			rc = 1;
		} else  {
			xstrfmtcat(name_char, " OR name='%s'", object);
		}

	} END_EACH_ROW;
	PQclear(result);

	if(!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		debug3("as/pg: modify_accounts: didn't effect anything");
		xfree(vals);
		return ret_list;
	}
	xstrcat(name_char, ")");

	user_name = uid_to_string((uid_t) uid);
	rc = pgsql_modify_common(pg_conn, DBD_MODIFY_ACCOUNTS, now, "",
				 user_name, acct_table, name_char, vals);
	xfree(user_name);
	xfree(name_char);
	xfree(vals);

	if (rc == SLURM_ERROR) {
		error("as/pg: couldn't modify accounts");
		list_destroy(ret_list);
		errno = SLURM_ERROR;
		ret_list = NULL;
	}
	return ret_list;
}

/* whether specified accounts has jobs in db */
/* assoc_cond format: "t2.acct=name OR t2.acct=name ..." */
static int
_acct_has_jobs(pgsql_conn_t *pg_conn, char *assoc_cond)
{
	DEF_VARS;
	int has_jobs = 0;

	FOR_EACH_CLUSTER(NULL) {
		if (query)
			xstrcat(query, " UNION ");
		xstrfmtcat(query, "SELECT t0.id_assoc FROM %s.%s AS t0, "
			   "%s.%s AS t1, %s.%s AS t2 WHERE "
			   "(t1.lft BETWEEN t2.lft AND t2.rgt) AND (%s) "
			   "AND t0.id_assoc=t1.id_assoc",
			   cluster_name, job_table, cluster_name, assoc_table,
			   cluster_name, assoc_table, assoc_cond);
	} END_EACH_CLUSTER;
	xstrcat(query, " LIMIT 1;");
	result = DEF_QUERY_RET;
	if (result) {
		has_jobs = (PQntuples(result) != 0);
		PQclear(result);
	}
	return has_jobs;
}


/* get running jobs of specified accounts */
/* assoc_cond format: "t2.acct=name OR t2.acct=name ..." */
static List
_get_acct_running_jobs(pgsql_conn_t *pg_conn, char *assoc_cond)
{
	DEF_VARS;
	List job_list = NULL;
	char *job = NULL;
	char *fields = "t0.id_job,t1.acct,t1.user_name,t1.partition";

	FOR_EACH_CLUSTER(NULL) {
		if (query)
			xstrcat(query, " UNION ");
		xstrfmtcat(
			query, "SELECT DISTINCT %s, '%s' FROM %s.%s AS t0, "
			"%s.%s AS t1, %s.%s AS t2 WHERE "
			"(t1.lft BETWEEN t2.lft AND t2.rgt) AND (%s) AND "
			"t0.id_assoc=t1.id_assoc AND t0.state=%d AND "
			"t0.time_end=0", fields, cluster_name, cluster_name,
			job_table, cluster_name, assoc_table, cluster_name,
			assoc_table, assoc_cond, JOB_RUNNING);
	} END_EACH_CLUSTER;
	result = DEF_QUERY_RET;
	if (!result)
		return NULL;

	FOR_EACH_ROW {
		if (ISEMPTY(2)) {
			error("how could job %s running on non-user "
			      "assoc <%s, %s, '', ''>", ROW(0),
			      ROW(4), ROW(1));
			continue;
		}
		job = xstrdup_printf(
			"JobID = %-10s C = %-10s A = %-10s U = %-9s",
			ROW(0), ROW(4), ROW(1), ROW(2));
		if(!ISEMPTY(3))
			xstrfmtcat(job, " P = %s", ROW(3));
		if (!job_list)
			job_list = list_create(slurm_destroy_char);
		list_append(job_list, job);
	} END_EACH_ROW;
	PQclear(result);
	return job_list;
}

/*
 * handle related associations:
 * 1. mark assoc usages as deleted
 * 2. delete assocs that does not has job
 * 3. mark other assocs as deleted
 * assoc_cond format: "t2.acct=name OR t2.acct=name..."
 */
static int
_cluster_remove_acct_assoc(pgsql_conn_t *pg_conn, char *cluster,
			   time_t now, char *assoc_cond, int has_jobs)
{
	DEF_VARS;
	slurmdb_association_rec_t *rem_assoc = NULL;
	char *assoc_char = NULL;
	int rc = SLURM_SUCCESS;
	uint32_t smallest_lft = 0xFFFFFFFF, lft;

	query = xstrdup_printf (
		"SELECT DISTINCT t1.id_assoc,t1.lft FROM %s.%s AS t1, %s.%s AS t2 "
		"WHERE t1.deleted=0 AND t2.deleted=0 AND (%s) AND "
		"t1.creation_time>%d "
		"AND (t1.lft BETWEEN t2.lft AND t2.rgt);",
		cluster, assoc_table, cluster, assoc_table, assoc_cond,
		(int)(now - DELETE_SEC_BACK));
	result = DEF_QUERY_RET;
	if (!result)
		return SLURM_ERROR;

	if (PQntuples(result) == 0) {
		PQclear(result);
		return SLURM_SUCCESS;
	}

	FOR_EACH_ROW {
		if (assoc_char)
			xstrfmtcat(assoc_char, " OR id_assoc=%s", ROW(0));
		else
			xstrfmtcat(assoc_char, "id_assoc=%s", ROW(0));

		lft = atoi(ROW(1));
		if (lft < smallest_lft)
			smallest_lft = lft;

		rem_assoc = xmalloc(sizeof(slurmdb_association_rec_t));
		rem_assoc->id = atoi(ROW(0));
		rem_assoc->cluster = xstrdup(cluster);
		if (addto_update_list(pg_conn->update_list,
				      SLURMDB_REMOVE_ASSOC,
				      rem_assoc) != SLURM_SUCCESS)
			error("could not add to the update list");
		if (! has_jobs) {
			xstrfmtcat(query, "SELECT %s.remove_assoc(%s);",
				   cluster, ROW(0));
		}
	} END_EACH_ROW;
	PQclear(result);

	/* mark usages as deleted */
	cluster_delete_assoc_usage(pg_conn, cluster, now, assoc_char);

	if (!has_jobs && query) {
		rc = DEF_QUERY_RET_RC;
		if (rc != SLURM_SUCCESS) {
			error("failed to remove account assoc");
		}
	}

	if(rc == SLURM_SUCCESS)
		rc = pgsql_get_modified_lfts(pg_conn,
					     cluster, smallest_lft);
	if (rc != SLURM_SUCCESS) {
		reset_pgsql_conn(pg_conn);
		return rc;
	}

	/* update associations to clear the limits */
	query = xstrdup_printf(
		"UPDATE %s.%s SET mod_time=%d, deleted=1, def_qos_id=NULL, "
		"shares=1, max_jobs=NULL, max_nodes_pj=NULL, max_wall_pj=NULL, "
		"max_cpu_mins_pj=NULL WHERE (%s);", cluster, assoc_table,
		(int)now, assoc_char);
	xfree(assoc_char);
	rc = DEF_QUERY_RET_RC;

	return rc;
}

/*
 * as_pg_remove_accts - remove accounts
 *
 * IN pg_conn: database connection
 * IN uid: user performing the remove operation
 * IN acct_cond: accounts to remove
 * RET: list of accounts removed
 */
extern List
as_pg_remove_accts(pgsql_conn_t *pg_conn, uint32_t uid,
		   slurmdb_account_cond_t *acct_cond)
{
	DEF_VARS;
	List ret_list = NULL, tmp_list = NULL;
	char *user_name = NULL, *cond = NULL, *name_char = NULL,
		*assoc_char = NULL;
	time_t now = time(NULL);
	int rc = SLURM_SUCCESS, has_jobs;

	if(!acct_cond) {
		error("as/pg: remove_accts: we need something to remove");
		return NULL;
	}
	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return NULL;

	if(acct_cond->assoc_cond)
		concat_cond_list(acct_cond->assoc_cond->acct_list,
				 NULL, "name", &cond);
	concat_cond_list(acct_cond->description_list,
			 NULL, "description", &cond);
	concat_cond_list(acct_cond->organization_list,
			 NULL, "organization", &cond);
	if(!cond) {
		error("as/pg: remove_accts: nothing to remove");
		return NULL;
	}

	query = xstrdup_printf(
		"SELECT name FROM %s WHERE deleted=0 %s;",
		acct_table, cond);
	xfree(cond);
	result = DEF_QUERY_RET;
	if(!result)
		return NULL;

	rc = 0;
	ret_list = list_create(slurm_destroy_char);
	FOR_EACH_ROW {
		char *object = xstrdup(ROW(0));
		list_append(ret_list, object);
		if(!rc) {
			xstrfmtcat(name_char, "name='%s'", object);
			xstrfmtcat(assoc_char, "t2.acct='%s'", object);
			rc = 1;
		} else  {
			xstrfmtcat(name_char, " OR name='%s'", object);
			xstrfmtcat(assoc_char, " OR t2.acct='%s'", object);
		}
	} END_EACH_ROW;
	PQclear(result);

	if(!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		debug3("as/pg: remove_accts: didn't effect anything");
		return ret_list;
	}

	/* remove these accounts from the coord's that have it */
	tmp_list = acct_storage_p_remove_coord(pg_conn, uid, ret_list, NULL);
	if(tmp_list)
		list_destroy(tmp_list);

	/* if there are running jobs of the accounts, return the jobs */
	tmp_list = _get_acct_running_jobs(pg_conn, assoc_char);
	if (tmp_list) {
		errno = ESLURM_JOBS_RUNNING_ON_ASSOC;
		list_destroy(ret_list);
		reset_pgsql_conn(pg_conn);
		return tmp_list;
	}

	/* delete recently added accounts */
	has_jobs = _acct_has_jobs(pg_conn, assoc_char);
	if (! has_jobs ) {
		query = xstrdup_printf(
			"DELETE FROM %s WHERE creation_time>%d AND (%s);",
			acct_table, (int)(now - DELETE_SEC_BACK), name_char);
	}
	/* mark others as deleted */
	xstrfmtcat(query, "UPDATE %s SET mod_time=%ld, deleted=1 "
		   "WHERE deleted=0 "
		   "AND (%s);", acct_table, (long)now, name_char);
	user_name = uid_to_string((uid_t) uid);
	xstrfmtcat(query, "INSERT INTO %s (timestamp, action, name, actor) "
		   "VALUES (%ld, %d, $$%s$$, '%s');", txn_table, (long)now,
		   DBD_REMOVE_ACCOUNTS, name_char, user_name);
	xfree(user_name);
	rc = DEF_QUERY_RET_RC;
	if (rc != SLURM_SUCCESS) {
		reset_pgsql_conn(pg_conn);
		list_destroy(ret_list);
		ret_list = NULL;
		goto out;
	}

	/* TODO: this may leave sub-accts without assoc */
	/* handle associations */
	FOR_EACH_CLUSTER(NULL) {
		rc = _cluster_remove_acct_assoc(pg_conn, cluster_name,
						now, assoc_char, has_jobs);
		if (rc != SLURM_SUCCESS)
			break;
	} END_EACH_CLUSTER;

	if (rc != SLURM_SUCCESS) {
		reset_pgsql_conn(pg_conn);
		list_destroy(ret_list);
		ret_list = NULL;
	}

out:
	xfree(name_char);
	xfree(assoc_char);
	return ret_list;
}

/*
 * as_pg_get_accts - get accounts
 *
 * IN pg_conn: database connection
 * IN uid: user performing the get operation
 * IN acct_cond: accounts to get
 * RET: list of accounts
 */
extern List
as_pg_get_accts(pgsql_conn_t *pg_conn, uid_t uid,
		slurmdb_account_cond_t *acct_cond)
{
	DEF_VARS;
	char *cond = NULL;
	List acct_list = NULL;
	ListIterator itr = NULL;
	int set=0, is_admin=1;
	slurmdb_user_rec_t user;	/* no need to free lists */

	char *ga_fields = "name, description, organization";
	enum {
		F_NAME,
		F_DESC,
		F_ORG,
		F_COUNT
	};

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return NULL;

	if (check_user_op(pg_conn, uid, PRIVATE_DATA_ACCOUNTS,
			  &is_admin, &user) != SLURM_SUCCESS) {
		error("as/pg: user(%u) not found in db", uid);
		errno = ESLURM_USER_ID_MISSING;
		return NULL;
	}

	if (!is_admin && ! is_user_any_coord(pg_conn, &user)) {
		errno = ESLURM_ACCESS_DENIED;
		return NULL;
	}

	if(!acct_cond) {
		xstrcat(cond, "WHERE deleted=0");
		goto empty;
	}

	if(acct_cond->with_deleted)
		xstrcat(cond, "WHERE (deleted=0 OR deleted=1)");
	else
		xstrcat(cond, "WHERE deleted=0");

	if(acct_cond->assoc_cond)
		concat_cond_list(acct_cond->assoc_cond->acct_list,
				 NULL, "name", &cond);
	concat_cond_list(acct_cond->description_list,
			 NULL, "description", &cond);
	concat_cond_list(acct_cond->organization_list,
			 NULL, "organization", &cond);

empty:
	if(!is_admin) {
		slurmdb_coord_rec_t *coord = NULL;
		set = 0;
		itr = list_iterator_create(user.coord_accts);
		while((coord = list_next(itr))) {
			if(set) {
				xstrfmtcat(cond, " OR name='%s'",
					   coord->name);
			} else {
				set = 1;
				xstrfmtcat(cond, " AND (name='%s'",
					   coord->name);
			}
		}
		list_iterator_destroy(itr);
		if(set)
			xstrcat(cond,")");
	}

	query = xstrdup_printf("SELECT %s FROM %s %s",
			       ga_fields, acct_table, cond);
	xfree(cond);
	result = DEF_QUERY_RET;
	if (!result)
		return NULL;

	acct_list = list_create(slurmdb_destroy_account_rec);

	if(acct_cond && acct_cond->with_assocs) {
		if(!acct_cond->assoc_cond)
			acct_cond->assoc_cond = xmalloc(
				sizeof(slurmdb_association_cond_t));
		else if(acct_cond->assoc_cond->acct_list)
			list_destroy(acct_cond->assoc_cond->acct_list);
		acct_cond->assoc_cond->acct_list = list_create(NULL);
	}

	FOR_EACH_ROW {
		slurmdb_account_rec_t *acct = xmalloc(sizeof(slurmdb_account_rec_t));
		list_append(acct_list, acct);

		acct->name =  xstrdup(ROW(F_NAME));
		acct->description = xstrdup(ROW(F_DESC));
		acct->organization = xstrdup(ROW(F_ORG));
		if(acct_cond && acct_cond->with_coords)
			_get_account_coords(pg_conn, acct);
		if(acct_cond && acct_cond->with_assocs) {
			list_append(acct_cond->assoc_cond->acct_list,
				    acct->name);
		}
	} END_EACH_ROW;
	PQclear(result);

	/* get associations */
	if(acct_cond && acct_cond->with_assocs &&
	   list_count(acct_cond->assoc_cond->acct_list)) {
		ListIterator assoc_itr = NULL;
		slurmdb_account_rec_t *acct = NULL;
		slurmdb_association_rec_t *assoc = NULL;
		List assoc_list = acct_storage_p_get_associations(
			pg_conn, uid, acct_cond->assoc_cond);

		if(!assoc_list) {
			error("as/pg: get_accounts: no associations");
			return acct_list;
		}

		itr = list_iterator_create(acct_list);
		assoc_itr = list_iterator_create(assoc_list);
		while((acct = list_next(itr))) {
			while((assoc = list_next(assoc_itr))) {
				if(strcmp(assoc->acct, acct->name))
					continue;

				if(!acct->assoc_list)
					acct->assoc_list = list_create(
						slurmdb_destroy_association_rec);
				list_append(acct->assoc_list, assoc);
				list_remove(assoc_itr);
			}
			list_iterator_reset(assoc_itr);
			if(!acct->assoc_list) /* problem acct */
				list_remove(itr);
		}
		list_iterator_destroy(itr);
		list_iterator_destroy(assoc_itr);
		list_destroy(assoc_list);
	}
	return acct_list;
}
