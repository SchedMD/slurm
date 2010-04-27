/*****************************************************************************\
 *  gres.h - driver for gres plugin
 *****************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
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

#ifndef _GRES_H
#define _GRES_H

#include <slurm/slurm.h>
#include "src/common/pack.h"

/*
 * Initialize the gres plugin.
 *
 * Returns a SLURM errno.
 */
extern int gres_plugin_init(void);

/*
 * Terminate the gres plugin. Free memory.
 *
 * Returns a SLURM errno.
 */
extern int gres_plugin_fini(void);

/*
 **************************************************************************
 *                          P L U G I N   C A L L S                       *
 **************************************************************************
 */

/*
 * Perform reconfig, re-read any configuration files
 * OUT did_change - set if gres configuration changed
 */
extern int gres_plugin_reconfig(bool *did_change);

/*
 * Provide a plugin-specific help message for salloc, sbatch and srun
 * IN/OUT msg - buffer provided by caller and filled in by plugin
 * IN msg_size - size of msg buffer in bytes
 */
extern int gres_plugin_help_msg(char *msg, int msg_size);

/*
 **************************************************************************
 *                 PLUGIN CALLS FOR SLURMD DAEMON                         *
 **************************************************************************
 */
/*
 * Load this node's configuration (i.e. how many resources it has)
 */
extern int gres_plugin_load_node_config(void);

/*
 * Pack this node's configuration into a buffer
 */
extern int gres_plugin_pack_node_config(Buf buffer);

/*
 **************************************************************************
 *                 PLUGIN CALLS FOR SLURMCTLD DAEMON                      *
 **************************************************************************
 */
/*
 * Unpack this node's configuration from a buffer
 * IN buffer - message buffer to unpack
 * IN node_name - name of node whose data is being unpacked
 */
extern int gres_plugin_unpack_node_config(Buf buffer, char *node_name);

/*
 * Validate a node's configuration and put a gres record onto a list
 *	(expected to be an element of slurmctld's node record).
 * Called immediately after gres_plugin_unpack_node_config().
 * IN node_name - name of the node for which the gres information applies
 * IN orig_config - Gres information supplied from slurm.conf
 * IN/OUT new_config - Gres information from slurm.conf if FastSchedule=0
 * IN/OUT gres_list - List of Gres records for this node to track usage
 * IN fast_schedule - 0: Validate and use actual hardware configuration
 *		      1: Validate hardware config, but use slurm.conf config
 *		      2: Don't validate hardware, use slurm.conf configuration
 * OUT reason_down - set to an explanation of failure, if any, don't set if NULL
 */
extern int gres_plugin_node_config_validate(char *node_name,
					    char *orig_config, 
					    char **new_config,
					    List *gres_list,
					    uint16_t fast_schedule,
					    char **reason_down);

/*
 * Note that a node's configuration has been modified (e.g. "scontol update ..")
 * IN node_name - name of the node for which the gres information applies
 * IN orig_config - Gres information supplied from slurm.conf
 * IN/OUT new_config - Updated gres info from slurm.conf if FastSchedule=0
 * IN/OUT gres_list - List of Gres records for this node to track usage
 * IN fast_schedule - 0: Validate and use actual hardware configuration
 *		      1: Validate hardware config, but use slurm.conf config
 *		      2: Don't validate hardware, use slurm.conf configuration
 */
extern int gres_plugin_node_reconfig(char *node_name,
				     char *orig_config, 
				     char **new_config,
				     List *gres_list,
				     uint16_t fast_schedule);

/*
 * Pack a node's current gres status, called from slurmctld for save/restore
 * IN gres_list - generated by gres_plugin_node_config_validate()
 * IN/OUT buffer - location to write state to
 * IN node_name - name of the node for which the gres information applies
 */
extern int gres_plugin_pack_node_state(List gres_list, Buf buffer,
				       char *node_name);
/*
 * Unpack a node's current gres status, called from slurmctld for save/restore
 * OUT gres_list - restored state stored by gres_plugin_pack_node_state()
 * IN/OUT buffer - location to read state from
 * IN node_name - name of the node for which the gres information applies
 */
extern int gres_plugin_unpack_node_state(List *gres_list, Buf buffer,
					 char *node_name);

/*
 * Log a node's current gres state
 * IN gres_list - generated by gres_plugin_node_config_validate()
 * IN node_name - name of the node for which the gres information applies
 */
extern void gres_plugin_node_state_log(List gres_list, char *node_name);

/*
 * Given a job's requested gres configuration, validate it and build a gres list
 * IN req_config - job request's gres input string
 * OUT gres_list - List of Gres records for this job to track usage
 * RET SLURM_SUCCESS or ESLURM__INVALIDGRES
 */
extern int gres_plugin_job_gres_validate(char *req_config, List *gres_list);

/*
 * Pack a job's current gres status, called from slurmctld for save/restore
 * IN gres_list - generated by gres_plugin_job_config_validate()
 * IN/OUT buffer - location to write state to
 * IN job_id - job's ID
 */
extern int gres_plugin_pack_job_state(List gres_list, Buf buffer,
				      uint32_t job_id);

/*
 * Unpack a job's current gres status, called from slurmctld for save/restore
 * OUT gres_list - restored state stored by gres_plugin_pack_job_state()
 * IN/OUT buffer - location to read state from
 * IN job_id - job's ID
 */
extern int gres_plugin_unpack_job_state(List *gres_list, Buf buffer,
					uint32_t job_id);

/*
 * Determine how many CPUs on the node can be used by this job
 * IN job_gres_list - job's gres_list built by gres_plugin_job_gres_validate()
 * IN node_gres_list - node's gres_list built by gres_plugin_node_config_validate()
 * IN use_total_gres - if set then consider all gres resources as available,
 *		       and none are commited to running jobs
 * RET: NO_VAL    - All CPUs on node are available
 *      otherwise - Specific CPU count
 */
extern uint32_t gres_plugin_cpus_usable_by_job(List job_gres_list, 
					       List node_gres_list, 
					       bool use_total_gres);

/*
 * Log a job's current gres state
 * IN gres_list - generated by gres_plugin_job_gres_validate()
 * IN job_id - job's ID
 */
extern void gres_plugin_job_state_log(List gres_list, uint32_t job_id);

#endif /* !_GRES_H */
