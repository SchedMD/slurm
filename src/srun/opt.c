/* $Id$ */

#include "config.h"

#include <string.h>	/* strcpy, strncasecmp */

#ifdef HAVE_STRINGS_H
#  include <strings.h>
#endif

#include <stdarg.h>	/* va_start */
#include <stdlib.h>	/* getenv   */
#include <pwd.h>	/* getpwuid */
#include <ctype.h>	/* isdigit  */
#include <sys/param.h>  /* MAXPATHLEN */

#include "opt.h"
#include "env.h"

#define tf_(b) (b == true) ? "true" : "false"

#define __DEBUG 1

/*---[ popt definitions ]------------------------------------------------*/

/* generic OPT_ definitions -- mainly for use with env vars 
 * (not to be confused with POPT_* definitions)
 */
#define OPT_NONE	0x00
#define OPT_INT		0x01
#define OPT_STRING	0x02

/* specific options, used with popt (and env var processing) */
#define OPT_NPROCS	0x03
#define OPT_CPUS	0x04
#define OPT_NODES	0x05
#define OPT_PARTITION	0x06
#define OPT_BASENODE	0x07
#define OPT_DISTRIB	0x08 
#define OPT_OUTPUT	0x09
#define OPT_INPUT	0x0a
#define OPT_ERROR	0x0b
#define OPT_CORE	0x0c
#define OPT_VERBOSE	0x0d
#define OPT_DEBUG	0x0e
#define OPT_ALLOCATE	0x0f
#define OPT_ATTACH	0x10
#define OPT_CONST	0x11
#define OPT_VERSION	0x12
#define OPT_JOIN	0x13
#define OPT_STEAL	0x14
#define OPT_CDDIR	0x15

/* constraint type options */
#define OPT_MINCPUS	0x50
#define OPT_REALMEM	0x51
#define OPT_VIRTMEM	0x52
#define OPT_TMPDISK	0x53
#define OPT_CONTIG	0x54
#define OPT_NODELIST	0x55 
#define OPT_CONSTRAINT	0x56


#ifndef POPT_TABLEEND
#  define POPT_TABLEEND { NULL, '\0', 0, 0, 0, NULL, NULL }
#endif

/* options related to attach mode only */
struct poptOption attachTable[] = {
	{ "attach", 'a', POPT_ARG_STRING, &opt.attach, OPT_ATTACH,
	 "attach to running job with job id = id",
	 "id"
	},
	{ "join", 'j', POPT_ARG_NONE, &opt.join, 0,
	  "join with running job",
	},
	{ "steal", 's', POPT_ARG_NONE, NULL, OPT_STEAL,
	  "steal connection to running job",
	},
	POPT_TABLEEND
};

/* options directly related to allocate-only mode */
struct poptOption allocateTable[] = {
	{ "allocate", 'A', POPT_ARG_NONE, &opt.allocate, OPT_ALLOCATE,
	  "allocate resources and spawn a shell",
	},
	POPT_TABLEEND
};

/* define constraint options here */
struct poptOption constraintTable[] = {
	{ "mincpus", '\0', POPT_ARG_INT, &opt.mincpus, OPT_MINCPUS,
	  "minimum number of cpus per node", 
	  "n"
	},
	{ "mem", '\0', POPT_ARG_STRING, NULL, OPT_REALMEM,
	  "minimum amount of real memory",
	  "MB"
	},
	{ "tmp", '\0', POPT_ARG_STRING, NULL, OPT_TMPDISK,
	  "minimum amount of temp disk",
	  "MB"
	}, 
	{ "constraint", 'C' , POPT_ARG_STRING, &opt.constraints, 
	  OPT_CONSTRAINT, "specify a list of constraints",
	  "list"
	},
	{ "contiguous", '\0', POPT_ARG_NONE, &opt.contiguous, OPT_CONTIG,
	  "demand a contiguous range of nodes",
	},
	{ "nodelist", 'w', POPT_ARG_STRING, &opt.nodelist, OPT_NODELIST,
	  "request a specific list of hosts",
	  "host1,host2,..."
	},
	POPT_TABLEEND
};
	  

