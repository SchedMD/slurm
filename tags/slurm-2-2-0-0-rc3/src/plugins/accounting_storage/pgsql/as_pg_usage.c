/*****************************************************************************\
 *  as_pg_usage.c - accounting interface to pgsql - cluster usage related 
 *  functions.
 *
 *  $Id: as_pg_usage.c 13061 2008-01-22 21:23:56Z da $
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

/* all per-cluster tables */

char *assoc_day_table = "assoc_day_usage_table";
char *assoc_hour_table = "assoc_hour_usage_table";
char *assoc_month_table = "assoc_month_usage_table";
static storage_field_t assoc_usage_table_fields[] = {
	{ "creation_time", "INTEGER NOT NULL" },
	{ "mod_time", "INTEGER DEFAULT 0 NOT NULL" },
	{ "deleted", "INTEGER DEFAULT 0" },
	{ "id_assoc", "INTEGER NOT NULL" },
	{ "time_start", "INTEGER NOT NULL" },
	{ "alloc_cpu_secs", "BIGINT DEFAULT 0" },
	{ NULL, NULL}
};
static char *assoc_usage_table_constraint = ", "
	"PRIMARY KEY (id_assoc, time_start) "
	")";

char *cluster_day_table = "cluster_day_usage_table";
char *cluster_hour_table = "cluster_hour_usage_table";
char *cluster_month_table = "cluster_month_usage_table";
static storage_field_t cluster_usage_table_fields[] = {
	{ "creation_time", "INTEGER NOT NULL" },
	{ "mod_time", "INTEGER DEFAULT 0 NOT NULL" },
	{ "deleted", "INTEGER DEFAULT 0" },
	{ "time_start", "INTEGER NOT NULL" },
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
	"PRIMARY KEY (time_start) "
	")";

char *wckey_day_table = "wckey_day_usage_table";
char *wckey_hour_table = "wckey_hour_usage_table";
char *wckey_month_table = "wckey_month_usage_table";
static storage_field_t wckey_usage_table_fields[] = {
	{ "creation_time", "INTEGER NOT NULL" },
	{ "mod_time", "INTEGER DEFAULT 0 NOT NULL" },
	{ "deleted", "INTEGER DEFAULT 0" },
	{ "id_wckey", "INTEGER NOT NULL" },
	{ "time_start", "INTEGER NOT NULL" },
	{ "alloc_cpu_secs", "BIGINT DEFAULT 0" },
	{ "resv_cpu_secs", "BIGINT DEFAULT 0" },
	{ "over_cpu_secs", "BIGINT DEFAULT 0" },
	{ NULL, NULL}
};
static char *wckey_usage_table_constraint = ", "
	"PRIMARY KEY (id_wckey, time_start) "
	")";

char *last_ran_table = "last_ran_table";
static storage_field_t last_ran_table_fields[] = {
	{ "hourly_rollup", "INTEGER DEFAULT 0 NOT NULL" },
	{ "daily_rollup", "INTEGER DEFAULT 0 NOT NULL" },
	{ "monthly_rollup", "INTEGER DEFAULT 0 NOT NULL" },
	{ NULL, NULL}
};
static char *last_ran_table_constraint = ")";

static int
_create_function_add_cluster_hour_usage(PGconn *db_conn, char *cluster)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION %s.add_cluster_hour_usage "
		"(rec %s.%s) RETURNS VOID AS $$"
		"BEGIN LOOP "
		"  BEGIN "
		"    INSERT INTO %s.%s VALUES (rec.*); RETURN; "
		"  EXCEPTION WHEN UNIQUE_VIOLATION THEN "
		"    UPDATE %s.%s SET (deleted, mod_time, cpu_count, "
		"        alloc_cpu_secs, down_cpu_secs, pdown_cpu_secs, "
		"        idle_cpu_secs, over_cpu_secs, resv_cpu_secs) = "
		"        (0, rec.mod_time, rec.cpu_count, rec.alloc_cpu_secs,"
		"        rec.down_cpu_secs, rec.pdown_cpu_secs, "
		"        rec.idle_cpu_secs, rec.over_cpu_secs, "
		"        rec.resv_cpu_secs)"
		"      WHERE time_start=rec.time_start;"
		"    IF FOUND THEN RETURN; END IF; "
		"  END;"
		"END LOOP; END; $$ LANGUAGE PLPGSQL;", cluster, cluster,
		cluster_hour_table, cluster, cluster_hour_table,
		cluster, cluster_hour_table);
	return create_function_xfree(db_conn, create_line);
}

static int
_create_function_add_cluster_hour_usages(PGconn *db_conn, char *cluster)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION %s.add_cluster_hour_usages "
		"(recs %s.%s[]) RETURNS VOID AS $$"
		"DECLARE "
		"  i INTEGER := 1; rec %s.%s; "
		"BEGIN LOOP "
		"  rec := recs[i]; i := i + 1; "
		"  EXIT WHEN rec IS NULL;"
		"  PERFORM %s.add_cluster_hour_usage(rec);"
		"END LOOP; END; $$ LANGUAGE PLPGSQL;", cluster,
		cluster, cluster_hour_table, cluster, cluster_hour_table, cluster);
	return create_function_xfree(db_conn, create_line);
}

