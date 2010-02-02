/*****************************************************************************\
 *  usage.c - accounting interface to pgsql - cluster usage related functions.
 *
 *  $Id: usage.c 13061 2008-01-22 21:23:56Z da $
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

char *assoc_day_table = "assoc_day_usage_table";
char *assoc_hour_table = "assoc_hour_usage_table";
char *assoc_month_table = "assoc_month_usage_table";
static storage_field_t assoc_usage_table_fields[] = {
	{ "creation_time", "INTEGER NOT NULL" },
	{ "mod_time", "INTEGER DEFAULT 0 NOT NULL" },
	{ "deleted", "INTEGER DEFAULT 0" },
	{ "id", "INTEGER NOT NULL" },
	{ "period_start", "INTEGER NOT NULL" },
	{ "alloc_cpu_secs", "INTEGER DEFAULT 0" },
	{ NULL, NULL}
};
static char *assoc_usage_table_constraint = ", "
	"PRIMARY KEY (id, period_start) "
	")";

char *cluster_day_table = "cluster_day_usage_table";
char *cluster_hour_table = "cluster_hour_usage_table";
char *cluster_month_table = "cluster_month_usage_table";
static storage_field_t cluster_usage_table_fields[] = {
	{ "creation_time", "INTEGER NOT NULL" },
	{ "mod_time", "INTEGER DEFAULT 0 NOT NULL" },
	{ "deleted", "INTEGER DEFAULT 0" },
	{ "cluster", "TEXT NOT NULL" },
	{ "period_start", "INTEGER NOT NULL" },
	{ "cpu_count", "INTEGER DEFAULT 0" },
	{ "alloc_cpu_secs", "BIGINT DEFAULT 0" },
	{ "down_cpu_secs", "BIGINT DEFAULT 0" },
	{ "pdown_cpu_secs", "BIGINT DEFAULT 0" },
	{ "idle_cpu_secs", "BIGINT DEFAULT 0" },
	{ "resv_cpu_secs", "BIGINT DEFAULT 0" },
	{ "over_cpu_secs", "BIGINT DEFAULT 0" },
	{ NULL, NULL}
};
static char *cluster_usage_table_constraint = ", "
	"PRIMARY KEY (cluster, period_start) "
	")";

char *wckey_day_table = "wckey_day_usage_table";
char *wckey_hour_table = "wckey_hour_usage_table";
char *wckey_month_table = "wckey_month_usage_table";
static storage_field_t wckey_usage_table_fields[] = {
	{ "creation_time", "INTEGER NOT NULL" },
	{ "mod_time", "INTEGER DEFAULT 0 NOT NULL" },
	{ "deleted", "INTEGER DEFAULT 0" },
	{ "id", "INTEGER NOT NULL" },
	{ "period_start", "INTEGER NOT NULL" },
	{ "alloc_cpu_secs", "BIGINT DEFAULT 0" },
	{ "resv_cpu_secs", "BIGINT DEFAULT 0" },
	{ "over_cpu_secs", "BIGINT DEFAULT 0" },
	{ NULL, NULL}
};
static char *wckey_usage_table_constraint = ", "
	"PRIMARY KEY (id, period_start) "
	")";

char *last_ran_table = "last_ran_table";
static storage_field_t last_ran_table_fields[] = {
	{ "hourly_rollup", "INTEGER DEFAULT 0 NOT NULL" },
	{ "daily_rollup", "INTEGER DEFAULT 0 NOT NULL" },
	{ "monthly_rollup", "INTEGER DEFAULT 0 NOT NULL" },
	{ NULL, NULL}
};
static char *last_ran_table_constraint = ")";

time_t global_last_rollup = 0;
pthread_mutex_t rollup_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * _create_function_add_cluster_hour_usage - create a PL/pgSQL function
 *   to add a single record of cluster hour usage data
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_add_cluster_hour_usage(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION add_cluster_hour_usage "
		"(rec %s) RETURNS VOID AS $$"
		"BEGIN LOOP "
		"  BEGIN "
		"    INSERT INTO %s VALUES (rec.*); RETURN; "
		"  EXCEPTION WHEN UNIQUE_VIOLATION THEN "
		"    UPDATE %s SET (deleted, mod_time, cpu_count, "
		"        alloc_cpu_secs, down_cpu_secs, pdown_cpu_secs, "
		"        idle_cpu_secs, over_cpu_secs, resv_cpu_secs) = "
		"        (0, rec.mod_time, rec.cpu_count, rec.alloc_cpu_secs,"
		"        rec.down_cpu_secs, rec.pdown_cpu_secs, "
		"        rec.idle_cpu_secs, rec.over_cpu_secs, "
		"        rec.resv_cpu_secs)"
		"      WHERE cluster=rec.cluster AND "
		"        period_start=rec.period_start;"
		"    IF FOUND THEN RETURN; END IF; "
		"  END;"
		"END LOOP; END; $$ LANGUAGE PLPGSQL;",
		cluster_hour_table, cluster_hour_table, cluster_hour_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _create_function_add_cluster_hour_usages - create a PL/pgSQL function
 *   to add records of cluster hour usage data
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_add_cluster_hour_usages(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION add_cluster_hour_usages "
		"(recs %s[]) RETURNS VOID AS $$"
		"DECLARE "
		"  i INTEGER := 1; rec %s; "
		"BEGIN LOOP "
		"  rec := recs[i]; i := i + 1; "
		"  EXIT WHEN rec IS NULL;"
		"  PERFORM add_cluster_hour_usage(rec);"
		"END LOOP; END; $$ LANGUAGE PLPGSQL;",
		cluster_hour_table, cluster_hour_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _create_function_add_cluster_day_usage - create a PL/pgSQL function
 *   to add cluster day usage data
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_add_cluster_day_usage(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION add_cluster_day_usage "
		"(rec %s) RETURNS VOID AS $$"
		"BEGIN LOOP "
		"  BEGIN "
		"    INSERT INTO %s VALUES (rec.*); RETURN;"
		"  EXCEPTION WHEN UNIQUE_VIOLATION THEN "
		"    UPDATE %s SET (deleted, mod_time, cpu_count, "
		"      alloc_cpu_secs, down_cpu_secs, pdown_cpu_secs, "
		"      idle_cpu_secs, over_cpu_secs, resv_cpu_secs) = "
		"      (0, rec.mod_time, rec.cpu_count, rec.alloc_cpu_secs,"
		"      rec.down_cpu_secs, rec.pdown_cpu_secs, "
		"      rec.idle_cpu_secs, rec.over_cpu_secs, "
		"      rec.resv_cpu_secs)"
		"    WHERE cluster=rec.cluster AND "
		"      period_start=rec.period_start;"
		"    IF FOUND THEN RETURN; END IF; "
		"  END;"
		"END LOOP; END; $$ LANGUAGE PLPGSQL;",
		cluster_day_table, cluster_day_table, cluster_day_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _create_function_add_cluster_month_usage - create a PL/pgSQL function
 *   to add cluster month usage data
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_add_cluster_month_usage(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION add_cluster_day_usage "
		"(rec %s) RETURNS VOID AS $$"
		"BEGIN LOOP "
		"  BEGIN "
		"    INSERT INTO %s VALUES (rec.*); RETURN;"
		"  EXCEPTION WHEN UNIQUE_VIOLATION THEN "
		"    UPDATE %s SET (deleted, mod_time, cpu_count, "
		"      alloc_cpu_secs, down_cpu_secs, pdown_cpu_secs, "
		"      idle_cpu_secs, over_cpu_secs, resv_cpu_secs) = "
		"      (0, rec.mod_time, rec.cpu_count, rec.alloc_cpu_secs,"
		"      rec.down_cpu_secs, rec.pdown_cpu_secs, "
		"      rec.idle_cpu_secs, rec.over_cpu_secs, "
		"      rec.resv_cpu_secs)"
		"    WHERE cluster=rec.cluster AND "
		"      period_start=rec.period_start;"
		"    IF FOUND THEN RETURN; END IF; "
		"  END;"
		"END LOOP; END; $$ LANGUAGE PLPGSQL;",
		cluster_month_table, cluster_month_table, cluster_month_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _create_function_cluster_daily_rollup - create a PL/pgSQL function
 *   to rollup cluster usage data daily
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_cluster_daily_rollup(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION cluster_daily_rollup "
		"(now INTEGER, start INTEGER, endtime INTEGER) "
		"RETURNS VOID AS $$"
		"DECLARE rec %s;"
		"BEGIN "
		"  FOR rec IN "
		"    SELECT now, now, 0, cluster, start, MAX(cpu_count), "
		"      SUM(alloc_cpu_secs), SUM(down_cpu_secs), "
		"      SUM(pdown_cpu_secs), SUM(idle_cpu_secs), "
		"      SUM(over_cpu_secs), SUM(resv_cpu_secs) FROM %s "
		"    WHERE period_start < endtime AND period_start > start "
		"    GROUP BY cluster"
		"  LOOP"
		"    PERFORM add_cluster_day_usage(rec);"
		"END LOOP; END; $$ LANGUAGE PLPGSQL;",
		cluster_day_table, cluster_hour_table);
	return create_function_xfree(db_conn, create_line);
}


/*
 * _create_function_cluster_monthly_rollup - create a PL/pgSQL function
 *   to rollup cluster usage data monthly
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_cluster_monthly_rollup(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION cluster_daily_rollup "
		"(now INTEGER, start INTEGER, endtime INTEGER) "
		"RETURNS VOID AS $$"
		"DECLARE rec %s;"
		"BEGIN "
		"  FOR rec IN "
		"    SELECT now, now, 0, cluster, start, MAX(cpu_count), "
		"      SUM(alloc_cpu_secs), SUM(down_cpu_secs), "
		"      SUM(pdown_cpu_secs), SUM(idle_cpu_secs), "
		"      SUM(over_cpu_secs), SUM(resv_cpu_secs) FROM %s "
		"    WHERE period_start < endtime AND period_start > start "
		"    GROUP BY cluster"
		"  LOOP"
		"    PERFORM add_cluster_day_usage(rec);"
		"END LOOP; END; $$ LANGUAGE PLPGSQL;",
		cluster_month_table, cluster_day_table);
	return create_function_xfree(db_conn, create_line);
}


/*
 * _create_function_add_assoc_hour_usage - create a PL/pgSQL function
 *   to add a single record of association hour usage data
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_add_assoc_hour_usage(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION add_assoc_hour_usage "
		"(rec %s) RETURNS VOID AS $$"
		"BEGIN LOOP "
		"  BEGIN "
		"    INSERT INTO %s VALUES (rec.*); RETURN;"
		"  EXCEPTION WHEN UNIQUE_VIOLATION THEN "
		"    UPDATE %s SET (deleted, mod_time, alloc_cpu_secs) = "
		"        (0, rec.mod_time, rec.alloc_cpu_secs)"
		"      WHERE id=rec.id AND "
		"        period_start=rec.period_start;"
		"    IF FOUND THEN RETURN; END IF; "
		"  END;"
		"END LOOP; END; $$ LANGUAGE PLPGSQL;",
		assoc_hour_table, assoc_hour_table, assoc_hour_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _create_function_add_assoc_hour_usages - create a PL/pgSQL function
 *   to add records of association hour usage data
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_add_assoc_hour_usages(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION add_assoc_hour_usages "
		"(recs %s[]) RETURNS VOID AS $$"
		"DECLARE "
		"  i INTEGER := 1; rec %s; "
		"BEGIN LOOP "
		"  rec := recs[i]; i := i + 1; "
		"  EXIT WHEN rec IS NULL;"
		"  PERFORM add_assoc_hour_usage(rec);"
		"END LOOP; END; $$ LANGUAGE PLPGSQL;",
		assoc_hour_table, assoc_hour_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _create_function_add_assoc_day_usage - create a PL/pgSQL function
 *   to add association day usage data
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_add_assoc_day_usage(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION add_assoc_day_usage "
		"(rec %s) RETURNS VOID AS $$"
		"BEGIN LOOP "
		"  BEGIN "
		"    INSERT INTO %s VALUES (rec.*); RETURN;"
		"  EXCEPTION WHEN UNIQUE_VIOLATION THEN "
		"    UPDATE %s SET (deleted, mod_time, alloc_cpu_secs) = "
		"      (0, rec.mod_time, rec.alloc_cpu_secs)"
		"    WHERE id=rec.id AND "
		"      period_start=rec.period_start;"
		"    IF FOUND THEN RETURN; END IF; "
		"  END; "
		"END LOOP; END; $$ LANGUAGE PLPGSQL;",
		assoc_day_table, assoc_day_table, assoc_day_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _create_function_add_assoc_month_usage - create a PL/pgSQL function
 *   to add association month usage data
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_add_assoc_month_usage(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION add_assoc_month_usage "
		"(rec %s) RETURNS VOID AS $$"
		"BEGIN LOOP "
		"  BEGIN "
		"    INSERT INTO %s VALUES (rec.*); RETURN;"
		"  EXCEPTION WHEN UNIQUE_VIOLATION THEN "
		"    UPDATE %s SET (deleted, mod_time, alloc_cpu_secs) = "
		"      (0, rec.mod_time, rec.alloc_cpu_secs)"
		"    WHERE id=rec.id AND "
		"      period_start=rec.period_start;"
		"    IF FOUND THEN RETURN; END IF; "
		"  END; "
		"END LOOP; END; $$ LANGUAGE PLPGSQL;",
		assoc_month_table, assoc_month_table, assoc_month_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _create_function_assoc_daily_rollup - create a PL/pgSQL function
 *   to rollup assoc usage data daily
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_assoc_daily_rollup(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION assoc_daily_rollup "
		"(now INTEGER, start INTEGER, endtime INTEGER) "
		"RETURNS VOID AS $$"
		"DECLARE rec %s;"
		"BEGIN "
		"  FOR rec IN "
		"    SELECT now, now, 0, id, start, SUM(alloc_cpu_secs)"
		"      FROM %s WHERE period_start < endtime AND "
		"      period_start > start GROUP BY id"
		"  LOOP"
		"    PERFORM add_assoc_day_usage(rec);"
		"END LOOP; END; $$ LANGUAGE PLPGSQL;",
		assoc_day_table, assoc_hour_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _create_function_assoc_monthly_rollup - create a PL/pgSQL function
 *   to rollup assoc usage data monthly
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_assoc_monthly_rollup(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION assoc_monthly_rollup "
		"(now INTEGER, start INTEGER, endtime INTEGER) "
		"RETURNS VOID AS $$"
		"DECLARE rec %s;"
		"BEGIN "
		"  FOR rec IN "
		"    SELECT now, now, 0, id, start, SUM(alloc_cpu_secs)"
		"      FROM %s WHERE period_start < endtime AND "
		"      period_start > start GROUP BY id"
		"  LOOP"
		"    PERFORM add_assoc_month_usage(rec);"
		"END LOOP; END; $$ LANGUAGE PLPGSQL;",
		assoc_month_table, assoc_day_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _create_function_add_wckey_hour_usage - create a PL/pgSQL function
 *   to add single record of wckey hour usage data
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_add_wckey_hour_usage(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION add_wckey_hour_usage "
		"(rec %s) RETURNS VOID AS $$"
		"DECLARE "
		"BEGIN LOOP "
		"  BEGIN "
		"    INSERT INTO %s VALUES (rec.*); RETURN;"
		"  EXCEPTION WHEN UNIQUE_VIOLATION THEN "
		"    UPDATE %s SET (deleted, mod_time, alloc_cpu_secs,"
		"        resv_cpu_secs, over_cpu_secs) = "
		"        (0, rec.mod_time, rec.alloc_cpu_secs,"
		"        rec.resv_cpu_secs, rec.over_cpu_secs)"
		"      WHERE id=rec.id AND period_start=rec.period_start;"
		"    IF FOUND THEN RETURN; END IF; "
		"  END; "
		"END LOOP; END; $$ LANGUAGE PLPGSQL;",
		wckey_hour_table, wckey_hour_table, wckey_hour_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _create_function_add_wckey_hour_usages - create a PL/pgSQL function
 *   to add records of wckey hour usage data
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_add_wckey_hour_usages(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION add_wckey_hour_usages "
		"(recs %s[]) RETURNS VOID AS $$"
		"DECLARE "
		"  i INTEGER := 1; rec %s; "
		"BEGIN LOOP "
		"  rec := recs[i]; i := i + 1; "
		"  EXIT WHEN rec IS NULL; "
		"  PERFORM add_wckey_hour_usage(rec);"
		"END LOOP; END; $$ LANGUAGE PLPGSQL;",
		wckey_hour_table, wckey_hour_table,
		wckey_hour_table, wckey_hour_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _create_function_add_wckey_day_usage - create a PL/pgSQL function
 *   to add wckey day usage data
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_add_wckey_day_usage(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION add_wckey_day_usage "
		"(rec %s) RETURNS VOID AS $$"
		"BEGIN LOOP "
		"  BEGIN "
		"    INSERT INTO %s VALUES (rec.*); RETURN;"
		"  EXCEPTION WHEN UNIQUE_VIOLATION THEN "
		"    UPDATE %s SET (deleted, mod_time, alloc_cpu_secs,"
		"      resv_cpu_secs, over_cpu_secs) = "
		"      (0, rec.mod_time, rec.alloc_cpu_secs,"
		"      rec.resv_cpu_secs, rec.over_cpu_secs)"
		"    WHERE id=rec.id AND period_start=rec.period_start;"
		"    IF FOUND THEN RETURN; END IF;"
		"  END; "
		"END LOOP; END; $$ LANGUAGE PLPGSQL;",
		wckey_day_table, wckey_day_table, wckey_day_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _create_function_add_wckey_month_usage - create a PL/pgSQL function
 *   to add wckey month usage data
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_add_wckey_month_usage(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION add_wckey_month_usage "
		"(rec %s) RETURNS VOID AS $$"
		"BEGIN LOOP "
		"  BEGIN "
		"    INSERT INTO %s VALUES (rec.*); RETURN;"
		"  EXCEPTION WHEN UNIQUE_VIOLATION THEN "
		"    UPDATE %s SET (deleted, mod_time, alloc_cpu_secs,"
		"      resv_cpu_secs, over_cpu_secs) = "
		"      (0, rec.mod_time, rec.alloc_cpu_secs,"
		"      rec.resv_cpu_secs, rec.over_cpu_secs)"
		"    WHERE id=rec.id AND period_start=rec.period_start;"
		"    IF FOUND THEN RETURN; END IF;"
		"  END; "
		"END LOOP; END; $$ LANGUAGE PLPGSQL;",
		wckey_month_table, wckey_month_table, wckey_month_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _create_function_wckey_daily_rollup - create a PL/pgSQL function
 *   to rollup wckey usage data daily
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_wckey_daily_rollup(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION wckey_daily_rollup "
		"(now INTEGER, start INTEGER, endtime INTEGER) "
		"RETURNS VOID AS $$"
		"DECLARE rec %s;"
		"BEGIN "
		"  FOR rec IN "
		"    SELECT now, now, 0, id, start, SUM(alloc_cpu_secs)"
		"      FROM %s WHERE period_start < endtime AND "
		"      period_start > start GROUP BY id"
		"  LOOP"
		"    PERFORM add_wckey_day_usage(rec);"
		"  END LOOP; "
		"END; $$ LANGUAGE PLPGSQL;",
		wckey_day_table, wckey_hour_table);
	return create_function_xfree(db_conn, create_line);
}


/*
 * _create_function_wckey_monthly_rollup - create a PL/pgSQL function
 *   to rollup wckey usage data monthly
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_wckey_monthly_rollup(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION wckey_monthly_rollup "
		"(now INTEGER, start INTEGER, endtime INTEGER) "
		"RETURNS VOID AS $$"
		"DECLARE rec %s;"
		"BEGIN "
		"  FOR rec IN "
		"    SELECT now, now, 0, id, start, SUM(alloc_cpu_secs)"
		"      FROM %s WHERE period_start < endtime AND "
		"      period_start > start GROUP BY id"
		"  LOOP"
		"    PERFORM add_wckey_month_usage(rec);"
		"  END LOOP; "
		"END; $$ LANGUAGE PLPGSQL;",
		wckey_month_table, wckey_day_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _create_function_init_last_ran - create a PL/pgSQL function
 *   to init the last_ran_table
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_init_last_ran(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION init_last_ran (now INTEGER) "
		"RETURNS INTEGER AS $$"
		"DECLARE ins INTEGER; ret INTEGER;"
		"BEGIN "
		"  SELECT period_start INTO ins FROM %s "
		"    ORDER BY period_start LIMIT 1; "
		"  IF FOUND THEN "
		"    ret := ins;"
		"  ELSE "
		"    ins := now; ret := -1;"
		"  END IF; "
		"  INSERT INTO %s (hourly_rollup, daily_rollup, "
		"    monthly_rollup) "
		"    VALUES(ins, ins, ins);"
		"  RETURN ret;"
		"END; $$ LANGUAGE PLPGSQL;",
		event_table, last_ran_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * check_usage_tables - check usage related tables and functions
 * IN pg_conn: database connection
 * IN user: database owner
 * RET: error code
 */
