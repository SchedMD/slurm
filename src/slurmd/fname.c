/*****************************************************************************\
 * src/slurmd/fname.c - IO filename creation routine (slurmd specific)
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "src/slurmd/job.h"
#include "src/slurmd/fname.h"
#include "src/slurmd/slurmd.h"

#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/xassert.h"

/*
 * Max zero-padding width 
 */
#define MAX_WIDTH 10

/* Create an IO filename from job parameters and the filename format
 * sent from client
 */
char *
fname_create(slurmd_job_t *job, const char *format, int taskid)
{
	unsigned long int wid   = 0;
	char *name = NULL;
	char *orig = xstrdup(format);
	char *p, *q;
	int id;

	if (((id = fname_single_task_io (format)) >= 0) && (taskid != id)) 
			return (xstrdup ("/dev/null"));

	/* If format doesn't specify an absolute pathname,
	 * use cwd
	 */
	if (orig[0] != '/') {
		xstrcat(name, job->cwd);
		if (name[strlen(name)-1] != '/')
			xstrcatchar(name, '/');
	}

	q = p = orig;
	while(*p != '\0') {
		if (*p == '%') {
			if (isdigit(*(++p))) {
				xmemcat(name, q, p - 1);
				if ((wid = strtoul(p, &p, 10)) > MAX_WIDTH)
					wid = MAX_WIDTH;
				q = p - 1;
				if (*p == '\0')
					break;
			}

			switch (*p) {
			case 't':  /* '%t' => taskid         */
				xmemcat(name, q, p - 1);
				xstrfmtcat(name, "%0*d", wid, taskid);
				q = ++p;
				break;
			case 'n':  /* '%n' => nodeid         */
				xmemcat(name, q, p - 1);
				xstrfmtcat(name, "%0*d", wid, job->nodeid);
				q = ++p;
				break;
			case 'N':  /* '%N' => node name      */
				xmemcat(name, q, p - 1);
				xstrfmtcat(name, "%s", conf->hostname);
				q = ++p;
				break;
			case 'J':
			case 'j':
				xmemcat(name, q, p - 1);
				xstrfmtcat(name, "%0*d", wid, job->jobid);

				if ((*p == 'J') && (job->stepid != NO_VAL))
					xstrfmtcat(name, ".%d", job->stepid);
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

	xfree(orig);
	return name;
}

static int
find_fname(void *obj, void *key)
{
	char *str  = obj;
	char *name = key;

	if (strcmp(str, name) == 0) 
		return 1;
	return 0;
}

static int
_trunc_file(char *path)
{
	int flags = O_CREAT|O_TRUNC|O_WRONLY;
	int fd;
       
	do {
		fd = open(path, flags, 0644);
	} while ((fd < 0) && (errno == EINTR));

	if (fd < 0) {
		error("Unable to open `%s': %m", path);
		return -1;
	} else
		debug3("opened and truncated `%s'", path);

	if (close(fd) < 0)
		error("Unable to close `%s': %m", path);

	return 0;
}

static void
fname_free(void *name)
{
	xfree(name);
}

/*
 * Return >= 0 if fmt specifies "single task only" IO
 *  i.e. if it specifies a single integer only
 */
int fname_single_task_io (const char *fmt)
{
	unsigned long taskid;
	char *p;
	
	taskid = strtoul (fmt, &p, 10);

	if (*p == '\0')
		return ((int) taskid);

	return (-1);
}

int 
fname_trunc_all(slurmd_job_t *job, const char *fmt)
{
	int i, rc = SLURM_SUCCESS;
	char *fname;
	ListIterator filei;
	List files = NULL;

	if (fname_single_task_io (fmt) >= 0)
		return (0);

	files = list_create((ListDelF)fname_free);
	for (i = 0; i < job->ntasks; i++) {
		fname = fname_create(job, fmt, job->task[i]->gid);
		if (!list_find_first(files, (ListFindF) find_fname, fname))
			list_append(files, (void *)fname);
	}	

	filei = list_iterator_create(files);
	while ((fname = list_next(filei))) {
		if ((rc = _trunc_file(fname)) < 0)
			break;
	}
	list_destroy(files);
	return rc;
}
