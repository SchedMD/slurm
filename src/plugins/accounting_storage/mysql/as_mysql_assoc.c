/*****************************************************************************\
 *  as_mysql_assoc.c - functions dealing with associations.
 *****************************************************************************
 *
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "as_mysql_assoc.h"
#include "as_mysql_usage.h"

static char *tmp_cluster_name = "slurmredolftrgttemp";


/* if this changes you will need to edit the corresponding enum */
char *assoc_req_inx[] = {
	"id_assoc",
	"lft",
	"rgt",
	"user",
	"acct",
	"`partition`",
	"shares",
	"grp_tres_mins",
	"grp_tres_run_mins",
	"grp_tres",
	"grp_jobs",
	"grp_jobs_accrue",
	"grp_submit_jobs",
	"grp_wall",
	"max_tres_mins_pj",
	"max_tres_run_mins",
	"max_tres_pj",
	"max_tres_pn",
	"max_jobs",
	"max_jobs_accrue",
	"min_prio_thresh",
	"max_submit_jobs",
	"max_wall_pj",
	"parent_acct",
	"def_qos_id",
	"qos",
	"delta_qos",
	"is_def",
};
enum {
	ASSOC_REQ_ID,
	ASSOC_REQ_LFT,
	ASSOC_REQ_RGT,
	ASSOC_REQ_USER,
	ASSOC_REQ_ACCT,
	ASSOC_REQ_PART,
	ASSOC_REQ_FS,
	ASSOC_REQ_GTM,
	ASSOC_REQ_GTRM,
	ASSOC_REQ_GT,
	ASSOC_REQ_GJ,
	ASSOC_REQ_GJA,
	ASSOC_REQ_GSJ,
	ASSOC_REQ_GW,
	ASSOC_REQ_MTMPJ,
	ASSOC_REQ_MTRM,
	ASSOC_REQ_MTPJ,
	ASSOC_REQ_MTPN,
	ASSOC_REQ_MJ,
	ASSOC_REQ_MJA,
	ASSOC_REQ_MPT,
	ASSOC_REQ_MSJ,
	ASSOC_REQ_MWPJ,
	ASSOC_REQ_PARENT,
	ASSOC_REQ_DEF_QOS,
	ASSOC_REQ_QOS,
	ASSOC_REQ_DELTA_QOS,
	ASSOC_REQ_DEFAULT,
	ASSOC_REQ_COUNT
};

static char *get_parent_limits_select =
	"select @par_id, @mj, @mja, @mpt, @msj, "
	"@mwpj, @mtpj, @mtpn, @mtmpj, @mtrm, "
	"@def_qos_id, @qos, @delta_qos;";

enum {
	ASSOC2_REQ_PARENT_ID,
	ASSOC2_REQ_MJ,
	ASSOC2_REQ_MJA,
	ASSOC2_REQ_MPT,
	ASSOC2_REQ_MSJ,
	ASSOC2_REQ_MWPJ,
	ASSOC2_REQ_MTPJ,
	ASSOC2_REQ_MTPN,
	ASSOC2_REQ_MTMPJ,
	ASSOC2_REQ_MTRM,
	ASSOC2_REQ_DEF_QOS,
	ASSOC2_REQ_QOS,
	ASSOC2_REQ_DELTA_QOS,
};

static char *aassoc_req_inx[] = {
	"id_assoc",
	"parent_acct",
	"lft",
	"rgt",
	"deleted"
};

enum {
	AASSOC_ID,
	AASSOC_PACCT,
	AASSOC_LFT,
	AASSOC_RGT,
	AASSOC_DELETED,
	AASSOC_COUNT
};

static char *massoc_req_inx[] = {
	"id_assoc",
	"acct",
	"parent_acct",
	"user",
	"`partition`",
	"lft",
	"rgt",
	"qos",
	"grp_tres_mins",
	"grp_tres_run_mins",
	"grp_tres",
	"max_tres_mins_pj",
	"max_tres_run_mins",
	"max_tres_pj",
	"max_tres_pn",
};

enum {
	MASSOC_ID,
	MASSOC_ACCT,
	MASSOC_PACCT,
	MASSOC_USER,
	MASSOC_PART,
	MASSOC_LFT,
	MASSOC_RGT,
	MASSOC_QOS,
	MASSOC_GTM,
	MASSOC_GTRM,
	MASSOC_GT,
	MASSOC_MTMPJ,
	MASSOC_MTRM,
	MASSOC_MTPJ,
	MASSOC_MTPN,
	MASSOC_COUNT
};

/* if this changes you will need to edit the corresponding
 * enum below also t1 is step_table */
static char *rassoc_req_inx[] = {
	"id_assoc",
	"lft",
	"acct",
	"parent_acct",
	"user",
	"`partition`"
};

enum {
	RASSOC_ID,
	RASSOC_LFT,
	RASSOC_ACCT,
	RASSOC_PACCT,
	RASSOC_USER,
	RASSOC_PART,
	RASSOC_COUNT
};

static int _assoc_sort_cluster(void *r1, void *r2)
{
	slurmdb_assoc_rec_t *rec_a = *(slurmdb_assoc_rec_t **)r1;
	slurmdb_assoc_rec_t *rec_b = *(slurmdb_assoc_rec_t **)r2;
	int diff;

	diff = xstrcmp(rec_a->cluster, rec_b->cluster);
	if (diff < 0)
		return -1;
	else if (diff > 0)
		return 1;

	return 0;
}

/* Caller responsible for freeing query being sent in as it could be
 * changed while running the function.
 */
static int _reset_default_assoc(mysql_conn_t *mysql_conn,
				slurmdb_assoc_rec_t *assoc,
				char **query,
				bool add_to_update)
{
	time_t now = time(NULL);
	int rc = SLURM_SUCCESS;

	xassert(query);

	if ((assoc->is_def != 1) || !assoc->cluster
	    || !assoc->acct || !assoc->user)
		return SLURM_ERROR;

	xstrfmtcat(*query, "update \"%s_%s\" set is_def=0, mod_time=%ld "
		   "where (user='%s' && acct!='%s' && is_def=1);",
		   assoc->cluster, assoc_table, (long)now,
		   assoc->user, assoc->acct);
	if (add_to_update) {
		char *sel_query = NULL;
		MYSQL_RES *result = NULL;
		MYSQL_ROW row;
		/* If moved parent all the associations will be sent
		   so no need to do this extra step.  Else, this has
		   to be done one at a time so we can send
		   the updated assocs back to the slurmctlds
		*/
		xstrfmtcat(sel_query, "select id_assoc from \"%s_%s\" "
			   "where (user='%s' && acct!='%s' && is_def=1);",
			   assoc->cluster, assoc_table,
			   assoc->user, assoc->acct);
		if (debug_flags & DEBUG_FLAG_DB_ASSOC)
			DB_DEBUG(mysql_conn->conn, "query\n%s", sel_query);
		if (!(result = mysql_db_query_ret(
			      mysql_conn, sel_query, 1))) {
			xfree(sel_query);
			rc = SLURM_ERROR;
			goto end_it;
		}
		xfree(sel_query);

		while ((row = mysql_fetch_row(result))) {
			slurmdb_assoc_rec_t *mod_assoc = xmalloc(
				sizeof(slurmdb_assoc_rec_t));
			slurmdb_init_assoc_rec(mod_assoc, 0);

			mod_assoc->cluster = xstrdup(assoc->cluster);
			mod_assoc->id = slurm_atoul(row[0]);
			mod_assoc->is_def = 0;
			if (addto_update_list(mysql_conn->update_list,
					      SLURMDB_MODIFY_ASSOC,
					      mod_assoc)
			    != SLURM_SUCCESS) {
				slurmdb_destroy_assoc_rec(mod_assoc);
				error("couldn't add to the update list");
				rc = SLURM_ERROR;
				break;
			}
		}
		mysql_free_result(result);
	}
end_it:
	return rc;
}

/* assoc_mgr_lock_t should be clear before coming in here. */
static int _check_coord_qos(mysql_conn_t *mysql_conn, char *cluster_name,
			    char *account, char *coord_name, List qos_list)
{
	char *query;
	bitstr_t *request_qos, *valid_qos;
	MYSQL_RES *result;
	MYSQL_ROW row;
	int rc = SLURM_SUCCESS;
	assoc_mgr_lock_t locks = { NO_LOCK, NO_LOCK, READ_LOCK, NO_LOCK,
				   NO_LOCK, NO_LOCK, NO_LOCK };

	if (!qos_list || !list_count(qos_list))
		return SLURM_SUCCESS;

	/* If there is a variable cleared here we need to make
	   sure we get the parent's information, if any. */
	query = xstrdup_printf(
		"call get_coord_qos('%s', '%s', '%s', '%s');",
		assoc_table, account,
		cluster_name, coord_name);
	debug4("%d(%s:%d) query\n%s",
	       mysql_conn->conn, THIS_FILE, __LINE__, query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 1))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	if (!(row = mysql_fetch_row(result)) || !row[0]) {
		mysql_free_result(result);
		return SLURM_ERROR;
	}

	/* First set the values of the valid ones this coordinator has
	   access to.
	*/

	assoc_mgr_lock(&locks);
	valid_qos = bit_alloc(g_qos_count);
	request_qos = bit_alloc(g_qos_count);
	assoc_mgr_unlock(&locks);

	set_qos_bitstr_from_string(valid_qos, row[0]);

	mysql_free_result(result);

	/* Now set the ones they are requesting */
	set_qos_bitstr_from_list(request_qos, qos_list);

	/* If they are authorized their list should be in the super set */
	if (!bit_super_set(request_qos, valid_qos))
		rc = SLURM_ERROR;

	FREE_NULL_BITMAP(valid_qos);
	FREE_NULL_BITMAP(request_qos);

	return rc;
}

/* This needs to happen to make since 2.1 code doesn't have enough
 * smarts to figure out it isn't adding a default account if just
 * adding an association to the mix.
 */
static int _make_sure_users_have_default(
	mysql_conn_t *mysql_conn, List user_list)
{
	char *query = NULL, *cluster = NULL, *user = NULL;
	ListIterator itr = NULL, clus_itr = NULL;
	int rc = SLURM_SUCCESS;

	if (!user_list)
		return SLURM_SUCCESS;

	slurm_mutex_lock(&as_mysql_cluster_list_lock);

	clus_itr = list_iterator_create(as_mysql_cluster_list);
	itr = list_iterator_create(user_list);

	while ((user = list_next(itr))) {
		while ((cluster = list_next(clus_itr))) {
			MYSQL_RES *result = NULL;
			MYSQL_ROW row;
			char *acct = NULL;
			query = xstrdup_printf(
				"select distinct is_def, acct from "
				"\"%s_%s\" where user='%s' FOR UPDATE;",
				cluster, assoc_table, user);
			debug4("%d(%s:%d) query\n%s",
			       mysql_conn->conn, THIS_FILE, __LINE__, query);
			if (!(result = mysql_db_query_ret(
				      mysql_conn, query, 0))) {
				xfree(query);
				error("couldn't query the database");
				rc = SLURM_ERROR;
				break;
			}
			xfree(query);
			/* Check to see if the user is even added to
			   the cluster.
			*/
			if (!mysql_num_rows(result)) {
				mysql_free_result(result);
				continue;
			}
			while ((row = mysql_fetch_row(result))) {
				if (row[0][0] == '1')
					break;
				if (!acct)
					acct = xstrdup(row[1]);
			}
			mysql_free_result(result);

			/* we found one so just continue */
			if (row || !acct) {
				xfree(acct);
				continue;
			}
			query = xstrdup_printf(
				"update \"%s_%s\" set is_def=1 where "
				"user='%s' and acct='%s';",
				cluster, assoc_table, user, acct);
			xfree(acct);
			if (debug_flags & DEBUG_FLAG_DB_ASSOC)
				DB_DEBUG(mysql_conn->conn, "query\n%s", query);
			rc = mysql_db_query(mysql_conn, query);
			xfree(query);
			if (rc != SLURM_SUCCESS) {
				error("problem with update query");
				rc = SLURM_ERROR;
				break;
			}
		}
		if (rc != SLURM_SUCCESS)
			break;
		list_iterator_reset(clus_itr);
	}
	list_iterator_destroy(itr);
	list_iterator_destroy(clus_itr);
	slurm_mutex_unlock(&as_mysql_cluster_list_lock);

	return rc;
}

/* This should take care of all the lft and rgts when you move an
 * account.  This handles deleted associations also.
 */
static int _move_account(mysql_conn_t *mysql_conn, uint32_t *lft, uint32_t *rgt,
			 char *cluster, char *id, char *parent, time_t now)
{
	int rc = SLURM_SUCCESS;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	uint32_t par_left = 0;
	uint32_t diff = 0;
	uint32_t width = 0;
	char *query = xstrdup_printf(
		"SELECT lft from \"%s_%s\" where acct='%s' && user='';",
		cluster, assoc_table, parent);
	if (debug_flags & DEBUG_FLAG_DB_ASSOC)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(
		      mysql_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);
	if (!(row = mysql_fetch_row(result))) {
		debug4("Can't move a none existent association");
		mysql_free_result(result);
		return ESLURM_INVALID_PARENT_ACCOUNT;
	}
	par_left = slurm_atoul(row[0]);
	mysql_free_result(result);

	diff = ((par_left + 1) - *lft);

	if (diff == 0) {
		if (debug_flags & DEBUG_FLAG_DB_ASSOC)
			DB_DEBUG(mysql_conn->conn,
				 "Trying to move association to the same "
				 "position?  Nothing to do.");
		return ESLURM_SAME_PARENT_ACCOUNT;
	}

	width = (*rgt - *lft + 1);

	/* every thing below needs to be a %d not a %u because we are
	   looking for -1 */
	xstrfmtcat(query,
		   "update \"%s_%s\" set mod_time=%ld, deleted = deleted + 2, "
		   "lft = lft + %d, rgt = rgt + %d "
		   "WHERE lft BETWEEN %d AND %d;",
		   cluster, assoc_table, now, diff, diff, *lft, *rgt);

	xstrfmtcat(query,
		   "UPDATE \"%s_%s\" SET mod_time=%ld, rgt = rgt + %d WHERE "
		   "rgt > %d && deleted < 2;"
		   "UPDATE \"%s_%s\" SET mod_time=%ld, lft = lft + %d WHERE "
		   "lft > %d && deleted < 2;",
		   cluster, assoc_table, now, width,
		   par_left,
		   cluster, assoc_table, now, width,
		   par_left);

	xstrfmtcat(query,
		   "UPDATE \"%s_%s\" SET mod_time=%ld, rgt = rgt - %d WHERE "
		   "(%d < 0 && rgt > %d && deleted < 2) "
		   "|| (%d > 0 && rgt > %d);"
		   "UPDATE \"%s_%s\" SET mod_time=%ld, lft = lft - %d WHERE "
		   "(%d < 0 && lft > %d && deleted < 2) "
		   "|| (%d > 0 && lft > %d);",
		   cluster, assoc_table, now, width,
		   diff, *rgt,
		   diff, *lft,
		   cluster, assoc_table, now, width,
		   diff, *rgt,
		   diff, *lft);

	xstrfmtcat(query,
		   "update \"%s_%s\" set mod_time=%ld, "
		   "deleted = deleted - 2 WHERE deleted > 1;",
		   cluster, assoc_table, now);
	xstrfmtcat(query,
		   "update \"%s_%s\" set mod_time=%ld, "
		   "parent_acct='%s' where id_assoc = %s;",
		   cluster, assoc_table, now, parent, id);
	/* get the new lft and rgt if changed */
	xstrfmtcat(query,
		   "select lft, rgt from \"%s_%s\" where id_assoc = %s",
		   cluster, assoc_table, id);
	if (debug_flags & DEBUG_FLAG_DB_ASSOC)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 1))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);
	if ((row = mysql_fetch_row(result))) {
		debug4("lft and rgt were %u %u and now is %s %s",
		       *lft, *rgt, row[0], row[1]);
		*lft = slurm_atoul(row[0]);
		*rgt = slurm_atoul(row[1]);
	}
	mysql_free_result(result);

	return rc;
}


