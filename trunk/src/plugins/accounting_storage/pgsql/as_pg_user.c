/*****************************************************************************\
 *  as_pg_user.c - accounting interface to pgsql - user related functions.
 *
 *  $Id: as_pg_user.c 13061 2008-01-22 21:23:56Z da $
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

/* shared table */
char *acct_coord_table_name = "acct_coord_table";
char *acct_coord_table = "public.acct_coord_table";
static storage_field_t acct_coord_table_fields[] = {
	{ "creation_time", "INTEGER NOT NULL" },
	{ "mod_time", "INTEGER DEFAULT 0 NOT NULL" },
	{ "deleted", "INTEGER DEFAULT 0" },
	{ "acct", "TEXT NOT NULL" },
	{ "user_name", "TEXT NOT NULL" },
	{ NULL, NULL}
};
static char *acct_coord_table_constraints = ", "
	"PRIMARY KEY (acct, user_name) "
	")";

/* shared table */
static char *user_table_name = "user_table";
char *user_table = "public.user_table";
static storage_field_t user_table_fields[] = {
	{ "creation_time", "INTEGER NOT NULL" },
	{ "mod_time", "INTEGER DEFAULT 0 NOT NULL" },
	{ "deleted", "INTEGER DEFAULT 0" },
	{ "name", "TEXT NOT NULL" },
	{ "default_acct", "TEXT NOT NULL" },
	{ "default_wckey", "TEXT DEFAULT '' NOT NULL" },
	{ "admin_level", "INTEGER DEFAULT 1 NOT NULL" },
	{ NULL, NULL}
};
static char *user_table_constraints = ", "
	"PRIMARY KEY (name) "
	")";

/*
 * _create_function_add_user - create a PL/pgSQL function to add user
 *
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_add_user(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION public.add_user "
		"(rec %s) RETURNS VOID AS $$"
		"BEGIN LOOP "
		"  BEGIN "
		"    INSERT INTO %s VALUES (rec.*); RETURN;"
		"  EXCEPTION WHEN UNIQUE_VIOLATION THEN"
		"    UPDATE %s SET"
		"        (deleted, mod_time, default_acct, "
		"         admin_level, default_wckey) = "
		"        (0, rec.mod_time, rec.default_acct, "
		"         rec.admin_level, rec.default_wckey) "
		"      WHERE name=rec.name;"
		"    IF FOUND THEN RETURN; END IF;"
		"  END; "
		"END LOOP; END; $$ LANGUAGE PLPGSQL;",
		user_table, user_table, user_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _create_function_add_coord -  create a PL/pgSQL function to add coord
 *
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_add_coord(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION public.add_coord "
		"(rec %s) RETURNS VOID AS $$"
		"BEGIN LOOP "
		"  BEGIN "
		"    INSERT INTO %s VALUES (rec.*); RETURN;"
		"  EXCEPTION WHEN UNIQUE_VIOLATION THEN"
		"    UPDATE %s SET (deleted, mod_time) = "
		"        (0, rec.mod_time) "
		"      WHERE acct=rec.acct AND "
		"        user_name=rec.user_name;"
		"    IF FOUND THEN RETURN; END IF; "
		"  END; "
		"END LOOP; END; $$ LANGUAGE PLPGSQL;",
		acct_coord_table, acct_coord_table, acct_coord_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _create_function_add_coords -  create a PL/pgSQL function to add coords
 *
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_add_coords(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION public.add_coords "
		"(recs %s[]) RETURNS VOID AS $$"
		"DECLARE "
		"  i INTEGER := 1; rec %s;"
		"BEGIN LOOP"
		"  rec := recs[i]; i := i + 1;"
		"  EXIT WHEN rec IS NULL;"
		"  PERFORM public.add_coord(rec); "
		"END LOOP; END; $$ LANGUAGE PLPGSQL;",
		acct_coord_table, acct_coord_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _make_user_record - make a user_table record for add
 *
 * IN object: user record
 * OUT rec: user_table record string
 * OUT txn: transaction info string
 */
static void
_make_user_record(slurmdb_user_rec_t *object, time_t now, char **rec, char **txn)
{
	xfree(*rec);
	/* XXX: order of vals must match structure of USER_TABLE */
	*rec = xstrdup_printf("(%ld, %ld, 0, '%s', '%s'",
			      now, now, object->name, object->default_acct);
	xstrfmtcat(*txn, "default_acct='%s'", object->default_acct);

	if(object->default_wckey) {
		xstrfmtcat(*rec, ", '%s'", object->default_wckey);
		xstrfmtcat(*txn, ", default_wckey='%s'",
			   object->default_wckey);
	} else {
		/* default value of default_wckey is '' */
		xstrcat(*rec, ", ''");
		xstrcat(*txn, ", default_wckey=''");
	}

	if(object->admin_level != SLURMDB_ADMIN_NOTSET) {
		xstrfmtcat(*rec, ", %u)", object->admin_level);
		xstrfmtcat(*txn, ", admin_level=%u",
			   object->admin_level);
	} else {
		/* default value of admin_level is 1 (SLURMDB_ADMIN_NONE) */
		xstrcat(*rec, ", 1)");
		xstrfmtcat(*txn, ", admin_level=%u",
			   SLURMDB_ADMIN_NONE);
	}
}

