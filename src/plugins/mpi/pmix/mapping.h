/*****************************************************************************\
 *  mapping.h - routines for compact process mapping representation
 *****************************************************************************
 *  Copyright (C) 2014 Institute of Semiconductor Physics
 *                     Siberian Branch of Russian Academy of Science
 *  Written by Artem Polyakov <artpol84@gmail.com>.
 *  All rights reserved.
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

#ifndef MAPPING_H
#define MAPPING_H

#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>

#include "slurm/slurm.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/xassert.h"

extern char *pack_process_mapping(uint32_t node_cnt,
				  uint32_t task_cnt,
				  uint16_t *tasks,
				  uint32_t **tids);
extern uint32_t *unpack_process_mapping_flat(char *map,
					     uint32_t node_cnt,
					     uint32_t task_cnt,
					     uint16_t *tasks);
extern int unpack_process_mapping(char *map,
				  uint32_t node_cnt,
				  uint32_t task_cnt,
				  uint16_t *tasks,
				  uint32_t **tids);

#endif // MAPPING_H
