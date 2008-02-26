/*****************************************************************************\
 *  slurm_account_storage.c - account torage plugin wrapper.
 *
 *  $Id: slurm_account_storage.c 10744 2007-01-11 20:09:18Z da $
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
#include "src/common/slurm_account_storage.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"

/*
 * Local data
 */

typedef struct slurm_account_storage_ops {
	int  (*add_users)          (List user_list);
	int  (*add_coord)          (char *account,
				    account_user_cond_t *user_q);
	int  (*add_accounts)       (List account_list);
	int  (*add_clusters)       (List cluster_list);
	int  (*add_associations)   (List association_list);
	int  (*modify_users)       (account_user_cond_t *user_q,
				    account_user_rec_t *user);
	int  (*modify_user_admin_level)(account_user_cond_t *user_q);
	int  (*modify_accounts)    (account_account_cond_t *account_q,
				    account_account_rec_t *account);
	int  (*modify_clusters)    (account_cluster_cond_t *cluster_q,
				    account_cluster_rec_t *cluster);
	int  (*modify_associations)(account_association_cond_t *assoc_q,
				    account_association_rec_t *assoc);
	int  (*remove_users)       (account_user_cond_t *user_q);
	int  (*remove_coord)       (char *account,
				    account_user_cond_t *user_q);
	int  (*remove_accounts)    (account_account_cond_t *account_q);
	int  (*remove_clusters)    (account_cluster_cond_t *cluster_q);
	int  (*remove_associations)(account_association_cond_t *assoc_q);
	List (*get_users)          (account_user_cond_t *user_q);
	List (*get_accounts)       (account_account_cond_t *account_q);
	List (*get_clusters)       (account_cluster_cond_t *cluster_q);
	List (*get_associations)   (account_association_cond_t *assoc_q);
	int (*get_hourly_usage)    (account_association_rec_t *acct_assoc,
				    time_t start, 
				    time_t end);
	int (*get_daily_usage)     (account_association_rec_t *acct_assoc,
				    time_t start, 
				    time_t end);
	int (*get_monthly_usage)   (account_association_rec_t *acct_assoc,
				    time_t start, 
				    time_t end);
} slurm_account_storage_ops_t;

typedef struct slurm_account_storage_context {
	char	       	*account_storage_type;
	plugrack_t     	plugin_list;
	plugin_handle_t	cur_plugin;
	int		account_storage_errno;
	slurm_account_storage_ops_t ops;
} slurm_account_storage_context_t;

static slurm_account_storage_context_t * g_account_storage_context = NULL;
static pthread_mutex_t		g_account_storage_context_lock = 
					PTHREAD_MUTEX_INITIALIZER;

/*
 * Local functions
 */
static slurm_account_storage_ops_t *_account_storage_get_ops(
	slurm_account_storage_context_t *c);
static slurm_account_storage_context_t *_account_storage_context_create(
	const char *account_storage_type);
static int _account_storage_context_destroy(
	slurm_account_storage_context_t *c);

/*
 * Locate and load the appropriate plugin
 */
static slurm_account_storage_ops_t * _account_storage_get_ops(
	slurm_account_storage_context_t *c)
{
	/*
	 * Must be synchronized with slurm_account_storage_ops_t above.
	 */
	static const char *syms[] = {
		"account_storage_p_add_users",
		"account_storage_p_add_coord",
		"account_storage_p_add_accounts",
		"account_storage_p_add_clusters",
		"account_storage_p_add_associations",
		"account_storage_p_modify_users",
		"account_storage_p_modify_user_admin_level",
		"account_storage_p_modify_accounts",
		"account_storage_p_modify_clusters",
		"account_storage_p_modify_associations",
		"account_storage_p_remove_users",
		"account_storage_p_remove_coord",
		"account_storage_p_remove_accounts",
		"account_storage_p_remove_clusters",
		"account_storage_p_remove_associations",
		"account_storage_p_get_users",
		"account_storage_p_get_accounts",
		"account_storage_p_get_clusters",
		"account_storage_p_get_associations",
		"account_storage_p_get_hourly_usage",
		"account_storage_p_get_daily_usage",
		"account_storage_p_get_monthly_usage"
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
		plugrack_set_major_type( c->plugin_list, "account_storage" );
		plugrack_set_paranoia( c->plugin_list,
				       PLUGRACK_PARANOIA_NONE,
				       0 );
		plugin_dir = slurm_get_plugin_dir();
		plugrack_read_dir( c->plugin_list, plugin_dir );
		xfree(plugin_dir);
	}

	c->cur_plugin = plugrack_use_by_type( c->plugin_list,
					      c->account_storage_type );
	if ( c->cur_plugin == PLUGIN_INVALID_HANDLE ) {
		error( "cannot find account_storage plugin for %s", 
			c->account_storage_type );
		return NULL;
	}

	/* Dereference the API. */
	if ( plugin_get_syms( c->cur_plugin,
			      n_syms,
			      syms,
			      (void **) &c->ops ) < n_syms ) {
		error( "incomplete account_storage plugin detected" );
		return NULL;
	}

	return &c->ops;
}

