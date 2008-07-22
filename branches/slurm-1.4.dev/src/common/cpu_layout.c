/*****************************************************************************\
 *  cpu_layout.c - functions to manage data structure identifying specific
 *	CPUs allocated to a job, step or partition
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Written by Morris Jette <jette1@llnl.gov>.
 *  LLNL-CODE-402394.
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
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#include <string.h>
#include <slurm/slurm_errno.h>

#include "src/common/cpu_layout.h"
#include "src/common/log.h"
#include "src/common/xmalloc.h"
#include "src/common/xassert.h"
#include <stdlib.h>
#include "src/common/hostlist.h"

extern cpu_layout_t *create_cpu_layout(char *hosts, uint16_t fast_schedule,
		struct node_record * (*node_finder) (char *host_name) )
{
	hostset_t hs;
	char *host_name;
	int core_cnt = 0, host_inx = 0, sock_inx = -1;
	cpu_layout_t *cpu_layout;
	struct node_record *node_ptr;

	xassert(hosts);
	hs = hostset_create(hosts);
	if (!hs) {
		error("create_cpu_layout: Invalid hostlist: %s", hosts);
		return NULL;
	}
	cpu_layout = xmalloc(sizeof(cpu_layout_t));
	cpu_layout->node_cnt = hostset_count(hs);
	cpu_layout->memory_reserved = xmalloc(sizeof(uint32_t) * 
					      cpu_layout->node_cnt);
	cpu_layout->memory_rep_count = xmalloc(sizeof(uint32_t) * 
					       cpu_layout->node_cnt);
	cpu_layout->sockets_per_node = xmalloc(sizeof(uint32_t) * 
					       cpu_layout->node_cnt);
	cpu_layout->cores_per_socket = xmalloc(sizeof(uint32_t) * 
					       cpu_layout->node_cnt);
	cpu_layout->sock_core_rep_count = xmalloc(sizeof(uint32_t) * 
						  cpu_layout->node_cnt);

/*	cpu_layout.memory_reserved initialized to zero */
	cpu_layout->memory_rep_count[0] = cpu_layout->node_cnt;
	while ((host_name = hostset_shift(hs))) {
		node_ptr = node_finder(host_name);
		if (++host_inx > cpu_layout->node_cnt) {
			error("create_cpu_layout: hostlist parsing problem: %s",
			      hosts);
			free(host_name);
			goto fail;
		} else if (node_ptr) {
			uint32_t cores, socks;
			if (fast_schedule) {
				socks = node_ptr->config_ptr->sockets;
				cores = node_ptr->config_ptr->cores;
			} else {
				socks = node_ptr->sockets;
				cores = node_ptr->cores;
			}
			if ((sock_inx < 0) ||
			    (socks != cpu_layout->sockets_per_node[sock_inx]) ||
			    (cores != cpu_layout->cores_per_socket[sock_inx])){
				sock_inx++;
				cpu_layout->sockets_per_node[sock_inx] = socks;
				cpu_layout->cores_per_socket[sock_inx] = cores;
			}
			cpu_layout->sock_core_rep_count[sock_inx]++;
			core_cnt += (cores * socks);
		} else {
			error("create_cpu_layout: Invalid host: %s", host_name);
			free(host_name);
			goto fail;
		}
		free(host_name);
	}
	hostset_destroy(hs);
	cpu_layout->allocated_cores = bit_alloc(core_cnt);
	return cpu_layout;

 fail:	free_cpu_layout(&cpu_layout);
	return NULL;
}

extern cpu_layout_t *copy_cpu_layout(cpu_layout_t *cpu_layout_ptr)
{
	int i, mem_inx = 0, sock_inx = 0;
	cpu_layout_t *new_layout = xmalloc(sizeof(cpu_layout_t));

	xassert(cpu_layout_ptr);
	new_layout->node_cnt = cpu_layout_ptr->node_cnt;
	new_layout->allocated_cores = bit_copy(cpu_layout_ptr->allocated_cores);

	/* Copy memory_reserved and memory_rep_count */
	new_layout->memory_reserved = xmalloc(sizeof(uint32_t) * 
					      new_layout->node_cnt);
	new_layout->memory_rep_count = xmalloc(sizeof(uint32_t) * 
					       new_layout->node_cnt);
	for (i=0; i<new_layout->node_cnt; i++) {
		mem_inx += cpu_layout_ptr->memory_rep_count[i];
		if (mem_inx >= cpu_layout_ptr->node_cnt) {
			i++;
			break;
		}
	}
	memcpy(new_layout->memory_reserved, 
	       cpu_layout_ptr->memory_reserved, (sizeof(uint32_t) * i));
	memcpy(new_layout->memory_rep_count, 
	       cpu_layout_ptr->memory_rep_count, (sizeof(uint32_t) * i));

	/* Copy sockets_per_node, cores_per_socket and core_sock_rep_count */
	new_layout->sockets_per_node = xmalloc(sizeof(uint32_t) * 
					       new_layout->node_cnt);	
	new_layout->cores_per_socket = xmalloc(sizeof(uint32_t) * 
					       new_layout->node_cnt);	
	new_layout->sock_core_rep_count = xmalloc(sizeof(uint32_t) * 
						  new_layout->node_cnt);	
	for (i=0; i<new_layout->node_cnt; i++) {
		sock_inx += cpu_layout_ptr->sock_core_rep_count[i];
		if (sock_inx >= cpu_layout_ptr->node_cnt) {
			i++;
			break;
		}
	}
	memcpy(new_layout->sockets_per_node, 
	       cpu_layout_ptr->sockets_per_node, (sizeof(uint32_t) * i));
	memcpy(new_layout->cores_per_socket, 
	       cpu_layout_ptr->cores_per_socket, (sizeof(uint32_t) * i));
	memcpy(new_layout->sock_core_rep_count, 
	       cpu_layout_ptr->sock_core_rep_count, (sizeof(uint32_t) * i));

	return new_layout;
}