/* options that affect parallel runs (may or may not apply to modes
 * above
 */
struct poptOption runTable[] = {
	{ "nprocs", 'n', POPT_ARG_INT, &opt.nprocs, OPT_NPROCS,
	  "number of processes to run",
	  "nprocs" 
	},
	{ "cpus", 'c', POPT_ARG_INT, &opt.cpus, OPT_CPUS,
	  "number of cpus required per process",
	  "ncpus"
	},
	{ "nodes", 'N', POPT_ARG_INT, &opt.nodes, OPT_NODES,
	  "number of nodes on which to run",
	  "nnodes"
	},
	{ "partition", 'p', POPT_ARG_STRING, &opt.partition, OPT_PARTITION,
	  "partition requested",
	  "partition"
	},
	{ "cddir", 'D', POPT_ARG_STRING, NULL, OPT_CDDIR,
	  "change current working directory of remote procs",
	  "path"
	},
	{ "immediate", 'I', POPT_ARG_NONE, &opt.immediate, 0,
	  "exit if resources are not immediately available",
	},
	{ "overcommit", 'O', POPT_ARG_NONE, &opt.overcommit, 0,
	  "overcommit resources",
	},
	{ "label", 'l', POPT_ARG_NONE, &opt.labelio, 0,
	  "prepend task number to lines of stdout/err",
	},
	{ "distribution", 'm', POPT_ARG_STRING, 0, OPT_DISTRIB,
	  "distribution method for processes",
	  "(block|cyclic)"
	},
	{ "job-name", 'J', POPT_ARG_STRING, &opt.job_name, 0,
	  "name of job",
	  "jobname"
	},
	{ "output", 'o', POPT_ARG_STRING, 0, OPT_OUTPUT,
	  "location of stdout redirection",
	  "out"
	},
	{ "input", 'i', POPT_ARG_STRING, 0, OPT_INPUT,
	  "location of stdin redirection",
	  "in"
	},
	{ "error", 'e', POPT_ARG_STRING, 0, OPT_ERROR,
	  "location of stderr redirection",
	  "err"
	},
/*	{ "corefile-format", '\0', POPT_ARG_STRING, &opt.core_format, 0,
	  "corefile output format",
	  "format"
	},
*/	{ "verbose", 'v', 0, 0, OPT_VERBOSE, 
	  "verbose operation", 
	},
	{ "debug", 'd', 0, 0, OPT_DEBUG,
	  "enable debug",
	},
	POPT_TABLEEND
};

/* table of "other" options (just version information for now) */
struct poptOption otherTable[] = {
	{ "version", 'V', POPT_ARG_NONE, 0, OPT_VERSION, 
	  "output version information and exit"
	},
	POPT_TABLEEND
};

/* full option table: */
struct poptOption options[] = {
	{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, &runTable, 0,
	  "Parallel run options:", NULL
	},
	{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, &allocateTable, 0,
	  "Allocate only:", NULL
	},
	{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, &attachTable, 0,
	  "Attach to running job:", NULL
	},
	{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, &constraintTable, 0,
	  "Constraint options:"
	},
	POPT_AUTOHELP
	{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, &otherTable, 0,
	  "Other options:", NULL
	},
	POPT_TABLEEND
};

/*---[ end popt definitions ]---------------------------------------------*/

/*---[ env var processing ]-----------------------------------------------*/

/*
 * try to use a similar scheme as popt. 
 * 
 * in order to add a new env var (to be processed like an option):
 *
 * define a new entry into env_vars[], if the option is a simple int
 * or string you may be able to get away with adding a pointer to the
 * option to set. Otherwise, process var based on "type" in opt_env.
 */
struct env_vars_t {
	const char *var;
	int type;
	void *arg;
};