static int
_create_function_add_cluster_day_usage(PGconn *db_conn, char *cluster)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION %s.add_cluster_day_usage "
		"(rec %s.%s) RETURNS VOID AS $$"
		"BEGIN LOOP "
		"  BEGIN "
		"    INSERT INTO %s.%s VALUES (rec.*); RETURN;"
		"  EXCEPTION WHEN UNIQUE_VIOLATION THEN "
		"    UPDATE %s.%s SET (deleted, mod_time, cpu_count, "
		"      alloc_cpu_secs, down_cpu_secs, pdown_cpu_secs, "
		"      idle_cpu_secs, over_cpu_secs, resv_cpu_secs) = "
		"      (0, rec.mod_time, rec.cpu_count, rec.alloc_cpu_secs,"
		"      rec.down_cpu_secs, rec.pdown_cpu_secs, "
		"      rec.idle_cpu_secs, rec.over_cpu_secs, "
		"      rec.resv_cpu_secs)"
		"    WHERE time_start=rec.time_start;"
		"    IF FOUND THEN RETURN; END IF; "
		"  END;"
		"END LOOP; END; $$ LANGUAGE PLPGSQL;", cluster,
		cluster, cluster_day_table, cluster, cluster_day_table,
		cluster, cluster_day_table);
	return create_function_xfree(db_conn, create_line);
}

static int
_create_function_add_cluster_month_usage(PGconn *db_conn, char *cluster)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION %s.add_cluster_day_usage "
		"(rec %s.%s) RETURNS VOID AS $$"
		"BEGIN LOOP "
		"  BEGIN "
		"    INSERT INTO %s.%s VALUES (rec.*); RETURN;"
		"  EXCEPTION WHEN UNIQUE_VIOLATION THEN "
		"    UPDATE %s.%s SET (deleted, mod_time, cpu_count, "
		"      alloc_cpu_secs, down_cpu_secs, pdown_cpu_secs, "
		"      idle_cpu_secs, over_cpu_secs, resv_cpu_secs) = "
		"      (0, rec.mod_time, rec.cpu_count, rec.alloc_cpu_secs,"
		"      rec.down_cpu_secs, rec.pdown_cpu_secs, "
		"      rec.idle_cpu_secs, rec.over_cpu_secs, "
		"      rec.resv_cpu_secs)"
		"    WHERE time_start=rec.time_start;"
		"    IF FOUND THEN RETURN; END IF; "
		"  END;"
		"END LOOP; END; $$ LANGUAGE PLPGSQL;", cluster,
		cluster, cluster_month_table, cluster, cluster_month_table,
		cluster, cluster_month_table);
	return create_function_xfree(db_conn, create_line);
}

static int
_create_function_cluster_daily_rollup(PGconn *db_conn, char *cluster)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION %s.cluster_daily_rollup "
		"(now INTEGER, starttime INTEGER, endtime INTEGER) "
		"RETURNS VOID AS $$"
		"DECLARE rec %s.%s;"
		"BEGIN "
		"  FOR rec IN "
		"    SELECT now, now, 0, starttime, MAX(cpu_count), "
		"      SUM(alloc_cpu_secs), SUM(down_cpu_secs), "
		"      SUM(pdown_cpu_secs), SUM(idle_cpu_secs), "
		"      SUM(over_cpu_secs), SUM(resv_cpu_secs) FROM %s.%s "
		"    WHERE time_start < endtime AND time_start > starttime "
		"  LOOP"
		"    PERFORM %s.add_cluster_day_usage(rec);"
		"END LOOP; END; $$ LANGUAGE PLPGSQL;", cluster, cluster,
		cluster_day_table, cluster, cluster_hour_table, cluster);
	return create_function_xfree(db_conn, create_line);
}

static int
_create_function_cluster_monthly_rollup(PGconn *db_conn, char *cluster)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION %s.cluster_monthly_rollup "
		"(now INTEGER, starttime INTEGER, endtime INTEGER) "
		"RETURNS VOID AS $$"
		"DECLARE rec %s.%s;"
		"BEGIN "
		"  FOR rec IN "
		"    SELECT now, now, 0, starttime, MAX(cpu_count), "
		"      SUM(alloc_cpu_secs), SUM(down_cpu_secs), "
		"      SUM(pdown_cpu_secs), SUM(idle_cpu_secs), "
		"      SUM(over_cpu_secs), SUM(resv_cpu_secs) FROM %s.%s "
		"    WHERE time_start < endtime AND time_start > starttime "
		"  LOOP"
		"    PERFORM %s.add_cluster_day_usage(rec);"
		"END LOOP; END; $$ LANGUAGE PLPGSQL;", cluster, cluster,
		cluster_month_table, cluster, cluster_day_table, cluster);
	return create_function_xfree(db_conn, create_line);
}


