/*****************************************************************************\
 *  sched_wiki.c - Wiki plugin for Moab and Maui schedulers.
 *****************************************************************************
 *  Copyright (C) 2006-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#include <stdio.h>

#include "src/common/plugin.h"
#include "src/common/log.h"
#include "src/common/slurm_priority.h"
#include "./msg.h"

const char		plugin_name[]	= "Wiki (Maui and Moab) Scheduler plugin";
const char		plugin_type[]	= "sched/wiki2";
const uint32_t		plugin_version	= 100;

/* A plugin-global errno. */
static int plugin_errno = SLURM_SUCCESS;

/**************************************************************************/
/*  TAG(                              init                              ) */
/**************************************************************************/
extern int init( void )
{
	verbose( "Wiki2 scheduler plugin loaded" );
	return spawn_msg_thread();
}

/**************************************************************************/
/*  TAG(                              fini                              ) */
/**************************************************************************/
extern void fini( void )
{
	term_msg_thread();
}


/***************************************************************************/
/*  TAG(                   slurm_sched_plugin_schedule                   ) */
/***************************************************************************/
extern int slurm_sched_plugin_schedule( void )
{
	(void) event_notify(1234, "Requested by Slurm");
	return SLURM_SUCCESS;
}

/***************************************************************************/
/*  TAG(                   slurm_sched_plugin_newalloc                   ) */
/***************************************************************************/
extern int slurm_sched_plugin_newalloc( struct job_record *job_ptr )
{
	return SLURM_SUCCESS;
}

/***************************************************************************/
/*  TAG(                   slurm_sched_plugin_freealloc                  ) */
/***************************************************************************/
extern int slurm_sched_plugin_freealloc( struct job_record *job_ptr )
{
	return SLURM_SUCCESS;
}


/**************************************************************************/
/* TAG(                   slurm_sched_plugin_initial_priority           ) */
/**************************************************************************/
extern uint32_t
slurm_sched_plugin_initial_priority( uint32_t last_prio,
				     struct job_record *job_ptr )
{
	(void) event_notify(1234, "Job submit");

	if ((job_ptr->job_id >= first_job_id) && exclude_part_ptr[0]) {
		/* Interactive job (initiated by srun) in partition
		 * excluded from Moab scheduling */
		int i;
		static int exclude_prio = 100000000;
		for (i=0; i<EXC_PART_CNT; i++) {
			if (exclude_part_ptr[i] == NULL)
				break;
			if (exclude_part_ptr[i] == job_ptr->part_ptr) {
				debug("Scheduiling job %u directly (no Moab)",
					job_ptr->job_id);
				return (exclude_prio--);
			}
		}
		return 0;
	}

	if (init_prio_mode == PRIO_DECREMENT)
		return priority_g_set(last_prio, job_ptr);

	return 0;
}

/**************************************************************************/
/* TAG(              slurm_sched_plugin_job_is_pending                  ) */
/**************************************************************************/
void slurm_sched_plugin_job_is_pending( void )
{
	/* No action required */
}

/**************************************************************************/
/* TAG(              slurm_sched_plugin_reconfig                        ) */
/**************************************************************************/
int slurm_sched_plugin_reconfig( void )
{
	int rc = parse_wiki_config();
	(void) event_notify(1235, "Partition change");
	return rc;
}

/**************************************************************************/
/* TAG(              slurm_sched_plugin_partition_change                ) */
/**************************************************************************/
void slurm_sched_plugin_partition_change( void )
{
	(void) event_notify(1235, "Partition change");
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
        wiki_job_requeue(job_ptr, reason);
}

/**************************************************************************/
/* TAG(              slurm_sched_get_conf                               ) */
/**************************************************************************/
char *slurm_sched_get_conf( void )
{
	return get_wiki_conf();
}
