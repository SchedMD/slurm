/*****************************************************************************\
 *  read_config.c - read the overall slurm configuration file
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2008 Vijay Ramasubramanian.
 *  Portions Copyright (C) 2010-2016 SchedMD <https://www.schedmd.com>.
 *  Portions (boards) copyright (C) 2012 Bull, <rod.schultz@bull.com>
 *  Portions (route) copyright (C) 2014 Bull, <rod.schultz@bull.com>
 *  Copyright (C) 2012-2013 Los Alamos National Security, LLC.
 *  Copyright (C) 2013 Intel, Inc.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
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

#include <arpa/inet.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/cpu_frequency.h"
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/node_conf.h"
#include "src/common/node_features.h"
#include "src/common/parse_config.h"
#include "src/common/parse_time.h"
#include "src/common/proc_args.h"
#include "src/common/read_config.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_resource_info.h"
#include "src/common/slurm_rlimits_info.h"
#include "src/common/slurm_selecttype_info.h"
#include "src/common/strlcpy.h"
#include "src/common/uid.h"
#include "src/common/util-net.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/*
** Define slurm-specific aliases for use by plugins, see slurm_xlator.h
** for details.
 */
strong_alias(destroy_config_plugin_params, slurm_destroy_config_plugin_params);
strong_alias(destroy_config_key_pair, slurm_destroy_config_key_pair);
strong_alias(get_extra_conf_path, slurm_get_extra_conf_path);
strong_alias(sort_key_pairs, slurm_sort_key_pairs);
strong_alias(run_in_daemon, slurm_run_in_daemon);

/*
 * Instantiation of the "extern slurm_ctl_conf_t slurmctld_conf" and
 * "bool ignore_state_errors" found in slurmctld.h
 */
slurm_ctl_conf_t slurmctld_conf;
bool ignore_state_errors = false;

#ifndef NDEBUG
uint16_t drop_priv_flag = 0;
#endif

static pthread_mutex_t conf_lock = PTHREAD_MUTEX_INITIALIZER;
static s_p_hashtbl_t *conf_hashtbl = NULL;
static slurm_ctl_conf_t *conf_ptr = &slurmctld_conf;
static bool conf_initialized = false;
static s_p_hashtbl_t *default_frontend_tbl;
static s_p_hashtbl_t *default_nodename_tbl;
static s_p_hashtbl_t *default_partition_tbl;
static bool	local_test_config = false;
static int	local_test_config_rc = SLURM_SUCCESS;

inline static void _normalize_debug_level(uint16_t *level);
static int _init_slurm_conf(const char *file_name);

#define NAME_HASH_LEN 512
typedef struct names_ll_s {
	char *alias;	/* NodeName */
	char *hostname;	/* NodeHostname */
	char *address;	/* NodeAddr */
	uint16_t port;
	uint16_t cpus;
	uint16_t boards;
	uint16_t sockets;
	uint16_t cores;
	uint16_t threads;
	char *cpu_spec_list;
	uint16_t core_spec_cnt;
	uint64_t mem_spec_limit;
	slurm_addr_t addr;
	bool addr_initialized;
	struct names_ll_s *next_alias;
	struct names_ll_s *next_hostname;
} names_ll_t;
static bool nodehash_initialized = false;
static names_ll_t *host_to_node_hashtbl[NAME_HASH_LEN] = {NULL};
static names_ll_t *node_to_host_hashtbl[NAME_HASH_LEN] = {NULL};

typedef struct slurm_conf_server {
	char *hostname;
	char *addr;
} slurm_conf_server_t;

static void _destroy_nodename(void *ptr);
static int _parse_frontend(void **dest, slurm_parser_enum_t type,
			   const char *key, const char *value,
			   const char *line, char **leftover);
static int _parse_nodename(void **dest, slurm_parser_enum_t type,
			   const char *key, const char *value,
			   const char *line, char **leftover);
static bool _is_valid_path(char *path, char *msg);
static int _parse_partitionname(void **dest, slurm_parser_enum_t type,
				const char *key, const char *value,
				const char *line, char **leftover);
static void _destroy_partitionname(void *ptr);
static int _parse_downnodes(void **dest, slurm_parser_enum_t type,
			    const char *key, const char *value,
			    const char *line, char **leftover);
static void _destroy_downnodes(void *ptr);

static int _load_slurmctld_host(slurm_ctl_conf_t *conf);
static int _parse_slurmctld_host(void **dest, slurm_parser_enum_t type,
				 const char *key, const char *value,
				 const char *line, char **leftover);
static void _destroy_slurmctld_host(void *ptr);

static int _defunct_option(void **dest, slurm_parser_enum_t type,
			   const char *key, const char *value,
			   const char *line, char **leftover);
static int _validate_and_set_defaults(slurm_ctl_conf_t *conf,
				      s_p_hashtbl_t *hashtbl);
static uint16_t *_parse_srun_ports(const char *);

s_p_options_t slurm_conf_options[] = {
	{"AccountingStorageTRES", S_P_STRING},
	{"AccountingStorageEnforce", S_P_STRING},
	{"AccountingStorageHost", S_P_STRING},
	{"AccountingStorageBackupHost", S_P_STRING},
	{"AccountingStorageLoc", S_P_STRING},
	{"AccountingStoragePass", S_P_STRING},
	{"AccountingStoragePort", S_P_UINT32},
	{"AccountingStorageType", S_P_STRING},
	{"AccountingStorageUser", S_P_STRING},
	{"AccountingStoreJobComment", S_P_BOOLEAN},
	{"AcctGatherEnergyType", S_P_STRING},
	{"AcctGatherNodeFreq", S_P_UINT16},
	{"AcctGatherProfileType", S_P_STRING},
	{"AcctGatherInterconnectType", S_P_STRING},
	{"AcctGatherInfinibandType", S_P_STRING},
	{"AcctGatherFilesystemType", S_P_STRING},
	{"AllowSpecResourcesUsage", S_P_BOOLEAN},
	{"AuthInfo", S_P_STRING},
	{"AuthType", S_P_STRING},
	{"BackupAddr", S_P_STRING},
	{"BackupController", S_P_STRING},
	{"BatchStartTimeout", S_P_UINT16},
	{"BurstBufferParameters", S_P_STRING},
	{"BurstBufferType", S_P_STRING},
	{"CacheGroups", S_P_UINT16},
	{"CheckpointType", S_P_STRING},
	{"ChosLoc", S_P_STRING},
	{"CoreSpecPlugin", S_P_STRING},
	{"ClusterName", S_P_STRING},
	{"CommunicationParameters", S_P_STRING},
	{"CompleteWait", S_P_UINT16},
	{"ControlAddr", S_P_STRING},
	{"ControlMachine", S_P_STRING},
	{"CpuFreqDef", S_P_STRING},
	{"CpuFreqGovernors", S_P_STRING},
	{"CryptoType", S_P_STRING},
	{"DebugFlags", S_P_STRING},
	{"DefaultStorageHost", S_P_STRING},
	{"DefaultStorageLoc", S_P_STRING},
	{"DefaultStoragePass", S_P_STRING},
	{"DefaultStoragePort", S_P_UINT32},
	{"DefaultStorageType", S_P_STRING},
	{"DefaultStorageUser", S_P_STRING},
	{"DefCPUPerGPU" , S_P_UINT64},
	{"DefMemPerCPU", S_P_UINT64},
	{"DefMemPerGPU" , S_P_UINT64},
	{"DefMemPerNode", S_P_UINT64},
	{"DisableRootJobs", S_P_BOOLEAN},
	{"EioTimeout", S_P_UINT16},
	{"EnforcePartLimits", S_P_STRING},
	{"Epilog", S_P_STRING},
	{"EpilogMsgTime", S_P_UINT32},
	{"EpilogSlurmctld", S_P_STRING},
	{"ExtSensorsType", S_P_STRING},
	{"ExtSensorsFreq", S_P_UINT16},
	{"FairShareDampeningFactor", S_P_UINT16},
	{"FastSchedule", S_P_UINT16},
	{"FederationParameters", S_P_STRING},
	{"FirstJobId", S_P_UINT32},
	{"GetEnvTimeout", S_P_UINT16},
	{"GresTypes", S_P_STRING},
	{"GroupUpdateForce", S_P_UINT16},
	{"GroupUpdateTime", S_P_UINT16},
	{"HealthCheckInterval", S_P_UINT16},
	{"HealthCheckNodeState", S_P_STRING},
	{"HealthCheckProgram", S_P_STRING},
	{"InactiveLimit", S_P_UINT16},
	{"JobAcctGatherType", S_P_STRING},
	{"JobAcctGatherFrequency", S_P_STRING},
	{"JobAcctGatherParams", S_P_STRING},
	{"JobCheckpointDir", S_P_STRING},
	{"JobCompHost", S_P_STRING},
	{"JobCompLoc", S_P_STRING},
	{"JobCompPass", S_P_STRING},
	{"JobCompPort", S_P_UINT32},
	{"JobCompType", S_P_STRING},
	{"JobContainerType", S_P_STRING},
	{"JobCompUser", S_P_STRING},
	{"JobCredentialPrivateKey", S_P_STRING},
	{"JobCredentialPublicCertificate", S_P_STRING},
	{"JobFileAppend", S_P_UINT16},
	{"JobRequeue", S_P_UINT16},
	{"JobSubmitPlugins", S_P_STRING},
	{"KeepAliveTime", S_P_UINT16},
	{"KillOnBadExit", S_P_UINT16},
	{"KillWait", S_P_UINT16},
	{"LaunchParameters", S_P_STRING},
	{"LaunchType", S_P_STRING},
	{"Layouts", S_P_STRING},
	{"Licenses", S_P_STRING},
	{"LogTimeFormat", S_P_STRING},
	{"MailDomain", S_P_STRING},
	{"MailProg", S_P_STRING},
	{"MaxArraySize", S_P_UINT32},
	{"MaxJobCount", S_P_UINT32},
	{"MaxJobId", S_P_UINT32},
	{"MaxMemPerCPU", S_P_UINT64},
	{"MaxMemPerNode", S_P_UINT64},
	{"MaxStepCount", S_P_UINT32},
	{"MaxTasksPerNode", S_P_UINT16},
	{"MCSParameters", S_P_STRING},
	{"MCSPlugin", S_P_STRING},
	{"MemLimitEnforce", S_P_STRING},
	{"MessageTimeout", S_P_UINT16},
	{"MinJobAge", S_P_UINT32},
	{"MpiDefault", S_P_STRING},
	{"MpiParams", S_P_STRING},
	{"MsgAggregationParams", S_P_STRING},
	{"NodeFeaturesPlugins", S_P_STRING},
	{"OverTimeLimit", S_P_UINT16},
	{"PluginDir", S_P_STRING},
	{"PlugStackConfig", S_P_STRING},
	{"PowerParameters", S_P_STRING},
	{"PowerPlugin", S_P_STRING},
	{"PreemptMode", S_P_STRING},
	{"PreemptType", S_P_STRING},
	{"PriorityDecayHalfLife", S_P_STRING},
	{"PriorityCalcPeriod", S_P_STRING},
	{"PriorityFavorSmall", S_P_BOOLEAN},
	{"PriorityMaxAge", S_P_STRING},
	{"PriorityParameters", S_P_STRING},
	{"PriorityUsageResetPeriod", S_P_STRING},
	{"PriorityType", S_P_STRING},
	{"PriorityFlags", S_P_STRING},
	{"PriorityWeightAge", S_P_UINT32},
	{"PriorityWeightFairshare", S_P_UINT32},
	{"PriorityWeightJobSize", S_P_UINT32},
	{"PriorityWeightPartition", S_P_UINT32},
	{"PriorityWeightQOS", S_P_UINT32},
	{"PriorityWeightTRES", S_P_STRING},
	{"PrivateData", S_P_STRING},
	{"ProctrackType", S_P_STRING},
	{"Prolog", S_P_STRING},
	{"PrologSlurmctld", S_P_STRING},
	{"PrologEpilogTimeout", S_P_UINT16},
	{"PrologFlags", S_P_STRING},
	{"PropagatePrioProcess", S_P_UINT16},
	{"PropagateResourceLimitsExcept", S_P_STRING},
	{"PropagateResourceLimits", S_P_STRING},
	{"RebootProgram", S_P_STRING},
	{"ReconfigFlags", S_P_STRING},
	{"RequeueExit", S_P_STRING},
	{"RequeueExitHold", S_P_STRING},
	{"ResumeFailProgram", S_P_STRING},
	{"ResumeProgram", S_P_STRING},
	{"ResumeRate", S_P_UINT16},
	{"ResumeTimeout", S_P_UINT16},
	{"ResvEpilog", S_P_STRING},
	{"ResvOverRun", S_P_UINT16},
	{"ResvProlog", S_P_STRING},
	{"ReturnToService", S_P_UINT16},
	{"RoutePlugin", S_P_STRING},
	{"SallocDefaultCommand", S_P_STRING},
	{"SbcastParameters", S_P_STRING},
	{"SchedulerAuth", S_P_STRING, _defunct_option},
	{"SchedulerParameters", S_P_STRING},
	{"SchedulerPort", S_P_UINT16},
	{"SchedulerRootFilter", S_P_UINT16},
	{"SchedulerTimeSlice", S_P_UINT16},
	{"SchedulerType", S_P_STRING},
	{"SelectType", S_P_STRING},
	{"SelectTypeParameters", S_P_STRING},
	{"SlurmUser", S_P_STRING},
	{"SlurmdUser", S_P_STRING},
	{"SlurmctldAddr", S_P_STRING},
	{"SlurmctldDebug", S_P_STRING},
	{"SlurmctldLogFile", S_P_STRING},
	{"SlurmctldPidFile", S_P_STRING},
	{"SlurmctldPlugstack", S_P_STRING},
	{"SlurmctldPort", S_P_STRING},
	{"SlurmctldPrimaryOffProg", S_P_STRING},
	{"SlurmctldPrimaryOnProg", S_P_STRING},
	{"SlurmctldSyslogDebug", S_P_STRING},
	{"SlurmctldTimeout", S_P_UINT16},
	{"SlurmctldParameters", S_P_STRING},
	{"SlurmdDebug", S_P_STRING},
	{"SlurmdLogFile", S_P_STRING},
	{"SlurmdParameters", S_P_STRING},
	{"SlurmdPidFile",  S_P_STRING},
	{"SlurmdPort", S_P_UINT32},
	{"SlurmdSpoolDir", S_P_STRING},
	{"SlurmdSyslogDebug", S_P_STRING},
	{"SlurmdTimeout", S_P_UINT16},
	{"SlurmSchedLogFile", S_P_STRING},
	{"SlurmSchedLogLevel", S_P_UINT16},
	{"SrunEpilog", S_P_STRING},
	{"SrunProlog", S_P_STRING},
	{"SrunPortRange", S_P_STRING},
	{"StateSaveLocation", S_P_STRING},
	{"SuspendExcNodes", S_P_STRING},
	{"SuspendExcParts", S_P_STRING},
	{"SuspendProgram", S_P_STRING},
	{"SuspendRate", S_P_UINT16},
	{"SuspendTime", S_P_STRING},
	{"SuspendTimeout", S_P_UINT16},
	{"SwitchType", S_P_STRING},
	{"TaskEpilog", S_P_STRING},
	{"TaskProlog", S_P_STRING},
	{"TaskPlugin", S_P_STRING},
	{"TaskPluginParam", S_P_STRING},
	{"TCPTimeout", S_P_UINT16},
	{"TmpFS", S_P_STRING},
	{"TopologyParam", S_P_STRING},
	{"TopologyPlugin", S_P_STRING},
	{"TrackWCKey", S_P_BOOLEAN},
	{"TreeWidth", S_P_UINT16},
	{"UnkillableStepProgram", S_P_STRING},
	{"UnkillableStepTimeout", S_P_UINT16},
	{"UsePAM", S_P_BOOLEAN},
	{"VSizeFactor", S_P_UINT16},
	{"WaitTime", S_P_UINT16},
	{"X11Parameters", S_P_STRING},

	{"DownNodes", S_P_ARRAY, _parse_downnodes, _destroy_downnodes},
	{"FrontendName", S_P_ARRAY, _parse_frontend, destroy_frontend},
	{"NodeName", S_P_ARRAY, _parse_nodename, _destroy_nodename},
	{"PartitionName", S_P_ARRAY, _parse_partitionname,
	 _destroy_partitionname},
	{"SlurmctldHost", S_P_ARRAY, _parse_slurmctld_host,
	 _destroy_slurmctld_host},

	{NULL}
};

static bool _is_valid_path(char *path, char *msg)
{
	char *saveptr = NULL, *buf, *entry;

	if (path == NULL) {
		error ("is_valid_path: path is NULL!");
		return false;
	}

	buf = xstrdup(path);
	entry = strtok_r(buf, ":", &saveptr);
	while (entry) {
		struct stat st;

		/*
		*  Check to see if current path element is a valid dir
		*/
		if (stat (entry, &st) < 0) {
			error ("%s: %s: %m", msg, entry);
			goto out_false;
		} else if (!S_ISDIR (st.st_mode)) {
			error ("%s: %s: Not a directory", msg, entry);
			goto out_false;
		}
		/*
		*  Otherwise path element is valid, continue..
		*/
		entry = strtok_r(NULL, ":", &saveptr);
	}

	xfree(buf);
 	return true;

out_false:
	xfree(buf);
	return false;
}

static int _defunct_option(void **dest, slurm_parser_enum_t type,
			  const char *key, const char *value,
			  const char *line, char **leftover)
{
	error("The option \"%s\" is defunct, see man slurm.conf.", key);
	return 0;
}

/* Used to get the general name of the machine, used primarily
 * for bluegene systems.  Not in general use because some systems
 * have multiple prefix's such as foo[1-1000],bar[1-1000].
 */
/* Caller must be holding slurm_conf_lock() */
static void _set_node_prefix(const char *nodenames)
{
	int i;
	char *tmp;

	xassert(nodenames != NULL);
	for (i = 1; nodenames[i] != '\0'; i++) {
		if ((nodenames[i-1] == '[')
		   || (nodenames[i-1] <= '9'
		       && nodenames[i-1] >= '0'))
			break;
	}

	if (i == 1) {
		error("In your Node definition in your slurm.conf you "
		      "gave a nodelist '%s' without a prefix.  "
		      "Please try something like bg%s.", nodenames, nodenames);
	}

	xfree(conf_ptr->node_prefix);
	if (nodenames[i] == '\0')
		conf_ptr->node_prefix = xstrdup(nodenames);
	else {
		tmp = xmalloc(sizeof(char)*i+1);
		snprintf(tmp, i, "%s", nodenames);
		conf_ptr->node_prefix = tmp;
		tmp = NULL;
	}
	debug3("Prefix is %s %s %d", conf_ptr->node_prefix, nodenames, i);
}

static int _parse_frontend(void **dest, slurm_parser_enum_t type,
			   const char *key, const char *value,
			   const char *line, char **leftover)
{
	s_p_hashtbl_t *tbl, *dflt;
	slurm_conf_frontend_t *n;
	char *node_state = NULL;
	static s_p_options_t _frontend_options[] = {
		{"AllowGroups", S_P_STRING},
		{"AllowUsers", S_P_STRING},
		{"DenyGroups", S_P_STRING},
		{"DenyUsers", S_P_STRING},
		{"FrontendAddr", S_P_STRING},
		{"Port", S_P_UINT16},
		{"Reason", S_P_STRING},
		{"State", S_P_STRING},
		{NULL}
	};

#ifndef HAVE_FRONT_END
	if (local_test_config) {
		error("Use of FrontendName in slurm.conf without Slurm being "
		      "configured/built with the --enable-front-end option");
		local_test_config = 1;
	} else {
		fatal("Use of FrontendName in slurm.conf without Slurm being "
		      "configured/built with the --enable-front-end option");
	}
#endif

	tbl = s_p_hashtbl_create(_frontend_options);
	s_p_parse_line(tbl, *leftover, leftover);
	/* s_p_dump_values(tbl, _frontend_options); */

	if (xstrcasecmp(value, "DEFAULT") == 0) {
		char *tmp;
		if (s_p_get_string(&tmp, "FrontendAddr", tbl)) {
			error("FrontendAddr not allowed with "
			      "FrontendName=DEFAULT");
			xfree(tmp);
			s_p_hashtbl_destroy(tbl);
			return -1;
		}

		if (default_frontend_tbl != NULL) {
			s_p_hashtbl_merge(tbl, default_frontend_tbl);
			s_p_hashtbl_destroy(default_frontend_tbl);
		}
		default_frontend_tbl = tbl;

		return 0;
	} else {
		n = xmalloc(sizeof(slurm_conf_frontend_t));
		dflt = default_frontend_tbl;

		n->frontends = xstrdup(value);

		(void) s_p_get_string(&n->allow_groups, "AllowGroups", tbl);
		(void) s_p_get_string(&n->allow_users,  "AllowUsers", tbl);
		(void) s_p_get_string(&n->deny_groups,  "DenyGroups", tbl);
		(void) s_p_get_string(&n->deny_users,   "DenyUsers", tbl);
		if (n->allow_groups && n->deny_groups) {
			if (local_test_config) {
				error("FrontEnd options AllowGroups and DenyGroups "
				      "are incompatible");
				local_test_config = 1;
			} else {
				fatal("FrontEnd options AllowGroups and DenyGroups "
				      "are incompatible");
			}
		}
		if (n->allow_users && n->deny_users) {
			if (local_test_config) {
				error("FrontEnd options AllowUsers and DenyUsers "
				      "are incompatible");
				local_test_config = 1;
			} else {
				fatal("FrontEnd options AllowUsers and DenyUsers "
				      "are incompatible");
			}
		}

		if (!s_p_get_string(&n->addresses, "FrontendAddr", tbl))
			n->addresses = xstrdup(n->frontends);

		if (!s_p_get_uint16(&n->port, "Port", tbl) &&
		    !s_p_get_uint16(&n->port, "Port", dflt)) {
			/* This gets resolved in slurm_conf_get_port()
			 * and slurm_conf_get_addr(). For now just
			 * leave with a value of zero */
			n->port = 0;
		}

		if (!s_p_get_string(&n->reason, "Reason", tbl))
			s_p_get_string(&n->reason, "Reason", dflt);

		if (!s_p_get_string(&node_state, "State", tbl) &&
		    !s_p_get_string(&node_state, "State", dflt)) {
			n->node_state = NODE_STATE_UNKNOWN;
		} else {
			n->node_state = state_str2int(node_state,
						      (char *) value);
			if (n->node_state == NO_VAL16)
				n->node_state = NODE_STATE_UNKNOWN;
			xfree(node_state);
		}

		*dest = (void *)n;

		s_p_hashtbl_destroy(tbl);
		return 1;
	}

	/* should not get here */
}

