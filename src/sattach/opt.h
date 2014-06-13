/*****************************************************************************\
 *  opt.h - definitions for sattach option processing
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona1@llnl.gov>,
 *    Christopher J. Morrone <morrone2@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
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
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifndef _HAVE_OPT_H
#define _HAVE_OPT_H

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <time.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/macros.h" /* true and false */
#include "src/common/env.h"


/* global variables relating to user options */
extern int _verbose;

typedef struct sbatch_options {
	char *progname;		/* argv[0] of this program or
				 * configuration file if multi_prog */
	char *user;		/* local username		*/
	uid_t uid;		/* local uid			*/
	gid_t gid;		/* local gid			*/
	uid_t euid;		/* effective user --uid=user	*/
	gid_t egid;		/* effective group --gid=group	*/
	char *job_name;		/* --job-name=,     -J name	*/
	uint32_t jobid;
	uint32_t stepid;
	bool jobid_set;		/* true of jobid explicitly set */
	int quiet;
	int verbose;
	char *ctrl_comm_ifhn;
	bool labelio;
	slurm_step_io_fds_t fds;
	bool layout_only;
	bool debugger_test;
	uint32_t input_filter;
	bool input_filter_set;
	uint32_t output_filter;
	bool output_filter_set;
	uint32_t error_filter;
	bool error_filter_set;
	bool pty;		/* --pty			*/
} opt_t;

extern opt_t opt;
extern int error_exit;

/* process options:
 * 1. set defaults
 * 2. update options with env vars
 * 3. update options with commandline args
 * 4. perform some verification that options are reasonable
 */
int initialize_and_process_args(int argc, char *argv[]);

/* set options based upon commandline args */
void set_options(const int argc, char **argv);


#endif	/* _HAVE_OPT_H */
