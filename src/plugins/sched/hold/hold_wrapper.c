/*****************************************************************************\
 *  hold_wrapper.c - Hold all newly arriving jobs if there is a file
 *  "/etc/slurm.hold", otherwise use SLURM's internal scheduler.
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
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

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"

#include "src/common/plugin.h"
#include "src/common/log.h"
#include "src/slurmctld/slurmctld.h"
#include "src/common/slurm_priority.h"

const char		plugin_name[]	= "SLURM Hold Scheduler plugin";
const char		plugin_type[]	= "sched/hold";
const uint32_t		plugin_version	= 110;

/* A plugin-global errno. */
static int plugin_errno = SLURM_SUCCESS;

/**************************************************************************/
/*  TAG(                              init                              ) */
/**************************************************************************/
int init( void )
{
	verbose( "sched: Hold scheduler plugin loaded" );
	return SLURM_SUCCESS;
}

/**************************************************************************/
/*  TAG(                              fini                              ) */
/**************************************************************************/
void fini( void )
{
	/* Empty. */
}

/**************************************************************************/
/* TAG(              slurm_sched_p_reconfig                             ) */
/**************************************************************************/
int slurm_sched_p_reconfig( void )
{
	return SLURM_SUCCESS;
}

/***************************************************************************/
/*  TAG(                   slurm_sched_p_schedule                        ) */
/***************************************************************************/
int
slurm_sched_p_schedule( void )
{
	return SLURM_SUCCESS;
}

/***************************************************************************/
/*  TAG(                   slurm_sched_p_newalloc                        ) */
/***************************************************************************/
int
slurm_sched_p_newalloc( struct job_record *job_ptr )
{
	return SLURM_SUCCESS;
}

/***************************************************************************/
/*  TAG(                   slurm_sched_p_freealloc                       ) */
/***************************************************************************/
int
slurm_sched_p_freealloc( struct job_record *job_ptr )
{
	return SLURM_SUCCESS;
}


/**************************************************************************/
/* TAG(                   slurm_sched_p_initial_priority                ) */
/**************************************************************************/
uint32_t
slurm_sched_p_initial_priority( uint32_t last_prio,
				     struct job_record *job_ptr )
{
	struct stat buf;

	if (stat("/etc/slurm.hold", &buf) == 0)
		return 0;	/* hold all new jobs */

	return priority_g_set(last_prio, job_ptr);
}

/**************************************************************************/
/* TAG(              slurm_sched_p_job_is_pending                       ) */
/**************************************************************************/
void slurm_sched_p_job_is_pending( void )
{
	/* Empty. */
}

/**************************************************************************/
/* TAG(              slurm_sched_p_partition_change                     ) */
/**************************************************************************/
void slurm_sched_p_partition_change( void )
{
	/* Empty. */
}

/**************************************************************************/
/* TAG(              slurm_sched_p_get_errno                            ) */
/**************************************************************************/
int slurm_sched_p_get_errno( void )
{
	return plugin_errno;
}

/**************************************************************************/
/* TAG(              slurm_sched_p_strerror                             ) */
/**************************************************************************/
char *slurm_sched_p_strerror( int errnum )
{
	return NULL;
}

/**************************************************************************/
/* TAG(              slurm_sched_p_requeue                              ) */
/**************************************************************************/
void slurm_sched_p_requeue( struct job_record *job_ptr, char *reason )
{
	/* Empty. */
}

/**************************************************************************/
/* TAG(              slurm_sched_p_get_conf                             ) */
/**************************************************************************/
char *slurm_sched_p_get_conf( void )
{
	return NULL;
}
