/*****************************************************************************\
 *  as_pg_txn.c - accounting interface to pgsql - txn related functions.
 *
 *  $Id: as_pg_txn.c 13061 2008-01-22 21:23:56Z da $
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
static char *txn_table_name = "txn_table";
char *txn_table = "public.txn_table";
static storage_field_t txn_table_fields[] = {
	{ "id", "SERIAL" },
	{ "timestamp", "INTEGER DEFAULT 0 NOT NULL" },
	{ "action", "INTEGER NOT NULL" },
	{ "name", "TEXT NOT NULL" },
	{ "actor", "TEXT NOT NULL" },
	{ "cluster", "TEXT DEFAULT '' NOT NULL" },
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

/* get group_concat-ed field value of assocs */
static int
_group_concat_assoc_field(pgsql_conn_t *pg_conn, char *cluster,
			  char *assoc_cond, char *field,
			  char **val)
{
	DEF_VARS;

	query = xstrdup_printf(
		"SELECT DISTINCT %s FROM %s.%s WHERE deleted=0 AND %s "
		"ORDER BY %s;", field, cluster, assoc_table, assoc_cond,
		field);
	result = DEF_QUERY_RET;
	if (!result)
		return SLURM_ERROR;

	FOR_EACH_ROW {
		xstrcat(*val, ROW(0));
		xstrcat(*val, " ");
	} END_EACH_ROW;
	PQclear(result);
	return SLURM_SUCCESS;
}

/*
 * _make_txn_cond - turn txn_cond into SQL query condition string
 *
 * IN pg_conn: database connection
 * IN txn_cond: condition of txn to get
 * RET: SQL query condition string, in format of "AND () AND ()..."
 */
static char *
_make_txn_cond(pgsql_conn_t *pg_conn, slurmdb_txn_cond_t *txn_cond)
{
	DEF_VARS;
	char *cond = NULL, *assoc_cond = NULL;
	int set = 0;

	/* handle query for associations first */
	concat_cond_list(txn_cond->acct_list, NULL, "acct", &assoc_cond);
	concat_cond_list(txn_cond->user_list, NULL, "user_name", &assoc_cond);
	if(assoc_cond) {
		FOR_EACH_CLUSTER(txn_cond->cluster_list) {
			query = xstrdup_printf(
				"SELECT id_assoc FROM %s.%s WHERE TRUE %s",
				cluster_name, assoc_table, assoc_cond);
			result = DEF_QUERY_RET;
			if (!result)
				break;
			if (PQntuples(result) == 0) {
				PQclear(result);
				continue;
			}
			if (!cond)
				xstrfmtcat(cond, " AND ( (cluster='%s' AND (",
					   cluster_name);
			else
				xstrfmtcat(cond, " OR (cluster='%s' AND (",
					   cluster_name);
			set = 0;
			FOR_EACH_ROW {
				if (set)
					xstrcat(cond, " OR ");
				xstrfmtcat(cond, "name LIKE '%%id_assoc=%s %%' "
					   " OR name LIKE '%%id_assoc=%s)')",
					   ROW(0), ROW(0));
				set = 1;
			} END_EACH_ROW;
			PQclear(result);
			xstrcat(cond, "))");
		} END_EACH_CLUSTER;
		if (cond)
			xstrcat(cond, ")"); /* first " AND ( " */
	}
	       
	_concat_txn_cond_list(txn_cond->acct_list, "acct", &cond);
	_concat_txn_cond_list(txn_cond->cluster_list, "cluster", &cond);
	_concat_txn_cond_list(txn_cond->user_list, "user_name", &cond);
	
	concat_cond_list(txn_cond->action_list, NULL, "action", &cond);
	concat_cond_list(txn_cond->actor_list, NULL, "actor", &cond);
	concat_cond_list(txn_cond->id_list, NULL, "id", &cond); /* validity of id not checked */
	concat_like_cond_list(txn_cond->info_list, NULL, "info", &cond);
	concat_like_cond_list(txn_cond->name_list, NULL, "name", &cond);

	if(txn_cond->time_start)
		xstrfmtcat(cond, " AND (timestamp >= %ld) ",
			   txn_cond->time_start);
	if (txn_cond->time_end)
		xstrfmtcat(cond, " AND (timestamp < %ld)",
			   txn_cond->time_end);
	return cond;
}


/*
 * check_txn_tables - check txn related tables and functions
 */
extern int
check_txn_tables(PGconn *db_conn)
{
	int rc;

	rc = check_table(db_conn, "public", txn_table_name,
			 txn_table_fields, txn_table_constraint);
	return rc;
}

/*
 * as_pg_get_txn - get transactions
 *
 * IN pg_conn: database connection
 * IN uid: user performing the get operation
 * IN txn_cond: transactions to get
 * RET: transactions
 */
extern List
as_pg_get_txn(pgsql_conn_t *pg_conn, uid_t uid,
	      slurmdb_txn_cond_t *txn_cond)
{
	DEF_VARS;
	char *cond = NULL;
	List txn_list = NULL;
	char *gt_fields = "id,timestamp,action,name,actor,cluster,info";
	enum {
		F_ID,
		F_TS,
		F_ACTION,
		F_NAME,
		F_ACTOR,
		F_CLUSTER,
		F_INFO,
		F_COUNT
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

	txn_list = list_create(slurmdb_destroy_txn_rec);
	FOR_EACH_ROW {
		slurmdb_txn_rec_t *txn = xmalloc(sizeof(slurmdb_txn_rec_t));
		list_append(txn_list, txn);

		txn->action = atoi(ROW(F_ACTION));
		txn->actor_name = xstrdup(ROW(F_ACTOR));
		txn->id = atoi(ROW(F_ID));
		txn->set_info = xstrdup(ROW(F_INFO));
		txn->timestamp = atoi(ROW(F_TS));
		txn->where_query = xstrdup(ROW(F_NAME));
		txn->clusters = xstrdup(ROW(F_CLUSTER));

		if(txn_cond && txn_cond->with_assoc_info
		   && (txn->action == DBD_ADD_ASSOCS
		       || txn->action == DBD_MODIFY_ASSOCS
		       || txn->action == DBD_REMOVE_ASSOCS)) {
			/* XXX: name in txn is used as SQL query condition */
			_group_concat_assoc_field(pg_conn, txn->clusters,
						  ROW(F_NAME), "user_name",
						  &txn->users);
			_group_concat_assoc_field(pg_conn, txn->clusters,
						  ROW(F_NAME), "acct",
						  &txn->accts);
		}
	} END_EACH_ROW;
	PQclear(result);
	return txn_list;
}

/*
 * add_txn - add a transaction record into database
 * IN now: current time
 * IN cluster: cluster involved
 * IN object: objects manipulated
 * IN actor: user name of actor
 * IN info: information of target object
 */
extern int
add_txn(pgsql_conn_t *pg_conn, time_t now,  char *cluster,
      slurmdbd_msg_type_t action, char *object, char *actor, char *info)
{
      char *query = xstrdup_printf(
              "INSERT INTO %s (timestamp, cluster, action, name, actor, "
              "  info) VALUES (%ld, '%s', %d, $$%s$$, '%s', $$%s$$);",
              txn_table, now, cluster, (int)action, object, actor, info ?: "");
      return DEF_QUERY_RET_RC;
}
