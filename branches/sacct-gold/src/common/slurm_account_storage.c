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
	int  (*add_coord)          (char *account, List user_list);
	int  (*add_accounts)       (List account_list);
	int  (*add_clusters)       (List cluster_list);
	int  (*add_records)        (List record_list);
	int  (*modify_users)       (List user_list);
	int  (*modify_user_admin_level)(account_admin_level_t level, 
					List user_list);
	int  (*modify_accounts)    (List account_list);
	int  (*modify_clusters)    (List cluster_list);
	int  (*modify_records)     (List record_list);
	int  (*remove_users)       (List user_list);
	int  (*remove_coord)       (char *account, List user_list);
	int  (*remove_accounts)    (List account_list);
	int  (*remove_clusters)    (List cluster_list);
	int  (*remove_records)     (List record_list);
	List (*get_users)          (List selected_users,
				    void *params);
	List (*get_accounts)       (List selected_accounts,
				    void *param);
	List (*get_clusters)       (List selected_clusters,
				    void *params);
	List (*get_records)        (List selected_users,
				    List selected_accounts,
				    List selected_parts,
				    char *cluster,
				    void *params);
	int (*get_hourly_usage)    (account_record_rec_t *acct_rec,
				    time_t start, 
				    time_t end,
				    void *params);
	int (*get_daily_usage)     (account_record_rec_t *acct_rec,
				    time_t start, 
				    time_t end,
				    void *params);
	int (*get_monthly_usage)   (account_record_rec_t *acct_rec,
				    time_t start, 
				    time_t end,
				    void *params);
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
		"account_storage_p_add_records",
		"account_storage_p_modify_users",
		"account_storage_p_modify_user_admin_level",
		"account_storage_p_modify_accounts",
		"account_storage_p_modify_clusters",
		"account_storage_p_modify_records",
		"account_storage_p_remove_users",
		"account_storage_p_remove_coord",
		"account_storage_p_remove_accounts",
		"account_storage_p_remove_clusters",
		"account_storage_p_remove_records",
		"account_storage_p_get_users",
		"account_storage_p_get_accounts",
		"account_storage_p_get_clusters",
		"account_storage_p_get_records",
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

extern void destroy_account_acct_rec(void *object)
{
	account_acct_rec_t *account_acct =
		(account_acct_rec_t *)object;

	if(account_acct) {
		xfree(account_acct->name);
		xfree(account_acct->description);
		xfree(account_acct->organization);
		if(account_acct->coodinators)
			list_destroy(account_acct->coodinators);
		xfree(account_acct);
	}
}

