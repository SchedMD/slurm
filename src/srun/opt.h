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
#include <config.h>
#endif

#include <sys/types.h>
#include <unistd.h>

/*
#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#include <getopt.h>
*/

#if HAVE_POPT_H
#include <popt.h>
#else
#include <src/popt/popt.h>
#endif

#include <src/common/macros.h> /* true and false */

#define MAX_THREADS	10
#define MAX_USERNAME	9

/* global variables relating to user options */
char **remote_argv;
int remote_argc;
int _debug;
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

#define format_distribution_t(t) (t == SRUN_DIST_BLOCK) ? "block" :   \
		                 (t == SRUN_DIST_CYCLIC) ? "cyclic" : \
			         "unknown"

enum io_t {
	IO_NORMAL 	= 0,
	IO_ALL		= 1,
	IO_PER_TASK	= 2,
	IO_NONE		= 3,
};

#define format_io_t(t) (t == IO_NORMAL) ? "normal" : (t == IO_ALL) ? \
                                                     "all" : "per task"

typedef struct srun_options {

	char *progname;		/* argv[0] of this program 	*/
	char user[MAX_USERNAME];/* local username		*/
	uid_t uid;		/* local uid			*/
	char *cwd;		/* current working directory	*/

	int  nprocs;		/* --nprocs=n,      -n n	*/
	bool nprocs_set;	/* true if nprocs explicitly set */
	int  cpus_per_task;	/* --cpus-per-task=n, -c n	*/
	bool cpus_set;		/* true if cpus_per_task explicitly set */
	int  max_threads;	/* --threads, -T (threads in srun) */
	int  nodes;		/* --nodes=n,       -N n	*/ 
	bool nodes_set;		/* true if nodes explicitly set */
	int  time_limit;	/* --time,   -t			*/
	char *partition;	/* --partition=n,   -p n   	*/
	enum distribution_t
		distribution;	/* --distribution=, -m dist	*/
	char *job_name;		/* --job-name=,     -J name	*/

	enum io_t output;	/* --output=,       -o type	*/
	char *ofname;		/* output filename if PER_TASK 	*/

	enum io_t input;	/* --input=, 	    -i type	*/
	char *ifname;		/* input filename if PER_TASK 	*/

	enum io_t error;	/* --error=,	    -e type	*/
	char *efname;		/* stderr filename if PER_TASK 	*/

	char *core_format;	/* --corefile-format=, -C type	*/
	char *attach;		/* --attach=id	    -a id	*/ 
	bool join;		/* --join, 	    -j		*/

	/* no longer need these, they are set globally : 	*/
	/*int verbose;*/	/* -v, --verbose		*/	
	/*int debug;*/		/* -d, --debug			*/

	int immediate;		/* -i, --immediate      	*/

	bool labelio;		/* --label-output, -l		*/
	bool allocate;		/* --allocate, 	   -A		*/
	bool overcommit;	/* --overcommit,   -O		*/
	bool batch;		/* --batch,   -b		*/
	bool fail_kill;		/* --kill,   -k			*/
	bool share;		/* --share,   -s		*/
#ifdef HAVE_TOTALVIEW
	bool totalview;		/* srun controlled by TotalView	*/
#endif

	/* constraint options */
	int mincpus;		/* --mincpus=n			*/
	int realmem;		/* --mem=n			*/
	long tmpdisk;		/* --tmp=n			*/
	char *constraints;	/* --constraints=, -C constraint*/
	bool contiguous;	/* --contiguous			*/
	char *nodelist;		/* --nodelist=node1,node2,...	*/
	bool no_alloc;		/* --no-allocate, -Z		*/

} opt_t;

opt_t opt;

/* return whether any constraints were specified by the user 
 * (if new constraints are added above, might want to add them to this
 *  macro or move this to a function if it gets a little complicated)
 */
#define constraints_given() opt.mincpus > 0 || opt.realmem > 0 ||\
                            opt.tmpdisk > 0 ||\
			    opt.contiguous  || opt.nodelist != NULL

/* process options:
 * 1. set defaults
 * 2. update options with env vars
 * 3. update options with commandline args
 * 4. perform some verification that options are reasonable
 */
int initialize_and_process_args(int argc, char *argv[]);

#endif	/* _HAVE_OPT_H */
