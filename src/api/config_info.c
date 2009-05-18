/****************************************************************************\
 *  config_info.c - get/print the system configuration information of slurm
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> and Kevin Tew <tew1@llnl.gov>.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <stdio.h>

#include <slurm/slurm.h>

#include "src/api/job_info.h"
#include "src/common/parse_time.h"
#include "src/common/read_config.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_resource_info.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/list.h"

/*
 * slurm_api_version - Return a single number reflecting the SLURM API's 
 *      version number. Use the macros SLURM_VERSION_NUM, SLURM_VERSION_MAJOR, 
 *      SLURM_VERSION_MINOR, and SLURM_VERSION_MICRO to work with this value
 * RET API's version number
 */
extern long slurm_api_version (void)
{
	return (long) SLURM_API_VERSION;
}


static char *
_select_info(uint16_t select_type_param)
{
	switch (select_type_param) {
		case SELECT_TYPE_INFO_NONE:
			return "NONE";
		case CR_CPU:
			return "CR_CPU";
		case CR_SOCKET:
			return "CR_SOCKET";
		case CR_CORE:
			return "CR_CORE";
		case CR_MEMORY:
			return "CR_MEMORY";
		case CR_SOCKET_MEMORY:
			return "CR_SOCKET_MEMORY";
		case CR_CORE_MEMORY:
			return "CR_CORE_MEMORY";
		case CR_CPU_MEMORY:
			return "CR_CPU_MEMORY";
		default:
			return "unknown";
	}
}

static char *
_reset_period_str(uint16_t reset_period)
{
	switch (reset_period) {
		case PRIORITY_RESET_NONE:
			return "NONE";
		case PRIORITY_RESET_NOW:
			return "NOW";
		case PRIORITY_RESET_DAILY:
			return "DAILY";
		case PRIORITY_RESET_WEEKLY:
			return "WEEKLY";
		case PRIORITY_RESET_MONTHLY:
			return "MONTHLY";
		case PRIORITY_RESET_QUARTERLY:
			return "QUARTERLY";
		case PRIORITY_RESET_YEARLY:
			return "YEARLY";
		default:
			return "UNKNOWN";
	}
}

/*
 * slurm_print_ctl_conf - output the contents of slurm control configuration 
 *	message as loaded using slurm_load_ctl_conf
 * IN out - file to write to
 * IN slurm_ctl_conf_ptr - slurm control configuration pointer
 */