extern void free_cpu_layout(cpu_layout_t **cpu_layout_pptr)
{
	if (cpu_layout_pptr) {
		cpu_layout_t *cpu_layout_ptr = *cpu_layout_pptr;
		xfree(cpu_layout_ptr->memory_reserved);
		xfree(cpu_layout_ptr->memory_rep_count);
		xfree(cpu_layout_ptr->sockets_per_node);
		xfree(cpu_layout_ptr->cores_per_socket);
		xfree(cpu_layout_ptr->sock_core_rep_count);
		if (cpu_layout_ptr->allocated_cores)
			bit_free(cpu_layout_ptr->allocated_cores);
		xfree(cpu_layout_ptr);
		*cpu_layout_pptr = NULL;
	}
}

/* Log the contents of a cpu_layout data structure using info() */
extern void log_cpu_layout(cpu_layout_t *cpu_layout_ptr)
{
	int bit_inx = 0, bit_reps, i;
	int mem_inx = 0, mem_reps = 0;
	int node_inx;
	int sock_inx = 0, sock_reps = 0;

	xassert(cpu_layout_ptr);
	info("====================");
	for (node_inx=0; node_inx<cpu_layout_ptr->node_cnt; node_inx++) {
		info("Node[%d]:", node_inx);

		if (mem_reps >= cpu_layout_ptr->memory_rep_count[mem_inx]) {
			mem_inx++;
			mem_reps = 0;
		}
		mem_reps++;

		if (sock_reps >= 
		    cpu_layout_ptr->sock_core_rep_count[sock_inx]) {
			sock_inx++;
			sock_reps = 0;
		}
		sock_reps++;

		info("  Mem(MB):%u  Sockets:%u  Cores:%u", 
		     cpu_layout_ptr->memory_reserved[mem_inx],
		     cpu_layout_ptr->sockets_per_node[sock_inx],
		     cpu_layout_ptr->cores_per_socket[sock_inx]);

		bit_reps = cpu_layout_ptr->sockets_per_node[sock_inx] *
			   cpu_layout_ptr->cores_per_socket[sock_inx];
		for (i=0; i<bit_reps; i++) {
			if (bit_test(cpu_layout_ptr->allocated_cores,
				     bit_inx)) {
				info("  Socket[%d] Core[%d] in use",
				     (i / cpu_layout_ptr->
				          cores_per_socket[sock_inx]),
				     (i % cpu_layout_ptr->
					  cores_per_socket[sock_inx]));
			}
			bit_inx++;
		}
	}
	info("====================");
}

extern void pack_cpu_layout(cpu_layout_t *cpu_layout_ptr, Buf buffer)
{
	int i;
	uint32_t core_cnt = 0, mem_recs = 0, sock_recs = 0;

	xassert(cpu_layout_ptr);
	pack32(cpu_layout_ptr->node_cnt, buffer);
	for (i=0; i<cpu_layout_ptr->node_cnt; i++) {
		mem_recs += cpu_layout_ptr->memory_rep_count[i];
		if (mem_recs >= cpu_layout_ptr->node_cnt)
			break;
	}
	i++;
	pack32_array(cpu_layout_ptr->memory_reserved,  (uint32_t) i, buffer);
	pack32_array(cpu_layout_ptr->memory_rep_count, (uint32_t) i, buffer);

	for (i=0; i<cpu_layout_ptr->node_cnt; i++) {
		core_cnt += cpu_layout_ptr->sockets_per_node[i] *
			    cpu_layout_ptr->cores_per_socket[i] *
			    cpu_layout_ptr->sock_core_rep_count[i];
		sock_recs += cpu_layout_ptr->sock_core_rep_count[i];
		if (sock_recs >= cpu_layout_ptr->node_cnt)
			break;
	}
	i++;
	pack32_array(cpu_layout_ptr->sockets_per_node,    (uint32_t) i, buffer);
	pack32_array(cpu_layout_ptr->cores_per_socket,    (uint32_t) i, buffer);
	pack32_array(cpu_layout_ptr->sock_core_rep_count, (uint32_t) i, buffer);
	pack32(core_cnt, buffer);
	xassert (core_cnt == bit_size(cpu_layout_ptr->allocated_cores));
	pack_bit_fmt(cpu_layout_ptr->allocated_cores, buffer);
}

