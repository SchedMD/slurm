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
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_strcasestr.h"
#include "src/common/xstring.h"
#include "src/sacctmgr/sacctmgr.h"
#include "src/slurmctld/slurmctld.h"

/*
 * Local data
 */

typedef struct slurm_acct_storage_ops {
	void *(*get_conn)          (bool make_agent, int conn_num,
				    bool rollback, char *cluster_name);
	int  (*close_conn)         (void **db_conn);
	int  (*commit)             (void *db_conn, bool commit);
	int  (*add_users)          (void *db_conn, uint32_t uid,
				    List user_list);
	int  (*add_coord)          (void *db_conn, uint32_t uid,
				    List acct_list,
				    slurmdb_user_cond_t *user_cond);
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
				    slurmdb_reservation_rec_t *resv);
	List (*modify_users)       (void *db_conn, uint32_t uid,
				    slurmdb_user_cond_t *user_cond,
				    slurmdb_user_rec_t *user);
	List (*modify_accts)       (void *db_conn, uint32_t uid,
				    slurmdb_account_cond_t *acct_cond,
				    slurmdb_account_rec_t *acct);
	List (*modify_clusters)    (void *db_conn, uint32_t uid,
				    slurmdb_cluster_cond_t *cluster_cond,
				    slurmdb_cluster_rec_t *cluster);
	List (*modify_associations)(void *db_conn, uint32_t uid,
				    slurmdb_association_cond_t *assoc_cond,
				    slurmdb_association_rec_t *assoc);
	List (*modify_qos)         (void *db_conn, uint32_t uid,
				    slurmdb_qos_cond_t *qos_cond,
				    slurmdb_qos_rec_t *qos);
	List (*modify_wckeys)      (void *db_conn, uint32_t uid,
				    slurmdb_wckey_cond_t *wckey_cond,
				    slurmdb_wckey_rec_t *wckey);
	int  (*modify_reservation) (void *db_conn,
				    slurmdb_reservation_rec_t *resv);
	List (*remove_users)       (void *db_conn, uint32_t uid,
				    slurmdb_user_cond_t *user_cond);
	List (*remove_coord)       (void *db_conn, uint32_t uid,
				    List acct_list,
				    slurmdb_user_cond_t *user_cond);
	List (*remove_accts)       (void *db_conn, uint32_t uid,
				    slurmdb_account_cond_t *acct_cond);
	List (*remove_clusters)    (void *db_conn, uint32_t uid,
				    slurmdb_cluster_cond_t *cluster_cond);
	List (*remove_associations)(void *db_conn, uint32_t uid,
				    slurmdb_association_cond_t *assoc_cond);
	List (*remove_qos)         (void *db_conn, uint32_t uid,
				    slurmdb_qos_cond_t *qos_cond);
	List (*remove_wckeys)      (void *db_conn, uint32_t uid,
				    slurmdb_wckey_cond_t *wckey_cond);
	int  (*remove_reservation) (void *db_conn,
				    slurmdb_reservation_rec_t *resv);
	List (*get_users)          (void *db_conn, uint32_t uid,
				    slurmdb_user_cond_t *user_cond);
	List (*get_accts)          (void *db_conn, uint32_t uid,
				    slurmdb_account_cond_t *acct_cond);
	List (*get_clusters)       (void *db_conn, uint32_t uid,
				    slurmdb_cluster_cond_t *cluster_cond);
	List (*get_config)         (void *db_conn);
	List (*get_associations)   (void *db_conn, uint32_t uid,
				    slurmdb_association_cond_t *assoc_cond);
	List (*get_events)         (void *db_conn, uint32_t uid,
				    slurmdb_event_cond_t *event_cond);
	List (*get_problems)       (void *db_conn, uint32_t uid,
				    slurmdb_association_cond_t *assoc_cond);
	List (*get_qos)            (void *db_conn, uint32_t uid,
				    slurmdb_qos_cond_t *qos_cond);
	List (*get_wckeys)         (void *db_conn, uint32_t uid,
				    slurmdb_wckey_cond_t *wckey_cond);
	List (*get_resvs)          (void *db_conn, uint32_t uid,
				    slurmdb_reservation_cond_t *resv_cond);
	List (*get_txn)            (void *db_conn, uint32_t uid,
				    slurmdb_txn_cond_t *txn_cond);
	int  (*get_usage)          (void *db_conn, uint32_t uid,
				    void *in, int type,
				    time_t start,
				    time_t end);
	int (*roll_usage)          (void *db_conn,
				    time_t sent_start, time_t sent_end,
				    uint16_t archive_data);
	int  (*node_down)          (void *db_conn,
				    struct node_record *node_ptr,
				    time_t event_time,
				    char *reason, uint32_t reason_uid);
	int  (*node_up)            (void *db_conn,
				    struct node_record *node_ptr,
				    time_t event_time);
	int  (*cluster_cpus)      (void *db_conn, char *cluster_nodes,
				   uint32_t cpus, time_t event_time);
	int  (*register_ctld)      (void *db_conn, uint16_t port);
	int  (*job_start)          (void *db_conn, struct job_record *job_ptr);
	int  (*job_complete)       (void *db_conn,
				    struct job_record *job_ptr);
	int  (*step_start)         (void *db_conn,
				    struct step_record *step_ptr);
	int  (*step_complete)      (void *db_conn,
				    struct step_record *step_ptr);
	int  (*job_suspend)        (void *db_conn,
				    struct job_record *job_ptr);
	List (*get_jobs_cond)      (void *db_conn, uint32_t uid,
				    slurmdb_job_cond_t *job_cond);
	int (*archive_dump)        (void *db_conn,
				    slurmdb_archive_cond_t *arch_cond);
	int (*archive_load)        (void *db_conn,
				    slurmdb_archive_rec_t *arch_rec);
	int (*update_shares_used)  (void *db_conn,
				    List shares_used);
	int (*flush_jobs)          (void *db_conn,
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
		"acct_storage_p_modify_accts",
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
		"acct_storage_p_get_events",
		"acct_storage_p_get_problems",
		"acct_storage_p_get_qos",
		"acct_storage_p_get_wckeys",
		"acct_storage_p_get_reservations",
		"acct_storage_p_get_txn",
		"acct_storage_p_get_usage",
		"acct_storage_p_roll_usage",
		"clusteracct_storage_p_node_down",
		"clusteracct_storage_p_node_up",
		"clusteracct_storage_p_cluster_cpus",
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

	if(errno != EPLUGIN_NOTFOUND) {
		error("Couldn't load specified plugin name for %s: %s",
		      c->acct_storage_type, plugin_strerror(errno));
		return NULL;
	}

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
					   bool rollback, char *cluster_name)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.get_conn))(
		make_agent, conn_num, rollback, cluster_name);
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
				    List acct_list,
				    slurmdb_user_cond_t *user_cond)
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
					  slurmdb_reservation_rec_t *resv)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NO_VAL;
	return (*(g_acct_storage_context->ops.add_reservation))
		(db_conn, resv);
}

