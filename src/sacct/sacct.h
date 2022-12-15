/*****************************************************************************\
 *  sacct.h - header file for sacct
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
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

#include "src/common/data.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/list.h"
#include "src/common/hostlist.h"
#include "src/interfaces/jobacct_gather.h"
#include "src/interfaces/accounting_storage.h"
#include "src/interfaces/jobcomp.h"
#include "src/common/print_fields.h"

#define ERROR 2

#define BRIEF_FIELDS "jobid,state,exitcode"
#define BRIEF_COMP_FIELDS "jobid,uid,state"
#define DEFAULT_FIELDS "jobid,jobname,partition,account,alloccpus,state,exitcode"
#define DEFAULT_COMP_FIELDS "jobid,uid,jobname,partition,nnodes,nodelist,state,end"
#define LONG_FIELDS "jobid,jobidraw,jobname,partition,maxvmsize,maxvmsizenode,maxvmsizetask,avevmsize,maxrss,maxrssnode,maxrsstask,averss,maxpages,maxpagesnode,maxpagestask,avepages,mincpu,mincpunode,mincputask,avecpu,ntasks,alloccpus,elapsed,state,exitcode,avecpufreq,reqcpufreqmin,reqcpufreqmax,reqcpufreqgov,reqmem,consumedenergy,maxdiskread,maxdiskreadnode,maxdiskreadtask,avediskread,maxdiskwrite,maxdiskwritenode,maxdiskwritetask,avediskwrite,reqtres,alloctres,tresusageinave,tresusageinmax,tresusageinmaxn,tresusageinmaxt,tresusageinmin,tresusageinminn,tresusageinmint,tresusageintot,tresusageoutmax,tresusageoutmaxn,tresusageoutmaxt,tresusageoutave,tresusageouttot"

#define LONG_COMP_FIELDS "jobid,uid,jobname,partition,nnodes,nodelist,state,start,end,timelimit"

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
		PRINT_ACCOUNT,
		PRINT_ADMIN_COMMENT,
		PRINT_ALLOC_CPUS,
		PRINT_ALLOC_NODES,
		PRINT_TRESA,
		PRINT_TRESR,
		PRINT_ASSOCID,
		PRINT_AVECPU,
		PRINT_ACT_CPUFREQ,
		PRINT_AVEDISKREAD,
		PRINT_AVEDISKWRITE,
		PRINT_AVEPAGES,
		PRINT_AVERSS,
		PRINT_AVEVSIZE,
		PRINT_BLOCKID,
		PRINT_CLUSTER,
		PRINT_COMMENT,
		PRINT_CONSTRAINTS,
		PRINT_CONTAINER,
		PRINT_CONSUMED_ENERGY,
		PRINT_CONSUMED_ENERGY_RAW,
		PRINT_CPU_TIME,
		PRINT_CPU_TIME_RAW,
		PRINT_DB_INX,
		PRINT_DERIVED_EC,
		PRINT_ELAPSED,
		PRINT_ELAPSED_RAW,
		PRINT_ELIGIBLE,
		PRINT_END,
		PRINT_EXITCODE,
		PRINT_EXTRA,
		PRINT_FAILED_NODE,
		PRINT_FLAGS,
		PRINT_GID,
		PRINT_GROUP,
		PRINT_JOBID,
		PRINT_JOBIDRAW,
		PRINT_JOBNAME,
		PRINT_LAYOUT,
		PRINT_LICENSES,
		PRINT_MAXDISKREAD,
		PRINT_MAXDISKREADNODE,
		PRINT_MAXDISKREADTASK,
		PRINT_MAXDISKWRITE,
		PRINT_MAXDISKWRITENODE,
		PRINT_MAXDISKWRITETASK,
		PRINT_MAXPAGES,
		PRINT_MAXPAGESNODE,
		PRINT_MAXPAGESTASK,
		PRINT_MAXRSS,
		PRINT_MAXRSSNODE,
		PRINT_MAXRSSTASK,
		PRINT_MAXVSIZE,
		PRINT_MAXVSIZENODE,
		PRINT_MAXVSIZETASK,
		PRINT_MCS_LABEL,
		PRINT_MINCPU,
		PRINT_MINCPUNODE,
		PRINT_MINCPUTASK,
		PRINT_NNODES,
		PRINT_NODELIST,
		PRINT_NTASKS,
		PRINT_PARTITION,
		PRINT_PLANNED,
		PRINT_PLANNED_CPU,
		PRINT_PLANNED_CPU_RAW,
		PRINT_PRIO,
		PRINT_QOS,
		PRINT_QOSRAW,
		PRINT_REASON,
		PRINT_REQ_CPUFREQ_MIN,
		PRINT_REQ_CPUFREQ_MAX,
		PRINT_REQ_CPUFREQ_GOV,
		PRINT_REQ_CPUS,
		PRINT_REQ_MEM,
		PRINT_REQ_NODES,
		PRINT_RESERVATION,
		PRINT_RESERVATION_ID,
		PRINT_START,
		PRINT_STATE,
		PRINT_SUBMIT,
		PRINT_SUBMIT_LINE,
		PRINT_SUSPENDED,
		PRINT_SYSTEMCPU,
		PRINT_SYSTEM_COMMENT,
		PRINT_TIMELIMIT,
		PRINT_TIMELIMIT_RAW,
		PRINT_TOTALCPU,
		PRINT_TRESUIA,
		PRINT_TRESUIM,
		PRINT_TRESUIMN,
		PRINT_TRESUIMT,
		PRINT_TRESUIMI,
		PRINT_TRESUIMIN,
		PRINT_TRESUIMIT,
		PRINT_TRESUIT,
		PRINT_TRESUOA,
		PRINT_TRESUOM,
		PRINT_TRESUOMN,
		PRINT_TRESUOMT,
		PRINT_TRESUOMI,
		PRINT_TRESUOMIN,
		PRINT_TRESUOMIT,
		PRINT_TRESUOT,
		PRINT_UID,
		PRINT_USER,
		PRINT_USERCPU,
		PRINT_WCKEY,
		PRINT_WCKEYID,
		PRINT_WORK_DIR
} sacct_print_types_t;

typedef struct {
	char *cluster_name;	/* Set if in federated cluster */
	uint32_t convert_flags;	/* --noconvert */
	slurmdb_job_cond_t *job_cond;
	bool opt_array;		/* --array */
	int opt_completion;	/* --completion */
	bool opt_federation;	/* --federation */
	char *opt_field_list;	/* --fields= */
	gid_t opt_gid;		/* running persons gid */
	int opt_help;		/* --help */
	bool opt_local;		/* --local */
	int opt_noheader;	/* can only be cleared */
	uid_t opt_uid;		/* running persons uid */
	int units;		/* --units*/
	bool use_local_uid;	/* --use-local-uid */
	char *mimetype;         /* --yaml or --json */
} sacct_parameters_t;

extern print_field_t fields[];
extern sacct_parameters_t params;

extern List jobs;
extern List print_fields_list;
extern ListIterator print_fields_itr;
extern int field_count;
extern List g_qos_list;
extern List g_tres_list;

/* process.c */
void aggregate_stats(slurmdb_stats_t *dest, slurmdb_stats_t *from);

/* print.c */
void print_fields(type_t type, void *object);

/* options.c */
int  get_data(void);
void parse_command_line(int argc, char **argv);
void do_help(void);
void do_list(int argc, char **argv);
void do_list_completion(void);
void sacct_init(void);
void sacct_fini(void);

#endif /* !_SACCT_H */
