/*****************************************************************************\
 *  slurm_topology.c - Topology plugin function setup.
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#include <pthread.h>

#include "src/common/log.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#if 0
#include "src/slurmctld/slurmctld.h"
#endif


/* ************************************************************************ */
/*  TAG(                        slurm_topo_ops_t                         )  */
/* ************************************************************************ */
typedef struct slurm_topo_ops {
	int		(*build_config)		( void );
	int		(*get_node_addr)	( char* node_name,
						  char** addr,
						  char** pattern );
} slurm_topo_ops_t;


/* ************************************************************************ */
/*  TAG(                        slurm_topo_contex_t                      )  */
/* ************************************************************************ */
typedef struct slurm_topo_context {
	char	       	*topo_type;
	plugrack_t     	plugin_list;
	plugin_handle_t	cur_plugin;
	int		topo_errno;
	slurm_topo_ops_t ops;
} slurm_topo_context_t;

static slurm_topo_context_t	*g_topo_context = NULL;
static pthread_mutex_t		g_topo_context_lock = PTHREAD_MUTEX_INITIALIZER;


/* ************************************************************************ */
/*  TAG(                       slurm_topo_get_ops                        )  */
/* ************************************************************************ */
static slurm_topo_ops_t *
slurm_topo_get_ops( slurm_topo_context_t *c )
{
	/*
	 * Must be synchronized with slurm_topo_ops_t above.
	 */
	static const char *syms[] = {
		"topo_build_config",
		"topo_get_node_addr",
	};
	int n_syms = sizeof( syms ) / sizeof( char * );

	/* Find the correct plugin. */
        c->cur_plugin = plugin_load_and_link(c->topo_type, n_syms, syms,
					     (void **) &c->ops);
        if ( c->cur_plugin != PLUGIN_INVALID_HANDLE ) 
        	return &c->ops;

	error("Couldn't find the specified plugin name for %s "
	      "looking at all files",
	      c->topo_type);
	
	/* Get plugin list. */
	if ( c->plugin_list == NULL ) {
		char *plugin_dir;
		c->plugin_list = plugrack_create();
		if ( c->plugin_list == NULL ) {
			error( "cannot create plugin manager" );
			return NULL;
		}
		plugrack_set_major_type( c->plugin_list, "topo" );
		plugrack_set_paranoia( c->plugin_list,
				       PLUGRACK_PARANOIA_NONE,
				       0 );
		plugin_dir = slurm_get_plugin_dir();
		plugrack_read_dir( c->plugin_list, plugin_dir );
		xfree(plugin_dir);
	}

	c->cur_plugin = plugrack_use_by_type( c->plugin_list, c->topo_type );
	if ( c->cur_plugin == PLUGIN_INVALID_HANDLE ) {
		error( "cannot find topology plugin for %s", c->topo_type );
		return NULL;
	}

	/* Dereference the API. */
	if ( plugin_get_syms( c->cur_plugin,
			      n_syms,
			      syms,
			      (void **) &c->ops ) < n_syms ) {
		error( "incomplete topology plugin detected" );
		return NULL;
	}

	return &c->ops;
}


/* ************************************************************************ */
/*  TAG(                  slurm_topo_context_create                      )  */
/* ************************************************************************ */
static slurm_topo_context_t *
slurm_topo_context_create( const char *topo_type )
{
	slurm_topo_context_t *c;

	if ( topo_type == NULL ) {
		debug3( "slurm_topo_context:  no topology type" );
		return NULL;
	}

	c = xmalloc( sizeof( slurm_topo_context_t ) );
	c->topo_type	= xstrdup( topo_type );
	c->plugin_list	= NULL;
	c->cur_plugin	= PLUGIN_INVALID_HANDLE;
	c->topo_errno 	= SLURM_SUCCESS;

	return c;
}


/* ************************************************************************ */
/*  TAG(                  slurm_topo_context_destroy                     )  */
/* ************************************************************************ */
static int
slurm_topo_context_destroy( slurm_topo_context_t *c )
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

	xfree( c->topo_type );
	xfree( c );

	return SLURM_SUCCESS;
}


/* *********************************************************************** */
/*  TAG(                        slurm_topo_init                         )  */
/*                                                                         */
/*  NOTE: The topology plugin can not be changed via reconfiguration       */
/*        due to background threads, job priorities, etc. Slurmctld must   */
/*        be restarted  and job priority changes may be required to change */
/*        the topology type.                                               */
/* *********************************************************************** */
extern int
slurm_topo_init( void )
{
	int retval = SLURM_SUCCESS;
	char *topo_type = NULL;
	
	slurm_mutex_lock( &g_topo_context_lock );

	if ( g_topo_context )
		goto done;

	topo_type = slurm_get_topology_plugin();
	g_topo_context = slurm_topo_context_create( topo_type );
	if ( g_topo_context == NULL ) {
		error( "cannot create topology context for %s",
			 topo_type );
		retval = SLURM_ERROR;
		goto done;
	}

	if ( slurm_topo_get_ops( g_topo_context ) == NULL ) {
		error( "cannot resolve topology plugin operations" );
		slurm_topo_context_destroy( g_topo_context );
		g_topo_context = NULL;
		retval = SLURM_ERROR;
	}

 done:
	slurm_mutex_unlock( &g_topo_context_lock );
	xfree(topo_type);
	return retval;
}

/* *********************************************************************** */
/*  TAG(                        slurm_topo_fini                         )  */
/* *********************************************************************** */
extern int
slurm_topo_fini( void )
{
	int rc;

	if (!g_topo_context)
		return SLURM_SUCCESS;

	rc = slurm_topo_context_destroy(g_topo_context);
	g_topo_context = NULL;
	return rc;
}


/* *********************************************************************** */
/*  TAG(                      slurm_topo_build_config                   )  */
/* *********************************************************************** */
extern int
slurm_topo_build_config( void )
{
	if ( slurm_topo_init() < 0 )
		return SLURM_ERROR;

	return (*(g_topo_context->ops.build_config))();
}


/* *********************************************************************** */
/*  TAG(                      slurm_topo_get_node_addr                  )  */
/* *********************************************************************** */
extern int
slurm_topo_get_node_addr( char* node_name, char ** addr, char** pattern )
{
	if ( slurm_topo_init() < 0 )
		return SLURM_ERROR;

	return (*(g_topo_context->ops.get_node_addr))(node_name,addr,pattern);
}

