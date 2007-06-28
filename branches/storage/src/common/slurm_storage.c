/*****************************************************************************\
 *  slurm_storage.c - storage plugin wrapper.
 *
 *  $Id: slurm_storage.c 10744 2007-01-11 20:09:18Z da $
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
#include "src/common/slurm_storage.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"

/*
 * Local data
 */

typedef struct slurm_storage_ops {
	int  (*jobacct_init)          (char *location);
	int  (*jobacct_fini)          ();
	int  (*jobacct_job_start)     (struct job_record *job_ptr);
	int  (*jobacct_job_complete)  (struct job_record *job_ptr);
	int  (*jobacct_step_start)    (struct step_record *step_ptr);
	int  (*jobacct_step_complete) (struct step_record *step_ptr);
	int  (*jobacct_job_suspend)   (struct job_record *job_ptr);
	List (*jobacct_get_jobs)      (List job_list,
				       List selected_steps,
				       List selected_parts,
				       void *params);	
	void (*jobacct_archive)       (List selected_parts,
				       void *params);
	
	int (*jobcomp_init)           (char * location);
	int (*jobcomp_fini)           ();
	int (*jobcomp_get_errno)      ();
	int (*jobcomp_log_record)     (struct job_record *job_ptr);
	char *(*jobcomp_strerror)     (int errnum);
	List (*jobcomp_get_jobs)      (List job_list,
				       List selected_steps,
				       List selected_parts,
				       void *params);	
	void (*jobcomp_archive)       (List selected_parts,
				       void *params);
} slurm_storage_ops_t;

typedef struct slurm_storage_context {
	char	       	*storage_type;
	plugrack_t     	plugin_list;
	plugin_handle_t	cur_plugin;
	int		storage_errno;
	slurm_storage_ops_t ops;
} slurm_storage_context_t;

static slurm_storage_context_t * g_storage_context = NULL;
static pthread_mutex_t		g_storage_context_lock = 
					PTHREAD_MUTEX_INITIALIZER;

/*
 * Local functions
 */
static slurm_storage_ops_t *_storage_get_ops(slurm_storage_context_t *c);
static slurm_storage_context_t *_storage_context_create(const char *storage_type);
static int _storage_context_destroy(slurm_storage_context_t *c);

/*
 * Locate and load the appropriate plugin
 */
static slurm_storage_ops_t * _storage_get_ops(slurm_storage_context_t *c)
{
	/*
	 * Must be synchronized with slurm_storage_ops_t above.
	 */
	static const char *syms[] = {
		"storage_p_jobacct_init",
		"storage_p_jobacct_fini",
		"storage_p_jobacct_job_start",
		"storage_p_jobacct_job_complete",
		"storage_p_jobacct_step_start",
		"storage_p_jobacct_step_complete",
		"storage_p_jobacct_suspend",
		"storage_p_jobacct_get_jobs",
		"storage_p_jobacct_archive",
		"storage_p_jobcomp_init",
		"storage_p_jobcomp_fini",
		"storage_p_jobcomp_get_errno",
		"storage_p_jobcomp_log_record",
		"storage_p_jobcomp_strerror",
		"storage_p_jobcomp_get_jobs",
		"storage_p_jobcomp_archive"
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
		plugrack_set_major_type( c->plugin_list, "storage" );
		plugrack_set_paranoia( c->plugin_list,
				       PLUGRACK_PARANOIA_NONE,
				       0 );
		plugin_dir = slurm_get_plugin_dir();
		plugrack_read_dir( c->plugin_list, plugin_dir );
		xfree(plugin_dir);
	}

	c->cur_plugin = plugrack_use_by_type( c->plugin_list, c->storage_type );
	if ( c->cur_plugin == PLUGIN_INVALID_HANDLE ) {
		error( "cannot find storage plugin for %s", 
			c->storage_type );
		return NULL;
	}

	/* Dereference the API. */
	if ( plugin_get_syms( c->cur_plugin,
			      n_syms,
			      syms,
			      (void **) &c->ops ) < n_syms ) {
		error( "incomplete storage plugin detected" );
		return NULL;
	}

	return &c->ops;
}

/*
 * Create a storage context
 */
static slurm_storage_context_t *_storage_context_create(const char *storage_type)
{
	slurm_storage_context_t *c;

	if ( storage_type == NULL ) {
		debug3( "_storage_context_create: no uler type" );
		return NULL;
	}

	c = xmalloc( sizeof( slurm_storage_context_t ) );
	c->storage_type	= xstrdup( storage_type );
	c->plugin_list	= NULL;
	c->cur_plugin	= PLUGIN_INVALID_HANDLE;
	c->storage_errno	= SLURM_SUCCESS;

	return c;
}

/*
 * Destroy a storage context
 */
static int _storage_context_destroy( slurm_storage_context_t *c )
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

	xfree( c->storage_type );
	xfree( c );

	return SLURM_SUCCESS;
}

/*
 * Initialize context for storage plugin
 */
