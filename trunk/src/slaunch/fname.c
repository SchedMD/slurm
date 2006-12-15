/*****************************************************************************\
 *  src/slaunch/fname.h - IO filename type implementation (slaunch specific)
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  UCRL-CODE-226842.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "src/slaunch/fname.h"
#include "src/slaunch/opt.h"

#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/xassert.h"

/* 
 * Max zero-padding width allowed
 */
#define MAX_WIDTH 10


/*
 * Fill in as much of filename as possible from slaunch, update
 * filename type to one of the io types ALL, NONE, PER_TASK, ONE
 */
fname_t *
fname_create(char *format, int jobid, int stepid)
{
	unsigned long int wid     = 0;
	unsigned long int taskid  = 0;
	fname_t *fname = NULL;
	char *p, *q, *name;

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

	if (strncasecmp(format, "none", (size_t) 4) == 0) {
		fname->name = xstrdup ("/dev/null");
		return fname;
	}

	taskid = strtoul(format, &p, 10);
	if ((*p == '\0') && ((int) taskid < opt.num_tasks)) {
		fname->type   = IO_ONE;
		fname->taskid = (uint32_t) taskid;
		/* Set the name string to pass to slurmd
		 *  to the taskid requested, so that tasks with
		 *  no IO can open /dev/null.
		 */
		fname->name = xstrdup (format);
		return fname;
	}

	name = NULL;
	q = p = format;
	while (*p != '\0') {
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
				 error("\"%%t\" is ignored for local files");
				 p++;
				 break;
			 case 'n':  /* '%n' => nodeid         */
				 error("\"%%n\" is ignored for local files");
				 p++;
				 break;
			 case 'N':  /* '%N' => node name      */
				 error("\"%%N\" is ignored for local files");
				 p++;
				 break;

			 case 'J':  /* '%J' => "jobid.stepid" */
			 case 'j':  /* '%j' => jobid          */

				 xmemcat(name, q, p - 1);
				 xstrfmtcat(name, "%0*d", wid, jobid);

				 if ((*p == 'J') && (stepid != NO_VAL)) 
					 xstrfmtcat(name, ".%d", stepid);
				 q = ++p;
				 break;

			 case 's':  /* '%s' => stepid         */
				 xmemcat(name, q, p - 1);
				 xstrfmtcat(name, "%0*d", wid, stepid);
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

