/*****************************************************************************\
 *  select_plugin.c - node selection plugin wrapper.
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.gov>.
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <pthread.h>

#include "src/common/list.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"

/*
 * Local data
 */

typedef struct slurm_select_ops {
	int		(*state_save)		( char *dir_name );
	int	       	(*state_restore)	( char *dir_name );
	int 		(*node_init)		( struct node_record *node_ptr,
						  int node_cnt);
	int 		(*part_init)		( List part_list );
	int		(*job_test)		( struct job_record *job_ptr,
						  bitstr_t *bitmap, int min_nodes, 
						  int max_nodes );
	int		(*job_init)		( struct job_record *job_ptr );
	int		(*job_fini)		( struct job_record *job_ptr );
} slurm_select_ops_t;

typedef struct slurm_select_context {
	char	       	*select_type;
	plugrack_t     	plugin_list;
	plugin_handle_t	cur_plugin;
	int		select_errno;
	slurm_select_ops_t ops;
} slurm_select_context_t;

static slurm_select_context_t * g_select_context = NULL;
static pthread_mutex_t		g_select_context_lock = 
					PTHREAD_MUTEX_INITIALIZER;

/*
 * Local functions
 */
static slurm_select_context_t *	_select_context_create(const char *select_type);
static int 			_select_context_destroy(slurm_select_context_t *c);
static slurm_select_ops_t *	_select_get_ops(slurm_select_context_t *c);

/*
 * Locate and load the appropriate plugin
 */
static slurm_select_ops_t * _select_get_ops(slurm_select_context_t *c)
{
	/*
	 * Must be synchronized with slurm_select_ops_t above.
	 */
	static const char *syms[] = {
		"select_p_state_save",
		"select_p_state_restore",
		"select_p_node_init",
		"select_p_part_init",
		"select_p_job_test",
		"select_p_job_init",
		"select_p_job_fini"
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
		plugrack_set_major_type( c->plugin_list, "select" );
		plugrack_set_paranoia( c->plugin_list,
				       PLUGRACK_PARANOIA_NONE,
				       0 );
		plugin_dir = slurm_get_plugin_dir();
		plugrack_read_dir( c->plugin_list, plugin_dir );
		xfree(plugin_dir);
	}

	c->cur_plugin = plugrack_use_by_type( c->plugin_list, c->select_type );
	if ( c->cur_plugin == PLUGIN_INVALID_HANDLE ) {
		error( "cannot find node selection plugin for %s", 
			c->select_type );
		return NULL;
	}

	/* Dereference the API. */
	if ( plugin_get_syms( c->cur_plugin,
			      n_syms,
			      syms,
			      (void **) &c->ops ) < n_syms ) {
		error( "incomplete node selection plugin detected" );
		return NULL;
	}

	return &c->ops;
}

/*
 * Create a node selection context
 */
static slurm_select_context_t *_select_context_create(const char *select_type)
{
	slurm_select_context_t *c;

	if ( select_type == NULL ) {
		debug3( "_select_context_create: no uler type" );
		return NULL;
	}

	c = xmalloc( sizeof( slurm_select_context_t ) );
	c->select_type	= xstrdup( select_type );
	c->plugin_list	= NULL;
	c->cur_plugin	= PLUGIN_INVALID_HANDLE;
	c->select_errno	= SLURM_SUCCESS;

	return c;
}

/*
 * Destroy a node selection context
 */
static int _select_context_destroy( slurm_select_context_t *c )
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

	xfree( c->select_type );
	xfree( c );

	return SLURM_SUCCESS;
}

/*
 * Initialize context for node selection plugin
 */
extern int slurm_select_init(void)
{
	int retval = SLURM_SUCCESS;
	char *select_type = NULL;
	
	slurm_mutex_lock( &g_select_context_lock );

	if ( g_select_context ) goto done;

	select_type = slurm_get_select_type();
	g_select_context = _select_context_create(select_type);
	if ( g_select_context == NULL ) {
		error( "cannot create node selection context for %s",
			 select_type );
		retval = SLURM_ERROR;
		goto done;
	}

	if ( _select_get_ops( g_select_context ) == NULL ) {
		error( "cannot resolve node selection plugin operations" );
		_select_context_destroy( g_select_context );
		g_select_context = NULL;
		retval = SLURM_ERROR;
	}

 done:
	slurm_mutex_unlock( &g_select_context_lock );
	xfree(select_type);
	return retval;
}

/*
 * Save any global state information
 */
extern int select_g_state_save(char *dir_name)
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	return (*(g_select_context->ops.state_save))(dir_name);
}

/*
 * Initialize context for node selection plugin and
 * restore any global state information
 */
extern int select_g_state_restore(char *dir_name)
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	return (*(g_select_context->ops.state_restore))(dir_name);
}

/*
 * Note re/initialization of node record data structure
 */
extern int select_g_node_init(struct node_record *node_ptr, int node_cnt)
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	return (*(g_select_context->ops.node_init))(node_ptr, node_cnt);
}


/*
 * Note re/initialization of partition record data structure
 */
extern int select_g_part_init(List part_list)
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	return (*(g_select_context->ops.part_init))(part_list);
}

/*
 * Select the "best" nodes for given job from those available
 */
extern int select_g_job_test(struct job_record *job_ptr, bitstr_t *bitmap,
        int min_nodes, int max_nodes)
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	return (*(g_select_context->ops.job_test))(job_ptr, bitmap, 
		min_nodes, max_nodes);
}

/*
 * Note initiation of job is about to begin
 */
extern int select_g_job_init(struct job_record *job_ptr)
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	return (*(g_select_context->ops.job_init))(job_ptr);
}

/*
 * Note termination of job is starting
 */
extern int select_g_job_fini(struct job_record *job_ptr)
{
	if (slurm_select_init() < 0)
		return SLURM_ERROR;

	return (*(g_select_context->ops.job_fini))(job_ptr);
}