static int
_create_function_add_assoc_hour_usage(PGconn *db_conn, char *cluster)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION %s.add_assoc_hour_usage "
		"(rec %s.%s) RETURNS VOID AS $$"
		"BEGIN LOOP "
		"  BEGIN "
		"    INSERT INTO %s.%s VALUES (rec.*); RETURN;"
		"  EXCEPTION WHEN UNIQUE_VIOLATION THEN "
		"    UPDATE %s.%s SET (deleted, mod_time, alloc_cpu_secs) = "
		"        (0, rec.mod_time, rec.alloc_cpu_secs)"
		"      WHERE id_assoc=rec.id_assoc AND "
		"        time_start=rec.time_start;"
		"    IF FOUND THEN RETURN; END IF; "
		"  END;"
		"END LOOP; END; $$ LANGUAGE PLPGSQL;", cluster, cluster,
		assoc_hour_table, cluster, assoc_hour_table, cluster,
		assoc_hour_table);
	return create_function_xfree(db_conn, create_line);
}

static int
_create_function_add_assoc_hour_usages(PGconn *db_conn, char *cluster)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION %s.add_assoc_hour_usages "
		"(recs %s.%s[]) RETURNS VOID AS $$"
		"DECLARE "
		"  i INTEGER := 1; rec %s.%s; "
		"BEGIN LOOP "
		"  rec := recs[i]; i := i + 1; "
		"  EXIT WHEN rec IS NULL;"
		"  PERFORM %s.add_assoc_hour_usage(rec);"
		"END LOOP; END; $$ LANGUAGE PLPGSQL;", cluster, cluster,
		assoc_hour_table, cluster, assoc_hour_table, cluster);
	return create_function_xfree(db_conn, create_line);
}

static int
_create_function_add_assoc_day_usage(PGconn *db_conn, char *cluster)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION %s.add_assoc_day_usage "
		"(rec %s.%s) RETURNS VOID AS $$"
		"BEGIN LOOP "
		"  BEGIN "
		"    INSERT INTO %s.%s VALUES (rec.*); RETURN;"
		"  EXCEPTION WHEN UNIQUE_VIOLATION THEN "
		"    UPDATE %s.%s SET (deleted, mod_time, alloc_cpu_secs) = "
		"      (0, rec.mod_time, rec.alloc_cpu_secs)"
		"    WHERE id_assoc=rec.id_assoc AND "
		"      time_start=rec.time_start;"
		"    IF FOUND THEN RETURN; END IF; "
		"  END; "
		"END LOOP; END; $$ LANGUAGE PLPGSQL;", cluster, cluster,
		assoc_day_table, cluster, assoc_day_table, cluster,
		assoc_day_table);
	return create_function_xfree(db_conn, create_line);
}

static int
_create_function_add_assoc_month_usage(PGconn *db_conn, char *cluster)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION %s.add_assoc_month_usage "
		"(rec %s.%s) RETURNS VOID AS $$"
		"BEGIN LOOP "
		"  BEGIN "
		"    INSERT INTO %s.%s VALUES (rec.*); RETURN;"
		"  EXCEPTION WHEN UNIQUE_VIOLATION THEN "
		"    UPDATE %s.%s SET (deleted, mod_time, alloc_cpu_secs) = "
		"      (0, rec.mod_time, rec.alloc_cpu_secs)"
		"    WHERE id_assoc=rec.id_assoc AND "
		"      time_start=rec.time_start;"
		"    IF FOUND THEN RETURN; END IF; "
		"  END; "
		"END LOOP; END; $$ LANGUAGE PLPGSQL;", cluster, cluster,
		assoc_month_table, cluster, assoc_month_table, cluster,
		assoc_month_table);
	return create_function_xfree(db_conn, create_line);
}

static int
_create_function_assoc_daily_rollup(PGconn *db_conn, char *cluster)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION %s.assoc_daily_rollup "
		"(now INTEGER, starttime INTEGER, endtime INTEGER) "
		"RETURNS VOID AS $$"
		"DECLARE rec %s.%s;"
		"BEGIN "
		"  FOR rec IN "
		"    SELECT now, now, 0, id_assoc, starttime, "
		"      SUM(alloc_cpu_secs) FROM %s.%s "
		"      WHERE time_start < endtime AND "
		"      time_start > starttime GROUP BY id_assoc"
		"  LOOP"
		"    PERFORM %s.add_assoc_day_usage(rec);"
		"END LOOP; END; $$ LANGUAGE PLPGSQL;", cluster, cluster,
		assoc_day_table, cluster, assoc_hour_table, cluster);
	return create_function_xfree(db_conn, create_line);
}

static int
_create_function_assoc_monthly_rollup(PGconn *db_conn, char *cluster)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION %s.assoc_monthly_rollup "
		"(now INTEGER, starttime INTEGER, endtime INTEGER) "
		"RETURNS VOID AS $$"
		"DECLARE rec %s.%s;"
		"BEGIN "
		"  FOR rec IN "
		"    SELECT now, now, 0, id_assoc, starttime, "
		"      SUM(alloc_cpu_secs) FROM %s.%s "
		"      WHERE time_start < endtime AND "
		"      time_start > starttime GROUP BY id_assoc"
		"  LOOP"
		"    PERFORM %s.add_assoc_month_usage(rec);"
		"END LOOP; END; $$ LANGUAGE PLPGSQL;", cluster, cluster,
		assoc_month_table, cluster, assoc_day_table, cluster);
	return create_function_xfree(db_conn, create_line);
}

