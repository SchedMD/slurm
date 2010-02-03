/*****************************************************************************\
 *  wckey.c - accounting interface to pgsql - wckey related functions.
 *
 *  $Id: wckey.c 13061 2008-01-22 21:23:56Z da $
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

#include "common.h"

char *wckey_table = "wckey_table";
static storage_field_t wckey_table_fields[] = {
	{ "creation_time", "INTEGER NOT NULL" },
	{ "mod_time", "INTEGER DEFAULT 0 NOT NULL" },
	{ "deleted", "INTEGER DEFAULT 0" },
	{ "id", "SERIAL" },
	{ "name", "TEXT DEFAULT '' NOT NULL" },
	{ "cluster", "TEXT NOT NULL" },
	{ "user_name", "TEXT NOT NULL" },
	{ NULL, NULL}
};
static char *wckey_table_constraint = ", "
	"PRIMARY KEY (id), "
	"UNIQUE (name, user_name, cluster) "
	")";

/*
 * _create_function_add_wckey - create a PL/pgSQL function to add wckey
 *
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_add_wckey(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION add_wckey "
		"(rec %s) RETURNS INTEGER AS $$"
		"DECLARE wckey_id INTEGER; "
		"BEGIN LOOP "
		"  BEGIN "
		"    INSERT INTO %s VALUES (rec.creation_time, rec.mod_time, "
		"      0, DEFAULT, rec.name, rec.cluster, rec.user_name)"
		"      RETURNING id INTO wckey_id;"
		"    RETURN wckey_id;"
		"  EXCEPTION WHEN UNIQUE_VIOLATION THEN"
		"    UPDATE %s SET"
		"        (deleted, mod_time) = (0, rec.mod_time) "
		"      WHERE name=rec.name AND cluster=rec.cluster AND "
		"            user_name=rec.user_name "
		"      RETURNING id INTO wckey_id;"
		"    IF FOUND THEN RETURN wckey_id; END IF;"
		"  END; "
		"END LOOP; END; $$ LANGUAGE PLPGSQL;",
		wckey_table, wckey_table, wckey_table);
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
_make_wckey_cond(acct_wckey_cond_t *wckey_cond)
{
	char *cond = NULL;
	concat_cond_list(wckey_cond->name_list, NULL, "name", &cond);
	concat_cond_list(wckey_cond->cluster_list, NULL, "cluster", &cond);
	concat_cond_list(wckey_cond->id_list, NULL, "id", &cond);
	return cond;
}

/*
 * check_wckey_tables - check wckey related tables and functions
 * IN pg_conn: database connection
 * IN user: database owner
 * RET: error code
 */
extern int
check_wckey_tables(PGconn *db_conn, char *user)
{
	int rc;

	rc = check_table(db_conn, wckey_table, wckey_table_fields,
			 wckey_table_constraint, user);

	rc |= _create_function_add_wckey(db_conn);

	return rc;
}

/*
 * as_p_add_wckeys - add wckeys
 *
 * IN pg_conn: database connection
 * IN uid: user performing the add operation
 * IN wckey_list: wckeys to add
 * RET: error code
 */