extern int slurm_storage_init(void)
{
	int retval = SLURM_SUCCESS;
	char *storage_type = NULL;
	
	slurm_mutex_lock( &g_storage_context_lock );

	if ( g_storage_context )
		goto done;

	storage_type = slurm_get_storage_type();
	g_storage_context = _storage_context_create(storage_type);
	if ( g_storage_context == NULL ) {
		error( "cannot create storage context for %s",
			 storage_type );
		retval = SLURM_ERROR;
		goto done;
	}

	if ( _storage_get_ops( g_storage_context ) == NULL ) {
		error( "cannot resolve storage plugin operations" );
		_storage_context_destroy( g_storage_context );
		g_storage_context = NULL;
		retval = SLURM_ERROR;
	}

 done:
	slurm_mutex_unlock( &g_storage_context_lock );
	xfree(storage_type);
	return retval;
}

extern int slurm_storage_fini(void)
{
	int rc;

	if (!g_storage_context)
		return SLURM_SUCCESS;

	(*(g_storage_context->ops.jobacct_fini))();
	rc = _storage_context_destroy( g_storage_context );
	g_storage_context = NULL;
	return rc;
}

/* 
 * Initialize the storage make sure tables are created and in working
 * order
 */
extern int storage_g_jobacct_init (char *location)
{
	if (slurm_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_storage_context->ops.jobacct_init))(location);
}

/*
 * finish up storage connection
 */
extern int storage_g_jobacct_fini ()
{
	if (slurm_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_storage_context->ops.jobacct_fini))();
}

/* 
 * load into the storage the start of a job
 */
extern int storage_g_jobacct_job_start (struct job_record *job_ptr) 
{
	if (slurm_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_storage_context->ops.jobacct_job_start))(job_ptr);
}

/* 
 * load into the storage the end of a job
 */
extern int storage_g_jobacct_job_complete  (struct job_record *job_ptr)
{
	if (slurm_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_storage_context->ops.jobacct_job_complete))(job_ptr);
}

/* 
 * load into the storage the start of a job step
 */
extern int storage_g_jobacct_step_start (struct step_record *step_ptr)
{
	if (slurm_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_storage_context->ops.jobacct_step_start))(step_ptr);
}

/* 
 * load into the storage the end of a job step
 */
extern int storage_g_jobacct_step_complete (struct step_record *step_ptr)
{
	if (slurm_storage_init() < 0)
		return SLURM_ERROR;
	return (*(g_storage_context->ops.jobacct_step_complete))(step_ptr);
}

/* 
 * load into the storage a suspention of a job
 */
extern int storage_g_jobacct_job_suspend (struct job_record *job_ptr)
{
	if (slurm_storage_init() < 0)
		return SLURM_ERROR;
 	return (*(g_storage_context->ops.jobacct_job_suspend))(job_ptr);
}


/* 
 * get info from the storage 
 * returns List of job_rec_t *
 * note List needs to be freed when called
 */
extern void storage_g_jobacct_get_jobs(List job_list,
					List selected_steps,
					List selected_parts,
					void *params)
{
	if (slurm_storage_init() < 0)
		return;
 	(*(g_storage_context->ops.jobacct_get_jobs))(job_list,
						      selected_steps,
						      selected_parts,
						      params);
	return;
}

/* 
 * expire old info from the storage 
 */
extern void storage_g_jobacct_archive(List selected_parts, void *params)
{
	if (slurm_storage_init() < 0)
		return;
 	(*(g_storage_context->ops.jobacct_archive))(selected_parts, params);
	return;
}


/* job comp */
extern int storage_g_jobcomp_init(char * location)
{
	if (slurm_storage_init() < 0)
		return SLURM_ERROR;
 	return (*(g_storage_context->ops.jobcomp_init))(location);
}

extern int storage_g_jobcomp_fini()
{
	if (slurm_storage_init() < 0)
		return SLURM_ERROR;
 	return (*(g_storage_context->ops.jobcomp_fini))();
}

extern int storage_g_jobcomp_get_errno()
{
	if (slurm_storage_init() < 0)
		return SLURM_ERROR;
 	return (*(g_storage_context->ops.jobcomp_get_errno))();

}

extern int storage_g_jobcomp_log_record(struct job_record *job_ptr)
{
	if (slurm_storage_init() < 0)
		return SLURM_ERROR;
 	return (*(g_storage_context->ops.jobcomp_log_record))(job_ptr);
}

extern char *storage_g_jobcomp_strerror(int errnum)
{
	if (slurm_storage_init() < 0)
		return NULL;
 	return (*(g_storage_context->ops.jobcomp_strerror))(errnum);
}

extern void storage_g_jobcomp_get_jobs(List job_list,
					List selected_steps,
					List selected_parts,
					void *params)
{
	if (slurm_storage_init() < 0)
		return;
 	(*(g_storage_context->ops.jobcomp_get_jobs))(job_list,
						      selected_steps,
						      selected_parts,
						      params);
	return;
}

/* 
 * expire old info from the storage 
 */
extern void storage_g_jobcomp_archive(List selected_parts, void *params)
{
	if (slurm_storage_init() < 0)
		return;
 	(*(g_storage_context->ops.jobcomp_archive))(selected_parts, params);
	return;
}