static int
_create_function_add_wckey_hour_usage(PGconn *db_conn, char *cluster)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION %s.add_wckey_hour_usage "
		"(rec %s.%s) RETURNS VOID AS $$"
		"DECLARE "
		"BEGIN LOOP "
		"  BEGIN "
		"    INSERT INTO %s.%s VALUES (rec.*); RETURN;"
		"  EXCEPTION WHEN UNIQUE_VIOLATION THEN "
		"    UPDATE %s.%s SET (deleted, mod_time, alloc_cpu_secs,"
		"        resv_cpu_secs, over_cpu_secs) = "
		"        (0, rec.mod_time, rec.alloc_cpu_secs,"
		"        rec.resv_cpu_secs, rec.over_cpu_secs)"
		"      WHERE id_wckey=rec.id_wckey AND time_start=rec.time_start;"
		"    IF FOUND THEN RETURN; END IF; "
		"  END; "
		"END LOOP; END; $$ LANGUAGE PLPGSQL;", cluster, cluster,
		wckey_hour_table, cluster, wckey_hour_table, cluster,
		wckey_hour_table);
	return create_function_xfree(db_conn, create_line);
}

static int
_create_function_add_wckey_hour_usages(PGconn *db_conn, char *cluster)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION %s.add_wckey_hour_usages "
		"(recs %s.%s[]) RETURNS VOID AS $$"
		"DECLARE "
		"  i INTEGER := 1; rec %s.%s; "
		"BEGIN LOOP "
		"  rec := recs[i]; i := i + 1; "
		"  EXIT WHEN rec IS NULL; "
		"  PERFORM %s.add_wckey_hour_usage(rec);"
		"END LOOP; END; $$ LANGUAGE PLPGSQL;", cluster, cluster,
		wckey_hour_table, cluster, wckey_hour_table, cluster);
	return create_function_xfree(db_conn, create_line);
}

static int
_create_function_add_wckey_day_usage(PGconn *db_conn, char *cluster)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION %s.add_wckey_day_usage "
		"(rec %s.%s) RETURNS VOID AS $$"
		"BEGIN LOOP "
		"  BEGIN "
		"    INSERT INTO %s.%s VALUES (rec.*); RETURN;"
		"  EXCEPTION WHEN UNIQUE_VIOLATION THEN "
		"    UPDATE %s.%s SET (deleted, mod_time, alloc_cpu_secs,"
		"      resv_cpu_secs, over_cpu_secs) = "
		"      (0, rec.mod_time, rec.alloc_cpu_secs,"
		"      rec.resv_cpu_secs, rec.over_cpu_secs)"
		"    WHERE id_wckey=rec.id_wckey AND time_start=rec.time_start;"
		"    IF FOUND THEN RETURN; END IF;"
		"  END; "
		"END LOOP; END; $$ LANGUAGE PLPGSQL;", cluster, cluster,
		wckey_day_table, cluster, wckey_day_table, cluster,
		wckey_day_table);
	return create_function_xfree(db_conn, create_line);
}

static int
_create_function_add_wckey_month_usage(PGconn *db_conn, char *cluster)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION %s.add_wckey_month_usage "
		"(rec %s.%s) RETURNS VOID AS $$"
		"BEGIN LOOP "
		"  BEGIN "
		"    INSERT INTO %s.%s VALUES (rec.*); RETURN;"
		"  EXCEPTION WHEN UNIQUE_VIOLATION THEN "
		"    UPDATE %s.%s SET (deleted, mod_time, alloc_cpu_secs,"
		"      resv_cpu_secs, over_cpu_secs) = "
		"      (0, rec.mod_time, rec.alloc_cpu_secs,"
		"      rec.resv_cpu_secs, rec.over_cpu_secs)"
		"    WHERE id_wckey=rec.id_wckey AND time_start=rec.time_start;"
		"    IF FOUND THEN RETURN; END IF;"
		"  END; "
		"END LOOP; END; $$ LANGUAGE PLPGSQL;", cluster, cluster,
		wckey_month_table, cluster, wckey_month_table, cluster,
		wckey_month_table);
	return create_function_xfree(db_conn, create_line);
}

static int
_create_function_wckey_daily_rollup(PGconn *db_conn, char *cluster)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION %s.wckey_daily_rollup "
		"(now INTEGER, starttime INTEGER, endtime INTEGER) "
		"RETURNS VOID AS $$"
		"DECLARE rec %s.%s;"
		"BEGIN "
		"  FOR rec IN "
		"    SELECT now, now, 0, id_wckey, starttime, "
		"      SUM(alloc_cpu_secs) FROM %s.%s "
		"      WHERE time_start < endtime AND "
		"      time_start > starttime GROUP BY id_wckey"
		"  LOOP"
		"    PERFORM %s.add_wckey_day_usage(rec);"
		"  END LOOP; "
		"END; $$ LANGUAGE PLPGSQL;", cluster, cluster,
		wckey_day_table, cluster, wckey_hour_table, cluster);
	return create_function_xfree(db_conn, create_line);
}


