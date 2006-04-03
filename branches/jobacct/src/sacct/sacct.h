/*****************************************************************************\
 *  sacct.h - header file for sacct
 *
 *  $Id: sacct.h 7541 2006-03-18 01:44:58Z da $
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>.
 *  UCRL-CODE-217948.
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
#ifndef _SACCT_H
#define _SACCT_H

#include <ctype.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "src/common/getopt.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/list.h"

#define ERROR 2

/* slurmd uses "(uint32_t) -2" to track data for batch allocations
 * which have no logical jobsteps. */
#define BATCH_JOB_TIMESTAMP 0

#define BRIEF_FIELDS "jobstep,status,error"
#define DEFAULT_FIELDS "jobstep,jobname,partition,ncpus,status,error"
#define LONG_FIELDS "jobstep,usercpu,systemcpu,minflt,majflt,ntasks,ncpus,elapsed,status,error"

#define BUFFER_SIZE 4096
#define STATUS_COUNT 10

#define MAX_PRINTFIELDS 100
#define EXPIRE_READ_LENGTH 10
#define MAX_RECORD_FIELDS 100

#define SECONDS_IN_MINUTE 60
#define SECONDS_IN_HOUR (60*SECONDS_IN_MINUTE)
#define SECONDS_IN_DAY (24*SECONDS_IN_HOUR)

#define TIMESTAMP_LENGTH 15

#ifndef SLURM_CONFIG_FILE
#define SLURM_CONFIG_FILE "sacct was built with no default slurm.conf path"
#endif

/* Map field names to positions */

/* Fields common to all records */
enum {	F_JOB =	0,
	F_PARTITION,	
	F_JOB_START,	
	F_TIMESTAMP,	
	F_UID,	
	F_GID,	
	F_RESERVED1,	
	F_RESERVED2,	
	F_RECTYPE,	
	HEADER_LENGTH
};

/* JOB_START fields */
enum {	F_JOBNAME = HEADER_LENGTH,
	F_BATCH,		
	F_PRIORITY,	
	F_NCPUS,		
	F_NODES,
	JOB_START_LENGTH
};

/* JOB_STEP fields */
enum {	F_JOBSTEP = HEADER_LENGTH,
	F_STATUS,
	F_ERROR,
	F_NTASKS,
	F_STEPNCPUS,
	F_ELAPSED,
	F_CPU_SEC,
	F_CPU_USEC,
	F_USER_SEC,
	F_USER_USEC,
	F_SYS_SEC,
	F_SYS_USEC,
	F_RSS,
	F_IXRSS,
	F_IDRSS,
	F_ISRSS,
	F_MINFLT,
	F_MAJFLT,
	F_NSWAP,
	F_INBLOCKS,
	F_OUBLOCKS,
	F_MSGSND,
	F_MSGRCV,
	F_NSIGNALS,
	F_NVCSW,
	F_NIVCSW,
	F_VSIZE,
	F_PSIZE,
	F_STEPNAME,
	JOB_STEP_LENGTH
};

/* JOB_TERM / JOB_SUSPEND fields */
enum {	F_TOT_ELAPSED = HEADER_LENGTH,
	F_TERM_STATUS,
	JOB_TERM_LENGTH
};

/* On output, use fields 12-37 from JOB_STEP */

typedef enum {	HEADLINE,
		UNDERSCORE,
		JOB,
		JOBSTEP
} type_t;

enum {	CANCELLED,
	COMPLETED,
	COMPLETING,
	FAILED,
	NODEFAILED,
	PENDING,
	RUNNING,
	TIMEDOUT
};

typedef struct header {
	long	jobnum;
	char	*partition;
	time_t 	job_start;
	time_t	timestamp;
	int	uid;
	int	gid;
	int     rec_type;
} acct_header_t;

typedef struct job_rec {
	int	job_start_seen,		/* useful flags */
		job_step_seen,
		job_terminated_seen,
		jobnum_superseded;	/* older jobnum was reused */
	acct_header_t header;
	char	*nodes;
	char	*jobname;
	int	batch;
	int	priority;
	long	ncpus;
	long	ntasks;
	int	status;
	int	error;
	long	elapsed;
	long	tot_cpu_sec, tot_cpu_usec;
	long	vsize, psize;
	struct rusage rusage;
	List    steps;
} job_rec_t;

