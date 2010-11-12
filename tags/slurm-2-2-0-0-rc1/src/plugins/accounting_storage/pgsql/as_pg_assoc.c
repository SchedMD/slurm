/*****************************************************************************\
 *  as_pg_assoc.c - accounting interface to pgsql - association
 *  related functions.
 *
 *  $Id: as_pg_assoc.c 13061 2008-01-22 21:23:56Z da $
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

#define SECS_PER_DAY	(24 * 60 * 60)

/* per-cluster table */
char *assoc_table = "assoc_table";
static storage_field_t assoc_table_fields[] = {
	{ "creation_time", "INTEGER NOT NULL" },
	{ "mod_time", "INTEGER DEFAULT 0 NOT NULL" },
	{ "deleted", "INTEGER DEFAULT 0" },
	{ "id_assoc", "SERIAL" }, /* "serial" is of range integer in PG */
	{ "acct", "TEXT NOT NULL" },
	{ "user_name", "TEXT NOT NULL DEFAULT ''" },
	{ "partition", "TEXT NOT NULL DEFAULT ''" },
	{ "parent_acct", "TEXT NOT NULL DEFAULT ''" },
	{ "lft", "INTEGER NOT NULL" },
	{ "rgt", "INTEGER NOT NULL" },
	{ "shares", "INTEGER DEFAULT 1 NOT NULL" },
	{ "max_jobs", "INTEGER DEFAULT NULL" },
	{ "max_submit_jobs", "INTEGER DEFAULT NULL" },
	{ "max_cpus_pj", "INTEGER DEFAULT NULL" },
	{ "max_nodes_pj", "INTEGER DEFAULT NULL" },
	{ "max_wall_pj", "INTEGER DEFAULT NULL" },
	{ "max_cpu_mins_pj", "BIGINT DEFAULT NULL" },
	{ "max_cpu_run_mins", "BIGINT DEFAULT NULL" },
	{ "grp_jobs", "INTEGER DEFAULT NULL" },
	{ "grp_submit_jobs", "INTEGER DEFAULT NULL" },
	{ "grp_cpus", "INTEGER DEFAULT NULL" },
	{ "grp_nodes", "INTEGER DEFAULT NULL" },
	{ "grp_wall", "INTEGER DEFAULT NULL" },
	{ "grp_cpu_mins", "BIGINT DEFAULT NULL" },
	{ "grp_cpu_run_mins", "BIGINT DEFAULT NULL" },
	{ "def_qos_id", "INTEGER DEFAULT NULL" },
	{ "qos", "TEXT NOT NULL DEFAULT ''" }, /* why blob(bytea in pg)? */
	{ "delta_qos", "TEXT NOT NULL DEFAULT ''" },
	{ NULL, NULL}
};
static char *assoc_table_constraints = ", "
	"PRIMARY KEY (id_assoc), "
	"UNIQUE (user_name, acct, partition), "
	"CHECK (partition='' OR user_name != ''), "
	"CHECK ((user_name='' AND parent_acct!='') "
	"  OR (user_name!='' AND parent_acct='') OR "
	"  (acct='root' AND user_name='' AND parent_acct='')), "
	"CHECK (qos='' OR delta_qos='')"
	")";

static char *max_rgt_table = "assoc_max_rgt_table";
static storage_field_t max_rgt_table_fields[] = {
	{"max_rgt", "INTEGER NOT NULL" },
	{NULL, NULL}
};
static char *max_rgt_table_constraints = ")";


/*
 * _create_function_show_assoc_hierarchy - create a SQL function to
 *    show associations in hierarchy. for debug.
 *
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_show_assoc_hierarchy(PGconn *db_conn, char *cluster)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION %s.show_assoc_hierarchy () "
		"RETURNS SETOF TEXT AS $$ "
		"  SELECT (CASE COUNT(p.acct) WHEN 1 THEN '' "
		"          ELSE repeat(' ', "
		"                 5*(CAST(COUNT(p.acct) AS INTEGER)-1)) "
		"               || ' |____ ' END) || c.id_assoc || "
		"      E':<\\'' || '%s' || E'\\', \\'' || c.acct || "
		"      E'\\', \\'' || c.user_name || E'\\', \\'' || "
		"      c.partition || E'\\'>'|| '[' || c.lft || ',' || "
		"      c.rgt || ']' "
		"    FROM %s.assoc_table AS p, %s.assoc_table AS c "
		"    WHERE c.lft BETWEEN p.lft AND p.rgt "
		"    GROUP BY c.acct, c.user_name, c.partition, "
		"      c.lft, c.rgt, c.id_assoc"
		"    ORDER BY c.lft;"
		"$$ LANGUAGE SQL;", cluster, cluster, cluster, cluster);
	return create_function_xfree(db_conn, create_line);
}

static int
_create_function_remove_assoc(PGconn *db_conn, char *cluster)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION %s.remove_assoc (aid INTEGER) "
		"  RETURNS VOID AS $$"
		"DECLARE"
		"  width INTEGER; alft INTEGER; argt INTEGER;"
		"BEGIN "
		"  SELECT lft, rgt, (rgt-lft+1) INTO alft, argt, width "
		"    FROM %s.%s WHERE id_assoc=aid;"
		"  DELETE FROM %s.%s WHERE lft BETWEEN alft AND argt;"
		"  UPDATE %s.%s SET rgt=rgt-width WHERE rgt > alft;"
		"  UPDATE %s.%s SET lft=lft-width WHERE lft > alft;"
		"END; $$ LANGUAGE PLPGSQL;", cluster,
		cluster, assoc_table, cluster, assoc_table,
		cluster, assoc_table, cluster, assoc_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _create_function_move_account - create a PL/PGSQL function to move account
 *
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_move_account(PGconn *db_conn, char *cluster)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION %s.move_account (plft INTEGER, "
		"INOUT alft INTEGER, INOUT argt INTEGER, "
		"aid INTEGER, pacct TEXT, mtime INTEGER) AS $$"
		"DECLARE"
		"  diff INTEGER; width INTEGER;"
		"BEGIN "
		"  diff := plft - alft + 1;"
		"  width := argt - alft + 1;"
		""
		"  -- insert to new positon and delete from old position\n"
		"  UPDATE %s.%s "
		"    SET mod_time=mtime, deleted=deleted+2, lft=lft+diff, "
		"      rgt=rgt+diff"
		"    WHERE lft BETWEEN alft AND argt;"
		""
		"  -- make space for the insertion\n"
		"  UPDATE %s.%s "
		"    SET mod_time=mtime, rgt=rgt+width "
		"    WHERE rgt>plft AND deleted<2; "
		"  UPDATE %s.%s "
		"    SET mod_time=mtime, lft=lft+width "
		"    WHERE lft>plft AND deleted<2; "
		""
		"  -- reclaim space for the deletion\n"
		"  UPDATE %s.%s "
		"    SET mod_time=mtime, rgt=rgt-width "
		"    WHERE rgt>argt; "
		"  UPDATE %s.%s "
		"    SET mod_time=mtime, lft=lft-width "
		"    WHERE lft>argt; "
		""
		"  -- clear the deleted flag\n"
		"  UPDATE %s.%s "
		"    SET deleted=deleted-2 "
		"    WHERE deleted>1; "
		""
		"  -- set the parent_acct field\n"
		"  -- get new lft & rgt\n"
		"  UPDATE %s.%s "
		"    SET mod_time=mtime, parent_acct=pacct "
		"    WHERE id_assoc=aid "
		"    RETURNING lft,rgt INTO alft,argt;"
		"END; $$ LANGUAGE PLPGSQL;", cluster,
		cluster, assoc_table, cluster, assoc_table,
		cluster, assoc_table, cluster, assoc_table,
		cluster, assoc_table, cluster, assoc_table,
		cluster, assoc_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _create_function_make_space - create a PL/PGSQL function to make space
 *
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_make_space(PGconn *db_conn, char *cluster)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION %s.make_space (plft INTEGER, "
		"incr INTEGER) RETURNS VOID AS $$ "
		"BEGIN "
		"  UPDATE %s.%s SET rgt=rgt+incr "
		"    WHERE rgt > plft AND deleted < 2;"
		"  UPDATE %s.%s SET lft=lft+incr "
		"    WHERE lft > plft AND deleted < 2;"
		"  UPDATE %s.%s SET deleted=0 WHERE deleted=2;"
		"  UPDATE %s.%s SET max_rgt=max_rgt+incr;"
		"END; $$ LANGUAGE PLPGSQL;", cluster,
		cluster, assoc_table, cluster, assoc_table,
		cluster, assoc_table, cluster, max_rgt_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _create_function_add_assoc - create a PL/PGSQL function to add association
 *
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_add_assoc(PGconn *db_conn, char *cluster)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION %s.add_assoc (na %s.%s) "
		"RETURNS INTEGER AS $$ "
		"DECLARE"
		"  na_id INTEGER;"
		"BEGIN LOOP "
		"  BEGIN "
		"    INSERT INTO %s.%s (creation_time, mod_time, deleted, "
		"        id_assoc, acct, user_name, partition, parent_acct, "
		"        lft, rgt, shares, max_jobs, max_submit_jobs, "
		"        max_cpus_pj, max_nodes_pj, "
		"        max_wall_pj, max_cpu_mins_pj, "
		"        grp_jobs, grp_submit_jobs, grp_cpus, grp_nodes, "
		"        grp_wall, grp_cpu_mins, qos, delta_qos) "
		"      VALUES (na.creation_time, na.mod_time, na.deleted, "
		"        DEFAULT, na.acct, na.user_name,"
		"        na.partition, na.parent_acct, na.lft, na.rgt, "
		"        na.shares, na.max_jobs, na.max_submit_jobs, "
		"        na.max_cpus_pj, na.max_nodes_pj, "
		"        na.max_wall_pj, "
		"        na.max_cpu_mins_pj, na.grp_jobs, "
		"        na.grp_submit_jobs, na.grp_cpus, na.grp_nodes, "
		"        na.grp_wall, na.grp_cpu_mins, na.qos, na.delta_qos) "
		"      RETURNING id_assoc INTO na_id;"
		"    RETURN na_id; "
		"  EXCEPTION WHEN UNIQUE_VIOLATION THEN "
		"    UPDATE %s.%s "
		"      SET mod_time=na.mod_time, deleted=0, "
		"        shares=na.shares, "
		"        max_jobs=na.max_jobs, "
		"        max_submit_jobs=na.max_submit_jobs,"
		"        max_cpus_pj=na.max_cpus_pj, "
		"        max_nodes_pj=na.max_nodes_pj, "
		"        max_wall_pj=na.max_wall_pj,"
		"        max_cpu_mins_pj=na.max_cpu_mins_pj, "
		"        grp_jobs=na.grp_jobs, "
		"        grp_submit_jobs=na.grp_submit_jobs, "
		"        grp_cpus=na.grp_cpus, grp_nodes=na.grp_nodes, "
		"        grp_wall=na.grp_wall, grp_cpu_mins=na.grp_cpu_mins, "
		"        qos=na.qos, delta_qos=na.delta_qos "
		"      WHERE acct=na.acct AND "
		"        user_name=na.user_name AND partition=na.partition"
		"      RETURNING id_assoc INTO na_id;"
		"    IF FOUND THEN RETURN na_id; END IF;"
		"  END; "
		"END LOOP; END; $$ LANGUAGE PLPGSQL;",
		cluster, cluster, assoc_table, cluster, assoc_table,
		cluster, assoc_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _create_function_add_assoc_update - create a PL/PGSQL function
 *   to update association when adding association
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_add_assoc_update(PGconn *db_conn, char *cluster)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION %s.add_assoc_update (assoc %s.%s) "
		"RETURNS INTEGER AS $$ "
		"DECLARE aid INTEGER;"
		"BEGIN "
		"  UPDATE %s.%s SET mod_time=assoc.mod_time, deleted=0, "
		"    id_assoc=nextval('%s.%s_id_assoc_seq'), shares=assoc.shares, "
		"    max_jobs=assoc.max_jobs, "
		"    max_submit_jobs=assoc.max_submit_jobs,"
		"    max_cpus_pj=assoc.max_cpus_pj, "
		"    max_nodes_pj=assoc.max_nodes_pj, "
		"    max_wall_pj=assoc.max_wall_pj,"
		"    max_cpu_mins_pj=assoc.max_cpu_mins_pj, "
		"    grp_jobs=assoc.grp_jobs, "
		"    grp_submit_jobs=assoc.grp_submit_jobs, "
		"    grp_cpus=assoc.grp_cpus, grp_nodes=assoc.grp_nodes, "
		"    grp_wall=assoc.grp_wall, grp_cpu_mins=assoc.grp_cpu_mins, "
		"    qos=assoc.qos, delta_qos=assoc.delta_qos "
		"  WHERE acct=assoc.acct AND "
		"    user_name=assoc.user_name AND partition=assoc.partition"
		"  RETURNING id_assoc INTO aid;"
		"  RETURN aid;"
		"END; $$ LANGUAGE PLPGSQL;", cluster, cluster,
		assoc_table, cluster, assoc_table, cluster, assoc_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _create_function_add_root_assoc - create a PL/PGSQL function to add
 *   root account association
 *
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_add_root_assoc(PGconn *db_conn, char *cluster)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION %s.add_root_assoc(ra %s.%s) "
		"RETURNS VOID AS $$"
		"DECLARE "
		"  mrgt INTEGER;"
		"BEGIN "
		"  UPDATE %s.%s SET max_rgt=max_rgt+2 RETURNING max_rgt INTO mrgt;"
		"  ra.lft := mrgt - 1;"
		"  ra.rgt := mrgt;"
		"  PERFORM %s.add_assoc(ra);"
		"END; $$ LANGUAGE PLPGSQL;", cluster, cluster,
		assoc_table, cluster, max_rgt_table, cluster);
	return create_function_xfree(db_conn, create_line);
}

static int
_create_function_delete_assoc(PGconn *db_conn, char *cluster)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION %s.delete_assoc(aid INTEGER) "
		"RETURNS VOID AS $$"
		"DECLARE "
		"  alft INTEGER; argt INTEGER; awid INTEGER;"
		"BEGIN "
		"  SELECT lft, rgt, (rgt - lft + 1) INTO alft, argt, awid "
		"    FROM %s.%s WHERE id_assoc=aid;"
		"  IF NOT FOUND THEN RETURN; END IF;"
		"  DELETE FROM %s.%s WHERE lft BETWEEN alft AND argt;"
		"  UPDATE %s.%s SET rgt = rgt - awid WHERE rgt > argt;"
		"  UPDATE %s.%s SET lft = lft - awid WHERE lft > argt;"
		"  UPDATE %s.%s SET max_rgt=max_rgt-awid;"
		"END; $$ LANGUAGE PLPGSQL;", cluster, cluster,
		assoc_table, cluster, assoc_table, cluster, assoc_table,
		cluster, assoc_table, cluster, max_rgt_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _create_function_init_max_rgt_table - create a PL/PGSQL function to
 *   initialize max_rgt_table
 *
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_init_max_rgt_table(PGconn *db_conn, char *cluster)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION %s.init_max_rgt_table() "
		"RETURNS VOID AS $$"
		"BEGIN "
		"  PERFORM * FROM %s.%s LIMIT 1;"
		"  IF FOUND THEN RETURN; END IF;"
		"  INSERT INTO %s.%s VALUES (0);"
		"END; $$ LANGUAGE PLPGSQL;", cluster, cluster,
		max_rgt_table, cluster, max_rgt_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _create_function_get_parent_limits - create a PL/PGSQL function to
 *   get parent account resource limits
 *
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_get_parent_limits(PGconn *db_conn, char *cluster)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION %s.get_parent_limits( "
		"  pacct TEXT, OUT mj INTEGER, OUT msj INTEGER, "
		"  OUT mcpj INTEGER, OUT mnpj INTEGER, OUT mwpj INTEGER, "
		"  OUT mcmpj BIGINT, OUT mcrm BIGINT, OUT def_qid INTEGER, "
		"  OUT aqos TEXT, OUT delta TEXT) "
		"AS $$"
		"DECLARE "
		"  my_acct TEXT;"
		"BEGIN "
		"  aqos := '';"
		"  delta := '';"
		"  my_acct := pacct;"
		"  WHILE (my_acct!='') AND ((mj IS NULL) OR (msj IS NULL) OR "
		"         (mcpj IS NULL) OR (mnpj IS NULL) OR (mwpj IS NULL) OR "
		"         (mcmpj IS NULL) OR (mcrm IS NULL) OR (def_qid IS NULL) "
		"         OR (aqos='')) LOOP "
		"    SELECT parent_acct, COALESCE(mj, max_jobs), "
		"           COALESCE(msj, max_submit_jobs), "
		"           COALESCE(mcpj, max_cpus_pj), "
		"           COALESCE(mnpj, max_nodes_pj), "
		"           COALESCE(mwpj, max_wall_pj), "
		"           COALESCE(mcmpj, max_cpu_mins_pj), "
		"           COALESCE(mcrm, max_cpu_run_mins), "
		"           COALESCE(def_qid, def_qos_id), "
		"           CASE aqos WHEN '' THEN qos ELSE aqos END, "
		"           CASE aqos WHEN '' THEN (delta_qos || delta) "
		"                             ELSE delta END "
		"      INTO my_acct, mj, msj, mcpj, mnpj, mwpj, mcmpj, mcrm, "
		"           def_qid, aqos, delta FROM %s.%s "
		"      WHERE acct=my_acct AND user_name='' ;"
		"  END LOOP;"
		"END; $$ LANGUAGE PLPGSQL;",
		cluster, cluster, assoc_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _init_max_rgt_table - insert a init value into max rgt table
 *
 * IN db_conn: database connection
 * RET: error code
 */
