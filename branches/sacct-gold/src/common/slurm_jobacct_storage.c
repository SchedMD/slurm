/*****************************************************************************\
 *  slurm_jobacct_storage.c - storage plugin wrapper.
 *
 *  $Id: slurm_jobacct_storage.c 10744 2007-01-11 20:09:18Z da $
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
#include "src/common/slurm_jobacct_storage.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"

/*
 * Local data
 */

typedef struct slurm_jobacct_storage_ops {
	int  (*jobacct_storage_init)          (char *location);
	int  (*jobacct_storage_fini)          ();
	int  (*jobacct_storage_job_start)     (struct job_record *job_ptr);
	int  (*jobacct_storage_job_complete)  (struct job_record *job_ptr);
	int  (*jobacct_storage_step_start)    (struct step_record *step_ptr);
	int  (*jobacct_storage_step_complete) (struct step_record *step_ptr);
	int  (*jobacct_storage_job_suspend)   (struct job_record *job_ptr);
	List (*jobacct_storage_get_jobs)      (List job_list,
					       List selected_steps,
					       List selected_parts,
					       void *params);	
	void (*jobacct_storage_archive)       (List selected_parts,
				       void *params);
	
} slurm_jobacct_storage_ops_t;

typedef struct slurm_jobacct_storage_context {
	char	       	*jobacct_storage_type;
	plugrack_t     	plugin_list;
	plugin_handle_t	cur_plugin;
	int		jobacct_storage_errno;
	slurm_jobacct_storage_ops_t ops;
} slurm_jobacct_storage_context_t;

static slurm_jobacct_storage_context_t * g_jobacct_storage_context = NULL;
static pthread_mutex_t		g_jobacct_storage_context_lock = 
					PTHREAD_MUTEX_INITIALIZER;

/*
 * Local functions
 */
static slurm_jobacct_storage_ops_t *_jobacct_storage_get_ops(
	slurm_jobacct_storage_context_t *c);
static slurm_jobacct_storage_context_t *_jobacct_storage_context_create(
	const char *jobacct_storage_type);
static int _jobacct_storage_context_destroy(slurm_jobacct_storage_context_t *c);

/*
 * Locate and load the appropriate plugin
 */
static slurm_jobacct_storage_ops_t * _jobacct_storage_get_ops(
	slurm_jobacct_storage_context_t *c)
{
	/*
	 * Must be synchronized with slurm_jobacct_storage_ops_t above.
	 */
	static const char *syms[] = {
		"jobacct_storage_p_init",
		"jobacct_storage_p_fini",
		"jobacct_storage_p_job_start",
		"jobacct_storage_p_job_complete",
		"jobacct_storage_p_step_start",
		"jobacct_storage_p_step_complete",
		"jobacct_storage_p_suspend",
		"jobacct_storage_p_get_jobs",
		"jobacct_storage_p_archive",
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
		plugrack_set_major_type( c->plugin_list, "jobacct_storage" );
		plugrack_set_paranoia( c->plugin_list,
				       PLUGRACK_PARANOIA_NONE,
				       0 );
		plugin_dir = slurm_get_plugin_dir();
		plugrack_read_dir( c->plugin_list, plugin_dir );
		xfree(plugin_dir);
	}

	c->cur_plugin = plugrack_use_by_type( c->plugin_list,
					      c->jobacct_storage_type );
	if ( c->cur_plugin == PLUGIN_INVALID_HANDLE ) {
		error( "cannot find jobacct_storage plugin for %s", 
			c->jobacct_storage_type );
		return NULL;
	}

	/* Dereference the API. */
	if ( plugin_get_syms( c->cur_plugin,
			      n_syms,
			      syms,
			      (void **) &c->ops ) < n_syms ) {
		error( "incomplete jobacct_storage plugin detected" );
		return NULL;
	}

	return &c->ops;
}

/*
 * Create a jobacct_storage context
 */
static slurm_jobacct_storage_context_t *_jobacct_storage_context_create(
	const char *jobacct_storage_type)
{
	slurm_jobacct_storage_context_t *c;

	if ( jobacct_storage_type == NULL ) {
		debug3( "_jobacct_storage_context_create: no uler type" );
		return NULL;
	}

	c = xmalloc( sizeof( slurm_jobacct_storage_context_t ) );
	c->jobacct_storage_type	= xstrdup( jobacct_storage_type );
	c->plugin_list	= NULL;
	c->cur_plugin	= PLUGIN_INVALID_HANDLE;
	c->jobacct_storage_errno	= SLURM_SUCCESS;

	return c;
}

