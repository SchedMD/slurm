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

#define ERROR 2

/* slurmd uses "(uint32_t) -2" to track data for batch allocations
 * which have no logical jobsteps. */
#define NOT_JOBSTEP "4294967294"

#define BRIEF_FIELDS "jobstep,status,error"
#define DEFAULT_FIELDS "jobstep,jobname,partition,ncpus,status,error"
#define LONG_FIELDS "jobstep,usercpu,systemcpu,minflt,majflt,nprocs,ncpus,elapsed,status,error"

#define BUFFER_SIZE 4096
#define MINBUFSIZE 1024

/* The following literals define how many significant characters
 * are used in a yyyymmddHHMMSS timestamp. */
#define MATCH_DAY 8
#define MATCH_HOUR 10
#define MATCH_MINUTE 12

#define MAX_JOBNAME_LENGTH 256
#define MAX_JOBS 100000
#define MAX_JOBSTEPS 500000
#define MAX_PARTITIONS 1000
#define MAX_PRINTFIELDS 100
#define MAX_STATES 100

#define MAX_RECORD_FIELDS 38

#define NO_JOBSTEP -1

#define SECONDS_IN_MINUTE 60
#define SECONDS_IN_HOUR (60*SECONDS_IN_MINUTE)
#define SECONDS_IN_DAY (24*SECONDS_IN_HOUR)

#define TIMESTAMP_LENGTH 15

#ifndef SLURM_CONFIG_FILE
#define SLURM_CONFIG_FILE "sacct was built with no default slurm.conf path"
#endif

/* Map field names to positions */

/* Fields common to all records */
#define F_JOB		0
#define F_PARTITION	1
#define F_SUBMITTED	2
#define F_STARTTIME	3
#define F_UIDGID	4
#define F_RESERVED1	5
#define F_RECTYPE	6
#define F_RECVERSION	7
#define F_NUMFIELDS	8

/* JOB_START fields */
#define F_UID		9
#define F_GID		10
#define F_JOBNAME	11
#define F_BATCH		12
#define F_PRIORITY	13
#define F_NCPUS		14
#define F_NODES		15

/* JOB_STEP fields */
#define F_JOBSTEP	9
#define F_FINISHED	10
#define F_CSTATUS	11
#define F_ERROR		12
#define F_NPROCS	13
/*define F_NCPUS 14 (defined above) */
#define F_ELAPSED	15
#define F_CPU_SEC	16
#define F_CPU_USEC	17
#define F_USER_SEC	18
#define F_USER_USEC	19
#define F_SYS_SEC	20
#define F_SYS_USEC	21
#define F_RSS		22
#define F_IXRSS		23
#define F_IDRSS		24
#define F_ISRSS		25
#define F_MINFLT	26
#define F_MAJFLT	27
#define F_NSWAP		28
#define F_INBLOCKS	29
#define F_OUBLOCKS	30
#define F_MSGSND	31
#define F_MSGRCV	32
#define F_NSIGNALS	33
#define F_NVCSW		34
#define F_NIVCSW	35
#define F_VSIZE		36
#define F_PSIZE		37

/* JOB_COMPLETION fields */
#define F_TOT_ELAPSED	9 
/*define F_FINISHED	10 */
/*define F_CSTATUS	11 */

struct {
	int	job_start_seen,		/* useful flags */
		job_step_seen,
		job_terminated_seen,
		jobnum_superseded;	/* older jobnum was reused */
	long	first_jobstep; /* linked list into jobsteps */
	/* fields retrieved from JOB_START and JOB_TERMINATED records */
	long	job;
	char	*partition;
	char	submitted[TIMESTAMP_LENGTH];	/* YYYYMMDDhhmmss */
	time_t	starttime;
	int	uid;
	int	gid;
	char	jobname[MAX_JOBNAME_LENGTH];
	int	batch;
	int	priority;
	long	ncpus;
	long	nprocs;
	char	*nodes;
	char	finished[15];
	char	cstatus[3];
	long	elapsed;
	long	tot_cpu_sec, tot_cpu_usec;
	long	tot_user_sec, tot_user_usec;
	long	tot_sys_sec, tot_sys_usec;
	long	rss, ixrss, idrss, isrss;
	long	minflt, majflt;
	long	nswap;
	long	inblocks, oublocks;
	long	msgsnd, msgrcv;
	long	nsignals;
	long	nvcsw, nivcsw;
	long	vsize, psize;
	/* fields accumulated from JOB_STEP records */
	int	error;
} jobs[MAX_JOBS];

