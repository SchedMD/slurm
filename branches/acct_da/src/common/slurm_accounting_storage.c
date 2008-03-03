/*****************************************************************************\
 *  slurm_accounting_storage.c - account torage plugin wrapper.
 *
 *  $Id: slurm_accounting_storage.c 10744 2007-01-11 20:09:18Z da $
 *****************************************************************************
 *  Copyright (C) 2002-2008 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Aubke <da@llnl.gov>.
 *  UCRL-CODE-226842.
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

/*
 * Local data
 */

typedef struct slurm_acct_storage_ops {
	int  (*add_users)          (List user_list);
	int  (*add_coord)          (char *acct,
				    acct_user_cond_t *user_q);
	int  (*add_accts)       (List acct_list);
	int  (*add_clusters)       (List cluster_list);
	int  (*add_associations)   (List association_list);
	uint32_t (*get_assoc_id)  (acct_association_rec_t *assoc);
	int  (*modify_users)       (acct_user_cond_t *user_q,
				    acct_user_rec_t *user);
	int  (*modify_user_admin_level)(acct_user_cond_t *user_q);
	int  (*modify_accts)    (acct_account_cond_t *acct_q,
				    acct_account_rec_t *acct);
	int  (*modify_clusters)    (acct_cluster_cond_t *cluster_q,
				    acct_cluster_rec_t *cluster);
	int  (*modify_associations)(acct_association_cond_t *assoc_q,
				    acct_association_rec_t *assoc);
	int  (*remove_users)       (acct_user_cond_t *user_q);
	int  (*remove_coord)       (char *acct,
				    acct_user_cond_t *user_q);
	int  (*remove_accts)    (acct_account_cond_t *acct_q);
	int  (*remove_clusters)    (acct_cluster_cond_t *cluster_q);
	int  (*remove_associations)(acct_association_cond_t *assoc_q);
	List (*get_users)          (acct_user_cond_t *user_q);
	List (*get_accts)       (acct_account_cond_t *acct_q);
	List (*get_clusters)       (acct_cluster_cond_t *cluster_q);
	List (*get_associations)   (acct_association_cond_t *assoc_q);
	int (*get_hourly_usage)    (acct_association_rec_t *acct_assoc,
				    time_t start, 
				    time_t end);
	int (*get_daily_usage)     (acct_association_rec_t *acct_assoc,
				    time_t start, 
				    time_t end);
	int (*get_monthly_usage)   (acct_association_rec_t *acct_assoc,
				    time_t start, 
				    time_t end);
	int  (*node_down)          (struct node_record *node_ptr,
				    time_t event_time,
				    char *reason);
	int  (*node_up)            (struct node_record *node_ptr,
				    time_t event_time);
	int  (*cluster_procs)      (uint32_t procs, time_t event_time);
	int (*c_get_hourly_usage)  (acct_cluster_rec_t *cluster_rec, 
				    time_t start, time_t end,
				    void *params);
	int (*c_get_daily_usage)   (acct_cluster_rec_t *cluster_rec, 
				    time_t start, time_t end,
				    void *params);
	int (*c_get_monthly_usage) (acct_cluster_rec_t *cluster_rec,
				    time_t start, time_t end,
				    void *params);	
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
		"acct_storage_p_add_users",
		"acct_storage_p_add_coord",
		"acct_storage_p_add_accts",
		"acct_storage_p_add_clusters",
		"acct_storage_p_add_associations",
		"acct_storage_p_modify_users",
		"acct_storage_p_modify_user_admin_level",
		"acct_storage_p_modify_accts",
		"acct_storage_p_modify_clusters",
		"acct_storage_p_modify_associations",
		"acct_storage_p_remove_users",
		"acct_storage_p_remove_coord",
		"acct_storage_p_remove_accts",
		"acct_storage_p_remove_clusters",
		"acct_storage_p_remove_associations",
		"acct_storage_p_get_users",
		"acct_storage_p_get_accts",
		"acct_storage_p_get_clusters",
		"acct_storage_p_get_associations",
		"acct_storage_p_get_assoc_id",
		"acct_storage_p_get_hourly_usage",
		"acct_storage_p_get_daily_usage",
		"acct_storage_p_get_monthly_usage",
		"clusteracct_storage_p_node_down",
		"clusteracct_storage_p_node_up",
		"clusteracct_storage_p_cluster_procs",
		"clusteracct_storage_p_get_hourly_usage",
		"clusteracct_storage_p_get_daily_usage",
		"clusteracct_storage_p_get_monthly_usage"

	};
	int n_syms = sizeof( syms ) / sizeof( char * );

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
	}

	xfree( c->acct_storage_type );
	xfree( c );

	return SLURM_SUCCESS;
}

