/****************************************************************************\
 *  squeue.h - definitions used for printing job queue state
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Joey Ekstrom <ekstrom1@llnl.gov>
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

#ifndef __SQUEUE_H__
#define __SQUEUE_H__

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/slurmdb_defs.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/squeue/print.h"

typedef struct job_step {
	uint32_t array_id;
	slurm_step_id_t step_id;
} squeue_job_step_t;

struct squeue_parameters {
	bool all_flag;
	bool all_states;
	bool array_flag;
	bool federation_flag;
	int  iterate;
	bool job_flag;
	bool local_flag;
	bool sibling_flag;
	bool start_flag;
	bool step_flag;
	bool long_format;
	bool long_list;
	bool no_header;
	bool priority_flag;
	int  verbose;

	char* accounts;
	List clusters;
	uint32_t cluster_flags;
	char* format;
	char* format_long;
	char* jobs;
	char *mimetype; /* --yaml or --json */
	char* names;
	hostset_t nodes;
	char* licenses;
	char* partitions;
	char* qoss;
	char* reservation;
	char* sort;
	char* states;
	char* steps;
	char* users;

	uint32_t job_id;	/* set if request for a single job ID */
	uint32_t user_id;	/* set if request for a single user ID */

	uint32_t convert_flags;

	List  account_list;
	List  format_list;
	List  job_list;
	List  licenses_list;
	List  name_list;
	List  part_list;
	List  qos_list;
	List  state_list;
	List  step_list;
	List  user_list;
};

extern struct squeue_parameters params;

extern void parse_command_line( int argc, char* *argv );
extern int  parse_format( char* format );
extern int  parse_long_format( char* format_long);
extern void sort_job_list( List job_list );
extern void sort_jobs_by_start_time( List job_list );
extern void sort_step_list( List step_list );

#endif
