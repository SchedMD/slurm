/*****************************************************************************\
 *  as_mysql_rollup.c - functions for rolling up data for associations
 *                   and machines from the as_mysql storage.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

#include "as_mysql_rollup.h"
#include "as_mysql_archive.h"
#include "src/common/parse_time.h"
#include "src/common/slurm_time.h"

enum {
	TIME_ALLOC,
	TIME_DOWN,
	TIME_PDOWN,
	TIME_RESV
};

enum {
	ASSOC_TABLES,
	WCKEY_TABLES
};

typedef struct {
	uint64_t count;
	uint32_t id;
	uint64_t time_alloc;
	uint64_t time_down;
	uint64_t time_idle;
	uint64_t time_over;
	uint64_t time_pd;
	uint64_t time_resv;
	uint64_t total_time;
} local_tres_usage_t;

typedef struct {
	int id;
	List loc_tres;
} local_id_usage_t;

typedef struct {
	time_t end;
	int id; /*only needed for reservations */
	List loc_tres;
	time_t start;
} local_cluster_usage_t;

typedef struct {
	time_t end;
	int id;
	List local_assocs; /* list of assocs to spread unused time
			      over of type local_id_usage_t */
	List loc_tres;
	time_t orig_start;
	time_t start;
	double unused_wall;
} local_resv_usage_t;

static void _destroy_local_tres_usage(void *object)
{
	local_tres_usage_t *a_usage = (local_tres_usage_t *)object;
	if (a_usage) {
		xfree(a_usage);
	}
}

static void _destroy_local_id_usage(void *object)
{
	local_id_usage_t *a_usage = (local_id_usage_t *)object;
	if (a_usage) {
		FREE_NULL_LIST(a_usage->loc_tres);
		xfree(a_usage);
	}
}

static void _destroy_local_cluster_usage(void *object)
{
	local_cluster_usage_t *c_usage = (local_cluster_usage_t *)object;
	if (c_usage) {
		FREE_NULL_LIST(c_usage->loc_tres);
		xfree(c_usage);
	}
}

static void _destroy_local_resv_usage(void *object)
{
	local_resv_usage_t *r_usage = (local_resv_usage_t *)object;
	if (r_usage) {
		FREE_NULL_LIST(r_usage->local_assocs);
		FREE_NULL_LIST(r_usage->loc_tres);
		xfree(r_usage);
	}
}

static int _find_loc_tres(void *x, void *key)
{
	local_tres_usage_t *loc_tres = (local_tres_usage_t *)x;
	uint32_t tres_id = *(uint32_t *)key;

	if (loc_tres->id == tres_id)
		return 1;
	return 0;
}

static int _find_id_usage(void *x, void *key)
{
	local_id_usage_t *loc = (local_id_usage_t *)x;
	uint32_t id = *(uint32_t *)key;

	if (loc->id == id)
		return 1;
	return 0;
}

static void _remove_job_tres_time_from_cluster(List c_tres, List j_tres,
					       int seconds)
{
	ListIterator c_itr;
	local_tres_usage_t *loc_c_tres, *loc_j_tres;
	uint64_t time;

	if ((seconds <= 0) || !c_tres || !j_tres ||
	    !list_count(c_tres) || !list_count(j_tres))
		return;

	c_itr = list_iterator_create(c_tres);
	while ((loc_c_tres = list_next(c_itr))) {
		if (!(loc_j_tres = list_find_first(
			      j_tres, _find_loc_tres, &loc_c_tres->id)))
			continue;
		time = seconds * loc_j_tres->count;

		if (time >= loc_c_tres->total_time)
			loc_c_tres->total_time = 0;
		else
			loc_c_tres->total_time -= time;
	}
	list_iterator_destroy(c_itr);
}


static local_tres_usage_t *_add_time_tres(List tres_list, int type, uint32_t id,
					  uint64_t time, bool times_count)
{
	local_tres_usage_t *loc_tres;

	/* Energy TRES could have a NO_VAL64, we want to skip those as it is the
	 * same as a 0 since nothing was gathered.
	 */
	if (!time || (time == NO_VAL64))
		return NULL;

	loc_tres = list_find_first(tres_list, _find_loc_tres, &id);

	if (!loc_tres) {
		if (times_count)
			return NULL;
		loc_tres = xmalloc(sizeof(local_tres_usage_t));
		loc_tres->id = id;
		list_append(tres_list, loc_tres);
	}

	if (times_count) {
		if (!loc_tres->count)
			return NULL;
		time *= loc_tres->count;
	}

	switch (type) {
	case TIME_ALLOC:
		loc_tres->time_alloc += time;
		break;
	case TIME_DOWN:
		loc_tres->time_down += time;
		break;
	case TIME_PDOWN:
		loc_tres->time_pd += time;
		break;
	case TIME_RESV:
		loc_tres->time_resv += time;
		break;
	default:
		error("_add_time_tres: unknown type %d given", type);
		xassert(0);
		break;
	}

	return loc_tres;
}

static void _add_time_tres_list(List tres_list_out, List tres_list_in, int type,
				uint64_t time_in, bool times_count)
{
	ListIterator itr;
	local_tres_usage_t *loc_tres;

	xassert(tres_list_in);
	xassert(tres_list_out);

	itr = list_iterator_create(tres_list_in);
	while ((loc_tres = list_next(itr)))
		_add_time_tres(tres_list_out, type,
			       loc_tres->id,
			       time_in ? time_in : loc_tres->total_time,
			       times_count);
	list_iterator_destroy(itr);
}

/*
 * Job usage is a ratio of its tres to the reservation's tres:
 * Unused wall = unused wall - job_seconds * job_tres / resv_tres
 */
static int _update_unused_wall(local_resv_usage_t *r_usage, List job_tres,
			       int job_seconds)
{
	ListIterator resv_itr;
	local_tres_usage_t *loc_tres;
	uint32_t resv_tres_id;
	uint64_t resv_tres_count;
	double tres_ratio = 0.0;

	/* Get TRES counts. Make sure the TRES types match. */
	resv_itr = list_iterator_create(r_usage->loc_tres);
	while ((loc_tres = list_next(resv_itr))) {
		/* Avoid dividing by zero. */
		if (!loc_tres->count)
			continue;
		resv_tres_id = loc_tres->id;
		resv_tres_count = loc_tres->count;
		if ((loc_tres = list_find_first(job_tres,
						_find_loc_tres,
						&resv_tres_id))) {
			tres_ratio = (double)loc_tres->count /
				(double)resv_tres_count;
			break;
		}
	}
	list_iterator_destroy(resv_itr);

	/*
	 * Here we are converting TRES seconds to wall seconds.  This is needed
	 * to determine how much time is actually idle in the reservation.
	 */
	r_usage->unused_wall -=	(double)job_seconds * tres_ratio;

	if (r_usage->unused_wall < 0) {
		/* I'm not sure if I should error or silently ignore. */
		debug3("WARNING: Unused wall is less than zero; this should never happen. Setting it to zero for resv id = %d, start = %ld.",
		       r_usage->id, r_usage->orig_start);
		r_usage->unused_wall = 0;
	}
	return SLURM_SUCCESS;
}

static void _add_job_alloc_time_to_cluster(List c_tres_list, List j_tres)
{
	ListIterator c_itr = list_iterator_create(c_tres_list);
	local_tres_usage_t *loc_c_tres, *loc_j_tres;

	while ((loc_c_tres = list_next(c_itr))) {
		if (!(loc_j_tres = list_find_first(
			      j_tres, _find_loc_tres, &loc_c_tres->id)))
			continue;
		loc_c_tres->time_alloc += loc_j_tres->time_alloc;
	}
	list_iterator_destroy(c_itr);
}

