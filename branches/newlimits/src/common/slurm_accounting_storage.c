/*****************************************************************************\
 *  slurm_accounting_storage.c - account storage plugin wrapper.
 *
 *  $Id: slurm_accounting_storage.c 10744 2007-01-11 20:09:18Z da $
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Aubke <da@llnl.gov>.
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

#include <pthread.h>

#include "src/common/list.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"
#include "src/sacctmgr/sacctmgr.h"

/*
 * Local data
 */

typedef struct slurm_acct_storage_ops {
	void *(*get_conn)          (bool make_agent, bool rollback);
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
	List (*get_users)          (void *db_conn, uint32_t uid,
				    acct_user_cond_t *user_cond);
	List (*get_accts)          (void *db_conn, uint32_t uid,
				    acct_account_cond_t *acct_cond);
	List (*get_clusters)       (void *db_conn, uint32_t uid,
				    acct_cluster_cond_t *cluster_cond);
	List (*get_associations)   (void *db_conn, uint32_t uid,
				    acct_association_cond_t *assoc_cond);
	List (*get_qos)            (void *db_conn, uint32_t uid,
				    acct_qos_cond_t *qos_cond);
	List (*get_txn)            (void *db_conn, uint32_t uid,
				    acct_txn_cond_t *txn_cond);
	int  (*get_usage)          (void *db_conn, uint32_t uid,
				    void *acct_assoc,
				    time_t start, 
				    time_t end);
	int (*roll_usage)          (void *db_conn, 
				    time_t sent_start);
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
				    char *cluster,
				    uint32_t procs, time_t event_time);
	int  (*c_get_usage)        (void *db_conn, uint32_t uid,
				    void *cluster_rec, 
				    time_t start, time_t end);
	int  (*register_ctld)      (char *cluster, uint16_t port);
	int  (*job_start)          (void *db_conn,
				    struct job_record *job_ptr);
	int  (*job_complete)       (void *db_conn,
				    struct job_record *job_ptr);
	int  (*step_start)         (void *db_conn,
				    struct step_record *step_ptr);
	int  (*step_complete)      (void *db_conn,
				    struct step_record *step_ptr);
	int  (*job_suspend)        (void *db_conn,
				    struct job_record *job_ptr);
	List (*get_jobs)           (void *db_conn, uint32_t uid,
				    List selected_steps,
				    List selected_parts,
				    void *params);	
	List (*get_jobs_cond)      (void *db_conn, uint32_t uid,
				    acct_job_cond_t *job_cond);	
	void (*job_archive)        (void *db_conn,
				    List selected_parts, void *params);	
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
		"acct_storage_p_modify_users",
		"acct_storage_p_modify_accounts",
		"acct_storage_p_modify_clusters",
		"acct_storage_p_modify_associations",
		"acct_storage_p_remove_users",
		"acct_storage_p_remove_coord",
		"acct_storage_p_remove_accts",
		"acct_storage_p_remove_clusters",
		"acct_storage_p_remove_associations",
		"acct_storage_p_remove_qos",
		"acct_storage_p_get_users",
		"acct_storage_p_get_accts",
		"acct_storage_p_get_clusters",
		"acct_storage_p_get_associations",
		"acct_storage_p_get_qos",
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
		"jobacct_storage_p_get_jobs",
		"jobacct_storage_p_get_jobs_cond",
		"jobacct_storage_p_archive",
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
	/*
	 * Must check return code here because plugins might still
	 * be loaded and active.
	 */
	if ( c->plugin_list ) {
		if ( plugrack_destroy( c->plugin_list ) != SLURM_SUCCESS ) {
			return SLURM_ERROR;
		}
	} else {
		plugin_unload(c->cur_plugin);
	}

	xfree( c->acct_storage_type );
	xfree( c );

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
		xfree(acct_user->name);
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
		xfree(acct_association->cluster);
		xfree(acct_association->parent_acct);
		xfree(acct_association->partition);
		if(acct_association->qos_list)
			list_destroy(acct_association->qos_list);
		xfree(acct_association->user);
		xfree(acct_association);
	}
}

extern void destroy_acct_qos_rec(void *object)
{
	acct_qos_rec_t *acct_qos = (acct_qos_rec_t *)object;
	if(acct_qos) {
		xfree(acct_qos->description);
		xfree(acct_qos->name);
		xfree(acct_qos);
	}
}

