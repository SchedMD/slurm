/*****************************************************************************\
 *  slurmctld.h - definitions of functions and structures for slurmcltd use
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
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

#ifndef _HAVE_SLURMCTLD_H
#define _HAVE_SLURMCTLD_H


#if HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif			/* HAVE_INTTYPES_H */
#endif

#include <pthread.h>
/* #include <stdlib.h> */
#include <time.h>
#include <strings.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif				/* WITH_PTHREADS */

#include <slurm/slurm.h>

#include "src/common/bitstring.h"
#include "src/common/checkpoint.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/slurm_cred.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/switch.h"
#include "src/common/timers.h"
#include "src/common/xmalloc.h"
#include "src/common/read_config.h" /* location of slurmctld_conf */

#define FREE_NULL_BITMAP(_X)		\
	do {				\
		if (_X) bit_free (_X);	\
		_X	= NULL; 	\
	} while (0)
#define IS_JOB_FINISHED(_X)		\
	((_X->job_state & (~JOB_COMPLETING)) >  JOB_SUSPENDED)
#define IS_JOB_PENDING(_X)		\
	((_X->job_state & (~JOB_COMPLETING)) == JOB_PENDING)

/*****************************************************************************\
 *  GENERAL CONFIGURATION parameters and data structures
\*****************************************************************************/
/* Maximum parallel threads to service incoming RPCs */
#define MAX_SERVER_THREADS 100

/* Maximum size we want to support for user strings (e.g. job comment).
 * Try to prevent user from filling slurmctld's memory */
#define MAX_STR_LEN  64 * 1024

/* Perform full slurmctld's state every PERIODIC_CHECKPOINT seconds */
#define	PERIODIC_CHECKPOINT	300

/* Retry an incomplete RPC agent request every RPC_RETRY_INTERVAL seconds */
#define	RPC_RETRY_INTERVAL	60

/* Attempt to schedule jobs every PERIODIC_SCHEDULE seconds despite 
 * any RPC activity. This will catch any state transisions that may 
 * have otherwise been missed */
#define	PERIODIC_SCHEDULE	60

/* Check for jobs reaching their time limit every PERIODIC_TIMEOUT seconds */
#define	PERIODIC_TIMEOUT	60

/* Attempt to purge defunct job records and resend job kill requests
 * every PURGE_JOB_INTERVAL seconds */
#define PURGE_JOB_INTERVAL 60

/* Process pending trigger events every TRIGGER_INTERVAL seconds */
#define TRIGGER_INTERVAL 15

/* Report current node accounting state every PERIODIC_NODE_ACCT seconds */
#define PERIODIC_NODE_ACCT 300

/* Pathname of group file record for checking update times */
#define GROUP_FILE	"/etc/group"

/* Check for updates to GROUP_FILE every PERIODIC_GROUP_CHECK seconds, 
 * Update the group uid_t access list as needed */
#define	PERIODIC_GROUP_CHECK	600

/* Seconds to wait for backup controller response to REQUEST_CONTROL RPC */
#define CONTROL_TIMEOUT 4	/* seconds */

/*****************************************************************************\
 *  General configuration parameters and data structures
\*****************************************************************************/

typedef struct slurmctld_config {
	int	daemonize;
	bool	resume_backup;
	time_t	boot_time;
	time_t	shutdown_time;
	int	server_thread_count;

	slurm_cred_ctx_t cred_ctx;
#ifdef WITH_PTHREADS
	pthread_mutex_t thread_count_lock;
	pthread_t thread_id_main;
	pthread_t thread_id_save;
	pthread_t thread_id_sig;
	pthread_t thread_id_power;
	pthread_t thread_id_rpc;
#else
	int thread_count_lock;
	int thread_id_main;
	int thread_id_save;
	int thread_id_sig;
	int thread_id_power;
	int thread_id_rpc;
#endif
} slurmctld_config_t;

extern slurmctld_config_t slurmctld_config;
extern int bg_recover;		/* state recovery mode */
extern char *slurmctld_cluster_name; /* name of cluster */
extern void *acct_db_conn;
extern int accounting_enforce;
extern int association_based_accounting;

/*****************************************************************************\
 *  NODE parameters and data structures
\*****************************************************************************/
#define CONFIG_MAGIC 0xc065eded
#define NODE_MAGIC   0x0de575ed

struct config_record {
	uint32_t magic;		/* magic cookie to test data integrity */
	uint16_t cpus;		/* count of processors running on the node */
	uint16_t sockets;	/* number of sockets per node */
	uint16_t cores;		/* number of cores per CPU */
	uint16_t threads;	/* number of threads per core */
	uint32_t real_memory;	/* MB real memory on the node */
	uint32_t tmp_disk;	/* MB total storage in TMP_FS file system */
	uint32_t weight;	/* arbitrary priority of node for 
				 * scheduling work on */
	char *feature;		/* arbitrary list of features associated */
	char *nodes;		/* name of nodes with this configuration */
	bitstr_t *node_bitmap;	/* bitmap of nodes with this configuration */
};

extern List config_list;	/* list of config_record entries */

struct node_record {
	uint32_t magic;			/* magic cookie for data integrity */
	char *name;			/* name of the node. NULL==defunct */
	uint16_t node_state;		/* enum node_states, ORed with 
					 * NODE_STATE_NO_RESPOND if not 
					 * responding */
	bool not_responding;		/* set if fails to respond, 
					 * clear after logging this */
	time_t last_response;		/* last response from the node */
	time_t last_idle;		/* time node last become idle */
	uint16_t cpus;			/* count of processors on the node */
	uint16_t sockets;		/* number of sockets per node */
	uint16_t cores;			/* number of cores per CPU */
	uint16_t threads;		/* number of threads per core */
	uint32_t real_memory;		/* MB real memory on the node */
	uint32_t tmp_disk;		/* MB total disk in TMP_FS */
	struct config_record *config_ptr;  /* configuration spec ptr */
	uint16_t part_cnt;		/* number of associated partitions */
	struct part_record **part_pptr;	/* array of pointers to partitions 
					 * associated with this node*/
	char *comm_name;		/* communications path name to node */
	uint16_t port;			/* TCP port number of the slurmd */
	slurm_addr slurm_addr;		/* network address */
	uint16_t comp_job_cnt;		/* count of jobs completing on node */
	uint16_t run_job_cnt;		/* count of jobs running on node */
	uint16_t no_share_job_cnt;	/* count of jobs running that will
					 * not share nodes */
	char *reason; 			/* why a node is DOWN or DRAINING */
	char *features;			/* associated features, used only
					 * for state save/restore, DO NOT
					 * use for scheduling purposes */
	char *arch;			/* computer architecture */
	char *os;			/* operating system currently running */
	struct node_record *node_next;	/* next entry with same hash index */ 
};

extern struct node_record *node_record_table_ptr;  /* ptr to node records */
extern time_t last_bitmap_update;	/* time of last node creation or 
					 * deletion */
extern time_t last_node_update;		/* time of last node record update */
extern int node_record_count;		/* count in node_record_table_ptr */
extern bitstr_t *avail_node_bitmap;	/* bitmap of available nodes, 
					 * not DOWN, DRAINED or DRAINING */
