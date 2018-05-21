/*****************************************************************************\
 *  slurm_protocol_api.h - high-level slurm communication functions
 *	definitions
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Copyright (C) 2013      Intel, Inc.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
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

#ifndef _SLURM_PROTOCOL_API_H
#define _SLURM_PROTOCOL_API_H

#include <inttypes.h>
#include <sys/types.h>
#include <stdarg.h>

#include "slurm/slurm_errno.h"

#include "src/common/pack.h"
#include "src/common/slurm_protocol_common.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_protocol_util.h"
#include "src/common/slurm_protocol_interface.h"

#define MIN_NOALLOC_JOBID ((uint32_t) 0xffff0000)
#define MAX_NOALLOC_JOBID ((uint32_t) 0xfffffffd)

#define CONVERT_NUM_UNIT_EXACT 0x00000001
#define CONVERT_NUM_UNIT_NO    0x00000002
#define CONVERT_NUM_UNIT_RAW   0x00000004

/* unit types */
enum {
	UNIT_NONE,
	UNIT_KILO,
	UNIT_MEGA,
	UNIT_GIGA,
	UNIT_TERA,
	UNIT_PETA,
	UNIT_UNKNOWN
};

/**********************************************************************\
 * protocol configuration functions
\**********************************************************************/

/* slurm_get_auth_info
 * returns the auth_info from slurmctld_conf object (AuthInfo parameter)
 * RET char * - AuthInfo value,  MUST be xfreed by caller
 */
char *slurm_get_auth_info(void);

/* slurm_get_sbcast_parameters
 * RET char * - SbcastParameters from slurm.conf,  MUST be xfreed by caller
 */
char *slurm_get_sbcast_parameters(void);

/* slurm_get_auth_ttl
 * returns the credential Time To Live option from the AuthInfo parameter
 * cache value in local buffer for best performance
 * RET int - Time To Live in seconds or 0 if not specified
 */
int slurm_get_auth_ttl(void);

/* slurm_get_batch_start_timeout
 * RET BatchStartTimeout value from slurm.conf
 */
uint16_t slurm_get_batch_start_timeout(void);

/*
 * slurm_get_control_cnt
 * RET Count of SlurmctldHost records from slurm.conf
 * (slurmctld server count, primary plus backups) 
 */
uint32_t slurm_get_control_cnt(void);

/* slurm_get_suspend_timeout
 * RET SuspendTimeout value from slurm.conf
 */
uint16_t slurm_get_suspend_timeout(void);

/* slurm_get_resume_timeout
 * RET ResumeTimeout value from slurm.conf
 */
uint16_t slurm_get_resume_timeout(void);

/* slurm_get_suspend_time
 * RET SuspendTime value from slurm.conf
 */
uint32_t slurm_get_suspend_time(void);

/* slurm_get_complete_wait
 * RET CompleteWait value from slurm.conf
 */
uint16_t slurm_get_complete_wait(void);

/* slurm_get_cpu_freq_def
 * RET CpuFreqDef value from slurm.conf
 */
uint32_t slurm_get_cpu_freq_def(void);

/* slurm_get_cpu_freq_govs
 * RET CpuFreqGovernors value from slurm.conf
 */
uint32_t slurm_get_cpu_freq_govs(void);

/* slurm_get_prolog_flags
 * RET PrologFlags value from slurm.conf
 */
uint32_t slurm_get_prolog_flags(void);

/* slurm_get_debug_flags
 * RET DebugFlags value from slurm.conf
 */
uint64_t slurm_get_debug_flags(void);

/* slurm_set_debug_flags
 */
void slurm_set_debug_flags(uint64_t debug_flags);

/* slurm_get_def_mem_per_cpu
 * RET DefMemPerCPU/Node value from slurm.conf
 */
uint64_t slurm_get_def_mem_per_cpu(void);

/* slurm_get_kill_on_bad_exit
 * RET KillOnBadExit value from slurm.conf
 */
uint16_t slurm_get_kill_on_bad_exit(void);

/* slurm_get_max_mem_per_cpu
 * RET MaxMemPerCPU/Node value from slurm.conf
 */
uint64_t slurm_get_max_mem_per_cpu(void);

/* slurm_get_epilog_msg_time
 * RET EpilogMsgTime value from slurm.conf
 */
uint32_t slurm_get_epilog_msg_time(void);

/* slurm_get_env_timeout
 * return default timeout for srun/sbatch --get-user-env option
 */
extern int slurm_get_env_timeout(void);

/* slurm_get_max_array_size
 * return MaxArraySize configuration parameter
 */
extern uint32_t slurm_get_max_array_size(void);

/* slurm_get_mpi_default
 * get default mpi value from slurmctld_conf object
 * RET char *   - mpi default value from slurm.conf,  MUST be xfreed by caller
 */
char *slurm_get_mpi_default(void);

/* slurm_get_mpi_params
 * get mpi parameters value from slurmctld_conf object
 * RET char *   - mpi default value from slurm.conf,  MUST be xfreed by caller
 */
char *slurm_get_mpi_params(void);

/* slurm_get_msg_aggr_params
 * get message aggregation parameters value from slurmctld_conf object
 * RET char *   - msg aggregation parameters default value from slurm.conf,
 *                MUST be xfreed by caller
 */
char *slurm_get_msg_aggr_params(void);

/* slurm_get_msg_timeout
 * get default message timeout value from slurmctld_conf object
 */
extern uint16_t slurm_get_msg_timeout(void);

/* slurm_get_reboot_program
 * RET char * - RebootProgram from slurm.conf, MUST be xfreed by caller
 */
extern char *slurm_get_reboot_program(void);

/* slurm_get_tcp_timeout
 * get default tcp timeout value from slurmctld_conf object
 */
extern uint16_t slurm_get_tcp_timeout(void);

/* slurm_api_set_default_config
 *	called by the send_controller_msg function to ensure that at least
 *	the compiled in default slurm_protocol_config object is initialized
 * RET int 		- return code
 */
extern int slurm_api_set_default_config(void);

/* slurm_api_clear_config
 * execute this only at program termination to free all memory */