struct {
	long	j;		/* index into jobs */
	long	jobstep;	/* job's step number */
	long	next;		/* linked list of job steps */
	char	finished[15];	/* YYYYMMDDhhmmss */
	char	stepname[MAX_JOBNAME_LENGTH];
	char	cstatus[3];
	int	error;
	long	nprocs, ncpus;
	long	elapsed;
	long	tot_cpu_sec, tot_cpu_usec;
	long	tot_user_sec, tot_user_usec;
	long	tot_sys_sec, tot_sys_usec;
	long	rss, ixrss, idrss, isrss;
	long	minflt, majflt;
	long	nswap;
	long	inblocks, oublocks;
	long	msgsnd, msgrcv;
	long	nsignals;
	long	nvcsw, nivcsw;
	long	vsize, psize;
} jobsteps[MAX_JOBSTEPS];

struct {
	char *job;
	char *step;
} jobstepsSelected[MAX_JOBSTEPS];

typedef struct fields {
	char *name;		/* Specified in --fields= */
	void (*print_routine) ();	/* Who gets to print it? */
} fields_t;


extern fields_t fields[];

/* On output, use fields 12-37 from JOB_STEP */

typedef enum {	HEADLINE,
		UNDERSCORE,
		JOB,
		JOBSTEP
} type_t;

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

extern sacct_parameters_t params;

extern long inputError;		/* Muddle through bad data, but complain! */

extern long Njobs;
extern long Njobsteps;
extern int printFields[MAX_PRINTFIELDS],	/* Indexed into fields[] */
	NprintFields;

/* process.c */
void processJobStart(char *f[]);
void processJobStep(char *f[]);
void processJobTerminated(char *f[]);

/* print.c */
void print_cpu(type_t type, long idx);
void print_elapsed(type_t type, long idx);
void print_error(type_t type, long idx);
void print_finished(type_t type, long idx);
void print_gid(type_t type, long idx);
void print_group(type_t type, long idx);
void print_idrss(type_t type, long idx);
void print_inblocks(type_t type, long idx);
void print_isrss(type_t type, long idx);
void print_ixrss(type_t type, long idx);
void print_job(type_t type, long idx);
void print_name(type_t type, long idx);
void print_step(type_t type, long idx);
void print_majflt(type_t type, long idx);
void print_minflt(type_t type, long idx);
void print_msgrcv(type_t type, long idx);
void print_msgsnd(type_t type, long idx);
void print_ncpus(type_t type, long idx);
void print_nivcsw(type_t type, long idx);
void print_nodes(type_t type, long idx);
void print_nsignals(type_t type, long idx);
void print_nswap(type_t type, long idx);
void print_ntasks(type_t type, long idx);
void print_nvcsw(type_t type, long idx);
void print_outblocks(type_t type, long idx);
void print_partition(type_t type, long idx);
void print_psize(type_t type, long idx);
void print_rss(type_t type, long idx);
void print_status(type_t type, long idx);
void print_submitted(type_t type, long idx);
void print_systemcpu(type_t type, long idx);
void print_uid(type_t type, long idx);
void print_user(type_t type, long idx);
void print_usercpu(type_t type, long idx);
void print_vsize(type_t type, long idx);

/* options.c */
int get_data(void);
void parse_command_line(int argc, char **argv);
void doDump(void);
void doExpire(void);
void doFdump(char* fields[], int lc);
void doHelp(void);
void doList(void);


#endif /* !_SACCT_H */
