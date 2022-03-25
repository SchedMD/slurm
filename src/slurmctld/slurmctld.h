/*****************************************************************************\
 *  slurmctld.h - definitions of functions and structures for slurmcltd use
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010-2014 SchedMD <https://www.schedmd.com>.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
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

#ifndef _HAVE_SLURMCTLD_H
#define _HAVE_SLURMCTLD_H

#include "config.h"

#include <inttypes.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/bitstring.h"
#include "src/common/cron.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/node_conf.h"
#include "src/common/pack.h"
#include "src/common/read_config.h" /* location of slurm_conf */
#include "src/common/job_resources.h"
#include "src/common/slurm_cred.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/timers.h"
#include "src/common/xmalloc.h"

/*****************************************************************************\
 *  GENERAL CONFIGURATION parameters and data structures
\*****************************************************************************/
/* Maximum parallel threads to service incoming RPCs.
 * Also maximum parallel threads to service outgoing RPCs (separate counter).
 * Since some systems schedule pthread on a First-In-Last-Out basis,
 * increasing this value is strongly discouraged. */
#ifndef MAX_SERVER_THREADS
#define MAX_SERVER_THREADS 256
#endif

/* Maximum number of threads to service emails (see MailProg) */
#ifndef MAX_MAIL_THREADS
#define MAX_MAIL_THREADS 64
#endif

/* Perform full slurmctld's state every PERIODIC_CHECKPOINT seconds */
#ifndef PERIODIC_CHECKPOINT
#define	PERIODIC_CHECKPOINT	300
#endif

/* Retry an incomplete RPC agent request every RPC_RETRY_INTERVAL seconds */
#ifndef RPC_RETRY_INTERVAL
#define	RPC_RETRY_INTERVAL	60
#endif

/* Check for jobs reaching their time limit every PERIODIC_TIMEOUT seconds */
#ifndef PERIODIC_TIMEOUT
#define	PERIODIC_TIMEOUT	30
#endif

/* Attempt to purge defunct job records and resend job kill requests
 * every PURGE_JOB_INTERVAL seconds */
#ifndef PURGE_JOB_INTERVAL
#define PURGE_JOB_INTERVAL 60
#endif

/* Process pending trigger events every TRIGGER_INTERVAL seconds */
#ifndef TRIGGER_INTERVAL
#define TRIGGER_INTERVAL 15
#endif

/* Report current node accounting state every PERIODIC_NODE_ACCT seconds */
#ifndef PERIODIC_NODE_ACCT
#define PERIODIC_NODE_ACCT 300
#endif

/* Seconds to wait for backup controller response to REQUEST_CONTROL RPC */
#ifndef CONTROL_TIMEOUT
#define CONTROL_TIMEOUT 30	/* seconds */
#endif

/* Maximum number of requeue attempts before the job is put JOB_REQUEUE_HOLD
 * with reason JobHeldUser.
 */
#ifndef MAX_BATCH_REQUEUE
#define MAX_BATCH_REQUEUE 5
#endif

/*****************************************************************************\
 *  General configuration parameters and data structures
\*****************************************************************************/

typedef struct slurmctld_config {
	pthread_cond_t backup_finish_cond; /* use thread_count_lock */
	time_t	boot_time;
	int	daemonize;
	char    node_name_long[HOST_NAME_MAX];
	char    node_name_short[HOST_NAME_MAX];
	bool	resume_backup;
	bool    scheduling_disabled;
	int	server_thread_count;
	time_t	shutdown_time;
	bool    submissions_disabled;

	slurm_cred_ctx_t cred_ctx;
	pthread_cond_t thread_count_cond;
	pthread_mutex_t thread_count_lock;
	pthread_t thread_id_main;
	pthread_t thread_id_save;
	pthread_t thread_id_sig;
	pthread_t thread_id_power;
	pthread_t thread_id_purge_files;
	pthread_t thread_id_rpc;
} slurmctld_config_t;

/* Job scheduling statistics */
typedef struct diag_stats {
	int proc_req_threads;
	int proc_req_raw;

	uint32_t schedule_cycle_max;
	uint32_t schedule_cycle_last;
	uint32_t schedule_cycle_sum;
	uint32_t schedule_cycle_counter;
	uint32_t schedule_cycle_depth;
	uint32_t schedule_queue_len;

	uint32_t jobs_submitted;
	uint32_t jobs_started;
	uint32_t jobs_completed;
	uint32_t jobs_canceled;
	uint32_t jobs_failed;

	uint32_t job_states_ts;
	uint32_t jobs_pending;
	uint32_t jobs_running;

	uint32_t backfilled_jobs;
	uint32_t last_backfilled_jobs;
	uint32_t backfilled_het_jobs;
	uint32_t bf_active;
	uint32_t bf_cycle_counter;
	uint32_t bf_cycle_last;
	uint32_t bf_cycle_max;
	uint64_t bf_cycle_sum;
	uint32_t bf_depth_sum;
	uint32_t bf_depth_try_sum;
	uint32_t bf_last_depth;
	uint32_t bf_last_depth_try;
	uint32_t bf_queue_len;
	uint32_t bf_queue_len_sum;
	uint32_t bf_table_size;
	uint32_t bf_table_size_sum;
	time_t   bf_when_last_cycle;

	uint32_t latency;
} diag_stats_t;

typedef struct {
	int index;
	bool shutdown;
} shutdown_arg_t;

/* This is used to point out constants that exist in the
 * curr_tres_array in tres_info_t  This should be the same order as
 * the tres_types_t enum that is defined in src/common/slurmdb_defs.h
 */
enum {
	TRES_ARRAY_CPU = 0,
	TRES_ARRAY_MEM,
	TRES_ARRAY_ENERGY,
	TRES_ARRAY_NODE,
	TRES_ARRAY_BILLING,
	TRES_ARRAY_FS_DISK,
	TRES_ARRAY_VMEM,
	TRES_ARRAY_PAGES,
	TRES_ARRAY_TOTAL_CNT
};

extern bool  preempt_send_user_signal;
extern time_t	last_proc_req_start;
extern diag_stats_t slurmctld_diag_stats;
extern slurmctld_config_t slurmctld_config;
extern void *acct_db_conn;
extern uint16_t accounting_enforce;
extern int   association_based_accounting;
extern int   backup_inx;		/* BackupController# index */
extern int   batch_sched_delay;
extern uint32_t   cluster_cpus;
extern bool disable_remote_singleton;
extern int max_depend_depth;
extern bool node_features_updated;
extern pthread_cond_t purge_thread_cond;
extern pthread_mutex_t purge_thread_lock;
extern pthread_mutex_t check_bf_running_lock;
extern int   sched_interval;
extern bool  slurmctld_init_db;
extern int   slurmctld_primary;
extern int   slurmctld_tres_cnt;
extern slurmdb_cluster_rec_t *response_cluster_rec;
extern bool   test_config;
extern int    test_config_rc;

/*****************************************************************************\
 *  NODE parameters and data structures, mostly in src/common/node_conf.h
\*****************************************************************************/
extern bool ping_nodes_now;		/* if set, ping nodes immediately */
extern bool want_nodes_reboot;		/* if set, check for idle nodes */
extern bool ignore_state_errors;

typedef struct node_features {
	uint32_t magic;		/* magic cookie to test data integrity */
	char *name;		/* name of a feature */
	bitstr_t *node_bitmap;	/* bitmap of nodes with this feature */
} node_feature_t;

extern List active_feature_list;/* list of currently active node features */
extern List avail_feature_list;	/* list of available node features */

/*****************************************************************************\
 *  NODE states and bitmaps
 *
 *  avail_node_bitmap       Set if node's state is not DOWN, DRAINING/DRAINED,
 *                          FAILING or NO_RESPOND (i.e. available to run a job)
 *  booting_node_bitmap     Set if node in process of booting
 *  cg_node_bitmap          Set if node in completing state
 *  future_node_bitmap      Set if node in FUTURE state
 *  idle_node_bitmap        Set if node has no jobs allocated to it
 *  power_node_bitmap       Set for nodes which are powered down
 *  share_node_bitmap       Set if no jobs allocated exclusive access to
 *                          resources on that node (cleared if --exclusive
 *                          option specified by job or OverSubscribe=NO
 *                          configured for the job's partition)
 *  up_node_bitmap          Set if the node's state is not DOWN
\*****************************************************************************/
extern bitstr_t *avail_node_bitmap;	/* bitmap of available nodes,
					 * state not DOWN, DRAIN or FAILING */
extern bitstr_t *bf_ignore_node_bitmap;	/* bitmap of nodes made available during
					 * backfill cycle */
extern bitstr_t *booting_node_bitmap;	/* bitmap of booting nodes */
extern bitstr_t *cg_node_bitmap;	/* bitmap of completing nodes */
extern bitstr_t *future_node_bitmap;	/* bitmap of FUTURE nodes */
extern bitstr_t *idle_node_bitmap;	/* bitmap of idle nodes */
extern bitstr_t *power_node_bitmap;	/* Powered down nodes */
extern bitstr_t *share_node_bitmap;	/* bitmap of sharable nodes */
extern bitstr_t *up_node_bitmap;	/* bitmap of up nodes, not DOWN */
extern bitstr_t *rs_node_bitmap;	/* next_state=resume nodes */

/*****************************************************************************\
 *  FRONT_END parameters and data structures
\*****************************************************************************/
#define FRONT_END_MAGIC 0xfe9b82fe

typedef struct {
	uint32_t magic;			/* magic cookie to test data integrity */
					/* DO NOT ALPHABETIZE */
	gid_t *allow_gids;		/* zero terminated list of allowed groups */
	char *allow_groups;		/* allowed group string */
	uid_t *allow_uids;		/* zero terminated list of allowed users */
	char *allow_users;		/* allowed user string */
	time_t boot_time;		/* Time of node boot,
					 * computed from up_time */
	char *comm_name;		/* communications path name to node */
	gid_t *deny_gids;		/* zero terminated list of denied groups */
	char *deny_groups;		/* denied group string */
	uid_t *deny_uids;		/* zero terminated list of denied users */
	char *deny_users;		/* denied user string */
	uint32_t job_cnt_comp;		/* count of completing jobs on node */
	uint16_t job_cnt_run;		/* count of running or suspended jobs */
	time_t last_response;		/* Time of last communication */
	char *name;			/* frontend node name */
	uint32_t node_state;		/* enum node_states, ORed with
					 * NODE_STATE_NO_RESPOND if not
					 * responding */
	bool not_responding;		/* set if fails to respond,
					 * clear after logging this */
	slurm_addr_t slurm_addr;	/* network address */
	uint16_t port;			/* frontend specific port */
	uint16_t protocol_version;	/* Slurm version number */
	char *reason;			/* reason for down frontend node */
	time_t reason_time;		/* Time stamp when reason was set,
					 * ignore if no reason is set. */
	uint32_t reason_uid;   		/* User that set the reason, ignore if
					 * no reason is set. */
	time_t slurmd_start_time;	/* Time of slurmd startup */
	char *version;			/* Slurm version */
} front_end_record_t;

extern front_end_record_t *front_end_nodes;
extern uint16_t front_end_node_cnt;
extern time_t last_front_end_update;	/* time of last front_end update */

/*****************************************************************************\
 *  PARTITION parameters and data structures
\*****************************************************************************/
#define PART_MAGIC 0xaefe8495

typedef struct {
	slurmdb_bf_usage_t *job_usage;
	slurmdb_bf_usage_t *resv_usage;
	xhash_t *user_usage;
} bf_part_data_t;

