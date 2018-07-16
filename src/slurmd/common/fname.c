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

static void _batch_path_check(char **p, char **q, char **name,
			      unsigned int wid, stepd_step_rec_t *job,
			      int taskid);
static char * _create_batch_fname(char *name, char *path,
				  stepd_step_rec_t *job, int taskid);
static char * _create_step_fname(char *name, char *path, stepd_step_rec_t *job,
				 int taskid);
static void _step_path_check(char **p, char **q, char **name, unsigned int wid,
			     bool double_p, stepd_step_rec_t *job, int taskid,
			     int offset);

/*
 * Max zero-padding width
 */
#define MAX_WIDTH 10

/* Create an IO filename from job parameters and the filename format
 * sent from client. Used by slurmstepd. */
extern char *fname_create(stepd_step_rec_t *job, const char *format, int taskid)
{
	char *name = NULL, *orig;
	int id;
	char *esc;

	if (((id = fname_single_task_io(format)) >= 0) && (taskid != id))
		return (xstrdup ("/dev/null"));

	orig = xstrdup(format);
	esc = remove_path_slashes(orig);

	/* If format doesn't specify an absolute pathname, use cwd
	 */
	if (orig[0] != '/') {
		xstrcat(name, job->cwd);
		if (esc) {
			xstrcat(name, esc);
			goto fini;
		}
		if (name[strlen(name)-1] != '/')
			xstrcatchar(name, '/');
	}

	if (esc) {
		/* esc is malloc */
		name = esc;
		goto fini;
	}

	if (job->batch)
		name = _create_batch_fname(name, orig, job, taskid);
	else
		name = _create_step_fname(name, orig, job, taskid);

fini:
	xfree(orig);
	return name;
}

/* Create an IO filename from job parameters and the filename format
 * sent from client. Used by slurmd for prolog errors. */
extern char *fname_create2(batch_job_launch_msg_t *req)
{
	stepd_step_rec_t job;
	char *esc, *name = NULL, *orig = NULL;

	if (req->std_err)
		orig = xstrdup(req->std_err);
	else if (req->std_out)
		orig = xstrdup(req->std_out);
	else
		xstrfmtcat(orig, "slurm-%u.out", req->job_id);
	esc = remove_path_slashes(orig);

	/* If format doesn't specify an absolute pathname, use cwd
	 */
	if (orig[0] != '/') {
		xstrcat(name, req->work_dir);
		if (esc) {
			xstrcat(name, esc);
			goto fini;
		}
		if (name[strlen(name)-1] != '/')
			xstrcatchar(name, '/');
	}

	if (esc) {
		/* esc is malloc */
		name = esc;
		goto fini;
	}

	memset(&job, 0, sizeof(stepd_step_rec_t));
	job.array_job_id	= req->array_job_id;
	job.array_task_id	= req->array_task_id;
	job.jobid		= req->job_id;
//	job->nodeid		= TBD;
	job.stepid		= req->step_id;
	job.uid			= req->uid;
	job.user_name		= req->user_name;
	name = _create_batch_fname(name, orig, &job, 0);

fini:
	xfree(orig);
	return name;
}

static char *_create_batch_fname(char *name, char *path, stepd_step_rec_t *job,
				 int taskid)
{
	unsigned int wid   = 0;
	char *p, *q;
	q = p = path;

	while (*p != '\0') {
		if (*p == '%') {
			if (*(p+1) == '%') {
				p++;
				xmemcat(name, q, p);
				q = ++p;
				continue;
			}
			if (isdigit(*(++p))) {
				unsigned long in_width = 0;
				xmemcat(name, q, p - 1);
				if ((in_width = strtoul(p, &p, 10)) >
				    MAX_WIDTH) {
					wid = MAX_WIDTH;
				} else
					wid = (unsigned int)in_width;
				q = p - 1;
				if (*p == '\0')
					break;
			}

			_batch_path_check(&p, &q, &name, wid, job, taskid);
			wid = 0;
		} else
			p++;
	}

	if (q != p)
		xmemcat(name, q, p);

	return name;
}

static char *_create_step_fname(char *name, char *path, stepd_step_rec_t *job,
				int taskid)
{

	unsigned int wid   = 0;
	char *p, *q;
	bool double_p = false;
	int str_offset = 1;

	q = p = path;
	while (*p != '\0') {
		if (*p == '%') {
			if (*(p+1) == '%') {
				p++;
				double_p = true;
			}

			if (isdigit(*(++p))) {
				unsigned long in_width = 0;
				if ((in_width = strtoul(p, &p, 10)) ==
				    MAX_WIDTH) {
					/* Remove % and double digit 10 */
					str_offset = 3;
				} else
					str_offset = 2;
				wid = (unsigned int)in_width;
				if (*p == '\0')
					break;

			}
			_step_path_check(&p, &q, &name, wid, double_p,
					 job, taskid, str_offset);
			wid = 0;
		} else
			p++;
		double_p = false;
		str_offset = 1;

	}

	if (q != p)
		xmemcat(name, q, p);

	return name;
}