extern uint32_t total_cpus;		/* count of CPUs in the entire cluster */
extern bitstr_t *idle_node_bitmap;	/* bitmap of idle nodes */
extern bitstr_t *share_node_bitmap;	/* bitmap of sharable nodes */
extern bitstr_t *up_node_bitmap;	/* bitmap of up nodes, not DOWN */
extern bool ping_nodes_now;		/* if set, ping nodes immediately */

/*****************************************************************************\
 *  PARTITION parameters and data structures
\*****************************************************************************/
#define PART_MAGIC 0xaefe8495

struct part_record {
	uint16_t disable_root_jobs; /* if set then user root can't run jobs */
	uint32_t magic;		/* magic cookie to test data integrity */
	char *name;		/* name of the partition */
	uint16_t hidden;	/* 1 if hidden by default */
	uint32_t max_time;	/* minutes or INFINITE */
	uint32_t max_nodes;	/* per job or INFINITE */
	uint32_t max_nodes_orig;/* unscaled value (c-nodes on BlueGene) */
	uint32_t min_nodes;	/* per job */
	uint32_t min_nodes_orig;/* unscaled value (c-nodes on BlueGene) */
	uint32_t total_nodes;	/* total number of nodes in the partition */
	uint32_t total_cpus;	/* total number of cpus in the partition */
	uint32_t min_offset;	/* select plugin min offset */
	uint32_t max_offset;	/* select plugin max offset */
	uint16_t root_only;	/* 1 if allocate/submit RPC can only be 
				   issued by user root */
	uint16_t max_share;	/* number of jobs to gang schedule */
	uint16_t priority;	/* scheduling priority for jobs */
	uint16_t state_up;	/* 1 if state is up, 0 if down */
	char *nodes;		/* comma delimited list names of nodes */
	char *allow_groups;	/* comma delimited list of groups, 
				 * NULL indicates all */
	uid_t *allow_uids;	/* zero terminated list of allowed users */
	bitstr_t *node_bitmap;	/* bitmap of nodes in partition */
};

extern List part_list;			/* list of part_record entries */
extern time_t last_part_update;		/* time of last part_list update */
extern struct part_record default_part;	/* default configuration values */
extern char *default_part_name;		/* name of default partition */
extern struct part_record *default_part_loc;	/* default partition ptr */

/*****************************************************************************\
 *  JOB parameters and data structures
\*****************************************************************************/
extern time_t last_job_update;	/* time of last update to part records */

/* Used for Moab
 * These QOS values only apply to LLNL's configuration
 * Other values may apply at other sites,
 * These may be mapped to partition priorities in the future */
#define QOS_EXPEDITE	300
#define QOS_NORMAL 	200
#define	QOS_STANDBY	100

#define DETAILS_MAGIC 0xdea84e7
#define JOB_MAGIC 0xf0b7392c
#define STEP_MAGIC 0xce593bc1
#define KILL_ON_STEP_DONE	1

#define FEATURE_OP_OR  0
#define FEATURE_OP_AND 1
#define FEATURE_OP_XOR 2
#define FEATURE_OP_END 3		/* last entry lacks separator */
struct feature_record {
	char *name;			/* name of feature */
	uint16_t count;			/* count of nodes with this feature */
	uint8_t op_code;		/* separator, see FEATURE_OP_ above */
};

/* job_details - specification of a job's constraints, 
 * can be purged after initiation */
struct job_details {
	uint32_t magic;			/* magic cookie for data integrity */
	uint32_t min_nodes;		/* minimum number of nodes */
	uint32_t max_nodes;		/* maximum number of nodes */
	char *req_nodes;		/* required nodes */
	char *exc_nodes;		/* excluded nodes */
	bitstr_t *req_node_bitmap;	/* bitmap of required nodes */
	uint16_t *req_node_layout;	/* task layout for required nodes */
	bitstr_t *exc_node_bitmap;	/* bitmap of excluded nodes */
	char *features;			/* required features */
	List feature_list;		/* required features with node counts */
	uint16_t shared;		/* 1 if job can share nodes,
					 * 0 if job cannot share nodes,
					 * any other value accepts the default
					 * sharing policy. */
	uint16_t contiguous;		/* set if requires contiguous nodes */
	uint16_t task_dist;		/* task layout for this job. Only
					 * useful when Consumable Resources
                                         * is enabled */
	uint32_t num_tasks;		/* number of tasks to start */
	uint8_t open_mode;		/* stdout/err append or trunctate */
	uint8_t overcommit;		/* processors being over subscribed */
	uint16_t acctg_freq;		/* accounting polling interval */
	uint16_t cpus_per_task;		/* number of processors required for 
					 * each task */
	uint16_t ntasks_per_node;	/* number of tasks on each node */
	/* job constraints: */
	uint32_t job_min_procs;		/* minimum processors per node */
	uint32_t job_min_memory;	/* minimum memory per node (MB) OR
					 * memory per allocated CPU | MEM_PER_CPU */
	uint32_t job_min_tmp_disk;	/* minimum tempdisk per node, MB */
	char *err;			/* pathname of job's stderr file */
	char *in;			/* pathname of job's stdin file */
	char *out;			/* pathname of job's stdout file */
	time_t submit_time;		/* time of submission */
	time_t begin_time;		/* start at this time (srun --being), 
					 * resets to time first eligible
					 * (all dependencies satisfied) */
	uint32_t reserved_resources;	/* CPU minutes of resources reserved
					 * for this job while it was pending */
	char *work_dir;			/* pathname of working directory */
	char **argv;			/* arguments for a batch job script */
	uint16_t argc;			/* count of argv elements */
	uint16_t requeue;		/* controls ability requeue job */
	multi_core_data_t *mc_ptr;	/* multi-core specific data */
	char *dependency;		/* wait for other jobs */
	List depend_list;		/* list of job_ptr:state pairs */
};