extern void destroy_account_cluster_rec(void *object)
{
	account_cluster_rec_t *account_cluster =
		(account_cluster_rec_t *)object;

	if(account_cluster) {
		xfree(account_cluster->name);
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

extern void destroy_account_record_rec(void *object)
{
	account_record_rec_t *account_record = (account_record_rec_t *)object;

	if(account_record) {
		xfree(account_record->user);
		xfree(account_record->account);
		xfree(account_record->cluster);
		xfree(account_record->partition);
		if(account_record->accounting_list)
			list_destroy(account_record->accounting_list);
		xfree(account_record);
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

extern int account_storage_g_add_coord(char *account, List user_list)
{
	if (slurm_account_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_account_storage_context->ops.add_coord))
		(account, user_list);
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

extern int account_storage_g_add_records(List record_list)
{
	if (slurm_account_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_account_storage_context->ops.add_records))(record_list);
}

extern int account_storage_g_modify_users(List user_list)
{
	if (slurm_account_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_account_storage_context->ops.modify_users))(user_list);
}

extern int account_storage_g_modify_user_admin_level(
	account_admin_level_t level, List user_list)
{
	if (slurm_account_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_account_storage_context->ops.modify_user_admin_level))
		(level, user_list);
}

/* 
 * modify existing accounts in the accounting system 
 * IN:  account_list List of account_acct_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_modify_accounts(List account_list)
{
	if (slurm_account_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_account_storage_context->ops.modify_accounts))
		(account_list);
}

/* 
 * modify existing clusters in the accounting system 
 * IN:  cluster_list List of account_cluster_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_modify_clusters(List cluster_list)
{
	if (slurm_account_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_account_storage_context->ops.modify_clusters))
		(cluster_list);
}

/* 
 * modify existing records in the accounting system 
 * IN:  record_list List of account_record_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_modify_records(List record_list)
{
	if (slurm_account_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_account_storage_context->ops.modify_records))
		(record_list);
}

/* 
 * remove users from accounting system 
 * IN:  user_list List of account_user_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_remove_users(List user_list)
{
	if (slurm_account_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_account_storage_context->ops.remove_users))(user_list);
}

/* 
 * remove accounts from accounting system 
 * IN:  account_list List of account_acct_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_remove_accounts(List account_list)
{
	if (slurm_account_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_account_storage_context->ops.remove_accounts))
		(account_list);
}

/* 
 * remove clusters from accounting system 
 * IN:  cluster_list List of account_cluster_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_remove_clusters(List cluster_list)
{
	if (slurm_account_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_account_storage_context->ops.remove_clusters))
		(cluster_list);
}

/* 
 * remove records from accounting system 
 * IN:  record_list List of account_record_rec_t *
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int account_storage_g_remove_records(List record_list)
{
	if (slurm_account_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_account_storage_context->ops.remove_records))
		(record_list);
}

/* 
 * get info from the storage 
 * returns List of account_user_rec_t *
 * note List needs to be freed when called
 */
extern List account_storage_g_get_users(List selected_users,
					void *params)
{
	if (slurm_account_storage_init() < 0)
		return NULL;
	return (*(g_account_storage_context->ops.get_users))(selected_users,
							     params);
}

/* 
 * get info from the storage 
 * returns List of account_acct_rec_t *
 * note List needs to be freed when called
 */
extern List account_storage_g_get_accounts(List selected_accounts,
					   void *params)
{
	if (slurm_account_storage_init() < 0)
		return NULL;
	return (*(g_account_storage_context->ops.get_accounts))
		(selected_accounts, params);
}

/* 
 * get info from the storage 
 * returns List of account_cluster_rec_t *
 * note List needs to be freed when called
 */
extern List account_storage_g_get_clusters(List selected_clusters,
					   void *params)
{
	if (slurm_account_storage_init() < 0)
		return NULL;
	return (*(g_account_storage_context->ops.get_clusters))
		(selected_clusters, params);
}

/* 
 * get info from the storage 
 * returns List of account_record_rec_t *
 * note List needs to be freed when called
 */
extern List account_storage_g_get_records(List selected_users,
					  List selected_accounts,
					  List selected_parts,
					  char *cluster,
					  void *params)
{
	if (slurm_account_storage_init() < 0)
		return NULL;
	return (*(g_account_storage_context->ops.get_records))
		(selected_users, selected_accounts, selected_parts, 
		 cluster, params);
}

extern int account_storage_g_get_hourly_usage(account_record_rec_t *acct_rec,
					      time_t start,
					      time_t end,
					      void *params)
{
	if (slurm_account_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_account_storage_context->ops.get_hourly_usage))
		(acct_rec, start, end, params);
}

extern int account_storage_g_get_daily_usage(account_record_rec_t *acct_rec,
					     time_t start,
					     time_t end,
					     void *params)
{
	if (slurm_account_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_account_storage_context->ops.get_daily_usage))
		(acct_rec, start, end, params);
}

extern int account_storage_g_get_monthly_usage(account_record_rec_t *acct_rec,
					       time_t start,
					       time_t end,
					       void *params)
{
	if (slurm_account_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_account_storage_context->ops.get_monthly_usage))
		(acct_rec, start, end, params);
}
