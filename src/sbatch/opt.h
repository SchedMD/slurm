/*****************************************************************************\
 *  opt.h - definitions for srun option processing
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona1@llnl.gov>,
 *    Christopher J. Morrone <morrone2@llnl.gov>, et. al.
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

#include <time.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/common/macros.h" /* true and false */
#include "src/common/env.h"
#include "src/common/slurmdb_defs.h"

#ifndef SYSTEM_DIMENSIONS
#  define SYSTEM_DIMENSIONS 1
#endif


typedef struct sbatch_options {
	List clusters; /* cluster to run this on. */
	char *progname;		/* argv[0] of this program or   */

	/* batch script argv and argc, if provided on the command line */
	int script_argc;
	char **script_argv;

	char *user;		/* local username		*/
	uid_t uid;		/* local uid			*/
	gid_t gid;		/* local gid			*/
	uid_t euid;		/* effective user --uid=user	*/
	gid_t egid;		/* effective group --gid=group	*/
	char *cwd;		/* current working directory	*/

	int  ntasks;		/* --ntasks=n,      -n n	*/
	bool ntasks_set;	/* true if ntasks explicitly set */
	int  cpus_per_task;	/* --cpus-per-task=n, -c n	*/
	bool cpus_set;		/* true if cpus_per_task explicitly set */
	int  min_nodes;		/* --nodes=n,       -N n	*/
	int  max_nodes;		/* --nodes=x-n,       -N x-n	*/
	bool nodes_set;		/* true if nodes explicitly set */
	int sockets_per_node;	/* --sockets-per-node=n		*/
	int cores_per_socket;	/* --cores-per-socket=n		*/
	int threads_per_core;	/* --threads-per-core=n		*/
	int ntasks_per_node;	/* --ntasks-per-node=n		*/
	int ntasks_per_socket;	/* --ntasks-per-socket=n	*/
	int ntasks_per_core;	/* --ntasks-per-core=n		*/
	mem_bind_type_t mem_bind_type; /* --mem_bind=		*/
	char *mem_bind;		/* binding map for map/mask_mem	*/
	bool extra_set;		/* true if extra node info explicitly set */
	int  time_limit;	/* --time,   -t	(int minutes)	*/
	char *time_limit_str;	/* --time,   -t (string)	*/
	int  time_min;		/* --min-time 	(int minutes)	*/
	char *time_min_str;	/* --min-time (string)		*/
	char *partition;	/* --partition=n,   -p n   	*/
	uint32_t profile;	/* --profile=[all | none}       */
	enum task_dist_states
	        distribution;	/* --distribution=, -m dist	*/
        uint32_t plane_size;    /* lllp distribution -> plane_size for
				 * when -m plane=<# of lllp per
				 * plane> */
	char *job_name;		/* --job-name=,     -J name	*/
	unsigned int jobid;     /* --jobid=jobid                */
	bool jobid_set;		/* true of jobid explicitly set */
	char *mpi_type;		/* --mpi=type			*/
	char *dependency;	/* --dependency, -P type:jobid	*/
	int nice;		/* --nice			*/
	uint32_t priority;	/* --priority                   */
	char *account;		/* --account, -U acct_name	*/
	char *comment;		/* --comment			*/
	char *propagate;	/* --propagate[=RLIMIT_CORE,...]*/
	char *qos;		/* --qos			*/
	int immediate;		/* -i, --immediate      	*/
	uint16_t warn_flags;	/* --signal=flags:<int>@<time>	*/
	uint16_t warn_signal;	/* --signal=flags:<int>@<time>	*/
	uint16_t warn_time;	/* --signal=flags:<int>@<time>	*/

	bool hold;		/* --hold, -H			*/
	bool parsable;		/* --parsable			*/
	bool no_kill;		/* --no-kill, -k		*/
	int requeue;		/* --requeue and --no-requeue	*/
	uint8_t open_mode;	/* --open-mode			*/
	char *acctg_freq;	/* --acctg-freq=<type1>=<freq1>,*/
				/* 	<type2>=<freq2>,...	*/
	bool overcommit;	/* --overcommit -O		*/
	uint16_t shared;	/* --share,   -s		*/
	char *licenses;		/* --licenses, -L		*/
	char *network;		/* --network=			*/
	int  quiet;
	int  verbose;
	uint16_t wait_all_nodes;  /* --wait-nodes-ready=val	*/
	char *wrap;

	/* constraint options */
	int mincpus;		/* --mincpus=n			*/
	int minsockets;		/* --minsockets=n		*/
	int mincores;		/* --mincores=n			*/
	int minthreads;		/* --minthreads=n		*/
	int mem_per_cpu;	/* --mem-per-cpu=n		*/
	int realmem;		/* --mem=n			*/
	long tmpdisk;		/* --tmp=n			*/
	char *constraints;	/* --constraints=, -C constraint*/
	char *gres;		/* --gres			*/
	bool contiguous;	/* --contiguous			*/
	char *nodelist;		/* --nodelist=node1,node2,...	*/
	char *exc_nodes;	/* --exclude=node1,node2,... -x	*/

	/* BLUEGENE SPECIFIC */
	uint16_t geometry[HIGHEST_DIMENSIONS]; /* --geometry, -g	*/
	bool reboot;		/* --reboot			*/
	bool no_rotate;		/* --no_rotate, -R		*/
	uint16_t conn_type[HIGHEST_DIMENSIONS];	/* --conn-type 	*/
	char *blrtsimage;       /* --blrts-image BlrtsImage for block */
	char *linuximage;       /* --linux-image LinuxImage for block */
	char *mloaderimage;     /* --mloader-image mloaderImage for block */
	char *ramdiskimage;     /* --ramdisk-image RamDiskImage for block */
	/*********************/

	char *array_inx;	/* -a, --array			*/
	time_t begin;		/* --begin			*/
	uint16_t mail_type;	/* --mail-type			*/
	char *mail_user;	/* --mail-user			*/
	char *ifname;		/* input file name		*/
	char *ofname;		/* output file name		*/
	char *efname;		/* error file name		*/
	int get_user_env_time;	/* --get-user-env[=timeout]	*/
	int get_user_env_mode;	/* --get-user-env=[S|L]         */
	char *export_env;	/* --export			*/
	char *export_file;	/* --export-file=file		*/
	char *wckey;            /* --wckey workload characterization key */
	char *reservation;      /* --reservation */
 	int ckpt_interval;	/* --checkpoint (int minutes)   */
 	char *ckpt_interval_str;/* --checkpoint (string)        */
 	char *ckpt_dir;		/* --checkpoint-dir (string)    */
	int req_switch;		/* Minimum number of switches   */
	int wait4switch;	/* Maximum time to wait for minimum switches */
	char **spank_job_env;	/* SPANK controlled environment for job
				 * Prolog and Epilog		*/
	int spank_job_env_size;	/* size of spank_job_env	*/
	int umask;		/* job umask for PBS		*/
	int core_spec;		/* --core-spec=n,      -S n	*/
	bool test_only;		/* --test-only			*/
} opt_t;

extern opt_t opt;
extern int error_exit;
extern int ignore_pbs;

/*
 * process_options_first_pass()
 *
 * In this first pass we only look at the command line options, and we
 * will only handle a few options (help, usage, quiet, verbose, version),
 * and look for the script name and arguments (if provided).
 *
 * We will parse the environment variable options, batch script options,
 * and all of the rest of the command line options in
 * process_options_second_pass().
 *
 * Return a pointer to the batch script file name if provided on the command
 * line, otherwise return NULL (in which case the script will need to be read
 * from standard input).
 */
char *process_options_first_pass(int argc, char **argv);

/* process options:
 * 1. update options with option set in the script
 * 2. update options with env vars
 * 3. update options with commandline args
 * 4. perform some verification that options are reasonable
 */
int process_options_second_pass(int argc, char *argv[], const char *file,
				const void *script_body, int script_size);

/* external functions available for SPANK plugins to modify the environment
 * exported to the SLURM Prolog and Epilog programs */
extern char *spank_get_job_env(const char *name);
extern int   spank_set_job_env(const char *name, const char *value,
			       int overwrite);
extern int   spank_unset_job_env(const char *name);

#endif	/* _HAVE_OPT_H */