static int
_init_max_rgt_table(PGconn *db_conn, char *cluster)
{
	int rc;
	char * query;

	query = xstrdup_printf("SELECT %s.init_max_rgt_table();", cluster);
	rc = pgsql_db_query(db_conn, query);
	xfree(query);
	return rc;
}

/*
 * _dump_assoc - show all associations in table
 * IN pg_conn: database connection
 */
/* static void */
/* _dump_assoc(pgsql_conn_t *pg_conn, char *cluster) */
/* { */
/* 	DEF_VARS; */

/* 	query = xstrdup_printf("SELECT %s.show_assoc_hierarchy();", cluster); */
/* 	result = DEF_QUERY_RET; */
/* 	if (! result) */
/* 		fatal("as/pg: unable to dump assoc"); */

/* 	debug3("==================== association dump ===================="); */
/* 	FOR_EACH_ROW { */
/* 		debug3("%s", ROW(0)); */
/* 	} END_EACH_ROW; */
/* 	debug3("=========================================================="); */
/* 	PQclear(result); */
/* } */

/*
 * _make_assoc_rec - make an assoc_table record from assoc
 *
 * IN assoc: association structure
 * IN now: current time
 * IN deleted: deleted value of table record
 * OUT rec: record string
 * OUT txn: txn string
 */
static void
_make_assoc_rec(slurmdb_association_rec_t *assoc, time_t now, int deleted,
		char **rec, char **txn)
{
	*rec = xstrdup_printf("(%ld, %ld, %d, %d, '%s', ",
			      now,              /* creation_time */
			      now,              /* mod_time */
			      deleted,  /* deleted */
			      assoc->id, /* id_assoc */
			      assoc->acct       /* acct */
		);
	*txn = xstrdup_printf("cluster='%s', acct='%s'",
			      assoc->cluster, assoc->acct);

	if (assoc->user) {	/* user association */
		xstrfmtcat(*rec, "'%s', '%s', '', ",
			   assoc->user, /* user_name, default '' */
			   assoc->partition ?: "" /* partition, default '' */
			   /* parent_acct is '' */
			);
		xstrfmtcat(*txn, ", user_name='%s', partition='%s'",
			   assoc->user, assoc->partition ?: "''");
	} else {
		xstrfmtcat(*rec, "'', '', '%s', ",
			   /* user_name is '' */
			   /* partition is '' */
			   assoc->parent_acct ?: "root" /* parent_acct,
							 * default "root" */
			);
		xstrfmtcat(*txn, ", user_name='', parent_acct='%s'",
			   assoc->parent_acct ?: "root");
	}

	xstrfmtcat(*rec, "%d, %d, ", assoc->lft, assoc->rgt); /* lft, rgt */

	if (assoc->shares_raw == INFINITE)
		assoc->shares_raw = 1;
	if ((int32_t)assoc->shares_raw >= 0) {
		xstrfmtcat(*rec, "%u, ", assoc->shares_raw);
		xstrfmtcat(*txn, ", shares=%u", assoc->shares_raw);
	} else {
		xstrcat(*rec, "1, "); /* shares, default 1 */
	}

	concat_limit_32("max_jobs", assoc->max_jobs, rec, txn);
	concat_limit_32("max_submit_jobs", assoc->max_submit_jobs, rec, txn);
	concat_limit_32("max_cpus_pj", assoc->max_cpus_pj, rec, txn);
	concat_limit_32("max_nodes_pj", assoc->max_nodes_pj, rec, txn);
	concat_limit_32("max_wall_pj", assoc->max_wall_pj, rec, txn);
	concat_limit_64("max_cpu_mins_pj", assoc->max_cpu_mins_pj, rec, txn);
	concat_limit_64("max_cpu_run_mins", assoc->max_cpu_run_mins, rec, txn);
	concat_limit_32("grp_jobs", assoc->grp_jobs, rec, txn);
	concat_limit_32("grp_submit_jobs", assoc->grp_submit_jobs, rec, txn);
	concat_limit_32("grp_cpus", assoc->grp_cpus, rec, txn);
	concat_limit_32("grp_nodes", assoc->grp_nodes, rec, txn);
	concat_limit_32("grp_wall", assoc->grp_wall, rec, txn);
	concat_limit_64("grp_cpu_mins", assoc->grp_cpu_mins, rec, txn);
	concat_limit_64("grp_cpu_run_mins", assoc->grp_cpu_run_mins, rec, txn);

	if(assoc->def_qos_id == INFINITE) {
		xstrcat(*rec, "NULL, ");
		xstrcat(*txn, ", def_qos_id=NULL");
		/* 0 is the no def_qos_id, so it that way */
		assoc->def_qos_id = 0;
	} else if((assoc->def_qos_id != NO_VAL)
		  && ((int32_t)assoc->def_qos_id > 0)) {
		xstrfmtcat(*rec, "%u, ", assoc->def_qos_id);
		xstrfmtcat(*txn, ", def_qos_id=%u", assoc->def_qos_id);
	} else {
		xstrcat(*rec, "NULL, ");
	}

	/* qos, delta_qos, default ''. only called in add_associations() */
	if (assoc->qos_list && list_count(assoc->qos_list)) {
		char *qos_val = NULL;
		char *tmp = NULL;
		int delta = 0;
		ListIterator itr = list_iterator_create(assoc->qos_list);
		while((tmp = list_next(itr))) {
			if (!tmp[0])
				continue;
			if(!delta && (tmp[0] == '+' || tmp[0] == '-'))
				delta = 1;
			/* XXX: always with ',' prefix */
			xstrfmtcat(qos_val, ",%s", tmp);
		}
		list_iterator_destroy(itr);

		xstrfmtcat(*rec, delta ? "'', '%s')" : "'%s', '')",
			   qos_val ?: "");
		xstrfmtcat(*txn, ", %s='%s'",
			   delta ? "delta_qos" : "qos",
			   qos_val ?: "");
		xfree(qos_val);
	} else {
		xstrcat(*rec, "'', '')");
	}
}

/*
 * _make_cluster_root_assoc_rec - make record for root association of cluster
 *
 * IN now: current time
 * IN cluster: cluster object
 * OUT rec: record string
 * OUT txn: txn string for cluster addition
 */
static void
_make_cluster_root_assoc_rec(time_t now, slurmdb_cluster_rec_t *cluster,
			     char **rec, char **txn)
{
	*rec = xstrdup_printf(
		"(%ld, %ld, 0, 0, 'root', '', '', '', 0, 0, ",
		now, /* creation_time */
		now /* mod_time */
		/* deleted = 0 */
		/* id generated */
		/* acct = 'root' */
		/* user_name = '' */
		/* partition = '' */
		/* parent_acct = '' */
		/* lft calc later */
		/* rgt calc later */);
	if (! cluster->root_assoc) { /* all fields take default value */
		xstrfmtcat(*rec, "1, "  /* fairshare */
			   "NULL, "     /* max_jobs */
			   "NULL, "     /* max_submit_jobs */
			   "NULL, "     /* max_cpus_per_job */
			   "NULL, "     /* max_nodes_per_job */
			   "NULL, "     /* max_wall_duration_per_job */
			   "NULL, "     /* max_cpu_mins_per_job */
			   "NULL, "     /* max_cpu_run_mins */
			   "NULL, "     /* grp_jobs */
			   "NULL, "     /* grp_submit_jobs */
			   "NULL, "     /* grp_cpus */
			   "NULL, "     /* grp_nodes */
			   "NULL, "     /* grp_wall */
			   "NULL, "     /* grp_cpu_mins */
			   "NULL, "     /* grp_cpu_run_mins */
			   "NULL, "     /* def_qos_id */
			   "'%s', "     /* qos */
			   "'')",	/* delta_qos */
			   default_qos_str ?: "");
	} else {
		slurmdb_association_rec_t *ra;
		ra = cluster->root_assoc;

		if ((int)(ra->shares_raw) >= 0) {
			xstrfmtcat(*rec, "%u, ", ra->shares_raw);
			xstrfmtcat(*txn, "shares=%u, ", ra->shares_raw);
		} else
			xstrcat(*rec, "1, ");

		concat_limit_32("max_jobs", ra->max_jobs, rec, txn);
		concat_limit_32("max_submit_jobs",
				ra->max_submit_jobs, rec, txn);
		concat_limit_32("max_cpus_pj", ra->max_cpus_pj, rec, txn);
		concat_limit_32("max_nodes_pj", ra->max_nodes_pj, rec, txn);
		concat_limit_32("max_wall_pj",
				ra->max_wall_pj, rec, txn);
		concat_limit_64("max_cpu_mins_pj",
				ra->max_cpu_mins_pj, rec, txn);
		concat_limit_64("max_cpu_run_mins",
				ra->max_cpu_run_mins, rec, txn);
		concat_limit_32("grp_jobs", ra->grp_jobs, rec, txn);
		concat_limit_32("grp_submit_jobs",
				ra->grp_submit_jobs, rec, txn);
		concat_limit_32("grp_cpus", ra->grp_cpus, rec, txn);
		concat_limit_32("grp_nodes", ra->grp_nodes, rec, txn);
		concat_limit_32("grp_wall", ra->grp_wall, rec, txn);
		concat_limit_64("grp_cpu_mins", ra->grp_cpu_mins, rec, txn);
		concat_limit_64("grp_cpu_run_mins",
				ra->grp_cpu_run_mins, rec, txn);

		if(ra->def_qos_id == INFINITE) {
			xstrcat(*rec, "NULL, ");
		} else if((ra->def_qos_id != NO_VAL)
			  && ((int32_t)ra->def_qos_id > 0)) {
			xstrfmtcat(*rec, "%u, ", ra->def_qos_id);
		} else {
			xstrcat(*rec, "NULL, ");
		}

		if (ra->qos_list && list_count(ra->qos_list)) {
			char *qos_val = NULL;
			char *tmp = NULL;
			int delta = 0;
			ListIterator itr = list_iterator_create(ra->qos_list);
			while((tmp = list_next(itr))) {
				if (!tmp[0])
					continue;
				if(!delta && (tmp[0] == '+' || tmp[0] == '-'))
					delta = 1;
				/* XXX: always with ',' prefix */
				xstrfmtcat(qos_val, ",%s", tmp);
			}
			list_iterator_destroy(itr);

			/* XXX: always set qos, not delta_qos */
			if (delta) {
				error("as/pg: delta_qos for "
				      "cluster root assoc");
				/*
				 * TODO: take delta to default_qos_str?
				 * or to ''?
				 */
				xstrcat(*rec, "'', '')");
				xfree(qos_val);
			} else if (qos_val) {
				xstrfmtcat(*rec, "'%s', '')", qos_val);
				xstrfmtcat(*txn, ", qos='%s'", qos_val);
				xfree(qos_val);
			} else if (default_qos_str) {
				xstrfmtcat(*rec, "'%s', '')", default_qos_str);
			} else {
				xstrcat(*rec, "'', '')");
			}
		} else {
			xstrfmtcat(*rec, "'%s', '')", default_qos_str ?: "");
		}
	}
}