/*
 * Destroy a jobacct_storage context
 */
static int _jobacct_storage_context_destroy(slurm_jobacct_storage_context_t *c)
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

	xfree( c->jobacct_storage_type );
	xfree( c );

	return SLURM_SUCCESS;
}

/*
 * Initialize context for jobacct_storage plugin
 */
extern int slurm_jobacct_storage_init(void)
{
	int retval = SLURM_SUCCESS;
	char *jobacct_storage_type = NULL;
	
	slurm_mutex_lock( &g_jobacct_storage_context_lock );

	if ( g_jobacct_storage_context )
		goto done;

	jobacct_storage_type = slurm_get_jobacct_storage_type();
	g_jobacct_storage_context = _jobacct_storage_context_create(
		jobacct_storage_type);
	if ( g_jobacct_storage_context == NULL ) {
		error( "cannot create jobacct_storage context for %s",
			 jobacct_storage_type );
		retval = SLURM_ERROR;
		goto done;
	}

	if ( _jobacct_storage_get_ops( g_jobacct_storage_context ) == NULL ) {
		error( "cannot resolve jobacct_storage plugin operations" );
		_jobacct_storage_context_destroy( g_jobacct_storage_context );
		g_jobacct_storage_context = NULL;
		retval = SLURM_ERROR;
	}

 done:
	slurm_mutex_unlock( &g_jobacct_storage_context_lock );
	xfree(jobacct_storage_type);
	return retval;
}

extern int slurm_jobacct_storage_fini(void)
{
	int rc;

	if (!g_jobacct_storage_context)
		return SLURM_SUCCESS;

	(*(g_jobacct_storage_context->ops.jobacct_storage_fini))();
	rc = _jobacct_storage_context_destroy( g_jobacct_storage_context );
	g_jobacct_storage_context = NULL;
	return rc;
}

/* 
 * Initialize the jobacct_storage make sure tables are created and in working
 * order
 */
extern int jobacct_storage_g_init (char *location)
{
	if (slurm_jobacct_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_jobacct_storage_context->ops.jobacct_storage_init))(
		location);
}

/*
 * finish up storage connection
 */
extern int jobacct_storage_g_fini ()
{
	if (slurm_jobacct_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_jobacct_storage_context->ops.jobacct_storage_fini))();
}

/* 
 * load into the storage the start of a job
 */
extern int jobacct_storage_g_job_start (struct job_record *job_ptr) 
{
	if (slurm_jobacct_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_jobacct_storage_context->ops.jobacct_storage_job_start))
		(job_ptr);
}

/* 
 * load into the storage the end of a job
 */
extern int jobacct_storage_g_job_complete  (struct job_record *job_ptr)
{
	if (slurm_jobacct_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_jobacct_storage_context->ops.jobacct_storage_job_complete))
		(job_ptr);
}

/* 
 * load into the storage the start of a job step
 */
extern int jobacct_storage_g_step_start (struct step_record *step_ptr)
{
	if (slurm_jobacct_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_jobacct_storage_context->ops.jobacct_storage_step_start))
		(step_ptr);
}

/* 
 * load into the storage the end of a job step
 */
extern int jobacct_storage_g_step_complete (struct step_record *step_ptr)
{
	if (slurm_jobacct_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_jobacct_storage_context->ops.jobacct_storage_step_complete))
		(step_ptr);
}

/* 
 * load into the storage a suspention of a job
 */
extern int jobacct_storage_g_job_suspend (struct job_record *job_ptr)
{
	if (slurm_jobacct_storage_init() < 0)
		return SLURM_ERROR;
 	return (*(g_jobacct_storage_context->ops.jobacct_storage_job_suspend))
		(job_ptr);
}


/* 
 * get info from the storage 
 * returns List of job_rec_t *
 * note List needs to be freed when called
 */
extern void jobacct_storage_g_get_jobs(List job_list,
					List selected_steps,
					List selected_parts,
					void *params)
{
	if (slurm_jobacct_storage_init() < 0)
		return;
 	(*(g_jobacct_storage_context->ops.jobacct_storage_get_jobs))
		(job_list, selected_steps, selected_parts, params);
	return;
}

/* 
 * expire old info from the storage 
 */
extern void jobacct_storage_g_archive(List selected_parts, void *params)
{
	if (slurm_jobacct_storage_init() < 0)
		return;
 	(*(g_jobacct_storage_context->ops.jobacct_storage_archive))
		(selected_parts, params);
	return;
}