static void _setup_cluster_tres(List tres_list, uint32_t id,
				uint64_t count, int seconds)
{
	local_tres_usage_t *loc_tres =
		list_find_first(tres_list, _find_loc_tres, &id);

	if (!loc_tres) {
		loc_tres = xmalloc(sizeof(local_tres_usage_t));
		loc_tres->id = id;
		list_append(tres_list, loc_tres);
	}

	loc_tres->count = count;
	loc_tres->total_time += seconds * loc_tres->count;
}

static void _add_tres_2_list(List tres_list, char *tres_str, int seconds)
{
	char *tmp_str = tres_str;
	int id;
	uint64_t count;

	xassert(tres_list);

	if (!tres_str || !tres_str[0])
		return;

	while (tmp_str) {
		id = atoi(tmp_str);
		if (id < 1) {
			error("_add_tres_2_list: no id "
			      "found at %s instead", tmp_str);
			break;
		}

		/* We don't run rollup on a node basis
		 * because they are shared resources on
		 * many systems so it will almost always
		 * have over committed resources.
		 */
		if (id != TRES_NODE) {
			if (!(tmp_str = strchr(tmp_str, '='))) {
				error("_add_tres_2_list: no value found");
				xassert(0);
				break;
			}
			count = slurm_atoull(++tmp_str);
			_setup_cluster_tres(tres_list, id, count, seconds);
		}

		if (!(tmp_str = strchr(tmp_str, ',')))
			break;
		tmp_str++;
	}

	return;
}

/* This will destroy the *loc_tres given after it is transfered */
static void _transfer_loc_tres(List *loc_tres, local_id_usage_t *usage)
{
	if (!usage || !*loc_tres) {
		FREE_NULL_LIST(*loc_tres);
		return;
	}

	if (!usage->loc_tres) {
		usage->loc_tres = *loc_tres;
		*loc_tres = NULL;
	} else {
		_add_job_alloc_time_to_cluster(usage->loc_tres, *loc_tres);
		FREE_NULL_LIST(*loc_tres);
	}
}

static void _add_tres_time_2_list(List tres_list, char *tres_str,
				  int type, int seconds, int suspend_seconds,
				  bool times_count)
{
	char *tmp_str = tres_str;
	int id;
	uint64_t time, count;
	local_tres_usage_t *loc_tres;

	xassert(tres_list);

	if (!tres_str || !tres_str[0])
		return;

	while (tmp_str) {
		int loc_seconds = seconds;

		id = atoi(tmp_str);
		if (id < 1) {
			error("_add_tres_time_2_list: no id "
			      "found at %s", tmp_str);
			break;
		}
		if (!(tmp_str = strchr(tmp_str, '='))) {
			error("_add_tres_time_2_list: no value found for "
			      "id %d '%s'", id, tres_str);
			xassert(0);
			break;
		}

		/* Take away suspended time from TRES that are idle when the
		 * job was suspended, currently only CPU's fill that bill.
		 */
		if (suspend_seconds && (id == TRES_CPU)) {
			loc_seconds -= suspend_seconds;
			if (loc_seconds < 1)
				loc_seconds = 0;
		}

		time = count = slurm_atoull(++tmp_str);
		/* ENERGY is already totalled for the entire job so don't
		 * multiple with time.
		 */
		if (id != TRES_ENERGY)
			time *= loc_seconds;

		loc_tres = _add_time_tres(tres_list, type, id,
					  time, times_count);

		if (loc_tres && !loc_tres->count)
			loc_tres->count = count;

		if (!(tmp_str = strchr(tmp_str, ',')))
			break;
		tmp_str++;
	}

	return;
}

static int _process_purge(mysql_conn_t *mysql_conn,
			  char *cluster_name,
			  uint16_t archive_data,
			  uint32_t purge_period)
{
	int rc = SLURM_SUCCESS;
	slurmdb_archive_cond_t arch_cond;
	slurmdb_job_cond_t job_cond;

	/* if we didn't ask for archive data return here and don't do
	   anything extra just rollup */

	if (!archive_data)
		return SLURM_SUCCESS;

	if (!slurmdbd_conf)
		return SLURM_SUCCESS;

	memset(&job_cond, 0, sizeof(job_cond));
	memset(&arch_cond, 0, sizeof(arch_cond));
	arch_cond.archive_dir = slurmdbd_conf->archive_dir;
	arch_cond.archive_script = slurmdbd_conf->archive_script;

	if (purge_period & slurmdbd_conf->purge_event)
		arch_cond.purge_event = slurmdbd_conf->purge_event;
	else
		arch_cond.purge_event = NO_VAL;
	if (purge_period & slurmdbd_conf->purge_job)
		arch_cond.purge_job = slurmdbd_conf->purge_job;
	else
		arch_cond.purge_job = NO_VAL;

	if (purge_period & slurmdbd_conf->purge_resv)
		arch_cond.purge_resv = slurmdbd_conf->purge_resv;
	else
		arch_cond.purge_resv = NO_VAL;

	if (purge_period & slurmdbd_conf->purge_step)
		arch_cond.purge_step = slurmdbd_conf->purge_step;
	else
		arch_cond.purge_step = NO_VAL;
	if (purge_period & slurmdbd_conf->purge_suspend)
		arch_cond.purge_suspend = slurmdbd_conf->purge_suspend;
	else
		arch_cond.purge_suspend = NO_VAL;
	if (purge_period & slurmdbd_conf->purge_txn)
		arch_cond.purge_txn = slurmdbd_conf->purge_txn;
	else
		arch_cond.purge_txn = NO_VAL;
	if (purge_period & slurmdbd_conf->purge_usage)
		arch_cond.purge_usage = slurmdbd_conf->purge_usage;
	else
		arch_cond.purge_usage = NO_VAL;

	job_cond.cluster_list = list_create(NULL);
	list_append(job_cond.cluster_list, cluster_name);

	arch_cond.job_cond = &job_cond;
	rc = as_mysql_jobacct_process_archive(mysql_conn, &arch_cond);
	FREE_NULL_LIST(job_cond.cluster_list);

	return rc;
}

