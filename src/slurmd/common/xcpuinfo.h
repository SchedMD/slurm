/*****************************************************************************\
 *  xcpuinfo.h - cpuinfo related primitives headers
 *****************************************************************************
 *  Copyright (C) 2009 CEA/DAM/DIF
 *  Written by Matthieu Hautreux <matthieu.hautreux@cea.fr>
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

#ifndef _XCPUINFO_H_
#define _XCPUINFO_H_


extern int get_procs(uint16_t *procs);

/* read or load topology and write if needed
 * init and destroy topology must be outside this function */
extern int xcpuinfo_hwloc_topo_load(
	void *topology_in, char *topo_file, bool full);
/*
 * Get the node's cpu info.
 *
 * OUT - cpus - cpus per node
 * OUT - boards - boards per node
 * OUT - sockets - sockets per board
 * OUT - cores - cores per socket
 * OUT - threads - threads per core
 * OUT - block_map_size - should be same as cpus
 * OUT - block_map - physical map of cpus
 * OUT - block_map_inv - absolute map of cpus
 *
 * RET SLURM_SUCCESS on success and 1 or 2 on failure.
 */
extern int xcpuinfo_hwloc_topo_get(
	uint16_t *cpus, uint16_t *boards,
	uint16_t *sockets, uint16_t *cores, uint16_t *threads,
	uint16_t *block_map_size,
	uint16_t **block_map, uint16_t **block_map_inv);

/*
 * Initialize xcpuinfo internal data
 *
 * returned values:
 *  - SLURM_ERROR
 *  - SLURM_SUCCESS
 */
int xcpuinfo_init(void);

/*
 * Destroy xcpuinfo internal data
 *
 * returned values:
 *  - SLURM_ERROR
 *  - SLURM_SUCCESS
 */
int xcpuinfo_fini(void);

/*
 * Convert an abstract core range string into a machine-specific CPU range
 * string. Abstract id to machine id conversion is done using block_map.
 * When a core is set in the input, all its threads will be set in the output.
 *
 * Inverse of xcpuinfo_mac_to_abs.
 *
 * Input:  lrange - abstract/logical core spec string.
 * Output: prange - machine/local/physical CPU spec string.
 *         return code - SLURM_SUCCESS if no error, otherwise SLURM_ERROR.
 */
int xcpuinfo_abs_to_mac(char *lrange,char **prange);

/*
 * Convert a machine-specific CPU range string into an abstract core range
 * string. Machine id to abstract id conversion is done using block_map_inv.
 * When a single thread in a core is set in the input, the corresponding core
 * will be set in its output.
 *
 * Inverse of xcpuinfo_abs_to_mac.
 *
 * Input:  in_range - machine/local/physical CPU range string.
 * Output: out_range - abstract/logical core range string. Caller should xfree()
 *         return code - SLURM_SUCCESS if no error, otherwise SLURM_ERROR.
 */
int xcpuinfo_mac_to_abs(char *in_range, char **out_range);

/*
 * Use xcpuinfo internal data to convert an abstract range
 * of cores (slurm internal format) into the equivalent
 * map of cores
 *
 * range is of the form 0-1,4-5
 *
 * on success, the output map must be freed using xfree
 *
 * returned values:
 *  - SLURM_ERROR
 *  - SLURM_SUCCESS
 */
int xcpuinfo_abs_to_map(char* lrange,uint16_t **map,uint16_t *map_size);

#endif