/* This code will move an account from one parent to another.  This
 * should work either way in the tree.  (i.e. move child to be parent
 * of current parent, and parent to be child of child.)
 */
static int _move_parent(mysql_conn_t *mysql_conn, uid_t uid,
			uint32_t *lft, uint32_t *rgt,
			char *cluster,
			char *id, char *old_parent, char *new_parent,
			time_t now)
{
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *query = NULL;
	int rc = SLURM_SUCCESS;

	/* first we need to see if we are going to make a child of this
	 * account the new parent.  If so we need to move that child to this
	 * accounts parent and then do the move.
	 */
	query = xstrdup_printf(
		"select id_assoc, lft, rgt from \"%s_%s\" "
		"where lft between %d and %d "
		"&& acct='%s' && user='' order by lft;",
		cluster, assoc_table, *lft, *rgt,
		new_parent);
	if (debug_flags & DEBUG_FLAG_DB_ASSOC)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result =
	      mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	if ((row = mysql_fetch_row(result))) {
		uint32_t child_lft = slurm_atoul(row[1]),
			child_rgt = slurm_atoul(row[2]);

		debug4("%s(%s) %s,%s is a child of %s",
		       new_parent, row[0], row[1], row[2], id);
		rc = _move_account(mysql_conn, &child_lft, &child_rgt,
				   cluster, row[0], old_parent, now);
	}

	mysql_free_result(result);

	if (rc != SLURM_SUCCESS)
		return rc;

	/* now move the one we wanted to move in the first place
	 * We need to get the new lft and rgts though since they may
	 * have changed.
	 */
	query = xstrdup_printf(
		"select lft, rgt from \"%s_%s\" where id_assoc=%s;",
		cluster, assoc_table, id);
	if (debug_flags & DEBUG_FLAG_DB_ASSOC)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result =
	      mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	if ((row = mysql_fetch_row(result))) {
		*lft = slurm_atoul(row[0]);
		*rgt = slurm_atoul(row[1]);
		rc = _move_account(mysql_conn, lft, rgt,
				   cluster, id, new_parent, now);
	} else {
		error("can't find parent? we were able to a second ago.");
		rc = SLURM_ERROR;
	}
	mysql_free_result(result);

	return rc;
}

static uint32_t _get_parent_id(
	mysql_conn_t *mysql_conn, char *parent, char *cluster)
{
	uint32_t parent_id = 0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *query = NULL;

	xassert(parent);
	xassert(cluster);

	query = xstrdup_printf("select id_assoc from \"%s_%s\" where user='' "
			       "and deleted = 0 and acct='%s';",
			       cluster, assoc_table, parent);
	debug4("%d(%s:%d) query\n%s",
	       mysql_conn->conn, THIS_FILE, __LINE__, query);

	if (!(result = mysql_db_query_ret(mysql_conn, query, 1))) {
		xfree(query);
		return 0;
	}
	xfree(query);

	if ((row = mysql_fetch_row(result))) {
		if (row[0])
			parent_id = slurm_atoul(row[0]);
	} else
		error("no association for parent %s on cluster %s",
		      parent, cluster);
	mysql_free_result(result);

	return parent_id;
}

static int _set_assoc_lft_rgt(
	mysql_conn_t *mysql_conn, slurmdb_assoc_rec_t *assoc)
{
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *query = NULL;
	int rc = SLURM_ERROR;

	xassert(assoc->cluster);
	xassert(assoc->id);

	query = xstrdup_printf("select lft, rgt from \"%s_%s\" "
			       "where id_assoc=%u;",
			       assoc->cluster, assoc_table, assoc->id);
	debug4("%d(%s:%d) query\n%s",
	       mysql_conn->conn, THIS_FILE, __LINE__, query);

	if (!(result = mysql_db_query_ret(mysql_conn, query, 1))) {
		xfree(query);
		return 0;
	}
	xfree(query);

	if ((row = mysql_fetch_row(result))) {
		if (row[0])
			assoc->lft = slurm_atoul(row[0]);
		if (row[1])
			assoc->rgt = slurm_atoul(row[1]);
		rc = SLURM_SUCCESS;
	} else
		error("no association (%u)", assoc->id);
	mysql_free_result(result);

	return rc;
}

