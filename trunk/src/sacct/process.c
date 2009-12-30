/*****************************************************************************\
 *  process.c - process functions for sacct
 *
 *  $Id: process.c 7541 2006-03-18 01:44:58Z da $
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#include "sacct.h"


char *find_hostname(uint32_t pos, char *hosts)
{
	hostlist_t hostlist = NULL;
	char *temp = NULL, *host = NULL;

	if(!hosts || (pos == (uint32_t)NO_VAL))
		return NULL;

	hostlist = hostlist_create(hosts);
	temp = hostlist_nth(hostlist, pos);
	if(temp) {
		host = xstrdup(temp);
		free(temp);
	}
	hostlist_destroy(hostlist);
	return host;
}

void aggregate_sacct(sacct_t *dest, sacct_t *from)
{
	if(dest->max_vsize < from->max_vsize) {
		dest->max_vsize = from->max_vsize;
		dest->max_vsize_id = from->max_vsize_id;
	}
	dest->ave_vsize += from->ave_vsize;

	if(dest->max_rss < from->max_rss) {
		dest->max_rss = from->max_rss;
		dest->max_rss_id = from->max_rss_id;
	}
	dest->ave_rss += from->ave_rss;

	if(dest->max_pages < from->max_pages) {
		dest->max_pages = from->max_pages;
		dest->max_pages_id = from->max_pages_id;
	}
	dest->ave_pages += from->ave_pages;

	if((dest->min_cpu > from->min_cpu)
	   || (dest->min_cpu == NO_VAL)) {
		dest->min_cpu = from->min_cpu;
		dest->min_cpu_id = from->min_cpu_id;
	}
	dest->ave_cpu += from->ave_cpu;
}
