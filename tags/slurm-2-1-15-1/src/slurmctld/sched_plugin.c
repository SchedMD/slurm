/*****************************************************************************\
 *  sched_plugin.c - scheduler plugin stub.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jay Windley <jwindley@lnxi.com>.
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

#include "src/slurmctld/gang.h"
#include "src/slurmctld/sched_plugin.h"
#include "src/slurmctld/slurmctld.h"


/* ************************************************************************ */
/*  TAG(                        slurm_sched_ops_t                        )  */
/* ************************************************************************ */
typedef struct slurm_sched_ops {
	int		(*schedule)		( void );
	int		(*newalloc)		( struct job_record * );
	int		(*freealloc)		( struct job_record * );
	uint32_t	(*initial_priority)	( uint32_t,
						  struct job_record * );
	void            (*job_is_pending)     	( void );
	int		(*reconfig)		( void );
	void            (*partition_change)    	( void );
	int		(*get_errno)		( void );
	char *		(*strerror)		( int );
	void		(*job_requeue)		( struct job_record *,
						  char *reason );
	char *		(*get_conf)		( void );
} slurm_sched_ops_t;


/* ************************************************************************ */
/*  TAG(                        slurm_sched_contex_t                     )  */
/* ************************************************************************ */
typedef struct slurm_sched_context {
	char	       	*sched_type;
	plugrack_t     	plugin_list;
	plugin_handle_t	cur_plugin;
	int		sched_errno;
	slurm_sched_ops_t ops;
} slurm_sched_context_t;

static slurm_sched_context_t	*g_sched_context = NULL;
static pthread_mutex_t		g_sched_context_lock = PTHREAD_MUTEX_INITIALIZER;


/* ************************************************************************ */
/*  TAG(                       slurm_sched_get_ops                       )  */
/* ************************************************************************ */
static slurm_sched_ops_t *
slurm_sched_get_ops( slurm_sched_context_t *c )
{
	/*
	 * Must be synchronized with slurm_sched_ops_t above.
	 */
	static const char *syms[] = {
		"slurm_sched_plugin_schedule",
		"slurm_sched_plugin_newalloc",
		"slurm_sched_plugin_freealloc",
		"slurm_sched_plugin_initial_priority",
		"slurm_sched_plugin_job_is_pending",
		"slurm_sched_plugin_reconfig",
		"slurm_sched_plugin_partition_change",
		"slurm_sched_get_errno",
		"slurm_sched_strerror",
		"slurm_sched_plugin_requeue",
		"slurm_sched_get_conf"
	};
	int n_syms = sizeof( syms ) / sizeof( char * );

	/* Find the correct plugin. */
        c->cur_plugin = plugin_load_and_link(c->sched_type, n_syms, syms,
					     (void **) &c->ops);
        if ( c->cur_plugin != PLUGIN_INVALID_HANDLE )
        	return &c->ops;

	error("Couldn't find the specified plugin name for %s "
	      "looking at all files",
	      c->sched_type);

	/* Get plugin list. */
	if ( c->plugin_list == NULL ) {
		char *plugin_dir;
		c->plugin_list = plugrack_create();
		if ( c->plugin_list == NULL ) {
			error( "cannot create plugin manager" );
			return NULL;
		}
		plugrack_set_major_type( c->plugin_list, "sched" );
		plugrack_set_paranoia( c->plugin_list,
				       PLUGRACK_PARANOIA_NONE,
				       0 );
		plugin_dir = slurm_get_plugin_dir();
		plugrack_read_dir( c->plugin_list, plugin_dir );
		xfree(plugin_dir);
	}

	c->cur_plugin = plugrack_use_by_type( c->plugin_list, c->sched_type );
	if ( c->cur_plugin == PLUGIN_INVALID_HANDLE ) {
		error( "cannot find scheduler plugin for %s", c->sched_type );
		return NULL;
	}

	/* Dereference the API. */
	if ( plugin_get_syms( c->cur_plugin,
			      n_syms,
			      syms,
			      (void **) &c->ops ) < n_syms ) {
		error( "incomplete scheduling plugin detected" );
		return NULL;
	}

	return &c->ops;
}


