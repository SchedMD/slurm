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
	char *p, *q;

	q = p = format;
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

	return name;
}