struct env_vars_t env_vars[] = {
	{ "SLURM_DEBUG",	 OPT_DEBUG,     NULL		},
	{ "SLURM_NPROCS", 	 OPT_INT,    	&opt.nprocs 	},
	{ "SLURM_CPUS_PER_TASK", OPT_INT,    	&opt.cpus    	},
	{ "SLURM_NNODES", 	 OPT_INT,    	&opt.nodes   	},
	{ "SLURM_PARTITION", 	 OPT_STRING, 	&opt.partition 	},
	{ "SLURM_STDINMODE",	 OPT_INPUT,     &opt.input	},
	{ "SLURM_STDOUTMODE",	 OPT_OUTPUT,    &opt.output	},
	{ "SLURM_STDERRMODE", 	 OPT_ERROR,     &opt.error	},
	{ "SLURM_DISTRIBUTION",  OPT_DISTRIB,   NULL		},
	{ NULL, 		 0, 		NULL		}
};

/* forward declarations of static functions 
 *
 */

/* 
 * fill in default options 
 */
static void opt_default();

/* set options based upon env vars 
 */
static void opt_env();

/* set options based upon commandline args
 */
static void opt_args(int, char **);

/* verity options sanity 
 */
static bool opt_verify(poptContext, bool, bool, bool);

/* list known options and their settings 
 */
static void opt_list();

/*---[ end forward declarations of static functions ]---------------------*/ 

int
initialize_and_process_args(int argc, char *argv[])
{
	/* initialize option defaults */
	opt_default();

	/* initialize options with env vars */
	opt_env();

	/* initialize options with argv */
	opt_args(argc, argv);

#ifdef __DEBUG
	opt_list();
#endif
	return 1;

}

static char *
xbasename(char *path)
{
	char *p;

	p = strchr(path, '/');
	return(p ? (p + 1) : path);
}

static void
print_version()
{
	printf("%s %s\n", PACKAGE, VERSION);
}

/*
 * verify_iotype(): helper for output/input/error arguments.
 *
 * will return IO_NORMAL if string matches "normal" 
 *
 * prunes off trailing '%' char after setting IO_PER_TASK if such
 * a one exists.
 */
static enum io_t 
verify_iotype(char **name)
{
	enum io_t type;
	char *p = *name;
	int end = strlen(p) - 1;

	/* name must have form "file.%" to be IO_PER_TASK */
	if (p[end] == '%') {
		type = IO_PER_TASK;
		p[end]  = '\0';		/* no longer need % char */

	} else if (strncasecmp(p, "normal", (size_t) 6) != 0)
		type = IO_ALL;
        else if (strncasecmp(p, "none", (size_t) 4) != 0)
		type = IO_NONE;
	else
		type = IO_NORMAL;

	return type;
}

/* 
 * verify that a distribution type in arg is of a known form
 * returns the distribution_t or DIST_UNKNOWN
 */
static enum distribution_t 
verify_dist_type(const char *arg)
{
	enum distribution_t result = DIST_UNKNOWN;

	if (strncasecmp(arg, "cyclic", strlen(arg)) == 0) 
		result = DIST_CYCLIC;
	else if (strncasecmp(arg, "block", strlen(arg)) == 0)
		result = DIST_BLOCK;
	else 
		result = DIST_UNKNOWN;

	return result;
}

/*
 * to_bytes(): verify that arg is numeric with optional "G" or "M" at end
 * if "G" or "M" is there, multiply by proper power of 2 and return
 * number in bytes
 */ 
static long 
to_bytes(const char *arg)
{
	char *buf;
	char *endptr;
	int end;
	int multiplier = 1;
	long result;

	buf = strdup(arg);

	end = strlen(buf) - 1;

	if (isdigit(buf[end])) {
		result = strtol(buf, &endptr, 10);
		
		if (*endptr != '\0') 
			result = -result;

	} else {

		switch(toupper(buf[end])) {

			case 'G':
			  multiplier =  1024;
			  break;

			case 'M':
			  /* do nothing */
			  break;

			default:
			  multiplier = -1;
		}

		buf[end] = '\0';

		result = multiplier * strtol(buf, &endptr, 10);

		if (*endptr != '\0') 
			result = -result;

	}


	return result;
}


