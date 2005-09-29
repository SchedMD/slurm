/*****************************************************************************\
 *  read_config.c - read the overall slurm configuration file
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
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

#include <slurm/slurm.h>

#include "src/common/hostlist.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/parse_spec.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/slurm_rlimits_info.h"

#define BUF_SIZE 1024
#define MAX_NAME_LEN	32
#define MULTIPLE_VALUE_MSG "Multiple values for %s, latest one used"

inline static void _normalize_debug_level(uint16_t *level);
static int  _parse_node_spec (char *in_line, bool slurmd_hosts);
static int  _parse_part_spec (char *in_line);


typedef struct names_ll_s {
	char *node_hostname;
	char *node_name;
	struct names_ll_s *next;
} names_ll_t;
bool all_slurmd_hosts = false;
#define NAME_HASH_LEN 512
static names_ll_t *host_to_node_hashtbl[NAME_HASH_LEN] = {NULL};
static names_ll_t *node_to_host_hashtbl[NAME_HASH_LEN] = {NULL};
static char *this_hostname = NULL;

static void _free_name_hashtbl()
{
	int i;
	names_ll_t *p, *q;

	for (i=0; i<NAME_HASH_LEN; i++) {
		p = host_to_node_hashtbl[i];
		while (p) {
			xfree(p->node_hostname);
			xfree(p->node_name);
			q = p->next;
			xfree(p);
			p = q;
		}
		host_to_node_hashtbl[i] = NULL;
		p = node_to_host_hashtbl[i];
		while (p) {
			xfree(p->node_hostname);
			xfree(p->node_name);
			q = p->next;
			xfree(p);
			p = q;
		}
		node_to_host_hashtbl[i] = NULL;
	}
	xfree(this_hostname);
}

static void _init_name_hashtbl()
{
	return;
}

static int _get_hash_idx(char *s)
{
	int i;

	i = 0;
	while (*s) i += (int)*s++;
	return i % NAME_HASH_LEN;
}

static void _push_to_hashtbl(char *node, char *host)
{
	int idx;
	names_ll_t *p, *new;
	char *hh;

	hh = host ? host : node;
	idx = _get_hash_idx(hh);
#ifndef HAVE_FRONT_END		/* Operate only on front-end */
	p = host_to_node_hashtbl[idx];
	while (p) {
		if (strcmp(p->node_hostname, hh)==0) {
			fatal("Duplicated NodeHostname %s in the config file",
				hh);
			return;
		}
		p = p->next;
	}
#endif
	new = (names_ll_t *)xmalloc(sizeof(*new));
	new->node_hostname = xstrdup(hh);
	new->node_name = xstrdup(node);
	new->next = host_to_node_hashtbl[idx];
	host_to_node_hashtbl[idx] = new;

	idx = _get_hash_idx(node);
	p = node_to_host_hashtbl[idx];
	while (p) {
		if (strcmp(p->node_name, node)==0) {
			fatal("Duplicated NodeName %s in the config file",
				node);
			return;
		}
		p = p->next;
	}
	new = (names_ll_t *)xmalloc(sizeof(*new));
	new->node_name = xstrdup(node);
	new->node_hostname = xstrdup(hh);
	new->next = node_to_host_hashtbl[idx];
	node_to_host_hashtbl[idx] = new;
}

/*
 * Register the given NodeName in the alias table.
 * If node_hostname is NULL, only node_name will be used and 
 * no lookup table record is created.
 */
static void _register_conf_node_aliases(char *node_name, char *node_hostname)
{
	hostlist_t node_list = NULL, host_list = NULL;
	char *hn = NULL, *nn;

	if (node_name == NULL || *node_name == '\0')
		return;
	if (strcasecmp(node_name, "DEFAULT") == 0) {
		if (node_hostname) {
			fatal("NodeHostname for NodeName=DEFAULT is illegal");
		}
		return;
	}
	if (!this_hostname) {
		this_hostname = xmalloc(MAX_NAME_LEN);
		getnodename(this_hostname, MAX_NAME_LEN);
	}
	if (strcasecmp(node_name, "localhost") == 0)
		node_name = this_hostname;
	if (node_hostname == NULL)
		node_hostname = node_name;
	if (strcasecmp(node_hostname, "localhost") == 0)
		node_hostname = this_hostname;

	node_list = hostlist_create(node_name);
#ifdef HAVE_FRONT_END	/* Common NodeHostname for all NodeName values */
	/* Expect one common node_hostname for all back-end nodes */
	hn = node_hostname;
#else
	host_list = hostlist_create(node_hostname);
	if (hostlist_count(node_list) != hostlist_count(host_list))
		fatal("NodeName and NodeHostname have different "
			"number of records");
#endif
	while ((nn = hostlist_shift(node_list))) {
		if (host_list)
			hn = hostlist_shift(host_list);
		_push_to_hashtbl(nn, hn);
		if (host_list)
			free(hn);
		free(nn);
	}
	hostlist_destroy(node_list);
	if (host_list)
		hostlist_destroy(host_list);

	return;
}

/*
 * get_conf_node_hostname - Return the NodeHostname for given NodeName
 */
extern char *get_conf_node_hostname(char *node_name)
{
	int idx;
	names_ll_t *p;

	idx = _get_hash_idx(node_name);
	p = node_to_host_hashtbl[idx];
	while (p) {
		if (strcmp(p->node_name, node_name) == 0) {
			return xstrdup(p->node_hostname);
		}
		p = p->next;
	}

	if (all_slurmd_hosts)
		return NULL;
	else {
		 /* Assume identical if we didn't explicitly save all pairs */
		return xstrdup(node_name);
	}
}

/*
 * get_conf_node_name - Return the NodeName for given NodeHostname
 */
extern char *get_conf_node_name(char *node_hostname)
{
	int idx;
	names_ll_t *p;

	idx = _get_hash_idx(node_hostname);
	p = host_to_node_hashtbl[idx];
	while (p) {
		if (strcmp(p->node_hostname, node_hostname) == 0) {
			return xstrdup(p->node_name);
		}
		p = p->next;
	}

	if (all_slurmd_hosts)
		return NULL;
	else {
		/* Assume identical if we didn't explicitly save all pairs */
		return xstrdup(node_hostname);
	}
}




/* getnodename - equivalent to gethostname, but return only the first 
 * component of the fully qualified name 
 * (e.g. "linux123.foo.bar" becomes "linux123") 
 */
int
getnodename (char *name, size_t len)
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
 */
