/*****************************************************************************\
 *  qos.c - accounting interface to pgsql - qos related functions.
 *
 *  $Id: qos.c 13061 2008-01-22 21:23:56Z da $
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

char *qos_table = "qos_table";
static storage_field_t qos_table_fields[] = {
	{ "creation_time", "INTEGER NOT NULL" },
	{ "mod_time", "INTEGER DEFAULT 0 NOT NULL" },
	{ "deleted", "INTEGER DEFAULT 0" },
	{ "id", "SERIAL" },
	{ "name", "TEXT NOT NULL" },
	{ "description", "TEXT" },
	{ "max_jobs_per_user", "INTEGER DEFAULT NULL" },
	{ "max_submit_jobs_per_user", "INTEGER DEFAULT NULL" },
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
	{ "preempt", "TEXT DEFAULT '' NOT NULL" },
	{ "priority", "INTEGER DEFAULT 0" },
	{ "usage_factor", "FLOAT DEFAULT 1.0 NOT NULL" },
	{ NULL, NULL}
};
static char *qos_table_constraint = ", "
	"PRIMARY KEY (id), UNIQUE (name)"
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
		"CREATE OR REPLACE FUNCTION add_qos "
		"(rec %s) RETURNS INTEGER AS $$"
		"DECLARE qos_id INTEGER; "
		"BEGIN LOOP "
		"  BEGIN "
		"    INSERT INTO %s (creation_time, mod_time, deleted, id, "
		"        name, description, max_jobs_per_user, "
		"        max_submit_jobs_per_user, max_cpus_per_job, "
		"        max_nodes_per_job, max_wall_duration_per_job, "
		"        max_cpu_mins_per_job, grp_jobs, grp_submit_jobs, "
		"        grp_cpus, grp_nodes, grp_wall, grp_cpu_mins, preempt, "
		"        priority, usage_factor) "
		"      VALUES (rec.creation_time, rec.mod_time, "
		"        0, DEFAULT, rec.name, rec.description, "
		"        rec.max_jobs_per_user, "
		"        rec.max_submit_jobs_per_user, "
		"        rec.max_cpus_per_job, rec.max_nodes_per_job, "
		"        rec.max_wall_duration_per_job, "
		"        rec.max_cpu_mins_per_job, "
		"        rec.grp_jobs, rec.grp_submit_jobs, rec.grp_cpus, "
		"        rec.grp_nodes, rec.grp_wall, rec.grp_cpu_mins, "
		"        rec.preempt, rec.priority, rec.usage_factor) "
		"      RETURNING id INTO qos_id;"
		"    RETURN qos_id;"
		"  EXCEPTION WHEN UNIQUE_VIOLATION THEN"
		"    UPDATE %s SET"
		"        (deleted, mod_time, description, max_jobs_per_user, "
		"         max_submit_jobs_per_user, max_cpus_per_job, "
		"         max_nodes_per_job, max_wall_duration_per_job, "
		"         max_cpu_mins_per_job, grp_jobs, grp_submit_jobs, "
		"         grp_cpus, grp_nodes, grp_wall, grp_cpu_mins, "
		"         preempt, priority, usage_factor) = "
		"        (0, rec.mod_time, rec.description, "
		"         rec.max_jobs_per_user, "
		"         rec.max_submit_jobs_per_user, "
		"         rec.max_cpus_per_job, rec.max_nodes_per_job, "
		"         rec.max_wall_duration_per_job, "
		"         rec.max_cpu_mins_per_job, "
		"         rec.grp_jobs, rec.grp_submit_jobs, rec.grp_cpus, "
		"         rec.grp_nodes, rec.grp_wall, rec.grp_cpu_mins, "
		"         rec.preempt, rec.priority, rec.usage_factor) "
		"      WHERE name=rec.name "
		"      RETURNING id INTO qos_id;"
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
_make_qos_record_for_add(acct_qos_rec_t *object, time_t now,
			 char **rec, char **txn)
{
	*rec = xstrdup_printf("(%d, %d, 0, %d, '%s', '%s', ",
			      now, /* creation_time */
			      now, /* mod_time */
			      /* deleted is 0 */
			      object->id,/* id, not used */
			      object->name, /* name */
			      object->description ?: "" /* description, default '' */
		);
	*txn = xstrdup_printf("description='%s'", object->description);

	/* resource limits default NULL */
	concat_limit("max_jobs_per_user", object->max_jobs_pu, rec, txn);
	concat_limit("max_submit_jobs_per_user", object->max_submit_jobs_pu, rec, txn);
	concat_limit("max_cpus_per_job", object->max_cpus_pj, rec, txn);
	concat_limit("max_nodes_per_job", object->max_nodes_pj, rec, txn);
	concat_limit("max_wall_duration_per_job", object->max_wall_pj, rec, txn);
	concat_limit("max_cpu_mins_per_job", object->max_cpu_mins_pj, rec, txn);

	concat_limit("grp_jobs", object->grp_jobs, rec, txn);
	concat_limit("grp_submit_jobs", object->grp_submit_jobs, rec, txn);
	concat_limit("grp_cpus", object->grp_cpus, rec, txn);
	concat_limit("grp_nodes", object->grp_nodes, rec, txn);
	concat_limit("grp_wall", object->grp_wall, rec, txn);
	concat_limit("grp_cpu_mins", object->grp_cpu_mins, rec, txn);

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

	/* priority, default 0 */
	if((int)object->priority >= 0) {
		xstrfmtcat(*rec, "%d, ", object->priority);
		xstrfmtcat(*txn, "priority=%d, ", object->priority);
	} else if ((int)object->priority == INFINITE) {
		xstrcat(*rec, "NULL, ");
		xstrcat(*txn, "priority=NULL, ");
	} else {
		xstrcat(*rec, "0, ");
	}

	/* usage_factor, default 1.0 */
	if (object->usage_factor >= 0) {
		xstrfmtcat(*rec, "%d)", object->usage_factor);
		xstrfmtcat(*txn, "usage_factor=%d", object->usage_factor);
	} else {
		xstrcat(*rec, "1.0");
		xstrcat(*txn, "usage_factor=1.0");
	}
	return SLURM_SUCCESS;
}