typedef struct step_rec {
	acct_header_t header;
	long	stepnum;	/* job's step number */
	long	next;		/* linked list of job steps */
	char	*stepname;
	int	status;
	int	error;
	long	ntasks, ncpus;
	long	elapsed;
	long	tot_cpu_sec, tot_cpu_usec;
	long	vsize, psize;
	struct rusage rusage;
} step_rec_t;

typedef struct selected_step_t {
	char *job;
	char *step;
} selected_step_t;

typedef struct fields {
	char *name;		/* Specified in --fields= */
	void (*print_routine) ();	/* Who gets to print it? */
} fields_t;

/* Input parameters */
typedef struct sacct_parameters {
	int opt_dump;		/* --dump */
	int opt_dup;		/* --duplicates; +1 = explicitly set */
	int opt_fdump;		/* --formattted_dump */
	int opt_gid;		/* --gid (-1=wildcard, 0=root) */
	int opt_header;		/* can only be cleared */
	int opt_help;		/* --help */
	int opt_long;		/* --long */
	int opt_lowmem;		/* --low_memory */
	int opt_purge;		/* --purge */
	int opt_total;		/* --total */
	int opt_uid;		/* --uid (-1=wildcard, 0=root) */
	int opt_verbose;	/* --verbose */
	long opt_expire;		/* --expire= */ 
	char *opt_expire_timespec; /* --expire= */
	char *opt_field_list;	/* --fields= */
	char *opt_filein;	/* --file */
	char *opt_job_list;	/* --jobs */
	char *opt_jobstep_list;	/* --jobstep */
	char *opt_partition_list;/* --partitions */
	char *opt_state_list;	/* --states */
} sacct_parameters_t;

extern fields_t fields[];
extern sacct_parameters_t params;

extern long inputError;		/* Muddle through bad data, but complain! */

extern List jobs;

extern int printfields[MAX_PRINTFIELDS],	/* Indexed into fields[] */
	nprintfields;

/* process.c */
void process_start(char *f[], int lc);
void process_step(char *f[], int lc);
void process_suspend(char *f[], int lc);
void process_terminated(char *f[], int lc);
void destroy_job(void *object);
void destroy_step(void *object);

/* print.c */
void print_fields(type_t type, void *object);
void print_cpu(type_t type, void *object);
void print_elapsed(type_t type, void *object);
void print_error(type_t type, void *object);
void print_gid(type_t type, void *object);
void print_group(type_t type, void *object);
void print_idrss(type_t type, void *object);
void print_inblocks(type_t type, void *object);
void print_isrss(type_t type, void *object);
void print_ixrss(type_t type, void *object);
void print_job(type_t type, void *object);
void print_name(type_t type, void *object);
void print_step(type_t type, void *object);
void print_majflt(type_t type, void *object);
void print_minflt(type_t type, void *object);
void print_msgrcv(type_t type, void *object);
void print_msgsnd(type_t type, void *object);
void print_ncpus(type_t type, void *object);
void print_nivcsw(type_t type, void *object);
void print_nodes(type_t type, void *object);
void print_nsignals(type_t type, void *object);
void print_nswap(type_t type, void *object);
void print_ntasks(type_t type, void *object);
void print_nvcsw(type_t type, void *object);
void print_outblocks(type_t type, void *object);
void print_partition(type_t type, void *object);
void print_psize(type_t type, void *object);
void print_rss(type_t type, void *object);
void print_status(type_t type, void *object);
void print_submitted(type_t type, void *object);
void print_systemcpu(type_t type, void *object);
void print_uid(type_t type, void *object);
void print_user(type_t type, void *object);
void print_usercpu(type_t type, void *object);
void print_vsize(type_t type, void *object);

/* options.c */
int decode_status_char(char *status);
char *decode_status_int(int status);
int get_data(void);
void parse_command_line(int argc, char **argv);
void do_dump(void);
void do_expire(void);
void do_fdump(char* fields[], int lc);
void do_help(void);
void do_list(void);
void sacct_init();
void sacct_fini();

#endif /* !_SACCT_H */
