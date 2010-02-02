/*****************************************************************************\
 *  account.c - accounting interface to pgsql - account related functions.
 *
 *  $Id: account.c 13061 2008-01-22 21:23:56Z da $
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

char *acct_table = "acct_table";
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

/*
 * _create_function_add_acct -  create a PL/pgSQL function to add account
 *
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_add_acct(PGconn *db_conn)
{
	/* try INSERT first, instead of UPDATE, for performance */
	/* TODO: could the loop be removed? */
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION add_acct "
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
 * _get_acct_coords - fill in all the users that are coordinator for
 *  this account. Also fill in coordinators from parent accounts.
 *
 * IN pg_conn: database connection
 * IN/OUT acct: account record
 * RET: error code
 */
static int
_get_acct_coords(pgsql_conn_t *pg_conn, acct_account_rec_t *acct)
{
	char *query = NULL;
	acct_coord_rec_t *coord = NULL;
	PGresult *result = NULL;

	if(!acct) {
		error("as/pg: _get_acct_coords: account not given");
		return SLURM_ERROR;
	}

	if(!acct->coordinators)
		acct->coordinators = list_create(destroy_acct_coord_rec);

	/* get direct coords */
	query = xstrdup_printf("SELECT user_name FROM %s "
			       "WHERE acct='%s' AND deleted=0",
			       acct_coord_table, acct->name);
	result = DEF_QUERY_RET;
	if(!result)
		return SLURM_ERROR;

	FOR_EACH_ROW {
		coord = xmalloc(sizeof(acct_coord_rec_t));
		list_append(acct->coordinators, coord);
		coord->name = xstrdup(ROW(0));
		coord->direct = 1;
	} END_EACH_ROW;
	PQclear(result);

	/* get parent account coords */
	query = xstrdup_printf(
		"SELECT DISTINCT t0.user_name FROM %s AS t0, %s AS t1, "
		"  %s AS t2 WHERE (t1.acct='%s' AND t1.user_name='' "
		"  AND (t1.lft>t2.lft AND t1.rgt < t2.rgt)) "
		"  AND t0.deleted=0 AND t0.acct=t2.acct "
		"  AND t2.acct != '%s'",
		acct_coord_table, assoc_table, assoc_table,
		acct->name, acct->name);
	result = DEF_QUERY_RET;
	if(!result)
		return SLURM_ERROR;

	FOR_EACH_ROW {
		coord = xmalloc(sizeof(acct_coord_rec_t));
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
check_acct_tables(PGconn *db_conn, char *user)
{
	int rc;

	rc = check_table(db_conn, acct_table, acct_table_fields,
			 acct_table_constraints, user);
	rc |= _create_function_add_acct(db_conn);
	return rc;
}

/*
 * as_p_add_accts - add accounts
 *
 * IN pg_conn: database connection
 * IN uid: user performing the add operation
 * IN acct_list: accounts to add
 * RET: error code
 */
extern int
as_p_add_accts(pgsql_conn_t *pg_conn, uint32_t uid, List acct_list)
{
	ListIterator itr = NULL;
	acct_account_rec_t *object = NULL;
	List assoc_list = NULL;
	int rc = SLURM_SUCCESS;
	char *user_name = NULL, *query = NULL, *txn_query = NULL;
	char *rec = NULL, *info = NULL;
	time_t now = time(NULL);

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	assoc_list = list_create(destroy_acct_association_rec);
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
		rec = xstrdup_printf("(%d, %d, 0, '%s', '%s', '%s')", now,
				     now, object->name, object->description,
				     object->organization);
		query = xstrdup_printf("SELECT add_acct(%s);", rec);
		xfree(rec);
		rc = DEF_QUERY_RET_RC;
		if(rc != SLURM_SUCCESS) {
			error("as/pg: couldn't add acct");
			continue;
		}

		info = xstrdup_printf("description='%s', organization='%s'",
				      object->description,
				      object->organization);
		if(txn_query)
			xstrfmtcat(txn_query,
				   ", (%d, %u, '%s', '%s', $$%s$$)",
				   now, DBD_ADD_ACCOUNTS, object->name,
				   user_name, info);
		else
			xstrfmtcat(txn_query,
				   "INSERT INTO %s "
				   "(timestamp, action, name, actor, info) "
				   "VALUES (%d, %u, '%s', '%s', $$%s$$)",
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
				/* TODO: why succees if add txn failed? */
/* 				rc = SLURM_SUCCESS; */
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
 * as_p_modify_accounts - modify accounts
 *
 * IN pg_conn: database connection
 * IN uid: user performing the modify operation
 * IN acct_cond: accounts to modify
 * IN acct: attribute of accounts after modification
 * RET: list of accounts modified
 */
extern List
as_p_modify_accounts(pgsql_conn_t *pg_conn, uint32_t uid,
		     acct_account_cond_t *acct_cond,
		     acct_account_rec_t *acct)
{
	List ret_list = NULL;
	int rc = SLURM_SUCCESS;
	char *object = NULL, *user_name = NULL;
	char *vals = NULL, *cond = NULL, *query = NULL, *name_char = NULL;
	PGresult *result = NULL;
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

	/* cond with "AND ()" prefix */
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
	rc = aspg_modify_common(pg_conn, DBD_MODIFY_ACCOUNTS, now,
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

/*
 * as_p_remove_accts - remove accounts
 *
 * IN pg_conn: database connection
 * IN uid: user performing the remove operation
 * IN acct_cond: accounts to remove
 * RET: list of accounts removed
 */
extern List
as_p_remove_accts(pgsql_conn_t *pg_conn, uint32_t uid,
			    acct_account_cond_t *acct_cond)
{
	List ret_list = NULL;
	List coord_list = NULL;
	int rc = SLURM_SUCCESS;
	char *user_name = NULL, *cond = NULL;
	char *query = NULL, *name_char = NULL, *assoc_char = NULL;
	PGresult *result = NULL;
	time_t now = time(NULL);

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
			xstrfmtcat(assoc_char, "t1.acct='%s'", object);
			rc = 1;
		} else  {
			xstrfmtcat(name_char, " OR name='%s'", object);
			xstrfmtcat(assoc_char, " OR t1.acct='%s'", object);
		}
	} END_EACH_ROW;
	PQclear(result);

	if(!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		debug3("as/pg: remove_accts: didn't effect anything");
		return ret_list;
	}

	/* remove these accounts from the coord's that have it */
	coord_list = acct_storage_p_remove_coord(pg_conn, uid, ret_list, NULL);
	if(coord_list)
		list_destroy(coord_list);

	user_name = uid_to_string((uid_t) uid);
	rc = aspg_remove_common(pg_conn, DBD_REMOVE_ACCOUNTS, now,
				user_name, acct_table, name_char, assoc_char);
	xfree(user_name);
	xfree(name_char);
	xfree(assoc_char);
	if (rc == SLURM_ERROR) {
		list_destroy(ret_list);
		return NULL;
	}
	return ret_list;
}

/*
 * as_p_get_accts - get accounts
 *
 * IN pg_conn: database connection
 * IN uid: user performing the get operation
 * IN acct_cond: accounts to get
 * RET: list of accounts
 */
extern List
as_p_get_accts(pgsql_conn_t *pg_conn, uid_t uid,
			 acct_account_cond_t *acct_cond)
{
	char *query = NULL;
	char *cond = NULL;
	List acct_list = NULL;
	ListIterator itr = NULL;
	int set=0, is_admin=1;
	PGresult *result = NULL;
	uint16_t private_data = 0;
	acct_user_rec_t user;	/* no need to free lists */

	char *ga_fields = "name, description, organization";
	enum {
		GA_NAME,
		GA_DESC,
		GA_ORG,
		GA_COUNT
	};

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return NULL;

	memset(&user, 0, sizeof(acct_user_rec_t));
	user.uid = uid;

	private_data = slurm_get_private_data();
	if (private_data & PRIVATE_DATA_ACCOUNTS) {
		is_admin = is_user_admin(pg_conn, uid);
		if (!is_admin && ! is_user_any_coord(pg_conn, &user)) {
			errno = ESLURM_ACCESS_DENIED;
			return NULL;
		}
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
	if(!is_admin && (private_data & PRIVATE_DATA_ACCOUNTS)) {
		acct_coord_rec_t *coord = NULL;
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

	acct_list = list_create(destroy_acct_account_rec);

	if(acct_cond && acct_cond->with_assocs) {
		if(!acct_cond->assoc_cond)
			acct_cond->assoc_cond = xmalloc(
				sizeof(acct_association_cond_t));
		else if(acct_cond->assoc_cond->acct_list)
			list_destroy(acct_cond->assoc_cond->acct_list);
		acct_cond->assoc_cond->acct_list = list_create(NULL);
	}

	FOR_EACH_ROW {
		acct_account_rec_t *acct = xmalloc(sizeof(acct_account_rec_t));
		list_append(acct_list, acct);

		acct->name =  xstrdup(ROW(GA_NAME));
		acct->description = xstrdup(ROW(GA_DESC));
		acct->organization = xstrdup(ROW(GA_ORG));
		if(acct_cond && acct_cond->with_coords)
			_get_acct_coords(pg_conn, acct);
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
		acct_account_rec_t *acct = NULL;
		acct_association_rec_t *assoc = NULL;
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
						destroy_acct_association_rec);
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

/*
 * get_acct_no_assocs - get accounts without associations
 *
 * IN pg_conn: database connection
 * IN assoc_q: association condition
 * OUT ret_list: problem accounts
 * RET: error code
 */
extern int
get_acct_no_assocs(pgsql_conn_t *pg_conn, acct_association_cond_t *assoc_q,
		   List ret_list)
{
	int rc = SLURM_SUCCESS;
	char *query = NULL;
	PGresult *result = NULL;

	xassert(ret_list);

	query = xstrdup_printf("SELECT name FROM %s WHERE deleted=0",
			       acct_table);
	if (assoc_q)
		concat_cond_list(assoc_q->acct_list, NULL, "name", &query);

	result = DEF_QUERY_RET;
	if(!result)
		return SLURM_ERROR;

	FOR_EACH_ROW {
		PGresult *result2 = NULL;
		acct_association_rec_t *assoc = NULL;
		/* See if we have at least 1 association in the system */
		query = xstrdup_printf("SELECT id FROM %s "
				       "WHERE deleted=0 AND "
				       "acct='%s' LIMIT 1;",
				       assoc_table, ROW(0));
		result2 = DEF_QUERY_RET;
		if(!result2) {
			rc = SLURM_ERROR;
			break;
		}
		if (PQntuples(result2) == 0) {
			assoc =	xmalloc(sizeof(acct_association_rec_t));
			list_append(ret_list, assoc);
			assoc->id = ACCT_PROBLEM_ACCT_NO_ASSOC;
			assoc->acct = xstrdup(ROW(0));
		}
		PQclear(result2);
	} END_EACH_ROW;
	PQclear(result);

	return rc;
}

/*
 * get_acct_no_users - get accounts without users
 *
 * IN pg_conn: database connection
 * IN assoc_q: association condition
 * OUT ret_list: problem accounts
 * RET: error code
 */
extern int
get_acct_no_users(pgsql_conn_t *pg_conn, acct_association_cond_t *assoc_q,
		   List ret_list)
{
	int rc = SLURM_SUCCESS;
	char *query = NULL;
	PGresult *result = NULL;

	xassert(ret_list);

	/* if this changes you will need to edit the corresponding enum */
	char *ga_fields = "id, user_name, acct, cluster, partition, parent_acct";
	enum {
		GA_ID,
		GA_USER,
		GA_ACCT,
		GA_CLUSTER,
		GA_PART,
		GA_PARENT,
		GA_COUNT
	};

	/* only get the account associations without children assoc */
	query = xstrdup_printf("SELECT DISTINCT %s FROM %s WHERE deleted=0 "
			       "  AND user_name='' AND lft=(rgt-1) ",
			       ga_fields, assoc_table);
	if (assoc_q) {
		concat_cond_list(assoc_q->acct_list, NULL, "acct", &query);
		concat_cond_list(assoc_q->cluster_list, NULL, "cluster", &query);
		/* we are querying acct associations */
/* 		concat_cond_list(assoc_q->user_list, NULL, "user_name", &query); */
		/* user_name='' ==> partition='' */
/* 		concat_cond_list(assoc_q->partition_list, NULL, "partition", &query); */
	}
	xstrcat(query, " ORDER BY cluster, acct;");
	result = DEF_QUERY_RET;
	if(!result)
		return SLURM_ERROR;

	FOR_EACH_ROW {
		acct_association_rec_t *assoc =
			xmalloc(sizeof(acct_association_rec_t));
		list_append(ret_list, assoc);
		assoc->id = ACCT_PROBLEM_ACCT_NO_USERS;
/* 		if(ROW(GA_USER)[0]) */
/* 			assoc->user = xstrdup(ROW(GA_USER)); */
		assoc->acct = xstrdup(ROW(GA_ACCT));
		assoc->cluster = xstrdup(ROW(GA_CLUSTER));
		if(ROW(GA_PARENT)[0])
			assoc->parent_acct = xstrdup(ROW(GA_PARENT));
/* 		if(ROW(GA_PART)[0]) */
/* 			assoc->partition = xstrdup(ROW(GA_PART)); */
	} END_EACH_ROW;
	PQclear(result);
	return rc;
}
