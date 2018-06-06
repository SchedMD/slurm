/*****************************************************************************\
 *  as_mysql_usage.c - functions dealing with usage.
 *****************************************************************************
 *
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "as_mysql_cluster.h"
#include "as_mysql_usage.h"
#include "as_mysql_rollup.h"
#include "src/common/macros.h"
#include "src/common/slurm_time.h"

time_t global_last_rollup = 0;
pthread_mutex_t rollup_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t usage_rollup_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
	uint16_t archive_data;
	char *cluster_name;
	mysql_conn_t *mysql_conn;
	int *rc;
	int *rolledup;
	pthread_mutex_t *rolledup_lock;
	pthread_cond_t *rolledup_cond;
	rollup_stats_t *rollup_stats;
	time_t sent_end;
	time_t sent_start;
} local_rollup_t;

static void *_cluster_rollup_usage(void *arg)
{
	local_rollup_t *local_rollup = (local_rollup_t *)arg;
	int i, rc = SLURM_SUCCESS;
	char timer_str[128];
	mysql_conn_t mysql_conn;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *query = NULL;
	struct tm start_tm;
	struct tm end_tm;
	time_t my_time = local_rollup->sent_end;
	time_t last_hour = local_rollup->sent_start;
	time_t last_day = local_rollup->sent_start;
	time_t last_month = local_rollup->sent_start;
	time_t hour_start;
	time_t hour_end;
	time_t day_start;
	time_t day_end;
	time_t month_start;
	time_t month_end;
	long rollup_time[ROLLUP_COUNT];
	DEF_TIMERS;

	char *update_req_inx[] = {
		"hourly_rollup",
		"daily_rollup",
		"monthly_rollup"
	};

	memset(&mysql_conn, 0, sizeof(mysql_conn_t));
	memset(rollup_time, 0, sizeof(long) * ROLLUP_COUNT);
	mysql_conn.rollback = 1;
	mysql_conn.conn = local_rollup->mysql_conn->conn;
	slurm_mutex_init(&mysql_conn.lock);

	/* Each thread needs it's own connection we can't use the one
	 * sent from the parent thread. */
	rc = check_connection(&mysql_conn);

	if (rc != SLURM_SUCCESS)
		goto end_it;

	if (!local_rollup->sent_start) {
		char *tmp = NULL, *sep = "";
		for (i = 0; i < ROLLUP_COUNT; i++) {
			xstrfmtcat(tmp, "%s%s", sep, update_req_inx[i]);
			sep = ", ";
		}
		query = xstrdup_printf("select %s from \"%s_%s\"",
				       tmp, local_rollup->cluster_name,
				       last_ran_table);
		xfree(tmp);

		debug4("%d(%s:%d) query\n%s", mysql_conn.conn,
		       THIS_FILE, __LINE__, query);
		if (!(result = mysql_db_query_ret(&mysql_conn, query, 0))) {
			xfree(query);
			rc = SLURM_ERROR;
			goto end_it;
		}

		xfree(query);
		row = mysql_fetch_row(result);
		if (row) {
			last_hour = slurm_atoul(row[ROLLUP_HOUR]);
			last_day = slurm_atoul(row[ROLLUP_DAY]);
			last_month = slurm_atoul(row[ROLLUP_MONTH]);
			mysql_free_result(result);
		} else {
			time_t now = time(NULL);
			time_t lowest = now;

			mysql_free_result(result);

			query = xstrdup_printf(
				"select time_start from \"%s_%s\" "
				"where node_name='' order by "
				"time_start asc limit 1;",
				local_rollup->cluster_name, event_table);
			if (debug_flags & DEBUG_FLAG_DB_USAGE)
				DB_DEBUG(mysql_conn.conn, "query\n%s", query);
			if (!(result = mysql_db_query_ret(
				      &mysql_conn, query, 0))) {
				xfree(query);
				rc = SLURM_ERROR;
				goto end_it;
			}
			xfree(query);
			if ((row = mysql_fetch_row(result))) {
				time_t check = slurm_atoul(row[0]);
				if (check < lowest)
					lowest = check;
			}
			mysql_free_result(result);

			/* If we don't have any events like adding a
			 * cluster this will not work correctly, so we
			 * will insert now as a starting point.
			 */

			query = xstrdup_printf(
				"insert into \"%s_%s\" "
				"(hourly_rollup, daily_rollup, monthly_rollup) "
				"values (%ld, %ld, %ld);",
				local_rollup->cluster_name, last_ran_table,
				lowest, lowest, lowest);

			if (debug_flags & DEBUG_FLAG_DB_USAGE)
				DB_DEBUG(mysql_conn.conn, "query\n%s", query);
			rc = mysql_db_query(&mysql_conn, query);
			xfree(query);
			if (rc != SLURM_SUCCESS) {
				rc = SLURM_ERROR;
				goto end_it;
			}

			if (lowest == now) {
				debug("Cluster %s not registered, "
				      "not doing rollup",
				      local_rollup->cluster_name);
				rc = SLURM_SUCCESS;
				goto end_it;
			}

			last_hour = last_day = last_month = lowest;
		}
	}

	if (!my_time)
		my_time = time(NULL);

	/* test month gap */