static int _parse_nodename(void **dest, slurm_parser_enum_t type,
			   const char *key, const char *value,
			   const char *line, char **leftover)
{
	s_p_hashtbl_t *tbl, *dflt;
	slurm_conf_node_t *n;
	int computed_procs;
	static s_p_options_t _nodename_options[] = {
		{"Boards", S_P_UINT16},
		{"CoreSpecCount", S_P_UINT16},
		{"CoresPerSocket", S_P_UINT16},
		{"CPUs", S_P_UINT16},
		{"CPUSpecList", S_P_STRING},
		{"CpuBind", S_P_STRING},
		{"Feature", S_P_STRING},
		{"Features", S_P_STRING},
		{"Gres", S_P_STRING},
		{"MemSpecLimit", S_P_UINT64},
		{"NodeAddr", S_P_STRING},
		{"NodeHostname", S_P_STRING},
		{"Port", S_P_STRING},
		{"Procs", S_P_UINT16},
		{"RealMemory", S_P_UINT64},
		{"Reason", S_P_STRING},
		{"Sockets", S_P_UINT16},
		{"SocketsPerBoard", S_P_UINT16},
		{"State", S_P_STRING},
		{"ThreadsPerCore", S_P_UINT16},
		{"TmpDisk", S_P_UINT32},
		{"TRESWeights", S_P_STRING},
		{"Weight", S_P_UINT32},
		{NULL}
	};

	tbl = s_p_hashtbl_create(_nodename_options);
	s_p_parse_line(tbl, *leftover, leftover);
	/* s_p_dump_values(tbl, _nodename_options); */

	if (xstrcasecmp(value, "DEFAULT") == 0) {
		char *tmp;
		if (s_p_get_string(&tmp, "NodeHostname", tbl)) {
			error("NodeHostname not allowed with "
			      "NodeName=DEFAULT");
			xfree(tmp);
			s_p_hashtbl_destroy(tbl);
			return -1;
		}
		if (s_p_get_string(&tmp, "NodeAddr", tbl)) {
			error("NodeAddr not allowed with NodeName=DEFAULT");
			xfree(tmp);
			s_p_hashtbl_destroy(tbl);
			return -1;
		}

		if (default_nodename_tbl != NULL) {
			s_p_hashtbl_merge(tbl, default_nodename_tbl);
			s_p_hashtbl_destroy(default_nodename_tbl);
		}
		default_nodename_tbl = tbl;

		return 0;
	} else {
		bool no_cpus    = false;
		bool no_boards  = false;
		bool no_sockets = false;
		bool no_cores   = false;
		bool no_threads = false;
		bool no_sockets_per_board = false;
		uint16_t sockets_per_board = 0;
		uint16_t calc_cpus;
		char *cpu_bind = NULL;

		n = xmalloc(sizeof(slurm_conf_node_t));
		dflt = default_nodename_tbl;

		n->nodenames = xstrdup(value);
		if ((slurmdb_setup_cluster_name_dims() > 1)
		    && conf_ptr->node_prefix == NULL)
			_set_node_prefix(n->nodenames);

		if (!s_p_get_string(&n->hostnames, "NodeHostname", tbl))
			n->hostnames = xstrdup(n->nodenames);
		if (!s_p_get_string(&n->addresses, "NodeAddr", tbl))
			n->addresses = xstrdup(n->hostnames);

		if (!s_p_get_uint16(&n->boards, "Boards", tbl)
		    && !s_p_get_uint16(&n->boards, "Boards", dflt)) {
			n->boards = 1;
			no_boards = true;
		}

		if (s_p_get_string(&cpu_bind, "CpuBind", tbl) ||
		    s_p_get_string(&cpu_bind, "CpuBind", dflt)) {
			if (xlate_cpu_bind_str(cpu_bind, &n->cpu_bind) !=
			    SLURM_SUCCESS) {
				error("NodeNames=%s CpuBind=\'%s\' is invalid, ignored",
				      n->nodenames, cpu_bind);
				n->cpu_bind = 0;
			}
			xfree(cpu_bind);
		}

		if (!s_p_get_uint16(&n->core_spec_cnt, "CoreSpecCount", tbl)
		    && !s_p_get_uint16(&n->core_spec_cnt,
				       "CoreSpecCount", dflt))
			n->core_spec_cnt = 0;


		if (!s_p_get_uint16(&n->cores, "CoresPerSocket", tbl)
		    && !s_p_get_uint16(&n->cores, "CoresPerSocket", dflt)) {
			n->cores = 1;
			no_cores = true;
		}

		if (!s_p_get_string(&n->cpu_spec_list, "CPUSpecList", tbl))
			s_p_get_string(&n->cpu_spec_list, "CPUSpecList", dflt);

		if (!s_p_get_string(&n->feature, "Feature",  tbl) &&
		    !s_p_get_string(&n->feature, "Features", tbl) &&
		    !s_p_get_string(&n->feature, "Feature",  dflt))
			s_p_get_string(&n->feature, "Features", dflt);

		if (!s_p_get_string(&n->gres, "Gres", tbl))
			s_p_get_string(&n->gres, "Gres", dflt);

		if (!s_p_get_uint64(&n->mem_spec_limit, "MemSpecLimit", tbl)
		    && !s_p_get_uint64(&n->mem_spec_limit,
				       "MemSpecLimit", dflt))
			n->mem_spec_limit = 0;

		if (!s_p_get_string(&n->port_str, "Port", tbl) &&
		    !s_p_get_string(&n->port_str, "Port", dflt)) {
			/* This gets resolved in slurm_conf_get_port()
			 * and slurm_conf_get_addr(). For now just
			 * leave with a value of NULL */
		}

		if (!s_p_get_uint16(&n->cpus, "CPUs",  tbl)  &&
		    !s_p_get_uint16(&n->cpus, "CPUs",  dflt) &&
		    !s_p_get_uint16(&n->cpus, "Procs", tbl)  &&
		    !s_p_get_uint16(&n->cpus, "Procs", dflt)) {
			n->cpus = 1;
			no_cpus = true;
		}

		if (!s_p_get_uint64(&n->real_memory, "RealMemory", tbl)
		    && !s_p_get_uint64(&n->real_memory, "RealMemory", dflt))
			n->real_memory = 1;

		if (!s_p_get_string(&n->reason, "Reason", tbl))
			s_p_get_string(&n->reason, "Reason", dflt);

		if (!s_p_get_uint16(&n->sockets, "Sockets", tbl)
		    && !s_p_get_uint16(&n->sockets, "Sockets", dflt)) {
			n->sockets = 1;
			no_sockets = true;
		}

		if (!s_p_get_uint16(&sockets_per_board, "SocketsPerBoard", tbl)
		    && !s_p_get_uint16(&sockets_per_board, "SocketsPerBoard",
				       dflt)) {
			sockets_per_board = 1;
			no_sockets_per_board = true;
		}

		if (!s_p_get_string(&n->state, "State", tbl)
		    && !s_p_get_string(&n->state, "State", dflt))
			n->state = NULL;

		if (!s_p_get_uint16(&n->threads, "ThreadsPerCore", tbl)
		    && !s_p_get_uint16(&n->threads, "ThreadsPerCore", dflt)) {
			n->threads = 1;
			no_threads = true;
		}

		if (!s_p_get_uint32(&n->tmp_disk, "TmpDisk", tbl)
		    && !s_p_get_uint32(&n->tmp_disk, "TmpDisk", dflt))
			n->tmp_disk = 0;

		if (!s_p_get_string(&n->tres_weights_str, "TRESWeights", tbl) &&
		    !s_p_get_string(&n->tres_weights_str, "TRESWeights", dflt))
			xfree(n->tres_weights_str);

		if (!s_p_get_uint32(&n->weight, "Weight", tbl)
		    && !s_p_get_uint32(&n->weight, "Weight", dflt))
			n->weight = 1;
		else if (n->weight == INFINITE)
			n->weight -= 1;

		s_p_hashtbl_destroy(tbl);

		if (n->cores == 0) {	/* make sure cores is non-zero */
			error("NodeNames=%s CoresPerSocket=0 is invalid, "
			      "reset to 1", n->nodenames);
			n->cores = 1;
		}
		if (n->threads == 0) {	/* make sure threads is non-zero */
			error("NodeNames=%s ThreadsPerCore=0 is invalid, "
			      "reset to 1", n->nodenames);
			n->threads = 1;
		}

		if (!no_sockets_per_board && sockets_per_board==0) {
			/* make sure sockets_per_boards is non-zero */
			error("NodeNames=%s SocketsPerBoards=0 is invalid, "
			      "reset to 1", n->nodenames);
			sockets_per_board = 1;
		}

		if (no_boards) {
			/* This case is exactly like if was without boards,
			 * Except SocketsPerBoard=# can be used,
			 * But it can't be used with Sockets=# */
			n->boards = 1;
			if (!no_sockets_per_board) {
				if (!no_sockets)
					error("NodeNames=%s Sockets=# and "
					      "SocketsPerBoard=# is invalid"
					      ", using SocketsPerBoard",
					      n->nodenames);
				n->sockets = sockets_per_board;
			} else if (!no_cpus && no_sockets) {
				/* infer missing Sockets= */
				n->sockets = n->cpus / (n->cores * n->threads);
			}

			if (n->sockets == 0) { /* make sure sockets != 0 */
				error("NodeNames=%s Sockets=0 is invalid, "
				      "reset to 1", n->nodenames);
				n->sockets = 1;
			}
			if (no_cpus) {		/* infer missing CPUs= */
				n->cpus = n->sockets * n->cores * n->threads;
			}
			/* if only CPUs= and Sockets=
			 * specified check for match */
			if (!no_cpus    && !no_sockets &&
			     no_cores   &&  no_threads &&
			     (n->cpus != n->sockets)) {
				n->sockets = n->cpus;
				error("NodeNames=%s CPUs doesn't match "
				      "Sockets, setting Sockets to %d",
				      n->nodenames, n->sockets);
			}
			computed_procs = n->sockets * n->cores * n->threads;
			if ((n->cpus != n->sockets) &&
			    (n->cpus != n->sockets * n->cores) &&
			    (n->cpus != computed_procs)) {
				error("NodeNames=%s CPUs=%d doesn't match "
				      "Sockets*CoresPerSocket*ThreadsPerCore "
				      "(%d), resetting CPUs",
				      n->nodenames, n->cpus, computed_procs);
				n->cpus = computed_procs;
			}
		} else {
			/* In this case Boards=# is used.
			 * CPUs=# or Procs=# are ignored.
			 */
			if (n->boards == 0) {
				/* make sure boards is non-zero */
				error("NodeNames=%s Boards=0 is "
				      "invalid, reset to 1",
				      n->nodenames);
				n->boards = 1;
			}

			if (!no_sockets_per_board) {
				if (!no_sockets)
					error("NodeNames=%s Sockets=# and "
					      "SocketsPerBoard=# is invalid, "
					      "using SocketsPerBoard",
					      n->nodenames);

				n->sockets = n->boards * sockets_per_board;
			} else if (!no_sockets) {
				error("NodeNames=%s Sockets=# with Boards=# is"
				      " not recommended, assume "
				      "SocketsPerBoard was meant",
				      n->nodenames);
				if (n->sockets == 0) {
					/* make sure sockets is non-zero */
					error("NodeNames=%s Sockets=0 is "
					      "invalid, reset to 1",
					      n->nodenames);
					n->sockets = 1;
				}
				n->sockets = n->boards * n->sockets;
			} else {
				n->sockets = n->boards;
			}
			/* Node boards factored into sockets */
			calc_cpus = n->sockets * n->cores * n->threads;
			if (!no_cpus && (n->cpus != calc_cpus)) {
				error("NodeNames=%s CPUs=# or Procs=# "
				      "with Boards=# is invalid and "
				      "is ignored.", n->nodenames);
			}
			n->cpus = calc_cpus;
		}

		if (n->core_spec_cnt >= (n->sockets * n->cores)) {
			error("NodeNames=%s CoreSpecCount=%u is invalid, "
			      "reset to 1", n->nodenames, n->core_spec_cnt);
			n->core_spec_cnt = 1;
		}

		if ((n->core_spec_cnt > 0) && n->cpu_spec_list) {
			error("NodeNames=%s CoreSpecCount=%u is invalid "
			      "with CPUSpecList, reset to 0",
			      n->nodenames, n->core_spec_cnt);
			n->core_spec_cnt = 0;
		}

		if (n->mem_spec_limit >= n->real_memory) {
			error("NodeNames=%s MemSpecLimit=%"
			      ""PRIu64" is invalid, reset to 0",
			      n->nodenames, n->mem_spec_limit);
			n->mem_spec_limit = 0;
		}

		*dest = (void *)n;

		return 1;
	}

	/* should not get here */
}

/* Destroy a front_end record built by slurm_conf_frontend_array() */
extern void destroy_frontend(void *ptr)
{
	slurm_conf_frontend_t *n = (slurm_conf_frontend_t *) ptr;
	xfree(n->addresses);
	xfree(n->allow_groups);
	xfree(n->allow_users);
	xfree(n->deny_groups);
	xfree(n->deny_users);
	xfree(n->frontends);
	xfree(n->reason);
	xfree(ptr);
}

/*
 * list_find_frontend - find an entry in the front_end list, see list.h for
 *	documentation
 * IN key - is feature name or NULL for all features
 * RET 1 if found, 0 otherwise
 */
extern int list_find_frontend (void *front_end_entry, void *key)
{
	slurm_conf_frontend_t *front_end_ptr;

	if (key == NULL)
		return 1;

	front_end_ptr = (slurm_conf_frontend_t *) front_end_entry;
	if (xstrcmp(front_end_ptr->frontends, (char *) key) == 0)
		return 1;
	return 0;
}

static void _destroy_nodename(void *ptr)
{
	slurm_conf_node_t *n = (slurm_conf_node_t *)ptr;

	xfree(n->addresses);
	xfree(n->cpu_spec_list);
	xfree(n->feature);
	xfree(n->hostnames);
	xfree(n->gres);
	xfree(n->nodenames);
	xfree(n->port_str);
	xfree(n->reason);
	xfree(n->state);
	xfree(n->tres_weights_str);
	xfree(ptr);
}

/* _parse_srun_ports()
 *
 * Parse the srun port range specified like min-max.
 *
 */
static uint16_t *
_parse_srun_ports(const char *str)
{
	char *min;
	char *max;
	char *dash;
	char *p;
	uint16_t *v;

	p = xstrdup(str);

	min = p;
	dash = strchr(p, '-');
	if (dash == NULL) {
		xfree(p);
		return NULL;
	}

	*dash = 0;
	max = dash + 1;

	v = xmalloc(2 * sizeof(uint16_t));

	if (parse_uint16(min, &v[0]))
		goto hosed;
	if (parse_uint16(max, &v[1]))
		goto hosed;
	if (v[1] <= v[0])
		goto hosed;

	xfree(p);

	return v;
hosed:
	xfree(v);
	xfree(p);

	return NULL;
}

int slurm_conf_frontend_array(slurm_conf_frontend_t **ptr_array[])
{
	int count = 0;
	slurm_conf_frontend_t **ptr;

	if (s_p_get_array((void ***)&ptr, &count, "FrontendName",
			  conf_hashtbl)) {
		*ptr_array = ptr;
		return count;
	} else {
#ifdef HAVE_FRONT_END
		/* No FrontendName in slurm.conf. Take the NodeAddr and
		 * NodeHostName from the first node's record and use that to
		 * build an equivalent structure to that constructed when
		 * FrontendName is configured. This is intended for backward
		 * compatibility with Slurm version 2.2. */
		static slurm_conf_frontend_t local_front_end;
		static slurm_conf_frontend_t *local_front_end_array[2] =
			{NULL, NULL};
		static char addresses[1024], hostnames[1024];

		if (local_front_end_array[0] == NULL) {
			slurm_conf_node_t **node_ptr;
			int node_count = 0;
			if (!s_p_get_array((void ***)&node_ptr, &node_count,
					   "NodeName", conf_hashtbl) ||
			    (node_count == 0)) {
				if (local_test_config) {
					error("No front end nodes configured");
					local_test_config = 1;
				} else {
					fatal("No front end nodes configured");
				}
			}
			strlcpy(addresses, node_ptr[0]->addresses,
				sizeof(addresses));
			strlcpy(hostnames, node_ptr[0]->hostnames,
				sizeof(hostnames));
			local_front_end.addresses = addresses;
			local_front_end.frontends = hostnames;
			if (node_ptr[0]->port_str) {
				local_front_end.port = atoi(node_ptr[0]->
							    port_str);
			}
			local_front_end.reason = NULL;
			local_front_end.node_state = NODE_STATE_UNKNOWN;
			local_front_end_array[0] = &local_front_end;
		}
		*ptr_array = local_front_end_array;
		return 1;
#else
		*ptr_array = NULL;
		return 0;
#endif
	}
}


int slurm_conf_nodename_array(slurm_conf_node_t **ptr_array[])
{
	int count = 0;
	slurm_conf_node_t **ptr;

	if (s_p_get_array((void ***)&ptr, &count, "NodeName", conf_hashtbl)) {
		*ptr_array = ptr;
		return count;
	} else {
		*ptr_array = NULL;
		return 0;
	}
}

/* Copy list of job_defaults_t elements */
extern List job_defaults_copy(List in_list)
{
	List out_list = NULL;
	job_defaults_t *in_default, *out_default;
	ListIterator iter;

	if (!in_list)
		return out_list;

	out_list = list_create(job_defaults_free);
	iter = list_iterator_create(in_list);
	while ((in_default = list_next(iter))) {
		out_default = xmalloc(sizeof(job_defaults_t));
		memcpy(out_default, in_default, sizeof(job_defaults_t));
		list_append(out_list, out_default);
	}
	list_iterator_destroy(iter);

	return out_list;
}

/* Destroy list of job_defaults_t elements */
extern void job_defaults_free(void *x)
{
	xfree(x);
}

static char *_job_def_name(uint16_t type)
{
	static char name[32];

	switch (type) {
	case JOB_DEF_CPU_PER_GPU:
		return "DefCpuPerGPU";
	case JOB_DEF_MEM_PER_GPU:
		return "DefMemPerGPU";
	}
	snprintf(name, sizeof(name), "Unknown(%u)", type);
	return name;
}

static uint16_t _job_def_type(char *type)
{
	if (!xstrcasecmp(type, "DefCpuPerGPU"))
		return JOB_DEF_CPU_PER_GPU;
	if (!xstrcasecmp(type, "DefMemPerGPU"))
		return JOB_DEF_MEM_PER_GPU;
	return NO_VAL16;
}

/*
 * Translate string of job_defaults_t elements into a List.
 * in_str IN - comma separated key=value pairs
 * out_list OUT - equivalent list of key=value pairs
 * Returns SLURM_SUCCESS or an error code
 */
