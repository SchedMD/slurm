/*****************************************************************************\
 *  slurm_clusteracct_storage.c - storage plugin wrapper.
 *
 *  $Id: slurm_clusteracct_storage.c 10744 2007-01-11 20:09:18Z da $
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
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
#include "src/common/slurm_clusteracct_storage.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"

/*
 * Local data
 */

typedef struct slurm_clusteracct_storage_ops {
	int  (*node_down)          (struct node_record *node_ptr,
				    time_t event_time,
				    char *reason);
	int  (*node_up)            (struct node_record *node_ptr,
				    time_t event_time);
	int  (*cluster_procs)      (uint32_t procs, time_t event_time);
	List (*get_hourly_usage)   (char *cluster, time_t start, time_t end,
				    void *params);
	List (*get_daily_usage)    (char *cluster, time_t start, time_t end,
				    void *params);
	List (*get_monthly_usage)  (char *cluster, time_t start, time_t end,
				    void *params);	
} slurm_clusteracct_storage_ops_t;

typedef struct slurm_clusteracct_storage_context {
	char	       	*clusteracct_storage_type;
	plugrack_t     	plugin_list;
	plugin_handle_t	cur_plugin;
	int		clusteracct_storage_errno;
	slurm_clusteracct_storage_ops_t ops;
} slurm_clusteracct_storage_context_t;

static slurm_clusteracct_storage_context_t *g_clusteracct_storage_context =
	NULL;
static pthread_mutex_t g_clusteracct_storage_context_lock = 
	PTHREAD_MUTEX_INITIALIZER;

/*
 * Local functions
 */
static slurm_clusteracct_storage_ops_t *_clusteracct_storage_get_ops(
	slurm_clusteracct_storage_context_t *c);
static slurm_clusteracct_storage_context_t *_clusteracct_storage_context_create(
	const char *clusteracct_storage_type);
static int _clusteracct_storage_context_destroy(
	slurm_clusteracct_storage_context_t *c);

/*
 * Locate and load the appropriate plugin
 */
static slurm_clusteracct_storage_ops_t * _clusteracct_storage_get_ops(
	slurm_clusteracct_storage_context_t *c)
{
	/*
	 * Must be synchronized with slurm_clusteracct_storage_ops_t above.
	 */
	static const char *syms[] = {
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
		plugrack_set_major_type( c->plugin_list,
					 "clusteracct_storage" );
		plugrack_set_paranoia( c->plugin_list,
				       PLUGRACK_PARANOIA_NONE,
				       0 );
		plugin_dir = slurm_get_plugin_dir();
		plugrack_read_dir( c->plugin_list, plugin_dir );
		xfree(plugin_dir);
	}

	c->cur_plugin = plugrack_use_by_type( c->plugin_list, 
					      c->clusteracct_storage_type );
	if ( c->cur_plugin == PLUGIN_INVALID_HANDLE ) {
		error( "cannot find clusteracct_storage plugin for %s", 
			c->clusteracct_storage_type );
		return NULL;
	}

	/* Dereference the API. */
	if ( plugin_get_syms( c->cur_plugin,
			      n_syms,
			      syms,
			      (void **) &c->ops ) < n_syms ) {
		error( "incomplete clusteracct_storage plugin detected: "
		       "Expecting %d", n_syms);
		return NULL;
	}

	return &c->ops;
}

/*
 * Create a clusteracct_storage context
 */
static slurm_clusteracct_storage_context_t *_clusteracct_storage_context_create(
	const char *clusteracct_storage_type)
{
	slurm_clusteracct_storage_context_t *c;

	if ( clusteracct_storage_type == NULL ) {
		debug3( "_clusteracct_storage_context_create: no uler type" );
		return NULL;
	}

	c = xmalloc( sizeof( slurm_clusteracct_storage_context_t ) );
	c->clusteracct_storage_type	= xstrdup( clusteracct_storage_type );
	c->plugin_list	= NULL;
	c->cur_plugin	= PLUGIN_INVALID_HANDLE;
	c->clusteracct_storage_errno	= SLURM_SUCCESS;

	return c;
}

/*
 * Destroy a clusteracct_storage context
 */
static int _clusteracct_storage_context_destroy( 
	slurm_clusteracct_storage_context_t *c )
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

	xfree( c->clusteracct_storage_type );
	xfree( c );

	return SLURM_SUCCESS;
}