extern int
as_p_add_wckeys(pgsql_conn_t *pg_conn, uint32_t uid, List wckey_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS, added=0;
	acct_wckey_rec_t *object = NULL;
	char *rec = NULL, *info = NULL, *query = NULL, *user_name = NULL;
	char *id_str = NULL;
	time_t now = time(NULL);

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	user_name = uid_to_string((uid_t) uid);
	itr = list_iterator_create(wckey_list);
	while((object = list_next(itr))) {
		if(!object->cluster || !object->user) {
			error("as/pg: add_wckeys: we need a wckey name, "
			      "cluster, and user to add.");
			rc = SLURM_ERROR;
			continue;
		}
		/*
		 * order of fields: creation_time, mod_time, deleted, id,
		 * name, cluster, user
		 */
		rec = xstrdup_printf("(%d, %d, 0, 0, '%s', '%s', '%s')",
				     now, now, object->name,
				     object->cluster, object->user ?: "");
		query = xstrdup_printf("SELECT add_wckey(%s);", rec);
		xfree(rec);
		DEBUG_QUERY;
		object->id = pgsql_query_ret_id(pg_conn->db_conn, query);
		xfree(query);
		if(!object->id) {
			error("Couldn't add wckey %s", object->name);
			added=0;
			break;
		}
		info = xstrdup_printf("name='%s', cluster='%s', user_name='%s'",
				      object->name, object->cluster,
				      object->user);
		id_str = xstrdup_printf("%d", object->id);
		rc = add_txn(pg_conn, now, DBD_ADD_WCKEYS, id_str,
			     user_name, info);
		xfree(id_str);
		xfree(info);
		if(rc != SLURM_SUCCESS) {
			error("Couldn't add txn");
		} else {
			if(addto_update_list(pg_conn->update_list,
					     ACCT_ADD_WCKEY,
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
 * as_p_modify_wckeys - modify wckeys
 *
 * IN pg_conn: database connection
 * IN uid: user performing the modify operation
 * IN wckey_cond: which wckeys to modify
 * IN wckey: attribute of wckeys after modification
 * RET: list of wckeys modified
 */
extern List
as_p_modify_wckeys(pgsql_conn_t *pg_conn, uint32_t uid,
		   acct_wckey_cond_t *wckey_cond,
		   acct_wckey_rec_t *wckey)
{
	/* TODO: complete this */
	return NULL;
}

/*
 * as_p_remove_wckeys - remove wckeys
 *
 * IN pg_conn: database connection
 * IN uid: user performing the remove operation
 * IN wckey_cond: wckeys to remove
 * RET: list of wckeys removed
 */
extern List
as_p_remove_wckeys(pgsql_conn_t *pg_conn, uint32_t uid,
		   acct_wckey_cond_t *wckey_cond)
{
	List ret_list = NULL;
	PGresult *result = NULL;
	int rc = SLURM_SUCCESS;
	char *cond = NULL, *query = NULL, *name_char = NULL;
	char *user_name, *assoc_char = NULL;
	time_t now = time(NULL);

	if(check_db_connection(pg_conn) != SLURM_SUCCESS)
		return NULL;

	/* XXX: remove all wckeys if null condition given? */
	if (wckey_cond) {
		cond = _make_wckey_cond(wckey_cond);
		if (wckey_cond->with_deleted)
			query = xstrdup_printf(
				"SELECT id, name FROM %s "
				"WHERE (deleted=0 OR deleted=1) %s;",
				wckey_table, cond ?: "");
		else
			query = xstrdup_printf(
				"SELECT id, name FROM %s "
				"WHERE deleted=0 %s;",
				wckey_table, cond ?: "");
		xfree(cond);
	} else {
		query = xstrdup_printf(
			"SELECT id, name FROM %s WHERE deleted=0;",
			wckey_table);
	}

	result = DEF_QUERY_RET;
	if (!result) {
		error("as/pg: remove_wckeys: failed to get wckeys");
		return NULL;
	}

	name_char = NULL;
	ret_list = list_create(slurm_destroy_char);
	FOR_EACH_ROW {
		acct_wckey_rec_t *wckey_rec = NULL;
		list_append(ret_list, xstrdup(ROW(1)));
		if(!name_char)
			xstrfmtcat(name_char, "id='%s'",
				   ROW(0));
		else
			xstrfmtcat(name_char, " OR id='%s'",
				   ROW(0));
		if(!assoc_char)
			xstrfmtcat(assoc_char, "wckeyid='%s'",
				   ROW(0));
		else
			xstrfmtcat(assoc_char, " OR wckeyid='%s'",
				   ROW(0));

		wckey_rec = xmalloc(sizeof(acct_wckey_rec_t));
		/* we only need id when removing no real need to init */
		wckey_rec->id = atoi(ROW(0));
		addto_update_list(pg_conn->update_list, ACCT_REMOVE_WCKEY,
				  wckey_rec);
	} END_EACH_ROW;
	PQclear(result);

	if(!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		debug3("as/pg: remove_wckeys: didn't effect anything");
		return ret_list;
	}

	user_name = uid_to_string((uid_t) uid);
	rc = aspg_remove_common(pg_conn, DBD_REMOVE_WCKEYS, now,
				user_name, wckey_table, name_char, assoc_char);
	xfree(name_char);
	xfree(assoc_char);
	xfree(user_name);
	if (rc == SLURM_ERROR) {
		list_destroy(ret_list);
		ret_list = NULL;
	}
	return ret_list;
}

/*
 * as_p_get_wckeys - get wckeys
 *
 * IN pg_conn: database connection
 * IN uid: user performing the get operation
 * IN wckey_cond: wckeys to get
 * RET: list of wckeys got
 */
extern List
as_p_get_wckeys(pgsql_conn_t *pg_conn, uid_t uid,
		acct_wckey_cond_t *wckey_cond)
{
	char *query = NULL;
	char *cond = NULL;
	List wckey_list = NULL;
	int is_admin=1;
	PGresult *result = NULL;
	uint16_t private_data = 0;
	acct_user_rec_t user;
	/* needed if we don't have an wckey_cond */
	uint16_t with_usage = 0;

	char *gw_fields = "id, name, user_name, cluster";
	enum {
		GW_ID,
		GW_NAME,
		GW_USER,
		GW_CLUSTER,
		GW_COUNT
	};

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return NULL;

	memset(&user, 0, sizeof(acct_user_rec_t));
	user.uid = uid;

	private_data = slurm_get_private_data();
	if (private_data & PRIVATE_DATA_USERS) {
		is_admin = is_user_admin(pg_conn, uid);
		if (!is_admin) {
			if(assoc_mgr_fill_in_user(pg_conn, &user, 1, NULL)
			   != SLURM_SUCCESS) {
				error("as/pg: get_wckeys: failed get info for user");
				return NULL;
			}
		}
	}

	if (wckey_cond) {
		with_usage = wckey_cond->with_usage;
		cond = _make_wckey_cond(wckey_cond);
	}
	if(!is_admin && (private_data & PRIVATE_DATA_USERS))
		xstrfmtcat(cond, " AND user_name='%s'", user.name);

	//START_TIMER;
	query = xstrdup_printf("SELECT DISTINCT %s FROM %s WHERE deleted=0 %s "
			       "ORDER BY name, cluster, user_name;",
			       gw_fields, wckey_table, cond ?: "");
	xfree(cond);
	result = DEF_QUERY_RET;
	if(!result) {
		error("as/pg: remove_wckeys: failed to get wckey");
		return NULL;
	}

	wckey_list = list_create(destroy_acct_wckey_rec);
	FOR_EACH_ROW {
		acct_wckey_rec_t *wckey = xmalloc(sizeof(acct_wckey_rec_t));
		list_append(wckey_list, wckey);

		wckey->id = atoi(ROW(GW_ID));
		wckey->user = xstrdup(ROW(GW_USER));

		/* we want a blank wckey if the name is null */
		if(ROW(GW_NAME))
			wckey->name = xstrdup(ROW(GW_NAME));
		else
			wckey->name = xstrdup("");

		wckey->cluster = xstrdup(ROW(GW_CLUSTER));
	} END_EACH_ROW;
	PQclear(result);

	if(with_usage && wckey_list) {
		get_usage_for_wckey_list(pg_conn, wckey_list,
					 wckey_cond->usage_start,
					 wckey_cond->usage_end);
	}
	//END_TIMER2("get_wckeys");
	return wckey_list;
}

/*
 * get_wckeyid - get wckey id for user
 *
 * IN pg_conn: database connection
 * OUT name: name of wckey
 * IN uid:
 * IN cluster:
 * IN associd:
 * RET: wckey id
 */
extern uint32_t
get_wckeyid(pgsql_conn_t *pg_conn, char **name,
	    uid_t uid, char *cluster, uint32_t associd)
{
	uint32_t wckeyid = 0;

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
	acct_wckey_rec_t wckey_rec;
	char *user = NULL;

	/* since we are unable to rely on uids here (someone could
	   not have there uid in the system yet) we must
	   first get the user name from the associd */
	if(!(user = get_user_from_associd(pg_conn, associd))) {
		error("No user for associd %u", associd);
		return 0;
	}

	/* get the default key */
	if(!*name) {
		acct_user_rec_t user_rec;
		memset(&user_rec, 0, sizeof(acct_user_rec_t));
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

	memset(&wckey_rec, 0, sizeof(acct_wckey_rec_t));
	wckey_rec.name = (*name);
	wckey_rec.uid = NO_VAL;
	wckey_rec.user = user;
	wckey_rec.cluster = cluster;
	if(assoc_mgr_fill_in_wckey(pg_conn, &wckey_rec,
				   ACCOUNTING_ENFORCE_WCKEYS,
				   NULL) != SLURM_SUCCESS) {
		List wckey_list = NULL;
		acct_wckey_rec_t *wckey_ptr = NULL;

		wckey_list = list_create(destroy_acct_wckey_rec);

		wckey_ptr = xmalloc(sizeof(acct_wckey_rec_t));
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
