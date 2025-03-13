/*****************************************************************************\
 *  job_record.h - JOB parameters and data structures
 *****************************************************************************
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

#ifndef _SLURM_JOB_RECORD_H
#define _SLURM_JOB_RECORD_H

#include "src/common/cron.h"
#include "src/common/extra_constraints.h"
#include "src/common/front_end.h"
#include "src/common/part_record.h"
#include "src/common/slurm_protocol_defs.h"

#ifndef __slurmctld_resv_t_defined
#  define __slurmctld_resv_t_defined
typedef struct slurmctld_resv slurmctld_resv_t;
#endif

extern time_t last_job_update;	/* time of last update to job records */
extern list_t *purge_files_list; /* list of job ids to purge files of */

#define DETAILS_MAGIC	0xdea84e7
#define JOB_MAGIC	0xf0b7392c

/*
 * these related to the JOB_SHARED_ macros in slurm.h
 * but with the logic for zero vs one inverted
 */
#define WHOLE_NODE_REQUIRED	0x01
#define WHOLE_NODE_USER		0x02
#define WHOLE_NODE_MCS		0x04
#define WHOLE_TOPO 		0x08

#define OLD_WHOLE_NODE_MCS	0x03

#define IS_JOB_WHOLE_TOPO(_X) \
	((_X->details->whole_node & WHOLE_TOPO) || \
	 (_X->part_ptr && (_X->part_ptr->flags & PART_FLAG_EXCLUSIVE_TOPO)))

/* job_details - specification of a job's constraints */
typedef struct {
	uint32_t magic;			/* magic cookie for data integrity */
					/* DO NOT ALPHABETIZE */
	char *acctg_freq;		/* accounting polling interval */
	time_t accrue_time;             /* Time when we start accruing time for
					 * priority, */
	uint16_t *arbitrary_tpn;	/* array of the number of tasks on each
					 * node for arbitrary distribution */
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
	list_t *depend_list;		/* list of job_ptr:state pairs */
	char *dependency;		/* wait for other jobs */
	char *orig_dependency;		/* original value (for archiving) */
	uint16_t env_cnt;		/* size of env_sup (see below) */
	char *env_hash;			/* hash value of environment */
	char **env_sup;			/* supplemental environment variables */
	bitstr_t *exc_node_bitmap;	/* bitmap of excluded nodes */
	char *exc_nodes;		/* excluded nodes */
	uint32_t expanding_jobid;	/* ID of job to be expanded */
	list_t *feature_list;		/* required features with node counts */
	list_t *feature_list_use;	/* Use these features for scheduling,
					 * DO NOT FREE or PACK */
	char *features;			/* required features */
	char *features_use;		/* Use these features for scheduling,
					 * DO NOT FREE or PACK */
	bitstr_t *job_size_bitmap;
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

	/* job constraints: */
	uint32_t pn_min_cpus;		/* minimum processors per node */
	uint32_t orig_pn_min_cpus;	/* requested value of pn_min_cpus */
	uint64_t pn_min_memory;		/* minimum memory per node (MB) OR
					 * memory per allocated
					 * CPU | MEM_PER_CPU */
	uint64_t orig_pn_min_memory;	/* requested value of pn_min_memory */
	uint16_t oom_kill_step;		/* Kill whole step in case of OOM */
	uint32_t pn_min_tmp_disk;	/* minimum tempdisk per node, MB */
	list_t *prefer_list;		/* soft features with node counts */
	char *prefer;			/* soft features */
	uint8_t prolog_running;		/* set while prolog_slurmctld is
					 * running */
	char *qos_req;			/* quality of service(s) requested */
	uint32_t reserved_resources;	/* CPU minutes of resources reserved
					 * for this job while it was pending */
	bitstr_t *req_node_bitmap;	/* bitmap of required nodes */
	time_t preempt_start_time;	/* time that preeption began to start
					 * this job */
	char *req_context;		/* requested SELinux context */
	char *req_nodes;		/* required nodes */
	uint16_t requeue;		/* controls ability requeue job */
	uint16_t resv_port_cnt;		/* count of MPI ports reserved per node */
	uint16_t segment_size;
	uint8_t share_res;		/* set if job can share resources with
					 * other jobs */
	char *script;			/* DBD USE ONLY DON'T PACK:
					 * job's script */
	char *script_hash;              /* hash value of script NO NOT PACK */
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
} job_details_t;

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