typedef struct {
	uint32_t magic;		/* magic cookie to test data integrity */
				/* DO NOT ALPHABETIZE */
	char *allow_accounts;	/* comma delimited list of accounts,
				 * NULL indicates all */
	char **allow_account_array; /* NULL terminated list of allowed
				 * accounts */
	char *allow_alloc_nodes;/* comma delimited list of allowed
				 * allocating nodes
				 * NULL indicates all */
	char *allow_groups;	/* comma delimited list of groups,
				 * NULL indicates all */
	uid_t *allow_uids;	/* zero terminated list of allowed user IDs */
	char *allow_qos;	/* comma delimited list of qos,
				 * NULL indicates all */
	bitstr_t *allow_qos_bitstr; /* (DON'T PACK) assocaited with
				 * char *allow_qos but used internally */
	char *alternate; 	/* name of alternate partition */
	double *billing_weights;    /* array of TRES billing weights */
	char   *billing_weights_str;/* per TRES billing weight string */
	uint32_t cpu_bind;	/* default CPU binding type */
	uint64_t def_mem_per_cpu; /* default MB memory per allocated CPU */
	uint32_t default_time;	/* minutes, NO_VAL or INFINITE */
	char *deny_accounts;	/* comma delimited list of denied accounts */
	char **deny_account_array; /* NULL terminated list of denied accounts */
	char *deny_qos;		/* comma delimited list of denied qos */
	bitstr_t *deny_qos_bitstr; /* (DON'T PACK) associated with
				 * char *deny_qos but used internallly */
	uint16_t flags;		/* see PART_FLAG_* in slurm.h */
	uint32_t grace_time;	/* default preempt grace time in seconds */
	List job_defaults_list;	/* List of job_defaults_t elements */
	uint32_t max_cpus_per_node; /* maximum allocated CPUs per node */
	uint64_t max_mem_per_cpu; /* maximum MB memory per allocated CPU */
	uint32_t max_nodes;	/* per job or INFINITE */
	uint32_t max_nodes_orig;/* unscaled value (c-nodes on BlueGene) */
	uint16_t max_share;	/* number of jobs to gang schedule */
	uint32_t max_time;	/* minutes or INFINITE */
	uint32_t min_nodes;	/* per job */
	uint32_t min_nodes_orig;/* unscaled value (c-nodes on BlueGene) */
	char *name;		/* name of the partition */
	bitstr_t *node_bitmap;	/* bitmap of nodes in partition */
	char *nodes;		/* comma delimited list names of nodes */
	double   norm_priority;	/* normalized scheduling priority for
				 * jobs (DON'T PACK) */
	uint16_t over_time_limit; /* job's time limit can be exceeded by this
				   * number of minutes before cancellation */
	uint16_t preempt_mode;	/* See PREEMPT_MODE_* in slurm/slurm.h */
	uint16_t priority_job_factor;	/* job priority weight factor */
	uint16_t priority_tier;	/* tier for scheduling and preemption */
	char *qos_char;         /* requested QOS from slurm.conf */
	slurmdb_qos_rec_t *qos_ptr; /* pointer to the quality of
				     * service record attached to this
				     * partition confirm the value before use */
	uint16_t resume_timeout; /* time required in order to perform a node
				  * resume operation */
	uint16_t state_up;	/* See PARTITION_* states in slurm.h */
	uint32_t suspend_time;  /* node idle for this long before power save
				 * mode */
	uint16_t suspend_timeout; /* time required in order to perform a node
				   * suspend operation */
	uint32_t total_nodes;	/* total number of nodes in the partition */
	uint32_t total_cpus;	/* total number of cpus in the partition */
	uint32_t max_cpu_cnt;	/* max # of cpus on a node in the partition */
	uint32_t max_core_cnt;	/* max # of cores on a node in the partition */
	uint16_t cr_type;	/* Custom CR values for partition (if supported by select plugin) */
	uint64_t *tres_cnt;	/* array of total TRES in partition. NO_PACK */
	char     *tres_fmt_str;	/* str of configured TRES in partition */
	bf_part_data_t *bf_data;/* backfill data, NO PACK */
} part_record_t;

extern List part_list;			/* list of part_record entries */
extern time_t last_part_update;		/* time of last part_list update */
extern part_record_t default_part;	/* default configuration values */
extern char *default_part_name;		/* name of default partition */
extern part_record_t *default_part_loc;	/* default partition ptr */

#define DEF_PART_MAX_PRIORITY   1
extern uint16_t part_max_priority;      /* max priority_job_factor in all parts */

/*****************************************************************************\
 *  RESERVATION parameters and data structures
\*****************************************************************************/

#define RESV_CTLD_ACCT_NOT       0x00000001
#define RESV_CTLD_USER_NOT       0x00000002
#define RESV_CTLD_FULL_NODE      0x00000004
#define RESV_CTLD_NODE_FLAGS_SET 0x00000008
#define RESV_CTLD_EPILOG         0x00000010
#define RESV_CTLD_PROLOG         0x00000020

typedef struct slurmctld_resv {
	uint16_t magic;		/* magic cookie, RESV_MAGIC		*/
				/* DO NOT ALPHABETIZE			*/
	char *accounts;		/* names of accounts permitted to use	*/
	int account_cnt;	/* count of accounts permitted to use	*/
	char **account_list;	/* list of accounts permitted to use	*/
	char *assoc_list;	/* list of associations			*/
	uint32_t boot_time;	/* time it would take to reboot a node	*/
	char *burst_buffer;	/* burst buffer resources		*/
	uint32_t ctld_flags;    /* see RESV_CTLD_* above */
	bitstr_t *core_bitmap;	/* bitmap of reserved cores		*/
	uint32_t core_cnt;	/* number of reserved cores		*/
	job_resources_t *core_resrcs;	/* details of allocated cores	*/
	uint32_t duration;	/* time in seconds for this
				 * reservation to last                  */
	time_t end_time;	/* end time of reservation		*/
	time_t idle_start_time;	/* first time when reservation had no jobs
				 * running on it */
	char *features;		/* required node features		*/
	uint64_t flags;		/* see RESERVE_FLAG_* in slurm.h	*/
	char *groups;		/* names of linux groups permitted to use */
	uint32_t job_pend_cnt;	/* number of pending jobs		*/
	uint32_t job_run_cnt;	/* number of running jobs		*/
	List license_list;	/* structure with license info		*/
	char *licenses;		/* required system licenses		*/
	uint32_t max_start_delay;/* Maximum delay in which jobs outside of the
				  * reservation will be permitted to overlap
				  * once any jobs are queued for the
				  * reservation */
	char *name;		/* name of reservation			*/
	bitstr_t *node_bitmap;	/* bitmap of reserved nodes		*/
	uint32_t node_cnt;	/* count of nodes required		*/
	char *node_list;	/* list of reserved nodes or ALL	*/
	char *partition;	/* name of partition to be used		*/
	part_record_t *part_ptr;/* pointer to partition used		*/
	uint32_t purge_comp_time; /* If PURGE_COMP flag is set the amount of
				   * minutes this reservation will sit idle
				   * until it is revoked.
				   */
	uint32_t resv_id;	/* unique reservation ID, internal use	*/
	uint32_t resv_watts;	/* amount of power to reserve */
	time_t start_time;	/* start time of reservation		*/
	time_t start_time_first;/* when the reservation first started	*/
	time_t start_time_prev;	/* If start time was changed this is
				 * the pervious start time.  Needed
				 * for accounting */
	char *tres_fmt_str;     /* formatted string of tres to deal with */
	char *tres_str;         /* simple string of tres to deal with */
	char *users;		/* names of users permitted to use	*/
	int user_cnt;		/* count of users permitted to use	*/
	uid_t *user_list;	/* array of users permitted to use	*/
} slurmctld_resv_t;

extern List resv_list;		/* list of slurmctld_resv entries */
extern time_t last_resv_update;	/* time of last resv_list update */

/*****************************************************************************\
 *  JOB parameters and data structures
\*****************************************************************************/
extern time_t last_job_update;	/* time of last update to job records */

#define DETAILS_MAGIC	0xdea84e7
#define JOB_MAGIC	0xf0b7392c

#define FEATURE_OP_OR   0
#define FEATURE_OP_AND  1
#define FEATURE_OP_XOR  2
#define FEATURE_OP_XAND 3
#define FEATURE_OP_END  4		/* last entry lacks separator */
typedef struct job_feature {
	char *name;			/* name of feature */
	bool changeable;		/* return value of
					 * node_features_g_changeable_feature */
	uint16_t count;			/* count of nodes with this feature */
	uint8_t op_code;		/* separator, see FEATURE_OP_ above */
	bitstr_t *node_bitmap_active;	/* nodes with this feature active */
	bitstr_t *node_bitmap_avail;	/* nodes with this feature available */
	uint16_t paren;			/* count of enclosing parenthesis */
} job_feature_t;

/*
 * these related to the JOB_SHARED_ macros in slurm.h
 * but with the logic for zero vs one inverted
 */
#define WHOLE_NODE_REQUIRED	0x01
#define WHOLE_NODE_USER		0x02
#define WHOLE_NODE_MCS		0x03

/* job_details - specification of a job's constraints */
struct job_details {
	uint32_t magic;			/* magic cookie for data integrity */
					/* DO NOT ALPHABETIZE */
	char *acctg_freq;		/* accounting polling interval */
	time_t accrue_time;             /* Time when we start accruing time for
					 * priority, */
	uint32_t argc;			/* count of argv elements */
	char **argv;			/* arguments for a batch job script */
	time_t begin_time;		/* start at this time (srun --begin),
					 * resets to time first eligible
					 * (all dependencies satisfied) */
	char *cluster_features;		/* required cluster_features */
	uint16_t contiguous;		/* set if requires contiguous nodes */
	uint16_t core_spec;		/* specialized core/thread count,
					 * threads if CORE_SPEC_THREAD flag set */
	char *cpu_bind;			/* binding map for map/mask_cpu - This
					 * currently does not matter to the
					 * job allocation, setting this does
					 * not do anything for steps. */
	uint16_t cpu_bind_type;		/* Default CPU bind type for steps,
					 * see cpu_bind_type_t */
	uint32_t cpu_freq_min;  	/* Minimum cpu frequency  */
	uint32_t cpu_freq_max;  	/* Maximum cpu frequency  */
	uint32_t cpu_freq_gov;  	/* cpu frequency governor */
	uint16_t cpus_per_task;		/* number of processors required for
					 * each task */
	cron_entry_t *crontab_entry;	/* crontab entry (job submitted through
					 * scrontab) */
	uint16_t orig_cpus_per_task;	/* requested value of cpus_per_task */
	List depend_list;		/* list of job_ptr:state pairs */
	char *dependency;		/* wait for other jobs */
	char *orig_dependency;		/* original value (for archiving) */
	uint16_t env_cnt;		/* size of env_sup (see below) */
	char **env_sup;			/* supplemental environment variables */
	bitstr_t *exc_node_bitmap;	/* bitmap of excluded nodes */
	char *exc_nodes;		/* excluded nodes */
	uint32_t expanding_jobid;	/* ID of job to be expanded */
	char *extra;			/* extra field, unused */
	List feature_list;		/* required features with node counts */
	char *features;			/* required features */
	uint32_t max_cpus;		/* maximum number of cpus */
	uint32_t orig_max_cpus;		/* requested value of max_cpus */
	uint32_t max_nodes;		/* maximum number of nodes */
	multi_core_data_t *mc_ptr;	/* multi-core specific data */
	char *mem_bind;			/* binding map for map/mask_cpu */
	uint16_t mem_bind_type;		/* see mem_bind_type_t */
	uint32_t min_cpus;		/* minimum number of cpus */
	uint32_t orig_min_cpus;		/* requested value of min_cpus */
	int min_gres_cpu;		/* Minimum CPU count per node required
					 * to satisfy GRES requirements,
					 * not saved/restored, but rebuilt */
	int min_job_gres_cpu;		/* CPU count per job required
					 * to satisfy GRES requirements,
					 * not saved/restored, but rebuilt */
	uint32_t min_nodes;		/* minimum number of nodes */
	uint32_t nice;			/* requested priority change,
					 * NICE_OFFSET == no change */
	uint16_t ntasks_per_node;	/* number of tasks on each node */
	uint16_t ntasks_per_tres;	/* number of tasks on each GPU */
	uint32_t num_tasks;		/* number of tasks to start */
	uint8_t open_mode;		/* stdout/err append or truncate */
	uint8_t overcommit;		/* processors being over subscribed */
	uint16_t plane_size;		/* plane size when task_dist =
					 * SLURM_DIST_PLANE */
	/* job constraints: */
	uint32_t pn_min_cpus;		/* minimum processors per node */
	uint32_t orig_pn_min_cpus;	/* requested value of pn_min_cpus */
	uint64_t pn_min_memory;		/* minimum memory per node (MB) OR
					 * memory per allocated
					 * CPU | MEM_PER_CPU */
	uint64_t orig_pn_min_memory;	/* requested value of pn_min_memory */
	uint32_t pn_min_tmp_disk;	/* minimum tempdisk per node, MB */
	uint8_t prolog_running;		/* set while prolog_slurmctld is
					 * running */
	uint32_t reserved_resources;	/* CPU minutes of resources reserved
					 * for this job while it was pending */
	bitstr_t *req_node_bitmap;	/* bitmap of required nodes */
	time_t preempt_start_time;	/* time that preeption began to start
					 * this job */
	char *req_context;		/* requested SELinux context */
	char *req_nodes;		/* required nodes */
	uint16_t requeue;		/* controls ability requeue job */
	uint8_t share_res;		/* set if job can share resources with
					 * other jobs */
	char *script;			/* DBD USE ONLY DON'T PACK:
					 * job's script */
	char *std_err;			/* pathname of job's stderr file */
	char *std_in;			/* pathname of job's stdin file */
	char *std_out;			/* pathname of job's stdout file */
	char *submit_line;              /* The command issued with all it's
					 * options in a string */
	time_t submit_time;		/* time of submission */
	uint32_t task_dist;		/* task layout for this job. Only
					 * useful when Consumable Resources
					 * is enabled */
	uint32_t usable_nodes;		/* node count needed by preemption */
	uint8_t whole_node;		/* WHOLE_NODE_REQUIRED: 1: --exclusive
					 * WHOLE_NODE_USER: 2: --exclusive=user
					 * WHOLE_NODE_MCS:  3: --exclusive=mcs */
	char *work_dir;			/* pathname of working directory */
	uint16_t x11;			/* --x11 flags */
	char *x11_magic_cookie;		/* x11 magic cookie */
	char *x11_target;		/* target host, or socket if port == 0 */
	uint16_t x11_target_port;	/* target TCP port on alloc_node */
};

