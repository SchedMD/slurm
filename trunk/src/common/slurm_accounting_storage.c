/*****************************************************************************\
 *  slurm_accounting_storage.c - account storage plugin wrapper.
 *
 *  $Id: slurm_accounting_storage.c 10744 2007-01-11 20:09:18Z da $
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifndef   _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#include <pthread.h>
#include <string.h>

#include "src/common/list.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"
#include "src/sacctmgr/sacctmgr.h"
#include "src/common/slurm_strcasestr.h"

/*
 * Local data
 */

typedef struct slurm_acct_storage_ops {
	void *(*get_conn)          (bool make_agent, int conn_num, 
				    bool rollback);
	int  (*close_conn)         (void **db_conn);
	int  (*commit)             (void *db_conn, bool commit);
	int  (*add_users)          (void *db_conn, uint32_t uid,
				    List user_list);
	int  (*add_coord)          (void *db_conn, uint32_t uid,
				    List acct_list,
				    acct_user_cond_t *user_cond);
	int  (*add_accts)          (void *db_conn, uint32_t uid,
				    List acct_list);
	int  (*add_clusters)       (void *db_conn, uint32_t uid,
				    List cluster_list);
	int  (*add_associations)   (void *db_conn, uint32_t uid,
				    List association_list);
	int  (*add_qos)            (void *db_conn, uint32_t uid,
				    List qos_list);
	int  (*add_wckeys)         (void *db_conn, uint32_t uid,
				    List wckey_list);
	int  (*add_reservation)    (void *db_conn,
				    acct_reservation_rec_t *resv);
	List (*modify_users)       (void *db_conn, uint32_t uid,
				    acct_user_cond_t *user_cond,
				    acct_user_rec_t *user);
	List (*modify_accts)       (void *db_conn, uint32_t uid,
				    acct_account_cond_t *acct_cond,
				    acct_account_rec_t *acct);
	List (*modify_clusters)    (void *db_conn, uint32_t uid,
				    acct_cluster_cond_t *cluster_cond,
				    acct_cluster_rec_t *cluster);
	List (*modify_associations)(void *db_conn, uint32_t uid,
				    acct_association_cond_t *assoc_cond,
				    acct_association_rec_t *assoc);
	List (*modify_qos)         (void *db_conn, uint32_t uid,
				    acct_qos_cond_t *qos_cond,
				    acct_qos_rec_t *qos);
	List (*modify_wckeys)      (void *db_conn, uint32_t uid,
				    acct_wckey_cond_t *wckey_cond,
				    acct_wckey_rec_t *wckey);
	int  (*modify_reservation) (void *db_conn,
				    acct_reservation_rec_t *resv);
	List (*remove_users)       (void *db_conn, uint32_t uid,
				    acct_user_cond_t *user_cond);
	List (*remove_coord)       (void *db_conn, uint32_t uid,
				    List acct_list,
				    acct_user_cond_t *user_cond);
	List (*remove_accts)       (void *db_conn, uint32_t uid,
				    acct_account_cond_t *acct_cond);
	List (*remove_clusters)    (void *db_conn, uint32_t uid,
				    acct_cluster_cond_t *cluster_cond);
	List (*remove_associations)(void *db_conn, uint32_t uid,
				    acct_association_cond_t *assoc_cond);
	List (*remove_qos)         (void *db_conn, uint32_t uid,
				    acct_qos_cond_t *qos_cond);
	List (*remove_wckeys)      (void *db_conn, uint32_t uid,
				    acct_wckey_cond_t *wckey_cond);
	int  (*remove_reservation) (void *db_conn,
				    acct_reservation_rec_t *resv);
	List (*get_users)          (void *db_conn, uint32_t uid,
				    acct_user_cond_t *user_cond);
	List (*get_accts)          (void *db_conn, uint32_t uid,
				    acct_account_cond_t *acct_cond);
	List (*get_clusters)       (void *db_conn, uint32_t uid,
				    acct_cluster_cond_t *cluster_cond);
	List (*get_config)         (void *db_conn);
	List (*get_associations)   (void *db_conn, uint32_t uid,
				    acct_association_cond_t *assoc_cond);
	List (*get_problems)       (void *db_conn, uint32_t uid,
				    acct_association_cond_t *assoc_cond);
	List (*get_qos)            (void *db_conn, uint32_t uid,
				    acct_qos_cond_t *qos_cond);
	List (*get_wckeys)         (void *db_conn, uint32_t uid,
				    acct_wckey_cond_t *wckey_cond);
	List (*get_resvs)          (void *db_conn, uint32_t uid,
				    acct_reservation_cond_t *resv_cond);
	List (*get_txn)            (void *db_conn, uint32_t uid,
				    acct_txn_cond_t *txn_cond);
	int  (*get_usage)          (void *db_conn, uint32_t uid,
				    void *in, int type,
				    time_t start, 
				    time_t end);
	int (*roll_usage)          (void *db_conn, 
				    time_t sent_start, time_t sent_end,
				    uint16_t archive_data);
	int  (*node_down)          (void *db_conn,
				    char *cluster,
				    struct node_record *node_ptr,
				    time_t event_time,
				    char *reason);
	int  (*node_up)            (void *db_conn,
				    char *cluster,
				    struct node_record *node_ptr,
				    time_t event_time);
	int  (*cluster_procs)      (void *db_conn,
				    char *cluster, char *cluster_nodes,
				    uint32_t procs, time_t event_time);
	int  (*c_get_usage)        (void *db_conn, uint32_t uid,
				    void *cluster_rec, int type,
				    time_t start, time_t end);
	int  (*register_ctld)      (void *db_conn, char *cluster,
				    uint16_t port);
	int  (*job_start)          (void *db_conn, char *cluster_name,
				    struct job_record *job_ptr);
	int  (*job_complete)       (void *db_conn,
				    struct job_record *job_ptr);
	int  (*step_start)         (void *db_conn,
				    struct step_record *step_ptr);
	int  (*step_complete)      (void *db_conn,
				    struct step_record *step_ptr);
	int  (*job_suspend)        (void *db_conn,
				    struct job_record *job_ptr);
	List (*get_jobs_cond)      (void *db_conn, uint32_t uid,
				    acct_job_cond_t *job_cond);	
	int (*archive_dump)        (void *db_conn,
				    acct_archive_cond_t *arch_cond);	
	int (*archive_load)        (void *db_conn,
				    acct_archive_rec_t *arch_rec);	
	int (*update_shares_used)  (void *db_conn,
				    List shares_used);
	int (*flush_jobs)          (void *db_conn,
				    char *cluster,
				    time_t event_time);
} slurm_acct_storage_ops_t;

typedef struct slurm_acct_storage_context {
	char	       	*acct_storage_type;
	plugrack_t     	plugin_list;
	plugin_handle_t	cur_plugin;
	int		acct_storage_errno;
	slurm_acct_storage_ops_t ops;
} slurm_acct_storage_context_t;

static slurm_acct_storage_context_t * g_acct_storage_context = NULL;
static pthread_mutex_t		g_acct_storage_context_lock = 
	PTHREAD_MUTEX_INITIALIZER;

/*
 * Local functions
 */
static slurm_acct_storage_ops_t *_acct_storage_get_ops(
	slurm_acct_storage_context_t *c);
static slurm_acct_storage_context_t *_acct_storage_context_create(
	const char *acct_storage_type);
static int _acct_storage_context_destroy(
	slurm_acct_storage_context_t *c);

/*
 * Locate and load the appropriate plugin
 */
static slurm_acct_storage_ops_t * _acct_storage_get_ops(
	slurm_acct_storage_context_t *c)
{
	/*
	 * Must be synchronized with slurm_acct_storage_ops_t above.
	 */
	static const char *syms[] = {
		"acct_storage_p_get_connection",
		"acct_storage_p_close_connection",
		"acct_storage_p_commit",
		"acct_storage_p_add_users",
		"acct_storage_p_add_coord",
		"acct_storage_p_add_accts",
		"acct_storage_p_add_clusters",
		"acct_storage_p_add_associations",
		"acct_storage_p_add_qos",
		"acct_storage_p_add_wckeys",
		"acct_storage_p_add_reservation",
		"acct_storage_p_modify_users",
		"acct_storage_p_modify_accounts",
		"acct_storage_p_modify_clusters",
		"acct_storage_p_modify_associations",
		"acct_storage_p_modify_qos",
		"acct_storage_p_modify_wckeys",
		"acct_storage_p_modify_reservation",
		"acct_storage_p_remove_users",
		"acct_storage_p_remove_coord",
		"acct_storage_p_remove_accts",
		"acct_storage_p_remove_clusters",
		"acct_storage_p_remove_associations",
		"acct_storage_p_remove_qos",
		"acct_storage_p_remove_wckeys",
		"acct_storage_p_remove_reservation",
		"acct_storage_p_get_users",
		"acct_storage_p_get_accts",
		"acct_storage_p_get_clusters",
		"acct_storage_p_get_config",
		"acct_storage_p_get_associations",
		"acct_storage_p_get_problems",
		"acct_storage_p_get_qos",
		"acct_storage_p_get_wckeys",
		"acct_storage_p_get_reservations",
		"acct_storage_p_get_txn",
		"acct_storage_p_get_usage",
		"acct_storage_p_roll_usage",
		"clusteracct_storage_p_node_down",
		"clusteracct_storage_p_node_up",
		"clusteracct_storage_p_cluster_procs",
		"clusteracct_storage_p_get_usage",
		"clusteracct_storage_p_register_ctld",
		"jobacct_storage_p_job_start",
		"jobacct_storage_p_job_complete",
		"jobacct_storage_p_step_start",
		"jobacct_storage_p_step_complete",
		"jobacct_storage_p_suspend",
		"jobacct_storage_p_get_jobs_cond",
		"jobacct_storage_p_archive",
		"jobacct_storage_p_archive_load",
		"acct_storage_p_update_shares_used",
		"acct_storage_p_flush_jobs_on_cluster"
	};
	int n_syms = sizeof( syms ) / sizeof( char * );

	/* Find the correct plugin. */
        c->cur_plugin = plugin_load_and_link(c->acct_storage_type, n_syms, syms,
					     (void **) &c->ops);
        if ( c->cur_plugin != PLUGIN_INVALID_HANDLE ) 
        	return &c->ops;

	error("Couldn't find the specified plugin name for %s "
	      "looking at all files",
	      c->acct_storage_type);

	/* Get plugin list. */
	if ( c->plugin_list == NULL ) {
		char *plugin_dir;
		c->plugin_list = plugrack_create();
		if ( c->plugin_list == NULL ) {
			error( "cannot create plugin manager" );
			return NULL;
		}
		plugrack_set_major_type( c->plugin_list, "accounting_storage" );
		plugrack_set_paranoia( c->plugin_list,
				       PLUGRACK_PARANOIA_NONE,
				       0 );
		plugin_dir = slurm_get_plugin_dir();
		plugrack_read_dir( c->plugin_list, plugin_dir );
		xfree(plugin_dir);
	}

	c->cur_plugin = plugrack_use_by_type( c->plugin_list,
					      c->acct_storage_type );
	if ( c->cur_plugin == PLUGIN_INVALID_HANDLE ) {
		error( "cannot find accounting_storage plugin for %s", 
		       c->acct_storage_type );
		return NULL;
	}

	/* Dereference the API. */
	if ( plugin_get_syms( c->cur_plugin,
			      n_syms,
			      syms,
			      (void **) &c->ops ) < n_syms ) {
		error( "incomplete acct_storage plugin detected" );
		return NULL;
	}

	return &c->ops;
}

/*
 * Create a acct_storage context
 */
static slurm_acct_storage_context_t *_acct_storage_context_create(
	const char *acct_storage_type)
{
	slurm_acct_storage_context_t *c;

	if ( acct_storage_type == NULL ) {
		debug3( "_acct_storage_context_create: no uler type" );
		return NULL;
	}

	c = xmalloc( sizeof( slurm_acct_storage_context_t ) );
	c->acct_storage_type	= xstrdup( acct_storage_type );
	c->plugin_list	= NULL;
	c->cur_plugin	= PLUGIN_INVALID_HANDLE;
	c->acct_storage_errno	= SLURM_SUCCESS;

	return c;
}

/*
 * Destroy a acct_storage context
 */
static int _acct_storage_context_destroy(slurm_acct_storage_context_t *c)
{
	int rc = SLURM_SUCCESS;
	/*
	 * Must check return code here because plugins might still
	 * be loaded and active.
	 */
	if ( c->plugin_list ) {
		if ( plugrack_destroy( c->plugin_list ) != SLURM_SUCCESS ) {
			rc = SLURM_ERROR;
		}
	} else {
		plugin_unload(c->cur_plugin);
	}

	xfree( c->acct_storage_type );
	xfree( c );

	return rc;
}

/* 
 * Comparator used for sorting immediate childern of acct_hierarchical_recs
 * 
 * returns: -1: assoc_a > assoc_b   0: assoc_a == assoc_b   1: assoc_a < assoc_b
 * 
 */

static int _sort_childern_list(acct_hierarchical_rec_t *assoc_a,
			       acct_hierarchical_rec_t *assoc_b)
{
	int diff = 0;

	/* first just check the lfts and rgts if a lft is inside of the
	 * others lft and rgt just return it is less
	 */ 
	if(assoc_a->assoc->lft > assoc_b->assoc->lft 
	   && assoc_a->assoc->lft < assoc_b->assoc->rgt)
		return 1;

	/* check to see if this is a user association or an account.
	 * We want the accounts at the bottom 
	 */
	if(assoc_a->assoc->user && !assoc_b->assoc->user)
		return -1;
	else if(!assoc_a->assoc->user && assoc_b->assoc->user)
		return 1;

	diff = strcmp(assoc_a->sort_name, assoc_b->sort_name);

	if (diff < 0)
		return -1;
	else if (diff > 0)
		return 1;
	
	return 0;

}

static int _sort_acct_hierarchical_rec_list(List acct_hierarchical_rec_list)
{
	acct_hierarchical_rec_t *acct_hierarchical_rec = NULL;
	ListIterator itr;

	if(!list_count(acct_hierarchical_rec_list))
		return SLURM_SUCCESS;

	list_sort(acct_hierarchical_rec_list, (ListCmpF)_sort_childern_list);

	itr = list_iterator_create(acct_hierarchical_rec_list);
	while((acct_hierarchical_rec = list_next(itr))) {
		if(list_count(acct_hierarchical_rec->childern))
			_sort_acct_hierarchical_rec_list(
				acct_hierarchical_rec->childern);
	}
	list_iterator_destroy(itr);

	return SLURM_SUCCESS;
}

static int _append_hierarchical_childern_ret_list(
	List ret_list, List acct_hierarchical_rec_list)
{
	acct_hierarchical_rec_t *acct_hierarchical_rec = NULL;
	ListIterator itr;

	if(!ret_list)
		return SLURM_ERROR;

	if(!list_count(acct_hierarchical_rec_list))
		return SLURM_SUCCESS;

	itr = list_iterator_create(acct_hierarchical_rec_list);
	while((acct_hierarchical_rec = list_next(itr))) {
		list_append(ret_list, acct_hierarchical_rec->assoc);

		if(list_count(acct_hierarchical_rec->childern)) 
			_append_hierarchical_childern_ret_list(
				ret_list, acct_hierarchical_rec->childern);
	}
	list_iterator_destroy(itr);

	return SLURM_SUCCESS;
}

extern void destroy_acct_user_rec(void *object)
{
	acct_user_rec_t *acct_user = (acct_user_rec_t *)object;

	if(acct_user) {
		if(acct_user->assoc_list)
			list_destroy(acct_user->assoc_list);
		if(acct_user->coord_accts)
			list_destroy(acct_user->coord_accts);
		xfree(acct_user->default_acct);
		xfree(acct_user->default_wckey);
		xfree(acct_user->name);
		if(acct_user->wckey_list)
			list_destroy(acct_user->wckey_list);
		xfree(acct_user);
	}
}

extern void destroy_acct_account_rec(void *object)
{
	acct_account_rec_t *acct_account =
		(acct_account_rec_t *)object;

	if(acct_account) {
		if(acct_account->assoc_list)
			list_destroy(acct_account->assoc_list);
		if(acct_account->coordinators)
			list_destroy(acct_account->coordinators);
		xfree(acct_account->description);
		xfree(acct_account->name);
		xfree(acct_account->organization);
		xfree(acct_account);
	}
}

extern void destroy_acct_coord_rec(void *object)
{
	acct_coord_rec_t *acct_coord =
		(acct_coord_rec_t *)object;

	if(acct_coord) {
		xfree(acct_coord->name);
		xfree(acct_coord);
	}
}

extern void destroy_cluster_accounting_rec(void *object)
{
	cluster_accounting_rec_t *clusteracct_rec =
		(cluster_accounting_rec_t *)object;

	if(clusteracct_rec) {
		xfree(clusteracct_rec);
	}
}

extern void destroy_acct_cluster_rec(void *object)
{
	acct_cluster_rec_t *acct_cluster =
		(acct_cluster_rec_t *)object;

	if(acct_cluster) {
		if(acct_cluster->accounting_list)
			list_destroy(acct_cluster->accounting_list);
		xfree(acct_cluster->control_host);
		xfree(acct_cluster->name);
		xfree(acct_cluster->nodes);
		destroy_acct_association_rec(acct_cluster->root_assoc);
		xfree(acct_cluster);
	}
}

extern void destroy_acct_accounting_rec(void *object)
{
	acct_accounting_rec_t *acct_accounting =
		(acct_accounting_rec_t *)object;

	if(acct_accounting) {
		xfree(acct_accounting);
	}
}

extern void destroy_acct_association_rec(void *object)
{
	acct_association_rec_t *acct_association = 
		(acct_association_rec_t *)object;

	if(acct_association) {
		if(acct_association->accounting_list)
			list_destroy(acct_association->accounting_list);
		xfree(acct_association->acct);
		if(acct_association->childern_list)
			list_destroy(acct_association->childern_list);
		xfree(acct_association->cluster);
		xfree(acct_association->parent_acct);
		xfree(acct_association->partition);
		if(acct_association->qos_list)
			list_destroy(acct_association->qos_list);
		xfree(acct_association->user);
		FREE_NULL_BITMAP(acct_association->valid_qos);
		xfree(acct_association);
	}
}

extern void destroy_acct_qos_rec(void *object)
{
	acct_qos_rec_t *acct_qos = (acct_qos_rec_t *)object;
	if(acct_qos) {
		xfree(acct_qos->description);
		xfree(acct_qos->job_flags);
		if(acct_qos->job_list)
			list_destroy(acct_qos->job_list);
		xfree(acct_qos->name);
		FREE_NULL_BITMAP(acct_qos->preempt_bitstr);
		if(acct_qos->preempt_list)
			list_destroy(acct_qos->preempt_list);
		if(acct_qos->user_limit_list)
			list_destroy(acct_qos->user_limit_list);
		xfree(acct_qos);
	}
}

extern void destroy_acct_reservation_rec(void *object)
{
	acct_reservation_rec_t *acct_resv = (acct_reservation_rec_t *)object;
	if(acct_resv) {
		xfree(acct_resv->assocs);
		xfree(acct_resv->cluster);
		xfree(acct_resv->name);
		xfree(acct_resv->nodes);
		xfree(acct_resv->node_inx);
		xfree(acct_resv);
	}
}

extern void destroy_acct_txn_rec(void *object)
{
	acct_txn_rec_t *acct_txn = (acct_txn_rec_t *)object;
	if(acct_txn) {
		xfree(acct_txn->accts);
		xfree(acct_txn->actor_name);
		xfree(acct_txn->clusters);
		xfree(acct_txn->set_info);
		xfree(acct_txn->users);
		xfree(acct_txn->where_query);
		xfree(acct_txn);
	}
}

extern void destroy_acct_wckey_rec(void *object)
{
	acct_wckey_rec_t *wckey = (acct_wckey_rec_t *)object;

	if(wckey) {
		if(wckey->accounting_list)
			list_destroy(wckey->accounting_list);
		xfree(wckey->cluster);
		xfree(wckey->name);
		xfree(wckey->user);
		xfree(wckey);
	}
}

extern void destroy_acct_archive_rec(void *object)
{
	acct_archive_rec_t *arch_rec = (acct_archive_rec_t *)object;
	
	if(arch_rec) {
		xfree(arch_rec->archive_file);
		xfree(arch_rec->insert);
		xfree(arch_rec);
	}
}

extern void destroy_acct_user_cond(void *object)
{
	acct_user_cond_t *acct_user = (acct_user_cond_t *)object;

	if(acct_user) {
		destroy_acct_association_cond(acct_user->assoc_cond);
		if(acct_user->def_acct_list)
			list_destroy(acct_user->def_acct_list);
		if(acct_user->def_wckey_list)
			list_destroy(acct_user->def_wckey_list);
		xfree(acct_user);
	}
}

