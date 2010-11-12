/*****************************************************************************\
 *  as_pg_wckey.c - accounting interface to pgsql - wckey related functions.
 *
 *  $Id: as_pg_wckey.c 13061 2008-01-22 21:23:56Z da $
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

/* per-cluster table */
char *wckey_table = "wckey_table";
static storage_field_t wckey_table_fields[] = {
	{ "creation_time", "INTEGER NOT NULL" },
	{ "mod_time", "INTEGER DEFAULT 0 NOT NULL" },
	{ "deleted", "INTEGER DEFAULT 0" },
	{ "id_wckey", "SERIAL" }, /* must be same with job_table */
	{ "wckey_name", "TEXT DEFAULT '' NOT NULL" },
	{ "user_name", "TEXT NOT NULL" },
	{ NULL, NULL}
};
static char *wckey_table_constraint = ", "
	"PRIMARY KEY (id_wckey), "
	"UNIQUE (wckey_name, user_name) "
	")";

/*
 * _create_function_add_wckey - create a PL/pgSQL function to add wckey
 *
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_add_wckey(PGconn *db_conn, char *cluster)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION %s.add_wckey "
		"(rec %s.%s) RETURNS INTEGER AS $$"
		"DECLARE wckey_id INTEGER; "
		"BEGIN LOOP "
		"  BEGIN "
		"    INSERT INTO %s.%s VALUES (rec.creation_time, rec.mod_time, "
		"      0, DEFAULT, rec.wckey_name, rec.user_name)"
		"      RETURNING id_wckey INTO wckey_id;"
		"    RETURN wckey_id;"
		"  EXCEPTION WHEN UNIQUE_VIOLATION THEN"
		"    UPDATE %s.%s SET"
		"        (deleted, mod_time) = (0, rec.mod_time) "
		"      WHERE wckey_name=rec.wckey_name AND "
		"            user_name=rec.user_name "
		"      RETURNING id_wckey INTO wckey_id;"
		"    IF FOUND THEN RETURN wckey_id; END IF;"
		"  END; "
		"END LOOP; END; $$ LANGUAGE PLPGSQL;", cluster,	cluster,
		wckey_table, cluster, wckey_table, cluster, wckey_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _make_wckey_cond - make a SQL query condition string for
 *    wckey remove/get/modify
 *
 * IN wckey_cond: condition specified
 * RET: condition string
 * NOTE: the string should be xfree-ed by caller
 */
static char *
_make_wckey_cond(slurmdb_wckey_cond_t *wckey_cond)
{
	char *cond;
	cond = xstrdup_printf("(deleted=0%s)",
			      wckey_cond->with_deleted ? " OR deleted=1" : "");
	concat_cond_list(wckey_cond->name_list, NULL, "wckey_name", &cond);
	concat_cond_list(wckey_cond->id_list, NULL, "id_wckey", &cond);
	concat_cond_list(wckey_cond->user_list, NULL, "user_name", &cond);
	return cond;
}

/*
 * check_wckey_tables - check wckey related tables and functions
 * IN pg_conn: database connection
 * IN user: database owner
 * RET: error code
 */
extern int
check_wckey_tables(PGconn *db_conn, char *cluster)
{
	int rc;

	rc = check_table(db_conn, cluster, wckey_table, wckey_table_fields,
			 wckey_table_constraint);
	rc |= _create_function_add_wckey(db_conn, cluster);
	return rc;
}

/*
 * as_pg_add_wckeys - add wckeys
 *
 * IN pg_conn: database connection
 * IN uid: user performing the add operation
 * IN wckey_list: wckeys to add
 * RET: error code
 */
