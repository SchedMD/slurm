/*****************************************************************************\
 *  user.c - accounting interface to pgsql - user related functions.
 *
 *  $Id: user.c 13061 2008-01-22 21:23:56Z da $
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

char *acct_coord_table = "acct_coord_table";
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

char *user_table = "user_table";
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
		"CREATE OR REPLACE FUNCTION add_user "
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
		"CREATE OR REPLACE FUNCTION add_coord "
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
		"CREATE OR REPLACE FUNCTION add_coords "
		"(recs %s[]) RETURNS VOID AS $$"
		"DECLARE "
		"  i INTEGER := 1; rec %s;"
		"BEGIN LOOP"
		"  rec := recs[i]; i := i + 1;"
		"  EXIT WHEN rec IS NULL;"
		"  PERFORM add_coord(rec); "
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
_make_user_record(acct_user_rec_t *object, time_t now, char **rec, char **txn)
{
	xfree(*rec);
	/* XXX: order of vals must match structure of USER_TABLE */
	*rec = xstrdup_printf("(%d, %d, 0, '%s', '%s'",
			      now, now, object->name, object->default_acct);
	xstrfmtcat(*txn, "default_acct='%s'", object->default_acct);

	if(object->default_wckey) {
		xstrfmtcat(*rec, ", '%s'", object->default_wckey);
		xstrfmtcat(*txn, ", default_wckey='%s'",
			   object->default_wckey);
	} else {
		/* default value of default_wckey is '' */
		xstrfmtcat(*rec, ", ''");
	}

	if(object->admin_level != ACCT_ADMIN_NOTSET) {
		xstrfmtcat(*rec, ", %u)", object->admin_level);
		xstrfmtcat(*txn, ", admin_level=%u",
			   object->admin_level);
	} else {
		/* default value of admin_level is 1 */
		xstrfmtcat(*rec, ", 1)");
	}
}

/*
 * _get_user_coords - fill in all the accounts this user is coordinator over.
 *  Also fill in sub accounts.
 *
 * IN pg_conn: database connection
 * IN/OUT user: user record
 * RET: error code
 */