extern void destroy_acct_txn_rec(void *object)
{
	acct_txn_rec_t *acct_txn = (acct_txn_rec_t *)object;
	if(acct_txn) {
		xfree(acct_txn->actor_name);
		xfree(acct_txn->set_info);
		xfree(acct_txn->where_query);
		xfree(acct_txn);
	}
}

extern void destroy_acct_user_cond(void *object)
{
	acct_user_cond_t *acct_user = (acct_user_cond_t *)object;

	if(acct_user) {
		destroy_acct_association_cond(acct_user->assoc_cond);
		if(acct_user->def_acct_list)
			list_destroy(acct_user->def_acct_list);
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
		if(acct_association->id_list)
			list_destroy(acct_association->id_list);
		if(acct_association->partition_list)
			list_destroy(acct_association->partition_list);
		xfree(acct_association->parent_acct);
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
		if(job_cond->step_list)
			list_destroy(job_cond->step_list);
		if(job_cond->state_list)
			list_destroy(job_cond->state_list);
		if(job_cond->userid_list)
			list_destroy(job_cond->userid_list);
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

extern void destroy_acct_txn_cond(void *object)
{
	acct_txn_cond_t *acct_txn = (acct_txn_cond_t *)object;
	if(acct_txn) {
		if(acct_txn->action_list)
			list_destroy(acct_txn->action_list);
		if(acct_txn->actor_list)
			list_destroy(acct_txn->actor_list);
		if(acct_txn->id_list)
			list_destroy(acct_txn->id_list);
		xfree(acct_txn);
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

extern void destroy_update_shares_rec(void *object)
{
	xfree(object);
}

extern void init_acct_association_rec(acct_association_rec_t *assoc)
{
	if(!assoc)
		return;

	memset(assoc, 0, sizeof(acct_association_rec_t));

	assoc->fairshare = NO_VAL;

	assoc->grp_cpu_hours = NO_VAL;
	assoc->grp_cpus = NO_VAL;
	assoc->grp_jobs = NO_VAL;
	assoc->grp_nodes = NO_VAL;
	assoc->grp_submit_jobs = NO_VAL;
	assoc->grp_wall = NO_VAL;

	assoc->max_cpu_mins_pj = NO_VAL;
	assoc->max_cpus_pj = NO_VAL;
	assoc->max_jobs = NO_VAL;
	assoc->max_nodes_pj = NO_VAL;
	assoc->max_submit_jobs = NO_VAL;
	assoc->max_wall_pj = NO_VAL;
}

extern void init_acct_association_cond(acct_association_cond_t *assoc)
{
	if(!assoc)
		return;
	
	memset(assoc, 0, sizeof(acct_association_cond_t));

	assoc->fairshare = NO_VAL;

	assoc->grp_cpu_hours = NO_VAL;
	assoc->grp_cpus = NO_VAL;
	assoc->grp_jobs = NO_VAL;
	assoc->grp_nodes = NO_VAL;
	assoc->grp_submit_jobs = NO_VAL;
	assoc->grp_wall = NO_VAL;

	assoc->max_cpu_mins_pj = NO_VAL;
	assoc->max_cpus_pj = NO_VAL;
	assoc->max_jobs = NO_VAL;
	assoc->max_nodes_pj = NO_VAL;
	assoc->max_submit_jobs = NO_VAL;
	assoc->max_wall_pj = NO_VAL;
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

	if(rpc_version < 3) {
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
 
		pack16((uint16_t)object->admin_level, buffer);
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
 
		pack16((uint16_t)object->admin_level, buffer);
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
	}
}

extern int unpack_acct_user_rec(void **object, uint16_t rpc_version, Buf buffer)
{
	uint32_t uint32_tmp;
	acct_user_rec_t *object_ptr = xmalloc(sizeof(acct_user_rec_t));
	uint32_t count = NO_VAL;
	acct_coord_rec_t *coord = NULL;
	acct_association_rec_t *assoc = NULL;
	int i;

	*object = object_ptr;
	
	if(rpc_version < 3) {
		safe_unpack16((uint16_t *)&object_ptr->admin_level, buffer);
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
		safe_unpack32(&object_ptr->uid, buffer);
	} else if(rpc_version >= 3) {
		safe_unpack16((uint16_t *)&object_ptr->admin_level, buffer);
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
	}

	return SLURM_SUCCESS;

unpack_error:
	destroy_acct_user_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void pack_update_shares_used(void *in, uint16_t rpc_version, Buf buffer)
{
	shares_used_object_t *object = (shares_used_object_t *)in;

	if(!object) {
		pack32(0, buffer);
		pack32(0, buffer);
		return;
	}

	pack32(object->assoc_id, buffer);
	pack32(object->shares_used, buffer);
}

extern int unpack_update_shares_used(void **object, uint16_t rpc_version, Buf buffer)
{
	shares_used_object_t *object_ptr = xmalloc(sizeof(shares_used_object_t));

	*object = (void *) object_ptr;
	safe_unpack32(&object_ptr->assoc_id, buffer);
	safe_unpack32(&object_ptr->shares_used, buffer);

	return SLURM_SUCCESS;

unpack_error:
	destroy_update_shares_rec(object_ptr);
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

	if(rpc_version < 3) {
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
	} else if(rpc_version >= 3) {
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
	}
}

extern int unpack_acct_account_rec(void **object, uint16_t rpc_version,
				   Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	acct_coord_rec_t *coord = NULL;
	acct_association_rec_t *assoc = NULL;
	acct_account_rec_t *object_ptr = xmalloc(sizeof(acct_account_rec_t));

	*object = object_ptr;

	if(rpc_version < 3) {
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
	} else if(rpc_version >= 3) {
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

extern int unpack_cluster_accounting_rec(void **object, uint16_t rpc_version,
					 Buf buffer)
{
	cluster_accounting_rec_t *object_ptr =
		xmalloc(sizeof(cluster_accounting_rec_t));
	
	*object = object_ptr;
	safe_unpack64(&object_ptr->alloc_secs, buffer);
	safe_unpack32(&object_ptr->cpu_count, buffer);
	safe_unpack64(&object_ptr->down_secs, buffer);
	safe_unpack64(&object_ptr->idle_secs, buffer);
	safe_unpack64(&object_ptr->over_secs, buffer);
	safe_unpack_time(&object_ptr->period_start, buffer);
	safe_unpack64(&object_ptr->resv_secs, buffer);
	
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

	if(rpc_version < 3) {
		if(!object) {
			pack32(NO_VAL, buffer);
			packnull(buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			pack32(0, buffer);
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
			pack32(0, buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			pack32(0, buffer);
		} else {
			pack32(object->root_assoc->fairshare, buffer);
			pack32(object->root_assoc->max_cpu_mins_pj, buffer);
			pack32(object->root_assoc->max_jobs, buffer);
			pack32(object->root_assoc->max_nodes_pj, buffer);
			pack32(object->root_assoc->max_wall_pj, buffer);
		}

		packstr(object->name, buffer);

		if(rpc_version >= 3)
			pack32(object->rpc_version, buffer);
	} else if(rpc_version >= 3) {
		if(!object) {
			pack32(NO_VAL, buffer);
			pack_acct_association_rec(NULL, rpc_version, buffer);
			packnull(buffer);
			pack32(0, buffer);

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

		packstr(object->name, buffer);

		pack_acct_association_rec(object->root_assoc,
					  rpc_version, buffer);

		pack16(object->rpc_version, buffer);
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

	if(rpc_version < 3) {
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
		safe_unpack32(&object_ptr->root_assoc->fairshare, buffer);
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

		if(unpack_acct_association_rec(
			   (void **)&object_ptr->root_assoc, 
			   rpc_version, buffer)
		   == SLURM_ERROR)
			goto unpack_error;

		safe_unpack16(&object_ptr->rpc_version, buffer);
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
	pack32(object->assoc_id, buffer);
	pack_time(object->period_start, buffer);
}

extern int unpack_acct_accounting_rec(void **object, uint16_t rpc_version,
				      Buf buffer)
{
	acct_accounting_rec_t *object_ptr =
		xmalloc(sizeof(acct_accounting_rec_t));
	
	*object = object_ptr;
	safe_unpack64(&object_ptr->alloc_secs, buffer);
	safe_unpack32(&object_ptr->assoc_id, buffer);
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
	
	if(rpc_version < 3) {
		if(!object) {
			pack32(NO_VAL, buffer);

			packnull(buffer);
			packnull(buffer);

			pack32(0, buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			pack32(0, buffer);

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
		pack32(object->fairshare, buffer);
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
		pack32(object->used_shares, buffer);
		packstr(object->user, buffer);	
	} else if (rpc_version >= 3) {
		if(!object) {
			pack32(NO_VAL, buffer);
			packnull(buffer);
			packnull(buffer);

			pack64(0, buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			pack32(0, buffer);

			pack32(0, buffer);
			pack32(0, buffer);
			pack32(0, buffer);

			pack64(0, buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			pack32(0, buffer);

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

		pack64(object->grp_cpu_hours, buffer);
		pack32(object->grp_cpus, buffer);
		pack32(object->grp_jobs, buffer);
		pack32(object->grp_nodes, buffer);
		pack32(object->grp_submit_jobs, buffer);
		pack32(object->grp_wall, buffer);

		pack32(object->fairshare, buffer);
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

		pack32(object->used_shares, buffer);

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

	if(rpc_version < 3) {
		init_acct_association_rec(object_ptr);

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

		safe_unpack32(&object_ptr->fairshare, buffer);
		safe_unpack32(&object_ptr->id, buffer);
		safe_unpack32(&object_ptr->lft, buffer);

		safe_unpack32((uint32_t *)&object_ptr->max_cpu_mins_pj, buffer);
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

		safe_unpack32(&object_ptr->used_shares, buffer);

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

		safe_unpack64(&object_ptr->grp_cpu_hours, buffer);
		safe_unpack32(&object_ptr->grp_cpus, buffer);
		safe_unpack32(&object_ptr->grp_jobs, buffer);
		safe_unpack32(&object_ptr->grp_nodes, buffer);
		safe_unpack32(&object_ptr->grp_submit_jobs, buffer);
		safe_unpack32(&object_ptr->grp_wall, buffer);

		safe_unpack32(&object_ptr->fairshare, buffer);
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

		safe_unpack32(&object_ptr->used_shares, buffer);

		safe_unpackstr_xmalloc(&object_ptr->user, &uint32_tmp, buffer);
	}
	//log_assoc_rec(object_ptr);
	return SLURM_SUCCESS;

unpack_error:
	destroy_acct_association_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void pack_acct_qos_rec(void *in, uint16_t rpc_version, Buf buffer)
{
	acct_qos_rec_t *object = (acct_qos_rec_t *)in;	
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

extern int unpack_acct_qos_rec(void **object, uint16_t rpc_version, Buf buffer)
{
	uint32_t uint32_tmp;
	acct_qos_rec_t *object_ptr = xmalloc(sizeof(acct_qos_rec_t));

	*object = object_ptr;
	safe_unpackstr_xmalloc(&object_ptr->description, &uint32_tmp, buffer);
	safe_unpack32(&object_ptr->id, buffer);
	safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);

	return SLURM_SUCCESS;

unpack_error:
	destroy_acct_qos_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void pack_acct_txn_rec(void *in, uint16_t rpc_version, Buf buffer)
{
	acct_txn_rec_t *object = (acct_txn_rec_t *)in;	
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

extern int unpack_acct_txn_rec(void **object, uint16_t rpc_version, Buf buffer)
{
	uint32_t uint32_tmp;
	acct_txn_rec_t *object_ptr = xmalloc(sizeof(acct_txn_rec_t));

	*object = object_ptr;

	safe_unpack16(&object_ptr->action, buffer);
	safe_unpackstr_xmalloc(&object_ptr->actor_name, &uint32_tmp, buffer);
	safe_unpack32(&object_ptr->id, buffer);
	safe_unpackstr_xmalloc(&object_ptr->set_info, &uint32_tmp, buffer);
	safe_unpack_time(&object_ptr->timestamp, buffer);
	safe_unpackstr_xmalloc(&object_ptr->where_query, &uint32_tmp, buffer);

	return SLURM_SUCCESS;

unpack_error:
	destroy_acct_txn_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;

}

extern void pack_acct_user_cond(void *in, uint16_t rpc_version, Buf buffer)
{
	char *tmp_info = NULL;
	ListIterator itr = NULL;
	acct_user_cond_t *object = (acct_user_cond_t *)in;
	uint32_t count = NO_VAL;

	if(rpc_version < 3) {
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
 
		pack16((uint16_t)object->admin_level, buffer);

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

		pack16((uint16_t)object->with_assocs, buffer);
		pack16((uint16_t)object->with_coords, buffer);
		pack16((uint16_t)object->with_deleted, buffer);
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
 
		pack16((uint16_t)object->admin_level, buffer);

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

		pack16((uint16_t)object->with_assocs, buffer);
		pack16((uint16_t)object->with_coords, buffer);
		pack16((uint16_t)object->with_deleted, buffer);
	}
}

extern int unpack_acct_user_cond(void **object, uint16_t rpc_version, Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	acct_user_cond_t *object_ptr = xmalloc(sizeof(acct_user_cond_t));
	char *tmp_info = NULL;

	*object = object_ptr;

	if(rpc_version < 3) {
		safe_unpack16((uint16_t *)&object_ptr->admin_level, buffer);
		
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

		safe_unpack16((uint16_t *)&object_ptr->with_assocs, buffer);
		safe_unpack16((uint16_t *)&object_ptr->with_coords, buffer);
		safe_unpack16((uint16_t *)&object_ptr->with_deleted, buffer);
	} else if(rpc_version >= 3) {
		safe_unpack16((uint16_t *)&object_ptr->admin_level, buffer);
		
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
		safe_unpack16((uint16_t *)&object_ptr->with_assocs, buffer);
		safe_unpack16((uint16_t *)&object_ptr->with_coords, buffer);
		safe_unpack16((uint16_t *)&object_ptr->with_deleted, buffer);
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

	if(rpc_version < 3) {
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
		
		pack16((uint16_t)object->with_assocs, buffer);
		pack16((uint16_t)object->with_coords, buffer);
		pack16((uint16_t)object->with_deleted, buffer);
	} else if(rpc_version >= 3) {
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
		
		pack16((uint16_t)object->with_assocs, buffer);
		pack16((uint16_t)object->with_coords, buffer);
		pack16((uint16_t)object->with_deleted, buffer);		
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

	if(rpc_version < 3) {
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
		safe_unpack16((uint16_t *)&object_ptr->with_assocs, buffer);
		safe_unpack16((uint16_t *)&object_ptr->with_coords, buffer);
		safe_unpack16((uint16_t *)&object_ptr->with_deleted, buffer);
	} else if (rpc_version >= 3) {
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

		safe_unpack16((uint16_t *)&object_ptr->with_assocs, buffer);
		safe_unpack16((uint16_t *)&object_ptr->with_coords, buffer);
		safe_unpack16((uint16_t *)&object_ptr->with_deleted, buffer);
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

	pack32(object->usage_end, buffer);
	pack32(object->usage_start, buffer);

	pack16((uint16_t)object->with_usage, buffer);
	pack16((uint16_t)object->with_deleted, buffer);
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
	safe_unpack32(&count, buffer);
	if(count != NO_VAL) {
		object_ptr->cluster_list = list_create(slurm_destroy_char);
		for(i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp, buffer);
			list_append(object_ptr->cluster_list, tmp_info);
		}
	}
	safe_unpack32(&object_ptr->usage_end, buffer);
	safe_unpack32(&object_ptr->usage_start, buffer);

	safe_unpack16((uint16_t *)&object_ptr->with_usage, buffer);
	safe_unpack16((uint16_t *)&object_ptr->with_deleted, buffer);

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

	if(rpc_version < 3) {
		if(!object) {
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(0, buffer);
			pack32(NO_VAL, buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			pack32(0, buffer);
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

		pack32(object->fairshare, buffer);
	
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

		pack32(object->max_cpu_mins_pj, buffer);
		pack32(object->max_jobs, buffer);
		pack32(object->max_nodes_pj, buffer);
		pack32(object->max_wall_pj, buffer);

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

		packstr(object->parent_acct, buffer);

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

		pack16((uint16_t)object->with_usage, buffer);
		pack16((uint16_t)object->with_deleted, buffer);
		pack16((uint16_t)object->without_parent_info, buffer);
		pack16((uint16_t)object->without_parent_limits, buffer);
	} else if(rpc_version >= 3) {
		if(!object) {
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);

			pack64(0, buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			pack32(0, buffer);

			pack32(0, buffer);
			pack32(NO_VAL, buffer);

			pack64(0, buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			pack32(0, buffer);

			pack32(NO_VAL, buffer);
			packnull(buffer);

			pack32(NO_VAL, buffer);

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

		pack64(object->grp_cpu_hours, buffer);
		pack32(object->grp_cpus, buffer);
		pack32(object->grp_jobs, buffer);
		pack32(object->grp_nodes, buffer);
		pack32(object->grp_submit_jobs, buffer);
		pack32(object->grp_wall, buffer);

		pack32(object->fairshare, buffer);
	
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

		pack64(object->max_cpu_mins_pj, buffer);
		pack32(object->max_cpus_pj, buffer);
		pack32(object->max_jobs, buffer);
		pack32(object->max_nodes_pj, buffer);
		pack32(object->max_submit_jobs, buffer);
		pack32(object->max_wall_pj, buffer);

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

		packstr(object->parent_acct, buffer);

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

		pack16((uint16_t)object->with_usage, buffer);
		pack16((uint16_t)object->with_deleted, buffer);
		pack16((uint16_t)object->without_parent_info, buffer);
		pack16((uint16_t)object->without_parent_limits, buffer);
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

	if(rpc_version < 3) {
		init_acct_association_cond(object_ptr);

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
			object_ptr->cluster_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->cluster_list, tmp_info);
			}
		}

		safe_unpack32(&object_ptr->fairshare, buffer);

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->id_list = list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp, 
						       buffer);
				list_append(object_ptr->id_list, tmp_info);
			}
		}
	
		safe_unpack32((uint32_t *)&object_ptr->max_cpu_mins_pj, buffer);
		safe_unpack32(&object_ptr->max_jobs, buffer);
		safe_unpack32(&object_ptr->max_nodes_pj, buffer);
		safe_unpack32(&object_ptr->max_wall_pj, buffer);

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

		safe_unpackstr_xmalloc(&object_ptr->parent_acct, &uint32_tmp,
				       buffer);

		safe_unpack32(&object_ptr->usage_end, buffer);
		safe_unpack32(&object_ptr->usage_start, buffer);

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->user_list = list_create(slurm_destroy_char);
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
	} else if(rpc_version >= 3) {
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
			object_ptr->cluster_list = 
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->cluster_list, tmp_info);
			}
		}

		safe_unpack64(&object_ptr->grp_cpu_hours, buffer);
		safe_unpack32(&object_ptr->grp_cpus, buffer);
		safe_unpack32(&object_ptr->grp_jobs, buffer);
		safe_unpack32(&object_ptr->grp_nodes, buffer);
		safe_unpack32(&object_ptr->grp_submit_jobs, buffer);
		safe_unpack32(&object_ptr->grp_wall, buffer);

		safe_unpack32(&object_ptr->fairshare, buffer);

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->id_list = list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp, 
						       buffer);
				list_append(object_ptr->id_list, tmp_info);
			}
		}
	
		safe_unpack64(&object_ptr->max_cpu_mins_pj, buffer);
		safe_unpack32(&object_ptr->max_cpus_pj, buffer);
		safe_unpack32(&object_ptr->max_jobs, buffer);
		safe_unpack32(&object_ptr->max_nodes_pj, buffer);
		safe_unpack32(&object_ptr->max_submit_jobs, buffer);
		safe_unpack32(&object_ptr->max_wall_pj, buffer);

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

		safe_unpackstr_xmalloc(&object_ptr->parent_acct, &uint32_tmp,
				       buffer);

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->qos_list = list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->qos_list, tmp_info);
			}
		}

		safe_unpack32(&object_ptr->usage_end, buffer);
		safe_unpack32(&object_ptr->usage_start, buffer);

		safe_unpack32(&count, buffer);
		if(count != NO_VAL) {
			object_ptr->user_list = list_create(slurm_destroy_char);
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
			pack_jobacct_selected_step(job, rpc_version, buffer);
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

extern int unpack_acct_job_cond(void **object, uint16_t rpc_version, Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	acct_job_cond_t *object_ptr = xmalloc(sizeof(acct_job_cond_t));
	char *tmp_info = NULL;
	jobacct_selected_step_t *job = NULL;

	*object = object_ptr;
	safe_unpack32(&count, buffer);
	if(count != NO_VAL) {
		object_ptr->acct_list = list_create(slurm_destroy_char);
		for(i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp, buffer);
			list_append(object_ptr->acct_list, tmp_info);
		}
	}

	safe_unpack32(&count, buffer);
	if(count != NO_VAL) {
		object_ptr->associd_list = list_create(slurm_destroy_char);
		for(i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp, buffer);
			list_append(object_ptr->associd_list, tmp_info);
		}
	}

	safe_unpack32(&count, buffer);
	if(count != NO_VAL) {
		object_ptr->cluster_list = list_create(slurm_destroy_char);
		for(i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp, buffer);
			list_append(object_ptr->cluster_list, tmp_info);
		}
	}

	safe_unpack16(&object_ptr->duplicates, buffer);

	safe_unpack32(&count, buffer);
	if(count != NO_VAL) {
		object_ptr->groupid_list = list_create(slurm_destroy_char);
		for(i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp, buffer);
			list_append(object_ptr->groupid_list, tmp_info);
		}
	}
	
	safe_unpack32(&count, buffer);
	if(count != NO_VAL) {
		object_ptr->partition_list = list_create(slurm_destroy_char);
		for(i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp, buffer);
			list_append(object_ptr->partition_list, tmp_info);
		}
	}


	safe_unpack32(&count, buffer);
	if(count != NO_VAL) {
		object_ptr->step_list =
			list_create(destroy_jobacct_selected_step);
		for(i=0; i<count; i++) {
			unpack_jobacct_selected_step(&job, rpc_version, buffer);
			list_append(object_ptr->step_list, job);
		}
	}

	safe_unpack32(&count, buffer);
	if(count != NO_VAL) {
		object_ptr->state_list = list_create(slurm_destroy_char);
		for(i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp, buffer);
			list_append(object_ptr->state_list, tmp_info);
		}
	}
	
	safe_unpack32(&object_ptr->usage_end, buffer);
	safe_unpack32(&object_ptr->usage_start, buffer);

	safe_unpack32(&count, buffer);
	if(count != NO_VAL) {
		object_ptr->userid_list = list_create(slurm_destroy_char);
		for(i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp, buffer);
			list_append(object_ptr->userid_list, tmp_info);
		}
	}

	safe_unpack16(&object_ptr->without_steps, buffer);

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

extern void pack_acct_txn_cond(void *in, uint16_t rpc_version, Buf buffer)
{
	uint32_t count = NO_VAL;
	char *tmp_info = NULL;
	ListIterator itr = NULL;
	acct_txn_cond_t *object = (acct_txn_cond_t *)in;

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

extern int unpack_acct_txn_cond(void **object, uint16_t rpc_version, Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	acct_txn_cond_t *object_ptr = xmalloc(sizeof(acct_txn_cond_t));
	char *tmp_info = NULL;

	*object = object_ptr;
	safe_unpack32(&count, buffer);
	if(count != NO_VAL) {
		object_ptr->action_list = list_create(slurm_destroy_char);
		for(i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp, buffer);
			list_append(object_ptr->action_list, tmp_info);
		}
	}

	safe_unpack32(&count, buffer);
	if(count != NO_VAL) {
		object_ptr->actor_list = list_create(slurm_destroy_char);
		for(i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp, buffer);
			list_append(object_ptr->actor_list, tmp_info);
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

	safe_unpack32(&object_ptr->time_end, buffer);
	safe_unpack32(&object_ptr->time_start, buffer);
	
	return SLURM_SUCCESS;

unpack_error:
	destroy_acct_txn_cond(object_ptr);
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

	pack16(object->type, buffer);
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
	case ACCT_REMOVE_QOS:
		my_function = pack_acct_qos_rec;
		break;
	case ACCT_UPDATE_NOTSET:
	default:
		error("unknown type set in update_object: %d", object->type);
		return;
	}
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

	safe_unpack16((uint16_t *)&object_ptr->type, buffer);
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
	case ACCT_REMOVE_QOS:
		my_function = unpack_acct_qos_rec;
		my_destroy = destroy_acct_qos_rec;
		break;
	case ACCT_UPDATE_NOTSET:
	default:
		error("unknown type set in update_object: %d",
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
		return "None";
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
	
	if(!qos_list) {
		error("We need a qos list to translate");
		return NO_VAL;
	} else if(!level) {
		debug2("no level");
		return 0;
	}


	itr = list_iterator_create(qos_list);
	while((qos = list_next(itr))) {
		if(!strncasecmp(level, qos->name, strlen(level)))
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

extern void log_assoc_rec(acct_association_rec_t *assoc_ptr)
{
	debug2("association rec id          : %u", assoc_ptr->id);
	debug2("  acct                      : %s", assoc_ptr->acct);
	debug2("  cluster                   : %s", assoc_ptr->cluster);
	if(assoc_ptr->fairshare == INFINITE)
		debug2("  fairshare                 : NONE");
	else
		debug2("  fairshare                 : %u",
		       assoc_ptr->fairshare);
	if(assoc_ptr->max_cpu_mins_pj == INFINITE)
		debug2("  max_cpu_mins_per_job      : NONE");
	else
		debug2("  max_cpu_mins_per_job      : %d",
		       assoc_ptr->max_cpu_mins_pj);
	if(assoc_ptr->max_jobs == INFINITE)
		debug2("  max_jobs                  : NONE");
	else
		debug2("  max_jobs                  : %u", assoc_ptr->max_jobs);
	if(assoc_ptr->max_nodes_pj == INFINITE)
		debug2("  max_nodes_per_job         : NONE");
	else
		debug2("  max_nodes_per_job         : %d",
		       assoc_ptr->max_nodes_pj);
	if(assoc_ptr->max_wall_pj == INFINITE)
		debug2("  max_wall_duration_per_job : NONE");
	else
		debug2("  max_wall_duration_per_job : %d", 
		       assoc_ptr->max_wall_pj);
	debug2("  parent_acct               : %s", assoc_ptr->parent_acct);
	debug2("  partition                 : %s", assoc_ptr->partition);
	debug2("  user                      : %s(%u)",
	       assoc_ptr->user, assoc_ptr->uid);
	debug2("  used_jobs                 : %u", assoc_ptr->used_jobs);
	debug2("  used_shares                : %u", assoc_ptr->used_shares);
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

extern void *acct_storage_g_get_connection(bool make_agent, bool rollback)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.get_conn))(make_agent, rollback);
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

extern List acct_storage_g_get_associations(void *db_conn, uint32_t uid,
					    acct_association_cond_t *assoc_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.get_associations))
		(db_conn, uid, assoc_cond);
}

extern List acct_storage_g_get_qos(void *db_conn, uint32_t uid, 
				   acct_qos_cond_t *qos_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.get_qos))(db_conn, uid, qos_cond);
}

extern List acct_storage_g_get_txn(void *db_conn,  uint32_t uid, 
				   acct_txn_cond_t *txn_cond)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.get_txn))(db_conn, uid, txn_cond);
}

extern int acct_storage_g_get_usage(void *db_conn,  uint32_t uid,
				    void *acct_assoc,
				    time_t start, time_t end)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.get_usage))
		(db_conn, uid, acct_assoc, start, end);
}

extern int acct_storage_g_roll_usage(void *db_conn, 
				     time_t sent_start)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.roll_usage))(db_conn, sent_start);
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
 	return (*(g_acct_storage_context->ops.node_up))
		(db_conn, cluster, node_ptr, event_time);
}


