/*****************************************************************************\
 *  opt.h - definitions for srun option processing
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona1@llnl.gov>, et. al.
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifndef _HAVE_OPT_H
#define _HAVE_OPT_H

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <sys/types.h>
#include <unistd.h>

#include "src/common/macros.h" /* true and false */
#include "src/srun/core-format.h"

#define MAX_THREADS	64
#define MAX_USERNAME	9

/* global variables relating to user options */
char **remote_argv;
int remote_argc;
int _verbose;

/* mutually exclusive modes for srun */
enum modes {
	MODE_UNKNOWN	= 0,
	MODE_NORMAL	= 1,
	MODE_IMMEDIATE	= 2,
	MODE_ATTACH	= 3,
	MODE_ALLOCATE	= 4,
	MODE_BATCH	= 5
};

enum modes mode;

enum distribution_t {
	SRUN_DIST_BLOCK 	= 0, 
	SRUN_DIST_CYCLIC 	= 1,
	SRUN_DIST_UNKNOWN 	= 2
};

enum mpi_t {
	MPI_UNKNOWN     = 0,
	MPI_LAM         = 1
};

#define format_distribution_t(t) (t == SRUN_DIST_BLOCK) ? "block" :   \
		                 (t == SRUN_DIST_CYCLIC) ? "cyclic" : \
			         "unknown"

enum io_t {
	IO_ALL		= 0, /* multiplex output from all/bcast stdin to all */
	IO_ONE 	        = 1, /* output from only one task/stdin to one task  */
	IO_PER_TASK	= 2, /* separate output/input file per task          */
	IO_NONE		= 3, /* close output/close stdin                     */
};

#define format_io_t(t) (t == IO_ONE) ? "one" : (t == IO_ALL) ? \
                                                     "all" : "per task"

typedef struct srun_options {

	char *progname;		/* argv[0] of this program 	*/
	char user[MAX_USERNAME];/* local username		*/
	uid_t uid;		/* local uid			*/
	uid_t euid;		/* effective user --uid=user	*/
	gid_t egid;		/* effective group --gid=group	*/
	char *cwd;		/* current working directory	*/

	int  nprocs;		/* --nprocs=n,      -n n	*/
	bool nprocs_set;	/* true if nprocs explicitly set */
	int  cpus_per_task;	/* --cpus-per-task=n, -c n	*/
	bool cpus_set;		/* true if cpus_per_task explicitly set */
	int  max_threads;	/* --threads, -T (threads in srun) */
	int  min_nodes;		/* --nodes=n,       -N n	*/ 
	int  max_nodes;		/* --nodes=x-n,       -N x-n	*/ 
	bool nodes_set;		/* true if nodes explicitly set */
	int  time_limit;	/* --time,   -t			*/
	char *partition;	/* --partition=n,   -p n   	*/
	enum distribution_t
		distribution;	/* --distribution=, -m dist	*/
	char *job_name;		/* --job-name=,     -J name	*/
	unsigned int jobid;     /* --jobid=jobid                */
	enum mpi_t mpi_type;	/* --mpi=type			*/

	char *ofname;		/* --output -o filename         */
	char *ifname;		/* --input  -i filename         */
	char *efname;		/* --error, -e filename         */

	int  slurmd_debug;	/* --slurmd-debug, -D           */
	core_format_t core_type;/* --core= 	        	*/
	char *attach;		/* --attach=id	    -a id	*/ 
	bool join;		/* --join, 	    -j		*/

	/* no longer need these, they are set globally : 	*/
	/*int verbose;*/	/* -v, --verbose		*/	
	/*int debug;*/		/* -d, --debug			*/

	int immediate;		/* -i, --immediate      	*/

	bool hold;		/* --hold, -H			*/
	bool labelio;		/* --label-output, -l		*/
	bool unbuffered;        /* --unbuffered,   -u           */
	bool allocate;		/* --allocate, 	   -A		*/
	bool noshell;		/* --noshell                    */
	bool overcommit;	/* --overcommit,   -O		*/
	bool batch;		/* --batch,   -b		*/
	bool no_kill;		/* --no-kill, -k		*/
	bool share;		/* --share,   -s		*/
	int  max_wait;		/* --wait,    -W		*/
	bool quit_on_intr;      /* --quit-on-interrupt, -q      */
	bool disable_status;    /* --disable-status, -X         */
	int  quiet;
	bool parallel_debug;	/* srun controlled by debugger	*/
	bool debugger_test;	/* --debugger-test		*/

	/* constraint options */
	int mincpus;		/* --mincpus=n			*/
	int realmem;		/* --mem=n			*/
	long tmpdisk;		/* --tmp=n			*/
	char *constraints;	/* --constraints=, -C constraint*/
	bool contiguous;	/* --contiguous			*/
	char *nodelist;		/* --nodelist=node1,node2,...	*/
	char *exc_nodes;	/* --exclude=node1,node2,... -x	*/
	char *relative;		/* --relative -r N              */
	bool no_alloc;		/* --no-allocate, -Z		*/
	int  max_launch_time;   /* Undocumented                 */
	int  max_exit_timeout;  /* Undocumented                 */
	int  msg_timeout;       /* Undocumented                 */

} opt_t;

opt_t opt;

/* return whether any constraints were specified by the user 
 * (if new constraints are added above, might want to add them to this
 *  macro or move this to a function if it gets a little complicated)
 */
#define constraints_given() opt.mincpus != -1 || opt.realmem != -1 ||\
                            opt.tmpdisk != -1 || opt.contiguous   

/* process options:
 * 1. set defaults
 * 2. update options with env vars
 * 3. update options with commandline args
 * 4. perform some verification that options are reasonable
 */
int initialize_and_process_args(int argc, char *argv[]);

#endif	/* _HAVE_OPT_H */