void
free_slurm_conf (slurm_ctl_conf_t *ctl_conf_ptr)
{
	xfree (ctl_conf_ptr->authtype);
	xfree (ctl_conf_ptr->backup_addr);
	xfree (ctl_conf_ptr->backup_controller);
	xfree (ctl_conf_ptr->checkpoint_type);
	xfree (ctl_conf_ptr->control_addr);
	xfree (ctl_conf_ptr->control_machine);
	xfree (ctl_conf_ptr->epilog);
	xfree (ctl_conf_ptr->job_acct_loc);
	xfree (ctl_conf_ptr->job_acct_parameters);
	xfree (ctl_conf_ptr->job_acct_type);
	xfree (ctl_conf_ptr->job_comp_loc);
	xfree (ctl_conf_ptr->job_comp_type);
	xfree (ctl_conf_ptr->job_credential_private_key);
	xfree (ctl_conf_ptr->job_credential_public_certificate);
	xfree (ctl_conf_ptr->mpi_default);
	xfree (ctl_conf_ptr->plugindir);
	xfree (ctl_conf_ptr->proctrack_type);
	xfree (ctl_conf_ptr->propagate_rlimits);
	xfree (ctl_conf_ptr->propagate_rlimits_except);
	xfree (ctl_conf_ptr->prolog);
	xfree (ctl_conf_ptr->schedauth);
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
	xfree (ctl_conf_ptr->checkpoint_type);
	xfree (ctl_conf_ptr->backup_addr);
	xfree (ctl_conf_ptr->backup_controller);
	xfree (ctl_conf_ptr->control_addr);
	xfree (ctl_conf_ptr->control_machine);
	xfree (ctl_conf_ptr->epilog);
	ctl_conf_ptr->fast_schedule		= (uint16_t) NO_VAL;
	ctl_conf_ptr->first_job_id		= (uint32_t) NO_VAL;
	ctl_conf_ptr->heartbeat_interval	= (uint16_t) NO_VAL;
	ctl_conf_ptr->inactive_limit		= (uint16_t) NO_VAL;
	xfree (ctl_conf_ptr->job_acct_loc);
	xfree (ctl_conf_ptr->job_acct_parameters);
	xfree (ctl_conf_ptr->job_acct_type);
	xfree (ctl_conf_ptr->job_comp_loc);
	xfree (ctl_conf_ptr->job_comp_type);
	xfree (ctl_conf_ptr->job_credential_private_key);
	xfree (ctl_conf_ptr->job_credential_public_certificate);
	ctl_conf_ptr->kill_wait			= (uint16_t) NO_VAL;
	ctl_conf_ptr->max_job_cnt		= (uint16_t) NO_VAL;
	ctl_conf_ptr->min_job_age		= (uint16_t) NO_VAL;
	xfree (ctl_conf_ptr->mpi_default);
	xfree (ctl_conf_ptr->plugindir);
	xfree (ctl_conf_ptr->proctrack_type);
	xfree (ctl_conf_ptr->prolog);
	xfree (ctl_conf_ptr->propagate_rlimits_except);
	xfree (ctl_conf_ptr->propagate_rlimits);
	ctl_conf_ptr->ret2service		= (uint16_t) NO_VAL;
	xfree( ctl_conf_ptr->schedauth );
	ctl_conf_ptr->schedport			= (uint16_t) NO_VAL;
	ctl_conf_ptr->schedrootfltr		= (uint16_t) NO_VAL;
	xfree( ctl_conf_ptr->schedtype );
	xfree( ctl_conf_ptr->select_type );
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
	xfree (ctl_conf_ptr->tmp_fs);
	ctl_conf_ptr->wait_time			= (uint16_t) NO_VAL;
	xfree (ctl_conf_ptr->srun_prolog);
	xfree (ctl_conf_ptr->srun_epilog);
	
	_free_name_hashtbl();
	_init_name_hashtbl();

	return;
}

/*
 * parse_config_spec - parse the overall configuration specifications, update  
 *	values
 * IN/OUT in_line - input line, parsed info overwritten with white-space
 * IN ctl_conf_ptr - pointer to data structure to be updated
 * RET 0 if no error, otherwise an error code
 *
 * NOTE: slurmctld and slurmd ports are built thus:
 *	if SlurmctldPort/SlurmdPort are set then
 *		get the port number based upon a look-up in /etc/services
 *		if the lookup fails then translate SlurmctldPort/SlurmdPort  
 *		into a number
 *	These port numbers are overridden if set in the configuration file
 */