extern int job_defaults_list(char *in_str, List *out_list)
{
	int rc = SLURM_SUCCESS;
	List tmp_list;
	char *end_ptr = NULL, *tmp_str, *save_ptr = NULL, *sep, *tok;
	uint16_t type;
	long long int value;
	job_defaults_t *out_default;

	*out_list = NULL;
	if (!in_str || (in_str[0] == '\0'))
		return rc;

	tmp_list = list_create(job_defaults_free);
	tmp_str = xstrdup(in_str);
	tok = strtok_r(tmp_str, ",", &save_ptr);
	while (tok) {
		sep = strchr(tok, '=');
		if (!sep) {
			rc = EINVAL;
			break;
		}
		sep[0] = '\0';
		sep++;
		type = _job_def_type(tok);
		if (type == NO_VAL16) {
			rc = EINVAL;
			break;
		}
		value = strtoll(sep, &end_ptr, 10);
		if (!end_ptr || (end_ptr[0] != '\0') ||
		    (value < 0) || (value == LLONG_MAX)) {
			rc = EINVAL;
			break;
		}
		out_default = xmalloc(sizeof(job_defaults_t));
		out_default->type = type;
		out_default->value = (uint64_t) value;
		list_append(tmp_list, out_default);
		tok = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(tmp_str);
	if (rc != SLURM_SUCCESS)
		FREE_NULL_LIST(tmp_list);
	else
		*out_list = tmp_list;
	return rc;
}

/*
 * Translate list of job_defaults_t elements into a string.
 * Return value must be released using xfree()
 */
extern char *job_defaults_str(List in_list)
{
	job_defaults_t *in_default;
	ListIterator iter;
	char *out_str = NULL, *sep = "";

	if (!in_list)
		return out_str;

	iter = list_iterator_create(in_list);
	while ((in_default = list_next(iter))) {
		xstrfmtcat(out_str, "%s%s=%"PRIu64, sep,
			   _job_def_name(in_default->type), in_default->value);
		sep = ",";
	}
	list_iterator_destroy(iter);

	return out_str;

}

/* Pack a job_defaults_t element. Used by slurm_pack_list() */
extern void job_defaults_pack(void *in, uint16_t protocol_version, Buf buffer)
{
	job_defaults_t *object = (job_defaults_t *)in;

	if (!object) {
		pack16(0, buffer);
		pack64(0, buffer);
		return;
	}

	pack16(object->type, buffer);
	pack64(object->value, buffer);
}

/* Unpack a job_defaults_t element. Used by slurm_unpack_list() */
extern int job_defaults_unpack(void **out, uint16_t protocol_version,
			       Buf buffer)
{
	job_defaults_t *object = xmalloc(sizeof(job_defaults_t));

	safe_unpack16(&object->type, buffer);
	safe_unpack64(&object->value, buffer);
	*out = object;
	return SLURM_SUCCESS;

unpack_error:
	xfree(object);
	*out = NULL;
	return SLURM_ERROR;
}

static int _parse_partitionname(void **dest, slurm_parser_enum_t type,
			       const char *key, const char *value,
			       const char *line, char **leftover)
{
	s_p_hashtbl_t *tbl, *dflt;
	slurm_conf_partition_t *p;
	uint64_t def_cpu_per_gpu = 0, def_mem_per_gpu = 0;
	job_defaults_t *job_defaults;
	char *cpu_bind = NULL, *tmp = NULL;
	uint16_t tmp_16 = 0;
	static s_p_options_t _partition_options[] = {
		{"AllocNodes", S_P_STRING},
		{"AllowAccounts",S_P_STRING},
		{"AllowGroups", S_P_STRING},
		{"AllowQos", S_P_STRING},
		{"Alternate", S_P_STRING},
		{"CpuBind", S_P_STRING},
		{"DefCPUPerGPU" , S_P_UINT64},
		{"DefMemPerCPU", S_P_UINT64},
		{"DefMemPerGPU" , S_P_UINT64},
		{"DefMemPerNode", S_P_UINT64},
		{"Default", S_P_BOOLEAN}, /* YES or NO */
		{"DefaultTime", S_P_STRING},
		{"DenyAccounts", S_P_STRING},
		{"DenyQos", S_P_STRING},
		{"DisableRootJobs", S_P_BOOLEAN}, /* YES or NO */
		{"ExclusiveUser", S_P_BOOLEAN}, /* YES or NO */
		{"GraceTime", S_P_UINT32},
		{"Hidden", S_P_BOOLEAN}, /* YES or NO */
		{"LLN", S_P_BOOLEAN}, /* YES or NO */
		{"MaxCPUsPerNode", S_P_UINT32},
		{"MaxMemPerCPU", S_P_UINT64},
		{"MaxMemPerNode", S_P_UINT64},
		{"MaxTime", S_P_STRING},
		{"MaxNodes", S_P_UINT32}, /* INFINITE or a number */
		{"MinNodes", S_P_UINT32},
		{"Nodes", S_P_STRING},
		{"OverSubscribe", S_P_STRING}, /* YES, NO, or FORCE */
		{"OverTimeLimit", S_P_STRING},
		{"PreemptMode", S_P_STRING},
		{"Priority", S_P_UINT16},
		{"PriorityJobFactor", S_P_UINT16},
		{"PriorityTier", S_P_UINT16},
		{"QOS", S_P_STRING},
		{"RootOnly", S_P_BOOLEAN}, /* YES or NO */
		{"ReqResv", S_P_BOOLEAN}, /* YES or NO */
		{"SelectTypeParameters", S_P_STRING},
		{"Shared", S_P_STRING}, /* YES, NO, or FORCE */
		{"State", S_P_STRING}, /* UP, DOWN, INACTIVE or DRAIN */
		{"TRESBillingWeights", S_P_STRING},
		{NULL}
	};


	tbl = s_p_hashtbl_create(_partition_options);
	s_p_parse_line(tbl, *leftover, leftover);
	/* s_p_dump_values(tbl, _partition_options); */

	if (xstrcasecmp(value, "DEFAULT") == 0) {
		if (default_partition_tbl != NULL) {
			s_p_hashtbl_merge(tbl, default_partition_tbl);
			s_p_hashtbl_destroy(default_partition_tbl);
		}
		default_partition_tbl = tbl;

		return 0;
	} else {
		p = xmalloc(sizeof(slurm_conf_partition_t));
		dflt = default_partition_tbl;

		p->name = xstrdup(value);

		if (!s_p_get_string(&p->allow_accounts, "AllowAccounts",tbl))
			s_p_get_string(&p->allow_accounts,
				       "AllowAccounts", dflt);
		/* lower case account names */
		if (p->allow_accounts)
			xstrtolower(p->allow_accounts);
		if (p->allow_accounts &&
		    (xstrcasecmp(p->allow_accounts, "ALL") == 0))
			xfree(p->allow_accounts);

		if (!s_p_get_string(&p->allow_groups, "AllowGroups", tbl))
			s_p_get_string(&p->allow_groups, "AllowGroups", dflt);
		if (p->allow_groups &&
		    (xstrcasecmp(p->allow_groups, "ALL") == 0))
			xfree(p->allow_groups);

		if (!s_p_get_string(&p->allow_qos, "AllowQos", tbl))
			s_p_get_string(&p->allow_qos, "AllowQos", dflt);
		/* lower case qos names */
		if (p->allow_qos)
			xstrtolower(p->allow_qos);
		if (p->allow_qos && (xstrcasecmp(p->allow_qos, "ALL") == 0))
			xfree(p->allow_qos);

		if (!s_p_get_string(&p->deny_accounts, "DenyAccounts", tbl))
			s_p_get_string(&p->deny_accounts,
				       "DenyAccounts", dflt);
		if (p->allow_accounts && p->deny_accounts) {
			error("Both AllowAccounts and DenyAccounts are "
			      "defined, DenyAccounts will be ignored");
		}
		/* lower case account names */
		else if(p->deny_accounts)
			xstrtolower(p->deny_accounts);

		if (!s_p_get_string(&p->deny_qos, "DenyQos", tbl))
			s_p_get_string(&p->deny_qos, "DenyQos", dflt);
		if (p->allow_qos && p->deny_qos) {
			error("Both AllowQos and DenyQos are defined, "
			      "DenyQos will be ignored");
		}
		/* lower case qos names */
		else if(p->deny_qos)
			xstrtolower(p->deny_qos);

		if (!s_p_get_string(&p->allow_alloc_nodes,
				    "AllocNodes", tbl)) {
			s_p_get_string(&p->allow_alloc_nodes, "AllocNodes",
				       dflt);
			if (p->allow_alloc_nodes &&
			    (xstrcasecmp(p->allow_alloc_nodes, "ALL") == 0))
				xfree(p->allow_alloc_nodes);
		}

		if (!s_p_get_string(&p->alternate, "Alternate", tbl))
			s_p_get_string(&p->alternate, "Alternate", dflt);

		if (s_p_get_string(&cpu_bind, "CpuBind", tbl) ||
		    s_p_get_string(&cpu_bind, "CpuBind", dflt)) {
			if (xlate_cpu_bind_str(cpu_bind, &p->cpu_bind) !=
			    SLURM_SUCCESS) {
				error("Partition=%s CpuBind=\'%s\' is invalid, ignored",
				      p->name, cpu_bind);
				p->cpu_bind = 0;
			}
			xfree(cpu_bind);
		}

		if (!s_p_get_string(&p->billing_weights_str,
				    "TRESBillingWeights", tbl) &&
		    !s_p_get_string(&p->billing_weights_str,
				    "TRESBillingWeights", dflt))
			xfree(p->billing_weights_str);

		if (!s_p_get_boolean(&p->default_flag, "Default", tbl)
		    && !s_p_get_boolean(&p->default_flag, "Default", dflt))
			p->default_flag = false;

		if (!s_p_get_uint32(&p->max_cpus_per_node, "MaxCPUsPerNode",
				    tbl) &&
		    !s_p_get_uint32(&p->max_cpus_per_node, "MaxCPUsPerNode",
				    dflt))
			p->max_cpus_per_node = INFINITE;


		if (s_p_get_uint64(&def_cpu_per_gpu, "DefCPUPerGPU", tbl) ||
		    s_p_get_uint64(&def_cpu_per_gpu, "DefCPUPerGPU", dflt)) {
			job_defaults = xmalloc(sizeof(job_defaults_t));
			job_defaults->type  = JOB_DEF_CPU_PER_GPU;
			job_defaults->value = def_cpu_per_gpu;
			if (!p->job_defaults_list) {
				p->job_defaults_list =
					list_create(job_defaults_free);
			}
			list_append(p->job_defaults_list, job_defaults);
		}
		if (s_p_get_uint64(&def_mem_per_gpu, "DefMemPerGPU", tbl) ||
		    s_p_get_uint64(&def_mem_per_gpu, "DefMemPerGPU", dflt)) {
			job_defaults = xmalloc(sizeof(job_defaults_t));
			job_defaults->type  = JOB_DEF_MEM_PER_GPU;
			job_defaults->value = def_mem_per_gpu;
			if (!p->job_defaults_list) {
				p->job_defaults_list =
					list_create(job_defaults_free);
			}
			list_append(p->job_defaults_list, job_defaults);
		}

		if (!s_p_get_uint64(&p->def_mem_per_cpu, "DefMemPerNode",
				    tbl) &&
		    !s_p_get_uint64(&p->def_mem_per_cpu, "DefMemPerNode",
				    dflt)) {
			if (s_p_get_uint64(&p->def_mem_per_cpu,
					   "DefMemPerCPU", tbl) ||
			    s_p_get_uint64(&p->def_mem_per_cpu,
					   "DefMemPerCPU", dflt)) {
				p->def_mem_per_cpu |= MEM_PER_CPU;
			} else {
				p->def_mem_per_cpu = 0;
			}
		}

		if (!s_p_get_uint64(&p->max_mem_per_cpu, "MaxMemPerNode",
				    tbl) &&
		    !s_p_get_uint64(&p->max_mem_per_cpu, "MaxMemPerNode",
				    dflt)) {
			if (s_p_get_uint64(&p->max_mem_per_cpu,
					   "MaxMemPerCPU", tbl) ||
			    s_p_get_uint64(&p->max_mem_per_cpu,
					   "MaxMemPerCPU", dflt)) {
				p->max_mem_per_cpu |= MEM_PER_CPU;
			} else {
				p->max_mem_per_cpu = 0;
			}
		}

		if (!s_p_get_boolean((bool *)&p->disable_root_jobs,
				     "DisableRootJobs", tbl))
			p->disable_root_jobs = NO_VAL16;

		if (!s_p_get_boolean((bool *)&p->exclusive_user,
				     "ExclusiveUser", tbl))
			p->exclusive_user = 0;

		if (!s_p_get_boolean(&p->hidden_flag, "Hidden", tbl) &&
		    !s_p_get_boolean(&p->hidden_flag, "Hidden", dflt))
			p->hidden_flag = false;

		if (!s_p_get_string(&tmp, "MaxTime", tbl) &&
		    !s_p_get_string(&tmp, "MaxTime", dflt))
			p->max_time = INFINITE;
		else {
			int max_time = time_str2mins(tmp);
			if ((max_time < 0) && (max_time != INFINITE)) {
				error("Bad value \"%s\" for MaxTime", tmp);
				_destroy_partitionname(p);
				s_p_hashtbl_destroy(tbl);
				xfree(tmp);
				return -1;
			}
			p->max_time = max_time;
			xfree(tmp);
		}

		if (!s_p_get_uint32(&p->grace_time, "GraceTime", tbl) &&
		    !s_p_get_uint32(&p->grace_time, "GraceTime", dflt))
			p->grace_time = 0;

		if (!s_p_get_string(&tmp, "DefaultTime", tbl) &&
		    !s_p_get_string(&tmp, "DefaultTime", dflt))
			p->default_time = NO_VAL;
		else {
			int default_time = time_str2mins(tmp);
			if ((default_time < 0) && (default_time != INFINITE)) {
				error("Bad value \"%s\" for DefaultTime", tmp);
				_destroy_partitionname(p);
				s_p_hashtbl_destroy(tbl);
				xfree(tmp);
				return -1;
			}
			p->default_time = default_time;
			xfree(tmp);
		}

		if (!s_p_get_uint32(&p->max_nodes, "MaxNodes", tbl)
		    && !s_p_get_uint32(&p->max_nodes, "MaxNodes", dflt))
			p->max_nodes = INFINITE;

		if (!s_p_get_uint32(&p->min_nodes, "MinNodes", tbl)
		    && !s_p_get_uint32(&p->min_nodes, "MinNodes", dflt))
			p->min_nodes = 0;

		if (!s_p_get_string(&p->nodes, "Nodes", tbl)
		    && !s_p_get_string(&p->nodes, "Nodes", dflt))
			p->nodes = NULL;
		else {
			int i;
			for (i=0; p->nodes[i]; i++) {
				if (isspace((int)p->nodes[i]))
					p->nodes[i] = ',';
			}
		}

		if (!s_p_get_boolean(&p->root_only_flag, "RootOnly", tbl)
		    && !s_p_get_boolean(&p->root_only_flag, "RootOnly", dflt))
			p->root_only_flag = false;

		if (!s_p_get_boolean(&p->req_resv_flag, "ReqResv", tbl)
		    && !s_p_get_boolean(&p->req_resv_flag, "ReqResv", dflt))
			p->req_resv_flag = false;

		if (!s_p_get_boolean(&p->lln_flag, "LLN", tbl) &&
		    !s_p_get_boolean(&p->lln_flag, "LLN", dflt))
			p->lln_flag = false;

		if (s_p_get_string(&tmp, "OverTimeLimit", tbl) ||
		    s_p_get_string(&tmp, "OverTimeLimit", dflt)) {
			if (!strcasecmp(tmp, "INFINITE") ||
			    !strcasecmp(tmp, "UNLIMITED")) {
				p->over_time_limit = INFINITE16;
			} else {
				int i = strtol(tmp, (char **) NULL, 10);
				if (i < 0)
					error("Ignoring bad OverTimeLimit value: %s",
					      tmp);
				else if (i > 0xfffe)
					p->over_time_limit = INFINITE16;
				else
					p->over_time_limit = i;
			}
		} else
			p->over_time_limit = NO_VAL16;

		if (s_p_get_string(&tmp, "PreemptMode", tbl) ||
		    s_p_get_string(&tmp, "PreemptMode", dflt)) {
			p->preempt_mode = preempt_mode_num(tmp);
			if (p->preempt_mode == NO_VAL16) {
				error("Bad value \"%s\" for PreemptMode", tmp);
				xfree(tmp);
				return -1;
			}
			xfree(tmp);
		} else
			p->preempt_mode = NO_VAL16;

		if (!s_p_get_uint16(&p->priority_job_factor,
				    "PriorityJobFactor", tbl) &&
		    !s_p_get_uint16(&p->priority_job_factor,
				    "PriorityJobFactor", dflt)) {
			p->priority_job_factor = 1;
		}
		if (!s_p_get_uint16(&p->priority_tier, "PriorityTier", tbl) &&
		    !s_p_get_uint16(&p->priority_tier, "PriorityTier", dflt)) {
			p->priority_tier = 1;
		}
		if (s_p_get_uint16(&tmp_16, "Priority", tbl) ||
		    s_p_get_uint16(&tmp_16, "Priority", dflt)) {
			p->priority_job_factor = tmp_16;
			p->priority_tier = tmp_16;
		}

		if (!s_p_get_string(&p->qos_char, "QOS", tbl)
		    && !s_p_get_string(&p->qos_char, "QOS", dflt))
			p->qos_char = NULL;

		if (s_p_get_string(&tmp, "SelectTypeParameters", tbl)) {
			if (xstrncasecmp(tmp, "CR_Core_Memory", 14) == 0)
				p->cr_type = CR_CORE | CR_MEMORY;
			else if (xstrncasecmp(tmp, "CR_Core", 7) == 0)
				p->cr_type = CR_CORE;
			else if (xstrncasecmp(tmp, "CR_Socket_Memory", 16) == 0)
				p->cr_type = CR_SOCKET | CR_MEMORY;
			else if (xstrncasecmp(tmp, "CR_Socket", 9) == 0)
				p->cr_type = CR_SOCKET;
			else {
				error("Bad value for SelectTypeParameters: %s",
				      tmp);
				_destroy_partitionname(p);
				s_p_hashtbl_destroy(tbl);
				xfree(tmp);
				return -1;
			}
			xfree(tmp);
		} else
			p->cr_type = 0;

		if (s_p_get_string(&tmp, "OverSubscribe", tbl) ||
		    s_p_get_string(&tmp, "OverSubscribe", dflt) ||
		    s_p_get_string(&tmp, "Shared", tbl) ||
		    s_p_get_string(&tmp, "Shared", dflt)) {
			if (xstrcasecmp(tmp, "NO") == 0)
				p->max_share = 1;
			else if (xstrcasecmp(tmp, "EXCLUSIVE") == 0)
				p->max_share = 0;
			else if (xstrncasecmp(tmp, "YES:", 4) == 0) {
				int i = strtol(&tmp[4], (char **) NULL, 10);
				if (i <= 1) {
					error("Ignoring bad OverSubscribe value: %s",
					      tmp);
					p->max_share = 1; /* Shared=NO */
				} else
					p->max_share = i;
			} else if (xstrcasecmp(tmp, "YES") == 0)
				p->max_share = 4;
			else if (xstrncasecmp(tmp, "FORCE:", 6) == 0) {
				int i = strtol(&tmp[6], (char **) NULL, 10);
				if (i < 1) {
					error("Ignoring bad OverSubscribe value: %s",
					      tmp);
					p->max_share = 1; /* Shared=NO */
				} else
					p->max_share = i | SHARED_FORCE;
			} else if (xstrcasecmp(tmp, "FORCE") == 0)
				p->max_share = 4 | SHARED_FORCE;
			else {
				error("Bad value \"%s\" for Shared", tmp);
				_destroy_partitionname(p);
				s_p_hashtbl_destroy(tbl);
				xfree(tmp);
				return -1;
			}
			xfree(tmp);
		} else
			p->max_share = 1;

		if (s_p_get_string(&tmp, "State", tbl) ||
		    s_p_get_string(&tmp, "State", dflt)) {
			if (xstrncasecmp(tmp, "DOWN", 4) == 0)
				p->state_up = PARTITION_DOWN;
			else if (xstrncasecmp(tmp, "UP", 2) == 0)
				p->state_up = PARTITION_UP;
			else if (xstrncasecmp(tmp, "DRAIN", 5) == 0)
				p->state_up = PARTITION_DRAIN;
			else if (xstrncasecmp(tmp, "INACTIVE", 8) == 0)
				 p->state_up = PARTITION_INACTIVE;
			else {
				error("Bad value \"%s\" for State", tmp);
				_destroy_partitionname(p);
				s_p_hashtbl_destroy(tbl);
				xfree(tmp);
				return -1;
			}
			xfree(tmp);
		} else
			p->state_up = PARTITION_UP;

		s_p_hashtbl_destroy(tbl);

		*dest = (void *)p;

		return 1;
	}

	/* should not get here */
}

static void _destroy_partitionname(void *ptr)
{
	slurm_conf_partition_t *p = (slurm_conf_partition_t *)ptr;

	xfree(p->allow_alloc_nodes);
	xfree(p->allow_accounts);
	xfree(p->allow_groups);
	xfree(p->allow_qos);
	xfree(p->alternate);
	xfree(p->billing_weights_str);
	xfree(p->deny_accounts);
	xfree(p->deny_qos);
	FREE_NULL_LIST(p->job_defaults_list);
	xfree(p->name);
	xfree(p->nodes);
	xfree(p->qos_char);
	xfree(ptr);
}

static int _load_slurmctld_host(slurm_ctl_conf_t *conf)
{
	int count = 0, i, j;
	char *ignore;
	slurm_conf_server_t **ptr = NULL;

	if (s_p_get_array((void ***)&ptr, &count, "SlurmctldHost", conf_hashtbl)) {
		/*
		 * Using new-style SlurmctldHost entries.
		 */
		conf->control_machine = xmalloc(sizeof(char *) * count);
		conf->control_addr = xmalloc(sizeof(char *) * count);
		conf->control_cnt = count;

		for (i = 0; i < count; i++) {
			conf->control_machine[i] = xstrdup(ptr[i]->hostname);
			conf->control_addr[i] = xstrdup(ptr[i]->addr);
		}

		/*
		 * Throw errors if old-style entries are still in the config,
		 * but continue on with the newer-style entries anyways.
		 */
		if (s_p_get_string(&ignore, "ControlMachine", conf_hashtbl)) {
			error("Ignoring ControlMachine since SlurmctldHost is set.");
			xfree(ignore);
		}
		if (s_p_get_string(&ignore, "ControlAddr", conf_hashtbl)) {
			error("Ignoring ControlAddr since SlurmctldHost is set.");
			xfree(ignore);
		}
		if (s_p_get_string(&ignore, "BackupController", conf_hashtbl)) {
			error("Ignoring BackupController since SlurmctldHost is set.");
			xfree(ignore);
		}
		if (s_p_get_string(&ignore, "BackupAddr", conf_hashtbl)) {
			error("Ignoring BackupAddr since SlurmctldHost is set.");
			xfree(ignore);
		}
	} else {
		/*
		 * Using old-style ControlMachine/BackupController entries.
		 *
		 * Allocate two entries, one for primary and one for backup.
		 */
		char *tmp = NULL;
		conf->control_machine = xmalloc(sizeof(char *));
		conf->control_addr = xmalloc(sizeof(char *));
		conf->control_cnt = 1;

		if (!s_p_get_string(&conf->control_machine[0],
				    "ControlMachine", conf_hashtbl)) {
			/*
			 * Missing SlurmctldHost and ControlMachine, so just
			 * warn about the newer config option.
			 */
			error("No SlurmctldHost defined.");
			goto error;
		}
		if (!s_p_get_string(&conf->control_addr[0],
				    "ControlAddr", conf_hashtbl) &&
		    conf->control_machine[0] &&
		    strchr(conf->control_machine[0], ',')) {
			error("ControlMachine has multiple host names, so ControlAddr must be specified.");
			goto error;
		}

		if (s_p_get_string(&tmp, "BackupController", conf_hashtbl)) {
			xrealloc(conf->control_machine, (sizeof(char *) * 2));
			xrealloc(conf->control_addr, (sizeof(char *) * 2));
			conf->control_cnt = 2;
			conf->control_machine[1] = tmp;
			tmp = NULL;
		}
		if (s_p_get_string(&tmp, "BackupAddr", conf_hashtbl)) {
			if (conf->control_cnt == 1) {
				error("BackupAddr specified without BackupController");
				xfree(tmp);
				goto error;
			}
			conf->control_addr[1] = tmp;
			tmp = NULL;
		}
	}

	/*
	 * Fix up the control_addr array if they were not explicitly set above,
	 * replace "localhost" with the actual hostname, and verify there are
	 * no duplicate entries.
	 */
	for (i = 0; i < conf->control_cnt; i++) {
		if (!conf->control_addr[i]) {
			conf->control_addr[i] =
				xstrdup(conf->control_machine[i]);
		}
		if (!xstrcasecmp("localhost", conf->control_machine[i])) {
			xfree(conf->control_machine[i]);
			conf->control_machine[i] = xmalloc(MAX_SLURM_NAME);
			if (gethostname_short(conf->control_machine[i],
					      MAX_SLURM_NAME)) {
				error("getnodename: %m");
				goto error;
			}
		}
		for (j = 0; j < i; j++) {
			if (!xstrcmp(conf->control_machine[i],
				     conf->control_machine[j])) {
				error("Duplicate SlurmctldHost records: %s",
				      conf->control_machine[i]);
				goto error;
			}
		}
	}
	return SLURM_SUCCESS;

error:
	if (conf->control_machine && conf->control_addr) {
		for (i = 0; i < conf->control_cnt; i++) {
			xfree(conf->control_machine[i]);
			xfree(conf->control_addr[i]);
		}
		xfree(conf->control_machine);
		xfree(conf->control_addr);
	}
	conf->control_cnt = 0;
	return SLURM_ERROR;
}

static int _parse_slurmctld_host(void **dest, slurm_parser_enum_t type,
				 const char *key, const char *value,
				 const char *line, char **leftover)
{
	s_p_hashtbl_t *tbl;
	slurm_conf_server_t *p;
	char *open_paren, *close_paren;
	static s_p_options_t _slurmctld_host_options[] = {
		{NULL}
	};

	tbl = s_p_hashtbl_create(_slurmctld_host_options);
	s_p_parse_line(tbl, *leftover, leftover);

	open_paren = strchr(value, '(');
	close_paren = strchr(value, ')');
	if ((open_paren && !close_paren) ||
	    (!open_paren && close_paren) ||
	    (close_paren && (close_paren[1] != '\0')) ||
	    (close_paren && (close_paren != strrchr(value, ')')))) {
		error("Bad value \"%s\" for SlurmctldHost", value);
		return -1;
	}

	p = xmalloc(sizeof(slurm_conf_server_t));
	if (open_paren && close_paren) {
		p->hostname = xstrdup(value);
		open_paren = strchr(p->hostname, '(');
		if (open_paren)
			open_paren[0] = '\0';
		p->addr = xstrdup(open_paren + 1);
		close_paren = strchr(p->addr, ')');
		if (close_paren)
			close_paren[0] = '\0';
	} else {
		p->hostname = xstrdup(value);
		p->addr = xstrdup(value);
	}

	s_p_hashtbl_destroy(tbl);
	*dest = (void *) p;

	return 1;
}

/* May not be needed */
static void _destroy_slurmctld_host(void *ptr)
{
	slurm_conf_server_t *p = (slurm_conf_server_t *) ptr;

	xfree(p->hostname);
	xfree(p->addr);
	xfree(ptr);
}

int slurm_conf_partition_array(slurm_conf_partition_t **ptr_array[])
{
	int count = 0;
	slurm_conf_partition_t **ptr;

	if (s_p_get_array((void ***)&ptr, &count, "PartitionName",
			  conf_hashtbl)) {
		*ptr_array = ptr;
		return count;
	} else {
		*ptr_array = NULL;
		return 0;
	}
}

static int _parse_downnodes(void **dest, slurm_parser_enum_t type,
			   const char *key, const char *value,
			   const char *line, char **leftover)
{
	s_p_hashtbl_t *tbl;
	slurm_conf_downnodes_t *n;
	static s_p_options_t _downnodes_options[] = {
		{"Reason", S_P_STRING},
		{"State", S_P_STRING},
		{NULL}
	};

	tbl = s_p_hashtbl_create(_downnodes_options);
	s_p_parse_line(tbl, *leftover, leftover);
	/* s_p_dump_values(tbl, _downnodes_options); */

	n = xmalloc(sizeof(slurm_conf_node_t));
	n->nodenames = xstrdup(value);

	if (!s_p_get_string(&n->reason, "Reason", tbl))
		n->reason = xstrdup("Set in slurm.conf");

	if (!s_p_get_string(&n->state, "State", tbl))
		n->state = NULL;

	s_p_hashtbl_destroy(tbl);

	*dest = (void *)n;

	return 1;
}

static void _destroy_downnodes(void *ptr)
{
	slurm_conf_downnodes_t *n = (slurm_conf_downnodes_t *)ptr;
	xfree(n->nodenames);
	xfree(n->reason);
	xfree(n->state);
	xfree(ptr);
}

extern int slurm_conf_downnodes_array(slurm_conf_downnodes_t **ptr_array[])
{
	int count = 0;
	slurm_conf_downnodes_t **ptr;

	if (s_p_get_array((void ***)&ptr, &count, "DownNodes", conf_hashtbl)) {
		*ptr_array = ptr;
		return count;
	} else {
		*ptr_array = NULL;
		return 0;
	}
}

static void _free_name_hashtbl(void)
{
	int i;
	names_ll_t *p, *q;

	for (i=0; i<NAME_HASH_LEN; i++) {
		p = node_to_host_hashtbl[i];
		while (p) {
			xfree(p->address);
			xfree(p->alias);
			xfree(p->cpu_spec_list);
			xfree(p->hostname);
			q = p->next_alias;
			xfree(p);
			p = q;
		}
		node_to_host_hashtbl[i] = NULL;
		host_to_node_hashtbl[i] = NULL;
	}
	nodehash_initialized = false;
}

static void _init_name_hashtbl(void)
{
	return;
}

static int _get_hash_idx(const char *name)
{
	int index = 0;
	int j;

	if (name == NULL)
		return 0;	/* degenerate case */

	/* Multiply each character by its numerical position in the
	 * name string to add a bit of entropy, because host names such
	 * as cluster[0001-1000] can cause excessive index collisions.
	 */
	for (j = 1; *name; name++, j++)
		index += (int)*name * j;
	index %= NAME_HASH_LEN;
	while (index < 0) /* Coverity thinks "index" could be negative with "if" */
		index += NAME_HASH_LEN;

	return index;
}

static void _push_to_hashtbls(char *alias, char *hostname,
			      char *address, uint16_t port,
			      uint16_t cpus, uint16_t boards,
			      uint16_t sockets, uint16_t cores,
			      uint16_t threads, bool front_end,
			      char *cpu_spec_list, uint16_t core_spec_cnt,
			      uint64_t mem_spec_limit, slurm_addr_t *addr,
			      bool initialized)
{
	int hostname_idx, alias_idx;
	names_ll_t *p, *new;

	alias_idx = _get_hash_idx(alias);
	hostname_idx = _get_hash_idx(hostname);

#if !defined(HAVE_FRONT_END) && !defined(MULTIPLE_SLURMD)
	/* Ensure only one slurmd configured on each host */
	p = host_to_node_hashtbl[hostname_idx];
	while (p) {
		if (xstrcmp(p->hostname, hostname) == 0) {
			error("Duplicated NodeHostName %s in the config file",
			      hostname);
			return;
		}
		p = p->next_hostname;
	}
#endif
	/* Ensure only one instance of each NodeName */
	p = node_to_host_hashtbl[alias_idx];
	while (p) {
		if (xstrcmp(p->alias, alias) == 0) {
			if (front_end) {
				if (local_test_config) {
					error("Frontend not configured correctly "
					      "in slurm.conf.  See man slurm.conf "
					      "look for frontendname.");
					local_test_config = 1;
				} else {
					fatal("Frontend not configured correctly "
					      "in slurm.conf.  See man slurm.conf "
					      "look for frontendname.");
				}
			}
			if (local_test_config) {
				error("Duplicated NodeName %s in the config file",
				      p->alias);
				local_test_config = 1;
			} else {
				fatal("Duplicated NodeName %s in the config file",
				      p->alias);
			}
			return;
		}
		p = p->next_alias;
	}

	/* Create the new data structure and link it into the hash tables */
	new = (names_ll_t *)xmalloc(sizeof(names_ll_t));
	new->alias	= xstrdup(alias);
	new->hostname	= xstrdup(hostname);
	new->address	= xstrdup(address);
	new->port	= port;
	new->cpus	= cpus;
	new->boards	= boards;
	new->sockets	= sockets;
	new->cores	= cores;
	new->threads	= threads;
	new->addr_initialized = initialized;
	new->cpu_spec_list = xstrdup(cpu_spec_list);
	new->core_spec_cnt = core_spec_cnt;
	new->mem_spec_limit = mem_spec_limit;

	if (addr)
		memcpy(&new->addr, addr, sizeof(slurm_addr_t));

	/* Put on end of each list */
	new->next_alias	= NULL;
	if (node_to_host_hashtbl[alias_idx]) {
		p = node_to_host_hashtbl[alias_idx];
		while (p->next_alias)
			p = p->next_alias;
		p->next_alias = new;
	} else {
		node_to_host_hashtbl[alias_idx] = new;
	}

	new->next_hostname = NULL;
	if (host_to_node_hashtbl[hostname_idx]) {
		p = host_to_node_hashtbl[hostname_idx];
		while (p->next_hostname)
			p = p->next_hostname;
		p->next_hostname = new;
	} else {
		host_to_node_hashtbl[hostname_idx] = new;
	}
}

/*
 * Register the given NodeName in the alias table.
 * If node_hostname is NULL, only node_name will be used and
 * no lookup table record is created.
 */
static int _register_conf_node_aliases(slurm_conf_node_t *node_ptr)
{
	hostlist_t address_list = NULL;
	hostlist_t alias_list = NULL;
	hostlist_t hostname_list = NULL;
	hostlist_t port_list = NULL;
	char *address = NULL;
	char *alias = NULL;
	char *hostname = NULL;
	char *port_str = NULL;
	int error_code = SLURM_SUCCESS;
	int address_count, alias_count, hostname_count, port_count, port_int;
	uint16_t port = 0;

	if ((node_ptr->nodenames == NULL) || (node_ptr->nodenames[0] == '\0'))
		return -1;

	if ((address_list = hostlist_create(node_ptr->addresses)) == NULL) {
		error("Unable to create NodeAddr list from %s",
		      node_ptr->addresses);
		error_code = errno;
		goto cleanup;
	}
	if ((alias_list = hostlist_create(node_ptr->nodenames)) == NULL) {
		error("Unable to create NodeName list from %s",
		      node_ptr->nodenames);
		error_code = errno;
		goto cleanup;
	}
	if ((hostname_list = hostlist_create(node_ptr->hostnames)) == NULL) {
		error("Unable to create NodeHostname list from %s",
		      node_ptr->hostnames);
		error_code = errno;
		goto cleanup;
	}

	if (node_ptr->port_str && node_ptr->port_str[0] &&
	    (node_ptr->port_str[0] != '[') &&
	    (strchr(node_ptr->port_str, '-') ||
	     strchr(node_ptr->port_str, ','))) {
		xstrfmtcat(port_str, "[%s]", node_ptr->port_str);
		port_list = hostlist_create(port_str);
		xfree(port_str);
	} else {
		port_list = hostlist_create(node_ptr->port_str);
	}
	if (port_list == NULL) {
		error("Unable to create Port list from %s",
		      node_ptr->port_str);
		error_code = errno;
		goto cleanup;
	}

	if ((slurmdb_setup_cluster_name_dims() > 1)
	    && conf_ptr->node_prefix == NULL)
		_set_node_prefix(node_ptr->nodenames);

	/* some sanity checks */
	address_count  = hostlist_count(address_list);
	alias_count    = hostlist_count(alias_list);
	hostname_count = hostlist_count(hostname_list);
	port_count     = hostlist_count(port_list);
#ifdef HAVE_FRONT_END
	if ((address_count != alias_count) && (address_count != 1)) {
		error("NodeAddr count must equal that of NodeName "
		      "records of there must be no more than one");
		goto cleanup;
	}
	if ((hostname_count != alias_count) && (hostname_count != 1)) {
		error("NodeHostname count must equal that of NodeName "
		      "records of there must be no more than one");
		goto cleanup;
	}
#else
#ifdef MULTIPLE_SLURMD
	if ((address_count != alias_count) && (address_count != 1)) {
		error("NodeAddr count must equal that of NodeName "
		      "records of there must be no more than one");
		goto cleanup;
	}
#else
	if (address_count < alias_count) {
		error("At least as many NodeAddr are required as NodeName");
		goto cleanup;
	}
	if (hostname_count < alias_count) {
		error("At least as many NodeHostname are required "
		      "as NodeName");
		goto cleanup;
	}
#endif	/* MULTIPLE_SLURMD */
#endif	/* HAVE_FRONT_END */
	if ((port_count != alias_count) && (port_count > 1)) {
		error("Port count must equal that of NodeName "
		      "records or there must be no more than one (%u != %u)",
		      port_count, alias_count);
		goto cleanup;
	}

	/* now build the individual node structures */
	while ((alias = hostlist_shift(alias_list))) {
		if (address_count > 0) {
			address_count--;
			if (address)
				free(address);
			address = hostlist_shift(address_list);
		}
		if (hostname_count > 0) {
			hostname_count--;
			if (hostname)
				free(hostname);
			hostname = hostlist_shift(hostname_list);
		}
		if (port_count > 0) {
			port_count--;
			if (port_str)
				free(port_str);
			port_str = hostlist_shift(port_list);
			port_int = atoi(port_str);
			if ((port_int <= 0) || (port_int > 0xffff)) {
				if (local_test_config) {
					error("Invalid Port %s",
					      node_ptr->port_str);
					local_test_config = 1;
				} else {
					fatal("Invalid Port %s",
					      node_ptr->port_str);
				}
			}
			port = port_int;
		}
		_push_to_hashtbls(alias, hostname, address, port,
				  node_ptr->cpus, node_ptr->boards,
				  node_ptr->sockets, node_ptr->cores,
				  node_ptr->threads, 0, node_ptr->cpu_spec_list,
				  node_ptr->core_spec_cnt,
				  node_ptr->mem_spec_limit, NULL, false);
		free(alias);
	}
	if (address)
		free(address);
	if (hostname)
		free(hostname);
	if (port_str)
		free(port_str);

	/* free allocated storage */
cleanup:
	if (address_list)
		hostlist_destroy(address_list);
	if (alias_list)
		hostlist_destroy(alias_list);
	if (hostname_list)
		hostlist_destroy(hostname_list);
	if (port_list)
		hostlist_destroy(port_list);
	return error_code;
}

static int _register_front_ends(slurm_conf_frontend_t *front_end_ptr)
{
	hostlist_t hostname_list = NULL;
	hostlist_t address_list = NULL;
	char *hostname = NULL;
	char *address = NULL;
	int error_code = SLURM_SUCCESS;

	if ((front_end_ptr->frontends == NULL) ||
	    (front_end_ptr->frontends[0] == '\0'))
		return -1;

	if ((hostname_list = hostlist_create(front_end_ptr->frontends))
	     == NULL) {
		error("Unable to create FrontendNames list from %s",
		      front_end_ptr->frontends);
		error_code = errno;
		goto cleanup;
	}
	if ((address_list = hostlist_create(front_end_ptr->addresses))
	     == NULL) {
		error("Unable to create FrontendAddr list from %s",
		      front_end_ptr->addresses);
		error_code = errno;
		goto cleanup;
	}
	if (hostlist_count(address_list) != hostlist_count(hostname_list)) {
		error("Node count mismatch between FrontendNames and "
		      "FrontendAddr");
		goto cleanup;
	}

	while ((hostname = hostlist_shift(hostname_list))) {
		address = hostlist_shift(address_list);
		_push_to_hashtbls(hostname, hostname, address,
				  front_end_ptr->port, 1, 1, 1, 1, 1, 1,
				  NULL, 0, 0, NULL, false);
		free(hostname);
		free(address);
	}

	/* free allocated storage */
cleanup:
	if (hostname_list)
		hostlist_destroy(hostname_list);
	if (address_list)
		hostlist_destroy(address_list);
	return error_code;
}

static void _init_slurmd_nodehash(void)
{
	slurm_conf_node_t **ptr_array;
	slurm_conf_frontend_t **ptr_front_end;
	int count, i;

	if (nodehash_initialized)
		return;
	else
		nodehash_initialized = true;

	if (!conf_initialized) {
		if (_init_slurm_conf(NULL) != SLURM_SUCCESS) {
			if (local_test_config) {
				error("Unable to process slurm.conf file");
				local_test_config_rc = 1;
			} else {
				fatal("Unable to process slurm.conf file");
			}
		}
		conf_initialized = true;
	}

	count = slurm_conf_nodename_array(&ptr_array);
	for (i = 0; i < count; i++)
		_register_conf_node_aliases(ptr_array[i]);

	count = slurm_conf_frontend_array(&ptr_front_end);
	for (i = 0; i < count; i++)
		_register_front_ends(ptr_front_end[i]);
}

/*
 * Caller needs to call slurm_conf_lock() and hold the lock before
 * calling this function (and call slurm_conf_unlock() afterwards).
 */
static char *_internal_get_hostname(const char *node_name)
{
	int idx;
	names_ll_t *p;

	_init_slurmd_nodehash();

	idx = _get_hash_idx(node_name);
	p = node_to_host_hashtbl[idx];
	while (p) {
		if (xstrcmp(p->alias, node_name) == 0) {
			return xstrdup(p->hostname);
		}
		p = p->next_alias;
	}
	return NULL;
}

/*
 * slurm_conf_get_hostname - Return the NodeHostname for given NodeName
 */
extern char *slurm_conf_get_hostname(const char *node_name)
{
	char *hostname = NULL;

	slurm_conf_lock();
	hostname = _internal_get_hostname(node_name);
	slurm_conf_unlock();

	return hostname;
}

/*
 * slurm_conf_get_nodename - Return the NodeName for given NodeHostname
 *
 * NOTE: Call xfree() to release returned value's memory.
 * NOTE: Caller must NOT be holding slurm_conf_lock().
 */
extern char *slurm_conf_get_nodename(const char *node_hostname)
{
	char *alias = NULL;
	int idx;
	names_ll_t *p;
#ifdef HAVE_FRONT_END
	slurm_conf_frontend_t *front_end_ptr = NULL;

 	slurm_conf_lock();
	if (!front_end_list) {
		debug("front_end_list is NULL");
	} else {
		front_end_ptr = list_find_first(front_end_list,
						list_find_frontend,
						(char *) node_hostname);
		if (front_end_ptr) {
			alias = xstrdup(front_end_ptr->frontends);
			slurm_conf_unlock();
			return alias;
		}
	}
#else
	slurm_conf_lock();
#endif

	_init_slurmd_nodehash();
	idx = _get_hash_idx(node_hostname);
	p = host_to_node_hashtbl[idx];
	while (p) {
		if (xstrcmp(p->hostname, node_hostname) == 0) {
			alias = xstrdup(p->alias);
			break;
		}
		p = p->next_hostname;
	}
	slurm_conf_unlock();

	return alias;
}

/*
 * slurm_conf_get_aliases - Return all the nodes NodeName value
 * associated to a given NodeHostname (usefull in case of multiple-slurmd
 * to get the list of virtual nodes associated with a real node)
 *
 * NOTE: Call xfree() to release returned value's memory.
 * NOTE: Caller must NOT be holding slurm_conf_lock().
 */
extern char *slurm_conf_get_aliases(const char *node_hostname)
{
	int idx;
	names_ll_t *p;
	char *aliases = NULL;
	char *s = NULL;

	slurm_conf_lock();
	_init_slurmd_nodehash();
	idx = _get_hash_idx(node_hostname);

	p = host_to_node_hashtbl[idx];
	while (p) {
		if (xstrcmp(p->hostname, node_hostname) == 0) {
			if ( aliases == NULL )
				aliases = xstrdup(p->alias);
			else {
				s = xstrdup_printf("%s %s",aliases,p->alias);
				xfree(aliases);
				aliases = s;
			}
		}
		p = p->next_hostname;
	}
	slurm_conf_unlock();

	return aliases;
}

/*
 * slurm_conf_get_nodeaddr - Return the NodeAddr for given NodeHostname
 *
 * NOTE: Call xfree() to release returned value's memory.
 * NOTE: Caller must NOT be holding slurm_conf_lock().
 */
extern char *slurm_conf_get_nodeaddr(const char *node_hostname)
{
	int idx;
	names_ll_t *p;

	slurm_conf_lock();
	_init_slurmd_nodehash();
	idx = _get_hash_idx(node_hostname);

	p = host_to_node_hashtbl[idx];
	while (p) {
		if (xstrcmp(p->hostname, node_hostname) == 0) {
			char *nodeaddr;
			if (p->address != NULL)
				nodeaddr = xstrdup(p->address);
			else
				nodeaddr = NULL;
			slurm_conf_unlock();
			return nodeaddr;
		}
		p = p->next_hostname;
	}
	slurm_conf_unlock();

	return NULL;
}

/*
 * slurm_conf_get_nodename_from_addr - Return the NodeName for given NodeAddr
 *
 * NOTE: Call xfree() to release returned value's memory.
 * NOTE: Caller must NOT be holding slurm_conf_lock().
 */
extern char *slurm_conf_get_nodename_from_addr(const char *node_addr)
{
	char hostname[NI_MAXHOST];
	unsigned long addr = inet_addr(node_addr);
	char *start_name, *ret_name = NULL, *dot_ptr;

	if (get_name_info((struct sockaddr *)&addr,
			  sizeof(addr), hostname) != 0) {
		error("%s: No node found with addr %s", __func__, node_addr);
		return NULL;
	}

	if (!xstrcmp(hostname, "localhost")) {
		start_name = xshort_hostname();
	} else {
		start_name = xstrdup(hostname);
		dot_ptr = strchr(start_name, '.');
		if (dot_ptr)
			dot_ptr[0] = '\0';
	}

	ret_name = slurm_conf_get_aliases(start_name);
	xfree(start_name);

	return ret_name;
}

/*
 * slurm_conf_get_aliased_nodename - Return the NodeName for the
 * complete hostname string returned by gethostname if there is
 * such a match, otherwise iterate through any aliases returned
 * by get_host_by_name
 */
extern char *slurm_conf_get_aliased_nodename()
{
	char hostname_full[1024];
	int error_code;
	char *nodename;

	error_code = gethostname(hostname_full, sizeof(hostname_full));
	/* we shouldn't have any problem here since by the time
	 * this function has been called, gethostname_short,
	 * which invokes gethostname, has probably already been called
	 * successfully, so just return NULL if something weird
	 * happens at this point
	 */
	if (error_code)
		return NULL;

	nodename = slurm_conf_get_nodename(hostname_full);
	/* if the full hostname did not match a nodename */
	if (nodename == NULL) {
		/* use get_host_by_name; buffer sizes, semantics, etc.
		 * copied from slurm_protocol_socket_implementation.c
		 */
		struct hostent * he = NULL;
		char * h_buf[4096];
		int h_err;

		he = get_host_by_name(hostname_full, (void *)&h_buf,
				      sizeof(h_buf), &h_err);
		if (he != NULL) {
			unsigned int i = 0;
			/* check the "official" host name first */
			nodename = slurm_conf_get_nodename(he->h_name);
			while ((nodename == NULL) &&
			       (he->h_aliases[i] != NULL)) {
				/* the "official" name still didn't match --
				 * iterate through the aliases */
				nodename =
				     slurm_conf_get_nodename(he->h_aliases[i]);
				i++;
			}
		}
	}

	return nodename;
}

/*
 * slurm_conf_get_port - Return the port for a given NodeName
 */
extern uint16_t slurm_conf_get_port(const char *node_name)
{
	int idx;
	names_ll_t *p;

	slurm_conf_lock();
	_init_slurmd_nodehash();

	idx = _get_hash_idx(node_name);
	p = node_to_host_hashtbl[idx];
	while (p) {
		if (xstrcmp(p->alias, node_name) == 0) {
			uint16_t port;
			if (!p->port)
				p->port = (uint16_t) conf_ptr->slurmd_port;
			port = p->port;
			slurm_conf_unlock();
			return port;
		}
		p = p->next_alias;
	}
	slurm_conf_unlock();

	return 0;
}

/*
 * slurm_reset_alias - Reset the address and hostname of a specific node name
 */
extern void slurm_reset_alias(char *node_name, char *node_addr,
			      char *node_hostname)
{
	int idx;
	names_ll_t *p;

	slurm_conf_lock();
	_init_slurmd_nodehash();

	idx = _get_hash_idx(node_name);
	p = node_to_host_hashtbl[idx];
	while (p) {
		if (xstrcmp(p->alias, node_name) == 0) {
			if (node_addr) {
				xfree(p->address);
				p->address = xstrdup(node_addr);
				p->addr_initialized = false;
			}
			if (node_hostname) {
				xfree(p->hostname);
				p->hostname = xstrdup(node_hostname);
			}
			break;
		}
		p = p->next_alias;
	}
	slurm_conf_unlock();

	return;
}

/*
 * slurm_conf_get_addr - Return the slurm_addr_t for a given NodeName
 * Returns SLURM_SUCCESS on success, SLURM_FAILURE on failure.
 */
extern int slurm_conf_get_addr(const char *node_name, slurm_addr_t *address)
{
	int idx;
	names_ll_t *p;

	slurm_conf_lock();
	_init_slurmd_nodehash();

	idx = _get_hash_idx(node_name);
	p = node_to_host_hashtbl[idx];
	while (p) {
		if (xstrcmp(p->alias, node_name) == 0) {
			if (!p->port)
				p->port = (uint16_t) conf_ptr->slurmd_port;
			if (!p->addr_initialized) {
				slurm_set_addr(&p->addr, p->port, p->address);
				if (p->addr.sin_family == 0 &&
				    p->addr.sin_port == 0) {
					slurm_conf_unlock();
					return SLURM_FAILURE;
				}
				p->addr_initialized = true;
			}
			*address = p->addr;
			slurm_conf_unlock();
			return SLURM_SUCCESS;
		}
		p = p->next_alias;
	}
	slurm_conf_unlock();

	return SLURM_FAILURE;
}

/*
 * slurm_conf_get_cpus_bsct -
 * Return the cpus, boards, sockets, cores, and threads configured for a
 * given NodeName
 * Returns SLURM_SUCCESS on success, SLURM_FAILURE on failure.
 */
extern int slurm_conf_get_cpus_bsct(const char *node_name,
				    uint16_t *cpus, uint16_t *boards,
				    uint16_t *sockets, uint16_t *cores,
				    uint16_t *threads)
{
	int idx;
	names_ll_t *p;

	slurm_conf_lock();
	_init_slurmd_nodehash();

	idx = _get_hash_idx(node_name);
	p = node_to_host_hashtbl[idx];
	while (p) {
		if (xstrcmp(p->alias, node_name) == 0) {
		    	if (cpus)
				*cpus    = p->cpus;
			if (boards)
				*boards  = p->boards;
			if (sockets)
				*sockets = p->sockets;
			if (cores)
				*cores   = p->cores;
			if (threads)
				*threads = p->threads;
			slurm_conf_unlock();
			return SLURM_SUCCESS;
		}
		p = p->next_alias;
	}
	slurm_conf_unlock();

	return SLURM_FAILURE;
}

/*
 * slurm_conf_get_res_spec_info - Return resource specialization info
 * for a given NodeName
 * Returns SLURM_SUCCESS on success, SLURM_FAILURE on failure.
 */
extern int slurm_conf_get_res_spec_info(const char *node_name,
					char **cpu_spec_list,
					uint16_t *core_spec_cnt,
					uint64_t *mem_spec_limit)
{
	int idx;
	names_ll_t *p;

	slurm_conf_lock();
	_init_slurmd_nodehash();

	idx = _get_hash_idx(node_name);
	p = node_to_host_hashtbl[idx];
	while (p) {
		if (xstrcmp(p->alias, node_name) == 0) {
			if (core_spec_cnt)
				*cpu_spec_list = xstrdup(p->cpu_spec_list);
			if (core_spec_cnt)
				*core_spec_cnt  = p->core_spec_cnt;
			if (mem_spec_limit)
				*mem_spec_limit = p->mem_spec_limit;
			slurm_conf_unlock();
			return SLURM_SUCCESS;
		}
		p = p->next_alias;
	}
	slurm_conf_unlock();

	return SLURM_FAILURE;
}

/* gethostname_short - equivalent to gethostname, but return only the first
 * component of the fully qualified name
 * (e.g. "linux123.foo.bar" becomes "linux123")
 * OUT name
 */
int
gethostname_short (char *name, size_t len)
{
	int error_code, name_len;
	char *dot_ptr, path_name[1024];

	error_code = gethostname (path_name, sizeof(path_name));
	if (error_code)
		return error_code;

	dot_ptr = strchr (path_name, '.');
	if (dot_ptr == NULL)
		dot_ptr = path_name + strlen(path_name);
	else
		dot_ptr[0] = '\0';

	name_len = (dot_ptr - path_name);
	if (name_len > len)
		return ENAMETOOLONG;

	strcpy (name, path_name);
	return 0;
}

/*
 * free_slurm_conf - free all storage associated with a slurm_ctl_conf_t.
 * IN/OUT ctl_conf_ptr - pointer to data structure to be freed
 * IN purge_node_hash - purge system-wide node hash table if set,
 *			set to zero if clearing private copy of config data
 */
extern void
free_slurm_conf (slurm_ctl_conf_t *ctl_conf_ptr, bool purge_node_hash)
{
	int i;

	xfree (ctl_conf_ptr->accounting_storage_backup_host);
	xfree (ctl_conf_ptr->accounting_storage_host);
	xfree (ctl_conf_ptr->accounting_storage_loc);
	xfree (ctl_conf_ptr->accounting_storage_pass);
	xfree (ctl_conf_ptr->accounting_storage_tres);
	xfree (ctl_conf_ptr->accounting_storage_type);
	xfree (ctl_conf_ptr->accounting_storage_user);
	FREE_NULL_LIST(ctl_conf_ptr->acct_gather_conf);
	xfree (ctl_conf_ptr->acct_gather_energy_type);
	xfree (ctl_conf_ptr->acct_gather_profile_type);
	xfree (ctl_conf_ptr->acct_gather_interconnect_type);
	xfree (ctl_conf_ptr->acct_gather_filesystem_type);
	xfree (ctl_conf_ptr->authinfo);
	xfree (ctl_conf_ptr->authtype);
	xfree (ctl_conf_ptr->bb_type);
	FREE_NULL_LIST(ctl_conf_ptr->cgroup_conf);
	xfree (ctl_conf_ptr->checkpoint_type);
	xfree (ctl_conf_ptr->cluster_name);
	for (i = 0; i < ctl_conf_ptr->control_cnt; i++) {
		xfree(ctl_conf_ptr->control_addr[i]);
		xfree(ctl_conf_ptr->control_machine[i]);
	}

	xfree (ctl_conf_ptr->comm_params);
	xfree (ctl_conf_ptr->control_addr);
	xfree (ctl_conf_ptr->control_machine);
	xfree (ctl_conf_ptr->core_spec_plugin);
	xfree (ctl_conf_ptr->crypto_type);
	xfree (ctl_conf_ptr->epilog);
	xfree (ctl_conf_ptr->epilog_slurmctld);
	FREE_NULL_LIST(ctl_conf_ptr->ext_sensors_conf);
	xfree (ctl_conf_ptr->ext_sensors_type);
	xfree (ctl_conf_ptr->fed_params);
	xfree (ctl_conf_ptr->gres_plugins);
	xfree (ctl_conf_ptr->health_check_program);
	xfree (ctl_conf_ptr->job_acct_gather_freq);
	xfree (ctl_conf_ptr->job_acct_gather_type);
	xfree (ctl_conf_ptr->job_acct_gather_params);
	xfree (ctl_conf_ptr->job_ckpt_dir);
	xfree (ctl_conf_ptr->job_comp_host);
	xfree (ctl_conf_ptr->job_comp_loc);
	xfree (ctl_conf_ptr->job_comp_pass);
	xfree (ctl_conf_ptr->job_comp_type);
	xfree (ctl_conf_ptr->job_comp_user);
	xfree (ctl_conf_ptr->job_container_plugin);
	xfree (ctl_conf_ptr->job_credential_private_key);
	xfree (ctl_conf_ptr->job_credential_public_certificate);
	FREE_NULL_LIST(ctl_conf_ptr->job_defaults_list);
	xfree (ctl_conf_ptr->job_submit_plugins);
	xfree (ctl_conf_ptr->launch_params);
	xfree (ctl_conf_ptr->launch_type);
	xfree (ctl_conf_ptr->layouts);
	xfree (ctl_conf_ptr->licenses);
	xfree (ctl_conf_ptr->licenses_used);
	xfree (ctl_conf_ptr->mail_domain);
	xfree (ctl_conf_ptr->mail_prog);
	xfree (ctl_conf_ptr->mcs_plugin);
	xfree (ctl_conf_ptr->mcs_plugin_params);
	xfree (ctl_conf_ptr->mpi_default);
	xfree (ctl_conf_ptr->mpi_params);
	xfree (ctl_conf_ptr->msg_aggr_params);
	FREE_NULL_LIST(ctl_conf_ptr->node_features_conf);
	xfree (ctl_conf_ptr->node_features_plugins);
	xfree (ctl_conf_ptr->node_prefix);
	xfree (ctl_conf_ptr->plugindir);
	xfree (ctl_conf_ptr->plugstack);
	FREE_NULL_LIST(ctl_conf_ptr->slurmctld_plugstack_conf);
	xfree (ctl_conf_ptr->power_parameters);
	xfree (ctl_conf_ptr->power_plugin);
	xfree (ctl_conf_ptr->preempt_type);
	xfree (ctl_conf_ptr->priority_params);
	xfree (ctl_conf_ptr->priority_type);
	xfree (ctl_conf_ptr->priority_weight_tres);
	xfree (ctl_conf_ptr->proctrack_type);
	xfree (ctl_conf_ptr->prolog);
	xfree (ctl_conf_ptr->prolog_slurmctld);
	xfree (ctl_conf_ptr->propagate_rlimits);
	xfree (ctl_conf_ptr->propagate_rlimits_except);
	xfree (ctl_conf_ptr->reboot_program);
	xfree (ctl_conf_ptr->requeue_exit);
	xfree (ctl_conf_ptr->requeue_exit_hold);
	xfree (ctl_conf_ptr->resume_fail_program);
	xfree (ctl_conf_ptr->resume_program);
	xfree (ctl_conf_ptr->resv_epilog);
	xfree (ctl_conf_ptr->resv_prolog);
	xfree (ctl_conf_ptr->route_plugin);
	xfree (ctl_conf_ptr->salloc_default_command);
	xfree (ctl_conf_ptr->sbcast_parameters);
	xfree (ctl_conf_ptr->sched_logfile);
	xfree (ctl_conf_ptr->sched_params);
	xfree (ctl_conf_ptr->schedtype);
	xfree (ctl_conf_ptr->select_type);
	FREE_NULL_LIST(ctl_conf_ptr->select_conf_key_pairs);
	xfree (ctl_conf_ptr->slurm_conf);
	xfree (ctl_conf_ptr->slurm_user_name);
	xfree (ctl_conf_ptr->slurmctld_addr);
	xfree (ctl_conf_ptr->slurmctld_logfile);
	xfree (ctl_conf_ptr->slurmctld_pidfile);
	xfree (ctl_conf_ptr->slurmctld_plugstack);
	xfree (ctl_conf_ptr->slurmctld_primary_off_prog);
	xfree (ctl_conf_ptr->slurmctld_primary_on_prog);
	xfree (ctl_conf_ptr->slurmd_logfile);
	xfree (ctl_conf_ptr->slurmctld_params);
	xfree (ctl_conf_ptr->slurmd_params);
	xfree (ctl_conf_ptr->slurmd_pidfile);
	xfree (ctl_conf_ptr->slurmd_spooldir);
	xfree (ctl_conf_ptr->slurmd_user_name);
	xfree (ctl_conf_ptr->srun_epilog);
	xfree (ctl_conf_ptr->srun_port_range);
	xfree (ctl_conf_ptr->srun_prolog);
	xfree (ctl_conf_ptr->state_save_location);
	xfree (ctl_conf_ptr->suspend_exc_nodes);
	xfree (ctl_conf_ptr->suspend_exc_parts);
	xfree (ctl_conf_ptr->suspend_program);
	xfree (ctl_conf_ptr->switch_type);
	xfree (ctl_conf_ptr->task_epilog);
	xfree (ctl_conf_ptr->task_plugin);
	xfree (ctl_conf_ptr->task_prolog);
	xfree (ctl_conf_ptr->tmp_fs);
	xfree (ctl_conf_ptr->topology_param);
	xfree (ctl_conf_ptr->topology_plugin);
	xfree (ctl_conf_ptr->unkillable_program);
	xfree (ctl_conf_ptr->version);
	xfree (ctl_conf_ptr->x11_params);

	if (purge_node_hash)
		_free_name_hashtbl();
}

/*
 * init_slurm_conf - initialize or re-initialize the slurm configuration
 *	values to defaults (NULL or NO_VAL). Note that the configuration
 *	file pathname (slurm_conf) is not changed.
 * IN/OUT ctl_conf_ptr - pointer to data structure to be initialized
 */
void
init_slurm_conf (slurm_ctl_conf_t *ctl_conf_ptr)
{
	int i;

	ctl_conf_ptr->last_update		= time(NULL);
	xfree (ctl_conf_ptr->accounting_storage_backup_host);
	ctl_conf_ptr->accounting_storage_enforce          = 0;
	xfree (ctl_conf_ptr->accounting_storage_host);
	xfree (ctl_conf_ptr->accounting_storage_loc);
	xfree (ctl_conf_ptr->accounting_storage_pass);
	ctl_conf_ptr->accounting_storage_port             = 0;
	xfree (ctl_conf_ptr->accounting_storage_tres);
	xfree (ctl_conf_ptr->accounting_storage_type);
	xfree (ctl_conf_ptr->accounting_storage_user);
	xfree (ctl_conf_ptr->authinfo);
	xfree (ctl_conf_ptr->authtype);
	ctl_conf_ptr->batch_start_timeout	= 0;
	xfree (ctl_conf_ptr->bb_type);
	xfree (ctl_conf_ptr->checkpoint_type);
	xfree (ctl_conf_ptr->cluster_name);
	xfree (ctl_conf_ptr->comm_params);
	ctl_conf_ptr->complete_wait		= NO_VAL16;
	for (i = 0; i < ctl_conf_ptr->control_cnt; i++) {
		xfree(ctl_conf_ptr->control_addr[i]);
		xfree(ctl_conf_ptr->control_machine[i]);
	}
	ctl_conf_ptr->control_cnt = 0;
	xfree (ctl_conf_ptr->control_addr);
	xfree (ctl_conf_ptr->control_machine);
	xfree (ctl_conf_ptr->core_spec_plugin);
	xfree (ctl_conf_ptr->crypto_type);
	ctl_conf_ptr->def_mem_per_cpu           = 0;
	ctl_conf_ptr->debug_flags		= 0;
	ctl_conf_ptr->disable_root_jobs         = 0;
	ctl_conf_ptr->acct_gather_node_freq	= 0;
	xfree (ctl_conf_ptr->acct_gather_energy_type);
	xfree (ctl_conf_ptr->acct_gather_profile_type);
	xfree (ctl_conf_ptr->acct_gather_interconnect_type);
	xfree (ctl_conf_ptr->acct_gather_filesystem_type);
	ctl_conf_ptr->ext_sensors_freq		= 0;
	xfree (ctl_conf_ptr->ext_sensors_type);
	ctl_conf_ptr->enforce_part_limits       = 0;
	xfree (ctl_conf_ptr->epilog);
	ctl_conf_ptr->epilog_msg_time		= NO_VAL;
	ctl_conf_ptr->fast_schedule		= NO_VAL16;
	xfree(ctl_conf_ptr->fed_params);
	ctl_conf_ptr->first_job_id		= NO_VAL;
	ctl_conf_ptr->get_env_timeout		= 0;
	xfree(ctl_conf_ptr->gres_plugins);
	ctl_conf_ptr->group_time		= 0;
	ctl_conf_ptr->group_force		= 0;
	ctl_conf_ptr->hash_val			= NO_VAL;
	ctl_conf_ptr->health_check_interval	= 0;
	xfree(ctl_conf_ptr->health_check_program);
	ctl_conf_ptr->inactive_limit		= NO_VAL16;
	xfree (ctl_conf_ptr->job_acct_gather_freq);
	xfree (ctl_conf_ptr->job_acct_gather_type);
	xfree (ctl_conf_ptr->job_acct_gather_params);
	xfree (ctl_conf_ptr->job_ckpt_dir);
	xfree (ctl_conf_ptr->job_comp_loc);
	xfree (ctl_conf_ptr->job_comp_pass);
	ctl_conf_ptr->job_comp_port             = 0;
	xfree (ctl_conf_ptr->job_comp_type);
	xfree (ctl_conf_ptr->job_comp_user);
	xfree (ctl_conf_ptr->job_container_plugin);
	xfree (ctl_conf_ptr->job_credential_private_key);
	xfree (ctl_conf_ptr->job_credential_public_certificate);
	FREE_NULL_LIST(ctl_conf_ptr->job_defaults_list);
	ctl_conf_ptr->job_file_append		= NO_VAL16;
	ctl_conf_ptr->job_requeue		= NO_VAL16;
	xfree(ctl_conf_ptr->job_submit_plugins);
	ctl_conf_ptr->keep_alive_time		= NO_VAL16;
	ctl_conf_ptr->kill_on_bad_exit		= 0;
	ctl_conf_ptr->kill_wait			= NO_VAL16;
	xfree (ctl_conf_ptr->launch_params);
	xfree (ctl_conf_ptr->launch_type);
	xfree (ctl_conf_ptr->layouts);
	xfree (ctl_conf_ptr->licenses);
	xfree (ctl_conf_ptr->mail_domain);
	xfree (ctl_conf_ptr->mail_prog);
	ctl_conf_ptr->max_array_sz		= NO_VAL;
	ctl_conf_ptr->max_job_cnt		= NO_VAL;
	ctl_conf_ptr->max_job_id		= NO_VAL;
	ctl_conf_ptr->max_mem_per_cpu           = 0;
	ctl_conf_ptr->max_step_cnt		= NO_VAL;
	xfree(ctl_conf_ptr->mcs_plugin);
	xfree(ctl_conf_ptr->mcs_plugin_params);
	ctl_conf_ptr->mem_limit_enforce         = false;
	ctl_conf_ptr->min_job_age = NO_VAL;
	xfree (ctl_conf_ptr->mpi_default);
	xfree (ctl_conf_ptr->mpi_params);
	xfree (ctl_conf_ptr->msg_aggr_params);
	ctl_conf_ptr->msg_timeout		= NO_VAL16;
	ctl_conf_ptr->next_job_id		= NO_VAL;
	xfree(ctl_conf_ptr->node_features_plugins);
	xfree (ctl_conf_ptr->node_prefix);
	ctl_conf_ptr->over_time_limit           = 0;
	xfree (ctl_conf_ptr->plugindir);
	xfree (ctl_conf_ptr->plugstack);
	xfree (ctl_conf_ptr->power_parameters);
	xfree (ctl_conf_ptr->power_plugin);
	ctl_conf_ptr->preempt_mode              = 0;
	xfree (ctl_conf_ptr->preempt_type);
	xfree (ctl_conf_ptr->priority_params);
	xfree (ctl_conf_ptr->priority_type);
	xfree (ctl_conf_ptr->priority_weight_tres);
	ctl_conf_ptr->private_data              = 0;
	xfree (ctl_conf_ptr->proctrack_type);
	xfree (ctl_conf_ptr->prolog);
	ctl_conf_ptr->prolog_flags				= 0;
	ctl_conf_ptr->propagate_prio_process	= NO_VAL16;
	xfree (ctl_conf_ptr->propagate_rlimits);
	xfree (ctl_conf_ptr->propagate_rlimits_except);
	xfree (ctl_conf_ptr->reboot_program);
	ctl_conf_ptr->reconfig_flags		= 0;
	xfree(ctl_conf_ptr->requeue_exit);
	xfree(ctl_conf_ptr->requeue_exit_hold);
	ctl_conf_ptr->resume_timeout		= 0;
	xfree (ctl_conf_ptr->resume_fail_program);
	xfree (ctl_conf_ptr->resume_program);
	ctl_conf_ptr->resume_rate		= NO_VAL16;
	xfree (ctl_conf_ptr->resv_epilog);
	ctl_conf_ptr->resv_over_run		= 0;
	xfree (ctl_conf_ptr->resv_prolog);
	ctl_conf_ptr->ret2service		= NO_VAL16;
	xfree (ctl_conf_ptr->route_plugin);
	xfree( ctl_conf_ptr->salloc_default_command);
	xfree( ctl_conf_ptr->sbcast_parameters);
	xfree( ctl_conf_ptr->sched_params );
	ctl_conf_ptr->sched_time_slice		= NO_VAL16;
	xfree( ctl_conf_ptr->schedtype );
	xfree( ctl_conf_ptr->select_type );
	ctl_conf_ptr->select_type_param         = NO_VAL16;
	ctl_conf_ptr->slurm_user_id		= NO_VAL16;
	xfree (ctl_conf_ptr->slurm_user_name);
	ctl_conf_ptr->slurmd_user_id		= NO_VAL16;
	xfree (ctl_conf_ptr->slurmd_user_name);
	ctl_conf_ptr->slurmctld_debug		= NO_VAL16;
	xfree (ctl_conf_ptr->slurmctld_logfile);
	ctl_conf_ptr->slurmctld_syslog_debug    = NO_VAL16;
	xfree (ctl_conf_ptr->sched_logfile);
	ctl_conf_ptr->sched_log_level		= NO_VAL16;
	xfree (ctl_conf_ptr->slurmctld_addr);
	xfree (ctl_conf_ptr->slurmctld_pidfile);
	xfree (ctl_conf_ptr->slurmctld_plugstack);
	ctl_conf_ptr->slurmctld_port		= NO_VAL;
	ctl_conf_ptr->slurmctld_port_count	= 1;
	xfree (ctl_conf_ptr->slurmctld_primary_off_prog);
	xfree (ctl_conf_ptr->slurmctld_primary_on_prog);
	ctl_conf_ptr->slurmctld_timeout		= NO_VAL16;
	xfree (ctl_conf_ptr->slurmctld_params);
	ctl_conf_ptr->slurmd_debug		= NO_VAL16;
	xfree (ctl_conf_ptr->slurmd_logfile);
	xfree (ctl_conf_ptr->slurmd_params);
	ctl_conf_ptr->slurmd_syslog_debug       = NO_VAL16;
	xfree (ctl_conf_ptr->slurmd_pidfile);
 	ctl_conf_ptr->slurmd_port		= NO_VAL;
	xfree (ctl_conf_ptr->slurmd_spooldir);
	ctl_conf_ptr->slurmd_timeout		= NO_VAL16;
	xfree (ctl_conf_ptr->srun_prolog);
	xfree (ctl_conf_ptr->srun_epilog);
	xfree (ctl_conf_ptr->state_save_location);
	xfree (ctl_conf_ptr->suspend_exc_nodes);
	xfree (ctl_conf_ptr->suspend_exc_parts);
	xfree (ctl_conf_ptr->suspend_program);
	ctl_conf_ptr->suspend_rate		= NO_VAL16;
	ctl_conf_ptr->suspend_time		= NO_VAL16;
	ctl_conf_ptr->suspend_timeout		= 0;
	xfree (ctl_conf_ptr->switch_type);
	xfree (ctl_conf_ptr->task_epilog);
	xfree (ctl_conf_ptr->task_plugin);
	ctl_conf_ptr->task_plugin_param		= 0;
	xfree (ctl_conf_ptr->task_prolog);
	ctl_conf_ptr->tcp_timeout		= NO_VAL16;
	xfree (ctl_conf_ptr->tmp_fs);
	xfree (ctl_conf_ptr->topology_param);
	xfree (ctl_conf_ptr->topology_plugin);
	ctl_conf_ptr->tree_width       		= NO_VAL16;
	xfree (ctl_conf_ptr->unkillable_program);
	ctl_conf_ptr->unkillable_timeout        = NO_VAL16;
	ctl_conf_ptr->use_pam			= 0;
	ctl_conf_ptr->use_spec_resources	= 0;
	ctl_conf_ptr->vsize_factor              = 0;
	ctl_conf_ptr->wait_time			= NO_VAL16;
	xfree (ctl_conf_ptr->x11_params);
	ctl_conf_ptr->prolog_epilog_timeout = NO_VAL16;

	_free_name_hashtbl();
	_init_name_hashtbl();

	return;
}

/* handle config name in form (example) slurmdbd:cluster0:10.0.0.254:6819
 *
 * NOTE: Changes are required in the accounting_storage/slurmdbd plugin in
 * order for this to work as desired. Andriy Grytsenko (Massive Solutions
 * Limited) has a private accounting_storage plugin with this functionality */
static int _config_is_storage(s_p_hashtbl_t *hashtbl, char *name)
{
	char *cluster, *host, *port;
	void *db_conn;
	config_key_pair_t *pair;
	List config;
	ListIterator iter;
	int rc = -1;

	cluster = strchr(name, ':');
	if (cluster == NULL)
		return (-1);
	host = strchr(&cluster[1], ':');
	if (host == NULL)
		return (-1);
	port = strrchr(&host[1], ':');
	if (port == NULL)
		return (-1);
	conf_ptr->accounting_storage_type =
		xstrdup_printf("accounting_storage/%.*s",
			       (int)(cluster - name), name);
	cluster++;
	cluster = xstrndup(cluster, host - cluster);
	host++;
	conf_ptr->accounting_storage_host = xstrndup(host, port - host);
	port++;
	debug3("trying retrieve config via %s from host %s on port %s",
	       conf_ptr->accounting_storage_type,
	       conf_ptr->accounting_storage_host, port);
	conf_ptr->accounting_storage_port = atoi(port);
	conf_ptr->plugindir = xstrdup(default_plugin_path);
	/* unlock conf_lock and set as initialized before accessing it */
	conf_initialized = true;
	slurm_mutex_unlock(&conf_lock);
	db_conn = acct_storage_g_get_connection(NULL, 0, NULL, false, NULL);
	if (db_conn == NULL)
		goto end; /* plugin will out error itself */
	config = acct_storage_g_get_config(db_conn, "slurm.conf");
	acct_storage_g_close_connection(&db_conn); /* ignore error code */
	if (config == NULL) {
		error("cannot retrieve config from storage");
		goto end;
	}
	iter = list_iterator_create(config);
	while ((pair = list_next(iter)) != NULL)
		s_p_parse_pair(hashtbl, pair->name, pair->value);
	list_iterator_destroy(iter);
	FREE_NULL_LIST(config);
	rc = 0; /* done */

end:
	/* restore status quo now */
	slurm_mutex_lock(&conf_lock);
	conf_initialized = false;
	xfree(cluster);
	xfree(conf_ptr->accounting_storage_type);
	xfree(conf_ptr->accounting_storage_host);
	xfree(conf_ptr->plugindir);
	conf_ptr->accounting_storage_type = NULL;
	conf_ptr->accounting_storage_host = NULL;
	conf_ptr->plugindir = NULL;
	return (rc);
}

/* caller must lock conf_lock */
static int _init_slurm_conf(const char *file_name)
{
	char *name = (char *)file_name;
	int rc = SLURM_SUCCESS;

	if (name == NULL) {
		name = getenv("SLURM_CONF");
		if (name == NULL)
			name = default_slurm_config_file;
	}
	if (conf_initialized)
		error("the conf_hashtbl is already inited");
	debug("Reading slurm.conf file: %s", name);
	conf_hashtbl = s_p_hashtbl_create(slurm_conf_options);
	conf_ptr->last_update = time(NULL);

	/* init hash to 0 */
	conf_ptr->hash_val = 0;
	if ((_config_is_storage(conf_hashtbl, name) < 0) &&
	    (s_p_parse_file(conf_hashtbl, &conf_ptr->hash_val, name, false)
	     == SLURM_ERROR)) {
		rc = SLURM_ERROR;
	}
	/* s_p_dump_values(conf_hashtbl, slurm_conf_options); */

	if (_validate_and_set_defaults(conf_ptr, conf_hashtbl) == SLURM_ERROR)
		rc = SLURM_ERROR;
	conf_ptr->slurm_conf = xstrdup(name);

	return rc;
}

/* caller must lock conf_lock */
static void
_destroy_slurm_conf(void)
{
	s_p_hashtbl_destroy(conf_hashtbl);
	if (default_frontend_tbl != NULL) {
		s_p_hashtbl_destroy(default_frontend_tbl);
		default_frontend_tbl = NULL;
	}
	if (default_nodename_tbl != NULL) {
		s_p_hashtbl_destroy(default_nodename_tbl);
		default_nodename_tbl = NULL;
	}
	if (default_partition_tbl != NULL) {
		s_p_hashtbl_destroy(default_partition_tbl);
		default_partition_tbl = NULL;
	}
	free_slurm_conf(conf_ptr, true);
	conf_initialized = false;

	/* xfree(conf_ptr); */
}

/*
 * slurm_conf_init - load the slurm configuration from the a file.
 * IN file_name - name of the slurm configuration file to be read
 *	If file_name is NULL, then this routine tries to use
 *	the value in the SLURM_CONF env variable.  Failing that,
 *	it uses the compiled-in default file name.
 *	If the conf structures have already been initialized by a call to
 *	slurm_conf_init, any subsequent calls will do nothing until
 *	slurm_conf_destroy is called.
 * RET SLURM_SUCCESS if conf file is initialized.  If the slurm conf
 *       was already initialied, return SLURM_ERROR.
 */
extern int
slurm_conf_init(const char *file_name)
{
	slurm_mutex_lock(&conf_lock);

	if (conf_initialized) {
		slurm_mutex_unlock(&conf_lock);
		return SLURM_ERROR;
	}

#ifndef NDEBUG
	/*
	 * This is done here to ensure all user commands parse this once at
	 * launch, rather than trying to test this during each RPC call.
	 * This environment variable is undocumented, and only
	 * respected in development builds. When set, the remote end
	 * will treat the request as if it was issued by an unprivileged
	 * user account rather than the (likely elevated) privileges that
	 * the account usually operates under. This makes it possible to
	 * test various access controls while running the testsuite under
	 * a single user account.
	 */
	if (getenv("SLURM_TESTSUITE_DROP_PRIV"))
		drop_priv_flag = SLURM_DROP_PRIV;
#endif

	init_slurm_conf(conf_ptr);
	if (_init_slurm_conf(file_name) != SLURM_SUCCESS) {
		if (local_test_config) {
			error("Unable to process configuration file");
			local_test_config_rc = 1;
		} else {
			fatal("Unable to process configuration file");
		}
	}
	conf_initialized = true;

	slurm_mutex_unlock(&conf_lock);
	return SLURM_SUCCESS;
}

static int _internal_reinit(const char *file_name)
{
	char *name = (char *)file_name;
	int rc = SLURM_SUCCESS;

	if (name == NULL) {
		name = getenv("SLURM_CONF");
		if (name == NULL)
			name = default_slurm_config_file;
	}

	if (conf_initialized) {
		/* could check modified time on slurm.conf here */
		_destroy_slurm_conf();
	}

	if (_init_slurm_conf(name) != SLURM_SUCCESS) {
		if (local_test_config) {
			error("Unable to process configuration file");
			local_test_config_rc = 1;
		} else {
			fatal("Unable to process configuration file");
		}
	}
	conf_initialized = true;


	return rc;
}

/*
 * slurm_conf_reinit - reload the slurm configuration from a file.
 * IN file_name - name of the slurm configuration file to be read
 *	If file_name is NULL, then this routine tries to use
 *	the value in the SLURM_CONF env variable.  Failing that,
 *	it uses the compiled-in default file name.
 *	Unlike slurm_conf_init, slurm_conf_reinit will always reread the
 *	file and reinitialize the configuration structures.
 * RET SLURM_SUCCESS if conf file is reinitialized, otherwise SLURM_ERROR.
 */
extern int
slurm_conf_reinit(const char *file_name)
{
	int rc;

	slurm_mutex_lock(&conf_lock);
	rc = _internal_reinit(file_name);
	slurm_mutex_unlock(&conf_lock);

	return rc;
}

extern void
slurm_conf_mutex_init(void)
{
	slurm_mutex_init(&conf_lock);
}

extern void
slurm_conf_install_fork_handlers(void)
{
	int err;
	if ((err = pthread_atfork(NULL, NULL, &slurm_conf_mutex_init)))
		fatal("can't install slurm_conf atfork handler");
	return;
}

extern int
slurm_conf_destroy(void)
{
	slurm_mutex_lock(&conf_lock);

	if (!conf_initialized) {
		slurm_mutex_unlock(&conf_lock);
		return SLURM_SUCCESS;
	}

	_destroy_slurm_conf();

	slurm_mutex_unlock(&conf_lock);

	return SLURM_SUCCESS;
}

extern slurm_ctl_conf_t *
slurm_conf_lock(void)
{
	int i;

	slurm_mutex_lock(&conf_lock);
	if (!conf_initialized) {
		if (_init_slurm_conf(NULL) != SLURM_SUCCESS) {
			/*
			 * Clearing control_addr array entries results in
			 * error for most APIs without generating a fatal
			 * error and exiting. Slurm commands and daemons
			 * should call slurm_conf_init() to get a fatal
			 * error instead.
			 */
			for (i = 0; i < conf_ptr->control_cnt; i++)
				xfree(conf_ptr->control_addr[i]);
			xfree(conf_ptr->control_addr);
		}
		conf_initialized = true;
	}

	return conf_ptr;
}

extern void
slurm_conf_unlock(void)
{
	slurm_mutex_unlock(&conf_lock);
}

/* Normalize supplied debug level to be in range per log.h definitions */
static void _normalize_debug_level(uint16_t *level)
{
	if (*level > LOG_LEVEL_END) {
		error("Normalizing debug level from %u to %d",
		      *level, (LOG_LEVEL_END - 1));
		*level = (LOG_LEVEL_END - 1);
	}
	/* level is uint16, always > LOG_LEVEL_QUIET(0), can't underflow */
}

/* Convert HealthCheckNodeState string to numeric value */
static uint16_t _health_node_state(char *state_str)
{
	uint16_t state_num = 0;
	char *tmp_str = xstrdup(state_str);
	char *token, *last = NULL;
	bool state_set = false;

	token = strtok_r(tmp_str, ",", &last);
	while (token) {
		if (!xstrcasecmp(token, "ANY")) {
			state_num |= HEALTH_CHECK_NODE_ANY;
			state_set = true;
		} else if (!xstrcasecmp(token, "ALLOC")) {
			state_num |= HEALTH_CHECK_NODE_ALLOC;
			state_set = true;
		} else if (!xstrcasecmp(token, "CYCLE")) {
			state_num |= HEALTH_CHECK_CYCLE;
		} else if (!xstrcasecmp(token, "IDLE")) {
			state_num |= HEALTH_CHECK_NODE_IDLE;
			state_set = true;
		} else if (!xstrcasecmp(token, "MIXED")) {
			state_num |= HEALTH_CHECK_NODE_MIXED;
			state_set = true;
		} else {
			error("Invalid HealthCheckNodeState value %s ignored",
			      token);
		}
		token = strtok_r(NULL, ",", &last);
	}
	if (!state_set)
		state_num |= HEALTH_CHECK_NODE_ANY;
	xfree(tmp_str);

	return state_num;
}

/*
 *
 * IN/OUT ctl_conf_ptr - a configuration as loaded by read_slurm_conf_ctl
 *
 * NOTE: a backup_controller or control_machine of "localhost" are over-written
 *	with this machine's name.
 * NOTE: if control_addr is NULL, it is over-written by control_machine
 */
static int
_validate_and_set_defaults(slurm_ctl_conf_t *conf, s_p_hashtbl_t *hashtbl)
{
	char *temp_str = NULL;
	long long_suspend_time;
	bool truth;
	char *default_storage_type = NULL, *default_storage_host = NULL;
	char *default_storage_user = NULL, *default_storage_pass = NULL;
	char *default_storage_loc = NULL;
	uint32_t default_storage_port = 0;
	uint16_t uint16_tmp;
	uint64_t def_cpu_per_gpu = 0, def_mem_per_gpu = 0, tot_prio_weight;
	job_defaults_t *job_defaults;
	int i;

	if (!s_p_get_uint16(&conf->batch_start_timeout, "BatchStartTimeout",
			    hashtbl))
		conf->batch_start_timeout = DEFAULT_BATCH_START_TIMEOUT;

	s_p_get_string(&conf->cluster_name, "ClusterName", hashtbl);
	/*
	 * Some databases are case sensitive so we have to make sure
	 * the cluster name is lower case since sacctmgr makes sure
	 * this is the case as well.
	 */
	if (conf->cluster_name && *conf->cluster_name) {
		for (i = 0; conf->cluster_name[i] != '\0'; i++)
			conf->cluster_name[i] =
				(char)tolower((int)conf->cluster_name[i]);
	} else {
		error("ClusterName needs to be specified");
		return SLURM_ERROR;
	}

	if (!s_p_get_uint16(&conf->complete_wait, "CompleteWait", hashtbl))
		conf->complete_wait = DEFAULT_COMPLETE_WAIT;

	if (_load_slurmctld_host(conf))
		return SLURM_ERROR;

	if (!s_p_get_string(&conf->acct_gather_energy_type,
			    "AcctGatherEnergyType", hashtbl))
		conf->acct_gather_energy_type =
			xstrdup(DEFAULT_ACCT_GATHER_ENERGY_TYPE);

	if (!s_p_get_string(&conf->acct_gather_profile_type,
			    "AcctGatherProfileType", hashtbl))
		conf->acct_gather_profile_type =
			xstrdup(DEFAULT_ACCT_GATHER_PROFILE_TYPE);

	if (!s_p_get_string(&conf->acct_gather_interconnect_type,
			    "AcctGatherInterconnectType", hashtbl) &&
	    !s_p_get_string(&conf->acct_gather_interconnect_type,
			    "AcctGatherInfinibandType", hashtbl))
		conf->acct_gather_interconnect_type =
			xstrdup(DEFAULT_ACCT_GATHER_INTERCONNECT_TYPE);
	else
		xstrsubstituteall(conf->acct_gather_interconnect_type,
				  "infiniband", "interconnect");

	if (!s_p_get_string(&conf->acct_gather_filesystem_type,
			   "AcctGatherFilesystemType", hashtbl))
		conf->acct_gather_filesystem_type =
			xstrdup(DEFAULT_ACCT_GATHER_FILESYSTEM_TYPE);

	if (!s_p_get_uint16(&conf->acct_gather_node_freq,
			    "AcctGatherNodeFreq", hashtbl))
		conf->acct_gather_node_freq = 0;

	if (s_p_get_boolean(&truth, "AllowSpecResourcesUsage", hashtbl))
		conf->use_spec_resources = truth;
	else
		conf->use_spec_resources = DEFAULT_ALLOW_SPEC_RESOURCE_USAGE;

	(void) s_p_get_string(&default_storage_type, "DefaultStorageType",
			      hashtbl);
	(void) s_p_get_string(&default_storage_host, "DefaultStorageHost",
			      hashtbl);
	(void) s_p_get_string(&default_storage_user, "DefaultStorageUser",
			      hashtbl);
	(void) s_p_get_string(&default_storage_pass, "DefaultStoragePass",
			      hashtbl);
	(void) s_p_get_string(&default_storage_loc,  "DefaultStorageLoc",
			      hashtbl);
	(void) s_p_get_uint32(&default_storage_port, "DefaultStoragePort",
			      hashtbl);
	(void) s_p_get_string(&conf->job_credential_private_key,
			     "JobCredentialPrivateKey", hashtbl);
	(void) s_p_get_string(&conf->job_credential_public_certificate,
			      "JobCredentialPublicCertificate", hashtbl);

	(void) s_p_get_string(&conf->authinfo, "AuthInfo", hashtbl);

	if (!s_p_get_string(&conf->authtype, "AuthType", hashtbl))
		conf->authtype = xstrdup(DEFAULT_AUTH_TYPE);

	(void) s_p_get_string(&conf->bb_type, "BurstBufferType", hashtbl);

	if (s_p_get_uint16(&uint16_tmp, "CacheGroups", hashtbl))
		debug("Ignoring obsolete CacheGroups option.");

	(void) s_p_get_string(&conf->comm_params, "CommunicationParameters",
			      hashtbl);

	if (!s_p_get_string(&conf->core_spec_plugin, "CoreSpecPlugin",
	    hashtbl)) {
		conf->core_spec_plugin =
			xstrdup(DEFAULT_CORE_SPEC_PLUGIN);
	}

	if (!s_p_get_string(&conf->checkpoint_type, "CheckpointType", hashtbl))
		conf->checkpoint_type = xstrdup(DEFAULT_CHECKPOINT_TYPE);

	if (s_p_get_string(&temp_str, "CpuFreqDef", hashtbl)) {
		if (cpu_freq_verify_def(temp_str, &conf->cpu_freq_def)) {
			error("Ignoring invalid CpuFreqDef: %s", temp_str);
			conf->cpu_freq_def = NO_VAL;
		}
		xfree(temp_str);
	} else {
		conf->cpu_freq_def = NO_VAL;
	}

	if (s_p_get_string(&temp_str, "CpuFreqGovernors", hashtbl)) {
		if (cpu_freq_verify_govlist(temp_str, &conf->cpu_freq_govs)) {
			error("Ignoring invalid CpuFreqGovernors: %s",
				temp_str);
			conf->cpu_freq_govs = CPU_FREQ_ONDEMAND    |
					      CPU_FREQ_PERFORMANCE |
					      CPU_FREQ_USERSPACE;
		}
		xfree(temp_str);
	} else {
		conf->cpu_freq_govs = CPU_FREQ_ONDEMAND | CPU_FREQ_PERFORMANCE |
				      CPU_FREQ_USERSPACE;
	}

	if (!s_p_get_string(&conf->crypto_type, "CryptoType", hashtbl))
		 conf->crypto_type = xstrdup(DEFAULT_CRYPTO_TYPE);
	if ((xstrcmp(conf->crypto_type, "crypto/openssl") == 0) &&
	    ((conf->job_credential_private_key == NULL) ||
	     (conf->job_credential_public_certificate == NULL))) {
		error("CryptoType=crypto/openssl requires that both "
		      "JobCredentialPrivateKey and "
		      "JobCredentialPublicCertificate be set");
		return SLURM_ERROR;
	}

	if (s_p_get_uint64(&conf->def_mem_per_cpu, "DefMemPerCPU", hashtbl))
		conf->def_mem_per_cpu |= MEM_PER_CPU;
	else if (!s_p_get_uint64(&conf->def_mem_per_cpu, "DefMemPerNode",
				 hashtbl))
		conf->def_mem_per_cpu = DEFAULT_MEM_PER_CPU;

	if (s_p_get_uint64(&def_cpu_per_gpu, "DefCPUPerGPU", hashtbl)) {
		job_defaults = xmalloc(sizeof(job_defaults_t));
		job_defaults->type  = JOB_DEF_CPU_PER_GPU;
		job_defaults->value = def_cpu_per_gpu;
		if (!conf->job_defaults_list) {
			conf->job_defaults_list =
				list_create(job_defaults_free);
		}
		list_append(conf->job_defaults_list, job_defaults);
	}

	if (s_p_get_uint64(&def_mem_per_gpu, "DefMemPerGPU", hashtbl)) {
		job_defaults = xmalloc(sizeof(job_defaults_t));
		job_defaults->type  = JOB_DEF_MEM_PER_GPU;
		job_defaults->value = def_mem_per_gpu;
		if (!conf->job_defaults_list) {
			conf->job_defaults_list =
				list_create(job_defaults_free);
		}
		list_append(conf->job_defaults_list, job_defaults);
	}

	if (s_p_get_string(&temp_str, "DebugFlags", hashtbl)) {
		if (debug_str2flags(temp_str, &conf->debug_flags)
		    != SLURM_SUCCESS) {
			error("DebugFlags invalid: %s", temp_str);
			return SLURM_ERROR;
		}
		xfree(temp_str);
	} else	/* Default: no DebugFlags */
		conf->debug_flags = 0;

	if (!s_p_get_boolean((bool *) &conf->disable_root_jobs,
			     "DisableRootJobs", hashtbl))
		conf->disable_root_jobs = DEFAULT_DISABLE_ROOT_JOBS;

	if (s_p_get_string(&temp_str,
			   "EnforcePartLimits", hashtbl)) {
		uint16_t enforce_param;
		if (parse_part_enforce_type(temp_str, &enforce_param) < 0) {
			error("Bad EnforcePartLimits: %s", temp_str);
			xfree(temp_str);
			return SLURM_ERROR;
		}
		xfree(temp_str);
		conf->enforce_part_limits = enforce_param;
	} else {
		conf->enforce_part_limits = DEFAULT_ENFORCE_PART_LIMITS;
	}

	(void) s_p_get_string(&conf->epilog, "Epilog", hashtbl);

	if (!s_p_get_uint32(&conf->epilog_msg_time, "EpilogMsgTime", hashtbl))
		conf->epilog_msg_time = DEFAULT_EPILOG_MSG_TIME;

	(void) s_p_get_string(&conf->epilog_slurmctld, "EpilogSlurmctld",
			      hashtbl);

	if (!s_p_get_string(&conf->ext_sensors_type,
			    "ExtSensorsType", hashtbl))
		conf->ext_sensors_type =
			xstrdup(DEFAULT_EXT_SENSORS_TYPE);

	if (!s_p_get_uint16(&conf->ext_sensors_freq,
			    "ExtSensorsFreq", hashtbl))
		conf->ext_sensors_freq = 0;

	if (!s_p_get_uint16(&conf->fs_dampening_factor,
			    "FairShareDampeningFactor", hashtbl))
		conf->fs_dampening_factor = 1;

	if (!s_p_get_uint16(&conf->fast_schedule, "FastSchedule", hashtbl))
		conf->fast_schedule = DEFAULT_FAST_SCHEDULE;

	(void) s_p_get_string(&conf->fed_params, "FederationParameters",
			      hashtbl);

	if (!s_p_get_uint32(&conf->first_job_id, "FirstJobId", hashtbl))
		conf->first_job_id = DEFAULT_FIRST_JOB_ID;

	(void) s_p_get_string(&conf->gres_plugins, "GresTypes", hashtbl);

	if (!s_p_get_uint16(&conf->group_force, "GroupUpdateForce", hashtbl))
		conf->group_force = DEFAULT_GROUP_FORCE;

	if (!s_p_get_uint16(&conf->group_time, "GroupUpdateTime", hashtbl))
		conf->group_time = DEFAULT_GROUP_TIME;

	if (!s_p_get_uint16(&conf->inactive_limit, "InactiveLimit", hashtbl))
		conf->inactive_limit = DEFAULT_INACTIVE_LIMIT;

	if (!s_p_get_string(&conf->job_acct_gather_freq,
			    "JobAcctGatherFrequency", hashtbl))
		conf->job_acct_gather_freq =
			xstrdup(DEFAULT_JOB_ACCT_GATHER_FREQ);

	if (!s_p_get_string(&conf->job_acct_gather_type,
			   "JobAcctGatherType", hashtbl))
		conf->job_acct_gather_type =
			xstrdup(DEFAULT_JOB_ACCT_GATHER_TYPE);

	(void) s_p_get_string(&conf->job_acct_gather_params,
			     "JobAcctGatherParams", hashtbl);

	if (!s_p_get_string(&conf->job_ckpt_dir, "JobCheckpointDir", hashtbl))
		conf->job_ckpt_dir = xstrdup(DEFAULT_JOB_CKPT_DIR);

	if (!s_p_get_string(&conf->job_comp_type, "JobCompType", hashtbl)) {
		if (default_storage_type) {
			if (!xstrcasecmp("slurmdbd", default_storage_type)) {
				error("Can not use the default storage type "
				      "specified for jobcomp since there is "
				      "not slurmdbd type.  We are using %s "
				      "as the type. To disable this message "
				      "set JobCompType in your slurm.conf",
				      DEFAULT_JOB_COMP_TYPE);
				conf->job_comp_type =
					xstrdup(DEFAULT_JOB_COMP_TYPE);
			} else
				conf->job_comp_type =
					xstrdup_printf("jobcomp/%s",
						       default_storage_type);
		} else
			conf->job_comp_type = xstrdup(DEFAULT_JOB_COMP_TYPE);
	}
	if (!s_p_get_string(&conf->job_comp_loc, "JobCompLoc", hashtbl)) {
		if (default_storage_loc)
			conf->job_comp_loc = xstrdup(default_storage_loc);
		else if (!xstrcmp(conf->job_comp_type, "jobcomp/mysql"))
			conf->job_comp_loc = xstrdup(DEFAULT_JOB_COMP_DB);
		else
			conf->job_comp_loc = xstrdup(DEFAULT_JOB_COMP_LOC);
	}

	if (!s_p_get_string(&conf->job_comp_host, "JobCompHost",
			    hashtbl)) {
		if (default_storage_host)
			conf->job_comp_host = xstrdup(default_storage_host);
		else
			conf->job_comp_host = xstrdup(DEFAULT_STORAGE_HOST);
	}
	if (!s_p_get_string(&conf->job_comp_user, "JobCompUser",
			    hashtbl)) {
		if (default_storage_user)
			conf->job_comp_user = xstrdup(default_storage_user);
		else
			conf->job_comp_user = xstrdup(DEFAULT_STORAGE_USER);
	}
	if (!s_p_get_string(&conf->job_comp_pass, "JobCompPass",
			    hashtbl)) {
		if (default_storage_pass)
			conf->job_comp_pass = xstrdup(default_storage_pass);
	}
	if (!s_p_get_uint32(&conf->job_comp_port, "JobCompPort",
			    hashtbl)) {
		if (default_storage_port)
			conf->job_comp_port = default_storage_port;
		else if (!xstrcmp(conf->job_comp_type, "job_comp/mysql"))
			conf->job_comp_port = DEFAULT_MYSQL_PORT;
		else
			conf->job_comp_port = DEFAULT_STORAGE_PORT;
	}


	if (!s_p_get_string(&conf->job_container_plugin, "JobContainerType",
	    hashtbl)) {
		conf->job_container_plugin =
			xstrdup(DEFAULT_JOB_CONTAINER_PLUGIN);
	}

	if (!s_p_get_uint16(&conf->job_file_append, "JobFileAppend", hashtbl))
		conf->job_file_append = 0;

	if (!s_p_get_uint16(&conf->job_requeue, "JobRequeue", hashtbl))
		conf->job_requeue = 1;
	else if (conf->job_requeue > 1)
		conf->job_requeue = 1;

	(void) s_p_get_string(&conf->job_submit_plugins, "JobSubmitPlugins",
			      hashtbl);

	if (!s_p_get_uint16(&conf->get_env_timeout, "GetEnvTimeout", hashtbl))
		conf->get_env_timeout = DEFAULT_GET_ENV_TIMEOUT;

	(void) s_p_get_uint16(&conf->health_check_interval,
			      "HealthCheckInterval", hashtbl);
	if (s_p_get_string(&temp_str, "HealthCheckNodeState", hashtbl)) {
		conf->health_check_node_state = _health_node_state(temp_str);
		xfree(temp_str);
	} else
		conf->health_check_node_state = HEALTH_CHECK_NODE_ANY;

	(void) s_p_get_string(&conf->health_check_program, "HealthCheckProgram",
			      hashtbl);

	if (!s_p_get_uint16(&conf->keep_alive_time, "KeepAliveTime", hashtbl))
		conf->keep_alive_time = DEFAULT_KEEP_ALIVE_TIME;

	if (!s_p_get_uint16(&conf->kill_on_bad_exit, "KillOnBadExit", hashtbl))
		conf->kill_on_bad_exit = DEFAULT_KILL_ON_BAD_EXIT;

	if (!s_p_get_uint16(&conf->kill_wait, "KillWait", hashtbl))
		conf->kill_wait = DEFAULT_KILL_WAIT;

	(void) s_p_get_string(&conf->launch_params, "LaunchParameters",
			      hashtbl);

	if (!s_p_get_string(&conf->launch_type, "LaunchType", hashtbl))
		conf->launch_type = xstrdup(DEFAULT_LAUNCH_TYPE);

	(void) s_p_get_string(&conf->licenses, "Licenses", hashtbl);

	if (s_p_get_string(&temp_str, "LogTimeFormat", hashtbl)) {
		/*
		 * If adding to this please update src/api/config_log.c to do
		 * the reverse translation.
		 */
		if (xstrcasestr(temp_str, "iso8601_ms"))
			conf->log_fmt = LOG_FMT_ISO8601_MS;
		else if (xstrcasestr(temp_str, "iso8601"))
			conf->log_fmt = LOG_FMT_ISO8601;
		else if (xstrcasestr(temp_str, "rfc5424_ms"))
			conf->log_fmt = LOG_FMT_RFC5424_MS;
		else if (xstrcasestr(temp_str, "rfc5424"))
			conf->log_fmt = LOG_FMT_RFC5424;
		else if (xstrcasestr(temp_str, "clock"))
			conf->log_fmt = LOG_FMT_CLOCK;
		else if (xstrcasestr(temp_str, "short"))
			conf->log_fmt = LOG_FMT_SHORT;
		else if (xstrcasestr(temp_str, "thread_id"))
			conf->log_fmt = LOG_FMT_THREAD_ID;
		xfree(temp_str);
	} else
		conf->log_fmt = LOG_FMT_ISO8601_MS;

	(void) s_p_get_string(&conf->mail_domain, "MailDomain", hashtbl);

	if (!s_p_get_string(&conf->mail_prog, "MailProg", hashtbl)) {
		struct stat stat_buf;
		if ((stat(DEFAULT_MAIL_PROG,     &stat_buf) == 0) ||
		    (stat(DEFAULT_MAIL_PROG_ALT, &stat_buf) != 0))
			conf->mail_prog = xstrdup(DEFAULT_MAIL_PROG);
		else
			conf->mail_prog = xstrdup(DEFAULT_MAIL_PROG_ALT);
	}

	if (!s_p_get_uint32(&conf->max_array_sz, "MaxArraySize", hashtbl))
		conf->max_array_sz = DEFAULT_MAX_ARRAY_SIZE;
	else if (conf->max_array_sz > 4000001) {
		error("MaxArraySize value (%u) is greater than 4000001",
		      conf->max_array_sz);
	}

	if (!s_p_get_uint32(&conf->max_job_cnt, "MaxJobCount", hashtbl))
		conf->max_job_cnt = DEFAULT_MAX_JOB_COUNT;
	else if (conf->max_job_cnt < 1) {
		error("MaxJobCount=%u, No jobs permitted", conf->max_job_cnt);
		return SLURM_ERROR;
	}

	if (!s_p_get_uint32(&conf->max_job_id, "MaxJobId", hashtbl))
		conf->max_job_id = DEFAULT_MAX_JOB_ID;
	if (conf->max_job_id > MAX_JOB_ID) {
		error("MaxJobId can not exceed MAX_JOB_ID, resetting value");
		conf->max_job_id = MAX_JOB_ID;
	}

	if (conf->first_job_id > conf->max_job_id) {
		error("FirstJobId > MaxJobId");
		return SLURM_ERROR;
	} else {
		uint32_t tmp32 = conf->max_job_id - conf->first_job_id + 1;
		if (conf->max_job_cnt > tmp32) {
			/* Needed for job array support */
			info("Resetting MaxJobCnt from %u to %u "
			     "(MaxJobId - FirstJobId + 1)",
			     conf->max_job_cnt, tmp32);
			conf->max_job_cnt = tmp32;
		}
	}

	if (s_p_get_uint64(&conf->max_mem_per_cpu,
			   "MaxMemPerCPU", hashtbl)) {
		conf->max_mem_per_cpu |= MEM_PER_CPU;
	} else if (!s_p_get_uint64(&conf->max_mem_per_cpu,
				 "MaxMemPerNode", hashtbl)) {
		conf->max_mem_per_cpu = DEFAULT_MAX_MEM_PER_CPU;
	}

	if (!s_p_get_uint32(&conf->max_step_cnt, "MaxStepCount", hashtbl))
		conf->max_step_cnt = DEFAULT_MAX_STEP_COUNT;
	else if (conf->max_step_cnt < 1) {
		error("MaxStepCount=%u, No steps permitted",
		      conf->max_step_cnt);
		return SLURM_ERROR;
	}

	if (!s_p_get_uint16(&conf->max_tasks_per_node, "MaxTasksPerNode",
			    hashtbl)) {
		conf->max_tasks_per_node = DEFAULT_MAX_TASKS_PER_NODE;
	}

	(void) s_p_get_string(&conf->mcs_plugin_params, "MCSParameters",
			      hashtbl);
	if (!s_p_get_string(&conf->mcs_plugin, "MCSPlugin", hashtbl)) {
		conf->mcs_plugin = xstrdup(DEFAULT_MCS_PLUGIN);
		if (conf->mcs_plugin_params) {
			/* no plugin mcs and a mcs plugin param */
			error("MCSParameters=%s used and no MCSPlugin",
				conf->mcs_plugin_params);
			return SLURM_ERROR;
		}
	}
	if (conf->mcs_plugin_params &&
	    !xstrcmp(conf->mcs_plugin, "mcs/none")) {
		/* plugin mcs none and a mcs plugin param */
		info("WARNING: MCSParameters=%s can't be used with"
			"MCSPlugin=mcs/none",
			conf->mcs_plugin_params);
	}
	if (!conf->mcs_plugin_params &&
	    !xstrcmp(conf->mcs_plugin, "mcs/group")) {
		/* plugin mcs/group and no mcs plugin param */
		 error("MCSPlugin is mcs/group and no MCSParameters");
		 return SLURM_ERROR;
	}

	if (!s_p_get_uint16(&conf->msg_timeout, "MessageTimeout", hashtbl))
		conf->msg_timeout = DEFAULT_MSG_TIMEOUT;
	else if (conf->msg_timeout > 100) {
		if (getuid() == 0) {
			info("WARNING: MessageTimeout is too high for "
				"effective fault-tolerance");
		} else {
			debug("WARNING: MessageTimeout is too high for "
				"effective fault-tolerance");
		}
	}

	if (!s_p_get_uint32(&conf->min_job_age, "MinJobAge", hashtbl))
		conf->min_job_age = DEFAULT_MIN_JOB_AGE;
	else if (conf->min_job_age < 2) {
		if (getuid() == 0)
			info("WARNING: MinJobAge must be at least 2");
		else
			debug("WARNING: MinJobAge must be at least 2");
		conf->min_job_age = 2;
	}

	if (!s_p_get_string(&conf->mpi_default, "MpiDefault", hashtbl))
		conf->mpi_default = xstrdup(DEFAULT_MPI_DEFAULT);

	(void) s_p_get_string(&conf->mpi_params, "MpiParams", hashtbl);
#if defined(HAVE_NATIVE_CRAY)
	if (conf->mpi_params == NULL ||
	    strstr(conf->mpi_params, "ports=") == NULL) {
		error("MpiParams=ports= is required on native Cray systems");
		return SLURM_ERROR;
	}
#endif

	(void) s_p_get_string(&conf->msg_aggr_params, "MsgAggregationParams",
			      hashtbl);

	if (!s_p_get_boolean((bool *)&conf->track_wckey,
			    "TrackWCKey", hashtbl))
		conf->track_wckey = false;

	if (!s_p_get_string(&conf->accounting_storage_type,
			    "AccountingStorageType", hashtbl)) {
		if (default_storage_type)
			conf->accounting_storage_type =
				xstrdup_printf("accounting_storage/%s",
					       default_storage_type);
		else
			conf->accounting_storage_type =
				xstrdup(DEFAULT_ACCOUNTING_STORAGE_TYPE);
	} else {
		if (xstrcasestr(conf->accounting_storage_type, "mysql"))
			fatal("AccountingStorageType=accounting_storage/mysql "
			      "only permitted in SlurmDBD.");
	}

	(void) s_p_get_string(&conf->node_features_plugins,
			     "NodeFeaturesPlugins", hashtbl);

	if (!s_p_get_string(&conf->accounting_storage_tres,
			    "AccountingStorageTRES", hashtbl))
		conf->accounting_storage_tres =
			xstrdup(DEFAULT_ACCOUNTING_TRES);
	else
		xstrfmtcat(conf->accounting_storage_tres,
			   ",%s", DEFAULT_ACCOUNTING_TRES);

	if (s_p_get_string(&temp_str, "AccountingStorageEnforce", hashtbl)) {
		if (xstrcasestr(temp_str, "1")
		    || xstrcasestr(temp_str, "associations"))
			conf->accounting_storage_enforce
				|= ACCOUNTING_ENFORCE_ASSOCS;

		if (xstrcasestr(temp_str, "2")
		    || xstrcasestr(temp_str, "limits")) {
			conf->accounting_storage_enforce
				|= ACCOUNTING_ENFORCE_ASSOCS;
			conf->accounting_storage_enforce
				|= ACCOUNTING_ENFORCE_LIMITS;
		}

		if (xstrcasestr(temp_str, "safe")) {
			conf->accounting_storage_enforce
				|= ACCOUNTING_ENFORCE_ASSOCS;
			conf->accounting_storage_enforce
				|= ACCOUNTING_ENFORCE_LIMITS;
			conf->accounting_storage_enforce
				|= ACCOUNTING_ENFORCE_SAFE;
		}

		if (xstrcasestr(temp_str, "wckeys")) {
			conf->accounting_storage_enforce
				|= ACCOUNTING_ENFORCE_ASSOCS;
			conf->accounting_storage_enforce
				|= ACCOUNTING_ENFORCE_WCKEYS;
			conf->track_wckey = true;
		}

		if (xstrcasestr(temp_str, "qos")) {
			conf->accounting_storage_enforce
				|= ACCOUNTING_ENFORCE_ASSOCS;
			conf->accounting_storage_enforce
				|= ACCOUNTING_ENFORCE_QOS;
		}

		if (xstrcasestr(temp_str, "all")) {
			conf->accounting_storage_enforce = 0xffff;
			conf->track_wckey = true;
			/* If all is used, nojobs and nosteps aren't
			   part of it.  They must be requested as well.
			*/
			conf->accounting_storage_enforce
				&= (~ACCOUNTING_ENFORCE_NO_JOBS);
			conf->accounting_storage_enforce
				&= (~ACCOUNTING_ENFORCE_NO_STEPS);
		}

		/* Everything that "all" doesn't mean should be put here */
		if (xstrcasestr(temp_str, "nojobs")) {
			conf->accounting_storage_enforce
				|= ACCOUNTING_ENFORCE_NO_JOBS;
			conf->accounting_storage_enforce
				|= ACCOUNTING_ENFORCE_NO_STEPS;
		}

		if (xstrcasestr(temp_str, "nosteps")) {
			conf->accounting_storage_enforce
				|= ACCOUNTING_ENFORCE_NO_STEPS;
		}

		xfree(temp_str);
	} else
		conf->accounting_storage_enforce = 0;

	/* if no backup we don't care */
	(void) s_p_get_string(&conf->accounting_storage_backup_host,
			      "AccountingStorageBackupHost", hashtbl);

	if (!s_p_get_string(&conf->accounting_storage_host,
			    "AccountingStorageHost", hashtbl)) {
		if (default_storage_host)
			conf->accounting_storage_host =
				xstrdup(default_storage_host);
		else
			conf->accounting_storage_host =
				xstrdup(DEFAULT_STORAGE_HOST);
	}

	if (!s_p_get_string(&conf->accounting_storage_loc,
			    "AccountingStorageLoc", hashtbl)) {
		if (default_storage_loc)
			conf->accounting_storage_loc =
				xstrdup(default_storage_loc);
		else if (!xstrcmp(conf->accounting_storage_type,
				 "accounting_storage/mysql"))
			conf->accounting_storage_loc =
				xstrdup(DEFAULT_ACCOUNTING_DB);
		else
			conf->accounting_storage_loc =
				xstrdup(DEFAULT_STORAGE_LOC);
	}
	if (!s_p_get_string(&conf->accounting_storage_user,
			    "AccountingStorageUser", hashtbl)) {
		if (default_storage_user)
			conf->accounting_storage_user =
				xstrdup(default_storage_user);
		else
			conf->accounting_storage_user =
				xstrdup(DEFAULT_STORAGE_USER);
	}
	if (!s_p_get_string(&conf->accounting_storage_pass,
			    "AccountingStoragePass", hashtbl)) {
		if (default_storage_pass)
			conf->accounting_storage_pass =
				xstrdup(default_storage_pass);
	}
	if (s_p_get_boolean(&truth, "AccountingStoreJobComment", hashtbl)
	    && !truth)
		conf->acctng_store_job_comment = 0;
	else
		conf->acctng_store_job_comment = 1;

	if (!s_p_get_uint32(&conf->accounting_storage_port,
			    "AccountingStoragePort", hashtbl)) {
		if (default_storage_port)
			conf->accounting_storage_port = default_storage_port;
		else if (!xstrcmp(conf->accounting_storage_type,
				"accounting_storage/slurmdbd"))
			conf->accounting_storage_port = SLURMDBD_PORT;
		else if (!xstrcmp(conf->accounting_storage_type,
			  "accounting_storage/mysql"))
			conf->accounting_storage_port = DEFAULT_MYSQL_PORT;
		else
			conf->accounting_storage_port = DEFAULT_STORAGE_PORT;
	}

	/* remove the user and loc if using slurmdbd */
	if (!xstrcmp(conf->accounting_storage_type,
		   "accounting_storage/slurmdbd")) {
		xfree(conf->accounting_storage_loc);
		conf->accounting_storage_loc = xstrdup("N/A");
		xfree(conf->accounting_storage_user);
		conf->accounting_storage_user = xstrdup("N/A");
	}

	(void) s_p_get_uint16(&conf->over_time_limit, "OverTimeLimit", hashtbl);

	if (!s_p_get_string(&conf->plugindir, "PluginDir", hashtbl))
		conf->plugindir = xstrdup(default_plugin_path);
	if (!_is_valid_path(conf->plugindir, "PluginDir")) {
		error("Bad value \"%s\" for PluginDir", conf->plugindir);
		return SLURM_ERROR;
	}

	if (!s_p_get_string(&conf->plugstack, "PlugStackConfig", hashtbl))
		conf->plugstack = xstrdup(default_plugstack);

	(void) s_p_get_string(&conf->power_parameters, "PowerParameters",
			      hashtbl);
	if (!s_p_get_string(&conf->power_plugin, "PowerPlugin", hashtbl))
		conf->power_plugin = xstrdup(DEFAULT_POWER_PLUGIN);

	if (s_p_get_string(&temp_str, "PreemptMode", hashtbl)) {
		conf->preempt_mode = preempt_mode_num(temp_str);
		if (conf->preempt_mode == NO_VAL16) {
			error("PreemptMode=%s invalid", temp_str);
			return SLURM_ERROR;
		}
		if (conf->preempt_mode == PREEMPT_MODE_SUSPEND) {
			error("PreemptMode=SUSPEND requires GANG too");
			return SLURM_ERROR;
		}
		xfree(temp_str);
	} else {
		conf->preempt_mode = PREEMPT_MODE_OFF;
	}
	if (!s_p_get_string(&conf->preempt_type, "PreemptType", hashtbl))
		conf->preempt_type = xstrdup(DEFAULT_PREEMPT_TYPE);
	if (xstrcmp(conf->preempt_type, "preempt/qos") == 0) {
		int preempt_mode = conf->preempt_mode & (~PREEMPT_MODE_GANG);
		if (preempt_mode == PREEMPT_MODE_OFF) {
			error("PreemptType and PreemptMode values "
			      "incompatible");
			return SLURM_ERROR;
		}
	} else if (xstrcmp(conf->preempt_type, "preempt/partition_prio") == 0) {
		int preempt_mode = conf->preempt_mode & (~PREEMPT_MODE_GANG);
		if (preempt_mode == PREEMPT_MODE_OFF) {
			error("PreemptType and PreemptMode values "
			      "incompatible");
			return SLURM_ERROR;
		}
	} else if (xstrcmp(conf->preempt_type, "preempt/none") == 0) {
		int preempt_mode = conf->preempt_mode & (~PREEMPT_MODE_GANG);
		if (preempt_mode != PREEMPT_MODE_OFF) {
			error("PreemptType and PreemptMode values "
			      "incompatible");
			return SLURM_ERROR;
		}
	}

	if (s_p_get_string(&temp_str, "PriorityDecayHalfLife", hashtbl)) {
		int max_time = time_str2mins(temp_str);
		if ((max_time < 0) && (max_time != INFINITE)) {
			error("Bad value \"%s\" for PriorityDecayHalfLife",
			      temp_str);
			return SLURM_ERROR;
		}
		conf->priority_decay_hl = max_time * 60;
		xfree(temp_str);
	} else
		conf->priority_decay_hl = DEFAULT_PRIORITY_DECAY;

	if (s_p_get_string(&temp_str, "PriorityCalcPeriod", hashtbl)) {
		int calc_period = time_str2mins(temp_str);
		if (calc_period < 1) {
			error("Bad value \"%s\" for PriorityCalcPeriod",
			      temp_str);
			return SLURM_ERROR;
		}
		conf->priority_calc_period = calc_period * 60;
		xfree(temp_str);
	} else
		conf->priority_calc_period = DEFAULT_PRIORITY_CALC_PERIOD;

	if (s_p_get_boolean(&truth, "PriorityFavorSmall", hashtbl) && truth)
		conf->priority_favor_small = 1;
	else
		conf->priority_favor_small = 0;

	conf->priority_flags = 0;
	if (s_p_get_string(&temp_str, "PriorityFlags", hashtbl)) {
		if (xstrcasestr(temp_str, "ACCRUE_ALWAYS"))
			conf->priority_flags |= PRIORITY_FLAGS_ACCRUE_ALWAYS;
		if (xstrcasestr(temp_str, "SMALL_RELATIVE_TO_TIME"))
			conf->priority_flags |= PRIORITY_FLAGS_SIZE_RELATIVE;
		if (xstrcasestr(temp_str, "CALCULATE_RUNNING"))
			conf->priority_flags |= PRIORITY_FLAGS_CALCULATE_RUNNING;

		if (xstrcasestr(temp_str, "DEPTH_OBLIVIOUS"))
			conf->priority_flags |= PRIORITY_FLAGS_DEPTH_OBLIVIOUS;
		else if (xstrcasestr(temp_str, "FAIR_TREE"))
			conf->priority_flags |= PRIORITY_FLAGS_FAIR_TREE;

		if (xstrcasestr(temp_str, "INCR_ONLY"))
			conf->priority_flags |= PRIORITY_FLAGS_INCR_ONLY;

		if (xstrcasestr(temp_str, "MAX_TRES"))
			conf->priority_flags |= PRIORITY_FLAGS_MAX_TRES;

		xfree(temp_str);
	}

	if (s_p_get_string(&temp_str, "PriorityMaxAge", hashtbl)) {
		int max_time = time_str2mins(temp_str);
		if ((max_time < 0) && (max_time != INFINITE)) {
			error("Bad value \"%s\" for PriorityMaxAge",
			      temp_str);
			return SLURM_ERROR;
		}
		conf->priority_max_age = max_time * 60;
		xfree(temp_str);
	} else
		conf->priority_max_age = DEFAULT_PRIORITY_DECAY;

	(void) s_p_get_string(&conf->priority_params, "PriorityParameters",
			      hashtbl);


	if (s_p_get_string(&temp_str, "PriorityUsageResetPeriod", hashtbl)) {
		if (xstrcasecmp(temp_str, "none") == 0)
			conf->priority_reset_period = PRIORITY_RESET_NONE;
		else if (xstrcasecmp(temp_str, "now") == 0)
			conf->priority_reset_period = PRIORITY_RESET_NOW;
		else if (xstrcasecmp(temp_str, "daily") == 0)
			conf->priority_reset_period = PRIORITY_RESET_DAILY;
		else if (xstrcasecmp(temp_str, "weekly") == 0)
			conf->priority_reset_period = PRIORITY_RESET_WEEKLY;
		else if (xstrcasecmp(temp_str, "monthly") == 0)
			conf->priority_reset_period = PRIORITY_RESET_MONTHLY;
		else if (xstrcasecmp(temp_str, "quarterly") == 0)
			conf->priority_reset_period = PRIORITY_RESET_QUARTERLY;
		else if (xstrcasecmp(temp_str, "yearly") == 0)
			conf->priority_reset_period = PRIORITY_RESET_YEARLY;
		else {
			error("Bad value \"%s\" for PriorityUsageResetPeriod",
			      temp_str);
			return SLURM_ERROR;
		}
		xfree(temp_str);
	} else {
		conf->priority_reset_period = PRIORITY_RESET_NONE;
		if (!conf->priority_decay_hl) {
			error("You have to either have "
			      "PriorityDecayHalfLife != 0 or "
			      "PriorityUsageResetPeriod set to something "
			      "or the priority plugin will result in "
			      "rolling over.");
			return SLURM_ERROR;
		}
	}

	if (!s_p_get_string(&conf->priority_type, "PriorityType", hashtbl))
		conf->priority_type = xstrdup(DEFAULT_PRIORITY_TYPE);

	if (!s_p_get_uint32(&conf->priority_weight_age,
			    "PriorityWeightAge", hashtbl))
		conf->priority_weight_age = 0;
	if (!s_p_get_uint32(&conf->priority_weight_fs,
			    "PriorityWeightFairshare", hashtbl))
		conf->priority_weight_fs = 0;
	if (!s_p_get_uint32(&conf->priority_weight_js,
			    "PriorityWeightJobSize", hashtbl))
		conf->priority_weight_js = 0;
	if (!s_p_get_uint32(&conf->priority_weight_part,
			    "PriorityWeightPartition", hashtbl))
		conf->priority_weight_part = 0;
	if (!s_p_get_uint32(&conf->priority_weight_qos,
			    "PriorityWeightQOS", hashtbl))
		conf->priority_weight_qos = 0;
	if (!s_p_get_string(&conf->priority_weight_tres, "PriorityWeightTRES",
			    hashtbl))
		conf->priority_weight_tres = NULL;

	/* Check for possible overflow of priority.
	 * We also check when doing the computation for each job. */
	tot_prio_weight = (uint64_t) conf->priority_weight_age   +
		(uint64_t) conf->priority_weight_fs   +
		(uint64_t) conf->priority_weight_js   +
		(uint64_t) conf->priority_weight_part +
		(uint64_t) conf->priority_weight_qos;
	/* TODO include TRES weights */
	if (tot_prio_weight > 0xffffffff) {
		error("PriorityWeight values too high, job priority value may "
		      "overflow");
	}

	/* Out of order due to use with ProctrackType */
	if (!s_p_get_string(&conf->switch_type, "SwitchType", hashtbl))
		conf->switch_type = xstrdup(DEFAULT_SWITCH_TYPE);

	if (!s_p_get_string(&conf->proctrack_type, "ProctrackType", hashtbl)) {
		conf->proctrack_type = xstrdup(DEFAULT_PROCTRACK_TYPE);
	}
#ifdef HAVE_NATIVE_CRAY
	if (xstrcmp(conf->proctrack_type, "proctrack/cray")) {
		error("On a native Cray ProctrackType=proctrack/cray "
		      "is required");
		return SLURM_ERROR;
	}
#else
#ifdef HAVE_REAL_CRAY
	if (xstrcmp(conf->proctrack_type, "proctrack/sgi_job")) {
		error("On Cray ProctrackType=proctrack/sgi_job is required");
		return SLURM_ERROR;
	}
#endif
#endif

	conf->private_data = 0; /* Set to default before parsing PrivateData */
	if (s_p_get_string(&temp_str, "PrivateData", hashtbl)) {
		if (xstrcasestr(temp_str, "account"))
			conf->private_data |= PRIVATE_DATA_ACCOUNTS;
		if (xstrcasestr(temp_str, "cloud"))
			conf->private_data |= PRIVATE_CLOUD_NODES;
		if (xstrcasestr(temp_str, "event"))
			conf->private_data |= PRIVATE_DATA_EVENTS;
		if (xstrcasestr(temp_str, "job"))
			conf->private_data |= PRIVATE_DATA_JOBS;
		if (xstrcasestr(temp_str, "node"))
			conf->private_data |= PRIVATE_DATA_NODES;
		if (xstrcasestr(temp_str, "partition"))
			conf->private_data |= PRIVATE_DATA_PARTITIONS;
		if (xstrcasestr(temp_str, "reservation"))
			conf->private_data |= PRIVATE_DATA_RESERVATIONS;
		if (xstrcasestr(temp_str, "usage"))
			conf->private_data |= PRIVATE_DATA_USAGE;
		if (xstrcasestr(temp_str, "user"))
			conf->private_data |= PRIVATE_DATA_USERS;
		if (xstrcasestr(temp_str, "all"))
			conf->private_data = 0xffff;
		xfree(temp_str);
	}

	(void) s_p_get_string(&conf->prolog, "Prolog", hashtbl);
	(void) s_p_get_string(&conf->prolog_slurmctld, "PrologSlurmctld",
			      hashtbl);

	if (s_p_get_string(&temp_str, "PrologFlags", hashtbl)) {
		conf->prolog_flags = prolog_str2flags(temp_str);
		if (conf->prolog_flags == NO_VAL16) {
			fatal("PrologFlags invalid: %s", temp_str);
		}

		if ((conf->prolog_flags & PROLOG_FLAG_NOHOLD) &&
		    (conf->prolog_flags & PROLOG_FLAG_CONTAIN)) {
			fatal("PrologFlags invalid combination: NoHold cannot be combined with Contain and/or X11");
		}
		if (conf->prolog_flags & PROLOG_FLAG_NOHOLD) {
			conf->prolog_flags |= PROLOG_FLAG_ALLOC;
#ifdef HAVE_ALPS_CRAY
			error("PrologFlags=NoHold is not compatible when "
			      "running on ALPS/Cray systems");
			conf->prolog_flags &= (~PROLOG_FLAG_NOHOLD);
			return SLURM_ERROR;
#endif
		}
		xfree(temp_str);
	} else { /* Default: no Prolog Flags are set */
		conf->prolog_flags = 0;
	}

	if (!s_p_get_uint16(&conf->propagate_prio_process,
			"PropagatePrioProcess", hashtbl)) {
		conf->propagate_prio_process = PROP_PRIO_OFF;
	} else if (conf->propagate_prio_process > PROP_PRIO_NICER) {
		error("Bad PropagatePrioProcess: %u",
			conf->propagate_prio_process);
		return SLURM_ERROR;
	}

	if (s_p_get_string(&conf->propagate_rlimits_except,
			   "PropagateResourceLimitsExcept", hashtbl)) {
		if ((parse_rlimits(conf->propagate_rlimits_except,
				   NO_PROPAGATE_RLIMITS)) < 0) {
			error("Bad PropagateResourceLimitsExcept: %s",
			      conf->propagate_rlimits_except);
			return SLURM_ERROR;
		}
	} else {
		if (!s_p_get_string(&conf->propagate_rlimits,
				    "PropagateResourceLimits", hashtbl))
			conf->propagate_rlimits = xstrdup( "ALL" );
		if ((parse_rlimits(conf->propagate_rlimits,
				   PROPAGATE_RLIMITS )) < 0) {
			error("Bad PropagateResourceLimits: %s",
			      conf->propagate_rlimits);
			return SLURM_ERROR;
		}
	}

	if (s_p_get_string(&temp_str, "ReconfigFlags", hashtbl)) {
		conf->reconfig_flags = reconfig_str2flags(temp_str);
		if (conf->reconfig_flags == 0xffff) {
			error("ReconfigFlags invalid: %s", temp_str);
			return SLURM_ERROR;
		}
		xfree(temp_str);
	} else  /* Default: no ReconfigFlags */
		conf->reconfig_flags = 0;

	if (!s_p_get_uint16(&conf->ret2service, "ReturnToService", hashtbl))
		conf->ret2service = DEFAULT_RETURN_TO_SERVICE;
#ifdef HAVE_ALPS_CRAY
	if (conf->ret2service > 1) {
		error("ReturnToService > 1 is not supported on ALPS Cray");
		return SLURM_ERROR;
	}
#endif

	(void) s_p_get_string(&conf->resv_epilog, "ResvEpilog", hashtbl);
	(void) s_p_get_uint16(&conf->resv_over_run, "ResvOverRun", hashtbl);
	(void)s_p_get_string(&conf->resv_prolog, "ResvProlog", hashtbl);

	(void)s_p_get_string(&conf->resume_fail_program, "ResumeFailProgram",
			     hashtbl);
	(void)s_p_get_string(&conf->resume_program, "ResumeProgram", hashtbl);
	if (!s_p_get_uint16(&conf->resume_rate, "ResumeRate", hashtbl))
		conf->resume_rate = DEFAULT_RESUME_RATE;
	if (!s_p_get_uint16(&conf->resume_timeout, "ResumeTimeout", hashtbl))
		conf->resume_timeout = DEFAULT_RESUME_TIMEOUT;

	(void) s_p_get_string(&conf->reboot_program, "RebootProgram", hashtbl);

	if (!s_p_get_string(&conf->route_plugin, "RoutePlugin", hashtbl))
		conf->route_plugin = xstrdup(DEFAULT_ROUTE_PLUGIN);

	(void) s_p_get_string(&conf->salloc_default_command,
			      "SallocDefaultCommand", hashtbl);
	(void) s_p_get_string(&conf->sbcast_parameters,
			      "SbcastParameters", hashtbl);

	(void) s_p_get_string(&conf->sched_params, "SchedulerParameters",
			      hashtbl);

	if (s_p_get_uint16(&uint16_tmp, "SchedulerPort", hashtbl)) {
		debug("Ignoring obsolete SchedulerPort option.");
	}

	if (s_p_get_uint16(&uint16_tmp, "SchedulerRootFilter", hashtbl)) {
		debug("Ignoring obsolete SchedulerRootFilter option.");
	}

	if (!s_p_get_uint16(&conf->sched_time_slice, "SchedulerTimeSlice",
	    hashtbl))
		conf->sched_time_slice = DEFAULT_SCHED_TIME_SLICE;
	else if (conf->sched_time_slice < 5) {
		error("SchedulerTimeSlice must be at least 5 seconds");
		conf->sched_time_slice = DEFAULT_SCHED_TIME_SLICE;
	}

	if (!s_p_get_string(&conf->schedtype, "SchedulerType", hashtbl))
		conf->schedtype = xstrdup(DEFAULT_SCHEDTYPE);

	if (!s_p_get_string(&conf->select_type, "SelectType", hashtbl))
		conf->select_type = xstrdup(DEFAULT_SELECT_TYPE);

	if (s_p_get_string(&temp_str,
			   "SelectTypeParameters", hashtbl)) {
		uint16_t type_param;
		if ((parse_select_type_param(temp_str, &type_param) < 0)) {
			error("Bad SelectTypeParameter: %s", temp_str);
			xfree(temp_str);
			return SLURM_ERROR;
		}
		conf->select_type_param = type_param;
		xfree(temp_str);
	} else
		conf->select_type_param = 0;

	if (!s_p_get_string( &conf->slurm_user_name, "SlurmUser", hashtbl)) {
		conf->slurm_user_name = xstrdup("root");
		conf->slurm_user_id   = 0;
	} else {
		uid_t my_uid;
		if (uid_from_string (conf->slurm_user_name, &my_uid) < 0) {
			error ("Invalid user for SlurmUser %s, ignored",
			       conf->slurm_user_name);
			xfree(conf->slurm_user_name);
			return SLURM_ERROR;
		} else {
			conf->slurm_user_id = my_uid;
		}
	}
#ifdef HAVE_REAL_CRAY
	/*
	 * This requirement derives from Cray ALPS:
	 * - ALPS reservations can only be created by the job owner or root
	 *   (confirmation may be done by other non-privileged users);
	 * - freeing a reservation always requires root privileges.
	 * Even when running on Native Cray the SlurmUser must be root
	 * to access the needed libraries.
	 */
	if (conf->slurm_user_id != 0) {
		error("Cray requires SlurmUser=root (default), but have '%s'.",
			conf->slurm_user_name);
		return SLURM_ERROR;
	}
#endif

	if (!s_p_get_string( &conf->slurmd_user_name, "SlurmdUser", hashtbl)) {
		conf->slurmd_user_name = xstrdup("root");
		conf->slurmd_user_id   = 0;
	} else {
		uid_t my_uid;
		if (uid_from_string (conf->slurmd_user_name, &my_uid) < 0) {
			error("Invalid user for SlurmdUser %s, ignored",
			       conf->slurmd_user_name);
			xfree(conf->slurmd_user_name);
			return SLURM_ERROR;
		} else {
			conf->slurmd_user_id = my_uid;
		}
	}

	(void) s_p_get_string(&conf->slurmctld_addr, "SlurmctldAddr",
			      hashtbl);

	if (s_p_get_string(&temp_str, "SlurmctldDebug", hashtbl)) {
		conf->slurmctld_debug = log_string2num(temp_str);
		if (conf->slurmctld_debug == NO_VAL16) {
			error("Invalid SlurmctldDebug %s", temp_str);
			return SLURM_ERROR;
		}
		xfree(temp_str);
		_normalize_debug_level(&conf->slurmctld_debug);
	} else
		conf->slurmctld_debug = LOG_LEVEL_INFO;

	if (!s_p_get_string(&conf->slurmctld_pidfile,
			    "SlurmctldPidFile", hashtbl))
		conf->slurmctld_pidfile = xstrdup(DEFAULT_SLURMCTLD_PIDFILE);

	(void) s_p_get_string(&conf->slurmctld_plugstack, "SlurmctldPlugstack",
			      hashtbl);

	(void )s_p_get_string(&conf->slurmctld_logfile, "SlurmctldLogFile",
			      hashtbl);

	if (s_p_get_string(&temp_str, "SlurmctldSyslogDebug", hashtbl)) {
		conf->slurmctld_syslog_debug = log_string2num(temp_str);
		if (conf->slurmctld_syslog_debug == NO_VAL16) {
			error("Invalid SlurmctldSyslogDebug %s", temp_str);
			return SLURM_ERROR;
		}
		xfree(temp_str);
		_normalize_debug_level(&conf->slurmctld_syslog_debug);
	} else
		conf->slurmctld_syslog_debug = LOG_LEVEL_END;

	if (s_p_get_string(&temp_str, "SlurmctldPort", hashtbl)) {
		char *end_ptr = NULL;
		long port_long;
		slurm_seterrno(0);
		port_long = strtol(temp_str, &end_ptr, 10);
		if ((port_long == LONG_MIN) || (port_long == LONG_MAX) ||
		    (port_long <= 0) || errno) {
			error("Invalid SlurmctldPort %s", temp_str);
			return SLURM_ERROR;
		}
		conf->slurmctld_port = port_long;
		if (end_ptr[0] == '-') {
			port_long = strtol(end_ptr+1, NULL, 10);
			if ((port_long == LONG_MIN) ||
			    (port_long == LONG_MAX) ||
			    (port_long <= conf->slurmctld_port) || errno) {
				error("Invalid SlurmctldPort %s", temp_str);
				return SLURM_ERROR;
			}
			conf->slurmctld_port_count = port_long + 1 -
						     conf->slurmctld_port;

			/*
			 * The port count needs to be at most FD_SETSIZE,
			 * otherwise we cannot select() on those high numbered
			 * ports and may miss traffic.
			 */
			if (conf->slurmctld_port_count > FD_SETSIZE) {
				error("SlurmctldPort=%s exceeds FD_SETSIZE=%d,"
				      " truncating to %d-%d", temp_str,
				      FD_SETSIZE, conf->slurmctld_port,
				      (conf->slurmctld_port + FD_SETSIZE - 1));
				conf->slurmctld_port_count = FD_SETSIZE;
			}
		} else if (end_ptr[0] != '\0') {
			error("Invalid SlurmctldPort %s", temp_str);
			return SLURM_ERROR;
		} else {
			conf->slurmctld_port_count = 1;
		}
		xfree(temp_str);
	} else {
		conf->slurmctld_port = SLURMCTLD_PORT;
		conf->slurmctld_port_count = SLURMCTLD_PORT_COUNT;
	}

	(void) s_p_get_string(&conf->slurmctld_primary_off_prog,
			      "SlurmctldPrimaryOffProg", hashtbl);
	(void) s_p_get_string(&conf->slurmctld_primary_on_prog,
			      "SlurmctldPrimaryOnProg", hashtbl);

	if (!s_p_get_uint16(&conf->slurmctld_timeout,
			    "SlurmctldTimeout", hashtbl))
		conf->slurmctld_timeout = DEFAULT_SLURMCTLD_TIMEOUT;

	(void) s_p_get_string(&conf->slurmctld_params,
			      "SlurmctldParameters", hashtbl);

	if (s_p_get_string(&temp_str, "SlurmdDebug", hashtbl)) {
		conf->slurmd_debug = log_string2num(temp_str);
		if (conf->slurmd_debug == NO_VAL16) {
			error("Invalid SlurmdDebug %s", temp_str);
			return SLURM_ERROR;
		}
		xfree(temp_str);
		_normalize_debug_level(&conf->slurmd_debug);
	} else
		conf->slurmd_debug = LOG_LEVEL_INFO;

	(void) s_p_get_string(&conf->slurmd_logfile, "SlurmdLogFile", hashtbl);

	(void) s_p_get_string(&conf->slurmd_params, "SlurmdParameters", hashtbl);

	if (!s_p_get_string(&conf->slurmd_pidfile, "SlurmdPidFile", hashtbl))
		conf->slurmd_pidfile = xstrdup(DEFAULT_SLURMD_PIDFILE);

	if (!s_p_get_uint32(&conf->slurmd_port, "SlurmdPort", hashtbl))
		conf->slurmd_port = SLURMD_PORT;

	(void) s_p_get_string(&conf->sched_logfile, "SlurmSchedLogFile",
			      hashtbl);

	if (!s_p_get_uint16(&conf->sched_log_level,
			   "SlurmSchedLogLevel", hashtbl))
		conf->sched_log_level = DEFAULT_SCHED_LOG_LEVEL;
	if (conf->sched_log_level && !conf->sched_logfile) {
		error("SlurmSchedLogLevel requires SlurmSchedLogFile value");
		return SLURM_ERROR;
	}

	if (!s_p_get_string(&conf->slurmd_spooldir, "SlurmdSpoolDir", hashtbl))
		conf->slurmd_spooldir = xstrdup(DEFAULT_SPOOLDIR);

	if (s_p_get_string(&temp_str, "SlurmdSyslogDebug", hashtbl)) {
		conf->slurmd_syslog_debug = log_string2num(temp_str);
		if (conf->slurmd_syslog_debug == NO_VAL16) {
			error("Invalid SlurmdSyslogDebug %s", temp_str);
			return SLURM_ERROR;
		}
		xfree(temp_str);
		_normalize_debug_level(&conf->slurmd_syslog_debug);
	} else
		conf->slurmd_syslog_debug = LOG_LEVEL_END;

	if (!s_p_get_uint16(&conf->slurmd_timeout, "SlurmdTimeout", hashtbl))
		conf->slurmd_timeout = DEFAULT_SLURMD_TIMEOUT;

	(void) s_p_get_string(&conf->srun_prolog, "SrunProlog", hashtbl);
	if (s_p_get_string(&temp_str, "SrunPortRange", hashtbl)) {
		conf->srun_port_range = _parse_srun_ports(temp_str);
		xfree(temp_str);
	}
	(void) s_p_get_string(&conf->srun_epilog, "SrunEpilog", hashtbl);

	if (!s_p_get_string(&conf->state_save_location,
			    "StateSaveLocation", hashtbl))
		conf->state_save_location = xstrdup(DEFAULT_SAVE_STATE_LOC);

	(void) s_p_get_string(&conf->suspend_exc_nodes, "SuspendExcNodes",
			      hashtbl);
	(void) s_p_get_string(&conf->suspend_exc_parts, "SuspendExcParts",
			      hashtbl);
	(void) s_p_get_string(&conf->suspend_program, "SuspendProgram",
			      hashtbl);
	if (!s_p_get_uint16(&conf->suspend_rate, "SuspendRate", hashtbl))
		conf->suspend_rate = DEFAULT_SUSPEND_RATE;
	if (s_p_get_string(&temp_str, "SuspendTime", hashtbl)) {
		if (!xstrcasecmp(temp_str, "NONE"))
			long_suspend_time = -1;
		else
			long_suspend_time = atoi(temp_str);
		xfree(temp_str);
		if (long_suspend_time < -1) {
			error("SuspendTime value (%ld) is less than -1",
			      long_suspend_time);
		} else
			conf->suspend_time = long_suspend_time + 1;
	} else {
		conf->suspend_time = 0;
	}
	if (!s_p_get_uint16(&conf->suspend_timeout, "SuspendTimeout", hashtbl))
		conf->suspend_timeout = DEFAULT_SUSPEND_TIMEOUT;

	/* see above for switch_type, order dependent */

	if (!s_p_get_string(&conf->task_plugin, "TaskPlugin", hashtbl))
		conf->task_plugin = xstrdup(DEFAULT_TASK_PLUGIN);
#ifdef HAVE_FRONT_END
	if (xstrcmp(conf->task_plugin, "task/none")) {
		error("On FrontEnd systems TaskPlugin=task/none is required");
		return SLURM_ERROR;
	}
#endif

	if (s_p_get_string(&temp_str, "TaskPluginParam", hashtbl)) {
		char *last = NULL, *tok;
		bool set_mode = false, set_unit = false, set_auto = false;
		tok = strtok_r(temp_str, ",", &last);
		while (tok) {
			if (xstrcasecmp(tok, "none") == 0) {
				if (set_unit) {
					error("Bad TaskPluginParam: %s", tok);
					return SLURM_ERROR;
				}
				set_unit = true;
				conf->task_plugin_param |= CPU_BIND_NONE;
			} else if (xstrcasecmp(tok, "boards") == 0) {
				if (set_unit) {
					error("Bad TaskPluginParam: %s", tok);
					return SLURM_ERROR;
				}
				set_unit = true;
				conf->task_plugin_param |= CPU_BIND_TO_BOARDS;
			} else if (xstrcasecmp(tok, "sockets") == 0) {
				if (set_unit) {
					error("Bad TaskPluginParam: %s", tok);
					return SLURM_ERROR;
				}
				set_unit = true;
				conf->task_plugin_param |= CPU_BIND_TO_SOCKETS;
			} else if (xstrcasecmp(tok, "cores") == 0) {
				if (set_unit) {
					error("Bad TaskPluginParam: %s", tok);
					return SLURM_ERROR;
				}
				set_unit = true;
				conf->task_plugin_param |= CPU_BIND_TO_CORES;
			} else if (xstrcasecmp(tok, "threads") == 0) {
				if (set_unit) {
					error("Bad TaskPluginParam: %s", tok);
					return SLURM_ERROR;
				}
				set_unit = true;
				conf->task_plugin_param |= CPU_BIND_TO_THREADS;
			} else if (xstrcasecmp(tok, "cpusets") == 0) {
				if (set_mode) {
					error("Bad TaskPluginParam: %s", tok);
					return SLURM_ERROR;
				}
				set_mode = true;
				conf->task_plugin_param |= CPU_BIND_CPUSETS;
			} else if (xstrcasecmp(tok, "sched") == 0) {
				if (set_mode) {
					error("Bad TaskPluginParam: %s", tok);
					return SLURM_ERROR;
				}
				set_mode = true;
				/* No change to task_plugin_param,
				 * this is the default */
			} else if (xstrcasecmp(tok, "verbose") == 0) {
				conf->task_plugin_param |= CPU_BIND_VERBOSE;
			} else if (xstrncasecmp(tok, "autobind=",
						strlen("autobind=")) == 0) {
				char *val_ptr = tok + strlen("autobind=");

				if (set_auto) {
					error("Bad TaskPluginParam: "
							"autobind already set");
					return SLURM_ERROR;
				}

				if (xstrcasecmp(val_ptr, "none") == 0) {
					set_auto = true;
				} else if (xstrcasecmp(val_ptr,
						       "threads") == 0) {
					set_auto = true;
					conf->task_plugin_param |=
						CPU_AUTO_BIND_TO_THREADS;
				} else if (xstrcasecmp(val_ptr,
						       "cores") == 0) {
					set_auto = true;
					conf->task_plugin_param |=
						CPU_AUTO_BIND_TO_CORES;
				} else if (xstrcasecmp(val_ptr,
						       "sockets") == 0) {
					set_auto = true;
					conf->task_plugin_param |=
						CPU_AUTO_BIND_TO_SOCKETS;
				} else {
					error("Bad TaskPluginParam autobind "
							"value: %s",val_ptr);
					return SLURM_ERROR;
				}
			} else if (xstrcasecmp(tok, "SlurmdOffSpec") == 0) {
				if (xstrcasestr(conf->task_plugin, "cray")) {
					error("TaskPluginParam=SlurmdOffSpec "
					      "invalid with TaskPlugin=task/cray");
					return SLURM_ERROR;
				}
				conf->task_plugin_param |= SLURMD_OFF_SPEC;
			} else {
				error("Bad TaskPluginParam: %s", tok);
				return SLURM_ERROR;
			}
			tok = strtok_r(NULL, ",", &last);
		}
		xfree(temp_str);
	}

	(void) s_p_get_string(&conf->task_epilog, "TaskEpilog", hashtbl);
	(void) s_p_get_string(&conf->task_prolog, "TaskProlog", hashtbl);

	if (!s_p_get_uint16(&conf->tcp_timeout, "TCPTimeout", hashtbl))
		conf->tcp_timeout = DEFAULT_TCP_TIMEOUT;

	if (!s_p_get_string(&conf->tmp_fs, "TmpFS", hashtbl))
		conf->tmp_fs = xstrdup(DEFAULT_TMP_FS);

	if (!s_p_get_uint16(&conf->wait_time, "WaitTime", hashtbl))
		conf->wait_time = DEFAULT_WAIT_TIME;

	(void) s_p_get_string(&conf->x11_params, "X11Parameters", hashtbl);

	(void) s_p_get_string(&conf->topology_param, "TopologyParam", hashtbl);
	if (conf->topology_param) {
		/* Move legacy settings over to new spot */
		char *legacy_var = "NoInAddrAny";
		if (xstrcasestr(conf->topology_param, legacy_var) &&
		    !xstrcasestr(conf->comm_params, legacy_var))
			xstrfmtcat(conf->comm_params, "%s%s",
				   conf->comm_params ? "," : "", legacy_var);

		legacy_var = "NoCtldInAddrAny";
		if (xstrcasestr(conf->topology_param, legacy_var) &&
		    !xstrcasestr(conf->comm_params, legacy_var))
			xstrfmtcat(conf->comm_params, "%s%s",
				   conf->comm_params ? "," : "", legacy_var);
	}

	if (!s_p_get_string(&conf->topology_plugin, "TopologyPlugin", hashtbl))
		conf->topology_plugin = xstrdup(DEFAULT_TOPOLOGY_PLUGIN);

	if (s_p_get_uint16(&conf->tree_width, "TreeWidth", hashtbl)) {
		if (conf->tree_width == 0) {
			error("TreeWidth=0 is invalid");
			conf->tree_width = DEFAULT_TREE_WIDTH;
		}
	} else {
		conf->tree_width = DEFAULT_TREE_WIDTH;
	}

	if (s_p_get_boolean(&truth, "UsePAM", hashtbl) && truth) {
		conf->use_pam = 1;
	} else {
		conf->use_pam = 0;
	}

	s_p_get_string(&conf->unkillable_program,
		       "UnkillableStepProgram", hashtbl);
	if (!s_p_get_uint16(&conf->unkillable_timeout,
			    "UnkillableStepTimeout", hashtbl))
		conf->unkillable_timeout = DEFAULT_UNKILLABLE_TIMEOUT;

	(void) s_p_get_uint16(&conf->vsize_factor, "VSizeFactor", hashtbl);

	/* The default value is true meaning the memory
	 * is going to be enforced by slurmstepd and/or
	 * slurmd.
	 */
	if (s_p_get_string(&temp_str, "MemLimitEnforce", hashtbl)) {
		if (xstrncasecmp(temp_str, "yes", 2) == 0)
			conf->mem_limit_enforce = true;
		xfree(temp_str);
	}

	/* The default values for both of these variables are NULL.
	 */
	(void) s_p_get_string(&conf->requeue_exit, "RequeueExit", hashtbl);
	(void) s_p_get_string(&conf->requeue_exit_hold, "RequeueExitHold",
			      hashtbl);

	if (!s_p_get_string(&conf->layouts, "Layouts", hashtbl))
		conf->layouts = xstrdup("");

	/* srun eio network timeout with the slurmstepd
	 */
	if (!s_p_get_uint16(&conf->eio_timeout, "EioTimeout", hashtbl))
		conf->eio_timeout = DEFAULT_EIO_SHUTDOWN_WAIT;

	if (!s_p_get_uint16(&conf->prolog_epilog_timeout,
			    "PrologEpilogTimeout",
			    hashtbl)) {
		/* The default value is wait forever
		 */
		conf->prolog_epilog_timeout = NO_VAL16;
	}

	xfree(default_storage_type);
	xfree(default_storage_loc);
	xfree(default_storage_host);
	xfree(default_storage_user);
	xfree(default_storage_pass);

	return SLURM_SUCCESS;
}

/*
 * Replace first "%h" in path string with NodeHostname.
 * Replace first "%n" in path string with NodeName.
 *
 * NOTE: Caller should be holding slurm_conf_lock() when calling this function.
 *
 * Returns an xmalloc()ed string which the caller must free with xfree().
 */
extern char *
slurm_conf_expand_slurmd_path(const char *path, const char *node_name)
{
	char *hostname;
	char *dir = NULL;

	dir = xstrdup(path);
	hostname = _internal_get_hostname(node_name);
	xstrsubstitute(dir, "%h", hostname);
	xfree(hostname);
	xstrsubstitute(dir, "%n", node_name);

	return dir;
}

/*
 * prolog_flags2str - convert a PrologFlags uint16_t to the equivalent string
 * Keep in sync with prolog_str2flags() below
 */
extern char * prolog_flags2str(uint16_t prolog_flags)
{
	char *rc = NULL;

	if (prolog_flags & PROLOG_FLAG_ALLOC) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "Alloc");
	}

	if (prolog_flags & PROLOG_FLAG_CONTAIN) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "Contain");
	}

	if (prolog_flags & PROLOG_FLAG_NOHOLD) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "NoHold");
	}

	if (prolog_flags & PROLOG_FLAG_SERIAL) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "Serial");
	}

	if (prolog_flags & PROLOG_FLAG_X11) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "X11");
	}

	return rc;
}

