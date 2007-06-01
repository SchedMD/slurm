/*****************************************************************************\
 *  slurm_jobacct.c - implementation-independent job accounting logging 
 *  functions
 *****************************************************************************
 *  Copyright (C) 2003 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jay Windley <jwindley@lnxi.com>, Morris Jette <jette1@llnl.com>
 *  UCRL-CODE-226842.
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
#include "src/common/slurm_jobacct.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"


/*
 * The following global is used by the jobacct/log plugin; it must
 * persist when the plugin is reloaded, so we define it here.
 */
extern FILE * JOBACCT_LOGFILE;

/*
 * WARNING:  Do not change the order of these fields or add additional
 * fields at the beginning of the structure.  If you do, job accounting
 * plugins will stop working.  If you need to add fields, add them 
 * at the end of the structure.
 */
typedef struct slurm_jobacct_ops {
	int (*jobacct_init_struct)    (jobacctinfo_t *jobacct, 
				       jobacct_id_t *jobacct_id);
	jobacctinfo_t *(*jobacct_alloc)(jobacct_id_t *jobacct_id);
	void (*jobacct_free)          (jobacctinfo_t *jobacct);
	int (*jobacct_setinfo)        (jobacctinfo_t *jobacct, 
				       enum jobacct_data_type type, 
				       void *data);
	int (*jobacct_getinfo)        (jobacctinfo_t *jobacct, 
				       enum jobacct_data_type type, 
				       void *data);
	void (*jobacct_aggregate)     (jobacctinfo_t *dest, 
				       jobacctinfo_t *from);
	void (*jobacct_2_sacct)       (sacct_t *sacct, 
				       jobacctinfo_t *jobacct);
	void (*jobacct_pack)          (jobacctinfo_t *jobacct, Buf buffer);
	int (*jobacct_unpack)         (jobacctinfo_t **jobacct, Buf buffer);
	int (*jobacct_init)	      (char *job_acct_log);
	int (*jobacct_fini)	      ();
	int (*jobacct_job_start)      (struct job_record *job_ptr);
	int (*jobacct_job_complete)   (struct job_record *job_ptr); 
	int (*jobacct_step_start)     (struct step_record *step);
	int (*jobacct_step_complete)  (struct step_record *step);
	int (*jobacct_suspend)        (struct job_record *job_ptr);
	int (*jobacct_startpoll)      (int frequency);
	int (*jobacct_endpoll)	      ();
	int (*jobacct_set_proctrack_container_id)(uint32_t id);
	int (*jobacct_add_task)       (pid_t pid, jobacct_id_t *jobacct_id);
	jobacctinfo_t *(*jobacct_stat_task)(pid_t pid);
	jobacctinfo_t *(*jobacct_remove_task)(pid_t pid);
	void (*jobacct_suspend_poll)  ();
	void (*jobacct_resume_poll)   ();
} slurm_jobacct_ops_t;

/*
 * A global job accounting context.  "Global" in the sense that there's
 * only one, with static bindings.  We don't export it.
 */

typedef struct slurm_jobacct_context {
	char 			*jobacct_type;
	plugrack_t		plugin_list;
	plugin_handle_t		cur_plugin;
	int			jobacct_errno;
	slurm_jobacct_ops_t	ops;
} slurm_jobacct_context_t;

static slurm_jobacct_context_t *g_jobacct_context = NULL;
static pthread_mutex_t      g_jobacct_context_lock = PTHREAD_MUTEX_INITIALIZER;

static int _slurm_jobacct_init(void);
static int _slurm_jobacct_fini(void);

static slurm_jobacct_context_t *
_slurm_jobacct_context_create( const char *jobacct_type)
{
	slurm_jobacct_context_t *c;

	if ( jobacct_type == NULL ) {
		error( "_slurm_jobacct_context_create: no jobacct type" );
		return NULL;
	}

	c = xmalloc( sizeof( struct slurm_jobacct_context ) );

	c->jobacct_errno = SLURM_SUCCESS;

	/* Copy the job completion job completion type. */
	c->jobacct_type = xstrdup( jobacct_type );
	if ( c->jobacct_type == NULL ) {
		error( "can't make local copy of jobacct type" );
		xfree( c );
		return NULL; 
	}

	/* Plugin rack is demand-loaded on first reference. */
	c->plugin_list = NULL; 
	c->cur_plugin = PLUGIN_INVALID_HANDLE; 
	c->jobacct_errno	= SLURM_SUCCESS;

	return c;
}

