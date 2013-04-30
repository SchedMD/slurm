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

#include "sacct.h"


char *find_hostname(uint32_t pos, char *hosts)
{
	hostlist_t hostlist = NULL;
	char *temp = NULL, *host = NULL;

	if (!hosts || (pos == (uint32_t)NO_VAL))
		return NULL;

	hostlist = hostlist_create(hosts);
	temp = hostlist_nth(hostlist, pos);
	if (temp) {
		host = xstrdup(temp);
		free(temp);
	}
	hostlist_destroy(hostlist);
	return host;
}

void aggregate_stats(slurmdb_stats_t *dest, slurmdb_stats_t *from)
{
	/* Means it is a blank record */
	if (from->cpu_min == NO_VAL)
		return;

	if (dest->vsize_max < from->vsize_max) {
		dest->vsize_max = from->vsize_max;
		dest->vsize_max_nodeid = from->vsize_max_nodeid;
		dest->vsize_max_taskid = from->vsize_max_taskid;
	}
	dest->vsize_ave += from->vsize_ave;

	if (dest->rss_max < from->rss_max) {
		dest->rss_max = from->rss_max;
		dest->rss_max_nodeid = from->rss_max_nodeid;
		dest->rss_max_taskid = from->rss_max_taskid;
	}
	dest->rss_ave += from->rss_ave;

	if (dest->pages_max < from->pages_max) {
		dest->pages_max = from->pages_max;
		dest->pages_max_nodeid = from->pages_max_nodeid;
		dest->pages_max_taskid = from->pages_max_taskid;
	}
	dest->pages_ave += from->pages_ave;

	if ((dest->cpu_min > from->cpu_min) || (dest->cpu_min == NO_VAL)) {
		dest->cpu_min = from->cpu_min;
		dest->cpu_min_nodeid = from->cpu_min_nodeid;
		dest->cpu_min_taskid = from->cpu_min_taskid;
	}
	dest->cpu_ave += from->cpu_ave;
	if ((from->consumed_energy == NO_VAL) ||
	    (dest->consumed_energy == NO_VAL))
		dest->consumed_energy = NO_VAL;
	else
		dest->consumed_energy += from->consumed_energy;
	dest->act_cpufreq += from->act_cpufreq;
	if (dest->disk_read_max < from->disk_read_max) {
		dest->disk_read_max = from->disk_read_max;
		dest->disk_read_max_nodeid = from->disk_read_max_nodeid;
		dest->disk_read_max_taskid = from->disk_read_max_taskid;
	}
	dest->disk_read_ave += from->disk_read_ave;
	if (dest->disk_write_max < from->disk_write_max) {
		dest->disk_write_max = from->disk_write_max;
		dest->disk_write_max_nodeid = from->disk_write_max_nodeid;
		dest->disk_write_max_taskid = from->disk_write_max_taskid;
	}
	dest->disk_write_ave += from->disk_write_ave;
}
