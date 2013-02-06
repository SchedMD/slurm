/*****************************************************************************\
 *  as_pg_qos.c - accounting interface to pgsql - qos related functions.
 *
 *  $Id: as_pg_qos.c 13061 2008-01-22 21:23:56Z da $
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
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

/* shared table */
static char *qos_table_name = "qos_table";
char *qos_table = "public.qos_table";
static storage_field_t qos_table_fields[] = {
	{ "creation_time", "INTEGER NOT NULL" },
	{ "mod_time", "INTEGER DEFAULT 0 NOT NULL" },
	{ "deleted", "INTEGER DEFAULT 0" },
	{ "id_qos", "SERIAL" },	/* must be same with job_table */
	{ "name", "TEXT NOT NULL" },
	{ "description", "TEXT" },
	{ "max_jobs_per_user", "INTEGER DEFAULT NULL" },
	{ "max_submit_jobs_per_user", "INTEGER DEFAULT NULL" },
	{ "max_cpus_per_job", "INTEGER DEFAULT NULL" },
	{ "max_nodes_per_job", "INTEGER DEFAULT NULL" },
	{ "max_wall_duration_per_job", "INTEGER DEFAULT NULL" },
	{ "max_cpu_mins_per_job", "BIGINT DEFAULT NULL" },
        { "max_cpu_run_mins_per_user", "BIGINT DEFAULT NULL" },
	{ "grp_jobs", "INTEGER DEFAULT NULL" },
	{ "grp_submit_jobs", "INTEGER DEFAULT NULL" },
	{ "grp_cpus", "INTEGER DEFAULT NULL" },
	{ "grp_mem", "INTEGER DEFAULT NULL" },
	{ "grp_nodes", "INTEGER DEFAULT NULL" },
	{ "grp_wall", "INTEGER DEFAULT NULL" },
	{ "grp_cpu_mins", "BIGINT DEFAULT NULL" },
        { "grp_cpu_run_mins", "BIGINT DEFAULT NULL" },
	{ "preempt", "TEXT DEFAULT '' NOT NULL" },
        { "preempt_mode", "INT DEFAULT 0" },
	{ "priority", "INTEGER DEFAULT 0" },
	{ "usage_factor", "FLOAT DEFAULT 1.0 NOT NULL" },
	{ NULL, NULL}
};
static char *qos_table_constraint = ", "
	"PRIMARY KEY (id_qos), UNIQUE (name)"
	")";

char *default_qos_str = NULL;

/*
 * _create_function_add_qos - create a PL/pgSQL function to add qos
 *
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_add_qos(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION public.add_qos "
		"(rec %s) RETURNS INTEGER AS $$"
		"DECLARE qos_id INTEGER; "
		"BEGIN LOOP "
		"  BEGIN "
		"    INSERT INTO %s (creation_time, mod_time, deleted, id_qos,"
		"        name, description, max_jobs_per_user, "
		"        max_submit_jobs_per_user, max_cpus_per_job, "
		"        max_nodes_per_job, max_wall_duration_per_job, "
		"        max_cpu_mins_per_job, max_cpu_run_mins_per_user, "
		"        grp_jobs, grp_submit_jobs, grp_cpus, grp_mem, grp_nodes, "
		"        grp_wall, grp_cpu_mins, grp_cpu_run_mins, preempt, "
		"        preempt_mode, priority, usage_factor) "
		"      VALUES (rec.creation_time, rec.mod_time, "
		"        0, DEFAULT, rec.name, rec.description, "
		"        rec.max_jobs_per_user, "
		"        rec.max_submit_jobs_per_user, "
		"        rec.max_cpus_per_job, rec.max_nodes_per_job, "
		"        rec.max_wall_duration_per_job, "
		"        rec.max_cpu_mins_per_job, "
		"        rec.max_cpu_run_mins_per_user, "
		"        rec.grp_jobs, rec.grp_submit_jobs, rec.grp_cpus, rec.grp_mem, "
		"        rec.grp_nodes, rec.grp_wall, rec.grp_cpu_mins, "
		"        rec.grp_cpu_run_mins, rec.preempt, rec.preempt_mode, "
		"        rec.priority, rec.usage_factor) "
		"      RETURNING id_qos INTO qos_id;"
		"    RETURN qos_id;"
		"  EXCEPTION WHEN UNIQUE_VIOLATION THEN"
		"    UPDATE %s SET"
		"        (deleted, mod_time, description, max_jobs_per_user, "
		"         max_submit_jobs_per_user, max_cpus_per_job, "
		"         max_nodes_per_job, max_wall_duration_per_job, "
		"         max_cpu_mins_per_job, max_cpu_run_mins_per_user, "
		"         grp_jobs, grp_submit_jobs, grp_cpus, grp_mem, grp_nodes, "
		"         grp_wall, grp_cpu_mins, grp_cpu_run_mins, "
		"         preempt, preempt_mode, priority, usage_factor) = "
		"        (0, rec.mod_time, rec.description, "
		"         rec.max_jobs_per_user, "
		"         rec.max_submit_jobs_per_user, "
		"         rec.max_cpus_per_job, rec.max_nodes_per_job, "
		"         rec.max_wall_duration_per_job, "
		"         rec.max_cpu_mins_per_job, "
		"         rec.max_cpu_run_mins_per_user, "
		"         rec.grp_jobs, rec.grp_submit_jobs, rec.grp_cpus, rec.grp_mem, "
		"         rec.grp_nodes, rec.grp_wall, rec.grp_cpu_mins, "
		"         rec.grp_cpu_run_mins, rec.preempt, rec.preempt_mode, "
		"         rec.priority, rec.usage_factor) "
		"      WHERE name=rec.name "
		"      RETURNING id_qos INTO qos_id;"
		"    IF FOUND THEN RETURN qos_id; END IF;"
		"  END; "
		"END LOOP; END; $$ LANGUAGE PLPGSQL;",
		qos_table, qos_table, qos_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * _make_qos_record_for_add - make a QOS_TABLE record for insertion
 *
 * IN object: qos record
 * OUT rec: qos_table record string
 * OUT txn: transaction info string
 * RET: error code. *rec and *txn will be xfree-ed on error.
 */