/*
 * Substitute the path option for a step.
 *
 */
static void _step_path_check(char **p, char **q, char **name, unsigned int wid,
			     bool double_p, stepd_step_rec_t *job, int taskid,
			     int offset)
{
	switch (**p) {
	case '%': /* This is in case there is a 3rd %, ie. %%% */
		xmemcat(*name, *q, *p - 1);
		*q = *p;
		break;
	case 't':  /* '%t' => taskid         */
		xmemcat(*name, *q, *p - offset);
		if (!double_p) {
			if (job->pack_task_offset != NO_VAL)
				taskid += job->pack_task_offset;
			xstrfmtcat(*name, "%0*u", wid, taskid);
			(*p)++;
		}
		*q = (*p)++;
		break;
	case 'n':  /* '%n' => nodeid         */
		xmemcat(*name, *q, *p - offset);
		if (!double_p) {
			xstrfmtcat(*name, "%0*u", wid, job->nodeid);
			(*p)++;
		}
		*q = (*p)++;
		break;
	case 'N':  /* '%N' => node name      */
		xmemcat(*name, *q, *p - offset);
		if (!double_p) {
			xstrfmtcat(*name, "%s", conf->hostname);
			(*p)++;
		}
		*q = (*p)++;
		break;
	case 'u':  /* '%u' => user name      */
		if (!job->user_name)
			job->user_name = uid_to_string(job->uid);
		xmemcat(*name, *q, *p - 1);
		if (!double_p) {
			xstrfmtcat(*name, "%s", job->user_name);
			(*p)++;
		}
		*q = (*p)++;
		break;
	default:
		break;
	}
}

/*
 * Substitute the path option for a batch job. These options should mirror
 * those used with "srun" (parsed in fname_create found in
 * src/srun/libsrun/fname.c).
 */
static void _batch_path_check(char **p, char **q, char **name,
			      unsigned int wid, stepd_step_rec_t *job,
			      int taskid)
{

	switch (**p) {
	case 'a':  /* '%a' => array task id   */
		xmemcat(*name, *q, *p - 1);
		xstrfmtcat(*name, "%0*u", wid,
			   job->array_task_id);
		*q = ++(*p);
		break;
	case 'A':  /* '%A' => array master job id */
		xmemcat(*name, *q, *p - 1);
		if (job->array_task_id == NO_VAL)
			xstrfmtcat(*name, "%0*u", wid, job->jobid);
		else
			xstrfmtcat(*name, "%0*u",wid, job->array_job_id);
		*q = ++(*p);
		break;
	case 'J':  /* '%J' => jobid.stepid */
	case 'j':  /* '%j' => jobid        */
		xmemcat(*name, *q, *p - 1);
		xstrfmtcat(*name, "%0*u", wid, job->jobid);
		if ((**p == 'J') && (job->stepid != NO_VAL))
			xstrfmtcat(*name, ".%u", job->stepid);
		*q = ++(*p);
		break;
	case 'n':  /* '%n' => nodeid         */
		xmemcat(*name, *q, *p - 1);
		xstrfmtcat(*name, "%0*u", wid, job->nodeid);
		*q = ++(*p);
		break;
	case 'N':  /* '%N' => node name      */
		xmemcat(*name, *q, *p - 1);
		xstrfmtcat(*name, "%s", conf->hostname);
		*q = ++(*p);
		break;
	case 's':  /* '%s' => step id        */
		xmemcat(*name, *q, *p - 1);
		xstrfmtcat(*name, "%0*u", wid, job->stepid);
		*q = ++(*p);
		break;
	case 't':  /* '%t' => taskid         */
		xmemcat(*name, *q, *p - 1);
		xstrfmtcat(*name, "%0*u", wid, taskid);
		*q = ++(*p);
		break;
	case 'u':  /* '%u' => user name      */
		if (!job->user_name)
			job->user_name = uid_to_string(job->uid);
		xmemcat(*name, *q, *p - 1);
		xstrfmtcat(*name, "%s", job->user_name);
		*q = ++(*p);
		break;
	case 'x':  /* '%x' => job name       */
		xmemcat(*name, *q, *p - 1);
		xstrfmtcat(*name, "%s", getenvp(job->env, "SLURM_JOB_NAME"));
		*q = ++(*p);
		break;
	default:
		break;
	}
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

	buf = xmalloc((strlen(p) + 1) * sizeof(char));
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
