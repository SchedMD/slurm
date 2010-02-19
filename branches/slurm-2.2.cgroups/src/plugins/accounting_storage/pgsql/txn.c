/*****************************************************************************\
 *  txn.c - accounting interface to pgsql - txn related functions.
 *
 *  $Id: txn.c 13061 2008-01-22 21:23:56Z da $
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

char *txn_table = "txn_table";
static storage_field_t txn_table_fields[] = {
	{ "id", "SERIAL" },
	{ "timestamp", "INTEGER DEFAULT 0 NOT NULL" },
	{ "action", "INTEGER NOT NULL" },
	{ "name", "TEXT NOT NULL" },
	{ "actor", "TEXT NOT NULL" },
	{ "info", "TEXT" },
	{ NULL, NULL}
};
static char *txn_table_constraint = ", "
	"PRIMARY KEY (id) "
	")";

/*
 * _concat_txn_cond_list - concat condition list to condition string for txn
 *
 * IN cond_list: list of string values to match the column
 * IN col: column name
 * OUT cond_str: condition string
 */
static void
_concat_txn_cond_list(List cond_list, char *col, char **cond)
{
	int set = 0;
	char *object;
	ListIterator itr;

	if(cond_list && list_count(cond_list)) {
		xstrcat(*cond, " AND (");
		itr = list_iterator_create(cond_list);
		list_iterator_destroy(itr);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(*cond, " OR ");
			xstrfmtcat(*cond,
				   "name LIKE '%%%s%%' OR "
				   "info LIKE '%%%s=%s%%'", object, col, object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*cond, ")");
	}
}

/*
 * _make_txn_cond - turn txn_cond into SQL query condition string
 *
 * IN pg_conn: database connection
 * IN txn_cond: condition of txn to get
 * RET: SQL query condition string, in format of "AND () AND ()..."
 */
static char *
_make_txn_cond(pgsql_conn_t *pg_conn, acct_txn_cond_t *txn_cond)
{
	char *cond = NULL, *assoc_cond = NULL, *id = NULL;
	int set = 0;

	/* handle query for associations first */
	concat_cond_list(txn_cond->acct_list, NULL, "acct", &assoc_cond);
	concat_cond_list(txn_cond->cluster_list, NULL, "cluster", &assoc_cond);
	concat_cond_list(txn_cond->user_list, NULL, "user_name", &assoc_cond);
	if(assoc_cond) {
		List assoc_id_list;
		ListIterator id_itr;
		assoc_id_list = get_assoc_ids(pg_conn, assoc_cond);
		if(assoc_id_list) {
			id_itr = list_iterator_create(assoc_id_list);
			set = 0;
			xstrcat(cond, "AND (");
			while((id = list_next(id_itr))) {
				if (set)
					xstrcat (cond, " OR ");
				xstrfmtcat(cond, "(name='%s' OR "
					   "name LIKE '%%id=%s %%' OR "
					   "name LIKE '%%id=%s)')",
					   id, id, id);
				set = 1;
			}
			xstrcat(cond, ")");
			list_iterator_destroy(id_itr);
			list_destroy(assoc_id_list);
		}
	}

	/* TODO: will these conditions conflict with assoc_cond result? */
	_concat_txn_cond_list(txn_cond->acct_list, "acct", &cond);
	_concat_txn_cond_list(txn_cond->cluster_list, "cluster", &cond);
	_concat_txn_cond_list(txn_cond->user_list, "user_name", &cond);
	concat_cond_list(txn_cond->action_list, NULL, "action", &cond);
	concat_cond_list(txn_cond->actor_list, NULL, "actor", &cond);
	concat_cond_list(txn_cond->id_list, NULL, "id", &cond); /* validity of id not checked */
	concat_like_cond_list(txn_cond->info_list, NULL, "info", &cond);
	concat_like_cond_list(txn_cond->name_list, NULL, "name", &cond);

	if(txn_cond->time_start)
		xstrfmtcat(cond, " AND (timestamp >= %d) ",
			   txn_cond->time_start);
	if (txn_cond->time_end)
		xstrfmtcat(cond, " AND (timestamp < %d)",
			   txn_cond->time_end);
	return cond;
}


/*
 * check_txn_tables - check txn related tables and functions
 * IN pg_conn: database connection
 * IN user: database owner
 * RET: error code
 */
extern int
check_txn_tables(PGconn *db_conn, char *user)
{
	int rc;

	rc = check_table(db_conn, txn_table, txn_table_fields,
			 txn_table_constraint, user);
	return rc;
}

/*
 * as_p_get_txn - get transactions
 *
 * IN pg_conn: database connection
 * IN uid: user performing the get operation
 * IN txn_cond: transactions to get
 * RET: transactions
 */
extern List
as_p_get_txn(pgsql_conn_t *pg_conn, uid_t uid,
	     acct_txn_cond_t *txn_cond)
{
	char *query = NULL, *cond = NULL;
	List txn_list = NULL;
	PGresult *result = NULL;
	char *gt_fields = "id, timestamp, action, name, actor, info";
	enum {
		GT_ID,
		GT_TS,
		GT_ACTION,
		GT_NAME,
		GT_ACTOR,
		GT_INFO,
		GT_COUNT
	};

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return NULL;

	if(txn_cond)
		cond = _make_txn_cond(pg_conn, txn_cond);
	query = xstrdup_printf("SELECT %s FROM %s", gt_fields, txn_table);
	if(cond) {
		xstrfmtcat(query, " WHERE TRUE %s", cond);
		xfree(cond);
	}
	xstrcat(query, " ORDER BY timestamp;");
	result = DEF_QUERY_RET;
	if(!result)
		return NULL;

	txn_list = list_create(destroy_acct_txn_rec);
	FOR_EACH_ROW {
		acct_txn_rec_t *txn = xmalloc(sizeof(acct_txn_rec_t));
		list_append(txn_list, txn);

		txn->action = atoi(ROW(GT_ACTION));
		txn->actor_name = xstrdup(ROW(GT_ACTOR));
		txn->id = atoi(ROW(GT_ID));
		txn->set_info = xstrdup(ROW(GT_INFO));
		txn->timestamp = atoi(ROW(GT_TS));
		txn->where_query = xstrdup(ROW(GT_NAME));

		if(txn_cond && txn_cond->with_assoc_info
		   && (txn->action == DBD_ADD_ASSOCS
		       || txn->action == DBD_MODIFY_ASSOCS
		       || txn->action == DBD_REMOVE_ASSOCS)) {
			/* XXX: name in txn is used as SQL query condition */
			group_concat_assoc_field(pg_conn, "user_name",
						 ROW(GT_NAME), &txn->users);
			group_concat_assoc_field(pg_conn, "acct",
						 ROW(GT_NAME), &txn->accts);
			group_concat_assoc_field(pg_conn, "cluster",
						 ROW(GT_NAME), &txn->clusters);
		}
	} END_EACH_ROW;
	PQclear(result);
	return txn_list;
}

/*
 * add_txn - add a transaction record into database
 * IN now: current time
 * IN action: action performed
 * IN object: objects manipulated
 * IN actor: user name of actor
 * IN info: information of target object
 */
extern int
add_txn(pgsql_conn_t *pg_conn, time_t now,  slurmdbd_msg_type_t action,
	char *object, char *actor, char *info)
{
	int rc;
	char *query = xstrdup_printf(
		"INSERT INTO %s (timestamp, action, name, actor, info) "
		"VALUES (%d, %u, $$%s$$, '%s', $$%s$$);",
		txn_table, now, action, object, actor, info ?: "");
	rc = DEF_QUERY_RET_RC;
	return rc;
}