/* set a few env vars for allocate mode so they'll be available in
 * the resulting subshell
 */
static void
set_allocate_mode_env_vars()
{
	int rc;

	if (opt.output == IO_ALL) { 	
		/* all output to single file*/
		rc = setenvf("SLURM_OUTPUT=%s", opt.ofname);
	} else if (opt.output == IO_PER_TASK) {
		/* output is per task, need to put '%' char back */
		rc = setenvf("SLURM_OUTPUT=%s%%", opt.ofname);
	}

	if (opt.error == IO_ALL) {
		rc = setenvf("SLURM_ERROR=%s", opt.efname);
	} else if (opt.output == IO_PER_TASK) {
		rc = setenvf("SLURM_ERROR=%s%%", opt.efname);
	}

	if (opt.input == IO_ALL) {
		rc = setenvf("SLURM_INPUT=%s", opt.ifname);
	} else if (opt.input == IO_PER_TASK) {
		rc = setenvf("SLURM_INPUT=%s%%", opt.ifname);
	}

}

/*
 * print error message to stderr with opt.progname prepended
 */
static void argerror(const char *msg, ...) 
{
	va_list ap;
	char buf[256];

	va_start(ap, msg);
	vsnprintf(buf, sizeof(buf), msg, ap);

	fprintf(stderr, "%s: %s\n", 
			opt.progname ? opt.progname : "srun",
			buf);
	va_end(ap);
}
	
/*
 * opt_default(): used by initialize_and_process_args to set defaults
 */
static void 
opt_default()
{
	char buf[MAXPATHLEN+1];
	struct passwd *pw;

	if ((pw = getpwuid(getuid())) != NULL) {
		strncpy(opt.user, pw->pw_name, MAX_USERNAME);
		opt.uid = pw->pw_uid;
	} else
		argerror("who are you?");


	if ((getcwd(buf, MAXPATHLEN)) == NULL) {
		argerror("getcwd failed\n");
		exit(1);
	}

	opt.cwd = strdup(buf);

	opt.progname = NULL;
	
	opt.nprocs 	= 1;
	opt.cpus 	= 1;
	opt.nodes	= 1;
	opt.partition	= NULL;

	opt.job_name	= "";
	
	opt.distribution = DIST_BLOCK;

	opt.output	= IO_NORMAL;
	opt.input	= IO_NORMAL;
	opt.error	= IO_NORMAL;
	opt.ofname	= NULL;
	opt.ifname	= NULL;
	opt.efname	= NULL;

	opt.core_format  = "normal";

	opt.labelio 	= false;
	opt.overcommit	= false;

	opt.immediate 	= false;

	opt.allocate 	= false;
	opt.attach   	= NULL;
	opt.join	= false;

	verbose = 0;
	debug = 0;

	/* constraint default (-1 is no constraint) */
	opt.mincpus = -1;
	opt.realmem = -1;
	opt.tmpdisk = -1;
	opt.constraints = NULL;
	opt.contiguous = false;
	opt.nodelist = NULL;

	mode = MODE_NORMAL;

}

/*
 * opt_env(): used by initialize_and_process_args to set options via
 *            environment variables. See comments above for how to
 *            extend srun to process different vars
 */