extern int
check_usage_tables(PGconn *db_conn, char *user)
{
	int rc = 0;

	rc |= check_table(db_conn, assoc_day_table, assoc_usage_table_fields,
			  assoc_usage_table_constraint, user);
	rc |= check_table(db_conn, assoc_hour_table, assoc_usage_table_fields,
			  assoc_usage_table_constraint, user);
	rc |= check_table(db_conn, assoc_month_table, assoc_usage_table_fields,
			  assoc_usage_table_constraint, user);

	rc |= check_table(db_conn, cluster_day_table, cluster_usage_table_fields,
			  cluster_usage_table_constraint, user);
	rc |= check_table(db_conn, cluster_hour_table, cluster_usage_table_fields,
			  cluster_usage_table_constraint, user);
	rc |= check_table(db_conn, cluster_month_table, cluster_usage_table_fields,
			  cluster_usage_table_constraint, user);

	rc |= check_table(db_conn, wckey_day_table, wckey_usage_table_fields,
			  wckey_usage_table_constraint, user);
	rc |= check_table(db_conn, wckey_hour_table, wckey_usage_table_fields,
			  wckey_usage_table_constraint, user);
	rc |= check_table(db_conn, wckey_month_table, wckey_usage_table_fields,
			  wckey_usage_table_constraint, user);

	rc |= check_table(db_conn, last_ran_table, last_ran_table_fields,
			  last_ran_table_constraint, user);

	rc |= _create_function_add_cluster_hour_usage(db_conn);
	rc |= _create_function_add_cluster_hour_usages(db_conn);
	rc |= _create_function_add_cluster_day_usage(db_conn);
	rc |= _create_function_add_cluster_month_usage(db_conn);
	rc |= _create_function_cluster_daily_rollup(db_conn);
	rc |= _create_function_cluster_monthly_rollup(db_conn);

	rc |= _create_function_add_assoc_hour_usage(db_conn);
	rc |= _create_function_add_assoc_hour_usages(db_conn);
	rc |= _create_function_add_assoc_day_usage(db_conn);
	rc |= _create_function_add_assoc_month_usage(db_conn);
	rc |= _create_function_assoc_daily_rollup(db_conn);
	rc |= _create_function_assoc_monthly_rollup(db_conn);

	rc |= _create_function_add_wckey_hour_usage(db_conn);
	rc |= _create_function_add_wckey_hour_usages(db_conn);
	rc |= _create_function_add_wckey_day_usage(db_conn);
	rc |= _create_function_add_wckey_month_usage(db_conn);
	rc |= _create_function_wckey_daily_rollup(db_conn);
	rc |= _create_function_wckey_monthly_rollup(db_conn);

	rc |= _create_function_init_last_ran(db_conn);
	return rc;
}

