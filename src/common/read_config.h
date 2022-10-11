/*****************************************************************************
 *  read_config.h - definitions for reading the overall slurm configuration
 *  file
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2008 Vijay Ramasubramanian.
 *  Portions Copyright (C) 2010-2016 SchedMD <https://www.schedmd.com>.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Mette <jette1@llnl.gov>.
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

#ifndef _READ_CONFIG_H
#define _READ_CONFIG_H

#include "config.h"

#include "src/common/list.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/parse_config.h"
#include "src/common/run_in_daemon.h"

extern slurm_conf_t slurm_conf;
extern char *default_slurm_config_file;
extern char *default_plugin_path;

#ifndef NDEBUG
extern uint16_t drop_priv_flag;
#endif

/*
 * We can't include node_conf.h to get node_record_t because node_conf.h
 * includes read_config.h and creates a circular dependency. We create the
 * typedef so that we don't have to move the struct around.
 */
#ifndef node_record_t
typedef struct node_record node_record_t;
#endif

#define ACCOUNTING_ENFORCE_ASSOCS SLURM_BIT(0)
#define ACCOUNTING_ENFORCE_LIMITS SLURM_BIT(1)
#define ACCOUNTING_ENFORCE_WCKEYS SLURM_BIT(2)
#define ACCOUNTING_ENFORCE_QOS    SLURM_BIT(3)
#define ACCOUNTING_ENFORCE_SAFE   SLURM_BIT(4)
#define ACCOUNTING_ENFORCE_NO_JOBS SLURM_BIT(5)
#define ACCOUNTING_ENFORCE_NO_STEPS SLURM_BIT(6)
#define ACCOUNTING_ENFORCE_TRES   SLURM_BIT(7)

#define DEFAULT_ACCOUNTING_TRES  "cpu,mem,energy,node,billing,fs/disk,vmem,pages"
#define DEFAULT_ACCOUNTING_DB      "slurm_acct_db"
#define DEFAULT_ACCOUNTING_ENFORCE  0
#define DEFAULT_ACCOUNTING_STORAGE_TYPE "accounting_storage/none"
#define DEFAULT_AUTH_TYPE          "auth/munge"
#define DEFAULT_AUTH_TOKEN_LIFESPAN 1800
#define DEFAULT_BATCH_START_TIMEOUT 10
#define DEFAULT_BCAST_EXCLUDE       "/lib,/usr/lib,/lib64,/usr/lib64"
#define DEFAULT_COMPLETE_WAIT       0
#define DEFAULT_CRED_TYPE           "cred/munge"
#define DEFAULT_EPILOG_MSG_TIME     2000
#define DEFAULT_EXT_SENSORS_TYPE    "ext_sensors/none"
#define DEFAULT_FIRST_JOB_ID        1
#define DEFAULT_GET_ENV_TIMEOUT     2
#define DEFAULT_GROUP_TIME          600
#define DEFAULT_GROUP_FORCE         1	/* if set, update group membership
					 * info even if no updates to
					 * /etc/group file */
