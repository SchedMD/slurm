/*****************************************************************************\
 *  read_config.c - read the overall slurm configuration file
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2008 Vijay Ramasubramanian.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
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

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <slurm/slurm.h>

#include "src/common/hostlist.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/parse_spec.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/slurm_rlimits_info.h"
#include "src/common/parse_config.h"
#include "src/common/parse_time.h"
#include "src/common/slurm_selecttype_info.h"
#include "src/common/util-net.h"
#include "src/common/uid.h"

/*
** Define slurm-specific aliases for use by plugins, see slurm_xlator.h 
** for details. 
 */
strong_alias(destroy_config_key_pair, slurm_destroy_config_key_pair);
strong_alias(sort_key_pairs, slurm_sort_key_pairs);

/* Instantiation of the "extern slurm_ctl_conf_t slurmcltd_conf"
 * found in slurmctld.h */
slurm_ctl_conf_t slurmctld_conf;

static pthread_mutex_t conf_lock = PTHREAD_MUTEX_INITIALIZER;
static s_p_hashtbl_t *conf_hashtbl = NULL;
static slurm_ctl_conf_t *conf_ptr = &slurmctld_conf;
static bool conf_initialized = false;

static s_p_hashtbl_t *default_nodename_tbl;
static s_p_hashtbl_t *default_partition_tbl;

inline static void _normalize_debug_level(uint16_t *level);
static void _init_slurm_conf(const char *file_name);

#define NAME_HASH_LEN 512
typedef struct names_ll_s {
	char *alias;	/* NodeName */
	char *hostname;	/* NodeHostname */
	char *address;	/* NodeAddr */
	uint16_t port;
	uint16_t cpus;
	uint16_t sockets;
	uint16_t cores;
	uint16_t threads;
	slurm_addr addr;
	bool addr_initialized;
	struct names_ll_s *next_alias;
	struct names_ll_s *next_hostname;
} names_ll_t;
bool nodehash_initialized = false;
static names_ll_t *host_to_node_hashtbl[NAME_HASH_LEN] = {NULL};
static names_ll_t *node_to_host_hashtbl[NAME_HASH_LEN] = {NULL};

static int _parse_nodename(void **dest, slurm_parser_enum_t type,
			   const char *key, const char *value,
			   const char *line, char **leftover);
static void _destroy_nodename(void *ptr);
static bool _is_valid_dir(char *file_name);
static int _parse_partitionname(void **dest, slurm_parser_enum_t type,
				const char *key, const char *value,
				const char *line, char **leftover);
static void _destroy_partitionname(void *ptr);
static int _parse_downnodes(void **dest, slurm_parser_enum_t type,
			    const char *key, const char *value,
			    const char *line, char **leftover);
static void _destroy_downnodes(void *ptr);
static int _defunct_option(void **dest, slurm_parser_enum_t type,
			   const char *key, const char *value,
			   const char *line, char **leftover);
static void _validate_and_set_defaults(slurm_ctl_conf_t *conf,
				       s_p_hashtbl_t *hashtbl);

s_p_options_t slurm_conf_options[] = {
	{"AccountingStorageEnforce", S_P_STRING},
	{"AccountingStorageHost", S_P_STRING},
	{"AccountingStorageBackupHost", S_P_STRING},
	{"AccountingStorageLoc", S_P_STRING},
	{"AccountingStoragePass", S_P_STRING},
	{"AccountingStoragePort", S_P_UINT32},
	{"AccountingStorageType", S_P_STRING},
	{"AccountingStorageUser", S_P_STRING},
	{"AuthType", S_P_STRING},
	{"BackupAddr", S_P_STRING},
	{"BackupController", S_P_STRING},
	{"BatchStartTimeout", S_P_UINT16},
	{"CheckpointType", S_P_STRING},
	{"CacheGroups", S_P_UINT16},
	{"ClusterName", S_P_STRING},
	{"CompleteWait", S_P_UINT16},
	{"ControlAddr", S_P_STRING},
	{"ControlMachine", S_P_STRING},
	{"CryptoType", S_P_STRING},
	{"DebugFlags", S_P_STRING},
	{"DefaultStorageHost", S_P_STRING},
	{"DefaultStorageLoc", S_P_STRING},
	{"DefaultStoragePass", S_P_STRING},
	{"DefaultStoragePort", S_P_UINT32},
	{"DefaultStorageType", S_P_STRING},
	{"DefaultStorageUser", S_P_STRING},
	{"DefMemPerCPU", S_P_UINT32},
	{"DefMemPerNode", S_P_UINT32},
	{"DisableRootJobs", S_P_BOOLEAN},
	{"EnforcePartLimits", S_P_BOOLEAN},
	{"Epilog", S_P_STRING},
	{"EpilogMsgTime", S_P_UINT32},
	{"EPilogSlurmctld", S_P_STRING},
	{"FastSchedule", S_P_UINT16},
	{"FirstJobId", S_P_UINT32},
	{"GetEnvTimeout", S_P_UINT16},
	{"HashBase", S_P_LONG, _defunct_option},
	{"HeartbeatInterval", S_P_LONG, _defunct_option},
	{"HealthCheckInterval", S_P_UINT16},
	{"HealthCheckProgram", S_P_STRING},
	{"InactiveLimit", S_P_UINT16},
	{"JobAcctGatherType", S_P_STRING},
	{"JobAcctFrequency", S_P_UINT16, _defunct_option},
	{"JobAcctGatherFrequency", S_P_UINT16},
	{"JobAcctLogFile", S_P_STRING},
	{"JobAcctType", S_P_STRING},
	{"JobCheckpointDir", S_P_STRING},
	{"JobCompHost", S_P_STRING},
	{"JobCompLoc", S_P_STRING},
	{"JobCompPass", S_P_STRING},
	{"JobCompPort", S_P_UINT32},
	{"JobCompType", S_P_STRING},
	{"JobCompUser", S_P_STRING},
	{"JobCredentialPrivateKey", S_P_STRING},
	{"JobCredentialPublicCertificate", S_P_STRING},
	{"JobFileAppend", S_P_UINT16},
	{"JobRequeue", S_P_UINT16},
	{"KillTree", S_P_UINT16, _defunct_option},
	{"KillOnBadExit", S_P_UINT16},
	{"KillWait", S_P_UINT16},
	{"Licenses", S_P_STRING},
	{"MailProg", S_P_STRING},
	{"MaxJobCount", S_P_UINT16},
	{"MaxMemPerCPU", S_P_UINT32},
	{"MaxMemPerNode", S_P_UINT32},
	{"MaxMemPerTask", S_P_UINT32},	/* defunct */
	{"MaxTasksPerNode", S_P_UINT16},
	{"MessageTimeout", S_P_UINT16},
	{"MinJobAge", S_P_UINT16},
	{"MpichGmDirectSupport", S_P_LONG, _defunct_option},
	{"MpiDefault", S_P_STRING},
	{"MpiParams", S_P_STRING},
	{"OverTimeLimit", S_P_UINT16},
	{"PluginDir", S_P_STRING},
	{"PlugStackConfig", S_P_STRING},
	{"PreemptMode", S_P_STRING},
	{"PriorityDecayHalfLife", S_P_STRING},
	{"PriorityFavorSmall", S_P_BOOLEAN},
	{"PriorityMaxAge", S_P_STRING},
	{"PriorityUsageResetPeriod", S_P_STRING},
	{"PriorityType", S_P_STRING},
	{"PriorityWeightAge", S_P_UINT32},
	{"PriorityWeightFairshare", S_P_UINT32},
	{"PriorityWeightJobSize", S_P_UINT32},
	{"PriorityWeightPartition", S_P_UINT32},
	{"PriorityWeightQOS", S_P_UINT32},
	{"PrivateData", S_P_STRING},
	{"ProctrackType", S_P_STRING},
	{"Prolog", S_P_STRING},
	{"PrologSlurmctld", S_P_STRING},
	{"PropagatePrioProcess", S_P_UINT16},
	{"PropagateResourceLimitsExcept", S_P_STRING},
	{"PropagateResourceLimits", S_P_STRING},
	{"ResumeProgram", S_P_STRING},
	{"ResumeRate", S_P_UINT16},
	{"ResumeTimeout", S_P_UINT16},
	{"ResvOverRun", S_P_UINT16},
	{"ReturnToService", S_P_UINT16},
	{"SallocDefaultCommand", S_P_STRING},
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
	{"SlurmctldDebug", S_P_UINT16},
	{"SlurmctldLogFile", S_P_STRING},
	{"SlurmctldPidFile", S_P_STRING},
	{"SlurmctldPort", S_P_UINT32},
	{"SlurmctldTimeout", S_P_UINT16},
	{"SlurmdDebug", S_P_UINT16},
	{"SlurmdLogFile", S_P_STRING},
	{"SlurmdPidFile",  S_P_STRING},
	{"SlurmdPort", S_P_UINT32},
	{"SlurmdSpoolDir", S_P_STRING},
	{"SlurmdTimeout", S_P_UINT16},
	{"SrunEpilog", S_P_STRING},
	{"SrunProlog", S_P_STRING},
	{"StateSaveLocation", S_P_STRING},
	{"SuspendExcNodes", S_P_STRING},
	{"SuspendExcParts", S_P_STRING},
	{"SuspendProgram", S_P_STRING},
	{"SuspendRate", S_P_UINT16},
	{"SuspendTime", S_P_LONG},
	{"SuspendTimeout", S_P_UINT16},
	{"SwitchType", S_P_STRING},
	{"TaskEpilog", S_P_STRING},
	{"TaskProlog", S_P_STRING},
	{"TaskPlugin", S_P_STRING},
	{"TaskPluginParam", S_P_STRING},
	{"TmpFS", S_P_STRING},
	{"TopologyPlugin", S_P_STRING},
	{"TrackWCKey", S_P_BOOLEAN},
	{"TreeWidth", S_P_UINT16},
	{"UnkillableStepProgram", S_P_STRING},
	{"UnkillableStepTimeout", S_P_UINT16},
	{"UsePAM", S_P_BOOLEAN},
	{"WaitTime", S_P_UINT16},

	{"NodeName", S_P_ARRAY, _parse_nodename, _destroy_nodename},
	{"PartitionName", S_P_ARRAY, _parse_partitionname,
	 _destroy_partitionname},
	{"DownNodes", S_P_ARRAY, _parse_downnodes, _destroy_downnodes},

	{NULL}
};

static bool _is_valid_dir(char *file_name)
{
	struct stat buf;

	if (stat(file_name, &buf) || !S_ISDIR(buf.st_mode))
		return false;
	return true;
}