/*
 * delete_assoc_usage - mark usage records of given associations as deleted
 *
 * IN pg_conn: database connection
 * IN now: current time
 * IN assoc_cond: owner associations. XXX: every option has "t1." prefix.
 *    FORMAT: "TODO"
 * RET: error code
 */
extern int
delete_assoc_usage(pgsql_conn_t *pg_conn, time_t now, char *assoc_cond)
{
	int rc;
	char *query = xstrdup_printf(
		"UPDATE %s AS t1 SET mod_time=%d, deleted=1 WHERE (%s);"
		"UPDATE %s AS t1 SET mod_time=%d, deleted=1 WHERE (%s);"
		"UPDATE %s AS t1 SET mod_time=%d, deleted=1 WHERE (%s);",
		assoc_day_table, now, assoc_cond,
		assoc_hour_table, now, assoc_cond,
		assoc_month_table, now, assoc_cond);
	rc = DEF_QUERY_RET_RC;
	return rc;
}


/*
 * _get_assoc_usage - get association usage data
 *
 * IN pg_conn: database connection
 * IN uid: user performing get operation
 * IN/OUT acct_assoc: data of which association to get
 * IN start: start time
 * IN end: end time
 * RET: error code
 */
static int
_get_assoc_usage(pgsql_conn_t *pg_conn, uid_t uid,
		 acct_association_rec_t *acct_assoc,
		 time_t start, time_t end)
{
	int rc = SLURM_SUCCESS, is_admin=1;
	PGresult *result = NULL;
	char *usage_table = NULL, *query = NULL;
	uint16_t private_data = 0;
	acct_user_rec_t user;
	enum {
		USAGE_ID,
		USAGE_START,
		USAGE_ACPU,
		USAGE_COUNT
	};

	if(!acct_assoc->id) {
		error("We need an assoc id to set data for getting usage");
		return SLURM_ERROR;
	}

	memset(&user, 0, sizeof(acct_user_rec_t));
	user.uid = uid;

	private_data = slurm_get_private_data();
	if (private_data & PRIVATE_DATA_USAGE) {
		is_admin = is_user_admin(pg_conn, uid);
		if (!is_admin) {
			ListIterator itr = NULL;
			acct_coord_rec_t *coord = NULL;
			assoc_mgr_fill_in_user(pg_conn, &user, 1, NULL);

			if(acct_assoc->user &&
			   !strcmp(acct_assoc->user, user.name))
				goto is_user;

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

is_user:
	usage_table = assoc_day_table;
	if(set_usage_information(&usage_table, DBD_GET_ASSOC_USAGE,
				 &start, &end) != SLURM_SUCCESS)
		return SLURM_ERROR;

	query = xstrdup_printf(
		"SELECT t3.id, t1.period_start, t1.alloc_cpu_secs "
		"FROM %s AS t1, %s AS t2, %s AS t3 "
		"WHERE (t1.period_start < %d AND t1.period_start >= %d) "
		"AND t1.id=t2.id AND t3.id=%d AND "
		"(t2.lft BETWEEN t3.lft AND t3.rgt) "
		"ORDER BY t3.id, t1.period_start;",
		usage_table, assoc_table, assoc_table,
		end, start, acct_assoc->id);
	result = DEF_QUERY_RET;
	if(!result)
		return SLURM_ERROR;

	if(!acct_assoc->accounting_list)
		acct_assoc->accounting_list =
			list_create(destroy_acct_accounting_rec);

	FOR_EACH_ROW {
		acct_accounting_rec_t *accounting_rec =
			xmalloc(sizeof(acct_accounting_rec_t));
		accounting_rec->id = atoi(ROW(USAGE_ID));
		accounting_rec->period_start = atoi(ROW(USAGE_START));
		accounting_rec->alloc_secs = atoll(ROW(USAGE_ACPU));
		list_append(acct_assoc->accounting_list, accounting_rec);
	} END_EACH_ROW;
	PQclear(result);

	return rc;
}

/*
 * _get_wckey_usage - get wckey usage data
 *
 * IN pg_conn: database connection
 * IN uid: user performing get operation
 * IN/OUT acct_wckey: usage data of which wckey to get
 * IN start: start time
 * IN end: end time
 * RET: error code
 */
static int
_get_wckey_usage(pgsql_conn_t *pg_conn, uid_t uid,
		 acct_wckey_rec_t *acct_wckey,
		 time_t start, time_t end)
{
	int rc = SLURM_SUCCESS, is_admin=1;
	PGresult *result = NULL;
	char *usage_table = NULL, *query = NULL;
	uint16_t private_data = 0;
	acct_user_rec_t user;
	enum {
		USAGE_ID,
		USAGE_START,
		USAGE_ACPU,
		USAGE_COUNT
	};

	if(!acct_wckey->id) {
		error("We need an wckey id to set data for getting usage");
		return SLURM_ERROR;
	}

	memset(&user, 0, sizeof(acct_user_rec_t));
	user.uid = uid;

	private_data = slurm_get_private_data();
	if (private_data & PRIVATE_DATA_USAGE) {
		is_admin = is_user_admin(pg_conn, uid);
		if (!is_admin) {
			assoc_mgr_fill_in_user(pg_conn, &user, 1, NULL);
			if(! acct_wckey->user ||
			   strcmp(acct_wckey->user, user.name)) {
				errno = ESLURM_ACCESS_DENIED;
				return SLURM_ERROR;
			}
		}
	}

	usage_table = wckey_day_table;
	if(set_usage_information(&usage_table, DBD_GET_WCKEY_USAGE,
				 &start, &end) != SLURM_SUCCESS) {
		return SLURM_ERROR;
	}

	query = xstrdup_printf(
		"SELECT id, period_start, alloc_cpu_secs FROM %s "
		"WHERE (period_start < %d AND period_start >= %d) "
		"AND id=%d ORDER BY id, period_start;",
		usage_table, end, start, acct_wckey->id);
	result = DEF_QUERY_RET;
	if(!result)
		return SLURM_ERROR;

	if(!acct_wckey->accounting_list)
		acct_wckey->accounting_list =
			list_create(destroy_acct_accounting_rec);

	FOR_EACH_ROW {
		acct_accounting_rec_t *accounting_rec =
			xmalloc(sizeof(acct_accounting_rec_t));
		accounting_rec->id = atoi(ROW(USAGE_ID));
		accounting_rec->period_start = atoi(ROW(USAGE_START));
		accounting_rec->alloc_secs = atoll(ROW(USAGE_ACPU));
		list_append(acct_wckey->accounting_list, accounting_rec);
	} END_EACH_ROW;
	PQclear(result);
	return rc;
}

/*
 * as_p_get_usage - get cluster usage
 *
 * IN pg_conn: database connection
 * IN uid: user performing the get operation
 * IN/OUT in: type dependent entity
 * IN type: type of usage to get
 * IN start: start time
 * IN end: end time
 * RET: error code
 */
extern int
as_p_get_usage(pgsql_conn_t *pg_conn, uid_t uid, void *in,
	       slurmdbd_msg_type_t type, time_t start, time_t end)
{
	int rc = SLURM_SUCCESS;

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	switch (type) {
	case DBD_GET_ASSOC_USAGE:
		rc = _get_assoc_usage(pg_conn, uid, in, start, end);
		break;
	case DBD_GET_WCKEY_USAGE:
		rc = _get_wckey_usage(pg_conn, uid, in, start, end);
		break;
	default:
		error("Unknown usage type %d", type);
		rc = SLURM_ERROR;
		break;
	}
	return rc;
}

/*
 * as_p_roll_usage - rollup usage information
 *
 * IN pg_conn: database connection
 * IN sent_start: start time
 * IN sent_end: end time
 * IN archive_data: whether to archive usage data
 * RET: error code
 */
extern int
as_p_roll_usage(pgsql_conn_t *pg_conn,  time_t sent_start,
		time_t sent_end, uint16_t archive_data)
{
	int rc = SLURM_SUCCESS;
	PGresult *result = NULL;
	char *query = NULL;
	time_t last_hour = sent_start;
	time_t last_day = sent_start;
	time_t last_month = sent_start;
	time_t start_time = 0;
  	time_t end_time = 0;
	time_t my_time = sent_end;
	struct tm start_tm;
	struct tm end_tm;
	DEF_TIMERS;
	char *ru_fields = "hourly_rollup, daily_rollup, monthly_rollup";
	enum {
		RU_HOUR,
		RU_DAY,
		RU_MONTH,
		RU_COUNT
	};

	if(check_db_connection(pg_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	if(!sent_start) {
		query = xstrdup_printf("SELECT %s FROM %s LIMIT 1",
				       ru_fields, last_ran_table);
		result = DEF_QUERY_RET;
		if(!result)
			return SLURM_ERROR;

		if(PQntuples(result)) {
			last_hour = atoi(PG_VAL(RU_HOUR));
			last_day = atoi(PG_VAL(RU_DAY));
			last_month = atoi(PG_VAL(RU_MONTH));
			PQclear(result);
		} else {
			time_t now = time(NULL);
			PQclear(result);
			query = xstrdup_printf("SELECT init_last_ran(%d);", now);
			result = DEF_QUERY_RET;
			if(!result)
				return SLURM_ERROR;
			last_hour = last_day = last_month =
				atoi(PG_VAL(0));
			PQclear(result);
			if (last_hour < 0) {
				debug("No clusters have been added "
				      "not doing rollup");
				return SLURM_SUCCESS;
			}
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

	/* align to hour boundary */
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
		if((rc = pgsql_hourly_rollup(pg_conn, start_time, end_time))
		   != SLURM_SUCCESS)
			return rc;
		END_TIMER3("hourly_rollup", 5000000);
		/* If we have a sent_end do not update the last_run_table */
		if(!sent_end)
			query = xstrdup_printf("UPDATE %s SET hourly_rollup=%d",
					       last_ran_table, end_time);
	} else {
		debug2("no need to run this hour %d <= %d",
		       end_time, start_time);
	}


	if(!localtime_r(&last_day, &start_tm)) {
		error("Couldn't get localtime from day %d", last_day);
		return SLURM_ERROR;
	}
	/* align to day boundary */
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
		if((rc = pgsql_daily_rollup(pg_conn, start_time, end_time,
					    archive_data))
		   != SLURM_SUCCESS)
			return rc;
		END_TIMER2("daily_rollup");
		if(query && !sent_end)
			xstrfmtcat(query, ", daily_rollup=%d", end_time);
		else if(!sent_end)
			query = xstrdup_printf("UPDATE %s SET daily_rollup=%d",
					       last_ran_table, end_time);
	} else {
		debug2("no need to run this day %d <= %d",
		       end_time, start_time);
	}

	if(!localtime_r(&last_month, &start_tm)) {
		error("Couldn't get localtime from month %d", last_month);
		return SLURM_ERROR;
	}

	/* align to month boundary */
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
		if((rc = pgsql_monthly_rollup(
			    pg_conn, start_time, end_time, archive_data))
		   != SLURM_SUCCESS)
			return rc;
		END_TIMER2("monthly_rollup");

		if(query && !sent_end)
			xstrfmtcat(query, ", monthly_rollup=%d", end_time);
		else if(!sent_end)
			query = xstrdup_printf(
				"UPDATE %s SET monthly_rollup=%d",
				last_ran_table, end_time);
	} else {
		debug2("no need to run this month %d <= %d",
		       end_time, start_time);
	}

	if(query) {
		rc = DEF_QUERY_RET_RC;
	}
	return rc;
}

/*
 * get_usage_for_assoc_list - get usage info for association list
 *
 * IN pg_conn: database connection
 * IN assoc_list: associations to get usage for
 * IN start: start time
 * IN end: end time
 * RET: error code
 */
extern int
get_usage_for_assoc_list(pgsql_conn_t *pg_conn, List assoc_list,
			 time_t start, time_t end)
{
	int rc = SLURM_SUCCESS;
	PGresult *result = NULL;
	char *usage_table = NULL, *query = NULL, *id_str = NULL;
	List usage_list = NULL;
	ListIterator itr = NULL, u_itr = NULL;
	acct_association_rec_t *assoc = NULL;
	acct_accounting_rec_t *accounting_rec = NULL;
	enum {
		USAGE_ID,
		USAGE_START,
		USAGE_ACPU,
		USAGE_COUNT
	};

	if(!assoc_list) {
		error("We need an object to set data for getting usage");
		return SLURM_ERROR;
	}
	usage_table = assoc_day_table;
	if(set_usage_information(&usage_table, DBD_GET_ASSOC_USAGE,
				 &start, &end) != SLURM_SUCCESS) {
		return SLURM_ERROR;
	}

	itr = list_iterator_create(assoc_list);
	while((assoc = list_next(itr))) {
		if(id_str)
			xstrfmtcat(id_str, " OR t3.id=%d", assoc->id);
		else
			xstrfmtcat(id_str, "t3.id=%d", assoc->id);
	}
	list_iterator_destroy(itr);

	query = xstrdup_printf(
		"SELECT t3.id, t1.period_start, t1.alloc_cpu_secs "
		"FROM %s AS t1, %s AS t2, %s AS t3 "
		"WHERE (t1.period_start < %d AND t1.period_start >= %d) "
		"AND t1.id=t2.id AND (%s) AND "
		"(t2.lft between t3.lft and t3.rgt) "
		"ORDER BY t3.id, period_start;",
		usage_table, assoc_table, assoc_table,
		end, start, id_str);
	xfree(id_str);
	result = DEF_QUERY_RET;
	if(!result)
		return SLURM_ERROR;

	usage_list = list_create(destroy_acct_accounting_rec);
	FOR_EACH_ROW {
		acct_accounting_rec_t *accounting_rec =
			xmalloc(sizeof(acct_accounting_rec_t));
		accounting_rec->id = atoi(ROW(USAGE_ID));
		accounting_rec->period_start = atoi(ROW(USAGE_START));
		accounting_rec->alloc_secs = atoll(ROW(USAGE_ACPU));
		list_append(usage_list, accounting_rec);
	} END_EACH_ROW;
	PQclear(result);

	u_itr = list_iterator_create(usage_list);
	itr = list_iterator_create(assoc_list);
	while((assoc = list_next(itr))) {
		int found = 0;
		if(!assoc->accounting_list)
			assoc->accounting_list = list_create(
				destroy_acct_accounting_rec);
		while((accounting_rec = list_next(u_itr))) {
			if(assoc->id == accounting_rec->id) {
				list_append(assoc->accounting_list,
					    accounting_rec);
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

/*
 * get_usage_for_wckey_list - get usage info for wckey list
 *
 * IN pg_conn: database connection
 * IN wckey_list: wckeys to get usage for
 * IN start: start time
 * IN end: end time
 * RET: error code
 */
extern int
get_usage_for_wckey_list(pgsql_conn_t *pg_conn, List wckey_list,
			 time_t start, time_t end)
{
	int rc = SLURM_SUCCESS;
	PGresult *result = NULL;
	char *usage_table = NULL, *query = NULL, *id_str = NULL;
	List usage_list = NULL;
	ListIterator itr = NULL, u_itr = NULL;
	acct_wckey_rec_t *wckey = NULL;
	acct_accounting_rec_t *accounting_rec = NULL;
	enum {
		USAGE_ID,
		USAGE_START,
		USAGE_ACPU,
		USAGE_COUNT
	};

	if(!wckey_list) {
		error("We need an object to set data for getting usage");
		return SLURM_ERROR;
	}

	usage_table = wckey_day_table;
	if(set_usage_information(&usage_table, DBD_GET_WCKEY_USAGE,
				 &start, &end) != SLURM_SUCCESS) {
		return SLURM_ERROR;
	}

	itr = list_iterator_create(wckey_list);
	while((wckey = list_next(itr))) {
		if(id_str)
			xstrfmtcat(id_str, " OR id=%d", wckey->id);
		else
			xstrfmtcat(id_str, "id=%d", wckey->id);
	}
	list_iterator_destroy(itr);

	query = xstrdup_printf(
		"SELECT id, period_start, alloc_cpu_secs FROM %s "
		"WHERE (period_start < %d AND period_start >= %d) "
		"AND (%s) ORDER BY id, period_start;",
		usage_table, end, start, id_str);
	xfree(id_str);
	result = DEF_QUERY_RET;
	if(!result)
		return SLURM_ERROR;

	usage_list = list_create(destroy_acct_accounting_rec);
	FOR_EACH_ROW {
		acct_accounting_rec_t *accounting_rec =
			xmalloc(sizeof(acct_accounting_rec_t));
		accounting_rec->id = atoi(ROW(USAGE_ID));
		accounting_rec->period_start = atoi(ROW(USAGE_START));
		accounting_rec->alloc_secs = atoll(ROW(USAGE_ACPU));
		list_append(usage_list, accounting_rec);
	} END_EACH_ROW;
	PQclear(result);

	u_itr = list_iterator_create(usage_list);
	itr = list_iterator_create(wckey_list);
	while((wckey = list_next(itr))) {
		int found = 0;
		if(!wckey->accounting_list)
			wckey->accounting_list = list_create(
				destroy_acct_accounting_rec);
		while((accounting_rec = list_next(u_itr))) {
			if(wckey->id == accounting_rec->id) {
				list_append(wckey->accounting_list,
					    accounting_rec);
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
		      "to the wckey list",
		      list_count(usage_list));
	list_destroy(usage_list);

	return rc;
}