typedef struct job_array_struct {
	uint32_t task_cnt;		/* count of remaining task IDs */
	bitstr_t *task_id_bitmap;	/* bitmap of remaining task IDs */
	char *task_id_str;		/* string describing remaining task IDs,
					 * needs to be recalculated if NULL */
	uint32_t array_flags;		/* Flags to control behavior (FUTURE) */
	uint32_t max_run_tasks;		/* Maximum number of running tasks */
	uint32_t tot_run_tasks;		/* Current running task count */
	uint32_t min_exit_code;		/* Minimum exit code from any task */
	uint32_t max_exit_code;		/* Maximum exit code from any task */
	uint32_t pend_run_tasks;	/* Number of tasks ready to run due to
					 * preempting other jobs */
	uint32_t tot_comp_tasks;	/* Completed task count */
} job_array_struct_t;

#define ADMIN_SET_LIMIT 0xffff

typedef struct {
	uint16_t qos;
	uint16_t time;
	uint16_t *tres;
} acct_policy_limit_set_t;

typedef struct {
	uint32_t cluster_lock;		/* sibling that has lock on job */
	char    *origin_str;		/* origin cluster name */
	uint64_t siblings_active;	/* bitmap of active sibling ids. */
	char    *siblings_active_str;	/* comma separated list of actual
					   sibling names */
	uint64_t siblings_viable;	/* bitmap of viable sibling ids. */
	char    *siblings_viable_str;	/* comma separated list of viable
					   sibling names */
} job_fed_details_t;

#define HETJOB_PRIO_MIN	0x0001	/* Sort by mininum component priority[tier] */
#define HETJOB_PRIO_MAX	0x0002	/* Sort by maximum component priority[tier] */
#define HETJOB_PRIO_AVG	0x0004	/* Sort by average component priority[tier] */

typedef struct {
	bool any_resv;			/* at least one component with resv */
	uint32_t priority_tier;		/* whole hetjob calculated tier */
	uint32_t priority;		/* whole hetjob calculated priority */
} het_job_details_t;

/*
 * NOTE: When adding fields to the job_record, or any underlying structures,
 * be sure to sync with job_array_split.
 */
typedef struct job_record job_record_t;
struct job_record {
	uint32_t magic;			/* magic cookie for data integrity */
					/* DO NOT ALPHABETIZE */
	char    *account;		/* account number to charge */
	char    *admin_comment;		/* administrator's arbitrary comment */
	char	*alias_list;		/* node name to address aliases */
	char    *alloc_node;		/* local node making resource alloc */
	uint16_t alloc_resp_port;	/* RESPONSE_RESOURCE_ALLOCATION port */
	uint32_t alloc_sid;		/* local sid making resource alloc */
	uint32_t array_job_id;		/* job_id of a job array or 0 if N/A */
	uint32_t array_task_id;		/* task_id of a job array */
	job_array_struct_t *array_recs;	/* job array details,
					 * only in meta-job record */
	uint32_t assoc_id;              /* used for accounting plugins */
	slurmdb_assoc_rec_t *assoc_ptr; /* job's assoc record ptr confirm the
					 * value before use */
	char *batch_features;		/* features required for batch script */
	uint16_t batch_flag;		/* 1 or 2 if batch job (with script),
					 * 2 indicates retry mode (one retry) */
	char *batch_host;		/* host executing batch script */
	double billable_tres;		/* calculated billable tres for the
					 * job, as defined by the partition's
					 * billing weight. Recalculated upon job
					 * resize.  Cannot be calculated until
					 * the job is alloocated resources. */
	uint64_t bit_flags;             /* various job flags */
	char *burst_buffer;		/* burst buffer specification */
	char *burst_buffer_state;	/* burst buffer state */
	char *clusters;			/* clusters job is submitted to with -M
					   option */
	char *comment;			/* arbitrary comment */
	char *container;		/* OCI container bundle path */
	uint32_t cpu_cnt;		/* current count of CPUs held
					 * by the job, decremented while job is
					 * completing */
	char *cpus_per_tres;		/* semicolon delimited list of TRES=# values */
	uint16_t cr_enabled;            /* specify if Consumable Resources
					 * is enabled. Needed since CR deals
					 * with a finer granularity in its
					 * node/cpu scheduling (available cpus
					 * instead of available nodes) than the
					 * linear plugin
					 * 0 if cr is NOT enabled,
					 * 1 if cr is enabled */
	uint32_t db_flags;              /* Flags to send to the database
					 * record */
	uint64_t db_index;              /* used only for database plugins */
	time_t deadline;		/* deadline */
	uint32_t delay_boot;		/* Delay boot for desired node mode */
	uint32_t derived_ec;		/* highest exit code of all job steps */
	struct job_details *details;	/* job details */
	uint16_t direct_set_prio;	/* Priority set directly if
					 * set the system will not
					 * change the priority any further. */
	time_t end_time;		/* time execution ended, actual or
					 * expected. if terminated from suspend
					 * state, this is time suspend began */
	time_t end_time_exp;		/* when we believe the job is
					   going to end. */
	bool epilog_running;		/* true of EpilogSlurmctld is running */
	uint32_t exit_code;		/* exit code for job (status from
					 * wait call) */
	job_fed_details_t *fed_details;	/* details for federated jobs. */
	front_end_record_t *front_end_ptr; /* Pointer to front-end node running
					 * this job */
	List gres_list_req;		/* Requested generic resource allocation
					   detail */
	List gres_list_alloc;		/* Allocated generic resource allocation
					 * detail */
	uint32_t gres_detail_cnt;	/* Count of gres_detail_str records,
					 * one per allocated node */
	char **gres_detail_str;		/* Details of GRES index alloc per node */
	char *gres_used;		/* Actual GRES use added over all nodes
					 * to be passed to slurmdbd */
	uint32_t group_id;		/* group submitted under */
	het_job_details_t *het_details;	/* HetJob details */
	uint32_t het_job_id;		/* job ID of HetJob leader */
	char *het_job_id_set;		/* job IDs for all components */
	uint32_t het_job_offset;	/* HetJob component index */
	List het_job_list;		/* List of job pointers to all
					 * components */
	uint32_t job_id;		/* job ID */
	job_record_t *job_next;		/* next entry with same hash index */
	job_record_t *job_array_next_j;	/* job array linked list by job_id */
	job_record_t *job_array_next_t;	/* job array linked list by task_id */
	job_record_t *job_preempt_comp; /* het job preempt component */
	job_resources_t *job_resrcs;	/* details of allocated cores */
	uint32_t job_state;		/* state of the job */
	uint16_t kill_on_node_fail;	/* 1 if job should be killed on
					 * node failure */
	time_t last_sched_eval;		/* last time job was evaluated for scheduling */
	char *licenses;			/* licenses required by the job */
	List license_list;		/* structure with license info */
	acct_policy_limit_set_t limit_set; /* flags if indicate an
					    * associated limit was set from
					    * a limit instead of from
					    * the request, or if the
					    * limit was set from admin */
	uint16_t mail_type;		/* see MAIL_JOB_* in slurm.h */
	char *mail_user;		/* user to get e-mail notification */
	char *mem_per_tres;		/* semicolon delimited list of TRES=# values */
	char *mcs_label;		/* mcs_label if mcs plugin in use */
	char *name;			/* name of the job */
	char *network;			/* network/switch requirement spec */
	uint32_t next_step_id;		/* next step id to be used */
	char *nodes;			/* list of nodes allocated to job */
	slurm_addr_t *node_addr;	/* addresses of the nodes allocated to
					 * job */
	bitstr_t *node_bitmap;		/* bitmap of nodes allocated to job */
	bitstr_t *node_bitmap_cg;	/* bitmap of nodes completing job */
	uint32_t node_cnt;		/* count of nodes currently
					 * allocated to job */
	uint32_t node_cnt_wag;		/* count of nodes Slurm thinks
					 * will be allocated when the
					 * job is pending and node_cnt
					 * wasn't given by the user.
					 * This is packed in total_nodes
					 * when dumping state.  When
					 * state is read in check for
					 * pending state and set this
					 * instead of total_nodes */
	char *nodes_completing;		/* nodes still in completing state
					 * for this job, used to ensure
					 * epilog is not re-run for job */
	char *origin_cluster;		/* cluster name that the job was
					 * submitted from */
	uint16_t other_port;		/* port for client communications */
	char *partition;		/* name of job partition(s) */
	List part_ptr_list;		/* list of pointers to partition recs */
	bool part_nodes_missing;	/* set if job's nodes removed from this
					 * partition */
	part_record_t *part_ptr;	/* pointer to the partition record */
	uint8_t power_flags;		/* power management flags,
					 * see SLURM_POWER_FLAGS_ */
	time_t pre_sus_time;		/* time job ran prior to last suspend */
	time_t preempt_time;		/* job preemption signal time */
	bool preempt_in_progress;	/* Premption of other jobs in progress
					 * in order to start this job,
					 * (Internal use only, don't save) */
	uint32_t prep_epilog_cnt;	/* count of epilog async tasks left */
	uint32_t prep_prolog_cnt;	/* count of prolog async tasks left */
	bool prep_prolog_failed;	/* any prolog_slurmctld failed */
	uint32_t priority;		/* relative priority of the job,
					 * zero == held (don't initiate) */
	uint32_t *priority_array;	/* partition based priority */
	priority_factors_object_t *prio_factors; /* cached value used
						  * by sprio command */
	uint32_t profile;		/* Acct_gather_profile option */
	uint32_t qos_id;		/* quality of service id */
	slurmdb_qos_rec_t *qos_ptr;	/* pointer to the quality of
					 * service record used for
					 * this job, confirm the
					 * value before use */
	void *qos_blocking_ptr;		/* internal use only, DON'T PACK */
	uint8_t reboot;			/* node reboot requested before start */
	uint16_t restart_cnt;		/* count of restarts */
	time_t resize_time;		/* time of latest size change */
	uint32_t resv_id;		/* reservation ID */
	List resv_list;                 /* Filled in if the job is requesting
					 * more than one reservation,
					 * DON'T PACK. */
	char *resv_name;		/* reservation name */
	struct slurmctld_resv *resv_ptr;/* reservation structure pointer */
	uint32_t requid;	    	/* requester user ID */
	char *resp_host;		/* host for srun communications */
	char *sched_nodes;		/* list of nodes scheduled for job */
	dynamic_plugin_data_t *select_jobinfo;/* opaque data, BlueGene */
	char *selinux_context;		/* SELinux context */
	uint32_t site_factor;		/* factor to consider in priority */
	char **spank_job_env;		/* environment variables for job prolog
					 * and epilog scripts as set by SPANK
					 * plugins */
	uint32_t spank_job_env_size;	/* element count in spank_env */
	uint16_t start_protocol_ver;	/* Slurm version job was
					 * started with either the
					 * creating message or the
					 * lowest slurmd in the
					 * allocation */
	time_t start_time;		/* time execution begins,
					 * actual or expected */
	char *state_desc;		/* optional details for state_reason */
	uint32_t state_reason;		/* reason job still pending or failed
					 * see slurm.h:enum job_state_reason */
	uint32_t state_reason_prev_db;	/* Previous state_reason that isn't
					 * priority or resources, only stored in
					 * the database. */
	List step_list;			/* list of job's steps */
	time_t suspend_time;		/* time job last suspended or resumed */
	char *system_comment;		/* slurmctld's arbitrary comment */
	time_t time_last_active;	/* time of last job activity */
	uint32_t time_limit;		/* time_limit minutes or INFINITE,
					 * NO_VAL implies partition max_time */
	uint32_t time_min;		/* minimum time_limit minutes or
					 * INFINITE,
					 * zero implies same as time_limit */
	time_t tot_sus_time;		/* total time in suspend state */
	uint32_t total_cpus;		/* number of allocated cpus,
					 * for accounting */
	uint32_t total_nodes;		/* number of allocated nodes
					 * for accounting */
	char *tres_bind;		/* Task to TRES binding directives */
	char *tres_freq;		/* TRES frequency directives */
	char *tres_per_job;		/* comma delimited list of TRES values */
	char *tres_per_node;		/* comma delimited list of TRES values */
	char *tres_per_socket;		/* comma delimited list of TRES values */
	char *tres_per_task;		/* comma delimited list of TRES values */
	uint64_t *tres_req_cnt;         /* array of tres counts requested
					 * based off g_tres_count in
					 * assoc_mgr */
	char *tres_req_str;             /* string format of
					 * tres_req_cnt primarily
					 * used for state */
	char *tres_fmt_req_str;         /* formatted req tres string for job */
	uint64_t *tres_alloc_cnt;       /* array of tres counts allocated
					 * based off g_tres_count in
					 * assoc_mgr */
	char *tres_alloc_str;           /* simple tres string for job */
	char *tres_fmt_alloc_str;       /* formatted tres string for job */
	uint32_t user_id;		/* user the job runs as */
	char *user_name;		/* string version of user */
	uint16_t wait_all_nodes;	/* if set, wait for all nodes to boot
					 * before starting the job */
	uint16_t warn_flags;		/* flags for signal to send */
	uint16_t warn_signal;		/* signal to send before end_time */
	uint16_t warn_time;		/* when to send signal before
					 * end_time (secs) */
	char *wckey;			/* optional wckey */

	/* Request number of switches support */
	uint32_t req_switch;  /* Minimum number of switches                */
	uint32_t wait4switch; /* Maximum time to wait for minimum switches */
	bool     best_switch; /* true=min number of switches met           */
	time_t wait4switch_start; /* Time started waiting for switch       */
};

