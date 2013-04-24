/*****************************************************************************\
 *  xcpuinfo.h - cpuinfo related primitives headers
 *****************************************************************************
 *  Copyright (C) 2009 CEA/DAM/DIF
 *  Written by Matthieu Hautreux <matthieu.hautreux@cea.fr>
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

#if HAVE_CONFIG_H
#   include "config.h"
#endif

#ifndef _XCPUINFO_H_
#define _XCPUINFO_H_

#define XCPUINFO_ERROR    1
#define XCPUINFO_SUCCESS  0

extern int get_procs(uint16_t *procs);
extern int get_cpuinfo(uint16_t *cpus, uint16_t *boards,
		       uint16_t *sockets, uint16_t *cores, uint16_t *threads,
		       uint16_t *block_map_size,
		       uint16_t **block_map, uint16_t **block_map_inv);

/*
 * Initialize xcpuinfo internal data
 *
 * returned values:
 *  - XCPUINFO_ERROR
 *  - XCPUINFO_SUCCESS
 */
int xcpuinfo_init(void);

/*
 * Destroy xcpuinfo internal data
 *
 * returned values:
 *  - XCPUINFO_ERROR
 *  - XCPUINFO_SUCCESS
 */
int xcpuinfo_fini(void);

/*
 * Use xcpuinfo internal data to convert an abstract range
 * of cores (slurm internal format) into the machine one
 *
 * range is of the form 0-1,4-5
 *
 * on success, the output range must be freed using xfree
 *
 * returned values:
 *  - XCPUINFO_ERROR
 *  - XCPUINFO_SUCCESS
 */
int xcpuinfo_abs_to_mac(char* lrange,char** prange);

/*
 * Use xcpuinfo internal data to convert a machine range
 * of cores into an abstract one (slurm internal format)
 *
 * range is of the form 0-1,4-5
 *
 * on success, the output range must be freed using xfree
 *
 * returned values:
 *  - XCPUINFO_ERROR
 *  - XCPUINFO_SUCCESS
 */
int xcpuinfo_mac_to_abs(char* lrange,char** prange);

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
 *  - XCPUINFO_ERROR
 *  - XCPUINFO_SUCCESS
 */
int xcpuinfo_abs_to_map(char* lrange,uint16_t **map,uint16_t *map_size);

/*
 * Use xcpuinfo internal data to convert a machine range
 * of cores into the equivalent map of cores
 *
 * range is of the form 0-1,4-5
 *
 * on success, the output map must be freed using xfree
 *
 * returned values:
 *  - XCPUINFO_ERROR
 *  - XCPUINFO_SUCCESS
 */
int xcpuinfo_mac_to_map(char* lrange,uint16_t **map,uint16_t *map_size);

/*
 * Use xcpuinfo internal data to convert a machine map
 * of cores into the equivalent machine range of cores
 *
 * on success, the output map must be freed using xfree
 *
 * returned values:
 *  - XCPUINFO_ERROR
 *  - XCPUINFO_SUCCESS
 */
int xcpuinfo_map_to_mac(uint16_t *map,uint16_t map_size,char** range);

/*
 * Use xcpuinfo internal data to convert an abstract map of cores
 * into the equivalent machine map of cores
 *
 * on success, the output map must be freed using xfree
 *
 * returned values:
 *  - XCPUINFO_ERROR
 *  - XCPUINFO_SUCCESS
 */
int xcpuinfo_absmap_to_macmap(uint16_t *amap,uint16_t amap_size,
			      uint16_t **bmap,uint16_t *bmap_size);

/*
 * Use xcpuinfo internal data to convert a machine map of cores
 * into the equivalent abstract map of cores
 *
 * on success, the output map must be freed using xfree
 *
 * returned values:
 *  - XCPUINFO_ERROR
 *  - XCPUINFO_SUCCESS
 */
int xcpuinfo_macmap_to_absmap(uint16_t *amap,uint16_t amap_size,
			      uint16_t **bmap,uint16_t *bmap_size);

#endif
