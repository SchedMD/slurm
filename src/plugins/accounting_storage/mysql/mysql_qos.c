/*****************************************************************************\
 *  mysql_qos.c - functions dealing with qos.
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

#include "mysql_qos.h"

static int _preemption_loop(mysql_conn_t *mysql_conn, int begin_qosid,
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
		assoc_mgr_fill_in_qos(mysql_conn, &qos_rec,
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
			/* check this qos' preempt list and make sure
			   no loops exist there either */
			if((rc = _preemption_loop(mysql_conn, begin_qosid,
						  qos_rec.preempt_bitstr)))
				break;
		}
	}
	return rc;
}

static int _setup_qos_limits(acct_qos_rec_t *qos,
			     char **cols, char **vals,
			     char **extra, char **added_preempt)
{
	if(!qos)
		return SLURM_ERROR;

	if(qos->description) {
		xstrcat(*cols, ", description");
		xstrfmtcat(*vals, ", \"%s\"", qos->description);
		xstrfmtcat(*extra, ", description=\"%s\"",
			   qos->description);

	}
	if((int)qos->priority >= 0) {
		xstrcat(*cols, ", priority");
		xstrfmtcat(*vals, ", %d", qos->priority);
		xstrfmtcat(*extra, ", priority=%d", qos->priority);
	} else if ((int)qos->priority == INFINITE) {
		xstrcat(*cols, ", priority");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", priority=NULL");
	}

	if((int)qos->grp_cpu_mins >= 0) {
		xstrcat(*cols, ", grp_cpu_mins");
		xstrfmtcat(*vals, ", %llu", qos->grp_cpu_mins);
		xstrfmtcat(*extra, ", grp_cpu_mins=%llu",
			   qos->grp_cpu_mins);
	} else if((int)qos->grp_cpu_mins == INFINITE) {
		xstrcat(*cols, ", grp_cpu_mins");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", grp_cpu_mins=NULL");
	}

	if((int)qos->grp_cpus >= 0) {
		xstrcat(*cols, ", grp_cpus");
		xstrfmtcat(*vals, ", %u", qos->grp_cpus);
		xstrfmtcat(*extra, ", grp_cpus=%u", qos->grp_cpus);
	} else if((int)qos->grp_cpus == INFINITE) {
		xstrcat(*cols, ", grp_cpus");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", grp_cpus=NULL");
	}

	if((int)qos->grp_jobs >= 0) {
		xstrcat(*cols, ", grp_jobs");
		xstrfmtcat(*vals, ", %u", qos->grp_jobs);
		xstrfmtcat(*extra, ", grp_jobs=%u", qos->grp_jobs);
	} else if((int)qos->grp_jobs == INFINITE) {
		xstrcat(*cols, ", grp_jobs");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", grp_jobs=NULL");
	}

	if((int)qos->grp_nodes >= 0) {
		xstrcat(*cols, ", grp_nodes");
		xstrfmtcat(*vals, ", %u", qos->grp_nodes);
		xstrfmtcat(*extra, ", grp_nodes=%u", qos->grp_nodes);
	} else if((int)qos->grp_nodes == INFINITE) {
		xstrcat(*cols, ", grp_nodes");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", grp_nodes=NULL");
	}

	if((int)qos->grp_submit_jobs >= 0) {
		xstrcat(*cols, ", grp_submit_jobs");
		xstrfmtcat(*vals, ", %u",
			   qos->grp_submit_jobs);
		xstrfmtcat(*extra, ", grp_submit_jobs=%u",
			   qos->grp_submit_jobs);
	} else if((int)qos->grp_submit_jobs == INFINITE) {
		xstrcat(*cols, ", grp_submit_jobs");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", grp_submit_jobs=NULL");
	}

	if((int)qos->grp_wall >= 0) {
		xstrcat(*cols, ", grp_wall");
		xstrfmtcat(*vals, ", %u", qos->grp_wall);
		xstrfmtcat(*extra, ", grp_wall=%u",
			   qos->grp_wall);
	} else if((int)qos->grp_wall == INFINITE) {
		xstrcat(*cols, ", grp_wall");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", grp_wall=NULL");
	}

	if((int)qos->max_cpu_mins_pj >= 0) {
		xstrcat(*cols, ", max_cpu_mins_per_job");
		xstrfmtcat(*vals, ", %llu", qos->max_cpu_mins_pj);
		xstrfmtcat(*extra, ", max_cpu_mins_per_job=%u",
			   qos->max_cpu_mins_pj);
	} else if((int)qos->max_cpu_mins_pj == INFINITE) {
		xstrcat(*cols, ", max_cpu_mins_per_job");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", max_cpu_mins_per_job=NULL");
	}

	if((int)qos->max_cpus_pj >= 0) {
		xstrcat(*cols, ", max_cpus_per_job");
		xstrfmtcat(*vals, ", %u", qos->max_cpus_pj);
		xstrfmtcat(*extra, ", max_cpus_per_job=%u",
			   qos->max_cpus_pj);
	} else if((int)qos->max_cpus_pj == INFINITE) {
		xstrcat(*cols, ", max_cpus_per_job");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", max_cpus_per_job=NULL");
	}

	if((int)qos->max_jobs_pu >= 0) {
		xstrcat(*cols, ", max_jobs_per_user");
		xstrfmtcat(*vals, ", %u", qos->max_jobs_pu);
		xstrfmtcat(*extra, ", max_jobs_per_user=%u",
			   qos->max_jobs_pu);
	} else if((int)qos->max_jobs_pu == INFINITE) {
		xstrcat(*cols, ", max_jobs_per_user");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", max_jobs_per_user=NULL");
	}

	if((int)qos->max_nodes_pj >= 0) {
		xstrcat(*cols, ", max_nodes_per_job");
		xstrfmtcat(*vals, ", %u", qos->max_nodes_pj);
		xstrfmtcat(*extra, ", max_nodes_per_job=%u",
			   qos->max_nodes_pj);
	} else if((int)qos->max_nodes_pj == INFINITE) {
		xstrcat(*cols, ", max_nodes_per_job");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", max_nodes_per_job=NULL");
	}

	if((int)qos->max_submit_jobs_pu >= 0) {
		xstrcat(*cols, ", max_submit_jobs_per_user");
		xstrfmtcat(*vals, ", %u", qos->max_submit_jobs_pu);
		xstrfmtcat(*extra, ", max_submit_jobs_per_user=%u",
			   qos->max_submit_jobs_pu);
	} else if((int)qos->max_submit_jobs_pu == INFINITE) {
		xstrcat(*cols, ", max_submit_jobs_per_user");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", max_submit_jobs_per_user=NULL");
	}

	if((int)qos->max_wall_pj >= 0) {
		xstrcat(*cols, ", max_wall_duration_per_job");
		xstrfmtcat(*vals, ", %u", qos->max_wall_pj);
		xstrfmtcat(*extra, ", max_wall_duration_per_job=%u",
			   qos->max_wall_pj);
	} else if((int)qos->max_wall_pj == INFINITE) {
		xstrcat(*cols, ", max_wall_duration_per_job");
		xstrcat(*vals, ", NULL");
		xstrcat(*extra, ", max_wall_duration_per_job=NULL");
	}

	if(qos->preempt_list && list_count(qos->preempt_list)) {
		char *preempt_val = NULL;
		char *tmp_char = NULL, *begin_preempt = NULL;
		ListIterator preempt_itr =
			list_iterator_create(qos->preempt_list);

		xstrcat(*cols, ", preempt");

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
					   "concat("
					   "replace(%s, ',%s', ''), ',%s')",
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

		xstrfmtcat(*vals, ", \"%s\"", preempt_val);
		xstrfmtcat(*extra, ", preempt=\"%s\"", preempt_val);
		xfree(preempt_val);
	}

	if((int)qos->usage_factor >= 0) {
		xstrcat(*cols, ", usage_factor");
		xstrfmtcat(*vals, ", %f", qos->usage_factor);
		xstrfmtcat(*extra, ", usage_factor=%f", qos->usage_factor);
	} else if((int)qos->usage_factor == INFINITE) {
		xstrcat(*cols, ", usage_factor");
		xstrcat(*vals, ", 1");
		xstrcat(*extra, ", usage_factor=1");
	}

	return SLURM_SUCCESS;

}