struct job_record {
	char    *account;		/* account number to charge */
	char    *alloc_node;		/* local node making resource alloc */
	uint16_t alloc_resp_port;	/* RESPONSE_RESOURCE_ALLOCATION port */
	uint32_t alloc_sid;		/* local sid making resource alloc */
	uint32_t assoc_id;              /* used for accounting plugins */
	void    *assoc_ptr;		/* job's association record ptr, it is
					 * void* because of interdependencies
					 * in the header files, confirm the 
					 * value before use */
	uint16_t batch_flag;		/* 1 or 2 if batch job (with script),
					 * 2 indicates retry mode (one retry) */
	char *comment;			/* arbitrary comment */
        uint16_t cr_enabled;            /* specify if if Consumable Resources
                                         * is enabled. Needed since CR deals
                                         * with a finer granularity in its
                                         * node/cpu scheduling (available cpus
                                         * instead of available nodes) than the
                                         * bluegene and the linear plugins 
                                         * 0 if cr is NOT enabled, 
                                         * 1 if cr is enabled */
	uint32_t db_index;              /* used only for database
					 * plugins */
	struct job_details *details;	/* job details */
	time_t end_time;		/* time of termination, 
					 * actual or expected */
	uint32_t exit_code;		/* exit code for job (status from 
					 * wait call) */
	uint32_t group_id;		/* group submitted under */
	uint32_t job_id;		/* job ID */
	struct job_record *job_next;	/* next entry with same hash index */
	enum job_states job_state;	/* state of the job */
	uint16_t kill_on_node_fail;	/* 1 if job should be killed on 
					 * node failure */
	uint16_t kill_on_step_done;	/* 1 if job should be killed when 
					 * the job step completes, 2 if kill
					 * in progress */
	char *licenses;			/* licenses required by the job */
	List license_list;		/* structure with license info */
	uint16_t mail_type;		/* see MAIL_JOB_* in slurm.h */
	char *mail_user;		/* user to get e-mail notification */
	uint32_t magic;			/* magic cookie for data integrity */
	char *name;			/* name of the job */
	char *network;			/* network/switch requirement spec */
	uint16_t next_step_id;		/* next step id to be used */
	char *nodes;			/* list of nodes allocated to job */
	slurm_addr *node_addr;		/* addresses of the nodes allocated to 
					 * job */
	bitstr_t *node_bitmap;		/* bitmap of nodes allocated to job */
	uint32_t node_cnt;		/* count of nodes currently 
					 * allocated to job */
	char *nodes_completing;		/* nodes still in completing state
					 * for this job, used to insure
					 * epilog is not re-run for job */
	uint32_t num_procs;		/* count of required processors */
	uint16_t other_port;		/* port for client communications */
	char *partition;		/* name of the partition */
	struct part_record *part_ptr;	/* pointer to the partition record */
	time_t pre_sus_time;		/* time job ran prior to last suspend */
	uint32_t priority;		/* relative priority of the job,
					 * zero == held (don't initiate) */
	uint16_t qos;			/* quality of service, used only by Moab */
	uint32_t requid;            	/* requester user ID */
	char *resp_host;		/* host for srun communications */
	select_jobinfo_t select_jobinfo;/* opaque data */
	time_t start_time;		/* time execution begins, 
					 * actual or expected */
	uint16_t state_reason;		/* reason job still pending or failed
					 * see slurm.h:enum job_wait_reason */
	List step_list;			/* list of job's steps */
	time_t suspend_time;		/* time job last suspended or resumed */
	time_t time_last_active;	/* time of last job activity */
	uint32_t time_limit;		/* time_limit minutes or INFINITE,
					 * NO_VAL implies partition max_time */
	time_t tot_sus_time;		/* total time in suspend state */
	uint32_t total_procs;		/* number of allocated processors, 
					 * for accounting */
	uint32_t user_id;		/* user the job runs as */

	/* Per node allocation details */
	uint16_t num_cpu_groups;	/* record count in cpus_per_node and 
					 * cpu_count_reps */
	uint32_t *cpus_per_node;	/* array of cpus per node allocated */
	uint32_t *cpu_count_reps;	/* array of consecutive nodes with 
					 * same cpu count */

        uint32_t alloc_lps_cnt;		/* number of hosts in alloc_lps
					 * or 0 if alloc_lps is not needed
					 * for the credentials */
        uint32_t *alloc_lps;		/* number of logical processors
					 * allocated for this job */
	uint32_t *used_lps;		/* number of logical processors
					 * already allocated to job steps */
};

/* Job dependency specification, used in "depend_list" within job_record */
#define SLURM_DEPEND_AFTER		1
#define SLURM_DEPEND_AFTER_ANY		2
#define SLURM_DEPEND_AFTER_NOT_OK	3
#define SLURM_DEPEND_AFTER_OK		4
#define SLURM_DEPEND_SINGLETON		5
struct	depend_spec {
	uint16_t	depend_type;	/* SLURM_DEPEND_* type */
	uint32_t	job_id;		/* SLURM job_id */
	struct job_record *job_ptr;	/* pointer to this job */
};

struct 	step_record {
	struct job_record* job_ptr; 	/* ptr to the job that owns the step */
	uint16_t step_id;		/* step number */
	uint16_t cyclic_alloc;		/* set for cyclic task allocation 
					   across nodes */
	time_t start_time;      	/* step allocation time */
/*	time_t suspend_time;		 * time step last suspended or resumed
					 * implicitly the same as suspend_time
					 * in the job record */
	time_t pre_sus_time;		/* time step ran prior to last suspend */
	time_t tot_sus_time;		/* total time in suspended state */
	bitstr_t *step_node_bitmap;	/* bitmap of nodes allocated to job 
					 * step */
	uint16_t port;			/* port for srun communications */
	char *host;			/* host for srun communications */
	uint16_t batch_step;		/* 1 if batch job step, 0 otherwise */
	uint16_t mem_per_task;		/* MB memory per task, 0=no limit */
	uint16_t ckpt_interval;		/* checkpoint interval in minutes */
	char *ckpt_path;	        /* path to store checkpoint image files */
	uint16_t exclusive;		/* dedicated resources for the step */
	time_t ckpt_time;		/* time of last checkpoint */
	switch_jobinfo_t switch_job;	/* switch context, opaque */
	check_jobinfo_t check_job;	/* checkpoint context, opaque */
	char *name;			/* name of job step */
	char *network;			/* step's network specification */
	uint32_t exit_code;		/* highest exit code from any task */
	bitstr_t *exit_node_bitmap;	/* bitmap of exited nodes */
	jobacctinfo_t *jobacct;         /* keep track of process info in the 
					 * step */
	slurm_step_layout_t *step_layout;/* info about how tasks are laid out
					  * in the step */
};

extern List job_list;			/* list of job_record entries */

/*****************************************************************************\
 *  Consumable Resources parameters and data structures
\*****************************************************************************/

/* 
 * Define the type of update and of data retrieval that can happen
 * from the "select/cons_res" plugin. This information needed to
 * support processors as consumable resources.  This structure will be
 * useful when updating other types of consumable resources as well
*/
enum select_data_info {
	SELECT_CR_PLUGIN,    /* data-> uint32 1 if CR plugin */
	SELECT_BITMAP,       /* data-> partially_idle_bitmap (CR support) */
	SELECT_ALLOC_CPUS,   /* data-> uint16 alloc cpus (CR support) */
	SELECT_ALLOC_LPS,    /* data-> uint32 alloc lps  (CR support) */
	SELECT_AVAIL_CPUS,   /* data-> uint16 avail cpus (CR support) */ 
	SELECT_AVAIL_MEMORY  /* data-> uint32 avail mem  (CR support) */ 
} ;

/*****************************************************************************\
 *  Global slurmctld functions
\*****************************************************************************/

/*
 * abort_job_on_node - Kill the specific job_id on a specific node,
 *	the request is not processed immediately, but queued. 
 *	This is to prevent a flood of pthreads if slurmctld restarts 
 *	without saved state and slurmd daemons register with a 
 *	multitude of running jobs. Slurmctld will not recognize 
 *	these jobs and use this function to kill them - one 
 *	agent request per node as they register.
 * IN job_id - id of the job to be killed
 * IN job_ptr - pointer to terminating job (NULL if unknown, e.g. orphaned)
 * IN node_ptr - pointer to the node on which the job resides
 */