static void
opt_env()
{
	char *val;
	char *end;
	struct env_vars_t *e = env_vars; 

	while (e->var) {

	    if (debug > 0)
		    fprintf(stderr, "looking for env var %s\n", e->var);

	    if ((val = getenv(e->var)) != NULL) {

		    if (debug > 0) 
			    fprintf(stderr, "now processing env var %s=%s\n",
					    e->var, val);

		    switch (e->type) {
		      case OPT_STRING:
		        *((char **)e->arg) = strdup(val);
		        break;

		      case OPT_INT:
			if (val != NULL ) {
				*((int *)e->arg) = (int)strtol(val, &end, 10);

				if (!(end && *end == '\0')) {
					fprintf(stderr, "%s=%s invalid"
							". ignoring...\n", 
							e->var, val);
				}
			}
			break;

		      case OPT_DEBUG:
			if (val != NULL) {
				debug = (int) strtol(val, &end, 10);
				if (!(end && *end == '\0')) {
					fprintf(stderr, "%s=%s invalid\n",
							e->var, val);
				}
			}
			break;

		      case OPT_INPUT:
		      case OPT_OUTPUT:
		      case OPT_ERROR:
			if (val != NULL) {
				*((char **)e->arg) = strdup(val);
			}
			break;

		      case OPT_DISTRIB:
			{
				enum distribution_t dt = 
					verify_dist_type(val);

				if (dt == DIST_UNKNOWN) {
					argerror("\"%s=%s\" -- "
						 "invalid distribution type."
						 " ignoring...",
						 e->var, val);
				} else 
					opt.distribution = dt;

			}
			break;

		      default:
			/* do nothing */
			break;
		    }

	    }

		e++;
	}

	
}

/*
 * opt_args() : set options via commandline args and popt
 */
static void
opt_args(int ac, char ** av)
{
	int rc;
	int i;
	bool nprocs_set, nnodes_set, cpus_set;
	const char **rest;
	const char *arg;
	poptContext optctx;

	opt.progname = xbasename(av[0]);

	optctx = poptGetContext("srun", ac, (const char **)av, options, 
			        POPT_CONTEXT_POSIXMEHARDER);

	poptSetOtherOptionHelp(optctx, "[OPTIONS...] executable [args...]"); 

	poptReadDefaultConfig(optctx, 0);

	/* first pass through args to see if attach or allocate mode
	 * are set
	 */
	while ((rc = poptGetNextOpt(optctx)) > 0) {
		arg = poptGetOptArg(optctx);

		switch (rc) {
		  case OPT_VERSION:
		    print_version();
		    exit(0);
		    break;

		  case OPT_ATTACH:
		    if (opt.allocate) {
			    argerror("cannot simultaneously allocate "
					    "and attach");
			    exit(1);
		    }
		    mode = MODE_ATTACH;
		    opt.attach = strdup(arg);
		    break;

		  case OPT_ALLOCATE:
		    if (opt.attach != NULL) {
			    argerror("cannot simultaneously allocate "
					    "and attach");
			    exit(1);
		    }
		    mode = MODE_ALLOCATE;
		    break;

		  default:
		    break;
		    /* do nothing */
		}
	}


	poptResetContext(optctx);

	nprocs_set = false;
	nnodes_set = false;
	cpus_set = false;


	while ((rc = poptGetNextOpt(optctx)) > 0) {
		arg = poptGetOptArg(optctx);

		switch (rc) {
		  case OPT_VERBOSE:
		    verbose++;
		    break;

		  case OPT_DEBUG:
		    debug++;
		    break;

		  case OPT_OUTPUT:
		    opt.ofname = strdup(arg);
		    opt.output = verify_iotype(&opt.ofname);
		    break;

		  case OPT_INPUT:
		    opt.ifname = strdup(arg);
		    opt.input = verify_iotype(&opt.ifname);
		    break;

		  case OPT_ERROR:
		    opt.efname = strdup(arg);
		    opt.error = verify_iotype(&opt.efname);
		    break;

		  case OPT_DISTRIB:
		    opt.distribution = verify_dist_type(arg);
		    if (opt.distribution == DIST_UNKNOWN)  {
			    argerror("Error: distribution type `%s' "
					    "is not recognized", arg);
			    poptPrintUsage(optctx, stderr, 0);
			    exit(1);
		    }
		    break;

		  case OPT_NPROCS:
		    nprocs_set = true;
		    break;

		  case OPT_CPUS:
		    cpus_set = true;
		    break;

		  case OPT_NODES:
		    nnodes_set = true;
		    break;

		  case OPT_REALMEM:
		    opt.realmem = (int) to_bytes(arg);
		    if (opt.realmem < 0) {
			    argerror("Error: invalid memory constraint %s",
					    arg);
			    exit(1);
		    }
		    break;

		  case OPT_TMPDISK:
		    opt.tmpdisk = to_bytes(arg);
		    if (opt.tmpdisk < 0) {
			    argerror("Error: invalid tmp disk constraint %s",
					    arg);
			    exit(1);
		    }
		    break;

		  case OPT_CDDIR:
		    free(opt.cwd);
		    opt.cwd = strdup(arg);
		    break;

		  case OPT_JOIN:
		    if (opt.attach == NULL) {
			    argerror("Error: --join only makes sense "
					    "with --attach");
			    exit(1);
		    }
		    opt.join = true;
		    break;

		  case OPT_STEAL:
		    if (opt.attach == NULL) {
			    argerror("Error: --steal only makes sense "
					    "with --attach");
			    exit(1);
		    }
		    opt.join = false;
		    break;

		  default:
		    break;
		    /* do nothing */
		}
	}
			  

	if (rc < -1) {
		argerror("bad argument %s: %s", 
			poptBadOption(optctx, POPT_BADOPTION_NOALIAS),
			poptStrerror(rc));
		poptPrintUsage(optctx, stderr, 0);

		exit(1);
	}

	rest = poptGetArgs(optctx);
	remote_argc = 0;

	if (rest != NULL)  {
		while (rest[remote_argc] != NULL)
			remote_argc++;
	}

	remote_argv = (char **) malloc(remote_argc*sizeof(char *));

	for (i = 0; i < remote_argc; i++)
		remote_argv[i] = strdup(rest[i]);

	if (!opt_verify(optctx, nnodes_set, cpus_set, nprocs_set)) {
		poptPrintUsage(optctx, stderr, 0);
		exit(1);
	}

	poptFreeContext(optctx);

}

