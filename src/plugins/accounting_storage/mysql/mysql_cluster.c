/*****************************************************************************\
 *  mysql_cluster.c - functions dealing with clusters.
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

#include "mysql_assoc.h"
#include "mysql_cluster.h"
#include "mysql_wckey.h"

extern int mysql_add_clusters(mysql_conn_t *mysql_conn, uint32_t uid,
			      List cluster_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	acct_cluster_rec_t *object = NULL;
	char *cols = NULL, *vals = NULL, *extra = NULL,
		*query = NULL, *tmp_extra = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	int affect_rows = 0;
	int added = 0;
	List assoc_list = NULL;
	acct_association_rec_t *assoc = NULL;

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	assoc_list = list_create(destroy_acct_association_rec);

	user_name = uid_to_string((uid_t) uid);
	itr = list_iterator_create(cluster_list);
	while((object = list_next(itr))) {
		if(!object->name || !object->name[0]) {
			error("We need a cluster name to add.");
			rc = SLURM_ERROR;
			continue;
		}

		xstrcat(cols, "creation_time, mod_time, acct, cluster");
		xstrfmtcat(vals, "%d, %d, 'root', \"%s\"",
			   now, now, object->name);
		xstrfmtcat(extra, ", mod_time=%d", now);
		if(object->root_assoc)
			setup_association_limits(object->root_assoc, &cols,
						 &vals, &extra,
						 QOS_LEVEL_SET, 1);
		xstrfmtcat(query,
			   "insert into %s (creation_time, mod_time, "
			   "name, classification) "
			   "values (%d, %d, \"%s\", %u) "
			   "on duplicate key update deleted=0, mod_time=%d, "
			   "control_host='', control_port=0;",
			   cluster_table,
			   now, now, object->name, object->classification,
			   now);
		debug3("%d(%d) query\n%s",
		       mysql_conn->conn, __LINE__, query);
		rc = mysql_db_query(mysql_conn->db_conn, query);
		xfree(query);
		if(rc != SLURM_SUCCESS) {
			error("Couldn't add cluster %s", object->name);
			xfree(extra);
			xfree(cols);
			xfree(vals);
			added=0;
			break;
		}

		affect_rows = last_affected_rows(mysql_conn->db_conn);

		if(!affect_rows) {
			debug2("nothing changed %d", affect_rows);
			xfree(extra);
			xfree(cols);
			xfree(vals);
			continue;
		}

		xstrfmtcat(query,
			   "SELECT @MyMax := coalesce(max(rgt), 0) FROM %s "
			   "FOR UPDATE;",
			   assoc_table);
		xstrfmtcat(query,
			   "insert into %s (%s, lft, rgt) "
			   "values (%s, @MyMax+1, @MyMax+2) "
			   "on duplicate key update deleted=0, "
			   "id=LAST_INSERT_ID(id)%s;",
			   assoc_table, cols,
			   vals,
			   extra);

		xfree(cols);
		xfree(vals);
		debug3("%d(%d) query\n%s",
		       mysql_conn->conn, __LINE__, query);

		rc = mysql_db_query(mysql_conn->db_conn, query);
		xfree(query);

		if(rc != SLURM_SUCCESS) {
			error("Couldn't add cluster root assoc");
			xfree(extra);
			added=0;
			break;
		}

		/* we always have a ', ' as the first 2 chars */
		tmp_extra = fix_double_quotes(extra+2);

		xstrfmtcat(query,
			   "insert into %s "
			   "(timestamp, action, name, actor, info) "
			   "values (%d, %u, \"%s\", \"%s\", \"%s\");",
			   txn_table, now, DBD_ADD_CLUSTERS,
			   object->name, user_name, tmp_extra);
		xfree(tmp_extra);
		xfree(extra);
		debug4("%d(%d) query\n%s",
		       mysql_conn->conn, __LINE__, query);

		rc = mysql_db_query(mysql_conn->db_conn, query);
		xfree(query);
		if(rc != SLURM_SUCCESS) {
			error("Couldn't add txn");
		} else
			added++;

		/* Add user root by default to run from the root
		 * association.  This gets popped off so we need to
		 * read it every time here.
		 */
		assoc = xmalloc(sizeof(acct_association_rec_t));
		init_acct_association_rec(assoc);
		list_append(assoc_list, assoc);

		assoc->cluster = xstrdup(object->name);
		assoc->user = xstrdup("root");
		assoc->acct = xstrdup("root");

		if(mysql_add_assocs(mysql_conn, uid, assoc_list)
		   == SLURM_ERROR) {
			error("Problem adding root user association");
			rc = SLURM_ERROR;
		}

	}
	list_iterator_destroy(itr);
	xfree(user_name);

	list_destroy(assoc_list);

	if(!added) {
		if(mysql_conn->rollback) {
			mysql_db_rollback(mysql_conn->db_conn);
		}
		list_flush(mysql_conn->update_list);
	}

	return rc;
}

