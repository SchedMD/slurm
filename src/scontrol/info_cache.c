/*****************************************************************************\
 *  info_cache.c - Cache information functions for scontrol.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Copyright (C) 2013 SchedMD
 *  Produced at CSCS.
 *  Written by Stephen Trofinoff
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

#include "scontrol.h"

static void _print_cache_info(const char *, cache_info_msg_t *);

/* scontrol_print_cache()
 *
 * Retrieve and display the cache information
 * from the controller
 *
 */

void
scontrol_print_cache(const char *name)
{
	int cc;
	cache_info_msg_t *msg;
	uint16_t show_flags;

	show_flags = 0;
	/* call the controller to get the meat
	 */
	cc = slurm_load_cache(&msg, show_flags);
	if (cc != SLURM_PROTOCOL_SUCCESS) {
		/* Hosed, crap out.
		 */
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror ("slurm_load_cache error");
		return;
	}

	/* print the info
	 */
	_print_cache_info(name, msg);

	/* free at last
	 */
	slurm_free_cache_info_msg(msg);

	return;
}

static void _print_cache_info(const char *name, cache_info_msg_t *msg)
{
	char time_str[32], tmp_str[128];
	int cc;

	slurm_make_time_str((time_t *)&msg->time_stamp,
			    time_str, sizeof(time_str));
	snprintf(tmp_str, sizeof(tmp_str), "Controller cache data as of %s\n",
		 time_str);
	printf("%s\n", tmp_str);


	if (!msg->num_users) {
		printf("No users currently cached in Slurm.\n");
	} else {

		for (cc = 0; cc < msg->num_users; cc++) {
			if (name && strcmp(msg->cache_user_array[cc].name, name))
				continue;
			printf("UserName=%s%sUID=%u DefAccount=%s OldName=%s "
			       "DefWckey=%s\n",
			       msg->cache_user_array[cc].name,
			       one_liner ? " " : "\n    ",
			       msg->cache_user_array[cc].uid,
			       msg->cache_user_array[cc].default_acct,
			       msg->cache_user_array[cc].old_name,
			       msg->cache_user_array[cc].default_wckey);
			if (name)
				break;
		}
	}

	if (!msg->num_assocs) {
		printf("No associations currently cached in Slurm.\n");
	} else {

		/* Do NOT prematurely break from loop if printing records from
		 * a specified user as there could be more than one associaton
		 * record per user.
		 */
		for (cc = 0; cc < msg->num_assocs; cc++) {
			if (name) {
				if ( !msg->cache_assoc_array[cc].user ||
				     strcmp(msg->cache_assoc_array[cc].user, name))
					continue;
			}
			printf("ClusterName=%s Account=%s ParentAccount=%s "
			       "UserName=%s UID=%u Partition=%s%s Share=%u "
			       "GrpJobs=%u GrpNodes=%u GrpCPUs=%u GrpMem=%u "
			       "GrpSubmit=%u GrpWall=%u GrpCPUMins=%"PRIu64" "
			       "MaxJobs=%u MaxNodes=%u MaxCPUs=%u MaxSubmit=%u "
			       "MaxWall=%u MaxCPUMins=%"PRIu64" QOS=%u "
			       "GrpCPURunMins=%"PRIu64" "
			       "MaxCPURunMins=%"PRIu64" ID=%u "
			       "DefAssoc=%u Lft=%u ParentID=%u Rgt=%u\n",
			       msg->cache_assoc_array[cc].cluster,
			       msg->cache_assoc_array[cc].acct,
			       msg->cache_assoc_array[cc].parent_acct,
			       msg->cache_assoc_array[cc].user,
			       msg->cache_assoc_array[cc].uid,
			       msg->cache_assoc_array[cc].partition,
			       one_liner ? " " : "\n    " ,
			       msg->cache_assoc_array[cc].shares_raw,
			       msg->cache_assoc_array[cc].grp_jobs,
			       msg->cache_assoc_array[cc].grp_nodes,
			       msg->cache_assoc_array[cc].grp_cpus,
			       msg->cache_assoc_array[cc].grp_mem,
			       msg->cache_assoc_array[cc].grp_submit_jobs,
			       msg->cache_assoc_array[cc].grp_wall,
			       msg->cache_assoc_array[cc].grp_cpu_mins,
			       msg->cache_assoc_array[cc].max_jobs,
			       msg->cache_assoc_array[cc].max_nodes_pj,
			       msg->cache_assoc_array[cc].max_cpus_pj,
			       msg->cache_assoc_array[cc].max_submit_jobs,
			       msg->cache_assoc_array[cc].max_wall_pj,
			       msg->cache_assoc_array[cc].max_cpu_mins_pj,
			       msg->cache_assoc_array[cc].def_qos_id,
			       msg->cache_assoc_array[cc].grp_cpu_run_mins,
			       msg->cache_assoc_array[cc].max_cpu_run_mins,
			       msg->cache_assoc_array[cc].id,
			       msg->cache_assoc_array[cc].is_def,
			       msg->cache_assoc_array[cc].lft,
			       msg->cache_assoc_array[cc].parent_id,
			       msg->cache_assoc_array[cc].rgt);
		}
	}
}