/* 
 * opt_verify : perform some post option processing verification
 *
 */
static bool
opt_verify(poptContext optctx, 
 	   bool nodes_set, bool cpus_set, bool procs_set)
{
	bool verified = true;


	if (mode == MODE_ATTACH) { /* attach to a running job */
		if (nodes_set || cpus_set || procs_set) {
			argerror("do not specific a node allocation "
				 "with --attach (-a)");
			verified = false;
		}

		if (constraints_given()) {
			argerror("do not specify any constraints with "
				 "--attach (-a)");
			verified = false;
		}


		/* XXX check here to see if job to attach to exists ? */
					
		/* XXX what other args are incompatible with attach mode? */

	} else { 
		
		if (mode == MODE_ALLOCATE) {
		
			/*
			 if (remote_argc > 0) {
				argerror("Error: do not specify a remote "
					 "command with --allocate (-A)");
				verified = false;
			}
			*/

			/* set output/input/err (an whatever else) as
			 * env vars so they will be "defaults" in allocate
			 * subshell
			 */
			set_allocate_mode_env_vars();

		} else {

			if (remote_argc == 0) {
				argerror("Error: must supply remote command");
				verified = false;
			}
		}


		/* check for realistic arguments */
		if (opt.nprocs <= 0) {
			fprintf(stderr, "%s: invalid number of processes (-n %d)\n",
					opt.progname, opt.nprocs);
			verified = false;
		}

		if (opt.cpus <= 0) {
			fprintf(stderr, "%s: invalid number of cpus per process"
					"(-n %d)\n", opt.progname, opt.cpus);
			verified = false;
		}

		if (opt.nodes <= 0) {
			fprintf(stderr, "%s: invalid number of nodes (-N %d)\n",
					opt.progname, opt.nodes);
			verified = false;
		}

		/* massage the numbers */
		if (nodes_set && !procs_set) {
			/* 1 proc / node default */
			opt.nprocs = opt.nodes;

		} else if (nodes_set && procs_set) {

			/* make sure # of procs >= nodes */
			if (opt.nprocs < opt.nodes) {

				argerror("Warning: can't run %d "
					 "processes on %d nodes, setting "
					 "nodes to %d", opt.nprocs, 
					 opt.nodes, opt.nprocs);

				opt.nodes = opt.nprocs;
			}

		} else if (procs_set && !nodes_set) {

			opt.nodes = opt.nprocs;

		}

	}


	return verified;
}

