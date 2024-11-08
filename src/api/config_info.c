/****************************************************************************\
 *  config_info.c - get/print the system configuration information of slurm
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Copyright (C) SchedMD LLC.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> and Kevin Tew <tew1@llnl.gov>.
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

#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "slurm/slurm.h"

#include "src/common/cpu_frequency.h"
#include "src/common/list.h"
#include "src/common/parse_time.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_resource_info.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/auth.h"
#include "src/interfaces/select.h"

/* Local functions */
static void _write_group_header(FILE* out, char * header);
static void _write_key_pairs(FILE* out, void *key_pairs);
static void _print_config_plugin_params_list(FILE *out, list_t *l, char *title);

/*
 * slurm_api_version - Return a single number reflecting the Slurm API's
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
	char time_str[256];
	char *tmp_str = NULL;
	char *base_path = NULL;
	char *path = NULL;
	void *ret_list = NULL;
	uint16_t val, force;
	FILE *fp = NULL;
	partition_info_t *p = NULL;
	struct records {
	  char *rec;
	  hostlist_t *hostlist;
	  struct records *next;
	} *rp = NULL;
	struct records *crp;

	if ( slurm_ctl_conf_ptr == NULL )
		return ;

	slurm_make_time_str ((time_t *)&slurm_ctl_conf_ptr->last_update,
			     time_str, sizeof(time_str));

	/* open new slurm.conf.<datetime> file for write. This file will
	 * contain the currently running slurm configuration. */
	base_path = getenv("SLURM_CONF_OUT");
	if (!base_path)
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
		"#  Configuration file for Slurm - %s  #\n", time_str);
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
		                fprintf(fp, " DefMemPerCPU=%"PRIu64"",
		                        p[i].def_mem_per_cpu & (~MEM_PER_CPU));
		} else if (p[i].def_mem_per_cpu != 0)
		        fprintf(fp, " DefMemPerNode=%"PRIu64"",
		                p[i].def_mem_per_cpu);

		if (!p[i].allow_accounts && p[i].deny_accounts)
			fprintf(fp, " DenyAccounts=%s", p[i].deny_accounts);

		if (!p[i].allow_qos && p[i].deny_qos)
			fprintf(fp, " DenyQos=%s", p[i].deny_qos);

		if (p[i].default_time != NO_VAL) {
			if (p[i].default_time == INFINITE)
				fprintf(fp, " DefaultTime=UNLIMITED");
			else {
		                char time_line[32];
		                secs2time_str(p[i].default_time * 60, time_line,
					      sizeof(time_line));
				fprintf(fp, " DefaultTime=%s", time_line);
			}
		}

		if (p[i].flags & PART_FLAG_NO_ROOT)
			fprintf(fp, " DisableRootJobs=YES");

		if (p[i].flags & PART_FLAG_EXCLUSIVE_USER)
			fprintf(fp, " ExclusiveUser=YES");

		if (p[i].flags & PART_FLAG_EXCLUSIVE_TOPO)
			fprintf(fp, " ExclusiveTopo=YES");

		if (p[i].grace_time)
			fprintf(fp, " GraceTime=%u", p[i].grace_time);

		if (p[i].flags & PART_FLAG_HIDDEN)
			fprintf(fp, " Hidden=YES");

		if (p[i].flags & PART_FLAG_LLN)
	                fprintf(fp, " LLN=YES");

		if (p[i].max_cpus_per_node != INFINITE)
			fprintf(fp, " MaxCPUsPerNode=%u",
				p[i].max_cpus_per_node);

		if (p[i].max_cpus_per_socket != INFINITE)
			fprintf(fp, " MaxCPUsPerSocket=%u",
				p[i].max_cpus_per_socket);

		if (p[i].max_mem_per_cpu & MEM_PER_CPU) {
		        if (p[i].max_mem_per_cpu != MEM_PER_CPU)
		                fprintf(fp, " MaxMemPerCPU=%"PRIu64"",
		                        p[i].max_mem_per_cpu & (~MEM_PER_CPU));
		} else if (p[i].max_mem_per_cpu != 0)
		        fprintf(fp, " MaxMemPerNode=%"PRIu64"",
				p[i].max_mem_per_cpu);

		if (p[i].max_nodes != INFINITE)
		        fprintf(fp, " MaxNodes=%u", p[i].max_nodes);

		if (p[i].max_time != INFINITE) {
			char time_line[32];
			secs2time_str(p[i].max_time * 60, time_line,
			              sizeof(time_line));
			fprintf(fp, " MaxTime=%s", time_line);
		}

		if (p[i].min_nodes != 1)
			fprintf(fp, " MinNodes=%u", p[i].min_nodes);

		if (p[i].nodes != NULL)
			fprintf(fp, " Nodes=%s", p[i].nodes);

		if (p[i].preempt_mode != NO_VAL16)
			fprintf(fp, " PreemptMode=%s",
				preempt_mode_string(p[i].preempt_mode));

		if (p[i].priority_job_factor != 1)
			fprintf(fp, " PriorityJobFactor=%u",
				p[i].priority_job_factor);

		if (p[i].priority_tier != 1)
			fprintf(fp, " PriorityTier=%u",
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

		if (p[i].flags & PART_FLAG_PDOI)
			fprintf(fp, " PowerDownOnIdle=YES");

		force = p[i].max_share & SHARED_FORCE;
		val = p[i].max_share & (~SHARED_FORCE);
		if (val == 0)
		        fprintf(fp, " OverSubscribe=EXCLUSIVE");
		else if (force) {
		        fprintf(fp, " OverSubscribe=FORCE:%u", val);
		} else if (val != 1)
		        fprintf(fp, " OverSubscribe=YES:%u", val);

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

		if (p[i].resume_timeout == INFINITE16)
	                fprintf(fp, " ResumeTimeout=INFINITE");
		else if (p[i].resume_timeout != NO_VAL16)
	                fprintf(fp, " ResumeTimeout=%d",
				p[i].resume_timeout);

		if (p[i].suspend_timeout == INFINITE16)
	                fprintf(fp, " SuspendTimeout=INFINITE");
		else if (p[i].suspend_timeout != NO_VAL16)
	                fprintf(fp, " SuspendTimeout=%d",
				p[i].suspend_timeout);

		if (p[i].suspend_time == INFINITE)
	                fprintf(fp, " SuspendTime=INFINITE");
		else if (p[i].suspend_time != NO_VAL)
	                fprintf(fp, " SuspendTime=%d",
				p[i].suspend_time);

		fprintf(fp, "\n");
	}

	fprintf(stdout, "Slurm config saved to %s\n", path);

	xfree(path);
	fclose(fp);
}