/* 	last_hour = 1212299999; */
/* 	last_day = 1212217200; */
/* 	last_month = 1212217200; */
/* 	my_time = 1212307200; */

/* 	last_hour = 1211475599; */
/* 	last_day = 1211475599; */
/* 	last_month = 1211475599; */

//	last_hour = 1211403599;
	//	last_hour = 1206946800;
//	last_day = 1207033199;
//	last_day = 1197033199;
//	last_month = 1204358399;

	if (!slurm_localtime_r(&last_hour, &start_tm)) {
		error("Couldn't get localtime from hour start %ld", last_hour);
		rc = SLURM_ERROR;
		goto end_it;
	}

	if (!slurm_localtime_r(&my_time, &end_tm)) {
		error("Couldn't get localtime from hour end %ld", my_time);
		rc = SLURM_ERROR;
		goto end_it;
	}

	/* Below and anywhere in a rollup plugin when dealing with
	 * epoch times we need to set the tm_isdst = -1 so we don't
	 * have to worry about the time changes.  Not setting it to -1
	 * will cause problems in the day and month with the date change.
	 *
	 * NOTE: slurm_mktime() implementation already sets it to -1 so
	 *	 there's no need to manually set it beforehand.
	 */

	start_tm.tm_sec = 0;
	start_tm.tm_min = 0;
	hour_start = slurm_mktime(&start_tm);

	end_tm.tm_sec = 0;
	end_tm.tm_min = 0;
	hour_end = slurm_mktime(&end_tm);

/* 	info("hour start %s", slurm_ctime2(&hour_start)); */
/* 	info("hour end %s", slurm_ctime2(&hour_end)); */
/* 	info("diff is %d", hour_end-hour_start); */

	slurm_mutex_lock(&rollup_lock);
	global_last_rollup = hour_end;
	slurm_mutex_unlock(&rollup_lock);

	/* set up the day period */
	if (!slurm_localtime_r(&last_day, &start_tm)) {
		error("Couldn't get localtime from day %ld", last_day);
		rc = SLURM_ERROR;
		goto end_it;
	}

	start_tm.tm_sec = 0;
	start_tm.tm_min = 0;
	start_tm.tm_hour = 0;
	day_start = slurm_mktime(&start_tm);

	end_tm.tm_hour = 0;
	day_end = slurm_mktime(&end_tm);

/* 	info("day start %s", slurm_ctime2(&day_start)); */
/* 	info("day end %s", slurm_ctime2(&day_end)); */
/* 	info("diff is %d", day_end-day_start); */

	/* set up the month period */
	if (!slurm_localtime_r(&last_month, &start_tm)) {
		error("Couldn't get localtime from month %ld", last_month);
		rc = SLURM_ERROR;
		goto end_it;
	}

	start_tm.tm_sec = 0;
	start_tm.tm_min = 0;
	start_tm.tm_hour = 0;
	start_tm.tm_mday = 1;
	month_start = slurm_mktime(&start_tm);

	end_tm.tm_sec = 0;
	end_tm.tm_min = 0;
	end_tm.tm_hour = 0;
	end_tm.tm_mday = 1;
	month_end = slurm_mktime(&end_tm);