/*
 * _get_user_coords - fill in all the accounts this user is coordinator over.
 *  Also fill in sub accounts.
 */
static int
_get_user_coords(pgsql_conn_t *pg_conn, slurmdb_user_rec_t *user)
{
	DEF_VARS;
	slurmdb_coord_rec_t *coord = NULL;
	ListIterator itr = NULL;
	char *cond = NULL;

	if(!user) {
		error("as/pg: _get_user_coord: user not given.");
		return SLURM_ERROR;
	}

	if(!user->coord_accts)
		user->coord_accts = list_create(slurmdb_destroy_coord_rec);

	query = xstrdup_printf(
		"SELECT acct FROM %s WHERE user_name='%s' AND deleted=0",
		acct_coord_table, user->name);
	result = DEF_QUERY_RET;
	if (!result)
		return SLURM_ERROR;

	FOR_EACH_ROW {
		coord = xmalloc(sizeof(slurmdb_coord_rec_t));
		list_append(user->coord_accts, coord);
		coord->name = xstrdup(ROW(0));
		coord->direct = 1;
		if (cond)
			xstrcat(cond, " OR ");
		xstrfmtcat(cond, "t2.acct='%s'", ROW(0));
	} END_EACH_ROW;
	PQclear(result);

	if (list_count(user->coord_accts) == 0)
		return SLURM_SUCCESS;

	FOR_EACH_CLUSTER(NULL) {
		if(query)
			xstrcat(query, " UNION ");
		query = xstrdup_printf(
			"SELECT DISTINCT t1.acct FROM %s.%s AS t1, %s.%s AS t2 "
			"WHERE t1.deleted=0 AND t2.deleted=0 AND "
			"t1.user_name='' AND (t1.lft>t2.lft AND t1.rgt<t2.rgt) "
			"AND (%s)", cluster_name, assoc_table, cluster_name,
			assoc_table, cond);
	} END_EACH_CLUSTER;
	xfree(cond);

	if(query) {
		result = DEF_QUERY_RET;
		if(!result)
			return SLURM_ERROR;

		itr = list_iterator_create(user->coord_accts);
		FOR_EACH_ROW {
			char *acct = ROW(0);
			while((coord = list_next(itr))) {
				if(!strcmp(coord->name, acct))
					break;
			}
			list_iterator_reset(itr);
			if(coord) /* already in list */
				continue;

			coord = xmalloc(sizeof(slurmdb_coord_rec_t));
			list_append(user->coord_accts, coord);
			coord->name = xstrdup(acct);
			coord->direct = 0;
		} END_EACH_ROW;
		list_iterator_destroy(itr);
		PQclear(result);
	}
	return SLURM_SUCCESS;
}


/*
 * _make_user_cond - turn user_cond into SQL query condition string
 *
 * IN user_cond: user condition
 * OUT: cond: condition string
 */
static void
_make_user_cond(slurmdb_user_cond_t *user_cond, char **cond)
{
	if(user_cond->assoc_cond)
		concat_cond_list(user_cond->assoc_cond->user_list,
				 NULL, "name", cond);

	if(user_cond->def_acct_list)
		info("here %d", list_count(user_cond->def_acct_list));
	concat_cond_list(user_cond->def_acct_list,
			 NULL, "default_acct", cond);
	concat_cond_list(user_cond->def_wckey_list,
			 NULL, "default_wckey", cond);
	if(user_cond->admin_level != SLURMDB_ADMIN_NOTSET) {
		xstrfmtcat(*cond, " AND admin_level=%u",
			   user_cond->admin_level);
	}
}


static int
_change_user_name(pgsql_conn_t *pg_conn, slurmdb_user_rec_t *user)
{
	char *query = NULL;
	int rc = SLURM_SUCCESS;

	xassert(user->old_name);
	xassert(user->name);

	FOR_EACH_CLUSTER(NULL) {
		// Change assoc_tables
		xstrfmtcat(query, "UPDATE %s.%s SET user='%s' "
			   "WHERE user='%s';", cluster_name, assoc_table,
			   user->name, user->old_name);
		// Change wckey_tables
		xstrfmtcat(query, "UPDATE %s.%s SET user='%s' "
			   "WHERE user='%s';", cluster_name, wckey_table,
			   user->name, user->old_name);
	} END_EACH_CLUSTER;
	// Change coord_tables
	xstrfmtcat(query, "UPDATE %s SET user='%s' WHERE user='%s';",
		   acct_coord_table, user->name, user->old_name);

	rc = DEF_QUERY_RET_RC;
	if(rc != SLURM_SUCCESS)
		reset_pgsql_conn(pg_conn);

	return rc;
}



