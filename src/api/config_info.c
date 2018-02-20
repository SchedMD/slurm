/****************************************************************************\
 *  config_info.c - get/print the system configuration information of slurm
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010 SchedMD <https://www.schedmd.com>.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> and Kevin Tew <tew1@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "slurm/slurm.h"

#include "src/common/cpu_frequency.h"
#include "src/common/list.h"
#include "src/common/parse_time.h"
#include "src/common/read_config.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_resource_info.h"
#include "src/common/slurm_selecttype_info.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/* Local functions */
static void _write_group_header(FILE* out, char * header);
static void _write_key_pairs(FILE* out, void *key_pairs);
static void _print_config_plugin_params_list(FILE* out, List l, char *title);

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
 * slurm_write_ctl_conf - write the contents of slurm control configuration
 * IN slurm_ctl_conf_ptr - slurm control configuration pointer
 * IN node_info_ptr - pointer to node table of information
 * IN part_info_ptr - pointer to partition information
 */
void slurm_write_ctl_conf ( slurm_ctl_conf_info_msg_t * slurm_ctl_conf_ptr,
			    node_info_msg_t * node_info_ptr,
			    partition_info_msg_t * part_info_ptr)
{
	int i = 0;
	char time_str[32];
	char *tmp_str = NULL;
	char *base_path = NULL;
	char *path = NULL;
	void *ret_list = NULL;
	uint16_t val, force;
	uint32_t cluster_flags = slurmdb_setup_cluster_flags();
	FILE *fp = NULL;
	partition_info_t *p = NULL;
	struct records {
	  char *rec;
	  hostlist_t hostlist;
	  struct records *next;
	} *rp = NULL;
	struct records *crp;

	if ( slurm_ctl_conf_ptr == NULL )
		return ;

	slurm_make_time_str ((time_t *)&slurm_ctl_conf_ptr->last_update,
			     time_str, sizeof(time_str));

	/* open new slurm.conf.<datetime> file for write. This file will
	 * contain the currently running slurm configuration. */

	base_path = getenv("SLURM_CONF");
	if (base_path == NULL)
		base_path = default_slurm_config_file;

	xstrfmtcat (path, "%s.%s", base_path, time_str);

	debug("Writing slurm.conf file: %s", path);

	if ( ( fp = fopen(path, "w") ) == NULL ) {
		fprintf(stderr, "Could not create file %s: %s\n", path,
			strerror(errno));
		xfree(path);
		return;
	}

	fprintf(fp,
		"########################################################\n");
	fprintf(fp,
		"#  Configuration file for SLURM - %s  #\n", time_str);
	fprintf(fp,
		"########################################################\n");
	fprintf(fp, "#\n#\n");

	ret_list = slurm_ctl_conf_2_key_pairs(slurm_ctl_conf_ptr);
	if (ret_list) {
		_write_key_pairs(fp, ret_list);
		FREE_NULL_LIST(ret_list);
	}

	_write_group_header (fp, "NODES");
	/* Write node info; first create a string (tmp_str) that contains
	 * all fields associated with a node (but do not include the node
	 * name itself). Search for duplicate tmp_str records as we process
	 * each node entry so not to have duplicates. Associate each node
	 * name that has equal tmp_str records and create a hostlist_t string
	 * for that record. */
	for (i = 0; i < node_info_ptr->record_count; i++) {
		if (node_info_ptr->node_array[i].name == NULL)
			continue;

		if (node_info_ptr->node_array[i].node_hostname != NULL &&
		   xstrcmp(node_info_ptr->node_array[i].node_hostname,
			   node_info_ptr->node_array[i].name))
			xstrfmtcat(tmp_str, " NodeHostName=%s",
				   node_info_ptr->node_array[i].node_hostname);

		if (node_info_ptr->node_array[i].node_addr != NULL &&
		   xstrcmp(node_info_ptr->node_array[i].node_addr,
			   node_info_ptr->node_array[i].name))
		                xstrfmtcat(tmp_str, " NodeAddr=%s",
				   node_info_ptr->node_array[i].node_addr);

		if (node_info_ptr->node_array[i].sockets)
		        xstrfmtcat(tmp_str, " Sockets=%u",
				   node_info_ptr->node_array[i].sockets);

		if (node_info_ptr->node_array[i].cores)
		        xstrfmtcat(tmp_str,  " CoresPerSocket=%u",
				   node_info_ptr->node_array[i].cores);

		if (node_info_ptr->node_array[i].threads)
		        xstrfmtcat(tmp_str, " ThreadsPerCore=%u",
				   node_info_ptr->node_array[i].threads);

		if (node_info_ptr->node_array[i].gres != NULL)
		        xstrfmtcat(tmp_str, " Gres=%s",
				   node_info_ptr->node_array[i].gres);

		if (node_info_ptr->node_array[i].real_memory > 1)
		        xstrfmtcat(tmp_str, " RealMemory=%"PRIu64"",
				   node_info_ptr->node_array[i].real_memory);

		if (node_info_ptr->node_array[i].tmp_disk)
		        xstrfmtcat(tmp_str, " TmpDisk=%u",
				   node_info_ptr->node_array[i].tmp_disk);

		if (node_info_ptr->node_array[i].weight != 1)
		        xstrfmtcat(tmp_str, " Weight=%u",
				   node_info_ptr->node_array[i].weight);

		if (node_info_ptr->node_array[i].features != NULL)
		        xstrfmtcat(tmp_str, " Feature=%s",
				   node_info_ptr->node_array[i].features);

		if (node_info_ptr->node_array[i].port &&
		    node_info_ptr->node_array[i].port
		    != slurm_ctl_conf_ptr->slurmd_port)
		        xstrfmtcat(tmp_str, " Port=%u",
				   node_info_ptr->node_array[i].port);

		/* check for duplicate records */
		for (crp = rp; crp != NULL; crp = crp->next) {
			if (!xstrcmp(crp->rec, tmp_str)) {
				xfree(tmp_str);
				break;
			}
		}
		if (crp == NULL) {
			crp = xmalloc(sizeof(struct records));
			crp->rec = tmp_str;
			tmp_str = NULL;	/* transfered to record */
			crp->hostlist = hostlist_create("");
			hostlist_push(crp->hostlist,
				      node_info_ptr->node_array[i].name);
			crp->next = rp;
			rp = crp;
		} else {
			hostlist_push(crp->hostlist,
				      node_info_ptr->node_array[i].name);
		}
	}

	/* now write the node strings to the output file */
	for (crp = rp; crp != NULL; crp = crp->next) {
		tmp_str = hostlist_ranged_string_xmalloc(crp->hostlist);
		fprintf(fp, "NodeName=%s%s\n", tmp_str, crp->rec);
		debug("Hostlist: %s written to output file.", tmp_str);
		xfree(tmp_str);
		xfree(crp->rec);
		hostlist_destroy(crp->hostlist);
	}
	/* free structure elements */
	while (rp != NULL) {
		crp = rp;
		rp = rp->next;
		xfree(crp);
	}

	_write_group_header (fp, "PARTITIONS");
	/* now write partition info */
	p = part_info_ptr->partition_array;
	for (i = 0; i < part_info_ptr->record_count; i++) {
		if (p[i].name == NULL)
			continue;
		fprintf(fp, "PartitionName=%s", p[i].name);

		if (p[i].allow_alloc_nodes &&
		    (xstrcasecmp(p[i].allow_alloc_nodes, "ALL") != 0))
			fprintf(fp, " AllocNodes=%s",
				p[i].allow_alloc_nodes);

		if (p[i].allow_accounts &&
		    (xstrcasecmp(p[i].allow_accounts, "ALL") != 0))
			fprintf(fp, " AllowAccounts=%s", p[i].allow_accounts);

		if (p[i].allow_groups &&
		    (xstrcasecmp(p[i].allow_groups, "ALL") != 0))
			fprintf(fp, " AllowGroups=%s", p[i].allow_groups);

		if (p[i].allow_qos && (xstrcasecmp(p[i].allow_qos, "ALL") != 0))
			fprintf(fp, " AllowQos=%s", p[i].allow_qos);

		if (p[i].alternate != NULL)
			fprintf(fp, " Alternate=%s", p[i].alternate);

		if (p[i].flags & PART_FLAG_DEFAULT)
			fprintf(fp, " Default=YES");

		if (p[i].def_mem_per_cpu & MEM_PER_CPU) {
		        if (p[i].def_mem_per_cpu != MEM_PER_CPU)
		                fprintf(fp, "DefMemPerCPU=%"PRIu64"",
		                        p[i].def_mem_per_cpu & (~MEM_PER_CPU));
		} else if (p[i].def_mem_per_cpu != 0)
		        fprintf(fp, "DefMemPerNode=%"PRIu64"",
		                p[i].def_mem_per_cpu);

		if (!p[i].allow_accounts && p[i].deny_accounts)
			fprintf(fp, "DenyAccounts=%s", p[i].deny_accounts);

		if (!p[i].allow_qos && p[i].deny_qos)
			fprintf(fp, "DenyQos=%s", p[i].deny_qos);

		if (p[i].default_time != NO_VAL) {
			if (p[i].default_time == INFINITE)
				fprintf(fp, "DefaultTime=UNLIMITED");
			else {
		                char time_line[32];
		                secs2time_str(p[i].default_time * 60, time_line,
					      sizeof(time_line));
				fprintf(fp, "DefaultTime=%s", time_line);
			}
		}

		if (p[i].flags & PART_FLAG_NO_ROOT)
			fprintf(fp, " DisableRootJobs=YES");

		if (p[i].flags & PART_FLAG_EXCLUSIVE_USER)
			fprintf(fp, " ExclusiveUser=YES");

		if (p[i].grace_time)
			fprintf(fp, " GraceTime=%"PRIu32"", p[i].grace_time);

		if (p[i].flags & PART_FLAG_HIDDEN)
			fprintf(fp, " Hidden=YES");

		if (p[i].flags & PART_FLAG_LLN)
	                fprintf(fp, " LLN=YES");

		if (p[i].max_cpus_per_node != INFINITE)
			fprintf(fp, " MaxCPUsPerNode=%"PRIu32"",
				p[i].max_cpus_per_node);

		if (p[i].max_mem_per_cpu & MEM_PER_CPU) {
		        if (p[i].max_mem_per_cpu != MEM_PER_CPU)
		                fprintf(fp, " MaxMemPerCPU=%"PRIu64"",
		                        p[i].max_mem_per_cpu & (~MEM_PER_CPU));
		} else if (p[i].max_mem_per_cpu != 0)
		        fprintf(fp, " MaxMemPerNode=%"PRIu64"",
				p[i].max_mem_per_cpu);

		if (p[i].max_nodes != INFINITE) {
			char tmp1[16];
		        if (cluster_flags & CLUSTER_FLAG_BG)
				convert_num_unit((float)p[i].max_nodes, tmp1,
						 sizeof(tmp1), UNIT_NONE,
						 NO_VAL,
						 CONVERT_NUM_UNIT_EXACT);
		        else
		                snprintf(tmp1, sizeof(tmp1), "%u",
					 p[i].max_nodes);

		        fprintf(fp, "MaxNodes=%s", tmp1);
		}

		if (p[i].max_time != INFINITE) {
			char time_line[32];
			secs2time_str(p[i].max_time * 60, time_line,
			              sizeof(time_line));
			fprintf(fp, " MaxTime=%s", time_line);
		}

		if (p[i].min_nodes != 1) {
			char tmp1[16];
			if (cluster_flags & CLUSTER_FLAG_BG)
				convert_num_unit((float)p[i].min_nodes, tmp1,
						 sizeof(tmp1), UNIT_NONE,
						 NO_VAL,
						 CONVERT_NUM_UNIT_EXACT);
			else
			        snprintf(tmp1, sizeof(tmp1), "%u",
					 p[i].min_nodes);
			fprintf(fp, " MinNodes=%s", tmp1);
		}

		if (p[i].nodes != NULL)
			fprintf(fp, " Nodes=%s", p[i].nodes);

		if (p[i].preempt_mode != NO_VAL16)
			fprintf(fp, " PreemptMode=%s",
				preempt_mode_string(p[i].preempt_mode));

		if (p[i].priority_job_factor != 1)
			fprintf(fp, " PriorityJobFactor=%"PRIu16,
				p[i].priority_job_factor);

		if (p[i].priority_tier != 1)
			fprintf(fp, " PriorityTier=%"PRIu16,
				p[i].priority_tier);

		if (p[i].qos_char != NULL)
			fprintf(fp, " QOS=%s", p[i].qos_char);

		if (p[i].flags & PART_FLAG_REQ_RESV)
	                fprintf(fp, " ReqResv=YES");

		if (p[i].flags & PART_FLAG_ROOT_ONLY)
	                fprintf(fp, " RootOnly=YES");

		if (p[i].cr_type & CR_CORE)
			fprintf(fp, " SelectTypeParameters=CR_CORE");
		else if (p[i].cr_type & CR_SOCKET)
			fprintf(fp, " SelectTypeParameters=CR_SOCKET");

		force = p[i].max_share & SHARED_FORCE;
		val = p[i].max_share & (~SHARED_FORCE);
		if (val == 0)
		        fprintf(fp, " Shared=EXCLUSIVE");
		else if (force) {
		        fprintf(fp, " Shared=FORCE:%u", val);
		} else if (val != 1)
		        fprintf(fp, " Shared=YES:%u", val);

		if (p[i].state_up == PARTITION_UP)
	                fprintf(fp, " State=UP");
	        else if (p[i].state_up == PARTITION_DOWN)
	                fprintf(fp, " State=DOWN");
	        else if (p[i].state_up == PARTITION_INACTIVE)
	                fprintf(fp, " State=INACTIVE");
	        else if (p[i].state_up == PARTITION_DRAIN)
	                fprintf(fp, " State=DRAIN");
	        else
	                fprintf(fp, " State=UNKNOWN");

		if (p[i].billing_weights_str != NULL)
			fprintf(fp, " TRESBillingWeights=%s",
			        p[i].billing_weights_str);

		fprintf(fp, "\n");
	}

	fprintf(stdout, "Slurm config saved to %s\n", path);

	xfree(path);
	fclose(fp);
}