static void _setup_cluster_tres_usage(mysql_conn_t *mysql_conn,
				      char *cluster_name,
				      time_t curr_start, time_t curr_end,
				      time_t now, time_t use_start,
				      local_tres_usage_t *loc_tres,
				      char **query)
{
	char start_char[20], end_char[20];
	uint64_t total_used;

	if (!loc_tres)
		return;

	/* Now put the lists into the usage tables */

	/* sanity check to make sure we don't have more
	   allocated cpus than possible. */
	if (loc_tres->total_time
	    && (loc_tres->total_time < loc_tres->time_alloc)) {
		slurm_make_time_str(&curr_start, start_char,
				    sizeof(start_char));
		slurm_make_time_str(&curr_end, end_char,
				    sizeof(end_char));
		error("We have more allocated time than is "
		      "possible (%"PRIu64" > %"PRIu64") for "
		      "cluster %s(%"PRIu64") from %s - %s tres %u",
		      loc_tres->time_alloc, loc_tres->total_time,
		      cluster_name, loc_tres->count,
		      start_char, end_char, loc_tres->id);
		loc_tres->time_alloc = loc_tres->total_time;
	}

	total_used = loc_tres->time_alloc +
		loc_tres->time_down + loc_tres->time_pd;

	/* Make sure the total time we care about
	   doesn't go over the limit */
	if (loc_tres->total_time && (loc_tres->total_time < total_used)) {
		int64_t overtime;

		slurm_make_time_str(&curr_start, start_char,
				    sizeof(start_char));
		slurm_make_time_str(&curr_end, end_char,
				    sizeof(end_char));
		error("We have more time than is "
		      "possible (%"PRIu64"+%"PRIu64"+%"
		      PRIu64")(%"PRIu64") > %"PRIu64" for "
		      "cluster %s(%"PRIu64") from %s - %s tres %u",
		      loc_tres->time_alloc, loc_tres->time_down,
		      loc_tres->time_pd, total_used,
		      loc_tres->total_time,
		      cluster_name, loc_tres->count,
		      start_char, end_char, loc_tres->id);

		/* First figure out how much actual down time
		   we have and then how much
		   planned down time we have. */
		overtime = (int64_t)(loc_tres->total_time -
				     (loc_tres->time_alloc +
				      loc_tres->time_down));
		if (overtime < 0) {
			loc_tres->time_down += overtime;
			if ((int64_t)loc_tres->time_down < 0)
				loc_tres->time_down = 0;
		}

		overtime = (int64_t)(loc_tres->total_time -
				     (loc_tres->time_alloc +
				      loc_tres->time_down +
				      loc_tres->time_pd));
		if (overtime < 0) {
			loc_tres->time_pd += overtime;
			if ((int64_t)loc_tres->time_pd < 0)
				loc_tres->time_pd = 0;
		}

		total_used = loc_tres->time_alloc +
			loc_tres->time_down + loc_tres->time_pd;
		/* info("We now have (%"PRIu64"+%"PRIu64"+" */
		/*      "%"PRIu64")(%"PRIu64") " */
		/*       "?= %"PRIu64"", */
		/*       loc_tres->time_alloc, loc_tres->time_down, */
		/*       loc_tres->time_pd, total_used, */
		/*       loc_tres->total_time); */
	}
	/* info("Cluster %s now has (%"PRIu64"+%"PRIu64"+" */
	/*      "%"PRIu64")(%"PRIu64") ?= %"PRIu64"", */
	/*      cluster_name, */
	/*      c_usage->a_cpu, c_usage->d_cpu, */
	/*      c_usage->pd_cpu, total_used, */
	/*      c_usage->total_time); */

	loc_tres->time_idle = loc_tres->total_time -
		total_used - loc_tres->time_resv;
	/* sanity check just to make sure we have a
	 * legitimate time after we calulated
	 * idle/reserved time put extra in the over
	 * commit field
	 */
	/* info("%s got idle of %lld", loc_tres->name, */
	/*      (int64_t)loc_tres->time_idle); */
	if ((int64_t)loc_tres->time_idle < 0) {
		/* info("got %d %d %d", loc_tres->time_resv, */
		/*      loc_tres->time_idle, loc_tres->time_over); */
		loc_tres->time_resv += (int64_t)loc_tres->time_idle;
		loc_tres->time_over -= (int64_t)loc_tres->time_idle;
		loc_tres->time_idle = 0;
		if ((int64_t)loc_tres->time_resv < 0)
			loc_tres->time_resv = 0;
	}

	/* info("cluster %s(%u) down %"PRIu64" alloc %"PRIu64" " */
	/*      "resv %"PRIu64" idle %"PRIu64" over %"PRIu64" " */
	/*      "total= %"PRIu64" ?= %"PRIu64" from %s", */
	/*      cluster_name, */
	/*      loc_tres->count, loc_tres->time_down, */
	/*      loc_tres->time_alloc, */
	/*      loc_tres->time_resv, loc_tres->time_idle, */
	/*      loc_tres->time_over, */
	/*      loc_tres->time_down + loc_tres->time_alloc + */
	/*      loc_tres->time_resv + loc_tres->time_idle, */
	/*      loc_tres->total_time, */
	/*      slurm_ctime2(&loc_tres->start)); */
	/* info("to %s", slurm_ctime2(&loc_tres->end)); */
	if (*query)
		xstrfmtcat(*query, ", (%ld, %ld, %ld, %u, %"PRIu64", "
			   "%"PRIu64", %"PRIu64", %"PRIu64", "
			   "%"PRIu64", %"PRIu64", %"PRIu64")",
			   now, now, use_start, loc_tres->id,
			   loc_tres->count,
			   loc_tres->time_alloc,
			   loc_tres->time_down,
			   loc_tres->time_pd,
			   loc_tres->time_idle,
			   loc_tres->time_over,
			   loc_tres->time_resv);
	else
		xstrfmtcat(*query, "insert into \"%s_%s\" "
			   "(creation_time, mod_time, "
			   "time_start, id_tres, count, "
			   "alloc_secs, down_secs, pdown_secs, "
			   "idle_secs, over_secs, resv_secs) "
			   "values (%ld, %ld, %ld, %u, %"PRIu64", "
			   "%"PRIu64", %"PRIu64", %"PRIu64", "
			   "%"PRIu64", %"PRIu64", %"PRIu64")",
			   cluster_name, cluster_hour_table,
			   now, now,
			   use_start, loc_tres->id,
			   loc_tres->count,
			   loc_tres->time_alloc,
			   loc_tres->time_down,
			   loc_tres->time_pd,
			   loc_tres->time_idle,
			   loc_tres->time_over,
			   loc_tres->time_resv);

	return;
}

static int _process_cluster_usage(mysql_conn_t *mysql_conn,
				  char *cluster_name,
				  time_t curr_start, time_t curr_end,
				  time_t now, local_cluster_usage_t *c_usage)
{
	int rc = SLURM_SUCCESS;
	char *query = NULL;
	ListIterator itr;
	local_tres_usage_t *loc_tres;

	if (!c_usage)
		return rc;
	/* Now put the lists into the usage tables */

	xassert(c_usage->loc_tres);
	itr = list_iterator_create(c_usage->loc_tres);
	while ((loc_tres = list_next(itr))) {
		_setup_cluster_tres_usage(mysql_conn, cluster_name,
					  curr_start, curr_end, now,
					  c_usage->start, loc_tres, &query);
	}
	list_iterator_destroy(itr);

	if (!query)
		return rc;

	xstrfmtcat(query,
		   " on duplicate key update "
		   "mod_time=%ld, count=VALUES(count), "
		   "alloc_secs=VALUES(alloc_secs), "
		   "down_secs=VALUES(down_secs), "
		   "pdown_secs=VALUES(pdown_secs), "
		   "idle_secs=VALUES(idle_secs), "
		   "over_secs=VALUES(over_secs), "
		   "resv_secs=VALUES(resv_secs)",
		   now);

	/* Spacing out the inserts here instead of doing them
	   all at once in the end proves to be faster.  Just FYI
	   so we don't go testing again and again.
	*/
	if (debug_flags & DEBUG_FLAG_DB_USAGE)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	rc = mysql_db_query(mysql_conn, query);
	xfree(query);
	if (rc != SLURM_SUCCESS)
		error("Couldn't add cluster hour rollup");

	return rc;
}