extern void slurm_api_clear_config(void);

/* slurm_get_hash_val
 * get hash val of the slurm.conf from slurmctld_conf object from
 * slurmctld_conf object
 * RET uint32_t  - hash_val
 */
uint32_t slurm_get_hash_val(void);

/* slurm_get_health_check_program
 * get health_check_program from slurmctld_conf object from
 * slurmctld_conf object
 * RET char *   - health_check_program, MUST be xfreed by caller
 */
char *slurm_get_health_check_program(void);

/* slurm_get_gres_plugins
 * get gres_plugins from slurmctld_conf object from
 * slurmctld_conf object
 * RET char *   - gres_plugins, MUST be xfreed by caller
 */
char *slurm_get_gres_plugins(void);

/* slurm_get_job_submit_plugins
 * get job_submit_plugins from slurmctld_conf object from
 * slurmctld_conf object
 * RET char *   - job_submit_plugins, MUST be xfreed by caller
 */
char *slurm_get_job_submit_plugins(void);

/* slurm_get_slurmctld_logfile
 * get slurmctld_logfile from slurmctld_conf object from slurmctld_conf object
 * RET char *   - slurmctld_logfile, MUST be xfreed by caller
 */
char *slurm_get_job_slurmctld_logfile(void);

/* slurm_get_node_features_plugins
 * get node_features_plugins from slurmctld_conf object
 * RET char *   - knl_plugins, MUST be xfreed by caller
 */
char *slurm_get_node_features_plugins(void);

/* slurm_get_slurmctld_plugstack
 * get slurmctld_plugstack from slurmctld_conf object from
 * slurmctld_conf object
 * RET char *   - slurmctld_plugstack, MUST be xfreed by caller
 */
char *slurm_get_slurmctld_plugstack(void);

/* slurm_get_slurmctld_timeout
 * get slurmctld_timeout from slurmctld_conf object from
 * slurmctld_conf object
 * RET uint16_t - slurmctld timeout in seconds
 */
uint16_t slurm_get_slurmctld_timeout(void);

/* slurm_get_plugin_dir
 * get plugin directory from slurmctld_conf object from slurmctld_conf object
 * RET char *   - plugin directory, MUST be xfreed by caller
 */
char *slurm_get_plugin_dir(void);

/* slurm_get_priority_decay_hl
 * returns the priority decay half life in seconds from slurmctld_conf object
 * RET uint32_t - decay_hl in secs.
 */
uint32_t slurm_get_priority_decay_hl(void);

/* slurm_get_priority_calc_period
 * returns the seconds between priority decay calculation from slurmctld_conf
 * RET uint32_t - calc_period in secs.
 */
uint32_t slurm_get_priority_calc_period(void);

/* slurm_get_priority_favor_small
 * returns weither or not we are favoring small jobs from slurmctld_conf object
 * RET bool - true if favor small, false else.
 */
bool slurm_get_priority_favor_small(void);

/* slurm_get_priority_flags
 * returns the priority flags bitmap from slurmctld_conf object
 * RET uint16_t - priority flags
 */
uint16_t slurm_get_priority_flags(void);

/* slurm_get_priority_max_age
 * returns the priority age max in seconds from slurmctld_conf object
 * RET uint32_t - max_age in secs.
 */
uint32_t slurm_get_priority_max_age(void);

/* slurm_get_priority_params
 * RET char * - Value of PriorityParameters, MUST be xfreed by caller */
char *slurm_get_priority_params(void);

/* slurm_get_priority_reset_period
 * returns the priority usage reset period in seconds from slurmctld_conf object
 * RET uint16_t - flag, see PRIORITY_RESET_* in slurm/slurm.h.
 */
uint16_t slurm_get_priority_reset_period(void);

/* slurm_get_priority_type
 * returns the priority type from slurmctld_conf object
 * RET char *    - priority type, MUST be xfreed by caller
 */
char *slurm_get_priority_type(void);

/* slurm_get_priority_weight_age
 * returns the priority weight for age from slurmctld_conf object
 * RET uint32_t - factor weight.
 */
uint32_t slurm_get_priority_weight_age(void);

/* slurm_get_priority_weight_fairshare
 * returns the priority weight for fairshare from slurmctld_conf object
 * RET uint32_t - factor weight.
 */
uint32_t slurm_get_priority_weight_fairshare(void);

/* slurm_get_fs_dampening_factor
 * returns the dampening factor for fairshare from slurmctld_conf object
 * RET uint32_t - factor.
 */
uint16_t slurm_get_fs_dampening_factor(void);

/* slurm_set_fs_dampening_factor
 * sets the value of fs_dampening in slurmctld_conf object
 */
void slurm_set_fs_dampening_factor(uint16_t);

/* slurm_get_priority_weight_job_size
 * returns the priority weight for job size from slurmctld_conf object
 * RET uint32_t - factor weight.
 */
uint32_t slurm_get_priority_weight_job_size(void);

/* slurm_get_priority_weight_partition
 * returns the priority weight for partitions from slurmctld_conf object
 * RET uint32_t - factor weight.
 */
uint32_t slurm_get_priority_weight_partition(void);

/* slurm_get_priority_weight_qos
 * returns the priority weight for QOS from slurmctld_conf object
 * RET uint32_t - factor weight.
 */
uint32_t slurm_get_priority_weight_qos(void);

/* slurm_get_priority_weight_tres
 * returns the priority weights for TRES' from slurmctld_conf object
 * RET char * of the tres weights.
 */
char *slurm_get_priority_weight_tres(void);

/* slurm_get_tres_weight_array
 * IN weights_str - string of tres and weights to be parsed.
 * IN tres_cnt - count of how many tres' are on the system (e.g.
 * 		slurmctld_tres_cnt).
 * IN fail - whether to fatal or not if there are parsing errors.
 * RET double* of tres weights.
 */
double *slurm_get_tres_weight_array(char *weights_str, int tres_cnt, bool fail);

/* slurm_get_private_data
 * get private data from slurmctld_conf object
 * RET uint16_t   - private_data
 */
uint16_t slurm_get_private_data(void);

