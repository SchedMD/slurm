/*****************************************************************************\
 *  slurm_opt.h - definitions for salloc/sbatch/srun option processing
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010-2017 SchedMD LLC <https://www.schedmd.com>
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona1@llnl.gov>,
 *    Christopher J. Morrone <morrone2@llnl.gov>, et. al.
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

#ifndef _SLURM_OPT_H_
#define _SLURM_OPT_H_

#include <inttypes.h>
#include <stdbool.h>
#include <time.h>

#include "config.h"

#include "slurm/slurm.h"

#include "src/common/data.h"

#define DEFAULT_IMMEDIATE	1
#define DEFAULT_BELL_DELAY	10
#define SRUN_MAX_THREADS	60

typedef enum {BELL_NEVER, BELL_AFTER_DELAY, BELL_ALWAYS} bell_flag_t;

/*
 * getopt_long flags used by salloc/sbatch/srun
 * integers and *not* valid characters to avoid collision with getopt flags.
 *
 * Please keep this list alphabetical on the master branch.
 * On release branches, you must *not* add to the middle of the list. Doing so
 * would constitute and ABI breaking change. In such cases, please add to the
 * end of the list, then alphabetize it on the master branch only.
 */

enum {
	LONG_OPT_ENUM_START = 0x100,
	LONG_OPT_ACCEL_BIND,
	LONG_OPT_ACCTG_FREQ,
	LONG_OPT_ALLOC_NODELIST,
	LONG_OPT_ARGV,
	LONG_OPT_BATCH,
	LONG_OPT_BCAST,
	LONG_OPT_BCAST_EXCLUDE,
	LONG_OPT_BELL,
	LONG_OPT_BLRTS_IMAGE,
	LONG_OPT_BURST_BUFFER_FILE,
	LONG_OPT_BURST_BUFFER_SPEC,
	LONG_OPT_CLUSTER,
	LONG_OPT_CLUSTER_CONSTRAINT,
	LONG_OPT_COMPLETE_FLAG,
	LONG_OPT_COMMENT,
	LONG_OPT_COMPRESS,
	LONG_OPT_CONTAINER,
	LONG_OPT_CONTAINER_ID,
	LONG_OPT_CONTEXT,
	LONG_OPT_CONTIGUOUS,
	LONG_OPT_CORE,
	LONG_OPT_CORESPERSOCKET,
	LONG_OPT_CPU_BIND,
	LONG_OPT_CPU_FREQ,
	LONG_OPT_CPUS_PER_GPU,
	LONG_OPT_DEADLINE,
	LONG_OPT_DEBUGGER_TEST,
	LONG_OPT_DELAY_BOOT,
	LONG_OPT_ENVIRONMENT, /* only for data */
	LONG_OPT_EPILOG,
	LONG_OPT_EXACT,
	LONG_OPT_EXCLUSIVE,
	LONG_OPT_EXPORT,
	LONG_OPT_EXPORT_FILE,
	LONG_OPT_EXTRA,
	LONG_OPT_GET_USER_ENV,
	LONG_OPT_GID,
	LONG_OPT_GPU_BIND,
	LONG_OPT_GPU_FREQ,
	LONG_OPT_GPUS,
	LONG_OPT_GPUS_PER_NODE,
	LONG_OPT_GPUS_PER_SOCKET,
	LONG_OPT_GPUS_PER_TASK,
	LONG_OPT_GRES,
	LONG_OPT_GRES_FLAGS,
	LONG_OPT_HINT,
	LONG_OPT_IGNORE_PBS,
	LONG_OPT_INTERACTIVE,
	LONG_OPT_JOBID,
	LONG_OPT_KILL_INV_DEP,
	LONG_OPT_LAUNCH_CMD,
	LONG_OPT_LAUNCHER_OPTS,
	LONG_OPT_LINUX_IMAGE,
	LONG_OPT_MAIL_TYPE,
	LONG_OPT_MAIL_USER,
	LONG_OPT_MCS_LABEL,
	LONG_OPT_MEM,
	LONG_OPT_MEM_BIND,
	LONG_OPT_MEM_PER_CPU,
	LONG_OPT_MEM_PER_GPU,
	LONG_OPT_MINCORES,
	LONG_OPT_MINCPUS,
	LONG_OPT_MINSOCKETS,
	LONG_OPT_MINTHREADS,
	LONG_OPT_MLOADER_IMAGE,
	LONG_OPT_MPI,
	LONG_OPT_MSG_TIMEOUT,
	LONG_OPT_MULTI,
	LONG_OPT_NETWORK,
	LONG_OPT_NICE,
	LONG_OPT_NO_BELL,
	LONG_OPT_NO_REQUEUE,
	LONG_OPT_NO_SHELL,
	LONG_OPT_NTASKSPERCORE,
	LONG_OPT_NTASKSPERGPU,
	LONG_OPT_NTASKSPERNODE,
	LONG_OPT_NTASKSPERSOCKET,
	LONG_OPT_NTASKSPERTRES,
	LONG_OPT_OPEN_MODE,
	LONG_OPT_OVERLAP,
	LONG_OPT_HET_GROUP,
	LONG_OPT_PARSABLE,
	LONG_OPT_POWER,
	LONG_OPT_PREFER,
	LONG_OPT_PRIORITY,
	LONG_OPT_PROFILE,
	LONG_OPT_PROLOG,
	LONG_OPT_PROPAGATE,
	LONG_OPT_PTY,
	LONG_OPT_QUIT_ON_INTR,
	LONG_OPT_RAMDISK_IMAGE,
	LONG_OPT_REBOOT,
	LONG_OPT_REQUEUE,
	LONG_OPT_RESERVATION,
	LONG_OPT_RESV_PORTS,
	LONG_OPT_SEND_LIBS,
	LONG_OPT_SIGNAL,
	LONG_OPT_SLURMD_DEBUG,
	LONG_OPT_SOCKETSPERNODE,
	LONG_OPT_SPREAD_JOB,
	LONG_OPT_SWITCH_REQ,
	LONG_OPT_SWITCH_WAIT,
	LONG_OPT_SWITCHES,
	LONG_OPT_TASK_EPILOG,
	LONG_OPT_TASK_PROLOG,
	LONG_OPT_TEST_ONLY,
	LONG_OPT_THREAD_SPEC,
	LONG_OPT_THREADSPERCORE,
	LONG_OPT_TIME_MIN,
	LONG_OPT_TMP,
	LONG_OPT_TRES_PER_JOB,
	LONG_OPT_TRES_PER_TASK,
	LONG_OPT_UID,
	LONG_OPT_UMASK,
	LONG_OPT_USAGE,
	LONG_OPT_USE_MIN_NODES,
	LONG_OPT_WAIT_ALL_NODES,
	LONG_OPT_WCKEY,
	LONG_OPT_WHOLE,
	LONG_OPT_WRAP,
	LONG_OPT_X11,
	LONG_OPT_ENUM_END
};