static void _create_id_usage_insert(char *cluster_name, int type,
				    time_t curr_start, time_t now,
				    local_id_usage_t *id_usage,
				    char **query)
{
	local_tres_usage_t *loc_tres;
	ListIterator itr;
	bool first;
	char *table = NULL, *id_name = NULL;

	xassert(query);

	switch (type) {
	case ASSOC_TABLES:
		id_name = "id_assoc";
		table = assoc_hour_table;
		break;
	case WCKEY_TABLES:
		id_name = "id_wckey";
		table = wckey_hour_table;
		break;
	default:
		error("_create_id_usage_insert: unknown type %d", type);
		return;
		break;
	}

	if (!id_usage->loc_tres || !list_count(id_usage->loc_tres)) {
		error("%s %d doesn't have any tres", id_name, id_usage->id);
		return;
	}

	first = 1;
	itr = list_iterator_create(id_usage->loc_tres);
	while ((loc_tres = list_next(itr))) {
		if (!first) {
			xstrfmtcat(*query,
				   ", (%ld, %ld, %u, %ld, %u, %"PRIu64")",
				   now, now,
				   id_usage->id, curr_start, loc_tres->id,
				   loc_tres->time_alloc);
		} else {
			xstrfmtcat(*query,
				   "insert into \"%s_%s\" "
				   "(creation_time, mod_time, id, "
				   "time_start, id_tres, alloc_secs) "
				   "values (%ld, %ld, %u, %ld, %u, %"PRIu64")",
				   cluster_name, table, now, now,
				   id_usage->id, curr_start, loc_tres->id,
				   loc_tres->time_alloc);
			first = 0;
		}
	}
	list_iterator_destroy(itr);
	xstrfmtcat(*query,
		   " on duplicate key update mod_time=%ld, "
		   "alloc_secs=VALUES(alloc_secs);", now);
}

static local_cluster_usage_t *_setup_cluster_usage(mysql_conn_t *mysql_conn,
						   char *cluster_name,
						   time_t curr_start,
						   time_t curr_end,
						   List cluster_down_list)
{
	local_cluster_usage_t *c_usage = NULL;
	char *query = NULL;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int i = 0;
	ListIterator d_itr = NULL;
	local_cluster_usage_t *loc_c_usage;

	char *event_req_inx[] = {
		"node_name",
		"time_start",
		"time_end",
		"state",
		"tres",
	};
	char *event_str = NULL;
	enum {
		EVENT_REQ_NAME,
		EVENT_REQ_START,
		EVENT_REQ_END,
		EVENT_REQ_STATE,
		EVENT_REQ_TRES,
		EVENT_REQ_COUNT
	};

	xstrfmtcat(event_str, "%s", event_req_inx[i]);
	for(i=1; i<EVENT_REQ_COUNT; i++) {
		xstrfmtcat(event_str, ", %s", event_req_inx[i]);
	}

	/* first get the events during this time.  All that is
	 * except things with the maintainance flag set in the
	 * state.  We handle those later with the reservations.
	 */
	query = xstrdup_printf("select %s from \"%s_%s\" where "
			       "!(state & %d) && (time_start < %ld "
			       "&& (time_end >= %ld "
			       "|| time_end = 0)) "
			       "order by node_name, time_start",
			       event_str, cluster_name, event_table,
			       NODE_STATE_MAINT,
			       curr_end, curr_start);
	xfree(event_str);

	if (debug_flags & DEBUG_FLAG_DB_USAGE)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return NULL;
	}

	xfree(query);

	d_itr = list_iterator_create(cluster_down_list);
	while ((row = mysql_fetch_row(result))) {
		time_t row_start = slurm_atoul(row[EVENT_REQ_START]);
		time_t row_end = slurm_atoul(row[EVENT_REQ_END]);
		uint16_t state = slurm_atoul(row[EVENT_REQ_STATE]);
		int seconds;

		if (row_start < curr_start)
			row_start = curr_start;

		if (!row_end || row_end > curr_end)
			row_end = curr_end;

		/* Don't worry about it if the time is less
		 * than 1 second.
		 */
		if ((seconds = (row_end - row_start)) < 1)
			continue;

		/* this means we are a cluster registration
		   entry */
		if (!row[EVENT_REQ_NAME][0]) {
			local_cluster_usage_t *loc_c_usage;

			/* if the cpu count changes we will
			 * only care about the last cpu count but
			 * we will keep a total of the time for
			 * all cpus to get the correct cpu time
			 * for the entire period.
			 */

			if (state || !c_usage) {
				loc_c_usage = xmalloc(
					sizeof(local_cluster_usage_t));
				loc_c_usage->start = row_start;
				loc_c_usage->loc_tres =
					list_create(_destroy_local_tres_usage);
				/* If this has a state it
				   means the slurmctld went
				   down and we should put this
				   on the list and remove any
				   jobs from this time that
				   were running later.
				*/
				if (state)
					list_append(cluster_down_list,
						    loc_c_usage);
				else
					c_usage = loc_c_usage;
			} else
				loc_c_usage = c_usage;

			loc_c_usage->end = row_end;

			_add_tres_2_list(loc_c_usage->loc_tres,
					 row[EVENT_REQ_TRES], seconds);

			continue;
		}

		/* only record down time for the cluster we
		   are looking for.  If it was during this
		   time period we would already have it.
		*/
		if (c_usage) {
			time_t local_start = row_start;
			time_t local_end = row_end;
			int seconds;
			if (c_usage->start > local_start)
				local_start = c_usage->start;
			if (c_usage->end < local_end)
				local_end = c_usage->end;
			seconds = (local_end - local_start);
			if (seconds > 0) {
				_add_tres_time_2_list(c_usage->loc_tres,
						      row[EVENT_REQ_TRES],
						      TIME_DOWN,
						      seconds, 0, 0);

				/* Now remove this time if there was a
				   disconnected slurmctld during the
				   down time.
				*/
				list_iterator_reset(d_itr);
				while ((loc_c_usage = list_next(d_itr))) {
					int temp_end = row_end;
					int temp_start = row_start;
					if (loc_c_usage->start > local_start)
						temp_start = loc_c_usage->start;
					if (loc_c_usage->end < temp_end)
						temp_end = loc_c_usage->end;
					seconds = (temp_end - temp_start);
					if (seconds < 1)
						continue;

					_remove_job_tres_time_from_cluster(
						loc_c_usage->loc_tres,
						c_usage->loc_tres, seconds);
					/* info("Node %s was down for " */
					/*      "%d seconds while " */
					/*      "cluster %s's slurmctld " */
					/*      "wasn't responding", */
					/*      row[EVENT_REQ_NAME], */
					/*      seconds, cluster_name); */
				}
			}
		}
	}
	mysql_free_result(result);

	list_iterator_destroy(d_itr);

	return c_usage;
}