extern void destroy_acct_account_cond(void *object)
{
	acct_account_cond_t *acct_account =
		(acct_account_cond_t *)object;

	if(acct_account) {
		destroy_acct_association_cond(acct_account->assoc_cond);
		if(acct_account->description_list)
			list_destroy(acct_account->description_list);
		if(acct_account->organization_list)
			list_destroy(acct_account->organization_list);
		xfree(acct_account);
	}
}

extern void destroy_acct_cluster_cond(void *object)
{
	acct_cluster_cond_t *acct_cluster =
		(acct_cluster_cond_t *)object;

	if(acct_cluster) {
		if(acct_cluster->cluster_list)
			list_destroy(acct_cluster->cluster_list);
		xfree(acct_cluster);
	}
}

extern void destroy_acct_association_cond(void *object)
{
	acct_association_cond_t *acct_association = 
		(acct_association_cond_t *)object;

	if(acct_association) {
		if(acct_association->acct_list)
			list_destroy(acct_association->acct_list);
		if(acct_association->cluster_list)
			list_destroy(acct_association->cluster_list);

		if(acct_association->fairshare_list)
			list_destroy(acct_association->fairshare_list);

		if(acct_association->grp_cpu_mins_list)
			list_destroy(acct_association->grp_cpu_mins_list);
		if(acct_association->grp_cpus_list)
			list_destroy(acct_association->grp_cpus_list);
		if(acct_association->grp_jobs_list)
			list_destroy(acct_association->grp_jobs_list);
		if(acct_association->grp_nodes_list)
			list_destroy(acct_association->grp_nodes_list);
		if(acct_association->grp_submit_jobs_list)
			list_destroy(acct_association->grp_submit_jobs_list);
		if(acct_association->grp_wall_list)
			list_destroy(acct_association->grp_wall_list);

		if(acct_association->id_list)
			list_destroy(acct_association->id_list);

		if(acct_association->max_cpu_mins_pj_list)
			list_destroy(acct_association->max_cpu_mins_pj_list);
		if(acct_association->max_cpus_pj_list)
			list_destroy(acct_association->max_cpus_pj_list);
		if(acct_association->max_jobs_list)
			list_destroy(acct_association->max_jobs_list);
		if(acct_association->max_nodes_pj_list)
			list_destroy(acct_association->max_nodes_pj_list);
		if(acct_association->max_submit_jobs_list)
			list_destroy(acct_association->max_submit_jobs_list);
		if(acct_association->max_wall_pj_list)
			list_destroy(acct_association->max_wall_pj_list);

		if(acct_association->partition_list)
			list_destroy(acct_association->partition_list);

		if(acct_association->parent_acct_list)
			list_destroy(acct_association->parent_acct_list);

		if(acct_association->qos_list)
			list_destroy(acct_association->qos_list);
		if(acct_association->user_list)
			list_destroy(acct_association->user_list);
		xfree(acct_association);
	}
}

extern void destroy_acct_job_cond(void *object)
{
	acct_job_cond_t *job_cond = 
		(acct_job_cond_t *)object;

	if(job_cond) {
		if(job_cond->acct_list)
			list_destroy(job_cond->acct_list);
		if(job_cond->associd_list)
			list_destroy(job_cond->associd_list);
		if(job_cond->cluster_list)
			list_destroy(job_cond->cluster_list);
		if(job_cond->groupid_list)
			list_destroy(job_cond->groupid_list);
		if(job_cond->partition_list)
			list_destroy(job_cond->partition_list);
		if(job_cond->resv_list)
			list_destroy(job_cond->resv_list);
		if(job_cond->resvid_list)
			list_destroy(job_cond->resvid_list);
		if(job_cond->step_list)
			list_destroy(job_cond->step_list);
		if(job_cond->state_list)
			list_destroy(job_cond->state_list);
		xfree(job_cond->used_nodes);
		if(job_cond->userid_list)
			list_destroy(job_cond->userid_list);
		if(job_cond->wckey_list)
			list_destroy(job_cond->wckey_list);
		xfree(job_cond);
	}
}

extern void destroy_acct_qos_cond(void *object)
{
	acct_qos_cond_t *acct_qos = (acct_qos_cond_t *)object;
	if(acct_qos) {
		if(acct_qos->id_list)
			list_destroy(acct_qos->id_list);
		if(acct_qos->name_list)
			list_destroy(acct_qos->name_list);
		xfree(acct_qos);
	}
}

extern void destroy_acct_reservation_cond(void *object)
{
	acct_reservation_cond_t *acct_resv = (acct_reservation_cond_t *)object;
	if(acct_resv) {
		if(acct_resv->cluster_list) 
			list_destroy(acct_resv->cluster_list);
		if(acct_resv->id_list)
			list_destroy(acct_resv->id_list);
		if(acct_resv->name_list)
			list_destroy(acct_resv->name_list);
		xfree(acct_resv->nodes);
		xfree(acct_resv);
	}
}

extern void destroy_acct_txn_cond(void *object)
{
	acct_txn_cond_t *acct_txn = (acct_txn_cond_t *)object;
	if(acct_txn) {
		if(acct_txn->acct_list)
			list_destroy(acct_txn->acct_list);
		if(acct_txn->action_list)
			list_destroy(acct_txn->action_list);
		if(acct_txn->actor_list)
			list_destroy(acct_txn->actor_list);
		if(acct_txn->cluster_list)
			list_destroy(acct_txn->cluster_list);
		if(acct_txn->id_list)
			list_destroy(acct_txn->id_list);
		if(acct_txn->info_list)
			list_destroy(acct_txn->info_list);
		if(acct_txn->name_list)
			list_destroy(acct_txn->name_list);
		if(acct_txn->user_list)
			list_destroy(acct_txn->user_list);
		xfree(acct_txn);
	}
}

extern void destroy_acct_wckey_cond(void *object)
{
	acct_wckey_cond_t *wckey = (acct_wckey_cond_t *)object;

	if(wckey) {
		if(wckey->cluster_list)
			list_destroy(wckey->cluster_list);
		if(wckey->id_list)
			list_destroy(wckey->id_list);
		if(wckey->name_list)
			list_destroy(wckey->name_list);
		if(wckey->user_list)
			list_destroy(wckey->user_list);
		xfree(wckey);
	}
}

extern void destroy_acct_archive_cond(void *object)
{
	acct_archive_cond_t *arch_cond = (acct_archive_cond_t *)object;

	if(arch_cond) {
		xfree(arch_cond->archive_dir);
		xfree(arch_cond->archive_script);
		destroy_acct_job_cond(arch_cond->job_cond);
		xfree(arch_cond);

	}
}

extern void destroy_acct_update_object(void *object)
{
	acct_update_object_t *acct_update = 
		(acct_update_object_t *) object;

	if(acct_update) {
		if(acct_update->objects) 
			list_destroy(acct_update->objects);
		
		xfree(acct_update);
	}
}

extern void destroy_acct_used_limits(void *object)
{
	acct_used_limits_t *acct_used_limits = (acct_used_limits_t *)object;

	if(acct_used_limits) {
		xfree(acct_used_limits);
	}
}

extern void destroy_update_shares_rec(void *object)
{
	xfree(object);
}

extern void destroy_acct_print_tree(void *object)
{
	acct_print_tree_t *acct_print_tree = (acct_print_tree_t *)object;

	if(acct_print_tree) {
		xfree(acct_print_tree->name);
		xfree(acct_print_tree->print_name);
		xfree(acct_print_tree->spaces);
		xfree(acct_print_tree);
	}
}

extern void destroy_acct_hierarchical_rec(void *object)
{
	/* Most of this is pointers to something else that will be
	 * destroyed elsewhere.
	 */
	acct_hierarchical_rec_t *acct_hierarchical_rec = 
		(acct_hierarchical_rec_t *)object;
	if(acct_hierarchical_rec) {
		if(acct_hierarchical_rec->childern) {
			list_destroy(acct_hierarchical_rec->childern);
		}
		xfree(acct_hierarchical_rec);
	}
}

extern void init_acct_association_rec(acct_association_rec_t *assoc)
{
	if(!assoc)
		return;

	memset(assoc, 0, sizeof(acct_association_rec_t));

	assoc->grp_cpu_mins = NO_VAL;
	assoc->grp_cpus = NO_VAL;
	assoc->grp_jobs = NO_VAL;
	assoc->grp_nodes = NO_VAL;
	assoc->grp_submit_jobs = NO_VAL;
	assoc->grp_wall = NO_VAL;

	assoc->level_shares = NO_VAL;

	assoc->max_cpu_mins_pj = NO_VAL;
	assoc->max_cpus_pj = NO_VAL;
	assoc->max_jobs = NO_VAL;
	assoc->max_nodes_pj = NO_VAL;
	assoc->max_submit_jobs = NO_VAL;
	assoc->max_wall_pj = NO_VAL;

	assoc->shares_norm = (double)NO_VAL;
	assoc->shares_raw = NO_VAL;

	assoc->usage_efctv = 0;
	assoc->usage_norm = (long double)NO_VAL;
	assoc->usage_raw = 0;
}

extern void init_acct_qos_rec(acct_qos_rec_t *qos)
{
	if(!qos)
		return;

	memset(qos, 0, sizeof(acct_qos_rec_t));

	qos->priority = NO_VAL;

	qos->grp_cpu_mins = NO_VAL;
	qos->grp_cpus = NO_VAL;
	qos->grp_jobs = NO_VAL;
	qos->grp_nodes = NO_VAL;
	qos->grp_submit_jobs = NO_VAL;
	qos->grp_wall = NO_VAL;

	qos->max_cpu_mins_pu = NO_VAL;
	qos->max_cpus_pu = NO_VAL;
	qos->max_jobs_pu = NO_VAL;
	qos->max_nodes_pu = NO_VAL;
	qos->max_submit_jobs_pu = NO_VAL;
	qos->max_wall_pu = NO_VAL;

	qos->usage_factor = NO_VAL;
}