/* slurm_get_resume_program
 * returns the ResumeProgram from slurmctld_conf object
 * RET char *    - ResumeProgram, MUST be xfreed by caller
 */
char *slurm_get_resume_program(void);

/* slurm_get_state_save_location
 * get state_save_location from slurmctld_conf object from slurmctld_conf object
 * RET char *   - state_save_location directory, MUST be xfreed by caller
 */
char *slurm_get_state_save_location(void);

/* slurm_get_tmp_fs
 * returns the TmpFS configuration parameter from slurmctld_conf object
 * RET char *    - tmp_fs, MUST be xfreed by caller
 */
extern char *slurm_get_tmp_fs(char *node_name);

/* slurm_get_auth_type
 * returns the authentication type from slurmctld_conf object
 * RET char *    - auth type, MUST be xfreed by caller
 */
extern char *slurm_get_auth_type(void);

/* slurm_set_auth_type
 * set the authentication type in slurmctld_conf object
 * used for security testing purposes
 * RET 0 or error code
 */
extern int slurm_set_auth_type(char *auth_type);

/* slurm_get_auth_type
 * returns the authentication type from slurmctld_conf object
 * RET char *    - auth type, MUST be xfreed by caller
 */
extern char *slurm_get_auth_type(void);

/* slurm_get_bb_params
 * returns the BurstBufferParameters (bb_params) from slurmctld_conf object
 * RET char *    - BurstBufferParameters, MUST be xfreed by caller
 */
extern char *slurm_get_bb_params(void);

/* slurm_get_bb_type
 * returns the BurstBufferType (bb_type) from slurmctld_conf object
 * RET char *    - BurstBufferType, MUST be xfreed by caller
 */
extern char *slurm_get_bb_type(void);

/* slurm_get_checkpoint_type
 * returns the checkpoint_type from slurmctld_conf object
 * RET char *    - checkpoint type, MUST be xfreed by caller
 */
extern char *slurm_get_checkpoint_type(void);

 /* slurm_get_checkpoint_dir
  * returns the checkpoint_dir from slurmctld_conf object
  * RET char *    - checkpoint dir, MUST be xfreed by caller
  */
extern char *slurm_get_checkpoint_dir(void);

/* slurm_get_cluster_name
 * returns the cluster name from slurmctld_conf object
 * RET char *    - cluster name,  MUST be xfreed by caller
 */
char *slurm_get_cluster_name(void);

/* slurm_get_comm_parameters
 * returns the value of comm_param in slurmctld_conf object
 * RET char *    - comm parameters, MUST be xfreed by caller
 */
extern char *slurm_get_comm_parameters(void);

/* slurm_get_crypto_type
 * returns the crypto_type from slurmctld_conf object
 * RET char *    - crypto type, MUST be xfreed by caller
 */
extern char *slurm_get_crypto_type(void);

/* slurm_get_fast_schedule
 * returns the value of fast_schedule in slurmctld_conf object
 */
extern uint16_t slurm_get_fast_schedule(void);

/* slurm_get_use_spec_resources
 * returns the value of use_spec_resources in slurmctld_conf object
 */
extern uint16_t slurm_get_use_spec_resources(void);

/* slurm_get_power_parameters
 * returns the PowerParameters from slurmctld_conf object
 * RET char *    - PowerParameters, MUST be xfreed by caller
 */
extern char *slurm_get_power_parameters(void);

/* slurm_set_power_parameters
 * reset the PowerParameters object
 */
extern void slurm_set_power_parameters(char *power_parameters);

/* slurm_get_power_plugin
 * returns the PowerPlugin from slurmctld_conf object
 * RET char *    - PowerPlugin, MUST be xfreed by caller
 */
extern char *slurm_get_power_plugin(void);

/* slurm_get_track_wckey
 * returns the value of track_wckey in slurmctld_conf object
 */
extern uint16_t slurm_get_track_wckey(void);

/* slurm_get_topology_param
 * returns the value of topology_param in slurmctld_conf object
 * RET char *    - topology parameters, MUST be xfreed by caller
 */
extern char * slurm_get_topology_param(void);

/* slurm_get_topology_plugin
 * returns the value of topology_plugin in slurmctld_conf object
 * RET char *    - topology type, MUST be xfreed by caller
 */
extern char * slurm_get_topology_plugin(void);

/* slurm_set_tree_width
 * sets the value of tree_width in slurmctld_conf object
 * RET 0 or error code
 */
extern int slurm_set_tree_width(uint16_t tree_width);

/* slurm_get_tree_width
 * returns the value of tree_width in slurmctld_conf object
 */
extern uint16_t slurm_get_tree_width(void);

/* slurm_get_vsize_factor
 * returns the value of vsize_factor in slurmctld_conf object
 */
extern uint16_t slurm_get_vsize_factor(void);

/* slurm_get_accounting_storage_type
 * returns the accounting storage type from slurmctld_conf object
 * RET char *    - accounting storage type,  MUST be xfreed by caller
 */
char *slurm_get_accounting_storage_type(void);

/* slurm_get_accounting_storage_tres
 * returns the accounting storage tres from slurmctld_conf object
 * RET char *    - accounting storage tres,  MUST be xfreed by caller
 */
char *slurm_get_accounting_storage_tres(void);

/* slurm_set_accounting_storage_tres
 * sets the value of accounting_storage_tres in slurmctld_conf object
 * RET 0 or error_code
 */
extern int slurm_set_accounting_storage_tres(char *tres);

/* slurm_get_accounting_storage_user
 * returns the storage user from slurmctld_conf object
 * RET char *    - storage user,  MUST be xfreed by caller
 */
char *slurm_get_accounting_storage_user(void);

/* slurm_set_accounting_storage_user
 * IN: char *user (name of file or database)
 * RET 0 or error code
 */
int slurm_set_accounting_storage_user(char *user);

/* slurm_get_accounting_storage_backup_host
 * returns the storage host from slurmctld_conf object
 * RET char *    - storage backup host,  MUST be xfreed by caller
 */
char *slurm_get_accounting_storage_backup_host(void);

/* slurm_get_accounting_storage_host
 * returns the storage host from slurmctld_conf object
 * RET char *    - storage host,  MUST be xfreed by caller
 */
