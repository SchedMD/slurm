/*****************************************************************************\
 *  select_cons_res.h 
 *
 *  $Id: select_cons_res.h,v 1.3 2006/10/31 20:01:38 palermo Exp $
 *****************************************************************************
 *  Copyright (C) 2006 Hewlett-Packard Development Company, L.P.
 *  Written by Susanne M. Balle, <susanne.balle@hp.com>
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

#ifndef _CONS_RES_H
#define _CONS_RES_H

#include <fcntl.h>
#include <stdio.h>
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/node_select.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/slurm_resource_info.h"
#include "src/slurmctld/slurmctld.h"

#include "src/slurmd/slurmd/slurmd.h"

/* node_cr_record keeps track of the resources within a node which 
 * have been reserved by already scheduled jobs. 
 */
/*** NOTE: If any changes are made here, the following data structure has
 ***       persistent state which is maintained by select_cons_res.c:
 ***		select_p_state_save
 ***		select_p_state_restore
 ***		select_p_node_init
 *** 
 *** as well as tracked by version control
 ***		select_cons_res.c:pstate_version
 *** which should be incremented if any changes are made.
 **/
struct node_cr_record {
	struct node_record *node_ptr;	/* ptr to the node that own these resources */
	char *name;		/* reference copy of node_ptr name */
	uint16_t alloc_lps;	/* cpu count reserved by already scheduled jobs */
	uint16_t alloc_sockets;	/* socket count reserved by already scheduled jobs */
	uint16_t num_sockets;	/* number of sockets in alloc_cores */
	uint16_t *alloc_cores;	/* core count per socket reserved by
				 * already scheduled jobs */
	uint32_t alloc_memory;	/* real memory reserved by already scheduled jobs */
	struct node_cr_record *node_next;/* next entry with same hash index */
};


/*** NOTE: If any changes are made here, the following data structure has
 ***       persistent state which is maintained by select_cons_res.c:
 ***		select_p_state_save
 ***		select_p_state_restore
 *** 
 *** as well as tracked by version control
 ***		select_cons_res.c:pstate_version
 *** which should be incremented if any changes are made.
 **/
struct select_cr_job {
	uint32_t job_id;	/* job ID, default set by SLURM        */
	uint16_t state;		/* job state information               */
	uint32_t nprocs;	/* --nprocs=n,      -n n               */
	uint16_t nhosts;	/* number of hosts allocated to job    */
	char **host;		/* hostname vector                     */
	uint16_t *cpus;		/* number of processors on each host   */
	uint16_t *alloc_lps;	/* number of allocated threads/lps on
				 * each host */
	uint16_t *alloc_sockets;/* number of allocated sockets on each
				 * host */
	uint16_t *num_sockets;	/* number of sockets in alloc_cores[node] */
	uint16_t **alloc_cores;	/* number of allocated cores on each
				 * host */
	uint32_t *alloc_memory;	/* number of allocated MB of real
				 * memory on each host */
	uint16_t max_sockets;
	uint16_t max_cores;
	uint16_t max_threads;
	uint16_t min_sockets;
	uint16_t min_cores;
	uint16_t min_threads;
	uint16_t ntasks_per_node;
	uint16_t ntasks_per_socket;
	uint16_t ntasks_per_core;
	uint16_t cpus_per_task;      
	bitstr_t *node_bitmap;	/* bitmap of nodes allocated to job    */
};

struct node_cr_record * find_cr_node_record (const char *name);

void get_resources_this_node(uint16_t *cpus, 
			     uint16_t *sockets, 
			     uint16_t *cores,
			     uint16_t *threads, 
			     struct node_cr_record *this_cr_node,
			     uint16_t *alloc_sockets, 
			     uint16_t *alloc_lps,
			     uint32_t *jobid);

#endif /* !_CONS_RES_H */
