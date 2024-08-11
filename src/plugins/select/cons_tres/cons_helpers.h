/*****************************************************************************\
 *  cons_helpers.h - Helper functions for the select/cons_tres plugin
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
 *  Derived in large part from select/cons_tres plugins
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

#ifndef _CONS_HELPERS_H
#define _CONS_HELPERS_H

#include "src/interfaces/gres.h"
#include "src/slurmctld/slurmctld.h"

/*
 * Get configured DefCpuPerGPU information from a list
 * (either global or per partition list)
 * Returns NO_VAL64 if configuration parameter not set
 */
extern uint64_t cons_helpers_get_def_cpu_per_gpu(list_t *job_defaults_list);

/*
 * Get configured DefMemPerGPU information from a list
 * (either global or per partition list)
 * Returns NO_VAL64 if configuration parameter not set
 */
extern uint64_t cons_helpers_get_def_mem_per_gpu(list_t *job_defaults_list);

/*
 * Bit a core bitmap array of available cores
 * node_bitmap IN - Nodes available for use
 * job_ptr IN - Various things set to restrict cores.
 * RET core bitmap array, one per node. Use free_core_array() to release memory
 */
extern bitstr_t **cons_helpers_mark_avail_cores(
	bitstr_t *node_bitmap, job_record_t *job_ptr);

#endif /* _CONS_HELPERS_H */