static void _print_config_plugin_params_list(FILE *out, list_t *l, char *title)
{
	list_itr_t *itr = NULL;
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
	char time_str[32], tmp_str[256];
	void *ret_list = NULL;
	char *select_title = "Select Plugin Configuration";
	char *tmp2_str = NULL;

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

	slurm_print_key_pairs(out, slurm_ctl_conf_ptr->mpi_conf,
			      "\nMPI Plugins Configuration:\n");

	xstrcat(tmp2_str, "\nNode Features Configuration:");
	_print_config_plugin_params_list(out,
		 (list_t *) slurm_ctl_conf_ptr->node_features_conf, tmp2_str);
	xfree(tmp2_str);

	slurm_print_key_pairs(out, slurm_ctl_conf_ptr->select_conf_key_pairs,
			      select_title);

}

static char *_accountingstoreflags(uint32_t conf_flags)
{
	char *str = NULL;

	if (conf_flags & CONF_FLAG_SJC)
		xstrfmtcat(str, "%sjob_comment", str ? "," : "");
	if (conf_flags & CONF_FLAG_SJE)
		xstrfmtcat(str, "%sjob_env", str ? "," : "");
	if (conf_flags & CONF_FLAG_SJX)
		xstrfmtcat(str, "%sjob_extra", str ? "," : "");
	if (conf_flags & CONF_FLAG_SJS)
		xstrfmtcat(str, "%sjob_script", str ? "," : "");
	if (conf_flags & CONF_FLAG_NO_STDIO)
		xstrfmtcat(str, "%sno_stdio", str ? "," : "");

	return str;
}

static char *_logfmtstr(uint16_t log_fmt)
{
	bool format_stderr = false;
	char *logfmtstr = NULL;

	if (log_fmt & LOG_FMT_FORMAT_STDERR) {
		format_stderr = true;
		log_fmt &= (~LOG_FMT_FORMAT_STDERR);
	}

	if (log_fmt == LOG_FMT_ISO8601_MS)
		logfmtstr = xstrdup("iso8601_ms");
	else if (log_fmt == LOG_FMT_ISO8601)
		logfmtstr = xstrdup("iso8601");
	else if (log_fmt == LOG_FMT_RFC5424_MS)
		logfmtstr = xstrdup("rfc5424_ms");
	else if (log_fmt == LOG_FMT_RFC5424)
		logfmtstr = xstrdup("rfc5424");
	else if (log_fmt == LOG_FMT_RFC3339)
		logfmtstr = xstrdup("rfc3339");
	else if (log_fmt == LOG_FMT_CLOCK)
		logfmtstr = xstrdup("clock");
	else if (log_fmt == LOG_FMT_SHORT)
		logfmtstr = xstrdup("short");
	else if (log_fmt == LOG_FMT_THREAD_ID)
		logfmtstr = xstrdup("thread_id");

	if (format_stderr)
		xstrcat(logfmtstr, ",format_stderr");

	return logfmtstr;
}

static void _sprint_task_plugin_params(char *str,
				       cpu_bind_type_t task_plugin_params)
{
	char tmp_str[256];
	if (!str)
		return;

	str[0] = '\0';

	/* Non CPUBIND parameters */
	if (task_plugin_params & OOM_KILL_STEP)
		strcat(str, "OOMKillStep,");
	if (task_plugin_params & SLURMD_OFF_SPEC)
		strcat(str, "SlurmdOffSpec,");

	slurm_sprint_cpu_bind_type(tmp_str, task_plugin_params);
	/*
	 * If we got something from the cpubind parameters append it to the
	 * existing string.
	 */
	if (xstrcmp(tmp_str, "(null type)"))
		strcat(str, tmp_str);

	if (*str) {
		/* Ensure we remove a comma */
		if (str[strlen(str) - 1] == ',')
			str[strlen(str) - 1] = '\0';
	} else {
		strcat(str, "(null type)");
	}
}