static int
_make_qos_record_for_add(slurmdb_qos_rec_t *object, time_t now,
			 char **rec, char **txn)
{
	*rec = xstrdup_printf("(%ld, %ld, 0, %d, '%s', '%s', ",
			      now, /* creation_time */
			      now, /* mod_time */
			      /* deleted is 0 */
			      object->id,/* id_qos, not used */
			      object->name, /* name */
			      object->description ?: "" /* description, default '' */
		);
	*txn = xstrdup_printf("description='%s'", object->description);

	/* resource limits default NULL */
	concat_limit_32("max_jobs_per_user", object->max_jobs_pu, rec, txn);
	concat_limit_32("max_submit_jobs_per_user", object->max_submit_jobs_pu, rec, txn);
	concat_limit_32("max_cpus_per_job", object->max_cpus_pj, rec, txn);
	concat_limit_32("max_nodes_per_job", object->max_nodes_pj, rec, txn);
	concat_limit_32("max_wall_duration_per_job", object->max_wall_pj, rec, txn);
	concat_limit_64("max_cpu_mins_per_job", object->max_cpu_mins_pj, rec, txn);
	concat_limit_64("max_cpu_run_mins_per_user", object->max_cpu_run_mins_pu, rec, txn);

	concat_limit_32("grp_jobs", object->grp_jobs, rec, txn);
	concat_limit_32("grp_submit_jobs", object->grp_submit_jobs, rec, txn);
	concat_limit_32("grp_cpus", object->grp_cpus, rec, txn);
	concat_limit_32("grp_mem", object->grp_mem, rec, txn);
	concat_limit_32("grp_nodes", object->grp_nodes, rec, txn);
	concat_limit_32("grp_wall", object->grp_wall, rec, txn);
	concat_limit_64("grp_cpu_mins", object->grp_cpu_mins, rec, txn);
	concat_limit_64("grp_cpu_run_mins", object->grp_cpu_run_mins, rec, txn);

	/* preempt, default '' */
	if (object->preempt_list && list_count(object->preempt_list)) {
		char *val = NULL, *tmp_char = NULL;
		ListIterator object_itr =
			list_iterator_create(object->preempt_list);
		while((tmp_char = list_next(object_itr))) {
			if (tmp_char[0] == '+' || tmp_char[0] == '-') {
				error("`+/-' of preempt not valid "
				      "when adding qos: %s", tmp_char);
				xfree(val);
				xfree(*rec);
				xfree(*txn);
				list_iterator_destroy(object_itr);
				return SLURM_ERROR;
			}
			xstrfmtcat(val, ",%s", tmp_char);
		}
		list_iterator_destroy(object_itr);

		xstrfmtcat(*rec, "'%s', ", val);
		xstrfmtcat(*txn, "preempt=%s", val);
		xfree(val);
	} else {
		xstrfmtcat(*rec, "'', ");
	}

	if ((object->preempt_mode != (uint16_t)NO_VAL)
	   && ((int16_t)object->preempt_mode >= 0)) {
		object->preempt_mode &= (~PREEMPT_MODE_GANG);
		xstrfmtcat(*rec, "%u, ", object->preempt_mode);
		xstrfmtcat(*txn, ", preempt_mode=%u", object->preempt_mode);
	}
 
	/* priority, default 0 */
	if (object->priority == INFINITE) {
		xstrcat(*rec, "NULL, ");
		xstrcat(*txn, "priority=NULL, ");
	} else if (object->priority != NO_VAL
		   && (int32_t)object->priority >= 0) {
		xstrfmtcat(*rec, "%u, ", object->priority);
		xstrfmtcat(*txn, "priority=%u, ", object->priority);
	} else {
		xstrcat(*rec, "0, ");
	}

	/* usage_factor, default 1.0 */
	if (object->usage_factor == INFINITE ||
	    object->usage_factor == NO_VAL ||
	    (int32_t)object->usage_factor < 0) {
		xstrcat(*rec, "1.0");
		xstrcat(*txn, "usage_factor=1.0");
	} else {
		xstrfmtcat(*rec, "%f", object->usage_factor);
		xstrfmtcat(*txn, "usage_factor=%f", object->usage_factor);
	}
 
	xstrcat(*rec, ")");
	return SLURM_SUCCESS;
}

