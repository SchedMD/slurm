/*****************************************************************************\
 *  opt.h - definitions for srun option processing
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010-2015 SchedMD LLC <https://www.schedmd.com>
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

#include "src/common/macros.h" /* true and false */
#include "src/common/env.h"
#include "src/common/slurmdb_defs.h"
#include "src/common/slurm_opt.h"

enum wrappers {
	WRPR_START,
	WRPR_BSUB,
	WRPR_PBS,
	WRPR_CNT
};

typedef struct sbatch_env_opts {
	uint32_t cpus_per_task;
	char *   dist;
	char *   mem_bind;
	char *   mem_bind_sort;
	char *   mem_bind_verbose;
	uint32_t ntasks;
	uint32_t ntasks_per_core;
	uint32_t ntasks_per_gpu;
	uint32_t ntasks_per_node;
	uint32_t ntasks_per_socket;
	uint32_t ntasks_per_tres;
	uint32_t plane_size;
	uint16_t threads_per_core;
} sbatch_env_t;

extern slurm_opt_t opt;
extern sbatch_opt_t sbopt;
extern sbatch_env_t het_job_env;
extern int   error_exit;
extern bool  is_het_job;

/*
 * process_options_first_pass()
 *
 * In this first pass we only look at the command line options, and we
 * will only handle a few options (help, usage, quiet, verbose, version),
 * and look for the script name and arguments (if provided).
 *
 * We will parse the environment variable options, batch script options,
 * and all of the rest of the command line options in
 * process_options_second_pass().
 *
 * Return a pointer to the batch script file name if provided on the command
 * line, otherwise return NULL (in which case the script will need to be read
 * from standard input).
 */
extern char *process_options_first_pass(int argc, char **argv);

/* process options:
 * 1. update options with option set in the script
 * 2. update options with env vars
 * 3. update options with commandline args
 * 4. perform some verification that options are reasonable
 *
 * argc IN - Count of elements in argv
 * argv IN - Array of elements to parse
 * argc_off OUT - Offset of first non-parsable element
 * het_job_inx IN - hetjob component ID, zero origin
 * more_het_comps OUT - more hetjob component specifications in script to process
 */
extern void process_options_second_pass(int argc, char **argv, int *argc_off,
					int het_job_inx, bool *more_het_comps,
					const char *file,
					const void *script_body,
					int script_size);

/* external functions available for SPANK plugins to modify the environment
 * exported to the Slurm Prolog and Epilog programs */
extern char *spank_get_job_env(const char *name);
extern int   spank_set_job_env(const char *name, const char *value,
			       int overwrite);
extern int   spank_unset_job_env(const char *name);


extern void init_envs(sbatch_env_t *local_env);
extern void set_envs(char ***array_ptr, sbatch_env_t *local_env,
		     int het_job_offset);

extern char *get_argument(const char *file, int lineno, const char *line,
			  int *skipped);
extern char *next_line(const void *buf, int size, void **state);

/* Translate #BSUB and #PBS directives in job script */
extern bool xlate_batch_script(const char *file, const void *body,
			       int size, int magic);
#endif	/* _HAVE_OPT_H */
