/*****************************************************************************\
 *  mysql_usage.c - functions dealing with usage.
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

#include "mysql_usage.h"
#include "mysql_rollup.h"

time_t global_last_rollup = 0;
pthread_mutex_t rollup_lock = PTHREAD_MUTEX_INITIALIZER;

static int _get_cluster_usage(mysql_conn_t *mysql_conn, uid_t uid,
			      acct_cluster_rec_t *cluster_rec,
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
	char *cluster_req_inx[] = {
		"alloc_cpu_secs",
		"down_cpu_secs",
		"pdown_cpu_secs",
		"idle_cpu_secs",
		"resv_cpu_secs",
		"over_cpu_secs",
		"cpu_count",
		"period_start"
	};

	enum {
		CLUSTER_ACPU,
		CLUSTER_DCPU,
		CLUSTER_PDCPU,
		CLUSTER_ICPU,
		CLUSTER_RCPU,
		CLUSTER_OCPU,
		CLUSTER_CPU_COUNT,
		CLUSTER_START,
		CLUSTER_COUNT
	};

	if(!cluster_rec->name || !cluster_rec->name[0]) {
		error("We need a cluster name to set data for");
		return SLURM_ERROR;
	}

	if(set_usage_information(&my_usage_table, type, &start, &end)
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
		"select %s from %s where (period_start < %d "
		"&& period_start >= %d) and cluster=\"%s\"",
		tmp, my_usage_table, end, start, cluster_rec->name);

	xfree(tmp);
	debug4("%d(%s:%d) query\n%s", mysql_conn->conn, __FILE__, __LINE__, query);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	if(!cluster_rec->accounting_list)
		cluster_rec->accounting_list =
			list_create(destroy_cluster_accounting_rec);

	while((row = mysql_fetch_row(result))) {
		cluster_accounting_rec_t *accounting_rec =
			xmalloc(sizeof(cluster_accounting_rec_t));
		accounting_rec->alloc_secs = atoll(row[CLUSTER_ACPU]);
		accounting_rec->down_secs = atoll(row[CLUSTER_DCPU]);
		accounting_rec->pdown_secs = atoll(row[CLUSTER_PDCPU]);
		accounting_rec->idle_secs = atoll(row[CLUSTER_ICPU]);
		accounting_rec->over_secs = atoll(row[CLUSTER_OCPU]);
		accounting_rec->resv_secs = atoll(row[CLUSTER_RCPU]);
		accounting_rec->cpu_count = atoi(row[CLUSTER_CPU_COUNT]);
		accounting_rec->period_start = atoi(row[CLUSTER_START]);
		list_append(cluster_rec->accounting_list, accounting_rec);
	}
	mysql_free_result(result);

	return rc;
}



/* checks should already be done before this to see if this is a valid
   user or not.
*/
extern int get_usage_for_list(mysql_conn_t *mysql_conn,
			      slurmdbd_msg_type_t type, List object_list,
			      time_t start, time_t end)
{
	int rc = SLURM_SUCCESS;
	int i=0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *tmp = NULL;
	char *my_usage_table = NULL;
	char *query = NULL;
	List usage_list = NULL;
	char *id_str = NULL;
	ListIterator itr = NULL, u_itr = NULL;
	void *object = NULL;
	acct_association_rec_t *assoc = NULL;
	acct_wckey_rec_t *wckey = NULL;
	acct_accounting_rec_t *accounting_rec = NULL;

	/* Since for id in association table we
	   use t3 and in wckey table we use t1 we can't define it here */
	char **usage_req_inx = NULL;

	enum {
		USAGE_ID,
		USAGE_START,
		USAGE_ACPU,
		USAGE_COUNT
	};


	if(!object_list) {
		error("We need an object to set data for getting usage");
		return SLURM_ERROR;
	}

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	switch (type) {
	case DBD_GET_ASSOC_USAGE:
	{
		char *temp_usage[] = {
			"t3.id",
			"t1.period_start",
			"t1.alloc_cpu_secs"
		};
		usage_req_inx = temp_usage;

		itr = list_iterator_create(object_list);
		while((assoc = list_next(itr))) {
			if(id_str)
				xstrfmtcat(id_str, " || t3.id=%d", assoc->id);
			else
				xstrfmtcat(id_str, "t3.id=%d", assoc->id);
		}
		list_iterator_destroy(itr);

		my_usage_table = assoc_day_table;
		break;
	}
	case DBD_GET_WCKEY_USAGE:
	{
		char *temp_usage[] = {
			"id",
			"period_start",
			"alloc_cpu_secs"
		};
		usage_req_inx = temp_usage;

		itr = list_iterator_create(object_list);
		while((wckey = list_next(itr))) {
			if(id_str)
				xstrfmtcat(id_str, " || id=%d", wckey->id);
			else
				xstrfmtcat(id_str, "id=%d", wckey->id);
		}
		list_iterator_destroy(itr);

		my_usage_table = wckey_day_table;
		break;
	}
	default:
		error("Unknown usage type %d", type);
		return SLURM_ERROR;
		break;
	}

	if(set_usage_information(&my_usage_table, type, &start, &end)
	   != SLURM_SUCCESS) {
		xfree(id_str);
		return SLURM_ERROR;
	}

	xfree(tmp);
	i=0;
	xstrfmtcat(tmp, "%s", usage_req_inx[i]);
	for(i=1; i<USAGE_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", usage_req_inx[i]);
	}
	switch (type) {
	case DBD_GET_ASSOC_USAGE:
		query = xstrdup_printf(
			"select %s from %s as t1, %s as t2, %s as t3 "
			"where (t1.period_start < %d && t1.period_start >= %d) "
			"&& t1.id=t2.id && (%s) && "
			"t2.lft between t3.lft and t3.rgt "
			"order by t3.id, period_start;",
			tmp, my_usage_table, assoc_table, assoc_table,
			end, start, id_str);
		break;
	case DBD_GET_WCKEY_USAGE:
		query = xstrdup_printf(
			"select %s from %s "
			"where (period_start < %d && period_start >= %d) "
			"&& (%s) order by id, period_start;",
			tmp, my_usage_table, end, start, id_str);
		break;
	default:
		error("Unknown usage type %d", type);
		xfree(id_str);
		xfree(tmp);
		return SLURM_ERROR;
		break;
	}
	xfree(id_str);
	xfree(tmp);

	debug4("%d(%s:%d) query\n%s", mysql_conn->conn, __FILE__, __LINE__, query);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	usage_list = list_create(destroy_acct_accounting_rec);

	while((row = mysql_fetch_row(result))) {
		acct_accounting_rec_t *accounting_rec =
			xmalloc(sizeof(acct_accounting_rec_t));
		accounting_rec->id = atoi(row[USAGE_ID]);
		accounting_rec->period_start = atoi(row[USAGE_START]);
		accounting_rec->alloc_secs = atoll(row[USAGE_ACPU]);
		list_append(usage_list, accounting_rec);
	}
	mysql_free_result(result);

	u_itr = list_iterator_create(usage_list);
	itr = list_iterator_create(object_list);
	while((object = list_next(itr))) {
		int found = 0;
		int id = 0;
		List acct_list = NULL;

		switch (type) {
		case DBD_GET_ASSOC_USAGE:
			assoc = (acct_association_rec_t *)object;
			if(!assoc->accounting_list)
				assoc->accounting_list = list_create(
					destroy_acct_accounting_rec);
			acct_list = assoc->accounting_list;
			id = assoc->id;
			break;
		case DBD_GET_WCKEY_USAGE:
			wckey = (acct_wckey_rec_t *)object;
			if(!wckey->accounting_list)
				wckey->accounting_list = list_create(
					destroy_acct_accounting_rec);
			acct_list = wckey->accounting_list;
			id = wckey->id;
			break;
		default:
			continue;
			break;
		}

		while((accounting_rec = list_next(u_itr))) {
			if(id == accounting_rec->id) {
				list_append(acct_list, accounting_rec);
				list_remove(u_itr);
				found = 1;
			} else if(found) {
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

	if(list_count(usage_list))
		error("we have %d records not added "
		      "to the association list",
		      list_count(usage_list));
	list_destroy(usage_list);


	return rc;
}

extern int mysql_get_usage(mysql_conn_t *mysql_conn, uid_t uid,
			   void *in, slurmdbd_msg_type_t type,
			   time_t start, time_t end)
{
	int rc = SLURM_SUCCESS;
	int i=0, is_admin=1;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *tmp = NULL;
	char *my_usage_table = NULL;
	acct_association_rec_t *acct_assoc = in;
	acct_wckey_rec_t *acct_wckey = in;
	char *query = NULL;
	char *username = NULL;
	uint16_t private_data = 0;
	acct_user_rec_t user;
	List *my_list;
	uint32_t id = NO_VAL;

	char **usage_req_inx = NULL;

	enum {
		USAGE_ID,
		USAGE_START,
		USAGE_ACPU,
		USAGE_COUNT
	};

	switch (type) {
	case DBD_GET_ASSOC_USAGE:
	{
		char *temp_usage[] = {
			"t3.id",
			"t1.period_start",
			"t1.alloc_cpu_secs"
		};
		usage_req_inx = temp_usage;

		id = acct_assoc->id;
		username = acct_assoc->user;
		my_list = &acct_assoc->accounting_list;
		my_usage_table = assoc_day_table;
		break;
	}
	case DBD_GET_WCKEY_USAGE:
	{
		char *temp_usage[] = {
			"id",
			"period_start",
			"alloc_cpu_secs"
		};
		usage_req_inx = temp_usage;

		id = acct_wckey->id;
		username = acct_wckey->user;
		my_list = &acct_wckey->accounting_list;
		my_usage_table = wckey_day_table;
		break;
	}
	case DBD_GET_CLUSTER_USAGE:
	{
		return _get_cluster_usage(mysql_conn, uid, in,
					  type, start, end);
		break;
	}
	default:
		error("Unknown usage type %d", type);
		return SLURM_ERROR;
		break;
	}

	if(!id) {
		error("We need an id to set data for getting usage");
		return SLURM_ERROR;
	}

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	memset(&user, 0, sizeof(acct_user_rec_t));
	user.uid = uid;

	private_data = slurm_get_private_data();
	if (private_data & PRIVATE_DATA_USAGE) {
		/* This only works when running though the slurmdbd.
		 * THERE IS NO AUTHENTICATION WHEN RUNNNING OUT OF THE
		 * SLURMDBD!
		 */
		if(slurmdbd_conf) {
			is_admin = 0;
			/* we have to check the authentication here in the
			 * plugin since we don't know what accounts are being
			 * referenced until after the query.  Here we will
			 * set if they are an operator or greater and then
			 * check it below after the query.
			 */
			if((uid == slurmdbd_conf->slurm_user_id || uid == 0)
			   || assoc_mgr_get_admin_level(mysql_conn, uid)
			   >= ACCT_ADMIN_OPERATOR)
				is_admin = 1;
			else {
				assoc_mgr_fill_in_user(mysql_conn, &user, 1,
						       NULL);
			}

			if(!is_admin) {
				ListIterator itr = NULL;
				acct_coord_rec_t *coord = NULL;

				if(username &&
				   !strcmp(acct_assoc->user, user.name))
					goto is_user;

				if(type != DBD_GET_ASSOC_USAGE)
					goto bad_user;

				if(!user.coord_accts) {
					debug4("This user isn't a coord.");
					goto bad_user;
				}

				if(!acct_assoc->acct) {
					debug("No account name given "
					      "in association.");
					goto bad_user;
				}

				itr = list_iterator_create(user.coord_accts);
				while((coord = list_next(itr))) {
					if(!strcasecmp(coord->name,
						       acct_assoc->acct))
						break;
				}
				list_iterator_destroy(itr);

				if(coord)
					goto is_user;

			bad_user:
				errno = ESLURM_ACCESS_DENIED;
				return SLURM_ERROR;
			}
		}
	}
is_user:

	if(set_usage_information(&my_usage_table, type, &start, &end)
	   != SLURM_SUCCESS) {
		return SLURM_ERROR;
	}

	xfree(tmp);
	i=0;
	xstrfmtcat(tmp, "%s", usage_req_inx[i]);
	for(i=1; i<USAGE_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", usage_req_inx[i]);
	}
	switch (type) {
	case DBD_GET_ASSOC_USAGE:
		query = xstrdup_printf(
			"select %s from %s as t1, %s as t2, %s as t3 "
			"where (t1.period_start < %d && t1.period_start >= %d) "
			"&& t1.id=t2.id && t3.id=%d && "
			"t2.lft between t3.lft and t3.rgt "
			"order by t3.id, period_start;",
			tmp, my_usage_table, assoc_table, assoc_table,
			end, start, id);
		break;
	case DBD_GET_WCKEY_USAGE:
		query = xstrdup_printf(
			"select %s from %s "
			"where (period_start < %d && period_start >= %d) "
			"&& id=%d order by id, period_start;",
			tmp, my_usage_table, end, start, id);
		break;
	default:
		error("Unknown usage type %d", type);
		return SLURM_ERROR;
		break;
	}

	xfree(tmp);
	debug4("%d(%s:%d) query\n%s", mysql_conn->conn, __FILE__, __LINE__, query);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	if(!(*my_list))
		(*my_list) = list_create(destroy_acct_accounting_rec);

	while((row = mysql_fetch_row(result))) {
		acct_accounting_rec_t *accounting_rec =
			xmalloc(sizeof(acct_accounting_rec_t));
		accounting_rec->id = atoi(row[USAGE_ID]);
		accounting_rec->period_start = atoi(row[USAGE_START]);
		accounting_rec->alloc_secs = atoll(row[USAGE_ACPU]);
		list_append((*my_list), accounting_rec);
	}
	mysql_free_result(result);

	return rc;
}

extern int mysql_roll_usage(mysql_conn_t *mysql_conn,
			    time_t sent_start, time_t sent_end,
			    uint16_t archive_data)
{
	int rc = SLURM_SUCCESS;
	int i = 0;
	time_t my_time = sent_end;
	struct tm start_tm;
	struct tm end_tm;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *query = NULL;
	char *tmp = NULL;
	time_t last_hour = sent_start;
	time_t last_day = sent_start;
	time_t last_month = sent_start;
	time_t start_time = 0;
  	time_t end_time = 0;
	DEF_TIMERS;

	char *update_req_inx[] = {
		"hourly_rollup",
		"daily_rollup",
		"monthly_rollup"
	};

	enum {
		UPDATE_HOUR,
		UPDATE_DAY,
		UPDATE_MONTH,
		UPDATE_COUNT
	};

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	if(!sent_start) {
		i=0;
		xstrfmtcat(tmp, "%s", update_req_inx[i]);
		for(i=1; i<UPDATE_COUNT; i++) {
			xstrfmtcat(tmp, ", %s", update_req_inx[i]);
		}
		query = xstrdup_printf("select %s from %s",
				       tmp, last_ran_table);
		xfree(tmp);

		debug4("%d(%s:%d) query\n%s", mysql_conn->conn,
		       __FILE__, __LINE__, query);
		if(!(result = mysql_db_query_ret(
			     mysql_conn->db_conn, query, 0))) {
			xfree(query);
			return SLURM_ERROR;
		}

		xfree(query);
		row = mysql_fetch_row(result);
		if(row) {
			last_hour = atoi(row[UPDATE_HOUR]);
			last_day = atoi(row[UPDATE_DAY]);
			last_month = atoi(row[UPDATE_MONTH]);
			mysql_free_result(result);
		} else {
			time_t now = time(NULL);
			/* If we don't have any events like adding a
			 * cluster this will not work correctly, so we
			 * will insert now as a starting point.
			 */
			query = xstrdup_printf(
				"set @PS = %d;"
				"select @PS := period_start from %s limit 1;"
				"insert into %s "
				"(hourly_rollup, daily_rollup, monthly_rollup) "
				"values (@PS, @PS, @PS);",
				now, event_table, last_ran_table);

			debug3("%d(%s:%d) query\n%s", mysql_conn->conn,
			       __FILE__, __LINE__, query);
			mysql_free_result(result);
			if(!(result = mysql_db_query_ret(
				     mysql_conn->db_conn, query, 0))) {
				xfree(query);
				return SLURM_ERROR;
			}
			xfree(query);
			row = mysql_fetch_row(result);
			if(!row) {
				debug("No clusters have been added "
				      "not doing rollup");
				mysql_free_result(result);
				return SLURM_SUCCESS;
			}

			last_hour = last_day = last_month = atoi(row[0]);
			mysql_free_result(result);
		}
	}

	if(!my_time)
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

	if(!localtime_r(&last_hour, &start_tm)) {
		error("Couldn't get localtime from hour start %d", last_hour);
		return SLURM_ERROR;
	}

	if(!localtime_r(&my_time, &end_tm)) {
		error("Couldn't get localtime from hour end %d", my_time);
		return SLURM_ERROR;
	}

	/* below and anywhere in a rollup plugin when dealing with
	 * epoch times we need to set the tm_isdst = -1 so we don't
	 * have to worry about the time changes.  Not setting it to -1
	 * will cause problems in the day and month with the date change.
	 */

	start_tm.tm_sec = 0;
	start_tm.tm_min = 0;
	start_tm.tm_isdst = -1;
	start_time = mktime(&start_tm);
	end_tm.tm_sec = 0;
	end_tm.tm_min = 0;
	end_tm.tm_isdst = -1;
	end_time = mktime(&end_tm);

/* 	info("hour start %s", ctime(&start_time)); */
/* 	info("hour end %s", ctime(&end_time)); */
/* 	info("diff is %d", end_time-start_time); */

	slurm_mutex_lock(&rollup_lock);
	global_last_rollup = end_time;
	slurm_mutex_unlock(&rollup_lock);

	if(end_time-start_time > 0) {
		START_TIMER;
		if((rc = mysql_hourly_rollup(mysql_conn, start_time, end_time))
		   != SLURM_SUCCESS)
			return rc;
		END_TIMER3("hourly_rollup", 5000000);
		/* If we have a sent_end do not update the last_run_table */
		if(!sent_end)
			query = xstrdup_printf("update %s set hourly_rollup=%d",
					       last_ran_table, end_time);
	} else {
		debug2("no need to run this hour %d <= %d",
		       end_time, start_time);
	}

	if(!localtime_r(&last_day, &start_tm)) {
		error("Couldn't get localtime from day %d", last_day);
		return SLURM_ERROR;
	}
	start_tm.tm_sec = 0;
	start_tm.tm_min = 0;
	start_tm.tm_hour = 0;
	start_tm.tm_isdst = -1;
	start_time = mktime(&start_tm);
	end_tm.tm_hour = 0;
	end_tm.tm_isdst = -1;
	end_time = mktime(&end_tm);

/* 	info("day start %s", ctime(&start_time)); */
/* 	info("day end %s", ctime(&end_time)); */
/* 	info("diff is %d", end_time-start_time); */

	if(end_time-start_time > 0) {
		START_TIMER;
		if((rc = mysql_daily_rollup(mysql_conn, start_time, end_time,
					    archive_data))
		   != SLURM_SUCCESS)
			return rc;
		END_TIMER2("daily_rollup");
		if(query && !sent_end)
			xstrfmtcat(query, ", daily_rollup=%d", end_time);
		else if(!sent_end)
			query = xstrdup_printf("update %s set daily_rollup=%d",
					       last_ran_table, end_time);
	} else {
		debug2("no need to run this day %d <= %d",
		       end_time, start_time);
	}

	if(!localtime_r(&last_month, &start_tm)) {
		error("Couldn't get localtime from month %d", last_month);
		return SLURM_ERROR;
	}

	start_tm.tm_sec = 0;
	start_tm.tm_min = 0;
	start_tm.tm_hour = 0;
	start_tm.tm_mday = 1;
	start_tm.tm_isdst = -1;
	start_time = mktime(&start_tm);
	end_time = mktime(&end_tm);

	end_tm.tm_sec = 0;
	end_tm.tm_min = 0;
	end_tm.tm_hour = 0;
	end_tm.tm_mday = 1;
	end_tm.tm_isdst = -1;
	end_time = mktime(&end_tm);

/* 	info("month start %s", ctime(&start_time)); */
/* 	info("month end %s", ctime(&end_time)); */
/* 	info("diff is %d", end_time-start_time); */

	if(end_time-start_time > 0) {
		START_TIMER;
		if((rc = mysql_monthly_rollup(
			    mysql_conn, start_time, end_time, archive_data))
		   != SLURM_SUCCESS)
			return rc;
		END_TIMER2("monthly_rollup");

		if(query && !sent_end)
			xstrfmtcat(query, ", monthly_rollup=%d", end_time);
		else if(!sent_end)
			query = xstrdup_printf(
				"update %s set monthly_rollup=%d",
				last_ran_table, end_time);
	} else {
		debug2("no need to run this month %d <= %d",
		       end_time, start_time);
	}

	if(query) {
		debug3("%d(%s:%d) query\n%s", mysql_conn->conn, __FILE__, __LINE__, query);
		rc = mysql_db_query(mysql_conn->db_conn, query);
		xfree(query);
	}
	return rc;
}
