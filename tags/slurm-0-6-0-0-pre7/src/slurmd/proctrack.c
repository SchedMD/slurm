/*****************************************************************************\
 *  proctrack.c - Process tracking plugin stub.
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
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

#include <pthread.h>

#include "src/common/log.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmd/slurmd_job.h"

/* ************************************************************************ */
/*  TAG(                        slurm_proctrack_ops_t                    )  */
/* ************************************************************************ */
typedef struct slurm_proctrack_ops {
	int		(*create)	( slurmd_job_t *job );
	int		(*add)		( slurmd_job_t *job, pid_t pid );
	int		(*signal)	( uint32_t id, int signal );
	int		(*destroy)	( uint32_t id );
	uint32_t	(*find_cont)	( pid_t pid );
} slurm_proctrack_ops_t;


/* ************************************************************************ */
/*  TAG(                        slurm_proctrack_contex_t                 )  */
/* ************************************************************************ */
typedef struct slurm_proctrack_context {
	char	       	*proctrack_type;
	plugrack_t     	plugin_list;
	plugin_handle_t	cur_plugin;
	int		op_errno;
	slurm_proctrack_ops_t ops;
} slurm_proctrack_context_t;

static slurm_proctrack_context_t *g_proctrack_context = NULL;
static pthread_mutex_t	g_proctrack_context_lock = PTHREAD_MUTEX_INITIALIZER;


/* ************************************************************************ */
/*  TAG(                        _proctrack_get_ops                       )  */
/* ************************************************************************ */
static slurm_proctrack_ops_t *
_proctrack_get_ops( slurm_proctrack_context_t *c )
{
	/*
	 * Must be synchronized with slurm_proctrack_ops_t above.
	 */
	static const char *syms[] = {
		"slurm_container_create",
		"slurm_container_add",
		"slurm_container_signal",
		"slurm_container_destroy",
		"slurm_container_find"
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
		plugrack_set_major_type( c->plugin_list, "proctrack" );
		plugrack_set_paranoia( c->plugin_list,
				       PLUGRACK_PARANOIA_NONE,
				       0 );
		plugin_dir = slurm_get_plugin_dir();
		plugrack_read_dir( c->plugin_list, plugin_dir );
		xfree(plugin_dir);
	}

	c->cur_plugin = plugrack_use_by_type( c->plugin_list, 
		c->proctrack_type );
	if ( c->cur_plugin == PLUGIN_INVALID_HANDLE ) {
		error( "cannot find proctrack plugin for %s", 
			c->proctrack_type );
		return NULL;
	}

	/* Dereference the API. */
	if ( plugin_get_syms( c->cur_plugin,
			      n_syms,
			      syms,
			      (void **) &c->ops ) < n_syms ) {
		error( "incomplete proctrack plugin detected" );
		return NULL;
	}

	return &c->ops;
}


/* ************************************************************************ */
/*  TAG(                   _proctrack_context_create                     )  */
/* ************************************************************************ */
static slurm_proctrack_context_t *
_proctrack_context_create( const char *proctrack_type )
{
	slurm_proctrack_context_t *c;

	if ( proctrack_type == NULL ) {
		debug3( "_proctrack_context_create:  no proctrack type" );
		return NULL;
	}

	c = xmalloc( sizeof( slurm_proctrack_context_t ) );
	c->proctrack_type  = xstrdup( proctrack_type );
	c->plugin_list	   = NULL;
	c->cur_plugin	   = PLUGIN_INVALID_HANDLE;
	c->op_errno        = SLURM_SUCCESS;

	return c;
}


/* ************************************************************************ */
/*  TAG(                   _proctrack_context_destroy                    )  */
/* ************************************************************************ */
static int
_proctrack_context_destroy( slurm_proctrack_context_t *c )
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

	xfree( c->proctrack_type );
	xfree( c );

	return SLURM_SUCCESS;
}


/* *********************************************************************** */
/*  TAG(                    slurm_proctrack_init                        )  */
/*                                                                         */
/*  NOTE: The proctrack plugin can only be changed by restarting slurmd    */
/*        without preserving state (-c option).                            */
/* *********************************************************************** */
extern int
slurm_proctrack_init( void )
{
	int retval = SLURM_SUCCESS;
	char *proctrack_type = NULL;
	
	slurm_mutex_lock( &g_proctrack_context_lock );

	if ( g_proctrack_context ) goto done;

	proctrack_type = slurm_get_proctrack_type();
	g_proctrack_context = _proctrack_context_create( proctrack_type );
	if ( g_proctrack_context == NULL ) {
		error( "cannot create proctrack context for %s",
			 proctrack_type );
		retval = SLURM_ERROR;
		goto done;
	}

	if ( _proctrack_get_ops( g_proctrack_context ) == NULL ) {
		error( "cannot resolve proctrack plugin operations for %s",
			proctrack_type );
		_proctrack_context_destroy( g_proctrack_context );
		g_proctrack_context = NULL;
		retval = SLURM_ERROR;
	}

 done:
	slurm_mutex_unlock( &g_proctrack_context_lock );
	xfree(proctrack_type);
	return retval;
}

/* *********************************************************************** */
/*  TAG(                    slurm_proctrack_fini                        )  */
/* *********************************************************************** */
extern int
slurm_proctrack_fini( void )
{
	int rc;

	if (!g_proctrack_context)
		return SLURM_SUCCESS;

	rc = _proctrack_context_destroy(g_proctrack_context);
	g_proctrack_context = NULL;
	return rc;
}

/* 
 * Create a container
 * job IN - slurmd_job_t structure
 * job->cont_id OUT - Plugin must fill in job->cont_id either here
 *                    or in slurm_container_add()
 *
 * Returns a SLURM errno.
 */
extern int
slurm_container_create(slurmd_job_t *job)
{
	if ( slurm_proctrack_init() < 0 )
		return 0;

	return (*(g_proctrack_context->ops.create))( job );
}

/*
 * Add a process to the specified container
 * job IN - slurmd_job_t structure
 * pid IN      - process ID to be added to the container
 * job->cont_id OUT - Plugin must fill in job->cont_id either here
 *                    or in slurm_container_create()
 *
 * Returns a SLURM errno.
 */
extern int
slurm_container_add(slurmd_job_t *job, pid_t pid)
{
	if ( slurm_proctrack_init() < 0 )
		return SLURM_ERROR;

	return (*(g_proctrack_context->ops.add))( job , pid );
}

/*
 * Signal all processes within a container
 * cont_id IN - container ID as returned by slurm_container_create()
 * signal IN  - signal to send, if zero then perform error checking 
 *              but do not send signal
 *
 * Returns a SLURM errno.
 */
extern int
slurm_container_signal(uint32_t cont_id, int signal)
{
	if ( slurm_proctrack_init() < 0 )
		return SLURM_ERROR;

	return (*(g_proctrack_context->ops.signal))( cont_id, signal );
}

/* 
 * Destroy a container, any processes within the container are not effected
 * cont_id IN - container ID as returned by slurm_container_create()
 *
 * Returns a SLURM errno.
*/
extern int
slurm_container_destroy(uint32_t cont_id)
{
	if ( slurm_proctrack_init() < 0 )
		return SLURM_ERROR;

	return (*(g_proctrack_context->ops.destroy))( cont_id );
}

/*
 * Get container ID for give process ID
 *
 * Returns a SLURM errno.
 */
extern uint32_t 
slurm_container_find(pid_t pid)
{
	if ( slurm_proctrack_init() < 0 )
		return SLURM_ERROR;

	return (*(g_proctrack_context->ops.find_cont))( pid );
}