void slurm_print_ctl_conf ( FILE* out, 
                            slurm_ctl_conf_info_msg_t * slurm_ctl_conf_ptr )
{
	char time_str[32], tmp_str[128], *xbuf;
	char *select_title = "";
#ifdef HAVE_BGL
	select_title = "Bluegene/L configuration";
#endif
#ifdef HAVE_BGP
	select_title = "Bluegene/P configuration";
#endif
#ifdef HAVE_BGQ
	select_title = "Bluegene/Q configuration";
#endif
	if ( slurm_ctl_conf_ptr == NULL )
		return ;

	slurm_make_time_str ((time_t *)&slurm_ctl_conf_ptr->last_update, 
			     time_str, sizeof(time_str));
	fprintf(out, "Configuration data as of %s\n", time_str);
	fprintf(out, "AccountingStorageEnforce = %u\n",
		slurm_ctl_conf_ptr->accounting_storage_enforce);
	fprintf(out, "AccountingStorageBackupHost = %s\n", 
		slurm_ctl_conf_ptr->accounting_storage_host);
	fprintf(out, "AccountingStorageHost   = %s\n", 
		slurm_ctl_conf_ptr->accounting_storage_host);
	fprintf(out, "AccountingStorageLoc    = %s\n", 
		slurm_ctl_conf_ptr->accounting_storage_loc);
	fprintf(out, "AccountingStoragePass   = %s\n", 
		slurm_ctl_conf_ptr->accounting_storage_pass);
	fprintf(out, "AccountingStoragePort   = %u\n", 
		slurm_ctl_conf_ptr->accounting_storage_port);
	fprintf(out, "AccountingStorageType   = %s\n", 
		slurm_ctl_conf_ptr->accounting_storage_type);
	fprintf(out, "AccountingStorageUser   = %s\n", 
		slurm_ctl_conf_ptr->accounting_storage_user);
	fprintf(out, "AuthType                = %s\n", 
		slurm_ctl_conf_ptr->authtype);
	fprintf(out, "BackupAddr              = %s\n", 
		slurm_ctl_conf_ptr->backup_addr);
	fprintf(out, "BackupController        = %s\n", 
		slurm_ctl_conf_ptr->backup_controller);
	fprintf(out, "BatchStartTime          = %u sec\n", 
		slurm_ctl_conf_ptr->batch_start_timeout);
	slurm_make_time_str ((time_t *)&slurm_ctl_conf_ptr->boot_time,
			     time_str, sizeof(time_str));
	fprintf(out, "BOOT_TIME               = %s\n",
		time_str);
	fprintf(out, "CacheGroups             = %u\n", 
		slurm_ctl_conf_ptr->cache_groups);
	fprintf(out, "CheckpointType          = %s\n",
		slurm_ctl_conf_ptr->checkpoint_type);
	fprintf(out, "ClusterName             = %s\n",
		slurm_ctl_conf_ptr->cluster_name);
	fprintf(out, "CompleteWait            = %u sec\n", 
		slurm_ctl_conf_ptr->complete_wait);
	fprintf(out, "ControlAddr             = %s\n", 
		slurm_ctl_conf_ptr->control_addr);
	fprintf(out, "ControlMachine          = %s\n", 
		slurm_ctl_conf_ptr->control_machine);
	fprintf(out, "CryptoType              = %s\n",
		slurm_ctl_conf_ptr->crypto_type);

	xbuf = debug_flags2str(slurm_ctl_conf_ptr->debug_flags);
	fprintf(out, "DebugFlags              = %s\n", xbuf);
	xfree(xbuf);

	if (slurm_ctl_conf_ptr->def_mem_per_task & MEM_PER_CPU) {
		fprintf(out, "DefMemPerCPU            = %u MB\n",
			slurm_ctl_conf_ptr->def_mem_per_task &
			(~MEM_PER_CPU));
	} else if (slurm_ctl_conf_ptr->def_mem_per_task) {
		fprintf(out, "DefMemPerNode           = %u MB\n",
			slurm_ctl_conf_ptr->def_mem_per_task);
	} else
		fprintf(out, "DefMemPerCPU            = UNLIMITED\n");

	if (slurm_ctl_conf_ptr->disable_root_jobs)
		fprintf(out, "DisableRootJobs         = YES\n");
	else
		fprintf(out, "DisableRootJobs         = NO\n");

	if (slurm_ctl_conf_ptr->enforce_part_limits)
		fprintf(out, "EnforcePartLimits       = YES\n");
	else
		fprintf(out, "EnforcePartLimits       = NO\n");
	fprintf(out, "Epilog                  = %s\n",
		slurm_ctl_conf_ptr->epilog);
	fprintf(out, "EpilogMsgTime           = %u usec\n",
		slurm_ctl_conf_ptr->epilog_msg_time);
	fprintf(out, "EpilogSlurmctld         = %s\n", 
		slurm_ctl_conf_ptr->epilog_slurmctld);
	fprintf(out, "FastSchedule            = %u\n",
		slurm_ctl_conf_ptr->fast_schedule);
	fprintf(out, "FirstJobId              = %u\n",
		slurm_ctl_conf_ptr->first_job_id);
	fprintf(out, "GetEnvTimeout           = %u sec\n",
		slurm_ctl_conf_ptr->get_env_timeout);
	fprintf(out, "HealthCheckInterval     = %u sec\n",
		slurm_ctl_conf_ptr->health_check_interval);
	fprintf(out, "HealthCheckProgram      = %s\n",
		slurm_ctl_conf_ptr->health_check_program);
#ifdef HAVE_XCPU
	fprintf(out, "HAVE_XCPU               = %d\n", HAVE_XCPU);
#endif
	fprintf(out, "InactiveLimit           = %u sec\n",
		slurm_ctl_conf_ptr->inactive_limit);
	fprintf(out, "JobAcctGatherFrequency  = %u sec\n",
		slurm_ctl_conf_ptr->job_acct_gather_freq);
	fprintf(out, "JobAcctGatherType       = %s\n",
		slurm_ctl_conf_ptr->job_acct_gather_type);
	fprintf(out, "JobCheckpointDir        = %s\n",
		slurm_ctl_conf_ptr->job_ckpt_dir);
	fprintf(out, "JobCompHost             = %s\n",
		slurm_ctl_conf_ptr->job_comp_host);
	fprintf(out, "JobCompLoc              = %s\n",
		 slurm_ctl_conf_ptr->job_comp_loc);
	fprintf(out, "JobCompPass             = %s\n",
		 slurm_ctl_conf_ptr->job_comp_pass);
	fprintf(out, "JobCompPort             = %u\n",
		slurm_ctl_conf_ptr->job_comp_port);
	fprintf(out, "JobCompType             = %s\n", 
		slurm_ctl_conf_ptr->job_comp_type);
	fprintf(out, "JobCompUser             = %s\n", 
		slurm_ctl_conf_ptr->job_comp_user);
	fprintf(out, "JobCredentialPrivateKey = %s\n", 
		slurm_ctl_conf_ptr->job_credential_private_key);
	fprintf(out, "JobCredentialPublicCertificate = %s\n", 
		slurm_ctl_conf_ptr->job_credential_public_certificate);
	fprintf(out, "JobFileAppend           = %u\n",
		slurm_ctl_conf_ptr->job_file_append);
	fprintf(out, "JobRequeue              = %u\n",
		slurm_ctl_conf_ptr->job_requeue);
	fprintf(out, "KillOnBadExit           = %u\n", 
		slurm_ctl_conf_ptr->kill_on_bad_exit);
	fprintf(out, "KillWait                = %u sec\n", 
		slurm_ctl_conf_ptr->kill_wait);
	fprintf(out, "Licenses                = %s\n",
		slurm_ctl_conf_ptr->licenses);
	fprintf(out, "MailProg                = %s\n",
		slurm_ctl_conf_ptr->mail_prog);
	fprintf(out, "MaxJobCount             = %u\n", 
		slurm_ctl_conf_ptr->max_job_cnt);
	if (slurm_ctl_conf_ptr->max_mem_per_task & MEM_PER_CPU) {
		fprintf(out, "MaxMemPerCPU            = %u MB\n",
			slurm_ctl_conf_ptr->max_mem_per_task &
			(~MEM_PER_CPU));
	} else if (slurm_ctl_conf_ptr->max_mem_per_task) {
		fprintf(out, "MaxMemPerNode           = %u MB\n",
			slurm_ctl_conf_ptr->max_mem_per_task);
	} else
		fprintf(out, "MaxMemPerCPU            = UNLIMITED\n");
	fprintf(out, "MessageTimeout          = %u sec\n",
		slurm_ctl_conf_ptr->msg_timeout);
	fprintf(out, "MinJobAge               = %u sec\n", 
		slurm_ctl_conf_ptr->min_job_age);
	fprintf(out, "MpiDefault              = %s\n",
		slurm_ctl_conf_ptr->mpi_default);
	fprintf(out, "MpiParams               = %s\n",
		slurm_ctl_conf_ptr->mpi_params);
#ifdef MULTIPLE_SLURMD
	fprintf(out, "MULTIPLE_SLURMD         = %d\n", MULTIPLE_SLURMD);
#endif
	fprintf(out, "NEXT_JOB_ID             = %u\n",
		slurm_ctl_conf_ptr->next_job_id);
	if (slurm_ctl_conf_ptr->over_time_limit == (uint16_t) INFINITE)
		fprintf(out, "OverTimeLimit           = UNLIMITED\n");
	else {
		fprintf(out, "OverTimeLimit           = %u min\n",
			slurm_ctl_conf_ptr->over_time_limit);
	}
	fprintf(out, "PluginDir               = %s\n", 
		slurm_ctl_conf_ptr->plugindir);
	fprintf(out, "PlugStackConfig         = %s\n",
		slurm_ctl_conf_ptr->plugstack);

	if (strcmp(slurm_ctl_conf_ptr->priority_type, "priority/basic") == 0) {
		fprintf(out, "PriorityType            = %s\n",
			slurm_ctl_conf_ptr->priority_type);
	} else {
		secs2time_str((time_t) slurm_ctl_conf_ptr->priority_decay_hl,
			      tmp_str, sizeof(tmp_str));
		fprintf(out, "PriorityDecayHalfLife   = %s\n", tmp_str);
		fprintf(out, "PriorityFavorSmall      = %u\n",
			slurm_ctl_conf_ptr->priority_favor_small);
		secs2time_str((time_t) slurm_ctl_conf_ptr->priority_max_age,
			      tmp_str, sizeof(tmp_str));
		fprintf(out, "PriorityMaxAge          = %s\n", tmp_str);
		fprintf(out, "PriorityUsageResetPeriod = %s\n", 
			_reset_period_str(slurm_ctl_conf_ptr->
					  priority_reset_period));
		fprintf(out, "PriorityType            = %s\n",
			slurm_ctl_conf_ptr->priority_type);
		fprintf(out, "PriorityWeightAge       = %u\n",
			slurm_ctl_conf_ptr->priority_weight_age);
		fprintf(out, "PriorityWeightFairShare = %u\n",
			slurm_ctl_conf_ptr->priority_weight_fs);
		fprintf(out, "PriorityWeightJobSize   = %u\n",
			slurm_ctl_conf_ptr->priority_weight_js);
		fprintf(out, "PriorityWeightPartition = %u\n",
			slurm_ctl_conf_ptr->priority_weight_part);
		fprintf(out, "PriorityWeightQOS       = %u\n",
			slurm_ctl_conf_ptr->priority_weight_qos);
	}

	private_data_string(slurm_ctl_conf_ptr->private_data,
			    tmp_str, sizeof(tmp_str));
	fprintf(out, "PrivateData             = %s\n", tmp_str);
	fprintf(out, "ProctrackType           = %s\n",
		slurm_ctl_conf_ptr->proctrack_type);
	fprintf(out, "Prolog                  = %s\n", 
		slurm_ctl_conf_ptr->prolog);
	fprintf(out, "PrologSlurmctld         = %s\n", 
		slurm_ctl_conf_ptr->prolog_slurmctld);
	fprintf(out, "PropagatePrioProcess    = %u\n",
		slurm_ctl_conf_ptr->propagate_prio_process);
        fprintf(out, "PropagateResourceLimits = %s\n",
                slurm_ctl_conf_ptr->propagate_rlimits);
        fprintf(out, "PropagateResourceLimitsExcept = %s\n", 
                slurm_ctl_conf_ptr->propagate_rlimits_except);
	fprintf(out, "ResumeProgram           = %s\n", 
		slurm_ctl_conf_ptr->resume_program);
	fprintf(out, "ResumeRate              = %u nodes/min\n", 
		slurm_ctl_conf_ptr->resume_rate);
	fprintf(out, "ResumeTimeout           = %u sec\n", 
		slurm_ctl_conf_ptr->resume_timeout);
	if (slurm_ctl_conf_ptr->resv_over_run == (uint16_t) INFINITE)
		fprintf(out, "ResvOverRun             = UNLIMITED\n");
	else {
		fprintf(out, "ResvOverRun             = %u min\n",
			slurm_ctl_conf_ptr->resv_over_run);
	}
	fprintf(out, "ReturnToService         = %u\n", 
		slurm_ctl_conf_ptr->ret2service);
	if (slurm_ctl_conf_ptr->salloc_default_command) {
		fprintf(out, "SallocDefaultCommand    = \"%s\"\n",
			slurm_ctl_conf_ptr->salloc_default_command);
	} else {
		fprintf(out, "SallocDefaultCommand    = %s\n",
			slurm_ctl_conf_ptr->salloc_default_command);
	}
	fprintf(out, "SchedulerParameters     = %s\n",
		slurm_ctl_conf_ptr->sched_params);
	fprintf(out, "SchedulerPort           = %u\n",
		slurm_ctl_conf_ptr->schedport);
	fprintf(out, "SchedulerRootFilter     = %u\n",
		slurm_ctl_conf_ptr->schedrootfltr);
	fprintf(out, "SchedulerTimeSlice      = %u sec\n",
		slurm_ctl_conf_ptr->sched_time_slice);
	fprintf(out, "SchedulerType           = %s\n",
		slurm_ctl_conf_ptr->schedtype);
	fprintf(out, "SelectType              = %s\n",
		slurm_ctl_conf_ptr->select_type);
	if (slurm_ctl_conf_ptr->select_type_param) {
		fprintf(out, "SelectTypeParameters    = %s\n",
			_select_info(slurm_ctl_conf_ptr->
			select_type_param));
	}
	fprintf(out, "SlurmUser               = %s(%u)\n", 
		slurm_ctl_conf_ptr->slurm_user_name,
		slurm_ctl_conf_ptr->slurm_user_id);
	fprintf(out, "SlurmctldDebug          = %u\n", 
		slurm_ctl_conf_ptr->slurmctld_debug);
	fprintf(out, "SlurmctldLogFile        = %s\n", 
		slurm_ctl_conf_ptr->slurmctld_logfile);
	fprintf(out, "SlurmctldPidFile        = %s\n", 
		slurm_ctl_conf_ptr->slurmctld_pidfile);
	fprintf(out, "SlurmctldPort           = %u\n", 
		slurm_ctl_conf_ptr->slurmctld_port);
	fprintf(out, "SlurmctldTimeout        = %u sec\n", 
		slurm_ctl_conf_ptr->slurmctld_timeout);
	fprintf(out, "SlurmdDebug             = %u\n", 
		slurm_ctl_conf_ptr->slurmd_debug);
	fprintf(out, "SlurmdLogFile           = %s\n", 
		slurm_ctl_conf_ptr->slurmd_logfile);
	fprintf(out, "SlurmdPidFile           = %s\n", 
		slurm_ctl_conf_ptr->slurmd_pidfile);
#ifndef MULTIPLE_SLURMD
	fprintf(out, "SlurmdPort              = %u\n", 
		slurm_ctl_conf_ptr->slurmd_port);
#endif
	fprintf(out, "SlurmdSpoolDir          = %s\n", 
		slurm_ctl_conf_ptr->slurmd_spooldir);
	fprintf(out, "SlurmdTimeout           = %u sec\n", 
		slurm_ctl_conf_ptr->slurmd_timeout);
	fprintf(out, "SlurmdUser              = %s(%u)\n", 
		slurm_ctl_conf_ptr->slurmd_user_name,
		slurm_ctl_conf_ptr->slurmd_user_id);
	fprintf(out, "SLURM_CONF              = %s\n", 
		slurm_ctl_conf_ptr->slurm_conf);
	fprintf(out, "SLURM_VERSION           = %s\n", SLURM_VERSION);
	fprintf(out, "SrunEpilog              = %s\n",
		slurm_ctl_conf_ptr->srun_epilog);
	fprintf(out, "SrunIOTimeout           = %u sec\n", 
		slurm_ctl_conf_ptr->srun_io_timeout);
	fprintf(out, "SrunProlog              = %s\n",
		slurm_ctl_conf_ptr->srun_prolog);
	fprintf(out, "StateSaveLocation       = %s\n", 
		slurm_ctl_conf_ptr->state_save_location);
	fprintf(out, "SuspendExcNodes         = %s\n", 
		slurm_ctl_conf_ptr->suspend_exc_nodes);
	fprintf(out, "SuspendExcParts         = %s\n", 
		slurm_ctl_conf_ptr->suspend_exc_parts);
	fprintf(out, "SuspendProgram          = %s\n", 
		slurm_ctl_conf_ptr->suspend_program);
	fprintf(out, "SuspendRate             = %u\n", 
		slurm_ctl_conf_ptr->suspend_rate);
	if (slurm_ctl_conf_ptr->suspend_time == 0) {
		fprintf(out, "SuspendTime             = NONE\n");
	} else {
		fprintf(out, "SuspendTime             = %d sec\n", 
			((int)slurm_ctl_conf_ptr->suspend_time - 1));
	}
	fprintf(out, "SuspendTimeout          = %u sec\n", 
		slurm_ctl_conf_ptr->suspend_timeout);
	fprintf(out, "SwitchType              = %s\n",
		slurm_ctl_conf_ptr->switch_type);
	fprintf(out, "TaskEpilog              = %s\n",
		slurm_ctl_conf_ptr->task_epilog);
	fprintf(out, "TaskPlugin              = %s\n",
		 slurm_ctl_conf_ptr->task_plugin);
	slurm_sprint_cpu_bind_type(tmp_str, 
				   slurm_ctl_conf_ptr->task_plugin_param);
	fprintf(out, "TaskPluginParam         = %s\n", tmp_str);
	fprintf(out, "TaskProlog              = %s\n",
		slurm_ctl_conf_ptr->task_prolog);
	fprintf(out, "TmpFS                   = %s\n", 
		slurm_ctl_conf_ptr->tmp_fs);
	fprintf(out, "TopologyPlugin          = %s\n",
		 slurm_ctl_conf_ptr->topology_plugin);
	fprintf(out, "TrackWCKey              = %u\n",
		slurm_ctl_conf_ptr->track_wckey);
	fprintf(out, "TreeWidth               = %u\n",
		slurm_ctl_conf_ptr->tree_width);
	fprintf(out, "UsePam                  = %u\n",
		slurm_ctl_conf_ptr->use_pam);
	fprintf(out, "UnkillableStepProgram   = %s\n",
		slurm_ctl_conf_ptr->unkillable_program);
	fprintf(out, "UnkillableStepTimeout   = %u sec\n",
		slurm_ctl_conf_ptr->unkillable_timeout);
	fprintf(out, "WaitTime                = %u sec\n", 
		slurm_ctl_conf_ptr->wait_time);

	slurm_print_key_pairs(out, slurm_ctl_conf_ptr->select_conf_key_pairs,
			      select_title);
}