/****************************************************************************\
 * Pack and unpack data structures
\****************************************************************************/
extern void pack_acct_user_rec(void *in, uint16_t rpc_version, Buf buffer)
{
	ListIterator itr = NULL;
	acct_user_rec_t *object = (acct_user_rec_t *)in;
	uint32_t count = NO_VAL;
	acct_coord_rec_t *coord = NULL;
	acct_association_rec_t *assoc = NULL;
	acct_wckey_rec_t *wckey = NULL;

	if(rpc_version >= 4) {
		if(!object) {
			pack16(0, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			packnull(buffer);
			packnull(buffer);
			packnull(buffer);
			pack32(0, buffer);
			pack32(NO_VAL, buffer);
			return;
		}
 
		pack16(object->admin_level, buffer);

		if(object->assoc_list)
			count = list_count(object->assoc_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->assoc_list);
			while((assoc = list_next(itr))) {
				pack_acct_association_rec(assoc, rpc_version,
							  buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->coord_accts)
			count = list_count(object->coord_accts);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->coord_accts);
			while((coord = list_next(itr))) {
				pack_acct_coord_rec(coord, rpc_version, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		packstr(object->default_acct, buffer);
		packstr(object->default_wckey, buffer);
		packstr(object->name, buffer);

		pack32(object->uid, buffer);	

		if(object->wckey_list)
			count = list_count(object->wckey_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->wckey_list);
			while((wckey = list_next(itr))) {
				pack_acct_wckey_rec(wckey, rpc_version,
						    buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;
	} else if(rpc_version >= 3) {
		if(!object) {
			pack16(0, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			packnull(buffer);
			packnull(buffer);
			pack32(0, buffer);
			return;
		}
 
		pack16(object->admin_level, buffer);
		if(object->assoc_list)
			count = list_count(object->assoc_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->assoc_list);
			while((assoc = list_next(itr))) {
				pack_acct_association_rec(assoc, rpc_version,
							  buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->coord_accts)
			count = list_count(object->coord_accts);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->coord_accts);
			while((coord = list_next(itr))) {
				pack_acct_coord_rec(coord, rpc_version, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		packstr(object->default_acct, buffer);
		packstr(object->name, buffer);

		pack32(object->uid, buffer);	
	} else {
		if(!object) {
			pack16(0, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			packnull(buffer);
			packnull(buffer);
			pack32(NO_VAL, buffer);
			pack32(0, buffer);
			return;
		}
 
		pack16(object->admin_level, buffer);
		if(object->assoc_list)
			count = list_count(object->assoc_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->assoc_list);
			while((assoc = list_next(itr))) {
				pack_acct_association_rec(assoc, rpc_version, 
							  buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->coord_accts)
			count = list_count(object->coord_accts);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->coord_accts);
			while((coord = list_next(itr))) {
				pack_acct_coord_rec(coord, rpc_version, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		packstr(object->default_acct, buffer);
		packstr(object->name, buffer);

		pack32(count, buffer); // NEEDED for old qos_list

		pack32(object->uid, buffer);
	} 
}

extern int unpack_acct_user_rec(void **object, uint16_t rpc_version, Buf buffer)
{
	uint32_t uint32_tmp;
	char *tmp_info = NULL;
	acct_user_rec_t *object_ptr = xmalloc(sizeof(acct_user_rec_t));
	uint32_t count = NO_VAL;
	acct_coord_rec_t *coord = NULL;
	acct_association_rec_t *assoc = NULL;
	acct_wckey_rec_t *wckey = NULL;
	int i;

	*object = object_ptr;
	
	if(rpc_version >= 4) {
		safe_unpack16(&object_ptr->admin_level, buffer);
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->assoc_list =
				list_create(destroy_acct_association_rec);
			for(i=0; i<count; i++) {
				if(unpack_acct_association_rec(
					   (void *)&assoc, rpc_version, buffer)
				   == SLURM_ERROR)
					goto unpack_error;
				list_append(object_ptr->assoc_list, assoc);
			}
		}
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->coord_accts =
				list_create(destroy_acct_coord_rec);
			for(i=0; i<count; i++) {
				if(unpack_acct_coord_rec((void *)&coord, 
							 rpc_version, buffer)
				   == SLURM_ERROR)
					goto unpack_error;
				list_append(object_ptr->coord_accts, coord);
			}
		}
		safe_unpackstr_xmalloc(&object_ptr->default_acct, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&object_ptr->default_wckey, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);
		safe_unpack32(&object_ptr->uid, buffer);
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->wckey_list =
				list_create(destroy_acct_wckey_rec);
			for(i=0; i<count; i++) {
				if(unpack_acct_wckey_rec(
					   (void *)&wckey, rpc_version, buffer)
				   == SLURM_ERROR)
					goto unpack_error;
				list_append(object_ptr->wckey_list, wckey);
			}
		}
		
	} else if(rpc_version >= 3) {
		safe_unpack16(&object_ptr->admin_level, buffer);
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->assoc_list =
				list_create(destroy_acct_association_rec);
			for(i=0; i<count; i++) {
				if(unpack_acct_association_rec(
					   (void *)&assoc, rpc_version, buffer)
				   == SLURM_ERROR)
					goto unpack_error;
				list_append(object_ptr->assoc_list, assoc);
			}
		}
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->coord_accts =
				list_create(destroy_acct_coord_rec);
			for(i=0; i<count; i++) {
				if(unpack_acct_coord_rec((void *)&coord, 
							 rpc_version, buffer)
				   == SLURM_ERROR)
					goto unpack_error;
				list_append(object_ptr->coord_accts, coord);
			}
		}
		safe_unpackstr_xmalloc(&object_ptr->default_acct, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);
		safe_unpack32(&object_ptr->uid, buffer);
	} else {
		safe_unpack16(&object_ptr->admin_level, buffer);
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->assoc_list =
				list_create(destroy_acct_association_rec);
			for(i=0; i<count; i++) {
				if(unpack_acct_association_rec(
					   (void *)&assoc, rpc_version, buffer)
				   == SLURM_ERROR)
					goto unpack_error;
				list_append(object_ptr->assoc_list, assoc);
			}
		}
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->coord_accts =
				list_create(destroy_acct_coord_rec);
			for(i=0; i<count; i++) {
				if(unpack_acct_coord_rec((void *)&coord, 
							 rpc_version, buffer)
				   == SLURM_ERROR)
					goto unpack_error;
				list_append(object_ptr->coord_accts, coord);
			}
		}
		safe_unpackstr_xmalloc(&object_ptr->default_acct, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				xfree(tmp_info);
			}
		}
		safe_unpack32(&object_ptr->uid, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	destroy_acct_user_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void pack_acct_used_limits(void *in, uint16_t rpc_version, Buf buffer)
{
	acct_used_limits_t *object = (acct_used_limits_t *)in;

	if(!object) {
		pack64(0, buffer);
		pack32(0, buffer);
		pack32(0, buffer);
		pack32(0, buffer);
		pack32(0, buffer);
		pack32(0, buffer);
		pack32(0, buffer);
		return;
	}
	
	pack64(object->cpu_mins, buffer);
	pack32(object->cpus, buffer);
	pack32(object->jobs, buffer);
	pack32(object->nodes, buffer);
	pack32(object->submit_jobs, buffer);
	pack32(object->wall, buffer);
	pack32(object->uid, buffer);
}

extern int unpack_acct_used_limits(void **object,
				   uint16_t rpc_version, Buf buffer)
{
	acct_used_limits_t *object_ptr = xmalloc(sizeof(acct_used_limits_t));

	*object = (void *)object_ptr;

	safe_unpack64(&object_ptr->cpu_mins, buffer);
	safe_unpack32(&object_ptr->cpus, buffer);
	safe_unpack32(&object_ptr->jobs, buffer);
	safe_unpack32(&object_ptr->nodes, buffer);
	safe_unpack32(&object_ptr->submit_jobs, buffer);
	safe_unpack32(&object_ptr->wall, buffer);
	safe_unpack32(&object_ptr->uid, buffer);
	return SLURM_SUCCESS;

unpack_error:
	destroy_acct_used_limits(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void pack_acct_account_rec(void *in, uint16_t rpc_version, Buf buffer)
{
	acct_coord_rec_t *coord = NULL;
	ListIterator itr = NULL;
	uint32_t count = NO_VAL;
	acct_account_rec_t *object = (acct_account_rec_t *)in;
	acct_association_rec_t *assoc = NULL;

	if(rpc_version >= 3) {
		if(!object) {
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			packnull(buffer);
			packnull(buffer);
			packnull(buffer);
			return;
		}
 
		if(object->assoc_list)
			count = list_count(object->assoc_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->assoc_list);
			while((assoc = list_next(itr))) {
				pack_acct_association_rec(assoc, rpc_version,
							  buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->coordinators)
			count = list_count(object->coordinators);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->coordinators);
			while((coord = list_next(itr))) {
				pack_acct_coord_rec(coord, rpc_version, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		packstr(object->description, buffer);
		packstr(object->name, buffer);
		packstr(object->organization, buffer);
	} else {
		if(!object) {
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			packnull(buffer);
			packnull(buffer);
			packnull(buffer);
			pack32(NO_VAL, buffer);
			return;
		}
 
		if(object->assoc_list)
			count = list_count(object->assoc_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->assoc_list);
			while((assoc = list_next(itr))) {
				pack_acct_association_rec(assoc, rpc_version,
							  buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->coordinators)
			count = list_count(object->coordinators);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->coordinators);
			while((coord = list_next(itr))) {
				pack_acct_coord_rec(coord, rpc_version, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		packstr(object->description, buffer);
		packstr(object->name, buffer);
		packstr(object->organization, buffer);

		pack32(count, buffer); // NEEDED FOR OLD QOS_LIST
	}
}

extern int unpack_acct_account_rec(void **object, uint16_t rpc_version,
				   Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	char *tmp_info = NULL;
	acct_coord_rec_t *coord = NULL;
	acct_association_rec_t *assoc = NULL;
	acct_account_rec_t *object_ptr = xmalloc(sizeof(acct_account_rec_t));

	*object = object_ptr;

	if(rpc_version >= 3) {
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->assoc_list =
				list_create(destroy_acct_association_rec);
			for(i=0; i<count; i++) {
				if(unpack_acct_association_rec((void *)&assoc, 
							       rpc_version,
							       buffer)
				   == SLURM_ERROR)
					goto unpack_error;
				list_append(object_ptr->assoc_list, assoc);
			}
		}
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->coordinators = 
				list_create(destroy_acct_coord_rec);
			for(i=0; i<count; i++) {
				if(unpack_acct_coord_rec((void *)&coord, 
							 rpc_version, buffer)
				   == SLURM_ERROR)
					goto unpack_error;
				list_append(object_ptr->coordinators, coord);
			}
		}
		safe_unpackstr_xmalloc(&object_ptr->description,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->organization,
				       &uint32_tmp, buffer);
	} else {
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->assoc_list =
				list_create(destroy_acct_association_rec);
			for(i=0; i<count; i++) {
				if(unpack_acct_association_rec((void *)&assoc, 
							       rpc_version,
							       buffer)
				   == SLURM_ERROR)
					goto unpack_error;
				list_append(object_ptr->assoc_list, assoc);
			}
		}
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->coordinators = 
				list_create(destroy_acct_coord_rec);
			for(i=0; i<count; i++) {
				if(unpack_acct_coord_rec((void *)&coord, 
							 rpc_version, buffer)
				   == SLURM_ERROR)
					goto unpack_error;
				list_append(object_ptr->coordinators, coord);
			}
		}
		safe_unpackstr_xmalloc(&object_ptr->description,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->organization,
				       &uint32_tmp, buffer);
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				xfree(tmp_info);
			}
		}
	} 

	return SLURM_SUCCESS;

unpack_error:
	destroy_acct_account_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void pack_acct_coord_rec(void *in, uint16_t rpc_version, Buf buffer)
{
	acct_coord_rec_t *object = (acct_coord_rec_t *)in;

	if(!object) {
		packnull(buffer);
		pack16(0, buffer);
		return;
	}

	packstr(object->name, buffer);
	pack16(object->direct, buffer);
}

extern int unpack_acct_coord_rec(void **object, uint16_t rpc_version,
				 Buf buffer)
{
	uint32_t uint32_tmp;
	acct_coord_rec_t *object_ptr = xmalloc(sizeof(acct_coord_rec_t));

	*object = object_ptr;
	safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);
	safe_unpack16(&object_ptr->direct, buffer);
	return SLURM_SUCCESS;

unpack_error:
	destroy_acct_coord_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void pack_cluster_accounting_rec(void *in, uint16_t rpc_version,
					Buf buffer)
{
	cluster_accounting_rec_t *object = (cluster_accounting_rec_t *)in;
	
	if(rpc_version >= 5) {
		if(!object) {
			pack64(0, buffer);
			pack32(0, buffer);
			pack64(0, buffer);
			pack64(0, buffer);
			pack64(0, buffer);
			pack64(0, buffer);
			pack_time(0, buffer);
			pack64(0, buffer);
			return;
		}
		
		pack64(object->alloc_secs, buffer);
		pack32(object->cpu_count, buffer);
		pack64(object->down_secs, buffer);
		pack64(object->idle_secs, buffer);
		pack64(object->over_secs, buffer);
		pack64(object->pdown_secs, buffer);
		pack_time(object->period_start, buffer);
		pack64(object->resv_secs, buffer);
	} else {
		if(!object) {
			pack64(0, buffer);
			pack32(0, buffer);
			pack64(0, buffer);
			pack64(0, buffer);
			pack64(0, buffer);
			pack_time(0, buffer);
			pack64(0, buffer);
			return;
		}
		
		pack64(object->alloc_secs, buffer);
		pack32(object->cpu_count, buffer);
		pack64(object->down_secs, buffer);
		pack64(object->idle_secs, buffer);
		pack64(object->over_secs, buffer);
		pack_time(object->period_start, buffer);
		pack64(object->resv_secs, buffer);
	}
}

extern int unpack_cluster_accounting_rec(void **object, uint16_t rpc_version,
					 Buf buffer)
{
	cluster_accounting_rec_t *object_ptr =
		xmalloc(sizeof(cluster_accounting_rec_t));
	
	*object = object_ptr;

	if(rpc_version >= 5) {
		safe_unpack64(&object_ptr->alloc_secs, buffer);
		safe_unpack32(&object_ptr->cpu_count, buffer);
		safe_unpack64(&object_ptr->down_secs, buffer);
		safe_unpack64(&object_ptr->idle_secs, buffer);
		safe_unpack64(&object_ptr->over_secs, buffer);
		safe_unpack64(&object_ptr->pdown_secs, buffer);
		safe_unpack_time(&object_ptr->period_start, buffer);
		safe_unpack64(&object_ptr->resv_secs, buffer);
	} else {
		safe_unpack64(&object_ptr->alloc_secs, buffer);
		safe_unpack32(&object_ptr->cpu_count, buffer);
		safe_unpack64(&object_ptr->down_secs, buffer);
		safe_unpack64(&object_ptr->idle_secs, buffer);
		safe_unpack64(&object_ptr->over_secs, buffer);
		safe_unpack_time(&object_ptr->period_start, buffer);
		safe_unpack64(&object_ptr->resv_secs, buffer);
	}
	
	return SLURM_SUCCESS;

unpack_error:
	destroy_cluster_accounting_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void pack_acct_cluster_rec(void *in, uint16_t rpc_version, Buf buffer)
{
	cluster_accounting_rec_t *acct_info = NULL;
	ListIterator itr = NULL;
	uint32_t count = NO_VAL;
	acct_cluster_rec_t *object = (acct_cluster_rec_t *)in;

	if(rpc_version >= 5) {
		if(!object) {
			pack32(NO_VAL, buffer);
			pack16(0, buffer);
			packnull(buffer);
			pack32(0, buffer);
			pack32(0, buffer);

			packnull(buffer);
			packnull(buffer);

			pack_acct_association_rec(NULL, rpc_version, buffer);

			pack16(0, buffer);
			return;
		}
 
		if(object->accounting_list)
			count = list_count(object->accounting_list);

		pack32(count, buffer);

		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->accounting_list);
			while((acct_info = list_next(itr))) {
				pack_cluster_accounting_rec(
					acct_info, rpc_version, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		pack16(object->classification, buffer);
		packstr(object->control_host, buffer);
		pack32(object->control_port, buffer);
		pack32(object->cpu_count, buffer);

		packstr(object->name, buffer);
		packstr(object->nodes, buffer);

		pack_acct_association_rec(object->root_assoc,
					  rpc_version, buffer);

		pack16(object->rpc_version, buffer);
	} else if(rpc_version >= 3) {
		if(!object) {
			pack32(NO_VAL, buffer);
			packnull(buffer);
			pack32(0, buffer);

			packnull(buffer);

			pack32(NO_VAL, buffer);
			pack_acct_association_rec(NULL, rpc_version, buffer);

			pack16(0, buffer);
			return;
		}
 
		if(object->accounting_list)
			count = list_count(object->accounting_list);

		pack32(count, buffer);

		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->accounting_list);
			while((acct_info = list_next(itr))) {
				pack_cluster_accounting_rec(
					acct_info, rpc_version, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		packstr(object->control_host, buffer);
		pack32(object->control_port, buffer);

		packstr(object->name, buffer);

		pack32(count, buffer); /* for defunt valid_qos_list */

		pack_acct_association_rec(object->root_assoc,
					  rpc_version, buffer);

		pack16(object->rpc_version, buffer);
	} else {
		if(!object) {
			pack32(NO_VAL, buffer);
			packnull(buffer);
			pack32(0, buffer);

			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);

			packnull(buffer);
			pack16(0, buffer);
			return;
		}
 
		if(object->accounting_list)
			count = list_count(object->accounting_list);

		pack32(count, buffer);

		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->accounting_list);
			while((acct_info = list_next(itr))) {
				pack_cluster_accounting_rec(
					acct_info, rpc_version, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		packstr(object->control_host, buffer);
		pack32(object->control_port, buffer);
		if(!object->root_assoc) {
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
		} else {
			pack32(object->root_assoc->shares_raw, buffer);
			pack32(object->root_assoc->max_cpu_mins_pj, buffer);
			pack32(object->root_assoc->max_jobs, buffer);
			pack32(object->root_assoc->max_nodes_pj, buffer);
			pack32(object->root_assoc->max_wall_pj, buffer);
		}

		packstr(object->name, buffer);
	} 
}

extern int unpack_acct_cluster_rec(void **object, uint16_t rpc_version,
				   Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	acct_cluster_rec_t *object_ptr = xmalloc(sizeof(acct_cluster_rec_t));
	cluster_accounting_rec_t *acct_info = NULL;

	*object = object_ptr;

	if(rpc_version >= 5) {
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->accounting_list =
				list_create(destroy_cluster_accounting_rec);
			for(i=0; i<count; i++) {
				unpack_cluster_accounting_rec(
					(void *)&acct_info,
					rpc_version, buffer);
				list_append(object_ptr->accounting_list,
					    acct_info);
			}
		}

		safe_unpack16(&object_ptr->classification, buffer);
		safe_unpackstr_xmalloc(&object_ptr->control_host,
				       &uint32_tmp, buffer);
		safe_unpack32(&object_ptr->control_port, buffer);
		safe_unpack32(&object_ptr->cpu_count, buffer);

		safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->nodes, &uint32_tmp, buffer);

		if(unpack_acct_association_rec(
			   (void **)&object_ptr->root_assoc, 
			   rpc_version, buffer)
		   == SLURM_ERROR)
			goto unpack_error;

		safe_unpack16(&object_ptr->rpc_version, buffer);
	} else if(rpc_version >= 3) {
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->accounting_list =
				list_create(destroy_cluster_accounting_rec);
			for(i=0; i<count; i++) {
				unpack_cluster_accounting_rec(
					(void *)&acct_info,
					rpc_version, buffer);
				list_append(object_ptr->accounting_list,
					    acct_info);
			}
		}

		safe_unpackstr_xmalloc(&object_ptr->control_host,
				       &uint32_tmp, buffer);
		safe_unpack32(&object_ptr->control_port, buffer);

		safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);

		safe_unpack32(&count, buffer); /* for defunt valid_qos_list */

		if(unpack_acct_association_rec(
			   (void **)&object_ptr->root_assoc, 
			   rpc_version, buffer)
		   == SLURM_ERROR)
			goto unpack_error;

		safe_unpack16(&object_ptr->rpc_version, buffer);
	} else {
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->accounting_list =
				list_create(destroy_cluster_accounting_rec);
			for(i=0; i<count; i++) {
				unpack_cluster_accounting_rec(
					(void *)&acct_info,
					rpc_version, buffer);
				list_append(object_ptr->accounting_list,
					    acct_info);
			}
		}
		safe_unpackstr_xmalloc(&object_ptr->control_host,
				       &uint32_tmp, buffer);
		safe_unpack32(&object_ptr->control_port, buffer);
		object_ptr->root_assoc = 
			xmalloc(sizeof(acct_association_rec_t));
		init_acct_association_rec(object_ptr->root_assoc);
		safe_unpack32(&object_ptr->root_assoc->shares_raw, buffer);
		safe_unpack32((uint32_t *)&object_ptr->root_assoc->
			      max_cpu_mins_pj, buffer);
		safe_unpack32(&object_ptr->root_assoc->max_jobs, buffer);
		safe_unpack32(&object_ptr->root_assoc->max_nodes_pj, buffer);
		safe_unpack32(&object_ptr->root_assoc->max_wall_pj,
			      buffer);
		safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);
		/* default to rpc version 2 since that was the version we had
		   before we started checking .
		*/
		object_ptr->rpc_version = 2;
	} 
	return SLURM_SUCCESS;

unpack_error:
	destroy_acct_cluster_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void pack_acct_accounting_rec(void *in, uint16_t rpc_version, Buf buffer)
{
	acct_accounting_rec_t *object = (acct_accounting_rec_t *)in;
	
	if(!object) {
		pack64(0, buffer);
		pack32(0, buffer);
		pack_time(0, buffer);
		return;
	}
	
	pack64(object->alloc_secs, buffer);
	pack32(object->id, buffer);
	pack_time(object->period_start, buffer);		
}

extern int unpack_acct_accounting_rec(void **object, uint16_t rpc_version,
				      Buf buffer)
{
	acct_accounting_rec_t *object_ptr =
		xmalloc(sizeof(acct_accounting_rec_t));
	
	*object = object_ptr;
	
	safe_unpack64(&object_ptr->alloc_secs, buffer);
	safe_unpack32(&object_ptr->id, buffer);
	safe_unpack_time(&object_ptr->period_start, buffer);	
		
	return SLURM_SUCCESS;

unpack_error:
	destroy_acct_accounting_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void pack_acct_association_rec(void *in, uint16_t rpc_version, 
				      Buf buffer)
{
	acct_accounting_rec_t *acct_info = NULL;
	ListIterator itr = NULL;
	uint32_t count = NO_VAL;
	char *tmp_info = NULL;
	acct_association_rec_t *object = (acct_association_rec_t *)in;	
	
	if (rpc_version >= 4) {
		if(!object) {
			pack32(NO_VAL, buffer);
			packnull(buffer);
			packnull(buffer);

			pack32(NO_VAL, buffer);

			pack64(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);

			pack32(0, buffer);
			pack32(0, buffer);

			pack64(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);

			packnull(buffer);
			pack32(0, buffer);
			packnull(buffer);

			pack32(NO_VAL, buffer);

			pack32(0, buffer);
			pack32(0, buffer);

			packnull(buffer);
			return;
		}
 
		if(object->accounting_list)
			count = list_count(object->accounting_list);

		pack32(count, buffer);

		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->accounting_list);
			while((acct_info = list_next(itr))) {
				pack_acct_accounting_rec(acct_info, 
							 rpc_version, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		packstr(object->acct, buffer);
		packstr(object->cluster, buffer);

		/* this used to be named fairshare to not have to redo
		   the order of things just to be in alpha order we
		   just renamed it and called it good */
		pack32(object->shares_raw, buffer);

		pack64(object->grp_cpu_mins, buffer);
		pack32(object->grp_cpus, buffer);
		pack32(object->grp_jobs, buffer);
		pack32(object->grp_nodes, buffer);
		pack32(object->grp_submit_jobs, buffer);
		pack32(object->grp_wall, buffer);

		pack32(object->id, buffer);
		pack32(object->lft, buffer);

		pack64(object->max_cpu_mins_pj, buffer);
		pack32(object->max_cpus_pj, buffer);
		pack32(object->max_jobs, buffer);
		pack32(object->max_nodes_pj, buffer);
		pack32(object->max_submit_jobs, buffer);
		pack32(object->max_wall_pj, buffer);

		packstr(object->parent_acct, buffer);
		pack32(object->parent_id, buffer);
		packstr(object->partition, buffer);

		if(object->qos_list)
			count = list_count(object->qos_list);

		pack32(count, buffer);

		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->qos_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		pack32(object->rgt, buffer);
		pack32(object->uid, buffer);

		packstr(object->user, buffer);	
	} else if (rpc_version == 3) {
		if(!object) {
			pack32(NO_VAL, buffer);
			packnull(buffer);
			packnull(buffer);

			pack32(NO_VAL, buffer);

			pack64(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);

			pack32(0, buffer);
			pack32(0, buffer);

			pack64(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);

			packnull(buffer);
			pack32(0, buffer);
			packnull(buffer);

			pack32(NO_VAL, buffer);

			pack32(0, buffer);
			pack32(0, buffer);

			pack32(0, buffer);

			packnull(buffer);
			return;
		}
 
		if(object->accounting_list)
			count = list_count(object->accounting_list);

		pack32(count, buffer);

		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->accounting_list);
			while((acct_info = list_next(itr))) {
				pack_acct_accounting_rec(acct_info, 
							 rpc_version, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		packstr(object->acct, buffer);
		packstr(object->cluster, buffer);

		pack32(object->shares_raw, buffer);

		pack64(object->grp_cpu_mins, buffer);
		pack32(object->grp_cpus, buffer);
		pack32(object->grp_jobs, buffer);
		pack32(object->grp_nodes, buffer);
		pack32(object->grp_submit_jobs, buffer);
		pack32(object->grp_wall, buffer);

		pack32(object->id, buffer);
		pack32(object->lft, buffer);

		pack64(object->max_cpu_mins_pj, buffer);
		pack32(object->max_cpus_pj, buffer);
		pack32(object->max_jobs, buffer);
		pack32(object->max_nodes_pj, buffer);
		pack32(object->max_submit_jobs, buffer);
		pack32(object->max_wall_pj, buffer);

		packstr(object->parent_acct, buffer);
		pack32(object->parent_id, buffer);
		packstr(object->partition, buffer);

		if(object->qos_list)
			count = list_count(object->qos_list);

		pack32(count, buffer);

		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->qos_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		pack32(object->rgt, buffer);
		pack32(object->uid, buffer);

		/* used shares which is taken out in 4 */
		pack32(0, buffer);

		packstr(object->user, buffer);	
	} else {
		if(!object) {
			pack32(NO_VAL, buffer);

			packnull(buffer);
			packnull(buffer);

			pack32(NO_VAL, buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);

			packnull(buffer);
			pack32(0, buffer);
			packnull(buffer);

			pack32(0, buffer);
			pack32(0, buffer);

			pack32(0, buffer);

			packnull(buffer);
			return;
		}
 
		if(object->accounting_list)
			count = list_count(object->accounting_list);

		pack32(count, buffer);

		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->accounting_list);
			while((acct_info = list_next(itr))) {
				pack_acct_accounting_rec(acct_info, 
							 rpc_version, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		packstr(object->acct, buffer);
		packstr(object->cluster, buffer);
		pack32(object->shares_raw, buffer);
		pack32(object->id, buffer);
		pack32(object->lft, buffer);
		pack32(object->max_cpu_mins_pj, buffer);
		pack32(object->max_jobs, buffer);
		pack32(object->max_nodes_pj, buffer);
		pack32(object->max_wall_pj, buffer);
		packstr(object->parent_acct, buffer);
		pack32(object->parent_id, buffer);
		packstr(object->partition, buffer);
		pack32(object->rgt, buffer);
		pack32(object->uid, buffer);
		/* used shares which is taken out in 4 */
		pack32(0, buffer);

		packstr(object->user, buffer);	
	} 
}

extern int unpack_acct_association_rec(void **object, uint16_t rpc_version,
				       Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	char *tmp_info = NULL;
	acct_association_rec_t *object_ptr = 
		xmalloc(sizeof(acct_association_rec_t));
	acct_accounting_rec_t *acct_info = NULL;

	*object = object_ptr;

	init_acct_association_rec(object_ptr);

	if (rpc_version >= 4) {
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->accounting_list =
				list_create(destroy_acct_accounting_rec);
			for(i=0; i<count; i++) {
				if(unpack_acct_accounting_rec(
					   (void **)&acct_info,
					   rpc_version, 
					   buffer) == SLURM_ERROR)
					goto unpack_error;
				list_append(object_ptr->accounting_list, 
					    acct_info);
			}
		}

		safe_unpackstr_xmalloc(&object_ptr->acct, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->cluster, &uint32_tmp,
				       buffer);

		safe_unpack32(&object_ptr->shares_raw, buffer);

		safe_unpack64(&object_ptr->grp_cpu_mins, buffer);
		safe_unpack32(&object_ptr->grp_cpus, buffer);
		safe_unpack32(&object_ptr->grp_jobs, buffer);
		safe_unpack32(&object_ptr->grp_nodes, buffer);
		safe_unpack32(&object_ptr->grp_submit_jobs, buffer);
		safe_unpack32(&object_ptr->grp_wall, buffer);

		safe_unpack32(&object_ptr->id, buffer);
		safe_unpack32(&object_ptr->lft, buffer);

		safe_unpack64(&object_ptr->max_cpu_mins_pj, buffer);
		safe_unpack32(&object_ptr->max_cpus_pj, buffer);
		safe_unpack32(&object_ptr->max_jobs, buffer);
		safe_unpack32(&object_ptr->max_nodes_pj, buffer);
		safe_unpack32(&object_ptr->max_submit_jobs, buffer);
		safe_unpack32(&object_ptr->max_wall_pj, buffer);

		safe_unpackstr_xmalloc(&object_ptr->parent_acct, &uint32_tmp,
				       buffer);
		safe_unpack32(&object_ptr->parent_id, buffer);
		safe_unpackstr_xmalloc(&object_ptr->partition, &uint32_tmp,
				       buffer);

		safe_unpack32(&count, buffer);
		/* This needs to look for zero to tell if something
		   has changed */
		if(count != NO_VAL) {
			object_ptr->qos_list = list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->qos_list, tmp_info);
			}
		}

		safe_unpack32(&object_ptr->rgt, buffer);
		safe_unpack32(&object_ptr->uid, buffer);

		safe_unpackstr_xmalloc(&object_ptr->user, &uint32_tmp, buffer);
	} else if (rpc_version >= 3) {
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->accounting_list =
				list_create(destroy_acct_accounting_rec);
			for(i=0; i<count; i++) {
				if(unpack_acct_accounting_rec(
					   (void **)&acct_info,
					   rpc_version, 
					   buffer) == SLURM_ERROR)
					goto unpack_error;
				list_append(object_ptr->accounting_list, 
					    acct_info);
			}
		}

		safe_unpackstr_xmalloc(&object_ptr->acct, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->cluster, &uint32_tmp,
				       buffer);

		safe_unpack32(&object_ptr->shares_raw, buffer);

		safe_unpack64(&object_ptr->grp_cpu_mins, buffer);
		safe_unpack32(&object_ptr->grp_cpus, buffer);
		safe_unpack32(&object_ptr->grp_jobs, buffer);
		safe_unpack32(&object_ptr->grp_nodes, buffer);
		safe_unpack32(&object_ptr->grp_submit_jobs, buffer);
		safe_unpack32(&object_ptr->grp_wall, buffer);

		safe_unpack32(&object_ptr->id, buffer);
		safe_unpack32(&object_ptr->lft, buffer);

		safe_unpack64(&object_ptr->max_cpu_mins_pj, buffer);
		safe_unpack32(&object_ptr->max_cpus_pj, buffer);
		safe_unpack32(&object_ptr->max_jobs, buffer);
		safe_unpack32(&object_ptr->max_nodes_pj, buffer);
		safe_unpack32(&object_ptr->max_submit_jobs, buffer);
		safe_unpack32(&object_ptr->max_wall_pj, buffer);

		safe_unpackstr_xmalloc(&object_ptr->parent_acct, &uint32_tmp,
				       buffer);
		safe_unpack32(&object_ptr->parent_id, buffer);
		safe_unpackstr_xmalloc(&object_ptr->partition, &uint32_tmp,
				       buffer);

		safe_unpack32(&count, buffer);
		/* This needs to look for zero to tell if something
		   has changed */
		if(count != NO_VAL) {
			object_ptr->qos_list = list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->qos_list, tmp_info);
			}
		}

		safe_unpack32(&object_ptr->rgt, buffer);
		safe_unpack32(&object_ptr->uid, buffer);

		/* used shares which is taken out in 4 */
		safe_unpack32(&uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&object_ptr->user, &uint32_tmp, buffer);
	} else {
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->accounting_list =
				list_create(destroy_acct_accounting_rec);
			for(i=0; i<count; i++) {
				if(unpack_acct_accounting_rec(
					   (void **)&acct_info,
					   rpc_version, 
					   buffer) == SLURM_ERROR)
					goto unpack_error;
				list_append(object_ptr->accounting_list, 
					    acct_info);
			}
		}
		safe_unpackstr_xmalloc(&object_ptr->acct, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->cluster, &uint32_tmp,
				       buffer);

		safe_unpack32(&object_ptr->shares_raw, buffer);
		safe_unpack32(&object_ptr->id, buffer);
		safe_unpack32(&object_ptr->lft, buffer);

		safe_unpack32(&uint32_tmp, buffer);
		object_ptr->max_cpu_mins_pj = uint32_tmp;
		safe_unpack32(&object_ptr->max_jobs, buffer);
		safe_unpack32(&object_ptr->max_nodes_pj, buffer);
		safe_unpack32(&object_ptr->max_wall_pj, buffer);

		safe_unpackstr_xmalloc(&object_ptr->parent_acct, &uint32_tmp,
				       buffer);
		safe_unpack32(&object_ptr->parent_id, buffer);
		safe_unpackstr_xmalloc(&object_ptr->partition, &uint32_tmp,
				       buffer);
		
		safe_unpack32(&object_ptr->rgt, buffer);
		safe_unpack32(&object_ptr->uid, buffer);

		/* used shares which is taken out in 4 */
		safe_unpack32(&uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&object_ptr->user, &uint32_tmp, buffer);
	} 

	return SLURM_SUCCESS;

unpack_error:
	destroy_acct_association_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void pack_acct_qos_rec(void *in, uint16_t rpc_version, Buf buffer)
{
	ListIterator itr = NULL;
	acct_qos_rec_t *object = (acct_qos_rec_t *)in;	
	uint32_t count = NO_VAL;
	char *tmp_info = NULL;
		
	if(rpc_version >= 6) {
		if(!object) {
			packnull(buffer);
			pack32(0, buffer);
			packnull(buffer);

			pack64(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);

			pack64(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);

			packnull(buffer);

			pack_bit_str(NULL, buffer);
			pack32(NO_VAL, buffer);

			pack32(0, buffer);

			packdouble(NO_VAL, buffer);

			pack32(NO_VAL, buffer);
			return;
		}
		packstr(object->description, buffer);	
		pack32(object->id, buffer);

		pack64(object->grp_cpu_mins, buffer);
		pack32(object->grp_cpus, buffer);
		pack32(object->grp_jobs, buffer);
		pack32(object->grp_nodes, buffer);
		pack32(object->grp_submit_jobs, buffer);
		pack32(object->grp_wall, buffer);

		pack64(object->max_cpu_mins_pu, buffer);
		pack32(object->max_cpus_pu, buffer);
		pack32(object->max_jobs_pu, buffer);
		pack32(object->max_nodes_pu, buffer);
		pack32(object->max_submit_jobs_pu, buffer);
		pack32(object->max_wall_pu, buffer);

		packstr(object->name, buffer);	

		pack_bit_str(object->preempt_bitstr, buffer);
		
		if(object->preempt_list)
			count = list_count(object->preempt_list);

		pack32(count, buffer);

		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->preempt_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		pack32(object->priority, buffer);
		
		packdouble(object->usage_factor, buffer);

		if(object->user_limit_list)
			count = list_count(object->user_limit_list);

		pack32(count, buffer);

		if(count && count != NO_VAL) {
			acct_used_limits_t *used_limits = NULL;
			itr = list_iterator_create(object->user_limit_list);
			while((used_limits = list_next(itr))) {
				pack_acct_used_limits(used_limits,
						      rpc_version, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;
	} else if(rpc_version >= 5) {
		if(!object) {
			packnull(buffer);
			pack32(0, buffer);
			packnull(buffer);

			pack64(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);

			pack64(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);

			packnull(buffer);

			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);

			pack32(0, buffer);

			packdouble(NO_VAL, buffer);

			pack32(NO_VAL, buffer);
			return;
		}
		packstr(object->description, buffer);	
		pack32(object->id, buffer);

		pack64(object->grp_cpu_mins, buffer);
		pack32(object->grp_cpus, buffer);
		pack32(object->grp_jobs, buffer);
		pack32(object->grp_nodes, buffer);
		pack32(object->grp_submit_jobs, buffer);
		pack32(object->grp_wall, buffer);

		pack64(object->max_cpu_mins_pu, buffer);
		pack32(object->max_cpus_pu, buffer);
		pack32(object->max_jobs_pu, buffer);
		pack32(object->max_nodes_pu, buffer);
		pack32(object->max_submit_jobs_pu, buffer);
		pack32(object->max_wall_pu, buffer);

		packstr(object->name, buffer);	

		/* These are here for the old preemptee preemptor
		   lists we could figure this out from the
		   preempt_bitstr, but qos wasn't used for anything
		   before rpc_version 6 so just send NO_VALS */
		pack32(NO_VAL, buffer);
		pack32(NO_VAL, buffer);
		
		pack32(object->priority, buffer);
		
		packdouble(object->usage_factor, buffer);

		if(object->user_limit_list)
			count = list_count(object->user_limit_list);

		pack32(count, buffer);

		if(count && count != NO_VAL) {
			acct_used_limits_t *used_limits = NULL;
			itr = list_iterator_create(object->user_limit_list);
			while((used_limits = list_next(itr))) {
				pack_acct_used_limits(used_limits,
						      rpc_version, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;
	} else if(rpc_version >= 3) {
		if(!object) {
			packnull(buffer);
			pack32(0, buffer);
			packnull(buffer);

			pack64(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);

			pack64(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);

			packnull(buffer);

			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);

			pack32(0, buffer);

			pack32(NO_VAL, buffer);
			return;
		}
		packstr(object->description, buffer);	
		pack32(object->id, buffer);

		pack64(object->grp_cpu_mins, buffer);
		pack32(object->grp_cpus, buffer);
		pack32(object->grp_jobs, buffer);
		pack32(object->grp_nodes, buffer);
		pack32(object->grp_submit_jobs, buffer);
		pack32(object->grp_wall, buffer);

		pack64(object->max_cpu_mins_pu, buffer);
		pack32(object->max_cpus_pu, buffer);
		pack32(object->max_jobs_pu, buffer);
		pack32(object->max_nodes_pu, buffer);
		pack32(object->max_submit_jobs_pu, buffer);
		pack32(object->max_wall_pu, buffer);

		packstr(object->name, buffer);	

		/* These are here for the old preemptee preemptor
		   lists we could figure this out from the
		   preempt_bitstr, but qos wasn't used for anything
		   before rpc_version 6 so just send NO_VALS */
		pack32(NO_VAL, buffer);
		pack32(NO_VAL, buffer);
		
		pack32(object->priority, buffer);
		
		if(object->user_limit_list)
			count = list_count(object->user_limit_list);

		pack32(count, buffer);

		if(count && count != NO_VAL) {
			acct_used_limits_t *used_limits = NULL;
			itr = list_iterator_create(object->user_limit_list);
			while((used_limits = list_next(itr))) {
				pack_acct_used_limits(used_limits,
						      rpc_version, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;
	} else {
		if(!object) {
			packnull(buffer);
			pack32(0, buffer);
			packnull(buffer);
			return;
		}
		packstr(object->description, buffer);	
		pack32(object->id, buffer);
		packstr(object->name, buffer);	
	} 
}

extern int unpack_acct_qos_rec(void **object, uint16_t rpc_version, Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	acct_qos_rec_t *object_ptr = xmalloc(sizeof(acct_qos_rec_t));
	uint32_t count = NO_VAL;
	char *tmp_info = NULL;

	*object = object_ptr;
	
	init_acct_qos_rec(object_ptr);

	if(rpc_version >= 6) {
		safe_unpackstr_xmalloc(&object_ptr->description,
				       &uint32_tmp, buffer);
		safe_unpack32(&object_ptr->id, buffer);

		safe_unpack64(&object_ptr->grp_cpu_mins, buffer);
		safe_unpack32(&object_ptr->grp_cpus, buffer);
		safe_unpack32(&object_ptr->grp_jobs, buffer);
		safe_unpack32(&object_ptr->grp_nodes, buffer);
		safe_unpack32(&object_ptr->grp_submit_jobs, buffer);
		safe_unpack32(&object_ptr->grp_wall, buffer);

		safe_unpack64(&object_ptr->max_cpu_mins_pu, buffer);
		safe_unpack32(&object_ptr->max_cpus_pu, buffer);
		safe_unpack32(&object_ptr->max_jobs_pu, buffer);
		safe_unpack32(&object_ptr->max_nodes_pu, buffer);
		safe_unpack32(&object_ptr->max_submit_jobs_pu, buffer);
		safe_unpack32(&object_ptr->max_wall_pu, buffer);

		safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);

		unpack_bit_str(&object_ptr->preempt_bitstr, buffer);

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->preempt_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->preempt_list,
					    tmp_info);
			}
		}
		
		safe_unpack32(&object_ptr->priority, buffer);

		safe_unpackdouble(&object_ptr->usage_factor, buffer);

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			void *used_limits = NULL;

			object_ptr->user_limit_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				unpack_acct_used_limits(&used_limits,
							rpc_version, buffer);
				list_append(object_ptr->user_limit_list,
					    used_limits);
			}
		}

	} else if(rpc_version >= 5) {
		safe_unpackstr_xmalloc(&object_ptr->description,
				       &uint32_tmp, buffer);
		safe_unpack32(&object_ptr->id, buffer);

		safe_unpack64(&object_ptr->grp_cpu_mins, buffer);
		safe_unpack32(&object_ptr->grp_cpus, buffer);
		safe_unpack32(&object_ptr->grp_jobs, buffer);
		safe_unpack32(&object_ptr->grp_nodes, buffer);
		safe_unpack32(&object_ptr->grp_submit_jobs, buffer);
		safe_unpack32(&object_ptr->grp_wall, buffer);

		safe_unpack64(&object_ptr->max_cpu_mins_pu, buffer);
		safe_unpack32(&object_ptr->max_cpus_pu, buffer);
		safe_unpack32(&object_ptr->max_jobs_pu, buffer);
		safe_unpack32(&object_ptr->max_nodes_pu, buffer);
		safe_unpack32(&object_ptr->max_submit_jobs_pu, buffer);
		safe_unpack32(&object_ptr->max_wall_pu, buffer);

		safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->preempt_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->preempt_list,
					    tmp_info);
			}
		}

		/* this is here for the old preemptor list.  which was
		   never used so just throw anything here away.
		*/
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				xfree(tmp_info);
			}
		}

		safe_unpack32(&object_ptr->priority, buffer);

		safe_unpackdouble(&object_ptr->usage_factor, buffer);

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			void *used_limits = NULL;

			object_ptr->user_limit_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				unpack_acct_used_limits(&used_limits,
							rpc_version, buffer);
				list_append(object_ptr->user_limit_list,
					    used_limits);
			}
		}

	} else if(rpc_version >= 3) {
		safe_unpackstr_xmalloc(&object_ptr->description,
				       &uint32_tmp, buffer);
		safe_unpack32(&object_ptr->id, buffer);

		safe_unpack64(&object_ptr->grp_cpu_mins, buffer);
		safe_unpack32(&object_ptr->grp_cpus, buffer);
		safe_unpack32(&object_ptr->grp_jobs, buffer);
		safe_unpack32(&object_ptr->grp_nodes, buffer);
		safe_unpack32(&object_ptr->grp_submit_jobs, buffer);
		safe_unpack32(&object_ptr->grp_wall, buffer);

		safe_unpack64(&object_ptr->max_cpu_mins_pu, buffer);
		safe_unpack32(&object_ptr->max_cpus_pu, buffer);
		safe_unpack32(&object_ptr->max_jobs_pu, buffer);
		safe_unpack32(&object_ptr->max_nodes_pu, buffer);
		safe_unpack32(&object_ptr->max_submit_jobs_pu, buffer);
		safe_unpack32(&object_ptr->max_wall_pu, buffer);

		safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->preempt_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->preempt_list,
					    tmp_info);
			}
		}

		/* this is here for the old preemptor list.  which was
		   never used so just throw anything here away.
		*/
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				xfree(tmp_info);
			}
		}

		safe_unpack32(&object_ptr->priority, buffer);

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			void *used_limits = NULL;

			object_ptr->user_limit_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				unpack_acct_used_limits(&used_limits,
							rpc_version, buffer);
				list_append(object_ptr->user_limit_list,
					    used_limits);
			}
		}

	} else {
		safe_unpackstr_xmalloc(&object_ptr->description,
				       &uint32_tmp, buffer);
		safe_unpack32(&object_ptr->id, buffer);
		safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	destroy_acct_qos_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void pack_acct_reservation_rec(void *in, uint16_t rpc_version,
				      Buf buffer)
{
	acct_reservation_rec_t *object = (acct_reservation_rec_t *)in;

	if(!object) {
		pack64(0, buffer);
		packnull(buffer);
		packnull(buffer);
		pack32((uint32_t)NO_VAL, buffer);
		pack64(0, buffer);
		pack16((uint16_t)NO_VAL, buffer);
		pack32(0, buffer);
		packnull(buffer);
		packnull(buffer);
		packnull(buffer);
		pack_time(0, buffer);
		pack_time(0, buffer);
		pack_time(0, buffer);
		return;
	}
	
	pack64(object->alloc_secs, buffer);
	packstr(object->assocs, buffer);
	packstr(object->cluster, buffer);
	pack32(object->cpus, buffer);
	pack64(object->down_secs, buffer);
	pack16(object->flags, buffer);
	pack32(object->id, buffer);
	packstr(object->name, buffer);
	packstr(object->nodes, buffer);
	packstr(object->node_inx, buffer);
	pack_time(object->time_end, buffer);
	pack_time(object->time_start, buffer);	
	pack_time(object->time_start_prev, buffer);	
}

extern int unpack_acct_reservation_rec(void **object, uint16_t rpc_version,
				      Buf buffer)
{
	uint32_t uint32_tmp;
	acct_reservation_rec_t *object_ptr = 
		xmalloc(sizeof(acct_reservation_rec_t));

	*object = object_ptr;

	safe_unpack64(&object_ptr->alloc_secs, buffer);
	safe_unpackstr_xmalloc(&object_ptr->assocs, &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&object_ptr->cluster, &uint32_tmp, buffer);
	safe_unpack32(&object_ptr->cpus, buffer);
	safe_unpack64(&object_ptr->down_secs, buffer);
	safe_unpack16(&object_ptr->flags, buffer);
	safe_unpack32(&object_ptr->id, buffer);
	safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&object_ptr->nodes, &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&object_ptr->node_inx, &uint32_tmp, buffer);
	safe_unpack_time(&object_ptr->time_end, buffer);
	safe_unpack_time(&object_ptr->time_start, buffer);	
	safe_unpack_time(&object_ptr->time_start_prev, buffer);	

	return SLURM_SUCCESS;

unpack_error:
	destroy_acct_reservation_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void pack_acct_txn_rec(void *in, uint16_t rpc_version, Buf buffer)
{
	acct_txn_rec_t *object = (acct_txn_rec_t *)in;	
	
	if(rpc_version >= 3) {
		if(!object) {
			packnull(buffer);
			pack16(0, buffer);
			packnull(buffer);
			packnull(buffer);
			pack32(0, buffer);
			packnull(buffer);
			pack_time(0, buffer);
			packnull(buffer);
			packnull(buffer);
			return;
		}
	
		packstr(object->accts, buffer);
		pack16(object->action, buffer);
		packstr(object->actor_name, buffer);
		packstr(object->clusters, buffer);
		pack32(object->id, buffer);
		packstr(object->set_info, buffer);
		pack_time(object->timestamp, buffer);
		packstr(object->users, buffer);
		packstr(object->where_query, buffer);
	} else {
		if(!object) {
			pack16(0, buffer);
			packnull(buffer);
			pack32(0, buffer);
			packnull(buffer);
			pack_time(0, buffer);
			packnull(buffer);
			return;
		}
	
		pack16(object->action, buffer);
		packstr(object->actor_name, buffer);
		pack32(object->id, buffer);
		packstr(object->set_info, buffer);
		pack_time(object->timestamp, buffer);
		packstr(object->where_query, buffer);
	} 
}

extern int unpack_acct_txn_rec(void **object, uint16_t rpc_version, Buf buffer)
{
	uint32_t uint32_tmp;
	acct_txn_rec_t *object_ptr = xmalloc(sizeof(acct_txn_rec_t));

	*object = object_ptr;
	if (rpc_version >= 3) {
		safe_unpackstr_xmalloc(&object_ptr->accts, 
				       &uint32_tmp, buffer);
		safe_unpack16(&object_ptr->action, buffer);
		safe_unpackstr_xmalloc(&object_ptr->actor_name, 
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->clusters, 
				       &uint32_tmp, buffer);
		safe_unpack32(&object_ptr->id, buffer);
		safe_unpackstr_xmalloc(&object_ptr->set_info,
				       &uint32_tmp, buffer);
		safe_unpack_time(&object_ptr->timestamp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->users, 
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->where_query,
				       &uint32_tmp, buffer);		
	} else {
		safe_unpack16(&object_ptr->action, buffer);
		safe_unpackstr_xmalloc(&object_ptr->actor_name, 
				       &uint32_tmp, buffer);
		safe_unpack32(&object_ptr->id, buffer);
		safe_unpackstr_xmalloc(&object_ptr->set_info,
				       &uint32_tmp, buffer);
		safe_unpack_time(&object_ptr->timestamp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->where_query,
				       &uint32_tmp, buffer);
	} 
	return SLURM_SUCCESS;

unpack_error:
	destroy_acct_txn_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;

}

extern void pack_acct_wckey_rec(void *in, uint16_t rpc_version, Buf buffer)
{
	acct_accounting_rec_t *acct_info = NULL;
	ListIterator itr = NULL;
	uint32_t count = NO_VAL;
	acct_wckey_rec_t *object = (acct_wckey_rec_t *)in;	
	
	if(!object) {
		pack32(NO_VAL, buffer);

		packnull(buffer);

		pack32(NO_VAL, buffer);

		packnull(buffer);

		pack32(NO_VAL, buffer);

		packnull(buffer);
		return;
	}
 
	if(object->accounting_list)
		count = list_count(object->accounting_list);

	pack32(count, buffer);

	if(count && count != NO_VAL) {
		itr = list_iterator_create(object->accounting_list);
		while((acct_info = list_next(itr))) {
			pack_acct_accounting_rec(acct_info, 
						 rpc_version, buffer);
		}
		list_iterator_destroy(itr);
	}
	count = NO_VAL;

	packstr(object->cluster, buffer);

	pack32(object->id, buffer);

	packstr(object->name, buffer);	

	pack32(object->uid, buffer);

	packstr(object->user, buffer);	
}

extern int unpack_acct_wckey_rec(void **object, uint16_t rpc_version,
				 Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	acct_wckey_rec_t *object_ptr = 
		xmalloc(sizeof(acct_wckey_rec_t));
	acct_accounting_rec_t *acct_info = NULL;

	*object = object_ptr;

	safe_unpack32(&count, buffer);
	if(count != NO_VAL) {
		object_ptr->accounting_list =
			list_create(destroy_acct_accounting_rec);
		for(i=0; i<count; i++) {
			if(unpack_acct_accounting_rec(
				   (void **)&acct_info,
				   rpc_version, 
				   buffer) == SLURM_ERROR)
				goto unpack_error;
			list_append(object_ptr->accounting_list, 
				    acct_info);
		}
	}
	
	safe_unpackstr_xmalloc(&object_ptr->cluster, &uint32_tmp,
			       buffer);
	
	safe_unpack32(&object_ptr->id, buffer);

	safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);

	safe_unpack32(&object_ptr->uid, buffer);
	
	safe_unpackstr_xmalloc(&object_ptr->user, &uint32_tmp, buffer);
	
	return SLURM_SUCCESS;

unpack_error:
	destroy_acct_wckey_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void pack_acct_archive_rec(void *in, uint16_t rpc_version, Buf buffer)
{
	acct_archive_rec_t *object = (acct_archive_rec_t *)in;	
	
	if(!object) {
		packnull(buffer);
		packnull(buffer);
		return;
	}

	packstr(object->archive_file, buffer);
	packstr(object->insert, buffer);
}

extern int unpack_acct_archive_rec(void **object, uint16_t rpc_version,
				   Buf buffer)
{
	uint32_t uint32_tmp;
	acct_archive_rec_t *object_ptr = 
		xmalloc(sizeof(acct_archive_rec_t));

	*object = object_ptr;

	safe_unpackstr_xmalloc(&object_ptr->archive_file, &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&object_ptr->insert, &uint32_tmp, buffer);

	return SLURM_SUCCESS;

unpack_error:
	destroy_acct_archive_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;

}

extern void pack_acct_user_cond(void *in, uint16_t rpc_version, Buf buffer)
{
	char *tmp_info = NULL;
	ListIterator itr = NULL;
	acct_user_cond_t *object = (acct_user_cond_t *)in;
	uint32_t count = NO_VAL;

	if(rpc_version >= 4) {
		if(!object) {
			pack16(0, buffer);
			pack_acct_association_cond(NULL, rpc_version, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			return;
		}
 
		pack16(object->admin_level, buffer);

		pack_acct_association_cond(object->assoc_cond, 
					   rpc_version, buffer);
	
		if(object->def_acct_list)
			count = list_count(object->def_acct_list);

		pack32(count, buffer);

		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->def_acct_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->def_wckey_list)
			count = list_count(object->def_wckey_list);

		pack32(count, buffer);

		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->def_wckey_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		pack16(object->with_assocs, buffer);
		pack16(object->with_coords, buffer);
		pack16(object->with_deleted, buffer);
		pack16(object->with_wckeys, buffer);
	} else if(rpc_version >= 3) {
		if(!object) {
			pack16(0, buffer);
			pack_acct_association_cond(NULL, rpc_version, buffer);
			pack32(NO_VAL, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			return;
		}
 
		pack16(object->admin_level, buffer);

		pack_acct_association_cond(object->assoc_cond, 
					   rpc_version, buffer);
	
		if(object->def_acct_list)
			count = list_count(object->def_acct_list);

		pack32(count, buffer);

		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->def_acct_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		pack16(object->with_assocs, buffer);
		pack16(object->with_coords, buffer);
		pack16(object->with_deleted, buffer);
	} else {
		if(!object) {
			pack16(0, buffer);
			pack_acct_association_cond(NULL, rpc_version, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			return;
		}
 
		pack16(object->admin_level, buffer);

		pack_acct_association_cond(object->assoc_cond, 
					   rpc_version, buffer);
	
		if(object->def_acct_list)
			count = list_count(object->def_acct_list);

		pack32(count, buffer);

		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->def_acct_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		pack32(count, buffer); // NEEDED FOR OLD qos_list

		pack16(object->with_assocs, buffer);
		pack16(object->with_coords, buffer);
		pack16(object->with_deleted, buffer);
	} 
}

extern int unpack_acct_user_cond(void **object, uint16_t rpc_version, 
				 Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	acct_user_cond_t *object_ptr = xmalloc(sizeof(acct_user_cond_t));
	char *tmp_info = NULL;

	*object = object_ptr;

	if(rpc_version >= 4) {
		safe_unpack16(&object_ptr->admin_level, buffer);
		
		if(unpack_acct_association_cond(
			   (void **)&object_ptr->assoc_cond,
			   rpc_version, buffer) == SLURM_ERROR)
			goto unpack_error;
		
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->def_acct_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->def_acct_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->def_wckey_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->def_wckey_list,
					    tmp_info);
			}
		}
		safe_unpack16(&object_ptr->with_assocs, buffer);
		safe_unpack16(&object_ptr->with_coords, buffer);
		safe_unpack16(&object_ptr->with_deleted, buffer);
		safe_unpack16(&object_ptr->with_wckeys, buffer);
	} else if(rpc_version >= 3) {
		safe_unpack16(&object_ptr->admin_level, buffer);
		
		if(unpack_acct_association_cond(
			   (void **)&object_ptr->assoc_cond,
			   rpc_version, buffer) == SLURM_ERROR)
			goto unpack_error;
		
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->def_acct_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->def_acct_list,
					    tmp_info);
			}
		}
		safe_unpack16(&object_ptr->with_assocs, buffer);
		safe_unpack16(&object_ptr->with_coords, buffer);
		safe_unpack16(&object_ptr->with_deleted, buffer);
	} else {
		safe_unpack16(&object_ptr->admin_level, buffer);
		
		if(unpack_acct_association_cond(
			   (void **)&object_ptr->assoc_cond,
			   rpc_version, buffer) == SLURM_ERROR)
			goto unpack_error;
		
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->def_acct_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->def_acct_list,
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);

		safe_unpack16(&object_ptr->with_assocs, buffer);
		safe_unpack16(&object_ptr->with_coords, buffer);
		safe_unpack16(&object_ptr->with_deleted, buffer);
	} 
	return SLURM_SUCCESS;
		