static int
_slurm_jobacct_context_destroy( slurm_jobacct_context_t *c )
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

	xfree( c->jobacct_type );
	xfree( c );

	return SLURM_SUCCESS;
}

/*
 * Resolve the operations from the plugin.
 */
static slurm_jobacct_ops_t *
_slurm_jobacct_get_ops( slurm_jobacct_context_t *c )
{
	/*
	 * These strings must be in the same order as the fields declared
	 * for slurm_jobacct_ops_t.
	 */
	static const char *syms[] = {
		"jobacct_p_init_struct",
		"jobacct_p_alloc",
		"jobacct_p_free",
		"jobacct_p_setinfo",
		"jobacct_p_getinfo",
		"jobacct_p_aggregate",
		"jobacct_p_2_sacct",
		"jobacct_p_pack",
		"jobacct_p_unpack",	
		"jobacct_p_init_slurmctld",
		"jobacct_p_fini_slurmctld",
		"jobacct_p_job_start_slurmctld",
		"jobacct_p_job_complete_slurmctld",
		"jobacct_p_step_start_slurmctld",
		"jobacct_p_step_complete_slurmctld",
		"jobacct_p_suspend_slurmctld",
		"jobacct_p_startpoll",
		"jobacct_p_endpoll",
		"jobacct_p_set_proctrack_container_id",
		"jobacct_p_add_task",
		"jobacct_p_stat_task",
		"jobacct_p_remove_task",
		"jobacct_p_suspend_poll",
		"jobacct_p_resume_poll"
	};
	int n_syms = sizeof( syms ) / sizeof( char * );
	int rc = 0;
        /* Get the plugin list, if needed. */
        if ( c->plugin_list == NULL ) {
		char *plugin_dir;
                c->plugin_list = plugrack_create();
                if ( c->plugin_list == NULL ) {
                        error( "Unable to create a plugin manager" );
                        return NULL;
                }

                plugrack_set_major_type( c->plugin_list, "jobacct" );
                plugrack_set_paranoia( c->plugin_list, 
				       PLUGRACK_PARANOIA_NONE, 
				       0 );
		plugin_dir = slurm_get_plugin_dir();
                plugrack_read_dir( c->plugin_list, plugin_dir );
		xfree(plugin_dir);
        }
  
        /* Find the correct plugin. */
        c->cur_plugin = 
		plugrack_use_by_type( c->plugin_list, c->jobacct_type );
        if ( c->cur_plugin == PLUGIN_INVALID_HANDLE ) {
                error( "can't find a plugin for type %s", c->jobacct_type );
                return NULL;
        }  

        /* Dereference the API. */
        if ( (rc = plugin_get_syms( c->cur_plugin,
				    n_syms,
				    syms,
				    (void **) &c->ops )) < n_syms ) {
                error( "incomplete jobacct plugin detected only "
		       "got %d out of %d",
		       rc, n_syms);
                return NULL;
        }

        return &c->ops;
}

static int _slurm_jobacct_init(void)
{
	char	*jobacct_type = NULL;
	int	retval=SLURM_SUCCESS;

	slurm_mutex_lock( &g_jobacct_context_lock );
	if ( g_jobacct_context )
		goto done;

	jobacct_type = slurm_get_jobacct_type();
	g_jobacct_context = _slurm_jobacct_context_create( jobacct_type );
	if ( g_jobacct_context == NULL ) {
		error( "cannot create a context for %s", jobacct_type );
		retval = SLURM_ERROR;
		goto done;
	}
	
	if ( _slurm_jobacct_get_ops( g_jobacct_context ) == NULL ) {
		error( "cannot resolve job accounting plugin operations" );
		_slurm_jobacct_context_destroy( g_jobacct_context );
		g_jobacct_context = NULL;
		retval = SLURM_ERROR;
	}

  done:
	slurm_mutex_unlock( &g_jobacct_context_lock );
	xfree(jobacct_type);

	return(retval);
}