char *slurm_get_accounting_storage_host(void);

/* slurm_set_accounting_storage_host
 * IN: char *host (name of file or database)
 * RET 0 or error code
 */
int slurm_set_accounting_storage_host(char *host);

/* slurm_get_accounting_storage_enforce
 * returns what level to enforce associations at
 */
uint16_t slurm_get_accounting_storage_enforce(void);

/* slurm_get_is_association_based_accounting
 * returns if we are doing accounting by associations
 */
int slurm_get_is_association_based_accounting(void);

/* slurm_get_accounting_storage_pass
 * returns the storage password from slurmctld_conf object
 * RET char *    - storage location,  MUST be xfreed by caller
 */
char *slurm_get_accounting_storage_loc(void);

/* slurm_set_accounting_storage_loc
 * IN: char *loc (name of file or database)
 * RET 0 or error code
 */
int slurm_set_accounting_storage_loc(char *loc);

/* slurm_get_accounting_storage_pass
 * returns the storage password from slurmctld_conf object
 * RET char *    - storage password,  MUST be xfreed by caller
 */
char *slurm_get_accounting_storage_pass(void);

/* slurm_get_accounting_storage_port
 * returns the storage port from slurmctld_conf object
 * RET uint32_t   - storage port
 */
uint32_t slurm_get_accounting_storage_port(void);

/* slurm_set_accounting_storage_port
 * sets the storage port in slurmctld_conf object
 * RET 0 or error code
 */
int slurm_set_accounting_storage_port(uint32_t storage_port);

/* slurm_get_launch_params
 * get launch_params from slurmctld_conf object
 * RET char *   - launch_params, MUST be xfreed by caller
 */
char *slurm_get_launch_params(void);

/* slurm_get_launch_type
 * get launch_type from slurmctld_conf object
 * RET char *   - launch_type, MUST be xfreed by caller
 */
char *slurm_get_launch_type(void);

/* slurm_set_launch_type
 * set launch_type in slurmctld_conf object
 * RET 0 or error code
 */
int slurm_set_launch_type(char *launch_type);

/* slurm_get_mcs_plugin
 * RET mcs_plugin name, must be xfreed by caller */
char *slurm_get_mcs_plugin(void);

/* slurm_get_mcs_plugin_params
 * RET mcs_plugin_params name, must be xfreed by caller */
char *slurm_get_mcs_plugin_params(void);

/* slurm_get_preempt_mode
 * returns the PreemptMode value from slurmctld_conf object
 * RET uint16_t   - PreemptMode value (See PREEMPT_MODE_* in slurm.h)
 */
uint16_t slurm_get_preempt_mode(void);

/* slurm_get_jobacct_gather_type
 * returns the job accounting type from slurmctld_conf object
 * RET char *    - job accounting type,  MUST be xfreed by caller
 */
char *slurm_get_jobacct_gather_type(void);

/* slurm_get_jobacct_gather_params
 * returns the job accounting params from the slurmctld_conf object
 * RET char *    - job accounting params,  MUST be xfreed by caller
 */
char *slurm_get_jobacct_gather_params(void);

/* slurm_get_jobacct_gather_freq
 * returns the job accounting poll frequency from the slurmctld_conf object
 * RET int    - job accounting frequency
 */
char *slurm_get_jobacct_gather_freq(void);

/* slurm_get_jobcomp_type
 * returns the job completion logger type from slurmctld_conf object
 * RET char *    - job completion type,  MUST be xfreed by caller
 */
char *slurm_get_jobcomp_type(void);

/* slurm_get_jobcomp_loc
 * returns the job completion loc from slurmctld_conf object
 * RET char *    - job completion location,  MUST be xfreed by caller
 */
char *slurm_get_jobcomp_loc(void);

/* slurm_get_jobcomp_user
 * returns the storage user from slurmctld_conf object
 * RET char *    - storage user,  MUST be xfreed by caller
 */
char *slurm_get_jobcomp_user(void);

/* slurm_get_jobcomp_host
 * returns the storage host from slurmctld_conf object
 * RET char *    - storage host,  MUST be xfreed by caller
 */
char *slurm_get_jobcomp_host(void);

/* slurm_get_jobcomp_pass
 * returns the storage password from slurmctld_conf object
 * RET char *    - storage password,  MUST be xfreed by caller
 */
char *slurm_get_jobcomp_pass(void);

/* slurm_get_jobcomp_port
 * returns the storage port from slurmctld_conf object
 * RET uint32_t   - storage port
 */
uint32_t slurm_get_jobcomp_port(void);

/* slurm_set_jobcomp_port
 * sets the jobcomp port in slurmctld_conf object
 * RET 0 or error code
 */
int slurm_set_jobcomp_port(uint32_t port);

/* slurm_get_keep_alive_time
 * returns keep_alive_time slurmctld_conf object
 * RET uint16_t        - keep_alive_time
 */
uint16_t slurm_get_keep_alive_time(void);

/* slurm_get_kill_wait
 * returns kill_wait from slurmctld_conf object
 * RET uint16_t        - kill_wait
 */
uint16_t slurm_get_kill_wait(void);

/* slurm_get_preempt_type
 * get PreemptType from slurmctld_conf object
 * RET char *   - preempt type, MUST be xfreed by caller
 */
char *slurm_get_preempt_type(void);

/* slurm_get_propagate_prio_process
 * return the PropagatePrioProcess flag from slurmctld_conf object
 */
extern uint16_t slurm_get_propagate_prio_process(void);

/* slurm_get_proctrack_type
 * get ProctrackType from slurmctld_conf object
 * RET char *   - proctrack type, MUST be xfreed by caller
 */
char *slurm_get_proctrack_type(void);

/* slurm_get_acct_gather_energy_type
 * get EnergyAccountingType from slurmctld_conf object
 * RET char *   - acct_gather_energy type, MUST be xfreed by caller
 */
char *slurm_get_acct_gather_energy_type(void);

/* slurm_get_acct_gather_profile_type
 * get ProfileAccountingType from slurmctld_conf object
 * RET char *   - acct_gather_profile_type, MUST be xfreed by caller
 */