extern void abort_job_on_node(uint32_t job_id, struct job_record *job_ptr,
			      struct node_record *node_ptr);

/*
 * bitmap2node_name - given a bitmap, build a list of comma separated node 
 *	names. names may include regular expressions (e.g. "lx[01-10]")
 * IN bitmap - bitmap pointer
 * RET pointer to node list or NULL on error 
 * globals: node_record_table_ptr - pointer to node table
 * NOTE: the caller must xfree the memory at node_list when no longer required
 */
extern char * bitmap2node_name (bitstr_t *bitmap) ;

/*
 * create_config_record - create a config_record entry and set is values to 
 *	the defaults. each config record corresponds to a line in the  
 *	slurm.conf file and typically describes the configuration of a 
 *	large number of nodes
 * RET pointer to the config_record
 * global: default_config_record - default configuration values
 * NOTE: memory allocated will remain in existence until 
 *	_delete_config_record() is called to delete all configuration records
 */
extern struct config_record *create_config_record (void);

/* 
 * create_job_record - create an empty job_record including job_details.
 *	load its values with defaults (zeros, nulls, and magic cookie)
 * IN/OUT error_code - set to zero if no error, errno otherwise
 * RET pointer to the record or NULL if error
 * global: job_list - global job list
 *	job_count - number of jobs in the system
 *	last_job_update - time of last job table update
 * NOTE: allocates memory that should be xfreed with _list_delete_job
 */
extern struct job_record * create_job_record (int *error_code);

/* 
 * create_node_record - create a node record and set its values to defaults
 * IN config_ptr - pointer to node's configuration information
 * IN node_name - name of the node
 * RET pointer to the record or NULL if error
 * global: default_node_record - default node values
 * NOTE: the record's values are initialized to those of default_node_record, 
 *	node_name and config_point's cpus, real_memory, and tmp_disk values
 * NOTE: allocates memory at node_record_table_ptr that must be xfreed when  
 *	the global node table is no longer required
 */
extern struct node_record *create_node_record (struct config_record
					       *config_ptr,
					       char *node_name);

/* 
 * create_part_record - create a partition record
 * RET a pointer to the record or NULL if error
 * global: default_part - default partition parameters
 *         part_list - global partition list
 * NOTE: the record's values are initialized to those of default_part
 * NOTE: allocates memory that should be xfreed with delete_part_record
 */
extern struct part_record *create_part_record (void);

/* 
 * create_step_record - create an empty step_record for the specified job.
 * IN job_ptr - pointer to job table entry to have step record added
 * RET a pointer to the record or NULL if error
 * NOTE: allocates memory that should be xfreed with delete_step_record
 */
extern struct step_record * create_step_record (struct job_record *job_ptr);

/* 
 * delete_step_records - delete step record for specified job_ptr
 * IN job_ptr - pointer to job table entry to have step records removed
 * IN filter  - determine which job steps to delete
 *              0: delete all job steps
 *              1: delete only job steps without a switch allocation
 */
extern void delete_step_records (struct job_record *job_ptr, int filter);

/* 
 * delete_job_details - delete a job's detail record and clear it's pointer
 *	this information can be deleted as soon as the job is allocated  
 *	resources and running (could need to restart batch job)
 * IN job_entry - pointer to job_record to clear the record of
 */
extern void  delete_job_details (struct job_record *job_entry);

/*
 * delete_partition - delete the specified partition (actually leave 
 *	the entry, just flag it as defunct)
 * IN job_specs - job specification from RPC
 * RET 0 on success, errno otherwise
 */
extern int delete_partition(delete_part_msg_t *part_desc_ptr);

/* 
 * delete_step_record - delete record for job step for specified job_ptr 
 *	and step_id
 * IN job_ptr - pointer to job table entry to have step record removed
 * IN step_id - id of the desired job step
 * RET 0 on success, errno otherwise
 */
extern int delete_step_record (struct job_record *job_ptr, uint32_t step_id);

/* 
 * drain_nodes - drain one or more nodes, 
 *  no-op for nodes already drained or draining
 * IN nodes - nodes to drain
 * IN reason - reason to drain the nodes
 * RET SLURM_SUCCESS or error code
 * global: node_record_table_ptr - pointer to global node table
 */
extern int drain_nodes ( char *nodes, char *reason );

/* dump_all_job_state - save the state of all jobs to file
 * RET 0 or error code */
extern int dump_all_job_state ( void );

/* dump_all_node_state - save the state of all nodes to file */
extern int dump_all_node_state ( void );

/* dump_all_part_state - save the state of all partitions to file */
extern int dump_all_part_state ( void );

/*
 * dump_job_desc - dump the incoming job submit request message
 * IN job_specs - job specification from RPC
 */
extern void dump_job_desc(job_desc_msg_t * job_specs);

/*
 * dump_job_step_state - dump the state of a specific job step to a buffer,
 *	load with load_step_state
 * IN step_ptr - pointer to job step for which information is to be dumpped
 * IN/OUT buffer - location to store data, pointers automatically advanced
 */
extern void dump_job_step_state(struct step_record *step_ptr, Buf buffer);

/*
 * dump_step_desc - dump the incoming step initiate request message
 * IN step_spec - job step request specification from RPC
 */
extern void dump_step_desc(job_step_create_request_msg_t *step_spec);

/* 
 * find_job_record - return a pointer to the job record with the given job_id
 * IN job_id - requested job's id
 * RET pointer to the job's record, NULL on error
 * global: job_list - global job list pointer
 *	job_hash - hash table into job records
 */
extern struct job_record *find_job_record (uint32_t job_id);

/*
 * find_first_node_record - find a record for first node in the bitmap
 * IN node_bitmap
 */
extern struct node_record *find_first_node_record (bitstr_t *node_bitmap);

/* find_node_record - find a record for node with specified name, 
 *	returns pointer to record or NULL if not found */
extern struct node_record *find_node_record (char *name);

/* 
 * find_part_record - find a record for partition with specified name
 * IN name - name of the desired partition 
 * RET pointer to node partition or NULL if not found
 * global: part_list - global partition list
 */
extern struct part_record *find_part_record (char *name);

/*
 * get_job_env - return the environment variables and their count for a 
 *	given job
 * IN job_ptr - pointer to job for which data is required
 * OUT env_size - number of elements to read
 * RET point to array of string pointers containing environment variables
 */
extern char **get_job_env (struct job_record *job_ptr, uint32_t *env_size);

/* 
 * get_job_script - return the script for a given job
 * IN job_ptr - pointer to job for which data is required
 * RET point to string containing job script
 */
extern char *get_job_script (struct job_record *job_ptr);

/*
 * get_next_job_id - return the job_id to be used by default for
 *	the next job
 */
extern uint32_t get_next_job_id(void);

/* 
 * find_step_record - return a pointer to the step record with the given 
 *	job_id and step_id
 * IN job_ptr - pointer to job table entry to have step record added
 * IN step_id - id of the desired job step
 * RET pointer to the job step's record, NULL on error
 */