extern List acct_storage_g_modify_users(void *db_conn, uint32_t uid,
					slurmdb_user_cond_t *user_cond,
					slurmdb_user_rec_t *user)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.modify_users))
		(db_conn, uid, user_cond, user);
}

extern List acct_storage_g_modify_accounts(void *db_conn, uint32_t uid,
					   slurmdb_account_cond_t *acct_cond,
					   slurmdb_account_rec_t *acct)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.modify_accts))
		(db_conn, uid, acct_cond, acct);
}

extern List acct_storage_g_modify_clusters(void *db_conn, uint32_t uid,
					   slurmdb_cluster_cond_t *cluster_cond,
					   slurmdb_cluster_rec_t *cluster)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.modify_clusters))
		(db_conn, uid, cluster_cond, cluster);
}

extern List acct_storage_g_modify_associations(
	void *db_conn, uint32_t uid,
	slurmdb_association_cond_t *assoc_cond,
	slurmdb_association_rec_t *assoc)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.modify_associations))
		(db_conn, uid, assoc_cond, assoc);
}

extern List acct_storage_g_modify_qos(void *db_conn, uint32_t uid,
				      slurmdb_qos_cond_t *qos_cond,
				      slurmdb_qos_rec_t *qos)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.modify_qos))
		(db_conn, uid, qos_cond, qos);
}

extern List acct_storage_g_modify_wckeys(void *db_conn, uint32_t uid,
					 slurmdb_wckey_cond_t *wckey_cond,
					 slurmdb_wckey_rec_t *wckey)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.modify_wckeys))
		(db_conn, uid, wckey_cond, wckey);
}

extern int acct_storage_g_modify_reservation(void *db_conn,
					     slurmdb_reservation_rec_t *resv)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NO_VAL;
	return (*(g_acct_storage_context->ops.modify_reservation))
		(db_conn, resv);
}

extern List acct_storage_g_remove_users(void *db_conn, uint32_t uid,
					slurmdb_user_cond_t *user_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.remove_users))
		(db_conn, uid, user_cond);
}

extern List acct_storage_g_remove_coord(void *db_conn, uint32_t uid,
					List acct_list,
					slurmdb_user_cond_t *user_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.remove_coord))
		(db_conn, uid, acct_list, user_cond);
}

extern List acct_storage_g_remove_accounts(void *db_conn, uint32_t uid,
					   slurmdb_account_cond_t *acct_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.remove_accts))
		(db_conn, uid, acct_cond);
}

extern List acct_storage_g_remove_clusters(void *db_conn, uint32_t uid,
					   slurmdb_cluster_cond_t *cluster_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.remove_clusters))
		(db_conn, uid, cluster_cond);
}

extern List acct_storage_g_remove_associations(
	void *db_conn, uint32_t uid,
	slurmdb_association_cond_t *assoc_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.remove_associations))
		(db_conn, uid, assoc_cond);
}

extern List acct_storage_g_remove_qos(void *db_conn, uint32_t uid,
				      slurmdb_qos_cond_t *qos_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.remove_qos))
		(db_conn, uid, qos_cond);
}

extern List acct_storage_g_remove_wckeys(void *db_conn, uint32_t uid,
					 slurmdb_wckey_cond_t *wckey_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.remove_wckeys))
		(db_conn, uid, wckey_cond);
}

