/*****************************************************************************\
 *  resource_info.h - Functions to determine number of available resources 
 *  $Id: slurm_resource_info.h,v 1.6 2006/10/04 18:53:13 palermo Exp $
 *****************************************************************************
 *  Copyright (C) 2006 Hewlett-Packard Development Company, L.P.
 *  Written by Susanne M. Balle, <susanne.balle@hp.com>
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

int slurm_get_avail_procs(const uint16_t mxsockets,
			  const uint16_t mxcores,
			  const uint16_t mxthreads,
			  const uint16_t minsockets,
			  const uint16_t mincores,
			  const uint16_t cpuspertask,
			  const uint16_t ntaskspernode,
			  const uint16_t ntaskspersocket,
			  const uint16_t ntaskspercore,
			  uint16_t *cpus, 
			  uint16_t *sockets, 
			  uint16_t *cores, 
			  uint16_t *threads,
			  const uint16_t *alloc_cores,
			  const select_type_plugin_info_t cr_type,
			  uint32_t job_id, char *name);

void slurm_print_cpu_bind_help(void);
void slurm_print_mem_bind_help(void);

void slurm_sprint_cpu_bind_type(char *str, cpu_bind_type_t cpu_bind_type);
void slurm_sprint_mem_bind_type(char *str, mem_bind_type_t mem_bind_type);

int slurm_verify_cpu_bind(const char *arg, char **cpu_bind, 
			  cpu_bind_type_t *flags);
int slurm_verify_mem_bind(const char *arg, char **mem_bind, 
			  mem_bind_type_t *flags);

#endif /* !_RES_INFO_H */