/*
 * check_user_tables - check user related tables and functions
 * IN pg_conn: database connection
 * RET: error code
 */
extern int
check_user_tables(PGconn *db_conn)
{
	int rc;

	rc = check_table(db_conn, "public", user_table_name, user_table_fields,
			 user_table_constraints);
	rc |= check_table(db_conn, "public", acct_coord_table_name,
			  acct_coord_table_fields,
			  acct_coord_table_constraints);
	rc |= _create_function_add_user(db_conn);
	rc |= _create_function_add_coord(db_conn);
	rc |= _create_function_add_coords(db_conn);
	return rc;
}

/*
 * as_pg_add_users - add users
 *
 * IN pg_conn: database connection
 * IN uid: user performing the add operation
 * IN user_list: users to add
 * RET: error code
 */
extern int
as_pg_add_users(pgsql_conn_t *pg_conn, uint32_t uid, List user_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	slurmdb_user_rec_t *object = NULL;
	char *rec = NULL, *info = NULL, *user_name = NULL,
		*query = NULL, *txn_query = NULL;
	time_t now = time(NULL);
	List assoc_list = list_create(slurmdb_destroy_association_rec);
	List wckey_list = list_create(slurmdb_destroy_wckey_rec);

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	user_name = uid_to_string((uid_t) uid);

	itr = list_iterator_create(user_list);
	while((object = list_next(itr))) {
		if(!object->name || !object->name[0] ||
		   !object->default_acct || !object->default_acct[0]) {
			error("as/pg: add_users: we need a user name and "
			      "default acct to add.");
			rc = SLURM_ERROR;
			continue;
		}

		_make_user_record(object, now, &rec, &info);
		query = xstrdup_printf("SELECT public.add_user(%s);", rec);
		xfree(rec);
		rc = DEF_QUERY_RET_RC;
		if(rc != SLURM_SUCCESS) {
			error("Couldn't add user %s", object->name);
			xfree(info);
			continue;
		}

		/* object moved to update_list, remove from user_list */
		if(addto_update_list(pg_conn->update_list, SLURMDB_ADD_USER,
				     object) == SLURM_SUCCESS)
			list_remove(itr);

		if(txn_query)
			xstrfmtcat(txn_query,
				   ", (%ld, %u, '%s', '%s', $$%s$$)",
				   now, DBD_ADD_USERS, object->name,
				   user_name, info);
		else
			xstrfmtcat(txn_query,
				   "INSERT INTO %s "
				   "(timestamp, action, name, actor, info) "
				   "VALUES (%ld, %u, '%s', '%s', $$%s$$)",
				   txn_table,
				   now, DBD_ADD_USERS, object->name,
				   user_name, info);
		xfree(info);

		if(object->assoc_list)
			list_transfer(assoc_list, object->assoc_list);
		if(object->wckey_list)
			list_transfer(wckey_list, object->wckey_list);
	}
	list_iterator_destroy(itr);
	xfree(user_name);

	if(rc == SLURM_SUCCESS) {
		if(txn_query) {
			xstrcat(txn_query, ";");
			rc = pgsql_db_query(pg_conn->db_conn, txn_query);
			xfree(txn_query);
			if(rc != SLURM_SUCCESS) {
				error("Couldn't add txn");
/* 				rc = SLURM_SUCCESS; */
			}
		}
	} else
		xfree(txn_query);

	if(rc == SLURM_SUCCESS && list_count(assoc_list)) {
		if(acct_storage_p_add_associations(pg_conn, uid, assoc_list)
		   == SLURM_ERROR) {
			error("Problem adding user associations");
			rc = SLURM_ERROR;
		}
	}
	list_destroy(assoc_list);

	if(rc == SLURM_SUCCESS && list_count(wckey_list)) {
		if(acct_storage_p_add_wckeys(pg_conn, uid, wckey_list)
		   == SLURM_ERROR) {
			error("Problem adding user wckeys");
			rc = SLURM_ERROR;
		}
	}
	list_destroy(wckey_list);

	return rc;
}

/*
 * as_pg_modify_users - modify users
 *
 * IN pg_conn: database connection
 * IN uid: user performing the modify operation
 * IN user_cond: which users to modify
 * IN user: attribute of users after modification
 * RET: list of users modified
 */
