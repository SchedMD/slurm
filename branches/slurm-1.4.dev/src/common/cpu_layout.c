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

#include <slurm/slurm_errno.h>

#include "src/common/cpu_layout.h"
#include "src/common/log.h"
#include "src/common/xmalloc.h"
#include "src/common/xassert.h"

extern cpu_layout_t *copy_cpu_layout(cpu_layout_t *cpu_layout_ptr)
{
	int core_inx = 0, i, mem_inx = 0, sock_cnt = 0, sock_inx = 0;
	cpu_layout_t *new_layout = xmalloc(sizeof(cpu_layout_t));

	new_layout->node_cnt = cpu_layout_ptr->node_cnt;
	new_layout->allocated_cores = bit_copy(cpu_layout_ptr->allocated_cores);

	/* Copy memory_reserved and memory_rep_count */
	new_layout->memory_reserved = xmalloc(sizeof(uint32_t) * 
					      new_layout->node_cnt);
	new_layout->memory_rep_count = xmalloc(sizeof(uint32_t) * 
					       new_layout->node_cnt);
	for (i=0; i<new_layout->node_cnt; i++) {
		new_layout->memory_reserved[i] = 
			cpu_layout_ptr->memory_reserved[i];
		new_layout->memory_rep_count[i] = 
			cpu_layout_ptr->memory_rep_count[i];
		mem_inx += new_layout->memory_rep_count[i];
		if (mem_inx >= new_layout->node_cnt)
			break;
	}

	/* Copy sockets_per_node and sockets_rep_count */
	new_layout->sockets_per_node = xmalloc(sizeof(uint32_t) * 
					       new_layout->node_cnt);		
	new_layout->sockets_rep_count = xmalloc(sizeof(uint32_t) * 
					        new_layout->node_cnt);
	for (i=0; i<new_layout->node_cnt; i++) {
		new_layout->sockets_per_node[i] = 
			cpu_layout_ptr->sockets_per_node[i];
		new_layout->sockets_rep_count[i] = 
			cpu_layout_ptr->sockets_rep_count[i];
		sock_cnt += (new_layout->sockets_per_node[i] *
			     new_layout->sockets_rep_count[i]);
		sock_inx += new_layout->sockets_rep_count[i];
		if (sock_inx >= new_layout->node_cnt)
			break;
	}

	/* Copy cores_per_socket and cores_rep_count */
	new_layout->cores_per_socket = xmalloc(sizeof(uint32_t) * 
					       sock_cnt);		
	new_layout->cores_rep_count = xmalloc(sizeof(uint32_t) * 
					      sock_cnt);
	for (i=0; i<sock_cnt; i++) {
		new_layout->cores_per_socket[i] = 
			cpu_layout_ptr->cores_per_socket[i];
		new_layout->cores_rep_count[i] = 
			cpu_layout_ptr->cores_rep_count[i];
		core_inx += new_layout->cores_rep_count[i];
		if (core_inx >= sock_cnt)
			break;
	}
	return new_layout;
}

extern void free_cpu_layout(cpu_layout_t **cpu_layout_pptr)
{
	if (cpu_layout_pptr) {
		cpu_layout_t *cpu_layout_ptr = *cpu_layout_pptr;
		xfree(cpu_layout_ptr->memory_reserved);
		xfree(cpu_layout_ptr->memory_rep_count);
		xfree(cpu_layout_ptr->sockets_per_node);
		xfree(cpu_layout_ptr->sockets_rep_count);
		xfree(cpu_layout_ptr->cores_per_socket);
		xfree(cpu_layout_ptr->cores_rep_count);
		bit_free(cpu_layout_ptr->allocated_cores);
		xfree(cpu_layout_ptr);
		*cpu_layout_pptr = NULL;
	}
}

