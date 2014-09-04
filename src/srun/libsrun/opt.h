/*****************************************************************************\
 *  opt.h - definitions for srun option processing
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona1@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
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

#ifndef _HAVE_OPT_H
#define _HAVE_OPT_H

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#ifndef SYSTEM_DIMENSIONS
#  define SYSTEM_DIMENSIONS 1
#endif

#include <time.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/common/macros.h" /* true and false */
#include "src/common/env.h"

#include "fname.h"

#define DEFAULT_IMMEDIATE	1
#define MAX_THREADS		60

#define INT_UNASSIGNED ((int)-1)

/* global variables relating to user options */
extern int _verbose;

extern enum modes mode;

typedef struct srun_options {

	char *progname;		/* argv[0] of this program or
				 * configuration file if multi_prog */
	bool multi_prog;	/* multiple programs to execute */
	int32_t multi_prog_cmds; /* number of commands in multi prog file */
	char *user;		/* local username		*/
	uid_t uid;		/* local uid			*/
	gid_t gid;		/* local gid			*/
	uid_t euid;		/* effective user --uid=user	*/
	gid_t egid;		/* effective group --gid=group	*/
	char *cwd;		/* current working directory	*/
	bool cwd_set;		/* true if cwd is explicitly set */

	int  ntasks;		/* --ntasks=n,      -n n	*/
	bool ntasks_set;	/* true if ntasks explicitly set */
	int  cpus_per_task;	/* --cpus-per-task=n, -c n	*/
	bool cpus_set;		/* true if cpus_per_task explicitly set */
	int32_t max_threads;	/* --threads, -T (threads in srun) */
	int32_t min_nodes;	/* --nodes=n,       -N n	*/
	int32_t max_nodes;	/* --nodes=x-n,       -N x-n	*/
	int32_t sockets_per_node; /* --sockets-per-node=n      */
	int32_t cores_per_socket; /* --cores-per-socket=n      */
	int32_t threads_per_core; /* --threads-per-core=n      */
	int32_t ntasks_per_node;   /* --ntasks-per-node=n	*/
	int32_t ntasks_per_socket; /* --ntasks-per-socket=n	*/
	int ntasks_per_core;	/* --ntasks-per-core=n		*/
	cpu_bind_type_t cpu_bind_type; /* --cpu_bind=           */
	char *cpu_bind;		/* binding map for map/mask_cpu */
	mem_bind_type_t mem_bind_type; /* --mem_bind=		*/
	char *mem_bind;		/* binding map for map/mask_mem	*/
	bool nodes_set;		/* true if nodes explicitly set */
	bool nodes_set_env;	/* true if nodes set via SLURM_NNODES */
	bool nodes_set_opt;	/* true if nodes explicitly set using
				 * command line option */
	bool extra_set;		/* true if extra node info explicitly set */
	int  time_limit;	/* --time,   -t	(int minutes)	*/
	char *time_limit_str;	/* --time,   -t (string)	*/
	int  time_min;		/* --min-time   (int minutes)	*/
	char *time_min_str;	/* --min-time (string)		*/
	int  ckpt_interval;	/* --checkpoint (int minutes)	*/
	char *ckpt_interval_str;/* --checkpoint (string)	*/
	char *ckpt_dir;  	/* --checkpoint-dir (string)   */
	bool exclusive;		/* --exclusive			*/
	int  resv_port_cnt;	/* --resv_ports			*/
	char *partition;	/* --partition=n,   -p n   	*/
	enum task_dist_states
	        distribution;	/* --distribution=, -m dist	*/
        uint32_t plane_size;    /* lllp distribution -> plane_size for
				 * when -m plane=<# of lllp per
				 * plane> */
	char *cmd_name;		/* name of command to execute	*/
	char *job_name;		/* --job-name=,     -J name	*/
	bool job_name_set_cmd;	/* true if job_name set by cmd line option */
	bool job_name_set_env;	/* true if job_name set by env var */
	unsigned int jobid;     /* --jobid=jobid                */
	bool jobid_set;		/* true if jobid explicitly set */
	char *mpi_type;		/* --mpi=type			*/
	char *dependency;	/* --dependency, -P type:jobid	*/
	int nice;		/* --nice			*/
	uint32_t priority;	/* --priority */
	char *account;		/* --account, -U acct_name	*/
	char *comment;		/* --comment			*/
	char *qos;		/* --qos			*/
	char *ofname;		/* --output -o filename         */
	char *ifname;		/* --input  -i filename         */
	char *efname;		/* --error, -e filename         */

	int  slurmd_debug;	/* --slurmd-debug, -D           */
	bool join;		/* --join, 	    -j		*/

	/* no longer need these, they are set globally : 	*/
	/*int verbose;*/	/* -v, --verbose		*/
	/*int debug;*/		/* -d, --debug			*/

	int immediate;		/* -I, --immediate=secs      	*/
	uint16_t warn_flags;	/* --signal=flags:<int>@<time>	*/
	uint16_t warn_signal;	/* --signal=flags:<int>@<time>	*/
	uint16_t warn_time;	/* --signal=flags:<int>@<time>	*/

	bool hold;		/* --hold, -H			*/
	char *hostfile;         /* location of hostfile if there is one */
	bool labelio;		/* --label-output, -l		*/
	bool unbuffered;        /* --unbuffered,   -u           */
	bool allocate;		/* --allocate, 	   -A		*/
	bool noshell;		/* --no-shell                   */
	bool overcommit;	/* --overcommit,   -O		*/
	bool no_kill;		/* --no-kill, -k		*/
	int32_t kill_bad_exit;	/* --kill-on-bad-exit, -K	*/
	uint16_t shared;	/* --share,   -s		*/
	int  max_wait;		/* --wait,    -W		*/
	bool quit_on_intr;      /* --quit-on-interrupt, -q      */
	bool disable_status;    /* --disable-status, -X         */
	int  quiet;
	bool parallel_debug;	/* srun controlled by debugger	*/
	bool debugger_test;	/* --debugger-test		*/
	bool test_only;		/* --test-only			*/
	uint32_t profile;	/* --profile=[all | none}       */
	char *propagate;	/* --propagate[=RLIMIT_CORE,...]*/
	char *task_epilog;	/* --task-epilog=		*/
	char *task_prolog;	/* --task-prolog=		*/
	char *licenses;		/* --licenses, -L		*/
	bool preserve_env;	/* --preserve-env		*/
	char *export_env;	/* --export			*/

	/* constraint options */
	int32_t pn_min_cpus;	/* --mincpus=n			*/
	int32_t pn_min_memory;	/* --mem=n			*/
	int32_t mem_per_cpu;	/* --mem-per-cpu=n		*/
	long pn_min_tmp_disk;	/* --tmp=n			*/
	char *constraints;	/* --constraints=, -C constraint*/
	char *gres;		/* --gres=			*/
	bool contiguous;	/* --contiguous			*/
	char *nodelist;		/* --nodelist=node1,node2,...	*/
	char *alloc_nodelist;   /* grabbed from the environment */
	char *exc_nodes;	/* --exclude=node1,node2,... -x	*/
	int  relative;		/* --relative -r N              */
	bool relative_set;
	bool no_alloc;		/* --no-allocate, -Z		*/
	int  max_launch_time;   /* Undocumented                 */
	int  max_exit_timeout;  /* Undocumented                 */
	int  msg_timeout;       /* Undocumented                 */
	bool launch_cmd;        /* --launch_cmd                 */
	char *launcher_opts;	/* --launcher-opts commands to be sent
				 * to the external launcher command if
				 * not SLURM */
	char *network;		/* --network=			*/
	bool network_set_env;	/* true if network set by env var */

	/* BLUEGENE SPECIFIC */
	uint16_t geometry[HIGHEST_DIMENSIONS]; /* --geometry, -g */
	bool reboot;		/* --reboot			*/
	bool no_rotate;		/* --no_rotate, -R		*/
	uint16_t conn_type[HIGHEST_DIMENSIONS];	/* --conn-type 	*/
	char *blrtsimage;       /* --blrtsimage BlrtsImage for block */
	char *linuximage;       /* --linuximage LinuxImage for block */
	char *mloaderimage;     /* --mloaderimage mloaderImage for block */
	char *ramdiskimage;     /* --ramdiskimage RamDiskImage for block */
	/*********************/

	char *prolog;           /* --prolog                     */
	char *epilog;           /* --epilog                     */
	time_t begin;		/* --begin			*/
	uint16_t mail_type;	/* --mail-type			*/
	char *mail_user;	/* --mail-user			*/
	uint8_t open_mode;	/* --open-mode=append|truncate	*/
	char *acctg_freq;	/* --acctg-freq=<type1>=<freq1>,*/
				/* 	<type2>=<freq2>,...	*/
	uint32_t cpu_freq;     	/* --cpu_freq=kilohertz		*/
	bool pty;		/* --pty			*/
	char *restart_dir;	/* --restart                    */
	int argc;		/* length of argv array		*/
	char **argv;		/* left over on command line	*/
	char *wckey;            /* --wckey workload characterization key */
	char *reservation;      /* --reservation		*/
	char **spank_job_env;	/* SPANK controlled environment for job
				 * Prolog and Epilog		*/
	int spank_job_env_size;	/* size of spank_job_env	*/
	int req_switch;		/* Minimum number of switches	*/
	int wait4switch;	/* Maximum time to wait for minimum switches */
	bool user_managed_io;   /* 0 for "normal" IO, 1 for "user manged" IO */
	int core_spec;		/* --core-spec=n,      -S n	*/
	bool core_spec_set;	/* true if core_spec explicitly set */
} opt_t;