/*
 * _make_qos_cond - make a SQL query condition string for
 *    qos remove/get/modify
 *
 * IN qos_cond: condition specified
 * RET: condition string. appropriate for pgsql_modify_common
 * NOTE: the string should be xfree-ed by caller
 */
static char *
_make_qos_cond(slurmdb_qos_cond_t *qos_cond)
{
	char *cond = NULL;
	concat_cond_list(qos_cond->description_list, NULL,
			 "description", &cond);
	concat_cond_list(qos_cond->id_list, NULL, "id_qos", &cond);
	concat_cond_list(qos_cond->name_list, NULL, "name", &cond);
	return cond;
}

/*
 * _make_qos_vals_for_modify - make SQL update value string for qos
 *    modify
 * IN qos: new qos record
 * OUT vals: value string. appropriate for pgsql_modify_common
 * OUT added_preempt: preempt qos newly added
 */
static void
_make_qos_vals_for_modify(slurmdb_qos_rec_t *qos, char **vals,
			  char **added_preempt)
{
	if (qos->description)
		xstrfmtcat(*vals, ", description='%s'", qos->description);
	concat_limit_32("max_jobs_per_user", qos->max_jobs_pu, NULL, vals);
	concat_limit_32("max_submit_jobs_per_user", qos->max_submit_jobs_pu,
			NULL, vals);
	concat_limit_32("max_cpus_per_job", qos->max_cpus_pj, NULL, vals);
	concat_limit_32("max_nodes_per_job", qos->max_nodes_pj, NULL, vals);
	concat_limit_32("max_wall_duration_per_job", qos->max_wall_pj,
			NULL, vals);
	concat_limit_64("max_cpu_mins_per_job", qos->max_cpu_mins_pj,
			NULL, vals);
	concat_limit_64("max_cpu_run_mins_per_user", qos->max_cpu_run_mins_pu,
			NULL, vals);
	concat_limit_32("grp_jobs", qos->grp_jobs, NULL, vals);
	concat_limit_32("grp_submit_jobs", qos->grp_submit_jobs, NULL, vals);
	concat_limit_32("grp_cpus", qos->grp_cpus, NULL, vals);
	concat_limit_32("grp_mem", qos->grp_mem, NULL, vals);
	concat_limit_32("grp_nodes", qos->grp_nodes, NULL, vals);
	concat_limit_32("grp_wall", qos->grp_wall, NULL, vals);
	concat_limit_64("grp_cpu_mins", qos->grp_cpu_mins, NULL, vals);
	concat_limit_64("grp_cpu_run_mins", qos->grp_cpu_run_mins, NULL, vals);

	if (qos->preempt_list && list_count(qos->preempt_list)) {
		char *preempt_val = NULL;
		char *tmp_char = NULL, *begin_preempt = NULL;
		ListIterator preempt_itr =
			list_iterator_create(qos->preempt_list);

		begin_preempt = xstrdup("preempt");

		while((tmp_char = list_next(preempt_itr))) {
			if (tmp_char[0] == '-') {
				xstrfmtcat(preempt_val,
					   "replace(%s, ',%s', '')",
					   begin_preempt, tmp_char+1);
				xfree(begin_preempt);
				begin_preempt = preempt_val;
			} else if (tmp_char[0] == '+') {
				xstrfmtcat(preempt_val,
					   "(replace(%s, ',%s', '') || ',%s')",
					   begin_preempt,
					   tmp_char+1, tmp_char+1);
				if (added_preempt)
					xstrfmtcat(*added_preempt, ",%s",
						   tmp_char+1);
				xfree(begin_preempt);
				begin_preempt = preempt_val;
			} else if (tmp_char[0]) {
				xstrfmtcat(preempt_val, ",%s", tmp_char);
				if (added_preempt)
					xstrfmtcat(*added_preempt, ",%s",
						   tmp_char);
			} else
				xstrcat(preempt_val, "");
		}
		list_iterator_destroy(preempt_itr);
		xstrfmtcat(*vals, ", preempt='%s'", preempt_val);
		xfree(preempt_val);
	}

	concat_limit_32("priority", qos->priority, NULL, vals);

	if (qos->usage_factor >= 0) {
		xstrfmtcat(*vals, ", usage_factor=%f", qos->usage_factor);
	} else if ((int)qos->usage_factor == INFINITE) {
		xstrcat(*vals, ", usage_factor=1.0");
	}
	return;
}