/* NOTE: DEFAULT_INACTIVE_LIMIT must be 0 for Blue Gene/L systems */
#define DEFAULT_INACTIVE_LIMIT      0
#define DEFAULT_INTERACTIVE_STEP_OPTS "--interactive --preserve-env --pty $SHELL"
#define DEFAULT_JOB_ACCT_GATHER_TYPE  "jobacct_gather/none"
#define JOB_ACCT_GATHER_TYPE_NONE "jobacct_gather/none"
#define DEFAULT_JOB_ACCT_GATHER_FREQ  "30"
#define DEFAULT_ACCT_GATHER_ENERGY_TYPE "acct_gather_energy/none"
#define DEFAULT_ACCT_GATHER_PROFILE_TYPE "acct_gather_profile/none"
#define DEFAULT_ACCT_GATHER_INTERCONNECT_TYPE "acct_gather_interconnect/none"
#define DEFAULT_ACCT_GATHER_FILESYSTEM_TYPE "acct_gather_filesystem/none"
#define ACCOUNTING_STORAGE_TYPE_NONE "accounting_storage/none"
#define DEFAULT_CORE_SPEC_PLUGIN    "core_spec/none"
#define DEFAULT_ENFORCE_PART_LIMITS 0
#define DEFAULT_JOB_COMP_TYPE       "jobcomp/none"
#define DEFAULT_JOB_COMP_LOC        "/var/log/slurm_jobcomp.log"
#define DEFAULT_JOB_COMP_DB         "slurm_jobcomp_db"
#if defined HAVE_NATIVE_CRAY
#  define DEFAULT_ALLOW_SPEC_RESOURCE_USAGE 1
#  define DEFAULT_JOB_CONTAINER_PLUGIN  "job_container/cncu"
#else
#  define DEFAULT_ALLOW_SPEC_RESOURCE_USAGE 0
#  define DEFAULT_JOB_CONTAINER_PLUGIN "job_container/none"
#endif
#define DEFAULT_KEEPALIVE_TIME (NO_VAL)
#define DEFAULT_KILL_ON_BAD_EXIT    0
#define DEFAULT_KILL_TREE           0
#define DEFAULT_KILL_WAIT           30
#define DEFAULT_LAUNCH_TYPE         "launch/slurm"
#define DEFAULT_MAIL_PROG           "/bin/mail"
#define DEFAULT_MAIL_PROG_ALT       "/usr/bin/mail"
#define DEFAULT_MAX_ARRAY_SIZE      1001
#define DEFAULT_MAX_DBD_MSGS        10000
#define DEFAULT_MAX_JOB_COUNT       10000
#define DEFAULT_MAX_JOB_ID          0x03ff0000
#define DEFAULT_MAX_STEP_COUNT      40000
#define DEFAULT_MCS_PLUGIN          "mcs/none"
#define DEFAULT_MEM_PER_CPU         0
#define DEFAULT_MAX_MEM_PER_CPU     0
#define DEFAULT_MIN_JOB_AGE         300
#define DEFAULT_MPI_DEFAULT         "none"
#define DEFAULT_MSG_AGGR_WINDOW_MSGS 1
#define DEFAULT_MSG_AGGR_WINDOW_TIME 100
#define DEFAULT_MSG_TIMEOUT         10
#define DEFAULT_POWER_PLUGIN        ""
#if defined WITH_CGROUP
#  define DEFAULT_PROCTRACK_TYPE      "proctrack/cgroup"
#else
#  define DEFAULT_PROCTRACK_TYPE      "proctrack/pgid"
#endif
#define DEFAULT_PREEMPT_TYPE        "preempt/none"
#define DEFAULT_PREP_PLUGINS        "prep/script"
#define DEFAULT_PRIORITY_DECAY      604800 /* 7 days */
#define DEFAULT_PRIORITY_CALC_PERIOD 300 /* in seconds */
#define DEFAULT_PRIORITY_TYPE       "priority/basic"
#define DEFAULT_RECONF_KEEP_PART_STATE 0
#define DEFAULT_RETURN_TO_SERVICE   0
#define DEFAULT_RESUME_RATE         300
#define DEFAULT_RESUME_TIMEOUT      60
#define DEFAULT_ROUTE_PLUGIN   	    "route/default"
#define DEFAULT_SAVE_STATE_LOC      "/var/spool"
#define DEFAULT_SCHED_LOG_LEVEL     0
#define DEFAULT_SCHED_TIME_SLICE    30
#define DEFAULT_SCHEDTYPE           "sched/backfill"
#if defined HAVE_NATIVE_CRAY
#  define DEFAULT_SELECT_TYPE       "select/cray_aries"
#else
#  define DEFAULT_SELECT_TYPE       "select/linear"
#endif
#define DEFAULT_SITE_FACTOR_PLUGIN  "site_factor/none"
#define DEFAULT_SLURMCTLD_PIDFILE   "/var/run/slurmctld.pid"
#define DEFAULT_SLURMCTLD_TIMEOUT   120
#define DEFAULT_SLURMD_PIDFILE      "/var/run/slurmd.pid"
#define DEFAULT_SLURMD_TIMEOUT      300
#define DEFAULT_SPOOLDIR            "/var/spool/slurmd"
#define DEFAULT_STORAGE_HOST        "localhost"
#define DEFAULT_STORAGE_LOC         "/var/log/slurm_jobacct.log"
#define DEFAULT_STORAGE_USER        "root"
#define DEFAULT_STORAGE_PORT        0
#define DEFAULT_MYSQL_PORT          3306
#define DEFAULT_SUSPEND_RATE        60
#define DEFAULT_SUSPEND_TIME        0
#define DEFAULT_SUSPEND_TIMEOUT     30
#if defined HAVE_NATIVE_CRAY
#  define DEFAULT_SWITCH_TYPE         "switch/cray_aries"
#else
#  define DEFAULT_SWITCH_TYPE         "switch/none"
#endif
#define DEFAULT_TASK_PLUGIN         "task/none"
#define DEFAULT_TCP_TIMEOUT         2
#define DEFAULT_TMP_FS              "/tmp"
#if defined HAVE_3D
#  define DEFAULT_TOPOLOGY_PLUGIN     "topology/3d_torus"
#else
#  define DEFAULT_TOPOLOGY_PLUGIN     "topology/none"
#endif
#define DEFAULT_WAIT_TIME           0
#  define DEFAULT_TREE_WIDTH        50
#define DEFAULT_UNKILLABLE_TIMEOUT  60 /* seconds */