extern List
as_pg_modify_users(pgsql_conn_t *pg_conn, uint32_t uid,
		   slurmdb_user_cond_t *user_cond, slurmdb_user_rec_t *user)
{
	List ret_list = NULL;
	int rc = SLURM_SUCCESS;
	char *object = NULL, *user_name = NULL;
	char *vals = NULL, *cond = NULL, *query = NULL, *name_char = NULL;
	time_t now = time(NULL);
	PGresult *result = NULL;

	if(!user_cond || !user) {
		error("as/pg: modify_users: we need something to change");
		return NULL;
	}

	if(check_db_connection(pg_conn) != SLURM_SUCCESS)
		return NULL;

	/* make condition string */
	_make_user_cond(user_cond, &cond);

	/* make value string */
	if(user->default_acct)
		xstrfmtcat(vals, ", default_acct='%s'", user->default_acct);
	if(user->default_wckey)
		xstrfmtcat(vals, ", default_wckey='%s'", user->default_wckey);
	if(user->name)
		xstrfmtcat(vals, ", name='%s'", user->name);
	if(user->admin_level != SLURMDB_ADMIN_NOTSET)
		xstrfmtcat(vals, ", admin_level=%u", user->admin_level);

	if (!cond || !vals) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		error("Nothing to change");
		return NULL;
	}
	/* cond with prefix "AND ()" */
	query = xstrdup_printf("SELECT name FROM %s WHERE deleted=0 %s;",
			       user_table, cond);
	xfree(cond);
	result = DEF_QUERY_RET;
	if(!result) {
		error("as/pg: failed to retrieve users to modify");
		xfree(vals);
		return NULL;
	}

	if(user->name && (PQntuples(result) != 1)) {
		errno = ESLURM_ONE_CHANGE;
		xfree(vals);
		return NULL;
	}

	rc = 0;
	ret_list = list_create(slurm_destroy_char);
	FOR_EACH_ROW {
		slurmdb_user_rec_t *user_rec = NULL;
		object = xstrdup(ROW(0));
		list_append(ret_list, object);
		if (!rc) {
			xstrfmtcat(name_char, "(name='%s'", object);
			rc = 1;
		} else  {
			xstrfmtcat(name_char, " OR name='%s'", object);
		}

		user_rec = xmalloc(sizeof(slurmdb_user_rec_t));

		if(!user->name)
			user_rec->name = xstrdup(object);
		else {
			user_rec->name = xstrdup(user->name);
			user_rec->old_name = xstrdup(object);
			if(_change_user_name(pg_conn, user_rec)
			   != SLURM_SUCCESS)
				break;
		}
		user_rec->default_acct = xstrdup(user->default_acct);
		user_rec->default_wckey = xstrdup(user->default_wckey);
		user_rec->admin_level = user->admin_level;
		addto_update_list(pg_conn->update_list, SLURMDB_MODIFY_USER,
				  user_rec);
	} END_EACH_ROW;
	PQclear(result);

	if(!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		debug3("didn't effect anything");
		xfree(vals);
		return ret_list;
	}
	xstrcat(name_char, ")");

	user_name = uid_to_string((uid_t) uid);
	rc = pgsql_modify_common(pg_conn, DBD_MODIFY_USERS, now,
				 "", user_name, user_table,
				 name_char, vals);
	xfree(user_name);
	xfree(name_char);
	xfree(vals);
	if (rc == SLURM_ERROR) {
		error("Couldn't modify users");
		list_destroy(ret_list);
		ret_list = NULL;
	}
	return ret_list;
}

/* whether specified users has jobs in db */
/* assoc_cond format: "t1.user_name=name OR t1.user=name ..." */
static int
_user_has_jobs(pgsql_conn_t *pg_conn, char *assoc_cond)
{
	DEF_VARS;
	int has_jobs = 0;

	FOR_EACH_CLUSTER(NULL) {
		if (query)
			xstrcat(query, " UNION ");
		xstrfmtcat(query, "SELECT t0.id_assoc FROM %s.%s AS t0, "
			   "%s.%s AS t1 WHERE (%s) AND "
			   "t0.id_assoc=t1.id_assoc",
			   cluster_name, job_table, cluster_name, assoc_table,
			   assoc_cond);
	} END_EACH_CLUSTER;
	xstrcat(query, " LIMIT 1;");
	result = DEF_QUERY_RET;
	if (result) {
		has_jobs = (PQntuples(result) != 0);
		PQclear(result);
	}
	return has_jobs;
}

/* get running jobs of specified users */
/* assoc_cond format: "t1.user_name=name OR t1.user_name=name ..." */
static List
_get_user_running_jobs(pgsql_conn_t *pg_conn, char *assoc_cond)
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
			"%s.%s AS t1 WHERE (%s) AND "
			"t0.id_assoc=t1.id_assoc AND t0.state=%d AND "
			"t0.time_end=0", fields, cluster_name, cluster_name,
			job_table, cluster_name, assoc_table, assoc_cond,
			JOB_RUNNING);
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
 * assoc_cond format: "t1.user_name=name OR t1.user_name=name..."
 */
