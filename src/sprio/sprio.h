/****************************************************************************\
 *  sprio.h - definitions used for printing job queue state
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Don Lipari <lipari1@llnl.gov>
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

#ifndef __SPRIO_H__
#define __SPRIO_H__

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
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/slurmdb_defs.h"
#include "src/sprio/print.h"

struct sprio_parameters {
	bool federation;
	bool job_flag;
	bool local;
	bool long_list;
	bool no_header;
	bool normalized;
	bool sibling;
	bool weights;

	int  verbose;

	List clusters;

	char* format;
	char* jobs;
	char* parts;
	char* users;
	char* sort;

	List  format_list;
	List  job_list;
	List  user_list;
};

/********************
 * Global Variables *
 ********************/
extern struct sprio_parameters params;
extern uint32_t max_age; /* time when not to add any more */
extern uint32_t weight_age; /* weight for age factor */
extern uint32_t weight_assoc; /* weight for Assoc factor */
extern uint32_t weight_fs; /* weight for Fairshare factor */
extern uint32_t weight_js; /* weight for Job Size factor */
extern uint32_t weight_part; /* weight for Partition factor */
extern uint32_t weight_qos; /* weight for QOS factor */
extern char    *weight_tres; /* weight str TRES factors */

extern void parse_command_line( int argc, char* *argv );
extern int  parse_format( char* format );
extern void sort_job_list(List job_list);

#endif