extern int
as_pg_add_wckeys(pgsql_conn_t *pg_conn, uint32_t uid, List wckey_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS, added=0;
	slurmdb_wckey_rec_t *object = NULL;
	char *rec = NULL, *info = NULL, *query = NULL, *user_name = NULL;
	char *id_str = NULL;
	time_t now = time(NULL);

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	user_name = uid_to_string((uid_t) uid);
	itr = list_iterator_create(wckey_list);
	while((object = list_next(itr))) {
		if(!object->cluster || !object->cluster[0] ||
		   !object->user || !object->user[0]) {
			error("as/pg: add_wckeys: we need a wckey name, "
			      "cluster, and user to add.");
			rc = SLURM_ERROR;
			continue;
		}
		rec = xstrdup_printf("(%ld, %ld, 0, 0, '%s', '%s')",
				     now, now, object->name, object->user);
		query = xstrdup_printf("SELECT %s.add_wckey(%s);",
				       object->cluster, rec);
		xfree(rec);
		DEBUG_QUERY;
		object->id = pgsql_query_ret_id(pg_conn->db_conn, query);
		xfree(query);
		if(!object->id) {
			error("Couldn't add wckey %s", object->name);
			added=0;
			break;
		}
		info = xstrdup_printf("name='%s', user_name='%s'",
				      object->name, object->user);
		id_str = xstrdup_printf("%d", object->id);
		rc = add_txn(pg_conn, now, object->cluster, DBD_ADD_WCKEYS,
			     id_str, user_name, info);
		xfree(id_str);
		xfree(info);
		if(rc != SLURM_SUCCESS) {
			error("Couldn't add txn");
		} else {
			if(addto_update_list(pg_conn->update_list,
					     SLURMDB_ADD_WCKEY,
					     object) == SLURM_SUCCESS)
				list_remove(itr);
			added++;
		}
	}
	list_iterator_destroy(itr);
	xfree(user_name);

	if(!added) {
		if(pg_conn->rollback) {
			pgsql_db_rollback(pg_conn->db_conn);
		}
		list_flush(pg_conn->update_list);
	}
	return rc;
}

/*
 * as_pg_modify_wckeys - modify wckeys
 *
 * IN pg_conn: database connection
 * IN uid: user performing the modify operation
 * IN wckey_cond: which wckeys to modify
 * IN wckey: attribute of wckeys after modification
 * RET: list of wckeys modified
 */
extern List
as_pg_modify_wckeys(pgsql_conn_t *pg_conn, uint32_t uid,
		    slurmdb_wckey_cond_t *wckey_cond,
		    slurmdb_wckey_rec_t *wckey)
{
	return NULL;
}


/* remove wckey from current selected cluster */
static int
_cluster_remove_wckeys(pgsql_conn_t *pg_conn, char *cluster, char *user_name,
		       char *cond, List ret_list)
{
	DEF_VARS;
	char *name_char = NULL;
	time_t now = time(NULL);
	int rc = SLURM_SUCCESS;

	query = xstrdup_printf("SELECT id_wckey, wckey_name FROM %s.%s WHERE %s;",
			       cluster, wckey_table, cond);
	result = DEF_QUERY_RET;
	if (!result) {
		error("as/pg: remove_wckeys: failed to get wckeys");
		return SLURM_ERROR;
	}
	if (PQntuples(result) == 0) {
		PQclear(result);
		return SLURM_SUCCESS;
	}

	name_char = NULL;
	FOR_EACH_ROW {
		slurmdb_wckey_rec_t *wckey_rec = NULL;
		list_append(ret_list, xstrdup(ROW(1)));
		if(!name_char)
			xstrfmtcat(name_char, "id_wckey=%s",
				   ROW(0));
		else
			xstrfmtcat(name_char, " OR id_wckey=%s",
				   ROW(0));
		wckey_rec = xmalloc(sizeof(slurmdb_wckey_rec_t));
		wckey_rec->id = atoi(ROW(0));
		wckey_rec->cluster = xstrdup(cluster);
		addto_update_list(pg_conn->update_list, SLURMDB_REMOVE_WCKEY,
				  wckey_rec);
	} END_EACH_ROW;
	PQclear(result);

	/* inline pgsql_remove_common to make logic clear */
	query = xstrdup_printf(
		"DELETE FROM %s.%s WHERE creation_time>%ld AND (%s) "
		"AND id_wckey NOT IN (SELECT DISTINCT id_wckey FROM %s.%s);",
		cluster, wckey_table, (now - DELETE_SEC_BACK), name_char,
		cluster, job_table);
	xstrfmtcat(query, "UPDATE %s.%s SET mod_time=%ld, deleted=1 "
		   "WHERE deleted=0 AND (%s);",
		   cluster, wckey_table, now, name_char);
	xstrfmtcat(query, "INSERT INTO %s (timestamp, cluster, action, name, "
		   "actor) VALUES (%ld, '%s', %d, $$%s$$, '%s');", txn_table, now,
		   cluster, SLURMDB_REMOVE_WCKEY, name_char, user_name);
	rc = DEF_QUERY_RET_RC;
	xfree(name_char);

	return rc;
}

