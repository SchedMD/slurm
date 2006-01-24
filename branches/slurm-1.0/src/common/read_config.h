/*****************************************************************************
 *  read_config.h - definitions for reading the overall slurm configuration 
 *  file
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Mette <jette1@llnl.gov>.
 *  UCRL-CODE-217948.
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

#ifndef _READ_CONFIG_H
#define _READ_CONFIG_H

#include "src/common/slurm_protocol_defs.h"

#define DEFAULT_AUTH_TYPE          "auth/none"
#define DEFAULT_FAST_SCHEDULE       1
#define DEFAULT_FIRST_JOB_ID        1
#define DEFAULT_HEARTBEAT_INTERVAL  60
/* NOTE: DEFAULT_INACTIVE_LIMIT must be 0 for Blue Gene/L systems */
#define DEFAULT_INACTIVE_LIMIT      0
#define DEFAULT_JOB_ACCT_LOC        "/var/log/slurm_accounting.log"
#define DEFAULT_JOB_ACCT_PARAMETERS "Frequency=30"
#define DEFAULT_JOB_ACCT_TYPE       "jobacct/none"
#define DEFAULT_JOB_COMP_TYPE       "jobcomp/none"
#define DEFAULT_KILL_TREE           0
#define DEFAULT_KILL_WAIT           30
#define DEFAULT_MAX_JOB_COUNT       2000
#define DEFAULT_MIN_JOB_AGE         300
#define DEFAULT_MPI_DEFAULT         "none"
#define DEFAULT_CACHE_GROUPS        0
#ifdef HAVE_AIX		/* AIX specific default configuration parameters */
#  define DEFAULT_CHECKPOINT_TYPE   "checkpoint/aix"
#  define DEFAULT_PROCTRACK_TYPE    "proctrack/aix"
#else
#  define DEFAULT_CHECKPOINT_TYPE   "checkpoint/none"
#  define DEFAULT_PROCTRACK_TYPE    "proctrack/pgid"
#endif
#define DEFAULT_RETURN_TO_SERVICE   0
#define DEFAULT_SAVE_STATE_LOC      "/tmp"
#define DEFAULT_SCHEDROOTFILTER     1
#define DEFAULT_SCHEDTYPE           "sched/builtin"
#ifdef HAVE_BG		/* Blue Gene specific default configuration parameters */
#  define DEFAULT_SELECT_TYPE       "select/bluegene"
#else
#  define DEFAULT_SELECT_TYPE       "select/linear"
#endif
#define DEFAULT_SLURMCTLD_PIDFILE   "/var/run/slurmctld.pid"
#define DEFAULT_SLURMCTLD_TIMEOUT   120
#define DEFAULT_SLURMD_PIDFILE      "/var/run/slurmd.pid"
#define DEFAULT_SLURMD_TIMEOUT      300
#define DEFAULT_SPOOLDIR            "/var/spool/slurmd"
#define DEFAULT_SWITCH_TYPE         "switch/none"
#define DEFAULT_TASK_PLUGIN         "task/none"
#define DEFAULT_TMP_FS              "/tmp"
#define DEFAULT_WAIT_TIME           0

/* 
 * init_slurm_conf - initialize or re-initialize the slurm configuration 
 *	values defaults (NULL or NO_VAL). Note that the configuration
 *	file pathname (slurm_conf) is not changed.    
 * IN/OUT ctl_conf_ptr - pointer to data structure to be initialized
 */
extern void init_slurm_conf (slurm_ctl_conf_t *ctl_conf_ptr);

/* 
 * free_slurm_conf - free all storage associated with a slurm_ctl_conf_t.   
 * IN/OUT ctl_conf_ptr - pointer to data structure to be freed
 */
extern void free_slurm_conf (slurm_ctl_conf_t *ctl_conf_ptr);

/*
 * getnodename - equivalent to gethostname(), but return only the first 
 *      component of the fully qualified name (e.g. "linux123.foo.bar" 
 *      becomes "linux123") 
 * NOTE: NodeName in the config may be different from real hostname.
 *       Use get_conf_node_name() to get the former.
 */
extern int getnodename (char *name, size_t len);

/*
 * get_conf_node_hostname - Return the NodeHostname for given NodeName
 */
extern char *get_conf_node_hostname(char *node_name);

/*
 * get_conf_node_name - Return the NodeName for given NodeHostname
 */
extern char *get_conf_node_name(char *node_hostname);

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
extern int parse_config_spec (char *in_line, slurm_ctl_conf_t *ctl_conf_ptr);

/*
 * read_slurm_conf_ctl - load the slurm configuration from the configured 
 *	file. 
 * OUT ctl_conf_ptr - pointer to data structure to be filled
 * IN  slurmd_hosts - if true then build a list of hosts on which slurmd runs
 *	(only useful for "scontrol show daemons" command). Otherwise only 
 *	record nodes in which NodeName and NodeHostname differ.
 * RET 0 if no error, otherwise an error code
 */
extern int read_slurm_conf_ctl (slurm_ctl_conf_t *ctl_conf_ptr,
	bool slurmd_hosts);

/* 
 * report_leftover - report any un-parsed (non-whitespace) characters on the
 * configuration input line (we over-write parsed characters with whitespace).
 * IN in_line - what is left of the configuration input line.
 * IN line_num - line number of the configuration file.
 */
extern void report_leftover (char *in_line, int line_num);

/* validate configuration
 *
 * IN/OUT ctl_conf_ptr - a configuration as loaded by read_slurm_conf_ctl
 *
 * NOTE: default slurmctld and slurmd ports are built thus:
 *	if SLURMCTLD_PORT/SLURMD_PORT are set then
 *		get the port number based upon a look-up in /etc/services
 *		if the lookup fails then translate SLURMCTLD_PORT/SLURMD_PORT 
 *		into a number
 *	These port numbers are overridden if set in the configuration file
 * NOTE: a backup_controller or control_machine of "localhost" are over-written
 *	with this machine's name.
 * NOTE: if backup_addr is NULL, it is over-written by backup_controller
 * NOTE: if control_addr is NULL, it is over-written by control_machine
 */
extern void validate_config (slurm_ctl_conf_t *ctl_conf_ptr);

#endif /* !_READ_CONFIG_H */