static int
_create_function_wckey_monthly_rollup(PGconn *db_conn, char *cluster)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION %s.wckey_monthly_rollup "
		"(now INTEGER, starttime INTEGER, endtime INTEGER) "
		"RETURNS VOID AS $$"
		"DECLARE rec %s.%s;"
		"BEGIN "
		"  FOR rec IN "
		"    SELECT now, now, 0, id_wckey, starttime, "
		"      SUM(alloc_cpu_secs) FROM %s.%s "
		"      WHERE time_start < endtime AND "
		"      time_start > starttime GROUP BY id_wckey"
		"  LOOP"
		"    PERFORM %s.add_wckey_month_usage(rec);"
		"  END LOOP; "
		"END; $$ LANGUAGE PLPGSQL;", cluster, cluster,
		wckey_month_table, cluster, wckey_day_table, cluster);
	return create_function_xfree(db_conn, create_line);
}

static int
_create_function_init_last_ran(PGconn *db_conn, char *cluster)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION %s.init_last_ran (now INTEGER) "
		"RETURNS INTEGER AS $$"
		"DECLARE ins INTEGER; ret INTEGER;"
		"BEGIN "
		"  SELECT time_start INTO ins FROM %s.%s "
		"    ORDER BY time_start LIMIT 1; "
		"  IF FOUND THEN "
		"    ret := ins;"
		"  ELSE "
		"    ins := now; ret := -1;"
		"  END IF; "
		"  INSERT INTO %s.%s (hourly_rollup, daily_rollup, "
		"    monthly_rollup) "
		"    VALUES(ins, ins, ins);"
		"  RETURN ret;"
		"END; $$ LANGUAGE PLPGSQL;", cluster, cluster,
		event_table, cluster, last_ran_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * check_usage_tables - check usage related tables and functions
 */
extern int
check_usage_tables(PGconn *db_conn, char *cluster)
{
	int rc = 0;

	rc |= check_table(db_conn, cluster, assoc_day_table,
			  assoc_usage_table_fields,
			  assoc_usage_table_constraint);
	rc |= check_table(db_conn, cluster, assoc_hour_table,
			  assoc_usage_table_fields,
			  assoc_usage_table_constraint);
	rc |= check_table(db_conn, cluster, assoc_month_table,
			  assoc_usage_table_fields,
			  assoc_usage_table_constraint);

	rc |= check_table(db_conn, cluster, cluster_day_table,
			  cluster_usage_table_fields,
			  cluster_usage_table_constraint);
	rc |= check_table(db_conn, cluster, cluster_hour_table,
			  cluster_usage_table_fields,
			  cluster_usage_table_constraint);
	rc |= check_table(db_conn, cluster, cluster_month_table,
			  cluster_usage_table_fields,
			  cluster_usage_table_constraint);

	rc |= check_table(db_conn, cluster, wckey_day_table,
			  wckey_usage_table_fields,
			  wckey_usage_table_constraint);
	rc |= check_table(db_conn, cluster, wckey_hour_table,
			  wckey_usage_table_fields,
			  wckey_usage_table_constraint);
	rc |= check_table(db_conn, cluster, wckey_month_table,
			  wckey_usage_table_fields,
			  wckey_usage_table_constraint);

	rc |= check_table(db_conn, cluster, last_ran_table,
			  last_ran_table_fields,
			  last_ran_table_constraint);

	rc |= _create_function_add_cluster_hour_usage(db_conn, cluster);
	rc |= _create_function_add_cluster_hour_usages(db_conn, cluster);
	rc |= _create_function_add_cluster_day_usage(db_conn, cluster);
	rc |= _create_function_add_cluster_month_usage(db_conn, cluster);
	rc |= _create_function_cluster_daily_rollup(db_conn, cluster);
	rc |= _create_function_cluster_monthly_rollup(db_conn, cluster);

	rc |= _create_function_add_assoc_hour_usage(db_conn, cluster);
	rc |= _create_function_add_assoc_hour_usages(db_conn, cluster);
	rc |= _create_function_add_assoc_day_usage(db_conn, cluster);
	rc |= _create_function_add_assoc_month_usage(db_conn, cluster);
	rc |= _create_function_assoc_daily_rollup(db_conn, cluster);
	rc |= _create_function_assoc_monthly_rollup(db_conn, cluster);

	rc |= _create_function_add_wckey_hour_usage(db_conn, cluster);
	rc |= _create_function_add_wckey_hour_usages(db_conn, cluster);
	rc |= _create_function_add_wckey_day_usage(db_conn, cluster);
	rc |= _create_function_add_wckey_month_usage(db_conn, cluster);
	rc |= _create_function_wckey_daily_rollup(db_conn, cluster);
	rc |= _create_function_wckey_monthly_rollup(db_conn, cluster);

	rc |= _create_function_init_last_ran(db_conn, cluster);
	return rc;
}

