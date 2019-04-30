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
	LONG_OPT_BATCH,
	LONG_OPT_BCAST,
	LONG_OPT_BELL,
	LONG_OPT_BLRTS_IMAGE,
	LONG_OPT_BURST_BUFFER_FILE,
	LONG_OPT_BURST_BUFFER_SPEC,
	LONG_OPT_CHECKPOINT,
	LONG_OPT_CHECKPOINT_DIR,
	LONG_OPT_CLUSTER,
	LONG_OPT_CLUSTER_CONSTRAINT,
	LONG_OPT_COMMENT,
	LONG_OPT_COMPRESS,
	LONG_OPT_CONTIGUOUS,
	LONG_OPT_CORE,
	LONG_OPT_CORESPERSOCKET,
	LONG_OPT_CPU_BIND,
	LONG_OPT_CPU_FREQ,
	LONG_OPT_CPUS_PER_GPU,
	LONG_OPT_DEADLINE,
	LONG_OPT_DEBUGGER_TEST,
	LONG_OPT_DELAY_BOOT,
	LONG_OPT_EPILOG,
	LONG_OPT_EXCLUSIVE,
	LONG_OPT_EXPORT,
	LONG_OPT_EXPORT_FILE,
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
	LONG_OPT_NTASKSPERNODE,
	LONG_OPT_NTASKSPERSOCKET,
	LONG_OPT_OPEN_MODE,
	LONG_OPT_PACK_GROUP,
	LONG_OPT_PARSABLE,
	LONG_OPT_POWER,
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
	LONG_OPT_RESTART_DIR,
	LONG_OPT_RESV_PORTS,
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
	LONG_OPT_UID,
	LONG_OPT_UMASK,
	LONG_OPT_USAGE,
	LONG_OPT_USE_MIN_NODES,
	LONG_OPT_WAIT_ALL_NODES,
	LONG_OPT_WCKEY,
	LONG_OPT_WRAP,
	LONG_OPT_X11,
	LONG_OPT_ENUM_END
};

/*
 * options only processed by salloc
 */
typedef struct salloc_opt {
	bell_flag_t bell;		/* --bell, --no-bell		*/
	int kill_command_signal;	/* --kill-command		*/
	bool no_shell;			/* --no-shell			*/
	uint16_t wait_all_nodes;	/* --wait-nodes-ready=val	*/
} salloc_opt_t;

/*
 * options only processed by sbatch
 */
typedef struct sbatch_opt {
	/* batch script argv and argc, if provided on the command line */
	int script_argc;
	char **script_argv;

	char *array_inx;		/* --array			*/
	char *batch_features;		/* --batch			*/
	int ckpt_interval;		/* --checkpoint (int minutes)	*/
	char *export_env;		/* --export			*/
	char *export_file;		/* --export-file=file		*/
	bool ignore_pbs;		/* --ignore-pbs			*/
	int minsockets;			/* --minsockets=n		*/
	int mincores;			/* --mincores=n			*/
	int minthreads;			/* --minthreads=n		*/
	bool parsable;			/* --parsable			*/
	char *propagate;		/* --propagate[=RLIMIT_CORE,...]*/
	uint8_t open_mode;		/* --open-mode			*/
	int requeue;			/* --requeue and --no-requeue	*/
	bool test_only;			/* --test-only			*/
	int umask;			/* job umask for PBS		*/
	bool wait;			/* --wait			*/
	uint16_t wait_all_nodes;	/* --wait-nodes-ready=val	*/
	char *wrap;
} sbatch_opt_t;

/*
 * options only processed by srun
 */