unpack_error:
	destroy_acct_user_cond(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void pack_acct_account_cond(void *in, uint16_t rpc_version, Buf buffer)
{
	char *tmp_info = NULL;
	ListIterator itr = NULL;
	acct_account_cond_t *object = (acct_account_cond_t *)in;
	uint32_t count = NO_VAL;

	if(rpc_version >= 3) {
		if(!object) {
			pack_acct_association_cond(NULL, rpc_version, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			return;
		}
		pack_acct_association_cond(object->assoc_cond,
					   rpc_version, buffer);
		
		count = NO_VAL;
		if(object->description_list)
			count = list_count(object->description_list);
		
		pack32(count, buffer);
		
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->description_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;
		
		if(object->organization_list)
			count = list_count(object->organization_list);
		
		pack32(count, buffer);
		
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->organization_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;
		
		pack16(object->with_assocs, buffer);
		pack16(object->with_coords, buffer);
		pack16(object->with_deleted, buffer);		
	} else {
		if(!object) {
			pack_acct_association_cond(NULL, rpc_version, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			return;
		}
		pack_acct_association_cond(object->assoc_cond,
					   rpc_version, buffer);
		
		count = NO_VAL;
		if(object->description_list)
			count = list_count(object->description_list);
		
		pack32(count, buffer);
		
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->description_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;
		
		if(object->organization_list)
			count = list_count(object->organization_list);
		
		pack32(count, buffer);
		
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->organization_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;
		
		pack32(count, buffer);
		
		pack16(object->with_assocs, buffer);
		pack16(object->with_coords, buffer);
		pack16(object->with_deleted, buffer);
	} 
}

extern int unpack_acct_account_cond(void **object, uint16_t rpc_version,
				    Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	acct_account_cond_t *object_ptr = xmalloc(sizeof(acct_account_cond_t));
	char *tmp_info = NULL;

	*object = object_ptr;

	if (rpc_version >= 3) {
		if(unpack_acct_association_cond(
			   (void **)&object_ptr->assoc_cond,
			   rpc_version, buffer) == SLURM_ERROR)
			goto unpack_error;
		
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->description_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->description_list,
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->organization_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->organization_list,
					    tmp_info);
			}
		}

		safe_unpack16(&object_ptr->with_assocs, buffer);
		safe_unpack16(&object_ptr->with_coords, buffer);
		safe_unpack16(&object_ptr->with_deleted, buffer);
	} else {
		if(unpack_acct_association_cond(
			   (void **)&object_ptr->assoc_cond,
			   rpc_version, buffer) == SLURM_ERROR)
			goto unpack_error;
		
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->description_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->description_list,
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->organization_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->organization_list,
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		safe_unpack16(&object_ptr->with_assocs, buffer);
		safe_unpack16(&object_ptr->with_coords, buffer);
		safe_unpack16(&object_ptr->with_deleted, buffer);
	} 
	return SLURM_SUCCESS;

unpack_error:
	destroy_acct_account_cond(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void pack_acct_cluster_cond(void *in, uint16_t rpc_version, Buf buffer)
{
	char *tmp_info = NULL;
	ListIterator itr = NULL;
	acct_cluster_cond_t *object = (acct_cluster_cond_t *)in;
	uint32_t count = NO_VAL;

	if(rpc_version >= 5) {
		if(!object) {
			pack16(0, buffer);
			pack32(NO_VAL, buffer);
			pack_time(0, buffer);
			pack_time(0, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			return;
		}

		pack16(object->classification, buffer);
		
		if(object->cluster_list)
			count = list_count(object->cluster_list);
		
		pack32(count, buffer);
		
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->cluster_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;
		
		pack_time(object->usage_end, buffer);
		pack_time(object->usage_start, buffer);
		
		pack16(object->with_usage, buffer);
		pack16(object->with_deleted, buffer);
	} else {
		if(!object) {
			pack32(NO_VAL, buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			return;
		}
		
		if(object->cluster_list)
			count = list_count(object->cluster_list);
		
		pack32(count, buffer);
		
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->cluster_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;
		
		pack32(object->usage_end, buffer);
		pack32(object->usage_start, buffer);
		
		pack16(object->with_usage, buffer);
		pack16(object->with_deleted, buffer);
	} 
}

extern int unpack_acct_cluster_cond(void **object, uint16_t rpc_version, 
				    Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	acct_cluster_cond_t *object_ptr = xmalloc(sizeof(acct_cluster_cond_t));
	char *tmp_info = NULL;

	*object = object_ptr;

	if(rpc_version >= 5) {
		safe_unpack16(&object_ptr->classification, buffer);
		safe_unpack32(&count, buffer);
		if(count && count != NO_VAL) {
			object_ptr->cluster_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->cluster_list, tmp_info);
			}
		}
		safe_unpack_time(&object_ptr->usage_end, buffer);
		safe_unpack_time(&object_ptr->usage_start, buffer);

		safe_unpack16(&object_ptr->with_usage, buffer);
		safe_unpack16(&object_ptr->with_deleted, buffer);
	} else {
		safe_unpack32(&count, buffer);
		if(count && count != NO_VAL) {
			object_ptr->cluster_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->cluster_list, tmp_info);
			}
		}
		safe_unpack32(&uint32_tmp, buffer);
		object_ptr->usage_end = uint32_tmp;
		safe_unpack32(&uint32_tmp, buffer);
		object_ptr->usage_start = uint32_tmp;

		safe_unpack16(&object_ptr->with_usage, buffer);
		safe_unpack16(&object_ptr->with_deleted, buffer);
	}
	return SLURM_SUCCESS;

unpack_error:
	destroy_acct_cluster_cond(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void pack_acct_association_cond(void *in, uint16_t rpc_version, 
				       Buf buffer)
{
	char *tmp_info = NULL;
	uint32_t count = NO_VAL;

	ListIterator itr = NULL;
	acct_association_cond_t *object = (acct_association_cond_t *)in;

	if(rpc_version >= 5) {
		if(!object) {
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);

			pack32(NO_VAL, buffer);

			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);

			pack32(NO_VAL, buffer);

			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);

			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			
			pack32(NO_VAL, buffer);

			pack_time(0, buffer);
			pack_time(0, buffer);

			pack32(NO_VAL, buffer);

			pack16(0, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			return;
		}

		if(object->acct_list)
			count = list_count(object->acct_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->acct_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->cluster_list)
			count = list_count(object->cluster_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->cluster_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->fairshare_list)
			count = list_count(object->fairshare_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->fairshare_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->grp_cpu_mins_list)
			count = list_count(object->grp_cpu_mins_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->grp_cpu_mins_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->grp_cpus_list)
			count = list_count(object->grp_cpus_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->grp_cpus_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->grp_jobs_list)
			count = list_count(object->grp_jobs_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->grp_jobs_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->grp_nodes_list)
			count = list_count(object->grp_nodes_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->grp_nodes_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->grp_submit_jobs_list)
			count = list_count(object->grp_submit_jobs_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(
				object->grp_submit_jobs_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->grp_wall_list)
			count = list_count(object->grp_wall_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->grp_wall_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->id_list)
			count = list_count(object->id_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->id_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
		}
		count = NO_VAL;

		if(object->max_cpu_mins_pj_list)
			count = list_count(object->max_cpu_mins_pj_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(
				object->max_cpu_mins_pj_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;
		if(object->max_cpus_pj_list)
			count = list_count(object->max_cpus_pj_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->max_cpus_pj_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;
		if(object->max_jobs_list)
			count = list_count(object->max_jobs_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->max_jobs_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;
		if(object->max_nodes_pj_list)
			count = list_count(object->max_nodes_pj_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->max_nodes_pj_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;
		if(object->max_submit_jobs_list)
			count = list_count(object->max_submit_jobs_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(
				object->max_submit_jobs_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;
		if(object->max_wall_pj_list)
			count = list_count(object->max_wall_pj_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->max_wall_pj_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;
	
		if(object->partition_list)
			count = list_count(object->partition_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->partition_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->parent_acct_list)
			count = list_count(object->parent_acct_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->parent_acct_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->qos_list)
			count = list_count(object->qos_list);

		pack32(count, buffer);

		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->qos_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		pack_time(object->usage_end, buffer);
		pack_time(object->usage_start, buffer);

		if(object->user_list)
			count = list_count(object->user_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->user_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		pack16(object->with_usage, buffer);
		pack16(object->with_deleted, buffer);
		pack16(object->with_raw_qos, buffer);
		pack16(object->with_sub_accts, buffer);
		pack16(object->without_parent_info, buffer);
		pack16(object->without_parent_limits, buffer);
	} else if(rpc_version >= 3) {
		if(!object) {
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);

			pack32(NO_VAL, buffer);

			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);

			pack32(NO_VAL, buffer);

			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);

			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			
			pack32(NO_VAL, buffer);

			pack32(0, buffer);
			pack32(0, buffer);

			pack32(NO_VAL, buffer);

			pack16(0, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			return;
		}

		if(object->acct_list)
			count = list_count(object->acct_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->acct_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->cluster_list)
			count = list_count(object->cluster_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->cluster_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->fairshare_list)
			count = list_count(object->fairshare_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->fairshare_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->grp_cpu_mins_list)
			count = list_count(object->grp_cpu_mins_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->grp_cpu_mins_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->grp_cpus_list)
			count = list_count(object->grp_cpus_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->grp_cpus_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->grp_jobs_list)
			count = list_count(object->grp_jobs_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->grp_jobs_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->grp_nodes_list)
			count = list_count(object->grp_nodes_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->grp_nodes_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->grp_submit_jobs_list)
			count = list_count(object->grp_submit_jobs_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(
				object->grp_submit_jobs_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->grp_wall_list)
			count = list_count(object->grp_wall_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->grp_wall_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->id_list)
			count = list_count(object->id_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->id_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
		}
		count = NO_VAL;

		if(object->max_cpu_mins_pj_list)
			count = list_count(object->max_cpu_mins_pj_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(
				object->max_cpu_mins_pj_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;
		if(object->max_cpus_pj_list)
			count = list_count(object->max_cpus_pj_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->max_cpus_pj_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;
		if(object->max_jobs_list)
			count = list_count(object->max_jobs_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->max_jobs_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;
		if(object->max_nodes_pj_list)
			count = list_count(object->max_nodes_pj_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->max_nodes_pj_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;
		if(object->max_submit_jobs_list)
			count = list_count(object->max_submit_jobs_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(
				object->max_submit_jobs_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;
		if(object->max_wall_pj_list)
			count = list_count(object->max_wall_pj_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->max_wall_pj_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;
	
		if(object->partition_list)
			count = list_count(object->partition_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->partition_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->parent_acct_list)
			count = list_count(object->parent_acct_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->parent_acct_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->qos_list)
			count = list_count(object->qos_list);

		pack32(count, buffer);

		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->qos_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		pack32(object->usage_end, buffer);
		pack32(object->usage_start, buffer);

		if(object->user_list)
			count = list_count(object->user_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->user_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		pack16(object->with_usage, buffer);
		pack16(object->with_deleted, buffer);
		pack16(object->with_raw_qos, buffer);
		pack16(object->with_sub_accts, buffer);
		pack16(object->without_parent_info, buffer);
		pack16(object->without_parent_limits, buffer);
	} else {
		if(!object) {
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			packnull(buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			pack32(NO_VAL, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			return;
		}

		if(object->acct_list)
			count = list_count(object->acct_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->acct_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->cluster_list)
			count = list_count(object->cluster_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->cluster_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->fairshare_list 
		   && list_count(object->fairshare_list)) 
			pack32(atoi(list_peek(object->fairshare_list)), 
			       buffer);
		else 
			pack32(count, buffer);
	
		if(object->id_list)
			count = list_count(object->id_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->id_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
		}
		count = NO_VAL;
		
		if(object->max_cpu_mins_pj_list
		   && list_count(object->max_cpu_mins_pj_list)) 
			pack32(atoi(list_peek(object->max_cpu_mins_pj_list)), 
			       buffer);
		else 
			pack32(count, buffer);
		
		if(object->max_jobs_list && list_count(object->max_jobs_list)) 
			pack32(atoi(list_peek(object->max_jobs_list)), 
			       buffer);
		else 
			pack32(count, buffer);

		if(object->max_nodes_pj_list
		   && list_count(object->max_nodes_pj_list)) 
			pack32(atoi(list_peek(object->max_nodes_pj_list)), 
			       buffer);
		else 
			pack32(count, buffer);

		if(object->max_wall_pj_list 
		   && list_count(object->max_wall_pj_list)) 
			pack32(atoi(list_peek(object->max_wall_pj_list)), 
			       buffer);
		else 
			pack32(count, buffer);

		if(object->partition_list)
			count = list_count(object->partition_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->partition_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->parent_acct_list 
		   && list_count(object->parent_acct_list)) 
			packstr(list_peek(object->parent_acct_list), 
			       buffer);
		else 
			packnull(buffer);

		pack32(object->usage_end, buffer);
		pack32(object->usage_start, buffer);

		if(object->user_list)
			count = list_count(object->user_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->user_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		pack16(object->with_usage, buffer);
		pack16(object->with_deleted, buffer);
		pack16(object->without_parent_info, buffer);
		pack16(object->without_parent_limits, buffer);
	} 
}

extern int unpack_acct_association_cond(void **object, 
					uint16_t rpc_version, Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	acct_association_cond_t *object_ptr =
		xmalloc(sizeof(acct_association_cond_t));
	char *tmp_info = NULL;
	*object = object_ptr;

	if(rpc_version >= 5) {
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->acct_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->acct_list, tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->cluster_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->cluster_list, 
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->fairshare_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->fairshare_list, 
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->grp_cpu_mins_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->grp_cpu_mins_list, 
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->grp_cpus_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->grp_cpus_list, 
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->grp_jobs_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->grp_jobs_list, 
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->grp_nodes_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->grp_nodes_list,
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->grp_submit_jobs_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->grp_submit_jobs_list, 
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->grp_wall_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->grp_wall_list, 
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->id_list = list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp, 
						       buffer);
				list_append(object_ptr->id_list, tmp_info);
			}
		}
	
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->max_cpu_mins_pj_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->max_cpu_mins_pj_list,
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->max_cpus_pj_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->max_cpus_pj_list,
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->max_jobs_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->max_jobs_list,
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->max_nodes_pj_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->max_nodes_pj_list,
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->max_submit_jobs_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->max_submit_jobs_list, 
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->max_wall_pj_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->max_wall_pj_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->partition_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->partition_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->parent_acct_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->parent_acct_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->qos_list = list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->qos_list, tmp_info);
			}
		}

		safe_unpack_time(&object_ptr->usage_end, buffer);
		safe_unpack_time(&object_ptr->usage_start, buffer);

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->user_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->user_list, tmp_info);
			}
		}

		safe_unpack16(&object_ptr->with_usage, buffer);
		safe_unpack16(&object_ptr->with_deleted, buffer);
		safe_unpack16(&object_ptr->with_raw_qos, buffer);
		safe_unpack16(&object_ptr->with_sub_accts, buffer);
		safe_unpack16(&object_ptr->without_parent_info, buffer);
		safe_unpack16(&object_ptr->without_parent_limits, buffer);
	} else if(rpc_version >= 3) {
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->acct_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->acct_list, tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->cluster_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->cluster_list, 
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->fairshare_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->fairshare_list, 
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->grp_cpu_mins_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->grp_cpu_mins_list, 
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->grp_cpus_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->grp_cpus_list, 
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->grp_jobs_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->grp_jobs_list, 
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->grp_nodes_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->grp_nodes_list,
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->grp_submit_jobs_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->grp_submit_jobs_list, 
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->grp_wall_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->grp_wall_list, 
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->id_list = list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp, 
						       buffer);
				list_append(object_ptr->id_list, tmp_info);
			}
		}
	
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->max_cpu_mins_pj_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->max_cpu_mins_pj_list,
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->max_cpus_pj_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->max_cpus_pj_list,
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->max_jobs_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->max_jobs_list,
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->max_nodes_pj_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->max_nodes_pj_list,
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->max_submit_jobs_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->max_submit_jobs_list, 
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->max_wall_pj_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->max_wall_pj_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->partition_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->partition_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->parent_acct_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->parent_acct_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->qos_list = list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->qos_list, tmp_info);
			}
		}

		safe_unpack32(&uint32_tmp, buffer);
		object_ptr->usage_end = uint32_tmp;
		safe_unpack32(&uint32_tmp, buffer);
		object_ptr->usage_start = uint32_tmp;

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->user_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->user_list, tmp_info);
			}
		}

		safe_unpack16(&object_ptr->with_usage, buffer);
		safe_unpack16(&object_ptr->with_deleted, buffer);
		safe_unpack16(&object_ptr->with_raw_qos, buffer);
		safe_unpack16(&object_ptr->with_sub_accts, buffer);
		safe_unpack16(&object_ptr->without_parent_info, buffer);
		safe_unpack16(&object_ptr->without_parent_limits, buffer);
	} else {
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->acct_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->acct_list, tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->cluster_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->cluster_list,
					    tmp_info);
			}
		}
		/* We have to check for 0 here because of a bug in
		   version 2 that sent 0's when it should had sent
		   NO_VAL
		*/
		safe_unpack32(&count, buffer);
		if(count && count != NO_VAL) {
			object_ptr->fairshare_list = 
				list_create(slurm_destroy_char);
			list_append(object_ptr->fairshare_list,
				    xstrdup_printf("%u", count));
		}

		safe_unpack32(&count, buffer);
		if(count && count != NO_VAL) {
			object_ptr->id_list = list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp, 
						       buffer);
				list_append(object_ptr->id_list, tmp_info);
			}
		}
	
		safe_unpack32(&count, buffer);
		if(count && count != NO_VAL) {
			object_ptr->max_cpu_mins_pj_list = 
				list_create(slurm_destroy_char);
			list_append(object_ptr->max_cpu_mins_pj_list,
				    xstrdup_printf("%u", count));
		}

		safe_unpack32(&count, buffer);
		if(count && count != NO_VAL) {
			object_ptr->max_jobs_list = 
				list_create(slurm_destroy_char);
			list_append(object_ptr->max_jobs_list,
				    xstrdup_printf("%u", count));
		}

		safe_unpack32(&count, buffer);
		if(count && count != NO_VAL) {
			object_ptr->max_nodes_pj_list = 
				list_create(slurm_destroy_char);
			list_append(object_ptr->max_nodes_pj_list,
				    xstrdup_printf("%u", count));
		}

		safe_unpack32(&count, buffer);
		if(count && count != NO_VAL) {
			object_ptr->max_wall_pj_list = 
				list_create(slurm_destroy_char);
			list_append(object_ptr->max_wall_pj_list,
				    xstrdup_printf("%u", count));
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->partition_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->partition_list,
					    tmp_info);
			}
		}

		safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp, buffer);
		if(tmp_info) {
			object_ptr->parent_acct_list = 
				list_create(slurm_destroy_char);
			list_append(object_ptr->parent_acct_list, tmp_info);
		}

		safe_unpack32(&uint32_tmp, buffer);
		object_ptr->usage_end = uint32_tmp;
		safe_unpack32(&uint32_tmp, buffer);
		object_ptr->usage_start = uint32_tmp;

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->user_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->user_list, tmp_info);
			}
		}

		safe_unpack16(&object_ptr->with_usage, buffer);
		safe_unpack16(&object_ptr->with_deleted, buffer);
		safe_unpack16(&object_ptr->without_parent_info, buffer);
		safe_unpack16(&object_ptr->without_parent_limits, buffer);
	} 

	return SLURM_SUCCESS;