/*
 * _get_assoc_usage - get association usage data
 *
 * IN pg_conn: database connection
 * IN uid: user performing get operation
 * IN/OUT slurmdb_assoc: data of which association to get
 * IN start: start time
 * IN end: end time
 * RET: error code
 */
static int
_get_assoc_usage(pgsql_conn_t *pg_conn, uid_t uid,
		 slurmdb_association_rec_t *slurmdb_assoc,
		 time_t start, time_t end)
{
	DEF_VARS;
	int rc = SLURM_SUCCESS, is_admin=1;
	char *usage_table = NULL, *cluster = NULL;
	slurmdb_user_rec_t user;
	enum {
		F_ID,
		F_START,
		F_ACPU,
		F_COUNT
	};

	if (!slurmdb_assoc->cluster) {
		error("We need an cluster to set data for getting usage");
		return SLURM_ERROR;
	}
	cluster = slurmdb_assoc->cluster;
	if(!slurmdb_assoc->id) {
		error("We need an assoc id to set data for getting usage");
		return SLURM_ERROR;
	}

	if (check_user_op(pg_conn, uid, PRIVATE_DATA_USAGE, &is_admin, &user)
	    != SLURM_SUCCESS) {
		error("as/pg: user(%u) not found", uid);
		errno = ESLURM_USER_ID_MISSING;
		return SLURM_ERROR;
	}
	
	if (!is_admin) {
		ListIterator itr = NULL;
		slurmdb_coord_rec_t *coord = NULL;

		if(slurmdb_assoc->user &&
		   !strcmp(slurmdb_assoc->user, user.name))
			goto is_user;

		if(!user.coord_accts) {
			debug4("This user isn't a coord.");
			goto bad_user;
		}
		if(!slurmdb_assoc->acct) {
			debug("No account name given "
			      "in association.");
			goto bad_user;
		}
		itr = list_iterator_create(user.coord_accts);
		while((coord = list_next(itr))) {
			if(!strcasecmp(coord->name,
				       slurmdb_assoc->acct))
				break;
		}
		list_iterator_destroy(itr);
		if(coord)
			goto is_user;

	bad_user:
		errno = ESLURM_ACCESS_DENIED;
		return SLURM_ERROR;
	}

is_user:
	usage_table = assoc_day_table;
	if(set_usage_information(&usage_table, DBD_GET_ASSOC_USAGE,
				 &start, &end) != SLURM_SUCCESS)
		return SLURM_ERROR;

	query = xstrdup_printf(
		"SELECT t3.id_assoc, t1.time_start, t1.alloc_cpu_secs "
		"FROM %s.%s AS t1, %s.%s AS t2, %s.%s AS t3 "
		"WHERE (t1.time_start < %ld AND t1.time_start >= %ld) "
		"AND t1.id_assoc=t2.id_assoc AND t3.id=%d AND "
		"(t2.lft BETWEEN t3.lft AND t3.rgt) "
		"ORDER BY t3.id_assoc, t1.time_start;",
		cluster, usage_table, cluster, assoc_table, cluster,
		assoc_table, end, start, slurmdb_assoc->id);
	result = DEF_QUERY_RET;
	if(!result)
		return SLURM_ERROR;

	if(!slurmdb_assoc->accounting_list)
		slurmdb_assoc->accounting_list =
			list_create(slurmdb_destroy_accounting_rec);

	FOR_EACH_ROW {
		slurmdb_accounting_rec_t *accounting_rec =
			xmalloc(sizeof(slurmdb_accounting_rec_t));
		accounting_rec->id = atoi(ROW(F_ID));
		accounting_rec->period_start = atoi(ROW(F_START));
		accounting_rec->alloc_secs = atoll(ROW(F_ACPU));
		list_append(slurmdb_assoc->accounting_list, accounting_rec);
	} END_EACH_ROW;
	PQclear(result);

	return rc;
}

/*
 * _get_wckey_usage - get wckey usage data
 *
 * IN pg_conn: database connection
 * IN uid: user performing get operation
 * IN/OUT slurmdb_wckey: usage data of which wckey to get
 * IN start: start time
 * IN end: end time
 * RET: error code
 */