/*
 * Create a account_storage context
 */
static slurm_account_storage_context_t *_account_storage_context_create(
	const char *account_storage_type)
{
	slurm_account_storage_context_t *c;

	if ( account_storage_type == NULL ) {
		debug3( "_account_storage_context_create: no uler type" );
		return NULL;
	}

	c = xmalloc( sizeof( slurm_account_storage_context_t ) );
	c->account_storage_type	= xstrdup( account_storage_type );
	c->plugin_list	= NULL;
	c->cur_plugin	= PLUGIN_INVALID_HANDLE;
	c->account_storage_errno	= SLURM_SUCCESS;

	return c;
}

/*
 * Destroy a account_storage context
 */
static int _account_storage_context_destroy(slurm_account_storage_context_t *c)
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

	xfree( c->account_storage_type );
	xfree( c );

	return SLURM_SUCCESS;
}

extern void destroy_account_user_rec(void *object)
{
	account_user_rec_t *account_user = (account_user_rec_t *)object;

	if(account_user) {
		xfree(account_user->name);
		xfree(account_user->default_account);
		xfree(account_user);
	}
}

extern void destroy_account_account_rec(void *object)
{
	account_account_rec_t *account_account =
		(account_account_rec_t *)object;

	if(account_account) {
		xfree(account_account->name);
		xfree(account_account->description);
		xfree(account_account->organization);
		if(account_account->coodinators)
			list_destroy(account_account->coodinators);
		xfree(account_account);
	}
}

extern void destroy_account_cluster_rec(void *object)
{
	account_cluster_rec_t *account_cluster =
		(account_cluster_rec_t *)object;

	if(account_cluster) {
		xfree(account_cluster->name);
		xfree(account_cluster->interface_node);
		if(account_cluster->accounting_list)
			list_destroy(account_cluster->accounting_list);
		xfree(account_cluster);
	}
}

extern void destroy_account_accounting_rec(void *object)
{
	account_accounting_rec_t *account_accounting =
		(account_accounting_rec_t *)object;

	if(account_accounting) {
		xfree(account_accounting);
	}
}

extern void destroy_account_association_rec(void *object)
{
	account_association_rec_t *account_association = 
		(account_association_rec_t *)object;

	if(account_association) {
		xfree(account_association->user);
		xfree(account_association->account);
		xfree(account_association->cluster);
		xfree(account_association->partition);
		if(account_association->accounting_list)
			list_destroy(account_association->accounting_list);
		xfree(account_association);
	}
}

extern void destroy_account_user_cond(void *object)
{
	account_user_cond_t *account_user = (account_user_cond_t *)object;

	if(account_user) {
		if(account_user->user_list)
			list_destroy(account_user->user_list);
		if(account_user->def_account_list)
			list_destroy(account_user->def_account_list);
		xfree(account_user);
	}
}

extern void destroy_account_account_cond(void *object)
{
	account_account_cond_t *account_account =
		(account_account_cond_t *)object;

	if(account_account) {
		if(account_account->account_list)
			list_destroy(account_account->account_list);
		if(account_account->description_list)
			list_destroy(account_account->description_list);
		if(account_account->organization_list)
			list_destroy(account_account->organization_list);
		xfree(account_account);
	}
}

extern void destroy_account_cluster_cond(void *object)
{
	account_cluster_cond_t *account_cluster =
		(account_cluster_cond_t *)object;

	if(account_cluster) {
		if(account_cluster->cluster_list)
			list_destroy(account_cluster->cluster_list);
		xfree(account_cluster);
	}
}