extern void destroy_clusteracct_rec(void *object)
{
	clusteracct_rec_t *clusteracct_rec = (clusteracct_rec_t *)object;
	if(clusteracct_rec) {
		xfree(clusteracct_rec->cluster);
		xfree(clusteracct_rec);
	}
}


/*
 * Initialize context for clusteracct_storage plugin
 */
extern int slurm_clusteracct_storage_init(void)
{
	int retval = SLURM_SUCCESS;
	char *clusteracct_storage_type = NULL;
	
	slurm_mutex_lock( &g_clusteracct_storage_context_lock );

	if ( g_clusteracct_storage_context )
		goto done;

	clusteracct_storage_type = slurm_get_clusteracct_storage_type();
	g_clusteracct_storage_context =
		_clusteracct_storage_context_create(clusteracct_storage_type);
	if ( g_clusteracct_storage_context == NULL ) {
		error( "cannot create clusteracct_storage context for %s",
			 clusteracct_storage_type );
		retval = SLURM_ERROR;
		goto done;
	}

	if ( _clusteracct_storage_get_ops( g_clusteracct_storage_context )
	     == NULL ) {
		error( "cannot resolve clusteracct_storage plugin operations" );
		_clusteracct_storage_context_destroy( 
			g_clusteracct_storage_context);
		g_clusteracct_storage_context = NULL;
		retval = SLURM_ERROR;
	}

 done:
	slurm_mutex_unlock( &g_clusteracct_storage_context_lock );
	xfree(clusteracct_storage_type);
	return retval;
}

extern int slurm_clusteracct_storage_fini(void)
{
	int rc;

	if (!g_clusteracct_storage_context)
		return SLURM_SUCCESS;

	rc = _clusteracct_storage_context_destroy(
		g_clusteracct_storage_context);
	g_clusteracct_storage_context = NULL;
	return rc;
}

extern int clusteracct_storage_g_node_down(struct node_record *node_ptr,
					   time_t event_time,
					   char *reason)
{
	if (slurm_clusteracct_storage_init() < 0)
		return SLURM_ERROR;
 	return (*(g_clusteracct_storage_context->ops.node_down))
		(node_ptr, event_time, reason);
}

extern int clusteracct_storage_g_node_up(struct node_record *node_ptr,
					 time_t event_time)
{
	if (slurm_clusteracct_storage_init() < 0)
		return SLURM_ERROR;
 	return (*(g_clusteracct_storage_context->ops.node_up))
		(node_ptr, event_time);
}


extern int clusteracct_storage_g_cluster_procs(uint32_t procs,
					       time_t event_time)
{
	if (slurm_clusteracct_storage_init() < 0)
		return SLURM_ERROR;
 	return (*(g_clusteracct_storage_context->ops.cluster_procs))
		(procs, event_time);
}


/* 
 * get info from the storage 
 * returns List of clusteracct_rec_t *
 * note List needs to be freed when called
 */
extern List clusteracct_storage_g_get_hourly_usage(char *cluster, 
						   time_t start, 
						   time_t end,
						   void *params)
{
	if (slurm_clusteracct_storage_init() < 0)
		return NULL;
	return (*(g_clusteracct_storage_context->ops.get_hourly_usage))
		(cluster, start, end, params);
}

/* 
 * get info from the storage 
 * returns List of clusteracct_rec_t *
 * note List needs to be freed when called
 */
extern List clusteracct_storage_g_get_daily_usage(char *cluster, 
						  time_t start, 
						  time_t end,
						  void *params)
{
	if (slurm_clusteracct_storage_init() < 0)
		return NULL;
	return (*(g_clusteracct_storage_context->ops.get_daily_usage))
		(cluster, start, end, params);
}

/* 
 * get info from the storage 
 * fills in List of clusteracct_rec_t *
 * note List needs to be freed when called
 */
extern List clusteracct_storage_g_get_monthly_usage(char *cluster, 
						    time_t start, 
						    time_t end,
						    void *params)
{
	if (slurm_clusteracct_storage_init() < 0)
		return NULL;
	return (*(g_clusteracct_storage_context->ops.get_monthly_usage))
		(cluster, start, end, params);
}
