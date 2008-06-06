/*****************************************************************************\
 *  slurm_jobacct_gather.c - implementation-independent job accounting logging 
 *  functions
 *****************************************************************************
 *  Copyright (C) 2003-2007/ The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jay Windley <jwindley@lnxi.com>, Morris Jette <jette1@llnl.com>
 *  LLNL-CODE-402394.
 *  
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
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

/*****************************************************************************\
 *  Modification history
 *
 *  19 Jan 2005 by Andy Riebs <andy.riebs@hp.com>
 *  	 This file is derived from the file slurm_jobcomp.c, written by
 *  	 Morris Jette, et al.
\*****************************************************************************/

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"


/*
 * WARNING:  Do not change the order of these fields or add additional
 * fields at the beginning of the structure.  If you do, job accounting
 * plugins will stop working.  If you need to add fields, add them 
 * at the end of the structure.
 */
typedef struct slurm_jobacct_gather_ops {
	jobacctinfo_t *(*jobacct_gather_create)(jobacct_id_t *jobacct_id);
	void (*jobacct_gather_destroy)          (jobacctinfo_t *jobacct);
	int (*jobacct_gather_setinfo)        (jobacctinfo_t *jobacct, 
					      enum jobacct_data_type type, 
					      void *data);
	int (*jobacct_gather_getinfo)        (jobacctinfo_t *jobacct, 
					      enum jobacct_data_type type, 
					      void *data);
	void (*jobacct_gather_pack)   (jobacctinfo_t *jobacct, Buf buffer);
	int (*jobacct_gather_unpack)  (jobacctinfo_t **jobacct, Buf buffer);
	void (*jobacct_gather_aggregate)     (jobacctinfo_t *dest, 
					      jobacctinfo_t *from);
	int (*jobacct_gather_startpoll)      (uint16_t frequency);
	int (*jobacct_gather_endpoll)	     ();
	void (*jobacct_gather_change_poll)   (uint16_t frequency);
	void (*jobacct_gather_suspend_poll)  ();
	void (*jobacct_gather_resume_poll)   ();
	int (*jobacct_gather_set_proctrack_container_id)(uint32_t id);
	int (*jobacct_gather_add_task) (pid_t pid, jobacct_id_t *jobacct_id);
	jobacctinfo_t *(*jobacct_gather_stat_task)(pid_t pid);
	jobacctinfo_t *(*jobacct_gather_remove_task)(pid_t pid);
	void (*jobacct_gather_2_sacct)       (sacct_t *sacct, 
					      jobacctinfo_t *jobacct);
} slurm_jobacct_gather_ops_t;

/*
 * A global job accounting context.  "Global" in the sense that there's
 * only one, with static bindings.  We don't export it.
 */

typedef struct slurm_jobacct_gather_context {
	char 			*jobacct_gather_type;
	plugrack_t		plugin_list;
	plugin_handle_t		cur_plugin;
	int			jobacct_gather_errno;
	slurm_jobacct_gather_ops_t	ops;
} slurm_jobacct_gather_context_t;

static slurm_jobacct_gather_context_t *g_jobacct_gather_context = NULL;
static pthread_mutex_t g_jobacct_gather_context_lock = PTHREAD_MUTEX_INITIALIZER;

static int _slurm_jobacct_gather_init(void);

static slurm_jobacct_gather_context_t *
_slurm_jobacct_gather_context_create( const char *jobacct_gather_type)
{
	slurm_jobacct_gather_context_t *c;

	if ( jobacct_gather_type == NULL ) {
		error("_slurm_jobacct_gather_context_create: no jobacct type");
		return NULL;
	}

	c = xmalloc( sizeof( struct slurm_jobacct_gather_context ) );

	c->jobacct_gather_errno = SLURM_SUCCESS;

	/* Copy the job completion job completion type. */
	c->jobacct_gather_type = xstrdup( jobacct_gather_type );
	if ( c->jobacct_gather_type == NULL ) {
		error( "can't make local copy of jobacct type" );
		xfree( c );
		return NULL; 
	}

	/* Plugin rack is demand-loaded on first reference. */
	c->plugin_list = NULL; 
	c->cur_plugin = PLUGIN_INVALID_HANDLE; 
	c->jobacct_gather_errno	= SLURM_SUCCESS;

	return c;
}

static int
_slurm_jobacct_gather_context_destroy( slurm_jobacct_gather_context_t *c )
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

	xfree( c->jobacct_gather_type );
	xfree( c );

	return SLURM_SUCCESS;
}

