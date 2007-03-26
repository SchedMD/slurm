/*****************************************************************************\
 *  sacct_types.h - header file for sacct types
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
#ifndef _SACCT_TYPES_H
#define _SACCT_TYPES_H

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

/* slurmd uses "(uint32_t) -2" to track data for batch allocations
 * which have no logical jobsteps. */


#define BUFFER_SIZE 4096

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
	enum job_states	status;
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
	enum job_states	status;
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

/* process.c */
void destroy_acct_header(void *object);
void destroy_job(void *object);
void destroy_step(void *object);



#endif /* !_SACCT_TYPES_H */