extern void destroy_account_association_cond(void *object)
{
	account_association_cond_t *account_association = 
		(account_association_cond_t *)object;

	if(account_association) {
		if(account_association->id_list)
			list_destroy(account_association->id_list);
		if(account_association->user_list)
			list_destroy(account_association->user_list);
		if(account_association->account_list)
			list_destroy(account_association->account_list);
		if(account_association->cluster_list)
			list_destroy(account_association->cluster_list);
		xfree(account_association);
	}
}

extern char *account_expedite_str(account_expedite_level_t level)
{
	switch(level) {
	case ACCOUNT_EXPEDITE_NOTSET:
		return "Not Set";
		break;
	case ACCOUNT_EXPEDITE_NORMAL:
		return "Normal";
		break;
	case ACCOUNT_EXPEDITE_EXPEDITE:
		return "Expedite";
		break;
	case ACCOUNT_EXPEDITE_STANDBY:
		return "Standby";
		break;
	case ACCOUNT_EXPEDITE_EXEMPT:
		return "Exempt";
		break;
	default:
		return "Unknown";
		break;
	}
	return "Unknown";
}

extern account_expedite_level_t str_2_account_expedite(char *level)
{
	if(!level) {
		return ACCOUNT_EXPEDITE_NOTSET;
	} else if(!strncasecmp(level, "Normal", 1)) {
		return ACCOUNT_EXPEDITE_NORMAL;
	} else if(!strncasecmp(level, "Expedite", 3)) {
		return ACCOUNT_EXPEDITE_EXPEDITE;
	} else if(!strncasecmp(level, "Standby", 1)) {
		return ACCOUNT_EXPEDITE_STANDBY;
	} else if(!strncasecmp(level, "Exempt", 3)) {
		return ACCOUNT_EXPEDITE_EXEMPT;
	} else {
		return ACCOUNT_EXPEDITE_NOTSET;		
	}
}

extern char *account_admin_level_str(account_admin_level_t level)
{
	switch(level) {
	case ACCOUNT_ADMIN_NOTSET:
		return "Not Set";
		break;
	case ACCOUNT_ADMIN_NONE:
		return "None";
		break;
	case ACCOUNT_ADMIN_OPERATOR:
		return "Operator";
		break;
	case ACCOUNT_ADMIN_SUPER_USER:
		return "Administrator";
		break;
	default:
		return "Unknown";
		break;
	}
	return "Unknown";
}

extern account_admin_level_t str_2_account_admin_level(char *level)
{
	if(!level) {
		return ACCOUNT_ADMIN_NOTSET;
	} else if(!strncasecmp(level, "None", 1)) {
		return ACCOUNT_ADMIN_NONE;
	} else if(!strncasecmp(level, "Operator", 1)) {
		return ACCOUNT_ADMIN_OPERATOR;
	} else if(!strncasecmp(level, "SuperUser", 1) 
		  || !strncasecmp(level, "Admin", 1)) {
		return ACCOUNT_ADMIN_SUPER_USER;
	} else {
		return ACCOUNT_ADMIN_NOTSET;		
	}	
}

/*
 * Initialize context for account_storage plugin
 */
extern int slurm_account_storage_init(void)
{
	int retval = SLURM_SUCCESS;
	char *account_storage_type = NULL;
	
	slurm_mutex_lock( &g_account_storage_context_lock );

	if ( g_account_storage_context )
		goto done;

	account_storage_type = slurm_get_account_storage_type();
	g_account_storage_context = _account_storage_context_create(
		account_storage_type);
	if ( g_account_storage_context == NULL ) {
		error( "cannot create account_storage context for %s",
			 account_storage_type );
		retval = SLURM_ERROR;
		goto done;
	}

	if ( _account_storage_get_ops( g_account_storage_context ) == NULL ) {
		error( "cannot resolve account_storage plugin operations" );
		_account_storage_context_destroy( g_account_storage_context );
		g_account_storage_context = NULL;
		retval = SLURM_ERROR;
	}

 done:
	slurm_mutex_unlock( &g_account_storage_context_lock );
	xfree(account_storage_type);
	return retval;
}

extern int slurm_account_storage_fini(void)
{
	int rc;

	if (!g_account_storage_context)
		return SLURM_SUCCESS;

//	(*(g_account_storage_context->ops.account_storage_fini))();
	rc = _account_storage_context_destroy( g_account_storage_context );
	g_account_storage_context = NULL;
	return rc;
}

extern int account_storage_g_add_users(List user_list)
{
	if (slurm_account_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_account_storage_context->ops.add_users))(user_list);
}