static int _set_assoc_limits_for_add(
	mysql_conn_t *mysql_conn, slurmdb_assoc_rec_t *assoc)
{
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *query = NULL;
	char *parent = NULL;
	char *qos_delta = NULL;
	uint32_t tres_str_flags = TRES_STR_FLAG_REMOVE;

	xassert(assoc);

	if (assoc->parent_acct)
		parent = assoc->parent_acct;
	else if (assoc->user)
		parent = assoc->acct;
	else
		return SLURM_SUCCESS;

	query = xstrdup_printf("call get_parent_limits('%s', "
			       "'%s', '%s', %u); %s",
			       assoc_table, parent, assoc->cluster, 0,
			       get_parent_limits_select);
	debug4("%d(%s:%d) query\n%s",
	       mysql_conn->conn, THIS_FILE, __LINE__, query);
	if (!(result = mysql_db_query_ret(mysql_conn, query, 1))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	if (!(row = mysql_fetch_row(result)))
		goto end_it;

	if (row[ASSOC2_REQ_DEF_QOS] && assoc->def_qos_id == INFINITE)
		assoc->def_qos_id = slurm_atoul(row[ASSOC2_REQ_DEF_QOS]);
	else if (assoc->def_qos_id == INFINITE)
		assoc->def_qos_id = 0;

	if (row[ASSOC2_REQ_MJ] && assoc->max_jobs == INFINITE)
		assoc->max_jobs = slurm_atoul(row[ASSOC2_REQ_MJ]);
	if (row[ASSOC2_REQ_MJA] && assoc->max_jobs_accrue == INFINITE)
		assoc->max_jobs_accrue = slurm_atoul(row[ASSOC2_REQ_MJA]);
	if (row[ASSOC2_REQ_MPT] && assoc->min_prio_thresh == INFINITE)
		assoc->min_prio_thresh = slurm_atoul(row[ASSOC2_REQ_MPT]);
	if (row[ASSOC2_REQ_MSJ] && assoc->max_submit_jobs == INFINITE)
		assoc->max_submit_jobs = slurm_atoul(row[ASSOC2_REQ_MSJ]);
	if (row[ASSOC2_REQ_MWPJ] && assoc->max_wall_pj == INFINITE)
		assoc->max_wall_pj = slurm_atoul(row[ASSOC2_REQ_MWPJ]);

	/* For the tres limits we just concatted the limits going up
	 * the hierarchy slurmdb_tres_list_from_string will just skip
	 * over any reoccuring limit to give us the first one per
	 * TRES.
	 */
	slurmdb_combine_tres_strings(
		&assoc->max_tres_pj, row[ASSOC2_REQ_MTPJ],
		tres_str_flags);
	slurmdb_combine_tres_strings(
		&assoc->max_tres_pn, row[ASSOC2_REQ_MTPN],
		tres_str_flags);
	slurmdb_combine_tres_strings(
		&assoc->max_tres_mins_pj, row[ASSOC2_REQ_MTMPJ],
		tres_str_flags);
	slurmdb_combine_tres_strings(
		&assoc->max_tres_run_mins, row[ASSOC2_REQ_MTRM],
		tres_str_flags);

	if (assoc->qos_list) {
		int set = 0;
		char *tmp_char = NULL;
		ListIterator qos_itr = list_iterator_create(assoc->qos_list);
		while ((tmp_char = list_next(qos_itr))) {
			/* we don't want to include blank names */
			if (!tmp_char[0])
				continue;

			if (!set) {
				if (tmp_char[0] != '+' && tmp_char[0] != '-')
					break;
				set = 1;
			}
			xstrfmtcat(qos_delta, ",%s", tmp_char);
		}
		list_iterator_destroy(qos_itr);

		if (tmp_char) {
			/* we have the qos here nothing from parents
			   needed */
			goto end_it;
		}
		list_flush(assoc->qos_list);
	} else
		assoc->qos_list = list_create(slurm_destroy_char);

	if (row[ASSOC2_REQ_QOS][0])
		slurm_addto_char_list(assoc->qos_list, row[ASSOC2_REQ_QOS]+1);

	if (row[ASSOC2_REQ_DELTA_QOS][0])
		slurm_addto_char_list(assoc->qos_list,
				      row[ASSOC2_REQ_DELTA_QOS]+1);
	if (qos_delta) {
		slurm_addto_char_list(assoc->qos_list, qos_delta+1);
		xfree(qos_delta);
	}

end_it:
	mysql_free_result(result);

	return SLURM_SUCCESS;
}

/* Used to get all the users inside a lft and rgt set.  This is just
 * to send the user all the associations that are being modified from
 * a previous change to it's parent.
 */
static int _modify_unset_users(mysql_conn_t *mysql_conn,
			       slurmdb_assoc_rec_t *assoc,
			       char *acct,
			       uint32_t lft, uint32_t rgt,
			       List ret_list, int moved_parent)
{
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *query = NULL, *object = NULL;
	int i;
	uint32_t tres_str_flags = TRES_STR_FLAG_REMOVE | TRES_STR_FLAG_NO_NULL;

	char *assoc_inx[] = {
		"id_assoc",
		"user",
		"acct",
		"`partition`",
		"max_jobs",
		"max_jobs_accrue",
		"min_prio_thresh",
		"max_submit_jobs",
		"max_tres_pj",
		"max_tres_pn",
		"max_wall_pj",
		"max_tres_mins_pj",
		"max_tres_run_mins",
		"def_qos_id",
		"qos",
		"delta_qos",
		"lft",
		"rgt"
	};

	enum {
		ASSOC_ID,
		ASSOC_USER,
		ASSOC_ACCT,
		ASSOC_PART,
		ASSOC_MJ,
		ASSOC_MJA,
		ASSOC_MPT,
		ASSOC_MSJ,
		ASSOC_MTPJ,
		ASSOC_MTPN,
		ASSOC_MWPJ,
		ASSOC_MTMPJ,
		ASSOC_MTRM,
		ASSOC_DEF_QOS,
		ASSOC_QOS,
		ASSOC_DELTA_QOS,
		ASSOC_LFT,
		ASSOC_RGT,
		ASSOC_COUNT
	};

	xassert(assoc);
	xassert(assoc->cluster);

	if (!ret_list || !acct)
		return SLURM_ERROR;

	xstrcat(object, assoc_inx[0]);
	for(i=1; i<ASSOC_COUNT; i++)
		xstrfmtcat(object, ", %s", assoc_inx[i]);

	/* We want all the sub accounts and user accounts */
	query = xstrdup_printf("select distinct %s from \"%s_%s\" "
			       "where deleted=0 "
			       "&& lft between %d and %d && "
			       "((user = '' && parent_acct = '%s') || "
			       "(user != '' && acct = '%s')) "
			       "order by lft;",
			       object, assoc->cluster, assoc_table,
			       lft, rgt, acct, acct);
	xfree(object);
	if (debug_flags & DEBUG_FLAG_DB_ASSOC)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result =
	      mysql_db_query_ret(mysql_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	while ((row = mysql_fetch_row(result))) {
		slurmdb_assoc_rec_t *mod_assoc = NULL;
		int modified = 0;
		char *tmp_char = NULL;

		mod_assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
		slurmdb_init_assoc_rec(mod_assoc, 0);
		mod_assoc->id = slurm_atoul(row[ASSOC_ID]);
		mod_assoc->cluster = xstrdup(assoc->cluster);

		if (!row[ASSOC_DEF_QOS] && assoc->def_qos_id != NO_VAL) {
			mod_assoc->def_qos_id = assoc->def_qos_id;
			modified = 1;
		}

		if (!row[ASSOC_MJ] && assoc->max_jobs != NO_VAL) {
			mod_assoc->max_jobs = assoc->max_jobs;
			modified = 1;
		}

		if (!row[ASSOC_MJA] && assoc->max_jobs_accrue != NO_VAL) {
			mod_assoc->max_jobs_accrue = assoc->max_jobs_accrue;
			modified = 1;
		}

		if (!row[ASSOC_MPT] && assoc->min_prio_thresh != NO_VAL) {
			mod_assoc->min_prio_thresh = assoc->min_prio_thresh;
			modified = 1;
		}

		if (!row[ASSOC_MSJ] && assoc->max_submit_jobs != NO_VAL) {
			mod_assoc->max_submit_jobs = assoc->max_submit_jobs;
			modified = 1;
		}

		if (!row[ASSOC_MWPJ] && assoc->max_wall_pj != NO_VAL) {
			mod_assoc->max_wall_pj = assoc->max_wall_pj;
			modified = 1;
		}

		if (assoc->max_tres_pj) {
			tmp_char = xstrdup(row[ASSOC_MTPJ]);
			slurmdb_combine_tres_strings(
				&tmp_char, assoc->max_tres_pj,
				tres_str_flags);
			mod_assoc->max_tres_pj = tmp_char;
			tmp_char = NULL;
			modified = 1;
		}

		if (assoc->max_tres_pn) {
			tmp_char = xstrdup(row[ASSOC_MTPN]);
			slurmdb_combine_tres_strings(
				&tmp_char, assoc->max_tres_pn,
				tres_str_flags);
			mod_assoc->max_tres_pn = tmp_char;
			tmp_char = NULL;
			modified = 1;
		}

		if (assoc->max_tres_mins_pj) {
			tmp_char = xstrdup(row[ASSOC_MTMPJ]);
			slurmdb_combine_tres_strings(
				&tmp_char, assoc->max_tres_mins_pj,
				tres_str_flags);
			mod_assoc->max_tres_mins_pj = tmp_char;
			tmp_char = NULL;
			modified = 1;
		}

		if (assoc->max_tres_run_mins) {
			tmp_char = xstrdup(row[ASSOC_MTRM]);
			slurmdb_combine_tres_strings(
				&tmp_char, assoc->max_tres_run_mins,
				tres_str_flags);
			mod_assoc->max_tres_run_mins = tmp_char;
			tmp_char = NULL;
			modified = 1;
		}
		if (!row[ASSOC_QOS][0] && assoc->qos_list) {
			List delta_qos_list = NULL;
			char *qos_char = NULL, *delta_char = NULL;
			ListIterator delta_itr = NULL;
			ListIterator qos_itr =
				list_iterator_create(assoc->qos_list);
			if (row[ASSOC_DELTA_QOS][0]) {
				delta_qos_list =
					list_create(slurm_destroy_char);
				slurm_addto_char_list(delta_qos_list,
						      row[ASSOC_DELTA_QOS]+1);
				delta_itr =
					list_iterator_create(delta_qos_list);
			}

			mod_assoc->qos_list = list_create(slurm_destroy_char);
			/* here we are making sure a child does not
			   have the qos added or removed before we add
			   it to the parent.
			*/
			while ((qos_char = list_next(qos_itr))) {
				if (delta_itr && qos_char[0] != '=') {
					while ((delta_char =
						list_next(delta_itr))) {

						if ((qos_char[0]
						     != delta_char[0])
						    && (!xstrcmp(qos_char+1,
								 delta_char+1)))
							break;
					}
					list_iterator_reset(delta_itr);
					if (delta_char)
						continue;
				}
				list_append(mod_assoc->qos_list,
					    xstrdup(qos_char));
			}
			list_iterator_destroy(qos_itr);
			if (delta_itr)
				list_iterator_destroy(delta_itr);
			FREE_NULL_LIST(delta_qos_list);
			if (list_count(mod_assoc->qos_list)
			    || !list_count(assoc->qos_list))
				modified = 1;
			else {
				FREE_NULL_LIST(mod_assoc->qos_list);
				mod_assoc->qos_list = NULL;
			}
		}

		/* We only want to add those that are modified here */
		if (modified) {
			/* Since we aren't really changing this non
			 * user association we don't want to send it.
			 */
			if (!row[ASSOC_USER][0]) {
				/* This is a sub account so run it
				 * through as if it is a parent.
				 */
				_modify_unset_users(mysql_conn,
						    mod_assoc,
						    row[ASSOC_ACCT],
						    slurm_atoul(row[ASSOC_LFT]),
						    slurm_atoul(row[ASSOC_RGT]),
						    ret_list, moved_parent);
				slurmdb_destroy_assoc_rec(mod_assoc);
				continue;
			}
			/* We do want to send all user accounts though */
			mod_assoc->shares_raw = NO_VAL;
			if (row[ASSOC_PART][0]) {
				// see if there is a partition name
				object = xstrdup_printf(
					"C = %-10s A = %-20s U = %-9s P = %s",
					assoc->cluster, row[ASSOC_ACCT],
					row[ASSOC_USER], row[ASSOC_PART]);
			} else {
				object = xstrdup_printf(
					"C = %-10s A = %-20s U = %-9s",
					assoc->cluster,
					row[ASSOC_ACCT],
					row[ASSOC_USER]);
			}

			list_append(ret_list, object);
			object = NULL;

			if (moved_parent)
				slurmdb_destroy_assoc_rec(mod_assoc);
			else
				if (addto_update_list(mysql_conn->update_list,
						      SLURMDB_MODIFY_ASSOC,
						      mod_assoc)
				    != SLURM_SUCCESS) {
					slurmdb_destroy_assoc_rec(
						mod_assoc);
					error("couldn't add to "
					      "the update list");
				}
		} else
			slurmdb_destroy_assoc_rec(mod_assoc);

	}
	mysql_free_result(result);

	return SLURM_SUCCESS;
}

/* when doing a select on this all the select should have a prefix of
 * t1. Returns "where" clause which needs to be xfreed. */
static char *_setup_assoc_cond_qos(slurmdb_assoc_cond_t *assoc_cond,
				   char *cluster_name)
{
	int set = 0;
	ListIterator itr = NULL;
	char *object = NULL;
	char *prefix = "t1";
	char *extra = NULL;

	/* Since this gets put in an SQL query we don't want it to be
	 * NULL since it would print (null) instead of nothing.
	 */
	if (!assoc_cond)
		return xstrdup("");

	/* we need to check this first so we can update the
	   with_sub_accts if needed since this the qos_list is a
	   parent thing
	*/
	if (assoc_cond->qos_list && list_count(assoc_cond->qos_list)) {
		/* we have to do the same thing as with_sub_accts does
		   first since we are looking for something that is
		   really most likely a parent thing */
		assoc_cond->with_sub_accts = 1;
		prefix = "t2";
		xstrfmtcat(extra, ", \"%s_%s\" as t2 where "
			   "(t1.lft between t2.lft and t2.rgt) && (",
			   cluster_name, assoc_table);
		set = 0;
		itr = list_iterator_create(assoc_cond->qos_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(extra, " || ");
			xstrfmtcat(extra,
				   "(%s.qos like '%%,%s' "
				   "|| %s.qos like '%%,%s,%%' "
				   "|| %s.delta_qos like '%%,+%s' "
				   "|| %s.delta_qos like '%%,+%s,%%')",
				   prefix, object, prefix, object,
				   prefix, object, prefix, object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(extra, ") &&");
	} else if (assoc_cond->with_sub_accts) {
		xstrfmtcat(extra, ", \"%s_%s\" as t2 where "
			   "(t1.lft between t2.lft and t2.rgt) &&",
			   cluster_name, assoc_table);
	} else
		xstrcat(extra, " where");
	return extra;
}

/* When doing a select on this all the select should have a prefix of t1. */
static int _setup_assoc_cond_limits(
	slurmdb_assoc_cond_t *assoc_cond,
	const char *prefix, char **extra)
{
	int set = 0;
	ListIterator itr = NULL;
	char *object = NULL;

	if (!assoc_cond)
		return 0;

	/*
	 * Don't use prefix here, always use t1 or we could get extra "deleted"
	 * entries we don't want.
	 */
	if (assoc_cond->with_deleted)
		xstrfmtcat(*extra, " (t1.deleted=0 || t1.deleted=1)");
	else
		xstrfmtcat(*extra, " t1.deleted=0");

	if (assoc_cond->only_defs) {
		set = 1;
		xstrfmtcat(*extra, " && (%s.is_def=1)", prefix);
	}

	if (assoc_cond->acct_list && list_count(assoc_cond->acct_list)) {
		set = 0;
		xstrcat(*extra, " && (");
		itr = list_iterator_create(assoc_cond->acct_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "%s.acct='%s'", prefix, object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if (assoc_cond->def_qos_id_list
	    && list_count(assoc_cond->def_qos_id_list)) {
		set = 0;
		xstrcat(*extra, " && (");
		itr = list_iterator_create(assoc_cond->def_qos_id_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "%s.def_qos_id='%s'",
				   prefix, object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if (assoc_cond->user_list && list_count(assoc_cond->user_list)) {
		set = 0;
		xstrcat(*extra, " && (");
		itr = list_iterator_create(assoc_cond->user_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "%s.user='%s'", prefix, object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	} else if (assoc_cond->user_list) {
		/* we want all the users, but no non-user associations */
		set = 1;
		xstrfmtcat(*extra, " && (%s.user!='')", prefix);
	}

	if (assoc_cond->partition_list
	    && list_count(assoc_cond->partition_list)) {
		set = 0;
		xstrcat(*extra, " && (");
		itr = list_iterator_create(assoc_cond->partition_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "%s.partition='%s'",
				   prefix, object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if (assoc_cond->id_list && list_count(assoc_cond->id_list)) {
		set = 0;
		xstrcat(*extra, " && (");
		itr = list_iterator_create(assoc_cond->id_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "%s.id_assoc=%s", prefix, object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}

	if (assoc_cond->parent_acct_list
	    && list_count(assoc_cond->parent_acct_list)) {
		set = 0;
		xstrcat(*extra, " && (");
		itr = list_iterator_create(assoc_cond->parent_acct_list);
		while ((object = list_next(itr))) {
			if (set)
				xstrcat(*extra, " || ");
			xstrfmtcat(*extra, "%s.parent_acct='%s'",
				   prefix, object);
			set = 1;
		}
		list_iterator_destroy(itr);
		xstrcat(*extra, ")");
	}
	return set;
}

static int _process_modify_assoc_results(mysql_conn_t *mysql_conn,
					 MYSQL_RES *result,
					 slurmdb_assoc_rec_t *assoc,
					 slurmdb_user_rec_t *user,
					 char *cluster_name, char *sent_vals,
					 bool is_admin, bool same_user,
					 List ret_list)
{
	ListIterator itr = NULL;
	MYSQL_ROW row;
	int added = 0;
	int rc = SLURM_SUCCESS;
	int set_qos_vals = 0;
	int moved_parent = 0;
	char *query = NULL, *vals = NULL, *object = NULL, *name_char = NULL;
	char *reset_query = NULL;
	time_t now = time(NULL);

	xassert(result);

	if (!mysql_num_rows(result))
		return SLURM_SUCCESS;

	vals = xstrdup(sent_vals);

	while ((row = mysql_fetch_row(result))) {
		MYSQL_RES *result2 = NULL;
		slurmdb_assoc_rec_t *mod_assoc = NULL, alt_assoc;
		int account_type=0;
		/* If parent changes these also could change
		   so we need to keep track of the latest
		   ones.
		*/
		uint32_t lft = slurm_atoul(row[MASSOC_LFT]);
		uint32_t rgt = slurm_atoul(row[MASSOC_RGT]);
		char *orig_acct, *account;

		orig_acct = account = row[MASSOC_ACCT];

		slurmdb_init_assoc_rec(&alt_assoc, 0);

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
		if (row[MASSOC_PACCT][0])
			account = row[MASSOC_PACCT];

		/* If this is the same user all has been done
		   previously to make sure the user is only changing
		   things they are allowed to change.
		*/
		if (!is_admin && !same_user) {
			slurmdb_coord_rec_t *coord = NULL;

			if (!user->coord_accts) { // This should never
				// happen
				error("We are here with no coord accts.");
				rc = ESLURM_ACCESS_DENIED;
				goto end_it;
			}
			itr = list_iterator_create(user->coord_accts);
			while ((coord = list_next(itr))) {
				if (!xstrcasecmp(coord->name, account))
					break;
			}
			list_iterator_destroy(itr);

			if (!coord) {
				if (row[MASSOC_PACCT][0])
					error("User %s(%d) can not modify "
					      "account (%s) because they "
					      "are not coordinators of "
					      "parent account '%s'.",
					      user->name, user->uid,
					      row[MASSOC_ACCT],
					      row[MASSOC_PACCT]);
				else
					error("User %s(%d) does not have the "
					      "ability to modify the account "
					      "(%s).",
					      user->name, user->uid,
					      row[MASSOC_ACCT]);

				rc = ESLURM_ACCESS_DENIED;
				goto end_it;
			} else if (_check_coord_qos(mysql_conn, cluster_name,
						    account, user->name,
						    assoc->qos_list)
				   == SLURM_ERROR) {
				assoc_mgr_lock_t locks = {
					NO_LOCK, NO_LOCK, READ_LOCK, NO_LOCK,
					NO_LOCK, NO_LOCK, NO_LOCK };
				char *requested_qos;

				assoc_mgr_lock(&locks);
				requested_qos = get_qos_complete_str(
					assoc_mgr_qos_list, assoc->qos_list);
				assoc_mgr_unlock(&locks);
				error("Coordinator %s(%d) does not have the "
				      "access to all the qos requested (%s), "
				      "so they can't modify account "
				      "%s with it.",
				      user->name, user->uid, requested_qos,
				      account);
				xfree(requested_qos);
				rc = ESLURM_ACCESS_DENIED;
				goto end_it;
			}
		}

		if (row[MASSOC_PART][0]) {
			// see if there is a partition name
			object = xstrdup_printf(
				"C = %-10s A = %-20s U = %-9s P = %s",
				cluster_name, row[MASSOC_ACCT],
				row[MASSOC_USER], row[MASSOC_PART]);
		} else if (row[MASSOC_USER][0]){
			object = xstrdup_printf(
				"C = %-10s A = %-20s U = %-9s",
				cluster_name, row[MASSOC_ACCT],
				row[MASSOC_USER]);
		} else {
			if (assoc->parent_acct) {
				if (!xstrcasecmp(row[MASSOC_ACCT],
						assoc->parent_acct)) {
					error("You can't make an account be a "
					      "child of it's self");
					continue;
				}
				rc = _move_parent(mysql_conn, user->uid,
						  &lft, &rgt,
						  cluster_name,
						  row[MASSOC_ID],
						  row[MASSOC_PACCT],
						  assoc->parent_acct,
						  now);

				if ((rc == ESLURM_INVALID_PARENT_ACCOUNT)
				    || (rc == ESLURM_SAME_PARENT_ACCOUNT)) {
					continue;
				} else if (rc != SLURM_SUCCESS)
					break;

				moved_parent = 1;
			}
			if (row[MASSOC_PACCT][0]) {
				object = xstrdup_printf(
					"C = %-10s A = %s of %s",
					cluster_name, row[MASSOC_ACCT],
					row[MASSOC_PACCT]);
			} else {
				object = xstrdup_printf(
					"C = %-10s A = %s",
					cluster_name, row[MASSOC_ACCT]);
			}
			account_type = 1;
		}
		list_append(ret_list, object);
		object = NULL;
		added++;

		if (name_char)
			xstrfmtcat(name_char, " || id_assoc=%s",
				   row[MASSOC_ID]);
		else
			xstrfmtcat(name_char, "(id_assoc=%s", row[MASSOC_ID]);

		/* Only do this when not dealing with the root association. */
		if (xstrcmp(orig_acct, "root") || row[MASSOC_USER][0]) {
			MYSQL_ROW row2;
			/* If there is a variable cleared here we need to make
			   sure we get the parent's information, if any. */
			query = xstrdup_printf(
				"call get_parent_limits('%s', "
				"'%s', '%s', %u); %s",
				assoc_table, account,
				cluster_name, 0,
				get_parent_limits_select);
			debug4("%d(%s:%d) query\n%s",
			       mysql_conn->conn, THIS_FILE, __LINE__, query);
			if (!(result2 = mysql_db_query_ret(
				      mysql_conn, query, 1))) {
				xfree(query);
				break;
			}
			xfree(query);

			if ((row2 = mysql_fetch_row(result2))) {
				if (assoc->def_qos_id == INFINITE
				    && row2[ASSOC2_REQ_DEF_QOS])
					alt_assoc.def_qos_id = slurm_atoul(
						row2[ASSOC2_REQ_DEF_QOS]);

				if ((assoc->max_jobs == INFINITE)
				    && row2[ASSOC2_REQ_MJ])
					alt_assoc.max_jobs = slurm_atoul(
						row2[ASSOC2_REQ_MJ]);
				if ((assoc->max_jobs_accrue == INFINITE)
				    && row2[ASSOC2_REQ_MJA])
					alt_assoc.max_jobs_accrue = slurm_atoul(
						row2[ASSOC2_REQ_MJA]);
				if ((assoc->min_prio_thresh == INFINITE)
				    && row2[ASSOC2_REQ_MPT])
					alt_assoc.min_prio_thresh = slurm_atoul(
						row2[ASSOC2_REQ_MPT]);
				if ((assoc->max_submit_jobs == INFINITE)
				    && row2[ASSOC2_REQ_MSJ])
					alt_assoc.max_submit_jobs = slurm_atoul(
						row2[ASSOC2_REQ_MSJ]);
				if ((assoc->max_wall_pj == INFINITE)
				    && row2[ASSOC2_REQ_MWPJ])
					alt_assoc.max_wall_pj = slurm_atoul(
						row2[ASSOC2_REQ_MWPJ]);

				/* We don't have to copy these strings
				 * or check for there existance,
				 * slurmdb_combine_tres_strings will
				 * do this for us below.
				 */
				if (row2[ASSOC2_REQ_MTPJ][0])
					alt_assoc.max_tres_pj =
						row2[ASSOC2_REQ_MTPJ];
				if (row2[ASSOC2_REQ_MTPN][0])
					alt_assoc.max_tres_pn =
						row2[ASSOC2_REQ_MTPN];
				if (row2[ASSOC2_REQ_MTMPJ][0])
					alt_assoc.max_tres_mins_pj =
						row2[ASSOC2_REQ_MTMPJ];
				if (row2[ASSOC2_REQ_MTRM][0])
					alt_assoc.max_tres_run_mins =
						row2[ASSOC2_REQ_MTRM];
			}
		}
		mod_assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
		slurmdb_init_assoc_rec(mod_assoc, 0);
		mod_assoc->id = slurm_atoul(row[MASSOC_ID]);
		mod_assoc->cluster = xstrdup(cluster_name);

		if (alt_assoc.def_qos_id != NO_VAL)
			mod_assoc->def_qos_id = alt_assoc.def_qos_id;
		else
			mod_assoc->def_qos_id = assoc->def_qos_id;

		mod_assoc->is_def = assoc->is_def;

		mod_assoc->shares_raw = assoc->shares_raw;

		mod_tres_str(&mod_assoc->grp_tres,
			     assoc->grp_tres, row[MASSOC_GT],
			     NULL, "grp_tres", &vals, mod_assoc->id, 1);
		mod_tres_str(&mod_assoc->grp_tres_mins,
			     assoc->grp_tres_mins, row[MASSOC_GTM],
			     NULL, "grp_tres_mins", &vals, mod_assoc->id, 1);
		mod_tres_str(&mod_assoc->grp_tres_run_mins,
			     assoc->grp_tres_run_mins, row[MASSOC_GTRM],
			     NULL, "grp_tres_run_mins", &vals,
			     mod_assoc->id, 1);

		mod_assoc->grp_jobs = assoc->grp_jobs;
		mod_assoc->grp_jobs_accrue = assoc->grp_jobs_accrue;
		mod_assoc->grp_submit_jobs = assoc->grp_submit_jobs;
		mod_assoc->grp_wall = assoc->grp_wall;

		mod_tres_str(&mod_assoc->max_tres_pj,
			     assoc->max_tres_pj, row[MASSOC_MTPJ],
			     alt_assoc.max_tres_pj, "max_tres_pj",
			     &vals, mod_assoc->id, 1);
		mod_tres_str(&mod_assoc->max_tres_pn,
			     assoc->max_tres_pn, row[MASSOC_MTPN],
			     alt_assoc.max_tres_pn, "max_tres_pn",
			     &vals, mod_assoc->id, 1);
		mod_tres_str(&mod_assoc->max_tres_mins_pj,
			     assoc->max_tres_mins_pj, row[MASSOC_MTMPJ],
			     alt_assoc.max_tres_mins_pj, "max_tres_mins_pj",
			     &vals, mod_assoc->id, 1);
		mod_tres_str(&mod_assoc->max_tres_run_mins,
			     assoc->max_tres_run_mins, row[MASSOC_MTRM],
			     alt_assoc.max_tres_run_mins, "max_tres_run_mins",
			     &vals, mod_assoc->id, 1);

		if (result2)
			mysql_free_result(result2);

		if (alt_assoc.max_jobs != NO_VAL)
			mod_assoc->max_jobs = alt_assoc.max_jobs;
		else
			mod_assoc->max_jobs = assoc->max_jobs;
		if (alt_assoc.max_jobs_accrue != NO_VAL)
			mod_assoc->max_jobs_accrue = alt_assoc.max_jobs_accrue;
		else
			mod_assoc->max_jobs_accrue = assoc->max_jobs_accrue;
		if (alt_assoc.min_prio_thresh != NO_VAL)
			mod_assoc->min_prio_thresh = alt_assoc.min_prio_thresh;
		else
			mod_assoc->min_prio_thresh = assoc->min_prio_thresh;
		if (alt_assoc.max_submit_jobs != NO_VAL)
			mod_assoc->max_submit_jobs = alt_assoc.max_submit_jobs;
		else
			mod_assoc->max_submit_jobs = assoc->max_submit_jobs;
		if (alt_assoc.max_wall_pj != NO_VAL)
			mod_assoc->max_wall_pj = alt_assoc.max_wall_pj;
		else
			mod_assoc->max_wall_pj = assoc->max_wall_pj;

		/* no need to get the parent id since if we moved
		 * parent id's we will get it when we send the total list */

		if (!row[MASSOC_USER][0])
			mod_assoc->parent_acct = xstrdup(assoc->parent_acct);
		if (assoc->qos_list && list_count(assoc->qos_list)) {
			ListIterator new_qos_itr =
				list_iterator_create(assoc->qos_list);
			char *new_qos = NULL, *tmp_qos = NULL;
			bool adding_straight = 0;

			mod_assoc->qos_list = list_create(slurm_destroy_char);

			while ((new_qos = list_next(new_qos_itr))) {
				if (new_qos[0] == '-' || new_qos[0] == '+') {
					list_append(mod_assoc->qos_list,
						    xstrdup(new_qos));
				} else if (new_qos[0]) {
					list_append(mod_assoc->qos_list,
						    xstrdup_printf("=%s",
								   new_qos));
				}

				if (set_qos_vals)
					continue;
				/* Now we can set up the values and
				   make sure we aren't over writing
				   things that are really from the
				   parent
				*/
				if (new_qos[0] == '-') {
					xstrfmtcat(vals,
						   ", qos=if (qos='', '', "
						   "replace(replace("
						   "qos, ',%s,', ','), "
						   "',,', ','))"
						   ", delta_qos=if (qos='', "
						   "replace(concat(replace("
						   "replace("
						   "delta_qos, ',+%s,', ','), "
						   "',-%s,', ','), "
						   "',%s,'), ',,', ','), '')",
						   new_qos+1, new_qos+1,
						   new_qos+1, new_qos);
				} else if (new_qos[0] == '+') {
					xstrfmtcat(vals,
						   ", qos=if (qos='', '', "
						   "replace(concat("
						   "replace(qos, ',%s,', ','), "
						   "',%s,'), ',,', ',')), "
						   "delta_qos=if ("
						   "qos='', replace(concat("
						   "replace(replace("
						   "delta_qos, ',+%s,', ','), "
						   "',-%s,', ','), "
						   "',%s,'), ',,', ','), '')",
						   new_qos+1, new_qos+1,
						   new_qos+1, new_qos+1,
						   new_qos);
				} else if (new_qos[0]) {
					xstrfmtcat(tmp_qos, ",%s", new_qos);
					adding_straight = 1;
				} else
					xstrcat(tmp_qos, "");

			}
			list_iterator_destroy(new_qos_itr);

			if (!set_qos_vals && tmp_qos) {
				xstrfmtcat(vals, ", qos='%s%s', delta_qos=''",
					   tmp_qos, adding_straight ? "," : "");
			}
			xfree(tmp_qos);

			set_qos_vals = 1;
		}


		if (account_type) {
			_modify_unset_users(mysql_conn,
					    mod_assoc,
					    row[MASSOC_ACCT],
					    lft, rgt,
					    ret_list,
					    moved_parent);
		} else if ((assoc->is_def == 1) && row[MASSOC_USER][0]) {
			/* Use fresh one here so we don't have to
			   worry about dealing with bad values.
			*/
			slurmdb_assoc_rec_t tmp_assoc;
			slurmdb_init_assoc_rec(&tmp_assoc, 0);
			tmp_assoc.is_def = 1;
			tmp_assoc.cluster = cluster_name;
			tmp_assoc.acct = row[MASSOC_ACCT];
			tmp_assoc.user = row[MASSOC_USER];
			if ((rc = _reset_default_assoc(
				     mysql_conn, &tmp_assoc, &reset_query,
				     moved_parent ? 0 : 1))
			    != SLURM_SUCCESS) {
				slurmdb_destroy_assoc_rec(mod_assoc);
				xfree(reset_query);
				goto end_it;
			}
		}

		if (!vals || !vals[0] || moved_parent)
			slurmdb_destroy_assoc_rec(mod_assoc);
		else if (addto_update_list(mysql_conn->update_list,
					   SLURMDB_MODIFY_ASSOC,
					   mod_assoc) != SLURM_SUCCESS) {
			error("couldn't add to the update list");
			slurmdb_destroy_assoc_rec(mod_assoc);
		}
	}

	xstrcat(name_char, ")");

	if (assoc->parent_acct) {
		if (((rc == ESLURM_INVALID_PARENT_ACCOUNT)
		     || (rc == ESLURM_SAME_PARENT_ACCOUNT))
		    && added)
			rc = SLURM_SUCCESS;
	}

	if (rc != SLURM_SUCCESS)
		goto end_it;

	if (vals && vals[0]) {
		char *user_name = uid_to_string((uid_t) user->uid);
		rc = modify_common(mysql_conn, DBD_MODIFY_ASSOCS, now,
				   user_name, assoc_table, name_char, vals,
				   cluster_name);
		xfree(user_name);
		if (rc == SLURM_ERROR) {
			error("Couldn't modify associations");
			goto end_it;
		}
	}

	if (moved_parent) {
		List local_assoc_list = NULL;
		ListIterator local_itr = NULL;
		slurmdb_assoc_rec_t *local_assoc = NULL;
		slurmdb_assoc_cond_t local_assoc_cond;
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

		memset(&local_assoc_cond, 0,
		       sizeof(slurmdb_assoc_cond_t));
		local_assoc_cond.cluster_list = list_create(NULL);
		list_append(local_assoc_cond.cluster_list, cluster_name);
		local_assoc_list = as_mysql_get_assocs(
			mysql_conn, user->uid, &local_assoc_cond);
		FREE_NULL_LIST(local_assoc_cond.cluster_list);
		if (!local_assoc_list)
			goto end_it;
		/* NOTE: you can not use list_pop, or list_push
		   anywhere either, since as_mysql is
		   exporting something of the same type as a macro,
		   which messes everything up (my_list.h is
		   the bad boy).
		   So we are just going to delete each item as it
		   comes out since we are moving it to the update_list.
		*/
		local_itr = list_iterator_create(local_assoc_list);
		while ((local_assoc = list_next(local_itr))) {
			if (addto_update_list(mysql_conn->update_list,
					      SLURMDB_MODIFY_ASSOC,
					      local_assoc) == SLURM_SUCCESS)
				list_remove(local_itr);
		}
		list_iterator_destroy(local_itr);
		FREE_NULL_LIST(local_assoc_list);
	}

	if (reset_query) {
		if (debug_flags & DEBUG_FLAG_DB_ASSOC)
			DB_DEBUG(mysql_conn->conn, "query\n%s", reset_query);
		if ((rc = mysql_db_query(mysql_conn, reset_query))
		    != SLURM_SUCCESS)
			error("Couldn't update defaults");
	}
end_it:
	xfree(reset_query);
	xfree(name_char);
	xfree(vals);

	return rc;
}

static int _process_remove_assoc_results(mysql_conn_t *mysql_conn,
					 MYSQL_RES *result,
					 slurmdb_user_rec_t *user,
					 char *cluster_name,
					 char *name_char,
					 bool is_admin, List ret_list,
					 bool *jobs_running)
{
	ListIterator itr = NULL;
	MYSQL_ROW row;
	int rc = SLURM_SUCCESS;
	char *assoc_char = NULL, *object = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	uint32_t smallest_lft = 0xFFFFFFFF;

	xassert(result);
	if (*jobs_running)
		goto skip_process;

	while ((row = mysql_fetch_row(result))) {
		slurmdb_assoc_rec_t *rem_assoc = NULL;
		uint32_t lft;

		if (!is_admin) {
			slurmdb_coord_rec_t *coord = NULL;
			if (!user->coord_accts) { // This should never
				// happen
				error("We are here with no coord accts");
				rc = ESLURM_ACCESS_DENIED;
				goto end_it;
			}
			itr = list_iterator_create(user->coord_accts);
			while ((coord = list_next(itr))) {
				if (!xstrcasecmp(coord->name,
						row[RASSOC_ACCT]))
					break;
			}
			list_iterator_destroy(itr);

			if (!coord) {
				error("User %s(%d) does not have the "
				      "ability to change this account (%s)",
				      user->name, user->uid, row[RASSOC_ACCT]);
				rc = ESLURM_ACCESS_DENIED;
				goto end_it;
			}
		}
		if (row[RASSOC_PART][0]) {
			// see if there is a partition name
			object = xstrdup_printf(
				"C = %-10s A = %-10s U = %-9s P = %s",
				cluster_name, row[RASSOC_ACCT],
				row[RASSOC_USER], row[RASSOC_PART]);
		} else if (row[RASSOC_USER][0]){
			object = xstrdup_printf(
				"C = %-10s A = %-10s U = %-9s",
				cluster_name, row[RASSOC_ACCT],
				row[RASSOC_USER]);
		} else {
			if (row[RASSOC_PACCT][0]) {
				object = xstrdup_printf(
					"C = %-10s A = %s of %s",
					cluster_name, row[RASSOC_ACCT],
					row[RASSOC_PACCT]);
			} else {
				object = xstrdup_printf(
					"C = %-10s A = %s",
					cluster_name, row[RASSOC_ACCT]);
			}
		}
		list_append(ret_list, object);
		if (assoc_char)
			xstrfmtcat(assoc_char, " || id_assoc=%s",
				   row[RASSOC_ID]);
		else
			xstrfmtcat(assoc_char, "id_assoc=%s", row[RASSOC_ID]);

		/* get the smallest lft here to be able to send all
		   the modified lfts after it.
		*/
		lft = slurm_atoul(row[RASSOC_LFT]);
		if (lft < smallest_lft)
			smallest_lft = lft;

		rem_assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
		slurmdb_init_assoc_rec(rem_assoc, 0);
		rem_assoc->id = slurm_atoul(row[RASSOC_ID]);
		rem_assoc->cluster = xstrdup(cluster_name);
		if (addto_update_list(mysql_conn->update_list,
				      SLURMDB_REMOVE_ASSOC,
				      rem_assoc) != SLURM_SUCCESS) {
			slurmdb_destroy_assoc_rec(rem_assoc);
			error("couldn't add to the update list");
		}

	}

	if ((rc = as_mysql_get_modified_lfts(
		     mysql_conn, cluster_name, smallest_lft)) != SLURM_SUCCESS)
		goto end_it;


skip_process:
	user_name = uid_to_string((uid_t) user->uid);

	rc = remove_common(mysql_conn, DBD_REMOVE_ASSOCS, now,
			   user_name, assoc_table, name_char,
			   assoc_char, cluster_name, ret_list, jobs_running);
end_it:
	xfree(user_name);
	xfree(assoc_char);

	return rc;
}

static int _cluster_get_assocs(mysql_conn_t *mysql_conn,
			       slurmdb_user_rec_t *user,
			       slurmdb_assoc_cond_t *assoc_cond,
			       char *cluster_name,
			       char *fields, char *sent_extra,
			       bool is_admin, List sent_list)
{
	List assoc_list;
	List delta_qos_list = NULL;
	ListIterator itr = NULL;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	uint32_t parent_def_qos_id = 0;
	uint32_t parent_mj = INFINITE;
	uint32_t parent_mja = INFINITE;
	uint32_t parent_mpt = INFINITE;
	uint32_t parent_msj = INFINITE;
	uint32_t parent_mwpj = INFINITE;
	char *parent_mtpj = NULL;
	char *parent_mtpn = NULL;
	char *parent_mtmpj = NULL;
	char *parent_mtrm = NULL;
	char *parent_acct = NULL;
	char *parent_qos = NULL;
	char *parent_delta_qos = NULL;
	char *last_acct = NULL;
	char *last_cluster = NULL;
	uint32_t parent_id = 0;
	uint16_t private_data = slurm_get_private_data();
	char *query = NULL;
	char *extra = xstrdup(sent_extra);
	char *qos_extra = NULL;

	/* needed if we don't have an assoc_cond */
	uint16_t without_parent_info = 0;
	uint16_t without_parent_limits = 0;
	uint16_t with_usage = 0;
	uint16_t with_raw_qos = 0;

	if (assoc_cond) {
		with_raw_qos = assoc_cond->with_raw_qos;
		with_usage = assoc_cond->with_usage;
		without_parent_limits = assoc_cond->without_parent_limits;
		without_parent_info = assoc_cond->without_parent_info;
	}

	/* this is here to make sure we are looking at only this user
	 * if this flag is set.  We also include any accounts they may be
	 * coordinator of.
	 */
	if (!is_admin && (private_data & PRIVATE_DATA_USERS)) {
		int set = 0;
		query = xstrdup_printf("select lft from \"%s_%s\" where user='%s'",
				       cluster_name, assoc_table, user->name);
		if (user->coord_accts) {
			slurmdb_coord_rec_t *coord = NULL;
			itr = list_iterator_create(user->coord_accts);
			while ((coord = list_next(itr))) {
				xstrfmtcat(query, " || acct='%s'",
					   coord->name);
			}
			list_iterator_destroy(itr);
		}
		if (debug_flags & DEBUG_FLAG_DB_ASSOC)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);
		if (!(result = mysql_db_query_ret(
			      mysql_conn, query, 0))) {
			xfree(extra);
			xfree(query);
			return SLURM_ERROR;
		}
		xfree(query);
		set = 0;
		while ((row = mysql_fetch_row(result))) {
			if (set) {
				xstrfmtcat(extra,
					   " || (%s between t1.lft and t1.rgt)",
					   row[0]);
			} else {
				set = 1;
				xstrfmtcat(extra,
					   " && ((%s between t1.lft and t1.rgt)",
					   row[0]);
			}
		}

		mysql_free_result(result);

		if (set)
			xstrcat(extra, ")");
		else {
			xfree(extra);
			debug("User %s has no associations, and is not admin, "
			      "so not returning any.", user->name);
			/* This user has no valid associations, so
			 * end. */
			return SLURM_SUCCESS;
		}
	}

	qos_extra = _setup_assoc_cond_qos(assoc_cond, cluster_name);


	//START_TIMER;
	query = xstrdup_printf("select distinct %s from \"%s_%s\" as t1%s%s "
			       "order by lft;",
			       fields, cluster_name, assoc_table,
			       qos_extra, extra);
	xfree(qos_extra);
	xfree(extra);
	if (debug_flags & DEBUG_FLAG_DB_ASSOC)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(
		      mysql_conn, query, 0))) {
		xfree(query);
		if (mysql_errno(mysql_conn->db_conn) == ER_NO_SUCH_TABLE)
			return SLURM_SUCCESS;
		else
			return SLURM_ERROR;
	}
	xfree(query);

	if (!mysql_num_rows(result)) {
		mysql_free_result(result);
		return SLURM_SUCCESS;
	}

	assoc_list = list_create(slurmdb_destroy_assoc_rec);
	delta_qos_list = list_create(slurm_destroy_char);
	while ((row = mysql_fetch_row(result))) {
		slurmdb_assoc_rec_t *assoc =
			xmalloc(sizeof(slurmdb_assoc_rec_t));
		MYSQL_RES *result2 = NULL;
		MYSQL_ROW row2;

		list_append(assoc_list, assoc);
		assoc->id = slurm_atoul(row[ASSOC_REQ_ID]);
		assoc->is_def = slurm_atoul(row[ASSOC_REQ_DEFAULT]);
		assoc->lft = slurm_atoul(row[ASSOC_REQ_LFT]);
		assoc->rgt = slurm_atoul(row[ASSOC_REQ_RGT]);

		if (row[ASSOC_REQ_USER][0])
			assoc->user = xstrdup(row[ASSOC_REQ_USER]);
		assoc->acct = xstrdup(row[ASSOC_REQ_ACCT]);
		assoc->cluster = xstrdup(cluster_name);

		if (row[ASSOC_REQ_GJ])
			assoc->grp_jobs = slurm_atoul(row[ASSOC_REQ_GJ]);
		else
			assoc->grp_jobs = INFINITE;

		if (row[ASSOC_REQ_GJA])
			assoc->grp_jobs_accrue =
				slurm_atoul(row[ASSOC_REQ_GJA]);
		else
			assoc->grp_jobs_accrue = INFINITE;

		if (row[ASSOC_REQ_GSJ])
			assoc->grp_submit_jobs =
				slurm_atoul(row[ASSOC_REQ_GSJ]);
		else
			assoc->grp_submit_jobs = INFINITE;

		if (row[ASSOC_REQ_GW])
			assoc->grp_wall = slurm_atoul(row[ASSOC_REQ_GW]);
		else
			assoc->grp_wall = INFINITE;

		if (row[ASSOC_REQ_GT][0])
			assoc->grp_tres = xstrdup(row[ASSOC_REQ_GT]);
		if (row[ASSOC_REQ_GTM][0])
			assoc->grp_tres_mins = xstrdup(row[ASSOC_REQ_GTM]);
		if (row[ASSOC_REQ_GTRM][0])
			assoc->grp_tres_run_mins = xstrdup(row[ASSOC_REQ_GTRM]);

		parent_acct = row[ASSOC_REQ_ACCT];
		if (!without_parent_info
		    && row[ASSOC_REQ_PARENT][0]) {
			assoc->parent_acct = xstrdup(row[ASSOC_REQ_PARENT]);
			parent_acct = row[ASSOC_REQ_PARENT];
		} else if (!assoc->user) {
			/* This is the root association so we have no
			   parent id */
			parent_acct = NULL;
			parent_id = 0;
		}

		if (row[ASSOC_REQ_PART][0])
			assoc->partition = xstrdup(row[ASSOC_REQ_PART]);
		if (row[ASSOC_REQ_FS])
			assoc->shares_raw = slurm_atoul(row[ASSOC_REQ_FS]);
		else
			assoc->shares_raw = 1;

		if (!without_parent_info && parent_acct &&
		    (!last_acct || !last_cluster
		     || xstrcmp(parent_acct, last_acct)
		     || xstrcmp(cluster_name, last_cluster))) {
			query = xstrdup_printf(
				"call get_parent_limits('%s', "
				"'%s', '%s', %u); %s",
				assoc_table, parent_acct,
				cluster_name,
				without_parent_limits,
				get_parent_limits_select);
			debug4("%d(%s:%d) query\n%s",
			       mysql_conn->conn, THIS_FILE, __LINE__, query);
			if (!(result2 = mysql_db_query_ret(
				      mysql_conn, query, 1))) {
				xfree(query);
				break;
			}
			xfree(query);

			if (!(row2 = mysql_fetch_row(result2))) {
				parent_id = 0;
				goto no_parent_limits;
			}

			parent_id = slurm_atoul(row2[ASSOC2_REQ_PARENT_ID]);
			if (!without_parent_limits) {
				if (row2[ASSOC2_REQ_DEF_QOS])
					parent_def_qos_id = slurm_atoul(
						row2[ASSOC2_REQ_DEF_QOS]);
				else
					parent_def_qos_id = 0;

				if (row2[ASSOC2_REQ_MJ])
					parent_mj = slurm_atoul(
						row2[ASSOC2_REQ_MJ]);
				else
					parent_mj = INFINITE;

				if (row2[ASSOC2_REQ_MJA])
					parent_mja = slurm_atoul(
						row2[ASSOC2_REQ_MJA]);
				else
					parent_mja = INFINITE;

				if (row2[ASSOC2_REQ_MPT])
					parent_mpt = slurm_atoul(
						row2[ASSOC2_REQ_MPT]);
				else
					parent_mpt = INFINITE;

				if (row2[ASSOC2_REQ_MSJ])
					parent_msj = slurm_atoul(
						row2[ASSOC2_REQ_MSJ]);
				else
					parent_msj = INFINITE;

				if (row2[ASSOC2_REQ_MWPJ])
					parent_mwpj = slurm_atoul(
						row2[ASSOC2_REQ_MWPJ]);
				else
					parent_mwpj = INFINITE;

				xfree(parent_mtpj);
				if (row2[ASSOC2_REQ_MTPJ][0])
					parent_mtpj = xstrdup(
						row2[ASSOC2_REQ_MTPJ]);

				xfree(parent_mtpn);
				if (row2[ASSOC2_REQ_MTPN][0])
					parent_mtpn = xstrdup(
						row2[ASSOC2_REQ_MTPN]);

				xfree(parent_mtmpj);
				if (row2[ASSOC2_REQ_MTMPJ][0])
					parent_mtmpj = xstrdup(
						row2[ASSOC2_REQ_MTMPJ]);

				xfree(parent_mtrm);
				if (row2[ASSOC2_REQ_MTRM][0])
					parent_mtrm = xstrdup(
						row2[ASSOC2_REQ_MTRM]);

				xfree(parent_qos);
				if (row2[ASSOC2_REQ_QOS][0])
					parent_qos =
						xstrdup(row2[ASSOC2_REQ_QOS]);

				xfree(parent_delta_qos);
				if (row2[ASSOC2_REQ_DELTA_QOS][0])
					parent_delta_qos = xstrdup(
						row2[ASSOC2_REQ_DELTA_QOS]);
			}
			last_acct = parent_acct;
			last_cluster = cluster_name;
		no_parent_limits:
			mysql_free_result(result2);
		}

		if (row[ASSOC_REQ_DEF_QOS])
			assoc->def_qos_id = slurm_atoul(row[ASSOC_REQ_DEF_QOS]);
		else
			assoc->def_qos_id = parent_def_qos_id;

		if (row[ASSOC_REQ_MJ])
			assoc->max_jobs = slurm_atoul(row[ASSOC_REQ_MJ]);
		else
			assoc->max_jobs = parent_mj;

		if (row[ASSOC_REQ_MJA])
			assoc->max_jobs_accrue =
				slurm_atoul(row[ASSOC_REQ_MJA]);
		else
			assoc->max_jobs_accrue = parent_mja;

		if (row[ASSOC_REQ_MPT])
			assoc->min_prio_thresh = slurm_atoul(
				row[ASSOC_REQ_MPT]);
		else
			assoc->min_prio_thresh = parent_mpt;

		if (row[ASSOC_REQ_MSJ])
			assoc->max_submit_jobs = slurm_atoul(
				row[ASSOC_REQ_MSJ]);
		else
			assoc->max_submit_jobs = parent_msj;

		if (row[ASSOC_REQ_MWPJ])
			assoc->max_wall_pj = slurm_atoul(row[ASSOC_REQ_MWPJ]);
		else
			assoc->max_wall_pj = parent_mwpj;

		if (row[ASSOC_REQ_MTPJ][0])
			assoc->max_tres_pj = xstrdup(row[ASSOC_REQ_MTPJ]);

		if (row[ASSOC_REQ_MTPN][0])
			assoc->max_tres_pn = xstrdup(row[ASSOC_REQ_MTPN]);

		if (row[ASSOC_REQ_MTMPJ][0])
			assoc->max_tres_mins_pj = xstrdup(row[ASSOC_REQ_MTMPJ]);

		if (row[ASSOC_REQ_MTRM][0])
			assoc->max_tres_run_mins = xstrdup(row[ASSOC_REQ_MTRM]);

		/* For the tres limits we just concatted the limits going up
		 * the hierarchy slurmdb_tres_list_from_string will just skip
		 * over any reoccuring limit to give us the first one per
		 * TRES.
		 */
		slurmdb_combine_tres_strings(
			&assoc->max_tres_pj, parent_mtpj,
			TRES_STR_FLAG_SORT_ID);
		slurmdb_combine_tres_strings(
			&assoc->max_tres_pn, parent_mtpn,
			TRES_STR_FLAG_SORT_ID);
		slurmdb_combine_tres_strings(
			&assoc->max_tres_mins_pj, parent_mtmpj,
			TRES_STR_FLAG_SORT_ID);
		slurmdb_combine_tres_strings(
			&assoc->max_tres_run_mins, parent_mtrm,
			TRES_STR_FLAG_SORT_ID);

		assoc->qos_list = list_create(slurm_destroy_char);

		/* do a plus 1 since a comma is the first thing there
		 * in the list.  Also you can never have both a qos
		 * and a delta qos so if you have a qos don't worry
		 * about the delta.
		 */

		if (row[ASSOC_REQ_QOS][0])
			slurm_addto_char_list(assoc->qos_list,
					      row[ASSOC_REQ_QOS]+1);
		else {
			/* if qos is set on the association itself do
			   not worry about the deltas
			*/

			/* add the parents first */
			if (parent_qos)
				slurm_addto_char_list(assoc->qos_list,
						      parent_qos+1);

			/* then add the parents delta */
			if (parent_delta_qos)
				slurm_addto_char_list(delta_qos_list,
						      parent_delta_qos+1);

			/* now add the associations */
			if (row[ASSOC_REQ_DELTA_QOS][0])
				slurm_addto_char_list(
					delta_qos_list,
					row[ASSOC_REQ_DELTA_QOS]+1);
		}

		/* Sometimes we want to see exactly what is here in
		   the database instead of a complete list.  This will
		   give it to us.
		*/
		if (with_raw_qos && list_count(delta_qos_list)) {
			list_transfer(assoc->qos_list, delta_qos_list);
			list_flush(delta_qos_list);
		} else if (list_count(delta_qos_list)) {
			ListIterator curr_qos_itr =
				list_iterator_create(assoc->qos_list);
			ListIterator new_qos_itr =
				list_iterator_create(delta_qos_list);
			char *new_qos = NULL, *curr_qos = NULL;

			while ((new_qos = list_next(new_qos_itr))) {
				if (new_qos[0] == '-') {
					while ((curr_qos =
						list_next(curr_qos_itr))) {
						if (!xstrcmp(curr_qos,
							     new_qos+1)) {
							list_delete_item(
								curr_qos_itr);
							break;
						}
					}
					list_iterator_reset(curr_qos_itr);
				} else if (new_qos[0] == '+') {
					while ((curr_qos =
						list_next(curr_qos_itr))) {
						if (!xstrcmp(curr_qos,
							     new_qos+1)) {
							break;
						}
					}
					if (!curr_qos) {
						list_append(assoc->qos_list,
							    xstrdup(new_qos+1));
					}
					list_iterator_reset(curr_qos_itr);
				}
			}

			list_iterator_destroy(new_qos_itr);
			list_iterator_destroy(curr_qos_itr);
			list_flush(delta_qos_list);
		}

		assoc->parent_id = parent_id;

		//info("parent id is %d", assoc->parent_id);
		//log_assoc_rec(assoc);
	}
	xfree(parent_mtpj);
	xfree(parent_mtpn);
	xfree(parent_mtmpj);
	xfree(parent_mtrm);
	mysql_free_result(result);

	FREE_NULL_LIST(delta_qos_list);

	xfree(parent_delta_qos);
	xfree(parent_qos);

	if (with_usage && assoc_list && list_count(assoc_list))
		get_usage_for_list(mysql_conn, DBD_GET_ASSOC_USAGE,
				   assoc_list, cluster_name,
				   assoc_cond->usage_start,
				   assoc_cond->usage_end);

	list_transfer(sent_list, assoc_list);
	FREE_NULL_LIST(assoc_list);
	return SLURM_SUCCESS;
}

extern int as_mysql_get_modified_lfts(mysql_conn_t *mysql_conn,
				      char *cluster_name, uint32_t start_lft)
{
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *query = xstrdup_printf(
		"select id_assoc, lft from \"%s_%s\" where lft > %u "
		"&& deleted = 0",
		cluster_name, assoc_table, start_lft);
	if (debug_flags & DEBUG_FLAG_DB_ASSOC)
		DB_DEBUG(mysql_conn->conn, "query\n%s", query);
	if (!(result = mysql_db_query_ret(
		      mysql_conn, query, 0))) {
		xfree(query);
		error("couldn't query the database for modified lfts");
		return SLURM_ERROR;
	}
	xfree(query);

	while ((row = mysql_fetch_row(result))) {
		slurmdb_assoc_rec_t *assoc =
			xmalloc(sizeof(slurmdb_assoc_rec_t));
		slurmdb_init_assoc_rec(assoc, 0);
		assoc->id = slurm_atoul(row[0]);
		assoc->lft = slurm_atoul(row[1]);
		assoc->cluster = xstrdup(cluster_name);
		if (addto_update_list(mysql_conn->update_list,
				      SLURMDB_MODIFY_ASSOC,
				      assoc) != SLURM_SUCCESS)
			slurmdb_destroy_assoc_rec(assoc);
	}
	mysql_free_result(result);

	return SLURM_SUCCESS;
}

extern int as_mysql_add_assocs(mysql_conn_t *mysql_conn, uint32_t uid,
			       List assoc_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	int i=0;
	slurmdb_assoc_rec_t *object = NULL;
	char *cols = NULL, *vals = NULL, *txn_query = NULL;
	char *extra = NULL, *query = NULL, *update = NULL, *tmp_extra = NULL;
	char *parent = NULL;
	time_t now = time(NULL);
	char *user_name = NULL;
	char *tmp_char = NULL;
	int assoc_id = 0;
	int incr = 0, my_left = 0, my_par_id = 0;
	int moved_parent = 0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *old_parent = NULL, *old_cluster = NULL;
	char *last_parent = NULL, *last_cluster = NULL;
	List local_cluster_list = NULL;
	List added_user_list = NULL;
	bool is_coord = false;
	slurmdb_update_object_t *update_object = NULL;
	List assoc_list_tmp = NULL;

	if (!assoc_list) {
		error("No association list given");
		return SLURM_ERROR;
	}

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	if (!is_user_min_admin_level(mysql_conn, uid,
				     SLURMDB_ADMIN_OPERATOR)) {
		ListIterator itr2 = NULL;
		slurmdb_user_rec_t user;
		slurmdb_coord_rec_t *coord = NULL;
		slurmdb_assoc_rec_t *object = NULL;

		memset(&user, 0, sizeof(slurmdb_user_rec_t));
		user.uid = uid;

		if (!is_user_any_coord(mysql_conn, &user)) {
			error("Only admins/operators/coordinators "
			      "can add associations");
			return ESLURM_ACCESS_DENIED;
		}

		itr = list_iterator_create(assoc_list);
		itr2 = list_iterator_create(user.coord_accts);
		while ((object = list_next(itr))) {
			char *account = "root";
			if (object->user)
				account = object->acct;
			else if (object->parent_acct)
				account = object->parent_acct;
			list_iterator_reset(itr2);
			while ((coord = list_next(itr2))) {
				if (!xstrcasecmp(coord->name, account))
					break;
			}
			if (!coord)
				break;
		}
		list_iterator_destroy(itr2);
		list_iterator_destroy(itr);
		if (!coord)  {
			error("Coordinator %s(%d) tried to add associations "
			      "where they were not allowed",
			      user.name, user.uid);
			return ESLURM_ACCESS_DENIED;
		}
		is_coord = true;
	}

	local_cluster_list = list_create(NULL);
	user_name = uid_to_string((uid_t) uid);
	/* these need to be in a specific order */
	list_sort(assoc_list, (ListCmpF)_assoc_sort_cluster);

	itr = list_iterator_create(assoc_list);
	while ((object = list_next(itr))) {
		if (!object->cluster || !object->cluster[0]
		    || !object->acct || !object->acct[0]) {
			error("We need a association cluster and "
			      "acct to add one.");
			rc = SLURM_ERROR;
			continue;
		}

		/*
		 * If the user issuing the command is a coordinator,
		 * do not allow changing the default account
		 */
		if (is_coord && (object->is_def == 1)) {
			error("Coordinator %s(%d) tried to change the default account of user %s to account %s",
			      user_name, uid, object->user, object->acct);
			rc = ESLURM_ACCESS_DENIED;
			break;
		}

		if (is_coord && _check_coord_qos(mysql_conn, object->cluster,
						 object->acct, user_name,
						 object->qos_list)
		    == SLURM_ERROR) {
			assoc_mgr_lock_t locks = {
				NO_LOCK, NO_LOCK, READ_LOCK, NO_LOCK,
				NO_LOCK, NO_LOCK, NO_LOCK };
			char *requested_qos;

			assoc_mgr_lock(&locks);
			requested_qos = get_qos_complete_str(
				assoc_mgr_qos_list, object->qos_list);
			assoc_mgr_unlock(&locks);
			error("Coordinator %s(%d) does not have the "
			      "access to all the qos requested (%s), "
			      "so they can't add to account "
			      "%s with it.",
			      user_name, uid, requested_qos,
			      object->acct);
			xfree(requested_qos);
			rc = ESLURM_ACCESS_DENIED;
			break;
		}

		/* When adding if this isn't a default might as well
		   force it to be 0 to avoid confusion since
		   uninitialized it is NO_VAL.
		*/
		if (object->is_def != 1)
			object->is_def = 0;

		list_append(local_cluster_list, object->cluster);

		if (object->parent_acct) {
			parent = object->parent_acct;
		} else if (object->user) {
			parent = object->acct;
		} else {
			parent = "root";
		}

		xstrcat(cols, "creation_time, mod_time, acct");
		xstrfmtcat(vals, "%ld, %ld, '%s'",
			   now, now, object->acct);
		xstrfmtcat(update, "where acct='%s'", object->acct);

		xstrfmtcat(extra, ", mod_time=%ld, acct='%s'",
			   now, object->acct);
		if (!object->user) {
			xstrcat(cols, ", parent_acct");
			xstrfmtcat(vals, ", '%s'", parent);
			xstrfmtcat(extra, ", parent_acct='%s', user=''",
				   parent);
			xstrfmtcat(update, " && user=''");
		} else {
			char *part = object->partition;
			xstrcat(cols, ", user");
			xstrfmtcat(vals, ", '%s'", object->user);
			xstrfmtcat(update, " && user='%s'", object->user);
			xstrfmtcat(extra, ", user='%s'", object->user);

			/* We need to give a partition whether it be
			 * '' or the actual partition name given
			 */
			if (!part)
				part = "";
			xstrcat(cols, ", `partition`");
			xstrfmtcat(vals, ", '%s'", part);
			xstrfmtcat(update, " && `partition`='%s'", part);
			xstrfmtcat(extra, ", `partition`='%s'", part);
			if (!added_user_list)
				added_user_list = list_create(NULL);
			list_append(added_user_list, object->user);
		}

		if (object->id) {
			xstrcat(cols, ", id_assoc");
			xstrfmtcat(vals, ", '%u'", object->id);
			xstrfmtcat(update, " && id_assoc='%u'", object->id);
			xstrfmtcat(extra, ", id_assoc='%u'", object->id);
		}

		if ((rc = setup_assoc_limits(object, &cols, &vals, &extra,
					     QOS_LEVEL_NONE, 1))) {
			error("%s: Failed, setup_assoc_limits functions returned error",
			      __func__);
			xfree(query);
			xfree(cols);
			xfree(vals);
			xfree(extra);
			xfree(update);
			break;
		}


		xstrcat(tmp_char, aassoc_req_inx[0]);
		for(i=1; i<AASSOC_COUNT; i++)
			xstrfmtcat(tmp_char, ", %s", aassoc_req_inx[i]);

		xstrfmtcat(query,
			   "select distinct %s from \"%s_%s\" %s order by lft "
			   "FOR UPDATE;",
			   tmp_char, object->cluster, assoc_table, update);
		xfree(tmp_char);
		if (debug_flags & DEBUG_FLAG_DB_ASSOC)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);
		if (!(result = mysql_db_query_ret(
			      mysql_conn, query, 0))) {
			xfree(query);
			xfree(cols);
			xfree(vals);
			xfree(extra);
			xfree(update);
			error("couldn't query the database");
			rc = SLURM_ERROR;
			break;
		}
		xfree(query);

		assoc_id = 0;
		if (!(row = mysql_fetch_row(result))) {
			/* This code speeds up the add process quite a bit
			 * here we are only doing an update when we are done
			 * adding to a specific group (cluster/account) other
			 * than that we are adding right behind what we were
			 * so just total them up and then do one update
			 * instead of the slow ones that require an update
			 * every time.  There is a incr check outside of the
			 * loop to catch everything on the last spin of the
			 * while.
			 */
			if (!old_parent || !old_cluster
			    || xstrcasecmp(parent, old_parent)
			    || xstrcasecmp(object->cluster, old_cluster)) {
				char *sel_query = xstrdup_printf(
					"SELECT lft FROM \"%s_%s\" WHERE "
					"acct = '%s' and user = '' "
					"order by lft;",
					object->cluster, assoc_table,
					parent);
				MYSQL_RES *sel_result = NULL;

				if (incr) {
					char *up_query = xstrdup_printf(
						"UPDATE \"%s_%s\" SET "
						"rgt = rgt+%d "
						"WHERE rgt > %d && deleted < 2;"
						"UPDATE \"%s_%s\" SET "
						"lft = lft+%d "
						"WHERE lft > %d "
						"&& deleted < 2;"
						"UPDATE \"%s_%s\" SET "
						"deleted = 0 "
						"WHERE deleted = 2;",
						old_cluster, assoc_table,
						incr, my_left,
						old_cluster, assoc_table,
						incr, my_left,
						old_cluster, assoc_table);
					if (debug_flags & DEBUG_FLAG_DB_ASSOC)
						DB_DEBUG(mysql_conn->conn,
							 "query\n%s",
							 up_query);
					rc = mysql_db_query(
						mysql_conn,
						up_query);
					xfree(up_query);
					if (rc != SLURM_SUCCESS) {
						error("Couldn't do update");
						xfree(cols);
						xfree(vals);
						xfree(update);
						xfree(extra);
						xfree(sel_query);
						break;
					}
				}

				if (debug_flags & DEBUG_FLAG_DB_ASSOC)
					DB_DEBUG(mysql_conn->conn, "query\n%s",
						 sel_query);
				if (!(sel_result = mysql_db_query_ret(
					      mysql_conn,
					      sel_query, 0))) {
					xfree(cols);
					xfree(vals);
					xfree(update);
					xfree(extra);
					xfree(sel_query);
					rc = SLURM_ERROR;
					break;
				}

				if (!(row = mysql_fetch_row(sel_result))) {
					error("Couldn't get left from "
					      "query\n%s",
					      sel_query);
					mysql_free_result(sel_result);
					xfree(cols);
					xfree(vals);
					xfree(update);
					xfree(extra);
					xfree(sel_query);
					rc = SLURM_ERROR;
					break;
				}
				xfree(sel_query);

				my_left = slurm_atoul(row[0]);
				mysql_free_result(sel_result);
				//info("left is %d", my_left);
				old_parent = parent;
				old_cluster = object->cluster;
				incr = 0;
			}
			incr += 2;
			xstrfmtcat(query,
				   "insert into \"%s_%s\" "
				   "(%s, lft, rgt, deleted) "
				   "values (%s, %d, %d, 2);",
				   object->cluster, assoc_table, cols,
				   vals, my_left+(incr-1), my_left+incr);

			/* definantly works but slow */
/* 			xstrfmtcat(query, */
/* 				   "SELECT @myLeft := lft FROM %s WHERE " */
/* 				   "acct = '%s' " */
/* 				   "and cluster = '%s' and user = '';", */
/* 				   assoc_table, */
/* 				   parent, */
/* 				   object->cluster); */
/* 			xstrfmtcat(query, */
/* 				   "UPDATE %s SET rgt = rgt+2 " */
/* 				   "WHERE rgt > @myLeft;" */
/* 				   "UPDATE %s SET lft = lft+2 " */
/* 				   "WHERE lft > @myLeft;", */
/* 				   assoc_table, */
/* 				   assoc_table); */
/* 			xstrfmtcat(query, */
/* 				   "insert into %s (%s, lft, rgt) " */
/* 				   "values (%s, @myLeft+1, @myLeft+2);", */
/* 				   assoc_table, cols, */
/* 				   vals); */
		} else if (!slurm_atoul(row[AASSOC_DELETED])) {
			/* We don't need to do anything here */
			debug("This account %s was added already",
			      object->acct);
			xfree(cols);
			xfree(vals);
			xfree(update);
			mysql_free_result(result);
			xfree(extra);
			continue;
		} else {
			uint32_t lft = slurm_atoul(row[AASSOC_LFT]);
			uint32_t rgt = slurm_atoul(row[AASSOC_RGT]);

			/* If it was once deleted we have kept the lft
			 * and rgt's consant while it was deleted and
			 * so we can just unset the deleted flag,
			 * check for the parent and move if needed.
			 */
			assoc_id = slurm_atoul(row[AASSOC_ID]);
			if (object->parent_acct
			    && xstrcasecmp(object->parent_acct,
					   row[AASSOC_PACCT])) {

				/* We need to move the parent! */
				if (_move_parent(mysql_conn, uid,
						 &lft, &rgt,
						 object->cluster,
						 row[AASSOC_ID],
						 row[AASSOC_PACCT],
						 object->parent_acct, now)
				    == SLURM_ERROR)
					continue;
				moved_parent = 1;
			} else {
				object->lft = lft;
				object->rgt = rgt;
			}

			xstrfmtcat(query,
				   "update \"%s_%s\" set deleted=0, "
				   "id_assoc=LAST_INSERT_ID(id_assoc)%s %s;",
				   object->cluster, assoc_table,
				   extra, update);
		}
		mysql_free_result(result);

		xfree(cols);
		xfree(vals);
		xfree(update);
		if (debug_flags & DEBUG_FLAG_DB_ASSOC)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);
		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
		if (rc != SLURM_SUCCESS) {
			error("Couldn't add assoc");
			xfree(extra);
			break;
		}
		/* see if this was an insert or update.  On an update
		 * the assoc_id will already be set
		 */
		if (!assoc_id) {
			(void) last_affected_rows(mysql_conn);
			assoc_id = mysql_insert_id(mysql_conn->db_conn);
			//info("last id was %d", assoc_id);
		}

		object->id = assoc_id;

		/* get the parent id only if we haven't moved the
		 * parent since we get the total list if that has
		 * happened */
		if (!moved_parent &&
		    (!last_parent || !last_cluster
		     || xstrcmp(parent, last_parent)
		     || xstrcmp(object->cluster, last_cluster))) {
			uint32_t tmp32 = 0;
			if ((tmp32 = _get_parent_id(mysql_conn,
						    parent,
						    object->cluster))) {
				my_par_id = tmp32;

				last_parent = parent;
				last_cluster = object->cluster;
			}
		}
		object->parent_id = my_par_id;

		if (!moved_parent) {
			_set_assoc_limits_for_add(mysql_conn, object);
			if (object->lft == NO_VAL)
				_set_assoc_lft_rgt(mysql_conn, object);
		}

		if (addto_update_list(mysql_conn->update_list,
				      SLURMDB_ADD_ASSOC,
				      object) == SLURM_SUCCESS) {
			list_remove(itr);
		}

		/* We don't want to record the transactions of the
		 * tmp_cluster.
		 */

		if (xstrcmp(object->cluster, tmp_cluster_name)) {
			/* we always have a ', ' as the first 2 chars */
			tmp_extra = slurm_add_slash_to_quotes(extra+2);
			if (txn_query)
				xstrfmtcat(txn_query,
					   ", (%ld, %d, 'id_assoc=%d', "
					   "'%s', '%s', '%s')",
					   now, DBD_ADD_ASSOCS, assoc_id,
					   user_name,
					   tmp_extra, object->cluster);
			else
				xstrfmtcat(txn_query,
					   "insert into %s "
					   "(timestamp, action, name, actor, "
					   "info, cluster) values (%ld, %d, "
					   "'id_assoc=%d', '%s', '%s', '%s')",
					   txn_table,
					   now, DBD_ADD_ASSOCS, assoc_id,
					   user_name,
					   tmp_extra, object->cluster);
			xfree(tmp_extra);
		}
		xfree(extra);
	}
	list_iterator_destroy(itr);
	xfree(user_name);

	if (rc != SLURM_SUCCESS)
		goto end_it;

	if (incr) {
		char *up_query = xstrdup_printf(
			"UPDATE \"%s_%s\" SET rgt = rgt+%d "
			"WHERE rgt > %d && deleted < 2;"
			"UPDATE \"%s_%s\" SET lft = lft+%d "
			"WHERE lft > %d "
			"&& deleted < 2;"
			"UPDATE \"%s_%s\" SET deleted = 0 "
			"WHERE deleted = 2;",
			old_cluster, assoc_table, incr,
			my_left,
			old_cluster, assoc_table, incr,
			my_left,
			old_cluster, assoc_table);
		if (debug_flags & DEBUG_FLAG_DB_ASSOC)
			DB_DEBUG(mysql_conn->conn, "query\n%s", up_query);
		rc = mysql_db_query(mysql_conn, up_query);
		xfree(up_query);
		if (rc != SLURM_SUCCESS)
			error("Couldn't do update 2");

	}

	/* Since we are already removed all the items from assoc_list
	 * we need to work off the update_list from here on out.
	 */
	itr = list_iterator_create(mysql_conn->update_list);;
	while ((update_object = list_next(itr))) {
		if (!update_object->objects ||
		    !list_count(update_object->objects))
			continue;
		if (update_object->type == SLURMDB_ADD_ASSOC)
			break;
	}
	list_iterator_destroy(itr);

	if (update_object && update_object->objects
	    && list_count(update_object->objects))
		assoc_list_tmp = update_object->objects;

	if (assoc_list_tmp) {
		ListIterator itr2 = list_iterator_create(assoc_list_tmp);

		if (!moved_parent) {
			char *cluster_name;

			slurm_mutex_lock(&as_mysql_cluster_list_lock);
			itr = list_iterator_create(as_mysql_cluster_list);
			while ((cluster_name = list_next(itr))) {
				uint32_t smallest_lft = 0xFFFFFFFF;
				while ((object = list_next(itr2))) {
					if (object->lft < smallest_lft
					    && !xstrcmp(object->cluster,
							cluster_name))
						smallest_lft = object->lft;
				}
				list_iterator_reset(itr2);
				/* now get the lowest lft from the
				   added files by cluster */
				if (smallest_lft != 0xFFFFFFFF)
					rc = as_mysql_get_modified_lfts(
						mysql_conn, cluster_name,
						smallest_lft);
			}
			list_iterator_destroy(itr);
			slurm_mutex_unlock(&as_mysql_cluster_list_lock);
		}

		/* make sure we don't have any other default accounts */
		list_iterator_reset(itr2);
		while ((object = list_next(itr2))) {
			if ((object->is_def != 1) || !object->cluster
			    || !object->acct || !object->user)
				continue;

			if ((rc = _reset_default_assoc(
				     mysql_conn, object,
				     &query, moved_parent ? 0 : 1))
			    != SLURM_SUCCESS) {
				xfree(query);
				goto end_it;
			}
		}
		list_iterator_destroy(itr2);
		/* This temp list is no longer needed */
		assoc_list_tmp = NULL;
	}

	if (query) {
		if (debug_flags & DEBUG_FLAG_DB_ASSOC)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);
		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
		if (rc != SLURM_SUCCESS)
			error("Couldn't update defaults");
	}
end_it:

	if (rc != SLURM_ERROR) {
		_make_sure_users_have_default(mysql_conn, added_user_list);
		FREE_NULL_LIST(added_user_list);

		if (txn_query) {
			xstrcat(txn_query, ";");
			debug4("%d(%s:%d) query\n%s",
			       mysql_conn->conn, THIS_FILE,
			       __LINE__, txn_query);
			rc = mysql_db_query(mysql_conn,
					    txn_query);
			xfree(txn_query);
			if (rc != SLURM_SUCCESS) {
				error("Couldn't add txn");
				rc = SLURM_SUCCESS;
			}
		}
		if (moved_parent) {
			ListIterator itr = NULL;
			slurmdb_assoc_rec_t *assoc = NULL;
			slurmdb_assoc_cond_t assoc_cond;
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
			memset(&assoc_cond, 0,
			       sizeof(slurmdb_assoc_cond_t));
			assoc_cond.cluster_list = local_cluster_list;
			if (!(assoc_list_tmp =
			      as_mysql_get_assocs(mysql_conn, uid, NULL))) {
				FREE_NULL_LIST(local_cluster_list);
				return rc;
			}
			/* NOTE: you can not use list_pop, or list_push
			   anywhere either, since as_mysql is
			   exporting something of the same type as a macro,
			   which messes everything up (my_list.h is
			   the bad boy).
			   So we are just going to delete each item as it
			   comes out since we are moving it to the update_list.
			*/
			itr = list_iterator_create(assoc_list_tmp);
			while ((assoc = list_next(itr))) {
				if (addto_update_list(mysql_conn->update_list,
						      SLURMDB_MODIFY_ASSOC,
						      assoc) == SLURM_SUCCESS)
					list_remove(itr);
			}
			list_iterator_destroy(itr);
			FREE_NULL_LIST(assoc_list_tmp);
		}
	} else {
		FREE_NULL_LIST(added_user_list);
		xfree(txn_query);
		reset_mysql_conn(mysql_conn);
	}
	FREE_NULL_LIST(local_cluster_list);
	return rc;
}

extern List as_mysql_modify_assocs(mysql_conn_t *mysql_conn, uint32_t uid,
				   slurmdb_assoc_cond_t *assoc_cond,
				   slurmdb_assoc_rec_t *assoc)
{
	ListIterator itr = NULL;
	List ret_list = NULL;
	int rc = SLURM_SUCCESS;
	char *object = NULL;
	char *vals = NULL, *extra = NULL, *query = NULL;
	int i = 0;
	bool is_admin=0, same_user=0;
	MYSQL_RES *result = NULL;
	slurmdb_user_rec_t user;
	char *tmp_char1=NULL, *tmp_char2=NULL;
	char *cluster_name = NULL;
	char *prefix = "t1";
	List use_cluster_list = as_mysql_cluster_list;

	if (!assoc_cond || !assoc) {
		error("we need something to change");
		return NULL;
	}

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	memset(&user, 0, sizeof(slurmdb_user_rec_t));
	user.uid = uid;

	if (!(is_admin = is_user_min_admin_level(
		      mysql_conn, uid, SLURMDB_ADMIN_OPERATOR))) {
		if (is_user_any_coord(mysql_conn, &user)) {
			goto is_same_user;
		} else if (assoc_cond->user_list
			   && (list_count(assoc_cond->user_list) == 1)) {
			uid_t pw_uid;
			char *name;
			name = list_peek(assoc_cond->user_list);
		        if ((uid_from_string (name, &pw_uid) >= 0)
			    && (pw_uid == uid)) {
				uint16_t is_def = assoc->is_def;
				uint32_t def_qos_id = assoc->def_qos_id;
				/* Make sure they aren't trying to
				   change something they aren't
				   allowed to.  Currently they are
				   only allowed to change the default
				   account, and default QOS.
				*/
				slurmdb_init_assoc_rec(assoc, 1);

				assoc->is_def = is_def;
				assoc->def_qos_id = def_qos_id;
				same_user = 1;

				goto is_same_user;
			}
		}

		error("Only admins/coordinators can modify associations");
		errno = ESLURM_ACCESS_DENIED;
		return NULL;
	}
is_same_user:

	if ((assoc_cond->qos_list && list_count(assoc_cond->qos_list))
	    || assoc_cond->with_sub_accts)
		prefix = "t2";

	(void) _setup_assoc_cond_limits(assoc_cond, prefix, &extra);

	/* This needs to be here to make sure we only modify the
	   correct set of assocs The first clause was already
	   taken care of above. */
	if (assoc_cond->user_list && !list_count(assoc_cond->user_list)) {
		debug4("no user specified looking at users");
		xstrcat(extra, " && user != '' ");
	} else if (!assoc_cond->user_list) {
		debug4("no user specified looking at accounts");
		xstrcat(extra, " && user = '' ");
	}

	if ((rc = setup_assoc_limits(assoc, &tmp_char1, &tmp_char2,
				     &vals, QOS_LEVEL_MODIFY, 0))) {
		xfree(tmp_char1);
		xfree(tmp_char2);
		xfree(vals);
		xfree(extra);
		errno = rc;
		error("%s: Failed, setup_assoc_limits functions returned error",
		      __func__);
		return NULL;
	}
	xfree(tmp_char1);
	xfree(tmp_char2);

	if (!extra || (!vals && !assoc->parent_acct)) {
		xfree(vals);
		xfree(extra);
		errno = SLURM_NO_CHANGE_IN_DATA;
		error("Nothing to change");
		return NULL;
	}

	xstrfmtcat(object, "t1.%s", massoc_req_inx[0]);
	for(i=1; i<MASSOC_COUNT; i++)
		xstrfmtcat(object, ", t1.%s", massoc_req_inx[i]);

	ret_list = list_create(slurm_destroy_char);

	if (assoc_cond->cluster_list && list_count(assoc_cond->cluster_list))
		use_cluster_list = assoc_cond->cluster_list;
	else
		slurm_mutex_lock(&as_mysql_cluster_list_lock);

	itr = list_iterator_create(use_cluster_list);
	while ((cluster_name = list_next(itr))) {
		char *qos_extra = _setup_assoc_cond_qos(
			assoc_cond, cluster_name);

		xstrfmtcat(query, "select distinct %s "
			   "from \"%s_%s\" as t1%s%s "
			   "order by lft FOR UPDATE;",
			   object, cluster_name,
			   assoc_table, qos_extra, extra);
		xfree(qos_extra);
		if (debug_flags & DEBUG_FLAG_DB_ASSOC)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);
		if (!(result = mysql_db_query_ret(
			      mysql_conn, query, 0))) {
			xfree(query);
			if (mysql_errno(mysql_conn->db_conn)
			    != ER_NO_SUCH_TABLE) {
				FREE_NULL_LIST(ret_list);
				ret_list = NULL;
			}
			break;
		}
		xfree(query);
		rc = _process_modify_assoc_results(mysql_conn, result, assoc,
						   &user, cluster_name, vals,
						   is_admin, same_user,
						   ret_list);
		mysql_free_result(result);

		if ((rc == ESLURM_INVALID_PARENT_ACCOUNT)
		    || (rc == ESLURM_SAME_PARENT_ACCOUNT)) {
			continue;
		} else if (rc != SLURM_SUCCESS) {
			FREE_NULL_LIST(ret_list);
			ret_list = NULL;
			break;
		}
	}
	list_iterator_destroy(itr);
	if (use_cluster_list == as_mysql_cluster_list)
		slurm_mutex_unlock(&as_mysql_cluster_list_lock);
	xfree(vals);
	xfree(object);
	xfree(extra);

	if (!ret_list) {
		reset_mysql_conn(mysql_conn);
		errno = rc;
		return NULL;
	} else if (!list_count(ret_list)) {
		reset_mysql_conn(mysql_conn);
		errno = SLURM_NO_CHANGE_IN_DATA;
		if (debug_flags & DEBUG_FLAG_DB_ASSOC)
			DB_DEBUG(mysql_conn->conn, "didn't effect anything");
		return ret_list;
	}

	return ret_list;
}

extern List as_mysql_remove_assocs(mysql_conn_t *mysql_conn, uint32_t uid,
				   slurmdb_assoc_cond_t *assoc_cond)
{
	ListIterator itr = NULL;
	List ret_list = NULL;
	int rc = SLURM_SUCCESS;
	char *object = NULL, *cluster_name = NULL;
	char *extra = NULL, *query = NULL, *name_char = NULL;
	int i = 0, is_admin = 0;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	slurmdb_user_rec_t user;
	char *prefix = "t1";
	List use_cluster_list = as_mysql_cluster_list;
	bool jobs_running = 0;

	if (!assoc_cond) {
		error("we need something to change");
		return NULL;
	}

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	memset(&user, 0, sizeof(slurmdb_user_rec_t));
	user.uid = uid;

	if (!(is_admin = is_user_min_admin_level(
		      mysql_conn, uid, SLURMDB_ADMIN_OPERATOR))) {
		if (!is_user_any_coord(mysql_conn, &user)) {
			error("Only admins/coordinators can "
			      "remove associations");
			errno = ESLURM_ACCESS_DENIED;
			return NULL;
		}
	}

	if ((assoc_cond->qos_list && list_count(assoc_cond->qos_list))
	    || assoc_cond->with_sub_accts)
		prefix = "t2";

	(void)_setup_assoc_cond_limits(assoc_cond, prefix, &extra);

	xstrcat(object, rassoc_req_inx[0]);
	for(i=1; i<RASSOC_COUNT; i++)
		xstrfmtcat(object, ", %s", rassoc_req_inx[i]);

	ret_list = list_create(slurm_destroy_char);

	if (assoc_cond->cluster_list && list_count(assoc_cond->cluster_list))
		use_cluster_list = assoc_cond->cluster_list;
	else
		slurm_mutex_lock(&as_mysql_cluster_list_lock);

	itr = list_iterator_create(use_cluster_list);
	while ((cluster_name = list_next(itr))) {
		char *qos_extra = _setup_assoc_cond_qos(
			assoc_cond, cluster_name);

		query = xstrdup_printf("select distinct t1.lft, t1.rgt from "
				       "\"%s_%s\" as t1%s%s order by "
				       "lft FOR UPDATE;",
				       cluster_name, assoc_table,
				       qos_extra, extra);
		xfree(qos_extra);
		if (debug_flags & DEBUG_FLAG_DB_ASSOC)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);
		if (!(result = mysql_db_query_ret(
			      mysql_conn, query, 0))) {
			xfree(query);
			if (mysql_errno(mysql_conn->db_conn)
			    != ER_NO_SUCH_TABLE) {
				FREE_NULL_LIST(ret_list);
				ret_list = NULL;
			}
			break;
		}
		xfree(query);

		if (!mysql_num_rows(result)) {
			mysql_free_result(result);
			continue;
		}

		while ((row = mysql_fetch_row(result))) {
			if (name_char)
				xstrfmtcat(name_char,
					   " || lft between %s and %s",
					   row[0], row[1]);
			else
				xstrfmtcat(name_char, "lft between %s and %s",
					   row[0], row[1]);
		}
		mysql_free_result(result);

		query = xstrdup_printf("select distinct %s "
				       "from \"%s_%s\" where (%s) "
				       "and deleted = 0 order by lft;",
				       object,
				       cluster_name, assoc_table, name_char);
		if (debug_flags & DEBUG_FLAG_DB_ASSOC)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);
		if (!(result = mysql_db_query_ret(
			      mysql_conn, query, 0))) {
			xfree(query);
			xfree(name_char);
			FREE_NULL_LIST(ret_list);
			ret_list = NULL;
			break;
		}
		xfree(query);

		rc = _process_remove_assoc_results(mysql_conn, result,
						   &user, cluster_name,
						   name_char, is_admin,
						   ret_list, &jobs_running);
		xfree(name_char);
		mysql_free_result(result);

		if (rc != SLURM_SUCCESS) {
			FREE_NULL_LIST(ret_list);
			ret_list = NULL;
			break;
		}
	}
	list_iterator_destroy(itr);
	if (use_cluster_list == as_mysql_cluster_list)
		slurm_mutex_unlock(&as_mysql_cluster_list_lock);
	xfree(object);
	xfree(extra);

	if (!ret_list) {
		reset_mysql_conn(mysql_conn);
		return NULL;
	} else if (!list_count(ret_list)) {
		reset_mysql_conn(mysql_conn);
		errno = SLURM_NO_CHANGE_IN_DATA;
		if (debug_flags & DEBUG_FLAG_DB_ASSOC)
			DB_DEBUG(mysql_conn->conn, "didn't effect anything");
		return ret_list;
	}
	if (jobs_running)
		errno = ESLURM_JOBS_RUNNING_ON_ASSOC;
	else
		errno = SLURM_SUCCESS;
	return ret_list;
}

extern List as_mysql_get_assocs(mysql_conn_t *mysql_conn, uid_t uid,
				slurmdb_assoc_cond_t *assoc_cond)
{
	//DEF_TIMERS;
	char *extra = NULL;
	char *tmp = NULL;
	List assoc_list = NULL;
	ListIterator itr = NULL;
	int i=0, is_admin=1;
	uint16_t private_data = 0;
	slurmdb_user_rec_t user;
	char *prefix = "t1";
	List use_cluster_list = as_mysql_cluster_list;
	char *cluster_name = NULL;

	if (!assoc_cond) {
		xstrcat(extra, " where deleted=0");
		goto empty;
	}

	if (check_connection(mysql_conn) != SLURM_SUCCESS)
		return NULL;

	memset(&user, 0, sizeof(slurmdb_user_rec_t));
	user.uid = uid;

	private_data = slurm_get_private_data();
	if (private_data & PRIVATE_DATA_USERS) {
		if (!(is_admin = is_user_min_admin_level(
			      mysql_conn, uid, SLURMDB_ADMIN_OPERATOR))) {
			/* Fill in the user with any accounts they may
			   be coordinator of, which is checked inside
			   _cluster_get_assocs.
			*/
			assoc_mgr_fill_in_user(
				mysql_conn, &user, 1, NULL, false);
		}
		if (!is_admin && !user.name) {
			debug("User %u has no associations, and is not admin, "
			      "so not returning any.", user.uid);
			return NULL;
		}
	}

	if ((assoc_cond->qos_list && list_count(assoc_cond->qos_list))
	    || assoc_cond->with_sub_accts)
		prefix = "t2";

	(void) _setup_assoc_cond_limits(assoc_cond, prefix, &extra);

	if (assoc_cond->cluster_list && list_count(assoc_cond->cluster_list))
		use_cluster_list = assoc_cond->cluster_list;
empty:
	xfree(tmp);
	xstrfmtcat(tmp, "t1.%s", assoc_req_inx[i]);
	for(i=1; i<ASSOC_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", t1.%s", assoc_req_inx[i]);
	}
	assoc_list = list_create(slurmdb_destroy_assoc_rec);

	if (use_cluster_list == as_mysql_cluster_list)
		slurm_mutex_lock(&as_mysql_cluster_list_lock);
	itr = list_iterator_create(use_cluster_list);
	while ((cluster_name = list_next(itr))) {
		int rc;
		if ((rc = _cluster_get_assocs(mysql_conn, &user, assoc_cond,
					      cluster_name, tmp, extra,
					      is_admin, assoc_list))
		    != SLURM_SUCCESS) {
			FREE_NULL_LIST(assoc_list);
			assoc_list = NULL;
			break;
		}
	}
	list_iterator_destroy(itr);
	if (use_cluster_list == as_mysql_cluster_list)
		slurm_mutex_unlock(&as_mysql_cluster_list_lock);
	xfree(tmp);
	xfree(extra);

	//END_TIMER2("get_assocs");
	return assoc_list;
}

extern int as_mysql_reset_lft_rgt(mysql_conn_t *mysql_conn, uid_t uid,
				  List cluster_list)
{
	List assoc_list = NULL;
	ListIterator itr = NULL, assoc_itr;
	int i=0, is_admin=1;
	slurmdb_user_rec_t user;
	char *query = NULL, *tmp = NULL, *cluster_name = NULL;
	slurmdb_assoc_cond_t assoc_cond;
	slurmdb_assoc_rec_t *assoc_rec;
	int rc = SLURM_SUCCESS;
	slurmdb_update_object_t *update_object;
	slurmdb_update_type_t type;
	List use_cluster_list = as_mysql_cluster_list;

	info("Resetting lft and rgt's called");
	/* This is not safe if ran during the middle of a slurmdbd
	 * run since we can not lock as_mysql_cluster_list_lock when
	 * no list is given.  At the time of this writing the function
	 * was only called at the beginning of the DBD.  If this ever
	 * changes please make the appropriate changes to allow empty lists.
	 */
	if (cluster_list && list_count(cluster_list))
		use_cluster_list = cluster_list;
	/* else */
	/* 	slurm_mutex_lock(&as_mysql_cluster_list_lock); */

	memset(&assoc_cond, 0, sizeof(slurmdb_assoc_cond_t));

	xfree(tmp);
	xstrfmtcat(tmp, "t1.%s", assoc_req_inx[i]);
	for (i=1; i<ASSOC_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", t1.%s", assoc_req_inx[i]);
	}

	memset(&user, 0, sizeof(slurmdb_user_rec_t));
	user.uid = uid;

	itr = list_iterator_create(use_cluster_list);
	while ((cluster_name = list_next(itr))) {
		time_t now = time(NULL);
		DEF_TIMERS;
		START_TIMER;
		info("Resetting cluster %s", cluster_name);
		assoc_list = list_create(slurmdb_destroy_assoc_rec);
		/* set this up to get the associations without parent_limits */
		assoc_cond.without_parent_limits = 1;

		/* Get the associations for the cluster that needs to
		 * somehow got lft and rgt's messed up. */
		if ((rc = _cluster_get_assocs(mysql_conn, &user, &assoc_cond,
					      cluster_name, tmp,
					      " deleted=1 || deleted=0",
					      is_admin, assoc_list))
		    != SLURM_SUCCESS) {
			info("fail for cluster %s", cluster_name);
			FREE_NULL_LIST(assoc_list);
			continue;
		}

		if (!list_count(assoc_list)) {
			info("Cluster %s has no associations, nothing to reset",
			     cluster_name);
			FREE_NULL_LIST(assoc_list);
			continue;
		}

		slurmdb_sort_hierarchical_assoc_list(assoc_list, false);
		info("Got current associations for cluster %s", cluster_name);
		/* Set the cluster name to the tmp name and remove qos */
		assoc_itr = list_iterator_create(assoc_list);
		while ((assoc_rec = list_next(assoc_itr))) {
			if (assoc_rec->id == 1) {
				/* Remove root association as we will make it
				 * manually in the next step.
				 */
				list_delete_item(assoc_itr);
				continue;
			}
			xfree(assoc_rec->cluster);
			assoc_rec->cluster = xstrdup(tmp_cluster_name);
			/* Remove the qos_list just to simplify things
			 * since we really want to just simplify
			 * things.
			 */
			FREE_NULL_LIST(assoc_rec->qos_list);
		}
		list_iterator_destroy(assoc_itr);

		/* What we are going to do now is add all the
		 * associations to a tmp cluster this will recreate
		 * the lft and rgt hierarchy.  Then when this is done
		 * we will move those lft/rgts back over to the
		 * original cluster based on the same id_assoc's.
		 * After that we will send these new objects out to
		 * the slurmctld to use.
		 */

		create_cluster_assoc_table(mysql_conn, tmp_cluster_name);

		/* We must add the root association here to make it so
		 * the adds work afterwards.
		 */
		xstrfmtcat(query,
			   "insert into \"%s_%s\" "
			   "(creation_time, mod_time, acct, lft, rgt) "
			   "values (%ld, %ld, 'root', 1, 2) "
			   "on duplicate key update deleted=0, "
			   "id_assoc=LAST_INSERT_ID(id_assoc), mod_time=%ld;",
			   tmp_cluster_name, assoc_table, now, now, now);

		if (debug_flags & DEBUG_FLAG_DB_ASSOC)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);

		rc = mysql_db_query(mysql_conn, query);
		xfree(query);

		if (rc != SLURM_SUCCESS) {
			error("Couldn't add cluster root assoc");
			break;
		}

		info("Redoing the hierarchy in a temporary table");
		if (as_mysql_add_assocs(mysql_conn, uid, assoc_list) !=
		    SLURM_SUCCESS)
			goto endit;

		/* Since the list we now have has deleted
		 * items in it we need to get a list with just
		 * the current items instead
		 */
		list_flush(assoc_list);

		info("Resetting cluster with correct lft and rgt's");
		/* Update the normal table with the correct lft and rgts */
		query = xstrdup_printf(
			"update \"%s_%s\" t1 left outer join \"%s_%s\" t2 on "
			"t1.id_assoc = t2.id_assoc set t1.lft = t2.lft, "
			"t1.rgt = t2.rgt, t1.mod_time = t2.mod_time;",
			cluster_name, assoc_table,
			tmp_cluster_name, assoc_table);

		if (debug_flags & DEBUG_FLAG_DB_ASSOC)
			DB_DEBUG(mysql_conn->conn, "query\n%s", query);

		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
		if (rc != SLURM_SUCCESS)
			error("Couldn't fix assocs");

		/* Now we will remove the adds that happened and
		 * replace them with mod's so we can push the new
		 * lft's to the cluster.
		 */
		type = SLURMDB_ADD_ASSOC;
		assoc_itr = list_iterator_create(mysql_conn->update_list);
		while ((update_object = list_next(assoc_itr))) {
			if (update_object->type == type) {
				list_delete_item(assoc_itr);
				break;
			}
		}
		list_iterator_destroy(assoc_itr);

		/* Make the mod assoc update_object if it doesn't exist */
		type = SLURMDB_MODIFY_ASSOC;
		if (!(update_object = list_find_first(
			      mysql_conn->update_list,
			      slurmdb_find_update_object_in_list,
			      &type))) {
			update_object = xmalloc(
				sizeof(slurmdb_update_object_t));
			list_append(mysql_conn->update_list, update_object);
			update_object->type = type;
			update_object->objects = list_create(
				slurmdb_destroy_assoc_rec);
		}

		/* set this up to get the associations with parent_limits */
		assoc_cond.without_parent_limits = 0;
		if ((rc = _cluster_get_assocs(mysql_conn, &user, &assoc_cond,
					      cluster_name, tmp,
					      " deleted=0",
					      is_admin, assoc_list))
		    != SLURM_SUCCESS) {
			goto endit;
		}
		list_transfer(update_object->objects, assoc_list);
	endit:
		FREE_NULL_LIST(assoc_list);
		/* Get rid of the temporary table. */
		query = xstrdup_printf("drop table \"%s_%s\";",
				       tmp_cluster_name, assoc_table);
		rc = mysql_db_query(mysql_conn, query);
		xfree(query);
		if (rc != SLURM_SUCCESS) {
			error("problem with update query");
			rc = SLURM_ERROR;
		}
		END_TIMER;
		info("resetting took %s", TIME_STR);
	}
	list_iterator_destroy(itr);

	xfree(tmp);

	/* if (use_cluster_list == as_mysql_cluster_list) */
	/* 	slurm_mutex_unlock(&as_mysql_cluster_list_lock); */

	return rc;
}
