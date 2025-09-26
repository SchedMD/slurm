/*****************************************************************************\
 *  fname.c - IO filename type implementation (srun specific)
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
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

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fname.h"
#include "opt.h"

#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/xassert.h"
#include "src/common/print_fields.h"

/*
 * Fill in as much of filename as possible from srun, update
 * filename type to one of the io types ALL, NONE, PER_TASK, ONE
 * These options should mirror those used with "sbatch" (parsed in
 * _batch_path_check found in src/slurmd/common/fname.c)
 */
extern fname_t *fname_create(srun_job_t *job, char *format, int task_count)
{
	job_std_pattern_t job_stp = { 0 };
	unsigned long int taskid = 0;
	fname_t *fname = NULL;
	char *array_job_id, *array_task_id, *p;

	fname = xmalloc(sizeof(*fname));
	fname->type = IO_ALL;
	fname->name = NULL;
	fname->taskid = -1;

	/* Handle special cases */

	if ((format == NULL) ||
	    (xstrncasecmp(format, "all", (size_t) 3) == 0) ||
	    (xstrncmp(format, "-", (size_t) 1) == 0)) {
		/* "all" explicitly sets IO_ALL and is the default */
		return fname;
	}

	if (xstrcasecmp(format, "none") == 0) {
		/*
		 * Set type to IO_PER_TASK so that /dev/null is opened
		 *  on every node, which should be more efficient
		 */
		fname->type = IO_PER_TASK;
		fname->name = xstrdup("/dev/null");
		return fname;
	}

	taskid = strtoul(format, &p, 10);
	if ((*p == '\0') && ((int) taskid < task_count)) {
		fname->type = IO_ONE;
		fname->taskid = (uint32_t) taskid;
		/* Set the name string to pass to slurmd
		 *  to the taskid requested, so that tasks with
		 *  no IO can open /dev/null.
		 */
		fname->name = xstrdup(format);
		return fname;
	}
	array_job_id = getenv("SLURM_ARRAY_JOB_ID");
	job_stp.array_job_id =
		array_job_id ? strtoul(array_job_id, NULL, 10) : 0;
	array_task_id = getenv("SLURM_ARRAY_TASK_ID");
	job_stp.array_task_id =
		array_task_id ? strtoul(array_task_id, NULL, 10) : 0;
	job_stp.first_step_id = job->step_id.step_id;
	job_stp.jobid = job->step_id.job_id;
	job_stp.jobname = getenv("SLURM_JOB_NAME");
	job_stp.is_srun = true;

	fname->name = expand_stdio_fields(format, &job_stp);
	if (job_stp.io_per_task)
		fname->type = IO_PER_TASK;
	return fname;
}

void
fname_destroy(fname_t *f)
{
	if (f->name)
		xfree(f->name);
	xfree(f);
}

char *
fname_remote_string (fname_t *f)
{
	if ((f->type == IO_PER_TASK) || (f->type == IO_ONE))
		return (xstrdup (f->name));

	return (NULL);
}