extern List mysql_modify_clusters(mysql_conn_t *mysql_conn, uint32_t uid,
				  acct_cluster_cond_t *cluster_cond,
				  acct_cluster_rec_t *cluster)
{
	ListIterator itr = NULL;
	List ret_list = NULL;
	int rc = SLURM_SUCCESS;
	char *object = NULL;
	char *vals = NULL, *extra = NULL, *query = NULL,
		*name_char = NULL, *send_char = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	int set = 0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	bool clust_reg = false;

	/* If you need to alter the default values of the cluster use
	 * modify_associations since this is used only for registering
	 * the controller when it loads
	 */

	if(!cluster_cond || !cluster) {
		error("we need something to change");
		return NULL;
	}

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	xstrcat(extra, "where deleted=0");
	if(cluster_cond->cluster_list
	   && list_count(cluster_cond->cluster_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(cluster_cond->cluster_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name='%s'", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(cluster_cond->classification) {
		xstrfmtcat(extra, " && (classification & %u)",
			   cluster_cond->classification);
	}

	set = 0;
	if(cluster->control_host) {
		xstrfmtcat(vals, ", control_host='%s'", cluster->control_host);
		set++;
		clust_reg = true;
	}

	if(cluster->control_port) {
		xstrfmtcat(vals, ", control_port=%u", cluster->control_port);
		set++;
		clust_reg = true;
	}

	if(cluster->rpc_version) {
		xstrfmtcat(vals, ", rpc_version=%u", cluster->rpc_version);
		set++;
		clust_reg = true;
	}

	if(cluster->classification) {
		xstrfmtcat(vals, ", classification=%u",
			   cluster->classification);
	}

	if(!vals) {
		xfree(extra);
		errno = SLURM_NO_CHANGE_IN_DATA;
		error("Nothing to change");
		return NULL;
	} else if(clust_reg && (set != 3)) {
		xfree(vals);
		xfree(extra);
		errno = EFAULT;
		error("Need control host, port and rpc version "
		      "to register a cluster");
		return NULL;
	}


	xstrfmtcat(query, "select name, control_port from %s %s;",
		   cluster_table, extra);

	xfree(extra);
	debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		xfree(query);
		xfree(vals);
		error("no result given for %s", extra);
		return NULL;
	}

	rc = 0;
	ret_list = list_create(slurm_destroy_char);
	while((row = mysql_fetch_row(result))) {
		object = xstrdup(row[0]);

		list_append(ret_list, object);
		if(!rc) {
			xstrfmtcat(name_char, "name='%s'", object);
			rc = 1;
		} else  {
			xstrfmtcat(name_char, " || name='%s'", object);
		}
	}
	mysql_free_result(result);

	if(!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		debug3("didn't effect anything\n%s", query);
		xfree(vals);
		xfree(query);
		return ret_list;
	}
	xfree(query);

	if(vals) {
		send_char = xstrdup_printf("(%s)", name_char);
		user_name = uid_to_string((uid_t) uid);
		rc = modify_common(mysql_conn, DBD_MODIFY_CLUSTERS, now,
				    user_name, cluster_table, send_char, vals);
		xfree(user_name);
		if (rc == SLURM_ERROR) {
			error("Couldn't modify cluster 1");
			list_destroy(ret_list);
			ret_list = NULL;
			goto end_it;
		}
	}

end_it:
	xfree(name_char);
	xfree(vals);
	xfree(send_char);

	return ret_list;
}

extern List mysql_remove_clusters(mysql_conn_t *mysql_conn, uint32_t uid,
				  acct_cluster_cond_t *cluster_cond)
{
	ListIterator itr = NULL;
	List ret_list = NULL;
	List tmp_list = NULL;
	int rc = SLURM_SUCCESS;
	char *object = NULL;
	char *extra = NULL, *query = NULL,
		*name_char = NULL, *assoc_char = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	int set = 0;
	acct_wckey_cond_t wckey_cond;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;

	if(!cluster_cond) {
		error("we need something to change");
		return NULL;
	}

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	xstrcat(extra, "where deleted=0");
	if(cluster_cond->cluster_list
	   && list_count(cluster_cond->cluster_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(cluster_cond->cluster_list);
		while((object = list_next(itr))) {
			if(!object[0])
				continue;
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(!extra) {
		error("Nothing to remove");
		return NULL;
	}

	query = xstrdup_printf("select name from %s %s;", cluster_table, extra);
	xfree(extra);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return NULL;
	}
	rc = 0;
	ret_list = list_create(slurm_destroy_char);
	while((row = mysql_fetch_row(result))) {
		char *object = xstrdup(row[0]);
		list_append(ret_list, object);
		if(!rc) {
			xstrfmtcat(name_char, "name=\"%s\"", object);
			xstrfmtcat(extra, "t2.cluster=\"%s\"", object);
			xstrfmtcat(assoc_char, "cluster=\"%s\"", object);
			rc = 1;
		} else  {
			xstrfmtcat(name_char, " || name=\"%s\"", object);
			xstrfmtcat(extra, " || t2.cluster=\"%s\"", object);
			xstrfmtcat(assoc_char, " || cluster=\"%s\"", object);
		}
	}
	mysql_free_result(result);

	if(!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		debug3("didn't effect anything\n%s", query);
		xfree(query);
		return ret_list;
	}
	xfree(query);

	/* We need to remove these clusters from the wckey table */
	memset(&wckey_cond, 0, sizeof(acct_wckey_cond_t));
	wckey_cond.cluster_list = ret_list;
	tmp_list = mysql_remove_wckeys(
		mysql_conn, uid, &wckey_cond);
	if(tmp_list)
		list_destroy(tmp_list);

	/* We should not need to delete any cluster usage just set it
	 * to deleted */
	xstrfmtcat(query,
		   "update %s set period_end=%d where period_end=0 && (%s);"
		   "update %s set mod_time=%d, deleted=1 where (%s);"
		   "update %s set mod_time=%d, deleted=1 where (%s);"
		   "update %s set mod_time=%d, deleted=1 where (%s);",
		   event_table, now, assoc_char,
		   cluster_day_table, now, assoc_char,
		   cluster_hour_table, now, assoc_char,
		   cluster_month_table, now, assoc_char);
	xfree(assoc_char);
	debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
	rc = mysql_db_query(mysql_conn->db_conn, query);
	xfree(query);
	if(rc != SLURM_SUCCESS) {
		if(mysql_conn->rollback) {
			mysql_db_rollback(mysql_conn->db_conn);
		}
		list_flush(mysql_conn->update_list);
		list_destroy(ret_list);
		xfree(name_char);
		xfree(extra);
		return NULL;
	}

	assoc_char = xstrdup_printf("t2.acct='root' && (%s)", extra);
	xfree(extra);

	user_name = uid_to_string((uid_t) uid);
	rc = remove_common(mysql_conn, DBD_REMOVE_CLUSTERS, now,
			    user_name, cluster_table, name_char, assoc_char);
	xfree(user_name);
	xfree(name_char);
	xfree(assoc_char);
	if (rc  == SLURM_ERROR) {
		list_destroy(ret_list);
		return NULL;
	}

	return ret_list;
}

extern List mysql_get_clusters(mysql_conn_t *mysql_conn, uid_t uid,
			       acct_cluster_cond_t *cluster_cond)
{
	char *query = NULL;
	char *extra = NULL;
	char *tmp = NULL;
	List cluster_list = NULL;
	ListIterator itr = NULL;
	char *object = NULL;
	int set = 0;
	int i=0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	acct_association_cond_t assoc_cond;
	ListIterator assoc_itr = NULL;
	acct_cluster_rec_t *cluster = NULL;
	acct_association_rec_t *assoc = NULL;
	List assoc_list = NULL;

	/* if this changes you will need to edit the corresponding enum */
	char *cluster_req_inx[] = {
		"name",
		"classification",
		"control_host",
		"control_port",
		"rpc_version",
	};
	enum {
		CLUSTER_REQ_NAME,
		CLUSTER_REQ_CLASS,
		CLUSTER_REQ_CH,
		CLUSTER_REQ_CP,
		CLUSTER_REQ_VERSION,
		CLUSTER_REQ_COUNT
	};

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;


	if(!cluster_cond) {
		xstrcat(extra, "where deleted=0");
		goto empty;
	}

	if(cluster_cond->with_deleted)
		xstrcat(extra, "where (deleted=0 || deleted=1)");
	else
		xstrcat(extra, "where deleted=0");

	if(cluster_cond->cluster_list
	   && list_count(cluster_cond->cluster_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(cluster_cond->cluster_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

empty:

	xfree(tmp);
	i=0;
	xstrfmtcat(tmp, "%s", cluster_req_inx[i]);
	for(i=1; i<CLUSTER_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", cluster_req_inx[i]);
	}

	query = xstrdup_printf("select %s from %s %s",
			       tmp, cluster_table, extra);
	xfree(tmp);
	xfree(extra);

	debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return NULL;
	}
	xfree(query);

	cluster_list = list_create(destroy_acct_cluster_rec);

	memset(&assoc_cond, 0, sizeof(acct_association_cond_t));

	if(cluster_cond) {
		/* I don't think we want the with_usage flag here.
		 * We do need the with_deleted though. */
		//assoc_cond.with_usage = cluster_cond->with_usage;
		assoc_cond.with_deleted = cluster_cond->with_deleted;
	}
	assoc_cond.cluster_list = list_create(NULL);

	while((row = mysql_fetch_row(result))) {
		MYSQL_RES *result2 = NULL;
		MYSQL_ROW row2;

		cluster = xmalloc(sizeof(acct_cluster_rec_t));
		list_append(cluster_list, cluster);

		cluster->name = xstrdup(row[CLUSTER_REQ_NAME]);

		list_append(assoc_cond.cluster_list, cluster->name);

		/* get the usage if requested */
		if(cluster_cond && cluster_cond->with_usage) {
			clusteracct_storage_p_get_usage(
				mysql_conn, uid, cluster,
				DBD_GET_CLUSTER_USAGE,
				cluster_cond->usage_start,
				cluster_cond->usage_end);
		}

		cluster->classification = atoi(row[CLUSTER_REQ_CLASS]);
		cluster->control_host = xstrdup(row[CLUSTER_REQ_CH]);
		cluster->control_port = atoi(row[CLUSTER_REQ_CP]);
		cluster->rpc_version = atoi(row[CLUSTER_REQ_VERSION]);
		query = xstrdup_printf(
			"select cpu_count, cluster_nodes from "
			"%s where cluster=\"%s\" "
			"and period_end=0 and node_name='' limit 1",
			event_table, cluster->name);
		debug4("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
		if(!(result2 = mysql_db_query_ret(
			     mysql_conn->db_conn, query, 0))) {
			xfree(query);
			continue;
		}
		xfree(query);
		if((row2 = mysql_fetch_row(result2))) {
			cluster->cpu_count = atoi(row2[0]);
			if(row2[1] && row2[1][0])
				cluster->nodes = xstrdup(row2[1]);
		}
		mysql_free_result(result2);
	}
	mysql_free_result(result);

	if(!list_count(assoc_cond.cluster_list)) {
		list_destroy(assoc_cond.cluster_list);
		return cluster_list;
	}

	assoc_cond.acct_list = list_create(NULL);
	list_append(assoc_cond.acct_list, "root");

	assoc_cond.user_list = list_create(NULL);
	list_append(assoc_cond.user_list, "");

	assoc_list = mysql_get_assocs(mysql_conn, uid, &assoc_cond);
	list_destroy(assoc_cond.cluster_list);
	list_destroy(assoc_cond.acct_list);
	list_destroy(assoc_cond.user_list);

	if(!assoc_list)
		return cluster_list;

	itr = list_iterator_create(cluster_list);
	assoc_itr = list_iterator_create(assoc_list);
	while((cluster = list_next(itr))) {
		while((assoc = list_next(assoc_itr))) {
			if(strcmp(assoc->cluster, cluster->name))
				continue;

			if(cluster->root_assoc) {
				debug("This cluster %s already has "
				      "an association.");
				continue;
			}
			cluster->root_assoc = assoc;
			list_remove(assoc_itr);
		}
		list_iterator_reset(assoc_itr);
	}
	list_iterator_destroy(itr);
	list_iterator_destroy(assoc_itr);
	if(list_count(assoc_list))
		error("I have %d left over associations",
		      list_count(assoc_list));
	list_destroy(assoc_list);

	return cluster_list;
}

extern List mysql_get_cluster_events(mysql_conn_t *mysql_conn, uint32_t uid,
				     acct_event_cond_t *event_cond)
{
	char *query = NULL;
	char *extra = NULL;
	char *tmp = NULL;
	List ret_list = NULL;
	ListIterator itr = NULL;
	char *object = NULL;
	int set = 0;
	int i=0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	time_t now = time(NULL);

	/* if this changes you will need to edit the corresponding enum */
	char *event_req_inx[] = {
		"node_name",
		"cluster",
		"cpu_count",
		"state",
		"period_start",
		"period_end",
		"reason",
		"cluster_nodes"
	};

	enum {
		EVENT_REQ_NODE,
		EVENT_REQ_CLUSTER,
		EVENT_REQ_CPU,
		EVENT_REQ_STATE,
		EVENT_REQ_START,
		EVENT_REQ_END,
		EVENT_REQ_REASON,
		EVENT_REQ_CNODES,
		EVENT_REQ_COUNT
	};

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	if(!event_cond)
		goto empty;

	if(event_cond->cluster_list
	   && list_count(event_cond->cluster_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(event_cond->cluster_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "cluster=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(event_cond->cpus_min) {
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");

		if(event_cond->cpus_max) {
			xstrfmtcat(extra, "cpu_count between %u and %u)",
				   event_cond->cpus_min, event_cond->cpus_max);

		} else {
			xstrfmtcat(extra, "cpu_count='%u')",
				   event_cond->cpus_min);

		}
	}

	switch(event_cond->event_type) {
	case ACCT_EVENT_ALL:
		break;
	case ACCT_EVENT_CLUSTER:
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		xstrcat(extra, "node_name = '')");

		break;
	case ACCT_EVENT_NODE:
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		xstrcat(extra, "node_name != '')");

		break;
	default:
		error("Unknown event %u doing all", event_cond->event_type);
		break;
	}

	if(event_cond->node_list
	   && list_count(event_cond->node_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(event_cond->node_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "node_name=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(event_cond->period_start) {
		if(!event_cond->period_end)
			event_cond->period_end = now;

		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");

		xstrfmtcat(query,
			   "(period_start < %d) "
			   "&& (period_end >= %d || period_end = 0))",
			   event_cond->period_end, event_cond->period_start);
	}

	if(event_cond->reason_list
	   && list_count(event_cond->reason_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(event_cond->reason_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "reason like \"%%%s%%\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(event_cond->state_list
	   && list_count(event_cond->state_list)) {
		set = 0;
		if(extra)
			xstrcat(extra, " && (");
		else
			xstrcat(extra, " where (");
		itr = list_iterator_create(event_cond->state_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "state=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}


empty:
	xfree(tmp);
	xstrfmtcat(tmp, "%s", event_req_inx[i]);
	for(i=1; i<EVENT_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", event_req_inx[i]);
	}

	query = xstrdup_printf("select %s from %s", tmp, event_table);
	xfree(tmp);
	if(extra) {
		xstrfmtcat(query, " %s", extra);
		xfree(extra);
	}

	ret_list = list_create(destroy_acct_event_rec);
	while((row = mysql_fetch_row(result))) {
		acct_event_rec_t *event = xmalloc(sizeof(acct_event_rec_t));

		list_append(ret_list, event);

		if(row[EVENT_REQ_NODE] && row[EVENT_REQ_NODE][0])
			event->node_name = xstrdup(row[EVENT_REQ_NODE]);

		if(row[EVENT_REQ_CLUSTER] && row[EVENT_REQ_CLUSTER][0])
			event->cluster = xstrdup(row[EVENT_REQ_CLUSTER]);

		event->cpu_count = atoi(row[EVENT_REQ_CPU]);
		event->state = atoi(row[EVENT_REQ_STATE]);
		event->period_start = atoi(row[EVENT_REQ_START]);
		event->period_end = atoi(row[EVENT_REQ_END]);

		if(row[EVENT_REQ_REASON] && row[EVENT_REQ_REASON][0])
			event->reason = xstrdup(row[EVENT_REQ_REASON]);

		if(row[EVENT_REQ_CLUSTER] && row[EVENT_REQ_CLUSTER][0])
			event->cluster_nodes = xstrdup(row[EVENT_REQ_CNODES]);
	}
	mysql_free_result(result);

	return ret_list;
}

extern int mysql_node_down(mysql_conn_t *mysql_conn, char *cluster,
			   struct node_record *node_ptr,
			   time_t event_time, char *reason,
			   uint32_t reason_uid)
{
	uint16_t cpus;
	int rc = SLURM_SUCCESS;
	char *query = NULL;
	char *my_reason;

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	if(!node_ptr) {
		error("No node_ptr given!");
		return SLURM_ERROR;
	}

	if (slurmctld_conf.fast_schedule && !slurmdbd_conf)
		cpus = node_ptr->config_ptr->cpus;
	else
		cpus = node_ptr->cpus;

	if (reason)
		my_reason = reason;
	else
		my_reason = node_ptr->reason;

	debug2("inserting %s(%s) with %u cpus", node_ptr->name, cluster, cpus);

	query = xstrdup_printf(
		"update %s set period_end=%d where cluster=\"%s\" "
		"and period_end=0 and node_name=\"%s\";",
		event_table, event_time, cluster, node_ptr->name);
	/* If you are clean-restarting the controller over and over again you
	 * could get records that are duplicates in the database.  If
	 * this is the case we will zero out the period_end we are
	 * just filled in.  This will cause the last time to be erased
	 * from the last restart, but if you are restarting things
	 * this often the pervious one didn't mean anything anyway.
	 * This way we only get one for the last time we let it run.
	 */
	xstrfmtcat(query,
		   "insert into %s "
		   "(node_name, state, cluster, cpu_count, "
		   "period_start, reason) "
		   "values (\"%s\", %u, \"%s\", %u, %d, \"%s\", %u) "
		   "on duplicate key "
		   "update period_end=0;",
		   event_table, node_ptr->name, node_ptr->node_state, cluster,
		   cpus, event_time, my_reason, reason_uid);
	debug4("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
	rc = mysql_db_query(mysql_conn->db_conn, query);
	xfree(query);

	return rc;
}

extern int mysql_node_up(mysql_conn_t *mysql_conn, char *cluster,
			 struct node_record *node_ptr,
			 time_t event_time)
{
	char* query;
	int rc = SLURM_SUCCESS;

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	query = xstrdup_printf(
		"update %s set period_end=%d where cluster=\"%s\" "
		"and period_end=0 and node_name=\"%s\";",
		event_table, event_time, cluster, node_ptr->name);
	debug4("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
	rc = mysql_db_query(mysql_conn->db_conn, query);
	xfree(query);
	return rc;
}

extern int mysql_register_ctld(mysql_conn_t *mysql_conn,
			       char *cluster, uint16_t port)
{
	char *query = NULL;
	char *address = NULL;
	char hostname[255];
	time_t now = time(NULL);

	if(slurmdbd_conf)
		fatal("clusteracct_storage_g_register_ctld "
		      "should never be called from the slurmdbd.");

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	info("Registering slurmctld for cluster %s at port %u in database.",
	     cluster, port);
	gethostname(hostname, sizeof(hostname));

	/* check if we are running on the backup controller */
	if(slurmctld_conf.backup_controller
	   && !strcmp(slurmctld_conf.backup_controller, hostname)) {
		address = slurmctld_conf.backup_addr;
	} else
		address = slurmctld_conf.control_addr;

	query = xstrdup_printf(
		"update %s set deleted=0, mod_time=%d, "
		"control_host='%s', control_port=%u, rpc_version=%d "
		"where name='%s';",
		cluster_table, now, address, port,
		SLURMDBD_VERSION,
		cluster);
	xstrfmtcat(query,
		   "insert into %s "
		   "(timestamp, action, name, actor, info) "
		   "values (%d, %d, \"%s\", \"%s\", \"%s %u\");",
		   txn_table,
		   now, DBD_MODIFY_CLUSTERS, cluster,
		   slurmctld_conf.slurm_user_name, address, port);

	debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);

	return mysql_db_query(mysql_conn->db_conn, query);
}

extern int mysql_cluster_cpus(mysql_conn_t *mysql_conn, char *cluster,
			      char *cluster_nodes, uint32_t cpus,
			      time_t event_time)
{
	char* query;
	int rc = SLURM_SUCCESS;
	int first = 0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;

 	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	/* Record the processor count */
	query = xstrdup_printf(
		"select cpu_count, cluster_nodes from %s where cluster=\"%s\" "
		"and period_end=0 and node_name='' limit 1",
		event_table, cluster);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	/* we only are checking the first one here */
	if(!(row = mysql_fetch_row(result))) {
		debug("We don't have an entry for this machine %s "
		      "most likely a first time running.", cluster);

		/* Get all nodes in a down state and jobs pending or running.
		 * This is for the first time a cluster registers
		 *
		 * We will return ACCOUNTING_FIRST_REG so this
		 * is taken care of since the message thread
		 * may not be up when we run this in the controller or
		 * in the slurmdbd.
		 */
		first = 1;
		goto add_it;
	}

	if(atoi(row[0]) == cpus) {
		debug3("we have the same cpu count as before for %s, "
		       "no need to update the database.", cluster);
		if(cluster_nodes) {
			if(!row[1][0]) {
				debug("Adding cluster nodes '%s' to "
				      "last instance of cluster '%s'.",
				      cluster_nodes, cluster);
				query = xstrdup_printf(
					"update %s set cluster_nodes=\"%s\" "
					"where cluster=\"%s\" "
					"and period_end=0 and node_name=''",
					event_table, cluster_nodes, cluster);
				rc = mysql_db_query(mysql_conn->db_conn, query);
				xfree(query);
				goto end_it;
			} else if(!strcmp(cluster_nodes, row[1])) {
				debug3("we have the same nodes in the cluster "
				       "as before no need to "
				       "update the database.");
				goto end_it;
			}
		} else
			goto end_it;
	} else
		debug("%s has changed from %s cpus to %u",
		      cluster, row[0], cpus);

	/* reset all the entries for this cluster since the cpus
	   changed some of the downed nodes may have gone away.
	   Request them again with ACCOUNTING_FIRST_REG */
	query = xstrdup_printf(
		"update %s set period_end=%d where cluster=\"%s\" "
		"and period_end=0",
		event_table, event_time, cluster);
	rc = mysql_db_query(mysql_conn->db_conn, query);
	xfree(query);
	first = 1;
	if(rc != SLURM_SUCCESS)
		goto end_it;
add_it:
	query = xstrdup_printf(
		"insert into %s (cluster, cluster_nodes, cpu_count, "
		"period_start, reason) "
		"values (\"%s\", \"%s\", %u, %d, 'Cluster processor count')",
		event_table, cluster, cluster_nodes, cpus, event_time);
	rc = mysql_db_query(mysql_conn->db_conn, query);
	xfree(query);
end_it:
	mysql_free_result(result);
	if(first && rc == SLURM_SUCCESS)
		rc = ACCOUNTING_FIRST_REG;

	return rc;
}