#define HETJOB_PRIO_MIN	0x0001	/* Sort by minimum component priority[tier] */
#define HETJOB_PRIO_MAX	0x0002	/* Sort by maximum component priority[tier] */
#define HETJOB_PRIO_AVG	0x0004	/* Sort by average component priority[tier] */

typedef struct {
	bool any_resv;			/* at least one component with resv */
	uint32_t priority_tier;		/* whole hetjob calculated tier */
	uint32_t priority;		/* whole hetjob calculated priority */
} het_job_details_t;

typedef struct {
	time_t last_update;
	uint32_t *priority_array;
	char *priority_array_names;
} priority_mult_t;

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
	char *container_id;		/* OCI container id */
	uint32_t cpu_cnt;		/* current count of CPUs held
					 * by the job, decremented while job is
					 * completing */
	char *cpus_per_tres;		/* semicolon delimited list of TRES=# values */
	uint32_t db_flags;              /* Flags to send to the database
					 * record */
	uint64_t db_index;              /* used only for database plugins */
	time_t deadline;		/* deadline */
	uint32_t delay_boot;		/* Delay boot for desired node mode */
	uint32_t derived_ec;		/* highest exit code of all job steps */
	job_details_t *details;		/* job details */
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
	char *extra;			/* Arbitrary string */
	elem_t *extra_constraints;	/* Head of tree built from constraints
					 * defined in "extra". Not state saved.
					 */
	char *failed_node;		/* Name of node that failed which caused
					 * this job to be killed.
					 * NULL in all other situations */
	job_fed_details_t *fed_details;	/* details for federated jobs. */
	front_end_record_t *front_end_ptr; /* Pointer to front-end node running
					 * this job */
	list_t *gres_list_req;		/* Requested generic resource allocation
					   detail */
	list_t *gres_list_req_accum;	/* Requested generic resource allocation
					   detail with accumulated subtypes (DO
					   NOT SAVE OR PACK). Only needed during
					   allocation selection time and will
					   be rebuilt there if needed. */
	list_t *gres_list_alloc;	/* Allocated generic resource allocation
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
	list_t *het_job_list;		/* List of job pointers to all
					 * components */
	uint32_t job_id;		/* job ID */
	identity_t *id;			/* job identity */
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
	list_t *license_list;		/* structure with license info */
	list_t *licenses_to_preempt;    /* list of licenses the job will look
					   for in its preemptee candidates
					   Don't pack, don't save it's only used
					   during resource selection */
	char *lic_req;		/* required system licenses directly requested*/
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
	slurm_addr_t *node_addrs;	/* allocated node addrs */
	bitstr_t *node_bitmap;		/* bitmap of nodes allocated to job */
	bitstr_t *node_bitmap_cg;	/* bitmap of nodes completing job */
	bitstr_t *node_bitmap_pr;	/* bitmap of nodes with running prolog */
	bitstr_t *node_bitmap_preempt; /* bitmap of nodes selected for the job
					 * when trying to preempt other jobs.
					 * (DO NOT SAVE OR PACK). */
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
					 * epilog is not re-run for job
					 * used only to dump/load nodes from/to dump file */
	char *nodes_pr;			/* nodes with prolog running,
					 * used only to dump/load nodes from/to dump file */
	char *origin_cluster;		/* cluster name that the job was
					 * submitted from */
	uint16_t other_port;		/* port for client communications */
	char *partition;		/* name of job partition(s) */
	list_t *part_ptr_list;		/* list of pointers to partition recs */
	bool part_nodes_missing;	/* set if job's nodes removed from this
					 * partition */
	part_record_t *part_ptr;	/* pointer to the partition record */
	priority_mult_t *part_prio;	/* partition based priority */
	time_t pre_sus_time;		/* time job ran prior to last suspend */
	time_t preempt_time;		/* job preemption signal time */
	bool preempt_in_progress;	/* Preemption of other jobs in progress
					 * in order to start this job,
					 * (Internal use only, don't save) */
	uint32_t prep_epilog_cnt;	/* count of epilog async tasks left */
	uint32_t prep_prolog_cnt;	/* count of prolog async tasks left */
	bool prep_prolog_failed;	/* any prolog_slurmctld failed */
	uint32_t priority;		/* relative priority of the job,
					 * zero == held (don't initiate) */
	priority_factors_t *prio_factors; /* cached value of priority factors
					   * figured out in the priority plugin
					   */
	uint32_t profile;		/* Acct_gather_profile option */
	time_t prolog_launch_time;	/* When the prolog was launched from the
					 * controller -- PrologFlags=alloc */
	uint32_t qos_id;		/* quality of service id */
	list_t *qos_list;		/* Filled in if the job is requesting
					 * more than one QOS,
					 * DON'T PACK. */
	slurmdb_qos_rec_t *qos_ptr;	/* pointer to the quality of
					 * service record used for
					 * this job, confirm the
					 * value before use */
	void *qos_blocking_ptr;		/* internal use only, DON'T PACK */
	uint8_t reboot;			/* node reboot requested before start */
	uint16_t restart_cnt;		/* count of restarts */
	time_t resize_time;		/* time of latest size change */
	uint32_t resv_id;		/* reservation ID */
	list_t *resv_list;		/* Filled in if the job is requesting
					 * more than one reservation,
					 * DON'T PACK. */
	char *resv_name;		/* reservation name */
	slurmctld_resv_t *resv_ptr;	/* reservation structure pointer */
	char *resv_ports;		/* MPI ports reserved for job */
	int *resv_port_array;		/* MPI reserved port indexes */
	uint16_t resv_port_cnt;		/* count of MPI ports reserved per node */
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
	list_t *step_list;		/* list of job's steps */
	time_t suspend_time;		/* time job last suspended or resumed */
	void *switch_jobinfo;		/* opaque blob for switch plugin */
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
	uint32_t req_switch;  /* Maximum number of switches                */
	uint32_t wait4switch; /* Maximum time to wait for Maximum switches */
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

