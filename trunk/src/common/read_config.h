/*****************************************************************************
 *  read_config.h - definitions for reading the overall slurm configuration 
 *  file
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2008 Vijay Ramasubramanian.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Mette <jette1@llnl.gov>.
 *  LLNL-CODE-402394.
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

#ifndef _READ_CONFIG_H
#define _READ_CONFIG_H

#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_protocol_socket_common.h"
#include "src/common/parse_config.h"

extern slurm_ctl_conf_t slurmctld_conf;
extern char *default_slurm_config_file;
extern char *default_plugin_path;
extern char *default_plugstack;

enum {
	ACCOUNTING_ENFORCE_NONE,
	ACCOUNTING_ENFORCE_YES,
	ACCOUNTING_ENFORCE_WITH_LIMITS
};

#define DEFAULT_ACCOUNTING_ENFORCE  ACCOUNTING_ENFORCE_NONE
#define DEFAULT_ACCOUNTING_STORAGE_TYPE "accounting_storage/none"
#define DEFAULT_AUTH_TYPE          "auth/munge"
#define DEFAULT_CACHE_GROUPS        0
#define DEFAULT_COMPLETE_WAIT       0
#define DEFAULT_CRYPTO_TYPE        "crypto/munge"
#define DEFAULT_EPILOG_MSG_TIME     2000
#define DEFAULT_FAST_SCHEDULE       1
#define DEFAULT_FIRST_JOB_ID        1
#define DEFAULT_GET_ENV_TIMEOUT     2
/* NOTE: DEFAULT_INACTIVE_LIMIT must be 0 for Blue Gene/L systems */
#define DEFAULT_INACTIVE_LIMIT      0
#define DEFAULT_JOB_ACCT_GATHER_TYPE  "jobacct_gather/none"
#define JOB_ACCT_GATHER_TYPE_NONE "jobacct_gather/none"
#define DEFAULT_JOB_ACCT_GATHER_FREQ  30
#define ACCOUNTING_STORAGE_TYPE_NONE "accounting_storage/none"
#define DEFAULT_DISABLE_ROOT_JOBS   0
#define DEFAULT_ENFORCE_PART_LIMITS 0
#define DEFAULT_JOB_COMP_TYPE       "jobcomp/none"
#define DEFAULT_JOB_COMP_LOC        "/var/log/slurm_jobcomp.log"
#define DEFAULT_KILL_TREE           0
#define DEFAULT_KILL_WAIT           30
#define DEFAULT_MAIL_PROG           "/bin/mail"
#define DEFAULT_MAX_JOB_COUNT       5000
#define DEFAULT_MEM_PER_CPU         0
#define DEFAULT_MAX_MEM_PER_CPU     0
#define DEFAULT_MIN_JOB_AGE         300
#define DEFAULT_MPI_DEFAULT         "none"
#define DEFAULT_MSG_TIMEOUT         10
#ifdef HAVE_AIX		/* AIX specific default configuration parameters */
#  define DEFAULT_CHECKPOINT_TYPE   "checkpoint/aix"
#  define DEFAULT_PROCTRACK_TYPE    "proctrack/aix"
#else
#  define DEFAULT_CHECKPOINT_TYPE   "checkpoint/none"
#  define DEFAULT_PROCTRACK_TYPE    "proctrack/pgid"
#endif
#define DEFAULT_PRIORITY_DECAY      604800 /* 7 days */
#define DEFAULT_PRIORITY_TYPE       "priority/basic"
#define DEFAULT_PROPAGATE_PRIO_PROCESS 0
#define DEFAULT_RETURN_TO_SERVICE   0
#define DEFAULT_RESUME_RATE         300
#define DEFAULT_SAVE_STATE_LOC      "/tmp"
#define DEFAULT_SCHEDROOTFILTER     1
#define DEFAULT_SCHEDULER_PORT      7321
#define DEFAULT_SCHED_TIME_SLICE    30
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
#define DEFAULT_STORAGE_HOST        "localhost"
#define DEFAULT_STORAGE_LOC         "/var/log/slurm_jobacct.log"
#define DEFAULT_STORAGE_USER        "root"
#define DEFAULT_STORAGE_PORT        0
#define DEFAULT_SUSPEND_RATE        60
#define DEFAULT_SUSPEND_TIME        0
#define DEFAULT_SWITCH_TYPE         "switch/none"
#define DEFAULT_TASK_PLUGIN         "task/none"
#define DEFAULT_TMP_FS              "/tmp"
#define DEFAULT_WAIT_TIME           0
#define DEFAULT_TREE_WIDTH          50
#define DEFAULT_UNKILLABLE_TIMEOUT  60 /* seconds */