extern void log_cpu_layout(cpu_layout_t *cpu_layout_ptr)
{
	int bit_inx = 0;
	int core_inx = 0, core_reps = 0;
	int i, j;
	int mem_inx = 0, mem_reps = 0;
	int node_inx;
	int sock_inx = 0, sock_reps = 0;

	xassert(cpu_layout_ptr);
	for (node_inx=0; node_inx<cpu_layout_ptr->node_cnt; node_inx++) {
		info("Node[%d]:", node_inx);

		if (mem_reps >= cpu_layout_ptr->memory_rep_count[mem_inx]) {
			mem_inx++;
			mem_reps = 0;
		}
		info(" Mem:%u MB", cpu_layout_ptr->memory_reserved[mem_inx]);
		mem_reps++;

		if (sock_reps >= cpu_layout_ptr->sockets_rep_count[sock_inx]) {
			sock_inx++;
			sock_reps = 0;
		}
		info(" Sockets:%u", cpu_layout_ptr->sockets_per_node[sock_inx]);
		sock_reps++;

		for (i=0; i<cpu_layout_ptr->sockets_per_node[sock_inx]; i++) {
			if (core_reps >= 
			    cpu_layout_ptr->cores_rep_count[core_inx]) {
				core_inx++;
				core_reps = 0;
			}
			info("  Socket[%d]: Cores:%u", 
			     i, cpu_layout_ptr->cores_per_socket[core_inx]);
			core_reps++;
			for (j=0; j<cpu_layout_ptr->cores_per_socket[core_inx];
			     j++) {
				if (bit_test(cpu_layout_ptr->allocated_cores,
					     bit_inx)) {
					info("  Socket[%d] Core[%d] in use",
					     i, j);
				}
				bit_inx++;
			}
		}
	}
}

extern void pack_cpu_layout(cpu_layout_t *cpu_layout_ptr, Buf buffer)
{
	int i;
	uint32_t core_cnt = 0, core_recs = 0, mem_recs = 0;
	uint32_t sock_cnt = 0, sock_recs = 0;

	pack32(cpu_layout_ptr->node_cnt, buffer);
	for (i=0; i<cpu_layout_ptr->node_cnt; i++) {
		mem_recs += cpu_layout_ptr->memory_rep_count[i];
		if (mem_recs >= cpu_layout_ptr->node_cnt)
			break;
	}
	pack32_array(cpu_layout_ptr->memory_reserved,  (uint32_t) i, buffer);
	pack32_array(cpu_layout_ptr->memory_rep_count, (uint32_t) i, buffer);
	for (i=0; i<cpu_layout_ptr->node_cnt; i++) {
		sock_cnt  += (cpu_layout_ptr->sockets_per_node[i] +
			      cpu_layout_ptr->sockets_rep_count[i]);
		sock_recs += cpu_layout_ptr->sockets_rep_count[i];
		if (sock_recs >= cpu_layout_ptr->node_cnt)
			break;
	}
	pack32_array(cpu_layout_ptr->sockets_per_node,  (uint32_t) i, buffer);
	pack32_array(cpu_layout_ptr->sockets_rep_count, (uint32_t) i, buffer);
	for (i=0; i<sock_cnt; i++) {
		core_cnt  += (cpu_layout_ptr->cores_per_socket[i] +
			      cpu_layout_ptr->cores_rep_count[i]);
		core_recs += cpu_layout_ptr->cores_rep_count[i];
		if (core_recs >= sock_cnt)
			break;
	}
	pack32_array(cpu_layout_ptr->cores_per_socket, (uint32_t) i, buffer);
	pack32_array(cpu_layout_ptr->cores_rep_count,  (uint32_t) i, buffer);
	pack32(core_cnt, buffer);
	pack_bit_fmt(cpu_layout_ptr->allocated_cores, buffer);
}

extern int  unpack_cpu_layout(cpu_layout_t **cpu_layout_pptr, Buf buffer)
{
	char *bit_fmt = NULL;
	uint32_t core_cnt, tmp32;
	cpu_layout_t *cpu_layout = xmalloc(sizeof(cpu_layout_t));

	safe_unpack32(&cpu_layout->node_cnt, buffer);
	safe_unpack32_array(&cpu_layout->memory_reserved,   &tmp32, buffer);
	safe_unpack32_array(&cpu_layout->memory_rep_count,  &tmp32, buffer);
	safe_unpack32_array(&cpu_layout->sockets_per_node,  &tmp32, buffer);
	safe_unpack32_array(&cpu_layout->sockets_rep_count, &tmp32, buffer);
	safe_unpack32_array(&cpu_layout->cores_per_socket,  &tmp32, buffer);
	safe_unpack32_array(&cpu_layout->cores_rep_count,   &tmp32, buffer);
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