unpack_error:
	destroy_acct_association_cond(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void pack_acct_job_cond(void *in, uint16_t rpc_version, Buf buffer)
{
	char *tmp_info = NULL;
	jobacct_selected_step_t *job = NULL;
	uint32_t count = NO_VAL;

	ListIterator itr = NULL;
	acct_job_cond_t *object = (acct_job_cond_t *)in;

	if(rpc_version >= 5) {
		if(!object) {
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack16(0, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack_time(0, buffer);
			pack_time(0, buffer);
			packnull(buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			return;
		}

		if(object->acct_list)
			count = list_count(object->acct_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->acct_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->associd_list)
			count = list_count(object->associd_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->associd_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
		}
		count = NO_VAL;

		if(object->cluster_list)
			count = list_count(object->cluster_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->cluster_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		pack16(object->duplicates, buffer);

		if(object->groupid_list)
			count = list_count(object->groupid_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->groupid_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
		}
		count = NO_VAL;
	
		if(object->partition_list)
			count = list_count(object->partition_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->partition_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->resv_list)
			count = list_count(object->resv_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->resv_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->resvid_list)
			count = list_count(object->resvid_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->resvid_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->step_list)
			count = list_count(object->step_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->step_list);
			while((job = list_next(itr))) {
				pack_jobacct_selected_step(job, rpc_version, 
							   buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->state_list)
			count = list_count(object->state_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->state_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		pack_time(object->usage_end, buffer);
		pack_time(object->usage_start, buffer);

		packstr(object->used_nodes, buffer);

		if(object->userid_list)
			count = list_count(object->userid_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->userid_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->wckey_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		pack16(object->without_steps, buffer);
		pack16(object->without_usage_truncation, buffer);
	} else if(rpc_version >= 4) {
		if(!object) {
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack16(0, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack16(0, buffer);
			return;
		}

		if(object->acct_list)
			count = list_count(object->acct_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->acct_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->associd_list)
			count = list_count(object->associd_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->associd_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
		}
		count = NO_VAL;

		if(object->cluster_list)
			count = list_count(object->cluster_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->cluster_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		pack16(object->duplicates, buffer);

		if(object->groupid_list)
			count = list_count(object->groupid_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->groupid_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
		}
		count = NO_VAL;
	
		if(object->partition_list)
			count = list_count(object->partition_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->partition_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->step_list)
			count = list_count(object->step_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->step_list);
			while((job = list_next(itr))) {
				pack_jobacct_selected_step(job, rpc_version, 
							   buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->state_list)
			count = list_count(object->state_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->state_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		pack32(object->usage_end, buffer);
		pack32(object->usage_start, buffer);

		if(object->userid_list)
			count = list_count(object->userid_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->userid_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->wckey_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		pack16(object->without_steps, buffer);
	} else {
		if(!object) {
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack16(0, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			pack32(NO_VAL, buffer);
			pack16(0, buffer);
			return;
		}

		if(object->acct_list)
			count = list_count(object->acct_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->acct_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->associd_list)
			count = list_count(object->associd_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->associd_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
		}
		count = NO_VAL;

		if(object->cluster_list)
			count = list_count(object->cluster_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->cluster_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		pack16(object->duplicates, buffer);

		if(object->groupid_list)
			count = list_count(object->groupid_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->groupid_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
		}
		count = NO_VAL;
	
		if(object->partition_list)
			count = list_count(object->partition_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->partition_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->step_list)
			count = list_count(object->step_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->step_list);
			while((job = list_next(itr))) {
				pack_jobacct_selected_step(job, rpc_version,
							   buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->state_list)
			count = list_count(object->state_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->state_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		pack32(object->usage_end, buffer);
		pack32(object->usage_start, buffer);

		if(object->userid_list)
			count = list_count(object->userid_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->userid_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		pack16(object->without_steps, buffer);
	}
}

extern int unpack_acct_job_cond(void **object, uint16_t rpc_version, Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	acct_job_cond_t *object_ptr = xmalloc(sizeof(acct_job_cond_t));
	char *tmp_info = NULL;
	jobacct_selected_step_t *job = NULL;

	*object = object_ptr;

	if(rpc_version >= 5) {
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->acct_list = list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->acct_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->associd_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->associd_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->cluster_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->cluster_list, tmp_info);
			}
		}

		safe_unpack16(&object_ptr->duplicates, buffer);

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->groupid_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp, 
						       buffer);
				list_append(object_ptr->groupid_list, tmp_info);
			}
		}
	
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->partition_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->partition_list, 
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->resv_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->resv_list, 
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->resvid_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->resvid_list, 
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->step_list =
				list_create(destroy_jobacct_selected_step);
			for(i=0; i<count; i++) {
				unpack_jobacct_selected_step(&job, rpc_version,
							     buffer);
				list_append(object_ptr->step_list, job);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->state_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->state_list, tmp_info);
			}
		}
	
		safe_unpack_time(&object_ptr->usage_end, buffer);
		safe_unpack_time(&object_ptr->usage_start, buffer);

		safe_unpackstr_xmalloc(&object_ptr->used_nodes,
				       &uint32_tmp, buffer);

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->userid_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->userid_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->wckey_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->wckey_list, tmp_info);
			}
		}

		safe_unpack16(&object_ptr->without_steps, buffer);
		safe_unpack16(&object_ptr->without_usage_truncation, buffer);
	} else if(rpc_version >= 4) {
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->acct_list = list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->acct_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->associd_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->associd_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->cluster_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->cluster_list, tmp_info);
			}
		}

		safe_unpack16(&object_ptr->duplicates, buffer);

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->groupid_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp, 
						       buffer);
				list_append(object_ptr->groupid_list, tmp_info);
			}
		}
	
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->partition_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->partition_list, 
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->step_list =
				list_create(destroy_jobacct_selected_step);
			for(i=0; i<count; i++) {
				unpack_jobacct_selected_step(&job, rpc_version,
							     buffer);
				list_append(object_ptr->step_list, job);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->state_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->state_list, tmp_info);
			}
		}
	
		safe_unpack32(&uint32_tmp, buffer);
		object_ptr->usage_end = uint32_tmp;
		safe_unpack32(&uint32_tmp, buffer);
		object_ptr->usage_start = uint32_tmp;

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->userid_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->userid_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->wckey_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->wckey_list, tmp_info);
			}
		}

		safe_unpack16(&object_ptr->without_steps, buffer);
	} else {
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->acct_list = list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->acct_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->associd_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->associd_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->cluster_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->cluster_list, tmp_info);
			}
		}

		safe_unpack16(&object_ptr->duplicates, buffer);

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->groupid_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp, 
						       buffer);
				list_append(object_ptr->groupid_list, tmp_info);
			}
		}
	
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->partition_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->partition_list, 
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->step_list =
				list_create(destroy_jobacct_selected_step);
			for(i=0; i<count; i++) {
				unpack_jobacct_selected_step(&job, rpc_version,
							     buffer);
				list_append(object_ptr->step_list, job);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->state_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->state_list, tmp_info);
			}
		}
	
		safe_unpack32(&uint32_tmp, buffer);
		object_ptr->usage_end = uint32_tmp;
		safe_unpack32(&uint32_tmp, buffer);
		object_ptr->usage_start = uint32_tmp;

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->userid_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->userid_list, tmp_info);
			}
		}

		safe_unpack16(&object_ptr->without_steps, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	destroy_acct_job_cond(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void pack_acct_qos_cond(void *in, uint16_t rpc_version, Buf buffer)
{
	uint32_t count = NO_VAL;
	char *tmp_info = NULL;
	ListIterator itr = NULL;
	acct_qos_cond_t *object = (acct_qos_cond_t *)in;

	if(!object) {
		pack32(NO_VAL, buffer);
		pack32(NO_VAL, buffer);
		pack32(NO_VAL, buffer);
		pack16(0, buffer);
		return;
	}

	if(object->description_list)
		count = list_count(object->description_list);
	
	pack32(count, buffer);
	if(count && count != NO_VAL) {
		itr = list_iterator_create(object->description_list);
		while((tmp_info = list_next(itr))) {
			packstr(tmp_info, buffer);
		}
		list_iterator_destroy(itr);
	}
	count = NO_VAL;

	if(object->id_list)
		count = list_count(object->id_list);
	
	pack32(count, buffer);
	if(count && count != NO_VAL) {
		itr = list_iterator_create(object->id_list);
		while((tmp_info = list_next(itr))) {
			packstr(tmp_info, buffer);
		}
		list_iterator_destroy(itr);
	}
	count = NO_VAL;

	if(object->name_list) 
		count = list_count(object->name_list);

	pack32(count, buffer);
	if(count && count != NO_VAL) {
		itr = list_iterator_create(object->name_list);
		while((tmp_info = list_next(itr))) {
			packstr(tmp_info, buffer);
		}
		list_iterator_destroy(itr); 
	}
	count = NO_VAL;

	pack16(object->with_deleted, buffer);
}

extern int unpack_acct_qos_cond(void **object, uint16_t rpc_version, Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	acct_qos_cond_t *object_ptr = xmalloc(sizeof(acct_qos_cond_t));
	char *tmp_info = NULL;

	*object = object_ptr;

	safe_unpack32(&count, buffer);
	if(count != NO_VAL) {
		object_ptr->description_list = list_create(slurm_destroy_char);
		for(i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp, buffer);
			list_append(object_ptr->description_list, tmp_info);
		}
	}

	safe_unpack32(&count, buffer);
	if(count != NO_VAL) {
		object_ptr->id_list = list_create(slurm_destroy_char);
		for(i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp, buffer);
			list_append(object_ptr->id_list, tmp_info);
		}
	}

	safe_unpack32(&count, buffer);
	if(count != NO_VAL) {
		object_ptr->name_list = list_create(slurm_destroy_char);
		for(i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp, buffer);
			list_append(object_ptr->name_list, tmp_info);
		}
	}

	safe_unpack16(&object_ptr->with_deleted, buffer);
	return SLURM_SUCCESS;