extern int as_mysql_hourly_rollup(mysql_conn_t *mysql_conn,
				  char *cluster_name,
				  time_t start, time_t end,
				  uint16_t archive_data)
{
	int rc = SLURM_SUCCESS;
	int add_sec = 3600;
	int i=0;
	time_t now = time(NULL);
	time_t curr_start = start;
	time_t curr_end = curr_start + add_sec;
	char *query = NULL;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	ListIterator a_itr = NULL;
	ListIterator c_itr = NULL;
	ListIterator w_itr = NULL;
	ListIterator r_itr = NULL;
	List assoc_usage_list = list_create(_destroy_local_id_usage);
	List cluster_down_list = list_create(_destroy_local_cluster_usage);
	List wckey_usage_list = list_create(_destroy_local_id_usage);
	List resv_usage_list = list_create(_destroy_local_resv_usage);
	uint16_t track_wckey = slurm_get_track_wckey();
	local_cluster_usage_t *loc_c_usage = NULL;
	local_cluster_usage_t *c_usage = NULL;
	local_resv_usage_t *r_usage = NULL;
	local_id_usage_t *a_usage = NULL;
	local_id_usage_t *w_usage = NULL;
	/* char start_char[20], end_char[20]; */

	char *job_req_inx[] = {
		"job.job_db_inx",
//		"job.id_job",
		"job.id_assoc",
		"job.id_wckey",
		"job.array_task_pending",
		"job.time_eligible",
		"job.time_start",
		"job.time_end",
		"job.time_suspended",
		"job.cpus_req",
		"job.id_resv",
		"job.tres_alloc"
	};
	char *job_str = NULL;
	enum {
		JOB_REQ_DB_INX,
//		JOB_REQ_JOBID,
		JOB_REQ_ASSOCID,
		JOB_REQ_WCKEYID,
		JOB_REQ_ARRAY_PENDING,
		JOB_REQ_ELG,
		JOB_REQ_START,
		JOB_REQ_END,
		JOB_REQ_SUSPENDED,
		JOB_REQ_RCPU,
		JOB_REQ_RESVID,
		JOB_REQ_TRES,
		JOB_REQ_COUNT
	};

	char *suspend_req_inx[] = {
		"time_start",
		"time_end"
	};
	char *suspend_str = NULL;
	enum {
		SUSPEND_REQ_START,
		SUSPEND_REQ_END,
		SUSPEND_REQ_COUNT
	};

	char *resv_req_inx[] = {
		"id_resv",
		"assoclist",
		"flags",
		"tres",
		"time_start",
		"time_end",
		"unused_wall"
	};
	char *resv_str = NULL;
	enum {
		RESV_REQ_ID,
		RESV_REQ_ASSOCS,
		RESV_REQ_FLAGS,
		RESV_REQ_TRES,
		RESV_REQ_START,
		RESV_REQ_END,
		RESV_REQ_UNUSED,
		RESV_REQ_COUNT
	};

	i=0;
	xstrfmtcat(job_str, "%s", job_req_inx[i]);
	for(i=1; i<JOB_REQ_COUNT; i++) {
		xstrfmtcat(job_str, ", %s", job_req_inx[i]);
	}

	i=0;
	xstrfmtcat(suspend_str, "%s", suspend_req_inx[i]);
	for(i=1; i<SUSPEND_REQ_COUNT; i++) {
		xstrfmtcat(suspend_str, ", %s", suspend_req_inx[i]);
	}

	i=0;
	xstrfmtcat(resv_str, "%s", resv_req_inx[i]);
	for(i=1; i<RESV_REQ_COUNT; i++) {
		xstrfmtcat(resv_str, ", %s", resv_req_inx[i]);
	}

/* 	info("begin start %s", slurm_ctime2(&curr_start)); */
/* 	info("begin end %s", slurm_ctime2(&curr_end)); */
	a_itr = list_iterator_create(assoc_usage_list);
	c_itr = list_iterator_create(cluster_down_list);
	w_itr = list_iterator_create(wckey_usage_list);
	r_itr = list_iterator_create(resv_usage_list);
	while (curr_start < end) {
		int last_id = -1;
		int last_wckeyid = -1;

		if (debug_flags & DEBUG_FLAG_DB_USAGE)
			DB_DEBUG(mysql_conn->conn,
				 "%s curr hour is now %ld-%ld",
				 cluster_name, curr_start, curr_end);
/* 		info("start %s", slurm_ctime2(&curr_start)); */
/* 		info("end %s", slurm_ctime2(&curr_end)); */

		c_usage = _setup_cluster_usage(mysql_conn, cluster_name,
					       curr_start, curr_end,
					       cluster_down_list);

		// now get the reservations during this time
		query = xstrdup_printf("select %s from \"%s_%s\" where "
				       "(time_start < %ld && time_end >= %ld) "
				       "order by time_start",
				       resv_str, cluster_name, resv_table,
				       curr_end, curr_start);

		if (debug_flags & DEBUG_FLAG_DB_USAGE)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);
		if (!(result = mysql_db_query_ret(
			      mysql_conn, query, 0))) {
			rc = SLURM_ERROR;
			goto end_it;
		}
		xfree(query);

		if (c_usage)
			xassert(c_usage->loc_tres);

		/* If a reservation overlaps another reservation we
		   total up everything here as if they didn't but when
		   calculating the total time for a cluster we will
		   remove the extra time received.  This may result in
		   unexpected results with association based reports
		   since the association is given the total amount of
		   time of each reservation, thus equaling more time
		   than is available.  Job/Cluster/Reservation reports
		   should be fine though since we really don't over
		   allocate resources.  The issue with us not being
		   able to handle overlapping reservations here is
		   unless the reservation completely overlaps the
		   other reservation we have no idea how many cpus
		   should be removed since this could be a
		   heterogeneous system.  This same problem exists
		   when a reservation is created with the ignore_jobs
		   option which will allow jobs to continue to run in the
		   reservation that aren't suppose to.
		*/
		while ((row = mysql_fetch_row(result))) {
			time_t row_start = slurm_atoul(row[RESV_REQ_START]);
			time_t row_end = slurm_atoul(row[RESV_REQ_END]);
			uint32_t row_flags = slurm_atoul(row[RESV_REQ_FLAGS]);
			int unused;
			int resv_seconds;
			time_t orig_start = row_start;

			if (row_start >= curr_start) {
				/*
				 * This is the first time we are seeing this
				 * reservation, so set our unused to be 0.
				 * This is mostly helpful when
				 * rerolling set it back to 0.
				 */
				unused = 0;
			} else
				unused = slurm_atoul(row[RESV_REQ_UNUSED]);

			if (row_start <= curr_start)
				row_start = curr_start;

			if (!row_end || row_end > curr_end)
				row_end = curr_end;

			/* Don't worry about it if the time is less
			 * than 1 second.
			 */
			if ((resv_seconds = (row_end - row_start)) < 1)
				continue;

			r_usage = xmalloc(sizeof(local_resv_usage_t));
			r_usage->id = slurm_atoul(row[RESV_REQ_ID]);

			r_usage->local_assocs = list_create(slurm_destroy_char);
			slurm_addto_char_list(r_usage->local_assocs,
					      row[RESV_REQ_ASSOCS]);
			r_usage->loc_tres =
				list_create(_destroy_local_tres_usage);

			_add_tres_2_list(r_usage->loc_tres,
					 row[RESV_REQ_TRES], resv_seconds);

			/*
			 * Original start is needed when updating the
			 * reservation's unused_wall later on.
			 */
			r_usage->orig_start = orig_start;
			r_usage->start = row_start;
			r_usage->end = row_end;
			r_usage->unused_wall = unused + resv_seconds;
			list_append(resv_usage_list, r_usage);

			/* Since this reservation was added to the
			   cluster and only certain people could run
			   there we will use this as allocated time on
			   the system.  If the reservation was a
			   maintenance then we add the time to planned
			   down time.
			*/


			/*
			 * Only record time for the clusters that have
			 * registered, or if a reservation has the IGNORE_JOBS
			 * flag we don't have an easy way to distinguish the
			 * cpus a job not running in the reservation, but on
			 * it's cpus.
			 * We still need them for figuring out unused wall time,
			 * but for cluster utilization we will just ignore them.
			 */
			if (!c_usage || (row_flags & RESERVE_FLAG_IGN_JOBS))
				continue;

			_add_time_tres_list(c_usage->loc_tres,
					    r_usage->loc_tres,
					    (row_flags & RESERVE_FLAG_MAINT) ?
					    TIME_PDOWN : TIME_ALLOC, 0, 0);

			/* slurm_make_time_str(&r_usage->start, start_char, */
			/* 		    sizeof(start_char)); */
			/* slurm_make_time_str(&r_usage->end, end_char, */
			/* 		    sizeof(end_char)); */
			/* info("adding this much %lld to cluster %s " */
			/*      "%d %d %s - %s", */
			/*      r_usage->total_time, c_usage->name, */
			/*      (row_flags & RESERVE_FLAG_MAINT),  */
			/*      r_usage->id, start_char, end_char); */
		}
		mysql_free_result(result);

		/* now get the jobs during this time only  */
		query = xstrdup_printf("select %s from \"%s_%s\" as job "
				       "where (job.time_eligible && "
				       "job.time_eligible < %ld && "
				       "(job.time_end >= %ld || "
				       "job.time_end = 0)) "
				       "group by job.job_db_inx "
				       "order by job.id_assoc, "
				       "job.time_eligible",
				       job_str, cluster_name, job_table,
				       curr_end, curr_start);

		if (debug_flags & DEBUG_FLAG_DB_USAGE)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);
		if (!(result = mysql_db_query_ret(
			      mysql_conn, query, 0))) {
			rc = SLURM_ERROR;
			goto end_it;
		}
		xfree(query);

		while ((row = mysql_fetch_row(result))) {
			//uint32_t job_id = slurm_atoul(row[JOB_REQ_JOBID]);
			uint32_t assoc_id = slurm_atoul(row[JOB_REQ_ASSOCID]);
			uint32_t wckey_id = slurm_atoul(row[JOB_REQ_WCKEYID]);
			uint32_t array_pending =
				slurm_atoul(row[JOB_REQ_ARRAY_PENDING]);
			uint32_t resv_id = slurm_atoul(row[JOB_REQ_RESVID]);
			time_t row_eligible = slurm_atoul(row[JOB_REQ_ELG]);
			time_t row_start = slurm_atoul(row[JOB_REQ_START]);
			time_t row_end = slurm_atoul(row[JOB_REQ_END]);
			uint32_t row_rcpu = slurm_atoul(row[JOB_REQ_RCPU]);
			List loc_tres = NULL;
			int loc_seconds = 0;
			int seconds = 0, suspend_seconds = 0;

			if (row_start && (row_start < curr_start))
				row_start = curr_start;

			if (!row_start && row_end)
				row_start = row_end;

			if (!row_end || row_end > curr_end)
				row_end = curr_end;

			if (!row_start || ((row_end - row_start) < 1))
				goto calc_cluster;

			seconds = (row_end - row_start);

			if (slurm_atoul(row[JOB_REQ_SUSPENDED])) {
				MYSQL_RES *result2 = NULL;
				MYSQL_ROW row2;
				/* get the suspended time for this job */
				query = xstrdup_printf(
					"select %s from \"%s_%s\" where "
					"(time_start < %ld && (time_end >= %ld "
					"|| time_end = 0)) && job_db_inx=%s "
					"order by time_start",
					suspend_str, cluster_name,
					suspend_table,
					curr_end, curr_start,
					row[JOB_REQ_DB_INX]);

				debug4("%d(%s:%d) query\n%s",
				       mysql_conn->conn, THIS_FILE,
				       __LINE__, query);
				if (!(result2 = mysql_db_query_ret(
					      mysql_conn,
					      query, 0))) {
					rc = SLURM_ERROR;
					mysql_free_result(result);
					goto end_it;
				}
				xfree(query);
				while ((row2 = mysql_fetch_row(result2))) {
					int tot_time = 0;
					time_t local_start = slurm_atoul(
						row2[SUSPEND_REQ_START]);
					time_t local_end = slurm_atoul(
						row2[SUSPEND_REQ_END]);

					if (!local_start)
						continue;

					if (row_start > local_start)
						local_start = row_start;
					if (!local_end || row_end < local_end)
						local_end = row_end;
					tot_time = (local_end - local_start);

					if (tot_time > 0)
						suspend_seconds += tot_time;
				}
				mysql_free_result(result2);
			}

			if (last_id != assoc_id) {
				a_usage = xmalloc(sizeof(local_id_usage_t));
				a_usage->id = assoc_id;
				list_append(assoc_usage_list, a_usage);
				last_id = assoc_id;
				/* a_usage->loc_tres is made later,
				   don't do it here.
				*/
			}

			/* Short circuit this so so we don't get a pointer. */
			if (!track_wckey)
				last_wckeyid = wckey_id;

			/* do the wckey calculation */
			if (last_wckeyid != wckey_id) {
				list_iterator_reset(w_itr);
				while ((w_usage = list_next(w_itr)))
					if (w_usage->id == wckey_id)
						break;

				if (!w_usage) {
					w_usage = xmalloc(
						sizeof(local_id_usage_t));
					w_usage->id = wckey_id;
					list_append(wckey_usage_list,
						    w_usage);
					w_usage->loc_tres = list_create(
						_destroy_local_tres_usage);
				}
				last_wckeyid = wckey_id;
			}

			/* do the cluster allocated calculation */
		calc_cluster:

			/*
			 * We need to have this clean for each job
			 * since we add the time to the cluster individually.
			 */
			loc_tres = list_create(_destroy_local_tres_usage);

			_add_tres_time_2_list(loc_tres, row[JOB_REQ_TRES],
					      TIME_ALLOC, seconds,
					      suspend_seconds, 0);
			if (w_usage)
				_add_tres_time_2_list(w_usage->loc_tres,
						      row[JOB_REQ_TRES],
						      TIME_ALLOC, seconds,
						      suspend_seconds, 0);

			/*
			 * Now figure out there was a disconnected
			 * slurmctld durning this job.
			 */
			list_iterator_reset(c_itr);
			while ((loc_c_usage = list_next(c_itr))) {
				int temp_end = row_end;
				int temp_start = row_start;
				if (loc_c_usage->start > temp_start)
					temp_start = loc_c_usage->start;
				if (loc_c_usage->end < temp_end)
					temp_end = loc_c_usage->end;
				loc_seconds = (temp_end - temp_start);
				if (loc_seconds < 1)
					continue;

				_remove_job_tres_time_from_cluster(
					loc_c_usage->loc_tres,
					loc_tres,
					loc_seconds);
				/* info("Job %u was running for " */
				/*      "%d seconds while " */
				/*      "cluster %s's slurmctld " */
				/*      "wasn't responding", */
				/*      job_id, loc_seconds, cluster_name); */
			}

			/* first figure out the reservation */
			if (resv_id) {
				if (seconds <= 0) {
					_transfer_loc_tres(&loc_tres, a_usage);
					continue;
				}
				/*
				 * Since we have already added the entire
				 * reservation as used time on the cluster we
				 * only need to calculate the used time for the
				 * reservation and then divy up the unused time
				 * over the associations able to run in the
				 * reservation. Since the job was to run, or ran
				 * a reservation we don't care about eligible
				 * time since that could totally skew the
				 * clusters reserved time since the job may be
				 * able to run outside of the reservation.
				 */
				list_iterator_reset(r_itr);
				while ((r_usage = list_next(r_itr))) {
					int temp_end, temp_start;
					/*
					 * since the reservation could have
					 * changed in some way, thus making a
					 * new reservation record in the
					 * database, we have to make sure all
					 * of the reservations are checked to
					 * see if such a thing has happened
					 */
					if (r_usage->id != resv_id)
						continue;
					temp_end = row_end;
					temp_start = row_start;
					if (r_usage->start > temp_start)
						temp_start =
							r_usage->start;
					if (r_usage->end < temp_end)
						temp_end = r_usage->end;

					loc_seconds = (temp_end - temp_start);

					if (loc_seconds > 0) {
						_add_time_tres_list(
							r_usage->loc_tres,
							loc_tres, TIME_ALLOC,
							loc_seconds, 1);
						if ((rc = _update_unused_wall(
							     r_usage,
							     loc_tres,
							     loc_seconds))
						    != SLURM_SUCCESS)
							goto end_it;
					}
				}

				_transfer_loc_tres(&loc_tres, a_usage);
				continue;
			}

			/*
			 * only record time for the clusters that have
			 * registered.  This continue should rarely if
			 * ever happen.
			 */
			if (!c_usage) {
				_transfer_loc_tres(&loc_tres, a_usage);
				continue;
			}

			if (row_start && (seconds > 0)) {
				/* info("%d assoc %d adds " */
				/*      "(%d)(%d-%d) * %d = %d " */
				/*      "to %d", */
				/*      job_id, */
				/*      a_usage->id, */
				/*      seconds, */
				/*      row_end, row_start, */
				/*      row_acpu, */
				/*      seconds * row_acpu, */
				/*      row_acpu); */

				_add_job_alloc_time_to_cluster(
					c_usage->loc_tres,
					loc_tres);
			}

			/*
			 * The loc_tres isn't needed after this so transfer to
			 * the association and go on our merry way.
			 */
			_transfer_loc_tres(&loc_tres, a_usage);

			/* now reserved time */
			if (!row_start || (row_start >= c_usage->start)) {
				int temp_end = row_start;
				int temp_start = row_eligible;
				if (c_usage->start > temp_start)
					temp_start = c_usage->start;
				if (c_usage->end < temp_end)
					temp_end = c_usage->end;
				loc_seconds = (temp_end - temp_start);
				if (loc_seconds > 0) {
					/*
					 * If we have pending jobs in an array
					 * they haven't been inserted into the
					 * database yet as proper job records,
					 * so handle them here.
					 */
					if (array_pending)
						loc_seconds *= array_pending;

					/* info("%d assoc %d reserved " */
					/*      "(%d)(%d-%d) * %d * %d = %d " */
					/*      "to %d", */
					/*      job_id, */
					/*      assoc_id, */
					/*      temp_end - temp_start, */
					/*      temp_end, temp_start, */
					/*      row_rcpu, */
					/*      array_pending, */
					/*      loc_seconds, */
					/*      row_rcpu); */

					_add_time_tres(c_usage->loc_tres,
						       TIME_RESV, TRES_CPU,
						       loc_seconds *
						       (uint64_t) row_rcpu,
						       0);
				}
			}
		}
		mysql_free_result(result);

		/* now figure out how much more to add to the
		   associations that could had run in the reservation
		*/
		query = NULL;
		list_iterator_reset(r_itr);
		while ((r_usage = list_next(r_itr))) {
			ListIterator t_itr;
			local_tres_usage_t *loc_tres;

			xstrfmtcat(query, "update \"%s_%s\" set unused_wall=%f where id_resv=%u and time_start=%ld;",
				   cluster_name, resv_table,
				   r_usage->unused_wall, r_usage->id,
				   r_usage->orig_start);

			if (!r_usage->loc_tres ||
			    !list_count(r_usage->loc_tres))
				continue;

			t_itr = list_iterator_create(r_usage->loc_tres);
			while ((loc_tres = list_next(t_itr))) {
				int64_t idle = loc_tres->total_time -
					loc_tres->time_alloc;
				char *assoc = NULL;
				ListIterator tmp_itr = NULL;
				int assoc_cnt, resv_unused_secs;

				if (idle <= 0)
					break; /* since this will be
						* the same for all TRES	*/

				/* now divide that time by the number of
				   associations in the reservation and add
				   them to each association */
				resv_unused_secs = idle;
				assoc_cnt = list_count(r_usage->local_assocs);
				if (assoc_cnt)
					resv_unused_secs /= assoc_cnt;
				/* info("resv %d got %d seconds for TRES %u " */
				/*      "for %d assocs", */
				/*      r_usage->id, resv_unused_secs, */
				/*      loc_tres->id, */
				/*      list_count(r_usage->local_assocs)); */
				tmp_itr = list_iterator_create(
					r_usage->local_assocs);
				while ((assoc = list_next(tmp_itr))) {
					uint32_t associd = slurm_atoul(assoc);
					if ((last_id != associd) &&
					    !(a_usage = list_find_first(
						      assoc_usage_list,
						      _find_id_usage,
						      &associd))) {
						a_usage = xmalloc(
							sizeof(local_id_usage_t));
						a_usage->id = associd;
						list_append(assoc_usage_list,
							    a_usage);
						last_id = associd;
						a_usage->loc_tres = list_create(
							_destroy_local_tres_usage);
					}

					_add_time_tres(a_usage->loc_tres,
						       TIME_ALLOC, loc_tres->id,
						       resv_unused_secs, 0);
				}
				list_iterator_destroy(tmp_itr);
			}
			list_iterator_destroy(t_itr);
		}

		if (query) {
			if (debug_flags & DEBUG_FLAG_DB_USAGE)
				DB_DEBUG(mysql_conn->conn, "query\n%s", query);
			rc = mysql_db_query(mysql_conn, query);
			xfree(query);
			if (rc != SLURM_SUCCESS) {
				error("couldn't update reservations with unused time");
				goto end_it;
			}
		}

		/* now apply the down time from the slurmctld disconnects */
		if (c_usage) {
			list_iterator_reset(c_itr);
			while ((loc_c_usage = list_next(c_itr))) {
				local_tres_usage_t *loc_tres;
				ListIterator tmp_itr = list_iterator_create(
					loc_c_usage->loc_tres);
				while ((loc_tres = list_next(tmp_itr)))
					_add_time_tres(c_usage->loc_tres,
						       TIME_DOWN,
						       loc_tres->id,
						       loc_tres->total_time,
						       0);
				list_iterator_destroy(tmp_itr);
			}

			if ((rc = _process_cluster_usage(
				     mysql_conn, cluster_name, curr_start,
				     curr_end, now, c_usage))
			    != SLURM_SUCCESS) {
				goto end_it;
			}
		}

		list_iterator_reset(a_itr);
		while ((a_usage = list_next(a_itr)))
			_create_id_usage_insert(cluster_name, ASSOC_TABLES,
						curr_start, now,
						a_usage, &query);
		if (query) {
			if (debug_flags & DEBUG_FLAG_DB_USAGE)
				DB_DEBUG(mysql_conn->conn, "query\n%s", query);
			rc = mysql_db_query(mysql_conn, query);
			xfree(query);
			if (rc != SLURM_SUCCESS) {
				error("Couldn't add assoc hour rollup");
				goto end_it;
			}
		}

		if (!track_wckey)
			goto end_loop;

		list_iterator_reset(w_itr);
		while ((w_usage = list_next(w_itr)))
			_create_id_usage_insert(cluster_name, WCKEY_TABLES,
						curr_start, now,
						w_usage, &query);
		if (query) {
			if (debug_flags & DEBUG_FLAG_DB_USAGE)
				DB_DEBUG(mysql_conn->conn, "query\n%s", query);
			rc = mysql_db_query(mysql_conn, query);
			xfree(query);
			if (rc != SLURM_SUCCESS) {
				error("Couldn't add wckey hour rollup");
				goto end_it;
			}
		}

	end_loop:
		_destroy_local_cluster_usage(c_usage);

		c_usage     = NULL;
		r_usage     = NULL;
		a_usage     = NULL;
		w_usage     = NULL;

		list_flush(assoc_usage_list);
		list_flush(cluster_down_list);
		list_flush(wckey_usage_list);
		list_flush(resv_usage_list);
		curr_start = curr_end;
		curr_end = curr_start + add_sec;
	}
