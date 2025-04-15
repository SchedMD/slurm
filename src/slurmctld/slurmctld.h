/*****************************************************************************\
 *  slurmctld.h - definitions of functions and structures for slurmctld use
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Copyright (C) SchedMD LLC.
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
#include "src/common/extra_constraints.h"
#include "src/common/front_end.h"
#include "src/common/identity.h"
#include "src/common/job_record.h"
#include "src/common/job_resources.h"
#include "src/common/job_state_reason.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/node_conf.h"
#include "src/common/pack.h"
#include "src/common/read_config.h" /* location of slurm_conf */
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/timers.h"
#include "src/common/xmalloc.h"

#include "src/interfaces/cred.h"

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

#ifndef UPDATE_CONFIG_LIST_TIMEOUT
#define UPDATE_CONFIG_LIST_TIMEOUT 60
#endif

/* Report current node accounting state every PERIODIC_NODE_ACCT seconds */
#ifndef PERIODIC_NODE_ACCT
#define PERIODIC_NODE_ACCT 300
#endif

/* Seconds to wait for backup controller response to REQUEST_CONTROL RPC */
#ifndef CONTROL_TIMEOUT
#define CONTROL_TIMEOUT 30	/* seconds */
#endif

/*****************************************************************************\
 *  General configuration parameters and data structures
\*****************************************************************************/

typedef struct slurmctld_config {
	list_t *acct_update_list;
	pthread_cond_t acct_update_cond;
	pthread_mutex_t acct_update_lock;
	pthread_cond_t backup_finish_cond;
	pthread_mutex_t backup_finish_lock;
	time_t	boot_time;
	char    node_name_long[HOST_NAME_MAX];
	char    node_name_short[HOST_NAME_MAX];
	bool	resume_backup;
	bool    scheduling_disabled;
	int	server_thread_count;
	time_t	shutdown_time;
	bool    submissions_disabled;

	pthread_cond_t thread_count_cond;
	pthread_mutex_t thread_count_lock;
	pthread_t thread_id_acct_update;
	pthread_t thread_id_main;
	pthread_t thread_id_save;
	pthread_t thread_id_purge_files;
} slurmctld_config_t;

typedef enum {
	SCHEDULE_EXIT_END,
	SCHEDULE_EXIT_MAX_DEPTH,
	SCHEDULE_EXIT_MAX_JOB_START,
	SCHEDULE_EXIT_LIC,
	SCHEDULE_EXIT_RPC_CNT,
	SCHEDULE_EXIT_TIMEOUT,
	SCHEDULE_EXIT_COUNT
} schedule_exit_t;

typedef enum {
	BF_EXIT_END,
	BF_EXIT_MAX_JOB_START,
	BF_EXIT_MAX_JOB_TEST,
	BF_EXIT_STATE_CHANGED,
	BF_EXIT_TABLE_LIMIT,
	BF_EXIT_TIMEOUT,
	BF_EXIT_COUNT
} bf_exit_t;