extern void *slurm_ctl_conf_2_key_pairs(slurm_conf_t *conf)
{
	list_t *ret_list = NULL;
	char tmp_str[256];
	uint32_t cluster_flags = slurmdb_setup_cluster_flags();

	if (!conf)
		return NULL;

	ret_list = list_create(destroy_config_key_pair);

	add_key_pair(ret_list, "AccountingStorageBackupHost", "%s",
		     conf->accounting_storage_backup_host);

	accounting_enforce_string(conf->accounting_storage_enforce,
				  tmp_str, sizeof(tmp_str));
	add_key_pair(ret_list, "AccountingStorageEnforce", "%s", tmp_str);

	add_key_pair(ret_list, "AccountingStorageHost", "%s",
		     conf->accounting_storage_host);

	add_key_pair(ret_list, "AccountingStorageExternalHost", "%s",
		     conf->accounting_storage_ext_host);

	add_key_pair(ret_list, "AccountingStorageParameters", "%s",
		     conf->accounting_storage_params);

	add_key_pair(ret_list, "AccountingStoragePort", "%u",
		     conf->accounting_storage_port);

	add_key_pair(ret_list, "AccountingStorageTRES", "%s",
		     conf->accounting_storage_tres);

	add_key_pair(ret_list, "AccountingStorageType", "%s",
		     conf->accounting_storage_type);

	add_key_pair(ret_list, "AccountingStorageUser", "%s",
		     conf->accounting_storage_user);

	add_key_pair_own(ret_list, "AccountingStoreFlags",
			 _accountingstoreflags(conf->conf_flags));

	add_key_pair(ret_list, "AcctGatherEnergyType", "%s",
		     conf->acct_gather_energy_type);

	add_key_pair(ret_list, "AcctGatherFilesystemType", "%s",
		     conf->acct_gather_filesystem_type);

	add_key_pair(ret_list, "AcctGatherInterconnectType", "%s",
		     conf->acct_gather_interconnect_type);

	add_key_pair(ret_list, "AcctGatherNodeFreq", "%u sec",
		     conf->acct_gather_node_freq);

	add_key_pair(ret_list, "AcctGatherProfileType", "%s",
		     conf->acct_gather_profile_type);

	add_key_pair_bool(ret_list, "AllowSpecResourcesUsage",
			  (conf->conf_flags & CONF_FLAG_ASRU));

	add_key_pair(ret_list, "AuthAltTypes", "%s", conf->authalttypes);

	add_key_pair(ret_list, "AuthAltParameters", "%s", conf->authalt_params);

	add_key_pair(ret_list, "AuthInfo", "%s", conf->authinfo);

	add_key_pair(ret_list, "AuthType", "%s", conf->authtype);

	add_key_pair(ret_list, "BatchStartTimeout", "%u sec",
		     conf->batch_start_timeout);

	add_key_pair(ret_list, "BcastExclude", "%s", conf->bcast_exclude);

	add_key_pair(ret_list, "BcastParameters", "%s", conf->bcast_parameters);

	/* FIXME: this cast is not safe */
	slurm_make_time_str((time_t *)&conf->boot_time,
			    tmp_str, sizeof(tmp_str));
	add_key_pair(ret_list, "BOOT_TIME", "%s", tmp_str);

	add_key_pair(ret_list, "BurstBufferType", "%s", conf->bb_type);

	add_key_pair(ret_list, "CertmgrParameters", "%s", conf->certmgr_params);
	add_key_pair(ret_list, "CertmgrType", "%s", conf->certmgr_type);

	add_key_pair(ret_list, "CliFilterPlugins", "%s",
		     conf->cli_filter_plugins);

	add_key_pair(ret_list, "ClusterName", "%s", conf->cluster_name);

	add_key_pair(ret_list, "CommunicationParameters", "%s",
		     conf->comm_params);

	add_key_pair(ret_list, "CompleteWait", "%u sec",
		     conf->complete_wait);

	cpu_freq_to_string(tmp_str, sizeof(tmp_str), conf->cpu_freq_def);
	add_key_pair(ret_list, "CpuFreqDef", "%s", tmp_str);

	cpu_freq_govlist_to_string(tmp_str, sizeof(tmp_str),
				   conf->cpu_freq_govs);
	add_key_pair(ret_list, "CpuFreqGovernors", "%s", tmp_str);

	add_key_pair(ret_list, "CredType", "%s", conf->cred_type);
	add_key_pair(ret_list, "DataParserParameters", "%s",
		     conf->data_parser_parameters);

	add_key_pair_own(ret_list, "DebugFlags",
			 debug_flags2str(conf->debug_flags));

	if (conf->def_mem_per_cpu == INFINITE64) {
		add_key_pair(ret_list, "DefMemPerNode", "%s", "UNLIMITED");
	} else if (conf->def_mem_per_cpu & MEM_PER_CPU) {
		add_key_pair(ret_list, "DefMemPerCPU", "%"PRIu64,
			     (conf->def_mem_per_cpu & (~MEM_PER_CPU)));
	} else if (conf->def_mem_per_cpu) {
		add_key_pair(ret_list, "DefMemPerNode", "%"PRIu64,
			     conf->def_mem_per_cpu);
	} else {
		add_key_pair(ret_list, "DefMemPerNode", "%s", "UNLIMITED");
	}

	add_key_pair(ret_list, "DependencyParameters", "%s",
		     conf->dependency_params);

	add_key_pair_bool(ret_list, "DisableRootJobs",
			  (conf->conf_flags & CONF_FLAG_DRJ));

	add_key_pair(ret_list, "EioTimeout", "%u", conf->eio_timeout);

	add_key_pair(ret_list, "EnforcePartLimits", "%s",
		     parse_part_enforce_type_2str(conf->enforce_part_limits));

	for (int i = 0; i < conf->epilog_cnt; i++) {
		char *key = xstrdup_printf("Epilog[%d]", i);
		add_key_pair(ret_list, key, "%s", conf->epilog[i]);
		xfree(key);
	}

	add_key_pair(ret_list, "EpilogMsgTime", "%u usec",
		     conf->epilog_msg_time);

	for (int i = 0; i < conf->epilog_slurmctld_cnt; i++) {
		char *key = xstrdup_printf("EpilogSlurmctld[%d]", i);
		add_key_pair(ret_list, key, "%s", conf->epilog_slurmctld[i]);
		xfree(key);
	}

	if (xstrcmp(conf->priority_type, "priority/basic")) {
		add_key_pair(ret_list, "FairShareDampeningFactor", "%u",
			     conf->fs_dampening_factor);
	}

	add_key_pair(ret_list, "FederationParameters", "%s", conf->fed_params);

	add_key_pair(ret_list, "FirstJobId", "%u", conf->first_job_id);

	add_key_pair(ret_list, "GetEnvTimeout", "%u sec",
		     conf->get_env_timeout);

	add_key_pair(ret_list, "GresTypes", "%s", conf->gres_plugins);

	add_key_pair(ret_list, "GpuFreqDef", "%s", conf->gpu_freq_def);

	add_key_pair(ret_list, "GroupUpdateForce", "%u", conf->group_force);

	add_key_pair(ret_list, "GroupUpdateTime", "%u sec", conf->group_time);

	if (conf->hash_val != NO_VAL) {
		if (conf->hash_val == slurm_conf.hash_val)
			snprintf(tmp_str, sizeof(tmp_str), "Match");
		else
			snprintf(tmp_str, sizeof(tmp_str),
				 "Different Ours=0x%x Slurmctld=0x%x",
				 slurm_conf.hash_val, conf->hash_val);
		add_key_pair(ret_list, "HASH_VAL", "%s", tmp_str);
	}

	add_key_pair(ret_list, "HashPlugin", "%s", conf->hash_plugin);

	add_key_pair(ret_list, "HealthCheckInterval", "%u sec",
		     conf->health_check_interval);

	add_key_pair_own(ret_list, "HealthCheckNodeState",
			 health_check_node_state_str(
				conf->health_check_node_state));

	add_key_pair(ret_list, "HealthCheckProgram", "%s",
		     conf->health_check_program);

	add_key_pair(ret_list, "InactiveLimit", "%u sec",
		     conf->inactive_limit);

	add_key_pair(ret_list, "InteractiveStepOptions", "%s",
		     conf->interactive_step_opts);

	add_key_pair(ret_list, "JobAcctGatherFrequency", "%s",
		     conf->job_acct_gather_freq);

	add_key_pair(ret_list, "JobAcctGatherType", "%s",
		     conf->job_acct_gather_type);

	add_key_pair(ret_list, "JobAcctGatherParams", "%s",
		     conf->job_acct_gather_params);

	add_key_pair(ret_list, "JobCompHost", "%s", conf->job_comp_host);

	add_key_pair(ret_list, "JobCompLoc", "%s", conf->job_comp_loc);

	add_key_pair(ret_list, "JobCompParams", "%s", conf->job_comp_params);

	add_key_pair(ret_list, "JobCompPort", "%u", conf->job_comp_port);

	add_key_pair(ret_list, "JobCompType", "%s", conf->job_comp_type);

	add_key_pair(ret_list, "JobCompUser", "%s", conf->job_comp_user);

	add_key_pair(ret_list, "JobContainerType", "%s",
		     conf->job_container_plugin);

	add_key_pair_own(ret_list, "JobDefaults",
			 job_defaults_str(conf->job_defaults_list));

	add_key_pair(ret_list, "JobFileAppend", "%u", conf->job_file_append);

	add_key_pair(ret_list, "JobRequeue", "%u", conf->job_requeue);

	add_key_pair(ret_list, "JobSubmitPlugins", "%s",
		     conf->job_submit_plugins);

	add_key_pair(ret_list, "KillOnBadExit", "%u", conf->kill_on_bad_exit);

	add_key_pair(ret_list, "KillWait", "%u sec", conf->kill_wait);

	add_key_pair(ret_list, "LaunchParameters", "%s", conf->launch_params);

	add_key_pair(ret_list, "Licenses", "%s", conf->licenses);

	add_key_pair_own(ret_list, "LogTimeFormat", _logfmtstr(conf->log_fmt));

	add_key_pair(ret_list, "MailDomain", "%s", conf->mail_domain);

	add_key_pair(ret_list, "MailProg", "%s", conf->mail_prog);

	add_key_pair(ret_list, "MaxArraySize", "%u", conf->max_array_sz);

	add_key_pair(ret_list, "MaxBatchRequeue", "%u",
		     conf->max_batch_requeue);

	add_key_pair(ret_list, "MaxDBDMsgs", "%u", conf->max_dbd_msgs);

	add_key_pair(ret_list, "MaxJobCount", "%u", conf->max_job_cnt);

	add_key_pair(ret_list, "MaxJobId", "%u", conf->max_job_id);

	if (conf->max_mem_per_cpu == INFINITE64) {
		add_key_pair(ret_list, "MaxMemPerNode", "UNLIMITED");
	} else if (conf->max_mem_per_cpu & MEM_PER_CPU) {
		add_key_pair(ret_list, "MaxMemPerCPU", "%"PRIu64,
			     (conf->max_mem_per_cpu & (~MEM_PER_CPU)));
	} else if (conf->max_mem_per_cpu) {
		add_key_pair(ret_list, "MaxMemPerNode", "%"PRIu64,
			     conf->max_mem_per_cpu);
	} else {
		add_key_pair(ret_list, "MaxMemPerNode", "UNLIMITED");
	}

	add_key_pair(ret_list, "MaxNodeCount", "%u", conf->max_node_cnt);

	add_key_pair(ret_list, "MaxStepCount", "%u", conf->max_step_cnt);

	add_key_pair(ret_list, "MaxTasksPerNode", "%u",
		     conf->max_tasks_per_node);

	add_key_pair(ret_list, "MCSPlugin", "%s", conf->mcs_plugin);

	add_key_pair(ret_list, "MCSParameters", "%s", conf->mcs_plugin_params);

	add_key_pair(ret_list, "MessageTimeout", "%u sec", conf->msg_timeout);

	add_key_pair(ret_list, "MinJobAge", "%u sec", conf->min_job_age);

	add_key_pair(ret_list, "MpiDefault", "%s", conf->mpi_default);

	add_key_pair(ret_list, "MpiParams", "%s", conf->mpi_params);

	if (cluster_flags & CLUSTER_FLAG_MULTSD)
		add_key_pair_bool(ret_list, "MULTIPLE_SLURMD", true);

	add_key_pair(ret_list, "NEXT_JOB_ID", "%u", conf->next_job_id);

	add_key_pair(ret_list, "NodeFeaturesPlugins", "%s",
		     conf->node_features_plugins);

	if (conf->over_time_limit == INFINITE16)
		add_key_pair(ret_list, "OverTimeLimit", "UNLIMITED");
	else
		add_key_pair(ret_list, "OverTimeLimit", "%u min",
			     conf->over_time_limit);

	add_key_pair(ret_list, "PluginDir", "%s", conf->plugindir);

	add_key_pair(ret_list, "PlugStackConfig", "%s", conf->plugstack);

	add_key_pair(ret_list, "PreemptMode", "%s",
		     preempt_mode_string(conf->preempt_mode));

	add_key_pair(ret_list, "PreemptParameters", "%s", conf->preempt_params);

	add_key_pair(ret_list, "PreemptType", "%s", conf->preempt_type);

	if (conf->preempt_exempt_time == INFINITE) {
		add_key_pair(ret_list, "PreemptExemptTime", "NONE");
	} else {
		secs2time_str(conf->preempt_exempt_time,
			      tmp_str, sizeof(tmp_str));
		add_key_pair(ret_list, "PreemptExemptTime", "%s", tmp_str);
	}

	add_key_pair(ret_list, "PrEpParameters", "%s", conf->prep_params);

	add_key_pair(ret_list, "PrEpPlugins", "%s", conf->prep_plugins);

	add_key_pair(ret_list, "PriorityParameters", "%s",
		     conf->priority_params);

	add_key_pair(ret_list, "PrioritySiteFactorParameters", "%s",
		     conf->site_factor_params);

	add_key_pair(ret_list, "PrioritySiteFactorPlugin", "%s",
		     conf->site_factor_plugin);


	if (!xstrcmp(conf->priority_type, "priority/basic")) {
		add_key_pair(ret_list, "PriorityType", "%s",
			     conf->priority_type);
	} else {
		secs2time_str((time_t) conf->priority_decay_hl,
			      tmp_str, sizeof(tmp_str));
		add_key_pair(ret_list, "PriorityDecayHalfLife", "%s", tmp_str);

		secs2time_str((time_t) conf->priority_calc_period,
			      tmp_str, sizeof(tmp_str));
		add_key_pair(ret_list, "PriorityCalcPeriod", "%s", tmp_str);

		add_key_pair_bool(ret_list, "PriorityFavorSmall",
				  conf->priority_favor_small);

		add_key_pair_own(ret_list, "PriorityFlags",
				 priority_flags_string(conf->priority_flags));

		secs2time_str((time_t) conf->priority_max_age,
			      tmp_str, sizeof(tmp_str));
		add_key_pair(ret_list, "PriorityMaxAge", "%s", tmp_str);

		add_key_pair(ret_list, "PriorityType", "%s",
			     conf->priority_type);

		add_key_pair(ret_list, "PriorityUsageResetPeriod", "%s",
			     _reset_period_str(conf->priority_reset_period));

		add_key_pair(ret_list, "PriorityWeightAge", "%u",
			     conf->priority_weight_age);

		add_key_pair(ret_list, "PriorityWeightAssoc", "%u",
			     conf->priority_weight_assoc);

		add_key_pair(ret_list, "PriorityWeightFairShare", "%u",
			     conf->priority_weight_fs);

		add_key_pair(ret_list, "PriorityWeightJobSize", "%u",
			     conf->priority_weight_js);

		add_key_pair(ret_list, "PriorityWeightPartition", "%u",
			     conf->priority_weight_part);

		add_key_pair(ret_list, "PriorityWeightQOS", "%u",
			     conf->priority_weight_qos);

		add_key_pair(ret_list, "PriorityWeightTRES", "%s",
			     conf->priority_weight_tres);
	}

	private_data_string(conf->private_data, tmp_str, sizeof(tmp_str));
	add_key_pair(ret_list, "PrivateData", "%s", tmp_str);

	add_key_pair(ret_list, "ProctrackType", "%s", conf->proctrack_type);

	for (int i = 0; i < conf->prolog_cnt; i++) {
		char *key = xstrdup_printf("Prolog[%d]", i);
		add_key_pair(ret_list, key, "%s", conf->prolog[i]);
		xfree(key);
	}

	add_key_pair(ret_list, "PrologEpilogTimeout", "%u",
		     conf->prolog_epilog_timeout);

	for (int i = 0; i < conf->prolog_slurmctld_cnt; i++) {
		char *key = xstrdup_printf("PrologSlurmctld[%d]", i);
		add_key_pair(ret_list, key, "%s", conf->prolog_slurmctld[i]);
		xfree(key);
	}

	add_key_pair_own(ret_list, "PrologFlags",
			 prolog_flags2str(conf->prolog_flags));

	add_key_pair(ret_list, "PropagatePrioProcess", "%u",
		     conf->propagate_prio_process);

	add_key_pair(ret_list, "PropagateResourceLimits", "%s",
		     conf->propagate_rlimits);

	add_key_pair(ret_list, "PropagateResourceLimitsExcept", "%s",
		     conf->propagate_rlimits_except);

	add_key_pair(ret_list, "RebootProgram", "%s", conf->reboot_program);

	add_key_pair_own(ret_list, "ReconfigFlags",
			 reconfig_flags2str(conf->reconfig_flags));

	add_key_pair(ret_list, "RequeueExit", "%s", conf->requeue_exit);

	add_key_pair(ret_list, "RequeueExitHold", "%s",
		     conf->requeue_exit_hold);

	add_key_pair(ret_list, "ResumeFailProgram", "%s",
		     conf->resume_fail_program);

	add_key_pair(ret_list, "ResumeProgram", "%s", conf->resume_program);

	add_key_pair(ret_list, "ResumeRate", "%u nodes/min",
		     conf->resume_rate);

	add_key_pair(ret_list, "ResumeTimeout", "%u sec",
		     conf->resume_timeout);

	add_key_pair(ret_list, "ResvEpilog", "%s", conf->resv_epilog);

	if (conf->resv_over_run == INFINITE16)
		add_key_pair(ret_list, "ResvOverRun", "UNLIMITED");
	else
		add_key_pair(ret_list, "ResvOverRun", "%u min",
			     conf->resv_over_run);

	add_key_pair(ret_list, "ResvProlog", "%s", conf->resv_prolog);

	add_key_pair(ret_list, "ReturnToService", "%u", conf->ret2service);

	add_key_pair(ret_list, "SchedulerParameters", "%s", conf->sched_params);

	add_key_pair(ret_list, "SchedulerTimeSlice", "%u sec",
		     conf->sched_time_slice);

	add_key_pair(ret_list, "SchedulerType", "%s", conf->schedtype);

	add_key_pair(ret_list, "ScronParameters", "%s", conf->scron_params);

	add_key_pair(ret_list, "SelectType", "%s", conf->select_type);

	if (conf->select_type_param) {
		add_key_pair(ret_list, "SelectTypeParameters", "%s",
			     select_type_param_string(conf->select_type_param));
	}

	add_key_pair(ret_list, "SlurmUser", "%s(%u)",
		     conf->slurm_user_name, conf->slurm_user_id);

	add_key_pair(ret_list, "SlurmctldAddr", "%s", conf->slurmctld_addr);

	add_key_pair(ret_list, "SlurmctldDebug", "%s",
		     log_num2string(conf->slurmctld_debug));

	for (int i = 0; i < conf->control_cnt; i++) {
		char *key = xstrdup_printf("SlurmctldHost[%d]", i);
		if (xstrcmp(conf->control_machine[i],
			    conf->control_addr[i])) {
			add_key_pair(ret_list, key, "%s(%s)",
				     conf->control_machine[i],
				     conf->control_addr[i]);
		} else {
			add_key_pair(ret_list, key, "%s",
				     conf->control_machine[i]);
		}
		xfree(key);
	}

	add_key_pair(ret_list, "SlurmctldLogFile", "%s",
		     conf->slurmctld_logfile);

	if (conf->slurmctld_port_count > 1) {
		uint32_t high_port = conf->slurmctld_port;
		high_port += (conf->slurmctld_port_count - 1);
		add_key_pair(ret_list, "SlurmctldPort", "%u-%u",
			     conf->slurmctld_port, high_port);
	} else {
		add_key_pair(ret_list, "SlurmctldPort", "%u",
			     conf->slurmctld_port);
	}

	add_key_pair(ret_list, "SlurmctldSyslogDebug", "%s",
		     log_num2string(conf->slurmctld_syslog_debug));

	add_key_pair(ret_list, "SlurmctldPrimaryOffProg", "%s",
		     conf->slurmctld_primary_off_prog);

	add_key_pair(ret_list, "SlurmctldPrimaryOnProg", "%s",
		     conf->slurmctld_primary_on_prog);

	add_key_pair(ret_list, "SlurmctldTimeout", "%u sec",
		     conf->slurmctld_timeout);

	add_key_pair(ret_list, "SlurmctldParameters", "%s",
		     conf->slurmctld_params);

	add_key_pair(ret_list, "SlurmdDebug", "%s",
		     log_num2string(conf->slurmd_debug));

	add_key_pair(ret_list, "SlurmdLogFile", "%s", conf->slurmd_logfile);

	add_key_pair(ret_list, "SlurmdParameters", "%s", conf->slurmd_params);

	add_key_pair(ret_list, "SlurmdPidFile", "%s", conf->slurmd_pidfile);

	add_key_pair(ret_list, "SlurmdPort", "%u", conf->slurmd_port);

	add_key_pair(ret_list, "SlurmdSpoolDir", "%s", conf->slurmd_spooldir);

	add_key_pair(ret_list, "SlurmdSyslogDebug", "%s",
		     log_num2string(conf->slurmd_syslog_debug));

	add_key_pair(ret_list, "SlurmdTimeout", "%u sec", conf->slurmd_timeout);

	add_key_pair(ret_list, "SlurmdUser", "%s(%u)",
		     conf->slurmd_user_name, conf->slurmd_user_id);

	add_key_pair(ret_list, "SlurmSchedLogFile", "%s", conf->sched_logfile);

	add_key_pair(ret_list, "SlurmSchedLogLevel", "%u",
		     conf->sched_log_level);

	add_key_pair(ret_list, "SlurmctldPidFile", "%s",
		     conf->slurmctld_pidfile);

	add_key_pair(ret_list, "SLURM_CONF", "%s", conf->slurm_conf);

	add_key_pair(ret_list, "SLURM_VERSION", "%s", conf->version);

	add_key_pair(ret_list, "SrunEpilog", "%s", conf->srun_epilog);

	if (conf->srun_port_range)
		add_key_pair(ret_list, "SrunPortRange", "%u-%u",
			     conf->srun_port_range[0],
			     conf->srun_port_range[1]);
	else
		add_key_pair(ret_list, "SrunPortRange", "0-0");

	add_key_pair(ret_list, "SrunProlog", "%s", conf->srun_prolog);

	add_key_pair(ret_list, "StateSaveLocation", "%s",
		     conf->state_save_location);

	add_key_pair(ret_list, "SuspendExcNodes", "%s",
		     conf->suspend_exc_nodes);

	add_key_pair(ret_list, "SuspendExcParts", "%s",
		     conf->suspend_exc_parts);

	add_key_pair(ret_list, "SuspendExcStates", "%s",
		     conf->suspend_exc_states);

	add_key_pair(ret_list, "SuspendProgram", "%s", conf->suspend_program);

	add_key_pair(ret_list, "SuspendRate", "%u nodes/min",
		     conf->suspend_rate);

	if (conf->suspend_time == INFINITE) {
		snprintf(tmp_str, sizeof(tmp_str), "INFINITE");
	} else {
		snprintf(tmp_str, sizeof(tmp_str), "%u sec",
			 conf->suspend_time);
	}
	add_key_pair(ret_list, "SuspendTime", "%s", tmp_str);

	if (conf->suspend_timeout == 0) {
		snprintf(tmp_str, sizeof(tmp_str), "NONE");
	} else {
		snprintf(tmp_str, sizeof(tmp_str), "%u sec",
			 conf->suspend_timeout);
	}
	add_key_pair(ret_list, "SuspendTimeout", "%s", tmp_str);

	add_key_pair(ret_list, "SwitchParameters", "%s", conf->switch_param);

	add_key_pair(ret_list, "SwitchType", "%s", conf->switch_type);

	add_key_pair(ret_list, "TaskEpilog", "%s", conf->task_epilog);

	add_key_pair(ret_list, "TaskPlugin", "%s", conf->task_plugin);

	_sprint_task_plugin_params(tmp_str, conf->task_plugin_param);
	add_key_pair(ret_list, "TaskPluginParam", "%s", tmp_str);

	add_key_pair(ret_list, "TaskProlog", "%s", conf->task_prolog);

	add_key_pair(ret_list, "TCPTimeout", "%u sec", conf->tcp_timeout);

	add_key_pair(ret_list, "TLSParameters", "%s", conf->tls_params);

	add_key_pair(ret_list, "TLSType", "%s", conf->tls_type);

	add_key_pair(ret_list, "TmpFS", "%s", conf->tmp_fs);

	add_key_pair(ret_list, "TopologyParam", "%s", conf->topology_param);

	add_key_pair(ret_list, "TopologyPlugin", "%s", conf->topology_plugin);

	add_key_pair_bool(ret_list, "TrackWCKey",
			  (conf->conf_flags & CONF_FLAG_WCKEY));

	add_key_pair(ret_list, "TreeWidth", "%u", conf->tree_width);

	add_key_pair_bool(ret_list, "UsePam",
			  (conf->conf_flags & CONF_FLAG_PAM));

	add_key_pair(ret_list, "UnkillableStepProgram", "%s",
		     conf->unkillable_program);

	add_key_pair(ret_list, "UnkillableStepTimeout", "%u sec",
		     conf->unkillable_timeout);

	add_key_pair(ret_list, "VSizeFactor", "%u percent",
		     conf->vsize_factor);

	add_key_pair(ret_list, "WaitTime", "%u sec", conf->wait_time);

	add_key_pair(ret_list, "X11Parameters", "%s", conf->x11_params);

	return ret_list;
}