/* MAX_TASKS_PER_NODE is defined in slurm.h
 */
#define DEFAULT_MAX_TASKS_PER_NODE  MAX_TASKS_PER_NODE

typedef struct slurm_conf_frontend {
	char *allow_groups;		/* allowed group string */
	char *allow_users;		/* allowed user string */
	char *deny_groups;		/* denied group string */
	char *deny_users;		/* denied user string */
	char *frontends;		/* frontend node name */
	char *addresses;		/* frontend node address */
	uint16_t port;			/* frontend specific port */
	char *reason;			/* reason for down frontend node */
	uint16_t node_state;		/* enum node_states, ORed with
					 * NODE_STATE_NO_RESPOND if not
					 * responding */
} slurm_conf_frontend_t;

typedef struct slurm_conf_node {
	char *nodenames;
	char *hostnames;
	char *addresses;
	char *bcast_addresses;
	char *gres;		/* arbitrary list of node's generic resources */
	char *feature;		/* arbitrary list of node's features */
	char *port_str;
	uint32_t cpu_bind;	/* default CPU bind type */
	uint16_t cpus;		/* count of cpus running on the node */
	char *cpu_spec_list;	/* arbitrary list of specialized cpus */
	uint16_t boards; 	/* number of boards per node */
	uint16_t tot_sockets;   /* number of sockets per node */
	uint16_t cores;         /* number of cores per CPU */
	uint16_t core_spec_cnt;	/* number of specialized cores */
	uint16_t threads;       /* number of threads per core */
	uint64_t real_memory;	/* MB real memory on the node */
	uint64_t mem_spec_limit; /* MB real memory for memory specialization */
	char *reason;
	char *state;
	uint32_t tmp_disk;	/* MB total storage in TMP_FS file system */
	char *tres_weights_str;	/* per TRES billing weight string */
	uint32_t weight;	/* arbitrary priority of node for
				 * scheduling work on */
} slurm_conf_node_t;

typedef struct slurm_conf_partition {
	char *allow_alloc_nodes;/* comma delimited list of allowed
				 * allocating nodes
				 * NULL indicates all */
	char *allow_accounts;   /* comma delimited list of accounts,
				 * NULL indicates all */
	char *allow_groups;	/* comma delimited list of groups,
				 * NULL indicates all */
	char *allow_qos;        /* comma delimited list of qos,
			         * NULL indicates all */
	char *alternate;	/* name of alternate partition */
	char *billing_weights_str;/* per TRES billing weights */
	uint32_t cpu_bind;	/* default CPU binding type */
	uint16_t cr_type;	/* Custom CR values for partition (supported
				 * by select/cons_res plugin only) */
	uint64_t def_mem_per_cpu; /* default MB memory per allocated CPU */
	bool default_flag;	/* Set if default partition */
	uint32_t default_time;	/* minutes or INFINITE */
	char *deny_accounts;    /* comma delimited list of denied accounts,
				 * NULL indicates all */
	char *deny_qos;		/* comma delimited list of denied qos,
				 * NULL indicates all */
	uint8_t disable_root_jobs; /* if set then user root can't run jobs
				    * if NO_VAL8, use global default */
	uint16_t exclusive_user; /* 1 if node allocations by user */
	uint32_t grace_time;	/* default grace time for partition */
	bool     hidden_flag;	/* 1 if hidden by default */
	List job_defaults_list;	/* List of job_defaults_t elements */
	bool     lln_flag;	/* 1 if nodes are selected in LLN order */
	uint32_t max_cpus_per_node; /* maximum allocated CPUs per node */
	uint16_t max_share;	/* number of jobs to gang schedule */
	uint32_t max_time;	/* minutes or INFINITE */
	uint64_t max_mem_per_cpu; /* maximum MB memory per allocated CPU */
	uint32_t max_nodes;	/* per job or INFINITE */
	uint32_t min_nodes;	/* per job */
	char	*name;		/* name of the partition */
	char 	*nodes;		/* comma delimited list names of nodes */
	uint16_t over_time_limit; /* job's time limit can be exceeded by this
				   * number of minutes before cancellation */
	uint16_t preempt_mode;	/* See PREEMPT_MODE_* in slurm/slurm.h */
	uint16_t priority_job_factor;	/* job priority weight factor */
	uint16_t priority_tier;	/* tier for scheduling and preemption */
	char    *qos_char;      /* Name of QOS associated with partition */
	bool     req_resv_flag; /* 1 if partition can only be used in a
				 * reservation */
	uint16_t resume_timeout; /* time required in order to perform a node
				  * resume operation */
	bool     root_only_flag;/* 1 if allocate/submit RPC can only be
				   issued by user root */
	uint16_t state_up;	/* for states see PARTITION_* in slurm.h */
	uint32_t suspend_time;  /* node idle for this long before power save
				 * mode */
	uint16_t suspend_timeout; /* time required in order to perform a node
				   * suspend operation */
	uint32_t total_nodes;	/* total number of nodes in the partition */
	uint32_t total_cpus;	/* total number of cpus in the partition */
} slurm_conf_partition_t;

