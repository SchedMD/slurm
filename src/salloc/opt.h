/*****************************************************************************\
 *  opt.h - definitions for salloc option processing
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona1@llnl.gov>,
 *    Christopher J. Morrone <morrone2@llnl.gov>, et. al.
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

#ifndef _HAVE_OPT_H
#define _HAVE_OPT_H

#include "config.h"

#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "src/common/env.h"
#include "src/common/macros.h" /* true and false */
#include "src/common/slurm_opt.h"

extern slurm_opt_t opt;
extern salloc_opt_t saopt;
extern int error_exit;		/* exit code for slurm errors */
extern int immediate_exit;	/* exit code for --immediate option & busy */

/*
 * process options:
 * 1. set defaults
 * 2. update options with env vars
 * 3. update options with commandline args
 * 4. perform some verification that options are reasonable
 *
 * argc      IN - Count of elements in argv
 * argv      IN - Array of elements to parse
 * argc_off OUT - Offset of first non-parsable element
 * het_job_inx  IN - offset of hetjob component
 */
extern int initialize_and_process_args(int argc, char **argv, int *argc_off,
				       int het_job_inx);

/* set options based upon commandline args */
void set_options(const int argc, char **argv);

/* external functions available for SPANK plugins to modify the environment
 * exported to the Slurm Prolog and Epilog programs */
extern char *spank_get_job_env(const char *name);
extern int   spank_set_job_env(const char *name, const char *value,
			       int overwrite);
extern int   spank_unset_job_env(const char *name);

#endif	/* _HAVE_OPT_H */
