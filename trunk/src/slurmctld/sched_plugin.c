/*****************************************************************************\
 *  sched_plugin.c - scheduler plugin stub.
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Moe Jette <jette@llnl.gov>, Kevin Tew <tew1@llnl.gov>, et. al.
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
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmctld/sched_plugin.h"


/* ************************************************************************ */
/*  TAG(                        slurm_sched_ops_t                        )  */
/* ************************************************************************ */
typedef struct slurm_sched_ops {
	int		(*schedule)		( void );
	u_int32_t	(*initial_priority)	( u_int32_t );
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
static slurm_ctl_conf_t     conf;
static pthread_mutex_t      config_lock  = PTHREAD_MUTEX_INITIALIZER;


/* ************************************************************************ */
/*  TAG(                        get_plugin_dir                           )  */
/* ************************************************************************ */
static char *
get_plugin_dir( void )
{
	slurm_mutex_lock( &config_lock );
	if ( conf.slurmd_port == 0 ) {
		read_slurm_conf_ctl( &conf );
	}
	if ( conf.plugindir == NULL ) {
		conf.plugindir = xstrdup( SLURM_PLUGIN_PATH );
	}
	slurm_mutex_unlock( &config_lock );

        verbose( "Reading scheduling plugins from %s", conf.plugindir );
	return conf.plugindir;
}


/* ************************************************************************ */
/*  TAG(                        get_sched_type                           )  */
/* ************************************************************************ */
static char *
get_sched_type( void )
{
	slurm_mutex_lock( &config_lock );
	if ( conf.slurmd_port == 0 ) {
		read_slurm_conf_ctl( &conf );
	}
	if ( conf.schedtype == NULL ) {
		conf.schedtype = xstrdup( DEFAULT_SCHEDTYPE );
	}
	slurm_mutex_unlock( &config_lock );

	return conf.schedtype;
}


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
		"slurm_sched_plugin_initial_priority"
	};
	int n_syms = sizeof( syms ) / sizeof( char * );

	/* Get plugin list. */
	if ( c->plugin_list == NULL ) {
		c->plugin_list = plugrack_create();
		if ( c->plugin_list == NULL ) {
			error( "cannot create plugin manager" );
			return NULL;
		}
		plugrack_set_major_type( c->plugin_list, "sched" );
		plugrack_set_paranoia( c->plugin_list,
				       PLUGRACK_PARANOIA_NONE,
				       0 );
		plugrack_read_dir( c->plugin_list, get_plugin_dir() );
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
		verbose( "incomplete scheduling plugin" );
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
	if ( c->plugin_list ) {
		if ( plugrack_destroy( c->plugin_list ) != SLURM_SUCCESS ) {
			return SLURM_ERROR;
		}
	}

	xfree( c->sched_type );
	xfree( c );

	return SLURM_SUCCESS;
}


/* *********************************************************************** */
/*  TAG(                        slurm_sched_init                        )  */
/* *********************************************************************** */
int
slurm_sched_init( void )
{
	int retval = SLURM_SUCCESS;
	
	slurm_mutex_lock( &g_sched_context_lock );

	if ( g_sched_context ) goto done;

	g_sched_context = slurm_sched_context_create( get_sched_type() );
	if ( g_sched_context == NULL ) {
		verbose( "cannot create scheduler context for %s",
			 get_sched_type() );
		retval = SLURM_ERROR;
		goto done;
	}

	if ( slurm_sched_get_ops( g_sched_context ) == NULL ) {
		verbose( "cannot resolve scheduler plugin operations" );
		slurm_sched_context_destroy( g_sched_context );
		g_sched_context = NULL;
		retval = SLURM_ERROR;
	}

 done:
	slurm_mutex_unlock( &g_sched_context_lock );
	return retval;
}


/* *********************************************************************** */
/*  TAG(                        slurm_sched_schedule                    )  */
/* *********************************************************************** */
int
slurm_sched_schedule( void )
{
	if ( slurm_sched_init() < 0 )
		return SLURM_ERROR;
	
	return (*(g_sched_context->ops.schedule))();
}


/* *********************************************************************** */
/*  TAG(                   slurm_sched_initital_priority                )  */
/* *********************************************************************** */
u_int32_t
slurm_sched_initial_priority( u_int32_t max_prio )
{
	if ( slurm_sched_init() < 0 )
		return SLURM_ERROR;

	return (*(g_sched_context->ops.initial_priority))( max_prio );
}