/*
 * Resolve the operations from the plugin.
 */
static slurm_jobacct_gather_ops_t *
_slurm_jobacct_gather_get_ops( slurm_jobacct_gather_context_t *c )
{
	/*
	 * These strings must be in the same order as the fields declared
	 * for slurm_jobacct_gather_ops_t.
	 */
	static const char *syms[] = {
		"jobacct_gather_p_create",
		"jobacct_gather_p_destroy",
		"jobacct_gather_p_setinfo",
		"jobacct_gather_p_getinfo",
		"jobacct_gather_p_pack",
		"jobacct_gather_p_unpack",	
		"jobacct_gather_p_aggregate",
		"jobacct_gather_p_startpoll",
		"jobacct_gather_p_endpoll",
		"jobacct_gather_p_change_poll",
		"jobacct_gather_p_suspend_poll",
		"jobacct_gather_p_resume_poll",
		"jobacct_gather_p_set_proctrack_container_id",
		"jobacct_gather_p_add_task",
		"jobacct_gather_p_stat_task",
		"jobacct_gather_p_remove_task",
		"jobacct_gather_p_2_sacct"
	};
	int n_syms = sizeof( syms ) / sizeof( char * );
	int rc = 0;
 	
	/* Find the correct plugin. */
        c->cur_plugin = plugin_load_and_link(c->jobacct_gather_type,
					     n_syms, syms,
					     (void **) &c->ops);
        if ( c->cur_plugin != PLUGIN_INVALID_HANDLE ) 
        	return &c->ops;

	error("Couldn't find the specified plugin name for %s "
	      "looking at all files",
	      c->jobacct_gather_type);
	
       /* Get the plugin list, if needed. */
        if ( c->plugin_list == NULL ) {
		char *plugin_dir;
                c->plugin_list = plugrack_create();
                if ( c->plugin_list == NULL ) {
                        error( "Unable to create a plugin manager" );
                        return NULL;
                }

                plugrack_set_major_type( c->plugin_list, "jobacct_gather" );
                plugrack_set_paranoia( c->plugin_list, 
				       PLUGRACK_PARANOIA_NONE, 
				       0 );
		plugin_dir = slurm_get_plugin_dir();
                plugrack_read_dir( c->plugin_list, plugin_dir );
		xfree(plugin_dir);
        }
  
        /* Find the correct plugin. */
        c->cur_plugin = 
		plugrack_use_by_type( c->plugin_list, c->jobacct_gather_type );
        if ( c->cur_plugin == PLUGIN_INVALID_HANDLE ) {
                error( "can't find a plugin for type %s",
		       c->jobacct_gather_type );
                return NULL;
        }  

        /* Dereference the API. */
        if ( (rc = plugin_get_syms( c->cur_plugin,
				    n_syms,
				    syms,
				    (void **) &c->ops )) < n_syms ) {
                error( "incomplete jobacct_gather plugin detected only "
		       "got %d out of %d",
		       rc, n_syms);
                return NULL;
        }

        return &c->ops;

}

static int _slurm_jobacct_gather_init(void)
{
	char	*jobacct_gather_type = NULL;
	int	retval=SLURM_SUCCESS;

	slurm_mutex_lock( &g_jobacct_gather_context_lock );
	if ( g_jobacct_gather_context )
		goto done;

	jobacct_gather_type = slurm_get_jobacct_gather_type();
	g_jobacct_gather_context = _slurm_jobacct_gather_context_create( 
		jobacct_gather_type);
	if ( g_jobacct_gather_context == NULL ) {
		error( "cannot create a context for %s", jobacct_gather_type );
		retval = SLURM_ERROR;
		goto done;
	}
	
	if ( _slurm_jobacct_gather_get_ops( g_jobacct_gather_context )
	     == NULL ) {
		error( "cannot resolve job accounting plugin operations" );
		_slurm_jobacct_gather_context_destroy(
			g_jobacct_gather_context);
		g_jobacct_gather_context = NULL;
		retval = SLURM_ERROR;
	}

  done:
	slurm_mutex_unlock( &g_jobacct_gather_context_lock );
	xfree(jobacct_gather_type);

	return(retval);
}