extern int clusteracct_storage_g_cluster_procs(void *db_conn,
					       char *cluster,
					       uint32_t procs,
					       time_t event_time)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
 	return (*(g_acct_storage_context->ops.cluster_procs))
		(db_conn, cluster, procs, event_time);
}


extern int clusteracct_storage_g_get_usage(
	void *db_conn, uint32_t uid, void *cluster_rec,
	time_t start, time_t end)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.c_get_usage))
		(db_conn, uid, cluster_rec, start, end);
}

extern int clusteracct_storage_g_register_ctld(char *cluster, uint16_t port)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
 	return (*(g_acct_storage_context->ops.register_ctld))(cluster, port);
}

/* 
 * load into the storage the start of a job
 */
extern int jobacct_storage_g_job_start (void *db_conn,
					struct job_record *job_ptr) 
{
	if (slurm_acct_storage_init(NULL) < 0)
		return SLURM_ERROR;
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
extern List jobacct_storage_g_get_jobs(void *db_conn, uint32_t uid,
				       List selected_steps,
				       List selected_parts,
				       void *params)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return NULL;
 	return (*(g_acct_storage_context->ops.get_jobs))
		(db_conn, uid, selected_steps, selected_parts, params);
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
extern void jobacct_storage_g_archive(void *db_conn,
				      List selected_parts, void *params)
{
	if (slurm_acct_storage_init(NULL) < 0)
		return;
 	(*(g_acct_storage_context->ops.job_archive))(db_conn, selected_parts,
						     params);
	return;
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