/* Job dependency specification, used in "depend_list" within job_record */
typedef enum {
	SLURM_DEPEND_AFTER = 1,	/* After job begins */
	SLURM_DEPEND_AFTER_ANY,	/* After job completes */
	SLURM_DEPEND_AFTER_NOT_OK, /* After job fails */
	SLURM_DEPEND_AFTER_OK,	/* After job completes successfully */
	SLURM_DEPEND_SINGLETON,	/* Only one job for this
				 * user/name at a time */
	SLURM_DEPEND_EXPAND,	/* Expand running job */
	SLURM_DEPEND_AFTER_CORRESPOND, /* After corresponding job array
					* elements completes */
	SLURM_DEPEND_BURST_BUFFER, /* After job burst buffer
				    * stage-out completes */
} slurm_depend_types_t;

#define SLURM_FLAGS_OR		0x0001	/* OR job dependencies */
#define SLURM_FLAGS_REMOTE      0x0002  /* Is a remote dependency */

/* Used as values for depend_state in depend_spec_t */
enum {
	DEPEND_NOT_FULFILLED = 0,
	DEPEND_FULFILLED,
	DEPEND_FAILED
};

typedef struct depend_spec {
	uint32_t	array_task_id;	/* INFINITE for all array tasks */
	uint16_t	depend_type;	/* SLURM_DEPEND_* type */
	uint16_t	depend_flags;	/* SLURM_FLAGS_* type */
	uint32_t        depend_state;   /* Status of the dependency */
	uint32_t        depend_time;    /* time to wait (mins) */
	uint32_t	job_id;		/* Slurm job_id */
	job_record_t   *job_ptr;	/* pointer to this job */
	uint64_t 	singleton_bits; /* which clusters have satisfied the
					   singleton dependency */
} depend_spec_t;

#define STEP_FLAG 0xbbbb
#define STEP_MAGIC 0xcafecafe

typedef struct {
	uint32_t magic;			/* magic cookie to test data integrity */
					/* DO NOT ALPHABETIZE */
	char *container;		/* OCI Container bundle path */
	bitstr_t *core_bitmap_job;	/* bitmap of cores allocated to this
					 * step relative to job's nodes,
					 * see src/common/job_resources.h */
	uint32_t cpu_count;		/* count of step's CPUs */
	uint32_t cpu_freq_min; 		/* Minimum cpu frequency  */
	uint32_t cpu_freq_max; 		/* Maximum cpu frequency  */
	uint32_t cpu_freq_gov; 		/* cpu frequency governor */
	uint16_t cpus_per_task;		/* cpus per task initiated */
	char *cpus_per_tres;		/* semicolon delimited list of TRES=# values */
	uint16_t cyclic_alloc;		/* set for cyclic task allocation
					 * across nodes */
	uint32_t exit_code;		/* highest exit code from any task */
	bitstr_t *exit_node_bitmap;	/* bitmap of exited nodes */
	ext_sensors_data_t *ext_sensors; /* external sensors plugin data */
	uint32_t flags;		        /* flags from step_spec_flags_t */
	List gres_list_req;		/* generic resource request detail */
	List gres_list_alloc;		/* generic resource allocation detail */
	char *host;			/* host for srun communications */
	job_record_t *job_ptr;		/* ptr to the job that owns the step */
	jobacctinfo_t *jobacct;         /* keep track of process info in the
					 * step */
	char *mem_per_tres;		/* semicolon delimited list of TRES=# values */
	uint64_t *memory_allocated;	/* per node array of memory allocated */
	char *name;			/* name of job step */
	char *network;			/* step's network specification */
	uint64_t pn_min_memory;		/* minimum real memory per node OR
					 * real memory per CPU | MEM_PER_CPU,
					 * default=0 (use job limit) */
	uint16_t port;			/* port for srun communications */
	time_t pre_sus_time;		/* time step ran prior to last suspend */
	uint16_t start_protocol_ver;	/* Slurm version step was
					 * started with either srun
					 * or the lowest slurmd
					 * version it is talking to */
	int *resv_port_array;		/* reserved port indexes */
	uint16_t resv_port_cnt;		/* count of ports reserved per node */
	char *resv_ports;		/* ports reserved for job */
	uint32_t requid;	    	/* requester user ID */
	time_t start_time;		/* step allocation start time */
	uint32_t time_limit;	  	/* step allocation time limit */
	dynamic_plugin_data_t *select_jobinfo;/* opaque data, BlueGene */
	uint32_t srun_pid;		/* PID of srun (also see host/port) */
	uint32_t state;			/* state of the step. See job_states */
	slurm_step_id_t step_id;	/* step number */
	slurm_step_layout_t *step_layout;/* info about how tasks are laid out
					  * in the step */
	bitstr_t *step_node_bitmap;	/* bitmap of nodes allocated to job
					 * step */
/*	time_t suspend_time;		 * time step last suspended or resumed
					 * implicitly the same as suspend_time
					 * in the job record */
	char *submit_line;              /* The command issued with all it's
					 * options in a string */
	dynamic_plugin_data_t *switch_job; /* switch context, opaque */
	uint16_t threads_per_core;	/* step threads-per-core */
	time_t time_last_active;	/* time step was last found on node */
	time_t tot_sus_time;		/* total time in suspended state */
	char *tres_alloc_str;           /* simple TRES string for step */
	char *tres_bind;		/* Task to TRES binding directives */
	char *tres_fmt_alloc_str;       /* formatted tres string for step */
	char *tres_freq;		/* TRES frequency directives */
	char *tres_per_step;		/* semicolon delimited list of TRES=# values */
	char *tres_per_node;		/* semicolon delimited list of TRES=# values */
	char *tres_per_socket;		/* semicolon delimited list of TRES=# values */
	char *tres_per_task;		/* semicolon delimited list of TRES=# values */
} step_record_t;

typedef struct {
	job_record_t *job_ptr;
	List job_queue;
	part_record_t *part_ptr;
	uint32_t prio;
	slurmctld_resv_t *resv_ptr;
} job_queue_req_t;

extern List job_list;			/* list of job_record entries */
extern List purge_files_list;		/* list of job ids to purge files of */

/*****************************************************************************\
 *  Consumable Resources parameters and data structures
\*****************************************************************************/

/*
 * Define the type of update and of data retrieval that can happen
 * from the "select/cons_res" plugin. This information needed to
 * support processors as consumable resources.  This structure will be
 * useful when updating other types of consumable resources as well
*/
enum select_plugindata_info {
	SELECT_CR_PLUGIN,    /* data-> uint32 See SELECT_TYPE_* below */
	SELECT_BITMAP,       /* Unused since version 2.0 */
	SELECT_ALLOC_CPUS,   /* data-> uint16 alloc cpus (CR support) */
	SELECT_ALLOC_LPS,    /* data-> uint32 alloc lps  (CR support) */
	SELECT_AVAIL_MEMORY, /* data-> uint64 avail mem  (CR support) */
	SELECT_STATIC_PART,  /* data-> uint16, 1 if static partitioning
			      * BlueGene support */
	SELECT_CONFIG_INFO,  /* data-> List get .conf info from select
			      * plugin */
	SELECT_SINGLE_JOB_TEST	/* data-> uint16 1 if one select_g_job_test()
				 * call per job, node weights in node data
				 * structure, 0 otherwise, for cons_tres */
};
#define SELECT_TYPE_CONS_RES	1
#define SELECT_TYPE_CONS_TRES	2


/*****************************************************************************\
 *  Global assoc_cache variables
\*****************************************************************************/

/* flag to let us know if we are running on cache or from the actual
 * database */
extern uint16_t running_cache;
/* mutex and signal to let us know if associations have been reset so we need to
 * redo all the pointers to the associations */
extern pthread_mutex_t assoc_cache_mutex; /* assoc cache mutex */
extern pthread_cond_t assoc_cache_cond; /* assoc cache condition */

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
 * IN node_name - name of the node on which the job resides
 */
extern void abort_job_on_node(uint32_t job_id, job_record_t *job_ptr,
			      char *node_name);

/*
 * abort_job_on_nodes - Kill the specific job_on the specific nodes,
 *	the request is not processed immediately, but queued.
 *	This is to prevent a flood of pthreads if slurmctld restarts
 *	without saved state and slurmd daemons register with a
 *	multitude of running jobs. Slurmctld will not recognize
 *	these jobs and use this function to kill them - one
 *	agent request per node as they register.
 * IN job_ptr - pointer to terminating job
 * IN node_name - name of the node on which the job resides
 */
extern void abort_job_on_nodes(job_record_t *job_ptr, bitstr_t *node_bitmap);

/*
 * If a job has a FAIL_ACCOUNT or FAIL_QOS start_reason check and set pointers
 * if they are now valid.
 */
extern void set_job_failed_assoc_qos_ptr(job_record_t *job_ptr);

/* set the tres_req_str and tres_req_fmt_str for the job.  assoc_mgr_locked
 * is set if the assoc_mgr read lock is already set.
 */
extern void set_job_tres_req_str(job_record_t *job_ptr, bool assoc_mgr_locked);

/* set the tres_alloc_str and tres_alloc_fmt_str for the job.  assoc_mgr_locked
 * is set if the assoc_mgr read lock is already set.
 */
extern void set_job_tres_alloc_str(job_record_t *job_ptr,
				   bool assoc_mgr_locked);

/* Note that the backup slurmctld has assumed primary control.
 * This function can be called multiple times. */
extern void backup_slurmctld_restart(void);

/* Complete a batch job requeue logic after all steps complete so that
 * subsequent jobs appear in a separate accounting record. */
extern void batch_requeue_fini(job_record_t *job_ptr);

/* Build a bitmap of nodes completing this job */
extern void build_cg_bitmap(job_record_t *job_ptr);

/* Build structure with job allocation details */
extern resource_allocation_response_msg_t *build_job_info_resp(
	job_record_t *job_ptr);

/*
 * create_part_record - create a partition record
 * IN name - name will be xstrdup()'d into the part_record
 * RET a pointer to the record or NULL if error
 * global: default_part - default partition parameters
 *         part_list - global partition list
 * NOTE: the record's values are initialized to those of default_part
 * NOTE: allocates memory that should be xfreed with delete_part_record
 */
extern part_record_t *create_part_record(const char *name);

/*
 * build_part_bitmap - update the total_cpus, total_nodes, and node_bitmap
 *	for the specified partition, also reset the partition pointers in
 *	the node back to this partition.
 * IN part_ptr - pointer to the partition
 * RET 0 if no error, errno otherwise
 * global: node_record_table_ptr - pointer to global node table
 * NOTE: this does not report nodes defined in more than one partition. this
 *	is checked only upon reading the configuration file, not on an update
 */
extern int build_part_bitmap(part_record_t *part_ptr);

/*
 * job_limits_check - check the limits specified for the job.
 * IN job_ptr - pointer to job table entry.
 * IN check_min_time - if true test job's minimum time limit,
 *		otherwise test maximum time limit
 * RET WAIT_NO_REASON on success, fail status otherwise.
 */
extern int job_limits_check(job_record_t **job_pptr, bool check_min_time);

/*
 * delete_partition - delete the specified partition
 * IN job_specs - job specification from RPC
 * RET 0 on success, errno otherwise
 */
extern int delete_partition(delete_part_msg_t *part_desc_ptr);

/*
 * delete_step_record - delete record for job step for specified job_ptr
 *	and step_id
 * IN job_ptr - pointer to job table entry to have step record removed
 * IN step_ptr - pointer to step table entry of the desired job step
 */
extern void delete_step_record(job_record_t *job_ptr, step_record_t *step_ptr);

/*
 * delete_step_records - delete step record for specified job_ptr
 * IN job_ptr - pointer to job table entry to have step records removed
 */
extern void delete_step_records(job_record_t *job_ptr);

/* free_step_record - delete a step record's data structures */
extern void free_step_record(void *x);

/*
 * Copy a job's dependency list
 * IN depend_list_src - a job's depend_lst
 * RET copy of depend_list_src, must bee freed by caller
 */
extern List depended_list_copy(List depend_list_src);

/*
 * drain_nodes - drain one or more nodes,
 *  no-op for nodes already drained or draining
 * IN nodes - nodes to drain
 * IN reason - reason to drain the nodes
 * IN reason_uid - who set the reason
 * RET SLURM_SUCCESS or error code
 * global: node_record_table_ptr - pointer to global node table
 */
extern int drain_nodes ( char *nodes, char *reason, uint32_t reason_uid );

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
 * IN step_ptr - pointer to job step for which information is to be dumped
 * IN/OUT buffer - location to store data, pointers automatically advanced
 */
extern int dump_job_step_state(void *x, void *arg);

/*
 * dump_step_desc - dump the incoming step initiate request message
 * IN step_spec - job step request specification from RPC
 */
extern void dump_step_desc(job_step_create_request_msg_t *step_spec);

/* Remove one node from a job's allocation */
extern void excise_node_from_job(job_record_t *job_ptr,
				 node_record_t *node_ptr);

/* make_node_avail - flag specified node as available */
extern void make_node_avail(node_record_t *node_ptr);

/*
 * Copy a job's feature list
 * IN feature_list_src - a job's depend_lst
 * RET copy of depend_list_src, must be freed by caller
 */
