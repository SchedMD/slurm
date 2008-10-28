/*****************************************************************************\
 *  sched_gang.c - Gang scheduler plugin functions.
 *****************************************************************************
 *  Copyright (C) 2008 Hewlett-Packard Development Company, L.P.
 *  Written by Chris Holmes
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

#include "./gang.h"

const char		plugin_name[]	= "Gang Scheduler plugin";
const char		plugin_type[]	= "sched/gang";
const uint32_t		plugin_version	= 101;

/* A plugin-global errno. */
static int plugin_errno = SLURM_SUCCESS;

/**************************************************************************/
/*  TAG(                              init                              ) */
/**************************************************************************/
extern int init( void )
{
	verbose( "gang scheduler plugin loaded" );
	return gs_init();
}

/**************************************************************************/
/*  TAG(                              fini                              ) */
/**************************************************************************/
extern void fini( void )
{
	gs_fini();
}

/**************************************************************************/
/* TAG(              slurm_sched_plugin_reconfig                        ) */
/**************************************************************************/
int slurm_sched_plugin_reconfig( void )
{
	return gs_reconfig();
}

/***************************************************************************/
/*  TAG(                   slurm_sched_plugin_schedule                   ) */
/***************************************************************************/
extern int slurm_sched_plugin_schedule( void )
{
	/* synchronize job listings */
	debug3("sched/gang: slurm_sched_schedule called");
	/* return gs_job_scan();*/
	return SLURM_SUCCESS;
}

/***************************************************************************/
/*  TAG(                   slurm_sched_plugin_newalloc                   ) */
/***************************************************************************/
extern int slurm_sched_plugin_newalloc( struct job_record *job_ptr )
{
	if (!job_ptr)
		return SLURM_ERROR;
	debug3("sched/gang: slurm_sched_newalloc called");
	return gs_job_start(job_ptr);
}

/***************************************************************************/
/*  TAG(                   slurm_sched_plugin_freealloc                  ) */
/***************************************************************************/
extern int slurm_sched_plugin_freealloc( struct job_record *job_ptr )
{
	if (!job_ptr)
		return SLURM_ERROR;
	debug3("sched/gang: slurm_sched_freealloc called");
	return gs_job_fini(job_ptr);
}


/**************************************************************************/
/* TAG(                   slurm_sched_plugin_initial_priority           ) */ 
/**************************************************************************/
extern uint32_t 
slurm_sched_plugin_initial_priority( uint32_t last_prio,
				     struct job_record *job_ptr )
{
	/* ignored for timeslicing, but will be used to support priority */

	if (last_prio >= 2)
		return (last_prio - 1);
	else
		return 1;
}

/**************************************************************************/
/* TAG(              slurm_sched_plugin_job_is_pending                  ) */
/**************************************************************************/
void slurm_sched_plugin_job_is_pending( void )
{
	/* synchronize job listings? Here? */
	/*return gs_job_scan();*/
}

/**************************************************************************/
/* TAG(              slurm_sched_plugin_partition_change                ) */
/**************************************************************************/
void slurm_sched_plugin_partition_change( void )
{
        gs_reconfig();
}

/**************************************************************************/
/* TAG(              slurm_sched_get_errno                              ) */
/**************************************************************************/
int slurm_sched_get_errno( void )
{
	return plugin_errno;
}

/**************************************************************************/
/* TAG(              slurm_sched_strerror                               ) */
/**************************************************************************/
char *slurm_sched_strerror( int errnum )
{
	return NULL;
}

/**************************************************************************/
/* TAG(              slurm_sched_plugin_requeue                         ) */
/**************************************************************************/
void slurm_sched_plugin_requeue( struct job_record *job_ptr, char *reason )
{
	/* Empty. */
}

/**************************************************************************/
/* TAG(              slurm_sched_get_conf                               ) */
/**************************************************************************/
char *slurm_sched_get_conf( void )
{
	return NULL;
}
