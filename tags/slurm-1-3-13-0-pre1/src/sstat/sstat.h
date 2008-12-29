/*****************************************************************************\
 *  sstat.h - header file for sstat
 *
 *  $Id: sstat.h 7541 2006-03-18 01:44:58Z da $
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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
#ifndef _SSTAT_H
#define _SSTAT_H

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

#define ERROR 2

#define STAT_FIELDS "jobid,vsize,rss,pages,cputime,ntasks,state"

#define BUFFER_SIZE 4096
#define STATE_COUNT 10

#define MAX_PRINTFIELDS 100

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

typedef struct {
	char *opt_field_list;	/* --fields= */
	int opt_help;		/* --help */
	List opt_job_list;	/* --jobs */
	int opt_noheader;	/* can only be cleared */
	int opt_verbose;	/* --verbose */
} sstat_parameters_t;

typedef struct fields {
	char *name;		/* Specified in --fields= */
	void (*print_routine) ();	/* Who gets to print it? */
} fields_t;

extern fields_t fields[];
extern sstat_parameters_t params;

extern List jobs;

extern int printfields[MAX_PRINTFIELDS],	/* Indexed into fields[] */
	nprintfields;

/* process.c */
void find_hostname(uint32_t pos, char *hosts, char *host);
void aggregate_sacct(sacct_t *dest, sacct_t *from);

/* print.c */
void print_cputime(type_t type, void *object);
void print_fields(type_t type, void *object);
void print_jobid(type_t type, void *object);
void print_ntasks(type_t type, void *object);
void print_pages(type_t type, void *object);
void print_rss(type_t type, void *object);
void print_state(type_t type, void *object);
void print_vsize(type_t type, void *object);


/* options.c */
void parse_command_line(int argc, char **argv);

#endif /* !_SACCT_H */
