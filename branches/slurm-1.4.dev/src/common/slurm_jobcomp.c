/*****************************************************************************\
 *  slurm_jobcomp.c - implementation-independent job completion logging 
 *  functions
 *****************************************************************************
 *  Copyright (C) 2003 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jay Windley <jwindley@lnxi.com>, Morris Jette <jette1@llnl.com>
 *  LLNL-CODE-402394.
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_jobcomp.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"

/*
 * WARNING:  Do not change the order of these fields or add additional
 * fields at the beginning of the structure.  If you do, job completion
 * logging plugins will stop working.  If you need to add fields, add them 
 * at the end of the structure.
 */
typedef struct slurm_jobcomp_ops {
	int          (*set_loc)   ( char *loc );
	int          (*job_write) ( struct job_record *job_ptr);
	int          (*sa_errno)  ( void );
	char *       (*job_strerror)  ( int errnum );
	List         (*get_jobs)  ( List selected_steps,
				    List selected_parts, void *params );
	void         (*archive)   ( List selected_parts, void *params );
} slurm_jobcomp_ops_t;


/*
 * A global job completion context.  "Global" in the sense that there's
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

static slurm_jobcomp_context_t
_slurm_jobcomp_context_create( const char *jobcomp_type)
{
	slurm_jobcomp_context_t c;

	if ( jobcomp_type == NULL ) {
		debug3( "_slurm_jobcomp_context_create: no jobcomp type" );
		return NULL;
	}

	c = xmalloc( sizeof( struct slurm_jobcomp_context ) );

	c->jobcomp_errno = SLURM_SUCCESS;

	/* Copy the job completion job completion type. */
	c->jobcomp_type = xstrdup( jobcomp_type );
	if ( c->jobcomp_type == NULL ) {
		debug3( "can't make local copy of jobcomp type" );
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
	} else {
		plugin_unload(c->cur_plugin);
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
         * declared for slurm_jobcomp_ops_t.
         */
	static const char *syms[] = {
		"slurm_jobcomp_set_location",
		"slurm_jobcomp_log_record",
		"slurm_jobcomp_get_errno",
		"slurm_jobcomp_strerror",
		"slurm_jobcomp_get_jobs",
		"slurm_jobcomp_archive"
	};
        int n_syms = sizeof( syms ) / sizeof( char * );
	
	/* Find the correct plugin. */
        c->cur_plugin = plugin_load_and_link(c->jobcomp_type, n_syms, syms,
					     (void **) &c->ops);
        if ( c->cur_plugin != PLUGIN_INVALID_HANDLE ) 
        	return &c->ops;

	error("Couldn't find the specified plugin name for %s "
	      "looking at all files",
	      c->jobcomp_type);
	
	/* Get the plugin list, if needed. */
        if ( c->plugin_list == NULL ) {
		char *plugin_dir;
                c->plugin_list = plugrack_create();
                if ( c->plugin_list == NULL ) {
                        error( "Unable to create a plugin manager" );
                        return NULL;
                }

                plugrack_set_major_type( c->plugin_list, "jobcomp" );
                plugrack_set_paranoia( c->plugin_list, 
				       PLUGRACK_PARANOIA_NONE, 
				       0 );
		plugin_dir = slurm_get_plugin_dir();
                plugrack_read_dir( c->plugin_list, plugin_dir );
		xfree(plugin_dir);
        }
  
        /* Find the correct plugin. */
        c->cur_plugin = 
		plugrack_use_by_type( c->plugin_list, c->jobcomp_type );
        if ( c->cur_plugin == PLUGIN_INVALID_HANDLE ) {
                error( "can't find a plugin for type %s", c->jobcomp_type );
                return NULL;
        }  

        /* Dereference the API. */
        if ( plugin_get_syms( c->cur_plugin,
                              n_syms,
                              syms,
                              (void **) &c->ops ) < n_syms ) {
                error( "incomplete jobcomp plugin detected" );
                return NULL;
        }

        return &c->ops;
}

extern void 
jobcomp_destroy_job(void *object)
{
	jobcomp_job_rec_t *job = (jobcomp_job_rec_t *)object;
	if (job) {
		xfree(job->partition);
		xfree(job->start_time);
		xfree(job->end_time);
		xfree(job->uid_name);
		xfree(job->gid_name);
		xfree(job->nodelist);
		xfree(job->jobname);
		xfree(job->state);
		xfree(job->timelimit);
#ifdef HAVE_BG
		xfree(job->blockid);
		xfree(job->connection);
		xfree(job->reboot);
		xfree(job->rotate);
		xfree(job->geo);
		xfree(job->bg_start_point);
#endif
		xfree(job);
	}
}


extern int
g_slurm_jobcomp_init( char *jobcomp_loc )
{
	int retval = SLURM_SUCCESS;
	char *jobcomp_type;

	slurm_mutex_lock( &context_lock );

	if ( g_context )
		_slurm_jobcomp_context_destroy(g_context);

	jobcomp_type = slurm_get_jobcomp_type();
	g_context = _slurm_jobcomp_context_create( jobcomp_type );
	if ( g_context == NULL ) {
		error( "cannot create a context for %s", jobcomp_type );
		xfree(jobcomp_type);
		retval = SLURM_ERROR;
		goto done;
	}
	xfree(jobcomp_type);

	if ( _slurm_jobcomp_get_ops( g_context ) == NULL ) {
		error( "cannot resolve job completion plugin operations" );
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

extern int
g_slurm_jobcomp_fini(void)
{
	int rc;

	if ( !g_context)
		return SLURM_SUCCESS;

	rc = _slurm_jobcomp_context_destroy ( g_context );
	g_context = NULL;
	return SLURM_SUCCESS;
}

extern int
g_slurm_jobcomp_write(struct job_record *job_ptr)
{
	int retval = SLURM_SUCCESS;

	slurm_mutex_lock( &context_lock );
	if ( g_context )
		retval = (*(g_context->ops.job_write))(job_ptr);
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

extern char *
g_slurm_jobcomp_strerror(int errnum)
{
	char *retval = NULL;

	slurm_mutex_lock( &context_lock );
	if ( g_context )
		retval = (*(g_context->ops.job_strerror))(errnum);
	else
		error ("slurm_jobcomp plugin context not initialized");
	slurm_mutex_unlock( &context_lock );
	return retval;
}

extern List
g_slurm_jobcomp_get_jobs(List selected_steps,
			 List selected_parts, void *params)
{
	slurm_mutex_lock( &context_lock );
	if ( g_context )
		return (*(g_context->ops.get_jobs))
			(selected_steps, selected_parts, params);
	else
		error ("slurm_jobcomp plugin context not initialized");
	slurm_mutex_unlock( &context_lock );
	return NULL ;
}

extern void
g_slurm_jobcomp_archive(List selected_parts, void *params)
{
	slurm_mutex_lock( &context_lock );
	if ( g_context )
		(*(g_context->ops.archive))(selected_parts, params);
	else
		error ("slurm_jobcomp plugin context not initialized");
	slurm_mutex_unlock( &context_lock );
	return;
}
