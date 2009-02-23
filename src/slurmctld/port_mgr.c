/*****************************************************************************\
 *  port_mgr.c - manage the reservation of I/O ports on the nodes.
 *	Design for use with OpenMPI.
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "src/common/bitstring.h"
#include "src/common/hostlist.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmctld/slurmctld.h"

bitstr_t **port_resv_table = (bitstr_t **) NULL;
int        port_resv_cnt   = 0;
int        port_resv_min   = 0;
int        port_resv_max   = 0;

/* Reserve ports for a job step
 * RET SLURM_SUCCESS or an error code */
extern int reserve_ports(struct step_record *step_ptr)
{
	int i, port_inx;
	int *port_array = NULL;
	char port_str[16];
	hostlist_t hl;

	if (step_ptr->resv_port_cnt > port_resv_cnt) {
		info("step %u.%u needs %u reserved ports, but only %d exist",
		     step_ptr->job_ptr->job_id, step_ptr->step_id,
		     step_ptr->resv_port_cnt, port_resv_cnt);
		return ESLURM_PORTS_INVALID;
	}

	/* Identify available ports */
	port_array = xmalloc(sizeof(int) * step_ptr->resv_port_cnt);
	port_inx = 0;
	for (i=0; i<port_resv_cnt; i++) {
		if (bit_overlap(step_ptr->step_node_bitmap,
				port_resv_table[i]))
			continue;
		port_array[port_inx++] = i;
		if (port_inx >= step_ptr->resv_port_cnt)
			break;
	}
	if (port_inx < step_ptr->resv_port_cnt) {
		info("insufficient ports for step %u.%u to reserve (%d of %u)",
		     step_ptr->job_ptr->job_id, step_ptr->step_id,
		     port_inx, step_ptr->resv_port_cnt);
		xfree(port_array);
		return ESLURM_PORTS_BUSY;
	}

	/* Reserve selected ports */
	hl = hostlist_create(NULL);
	if (hl == NULL)
		fatal("malloc: hostlist_create");
	for (i=0; i<port_inx; i++) {
		bit_or(port_resv_table[port_array[i]], 
		       step_ptr->step_node_bitmap);
		snprintf(port_str, sizeof(port_str), 
			 "%d", (port_array[i] + port_resv_min));
		hostlist_push(hl, port_str);
	}
	hostlist_sort(hl);
	for (i=1024; ; i*=2) {
		step_ptr->resv_ports = xmalloc(i);
		if (hostlist_ranged_string(hl, i, step_ptr->resv_ports) >= 0)
			break;
		xfree(step_ptr->resv_ports);
	}
	hostlist_destroy(hl);
	xfree(port_array);
	info("reserved ports %s for step %u.%u",
	     step_ptr->resv_ports,
	     step_ptr->job_ptr->job_id, step_ptr->step_id);

	return SLURM_SUCCESS;
}