/*
 * slurm_load_ctl_conf - issue RPC to get slurm control configuration  
 *	information if changed since update_time 
 * IN update_time - time of current configuration data
 * IN slurm_ctl_conf_ptr - place to store slurm control configuration 
 *	pointer
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 * NOTE: free the response using slurm_free_ctl_conf
 */
int
slurm_load_ctl_conf (time_t update_time, slurm_ctl_conf_t **confp)
{
	int rc;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;
        last_update_msg_t req; 
	
	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	req.last_update  = update_time;
	req_msg.msg_type = REQUEST_BUILD_INFO;
	req_msg.data     = &req;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg) < 0) 
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_BUILD_INFO:
		*confp = (slurm_ctl_conf_info_msg_t *) resp_msg.data;
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);	
		if (rc) 
			slurm_seterrno_ret(rc);
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}
        return SLURM_PROTOCOL_SUCCESS;
}

/*
 * slurm_load_slurmd_status - issue RPC to get the status of slurmd 
 *	daemon on this machine
 * IN slurmd_info_ptr - place to store slurmd status information
 * RET 0 or -1 on error
 * NOTE: free the response using slurm_free_slurmd_status()
 */
extern int
slurm_load_slurmd_status(slurmd_status_t **slurmd_status_ptr)
{
	int rc;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;
	char this_host[256], *this_addr;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	/*
	 *  Set request message address to slurmd on localhost
	 */
	gethostname_short(this_host, sizeof(this_host));
	this_addr = slurm_conf_get_nodeaddr(this_host);
	if (this_addr == NULL)
		this_addr = xstrdup("localhost");
	slurm_set_addr(&req_msg.address, (uint16_t)slurm_get_slurmd_port(), 
		       this_addr);
	xfree(this_addr);

	req_msg.msg_type = REQUEST_DAEMON_STATUS;
	req_msg.data     = NULL;
	
	rc = slurm_send_recv_node_msg(&req_msg, &resp_msg, 0);

	if ((rc != 0) || !resp_msg.auth_cred) {
		error("slurm_slurmd_info: %m");
		if (resp_msg.auth_cred)
			g_slurm_auth_destroy(resp_msg.auth_cred);
		return SLURM_ERROR;
	}
	if (resp_msg.auth_cred)
		g_slurm_auth_destroy(resp_msg.auth_cred);

	switch (resp_msg.msg_type) {
	case RESPONSE_SLURMD_STATUS:
		*slurmd_status_ptr = (slurmd_status_t *) resp_msg.data;
		break;
	case RESPONSE_SLURM_RC:
	        rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);	
		if (rc) 
			slurm_seterrno_ret(rc);
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_PROTOCOL_SUCCESS;
}

