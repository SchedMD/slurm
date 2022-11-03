/*****************************************************************************\
 *  slurm_ext_sensors.h - implementation-independent external sensors plugin
 *  definitions
 *****************************************************************************
 *  Written by Bull-HN-PHX/Martin Perry,
 *  Copyright (C) 2013 Bull-HN-PHX
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

#ifndef __SLURM_EXT_SENSORS_H__
#define __SLURM_EXT_SENSORS_H__

#include <inttypes.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurmdb.h"

#include "src/slurmctld/slurmctld.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/list.h"
#include "src/common/xmalloc.h"

extern int ext_sensors_init(void); /* load the plugin */
extern int ext_sensors_fini(void); /* unload the plugin */
extern ext_sensors_data_t *ext_sensors_alloc(void);
extern void ext_sensors_destroy(ext_sensors_data_t *ext_sensors);
extern void ext_sensors_data_pack(ext_sensors_data_t *ext_sensors,
				  buf_t *buffer,
				  uint16_t protocol_version);
extern int ext_sensors_data_unpack(ext_sensors_data_t **ext_sensors,
				   buf_t *buffer, uint16_t protocol_version);

extern int ext_sensors_g_update_component_data(void);
extern int ext_sensors_g_get_stepstartdata(step_record_t *step_rec);
extern int ext_sensors_g_get_stependdata(step_record_t *step_rec);
extern int ext_sensors_g_get_config(void *data);
#endif /*__SLURM_EXT_SENSORS_H__*/