extern List feature_list_copy(List feature_list_src);

/*
 * find_job_array_rec - return a pointer to the job record with the given
 *	array_job_id/array_task_id
 * IN job_id - requested job's id
 * IN array_task_id - requested job's task id,
 *		      NO_VAL if none specified (i.e. not a job array)
 *		      INFINITE return any task for specified job id
 * RET pointer to the job's record, NULL on error
 */
extern job_record_t *find_job_array_rec(uint32_t array_job_id,
					uint32_t array_task_id);

/*
 * find_het_job_record - return a pointer to the job record with the given ID
 * IN job_id - requested job's ID
 * in het_job_id - hetjob component ID
 * RET pointer to the job's record, NULL on error
 */
extern job_record_t *find_het_job_record(uint32_t job_id, uint32_t het_job_id);

/*
 * find_job_record - return a pointer to the job record with the given job_id
 * IN job_id - requested job's id
 * RET pointer to the job's record, NULL on error
 */
extern job_record_t *find_job_record(uint32_t job_id);

/*
 * find_first_node_record - find a record for first node in the bitmap
 * IN node_bitmap
 */
extern node_record_t *find_first_node_record(bitstr_t *node_bitmap);

/*
 * find_part_record - find a record for partition with specified name
 * IN name - name of the desired partition
 * RET pointer to partition or NULL if not found
 */
extern part_record_t *find_part_record(char *name);

/*
 * find_step_record - return a pointer to the step record with the given
 *	job_id and step_id
 * IN job_ptr - pointer to job table entry to have step record added
 * IN step_id - id+het_comp of the desired job step
 * RET pointer to the job step's record, NULL on error
 */
extern step_record_t *find_step_record(job_record_t *job_ptr,
				       slurm_step_id_t *step_id);

/*
 * free_null_array_recs - free an xmalloc'd job_array_struct_t structure inside
 *                        of a job_record_t and set job_ptr->array_recs to NULL.
 */
extern void free_null_array_recs(job_record_t *array_recs);

/*
 * get_job_env - return the environment variables and their count for a
 *	given job
 * IN job_ptr - pointer to job for which data is required
 * OUT env_size - number of elements to read
 * RET point to array of string pointers containing environment variables
 */
extern char **get_job_env(job_record_t *job_ptr, uint32_t *env_size);

/*
 * get_job_script - return the script for a given job
 * IN job_ptr - pointer to job for which data is required
 * RET buf_t containing job script
 */
extern buf_t *get_job_script(const job_record_t *job_ptr);

/*
 * job_get_sockets_per_node
 * IN job_ptr - pointer to the job
 * RET number of requested sockets per node if set, otherwise 1
 */
extern uint16_t job_get_sockets_per_node(job_record_t *job_ptr);

/*
 * Return the next available job_id to be used.
 * IN test_only - if true, doesn't advance the job_id sequence, just returns
 * 	what the next job id will be.
 * RET a valid job_id or SLURM_ERROR if all job_ids are exhausted.
 */
extern uint32_t get_next_job_id(bool test_only);

/*
 * get_part_list - find record for named partition(s)
 * IN name - partition name(s) in a comma separated list
 * OUT err_part - The first invalid partition name.
 * RET List of pointers to the partitions or NULL if not found
 * NOTE: Caller must free the returned list
 * NOTE: Caller must free err_part
 */
extern List get_part_list(char *name, char **err_part);

/*
 * init_depend_policy()
 * Initialize variables from DependencyParameters
 */
extern void init_depend_policy(void);

/*
 * init_job_conf - initialize the job configuration tables and values.
 *	this should be called after creating node information, but
 *	before creating any job entries.
 * global: last_job_update - time of last job table update
 *	job_list - pointer to global job list
 */
extern void init_job_conf(void);

/*
 * init_node_conf - initialize the node configuration tables and values.
 *	this should be called before creating any node or configuration
 *	entries.
 * global: node_record_table_ptr - pointer to global node table
 *         default_node_record - default values for node records
 *         default_config_record - default values for configuration records
 *         hash_table - table of hash indexes
 *         last_node_update - time of last node table update
 */
extern void init_node_conf(void);

/*
 * init_part_conf - initialize the default partition configuration values
 *	and create a (global) partition list.
 * this should be called before creating any partition entries.
 * global: default_part - default partition values
 *         part_list - global partition list
 */
extern void init_part_conf(void);

/* init_requeue_policy()
 * Initialize the requeue exit/hold bitmaps.
 */
extern void init_requeue_policy(void);

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

/* Fail a job because the qos is no longer valid */
extern int job_fail_qos(job_record_t *job_ptr, const char *func_name);

/*
 * delete_job_desc_files - remove the state files and directory
 * for a given job_id from SlurmStateSaveLocation
 */
extern void delete_job_desc_files(uint32_t job_id);

/*
 * job_alloc_info - get details about an existing job allocation
 * IN uid - job issuing the code
 * IN job_id - ID of job for which info is requested
 * OUT job_pptr - set to pointer to job record
 * NOTE: See job_alloc_info_ptr() if job pointer is known
 */
extern int job_alloc_info(uint32_t uid, uint32_t job_id,
			  job_record_t **job_pptr);

/*
 * job_alloc_info_ptr - get details about an existing job allocation
 * IN uid - job issuing the code
 * IN job_ptr - pointer to job record
 * NOTE: See job_alloc_info() if job pointer not known
 */
extern int job_alloc_info_ptr(uint32_t uid, job_record_t *job_ptr);

/*
 * job_allocate - create job_records for the supplied job specification and
 *	allocate nodes for it.
 * IN job_specs - job specifications
 * IN immediate - if set then either initiate the job immediately or fail
 * IN will_run - don't initiate the job if set, just test if it could run
 *	now or later
 * OUT resp - will run response (includes start location, time, etc.)
 * IN allocate - resource allocation request only if set, batch job if zero
 * IN submit_uid -uid of user issuing the request
 * IN cron - true if cron
 * OUT job_pptr - set to pointer to job record
 * OUT err_msg - Custom error message to the user, caller to xfree results
 * IN protocol_version - version of the code the caller is using
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
			int allocate, uid_t submit_uid, bool cron,
			job_record_t **job_pptr,
			char **err_msg, uint16_t protocol_version);

/* If this is a job array meta-job, prepare it for being scheduled */
extern void job_array_pre_sched(job_record_t *job_ptr);

/* If this is a job array meta-job, clean up after scheduling attempt */
extern job_record_t *job_array_post_sched(job_record_t *job_ptr);

/* Create an exact copy of an existing job record for a job array.
 * IN job_ptr - META job record for a job array, which is to become an
 *		individial task of the job array.
 *		Set the job's array_task_id to the task to be split out.
 * RET - The new job record, which is the new META job record. */
extern job_record_t *job_array_split(job_record_t *job_ptr);

/* Record the start of one job array task */
extern void job_array_start(job_record_t *job_ptr);

/* Return true if a job array task can be started */
extern bool job_array_start_test(job_record_t *job_ptr);

/* Clear job's CONFIGURING flag and advance end time as needed */
extern void job_config_fini(job_record_t *job_ptr);

/* Reset a job's end_time based upon it's start_time and time_limit.
 * NOTE: Do not reset the end_time if already being preempted */
extern void job_end_time_reset(job_record_t *job_ptr);
/*
 * job_hold_by_assoc_id - Hold all pending jobs with a given
 *	association ID. This happens when an association is deleted (e.g. when
 *	a user is removed from the association database).
 * RET count of held jobs
 */
extern int job_hold_by_assoc_id(uint32_t assoc_id);

/*
 * job_hold_by_qos_id - Hold all pending jobs with a given
 *	QOS ID. This happens when a QOS is deleted (e.g. when
 *	a QOS is removed from the association database).
 * RET count of held jobs
 */
extern int job_hold_by_qos_id(uint32_t qos_id);

/* log the completion of the specified job */
extern void job_completion_logger(job_record_t *job_ptr, bool requeue);

/*
 * Return total amount of memory allocated to a job. This can be based upon
 * a GRES specification with various GRES/memory allocations on each node.
 * If current allocation information is not available, estimate memory based
 * upon pn_min_memory and either CPU or node count, or upon mem_per_gres
 * and estimated gres count (both values gotten from gres list).
 */
extern uint64_t job_get_tres_mem(struct job_resources *job_res,
				 uint64_t pn_min_memory, uint32_t cpu_cnt,
				 uint32_t node_cnt, part_record_t *part_ptr,
				 List gres_list, bool user_set_mem,
				 uint16_t min_sockets_per_node,
				 uint32_t num_tasks);

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
 * RET SLURM_SUCCESS or an error code
 */
extern int job_end_time(job_alloc_info_msg_t *time_req_msg,
			srun_timeout_msg_t *timeout_msg);

/* job_fini - free all memory associated with job records */
extern void job_fini (void);

/* job_hold_requeue()
 *
 * Requeue the job based upon its current state.
 * If JOB_SPECIAL_EXIT then requeue and hold with JOB_SPECIAL_EXIT state.
 * If JOB_REQUEUE_HOLD then requeue and hold.
 * If JOB_REQUEUE then requeue and let it run again.
 * The requeue can happen directly from job_requeue() or from
 * job_epilog_complete() after the last component has finished.
 */
extern bool job_hold_requeue(job_record_t *job_ptr);

/*
 * determine if job is ready to execute per the node select plugin
 * IN job_id - job to test
 * OUT ready - 1 if job is ready to execute 0 otherwise
 * RET Slurm error code
 */
extern int job_node_ready(uint32_t job_id, int *ready);

/* Record accounting information for a job immediately before changing size */
extern void job_pre_resize_acctg(job_record_t *job_ptr);

/* Record accounting information for a job immediately after changing size */
extern void job_post_resize_acctg(job_record_t *job_ptr);

/*
 * job_signal - signal the specified job, access checks already done
 * IN job_ptr - job to be signaled
 * IN signal - signal to send, SIGKILL == cancel the job
 * IN flags  - see KILL_JOB_* flags in slurm.h
 * IN uid - uid of requesting user
 * IN preempt - true if job being preempted
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_signal(job_record_t *job_ptr, uint16_t signal,
		      uint16_t flags, uid_t uid, bool preempt);

/*
 * job_signal_id - signal the specified job
 * IN job_id - id of the job to be signaled
 * IN signal - signal to send, SIGKILL == cancel the job
 * IN flags  - see KILL_JOB_* flags in slurm.h
 * IN uid - uid of requesting user
 * IN preempt - true if job being preempted
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_signal_id(uint32_t job_id, uint16_t signal, uint16_t flags,
			 uid_t uid, bool preempt);
/*
 * het_job_signal - signal all components of a hetjob
 * IN het_job_leader - job record of job hetjob leader
 * IN signal - signal to send, SIGKILL == cancel the job
 * IN flags  - see KILL_JOB_* flags in slurm.h
 * IN uid - uid of requesting user
 * IN preempt - true if job being preempted
 * RET 0 on success, otherwise ESLURM error code
 */
extern int het_job_signal(job_record_t *het_job_leader, uint16_t signal,
			   uint16_t flags, uid_t uid, bool preempt);

/*
 * job_str_signal - signal the specified job
 * IN job_id_str - id of the job to be signaled, valid formats include "#"
 *	"#_#" and "#_[expr]"
 * IN signal - signal to send, SIGKILL == cancel the job
 * IN flags  - see KILL_JOB_* flags in slurm.h
 * IN uid - uid of requesting user
 * IN preempt - true if job being preempted
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_str_signal(char *job_id_str, uint16_t signal, uint16_t flags,
			  uid_t uid, bool preempt);

/*
 * job_suspend/job_suspend2 - perform some suspend/resume operation
 * NB job_suspend  - Uses the job_id field and ignores job_id_str
 * NB job_suspend2 - Ignores the job_id field and uses job_id_str
 *
 * IN sus_ptr - suspend/resume request message
 * IN uid - user id of the user issuing the RPC
 * IN conn_fd - file descriptor on which to send reply,
 *              -1 if none
 * indf_susp IN - set if job is being suspended indefinitely by user or admin
 *                and we should clear it's priority, otherwise suspended
 *		  temporarily for gang scheduling
 * IN protocol_version - slurm protocol version of client
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_suspend(suspend_msg_t *sus_ptr, uid_t uid,
		       int conn_fd, bool indf_susp,
		       uint16_t protocol_version);
extern int job_suspend2(suspend_msg_t *sus_ptr, uid_t uid,
			int conn_fd, bool indf_susp,
			uint16_t protocol_version);

/*
 * job_complete - note the normal termination the specified job
 * IN job_id - id of the job which completed
 * IN uid - user id of user issuing the RPC
 * IN requeue - job should be run again if possible
 * IN node_fail - true if job terminated due to node failure
 * IN job_return_code - job's return code, if set then set state to JOB_FAILED
 * RET - 0 on success, otherwise ESLURM error code
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
extern int job_complete(uint32_t job_id, uid_t uid, bool requeue,
			bool node_fail, uint32_t job_return_code);

/*
 * job_independent - determine if this job has a dependent job pending
 *	or if the job's scheduled begin time is in the future
 * IN job_ptr - pointer to job being tested
 * RET - true if job no longer must be defered for another job
 */
extern bool job_independent(job_record_t *job_ptr);