static int _slurm_jobacct_fini(void)
{
	int rc;

	if (!g_jobacct_context)
		return SLURM_SUCCESS;

	rc = _slurm_jobacct_context_destroy(g_jobacct_context);
	g_jobacct_context = NULL;
	return rc;
}

extern int jobacct_init(void)
{
	if (_slurm_jobacct_init() < 0)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

extern int jobacct_g_init_struct(jobacctinfo_t *jobacct, 
				 jobacct_id_t *jobacct_id)
{
	int retval = SLURM_SUCCESS;
	
	if (_slurm_jobacct_init() < 0)
		return SLURM_ERROR;
	
	slurm_mutex_lock( &g_jobacct_context_lock );
	if ( g_jobacct_context )
		retval = (*(g_jobacct_context->ops.jobacct_init_struct))
			(jobacct, jobacct_id);
	slurm_mutex_unlock( &g_jobacct_context_lock );	
	return retval;
}

extern jobacctinfo_t *jobacct_g_alloc(jobacct_id_t *jobacct_id)
{
	jobacctinfo_t *jobacct = NULL;

	if (_slurm_jobacct_init() < 0)
		return jobacct;
	
	slurm_mutex_lock( &g_jobacct_context_lock );
	if ( g_jobacct_context )
		jobacct = (*(g_jobacct_context->ops.jobacct_alloc))
			(jobacct_id);
	
	slurm_mutex_unlock( &g_jobacct_context_lock );	
	return jobacct;
}

extern void jobacct_g_free(jobacctinfo_t *jobacct)
{
	if (_slurm_jobacct_init() < 0)
		return;
	
	slurm_mutex_lock( &g_jobacct_context_lock );
	if ( g_jobacct_context )
		(*(g_jobacct_context->ops.jobacct_free))(jobacct);
	slurm_mutex_unlock( &g_jobacct_context_lock );	
	return;
}

extern int jobacct_g_setinfo(jobacctinfo_t *jobacct, 
			     enum jobacct_data_type type, void *data)
{
	int retval = SLURM_SUCCESS;

	if (_slurm_jobacct_init() < 0)
		return SLURM_ERROR;
	
	slurm_mutex_lock( &g_jobacct_context_lock );
	if ( g_jobacct_context )
		retval = (*(g_jobacct_context->ops.jobacct_setinfo))
			(jobacct, type, data);
	slurm_mutex_unlock( &g_jobacct_context_lock );	
	return retval;
}

extern int jobacct_g_getinfo(jobacctinfo_t *jobacct, 
			     enum jobacct_data_type type, void *data)
{
	int retval = SLURM_SUCCESS;

	if (_slurm_jobacct_init() < 0)
		return SLURM_ERROR;
	
	slurm_mutex_lock( &g_jobacct_context_lock );
	if ( g_jobacct_context )
		retval = (*(g_jobacct_context->ops.jobacct_getinfo))
			(jobacct, type, data);
	slurm_mutex_unlock( &g_jobacct_context_lock );	
	return retval;
}

extern void jobacct_g_aggregate(jobacctinfo_t *dest, jobacctinfo_t *from)
{
	if (_slurm_jobacct_init() < 0)
		return;
	
	slurm_mutex_lock( &g_jobacct_context_lock );
	if ( g_jobacct_context )
		(*(g_jobacct_context->ops.jobacct_aggregate))(dest, from);
	slurm_mutex_unlock( &g_jobacct_context_lock );	
	return;
}

extern void jobacct_g_2_sacct(sacct_t *sacct, jobacctinfo_t *jobacct)
{
	if (_slurm_jobacct_init() < 0)
		return;
	
	slurm_mutex_lock( &g_jobacct_context_lock );
	if ( g_jobacct_context )
		(*(g_jobacct_context->ops.jobacct_2_sacct))(sacct, jobacct);
	slurm_mutex_unlock( &g_jobacct_context_lock );	
	return;
}

extern void jobacct_g_pack(jobacctinfo_t *jobacct, Buf buffer)
{
	if (_slurm_jobacct_init() < 0)
		return;
	
	slurm_mutex_lock( &g_jobacct_context_lock );
	if ( g_jobacct_context )
		(*(g_jobacct_context->ops.jobacct_pack))(jobacct, buffer);
	slurm_mutex_unlock( &g_jobacct_context_lock );	
	return;
}

extern int jobacct_g_unpack(jobacctinfo_t **jobacct, Buf buffer)
{
	int retval = SLURM_SUCCESS;

	if (_slurm_jobacct_init() < 0)
		return SLURM_ERROR;
	
	slurm_mutex_lock( &g_jobacct_context_lock );
	if ( g_jobacct_context )
		retval = (*(g_jobacct_context->ops.jobacct_unpack))
			(jobacct, buffer);
	slurm_mutex_unlock( &g_jobacct_context_lock );	
	return retval;
}

extern int jobacct_g_init_slurmctld(char *job_acct_log)
{
	int retval = SLURM_SUCCESS;
	if (_slurm_jobacct_init() < 0)
		return SLURM_ERROR;
	
	slurm_mutex_lock( &g_jobacct_context_lock );
	if ( g_jobacct_context ) 
		retval = (*(g_jobacct_context->ops.jobacct_init))
			(job_acct_log);
	slurm_mutex_unlock( &g_jobacct_context_lock );
	return retval;
}

extern int jobacct_g_fini_slurmctld()
{
	int retval = SLURM_SUCCESS;
	if (_slurm_jobacct_init() < 0)
		return SLURM_ERROR;
	
	slurm_mutex_lock( &g_jobacct_context_lock );
	if ( g_jobacct_context ) 
		retval = (*(g_jobacct_context->ops.jobacct_fini))();
	slurm_mutex_unlock( &g_jobacct_context_lock );
	
	if (_slurm_jobacct_fini() < 0)
		return SLURM_ERROR;
	return retval;
}

extern int jobacct_g_job_start_slurmctld(struct job_record *job_ptr)
{
	int retval = SLURM_SUCCESS;
	if (_slurm_jobacct_init() < 0)
		return SLURM_ERROR;
	
	slurm_mutex_lock( &g_jobacct_context_lock );
	if ( g_jobacct_context )
		retval = (*(g_jobacct_context->ops.jobacct_job_start))
			(job_ptr);
	slurm_mutex_unlock( &g_jobacct_context_lock );	
	return retval;
}

extern int jobacct_g_job_complete_slurmctld(struct job_record *job_ptr)
{
	int retval = SLURM_SUCCESS;
	if (_slurm_jobacct_init() < 0)
		return SLURM_ERROR;
	
	slurm_mutex_lock( &g_jobacct_context_lock );
	if ( g_jobacct_context )
		retval = (*(g_jobacct_context->ops.jobacct_job_complete))
			(job_ptr);
	slurm_mutex_unlock( &g_jobacct_context_lock );	
	return retval;
}

extern int jobacct_g_step_start_slurmctld(struct step_record *step_ptr)
{
	int retval = SLURM_SUCCESS;
	if (_slurm_jobacct_init() < 0)
		return SLURM_ERROR;
	
	slurm_mutex_lock( &g_jobacct_context_lock );
	if ( g_jobacct_context )
		retval = (*(g_jobacct_context->ops.jobacct_step_start))
			(step_ptr);
	slurm_mutex_unlock( &g_jobacct_context_lock );	
	return retval;
}

extern int jobacct_g_step_complete_slurmctld(struct step_record *step_ptr)
{
	int retval = SLURM_SUCCESS;
	if (_slurm_jobacct_init() < 0)
		return SLURM_ERROR;
	
	slurm_mutex_lock( &g_jobacct_context_lock );
	if ( g_jobacct_context )
		retval = (*(g_jobacct_context->ops.jobacct_step_complete))
			(step_ptr);
	slurm_mutex_unlock( &g_jobacct_context_lock );	
	return retval;
}

extern int jobacct_g_suspend_slurmctld(struct job_record *job_ptr)
{
	int retval = SLURM_SUCCESS;
	if (_slurm_jobacct_init() < 0)
		return SLURM_ERROR;
	
	slurm_mutex_lock( &g_jobacct_context_lock );
	if ( g_jobacct_context )
		retval = (*(g_jobacct_context->ops.jobacct_suspend))
			(job_ptr);
	slurm_mutex_unlock( &g_jobacct_context_lock );	
	return retval;
}

extern int jobacct_g_startpoll(int frequency)
{
	int retval = SLURM_SUCCESS;
	if (_slurm_jobacct_init() < 0)
		return SLURM_ERROR;
	
	if (_slurm_jobacct_init() < 0)
		return SLURM_ERROR;

	slurm_mutex_lock( &g_jobacct_context_lock );
	if ( g_jobacct_context ) 
		retval = (*(g_jobacct_context->ops.jobacct_startpoll))
			(frequency);
	
	slurm_mutex_unlock( &g_jobacct_context_lock );
	return retval;
}

extern int jobacct_g_endpoll()
{
	int retval = SLURM_SUCCESS;
	if (_slurm_jobacct_init() < 0)
		return SLURM_ERROR;
	
	slurm_mutex_lock( &g_jobacct_context_lock );
	if ( g_jobacct_context )
		retval = (*(g_jobacct_context->ops.jobacct_endpoll))();
	slurm_mutex_unlock( &g_jobacct_context_lock );	
	return retval;
}

extern int jobacct_g_set_proctrack_container_id(uint32_t id)
{
	int retval = SLURM_SUCCESS;
	if (_slurm_jobacct_init() < 0)
		return SLURM_ERROR;
	
	slurm_mutex_lock( &g_jobacct_context_lock );
	if ( g_jobacct_context )
		retval = (*(g_jobacct_context->ops.
			    jobacct_set_proctrack_container_id))(id);
	slurm_mutex_unlock( &g_jobacct_context_lock );	
	return retval;
	
}

extern int jobacct_g_add_task(pid_t pid, jobacct_id_t *jobacct_id)
{
	int retval = SLURM_SUCCESS;
	if (_slurm_jobacct_init() < 0)
		return SLURM_ERROR;
	
	slurm_mutex_lock( &g_jobacct_context_lock );
	if ( g_jobacct_context )
		retval = (*(g_jobacct_context->ops.jobacct_add_task))
			(pid, jobacct_id);
	slurm_mutex_unlock( &g_jobacct_context_lock );	
	return retval;
}

extern jobacctinfo_t *jobacct_g_stat_task(pid_t pid)
{
	jobacctinfo_t *jobacct = NULL;
	if (_slurm_jobacct_init() < 0)
		return jobacct;
	
	slurm_mutex_lock( &g_jobacct_context_lock );
	if ( g_jobacct_context )
		jobacct = (*(g_jobacct_context->ops.jobacct_stat_task))(pid);
	slurm_mutex_unlock( &g_jobacct_context_lock );	
	return jobacct;
}

extern jobacctinfo_t *jobacct_g_remove_task(pid_t pid)
{
	jobacctinfo_t *jobacct = NULL;
	if (_slurm_jobacct_init() < 0)
		return jobacct;
	
	slurm_mutex_lock( &g_jobacct_context_lock );
	if ( g_jobacct_context )
		jobacct = (*(g_jobacct_context->ops.jobacct_remove_task))(pid);
	slurm_mutex_unlock( &g_jobacct_context_lock );	
	return jobacct;
}

extern void jobacct_g_suspend_poll()
{
	if (_slurm_jobacct_init() < 0)
		return;
	
	slurm_mutex_lock( &g_jobacct_context_lock );
	if ( g_jobacct_context )
		(*(g_jobacct_context->ops.jobacct_suspend_poll))();
	slurm_mutex_unlock( &g_jobacct_context_lock );	
	return;
}

extern void jobacct_g_resume_poll()
{
	if (_slurm_jobacct_init() < 0)
		return;

	slurm_mutex_lock( &g_jobacct_context_lock );
	if ( g_jobacct_context )
		(*(g_jobacct_context->ops.jobacct_resume_poll))();
	slurm_mutex_unlock( &g_jobacct_context_lock );
	return;
}