/*
 * options only processed by salloc
 */
typedef struct {
	bell_flag_t bell;		/* --bell, --no-bell		*/
	int kill_command_signal;	/* --kill-command		*/
	bool no_shell;			/* --no-shell			*/
	uint16_t wait_all_nodes;	/* --wait-nodes-ready=val	*/
} salloc_opt_t;

/*
 * options only processed by sbatch
 */
typedef struct {
	char *array_inx;		/* --array			*/
	char *batch_features;		/* --batch			*/
	char *export_file;		/* --export-file=file		*/
	bool ignore_pbs;		/* --ignore-pbs			*/
	int minsockets;			/* --minsockets=n		*/
	int mincores;			/* --mincores=n			*/
	int minthreads;			/* --minthreads=n		*/
	bool parsable;			/* --parsable			*/
	char *propagate;		/* --propagate[=RLIMIT_CORE,...]*/
	int requeue;			/* --requeue and --no-requeue	*/
	bool test_only;			/* --test-only			*/
	int umask;			/* job umask for PBS		*/
	bool wait;			/* --wait			*/
	uint16_t wait_all_nodes;	/* --wait-nodes-ready=val	*/
	char *wrap;
} sbatch_opt_t;

typedef struct {
	char *placeholder;
} scron_opt_t;