/*
 * _preemption_loop - check for loop in QOS preemption
 *
 * IN pg_conn: database connection
 * IN begin_qosid: qos whoes preemptees changed
 * IN preempt_bitstr: (new) preemptees of begin_qosid
 * RET: true if there is loop, false else
 */
static int
_preemption_loop(pgsql_conn_t *pg_conn, int begin_qosid,
		 bitstr_t *preempt_bitstr)
{
	slurmdb_qos_rec_t qos_rec;
	int rc = 0, i=0;

	xassert(preempt_bitstr);

	/* check in the preempt list for all qos's preempted */
	for(i=0; i<bit_size(preempt_bitstr); i++) {
		if (!bit_test(preempt_bitstr, i))
			continue;

		memset(&qos_rec, 0, sizeof(qos_rec));
		qos_rec.id = i;
		assoc_mgr_fill_in_qos(pg_conn, &qos_rec,
				      ACCOUNTING_ENFORCE_QOS,
				      NULL);
		/* check if the begin_qosid is preempted by this qos
		 * if so we have a loop */
		if (qos_rec.preempt_bitstr
		   && bit_test(qos_rec.preempt_bitstr, begin_qosid)) {
			error("QOS id %d has a loop at QOS %s",
			      begin_qosid, qos_rec.name);
			rc = 1;
			break;
		} else if (qos_rec.preempt_bitstr) {
			/*
			 * qos_rec.preempt_bitstr are also (newly introduced)
			 * preemptees of begin_qosid.
			 * i.e., preemption is transitive
			 */
			if ((rc = _preemption_loop(pg_conn, begin_qosid,
						  qos_rec.preempt_bitstr)))
				break;
		}
	}
	return rc;
}


static int
_set_qos_cnt(PGconn *db_conn)
{
	DEF_VARS;

	query = xstrdup_printf("select MAX(id_qos) from %s", qos_table);

	result = pgsql_db_query_ret(db_conn, query);
	xfree(query);
	if (!result)
		return SLURM_ERROR;
	if (PQntuples(result) == 0) {
		PQclear(result);
		return SLURM_ERROR;
	}
	/* Set the current qos_count on the system for
	   generating bitstr of that length.  Since 0 isn't
	   possible as an id we add 1 to the total to burn 0 and
	   start at the 1 bit.
	*/
	g_qos_count = atoi(PG_VAL(0)) + 1;
	PQclear(result);
	return SLURM_SUCCESS;
}


/*
 * check_qos_tables - check qos related tables and functions
 * IN pg_conn: database connection
 * IN user: database owner
 * RET: error code
 */
extern int
check_qos_tables(PGconn *db_conn)
{
	int rc;

	rc = check_table(db_conn, "public", qos_table_name, qos_table_fields,
			 qos_table_constraint);

	rc |= _create_function_add_qos(db_conn);

	/* add default QOS */
	if (rc == SLURM_SUCCESS) {
		int qos_id = 0;
		List char_list = list_create(slurm_destroy_char);
		ListIterator itr = NULL;
		char *qos = NULL, *desc = NULL, *query = NULL;
		time_t now = time(NULL);

		if (slurmdbd_conf && slurmdbd_conf->default_qos) {
			slurm_addto_char_list(char_list,
					      slurmdbd_conf->default_qos);
			desc = "Added as default";
		} else {
			slurm_addto_char_list(char_list, "normal");
			desc = "Normal QOS default";
		}

		itr = list_iterator_create(char_list);
		while((qos = list_next(itr))) {
			query = xstrdup_printf(
				"SELECT public.add_qos("
                                "(%ld, %ld, 0, 0, $$%s$$, $$%s$$, "
				"NULL, NULL, NULL, NULL, NULL, NULL, NULL, "
				"NULL, NULL, NULL, NULL, NULL, NULL, NULL, "
				"NULL, '', 0, 0, 1.0)"
                                ")",
                                (long)now, (long)now,
				/* deleted=0, id not used */ qos, desc
                                /* resource limits all NULL */
				/* preempt='', preempt_mode=0, priority=0,
				   usage_factor=1.0 */
				);
			DEBUG_QUERY;
			qos_id = pgsql_query_ret_id(db_conn, query);
			xfree(query);
			if (!qos_id)
				fatal("problem add default qos '%s'", qos);
			xstrfmtcat(default_qos_str, ",%d", qos_id);
		}
		list_iterator_destroy(itr);
		list_destroy(char_list);

		if (_set_qos_cnt(db_conn) != SLURM_SUCCESS)
			return SLURM_ERROR;
	}
	return rc;
}