typedef struct slurm_conf_node {
	char *nodenames;
	char *hostnames;
	char *addresses;
	char *feature;		/* arbitrary list of features associated */
	uint16_t port;
	uint16_t cpus;		/* count of cpus running on the node */
	uint16_t sockets;       /* number of sockets per node */
	uint16_t cores;         /* number of cores per CPU */
	uint16_t threads;       /* number of threads per core */
	uint32_t real_memory;	/* MB real memory on the node */
	char *reason;
	char *state;
	uint32_t tmp_disk;	/* MB total storage in TMP_FS file system */
	uint32_t weight;	/* arbitrary priority of node for 
				 * scheduling work on */
} slurm_conf_node_t;

typedef struct slurm_conf_partition {
	uint16_t disable_root_jobs; /* if set then user root can't run
				     * jobs if NO_VAL use global
				     * default */
	char	*name;		/* name of the partition */
	bool     hidden_flag;	/* 1 if hidden by default */
	uint32_t max_time;	/* minutes or INFINITE */
	uint32_t max_nodes;	/* per job or INFINITE */
	uint32_t min_nodes;	/* per job */
	uint32_t total_nodes;	/* total number of nodes in the partition */
	uint32_t total_cpus;	/* total number of cpus in the partition */
	uint16_t priority;	/* scheduling priority for jobs */
	bool     root_only_flag;/* 1 if allocate/submit RPC can only be 
				   issued by user root */
	uint16_t max_share;	/* number of jobs to gang schedule */
	bool     state_up_flag;	/* 1 if state is up, 0 if down */
	char *nodes;		/* comma delimited list names of nodes */
	char *allow_groups;	/* comma delimited list of groups, 
				 * NULL indicates all */
	bool default_flag;
} slurm_conf_partition_t;

typedef struct slurm_conf_downnodes {
	char *nodenames;
	char *reason;
	char *state;
} slurm_conf_downnodes_t;

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
 * NOTE: Caller must NOT be holding slurm_conf_lock().
 */
extern int slurm_conf_init(const char *file_name);

/*
 * slurm_conf_reinit - reload the slurm configuration from a file.
 * IN file_name - name of the slurm configuration file to be read
 *	If file_name is NULL, then this routine tries to use
 *	the value in the SLURM_CONF env variable.  Failing that,
 *	it uses the compiled-in default file name.
 *	Unlike slurm_conf_init, slurm_conf_reinit will always reread the
 *	file and reinitialize the configuration structures.
 * RET SLURM_SUCCESS if conf file is reinitialized, otherwise SLURM_ERROR.
 * NOTE: Caller must NOT be holding slurm_conf_lock().
 */
extern int slurm_conf_reinit(const char *file_name);

/* 
 * slurm_conf_mutex_init - init the slurm_conf mutex
 */
extern void slurm_conf_mutex_init(void);

/* slurm_conf_install_fork_handlers
 * installs what to do with a fork with the conf mutex
 */
void slurm_conf_install_fork_handlers();

/*
 * NOTE: Caller must NOT be holding slurm_conf_lock().
 */
extern int slurm_conf_destroy(void);

extern slurm_ctl_conf_t *slurm_conf_lock(void);

extern void slurm_conf_unlock(void);

/*
 * Set "ptr_array" with the pointer to an array of pointers to
 * slurm_conf_node_t structures.
 * 
 * Return value is the length of the array.
 */
extern int slurm_conf_nodename_array(slurm_conf_node_t **ptr_array[]);