/*
 * as_pg_remove_wckeys - remove wckeys
 *
 * IN pg_conn: database connection
 * IN uid: user performing the remove operation
 * IN wckey_cond: wckeys to remove
 * RET: list of wckeys removed
 */
extern List
as_pg_remove_wckeys(pgsql_conn_t *pg_conn, uint32_t uid,
		    slurmdb_wckey_cond_t *wckey_cond)
{
	List ret_list = NULL;
	char *cond = NULL, *user_name = NULL;
	int rc = SLURM_SUCCESS;

	if (!wckey_cond) {
		error("as/pg: remove_wckeys: nothing to remove");
		return NULL;
	}

	if(check_db_connection(pg_conn) != SLURM_SUCCESS)
		return NULL;

	cond = _make_wckey_cond(wckey_cond);

	user_name = uid_to_string((uid_t) uid);
	ret_list = list_create(slurm_destroy_char);
	FOR_EACH_CLUSTER(wckey_cond->cluster_list) {
		if (wckey_cond->cluster_list &&
		    !cluster_in_db(pg_conn, cluster_name))
			continue;
		rc = _cluster_remove_wckeys(pg_conn, cluster_name, user_name,
					    cond, ret_list);
		if (rc != SLURM_SUCCESS)
			break;
	} END_EACH_CLUSTER;
	xfree(user_name);
	xfree(cond);

	if (rc != SLURM_SUCCESS) {
		reset_pgsql_conn(pg_conn);
		list_destroy(ret_list);
		ret_list = NULL;
	} else if(!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		debug3("as/pg: remove_wckeys: didn't effect anything");
	}

	return ret_list;
}

static int
_cluster_get_wckeys(pgsql_conn_t *pg_conn, char *cluster,
		    slurmdb_wckey_cond_t *wckey_cond, char *cond,
		    List ret_list)
{
	PGresult *result = NULL;
	List wckey_list = NULL;
	char *query = NULL;
	uint16_t with_usage = 0;
	char *gw_fields = "id_wckey, wckey_name, user_name";
	enum {
		GW_ID,
		GW_NAME,
		GW_USER,
		GW_COUNT
	};

	if (wckey_cond)
		with_usage = wckey_cond->with_usage;

	//START_TIMER;
	query = xstrdup_printf("SELECT DISTINCT %s FROM %s.%s WHERE %s "
			       "ORDER BY wckey_name, user_name;",
			       gw_fields, cluster, wckey_table, cond ?: "");
	result = DEF_QUERY_RET;
	if(!result) {
		error("as/pg: get_wckeys: failed to get wckey");
		return SLURM_ERROR;
	}

	wckey_list = list_create(slurmdb_destroy_wckey_rec);
	FOR_EACH_ROW {
		slurmdb_wckey_rec_t *wckey =
			xmalloc(sizeof(slurmdb_wckey_rec_t));
		list_append(wckey_list, wckey);

		wckey->id = atoi(ROW(GW_ID));
		wckey->user = xstrdup(ROW(GW_USER));
		wckey->cluster = xstrdup(cluster);
		/* we want a blank wckey if the name is null */
		if(ROW(GW_NAME))
			wckey->name = xstrdup(ROW(GW_NAME));
		else
			wckey->name = xstrdup("");
	} END_EACH_ROW;
	PQclear(result);

	if(with_usage && list_count(wckey_list)) {
		get_usage_for_wckey_list(pg_conn, cluster, wckey_list,
					 wckey_cond->usage_start,
					 wckey_cond->usage_end);
	}
	list_transfer(ret_list, wckey_list);
	list_destroy(wckey_list);
	return SLURM_SUCCESS;
}

/*
 * as_pg_get_wckeys - get wckeys
 *
 * IN pg_conn: database connection
 * IN uid: user performing the get operation
 * IN wckey_cond: wckeys to get
 * RET: list of wckeys got
 */