/* 	info("month start %s", slurm_ctime2(&month_start)); */
/* 	info("month end %s", slurm_ctime2(&month_end)); */
/* 	info("diff is %d", month_end-month_start); */

	if ((hour_end - hour_start) > 0) {
		START_TIMER;
		rc = as_mysql_hourly_rollup(&mysql_conn,
					    local_rollup->cluster_name,
					    hour_start,
					    hour_end,
					    local_rollup->archive_data);
		snprintf(timer_str, sizeof(timer_str),
			 "hourly_rollup for %s", local_rollup->cluster_name);
		END_TIMER3(timer_str, 5000000);
		rollup_time[ROLLUP_HOUR] += DELTA_TIMER;
		if (rc != SLURM_SUCCESS)
			goto end_it;
	}

	if ((day_end - day_start) > 0) {
		START_TIMER;
		rc = as_mysql_nonhour_rollup(&mysql_conn, 0,
					     local_rollup->cluster_name,
					     day_start,
					     day_end,
					     local_rollup->archive_data);
		snprintf(timer_str, sizeof(timer_str),
			 "daily_rollup for %s", local_rollup->cluster_name);
		END_TIMER3(timer_str, 5000000);
		rollup_time[ROLLUP_DAY] += DELTA_TIMER;
		if (rc != SLURM_SUCCESS)
			goto end_it;
	}

	if ((month_end - month_start) > 0) {
		START_TIMER;
		rc = as_mysql_nonhour_rollup(&mysql_conn, 1,
					     local_rollup->cluster_name,
					     month_start,
					     month_end,
					     local_rollup->archive_data);
		snprintf(timer_str, sizeof(timer_str),
			 "monthly_rollup for %s", local_rollup->cluster_name);
		END_TIMER3(timer_str, 5000000);
		rollup_time[ROLLUP_MONTH] += DELTA_TIMER;
		if (rc != SLURM_SUCCESS)
			goto end_it;
	}

	if ((hour_end - hour_start) > 0) {
		/* If we have a sent_end do not update the last_run_table */
		if (!local_rollup->sent_end)
			query = xstrdup_printf(
				"update \"%s_%s\" set hourly_rollup=%ld",
				local_rollup->cluster_name,
				last_ran_table, hour_end);
	} else
		debug2("No need to roll cluster %s this hour %ld <= %ld",
		       local_rollup->cluster_name, hour_end, hour_start);

	if ((day_end - day_start) > 0) {
		if (query && !local_rollup->sent_end)
			xstrfmtcat(query, ", daily_rollup=%ld", day_end);
		else if (!local_rollup->sent_end)
			query = xstrdup_printf(
				"update \"%s_%s\" set daily_rollup=%ld",
				local_rollup->cluster_name,
				last_ran_table, day_end);
	} else
		debug2("No need to roll cluster %s this day %ld <= %ld",
		       local_rollup->cluster_name, day_end, day_start);

	if ((month_end - month_start) > 0) {
		if (query && !local_rollup->sent_end)
			xstrfmtcat(query, ", monthly_rollup=%ld", month_end);
		else if (!local_rollup->sent_end)
			query = xstrdup_printf(
				"update \"%s_%s\" set monthly_rollup=%ld",
				local_rollup->cluster_name,
				last_ran_table, month_end);
	} else
		debug2("No need to roll cluster %s this month %ld <= %ld",
		       local_rollup->cluster_name, month_end, month_start);

	if (query) {
		if (debug_flags & DEBUG_FLAG_DB_USAGE)
			DB_DEBUG(mysql_conn.conn, "query\n%s", query);
		rc = mysql_db_query(&mysql_conn, query);
		xfree(query);
	}
