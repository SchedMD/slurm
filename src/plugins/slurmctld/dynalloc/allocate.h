/*****************************************************************************\
 *  allocate.h - dynamic resource allocation
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

#ifndef DYNALLOC_ALLOCATE_H_
#define DYNALLOC_ALLOCATE_H_


#if HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif  /* HAVE_INTTYPES_H */
#else   /* !HAVE_CONFIG_H */
#  include <inttypes.h>
#endif  /*  HAVE_CONFIG_H */

/**
 *	select n nodes from the given node_range_list through rpc
 *
 *  if (flag == mandatory), all requested nodes must be allocated
 *  from node_list; else if (flag == optional), try best to allocate
 *  node from node_list, and the allocation should include all
 *  nodes in the given list that are currently available. If that
 *  isn't enough to meet the node_num_request, then take any other
 *  nodes that are available to fill out the requested number.
 *
 *	IN:
 *		np: number of process to run
 *		request_node_num: requested node number
 *		node_range_list: specified node range to select from
 *		flag: optional or mandatory
 *		timeout: timeout
 *		cpu_bind：e.g., cores, threads, sockets
 *		mem_per_cpu: memory size per CPU (MB)
 *	OUT Parameter:
 *		jobid: slurm jobid
 *		reponse_node_list:
 *		tasks_per_node: like 4(x2) 3,2
 *	RET OUT:
 *		-1 if requested node number is larger than available or timeout
 *		0  successful
 */
int allocate_node_rpc(uint32_t np, uint32_t request_node_num,
		      char *node_range_list, const char *flag,
		      time_t timeout, const char *cpu_bind,
		      uint32_t mem_per_cpu, uint32_t resv_port_cnt,
		      uint32_t *slurm_jobid, char *reponse_node_list,
		      char *tasks_per_node, char *resv_ports);

/**
 *	select n nodes from the given node_range_list directly through
 *	"job_allocate" in slurmctld/job_mgr.c
 *
 *  if (flag == mandatory), all requested nodes must be allocated
 *  from node_list; else if (flag == optional), try best to allocate
 *  node from node_list, and the allocation should include all
 *  nodes in the given list that are currently available. If that
 *  isn't enough to meet the node_num_request, then take any other
 *  nodes that are available to fill out the requested number.
 *
 *	IN:
 *		np: number of process to run
 *		request_node_num: requested node number
 *		node_range_list: specified node range to select from
 *		flag: optional or mandatory
 *		timeout: timeout
 *		cpu_bind: cpu bind type, e.g., cores, socket
 *		mem_per_cpu: memory size per cpu (MB)
 *	OUT Parameter:
 *		slurm_jobid: slurm jobid
 *		reponse_node_list:
 *		tasks_per_node: like 4(x2) 3,2
 *	RET OUT:
 *		-1 if requested node number is larger than available or timeout
 *		0  successful, final_req_node_list is returned
 */
int allocate_node(uint32_t np, uint32_t request_node_num,
		  char *node_range_list, const char *flag,
		  time_t timeout, const char *cpu_bind,
		  uint32_t mem_per_cpu, uint32_t resv_port_cnt,
		  uint32_t *slurm_jobid, char *reponse_node_list,
		  char *tasks_per_node, char *resv_ports);

/**
 *	cancel a job
 *
 *	IN:
 *		job_id: slurm jobid
 *		uid: user id
 *	OUT Parameter:
 *	RET OUT:
 *		-1 failed
 *		0  successful
 */
extern int cancel_job(uint32_t job_id, uid_t uid);

#endif /* DYNALLOC_ALLOCATE_H_ */