extern int acct_storage_g_remove_reservation(void *db_conn,
					     slurmdb_reservation_rec_t *resv)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NO_VAL;
	return (*(g_acct_storage_context->ops.remove_reservation))
		(db_conn, resv);
}

extern List acct_storage_g_get_users(void *db_conn, uint32_t uid,
				     slurmdb_user_cond_t *user_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.get_users))
		(db_conn, uid, user_cond);
}

extern List acct_storage_g_get_accounts(void *db_conn, uint32_t uid,
					slurmdb_account_cond_t *acct_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.get_accts))
		(db_conn, uid, acct_cond);
}

extern List acct_storage_g_get_clusters(void *db_conn, uint32_t uid,
					slurmdb_cluster_cond_t *cluster_cond)
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

extern List acct_storage_g_get_associations(
	void *db_conn, uint32_t uid,
	slurmdb_association_cond_t *assoc_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.get_associations))
		(db_conn, uid, assoc_cond);
}

extern List acct_storage_g_get_events(void *db_conn, uint32_t uid,
				      slurmdb_event_cond_t *event_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.get_events))
		(db_conn, uid, event_cond);
}

extern List acct_storage_g_get_problems(void *db_conn, uint32_t uid,
					slurmdb_association_cond_t *assoc_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.get_problems))
		(db_conn, uid, assoc_cond);
}

extern List acct_storage_g_get_qos(void *db_conn, uint32_t uid,
				   slurmdb_qos_cond_t *qos_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.get_qos))(db_conn, uid, qos_cond);
}

extern List acct_storage_g_get_wckeys(void *db_conn, uint32_t uid,
				      slurmdb_wckey_cond_t *wckey_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.get_wckeys))(db_conn, uid,
							   wckey_cond);
}

extern List acct_storage_g_get_reservations(void *db_conn, uint32_t uid,
					    slurmdb_reservation_cond_t *resv_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.get_resvs))(db_conn, uid,
							  resv_cond);
}

extern List acct_storage_g_get_txn(void *db_conn,  uint32_t uid,
				   slurmdb_txn_cond_t *txn_cond)
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
					   struct node_record *node_ptr,
					   time_t event_time,
					   char *reason, uint32_t reason_uid)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
 	return (*(g_acct_storage_context->ops.node_down))
		(db_conn, node_ptr, event_time, reason, reason_uid);
}

extern int clusteracct_storage_g_node_up(void *db_conn,
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
		if(err_cpus) {
			char *reason = "Setting partial node down.";
			struct node_record send_node;
			struct config_record config_rec;
			uint16_t cpu_cnt = 0;
			select_g_alter_node_cnt(SELECT_GET_NODE_CPU_CNT,
						&cpu_cnt);
			err_cpus *= cpu_cnt;
			memset(&send_node, 0, sizeof(struct node_record));
			memset(&config_rec, 0, sizeof(struct config_record));
			send_node.name = node_ptr->name;
			send_node.config_ptr = &config_rec;
			send_node.cpus = err_cpus;
			config_rec.cpus = err_cpus;

			send_node.node_state = NODE_STATE_ERROR;

			return (*(g_acct_storage_context->ops.node_down))
				(db_conn, &send_node,
				 event_time, reason, slurm_get_slurm_user_id());
		}
	}

 	return (*(g_acct_storage_context->ops.node_up))
		(db_conn, node_ptr, event_time);
}


extern int clusteracct_storage_g_cluster_cpus(void *db_conn,
					      char *cluster_nodes,
					      uint32_t cpus,
					      time_t event_time)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
 	return (*(g_acct_storage_context->ops.cluster_cpus))
		(db_conn, cluster_nodes, cpus, event_time);
}


extern int clusteracct_storage_g_register_ctld(void *db_conn, uint16_t port)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
 	return (*(g_acct_storage_context->ops.register_ctld))
		(db_conn, port);
}

/*
 * load into the storage information about a job,
 * typically when it begins execution, but possibly earlier
 */
extern int jobacct_storage_g_job_start (void *db_conn,
					struct job_record *job_ptr)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;

	/* A pending job's start_time is it's expected initiation time
	 * (changed in slurm v2.1). Rather than changing a bunch of code
	 * in the accounting_storage plugins and SlurmDBD, just clear
	 * start_time before accounting and restore it later. */
	if (IS_JOB_PENDING(job_ptr)) {
		int rc;
		time_t orig_start_time = job_ptr->start_time;
		job_ptr->start_time = (time_t) 0;
		rc = (*(g_acct_storage_context->ops.job_start))(
			db_conn, job_ptr);
		job_ptr->start_time = orig_start_time;
		return rc;
	}

	return (*(g_acct_storage_context->ops.job_start))(db_conn, job_ptr);
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
					    slurmdb_job_cond_t *job_cond)
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
				     slurmdb_archive_cond_t *arch_cond)
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
					  slurmdb_archive_rec_t *arch_rec)
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
	void *db_conn, time_t event_time)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
 	return (*(g_acct_storage_context->ops.flush_jobs))
		(db_conn, event_time);

}