extern List
as_pg_get_wckeys(pgsql_conn_t *pg_conn, uid_t uid,
		 slurmdb_wckey_cond_t *wckey_cond)
{
	char *cond = NULL;
	List wckey_list = NULL;
	int with_usage, is_admin;
	slurmdb_user_rec_t user;

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return NULL;

	if (check_user_op(pg_conn, uid, PRIVATE_DATA_USERS, &is_admin, &user) != SLURM_SUCCESS) {
		error("as/pg: user(%u) not found in db", uid);
		errno = ESLURM_USER_ID_MISSING;
		return NULL;
	}

	if (wckey_cond) {
		with_usage = wckey_cond->with_usage;
		cond = _make_wckey_cond(wckey_cond);
	}
	if (!is_admin)
		xstrfmtcat(cond, " AND user_name='%s'", user.name);

	wckey_list = list_create(slurmdb_destroy_wckey_rec);

	FOR_EACH_CLUSTER(wckey_cond->cluster_list) {
		if (wckey_cond->cluster_list &&
		    list_count(wckey_cond->cluster_list) &&
		    !cluster_in_db(pg_conn, cluster_name)) {
			/*
			 * When loading sacctmgr config files,
			 * non-existing clusters will be specified
			 */
			continue;
		}
		if(_cluster_get_wckeys(pg_conn, cluster_name, wckey_cond,
				       cond, wckey_list)
		   != SLURM_SUCCESS) {
			list_destroy(wckey_list);
			wckey_list = NULL;
			break;
		}
	} END_EACH_CLUSTER;
	xfree(cond);

	return wckey_list;
}

/* get_wckeyid - get wckey id for user */
extern uint32_t
get_wckeyid(pgsql_conn_t *pg_conn, char **name,
	    uid_t uid, char *cluster, uint32_t associd)
{
	uint32_t wckeyid = 0;
	slurmdb_wckey_rec_t wckey_rec;
	char *user = NULL;

	if (! slurm_get_track_wckey())
		return 0;

	/* Here we are looking for the wckeyid if it doesn't
	 * exist we will create one.  We don't need to check
	 * if it is good or not.  Right now this is the only
	 * place things are created. We do this only on a job
	 * start, not on a job submit since we don't want to
	 * slow down getting the db_index back to the
	 * controller.
	 */
	/* since we are unable to rely on uids here (someone could
	   not have there uid in the system yet) we must
	   first get the user name from the associd */
	if(!(user = get_user_from_associd(pg_conn, cluster, associd))) {
		error("No user for associd %u", associd);
		return 0;
	}

	/* get the default key */
	if(!*name) {
		slurmdb_user_rec_t user_rec;
		memset(&user_rec, 0, sizeof(slurmdb_user_rec_t));
		user_rec.uid = NO_VAL;
		user_rec.name = user;
		if(assoc_mgr_fill_in_user(pg_conn, &user_rec,
					  1, NULL) != SLURM_SUCCESS) {
			error("No user by name of %s assoc %u",
			      user, associd);
			xfree(user);
			goto no_wckeyid;
		}

		if(user_rec.default_wckey)
			*name = xstrdup_printf("*%s",
					       user_rec.default_wckey);
		else
			*name = xstrdup_printf("*");
	}

	memset(&wckey_rec, 0, sizeof(slurmdb_wckey_rec_t));
	wckey_rec.name = (*name);
	wckey_rec.uid = NO_VAL;
	wckey_rec.user = user;
	wckey_rec.cluster = cluster;
	if(assoc_mgr_fill_in_wckey(pg_conn, &wckey_rec,
				   ACCOUNTING_ENFORCE_WCKEYS,
				   NULL) != SLURM_SUCCESS) {
		List wckey_list = NULL;
		slurmdb_wckey_rec_t *wckey_ptr = NULL;

		wckey_list = list_create(slurmdb_destroy_wckey_rec);

		wckey_ptr = xmalloc(sizeof(slurmdb_wckey_rec_t));
		wckey_ptr->name = xstrdup((*name));
		wckey_ptr->user = xstrdup(user);
		wckey_ptr->cluster = xstrdup(cluster);
		list_append(wckey_list, wckey_ptr);
/* 		info("adding wckey '%s' '%s' '%s'", */
/* 			     wckey_ptr->name, wckey_ptr->user, */
/* 			     wckey_ptr->cluster); */
		/* we have already checked to make
		   sure this was the slurm user before
		   calling this */
		if(acct_storage_p_add_wckeys(
			   pg_conn,
			   slurm_get_slurm_user_id(),
			   wckey_list)
		   == SLURM_SUCCESS)
			acct_storage_p_commit(pg_conn, 1);
		/* If that worked lets get it */
		assoc_mgr_fill_in_wckey(pg_conn, &wckey_rec,
					ACCOUNTING_ENFORCE_WCKEYS,
					NULL);

		list_destroy(wckey_list);
	}
	xfree(user);
/* 	info("got wckeyid of %d", wckey_rec.id); */
	wckeyid = wckey_rec.id;
no_wckeyid:
	return wckeyid;
}