/* Job scheduling statistics */
typedef struct diag_stats {
	int proc_req_threads;
	int proc_req_raw;

	uint32_t schedule_cycle_max;
	uint32_t schedule_cycle_last;
	uint32_t schedule_cycle_sum;
	uint32_t schedule_cycle_counter;
	uint32_t schedule_cycle_depth;
	uint32_t schedule_exit[SCHEDULE_EXIT_COUNT];
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
	uint32_t bf_exit[BF_EXIT_COUNT];
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
extern int   backup_inx;		/* BackupController# index */
extern int   batch_sched_delay;
extern bool cloud_dns;
extern uint32_t   cluster_cpus;
extern bool disable_remote_singleton;
extern int listen_nports;
extern int max_depend_depth;
extern uint32_t max_powered_nodes;
extern pthread_cond_t purge_thread_cond;
extern pthread_mutex_t purge_thread_lock;
extern pthread_mutex_t check_bf_running_lock;
extern int   sched_interval;
extern bool  slurmctld_init_db;
extern bool slurmctld_primary;
extern int   slurmctld_tres_cnt;
extern slurmdb_cluster_rec_t *response_cluster_rec;

/*****************************************************************************\
 * Configless data structures, defined in src/slurmctld/proc_req.c
\*****************************************************************************/
extern bool running_configless;

/*****************************************************************************\
 *  NODE parameters and data structures, mostly in src/common/node_conf.h
\*****************************************************************************/
extern bool ping_nodes_now;		/* if set, ping nodes immediately */
extern bool want_nodes_reboot;		/* if set, check for idle nodes */
extern bool ignore_state_errors;

extern list_t *conf_includes_list;  /* list of conf_includes_map_t */

#define PACK_FANOUT_ADDRS(_X) \
	(IS_NODE_DYNAMIC_FUTURE(_X) || \
	 IS_NODE_DYNAMIC_NORM(_X) || \
	 (!cloud_dns && IS_NODE_CLOUD(_X)))

/*****************************************************************************\
 *  NODE states and bitmaps
 *  asap_node_bitmap 	    Set if the node is marked to be rebooted asap
 *  avail_node_bitmap       Set if node's state is not DOWN, DRAINING/DRAINED,
 *                          FAILING or NO_RESPOND (i.e. available to run a job)
 *  booting_node_bitmap     Set if node in process of booting
 *  cg_node_bitmap          Set if node in completing state
 *  cloud_node_bitmap       Set if node in CLOUD state
 *  future_node_bitmap      Set if node in FUTURE state
 *  idle_node_bitmap        Set if node has no jobs allocated to it
 *  power_down_node_bitmap  Set for nodes which are powered down
 *  power_up_node_bitmap    Set for nodes which are powered down
 *  share_node_bitmap       Set if no jobs allocated exclusive access to
 *                          resources on that node (cleared if --exclusive
 *                          option specified by job or OverSubscribe=NO
 *                          configured for the job's partition)
 *  up_node_bitmap          Set if the node's state is not DOWN
\*****************************************************************************/
extern bitstr_t *asap_node_bitmap; /* reboot asap nodes */
extern bitstr_t *avail_node_bitmap;	/* bitmap of available nodes,
					 * state not DOWN, DRAIN or FAILING */
extern bitstr_t *bf_ignore_node_bitmap;	/* bitmap of nodes made available during
					 * backfill cycle */
extern bitstr_t *booting_node_bitmap;	/* bitmap of booting nodes */
extern bitstr_t *cg_node_bitmap;	/* bitmap of completing nodes */
extern bitstr_t *cloud_node_bitmap;	/* bitmap of cloud nodes */
extern bitstr_t *future_node_bitmap;	/* bitmap of FUTURE nodes */
extern bitstr_t *idle_node_bitmap;	/* bitmap of idle nodes */
extern bitstr_t *power_down_node_bitmap; /* Powered down nodes */
extern bitstr_t *power_up_node_bitmap;	/* Powered up and requested nodes */
extern bitstr_t *share_node_bitmap;	/* bitmap of sharable nodes */
extern bitstr_t *up_node_bitmap;	/* bitmap of up nodes, not DOWN */
extern bitstr_t *rs_node_bitmap;	/* next_state=resume nodes */

/*****************************************************************************\
 *  PARTITION parameters and data structures
\*****************************************************************************/
extern list_t *part_list;		/* list of part_record entries */
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
	char *comment;		/* arbitrary comment			*/
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
	list_t *gres_list_alloc;/* Allocated generic resource allocation
				 * detail */
	char *groups;		/* names of linux groups permitted to use */
	uint32_t job_pend_cnt;	/* number of pending jobs		*/
	uint32_t job_run_cnt;	/* number of running jobs		*/
	list_t *license_list;	/* structure with license info		*/
	char *licenses;		/* required system licenses (including those
				 * from TRES requests */
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

typedef struct {
	bitstr_t *core_bitmap;
	void *gres_js_exc;
	void *gres_js_inc;
	list_t *gres_list_exc;
	list_t *gres_list_inc;
	bitstr_t **exc_cores;
} resv_exc_t;

extern list_t *resv_list;	/* list of slurmctld_resv_t entries */
extern time_t last_resv_update;	/* time of last resv_list update */

/*****************************************************************************\
 *  Job lists
\*****************************************************************************/
extern list_t *job_list;		/* list of job_record entries */
extern list_t *purge_jobs_list;		/* list of job_record_t to free */

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
	SELECT_CR_PLUGIN = 0,    /* data-> uint32 See SELECT_TYPE_* below */
	SELECT_CONFIG_INFO = 6,  /* data-> list_t * get .conf info from select
				  * plugin */
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

/* Note that the backup slurmctld has assumed primary control.
 * This function can be called multiple times. */
extern void backup_slurmctld_restart(void);

/* Handle SIGHUP while in backup mode */
extern void backup_on_sighup(void);

/* Complete a batch job requeue logic after all steps complete so that
 * subsequent jobs appear in a separate accounting record. */
extern void batch_requeue_fini(job_record_t *job_ptr);

/* Build a bitmap of nodes completing this job */
extern void build_cg_bitmap(job_record_t *job_ptr);

/* Build structure with job allocation details */
extern resource_allocation_response_msg_t *build_job_info_resp(
	job_record_t *job_ptr);

/*
 * create_ctld_part_record - create a partition record
 * IN name - name will be xstrdup()'d into the part_record
 * RET a pointer to the record or NULL if error
 * global: default_part - default partition parameters
 *         part_list - global partition list
 * NOTE: the record's values are initialized to those of default_part
 * NOTE: allocates memory that should be xfreed with delete_part_record
 */
extern part_record_t *create_ctld_part_record(const char *name);

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
 * IN delete_part_msg_t - partition specification from RPC
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
 * Copy a job's dependency list
 * IN depend_list_src - a job's depend_lst
 * RET copy of depend_list_src, must bee freed by caller
 */
extern list_t *depended_list_copy(list_t *depend_list_src);

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

/*
 * Set job state
 * IN job_ptr - Job to update
 * IN state - state from enum job_states
 */
extern void job_state_set(job_record_t *job_ptr, uint32_t state);

/*
 * Set job state flag
 * IN job_ptr - Job to update
 * IN flag - flag to set (from JOB_* macro)
 */
extern void job_state_set_flag(job_record_t *job_ptr, uint32_t flag);

/*
 * Unset job state flag
 * IN job_ptr - Job to update
 * IN flag - flag to unset (from JOB_* macro)
 */
extern void job_state_unset_flag(job_record_t *job_ptr, uint32_t flag);

/* dump_all_job_state - save the state of all jobs to file
 * RET 0 or error code */
extern int dump_all_job_state ( void );

/*
 * Notify/update job state hash table that job state has changed
 * IN job_ptr - Job about to be updated
 * IN new_state - New value that will be assigned to job_ptr->job_state.
 *                If NO_VAL, then delete the cache entry.
 */
extern void on_job_state_change(job_record_t *job_ptr, uint32_t new_state);

/* dump_all_node_state - save the state of all nodes to file */
extern int dump_all_node_state ( void );

/* dump_all_part_state - save the state of all partitions to file */
extern int dump_all_part_state ( void );

/*
 * dump_job_desc - dump the incoming job submit request message
 * IN job_desc - job specification from RPC
 */
extern void dump_job_desc(job_desc_msg_t *job_desc);

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
 * Reset load & power statistics for node.
 *
 * When node is powered down or downed unexpectedly, the load/power stats
 * are effectively '0'.
 *
 * IN node_ptr - node to reset statistics for.
 */
extern void node_mgr_reset_node_stats(node_record_t *node_ptr);

/*
 * Copy a job's feature list
 * IN feature_list_src - a job's depend_lst
 * RET copy of depend_list_src, must be freed by caller
 */
extern list_t *feature_list_copy(list_t *feature_list_src);

typedef enum {
	FOR_EACH_JOB_BY_ID_EACH_INVALID = 0,
	FOR_EACH_JOB_BY_ID_EACH_CONT, /* continue for each processing */
	FOR_EACH_JOB_BY_ID_EACH_STOP, /* stop for each processing */
	FOR_EACH_JOB_BY_ID_EACH_FAIL, /* stop for each processing due to failure */
	FOR_EACH_JOB_BY_ID_EACH_INVALID_MAX /* assertion only value on max value */
} foreach_job_by_id_control_t;

/*
 *  Function prototype for operating on each job that matches
 *  Returns control requested for processing
 */
typedef foreach_job_by_id_control_t (*JobForEachFunc)(
					job_record_t *job_ptr,
					const slurm_selected_step_t *id,
					void *arg);
/*
 *  Function prototype for operating on each read only job that matches
 *  Returns control requested for processing
 */
typedef foreach_job_by_id_control_t (*JobROForEachFunc)(
					const job_record_t *job_ptr,
					const slurm_selected_step_t *id,
					void *arg);
/*
 * Function prototype for operating on a job id that is not found
 * This is called just once for an array expression with a bitmap of array
 * tasks that were not found.
 * Returns control requested for processing
 */
typedef foreach_job_by_id_control_t
	(*JobNullForEachFunc)(const slurm_selected_step_t *id, void *arg);

/*
 * Walk all matching job_record_t's that match filter
 * If a job id is a het job leader or array job leader, then all components of
 * the het job or all jobs in the array will be walked.
 * Warning: Caller must hold job write lock
 *
 * IN filter - Filter to select jobs
 * IN callback - Function to call on each matching job record pointer
 *               NOTE: If array_task_id was given and the task has not been
 *               split from the meta job record, the meta job record will be
 *               passed to the callback function.
 * IN null_callback - (optional) Function to call on each non-matching job id
 * IN arg - Arbitrary pointer to pass to callback
 * RET number of jobs matched.
 * 	negative if callback returns FOR_EACH_JOB_BY_ID_EACH_FAIL.
 * 	may be zero if no jobs matched.
 */
extern int foreach_job_by_id(const slurm_selected_step_t *filter,
			     JobForEachFunc callback,
			     JobNullForEachFunc null_callback, void *arg);

/*
 * Walk all matching read only job_record_t's that match filter
 * If a job id is a het job leader or array job leader, then all components of
 * the het job or all jobs in the array will be walked.
 * Warning: Caller must hold job read lock
 *
 * IN filter - Filter to select jobs
 * IN callback - Function to call on each matching job record pointer
 *               NOTE: If array_task_id was given and the task has not been
 *               split from the meta job record, the meta job record will be
 *               passed to the callback function.
 * IN null_callback - (optional) Function to call on each non-matching job id
 * IN arg - Arbitrary pointer to pass to callback
 * RET number of jobs matched.
 * 	negative if callback returns FOR_EACH_JOB_BY_ID_EACH_FAIL.
 * 	may be zero if no jobs matched.
 */
extern int foreach_job_by_id_ro(const slurm_selected_step_t *filter,
				JobROForEachFunc callback,
				JobNullForEachFunc null_callback, void *arg);

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
 * find_part_record - find a record for partition with specified name
 * IN name - name of the desired partition
 * RET pointer to partition or NULL if not found
 */
extern part_record_t *find_part_record(char *name);

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
 * OUT part_ptr_list - sorted list of pointers to the partitions or NULL
 * OUT prim_part_ptr - pointer to the primary partition
 * OUT err_part - The first invalid partition name.
 * NOTE: Caller must free the returned list
 * NOTE: Caller must free err_part
 */
extern void get_part_list(char *name, list_t **part_ptr_list,
			  part_record_t **prim_part_ptr, char **err_part);

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
 * consolidate_config_list
 * Try to combine duplicate config records.
 *
 * IN is_locked - whether NODE_WRITE_LOCK is set or not.
 * IN bool - whether to do consolidate regardless of a queued event.
 */
extern void consolidate_config_list(bool is_locked, bool force);

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
extern int job_fail_qos(job_record_t *job_ptr, const char *func_name,
			bool assoc_locked);

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
 * IN job_desc - job specifications
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
extern int job_allocate(job_desc_msg_t *job_desc, int immediate,
			int will_run, will_run_response_msg_t **resp,
			int allocate, uid_t submit_uid, bool cron,
			job_record_t **job_pptr,
			char **err_msg, uint16_t protocol_version);

/* If this is a job array meta-job, prepare it for being scheduled */
extern void job_array_pre_sched(job_record_t *job_ptr);

/* If this is a job array meta-job, clean up after scheduling attempt */
extern job_record_t *job_array_post_sched(job_record_t *job_ptr, bool list_add);

/* Create an exact copy of an existing job record for a job array.
 * IN job_ptr - META job record for a job array, which is to become an
 *		individial task of the job array.
 *		Set the job's array_task_id to the task to be split out.
 * IN list_add - add to the job_list or not.
 * RET - The new job record, which is the new META job record. */
extern job_record_t *job_array_split(job_record_t *job_ptr, bool list_add);

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
				 list_t *gres_list, bool user_set_mem,
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
 * Signal the jobs matching the specified filters and build a response message
 * detailing the results of the request.
 *
 * IN kill_msg - the specification for which jobs to signal
 * IN auth_uid - the authenticated UID of the requesting user
 * OUT resp_msg_p - a response message to send back to the requesting user
 * RET - SLURM_SUCCESS if successful, an error code otherwise
 */
extern int job_mgr_signal_jobs(kill_jobs_msg_t *kill_msg, uid_t auth_uid,
			       kill_jobs_resp_msg_t **resp_msg_p);

/*
 * job_suspend/job_suspend2 - perform some suspend/resume operation
 * NB job_suspend  - Uses the job_id field and ignores job_id_str
 * NB job_suspend2 - Ignores the job_id field and uses job_id_str
 *
 * IN sus_ptr - suspend/resume request message
 * IN uid - user id of the user issuing the RPC
 * indf_susp IN - set if job is being suspended indefinitely by user or admin
 *                and we should clear it's priority, otherwise suspended
 *		  temporarily for gang scheduling
 * IN protocol_version - slurm protocol version of client
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_suspend(slurm_msg_t *msg, suspend_msg_t *sus_ptr, uid_t uid,
		       bool indf_susp, uint16_t protocol_version);
extern int job_suspend2(slurm_msg_t *msg, suspend_msg_t *sus_ptr, uid_t uid,
			bool indf_susp, uint16_t protocol_version);

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
 *	It just matches first element of MOR and ignores count.
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
 * IN msg - original request msg
 * IN top_ptr - user request
 * IN uid - user id of the user issuing the RPC
 * IN protocol_version - slurm protocol version of client
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_set_top(slurm_msg_t *msg, top_job_msg_t *top_ptr, uid_t uid,
		       uint16_t protocol_version);

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
extern void load_part_uid_allow_list(bool force);

/*
 * load_all_part_state - load the partition state from file, recover from
 *	slurmctld restart. execute this after loading the configuration
 *	file data.
 */
extern int load_all_part_state(uint16_t reconfig_flags);

/* make_node_alloc - flag specified node as allocated to a job
 * IN node_ptr - pointer to node being allocated
 * IN job_ptr  - pointer to job that is starting
 */
extern void make_node_alloc(node_record_t *node_ptr, job_record_t *job_ptr);

extern void node_mgr_make_node_blocked(job_record_t *job_ptr, bool set);

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

/* msg_to_slurmd - send given msg_type every slurmd, no args */
extern void msg_to_slurmd (slurm_msg_type_t msg_type);

/* request a "configless" RPC be send to all slurmd nodes */
void push_reconfig_to_slurmd(void);

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
 * Dump state of jobs given list of jobs (or none for all jobs)
 * IN filter_jobs_count - number of entries in filter_jobs_ptr array
 * IN filter_jobs_ptr - array of jobs to filter
 * IN/OUT jobs_count_ptr - pointer to number of jobs dumped
 * IN/OUT jobs_pptr - pointer to dumped jobs array
 * RET SLURM_SUCCESS or error
 */
extern int dump_job_state(const uint32_t filter_jobs_count,
			  const slurm_selected_step_t *filter_jobs_ptr,
			  uint32_t *jobs_count_ptr,
			  job_state_response_job_t **jobs_pptr);

/*
 * pack_all_jobs - dump all job information for all jobs in
 *	machine independent form (for network transmission)
 * IN show_flags - job filtering options
 * IN uid - uid of user making request (for partition filtering)
 * IN filter_uid - pack only jobs belonging to this user if not NO_VAL
 * IN protocol_version - slurm protocol version of client
 * OUT buffer
 * global: job_list - global list of job records
 * NOTE: change _unpack_job_desc_msg() in common/slurm_protocol_pack.c
 *	whenever the data format changes
 */
extern buf_t *pack_all_jobs(uint16_t show_flags, uid_t uid, uint32_t filter_uid,
			    uint16_t protocol_version);

/*
 * pack_spec_jobs - dump job information for specified jobs in
 *	machine independent form (for network transmission)
 * IN show_flags - job filtering options
 * IN job_ids - list of job_ids to pack
 * IN uid - uid of user making request (for partition filtering)
 * IN filter_uid - pack only jobs belonging to this user if not NO_VAL
 * OUT buffer
 * global: job_list - global list of job records
 * NOTE: the buffer at *buffer_ptr must be xfreed by the caller
 * NOTE: change _unpack_job_desc_msg() in common/slurm_protocol_pack.c
 *	whenever the data format changes
 */
extern buf_t *pack_spec_jobs(list_t *job_ids, uint16_t show_flags, uid_t uid,
			     uint32_t filter_uid, uint16_t protocol_version);

/*
 * pack_all_nodes - dump all configuration and node information for all nodes
 *	in machine independent form (for network transmission)
 * IN show_flags - node filtering options
 * IN uid - uid of user making request (for partition filtering)
 * IN protocol_version - slurm protocol version of client
 * OUT buffer
 * global: node_record_table_ptr - pointer to global node table
 * NOTE: change slurm_load_node() in api/node_info.c when data format changes
 * NOTE: READ lock_slurmctld config before entry
 */
extern buf_t *pack_all_nodes(uint16_t show_flags, uid_t uid,
			     uint16_t protocol_version);

/* Pack all scheduling statistics */
extern buf_t *pack_all_stat(uint16_t protocol_version);

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
 * IN show_flags - partition filtering options
 * IN uid - uid of user making request (for partition filtering)
 * IN protocol_version - slurm protocol version of client
 * OUT buffer
 * global: part_list - global list of partition records
 * NOTE: change slurm_load_part() in api/part_info.c if data format changes
 */
extern buf_t *pack_all_part(uint16_t show_flags, uid_t uid,
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
 * IN job_id - ID of job that we want info for
 * IN show_flags - job filtering options
 * IN uid - uid of user making request (for partition filtering)
 * OUT buffer
 * NOTE: change _unpack_job_desc_msg() in common/slurm_protocol_pack.c
 *	whenever the data format changes
 */
extern buf_t *pack_one_job(uint32_t job_id, uint16_t show_flags, uid_t uid,
			   uint16_t protocol_version);

/*
 * pack_one_node - dump all configuration and node information for one node
 *	in machine independent form (for network transmission)
 * IN show_flags - node filtering options
 * IN uid - uid of user making request (for partition filtering)
 * IN node_name - name of node for which information is desired,
 *		  use first node if name is NULL
 * IN protocol_version - slurm protocol version of client
 * OUT buffer
 * global: node_record_table_ptr - pointer to global node table
 * NOTE: change slurm_load_node() in api/node_info.c when data format changes
 * NOTE: READ lock_slurmctld config before entry
 */
extern buf_t *pack_one_node(uint16_t show_flags, uid_t uid, char *node_name,
			    uint16_t protocol_version);

/* part_not_on_list - helper function to check if array parts contains x */
extern int part_not_on_list(part_record_t **parts, part_record_t *x);

/*
 * build_visible_parts - returns an array with pointers to partitions visible
 * to user based on partition Hidden and AllowedGroups properties.
 */
extern part_record_t **build_visible_parts(uid_t uid, bool privileged);

/* part_fini - free all memory associated with partition records */
extern void part_fini (void);

/*
 * Create a copy of a job's part_list *partition list
 * IN part_list_src - a job's part_list
 * RET copy of part_list_src, must be freed by caller
 */
extern list_t *part_list_copy(list_t *part_list_src);

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
 * IN submit_uid - uid of user issuing the request
 * in job_ptr - Job pointer or NULL. If set and job can not run, then set the
 *		job's state_desc and state_reason fields
 * RET SLURM_SUCCESS or error code
 */
extern int part_policy_valid_qos(part_record_t *part_ptr,
				 slurmdb_qos_rec_t *qos_ptr,
				 uid_t submit_uid,
				 job_record_t *job_ptr);

/*
 * part_list_update_assoc_lists - Update assoc_mgr pointers from
 *                                [allow|deny]_accts_lists.
 */
extern void part_list_update_assoc_lists(void);


/*
 * part_update_assoc_lists - Update assoc_mgr pointers from
 *                           [allow|deny]_accts_lists.
 * IN x - part_rec_t
 */
extern int part_update_assoc_lists(void *x, void *arg);

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
extern int prolog_complete(uint32_t job_id, uint32_t prolog_return_code,
			   char *node_name);

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

/*
 * Free memory from purged job records. This is a distinct phase from
 * purge_old_job() so this can run outside of the job write lock.
 */
extern void free_old_jobs(void);

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
 * Setup and prepare job state cache (if configured)
 * IN new_hash_table_size - number of entries in hash table
 */
extern void setup_job_state_hash(int new_hash_table_size);

/* update first assigned job id as needed on reconfigure */
extern void reset_first_job_id(void);

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

/* run_backup - this is the backup controller, it should run in standby
 *	mode, assuming control when the primary controller stops responding */
extern void run_backup(void);

/* conmgr RPC connection callbacks */
extern void *on_backup_connection(conmgr_fd_t *con, void *arg);
extern void on_backup_finish(conmgr_fd_t *con, void *arg);
extern int on_backup_msg(conmgr_fd_t *con, slurm_msg_t *msg, void *arg);

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

/* Make sure the assoc_mgr thread is terminated */
extern void ctld_assoc_mgr_fini(void);

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

/*
 * Set a job's node_addrs
 *
 * IN job_ptr - job to set node_addrs on
 * IN origin_cluster - cluster creating/requesting addrs.
 */
extern void set_job_node_addrs(job_record_t *job_ptr,
			       const char *origin_cluster);

/*
 * Set a job's initial alias_list/node_addrs.
 *
 * If the job's node list has powering up nodes then set alias_list to "TBD".
 */
extern void set_initial_job_alias_list(job_record_t *job_ptr);

/* Set a job's alias_list string */
extern void set_job_alias_list(job_record_t *job_ptr);

/* Set a job's features_use and feature_list_use pointers */
extern void set_job_features_use(job_details_t *details_ptr);

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
 * job_mgr_dump_job_state - dump the state of a specific job, its details, and
 *	steps to a buffer
 * IN dump_job_ptr - pointer to job for which information is requested
 * IN/OUT buffer - location to store data, pointers automatically advanced
 */
extern int job_mgr_dump_job_state(void *object, void *arg);

/*
 * job_mgr_load_job_state - Unpack a job's state information from a buffer
 *
 * If job_ptr_out is not NULL it will be filled in outside of the job_list.
 *
 * NOTE: assoc_mgr qos, tres and assoc read lock must be unlocked before
 * calling
 */
extern int job_mgr_load_job_state(buf_t *buffer,
				  uint16_t protocol_version);


/* For the job array data structure, build the string representation of the
 * bitmap.
 * NOTE: bit_fmt_hexmask() is far more scalable than bit_fmt(). */
extern void build_array_str(job_record_t *job_ptr);

/*
 * Return the number of usable logical processors by a given job on
 * some specified node. Returns INFINITE16 if no limit.
 */
extern uint16_t job_mgr_determine_cpus_per_core(
	job_details_t *details, int node_inx);

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
 * IN uid - uid of user issuing RPC
 * RET returns an error code from slurm_errno.h
 * global: job_list - global list of job entries
 *	last_job_update - time of last job table update
 */
extern int update_job_str(slurm_msg_t *msg, uid_t uid);

/*
 * Allocate a kill_job_msg_t and populate most fields.
 */
extern kill_job_msg_t *create_kill_job_msg(job_record_t *job_ptr,
					   uint16_t protocol_version);

/*
 * Modify the wckey associated with a pending job
 * IN module - where this is called from
 * IN job_ptr - pointer to job which should be modified
 * IN new_wckey - desired wckey name
 * RET SLURM_SUCCESS or error code
 */
extern int update_job_wckey(char *module, job_record_t *job_ptr,
			    char *new_wckey);

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
 * IN auth_uid - UID that issued the update
 * RET 0 or error code
 * global: node_record_table_ptr - pointer to global node table
 */
extern int update_node(update_node_msg_t *update_node_msg, uid_t auth_uid);

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
 * Create nodes from scontrol using slurm.conf nodeline syntax.
 *
 * IN nodeline - slurm.conf nodename description.
 * OUT err_msg - pass error messages out.
 * RET SLURM_SUCCESS on success, SLURM_ERROR otherwise.
 */
extern int create_nodes(char *nodeline, char **err_msg);

/*
 * Create and add dynamic node to system from registration.
 *
 * IN msg - slurm_msg_t containing slurm_node_registration_status_msg_t.
 * RET SLURM_SUCCESS on success, SLURM_ERROR otherwise.
 */
extern int create_dynamic_reg_node(slurm_msg_t *msg);

/*
 * Delete node names from system from a slurmctld perspective.
 *
 * e.g. remove node from partitions, reconfig cons_tres, etc.
 *
 * IN names - node names to delete.
 * OUT err_msg - pass error messages out.
 * RET SLURM_SUCCESS on success, error code otherwise.
 */
extern int delete_nodes(char *names, char **err_msg);

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

/*
 * Sort all jobs' part_ptr_list to be in descending order according to
 * partition priority tier. This Should be called anytime a partition's priority
 * tier is modified.
 */
extern void sort_all_jobs_partition_lists();

/*
 * Common code to handle a job when a cred can't be created.
 */
extern void job_mgr_handle_cred_failure(job_record_t *job_ptr);

/*
 * validate_alloc_node - validate that the allocating node
 * is allowed to use this partition
 * IN part_ptr - pointer to a partition
 * IN alloc_node - allocating node of the request
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
extern int validate_job_create_req(job_desc_msg_t *job_desc, uid_t submit_uid,
				   char **err_msg);

/*
 * validate_jobs_on_node - validate that any jobs that should be on the node
 *	are actually running, if not clean up the job records and/or node
 *	records.
 *
 * IN slurm_msg - contains the node registration message
 */
extern void validate_jobs_on_node(slurm_msg_t *slurm_msg);

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
extern bool validate_operator_locked(uid_t uid);

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
/* Reload the internal cached config values. */
extern void configless_update(void);
/* Free cached values to avoid memory leak. */
extern void configless_clear(void);

/*
 * Calculate and populate the number of tres' for all partitions.
 */
extern void set_partition_tres(bool assoc_mgr_locked);

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
 * Check for node timed events
 *
 * Such as:
 * reboots - If the node hasn't booted by ResumeTimeout, mark the node as down.
 * resume_after - Resume a down|drain node after resume_after time.
 */
extern void check_node_timers(void);

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
extern bool job_overlap_and_running(bitstr_t *node_map, list_t *license_list,
				    job_record_t *job_ptr);

/*
 * Respond to request for backup slurmctld status
 */
extern void slurm_rpc_control_status(slurm_msg_t *msg);

/*
 * Callbacks to let the PrEp plugins signal completion if running async.
 */
extern void prep_prolog_slurmctld_callback(int rc, uint32_t job_id,
					   bool timed_out);
extern void prep_epilog_slurmctld_callback(int rc, uint32_t job_id,
					   bool timed_out);

/*
 * Set node's comm_name and hostname.
 *
 * If comm_name is NULL, hostname will be used for both fields.
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
			   char *alloc_node, identity_t *id,
			   uint16_t protocol_version);

extern void crontab_add_disabled_lines(uid_t uid, int line_start, int line_end);

/*
 * Return a env** araray with common output job env variables.
 *
 * Used for <Pro|Epi>logSlurmctld and MailProg.
 */
extern char **job_common_env_vars(job_record_t *job_ptr, bool is_complete);

/*
 * update_node_active_features - Update active features associated with nodes
 * IN node_names - list of nodes to update
 * IN active_features - New active features value
 * IN mode - FEATURE_MODE_IND : Print each node change indivually
 *           FEATURE_MODE_COMB: Try to combine like changes (SEE NOTE BELOW)
 *           FEATURE_MODE_PEND: Print any pending change message
 * RET: SLURM_SUCCESS or error code
 * NOTE: Use mode=FEATURE_MODE_IND in a loop with node write lock set,
 *	 then call with mode=FEATURE_MODE_PEND at the end of the loop
 */
extern int update_node_active_features(char *node_names, char *active_features,
				       int mode);

/*
 * update_node_avail_features - Update available features associated with
 *	nodes, build new config list records as needed
 * IN node_names - list of nodes to update
 * IN avail_features - New available features value
 * IN mode - FEATURE_MODE_IND : Print each node change indivually
 *           FEATURE_MODE_COMB: Try to combine like changes (SEE NOTE BELOW)
 *           FEATURE_MODE_PEND: Print any pending change message
 * RET: SLURM_SUCCESS or error code
 * NOTE: Use mode=FEATURE_MODE_IND in a loop with node write lock set,
 *	 then call with mode=FEATURE_MODE_PEND at the end of the loop
 */
extern int update_node_avail_features(char *node_names, char *avail_features,
				      int mode);

/*
 * Filter out changeable features and only feature conf only features
 *
 * IN features - features string to remove changeable features from
 *
 * RET: return xmalloc'ed string that doesn't contain changeable features.
 */
extern char *filter_out_changeable_features(const char *features);

/*
 * Reset a nodes active features to only non-changeable available features.
 */
extern void reset_node_active_features(node_record_t *node_ptr);

/*
 * Reset a node's instance variables
 */
extern void reset_node_instance(node_record_t *node_ptr);

/*
 * Return a hostlist with expanded node specification.
 *
 * Handles node range expressions, nodesets and ALL keyword.
 *
 * IN nodes - nodelist that can have nodesets or ALL in it.
 * IN uniq - call hostlist_uniq() before returning the hostlist
 * OUT nodesets (optional) - list of nodesets found in nodes string
 *
 * RET NULL on error, hostlist_t otherwise.
 *
 * NOTE: Caller must FREE_NULL_HOSTLIST() returned hostlist_t.
 * NOTE: Caller should interpret a non-NULL but empty hostlist conveniently.
 */
extern hostlist_t *nodespec_to_hostlist(const char *nodes, bool uniq,
					char **nodesets);

/*
 * set_node_reason - appropriately set node reason with message
 * IN node_ptr - node_ptr to the node
 * IN message - message to be set/appended
 * IN time - timestamp of message
 */
extern void set_node_reason(node_record_t *node_ptr,
			    char *message,
			    time_t time);

extern void reconfigure_slurm(slurm_msg_t *msg);

extern void notify_parent_of_success(void);

/*
 * free_job_record - delete a job record and its corresponding
 *	job_details,
 *	see common/list.h for documentation
 * IN job_entry - pointer to job_record to delete
 */
extern void free_job_record(void *job_entry);

/*
 * Build a job rec from an advanced reservation request.
 */
extern job_record_t *job_mgr_copy_resv_desc_to_job_record(
	resv_desc_msg_t *resv_desc_ptr);

/*
 * Initialize the various schedulers.
 */
extern int controller_init_scheduling(bool init_gang);

/*
 * Finialize the various schedulers.
 */
extern void controller_fini_scheduling(void);

/*
 * Reconfigure the various schedulers.
 */
extern void controller_reconfig_scheduling(void);

/*
 * Return a comma separate xstr of partition names from a list of
 * part_record_t's.
 */
extern char *part_list_to_xstr(list_t *list);

/* Allow listener sockets to accept() new incoming requests */
extern void listeners_unquiesce(void);

/* Stop listener sockets from accept()ing new incoming requests */
extern void listeners_quiesce(void);

#endif /* !_HAVE_SLURMCTLD_H */
