/*****************************************************************************\
 *  sstat.h - header file for sstat
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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
#ifndef _SSTAT_H
#define _SSTAT_H

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
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

#include "src/common/assoc_mgr.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/list.h"
#include "src/common/hostlist.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/slurm_jobcomp.h"
#include "src/common/print_fields.h"

#define STAT_FIELDS "jobid,maxvmsize,maxvmsizenode,maxvmsizetask,avevmsize,maxrss,maxrssnode,maxrsstask,averss,maxpages,maxpagesnode,maxpagestask,avepages,mincpu,mincpunode,mincputask,avecpu,ntasks,avecpufreq,reqcpufreqmin,reqcpufreqmax,reqcpufreqgov,consumedenergy,maxdiskread,maxdiskreadnode,maxdiskreadtask,avediskread,maxdiskwrite,maxdiskwritenode,maxdiskwritetask,avediskwrite,tresusageinave,tresusageinmax,tresusageinmaxn,tresusageinmaxt,tresusageinmin,tresusageinminn,tresusageinmint,tresusageintot,tresusageoutave,tresusageoutmax,tresusageoutmaxn,tresusageoutmaxt,tresusageoutmin,tresusageoutminn,tresusageoutmint,tresusageouttot"

#define STAT_FIELDS_PID "jobid,nodelist,pids"

/* On output, use fields 12-37 from JOB_STEP */

typedef enum {
		PRINT_ACT_CPUFREQ,
		PRINT_AVECPU,
		PRINT_AVEDISKREAD,
		PRINT_AVEDISKWRITE,
		PRINT_AVEPAGES,
		PRINT_AVERSS,
		PRINT_AVEVSIZE,
		PRINT_CONSUMED_ENERGY,
		PRINT_CONSUMED_ENERGY_RAW,
		PRINT_JOBID,
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
		PRINT_MINCPU,
		PRINT_MINCPUNODE,
		PRINT_MINCPUTASK,
		PRINT_NODELIST,
		PRINT_NTASKS,
		PRINT_PIDS,
		PRINT_REQ_CPUFREQ_MIN,
		PRINT_REQ_CPUFREQ_MAX,
		PRINT_REQ_CPUFREQ_GOV,
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
} sstat_print_types_t;


typedef struct {
	int opt_all_steps;	/* --allsteps */
	char *opt_field_list;	/* --fields= */
	int opt_help;		/* --help */
	List opt_job_list;	/* --jobs */
	int opt_noheader;	/* can only be cleared */
	int opt_verbose;	/* --verbose */
	bool pid_format;
	uint32_t convert_flags;
	int units;		/* --units*/
} sstat_parameters_t;

extern List print_fields_list;
extern ListIterator print_fields_itr;
extern print_field_t fields[];
extern sstat_parameters_t params;
extern int field_count;

extern List jobs;

/* print.c */
void print_fields(slurmdb_step_rec_t *step);

/* options.c */
void parse_command_line(int argc, char **argv);

#endif /* !_SSTAT_H */
