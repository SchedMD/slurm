/*****************************************************************************\
 *  src/srun/fname.h - IO filename type implementation (srun specific)
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "fname.h"
#include "opt.h"

#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/xassert.h"

/*
 * Max zero-padding width allowed
 */
#define MAX_WIDTH 10

static char *_is_path_escaped(char *);

/*
 * Fill in as much of filename as possible from srun, update
 * filename type to one of the io types ALL, NONE, PER_TASK, ONE
 */
fname_t *
fname_create(srun_job_t *job, char *format)
{
	unsigned int wid     = 0;
	unsigned long int taskid  = 0;
	fname_t *fname = NULL;
	char *p, *q, *name, *tmp_env;
	uint32_t array_job_id  = job->jobid;
	uint32_t array_task_id = NO_VAL;
	char *esc;
	char *end;

	fname = xmalloc(sizeof(*fname));
	fname->type = IO_ALL;
	fname->name = NULL;
	fname->taskid = -1;

	/* Handle special  cases
	 */

	if ((format == NULL)
	    || (strncasecmp(format, "all", (size_t) 3) == 0)
	    || (strncmp(format, "-", (size_t) 1) == 0)       ) {
		 /* "all" explicitly sets IO_ALL and is the default */
		return fname;
	}

	if (strcasecmp(format, "none") == 0) {
		/*
		 * Set type to IO_PER_TASK so that /dev/null is opened
		 *  on every node, which should be more efficient
		 */
		fname->type = IO_PER_TASK;
		fname->name = xstrdup ("/dev/null");
		return fname;
	}

	taskid = strtoul(format, &p, 10);
	if ((*p == '\0') && ((int) taskid < opt.ntasks)) {
		fname->type   = IO_ONE;
		fname->taskid = (uint32_t) taskid;
		/* Set the name string to pass to slurmd
		 *  to the taskid requested, so that tasks with
		 *  no IO can open /dev/null.
		 */
		fname->name   = xstrdup (format);
		return fname;
	}

	/* Check if path has escaped characters
	 * in it and prevent them to be expanded.
	 */
	esc = _is_path_escaped(format);
	if (esc) {
		fname->name = esc;
		return fname;
	}

	name = NULL;
	q = p = format;
	while (*p != '\0') {
		if (*p == '%') {
			if (isdigit(*(++p))) {
				unsigned long in_width = 0;
				xmemcat(name, q, p - 1);
				if ((in_width = strtoul(p, &p, 10)) > MAX_WIDTH)
					wid = MAX_WIDTH;
				else
					wid = in_width;
				q = p - 1;
				if (*p == '\0')
					break;
			}

			switch (*p) {
			 case 'a':  /* '%a' => array task id   */
				tmp_env = getenv("SLURM_ARRAY_TASK_ID");
				if (tmp_env)
					array_task_id = strtoul(tmp_env, &end, 10);
				xmemcat(name, q, p - 1);
				xstrfmtcat(name, "%0*u", wid, array_task_id);
				q = ++p;
				break;
			 case 'A':  /* '%A' => array master job id */
				tmp_env = getenv("SLURM_ARRAY_JOB_ID");
				if (tmp_env)
					array_job_id = strtoul(tmp_env, &end, 10);
				xmemcat(name, q, p - 1);
				xstrfmtcat(name, "%0*u", wid, array_job_id);
				q = ++p;
				break;

			 case 't':  /* '%t' => taskid         */
			 case 'n':  /* '%n' => nodeid         */
			 case 'N':  /* '%N' => node name      */

				 fname->type = IO_PER_TASK;
				 if (wid)
					 xstrcatchar(name, '%');
				 p++;
				 break;

			 case 'J':  /* '%J' => "jobid.stepid" */
			 case 'j':  /* '%j' => jobid          */

				 xmemcat(name, q, p - 1);
				 xstrfmtcat(name, "%0*d", wid, job->jobid);

				 if ((*p == 'J') && (job->stepid != NO_VAL))
					 xstrfmtcat(name, ".%d", job->stepid);
				 q = ++p;
				 break;

			 case 's':  /* '%s' => stepid         */
				 xmemcat(name, q, p - 1);
				 xstrfmtcat(name, "%0*d", wid, job->stepid);
				 q = ++p;
				 break;

			 case 'u':  /* '%u' => username       */
				xmemcat(name, q, p - 1);
				xstrfmtcat(name, "%s", opt.user);
				q = ++p;
				break;

			 default:
				 break;
			}
			wid = 0;
		} else
			p++;
	}

	if (q != p)
		xmemcat(name, q, p);

	fname->name = name;
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

/* is_path_escaped()
 *
 * If there are \ chars in the path strip them.
 * The new path will tell the caller not to
 * translate escaped characters.
 */
static char *
_is_path_escaped(char *p)
{
	char *buf;
	bool t;
	int i;

	if (p == NULL)
		return NULL;

	buf = xmalloc((strlen(p) + 1) * sizeof(char));
	t = false;
	i = 0;

	while (*p) {
		if (*p == '\\') {
			t = true;
			++p;
			continue;
		}
		buf[i] = *p;
		++i;
		++p;
	}

	if (t == false) {
		xfree(buf);
		return NULL;
	}
	return buf;
}