/*
 * as_pg_add_qos - add qos
 *
 * IN pg_conn: database connection
 * IN uid: user performing the add operation
 * IN qos_list: qos'es to add
 * RET: error code
 */
extern int
as_pg_add_qos(pgsql_conn_t *pg_conn, uint32_t uid, List qos_list)
{
	ListIterator itr = NULL;
	slurmdb_qos_rec_t *object = NULL;
	int rc = SLURM_SUCCESS, added = 0;
	char *query = NULL, *rec = NULL, *txn = NULL, *user_name = NULL;
	time_t now = time(NULL);

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	user_name = uid_to_string((uid_t) uid);
	itr = list_iterator_create(qos_list);
	while((object = list_next(itr))) {
		if (!object->name || !object->name[0]) {
			error("as/pg: add_qos: We need a qos name to add.");
			rc = SLURM_ERROR;
			continue;
		}
		if (_make_qos_record_for_add(object, now, &rec, &txn)
		    != SLURM_SUCCESS) {
			error("as/pg: add_qos: invalid qos attribute.");
			rc = SLURM_ERROR;
			continue;
		}

		xstrfmtcat(query, "SELECT public.add_qos(%s);", rec);
		object->id = DEF_QUERY_RET_ID;
		if (!object->id) {
			error("as/pg: couldn't add qos %s", object->name);
			added=0;
			break;
		}

		rc = add_txn(pg_conn, now, "", DBD_ADD_QOS, object->name,
			     user_name, txn);
		if (rc != SLURM_SUCCESS) {
			error("Couldn't add txn");
		} else {
			if (addto_update_list(pg_conn->update_list,
					     SLURMDB_ADD_QOS,
					     object) == SLURM_SUCCESS)
				list_remove(itr);
			added++;
		}
	}
	list_iterator_destroy(itr);
	xfree(user_name);

	if (!added)
		reset_pgsql_conn(pg_conn);

	return rc;
}

/*
 * as_pg_modify_qos - modify qos
 *
 * IN pg_conn: database connection
 * IN uid: user performing the modify action
 * IN qos_cond: which qos to modify
 * IN qos: new values of qos
 * RET: qos'es modified
 */
