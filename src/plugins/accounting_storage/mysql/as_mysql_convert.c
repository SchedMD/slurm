/*****************************************************************************\
 *  as_mysql_convert.c - functions dealing with converting from tables in
 *                    slurm <= 17.02.
 *****************************************************************************
 *
 *  Copyright (C) 2015 SchedMD LLC.
 *  Written by Danny Auble <da@schedmd.com>
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

#include "as_mysql_convert.h"

/* Any time you have to add to an existing convert update this number. */
#define CONVERT_VERSION 1

static int _convert_job_table(mysql_conn_t *mysql_conn, char *cluster_name)
{
	int rc = SLURM_SUCCESS;
	char *query = xstrdup_printf("update \"%s_%s\" as job "
				     "left outer join ( select job_db_inx, "
				     "SUM(consumed_energy) 'sum_energy' "
				     "from \"%s_%s\" where id_step >= 0 "
				     "and consumed_energy != %"PRIu64
				     " group by job_db_inx ) step on "
				     "job.job_db_inx=step.job_db_inx "
				     "set job.tres_alloc=concat("
				     "job.tres_alloc, concat(',%d=', "
				     "case when step.sum_energy then "
				     "step.sum_energy else %"PRIu64" END)) "
				     "where job.tres_alloc != '' && "
				     "job.tres_alloc not like '%%,%d=%%';",
				     cluster_name, job_table,
				     cluster_name, step_table,
				     NO_VAL64, TRES_ENERGY, NO_VAL64,
				     TRES_ENERGY);

	debug4("(%s:%d) query\n%s", THIS_FILE, __LINE__, query);
	if ((rc = mysql_db_query(mysql_conn, query)) != SLURM_SUCCESS)
		error("Can't convert %s_%s info: %m",
		      cluster_name, job_table);
	xfree(query);

	return rc;
}

static int _convert_step_table(mysql_conn_t *mysql_conn, char *cluster_name)
{
	int rc = SLURM_SUCCESS;
	char *query = xstrdup_printf("update \"%s_%s\" set consumed_energy=%"
				     PRIu64" where consumed_energy=%u;",
				     cluster_name, step_table,
				     NO_VAL64, NO_VAL);

	debug4("(%s:%d) query\n%s", THIS_FILE, __LINE__, query);
	if ((rc = mysql_db_query(mysql_conn, query)) != SLURM_SUCCESS)
		error("Can't convert %s_%s info: %m",
		      cluster_name, step_table);
	xfree(query);

	return rc;
}

extern int as_mysql_convert_tables(mysql_conn_t *mysql_conn)
{
	char *query;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int i = 0, rc = SLURM_SUCCESS;
	ListIterator itr;
	char *cluster_name;
	uint32_t curr_ver = 0;

	xassert(as_mysql_total_cluster_list);

	/* no valid clusters, just return */
	if (!(cluster_name = list_peek(as_mysql_total_cluster_list)))
		return SLURM_SUCCESS;

	query = xstrdup_printf("select version from %s", convert_version_table);
	debug4("%d(%s:%d) query\n%s", mysql_conn->conn,
	       THIS_FILE, __LINE__, query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);
	row = mysql_fetch_row(result);
	if (row) {
		curr_ver = slurm_atoul(row[0]);
		mysql_free_result(result);
		if (curr_ver == CONVERT_VERSION) {
			debug4("No conversion needed, Horray!");
			return SLURM_SUCCESS;
		}
	} else {
		mysql_free_result(result);
		query = xstrdup_printf("insert into %s (version) values (0);",
				       convert_version_table);
		debug4("(%s:%d) query\n%s", THIS_FILE, __LINE__, query);
		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
		if (rc != SLURM_SUCCESS)
			return SLURM_ERROR;
	}

	/* make it up to date */
	itr = list_iterator_create(as_mysql_total_cluster_list);
	while ((cluster_name = list_next(itr))) {
		query = xstrdup_printf("select tres_alloc from \"%s_%s\" where "
				       "tres_alloc like '%%,%d=%%' limit 1;",
				       cluster_name, job_table, TRES_ENERGY);

		debug4("(%s:%d) query\n%s", THIS_FILE, __LINE__, query);
		if (!(result = mysql_db_query_ret(mysql_conn, query, 0))) {
			xfree(query);
			error("QUERY BAD: No count col name for cluster %s, this should never happen",
			      cluster_name);
			continue;
		}
		xfree(query);

		i = mysql_num_rows(result);
		mysql_free_result(result);
		result = NULL;
		if (i) {
			debug2("Conversion on cluster %s not needed",
			       cluster_name);
			continue;
		} else {
			/* Need to check if the database is empty */
			query = xstrdup_printf(
				"select tres_alloc from \"%s_%s\" limit 1;",
				cluster_name, job_table);
			debug4("(%s:%d) query\n%s", THIS_FILE, __LINE__, query);
			if (!(result = mysql_db_query_ret(
				      mysql_conn, query, 0))) {
				xfree(query);
				continue;
			}
			xfree(query);
			i = mysql_num_rows(result);
			mysql_free_result(result);
			result = NULL;

			if (!i) {
				debug2("Conversion on cluster %s not needed, it doesn't have any rows in the table",
				       cluster_name);
				continue;
			}
		}

		info("converting step table for %s", cluster_name);
		if ((rc = _convert_step_table(mysql_conn, cluster_name)
		     != SLURM_SUCCESS))
			break;

		/* Now convert the job tables */
		info("converting job table for %s", cluster_name);
		if ((rc = _convert_job_table(mysql_conn, cluster_name)
		     != SLURM_SUCCESS))
			break;
	}
	list_iterator_destroy(itr);

	if (rc == SLURM_SUCCESS) {
		info("Conversion done: success!");
		query = xstrdup_printf("update %s set version=%d, "
				       "mod_time=UNIX_TIMESTAMP()",
				       convert_version_table, CONVERT_VERSION);
		debug4("(%s:%d) query\n%s", THIS_FILE, __LINE__, query);
		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
	}

	return rc;
}
