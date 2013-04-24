/*****************************************************************************\
 *  job_ports_list.c - keep the pair of (slurm_jobid, resv_ports) for future release
 *****************************************************************************
 *  Copyright (C) 2012-2013 Los Alamos National Security, LLC.
 *  Written by Jimmy Cao <Jimmy.Cao@emc.com>, Ralph Castain <rhc@open-mpi.org>
 *  All rights reserved.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "job_ports_list.h"

List job_ports_list = NULL;

extern void append_job_ports_item(uint32_t slurm_jobid, uint16_t port_cnt,
				  char *resv_ports, int *port_array)
{
	job_ports_t *item = NULL;

	if (NULL == job_ports_list)
		job_ports_list = list_create(free_job_ports_item_func);

	item = xmalloc(sizeof(job_ports_t));
	item->slurm_jobid = slurm_jobid;
	item->port_cnt = port_cnt;
	item->resv_ports = xstrdup(resv_ports);
	item->port_array = xmalloc(sizeof(int) * port_cnt);
	memcpy(item->port_array, port_array, sizeof(int)*port_cnt);
	list_append (job_ports_list, item);
}

extern void free_job_ports_item_func(void *voiditem)
{
	job_ports_t *item = (job_ports_t *) voiditem;
	if (item) {
		xfree(item->resv_ports);
		xfree(item->port_array);
		xfree(item);
	}
}

extern int find_job_ports_item_func(void *voiditem, void *key)
{
	job_ports_t *item = NULL;
	uint32_t *jobid = NULL;

	item = (job_ports_t *)voiditem;
	jobid = (uint32_t *)key;

	if (item->slurm_jobid == *jobid)
		return 1;
	else
		return 0;
}


extern void print_list()
{
	int i, j;
	ListIterator it = NULL;
	job_ports_t *item = NULL;

	info("count = %d", list_count (job_ports_list));

	/* create iterator! */
	it = list_iterator_create (job_ports_list);
	/* list_next until NULL */
	j = 0;
	while ( NULL != (item = (job_ports_t*)list_next(it)) ) {
		info("j = %d", j++);
		info("item->slurm_jobid = %u", item->slurm_jobid);
		info("item->port_cnt = %d", item->port_cnt);
		info("item->resv_ports = %s", item->resv_ports);
		for (i = 0; i < item->port_cnt; i++) {
			info("item->port_array[i] = %d", item->port_array[i]);
		}
	}
	list_iterator_destroy(it);
}
