/*****************************************************************************\
 *  mysql_convert.c - functions dealing with converting from tables in
 *                    slurm <= 2.1.
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

#include "mysql_convert.h"

extern int mysql_convert_tables(MYSQL *db_conn)
{
	storage_field_t event_table_fields_2_1[] = {
		{ "node_name", "tinytext default '' not null" },
		{ "cluster", "tinytext not null" },
		{ "cpu_count", "int not null" },
		{ "state", "smallint unsigned default 0 not null" },
		{ "period_start", "int unsigned not null" },
		{ "period_end", "int unsigned default 0 not null" },
		{ "reason", "tinytext not null" },
		{ "reason_uid", "int unsigned default 0xfffffffe not null" },
		{ "cluster_nodes", "text not null default ''" },
		{ NULL, NULL}
	};

	storage_field_t job_table_fields_2_1[] = {
		{ "id", "int not null auto_increment" },
		{ "deleted", "tinyint default 0" },
		{ "jobid", "int unsigned not null" },
		{ "associd", "int unsigned not null" },
		{ "wckey", "tinytext not null default ''" },
		{ "wckeyid", "int unsigned not null" },
		{ "uid", "int unsigned not null" },
		{ "gid", "int unsigned not null" },
		{ "cluster", "tinytext not null" },
		{ "partition", "tinytext not null" },
		{ "blockid", "tinytext" },
		{ "account", "tinytext" },
		{ "eligible", "int unsigned default 0 not null" },
		{ "submit", "int unsigned default 0 not null" },
		{ "start", "int unsigned default 0 not null" },
		{ "end", "int unsigned default 0 not null" },
		{ "suspended", "int unsigned default 0 not null" },
		{ "timelimit", "int unsigned default 0 not null" },
		{ "name", "tinytext not null" },
		{ "track_steps", "tinyint not null" },
		{ "state", "smallint unsigned not null" },
		{ "comp_code", "int default 0 not null" },
		{ "priority", "int not null" },
		{ "req_cpus", "int unsigned not null" },
		{ "alloc_cpus", "int unsigned not null" },
		{ "alloc_nodes", "int unsigned not null" },
		{ "nodelist", "text" },
		{ "node_inx", "text" },
		{ "kill_requid", "int default -1 not null" },
		{ "qos", "smallint default 0" },
		{ "resvid", "int unsigned not null" },
		{ NULL, NULL}
	};

	storage_field_t resv_table_fields_2_1[] = {
		{ "id", "int unsigned default 0 not null" },
		{ "name", "text not null" },
		{ "cluster", "text not null" },
		{ "deleted", "tinyint default 0" },
		{ "cpus", "int unsigned not null" },
		{ "assoclist", "text not null default ''" },
		{ "nodelist", "text not null default ''" },
		{ "node_inx", "text not null default ''" },
		{ "start", "int unsigned default 0 not null"},
		{ "end", "int unsigned default 0 not null" },
		{ "flags", "smallint unsigned default 0 not null" },
		{ NULL, NULL}
	};

	storage_field_t step_table_fields_2_1[] = {
		{ "id", "int not null" },
		{ "deleted", "tinyint default 0" },
		{ "stepid", "smallint not null" },
		{ "start", "int unsigned default 0 not null" },
		{ "end", "int unsigned default 0 not null" },
		{ "suspended", "int unsigned default 0 not null" },
		{ "name", "text not null" },
		{ "nodelist", "text not null" },
		{ "node_inx", "text" },
		{ "state", "smallint unsigned not null" },
		{ "kill_requid", "int default -1 not null" },
		{ "comp_code", "int default 0 not null" },
		{ "nodes", "int unsigned not null" },
		{ "cpus", "int unsigned not null" },
		{ "tasks", "int unsigned not null" },
		{ "task_dist", "smallint default 0" },
		{ "user_sec", "int unsigned default 0 not null" },
		{ "user_usec", "int unsigned default 0 not null" },
		{ "sys_sec", "int unsigned default 0 not null" },
		{ "sys_usec", "int unsigned default 0 not null" },
		{ "max_vsize", "bigint unsigned default 0 not null" },
		{ "max_vsize_task", "smallint unsigned default 0 not null" },
		{ "max_vsize_node", "int unsigned default 0 not null" },
		{ "ave_vsize", "double unsigned default 0.0 not null" },
		{ "max_rss", "bigint unsigned default 0 not null" },
		{ "max_rss_task", "smallint unsigned default 0 not null" },
		{ "max_rss_node", "int unsigned default 0 not null" },
		{ "ave_rss", "double unsigned default 0.0 not null" },
		{ "max_pages", "int unsigned default 0 not null" },
		{ "max_pages_task", "smallint unsigned default 0 not null" },
		{ "max_pages_node", "int unsigned default 0 not null" },
		{ "ave_pages", "double unsigned default 0.0 not null" },
		{ "min_cpu", "int unsigned default 0 not null" },
		{ "min_cpu_task", "smallint unsigned default 0 not null" },
		{ "min_cpu_node", "int unsigned default 0 not null" },
		{ "ave_cpu", "double unsigned default 0.0 not null" },
		{ NULL, NULL}
	};

	storage_field_t suspend_table_fields_2_1[] = {
		{ "id", "int not null" },
		{ "associd", "int not null" },
		{ "start", "int unsigned default 0 not null" },
		{ "end", "int unsigned default 0 not null" },
		{ NULL, NULL}
	};

	ListIterator itr = NULL;
	char *cluster_name;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *query = xstrdup_printf("show tables like '%s';", event_table);
	int rc;

	debug3("(%s:%d) query\n%s", __FILE__, __LINE__, query);
	if(!(result = mysql_db_query_ret(db_conn, query, 0))) {
		xfree(query);
		goto end_it;
	}
	xfree(query);

	if((row = mysql_fetch_row(result))) {
		/* make it up to date */
		if(mysql_db_create_table(db_conn, event_table,
					 event_table_fields_2_1,
					 ", primary key (node_name(20), "
					 "cluster(20), period_start))")
		   == SLURM_ERROR)
			goto end_it;

		/* now convert to new form */
		slurm_mutex_lock(&mysql_cluster_list_lock);
		if(!itr)
			itr = list_iterator_create(mysql_cluster_list);
		else
			list_iterator_reset(itr);

		while((cluster_name = list_next(itr))) {
			query = xstrdup_printf(
				"insert into %s_%s (node_name, cpu_count, "
				"state, period_start, period_end, reason, "
				"reason_uid, cluster_nodes) "
				"select node_name, cpu_count, state, "
				"period_start, period_end, reason, "
				"reason_uid, cluster_nodes from %s where "
				"cluster='%s' on duplicate key update "
				"period_start=VALUES(period_start), "
				"period_end=VALUES(period_end);",
				cluster_name, event_table,
				event_table, cluster_name);
			debug("(%s:%d) query\n%s", __FILE__, __LINE__, query);
			rc = mysql_db_query(db_conn, query);
			xfree(query);
			if(rc != SLURM_SUCCESS) {
				error("Couldn't update event table correctly");
				goto end_it;
			}
		}
	}
	mysql_free_result(result);

	return SLURM_SUCCESS;

end_it:
	if(itr)
		list_iterator_destroy(itr);
	if(result)
		mysql_free_result(result);
	return SLURM_ERROR;
}