end_it:
	if (rc == SLURM_SUCCESS) {
		if (mysql_db_commit(&mysql_conn)) {
			error("Couldn't commit rollup of cluster %s",
			      local_rollup->cluster_name);
			rc = SLURM_ERROR;
		}
	} else {
		error("Cluster %s rollup failed", local_rollup->cluster_name);
		if (mysql_db_rollback(&mysql_conn))
			error("rollback failed");
	}

	mysql_db_close_db_connection(&mysql_conn);
	slurm_mutex_destroy(&mysql_conn.lock);

	slurm_mutex_lock(local_rollup->rolledup_lock);
	(*local_rollup->rolledup)++;
	if (local_rollup->rollup_stats) {
		for (i = 0; i < ROLLUP_COUNT; i++) {
			local_rollup->rollup_stats->rollup_time[i] +=
				rollup_time[i];
		}
	}
	if ((rc != SLURM_SUCCESS) && ((*local_rollup->rc) == SLURM_SUCCESS))
		(*local_rollup->rc) = rc;
	slurm_cond_signal(local_rollup->rolledup_cond);
	slurm_mutex_unlock(local_rollup->rolledup_lock);
	xfree(local_rollup);

	return NULL;
}

/* assoc_mgr locks need to be unlocked before coming here */
static int _get_object_usage(mysql_conn_t *mysql_conn,
			     slurmdbd_msg_type_t type, char *my_usage_table,
			     char *cluster_name, char *id_str,
			     time_t start, time_t end, List *usage_list)
{
	char *tmp = NULL;
	int i = 0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *query = NULL;
	assoc_mgr_lock_t locks = { NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK,
				   READ_LOCK, NO_LOCK, NO_LOCK };

	char *usage_req_inx[] = {
		"t3.id_assoc",
		"t1.id_tres",
		"t1.time_start",
		"t1.alloc_secs",
	};
	enum {
		USAGE_ID,
		USAGE_TRES,
		USAGE_START,
		USAGE_ALLOC,
		USAGE_COUNT
	};

	if (type == DBD_GET_WCKEY_USAGE)
		usage_req_inx[0] = "t1.id";

	xstrfmtcat(tmp, "%s", usage_req_inx[i]);
	for (i=1; i<USAGE_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", usage_req_inx[i]);
	}

	switch (type) {
	case DBD_GET_ASSOC_USAGE:
		query = xstrdup_printf(
			"select %s from \"%s_%s\" as t1, "
			"\"%s_%s\" as t2, \"%s_%s\" as t3 "
			"where (t1.time_start < %ld && t1.time_start >= %ld) "
			"&& t1.id=t2.id_assoc && (%s) && "
			"t2.lft between t3.lft and t3.rgt "
			"order by t3.id_assoc, time_start;",
			tmp, cluster_name, my_usage_table,
			cluster_name, assoc_table, cluster_name, assoc_table,
			end, start, id_str);
		break;
	case DBD_GET_WCKEY_USAGE:
		query = xstrdup_printf(
			"select %s from \"%s_%s\" as t1 "
			"where (time_start < %ld && time_start >= %ld) "
			"&& (%s) order by id, time_start;",
			tmp, cluster_name, my_usage_table, end, start, id_str);
		break;
	default:
		error("Unknown usage type %d", type);
		xfree(tmp);
		return SLURM_ERROR;
		break;
	}
	xfree(tmp);

	if (debug_flags & DEBUG_FLAG_DB_USAGE)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	result = mysql_db_query_ret(mysql_conn, query, 0);
	xfree(query);

	if (!result)
		return SLURM_ERROR;

	if (!(*usage_list))
		(*usage_list) = list_create(slurmdb_destroy_accounting_rec);

	assoc_mgr_lock(&locks);
	while ((row = mysql_fetch_row(result))) {
		slurmdb_tres_rec_t *tres_rec;
		slurmdb_accounting_rec_t *accounting_rec =
			xmalloc(sizeof(slurmdb_accounting_rec_t));

		accounting_rec->tres_rec.id = slurm_atoul(row[USAGE_TRES]);
		if ((tres_rec = list_find_first(
			     assoc_mgr_tres_list, slurmdb_find_tres_in_list,
			     &accounting_rec->tres_rec.id))) {
			accounting_rec->tres_rec.name =
				xstrdup(tres_rec->name);
			accounting_rec->tres_rec.type =
				xstrdup(tres_rec->type);
		}

		accounting_rec->id = slurm_atoul(row[USAGE_ID]);
		accounting_rec->period_start = slurm_atoul(row[USAGE_START]);
		accounting_rec->alloc_secs = slurm_atoull(row[USAGE_ALLOC]);

		list_append(*usage_list, accounting_rec);
	}
	assoc_mgr_unlock(&locks);

	mysql_free_result(result);

	return SLURM_SUCCESS;
}

