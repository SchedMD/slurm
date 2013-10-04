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
 *  For details, see <http://slurm.schedmd.com/>.
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

/*
 * Must be synchronized with slurm_sched_ops_t above.
 */
static const char *syms[] = {
	"slurm_sched_p_schedule",
	"slurm_sched_p_newalloc",
	"slurm_sched_p_freealloc",
	"slurm_sched_p_initial_priority",
	"slurm_sched_p_job_is_pending",
	"slurm_sched_p_reconfig",
	"slurm_sched_p_partition_change",
	"slurm_sched_p_get_errno",
	"slurm_sched_p_strerror",
	"slurm_sched_p_requeue",
	"slurm_sched_p_get_conf"
};

static slurm_sched_ops_t ops;
static plugin_context_t	*g_context = NULL;
static pthread_mutex_t g_context_lock = PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;

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
	char *plugin_type = "sched";
	char *type = NULL;

	if ( init_run && g_context )
		return retval;

	slurm_mutex_lock( &g_context_lock );

	if ( g_context )
		goto done;

	type = slurm_get_sched_type();
	g_context = plugin_context_create(
		plugin_type, type, (void **)&ops, syms, sizeof(syms));

	if (!g_context) {
		error("cannot create %s context for %s", plugin_type, type);
		retval = SLURM_ERROR;
		goto done;
	}
	init_run = true;

done:
	slurm_mutex_unlock( &g_context_lock );
	xfree(type);
	return retval;
}

/* *********************************************************************** */
/*  TAG(                        slurm_sched_fini                        )  */
/* *********************************************************************** */
extern int
slurm_sched_fini( void )
{
	int rc;

	if (!g_context)
		return SLURM_SUCCESS;

	init_run = false;
	rc = plugin_context_destroy(g_context);
	g_context = NULL;

	if ( (slurm_get_preempt_mode() & PREEMPT_MODE_GANG) &&
	     (gs_fini() != SLURM_SUCCESS))
		error( "cannot stop gang scheduler" );

	return rc;
}


/* *********************************************************************** */
/*  TAG(                        slurm_sched_g_reconfig                  )  */
/* *********************************************************************** */
extern int
slurm_sched_g_reconfig( void )
{
	if ( slurm_sched_init() < 0 )
		return SLURM_ERROR;

	if ( (slurm_get_preempt_mode() & PREEMPT_MODE_GANG) &&
	     (gs_reconfig() != SLURM_SUCCESS))
		error( "cannot reconfigure gang scheduler" );

	return (*(ops.reconfig))();
}

/* *********************************************************************** */
/*  TAG(                        slurm_sched_g_schedule                  )  */
/* *********************************************************************** */
int
slurm_sched_g_schedule( void )
{
	if ( slurm_sched_init() < 0 )
		return SLURM_ERROR;

#if 0
	/* Must have job write lock and node read lock set here */
	if ( (slurm_get_preempt_mode() & PREEMPT_MODE_GANG) &&
	     (gs_job_scan() != SLURM_SUCCESS))
		error( "gang scheduler could not rescan jobs" );
#endif

	return (*(ops.schedule))();
}

/* *********************************************************************** */
/*  TAG(                        slurm_sched_g_newalloc                  )  */
/* *********************************************************************** */
int
slurm_sched_g_newalloc( struct job_record *job_ptr )
{
	if ( slurm_sched_init() < 0 )
		return SLURM_ERROR;

	if ( (slurm_get_preempt_mode() & PREEMPT_MODE_GANG) &&
	     (gs_job_start( job_ptr ) != SLURM_SUCCESS)) {
		error( "gang scheduler problem starting job %u",
		       job_ptr->job_id);
	}

	return (*(ops.newalloc))( job_ptr );
}

/* *********************************************************************** */
/*  TAG(                        slurm_sched_g_freealloc                 )  */
/* *********************************************************************** */
int
slurm_sched_g_freealloc( struct job_record *job_ptr )
{
	if ( slurm_sched_init() < 0 )
		return SLURM_ERROR;

	if ( (slurm_get_preempt_mode() & PREEMPT_MODE_GANG) &&
	     (gs_job_fini( job_ptr ) != SLURM_SUCCESS)) {
		error( "gang scheduler problem finishing job %u",
		       job_ptr->job_id);
	}

	return (*(ops.freealloc))( job_ptr );
}


/* *********************************************************************** */
/*  TAG(                   slurm_sched_g_initital_priority              )  */
/* *********************************************************************** */
uint32_t
slurm_sched_g_initial_priority( uint32_t last_prio,
			      struct job_record *job_ptr )
{
	if ( slurm_sched_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.initial_priority))( last_prio, job_ptr );
}

/* *********************************************************************** */
/*  TAG(                   slurm_sched_g_job_is_pending                 )  */
/* *********************************************************************** */
void
slurm_sched_g_job_is_pending( void )
{
	if ( slurm_sched_init() < 0 )
		return;

	(*(ops.job_is_pending))();
}

/* *********************************************************************** */
/*  TAG(                   slurm_sched_g_partition_change               )  */
/* *********************************************************************** */
void
slurm_sched_g_partition_change( void )
{
	if ( slurm_sched_init() < 0 )
		return;

	if ( (slurm_get_preempt_mode() & PREEMPT_MODE_GANG) &&
	     (gs_reconfig() != SLURM_SUCCESS))
		error( "cannot reconfigure gang scheduler" );

	(*(ops.partition_change))();
}

/* *********************************************************************** */
/*  TAG(                   slurm_sched_g_get_errno                      )  */
/* *********************************************************************** */
int
slurm_sched_g_get_errno( void )
{
	if ( slurm_sched_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.get_errno))( );
}

/* *********************************************************************** */
/*  TAG(                   slurm_sched_g_strerror                       )  */
/* *********************************************************************** */
char *
slurm_sched_g_strerror( int errnum )
{
	if ( slurm_sched_init() < 0 )
		return NULL;

	return (*(ops.strerror))( errnum );
}

/* *********************************************************************** */
/*  TAG(                   slurm_sched_g_requeue                        )  */
/* *********************************************************************** */
void
slurm_sched_g_requeue( struct job_record *job_ptr, char *reason )
{
        if ( slurm_sched_init() < 0 )
                return;

        (*(ops.job_requeue))( job_ptr, reason );
}

/* *********************************************************************** */
/*  TAG(                   slurm_sched_g_get_conf                       )  */
/* *********************************************************************** */
char *
slurm_sched_g_get_conf( void )
{
        if ( slurm_sched_init() < 0 )
                return NULL;

        return (*(ops.get_conf))( );
}