/*
 * job_req_node_filter - job reqeust node filter.
 *	clear from a bitmap the nodes which can not be used for a job
 *	test memory size, required features, processor count, etc.
 * NOTE: Does not support exclusive OR of features.
 *	It just matches first element of XOR and ignores count.
 * IN job_ptr - pointer to node to be scheduled
 * IN/OUT bitmap - set of nodes being considered for use
 * RET SLURM_SUCCESS or EINVAL if can't filter (exclusive OR of features)
 */
extern int job_req_node_filter(job_record_t *job_ptr, bitstr_t *avail_bitmap,
			       bool test_only);

/*
 * job_requeue - Requeue a running or pending batch job
 * IN uid - user id of user issuing the RPC
 * IN job_id - id of the job to be requeued
 * IN msg - slurm_msg to send response back on
 * IN preempt - true if job being preempted
 * IN flags - JobExitRequeue | Hold | JobFailed | etc.
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_requeue(uid_t uid, uint32_t job_id, slurm_msg_t *msg,
		       bool preempt, uint32_t flags);

/*
 * job_requeue2 - Requeue a running or pending batch job
 * IN uid - user id of user issuing the RPC
 * IN req_ptr - request including ID of the job to be requeued
 * IN msg - slurm_msg to send response back on
 * IN preempt - true if job being preempted
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_requeue2(uid_t uid, requeue_msg_t *req_ptr, slurm_msg_t *msg,
			bool preempt);

/*
 * job_set_top - Move the specified job to the top of the queue (at least
 *	for that user ID, partition, account, and QOS).
 *
 * IN top_ptr - user request
 * IN uid - user id of the user issuing the RPC
 * IN conn_fd - file descriptor on which to send reply,
 *              -1 if none
 * IN protocol_version - slurm protocol version of client
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_set_top(top_job_msg_t *top_ptr, uid_t uid, int conn_fd,
		       uint16_t protocol_version);

/*
 * job_step_signal - signal the specified job step
 * IN step_id - filled in slurm_step_id_t
 * IN signal - user id of user issuing the RPC
 * IN flags - RPC flags
 * IN uid - user id of user issuing the RPC
 * RET 0 on success, otherwise ESLURM error code
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
extern int job_step_signal(slurm_step_id_t *step_id,
			   uint16_t signal, uint16_t flags, uid_t uid);

/*
 * job_time_limit - terminate jobs which have exceeded their time limit
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
extern void job_time_limit (void);

/* Builds the tres_req_cnt and tres_req_str of a job.
 * Only set when job is pending.
 * NOTE: job write lock must be locked before calling this */
extern void job_set_req_tres(job_record_t *job_ptr, bool assoc_mgr_locked);

/*
 * job_set_tres - set the tres up when allocating the job.
 * Only set when job is running.
 * NOTE: job write lock must be locked before calling this */
extern void job_set_alloc_tres(job_record_t *job_ptr, bool assoc_mgr_locked);

/*
 * job_update_tres_cnt - when job is completing remove allocated tres
 *                      from count.
 * IN/OUT job_ptr - job structure to be updated
 * IN node_inx    - node bit that is finished with job.
 * RET SLURM_SUCCES on success SLURM_ERROR on cpu_cnt underflow
 */
extern int job_update_tres_cnt(job_record_t *job_ptr, int node_inx);

/*
 * check_job_step_time_limit - terminate jobsteps which have exceeded
 * their time limit
 */
extern int check_job_step_time_limit(void *x, void *arg);

/*
 * Kill job or job step
 *
 * IN job_step_kill_msg - msg with specs on which job/step to cancel.
 * IN uid               - uid of user requesting job/step cancel.
 */
extern int kill_job_step(job_step_kill_msg_t *job_step_kill_msg, uint32_t uid);

/*
 * kill_job_by_part_name - Given a partition name, deallocate resource for
 *	its jobs and kill them
 * IN part_name - name of a partition
 * RET number of killed jobs
 */
extern int kill_job_by_part_name(char *part_name);

/*
 * kill_job_on_node - Kill the specific job on a specific node.
 * IN job_ptr - pointer to terminating job
 * IN node_ptr - pointer to the node on which the job resides
 */
extern void kill_job_on_node(job_record_t *job_ptr, node_record_t *node_ptr);

/*
 * kill_job_by_front_end_name - Given a front end node name, deallocate
 *	resource for its jobs and kill them.
 * IN node_name - name of a front end node
 * RET number of jobs associated with this front end node
 */
extern int kill_job_by_front_end_name(char *node_name);

/*
 * kill_running_job_by_node_name - Given a node name, deallocate RUNNING
 *	or COMPLETING jobs from the node or kill them
 * IN node_name - name of a node
 * RET number of killed jobs
 */
extern int kill_running_job_by_node_name(char *node_name);

/*
 * kill_step_on_node - determine if the specified job has any job steps
 *	allocated to the specified node and kill them unless no_kill flag
 *	is set on the step
 * IN job_ptr - pointer to an active job record
 * IN node_ptr - pointer to a node record
 * IN node_fail - true of removed node has failed
 */
extern void kill_step_on_node(job_record_t *job_ptr, node_record_t *node_ptr,
			      bool node_fail);

/* list_compare_config - compare two entry from the config list based upon
 *	weight, see common/list.h for documentation */
int list_compare_config (void *config_entry1, void *config_entry2);

/*
 * list_find_feature - find an entry in the feature list, see list.h for
 *	documentation
 * IN key - is feature name or NULL for all features
 * RET 1 if found, 0 otherwise
 */
extern int list_find_feature(void *feature_entry, void *key);

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
 * IN state_only - if true over-write only node state, features, gres and reason
 * RET 0 or error code
 */
extern int load_all_node_state ( bool state_only );

/*
 * load_last_job_id - load only the last job ID from state save file.
 * RET 0 or error code
 */
extern int load_last_job_id( void );

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
 * Create a new job step from data in a buffer (as created by
 * dump_job_stepstate)
 * IN/OUT - job_ptr - point to a job for which the step is to be loaded.
 * IN/OUT buffer - location from which to get data, pointers
 *                 automatically advanced
 */
extern int load_step_state(job_record_t *job_ptr, buf_t *buffer,
			   uint16_t protocol_version);

/*
 * Log contents of avail_feature_list and active_feature_list
 */
extern void log_feature_lists(void);

/* make_node_alloc - flag specified node as allocated to a job
 * IN node_ptr - pointer to node being allocated
 * IN job_ptr  - pointer to job that is starting
 */
extern void make_node_alloc(node_record_t *node_ptr, job_record_t *job_ptr);

/* make_node_comp - flag specified node as completing a job
 * IN node_ptr - pointer to node marked for completion of job
 * IN job_ptr  - pointer to job that is completing
 * IN suspended - true if job was previously suspended
 */
extern void make_node_comp(node_record_t *node_ptr, job_record_t *job_ptr,
			   bool suspended);

/*
 * make_node_idle - flag specified node as having finished with a job
 * IN node_ptr - pointer to node reporting job completion
 * IN job_ptr - pointer to job that just completed or NULL if not applicable
 */
extern void make_node_idle(node_record_t *node_ptr, job_record_t *job_ptr);

/*
 * Determine of the specified job can execute right now or is currently
 * blocked by a partition state or limit. These job states should match the
 * reason values returned by job_limits_check().
 */
extern bool misc_policy_job_runnable_state(job_record_t *job_ptr);

/* msg_to_slurmd - send given msg_type every slurmd, no args */
extern void msg_to_slurmd (slurm_msg_type_t msg_type);

/* request a "configless" RPC be send to all slurmd nodes */
void push_reconfig_to_slurmd(char **slurmd_config_files);

/* node_fini - free all memory associated with node records */
extern void node_fini (void);

/* node_did_resp - record that the specified node is responding
 * IN name - name of the node */
extern void node_did_resp (char *name);

/*
 * node_not_resp - record that the specified node is not responding
 * IN name - name of the node
 * IN msg_time - time message was sent
 * IN resp_type - what kind of response came back from the node
 */
extern void node_not_resp (char *name, time_t msg_time,
			   slurm_msg_type_t resp_type);

/* For every node with the "not_responding" flag set, clear the flag
 * and log that the node is not responding using a hostlist expression */
extern void node_no_resp_msg(void);

/* For a given job ID return the number of PENDING tasks which have their
 * own separate job_record (do not count tasks in pending META job record) */
extern int num_pending_job_array_tasks(uint32_t array_job_id);

/*
 * pack_all_jobs - dump all job information for all jobs in
 *	machine independent form (for network transmission)
 * OUT buffer_ptr - the pointer is set to the allocated buffer.
 * OUT buffer_size - set to size of the buffer in bytes
 * IN show_flags - job filtering options
 * IN uid - uid of user making request (for partition filtering)
 * IN filter_uid - pack only jobs belonging to this user if not NO_VAL
 * IN protocol_version - slurm protocol version of client
 * global: job_list - global list of job records
 * NOTE: the buffer at *buffer_ptr must be xfreed by the caller
 * NOTE: change _unpack_job_desc_msg() in common/slurm_protocol_pack.c
 *	whenever the data format changes
 */
extern void pack_all_jobs(char **buffer_ptr, int *buffer_size,
			  uint16_t show_flags, uid_t uid, uint32_t filter_uid,
			  uint16_t protocol_version);

/*
 * pack_spec_jobs - dump job information for specified jobs in
 *	machine independent form (for network transmission)
 * OUT buffer_ptr - the pointer is set to the allocated buffer.
 * OUT buffer_size - set to size of the buffer in bytes
 * IN show_flags - job filtering options
 * IN job_ids - list of job_ids to pack
 * IN uid - uid of user making request (for partition filtering)
 * IN filter_uid - pack only jobs belonging to this user if not NO_VAL
 * global: job_list - global list of job records
 * NOTE: the buffer at *buffer_ptr must be xfreed by the caller
 * NOTE: change _unpack_job_desc_msg() in common/slurm_protocol_pack.c
 *	whenever the data format changes
 */
extern void pack_spec_jobs(char **buffer_ptr, int *buffer_size, List job_ids,
			   uint16_t show_flags, uid_t uid, uint32_t filter_uid,
			   uint16_t protocol_version);

/*
 * pack_all_node - dump all configuration and node information for all nodes
 *	in machine independent form (for network transmission)
 * OUT buffer_ptr - pointer to the stored data
 * OUT buffer_size - set to size of the buffer in bytes
 * IN show_flags - node filtering options
 * IN uid - uid of user making request (for partition filtering)
 * IN protocol_version - slurm protocol version of client
 * global: node_record_table_ptr - pointer to global node table
 * NOTE: the caller must xfree the buffer at *buffer_ptr
 * NOTE: change slurm_load_node() in api/node_info.c when data format changes
 * NOTE: READ lock_slurmctld config before entry
 */
extern void pack_all_node (char **buffer_ptr, int *buffer_size,
			   uint16_t show_flags, uid_t uid,
			   uint16_t protocol_version);

/* Pack all scheduling statistics */
extern void pack_all_stat(int resp, char **buffer_ptr, int *buffer_size,
			  uint16_t protocol_version);

/*
 * pack_ctld_job_step_info_response_msg - packs job step info
 * IN step_id - specific id or NO_VAL/NO_VAL for all
 * IN uid - user issuing request
 * IN show_flags - job step filtering options
 * OUT buffer - location to store data, pointers automatically advanced
 * IN protocol_version - slurm protocol version of client
 * RET - 0 or error code
 * NOTE: MUST free_buf buffer
 */
extern int pack_ctld_job_step_info_response_msg(
	slurm_step_id_t *step_id, uid_t uid, uint16_t show_flags,
	buf_t *buffer, uint16_t protocol_version);

/*
 * pack_all_part - dump all partition information for all partitions in
 *	machine independent form (for network transmission)
 * OUT buffer_ptr - the pointer is set to the allocated buffer.
 * OUT buffer_size - set to size of the buffer in bytes
 * IN show_flags - partition filtering options
 * IN uid - uid of user making request (for partition filtering)
 * IN protocol_version - slurm protocol version of client
 * global: part_list - global list of partition records
 * NOTE: the buffer at *buffer_ptr must be xfreed by the caller
 * NOTE: change slurm_load_part() in api/part_info.c if data format changes
 */
extern void pack_all_part(char **buffer_ptr, int *buffer_size,
			  uint16_t show_flags, uid_t uid,
			  uint16_t protocol_version);

/*
 * pack_job - dump all configuration information about a specific job in
 *	machine independent form (for network transmission)
 * IN dump_job_ptr - pointer to job for which information is requested
 * IN show_flags - job filtering options
 * IN/OUT buffer - buffer in which data is placed, pointers automatically
 *	updated
 * IN uid - user requesting the data
 * IN has_qos_lock - true if assoc_lock .qos=READ_LOCK already acquired
 * NOTE: change _unpack_job_desc_msg() in common/slurm_protocol_pack.c
 *	  whenever the data format changes
 */
extern void pack_job(job_record_t *dump_job_ptr, uint16_t show_flags,
		     buf_t *buffer, uint16_t protocol_version, uid_t uid,
		     bool has_qos_lock);

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
extern void pack_part(part_record_t *part_ptr, buf_t *buffer,
		      uint16_t protocol_version);