int 
parse_config_spec (char *in_line, slurm_ctl_conf_t *ctl_conf_ptr) 
{
	int error_code;
	long fast_schedule = -1, hash_base, heartbeat_interval = -1;
	long inactive_limit = -1, kill_wait = -1;
	long ret2service = -1, slurmctld_timeout = -1, slurmd_timeout = -1;
	long sched_port = -1, sched_rootfltr = -1;
	long slurmctld_debug = -1, slurmd_debug = -1;
	long max_job_cnt = -1, min_job_age = -1, wait_time = -1;
	long slurmctld_port = -1, slurmd_port = -1;
	long mpich_gm_dir = -1, kill_tree = -1;
	char *backup_addr = NULL, *backup_controller = NULL;
	char *checkpoint_type = NULL, *control_addr = NULL;
	char *control_machine = NULL, *epilog = NULL, *mpi_default = NULL;
	char *proctrack_type = NULL, *prolog = NULL;
	char *propagate_rlimits_except = NULL, *propagate_rlimits = NULL;
	char *sched_type = NULL, *sched_auth = NULL;
	char *select_type = NULL;
	char *state_save_location = NULL, *tmp_fs = NULL;
	char *slurm_user = NULL, *slurmctld_pidfile = NULL;
	char *slurmctld_logfile = NULL;
	char *slurmd_logfile = NULL;
	char *slurmd_spooldir = NULL, *slurmd_pidfile = NULL;
	char *plugindir = NULL, *auth_type = NULL, *switch_type = NULL;
	char *job_acct_loc = NULL, *job_acct_parameters = NULL,
	     *job_acct_type = NULL;
	char *job_comp_loc = NULL, *job_comp_type = NULL;
	char *job_credential_private_key = NULL;
	char *job_credential_public_certificate = NULL;
	char *srun_prolog = NULL, *srun_epilog = NULL;
	long first_job_id = -1;

	error_code = slurm_parser (in_line,
		"AuthType=", 's', &auth_type,
		"CheckpointType=", 's', &checkpoint_type,
		"BackupAddr=", 's', &backup_addr, 
		"BackupController=", 's', &backup_controller, 
		"ControlAddr=", 's', &control_addr, 
		"ControlMachine=", 's', &control_machine, 
		/* SrunEpilog MUST come before Epilog */
		"SrunEpilog=", 's', &srun_epilog,
		"Epilog=", 's', &epilog, 
		"FastSchedule=", 'l', &fast_schedule,
		"FirstJobId=", 'l', &first_job_id,
		"HashBase=", 'l', &hash_base,	/* defunct */
		"HeartbeatInterval=", 'l', &heartbeat_interval,
		"InactiveLimit=", 'l', &inactive_limit,
		"JobAcctloc=", 's', &job_acct_loc,
		"JobAcctParameters=", 's', &job_acct_parameters,
		"JobAcctType=", 's', &job_acct_type,
		"JobCompLoc=", 's', &job_comp_loc,
		"JobCompType=", 's', &job_comp_type,
		"JobCredentialPrivateKey=", 's', &job_credential_private_key,
		"JobCredentialPublicCertificate=", 's', 
					&job_credential_public_certificate,
		"KillTree=", 'l', &kill_tree,
		"KillWait=", 'l', &kill_wait,
		"MaxJobCount=", 'l', &max_job_cnt,
		"MinJobAge=", 'l', &min_job_age,
		"MpichGmDirectSupport=", 'l', &mpich_gm_dir,
		"MpiDefault=", 's', &mpi_default,
		"PluginDir=", 's', &plugindir,
		"ProctrackType=", 's', &proctrack_type,
		/* SrunProlog MUST come before Prolog */
		"SrunProlog=", 's', &srun_prolog,
		"Prolog=", 's', &prolog,
		"PropagateResourceLimitsExcept=", 's',&propagate_rlimits_except,
		"PropagateResourceLimits=",       's',&propagate_rlimits,
		"ReturnToService=", 'l', &ret2service,
		"SchedulerAuth=", 's', &sched_auth,
		"SchedulerPort=", 'l', &sched_port,
		"SchedulerRootFilter=", 'l', &sched_rootfltr,
		"SchedulerType=", 's', &sched_type,
		"SelectType=", 's', &select_type,
		"SlurmUser=", 's', &slurm_user,
		"SlurmctldDebug=", 'l', &slurmctld_debug,
		"SlurmctldLogFile=", 's', &slurmctld_logfile,
		"SlurmctldPidFile=", 's', &slurmctld_pidfile,
		"SlurmctldPort=", 'l', &slurmctld_port,
		"SlurmctldTimeout=", 'l', &slurmctld_timeout,
		"SlurmdDebug=", 'l', &slurmd_debug,
		"SlurmdLogFile=", 's', &slurmd_logfile,
		"SlurmdPidFile=",  's', &slurmd_pidfile,
		"SlurmdPort=", 'l', &slurmd_port,
		"SlurmdSpoolDir=", 's', &slurmd_spooldir,
		"SlurmdTimeout=", 'l', &slurmd_timeout,
		"StateSaveLocation=", 's', &state_save_location,
		"SwitchType=", 's', &switch_type,
		"TmpFS=", 's', &tmp_fs,
		"WaitTime=", 'l', &wait_time,
		"END");

	if (error_code)
		return error_code;

	if ( auth_type ) {
		if ( ctl_conf_ptr->authtype ) {
			error( MULTIPLE_VALUE_MSG, "AuthType" );
			xfree( ctl_conf_ptr->authtype );
		}
		ctl_conf_ptr->authtype = auth_type;
	}

	if ( checkpoint_type ) {
		if ( ctl_conf_ptr->checkpoint_type ) {
			error( MULTIPLE_VALUE_MSG, "CheckpointType" );
			xfree( ctl_conf_ptr->checkpoint_type );
		}
		ctl_conf_ptr->checkpoint_type = checkpoint_type;
	}

	if ( backup_addr ) {
		if ( ctl_conf_ptr->backup_addr ) {
			error (MULTIPLE_VALUE_MSG, "BackupAddr");
			xfree (ctl_conf_ptr->backup_addr);
		}
		ctl_conf_ptr->backup_addr = backup_addr;
	}

	if ( backup_controller ) {
		if ( ctl_conf_ptr->backup_controller ) {
			error (MULTIPLE_VALUE_MSG, "BackupController");
			xfree (ctl_conf_ptr->backup_controller);
		}
		ctl_conf_ptr->backup_controller = backup_controller;
	}

	if ( control_addr ) {
		if ( ctl_conf_ptr->control_addr ) {
			error (MULTIPLE_VALUE_MSG, "ControlAddr");
			xfree (ctl_conf_ptr->control_addr);
		}
		ctl_conf_ptr->control_addr = control_addr;
	}

	if ( control_machine ) {
		if ( ctl_conf_ptr->control_machine ) {
			error (MULTIPLE_VALUE_MSG, "ControlMachine");
			xfree (ctl_conf_ptr->control_machine);
		}
		ctl_conf_ptr->control_machine = control_machine;
	}

	if ( epilog ) {
		if ( ctl_conf_ptr->epilog ) {
			error (MULTIPLE_VALUE_MSG, "Epilog");
			xfree (ctl_conf_ptr->epilog);
		}
		ctl_conf_ptr->epilog = epilog;
	}

	if ( fast_schedule != -1 ) {
		if ( ctl_conf_ptr->fast_schedule != (uint16_t) NO_VAL)
			error (MULTIPLE_VALUE_MSG, "FastSchedule");
		if ((fast_schedule < 0) || (fast_schedule > 0xffff))
			error("FastSchedule=%ld is invalid", fast_schedule);
		else
			ctl_conf_ptr->fast_schedule = fast_schedule;
	}

	if ( first_job_id != -1) {
		if ( ctl_conf_ptr->first_job_id != (uint32_t) NO_VAL)
			error (MULTIPLE_VALUE_MSG, "FirstJobId");
		if (first_job_id < 0)
			error("FirstJobId=%ld is invalid", first_job_id);
		else
			ctl_conf_ptr->first_job_id = first_job_id;
	}

	if ( heartbeat_interval != -1) {
		if ( ctl_conf_ptr->heartbeat_interval != (uint16_t) NO_VAL)
			error (MULTIPLE_VALUE_MSG, "HeartbeatInterval");
		if ((heartbeat_interval < 0) || (heartbeat_interval > 0xffff))
			error("HeartbeatInterval=%ld is invalid", 
				heartbeat_interval);
		else
			ctl_conf_ptr->heartbeat_interval = heartbeat_interval;
	}

	if ( inactive_limit != -1) {
		if ( ctl_conf_ptr->inactive_limit != (uint16_t) NO_VAL)
			error (MULTIPLE_VALUE_MSG, "InactiveLimit");
#ifdef HAVE_BGL		/* Inactive limit must be zero on Blue Gene */
		if (inactive_limit) {
			error("InactiveLimit=%ld is invalid on Blue Gene",
				inactive_limit);
		}
		inactive_limit = 0;	/* default value too */
#endif
		if ((inactive_limit < 0) || (inactive_limit > 0xffff))
			error("InactiveLimit=%ld is invalid", inactive_limit);
		else
			ctl_conf_ptr->inactive_limit = inactive_limit;
	}

	if ( job_acct_loc ) {
		if ( ctl_conf_ptr->job_acct_loc ) {
			error( MULTIPLE_VALUE_MSG, "JobAcctLoc" );
			xfree( ctl_conf_ptr->job_acct_loc );
		}
		ctl_conf_ptr->job_acct_loc = job_acct_loc;
	}

	if ( job_acct_parameters ) {
		if ( ctl_conf_ptr->job_acct_parameters ) {
			error( MULTIPLE_VALUE_MSG, "JobAcctParameters" );
			xfree( ctl_conf_ptr->job_acct_parameters );
		}
		ctl_conf_ptr->job_acct_parameters = job_acct_parameters;
	}

	if ( job_acct_type ) {
		if ( ctl_conf_ptr->job_acct_type ) {
			error( MULTIPLE_VALUE_MSG, "JobAcctType" );
			xfree( ctl_conf_ptr->job_acct_type );
		}
		ctl_conf_ptr->job_acct_type = job_acct_type;
	}

	if ( job_comp_loc ) {
		if ( ctl_conf_ptr->job_comp_loc ) {
			error( MULTIPLE_VALUE_MSG, "JobCompLoc" );
			xfree( ctl_conf_ptr->job_comp_loc );
		}
		ctl_conf_ptr->job_comp_loc = job_comp_loc;
	}

	if ( job_comp_type ) {
		if ( ctl_conf_ptr->job_comp_type ) {
			error( MULTIPLE_VALUE_MSG, "JobCompType" );
			xfree( ctl_conf_ptr->job_comp_type );
		}
		ctl_conf_ptr->job_comp_type = job_comp_type;
	}

	if ( job_credential_private_key ) {
		if ( ctl_conf_ptr->job_credential_private_key ) {
			error (MULTIPLE_VALUE_MSG, "JobCredentialPrivateKey");
			xfree (ctl_conf_ptr->job_credential_private_key);
		}
		ctl_conf_ptr->job_credential_private_key = 
						job_credential_private_key;
	}

	if ( job_credential_public_certificate ) {
		if ( ctl_conf_ptr->job_credential_public_certificate ) {
			error (MULTIPLE_VALUE_MSG, 
			       "JobCredentialPublicCertificate");
			xfree (ctl_conf_ptr->
			       job_credential_public_certificate);
		}
		ctl_conf_ptr->job_credential_public_certificate = 
					job_credential_public_certificate;
	}

	if ( kill_tree != -1) {
		verbose("KillTree configuration parameter is defunct");
		verbose("  mapping to ProctrackType=proctrack/linuxproc");
		xfree(proctrack_type);
		proctrack_type = xstrdup("proctrack/linuxproc");
	}

	if ( kill_wait != -1) {
		if ( ctl_conf_ptr->kill_wait != (uint16_t) NO_VAL)
			error (MULTIPLE_VALUE_MSG, "KillWait");
		if ((kill_wait < 0) || (kill_wait > 0xffff))
			error("KillWait=%ld is invalid", kill_wait);
		else
			ctl_conf_ptr->kill_wait = kill_wait;
	}

	if ( max_job_cnt != -1) {
		if ( ctl_conf_ptr->max_job_cnt != (uint16_t) NO_VAL)
			error (MULTIPLE_VALUE_MSG, "MaxJobCount");
		if ((max_job_cnt < 0) || (max_job_cnt > 0xffff))
			error("MaxJobCount=%ld is invalid", max_job_cnt);
		else
			ctl_conf_ptr->max_job_cnt = max_job_cnt;
	}

	if ( min_job_age != -1) {
		if ( ctl_conf_ptr->min_job_age != (uint16_t) NO_VAL)
			error (MULTIPLE_VALUE_MSG, "MinJobAge");
		if ((min_job_age < 0) || (min_job_age > 0xffff))
			error("MinJobAge=%ld is invalid", min_job_age);
		else
			ctl_conf_ptr->min_job_age = min_job_age;
	}

	if ( mpich_gm_dir != -1) {
		verbose("MpichGmDirectSupport configuration parameter is defunct");
		verbose("  mapping to ProctrackType=proctrack/linuxproc");
		xfree(proctrack_type);
		proctrack_type = xstrdup("proctrack/linuxproc");
	}

	if (mpi_default) {
		if ( ctl_conf_ptr->mpi_default ) {
			error( MULTIPLE_VALUE_MSG, "MpiDefault" );
			xfree( ctl_conf_ptr->mpi_default );
		}
		ctl_conf_ptr->mpi_default = mpi_default;
	}

	if ( plugindir ) {
		if ( ctl_conf_ptr->plugindir ) {
			error( MULTIPLE_VALUE_MSG, "PluginDir" );
			xfree( ctl_conf_ptr->plugindir );
		}
		ctl_conf_ptr->plugindir = plugindir;
	}

	if ( proctrack_type ) {
		if ( ctl_conf_ptr->proctrack_type ) {
			error( MULTIPLE_VALUE_MSG, "ProctrackType" );
			xfree( ctl_conf_ptr->proctrack_type );
		}
		ctl_conf_ptr->proctrack_type = proctrack_type;
	}
	
	if ( prolog ) {
		if ( ctl_conf_ptr->prolog ) {
			error (MULTIPLE_VALUE_MSG, "Prolog");
			xfree (ctl_conf_ptr->prolog);
		}
		ctl_conf_ptr->prolog = prolog;
	}

        if ( propagate_rlimits ) {
                if ( ctl_conf_ptr->propagate_rlimits ) {
                        error( MULTIPLE_VALUE_MSG,
                               "PropagateResourceLimits" );
                        xfree( ctl_conf_ptr->propagate_rlimits );
                }
                else if ( ctl_conf_ptr->propagate_rlimits_except ) {
                        error( "%s keyword conflicts with %s, using latter.",
                                "PropagateResourceLimitsExcept",
                                "PropagateResourceLimits");
                        xfree( ctl_conf_ptr->propagate_rlimits_except );
                }
                ctl_conf_ptr->propagate_rlimits = propagate_rlimits;
        }
        if ( propagate_rlimits_except ) {
                if ( ctl_conf_ptr->propagate_rlimits_except ) {
                        error( MULTIPLE_VALUE_MSG,
                               "PropagateResourceLimitsExcept" );
                        xfree( ctl_conf_ptr->propagate_rlimits_except );
                }
                else if ( ctl_conf_ptr->propagate_rlimits ) {
                        error( "%s keyword conflicts with %s, using latter.",
                                "PropagateResourceLimits",
                                "PropagateResourceLimitsExcept");
                        xfree( ctl_conf_ptr->propagate_rlimits );
                }
                ctl_conf_ptr->propagate_rlimits_except = 
                                                      propagate_rlimits_except;
        }

	if ( ret2service != -1) {
		if ( ctl_conf_ptr->ret2service != (uint16_t) NO_VAL)
			error (MULTIPLE_VALUE_MSG, "ReturnToService");
		if ((ret2service < 0) || (ret2service > 0xffff))
			error("ReturnToService=%ld is invalid", ret2service);
		else
			ctl_conf_ptr->ret2service = ret2service;
	}

	if ( sched_auth ) {
		if ( ctl_conf_ptr->schedauth ) {
			xfree( ctl_conf_ptr->schedauth );
		}
		ctl_conf_ptr->schedauth = sched_auth;
	}

	if ( sched_port != -1 ) {
		if (ctl_conf_ptr->schedport != (uint16_t) NO_VAL)
			 error (MULTIPLE_VALUE_MSG, "SchedPort");
		if (( sched_port < 1 ) || (sched_port > 0xffff))
			error( "SchedPort=%ld is invalid", sched_port );
		else
			ctl_conf_ptr->schedport = (uint16_t) sched_port;
	}

	if ( sched_rootfltr != -1 ) {
		if ( ctl_conf_ptr->schedrootfltr != (uint16_t) NO_VAL)
			error (MULTIPLE_VALUE_MSG, "SchedulerRootFilter");
		if ((sched_rootfltr < 0) || (sched_rootfltr > 0xffff))
			error("SchedulerRootFilter=%ld is invalid");
		else
			ctl_conf_ptr->schedrootfltr = (uint16_t) sched_rootfltr;
	}

	if ( sched_type ) {
		if ( ctl_conf_ptr->schedtype ) {
			xfree( ctl_conf_ptr->schedtype );
		}
		ctl_conf_ptr->schedtype = sched_type;
	}

	if ( select_type ) {
		if ( ctl_conf_ptr->select_type ) {
			xfree( ctl_conf_ptr->select_type );
		}
		ctl_conf_ptr->select_type = select_type;
	}

	if ( slurm_user ) {
		struct passwd *slurm_passwd;
		slurm_passwd = getpwnam(slurm_user);
		if (slurm_passwd == NULL) {
			error ("Invalid user for SlurmUser %s, ignored",
			       slurm_user);
		} else {
			if ( ctl_conf_ptr->slurm_user_name ) {
				error (MULTIPLE_VALUE_MSG, "SlurmUser");
				xfree (ctl_conf_ptr->slurm_user_name);
			}
			ctl_conf_ptr->slurm_user_name = slurm_user;
			if (slurm_passwd->pw_uid > 0xffff)
				error("SlurmUser numberic overflow, will be fixed soon");
			else
				ctl_conf_ptr->slurm_user_id = slurm_passwd->pw_uid;
		}
	}

	if ( slurmctld_debug != -1) {
		if ( ctl_conf_ptr->slurmctld_debug != (uint16_t) NO_VAL)
			error (MULTIPLE_VALUE_MSG, "SlurmctldDebug");
		if ((slurmctld_debug < 0) || (slurmctld_debug > 0xffff))
			error("SlurmctldDebug=%ld is invalid");
		else
			ctl_conf_ptr->slurmctld_debug = slurmctld_debug;
	}

	if ( slurmctld_pidfile ) {
		if ( ctl_conf_ptr->slurmctld_pidfile ) {
			error (MULTIPLE_VALUE_MSG, "SlurmctldPidFile");
			xfree (ctl_conf_ptr->slurmctld_pidfile);
		}
		ctl_conf_ptr->slurmctld_pidfile = slurmctld_pidfile;
	}

	if ( slurmctld_logfile ) {
		if ( ctl_conf_ptr->slurmctld_logfile ) {
			error (MULTIPLE_VALUE_MSG, "SlurmctldLogFile");
			xfree (ctl_conf_ptr->slurmctld_logfile);
		}
		ctl_conf_ptr->slurmctld_logfile = slurmctld_logfile;
	}

	if ( slurmctld_port != -1) {
		if ( ctl_conf_ptr->slurmctld_port != (uint32_t) NO_VAL)
			error (MULTIPLE_VALUE_MSG, "SlurmctldPort");
		else if (slurmctld_port < 0)
			error ("SlurmctldPort=%ld is invalid", 
			       slurmctld_port);
		else 
			ctl_conf_ptr->slurmctld_port = slurmctld_port;
	}

	if ( slurmctld_timeout != -1) {
		if ( ctl_conf_ptr->slurmctld_timeout != (uint16_t) NO_VAL)
			error (MULTIPLE_VALUE_MSG, "SlurmctldTimeout");
		if ((slurmctld_timeout < 0) || (slurmctld_timeout > 0xffff))
			error("SlurmctldTimeout=%ld is invalid", 
				slurmctld_timeout);
		else
			ctl_conf_ptr->slurmctld_timeout = slurmctld_timeout;
	}

	if ( slurmd_debug != -1) {
		if ( ctl_conf_ptr->slurmd_debug != (uint16_t) NO_VAL)
			error (MULTIPLE_VALUE_MSG, "SlurmdDebug");
		if ((slurmd_debug < 0) || (slurmd_debug > 0xffff))
			error("SlurmdDebug=%ld is invalid", slurmd_debug);
		else
			ctl_conf_ptr->slurmd_debug = slurmd_debug;
	}

	if ( slurmd_logfile ) {
		if ( ctl_conf_ptr->slurmd_logfile ) {
			error (MULTIPLE_VALUE_MSG, "SlurmdLogFile");
			xfree (ctl_conf_ptr->slurmd_logfile);
		}
		ctl_conf_ptr->slurmd_logfile = slurmd_logfile;
	}

	if ( slurmd_port != -1) {
		if ( ctl_conf_ptr->slurmd_port != (uint32_t) NO_VAL)
			error (MULTIPLE_VALUE_MSG, "SlurmdPort");
		else if (slurmd_port < 0)
			error ("SlurmdPort=%ld is invalid", slurmd_port);
		else
			ctl_conf_ptr->slurmd_port = slurmd_port;
	}

	if ( slurmd_spooldir ) {
		if ( ctl_conf_ptr->slurmd_spooldir ) {
			error (MULTIPLE_VALUE_MSG, "SlurmdSpoolDir");
			xfree (ctl_conf_ptr->slurmd_spooldir);
		}
		ctl_conf_ptr->slurmd_spooldir = slurmd_spooldir;
	}

	if ( slurmd_pidfile ) {
		if ( ctl_conf_ptr->slurmd_pidfile ) {
			error (MULTIPLE_VALUE_MSG, "SlurmdPidFile");
			xfree (ctl_conf_ptr->slurmd_pidfile);
		}
		ctl_conf_ptr->slurmd_pidfile = slurmd_pidfile;
	}

	if ( slurmd_timeout != -1) {
		if ( ctl_conf_ptr->slurmd_timeout != (uint16_t) NO_VAL)
			error (MULTIPLE_VALUE_MSG, "SlurmdTimeout");
		if ((slurmd_timeout < 0) || (slurmd_timeout > 0xffff))
			error("SlurmdTimeout=%ld is invalid", slurmd_timeout);
		else
			ctl_conf_ptr->slurmd_timeout = slurmd_timeout;
	}

	if ( srun_prolog ) {
		if ( ctl_conf_ptr->srun_prolog ) {
			error (MULTIPLE_VALUE_MSG, "SrunProlog");
			xfree (ctl_conf_ptr->srun_prolog);
		}
		ctl_conf_ptr->srun_prolog = srun_prolog;
	}

	if ( srun_epilog ) {
		if ( ctl_conf_ptr->srun_epilog ) {
			error (MULTIPLE_VALUE_MSG, "SrunEpilog");
			xfree (ctl_conf_ptr->srun_epilog);
		}
		ctl_conf_ptr->srun_epilog = srun_epilog;
	}

	if ( state_save_location ) {
		if ( ctl_conf_ptr->state_save_location ) {
			error (MULTIPLE_VALUE_MSG, "StateSaveLocation");
			xfree (ctl_conf_ptr->state_save_location);
		}
		ctl_conf_ptr->state_save_location = state_save_location;
	}

	if ( switch_type ) {
		if ( ctl_conf_ptr->switch_type ) {
			error (MULTIPLE_VALUE_MSG, "SwitchType");
			xfree (ctl_conf_ptr->switch_type);
		}
		ctl_conf_ptr->switch_type = switch_type;
	}

	if ( tmp_fs ) {
		if ( ctl_conf_ptr->tmp_fs ) {
			error (MULTIPLE_VALUE_MSG, "TmpFS");
			xfree (ctl_conf_ptr->tmp_fs);
		}
		ctl_conf_ptr->tmp_fs = tmp_fs;
	}

	if ( wait_time != -1) {
		if ( ctl_conf_ptr->wait_time != (uint16_t) NO_VAL)
			error (MULTIPLE_VALUE_MSG, "WaitTime");
		if ((wait_time < 0) || (wait_time > 0xffff))
			error("WaitTime=%ld is invalid", wait_time);
		else
			ctl_conf_ptr->wait_time = wait_time;
	}

	return 0;
}