unpack_error:
	destroy_acct_qos_cond(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void pack_acct_reservation_cond(void *in, uint16_t rpc_version,
				      Buf buffer)
{
	acct_reservation_cond_t *object = (acct_reservation_cond_t *)in;
	uint32_t count = NO_VAL;
	ListIterator itr = NULL;
	char *tmp_info = NULL;

	if(!object) {
		pack32((uint32_t)NO_VAL, buffer);
		pack16(0, buffer);
		pack32((uint16_t)NO_VAL, buffer);
		pack32((uint16_t)NO_VAL, buffer);
		packnull(buffer);
		pack_time(0, buffer);
		pack_time(0, buffer);
		pack16(0, buffer);
		return;
	}
	
	if(object->cluster_list)
		count = list_count(object->cluster_list);
	
	pack32(count, buffer);
	if(count && count != NO_VAL) {
		itr = list_iterator_create(object->cluster_list);
		while((tmp_info = list_next(itr))) {
			packstr(tmp_info, buffer);
		}
		list_iterator_destroy(itr);
	}
	count = NO_VAL;

	pack16(object->flags, buffer);
	
	if(object->id_list)
		count = list_count(object->id_list);
	
	pack32(count, buffer);
	if(count && count != NO_VAL) {
		itr = list_iterator_create(object->id_list);
		while((tmp_info = list_next(itr))) {
			packstr(tmp_info, buffer);
		}
		list_iterator_destroy(itr);
	}
	count = NO_VAL;

	if(object->name_list)
		count = list_count(object->name_list);
	
	pack32(count, buffer);
	if(count && count != NO_VAL) {
		itr = list_iterator_create(object->name_list);
		while((tmp_info = list_next(itr))) {
			packstr(tmp_info, buffer);
		}
		list_iterator_destroy(itr);
	}
	count = NO_VAL;

	packstr(object->nodes, buffer);
	pack_time(object->time_end, buffer);
	pack_time(object->time_start, buffer);	
	pack16(object->with_usage, buffer);	
}

extern int unpack_acct_reservation_cond(void **object, uint16_t rpc_version,
				      Buf buffer)
{
	uint32_t uint32_tmp, count;
	int i = 0;
	char *tmp_info = NULL;
	acct_reservation_cond_t *object_ptr = 
		xmalloc(sizeof(acct_reservation_cond_t));

	*object = object_ptr;

	safe_unpack32(&count, buffer);
	if(count != NO_VAL) {
		object_ptr->cluster_list = list_create(slurm_destroy_char);
		for(i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp, buffer);
			list_append(object_ptr->cluster_list, tmp_info);
		}
	}

	safe_unpack16(&object_ptr->flags, buffer);

	safe_unpack32(&count, buffer);
	if(count != NO_VAL) {
		object_ptr->id_list = list_create(slurm_destroy_char);
		for(i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp, buffer);
			list_append(object_ptr->id_list, tmp_info);
		}
	}

	safe_unpack32(&count, buffer);
	if(count != NO_VAL) {
		object_ptr->name_list = list_create(slurm_destroy_char);
		for(i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp, buffer);
			list_append(object_ptr->name_list, tmp_info);
		}
	}

	safe_unpackstr_xmalloc(&object_ptr->nodes, &uint32_tmp, buffer);
	safe_unpack_time(&object_ptr->time_end, buffer);
	safe_unpack_time(&object_ptr->time_start, buffer);	
	safe_unpack16(&object_ptr->with_usage, buffer);	

	return SLURM_SUCCESS;

unpack_error:
	destroy_acct_reservation_cond(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void pack_acct_txn_cond(void *in, uint16_t rpc_version, Buf buffer)
{
	uint32_t count = NO_VAL;
	char *tmp_info = NULL;
	ListIterator itr = NULL;
	acct_txn_cond_t *object = (acct_txn_cond_t *)in;

	if(rpc_version >= 5) {
		if(!object) {
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack_time(0, buffer);
			pack_time(0, buffer);
			pack32(NO_VAL, buffer);
			pack16(0, buffer);
			return;
		}
		if(object->acct_list)
			count = list_count(object->acct_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->acct_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->action_list)
			count = list_count(object->action_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->action_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->actor_list) 
			count = list_count(object->actor_list);

		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->actor_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr); 
		}
		count = NO_VAL;

		if(object->cluster_list)
			count = list_count(object->cluster_list);
	 
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->cluster_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			} 
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->id_list)
			count = list_count(object->id_list);
	 
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->id_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			} 
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->info_list)
			count = list_count(object->info_list);
	 
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->info_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			} 
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->name_list)
			count = list_count(object->name_list);
	 
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->name_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			} 
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		pack_time(object->time_end, buffer);
		pack_time(object->time_start, buffer);
		if(object->user_list)
			count = list_count(object->user_list);
	 
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->user_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			} 
			list_iterator_destroy(itr);
		}
		count = NO_VAL;
		
		pack16(object->with_assoc_info, buffer);
	} else if(rpc_version >= 3) {
		if(!object) {
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			pack32(NO_VAL, buffer);
			pack16(0, buffer);
			return;
		}
		if(object->acct_list)
			count = list_count(object->acct_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->acct_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->action_list)
			count = list_count(object->action_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->action_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->actor_list) 
			count = list_count(object->actor_list);

		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->actor_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr); 
		}
		count = NO_VAL;

		if(object->cluster_list)
			count = list_count(object->cluster_list);
	 
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->cluster_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			} 
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->id_list)
			count = list_count(object->id_list);
	 
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->id_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			} 
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->info_list)
			count = list_count(object->info_list);
	 
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->info_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			} 
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->name_list)
			count = list_count(object->name_list);
	 
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->name_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			} 
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		pack32(object->time_end, buffer);
		pack32(object->time_start, buffer);
		if(object->user_list)
			count = list_count(object->user_list);
	 
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->user_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			} 
			list_iterator_destroy(itr);
		}
		count = NO_VAL;
		
		pack16(object->with_assoc_info, buffer);
	} else {
		if(!object) {
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			return;
		}
		if(object->action_list)
			count = list_count(object->action_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->action_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->actor_list) 
			count = list_count(object->actor_list);

		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->actor_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr); 
		}
		count = NO_VAL;

		if(object->id_list)
			count = list_count(object->id_list);
	 
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->id_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			} 
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		pack32(object->time_end, buffer);
		pack32(object->time_start, buffer);
	} 
}

extern int unpack_acct_txn_cond(void **object, uint16_t rpc_version, Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	acct_txn_cond_t *object_ptr = xmalloc(sizeof(acct_txn_cond_t));
	char *tmp_info = NULL;

	*object = object_ptr;
	if (rpc_version >= 5) {
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->acct_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->acct_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->action_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->action_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->actor_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->actor_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->cluster_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->cluster_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->id_list = list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->id_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->info_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->info_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->name_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->name_list, tmp_info);
			}
		}

		safe_unpack_time(&object_ptr->time_end, buffer);
		safe_unpack_time(&object_ptr->time_start, buffer);

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->user_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->user_list, tmp_info);
			}
		}

		safe_unpack16(&object_ptr->with_assoc_info, buffer);
	} else if (rpc_version >= 3) {
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->acct_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->acct_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->action_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->action_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->actor_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->actor_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->cluster_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->cluster_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->id_list = list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->id_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->info_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->info_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->name_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->name_list, tmp_info);
			}
		}

		safe_unpack32(&uint32_tmp, buffer);
		object_ptr->time_end = uint32_tmp;
		safe_unpack32(&uint32_tmp, buffer);
		object_ptr->time_start = uint32_tmp;

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->user_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->user_list, tmp_info);
			}
		}

		safe_unpack16(&object_ptr->with_assoc_info, buffer);
	} else {
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->action_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->action_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->actor_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->actor_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->id_list = list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->id_list, tmp_info);
			}
		}

		safe_unpack32(&uint32_tmp, buffer);
		object_ptr->time_end = uint32_tmp;
		safe_unpack32(&uint32_tmp, buffer);
		object_ptr->time_start = uint32_tmp;
	}

	return SLURM_SUCCESS;