extern int mysql_add_qos(mysql_conn_t *mysql_conn, uint32_t uid, List qos_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	acct_qos_rec_t *object = NULL;
	char *cols = NULL, *extra = NULL, *vals = NULL, *query = NULL,
		*tmp_extra = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	int affect_rows = 0;
	int added = 0;
	char *added_preempt = NULL;

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	user_name = uid_to_string((uid_t) uid);
	itr = list_iterator_create(qos_list);
	while((object = list_next(itr))) {
		if(!object->name || !object->name[0]) {
			error("We need a qos name to add.");
			rc = SLURM_ERROR;
			continue;
		}
		xstrcat(cols, "creation_time, mod_time, name");
		xstrfmtcat(vals, "%d, %d, \"%s\"",
			   now, now, object->name);
		xstrfmtcat(extra, ", mod_time=%d", now);

		_setup_qos_limits(object, &cols, &vals, &extra, &added_preempt);
		if(added_preempt) {
			object->preempt_bitstr = bit_alloc(g_qos_count);
			bit_unfmt(object->preempt_bitstr, added_preempt+1);
			xfree(added_preempt);
		}

		xstrfmtcat(query,
			   "insert into %s (%s) values (%s) "
			   "on duplicate key update deleted=0, "
			   "id=LAST_INSERT_ID(id)%s;",
			   qos_table, cols, vals, extra);


		debug3("%d(%d) query\n%s",
		       mysql_conn->conn, __LINE__, query);
		object->id = mysql_insert_ret_id(mysql_conn->db_conn, query);
		xfree(query);
		if(!object->id) {
			error("Couldn't add qos %s", object->name);
			added=0;
			xfree(cols);
			xfree(extra);
			xfree(vals);
			break;
		}

		affect_rows = last_affected_rows(mysql_conn->db_conn);

		if(!affect_rows) {
			debug2("nothing changed %d", affect_rows);
			xfree(cols);
			xfree(extra);
			xfree(vals);
			continue;
		}

		/* we always have a ', ' as the first 2 chars */
		tmp_extra = fix_double_quotes(extra+2);

		xstrfmtcat(query,
			   "insert into %s "
			   "(timestamp, action, name, actor, info) "
			   "values (%d, %u, \"%s\", \"%s\", \"%s\");",
			   txn_table,
			   now, DBD_ADD_QOS, object->name, user_name,
			   tmp_extra);

		xfree(tmp_extra);
		xfree(cols);
		xfree(extra);
		xfree(vals);
		debug4("query\n%s",query);
		rc = mysql_db_query(mysql_conn->db_conn, query);
		xfree(query);
		if(rc != SLURM_SUCCESS) {
			error("Couldn't add txn");
		} else {
			if(addto_update_list(mysql_conn->update_list,
					      ACCT_ADD_QOS,
					      object) == SLURM_SUCCESS)
				list_remove(itr);
			added++;
		}

	}
	list_iterator_destroy(itr);
	xfree(user_name);

	if(!added) {
		if(mysql_conn->rollback) {
			mysql_db_rollback(mysql_conn->db_conn);
		}
		list_flush(mysql_conn->update_list);
	}

	return rc;
}