/* Used as the mode for update_node_active_features() */
typedef enum {
	FEATURE_MODE_IND,  /* Print each node change indivually */
	FEATURE_MODE_COMB, /* Try to combine like changes */
	FEATURE_MODE_PEND, /* Print any pending change message */
} feature_mode_t;

#define STEP_FLAG 0xbbbb
#define STEP_MAGIC 0xcafecafe

typedef struct {
	uint32_t magic;			/* magic cookie to test data integrity */
					/* DO NOT ALPHABETIZE */
	char *container;		/* OCI Container bundle path */
	char *container_id;		/* OCI Container ID */
	bitstr_t *core_bitmap_job;	/* bitmap of cores allocated to this
					 * step relative to job's nodes,
					 * see src/common/job_resources.h */
	uint32_t cpu_alloc_array_cnt;	/* Elements in cpu_alloc* arrays */
	uint32_t *cpu_alloc_reps;	/* Number of consecutive nodes for which
					 * a value in cpu_alloc_values is
					 * duplicated */
	uint16_t *cpu_alloc_values;	/* Compressed per-node allocated cpus */
	uint32_t cpu_count;		/* count of step's CPUs */
	uint32_t cpu_freq_min; 		/* Minimum cpu frequency  */
	uint32_t cpu_freq_max; 		/* Maximum cpu frequency  */
	uint32_t cpu_freq_gov; 		/* cpu frequency governor */
	uint16_t cpus_per_task;		/* cpus per task initiated */
	uint16_t ntasks_per_core;	/* Maximum tasks per core */
	char *cpus_per_tres;		/* semicolon delimited list of TRES=# values */
	uint16_t cyclic_alloc;		/* set for cyclic task allocation
					 * across nodes */
	uint32_t exit_code;		/* highest exit code from any task */
	bitstr_t *exit_node_bitmap;	/* bitmap of exited nodes */
	uint32_t flags;		        /* flags from step_spec_flags_t */
	list_t *gres_list_req;		/* generic resource request detail */
	list_t *gres_list_alloc;	/* generic resource allocation detail */
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
	dynamic_plugin_data_t *switch_step; /* switch context, opaque */
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
	list_t *job_queue;
	part_record_t *part_ptr;
	uint32_t prio;
	slurmctld_resv_t *resv_ptr;
} job_queue_req_t;