extern int account_storage_g_add_coord(char *account,
				       account_user_cond_t *user_q)
{
	if (slurm_account_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_account_storage_context->ops.add_coord))
		(account, user_q);
}

extern int account_storage_g_add_accounts(List account_list)
{
	if (slurm_account_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_account_storage_context->ops.add_accounts))(account_list);
}

extern int account_storage_g_add_clusters(List cluster_list)
{
	if (slurm_account_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_account_storage_context->ops.add_clusters))(cluster_list);
}

extern int account_storage_g_add_associations(List association_list)
{
	if (slurm_account_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_account_storage_context->ops.add_associations))
		(association_list);
}

extern int account_storage_g_modify_users(account_user_cond_t *user_q,
					  account_user_rec_t *user)
{
	if (slurm_account_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_account_storage_context->ops.modify_users))(user_q, user);
}

extern int account_storage_g_modify_user_admin_level(
	account_user_cond_t *user_q)
{
	if (slurm_account_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_account_storage_context->ops.modify_user_admin_level))
		(user_q);
}

extern int account_storage_g_modify_accounts(account_account_cond_t *account_q,
					     account_account_rec_t *account)
{
	if (slurm_account_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_account_storage_context->ops.modify_accounts))
		(account_q, account);
}

extern int account_storage_g_modify_clusters(account_cluster_cond_t *cluster_q,
					     account_cluster_rec_t *cluster)
{
	if (slurm_account_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_account_storage_context->ops.modify_clusters))
		(cluster_q, cluster);
}

extern int account_storage_g_modify_associations(
	account_association_cond_t *assoc_q, account_association_rec_t *assoc)
{
	if (slurm_account_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_account_storage_context->ops.modify_associations))
		(assoc_q, assoc);
}

extern int account_storage_g_remove_users(account_user_cond_t *user_q)
{
	if (slurm_account_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_account_storage_context->ops.remove_users))(user_q);
}

extern int account_storage_g_remove_coord(char *account,
					  account_user_cond_t *user_q)
{
	if (slurm_account_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_account_storage_context->ops.remove_coord))
		(account, user_q);
}

extern int account_storage_g_remove_accounts(account_account_cond_t *account_q)
{
	if (slurm_account_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_account_storage_context->ops.remove_accounts))
		(account_q);
}

extern int account_storage_g_remove_clusters(account_cluster_cond_t *cluster_q)
{
	if (slurm_account_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_account_storage_context->ops.remove_clusters))
		(cluster_q);
}

extern int account_storage_g_remove_associations(
	account_association_cond_t *assoc_q)
{
	if (slurm_account_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_account_storage_context->ops.remove_associations))
		(assoc_q);
}

extern List account_storage_g_get_users(account_user_cond_t *user_q)
{
	if (slurm_account_storage_init() < 0)
		return NULL;
	return (*(g_account_storage_context->ops.get_users))(user_q);
}

extern List account_storage_g_get_accounts(account_account_cond_t *account_q)
{
	if (slurm_account_storage_init() < 0)
		return NULL;
	return (*(g_account_storage_context->ops.get_accounts))
		(account_q);
}

extern List account_storage_g_get_clusters(account_cluster_cond_t *cluster_q)
{
	if (slurm_account_storage_init() < 0)
		return NULL;
	return (*(g_account_storage_context->ops.get_clusters))
		(cluster_q);
}

extern List account_storage_g_get_associations(
	account_association_cond_t *assoc_q)
{
	if (slurm_account_storage_init() < 0)
		return NULL;
	return (*(g_account_storage_context->ops.get_associations))
		(assoc_q);
}

extern int account_storage_g_get_hourly_usage(
	account_association_rec_t *acct_assoc,
	time_t start, time_t end)
{
	if (slurm_account_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_account_storage_context->ops.get_hourly_usage))
		(acct_assoc, start, end);
}

extern int account_storage_g_get_daily_usage(
	account_association_rec_t *acct_assoc,
	time_t start, time_t end)
{
	if (slurm_account_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_account_storage_context->ops.get_daily_usage))
		(acct_assoc, start, end);
}

extern int account_storage_g_get_monthly_usage(
	account_association_rec_t *acct_assoc,
	time_t start, time_t end)
{
	if (slurm_account_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_account_storage_context->ops.get_monthly_usage))
		(acct_assoc, start, end);
}