char *slurm_get_acct_gather_profile_type(void);

/* slurm_get_acct_interconnect_profile_type
 * get InterconnectAccountingType from slurmctld_conf object
 * RET char *   - acct_gather_interconnect_type, MUST be xfreed by caller
 */
char *slurm_get_acct_gather_interconnect_type(void);

/* slurm_get_acct_filesystem_profile_type
 * get FilesystemAccountingType from slurmctld_conf object
 * RET char *   - acct_gather_filesystem_type, MUST be xfreed by caller
 */
char *slurm_get_acct_gather_filesystem_type(void);


/* slurm_get_acct_gather_node_freq
 * returns the accounting poll frequency for requesting info from a
 * node from the slurmctld_conf object
 * RET int    - accounting node frequency
 */
extern uint16_t slurm_get_acct_gather_node_freq(void);

/* slurm_get_ext_sensors_type
 * get ExtSensorsType from slurmctld_conf object
 * RET char *   - ext_sensors type, MUST be xfreed by caller
 */
char *slurm_get_ext_sensors_type(void);

/* slurm_get_ext_sensors_freq
 * returns the external sensors sampling frequency from the slurmctld_conf
 * object for requesting info from a hardware component (node, switch, etc.)
 * RET int    - external sensors sampling frequency
 */
extern uint16_t slurm_get_ext_sensors_freq(void);

/* slurm_get_route_plugin
 * returns the value of route_plugin in slurmctld_conf object
 * RET char *    - routing type, MUST be xfreed by caller
 */
extern char * slurm_get_route_plugin(void);

/* slurm_get_sched_params
 * RET char * - Value of SchedulerParameters, MUST be xfreed by caller */
extern char *slurm_get_sched_params(void);

/* slurm_get_slurmd_port
 * returns slurmd port from slurmctld_conf object
 * RET uint16_t	- slurmd port
 */
extern uint16_t slurm_get_slurmd_port(void);

/* slurm_get_slurm_user_id
 * returns slurm uid from slurmctld_conf object
 * RET uint32_t	- slurm user id
 */
uint32_t slurm_get_slurm_user_id(void);

/* slurm_get_slurmd_user_id
 * returns slurmd uid from slurmctld_conf object
 * RET uint32_t	- slurmd user id
 */
uint32_t slurm_get_slurmd_user_id(void);

/*
 * slurm_get_slurmd_parmams
 * returns slurmd_params
 * RET char * - slurmd_params
 */
char *slurm_get_slurmd_params(void);

/* slurm_get_sched_type
 * get sched type from slurmctld_conf object
 * RET char *   - sched type, MUST be xfreed by caller
 */
char *slurm_get_sched_type(void);

/* slurm_get_select_type
 * get select_type from slurmctld_conf object
 * RET char *   - select_type, MUST be xfreed by caller
 */
char *slurm_get_select_type(void);

/* slurm_get_select_type_param
 * get select_type_param from slurmctld_conf object
 * RET uint16_t   - select_type_param
 */
uint16_t slurm_get_select_type_param(void);

/* slurm_set_select_type_param
 * set select_type_param for slurmctld_conf object
 * IN uint16_t   - select_type_param
 */
void slurm_set_select_type_param(uint16_t select_type_param);

/** Return true if (remote) system runs Cray XT/XE */
bool is_cray_select_type(void);

/* slurm_get_switch_type
 * get switch type from slurmctld_conf object
 * RET char *   - switch type, MUST be xfreed by caller
 */
char *slurm_get_switch_type(void);

/* slurm_get_wait_time
 * returns wait_time from slurmctld_conf object
 * RET uint16_t        - wait_time
 */
uint16_t slurm_get_wait_time(void);

/* slurm_get_srun_prolog
 * return the name of the srun prolog program
 * RET char *   - name of prolog program, must be xfreed by caller
 */
char *slurm_get_srun_prolog(void);

/* slurm_get_srun_epilog
 * return the name of the srun epilog program
 * RET char *   - name of epilog program, must be xfreed by caller
 */
char *slurm_get_srun_epilog(void);

/* slurm_get_srun_port_range()
 *
 * Return the array with 2 members indicating the
 * min and max ports that srun should use to listen to.
 */
uint16_t *slurm_get_srun_port_range(void);

/* slurm_get_task_epilog
 * RET task_epilog name, must be xfreed by caller */
char *slurm_get_task_epilog(void);

/* slurm_get_task_prolog
 * RET task_prolog name, must be xfreed by caller */
char *slurm_get_task_prolog(void);

/* slurm_get_task_plugin
 * RET task_plugin name, must be xfreed by caller */
char *slurm_get_task_plugin(void);

/* slurm_get_task_plugin_param */
uint32_t slurm_get_task_plugin_param(void);

/* Get SchedulerTimeSlice (secs) */
uint16_t slurm_get_time_slice(void);

/* slurm_get_core_spec_plugin
 * RET core_spec plugin name, must be xfreed by caller */
char *slurm_get_core_spec_plugin(void);

/* slurm_get_job_container_plugin
 * RET job_container plugin name, must be xfreed by caller */
char *slurm_get_job_container_plugin(void);

/* slurm_get_slurmd_spooldir
 * RET slurmd_spooldir name, must be xfreed by caller */
char *slurm_get_slurmd_spooldir(char *node_name);

/* slurm_get_layouts
 * RET comma seperated list of layouts in a string, must be xfreed by caller
 */
char *slurm_get_layouts(void);

/* slurm_get_srun_eio_timeout()
 *
 * Return the eio timeout for srun.
 */
int16_t slurm_get_srun_eio_timeout(void);

/* slurm_get_prolog_timeout()
 *
 * Return the timeout used for prolog/epilog
 */
extern uint16_t slurm_get_prolog_timeout(void);

/**********************************************************************\
 * general message management functions used by slurmctld, slurmd
\**********************************************************************/

/* In the socket implementation it creates a socket, binds to it, and
 *	listens for connections.
 *
 * IN port		- port to bind the msg server to
 * RET slurm_fd		- file descriptor of the connection created
 */