static void _print_config_plugin_params_list(FILE* out, List l, char *title)
{
	ListIterator itr = NULL;
	config_plugin_params_t *p;

	if (!l || !list_count(l))
		return;

	fprintf(out, "%s", title);
	itr = list_iterator_create(l);
	while ((p = list_next(itr))){
		fprintf(out, "\n----- %s -----\n", p->name);
		slurm_print_key_pairs(out, p->key_pairs,"");
	}
	list_iterator_destroy(itr);
}

/*
 * slurm_print_ctl_conf - output the contents of slurm control configuration
 *	message as loaded using slurm_load_ctl_conf()
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
	char *tmp2_str = NULL;

	if (cluster_flags & CLUSTER_FLAG_BGQ)
		select_title = "\nBluegene/Q configuration\n";
	else if (cluster_flags & CLUSTER_FLAG_CRAY)
		select_title = "\nCray configuration\n";

	if (slurm_ctl_conf_ptr == NULL)
		return;

	slurm_make_time_str((time_t *)&slurm_ctl_conf_ptr->last_update,
			     time_str, sizeof(time_str));
	snprintf(tmp_str, sizeof(tmp_str), "Configuration data as of %s\n",
		 time_str);

	ret_list = slurm_ctl_conf_2_key_pairs(slurm_ctl_conf_ptr);
	if (ret_list) {
		slurm_print_key_pairs(out, ret_list, tmp_str);
		FREE_NULL_LIST(ret_list);
	}

	slurm_print_key_pairs(out, slurm_ctl_conf_ptr->acct_gather_conf,
			      "\nAccount Gather Configuration:\n");

	slurm_print_key_pairs(out, slurm_ctl_conf_ptr->cgroup_conf,
			      "\nCgroup Support Configuration:\n");

	slurm_print_key_pairs(out, slurm_ctl_conf_ptr->ext_sensors_conf,
			      "\nExternal Sensors Configuration:\n");

	xstrcat(tmp2_str, "\nNode Features Configuration:");
	_print_config_plugin_params_list(out,
		 (List) slurm_ctl_conf_ptr->node_features_conf, tmp2_str);
	xfree(tmp2_str);

	xstrcat(tmp2_str, "\nSlurmctld Plugstack Plugins Configuration:");
	_print_config_plugin_params_list(out,
		 (List) slurm_ctl_conf_ptr->slurmctld_plugstack_conf, tmp2_str);
	xfree(tmp2_str);

	slurm_print_key_pairs(out, slurm_ctl_conf_ptr->select_conf_key_pairs,
			      select_title);

}
extern void *slurm_ctl_conf_2_key_pairs (slurm_ctl_conf_t* slurm_ctl_conf_ptr)
{
	List ret_list = NULL;
	config_key_pair_t *key_pair;
	char tmp_str[128];
	uint32_t cluster_flags = slurmdb_setup_cluster_flags();
	int i;

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
	key_pair->name = xstrdup("AccountingStorageTRES");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->accounting_storage_tres);
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
	key_pair->value = xstrdup(
		slurm_ctl_conf_ptr->acctng_store_job_comment ? "Yes" : "No");
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
	key_pair->name = xstrdup("AcctGatherInterconnectType");
	key_pair->value =
		xstrdup(slurm_ctl_conf_ptr->acct_gather_interconnect_type);
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
	key_pair->name = xstrdup("AllowSpecResourcesUsage");
	key_pair->value = xstrdup_printf(
		"%u", slurm_ctl_conf_ptr->use_spec_resources);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("AuthInfo");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->authinfo);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("AuthType");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->authtype);
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
	key_pair->name = xstrdup("BurstBufferType");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->bb_type);
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
	key_pair->name = xstrdup("CoreSpecPlugin");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->core_spec_plugin);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("CpuFreqDef");
	cpu_freq_to_string(tmp_str, sizeof(tmp_str),
			   slurm_ctl_conf_ptr->cpu_freq_def);
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("CpuFreqGovernors");
	cpu_freq_govlist_to_string(tmp_str, sizeof(tmp_str),
			   slurm_ctl_conf_ptr->cpu_freq_govs);
	key_pair->value = xstrdup(tmp_str);
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
	if (slurm_ctl_conf_ptr->def_mem_per_cpu == INFINITE64) {
		key_pair->name = xstrdup("DefMemPerNode");
		key_pair->value = xstrdup("UNLIMITED");
	} else if (slurm_ctl_conf_ptr->def_mem_per_cpu & MEM_PER_CPU) {
		key_pair->name = xstrdup("DefMemPerCPU");
		snprintf(tmp_str, sizeof(tmp_str), "%"PRIu64"",
			 slurm_ctl_conf_ptr->def_mem_per_cpu &
			 (~MEM_PER_CPU));
		key_pair->value = xstrdup(tmp_str);
	} else if (slurm_ctl_conf_ptr->def_mem_per_cpu) {
		key_pair->name = xstrdup("DefMemPerNode");
		snprintf(tmp_str, sizeof(tmp_str), "%"PRIu64"",
			 slurm_ctl_conf_ptr->def_mem_per_cpu);
		key_pair->value = xstrdup(tmp_str);
	} else {
		key_pair->name = xstrdup("DefMemPerNode");
		key_pair->value = xstrdup("UNLIMITED");
	}

	key_pair = xmalloc(sizeof(config_key_pair_t));
	list_append(ret_list, key_pair);
	key_pair->name = xstrdup("DisableRootJobs");
	key_pair->value = xstrdup(
		slurm_ctl_conf_ptr->disable_root_jobs ? "Yes" : "No");

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("EioTimeout");
	snprintf(tmp_str, sizeof(tmp_str), "%u",
		 slurm_ctl_conf_ptr->eio_timeout);
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	list_append(ret_list, key_pair);
	key_pair->name = xstrdup("EnforcePartLimits");
	key_pair->value = xstrdup(
		parse_part_enforce_type_2str(
			slurm_ctl_conf_ptr->enforce_part_limits));

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

	if (xstrcmp(slurm_ctl_conf_ptr->priority_type, "priority/basic")) {
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

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("FederationParameters");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->fed_params);
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

	snprintf(tmp_str, sizeof(tmp_str), "%u",
		 slurm_ctl_conf_ptr->group_force);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("GroupUpdateForce");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u sec",
		 slurm_ctl_conf_ptr->group_time);
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
	key_pair->name = xstrdup("JobContainerType");
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

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("JobRequeue");
	key_pair->value = xstrdup_printf(
		"%u", slurm_ctl_conf_ptr->job_requeue);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("JobSubmitPlugins");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->job_submit_plugins);
	list_append(ret_list, key_pair);

	if (slurm_ctl_conf_ptr->keep_alive_time == NO_VAL16)
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
	key_pair->value = xstrdup_printf(
		"%u", slurm_ctl_conf_ptr->kill_on_bad_exit);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u sec",
		 slurm_ctl_conf_ptr->kill_wait);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("KillWait");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("LaunchParameters");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->launch_params);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("LaunchType");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->launch_type);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("Layouts");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->layouts);
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
	key_pair->name = xstrdup("LogTimeFormat");
	if (slurm_ctl_conf_ptr->log_fmt == LOG_FMT_ISO8601_MS)
		key_pair->value = xstrdup("iso8601_ms");
	else if (slurm_ctl_conf_ptr->log_fmt == LOG_FMT_ISO8601)
		key_pair->value = xstrdup("iso8601");
	else if (slurm_ctl_conf_ptr->log_fmt == LOG_FMT_RFC5424_MS)
		key_pair->value = xstrdup("rfc5424_ms");
	else if (slurm_ctl_conf_ptr->log_fmt == LOG_FMT_RFC5424)
		key_pair->value = xstrdup("rfc5424");
	else if (slurm_ctl_conf_ptr->log_fmt == LOG_FMT_CLOCK)
		key_pair->value = xstrdup("clock");
	else if (slurm_ctl_conf_ptr->log_fmt == LOG_FMT_SHORT)
		key_pair->value = xstrdup("short");
	else if (slurm_ctl_conf_ptr->log_fmt == LOG_FMT_THREAD_ID)
		key_pair->value = xstrdup("thread_id");
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MailDomain");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->mail_domain);
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
	if (slurm_ctl_conf_ptr->max_mem_per_cpu == INFINITE64) {
		key_pair->name = xstrdup("MaxMemPerNode");
		key_pair->value = xstrdup("UNLIMITED");
	} else if (slurm_ctl_conf_ptr->max_mem_per_cpu & MEM_PER_CPU) {
		key_pair->name = xstrdup("MaxMemPerCPU");
		snprintf(tmp_str, sizeof(tmp_str), "%"PRIu64"",
			 slurm_ctl_conf_ptr->max_mem_per_cpu & (~MEM_PER_CPU));
		key_pair->value = xstrdup(tmp_str);

	} else if (slurm_ctl_conf_ptr->max_mem_per_cpu) {
		key_pair->name = xstrdup("MaxMemPerNode");
		snprintf(tmp_str, sizeof(tmp_str), "%"PRIu64"",
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

 	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MCSPlugin");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->mcs_plugin);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MCSParameters");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->mcs_plugin_params);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MemLimitEnforce");
	key_pair->value = xstrdup(
		slurm_ctl_conf_ptr->mem_limit_enforce ? "Yes" : "No");
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

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MsgAggregationParams");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->msg_aggr_params);
	list_append(ret_list, key_pair);

	if (cluster_flags & CLUSTER_FLAG_MULTSD) {
		key_pair = xmalloc(sizeof(config_key_pair_t));
		key_pair->name = xstrdup("MULTIPLE_SLURMD");
		key_pair->value = xstrdup("Yes");
		list_append(ret_list, key_pair);
	}

	snprintf(tmp_str, sizeof(tmp_str), "%u",
		 slurm_ctl_conf_ptr->next_job_id);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("NEXT_JOB_ID");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("NodeFeaturesPlugins");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->node_features_plugins);
	list_append(ret_list, key_pair);

	if (slurm_ctl_conf_ptr->over_time_limit == INFINITE16)
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
	key_pair->name = xstrdup("PowerParameters");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->power_parameters);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("PowerPlugin");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->power_plugin);
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

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("PriorityParameters");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->priority_params);
	list_append(ret_list, key_pair);

	if (xstrcmp(slurm_ctl_conf_ptr->priority_type, "priority/basic") == 0) {
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

		key_pair = xmalloc(sizeof(config_key_pair_t));
		key_pair->name = xstrdup("PriorityFavorSmall");
		key_pair->value = xstrdup(
			slurm_ctl_conf_ptr->priority_favor_small ?
			"Yes" : "No");
		list_append(ret_list, key_pair);

		key_pair = xmalloc(sizeof(config_key_pair_t));
		key_pair->name = xstrdup("PriorityFlags");
		key_pair->value = priority_flags_string(slurm_ctl_conf_ptr->
							priority_flags);
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

		key_pair = xmalloc(sizeof(config_key_pair_t));
		key_pair->name = xstrdup("PriorityWeightTRES");
		key_pair->value =
			xstrdup(slurm_ctl_conf_ptr->priority_weight_tres);
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

	snprintf(tmp_str, sizeof(tmp_str), "%u",
		 slurm_ctl_conf_ptr->prolog_epilog_timeout);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("PrologEpilogTimeout");
	key_pair->value = xstrdup(tmp_str);
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
	key_pair->name = xstrdup("RequeueExit");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->requeue_exit);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("RequeueExitHold");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->requeue_exit_hold);
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

	if (slurm_ctl_conf_ptr->resv_over_run == INFINITE16)
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
	key_pair->name = xstrdup("RoutePlugin");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->route_plugin);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SallocDefaultCommand");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->salloc_default_command);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SbcastParameters");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->sbcast_parameters);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SchedulerParameters");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->sched_params);
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


	for (i = 0; i < slurm_ctl_conf_ptr->control_cnt; i++) {
		if (!slurm_ctl_conf_ptr->control_machine[i])
			break;

		key_pair = xmalloc(sizeof(config_key_pair_t));
		xstrfmtcat(key_pair->name, "SlurmctldHost[%d]", i);
		if (slurm_ctl_conf_ptr->control_addr[i] &&
		    xstrcmp(slurm_ctl_conf_ptr->control_machine[i],
			    slurm_ctl_conf_ptr->control_addr[i])) {
			xstrfmtcat(key_pair->value, "%s(%s)",
				   slurm_ctl_conf_ptr->control_machine[i],
				   slurm_ctl_conf_ptr->control_addr[i]);
		} else {
			key_pair->value =
				xstrdup(slurm_ctl_conf_ptr->control_machine[i]);
		}
		list_append(ret_list, key_pair);
	}

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SlurmctldLogFile");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->slurmctld_logfile);
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

	snprintf(tmp_str, sizeof(tmp_str), "%s",
		 log_num2string(slurm_ctl_conf_ptr->slurmctld_syslog_debug));
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SlurmctldSyslogDebug");
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

	snprintf(tmp_str, sizeof(tmp_str), "%u",
		 slurm_ctl_conf_ptr->slurmd_port);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SlurmdPort");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SlurmdSpoolDir");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->slurmd_spooldir);
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%s",
		 log_num2string(slurm_ctl_conf_ptr->slurmd_syslog_debug));
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SlurmdSyslogDebug");
	key_pair->value = xstrdup(tmp_str);
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

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SlurmSchedLogFile");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->sched_logfile);
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
	key_pair->name = xstrdup("SrunPortRange");
	key_pair->value = xstrdup_printf("%u-%u",
			(slurm_ctl_conf_ptr->srun_port_range &&
			 slurm_ctl_conf_ptr->srun_port_range[0] != 0) ?
				slurm_ctl_conf_ptr->srun_port_range[0] : 0,
			(slurm_ctl_conf_ptr->srun_port_range &&
			 slurm_ctl_conf_ptr->srun_port_range[1] != 0) ?
				slurm_ctl_conf_ptr->srun_port_range[1] : 0);
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

	if (slurm_ctl_conf_ptr->suspend_time == 0) {
		snprintf(tmp_str, sizeof(tmp_str), "NONE");
	} else {
		snprintf(tmp_str, sizeof(tmp_str), "%d sec",
			 ((int)slurm_ctl_conf_ptr->suspend_time - 1));
	}
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("SuspendTime");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	if (slurm_ctl_conf_ptr->suspend_timeout == 0) {
		snprintf(tmp_str, sizeof(tmp_str), "NONE");
	} else {
		snprintf(tmp_str, sizeof(tmp_str), "%u sec",
			 slurm_ctl_conf_ptr->suspend_timeout);
	}
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
	key_pair->name = xstrdup("TCPTimeout");
	key_pair->value = xstrdup_printf("%u sec",
					 slurm_ctl_conf_ptr->tcp_timeout);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("TmpFS");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->tmp_fs);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("TopologyParam");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->topology_param);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("TopologyPlugin");
	key_pair->value = xstrdup(slurm_ctl_conf_ptr->topology_plugin);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("TrackWCKey");
	key_pair->value = xstrdup(
		slurm_ctl_conf_ptr->track_wckey ? "Yes" : "No");
	list_append(ret_list, key_pair);

	snprintf(tmp_str, sizeof(tmp_str), "%u",
		 slurm_ctl_conf_ptr->tree_width);
	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("TreeWidth");
	key_pair->value = xstrdup(tmp_str);
	list_append(ret_list, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("UsePam");
	key_pair->value = xstrdup_printf("%u", slurm_ctl_conf_ptr->use_pam);
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

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg,
					   working_cluster_rec) < 0)
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
	fprintf(out, "Actual real memory       = %"PRIu64" MB\n",
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

/*
 * _write_key_pairs - write the contents of slurm
 *	configuration to an output file
 * IN out - file to write to
 * IN key_pairs - key pairs of the running slurm configuration
 */
