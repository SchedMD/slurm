/*****************************************************************************\
 *  builtin_wrapper.c - NO-OP plugin for SLURM's internal scheduler.
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov> et. al.
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
#include <slurm/slurm_errno.h>

#include "src/common/plugin.h"
#include "src/common/log.h"
#include "src/slurmctld/slurmctld.h"
#include "src/common/slurm_priority.h"
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/reservation.h"
#include "src/common/node_select.h"

const char		plugin_name[]	= "SLURM Built-in Scheduler plugin";
const char		plugin_type[]	= "sched/builtin";
const uint32_t		plugin_version	= 100;

/* A plugin-global errno. */
static int plugin_errno = SLURM_SUCCESS;

/**************************************************************************/
/*  TAG(                              init                              ) */
/**************************************************************************/
int init( void )
{
	verbose( "Built-in scheduler plugin loaded" );
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
/* TAG(              slurm_sched_plugin_reconfig                        ) */
/**************************************************************************/
int slurm_sched_plugin_reconfig( void )
{
	return SLURM_SUCCESS;
}

/***************************************************************************/
/*  TAG(                   slurm_sched_plugin_schedule                   ) */
/***************************************************************************/
int
slurm_sched_plugin_schedule( void )
{
	return SLURM_SUCCESS;
}

/***************************************************************************/
/*  TAG(                   slurm_sched_plugin_newalloc                   ) */
/***************************************************************************/
int
slurm_sched_plugin_newalloc( struct job_record *job_ptr )
{
	return SLURM_SUCCESS;
}

/***************************************************************************/
/*  TAG(                   slurm_sched_plugin_freealloc                  ) */
/***************************************************************************/
int
slurm_sched_plugin_freealloc( struct job_record *job_ptr )
{
	return SLURM_SUCCESS;
}


/**************************************************************************/
/* TAG(                   slurm_sched_plugin_initial_priority           ) */
/**************************************************************************/
uint32_t
slurm_sched_plugin_initial_priority( uint32_t last_prio,
				     struct job_record *job_ptr )
{
	return priority_g_set(last_prio, job_ptr);
}

/**************************************************************************/
/* TAG(              slurm_sched_plugin_job_is_pending                  ) */
/*   This entire implementation does nothing more than calculate the      */
/*   expected start time for pending jobs.  The logic is borrowed from    */
/*   backfill.c                                                           */
/**************************************************************************/
void slurm_sched_plugin_job_is_pending( void )
{
	int rc = SLURM_SUCCESS;
	int i, j, job_queue_size;
	List preemptee_candidates = NULL;
	struct job_queue *job_queue = NULL;
	struct job_record *job_ptr;
	struct part_record *part_ptr;
	bitstr_t *avail_bitmap = NULL;
	uint32_t max_nodes, min_nodes, req_nodes;
	time_t now = time(NULL);

	job_queue_size = build_job_queue(&job_queue);
	if (job_queue_size == 0) return;

	sort_job_queue(job_queue, job_queue_size);

	for (i = 0; i < job_queue_size; i++) {
		job_ptr = job_queue[i].job_ptr;
		part_ptr = job_ptr->part_ptr;

		/* Determine minimum and maximum node counts */
		min_nodes = MAX(job_ptr->details->min_nodes,
				part_ptr->min_nodes);

		if (job_ptr->details->max_nodes == 0)
			max_nodes = part_ptr->max_nodes;
		else
			max_nodes = MIN(job_ptr->details->max_nodes,
					part_ptr->max_nodes);

		max_nodes = MIN(max_nodes, 500000);     /* prevent overflows */

		if (job_ptr->details->max_nodes)
			req_nodes = max_nodes;
		else
			req_nodes = min_nodes;

		if (min_nodes > max_nodes) {
			/* job's min_nodes exceeds partition's max_nodes */
			continue;
		}

		j = job_test_resv(job_ptr, &now, true, &avail_bitmap);
		if (j != SLURM_SUCCESS)
			continue;

		rc = select_g_job_test(job_ptr, avail_bitmap,
				       min_nodes, max_nodes, req_nodes,
				       SELECT_MODE_WILL_RUN,
				       preemptee_candidates, NULL);

		FREE_NULL_BITMAP(avail_bitmap);
	}
}

/**************************************************************************/
/* TAG(              slurm_sched_plugin_partition_change                ) */
/**************************************************************************/
void slurm_sched_plugin_partition_change( void )
{
	/* Empty. */
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