/*
 * pack_one_job - dump information for one jobs in
 *	machine independent form (for network transmission)
 * OUT buffer_ptr - the pointer is set to the allocated buffer.
 * OUT buffer_size - set to size of the buffer in bytes
 * IN job_id - ID of job that we want info for
 * IN show_flags - job filtering options
 * IN uid - uid of user making request (for partition filtering)
 * NOTE: the buffer at *buffer_ptr must be xfreed by the caller
 * NOTE: change _unpack_job_desc_msg() in common/slurm_protocol_pack.c
 *	whenever the data format changes
 */
extern int pack_one_job(char **buffer_ptr, int *buffer_size,
			uint32_t job_id, uint16_t show_flags, uid_t uid,
			uint16_t protocol_version);

/*
 * pack_one_node - dump all configuration and node information for one node
 *	in machine independent form (for network transmission)
 * OUT buffer_ptr - pointer to the stored data
 * OUT buffer_size - set to size of the buffer in bytes
 * IN show_flags - node filtering options
 * IN uid - uid of user making request (for partition filtering)
 * IN node_name - name of node for which information is desired,
 *		  use first node if name is NULL
 * IN protocol_version - slurm protocol version of client
 * global: node_record_table_ptr - pointer to global node table
 * NOTE: the caller must xfree the buffer at *buffer_ptr
 * NOTE: change slurm_load_node() in api/node_info.c when data format changes
 * NOTE: READ lock_slurmctld config before entry
 */
extern void pack_one_node (char **buffer_ptr, int *buffer_size,
			   uint16_t show_flags, uid_t uid, char *node_name,
			   uint16_t protocol_version);

/* part_is_visible - should user be able to see this partition */
extern bool part_is_visible(part_record_t *part_ptr, uid_t uid);

/* part_is_visible_user_rec - should user be able to see this partition */
extern bool part_is_visible_user_rec(part_record_t *part_ptr,
				     slurmdb_user_rec_t *user);

/* part_fini - free all memory associated with partition records */
extern void part_fini (void);

/*
 * Create a copy of a job's part_list *partition list
 * IN part_list_src - a job's part_list
 * RET copy of part_list_src, must be freed by caller
 */
extern List part_list_copy(List part_list_src);

/*
 * Determine of the specified job can execute right now or is currently
 * blocked by a partition state or limit. Execute job_limits_check() to
 * re-validate job state.
 */
extern bool part_policy_job_runnable_state(job_record_t *job_ptr);

/*
 * Validate a job's account against the partition's AllowAccounts or
 *	DenyAccounts parameters.
 * IN part_ptr - Partition pointer
 * IN acct - account name
 * in job_ptr - Job pointer or NULL. If set and job can not run, then set the
 *		job's state_desc and state_reason fields
 * RET SLURM_SUCCESS or error code
 */
extern int part_policy_valid_acct(part_record_t *part_ptr, char *acct,
				  job_record_t *job_ptr);

/*
 * Validate a job's QOS against the partition's AllowQOS or DenyQOS parameters.
 * IN part_ptr - Partition pointer
 * IN qos_ptr - QOS pointer
 * in job_ptr - Job pointer or NULL. If set and job can not run, then set the
 *		job's state_desc and state_reason fields
 * RET SLURM_SUCCESS or error code
 */
extern int part_policy_valid_qos(part_record_t *part_ptr,
				 slurmdb_qos_rec_t *qos_ptr,
				 job_record_t *job_ptr);

/*
 * partition_in_use - determine whether a partition is in use by a RUNNING
 *	PENDING or SUSPENDED job
 * IN part_name - name of a partition
 * RET true if the partition is in use, else false
 */
extern bool partition_in_use(char *part_name);

/*
 * Set "batch_host" for this job based upon it's "batch_features" and
 * "node_bitmap". The selection is deferred in case a node's "active_features"
 * is changed by a reboot.
 * Return SLURM_SUCCESS or error code
 */
extern int pick_batch_host(job_record_t *job_ptr);

/*
 * prolog_complete - note the normal termination of the prolog
 * IN job_id - id of the job which completed
 * IN prolog_return_code - prolog's return code,
 *    if set then set job state to FAILED
 * RET - 0 on success, otherwise ESLURM error code
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
extern int prolog_complete(uint32_t job_id, uint32_t prolog_return_code);

/*
 * If the job or slurm.conf requests to not kill on invalid dependency,
 * then set the job state reason to WAIT_DEP_INVALID. Otherwise, kill the
 * job.
 */
extern void handle_invalid_dependency(job_record_t *job_ptr);

/*
 * purge_old_job - purge old job records.
 *	The jobs must have completed at least MIN_JOB_AGE minutes ago.
 *	Test job dependencies, handle after_ok, after_not_ok before
 *	purging any jobs.
 * NOTE: READ lock slurmctld config and WRITE lock jobs before entry
 */
void purge_old_job(void);

/* Convert a comma delimited list of QOS names into a bitmap */
extern void qos_list_build(char *qos, bitstr_t **qos_bits);

/* Request that the job scheduler execute soon (typically within seconds) */
extern void queue_job_scheduler(void);

/*
 * rehash_jobs - Create or rebuild the job hash table.
 * NOTE: run lock_slurmctld before entry: Read config, write job
 */
extern void rehash_jobs(void);

/*
 * Rebuild a job step's core_bitmap_job after a job has just changed size
 * job_ptr IN - job that was just re-sized
 * orig_job_node_bitmap IN - The job's original node bitmap
 */
extern void rebuild_step_bitmaps(job_record_t *job_ptr,
				 bitstr_t *orig_job_node_bitmap);

/*
 * Create the extern step and add it to the job.
 */
extern step_record_t *build_extern_step(job_record_t *job_ptr);

/*
 * Create the batch step and add it to the job.
 */
extern step_record_t *build_batch_step(job_record_t *job_ptr_in);

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

/* Reset a node's CPU load value */
extern void reset_node_load(char *node_name, uint32_t cpu_load);

/* Reset a node's free memory value */
extern void reset_node_free_mem(char *node_name, uint64_t free_mem);

/* Reset all scheduling statistics
 * level IN - clear backfilled_jobs count if set */
extern void reset_stats(int level);

/*
 * restore_node_features - Make node and config (from slurm.conf) fields
 *	consistent for Features, Gres and Weight
 * IN recover -
 *              0, 1 - use data from config record, built using slurm.conf
 *              2 = use data from node record, built from saved state
 */
extern void restore_node_features(int recover);

/* Update time stamps for job step resume */
extern void resume_job_step(job_record_t *job_ptr);

/* run_backup - this is the backup controller, it should run in standby
 *	mode, assuming control when the primary controller stops responding */
extern void run_backup(void);

/*
 * ping_controllers - ping other controllers in HA configuration.
 * IN active_controller - true if active controller, false if backup
 */
extern int ping_controllers(bool active_controller);

/* Spawn health check function for every node that is not DOWN */
extern void run_health_check(void);

/* save_all_state - save entire slurmctld state for later recovery */
extern void save_all_state(void);

/* make sure the assoc_mgr lists are up and running and state is
 * restored */
extern void ctld_assoc_mgr_init(void);

/* send all info for the controller to accounting */
extern void send_all_to_accounting(time_t event_time, int db_rc);

/* A slurmctld lock needs to at least have a node read lock set before
 * this is called */
extern void set_cluster_tres(bool assoc_mgr_locked);

/* sends all jobs in eligible state to accounting.  Only needed at
 * first registration
 */
extern int send_jobs_to_accounting(void);

/* send all nodes in a down like state to accounting.  Only needed at
 * first registration
 */
extern int send_nodes_to_accounting(time_t event_time);

/* Decrement slurmctld thread count (as applies to thread limit) */
extern void server_thread_decr(void);

/* Increment slurmctld thread count (as applies to thread limit) */
extern void server_thread_incr(void);

/* Set a job's alias_list string */
extern void set_job_alias_list(job_record_t *job_ptr);

/*
 * set_job_prio - set a default job priority
 * IN job_ptr - pointer to the job_record
 */
extern void set_job_prio(job_record_t *job_ptr);

/*
 * set_node_down - make the specified node's state DOWN if possible
 *	(not in a DRAIN state), kill jobs as needed
 * IN name - name of the node
 * IN reason - why the node is DOWN
 */
extern void set_node_down (char *name, char *reason);

/*
 * set_node_down_ptr - make the specified compute node's state DOWN and
 *	kill jobs as needed
 * IN node_ptr - node_ptr to the node
 * IN reason - why the node is DOWN
 */
void set_node_down_ptr(node_record_t *node_ptr, char *reason);

/*
 * set_slurmctld_state_loc - create state directory as needed and "cd" to it
 */
extern void set_slurmctld_state_loc(void);

/*
 * signal_step_tasks - send specific signal to specific job step
 * IN step_ptr - step record pointer
 * IN signal - signal to send
 * IN msg_type - message type to send
 */
void signal_step_tasks(step_record_t *step_ptr, uint16_t signal,
		       slurm_msg_type_t msg_type);

/*
 * signal_step_tasks_on_node - send specific signal to specific job step
 *                             on a specific node.
 * IN node_name - name of node on which to signal tasks
 * IN step_ptr - step record pointer
 * IN signal - signal to send
 * IN msg_type - message type to send
 */
void signal_step_tasks_on_node(char* node_name, step_record_t *step_ptr,
			       uint16_t signal, slurm_msg_type_t msg_type);

/*
 * slurmctld_shutdown - wake up slurm_rpc_mgr thread via signal
 * RET 0 or error code
 */
extern int slurmctld_shutdown(void);

/*
 * step_create - creates a step_record in step_specs->job_id, sets up the
 *	according to the step_specs.
 * IN step_specs - job step specifications
 * OUT new_step_record - pointer to the new step_record (NULL on error)
 * IN protocol_version - slurm protocol version of client
 * OUT err_msg - Custom error message to the user, caller to xfree results
  * RET - 0 or error code
 * NOTE: don't free the returned step_record because that is managed through
 * 	the job.
 */
extern int step_create(job_step_create_request_msg_t *step_specs,
		       step_record_t **new_step_record,
		       uint16_t protocol_version, char **err_msg);

/*
 * step_layout_create - creates a step_layout according to the inputs.
 * IN step_ptr - step having tasks layed out
 * IN step_node_list - node list of hosts in step
 * IN node_count - count of nodes in step allocation
 * IN num_tasks - number of tasks in step
 * IN cpus_per_task - number of cpus per task
 * IN task_dist - type of task distribution
 * IN plane_size - size of plane (only needed for the plane distribution)
 * RET - NULL or slurm_step_layout_t *
 * NOTE: you need to free the returned step_layout usually when the
 *       step is freed.
 */
extern slurm_step_layout_t *step_layout_create(step_record_t *step_ptr,
					       char *step_node_list,
					       uint32_t node_count,
					       uint32_t num_tasks,
					       uint16_t cpus_per_task,
					       uint32_t task_dist,
					       uint16_t plane_size);

/*
 * step_partial_comp - Note the completion of a job step on at least
 *	some of its nodes
 * IN req     - step_completion_msg RPC from slurmstepd
 * IN uid     - UID issuing the request
 * IN finish  - If true, no error, and no rem is 0 finish the step.
 * OUT rem    - count of nodes for which responses are still pending
 * OUT max_rc - highest return code for any step thus far
 * RET 0 on success, otherwise ESLURM error code
 */
extern int step_partial_comp(step_complete_msg_t *req, uid_t uid, bool finish,
			     int *rem, uint32_t *max_rc);

/*
 * step_set_alloc_tres - set the tres up when allocating the step.
 * Only set when job is running.
 * NOTE: job write lock must be locked before calling this */
extern void step_set_alloc_tres(step_record_t *step_ptr, uint32_t node_count,
				bool assoc_mgr_locked, bool make_formatted);

/* Update time stamps for job step suspend */
extern void suspend_job_step(job_record_t *job_ptr);

/* For the job array data structure, build the string representation of the
 * bitmap.
 * NOTE: bit_fmt_hexmask() is far more scalable than bit_fmt(). */
extern void build_array_str(job_record_t *job_ptr);

/* Return true if ALL tasks of specific array job ID are complete */
extern bool test_job_array_complete(uint32_t array_job_id);

/* Return true if ALL tasks of specific array job ID are completed */
extern bool test_job_array_completed(uint32_t array_job_id);

/* Return true if ALL tasks of specific array job ID are finished */
extern bool test_job_array_finished(uint32_t array_job_id);

/* Return true if ANY tasks of specific array job ID are pending */
extern bool test_job_array_pending(uint32_t array_job_id);

/* Determine of the nodes are ready to run a job
 * RET true if ready */
extern bool test_job_nodes_ready(job_record_t *job_ptr);

/*
 * Synchronize the batch job in the system with their files.
 * All pending batch jobs must have script and environment files
 * No other jobs should have such files
 */
extern int sync_job_files(void);

/* After recovering job state, if using priority/basic then we increment the
 * priorities of all jobs to avoid decrementing the base down to zero */
extern void sync_job_priorities(void);

/* True if running jobs are allowed to expand, false otherwise. */
extern bool permit_job_expansion(void);

/* True if running jobs are allowed to shrink, false otherwise. */
extern bool permit_job_shrink(void);