/*
 * prolog_str2flags - Convert a PrologFlags string to the equivalent uint16_t
 * Keep in sync with prolog_flags2str() above
 * Returns NO_VAL if invalid
 */
extern uint16_t prolog_str2flags(char *prolog_flags)
{
	uint16_t rc = 0;
	char *tmp_str, *tok, *last = NULL;

	if (!prolog_flags)
		return rc;

	tmp_str = xstrdup(prolog_flags);
	tok = strtok_r(tmp_str, ",", &last);
	while (tok) {
		if (xstrcasecmp(tok, "Alloc") == 0)
			rc |= PROLOG_FLAG_ALLOC;
		else if (xstrcasecmp(tok, "Contain") == 0)
			rc |= (PROLOG_FLAG_ALLOC | PROLOG_FLAG_CONTAIN);
		else if (xstrcasecmp(tok, "NoHold") == 0)
			rc |= PROLOG_FLAG_NOHOLD;
		else if (xstrcasecmp(tok, "Serial") == 0)
			rc |= PROLOG_FLAG_SERIAL;
		else if (xstrcasecmp(tok, "X11") == 0) {
#ifdef WITH_SLURM_X11
			rc |= (PROLOG_FLAG_ALLOC | PROLOG_FLAG_CONTAIN
			       | PROLOG_FLAG_X11);
#else
			error("X11 forwarding not built in, cannot enable.");
			rc = NO_VAL16;
			break;
#endif
		} else {
			error("Invalid PrologFlag: %s", tok);
			rc = NO_VAL16;
			break;
		}
		tok = strtok_r(NULL, ",", &last);
	}
	xfree(tmp_str);

	return rc;
}