typedef struct slurm_conf_downnodes {
	char *nodenames;
	char *reason;
	char *state;
} slurm_conf_downnodes_t;

typedef struct {
	char *feature;
	char *name;
	char *nodes;
} slurm_conf_nodeset_t;

typedef struct {
	char *name;
	char *value;
} config_key_pair_t;

typedef struct {
	char *name;
	List key_pairs;
} config_plugin_params_t;

/*
 * Get result of configuration file test.
 * RET SLURM_SUCCESS or error code
 */
extern int config_test_result(void);

/*
 * Start configuration file test mode. Disables fatal errors.
 */
extern void config_test_start(void);

/* Destroy a front_end record built by slurm_conf_frontend_array() */
extern void destroy_frontend(void *ptr);

/* Copy list of job_defaults_t elements */
extern List job_defaults_copy(List in_list);

/* Destroy list of job_defaults_t elements */
extern void job_defaults_free(void *x);

/*
 * Translate string of job_defaults_t elements into a List.
 * in_str IN - comma separated key=value pairs
 * out_list OUT - equivalent list of key=value pairs
 * Returns SLURM_SUCCESS or an error code
 */
extern int job_defaults_list(char *in_str, List *out_list);

/*
 * Translate list of job_defaults_t elements into a string.
 * Return value must be released using xfree()
 */
extern char *job_defaults_str(List in_list);

/* Pack a job_defaults_t element. Used by slurm_pack_list() */
extern void job_defaults_pack(void *in, uint16_t protocol_version,
			      buf_t *buffer);

/* Unpack a job_defaults_t element. Used by slurm_pack_list() */
extern int job_defaults_unpack(void **out, uint16_t protocol_version,
			       buf_t *buffer);

/*
 * slurm_reset_alias() for each node in alias_list
 *
 * IN alias_list - string with sets of node name, communication address in []
 * 	and hostname. Each element in the set if colon separated and
 * 	each set is comma separated.
 * 	eg.: ec0:[1.2.3.4]:foo,ec1:[1.2.3.5]:bar
 * RET return SLURM_SUCCESS on success, SLURM_ERROR otherwise.
 */
extern int set_nodes_alias(const char *alias_list);

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
void slurm_conf_install_fork_handlers(void);

/*
 * NOTE: Caller must NOT be holding slurm_conf_lock().
 */
extern int slurm_conf_destroy(void);

extern slurm_conf_t *slurm_conf_lock(void);

extern void slurm_conf_unlock(void);


/*
 * Set "ptr_array" with the pointer to an array of pointers to
 * slurm_conf_frontend_t structures.
 *
 * Return value is the length of the array.
 */
extern int slurm_conf_frontend_array(slurm_conf_frontend_t **ptr_array[]);

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
 * slurm_conf_downnodes_t structures.
 *
 * Return value is the length of the array.
 */
extern int slurm_conf_downnodes_array(slurm_conf_downnodes_t **ptr_array[]);

/*
 * Set "ptr_array" with the pointer to an array of pointers to
 * slurm_conf_nodeset_t structures.
 *
 * Return value is the length of the array.
 */
extern int slurm_conf_nodeset_array(slurm_conf_nodeset_t **ptr_array[]);

