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

#ifndef SYSTEM_DIMENSIONS
#define SYSTEM_DIMENSIONS 1
#endif

#define DEFAULT_IMMEDIATE	1
#define DEFAULT_BELL_DELAY	10

typedef enum {BELL_NEVER, BELL_AFTER_DELAY, BELL_ALWAYS} bell_flag_t;

/*
 * options only processed by salloc
 */
typedef struct salloc_opt {
	bell_flag_t bell;		/* --bell, --no-bell		*/
	bool default_job_name;		/* set if no command or job name specified */
	int kill_command_signal;	/* --kill-command		*/
	bool kill_command_signal_set;
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

	char *ifname;			/* input file name		*/
	char *ofname;			/* output file name		*/
	char *efname;			/* error file name		*/

	char *array_inx;		/* --array			*/
	char *batch_features;		/* --batch			*/
	char *burst_buffer_file;	/* --bbf			*/
	int ckpt_interval;		/* --checkpoint (int minutes)	*/
	char *ckpt_interval_str;	/* --checkpoint			*/
	char *ckpt_dir;			/* --checkpoint-dir		*/
	char *export_env;		/* --export			*/
	char *export_file;		/* --export-file=file		*/
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
	bool allocate;			/* --allocate			*/
	char *alloc_nodelist;		/* grabbed from the environment	*/
	char *bcast_file;		/* --bcast, copy executable to compute nodes */
	bool bcast_flag;		/* --bcast, copy executable to compute nodes */
	int ckpt_interval;		/* --checkpoint, in minutes	*/
	char *ckpt_interval_str;	/* --checkpoint			*/
	char *ckpt_dir;			/* --checkpoint-dir		*/
	char *cmd_name;			/* name of command to execute	*/
	uint16_t compress;		/* --compress (for --bcast option) */
	bool core_spec_set;		/* core_spec explicitly set	*/
	char *cpu_bind;			/* binding map for map/mask_cpu	*/
	cpu_bind_type_t cpu_bind_type;	/* --cpu-bind			*/
	bool cpu_bind_type_set;		/* --cpu-bind explicitly set	*/
	bool cwd_set;			/* --cwd explicitly set		*/
	bool debugger_test;		/* --debugger-test		*/
	bool disable_status;		/* --disable-status		*/
	char *epilog;			/* --epilog			*/
	bool exclusive;			/* --exclusive			*/
	char *export_env;		/* --export			*/
	char *hostfile;			/* location of hostfile if there is one */
	bool job_name_set_cmd;		/* true if job_name set by cmd line option */
	bool job_name_set_env;		/* true if job_name set by env var */
	int32_t kill_bad_exit;		/* --kill-on-bad-exit		*/
	bool labelio;			/* --label-output		*/
	bool launch_cmd;		/* --launch_cmd			*/
	char *launcher_opts;		/* --launcher-opts commands to be sent
					 * to the external launcher command if
					 * not Slurm */
	int32_t max_threads;		/* --threads			*/
	int max_wait;			/* --wait			*/
	int msg_timeout;		/* undocumented			*/
	bool multi_prog;		/* multiple programs to execute */
	int32_t multi_prog_cmds;	/* number of commands in multi prog file */
	bool network_set_env;		/* true if network set by env var */
	bool no_alloc;			/* --no-allocate		*/
	bool nodes_set_env;		/* true if nodes set via SLURM_NNODES */
	bool nodes_set_opt;		/* true if nodes explicitly set using
					   command line option */
	bool noshell;			/* --no-shell			*/
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
	bool relative_set;
	char *restart_dir;		/* --restart			*/
	int resv_port_cnt;		/* --resv_ports			*/
	int slurmd_debug;		/* --slurmd-debug		*/
	char *task_epilog;		/* --task-epilog		*/
	char *task_prolog;		/* --task-prolog		*/
	bool test_exec;			/* test_exec set		*/
	bool test_only;			/* --test-only			*/
	bool unbuffered;		/* --unbuffered			*/
	bool user_managed_io;		/* 0 for "normal" IO,		*/
					/* 1 for "user manged" IO	*/
} srun_opt_t;

typedef struct slurm_options {
	salloc_opt_t *salloc_opt;
	sbatch_opt_t *sbatch_opt;
	srun_opt_t *srun_opt;

	char *progname;			/* argv[0] of this program or	*/

	char *burst_buffer;		/* --bb				*/
	char *clusters;			/* cluster to run this on. */
	char *user;			/* local username		*/
	uid_t uid;			/* local uid			*/
	gid_t gid;			/* local gid			*/
	uid_t euid;			/* effective user --uid=user	*/
	gid_t egid;			/* effective group --gid=group	*/
	char *cwd;			/* current working directory	*/
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
	bool threads_per_core_set;	/* --threads-per-core explicitly set */
	int ntasks_per_node;		/* --ntasks-per-node=n		*/
	int ntasks_per_socket;		/* --ntasks-per-socket=n	*/
	int ntasks_per_core;		/* --ntasks-per-core=n		*/
	bool ntasks_per_core_set;	/* ntasks-per-core explicitly set */
	char *hint_env;			/* SLURM_HINT env var setting	*/
	bool hint_set;			/* --hint set explicitly set	*/
	mem_bind_type_t mem_bind_type;	/* --mem-bind=		*/
	char *mem_bind;			/* binding map for map/mask_mem	*/
	bool extra_set;			/* extra node info explicitly set */
	int time_limit;			/* --time, in minutes		*/
	char *time_limit_str;		/* --time			*/
	int time_min;			/* --min-time, in minutes	*/
	char *time_min_str;		/* --min-time			*/
	char *partition;		/* --partition			*/
	uint32_t profile;		/* --profile=[all | none]	*/
	enum task_dist_states distribution;
					/* --distribution		*/
	uint32_t plane_size;		/* lllp distribution -> plane_size for
					 * when -m plane=<# of lllp per
					 * plane> */
	char *job_name;			/* --job-name			*/
	uint32_t jobid;			/* --jobid			*/
	bool jobid_set;			/* jobid explicitly set		*/
	char *mpi_type;			/* --mpi=type			*/
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
	int64_t mem_per_cpu;		/* --mem-per-cpu		*/
	int64_t mem_per_gpu;		/* --mem-per-gpu		*/
	int64_t pn_min_memory;		/* --mem			*/
	long pn_min_tmp_disk;		/* --tmp			*/
	char *constraints;		/* --constraints		*/
	char *c_constraints;		/* --cluster-constraints	*/
	char *gres;			/* --gres			*/
	bool contiguous;		/* --contiguous			*/
	char *nodelist;			/* --nodelist=node1,node2,...	*/
	char *exc_nodes;		/* --exclude=node1,node2,...	*/

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
	uint8_t power_flags;		/* Power management options	*/
	char *mcs_label;		/* mcs label			*/
	time_t deadline;		/* ---deadline			*/
	uint32_t delay_boot;		/* --delay-boot			*/
	char *tres_bind;		/* derived from gpu_bind	*/
	char *tres_freq;		/* derived from gpu_freq	*/
	uint16_t x11;			/* --x11			*/
	char *x11_magic_cookie;		/* cookie retrieved from xauth	*/
	/* no x11_target_host here, alloc_host will be equivalent */
	uint16_t x11_target_port;	/* target display TCP port on localhost */
} slurm_opt_t;

#endif	/* _SLURM_OPT_H_ */