extern int slurm_init_msg_engine_port(uint16_t port);

/* Creates a TCP socket and binds to a port in the given
 * range.
 *
 * IN ports - range of ports from which to select the one
 *            to bind
 * RET slurm_fd - file descriptor of the listening socket
 */
extern int slurm_init_msg_engine_ports(uint16_t *);

/* sock_bind_range()
 *
 * Try to bind() sock to any port in a given interval of ports
 */
extern int sock_bind_range(int, uint16_t *, bool local);

/* In the socket implementation it creates a socket, binds to it, and
 *	listens for connections.
 *
 * IN  addr_name        - address to bind the msg server to (NULL means any)
 * IN port		- port to bind the msg server to
 * RET slurm_fd		- file descriptor of the connection created
 */
extern int slurm_init_msg_engine_addrname_port(char *addr_name,
						    uint16_t port);

/* In the socket implementation it creates a socket, binds to it, and
 *	listens for connections.
 * IN slurm_address 	- slurm_addr_t to bind the msg server to
 * RET slurm_fd		- file descriptor of the connection created
 */
extern int slurm_init_msg_engine(slurm_addr_t * slurm_address);

/* In the bsd implmentation maps directly to a accept call
 * IN open_fd		- file descriptor to accept connection on
 * OUT slurm_address 	- slurm_addr_t of the accepted connection
 * RET slurm_fd		- file descriptor of the connection created
 */
extern int slurm_accept_msg_conn(int open_fd, slurm_addr_t * slurm_address);

/* just calls close on an established msg connection
 * IN open_fd	- an open file descriptor to close
 * RET int	- the return code
 */
extern int slurm_shutdown_msg_engine(int open_fd);

/**********************************************************************\
 * receive message functions
\**********************************************************************/

/* unpack a complete recieved message
 * OUT msg - a slurm_msg struct to be filled in by the function
 * IN  fd - file descriptor the message came from
 * IN  buffer - Buf we will fill in the message with
 * RET int	- returns 0 on success, -1 on failure and sets errno
 */

extern int slurm_unpack_received_msg(slurm_msg_t *msg, int fd, Buf buffer);

/*
 *  Receive a slurm message on the open slurm descriptor "fd" waiting
 *    at most "timeout" seconds for the message data. If timeout is
 *    zero, a default timeout is used. Memory for the message data
 *    (msg->data) is allocated from within this function, and must be
 *    freed at some point using one of the slurm_free* functions.
 *    Also a slurm_cred is allocated (msg->auth_cred) which must be
 *    freed with g_slurm_auth_destroy() if it exists.
 *
 * IN open_fd	- file descriptor to receive msg on
 * OUT msg	- a slurm_msg struct to be filled in by the function
 * IN timeout	- how long to wait in milliseconds
 * RET int	- returns 0 on success, -1 on failure and sets errno
 */
int slurm_receive_msg(int fd, slurm_msg_t *msg, int timeout);

/*
 *  Receive a slurm message on the open slurm descriptor "fd" waiting
 *    at most "timeout" seconds for the message data. If timeout is
 *    zero, a default timeout is used. Memory is allocated for the
 *    returned list and must be freed at some point using the
 *    list_destroy function.
 *
 * IN open_fd	- file descriptor to receive msg on
 * IN steps	- how many steps down the tree we have to wait for
 * IN timeout	- how long to wait in milliseconds
 * RET List	- List containing the responses of the children (if any) we
 *                forwarded the message to. List containing type
 *                (ret_data_info_t). NULL is returned on failure. and
 *                errno set.
 */
List slurm_receive_msgs(int fd, int steps, int timeout);

/*
 *  Receive a slurm message on the open slurm descriptor "fd" waiting
 *    at most "timeout" seconds for the message data. This will also
 *    forward the message to the nodes contained in the forward_t
 *    structure inside the header of the message.  If timeout is
 *    zero, a default timeout is used. The 'resp' is the actual message
 *    received and contains the ret_list of it's children and the
 *    forward_structure_t containing information about it's children
 *    also. Memory is allocated for the returned msg and the returned
 *    list both must be freed at some point using the
 *    slurm_free_functions and list_destroy function.
 *
 * IN open_fd	- file descriptor to receive msg on
 * OUT resp	- a slurm_msg struct to be filled in by the function
 * IN timeout	- how long to wait in milliseconds
 * RET int	- returns 0 on success, -1 on failure and sets errno
 */
int slurm_receive_msg_and_forward(int fd, slurm_addr_t *orig_addr,
				  slurm_msg_t *resp, int timeout);

/**********************************************************************\
 * send message functions
\**********************************************************************/

/* sends a message to an arbitrary node
 *
 * IN open_fd		- file descriptor to send msg on
 * IN msg		- a slurm msg struct to be sent
 * RET int		- size of msg sent in bytes
 */
int slurm_send_node_msg(int open_fd, slurm_msg_t *msg);

/**********************************************************************\
 * msg connection establishment functions used by msg clients
\**********************************************************************/

/*
 * Calls connect to make a connection-less datagram connection to the
 *	primary or secondary slurmctld message engine
 * IN/OUT addr       - address of controller contacted
 * IN/OUT use_backup - IN: whether to try the backup first or not
 *                     OUT: set to true if connection established with backup
 * IN comm_cluster_rec	- Communication record (host/port/version)/
 * RET slurm_fd	- file descriptor of the connection created
 */
extern int slurm_open_controller_conn(slurm_addr_t *addr, bool *use_backup,
				      slurmdb_cluster_rec_t *comm_cluster_rec);

/*
 * Calls connect to make a connection-less datagram connection to a specific
 *	primary or backup slurmctld message engine
 * IN dest      - controller to contact (0=primary, 1=backup, 2=backup2, etc.)
 * IN comm_cluster_rec	- Communication record (host/port/version)/
 * RET int      - file descriptor of the connection created
 */
extern int slurm_open_controller_conn_spec(int dest,
				      slurmdb_cluster_rec_t *comm_cluster_rec);