unpack_error:
	destroy_acct_txn_cond(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void pack_acct_wckey_cond(void *in, uint16_t rpc_version, Buf buffer)
{
	char *tmp_info = NULL;
	uint32_t count = NO_VAL;

	ListIterator itr = NULL;
	acct_wckey_cond_t *object = (acct_wckey_cond_t *)in;
	if(rpc_version >= 5) {
		if(!object) {
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);

			pack_time(0, buffer);
			pack_time(0, buffer);

			pack32(NO_VAL, buffer);

			pack16(0, buffer);
			pack16(0, buffer);
			return;
		}

		if(object->cluster_list)
			count = list_count(object->cluster_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->cluster_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->id_list)
			count = list_count(object->id_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->id_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
		}
		count = NO_VAL;

		if(object->name_list)
			count = list_count(object->name_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->name_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		pack_time(object->usage_end, buffer);
		pack_time(object->usage_start, buffer);

		if(object->user_list)
			count = list_count(object->user_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->user_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		pack16(object->with_usage, buffer);
		pack16(object->with_deleted, buffer);
	} else {
		if(!object) {
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);

			pack32(0, buffer);
			pack32(0, buffer);

			pack32(NO_VAL, buffer);

			pack16(0, buffer);
			pack16(0, buffer);
			return;
		}

		if(object->cluster_list)
			count = list_count(object->cluster_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->cluster_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if(object->id_list)
			count = list_count(object->id_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->id_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
		}
		count = NO_VAL;

		if(object->name_list)
			count = list_count(object->name_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->name_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		pack32(object->usage_end, buffer);
		pack32(object->usage_start, buffer);

		if(object->user_list)
			count = list_count(object->user_list);
	
		pack32(count, buffer);
		if(count && count != NO_VAL) {
			itr = list_iterator_create(object->user_list);
			while((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		pack16(object->with_usage, buffer);
		pack16(object->with_deleted, buffer);
	}
}

extern int unpack_acct_wckey_cond(void **object, uint16_t rpc_version,
				  Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	acct_wckey_cond_t *object_ptr =	xmalloc(sizeof(acct_wckey_cond_t));
	char *tmp_info = NULL;

	*object = object_ptr;

	if(rpc_version >= 5) {
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->cluster_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->cluster_list, 
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->id_list = list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp, 
						       buffer);
				list_append(object_ptr->id_list, tmp_info);
			}
		}
	
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->name_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->name_list, tmp_info);
			}
		}

		safe_unpack_time(&object_ptr->usage_end, buffer);
		safe_unpack_time(&object_ptr->usage_start, buffer);

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->user_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->user_list, tmp_info);
			}
		}

		safe_unpack16(&object_ptr->with_usage, buffer);
		safe_unpack16(&object_ptr->with_deleted, buffer);
	} else {
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->cluster_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->cluster_list, 
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->id_list = list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp, 
						       buffer);
				list_append(object_ptr->id_list, tmp_info);
			}
		}
	
		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->name_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->name_list, tmp_info);
			}
		}

		safe_unpack32(&uint32_tmp, buffer);
		object_ptr->usage_end = uint32_tmp;
		safe_unpack32(&uint32_tmp, buffer);
		object_ptr->usage_start = uint32_tmp;

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->user_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->user_list, tmp_info);
			}
		}

		safe_unpack16(&object_ptr->with_usage, buffer);
		safe_unpack16(&object_ptr->with_deleted, buffer);
	}
	return SLURM_SUCCESS;

unpack_error:
	destroy_acct_wckey_cond(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void pack_acct_archive_cond(void *in, uint16_t rpc_version, Buf buffer)
{
	acct_archive_cond_t *object = (acct_archive_cond_t *)in;	
	
	if(!object) {
		packnull(buffer);
		pack16((uint16_t)NO_VAL, buffer);
		pack16((uint16_t)NO_VAL, buffer);
		packnull(buffer);
		pack16((uint16_t)NO_VAL, buffer);
		pack16((uint16_t)NO_VAL, buffer);
		pack_acct_job_cond(NULL, rpc_version, buffer);
		pack16((uint16_t)NO_VAL, buffer);
		pack16((uint16_t)NO_VAL, buffer);
		pack16((uint16_t)NO_VAL, buffer);
		pack16((uint16_t)NO_VAL, buffer);
		return;
	}

	packstr(object->archive_dir, buffer);
	pack16(object->archive_events, buffer);
	pack16(object->archive_jobs, buffer);
	packstr(object->archive_script, buffer);
	pack16(object->archive_steps, buffer);
	pack16(object->archive_suspend, buffer);
	pack_acct_job_cond(object->job_cond, rpc_version, buffer);
	pack16(object->purge_event, buffer);
	pack16(object->purge_job, buffer);
	pack16(object->purge_step, buffer);
	pack16(object->purge_suspend, buffer);
}

extern int unpack_acct_archive_cond(void **object, uint16_t rpc_version,
				   Buf buffer)
{
	uint32_t uint32_tmp;
	acct_archive_cond_t *object_ptr = 
		xmalloc(sizeof(acct_archive_cond_t));

	*object = object_ptr;

	safe_unpackstr_xmalloc(&object_ptr->archive_dir, &uint32_tmp, buffer);
	safe_unpack16(&object_ptr->archive_events, buffer);
	safe_unpack16(&object_ptr->archive_jobs, buffer);
	safe_unpackstr_xmalloc(&object_ptr->archive_script,
			       &uint32_tmp, buffer);
	safe_unpack16(&object_ptr->archive_steps, buffer);
	safe_unpack16(&object_ptr->archive_suspend, buffer);
	if(unpack_acct_job_cond((void *)&object_ptr->job_cond,
				rpc_version, buffer) == SLURM_ERROR)
		goto unpack_error;
	safe_unpack16(&object_ptr->purge_event, buffer);
	safe_unpack16(&object_ptr->purge_job, buffer);
	safe_unpack16(&object_ptr->purge_step, buffer);
	safe_unpack16(&object_ptr->purge_suspend, buffer);

	return SLURM_SUCCESS;

unpack_error:
	destroy_acct_archive_cond(object_ptr);
	*object = NULL;
	return SLURM_ERROR;

}

extern void pack_acct_update_object(acct_update_object_t *object,
				    uint16_t rpc_version, Buf buffer)
{
	uint32_t count = NO_VAL;
	ListIterator itr = NULL;
	void *acct_object = NULL;
	void (*my_function) (void *object, uint16_t rpc_version, Buf buffer);

	switch(object->type) {
	case ACCT_MODIFY_USER:
	case ACCT_ADD_USER:
	case ACCT_REMOVE_USER:
	case ACCT_ADD_COORD:
	case ACCT_REMOVE_COORD:
		my_function = pack_acct_user_rec;
		break;
	case ACCT_ADD_ASSOC:
	case ACCT_MODIFY_ASSOC:
	case ACCT_REMOVE_ASSOC:
		my_function = pack_acct_association_rec;
		break;
	case ACCT_ADD_QOS:
	case ACCT_MODIFY_QOS:
	case ACCT_REMOVE_QOS:
		my_function = pack_acct_qos_rec;
		break;
	case ACCT_ADD_WCKEY:
	case ACCT_MODIFY_WCKEY:
	case ACCT_REMOVE_WCKEY:
		if(rpc_version <= 3) {
			/* since this wasn't introduced before version
			   4 pack a known type with NO_VAL as the count */
			pack16(ACCT_MODIFY_USER, buffer);
			pack32(count, buffer);
			return;
		}
		my_function = pack_acct_wckey_rec;
		break;
	case ACCT_UPDATE_NOTSET:
	default:
		error("pack: unknown type set in update_object: %d",
		      object->type);
		return;
	}

	pack16(object->type, buffer);
	if(object->objects) 
		count = list_count(object->objects);
			
	pack32(count, buffer);
	if(count && count != NO_VAL) {
		itr = list_iterator_create(object->objects);
		while((acct_object = list_next(itr))) {
			(*(my_function))(acct_object, rpc_version, buffer);
		}
		list_iterator_destroy(itr);
	}
}

extern int unpack_acct_update_object(acct_update_object_t **object, 
				     uint16_t rpc_version, Buf buffer)
{
	int i;
	uint32_t count;
	acct_update_object_t *object_ptr = 
		xmalloc(sizeof(acct_update_object_t));
	void *acct_object = NULL;
	int (*my_function) (void **object, uint16_t rpc_version, Buf buffer);
	void (*my_destroy) (void *object);

	*object = object_ptr;

	safe_unpack16(&object_ptr->type, buffer);
	switch(object_ptr->type) {
	case ACCT_MODIFY_USER:
	case ACCT_ADD_USER:
	case ACCT_REMOVE_USER:
	case ACCT_ADD_COORD:
	case ACCT_REMOVE_COORD:
		my_function = unpack_acct_user_rec;
		my_destroy = destroy_acct_user_rec;
		break;
	case ACCT_ADD_ASSOC:
	case ACCT_MODIFY_ASSOC:
	case ACCT_REMOVE_ASSOC:
		my_function = unpack_acct_association_rec;
		my_destroy = destroy_acct_association_rec;
		break;
	case ACCT_ADD_QOS:
	case ACCT_MODIFY_QOS:
	case ACCT_REMOVE_QOS:
		my_function = unpack_acct_qos_rec;
		my_destroy = destroy_acct_qos_rec;
		break;
	case ACCT_ADD_WCKEY:
	case ACCT_MODIFY_WCKEY:
	case ACCT_REMOVE_WCKEY:
		my_function = unpack_acct_wckey_rec;
		my_destroy = destroy_acct_wckey_rec;
		break;
	case ACCT_UPDATE_NOTSET:
	default:
		error("unpack: unknown type set in update_object: %d",
		      object_ptr->type);
		goto unpack_error;
	}
	safe_unpack32(&count, buffer);
	if(count != NO_VAL) {
		object_ptr->objects = list_create((*(my_destroy)));
		for(i=0; i<count; i++) {
			if(((*(my_function))(&acct_object, rpc_version, buffer))
			   == SLURM_ERROR)
				goto unpack_error;
			list_append(object_ptr->objects, acct_object);
		}
	}
	return SLURM_SUCCESS;
	
unpack_error:
	destroy_acct_update_object(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern char *acct_qos_str(List qos_list, uint32_t level)
{
	ListIterator itr = NULL;
	acct_qos_rec_t *qos = NULL;
	
	if(!qos_list) {
		error("We need a qos list to translate");
		return NULL;
	} else if(!level) {
		debug2("no level");
		return "";
	}

	itr = list_iterator_create(qos_list);
	while((qos = list_next(itr))) {
		if(level == qos->id)
			break;
	}
	list_iterator_destroy(itr);
	if(qos)
		return qos->name;
	else
		return NULL;
}

extern uint32_t str_2_acct_qos(List qos_list, char *level)
{
	ListIterator itr = NULL;
	acct_qos_rec_t *qos = NULL;
	char *working_level = NULL;

	if(!qos_list) {
		error("We need a qos list to translate");
		return NO_VAL;
	} else if(!level) {
		debug2("no level");
		return 0;
	}
	if(level[0] == '+' || level[0] == '-')
		working_level = level+1;
	else
		working_level = level;

	itr = list_iterator_create(qos_list);
	while((qos = list_next(itr))) {
		if(!strncasecmp(working_level, qos->name, 
				strlen(working_level)))
			break;
	}
	list_iterator_destroy(itr);
	if(qos)
		return qos->id;
	else
		return NO_VAL;
}

extern char *acct_admin_level_str(acct_admin_level_t level)
{
	switch(level) {
	case ACCT_ADMIN_NOTSET:
		return "Not Set";
		break;
	case ACCT_ADMIN_NONE:
		return "None";
		break;
	case ACCT_ADMIN_OPERATOR:
		return "Operator";
		break;
	case ACCT_ADMIN_SUPER_USER:
		return "Administrator";
		break;
	default:
		return "Unknown";
		break;
	}
	return "Unknown";
}

extern acct_admin_level_t str_2_acct_admin_level(char *level)
{
	if(!level) {
		return ACCT_ADMIN_NOTSET;
	} else if(!strncasecmp(level, "None", 1)) {
		return ACCT_ADMIN_NONE;
	} else if(!strncasecmp(level, "Operator", 1)) {
		return ACCT_ADMIN_OPERATOR;
	} else if(!strncasecmp(level, "SuperUser", 1) 
		  || !strncasecmp(level, "Admin", 1)) {
		return ACCT_ADMIN_SUPER_USER;
	} else {
		return ACCT_ADMIN_NOTSET;		
	}	
}

/* This reorders the list into a alphabetical hierarchy returned in a
 * separate list.  The orginal list is not affected */
extern List get_hierarchical_sorted_assoc_list(List assoc_list)
{
	List acct_hierarchical_rec_list =
		get_acct_hierarchical_rec_list(assoc_list);
	List ret_list = list_create(NULL);

	_append_hierarchical_childern_ret_list(ret_list,
					       acct_hierarchical_rec_list);
	list_destroy(acct_hierarchical_rec_list);
	
	return ret_list;
}

extern List get_acct_hierarchical_rec_list(List assoc_list)
{
	acct_hierarchical_rec_t *par_arch_rec = NULL;
	acct_hierarchical_rec_t *last_acct_parent = NULL;
	acct_hierarchical_rec_t *last_parent = NULL;
	acct_hierarchical_rec_t *arch_rec = NULL;
	acct_association_rec_t *assoc = NULL;
	List total_assoc_list = list_create(NULL);
	List arch_rec_list = 
		list_create(destroy_acct_hierarchical_rec);
	ListIterator itr, itr2;

	itr = list_iterator_create(assoc_list);
	itr2 = list_iterator_create(total_assoc_list);
	
	while((assoc = list_next(itr))) {
		arch_rec = 
			xmalloc(sizeof(acct_hierarchical_rec_t));
		arch_rec->childern = 
			list_create(destroy_acct_hierarchical_rec);
		arch_rec->assoc = assoc;
	
		/* To speed things up we are first looking if we have
		   a parent_id to look for.  If that doesn't work see
		   if the last parent we had was what we are looking
		   for.  Then if that isn't panning out look at the
		   last account parent.  If still we don't have it we
		   will look for it in the list.  If it isn't there we
		   will just add it to the parent and call it good 
		*/
		if(!assoc->parent_id) {
			arch_rec->sort_name = assoc->cluster;

			list_append(arch_rec_list, arch_rec);
			list_append(total_assoc_list, arch_rec);

			continue;
		} 
		
		if(assoc->user) 
			arch_rec->sort_name = assoc->user;
		else 
			arch_rec->sort_name = assoc->acct;		

		if(last_parent && assoc->parent_id == last_parent->assoc->id) {
			par_arch_rec = last_parent;
		} else if(last_acct_parent 
			  && assoc->parent_id == last_acct_parent->assoc->id) {
			par_arch_rec = last_acct_parent;
		} else {
			list_iterator_reset(itr2);
			while((par_arch_rec = list_next(itr2))) {
				if(assoc->parent_id 
				   == par_arch_rec->assoc->id) {
					if(assoc->user) 
						last_parent = par_arch_rec;	
					else 
						last_parent 
							= last_acct_parent
							= par_arch_rec;
					break;
				}
			}
		}

		if(!par_arch_rec) {
			list_append(arch_rec_list, arch_rec);
			last_parent = last_acct_parent = arch_rec;
		} else 
			list_append(par_arch_rec->childern, arch_rec);
		
		list_append(total_assoc_list, arch_rec);
	}
	list_iterator_destroy(itr);
	list_iterator_destroy(itr2);

	list_destroy(total_assoc_list);
//	info("got %d", list_count(arch_rec_list));
	_sort_acct_hierarchical_rec_list(arch_rec_list);

	return arch_rec_list;
}

/* IN/OUT: tree_list a list of acct_print_tree_t's */ 
extern char *get_tree_acct_name(char *name, char *parent, List tree_list)
{
	ListIterator itr = NULL;
	acct_print_tree_t *acct_print_tree = NULL;
	acct_print_tree_t *par_acct_print_tree = NULL;

	if(!tree_list) 
		return NULL;
		
	itr = list_iterator_create(tree_list);
	while((acct_print_tree = list_next(itr))) {
		/* we don't care about users in this list.  They are
		   only there so we don't leak memory */
		if(acct_print_tree->user)
			continue;

		if(!strcmp(name, acct_print_tree->name))
			break;
		else if(parent && !strcmp(parent, acct_print_tree->name)) 
			par_acct_print_tree = acct_print_tree;
		
	}
	list_iterator_destroy(itr);
	
	if(parent && acct_print_tree) 
		return acct_print_tree->print_name;
	
	acct_print_tree = xmalloc(sizeof(acct_print_tree_t));
	acct_print_tree->name = xstrdup(name);
	if(par_acct_print_tree) 
		acct_print_tree->spaces =
			xstrdup_printf(" %s", par_acct_print_tree->spaces);
	else 
		acct_print_tree->spaces = xstrdup("");
	
	/* user account */
	if(name[0] == '|') {
		acct_print_tree->print_name = xstrdup_printf(
			"%s%s", acct_print_tree->spaces, parent);	
		acct_print_tree->user = 1;
	} else 
		acct_print_tree->print_name = xstrdup_printf(
			"%s%s", acct_print_tree->spaces, name);	
	
	list_append(tree_list, acct_print_tree);
	
	return acct_print_tree->print_name;
}

extern int set_qos_bitstr_from_list(bitstr_t *valid_qos, List qos_list)
{
	ListIterator itr = NULL;
	bitoff_t bit = 0;
	int rc = SLURM_SUCCESS;
	char *temp_char = NULL;
	void (*my_function) (bitstr_t *b, bitoff_t bit);

	xassert(valid_qos);

	if(!qos_list)
		return SLURM_ERROR;

	itr = list_iterator_create(qos_list);
	while((temp_char = list_next(itr))) {
		if(temp_char[0] == '-') {
			temp_char++;
			my_function = bit_clear;
		} else if(temp_char[0] == '+') {
			temp_char++;
			my_function = bit_set;
		} else
			my_function = bit_set;
		bit = atoi(temp_char);
		if(bit >= bit_size(valid_qos)) {
			rc = SLURM_ERROR;
			break;
		}
		(*(my_function))(valid_qos, bit);
	}
	list_iterator_destroy(itr);
	
	return rc;
}

extern char *get_qos_complete_str_bitstr(List qos_list, bitstr_t *valid_qos)
{
	List temp_list = NULL;
	char *temp_char = NULL;
	char *print_this = NULL;
	ListIterator itr = NULL;
	int i = 0;

	if(!qos_list || !list_count(qos_list)
	   || !valid_qos || (bit_ffs(valid_qos) == -1))
		return xstrdup("");

	temp_list = list_create(NULL);

	for(i=0; i<bit_size(valid_qos); i++) {
		if(!bit_test(valid_qos, i))
			continue;
		if((temp_char = acct_qos_str(qos_list, i)))
			list_append(temp_list, temp_char);
	}
	list_sort(temp_list, (ListCmpF)slurm_sort_char_list_asc);
	itr = list_iterator_create(temp_list);
	while((temp_char = list_next(itr))) {
		if(print_this) 
			xstrfmtcat(print_this, ",%s", temp_char);
		else 
			print_this = xstrdup(temp_char);
	}
	list_iterator_destroy(itr);
	list_destroy(temp_list);

	if(!print_this)
		return xstrdup("");

	return print_this;
}

extern char *get_qos_complete_str(List qos_list, List num_qos_list)
{
	List temp_list = NULL;
	char *temp_char = NULL;
	char *print_this = NULL;
	ListIterator itr = NULL;
	int option = 0;

	if(!qos_list || !list_count(qos_list)
	   || !num_qos_list || !list_count(num_qos_list))
		return xstrdup("");

	temp_list = list_create(slurm_destroy_char);

	itr = list_iterator_create(num_qos_list);
	while((temp_char = list_next(itr))) {
		option = 0;
		if(temp_char[0] == '+' || temp_char[0] == '-') {
			option = temp_char[0];
			temp_char++;
		}
		temp_char = acct_qos_str(qos_list, atoi(temp_char));
		if(temp_char) {
			if(option) 
				list_append(temp_list, xstrdup_printf(
						    "%c%s", option, temp_char));
			else 
				list_append(temp_list, xstrdup(temp_char));
		}
	}
	list_iterator_destroy(itr);
	list_sort(temp_list, (ListCmpF)slurm_sort_char_list_asc);
	itr = list_iterator_create(temp_list);
	while((temp_char = list_next(itr))) {
		if(print_this) 
			xstrfmtcat(print_this, ",%s", temp_char);
		else 
			print_this = xstrdup(temp_char);
	}
	list_iterator_destroy(itr);
	list_destroy(temp_list);

	if(!print_this)
		return xstrdup("");

	return print_this;
}

extern char *get_classification_str(uint16_t class)
{
	bool classified = class & ACCT_CLASSIFIED_FLAG;
	acct_classification_type_t type = class & ACCT_CLASS_BASE;

	switch(type) {
	case ACCT_CLASS_NONE:
		return NULL;
		break;
	case ACCT_CLASS_CAPACITY:
		if(classified)
			return "*Capacity";
		else
			return "Capacity";
		break;
	case ACCT_CLASS_CAPABILITY:
		if(classified)
			return "*Capability";
		else
			return "Capability";
		break;
	case ACCT_CLASS_CAPAPACITY:
		if(classified)
			return "*Capapacity";
		else
			return "Capapacity";
		break;
	default:
		if(classified)
			return "*Unknown";
		else
			return "Unknown";
		break;
	}
}

extern uint16_t str_2_classification(char *class)
{
	uint16_t type = 0;
	if(!class)
		return type;

	if(slurm_strcasestr(class, "capac"))
		type = ACCT_CLASS_CAPACITY;
	else if(slurm_strcasestr(class, "capab"))
		type = ACCT_CLASS_CAPABILITY;
	else if(slurm_strcasestr(class, "capap"))
		type = ACCT_CLASS_CAPAPACITY;
	
	if(slurm_strcasestr(class, "*")) 
		type |= ACCT_CLASSIFIED_FLAG; 
	else if(slurm_strcasestr(class, "class")) 
		type |= ACCT_CLASSIFIED_FLAG;
	
	return type;
}

extern char *get_acct_problem_str(uint16_t problem)
{
	acct_problem_type_t type = problem;

	switch(type) {
	case ACCT_PROBLEM_NOT_SET:
		return NULL;
		break;
	case ACCT_PROBLEM_ACCT_NO_ASSOC:
		return "Account has no Associations";
		break;
	case ACCT_PROBLEM_ACCT_NO_USERS:
		return "Account has no users";
		break;
	case ACCT_PROBLEM_USER_NO_ASSOC:
		return "User has no Associations";
		break;
	case ACCT_PROBLEM_USER_NO_UID:
		return "User does not have a uid";
		break;
	default:
		return "Unknown";
		break;
	}
}

extern uint16_t str_2_acct_problem(char *problem)
{
	uint16_t type = 0;

	if(!problem)
		return type;

	if(slurm_strcasestr(problem, "account no associations"))
		type = ACCT_PROBLEM_USER_NO_ASSOC;
	else if(slurm_strcasestr(problem, "account no users"))
		type = ACCT_PROBLEM_ACCT_NO_USERS;
	else if(slurm_strcasestr(problem, "user no associations"))
		type = ACCT_PROBLEM_USER_NO_ASSOC;
	else if(slurm_strcasestr(problem, "user no uid"))
		type = ACCT_PROBLEM_USER_NO_UID;
       
	return type;
}

extern void log_assoc_rec(acct_association_rec_t *assoc_ptr, List qos_list)
{
	xassert(assoc_ptr);

	debug2("association rec id : %u", assoc_ptr->id);
	debug2("  acct             : %s", assoc_ptr->acct);
	debug2("  cluster          : %s", assoc_ptr->cluster);

	if(assoc_ptr->shares_raw == INFINITE)
		debug2("  RawShares        : NONE");
	else if(assoc_ptr->shares_raw != NO_VAL) 
		debug2("  RawShares        : %u", assoc_ptr->shares_raw);

	if(assoc_ptr->shares_norm != (double)NO_VAL) 
		debug2("  NormalizedShares : %f", assoc_ptr->shares_norm);

	if(assoc_ptr->level_shares != NO_VAL) 
		debug2("  LevelShares      : %u", assoc_ptr->level_shares);

	if(assoc_ptr->grp_cpu_mins == INFINITE)
		debug2("  GrpCPUMins       : NONE");
	else if(assoc_ptr->grp_cpu_mins != NO_VAL) 
		debug2("  GrpCPUMins       : %llu", assoc_ptr->grp_cpu_mins);
		
	if(assoc_ptr->grp_cpus == INFINITE)
		debug2("  GrpCPUs          : NONE");
	else if(assoc_ptr->grp_cpus != NO_VAL) 
		debug2("  GrpCPUs          : %u", assoc_ptr->grp_cpus);
				
	if(assoc_ptr->grp_jobs == INFINITE) 
		debug2("  GrpJobs          : NONE");
	else if(assoc_ptr->grp_jobs != NO_VAL) 
		debug2("  GrpJobs          : %u", assoc_ptr->grp_jobs);
		
	if(assoc_ptr->grp_nodes == INFINITE)
		debug2("  GrpNodes         : NONE");
	else if(assoc_ptr->grp_nodes != NO_VAL)
		debug2("  GrpNodes         : %u", assoc_ptr->grp_nodes);
		
	if(assoc_ptr->grp_submit_jobs == INFINITE) 
		debug2("  GrpSubmitJobs    : NONE");
	else if(assoc_ptr->grp_submit_jobs != NO_VAL) 
		debug2("  GrpSubmitJobs    : %u", assoc_ptr->grp_submit_jobs);
		
	if(assoc_ptr->grp_wall == INFINITE) 
		debug2("  GrpWall          : NONE");		
	else if(assoc_ptr->grp_wall != NO_VAL) {
		char time_buf[32];
		mins2time_str((time_t) assoc_ptr->grp_wall, 
			      time_buf, sizeof(time_buf));
		debug2("  GrpWall          : %s", time_buf);
	}

	if(assoc_ptr->max_cpu_mins_pj == INFINITE)
		debug2("  MaxCPUMins       : NONE");
	else if(assoc_ptr->max_cpu_mins_pj != NO_VAL) 
		debug2("  MaxCPUMins       : %llu", assoc_ptr->max_cpu_mins_pj);
		
	if(assoc_ptr->max_cpus_pj == INFINITE)
		debug2("  MaxCPUs          : NONE");
	else if(assoc_ptr->max_cpus_pj != NO_VAL) 
		debug2("  MaxCPUs          : %u", assoc_ptr->max_cpus_pj);
				
	if(assoc_ptr->max_jobs == INFINITE) 
		debug2("  MaxJobs          : NONE");
	else if(assoc_ptr->max_jobs != NO_VAL) 
		debug2("  MaxJobs          : %u", assoc_ptr->max_jobs);
		
	if(assoc_ptr->max_nodes_pj == INFINITE)
		debug2("  MaxNodes         : NONE");
	else if(assoc_ptr->max_nodes_pj != NO_VAL)
		debug2("  MaxNodes         : %u", assoc_ptr->max_nodes_pj);
		
	if(assoc_ptr->max_submit_jobs == INFINITE) 
		debug2("  MaxSubmitJobs    : NONE");
	else if(assoc_ptr->max_submit_jobs != NO_VAL) 
		debug2("  MaxSubmitJobs    : %u", assoc_ptr->max_submit_jobs);
		
	if(assoc_ptr->max_wall_pj == INFINITE) 
		debug2("  MaxWall          : NONE");		
	else if(assoc_ptr->max_wall_pj != NO_VAL) {
		char time_buf[32];
		mins2time_str((time_t) assoc_ptr->max_wall_pj, 
			      time_buf, sizeof(time_buf));
		debug2("  MaxWall          : %s", time_buf);
	}

	if(assoc_ptr->qos_list) {
		char *temp_char = get_qos_complete_str(qos_list,
						       assoc_ptr->qos_list);
		if(temp_char) {		
			debug2("  Qos              : %s", temp_char);
			xfree(temp_char);
		}
	} else {
		debug2("  Qos              : %s", "Normal");
	}

	if(assoc_ptr->parent_acct)
		debug2("  ParentAccount    : %s", assoc_ptr->parent_acct);
	if(assoc_ptr->partition)
		debug2("  Partition        : %s", assoc_ptr->partition);
	if(assoc_ptr->user)
		debug2("  User             : %s(%u)",
		       assoc_ptr->user, assoc_ptr->uid);
	debug2("  UsedJobs        : %u", assoc_ptr->used_jobs);
	debug2("  RawUsage        : %Lf", assoc_ptr->usage_raw);
}

/*
 * Initialize context for acct_storage plugin
 */
extern int slurm_acct_storage_init(char *loc)
{
	int retval = SLURM_SUCCESS;
	char *acct_storage_type = NULL;
	
	slurm_mutex_lock( &g_acct_storage_context_lock );

	if ( g_acct_storage_context )
		goto done;
	if(loc)
		slurm_set_accounting_storage_loc(loc);
	
	acct_storage_type = slurm_get_accounting_storage_type();
	
	g_acct_storage_context = _acct_storage_context_create(
		acct_storage_type);
	if ( g_acct_storage_context == NULL ) {
		error( "cannot create acct_storage context for %s",
		       acct_storage_type );
		retval = SLURM_ERROR;
		goto done;
	}

	if ( _acct_storage_get_ops( g_acct_storage_context ) == NULL ) {
		error( "cannot resolve acct_storage plugin operations" );
		_acct_storage_context_destroy( g_acct_storage_context );
		g_acct_storage_context = NULL;
		retval = SLURM_ERROR;
	}

done:
	slurm_mutex_unlock( &g_acct_storage_context_lock );
	xfree(acct_storage_type);
	return retval;
}

extern int slurm_acct_storage_fini(void)
{
	int rc;

	if (!g_acct_storage_context)
		return SLURM_SUCCESS;

//	(*(g_acct_storage_context->ops.acct_storage_fini))();
	rc = _acct_storage_context_destroy( g_acct_storage_context );
	g_acct_storage_context = NULL;
	return rc;
}

extern void *acct_storage_g_get_connection(bool make_agent, int conn_num,
					   bool rollback)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.get_conn))(
		make_agent, conn_num, rollback);
}

