/*****************************************************************************\
 *  power_common.h - Common header for power management
 *
 *  NOTE: These functions are designed so they can be used by multiple power
 *  management plugins at the same time, so the state information is largely in
 *  the individual plugin and passed as a pointer argument to these functions.
 *****************************************************************************
 *  Copyright (C) 2014-2015 SchedMD LLC.
 *  Written by Morris Jette <jette@schedmd.com>
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifndef __POWER_COMMON_H__
#define __POWER_COMMON_H__

#include "slurm/slurm.h"
#include "src/common/list.h"
#include "src/common/pack.h"
#include "src/slurmctld/slurmctld.h"

typedef struct power_by_job {
	uint32_t job_id;	/* Running Job ID */
	time_t   start_time;	/* When job allocation started */
	uint32_t alloc_watts;	/* Currently allocated power, in watts */
	uint32_t used_watts;	/* Recent power use rate, in watts */
} power_by_job_t;

typedef struct power_by_nodes {
	uint32_t alloc_watts;	/* Currently allocated power, in watts */
	bool increase_power;	/* Set if node's power allocation increasing */
	char *nodes;		/* Node names (nid range list values on Cray) */
} power_by_nodes_t;

/* For all nodes in a cluster
 * 1) set default values and
 * 2) return global power allocation/consumption information */
extern void get_cluster_power(struct node_record *node_record_table_ptr,
			      int node_record_count,
			      uint32_t *alloc_watts, uint32_t *used_watts);

/* For each running job, return power allocation/use information in a List
 * containing elements of type power_by_job_t.
 * NOTE: Job data structure must be locked on function entry
 * NOTE: Call list_delete() to free return value */
extern List get_job_power(List job_list,
			  struct node_record *node_record_table_ptr);

/* Execute a script, wait for termination and return its stdout.
 * script_name IN - Name of program being run (e.g. "StartStageIn")
 * script_path IN - Fully qualified program of the program to execute
 * script_args IN - Arguments to the script
 * max_wait IN - Maximum time to wait in milliseconds,
 *		 -1 for no limit (asynchronous)
 * data_in IN - data to use as program STDIN (NULL if not STDIN)
 * status OUT - Job exit code
 * Return stdout+stderr of spawned program, value must be xfreed. */
extern char *power_run_script(char *script_name, char *script_path,
			      char **script_argv, int max_wait, char *data_in,
			      int *status);

/* For a newly starting job, set "new_job_time" in each of it's nodes
 * NOTE: The job and node data structures must be locked on function entry */
extern void set_node_new_job(struct job_record *job_ptr,
			     struct node_record *node_record_table_ptr);

#endif	/* __POWER_COMMON_H__ */