static void _write_key_pairs(FILE* out, void *key_pairs)
{
	config_key_pair_t *key_pair;
	char *temp = NULL;
	List config_list = (List)key_pairs;
	ListIterator iter = NULL;
	/* define lists of specific configuration sections */
	List other_list = list_create(slurm_destroy_char);
	List control_list = list_create(slurm_destroy_char);
	List accounting_list = list_create(slurm_destroy_char);
	List logging_list = list_create(slurm_destroy_char);
	List power_list = list_create(slurm_destroy_char);
	List sched_list = list_create(slurm_destroy_char);
	List topology_list = list_create(slurm_destroy_char);
	List timers_list = list_create(slurm_destroy_char);
	List debug_list = list_create(slurm_destroy_char);
	List proepilog_list = list_create(slurm_destroy_char);
	List resconf_list = list_create(slurm_destroy_char);
	List proctrac_list = list_create(slurm_destroy_char);

	if (!config_list)
		return;

	iter = list_iterator_create(config_list);
	while ((key_pair = list_next(iter))) {
		/* Ignore ENV variables in config_list; they'll
		 * cause problems in an active slurm.conf */
		if (!xstrcmp(key_pair->name, "BOOT_TIME") ||
		    !xstrcmp(key_pair->name, "HASH_VAL") ||
		    !xstrcmp(key_pair->name, "NEXT_JOB_ID") ||
		    !xstrcmp(key_pair->name, "SLURM_CONF") ||
		    !xstrcmp(key_pair->name, "SLURM_VERSION")) {
			debug("Ignoring %s (not written)", key_pair->name);
			continue;
		}

		/* Comment out certain key_pairs */
		/* - TaskPluginParam=(null type) is not a NULL but
		 * it does imply no value */
		if ((key_pair->value == NULL) ||
		    (strlen(key_pair->value) == 0) ||
		    !xstrcasecmp(key_pair->value, "(null type)") ||
		    !xstrcasecmp(key_pair->value, "(null)") ||
		    !xstrcasecmp(key_pair->value, "N/A") ||
		    (!xstrcasecmp(key_pair->name, "KeepAliveTime") &&
		     !xstrcasecmp(key_pair->value, "SYSTEM_DEFAULT")) ||
		    !xstrcasecmp(key_pair->name, "DynAllocPort") ||
		    (!xstrcasecmp(key_pair->name, "DefMemPerNode") &&
		     !xstrcasecmp(key_pair->value, "UNLIMITED"))) {
			temp = xstrdup_printf("#%s=", key_pair->name);
			debug("Commenting out %s=%s",
			      key_pair->name,
			      key_pair->value);
		} else {
			if ((!xstrcasecmp(key_pair->name, "ChosLoc")) ||
			    (!xstrcasecmp(key_pair->name, "Epilog")) ||
			    (!xstrcasecmp(key_pair->name, "EpilogSlurmctld")) ||
			    (!xstrcasecmp(key_pair->name,
					  "HealthCheckProgram")) ||
			    (!xstrcasecmp(key_pair->name, "MailProg")) ||
			    (!xstrcasecmp(key_pair->name, "Prolog")) ||
			    (!xstrcasecmp(key_pair->name, "PrologSlurmctld")) ||
			    (!xstrcasecmp(key_pair->name, "RebootProgram")) ||
			    (!xstrcasecmp(key_pair->name, "ResumeProgram")) ||
			    (!xstrcasecmp(key_pair->name, "ResvEpilog")) ||
			    (!xstrcasecmp(key_pair->name, "ResvProlog")) ||
			    (!xstrcasecmp(key_pair->name,
					  "SallocDefaultCommand")) ||
			    (!xstrcasecmp(key_pair->name, "SrunEpilog")) ||
			    (!xstrcasecmp(key_pair->name, "SrunProlog")) ||
			    (!xstrcasecmp(key_pair->name, "SuspendProgram")) ||
			    (!xstrcasecmp(key_pair->name, "TaskEpilog")) ||
			    (!xstrcasecmp(key_pair->name, "TaskProlog")) ||
			    (!xstrcasecmp(key_pair->name,
					  "UnkillableStepProgram"))) {
				/* Exceptions not be tokenized in the output */
				temp = key_pair->value;
			} else {
				/*
				 * Only write out values. Use strtok
				 * to grab just the value (ie. "60 sec")
				 */
				temp = strtok(key_pair->value, " (");
			}
			temp = xstrdup_printf("%s=%s",
					      key_pair->name, temp);
		}

		if (!xstrcasecmp(key_pair->name, "ControlMachine") ||
		    !xstrcasecmp(key_pair->name, "ControlAddr") ||
		    !xstrcasecmp(key_pair->name, "ClusterName") ||
		    !xstrcasecmp(key_pair->name, "SlurmUser") ||
		    !xstrcasecmp(key_pair->name, "SlurmdUser") ||
		    !xstrcasecmp(key_pair->name, "SlurmctldPort") ||
		    !xstrcasecmp(key_pair->name, "SlurmdPort") ||
		    !xstrcasecmp(key_pair->name, "BackupAddr") ||
		    !xstrcasecmp(key_pair->name, "BackupController")) {
			list_append(control_list, temp);
			continue;
		}

		if (!xstrcasecmp(key_pair->name, "StateSaveLocation") ||
		    !xstrcasecmp(key_pair->name, "SlurmdSpoolDir") ||
		    !xstrcasecmp(key_pair->name, "SlurmctldLogFile") ||
		    !xstrcasecmp(key_pair->name, "SlurmdLogFile") ||
		    !xstrcasecmp(key_pair->name, "SlurmctldPidFile") ||
		    !xstrcasecmp(key_pair->name, "SlurmdPidFile") ||
		    !xstrcasecmp(key_pair->name, "SlurmSchedLogFile") ||
		    !xstrcasecmp(key_pair->name, "SlurmEventHandlerLogfile")) {
			list_append(logging_list, temp);
			continue;
		}

		if (!xstrcasecmp(key_pair->name, "AccountingStorageBackupHost") ||
		    !xstrcasecmp(key_pair->name, "AccountingStorageEnforce") ||
		    !xstrcasecmp(key_pair->name, "AccountingStorageHost") ||
		    !xstrcasecmp(key_pair->name, "AccountingStorageLoc") ||
		    !xstrcasecmp(key_pair->name, "AccountingStoragePort") ||
		    !xstrcasecmp(key_pair->name, "AccountingStorageType") ||
		    !xstrcasecmp(key_pair->name, "AccountingStorageUser") ||
		    !xstrcasecmp(key_pair->name, "AccountingStoreJobComment") ||
		    !xstrcasecmp(key_pair->name, "AcctGatherEnergyType") ||
		    !xstrcasecmp(key_pair->name, "AcctGatherFilesystemType") ||
		    !xstrcasecmp(key_pair->name, "AcctGatherInterconnectType") ||
		    !xstrcasecmp(key_pair->name, "AcctGatherNodeFreq") ||
		    !xstrcasecmp(key_pair->name, "AcctGatherProfileType") ||
		    !xstrcasecmp(key_pair->name, "JobAcctGatherFrequency") ||
		    !xstrcasecmp(key_pair->name, "JobAcctGatherType") ||
		    !xstrcasecmp(key_pair->name, "ExtSensorsType") ||
		    !xstrcasecmp(key_pair->name, "ExtSensorsFreq")) {
			list_append(accounting_list, temp);
			continue;
		}

		if (!xstrcasecmp(key_pair->name, "SuspendExcNodes") ||
		    !xstrcasecmp(key_pair->name, "SuspendExcParts") ||
		    !xstrcasecmp(key_pair->name, "SuspendProgram") ||
		    !xstrcasecmp(key_pair->name, "SuspendRate") ||
		    !xstrcasecmp(key_pair->name, "SuspendTime") ||
		    !xstrcasecmp(key_pair->name, "SuspendTimeout") ||
		    !xstrcasecmp(key_pair->name, "ResumeProgram") ||
		    !xstrcasecmp(key_pair->name, "ResumeRate") ||
		    !xstrcasecmp(key_pair->name, "ResumeTimeout")) {
			list_append(power_list, temp);
			continue;
		}

		if (!xstrcasecmp(key_pair->name, "SelectType") ||
		    !xstrcasecmp(key_pair->name, "SelectTypeParameters") ||
		    !xstrcasecmp(key_pair->name, "SchedulerParameters") ||
		    !xstrcasecmp(key_pair->name, "SchedulerTimeSlice") ||
		    !xstrcasecmp(key_pair->name, "SchedulerType") ||
		    !xstrcasecmp(key_pair->name, "SlurmSchedLogLevel") ||
		    !xstrcasecmp(key_pair->name, "PreemptMode") ||
		    !xstrcasecmp(key_pair->name, "PreemptType") ||
		    !xstrcasecmp(key_pair->name, "PriorityType") ||
		    !xstrcasecmp(key_pair->name, "FastSchedule")) {
			list_append(sched_list, temp);
			continue;
		}

		if (!xstrcasecmp(key_pair->name, "TopologyPlugin")) {
			list_append(topology_list, temp);
			continue;
		}

		if (!xstrcasecmp(key_pair->name, "SlurmctldTimeout") ||
		    !xstrcasecmp(key_pair->name, "SlurmdTimeout") ||
		    !xstrcasecmp(key_pair->name, "InactiveLimit") ||
		    !xstrcasecmp(key_pair->name, "MinJobAge") ||
		    !xstrcasecmp(key_pair->name, "KillWait") ||
		    !xstrcasecmp(key_pair->name, "BatchStartTimeout") ||
		    !xstrcasecmp(key_pair->name, "CompleteWait") ||
		    !xstrcasecmp(key_pair->name, "EpilogMsgTime") ||
		    !xstrcasecmp(key_pair->name, "GetEnvTimeout") ||
		    !xstrcasecmp(key_pair->name, "Waittime")) {
			list_append(timers_list, temp);
			continue;
		}

		if (!xstrcasecmp(key_pair->name, "SlurmctldDebug") ||
		    !xstrcasecmp(key_pair->name, "SlurmdDebug") ||
		    !xstrcasecmp(key_pair->name, "DebugFlags")) {
			list_append(debug_list, temp);
			continue;
		}

		if (!xstrcasecmp(key_pair->name, "TaskPlugin") ||
		    !xstrcasecmp(key_pair->name, "TaskPluginParam")) {
			list_append(resconf_list, temp);
			continue;
		}

		if (!xstrcasecmp(key_pair->name, "ProcTrackType")) {
			list_append(proctrac_list, temp);
			continue;
		}

		if (!xstrcasecmp(key_pair->name, "Epilog") ||
		    !xstrcasecmp(key_pair->name, "Prolog") ||
		    !xstrcasecmp(key_pair->name, "SrunProlog") ||
		    !xstrcasecmp(key_pair->name, "SrunEpilog") ||
		    !xstrcasecmp(key_pair->name, "TaskEpilog") ||
		    !xstrcasecmp(key_pair->name, "TaskProlog")) {
			list_append(proepilog_list, temp);
			continue;
		} else {
			list_append(other_list, temp);
		}
	}
	list_iterator_destroy(iter);

	_write_group_header (out, "CONTROL");
	iter = list_iterator_create(control_list);
	while ((temp = list_next(iter)))
		fprintf(out, "%s\n", temp);
	list_iterator_destroy(iter);
	FREE_NULL_LIST(control_list);

	_write_group_header (out, "LOGGING & OTHER PATHS");
	iter = list_iterator_create(logging_list);
	while ((temp = list_next(iter)))
		fprintf(out, "%s\n", temp);
	list_iterator_destroy(iter);
	FREE_NULL_LIST(logging_list);

	_write_group_header (out, "ACCOUNTING");
	iter = list_iterator_create(accounting_list);
	while ((temp = list_next(iter)))
		fprintf(out, "%s\n", temp);
	list_iterator_destroy(iter);
	FREE_NULL_LIST(accounting_list);

	_write_group_header (out, "SCHEDULING & ALLOCATION");
	iter = list_iterator_create(sched_list);
	while ((temp = list_next(iter)))
		fprintf(out, "%s\n", temp);
	list_iterator_destroy(iter);
	FREE_NULL_LIST(sched_list);

	_write_group_header (out, "TOPOLOGY");
	iter = list_iterator_create(topology_list);
	while ((temp = list_next(iter)))
		fprintf(out, "%s\n", temp);
	list_iterator_destroy(iter);
	FREE_NULL_LIST(topology_list);

	_write_group_header (out, "TIMERS");
	iter = list_iterator_create(timers_list);
	while ((temp = list_next(iter)))
		fprintf(out, "%s\n", temp);
	list_iterator_destroy(iter);
	FREE_NULL_LIST(timers_list);

	_write_group_header (out, "POWER");
	iter = list_iterator_create(power_list);
	while ((temp = list_next(iter)))
		fprintf(out, "%s\n", temp);
	list_iterator_destroy(iter);
	FREE_NULL_LIST(power_list);

	_write_group_header (out, "DEBUG");
	iter = list_iterator_create(debug_list);
	while ((temp = list_next(iter)))
		fprintf(out, "%s\n", temp);
	list_iterator_destroy(iter);
	FREE_NULL_LIST(debug_list);

	_write_group_header (out, "EPILOG & PROLOG");
	iter = list_iterator_create(proepilog_list);
	while ((temp = list_next(iter)))
		fprintf(out, "%s\n", temp);
	list_iterator_destroy(iter);
	FREE_NULL_LIST(proepilog_list);

	_write_group_header (out, "PROCESS TRACKING");
	iter = list_iterator_create(proctrac_list);
	while ((temp = list_next(iter)))
		fprintf(out, "%s\n", temp);
	list_iterator_destroy(iter);
	FREE_NULL_LIST(proctrac_list);

	_write_group_header (out, "RESOURCE CONFINEMENT");
	iter = list_iterator_create(resconf_list);
	while ((temp = list_next(iter)))
		fprintf(out, "%s\n", temp);
	list_iterator_destroy(iter);
	FREE_NULL_LIST(resconf_list);

	_write_group_header (out, "OTHER");
	iter = list_iterator_create(other_list);
	while ((temp = list_next(iter)))
		fprintf(out, "%s\n", temp);
	list_iterator_destroy(iter);
	FREE_NULL_LIST(other_list);

}

