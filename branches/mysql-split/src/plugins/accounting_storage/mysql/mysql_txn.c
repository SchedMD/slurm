/*****************************************************************************\
 *  mysql_txn.c - functions dealing with transactions.
 *****************************************************************************
 *
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
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

#include "mysql_txn.h"

extern List mysql_get_txn(mysql_conn_t *mysql_conn, uid_t uid,
			  acct_txn_cond_t *txn_cond)
{
	char *query = NULL;
	char *assoc_extra = NULL;
	char *name_extra = NULL;
	char *extra = NULL;
	char *tmp = NULL;
	List txn_list = NULL;
	ListIterator itr = NULL;
	char *object = NULL;
	int set = 0;
	int i=0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;

	/* if this changes you will need to edit the corresponding enum */
	char *txn_req_inx[] = {
		"id",
		"timestamp",
		"action",
		"name",
		"actor",
		"info"
	};
	enum {
		TXN_REQ_ID,
		TXN_REQ_TS,
		TXN_REQ_ACTION,
		TXN_REQ_NAME,
		TXN_REQ_ACTOR,
		TXN_REQ_INFO,
		TXN_REQ_COUNT
	};

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	if(!txn_cond)
		goto empty;

	/* handle query for associations first */
	if(txn_cond->acct_list && list_count(txn_cond->acct_list)) {
		set = 0;
		if(assoc_extra)
			xstrcat(assoc_extra, " && (");
		else
			xstrcat(assoc_extra, " where (");

		if(name_extra)
			xstrcat(name_extra, " && (");
		else
			xstrcat(name_extra, " (");
		itr = list_iterator_create(txn_cond->acct_list);
		while((object = list_next(itr))) {
			if(set) {
				xstrcat(assoc_extra, " || ");
				xstrcat(name_extra, " || ");
			}

			xstrfmtcat(assoc_extra, "acct=\"%s\"", object);

			xstrfmtcat(name_extra, "(name like \"%%\\\"%s\\\"%%\""
				   " || name=\"%s\")"
				   " || (info like \"%%acct=\\\"%s\\\"%%\")",
				   object, object, object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(assoc_extra, ")");
		xstrcat(name_extra, ")");
	}

	if(txn_cond->cluster_list && list_count(txn_cond->cluster_list)) {
		set = 0;
		if(assoc_extra)
			xstrcat(assoc_extra, " && (");
		else
			xstrcat(assoc_extra, " where (");

		if(name_extra)
			xstrcat(name_extra, " && (");
		else
			xstrcat(name_extra, "(");

		itr = list_iterator_create(txn_cond->cluster_list);
		while((object = list_next(itr))) {
			if(set) {
				xstrcat(assoc_extra, " || ");
				xstrcat(name_extra, " || ");
			}
			xstrfmtcat(assoc_extra, "cluster=\"%s\"", object);

			xstrfmtcat(name_extra, "(name like \"%%\\\"%s\\\"%%\""
				   " || name=\"%s\")"
				   " || (info like \"%%cluster=\\\"%s\\\"%%\")",
				   object, object, object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(assoc_extra, ")");
		xstrcat(name_extra, ")");
	}

	if(txn_cond->user_list && list_count(txn_cond->user_list)) {
		set = 0;
		if(assoc_extra)
			xstrcat(assoc_extra, " && (");
		else
			xstrcat(assoc_extra, " where (");

		if(name_extra)
			xstrcat(name_extra, " && (");
		else
			xstrcat(name_extra, "(");

		itr = list_iterator_create(txn_cond->user_list);
		while((object = list_next(itr))) {
			if(set) {
				xstrcat(assoc_extra, " || ");
				xstrcat(name_extra, " || ");
			}
			xstrfmtcat(assoc_extra, "user=\"%s\"", object);

			xstrfmtcat(name_extra, "(name like \"%%\\\"%s\\\"%%\""
				   " || name=\"%s\")"
				   " || (info like \"%%user=\\\"%s\\\"%%\")",
				   object, object, object);

			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(assoc_extra, ")");
		xstrcat(name_extra, ")");
	}

	if(assoc_extra) {
		query = xstrdup_printf("select id from %s%s",
				       assoc_table, assoc_extra);
		xfree(assoc_extra);

		debug3("%d(%s:%d) query\n%s",
		       mysql_conn->conn, __FILE__, __LINE__, query);
		if(!(result = mysql_db_query_ret(
			     mysql_conn->db_conn, query, 0))) {
			xfree(query);
			return NULL;
		}
		xfree(query);

		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");

		set = 0;

		if(mysql_num_rows(result)) {
			if(name_extra) {
				xstrfmtcat(extra, "(%s) || (", name_extra);
				xfree(name_extra);
			} else
				xstrcat(extra, "(");
			while((row = mysql_fetch_row(result))) {
				if(set)
					xstrcat(extra, " || ");

				xstrfmtcat(extra, "(name like '%%id=%s %%' "
					   "|| name like '%%id=%s)' "
					   "|| name=%s)",
					   row[0], row[0], row[0]);
				set = 1;
			}
			xstrcat(extra, "))");
		} else if(name_extra) {
			xstrfmtcat(extra, "(%s))", name_extra);
			xfree(name_extra);
		}
		mysql_free_result(result);
	}

	/*******************************************/

	if(txn_cond->action_list && list_count(txn_cond->action_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(txn_cond->action_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "action=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(txn_cond->actor_list && list_count(txn_cond->actor_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(txn_cond->actor_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "actor=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(txn_cond->id_list && list_count(txn_cond->id_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(txn_cond->id_list);
		while((object = list_next(itr))) {
			char *ptr = NULL;
			long num = strtol(object, &ptr, 10);
			if ((num == 0) && ptr && ptr[0]) {
				error("Invalid value for txn id (%s)",
				      object);
				xfree(extra);
				list_iterator_destroy(itr);
				return NULL;
			}

			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "id=%s", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(txn_cond->info_list && list_count(txn_cond->info_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(txn_cond->info_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "info like '%%%s%%'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(txn_cond->name_list && list_count(txn_cond->name_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(txn_cond->name_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name like '%%%s%%'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(txn_cond->time_start && txn_cond->time_end) {
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		xstrfmtcat(extra, "timestamp < %d && timestamp >= %d)",
			   txn_cond->time_end, txn_cond->time_start);
	} else if(txn_cond->time_start) {
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		xstrfmtcat(extra, "timestamp >= %d)", txn_cond->time_start);

	} else if(txn_cond->time_end) {
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		xstrfmtcat(extra, "timestamp < %d)", txn_cond->time_end);
	}

	/* make sure we can get the max length out of the database
	 * when grouping the names
	 */
	if(txn_cond->with_assoc_info)
		mysql_db_query(mysql_conn->db_conn,
			       "set session group_concat_max_len=65536;");

empty:
	xfree(tmp);
	xstrfmtcat(tmp, "%s", txn_req_inx[i]);
	for(i=1; i<TXN_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", txn_req_inx[i]);
	}

	query = xstrdup_printf("select %s from %s", tmp, txn_table);

	if(extra) {
		xstrfmtcat(query, "%s", extra);
		xfree(extra);
	}
	xstrcat(query, " order by timestamp;");

	xfree(tmp);

	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, __FILE__, __LINE__, query);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return NULL;
	}
	xfree(query);

	txn_list = list_create(destroy_acct_txn_rec);

	while((row = mysql_fetch_row(result))) {
		acct_txn_rec_t *txn = xmalloc(sizeof(acct_txn_rec_t));

		list_append(txn_list, txn);

		txn->action = atoi(row[TXN_REQ_ACTION]);
		txn->actor_name = xstrdup(row[TXN_REQ_ACTOR]);
		txn->id = atoi(row[TXN_REQ_ID]);
		txn->set_info = xstrdup(row[TXN_REQ_INFO]);
		txn->timestamp = atoi(row[TXN_REQ_TS]);
		txn->where_query = xstrdup(row[TXN_REQ_NAME]);

		if(txn_cond && txn_cond->with_assoc_info
		   && (txn->action == DBD_ADD_ASSOCS
		       || txn->action == DBD_MODIFY_ASSOCS
		       || txn->action == DBD_REMOVE_ASSOCS)) {
			MYSQL_RES *result2 = NULL;
			MYSQL_ROW row2;

			query = xstrdup_printf(
				"select "
				"group_concat(distinct user order by user), "
				"group_concat(distinct acct order by acct), "
				"group_concat(distinct cluster "
				"order by cluster) from %s where %s",
				assoc_table, row[TXN_REQ_NAME]);
			debug4("%d(%s:%d) query\n%s", mysql_conn->conn,
			       __FILE__, __LINE__, query);
			if(!(result2 = mysql_db_query_ret(
				     mysql_conn->db_conn, query, 0))) {
				xfree(query);
				continue;
			}
			xfree(query);

			if((row2 = mysql_fetch_row(result2))) {
				if(row2[0] && row2[0][0])
					txn->users = xstrdup(row2[0]);
				if(row2[1] && row2[1][0])
					txn->accts = xstrdup(row2[1]);
				if(row2[2] && row2[2][0])
					txn->clusters = xstrdup(row2[2]);
			}
			mysql_free_result(result2);
		}
	}
	mysql_free_result(result);

	return txn_list;
}