/* ************************************************************************ */
/*  TAG(                  slurm_sched_context_create                     )  */
/* ************************************************************************ */
static slurm_sched_context_t *
slurm_sched_context_create( const char *sched_type )
{
	slurm_sched_context_t *c;

	if ( sched_type == NULL ) {
		debug3( "slurm_sched_context:  no scheduler type" );
		return NULL;
	}

	c = xmalloc( sizeof( slurm_sched_context_t ) );
	c->sched_type	= xstrdup( sched_type );
	c->plugin_list	= NULL;
	c->cur_plugin	= PLUGIN_INVALID_HANDLE;
	c->sched_errno 	= SLURM_SUCCESS;

	return c;
}


/* ************************************************************************ */
/*  TAG(                  slurm_sched_context_destroy                    )  */
/* ************************************************************************ */
static int
slurm_sched_context_destroy( slurm_sched_context_t *c )
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

	xfree( c->sched_type );
	xfree( c );

	return rc;
}


/* *********************************************************************** */
/*  TAG(                        slurm_sched_init                        )  */
/*                                                                         */
/*  NOTE: The scheduler plugin can not be changed via reconfiguration      */
/*        due to background threads, job priorities, etc. Slurmctld must   */
/*        be restarted  and job priority changes may be required to change */
/*        the scheduler type.                                              */
/* *********************************************************************** */
extern int
slurm_sched_init( void )
{
	int retval = SLURM_SUCCESS;
	char *sched_type = NULL;

	slurm_mutex_lock( &g_sched_context_lock );

	if ( g_sched_context )
		goto done;

	sched_type = slurm_get_sched_type();
	g_sched_context = slurm_sched_context_create( sched_type );
	if ( g_sched_context == NULL ) {
		error( "cannot create scheduler context for %s",
			 sched_type );
		retval = SLURM_ERROR;
		goto done;
	}

	if ( slurm_sched_get_ops( g_sched_context ) == NULL ) {
		error( "cannot resolve scheduler plugin operations" );
		slurm_sched_context_destroy( g_sched_context );
		g_sched_context = NULL;
		retval = SLURM_ERROR;
		goto done;
	}

	if ( (slurm_get_preempt_mode() & PREEMPT_MODE_GANG) &&
	     (gs_init() != SLURM_SUCCESS))
		error( "cannot start gang scheduler ");

 done:
	slurm_mutex_unlock( &g_sched_context_lock );
	xfree(sched_type);
	return retval;
}

/* *********************************************************************** */
/*  TAG(                        slurm_sched_fini                        )  */
/* *********************************************************************** */
extern int
slurm_sched_fini( void )
{
	int rc;

	if (!g_sched_context)
		return SLURM_SUCCESS;

	rc = slurm_sched_context_destroy(g_sched_context);
	g_sched_context = NULL;

	if ( (slurm_get_preempt_mode() & PREEMPT_MODE_GANG) &&
	     (gs_fini() != SLURM_SUCCESS))
		error( "cannot stop gang scheduler" );

	return rc;
}


/* *********************************************************************** */
/*  TAG(                        slurm_sched_reconfig                    )  */
/* *********************************************************************** */
extern int
slurm_sched_reconfig( void )
{
	if ( slurm_sched_init() < 0 )
		return SLURM_ERROR;

	if ( (slurm_get_preempt_mode() & PREEMPT_MODE_GANG) &&
	     (gs_reconfig() != SLURM_SUCCESS))
		error( "cannot reconfigure gang scheduler" );

	return (*(g_sched_context->ops.reconfig))();
}

/* *********************************************************************** */
/*  TAG(                        slurm_sched_schedule                    )  */
/* *********************************************************************** */
int
slurm_sched_schedule( void )
{
	if ( slurm_sched_init() < 0 )
		return SLURM_ERROR;

#if 0
	/* Must have job write lock and node read lock set here */
	if ( (slurm_get_preempt_mode() & PREEMPT_MODE_GANG) &&
	     (gs_job_scan() != SLURM_SUCCESS))
		error( "gang scheduler could not rescan jobs" );
#endif

	return (*(g_sched_context->ops.schedule))();
}