extern struct step_record * find_step_record(struct job_record *job_ptr, 
					     uint16_t step_id);

/* 
 * init_job_conf - initialize the job configuration tables and values. 
 *	this should be called after creating node information, but 
 *	before creating any job entries.
 * RET 0 if no error, otherwise an error code
 * global: last_job_update - time of last job table update
 *	job_list - pointer to global job list
 */
extern int init_job_conf (void);

/* 
 * init_node_conf - initialize the node configuration tables and values. 
 *	this should be called before creating any node or configuration 
 *	entries.
 * RET 0 if no error, otherwise an error code
 * global: node_record_table_ptr - pointer to global node table
 *         default_node_record - default values for node records
 *         default_config_record - default values for configuration records
 *         hash_table - table of hash indecies
 *         last_node_update - time of last node table update
 */
extern int init_node_conf ();

/* 
 * init_part_conf - initialize the default partition configuration values 
 *	and create a (global) partition list. 
 * this should be called before creating any partition entries.
 * RET 0 if no error, otherwise an error code
 * global: default_part - default partition values
 *         part_list - global partition list
 */
extern int init_part_conf (void);

/*
 * init_power_save - initialize the power save module. Started as a 
 *	pthread. Terminates automatically at slurmctld shutdown time.
 *	Input and output are unused.
 */
extern void *init_power_save(void *arg);

/*
 * is_node_down - determine if the specified node's state is DOWN
 * IN name - name of the node
 * RET true if node exists and is down, otherwise false 
 */
extern bool is_node_down (char *name);

/*
 * is_node_resp - determine if the specified node's state is responding
 * IN name - name of the node
 * RET true if node exists and is responding, otherwise false 
 */
extern bool is_node_resp (char *name);

/*
 * job_allocate - create job_records for the suppied job specification and 
 *	allocate nodes for it.
 * IN job_specs - job specifications
 * IN immediate - if set then either initiate the job immediately or fail
 * IN will_run - don't initiate the job if set, just test if it could run 
 *	now or later
 * OUT resp - will run response (includes start location, time, etc.)
 * IN allocate - resource allocation request if set, not a full job
 * IN submit_uid -uid of user issuing the request
 * OUT job_pptr - set to pointer to job record
 * RET 0 or an error code. If the job would only be able to execute with 
 *	some change in partition configuration then 
 *	ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE is returned
 * NOTE: If allocating nodes lx[0-7] to a job and those nodes have cpu counts  
 *	of 4, 4, 4, 4, 8, 8, 4, 4 then num_cpu_groups=3, cpus_per_node={4,8,4}
 *	and cpu_count_reps={4,2,2}
 * globals: job_list - pointer to global job list 
 *	list_part - global list of partition info
 *	default_part_loc - pointer to default partition
 * NOTE: lock_slurmctld on entry: Read config Write job, Write node, Read part
 */
extern int job_allocate(job_desc_msg_t * job_specs, int immediate, 
		int will_run, will_run_response_msg_t **resp, 
		int allocate, uid_t submit_uid, struct job_record **job_pptr);

/*
 * job_cancel_by_assoc_id - Cancel all pending and running jobs with a given
 *	association ID. This happens when an association is deleted (e.g. when
 *	a user is removed from the association database).
 * RET count of cancelled jobs
 */
extern int job_cancel_by_assoc_id(uint32_t assoc_id);

/* log the completion of the specified job */
extern void job_completion_logger(struct job_record  *job_ptr);

/*
 * job_epilog_complete - Note the completion of the epilog script for a 
 *	given job
 * IN job_id      - id of the job for which the epilog was executed
 * IN node_name   - name of the node on which the epilog was executed
 * IN return_code - return code from epilog script
 * RET true if job is COMPLETED, otherwise false
 */
extern bool job_epilog_complete(uint32_t job_id, char *node_name, 
		uint32_t return_code);

/*
 * job_end_time - Process JOB_END_TIME
 * IN time_req_msg - job end time request
 * OUT timeout_msg - job timeout response to be sent
 * RET SLURM_SUCESS or an error code
 */
extern int job_end_time(job_alloc_info_msg_t *time_req_msg,
			srun_timeout_msg_t *timeout_msg);

/* job_fini - free all memory associated with job records */
extern void job_fini (void);

/*
 * job_fail - terminate a job due to initiation failure
 * IN job_id - id of the job to be killed
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_fail(uint32_t job_id);

/*
 * determine if job is ready to execute per the node select plugin
 * IN job_id - job to test
 * OUT ready - 1 if job is ready to execute 0 otherwise
 * RET SLURM error code
 */
extern int job_node_ready(uint32_t job_id, int *ready);

/* 
 * job_signal - signal the specified job
 * IN job_id - id of the job to be signaled
 * IN signal - signal to send, SIGKILL == cancel the job
 * IN batch_flag - signal batch shell only if set
 * IN uid - uid of requesting user
 * RET 0 on success, otherwise ESLURM error code 
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
extern int job_signal(uint32_t job_id, uint16_t signal, uint16_t batch_flag, 
		uid_t uid);

/* 
 * job_step_cancel - cancel the specified job step
 * IN job_id - id of the job to be cancelled
 * IN step_id - id of the job step to be cancelled
 * IN uid - user id of user issuing the RPC
 * RET 0 on success, otherwise ESLURM error code 
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
extern int job_step_cancel (uint32_t job_id, uint32_t job_step_id, uid_t uid );

/*
 * job_step_checkpoint - perform some checkpoint operation
 * IN ckpt_ptr - checkpoint request message
 * IN uid - user id of the user issuing the RPC
 * IN conn_fd - file descriptor on which to send reply
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_step_checkpoint(checkpoint_msg_t *ckpt_ptr,
		uid_t uid, slurm_fd conn_fd);

/*
 * job_step_checkpoint_comp - note job step checkpoint completion
 * IN ckpt_ptr - checkpoint complete status message
 * IN uid - user id of the user issuing the RPC
 * IN conn_fd - file descriptor on which to send reply
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_step_checkpoint_comp(checkpoint_comp_msg_t *ckpt_ptr,
		uid_t uid, slurm_fd conn_fd);
/*
 * job_step_checkpoint_task_comp - note task checkpoint completion
 * IN ckpt_ptr - checkpoint task complete status message
 * IN uid - user id of the user issuing the RPC
 * IN conn_fd - file descriptor on which to send reply
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_step_checkpoint_task_comp(checkpoint_task_comp_msg_t *ckpt_ptr,
                uid_t uid, slurm_fd conn_fd);

/*
 * job_step_suspend - perform some suspend/resume operation
 * IN sus_ptr - suspend/resume request message
 * IN uid - user id of the user issuing the RPC
 * IN conn_fd - file descriptor on which to send reply,
 *              -1 if none
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_suspend(suspend_msg_t *sus_ptr, uid_t uid, 
		slurm_fd conn_fd);

/* 
 * job_complete - note the normal termination the specified job
 * IN job_id - id of the job which completed
 * IN uid - user id of user issuing the RPC
 * IN requeue - job should be run again if possible
 * IN job_return_code - job's return code, if set then set state to JOB_FAILED
 * RET - 0 on success, otherwise ESLURM error code 
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
extern int job_complete (uint32_t job_id, uid_t uid, bool requeue, 
		uint32_t job_return_code);

/*
 * job_independent - determine if this job has a depenentent job pending
 *	or if the job's scheduled begin time is in the future
 * IN job_ptr - pointer to job being tested
 * RET - true if job no longer must be defered for another job
 */
