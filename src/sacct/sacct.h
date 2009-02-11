/*****************************************************************************\
 *  sacct.h - header file for sacct
 *
 *  $Id: sacct.h 7541 2006-03-18 01:44:58Z da $
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>.
 *  LLNL-CODE-402394.
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
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurm_jobcomp.h"
#include "src/common/print_fields.h"

#define ERROR 2

#define BRIEF_FIELDS "jobid,state,exitcode"
#define BRIEF_COMP_FIELDS "jobid,uid,state"
#define DEFAULT_FIELDS "jobid,jobname,partition,account,alloccpus,state,exitcode"
#define DEFAULT_COMP_FIELDS "jobid,uid,jobname,partition,nnodes,nodes,state,end"
#define LONG_FIELDS "jobid,jobname,partition,maxvmsize,maxvmsizenode,maxvmsizetask,avevmsize,maxrss,maxrssnode,maxrsstask,averss,maxpages,maxpagesnode,maxpagestask,avepages,mincpu,mincpunode,mincputask,avecpu,ntasks,alloccpus,elapsed,state,exitcode"

#define LONG_COMP_FIELDS "jobid,uid,jobname,partition,nnodes,nodes,state,start,end,timelimit"

#define BUFFER_SIZE 4096
#define STATE_COUNT 10

#define MAX_PRINTFIELDS 100
#define FORMAT_STRING_SIZE 34

#define SECONDS_IN_MINUTE 60
#define SECONDS_IN_HOUR (60*SECONDS_IN_MINUTE)
#define SECONDS_IN_DAY (24*SECONDS_IN_HOUR)

/* On output, use fields 12-37 from JOB_STEP */

typedef enum {	HEADLINE,
		UNDERSCORE,
		JOB,
		JOBSTEP,
		JOBCOMP
} type_t;

typedef enum {
		PRINT_ALLOC_CPUS,
		PRINT_ACCOUNT,
		PRINT_ASSOCID,
		PRINT_AVECPU,
		PRINT_AVEPAGES,
		PRINT_AVERSS,
		PRINT_AVEVSIZE,
		PRINT_BLOCKID,
		PRINT_CLUSTER,
		PRINT_CPU_TIME,
		PRINT_CPU_TIME_RAW,
		PRINT_ELAPSED,
		PRINT_ELIGIBLE,
		PRINT_END,
		PRINT_EXITCODE,
		PRINT_GID,
		PRINT_GROUP,
		PRINT_JOBID,
		PRINT_JOBNAME,
		PRINT_MAXPAGES,
		PRINT_MAXPAGESNODE,
		PRINT_MAXPAGESTASK,
		PRINT_MAXRSS,
		PRINT_MAXRSSNODE,
		PRINT_MAXRSSTASK,
		PRINT_MAXVSIZE,
		PRINT_MAXVSIZENODE,
		PRINT_MAXVSIZETASK,
		PRINT_MINCPU,
		PRINT_MINCPUNODE,
		PRINT_MINCPUTASK,
		PRINT_NODELIST,
		PRINT_NNODES,
		PRINT_NTASKS,
		PRINT_PRIO,
		PRINT_PARTITION,
		PRINT_QOS,
		PRINT_QOSRAW,
		PRINT_REQ_CPUS,
		PRINT_RESV,
		PRINT_RESV_CPU,
		PRINT_RESV_CPU_RAW,
		PRINT_START,
		PRINT_STATE,
		PRINT_SUBMIT,
		PRINT_SUSPENDED,
		PRINT_SYSTEMCPU,
		PRINT_TIMELIMIT,
		PRINT_TOTALCPU,
		PRINT_UID,
		PRINT_USER,
		PRINT_USERCPU,
		PRINT_WCKEY,
		PRINT_WCKEYID,
} sacct_print_types_t;

typedef struct {
	acct_job_cond_t *job_cond;
	int opt_completion;	/* --completion */
	int opt_dump;		/* --dump */
	int opt_dup;		/* --duplicates; +1 = explicitly set */
	int opt_fdump;		/* --formattted_dump */
	char *opt_field_list;	/* --fields= */
	int opt_gid;		/* running persons gid */
	int opt_help;		/* --help */
	char *opt_filein;
	int opt_noheader;	/* can only be cleared */
	int opt_allocs;		/* --total */
	int opt_uid;		/* running persons uid */
} sacct_parameters_t;

extern print_field_t fields[];
extern sacct_parameters_t params;

extern List jobs;

extern List print_fields_list;
extern ListIterator print_fields_itr;
extern int field_count;
extern List qos_list;

/* process.c */
char *find_hostname(uint32_t pos, char *hosts);
void aggregate_sacct(sacct_t *dest, sacct_t *from);

/* print.c */
void print_fields(type_t type, void *object);

/* options.c */
int decode_state_char(char *state);
char *decode_state_int(int state);
int get_data(void);
void parse_command_line(int argc, char **argv);
void do_dump(void);
void do_dump_completion(void);
void do_help(void);
void do_list(void);
void do_list_completion(void);
void sacct_init();
void sacct_fini();

#endif /* !_SACCT_H */