/*
 * options only processed by srun
 */
typedef struct {
	uint16_t accel_bind_type;	/* --accel-bind			*/
	char *alloc_nodelist;		/* grabbed from the environment	*/
	char *bcast_exclude;		/* --bcast-exclude */
	char *bcast_file;		/* --bcast, copy executable to compute nodes */
	bool bcast_flag;		/* --bcast, copy executable to compute nodes */
	char *cmd_name;			/* name of command to execute	*/
	uint16_t compress;		/* --compress (for --bcast option) */
	bool core_spec_set;		/* core_spec explicitly set	*/
	char *cpu_bind;			/* binding map for map/mask_cpu	*/
	cpu_bind_type_t cpu_bind_type;	/* --cpu-bind			*/
	bool debugger_test;		/* --debugger-test		*/
	bool disable_status;		/* --disable-status		*/
	char *epilog;			/* --epilog			*/
	bool exact;			/* --exact			*/
	bool exclusive;			/* --exclusive			*/
	bool interactive;		/* --interactive		*/
	uint32_t jobid;			/* --jobid			*/
	uint32_t array_task_id;		/* --jobid			*/
	int32_t kill_bad_exit;		/* --kill-on-bad-exit		*/
	bool labelio;			/* --label-output		*/
	int32_t max_threads;		/* --threads			*/
	int max_wait;			/* --wait			*/
	int msg_timeout;		/* undocumented			*/
	char *mpi_type;			/* --mpi=type			*/
	bool multi_prog;		/* multiple programs to execute */
	int32_t multi_prog_cmds;	/* number of commands in multi prog file */
	bool no_alloc;			/* --no-allocate		*/
	bool overlap_force;		/* true if --overlap		*/
	char *het_group;		/* --het-group			*/
	bitstr_t *het_grp_bits;		/* --het-group in bitmap form	*/
	int het_step_cnt;		/* Total count of het groups to launch */
	bool parallel_debug;		/* srun controlled by debugger	*/
	bool preserve_env;		/* --preserve-env		*/
	char *prolog;			/* --prolog			*/
	char *propagate;		/* --propagate[=RLIMIT_CORE,...]*/
	char *pty;			/* --pty[=fd]			*/
	bool quit_on_intr;		/* --quit-on-interrupt		*/
	int relative;			/* --relative			*/
	int resv_port_cnt;		/* --resv_ports			*/
	bool send_libs;			/* --send-libs			*/
	int slurmd_debug;		/* --slurmd-debug		*/
	char *task_epilog;		/* --task-epilog		*/
	char *task_prolog;		/* --task-prolog		*/
	bool test_exec;			/* test_exec set		*/
	bool test_only;			/* --test-only			*/
	bool unbuffered;		/* --unbuffered			*/
	bool whole;			/* --whole			*/
} srun_opt_t;

typedef struct {
	bool set;			/* Has the option been set */
	bool set_by_env;		/* Has the option been set by env var */
	bool set_by_data;		/* Has the option been set by data_t */
} slurm_opt_state_t;