extern bool job_independent(struct job_record *job_ptr);

/*
 * job_req_node_filter - job reqeust node filter.
 * clear from a bitmap the nodes which can not be used for a job
 * test memory size, required features, processor count, etc.
 * IN job_ptr - pointer to node to be scheduled
 * IN/OUT bitmap - set of nodes being considered for use
 * RET SLURM_SUCCESS or EINVAL if can't filter (exclusive OR of features)
 */
extern int job_req_node_filter(struct job_record *job_ptr, 
			       bitstr_t *avail_bitmap);

/*
 * job_requeue - Requeue a running or pending batch job
 * IN uid - user id of user issuing the RPC
 * IN job_id - id of the job to be requeued
 * IN conn_fd - file descriptor on which to send reply, -1 if none
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_requeue (uid_t uid, uint32_t job_id, slurm_fd conn_fd);

/* 
 * job_step_complete - note normal completion the specified job step
 * IN job_id - id of the job to be completed
 * IN step_id - id of the job step to be completed
 * IN uid - user id of user issuing the RPC
 * IN requeue - job should be run again if possible
 * IN job_return_code - job's return code, if set then set state to JOB_FAILED
 * RET 0 on success, otherwise ESLURM error code 
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
extern int job_step_complete (uint32_t job_id, uint32_t job_step_id, 
			uid_t uid, bool requeue, uint32_t job_return_code);

/* 
 * job_step_signal - signal the specified job step
 * IN job_id - id of the job to be cancelled
 * IN step_id - id of the job step to be cancelled
 * IN signal - user id of user issuing the RPC
 * IN uid - user id of user issuing the RPC
 * RET 0 on success, otherwise ESLURM error code 
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
extern int job_step_signal(uint32_t job_id, uint32_t step_id, 
			   uint16_t signal, uid_t uid);

/* 
 * job_time_limit - terminate jobs which have exceeded their time limit
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
extern void job_time_limit (void);

/*
 * kill_job_by_part_name - Given a partition name, deallocate resource for 
 *	its jobs and kill them 
 * IN part_name - name of a partition
 * RET number of killed jobs
 */
extern int kill_job_by_part_name(char *part_name);

/*
 * kill_job_on_node - Kill the specific job_id on a specific node.
 *	agent request per node as they register.
 * IN job_id - id of the job to be killed
 * IN job_ptr - pointer to terminating job (NULL if unknown, e.g. orphaned)
 * IN node_ptr - pointer to the node on which the job resides
 */
extern void kill_job_on_node(uint32_t job_id, struct job_record *job_ptr,
			     struct node_record *node_ptr);

/*
 * kill_running_job_by_node_name - Given a node name, deallocate jobs 
 *	from the node or kill them 
 * IN node_name - name of a node
 * IN step_test - if true, only kill the job if a step is running on the node
 * RET number of killed jobs
 */
extern int kill_running_job_by_node_name(char *node_name, bool step_test);

/* list_compare_config - compare two entry from the config list based upon 
 *	weight, see common/list.h for documentation */
int list_compare_config (void *config_entry1, void *config_entry2);

/*
 * list_find_part - find an entry in the partition list, see common/list.h 
 *	for documentation
 * IN key - partition name or "universal_key" for all partitions 
 * RET 1 if matches key, 0 otherwise 
 * global- part_list - the global partition list
 */
extern int list_find_part (void *part_entry, void *key);

/*
 * load_all_job_state - load the job state from file, recover from last 
 *	checkpoint. Execute this after loading the configuration file data.
 * RET 0 or error code
 */
extern int load_all_job_state ( void );

/*
 * load_all_node_state - Load the node state from file, recover on slurmctld 
 *	restart. Execute this after loading the configuration file data.
 *	Data goes into common storage.
 * IN state_only - if true over-write only node state, features and reason
 * RET 0 or error code
 */
extern int load_all_node_state ( bool state_only );

/*
 * load_part_uid_allow_list - reload the allow_uid list of partitions
 *	if required (updated group file or force set)
 * IN force - if set then always reload the allow_uid list
 */
extern void load_part_uid_allow_list ( int force );

/*
 * load_all_part_state - load the partition state from file, recover from 
 *	slurmctld restart. execute this after loading the configuration 
 *	file data.
 */
extern int load_all_part_state ( void );

/*
 * Create a new job step from data in a buffer (as created by dump_job_step_state)
 * IN/OUT - job_ptr - point to a job for which the step is to be loaded.
 * IN/OUT buffer - location from which to get data, pointers automatically advanced
 */
extern int load_step_state(struct job_record *job_ptr, Buf buffer);

/* make_node_alloc - flag specified node as allocated to a job
 * IN node_ptr - pointer to node being allocated
 * IN job_ptr  - pointer to job that is starting
 */
extern void make_node_alloc(struct node_record *node_ptr,
			    struct job_record *job_ptr);

/* make_node_comp - flag specified node as completing a job
 * IN node_ptr - pointer to node marked for completion of job
 * IN job_ptr  - pointer to job that is completing
 * IN suspended - true if job was previously suspended
 */
extern void make_node_comp(struct node_record *node_ptr,
			   struct job_record *job_ptr, bool suspended);

/*
 * make_node_idle - flag specified node as having finished with a job
 * IN node_ptr - pointer to node reporting job completion
 * IN job_ptr  - pointer to job that just completed
 */
extern void make_node_idle(struct node_record *node_ptr, 
			   struct job_record *job_ptr);

/* msg_to_slurmd - send given msg_type every slurmd, no args */
extern void msg_to_slurmd (slurm_msg_type_t msg_type);

/* node_fini - free all memory associated with node records */
extern void node_fini(void);

/*
 * node_name2bitmap - given a node name regular expression, build a bitmap 
 *	representation
 * IN node_names  - list of nodes
 * IN best_effort - if set don't return an error on invalid node name entries 
 * OUT bitmap     - set to bitmap or NULL on error 
 * RET 0 if no error, otherwise EINVAL
 * global: node_record_table_ptr - pointer to global node table
 * NOTE: the caller must bit_free() memory at bitmap when no longer required
 */
extern int node_name2bitmap (char *node_names, bool best_effort, 
		bitstr_t **bitmap);

/* node_did_resp - record that the specified node is responding
 * IN name - name of the node */
extern void node_did_resp (char *name);

/* 
 * node_not_resp - record that the specified node is not responding
 * IN name - name of the node 
 * IN msg_time - time message was sent
 */
extern void node_not_resp (char *name, time_t msg_time);

/* For every node with the "not_responding" flag set, clear the flag
 * and log that the node is not responding using a hostlist expression */