/* assoc_mgr locks need to unlocked before you get here */
static int _get_cluster_usage(mysql_conn_t *mysql_conn, uid_t uid,
			      slurmdb_cluster_rec_t *cluster_rec,
			      slurmdbd_msg_type_t type,
			      time_t start, time_t end)
{
	int rc = SLURM_SUCCESS;
	int i=0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *tmp = NULL;
	char *my_usage_table = cluster_day_table;
	char *query = NULL;
	assoc_mgr_lock_t locks = { NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK,
				   READ_LOCK, NO_LOCK, NO_LOCK };
	char *cluster_req_inx[] = {
		"id_tres",
		"alloc_secs",
		"down_secs",
		"pdown_secs",
		"idle_secs",
		"resv_secs",
		"over_secs",
		"count",
		"time_start",
	};

	enum {
		CLUSTER_TRES,
		CLUSTER_ACPU,
		CLUSTER_DCPU,
		CLUSTER_PDCPU,
		CLUSTER_ICPU,
		CLUSTER_RCPU,
		CLUSTER_OCPU,
		CLUSTER_CNT,
		CLUSTER_START,
		CLUSTER_COUNT
	};

	if (!cluster_rec->name || !cluster_rec->name[0]) {
		error("We need a cluster name to set data for");
		return SLURM_ERROR;
	}

	if (set_usage_information(&my_usage_table, type, &start, &end)
	    != SLURM_SUCCESS) {
		return SLURM_ERROR;
	}

	xfree(tmp);
	i=0;
	xstrfmtcat(tmp, "%s", cluster_req_inx[i]);
	for(i=1; i<CLUSTER_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", cluster_req_inx[i]);
	}

	query = xstrdup_printf(
		"select %s from \"%s_%s\" where (time_start < %ld "
		"&& time_start >= %ld)",
		tmp, cluster_rec->name, my_usage_table, end, start);

	xfree(tmp);
	if (debug_flags & DEBUG_FLAG_DB_USAGE)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);

	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	if (!cluster_rec->accounting_list)
		cluster_rec->accounting_list =
			list_create(slurmdb_destroy_cluster_accounting_rec);

	assoc_mgr_lock(&locks);
	while ((row = mysql_fetch_row(result))) {
		slurmdb_tres_rec_t *tres_rec;
		slurmdb_cluster_accounting_rec_t *accounting_rec =
			xmalloc(sizeof(slurmdb_cluster_accounting_rec_t));

		accounting_rec->tres_rec.id = slurm_atoul(row[CLUSTER_TRES]);
		accounting_rec->tres_rec.count = slurm_atoul(row[CLUSTER_CNT]);
		if ((tres_rec = list_find_first(
			     assoc_mgr_tres_list, slurmdb_find_tres_in_list,
			     &accounting_rec->tres_rec.id))) {
			accounting_rec->tres_rec.name =
				xstrdup(tres_rec->name);
			accounting_rec->tres_rec.type =
				xstrdup(tres_rec->type);
		}

		accounting_rec->alloc_secs = slurm_atoull(row[CLUSTER_ACPU]);
		accounting_rec->down_secs = slurm_atoull(row[CLUSTER_DCPU]);
		accounting_rec->pdown_secs = slurm_atoull(row[CLUSTER_PDCPU]);
		accounting_rec->idle_secs = slurm_atoull(row[CLUSTER_ICPU]);
		accounting_rec->over_secs = slurm_atoull(row[CLUSTER_OCPU]);
		accounting_rec->resv_secs = slurm_atoull(row[CLUSTER_RCPU]);
		accounting_rec->period_start = slurm_atoul(row[CLUSTER_START]);
		list_append(cluster_rec->accounting_list, accounting_rec);
	}
	assoc_mgr_unlock(&locks);

	mysql_free_result(result);
	return rc;
}