/*
 * Set "ptr_array" with the pointer to an array of pointers to
 * slurm_conf_partition_t structures.
 * 
 * Return value is the length of the array.
 */
extern int slurm_conf_partition_array(slurm_conf_partition_t **ptr_array[]);

/*
 * Set "ptr_array" with the pointer to an array of pointers to
 * slurm_conf_node_t structures.
 * 
 * Return value is the length of the array.
 */
extern int slurm_conf_downnodes_array(slurm_conf_downnodes_t **ptr_array[]);

/*
 * slurm_conf_get_hostname - Return the NodeHostname for given NodeName
 *
 * Returned string was allocated with xmalloc(), and must be freed by
 * the caller using xfree().
 *
 * NOTE: Caller must NOT be holding slurm_conf_lock().
 */
extern char *slurm_conf_get_hostname(const char *node_name);

/*
 * slurm_conf_get_nodename - Return the NodeName for given NodeHostname
 *
 * Returned string was allocated with xmalloc(), and must be freed by
 * the caller using xfree().
 *
 * NOTE: Caller must NOT be holding slurm_conf_lock().
 */
extern char *slurm_conf_get_nodename(const char *node_hostname);

/*
 * slurm_conf_get_aliased_nodename - Return the NodeName matching an alias
 * of the local hostname
 *
 * Returned string was allocated with xmalloc(), and must be freed by
 * the caller using xfree().
 *
 * NOTE: Caller must NOT be holding slurm_conf_lock().
 */
extern char *slurm_conf_get_aliased_nodename(void);

/*
 * slurm_conf_get_port - Return the port for a given NodeName
 *
 * NOTE: Caller must NOT be holding slurm_conf_lock().
 */
extern uint16_t slurm_conf_get_port(const char *node_name);

/*
 * slurm_conf_get_addr - Return the slurm_addr for a given NodeName in
 *	the parameter "address".  The return code is SLURM_SUCCESS on success,
 *	and SLURM_FAILURE if the address lookup failed.
 *
 * NOTE: Caller must NOT be holding slurm_conf_lock().
 */
extern int slurm_conf_get_addr(const char *node_name, slurm_addr *address);

/*
 * slurm_conf_get_cpus_sct -
 * Return the cpus, sockets, cores, and threads configured for a given NodeName
 * Returns SLURM_SUCCESS on success, SLURM_FAILURE on failure.
 *
 * NOTE: Caller must NOT be holding slurm_conf_lock().
 */
extern int slurm_conf_get_cpus_sct(const char *node_name,
				   uint16_t *procs, uint16_t *sockets,
				   uint16_t *cores, uint16_t *threads);

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
 * IN purge_node_hash - purge system-wide node hash table if set,
 *			set to zero if clearing private copy of config data
 */
extern void free_slurm_conf (slurm_ctl_conf_t *ctl_conf_ptr,
			     bool purge_node_hash);

/*
 * gethostname_short - equivalent to gethostname(), but return only the first 
 *      component of the fully qualified name (e.g. "linux123.foo.bar" 
 *      becomes "linux123") 
 * NOTE: NodeName in the config may be different from real hostname.
 *       Use get_conf_node_name() to get the former.
 */
extern int gethostname_short (char *name, size_t len);

/*
 * Replace first "%h" in path string with NodeHostname.
 * Replace first "%n" in path string with NodeName.
 *
 * NOTE: Caller should be holding slurm_conf_lock() when calling this function.
 *
 * Returns an xmalloc()ed string which the caller must free with xfree().
 */
extern char *slurm_conf_expand_slurmd_path(const char *path,
					   const char *node_name);

/*
 * debug_flags2str - convert a DebugFlags uint32_t to the equivalent string
 * Returns an xmalloc()ed string which the caller must free with xfree().
 */
extern char *debug_flags2str(uint32_t debug_flags);

/*
 * debug_str2flags - Convert a DebugFlags string to the equivalent uint32_t
 * Returns NO_VAL if invalid
 */
extern uint32_t debug_str2flags(char *debug_flags);

#endif /* !_READ_CONFIG_H */