extern void node_no_resp_msg(void);

/*
 * job_alloc_info - get details about an existing job allocation
 * IN uid - job issuing the code
 * IN job_id - ID of job for which info is requested
 * OUT job_pptr - set to pointer to job record
 */
extern int job_alloc_info(uint32_t uid, uint32_t job_id, 
			  struct job_record **job_pptr);


/* 
 * pack_all_jobs - dump all job information for all jobs in 
 *	machine independent form (for network transmission)
 * OUT buffer_ptr - the pointer is set to the allocated buffer.
 * OUT buffer_size - set to size of the buffer in bytes
 * IN show_flags - job filtering options
 * IN uid - uid of user making request (for partition filtering)
 * global: job_list - global list of job records
 * NOTE: the buffer at *buffer_ptr must be xfreed by the caller
 * NOTE: change _unpack_job_desc_msg() in common/slurm_protocol_pack.c 
 *	whenever the data format changes
 */
extern void pack_all_jobs(char **buffer_ptr, int *buffer_size,
		uint16_t show_flags, uid_t uid);

/* 
 * pack_all_node - dump all configuration and node information for all nodes  
 *	in machine independent form (for network transmission)
 * OUT buffer_ptr - pointer to the stored data
 * OUT buffer_size - set to size of the buffer in bytes
 * IN show_flags - node filtering options
 * IN uid - uid of user making request (for partition filtering)
 * global: node_record_table_ptr - pointer to global node table
 * NOTE: the caller must xfree the buffer at *buffer_ptr
 * NOTE: change slurm_load_node() in api/node_info.c when data format changes
 * NOTE: READ lock_slurmctld config before entry
 */
extern void pack_all_node (char **buffer_ptr, int *buffer_size,
		uint16_t show_flags, uid_t uid);

/* 
 * pack_ctld_job_step_info_response_msg - packs job step info
 * IN job_id - specific id or zero for all
 * IN step_id - specific id or zero for all
 * IN uid - user issuing request
 * IN show_flags - job step filtering options
 * OUT buffer - location to store data, pointers automatically advanced 
 * RET - 0 or error code
 * NOTE: MUST free_buf buffer
 */
extern int pack_ctld_job_step_info_response_msg(uint32_t job_id, 
			uint32_t step_id, uid_t uid, 
			uint16_t show_flags, Buf buffer);

/* 
 * pack_all_part - dump all partition information for all partitions in 
 *	machine independent form (for network transmission)
 * OUT buffer_ptr - the pointer is set to the allocated buffer.
 * OUT buffer_size - set to size of the buffer in bytes
 * IN show_flags - partition filtering options
 * IN uid - uid of user making request (for partition filtering)
 * global: part_list - global list of partition records
 * NOTE: the buffer at *buffer_ptr must be xfreed by the caller
 * NOTE: change slurm_load_part() in api/part_info.c if data format changes
 */
extern void pack_all_part(char **buffer_ptr, int *buffer_size, 
		uint16_t show_flags, uid_t uid);

/* 
 * pack_job - dump all configuration information about a specific job in 
 *	machine independent form (for network transmission)
 * IN dump_job_ptr - pointer to job for which information is requested
 * IN/OUT buffer - buffer in which data is placed, pointers automatically 
 *	updated
 * NOTE: change _unpack_job_desc_msg() in common/slurm_protocol_pack.c
 *	  whenever the data format changes
 */
extern void pack_job (struct job_record *dump_job_ptr, Buf buffer);

/* 
 * pack_part - dump all configuration information about a specific partition 
 *	in machine independent form (for network transmission)
 * IN part_ptr - pointer to partition for which information is requested
 * IN/OUT buffer - buffer in which data is placed, pointers automatically 
 *	updated
 * global: default_part_loc - pointer to the default partition
 * NOTE: if you make any changes here be sure to make the corresponding 
 *	changes to load_part_config in api/partition_info.c
 */
extern void pack_part (struct part_record *part_ptr, Buf buffer);

/* 
 * pack_one_job - dump information for one jobs in 
 *	machine independent form (for network transmission)
 * OUT buffer_ptr - the pointer is set to the allocated buffer.
 * OUT buffer_size - set to size of the buffer in bytes
 * IN job_id - ID of job that we want info for
 * IN uid - uid of user making request (for partition filtering)
 * NOTE: the buffer at *buffer_ptr must be xfreed by the caller
 * NOTE: change _unpack_job_desc_msg() in common/slurm_protocol_pack.c 
 *	whenever the data format changes
 */
extern int pack_one_job(char **buffer_ptr, int *buffer_size,
			 uint32_t job_id, uid_t uid);

/* part_filter_clear - Clear the partition's hidden flag based upon a user's
 * group access. This must follow a call to part_filter_set() */
extern void part_filter_clear(void);

/* part_filter_set - Set the partition's hidden flag based upon a user's 
 * group access. This must be followed by a call to part_filter_clear() */
extern void part_filter_set(uid_t uid);

/* part_fini - free all memory associated with partition records */
extern void part_fini (void);

/*
 * purge_old_job - purge old job records. 
 *	the jobs must have completed at least MIN_JOB_AGE minutes ago
 * global: job_list - global job table
 *	last_job_update - time of last job table update
 */
extern void purge_old_job (void);

/*
 * rehash_jobs - Create or rebuild the job hash table.
 * NOTE: run lock_slurmctld before entry: Read config, write job
 */
extern void rehash_jobs(void);

/* 
 * rehash_node - build a hash table of the node_record entries. 
 * global: node_record_table_ptr - pointer to global node table
 *         node_hash_table - table of hash indecies
 * NOTE: manages memory for node_hash_table
 */
extern void rehash_node (void);

/* update first assigned job id as needed on reconfigure */
extern void reset_first_job_id(void);

/* 
 * reset_job_bitmaps - reestablish bitmaps for existing jobs. 
 *	this should be called after rebuilding node information, 
 *	but before using any job entries.
 * global: last_job_update - time of last job table update
 *	job_list - pointer to global job list
 */
extern void reset_job_bitmaps (void);

/* After a node is returned to service, reset the priority of jobs 
 * which may have been held due to that node being unavailable */
extern void reset_job_priority(void);

/*
 * restore_node_features - Restore node features based upon state
 *      saved (preserves interactive updates)
 */
extern void restore_node_features(void);

/* Update time stamps for job step resume */
extern void resume_job_step(struct job_record *job_ptr);

/* run_backup - this is the backup controller, it should run in standby 
 *	mode, assuming control when the primary controller stops responding */
extern void run_backup(void);

/* Spawn health check function for every node that is not DOWN */
extern void run_health_check(void);

/* save_all_state - save entire slurmctld state for later recovery */
extern void save_all_state(void);

/* sends all jobs in eligible state to accounting.  Only needed at
 * first registration
 */
extern int send_jobs_to_accounting(time_t event_time);

/* send all nodes in a down like state to accounting.  Only needed at
 * first registration
 */
extern int send_nodes_to_accounting(time_t event_time);