/*
 * debug_flags2str - convert a DebugFlags uint64_t to the equivalent string
 * Keep in sync with debug_str2flags() below
 */
extern char * debug_flags2str(uint64_t debug_flags)
{
	char *rc = NULL;

	/* When adding to this please attempt to keep flags in
	 * alphabetical order.
	 */

	if (debug_flags & DEBUG_FLAG_BACKFILL) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "Backfill");
	}
	if (debug_flags & DEBUG_FLAG_BACKFILL_MAP) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "BackfillMap");
	}
	if (debug_flags & DEBUG_FLAG_BG_ALGO) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "BGBlockAlgo");
	}
	if (debug_flags & DEBUG_FLAG_BG_ALGO_DEEP) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "BGBlockAlgoDeep");
	}
	if (debug_flags & DEBUG_FLAG_BG_PICK) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "BGBlockPick");
	}
	if (debug_flags & DEBUG_FLAG_BG_WIRES) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "BGBlockWires");
	}
	if (debug_flags & DEBUG_FLAG_BURST_BUF) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "BurstBuffer");
	}
	if (debug_flags & DEBUG_FLAG_CPU_FREQ) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "CpuFrequency");
	}
	if (debug_flags & DEBUG_FLAG_CPU_BIND) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "CPU_Bind");
	}
	if (debug_flags & DEBUG_FLAG_DB_ARCHIVE) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "DB_Archive");
	}
	if (debug_flags & DEBUG_FLAG_DB_ASSOC) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "DB_Assoc");
	}
	if (debug_flags & DEBUG_FLAG_DB_TRES) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "DB_TRES");
	}
	if (debug_flags & DEBUG_FLAG_DB_EVENT) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "DB_Event");
	}
	if (debug_flags & DEBUG_FLAG_DB_JOB) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "DB_Job");
	}
	if (debug_flags & DEBUG_FLAG_DB_QOS) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "DB_QOS");
	}
	if (debug_flags & DEBUG_FLAG_DB_QUERY) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "DB_Query");
	}
	if (debug_flags & DEBUG_FLAG_DB_RESV) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "DB_Reservation");
	}
	if (debug_flags & DEBUG_FLAG_DB_RES) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "DB_Resource");
	}
	if (debug_flags & DEBUG_FLAG_DB_STEP) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "DB_Step");
	}
	if (debug_flags & DEBUG_FLAG_DB_USAGE) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "DB_Usage");
	}
	if (debug_flags & DEBUG_FLAG_DB_WCKEY) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "DB_WCKey");
	}
	if (debug_flags & DEBUG_FLAG_ESEARCH) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "Elasticsearch");
	}
	if (debug_flags & DEBUG_FLAG_ENERGY) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "Energy");
	}
	if (debug_flags & DEBUG_FLAG_EXT_SENSORS) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "ExtSensors");
	}
	if (debug_flags & DEBUG_FLAG_FILESYSTEM) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "Filesystem");
	}
	if (debug_flags & DEBUG_FLAG_FEDR) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "Federation");
	}
	if (debug_flags & DEBUG_FLAG_FRONT_END) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "FrontEnd");
	}
	if (debug_flags & DEBUG_FLAG_GANG) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "Gang");
	}
	if (debug_flags & DEBUG_FLAG_GRES) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "Gres");
	}
	if (debug_flags & DEBUG_FLAG_HETERO_JOBS) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "HeteroJobs");
	}
	if (debug_flags & DEBUG_FLAG_INTERCONNECT) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "Interconnect");
	}
	if (debug_flags & DEBUG_FLAG_JOB_CONT) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "JobContainer");
	}
	if (debug_flags & DEBUG_FLAG_NODE_FEATURES) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "NodeFeatures");
	}
	if (debug_flags & DEBUG_FLAG_LICENSE) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "License");
	}
	if (debug_flags & DEBUG_FLAG_NO_CONF_HASH) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "NO_CONF_HASH");
	}
	if (debug_flags & DEBUG_FLAG_NO_REALTIME) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "NoRealTime");
	}
	if (debug_flags & DEBUG_FLAG_POWER) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "Power");
	}
	if (debug_flags & DEBUG_FLAG_PRIO) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "Priority");
	}
	if (debug_flags & DEBUG_FLAG_PROFILE) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "Profile");
	}
	if (debug_flags & DEBUG_FLAG_PROTOCOL) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "Protocol");
	}
	if (debug_flags & DEBUG_FLAG_RESERVATION) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "Reservation");
	}
	if (debug_flags & DEBUG_FLAG_ROUTE) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "Route");
	}
	if (debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "SelectType");
	}
	if (debug_flags & DEBUG_FLAG_STEPS) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "Steps");
	}
	if (debug_flags & DEBUG_FLAG_SWITCH) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "Switch");
	}
	if (debug_flags & DEBUG_FLAG_TASK) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "Task");
	}
	if (debug_flags & DEBUG_FLAG_TIME_CRAY) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "TimeCray");
	}
	if (debug_flags & DEBUG_FLAG_TRACE_JOBS) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "TraceJobs");
	}
	if (debug_flags & DEBUG_FLAG_TRIGGERS) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "Triggers");
	}

	return rc;
}