/*
 * _parse_node_spec - just overwrite node specifications (toss the results)
 * IN/OUT in_line - input line, parsed info overwritten with white-space
 * IN  slurmd_hosts - if true then build a list of hosts on which slurmd runs,
 *	only useful for "scontrol show daemons" command
 * RET 0 if no error, otherwise an error code
 */
static int 
_parse_node_spec (char *in_line, bool slurmd_hosts) 
{
	int error_code;
	char *feature = NULL, *node_addr = NULL, *node_name = NULL;
	char *state = NULL, *reason=NULL;
	char *node_hostname = NULL;
	int cpus_val, real_memory_val, tmp_disk_val, weight_val;

	error_code = slurm_parser (in_line,
		"Feature=", 's', &feature, 
		"NodeAddr=", 's', &node_addr, 
		"NodeName=", 's', &node_name, 
		"NodeHostname=", 's', &node_hostname, 
		"Procs=", 'd', &cpus_val, 
		"RealMemory=", 'd', &real_memory_val, 
		"Reason=", 's', &reason, 
		"State=", 's', &state, 
		"TmpDisk=", 'd', &tmp_disk_val, 
		"Weight=", 'd', &weight_val, 
		"END");

	if (error_code)
		return error_code;

	if (node_name
	&&  (node_hostname || slurmd_hosts)) {
		all_slurmd_hosts = true;
		_register_conf_node_aliases(node_name, node_hostname);
	}

	xfree(feature);
	xfree(node_addr);
	xfree(node_name);
	xfree(node_hostname);
	xfree(reason);
	xfree(state);

	return error_code;
}