static int
_cluster_remove_user_assoc(pgsql_conn_t *pg_conn, char *cluster,
			   time_t now, char *assoc_cond, int has_jobs)
{
	DEF_VARS;
	slurmdb_association_rec_t *rem_assoc = NULL;
	char *assoc_char = NULL;
	int rc = SLURM_SUCCESS;
	uint32_t smallest_lft = 0xFFFFFFFF, lft;

	query = xstrdup_printf (
		"SELECT DISTINCT t1.id_assoc,t1.lft FROM %s.%s AS t1 "
		"WHERE t1.deleted=0 AND (%s) AND t1.creation_time>%ld; ",
		cluster, assoc_table, assoc_cond,
		(now - DELETE_SEC_BACK));
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
		if(lft < smallest_lft)
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
			error ("failed to remove user assoc");
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
		"UPDATE %s.%s SET mod_time=%ld, deleted=1, def_qos_id=NULL, "
		"shares=1, max_jobs=NULL, max_nodes_pj=NULL, max_wall_pj=NULL, "
		"max_cpu_mins_pj=NULL WHERE (%s);", cluster, assoc_table,
		now, assoc_char);
	xfree(assoc_char);
	rc = DEF_QUERY_RET_RC;
	return rc;
}

/*
 * as_pg_remove_users - remove users
 *
 * IN pg_conn: database connection
 * IN uid: user performing the remove operation
 * IN user_cond: users to remove
 * RET: list of users removed
 */