/*
 * slurm_load_ctl_conf - issue RPC to get slurm control configuration
 *	information if changed since update_time
 * IN update_time - time of current configuration data
 * IN slurm_ctl_conf_ptr - place to store slurm control configuration
 *	pointer
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 * NOTE: free the response using slurm_free_ctl_conf
 */
int slurm_load_ctl_conf(time_t update_time, slurm_conf_t **confp)
{
	int rc;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;
	last_update_msg_t req;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	memset(&req, 0, sizeof(req));
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
	return SLURM_SUCCESS;
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
			if (slurm_conf_get_addr(this_addr, &req_msg.address,
						req_msg.flags)) {
				/*
				 * The node isn't in the conf, see if the
				 * controller has an address for it.
				 */
				slurm_node_alias_addrs_t *alias_addrs;
				if (!slurm_get_node_alias_addrs(this_addr,
								&alias_addrs)) {
					add_remote_nodes_to_conf_tbls(
						alias_addrs->node_list,
						alias_addrs->node_addrs);
				}
				slurm_free_node_alias_addrs(alias_addrs);
				slurm_conf_get_addr(this_addr, &req_msg.address,
						    req_msg.flags);
			}
		} else {
			this_addr = "localhost";
			slurm_set_addr(&req_msg.address, slurm_conf.slurmd_port,
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
		slurm_set_addr(&req_msg.address, slurm_conf.slurmd_port,
			       this_addr);
		xfree(this_addr);
	}
	req_msg.msg_type = REQUEST_DAEMON_STATUS;
	req_msg.data     = NULL;
	slurm_msg_set_r_uid(&req_msg, SLURM_AUTH_UID_ANY);

	rc = slurm_send_recv_node_msg(&req_msg, &resp_msg, 0);

	if (rc != SLURM_SUCCESS) {
		error("slurm_slurmd_info: %m");
		if (resp_msg.auth_cred)
			auth_g_destroy(resp_msg.auth_cred);
		return SLURM_ERROR;
	}
	if (resp_msg.auth_cred)
		auth_g_destroy(resp_msg.auth_cred);

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

	return SLURM_SUCCESS;
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
	char time_str[256];

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
	list_t *config_list = key_pairs;
	list_itr_t *iter = NULL;
	/* define lists of specific configuration sections */
	list_t *other_list;
	list_t *control_list;
	list_t *accounting_list;
	list_t *logging_list;
	list_t *power_list;
	list_t *sched_list;
	list_t *topology_list;
	list_t *timers_list;
	list_t *debug_list;
	list_t *proepilog_list;
	list_t *resconf_list;
	list_t *proctrac_list;

	if (!config_list)
		return;

	other_list = list_create(xfree_ptr);
	control_list = list_create(xfree_ptr);
	accounting_list = list_create(xfree_ptr);
	logging_list = list_create(xfree_ptr);
	power_list = list_create(xfree_ptr);
	sched_list = list_create(xfree_ptr);
	topology_list = list_create(xfree_ptr);
	timers_list = list_create(xfree_ptr);
	debug_list = list_create(xfree_ptr);
	proepilog_list = list_create(xfree_ptr);
	resconf_list = list_create(xfree_ptr);
	proctrac_list = list_create(xfree_ptr);

	iter = list_iterator_create(config_list);
	while ((key_pair = list_next(iter))) {
		/* Ignore ENV variables in config_list; they'll
		 * cause problems in an active slurm.conf */
		if (!xstrcmp(key_pair->name, "BOOT_TIME") ||
		    !xstrcmp(key_pair->name, "HASH_VAL") ||
		    !xstrcmp(key_pair->name, "MULTIPLE_SLURMD") ||
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
		    (!xstrcasecmp(key_pair->name, "DefMemPerNode") &&
		     !xstrcasecmp(key_pair->value, "UNLIMITED")) ||
		    ((!xstrcasecmp(key_pair->name, "SlurmctldSyslogDebug") ||
		      !xstrcasecmp(key_pair->name, "SlurmdSyslogDebug")) &&
		     !xstrcasecmp(key_pair->value, "unknown")) ||
		    (!xstrcasecmp(key_pair->name, "CpuFreqDef") &&
		     !xstrcasecmp(key_pair->value, "Unknown"))) {
			temp = xstrdup_printf("#%s=", key_pair->name);
			debug("Commenting out %s=%s",
			      key_pair->name,
			      key_pair->value);
		} else {
			if ((!xstrcasecmp(key_pair->name, "Epilog")) ||
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
			strtok(key_pair->name, "[");
			if (strchr(temp, ' '))
				temp = xstrdup_printf("%s=\"%s\"",
						      key_pair->name, temp);
			else
				temp = xstrdup_printf("%s=%s",
						      key_pair->name, temp);
		}

		if (!xstrcasecmp(key_pair->name, "ControlMachine") ||
		    !xstrcasecmp(key_pair->name, "ControlAddr") ||
		    !xstrcasecmp(key_pair->name, "ClusterName") ||
		    !xstrcasecmp(key_pair->name, "SlurmUser") ||
		    !xstrcasecmp(key_pair->name, "SlurmdUser") ||
		    !xstrcasecmp(key_pair->name, "SlurmctldHost") ||
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
		    !xstrcasecmp(key_pair->name, "AccountingStoragePort") ||
		    !xstrcasecmp(key_pair->name, "AccountingStorageType") ||
		    !xstrcasecmp(key_pair->name, "AccountingStorageUser") ||
		    !xstrcasecmp(key_pair->name, "AccountingStoreFlags") ||
		    !xstrcasecmp(key_pair->name, "AcctGatherEnergyType") ||
		    !xstrcasecmp(key_pair->name, "AcctGatherFilesystemType") ||
		    !xstrcasecmp(key_pair->name, "AcctGatherInterconnectType") ||
		    !xstrcasecmp(key_pair->name, "AcctGatherNodeFreq") ||
		    !xstrcasecmp(key_pair->name, "AcctGatherProfileType") ||
		    !xstrcasecmp(key_pair->name, "JobAcctGatherFrequency") ||
		    !xstrcasecmp(key_pair->name, "JobAcctGatherType")) {
			list_append(accounting_list, temp);
			continue;
		}

		if (!xstrcasecmp(key_pair->name, "SuspendExcNodes") ||
		    !xstrcasecmp(key_pair->name, "SuspendExcParts") ||
		    !xstrcasecmp(key_pair->name, "SuspendExcStates") ||
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
		    !xstrcasecmp(key_pair->name, "PreemptParameters") ||
		    !xstrcasecmp(key_pair->name, "PreemptType") ||
		    !xstrcasecmp(key_pair->name, "PreemptExemptTime") ||
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
	list_t *config_list = key_pairs;
	list_itr_t *iter = NULL;
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