/*
 * slurm_print_slurmd_status - output the contents of slurmd status 
 *	message as loaded using slurm_load_slurmd_status
 * IN out - file to write to
 * IN slurmd_status_ptr - slurmd status pointer
 */
void slurm_print_slurmd_status (FILE* out, 
				slurmd_status_t * slurmd_status_ptr)
{
	char time_str[32];

	if (slurmd_status_ptr == NULL )
		return ;

	fprintf(out, "Active Steps             = %s\n",
		slurmd_status_ptr->step_list);

	fprintf(out, "Actual CPUs              = %u\n",
		slurmd_status_ptr->actual_cpus);
	fprintf(out, "Actual sockets           = %u\n",
		slurmd_status_ptr->actual_sockets);
	fprintf(out, "Actual cores             = %u\n",
		slurmd_status_ptr->actual_cores);
	fprintf(out, "Actual threads per core  = %u\n",
		slurmd_status_ptr->actual_threads);
	fprintf(out, "Actual real memory       = %u MB\n",
		slurmd_status_ptr->actual_real_mem);
	fprintf(out, "Actual temp disk space   = %u MB\n",
		slurmd_status_ptr->actual_tmp_disk);

	slurm_make_time_str ((time_t *)&slurmd_status_ptr->booted, 
			     time_str, sizeof(time_str));
	fprintf(out, "Boot time                = %s\n", time_str);

	fprintf(out, "Hostname                 = %s\n",
		slurmd_status_ptr->hostname);

	if (slurmd_status_ptr->last_slurmctld_msg) {
		slurm_make_time_str ((time_t *)
				&slurmd_status_ptr->last_slurmctld_msg, 
				time_str, sizeof(time_str));
		fprintf(out, "Last slurmctld msg time  = %s\n", time_str);
	} else 
		fprintf(out, "Last slurmctld msg time  = NONE\n");

	fprintf(out, "Slurmd PID               = %u\n",
		slurmd_status_ptr->pid);
	fprintf(out, "Slurmd Debug             = %u\n",
		slurmd_status_ptr->slurmd_debug);
	fprintf(out, "Slurmd Logfile           = %s\n",
		slurmd_status_ptr->slurmd_logfile);
	fprintf(out, "Version                  = %s\n",
		slurmd_status_ptr->version);
	return;
}

extern void slurm_print_key_pairs(FILE* out, void *key_pairs, char *title)
{
	List config_list = (List)key_pairs;
	ListIterator iter = NULL;
	config_key_pair_t *key_pair;

	if (!config_list)
		return;
	
	fprintf(out, "\n%s:\n", title);
	iter = list_iterator_create(config_list);
	while((key_pair = list_next(iter))) {
		fprintf(out, "%-23s = %s\n", key_pair->name, key_pair->value);
	}
	list_iterator_destroy(iter);
}