extern int acct_storage_g_close_connection(void **db_conn)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.close_conn))(db_conn);

}

extern int acct_storage_g_commit(void *db_conn, bool commit)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.commit))(db_conn, commit);

}

extern int acct_storage_g_add_users(void *db_conn, uint32_t uid,
				    List user_list)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.add_users))
		(db_conn, uid, user_list);
}

extern int acct_storage_g_add_coord(void *db_conn, uint32_t uid,
				    List acct_list, acct_user_cond_t *user_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.add_coord))
		(db_conn, uid, acct_list, user_cond);
}

extern int acct_storage_g_add_accounts(void *db_conn, uint32_t uid,
				       List acct_list)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.add_accts))
		(db_conn, uid, acct_list);
}

extern int acct_storage_g_add_clusters(void *db_conn, uint32_t uid,
				       List cluster_list)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.add_clusters))
		(db_conn, uid, cluster_list);
}

extern int acct_storage_g_add_associations(void *db_conn, uint32_t uid,
					   List association_list)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.add_associations))
		(db_conn, uid, association_list);
}

extern int acct_storage_g_add_qos(void *db_conn, uint32_t uid,
				  List qos_list)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.add_qos))
		(db_conn, uid, qos_list);
}

extern int acct_storage_g_add_wckeys(void *db_conn, uint32_t uid,
				     List wckey_list)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.add_wckeys))
		(db_conn, uid, wckey_list);
}

extern int acct_storage_g_add_reservation(void *db_conn,
					   acct_reservation_rec_t *resv)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NO_VAL;
	return (*(g_acct_storage_context->ops.add_reservation))
		(db_conn, resv);
}

extern List acct_storage_g_modify_users(void *db_conn, uint32_t uid,
					acct_user_cond_t *user_cond,
					acct_user_rec_t *user)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.modify_users))
		(db_conn, uid, user_cond, user);
}

extern List acct_storage_g_modify_accounts(void *db_conn, uint32_t uid,
					   acct_account_cond_t *acct_cond,
					   acct_account_rec_t *acct)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.modify_accts))
		(db_conn, uid, acct_cond, acct);
}

extern List acct_storage_g_modify_clusters(void *db_conn, uint32_t uid,
					   acct_cluster_cond_t *cluster_cond,
					   acct_cluster_rec_t *cluster)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.modify_clusters))
		(db_conn, uid, cluster_cond, cluster);
}

extern List acct_storage_g_modify_associations(
	void *db_conn, uint32_t uid,
	acct_association_cond_t *assoc_cond,
	acct_association_rec_t *assoc)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.modify_associations))
		(db_conn, uid, assoc_cond, assoc);
}

extern List acct_storage_g_modify_qos(void *db_conn, uint32_t uid,
				      acct_qos_cond_t *qos_cond,
				      acct_qos_rec_t *qos)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.modify_qos))
		(db_conn, uid, qos_cond, qos);
}

extern List acct_storage_g_modify_wckeys(void *db_conn, uint32_t uid,
					 acct_wckey_cond_t *wckey_cond,
					 acct_wckey_rec_t *wckey)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.modify_wckeys))
		(db_conn, uid, wckey_cond, wckey);
}

extern int acct_storage_g_modify_reservation(void *db_conn,
					   acct_reservation_rec_t *resv)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NO_VAL;
	return (*(g_acct_storage_context->ops.modify_reservation))
		(db_conn, resv);
}

extern List acct_storage_g_remove_users(void *db_conn, uint32_t uid,
					acct_user_cond_t *user_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.remove_users))
		(db_conn, uid, user_cond);
}

extern List acct_storage_g_remove_coord(void *db_conn, uint32_t uid,
					List acct_list,
					acct_user_cond_t *user_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.remove_coord))
		(db_conn, uid, acct_list, user_cond);
}

extern List acct_storage_g_remove_accounts(void *db_conn, uint32_t uid,
					   acct_account_cond_t *acct_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.remove_accts))
		(db_conn, uid, acct_cond);
}

extern List acct_storage_g_remove_clusters(void *db_conn, uint32_t uid,
					   acct_cluster_cond_t *cluster_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.remove_clusters))
		(db_conn, uid, cluster_cond);
}

extern List acct_storage_g_remove_associations(
	void *db_conn, uint32_t uid,
	acct_association_cond_t *assoc_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.remove_associations))
		(db_conn, uid, assoc_cond);
}

extern List acct_storage_g_remove_qos(void *db_conn, uint32_t uid,
				      acct_qos_cond_t *qos_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.remove_qos))
		(db_conn, uid, qos_cond);
}

extern List acct_storage_g_remove_wckeys(void *db_conn, uint32_t uid,
					 acct_wckey_cond_t *wckey_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.remove_wckeys))
		(db_conn, uid, wckey_cond);
}

extern int acct_storage_g_remove_reservation(void *db_conn,
					     acct_reservation_rec_t *resv)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NO_VAL;
	return (*(g_acct_storage_context->ops.remove_reservation))
		(db_conn, resv);
}

extern List acct_storage_g_get_users(void *db_conn, uint32_t uid,
				     acct_user_cond_t *user_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.get_users))
		(db_conn, uid, user_cond);
}

extern List acct_storage_g_get_accounts(void *db_conn, uint32_t uid,
					acct_account_cond_t *acct_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.get_accts))
		(db_conn, uid, acct_cond);
}

extern List acct_storage_g_get_clusters(void *db_conn, uint32_t uid,
					acct_cluster_cond_t *cluster_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.get_clusters))
		(db_conn, uid, cluster_cond);
}

extern List acct_storage_g_get_config(void *db_conn)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.get_config))(db_conn);
}

extern List acct_storage_g_get_associations(void *db_conn, uint32_t uid,
					    acct_association_cond_t *assoc_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.get_associations))
		(db_conn, uid, assoc_cond);
}

extern List acct_storage_g_get_problems(void *db_conn, uint32_t uid,
					acct_association_cond_t *assoc_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.get_problems))
		(db_conn, uid, assoc_cond);
}

extern List acct_storage_g_get_qos(void *db_conn, uint32_t uid, 
				   acct_qos_cond_t *qos_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.get_qos))(db_conn, uid, qos_cond);
}

extern List acct_storage_g_get_wckeys(void *db_conn, uint32_t uid, 
				      acct_wckey_cond_t *wckey_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.get_wckeys))(db_conn, uid,
							   wckey_cond);
}

extern List acct_storage_g_get_reservations(void *db_conn, uint32_t uid, 
				      acct_reservation_cond_t *resv_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.get_resvs))(db_conn, uid,
							  resv_cond);
}

extern List acct_storage_g_get_txn(void *db_conn,  uint32_t uid, 
				   acct_txn_cond_t *txn_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.get_txn))(db_conn, uid, txn_cond);
}

extern int acct_storage_g_get_usage(void *db_conn,  uint32_t uid,
				    void *in, int type,
				    time_t start, time_t end)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.get_usage))
		(db_conn, uid, in, type, start, end);
}

extern int acct_storage_g_roll_usage(void *db_conn, 
				     time_t sent_start, time_t sent_end,
				     uint16_t archive_data)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.roll_usage))
		(db_conn, sent_start, sent_end, archive_data);
}

extern int clusteracct_storage_g_node_down(void *db_conn,
					   char *cluster,
					   struct node_record *node_ptr,
					   time_t event_time,
					   char *reason)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
 	return (*(g_acct_storage_context->ops.node_down))
		(db_conn, cluster, node_ptr, event_time, reason);
}

extern int clusteracct_storage_g_node_up(void *db_conn,
					 char *cluster,
					 struct node_record *node_ptr,
					 time_t event_time)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
       
	/* on some systems we need to make sure we don't say something
	   is completely up if there are cpus in an error state */
	if(node_ptr->select_nodeinfo) {
		uint16_t err_cpus = 0;
		select_g_select_nodeinfo_get(node_ptr->select_nodeinfo, 
					     SELECT_NODEDATA_SUBCNT,
					     NODE_STATE_ERROR,
					     &err_cpus);
		if(err_cpus) 
			return SLURM_SUCCESS;
	}

 	return (*(g_acct_storage_context->ops.node_up))
		(db_conn, cluster, node_ptr, event_time);
}


extern int clusteracct_storage_g_cluster_procs(void *db_conn,
					       char *cluster,
					       char *cluster_nodes,
					       uint32_t procs,
					       time_t event_time)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
 	return (*(g_acct_storage_context->ops.cluster_procs))
		(db_conn, cluster, cluster_nodes, procs, event_time);
}


extern int clusteracct_storage_g_get_usage(
	void *db_conn, uint32_t uid, void *cluster_rec, int type,
	time_t start, time_t end)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.c_get_usage))
		(db_conn, uid, cluster_rec, type, start, end);
}

extern int clusteracct_storage_g_register_ctld(
	void *db_conn, char *cluster, uint16_t port)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
 	return (*(g_acct_storage_context->ops.register_ctld))
		(db_conn, cluster, port);
}

/* 
 * load into the storage the start of a job
 */
extern int jobacct_storage_g_job_start (void *db_conn, char *cluster_name,
					struct job_record *job_ptr) 
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.job_start))(
		db_conn, cluster_name, job_ptr);
}

/* 
 * load into the storage the end of a job
 */
extern int jobacct_storage_g_job_complete  (void *db_conn,
					    struct job_record *job_ptr)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.job_complete))(db_conn, job_ptr);
}

/* 
 * load into the storage the start of a job step
 */
extern int jobacct_storage_g_step_start (void *db_conn,
					 struct step_record *step_ptr)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.step_start))(db_conn, step_ptr);
}

/* 
 * load into the storage the end of a job step
 */
extern int jobacct_storage_g_step_complete (void *db_conn,
					    struct step_record *step_ptr)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.step_complete))(db_conn, 
							      step_ptr);
}

/* 
 * load into the storage a suspention of a job
 */
extern int jobacct_storage_g_job_suspend (void *db_conn,
					  struct job_record *job_ptr)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
 	return (*(g_acct_storage_context->ops.job_suspend))(db_conn, job_ptr);
}

/* 
 * get info from the storage 
 * returns List of job_rec_t *
 * note List needs to be freed when called
 */
extern List jobacct_storage_g_get_jobs_cond(void *db_conn, uint32_t uid,
					    acct_job_cond_t *job_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
 	return (*(g_acct_storage_context->ops.get_jobs_cond))
		(db_conn, uid, job_cond);
}

/* 
 * expire old info from the storage 
 */
extern int jobacct_storage_g_archive(void *db_conn,
				     acct_archive_cond_t *arch_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
 	return (*(g_acct_storage_context->ops.archive_dump))
		(db_conn, arch_cond);
}

/* 
 * load expired info into the storage 
 */
extern int jobacct_storage_g_archive_load(void *db_conn,
					  acct_archive_rec_t *arch_rec)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
 	return (*(g_acct_storage_context->ops.archive_load))(db_conn, arch_rec);
	
}

/* 
 * record shares used information for backup in case slurmctld restarts 
 * IN:  account_list List of shares_used_object_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_update_shares_used(void *db_conn, List acct_list)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
 	return (*(g_acct_storage_context->ops.update_shares_used))(db_conn, 
								   acct_list);
}

/* 
 * This should be called when a cluster does a cold start to flush out
 * any jobs that were running during the restart so we don't have any
 * jobs in the database "running" forever since no endtime will be
 * placed in there other wise. 
 * IN:  char * = cluster name
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int acct_storage_g_flush_jobs_on_cluster(
	void *db_conn, char *cluster, time_t event_time)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
 	return (*(g_acct_storage_context->ops.flush_jobs))
		(db_conn, cluster, event_time);

}