/*
 * _parse_part_spec - just overwrite partition specifications (toss the  
 *	results)
 * IN/OUT in_line - input line, parsed info overwritten with white-space
 * RET 0 if no error, otherwise an error code
 */
static int 
_parse_part_spec (char *in_line) 
{
	int error_code;
	char *allow_groups = NULL, *default_str = NULL, *hidden_str = NULL;
	char *partition = NULL, *max_time_str = NULL, *root_str = NULL;
	char *nodes = NULL, *shared_str = NULL, *state_str = NULL;
	int max_nodes_val, min_nodes_val;

	error_code = slurm_parser (in_line,
		"AllowGroups=", 's', &allow_groups, 
		"Default=", 's', &default_str, 
		"Hidden=", 's', &hidden_str,
		"PartitionName=", 's', &partition, 
		"RootOnly=", 's', &root_str, 
		"MaxTime=", 's', &max_time_str, 
		"MaxNodes=", 'd', &max_nodes_val, 
		"MinNodes=", 'd', &min_nodes_val, 
		"Nodes=", 's', &nodes, 
		"Shared=", 's', &shared_str, 
		"State=", 's', &state_str, 
		"END");

	xfree(allow_groups);
	xfree(default_str);
	xfree(hidden_str);
	xfree(partition);
	xfree(max_time_str);
	xfree(root_str);
	xfree(nodes);
	xfree(shared_str);
	xfree(state_str);

	return error_code;
}