static int
_get_user_coords(pgsql_conn_t *pg_conn, acct_user_rec_t *user)
{
	char *query = NULL;
	acct_coord_rec_t *coord = NULL;
	PGresult *result = NULL;
	ListIterator itr = NULL;

	if(!user) {
		error("as/pg: _get_user_coord: user not given.");
		return SLURM_ERROR;
	}

	if(!user->coord_accts)
		user->coord_accts = list_create(destroy_acct_coord_rec);

	query = xstrdup_printf(
		"SELECT acct FROM %s WHERE user_name='%s' AND deleted=0",
		acct_coord_table, user->name);
	result = DEF_QUERY_RET;
	if (!result)
		return SLURM_ERROR;

	FOR_EACH_ROW {
		coord = xmalloc(sizeof(acct_coord_rec_t));
		list_append(user->coord_accts, coord);
		coord->name = xstrdup(ROW(0));
		coord->direct = 1;
		if(query)
			xstrcat(query, " OR ");
		else
			/* strict sub accounts */
			query = xstrdup_printf(
				"SELECT DISTINCT t1.acct "
				"FROM %s AS t1, %s AS t2 "
				"WHERE t1.deleted=0 AND t1.user_name='' "
				"  AND (t1.lft > t2.lft AND t1.rgt < t2.rgt) "
				"  AND (",
				assoc_table, assoc_table);
		xstrfmtcat(query, "t2.acct='%s'", coord->name);
	} END_EACH_ROW;
	PQclear(result);

	if(query) {
		xstrcat(query, ");");
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

			coord = xmalloc(sizeof(acct_coord_rec_t));
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
_make_user_cond(acct_user_cond_t *user_cond, char **cond)
{
	if(user_cond->assoc_cond) {
		concat_cond_list(user_cond->assoc_cond->user_list,
				 NULL, "name", cond);
	}
	concat_cond_list(user_cond->def_acct_list, NULL, "default_acct", cond);
	concat_cond_list(user_cond->def_wckey_list, NULL, "default_wckey", cond);
	if(user_cond->admin_level != ACCT_ADMIN_NOTSET) {
		xstrfmtcat(*cond, " AND admin_level=%u", user_cond->admin_level);
	}
}

/*
 * check_user_tables - check user related tables and functions
 * IN pg_conn: database connection
 * IN user: database owner
 * RET: error code
 */
extern int
check_user_tables(PGconn *db_conn, char *user)
{
	int rc;

	rc = check_table(db_conn, user_table, user_table_fields,
			 user_table_constraints, user);
	rc |= check_table(db_conn, acct_coord_table, acct_coord_table_fields,
			  acct_coord_table_constraints, user);
	rc |= _create_function_add_user(db_conn);
	rc |= _create_function_add_coord(db_conn);
	rc |= _create_function_add_coords(db_conn);
	return rc;
}

/*
 * as_p_add_users - add users
 *
 * IN pg_conn: database connection
 * IN uid: user performing the add operation
 * IN user_list: users to add
 * RET: error code
 */
extern int
as_p_add_users(pgsql_conn_t *pg_conn, uint32_t uid, List user_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	acct_user_rec_t *object = NULL;
	char *rec = NULL, *info = NULL, *user_name = NULL,
		*query = NULL, *txn_query = NULL;
	time_t now = time(NULL);
	List assoc_list = list_create(destroy_acct_association_rec);
	List wckey_list = list_create(destroy_acct_wckey_rec);

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	user_name = uid_to_string((uid_t) uid);

	itr = list_iterator_create(user_list);
	while((object = list_next(itr))) {
		if(!object->name || !object->default_acct) {
			error("as/pg: add_users: we need a user name and "
			      "default acct to add.");
			rc = SLURM_ERROR;
			continue;
		}

		_make_user_record(object, now, &rec, &info);
		query = xstrdup_printf("SELECT add_user(%s);", rec);
		xfree(rec);
		rc = DEF_QUERY_RET_RC;
		if(rc != SLURM_SUCCESS) {
			error("Couldn't add user %s", object->name);
			xfree(info);
			continue;
		}

		/* object moved to update_list, remove from user_list */
		if(addto_update_list(pg_conn->update_list, ACCT_ADD_USER,
				     object) == SLURM_SUCCESS)
			list_remove(itr);

		if(txn_query)
			xstrfmtcat(txn_query,
				   ", (%d, %u, '%s', '%s', $$%s$$)",
				   now, DBD_ADD_USERS, object->name,
				   user_name, info);
		else
			xstrfmtcat(txn_query,
				   "INSERT INTO %s "
				   "(timestamp, action, name, actor, info) "
				   "VALUES (%d, %u, '%s', '%s', $$%s$$)",
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
 * as_p_modify_users - modify users
 *
 * IN pg_conn: database connection
 * IN uid: user performing the modify operation
 * IN user_cond: which users to modify
 * IN user: attribute of users after modification
 * RET: list of users modified
 */
extern List
as_p_modify_users(pgsql_conn_t *pg_conn, uint32_t uid,
		  acct_user_cond_t *user_cond, acct_user_rec_t *user)
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
	if(user->admin_level != ACCT_ADMIN_NOTSET)
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

	rc = 0;
	ret_list = list_create(slurm_destroy_char);
	FOR_EACH_ROW {
		acct_user_rec_t *user_rec = NULL;
		object = xstrdup(ROW(0));
		list_append(ret_list, object);
		if (!rc) {
			xstrfmtcat(name_char, "(name='%s'", object);
			rc = 1;
		} else  {
			xstrfmtcat(name_char, " OR name='%s'", object);
		}
		user_rec = xmalloc(sizeof(acct_user_rec_t));
		user_rec->name = xstrdup(object);
		user_rec->default_acct = xstrdup(user->default_acct);
		user_rec->default_wckey = xstrdup(user->default_wckey);
		user_rec->admin_level = user->admin_level;
		addto_update_list(pg_conn->update_list, ACCT_MODIFY_USER,
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
	rc = aspg_modify_common(pg_conn, DBD_MODIFY_USERS, now,
				user_name, user_table,
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

/*
 * as_p_remove_users - remove users
 *
 * IN pg_conn: database connection
 * IN uid: user performing the remove operation
 * IN user_cond: users to remove
 * RET: list of users removed
 */
extern List
as_p_remove_users(pgsql_conn_t *pg_conn, uint32_t uid,
		  acct_user_cond_t *user_cond)
{
	List ret_list = NULL, coord_list = NULL;
	char *user_name = NULL, *assoc_char = NULL;
	char *cond = NULL, *query = NULL, *name_char = NULL;
	time_t now = time(NULL);
	int rc = SLURM_SUCCESS;
	PGresult *result = NULL;
	acct_user_cond_t user_coord_cond;
	acct_association_cond_t assoc_cond;
	acct_wckey_cond_t wckey_cond;

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

	memset(&user_coord_cond, 0, sizeof(acct_user_cond_t));
	memset(&wckey_cond, 0, sizeof(acct_wckey_cond_t));
	memset(&assoc_cond, 0, sizeof(acct_association_cond_t));
	/*
	 * objects in assoc_cond.user_list also in ret_list.
	 * DO NOT xfree them. Hence the NULL parameter.
	 */
	assoc_cond.user_list = list_create(NULL);
	user_coord_cond.assoc_cond = &assoc_cond;

	rc = 0;
	ret_list = list_create(slurm_destroy_char);
	FOR_EACH_ROW {
		acct_user_rec_t *user_rec = NULL;
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
		user_rec = xmalloc(sizeof(acct_user_rec_t));
		user_rec->name = xstrdup(object);
		addto_update_list(pg_conn->update_list, ACCT_REMOVE_USER,
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
	coord_list = acct_storage_p_remove_coord(pg_conn, uid, NULL,
						 &user_coord_cond);
	if(coord_list)
		list_destroy(coord_list);
	/* remove these users from the wckey table */
	wckey_cond.user_list = assoc_cond.user_list;
	coord_list = acct_storage_p_remove_wckeys(pg_conn, uid, &wckey_cond);
	if(coord_list)
		list_destroy(coord_list);
	list_destroy(assoc_cond.user_list);

	user_name = uid_to_string((uid_t) uid);
	rc = aspg_remove_common(pg_conn, DBD_REMOVE_USERS, now,
				user_name, user_table, name_char, assoc_char);
	xfree(user_name);
	xfree(name_char);
	xfree(assoc_char);

	if (rc == SLURM_ERROR) {
		list_destroy(ret_list);
		return NULL;
	}

	/* TODO: why execute this query after remove_coords */
/* 	query = xstrdup_printf("UPDATE %s AS t2 SET deleted=1, mod_time=%d " */
/* 			       "WHERE %s", acct_coord_table, */
/* 			       now, assoc_char); */
/* 	xfree(assoc_char); */
/* 	rc = pgsql_db_query(pg_conn->db_conn, query); */
/* 	xfree(query); */
/* 	if(rc != SLURM_SUCCESS) { */
/* 		error("Couldn't remove user coordinators"); */
/* 		list_destroy(ret_list); */
/* 		return NULL; */
/* 	} */

	return ret_list;
}

/*
 * as_p_get_users - get users
 *
 * IN pg_conn: database connection
 * IN uid: user performing the get operation
 * IN user_cond: which users to get
 * RET: the users
 */
extern List
as_p_get_users(pgsql_conn_t *pg_conn, uid_t uid, acct_user_cond_t *user_cond)
{
	char *query = NULL, *cond = NULL;
	List user_list = NULL;
	ListIterator itr = NULL;
	int is_admin = 1;
	PGresult *result = NULL;
	uint16_t private_data = 0;
	acct_user_rec_t user;
	char *gu_fields = "name, default_acct, default_wckey, admin_level";
	enum {
		GU_NAME,
		GU_DEF_ACCT,
		GU_DEF_WCKEY,
		GU_ADMIN_LEVEL,
		GU_COUNT
	};

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return NULL;

	memset(&user, 0, sizeof(acct_user_rec_t));
	user.uid = uid;

	private_data = slurm_get_private_data();
	if (private_data & PRIVATE_DATA_USERS) {
		is_admin = is_user_admin(pg_conn, uid);
		if (!is_admin)
			assoc_mgr_fill_in_user(pg_conn, &user, 1, NULL);
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
	if(!is_admin && (private_data & PRIVATE_DATA_USERS)) {
		xstrfmtcat(cond, " AND name='%s'", user.name);
	}

	query = xstrdup_printf("SELECT %s FROM %s %s", gu_fields,
			       user_table, cond);
	xfree(cond);

	result = DEF_QUERY_RET;
	if(!result)
		return NULL;

	user_list = list_create(destroy_acct_user_rec);
	FOR_EACH_ROW {
		acct_user_rec_t *user = xmalloc(sizeof(acct_user_rec_t));
		list_append(user_list, user);

		user->name = xstrdup(ROW(GU_NAME));
		user->default_acct = xstrdup(ROW(GU_DEF_ACCT));
		if(! ISNULL(GU_DEF_WCKEY))
			user->default_wckey = xstrdup(ROW(GU_DEF_WCKEY));
		else
			user->default_wckey = xstrdup("");
		user->admin_level = atoi(ROW(GU_ADMIN_LEVEL));
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
		acct_user_rec_t *user = NULL;
		acct_association_rec_t *assoc = NULL;
		List assoc_list = NULL;

		/* Make sure we don't get any non-user associations
		 * this is done by at least having a user_list
		 * defined */
		if(!user_cond->assoc_cond)
			user_cond->assoc_cond =
				xmalloc(sizeof(acct_association_cond_t));
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
						destroy_acct_association_rec);
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
		acct_user_rec_t *user = NULL;
		acct_wckey_rec_t *wckey = NULL;
		List wckey_list = NULL;
		acct_wckey_cond_t wckey_cond;

		memset(&wckey_cond, 0, sizeof(acct_wckey_cond_t));
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
						destroy_acct_wckey_rec);
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
 * as_p_add_coord - add account coordinators
 *
 * IN pg_conn - database connection
 * IN uid - user performing the add operation
 * IN acct_list - accounts the coordinator manages
 * IN user_cond - users to be added as coordinators
 *    (user_cond->assoc_cond->user_list)
 * RET - error code
 */
extern int
as_p_add_coord(pgsql_conn_t *pg_conn, uint32_t uid,
	       List acct_list, acct_user_cond_t *user_cond)
{
	char *query = NULL, *user = NULL, *acct = NULL;
	char *user_name = NULL, *vals = NULL, *txn_query = NULL;
	ListIterator itr, itr2;
	time_t now = time(NULL);
	int rc = SLURM_SUCCESS;
	acct_user_rec_t *user_rec = NULL;

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
			xstrfmtcat(vals, "CAST((%d, %d, 0, '%s', '%s') AS %s)",
				   now, now, acct, user, acct_coord_table);

			if(txn_query)
				xstrfmtcat(txn_query,
					   ", (%d, %u, '%s', '%s', '%s')",
					   now, DBD_ADD_ACCOUNT_COORDS, user,
					   user_name, acct);
			else
				xstrfmtcat(txn_query,
					   "INSERT INTO %s "
					   "(timestamp, action, name, "
					   "actor, info) "
					   "VALUES (%d, %u, '%s', "
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
		xstrfmtcat(query, "SELECT add_coords(ARRAY[%s]); %s;",
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
			user_rec = xmalloc(sizeof(acct_user_rec_t));
			user_rec->name = xstrdup(user);
			_get_user_coords(pg_conn, user_rec);
			addto_update_list(pg_conn->update_list,
					  ACCT_ADD_COORD, user_rec);
		}
		list_iterator_destroy(itr);
	}
	return SLURM_SUCCESS;
}

/*
 * as_p_remove_coord - remove account coordinators
 *
 * IN pg_conn - database connection
 * IN uid - user performing the remove operation
 * IN acct_list - accounts the coordinator manages
 * IN user_cond - coordinator users to be removed
 *    (user_cond->assoc_cond->user_list)
 * RET - list of coords removed
 */
extern List
as_p_remove_coord(pgsql_conn_t *pg_conn, uint32_t uid,
		  List acct_list, acct_user_cond_t *user_cond)
{
	List user_list = NULL;
	acct_user_rec_t user;
	char *query = NULL, *cond = NULL, *last_user = NULL;
	char *user_name = NULL;
	time_t now = time(NULL);
	int is_admin=0, rc;
	ListIterator itr = NULL;
	acct_user_rec_t *user_rec = NULL;
	List ret_list = NULL;
	PGresult *result = NULL;

	if(!user_cond && !acct_list) {
		error("as/pg: remove_coord: we need something to remove");
		return NULL;
	} else if(user_cond && user_cond->assoc_cond)
		user_list = user_cond->assoc_cond->user_list;

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return NULL;

	memset(&user, 0, sizeof(acct_user_rec_t));
	user.uid = uid;

	is_admin = is_user_admin(pg_conn, uid);
	if (!is_admin && ! is_user_any_coord(pg_conn, &user)) {
		error("as/pg: remove_coord: user not admin or any coord");
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
		if(!is_admin) {
			acct_coord_rec_t *coord = NULL;
			itr = list_iterator_create(user.coord_accts);
			while((coord = list_next(itr))) {
				if(!strcasecmp(coord->name, ROW(1)))
					break;
			}
			list_iterator_destroy(itr);
			if(!coord) {
				error("as/pg: remove_coord: User %s(%d) does "
				      "not have the ability to change this "
				      "account (%s)",
				      user.name, user.uid, ROW(1));
				errno = ESLURM_ACCESS_DENIED;
				list_destroy(ret_list);
				list_destroy(user_list);
				xfree(cond);
				PQclear(result);
				return NULL;
			}
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

	user_name = uid_to_string((uid_t) uid);
	/* cond begins with "AND ()" since constructed with concat_cond_list() */
	/* TODO: fix this */
	rc = aspg_remove_common(pg_conn, DBD_REMOVE_ACCOUNT_COORDS, now,
				user_name, acct_coord_table, (cond + 4), NULL);
	xfree(user_name);
	xfree(cond);
	if (rc != SLURM_SUCCESS) {
		list_destroy(ret_list);
		list_destroy(user_list);
		errno = SLURM_ERROR;
		return NULL;
	}

	/* get the update list set */
	itr = list_iterator_create(user_list);
	while((last_user = list_next(itr))) {
		user_rec = xmalloc(sizeof(acct_user_rec_t));
		user_rec->name = xstrdup(last_user);
		_get_user_coords(pg_conn, user_rec);
		addto_update_list(pg_conn->update_list,
				  ACCT_REMOVE_COORD, user_rec);
	}
	list_iterator_destroy(itr);
	list_destroy(user_list);

	return ret_list;
}


/*
 * get_user_no_assoc_or_no_uid - get users without assoc or uid
 *
 * IN pg_conn: database connection
 * IN assoc_q: association condition
 * OUT ret_list: problem users
 * RET: error code
 */
extern int
get_user_no_assocs_or_no_uid(pgsql_conn_t *pg_conn,
			     acct_association_cond_t *assoc_q,
			     List ret_list)
{
	int rc = SLURM_SUCCESS;
	char *query = NULL;
	PGresult *result = NULL;

	xassert(ret_list);

	query = xstrdup_printf("SELECT name FROM %s WHERE deleted=0",
			       user_table);
	if(assoc_q)
		concat_cond_list(assoc_q->user_list, NULL, "name", &query);

	result = DEF_QUERY_RET;
	if(!result)
		return SLURM_ERROR;

	FOR_EACH_ROW {
		PGresult *result2 = NULL;
		acct_association_rec_t *assoc = NULL;
		uid_t pw_uid;
		char *name = ROW(0);

		if (uid_from_string (name, &pw_uid) < 0) {
			assoc =	xmalloc(sizeof(acct_association_rec_t));
			list_append(ret_list, assoc);
			assoc->id = ACCT_PROBLEM_USER_NO_UID;
			assoc->user = xstrdup(name);
			continue;
		}

		/* See if we have at least 1 association in the system */
		query = xstrdup_printf(
			"SELECT id FROM %s WHERE deleted=0 AND "
			"user_name='%s' LIMIT 1;", assoc_table, name);
		result2 = DEF_QUERY_RET;
		if(!result2) {
			rc = SLURM_ERROR;
			break;
		}
		if (PQntuples(result2) == 0) {
			assoc =	xmalloc(sizeof(acct_association_rec_t));
			list_append(ret_list, assoc);
			assoc->id = ACCT_PROBLEM_USER_NO_ASSOC;
			assoc->user = xstrdup(name);
		}
		PQclear(result2);
	} END_EACH_ROW;
	PQclear(result);
	return rc;
}