/*
 * debug_str2flags - Convert a DebugFlags string to the equivalent uint64_t
 * Keep in sycn with debug_flags2str() above
 * Returns SLURM_ERROR if invalid
 */
extern int debug_str2flags(char *debug_flags, uint64_t *flags_out)
{
	int rc = SLURM_SUCCESS;
	char *tmp_str, *tok, *last = NULL;

	xassert(flags_out);

	(*flags_out) = 0;

	if (!debug_flags)
		return rc;

	tmp_str = xstrdup(debug_flags);
	tok = strtok_r(tmp_str, ",", &last);
	while (tok) {
		if (xstrcasecmp(tok, "Backfill") == 0)
			(*flags_out) |= DEBUG_FLAG_BACKFILL;
		else if (xstrcasecmp(tok, "BackfillMap") == 0)
			(*flags_out) |= DEBUG_FLAG_BACKFILL_MAP;
		else if (xstrcasecmp(tok, "BGBlockAlgo") == 0)
			(*flags_out) |= DEBUG_FLAG_BG_ALGO;
		else if (xstrcasecmp(tok, "BGBlockAlgoDeep") == 0)
			(*flags_out) |= DEBUG_FLAG_BG_ALGO_DEEP;
		else if (xstrcasecmp(tok, "BGBlockPick") == 0)
			(*flags_out) |= DEBUG_FLAG_BG_PICK;
		else if (xstrcasecmp(tok, "BGBlockWires") == 0)
			(*flags_out) |= DEBUG_FLAG_BG_WIRES;
		else if (xstrcasecmp(tok, "BurstBuffer") == 0)
			(*flags_out) |= DEBUG_FLAG_BURST_BUF;
		else if (xstrcasecmp(tok, "CPU_Bind") == 0)
			(*flags_out) |= DEBUG_FLAG_CPU_BIND;
		else if (xstrcasecmp(tok, "DB_Archive") == 0)
			(*flags_out) |= DEBUG_FLAG_DB_ARCHIVE;
		else if (xstrcasecmp(tok, "DB_Assoc") == 0)
			(*flags_out) |= DEBUG_FLAG_DB_ASSOC;
		else if (xstrcasecmp(tok, "DB_TRES") == 0)
			(*flags_out) |= DEBUG_FLAG_DB_TRES;
		else if (xstrcasecmp(tok, "DB_Event") == 0)
			(*flags_out) |= DEBUG_FLAG_DB_EVENT;
		else if (xstrcasecmp(tok, "DB_Job") == 0)
			(*flags_out) |= DEBUG_FLAG_DB_JOB;
		else if (xstrcasecmp(tok, "DB_QOS") == 0)
			(*flags_out) |= DEBUG_FLAG_DB_QOS;
		else if (xstrcasecmp(tok, "DB_Query") == 0)
			(*flags_out) |= DEBUG_FLAG_DB_QUERY;
		else if (xstrcasecmp(tok, "DB_Reservation") == 0)
			(*flags_out) |= DEBUG_FLAG_DB_RESV;
		else if (xstrcasecmp(tok, "DB_Resource") == 0)
			(*flags_out) |= DEBUG_FLAG_DB_RES;
		else if (xstrcasecmp(tok, "DB_Step") == 0)
			(*flags_out) |= DEBUG_FLAG_DB_STEP;
		else if (xstrcasecmp(tok, "DB_Usage") == 0)
			(*flags_out) |= DEBUG_FLAG_DB_USAGE;
		else if (xstrcasecmp(tok, "DB_WCKey") == 0)
			(*flags_out) |= DEBUG_FLAG_DB_WCKEY;
		else if (xstrcasecmp(tok, "Elasticsearch") == 0)
			(*flags_out) |= DEBUG_FLAG_ESEARCH;
		else if (xstrcasecmp(tok, "Energy") == 0)
			(*flags_out) |= DEBUG_FLAG_ENERGY;
		else if (xstrcasecmp(tok, "ExtSensors") == 0)
			(*flags_out) |= DEBUG_FLAG_EXT_SENSORS;
		else if (xstrcasecmp(tok, "Federation") == 0)
			(*flags_out) |= DEBUG_FLAG_FEDR;
		else if (xstrcasecmp(tok, "FrontEnd") == 0)
			(*flags_out) |= DEBUG_FLAG_FRONT_END;
		else if (xstrcasecmp(tok, "Gang") == 0)
			(*flags_out) |= DEBUG_FLAG_GANG;
		else if (xstrcasecmp(tok, "Gres") == 0)
			(*flags_out) |= DEBUG_FLAG_GRES;
		else if (xstrcasecmp(tok, "HeteroJobs") == 0)
			(*flags_out) |= DEBUG_FLAG_HETERO_JOBS;
		else if (xstrcasecmp(tok, "Federation") == 0)
			(*flags_out) |= DEBUG_FLAG_FEDR;
		else if (xstrcasecmp(tok, "Interconnect") == 0)
			(*flags_out) |= DEBUG_FLAG_INTERCONNECT;
		else if (xstrcasecmp(tok, "Filesystem") == 0)
			(*flags_out) |= DEBUG_FLAG_FILESYSTEM;
		else if (xstrcasecmp(tok, "JobContainer") == 0)
			(*flags_out) |= DEBUG_FLAG_JOB_CONT;
		else if (xstrcasecmp(tok, "License") == 0)
			(*flags_out) |= DEBUG_FLAG_LICENSE;
		else if (xstrcasecmp(tok, "NO_CONF_HASH") == 0)
			(*flags_out) |= DEBUG_FLAG_NO_CONF_HASH;
		else if (xstrcasecmp(tok, "NodeFeatures") == 0)
			(*flags_out) |= DEBUG_FLAG_NODE_FEATURES;
		else if (xstrcasecmp(tok, "NoRealTime") == 0)
			(*flags_out) |= DEBUG_FLAG_NO_REALTIME;
		else if (xstrcasecmp(tok, "Priority") == 0)
			(*flags_out) |= DEBUG_FLAG_PRIO;
		else if (xstrcasecmp(tok, "Profile") == 0)
			(*flags_out) |= DEBUG_FLAG_PROFILE;
		else if (xstrcasecmp(tok, "Protocol") == 0)
			(*flags_out) |= DEBUG_FLAG_PROTOCOL;
		else if (xstrcasecmp(tok, "Reservation") == 0)
			(*flags_out) |= DEBUG_FLAG_RESERVATION;
		else if (xstrcasecmp(tok, "Route") == 0)
			(*flags_out) |= DEBUG_FLAG_ROUTE;
		else if (xstrcasecmp(tok, "SelectType") == 0)
			(*flags_out) |= DEBUG_FLAG_SELECT_TYPE;
		else if (xstrcasecmp(tok, "Steps") == 0)
			(*flags_out) |= DEBUG_FLAG_STEPS;
		else if (xstrcasecmp(tok, "Switch") == 0)
			(*flags_out) |= DEBUG_FLAG_SWITCH;
		else if (xstrcasecmp(tok, "Task") == 0)
			(*flags_out) |= DEBUG_FLAG_TASK;
		else if (xstrcasecmp(tok, "TraceJobs") == 0)
			(*flags_out) |= DEBUG_FLAG_TRACE_JOBS;
		else if (xstrcasecmp(tok, "Trigger") == 0)
			(*flags_out) |= DEBUG_FLAG_TRIGGERS;
		else if (xstrcasecmp(tok, "Triggers") == 0)
			(*flags_out) |= DEBUG_FLAG_TRIGGERS;
		else if (xstrcasecmp(tok, "CpuFrequency") == 0)
			(*flags_out) |= DEBUG_FLAG_CPU_FREQ;
		else if (xstrcasecmp(tok, "Power") == 0)
			(*flags_out) |= DEBUG_FLAG_POWER;
		else if (xstrcasecmp(tok, "TimeCray") == 0)
			(*flags_out) |= DEBUG_FLAG_TIME_CRAY;
		else {
			error("Invalid DebugFlag: %s", tok);
			(*flags_out) = 0;
			rc = SLURM_ERROR;
			break;
		}
		tok = strtok_r(NULL, ",", &last);
	}
	xfree(tmp_str);

	return rc;
}