/*
 * update_job - update a job's parameters per the supplied specifications
 * IN msg - RPC to update job, including change specification
 * IN uid - uid of user issuing RPC
 * IN send_msg - whether to send msg back or not
 * RET returns an error code from slurm_errno.h
 * global: job_list - global list of job entries
 *	last_job_update - time of last job table update
 */
extern int update_job(slurm_msg_t *msg, uid_t uid, bool send_msg);

/*
 * IN msg - RPC to update job, including change specification
 * IN job_specs - a job's specification
 * IN uid - uid of user issuing RPC
 * RET returns an error code from slurm_errno.h
 * global: job_list - global list of job entries
 *	last_job_update - time of last job table update
 */
extern int update_job_str(slurm_msg_t *msg, uid_t uid);

/*
 * Modify the wckey associated with a pending job
 * IN module - where this is called from
 * IN job_ptr - pointer to job which should be modified
 * IN new_wckey - desired wckey name
 * RET SLURM_SUCCESS or error code
 */
extern int update_job_wckey(char *module, job_record_t *job_ptr,
			    char *new_wckey);

/* Reset nodes_completing field for all jobs */
extern void update_job_nodes_completing(void);

/*
 * Update log levels given requested levels
 * IN req_slurmctld_debug - requested debug level
 * IN req_syslog_debug - requested syslog level
 * NOTE: Will not turn on originally configured off (quiet) channels
 */
extern void update_log_levels(int req_slurmctld_debug, int req_syslog_debug);

/* Reset slurmctld logging based upon configuration parameters
 * uses common slurm_conf data structure */
extern void update_logging(void);

/*
 * update_node - update the configuration data for one or more nodes
 * IN update_node_msg - update node request
 * RET 0 or error code
 * global: node_record_table_ptr - pointer to global node table
 */
extern int update_node ( update_node_msg_t * update_node_msg )  ;

/* Update nodes accounting usage data */
extern void update_nodes_acct_gather_data(void);

/*
 * update_node_record_acct_gather_data - update the energy data in the
 * node_record
 * IN msg - node energy data message
 * RET 0 if no error, ENOENT if no such node
 */
extern int update_node_record_acct_gather_data(
	acct_gather_node_resp_msg_t *msg);

/*
 * Process string and set partition fields to appropriate values if valid
 *
 * IN billing_weights_str - suggested billing weights
 * IN part_ptr - pointer to partition
 * IN fail - whether the inner function should fatal if the string is invalid.
 * RET return SLURM_ERROR on error, SLURM_SUCCESS otherwise.
 */
extern int set_partition_billing_weights(char *billing_weights_str,
					 part_record_t *part_ptr, bool fail);

/*
 * update_part - create or update a partition's configuration data
 * IN part_desc - description of partition changes
 * IN create_flag - create a new partition
 * RET 0 or an error code
 * global: part_list - list of partition entries
 *	last_part_update - update time of partition records
 */
extern int update_part (update_part_msg_t * part_desc, bool create_flag);

/* Process job step update request from specified user,
 * RET - 0 or error code */
extern int update_step(step_update_request_msg_t *req, uid_t uid);

/*
 * validate_alloc_node - validate that the allocating node
 * is allowed to use this partition
 * IN part_ptr - pointer to a partition
 * IN alloc_node - allocting node of the request
 * RET 1 if permitted to run, 0 otherwise
 */
extern int validate_alloc_node(part_record_t *part_ptr, char *alloc_node);

/*
 * validate_group - validate that the uid is authorized to access the partition
 * IN part_ptr - pointer to a partition
 * IN run_uid - user to run the job as
 * RET 1 if permitted to run, 0 otherwise
 */
extern int validate_group(part_record_t *part_ptr, uid_t run_uid);

/* Perform some size checks on strings we store to prevent
 * malicious user filling slurmctld's memory
 * IN job_desc   - user job submit request
 * IN submit_uid - UID making job submit request
 * OUT err_msg   - custom error message to return
 * RET 0 or error code */
extern int validate_job_create_req(job_desc_msg_t * job_desc, uid_t submit_uid,
				   char **err_msg);

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
 * IN slurm_msg - get node registration message it
 * OUT newly_up - set if node newly brought into service
 * RET 0 if no error, ENOENT if no such node, EINVAL if values too low
 * NOTE: READ lock_slurmctld config before entry
 */
extern int validate_node_specs(slurm_msg_t *slurm_msg, bool *newly_up);

/*
 * validate_nodes_via_front_end - validate all nodes on a cluster as having
 *	a valid configuration as soon as the front-end registers. Individual
 *	nodes will not register with this configuration
 * IN reg_msg - node registration message
 * IN protocol_version - Version of Slurm on this node
 * OUT newly_up - set if node newly brought into service
 * RET 0 if no error, Slurm error code otherwise
 * NOTE: READ lock_slurmctld config before entry
 */
extern int validate_nodes_via_front_end(
		slurm_node_registration_status_msg_t *reg_msg,
		uint16_t protocol_version, bool *newly_up);

/*
 * validate_slurm_user - validate that the uid is authorized to see
 *      privileged data (either user root or SlurmUser)
 * IN uid - user to validate
 * RET true if permitted to run, false otherwise
 */
extern bool validate_slurm_user(uid_t uid);

/*
 * validate_super_user - validate that the uid is authorized at the
 *      root, SlurmUser, or SLURMDB_ADMIN_SUPER_USER level
 * IN uid - user to validate
 * RET true if permitted to run, false otherwise
 */
extern bool validate_super_user(uid_t uid);

/*
 * validate_operator - validate that the uid is authorized at the
 *      root, SlurmUser, or SLURMDB_ADMIN_OPERATOR level
 * IN uid - user to validate
 * RET true if permitted to run, false otherwise
 */
extern bool validate_operator(uid_t uid);

/*
 * validate_operator_user_rec - validate that the user is authorized at the
 *      root, SlurmUser, or SLURMDB_ADMIN_OPERATOR level
 * IN user - slurmdb_user_rec_t of user to check
 * RET true if permitted to run, false otherwise
 */
extern bool validate_operator_user_rec(slurmdb_user_rec_t *user);

/* cleanup_completing()
 *
 * Clean up the JOB_COMPLETING flag and eventually
 * requeue the job if there is a pending request
 * for it. This function assumes the caller has the
 * appropriate locks on the job_record.
 * This function is called when a job completes
 * by either when the slurmd epilog finishes or
 * when the slurmctld epilog finishes, whichever
 * comes last.
 */
extern void cleanup_completing(job_record_t *job_ptr);

/*
 * Determine if slurmctld will respond to "configless" RPCs. If so,
 * load the internal cached config values to avoid regenerating on each
 * RPC.
 */
extern void configless_setup(void);
/* Free cached values to avoid memory leak. */
extern void configless_clear(void);

/*
 */
int
waitpid_timeout(const char *, pid_t, int *, int);

/*
 * Calculate and populate the number of tres' for all partitions.
 */
extern void set_partition_tres();

/*
 * Update job's federated siblings strings.
 *
 * IN job_ptr - job_ptr to update
 */
extern void update_job_fed_details(job_record_t *job_ptr);

/*
 * purge_job_record - purge specific job record. No testing is performed to
 *	ensure the job records has no active references. Use only for job
 *	records that were never fully operational (e.g. WILL_RUN test, failed
 *	job load, failed job create, etc.).
 * IN job_id - job_id of job record to be purged
 * RET int - count of job's purged
 * global: job_list - global job table
 */
extern int purge_job_record(uint32_t job_id);

/*
 * Remove job from job hashes so that it can't be found, but leave job in
 * job_table so that it can be deleted by _list_delete_job().
 *
 * IN job_ptr - job_ptr to be unlinked
 */
extern void unlink_job_record(job_record_t *job_ptr);

/*
 * copy_job_record_to_job_desc - construct a job_desc_msg_t for a job.
 * IN job_ptr - the job record
 * RET the job_desc_msg_t, NULL on error
 */
extern job_desc_msg_t *copy_job_record_to_job_desc(job_record_t *job_ptr);


/*
 * Set the allocation response with the current cluster's information and the
 * job's allocated node's addr's if the allocation is being filled by a cluster
 * other than the cluster that submitted the job
 *
 * Note: make sure that the resp's working_cluster_rec is NULL'ed out before the
 * resp is free'd since it points to global memory.
 *
 * IN resp - allocation response being sent back to client.
 * IN job_ptr - allocated job
 * IN req_cluster - the cluster requesting the allocation info.
 */
extern void
set_remote_working_response(resource_allocation_response_msg_t *resp,
			    job_record_t *job_ptr,
			    const char *req_cluster);

/*
 * Free job's fed_details ptr.
 */
extern void free_job_fed_details(job_fed_details_t **fed_details_pptr);

/*
 * Calculate billable TRES based on partition's defined BillingWeights. If none
 * is defined, return total_cpus. This is cached on job_ptr->billable_tres and
 * is updated if the job was resized since the last iteration.
 *
 * IN job_ptr          - job to calc billable tres on
 * IN start_time       - time the has started or been resized
 * IN assoc_mgr_locked - whether the tres assoc lock is set or not
 */
extern double calc_job_billable_tres(job_record_t *job_ptr, time_t start_time,
				     bool assoc_mgr_locked);

/*
 * Realloc and possibly update a job_ptr->limit_set->tres array.
 *
 * If a new TRES is added the TRES positions in the array could have been moved
 * around. The array either needs to be grown and/or the values need to be put
 * in their new position.
 *
 * IN: tres_limits - job_ptr->limit_set->tres array.
 */
extern void update_job_limit_set_tres(uint16_t **tres_limits);

/*
 * Validate TRES specification of the form:
 * "name=[type:]#[,[type:]#][;name=[type:]#]"
 * For example: "gpu:kepler:2,craynetwork=1"
 */
extern bool valid_tres_cnt(char *tres);

/*
 * Validate the named GRES is valid for scheduling parameters.
 * This is currently a subset of all defined TRES.
 */
extern bool valid_gres_name(char *name);

/*
 * Check for nodes that haven't rebooted yet.
 *
 * If the node hasn't booted by ResumeTimeout, mark the node as down.
 */
extern void check_reboot_nodes();

/*
 * Send warning signal to job before end time.
 *
 * IN job_ptr - job to send warn signal to.
 * IN ignore_time - If set, ignore the warn time and just send it.
 */
extern void send_job_warn_signal(job_record_t *job_ptr, bool ignore_time);

/*
 * Check if waiting for the node to still boot.
 *
 * IN node_ptr - node to check if still waiting for boot.
 *
 * RET return true if still expecting the node to boot, false otherwise.
 */
extern bool waiting_for_node_boot(node_record_t *node_ptr);

/*
 * Check if waiting for the node to still power down.
 *
 * IN node_ptr - node to check if still waiting for power down.
 *
 * RET return true if still expecting the node to power down, false otherwise.
 */
extern bool waiting_for_node_power_down(node_record_t *node_ptr);

/*
 * Check if any part of job_ptr is overlaping node_map.
 * IN node_map - bitstr of nodes set.
 * IN job_ptr (hetjob or not) to check.
 *
 * RET true if we overlap, false otherwise
 */
extern bool job_overlap_and_running(bitstr_t *node_map, job_record_t *job_ptr);

/*
 * Respond to request for backup slurmctld status
 */
extern void slurm_rpc_control_status(slurm_msg_t *msg);

/*
 * Callbacks to let the PrEp plugins signal completion if running async.
 */
extern void prep_prolog_slurmctld_callback(int rc, uint32_t job_id);
extern void prep_epilog_slurmctld_callback(int rc, uint32_t job_id);

/*
 * Set node's comm_name and hostname.
 */
extern void set_node_comm_name(node_record_t *node_ptr, char *comm_name,
			       char *hostname);

/*
 * Create a new file (file_name) and write (data) into it.
 * The file will have a trailing '\0' written into it, this makes it
 * easier to work with then loaded with create_mmap_buf as the string
 * representation of the file will be NUL terminated for us already.
 */
extern int write_data_to_file(char *file_name, char *data);

/*
 * Update a user's crontab entry, and submit new jobs as required.
 * Will mark existing crontab-submitted jobs as complete.
 */
extern void crontab_submit(crontab_update_request_msg_t *req_msg,
			   crontab_update_response_msg_t *response,
			   char *alloc_node, uint16_t protocol_version);

extern void crontab_add_disabled_lines(uid_t uid, int line_start, int line_end);

/*
 * Return a env** araray with common output job env variables.
 *
 * Used for <Pro|Epi>logSlurmctld and MailProg.
 */
extern char **job_common_env_vars(job_record_t *job_ptr, bool is_complete);

/*
 * Given a full system bitmap return the nth bit set where node_name is in it
 * IN - node_name - name of node
 * IN - node_bitmap - full system bitmap
 *
 * Used when you have a job/step specific array and you want to find the index
 * where that node is represented in that array.
 */
extern int job_get_node_inx(char *node_name, bitstr_t *node_bitmap);

/*
 * set_node_reboot_reason - appropriately set node reason with reboot message
 * IN node_ptr - node_ptr to the node
 * IN reason - message to be appended
 */
extern void set_node_reboot_reason(node_record_t *node_ptr, char *message);

#endif /* !_HAVE_SLURMCTLD_H */
