/*****************************************************************************\
 *  association.c - accounting interface to pgsql - association
 *  related functions.
 *
 *  $Id: association.c 13061 2008-01-22 21:23:56Z da $
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

#define SECS_PER_DAY	(24 * 60 * 60)

char *assoc_table = "assoc_table";
static storage_field_t assoc_table_fields[] = {
	{ "creation_time", "INTEGER NOT NULL" },
	{ "mod_time", "INTEGER DEFAULT 0 NOT NULL" },
	{ "deleted", "INTEGER DEFAULT 0" },
	{ "id", "SERIAL" }, /* "serial" is of range integer in PG */
	{ "cluster", "TEXT NOT NULL" },
	{ "acct", "TEXT NOT NULL" },
	{ "user_name", "TEXT NOT NULL DEFAULT ''" }, /* 'user' is
						      * reserved
						      * keyword in PG
						      * */ 
	{ "partition", "TEXT NOT NULL DEFAULT ''" },
	{ "parent_acct", "TEXT NOT NULL DEFAULT ''" },
	{ "lft", "INTEGER NOT NULL" },
	{ "rgt", "INTEGER NOT NULL" },
	{ "fairshare", "INTEGER DEFAULT 1 NOT NULL" },
	{ "max_jobs", "INTEGER DEFAULT NULL" },
	{ "max_submit_jobs", "INTEGER DEFAULT NULL" },
	{ "max_cpus_per_job", "INTEGER DEFAULT NULL" },
	{ "max_nodes_per_job", "INTEGER DEFAULT NULL" },
	{ "max_wall_duration_per_job", "INTEGER DEFAULT NULL" },
	{ "max_cpu_mins_per_job", "BIGINT DEFAULT NULL" },
	{ "grp_jobs", "INTEGER DEFAULT NULL" },
	{ "grp_submit_jobs", "INTEGER DEFAULT NULL" },
	{ "grp_cpus", "INTEGER DEFAULT NULL" },
	{ "grp_nodes", "INTEGER DEFAULT NULL" },
	{ "grp_wall", "INTEGER DEFAULT NULL" },
	{ "grp_cpu_mins", "BIGINT DEFAULT NULL" },
	{ "qos", "TEXT NOT NULL DEFAULT ''" }, /* why blob(bytea in pg)? */
	{ "delta_qos", "TEXT NOT NULL DEFAULT ''" },
	{ NULL, NULL}
};
static char *assoc_table_constraints = ", "
	"PRIMARY KEY (id), "
	"UNIQUE (user_name, acct, cluster, partition), "
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
_create_function_show_assoc_hierarchy(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION show_assoc_hierarchy () "
		"RETURNS SETOF TEXT AS $$ "
		"  SELECT (CASE COUNT(p.cluster) WHEN 1 THEN '' "
		"          ELSE repeat(' ', "
		"                 5*(CAST(COUNT(p.cluster) AS INTEGER)-1)) "
		"               || ' |____ ' END) || c.id || "
		"      E':<\\'' || c.cluster || E'\\', \\'' || c.acct || "
		"      E'\\', \\'' || c.user_name || E'\\', \\'' || "
		"      c.partition || E'\\'>'|| '[' || c.lft || ',' || "
		"      c.rgt || ']' "
		"    FROM assoc_table AS p, assoc_table AS c "
		"    WHERE c.lft BETWEEN p.lft AND p.rgt "
		"    GROUP BY c.cluster, c.acct, c.user_name, c.partition, "
		"      c.lft, c.rgt, c.id"
		"    ORDER BY c.lft;"
		"$$ LANGUAGE SQL;");
	return create_function_xfree(db_conn, create_line);
}

/*
 * _create_function_move_account - create a PL/PGSQL function to move account
 *
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_move_account(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION move_account (plft INTEGER, "
		"INOUT alft INTEGER, INOUT argt INTEGER, cl TEXT, "
		"aid INTEGER, pacct TEXT, mtime INTEGER) AS $$"
		"DECLARE"
		"  diff INTEGER; width INTEGER;"
		"BEGIN "
		"  diff := plft - alft + 1;"
		"  width := argt - alft + 1;"
		""
		"  -- insert to new positon and delete from old position\n"
		"  UPDATE %s "
		"    SET mod_time=mtime, deleted=deleted+2, lft=lft+diff, "
		"      rgt=rgt+diff"
		"    WHERE lft BETWEEN alft AND argt;"
		""
		"  -- make space for the insertion\n"
		"  UPDATE %s "
		"    SET mod_time=mtime, rgt=rgt+width "
		"    WHERE rgt>plft AND deleted<2; "
		"  UPDATE %s "
		"    SET mod_time=mtime, lft=lft+width "
		"    WHERE lft>plft AND deleted<2; "
		""
		"  -- reclaim space for the deletion\n"
		"  UPDATE %s "
		"    SET mod_time=mtime, rgt=rgt-width "
		"    WHERE rgt>argt; "
		"  UPDATE %s "
		"    SET mod_time=mtime, lft=lft-width "
		"    WHERE lft>argt; "
		""
		"  -- clear the deleted flag\n"
		"  UPDATE %s "
		"    SET deleted=deleted-2 "
		"    WHERE deleted>1; "
		""
		"  -- set the parent_acct field\n"
		"  -- get new lft & rgt\n"
		"  UPDATE %s "
		"    SET mod_time=mtime, parent_acct=pacct "
		"    WHERE id=aid "
		"    RETURNING lft,rgt INTO alft,argt;"
		"END; $$ LANGUAGE PLPGSQL;",
		assoc_table, assoc_table, assoc_table, assoc_table,
		assoc_table, assoc_table, assoc_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _create_function_make_space - create a PL/PGSQL function to make space
 *
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_make_space(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION make_space (plft INTEGER, "
		"incr INTEGER) RETURNS VOID AS $$ "
		"BEGIN "
		"  UPDATE %s SET rgt=rgt+incr "
		"    WHERE rgt > plft AND deleted < 2;"
		"  UPDATE %s SET lft=lft+incr "
		"    WHERE lft > plft AND deleted < 2;"
		"  UPDATE %s SET deleted=0 WHERE deleted=2;"
		"  UPDATE %s SET max_rgt=max_rgt+incr;"
		"END; $$ LANGUAGE PLPGSQL;",
		assoc_table, assoc_table, assoc_table, max_rgt_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _create_function_add_assoc - create a PL/PGSQL function to add association
 *
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_add_assoc(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION add_assoc (na %s) "
		"RETURNS INTEGER AS $$ "
		"DECLARE"
		"  na_id INTEGER;"
		"BEGIN LOOP "
		"  BEGIN "
		"    INSERT INTO %s (creation_time, mod_time, deleted, id, "
		"        cluster, acct, user_name, partition, parent_acct, "
		"        lft, rgt, fairshare, max_jobs, max_submit_jobs, "
		"        max_cpus_per_job, max_nodes_per_job, "
		"        max_wall_duration_per_job, max_cpu_mins_per_job, "
		"        grp_jobs, grp_submit_jobs, grp_cpus, grp_nodes, "
		"        grp_wall, grp_cpu_mins, qos, delta_qos) "
		"      VALUES (na.creation_time, na.mod_time, na.deleted, "
		"        DEFAULT, na.cluster, na.acct, na.user_name,"
		"        na.partition, na.parent_acct, na.lft, na.rgt, "
		"        na.fairshare, na.max_jobs, na.max_submit_jobs, "
		"        na.max_cpus_per_job, na.max_nodes_per_job, "
		"        na.max_wall_duration_per_job, "
		"        na.max_cpu_mins_per_job, na.grp_jobs, "
		"        na.grp_submit_jobs, na.grp_cpus, na.grp_nodes, "
		"        na.grp_wall, na.grp_cpu_mins, na.qos, na.delta_qos) "
		"      RETURNING id INTO na_id;"
		"    RETURN na_id; "
		"  EXCEPTION WHEN UNIQUE_VIOLATION THEN "
		"    UPDATE %s "
		"      SET mod_time=na.mod_time, deleted=0, "
		"        fairshare=na.fairshare, "
		"        max_jobs=na.max_jobs, "
		"        max_submit_jobs=na.max_submit_jobs,"
		"        max_cpus_per_job=na.max_cpus_per_job, "
		"        max_nodes_per_job=na.max_nodes_per_job, "
		"        max_wall_duration_per_job=na.max_wall_duration_per_job,"
		"        max_cpu_mins_per_job=na.max_cpu_mins_per_job, "
		"        grp_jobs=na.grp_jobs, "
		"        grp_submit_jobs=na.grp_submit_jobs, "
		"        grp_cpus=na.grp_cpus, grp_nodes=na.grp_nodes, "
		"        grp_wall=na.grp_wall, grp_cpu_mins=na.grp_cpu_mins, "
		"        qos=na.qos, delta_qos=na.delta_qos "
		"      WHERE cluster=na.cluster AND acct=na.acct AND "
		"        user_name=na.user_name AND partition=na.partition"
		"      RETURNING id INTO na_id;"
		"    IF FOUND THEN RETURN na_id; END IF;"
		"  END; "
		"END LOOP; END; $$ LANGUAGE PLPGSQL;",
		assoc_table, assoc_table, assoc_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _create_function_add_assoc_update - create a PL/PGSQL function
 *   to update association when adding association
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_add_assoc_update(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION add_assoc_update (assoc %s) "
		"RETURNS INTEGER AS $$ "
		"DECLARE aid INTEGER;"
		"BEGIN "
		"  UPDATE %s SET mod_time=assoc.mod_time, deleted=0, "
		"    id=nextval('%s_id_seq'), fairshare=assoc.fairshare, "
		"    max_jobs=assoc.max_jobs, "
		"    max_submit_jobs=assoc.max_submit_jobs,"
		"    max_cpus_per_job=assoc.max_cpus_per_job, "
		"    max_nodes_per_job=assoc.max_nodes_per_job, "
		"    max_wall_duration_per_job=assoc.max_wall_duration_per_job,"
		"    max_cpu_mins_per_job=assoc.max_cpu_mins_per_job, "
		"    grp_jobs=assoc.grp_jobs, "
		"    grp_submit_jobs=assoc.grp_submit_jobs, "
		"    grp_cpus=assoc.grp_cpus, grp_nodes=assoc.grp_nodes, "
		"    grp_wall=assoc.grp_wall, grp_cpu_mins=assoc.grp_cpu_mins, "
		"    qos=assoc.qos, delta_qos=assoc.delta_qos "
		"  WHERE cluster=assoc.cluster AND acct=assoc.acct AND "
		"    user_name=assoc.user_name AND partition=assoc.partition"
		"  RETURNING id INTO aid;"
		"  RETURN aid;"
		"END; $$ LANGUAGE PLPGSQL;",
		assoc_table, assoc_table, assoc_table);
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
_create_function_add_root_assoc(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION add_root_assoc(ra %s) "
		"RETURNS VOID AS $$"
		"DECLARE "
		"  mrgt INTEGER;"
		"BEGIN "
		"  UPDATE %s SET max_rgt=max_rgt+2 RETURNING max_rgt INTO mrgt;"
		"  ra.lft := mrgt - 1;"
		"  ra.rgt := mrgt;"
		"  PERFORM add_assoc(ra);"
		"END; $$ LANGUAGE PLPGSQL;",
		assoc_table, max_rgt_table, assoc_table, assoc_table, assoc_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _create_function_remove_assoc - create a PL/PGSQL function to remove
 *   association physically
 *
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_remove_assoc(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION remove_assoc(aid INTEGER) "
		"RETURNS VOID AS $$"
		"DECLARE "
		"  alft INTEGER; argt INTEGER; awid INTEGER;"
		"BEGIN "
		"  SELECT lft, rgt, (rgt - lft + 1) INTO alft, argt, awid "
		"    FROM %s WHERE id=aid;"
		"  IF NOT FOUND THEN RETURN; END IF;"
		"  DELETE FROM %s WHERE lft BETWEEN alft AND argt;"
		"  UPDATE %s SET rgt = rgt - awid WHERE rgt > argt;"
		"  UPDATE %s SET lft = lft - awid WHERE lft > argt;"
		"  UPDATE %s SET max_rgt=max_rgt-awid;"
		"END; $$ LANGUAGE PLPGSQL;",
		assoc_table, assoc_table, assoc_table, assoc_table,
		max_rgt_table);
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
_create_function_init_max_rgt_table(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION init_max_rgt_table() "
		"RETURNS VOID AS $$"
		"BEGIN "
		"  PERFORM * FROM %s LIMIT 1;"
		"  IF FOUND THEN RETURN; END IF;"
		"  INSERT INTO %s VALUES (0);"
		"END; $$ LANGUAGE PLPGSQL;",
		max_rgt_table, max_rgt_table);
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
_create_function_get_parent_limits(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION get_parent_limits(cl TEXT, "
		"  pacct TEXT, OUT mj INTEGER, OUT msj INTEGER, "
		"  OUT mcpj INTEGER, OUT mnpj INTEGER, OUT mwpj INTEGER, "
		"  OUT mcmpj INTEGER, OUT aqos TEXT, OUT delta TEXT) "
		"AS $$"
		"DECLARE "
		"  my_acct TEXT;"
		"BEGIN "
		"  aqos := '';"
		"  delta := '';"
		"  my_acct := pacct;"
		"  WHILE (my_acct!='') AND ((mj IS NULL) OR (msj IS NULL) OR "
		"         (mcpj IS NULL) OR (mnpj IS NULL) OR (mwpj IS NULL) OR "
		"         (mcmpj IS NULL) OR (aqos='')) LOOP "
		"    SELECT parent_acct, COALESCE(mj, max_jobs), "
		"           COALESCE(msj, max_submit_jobs), "
		"           COALESCE(mcpj, max_cpus_per_job), "
		"           COALESCE(mnpj, max_nodes_per_job), "
		"           COALESCE(mwpj, max_wall_duration_per_job), "
		"           COALESCE(mcmpj, max_cpu_mins_per_job), "
		"           CASE aqos WHEN '' THEN qos ELSE aqos END, "
		"           CASE aqos WHEN '' THEN delta_qos || delta ELSE delta END "
		"      INTO my_acct, mj, msj, mcpj, mnpj, mwpj, mcmpj, aqos, "
		"           delta FROM %s "
		"      WHERE cluster=cl AND acct=my_acct AND user_name='' ;"
		"  END LOOP;"
		"END; $$ LANGUAGE PLPGSQL;",
		assoc_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _init_max_rgt_table - insert a init value into max rgt table
 *
 * IN db_conn: database connection
 * RET: error code
 */
