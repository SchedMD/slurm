/*****************************************************************************\
 * src/slurmd/common/fname.c - IO filename creation routine
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
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "src/slurmd/common/fname.h"
#include "src/slurmd/slurmd/slurmd.h"

#include "src/common/uid.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/print_fields.h"

extern char *fname_create(stepd_step_rec_t *step, char *stdio_path, int taskid)
{
	job_std_pattern_t job_stp = { 0 };

	job_stp.array_job_id = step->array_job_id;
	job_stp.array_task_id = step->array_task_id;
	job_stp.first_step_id = step->step_id.step_id;
	job_stp.first_step_node = conf->hostname;
	job_stp.jobid = step->step_id.job_id;
	job_stp.jobname = getenvp(step->env, "SLURM_JOB_NAME");
	job_stp.nodeid = step->nodeid;
	job_stp.taskid = taskid;
	job_stp.user = step->user_name;
	job_stp.work_dir = step->cwd;

	return expand_stdio_fields(stdio_path, &job_stp);
}

/*
 * Return >= 0 if fmt specifies "single task only" IO
 *  i.e. if it specifies a single integer only
 */
extern int fname_single_task_io (const char *fmt)
{
	unsigned long taskid;
	char *p;

	taskid = strtoul (fmt, &p, 10);

	if (*p == '\0')
		return (int)taskid;

	return -1;
}

/* remove_path_slashes()
 *
 * If there are \ chars in the path strip the escaping ones.
 * The new path will tell the caller not to translate escaped characters.
 */
extern char *remove_path_slashes(char *p)
{
	char *buf, *pp;
	bool t;
	int i;

	if (p == NULL)
		return NULL;

	buf = xmalloc(strlen(p) + 1);
	t = false;
	i = 0;

	pp = p;
	++pp;
	while (*p) {
		if (*p == '\\' && *pp == '\\') {
			t = true;
			buf[i] = *pp;
			++i;
			p = p + 2;
			pp = pp + 2;
		} else if (*p == '\\') {
			t = true;
			++p;
			++pp;
		} else {
			buf[i] = *p;
			++i;
			++p;
			++pp;
		}
	}

	if (t == false) {
		xfree(buf);
		return NULL;
	}

	return buf;
}