extern List
as_pg_remove_users(pgsql_conn_t *pg_conn, uint32_t uid,
		   slurmdb_user_cond_t *user_cond)
{
	DEF_VARS;
	List ret_list = NULL, tmp_list = NULL;
	char *user_name = NULL, *assoc_char = NULL;
	char *cond = NULL, *name_char = NULL;
	time_t now = time(NULL);
	int rc = SLURM_SUCCESS, has_jobs;
	slurmdb_user_cond_t user_coord_cond;
	slurmdb_association_cond_t assoc_cond;
	slurmdb_wckey_cond_t wckey_cond;

	if (!user_cond) {
		error("as/pg: remove_users: we need something to remove");
		return NULL;
	}

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return NULL;

	/* make condition string */
	_make_user_cond(user_cond, &cond);
	if(!cond) {
		error("Nothing to remove");
		return NULL;
	}

	query = xstrdup_printf("SELECT name FROM %s WHERE deleted=0 %s;",
			       user_table, cond);
	xfree(cond);
	result = DEF_QUERY_RET;
	if(!result) {
		error("as/pg: remove_users: failed to get users to remove");
		return NULL;
	}

	memset(&user_coord_cond, 0, sizeof(slurmdb_user_cond_t));
	memset(&wckey_cond, 0, sizeof(slurmdb_wckey_cond_t));
	memset(&assoc_cond, 0, sizeof(slurmdb_association_cond_t));
	/*
	 * objects in assoc_cond.user_list also in ret_list.
	 * DO NOT xfree them. Hence the NULL parameter.
	 */
	assoc_cond.user_list = list_create(NULL);
	user_coord_cond.assoc_cond = &assoc_cond;

	rc = 0;
	ret_list = list_create(slurm_destroy_char);
	FOR_EACH_ROW {
		slurmdb_user_rec_t *user_rec = NULL;
		char *object = xstrdup(ROW(0));

		list_append(ret_list, object);
		list_append(assoc_cond.user_list, object);

		if(!rc) {
			xstrfmtcat(name_char, "name='%s'", object);
			xstrfmtcat(assoc_char, "t1.user_name='%s'", object);
			rc = 1;
		} else {
			xstrfmtcat(name_char, " OR name='%s'", object);
			xstrfmtcat(assoc_char, " OR t1.user_name='%s'", object);
		}
		user_rec = xmalloc(sizeof(slurmdb_user_rec_t));
		user_rec->name = xstrdup(object);
		addto_update_list(pg_conn->update_list, SLURMDB_REMOVE_USER,
				  user_rec);
	} END_EACH_ROW;
	PQclear(result);

	if(!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		debug3("as/pg: remove_users: nothing affected");
		list_destroy(assoc_cond.user_list);
		return ret_list;
	}

	/* remove these users from the coord table */
	tmp_list = acct_storage_p_remove_coord(pg_conn, uid, NULL,
					       &user_coord_cond);
	if(tmp_list)
		list_destroy(tmp_list);

	/* remove these users from the wckey table */
	wckey_cond.user_list = assoc_cond.user_list;
	tmp_list = acct_storage_p_remove_wckeys(pg_conn, uid, &wckey_cond);
	if(tmp_list)
		list_destroy(tmp_list);
	list_destroy(assoc_cond.user_list);

	/* if there are running jobs of the users, return the jobs */
	tmp_list = _get_user_running_jobs(pg_conn, assoc_char);
	if (tmp_list) {
		reset_pgsql_conn(pg_conn);
		list_destroy(ret_list);
		errno = ESLURM_JOBS_RUNNING_ON_ASSOC;
		return tmp_list;
	}

	/* delete recently added users */
	has_jobs = _user_has_jobs(pg_conn, assoc_char);
	if (! has_jobs ) {
		query = xstrdup_printf(
			"DELETE FROM %s WHERE creation_time>%ld AND (%s);",
			user_table, (now - DELETE_SEC_BACK), name_char);
	}
	/* mark others as deleted */
	xstrfmtcat(query, "UPDATE %s SET mod_time=%ld, deleted=1 WHERE deleted=0 "
		   "AND (%s);", user_table, now, name_char);
	user_name = uid_to_string((uid_t) uid);
	xstrfmtcat(query, "INSERT INTO %s (timestamp, action, name, actor) "
		   "VALUES (%ld, %d, $$%s$$, '%s');", txn_table, now,
		   DBD_REMOVE_USERS, name_char, user_name);
	xfree(user_name);
	rc = DEF_QUERY_RET_RC;
	if (rc == SLURM_ERROR) {
		reset_pgsql_conn(pg_conn);
		list_destroy(ret_list);
		ret_list = NULL;
		goto out;
	}

	/* handle associations */
	FOR_EACH_CLUSTER(NULL) {
		rc = _cluster_remove_user_assoc(pg_conn, cluster_name,
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
 * as_pg_get_users - get users
 *
 * IN pg_conn: database connection
 * IN uid: user performing the get operation
 * IN user_cond: which users to get
 * RET: the users
 */
extern List
as_pg_get_users(pgsql_conn_t *pg_conn, uid_t uid,
		slurmdb_user_cond_t *user_cond)
{
	DEF_VARS;
	char *cond = NULL;
	List user_list = NULL;
	ListIterator itr = NULL;
	int is_admin = 1;
	slurmdb_user_rec_t user;
	char *gu_fields = "name, default_acct, default_wckey, admin_level";
	enum {
		F_NAME,
		F_DEF_ACCT,
		F_DEF_WCKEY,
		F_ADMIN_LEVEL,
		F_COUNT
	};

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return NULL;

	if (check_user_op(pg_conn, uid, PRIVATE_DATA_USERS,
			  &is_admin, &user) != SLURM_SUCCESS) {
		error("as/pg: user(%u) not found in db", uid);
		errno = ESLURM_USER_ID_MISSING;
		return NULL;
	}

	if(!user_cond) {
		xstrcat(cond, "WHERE deleted=0");
	} else {

		if(user_cond->with_deleted)
			xstrcat(cond, "WHERE (deleted=0 OR deleted=1)");
		else
			xstrcat(cond, "WHERE deleted=0");
		_make_user_cond(user_cond, &cond);
	}

	/* only get the requesting user if this flag is set */
	if(!is_admin) {
		xstrfmtcat(cond, " AND name='%s'", user.name);
	}

	query = xstrdup_printf("SELECT %s FROM %s %s", gu_fields,
			       user_table, cond);
	xfree(cond);
	result = DEF_QUERY_RET;
	if(!result)
		return NULL;

	user_list = list_create(slurmdb_destroy_user_rec);
	FOR_EACH_ROW {
		slurmdb_user_rec_t *user = xmalloc(sizeof(slurmdb_user_rec_t));
		list_append(user_list, user);

		user->name = xstrdup(ROW(F_NAME));
		user->default_acct = xstrdup(ROW(F_DEF_ACCT));
		if(! ISNULL(F_DEF_WCKEY))
			user->default_wckey = xstrdup(ROW(F_DEF_WCKEY));
		else
			user->default_wckey = xstrdup("");
		user->admin_level = atoi(ROW(F_ADMIN_LEVEL));
		/*
		 * user->uid will be set on the client since this could be on a
		 * different machine where this user may not exist or
		 * may have a different uid
		 */
		if(user_cond && user_cond->with_coords)
			_get_user_coords(pg_conn, user);
	} END_EACH_ROW;
	PQclear(result);

	/* get associations for users */
	if(user_cond && user_cond->with_assocs) {
		ListIterator assoc_itr = NULL;
		slurmdb_user_rec_t *user = NULL;
		slurmdb_association_rec_t *assoc = NULL;
		List assoc_list = NULL;

		/* Make sure we don't get any non-user associations
		 * this is done by at least having a user_list
		 * defined */
		if(!user_cond->assoc_cond)
			user_cond->assoc_cond =
				xmalloc(sizeof(slurmdb_association_cond_t));
		if(!user_cond->assoc_cond->user_list)
			user_cond->assoc_cond->user_list = list_create(NULL);

		assoc_list = acct_storage_p_get_associations(
			pg_conn, uid, user_cond->assoc_cond);

		if(!assoc_list) {
			error("as/pg: gt_users: no associations got");
			goto get_wckeys;
		}

		itr = list_iterator_create(user_list);
		assoc_itr = list_iterator_create(assoc_list);
		while((user = list_next(itr))) {
			while((assoc = list_next(assoc_itr))) {
				if(strcmp(assoc->user, user->name))
					continue;

				if(!user->assoc_list)
					user->assoc_list = list_create(
						slurmdb_destroy_association_rec);
				list_append(user->assoc_list, assoc);
				list_remove(assoc_itr);
			}
			list_iterator_reset(assoc_itr);
		}
		list_iterator_destroy(itr);
		list_iterator_destroy(assoc_itr);
		list_destroy(assoc_list);
	}

get_wckeys:
	/* get wckey for users */
	if(user_cond && user_cond->with_wckeys) {
		ListIterator wckey_itr = NULL;
		slurmdb_user_rec_t *user = NULL;
		slurmdb_wckey_rec_t *wckey = NULL;
		List wckey_list = NULL;
		slurmdb_wckey_cond_t wckey_cond;

		memset(&wckey_cond, 0, sizeof(slurmdb_wckey_cond_t));
		if(user_cond->assoc_cond) {
			wckey_cond.user_list =
				user_cond->assoc_cond->user_list;
			wckey_cond.cluster_list =
				user_cond->assoc_cond->cluster_list;
		}
		wckey_list = acct_storage_p_get_wckeys(
			pg_conn, uid, &wckey_cond);

		if(!wckey_list) {
			error("as/pg: get_users: no wckeys got");
			return user_list;
		}

		itr = list_iterator_create(user_list);
		wckey_itr = list_iterator_create(wckey_list);
		while((user = list_next(itr))) {
			while((wckey = list_next(wckey_itr))) {
				if(strcmp(wckey->user, user->name))
					continue;

				if(!user->wckey_list)
					user->wckey_list = list_create(
						slurmdb_destroy_wckey_rec);
				list_append(user->wckey_list, wckey);
				list_remove(wckey_itr);
			}
			list_iterator_reset(wckey_itr);
		}
		list_iterator_destroy(itr);
		list_iterator_destroy(wckey_itr);
		list_destroy(wckey_list);
	}
	return user_list;
}

/*
 * as_pg_add_coord - add account coordinators
 *
 * IN pg_conn - database connection
 * IN uid - user performing the add operation
 * IN acct_list - accounts the coordinator manages
 * IN user_cond - users to be added as coordinators
 *    (user_cond->assoc_cond->user_list)
 * RET - error code
 */
extern int
as_pg_add_coord(pgsql_conn_t *pg_conn, uint32_t uid,
		List acct_list, slurmdb_user_cond_t *user_cond)
{
	char *query = NULL, *user = NULL, *acct = NULL;
	char *user_name = NULL, *vals = NULL, *txn_query = NULL;
	ListIterator itr, itr2;
	time_t now = time(NULL);
	int rc = SLURM_SUCCESS;
	slurmdb_user_rec_t *user_rec = NULL;

	if(!user_cond || !user_cond->assoc_cond
	   || !user_cond->assoc_cond->user_list
	   || !list_count(user_cond->assoc_cond->user_list)
	   || !acct_list || !list_count(acct_list)) {
		error("as/pg: add_coord: we need something to add");
		return SLURM_ERROR;
	}

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	user_name = uid_to_string((uid_t) uid);
	itr = list_iterator_create(user_cond->assoc_cond->user_list);
	itr2 = list_iterator_create(acct_list);
	while((user = list_next(itr))) {
		while((acct = list_next(itr2))) {
			/*
			 * order of vals must match structure of
			 * acct_coord_table: creation_time, mod_time, deleted,
			 * acct, user_name
			 * CAST is required in ARRAY
			 */
			if(vals)
				xstrcat(vals, ", ");
			xstrfmtcat(vals,
				   "CAST((%ld, %ld, 0, '%s', '%s') AS %s)",
				   now, now, acct, user, acct_coord_table);

			if(txn_query)
				xstrfmtcat(txn_query,
					   ", (%ld, %u, '%s', '%s', '%s')",
					   now, DBD_ADD_ACCOUNT_COORDS, user,
					   user_name, acct);
			else
				xstrfmtcat(txn_query,
					   "INSERT INTO %s "
					   "(timestamp, action, name, "
					   "actor, info) "
					   "VALUES (%ld, %u, '%s', "
					   "'%s', '%s')",
					   txn_table,
					   now, DBD_ADD_ACCOUNT_COORDS, user,
					   user_name, acct);
		}
		list_iterator_reset(itr2);
	}
	xfree(user_name);
	list_iterator_destroy(itr);
	list_iterator_destroy(itr2);

	if(vals) {
		xstrfmtcat(query, "SELECT public.add_coords(ARRAY[%s]); %s;",
			   vals, txn_query);
		xfree(vals);
		xfree(txn_query);
		rc = DEF_QUERY_RET_RC;
		if(rc != SLURM_SUCCESS) {
			error("Couldn't add account coordinator");
			return rc;
		}
		/* get the update list set */
		itr = list_iterator_create(user_cond->assoc_cond->user_list);
		while((user = list_next(itr))) {
			user_rec = xmalloc(sizeof(slurmdb_user_rec_t));
			user_rec->name = xstrdup(user);
			_get_user_coords(pg_conn, user_rec);
			addto_update_list(pg_conn->update_list,
					  SLURMDB_ADD_COORD, user_rec);
		}
		list_iterator_destroy(itr);
	}
	return SLURM_SUCCESS;
}

/*
 * as_pg_remove_coord - remove account coordinators
 *
 * IN pg_conn - database connection
 * IN uid - user performing the remove operation
 * IN acct_list - accounts the coordinator manages
 * IN user_cond - coordinator users to be removed
 *    (user_cond->assoc_cond->user_list)
 * RET - list of coords removed
 */
extern List
as_pg_remove_coord(pgsql_conn_t *pg_conn, uint32_t uid,
		   List acct_list, slurmdb_user_cond_t *user_cond)
{
	DEF_VARS;
	List user_list = NULL, ret_list = NULL;
	ListIterator itr = NULL;
	slurmdb_user_rec_t user, *user_rec = NULL;
	char *cond = NULL, *last_user = NULL, *user_name = NULL;
	int is_admin, rc;
	time_t now = time(NULL);

	if (!user_cond && !acct_list) {
		error("as/pg: remove_coord: we need something to remove");
		return NULL;
	} else if(user_cond && user_cond->assoc_cond)
		user_list = user_cond->assoc_cond->user_list;

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return NULL;

	if (check_user_op(pg_conn, uid, 0, &is_admin, &user) != SLURM_SUCCESS) {
		error("as/pg: user(%u) not found in db", uid);
		errno = ESLURM_USER_ID_MISSING;
		return NULL;
	}

	if (!is_admin && ! is_user_any_coord(pg_conn, &user)) {
		error("as/pg: remove_coord: only admins/coords "
		      "can remove coords");
		return NULL;
	}

	concat_cond_list(user_list, NULL, "user_name", &cond);
	concat_cond_list(acct_list, NULL, "acct", &cond);
	if(!cond) {
		errno = SLURM_ERROR;
		debug3("as/pg: remove_coord: No conditions given");
		return NULL;
	}

	query = xstrdup_printf("SELECT user_name, acct FROM %s "
			       "WHERE deleted=0 %s ORDER BY user_name",
			       acct_coord_table, cond);
	/* cond used below */
	result = DEF_QUERY_RET;
	if(!result) {
		xfree(cond);
		errno = SLURM_ERROR;
		return NULL;
	}

	ret_list = list_create(slurm_destroy_char);
	user_list = list_create(slurm_destroy_char);
	FOR_EACH_ROW {
		if(!is_admin && !is_user_coord(&user, ROW(1))) {
			error("as/pg: remove_coord: User %s(%d) does "
			      "not have the ability to change this "
			      "account (%s)",
			      user.name, user.uid, ROW(1));
			list_destroy(ret_list);
			list_destroy(user_list);
			xfree(cond);
			PQclear(result);
			errno = ESLURM_ACCESS_DENIED;
			return NULL;
		}
		/* record users affected */
		if(!last_user || strcasecmp(last_user, ROW(0))) {
			list_append(user_list, xstrdup(ROW(0)));
			last_user = ROW(0);
		}
		list_append(ret_list, xstrdup_printf("U = %-9s A = %-10s",
						     ROW(0), ROW(1)));
	} END_EACH_ROW;
	PQclear(result);

	if (list_count(ret_list) == 0) {
		list_destroy(user_list);
		xfree(cond);
		errno = SLURM_NO_CHANGE_IN_DATA;
		debug3("as/pg: remove_coords: didn't effect anything");
		return ret_list;
	}

	user_name = uid_to_string((uid_t) uid);
	/* inline pgsql_remove_common() to make logic clear */

	/* remove completely all that is less than a day old */
	query = xstrdup_printf("DELETE FROM %s WHERE creation_time>%ld %s;",
			       acct_coord_table,
			       (now - DELETE_SEC_BACK), cond);
	xstrfmtcat(query, "UPDATE %s SET mod_time=%ld, deleted=1 "
		   "WHERE deleted=0 %s;", acct_coord_table, now, cond);
	/* cond format: " AND (...)" */
	xstrfmtcat(query, "INSERT INTO %s (timestamp, action, name, actor) "
		   "VALUES (%ld, %d, $$%s$$, '%s');", txn_table, now,
		   DBD_REMOVE_ACCOUNT_COORDS, (cond+5), user_name);
	rc = DEF_QUERY_RET_RC;
	xfree(cond);
	xfree(user_name);
	if (rc != SLURM_SUCCESS) {
		list_destroy(ret_list);
		list_destroy(user_list);
		reset_pgsql_conn(pg_conn);
		errno = SLURM_ERROR;
		return NULL;
	}

	/* get the update list set */
	itr = list_iterator_create(user_list);
	while((last_user = list_next(itr))) {
		user_rec = xmalloc(sizeof(slurmdb_user_rec_t));
		user_rec->name = xstrdup(last_user);
		_get_user_coords(pg_conn, user_rec);
		addto_update_list(pg_conn->update_list,
				  SLURMDB_REMOVE_COORD, user_rec);
	}
	list_iterator_destroy(itr);
	list_destroy(user_list);

	return ret_list;
}