/* *********************************************************************** */
/*  TAG(                        slurm_sched_newalloc                    )  */
/* *********************************************************************** */
int
slurm_sched_newalloc( struct job_record *job_ptr )
{
	if ( slurm_sched_init() < 0 )
		return SLURM_ERROR;

	if ( (slurm_get_preempt_mode() & PREEMPT_MODE_GANG) &&
	     (gs_job_start( job_ptr ) != SLURM_SUCCESS)) {
		error( "gang scheduler problem starting job %u",
		       job_ptr->job_id);
	}

	return (*(g_sched_context->ops.newalloc))( job_ptr );
}

/* *********************************************************************** */
/*  TAG(                        slurm_sched_freealloc                    )  */
/* *********************************************************************** */
int
slurm_sched_freealloc( struct job_record *job_ptr )
{
	if ( slurm_sched_init() < 0 )
		return SLURM_ERROR;

	if ( (slurm_get_preempt_mode() & PREEMPT_MODE_GANG) &&
	     (gs_job_fini( job_ptr ) != SLURM_SUCCESS)) {
		error( "gang scheduler problem finishing job %u",
		       job_ptr->job_id);
	}

	return (*(g_sched_context->ops.freealloc))( job_ptr );
}


/* *********************************************************************** */
/*  TAG(                   slurm_sched_initital_priority                )  */
/* *********************************************************************** */
uint32_t
slurm_sched_initial_priority( uint32_t last_prio,
			      struct job_record *job_ptr )
{
	if ( slurm_sched_init() < 0 )
		return SLURM_ERROR;

	return (*(g_sched_context->ops.initial_priority))( last_prio,
							   job_ptr );
}

/* *********************************************************************** */
/*  TAG(                   slurm_sched_job_is_pending                   )  */
/* *********************************************************************** */
void
slurm_sched_job_is_pending( void )
{
	if ( slurm_sched_init() < 0 )
		return;

	if ( (slurm_get_preempt_mode() & PREEMPT_MODE_GANG) &&
	     (gs_reconfig() != SLURM_SUCCESS))
		error( "cannot reconfigure gang scheduler" );

	(*(g_sched_context->ops.job_is_pending))();
}

/* *********************************************************************** */
/*  TAG(                   slurm_sched_partition_change                 )  */
/* *********************************************************************** */
void
slurm_sched_partition_change( void )
{
	if ( slurm_sched_init() < 0 )
		return;

#if 0
	/* Must have job write lock and node read lock set here */
	if ( (slurm_get_preempt_mode() & PREEMPT_MODE_GANG) &&
	     (gs_job_scan() != SLURM_SUCCESS))
		error( "gang scheduler could not rescan jobs" );
#endif

	(*(g_sched_context->ops.partition_change))();
}

/* *********************************************************************** */
/*  TAG(                   slurm_sched_p_get_errno                      )  */
/* *********************************************************************** */
int
slurm_sched_p_get_errno( void )
{
	if ( slurm_sched_init() < 0 )
		return SLURM_ERROR;

	return (*(g_sched_context->ops.get_errno))( );
}

/* *********************************************************************** */
/*  TAG(                   slurm_sched_p_strerror                       )  */
/* *********************************************************************** */
char *
slurm_sched_p_strerror( int errnum )
{
	if ( slurm_sched_init() < 0 )
		return NULL;

	return (*(g_sched_context->ops.strerror))( errnum );
}

/* *********************************************************************** */
/*  TAG(                   slurm_sched_requeue                          )  */
/* *********************************************************************** */
void
slurm_sched_requeue( struct job_record *job_ptr, char *reason )
{
        if ( slurm_sched_init() < 0 )
                return;

        (*(g_sched_context->ops.job_requeue))( job_ptr, reason );
}

/* *********************************************************************** */
/*  TAG(                   slurm_sched_p_get_conf                       )  */
/* *********************************************************************** */
char *
slurm_sched_p_get_conf( void )
{
        if ( slurm_sched_init() < 0 )
                return NULL;

        return (*(g_sched_context->ops.get_conf))( );
}