extern void destroy_acct_user_rec(void *object)
{
	acct_user_rec_t *acct_user = (acct_user_rec_t *)object;

	if(acct_user) {
		xfree(acct_user->name);
		xfree(acct_user->default_acct);
		xfree(acct_user);
	}
}

extern void destroy_acct_account_rec(void *object)
{
	acct_account_rec_t *acct_account =
		(acct_account_rec_t *)object;

	if(acct_account) {
		xfree(acct_account->name);
		xfree(acct_account->description);
		xfree(acct_account->organization);
		if(acct_account->coodinators)
			list_destroy(acct_account->coodinators);
		xfree(acct_account);
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
		xfree(acct_cluster->name);
		xfree(acct_cluster->interface_node);
		if(acct_cluster->accounting_list)
			list_destroy(acct_cluster->accounting_list);
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
		xfree(acct_association->user);
		xfree(acct_association->acct);
		xfree(acct_association->cluster);
		xfree(acct_association->partition);
		if(acct_association->accounting_list)
			list_destroy(acct_association->accounting_list);
		xfree(acct_association);
	}
}

extern void destroy_acct_user_cond(void *object)
{
	acct_user_cond_t *acct_user = (acct_user_cond_t *)object;

	if(acct_user) {
		if(acct_user->user_list)
			list_destroy(acct_user->user_list);
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
		if(acct_account->acct_list)
			list_destroy(acct_account->acct_list);
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
		if(acct_association->id_list)
			list_destroy(acct_association->id_list);
		if(acct_association->user_list)
			list_destroy(acct_association->user_list);
		if(acct_association->acct_list)
			list_destroy(acct_association->acct_list);
		if(acct_association->cluster_list)
			list_destroy(acct_association->cluster_list);
		xfree(acct_association);
	}
}

extern char *acct_expedite_str(acct_expedite_level_t level)
{
	switch(level) {
	case ACCT_EXPEDITE_NOTSET:
		return "Not Set";
		break;
	case ACCT_EXPEDITE_NORMAL:
		return "Normal";
		break;
	case ACCT_EXPEDITE_EXPEDITE:
		return "Expedite";
		break;
	case ACCT_EXPEDITE_STANDBY:
		return "Standby";
		break;
	case ACCT_EXPEDITE_EXEMPT:
		return "Exempt";
		break;
	default:
		return "Unknown";
		break;
	}
	return "Unknown";
}

extern acct_expedite_level_t str_2_acct_expedite(char *level)
{
	if(!level) {
		return ACCT_EXPEDITE_NOTSET;
	} else if(!strncasecmp(level, "Normal", 1)) {
		return ACCT_EXPEDITE_NORMAL;
	} else if(!strncasecmp(level, "Expedite", 3)) {
		return ACCT_EXPEDITE_EXPEDITE;
	} else if(!strncasecmp(level, "Standby", 1)) {
		return ACCT_EXPEDITE_STANDBY;
	} else if(!strncasecmp(level, "Exempt", 3)) {
		return ACCT_EXPEDITE_EXEMPT;
	} else {
		return ACCT_EXPEDITE_NOTSET;		
	}
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

/*
 * Initialize context for acct_storage plugin
 */
extern int slurm_acct_storage_init(void)
{
	int retval = SLURM_SUCCESS;
	char *acct_storage_type = NULL;
	
	slurm_mutex_lock( &g_acct_storage_context_lock );

	if ( g_acct_storage_context )
		goto done;

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

extern int acct_storage_g_add_users(List user_list)
{
	if (slurm_acct_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.add_users))(user_list);
}

extern int acct_storage_g_add_coord(char *acct, acct_user_cond_t *user_q)
{
	if (slurm_acct_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.add_coord))
		(acct, user_q);
}

extern int acct_storage_g_add_accounts(List acct_list)
{
	if (slurm_acct_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.add_accts))(acct_list);
}

extern int acct_storage_g_add_clusters(List cluster_list)
{
	if (slurm_acct_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.add_clusters))(cluster_list);
}

extern int acct_storage_g_add_associations(List association_list)
{
	if (slurm_acct_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.add_associations))
		(association_list);
}

extern uint32_t acct_storage_g_get_assoc_id(acct_association_rec_t *assoc)
{
	if (slurm_acct_storage_init() < 0)
		return SLURM_ERROR;

	return (*(g_acct_storage_context->ops.get_assoc_id))(assoc);
}

extern int acct_storage_g_modify_users(acct_user_cond_t *user_q,
				       acct_user_rec_t *user)
{
	if (slurm_acct_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.modify_users))(user_q, user);
}

extern int acct_storage_g_modify_user_admin_level(acct_user_cond_t *user_q)
{
	if (slurm_acct_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.modify_user_admin_level))
		(user_q);
}

