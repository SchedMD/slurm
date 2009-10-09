/*****************************************************************************\
 *  opt.h - definitions for salloc option processing
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona1@llnl.gov>,
 *    Christopher J. Morrone <morrone2@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "src/common/env.h"
#include "src/common/macros.h" /* true and false */

#ifndef SYSTEM_DIMENSIONS
#  define SYSTEM_DIMENSIONS 1
#endif

#define DEFAULT_IMMEDIATE	1
#define MAX_USERNAME		9
#define DEFAULT_BELL_DELAY	10

typedef enum {BELL_NEVER, BELL_AFTER_DELAY, BELL_ALWAYS} bell_flag_t;

typedef struct salloc_options {

	char *progname;		/* argv[0] of this program or 
				 * configuration file if multi_prog */
	char user[MAX_USERNAME];/* local username		*/
	uid_t uid;		/* local uid			*/
	gid_t gid;		/* local gid			*/
	uid_t euid;		/* effective user --uid=user	*/
	gid_t egid;		/* effective group --gid=group	*/

	int  nprocs;		/* --nprocs=n,      -n n	*/
	bool nprocs_set;	/* true if nprocs explicitly set */
	int  cpus_per_task;	/* --cpus-per-task=n, -c n	*/
	bool cpus_set;		/* true if cpus_per_task explicitly set */
	int  min_nodes;		/* --nodes=n,       -N n	*/ 
	int  max_nodes;		/* --nodes=x-n,       -N x-n	*/ 
	bool nodes_set;		/* true if nodes explicitly set */
	int min_sockets_per_node; /* --sockets-per-node=n      */
	int max_sockets_per_node; /* --sockets-per-node=x-n    */
	int min_cores_per_socket; /* --cores-per-socket=n      */
	int max_cores_per_socket; /* --cores-per-socket=x-n    */
	int min_threads_per_core; /* --threads-per-core=n      */
	int max_threads_per_core; /* --threads-per-core=x-n    */
	int ntasks_per_node;   /* --ntasks-per-node=n	    */
	int ntasks_per_socket; /* --ntasks-per-socket=n     */
	int ntasks_per_core;   /* --ntasks-per-core=n	    */
	cpu_bind_type_t cpu_bind_type; /* --cpu_bind=           */
	char *cpu_bind;		/* binding map for map/mask_cpu */
	mem_bind_type_t mem_bind_type; /* --mem_bind=		*/
	char *mem_bind;		/* binding map for map/mask_mem	*/
	bool extra_set;		/* true if extra node info explicitly set */
	int  time_limit;	/* --time,   -t	(int minutes)	*/
	char *time_limit_str;	/* --time,   -t (string)	*/
	char *partition;	/* --partition=n,   -p n   	*/
	enum task_dist_states
		distribution;	/* --distribution=, -m dist	*/
        uint32_t plane_size;    /* lllp distribution -> plane_size for
				 * when -m plane=<# of lllp per
				 * plane> */      
	char *job_name;		/* --job-name=,     -J name	*/
	unsigned int jobid;	/* --jobid=jobid		*/
	char *dependency;	/* --dependency, -P type:jobid	*/
	int nice;		/* --nice			*/
	char *account;		/* --account, -U acct_name	*/
	char *comment;		/* --comment			*/
	char *qos;		/* --qos			*/
	int immediate;		/* -I, --immediate      	*/
	uint16_t warn_signal;	/* --signal=<int>@<time>	*/
	uint16_t warn_time;	/* --signal=<int>@<time>	*/

	bool hold;		/* --hold, -H			*/
	bool no_kill;		/* --no-kill, -k		*/
	int	acctg_freq;	/* --acctg-freq=secs		*/
	char *licenses;		/* --licenses, -L		*/
	bool overcommit;	/* --overcommit -O		*/
	int kill_command_signal;/* --kill-command, -K           */
	bool kill_command_signal_set;
	uint16_t shared;	/* --share,   -s		*/
	int  quiet;
	int  verbose;

	/* constraint options */
	int mincpus;		/* --mincpus=n			*/
	int minsockets;		/* --minsockets=n		*/
	int mincores;		/* --mincores=n			*/
	int minthreads;		/* --minthreads=n		*/
	int mem_per_cpu;	/* --mem_per_cpu=n		*/
	int realmem;		/* --mem=n			*/
	long tmpdisk;		/* --tmp=n			*/
	char *constraints;	/* --constraints=, -C constraint*/
	bool contiguous;	/* --contiguous			*/
	char *nodelist;		/* --nodelist=node1,node2,...	*/
	char *exc_nodes;	/* --exclude=node1,node2,... -x	*/
	char *network;		/* --network=			*/

	/* BLUEGENE SPECIFIC */
	uint16_t geometry[SYSTEM_DIMENSIONS]; /* --geometry, -g	*/
	bool reboot;		/* --reboot			*/
	bool no_rotate;		/* --no_rotate, -R		*/
	uint16_t conn_type;	/* --conn-type 			*/
	char *blrtsimage;       /* --blrts-image BlrtsImage for block */
	char *linuximage;       /* --linux-image LinuxImage for block */
	char *mloaderimage;     /* --mloader-image mloaderImage for block */
	char *ramdiskimage;     /* --ramdisk-image RamDiskImage for block */
	/*********************/

	time_t begin;		/* --begin			*/
	uint16_t mail_type;	/* --mail-type			*/
	char *mail_user;	/* --mail-user			*/
	bell_flag_t bell;       /* --bell, --no-bell            */
	bool no_shell;		/* --no-shell                   */
	int get_user_env_time;	/* --get-user-env[=secs]	*/
	int get_user_env_mode; 	/* --get-user-env=[S|L]		*/
	char *cwd;		/* current working directory	*/
	char *reservation;	/* --reservation		*/
	char *wckey;            /* --wckey workload characterization key */
	char **spank_job_env;	/* SPANK controlled environment for job
				 * Prolog and Epilog		*/
	int spank_job_env_size;	/* size of spank_job_env	*/
} opt_t;

extern opt_t opt;
extern int error_exit;		/* exit code for slurm errors */
extern int immediate_exit;	/* exit code for --imediate option & busy */

/* process options:
 * 1. set defaults
 * 2. update options with env vars
 * 3. update options with commandline args
 * 4. perform some verification that options are reasonable
 */
int initialize_and_process_args(int argc, char *argv[]);

/* set options based upon commandline args */
void set_options(const int argc, char **argv);

/* external functions available for SPANK plugins to modify the environment
 * exported to the SLURM Prolog and Epilog programs */
extern char *spank_get_job_env(const char *name);
extern int   spank_set_job_env(const char *name, const char *value, 
			       int overwrite);
extern int   spank_unset_job_env(const char *name);

#endif	/* _HAVE_OPT_H */