typedef struct {
	slurm_step_id_t *step_id;
	uint16_t show_flags;
	uid_t uid;
	uint32_t steps_packed;
	buf_t *buffer;
	bool privileged;
	uint16_t proto_version;
	bool valid_job;
	part_record_t **visible_parts;
	list_t *job_step_list;
	list_t *stepmgr_jobs;
	int (*pack_job_step_list_func)(void *x, void *arg);
} pack_step_args_t;

/*
 * Create and initialize job_record_t.
 *
 * Not added to a global job list.
 */
extern job_record_t *job_record_create(void);

/* free_step_record - delete a step record's data structures */
extern void free_step_record(void *x);

/*
 *  Delete a job record and its corresponding job_details,
 *
 * IN job_entry - pointer to job_record to delete
 */
extern void job_record_delete(void *job_entry);

/*
 * Free an xmalloc'd job_array_struct_t structure inside of a job_record_t and
 * set job_ptr->array_recs to NULL.
 */
extern void job_record_free_null_array_recs(job_record_t *array_recs);

/*
 * Free job's fed_details ptr.
 */
extern void job_record_free_fed_details(job_fed_details_t **fed_details_pptr);

extern int pack_ctld_job_step_info(void *x, void *arg);

/*
 * dump_job_step_state - dump the state of a specific job step to a buffer,
 *	load with load_step_state
 * IN step_ptr - pointer to job step for which information is to be dumped
 * IN/OUT buffer - location to store data, pointers automatically advanced
 */
extern int dump_job_step_state(void *x, void *arg);

/*
 * Create a new job step from data in a buffer (as created by
 * dump_job_stepstate)
 * IN/OUT - job_ptr - point to a job for which the step is to be loaded.
 * IN/OUT buffer - location from which to get data, pointers
 *                 automatically advanced
 */
extern int load_step_state(job_record_t *job_ptr, buf_t *buffer,
			   uint16_t protocol_version);

extern int job_record_calc_arbitrary_tpn(job_record_t *job_ptr);

extern void job_record_pack_details_common(
	job_details_t *detail_ptr, buf_t *buffer, uint16_t protocol_version);

extern void job_record_pack_common(job_record_t *dump_job_ptr,
				   bool for_state,
				   buf_t *buffer,
				   uint16_t protocol_version);
extern int job_record_unpack_common(job_record_t *dump_job_ptr,
				    buf_t *buffer,
				    uint16_t protocol_version);

/*
 * WARNING: this contains sensitive data. e.g., the x11_magic_cookie.
 * DO NOT use this in client-facing RPCs.
 */
extern int job_record_pack(job_record_t *dump_job_ptr,
			   int tres_cnt,
			   buf_t *buffer,
			   uint16_t proto_version);

extern int job_record_unpack(job_record_t **out,
			     int tres_cnt,
			     buf_t *buffer,
			     uint16_t protocol_version);

/*
 * create_step_record - create an empty step_record for the specified job.
 * IN job_ptr - pointer to job table entry to have step record added
 * IN protocol_version - slurm protocol version of client
 * RET a pointer to the record or NULL if error
 * NOTE: allocates memory that should be xfreed with delete_step_record
 */
extern step_record_t *create_step_record(job_record_t *job_ptr,
					 uint16_t protocol_version);

/*
 * _find_step_id - Find specific step_id entry in the step list,
 *		   see common/list.h for documentation
 * - object - the step list from a job_record_t
 * - key - slurm_step_id_t
 */
extern int find_step_id(void *object, void *key);

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
 * Realloc and possibly update a job_ptr->limit_set->tres array.
 *
 * If a new TRES is added the TRES positions in the array could have been moved
 * around. The array either needs to be grown and/or the values need to be put
 * in their new position.
 *
 * IN: tres_limits - job_ptr->limit_set->tres array.
 */
extern void update_job_limit_set_tres(uint16_t **tres_limits, int tres_cnt);

/*
 * Set a new sluid on the job_ptr
 */
extern void job_record_set_sluid(job_record_t *job_ptr);

#endif /* _SLURM_JOB_RECORD_H */
