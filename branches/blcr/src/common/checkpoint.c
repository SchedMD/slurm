/*****************************************************************************\
 *  checkpoint.c - implementation-independent checkpoint functions
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.com>
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
#include <stdlib.h>
#include <string.h>

#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/checkpoint.h"
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
typedef struct slurm_checkpoint_ops {
	int     (*ckpt_op) (uint32_t job_id, uint32_t step_id, 
			    struct step_record *step_ptr, uint16_t op,
			    uint16_t data, char *image_dir, time_t *event_time,
			    uint32_t *error_code, char **error_msg);
	int	(*ckpt_comp) (struct step_record * step_ptr, time_t event_time,
			      uint32_t error_code, char *error_msg);
	int	(*ckpt_task_comp) (struct step_record * step_ptr, uint32_t task_id,
				   time_t event_time, uint32_t error_code, char *error_msg);

	int	(*ckpt_alloc_jobinfo) (check_jobinfo_t *jobinfo);
	int	(*ckpt_free_jobinfo) (check_jobinfo_t jobinfo);
	int	(*ckpt_pack_jobinfo) (check_jobinfo_t jobinfo, Buf buffer);
	int	(*ckpt_unpack_jobinfo) (check_jobinfo_t jobinfo, Buf buffer);
	int     (*ckpt_stepd_prefork) (void *slurmd_job);
	int     (*ckpt_signal_tasks) (void *slurmd_job, char *image_dir);
	int     (*ckpt_restart_task) (void *slurmd_job, char *image_dir, int gtid);
} slurm_checkpoint_ops_t;

/*
 * A global job completion context.  "Global" in the sense that there's
 * only one, with static bindings.  We don't export it.
 */

struct slurm_checkpoint_context {
	char *			checkpoint_type;
	plugrack_t		plugin_list;
	plugin_handle_t		cur_plugin;
	int			checkpoint_errno;
	slurm_checkpoint_ops_t	ops;
};

static slurm_checkpoint_context_t g_context = NULL;
static pthread_mutex_t      context_lock = PTHREAD_MUTEX_INITIALIZER;

static slurm_checkpoint_context_t
_slurm_checkpoint_context_create( const char *checkpoint_type )
{
	slurm_checkpoint_context_t c;

	if ( checkpoint_type == NULL ) {
		debug3( "_slurm_checkpoint_context_create: no checkpoint type");
		return NULL;
	}

	c = xmalloc( sizeof( struct slurm_checkpoint_context ) );

	c->checkpoint_errno = SLURM_SUCCESS;

	/* Copy the job completion job completion type. */
	c->checkpoint_type = xstrdup( checkpoint_type );
	if ( c->checkpoint_type == NULL ) {
		debug3( "can't make local copy of checkpoint type" );
		xfree( c );
		return NULL; 
	}

	/* Plugin rack is demand-loaded on first reference. */
	c->plugin_list = NULL; 
	c->cur_plugin = PLUGIN_INVALID_HANDLE; 

	return c;
}

static int
_slurm_checkpoint_context_destroy( slurm_checkpoint_context_t c )
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

	xfree( c->checkpoint_type );
	xfree( c );

	return SLURM_SUCCESS;
}

/*
 * Resolve the operations from the plugin.
 */
static slurm_checkpoint_ops_t *
_slurm_checkpoint_get_ops( slurm_checkpoint_context_t c )
{
        /*
         * These strings must be kept in the same order as the fields
         * declared for slurm_checkpoint_ops_t.
         */
	static const char *syms[] = {
		"slurm_ckpt_op",
		"slurm_ckpt_comp",
		"slurm_ckpt_task_comp",
		"slurm_ckpt_alloc_job",
		"slurm_ckpt_free_job",
		"slurm_ckpt_pack_job",
		"slurm_ckpt_unpack_job",
		"slurm_ckpt_stepd_prefork",
		"slurm_ckpt_signal_tasks",
		"slurm_ckpt_restart_task"
	};
        int n_syms = sizeof( syms ) / sizeof( char * );

	/* Find the correct plugin. */
        c->cur_plugin = plugin_load_and_link(c->checkpoint_type, n_syms, syms,
					     (void **) &c->ops);
        if ( c->cur_plugin != PLUGIN_INVALID_HANDLE ) 
        	return &c->ops;

	error("Couldn't find the specified plugin name for %s "
	      "looking at all files",
	      c->checkpoint_type);
	
        /* Get the plugin list, if needed. */
        if ( c->plugin_list == NULL ) {
		char *plugin_dir;
                c->plugin_list = plugrack_create();
                if ( c->plugin_list == NULL ) {
                        error( "Unable to create a plugin manager" );
                        return NULL;
                }

                plugrack_set_major_type( c->plugin_list, "checkpoint" );
                plugrack_set_paranoia( c->plugin_list, 
				       PLUGRACK_PARANOIA_NONE, 
				       0 );
		plugin_dir = slurm_get_plugin_dir();
                plugrack_read_dir( c->plugin_list, plugin_dir );
		xfree(plugin_dir);
        }
  
        /* Find the correct plugin. */
        c->cur_plugin = 
		plugrack_use_by_type( c->plugin_list, c->checkpoint_type );
        if ( c->cur_plugin == PLUGIN_INVALID_HANDLE ) {
                error( "can't find a plugin for type %s", c->checkpoint_type );
                return NULL;
        }  

        /* Dereference the API. */
        if ( plugin_get_syms( c->cur_plugin,
                              n_syms,
                              syms,
                              (void **) &c->ops ) < n_syms ) {
                error( "incomplete checkpoint plugin detected" );
                return NULL;
        }

        return &c->ops;
}