static int
_init_max_rgt_table(PGconn *db_conn)
{
	int rc;
	char * query;

	query = xstrdup_printf("SELECT init_max_rgt_table();");
	rc = pgsql_db_query(db_conn, query);
	xfree(query);
	return rc;
}

/*
 * _dump_assoc - show all associations in table
 * IN pg_conn: database connection
 */
static void
_dump_assoc(pgsql_conn_t *pg_conn)
{
	PGresult *result;
	char *query = "SELECT show_assoc_hierarchy();";

	result = pgsql_db_query_ret(pg_conn->db_conn, query);
	if (! result)
		fatal("as/pg: unable to dump assoc");

	debug3("==================== association dump ====================");
	FOR_EACH_ROW {
		debug3("%s", ROW(0));
	} END_EACH_ROW;
	debug3("==========================================================");
}

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
_make_assoc_rec(acct_association_rec_t *assoc, time_t now, int deleted,
		char **rec, char **txn)
{
	*rec = xstrdup_printf("(%d, %d, %d, %d, '%s', '%s', ",
			      now,		/* creation_time */
			      now,		/* mod_time */
			      deleted,	/* deleted */
			      assoc->id, /* id */
			      assoc->cluster,    /* cluster */
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
	if ((int)assoc->shares_raw >= 0) {
		xstrfmtcat(*rec, "%u, ", assoc->shares_raw);
		xstrfmtcat(*txn, ", fairshare=%u", assoc->shares_raw);
	} else {
		strcat(*rec, "1, "); /* fairshare, default 1 */
	}

	concat_limit("max_jobs", assoc->max_jobs, rec, txn);
	concat_limit("max_submit_jobs", assoc->max_submit_jobs, rec, txn);
	concat_limit("max_cpus_per_job", assoc->max_cpus_pj, rec, txn);
	concat_limit("max_nodes_per_job", assoc->max_nodes_pj, rec, txn);
	concat_limit("max_wall_duration_per_job", assoc->max_wall_pj, rec, txn);
	concat_limit("max_cpu_mins_per_job", assoc->max_cpu_mins_pj, rec, txn);
	concat_limit("grp_jobs", assoc->grp_jobs, rec, txn);
	concat_limit("grp_submit_jobs", assoc->grp_submit_jobs, rec, txn);
	concat_limit("grp_cpus", assoc->grp_cpus, rec, txn);
	concat_limit("grp_nodes", assoc->grp_nodes, rec, txn);
	concat_limit("grp_wall", assoc->grp_wall, rec, txn);
	concat_limit("grp_cpu_mins", assoc->grp_cpu_mins, rec, txn);

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
_make_cluster_root_assoc_rec(time_t now, acct_cluster_rec_t *cluster,
			     char **rec, char **txn)
{
	*rec = xstrdup_printf(
		"(%d, %d, 0, 0, '%s', 'root', '', '', '', 0, 0, ",
		now, /* creation_time */
		now, /* mod_time */
		/* deleted = 0 */
		/* id generated */
		cluster->name /* cluster */
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
			   "NULL, "     /* grp_jobs */
			   "NULL, "     /* grp_submit_jobs */
			   "NULL, "     /* grp_cpus */
			   "NULL, "     /* grp_nodes */
			   "NULL, "     /* grp_wall */
			   "NULL, "     /* grp_cpu_mins */
			   "'%s', "     /* qos */
			   "'')",	/* delta_qos */
			   default_qos_str ?: "");
	} else {
		acct_association_rec_t *ra;
		ra = cluster->root_assoc;

		if ((int)(ra->shares_raw) >= 0) {
			xstrfmtcat(*rec, "%u, ", ra->shares_raw);
			xstrfmtcat(*txn, "fairshare=%u, ", ra->shares_raw);
		} else
			xstrcat(*rec, "1, ");

		concat_limit("max_jobs", ra->max_jobs, rec, txn);
		concat_limit("max_submit_jobs", ra->max_submit_jobs, rec, txn);
		concat_limit("max_cpus_per_job", ra->max_cpus_pj, rec, txn);
		concat_limit("max_nodes_per_job", ra->max_nodes_pj, rec, txn);
		concat_limit("max_wall_duration_per_job",
			     ra->max_wall_pj, rec, txn);
		concat_limit("max_cpu_mins_per_job",
			     ra->max_cpu_mins_pj, rec, txn);
		concat_limit("grp_jobs", ra->grp_jobs, rec, txn);
		concat_limit("grp_submit_jobs", ra->grp_submit_jobs, rec, txn);
		concat_limit("grp_cpus", ra->grp_cpus, rec, txn);
		concat_limit("grp_nodes", ra->grp_nodes, rec, txn);
		concat_limit("grp_wall", ra->grp_wall, rec, txn);
		concat_limit("grp_cpu_mins", ra->grp_cpu_mins, rec, txn);

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
 * IN db_conn: database connection
 * IN parent_lft: lft of parent association
 * IN incr: increase of parent and sibling lft/rgt
 * RET: error code
 */
static int
_make_space(PGconn *db_conn, int parent_lft, int incr)
{
	char *query;
	int rc;

	query = xstrdup_printf("SELECT make_space(%d, %d);",
			       parent_lft, incr);
	DEBUG_QUERY;
	rc = pgsql_db_query(db_conn, query);
	xfree(query);
	return rc;
}

/*
 * _get_parent_field - get field of parent association(<c, pa, '', ''>)
 *
 * IN db_conn: database connection
 * IN cluster: cluster of parent association
 * IN pacct: account of parent association
 * IN field: which field to return
 * RET: required field of parent assoc, or NULL on error
 * NOTE: caller should xfree the string returned
 */
static char *
_get_parent_field(PGconn *db_conn, char *cluster, char *pacct, char *field)
{
	char *query;
	PGresult *result = NULL;
	char *val = NULL;

	query = xstrdup_printf("SELECT %s FROM %s WHERE cluster='%s' AND "
			       "acct='%s' AND user_name='' AND deleted=0;",
			       field, assoc_table, cluster, pacct);
	DEBUG_QUERY;
	result = pgsql_db_query_ret(db_conn, query);
	xfree(query);

	if (!result) {
		error("failed to get parent info");
	} else if (PQntuples(result) == 0) {
		error("couldn't find assoc of <%s, %s, '', ''>",
		      cluster, pacct);
	} else {
		val = xstrdup(PG_VAL(0));
	}
	PQclear(result);
	//info("got parent field: %s", val);
	return val;
}

/*
 * _get_parent_id - get id of parent association(<c, pa, '', ''>)
 *
 * IN db_conn: database connection
 * IN cluster: cluster of parent association
 * IN pacct: account of parent association
 * RET: id of parent assoc, 0 for error
 */
static int
_get_parent_id(PGconn *db_conn, char *cluster, char *pacct)
{
	char *id_str;
	int id = 0;

	id_str = _get_parent_field(db_conn, cluster, pacct, "id");
	if (id_str) {
		id = atoi(id_str);
		xfree(id_str);
	}
	return id;
}

/*
 * _get_parent_lft - get lft of parent association(<c, pa, '', ''>)
 *
 * IN db_conn: database connection
 * IN cluster: cluster of parent association
 * IN pacct: account of parent association
 * RET: lft of parent assoc, -1 for error
 */
static int
_get_parent_lft(PGconn *db_conn, char *cluster, char *pacct)
{
	char *lft_str;
	int lft = -1;

	lft_str = _get_parent_field(db_conn, cluster, pacct, "lft");
	if (lft_str) {
		lft = atoi(lft_str);
		xfree(lft_str);
	}
	return lft;
}

/*
 * _move_account - move account association to new parent
 *
 * IN db_conn: database connection
 * IN/OUT lft: lft of account association
 * IN/OUT rgt: rgt of account association
 * IN cluster: cluster of account association
 * IN id: id of account association
 * IN parent: new parent of account
 * IN now: current time
 * RET: error code
 */
static int
_move_account(pgsql_conn_t *pg_conn, uint32_t *lft, uint32_t *rgt,
	      char *cluster, char *id, char *parent, time_t now)
{
	char * query = NULL;
	PGresult *result;
	int rc = SLURM_SUCCESS, plft = -1;

	plft = _get_parent_lft(pg_conn->db_conn, cluster, parent);
	if (plft < 0)
		return ESLURM_INVALID_PARENT_ACCOUNT;

	if ((plft + 1 - *lft) == 0)
		return ESLURM_SAME_PARENT_ACCOUNT;

	query = xstrdup_printf(
		"SELECT * FROM move_account(%d, %d, %d, '%s', %s, '%s', %d);",
		plft, *lft, *rgt, cluster, id, parent, now);
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
 * IN db_conn: database connection
 * IN id: id of account association
 * IN/OUT lft: lft of account association
 * IN/OUT rgt: rgt of account association
 * IN cluster: cluster of account association
 * IN old_parent: old parent of account
 * IN new_parent: new parent of account
 * IN now: current time
 * RET: error code
 */
static int
_move_parent(pgsql_conn_t *pg_conn, char *id, uint32_t *lft, uint32_t  *rgt,
	     char *cluster, char *old_parent, char *new_parent, time_t now)
{
	PGresult *result = NULL;
	char *query = NULL;
	int rc = SLURM_SUCCESS;

	/*
	 * if new_parent is child of this account, move new_parent
	 * to be child of old_parent.
	 */
	query = xstrdup_printf("SELECT id, lft, rgt FROM %s "
			       "WHERE (lft BETWEEN %d AND %d) "
			       "  AND cluster='%s' AND acct='%s' "
			       "  AND user_name='' ORDER BY lft;",
			       assoc_table, *lft, *rgt, cluster, new_parent);
	result = DEF_QUERY_RET;
	if (!result)
		return SLURM_ERROR;

	if (PQntuples(result) > 0) {
		uint32_t child_lft = atoi(PG_VAL(1));
		uint32_t child_rgt = atoi(PG_VAL(2));
		debug4("%s(%s) %s,%s is a child of %s", new_parent,
		       PG_VAL(0), PG_VAL(1), PG_VAL(2), id);
		rc = _move_account(pg_conn, &child_lft, &child_rgt,
				   cluster, PG_VAL(0), old_parent, now);
		_dump_assoc(pg_conn);
	}
	PQclear(result);

	if(rc != SLURM_SUCCESS)
		return rc;

	/*
	 * get the new lft and rgt since they may have changed.
	 */
	query = xstrdup_printf("SELECT lft, rgt FROM %s WHERE id=%s;",
			       assoc_table, id);
	result = DEF_QUERY_RET;
	if(! result)
		return SLURM_ERROR;

	if(PQntuples(result) > 0) {
		/* move account to destination */
		*lft = atoi(PG_VAL(0));
		*rgt = atoi(PG_VAL(1));
		rc = _move_account(pg_conn, lft, rgt,
				   cluster, id, new_parent, now);
		_dump_assoc(pg_conn);
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
_make_assoc_cond(acct_association_cond_t *assoc_cond)
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
		xstrfmtcat(cond, ", %s AS t2 WHERE "
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
		xstrfmtcat(cond, ", %s AS t2 WHERE "
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
	concat_cond_list(assoc_cond->cluster_list,
			 prefix, "cluster", &cond);
	concat_cond_list(assoc_cond->fairshare_list,
			 prefix, "fairshare", &cond);
	concat_cond_list(assoc_cond->grp_cpu_mins_list,
			 prefix, "grp_cpu_mins", &cond);
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
			 prefix, "max_cpu_mins_per_job", &cond);
	concat_cond_list(assoc_cond->max_cpus_pj_list,
			 prefix, "max_cpus_per_job", &cond);
	concat_cond_list(assoc_cond->max_jobs_list,
			 prefix, "max_jobs", &cond);
	concat_cond_list(assoc_cond->max_nodes_pj_list,
			 prefix, "max_nodes_per_job", &cond);
	concat_cond_list(assoc_cond->max_submit_jobs_list,
			 prefix, "max_submit_jobs", &cond);
	concat_cond_list(assoc_cond->max_wall_pj_list,
			 prefix, "max_wall_duration_per_job", &cond);

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
_make_assoc_limit_vals(acct_association_rec_t *assoc, char **vals)
{
	char *tmp = NULL;

	if(!assoc)
		return SLURM_ERROR;

	if((int)assoc->shares_raw >= 0) {
		xstrfmtcat(*vals, ", fairshare=%u", assoc->shares_raw);
	} else if (((int)assoc->shares_raw == INFINITE)) {
		xstrcat(*vals, ", fairshare=1");
		assoc->shares_raw = 1;
	}

	concat_limit("grp_cpu_mins", assoc->grp_cpu_mins, &tmp, vals);
	concat_limit("grp_cpus", assoc->grp_cpus, &tmp, vals);
	concat_limit("grp_jobs", assoc->grp_jobs, &tmp, vals);
	concat_limit("grp_nodes", assoc->grp_nodes, &tmp, vals);
	concat_limit("grp_submit_jobs", assoc->grp_submit_jobs, &tmp, vals);
	concat_limit("grp_wall", assoc->grp_wall, &tmp, vals);
	concat_limit("max_cpu_mins_per_job",
		     assoc->max_cpu_mins_pj, &tmp, vals);
	concat_limit("max_cpus_per_job", assoc->max_cpus_pj, &tmp, vals);
	concat_limit("max_jobs", assoc->max_jobs, &tmp, vals);
	concat_limit("max_nodes_per_job", assoc->max_nodes_pj, &tmp, vals);
	concat_limit("max_submit_jobs", assoc->max_submit_jobs, &tmp, vals);
	concat_limit("max_wall_duration_per_job",
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
_copy_assoc_limits(acct_association_rec_t *dest, acct_association_rec_t *src)
{
	dest->shares_raw = src->shares_raw;

	dest->grp_cpus = src->grp_cpus;
	dest->grp_cpu_mins = src->grp_cpu_mins;
	dest->grp_jobs = src->grp_jobs;
	dest->grp_nodes = src->grp_nodes;
	dest->grp_submit_jobs = src->grp_submit_jobs;
	dest->grp_wall = src->grp_wall;

	dest->max_cpus_pj = src->max_cpus_pj;
	dest->max_cpu_mins_pj = src->max_cpu_mins_pj;
	dest->max_jobs = src->max_jobs;
	dest->max_nodes_pj = src->max_nodes_pj;
	dest->max_submit_jobs = src->max_submit_jobs;
	dest->max_wall_pj = src->max_wall_pj;
}

/* Used to get all the users inside a lft and rgt set.  This is just
 * to send the user all the associations that are being modified from
 * a previous change to it's parent.
 */
static int
_modify_unset_users(pgsql_conn_t *pg_conn, acct_association_rec_t *assoc,
		    char *acct, uint32_t lft, uint32_t rgt,
		    List ret_list, int moved_parent)
{
	PGresult *result = NULL;
	char *query = NULL, *object = NULL;
	char *ma_fields = "id,user_name,acct,cluster,partition,max_jobs,"
		"max_submit_jobs,max_nodes_per_job,max_cpus_per_job,"
		"max_wall_duration_per_job,max_cpu_mins_per_job,"
		"qos,delta_qos,lft,rgt";
	enum {
		MA_ID,
		MA_USER,
		MA_ACCT,
		MA_CLUSTER,
		MA_PART,
		MA_MJ,
		MA_MSJ,
		MA_MNPJ,
		MA_MCPJ,
		MA_MWPJ,
		MA_MCMPJ,
		MA_QOS,
		MA_DELTA_QOS,
		MA_LFT,
		MA_RGT,
		MA_COUNT
	};

	if(!ret_list || !acct)
		return SLURM_ERROR;

	/* We want all the sub accounts and user accounts */
	query = xstrdup_printf("SELECT DISTINCT %s FROM %s WHERE deleted=0 "
			       "  AND (lft BETWEEN %d AND %d) "
			       "  AND ((user_name='' AND parent_acct='%s') OR"
			       "       (user_name!='' AND acct='%s')) "
			       "  ORDER BY lft;",
			       ma_fields, assoc_table, lft, rgt, acct, acct);
	result = DEF_QUERY_RET;
	if(!result)
		return SLURM_ERROR;

	FOR_EACH_ROW {
		acct_association_rec_t *mod_assoc = NULL;
		int modified = 0;

		mod_assoc = xmalloc(sizeof(acct_association_rec_t));
		init_acct_association_rec(mod_assoc);

		mod_assoc->id = atoi(ROW(MA_ID));

		if(ISNULL(MA_MJ) && assoc->max_jobs != NO_VAL) {
			mod_assoc->max_jobs = assoc->max_jobs;
			modified = 1;
		}

		if(ISNULL(MA_MSJ) && assoc->max_submit_jobs != NO_VAL) {
			mod_assoc->max_submit_jobs = assoc->max_submit_jobs;
			modified = 1;
		}

		if(ISNULL(MA_MNPJ) && assoc->max_nodes_pj != NO_VAL) {
			mod_assoc->max_nodes_pj = assoc->max_nodes_pj;
			modified = 1;
		}

		if(ISNULL(MA_MCPJ) && assoc->max_cpus_pj != NO_VAL) {
			mod_assoc->max_cpus_pj = assoc->max_cpus_pj;
			modified = 1;
		}

		if(ISNULL(MA_MWPJ) && assoc->max_wall_pj != NO_VAL) {
			mod_assoc->max_wall_pj = assoc->max_wall_pj;
			modified = 1;
		}

		if(ISNULL(MA_MCMPJ) && assoc->max_cpu_mins_pj != NO_VAL) {
			mod_assoc->max_cpu_mins_pj = assoc->max_cpu_mins_pj;
			modified = 1;
		}

		if(ISEMPTY(MA_QOS) && assoc->qos_list) {
			List delta_qos_list = NULL;
			char *qos_char = NULL, *delta_char = NULL;
			ListIterator delta_itr = NULL;
			ListIterator qos_itr =
				list_iterator_create(assoc->qos_list);
			if(! ISEMPTY(MA_DELTA_QOS)) {
				delta_qos_list =
					list_create(slurm_destroy_char);
				slurm_addto_char_list(delta_qos_list,
						      ROW(MA_DELTA_QOS)+1);
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
			if(ISEMPTY(MA_USER)) {
				/* This is a sub account so run it
				 * through as if it is a parent.
				 */
				_modify_unset_users(pg_conn,
						    mod_assoc,
						    ROW(MA_ACCT),
						    atoi(ROW(MA_LFT)),
						    atoi(ROW(MA_RGT)),
						    ret_list, moved_parent);
				destroy_acct_association_rec(mod_assoc);
				continue;
			}
			/* We do want to send all user accounts though */
			mod_assoc->shares_raw = NO_VAL;
			if(! ISEMPTY(MA_PART)) {
				// see if there is a partition name
				object = xstrdup_printf(
					"C = %-10s A = %-20s U = %-9s P = %s",
					ROW(MA_CLUSTER), ROW(MA_ACCT),
					ROW(MA_USER), ROW(MA_PART));
			} else {
				object = xstrdup_printf(
					"C = %-10s A = %-20s U = %-9s",
					ROW(MA_CLUSTER), ROW(MA_ACCT),
					ROW(MA_USER));
			}

			list_append(ret_list, object);

			if(moved_parent)
				destroy_acct_association_rec(mod_assoc);
			else
				if(addto_update_list(pg_conn->update_list,
						      ACCT_MODIFY_ASSOC,
						      mod_assoc)
				   != SLURM_SUCCESS)
					error("couldn't add to "
					      "the update list");
		} else
			destroy_acct_association_rec(mod_assoc);

	} END_EACH_ROW;
	PQclear(result);

	return SLURM_SUCCESS;
}

/*
 * _init_parent_limits - set init value for parent limits
 * IN/OUT passoc: parent association record
 */
static void
_init_parent_limits(acct_association_rec_t *passoc)
{
	passoc->max_jobs = INFINITE;
	passoc->max_submit_jobs = INFINITE;
	passoc->max_cpus_pj = INFINITE;
	passoc->max_nodes_pj = INFINITE;
	passoc->max_wall_pj = INFINITE;
	passoc->max_cpu_mins_pj = INFINITE;
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
		   char *pacct, acct_association_rec_t *passoc,
		   char **qos, char **delta_qos)
{
	PGresult *result = NULL;
	char *query = NULL;
	enum {
		GPL_MJ,
		GPL_MSJ,
		GPL_MCPJ,
		GPL_MNPJ,
		GPL_MWPJ,
		GPL_MCMPJ,
		GPL_QOS,
		GPL_DELTA,
		GPL_COUNT,
	};

	query = xstrdup_printf(
		"SELECT * FROM get_parent_limits('%s', '%s');",
		cluster, pacct);
	result = DEF_QUERY_RET;
	if(! result)
		return SLURM_ERROR;

	if (PQntuples(result) == 0) {
		PQclear(result);
		return SLURM_ERROR;
	}

	passoc->max_jobs = PG_NULL(GPL_MJ) ? INFINITE :
		atoi(PG_VAL(GPL_MJ));
	passoc->max_submit_jobs = PG_NULL(GPL_MSJ) ? INFINITE :
		atoi(PG_VAL(GPL_MSJ));
	passoc->max_cpus_pj = PG_NULL(GPL_MCPJ) ? INFINITE :
		atoi(PG_VAL(GPL_MCPJ));
	passoc->max_nodes_pj = PG_NULL(GPL_MNPJ) ? INFINITE :
		atoi(PG_VAL(GPL_MNPJ));
	passoc->max_wall_pj = PG_NULL(GPL_MWPJ) ? INFINITE :
		atoi(PG_VAL(GPL_MWPJ));
	passoc->max_cpu_mins_pj = PG_NULL(GPL_MCMPJ) ? INFINITE :
		atoll(PG_VAL(GPL_MCMPJ));

	*qos = PG_NULL(GPL_QOS) ? NULL :
		xstrdup(PG_VAL(GPL_QOS));
	*delta_qos = PG_NULL(GPL_DELTA) ? NULL:
		xstrdup(PG_VAL(GPL_DELTA));

	debug3("got parent account limits of <%s, %s>:\n"
	       "\tmax_jobs:%d, max_submit_jobs:%d, max_cpus_pj:%d,\n"
	       "\tmax_nodes_pj:%d, max_wall_pj:%d, max_cpu_mins_pj:%d\n"
	       "\tqos:%s, delta_qos:%s",
	       cluster, pacct, passoc->max_jobs, passoc->max_submit_jobs,
	       passoc->max_cpus_pj, passoc->max_nodes_pj, passoc->max_wall_pj,
	       passoc->max_cpu_mins_pj, *qos, *delta_qos);

	PQclear(result);
	return SLURM_SUCCESS;
}

/*
 * check_assoc_tables - check association related tables and functions
 *
 * IN db_conn: database connection
 * IN user: database owner
 * RET: error code
 */
extern int
check_assoc_tables(PGconn *db_conn, char *user)
{
	int rc;

	rc = check_table(db_conn, assoc_table, assoc_table_fields,
			 assoc_table_constraints, user);
	rc |= check_table(db_conn, max_rgt_table, max_rgt_table_fields,
			  max_rgt_table_constraints, user);

	rc |= _create_function_show_assoc_hierarchy(db_conn);

	rc |= _create_function_init_max_rgt_table(db_conn);
	rc |= _create_function_move_account(db_conn);
	rc |= _create_function_make_space(db_conn);
	rc |= _create_function_add_assoc(db_conn);
	rc |= _create_function_add_assoc_update(db_conn);
	rc |= _create_function_remove_assoc(db_conn);
	rc |= _create_function_add_root_assoc(db_conn);
	rc |= _create_function_get_parent_limits(db_conn);

	rc |= _init_max_rgt_table(db_conn);

	return rc;
}


/*
 * as_p_add_assocaitons - add associations
 *
 * IN pg_conn: database connection
 * IN uid: user performing the add operation
 * IN assoc_list: associations to add
 * RET: error code
 */
extern int
as_p_add_associations(pgsql_conn_t *pg_conn, uint32_t uid,
		      List assoc_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	acct_association_rec_t *object = NULL;
	char *rec = NULL, *txn = NULL, *cond = NULL, *query = NULL;
	char *parent = NULL, *user_name = NULL, *txn_query = NULL;
	int incr = 0, p_lft = 0, p_id = 0, moved_parent = 0;
	PGresult *result = NULL;
	char *old_parent = NULL, *old_cluster = NULL;
	char *last_parent = NULL, *last_cluster = NULL;
	time_t now = time(NULL);
	char *ga_fields = "id, parent_acct, lft, rgt, deleted";
	enum {
		GA_ID,
		GA_PACCT,
		GA_LFT,
		GA_RGT,
		GA_DELETED,
		GA_COUNT
	};

	if(!assoc_list) {
		error("as/pg: add_associations: no association list given");
		return SLURM_ERROR;
	}

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	user_name = uid_to_string((uid_t) uid);
	itr = list_iterator_create(assoc_list);
	while((object = list_next(itr))) {
		if(!object->cluster || !object->acct) {
			error("We need an association cluster and "
			      "acct to add one.");
			rc = SLURM_ERROR;
			continue;
		}

		/* query to check if this assoc is already in DB */
		cond = xstrdup_printf("cluster='%s' AND acct='%s' ",
				      object->cluster, object->acct);
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

		/*
		 * "SELECT DISTINCT ... FOR UPDATE" not supported by PGSQL
		 * But we already have <c, a, u, p> UNIQUE.
		 */
		xstrfmtcat(query, "SELECT %s FROM %s WHERE %s ORDER BY lft "
			   "FOR UPDATE;", ga_fields, assoc_table, cond);
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
					rc = _make_space(pg_conn->db_conn,
							 p_lft, incr);
					if(rc != SLURM_SUCCESS) {
						error("Couldn't make space");
						break;
					}
				}
				/* get new parent info */
				p_lft = _get_parent_lft(pg_conn->db_conn,
							object->cluster,
							parent);
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
			query = xstrdup_printf("SELECT add_assoc(%s);", rec);
			xfree(rec);
		} else if(atoi(PG_VAL(GA_DELETED)) == 0) {
			/* assoc exists and not deleted */
			/* We don't need to do anything here */
			debug("This association was added already");
			PQclear(result);
			continue;
		} else {	/* assoc exists but deleted */
			uint32_t lft = atoi(PG_VAL(GA_LFT));
			uint32_t rgt = atoi(PG_VAL(GA_RGT));

			if(object->parent_acct
			   && strcasecmp(object->parent_acct,
					 PG_VAL(GA_PACCT))) {
				/* We need to move the parent! */
				if(_move_parent(pg_conn,
						PG_VAL(GA_ID),
						&lft, &rgt,
						object->cluster,
						PG_VAL(GA_PACCT),
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
			query = xstrdup_printf("SELECT add_assoc_update(%s);",
					       rec);
			xfree(rec);
		}
		PQclear(result);

		DEBUG_QUERY;
		object->id = pgsql_query_ret_id(pg_conn->db_conn, query);
		xfree(query);
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
				p_id = _get_parent_id(pg_conn->db_conn,
						      object->cluster, parent);
				last_parent = parent;
				last_cluster = object->cluster;
			}
			object->parent_id = p_id;

			if(addto_update_list(pg_conn->update_list,
					     ACCT_ADD_ASSOC,
					     object) == SLURM_SUCCESS) {
				list_remove(itr);
			}
		}

		/* add to txn query string */
		if(txn_query)
			xstrfmtcat(txn_query,
				   ", (%d, %d, '%d', '%s', $$%s$$)",
				   now, DBD_ADD_ASSOCS, object->id, user_name,
				   txn);
		else
			xstrfmtcat(txn_query,
				   "INSERT INTO %s "
				   "(timestamp, action, name, actor, info) "
				   "VALUES (%d, %d, '%d', '%s', $$%s$$)",
				   txn_table, now, DBD_ADD_ASSOCS, object->id,
				   user_name, txn);
		xfree(txn);
	}
	list_iterator_destroy(itr);
	xfree(user_name);

	if(rc == SLURM_SUCCESS && incr) {
		/* _make_space() change delete=2 => deleted=0 */
		rc = _make_space(pg_conn->db_conn, p_lft, incr);
		if(rc != SLURM_SUCCESS) {
			error("Couldn't make space 2");
		}
	}

	if(rc == SLURM_SUCCESS) {
		if(txn_query) {
			xstrcat(txn_query, ";");
			debug3("as/pg(%s:%d) query\n%s", __FILE__,
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

			List assoc_list = NULL;
			ListIterator itr = NULL;
			acct_association_rec_t *assoc = NULL;
			if(!(assoc_list =
			     acct_storage_p_get_associations(pg_conn,
							     uid, NULL)))
				return rc;
			itr = list_iterator_create(assoc_list);
			while((assoc = list_next(itr))) {
				if(addto_update_list(pg_conn->update_list,
						     ACCT_MODIFY_ASSOC,
						     assoc) == SLURM_SUCCESS)
					list_remove(itr);
			}
			list_iterator_destroy(itr);
			list_destroy(assoc_list);
		}
	} else {
		xfree(txn_query);
		if(pg_conn->rollback) {
			pgsql_db_rollback(pg_conn->db_conn);
		}
		list_flush(pg_conn->update_list);
	}
	return rc;
}


/*
 * as_p_modify_associations - modify associations
 *
 * IN pg_conn: database connection
 * IN uid: user performing the modify operation
 * IN assoc_cond: which associations to modify
 * IN assoc: attribute of associations after modification
 * RET: list of users modified
 */
extern List
as_p_modify_associations(pgsql_conn_t *pg_conn, uint32_t uid,
			 acct_association_cond_t *assoc_cond,
			 acct_association_rec_t *assoc)
{
	List ret_list = NULL;
	char *object = NULL, *user_name = NULL;
	char *vals = NULL, *cond = NULL, *query = NULL, *name_char = NULL;
	time_t now = time(NULL);
	int set = 0, i = 0, is_admin=0, rc = SLURM_SUCCESS;
	PGresult *result = NULL;
	acct_user_rec_t user;
	int set_qos_vals = 0;
	int moved_parent = 0;

	char *ma_fields[] = {"id", "acct", "parent_acct", "cluster",
			    "user_name","partition", "lft", "rgt",
			    "qos" };
	enum {
		MA_ID,
		MA_ACCT,
		MA_PACCT,
		MA_CLUSTER,
		MA_USER,
		MA_PART,
		MA_LFT,
		MA_RGT,
		MA_QOS,
		MA_COUNT
	};

	if(!assoc_cond || !assoc) {
		error("as/pg: modify_associations: nothing to change");
		return NULL;
	}

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return NULL;

	memset(&user, 0, sizeof(acct_user_rec_t));
	user.uid = uid;

	is_admin = is_user_admin(pg_conn, uid);
	if (!is_admin && !is_user_any_coord(pg_conn, &user))
		return NULL;

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
		errno = SLURM_NO_CHANGE_IN_DATA;
		error("Nothing to change");
		return NULL;
	}

	for(i = 0; i < MA_COUNT; i ++) {
		if(i)
			xstrcat(object, ", ");
		xstrfmtcat(object, "t1.%s", ma_fields[i]);
	}

	/* TODO: SELECT DISTINCT .. FOR UPDATE not supported */
	query = xstrdup_printf("SELECT %s FROM %s AS t1 %s "
			       "ORDER BY lft FOR UPDATE;",
			       object, assoc_table, cond);
	xfree(object);
	xfree(cond);
	result = DEF_QUERY_RET;
	if(!result)
		return NULL;

	if (! PQntuples(result)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		error("No association to change");
		PQclear(result);
		return NULL;
	}

	rc = SLURM_SUCCESS;
	set = 0;
	ret_list = list_create(slurm_destroy_char);
	FOR_EACH_ROW {
		acct_association_rec_t *mod_assoc = NULL;
		int account_type=0;
		/* If parent changes these also could change
		   so we need to keep track of the latest
		   ones.
		*/
		uint32_t lft = atoi(ROW(MA_LFT));
		uint32_t rgt = atoi(ROW(MA_RGT));

		if(!is_admin) {
			char *account = ROW(MA_ACCT);

			if(!ISEMPTY(MA_PACCT))
				/* parent_acct != '' => user_name = '' */
				account = ROW(MA_PACCT);

			if (!is_coord(&user, account)) {
				if(!ISEMPTY(MA_PACCT))
					error("User %s(%d) can not modify "
					      "account (%s) because they "
					      "are not coordinators of "
					      "parent account \"%s\".",
					      user.name, user.uid,
					      ROW(MA_ACCT),
					      ROW(MA_PACCT));
				else
					error("User %s(%d) does not have the "
					      "ability to modify the account "
					      "(%s).",
					      user.name, user.uid,
					      ROW(MA_ACCT));
				errno = ESLURM_ACCESS_DENIED;
				PQclear(result);
				xfree(vals);
				list_destroy(ret_list);
				if(pg_conn->rollback) {
					pgsql_db_rollback(pg_conn->db_conn);
				}
				return NULL;
			}
		}

		if(! ISEMPTY(MA_PART)) { 	/* partition != '' */
			object = xstrdup_printf(
				"C = %-10s A = %-20s U = %-9s P = %s",
				ROW(MA_CLUSTER), ROW(MA_ACCT),
				ROW(MA_USER), ROW(MA_PART));
		} else if(! ISEMPTY(MA_USER)){ /* user != '' */
			object = xstrdup_printf(
				"C = %-10s A = %-20s U = %-9s",
				ROW(MA_CLUSTER), ROW(MA_ACCT),
				ROW(MA_USER));
		} else {
			if(assoc->parent_acct) {
				if(!strcasecmp(ROW(MA_ACCT),
					       assoc->parent_acct)) {
					error("You can't make an account be "
					      "child of it's self");
					xfree(object);
					continue;
				}

				rc = _move_parent(pg_conn, ROW(MA_ID),
						  &lft, &rgt,
						  ROW(MA_CLUSTER),
						  ROW(MA_PACCT),
						  assoc->parent_acct, now);
				if (rc == ESLURM_INVALID_PARENT_ACCOUNT ||
				    rc == ESLURM_SAME_PARENT_ACCOUNT)
					continue;
				else if (rc != SLURM_SUCCESS)
					break;

				moved_parent = 1;
			}
			if(! ISEMPTY(MA_PACCT)) {
				object = xstrdup_printf(
					"C = %-10s A = %s of %s",
					ROW(MA_CLUSTER), ROW(MA_ACCT),
					ROW(MA_PACCT));
			} else {
				object = xstrdup_printf(
					"C = %-10s A = %s",
					ROW(MA_CLUSTER), ROW(MA_ACCT));
			}
			account_type = 1;
		}
		list_append(ret_list, object);

		if(!set) {
			xstrfmtcat(name_char, "(id=%s", ROW(MA_ID));
			set = 1;
		} else {
			xstrfmtcat(name_char, " OR id=%s", ROW(MA_ID));
		}

		mod_assoc = xmalloc(sizeof(acct_association_rec_t));
		init_acct_association_rec(mod_assoc);
		mod_assoc->id = atoi(ROW(MA_ID));
		_copy_assoc_limits(mod_assoc, assoc);

		/* no need to get the parent id since if we moved
		 * parent id's we will get it when we send the total list */
		if(ISEMPTY(MA_USER))
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
							tmp_qos, new_qos+1, new_qos+1);
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

		if(addto_update_list(pg_conn->update_list,
				      ACCT_MODIFY_ASSOC,
				      mod_assoc) != SLURM_SUCCESS)
			error("couldn't add to the update list");
		if(account_type) { /* propagate change to sub account and users */
			_modify_unset_users(pg_conn,
					    mod_assoc,
					    ROW(MA_ACCT),
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
			if(pg_conn->rollback) {
				pgsql_db_rollback(pg_conn->db_conn);
			}
			list_flush(pg_conn->update_list);
			list_destroy(ret_list);
			xfree(vals);
			errno = rc;
			return NULL;
		}
	}


	if(!list_count(ret_list)) {
		if(pg_conn->rollback) {
			pgsql_db_rollback(pg_conn->db_conn);
		}
		errno = SLURM_NO_CHANGE_IN_DATA;
		debug3("didn't effect anything");
		xfree(vals);
		return ret_list;
	}
	xstrcat(name_char, ")");

	if(vals) {
		user_name = uid_to_string((uid_t) uid);
		rc = aspg_modify_common(pg_conn, DBD_MODIFY_ASSOCS, now,
					user_name, assoc_table, name_char, vals);
		xfree(user_name);
		if (rc == SLURM_ERROR) {
			if(pg_conn->rollback) {
				pgsql_db_rollback(pg_conn->db_conn);
			}
			list_flush(pg_conn->update_list);
			error("Couldn't modify associations");
			list_destroy(ret_list);
			ret_list = NULL;
			goto end_it;
		}
	}
	if(moved_parent) {	/* some assoc parent changed */
		List local_assoc_list = NULL;
		ListIterator local_itr = NULL;
		acct_association_rec_t *local_assoc = NULL;
		//acct_association_cond_t local_assoc_cond;
		/* now we need to send the update of the new parents and
		 * limits, so just to be safe, send the whole
		 * tree because we could have some limits that
		 * were affected but not noticed.
		 */
		/* we can probably just look at the mod time now but
		 * we will have to wait for the next revision number
		 * since you can't query on mod time here and I don't
		 * want to rewrite code to make it happen
		 */

		//memset(&local_assoc_cond, 0, sizeof(acct_association_cond_t));

		if(!(local_assoc_list =
		     acct_storage_p_get_associations(pg_conn,
						     uid, NULL)))
			return ret_list;

		local_itr = list_iterator_create(local_assoc_list);
		while((local_assoc = list_next(local_itr))) {
			if(addto_update_list(pg_conn->update_list,
					      ACCT_MODIFY_ASSOC,
					      local_assoc) == SLURM_SUCCESS)
				list_remove(local_itr);
		}
		list_iterator_destroy(local_itr);
		list_destroy(local_assoc_list);
	}

end_it:
	xfree(name_char);
	xfree(vals);

	return ret_list;
}

/*
 * as_p_remove_associations - remove associations
 *
 * IN pg_conn: database connection
 * IN uid: user performing the remove operation
 * IN assoc_cond: which associations to remove
 * RET: associations removed
 */
extern List
as_p_remove_associations(pgsql_conn_t *pg_conn, uint32_t uid,
			 acct_association_cond_t *assoc_cond)
{
	ListIterator itr = NULL;
	List ret_list = NULL;
	int rc = SLURM_SUCCESS;
	char *object = NULL;
	char *cond = NULL, *query = NULL,
		*name_char = NULL, *assoc_char = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	int is_admin=0;
	PGresult *result = NULL;
	acct_user_rec_t user;

	char *ra_fields = "id, acct, parent_acct, cluster, user_name, partition, lft";
	enum {
		RA_ID,
		RA_ACCT,
		RA_PACCT,
		RA_CLUSTER,
		RA_USER,
		RA_PART,
		/* For SELECT DISTINCT, ORDER BY expr must be in select list */
		RA_LFT,
		RA_COUNT
	};

	if(!assoc_cond) {
		error("as/pg: remove_associations: no condition given");
		return NULL;
	}
	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return NULL;

	memset(&user, 0, sizeof(acct_user_rec_t));
	user.uid = uid;

	is_admin = is_user_admin(pg_conn, uid);
	if (!is_admin && !is_user_any_coord(pg_conn, &user))
		return NULL;

	cond = _make_assoc_cond(assoc_cond);
	/* TODO: "SELECT DISTINCT ... FOR UPDATE" not supported */
	query = xstrdup_printf("SELECT lft, rgt FROM %s AS t1 %s "
			       "ORDER BY lft FOR UPDATE;",
			       assoc_table, cond);
	xfree(cond);
	result = DEF_QUERY_RET;
	if(!result)
		return NULL;

	rc = 0;
	FOR_EACH_ROW {
		if(!rc) {
			xstrfmtcat(name_char, "lft BETWEEN %s AND %s",
				   ROW(0), ROW(1));
			rc = 1;
		} else {
			xstrfmtcat(name_char, " OR lft BETWEEN %s AND %s",
				   ROW(0), ROW(1));
		}
	} END_EACH_ROW;
	PQclear(result);

	if(!name_char) {
		if(pg_conn->rollback) {
			pgsql_db_rollback(pg_conn->db_conn);
		}
		list_flush(pg_conn->update_list);
		errno = SLURM_NO_CHANGE_IN_DATA;
		debug3("as/pg: remove_associations: didn't effect anything");
		return ret_list;
	}

	query = xstrdup_printf("SELECT DISTINCT %s "
			       "FROM %s WHERE (%s) ORDER BY lft;",
			       ra_fields, assoc_table, name_char);
	result = DEF_QUERY_RET;
	if (!result) {
		if(pg_conn->rollback) {
			pgsql_db_rollback(pg_conn->db_conn);
		}
		list_flush(pg_conn->update_list);
		xfree(name_char);
		return NULL;
	}

	rc = 0;
	ret_list = list_create(slurm_destroy_char);
	FOR_EACH_ROW {
		acct_association_rec_t *rem_assoc = NULL;
		if(!is_admin) {
			acct_coord_rec_t *coord = NULL;
			itr = list_iterator_create(user.coord_accts);
			while((coord = list_next(itr))) {
				if(!strcasecmp(coord->name,
					       ROW(RA_ACCT)))
					break;
			}
			list_iterator_destroy(itr);

			if(!coord) {
				error("User %s(%d) does not have the "
				      "ability to change this account (%s)",
				      user.name, user.uid, ROW(RA_ACCT));
				errno = ESLURM_ACCESS_DENIED;
				goto end_it;
			}
		}
		if(! ISEMPTY(RA_PART)) {
			object = xstrdup_printf(
				"C = %-10s A = %-10s U = %-9s P = %s",
				ROW(RA_CLUSTER), ROW(RA_ACCT),
				ROW(RA_USER), ROW(RA_PART));
		} else if(! ISEMPTY(RA_USER)){
			object = xstrdup_printf(
				"C = %-10s A = %-10s U = %-9s",
				ROW(RA_CLUSTER), ROW(RA_ACCT),
				ROW(RA_USER));
		} else {
			if(! ISEMPTY(RA_PACCT)) {
				object = xstrdup_printf(
					"C = %-10s A = %s of %s",
					ROW(RA_CLUSTER), ROW(RA_ACCT),
					ROW(RA_PACCT));
			} else {
				object = xstrdup_printf(
					"C = %-10s A = %s",
					ROW(RA_CLUSTER), ROW(RA_ACCT));
			}
		}
		list_append(ret_list, object);
		if(!rc) {
			xstrfmtcat(assoc_char, "id=%s", ROW(RA_ID));
			rc = 1;
		} else {
			xstrfmtcat(assoc_char, " OR id=%s", ROW(RA_ID));
		}

		rem_assoc = xmalloc(sizeof(acct_association_rec_t));
		init_acct_association_rec(rem_assoc);
		rem_assoc->id = atoi(ROW(RA_ID));
		if(addto_update_list(pg_conn->update_list,
				     ACCT_REMOVE_ASSOC,
				     rem_assoc) != SLURM_SUCCESS)
			error("couldn't add to the update list");
	} END_EACH_ROW;
	PQclear(result);

	user_name = uid_to_string((uid_t) uid);
	rc = aspg_remove_common(pg_conn, DBD_REMOVE_ASSOCS, now, user_name,
				assoc_table, name_char, assoc_char);
	xfree(user_name);
	xfree(name_char);
	xfree(assoc_char);
	if (rc  == SLURM_ERROR)
		goto end_it;

	return ret_list;
end_it:
	if(pg_conn->rollback) {
		pgsql_db_rollback(pg_conn->db_conn);
	}
	list_flush(pg_conn->update_list);

	if(ret_list) {
		list_destroy(ret_list);
		ret_list = NULL;
	}
	PQclear(result);

	return NULL;
}

/*
 * as_p_get_associaitons - get associations
 *
 * IN pg_conn: database connection
 * IN uid: user performing the get operation
 * IN assoc_cond: assocations to return
 * RET: assocations got
 */
extern List
as_p_get_associations(pgsql_conn_t *pg_conn, uid_t uid,
		      acct_association_cond_t *assoc_cond)
{
	char *query = NULL, *cond = NULL;
	List assoc_list = NULL;
	List delta_qos_list = NULL;
	ListIterator itr = NULL;
	int set = 0, is_admin=1;
	PGresult *result = NULL;
	acct_association_rec_t p_assoc;
	char *p_qos = NULL;
	char *p_delta = NULL;
	char *parent_acct = NULL;
	char *last_acct = NULL;
	char *last_cluster = NULL;
	uint32_t parent_id = 0;
	uint16_t private_data = 0;
	acct_user_rec_t user;

	/* needed if we don't have an assoc_cond */
	uint16_t without_parent_info = 0;
	uint16_t without_parent_limits = 0;
	uint16_t with_usage = 0;
	uint16_t with_raw_qos = 0;

	/* if this changes you will need to edit the corresponding enum */
	char *ga_fields = "t1.id, t1.lft, t1.rgt, t1.user_name, t1.acct, "
		"t1.cluster, t1.partition, t1.fairshare, t1.grp_cpu_mins, "
		"t1.grp_cpus, t1.grp_jobs, t1.grp_nodes, t1.grp_submit_jobs, "
		"t1.grp_wall, t1.max_cpu_mins_per_job, t1.max_cpus_per_job, "
		"t1.max_jobs, t1.max_nodes_per_job, t1.max_submit_jobs, "
		"t1.max_wall_duration_per_job, t1.parent_acct, t1.qos, "
		"t1.delta_qos";
	enum {
		GA_ID,
		GA_LFT,
		GA_RGT,
		GA_USER,
		GA_ACCT,
		GA_CLUSTER,
		GA_PART,
		GA_FS,
		GA_GCM,
		GA_GC,
		GA_GJ,
		GA_GN,
		GA_GSJ,
		GA_GW,
		GA_MCMPJ,
		GA_MCPJ,
		GA_MJ,
		GA_MNPJ,
		GA_MSJ,
		GA_MWPJ,
		GA_PARENT,
		GA_QOS,
		GA_DELTA_QOS,
		GA_COUNT
	};

	if(!assoc_cond) {
		xstrcat(cond, " WHERE deleted=0");
		goto empty;
	}

	memset(&user, 0, sizeof(acct_user_rec_t));
	user.uid = uid;

	private_data = slurm_get_private_data();
	if (private_data & PRIVATE_DATA_USERS) {
		is_admin = is_user_admin(pg_conn, uid);
		if (!is_admin)
			assoc_mgr_fill_in_user(pg_conn, &user, 1, NULL);
	}

	cond = _make_assoc_cond(assoc_cond);

	with_raw_qos = assoc_cond->with_raw_qos;
	with_usage = assoc_cond->with_usage;
	without_parent_limits = assoc_cond->without_parent_limits;
	without_parent_info = assoc_cond->without_parent_info;

empty:
	/* this is here to make sure we are looking at only this user
	 * if this flag is set.  We also include any accounts they may be
	 * coordinator of.
	 */
	if(!is_admin && (private_data & PRIVATE_DATA_USERS)) {
		query = xstrdup_printf("SELECT lft FROM %s WHERE user_name='%s'",
				       assoc_table, user.name);
		if(user.coord_accts) {
			acct_coord_rec_t *coord = NULL;
			itr = list_iterator_create(user.coord_accts);
			while((coord = list_next(itr))) {
				xstrfmtcat(query, " OR acct='%s'",
					   coord->name);
			}
			list_iterator_destroy(itr);
		}
		result = DEF_QUERY_RET;
		if(!result) {
			xfree(cond);
			return NULL;
		}

		set = 0;
		FOR_EACH_ROW {
			/* TODO: is the condition right, or reversed? */
			if(set) {
				xstrfmtcat(cond,
					   " OR (%s BETWEEN lft AND rgt)",
					   ROW(0));
			} else {
				set = 1;
				xstrfmtcat(cond,
					   " AND ((%s BETWEEN lft AND rgt)",
					   ROW(0));
			}
		} END_EACH_ROW;
		if(set)
			xstrcat(cond,")");
		PQclear(result);
	}

	query = xstrdup_printf("SELECT DISTINCT %s FROM %s AS t1 %s "
			       "ORDER BY cluster,lft;",
			       ga_fields, assoc_table, cond);
	xfree(cond);
	result = DEF_QUERY_RET;
	if(!result)
		return NULL;

	assoc_list = list_create(destroy_acct_association_rec);
	delta_qos_list = list_create(slurm_destroy_char);
	FOR_EACH_ROW {
		acct_association_rec_t *assoc =
			xmalloc(sizeof(acct_association_rec_t));
		list_append(assoc_list, assoc);

		assoc->id = atoi(ROW(GA_ID));
		assoc->lft = atoi(ROW(GA_LFT));
		assoc->rgt = atoi(ROW(GA_RGT));
		assoc->cluster = xstrdup(ROW(GA_CLUSTER));
		assoc->acct = xstrdup(ROW(GA_ACCT));
		if(! ISEMPTY(GA_USER))
			assoc->user = xstrdup(ROW(GA_USER));
		if(! ISEMPTY(GA_PART))
			assoc->partition = xstrdup(ROW(GA_PART));

		assoc->grp_jobs = ISNULL(GA_GJ) ? INFINITE : atoi(ROW(GA_GJ));
		assoc->grp_cpus = ISNULL(GA_GC) ? INFINITE : atoi(ROW(GA_GC));
		assoc->grp_nodes = ISNULL(GA_GN) ? INFINITE : atoi(ROW(GA_GN));
		assoc->grp_wall = ISNULL(GA_GW) ? INFINITE : atoll(ROW(GA_GW));
		assoc->grp_submit_jobs = ISNULL(GA_GSJ) ? INFINITE : atoi(ROW(GA_GSJ));
		assoc->grp_cpu_mins = ISNULL(GA_GCM) ? INFINITE : atoll(ROW(GA_GCM));
		assoc->shares_raw = ISNULL(GA_FS) ? INFINITE : atoi(ROW(GA_FS));

		parent_acct = ROW(GA_ACCT);
		if(!without_parent_info
		   && !ISEMPTY(GA_PARENT)) {
			assoc->parent_acct = xstrdup(ROW(GA_PARENT));
			parent_acct = ROW(GA_PARENT);
		} else if(!assoc->user) {
			/* (parent_acct='' AND user_name='') => acct='root' */
			parent_acct = NULL;
			parent_id = 0;
			_init_parent_limits(&p_assoc);
			last_acct = NULL;
		}

		if(!without_parent_info && parent_acct &&
		   (!last_acct || !last_cluster
		    || strcmp(parent_acct, last_acct)
		    || strcmp(ROW(GA_CLUSTER), last_cluster))) {

			_init_parent_limits(&p_assoc);
			xfree(p_qos);
			xfree(p_delta);
			parent_id = _get_parent_id(pg_conn->db_conn,
						   ROW(GA_CLUSTER),
						   parent_acct);
			if(!without_parent_limits) {
				if(_get_parent_limits(pg_conn, ROW(GA_CLUSTER),
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
			last_cluster = ROW(GA_CLUSTER);
		}
	no_parent_limits:
		assoc->max_jobs = ISNULL(GA_MJ) ?
			p_assoc.max_jobs : atoi(ROW(GA_MJ));
		assoc->max_submit_jobs = ISNULL(GA_MSJ) ?
			p_assoc.max_submit_jobs: atoi(ROW(GA_MSJ));
		assoc->max_cpus_pj = ISNULL(GA_MCPJ) ?
			p_assoc.max_cpus_pj : atoi(ROW(GA_MCPJ));
		assoc->max_nodes_pj = ISNULL(GA_MNPJ) ?
			p_assoc.max_nodes_pj : atoi(ROW(GA_MNPJ));
		assoc->max_wall_pj = ISNULL(GA_MWPJ) ?
			p_assoc.max_wall_pj : atoi(ROW(GA_MWPJ));
		assoc->max_cpu_mins_pj = ISNULL(GA_MCMPJ) ?
			p_assoc.max_cpu_mins_pj : atoll(ROW(GA_MCMPJ));

		assoc->qos_list = list_create(slurm_destroy_char);
		/* alway with a ',' in qos and delta_qos */
		if(! ISEMPTY(GA_QOS))
			slurm_addto_char_list(assoc->qos_list,
					      ROW(GA_QOS)+1);
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
			if(! ISEMPTY(GA_DELTA_QOS))
				slurm_addto_char_list(delta_qos_list,
						      ROW(GA_DELTA_QOS)+1);
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

	if(with_usage && assoc_list)
		get_usage_for_assoc_list(pg_conn, assoc_list,
					 assoc_cond->usage_start,
					 assoc_cond->usage_end);

	list_destroy(delta_qos_list);

	xfree(p_delta);
	xfree(p_qos);
	//END_TIMER2("get_associations");
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
		       acct_cluster_rec_t *cluster, char **txn_info)
{
	int rc = SLURM_SUCCESS;
	char *rec = NULL, *query;
	PGresult *result;

	_make_cluster_root_assoc_rec(now, cluster, &rec, txn_info);
	query = xstrdup_printf("SELECT add_root_assoc(%s);", rec);
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
 * find_children_assoc - find children associations
 *
 * IN pg_conn: database connection
 * IN parent_cond: condition string of parent associations
 *    FORMAT: "t1.field1=value1 OR t1.field2=value2..."
 * OUT children_list: id list of children associations
 * RET: NULL on error, children association id list else
 */
extern List
find_children_assoc(pgsql_conn_t *pg_conn, char *parent_cond)
{
	char *query = NULL;
	PGresult *result = NULL;
	int rc = 0;
	List ret_list = NULL;

	query = xstrdup_printf(
		"SELECT DISTINCT t0.id FROM %s AS t0, %s AS t1 "
		"  WHERE (t0.lft BETWEEN t1.lft AND t1.rgt) "
		"    AND t0.deleted=0 AND t1.deleted=0"
		"    AND (%s);",
		assoc_table, assoc_table, parent_cond);
	result = DEF_QUERY_RET;
	if(!result) {
		return NULL;
	}

	rc = 0;
	ret_list = list_create(slurm_destroy_char);
	FOR_EACH_ROW {
		slurm_addto_char_list(ret_list, ROW(0));
	} END_EACH_ROW;
	PQclear(result);
	return ret_list;
}


/*
 */
extern int
remove_young_assoc(pgsql_conn_t *pg_conn, time_t now, char *cond)
{
	char *query;
	PGresult *result;
	int rc = SLURM_SUCCESS;
	time_t day_old = now - SECS_PER_DAY;

	query = xstrdup_printf("SELECT id FROM %s AS t1 WHERE "
			       "creation_time>%d AND (%s);",
			       assoc_table, day_old, cond);

	result = DEF_QUERY_RET;
	if(!result)
		return SLURM_ERROR;

	FOR_EACH_ROW {
		query = xstrdup_printf("SELECT remove_assoc(%s);",
				       ROW(0));
		DEBUG_QUERY;
		rc = pgsql_db_query(pg_conn->db_conn, query);
		xfree(query);
		if(rc != SLURM_SUCCESS) {
			error("couldn't remove assoc");
			break;
		}
	} END_EACH_ROW;
	PQclear(result);
	return rc;
}

/*
 * get_assoc_ids - get id list of associations
 *
 * IN pg_conn: database connection
 * IN cond: which associations to get
 *    FORMAT: "AND( ) AND( )..."
 * RET: list of id of associations
 */
extern List
get_assoc_ids(pgsql_conn_t *pg_conn, char *cond)
{
	char *query;
	PGresult *result;
	List ret_list;

	query = xstrdup_printf("SELECT id FROM %s WHERE TRUE %s;",
			       assoc_table, cond);
	result = DEF_QUERY_RET;
	if(!result) {
		error("as/pg: failed to get assoc ids");
		return NULL;
	}

	ret_list = list_create(slurm_destroy_char);
	FOR_EACH_ROW {
		slurm_addto_char_list(ret_list, ROW(0));
	} END_EACH_ROW;
	PQclear(result);
	return ret_list;
}

/*
 * group_concat_assoc_field - get group_concat-ed field value of assocs
 *
 * IN pg_conn: database connection
 * IN field: which field to get
 * IN cond: of which assocs info to get
 *    FORMAT: "field1=value1..."
 * OUT val: group_concat-ed field value
 * RET: error code
 */
extern int
group_concat_assoc_field(pgsql_conn_t *pg_conn, char *field, char *cond,
			 char **val)
{
	PGresult *result;
	char *query = NULL;

	query = xstrdup_printf(
		"SELECT DISTINCT %s FROM %s WHERE deleted=0 AND %s "
		"ORDER BY %s;", field, assoc_table, cond, field);
	result = DEF_QUERY_RET;
	if (!result)
		return SLURM_ERROR;

	FOR_EACH_ROW {
		xstrcat(*val, ROW(0));
		xstrcat(*val, " ");
	} END_EACH_ROW;
	PQclear(result);
	return SLURM_SUCCESS;
}

/*
 * get_cluster_from_associd - get cluster of association
 * IN pg_conn: database connection
 * IN associd: id of association
 * RET: cluster name string, should be xfree-ed by caller
 */
extern char *
get_cluster_from_associd(pgsql_conn_t *pg_conn,
			 uint32_t associd)
{
	char *cluster = NULL, *query = NULL;
	PGresult *result = NULL;

	/* Just so we don't have to keep a
	   cache of the associations around we
	   will just query the db for the cluster
	   name of the association id.  Since
	   this should sort of be a rare case
	   this isn't too bad.
	*/
	query = xstrdup_printf("SELECT cluster FROM %s WHERE id=%u",
			       assoc_table, associd);
	result = DEF_QUERY_RET;
	if(!result)
		return NULL;
	if (PQntuples(result))
		cluster = xstrdup(PG_VAL(0));
	PQclear(result);
	return cluster;
}

/*
 * get_user_from_associd - get user of association
 * IN pg_conn: database connection
 * IN associd: id of association
 * RET: user name string, should be xfree-ed by caller
 */
extern char *
get_user_from_associd(pgsql_conn_t *pg_conn,
		      uint32_t associd)
{
	char *user_name = NULL, *query = NULL;
	PGresult *result = NULL;

	query = xstrdup_printf("SELECT user_name FROM %s WHERE id=%u",
			       assoc_table, associd);
	result = DEF_QUERY_RET;
	if(!result)
		return NULL;
	if (PQntuples(result))
		user_name = xstrdup(PG_VAL(0));
	PQclear(result);
	return user_name;
}