end_it:
	xfree(query);
	xfree(suspend_str);
	xfree(job_str);
	xfree(resv_str);
	_destroy_local_cluster_usage(c_usage);

	if (a_itr)
		list_iterator_destroy(a_itr);
	if (c_itr)
		list_iterator_destroy(c_itr);
	if (w_itr)
		list_iterator_destroy(w_itr);
	if (r_itr)
		list_iterator_destroy(r_itr);

	FREE_NULL_LIST(assoc_usage_list);
	FREE_NULL_LIST(cluster_down_list);
	FREE_NULL_LIST(wckey_usage_list);
	FREE_NULL_LIST(resv_usage_list);

/* 	info("stop start %s", slurm_ctime2(&curr_start)); */
/* 	info("stop end %s", slurm_ctime2(&curr_end)); */

	/* go check to see if we archive and purge */

	if (rc == SLURM_SUCCESS) {
		if (mysql_db_commit(mysql_conn)) {
			char start[25], end[25];
			error("Couldn't commit cluster (%s) "
			      "hour rollup for %s - %s",
			      cluster_name, slurm_ctime2_r(&curr_start, start),
			      slurm_ctime2_r(&curr_end, end));
			rc = SLURM_ERROR;
		} else
			rc = _process_purge(mysql_conn, cluster_name,
					    archive_data, SLURMDB_PURGE_HOURS);
	}

	return rc;
}
extern int as_mysql_nonhour_rollup(mysql_conn_t *mysql_conn,
				   bool run_month,
				   char *cluster_name,
				   time_t start, time_t end,
				   uint16_t archive_data)
{
	/* can't just add 86400 since daylight savings starts and ends every
	 * once in a while
	 */
	int rc = SLURM_SUCCESS;
	struct tm start_tm;
	time_t curr_start = start;
	time_t curr_end;
	time_t now = time(NULL);
	char *query = NULL;
	uint16_t track_wckey = slurm_get_track_wckey();
	char *unit_name;

	while (curr_start < end) {
		if (!slurm_localtime_r(&curr_start, &start_tm)) {
			error("Couldn't get localtime from start %ld",
			      curr_start);
			return SLURM_ERROR;
		}
		start_tm.tm_sec = 0;
		start_tm.tm_min = 0;
		start_tm.tm_hour = 0;
		start_tm.tm_isdst = -1;

		if (run_month) {
			unit_name = "month";
			start_tm.tm_mday = 1;
			start_tm.tm_mon++;
		} else {
			unit_name = "day";
			start_tm.tm_mday++;
		}

		curr_end = slurm_mktime(&start_tm);

		if (debug_flags & DEBUG_FLAG_DB_USAGE)
			DB_DEBUG(mysql_conn->conn,
				 "curr %s is now %ld-%ld",
				 unit_name, curr_start, curr_end);
/* 		info("start %s", slurm_ctime2(&curr_start)); */
/* 		info("end %s", slurm_ctime2(&curr_end)); */
		query = xstrdup_printf(
			"insert into \"%s_%s\" (creation_time, mod_time, id, "
			"id_tres, time_start, alloc_secs) "
			"select %ld, %ld, id, id_tres, "
			"%ld, @ASUM:=SUM(alloc_secs) from \"%s_%s\" where "
			"(time_start < %ld && time_start >= %ld) "
			"group by id, id_tres on duplicate key update "
			"mod_time=%ld, alloc_secs=@ASUM;",
			cluster_name,
			run_month ? assoc_month_table : assoc_day_table,
			now, now, curr_start,
			cluster_name,
			run_month ? assoc_day_table : assoc_hour_table,
			curr_end, curr_start, now);

		/* We group on deleted here so if there are no entries
		   we don't get an error, just nothing is returned.
		   Else we get a bunch of NULL's
		*/
		xstrfmtcat(query,
			   "insert into \"%s_%s\" (creation_time, "
			   "mod_time, time_start, id_tres, count, "
			   "alloc_secs, down_secs, pdown_secs, "
			   "idle_secs, over_secs, resv_secs) "
			   "select %ld, %ld, "
			   "%ld, id_tres, @CPU:=MAX(count), "
			   "@ASUM:=SUM(alloc_secs), "
			   "@DSUM:=SUM(down_secs), "
			   "@PDSUM:=SUM(pdown_secs), "
			   "@ISUM:=SUM(idle_secs), "
			   "@OSUM:=SUM(over_secs), "
			   "@RSUM:=SUM(resv_secs) from \"%s_%s\" where "
			   "(time_start < %ld && time_start >= %ld) "
			   "group by deleted, id_tres "
			   "on duplicate key update "
			   "mod_time=%ld, count=@CPU, "
			   "alloc_secs=@ASUM, down_secs=@DSUM, "
			   "pdown_secs=@PDSUM, idle_secs=@ISUM, "
			   "over_secs=@OSUM, resv_secs=@RSUM;",
			   cluster_name,
			   run_month ? cluster_month_table : cluster_day_table,
			   now, now, curr_start,
			   cluster_name,
			   run_month ? cluster_day_table : cluster_hour_table,
			   curr_end, curr_start, now);
		if (track_wckey) {
			xstrfmtcat(query,
				   "insert into \"%s_%s\" (creation_time, "
				   "mod_time, id, id_tres, time_start, "
				   "alloc_secs) "
				   "select %ld, %ld, "
				   "id, id_tres, %ld, @ASUM:=SUM(alloc_secs) "
				   "from \"%s_%s\" where (time_start < %ld && "
				   "time_start >= %ld) group by id, id_tres "
				   "on duplicate key update "
				   "mod_time=%ld, alloc_secs=@ASUM;",
				   cluster_name,
				   run_month ? wckey_month_table :
				   wckey_day_table,
				   now, now, curr_start,
				   cluster_name,
				   run_month ? wckey_day_table :
				   wckey_hour_table,
				   curr_end, curr_start, now);
		}
		if (debug_flags & DEBUG_FLAG_DB_USAGE)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);
		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
		if (rc != SLURM_SUCCESS) {
			error("Couldn't add %s rollup", unit_name);
			return SLURM_ERROR;
		}

		curr_start = curr_end;
	}

/* 	info("stop start %s", slurm_ctime2(&curr_start)); */
/* 	info("stop end %s", slurm_ctime2(&curr_end)); */

	/* go check to see if we archive and purge */
	rc = _process_purge(mysql_conn, cluster_name, archive_data,
			    run_month ? SLURMDB_PURGE_MONTHS :
			    SLURMDB_PURGE_DAYS);
	return rc;
}