/*
 * read_slurm_conf_ctl - load the slurm configuration from the configured 
 *	file. 
 * OUT ctl_conf_ptr - pointer to data structure to be filled
 * IN  slurmd_hosts - if true then build a list of hosts on which slurmd runs
 *	(only useful for "scontrol show daemons" command). Otherwise only
 *	record nodes in which NodeName and NodeHostname differ.
 * RET 0 if no error, otherwise an error code
 */
extern int 
read_slurm_conf_ctl (slurm_ctl_conf_t *ctl_conf_ptr, bool slurmd_hosts) 
{
	FILE *slurm_spec_file;	/* pointer to input data file */
	int line_num;		/* line number in input file */
	int line_size;		/* bytes in current input line */
	char in_line[BUF_SIZE];	/* input line */
	int error_code, i, j;

	assert (ctl_conf_ptr);
	init_slurm_conf (ctl_conf_ptr);

	if (ctl_conf_ptr->slurm_conf == NULL) {
		char *val = getenv("SLURM_CONF");

		if (val == NULL) {
			val = SLURM_CONFIG_FILE;
		}
		ctl_conf_ptr->slurm_conf = xstrdup (val);
	}
	slurm_spec_file = fopen (ctl_conf_ptr->slurm_conf, "r");
	if (slurm_spec_file == NULL) {
		fatal ("read_slurm_conf_ctl error opening file %s, %m", 
			ctl_conf_ptr->slurm_conf);
	}

	/* process the data file */
	line_num = 0;
	while (fgets (in_line, BUF_SIZE, slurm_spec_file) != NULL) {
		line_num++;
		line_size = strlen (in_line);
		if (line_size >= (BUF_SIZE - 1)) {
			error ("Line %d, of configuration file %s too long",
			       line_num, ctl_conf_ptr->slurm_conf);
			fclose (slurm_spec_file);
			return E2BIG;
			break;
		}		

		/* everything after a non-escaped "#" is a comment	*/
		/* replace comment flag "#" with a `\0' (End of string)	*/
		/* an escaped value "\#" is translated to "#"		*/
		/* this permitted embedded "#" in node/partition names	*/
		for (i = 0; i < line_size; i++) {
			if (in_line[i] == '\0')
				break;
			if (in_line[i] != '#')
				continue;
			if ((i > 0) && (in_line[i - 1] == '\\')) {
				for (j = i; j < line_size; j++) {
					in_line[j - 1] = in_line[j];
				}
				line_size--;
				continue;
			}	
			in_line[i] = '\0';
			break;
		}		

		/* parse what is left */
		
		/* overall configuration parameters */
		if ((error_code = parse_config_spec (in_line, ctl_conf_ptr))) {
			fclose (slurm_spec_file);
			return error_code;
		}

		/* node configuration parameters */
		if ((error_code = _parse_node_spec (in_line, slurmd_hosts))) {
			fclose (slurm_spec_file);
			return error_code;
		}		

		/* partition configuration parameters */
		if ((error_code = _parse_part_spec (in_line))) {
			fclose (slurm_spec_file);
			return error_code;
		}		

		/* report any leftover strings on input line */
		report_leftover (in_line, line_num);
	}

	fclose (slurm_spec_file);
	validate_config (ctl_conf_ptr);
	return SLURM_SUCCESS;
}

