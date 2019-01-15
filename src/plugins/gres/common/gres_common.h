/*****************************************************************************\
 *  gres_common.h - common functions for gres plugins
 *****************************************************************************
 *  Copyright (C) 2017 SchedMD LLC
 *  Written by Danny Auble <da@schedmd.com>
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

#ifndef _HAVE_GRES_COMMON_H
#define _HAVE_GRES_COMMON_H

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/slurm_xlator.h"

#include "src/common/gres.h"
#include "src/common/list.h"

/*
 * Common validation for what was read in from the gres.conf.
 * IN gres_conf_list
 * IN gres_name
 * OUT gres_devices
 */
extern int common_node_config_load(List gres_conf_list,
				   char *gres_name,
				   List *gres_devices);

/*
 * Test if GRES env variables should be set to global device ID or a device
 * ID that always starts at zero (based upon what the application can see).
 * RET true if TaskPlugin=task/cgroup AND ConstrainDevices=yes (in cgroup.conf).
 */
extern bool common_use_local_device_index(void);

/* set the environment for a job/step with the appropriate values */
extern void common_gres_set_env(List gres_devices, char ***env_ptr,
				void *gres_ptr, int node_inx,
				bitstr_t *usable_gres, char *prefix,
				int *local_inx, uint64_t *gres_per_node,
				char **local_list, char **global_list,
				bool reset, bool is_job, int *global_id);

/* Send GRES information from slurmd on the specified file descriptor */
extern void common_send_stepd(int fd, List gres_devices);

/* Receive GRES information from slurmd on the specified file descriptor */
extern void common_recv_stepd(int fd, List *gres_devices);

/*
 * A one-liner version of _print_gres_conf_full()
 */
extern void print_gres_conf(gres_slurmd_conf_t *gres_slurmd_conf,
			    log_level_t log_lvl);
/*
 * Print each gres_slurmd_conf_t record in the list
 */
extern void print_gres_list(List gres_list, log_level_t log_lvl);

/*
 * Print each gres_slurmd_conf_t record in the list in a parsable manner for
 * test consumption
 */
extern void print_gres_list_parsable(List gres_list);
#endif
