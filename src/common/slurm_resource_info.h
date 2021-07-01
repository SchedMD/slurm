/*****************************************************************************\
 *  resource_info.h - Functions to determine number of available resources
 *****************************************************************************
 *  Copyright (C) 2006 Hewlett-Packard Development Company, L.P.
 *  Written by Susanne M. Balle, <susanne.balle@hp.com>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifndef _RES_INFO_H
#define _RES_INFO_H

#include <stdint.h>

void slurm_print_cpu_bind_help(void);
void slurm_print_mem_bind_help(void);

void slurm_sprint_cpu_bind_type(char *str, cpu_bind_type_t cpu_bind_type);
extern char *slurm_xstr_mem_bind_type(mem_bind_type_t mem_bind_type);

/*
 * verify cpu_bind arguments, set default values as needed
 *
 * we support different launch policy names
 * we also allow a verbose setting to be specified
 *     --cpu-bind=threads
 *     --cpu-bind=cores
 *     --cpu-bind=sockets
 *     --cpu-bind=v
 *     --cpu-bind=rank,v
 *     --cpu-bind=rank
 *     --cpu-bind={MAP_CPU|MASK_CPU}:0,1,2,3,4
 *
 * arg IN - user task binding option
 * cpu_bind OUT - task binding string
 * flags OUT OUT - task binding flags
 * default_cpu_bind IN - default task binding (based upon Slurm configuration)
 * RET SLURM_SUCCESS, SLURM_ERROR (-1) on failure, 1 for return for "help" arg
 */
extern int slurm_verify_cpu_bind(const char *arg, char **cpu_bind,
				 cpu_bind_type_t *flags);

int slurm_verify_mem_bind(const char *arg, char **mem_bind,
			  mem_bind_type_t *flags);

/*
 * Translate a CPU bind string to its equivalent numeric value
 * cpu_bind_str IN - string to translate
 * flags OUT - equlvalent numeric value
 * RET SLURM_SUCCESS or SLURM_ERROR
 */
extern int xlate_cpu_bind_str(char *cpu_bind_str, uint32_t *flags);

#endif /* !_RES_INFO_H */