extern int acct_storage_g_modify_accounts(acct_account_cond_t *acct_q,
					  acct_account_rec_t *acct)
{
	if (slurm_acct_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.modify_accts))
		(acct_q, acct);
}

extern int acct_storage_g_modify_clusters(acct_cluster_cond_t *cluster_q,
					  acct_cluster_rec_t *cluster)
{
	if (slurm_acct_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.modify_clusters))
		(cluster_q, cluster);
}

extern int acct_storage_g_modify_associations(acct_association_cond_t *assoc_q,
					      acct_association_rec_t *assoc)
{
	if (slurm_acct_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.modify_associations))
		(assoc_q, assoc);
}

extern int acct_storage_g_remove_users(acct_user_cond_t *user_q)
{
	if (slurm_acct_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.remove_users))(user_q);
}

extern int acct_storage_g_remove_coord(char *acct, acct_user_cond_t *user_q)
{
	if (slurm_acct_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.remove_coord))
		(acct, user_q);
}

extern int acct_storage_g_remove_accounts(acct_account_cond_t *acct_q)
{
	if (slurm_acct_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.remove_accts))
		(acct_q);
}

extern int acct_storage_g_remove_clusters(acct_cluster_cond_t *cluster_q)
{
	if (slurm_acct_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.remove_clusters))
		(cluster_q);
}

extern int acct_storage_g_remove_associations(acct_association_cond_t *assoc_q)
{
	if (slurm_acct_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.remove_associations))
		(assoc_q);
}

extern List acct_storage_g_get_users(acct_user_cond_t *user_q)
{
	if (slurm_acct_storage_init() < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.get_users))(user_q);
}

extern List acct_storage_g_get_accounts(acct_account_cond_t *acct_q)
{
	if (slurm_acct_storage_init() < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.get_accts))
		(acct_q);
}

extern List acct_storage_g_get_clusters(acct_cluster_cond_t *cluster_q)
{
	if (slurm_acct_storage_init() < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.get_clusters))
		(cluster_q);
}

extern List acct_storage_g_get_associations(acct_association_cond_t *assoc_q)
{
	if (slurm_acct_storage_init() < 0)
		return NULL;
	return (*(g_acct_storage_context->ops.get_associations))
		(assoc_q);
}

extern int acct_storage_g_get_hourly_usage(acct_association_rec_t *acct_assoc,
					   time_t start, time_t end)
{
	if (slurm_acct_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.get_hourly_usage))
		(acct_assoc, start, end);
}

extern int acct_storage_g_get_daily_usage(acct_association_rec_t *acct_assoc,
					  time_t start, time_t end)
{
	if (slurm_acct_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.get_daily_usage))
		(acct_assoc, start, end);
}

extern int acct_storage_g_get_monthly_usage(acct_association_rec_t *acct_assoc,
					    time_t start, time_t end)
{
	if (slurm_acct_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.get_monthly_usage))
		(acct_assoc, start, end);
}

extern int clusteracct_storage_g_node_down(struct node_record *node_ptr,
					   time_t event_time,
					   char *reason)
{
	if (slurm_acct_storage_init() < 0)
		return SLURM_ERROR;
 	return (*(g_acct_storage_context->ops.node_down))
		(node_ptr, event_time, reason);
}

extern int clusteracct_storage_g_node_up(struct node_record *node_ptr,
					 time_t event_time)
{
	if (slurm_acct_storage_init() < 0)
		return SLURM_ERROR;
 	return (*(g_acct_storage_context->ops.node_up))
		(node_ptr, event_time);
}


extern int clusteracct_storage_g_cluster_procs(uint32_t procs,
					       time_t event_time)
{
	if (slurm_acct_storage_init() < 0)
		return SLURM_ERROR;
 	return (*(g_acct_storage_context->ops.cluster_procs))
		(procs, event_time);
}


extern int clusteracct_storage_g_get_hourly_usage(
	acct_cluster_rec_t *cluster_rec, 
	time_t start, time_t end, void *params)
{
	if (slurm_acct_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.c_get_hourly_usage))
		(cluster_rec, start, end, params);
}

extern int clusteracct_storage_g_get_daily_usage(
	acct_cluster_rec_t *cluster_rec, 
	time_t start, time_t end, void *params)
{
	if (slurm_acct_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.c_get_daily_usage))
		(cluster_rec, start, end, params);
}

extern int clusteracct_storage_g_get_monthly_usage(
	acct_cluster_rec_t *cluster_rec, 
	time_t start, time_t end, void *params)
{
	if (slurm_acct_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_acct_storage_context->ops.c_get_monthly_usage))
		(cluster_rec, start, end, params);
}