/* checks should already be done before this to see if this is a valid
   user or not.  The assoc_mgr locks should be unlocked before coming here.
*/
extern int get_usage_for_list(mysql_conn_t *mysql_conn,
			      slurmdbd_msg_type_t type, List object_list,
			      char *cluster_name, time_t start, time_t end)
{
	int rc = SLURM_SUCCESS;
	char *my_usage_table = NULL;
	List usage_list = NULL;
	char *id_str = NULL, *name_char = NULL;
	ListIterator itr = NULL, u_itr = NULL;
	void *object = NULL;
	slurmdb_assoc_rec_t *assoc = NULL;
	slurmdb_wckey_rec_t *wckey = NULL;
	slurmdb_accounting_rec_t *accounting_rec = NULL;
	hostlist_t hl = NULL;
	char id[100];

	if (!object_list) {
		error("We need an object to set data for getting usage");
		return SLURM_ERROR;
	}

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	/* Previously this would just tack id's onto a long list.  It turns out
	 * that isn't very efficient.  This attempts to combine id's into a
	 * hostlist and then query id sets instead of against each id
	 * separately.  This has proven to be much more efficient.
	 */
	switch (type) {
	case DBD_GET_ASSOC_USAGE:
		name_char = "t3.id_assoc";
		itr = list_iterator_create(object_list);
		while ((assoc = list_next(itr))) {
			snprintf(id, sizeof(id), "%u", assoc->id);

			if (hl)
				hostlist_push_host_dims(hl, id, 1);
			else
				hl = hostlist_create_dims(id, 1);
		}
		list_iterator_destroy(itr);
		my_usage_table = assoc_day_table;
		break;
	case DBD_GET_WCKEY_USAGE:
		name_char = "id";
		itr = list_iterator_create(object_list);
		while ((wckey = list_next(itr))) {
			snprintf(id, sizeof(id), "%u", wckey->id);

			if (hl)
				hostlist_push_host_dims(hl, id, 1);
			else
				hl = hostlist_create_dims(id, 1);
		}
		list_iterator_destroy(itr);

		my_usage_table = wckey_day_table;
		break;
	default:
		error("Unknown usage type %d", type);
		return SLURM_ERROR;
		break;
	}

	if (hl) {
		unsigned long lo, hi;

		xfree(id_str);

		hostlist_sort(hl);
		while (hostlist_pop_range_values(hl, &lo, &hi)) {
			if (id_str)
				xstrcat(id_str, " || ");
			if (lo >= hi)
				xstrfmtcat(id_str, "%s=%lu", name_char, lo);
			else
				xstrfmtcat(id_str, "%s between %lu and %lu",
					   name_char, lo, hi);
		}
		hostlist_destroy(hl);
	}

	if (set_usage_information(&my_usage_table, type, &start, &end)
	    != SLURM_SUCCESS) {
		xfree(id_str);
		return SLURM_ERROR;
	}

	if (_get_object_usage(mysql_conn, type, my_usage_table, cluster_name,
			      id_str, start, end, &usage_list)
	    != SLURM_SUCCESS) {
		xfree(id_str);
		return SLURM_ERROR;
	}

	xfree(id_str);

	if (!usage_list) {
		error("No usage given back?  This should never happen");
		return SLURM_ERROR;
	}

	u_itr = list_iterator_create(usage_list);
	itr = list_iterator_create(object_list);
	while ((object = list_next(itr))) {
		int found = 0;
		int id = 0;
		List acct_list = NULL;

		switch (type) {
		case DBD_GET_ASSOC_USAGE:
			assoc = (slurmdb_assoc_rec_t *)object;
			if (!assoc->accounting_list)
				assoc->accounting_list = list_create(
					slurmdb_destroy_accounting_rec);
			acct_list = assoc->accounting_list;
			id = assoc->id;
			break;
		case DBD_GET_WCKEY_USAGE:
			wckey = (slurmdb_wckey_rec_t *)object;
			if (!wckey->accounting_list)
				wckey->accounting_list = list_create(
					slurmdb_destroy_accounting_rec);
			acct_list = wckey->accounting_list;
			id = wckey->id;
			break;
		default:
			continue;
			break;
		}

		while ((accounting_rec = list_next(u_itr))) {
			if (id == accounting_rec->id) {
				list_append(acct_list, accounting_rec);
				list_remove(u_itr);
				found = 1;
			} else if (found) {
				/* here we know the
				   list is in id order so
				   if the next record
				   isn't the correct id
				   just continue since
				   there is no reason to
				   go through the rest of
				   the list when we know
				   it isn't going to be
				   the correct id */
				break;
			}
		}
		list_iterator_reset(u_itr);
	}
	list_iterator_destroy(itr);
	list_iterator_destroy(u_itr);

	if (list_count(usage_list))
		error("we have %d records not added "
		      "to the association list",
		      list_count(usage_list));
	FREE_NULL_LIST(usage_list);

	return rc;
}