/*
 * reconfig_flags2str - convert a ReconfFlags uint16_t to the equivalent string
 * Keep in sync with reconfig_str2flags() below
 */
extern char * reconfig_flags2str(uint16_t reconfig_flags)
{
	char *rc = NULL;

	if (reconfig_flags & RECONFIG_KEEP_PART_INFO) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "KeepPartInfo");
	}
	if (reconfig_flags & RECONFIG_KEEP_PART_STAT) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "KeepPartState");
	}

	return rc;
}

/*
 * reconfig_str2flags - Convert a ReconfFlags string to the equivalent uint16_t
 * Keep in sync with reconfig_flags2str() above
 * Returns NO_VAL if invalid
 */
extern uint16_t reconfig_str2flags(char *reconfig_flags)
{
	uint16_t rc = 0;
	char *tmp_str, *tok, *last = NULL;

	if (!reconfig_flags)
		 return rc;

	tmp_str = xstrdup(reconfig_flags);
	tok = strtok_r(tmp_str, ",", &last);
	while (tok) {
		if (xstrcasecmp(tok, "KeepPartInfo") == 0)
			rc |= RECONFIG_KEEP_PART_INFO;
		else if (xstrcasecmp(tok, "KeepPartState") == 0)
			rc |= RECONFIG_KEEP_PART_STAT;
		else {
			error("Invalid ReconfigFlag: %s", tok);
			rc = NO_VAL16;
			break;
		}
		tok = strtok_r(NULL, ",", &last);
	}
	xfree(tmp_str);

	return rc;
}