typedef struct srun_opt {
	int argc;			/* length of argv array		*/
	char **argv;			/* left over on command line	*/

	char *ifname;			/* input file name		*/
	char *ofname;			/* output file name		*/
	char *efname;			/* error file name		*/

	uint16_t accel_bind_type;	/* --accel-bind			*/
	char *alloc_nodelist;		/* grabbed from the environment	*/
	char *bcast_file;		/* --bcast, copy executable to compute nodes */
	bool bcast_flag;		/* --bcast, copy executable to compute nodes */
	int ckpt_interval;		/* --checkpoint, in minutes	*/
	char *cmd_name;			/* name of command to execute	*/
	uint16_t compress;		/* --compress (for --bcast option) */
	bool core_spec_set;		/* core_spec explicitly set	*/
	char *cpu_bind;			/* binding map for map/mask_cpu	*/
	cpu_bind_type_t cpu_bind_type;	/* --cpu-bind			*/
	bool debugger_test;		/* --debugger-test		*/
	bool disable_status;		/* --disable-status		*/
	char *epilog;			/* --epilog			*/
	bool exclusive;			/* --exclusive			*/
	char *export_env;		/* --export			*/
	uint32_t jobid;			/* --jobid			*/
	int32_t kill_bad_exit;		/* --kill-on-bad-exit		*/
	bool labelio;			/* --label-output		*/
	int32_t max_threads;		/* --threads			*/
	int max_wait;			/* --wait			*/
	int msg_timeout;		/* undocumented			*/
	char *mpi_type;			/* --mpi=type			*/
	bool multi_prog;		/* multiple programs to execute */
	int32_t multi_prog_cmds;	/* number of commands in multi prog file */
	bool no_alloc;			/* --no-allocate		*/
	uint8_t open_mode;		/* --open-mode=append|truncate	*/
	char *pack_group;		/* --pack-group			*/
	bitstr_t *pack_grp_bits;	/* --pack-group in bitmap form	*/
	int pack_step_cnt;		/* Total count of pack groups to launch */
	bool parallel_debug;		/* srun controlled by debugger	*/
	bool preserve_env;		/* --preserve-env		*/
	char *prolog;			/* --prolog			*/
	char *propagate;		/* --propagate[=RLIMIT_CORE,...]*/
	bool pty;			/* --pty			*/
	bool quit_on_intr;		/* --quit-on-interrupt		*/
	int relative;			/* --relative			*/
	char *restart_dir;		/* --restart			*/
	int resv_port_cnt;		/* --resv_ports			*/
	int slurmd_debug;		/* --slurmd-debug		*/
	char *task_epilog;		/* --task-epilog		*/
	char *task_prolog;		/* --task-prolog		*/
	bool test_exec;			/* test_exec set		*/
	bool test_only;			/* --test-only			*/
	bool unbuffered;		/* --unbuffered			*/
} srun_opt_t;

typedef struct slurm_options {
	salloc_opt_t *salloc_opt;
	sbatch_opt_t *sbatch_opt;
	srun_opt_t *srun_opt;

	void (*help_func)(void);	/* Print --help info		*/
	void (*usage_func)(void);	/* Print --usage info		*/

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
	bool nodes_set;			/* nodes explicitly set		*/
	int sockets_per_node;		/* --sockets-per-node=n		*/
	int cores_per_socket;		/* --cores-per-socket=n		*/
	uint32_t job_flags;		/* --kill_invalid_dep, --gres-flags */
	int threads_per_core;		/* --threads-per-core=n		*/
	int ntasks_per_node;		/* --ntasks-per-node=n		*/
	int ntasks_per_socket;		/* --ntasks-per-socket=n	*/
	int ntasks_per_core;		/* --ntasks-per-core=n		*/
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
	char *constraint;		/* --constraint			*/
	char *c_constraint;		/* --cluster-constraint		*/
	char *gres;			/* --gres			*/
	bool contiguous;		/* --contiguous			*/
	char *nodefile;			/* --nodefile			*/
	char *nodelist;			/* --nodelist=node1,node2,...	*/
	char *exclude;			/* --exclude=node1,node2,...	*/

	bool reboot;			/* --reboot			*/

	time_t begin;			/* --begin			*/
	char *extra;			/* unused			*/
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
	char *tres_bind;		/* derived from gpu_bind	*/
	char *tres_freq;		/* derived from gpu_freq	*/
	uint16_t x11;			/* --x11			*/
	char *x11_magic_cookie;		/* cookie retrieved from xauth	*/
	char *x11_target;		/* target host, or unix socket	*/
					/* if x11_target_port == 0	*/
	uint16_t x11_target_port;	/* target display TCP port on localhost */

	/* used in both sbatch and srun, here for convenience */
	char *efname;			/* error file name		*/
	char *ifname;			/* input file name		*/
	char *ofname;			/* output file name		*/
} slurm_opt_t;

extern struct option *slurm_option_table_create(slurm_opt_t *opt,
						char **opt_string);
extern void slurm_option_table_destroy(struct option *optz);

/*
 * Warning: this will permute the state of a global common_options table,
 * and thus is not thread-safe. The expectation is that it is called from
 * within a single thread in salloc/sbatch/srun, and that this restriction
 * should not be problematic. If it is, please refactor.
 */
extern int slurm_process_option(slurm_opt_t *opt, int optval, const char *arg,
				bool set_by_env, bool early_pass);

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
 * Was the option set by a cli argument?
 */
extern bool slurm_option_set_by_cli(int optval);

/*
 * Was the option set by an env var?
 */
extern bool slurm_option_set_by_env(int optval);

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

#endif	/* _SLURM_OPT_H_ */