#ifdef __DEBUG 

/* generate meaningful output message based on io type and "filename" */
char *
print_io_t_with_filename(enum io_t type, char *filename)
{
	char buf[256];
	
	switch (type) {
		case IO_ALL:
		  snprintf(buf, 256, "%s (file `%s')", 
				  format_io_t(type),
				  filename);
		  break;

		case IO_PER_TASK:
		  snprintf(buf, 256, "%s (file `%s<task_id>')",
				  format_io_t(type),
				  filename);
		  break;

		case IO_NORMAL:
		  snprintf(buf, 256, "normal");
		  break;
		  
		default:
		  snprintf(buf, 256, "error, unknown type");
		  break;
	}
	return strdup(buf);
}

static char *
print_constraints()
{
	char buf[256];

	buf[0] = '\0';

	if (opt.mincpus > 0)
		snprintf(buf, 256, "mincpus=%d", opt.mincpus);

	if (opt.realmem > 0)
		snprintf(buf, 256, "%s mem=%dM", buf, opt.realmem);

	if (opt.tmpdisk > 0)
		snprintf(buf, 256, "%s tmp=%ldM", buf, opt.tmpdisk);

	if (opt.contiguous == true)
		snprintf(buf, 256, "%s contiguous", buf);

	if (opt.nodelist != NULL)
		snprintf(buf, 256, "%s nodelist=%s", buf, opt.nodelist);

	if (opt.constraints != NULL)
		snprintf(buf, 256, "%s constraints=`%s'", buf, 
				opt.constraints);

	return strdup(buf);
}

static void
opt_list()
{
	int i;

	printf("\ndefined options for program `%s'\n", opt.progname);
	printf("------------ ---------------------\n");

	printf("user        : `%s'\n", opt.user);
	printf("uid         : %ld\n" , (long)opt.uid);
	printf("cwd	    : %s\n"  , opt.cwd);
	printf("nprocs      : %d\n", opt.nprocs);
	printf("cpus/proc   : %d\n", opt.cpus);
	printf("nodes       : %d\n", opt.nodes);
	printf("total cpus  : %d\n", opt.nprocs * opt.cpus);
	printf("partition   : %s\n", 
			opt.partition == NULL ? "default" : opt.partition);
	printf("job name    : `%s'\n", opt.job_name);
	printf("distribution: %s\n", 
			format_distribution_t(opt.distribution));
	printf("output      : %s\n", 
			print_io_t_with_filename(opt.output, opt.ofname));
	printf("error	    : %s\n", 
			print_io_t_with_filename(opt.error, opt.efname));
	printf("input       : %s\n", 
			print_io_t_with_filename(opt.input, opt.ifname));
	printf("core format : %s\n", opt.core_format);
	printf("verbose	    : %d\n"  , verbose); 
	printf("debug       : %d\n"  , debug);
	printf("immediate   : %s\n"  , tf_(opt.immediate));
	printf("label output: %s\n"  , tf_(opt.labelio));
	printf("allocate    : %s\n"  , tf_(opt.allocate));
	printf("attach      : `%s'\n", opt.attach);
	printf("overcommit  : %s\n"  , tf_(opt.overcommit));
	printf("\n");
	printf("constraints : %s\n"  , print_constraints());
	printf("\n");
	printf("remote command :");

	for (i = 0; i < remote_argc;  i++)
		printf(" %s", remote_argv[i]);
	printf("\n");

}
#endif /* __DEBUG */