/*
 * set_node_down - make the specified node's state DOWN if possible
 *	(not in a DRAIN state), kill jobs as needed 
 * IN name - name of the node 
 * IN reason - why the node is DOWN
 */
extern void set_node_down (char *name, char *reason);

/*
 * set_slurmctld_state_loc - create state directory as needed and "cd" to it
 */
extern void set_slurmctld_state_loc(void);

/* set_slurmd_addr - establish the slurm_addr for the slurmd on each node
 *	Uses common data structures. */
extern void set_slurmd_addr (void);

/*
 * signal_step_tasks - send specific signal to specific job step
 * IN step_ptr - step record pointer
 * IN signal - signal to send
 */
extern void signal_step_tasks(struct step_record *step_ptr, uint16_t signal);

/* Read configuration file.
 * Same name as API function for use in accounting_storage plugin */
extern int slurm_reconfigure(void);

/*
 * slurmctld_shutdown - wake up slurm_rpc_mgr thread via signal
 * RET 0 or error code
 */
extern int slurmctld_shutdown(void);

/* Perform periodic job step checkpoints (per user request) */
extern void step_checkpoint(void);

/* Update a job's record of allocated CPUs when a job step gets scheduled */
extern void step_alloc_lps(struct step_record *step_ptr);

/*
 * step_create - creates a step_record in step_specs->job_id, sets up the
 *	according to the step_specs.
 * IN step_specs - job step specifications
 * OUT new_step_record - pointer to the new step_record (NULL on error)
 * IN kill_job_when_step_done - if set kill the job on step completion
 * IN batch_step - set if step is a batch script
 * RET - 0 or error code
 * NOTE: don't free the returned step_record because that is managed through
 * 	the job.
 */
extern int step_create ( job_step_create_request_msg_t *step_specs, 
			 struct step_record** new_step_record,
			 bool kill_job_when_step_done,
			 bool batch_step );

/*
 * step_layout_create - creates a step_layout according to the inputs.
 * IN job_ptr - job record step belongs to
 * IN step_node_list - node list of hosts in step
 * IN num_tasks - number of tasks in step
 * IN task_dist - type of task distribution
 * IN plane_size - size of plane (only needed for the plane distribution)
 * RET - NULL or slurm_step_layout_t *
 * NOTE: you need to free the returned step_layout usually when the 
 *       step is freed.
 */
extern slurm_step_layout_t *step_layout_create(struct step_record *step_ptr,
					       char *step_node_list,
					       uint16_t node_count,
					       uint32_t num_tasks,
					       uint16_t task_dist,
					       uint32_t plane_size);
/*
 * step_epilog_complete - note completion of epilog on some node and 
 *	release it's switch windows if appropriate. can perform partition 
 *	switch window releases.
 * IN job_ptr - pointer to job which has completed epilog
 * IN node_name - name of node which has completed epilog
 */
extern int step_epilog_complete(struct job_record  *job_ptr, 
	char *node_name);

/* 
 * step_on_node - determine if the specified job has any job steps allocated to 
 * 	the specified node 
 * IN job_ptr - pointer to an active job record
 * IN node_ptr - pointer to a node record
 * RET true of job has step on the node, false otherwise 
 */
extern bool step_on_node(struct job_record  *job_ptr, 
			 struct node_record *node_ptr);

/*
 * step_partial_comp - Note the completion of a job step on at least
 *	some of its nodes
 * IN req     - step_completion_msg RPC from slurmstepd
 * OUT rem    - count of nodes for which responses are still pending
 * OUT max_rc - highest return code for any step thus far
 * RET 0 on success, otherwise ESLURM error code
 */
extern int step_partial_comp(step_complete_msg_t *req, int *rem,
			     uint32_t *max_rc);

/* Update time stamps for job step suspend */
extern void suspend_job_step(struct job_record *job_ptr);

/*
 * Synchronize the batch job in the system with their files.
 * All pending batch jobs must have script and environment files
 * No other jobs should have such files
 */
extern int sync_job_files(void);

/*
 * update_job - update a job's parameters per the supplied specifications
 * IN job_specs - a job's specification
 * IN uid - uid of user issuing RPC
 * RET returns an error code from slurm_errno.h
 * global: job_list - global list of job entries
 *	last_job_update - time of last job table update
 */
extern int update_job (job_desc_msg_t * job_specs, uid_t uid);

/*
 * Modify the account associated with a pending job
 * IN module - where this is called from
 * IN job_ptr - pointer to job which should be modified
 * IN new_account - desired account name
 * RET SLURM_SUCCESS or error code
 */
extern int update_job_account(char *module, struct job_record *job_ptr, 
			      char *new_account);

/* Reset nodes_completing field for all jobs */
extern void update_job_nodes_completing(void);

/* Reset slurmctld logging based upon configuration parameters
 * uses common slurmctld_conf data structure */
extern void update_logging(void);

/* 
 * update_node - update the configuration data for one or more nodes
 * IN update_node_msg - update node request
 * RET 0 or error code
 * global: node_record_table_ptr - pointer to global node table
 */
extern int update_node ( update_node_msg_t * update_node_msg )  ;

/* 
 * update_part - update a partition's configuration data
 * IN part_desc - description of partition changes
 * RET 0 or an error code
 * global: part_list - list of partition entries
 *	last_part_update - update time of partition records
 */
extern int update_part (update_part_msg_t * part_desc );

/*
 * validate_group - validate that the submit uid is authorized to run in 
 *	this partition
 * IN part_ptr - pointer to a partition
 * IN run_uid - user to run the job as
 * RET 1 if permitted to run, 0 otherwise
 */
extern int validate_group (struct part_record *part_ptr, uid_t run_uid);

/*
 * validate_jobs_on_node - validate that any jobs that should be on the node 
 *	are actually running, if not clean up the job records and/or node 
 *	records, call this function after validate_node_specs() sets the node 
 *	state properly 
 * IN reg_msg - node registration message
 */
extern void validate_jobs_on_node(slurm_node_registration_status_msg_t *reg_msg);

/*
 * validate_node_specs - validate the node's specifications as valid, 
 *	if not set state to down, in any case update last_response
 * IN reg_msg - node registration message
 * RET 0 if no error, ENOENT if no such node, EINVAL if values too low
 * NOTE: READ lock_slurmctld config before entry
 */
extern int validate_node_specs(slurm_node_registration_status_msg_t *reg_msg);

/*
 * validate_nodes_via_front_end - validate all nodes on a cluster as having
 *	a valid configuration as soon as the front-end registers. Individual
 *	nodes will not register with this configuration
 * IN reg_msg - node registration message
 * RET 0 if no error, SLURM error code otherwise
 * NOTE: READ lock_slurmctld config before entry
 */
extern int validate_nodes_via_front_end(
		slurm_node_registration_status_msg_t *reg_msg);

/*
 * validate_super_user - validate that the uid is authorized to see 
 *      privileged data (either user root or SlurmUser)
 * IN uid - user to validate
 * RET true if permitted to run, false otherwise
 */
extern bool validate_super_user (uid_t uid);

#endif /* !_HAVE_SLURMCTLD_H */