extern List
as_pg_modify_qos(pgsql_conn_t *pg_conn, uint32_t uid,
		 slurmdb_qos_cond_t *qos_cond, slurmdb_qos_rec_t *qos)
{
	List ret_list = NULL;
	char *object = NULL, *user_name = NULL, *added_preempt = NULL;
	char *vals = NULL, *cond = NULL, *query = NULL, *name_char = NULL;
	PGresult *result = NULL;
	bitstr_t *preempt_bitstr = NULL;
	time_t now = time(NULL);
	int rc = SLURM_SUCCESS, loop = 0;

	if (!qos_cond || !qos) {
		error("as/pg: modify_qos: we need something to change");
		return NULL;
	}
	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return NULL;

	_make_qos_vals_for_modify(qos, &vals, &added_preempt);
	if (!vals) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		error("Nothing to change");
		return NULL;
	}

	cond = _make_qos_cond(qos_cond);
	if (!cond) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		xfree(vals);
		return NULL;
	}
	if (added_preempt) {
		preempt_bitstr = bit_alloc(g_qos_count);
		bit_unfmt(preempt_bitstr, added_preempt+1);
		xfree(added_preempt);
	}

	query = xstrdup_printf("SELECT name, preempt, id_qos FROM %s "
			       "WHERE deleted=0 %s;", qos_table, cond);
	xfree(cond);
	result = DEF_QUERY_RET;
	if (!result) {
		xfree (vals);
		return NULL;
	}

	rc = 0;
	ret_list = list_create(slurm_destroy_char);
	FOR_EACH_ROW {
		slurmdb_qos_rec_t *qos_rec = NULL;
		if (preempt_bitstr &&
		    _preemption_loop(pg_conn,
				     atoi(ROW(2)),
				     preempt_bitstr)) {
			loop = 1;
			break;
		}
		object = xstrdup(ROW(0));
		list_append(ret_list, object);
		if (!rc) {
			xstrfmtcat(name_char, "(name='%s'", object);
			rc = 1;
		} else  {
			xstrfmtcat(name_char, " OR name='%s'", object);
		}
		qos_rec = xmalloc(sizeof(slurmdb_qos_rec_t));
		qos_rec->name = xstrdup(object);

		qos_rec->grp_cpus = qos->grp_cpus;
		qos_rec->grp_cpu_mins = qos->grp_cpu_mins;
		qos_rec->grp_cpu_run_mins = qos->grp_cpu_run_mins;
		qos_rec->grp_jobs = qos->grp_jobs;
		qos_rec->grp_mem = qos->grp_mem;
		qos_rec->grp_nodes = qos->grp_nodes;
		qos_rec->grp_submit_jobs = qos->grp_submit_jobs;
		qos_rec->grp_wall = qos->grp_wall;

		qos_rec->max_cpus_pj = qos->max_cpus_pj;
		qos_rec->max_cpu_mins_pj = qos->max_cpu_mins_pj;
		qos_rec->max_cpu_run_mins_pu = qos->max_cpu_run_mins_pu;
		qos_rec->max_jobs_pu  = qos->max_jobs_pu;
		qos_rec->max_nodes_pj = qos->max_nodes_pj;
		qos_rec->max_submit_jobs_pu  = qos->max_submit_jobs_pu;
		qos_rec->max_wall_pj = qos->max_wall_pj;

		qos_rec->preempt_mode = qos->preempt_mode;
		qos_rec->priority = qos->priority;

		if (qos->preempt_list) {
			ListIterator new_preempt_itr =
				list_iterator_create(qos->preempt_list);
			char *preempt = ROW(0);
			char *new_preempt = NULL;
			int cleared = 0;

			qos_rec->preempt_bitstr = bit_alloc(g_qos_count);
			if (preempt && preempt[0])
				bit_unfmt(qos_rec->preempt_bitstr, preempt+1);

			while((new_preempt = list_next(new_preempt_itr))) {
				if (new_preempt[0] == '-') {
					bit_clear(qos_rec->preempt_bitstr,
						  atoi(new_preempt+1));
				} else if (new_preempt[0] == '+') {
					bit_set(qos_rec->preempt_bitstr,
						atoi(new_preempt+1));
				} else {
					if (!cleared) {
						cleared = 1;
						bit_nclear(
							qos_rec->preempt_bitstr,
							0,
							g_qos_count - 1);
					}
					bit_set(qos_rec->preempt_bitstr,
						atoi(new_preempt));
				}
			}
			list_iterator_destroy(new_preempt_itr);
		}
		addto_update_list(pg_conn->update_list, SLURMDB_MODIFY_QOS,
				  qos_rec);
	} END_EACH_ROW;
	PQclear(result);
	FREE_NULL_BITMAP(preempt_bitstr);

	if (loop) {
		xfree(vals);
		xfree(name_char);
		list_destroy(ret_list);
		errno = ESLURM_QOS_PREEMPTION_LOOP;
		return NULL;
	}

	if (!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		debug3("didn't effect anything");
		xfree(vals);
		return ret_list;
	}
	xstrcat(name_char, ")");

	user_name = uid_to_string((uid_t) uid);
	rc = pgsql_modify_common(pg_conn, DBD_MODIFY_QOS, now, "",
				user_name, qos_table, name_char, vals);
	xfree(user_name);
	xfree(name_char);
	xfree(vals);
	if (rc != SLURM_SUCCESS) {
		error("Couldn't modify qos");
		list_destroy(ret_list);
		ret_list = NULL;
	}
	return ret_list;
}

/* check whether there are jobs with specified qos-es */
static int
_qos_has_jobs(pgsql_conn_t *pg_conn, char *cond)
{
	DEF_VARS;
	int has_jobs = 0;

	FOR_EACH_CLUSTER(NULL) {
		if (query)
			xstrcat(query, " UNION ");
		xstrfmtcat(query,
			   "SELECT id_assoc FROM %s.%s WHERE %s",
			   cluster_name, job_table, cond);
	} END_EACH_CLUSTER;
	xstrcat(query, " LIMIT 1;");
	result = DEF_QUERY_RET;
	if (result) {
		has_jobs = (PQntuples(result) != 0);
		PQclear(result);
	}
	return has_jobs;
}

/*
 * as_pg_remove_qos - remove qos
 *
 * IN pg_conn: database connection
 * IN uid: user performing the remove operation
 * IN qos_cond: which qos to remove
 * RET: list of qos'es removed
 */