extern int  unpack_cpu_layout(cpu_layout_t **cpu_layout_pptr, Buf buffer)
{
	char *bit_fmt = NULL;
	uint32_t core_cnt, tmp32;
	cpu_layout_t *cpu_layout = xmalloc(sizeof(cpu_layout_t));

	xassert(cpu_layout_pptr);
	safe_unpack32(&cpu_layout->node_cnt, buffer);
	safe_unpack32_array(&cpu_layout->memory_reserved,     &tmp32, buffer);
	safe_unpack32_array(&cpu_layout->memory_rep_count,    &tmp32, buffer);
	safe_unpack32_array(&cpu_layout->sockets_per_node,    &tmp32, buffer);
	safe_unpack32_array(&cpu_layout->cores_per_socket,    &tmp32, buffer);
	safe_unpack32_array(&cpu_layout->sock_core_rep_count, &tmp32, buffer);
	safe_unpack32(&core_cnt, buffer);    /* NOTE: Not part of struct */
	safe_unpackstr_xmalloc(&bit_fmt, &tmp32, buffer);
	cpu_layout->allocated_cores = bit_alloc((bitoff_t) core_cnt);
	if (bit_unfmt(cpu_layout->allocated_cores, bit_fmt))
		goto unpack_error;
	xfree(bit_fmt);
	*cpu_layout_pptr = cpu_layout;
	return SLURM_SUCCESS;

  unpack_error:
	xfree(cpu_layout);
	xfree(bit_fmt);
	*cpu_layout_pptr = NULL;
	return SLURM_ERROR;
}

extern int get_cpu_layout_bit(cpu_layout_t *cpu_layout_ptr, uint32_t node_id,
			      uint32_t socket_id, uint32_t core_id)
{
	int i, bit_inx = 0;

	xassert(cpu_layout_ptr);

	for (i=0; i<cpu_layout_ptr->node_cnt; i++) {
		if (cpu_layout_ptr->sock_core_rep_count[i] <= node_id) {
			bit_inx += cpu_layout_ptr->sockets_per_node[i] *
				   cpu_layout_ptr->cores_per_socket[i] *
				   cpu_layout_ptr->sock_core_rep_count[i];
			node_id -= cpu_layout_ptr->sock_core_rep_count[i];
		} else {
			bit_inx += cpu_layout_ptr->sockets_per_node[i] *
				   cpu_layout_ptr->cores_per_socket[i] *
				   node_id;
			bit_inx += cpu_layout_ptr->cores_per_socket[i] *
				   socket_id;
			bit_inx += core_id;
			break;
		}
	}
	i = bit_size(cpu_layout_ptr->allocated_cores);
	if (bit_inx >= i) {
		error("get_cpu_layout_bit: offset >= bitmap size (%d >= %d)",
		      bit_inx, i);
		return 0;
	}

	return bit_test(cpu_layout_ptr->allocated_cores, bit_inx);
}

extern int set_cpu_layout_bit(cpu_layout_t *cpu_layout_ptr, uint32_t node_id,
			      uint32_t socket_id, uint32_t core_id)
{
	int i, bit_inx = 0;

	xassert(cpu_layout_ptr);

	for (i=0; i<cpu_layout_ptr->node_cnt; i++) {
		if (cpu_layout_ptr->sock_core_rep_count[i] <= node_id) {
			bit_inx += cpu_layout_ptr->sockets_per_node[i] *
				   cpu_layout_ptr->cores_per_socket[i] *
				   cpu_layout_ptr->sock_core_rep_count[i];
			node_id -= cpu_layout_ptr->sock_core_rep_count[i];
		} else {
			bit_inx += cpu_layout_ptr->sockets_per_node[i] *
				   cpu_layout_ptr->cores_per_socket[i] *
				   node_id;
			bit_inx += cpu_layout_ptr->cores_per_socket[i] *
				   socket_id;
			bit_inx += core_id;
			break;
		}
	}
	i = bit_size(cpu_layout_ptr->allocated_cores);
	if (bit_inx >= i) {
		error("set_cpu_layout_bit: offset >= bitmap size (%d >= %d)",
		      bit_inx, i);
		return SLURM_ERROR;
	}

	bit_set(cpu_layout_ptr->allocated_cores, bit_inx);
	return SLURM_SUCCESS;
}