/*
 * _make_space - update parent and sibling lft/rgt for newly added
 *   children associations
 *
 * IN pg_conn: database connection
 * IN parent_lft: lft of parent association
 * IN incr: increase of parent and sibling lft/rgt
 * RET: error code
 */
static inline int
_make_space(pgsql_conn_t *pg_conn, char *cluster, int parent_lft, int incr)
{
	char *query = xstrdup_printf(
		"SELECT %s.make_space(%d, %d);", cluster, parent_lft, incr);
	return DEF_QUERY_RET_RC;
}

/*
 * _get_parent_field - get field of parent association(<c, pa, '', ''>)
 * NOTE: caller should xfree the string returned
 */
static inline char *
_get_parent_field(pgsql_conn_t *pg_conn, char *cluster, char *pacct,
		  char *field)
{
	DEF_VARS;
	char *val = NULL;

	/* include deleted records for WithDeleted queries */
	query = xstrdup_printf(
		"SELECT %s FROM %s.%s WHERE acct='%s' AND user_name='';",
		field, cluster, assoc_table, pacct);
	result = DEF_QUERY_RET;
	if (!result)
		error("failed to get parent info");
	else if (PQntuples(result) == 0)
		error("couldn't find parent acct(%s) assoc", pacct);
	else
		val = xstrdup(PG_VAL(0));
	PQclear(result);
	//info("got parent field: %s", val);
	return val;
}

/* _get_parent_id - get id of parent association(<c, pa, '', ''>) */
static inline int
_get_parent_id(pgsql_conn_t *pg_conn, char *cluster, char *pacct)
{
	int id = -1;
	char *id_str = _get_parent_field(pg_conn, cluster, pacct, "id_assoc");
	if (id_str) {
		id = atoi(id_str);
		xfree(id_str);
	}
	return id;
}

/* _get_parent_lft - get lft of parent association(<c, pa, '', ''>) */
static inline int
_get_parent_lft(pgsql_conn_t *pg_conn, char *cluster, char *pacct)
{
	int lft = -1;
	char *lft_str = _get_parent_field(pg_conn, cluster, pacct, "lft");
	if (lft_str) {
		lft = atoi(lft_str);
		xfree(lft_str);
	}
	return lft;
}

/*
 * _move_account - move account association to new parent
 *
 * IN pg_conn: database connection
 * IN/OUT lft: lft of account association
 * IN/OUT rgt: rgt of account association
 * IN id: id of account association
 * IN parent: new parent of account
 * IN now: current time
 * RET: error code
 */
static int
_move_account(pgsql_conn_t *pg_conn, char *cluster, uint32_t *lft,
	      uint32_t *rgt, char *id, char *parent, time_t now)
{
	char * query = NULL;
	PGresult *result;
	int rc = SLURM_SUCCESS, plft = -1;

	plft = _get_parent_lft(pg_conn, cluster, parent);
	if (plft < 0)
		return ESLURM_INVALID_PARENT_ACCOUNT;

	if ((plft + 1 - *lft) == 0)
		return ESLURM_SAME_PARENT_ACCOUNT;

	query = xstrdup_printf(
		"SELECT * FROM %s.move_account(%d, %d, %d, %s, '%s', %ld);",
		cluster, plft, *lft, *rgt, id, parent, now);
	result = DEF_QUERY_RET;
	if (result) {
		*lft = atoi(PG_VAL(0));
		*rgt = atoi(PG_VAL(1));
		PQclear(result);
	} else
		rc = SLURM_ERROR;
	return rc;
}

/*
 * _move_parent - change parent of an account association
 *   This should work either way in the tree, i.e., move child to
 *   be parent of current parent, and move parent to be child of child.
 *
 * IN pg_conn: database connection
 * IN cluster: cluster of association
 * IN id: id of account association
 * IN/OUT lft: lft of account association
 * IN/OUT rgt: rgt of account association
 * IN old_parent: old parent of account
 * IN new_parent: new parent of account
 * IN now: current time
 * RET: error code
 */
static int
_move_parent(pgsql_conn_t *pg_conn, char *cluster, char *id, uint32_t *lft,
	     uint32_t  *rgt, char *old_parent, char *new_parent, time_t now)
{
	DEF_VARS;
	int rc = SLURM_SUCCESS;

	/*
	 * if new_parent is child of this account, move new_parent
	 * to be child of old_parent.
	 */
	query = xstrdup_printf("SELECT id_assoc, lft, rgt FROM %s.%s "
			       "WHERE (lft BETWEEN %d AND %d) AND acct='%s' "
			       "AND user_name='' ORDER BY lft;",
			       cluster, assoc_table, *lft, *rgt, new_parent);
	result = DEF_QUERY_RET;
	if (!result)
		return SLURM_ERROR;

	if (PQntuples(result) > 0) {
		uint32_t child_lft = atoi(PG_VAL(1));
		uint32_t child_rgt = atoi(PG_VAL(2));
		debug4("%s(%s) %s,%s is a child of %s", new_parent,
		       PG_VAL(0), PG_VAL(1), PG_VAL(2), id);
		rc = _move_account(pg_conn, cluster, &child_lft, &child_rgt,
				   PG_VAL(0), old_parent, now);
		//_dump_assoc(pg_conn);
	}
	PQclear(result);

	if(rc != SLURM_SUCCESS)
		return rc;

	/*
	 * get the new lft and rgt since they may have changed.
	 */
	query = xstrdup_printf("SELECT lft, rgt FROM %s.%s WHERE id_assoc=%s;",
			       cluster, assoc_table, id);
	result = DEF_QUERY_RET;
	if(! result)
		return SLURM_ERROR;

	if(PQntuples(result) > 0) {
		/* move account to destination */
		*lft = atoi(PG_VAL(0));
		*rgt = atoi(PG_VAL(1));
		rc = _move_account(pg_conn, cluster, lft, rgt, id, new_parent,
				   now);
		//_dump_assoc(pg_conn);
	} else {
		error("can't find parent? we were able to a second ago.");
		rc = SLURM_ERROR;
	}
	PQclear(result);

	return rc;
}

/*
 * _make_assoc_cond -  turn association condition into SQL query condition
 *
 * IN assoc_cond: association condition
 * RET: SQL query condition string
 * XXX: the returned string must be immediately after "FROM assoc_table AS t1"
 */