/* initialize checkpoint plugin */
extern int
checkpoint_init(char *checkpoint_type)
{
	int retval = SLURM_SUCCESS;

	slurm_mutex_lock( &context_lock );

	if ( g_context )
		_slurm_checkpoint_context_destroy(g_context);
	g_context = _slurm_checkpoint_context_create( checkpoint_type );
	if ( g_context == NULL ) {
		error( "cannot create a context for %s", checkpoint_type );
		xfree(checkpoint_type);
		retval = SLURM_ERROR;
		goto done;
	}

	if ( _slurm_checkpoint_get_ops( g_context ) == NULL ) {
		error( "cannot resolve checkpoint plugin operations" );
		_slurm_checkpoint_context_destroy( g_context );
		g_context = NULL;
		retval = SLURM_ERROR;
	}
	verbose("Checkpoint plugin loaded: %s", checkpoint_type);

  done:
	slurm_mutex_unlock( &context_lock );
	return retval;
}

/* shutdown checkpoint plugin */
extern int
checkpoint_fini(void)
{
	int rc;

	if ( !g_context )
		return SLURM_SUCCESS;

	slurm_mutex_lock( &context_lock );
	rc =_slurm_checkpoint_context_destroy(g_context);
	slurm_mutex_unlock( &context_lock );
	return rc;
}


/* perform some checkpoint operation */
extern int
checkpoint_op(uint32_t job_id, uint32_t step_id, 
	      void *step_ptr, uint16_t op,
	      uint16_t data, char *image_dir, time_t *event_time,
	      uint32_t *error_code, char **error_msg)
{
	int retval = SLURM_SUCCESS;

	slurm_mutex_lock( &context_lock );
	if ( g_context ) {
		retval = (*(g_context->ops.ckpt_op))(job_id, step_id, 
					(struct step_record *) step_ptr,
					op, data, image_dir, 
					event_time, error_code, error_msg);
	} else {
		error ("slurm_checkpoint plugin context not initialized");
		retval = ENOENT;
	}
	slurm_mutex_unlock( &context_lock );
	return retval;
}

extern int
checkpoint_comp(void * step_ptr, time_t event_time, uint32_t error_code,
		char *error_msg)
{
	int retval = SLURM_SUCCESS;

	slurm_mutex_lock( &context_lock );
	if ( g_context )
		retval = (*(g_context->ops.ckpt_comp))(
			(struct step_record *) step_ptr,
			event_time, error_code, error_msg);
	else {
		error ("slurm_checkpoint plugin context not initialized");
		retval = ENOENT;
	}
	slurm_mutex_unlock( &context_lock );
	return retval;
}

extern int
checkpoint_task_comp(void * step_ptr, uint32_t task_id, time_t event_time,
		     uint32_t error_code, char *error_msg)
{
	int retval = SLURM_SUCCESS;

	slurm_mutex_lock( &context_lock );
	if ( g_context )
		retval = (*(g_context->ops.ckpt_task_comp))(
			(struct step_record *) step_ptr, task_id, 
			event_time, error_code, error_msg);
	else {
		error ("slurm_checkpoint plugin context not initialized");
		retval = ENOENT;
	}
	slurm_mutex_unlock( &context_lock );
	return retval;
}

