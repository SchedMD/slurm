/****************************************************************************\
 *  config_info.c - get/print the system configuration information of slurm
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010 SchedMD <http://www.schedmd.com>.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> and Kevin Tew <tew1@llnl.gov>.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "slurm/slurm.h"

#include "src/common/parse_time.h"
#include "src/common/read_config.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_resource_info.h"
#include "src/common/slurm_selecttype_info.h"
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
	char time_str[32], tmp_str[128];
	void *ret_list = NULL;
	char *select_title = "Select Plugin Configuration";
	uint32_t cluster_flags = slurmdb_setup_cluster_flags();

	if (cluster_flags & CLUSTER_FLAG_BGL)
		select_title = "\nBluegene/L configuration\n";
	else if (cluster_flags & CLUSTER_FLAG_BGP)
		select_title = "\nBluegene/P configuration\n";
	else if (cluster_flags & CLUSTER_FLAG_BGQ)
		select_title = "\nBluegene/Q configuration\n";
	else if (cluster_flags & CLUSTER_FLAG_CRAY)
		select_title = "\nCray configuration\n";

	if ( slurm_ctl_conf_ptr == NULL )
		return ;

	slurm_make_time_str((time_t *)&slurm_ctl_conf_ptr->last_update,
			     time_str, sizeof(time_str));
	snprintf(tmp_str, sizeof(tmp_str), "Configuration data as of %s\n",
		 time_str);

	ret_list = slurm_ctl_conf_2_key_pairs(slurm_ctl_conf_ptr);
	if (ret_list) {
		slurm_print_key_pairs(out, ret_list, tmp_str);

		list_destroy((List)ret_list);
	}

	slurm_print_key_pairs(out, slurm_ctl_conf_ptr->acct_gather_conf,
			      "\nAccount Gather\n");

	slurm_print_key_pairs(out, slurm_ctl_conf_ptr->ext_sensors_conf,
			      "\nExternal Sensors\n");

	slurm_print_key_pairs(out, slurm_ctl_conf_ptr->select_conf_key_pairs,
			      select_title);

}

extern void *slurm_ctl_conf_2_key_pairs (slurm_ctl_conf_t* slurm_ctl_conf_ptr)
{
	List ret_list = NULL;
	config_key_pair_t *key_pair;
	char tmp_str[128];
	uint32_t cluster_flags = slurmdb_setup_cluster_flags();

	if ( slurm_ctl_conf_ptr == NULL )
		return NULL;

	ret_list = list_create(destroy_config_key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("AccountingStorageBackupHost");
	key_pair->value =
		xstrdup(slurm_ctl_conf_ptr->accounting_storage_backup_host);
	list_append(ret_list, key_pair);

	accounting_enforce_string(slurm_ctl_conf_ptr->
				  accounting_storage_enforce,
				  tmp_str, sizeof(tmp_str));
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("AccountingStorageEnforce");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("AccountingStorageHost");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->accounting_storage_host);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("AccountingStorageLoc");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->accounting_storage_loc);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u",
		 slurm_ctl_conf_ptr->accounting_storage_port);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("AccountingStoragePort");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("AccountingStorageType");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->accounting_storage_type);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("AccountingStorageUser");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->accounting_storage_user);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("AccountingStoreJobComment");
	if (slurm_ctl_conf_ptr->acctng_store_job_comment)
		key_pair->value = xstrdup("YES");
	else
		key_pair->value = xstrdup("NO");
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("AcctGatherEnergyType");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->acct_gather_energy_type);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("AcctGatherFilesystemType");
	key_pair->value =
		xstrdup(slurm_ctl_conf_ptr->acct_gather_filesystem_type);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("AcctGatherInfinibandType");
	key_pair->value =
		xstrdup(slurm_ctl_conf_ptr->acct_gather_infiniband_type);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u sec",
		 slurm_ctl_conf_ptr->acct_gather_node_freq);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("AcctGatherNodeFreq");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("AcctGatherProfileType");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->acct_gather_profile_type);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("AuthInfo");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->authinfo);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("AuthType");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->authtype);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("BackupAddr");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->backup_addr);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("BackupController");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->backup_controller);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u sec",
		 slurm_ctl_conf_ptr->batch_start_timeout);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("BatchStartTimeout");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	slurm_make_time_str((time_t *)&slurm_ctl_conf_ptr->boot_time,
			    tmp_str, sizeof(tmp_str));
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("BOOT_TIME");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("CacheGroups");
	if (slurm_ctl_conf_ptr->group_info & GROUP_CACHE)
		key_pair->value = xstrdup("1");
	else
		key_pair->value = xstrdup("0");
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("CheckpointType");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->checkpoint_type);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ClusterName");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->cluster_name);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u sec",
		 slurm_ctl_conf_ptr->complete_wait);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("CompleteWait");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ControlAddr");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->control_addr);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ControlMachine");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->control_machine);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("CoreSpecPlugin");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->core_spec_plugin);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("CryptoType");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->crypto_type);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("DebugFlags");
	key_pair->value = debug_flags2str(slurm_ctl_conf_ptr->debug_flags);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	list_append(ret_list, key_pair);
	if (slurm_ctl_conf_ptr->def_mem_per_cpu & MEM_PER_CPU) {
		key_pair->name = xstrdup("DefMemPerCPU");
		snprintf(tmp_str, sizeof(tmp_str), "%u",
			 slurm_ctl_conf_ptr->def_mem_per_cpu &
			 (~MEM_PER_CPU));
		key_pair->value = xstrdup(tmp_str);
	} else if (slurm_ctl_conf_ptr->def_mem_per_cpu) {
		key_pair->name = xstrdup("DefMemPerNode");
		snprintf(tmp_str, sizeof(tmp_str), "%u",
			 slurm_ctl_conf_ptr->def_mem_per_cpu);
		key_pair->value = xstrdup(tmp_str);
	} else {
		key_pair->name = xstrdup("DefMemPerNode");
		key_pair->value = xstrdup("UNLIMITED");
	}

	key_pair = xmalloc(sizeof(config_key_pair_t));
	list_append(ret_list, key_pair);
	key_pair->name = xstrdup("DisableRootJobs");
	if (slurm_ctl_conf_ptr->disable_root_jobs)
		key_pair->value = xstrdup("YES");
	else
		key_pair->value = xstrdup("NO");

	snprintf(tmp_str, sizeof(tmp_str), "%u",
		 slurm_ctl_conf_ptr->dynalloc_port);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("DynAllocPort");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	list_append(ret_list, key_pair);
	key_pair->name = xstrdup("EnforcePartLimits");
	if (slurm_ctl_conf_ptr->enforce_part_limits)
		key_pair->value = xstrdup("YES");
	else
		key_pair->value = xstrdup("NO");

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("Epilog");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->epilog);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u usec",
		 slurm_ctl_conf_ptr->epilog_msg_time);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("EpilogMsgTime");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("EpilogSlurmctld");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->epilog_slurmctld);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ExtSensorsType");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->ext_sensors_type);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u sec",
		 slurm_ctl_conf_ptr->ext_sensors_freq);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ExtSensorsFreq");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	if (strcmp(slurm_ctl_conf_ptr->priority_type, "priority/basic")) {
		snprintf(tmp_str, sizeof(tmp_str), "%u",
			 slurm_ctl_conf_ptr->fs_dampening_factor);
		key_pair = xmalloc(sizeof(config_key_pair_t));
		key_pair->name = xstrdup("FairShareDampeningFactor");
		key_pair->value = xstrdup(tmp_str);
		list_append(ret_list, key_pair);
	}

	snprintf(tmp_str, sizeof(tmp_str), "%u",
		 slurm_ctl_conf_ptr->fast_schedule);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("FastSchedule");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u",
		 slurm_ctl_conf_ptr->first_job_id);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("FirstJobId");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u sec",
		 slurm_ctl_conf_ptr->get_env_timeout);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("GetEnvTimeout");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);


	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("GresTypes");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->gres_plugins);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("GroupUpdateForce");
	if (slurm_ctl_conf_ptr->group_info & GROUP_FORCE)
		key_pair->value = xstrdup("1");
	else
		key_pair->value = xstrdup("0");
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u sec",
		 slurm_ctl_conf_ptr->group_info & GROUP_TIME_MASK);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("GroupUpdateTime");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	if (slurm_ctl_conf_ptr->hash_val != NO_VAL) {
		if (slurm_ctl_conf_ptr->hash_val == slurm_get_hash_val())
			snprintf(tmp_str, sizeof(tmp_str), "Match");
		else {
			snprintf(tmp_str, sizeof(tmp_str),
				 "Different Ours=0x%x Slurmctld=0x%x",
				 slurm_get_hash_val(),
				 slurm_ctl_conf_ptr->hash_val);
		}
		key_pair = xmalloc(sizeof(config_key_pair_t));
		key_pair->name = xstrdup("HASH_VAL");
		key_pair->value = xstrdup(tmp_str);
		list_append(ret_list, key_pair);
	}

	snprintf(tmp_str, sizeof(tmp_str), "%u sec",
		 slurm_ctl_conf_ptr->health_check_interval);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("HealthCheckInterval");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("HealthCheckNodeState");
	key_pair->value = health_check_node_state_str(slurm_ctl_conf_ptr->
						      health_check_node_state);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("HealthCheckProgram");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->health_check_program);
	list_append(ret_list, key_pair);

	if (cluster_flags & CLUSTER_FLAG_XCPU) {
		key_pair = xmalloc(sizeof(config_key_pair_t));
		key_pair->name = xstrdup("HAVE_XCPU");
		key_pair->value = xstrdup("1");
		list_append(ret_list, key_pair);
	}

	snprintf(tmp_str, sizeof(tmp_str), "%u sec",
		 slurm_ctl_conf_ptr->inactive_limit);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("InactiveLimit");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("JobAcctGatherFrequency");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->job_acct_gather_freq);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("JobAcctGatherType");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->job_acct_gather_type);
	list_append(ret_list, key_pair);

 	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("JobAcctGatherParams");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->job_acct_gather_params);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("JobCheckpointDir");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->job_ckpt_dir);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("JobCompHost");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->job_comp_host);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("JobCompLoc");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->job_comp_loc);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u",
		 slurm_ctl_conf_ptr->job_comp_port);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("JobCompPort");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("JobCompType");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->job_comp_type);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("JobCompUser");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->job_comp_user);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("JobContainerPlugin");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->job_container_plugin);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("JobCredentialPrivateKey");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->
				  job_credential_private_key);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("JobCredentialPublicCertificate");
	key_pair->value = xstrdup(
		slurm_ctl_conf_ptr->job_credential_public_certificate);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u",
		 slurm_ctl_conf_ptr->job_file_append);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("JobFileAppend");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u",
		 slurm_ctl_conf_ptr->job_requeue);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("JobRequeue");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("JobSubmitPlugins");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->job_submit_plugins);
	list_append(ret_list, key_pair);

	if (slurm_ctl_conf_ptr->keep_alive_time == (uint16_t) NO_VAL)
		snprintf(tmp_str, sizeof(tmp_str), "SYSTEM_DEFAULT");
	else {
		snprintf(tmp_str, sizeof(tmp_str), "%u sec",
			 slurm_ctl_conf_ptr->keep_alive_time);
	}
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("KeepAliveTime");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u",
		 slurm_ctl_conf_ptr->kill_on_bad_exit);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("KillOnBadExit");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u sec",
		 slurm_ctl_conf_ptr->kill_wait);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("KillWait");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("LaunchType");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->launch_type);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("Licenses");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->licenses);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("LicensesUsed");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->licenses_used);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MailProg");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->mail_prog);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u",
		 slurm_ctl_conf_ptr->max_array_sz);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MaxArraySize");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u",
		 slurm_ctl_conf_ptr->max_job_cnt);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MaxJobCount");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u",
		 slurm_ctl_conf_ptr->max_job_id);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MaxJobId");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	list_append(ret_list, key_pair);
	if (slurm_ctl_conf_ptr->max_mem_per_cpu & MEM_PER_CPU) {
		key_pair->name = xstrdup("MaxMemPerCPU");
		snprintf(tmp_str, sizeof(tmp_str), "%u",
			 slurm_ctl_conf_ptr->max_mem_per_cpu & (~MEM_PER_CPU));
		key_pair->value = xstrdup(tmp_str);

	} else if (slurm_ctl_conf_ptr->max_mem_per_cpu) {
		key_pair->name = xstrdup("MaxMemPerNode");
		snprintf(tmp_str, sizeof(tmp_str), "%u",
			 slurm_ctl_conf_ptr->max_mem_per_cpu);
		key_pair->value = xstrdup(tmp_str);
	} else {
		key_pair->name = xstrdup("MaxMemPerNode");
		key_pair->value = xstrdup("UNLIMITED");
	}

	snprintf(tmp_str, sizeof(tmp_str), "%u",
		 slurm_ctl_conf_ptr->max_step_cnt);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MaxStepCount");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u",
		 slurm_ctl_conf_ptr->max_tasks_per_node);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MaxTasksPerNode");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u sec",
		 slurm_ctl_conf_ptr->msg_timeout);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MessageTimeout");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u sec",
		 slurm_ctl_conf_ptr->min_job_age);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MinJobAge");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MpiDefault");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->mpi_default);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MpiParams");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->mpi_params);
	list_append(ret_list, key_pair);

	if (cluster_flags & CLUSTER_FLAG_MULTSD) {
		key_pair = xmalloc(sizeof(config_key_pair_t));
		key_pair->name = xstrdup("MULTIPLE_SLURMD");
		key_pair->value = xstrdup("1");
		list_append(ret_list, key_pair);
	}

	snprintf(tmp_str, sizeof(tmp_str), "%u",
		 slurm_ctl_conf_ptr->next_job_id);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("NEXT_JOB_ID");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	if (slurm_ctl_conf_ptr->over_time_limit == (uint16_t) INFINITE)
		snprintf(tmp_str, sizeof(tmp_str), "UNLIMITED");
	else
		snprintf(tmp_str, sizeof(tmp_str), "%u min",
			 slurm_ctl_conf_ptr->over_time_limit);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("OverTimeLimit");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("PluginDir");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->plugindir);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("PlugStackConfig");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->plugstack);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("PreemptMode");
	key_pair->value = xstrdup(preempt_mode_string(slurm_ctl_conf_ptr->
						      preempt_mode));
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("PreemptType");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->preempt_type);
	list_append(ret_list, key_pair);

	if (strcmp(slurm_ctl_conf_ptr->priority_type, "priority/basic") == 0) {
		key_pair = xmalloc(sizeof(config_key_pair_t));
		key_pair->name = xstrdup("PriorityType");
		key_pair->value = xstrdup(slurm_ctl_conf_ptr->priority_type);
		list_append(ret_list, key_pair);
	} else {
		secs2time_str((time_t) slurm_ctl_conf_ptr->priority_decay_hl,
			      tmp_str, sizeof(tmp_str));
		key_pair = xmalloc(sizeof(config_key_pair_t));
		key_pair->name = xstrdup("PriorityDecayHalfLife");
		key_pair->value = xstrdup(tmp_str);
		list_append(ret_list, key_pair);

		secs2time_str((time_t)slurm_ctl_conf_ptr->priority_calc_period,
			      tmp_str, sizeof(tmp_str));
		key_pair = xmalloc(sizeof(config_key_pair_t));
		key_pair->name = xstrdup("PriorityCalcPeriod");
		key_pair->value = xstrdup(tmp_str);
		list_append(ret_list, key_pair);

		snprintf(tmp_str, sizeof(tmp_str), "%u",
			 slurm_ctl_conf_ptr->priority_favor_small);
		key_pair = xmalloc(sizeof(config_key_pair_t));
		key_pair->name = xstrdup("PriorityFavorSmall");
		key_pair->value = xstrdup(tmp_str);
		list_append(ret_list, key_pair);

		snprintf(tmp_str, sizeof(tmp_str), "%u",
			 slurm_ctl_conf_ptr->priority_flags);
		key_pair = xmalloc(sizeof(config_key_pair_t));
		key_pair->name = xstrdup("PriorityFlags");
		key_pair->value = xstrdup(tmp_str);
		list_append(ret_list, key_pair);

		snprintf(tmp_str, sizeof(tmp_str), "%u",
			 slurm_ctl_conf_ptr->priority_levels);
		key_pair = xmalloc(sizeof(config_key_pair_t));
		key_pair->name = xstrdup("PriorityLevels");
		key_pair->value = xstrdup(tmp_str);
		list_append(ret_list, key_pair);

		secs2time_str((time_t) slurm_ctl_conf_ptr->priority_max_age,
			      tmp_str, sizeof(tmp_str));
		key_pair = xmalloc(sizeof(config_key_pair_t));
		key_pair->name = xstrdup("PriorityMaxAge");
		key_pair->value = xstrdup(tmp_str);
		list_append(ret_list, key_pair);

		key_pair = xmalloc(sizeof(config_key_pair_t));
		key_pair->name = xstrdup("PriorityUsageResetPeriod");
		key_pair->value = xstrdup(_reset_period_str(
						  slurm_ctl_conf_ptr->
						  priority_reset_period));
		list_append(ret_list, key_pair);

		key_pair = xmalloc(sizeof(config_key_pair_t));
		key_pair->name = xstrdup("PriorityType");
		key_pair->value = xstrdup(slurm_ctl_conf_ptr->priority_type);
		list_append(ret_list, key_pair);

		snprintf(tmp_str, sizeof(tmp_str), "%u",
			 slurm_ctl_conf_ptr->priority_weight_age);
		key_pair = xmalloc(sizeof(config_key_pair_t));
		key_pair->name = xstrdup("PriorityWeightAge");
		key_pair->value = xstrdup(tmp_str);
		list_append(ret_list, key_pair);

		snprintf(tmp_str, sizeof(tmp_str), "%u",
			 slurm_ctl_conf_ptr->priority_weight_fs);
		key_pair = xmalloc(sizeof(config_key_pair_t));
		key_pair->name = xstrdup("PriorityWeightFairShare");
		key_pair->value = xstrdup(tmp_str);
		list_append(ret_list, key_pair);

		snprintf(tmp_str, sizeof(tmp_str), "%u",
			 slurm_ctl_conf_ptr->priority_weight_js);
		key_pair = xmalloc(sizeof(config_key_pair_t));
		key_pair->name = xstrdup("PriorityWeightJobSize");
		key_pair->value = xstrdup(tmp_str);
		list_append(ret_list, key_pair);

		snprintf(tmp_str, sizeof(tmp_str), "%u",
			 slurm_ctl_conf_ptr->priority_weight_part);
		key_pair = xmalloc(sizeof(config_key_pair_t));
		key_pair->name = xstrdup("PriorityWeightPartition");
		key_pair->value = xstrdup(tmp_str);
		list_append(ret_list, key_pair);

		snprintf(tmp_str, sizeof(tmp_str), "%u",
			 slurm_ctl_conf_ptr->priority_weight_qos);
		key_pair = xmalloc(sizeof(config_key_pair_t));
		key_pair->name = xstrdup("PriorityWeightQOS");
		key_pair->value = xstrdup(tmp_str);
		list_append(ret_list, key_pair);
	}


	private_data_string(slurm_ctl_conf_ptr->private_data,
			    tmp_str, sizeof(tmp_str));
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("PrivateData");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ProctrackType");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->proctrack_type);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("Prolog");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->prolog);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("PrologSlurmctld");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->prolog_slurmctld);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("PrologFlags");
	key_pair->value = prolog_flags2str(slurm_ctl_conf_ptr->prolog_flags);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u",
		 slurm_ctl_conf_ptr->propagate_prio_process);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("PropagatePrioProcess");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("PropagateResourceLimits");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->propagate_rlimits);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("PropagateResourceLimitsExcept");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->
				  propagate_rlimits_except);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("RebootProgram");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->reboot_program);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ReconfigFlags");
	key_pair->value =
		reconfig_flags2str(slurm_ctl_conf_ptr->reconfig_flags);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ResumeProgram");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->resume_program);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u nodes/min",
		 slurm_ctl_conf_ptr->resume_rate);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ResumeRate");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u sec",
		 slurm_ctl_conf_ptr->resume_timeout);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ResumeTimeout");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ResvEpilog");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->resv_epilog);
	list_append(ret_list, key_pair);

	if (slurm_ctl_conf_ptr->resv_over_run == (uint16_t) INFINITE)
		snprintf(tmp_str, sizeof(tmp_str), "UNLIMITED");
	else
		snprintf(tmp_str, sizeof(tmp_str), "%u min",
			 slurm_ctl_conf_ptr->resv_over_run);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ResvOverRun");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ResvProlog");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->resv_prolog);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u",
		 slurm_ctl_conf_ptr->ret2service);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ReturnToService");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SallocDefaultCommand");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->salloc_default_command);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SchedulerParameters");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->sched_params);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u",
		 slurm_ctl_conf_ptr->schedport);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SchedulerPort");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u",
		 slurm_ctl_conf_ptr->schedrootfltr);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SchedulerRootFilter");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u sec",
		 slurm_ctl_conf_ptr->sched_time_slice);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SchedulerTimeSlice");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SchedulerType");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->schedtype);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SelectType");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->select_type);
	list_append(ret_list, key_pair);

	if (slurm_ctl_conf_ptr->select_type_param) {
		key_pair = xmalloc(sizeof(config_key_pair_t));
		key_pair->name = xstrdup("SelectTypeParameters");
		key_pair->value = xstrdup(
			select_type_param_string(slurm_ctl_conf_ptr->
						 select_type_param));
		list_append(ret_list, key_pair);
	}

	snprintf(tmp_str, sizeof(tmp_str), "%s(%u)",
		 slurm_ctl_conf_ptr->slurm_user_name,
		 slurm_ctl_conf_ptr->slurm_user_id);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SlurmUser");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%s",
		 log_num2string(slurm_ctl_conf_ptr->slurmctld_debug));
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SlurmctldDebug");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SlurmctldLogFile");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->slurmctld_logfile);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SlurmSchedLogFile");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->sched_logfile);
	list_append(ret_list, key_pair);

	if (slurm_ctl_conf_ptr->slurmctld_port_count > 1) {
		uint32_t high_port = slurm_ctl_conf_ptr->slurmctld_port;
		high_port += (slurm_ctl_conf_ptr->slurmctld_port_count - 1);
		snprintf(tmp_str, sizeof(tmp_str), "%u-%u",
			 slurm_ctl_conf_ptr->slurmctld_port, high_port);
	} else {
		snprintf(tmp_str, sizeof(tmp_str), "%u",
			 slurm_ctl_conf_ptr->slurmctld_port);
	}
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SlurmctldPort");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u sec",
		 slurm_ctl_conf_ptr->slurmctld_timeout);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SlurmctldTimeout");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%s",
		 log_num2string(slurm_ctl_conf_ptr->slurmd_debug));
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SlurmdDebug");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SlurmdLogFile");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->slurmd_logfile);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SlurmdPidFile");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->slurmd_pidfile);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SlurmdPlugstack");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->slurmd_plugstack);
	list_append(ret_list, key_pair);

#ifndef MULTIPLE_SLURMD
	snprintf(tmp_str, sizeof(tmp_str), "%u",
		 slurm_ctl_conf_ptr->slurmd_port);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SlurmdPort");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

#endif
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SlurmdSpoolDir");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->slurmd_spooldir);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u sec",
		 slurm_ctl_conf_ptr->slurmd_timeout);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SlurmdTimeout");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%s(%u)",
		 slurm_ctl_conf_ptr->slurmd_user_name,
		 slurm_ctl_conf_ptr->slurmd_user_id);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SlurmdUser");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u",
		 slurm_ctl_conf_ptr->sched_log_level);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SlurmSchedLogLevel");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SlurmctldPidFile");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->slurmctld_pidfile);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SlurmctldPlugstack");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->slurmctld_plugstack);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SLURM_CONF");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->slurm_conf);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SLURM_VERSION");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->version);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SrunEpilog");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->srun_epilog);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SrunProlog");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->srun_prolog);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("StateSaveLocation");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->state_save_location);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SuspendExcNodes");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->suspend_exc_nodes);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SuspendExcParts");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->suspend_exc_parts);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SuspendProgram");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->suspend_program);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u nodes/min",
		 slurm_ctl_conf_ptr->suspend_rate);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SuspendRate");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	if (!slurm_ctl_conf_ptr->suspend_time)
		snprintf(tmp_str, sizeof(tmp_str), "NONE");
	else
		snprintf(tmp_str, sizeof(tmp_str), "%d sec",
			 ((int)slurm_ctl_conf_ptr->suspend_time - 1));
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SuspendTime");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u sec",
		 slurm_ctl_conf_ptr->suspend_timeout);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SuspendTimeout");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SwitchType");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->switch_type);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("TaskEpilog");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->task_epilog);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("TaskPlugin");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->task_plugin);
	list_append(ret_list, key_pair);

	slurm_sprint_cpu_bind_type(tmp_str,
				   slurm_ctl_conf_ptr->task_plugin_param);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("TaskPluginParam");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("TaskProlog");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->task_prolog);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("TmpFS");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->tmp_fs);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("TopologyPlugin");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->topology_plugin);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u",
		 slurm_ctl_conf_ptr->track_wckey);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("TrackWCKey");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u",
		 slurm_ctl_conf_ptr->tree_width);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("TreeWidth");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u",
		 slurm_ctl_conf_ptr->use_pam);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("UsePam");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("UnkillableStepProgram");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->unkillable_program);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u sec",
		 slurm_ctl_conf_ptr->unkillable_timeout);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("UnkillableStepTimeout");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u percent",
		 slurm_ctl_conf_ptr->vsize_factor);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("VSizeFactor");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u sec",
		 slurm_ctl_conf_ptr->wait_time);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("WaitTime");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	return (void *)ret_list;
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
	uint32_t cluster_flags = slurmdb_setup_cluster_flags();
	char *this_addr;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	if (cluster_flags & CLUSTER_FLAG_MULTSD) {
		if ((this_addr = getenv("SLURMD_NODENAME"))) {
			slurm_conf_get_addr(this_addr, &req_msg.address);
		} else {
			this_addr = "localhost";
			slurm_set_addr(&req_msg.address,
				       (uint16_t)slurm_get_slurmd_port(),
				       this_addr);
		}
	} else {
		char this_host[256];

		/*
		 *  Set request message address to slurmd on localhost
		 */
		gethostname_short(this_host, sizeof(this_host));
		this_addr = slurm_conf_get_nodeaddr(this_host);
		if (this_addr == NULL)
			this_addr = xstrdup("localhost");
		slurm_set_addr(&req_msg.address,
			       (uint16_t)slurm_get_slurmd_port(),
			       this_addr);
		xfree(this_addr);
	}
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
	fprintf(out, "Actual Boards            = %u\n",
		slurmd_status_ptr->actual_boards);
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

	if (!config_list || !list_count(config_list))
		return;

	fprintf(out, "%s", title);
	iter = list_iterator_create(config_list);
	while((key_pair = list_next(iter))) {
		fprintf(out, "%-23s = %s\n", key_pair->name, key_pair->value);
	}
	list_iterator_destroy(iter);
}