typedef struct {
	salloc_opt_t *salloc_opt;
	sbatch_opt_t *sbatch_opt;
	scron_opt_t *scron_opt;
	srun_opt_t *srun_opt;

	slurm_opt_state_t *state;

	void (*help_func)(void);	/* Print --help info		*/
	void (*usage_func)(void);	/* Print --usage info		*/
	void (*autocomplete_func)(const char *); /* Print --autocomplete= info*/

	int argc;			/* command/script argc		*/
	char **argv;			/* command/script argv		*/
	char *burst_buffer;		/* --bb				*/
	char *burst_buffer_file;	/* --bbf			*/
	char *clusters;			/* cluster to run this on. */
	uid_t uid;			/* local uid			*/
	gid_t gid;			/* local gid			*/
	char *chdir;			/* --chdir			*/
	int ntasks;			/* --ntasks			*/
	bool ntasks_set;		/* ntasks explicitly set	*/
	int cpus_per_task;		/* --cpus-per-task=n		*/
	bool cpus_set;			/* cpus_per_task explicitly set	*/
	int min_nodes;			/* --nodes=n			*/
	int max_nodes;			/* --nodes=x-n			*/
	char *job_size_str;		/* --nodes			*/
	bool nodes_set;			/* nodes explicitly set		*/
	int sockets_per_node;		/* --sockets-per-node=n		*/
	int cores_per_socket;		/* --cores-per-socket=n		*/
	uint32_t job_flags;		/* --kill_invalid_dep, --gres-flags */
	int threads_per_core;		/* --threads-per-core=n		*/
	int ntasks_per_node;		/* --ntasks-per-node=n		*/
	int ntasks_per_gpu;		/* --ntasks-per-gpu=n		*/
	int ntasks_per_socket;		/* --ntasks-per-socket=n	*/
	int ntasks_per_core;		/* --ntasks-per-core=n		*/
	int ntasks_per_tres;		/* --ntasks-per-gpu=n	*/
	char *hint;			/* --hint or SLURM_HINT envvar	*/
	mem_bind_type_t mem_bind_type;	/* --mem-bind=		*/
	char *mem_bind;			/* binding map for map/mask_mem	*/
	bool extra_set;			/* extra node info explicitly set */
	int time_limit;			/* --time, in minutes		*/
	int time_min;			/* --min-time, in minutes	*/
	char *partition;		/* --partition			*/
	uint32_t profile;		/* --profile=[all | none]	*/
	enum task_dist_states distribution;
					/* --distribution		*/
	uint32_t plane_size;		/* lllp distribution -> plane_size for
					 * when -m plane=<# of lllp per
					 * plane> */
	char *job_name;			/* --job-name			*/
	char *dependency;		/* --dependency			*/
	int nice;			/* --nice			*/
	uint32_t priority;		/* --priority			*/
	char *account;			/* --account			*/
	char *comment;			/* --comment			*/
	char *qos;			/* --qos			*/
	int immediate;			/* --immediate			*/
	uint16_t warn_flags;		/* --signal=flags:<int>@<time>	*/
	uint16_t warn_signal;		/* --signal=flags:<int>@<time>	*/
	uint16_t warn_time;		/* --signal=flags:<int>@<time>	*/

	bool hold;			/* --hold			*/
	bool no_kill;			/* --no-kill			*/
	char *acctg_freq;		/* --acctg-freq=<type1>=<freq1>,... */
	bool overcommit;		/* --overcommit			*/
	uint16_t shared;		/* --share			*/
	char *licenses;			/* --licenses			*/
	char *network;			/* --network			*/
	int quiet;
	int verbose;

	/* constraint options */
	int cpus_per_gpu;		/* --cpus-per-gpu		*/
	char *gpus;			/* --gpus			*/
	char *gpu_bind;			/* --gpu_bind			*/
	char *gpu_freq;			/* --gpu_freq			*/
	char *gpus_per_node;		/* --gpus_per_node		*/
	char *gpus_per_socket;		/* --gpus_per_socket		*/
	char *gpus_per_task;		/* --gpus_per_task		*/

	int pn_min_cpus;		/* --mincpus			*/
	uint64_t mem_per_cpu;		/* --mem-per-cpu		*/
	uint64_t mem_per_gpu;		/* --mem-per-gpu		*/
	uint64_t pn_min_memory;		/* --mem			*/
	uint64_t pn_min_tmp_disk;	/* --tmp			*/
	char *prefer;			/* --prefer			*/
	char *constraint;		/* --constraint			*/
	char *c_constraint;		/* --cluster-constraint		*/
	char *gres;			/* --gres			*/
	char *container;		/* --container			*/
	char *container_id;		/* --container-id		*/
	char *context;			/* --context			*/
	bool contiguous;		/* --contiguous			*/
	char *nodefile;			/* --nodefile			*/
	char *nodelist;			/* --nodelist=node1,node2,...	*/
	char **environment;		/* job environment     		*/
	char *exclude;			/* --exclude=node1,node2,...	*/

	bool reboot;			/* --reboot			*/

	time_t begin;			/* --begin			*/
	char *extra;			/* --extra			*/
	uint16_t mail_type;		/* --mail-type			*/
	char *mail_user;		/* --mail-user			*/
	int get_user_env_time;		/* --get-user-env[=timeout]	*/
	int get_user_env_mode;		/* --get-user-env=[S|L]		*/
	char *wckey;			/* workload characterization key */
	char *reservation;		/* --reservation		*/
	int req_switch;			/* min number of switches	*/
	int wait4switch;		/* max time to wait for min switches */
	char **spank_job_env;		/* SPANK controlled environment for job
					 * Prolog and Epilog		*/
	int spank_job_env_size;		/* size of spank_job_env	*/
	int core_spec;			/* --core-spec			*/
	uint32_t cpu_freq_min;		/* Minimum cpu frequency	*/
	uint32_t cpu_freq_max;		/* Maximum cpu frequency	*/
	uint32_t cpu_freq_gov;		/* cpu frequency governor	*/
	uint8_t power;			/* power management flags	*/
	char *mcs_label;		/* mcs label			*/
	time_t deadline;		/* ---deadline			*/
	uint32_t delay_boot;		/* --delay-boot			*/
	uint32_t step_het_comp_cnt;     /* How many components are in this het
					 * step that is part of a non-hetjob. */
	char *step_het_grps;		/* what het groups are used by step */
	char *submit_line;		/* submit line of the caller	*/
	char *tres_bind;		/* derived from gpu_bind	*/
	char *tres_freq;		/* derived from gpu_freq	*/
	char *tres_per_task;		/* --tres_per_task		*/

	uint16_t x11;			/* --x11			*/
	char *x11_magic_cookie;		/* cookie retrieved from xauth	*/
	char *x11_target;		/* target host, or unix socket	*/
					/* if x11_target_port == 0	*/
	uint16_t x11_target_port;	/* target display TCP port on localhost */

	/* used in both sbatch and srun, here for convenience */
	uint8_t open_mode;		/* --open-mode=append|truncate	*/
	char *export_env;		/* --export			*/
	char *efname;			/* error file name		*/
	char *ifname;			/* input file name		*/
	char *ofname;			/* output file name		*/

} slurm_opt_t;