/*   The assoc_mgr locks should be unlocked before coming here. */
extern int as_mysql_get_usage(mysql_conn_t *mysql_conn, uid_t uid,
			      void *in, slurmdbd_msg_type_t type,
			      time_t start, time_t end)
{
	int rc = SLURM_SUCCESS;
	int is_admin=1;
	char *my_usage_table = NULL;
	slurmdb_assoc_rec_t *slurmdb_assoc = in;
	slurmdb_wckey_rec_t *slurmdb_wckey = in;
	char *username = NULL;
	uint16_t private_data = 0;
	List *my_list = NULL;
	char *cluster_name = NULL;
	char *id_str = NULL;

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	switch (type) {
	case DBD_GET_ASSOC_USAGE:
		if (!slurmdb_assoc->id) {
			error("We need an id to set data for getting usage");
			return SLURM_ERROR;
		}
		id_str = xstrdup_printf("t3.id_assoc=%u", slurmdb_assoc->id);
		cluster_name = slurmdb_assoc->cluster;
		username = slurmdb_assoc->user;
		my_list = &slurmdb_assoc->accounting_list;
		my_usage_table = assoc_day_table;
		break;
	case DBD_GET_WCKEY_USAGE:
		if (!slurmdb_wckey->id) {
			error("We need an id to set data for getting usage");
			return SLURM_ERROR;
		}
		id_str = xstrdup_printf("id=%d", slurmdb_wckey->id);
		cluster_name = slurmdb_wckey->cluster;
		username = slurmdb_wckey->user;
		my_list = &slurmdb_wckey->accounting_list;
		my_usage_table = wckey_day_table;
		break;
	case DBD_GET_CLUSTER_USAGE:
		rc = _get_cluster_usage(mysql_conn, uid, in,
					type, start, end);
		return rc;
		break;
	default:
		error("Unknown usage type %d", type);
		return SLURM_ERROR;
		break;
	}

	if (!cluster_name) {
		error("We need a cluster_name to set data for getting usage");
		xfree(id_str);
		return SLURM_ERROR;
	}

	private_data = slurm_get_private_data();
	if (private_data & PRIVATE_DATA_USAGE) {
		if (!(is_admin = is_user_min_admin_level(
			      mysql_conn, uid, SLURMDB_ADMIN_OPERATOR))) {
			ListIterator itr = NULL;
			slurmdb_coord_rec_t *coord = NULL;
			slurmdb_user_rec_t user;
			bool is_coord;

			memset(&user, 0, sizeof(slurmdb_user_rec_t));
			user.uid = uid;
			is_coord = is_user_any_coord(mysql_conn, &user);

			if (username && !xstrcmp(username, user.name))
				goto is_user;

			if (type != DBD_GET_ASSOC_USAGE)
				goto bad_user;

			if (!slurmdb_assoc->acct) {
				debug("No account name given "
				      "in association.");
				goto bad_user;
			}

			if (!is_coord) {
				debug4("This user is not a coordinator.");
				goto bad_user;
			}

			/* Existance of user.coord_accts is checked in
			   is_user_any_coord.
			*/
			itr = list_iterator_create(user.coord_accts);
			while ((coord = list_next(itr)))
				if (!xstrcasecmp(coord->name,
						 slurmdb_assoc->acct))
					break;
			list_iterator_destroy(itr);

			if (coord)
				goto is_user;

		bad_user:
			errno = ESLURM_ACCESS_DENIED;
			xfree(id_str);
			return SLURM_ERROR;
		}
	}
is_user:

	if (set_usage_information(&my_usage_table, type, &start, &end)
	    != SLURM_SUCCESS) {
		xfree(id_str);
		return SLURM_ERROR;
	}

	_get_object_usage(mysql_conn, type, my_usage_table, cluster_name,
			  id_str, start, end, my_list);
	xfree(id_str);

	return rc;
}

