/*****************************************************************************\
 *  sacct.h - header file for sacct
 *
 *  $Id: sacct.h 7541 2006-03-18 01:44:58Z da $
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>.
 *  UCRL-CODE-226842.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/list.h"
#include "src/common/hostlist.h"

#include "src/sacct/sacct_stat.h"

#define ERROR 2

/* slurmd uses "(uint32_t) -2" to track data for batch allocations
 * which have no logical jobsteps. */
#define BATCH_JOB_TIMESTAMP 0

#define ACCOUNT_FIELDS "jobid,jobname,start,end,cpu,vsize_short,status,exitcode"
#define BRIEF_FIELDS "jobid,status,exitcode"
#define DEFAULT_FIELDS "jobid,jobname,partition,ncpus,status,exitcode"
#define STAT_FIELDS "jobid,vsize,rss,pages,cputime,ntasks,status"
#define LONG_FIELDS "jobid,jobname,partition,vsize,rss,pages,cputime,ntasks,ncpus,elapsed,status,exitcode"

#define BUFFER_SIZE 4096
#define STATUS_COUNT 10

#define MAX_PRINTFIELDS 100
#define EXPIRE_READ_LENGTH 10
#define MAX_RECORD_FIELDS 100

#define SECONDS_IN_MINUTE 60
#define SECONDS_IN_HOUR (60*SECONDS_IN_MINUTE)
#define SECONDS_IN_DAY (24*SECONDS_IN_HOUR)

#define TIMESTAMP_LENGTH 15

/* Map field names to positions */

/* Fields common to all records */
enum {	F_JOB =	0,
	F_PARTITION,	
	F_JOB_SUBMIT,	
	F_TIMESTAMP,	
	F_UID,	
	F_GID,	
	F_BLOCKID,	
	F_RESERVED2,	
	F_RECTYPE,	
	HEADER_LENGTH
};

/* JOB_START fields */
enum {	F_JOBNAME = HEADER_LENGTH,
	F_TRACK_STEPS,		
	F_PRIORITY,	
	F_NCPUS,		
	F_NODES,
	F_JOB_ACCOUNT,
	JOB_START_LENGTH
};

/* JOB_STEP fields */
enum {	F_JOBSTEP = HEADER_LENGTH,
	F_STATUS,
	F_EXITCODE,
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
	F_MAX_VSIZE,
	F_MAX_VSIZE_TASK,
	F_AVE_VSIZE,
	F_MAX_RSS,
	F_MAX_RSS_TASK,
	F_AVE_RSS,
	F_MAX_PAGES,
	F_MAX_PAGES_TASK,
	F_AVE_PAGES,
	F_MIN_CPU,
	F_MIN_CPU_TASK,
	F_AVE_CPU,
	F_STEPNAME,
	F_STEPNODES,
	F_MAX_VSIZE_NODE,
	F_MAX_RSS_NODE,
	F_MAX_PAGES_NODE,
	F_MIN_CPU_NODE,
	F_STEP_ACCOUNT,
	F_STEP_REQUID,
	JOB_STEP_LENGTH
};

/* JOB_TERM / JOB_SUSPEND fields */
enum {	F_TOT_ELAPSED = HEADER_LENGTH,
	F_TERM_STATUS,
	F_JOB_REQUID,
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
	uint32_t jobnum;
	char	*partition;
	char	*blockid;
	time_t 	job_submit;
	time_t	timestamp;
	uint32_t uid;
	uint32_t gid;
	uint16_t rec_type;
} acct_header_t;

typedef struct job_rec {
	uint32_t	job_start_seen,		/* useful flags */
		job_step_seen,
		job_terminated_seen,
		jobnum_superseded;	/* older jobnum was reused */
	acct_header_t header;
	uint16_t show_full;
	char	*nodes;
	char	*jobname;
	uint16_t track_steps;
	int32_t priority;
	uint32_t ncpus;
	uint32_t ntasks;
	int32_t	status;
	int32_t	exitcode;
	uint32_t elapsed;
	time_t end;
	uint32_t tot_cpu_sec;
	uint32_t tot_cpu_usec;
	struct rusage rusage;
	sacct_t sacct;
	List    steps;
	char    *account;
	uint32_t requid;
} job_rec_t;

typedef struct step_rec {
	acct_header_t   header;
	uint32_t	stepnum;	/* job's step number */
	char	        *nodes;
	char	        *stepname;
	int32_t	        status;
	int32_t	        exitcode;
	uint32_t	ntasks; 
	uint32_t        ncpus;
	uint32_t	elapsed;
	time_t          end;
	uint32_t	tot_cpu_sec;
	uint32_t        tot_cpu_usec;
	struct rusage   rusage;
	sacct_t         sacct;
	char            *account;
	uint32_t requid;
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
	int opt_stat;		/* --stat */
	int opt_gid;		/* --gid (-1=wildcard, 0=root) */
	int opt_header;		/* can only be cleared */
	int opt_help;		/* --help */
	int opt_long;		/* --long */
	int opt_lowmem;		/* --low_memory */
	int opt_purge;		/* --purge */
	int opt_raw;		/* --raw */
	int opt_total;		/* --total */
	int opt_uid;		/* --uid (-1=wildcard, 0=root) */
	int opt_verbose;	/* --verbose */
	long opt_expire;		/* --expire= */ 
	char *opt_expire_timespec; /* --expire= */
	char *opt_field_list;	/* --fields= */
	char *opt_filein;	/* --file */
	char *opt_job_list;	/* --jobs */
	char *opt_partition_list;/* --partitions */
	char *opt_state_list;	/* --states */
} sacct_parameters_t;

extern fields_t fields[];
extern sacct_parameters_t params;

extern long input_error;	/* Muddle through bad data, but complain! */

extern List jobs;

extern int printfields[MAX_PRINTFIELDS],	/* Indexed into fields[] */
	nprintfields;

/* process.c */
void process_start(char *f[], int lc, int show_full, int len);
void process_step(char *f[], int lc, int show_full, int len);
void process_suspend(char *f[], int lc, int show_full, int len);
void process_terminated(char *f[], int lc, int show_full, int len);
void find_hostname(uint32_t pos, char *hosts, char *host);
void aggregate_sacct(sacct_t *dest, sacct_t *from);
void destroy_acct_header(void *object);
void destroy_job(void *object);
void destroy_step(void *object);

/* print.c */
void print_fields(type_t type, void *object);
void print_cpu(type_t type, void *object);
void print_elapsed(type_t type, void *object);
void print_exitcode(type_t type, void *object);
void print_gid(type_t type, void *object);
void print_group(type_t type, void *object);
void print_idrss(type_t type, void *object);
void print_inblocks(type_t type, void *object);
void print_isrss(type_t type, void *object);
void print_ixrss(type_t type, void *object);
void print_job(type_t type, void *object);
void print_name(type_t type, void *object);
void print_jobid(type_t type, void *object);
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
void print_blockid(type_t type, void *object);
void print_pages(type_t type, void *object);
void print_rss(type_t type, void *object);
void print_status(type_t type, void *object);
void print_submit(type_t type, void *object);
void print_start(type_t type, void *object);
void print_end(type_t type, void *object);
void print_systemcpu(type_t type, void *object);
void print_uid(type_t type, void *object);
void print_user(type_t type, void *object);
void print_usercpu(type_t type, void *object);
void print_vsize(type_t type, void *object);
void print_vsize_short(type_t type, void *object);
void print_cputime(type_t type, void *object);
void print_account(type_t type, void *object);

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
void do_stat(void);
void sacct_init();
void sacct_fini();

#endif /* !_SACCT_H */
