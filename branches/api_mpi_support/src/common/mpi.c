/*****************************************************************************\
 * src/common/mpi.c - Generic mpi selector for slurm
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondo1@llnl.gov>.
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <unistd.h>
#include <stdlib.h>

#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/env.h"
#include "src/common/mpi.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"


/*
 * WARNING:  Do not change the order of these fields or add additional
 * fields at the beginning of the structure.  If you do, job completion
 * logging plugins will stop working.  If you need to add fields, add them 
 * at the end of the structure.
 */
typedef struct slurm_mpi_ops {
	int          (*init)          (slurmd_job_t *job, int rank);
	int          (*create_thread) (mpi_hook_client_info_t *job,
				       char ***env);
	int          (*single_task)   (void);
	int          (*exit)          (void);
} slurm_mpi_ops_t;

struct slurm_mpi_context {
	char *			mpi_type;
	plugrack_t              plugin_list;
	plugin_handle_t         cur_plugin;
	int                     mpi_errno;
	slurm_mpi_ops_t	        ops;
};

static slurm_mpi_context_t g_context = NULL;
static pthread_mutex_t      context_lock = PTHREAD_MUTEX_INITIALIZER;

static slurm_mpi_context_t
_slurm_mpi_context_create(const char *mpi_type)
{
	slurm_mpi_context_t c;

	if ( mpi_type == NULL ) {
		debug3( "_slurm_mpi_context_create: no mpi type" );
		return NULL;
	}

	c = xmalloc(sizeof(struct slurm_mpi_context));

	c->mpi_errno = SLURM_SUCCESS;

	/* Copy the job completion authentication type. */
	c->mpi_type = xstrdup(mpi_type);
	if (c->mpi_type == NULL ) {
		debug3( "can't make local copy of mpi type" );
		xfree(c);
		return NULL;
	}

	/* Plugin rack is demand-loaded on first reference. */
	c->plugin_list = NULL;
	c->cur_plugin = PLUGIN_INVALID_HANDLE;

	return c;
}

static int
_slurm_mpi_context_destroy( slurm_mpi_context_t c )
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

	xfree(c->mpi_type);
	xfree(c);

	return SLURM_SUCCESS;
}

/*
 * Resolve the operations from the plugin.
 */
static slurm_mpi_ops_t * 
_slurm_mpi_get_ops( slurm_mpi_context_t c )
{
	/*
	 * These strings must be kept in the same order as the fields
	 * declared for slurm_mpi_ops_t.
	 */
	static const char *syms[] = {
		"mpi_p_init",
		"mpi_p_thr_create",
		"mpi_p_single_task",
		"mpi_p_exit"		
	};
	int n_syms = sizeof( syms ) / sizeof( char * );
	char *plugin_dir = NULL;
	
	/* Get the plugin list, if needed. */
	if ( c->plugin_list == NULL ) {
		c->plugin_list = plugrack_create();
		if ( c->plugin_list == NULL ) {
			error("Unable to create a plugin manager");
			return NULL; 
		}

		plugrack_set_major_type(c->plugin_list, "mpi");
		plugrack_set_paranoia(c->plugin_list,
				      PLUGRACK_PARANOIA_NONE,
				      0);
		plugin_dir = slurm_get_plugin_dir();
		plugrack_read_dir(c->plugin_list, plugin_dir);
		xfree(plugin_dir);
	}
	
	if (strcasecmp (c->mpi_type, "mpi/list") == 0) { 
		plugrack_print_all_plugin(c->plugin_list);
		exit(0);
	} else {
		/* Find the correct plugin. */
		c->cur_plugin = plugrack_use_by_type(c->plugin_list, 
						     c->mpi_type);
		if ( c->cur_plugin == PLUGIN_INVALID_HANDLE ) {
			error("can't find a valid plugin for type %s", 
				c->mpi_type);
			return NULL;
		}
	}

	/* Dereference the API. */
	if ( plugin_get_syms( c->cur_plugin,
				n_syms,
				syms,
				(void **) &c->ops ) < n_syms ) {
		error( "incomplete mpi plugin detected" );
		return NULL;
	}

	return &c->ops;
}

int _mpi_init (char *mpi_type)
{
	int retval = SLURM_SUCCESS;
	char *full_type = NULL;
	int got_default = 0;

	slurm_mutex_lock( &context_lock );

	if ( g_context )
		goto done;
	
	if (mpi_type == NULL) {
		mpi_type = slurm_get_mpi_default();
		got_default = 1;
	}
	if (mpi_type == NULL) {
		error("No MPI default set.");
		retval = SLURM_ERROR;
		goto done;		
	}
	setenvf (NULL, "SLURM_MPI_TYPE", "%s", mpi_type);
		
	full_type = xmalloc(sizeof(char) * (strlen(mpi_type)+5));
	sprintf(full_type,"mpi/%s",mpi_type);
       
	g_context = _slurm_mpi_context_create(full_type);
	xfree(full_type);
	if ( g_context == NULL ) {
		error( "cannot create a context for %s", mpi_type);
		retval = SLURM_ERROR;
		goto done;
	}

	if ( _slurm_mpi_get_ops( g_context ) == NULL ) {
		error( "cannot resolve plugin operations for %s", mpi_type);
		_slurm_mpi_context_destroy( g_context );
		g_context = NULL;
		retval = SLURM_ERROR;
	}
	
		
done:
	if(got_default)
		xfree(mpi_type);
	slurm_mutex_unlock( &context_lock );
	return retval;
}

int mpi_hook_client_init (char *mpi_type)
{
	debug("mpi type = %s", mpi_type);
	
	if(_mpi_init(mpi_type) == SLURM_ERROR) 
		return SLURM_ERROR;
	
	return SLURM_SUCCESS;
}


int mpi_hook_slurmstepd_init (slurmd_job_t *job, int rank)
{   
	char *mpi_type = getenvp (job->env, "SLURM_MPI_TYPE");
	
	debug("mpi type = %s", mpi_type);

	if(_mpi_init(mpi_type) == SLURM_ERROR) 
		return SLURM_ERROR;
	
	unsetenvp (job->env, "SLURM_MPI_TYPE");	
	return (*(g_context->ops.init))(job, rank);
}

int mpi_fini (void)
{
	int rc;

	if (!g_context)
		return SLURM_SUCCESS;

	rc = _slurm_mpi_context_destroy(g_context);
	return rc;
}

int mpi_hook_client_thr_create(mpi_hook_client_info_t *job, char ***env)
{
	if (_mpi_init(NULL) < 0)
		return SLURM_ERROR;
		
	return (*(g_context->ops.create_thread))(job, env);
}

int mpi_hook_client_single_task_per_node (void)
{
	if (_mpi_init(NULL) < 0)
		return SLURM_ERROR;
	
	return (*(g_context->ops.single_task))();
}

int mpi_hook_client_exit (void)
{
	if (_mpi_init(NULL) < 0)
		return SLURM_ERROR;
	
	return (*(g_context->ops.exit))();
}