extern List mysql_modify_qos(mysql_conn_t *mysql_conn, uint32_t uid,
			     acct_qos_cond_t *qos_cond,
			     acct_qos_rec_t *qos)
{
	ListIterator itr = NULL;
	List ret_list = NULL;
	int rc = SLURM_SUCCESS;
	char *object = NULL;
	char *vals = NULL, *extra = NULL, *query = NULL, *name_char = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	int set = 0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *tmp_char1=NULL, *tmp_char2=NULL;
	bitstr_t *preempt_bitstr = NULL;
	char *added_preempt = NULL;

	if(!qos_cond || !qos) {
		error("we need something to change");
		return NULL;
	}

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	xstrcat(extra, "where deleted=0");

	if(qos_cond->description_list
	   && list_count(qos_cond->description_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(qos_cond->description_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "description=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(qos_cond->id_list
	   && list_count(qos_cond->id_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(qos_cond->id_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "id=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(qos_cond->name_list
	   && list_count(qos_cond->name_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(qos_cond->name_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	_setup_qos_limits(qos, &tmp_char1, &tmp_char2, &vals, &added_preempt);
	if(added_preempt) {
		preempt_bitstr = bit_alloc(g_qos_count);
		bit_unfmt(preempt_bitstr, added_preempt+1);
		xfree(added_preempt);
	}
	xfree(tmp_char1);
	xfree(tmp_char2);

	if(!extra || !vals) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		FREE_NULL_BITMAP(preempt_bitstr);
		error("Nothing to change");
		return NULL;
	}
	query = xstrdup_printf("select name, preempt, id from %s %s;",
			       qos_table, extra);
	xfree(extra);
	if(!(result = mysql_db_query_ret(mysql_conn->db_conn, query, 0))) {
		xfree(query);
		FREE_NULL_BITMAP(preempt_bitstr);
		return NULL;
	}

	rc = 0;
	ret_list = list_create(slurm_destroy_char);
	while((row = mysql_fetch_row(result))) {
		acct_qos_rec_t *qos_rec = NULL;
		if(preempt_bitstr) {
			if(_preemption_loop(mysql_conn,
					    atoi(row[2]), preempt_bitstr))
				break;
		}
		object = xstrdup(row[0]);
		list_append(ret_list, object);
		if(!rc) {
			xstrfmtcat(name_char, "(name='%s'", object);
			rc = 1;
		} else  {
			xstrfmtcat(name_char, " || name='%s'", object);
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
			char *new_preempt = NULL;

			qos->preempt_bitstr = bit_alloc(g_qos_count);
			if(row[1] && row[1][0])
				bit_unfmt(qos->preempt_bitstr, row[1]+1);

			while((new_preempt = list_next(new_preempt_itr))) {
				bool cleared = 0;
				if(new_preempt[0] == '-') {
					bit_clear(qos->preempt_bitstr,
						  atoi(new_preempt+1));
				} else if(new_preempt[0] == '+') {
					bit_set(qos->preempt_bitstr,
						atoi(new_preempt+1));
				} else {
					if(!cleared) {
						cleared = 1;
						bit_nclear(qos->preempt_bitstr,
							   0,
							   bit_size(qos->preempt_bitstr)-1);
					}

					bit_set(qos->preempt_bitstr,
						atoi(new_preempt));
				}
			}
			list_iterator_destroy(new_preempt_itr);
		}

		addto_update_list(mysql_conn->update_list, ACCT_MODIFY_QOS,
				   qos_rec);
	}
	mysql_free_result(result);

	FREE_NULL_BITMAP(preempt_bitstr);

	if(row) {
		xfree(vals);
		xfree(name_char);
		xfree(query);
		list_destroy(ret_list);
		ret_list = NULL;
		errno = ESLURM_QOS_PREEMPTION_LOOP;
		return ret_list;
	}

	if(!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		debug3("didn't effect anything\n%s", query);
		xfree(vals);
		xfree(query);
		return ret_list;
	}
	xfree(query);
	xstrcat(name_char, ")");

	user_name = uid_to_string((uid_t) uid);
	rc = modify_common(mysql_conn, DBD_MODIFY_QOS, now,
			    user_name, qos_table, name_char, vals);
	xfree(user_name);
	xfree(name_char);
	xfree(vals);
	if (rc == SLURM_ERROR) {
		error("Couldn't modify qos");
		list_destroy(ret_list);
		ret_list = NULL;
	}

	return ret_list;
}

extern List mysql_remove_qos(mysql_conn_t *mysql_conn, uint32_t uid,
			     acct_qos_cond_t *qos_cond)
{
	ListIterator itr = NULL;
	List ret_list = NULL;
	int rc = SLURM_SUCCESS;
	char *object = NULL;
	char *extra = NULL, *query = NULL,
		*name_char = NULL, *assoc_char = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	int set = 0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;

	if(!qos_cond) {
		error("we need something to change");
		return NULL;
	}

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	xstrcat(extra, "where deleted=0");
	if(qos_cond->description_list
	   && list_count(qos_cond->description_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(qos_cond->description_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "description=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(qos_cond->id_list
	   && list_count(qos_cond->id_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(qos_cond->id_list);
		while((object = list_next(itr))) {
			if(!object[0])
				continue;
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "id=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(qos_cond->name_list
	   && list_count(qos_cond->name_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(qos_cond->name_list);
		while((object = list_next(itr))) {
			if(!object[0])
				continue;
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(!extra) {
		error("Nothing to remove");
		return NULL;
	}

	query = xstrdup_printf("select id, name from %s %s;", qos_table, extra);
	xfree(extra);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return NULL;
	}

	name_char = NULL;
	ret_list = list_create(slurm_destroy_char);
	while((row = mysql_fetch_row(result))) {
		acct_qos_rec_t *qos_rec = NULL;

		list_append(ret_list, xstrdup(row[1]));
		if(!name_char)
			xstrfmtcat(name_char, "id=\"%s\"", row[0]);
		else
			xstrfmtcat(name_char, " || id=\"%s\"", row[0]);
		if(!assoc_char)
			xstrfmtcat(assoc_char, "qos=\"%s\"", row[0]);
		else
			xstrfmtcat(assoc_char, " || qos=\"%s\"", row[0]);
		xstrfmtcat(extra,
			   ", qos=replace(qos, ',%s', '')"
			   ", delta_qos=replace(delta_qos, ',+%s', '')"
			   ", delta_qos=replace(delta_qos, ',-%s', '')",
			   row[0], row[0], row[0]);

		qos_rec = xmalloc(sizeof(acct_qos_rec_t));
		/* we only need id when removing no real need to init */
		qos_rec->id = atoi(row[0]);
		addto_update_list(mysql_conn->update_list, ACCT_REMOVE_QOS,
				   qos_rec);
	}
	mysql_free_result(result);

	if(!list_count(ret_list)) {
		errno = SLURM_NO_CHANGE_IN_DATA;
		debug3("didn't effect anything\n%s", query);
		xfree(query);
		return ret_list;
	}
	xfree(query);

	/* remove this qos from all the users/accts that have it */
	query = xstrdup_printf("update %s set mod_time=%d %s where deleted=0;",
			       assoc_table, now, extra);
	xfree(extra);
	debug3("%d(%d) query\n%s",
	       mysql_conn->conn, __LINE__, query);
	rc = mysql_db_query(mysql_conn->db_conn, query);
	xfree(query);
	if(rc != SLURM_SUCCESS) {
		if(mysql_conn->rollback) {
			mysql_db_rollback(mysql_conn->db_conn);
		}
		list_flush(mysql_conn->update_list);
		list_destroy(ret_list);
		return NULL;
	}

	user_name = uid_to_string((uid_t) uid);
	rc = remove_common(mysql_conn, DBD_REMOVE_QOS, now,
			    user_name, qos_table, name_char, assoc_char);
	xfree(assoc_char);
	xfree(name_char);
	xfree(user_name);
	if (rc == SLURM_ERROR) {
		list_destroy(ret_list);
		return NULL;
	}

	return ret_list;
}

extern List mysql_get_qos(mysql_conn_t *mysql_conn, uid_t uid,
			  acct_qos_cond_t *qos_cond)
{
	char *query = NULL;
	char *extra = NULL;
	char *tmp = NULL;
	List qos_list = NULL;
	ListIterator itr = NULL;
	char *object = NULL;
	int set = 0;
	int i=0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;

	/* if this changes you will need to edit the corresponding enum */
	char *qos_req_inx[] = {
		"name",
		"description",
		"id",
		"grp_cpu_mins",
		"grp_cpus",
		"grp_jobs",
		"grp_nodes",
		"grp_submit_jobs",
		"grp_wall",
		"max_cpu_mins_per_job",
		"max_cpus_per_job",
		"max_jobs_per_user",
		"max_nodes_per_job",
		"max_submit_jobs_per_user",
		"max_wall_duration_per_job",
		"preempt",
		"priority",
		"usage_factor",
	};
	enum {
		QOS_REQ_NAME,
		QOS_REQ_DESC,
		QOS_REQ_ID,
		QOS_REQ_GCH,
		QOS_REQ_GC,
		QOS_REQ_GJ,
		QOS_REQ_GN,
		QOS_REQ_GSJ,
		QOS_REQ_GW,
		QOS_REQ_MCMPJ,
		QOS_REQ_MCPJ,
		QOS_REQ_MJPU,
		QOS_REQ_MNPJ,
		QOS_REQ_MSJPU,
		QOS_REQ_MWPJ,
		QOS_REQ_PREE,
		QOS_REQ_PRIO,
		QOS_REQ_UF,
		QOS_REQ_COUNT
	};

	if(check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	if(!qos_cond) {
		xstrcat(extra, "where deleted=0");
		goto empty;
	}

	if(qos_cond->with_deleted)
		xstrcat(extra, "where (deleted=0 || deleted=1)");
	else
		xstrcat(extra, "where deleted=0");


	if(qos_cond->description_list
	   && list_count(qos_cond->description_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(qos_cond->description_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "description=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(qos_cond->id_list
	   && list_count(qos_cond->id_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(qos_cond->id_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "id=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

	if(qos_cond->name_list
	   && list_count(qos_cond->name_list)) {
		set = 0;
		xstrcat(extra, " && (");
		itr = list_iterator_create(qos_cond->name_list);
		while((object = list_next(itr))) {
			if(set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra, "name=\"%s\"", object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ")");
	}

empty:

	xfree(tmp);
	xstrfmtcat(tmp, "%s", qos_req_inx[i]);
	for(i=1; i<QOS_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", qos_req_inx[i]);
	}

	query = xstrdup_printf("select %s from %s %s", tmp, qos_table, extra);
	xfree(tmp);
	xfree(extra);

	debug3("%d(%d) query\n%s", mysql_conn->conn, __LINE__, query);
	if(!(result = mysql_db_query_ret(
		     mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return NULL;
	}
	xfree(query);

	qos_list = list_create(destroy_acct_qos_rec);

	while((row = mysql_fetch_row(result))) {
		acct_qos_rec_t *qos = xmalloc(sizeof(acct_qos_rec_t));
		list_append(qos_list, qos);

		if(row[QOS_REQ_DESC] && row[QOS_REQ_DESC][0])
			qos->description = xstrdup(row[QOS_REQ_DESC]);

		qos->id = atoi(row[QOS_REQ_ID]);

		if(row[QOS_REQ_NAME] && row[QOS_REQ_NAME][0])
			qos->name =  xstrdup(row[QOS_REQ_NAME]);

		if(row[QOS_REQ_GCH])
			qos->grp_cpu_mins = atoll(row[QOS_REQ_GCH]);
		else
			qos->grp_cpu_mins = INFINITE;
		if(row[QOS_REQ_GC])
			qos->grp_cpus = atoi(row[QOS_REQ_GC]);
		else
			qos->grp_cpus = INFINITE;
		if(row[QOS_REQ_GJ])
			qos->grp_jobs = atoi(row[QOS_REQ_GJ]);
		else
			qos->grp_jobs = INFINITE;
		if(row[QOS_REQ_GN])
			qos->grp_nodes = atoi(row[QOS_REQ_GN]);
		else
			qos->grp_nodes = INFINITE;
		if(row[QOS_REQ_GSJ])
			qos->grp_submit_jobs = atoi(row[QOS_REQ_GSJ]);
		else
			qos->grp_submit_jobs = INFINITE;
		if(row[QOS_REQ_GW])
			qos->grp_wall = atoi(row[QOS_REQ_GW]);
		else
			qos->grp_wall = INFINITE;

		if(row[QOS_REQ_MCMPJ])
			qos->max_cpu_mins_pj = atoi(row[QOS_REQ_MCMPJ]);
		else
			qos->max_cpu_mins_pj = INFINITE;
		if(row[QOS_REQ_MCPJ])
			qos->max_cpus_pj = atoi(row[QOS_REQ_MCPJ]);
		else
			qos->max_cpus_pj = INFINITE;
		if(row[QOS_REQ_MJPU])
			qos->max_jobs_pu = atoi(row[QOS_REQ_MJPU]);
		else
			qos->max_jobs_pu = INFINITE;
		if(row[QOS_REQ_MNPJ])
			qos->max_nodes_pj = atoi(row[QOS_REQ_MNPJ]);
		else
			qos->max_nodes_pj = INFINITE;
		if(row[QOS_REQ_MSJPU])
			qos->max_submit_jobs_pu = atoi(row[QOS_REQ_MSJPU]);
		else
			qos->max_submit_jobs_pu = INFINITE;
		if(row[QOS_REQ_MWPJ])
			qos->max_wall_pj = atoi(row[QOS_REQ_MWPJ]);
		else
			qos->max_wall_pj = INFINITE;

		if(row[QOS_REQ_PREE] && row[QOS_REQ_PREE][0]) {
			if(!qos->preempt_bitstr)
				qos->preempt_bitstr = bit_alloc(g_qos_count);
			bit_unfmt(qos->preempt_bitstr, row[QOS_REQ_PREE]+1);
		}
		if(row[QOS_REQ_PRIO])
			qos->priority = atoi(row[QOS_REQ_PRIO]);

		if(row[QOS_REQ_UF])
			qos->usage_factor = atof(row[QOS_REQ_UF]);
	}
	mysql_free_result(result);

	return qos_list;
}