extern int as_mysql_roll_usage(mysql_conn_t *mysql_conn, time_t sent_start,
			       time_t sent_end, uint16_t archive_data,
			       rollup_stats_t *rollup_stats)
{
	int rc = SLURM_SUCCESS;
	int rolledup = 0;
	int roll_started = 0;
	char *cluster_name = NULL;
	ListIterator itr;
	pthread_mutex_t rolledup_lock = PTHREAD_MUTEX_INITIALIZER;
	pthread_cond_t rolledup_cond;
	//DEF_TIMERS;

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	slurm_mutex_lock(&usage_rollup_lock);

	slurm_mutex_init(&rolledup_lock);
	slurm_cond_init(&rolledup_cond, NULL);

	//START_TIMER;
	slurm_mutex_lock(&as_mysql_cluster_list_lock);
	itr = list_iterator_create(as_mysql_cluster_list);
	while ((cluster_name = list_next(itr))) {
		local_rollup_t *local_rollup = xmalloc(sizeof(local_rollup_t));

		local_rollup->archive_data = archive_data;
		local_rollup->cluster_name = cluster_name;

		local_rollup->mysql_conn = mysql_conn;
		local_rollup->rc = &rc;
		local_rollup->rolledup = &rolledup;
		local_rollup->rolledup_lock = &rolledup_lock;
		local_rollup->rolledup_cond = &rolledup_cond;

		local_rollup->sent_end = sent_end;
		local_rollup->sent_start = sent_start;
		local_rollup->rollup_stats = rollup_stats;

		/* _cluster_rollup_usage is responsible for freeing
		   this local_rollup */
		/* If you have many jobs in your system the
		 * _cluster_rollup_usage call takes up a bunch of time
		 * and all the while the as_mysql_cluster_list_lock is
		 * locked.  If a slurmctld is starting up while this
		 * is locked it will hang waiting to get information
		 * from the DBD.  So threading this makes a lot of
		 * sense.  While it only buys a very small victory in
		 * terms of speed, having the
		 * as_mysql_cluster_list_lock lock unlock in a timely
		 * fashion buys a bunch on systems with lots
		 * (millions) of jobs.
		 */
		slurm_thread_create_detached(NULL, _cluster_rollup_usage,
					     local_rollup);
		roll_started++;
	}
	slurm_mutex_lock(&rolledup_lock);
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&as_mysql_cluster_list_lock);

	while (rolledup < roll_started) {
		slurm_cond_wait(&rolledup_cond, &rolledup_lock);
		debug2("Got %d of %d rolled up", rolledup, roll_started);
	}
	slurm_mutex_unlock(&rolledup_lock);
	debug2("Everything rolled up");
	slurm_mutex_destroy(&rolledup_lock);
	slurm_cond_destroy(&rolledup_cond);
	/* END_TIMER; */
	/* info("total time was %s", TIME_STR); */

	slurm_mutex_unlock(&usage_rollup_lock);

	return rc;
}