static char *
_make_assoc_cond(slurmdb_association_cond_t *assoc_cond)
{
	ListIterator itr = NULL;
	char *object = NULL, *cond = NULL;
	char *prefix = "t1";
	int set = 0;

	if(!assoc_cond)
		return NULL;

	if(assoc_cond->qos_list && list_count(assoc_cond->qos_list)) {
		/*
		 * QOSLevel applies to all sub-associations in hierarchy.
		 * So find all sub-associations like WithSubAccounts
		 */
		assoc_cond->with_sub_accts = 1;
		prefix = "t2";	/* conditions apply to parent table */
		xstrfmtcat(cond, ", %%s.%s AS t2 WHERE "
			   "(t1.lft BETWEEN t2.lft AND t2.rgt) AND (",
			   assoc_table);
		set = 0;
		itr = list_iterator_create(assoc_cond->qos_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(cond, " OR ");
			xstrfmtcat(cond,
				   "(%s.qos ~ ',%s(,.+)?$' "
				   "OR %s.delta_qos ~ ',\\\\+%s(,.+)?$')",
				   prefix, object, prefix, object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(cond, ") AND");
	} else if(assoc_cond->with_sub_accts) {
		prefix = "t2";
		xstrfmtcat(cond, ", %%s.%s AS t2 WHERE "
			   "(t1.lft BETWEEN t2.lft AND t2.rgt) AND",
			   assoc_table);
	} else 			/* No QOS condition, no WithSubAccounts */
		xstrcat(cond, " WHERE");

	if(assoc_cond->with_deleted)
		xstrfmtcat(cond, " (%s.deleted=0 OR %s.deleted=1)",
			   prefix, prefix);
	else
		xstrfmtcat(cond, " %s.deleted=0", prefix);

	concat_cond_list(assoc_cond->acct_list, prefix, "acct", &cond);
	concat_cond_list(assoc_cond->def_qos_id_list,
			 prefix, "def_qos_id", &cond);
	concat_cond_list(assoc_cond->fairshare_list,
			 prefix, "shares", &cond);
	concat_cond_list(assoc_cond->grp_cpu_mins_list,
			 prefix, "grp_cpu_mins", &cond);
	concat_cond_list(assoc_cond->grp_cpu_run_mins_list,
			 prefix, "grp_cpu_run_mins", &cond);
	concat_cond_list(assoc_cond->grp_cpus_list,
			 prefix, "grp_cpus", &cond);
	concat_cond_list(assoc_cond->grp_jobs_list,
			 prefix, "grp_jobs", &cond);
	concat_cond_list(assoc_cond->grp_nodes_list,
			 prefix, "grp_nodes", &cond);
	concat_cond_list(assoc_cond->grp_submit_jobs_list,
			 prefix, "grp_submit_jobs", &cond);
	concat_cond_list(assoc_cond->grp_wall_list,
			 prefix, "grp_wall", &cond);
	concat_cond_list(assoc_cond->max_cpu_mins_pj_list,
			 prefix, "max_cpu_mins_pj", &cond);
	concat_cond_list(assoc_cond->max_cpu_run_mins_list,
			 prefix, "max_cpu_run_mins", &cond);
	concat_cond_list(assoc_cond->max_cpus_pj_list,
			 prefix, "max_cpus_pj", &cond);
	concat_cond_list(assoc_cond->max_jobs_list,
			 prefix, "max_jobs", &cond);
	concat_cond_list(assoc_cond->max_nodes_pj_list,
			 prefix, "max_nodes_pj", &cond);
	concat_cond_list(assoc_cond->max_submit_jobs_list,
			 prefix, "max_submit_jobs", &cond);
	concat_cond_list(assoc_cond->max_wall_pj_list,
			 prefix, "max_wall_pj", &cond);

	concat_cond_list(assoc_cond->partition_list,
			 prefix, "partition", &cond);
	concat_cond_list(assoc_cond->id_list,
			 prefix, "id", &cond);
	concat_cond_list(assoc_cond->parent_acct_list,
			 prefix, "parent_acct", &cond);

	if (assoc_cond->user_list && list_count(assoc_cond->user_list)) {
		concat_cond_list(assoc_cond->user_list,
				 prefix, "user_name", &cond);
		/* user_name specified */
	} else if(assoc_cond->user_list) {
		/* we want all the users, but no non-user(account)
		   associations */
		debug4("no user specified looking at users");
		xstrfmtcat(cond, " AND (%s.user_name!='')", prefix);
	}

	return cond;
}

/*
 * _make_assoc_limit_vals - make limit value string for assoc update
 *
 * IN assoc: association
 * OUT vals: value string
 * RET: error code
 */
static int
_make_assoc_limit_vals(slurmdb_association_rec_t *assoc, char **vals)
{
	char *tmp = NULL;

	if(!assoc)
		return SLURM_ERROR;

	if((int)assoc->shares_raw >= 0) {
		xstrfmtcat(*vals, ", shares=%u", assoc->shares_raw);
	} else if (((int)assoc->shares_raw == INFINITE)) {
		xstrcat(*vals, ", shares=1");
		assoc->shares_raw = 1;
	}

	concat_limit_64("grp_cpu_mins", assoc->grp_cpu_mins, &tmp, vals);
	concat_limit_64("grp_cpu_run_mins", assoc->grp_cpu_run_mins,
			&tmp, vals);
	concat_limit_32("grp_cpus", assoc->grp_cpus, &tmp, vals);
	concat_limit_32("grp_jobs", assoc->grp_jobs, &tmp, vals);
	concat_limit_32("grp_nodes", assoc->grp_nodes, &tmp, vals);
	concat_limit_32("grp_submit_jobs", assoc->grp_submit_jobs, &tmp, vals);
	concat_limit_32("grp_wall", assoc->grp_wall, &tmp, vals);
	concat_limit_64("max_cpu_mins_pj",
			assoc->max_cpu_mins_pj, &tmp, vals);
	concat_limit_64("max_cpu_run_mins",
			assoc->max_cpu_run_mins, &tmp, vals);
	concat_limit_32("max_cpus_pj", assoc->max_cpus_pj, &tmp, vals);
	concat_limit_32("max_jobs", assoc->max_jobs, &tmp, vals);
	concat_limit_32("max_nodes_pj", assoc->max_nodes_pj, &tmp, vals);
	concat_limit_32("max_submit_jobs", assoc->max_submit_jobs, &tmp, vals);
	concat_limit_32("max_wall_pj",
			assoc->max_wall_pj, &tmp, vals);
	xfree(tmp);
	return SLURM_SUCCESS;
}

/*
 * _copy_assoc_limits - copy resource limits of assoc
 * OUT dest: destination assoc
 * IN src: source assoc
 */
inline static void
_copy_assoc_limits(slurmdb_association_rec_t *dest, slurmdb_association_rec_t *src)
{
	dest->shares_raw = src->shares_raw;

	dest->grp_cpus = src->grp_cpus;
	dest->grp_cpu_mins = src->grp_cpu_mins;
	dest->grp_cpu_run_mins = src->grp_cpu_run_mins;
	dest->grp_jobs = src->grp_jobs;
	dest->grp_nodes = src->grp_nodes;
	dest->grp_submit_jobs = src->grp_submit_jobs;
	dest->grp_wall = src->grp_wall;

	dest->max_cpus_pj = src->max_cpus_pj;
	dest->max_cpu_mins_pj = src->max_cpu_mins_pj;
	dest->max_cpu_run_mins = src->max_cpu_run_mins;
	dest->max_jobs = src->max_jobs;
	dest->max_nodes_pj = src->max_nodes_pj;
	dest->max_submit_jobs = src->max_submit_jobs;
	dest->max_wall_pj = src->max_wall_pj;

	dest->def_qos_id = src->def_qos_id;
}

/* Used to get all the users inside a lft and rgt set.  This is just
 * to send the user all the associations that are being modified from
 * a previous change to it's parent.
 */
static int
_modify_unset_users(pgsql_conn_t *pg_conn, char *cluster,
		    slurmdb_association_rec_t *assoc,
		    char *acct, uint32_t lft, uint32_t rgt,
		    List ret_list, int moved_parent)
{
	DEF_VARS;
	char *object = NULL;
	char *ma_fields = "id_assoc,user_name,acct,partition,max_jobs,"
		"max_submit_jobs,max_nodes_pj,max_cpus_pj,max_wall_pj,"
		"max_cpu_mins_pj,max_cpu_run_mins,def_qos_id,qos,delta_qos,"
		"lft,rgt";
	enum {
		F_ID,
		F_USER,
		F_ACCT,
		F_PART,
		F_MJ,
		F_MSJ,
		F_MNPJ,
		F_MCPJ,
		F_MWPJ,
		F_MCMPJ,
		F_MCRM,
		F_DEF_QOS,
		F_QOS,
		F_DELTA_QOS,
		F_LFT,
		F_RGT,
		F_COUNT
	};

	if(!ret_list || !acct)
		return SLURM_ERROR;

	/* We want all the sub accounts and user accounts */
	query = xstrdup_printf("SELECT DISTINCT %s FROM %s.%s WHERE deleted=0 "
			       "  AND (lft BETWEEN %d AND %d) "
			       "  AND ((user_name='' AND parent_acct='%s') OR"
			       "       (user_name!='' AND acct='%s')) "
			       "  ORDER BY lft;", ma_fields, cluster,
			       assoc_table, lft, rgt, acct, acct);
	result = DEF_QUERY_RET;
	if(!result)
		return SLURM_ERROR;

	FOR_EACH_ROW {
		slurmdb_association_rec_t *mod_assoc = NULL;
		int modified = 0;

		mod_assoc = xmalloc(sizeof(slurmdb_association_rec_t));
		slurmdb_init_association_rec(mod_assoc, 0);

		mod_assoc->id = atoi(ROW(F_ID));
		mod_assoc->cluster = xstrdup(cluster);

		if(ISNULL(F_MJ) && assoc->max_jobs != NO_VAL) {
			mod_assoc->max_jobs = assoc->max_jobs;
			modified = 1;
		}

		if(ISNULL(F_MSJ) && assoc->max_submit_jobs != NO_VAL) {
			mod_assoc->max_submit_jobs = assoc->max_submit_jobs;
			modified = 1;
		}

		if(ISNULL(F_MNPJ) && assoc->max_nodes_pj != NO_VAL) {
			mod_assoc->max_nodes_pj = assoc->max_nodes_pj;
			modified = 1;
		}

		if(ISNULL(F_MCPJ) && assoc->max_cpus_pj != NO_VAL) {
			mod_assoc->max_cpus_pj = assoc->max_cpus_pj;
			modified = 1;
		}

		if(ISNULL(F_MWPJ) && assoc->max_wall_pj != NO_VAL) {
			mod_assoc->max_wall_pj = assoc->max_wall_pj;
			modified = 1;
		}

		if(ISNULL(F_MCMPJ) &&
		   assoc->max_cpu_mins_pj != (uint64_t)NO_VAL) {
			mod_assoc->max_cpu_mins_pj = assoc->max_cpu_mins_pj;
			modified = 1;
		}

		if(ISNULL(F_MCRM) &&
		   assoc->max_cpu_run_mins != (uint64_t)NO_VAL) {
			mod_assoc->max_cpu_run_mins = assoc->max_cpu_run_mins;
			modified = 1;
		}

		if(ISNULL(F_DEF_QOS) && assoc->def_qos_id != NO_VAL) {
			mod_assoc->def_qos_id = assoc->def_qos_id;
			modified = 1;
		}

		if(ISEMPTY(F_QOS) && assoc->qos_list) {
			List delta_qos_list = NULL;
			char *qos_char = NULL, *delta_char = NULL;
			ListIterator delta_itr = NULL;
			ListIterator qos_itr =
				list_iterator_create(assoc->qos_list);
			if(! ISEMPTY(F_DELTA_QOS)) {
				delta_qos_list =
					list_create(slurm_destroy_char);
				slurm_addto_char_list(delta_qos_list,
						      ROW(F_DELTA_QOS)+1);
				delta_itr =
					list_iterator_create(delta_qos_list);
			}

			mod_assoc->qos_list = list_create(slurm_destroy_char);
			/* here we are making sure a child does not
			   have the qos added or removed before we add
			   it to the parent.
			*/
			while((qos_char = list_next(qos_itr))) {
				if(delta_itr && qos_char[0] != '=') {
					while((delta_char =
					       list_next(delta_itr))) {

						if((qos_char[0]
						    != delta_char[0])
						   && (!strcmp(qos_char+1,
							       delta_char+1)))
							break;
					}
					list_iterator_reset(delta_itr);
					if(delta_char)
						continue;
				}
				list_append(mod_assoc->qos_list,
					    xstrdup(qos_char));
			}
			list_iterator_destroy(qos_itr);
			if(delta_itr)
				list_iterator_destroy(delta_itr);
			if(list_count(mod_assoc->qos_list)
			   || !list_count(assoc->qos_list))
				modified = 1;
			else {
				list_destroy(mod_assoc->qos_list);
				mod_assoc->qos_list = NULL;
			}
		}

		/* We only want to add those that are modified here */
		if(modified) {
			/* Since we aren't really changing this non
			 * user association we don't want to send it.
			 */
			if(ISEMPTY(F_USER)) {
				/* This is a sub account so run it
				 * through as if it is a parent.
				 */
				_modify_unset_users(pg_conn,
						    cluster,
						    mod_assoc,
						    ROW(F_ACCT),
						    atoi(ROW(F_LFT)),
						    atoi(ROW(F_RGT)),
						    ret_list, moved_parent);
				slurmdb_destroy_association_rec(mod_assoc);
				continue;
			}
			/* We do want to send all user accounts though */
			mod_assoc->shares_raw = NO_VAL;
			if(! ISEMPTY(F_PART)) {
				// see if there is a partition name
				object = xstrdup_printf(
					"C = %-10s A = %-20s U = %-9s P = %s",
					cluster, ROW(F_ACCT),
					ROW(F_USER), ROW(F_PART));
			} else {
				object = xstrdup_printf(
					"C = %-10s A = %-20s U = %-9s",
					cluster, ROW(F_ACCT),
					ROW(F_USER));
			}

			list_append(ret_list, object);

			if(moved_parent)
				slurmdb_destroy_association_rec(mod_assoc);
			else
				if(addto_update_list(pg_conn->update_list,
						     SLURMDB_MODIFY_ASSOC,
						     mod_assoc)
				   != SLURM_SUCCESS)
					error("couldn't add to "
					      "the update list");
		} else
			slurmdb_destroy_association_rec(mod_assoc);

	} END_EACH_ROW;
	PQclear(result);

	return SLURM_SUCCESS;
}

/*
 * _init_parent_limits - set init value for parent limits
 * IN/OUT passoc: parent association record
 */
static void
_init_parent_limits(slurmdb_association_rec_t *passoc)
{
	passoc->max_jobs = INFINITE;
	passoc->max_submit_jobs = INFINITE;
	passoc->max_cpus_pj = INFINITE;
	passoc->max_nodes_pj = INFINITE;
	passoc->max_wall_pj = INFINITE;
	passoc->max_cpu_mins_pj = (uint64_t)INFINITE;
	passoc->max_cpu_run_mins = (uint64_t)INFINITE;
	passoc->def_qos_id = 0;
}

/*
 * _get_parent_limits - get parent account resource limits
 * IN pg_conn: database connection
 * IN cluster: cluster of parent account
 * IN pacct: parent account
 * OUT passoc: parent resource limits
 * OUT qos: parent qos
 * OUT delta_qos: parent delta_qos
 * RET: error code
 */
static int
_get_parent_limits(pgsql_conn_t *pg_conn, char *cluster,
		   char *pacct, slurmdb_association_rec_t *passoc,
		   char **qos, char **delta_qos)
{
	PGresult *result = NULL;
	char *query = NULL;
	enum {
		F_MJ,
		F_MSJ,
		F_MCPJ,
		F_MNPJ,
		F_MWPJ,
		F_MCMPJ,
		F_MCRM,
		F_DEF_QOS,
		F_QOS,
		F_DELTA,
		F_COUNT,
	};

	query = xstrdup_printf(
		"SELECT * FROM %s.get_parent_limits('%s');",
		cluster, pacct);
	result = DEF_QUERY_RET;
	if(! result)
		return SLURM_ERROR;

	if (PQntuples(result) == 0) {
		PQclear(result);
		return SLURM_ERROR;
	}

	passoc->max_jobs = PG_NULL(F_MJ) ? INFINITE :
		atoi(PG_VAL(F_MJ));
	passoc->max_submit_jobs = PG_NULL(F_MSJ) ? INFINITE :
		atoi(PG_VAL(F_MSJ));
	passoc->max_cpus_pj = PG_NULL(F_MCPJ) ? INFINITE :
		atoi(PG_VAL(F_MCPJ));
	passoc->max_nodes_pj = PG_NULL(F_MNPJ) ? INFINITE :
		atoi(PG_VAL(F_MNPJ));
	passoc->max_wall_pj = PG_NULL(F_MWPJ) ? INFINITE :
		atoi(PG_VAL(F_MWPJ));
	passoc->max_cpu_mins_pj = PG_NULL(F_MCMPJ) ? (uint64_t)INFINITE :
		atoll(PG_VAL(F_MCMPJ));
	passoc->max_cpu_run_mins = PG_NULL(F_MCRM) ? (uint64_t)INFINITE :
		atoll(PG_VAL(F_MCRM));
	passoc->def_qos_id = PG_NULL(F_DEF_QOS) ? 0 :
		atoi(PG_VAL(F_DEF_QOS));

	*qos = PG_NULL(F_QOS) ? NULL :
		xstrdup(PG_VAL(F_QOS));
	*delta_qos = PG_NULL(F_DELTA) ? NULL:
		xstrdup(PG_VAL(F_DELTA));

	debug3("got parent account limits of <%s, %s>:\n"
	       "\tmax_jobs:%d, max_submit_jobs:%d, max_cpus_pj:%d,\n"
	       "\tmax_nodes_pj:%d, max_wall_pj:%d, max_cpu_mins_pj:%"PRIu64"\n"
	       "\tmax_cpu_run_mins:%"PRIu64", def_qos_id:%d, "
	       "qos:%s, delta_qos:%s",
	       cluster, pacct, passoc->max_jobs, passoc->max_submit_jobs,
	       passoc->max_cpus_pj, passoc->max_nodes_pj, passoc->max_wall_pj,
	       passoc->max_cpu_mins_pj, passoc->max_cpu_run_mins,
	       passoc->def_qos_id, *qos, *delta_qos);

	PQclear(result);
	return SLURM_SUCCESS;
}

/*
 * check_assoc_tables - check association related tables and functions
 *
 * IN db_conn: database connection
 * IN cluster: cluster schema
 * RET: error code
 */
extern int
check_assoc_tables(PGconn *db_conn, char *cluster)
{
	int rc;

	rc = check_table(db_conn, cluster, assoc_table, assoc_table_fields,
			 assoc_table_constraints);
	rc |= check_table(db_conn, cluster, max_rgt_table, max_rgt_table_fields,
			  max_rgt_table_constraints);

	rc |= _create_function_show_assoc_hierarchy(db_conn, cluster);

	rc |= _create_function_remove_assoc(db_conn, cluster);
	rc |= _create_function_init_max_rgt_table(db_conn, cluster);
	rc |= _create_function_move_account(db_conn, cluster);
	rc |= _create_function_make_space(db_conn, cluster);
	rc |= _create_function_add_assoc(db_conn, cluster);
	rc |= _create_function_add_assoc_update(db_conn, cluster);
	rc |= _create_function_delete_assoc(db_conn, cluster);
	rc |= _create_function_add_root_assoc(db_conn, cluster);
	rc |= _create_function_get_parent_limits(db_conn, cluster);

	rc |= _init_max_rgt_table(db_conn, cluster);

	return rc;
}

static int
_assoc_sort_cluster(slurmdb_association_rec_t *rec_a,
		    slurmdb_association_rec_t *rec_b)
{
	int diff = strcmp(rec_a->cluster, rec_b->cluster);

	if (diff < 0)
		return -1;
	else if (diff > 0)
		return 1;
	return 0;
}

static int
_concat_user_get_assoc_cond(pgsql_conn_t *pg_conn, char *cluster,
			    slurmdb_user_rec_t *user, char **cond)
{
	DEF_VARS;
	ListIterator itr = NULL;
	int set = 0;

	query = xstrdup_printf(
		"SELECT lft, rgt FROM %s.%s WHERE user_name='%s'",
		cluster, assoc_table, user->name);
	if(user->coord_accts) {
		slurmdb_coord_rec_t *coord = NULL;
		itr = list_iterator_create(user->coord_accts);
		while((coord = list_next(itr))) {
			xstrfmtcat(query, " OR acct='%s'", coord->name);
		}
		list_iterator_destroy(itr);
	}
	result = DEF_QUERY_RET;
	if(!result)
		return SLURM_ERROR;

	FOR_EACH_ROW {
		if(set) {
			xstrfmtcat(*cond, " OR (t1.lft BETWEEN %s AND %s)",
				   ROW(0), ROW(1));
		} else {
			set = 1;
			xstrfmtcat(*cond, " AND ((t1.lft BETWEEN %s AND %s)",
				   ROW(0), ROW(1));
		}
	} END_EACH_ROW;
	if(set)
		xstrcat(*cond,")");
	PQclear(result);
	return SLURM_SUCCESS;
}

static int
_cluster_get_assocs(pgsql_conn_t *pg_conn, char *cluster,
		    slurmdb_association_cond_t *assoc_cond, char *sent_cond,
		    int is_admin, slurmdb_user_rec_t *user, List sent_list)
{
	DEF_VARS;
	char *cond = NULL;
	List assoc_list = NULL;
	List delta_qos_list = NULL;
	slurmdb_association_rec_t p_assoc;
	char *p_qos = NULL;
	char *p_delta = NULL;
	char *parent_acct = NULL;
	char *last_acct = NULL;
	uint32_t parent_id = 0;
	/* needed if we don't have an assoc_cond */
	uint16_t without_parent_info = 0;
	uint16_t without_parent_limits = 0;
	uint16_t with_usage = 0;
	uint16_t with_raw_qos = 0;
	/* if this changes you will need to edit the corresponding enum */
	char *ga_fields = "t1.id_assoc, t1.lft, t1.rgt, t1.user_name, t1.acct,"
		"t1.partition, t1.shares, t1.grp_cpu_mins, t1.grp_cpu_run_mins,"
		"t1.grp_cpus, t1.grp_jobs, t1.grp_nodes, t1.grp_submit_jobs,"
		"t1.grp_wall, t1.max_cpu_mins_pj, t1.max_cpu_run_mins, "
		"t1.max_cpus_pj, t1.max_jobs, t1.max_nodes_pj, "
		"t1.max_submit_jobs, t1.max_wall_pj, t1.parent_acct, "
		"t1.def_qos_id, t1.qos, t1.delta_qos";
	enum {
		F_ID,
		F_LFT,
		F_RGT,
		F_USER,
		F_ACCT,
		F_PART,
		F_FS,
		F_GCM,
		F_GCRM,
		F_GC,
		F_GJ,
		F_GN,
		F_GSJ,
		F_GW,
		F_MCMPJ,
		F_MCRM,
		F_MCPJ,
		F_MJ,
		F_MNPJ,
		F_MSJ,
		F_MWPJ,
		F_PARENT,
		F_DEF_QOS,
		F_QOS,
		F_DELTA_QOS,
		F_COUNT
	};


	if (assoc_cond) {
		with_raw_qos = assoc_cond->with_raw_qos;
		with_usage = assoc_cond->with_usage;
		without_parent_limits = assoc_cond->without_parent_limits;
		without_parent_info = assoc_cond->without_parent_info;
	}

	cond = xstrdup_printf(sent_cond, cluster);
	if (!is_admin) {
		if (_concat_user_get_assoc_cond(pg_conn, cluster, user, &cond)
		    != SLURM_SUCCESS) {
			xfree(cond);
			return SLURM_ERROR;
		}
	}

	query = xstrdup_printf("SELECT DISTINCT %s FROM %s.%s AS t1 %s "
			       "ORDER BY lft;",
			       ga_fields, cluster, assoc_table, cond);
	xfree(cond);
	result = DEF_QUERY_RET;
	if(!result)
		return SLURM_ERROR;

	assoc_list = list_create(slurmdb_destroy_association_rec);
	delta_qos_list = list_create(slurm_destroy_char);
	FOR_EACH_ROW {
		slurmdb_association_rec_t *assoc =
			xmalloc(sizeof(slurmdb_association_rec_t));
		list_append(assoc_list, assoc);

		assoc->id = atoi(ROW(F_ID));
		assoc->lft = atoi(ROW(F_LFT));
		assoc->rgt = atoi(ROW(F_RGT));
		assoc->cluster = xstrdup(cluster);
		assoc->acct = xstrdup(ROW(F_ACCT));
		if(! ISEMPTY(F_USER))
			assoc->user = xstrdup(ROW(F_USER));
		if(! ISEMPTY(F_PART))
			assoc->partition = xstrdup(ROW(F_PART));

		assoc->grp_jobs = ISNULL(F_GJ) ? INFINITE : atoi(ROW(F_GJ));
		assoc->grp_cpus = ISNULL(F_GC) ? INFINITE : atoi(ROW(F_GC));
		assoc->grp_nodes = ISNULL(F_GN) ? INFINITE : atoi(ROW(F_GN));
		assoc->grp_wall = ISNULL(F_GW) ? INFINITE : atoll(ROW(F_GW));
		assoc->grp_submit_jobs = ISNULL(F_GSJ) ? INFINITE : atoi(ROW(F_GSJ));
		assoc->grp_cpu_mins = ISNULL(F_GCM) ? (uint64_t)INFINITE :
			atoll(ROW(F_GCM));
		assoc->grp_cpu_run_mins = ISNULL(F_GCRM) ? (uint64_t)INFINITE :
			atoll(ROW(F_GCRM));
		assoc->shares_raw = ISNULL(F_FS) ? INFINITE : atoi(ROW(F_FS));

		parent_acct = ROW(F_ACCT);
		if(!without_parent_info
		   && !ISEMPTY(F_PARENT)) {
			assoc->parent_acct = xstrdup(ROW(F_PARENT));
			parent_acct = ROW(F_PARENT);
		} else if(!assoc->user) {
			/* (parent_acct='' AND user_name='') => acct='root' */
			parent_acct = NULL;
			parent_id = 0;
			_init_parent_limits(&p_assoc);
			last_acct = NULL;
		}

		if(!without_parent_info && parent_acct &&
		   (!last_acct || strcmp(parent_acct, last_acct))) {

			_init_parent_limits(&p_assoc);
			xfree(p_qos);
			xfree(p_delta);
			parent_id = _get_parent_id(pg_conn, cluster, parent_acct);
			if(!without_parent_limits) {
				if(_get_parent_limits(pg_conn, cluster,
						      parent_acct, &p_assoc,
						      &p_qos, &p_delta)
				   != SLURM_SUCCESS) {
					parent_id = 0;
					goto no_parent_limits;
				}
			} else {
				memset(&p_assoc, 0, sizeof(p_assoc));
			}
			last_acct = parent_acct;
		}
	no_parent_limits:
		assoc->def_qos_id = ISNULL(F_DEF_QOS) ?
			p_assoc.def_qos_id : atoi(ROW(F_DEF_QOS));
		assoc->max_jobs = ISNULL(F_MJ) ?
			p_assoc.max_jobs : atoi(ROW(F_MJ));
		assoc->max_submit_jobs = ISNULL(F_MSJ) ?
			p_assoc.max_submit_jobs: atoi(ROW(F_MSJ));
		assoc->max_cpus_pj = ISNULL(F_MCPJ) ?
			p_assoc.max_cpus_pj : atoi(ROW(F_MCPJ));
		assoc->max_nodes_pj = ISNULL(F_MNPJ) ?
			p_assoc.max_nodes_pj : atoi(ROW(F_MNPJ));
		assoc->max_wall_pj = ISNULL(F_MWPJ) ?
			p_assoc.max_wall_pj : atoi(ROW(F_MWPJ));
		assoc->max_cpu_mins_pj = ISNULL(F_MCMPJ) ?
			p_assoc.max_cpu_mins_pj : atoll(ROW(F_MCMPJ));
		assoc->max_cpu_run_mins = ISNULL(F_MCRM) ?
			p_assoc.max_cpu_run_mins : atoll(ROW(F_MCRM));

		assoc->qos_list = list_create(slurm_destroy_char);
		/* alway with a ',' in qos and delta_qos */
		if(! ISEMPTY(F_QOS))
			slurm_addto_char_list(assoc->qos_list,
					      ROW(F_QOS)+1);
		else {
			/* add the parents first */
			if(p_qos)
				slurm_addto_char_list(assoc->qos_list,
						      p_qos+1);
			/* then add the parents delta */
			if(p_delta)
				slurm_addto_char_list(delta_qos_list,
						      p_delta+1);
			/* now add the associations */
			if(! ISEMPTY(F_DELTA_QOS))
				slurm_addto_char_list(delta_qos_list,
						      ROW(F_DELTA_QOS)+1);
		}

		if(with_raw_qos && list_count(delta_qos_list)) {
			list_transfer(assoc->qos_list, delta_qos_list);
		} else if(list_count(delta_qos_list)) {
			merge_delta_qos_list(assoc->qos_list, delta_qos_list);
		}
		list_flush(delta_qos_list);

		assoc->parent_id = parent_id;

		//info("parent id is %d", assoc->parent_id);
		//log_assoc_rec(assoc);
	} END_EACH_ROW;
	PQclear(result);
	list_destroy(delta_qos_list);
	xfree(p_delta);
	xfree(p_qos);

	if(with_usage && assoc_list)
		get_usage_for_assoc_list(pg_conn, cluster, assoc_list,
					 assoc_cond->usage_start,
					 assoc_cond->usage_end);

	list_transfer(sent_list, assoc_list);
	list_destroy(assoc_list);
	return SLURM_SUCCESS;
}


static int
_clusters_assoc_update(pgsql_conn_t *pg_conn, List cluster_list, uid_t uid)
{
	List assoc_list = NULL;
	ListIterator itr = NULL;
	slurmdb_association_rec_t *assoc = NULL;
	slurmdb_association_cond_t assoc_q;
	slurmdb_user_rec_t user;
	char *cond = NULL;
	int rc = 0, is_admin=1;

	if (cluster_list == NULL || list_count(cluster_list) == 0)
		return SLURM_SUCCESS;

	/*
	 * XXX: do not call as_pg_get_associations(), because it calls
	 * cluster_in_db() which leads to dead lock. so inline it here.
	 */
	if (check_user_op(pg_conn, uid, PRIVATE_DATA_USERS, &is_admin, &user)
	    != SLURM_SUCCESS) {
		error("as/pg: user(%u) not found in db", uid);
		errno = ESLURM_USER_ID_MISSING;
		return SLURM_ERROR;
	}

	memset(&assoc_q, 0, sizeof(slurmdb_association_cond_t));
	assoc_q.cluster_list = cluster_list;
	cond = _make_assoc_cond(&assoc_q);

	assoc_list = list_create(slurmdb_destroy_association_rec);

	FOR_EACH_CLUSTER(assoc_q.cluster_list) {
		/* we know cluster is in db */
		rc = _cluster_get_assocs(pg_conn, cluster_name, &assoc_q,
					 cond, is_admin, &user, assoc_list);
		if (rc != SLURM_SUCCESS) {
			error("_clusters_assoc_update: failed to get assocs "
			      "for cluster %s. ignored", cluster_name);
			continue;
		}
	} END_EACH_CLUSTER;
	xfree(cond);

	itr = list_iterator_create(assoc_list);
	while((assoc = list_next(itr))) {
		if(addto_update_list(pg_conn->update_list,
				     SLURMDB_MODIFY_ASSOC,
				     assoc) == SLURM_SUCCESS)
			list_remove(itr);
	}
	list_iterator_destroy(itr);
	list_destroy(assoc_list);

	return SLURM_SUCCESS;
}

static int
_set_assoc_limits_for_add(pgsql_conn_t *pg_conn,
			  slurmdb_association_rec_t *assoc)
{
	DEF_VARS;
	slurmdb_association_rec_t p_assoc;
	char *p_acct = NULL, *p_qos = NULL, *p_delta = NULL;
	char *qos_delta = NULL;

	xassert(assoc);

	if(assoc->parent_acct)
		p_acct = assoc->parent_acct;
	else if(assoc->user)
		p_acct = assoc->acct;
	else
		return SLURM_SUCCESS;

	if (_get_parent_limits(pg_conn, assoc->cluster, p_acct, &p_assoc,
			       &p_qos, &p_delta) != SLURM_SUCCESS)
		return SLURM_ERROR;

	if(p_assoc.def_qos_id && assoc->def_qos_id == NO_VAL)
		assoc->def_qos_id = p_assoc.def_qos_id;
	else if(assoc->def_qos_id == NO_VAL)
		assoc->def_qos_id = 0;

	if(p_assoc.max_jobs && assoc->max_jobs == NO_VAL)
		assoc->max_jobs = p_assoc.max_jobs;
	if(p_assoc.max_submit_jobs && assoc->max_submit_jobs == NO_VAL)
		assoc->max_submit_jobs = p_assoc.max_submit_jobs;
	if(p_assoc.max_cpus_pj && assoc->max_cpus_pj == NO_VAL)
		assoc->max_cpus_pj = p_assoc.max_cpus_pj;
	if(p_assoc.max_nodes_pj && assoc->max_nodes_pj == NO_VAL)
		assoc->max_nodes_pj = p_assoc.max_nodes_pj;
	if(p_assoc.max_wall_pj && assoc->max_wall_pj == NO_VAL)
		assoc->max_wall_pj = p_assoc.max_wall_pj;
	if(p_assoc.max_cpu_mins_pj && assoc->max_cpu_mins_pj == (uint64_t)NO_VAL)
		assoc->max_cpu_mins_pj = p_assoc.max_cpu_mins_pj;
	if(p_assoc.max_cpu_run_mins && assoc->max_cpu_run_mins == (uint64_t)NO_VAL)
		assoc->max_cpu_run_mins = p_assoc.max_cpu_run_mins;

	if(assoc->qos_list) {
		int set = 0;
		char *tmp_char = NULL;
		ListIterator qos_itr = list_iterator_create(assoc->qos_list);
		while((tmp_char = list_next(qos_itr))) {
			/* we don't want to include blank names */
			if(!tmp_char[0])
				continue;

			if(!set) {
				if(tmp_char[0] != '+' && tmp_char[0] != '-')
					break;
				set = 1;
			}
			xstrfmtcat(qos_delta, ",%s", tmp_char);
		}
		list_iterator_destroy(qos_itr);

		if(tmp_char) {
			goto end_it;
		}
		list_flush(assoc->qos_list);
	} else
		assoc->qos_list = list_create(slurm_destroy_char);

	if(p_qos)
		slurm_addto_char_list(assoc->qos_list, p_qos+1);

	if(p_delta)
		slurm_addto_char_list(assoc->qos_list,
				      p_delta+1);
	if(qos_delta) {
		slurm_addto_char_list(assoc->qos_list, qos_delta+1);
	}

end_it:
	xfree(qos_delta);
	PQclear(result);

	if (!assoc->lft) {
		query = xstrdup_printf(
			"SELECT lft,rgt FROM %s.%s WHERE id_assoc=%u",
			assoc->cluster, assoc_table, assoc->id);
		result = DEF_QUERY_RET;
		if (!result)
			return SLURM_ERROR;
		if (PQntuples(result)) {
			assoc->lft = atoi(PG_VAL(0));
			assoc->rgt = atoi(PG_VAL(1));
		} else
			error ("no association with id %u", assoc->id);
		PQclear(result);
	}
	return SLURM_SUCCESS;
}

/*
 * as_pg_add_assocaitons - add associations
 *
 * IN pg_conn: database connection
 * IN uid: user performing the add operation
 * IN assoc_list: associations to add
 * RET: error code
 */
extern int
as_pg_add_associations(pgsql_conn_t *pg_conn, uint32_t uid,
		       List assoc_list)
{
	DEF_VARS;
	List update_cluster_list = NULL;
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	slurmdb_association_rec_t *object = NULL;
	char *rec = NULL, *txn = NULL, *cond = NULL;
	char *parent = NULL, *user_name = NULL, *txn_query = NULL;
	int incr = 0, p_lft = 0, p_id = 0, moved_parent = 0;
	char *old_parent = NULL, *old_cluster = NULL;
	char *last_parent = NULL, *last_cluster = NULL;
	time_t now = time(NULL);
	char *ga_fields = "id_assoc, parent_acct, lft, rgt, deleted";
	enum {
		F_ID,
		F_PACCT,
		F_LFT,
		F_RGT,
		F_DELETED,
		F_COUNT
	};

	if(!assoc_list) {
		error("as/pg: add_associations: no association list given");
		return SLURM_ERROR;
	}
	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	update_cluster_list = list_create(NULL);
	user_name = uid_to_string((uid_t) uid);

	list_sort(assoc_list, (ListCmpF)_assoc_sort_cluster);

	itr = list_iterator_create(assoc_list);
	while((object = list_next(itr))) {
		if(!object->cluster || !object->cluster[0] ||
		   !object->acct || !object->acct[0]) {
			error("We need an association cluster and "
			      "acct to add one.");
			rc = SLURM_ERROR;
			break;
		}

		list_append(update_cluster_list, object->cluster);

		/* query to check if this assoc is already in DB */
		cond = xstrdup_printf("acct='%s' ", object->acct);
		if (object->user) { /* user association, parent is <c,
				     * a, '', ''> */
			parent = object->acct;
			xstrfmtcat(cond, "AND user_name='%s' AND partition='%s'",
				   object->user, object->partition ?: "");
		} else {	/* account association, parent is <c,
				 * pa, '', ''> */
			parent = object->parent_acct ?: "root";
			xstrfmtcat(cond, "AND user_name='' ");
		}
		xstrfmtcat(query, "SELECT %s FROM %s.%s WHERE %s ORDER BY lft "
			   "FOR UPDATE;", ga_fields, object->cluster,
			   assoc_table, cond);
		xfree(cond);
		result = DEF_QUERY_RET;
		if(!result) {
			error("couldn't query the database");
			rc = SLURM_ERROR;
			break;
		}

		if(PQntuples(result) == 0) { /* assoc not in table */
			if(!old_parent || !old_cluster
			   || strcasecmp(parent, old_parent)
			   || strcasecmp(object->cluster, old_cluster)) {
				if(incr) { /* make space for newly
					    * added assocs */
					rc = _make_space(pg_conn, old_cluster,
							 p_lft, incr);
					if(rc != SLURM_SUCCESS) {
						error("Couldn't make space");
						break;
					}
				}
				/* get new parent info */
				p_lft = _get_parent_lft(pg_conn, object->cluster, parent);
				if (p_lft < 0) {
					rc = SLURM_ERROR;
					break;
				}
				old_parent = parent;
				old_cluster = object->cluster;
				incr = 0;
			}
			incr += 2;

			/* add as the left-most child of parent, in
			 * accord with _make_space() */
			object->lft = p_lft + incr - 1;
			object->rgt = p_lft + incr;

			/* TODO: deleted = 2 ? */
			_make_assoc_rec(object, now, 2, &rec, &txn);
			query = xstrdup_printf("SELECT %s.add_assoc(%s);",
					       object->cluster, rec);
			xfree(rec);
		} else if(atoi(PG_VAL(F_DELETED)) == 0) {
			/* assoc exists and not deleted */
			/* We don't need to do anything here */
			debug("This association was added already");
			PQclear(result);
			continue;
		} else {	/* assoc exists but deleted */
			uint32_t lft = atoi(PG_VAL(F_LFT));
			uint32_t rgt = atoi(PG_VAL(F_RGT));

			if(object->parent_acct
			   && strcasecmp(object->parent_acct,
					 PG_VAL(F_PACCT))) {
				/* We need to move the parent! */
				if(_move_parent(pg_conn,
						object->cluster,
						PG_VAL(F_ID),
						&lft, &rgt,
						PG_VAL(F_PACCT),
						object->parent_acct, now)
				   == SLURM_ERROR) {
					PQclear(result);
					continue;
				}
				moved_parent = 1;
			} else {
				object->lft = lft;
				object->rgt = rgt;
			}

			_make_assoc_rec(object, now, 0, &rec, &txn);
			query = xstrdup_printf("SELECT %s.add_assoc_update(%s);",
					       object->cluster, rec);
			xfree(rec);
		}
		PQclear(result);

		object->id = DEF_QUERY_RET_ID;
		if (!object->id) {
			rc = SLURM_ERROR;
			error("Couldn't add assoc");
			break;
		}

		/* if not moved parent, we add this assoc to update list */
		if (!moved_parent) {
			if (!last_parent || !last_cluster ||
			    strcmp(parent, last_parent) ||
			    strcmp(object->cluster, last_cluster)) {
				p_id = _get_parent_id(pg_conn, object->cluster,
						      parent);
				last_parent = parent;
				last_cluster = object->cluster;
			}
			object->parent_id = p_id;

			_set_assoc_limits_for_add(pg_conn, object);
			if(addto_update_list(pg_conn->update_list,
					     SLURMDB_ADD_ASSOC,
					     object) == SLURM_SUCCESS) {
				list_remove(itr);
			}
		}

		/* add to txn query string */
		if(txn_query)
			xstrfmtcat(txn_query,
				   ", (%ld, %d, '%d', '%s', $$%s$$)",
				   now, DBD_ADD_ASSOCS, object->id, user_name,
				   txn);
		else
			xstrfmtcat(txn_query,
				   "INSERT INTO %s "
				   "(timestamp, action, name, actor, info) "
				   "VALUES (%ld, %d, '%d', '%s', $$%s$$)",
				   txn_table, now, DBD_ADD_ASSOCS, object->id,
				   user_name, txn);
		xfree(txn);
	}
	list_iterator_destroy(itr);
	xfree(user_name);

	if(rc == SLURM_SUCCESS && incr) {
		/* _make_space() change delete=2 => deleted=0 */
		rc = _make_space(pg_conn, old_cluster, p_lft, incr);
		if(rc != SLURM_SUCCESS) {
			error("Couldn't make space 2");
		}
	}

	if(!moved_parent) {
		slurmdb_update_object_t *update_object = NULL;

		itr = list_iterator_create(pg_conn->update_list);;
		while((update_object = list_next(itr))) {
			if(!update_object->objects
			   || !list_count(update_object->objects))
				continue;
			if(update_object->type == SLURMDB_ADD_ASSOC)
				break;
		}
		list_iterator_destroy(itr);

		if(update_object && update_object->objects
		   && list_count(update_object->objects)) {
			ListIterator itr2 =
				list_iterator_create(update_object->objects);

			FOR_EACH_CLUSTER(NULL) {
				uint32_t smallest_lft = 0xFFFFFFFF;
				while((object = list_next(itr2))) {
					if(object->lft < smallest_lft
					   && !strcmp(object->cluster,
						      cluster_name))
						smallest_lft = object->lft;
				}
				list_iterator_reset(itr2);
				/* now get the lowest lft from the
				   added files by cluster */
				if(smallest_lft != 0xFFFFFFFF)
					rc = pgsql_get_modified_lfts(
						pg_conn, cluster_name,
						smallest_lft);
			} END_EACH_CLUSTER;
			list_iterator_destroy(itr2);
		}
	}

	if(rc == SLURM_SUCCESS) {
		if(txn_query) {
			xstrcat(txn_query, ";");
			debug3("as/pg(%s:%d) query\n%s", THIS_FILE,
			       __LINE__, txn_query);
			rc = pgsql_db_query(pg_conn->db_conn, txn_query);
			xfree(txn_query);
			if(rc != SLURM_SUCCESS) {
				error("Couldn't add txn");
				rc = SLURM_SUCCESS;
			}
		}
		if(moved_parent) {
			list_flush(pg_conn->update_list);
			if (_clusters_assoc_update(pg_conn,
						   update_cluster_list, uid)
			    != SLURM_SUCCESS) {
				rc = SLURM_ERROR;
			}
		}
	} else {
		xfree(txn_query);
		reset_pgsql_conn(pg_conn);
	}

	list_destroy(update_cluster_list);
	return rc;
}


static int
_cluster_modify_associations(pgsql_conn_t *pg_conn, char *cluster,
			     slurmdb_association_rec_t *assoc, char *sent_cond,
			     char *sent_vals, int is_admin,
			     slurmdb_user_rec_t *user, List sent_list)
{
	DEF_VARS;
	List ret_list = NULL;
	char *object = NULL, *user_name = NULL, *cond;
	char *vals = NULL, *name_char = NULL;
	time_t now = time(NULL);
	int set = 0, rc = SLURM_SUCCESS;
	int set_qos_vals = 0;
	int moved_parent = 0;
	char *ma_fields = "t1.id_assoc,t1.acct,t1.parent_acct,t1.user_name,"
		"t1.partition,t1.lft,t1.rgt,t1.qos";
	enum {
		F_ID,
		F_ACCT,
		F_PACCT,
		F_USER,
		F_PART,
		F_LFT,
		F_RGT,
		F_QOS,
		F_COUNT
	};

	cond = xstrdup_printf(sent_cond, cluster);
	query = xstrdup_printf("SELECT %s FROM %s.%s AS t1 %s "
			       "ORDER BY lft FOR UPDATE;",
			       ma_fields, cluster, assoc_table, cond);
	xfree(cond);
	result = DEF_QUERY_RET;
	if(!result)
		return SLURM_ERROR;

	if (PQntuples(result) == 0) {
		PQclear(result);
		return SLURM_SUCCESS;
	}

	rc = SLURM_SUCCESS;
	set = 0;
	ret_list = list_create(slurm_destroy_char);
	vals = xstrdup(sent_vals);
	FOR_EACH_ROW {
		slurmdb_association_rec_t *mod_assoc = NULL;
		int account_type=0;
		/* If parent changes these also could change
		   so we need to keep track of the latest
		   ones.
		*/
		uint32_t lft = atoi(ROW(F_LFT));
		uint32_t rgt = atoi(ROW(F_RGT));
		char *account = ROW(F_ACCT);
		/* Here we want to see if the person
		 * is a coord of the parent account
		 * since we don't want him to be able
		 * to alter the limits of the account
		 * he is directly coord of.  They
		 * should be able to alter the
		 * sub-accounts though. If no parent account
		 * that means we are talking about a user
		 * association so account is really the parent
		 * of the user a coord can change that all day long.
		 */
		if(!ISEMPTY(F_PACCT))
			/* parent_acct != '' => user_name = '' */
			account = ROW(F_PACCT);

		if(!is_admin) {

			if (!is_user_coord(user, account)) {
				if(!ISEMPTY(F_PACCT))
					error("User %s(%d) can not modify "
					      "account (%s) because they "
					      "are not coordinators of "
					      "parent account \"%s\".",
					      user->name, user->uid,
					      ROW(F_ACCT),
					      ROW(F_PACCT));
				else
					error("User %s(%d) does not have the "
					      "ability to modify the account "
					      "(%s).",
					      user->name, user->uid,
					      ROW(F_ACCT));
				PQclear(result);
				list_destroy(ret_list);
				errno = ESLURM_ACCESS_DENIED;
				return SLURM_ERROR;
			}
		}

		if(! ISEMPTY(F_PART)) { 	/* partition != '' */
			object = xstrdup_printf(
				"C = %-10s A = %-20s U = %-9s P = %s",
				cluster, ROW(F_ACCT),
				ROW(F_USER), ROW(F_PART));
		} else if(! ISEMPTY(F_USER)){ /* user != '' */
			object = xstrdup_printf(
				"C = %-10s A = %-20s U = %-9s",
				cluster, ROW(F_ACCT),
				ROW(F_USER));
		} else {
			if(assoc->parent_acct) {
				if(!strcasecmp(ROW(F_ACCT),
					       assoc->parent_acct)) {
					error("You can't make an account be "
					      "child of it's self");
					xfree(object);
					continue;
				}

				rc = _move_parent(pg_conn, cluster, ROW(F_ID),
						  &lft, &rgt,
						  ROW(F_PACCT),
						  assoc->parent_acct, now);
				if (rc == ESLURM_INVALID_PARENT_ACCOUNT ||
				    rc == ESLURM_SAME_PARENT_ACCOUNT)
					continue;
				else if (rc != SLURM_SUCCESS)
					break;

				moved_parent = 1;
			}
			if(! ISEMPTY(F_PACCT)) {
				object = xstrdup_printf(
					"C = %-10s A = %s of %s",
					cluster, ROW(F_ACCT),
					ROW(F_PACCT));
			} else {
				object = xstrdup_printf(
					"C = %-10s A = %s",
					cluster, ROW(F_ACCT));
			}
			account_type = 1;
		}
		list_append(ret_list, object);

		if(!set) {
			xstrfmtcat(name_char, "(id_assoc=%s", ROW(F_ID));
			set = 1;
		} else {
			xstrfmtcat(name_char, " OR id_assoc=%s", ROW(F_ID));
		}

		mod_assoc = xmalloc(sizeof(slurmdb_association_rec_t));
		slurmdb_init_association_rec(mod_assoc, 0);
		mod_assoc->id = atoi(ROW(F_ID));
		mod_assoc->cluster = xstrdup(cluster);
		//mod_assoc->acct = xstrdup(assoc->acct);
		_copy_assoc_limits(mod_assoc, assoc);

		/* no need to get the parent id since if we moved
		 * parent id's we will get it when we send the total list */
		if(ISEMPTY(F_USER))
			mod_assoc->parent_acct = xstrdup(assoc->parent_acct);

		if(assoc->qos_list && list_count(assoc->qos_list)) {
			ListIterator new_qos_itr =
				list_iterator_create(assoc->qos_list);
			char *new_qos = NULL, *tmp_qos = NULL;
			char *tmp_delta = NULL;
			int delta = 0;

			mod_assoc->qos_list = list_create(slurm_destroy_char);
			while((new_qos = list_next(new_qos_itr))) {
				if(new_qos[0] == '-' || new_qos[0] == '+') {
					list_append(mod_assoc->qos_list,
						    xstrdup(new_qos));
					delta = 1;
				} else if(new_qos[0]) {
					list_append(mod_assoc->qos_list,
						    xstrdup_printf("=%s",
								   new_qos));
				}
			}
			if (set_qos_vals)
				goto qos_vals_set;

			if (! delta) {
				list_iterator_reset(new_qos_itr);
				while((new_qos = list_next(new_qos_itr))) {
					if (!new_qos[0]) {
						xstrcat(tmp_qos, "");
						continue;
					}
					xstrfmtcat(tmp_qos, ",%s", new_qos);
				}
				xstrfmtcat(vals, ", qos='%s', delta_qos=''",
					   tmp_qos);
				xfree(tmp_qos);

			} else {
				tmp_qos = xstrdup("qos");
				tmp_delta = xstrdup("delta_qos");
				list_iterator_reset(new_qos_itr);
				while((new_qos = list_next(new_qos_itr))) {
					if (!new_qos[0]) {
						continue;
					} else if (new_qos[0] == '+') {
						tmp_qos = xstrdup_printf(
							"(replace(%s, ',%s', '') || ',%s')",
							tmp_qos, new_qos+1, new_qos+1);
						tmp_delta = xstrdup_printf(
							"(replace(replace(%s, ',+%s', ''), "
							"',-%s', '') || ',%s')",
							tmp_delta, new_qos+1, new_qos+1, new_qos);
					} else if (new_qos[0] == '-') {
						tmp_qos = xstrdup_printf(
							"replace(%s, ',%s', '')",
							tmp_qos, new_qos+1);
						tmp_delta = xstrdup_printf(
							"(replace(replace(%s, ',+%s', ''), "
							"',-%s', '') || ',%s')",
							tmp_delta, new_qos+1, new_qos+1, new_qos);
					} else {
						fatal("as/pg: delta=1 with non-delta qos");
					}
				}
				xstrfmtcat(vals, ", qos=(CASE WHEN qos='' THEN '' "
					   "ELSE %s END), delta_qos=(CASE WHEN "
					   "qos='' THEN %s ELSE '' END)",
					   tmp_qos, tmp_delta);
				xfree(tmp_qos);
				xfree(tmp_delta);
			}
			set_qos_vals = 1;

		qos_vals_set:
			list_iterator_destroy(new_qos_itr);
		}

		/* TODO: how about here set_assoc_parent_limits_... */
		_set_assoc_limits_for_add(pg_conn, mod_assoc); /* XXX: parent account?  */

		if(addto_update_list(pg_conn->update_list,
				     SLURMDB_MODIFY_ASSOC,
				     mod_assoc) != SLURM_SUCCESS)
			error("couldn't add to the update list");
		if(account_type) { /* propagate change to sub account and users */
			_modify_unset_users(pg_conn,
					    cluster,
					    mod_assoc,
					    ROW(F_ACCT),
					    lft, rgt,
					    ret_list,
					    moved_parent);
		}
	} END_EACH_ROW;
	PQclear(result);

	if(assoc->parent_acct) {
		if ((rc == ESLURM_INVALID_PARENT_ACCOUNT ||
		     rc == ESLURM_SAME_PARENT_ACCOUNT) &&
		    list_count(ret_list))
			rc = SLURM_SUCCESS;

		if(rc != SLURM_SUCCESS) {
			list_destroy(ret_list);
			errno = rc;
			return SLURM_ERROR;
		}
	}


	if(!list_count(ret_list)) {
		debug3("didn't effect anything");
		xfree(vals);
		list_destroy(ret_list);
		errno = SLURM_NO_CHANGE_IN_DATA;
		return SLURM_SUCCESS;
	}
	xstrcat(name_char, ")");

	if(vals) {
		char *table = xstrdup_printf("%s.%s", cluster, assoc_table);
		user_name = uid_to_string((uid_t) user->uid);
		rc = pgsql_modify_common(pg_conn, DBD_MODIFY_ASSOCS, now,
					 cluster, user_name, table,
					 name_char, vals);
		xfree(user_name);
		xfree(table);
		if (rc != SLURM_SUCCESS) {
			error("Couldn't modify associations");
			list_destroy(ret_list);
			ret_list = NULL;
			return rc;
		}
	}
	if(moved_parent) {
		List cl = list_create(NULL);
		list_append(cl, cluster);
		rc = _clusters_assoc_update(pg_conn, cl, user->uid);
		list_destroy(cl);
	}

	list_transfer(sent_list, ret_list);
	list_destroy(ret_list);
	return rc;
}


/*
 * as_pg_modify_associations - modify associations
 *
 * IN pg_conn: database connection
 * IN uid: user performing the modify operation
 * IN assoc_cond: which associations to modify
 * IN assoc: attribute of associations after modification
 * RET: list of users modified
 */
extern List
as_pg_modify_associations(pgsql_conn_t *pg_conn, uint32_t uid,
 			  slurmdb_association_cond_t *assoc_cond,
 			  slurmdb_association_rec_t *assoc)
{
 	List ret_list = NULL;
 	char *vals = NULL, *cond = NULL;
 	int is_admin=0, rc = SLURM_SUCCESS;
 	slurmdb_user_rec_t user;

 	if(!assoc_cond || !assoc) {
 		error("as/pg: modify_associations: nothing to change");
 		return NULL;
 	}
 	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
 		return NULL;

 	if (check_user_op(pg_conn, uid, 0, &is_admin, &user) != SLURM_SUCCESS) {
 		error("as/pg: user(%u) not found in db", uid);
 		errno = ESLURM_USER_ID_MISSING;
 		return NULL;
 	}

 	if (!is_admin && !is_user_any_coord(pg_conn, &user)) {
 		error("only admins/coords can modify associations");
 		errno = ESLURM_ACCESS_DENIED;
 		return NULL;
 	}

 	cond = _make_assoc_cond(assoc_cond);
 	if (!cond) {		/* never happens */
 		error("as/pg: modify_associations: null condition");
 		return NULL;
 	}

 	/* This needs to be here to make sure we only modify the
 	   correct set of associations The first clause was already
 	   taken care of above. */
 	if (assoc_cond->user_list && !list_count(assoc_cond->user_list)) {
 		debug4("no user specified looking at users");
 		xstrcat(cond, " AND user_name!='' ");
 	} else if (!assoc_cond->user_list) {
 		debug4("no user specified looking at accounts");
 		xstrcat(cond, " AND user_name='' ");
 	}

 	_make_assoc_limit_vals(assoc, &vals);
 	if((!vals && !assoc->parent_acct
 	    && (!assoc->qos_list || !list_count(assoc->qos_list)))) {
 		error("Nothing to change");
 		xfree(cond);
 		errno = SLURM_NO_CHANGE_IN_DATA;
 		return NULL;
 	}

 	ret_list = list_create(slurm_destroy_char);
 	FOR_EACH_CLUSTER(assoc_cond->cluster_list) {

 		rc = _cluster_modify_associations(pg_conn, cluster_name, assoc,
 						  cond, vals, is_admin, &user,
 						  ret_list);
 		if (rc != SLURM_SUCCESS) {
 			list_destroy(ret_list);
 			ret_list = NULL;
 			break;
 		}
 	} END_EACH_CLUSTER;
 	xfree(cond);
 	xfree(vals);

 	return ret_list;
}

/* get running jobs of specifed assoc */
/* assoc_cond format: "t1.id_assoc=id OR t1.id_assoc=id ... */
static List
_get_assoc_running_jobs(pgsql_conn_t *pg_conn, char *cluster, char *assoc_cond)
{
 	DEF_VARS;
 	List job_list = NULL;
 	char *job = NULL;
 	char *fields = "t0.id_job,t1.acct,t1.user_name,t1.partition";

 	query = xstrdup_printf(
 		"SELECT DISTINCT %s, '%s' FROM %s.%s AS t0, "
 		"%s.%s AS t1 WHERE (%s) AND "
 		"t0.id_assoc=t1.id_assoc AND t0.state=%d AND "
 		"t0.time_end=0", fields, cluster, cluster,
 		job_table, cluster, assoc_table, assoc_cond,
 		JOB_RUNNING);

 	result = DEF_QUERY_RET;
 	if (!result)
 		return NULL;

 	FOR_EACH_ROW {
 		if (ISEMPTY(2)) {
 			error("how could job %s running on non-user "
 			      "assoc <%s, %s, '', ''>", ROW(0),
 			      ROW(4), ROW(1));
 			continue;
 		}
 		job = xstrdup_printf(
 			"JobID = %-10s C = %-10s A = %-10s U = %-9s",
 			ROW(0), ROW(4), ROW(1), ROW(2));
 		if(!ISEMPTY(3))
 			xstrfmtcat(job, " P = %s", ROW(3));
 		if (!job_list)
 			job_list = list_create(slurm_destroy_char);
 		list_append(job_list, job);
 	} END_EACH_ROW;
 	PQclear(result);
 	return job_list;
}

/* whether specifed assoc has jobs in db */
/* assoc_cond format: "t1.id_assoc=id OR t1.id_assoc=id ... */
static int
_assoc_has_jobs(pgsql_conn_t *pg_conn, char *cluster, char *assoc_cond)
{
 	DEF_VARS;
 	int has_jobs = 0;

 	xstrfmtcat(query, "SELECT t0.id_assoc FROM %s.%s AS t0, "
 		   "%s.%s AS t1 WHERE (%s) AND "
 		   "t0.id_assoc=t1.id_assoc LIMIT 1;",
 		   cluster, job_table, cluster, assoc_table,
 		   assoc_cond);
 	result = DEF_QUERY_RET;
 	if (result) {
 		has_jobs = (PQntuples(result) != 0);
 		PQclear(result);
 	}
 	return has_jobs;
}

static int
_cluster_remove_associations(pgsql_conn_t *pg_conn, char *cluster,
 			     char *sent_cond, int is_admin,
 			     slurmdb_user_rec_t *user,
 			     List sent_list, List *job_list)
{
 	DEF_VARS;
 	char *name_char = NULL, *assoc_char = NULL, *object = NULL;
 	time_t now = time(NULL);
 	int rc = SLURM_SUCCESS, has_jobs;
 	char *user_name, *id_assoc, *cond;
 	char *ra_fields = "id_assoc,acct,parent_acct,user_name,partition,lft";
 	List ret_list, assoc_id_list;
 	ListIterator itr;
	uint32_t smallest_lft = 0xFFFFFFFF;
 	enum {
 		F_ID,
 		F_ACCT,
 		F_PACCT,
 		F_USER,
 		F_PART,
 		/* For SELECT DISTINCT, ORDER BY expr must be in select list */
 		F_LFT,
 		F_COUNT
 	};

 	cond = xstrdup_printf(sent_cond, cluster);
 	/* TODO: "SELECT DISTINCT ... FOR UPDATE" not supported */
 	query = xstrdup_printf("SELECT lft, rgt FROM %s.%s AS t1 %s "
 			       "ORDER BY lft FOR UPDATE;",
 			       cluster, assoc_table, cond);
 	xfree(cond);
 	result = DEF_QUERY_RET;
 	if(!result)
 		return SLURM_ERROR;

 	FOR_EACH_ROW {
 		if(! name_char)
 			xstrfmtcat(name_char, "lft BETWEEN %s AND %s",
 				   ROW(0), ROW(1));
 		else
 			xstrfmtcat(name_char, " OR lft BETWEEN %s AND %s",
 				   ROW(0), ROW(1));
 	} END_EACH_ROW;
 	PQclear(result);

 	if(!name_char) {
 		return SLURM_SUCCESS;
 	}

 	query = xstrdup_printf(
 		"SELECT DISTINCT %s FROM %s.%s WHERE (%s) ORDER BY lft;",
 		ra_fields, cluster, assoc_table, name_char);
 	xfree(name_char);
 	result = DEF_QUERY_RET;
 	if (!result) {
 		return SLURM_ERROR;
 	}

 	ret_list = list_create(slurm_destroy_char);
 	assoc_id_list = list_create(slurm_destroy_char);
 	FOR_EACH_ROW {
 		uint32_t lft;
 		slurmdb_association_rec_t *rem_assoc = NULL;
 		if(!is_admin && !is_user_coord(user, ROW(F_ACCT))) {
 			error("User %s(%d) does not have the "
 			      "ability to change this account (%s)",
 			      user->name, user->uid, ROW(F_ACCT));
 			errno = ESLURM_ACCESS_DENIED;
 			rc = SLURM_ERROR;
 			break;
 		}
 		if(! ISEMPTY(F_PART)) {
 			object = xstrdup_printf(
 				"C = %-10s A = %-10s U = %-9s P = %s",
 				cluster, ROW(F_ACCT),
 				ROW(F_USER), ROW(F_PART));
 		} else if(! ISEMPTY(F_USER)){
 			object = xstrdup_printf(
 				"C = %-10s A = %-10s U = %-9s",
 				cluster, ROW(F_ACCT),
 				ROW(F_USER));
 		} else {
 			if(! ISEMPTY(F_PACCT)) {
 				object = xstrdup_printf(
 					"C = %-10s A = %s of %s",
 					cluster, ROW(F_ACCT),
 					ROW(F_PACCT));
 			} else {
 				object = xstrdup_printf(
 					"C = %-10s A = %s",
 					cluster, ROW(F_ACCT));
 			}
 		}
 		list_append(ret_list, object);
 		list_append(assoc_id_list, xstrdup(ROW(F_ID)));
 		if(! assoc_char)
 			xstrfmtcat(assoc_char, "t1.id_assoc=%s", ROW(F_ID));
 		else
 			xstrfmtcat(assoc_char, " OR t1.id_assoc=%s", ROW(F_ID));
 		if (!name_char)
 			xstrfmtcat(name_char, "id_assoc=%s", ROW(F_ID));
 		else
 			xstrfmtcat(name_char, "OR id_assoc=%s", ROW(F_ID));

 		/* get the smallest lft here to be able to send all
 		   the modified lfts after it.
 		*/
 		lft = atoi(ROW(F_LFT));
 		if(lft < smallest_lft)
 			smallest_lft = lft;

 		rem_assoc = xmalloc(sizeof(slurmdb_association_rec_t));
 		slurmdb_init_association_rec(rem_assoc, 0);
 		rem_assoc->id = atoi(ROW(F_ID));
 		rem_assoc->cluster = xstrdup(cluster);
 		if(addto_update_list(pg_conn->update_list,
 				     SLURMDB_REMOVE_ASSOC,
 				     rem_assoc) != SLURM_SUCCESS)
 			error("couldn't add to the update list");
 	} END_EACH_ROW;
 	PQclear(result);
 	if (rc != SLURM_SUCCESS)
 		goto out;

 	if ((rc = pgsql_get_modified_lfts(pg_conn, cluster, smallest_lft))
 	    != SLURM_SUCCESS)
 		goto out;

 	*job_list = _get_assoc_running_jobs(pg_conn, cluster, assoc_char);
 	if (*job_list) {
 		rc = SLURM_ERROR;
 		goto out;
 	}

 	has_jobs = _assoc_has_jobs(pg_conn, cluster, assoc_char);

 	user_name = uid_to_string((uid_t) user->uid);
 	rc = add_txn(pg_conn, now, cluster, DBD_REMOVE_ASSOCS, name_char,
 		     user_name, "");
 	xfree(user_name);
 	if (rc != SLURM_SUCCESS)
 		goto out;

 	/* mark usages as deleted */
 	cluster_delete_assoc_usage(pg_conn, cluster, now, name_char);

 	if (!has_jobs) {
 		itr = list_iterator_create(assoc_id_list);
 		while((id_assoc = list_next(itr))) {
 			xstrfmtcat(query, "SELECT %s.remove_assoc(%s)",
 				   cluster, id_assoc);
 		}
 		list_iterator_destroy(itr);
 	}

 	/* update associations to clear the limits */
 	query = xstrdup_printf(
 		"UPDATE %s.%s SET mod_time=%ld, deleted=1, def_qos_id=NULL, "
 		"shares=1, max_jobs=NULL, max_nodes_pj=NULL, max_wall_pj=NULL, "
 		"max_cpu_mins_pj=NULL WHERE (%s);", cluster, assoc_table,
 		now, name_char);
 	rc = DEF_QUERY_RET_RC;

out:
 	xfree(name_char);
 	xfree(assoc_char);
 	if (rc == SLURM_SUCCESS) {
 		list_transfer(sent_list, ret_list);
 	}
 	list_destroy(ret_list);
 	return rc;
}

/*
 * as_pg_remove_associations - remove associations
 *
 * IN pg_conn: database connection
 * IN uid: user performing the remove operation
 * IN assoc_cond: which associations to remove
 * RET: associations removed
 */
extern List
as_pg_remove_associations(pgsql_conn_t *pg_conn, uint32_t uid,
			  slurmdb_association_cond_t *assoc_cond)
{
 	List ret_list = NULL, job_list = NULL;
 	int rc = SLURM_SUCCESS, is_admin;
 	slurmdb_user_rec_t user;
 	char *cond;

 	if(!assoc_cond) {
 		error("as/pg: remove_associations: no condition given");
 		return NULL;
 	}
 	if (validate_cluster_list(assoc_cond->cluster_list) != SLURM_SUCCESS) {
 		error("as/pg: invalid cluster name(s) given");
 		errno = ESLURM_CLUSTER_DELETED;
 		return NULL;
 	}

 	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
 		return NULL;

 	if (check_user_op(pg_conn, uid, 0, &is_admin, &user) != SLURM_SUCCESS) {
 		error("as/pg: user(%u) not found in db", uid);
 		errno = ESLURM_USER_ID_MISSING;
 		return NULL;
 	}

 	if (!is_admin && !is_user_any_coord(pg_conn, &user)) {
 		error("Only admin/coords can remove associations");
 		errno = ESLURM_ACCESS_DENIED;
 		return NULL;
 	}

 	cond = _make_assoc_cond(assoc_cond);

 	ret_list = list_create(slurm_destroy_char);
 	FOR_EACH_CLUSTER(assoc_cond->cluster_list) {
 		rc = _cluster_remove_associations(pg_conn, cluster_name, cond,
 						  is_admin, &user, ret_list,
 						  &job_list);
 		if (rc != SLURM_SUCCESS)
 			break;
 	} END_EACH_CLUSTER;
 	xfree(cond);

 	if (rc != SLURM_SUCCESS) {
		reset_pgsql_conn(pg_conn);
 		list_destroy(ret_list);
 		if (job_list) {
 			errno = ESLURM_JOBS_RUNNING_ON_ASSOC;
 			ret_list = job_list;
 		} else
 			ret_list = NULL;
 	}
 	return ret_list;
}

/*
 * as_pg_get_associations - get associations
 *
 * IN pg_conn: database connection
 * IN uid: user performing the get operation
 * IN assoc_cond: assocations to return
 * RET: assocations got
 */
extern List
as_pg_get_associations(pgsql_conn_t *pg_conn, uid_t uid,
 		       slurmdb_association_cond_t *assoc_cond)
{
 	char *cond = NULL;
 	int rc = 0, is_admin=1;
 	slurmdb_user_rec_t user;
 	List assoc_list = NULL;

 	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
 		return NULL;

 	if (check_user_op(pg_conn, uid, PRIVATE_DATA_USERS, &is_admin, &user)
 	    != SLURM_SUCCESS) {
 		error("as/pg: user(%u) not found in db", uid);
 		errno = ESLURM_USER_ID_MISSING;
 		return NULL;
 	}


 	if(!assoc_cond)
 		xstrcat(cond, " WHERE deleted=0");
 	else
 		cond = _make_assoc_cond(assoc_cond);

 	assoc_list = list_create(slurmdb_destroy_association_rec);

 	FOR_EACH_CLUSTER(assoc_cond->cluster_list) {
 		if (assoc_cond->cluster_list &&
 		    list_count(assoc_cond->cluster_list) &&
 		    ! cluster_in_db(pg_conn, cluster_name)) {
			error("cluster %s no in db, ignored", cluster_name);
			continue;
		}

 		rc = _cluster_get_assocs(pg_conn, cluster_name, assoc_cond,
 					 cond, is_admin, &user, assoc_list);
 		if (rc != SLURM_SUCCESS) {
 			list_destroy(assoc_list);
 			assoc_list = NULL;
 			break;
 		}
 	} END_EACH_CLUSTER;
 	xfree(cond);

 	return assoc_list;
}

/*
 * add_cluster_root_assoc - add root association for newly added cluster
 *
 * IN pg_conn: database connection
 * IN now: current time
 * IN cluster: cluster object
 * OUT txn_info: txn info for cluster addition
 * RET: error code
 */
extern int
add_cluster_root_assoc(pgsql_conn_t *pg_conn, time_t now,
 		       slurmdb_cluster_rec_t *cluster, char **txn_info)
{
 	int rc = SLURM_SUCCESS;
 	char *rec = NULL, *query;
 	PGresult *result;

 	_make_cluster_root_assoc_rec(now, cluster, &rec, txn_info);
 	query = xstrdup_printf("SELECT %s.add_root_assoc(%s);",
 			       cluster->name, rec);
 	xfree(rec);
 	result = DEF_QUERY_RET;
 	if (!result) {
 		error("as/pg: failed to add cluster root association");
 		rc = SLURM_ERROR;
 	}
 	PQclear(result);
 	return rc;
}

/*
 * get user of association. Returned string should be xfree-ed by caller.
 */
extern char *
get_user_from_associd(pgsql_conn_t *pg_conn, char *cluster,
 		      uint32_t associd)
{
 	char *user_name = NULL, *query = NULL;
 	PGresult *result = NULL;

 	query = xstrdup_printf("SELECT user_name FROM %s.%s WHERE id_assoc=%u",
 			       cluster, assoc_table, associd);
  	result = DEF_QUERY_RET;
  	if(!result)
  		return NULL;
	if (PQntuples(result))
		user_name = xstrdup(PG_VAL(0));
	PQclear(result);
	return user_name;
}


extern int
pgsql_get_modified_lfts(pgsql_conn_t *pg_conn,
			char *cluster_name, uint32_t start_lft)
{
	DEF_VARS;


	query = xstrdup_printf(
		"SELECT id_assoc, lft FROM %s.%s WHERE lft > %u",
		cluster_name, assoc_table, start_lft);
	result = DEF_QUERY_RET;
	if (!result) {
		error("couldn't query the database for modified lfts");
		return SLURM_ERROR;
	}

	FOR_EACH_ROW {
		slurmdb_association_rec_t *assoc =
			xmalloc(sizeof(slurmdb_association_rec_t));
		slurmdb_init_association_rec(assoc, 0);
		assoc->id = atoi(ROW(0));
		assoc->lft = atoi(ROW(1));
		assoc->cluster = xstrdup(cluster_name);
		if(addto_update_list(pg_conn->update_list,
				     SLURMDB_MODIFY_ASSOC,
				     assoc) != SLURM_SUCCESS)
			slurmdb_destroy_association_rec(assoc);
	} END_EACH_ROW;
	return SLURM_SUCCESS;
}
