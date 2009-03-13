/*****************************************************************************\
 *  slurm_priority.c - Define priority plugin functions
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#include "src/common/slurm_priority.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/xstring.h"

typedef struct slurm_priority_ops {
	uint32_t (*set)            (uint32_t last_prio,
				    struct job_record *job_ptr);
	void     (*reconfig)       ();
	int      (*set_max_usage)  (uint32_t procs, uint32_t half_life);
	void     (*set_assoc_usage)(acct_association_rec_t *assoc);
	List	 (*get_priority_factors) (List job_list);

} slurm_priority_ops_t;

typedef struct slurm_priority_context {
	char	       	*priority_type;
	plugrack_t     	plugin_list;
	plugin_handle_t	cur_plugin;
	int		priority_errno;
	slurm_priority_ops_t ops;
} slurm_priority_context_t;

static slurm_priority_context_t * g_priority_context = NULL;
static pthread_mutex_t		g_priority_context_lock = 
	PTHREAD_MUTEX_INITIALIZER;

/*
 * Local functions
 */
static slurm_priority_ops_t *_priority_get_ops(
	slurm_priority_context_t *c);
static slurm_priority_context_t *_priority_context_create(
	const char *priority_type);
static int _priority_context_destroy(
	slurm_priority_context_t *c);

/*
 * Locate and load the appropriate plugin
 */
static slurm_priority_ops_t * _priority_get_ops(
	slurm_priority_context_t *c)
{
	/*
	 * Must be synchronized with slurm_priority_ops_t above.
	 */
	static const char *syms[] = {
		"priority_p_set",
		"priority_p_reconfig",
		"priority_p_set_max_cluster_usage",
		"priority_p_set_assoc_usage",
		"priority_p_get_priority_factors_list",
	};
	int n_syms = sizeof( syms ) / sizeof( char * );

	/* Find the correct plugin. */
        c->cur_plugin = plugin_load_and_link(c->priority_type, n_syms, syms,
					     (void **) &c->ops);
        if ( c->cur_plugin != PLUGIN_INVALID_HANDLE ) 
        	return &c->ops;

	error("Couldn't find the specified plugin name for %s "
	      "looking at all files",
	      c->priority_type);

	/* Get plugin list. */
	if ( c->plugin_list == NULL ) {
		char *plugin_dir;
		c->plugin_list = plugrack_create();
		if ( c->plugin_list == NULL ) {
			error( "cannot create plugin manager" );
			return NULL;
		}
		plugrack_set_major_type( c->plugin_list, "priority" );
		plugrack_set_paranoia( c->plugin_list,
				       PLUGRACK_PARANOIA_NONE,
				       0 );
		plugin_dir = slurm_get_plugin_dir();
		plugrack_read_dir( c->plugin_list, plugin_dir );
		xfree(plugin_dir);
	}

	c->cur_plugin = plugrack_use_by_type( c->plugin_list,
					      c->priority_type );
	if ( c->cur_plugin == PLUGIN_INVALID_HANDLE ) {
		error( "cannot find accounting_storage plugin for %s", 
		       c->priority_type );
		return NULL;
	}

	/* Dereference the API. */
	if ( plugin_get_syms( c->cur_plugin,
			      n_syms,
			      syms,
			      (void **) &c->ops ) < n_syms ) {
		error( "incomplete priority plugin detected" );
		return NULL;
	}

	return &c->ops;
}

/*
 * Create a priority context
 */
static slurm_priority_context_t *_priority_context_create(
	const char *priority_type)
{
	slurm_priority_context_t *c;

	if ( priority_type == NULL ) {
		debug3( "_priority_context_create: no uler type" );
		return NULL;
	}

	c = xmalloc( sizeof( slurm_priority_context_t ) );
	c->priority_type	= xstrdup( priority_type );
	c->plugin_list	= NULL;
	c->cur_plugin	= PLUGIN_INVALID_HANDLE;
	c->priority_errno	= SLURM_SUCCESS;

	return c;
}

/*
 * Destroy a priority context
 */
static int _priority_context_destroy(slurm_priority_context_t *c)
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

	xfree( c->priority_type );
	xfree( c );

	return SLURM_SUCCESS;
}

/*
 * Initialize context for priority plugin
 */
extern int slurm_priority_init(void)
{
	int retval = SLURM_SUCCESS;
	char *priority_type = NULL;
	
	slurm_mutex_lock( &g_priority_context_lock );

	if ( g_priority_context )
		goto done;
	
	priority_type = slurm_get_priority_type();
	
	g_priority_context = _priority_context_create(priority_type);
	if ( g_priority_context == NULL ) {
		error( "cannot create priority context for %s",
		       priority_type );
		retval = SLURM_ERROR;
		goto done;
	}

	if ( _priority_get_ops( g_priority_context ) == NULL ) {
		error( "cannot resolve priority plugin operations" );
		_priority_context_destroy( g_priority_context );
		g_priority_context = NULL;
		retval = SLURM_ERROR;
	}

done:
	slurm_mutex_unlock( &g_priority_context_lock );
	xfree(priority_type);
	return retval;
}

extern int slurm_priority_fini(void)
{
	int rc;

	if (!g_priority_context)
		return SLURM_SUCCESS;

	rc = _priority_context_destroy( g_priority_context );
	g_priority_context = NULL;
	return rc;
}

extern uint32_t priority_g_set(uint32_t last_prio, struct job_record *job_ptr)
{
	if (slurm_priority_init() < 0)
		return 0;

	return (*(g_priority_context->ops.set))(last_prio, job_ptr);
}

extern void priority_g_reconfig()
{
	if (slurm_priority_init() < 0)
		return;

	(*(g_priority_context->ops.reconfig))();

	return;
}

extern int priority_g_set_max_cluster_usage(uint32_t procs, uint32_t half_life)
{
	if (slurm_priority_init() < 0)
		return SLURM_ERROR;

	return (*(g_priority_context->ops.set_max_usage))(procs, half_life);
}

extern void priority_g_set_assoc_usage(acct_association_rec_t *assoc)
{
	if (slurm_priority_init() < 0)
		return;

       (*(g_priority_context->ops.set_assoc_usage))(assoc);
       return;
}

extern List priority_g_get_priority_factors_list(List job_list)
{
	if (slurm_priority_init() < 0)
		return NULL;

	return (*(g_priority_context->ops.get_priority_factors))(job_list);
}
