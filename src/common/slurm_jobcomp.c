/*****************************************************************************\
 *  slurm_jobcomp.h - implementation-independent job completion logging 
 *  API definitions
 *****************************************************************************
 *  Copyright (C) 2003 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jay Windley <jwindley@lnxi.com> and Moe Jette <jette@llnl.com>
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

#include <stdlib.h>
#include <string.h>

#include <pthread.h>

#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_jobcomp.h"
#include "src/common/xmalloc.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"


/*
 * WARNING:  Do not change the order of these fields or add additional
 * fields at the beginning of the structure.  If you do, job completion
 * logging plugins will stop working.  If you need to add fields, add them 
 * at the end of the structure.
 */
typedef struct slurm_jobcomp_ops {
	int          (*set_loc)   ( char *loc );
	int          (*job_write) ( uint32_t job_id, uint32_t user_id, char *job_name,
				char *job_state, char *partition, uint32_t time_limit,
				time_t start_time, time_t end_time, char *node_list);
	int          (*sa_errno)  ( void );
} slurm_jobcomp_ops_t;

/*
 * A global authentication context.  "Global" in the sense that there's
 * only one, with static bindings.  We don't export it.
 */

struct slurm_jobcomp_context {
	char *			jobcomp_type;
	plugrack_t		plugin_list;
	plugin_handle_t		cur_plugin;
	int			jobcomp_errno;
	slurm_jobcomp_ops_t	ops;
};

static slurm_jobcomp_context_t g_context = NULL;
static pthread_mutex_t      context_lock = PTHREAD_MUTEX_INITIALIZER;

static slurm_ctl_conf_t  conf;
static pthread_mutex_t   config_lock     = PTHREAD_MUTEX_INITIALIZER;

static char *
_get_plugin_dir( void )
{
	slurm_mutex_lock( &config_lock );
	if ( conf.slurmd_port == 0 )
		read_slurm_conf_ctl( &conf );
	if ( conf.plugindir == NULL )
		conf.plugindir = xstrdup( SLURM_PLUGIN_PATH );
	slurm_mutex_unlock( &config_lock );

	return conf.plugindir;
}

static char *
_get_jobcomp_type( void )
{
        slurm_mutex_lock( &config_lock );
        if ( conf.slurmd_port == 0 ) {
                read_slurm_conf_ctl( &conf );
        }
        if ( conf.job_comp_type == NULL ) {
                conf.job_comp_type = xstrdup( "jobcomp/none" );
        }
        slurm_mutex_unlock( &config_lock );

        return conf.job_comp_type;
}

static slurm_jobcomp_context_t
_slurm_jobcomp_context_create( const char *jobcomp_type)
{
	slurm_jobcomp_context_t c;

	if ( jobcomp_type == NULL ) {
		debug3( "_slurm_jobcomp_context_create: no authentication type" );
		return NULL;
	}

	c = xmalloc( sizeof( struct slurm_jobcomp_context ) );

	c->jobcomp_errno = SLURM_SUCCESS;

	/* Copy the job completion authentication type. */
	c->jobcomp_type = xstrdup( jobcomp_type );
	if ( c->jobcomp_type == NULL ) {
		debug3( "can't make local copy of authentication type" );
		xfree( c );
		return NULL; 
	}

	/* Plugin rack is demand-loaded on first reference. */
	c->plugin_list = NULL; 
	c->cur_plugin = PLUGIN_INVALID_HANDLE; 

	return c;
}

static int
_slurm_jobcomp_context_destroy( slurm_jobcomp_context_t c )
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

	xfree( c->jobcomp_type );
	xfree( c );

	return SLURM_SUCCESS;
}

/*
 * Resolve the operations from the plugin.
 */
static slurm_jobcomp_ops_t *
_slurm_jobcomp_get_ops( slurm_jobcomp_context_t c )
{
        /*
         * These strings must be kept in the same order as the fields
         * declared for slurm_auth_ops_t.
         */
	static const char *syms[] = {
		"slurm_jobcomp_set_location",
		"slurm_jobcomp_log_record",
		"slurm_jobcomp_get_errno"
	};
        int n_syms = sizeof( syms ) / sizeof( char * );

        /* Get the plugin list, if needed. */
        if ( c->plugin_list == NULL ) {
                c->plugin_list = plugrack_create();
                if ( c->plugin_list == NULL ) {
                        verbose( "Unable to create a plugin manager" );
                        return NULL;
                }

                plugrack_set_major_type( c->plugin_list, "jobcomp" );
                plugrack_set_paranoia( c->plugin_list, 
				       PLUGRACK_PARANOIA_NONE, 
				       0 );
                plugrack_read_dir( c->plugin_list, _get_plugin_dir() );
        }
  
        /* Find the correct plugin. */
        c->cur_plugin = 
		plugrack_use_by_type( c->plugin_list, c->jobcomp_type );
        if ( c->cur_plugin == PLUGIN_INVALID_HANDLE ) {
                verbose( "can't find a plugin for type %s", c->jobcomp_type );
                return NULL;
        }  

        /* Dereference the API. */
        if ( plugin_get_syms( c->cur_plugin,
                              n_syms,
                              syms,
                              (void **) &c->ops ) < n_syms ) {
                verbose( "incomplete plugin detected" );
                return NULL;
        }

        return &c->ops;
}

extern int
g_slurm_jobcomp_init( char *jobcomp_loc )
{
	int retval = SLURM_SUCCESS;

	slurm_mutex_lock( &context_lock );

	if ( g_context )
		_slurm_jobcomp_context_destroy(g_context);
	g_context = _slurm_jobcomp_context_create( _get_jobcomp_type() );
	if ( g_context == NULL ) {
		verbose( "cannot create a context for %s", _get_jobcomp_type() );
		retval = SLURM_ERROR;
		goto done;
	}

	if ( _slurm_jobcomp_get_ops( g_context ) == NULL ) {
		verbose( "cannot resolve plugin operations" );
		_slurm_jobcomp_context_destroy( g_context );
		g_context = NULL;
		retval = SLURM_ERROR;
	}

  done:
	if ( g_context )
		retval = (*(g_context->ops.set_loc))(jobcomp_loc);
	slurm_mutex_unlock( &context_lock );
	return retval;
}

extern void
g_slurm_jobcomp_fini( void )
{
	slurm_mutex_lock( &config_lock );
	if ( conf.slurmd_port ) {
		free_slurm_conf( &conf );
		conf.slurmd_port = 0;
	}
	slurm_mutex_unlock( &config_lock );
}

extern int
g_slurm_jobcomp_write(uint32_t job_id, uint32_t user_id, char *job_name, 
			char *job_state, char *partition, uint32_t time_limit,
			time_t start_time, time_t end_time, char *node_list)
{
	int retval = SLURM_SUCCESS;

	slurm_mutex_lock( &context_lock );
	if ( g_context )
		retval = (*(g_context->ops.job_write))(job_id, user_id, job_name,
				job_state, partition, time_limit, start_time, 
				end_time, node_list);
	else {
		error ("slurm_jobcomp plugin context not initialized");
		retval = ENOENT;
	}
	slurm_mutex_unlock( &context_lock );
	return retval;
}

extern int
g_slurm_jobcomp_errno(void)
{
	int retval = SLURM_SUCCESS;

	slurm_mutex_lock( &context_lock );
	if ( g_context )
		 retval = (*(g_context->ops.sa_errno))();
	else {
		error ("slurm_jobcomp plugin context not initialized");
		retval = ENOENT;
	}
	slurm_mutex_unlock( &context_lock );
	return retval;
}
