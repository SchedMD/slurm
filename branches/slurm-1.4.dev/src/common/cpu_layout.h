/*****************************************************************************\
 *  cpu_layout.h - functions to manage data structure identifying specific
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

#ifndef _CPU_LAYOUT_H
#define _CPU_LAYOUT_H

#if HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif			/* HAVE_INTTYPES_H */
#endif

#include "src/common/bitstring.h"
#include "src/common/pack.h"

typedef struct cpu_layout {
	uint32_t	node_cnt;
	uint32_t *	memory_reserved;	/* MB per node */
	uint32_t *	memory_rep_count;
	uint32_t *	sockets_per_node;
	uint32_t *	sockets_rep_count;
	uint32_t *	cores_per_socket;
	uint32_t *	cores_rep_count;
	bitstr_t *	allocated_cores;
} cpu_layout_t;

extern cpu_layout_t *copy_cpu_layout(cpu_layout_t *cpu_layout_ptr);
extern void free_cpu_layout(cpu_layout_t **cpu_layout_pptr);
extern void log_cpu_layout(cpu_layout_t *cpu_layout_ptr);

extern void pack_cpu_layout(cpu_layout_t *cpu_layout_ptr, Buf buffer);
extern int  unpack_cpu_layout(cpu_layout_t **cpu_layout_pptr, Buf buffer);

#endif /* !_CPU_LAYOUT_H */