extern List
as_pg_remove_qos(pgsql_conn_t *pg_conn, uint32_t uid,
		 slurmdb_qos_cond_t *qos_cond)
{
	DEF_VARS;
	List ret_list = NULL;
	int rc = SLURM_SUCCESS, has_jobs;
	char *cond = NULL, *user_name = NULL;
	char *name_char = NULL;
	char *qos = NULL, *delta_qos = NULL, *tmp = NULL;
	time_t now = time(NULL);

	if (!qos_cond) {
		error("as/pg: remove_qos: we need something to remove");
		return NULL;
	}

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return NULL;

	cond = _make_qos_cond(qos_cond);
	if (!cond) {
		error("Nothing to remove");
		return NULL;
	}

	query = xstrdup_printf("SELECT id_qos, name FROM %s WHERE deleted=0 %s;",
			       qos_table, cond);
	xfree(cond);
	result = DEF_QUERY_RET;
	if (!result)
		return NULL;

	name_char = NULL;
	qos = xstrdup("qos");
	delta_qos = xstrdup("delta_qos");
	ret_list = list_create(slurm_destroy_char);
	FOR_EACH_ROW {
		slurmdb_qos_rec_t *qos_rec = NULL;
		char *id = ROW(0);
		char *name = ROW(1);

		list_append(ret_list, xstrdup(name));
		if (!name_char)
			xstrfmtcat(name_char, "id_qos='%s'", id);
		else
			xstrfmtcat(name_char, " OR id_qos='%s'", id);

		tmp = xstrdup_printf("replace(%s, ',%s', '')", qos, id);
		xfree(qos);
		qos = tmp;

		tmp = xstrdup_printf("replace(replace(%s, ',+%s', ''),"
				     "',-%s', '')", delta_qos, id, id);
		xfree(delta_qos);
		delta_qos = tmp;

		qos_rec = xmalloc(sizeof(slurmdb_qos_rec_t));
		/* we only need id when removing no real need to init */
		qos_rec->id = atoi(id);
		addto_update_list(pg_conn->update_list, SLURMDB_REMOVE_QOS,
				  qos_rec);
	} END_EACH_ROW;
	PQclear(result);

	if (!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		debug3("didn't effect anything");
		return ret_list;
	}

	/* remove this qos from all the users/accts that have it */
	FOR_EACH_CLUSTER(NULL) {
		query = xstrdup_printf(
			"UPDATE %s.%s SET mod_time=%ld,qos=%s,delta_qos=%s "
			"WHERE deleted=0;", cluster_name, assoc_table, now,
			qos, delta_qos);
	} END_EACH_CLUSTER;
	xfree(qos);
	xfree(delta_qos);
	rc = DEF_QUERY_RET_RC;
	if (rc != SLURM_SUCCESS) {
		reset_pgsql_conn(pg_conn);
		list_destroy(ret_list);
		return NULL;
	}

	user_name = uid_to_string((uid_t) uid);
	/* inline pgsql_remove_common */
	has_jobs = _qos_has_jobs(pg_conn, name_char);
	if (!has_jobs)
		query = xstrdup_printf(
			"DELETE FROM %s WHERE creation_time>%ld AND (%s);",
			qos_table, (now - DELETE_SEC_BACK), name_char);
	xstrfmtcat(query, "UPDATE %s SET mod_time=%ld, deleted=1 "
		   "WHERE deleted=0 AND (%s);", qos_table, now, name_char);
	xstrfmtcat(query, "INSERT INTO %s (timestamp, action, name, actor)"
		   " VALUES (%ld, %d, $$%s$$, '%s');", txn_table, now,
		   SLURMDB_REMOVE_QOS, name_char, user_name);
	rc = DEF_QUERY_RET_RC;
	xfree(name_char);
	xfree(user_name);
	if (rc != SLURM_SUCCESS) {
		reset_pgsql_conn(pg_conn);
		list_destroy(ret_list);
		ret_list = NULL;
	}
	return ret_list;
}

/*
 * as_pg_get_qos - get qos
 *
 * IN pg_conn: database connection
 * IN uid: user performing the get operation
 * IN qos_cond: which qos'es to get
 * RET: list of qos'es got
 */