/* In the bsd socket implementation it creates a SOCK_STREAM socket
 *	and calls connect on it a SOCK_DGRAM socket called with connect
 *	is defined to only receive messages from the address/port pair
 *	argument of the connect call slurm_address - for now it is
 *	really just a sockaddr_in
 * IN slurm_address 	- slurm_addr_t of the connection destination
 * RET slurm_fd		- file descriptor of the connection created
 */
extern int slurm_open_msg_conn(slurm_addr_t * slurm_address);

/* just calls close on an established msg connection to close
 * IN open_fd	- an open file descriptor to close
 * RET int	- the return code
 */
extern int slurm_shutdown_msg_conn(int open_fd);


/**********************************************************************\
 * stream functions
\**********************************************************************/

/* slurm_write_stream
 * writes a buffer out a stream file descriptor
 * IN open_fd		- file descriptor to write on
 * IN buffer		- buffer to send
 * IN size		- size of buffer send
 * IN timeout		- how long to wait in milliseconds
 * RET size_t		- bytes sent , or -1 on errror
 */
extern size_t slurm_write_stream(int open_fd, char *buffer, size_t size);
extern size_t slurm_write_stream_timeout(int open_fd,
					 char *buffer, size_t size,
					 int timeout);

/* slurm_read_stream
 * read into buffer grom a stream file descriptor
 * IN open_fd		- file descriptor to read from
 * OUT buffer	- buffer to receive into
 * IN size		- size of buffer
 * IN timeout		- how long to wait in milliseconds
 * RET size_t		- bytes read , or -1 on errror
 */
extern size_t slurm_read_stream(int open_fd, char *buffer, size_t size);
extern size_t slurm_read_stream_timeout(int open_fd,
					char *buffer, size_t size,
					int timeout);

/**********************************************************************\
 * address conversion and management functions
\**********************************************************************/

/* slurm_set_addr
 * initializes the slurm_address with the supplied port and ip_address
 * OUT slurm_address	- slurm_addr_t to be filled in
 * IN port		- port in host order
 * IN host		- hostname or dns name
 */
extern void slurm_set_addr(slurm_addr_t * slurm_address,
			   uint16_t port, char *host);

/* slurm_get_ip_str
 * given a slurm_address it returns its port and ip address string
 * IN slurm_address	- slurm_addr_t to be queried
 * OUT port		- port number
 * OUT ip		- ip address in dotted-quad string form
 * IN buf_len		- length of ip buffer
 */
extern void slurm_get_ip_str(slurm_addr_t * slurm_address, uint16_t * port,
			     char *ip, unsigned int buf_len);

/* slurm_get_peer_addr
 * get the slurm address of the peer connection, similar to getpeeraddr
 * IN fd		- an open connection
 * OUT slurm_address	- place to park the peer's slurm_addr
 */
extern int slurm_get_peer_addr(int fd, slurm_addr_t * slurm_address);

/**********************************************************************\
 * slurm_addr_t pack routines
\**********************************************************************/

/* slurm_pack_slurm_addr_array
 * packs an array of slurm_addrs into a buffer
 * OUT slurm_address	- slurm_addr_t to pack
 * IN size_val  	- how many to pack
 * IN/OUT buffer	- buffer to pack the slurm_addr_t from
 * returns		- Slurm error code
 */
extern void slurm_pack_slurm_addr_array(slurm_addr_t * slurm_address,
					uint32_t size_val, Buf buffer);
/* slurm_unpack_slurm_addr_array
 * unpacks an array of slurm_addrs from a buffer
 * OUT slurm_address	- slurm_addr_t to unpack to
 * IN size_val  	- how many to unpack
 * IN/OUT buffer	- buffer to upack the slurm_addr_t from
 * returns		- Slurm error code
 */
extern int slurm_unpack_slurm_addr_array(slurm_addr_t ** slurm_address,
					 uint32_t * size_val, Buf buffer);

/**********************************************************************\
 * simplified communication routines
 * They open a connection do work then close the connection all within
 * the function
\**********************************************************************/

/* slurm_send_msg
 * given the original request message this function sends a
 *	arbitrary message back to the client that made the request
 * IN request_msg	- slurm_msg the request msg
 * IN msg_type          - message type being returned
 * IN resp_msg		- the message being returned to the client
 */
int slurm_send_msg(slurm_msg_t *msg, uint16_t msg_type, void *resp);

/* slurm_send_rc_msg
 * given the original request message this function sends a
 *	slurm_return_code message back to the client that made the request
 * IN request_msg	- slurm_msg the request msg
 * IN rc 		- the return_code to send back to the client
 */
int slurm_send_rc_msg(slurm_msg_t * request_msg, int rc);

/* slurm_send_rc_err_msg
 * given the original request message this function sends a
 *	slurm_return_code message back to the client that made the request
 * IN request_msg	- slurm_msg the request msg
 * IN rc		- the return_code to send back to the client
 * IN err_msg		- message for user
 */
int slurm_send_rc_err_msg(slurm_msg_t *msg, int rc, char *err_msg);

/*
 * slurm_send_recv_controller_msg
 * opens a connection to the controller, sends the controller a message,
 * listens for the response, then closes the connection
 * IN request_msg	- slurm_msg request
 * OUT response_msg	- slurm_msg response
 * IN comm_cluster_rec	- Communication record (host/port/version)/
 * RET int 		- returns 0 on success, -1 on failure and sets errno
 */
extern int slurm_send_recv_controller_msg(slurm_msg_t * request_msg,
				slurm_msg_t * response_msg,
				slurmdb_cluster_rec_t *comm_cluster_rec);


/* slurm_send_recv_node_msg
 * opens a connection to node,
 * and sends the nodes a message, listens
 * for the response, then closes the connections
 * IN request_msg	- slurm_msg request
 * OUT response_msg	- slurm_msg response
 * RET int 		- returns 0 on success, -1 on failure and sets errno
 */
int slurm_send_recv_node_msg(slurm_msg_t * request_msg,
			     slurm_msg_t * response_msg,
			     int timeout);