/*
 * _make_qos_cond - make a SQL query condition string for
 *    qos remove/get/modify
 *
 * IN qos_cond: condition specified
 * RET: condition string. appropriate for aspg_modify_common
 * NOTE: the string should be xfree-ed by caller
 */
static char *
_make_qos_cond(acct_qos_cond_t *qos_cond)
{
	char *cond = NULL;
	concat_cond_list(qos_cond->description_list, NULL,
			 "description", &cond);
	concat_cond_list(qos_cond->id_list, NULL, "id", &cond);
	concat_cond_list(qos_cond->name_list, NULL, "name", &cond);
	return cond;
}

/*
 * _make_qos_vals_for_modify - make SQL update value string for qos
 *    modify
 * IN qos: new qos record
 * OUT vals: value string. appropriate for aspg_modify_common
 * OUT added_preempt: preempt qos newly added
 */
static void
_make_qos_vals_for_modify(acct_qos_rec_t *qos, char **vals,
			  char **added_preempt)
{
	if (qos->description)
		xstrfmtcat(*vals, ", description=''", qos->description);
	concat_limit("max_jobs_per_user", qos->max_jobs_pu, NULL, vals);
	concat_limit("max_submit_jobs_per_user", qos->max_submit_jobs_pu,
		     NULL, vals);
	concat_limit("max_cpus_per_job", qos->max_cpus_pj, NULL, vals);
	concat_limit("max_nodes_per_job", qos->max_nodes_pj, NULL, vals);
	concat_limit("max_wall_duration_per_job", qos->max_wall_pj,
		     NULL, vals);
	concat_limit("max_cpu_mins_per_job", qos->max_cpu_mins_pj,
		     NULL, vals);
	concat_limit("grp_jobs", qos->grp_jobs, NULL, vals);
	concat_limit("grp_submit_jobs", qos->grp_submit_jobs, NULL, vals);
	concat_limit("grp_cpus", qos->grp_cpus, NULL, vals);
	concat_limit("grp_nodes", qos->grp_nodes, NULL, vals);
	concat_limit("grp_wall", qos->grp_wall, NULL, vals);
	concat_limit("grp_cpu_mins", qos->grp_cpu_mins, NULL, vals);

	if(qos->preempt_list && list_count(qos->preempt_list)) {
		char *preempt_val = NULL;
		char *tmp_char = NULL, *begin_preempt = NULL;
		ListIterator preempt_itr =
			list_iterator_create(qos->preempt_list);

		begin_preempt = xstrdup("preempt");

		while((tmp_char = list_next(preempt_itr))) {
			if(tmp_char[0] == '-') {
				xstrfmtcat(preempt_val,
					   "replace(%s, ',%s', '')",
					   begin_preempt, tmp_char+1);
				xfree(begin_preempt);
				begin_preempt = preempt_val;
			} else if(tmp_char[0] == '+') {
				xstrfmtcat(preempt_val,
					   "(replace(%s, ',%s', '') || ',%s')",
					   begin_preempt,
					   tmp_char+1, tmp_char+1);
				if(added_preempt)
					xstrfmtcat(*added_preempt, ",%s",
						   tmp_char+1);
				xfree(begin_preempt);
				begin_preempt = preempt_val;
			} else if(tmp_char[0]) {
				xstrfmtcat(preempt_val, ",%s", tmp_char);
				if(added_preempt)
					xstrfmtcat(*added_preempt, ",%s",
						   tmp_char);
			} else
				xstrcat(preempt_val, "");
		}
		list_iterator_destroy(preempt_itr);
		xstrfmtcat(*vals, ", preempt='%s'", preempt_val);
		xfree(preempt_val);
	}

	concat_limit("priority", qos->priority, NULL, vals);

	if(qos->usage_factor >= 0) {
		xstrfmtcat(*vals, ", usage_factor=%f", qos->usage_factor);
	} else if((int)qos->usage_factor == INFINITE) {
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
	acct_qos_rec_t qos_rec;
	int rc = 0, i=0;

	xassert(preempt_bitstr);

	/* check in the preempt list for all qos's preempted */
	for(i=0; i<bit_size(preempt_bitstr); i++) {
		if(!bit_test(preempt_bitstr, i))
			continue;

		memset(&qos_rec, 0, sizeof(qos_rec));
		qos_rec.id = i;
		assoc_mgr_fill_in_qos(pg_conn, &qos_rec,
				      ACCOUNTING_ENFORCE_QOS,
				      NULL);
		/* check if the begin_qosid is preempted by this qos
		 * if so we have a loop */
		if(qos_rec.preempt_bitstr
		   && bit_test(qos_rec.preempt_bitstr, begin_qosid)) {
			error("QOS id %d has a loop at QOS %s",
			      begin_qosid, qos_rec.name);
			rc = 1;
			break;
		} else if(qos_rec.preempt_bitstr) {
			/*
			 * qos_rec.preempt_bitstr are also (newly introduced)
			 * preemptees of begin_qosid.
			 * i.e., preemption is transitive
			 */
			if((rc = _preemption_loop(pg_conn, begin_qosid,
						  qos_rec.preempt_bitstr)))
				break;
		}
	}
	return rc;
}


static int
_set_qos_cnt(PGconn *db_conn)
{
	PGresult *result = NULL;
	char *query = xstrdup_printf("select MAX(id) from %s", qos_table);

	result = pgsql_db_query_ret(db_conn, query);
	xfree(query);
	if(!result)
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
check_qos_tables(PGconn *db_conn, char *user)
{
	int rc;

	rc = check_table(db_conn, qos_table, qos_table_fields,
			 qos_table_constraint, user);

	rc |= _create_function_add_qos(db_conn);

	/* add default QOS */
	if (rc == SLURM_SUCCESS) {
		int qos_id = 0;
		List char_list = list_create(slurm_destroy_char);
		ListIterator itr = NULL;
		char *qos = NULL, *desc = NULL, *query = NULL;
		time_t now = time(NULL);

		if(slurmdbd_conf && slurmdbd_conf->default_qos) {
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
				"SELECT add_qos("
				"(%d, %d, 0, 0, $$%s$$, $$%s$$, "
				"NULL, NULL, NULL, NULL, NULL, NULL, "
				"NULL, NULL, NULL, NULL, NULL, NULL,"
				"'', 0, 1.0)"
				")",
				now, now, /* deleted=0, id not used */ qos, desc
				/* resource limits all NULL */
				/* preempt='', priority=0, usage_factor=1.0 */
				);
			DEBUG_QUERY;
			qos_id = pgsql_query_ret_id(db_conn, query);
			xfree(query);
			if(!qos_id)
				fatal("problem add default qos '%s", qos);
			xstrfmtcat(default_qos_str, ",%d", qos_id);
		}
		list_iterator_destroy(itr);
		list_destroy(char_list);

		if(_set_qos_cnt(db_conn) != SLURM_SUCCESS)
			return SLURM_ERROR;
	}
	return rc;
}

/*
 * as_p_add_qos - add qos
 *
 * IN pg_conn: database connection
 * IN uid: user performing the add operation
 * IN qos_list: qos'es to add
 * RET: error code
 */
extern int
as_p_add_qos(pgsql_conn_t *pg_conn, uint32_t uid, List qos_list)
{
	ListIterator itr = NULL;
	acct_qos_rec_t *object = NULL;
	int rc = SLURM_SUCCESS, added = 0;
	char *query = NULL, *rec = NULL, *txn = NULL, *user_name = NULL;
	time_t now = time(NULL);

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	user_name = uid_to_string((uid_t) uid);
	itr = list_iterator_create(qos_list);
	while((object = list_next(itr))) {
		if(!object->name) {
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

		xstrfmtcat(query, "SELECT add_qos(%s);", rec);
		DEBUG_QUERY;
		object->id = pgsql_query_ret_id(pg_conn->db_conn, query);
		xfree(query);
		if(!object->id) {
			error("as/pg: couldn't add qos %s", object->name);
			added=0;
			break;
		}

		rc = add_txn(pg_conn, now, DBD_ADD_QOS, object->name,
			     user_name, txn);
		if(rc != SLURM_SUCCESS) {
			error("Couldn't add txn");
		} else {
			if(addto_update_list(pg_conn->update_list,
					     ACCT_ADD_QOS,
					     object) == SLURM_SUCCESS)
				list_remove(itr);
			added++;
		}
	}
	list_iterator_destroy(itr);
	xfree(user_name);

	if(!added) {
		if(pg_conn->rollback) {
			pgsql_db_rollback(pg_conn->db_conn);
		}
		list_flush(pg_conn->update_list);
	}
	return rc;
}

/*
 * as_p_modify_qos - modify qos
 *
 * IN pg_conn: database connection
 * IN uid: user performing the modify action
 * IN qos_cond: which qos to modify
 * IN qos: new values of qos
 * RET: qos'es modified
 */
extern List
as_p_modify_qos(pgsql_conn_t *pg_conn, uint32_t uid,
		acct_qos_cond_t *qos_cond, acct_qos_rec_t *qos)
{
	List ret_list = NULL;
	char *object = NULL, *user_name = NULL, *added_preempt = NULL;
	char *vals = NULL, *cond = NULL, *query = NULL, *name_char = NULL;
	PGresult *result = NULL;
	bitstr_t *preempt_bitstr = NULL;
	time_t now = time(NULL);
	int rc = SLURM_SUCCESS, loop = 0;

	if(!qos_cond || !qos) {
		error("as/pg: modify_qos: we need something to change");
		return NULL;
	}
	if(check_db_connection(pg_conn) != SLURM_SUCCESS)
		return NULL;

	_make_qos_vals_for_modify(qos, &vals, &added_preempt);
	if(!vals) {
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

	query = xstrdup_printf("SELECT name, preemp, id FROM %s "
			       "WHERE deleted=0 %s;", qos_table, cond);
	xfree(cond);
	result = DEF_QUERY_RET;
	if(!result) {
		xfree (vals);
		return NULL;
	}

	rc = 0;
	ret_list = list_create(slurm_destroy_char);
	FOR_EACH_ROW {
		acct_qos_rec_t *qos_rec = NULL;
		if (preempt_bitstr &&
		    _preemption_loop(pg_conn,
				     atoi(ROW(2)),
				     preempt_bitstr)) {
			loop = 1;
			break;
		}
		object = xstrdup(ROW(0));
		list_append(ret_list, object);
		if(!rc) {
			xstrfmtcat(name_char, "(name='%s'", object);
			rc = 1;
		} else  {
			xstrfmtcat(name_char, " OR name='%s'", object);
		}
		qos_rec = xmalloc(sizeof(acct_qos_rec_t));
		qos_rec->name = xstrdup(object);

		qos_rec->grp_cpus = qos->grp_cpus;
		qos_rec->grp_cpu_mins = qos->grp_cpu_mins;
		qos_rec->grp_jobs = qos->grp_jobs;
		qos_rec->grp_nodes = qos->grp_nodes;
		qos_rec->grp_submit_jobs = qos->grp_submit_jobs;
		qos_rec->grp_wall = qos->grp_wall;

		qos_rec->max_cpus_pj = qos->max_cpus_pj;
		qos_rec->max_cpu_mins_pj = qos->max_cpu_mins_pj;
		qos_rec->max_jobs_pu  = qos->max_jobs_pu;
		qos_rec->max_nodes_pj = qos->max_nodes_pj;
		qos_rec->max_submit_jobs_pu  = qos->max_submit_jobs_pu;
		qos_rec->max_wall_pj = qos->max_wall_pj;

		qos_rec->priority = qos->priority;

		if(qos->preempt_list) {
			ListIterator new_preempt_itr =
				list_iterator_create(qos->preempt_list);
			char *preempt = ROW(0);
			char *new_preempt = NULL;
			int cleared = 0;

			qos_rec->preempt_bitstr = bit_alloc(g_qos_count);
			if(preempt && preempt[0])
				bit_unfmt(qos->preempt_bitstr, preempt+1);

			while((new_preempt = list_next(new_preempt_itr))) {
				if(new_preempt[0] == '-') {
					bit_clear(qos_rec->preempt_bitstr,
						  atoi(new_preempt+1));
				} else if(new_preempt[0] == '+') {
					bit_set(qos_rec->preempt_bitstr,
						atoi(new_preempt+1));
				} else {
					if(!cleared) {
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
		addto_update_list(pg_conn->update_list, ACCT_MODIFY_QOS,
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

	if(!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		debug3("didn't effect anything");
		xfree(vals);
		return ret_list;
	}
	xstrcat(name_char, ")");

	user_name = uid_to_string((uid_t) uid);
	rc = aspg_modify_common(pg_conn, DBD_MODIFY_QOS, now,
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

/*
 * as_p_remove_qos - remove qos
 *
 * IN pg_conn: database connection
 * IN uid: user performing the remove operation
 * IN qos_cond: which qos to remove
 * RET: list of qos'es removed
 */
extern List
as_p_remove_qos(pgsql_conn_t *pg_conn, uint32_t uid,
		acct_qos_cond_t *qos_cond)
{
	List ret_list = NULL;
	PGresult *result = NULL;
	int rc = SLURM_SUCCESS;
	char *cond = NULL, *query = NULL, *user_name = NULL;
	char *name_char = NULL, *assoc_char = NULL;
	char *qos = NULL, *delta_qos = NULL, *tmp = NULL;
	time_t now = time(NULL);

	if(!qos_cond) {
		error("as/pg: remove_qos: we need something to remove");
		return NULL;
	}

	if(check_db_connection(pg_conn) != SLURM_SUCCESS)
		return NULL;

	cond = _make_qos_cond(qos_cond);
	if(!cond) {
		error("Nothing to remove");
		return NULL;
	}

	query = xstrdup_printf("SELECT id, name FROM %s WHERE deleted=0 %s;",
			       qos_table, cond);
	xfree(cond);
	result = DEF_QUERY_RET;
	if(!result)
		return NULL;

	name_char = NULL;
	qos = xstrdup("qos");
	delta_qos = xstrdup("delta_qos");
	ret_list = list_create(slurm_destroy_char);
	FOR_EACH_ROW {
		acct_qos_rec_t *qos_rec = NULL;
		char *id = ROW(0);
		char *name = ROW(1);

		list_append(ret_list, xstrdup(name));
		if(!name_char)
			xstrfmtcat(name_char, "id='%s'", id);
		else
			xstrfmtcat(name_char, " OR id='%s'", id);
		if(!assoc_char)
			xstrfmtcat(assoc_char, "t1.qos='%s'", id);
		else
			xstrfmtcat(assoc_char, " OR t1.qos='%s'", id);

		tmp = xstrdup_printf("replace(%s, ',%s', '')", qos, id);
		xfree(qos);
		qos = tmp;

		tmp = xstrdup_printf("replace(replace(%s, ',+%s', ''),"
				     "',-%s', '')", delta_qos, id, id);
		xfree(delta_qos);
		delta_qos = tmp;

		qos_rec = xmalloc(sizeof(acct_qos_rec_t));
		/* we only need id when removing no real need to init */
		qos_rec->id = atoi(id);
		addto_update_list(pg_conn->update_list, ACCT_REMOVE_QOS,
				  qos_rec);
	} END_EACH_ROW;
	PQclear(result);

	if(!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		debug3("didn't effect anything");
		return ret_list;
	}

	/* remove this qos from all the users/accts that have it */
	query = xstrdup_printf("UPDATE %s SET mod_time=%d,qos=%s,delta_qos=%s "
			       "WHERE deleted=0;",
			       assoc_table, now, qos, delta_qos);
	xfree(qos);
	xfree(delta_qos);
	rc = DEF_QUERY_RET_RC;
	if(rc != SLURM_SUCCESS) {
		if(pg_conn->rollback) {
			pgsql_db_rollback(pg_conn->db_conn);
		}
		list_flush(pg_conn->update_list);
		list_destroy(ret_list);
		return NULL;
	}

	user_name = uid_to_string((uid_t) uid);
	rc = aspg_remove_common(pg_conn, DBD_REMOVE_QOS, now,
				user_name, qos_table, name_char, assoc_char);
	xfree(assoc_char);
	xfree(name_char);
	xfree(user_name);
	if (rc != SLURM_SUCCESS) {
		list_destroy(ret_list);
		ret_list = NULL;
	}
	return ret_list;
}

/*
 * as_p_get_qos - get qos
 *
 * IN pg_conn: database connection
 * IN uid: user performing the get operation
 * IN qos_cond: which qos'es to get
 * RET: list of qos'es got
 */
extern List
as_p_get_qos(pgsql_conn_t *pg_conn, uid_t uid,
	     acct_qos_cond_t *qos_cond)
{
	char *query = NULL, *cond = NULL;
	List qos_list = NULL;
	PGresult *result = NULL;

	/* if this changes you will need to edit the corresponding enum */
	char *gq_fields = "name,description,id,grp_cpu_mins,grp_cpus,grp_jobs,"
		"grp_nodes,grp_submit_jobs,grp_wall,max_cpu_mins_per_job,"
		"max_cpus_per_job,max_jobs_per_user,max_nodes_per_job,"
		"max_submit_jobs_per_user,max_wall_duration_per_job,preempt,"
		"priority,usage_factor";
	enum {
		GQ_NAME,
		GQ_DESC,
		GQ_ID,
		GQ_GCM,
		GQ_GC,
		GQ_GJ,
		GQ_GN,
		GQ_GSJ,
		GQ_GW,
		GQ_MCMPJ,
		GQ_MCPJ,
		GQ_MJPU,
		GQ_MNPJ,
		GQ_MSJPU,
		GQ_MWPJ,
		GQ_PREE,
		GQ_PRIO,
		GQ_UF,
		GQ_COUNT
	};

	if(check_db_connection(pg_conn) != SLURM_SUCCESS)
		return NULL;

	if(!qos_cond) {
		query = xstrdup_printf("SELECT %s FROM %s WHERE deleted=0;",
				       gq_fields, qos_table);
	} else {
		cond = _make_qos_cond(qos_cond);
		if(qos_cond->with_deleted)
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

	qos_list = list_create(destroy_acct_qos_rec);
	FOR_EACH_ROW {
		acct_qos_rec_t *qos = xmalloc(sizeof(acct_qos_rec_t));
		list_append(qos_list, qos);

		if(! ISEMPTY(GQ_DESC))
			qos->description = xstrdup(ROW(GQ_DESC));

		qos->id = atoi(ROW(GQ_ID));

		if(! ISEMPTY(GQ_NAME))
			qos->name =  xstrdup(ROW(GQ_NAME));

		if(! ISNULL(GQ_GCM))
			qos->grp_cpu_mins = atoll(ROW(GQ_GCM));
		else
			qos->grp_cpu_mins = INFINITE;
		if(! ISNULL(GQ_GC))
			qos->grp_cpus = atoi(ROW(GQ_GC));
		else
			qos->grp_cpus = INFINITE;
		if(! ISNULL(GQ_GJ))
			qos->grp_jobs = atoi(ROW(GQ_GJ));
		else
			qos->grp_jobs = INFINITE;
		if(! ISNULL(GQ_GN))
			qos->grp_nodes = atoi(ROW(GQ_GN));
		else
			qos->grp_nodes = INFINITE;
		if(! ISNULL(GQ_GSJ))
			qos->grp_submit_jobs = atoi(ROW(GQ_GSJ));
		else
			qos->grp_submit_jobs = INFINITE;
		if(! ISNULL(GQ_GW))
			qos->grp_wall = atoi(ROW(GQ_GW));
		else
			qos->grp_wall = INFINITE;

		if(! ISNULL(GQ_MCMPJ))
			qos->max_cpu_mins_pj = atoi(ROW(GQ_MCMPJ));
		else
			qos->max_cpu_mins_pj = INFINITE;
		if(! ISNULL(GQ_MCPJ))
			qos->max_cpus_pj = atoi(ROW(GQ_MCPJ));
		else
			qos->max_cpus_pj = INFINITE;
		if(! ISNULL(GQ_MJPU))
			qos->max_jobs_pu = atoi(ROW(GQ_MJPU));
		else
			qos->max_jobs_pu = INFINITE;
		if(! ISNULL(GQ_MNPJ))
			qos->max_nodes_pj = atoi(ROW(GQ_MNPJ));
		else
			qos->max_nodes_pj = INFINITE;
		if(! ISNULL(GQ_MSJPU))
			qos->max_submit_jobs_pu = atoi(ROW(GQ_MSJPU));
		else
			qos->max_submit_jobs_pu = INFINITE;
		if(! ISNULL(GQ_MWPJ))
			qos->max_wall_pj = atoi(ROW(GQ_MWPJ));
		else
			qos->max_wall_pj = INFINITE;

		if(! ISEMPTY(GQ_PREE)) {
			if(!qos->preempt_bitstr)
				qos->preempt_bitstr = bit_alloc(g_qos_count);
			bit_unfmt(qos->preempt_bitstr, ROW(GQ_PREE)+1);
		}
		if(! ISNULL(GQ_PRIO))
			qos->priority = atoi(ROW(GQ_PRIO));

		if(! ISNULL(GQ_UF))
			qos->usage_factor = atof(ROW(GQ_UF));
	} END_EACH_ROW;
	PQclear(result);
	return qos_list;
}