extern opt_t opt;

extern int error_exit;		/* exit code for slurm errors */
extern int immediate_exit;	/* exit code for --imediate option & busy */
extern bool srun_max_timer;
extern bool srun_shutdown;
extern int sig_array[];
extern resource_allocation_response_msg_t *global_resp;

/* return whether any constraints were specified by the user
 * (if new constraints are added above, might want to add them to this
 *  macro or move this to a function if it gets a little complicated)
 */
#define constraints_given() ((opt.pn_min_cpus     != NO_VAL) || \
			     (opt.pn_min_memory   != NO_VAL) || \
			     (opt.job_max_memory   != NO_VAL) || \
			     (opt.pn_min_tmp_disk != NO_VAL) || \
			     (opt.pn_min_sockets  != NO_VAL) || \
			     (opt.pn_min_cores    != NO_VAL) || \
			     (opt.pn_min_threads  != NO_VAL) || \
			     (opt.contiguous))

/* process options:
 * 1. set defaults
 * 2. update options with env vars
 * 3. update options with commandline args
 * 4. perform some verification that options are reasonable
 */
int initialize_and_process_args(int argc, char *argv[]);

/* external functions available for SPANK plugins to modify the environment
 * exported to the SLURM Prolog and Epilog programs */
extern char *spank_get_job_env(const char *name);
extern int   spank_set_job_env(const char *name, const char *value,
			       int overwrite);
extern int   spank_unset_job_env(const char *name);

/* Initialize the spank_job_env based upon environment variables set
 *	via salloc or sbatch commands */
extern void init_spank_env(void);

#endif	/* _HAVE_OPT_H */
