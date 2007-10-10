/*****************************************************************************\
 *  read_config.c - read the overall slurm configuration file
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
 *  UCRL-CODE-226842.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

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
#include "src/common/slurm_selecttype_info.h"

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

static int parse_nodename(void **dest, slurm_parser_enum_t type,
			  const char *key, const char *value,
			  const char *line, char **leftover);
static void destroy_nodename(void *ptr);
static int parse_partitionname(void **dest, slurm_parser_enum_t type,
			       const char *key, const char *value,
			       const char *line, char **leftover);
static void destroy_partitionname(void *ptr);
static int parse_downnodes(void **dest, slurm_parser_enum_t type,
			   const char *key, const char *value,
			   const char *line, char **leftover);
static void destroy_downnodes(void *ptr);
static int defunct_option(void **dest, slurm_parser_enum_t type,
			  const char *key, const char *value,
			  const char *line, char **leftover);
static void validate_and_set_defaults(slurm_ctl_conf_t *conf,
				      s_p_hashtbl_t *hashtbl);

s_p_options_t slurm_conf_options[] = {
	{"AuthType", S_P_STRING},
	{"CheckpointType", S_P_STRING},
	{"CacheGroups", S_P_UINT16},
	{"BackupAddr", S_P_STRING},
	{"BackupController", S_P_STRING},
	{"ControlAddr", S_P_STRING},
	{"ControlMachine", S_P_STRING},
	{"Epilog", S_P_STRING},
	{"FastSchedule", S_P_UINT16},
	{"FirstJobId", S_P_UINT32},
	{"HashBase", S_P_LONG, defunct_option},
	{"HeartbeatInterval", S_P_LONG, defunct_option},
	{"InactiveLimit", S_P_UINT16},
	{"JobAcctLogFile", S_P_STRING},
	{"JobAcctFrequency", S_P_UINT16},
	{"JobAcctType", S_P_STRING},
	{"JobCompLoc", S_P_STRING},
	{"JobCompType", S_P_STRING},
	{"JobCredentialPrivateKey", S_P_STRING},
	{"JobCredentialPublicCertificate", S_P_STRING},
	{"JobFileAppend", S_P_UINT16},
	{"KillTree", S_P_UINT16, defunct_option},
	{"KillWait", S_P_UINT16},
	{"MailProg", S_P_STRING},
	{"MaxJobCount", S_P_UINT16},
	{"MessageTimeout", S_P_UINT16},
	{"MinJobAge", S_P_UINT16},
	{"MpichGmDirectSupport", S_P_LONG},
	{"MpiDefault", S_P_STRING},
	{"PluginDir", S_P_STRING},
	{"PlugStackConfig", S_P_STRING},
	{"ProctrackType", S_P_STRING},
	{"Prolog", S_P_STRING},
	{"PropagatePrioProcess", S_P_UINT16},
	{"PropagateResourceLimitsExcept", S_P_STRING},
	{"PropagateResourceLimits", S_P_STRING},
	{"ReturnToService", S_P_UINT16},
	{"SchedulerAuth", S_P_STRING},
	{"SchedulerPort", S_P_UINT16},
	{"SchedulerRootFilter", S_P_UINT16},
	{"SchedulerType", S_P_STRING},
	{"SelectType", S_P_STRING},
	{"SelectTypeParameters", S_P_STRING},
	{"SlurmUser", S_P_STRING},
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
	{"SwitchType", S_P_STRING},
	{"TaskEpilog", S_P_STRING},
	{"TaskProlog", S_P_STRING},
	{"TaskPlugin", S_P_STRING},
	{"TaskPluginParam", S_P_STRING},
	{"TmpFS", S_P_STRING},
	{"TreeWidth", S_P_UINT16},
	{"UnkillableStepProgram", S_P_STRING},
	{"UnkillableStepTimeout", S_P_UINT16},
	{"UsePAM", S_P_BOOLEAN},
	{"WaitTime", S_P_UINT16},

	{"NodeName", S_P_ARRAY, parse_nodename, destroy_nodename},
	{"PartitionName", S_P_ARRAY, parse_partitionname, destroy_partitionname},
	{"DownNodes", S_P_ARRAY, parse_downnodes, destroy_downnodes},

	{NULL}
};


static int defunct_option(void **dest, slurm_parser_enum_t type,
			  const char *key, const char *value,
			  const char *line, char **leftover)
{
	error("The option \"%s\" is defunct, see man slurm.conf.", key);
	return 0;
}

static int parse_nodename(void **dest, slurm_parser_enum_t type,
			  const char *key, const char *value,
			  const char *line, char **leftover)
{
	s_p_hashtbl_t *tbl, *dflt;
	slurm_conf_node_t *n;
	static s_p_options_t _nodename_options[] = {
		{"NodeHostname", S_P_STRING},
		{"NodeAddr", S_P_STRING},
		{"CoresPerSocket", S_P_UINT16},
		{"Feature", S_P_STRING},
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
			error("NodeHostname not allowed with NodeName=DEFAULT");
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
			n->tmp_disk = 1;

		if (!s_p_get_uint32(&n->weight, "Weight", tbl)
		    && !s_p_get_uint32(&n->weight, "Weight", dflt))
			n->weight = 1;

		s_p_hashtbl_destroy(tbl);

		if (n->cores == 0)	/* make sure cores is non-zero */
			n->cores = 1;
		if (n->threads == 0)	/* make sure threads is non-zero */
			n->threads = 1;
		 
		if (!no_cpus    &&	/* infer missing Sockets= */
		    no_sockets) {
			n->sockets = n->cpus / (n->cores * n->threads);
		}

		if (n->sockets == 0)	/* make sure sockets is non-zero */
			n->sockets = 1;

		if (no_cpus     &&	/* infer missing Procs= */
		    !no_sockets) {
			n->cpus = n->sockets * n->cores * n->threads;
		}

		/* if only Procs= and Sockets= specified check for match */
		if (!no_cpus    &&
		    !no_sockets &&
		    no_cores    &&
		    no_threads) {
			if (n->cpus != n->sockets) {
				n->sockets = n->cpus;
				error("Procs doesn't match Sockets, "
				      "setting Sockets to %d",
				      n->sockets);
			}
		}

		*dest = (void *)n;

		return 1;
	}

	/* should not get here */
}