/* 
 * report_leftover - report any un-parsed (non-whitespace) characters on the
 * configuration input line (we over-write parsed characters with whitespace).
 * IN in_line - what is left of the configuration input line.
 * IN line_num - line number of the configuration file.
 */
void
report_leftover (char *in_line, int line_num)
{
	int i;

	for (i = 0; i < strlen (in_line); i++) {
		if (isspace ((int) in_line[i]) || (in_line[i] == '\n'))
			continue;
		error ("Ignored input on line %d of configuration: %s",
			line_num, &in_line[i]);
		break;
	}
}

/* validate configuration
 *
 * IN/OUT ctl_conf_ptr - a configuration as loaded by read_slurm_conf_ctl
 *
 * NOTE: a backup_controller or control_machine of "localhost" are over-written
 *	with this machine's name.
 * NOTE: if backup_addr is NULL, it is over-written by backup_controller
 * NOTE: if control_addr is NULL, it is over-written by control_machine
 */
void
validate_config (slurm_ctl_conf_t *ctl_conf_ptr)
{
	if ((ctl_conf_ptr->backup_controller != NULL) &&
	    (strcasecmp("localhost", ctl_conf_ptr->backup_controller) == 0)) {
		xfree (ctl_conf_ptr->backup_controller);
		ctl_conf_ptr->backup_controller = xmalloc (MAX_NAME_LEN);
		if ( getnodename (ctl_conf_ptr->backup_controller, 
		                  MAX_NAME_LEN) ) 
			fatal ("getnodename: %m");
	}

	if ((ctl_conf_ptr->backup_addr == NULL) && 
	    (ctl_conf_ptr->backup_controller != NULL))
		ctl_conf_ptr->backup_addr = 
				xstrdup (ctl_conf_ptr->backup_controller);

	if ((ctl_conf_ptr->backup_controller == NULL) && 
	    (ctl_conf_ptr->backup_addr != NULL)) {
		error ("BackupAddr specified without BackupController");
		xfree (ctl_conf_ptr->backup_addr);
	}

	if (ctl_conf_ptr->control_machine == NULL)
		fatal ("validate_config: ControlMachine not specified.");
	else if (strcasecmp("localhost", ctl_conf_ptr->control_machine) == 0) {
		xfree (ctl_conf_ptr->control_machine);
		ctl_conf_ptr->control_machine = xmalloc (MAX_NAME_LEN);
		if ( getnodename (ctl_conf_ptr->control_machine, 
		                  MAX_NAME_LEN) ) 
			fatal ("getnodename: %m");
	}

	if ((ctl_conf_ptr->control_addr == NULL) && 
	    (ctl_conf_ptr->control_machine != NULL))
		ctl_conf_ptr->control_addr = 
				xstrdup (ctl_conf_ptr->control_machine);

	if ((ctl_conf_ptr->backup_controller != NULL) && 
	    (strcmp (ctl_conf_ptr->backup_controller, 
	             ctl_conf_ptr->control_machine) == 0)) {
		error ("ControlMachine and BackupController identical");
		xfree (ctl_conf_ptr->backup_addr);
		xfree (ctl_conf_ptr->backup_controller);
	}

	if (ctl_conf_ptr->job_credential_private_key == NULL)
		fatal ("JobCredentialPrivateKey not set");
	if (ctl_conf_ptr->job_credential_public_certificate == NULL)
		fatal ("JobCredentialPublicCertificate not set");

	if (ctl_conf_ptr->max_job_cnt < 1)
		fatal ("MaxJobCount=%u, No jobs permitted",
		       ctl_conf_ptr->max_job_cnt);

	if (ctl_conf_ptr->authtype == NULL)
		ctl_conf_ptr->authtype = xstrdup(DEFAULT_AUTH_TYPE);

	if (ctl_conf_ptr->checkpoint_type == NULL)
		 ctl_conf_ptr->checkpoint_type = 
			xstrdup(DEFAULT_CHECKPOINT_TYPE);

	if (ctl_conf_ptr->fast_schedule == (uint16_t) NO_VAL)
		ctl_conf_ptr->fast_schedule = DEFAULT_FAST_SCHEDULE;

	if (ctl_conf_ptr->first_job_id == (uint32_t) NO_VAL)
		ctl_conf_ptr->first_job_id = DEFAULT_FIRST_JOB_ID;

	if (ctl_conf_ptr->heartbeat_interval == (uint16_t) NO_VAL)
		ctl_conf_ptr->heartbeat_interval = DEFAULT_HEARTBEAT_INTERVAL;

	if (ctl_conf_ptr->inactive_limit == (uint16_t) NO_VAL)
		ctl_conf_ptr->inactive_limit = DEFAULT_INACTIVE_LIMIT;

	if (ctl_conf_ptr->job_acct_loc == NULL)
		ctl_conf_ptr->job_acct_loc = xstrdup(DEFAULT_JOB_ACCT_LOC);

	if (ctl_conf_ptr->job_acct_parameters == NULL)
		ctl_conf_ptr->job_acct_parameters =
				xstrdup(DEFAULT_JOB_ACCT_PARAMETERS);

	if (ctl_conf_ptr->job_acct_type == NULL)
		ctl_conf_ptr->job_acct_type = xstrdup(DEFAULT_JOB_ACCT_TYPE);

	if (ctl_conf_ptr->job_comp_type == NULL)
		ctl_conf_ptr->job_comp_type = xstrdup(DEFAULT_JOB_COMP_TYPE);

	if (ctl_conf_ptr->kill_wait == (uint16_t) NO_VAL)
		ctl_conf_ptr->kill_wait = DEFAULT_KILL_WAIT;

	if (ctl_conf_ptr->max_job_cnt == (uint16_t) NO_VAL)
		ctl_conf_ptr->max_job_cnt = DEFAULT_MAX_JOB_COUNT;

	if (ctl_conf_ptr->min_job_age == (uint16_t) NO_VAL)
		ctl_conf_ptr->min_job_age = DEFAULT_MIN_JOB_AGE;

	if (ctl_conf_ptr->mpi_default == NULL)
		ctl_conf_ptr->mpi_default = xstrdup(DEFAULT_MPI_DEFAULT);
	if (ctl_conf_ptr->plugindir == NULL)
		ctl_conf_ptr->plugindir = xstrdup(SLURM_PLUGIN_PATH);

	if (ctl_conf_ptr->switch_type == NULL)
		ctl_conf_ptr->switch_type = xstrdup(DEFAULT_SWITCH_TYPE);

	if (ctl_conf_ptr->proctrack_type == NULL) {
		if (!strcmp(ctl_conf_ptr->switch_type,"switch/elan"))
			ctl_conf_ptr->proctrack_type = 
					xstrdup("proctrack/rms");
		else
			ctl_conf_ptr->proctrack_type = 
					xstrdup(DEFAULT_PROCTRACK_TYPE);
	}
	if ((!strcmp(ctl_conf_ptr->switch_type,   "switch/elan"))
	&&  (!strcmp(ctl_conf_ptr->proctrack_type,"proctrack/linuxproc")))
		fatal("proctrack/linuxproc is incompatable with switch/elan");

        if (ctl_conf_ptr->propagate_rlimits_except) {
                if ((parse_rlimits( ctl_conf_ptr->propagate_rlimits_except,
                                   NO_PROPAGATE_RLIMITS )) < 0)
                        fatal( "Bad PropagateResourceLimitsExcept: %s",
                                ctl_conf_ptr->propagate_rlimits_except );
        }
        else {
                if (ctl_conf_ptr->propagate_rlimits == NULL)
                        ctl_conf_ptr->propagate_rlimits = xstrdup( "ALL" );
                if ((parse_rlimits( ctl_conf_ptr->propagate_rlimits,
                                   PROPAGATE_RLIMITS )) < 0)
                        fatal( "Bad PropagateResourceLimits: %s",
                                ctl_conf_ptr->propagate_rlimits );
        }

	if (ctl_conf_ptr->ret2service == (uint16_t) NO_VAL)
		ctl_conf_ptr->ret2service = DEFAULT_RETURN_TO_SERVICE;

	if (ctl_conf_ptr->schedrootfltr == (uint16_t) NO_VAL)
		ctl_conf_ptr->schedrootfltr = DEFAULT_SCHEDROOTFILTER;

	if (ctl_conf_ptr->schedtype == NULL)
		ctl_conf_ptr->schedtype = xstrdup(DEFAULT_SCHEDTYPE);

	if (ctl_conf_ptr->select_type == NULL)
		ctl_conf_ptr->select_type = xstrdup(DEFAULT_SELECT_TYPE);

	if (ctl_conf_ptr->slurm_user_name == NULL) {
		ctl_conf_ptr->slurm_user_name = xstrdup("root");
		ctl_conf_ptr->slurm_user_id   = 0;
	}

	if (ctl_conf_ptr->slurmctld_debug != (uint16_t) NO_VAL)
		_normalize_debug_level(&ctl_conf_ptr->slurmctld_debug);
	else
		ctl_conf_ptr->slurmctld_debug = LOG_LEVEL_INFO;

	if (ctl_conf_ptr->slurmctld_pidfile == NULL)
		ctl_conf_ptr->slurmctld_pidfile =
			xstrdup(DEFAULT_SLURMCTLD_PIDFILE);

	if (ctl_conf_ptr->slurmctld_port == (uint32_t) NO_VAL) 
		ctl_conf_ptr->slurmctld_port = SLURMCTLD_PORT;

	if (ctl_conf_ptr->slurmctld_timeout == (uint16_t) NO_VAL)
		ctl_conf_ptr->slurmctld_timeout = DEFAULT_SLURMCTLD_TIMEOUT;

	if (ctl_conf_ptr->slurmd_debug != (uint16_t) NO_VAL)
		_normalize_debug_level(&ctl_conf_ptr->slurmd_debug);
	else
		ctl_conf_ptr->slurmd_debug = LOG_LEVEL_INFO;

	if (ctl_conf_ptr->slurmd_pidfile == NULL)
		ctl_conf_ptr->slurmd_pidfile = xstrdup(DEFAULT_SLURMD_PIDFILE);

	if (ctl_conf_ptr->slurmd_port == (uint32_t) NO_VAL) 
		ctl_conf_ptr->slurmd_port = SLURMD_PORT;

	if (ctl_conf_ptr->slurmd_spooldir == NULL)
		ctl_conf_ptr->slurmd_spooldir = xstrdup(DEFAULT_SPOOLDIR);

	if (ctl_conf_ptr->slurmd_timeout == (uint16_t) NO_VAL)
		ctl_conf_ptr->slurmd_timeout = DEFAULT_SLURMD_TIMEOUT;

	if (ctl_conf_ptr->state_save_location == NULL)
		ctl_conf_ptr->state_save_location = xstrdup(
				DEFAULT_SAVE_STATE_LOC);

	/* see above for switch_type, order dependent */

	if (ctl_conf_ptr->tmp_fs == NULL)
		ctl_conf_ptr->tmp_fs = xstrdup(DEFAULT_TMP_FS);

	if (ctl_conf_ptr->wait_time == (uint16_t) NO_VAL)
		ctl_conf_ptr->wait_time = DEFAULT_WAIT_TIME;
}

/* Normalize supplied debug level to be in range per log.h definitions */
static void _normalize_debug_level(uint16_t *level)
{
	if (*level > LOG_LEVEL_DEBUG3) {
		error("Normalizing debug level from %u to %d", 
		      *level, LOG_LEVEL_DEBUG3);
		*level = LOG_LEVEL_DEBUG3;
	}
	/* level is uint16, always > LOG_LEVEL_QUIET(0), can't underflow */
}