extern int slurm_jobacct_gather_init(void)
{
	if (_slurm_jobacct_gather_init() < 0)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

extern int slurm_jobacct_gather_fini(void)
{
	int rc;

	if (!g_jobacct_gather_context)
		return SLURM_SUCCESS;

	rc = _slurm_jobacct_gather_context_destroy(g_jobacct_gather_context);
	g_jobacct_gather_context = NULL;
	return rc;
}

extern jobacctinfo_t *jobacct_gather_g_create(jobacct_id_t *jobacct_id)
{
	jobacctinfo_t *jobacct = NULL;

	if (_slurm_jobacct_gather_init() < 0)
		return jobacct;
	
	slurm_mutex_lock( &g_jobacct_gather_context_lock );
	if ( g_jobacct_gather_context )
		jobacct = (*(g_jobacct_gather_context->
			     ops.jobacct_gather_create))(jobacct_id);
	
	slurm_mutex_unlock( &g_jobacct_gather_context_lock );	
	return jobacct;
}

extern void jobacct_gather_g_destroy(jobacctinfo_t *jobacct)
{
	if (_slurm_jobacct_gather_init() < 0)
		return;
	
	slurm_mutex_lock( &g_jobacct_gather_context_lock );
	if ( g_jobacct_gather_context )
		(*(g_jobacct_gather_context->ops.jobacct_gather_destroy))
			(jobacct);
	slurm_mutex_unlock( &g_jobacct_gather_context_lock );	
	return;
}

extern int jobacct_gather_g_setinfo(jobacctinfo_t *jobacct, 
				    enum jobacct_data_type type, void *data)
{
	int retval = SLURM_SUCCESS;

	if (_slurm_jobacct_gather_init() < 0)
		return SLURM_ERROR;
	
	slurm_mutex_lock( &g_jobacct_gather_context_lock );
	if ( g_jobacct_gather_context )
		retval = (*(g_jobacct_gather_context->
			    ops.jobacct_gather_setinfo))(jobacct, type, data);
	slurm_mutex_unlock( &g_jobacct_gather_context_lock );	
	return retval;
}

extern int jobacct_gather_g_getinfo(jobacctinfo_t *jobacct, 
				    enum jobacct_data_type type, void *data)
{
	int retval = SLURM_SUCCESS;

	if (_slurm_jobacct_gather_init() < 0)
		return SLURM_ERROR;
	
	slurm_mutex_lock( &g_jobacct_gather_context_lock );
	if ( g_jobacct_gather_context )
		retval = (*(g_jobacct_gather_context->
			    ops.jobacct_gather_getinfo))(jobacct, type, data);
	slurm_mutex_unlock( &g_jobacct_gather_context_lock );	
	return retval;
}

extern void jobacct_gather_g_pack(jobacctinfo_t *jobacct, Buf buffer)
{
	if (_slurm_jobacct_gather_init() < 0)
		return;
	
	slurm_mutex_lock( &g_jobacct_gather_context_lock );
	if ( g_jobacct_gather_context )
		(*(g_jobacct_gather_context->ops.jobacct_gather_pack))
			(jobacct, buffer);
	slurm_mutex_unlock( &g_jobacct_gather_context_lock );	
	return;
}

extern int jobacct_gather_g_unpack(jobacctinfo_t **jobacct, Buf buffer)
{
	int retval = SLURM_SUCCESS;

	if (_slurm_jobacct_gather_init() < 0)
		return SLURM_ERROR;
	
	slurm_mutex_lock( &g_jobacct_gather_context_lock );
	if ( g_jobacct_gather_context )
		retval = (*(g_jobacct_gather_context->
			    ops.jobacct_gather_unpack))(jobacct, buffer);
	slurm_mutex_unlock( &g_jobacct_gather_context_lock );	
	return retval;
}

extern void jobacct_gather_g_aggregate(jobacctinfo_t *dest,
				       jobacctinfo_t *from)
{
	if (_slurm_jobacct_gather_init() < 0)
		return;
	
	slurm_mutex_lock( &g_jobacct_gather_context_lock );
	if ( g_jobacct_gather_context )
		(*(g_jobacct_gather_context->ops.jobacct_gather_aggregate))
			(dest, from);
	slurm_mutex_unlock( &g_jobacct_gather_context_lock );	
	return;
}

extern int jobacct_gather_g_startpoll(uint16_t frequency)
{
	int retval = SLURM_SUCCESS;
	if (_slurm_jobacct_gather_init() < 0)
		return SLURM_ERROR;

	slurm_mutex_lock( &g_jobacct_gather_context_lock );
	if ( g_jobacct_gather_context ) 
		retval = (*(g_jobacct_gather_context->ops.jobacct_gather_startpoll))
			(frequency);
	
	slurm_mutex_unlock( &g_jobacct_gather_context_lock );
	return retval;
}

extern int jobacct_gather_g_endpoll()
{
	int retval = SLURM_SUCCESS;
	if (_slurm_jobacct_gather_init() < 0)
		return SLURM_ERROR;
	
	slurm_mutex_lock( &g_jobacct_gather_context_lock );
	if ( g_jobacct_gather_context )
		retval = (*(g_jobacct_gather_context->ops.jobacct_gather_endpoll))();
	slurm_mutex_unlock( &g_jobacct_gather_context_lock );	
	return retval;
}

extern void jobacct_gather_g_change_poll(uint16_t frequency)
{
	if (_slurm_jobacct_gather_init() < 0)
		return;

	slurm_mutex_lock( &g_jobacct_gather_context_lock );
	if ( g_jobacct_gather_context ) 
		(*(g_jobacct_gather_context->ops.jobacct_gather_change_poll))
			(frequency);
	
	slurm_mutex_unlock( &g_jobacct_gather_context_lock );
}

extern void jobacct_gather_g_suspend_poll()
{
	if (_slurm_jobacct_gather_init() < 0)
		return;
	
	slurm_mutex_lock( &g_jobacct_gather_context_lock );
	if ( g_jobacct_gather_context )
		(*(g_jobacct_gather_context->ops.jobacct_gather_suspend_poll))();
	slurm_mutex_unlock( &g_jobacct_gather_context_lock );	
	return;
}

extern void jobacct_gather_g_resume_poll()
{
	if (_slurm_jobacct_gather_init() < 0)
		return;

	slurm_mutex_lock( &g_jobacct_gather_context_lock );
	if ( g_jobacct_gather_context )
		(*(g_jobacct_gather_context->ops.jobacct_gather_resume_poll))();
	slurm_mutex_unlock( &g_jobacct_gather_context_lock );
	return;
}

extern int jobacct_gather_g_set_proctrack_container_id(uint32_t id)
{
	int retval = SLURM_SUCCESS;
	if (_slurm_jobacct_gather_init() < 0)
		return SLURM_ERROR;
	
	slurm_mutex_lock( &g_jobacct_gather_context_lock );
	if ( g_jobacct_gather_context )
		retval = (*(g_jobacct_gather_context->ops.
			    jobacct_gather_set_proctrack_container_id))(id);
	slurm_mutex_unlock( &g_jobacct_gather_context_lock );	
	return retval;
	
}

extern int jobacct_gather_g_add_task(pid_t pid, jobacct_id_t *jobacct_id)
{
	int retval = SLURM_SUCCESS;
	if (_slurm_jobacct_gather_init() < 0)
		return SLURM_ERROR;
	
	slurm_mutex_lock( &g_jobacct_gather_context_lock );
	if ( g_jobacct_gather_context )
		retval = (*(g_jobacct_gather_context->
			    ops.jobacct_gather_add_task))(pid, jobacct_id);
	slurm_mutex_unlock( &g_jobacct_gather_context_lock );	
	return retval;
}

extern jobacctinfo_t *jobacct_gather_g_stat_task(pid_t pid)
{
	jobacctinfo_t *jobacct = NULL;
	if (_slurm_jobacct_gather_init() < 0)
		return jobacct;
	
	slurm_mutex_lock( &g_jobacct_gather_context_lock );
	if ( g_jobacct_gather_context )
		jobacct = (*(g_jobacct_gather_context->
			     ops.jobacct_gather_stat_task))(pid);
	slurm_mutex_unlock( &g_jobacct_gather_context_lock );	
	return jobacct;
}

extern jobacctinfo_t *jobacct_gather_g_remove_task(pid_t pid)
{
	jobacctinfo_t *jobacct = NULL;
	if (_slurm_jobacct_gather_init() < 0)
		return jobacct;
	
	slurm_mutex_lock( &g_jobacct_gather_context_lock );
	if ( g_jobacct_gather_context )
		jobacct = (*(g_jobacct_gather_context->
			     ops.jobacct_gather_remove_task))(pid);
	slurm_mutex_unlock( &g_jobacct_gather_context_lock );	
	return jobacct;
}

extern void jobacct_gather_g_2_sacct(sacct_t *sacct, jobacctinfo_t *jobacct)
{
	if (_slurm_jobacct_gather_init() < 0)
		return;
	
	slurm_mutex_lock( &g_jobacct_gather_context_lock );
	if ( g_jobacct_gather_context )
		(*(g_jobacct_gather_context->ops.jobacct_gather_2_sacct))
			(sacct, jobacct);
	slurm_mutex_unlock( &g_jobacct_gather_context_lock );	
	return;
}