extern struct option *slurm_option_table_create(slurm_opt_t *opt,
						char **opt_string);
extern void slurm_option_table_destroy(struct option *optz);

/*
 * Process individual argument for the current job component
 * IN opt - current component
 * IN optval - argument identifier
 * IN arg - argument value
 * IN set_by_env - flag if set by environment (and not cli)
 * IN early_pass - early vs. late pass for HetJob option inheritance
 * RET SLURM_SUCCESS or error
 */
extern int slurm_process_option(slurm_opt_t *opt, int optval, const char *arg,
				bool set_by_env, bool early_pass);

/*
 * Use slurm_process_option and call exit(-1) in case of non-zero return code
 */
extern void slurm_process_option_or_exit(slurm_opt_t *opt, int optval,
					 const char *arg, bool set_by_env,
					 bool early_pass);

/*
 * Process incoming single component of Job data entry
 * IN opt - options to populate from job chunk
 * IN job - data containing job request
 * IN/OUT errors - data dictionary to populate with detailed errors
 * RET SLURM_SUCCESS or error
 */
extern int slurm_process_option_data(slurm_opt_t *opt, int optval,
				     const data_t *arg, data_t *errors);

/*
 * Print all options that have been set through slurm_process_option()
 * in a form suitable for use with the -v flag to salloc/sbatch/srun.
 */
