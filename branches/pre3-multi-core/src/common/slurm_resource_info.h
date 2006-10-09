/*****************************************************************************\
 *  resource_info.h - Functions to determine number of available resources 
 *  $Id: slurm_resource_info.h,v 1.6 2006/10/04 18:53:13 palermo Exp $
 *****************************************************************************
 *  Copyright (C) 2006 Hewlett-Packard Development Company, L.P.
 *  Written by Susanne M. Balle, <susanne.balle@hp.com>
 *  UCRL-CODE-217948.
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

#ifndef _RES_INFO_H
#define _RES_INFO_H

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

int slurm_get_avail_procs(const int mxsockets,
				 const int mxcores,
				 const int mxthreads,
				 const int cpuspertask,
				 const int ntaskspernode,
				 const int ntaskspersocket,
				 const int ntaskspercore,
				 int *cpus, 
				 int *sockets, 
				 int *cores, 
				 int *threads,
				 const int alloc_sockets,
				 const int alloc_lps,
				 const select_type_plugin_info_t cr_type);

void slurm_sprint_cpu_bind_type(char *str, cpu_bind_type_t cpu_bind_type);
void slurm_sprint_mem_bind_type(char *str, mem_bind_type_t mem_bind_type);

#endif /* !_RES_INFO_H */