/*
 *  Send a message to the nodelist specificed using fanout
 *    Then return List containing type (ret_data_info_t).
 * IN nodelist	    - list of nodes to send to.
 * IN msg           - a slurm_msg struct to be sent by the function
 * IN timeout	    - how long to wait in milliseconds
 * IN quiet         - if set, reduce logging details
 * RET List	    - List containing the responses of the children
 *                    (if any) we forwarded the message to. List
 *                    containing type (ret_types_t).
 */
List slurm_send_recv_msgs(const char *nodelist, slurm_msg_t *msg, int timeout,
			  bool quiet);

/*
 * Sends back reroute_msg_t which directs the client to make the request to
 * another cluster.
 *
 * IN msg	  - msg to respond to.
 * IN cluster_rec - cluster to direct msg to.
 */
int slurm_send_reroute_msg(slurm_msg_t *msg,
			   slurmdb_cluster_rec_t *cluster_rec);

/*
 *  Send a message to msg->address
 *    Then return List containing type (ret_data_info_t).
 * IN msg           - a slurm_msg struct to be sent by the function
 * IN name          - the name of the node the message is being sent to
 * IN timeout	    - how long to wait in milliseconds
 * RET List	    - List containing the responses of the children
 *                    (if any) we forwarded the message to. List
 *                    containing type (ret_types_t).
 */
List slurm_send_addr_recv_msgs(slurm_msg_t *msg, char *name, int timeout);

/*
 *  Same as above, but only to one node
 *  returns 0 on success, -1 on failure and sets errno
 */

int slurm_send_recv_rc_msg_only_one(slurm_msg_t *req, int *rc, int timeout);

/*
 * Send message to controller and get return code.
 * Make use of slurm_send_recv_controller_msg(), which handles
 * support for backup controller and retry during transistion.
 * IN req - request to send
 * OUT rc - return code
 * IN comm_cluster_rec	- Communication record (host/port/version)
 * RET - 0 on success, -1 on failure
 */
extern int slurm_send_recv_controller_rc_msg(slurm_msg_t *req, int *rc,
				       slurmdb_cluster_rec_t *comm_cluster_rec);

/* slurm_send_only_controller_msg
 * opens a connection to the controller, sends the controller a
 * message then, closes the connection
 * IN request_msg	- slurm_msg request
 * IN comm_cluster_rec	- Communication record (host/port/version)
 * RET int		- return code
 * NOTE: NOT INTENDED TO BE CROSS-CLUSTER
 */
extern int slurm_send_only_controller_msg(slurm_msg_t *req,
				slurmdb_cluster_rec_t *comm_cluster_rec);

/* slurm_send_only_node_msg
 * opens a connection to node, sends the node a message then,
 * closes the connection
 * IN request_msg	- slurm_msg request
 * RET int 		- return code
 */
int slurm_send_only_node_msg(slurm_msg_t * request_msg);

/* Send and recv a slurm request and response on the open slurm descriptor
 * Doesn't close the connection.
 * IN fd	- file descriptor to receive msg on
 * IN req	- a slurm_msg struct to be sent by the function
 * OUT resp	- a slurm_msg struct to be filled in by the function
 * IN timeout	- how long to wait in milliseconds
 * RET int	- returns 0 on success, -1 on failure and sets errno
 */
extern int slurm_send_recv_msg(int fd, slurm_msg_t *req,
			       slurm_msg_t *resp, int timeout);

/* Slurm message functions */

/* set_span
 * build an array indicating how message fanout should occur
 * IN total - total number of nodes to communicate with
 * IN tree_width - message fanout, use system default if zero
 * NOTE: Returned array MUST be release by caller using xfree */
extern int *set_span(int total, uint16_t tree_width);

extern void slurm_free_msg_members(slurm_msg_t *msg);
extern void slurm_free_msg(slurm_msg_t * msg);

/* must free this memory with free not xfree */
extern char *nodelist_nth_host(const char *nodelist, int inx);
extern int nodelist_find(const char *nodelist, const char *name);
extern void convert_num_unit2(double num, char *buf, int buf_size,
			      int orig_type, int spec_type, int divisor,
			      uint32_t flags);
extern void convert_num_unit(double num, char *buf, int buf_size,
			     int orig_type, int spec_type, uint32_t flags);
extern int revert_num_unit(const char *buf);
extern int get_convert_unit_val(int base_type, char convert_to);
extern int get_unit_type(char unit);
extern void parse_int_to_array(int in, int *out);

/*
 * slurm_job_step_create - Ask the slurm controller for a new job step
 *	credential.
 * IN slurm_step_alloc_req_msg - description of job step request
 * OUT slurm_step_alloc_resp_msg - response to request
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 * NOTE: free the response using slurm_free_job_step_create_response_msg
 */
extern int slurm_job_step_create (
	job_step_create_request_msg_t *slurm_step_alloc_req_msg,
	job_step_create_response_msg_t **slurm_step_alloc_resp_msg);


/* Should this be in <slurm/slurm.h> ? */
/*
 * slurm_forward_data - forward arbitrary data to unix domain sockets on nodes
 * IN/OUT nodelist: Nodes to forward data to (if failure this list is changed to
 *                  reflect the failed nodes).
 * IN address: address of unix domain socket
 * IN len: length of data
 * IN data: real data
 * RET: error code
 */
extern int slurm_forward_data(
	char **nodelist, char *address, uint32_t len, const char *data);

/*
 * slurm_setup_sockaddr - setup a sockaddr_in struct to be used for
 *                        communication. If TopologyParameters has
 *                        NoInAddrAny set it will work of the
 *                        interface given from gethostname from the
 *                        hostname of the node.
 * OUT sin - uninitialized sockaddr_in
 * IN  port - port to used, we will call htons on it
 */
extern void slurm_setup_sockaddr(struct sockaddr_in *sin, uint16_t port);

/*
 * slurm_val_to_char - convert an int value (0-16) to a hex value (0-F) char.
 * IN v - value of number (0-16)
 * RET ascii value of int given, -1 on error.
 */
extern int slurm_hex_to_char(int v);

/*
 * slurm_val_to_char - convert a hex value (0-F) char to an int value (0-16).
 * IN c - char value (0-F)
 * RET value as an int, -1 on error.
 */
extern int slurm_char_to_hex(int c);

#endif