/*
 * slurm_reset_alias - Reset the address and hostname of a specific node name
 */
extern void slurm_reset_alias(char *node_name, char *node_addr,
			      char *node_hostname);

/*
 * Return NodeAddr (if set) for a given NodeName, or NULL
 *
 * Returned string was allocated with xmalloc(), and must be freed by
 * the caller using xfree().
 *
 * NOTE: Caller must NOT be holding slurm_conf_lock().
 */
extern char* slurm_conf_get_address(const char *node_name);

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
 * NOTE: Call xfree() to release returned value's memory.
 * NOTE: Caller must NOT be holding slurm_conf_lock().
 */
extern char *slurm_conf_get_nodename(const char *node_hostname);

/*
 * slurm_conf_get_aliases - Return all the nodes NodeName value
 * associated to a given NodeHostname (useful in case of multiple-slurmd
 * to get the list of virtual nodes associated with a real node)
 *
 * NOTE: Call xfree() to release returned value's memory.
 * NOTE: Caller must NOT be holding slurm_conf_lock().
 */
extern char *slurm_conf_get_aliases(const char *node_hostname);

/*
 * slurm_conf_get_nodeaddr - Return the NodeAddr for given NodeHostname
 *
 * NOTE: Call xfree() to release returned value's memory.
 * NOTE: Caller must NOT be holding slurm_conf_lock().
 */
extern char *slurm_conf_get_nodeaddr(const char *node_hostname);

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
 * Return BcastAddr (if set) for a given NodeName, or NULL
 *
 * Returned string was allocated with xmalloc(), and must be freed by
 * the caller using xfree().
 *
 * NOTE: Caller must NOT be holding slurm_conf_lock().
 */
extern char *slurm_conf_get_bcast_address(const char *node_name);

/*
 * slurm_conf_get_port - Return the port for a given NodeName
 *
 * NOTE: Caller must NOT be holding slurm_conf_lock().
 */
extern uint16_t slurm_conf_get_port(const char *node_name);

/*
 * slurm_conf_get_addr - Return the slurm_addr_t for a given NodeName in
 *	the parameter "address".  The return code is SLURM_SUCCESS on success,
 *	and SLURM_ERROR if the address lookup failed.
 *
 * NOTE: Caller must NOT be holding slurm_conf_lock().
 */
extern int slurm_conf_get_addr(const char *node_name, slurm_addr_t *address,
			       uint16_t flags);

/*
 * slurm_conf_get_cpus_bsct -
 * Return the cpus, boards, sockets, cores, and threads configured for a
 * given NodeName
 * Returns SLURM_SUCCESS on success, SLURM_ERROR on failure.
 *
 * NOTE: Caller must NOT be holding slurm_conf_lock().
 */
extern int slurm_conf_get_cpus_bsct(const char *node_name,
				    uint16_t *cpus, uint16_t *boards,
				    uint16_t *sockets, uint16_t *cores,
				    uint16_t *threads);

/*
 * slurm_conf_get_res_spec_info - Return resource specialization info
 * for a given NodeName
 * Returns SLURM_SUCCESS on success, SLURM_ERROR on failure.
 *
 * NOTE: Caller must NOT be holding slurm_conf_lock().
 */
extern int slurm_conf_get_res_spec_info(const char *node_name,
					char **cpu_spec_list,
					uint16_t *core_spec_cnt,
					uint64_t *mem_spec_limit);

/*
 * Parse slurm.conf NodeName line and return single slurm_conf_node_t*.
 *
 * IN nodeline - NodeName= line string.
 * OUT out_hashtbl - ptr to the generated hashtable so it can be deleted by
 *                   caller after using the slurm_conf_node_t*. Currently, not a
 *                   way to disassociate items from the hashtbl.
 * RET slurm_conf_t* on success, NULL otherwise.
 */
extern slurm_conf_node_t *slurm_conf_parse_nodeline(const char *nodeline,
						    s_p_hashtbl_t **out_hashtbl);

/*
 * init_slurm_conf - initialize or re-initialize the slurm configuration
 *	values to defaults (NULL or NO_VAL). Note that the configuration
 *	file pathname (slurm_conf) is not changed.
 * IN/OUT ctl_conf_ptr - pointer to data structure to be initialized
 */
extern void init_slurm_conf(slurm_conf_t *ctl_conf_ptr);