static int
_get_wckey_usage(pgsql_conn_t *pg_conn, uid_t uid,
		 slurmdb_wckey_rec_t *slurmdb_wckey,
		 time_t start, time_t end)
{
	DEF_VARS;
	int rc = SLURM_SUCCESS, is_admin=1;
	char *usage_table = NULL, *cluster = NULL;
	slurmdb_user_rec_t user;
	enum {
		F_ID,
		F_START,
		F_ACPU,
		F_COUNT
	};

	if (!slurmdb_wckey->cluster) {
		error("We need an cluster to set data for getting usage");
		return SLURM_ERROR;
	}
	cluster = slurmdb_wckey->cluster;
	if(!slurmdb_wckey->id) {
		error("We need an wckey id to set data for getting usage");
		return SLURM_ERROR;
	}

	if (check_user_op(pg_conn, uid, PRIVATE_DATA_USAGE, &is_admin, &user)
	    != SLURM_SUCCESS) {
		error("as/pg: user(%u) not found in db", uid);
		errno = ESLURM_USER_ID_MISSING;
		return SLURM_ERROR;
	}
	
	if (!is_admin) {
		if(! slurmdb_wckey->user ||
		   strcmp(slurmdb_wckey->user, user.name)) {
			errno = ESLURM_ACCESS_DENIED;
			return SLURM_ERROR;
		}
	}

	usage_table = wckey_day_table;
	if(set_usage_information(&usage_table, DBD_GET_WCKEY_USAGE,
				 &start, &end) != SLURM_SUCCESS) {
		return SLURM_ERROR;
	}

	query = xstrdup_printf(
		"SELECT id_wckey, time_start, alloc_cpu_secs FROM %s.%s "
		"WHERE (time_start < %ld AND time_start >= %ld) "
		"AND id_wckey=%d ORDER BY id_wckey, time_start;", cluster, 
		usage_table, end, start, slurmdb_wckey->id);
	result = DEF_QUERY_RET;
	if(!result)
		return SLURM_ERROR;

	if(!slurmdb_wckey->accounting_list)
		slurmdb_wckey->accounting_list =
			list_create(slurmdb_destroy_accounting_rec);

	FOR_EACH_ROW {
		slurmdb_accounting_rec_t *accounting_rec =
			xmalloc(sizeof(slurmdb_accounting_rec_t));
		accounting_rec->id = atoi(ROW(F_ID));
		accounting_rec->period_start = atoi(ROW(F_START));
		accounting_rec->alloc_secs = atoll(ROW(F_ACPU));
		list_append(slurmdb_wckey->accounting_list, accounting_rec);
	} END_EACH_ROW;
	PQclear(result);
	return rc;
}

/*
 * _get_cluster_usage - get cluster usage data
 *
 * IN pg_conn: database connection
 * IN uid: user performing get operation
 * IN/OUT slurmdb_wckey: usage data of which cluster to get
 * IN start: start time
 * IN end: end time
 * RET: error code
 */
static int
_get_cluster_usage(pgsql_conn_t *pg_conn, uid_t uid,
		 slurmdb_cluster_rec_t *cluster_rec,
		 time_t start, time_t end)
{
	DEF_VARS;
        int rc = SLURM_SUCCESS;
        char *usage_table = cluster_day_table;
	char *gu_fields = "alloc_cpu_secs,down_cpu_secs,pdown_cpu_secs,"
		"idle_cpu_secs,resv_cpu_secs,over_cpu_secs,cpu_count,"
		"time_start";
        enum {
                F_ACPU,
                F_DCPU,
                F_PDCPU,
                F_ICPU,
                F_RCPU,
                F_OCPU,
                F_CPU_COUNT,
                F_START,
                F_COUNT
        };

        if(!cluster_rec->name || !cluster_rec->name[0]) {
                error("We need a cluster name to set data for");
                return SLURM_ERROR;
        }

        if(set_usage_information(&usage_table, DBD_GET_CLUSTER_USAGE,
				 &start, &end) != SLURM_SUCCESS) {
                return SLURM_ERROR;
        }

        query = xstrdup_printf(
                "SELECT %s FROM %s.%s WHERE (time_start<%ld "
                "AND time_start>=%ld)",
                gu_fields, cluster_rec->name, usage_table, end, start);

	result = DEF_QUERY_RET;
	if (!result)
		return SLURM_ERROR;

	if(!cluster_rec->accounting_list)
                cluster_rec->accounting_list =
                        list_create(slurmdb_destroy_cluster_accounting_rec);

	FOR_EACH_ROW {
                slurmdb_cluster_accounting_rec_t *accounting_rec =
                        xmalloc(sizeof(slurmdb_cluster_accounting_rec_t));
                accounting_rec->alloc_secs = atoll(ROW(F_ACPU));
                accounting_rec->down_secs = atoll(ROW(F_DCPU));
                accounting_rec->pdown_secs = atoll(ROW(F_PDCPU));
                accounting_rec->idle_secs = atoll(ROW(F_ICPU));
                accounting_rec->over_secs = atoll(ROW(F_OCPU));
                accounting_rec->resv_secs = atoll(ROW(F_RCPU));
                accounting_rec->cpu_count = atoi(ROW(F_CPU_COUNT));
                accounting_rec->period_start = atoi(ROW(F_START));
                list_append(cluster_rec->accounting_list, accounting_rec);
        } END_EACH_ROW;

        return rc;
}