/* allocate and initialize a job step's checkpoint context */
extern int checkpoint_alloc_jobinfo(check_jobinfo_t *jobinfo)
{
	int retval = SLURM_SUCCESS;

	slurm_mutex_lock( &context_lock );
	if ( g_context )
		retval = (*(g_context->ops.ckpt_alloc_jobinfo))(
				jobinfo);
	else {
		error ("slurm_checkpoint plugin context not initialized");
		retval = ENOENT;
	}
	slurm_mutex_unlock( &context_lock );
	return retval;
}

/* free storage for a job step's checkpoint context */
extern int checkpoint_free_jobinfo(check_jobinfo_t jobinfo)
{
	int retval = SLURM_SUCCESS;

	slurm_mutex_lock( &context_lock );
	if ( g_context )
		retval = (*(g_context->ops.ckpt_free_jobinfo))(
				jobinfo);
	else {
		error ("slurm_checkpoint plugin context not initialized");
		retval = ENOENT;
	}
	slurm_mutex_unlock( &context_lock );
	return retval;
}

/* un/pack a job step's checkpoint context */
extern int  checkpoint_pack_jobinfo  (check_jobinfo_t jobinfo, Buf buffer)
{
	int retval = SLURM_SUCCESS;

	slurm_mutex_lock( &context_lock );
	if ( g_context )
		retval = (*(g_context->ops.ckpt_pack_jobinfo))(
				jobinfo, buffer);
	else {
		error ("slurm_checkpoint plugin context not initialized");
		retval = ENOENT;
	}
	slurm_mutex_unlock( &context_lock );
	return retval;
}

extern int  checkpoint_unpack_jobinfo  (check_jobinfo_t jobinfo, Buf buffer)
{
	int retval = SLURM_SUCCESS;

	slurm_mutex_lock( &context_lock );
	if ( g_context )
		retval = (*(g_context->ops.ckpt_unpack_jobinfo))(
				jobinfo, buffer);
	else {
		error ("slurm_checkpoint plugin context not initialized");
		retval = ENOENT;
	}
	slurm_mutex_unlock( &context_lock );
	return retval;
}

extern int checkpoint_stepd_prefork (void *job)
{
        int retval = SLURM_SUCCESS;

        slurm_mutex_lock( &context_lock );
        if ( g_context )
                retval = (*(g_context->ops.ckpt_stepd_prefork))(job);
        else {
                error ("slurm_checkpoint plugin context not initialized");
                retval = ENOENT;
        }
        slurm_mutex_unlock( &context_lock );
        return retval;
}

extern int checkpoint_signal_tasks (void *job, char *image_dir)
{
        int retval = SLURM_SUCCESS;

        slurm_mutex_lock( &context_lock );
        if ( g_context )
                retval = (*(g_context->ops.ckpt_signal_tasks))(job, image_dir);
        else {
                error ("slurm_checkpoint plugin context not initialized");
                retval = ENOENT;
        }
        slurm_mutex_unlock( &context_lock );
        return retval;
}


extern int checkpoint_restart_task (void *job, char *image_dir, int gtid)
{
        int retval = SLURM_SUCCESS;

        slurm_mutex_lock( &context_lock );
        if ( g_context ) {
                retval = (*(g_context->ops.ckpt_restart_task))(job, image_dir, 
							       gtid);
        } else {
                error ("slurm_checkpoint plugin context not initialized");
                retval = ENOENT;
        }
        slurm_mutex_unlock( &context_lock );
        return retval;
}

extern int checkpoint_tasks (uint32_t job_id, uint32_t step_id, 
			     time_t begin_time, char *image_dir, 
			     uint16_t wait, char *nodelist)
{
	int rc = SLURM_SUCCESS, temp_rc;
	checkpoint_tasks_msg_t ckpt_req;
	slurm_msg_t req_msg;
	List ret_list;
        ret_data_info_t *ret_data_info = NULL;

	slurm_msg_t_init(&req_msg);
	ckpt_req.job_id   = job_id;
	ckpt_req.job_step_id  = step_id;
	ckpt_req.timestamp = begin_time,
	ckpt_req.image_dir = image_dir;
	req_msg.msg_type = REQUEST_CHECKPOINT_TASKS;
	req_msg.data     = &ckpt_req;

	if ((ret_list = slurm_send_recv_msgs(nodelist, &req_msg, (wait*1000),
					     false))) {
		while((ret_data_info = list_pop(ret_list))) {
                        temp_rc = slurm_get_return_code(ret_data_info->type,
                                                        ret_data_info->data);
                        if(temp_rc)
                                rc = temp_rc;
                }
	} else {
                error("slurm_checkpoint_tasks: no list was returned");
                rc = SLURM_ERROR;
	}
	slurm_seterrno(rc);
	return rc;
}