/*
 * free_slurm_conf - free all storage associated with a slurm_conf_t.
 * IN/OUT ctl_conf_ptr - pointer to data structure to be freed
 * IN purge_node_hash - purge system-wide node hash table if set,
 *			set to zero if clearing private copy of config data
 */
extern void free_slurm_conf(slurm_conf_t *ctl_conf_ptr, bool purge_node_hash);

/*
 * gethostname_short - equivalent to gethostname(), but return only the first
 *      component of the fully qualified name (e.g. "linux123.foo.bar"
 *      becomes "linux123")
 * NOTE: NodeName in the config may be different from real hostname.
 *       Use get_conf_node_name() to get the former.
 */
extern int gethostname_short(char *name, size_t len);

/*
 * Replace first "%h" in path string with NodeHostname.
 * Replace first "%n" in path string with NodeName.
 *
 * NOTE: Caller should be holding slurm_conf_lock() when calling this function.
 *
 * Returns an xmalloc()ed string which the caller must free with xfree().
 */
extern char *slurm_conf_expand_slurmd_path(const char *path,
					   const char *node_name,
					   const char *host_name);

/*
 * prolog_flags2str - convert a PrologFlags uint16_t to the equivalent string
 * Returns an xmalloc()ed string which the caller must free with xfree().
 */
extern char *prolog_flags2str(uint16_t prolog_flags);

/*
 * prolog_str2flags - Convert a PrologFlags string to the equivalent uint16_t
 * Returns NO_VAL if invalid
 */
extern uint16_t prolog_str2flags(char *prolog_flags);

/*
 * debug_flags2str - convert a DebugFlags uint64_t to the equivalent string
 * Returns an xmalloc()ed string which the caller must free with xfree().
 */
extern char *debug_flags2str(uint64_t debug_flags);

/*
 * debug_str2flags - Convert a DebugFlags string to the equivalent uint64_t
 * Returns SLURM_ERROR if invalid
 */
extern int debug_str2flags(char *debug_flags, uint64_t *flags_out);

/*
 * reconfig_flags2str - convert a ReconfigFlags uint16_t to the equivalent string
 * Returns an xmalloc()ed string which the caller must free with xfree().
 */
extern char *reconfig_flags2str(uint16_t reconfig_flags);

/*
 * reconfig_str2flags - Convert a ReconfigFlags string to the equivalent uint16_t
 * Returns NO_VAL if invalid
 */
extern uint16_t reconfig_str2flags(char *reconfig_flags);

extern void destroy_config_plugin_params(void *object);
extern void pack_config_plugin_params(void *in, uint16_t protocol_version,
				      buf_t *buff);
extern int unpack_config_plugin_params(void **object, uint16_t protocol_version,
				       buf_t *buff);
extern void pack_config_plugin_params_list(void *in, uint16_t protocol_version,
					   buf_t *buff);
extern int unpack_config_plugin_params_list(void **object,
					    uint16_t protocol_version,
					    buf_t *buff);

extern void destroy_config_key_pair(void *object);
extern void pack_key_pair_list(void *key_pairs, uint16_t protocol_version,
			       buf_t *buffer);
extern int unpack_key_pair_list(void **key_pairs, uint16_t protocol_version,
				buf_t *buffer);
extern void pack_config_key_pair(void *in, uint16_t protocol_version,
				 buf_t *buffer);
extern int unpack_config_key_pair(void **object, uint16_t protocol_version,
				  buf_t *buffer);

extern int sort_key_pairs(void *v1, void *v2);
/*
 * Return the pathname of the extra .conf file
 * return value must be xfreed
 */
extern char *get_extra_conf_path(char *conf_name);

/* Translate a job constraint specification into a node feature specification
 * RET - String MUST be xfreed */
extern char *xlate_features(char *job_features);

/*
 * Add nodes and corresponding pre-configured slurm_addr_t's to node conf hash
 * tables.
 *
 * IN node_list  - node_list allocated to job
 * IN node_addrs - array of slurm_addr_t that corresponds to nodes built from
 * 	host_list. See build_node_details().
 * RET return SLURM_SUCCESS on success, SLURM_ERROR otherwise.
 */
extern int add_remote_nodes_to_conf_tbls(char *node_list,
					 slurm_addr_t *node_addrs);

/*
 * Add record to conf hash tables from node_record_t.
 */
extern void slurm_conf_add_node(node_record_t *node_ptr);

/*
 * Remove node from node conf hash tables.
 */
extern void slurm_conf_remove_node(char *node_name);

#endif /* !_READ_CONFIG_H */
