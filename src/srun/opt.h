/* $Id$ */

/* Options for srun */

#ifndef _HAVE_OPT_H

#include "config.h"

#include <sys/types.h>
#include <unistd.h>

/*
#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#include <getopt.h>
*/

#ifndef HAVE_POPT_H
#  include "popt/popt.h"
#else 
#  include <popt.h>
#endif

#ifndef _BOOL_DEFINED
#define _BOOL_DEFINED
typedef enum {false, true} bool;
#endif

#define MAX_USERNAME	9

/* global variables relating to user options */
char **remote_argv;
int remote_argc;
int debug;
int verbose;

/* mutually exclusive modes for srun */
enum modes {
	MODE_UNKNOWN	= 0,
	MODE_NORMAL	= 1,
	MODE_IMMEDIATE	= 2,
	MODE_ATTACH	= 3,
	MODE_ALLOCATE	= 4
};

enum modes mode;

enum distribution_t {
	DIST_UNKNOWN	= 0,
	DIST_BLOCK 	= 1, 
	DIST_CYCLIC 	= 2
};

#define format_distribution_t(t) (t == DIST_BLOCK) ? "block" : \
			                   (t == DIST_CYCLIC) ? "cyclic" : \
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
	int  cpus;		/* --cpus=n,        -c n	*/
	int  nodes;		/* --nodes=n,       -N n	*/ 
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

	/* no longer need these, they are set globally : */
	/*int verbose;*/	/* -v, --verbose		*/	
	/*int debug;*/		/* -d, --debug			*/

	int immediate;		/* -i, --immediate      	*/

	bool labelio;		/* --label-output, -l		*/
	bool allocate;		/* --allocate, 	   -A		*/
	bool overcommit;	/* --overcommit,   -O		*/

	/* constraint options */
	int mincpus;		/* --mincpus=n			*/
	int realmem;		/* --mem=n			*/
	long tmpdisk;		/* --tmp=n			*/
	char *constraints;	/* --constraints=, -C constraint*/
	bool contiguous;	/* --contiguous			*/
	char *nodelist;		/* --nodelist=node1,node2,...	*/

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