extern void slurm_print_set_options(slurm_opt_t *opt);

/*
 * Reset slurm_opt_t settings for a given pass.
 */
extern void slurm_reset_all_options(slurm_opt_t *opt, bool first_pass);

/*
 * Free all memory associated with opt members
 * Note: assumes that opt, opt->salloc_opt, opt->sbatch_opt, opt->scron_opt,
 * and opt->srun_opt should not be xfreed.
 */
extern void slurm_free_options_members(slurm_opt_t *opt);

/*
 * Was the option set by a cli argument?
 */
extern bool slurm_option_set_by_cli(slurm_opt_t *opt, int optval);

/*
 * Was the option set by an env var?
 */
extern bool slurm_option_set_by_env(slurm_opt_t *opt, int optval);

/*
 * Was the option set by an data_t value?
 */
extern bool slurm_option_set_by_env(slurm_opt_t *opt, int optval);

/*
 * Get option value by common option name.
 */
extern char *slurm_option_get(slurm_opt_t *opt, const char *name);

/*
 * Is option set? Discover by common option name.
 */
extern bool slurm_option_isset(slurm_opt_t *opt, const char *name);

/*
 * Replace option value by common option name.
 */
extern int slurm_option_set(slurm_opt_t *opt, const char *name,
			    const char *value, bool early);

/*
 * Reset option by common option name.
 */
extern bool slurm_option_reset(slurm_opt_t *opt, const char *name);

/*
 * Function for iterating through all the common option data structure
 * and returning (via parameter arguments) the name and value of each
 * set slurm option.
 *
 * IN opt	- option data structure being interpreted
 * OUT name	- xmalloc()'d string with the option name
 * OUT value	- xmalloc()'d string with the option value
 * IN/OUT state	- internal state, should be set to 0 for the first call
 * RETURNS      - true if name/value set; false if no more options
 */
extern bool slurm_option_get_next_set(slurm_opt_t *opt, char **name,
				      char **value, size_t *state);

/*
 * Validate that conflicting optons (--hint, --ntasks-per-core,
 * --nthreads-per-core, --cpu-bind [for srun]) are not used together.
 *
 */
extern int validate_hint_option(slurm_opt_t *opt);

/*
 * Validate --threads-per-core option and set --cpu-bind=threads if
 * not already set by user.
 */
extern int validate_threads_per_core_option(slurm_opt_t *opt);

/*
 * Validate options that are common to salloc, sbatch, and srun.
 */
extern void validate_options_salloc_sbatch_srun(slurm_opt_t *opt);

/*
 * Validate that two spec cores options (-S/--core-spec and --thread-spec)
 * are not used together.
 *
 * This function follows approach of validate_memory_options.
 */
extern void validate_spec_cores_options(slurm_opt_t *opt);

/*
 * Return the argv options in a string.
 */
extern char *slurm_option_get_argv_str(const int argc, char **argv);

/*
 * Return a job_desc_msg_t based on slurm_opt_t.
 * IN set_defaults - If true, sets default values for struct members. If false,
 *   all values will be their no value state (either NULL or NO_VAL equiv).
 */
extern job_desc_msg_t *slurm_opt_create_job_desc(slurm_opt_t *opt_local,
						 bool set_defaults);

/*
 * Compatible with shell/bash completions.
 */
extern void suggest_completion(struct option *opts, const char *query);

#endif	/* _SLURM_OPT_H_ */