static void destroy_nodename(void *ptr)
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

static int parse_partitionname(void **dest, slurm_parser_enum_t type,
			       const char *key, const char *value,
			       const char *line, char **leftover)
{
	s_p_hashtbl_t *tbl, *dflt;
	slurm_conf_partition_t *p;
	char *tmp = NULL;
	static s_p_options_t _partition_options[] = {
		{"AllowGroups", S_P_STRING},
		{"Default", S_P_BOOLEAN}, /* YES or NO */
		{"Hidden", S_P_BOOLEAN}, /* YES or NO */
		{"MaxTime", S_P_UINT32}, /* INFINITE or a number */
		{"MaxNodes", S_P_UINT32}, /* INFINITE or a number */
		{"MinNodes", S_P_UINT32},
		{"Nodes", S_P_STRING},
		{"RootOnly", S_P_BOOLEAN}, /* YES or NO */
		{"Shared", S_P_STRING}, /* YES, NO, or FORCE */
		{"State", S_P_BOOLEAN}, /* UP or DOWN */
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

		if (!s_p_get_boolean(&p->default_flag, "Default", tbl)
		    && !s_p_get_boolean(&p->default_flag, "Default", dflt))
			p->default_flag = false;

		if (!s_p_get_boolean(&p->hidden_flag, "Hidden", tbl)
		    && !s_p_get_boolean(&p->hidden_flag, "Hidden", dflt))
			p->hidden_flag = false;

		if (!s_p_get_uint32(&p->max_time, "MaxTime", tbl)
		    && !s_p_get_uint32(&p->max_time, "MaxTime", dflt))
			p->max_time = INFINITE;

		if (!s_p_get_uint32(&p->max_nodes, "MaxNodes", tbl)
		    && !s_p_get_uint32(&p->max_nodes, "MaxNodes", dflt))
			p->max_nodes = INFINITE;

		if (!s_p_get_uint32(&p->min_nodes, "MinNodes", tbl)
		    && !s_p_get_uint32(&p->min_nodes, "MinNodes", dflt))
			p->min_nodes = 1;

		if (!s_p_get_string(&p->nodes, "Nodes", tbl)
		    && !s_p_get_string(&p->nodes, "Nodes", dflt))
			p->nodes = NULL;

		if (!s_p_get_boolean(&p->root_only_flag, "RootOnly", tbl)
		    && !s_p_get_boolean(&p->root_only_flag, "RootOnly", dflt))
			p->root_only_flag = false;

		if (!s_p_get_string(&tmp, "Shared", tbl)
		    && !s_p_get_string(&tmp, "Shared", dflt)) {
			p->shared = SHARED_NO;
		} else {
			if (strcasecmp(tmp, "NO") == 0)
				p->shared = SHARED_NO;
#ifndef HAVE_XCPU
			/* Only "Shared=NO" is valid on XCPU systems */
			else if (strcasecmp(tmp, "YES") == 0)
				p->shared = SHARED_YES;
			else if (strcasecmp(tmp, "EXCLUSIVE") == 0)
				p->shared = SHARED_EXCLUSIVE;
			else if (strcasecmp(tmp, "FORCE") == 0)
				p->shared = SHARED_FORCE;
#endif
			else {
				error("Bad value \"%s\" for Shared", tmp);
				destroy_partitionname(p);
				s_p_hashtbl_destroy(tbl);
				xfree(tmp);
				return -1;
			}
		}
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

static void destroy_partitionname(void *ptr)
{
	slurm_conf_partition_t *p = (slurm_conf_partition_t *)ptr;

	xfree(p->name);
	xfree(p->nodes);
	xfree(p->allow_groups);
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

static int parse_downnodes(void **dest, slurm_parser_enum_t type,
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

static void destroy_downnodes(void *ptr)
{
	slurm_conf_downnodes_t *n = (slurm_conf_downnodes_t *)ptr;
	xfree(n->nodenames);
	xfree(n->reason);
	xfree(n->state);
	xfree(ptr);
}

int slurm_conf_downnodes_array(slurm_conf_downnodes_t **ptr_array[])
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
	new->alias = xstrdup(alias);
	new->hostname = xstrdup(hostname);
	new->address = xstrdup(address);
	new->port = port;
	new->cpus	= cpus;
	new->sockets	= sockets;
	new->cores	= cores;
	new->threads	= threads;
	new->addr_initialized = false;
	new->next_hostname = host_to_node_hashtbl[hostname_idx];
	host_to_node_hashtbl[hostname_idx] = new;
	new->next_alias = node_to_host_hashtbl[alias_idx];
	node_to_host_hashtbl[alias_idx] = new;
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

	if(!conf_initialized) 
		_init_slurm_conf(NULL);

	count = slurm_conf_nodename_array(&ptr_array);
	if (count == 0) {
		return;
	}

	for (i = 0; i < count; i++) {
		_register_conf_node_aliases(ptr_array[i]);
	}
}

extern void slurm_conf_nodehash_init(void)
{
	slurm_conf_lock();
	_init_slurmd_nodehash();
	slurm_conf_unlock();
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
			uint16_t port = p->port;
			if (!port)
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
	xfree (ctl_conf_ptr->authtype);
	xfree (ctl_conf_ptr->checkpoint_type);
	xfree (ctl_conf_ptr->backup_addr);
	xfree (ctl_conf_ptr->backup_controller);
	xfree (ctl_conf_ptr->control_addr);
	xfree (ctl_conf_ptr->control_machine);
	xfree (ctl_conf_ptr->epilog);
	xfree (ctl_conf_ptr->job_acct_logfile);
	xfree (ctl_conf_ptr->job_acct_type);
	xfree (ctl_conf_ptr->job_comp_loc);
	xfree (ctl_conf_ptr->job_comp_type);
	xfree (ctl_conf_ptr->job_credential_private_key);
	xfree (ctl_conf_ptr->job_credential_public_certificate);
	xfree (ctl_conf_ptr->mail_prog);
	xfree (ctl_conf_ptr->mpi_default);
	xfree (ctl_conf_ptr->plugindir);
	xfree (ctl_conf_ptr->plugstack);
	xfree (ctl_conf_ptr->proctrack_type);
	xfree (ctl_conf_ptr->prolog);
	xfree (ctl_conf_ptr->propagate_rlimits_except);
	xfree (ctl_conf_ptr->propagate_rlimits);
	xfree (ctl_conf_ptr->schedtype);
	xfree (ctl_conf_ptr->select_type);
	xfree (ctl_conf_ptr->slurm_conf);
	xfree (ctl_conf_ptr->slurm_user_name);
	xfree (ctl_conf_ptr->slurmctld_logfile);
	xfree (ctl_conf_ptr->slurmctld_pidfile);
	xfree (ctl_conf_ptr->slurmd_logfile);
	xfree (ctl_conf_ptr->slurmd_pidfile);
	xfree (ctl_conf_ptr->slurmd_spooldir);
	xfree (ctl_conf_ptr->state_save_location);
	xfree (ctl_conf_ptr->switch_type);
	xfree (ctl_conf_ptr->tmp_fs);
	xfree (ctl_conf_ptr->task_epilog);
	xfree (ctl_conf_ptr->task_prolog);
	xfree (ctl_conf_ptr->task_plugin);
	xfree (ctl_conf_ptr->tmp_fs);
	xfree (ctl_conf_ptr->srun_prolog);
	xfree (ctl_conf_ptr->srun_epilog);
	xfree (ctl_conf_ptr->node_prefix);
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
	xfree (ctl_conf_ptr->authtype);
	ctl_conf_ptr->cache_groups		= (uint16_t) NO_VAL;
	xfree (ctl_conf_ptr->checkpoint_type);
	xfree (ctl_conf_ptr->backup_addr);
	xfree (ctl_conf_ptr->backup_controller);
	xfree (ctl_conf_ptr->control_addr);
	xfree (ctl_conf_ptr->control_machine);
	xfree (ctl_conf_ptr->epilog);
	ctl_conf_ptr->fast_schedule		= (uint16_t) NO_VAL;
	ctl_conf_ptr->first_job_id		= (uint32_t) NO_VAL;
	ctl_conf_ptr->inactive_limit		= (uint16_t) NO_VAL;
	xfree (ctl_conf_ptr->job_acct_logfile);
	ctl_conf_ptr->job_acct_freq             = 0;
	xfree (ctl_conf_ptr->job_acct_type);
	xfree (ctl_conf_ptr->job_comp_loc);
	xfree (ctl_conf_ptr->job_comp_type);
	xfree (ctl_conf_ptr->job_credential_private_key);
	xfree (ctl_conf_ptr->job_credential_public_certificate);
	ctl_conf_ptr->job_file_append		= (uint16_t) NO_VAL;
	ctl_conf_ptr->kill_wait			= (uint16_t) NO_VAL;
	xfree (ctl_conf_ptr->mail_prog);
	ctl_conf_ptr->max_job_cnt		= (uint16_t) NO_VAL;
	ctl_conf_ptr->min_job_age		= (uint16_t) NO_VAL;
	xfree (ctl_conf_ptr->mpi_default);
	ctl_conf_ptr->msg_timeout		= (uint16_t) NO_VAL;
	ctl_conf_ptr->next_job_id		= (uint32_t) NO_VAL;
	xfree (ctl_conf_ptr->plugindir);
	xfree (ctl_conf_ptr->plugstack);
	xfree (ctl_conf_ptr->proctrack_type);
	xfree (ctl_conf_ptr->prolog);
	ctl_conf_ptr->propagate_prio_process	= (uint16_t) NO_VAL;
	xfree (ctl_conf_ptr->propagate_rlimits_except);
	xfree (ctl_conf_ptr->propagate_rlimits);
	ctl_conf_ptr->ret2service		= (uint16_t) NO_VAL;
	ctl_conf_ptr->schedport			= (uint16_t) NO_VAL;
	ctl_conf_ptr->schedrootfltr		= (uint16_t) NO_VAL;
	xfree( ctl_conf_ptr->schedtype );
	xfree( ctl_conf_ptr->select_type );
	ctl_conf_ptr->select_type_param         = (uint16_t) NO_VAL;
	ctl_conf_ptr->slurm_user_id		= (uint16_t) NO_VAL; 
	xfree (ctl_conf_ptr->slurm_user_name);
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
	xfree (ctl_conf_ptr->state_save_location);
	xfree (ctl_conf_ptr->switch_type);
	xfree (ctl_conf_ptr->task_epilog);
	xfree (ctl_conf_ptr->task_prolog);
	xfree (ctl_conf_ptr->task_plugin);
	xfree (ctl_conf_ptr->tmp_fs);
	ctl_conf_ptr->wait_time			= (uint16_t) NO_VAL;
	xfree (ctl_conf_ptr->srun_prolog);
	xfree (ctl_conf_ptr->srun_epilog);
	xfree (ctl_conf_ptr->node_prefix);
	ctl_conf_ptr->tree_width       		= (uint16_t) NO_VAL;
	ctl_conf_ptr->use_pam			= 0;
	xfree (ctl_conf_ptr->unkillable_program);
	ctl_conf_ptr->unkillable_timeout        = (uint16_t) NO_VAL;

	_free_name_hashtbl();
	_init_name_hashtbl();

	return;
}

/* caller must lock conf_lock */
static void
_init_slurm_conf(const char *file_name)
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
	validate_and_set_defaults(conf_ptr, conf_hashtbl);
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

/*
 * slurm_conf_reinit_nolock - reload the slurm configuration from a file.
 *	This does the same thing as slurm_conf_reinit, but it performs
 *	no internal locking.  You are responsible for calling slurm_conf_lock()
 *	before calling this function, and calling slurm_conf_unlock()
 *	afterwards.
 * IN file_name - name of the slurm configuration file to be read
 *	If file_name is NULL, then this routine tries to use
 *	the value in the SLURM_CONF env variable.  Failing that,
 *	it uses the compiled-in default file name.
 *	Unlike slurm_conf_init, slurm_conf_reinit will always reread the
 *	file and reinitialize the configuration structures.
 * RET SLURM_SUCCESS if conf file is reinitialized, otherwise SLURM_ERROR.
 */
extern int 
slurm_conf_reinit_nolock(const char *file_name)
{
	return _internal_reinit(file_name);
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
validate_and_set_defaults(slurm_ctl_conf_t *conf, s_p_hashtbl_t *hashtbl)
{
	char *temp_str = NULL;
	bool truth;

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

	if (!s_p_get_string(&conf->control_machine, "ControlMachine", hashtbl))
		fatal ("validate_and_set_defaults: "
		       "ControlMachine not specified.");
	else if (strcasecmp("localhost", conf->control_machine) == 0) {
		xfree (conf->control_machine);
		conf->control_machine = xmalloc(MAX_SLURM_NAME);
		if (gethostname_short(conf->control_machine, MAX_SLURM_NAME))
			fatal("getnodename: %m");
	}

	if (!s_p_get_string(&conf->control_addr, "ControlAddr", hashtbl)
	    && conf->control_machine != NULL)
		conf->control_addr = xstrdup (conf->control_machine);

	if ((conf->backup_controller != NULL)
	    && (strcmp(conf->backup_controller, conf->control_machine) == 0)) {
		error("ControlMachine and BackupController identical");
		xfree(conf->backup_addr);
		xfree(conf->backup_controller);
	}

	if (!s_p_get_string(&conf->job_credential_private_key,
			    "JobCredentialPrivateKey", hashtbl))
		fatal("JobCredentialPrivateKey not set");
	if (!s_p_get_string(&conf->job_credential_public_certificate,
			    "JobCredentialPublicCertificate", hashtbl))
		fatal("JobCredentialPublicCertificate not set");

	if (s_p_get_uint16(&conf->max_job_cnt, "MaxJobCount", hashtbl)
	    && conf->max_job_cnt < 1)
		fatal("MaxJobCount=%u, No jobs permitted", conf->max_job_cnt);

	if (!s_p_get_string(&conf->authtype, "AuthType", hashtbl))
		conf->authtype = xstrdup(DEFAULT_AUTH_TYPE);

	if (!s_p_get_uint16(&conf->cache_groups, "CacheGroups", hashtbl))
		conf->cache_groups = DEFAULT_CACHE_GROUPS;

	if (!s_p_get_string(&conf->checkpoint_type, "CheckpointType", hashtbl))
		conf->checkpoint_type = xstrdup(DEFAULT_CHECKPOINT_TYPE);

	s_p_get_string(&conf->epilog, "Epilog", hashtbl);

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

	if (!s_p_get_string(&conf->job_acct_logfile, 
			    "JobAcctLogFile", hashtbl))
		conf->job_acct_logfile = xstrdup(DEFAULT_JOB_ACCT_LOGFILE);

	if (!s_p_get_uint16(&conf->job_acct_freq, "JobAcctFrequency", hashtbl))
		conf->job_acct_freq = DEFAULT_JOB_ACCT_FREQ;

	if (!s_p_get_string(&conf->job_acct_type, "JobAcctType", hashtbl))
		conf->job_acct_type = xstrdup(DEFAULT_JOB_ACCT_TYPE);

	s_p_get_string(&conf->job_comp_loc, "JobCompLoc", hashtbl);

	if (!s_p_get_string(&conf->job_comp_type, "JobCompType", hashtbl))
		conf->job_comp_type = xstrdup(DEFAULT_JOB_COMP_TYPE);

	if (!s_p_get_uint16(&conf->job_file_append, "JobFileAppend", hashtbl))
		conf->job_file_append = 0;

	if (!s_p_get_uint16(&conf->kill_wait, "KillWait", hashtbl))
		conf->kill_wait = DEFAULT_KILL_WAIT;

	if (!s_p_get_string(&conf->mail_prog, "MailProg", hashtbl))
		conf->mail_prog = xstrdup(DEFAULT_MAIL_PROG);

	if (!s_p_get_uint16(&conf->max_job_cnt, "MaxJobCount", hashtbl))
		conf->max_job_cnt = DEFAULT_MAX_JOB_COUNT;

	if (!s_p_get_uint16(&conf->msg_timeout, "MessageTimeout", hashtbl))
		conf->msg_timeout = DEFAULT_MSG_TIMEOUT;
	else if (conf->msg_timeout > 100)
		info("WARNING: MessageTimeout is too high for effective fault-tolerance");

	if (!s_p_get_uint16(&conf->min_job_age, "MinJobAge", hashtbl))
		conf->min_job_age = DEFAULT_MIN_JOB_AGE;

	if (!s_p_get_string(&conf->mpi_default, "MpiDefault", hashtbl))
		conf->mpi_default = xstrdup(DEFAULT_MPI_DEFAULT);

	if (!s_p_get_string(&conf->plugindir, "PluginDir", hashtbl))
		conf->plugindir = xstrdup(default_plugin_path);

	if (!s_p_get_string(&conf->plugstack, "PlugStackConfig", hashtbl))
		conf->plugstack = xstrdup(default_plugstack);

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

	s_p_get_string(&conf->prolog, "Prolog", hashtbl);

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

	if (!s_p_get_string(&conf->schedtype, "SchedulerType", hashtbl))
		conf->schedtype = xstrdup(DEFAULT_SCHEDTYPE);

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
		struct passwd *slurm_passwd;
		slurm_passwd = getpwnam(conf->slurm_user_name);
		if (slurm_passwd == NULL) {
			error ("Invalid user for SlurmUser %s, ignored",
			       conf->slurm_user_name);
			xfree(conf->slurm_user_name);
		} else {
			if (slurm_passwd->pw_uid > 0xffff)
				error("SlurmUser numeric overflow, "
				      "will be fixed soon");
			else
				conf->slurm_user_id = slurm_passwd->pw_uid;
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

	/* see above for switch_type, order dependent */

	if (!s_p_get_string(&conf->task_plugin, "TaskPlugin", hashtbl))
		conf->task_plugin = xstrdup(DEFAULT_TASK_PLUGIN);

	if (s_p_get_string(&temp_str, "TaskPluginParam", hashtbl)) {
		if (strcasecmp(temp_str, "cpusets") == 0)
			conf->task_plugin_param = TASK_PARAM_CPUSETS;
		else if (strcasecmp(temp_str, "sched") == 0)
			conf->task_plugin_param = TASK_PARAM_SCHED;
		else {
			fatal("Bad TaskPluginParam: %s", temp_str);
			conf->task_plugin_param = TASK_PARAM_NONE;
		}
		xfree(temp_str);
	}

	s_p_get_string(&conf->task_epilog, "TaskEpilog", hashtbl);
	s_p_get_string(&conf->task_prolog, "TaskProlog", hashtbl);

	if (!s_p_get_string(&conf->tmp_fs, "TmpFS", hashtbl))
		conf->tmp_fs = xstrdup(DEFAULT_TMP_FS);

	if (!s_p_get_uint16(&conf->wait_time, "WaitTime", hashtbl))
		conf->wait_time = DEFAULT_WAIT_TIME;
	
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