static int _defunct_option(void **dest, slurm_parser_enum_t type,
			  const char *key, const char *value,
			  const char *line, char **leftover)
{
	error("The option \"%s\" is defunct, see man slurm.conf.", key);
	return 0;
}

#ifdef HAVE_3D
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
		if((nodenames[i-1] == '[') 
		   || (nodenames[i-1] <= '9'
		       && nodenames[i-1] >= '0'))
			break;
	}
	xfree(conf_ptr->node_prefix);
	if(nodenames[i] == '\0')
		conf_ptr->node_prefix = xstrdup(nodenames);
	else {
		tmp = xmalloc(sizeof(char)*i+1);
		memset(tmp, 0, i+1);
		snprintf(tmp, i, "%s", nodenames);
		conf_ptr->node_prefix = tmp;
		tmp = NULL;
	}
	debug3("Prefix is %s %s %d", conf_ptr->node_prefix, nodenames, i);
}
#endif /* HAVE_BG */


static int _parse_nodename(void **dest, slurm_parser_enum_t type,
			   const char *key, const char *value,
			   const char *line, char **leftover)
{
	s_p_hashtbl_t *tbl, *dflt;
	slurm_conf_node_t *n;
	int computed_procs;
	static s_p_options_t _nodename_options[] = {
		{"CoresPerSocket", S_P_UINT16},
		{"Feature", S_P_STRING},
		{"NodeAddr", S_P_STRING},
		{"NodeHostname", S_P_STRING},
		{"Port", S_P_UINT16},
		{"Procs", S_P_UINT16},
		{"RealMemory", S_P_UINT32},
		{"Reason", S_P_STRING},
		{"Sockets", S_P_UINT16},
		{"State", S_P_STRING},
		{"ThreadsPerCore", S_P_UINT16},
		{"TmpDisk", S_P_UINT32},
		{"Weight", S_P_UINT32},
		{NULL}
	};

	tbl = s_p_hashtbl_create(_nodename_options);
	s_p_parse_line(tbl, *leftover, leftover);
	/* s_p_dump_values(tbl, _nodename_options); */

	if (strcasecmp(value, "DEFAULT") == 0) {
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

		if (default_nodename_tbl != NULL)
			s_p_hashtbl_destroy(default_nodename_tbl);
		default_nodename_tbl = tbl;

		return 0;
	} else {
		bool no_cpus    = false;
		bool no_sockets = false;
		bool no_cores   = false;
		bool no_threads = false;

		n = xmalloc(sizeof(slurm_conf_node_t));
		dflt = default_nodename_tbl;

		n->nodenames = xstrdup(value);
#ifdef HAVE_3D
		if (conf_ptr->node_prefix == NULL)
			_set_node_prefix(n->nodenames);
#endif

		if (!s_p_get_string(&n->hostnames, "NodeHostname", tbl))
			n->hostnames = xstrdup(n->nodenames);
		if (!s_p_get_string(&n->addresses, "NodeAddr", tbl))
			n->addresses = xstrdup(n->hostnames);

		if (!s_p_get_uint16(&n->cores, "CoresPerSocket", tbl)
		    && !s_p_get_uint16(&n->cores, "CoresPerSocket", dflt)) {
			n->cores = 1;
			no_cores = true;
		}

		if (!s_p_get_string(&n->feature, "Feature", tbl))
			s_p_get_string(&n->feature, "Feature", dflt);

		if (!s_p_get_uint16(&n->port, "Port", tbl)
		    && !s_p_get_uint16(&n->port, "Port", dflt)) {
			/* This gets resolved in slurm_conf_get_port() 
			 * and slurm_conf_get_addr(). For now just 
			 * leave with a value of zero */
			n->port = 0;
		}

		if (!s_p_get_uint16(&n->cpus, "Procs", tbl)
		    && !s_p_get_uint16(&n->cpus, "Procs", dflt)) {
			n->cpus = 1;
			no_cpus = true;
		}

		if (!s_p_get_uint32(&n->real_memory, "RealMemory", tbl)
		    && !s_p_get_uint32(&n->real_memory, "RealMemory", dflt))
			n->real_memory = 1;

		if (!s_p_get_string(&n->reason, "Reason", tbl))
			s_p_get_string(&n->reason, "Reason", dflt);

		if (!s_p_get_uint16(&n->sockets, "Sockets", tbl)
		    && !s_p_get_uint16(&n->sockets, "Sockets", dflt)) {
			n->sockets = 1;
			no_sockets = true;
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

		if (!s_p_get_uint32(&n->weight, "Weight", tbl)
		    && !s_p_get_uint32(&n->weight, "Weight", dflt))
			n->weight = 1;

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
		 
		if (!no_cpus    &&	/* infer missing Sockets= */
		    no_sockets) {
			n->sockets = n->cpus / (n->cores * n->threads);
		}

		if (n->sockets == 0) {	/* make sure sockets is non-zero */
			error("NodeNames=%s Sockets=0 is invalid, "
			      "reset to 1", n->nodenames);
			n->sockets = 1;
		}

		if (no_cpus) {		/* infer missing Procs= */
			n->cpus = n->sockets * n->cores * n->threads;
		}

		/* if only Procs= and Sockets= specified check for match */
		if (!no_cpus    &&
		    !no_sockets &&
		    no_cores    &&
		    no_threads) {
			if (n->cpus != n->sockets) {
				n->sockets = n->cpus;
				error("NodeNames=%s Procs doesn't match "
				      "Sockets, setting Sockets to %d",
				      n->nodenames, n->sockets);
			}
		}

		computed_procs = n->sockets * n->cores * n->threads;
		if ((n->cpus != n->sockets) &&
		    (n->cpus != n->sockets * n->cores) &&
		    (n->cpus != computed_procs)) {
			error("NodeNames=%s Procs=%d doesn't match "
			      "Sockets*CoresPerSocket*ThreadsPerCore (%d), "
			      "resetting Procs",
			      n->nodenames, n->cpus, computed_procs);
			n->cpus = computed_procs;
		}

		*dest = (void *)n;

		return 1;
	}

	/* should not get here */
}

static void _destroy_nodename(void *ptr)
{
	slurm_conf_node_t *n = (slurm_conf_node_t *)ptr;
	xfree(n->nodenames);
	xfree(n->hostnames);
	xfree(n->addresses);
	xfree(n->feature);
	xfree(n->reason);
	xfree(n->state);
	xfree(ptr);
}

int slurm_conf_nodename_array(slurm_conf_node_t **ptr_array[])
{
	int count;
	slurm_conf_node_t **ptr;

	if (s_p_get_array((void ***)&ptr, &count, "NodeName", conf_hashtbl)) {
		*ptr_array = ptr;
		return count;
	} else {
		*ptr_array = NULL;
		return 0;
	}
}


static int _parse_partitionname(void **dest, slurm_parser_enum_t type,
			       const char *key, const char *value,
			       const char *line, char **leftover)
{
	s_p_hashtbl_t *tbl, *dflt;
	slurm_conf_partition_t *p;
	char *tmp = NULL;
	static s_p_options_t _partition_options[] = {
		{"AllowGroups", S_P_STRING},
		{"Default", S_P_BOOLEAN}, /* YES or NO */
		{"DefaultTime", S_P_STRING},
		{"DisableRootJobs", S_P_BOOLEAN}, /* YES or NO */
		{"Hidden", S_P_BOOLEAN}, /* YES or NO */
		{"MaxTime", S_P_STRING},
		{"MaxNodes", S_P_UINT32}, /* INFINITE or a number */
		{"MinNodes", S_P_UINT32},
		{"Nodes", S_P_STRING},
		{"Priority", S_P_UINT16},
		{"RootOnly", S_P_BOOLEAN}, /* YES or NO */
		{"Shared", S_P_STRING}, /* YES, NO, or FORCE */
		{"State", S_P_BOOLEAN}, /* UP or DOWN */
		{"AllocNodes", S_P_STRING},
		{NULL}
	};


	tbl = s_p_hashtbl_create(_partition_options);
	s_p_parse_line(tbl, *leftover, leftover);
	/* s_p_dump_values(tbl, _partition_options); */

	if (strcasecmp(value, "DEFAULT") == 0) {
		if (default_partition_tbl != NULL)
			s_p_hashtbl_destroy(default_partition_tbl);
		default_partition_tbl = tbl;

		return 0;
	} else {
		p = xmalloc(sizeof(slurm_conf_partition_t));
		dflt = default_partition_tbl;

		p->name = xstrdup(value);

		if (!s_p_get_string(&p->allow_groups, "AllowGroups", tbl))
			s_p_get_string(&p->allow_groups, "AllowGroups", dflt);
		if (p->allow_groups && strcasecmp(p->allow_groups, "ALL")==0) {
			xfree(p->allow_groups);
			p->allow_groups = NULL; /* NULL means allow all */
		}

		if (!s_p_get_string(&p->allow_alloc_nodes, "AllocNodes", tbl)) {
			s_p_get_string(&p->allow_alloc_nodes, "AllocNodes", 
				       dflt);
			if (p->allow_alloc_nodes && 
			    (strcasecmp(p->allow_alloc_nodes, "ALL") == 0)) {
				/* NULL means to allow all submit notes */
				xfree(p->allow_alloc_nodes);
			}
		}

		if (!s_p_get_boolean(&p->default_flag, "Default", tbl)
		    && !s_p_get_boolean(&p->default_flag, "Default", dflt))
			p->default_flag = false;

		if (!s_p_get_boolean((bool *)&p->disable_root_jobs,
				     "DisableRootJobs", tbl))
			p->disable_root_jobs = (uint16_t)NO_VAL;

		if (!s_p_get_boolean(&p->hidden_flag, "Hidden", tbl)
		    && !s_p_get_boolean(&p->hidden_flag, "Hidden", dflt))
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
			p->min_nodes = 1;

		if (!s_p_get_string(&p->nodes, "Nodes", tbl)
		    && !s_p_get_string(&p->nodes, "Nodes", dflt))
			p->nodes = NULL;
		else {
			int i;
			for (i=0; p->nodes[i]; i++) {
				if (isspace(p->nodes[i]))
					p->nodes[i] = ',';
			}
		}

		if (!s_p_get_boolean(&p->root_only_flag, "RootOnly", tbl)
		    && !s_p_get_boolean(&p->root_only_flag, "RootOnly", dflt))
			p->root_only_flag = false;

		if (!s_p_get_uint16(&p->priority, "Priority", tbl) &&
		    !s_p_get_uint16(&p->priority, "Priority", dflt))
			p->priority = 1;

		if (s_p_get_string(&tmp, "Shared", tbl) ||
		    s_p_get_string(&tmp, "Shared", dflt)) {
			if (strcasecmp(tmp, "NO") == 0)
				p->max_share = 1;
#ifndef HAVE_XCPU
			/* Only "Shared=NO" is valid on XCPU systems */
			else if (strcasecmp(tmp, "EXCLUSIVE") == 0)
				p->max_share = 0;
			else if (strncasecmp(tmp, "YES:", 4) == 0) {
				int i = strtol(&tmp[4], (char **) NULL, 10);
				if (i <= 1) {
					error("Ignoring bad Shared value: %s",
					      tmp);
					p->max_share = 1; /* Shared=NO */
				} else
					p->max_share = i;
			} else if (strcasecmp(tmp, "YES") == 0) 
				p->max_share = 4;
			else if (strncasecmp(tmp, "FORCE:", 6) == 0) {
				int i = strtol(&tmp[6], (char **) NULL, 10);
				if (i < 1) {
					error("Ignoring bad Shared value: %s",
					      tmp);
					p->max_share = 1; /* Shared=NO */
				} else
					p->max_share = i | SHARED_FORCE;
			} else if (strcasecmp(tmp, "FORCE") == 0)
				p->max_share = 4 | SHARED_FORCE;
#endif
			else {
				error("Bad value \"%s\" for Shared", tmp);
				_destroy_partitionname(p);
				s_p_hashtbl_destroy(tbl);
				xfree(tmp);
				return -1;
			}
		} else
			p->max_share = 1;

		xfree(tmp);

		if (!s_p_get_boolean(&p->state_up_flag, "State", tbl)
		    && !s_p_get_boolean(&p->state_up_flag, "State", dflt))
			p->state_up_flag = true;

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
	xfree(p->allow_groups);
	xfree(p->name);
	xfree(p->nodes);
	xfree(ptr);
}

int slurm_conf_partition_array(slurm_conf_partition_t **ptr_array[])
{
	int count;
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
	s_p_hashtbl_t *tbl, *dflt;
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
	dflt = default_nodename_tbl;

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
	int count;
	slurm_conf_downnodes_t **ptr;

	if (s_p_get_array((void ***)&ptr, &count, "DownNodes", conf_hashtbl)) {
		*ptr_array = ptr;
		return count;
	} else {
		*ptr_array = NULL;
		return 0;
	}
}

static void _free_name_hashtbl()
{
	int i;
	names_ll_t *p, *q;

	for (i=0; i<NAME_HASH_LEN; i++) {
		p = node_to_host_hashtbl[i];
		while (p) {
			xfree(p->alias);
			xfree(p->hostname);
			xfree(p->address);
			q = p->next_alias;
			xfree(p);
			p = q;
		}
		node_to_host_hashtbl[i] = NULL;
		host_to_node_hashtbl[i] = NULL;
	}
	nodehash_initialized = false;
}

static void _init_name_hashtbl()
{
	return;
}

static int _get_hash_idx(const char *s)
{
	int i;

	i = 0;
	while (*s) i += (int)*s++;
	return i % NAME_HASH_LEN;
}

static void _push_to_hashtbls(char *alias, char *hostname,
			      char *address, uint16_t port,
			      uint16_t cpus, uint16_t sockets,
			      uint16_t cores, uint16_t threads)
{
	int hostname_idx, alias_idx;
	names_ll_t *p, *new;

	alias_idx = _get_hash_idx(alias);
	hostname_idx = _get_hash_idx(hostname);

#if !defined(HAVE_FRONT_END) && !defined(MULTIPLE_SLURMD)
	/* Ensure only one slurmd configured on each host */
	p = host_to_node_hashtbl[hostname_idx];
	while (p) {
		if (strcmp(p->hostname, hostname)==0) {
			error("Duplicated NodeHostname %s in the config file",
			      hostname);
			return;
		}
		p = p->next_hostname;
	}
#endif
	/* Ensure only one instance of each NodeName */
	p = node_to_host_hashtbl[alias_idx];
	while (p) {
		if (strcmp(p->alias, alias)==0) {
			fatal("Duplicated NodeName %s in the config file",
			      p->alias);
			return;
		}
		p = p->next_alias;
	}

	/* Create the new data structure and link it into the hash tables */
	new = (names_ll_t *)xmalloc(sizeof(*new));
	new->alias	= xstrdup(alias);
	new->hostname	= xstrdup(hostname);
	new->address	= xstrdup(address);
	new->port	= port;
	new->cpus	= cpus;
	new->sockets	= sockets;
	new->cores	= cores;
	new->threads	= threads;
	new->addr_initialized = false;

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
	hostlist_t alias_list = NULL;
	hostlist_t hostname_list = NULL;
	hostlist_t address_list = NULL;
	char *alias = NULL;
	char *hostname = NULL;
	char *address = NULL;
	int error_code = SLURM_SUCCESS;

	if (node_ptr->nodenames == NULL || *node_ptr->nodenames == '\0')
		return -1;

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
	if ((address_list = hostlist_create(node_ptr->addresses)) == NULL) {
		error("Unable to create NodeAddr list from %s",
		      node_ptr->addresses);
		error_code = errno;
		goto cleanup;
	}

#ifdef HAVE_3D
	if (conf_ptr->node_prefix == NULL)
		_set_node_prefix(node_ptr->nodenames);
#endif

	/* some sanity checks */
#ifdef HAVE_FRONT_END
	if (hostlist_count(hostname_list) != 1
	    || hostlist_count(address_list) != 1) {
		error("Only one hostname and address allowed "
		      "in FRONT_END mode");
		goto cleanup;
	}

	hostname = node_ptr->hostnames;
	address = node_ptr->addresses;
#else
	if (hostlist_count(hostname_list) < hostlist_count(alias_list)) {
		error("At least as many NodeHostname are required "
		      "as NodeName");
		goto cleanup;
	}
	if (hostlist_count(address_list) < hostlist_count(alias_list)) {
		error("At least as many NodeAddr are required as NodeName");
		goto cleanup;
	}
#endif

	/* now build the individual node structures */
	while ((alias = hostlist_shift(alias_list))) {
#ifndef HAVE_FRONT_END
		hostname = hostlist_shift(hostname_list);
		address = hostlist_shift(address_list);
#endif

		_push_to_hashtbls(alias, hostname, address, node_ptr->port,
				  node_ptr->cpus, node_ptr->sockets,
				  node_ptr->cores, node_ptr->threads);

		free(alias);
#ifndef HAVE_FRONT_END
		free(hostname);
		free(address);
#endif
		
	}

	/* free allocated storage */
cleanup:
	if (alias_list)
		hostlist_destroy(alias_list);
	if (hostname_list)
		hostlist_destroy(hostname_list);
	if (address_list)
		hostlist_destroy(address_list);
	return error_code;
}

static void _init_slurmd_nodehash(void)
{
	slurm_conf_node_t **ptr_array;
	int count;
	int i;

	if (nodehash_initialized)
		return;
	else
		nodehash_initialized = true;

	if(!conf_initialized) {
		_init_slurm_conf(NULL);
		conf_initialized = true;
	}

	count = slurm_conf_nodename_array(&ptr_array);
	if (count == 0) {
		return;
	}

	for (i = 0; i < count; i++) {
		_register_conf_node_aliases(ptr_array[i]);
	}
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
		if (strcmp(p->alias, node_name) == 0) {
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
	int idx;
	names_ll_t *p;

	slurm_conf_lock();
	_init_slurmd_nodehash();
	idx = _get_hash_idx(node_hostname);

	p = host_to_node_hashtbl[idx];
	while (p) {
		if (strcmp(p->hostname, node_hostname) == 0) {
			char *alias = xstrdup(p->alias);
			slurm_conf_unlock();
			return alias;
		}
		p = p->next_hostname;
	}
	slurm_conf_unlock();

	return NULL;
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
		if (strcmp(p->hostname, node_hostname) == 0) {
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
		if (strcmp(p->alias, node_name) == 0) {
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
 * slurm_conf_get_addr - Return the slurm_addr for a given NodeName
 * Returns SLURM_SUCCESS on success, SLURM_FAILURE on failure.
 */
extern int slurm_conf_get_addr(const char *node_name, slurm_addr *address)
{
	int idx;
	names_ll_t *p;

	slurm_conf_lock();
	_init_slurmd_nodehash();

	idx = _get_hash_idx(node_name);
	p = node_to_host_hashtbl[idx];
	while (p) {
		if (strcmp(p->alias, node_name) == 0) {
			if (!p->port)
				p->port = (uint16_t) conf_ptr->slurmd_port;
			if (!p->addr_initialized) {
				slurm_set_addr(&p->addr, p->port, p->address);
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
 * slurm_conf_get_cpus_sct -
 * Return the cpus, sockets, cores, and threads for a given NodeName
 * Returns SLURM_SUCCESS on success, SLURM_FAILURE on failure.
 */
extern int slurm_conf_get_cpus_sct(const char *node_name,
			uint16_t *cpus, uint16_t *sockets,
			uint16_t *cores, uint16_t *threads)
{
	int idx;
	names_ll_t *p;

	slurm_conf_lock();
	_init_slurmd_nodehash();

	idx = _get_hash_idx(node_name);
	p = node_to_host_hashtbl[idx];
	while (p) {
		if (strcmp(p->alias, node_name) == 0) {
		    	if (cpus)
				*cpus    = p->cpus;
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
	xfree (ctl_conf_ptr->accounting_storage_backup_host);
	xfree (ctl_conf_ptr->accounting_storage_host);
	xfree (ctl_conf_ptr->accounting_storage_loc);
	xfree (ctl_conf_ptr->accounting_storage_pass);
	xfree (ctl_conf_ptr->accounting_storage_type);
	xfree (ctl_conf_ptr->accounting_storage_user);
	xfree (ctl_conf_ptr->authtype);
	xfree (ctl_conf_ptr->backup_addr);
	xfree (ctl_conf_ptr->backup_controller);
	xfree (ctl_conf_ptr->checkpoint_type);
	xfree (ctl_conf_ptr->cluster_name);
	xfree (ctl_conf_ptr->control_addr);
	xfree (ctl_conf_ptr->control_machine);
	xfree (ctl_conf_ptr->crypto_type);
	xfree (ctl_conf_ptr->epilog);
	xfree (ctl_conf_ptr->health_check_program);
	xfree (ctl_conf_ptr->job_acct_gather_type);
	xfree (ctl_conf_ptr->job_ckpt_dir);
	xfree (ctl_conf_ptr->job_comp_host);
	xfree (ctl_conf_ptr->job_comp_loc);
	xfree (ctl_conf_ptr->job_comp_pass);
	xfree (ctl_conf_ptr->job_comp_type);
	xfree (ctl_conf_ptr->job_comp_user);
	xfree (ctl_conf_ptr->job_credential_private_key);
	xfree (ctl_conf_ptr->job_credential_public_certificate);
	xfree (ctl_conf_ptr->licenses);
	xfree (ctl_conf_ptr->mail_prog);
	xfree (ctl_conf_ptr->mpi_default);
	xfree (ctl_conf_ptr->mpi_params);
	xfree (ctl_conf_ptr->node_prefix);
	xfree (ctl_conf_ptr->plugindir);
	xfree (ctl_conf_ptr->plugstack);
	xfree (ctl_conf_ptr->priority_type);
	xfree (ctl_conf_ptr->proctrack_type);
	xfree (ctl_conf_ptr->prolog);
	xfree (ctl_conf_ptr->propagate_rlimits_except);
	xfree (ctl_conf_ptr->propagate_rlimits);
	xfree (ctl_conf_ptr->resume_program);
	xfree (ctl_conf_ptr->salloc_default_command);
	xfree (ctl_conf_ptr->slurm_conf);
	xfree (ctl_conf_ptr->sched_params);
	xfree (ctl_conf_ptr->schedtype);
	xfree (ctl_conf_ptr->select_type);
	xfree (ctl_conf_ptr->slurm_user_name);
	xfree (ctl_conf_ptr->slurmctld_logfile);
	xfree (ctl_conf_ptr->slurmctld_pidfile);
	xfree (ctl_conf_ptr->slurmd_logfile);
	xfree (ctl_conf_ptr->slurmd_pidfile);
	xfree (ctl_conf_ptr->slurmd_spooldir);
	xfree (ctl_conf_ptr->slurmd_user_name);
	xfree (ctl_conf_ptr->srun_epilog);
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
	xfree (ctl_conf_ptr->topology_plugin);
	xfree (ctl_conf_ptr->unkillable_program);

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
	ctl_conf_ptr->last_update		= time(NULL);
	ctl_conf_ptr->cache_groups		= (uint16_t) NO_VAL;
	xfree (ctl_conf_ptr->accounting_storage_backup_host);
	xfree (ctl_conf_ptr->accounting_storage_host);
	xfree (ctl_conf_ptr->accounting_storage_loc);
	xfree (ctl_conf_ptr->accounting_storage_pass);
	ctl_conf_ptr->accounting_storage_port             = 0;
	xfree (ctl_conf_ptr->accounting_storage_type);
	xfree (ctl_conf_ptr->accounting_storage_user);
	xfree (ctl_conf_ptr->authtype);
	xfree (ctl_conf_ptr->backup_addr);
	xfree (ctl_conf_ptr->backup_controller);
	ctl_conf_ptr->batch_start_timeout	= 0;
	ctl_conf_ptr->cache_groups		= 0;
	xfree (ctl_conf_ptr->checkpoint_type);
	xfree (ctl_conf_ptr->cluster_name);
	ctl_conf_ptr->complete_wait		= (uint16_t) NO_VAL;
	xfree (ctl_conf_ptr->control_addr);
	xfree (ctl_conf_ptr->control_machine);
	xfree (ctl_conf_ptr->crypto_type);
	ctl_conf_ptr->def_mem_per_task          = 0;
	ctl_conf_ptr->debug_flags		= 0;
	ctl_conf_ptr->disable_root_jobs         = 0;
	ctl_conf_ptr->enforce_part_limits       = 0;
	xfree (ctl_conf_ptr->epilog);
	ctl_conf_ptr->epilog_msg_time		= (uint32_t) NO_VAL;
	ctl_conf_ptr->fast_schedule		= (uint16_t) NO_VAL;
	ctl_conf_ptr->first_job_id		= (uint32_t) NO_VAL;
	ctl_conf_ptr->get_env_timeout		= 0;
	ctl_conf_ptr->health_check_interval	= 0;
	xfree(ctl_conf_ptr->health_check_program);
	ctl_conf_ptr->inactive_limit		= (uint16_t) NO_VAL;
	xfree (ctl_conf_ptr->job_acct_gather_type);
	ctl_conf_ptr->job_acct_gather_freq             = 0;
	xfree (ctl_conf_ptr->job_ckpt_dir);
	xfree (ctl_conf_ptr->job_comp_loc);
	xfree (ctl_conf_ptr->job_comp_pass);
	ctl_conf_ptr->job_comp_port             = 0;
	xfree (ctl_conf_ptr->job_comp_type);
	xfree (ctl_conf_ptr->job_comp_user);
	xfree (ctl_conf_ptr->job_credential_private_key);
	xfree (ctl_conf_ptr->job_credential_public_certificate);
	ctl_conf_ptr->job_file_append		= (uint16_t) NO_VAL;
	ctl_conf_ptr->job_requeue		= (uint16_t) NO_VAL;
	ctl_conf_ptr->kill_wait			= (uint16_t) NO_VAL;
	xfree (ctl_conf_ptr->licenses);
	xfree (ctl_conf_ptr->mail_prog);
	ctl_conf_ptr->max_job_cnt		= (uint16_t) NO_VAL;
	ctl_conf_ptr->max_mem_per_task          = 0;
	ctl_conf_ptr->min_job_age		= (uint16_t) NO_VAL;
	xfree (ctl_conf_ptr->mpi_default);
	xfree (ctl_conf_ptr->mpi_params);
	ctl_conf_ptr->msg_timeout		= (uint16_t) NO_VAL;
	ctl_conf_ptr->next_job_id		= (uint32_t) NO_VAL;
	xfree (ctl_conf_ptr->node_prefix);
	ctl_conf_ptr->over_time_limit           = 0;
	xfree (ctl_conf_ptr->plugindir);
	xfree (ctl_conf_ptr->plugstack);
	ctl_conf_ptr->preempt_mode              = 0;
	ctl_conf_ptr->private_data              = 0;
	xfree (ctl_conf_ptr->proctrack_type);
	xfree (ctl_conf_ptr->prolog);
	ctl_conf_ptr->propagate_prio_process	= (uint16_t) NO_VAL;
	xfree (ctl_conf_ptr->propagate_rlimits);
	xfree (ctl_conf_ptr->propagate_rlimits_except);
	ctl_conf_ptr->resume_timeout		= 0;
	xfree (ctl_conf_ptr->resume_program);
	ctl_conf_ptr->resume_rate		= (uint16_t) NO_VAL;
	ctl_conf_ptr->resv_over_run		= 0;
	ctl_conf_ptr->ret2service		= (uint16_t) NO_VAL;
	xfree( ctl_conf_ptr->salloc_default_command);
	xfree( ctl_conf_ptr->sched_params );
	ctl_conf_ptr->sched_time_slice		= (uint16_t) NO_VAL;
	xfree( ctl_conf_ptr->schedtype );
	ctl_conf_ptr->schedport			= (uint16_t) NO_VAL;
	ctl_conf_ptr->schedrootfltr		= (uint16_t) NO_VAL;
	xfree( ctl_conf_ptr->select_type );
	ctl_conf_ptr->select_type_param         = (uint16_t) NO_VAL;
	ctl_conf_ptr->slurm_user_id		= (uint16_t) NO_VAL; 
	xfree (ctl_conf_ptr->slurm_user_name);
	ctl_conf_ptr->slurmd_user_id		= (uint16_t) NO_VAL; 
	xfree (ctl_conf_ptr->slurmd_user_name);
	ctl_conf_ptr->slurmctld_debug		= (uint16_t) NO_VAL; 
	xfree (ctl_conf_ptr->slurmctld_logfile);
	xfree (ctl_conf_ptr->slurmctld_pidfile);
	ctl_conf_ptr->slurmctld_port		= (uint32_t) NO_VAL;
	ctl_conf_ptr->slurmctld_timeout		= (uint16_t) NO_VAL;
	ctl_conf_ptr->slurmd_debug		= (uint16_t) NO_VAL; 
	xfree (ctl_conf_ptr->slurmd_logfile);
	xfree (ctl_conf_ptr->slurmd_pidfile);
 	ctl_conf_ptr->slurmd_port		= (uint32_t) NO_VAL;
	xfree (ctl_conf_ptr->slurmd_spooldir);
	ctl_conf_ptr->slurmd_timeout		= (uint16_t) NO_VAL;
	xfree (ctl_conf_ptr->srun_prolog);
	xfree (ctl_conf_ptr->srun_epilog);
	xfree (ctl_conf_ptr->state_save_location);
	xfree (ctl_conf_ptr->suspend_exc_nodes);
	xfree (ctl_conf_ptr->suspend_exc_parts);
	xfree (ctl_conf_ptr->suspend_program);
	ctl_conf_ptr->suspend_rate		= (uint16_t) NO_VAL;
	ctl_conf_ptr->suspend_time		= (uint16_t) NO_VAL;
	ctl_conf_ptr->suspend_timeout		= 0;
	xfree (ctl_conf_ptr->switch_type);
	xfree (ctl_conf_ptr->task_epilog);
	xfree (ctl_conf_ptr->task_plugin);
	ctl_conf_ptr->task_plugin_param		= 0;
	xfree (ctl_conf_ptr->task_prolog);
	xfree (ctl_conf_ptr->tmp_fs);
	xfree (ctl_conf_ptr->topology_plugin);
	ctl_conf_ptr->tree_width       		= (uint16_t) NO_VAL;
	xfree (ctl_conf_ptr->unkillable_program);
	ctl_conf_ptr->unkillable_timeout        = (uint16_t) NO_VAL;
	ctl_conf_ptr->use_pam			= 0;
	ctl_conf_ptr->wait_time			= (uint16_t) NO_VAL;
	ctl_conf_ptr->kill_on_bad_exit	= 0;

	_free_name_hashtbl();
	_init_name_hashtbl();

	return;
}

/* caller must lock conf_lock */
static void _init_slurm_conf(const char *file_name)
{
	char *name = (char *)file_name;
	/* conf_ptr = (slurm_ctl_conf_t *)xmalloc(sizeof(slurm_ctl_conf_t)); */

	if (name == NULL) {
		name = getenv("SLURM_CONF");
		if (name == NULL)
			name = default_slurm_config_file;
	}
       	if(conf_initialized) {
		error("the conf_hashtbl is already inited");	
	}
	conf_hashtbl = s_p_hashtbl_create(slurm_conf_options);
	conf_ptr->last_update = time(NULL);
	if(s_p_parse_file(conf_hashtbl, name) == SLURM_ERROR)
		fatal("something wrong with opening/reading conf file");
	/* s_p_dump_values(conf_hashtbl, slurm_conf_options); */
	_validate_and_set_defaults(conf_ptr, conf_hashtbl);
	conf_ptr->slurm_conf = xstrdup(name);
}

/* caller must lock conf_lock */
static void
_destroy_slurm_conf()
{
	s_p_hashtbl_destroy(conf_hashtbl);
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
	pthread_mutex_lock(&conf_lock);

	if (conf_initialized) {
		pthread_mutex_unlock(&conf_lock);
		return SLURM_ERROR;
	}

	_init_slurm_conf(file_name);
	conf_initialized = true;

	pthread_mutex_unlock(&conf_lock);
	return SLURM_SUCCESS;
}

static int _internal_reinit(const char *file_name)
{
	char *name = (char *)file_name;

	if (name == NULL) {
		name = getenv("SLURM_CONF");
		if (name == NULL)
			name = default_slurm_config_file;
	}

	if (conf_initialized) {
		/* could check modified time on slurm.conf here */
		_destroy_slurm_conf();
	}
	
	_init_slurm_conf(name);
	
	conf_initialized = true;

	return SLURM_SUCCESS;
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

	pthread_mutex_lock(&conf_lock);
	rc = _internal_reinit(file_name);
	pthread_mutex_unlock(&conf_lock);

	return rc;
}

extern void
slurm_conf_mutex_init(void)
{
	pthread_mutex_init(&conf_lock, NULL);
}

extern void 
slurm_conf_install_fork_handlers()
{
	int err;
	if ((err = pthread_atfork(NULL, NULL, &slurm_conf_mutex_init)))
		fatal("can't install slurm_conf atfork handler");
	return;
}

extern int
slurm_conf_destroy(void)
{
	pthread_mutex_lock(&conf_lock);

	if (!conf_initialized) {
		pthread_mutex_unlock(&conf_lock);
		return SLURM_SUCCESS;
	}

	_destroy_slurm_conf();

	pthread_mutex_unlock(&conf_lock);

	return SLURM_SUCCESS;
}

extern slurm_ctl_conf_t *
slurm_conf_lock(void)
{
	pthread_mutex_lock(&conf_lock);

	if (!conf_initialized) {
		_init_slurm_conf(NULL);
		conf_initialized = true;
	}

	return conf_ptr;
}

extern void
slurm_conf_unlock(void)
{
	pthread_mutex_unlock(&conf_lock);
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

/* 
 *
 * IN/OUT ctl_conf_ptr - a configuration as loaded by read_slurm_conf_ctl
 *
 * NOTE: a backup_controller or control_machine of "localhost" are over-written
 *	with this machine's name.
 * NOTE: if backup_addr is NULL, it is over-written by backup_controller
 * NOTE: if control_addr is NULL, it is over-written by control_machine
 */
static void
_validate_and_set_defaults(slurm_ctl_conf_t *conf, s_p_hashtbl_t *hashtbl)
{
	char *temp_str = NULL;
	long long_suspend_time;
	bool truth;
	char *default_storage_type = NULL, *default_storage_host = NULL;
	char *default_storage_user = NULL, *default_storage_pass = NULL;
	char *default_storage_loc = NULL;
	uint32_t default_storage_port = 0;
		
	if (s_p_get_string(&conf->backup_controller, "BackupController",
			   hashtbl)
	    && strcasecmp("localhost", conf->backup_controller) == 0) {
		xfree(conf->backup_controller);
		conf->backup_controller = xmalloc (MAX_SLURM_NAME);
		if (gethostname_short(conf->backup_controller, MAX_SLURM_NAME))
			fatal("getnodename: %m");
	}
	if (s_p_get_string(&conf->backup_addr, "BackupAddr", hashtbl)) {
		if (conf->backup_controller == NULL) {
			error("BackupAddr specified without BackupController");
			xfree(conf->backup_addr);
		}
	} else {
		if (conf->backup_controller != NULL)
			conf->backup_addr = xstrdup(conf->backup_controller);
	}

	if (!s_p_get_uint16(&conf->batch_start_timeout, "BatchStartTimeout", 
			    hashtbl))
		conf->batch_start_timeout = DEFAULT_BATCH_START_TIMEOUT;

	s_p_get_string(&conf->cluster_name, "ClusterName", hashtbl);

	if (!s_p_get_uint16(&conf->complete_wait, "CompleteWait", hashtbl))
		conf->complete_wait = DEFAULT_COMPLETE_WAIT;

	if (!s_p_get_string(&conf->control_machine, "ControlMachine", hashtbl))
		fatal ("ControlMachine not specified.");
	else if (strcasecmp("localhost", conf->control_machine) == 0) {
		xfree (conf->control_machine);
		conf->control_machine = xmalloc(MAX_SLURM_NAME);
		if (gethostname_short(conf->control_machine, MAX_SLURM_NAME))
			fatal("getnodename: %m");
	}

	if (!s_p_get_string(&conf->control_addr, "ControlAddr", hashtbl) &&
	    (conf->control_machine != NULL)) {
		if (strchr(conf->control_machine, ',')) {
			fatal("ControlMachine has multiple host names so "
			      "ControlAddr must be specified");
		}
		conf->control_addr = xstrdup (conf->control_machine);
	}

	if ((conf->backup_controller != NULL) &&
	    (strcmp(conf->backup_controller, conf->control_machine) == 0)) {
		error("ControlMachine and BackupController identical");
		xfree(conf->backup_addr);
		xfree(conf->backup_controller);
	}

	s_p_get_string(&default_storage_type, "DefaultStorageType", hashtbl);
	s_p_get_string(&default_storage_host, "DefaultStorageHost", hashtbl);
	s_p_get_string(&default_storage_user, "DefaultStorageUser", hashtbl);
	s_p_get_string(&default_storage_pass, "DefaultStoragePass", hashtbl);
	s_p_get_string(&default_storage_loc,  "DefaultStorageLoc",  hashtbl);
	s_p_get_uint32(&default_storage_port, "DefaultStoragePort", hashtbl);
	s_p_get_string(&conf->job_credential_private_key,
		       "JobCredentialPrivateKey", hashtbl);
	s_p_get_string(&conf->job_credential_public_certificate,
		      "JobCredentialPublicCertificate", hashtbl);

	if (s_p_get_uint16(&conf->max_job_cnt, "MaxJobCount", hashtbl) &&
	    (conf->max_job_cnt < 1))
		fatal("MaxJobCount=%u, No jobs permitted", conf->max_job_cnt);

	if (!s_p_get_string(&conf->authtype, "AuthType", hashtbl))
		conf->authtype = xstrdup(DEFAULT_AUTH_TYPE);

	if (!s_p_get_uint16(&conf->cache_groups, "CacheGroups", hashtbl))
		conf->cache_groups = DEFAULT_CACHE_GROUPS;

	if (!s_p_get_string(&conf->checkpoint_type, "CheckpointType", hashtbl))
		conf->checkpoint_type = xstrdup(DEFAULT_CHECKPOINT_TYPE);

	if (!s_p_get_string(&conf->crypto_type, "CryptoType", hashtbl))
		 conf->crypto_type = xstrdup(DEFAULT_CRYPTO_TYPE);
	if ((strcmp(conf->crypto_type, "crypto/openssl") == 0) &&
	    ((conf->job_credential_private_key == NULL) ||
	     (conf->job_credential_public_certificate == NULL))) {
		fatal("CryptoType=crypto/openssl requires that both "
		      "JobCredentialPrivateKey and "
		      "JobCredentialPublicCertificate be set");
	}

	if (s_p_get_uint32(&conf->def_mem_per_task, "DefMemPerCPU", hashtbl))
		conf->def_mem_per_task |= MEM_PER_CPU;
	else if (!s_p_get_uint32(&conf->def_mem_per_task, "DefMemPerNode", 
				 hashtbl))
		conf->def_mem_per_task = DEFAULT_MEM_PER_CPU;

	if (s_p_get_string(&temp_str, "DebugFlags", hashtbl)) {
		conf->debug_flags = debug_str2flags(temp_str);
		if (conf->debug_flags == NO_VAL)
			fatal("DebugFlags invalid: %s", temp_str);
		xfree(temp_str);
	} else	/* Default: no DebugFlags */
		conf->debug_flags = 0;

	if (!s_p_get_boolean((bool *) &conf->disable_root_jobs, 
			     "DisableRootJobs", hashtbl))
		conf->disable_root_jobs = DEFAULT_DISABLE_ROOT_JOBS;

	if (!s_p_get_boolean((bool *) &conf->enforce_part_limits, 
			     "EnforcePartLimits", hashtbl))
		conf->enforce_part_limits = DEFAULT_ENFORCE_PART_LIMITS;

	s_p_get_string(&conf->epilog, "Epilog", hashtbl);

	if (!s_p_get_uint32(&conf->epilog_msg_time, "EpilogMsgTime", hashtbl))
		conf->epilog_msg_time = DEFAULT_EPILOG_MSG_TIME;

	s_p_get_string(&conf->epilog_slurmctld, "EpilogSlurmctld", hashtbl);

	if (!s_p_get_uint16(&conf->fast_schedule, "FastSchedule", hashtbl))
		conf->fast_schedule = DEFAULT_FAST_SCHEDULE;

	if (!s_p_get_uint32(&conf->first_job_id, "FirstJobId", hashtbl))
		conf->first_job_id = DEFAULT_FIRST_JOB_ID;

	if (s_p_get_uint16(&conf->inactive_limit, "InactiveLimit", hashtbl)) {
#ifdef HAVE_BG
		/* Inactive limit must be zero on Blue Gene */
		if (conf->inactive_limit) {
			error("InactiveLimit=%ld is invalid on Blue Gene",
				conf->inactive_limit);
		}
		conf->inactive_limit = 0;
#endif
	} else {
#ifdef HAVE_BG
		conf->inactive_limit = 0;
#endif
		conf->inactive_limit = DEFAULT_INACTIVE_LIMIT;
	}

	if (!s_p_get_uint16(&conf->job_acct_gather_freq,
			    "JobAcctGatherFrequency", hashtbl))
		conf->job_acct_gather_freq = DEFAULT_JOB_ACCT_GATHER_FREQ;

	if (s_p_get_string(&conf->job_acct_gather_type,
			    "JobAcctType", hashtbl)) {
		fatal("JobAcctType is no longer a valid parameter.\n"
		      "The job accounting plugin has changed to 2 different "
		      "plugins one for gathering and one for storing the "
		      "gathered information.\n"
		      "Please change this to JobAcctGatherType to "
		      "correctly work.\n"
		      "The major 'jobacct' is now 'jobacct_gather' and "
		      "'jobacct_storage' your declarations will also need "
		      "to change in your slurm.conf file.\n"
		      "Refer to the slurm.conf man page or the web "
		      "documentation for further explanation.");
	}
	
	if(!s_p_get_string(&conf->job_acct_gather_type,
			   "JobAcctGatherType", hashtbl))
		conf->job_acct_gather_type =
			xstrdup(DEFAULT_JOB_ACCT_GATHER_TYPE);

	if (!s_p_get_string(&conf->job_ckpt_dir, "JobCheckpointDir", hashtbl))
		conf->job_ckpt_dir = xstrdup(DEFAULT_JOB_CKPT_DIR);

	if (!s_p_get_string(&conf->job_comp_type, "JobCompType", hashtbl)) {
		if(default_storage_type) {
			if(!strcasecmp("slurmdbd", default_storage_type)) {
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
		if(default_storage_loc)
			conf->job_comp_loc = xstrdup(default_storage_loc);
		else if(!strcmp(conf->job_comp_type, "job_comp/mysql")
			|| !strcmp(conf->job_comp_type, "job_comp/pgsql")) 
			conf->job_comp_loc = xstrdup(DEFAULT_JOB_COMP_DB);
		else
			conf->job_comp_loc = xstrdup(DEFAULT_JOB_COMP_LOC);
	}

	if (!s_p_get_string(&conf->job_comp_host, "JobCompHost",
			    hashtbl)) {
		if(default_storage_host)
			conf->job_comp_host = xstrdup(default_storage_host);
		else
			conf->job_comp_host = xstrdup(DEFAULT_STORAGE_HOST);
	}
	if (!s_p_get_string(&conf->job_comp_user, "JobCompUser",
			    hashtbl)) {
		if(default_storage_user)
			conf->job_comp_user = xstrdup(default_storage_user);
		else
			conf->job_comp_user = xstrdup(DEFAULT_STORAGE_USER);
	}
	if (!s_p_get_string(&conf->job_comp_pass, "JobCompPass",
			    hashtbl)) {
		if(default_storage_pass)
			conf->job_comp_pass = xstrdup(default_storage_pass);
	}
	if (!s_p_get_uint32(&conf->job_comp_port, "JobCompPort",
			    hashtbl)) {
		if(default_storage_port)
			conf->job_comp_port = default_storage_port;
		else if(!strcmp(conf->job_comp_type, "job_comp/mysql")) 
			conf->job_comp_port = DEFAULT_MYSQL_PORT;
		else if(!strcmp(conf->job_comp_type, "job_comp/pgsql")) 
			conf->job_comp_port = DEFAULT_PGSQL_PORT;
		else 
			conf->job_comp_port = DEFAULT_STORAGE_PORT;
	}

	if (!s_p_get_uint16(&conf->job_file_append, "JobFileAppend", hashtbl))
		conf->job_file_append = 0;

	if (!s_p_get_uint16(&conf->job_requeue, "JobRequeue", hashtbl))
		conf->job_requeue = 1;
	else if (conf->job_requeue > 1)
		conf->job_requeue = 1;

	if (!s_p_get_uint16(&conf->get_env_timeout, "GetEnvTimeout", hashtbl))
		conf->get_env_timeout = DEFAULT_GET_ENV_TIMEOUT;

	s_p_get_uint16(&conf->health_check_interval, "HealthCheckInterval", 
		       hashtbl);
	s_p_get_string(&conf->health_check_program, "HealthCheckProgram", 
		       hashtbl);

	if (!s_p_get_uint16(&conf->kill_on_bad_exit, "KillOnBadExit", hashtbl))
		conf->kill_on_bad_exit = DEFAULT_KILL_ON_BAD_EXIT;

	if (!s_p_get_uint16(&conf->kill_wait, "KillWait", hashtbl))
		conf->kill_wait = DEFAULT_KILL_WAIT;

	s_p_get_string(&conf->licenses, "Licenses", hashtbl);

	if (!s_p_get_string(&conf->mail_prog, "MailProg", hashtbl))
		conf->mail_prog = xstrdup(DEFAULT_MAIL_PROG);

	if (!s_p_get_uint16(&conf->max_job_cnt, "MaxJobCount", hashtbl))
		conf->max_job_cnt = DEFAULT_MAX_JOB_COUNT;

	if ((s_p_get_uint32(&conf->max_mem_per_task, 
			    "MaxMemPerCPU", hashtbl)) ||
	    (s_p_get_uint32(&conf->max_mem_per_task, 
			    "MaxMemPerTask", hashtbl))) {
		conf->max_mem_per_task |= MEM_PER_CPU;
	} else if (!s_p_get_uint32(&conf->max_mem_per_task, 
				 "MaxMemPerNode", hashtbl)) {
		conf->max_mem_per_task = DEFAULT_MAX_MEM_PER_CPU;
	}

	if (!s_p_get_uint16(&conf->max_tasks_per_node, "MaxTasksPerNode", 
			    hashtbl)) {
		conf->max_tasks_per_node = DEFAULT_MAX_TASKS_PER_NODE;
	}

	if (!s_p_get_uint16(&conf->msg_timeout, "MessageTimeout", hashtbl))
		conf->msg_timeout = DEFAULT_MSG_TIMEOUT;
	else if (conf->msg_timeout > 100) {
		info("WARNING: MessageTimeout is too high for effective "
			"fault-tolerance");
	}

	if (!s_p_get_uint16(&conf->min_job_age, "MinJobAge", hashtbl))
		conf->min_job_age = DEFAULT_MIN_JOB_AGE;

	if (!s_p_get_string(&conf->mpi_default, "MpiDefault", hashtbl))
		conf->mpi_default = xstrdup(DEFAULT_MPI_DEFAULT);

	s_p_get_string(&conf->mpi_params, "MpiParams", hashtbl);

	if(!s_p_get_boolean((bool *)&conf->track_wckey, 
			    "TrackWCKey", hashtbl))
		conf->track_wckey = false;

	if (!s_p_get_string(&conf->accounting_storage_type,
			    "AccountingStorageType", hashtbl)) {
		if(default_storage_type)
			conf->accounting_storage_type =
				xstrdup_printf("accounting_storage/%s",
					       default_storage_type);
		else	
			conf->accounting_storage_type =
				xstrdup(DEFAULT_ACCOUNTING_STORAGE_TYPE);
	}

	if (s_p_get_string(&temp_str, "AccountingStorageEnforce", hashtbl)) {
		if (strstr(temp_str, "1") || strstr(temp_str, "associations"))
			conf->accounting_storage_enforce 
				|= ACCOUNTING_ENFORCE_ASSOCS;
		if (strstr(temp_str, "2") || strstr(temp_str, "limits")) {
			conf->accounting_storage_enforce 
				|= ACCOUNTING_ENFORCE_ASSOCS;
			conf->accounting_storage_enforce 
				|= ACCOUNTING_ENFORCE_LIMITS;
		}
		if (strstr(temp_str, "wckeys")) {
			conf->accounting_storage_enforce 
				|= ACCOUNTING_ENFORCE_ASSOCS;
			conf->accounting_storage_enforce 
				|= ACCOUNTING_ENFORCE_WCKEYS;
			conf->track_wckey = true;
		}		
		if (strstr(temp_str, "all")) {
			conf->accounting_storage_enforce = 0xffff;
			conf->track_wckey = true;
		}		
			
		xfree(temp_str);
	}

	/* if no backup we don't care */
	s_p_get_string(&conf->accounting_storage_backup_host,
		       "AccountingStorageBackupHost", hashtbl);
	
	if (!s_p_get_string(&conf->accounting_storage_host,
			    "AccountingStorageHost", hashtbl)) {
		if(default_storage_host)
			conf->accounting_storage_host =
				xstrdup(default_storage_host);
		else
			conf->accounting_storage_host =
				xstrdup(DEFAULT_STORAGE_HOST);
	}

	/* AccountingStorageLoc replaces JobAcctLogFile since it now represents
	 * the database name also depending on the storage type you
	 * use so we still check JobAcctLogFile for the same thing
	 */
	if (!s_p_get_string(&conf->accounting_storage_loc,
			    "AccountingStorageLoc", hashtbl)
		&& !s_p_get_string(&conf->accounting_storage_loc,
			       "JobAcctLogFile", hashtbl)) {
		if(default_storage_loc)
			conf->accounting_storage_loc =
				xstrdup(default_storage_loc);
		else if(!strcmp(conf->accounting_storage_type, 
				"accounting_storage/mysql")
			|| !strcmp(conf->accounting_storage_type, 
				"accounting_storage/pgsql")) 
			conf->accounting_storage_loc =
				xstrdup(DEFAULT_ACCOUNTING_DB);
		else
			conf->accounting_storage_loc =
				xstrdup(DEFAULT_STORAGE_LOC);
	}
	if (!s_p_get_string(&conf->accounting_storage_user,
			    "AccountingStorageUser", hashtbl)) {
		if(default_storage_user)
			conf->accounting_storage_user =
				xstrdup(default_storage_user);
		else
			conf->accounting_storage_user =
				xstrdup(DEFAULT_STORAGE_USER);
	}
	if (!s_p_get_string(&conf->accounting_storage_pass,
			    "AccountingStoragePass", hashtbl)) {
		if(default_storage_pass)
			conf->accounting_storage_pass =
				xstrdup(default_storage_pass);
	}
	if (!s_p_get_uint32(&conf->accounting_storage_port,
			    "AccountingStoragePort", hashtbl)) {
		if(default_storage_port)
			conf->accounting_storage_port = default_storage_port;
		else if(!strcmp(conf->accounting_storage_type,
				"accounting_storage/slurmdbd")) 
			conf->accounting_storage_port = SLURMDBD_PORT;
		else if(!strcmp(conf->accounting_storage_type, 
			  "accounting_storage/mysql")) 
			conf->accounting_storage_port = DEFAULT_MYSQL_PORT;
		else if(!strcmp(conf->accounting_storage_type,
			  "accounting_storage/pgsql")) 
			conf->accounting_storage_port = DEFAULT_PGSQL_PORT;
		else
			conf->accounting_storage_port = DEFAULT_STORAGE_PORT;
	}
	
	/* remove the user and loc if using slurmdbd */
	if(!strcmp(conf->accounting_storage_type,
		   "accounting_storage/slurmdbd")) {
		xfree(conf->accounting_storage_loc);
		conf->accounting_storage_loc = xstrdup("N/A");
		xfree(conf->accounting_storage_user);
		conf->accounting_storage_user = xstrdup("N/A");
	}

	s_p_get_uint16(&conf->over_time_limit, "OverTimeLimit", hashtbl);

	if (!s_p_get_string(&conf->plugindir, "PluginDir", hashtbl))
		conf->plugindir = xstrdup(default_plugin_path);
	if (!_is_valid_dir(conf->plugindir))
		fatal("Bad value \"%s\" for PluginDir", conf->plugindir);

	if (!s_p_get_string(&conf->plugstack, "PlugStackConfig", hashtbl))
		conf->plugstack = xstrdup(default_plugstack);

	if (s_p_get_string(&temp_str, "PreemptMode", hashtbl)) {
		int preempt_modes = 0;
		char *last = NULL, *tok;
		conf->preempt_mode = 0;
		tok = strtok_r(temp_str, ",", &last);
		while (tok) {
			if (strcasecmp(tok, "gang") == 0) {
				conf->preempt_mode |= PREEMPT_MODE_GANG;
			} else if (strcasecmp(tok, "off") == 0) {
				conf->preempt_mode += PREEMPT_MODE_OFF;
				preempt_modes++;
			} else if (strcasecmp(tok, "cancel") == 0) {
				conf->preempt_mode += PREEMPT_MODE_CANCEL;
				preempt_modes++;
			} else if (strcasecmp(tok, "checkpoint") == 0) {
				conf->preempt_mode += PREEMPT_MODE_CHECKPOINT;
				preempt_modes++;
			} else if (strcasecmp(tok, "requeue") == 0) {
				conf->preempt_mode += PREEMPT_MODE_REQUEUE;
				preempt_modes++;
			} else if ((strcasecmp(tok, "on") == 0) ||
				 (strcasecmp(tok, "suspend") == 0)) {
				conf->preempt_mode += PREEMPT_MODE_SUSPEND;
				preempt_modes++;
			} else
				fatal("Invalid PreemptMode: %s", tok);
			tok = strtok_r(NULL, ",", &last);
		}
		xfree(temp_str);
		if (preempt_modes > 1)
			fatal("More than one PreemptMode specified");
	}

	if (s_p_get_string(&temp_str, "PriorityDecayHalfLife", hashtbl)) {
		int max_time = time_str2mins(temp_str);
		if ((max_time < 0) && (max_time != INFINITE)) {
			fatal("Bad value \"%s\" for PriorityDecayHalfLife",
			      temp_str);
		}
		conf->priority_decay_hl = max_time * 60;
		xfree(temp_str);
	} else 
		conf->priority_decay_hl = DEFAULT_PRIORITY_DECAY;

	if (s_p_get_boolean(&truth, "PriorityFavorSmall", hashtbl) && truth) 
		conf->priority_favor_small = 1;
	else 
		conf->priority_favor_small = 0;
	
	if (s_p_get_string(&temp_str, "PriorityMaxAge", hashtbl)) {
		int max_time = time_str2mins(temp_str);
		if ((max_time < 0) && (max_time != INFINITE)) {
			fatal("Bad value \"%s\" for PriorityMaxAge",
			      temp_str);
		}
		conf->priority_max_age = max_time * 60;
		xfree(temp_str);
	} else 
		conf->priority_max_age = DEFAULT_PRIORITY_DECAY;

	if (s_p_get_string(&temp_str, "PriorityUsageResetPeriod", hashtbl)) {
		if (strcasecmp(temp_str, "none") == 0)
			conf->priority_reset_period = PRIORITY_RESET_NONE;
		else if (strcasecmp(temp_str, "now") == 0)
			conf->priority_reset_period = PRIORITY_RESET_NOW;
		else if (strcasecmp(temp_str, "daily") == 0)
			conf->priority_reset_period = PRIORITY_RESET_DAILY;
		else if (strcasecmp(temp_str, "weekly") == 0)
			conf->priority_reset_period = PRIORITY_RESET_WEEKLY;
		else if (strcasecmp(temp_str, "monthly") == 0)
			conf->priority_reset_period = PRIORITY_RESET_MONTHLY;
		else if (strcasecmp(temp_str, "quarterly") == 0)
			conf->priority_reset_period = PRIORITY_RESET_QUARTERLY;
		else if (strcasecmp(temp_str, "yearly") == 0)
			conf->priority_reset_period = PRIORITY_RESET_YEARLY;
		else {
			fatal("Bad value \"%s\" for PriorityUsageResetPeriod",
			      temp_str);
		}
		xfree(temp_str);
	} else {
		conf->priority_reset_period = PRIORITY_RESET_NONE;
		if(!conf->priority_decay_hl) {
			fatal("You have to either have "
			      "PriorityDecayHalfLife != 0 or "
			      "PriorityUsageResetPeriod set to something "
			      "or the priority plugin will result in "
			      "rolling over.");
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

	/* Out of order due to use with ProctrackType */
	if (!s_p_get_string(&conf->switch_type, "SwitchType", hashtbl))
		conf->switch_type = xstrdup(DEFAULT_SWITCH_TYPE);

	if (!s_p_get_string(&conf->proctrack_type, "ProctrackType", hashtbl)) {
		if (!strcmp(conf->switch_type,"switch/elan"))
			conf->proctrack_type = xstrdup("proctrack/rms");
		else
			conf->proctrack_type = 
				xstrdup(DEFAULT_PROCTRACK_TYPE);
	}
	if ((!strcmp(conf->switch_type, "switch/elan"))
	    && (!strcmp(conf->proctrack_type,"proctrack/linuxproc")))
		fatal("proctrack/linuxproc is incompatable with switch/elan");

	if (s_p_get_string(&temp_str, "PrivateData", hashtbl)) {
		if (strstr(temp_str, "account"))
			conf->private_data |= PRIVATE_DATA_ACCOUNTS;
		if (strstr(temp_str, "job"))
			conf->private_data |= PRIVATE_DATA_JOBS;
		if (strstr(temp_str, "node"))
			conf->private_data |= PRIVATE_DATA_NODES;
		if (strstr(temp_str, "partition"))
			conf->private_data |= PRIVATE_DATA_PARTITIONS;
		if (strstr(temp_str, "reservation"))
			conf->private_data |= PRIVATE_DATA_RESERVATIONS;
		if (strstr(temp_str, "usage"))
			conf->private_data |= PRIVATE_DATA_USAGE;
		if (strstr(temp_str, "user"))
			conf->private_data |= PRIVATE_DATA_USERS;
		if (strstr(temp_str, "all"))
			conf->private_data = 0xffff;
		xfree(temp_str);
	}

	s_p_get_string(&conf->prolog, "Prolog", hashtbl);
	s_p_get_string(&conf->prolog_slurmctld, "PrologSlurmctld", hashtbl);

	if (!s_p_get_uint16(&conf->propagate_prio_process,
			"PropagatePrioProcess", hashtbl)) {
		conf->propagate_prio_process = DEFAULT_PROPAGATE_PRIO_PROCESS;
	} else if (conf->propagate_prio_process > 1) {
		fatal("Bad PropagatePrioProcess: %u",
			conf->propagate_prio_process);
	}

        if (s_p_get_string(&conf->propagate_rlimits_except,
			   "PropagateResourceLimitsExcept", hashtbl)) {
                if ((parse_rlimits(conf->propagate_rlimits_except,
                                   NO_PROPAGATE_RLIMITS)) < 0)
                        fatal("Bad PropagateResourceLimitsExcept: %s",
			      conf->propagate_rlimits_except);
        } else {
                if (!s_p_get_string(&conf->propagate_rlimits,
				    "PropagateResourceLimits", hashtbl))
                        conf->propagate_rlimits = xstrdup( "ALL" );
                if ((parse_rlimits(conf->propagate_rlimits,
                                   PROPAGATE_RLIMITS )) < 0)
                        fatal("Bad PropagateResourceLimits: %s",
			      conf->propagate_rlimits);
        }

	if (!s_p_get_uint16(&conf->ret2service, "ReturnToService", hashtbl))
		conf->ret2service = DEFAULT_RETURN_TO_SERVICE;

	s_p_get_uint16(&conf->resv_over_run, "ResvOverRun", hashtbl);

	s_p_get_string(&conf->resume_program, "ResumeProgram", hashtbl);
	if (!s_p_get_uint16(&conf->resume_rate, "ResumeRate", hashtbl))
		conf->resume_rate = DEFAULT_RESUME_RATE;
	if (!s_p_get_uint16(&conf->resume_timeout, "ResumeTimeout", hashtbl))
		conf->resume_timeout = DEFAULT_RESUME_TIMEOUT;

	s_p_get_string(&conf->salloc_default_command, "SallocDefaultCommand",
			hashtbl);

	s_p_get_string(&conf->sched_params, "SchedulerParameters", hashtbl);

	if (s_p_get_uint16(&conf->schedport, "SchedulerPort", hashtbl)) {
		if (conf->schedport == 0) {
			error("SchedulerPort=0 is invalid");
			conf->schedport = DEFAULT_SCHEDULER_PORT;
		}
	} else {
		conf->schedport = DEFAULT_SCHEDULER_PORT;
	}

	if (!s_p_get_uint16(&conf->schedrootfltr,
			    "SchedulerRootFilter", hashtbl))
		conf->schedrootfltr = DEFAULT_SCHEDROOTFILTER;

	if (!s_p_get_uint16(&conf->sched_time_slice, "SchedulerTimeSlice",
	    hashtbl))
		conf->sched_time_slice = DEFAULT_SCHED_TIME_SLICE;

	if (!s_p_get_string(&conf->schedtype, "SchedulerType", hashtbl))
		conf->schedtype = xstrdup(DEFAULT_SCHEDTYPE);

	if (strcmp(conf->priority_type, "priority/multifactor") == 0) {
		if ((strcmp(conf->schedtype, "sched/wiki")  == 0) ||
		    (strcmp(conf->schedtype, "sched/wiki2") == 0)) {
			fatal("PriorityType=priority/multifactor is "
			      "incompatible with SchedulerType=%s",
			      conf->schedtype);
		}
	}
	if (conf->preempt_mode) {
		if ((strcmp(conf->schedtype, "sched/wiki")  == 0) ||
		    (strcmp(conf->schedtype, "sched/wiki2") == 0)) {
			fatal("Job preemption is incompatible with "
			      "SchedulerType=%s",
			      conf->schedtype);
		}
	}

	if (!s_p_get_string(&conf->select_type, "SelectType", hashtbl))
		conf->select_type = xstrdup(DEFAULT_SELECT_TYPE);

        if (s_p_get_string(&temp_str,
			   "SelectTypeParameters", hashtbl)) {
		select_type_plugin_info_t type_param;
		if ((parse_select_type_param(temp_str, &type_param) < 0)) {
			xfree(temp_str);
			fatal("Bad SelectTypeParameter: %s",
			      conf->select_type_param);
		}
		conf->select_type_param = (uint16_t) type_param;
		xfree(temp_str);
	} else {
		if (strcmp(conf->select_type,"select/cons_res") == 0)
			conf->select_type_param = CR_CPU;
		else
			conf->select_type_param = SELECT_TYPE_INFO_NONE;
	}

	if (!s_p_get_string( &conf->slurm_user_name, "SlurmUser", hashtbl)) {
		conf->slurm_user_name = xstrdup("root");
		conf->slurm_user_id   = 0;
	} else {
		uid_t my_uid;
		if (uid_from_string (conf->slurm_user_name, &my_uid) < 0) {
			fatal ("Invalid user for SlurmUser %s, ignored",
			       conf->slurm_user_name);
			xfree(conf->slurm_user_name);
		} else {
			conf->slurm_user_id = my_uid;
		}
	}

	if (!s_p_get_string( &conf->slurmd_user_name, "SlurmdUser", hashtbl)) {
		conf->slurmd_user_name = xstrdup("root");
		conf->slurmd_user_id   = 0;
	} else {
		uid_t my_uid;
		if (uid_from_string (conf->slurmd_user_name, &my_uid) < 0) {
			fatal ("Invalid user for SlurmdUser %s, ignored",
			       conf->slurmd_user_name);
			xfree(conf->slurmd_user_name);
		} else {
			conf->slurmd_user_id = my_uid;
		}
	}

	if (s_p_get_uint16(&conf->slurmctld_debug, "SlurmctldDebug", hashtbl))
		_normalize_debug_level(&conf->slurmctld_debug);
	else
		conf->slurmctld_debug = LOG_LEVEL_INFO;

	if (!s_p_get_string(&conf->slurmctld_pidfile,
			    "SlurmctldPidFile", hashtbl))
		conf->slurmctld_pidfile = xstrdup(DEFAULT_SLURMCTLD_PIDFILE);

	s_p_get_string(&conf->slurmctld_logfile, "SlurmctldLogFile", hashtbl);

	if (!s_p_get_uint32(&conf->slurmctld_port, "SlurmctldPort", hashtbl))
		conf->slurmctld_port = SLURMCTLD_PORT;

	if (!s_p_get_uint16(&conf->slurmctld_timeout,
			    "SlurmctldTimeout", hashtbl))
		conf->slurmctld_timeout = DEFAULT_SLURMCTLD_TIMEOUT;

	if (s_p_get_uint16(&conf->slurmd_debug, "SlurmdDebug", hashtbl))
		_normalize_debug_level(&conf->slurmd_debug);
	else
		conf->slurmd_debug = LOG_LEVEL_INFO;

	s_p_get_string(&conf->slurmd_logfile, "SlurmdLogFile", hashtbl);

	if (!s_p_get_string(&conf->slurmd_pidfile, "SlurmdPidFile", hashtbl))
		conf->slurmd_pidfile = xstrdup(DEFAULT_SLURMD_PIDFILE);

	if (!s_p_get_uint32(&conf->slurmd_port, "SlurmdPort", hashtbl))
		conf->slurmd_port = SLURMD_PORT;

	if (!s_p_get_string(&conf->slurmd_spooldir, "SlurmdSpoolDir", hashtbl))
		conf->slurmd_spooldir = xstrdup(DEFAULT_SPOOLDIR);

	if (!s_p_get_uint16(&conf->slurmd_timeout, "SlurmdTimeout", hashtbl))
		conf->slurmd_timeout = DEFAULT_SLURMD_TIMEOUT;

	s_p_get_string(&conf->srun_prolog, "SrunProlog", hashtbl);
	s_p_get_string(&conf->srun_epilog, "SrunEpilog", hashtbl);

	if (!s_p_get_string(&conf->state_save_location,
			    "StateSaveLocation", hashtbl))
		conf->state_save_location = xstrdup(DEFAULT_SAVE_STATE_LOC);

	s_p_get_string(&conf->suspend_exc_nodes, "SuspendExcNodes", hashtbl);
	s_p_get_string(&conf->suspend_exc_parts, "SuspendExcParts", hashtbl);
	s_p_get_string(&conf->suspend_program, "SuspendProgram", hashtbl);
	if (!s_p_get_uint16(&conf->suspend_rate, "SuspendRate", hashtbl))
		conf->suspend_rate = DEFAULT_SUSPEND_RATE;
	if (s_p_get_long(&long_suspend_time, "SuspendTime", hashtbl)) {
		if ((long_suspend_time > 0) && 
		    (strcmp(conf->select_type, "select/bluegene") == 0)) {
			fatal("SuspendTime (power save mode) incomptible with "
			      "select/bluegene");
		}
		conf->suspend_time = long_suspend_time + 1;
	} else
		conf->suspend_time = 0;
	if (!s_p_get_uint16(&conf->suspend_timeout, "SuspendTimeout", hashtbl))
		conf->suspend_timeout = DEFAULT_SUSPEND_TIMEOUT;

	/* see above for switch_type, order dependent */

	if (!s_p_get_string(&conf->task_plugin, "TaskPlugin", hashtbl))
		conf->task_plugin = xstrdup(DEFAULT_TASK_PLUGIN);

	if (s_p_get_string(&temp_str, "TaskPluginParam", hashtbl)) {
		char *last = NULL, *tok;
		bool set_mode = false, set_unit = false;
		tok = strtok_r(temp_str, ",", &last);
		while (tok) {
			if (strcasecmp(tok, "none") == 0) {
				if (set_unit)
					fatal("Bad TaskPluginParam: %s", tok);
				set_unit = true;
				conf->task_plugin_param |= CPU_BIND_NONE;
			} else if (strcasecmp(tok, "sockets") == 0) {
				if (set_unit)
					fatal("Bad TaskPluginParam: %s", tok);
				set_unit = true;
				conf->task_plugin_param |= CPU_BIND_TO_SOCKETS;
			} else if (strcasecmp(tok, "cores") == 0) {
				if (set_unit)
					fatal("Bad TaskPluginParam: %s", tok);
				set_unit = true;
				conf->task_plugin_param |= CPU_BIND_TO_CORES;
			} else if (strcasecmp(tok, "threads") == 0) {
				if (set_unit)
					fatal("Bad TaskPluginParam: %s", tok);
				set_unit = true;
				conf->task_plugin_param |= CPU_BIND_TO_THREADS;
			} else if (strcasecmp(tok, "cpusets") == 0) {
				if (set_mode)
					fatal("Bad TaskPluginParam: %s", tok);
				set_mode = true;
				conf->task_plugin_param |= CPU_BIND_CPUSETS;
			} else if (strcasecmp(tok, "sched") == 0) {
				if (set_mode)
					fatal("Bad TaskPluginParam: %s", tok);
				set_mode = true;
				/* No change to task_plugin_param, 
				 * this is the default */
			} else if (strcasecmp(tok, "verbose") == 0) {
				conf->task_plugin_param |= CPU_BIND_VERBOSE;
			} else
				fatal("Bad TaskPluginParam: %s", tok);
			tok = strtok_r(NULL, ",", &last);
		}
		xfree(temp_str);
	}

	s_p_get_string(&conf->task_epilog, "TaskEpilog", hashtbl);
	s_p_get_string(&conf->task_prolog, "TaskProlog", hashtbl);

	if (!s_p_get_string(&conf->tmp_fs, "TmpFS", hashtbl))
		conf->tmp_fs = xstrdup(DEFAULT_TMP_FS);

	if (!s_p_get_uint16(&conf->wait_time, "WaitTime", hashtbl))
		conf->wait_time = DEFAULT_WAIT_TIME;

	if (!s_p_get_string(&conf->topology_plugin, "TopologyPlugin", hashtbl))
		conf->topology_plugin = xstrdup(DEFAULT_TOPOLOGY_PLUGIN);

	if (s_p_get_uint16(&conf->tree_width, "TreeWidth", hashtbl)) {
		if (conf->tree_width == 0) {
			error("TreeWidth=0 is invalid");
			conf->tree_width = DEFAULT_TREE_WIDTH; /* default? */
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

#ifdef HAVE_BG
	if (conf->node_prefix == NULL)
		fatal("No valid node name prefix identified");
#endif

	xfree(default_storage_type);
	xfree(default_storage_loc);
	xfree(default_storage_host);
	xfree(default_storage_user);
	xfree(default_storage_pass);
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
 * debug_flags2str - convert a DebugFlags uint32_t to the equivalent string
 */
extern char * debug_flags2str(uint32_t debug_flags)
{
	char *rc = NULL;

	if (debug_flags & DEBUG_FLAG_CPU_BIND) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "CPU_Bind");
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
	if (debug_flags & DEBUG_FLAG_TRIGGERS) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "Triggers");
	}
	if (debug_flags & DEBUG_FLAG_WIKI) {
		if (rc)
			xstrcat(rc, ",");
		xstrcat(rc, "Wiki");
	}
		
	return rc;
}

/*
 * debug_str2flags - Convert a DebugFlags string to the equivalent uint32_t
 * Returns NO_VAL if invalid
 */
extern uint32_t debug_str2flags(char *debug_flags)
{
	uint32_t rc = 0;
	char *tmp_str, *tok, *last = NULL;

	if (!debug_flags)
		return rc;

	tmp_str = xstrdup(debug_flags);
	tok = strtok_r(tmp_str, ",", &last);
	while (tok) {
		if      (strcasecmp(tok, "CPU_Bind") == 0)
			rc |= DEBUG_FLAG_CPU_BIND;
		else if (strcasecmp(tok, "SelectType") == 0)
			rc |= DEBUG_FLAG_SELECT_TYPE;
		else if (strcasecmp(tok, "Steps") == 0)
			rc |= DEBUG_FLAG_STEPS;
		else if (strcasecmp(tok, "Triggers") == 0)
			rc |= DEBUG_FLAG_TRIGGERS;
		else if (strcasecmp(tok, "Wiki") == 0)
			rc |= DEBUG_FLAG_WIKI;
		else {
			error("Invalid DebugFlag: %s", tok);
			rc = NO_VAL;
			break;
		}
		tok = strtok_r(NULL, ",", &last);
	}
	xfree(tmp_str);

	return rc;
}

extern void destroy_config_key_pair(void *object)
{
	config_key_pair_t *key_pair_ptr = (config_key_pair_t *)object;

	if(key_pair_ptr) {
		xfree(key_pair_ptr->name);
		xfree(key_pair_ptr->value);
		xfree(key_pair_ptr);
	}
}

extern void pack_config_key_pair(void *in, uint16_t rpc_version, Buf buffer)
{
	config_key_pair_t *object = (config_key_pair_t *)in;
	packstr(object->name,  buffer);
	packstr(object->value, buffer);
}

extern int unpack_config_key_pair(void **object, uint16_t rpc_version,
				  Buf buffer)
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

extern int sort_key_pairs(config_key_pair_t *key_a, config_key_pair_t *key_b)
{
	int size_a = strcmp(key_a->name, key_b->name);

	if (size_a < 0)
		return -1;
	else if (size_a > 0)
		return 1;

	return 0;
}