extern void destroy_config_plugin_params(void *object)
{
	config_plugin_params_t *plugin_ptr = (config_plugin_params_t *)object;

	if (plugin_ptr) {
		xfree(plugin_ptr->name);
		FREE_NULL_LIST(plugin_ptr->key_pairs);
		xfree(object);
	}
}

extern void pack_config_plugin_params(void *in, uint16_t protocol_version,
				      Buf buff)
{
       config_plugin_params_t *object = (config_plugin_params_t *)in;

       packstr(object->name, buff);
       pack_key_pair_list((void *)object->key_pairs, protocol_version, buff);
}

extern int
unpack_config_plugin_params(void **object, uint16_t protocol_version, Buf buff)
{
	uint32_t uint32_tmp;
	config_plugin_params_t *object_ptr =
		xmalloc(sizeof(config_plugin_params_t));

	*object = object_ptr;
	safe_unpackstr_xmalloc(&object_ptr->name,  &uint32_tmp, buff);

	if (unpack_key_pair_list((void *) &object_ptr->key_pairs,
				 protocol_version, buff) != SLURM_SUCCESS)
		goto unpack_error;

	return SLURM_SUCCESS;

unpack_error:
	destroy_config_plugin_params(object_ptr);
	return SLURM_ERROR;
}

extern void
pack_config_plugin_params_list(void *in, uint16_t protocol_version, Buf buff)
{
	uint32_t count = NO_VAL;

	if (in)
		count = list_count(in);
	pack32(count, buff);
	if (count && (count != NO_VAL))	{
		ListIterator itr = list_iterator_create((List)in);
		config_plugin_params_t *obj = NULL;
		while ((obj = list_next(itr))) {
			pack_config_plugin_params(obj, protocol_version, buff);
		}
		list_iterator_destroy(itr);
	}
}

extern int
unpack_config_plugin_params_list(void **plugin_params_l,
				 uint16_t protocol_version, Buf buff)
{
	uint32_t count = NO_VAL;
	List tmp_list = NULL;

	safe_unpack32(&count, buff);
	if (count > NO_VAL)
		goto unpack_error;
	if (count != NO_VAL) {
		tmp_list = list_create(destroy_config_plugin_params);
		config_plugin_params_t *object = NULL;
		int i;
		for (i = 0; i < count; i++) {
			if (unpack_config_plugin_params(
				    (void *)&object, protocol_version, buff)
			    == SLURM_ERROR)
				goto unpack_error;
			list_append(tmp_list, object);
		}
		*plugin_params_l = (void *)tmp_list;
	}
	return SLURM_SUCCESS;

unpack_error:
	FREE_NULL_LIST(tmp_list);
	return SLURM_ERROR;
}

extern void destroy_config_key_pair(void *object)
{
	config_key_pair_t *key_pair_ptr = (config_key_pair_t *)object;

	if (key_pair_ptr) {
		xfree(key_pair_ptr->name);
		xfree(key_pair_ptr->value);
		xfree(key_pair_ptr);
	}
}

extern void
pack_config_key_pair(void *in, uint16_t protocol_version, Buf buffer)
{
	config_key_pair_t *object = (config_key_pair_t *)in;
	packstr(object->name,  buffer);
	packstr(object->value, buffer);
}

extern int
unpack_config_key_pair(void **object, uint16_t protocol_version, Buf buffer)
{
	uint32_t uint32_tmp;
	config_key_pair_t *object_ptr = xmalloc(sizeof(config_key_pair_t));

	*object = object_ptr;
	safe_unpackstr_xmalloc(&object_ptr->name,  &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&object_ptr->value, &uint32_tmp, buffer);

	return SLURM_SUCCESS;

unpack_error:
	destroy_config_key_pair(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void
pack_key_pair_list(void *key_pairs, uint16_t protocol_version, Buf buffer)
{
	uint32_t count = NO_VAL;

	if (key_pairs)
		count = list_count(key_pairs);
	pack32(count, buffer);
	if (count && (count != NO_VAL)) {
		ListIterator itr = list_iterator_create(
			(List)key_pairs);
		config_key_pair_t *key_pair = NULL;
		while ((key_pair = list_next(itr))) {
			pack_config_key_pair(key_pair, protocol_version,
					     buffer);
		}
		list_iterator_destroy(itr);
	}
}

extern int
unpack_key_pair_list(void **key_pairs, uint16_t protocol_version, Buf buffer)
{
	uint32_t count = NO_VAL;
	List tmp_list = NULL;

	safe_unpack32(&count, buffer);
	if (count > NO_VAL)
		goto unpack_error;
	if (count != NO_VAL) {
		tmp_list = list_create(destroy_config_key_pair);
		config_key_pair_t *object = NULL;
		int i;
		for (i = 0; i < count; i++) {
			if (unpack_config_key_pair((void *)&object,
						   protocol_version, buffer)
			    == SLURM_ERROR)
				goto unpack_error;
			list_append(tmp_list, object);
		}
		*key_pairs = (void *)tmp_list;
	}
	return SLURM_SUCCESS;

unpack_error:
	FREE_NULL_LIST(tmp_list);
	return SLURM_ERROR;
}

extern int sort_key_pairs(void *v1, void *v2)
{
	config_key_pair_t *key_a = *(config_key_pair_t **)v1;
	config_key_pair_t *key_b = *(config_key_pair_t **)v2;

	int size_a = xstrcmp(key_a->name, key_b->name);

	if (size_a < 0)
		return -1;
	else if (size_a > 0)
		return 1;

	return 0;
}

/*
 * Return the pathname of the extra .conf file
 */
extern char *get_extra_conf_path(char *conf_name)
{
	char *val = getenv("SLURM_CONF");
	char *rc = NULL, *slash;

	if (!val)
		val = default_slurm_config_file;

	/* Replace file name on end of path */
	rc = xstrdup(val);
	if ((slash = strrchr(rc, '/')))
		slash[1] = '\0';
	else
		rc[0] = '\0';
	xstrcat(rc, conf_name);

	return rc;
}

extern bool run_in_daemon(char *daemons)
{
	char *full, *start_char, *end_char;

	xassert(slurm_prog_name);

	if (!xstrcmp(daemons, slurm_prog_name))
		return true;

	full = xstrdup(daemons);
	start_char = full;

	while (start_char && (end_char = strstr(start_char, ","))) {
		*end_char = 0;
		if (!xstrcmp(start_char, slurm_prog_name)) {
			xfree(full);
			return true;
		}

		start_char = end_char + 1;
	}

	if (start_char && !xstrcmp(start_char, slurm_prog_name)) {
		xfree(full);
		return true;
	}

	xfree(full);

	return false;
}

/*
 * Add nodes and corresponding pre-configured slurm_addr_t's to node conf hash
 * tables.
 *
 * IN node_list - node_list allocated to job
 * IN node_addrs - array of slurm_addr_t that corresponds to nodes built from
 * 	host_list. See build_node_details().
 * RET return SLURM_SUCCESS on success, SLURM_ERROR otherwise.
 */
extern int add_remote_nodes_to_conf_tbls(char *node_list,
					 slurm_addr_t *node_addrs)
{
	char *hostname       = NULL;
	hostlist_t host_list = NULL;
	int i = 0;

	xassert(node_list);
	xassert(node_addrs);

	if ((host_list = hostlist_create(node_list)) == NULL) {
		error("hostlist_create error for %s: %m",
		      node_list);
		return SLURM_ERROR;
	}

	/*
	 * flush tables since clusters could share the same nodes names.
	 * Leave nodehash_intialized so that the tables don't get overridden
	 * later
	 */
	_free_name_hashtbl();
	nodehash_initialized = true;

	while ((hostname = hostlist_shift(host_list))) {
		_push_to_hashtbls(hostname, hostname,
				  NULL, 0, 0,
				  0, 0, 0, 0, false, NULL, 0,
				  0, &node_addrs[i++], true);
		free(hostname);
	}

	hostlist_destroy(host_list);

	return SLURM_SUCCESS;
}

/*
 * Get result of configuration file test.
 * RET SLURM_SUCCESS or error code
 */
extern int config_test_result(void)
{
	return local_test_config_rc;
}


/*
 * Start configuration file test mode. Disables fatal errors.
 */
extern void config_test_start(void)
{
	local_test_config = true;
	local_test_config_rc = 0;
}