/*
 * slurm_print_key_pairs - output the contents of key_pairs
 * which is a list of opaque data type config_key_pair_t
 * IN out - file to write to
 * IN key_pairs - List containing key pairs to be printed
 * IN title - title of key pair list
 */
extern void slurm_print_key_pairs(FILE* out, void *key_pairs, char *title)
{
	List config_list = (List)key_pairs;
	ListIterator iter = NULL;
	config_key_pair_t *key_pair;

	if (!config_list || !list_count(config_list))
		return;

	fprintf(out, "%s", title);
	iter = list_iterator_create(config_list);
	while ((key_pair = list_next(iter))) {
		fprintf(out, "%-23s = %s\n", key_pair->name, key_pair->value);
	}
	list_iterator_destroy(iter);
}

/*
 * _write_group_header - write the group headers on the
 *	output slurm configuration file - with the header
 *      string centered between the hash characters
 * IN out - file to write to
 * IN header - header string to write
 */
static void _write_group_header(FILE* out, char * header)
{
	static int comlen = 48;
	int i, hdrlen, left, right;

	if (!header)
		return;
	hdrlen = strlen(header);
	left = ((comlen - hdrlen) / 2) - 1;
	right = left;
	if ((comlen - hdrlen) % 2)
		right++;

	fprintf(out, "#\n");
	for (i = 0; i < comlen; i++)
		fprintf(out, "#");
	fprintf(out, "\n#");
	for (i = 0; i < left; i++)
		fprintf(out, " ");
	fprintf(out, "%s", header);
	for (i = 0; i < right; i++)
		fprintf(out, " ");
	fprintf(out, "#\n");
	for (i = 0; i < comlen; i++)
		fprintf(out, "#");
	fprintf(out, "\n");
}