/*
 * as_pg_get_usage - get cluster usage
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
as_pg_get_usage(pgsql_conn_t *pg_conn, uid_t uid, void *in,
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
	case DBD_GET_CLUSTER_USAGE:
		rc = _get_cluster_usage(pg_conn, uid, in, start, end);
		break;
	default:
		error("Unknown usage type %d", type);
		rc = SLURM_ERROR;
		break;
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
get_usage_for_assoc_list(pgsql_conn_t *pg_conn, char *cluster, List assoc_list,
			 time_t start, time_t end)
{
	DEF_VARS;
	int rc = SLURM_SUCCESS;
	char *usage_table = NULL, *id_str = NULL;
	List usage_list = NULL;
	ListIterator itr = NULL, u_itr = NULL;
	slurmdb_association_rec_t *assoc = NULL;
	slurmdb_accounting_rec_t *accounting_rec = NULL;
	enum {
		F_ID,
		F_START,
		F_ACPU,
		F_COUNT
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
			xstrfmtcat(id_str, " OR t3.id_assoc=%d", assoc->id);
		else
			xstrfmtcat(id_str, "t3.id_assoc=%d", assoc->id);
	}
	list_iterator_destroy(itr);

	query = xstrdup_printf(
		"SELECT t3.id_assoc, t1.time_start, t1.alloc_cpu_secs "
		"FROM %s.%s AS t1, %s.%s AS t2, %s.%s AS t3 "
		"WHERE (t1.time_start < %ld AND t1.time_start >= %ld) "
		"AND t1.id_assoc=t2.id_assoc AND (%s) AND "
		"(t2.lft between t3.lft and t3.rgt) "
		"ORDER BY t3.id_assoc, time_start;",
		cluster, usage_table, cluster, assoc_table, cluster,
		assoc_table, end, start, id_str);
	xfree(id_str);
	result = DEF_QUERY_RET;
	if(!result)
		return SLURM_ERROR;

	usage_list = list_create(slurmdb_destroy_accounting_rec);
	FOR_EACH_ROW {
		slurmdb_accounting_rec_t *accounting_rec =
			xmalloc(sizeof(slurmdb_accounting_rec_t));
		accounting_rec->id = atoi(ROW(F_ID));
		accounting_rec->period_start = atoi(ROW(F_START));
		accounting_rec->alloc_secs = atoll(ROW(F_ACPU));
		list_append(usage_list, accounting_rec);
	} END_EACH_ROW;
	PQclear(result);

	u_itr = list_iterator_create(usage_list);
	itr = list_iterator_create(assoc_list);
	while((assoc = list_next(itr))) {
		int found = 0;
		if(!assoc->accounting_list)
			assoc->accounting_list = list_create(
				slurmdb_destroy_accounting_rec);
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
get_usage_for_wckey_list(pgsql_conn_t *pg_conn, char *cluster, List wckey_list,
			 time_t start, time_t end)
{
	int rc = SLURM_SUCCESS;
	PGresult *result = NULL;
	char *usage_table = NULL, *query = NULL, *id_str = NULL;
	List usage_list = NULL;
	ListIterator itr = NULL, u_itr = NULL;
	slurmdb_wckey_rec_t *wckey = NULL;
	slurmdb_accounting_rec_t *accounting_rec = NULL;
	enum {
		F_ID,
		F_START,
		F_ACPU,
		F_COUNT
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
			xstrfmtcat(id_str, " OR id_wckey=%d", wckey->id);
		else
			xstrfmtcat(id_str, "id_wckey=%d", wckey->id);
	}
	list_iterator_destroy(itr);

	query = xstrdup_printf(
		"SELECT id_wckey, time_start, alloc_cpu_secs FROM %s.%s "
		"WHERE (time_start < %ld AND time_start >= %ld) "
		"AND (%s) ORDER BY id_wckey, time_start;",
		cluster, usage_table, end, start, id_str);
	xfree(id_str);
	result = DEF_QUERY_RET;
	if(!result)
		return SLURM_ERROR;

	usage_list = list_create(slurmdb_destroy_accounting_rec);
	FOR_EACH_ROW {
		slurmdb_accounting_rec_t *accounting_rec =
			xmalloc(sizeof(slurmdb_accounting_rec_t));
		accounting_rec->id = atoi(ROW(F_ID));
		accounting_rec->period_start = atoi(ROW(F_START));
		accounting_rec->alloc_secs = atoll(ROW(F_ACPU));
		list_append(usage_list, accounting_rec);
	} END_EACH_ROW;
	PQclear(result);

	u_itr = list_iterator_create(usage_list);
	itr = list_iterator_create(wckey_list);
	while((wckey = list_next(itr))) {
		int found = 0;
		if(!wckey->accounting_list)
			wckey->accounting_list = list_create(
				slurmdb_destroy_accounting_rec);
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

/*
 * cluster_delete_assoc_usage - mark usage records of given assoc as deleted
 * assoc_cond format: "id_assoc=name OR id_assoc=name..."
 */
extern int
cluster_delete_assoc_usage(pgsql_conn_t *pg_conn, char *cluster, time_t now,
			   char *assoc_cond)
{
	char *query = xstrdup_printf(
		"UPDATE %s.%s SET mod_time=%ld, deleted=1 WHERE (%s);"
		"UPDATE %s.%s SET mod_time=%ld, deleted=1 WHERE (%s);"
		"UPDATE %s.%s SET mod_time=%ld, deleted=1 WHERE (%s);",
		cluster, assoc_day_table, now, assoc_cond,
		cluster, assoc_hour_table, now, assoc_cond,
		cluster, assoc_month_table, now, assoc_cond);
	return DEF_QUERY_RET_RC;
}