extern List
as_pg_get_qos(pgsql_conn_t *pg_conn, uid_t uid,
	      slurmdb_qos_cond_t *qos_cond)
{
	DEF_VARS;
	char *cond = NULL;
	List qos_list = NULL;

	/* if this changes you will need to edit the corresponding enum */
	char *gq_fields = "name,description,id_qos,grp_cpu_mins,"
		"grp_cpu_run_mins,grp_cpus,grp_jobs,grp_mem,grp_nodes,grp_submit_jobs,"
		"grp_wall,max_cpu_mins_per_job,max_cpu_run_mins_per_user,"
		"max_cpus_per_job,max_jobs_per_user,max_nodes_per_job,"
		"max_submit_jobs_per_user,max_wall_duration_per_job,preempt,"
		"preempt_mode,priority,usage_factor";
	enum {
		F_NAME,
		F_DESC,
		F_ID,
		F_GCM,
		F_GCRM,
		F_GC,
		F_GJ,
		F_GMEM,
		F_GN,
		F_GSJ,
		F_GW,
		F_MCMPJ,
		F_MCRMPU,
		F_MCPJ,
		F_MJPU,
		F_MNPJ,
		F_MSJPU,
		F_MWPJ,
		F_PREE,
		F_PREEM,
		F_PRIO,
		F_UF,
		F_COUNT
	};

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return NULL;

	if (!qos_cond) {
		query = xstrdup_printf("SELECT %s FROM %s WHERE deleted=0;",
				       gq_fields, qos_table);
	} else {
		cond = _make_qos_cond(qos_cond);
		if (qos_cond->with_deleted)
			query = xstrdup_printf("SELECT %s FROM %s WHERE "
					       "(deleted=0 OR deleted=1) %s",
					       gq_fields, qos_table,
					       cond ?: "");
		else
			query = xstrdup_printf("SELECT %s FROM %s WHERE "
					       "deleted=0 %s",
					       gq_fields, qos_table,
					       cond ?: "");
		xfree(cond);
	}

	result = DEF_QUERY_RET;
	if (!result)
		return NULL;

	qos_list = list_create(slurmdb_destroy_qos_rec);
	FOR_EACH_ROW {
		slurmdb_qos_rec_t *qos = xmalloc(sizeof(slurmdb_qos_rec_t));
		list_append(qos_list, qos);

		if (! ISEMPTY(F_DESC))
			qos->description = xstrdup(ROW(F_DESC));

		qos->id = atoi(ROW(F_ID));

		if (! ISEMPTY(F_NAME))
			qos->name =  xstrdup(ROW(F_NAME));

		if (! ISNULL(F_GCM))
			qos->grp_cpu_mins = atoll(ROW(F_GCM));
		else
			qos->grp_cpu_mins = (uint64_t)INFINITE;
		if (! ISNULL(F_GCRM))
			qos->grp_cpu_run_mins = atoll(ROW(F_GCRM));
		else
			qos->grp_cpu_run_mins = (uint64_t)INFINITE;
		if (! ISNULL(F_GC))
			qos->grp_cpus = atoi(ROW(F_GC));
		else
			qos->grp_cpus = INFINITE;
		if (! ISNULL(F_GJ))
			qos->grp_jobs = atoi(ROW(F_GJ));
		else
			qos->grp_jobs = INFINITE;
		if (! ISNULL(F_GMEM))
			qos->grp_mem = atoi(ROW(F_GMEM));
		else
			qos->grp_mem = INFINITE;
		if (! ISNULL(F_GN))
			qos->grp_nodes = atoi(ROW(F_GN));
		else
			qos->grp_nodes = INFINITE;
		if (! ISNULL(F_GSJ))
			qos->grp_submit_jobs = atoi(ROW(F_GSJ));
		else
			qos->grp_submit_jobs = INFINITE;
		if (! ISNULL(F_GW))
			qos->grp_wall = atoi(ROW(F_GW));
		else
			qos->grp_wall = INFINITE;

		if (! ISNULL(F_MCMPJ))
			qos->max_cpu_mins_pj = atoll(ROW(F_MCMPJ));
		else
			qos->max_cpu_mins_pj = (uint64_t)INFINITE;
		if (! ISNULL(F_MCRMPU))
			qos->max_cpu_run_mins_pu = atoll(ROW(F_MCRMPU));
		else
			qos->max_cpu_run_mins_pu = (uint64_t)INFINITE;
		if (! ISNULL(F_MCPJ))
			qos->max_cpus_pj = atoi(ROW(F_MCPJ));
		else
			qos->max_cpus_pj = INFINITE;
		if (! ISNULL(F_MJPU))
			qos->max_jobs_pu = atoi(ROW(F_MJPU));
		else
			qos->max_jobs_pu = INFINITE;
		if (! ISNULL(F_MNPJ))
			qos->max_nodes_pj = atoi(ROW(F_MNPJ));
		else
			qos->max_nodes_pj = INFINITE;
		if (! ISNULL(F_MSJPU))
			qos->max_submit_jobs_pu = atoi(ROW(F_MSJPU));
		else
			qos->max_submit_jobs_pu = INFINITE;
		if (! ISNULL(F_MWPJ))
			qos->max_wall_pj = atoi(ROW(F_MWPJ));
		else
			qos->max_wall_pj = INFINITE;

		if (! ISEMPTY(F_PREE)) {
			if (!qos->preempt_bitstr)
				qos->preempt_bitstr = bit_alloc(g_qos_count);
			bit_unfmt(qos->preempt_bitstr, ROW(F_PREE)+1);
		}
		if (! ISNULL(F_PREEM))
			qos->preempt_mode = atoi(ROW(F_PREEM));
		if (! ISNULL(F_PRIO))
			qos->priority = atoi(ROW(F_PRIO));

		if (! ISNULL(F_UF))
			qos->usage_factor = atof(ROW(F_UF));
	} END_EACH_ROW;
	PQclear(result);
	return qos_list;
}
