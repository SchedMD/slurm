/*****************************************************************************\
 *  resctrl_conf.h - resctrl.yaml configuration
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#ifndef _COMMON_RESCTRL_CONF_H
#define _COMMON_RESCTRL_CONF_H

#include "src/common/list.h"

#define RESCTRL_CONF_FILE "resctrl.yaml"

/* The groups resctrl.yaml must define; steps are placed into one of them */
#define RESCTRL_GROUP_CPU_ONLY "cpu_only"
#define RESCTRL_GROUP_GPU_ONE_PER_SOCKET "gpu_one_per_socket"
#define RESCTRL_GROUP_GPU_TWO_PER_SOCKET "gpu_two_per_socket"

/* One CTRL_MON group described in resctrl.yaml */
typedef struct {
	char *name; /* directory name under the resctrl mount point */
	char *schemata; /* contents written to the schemata file */
} resctrl_conf_group_t;

/* Contents of resctrl.yaml */
typedef struct {
	list_t *groups; /* list of resctrl_conf_group_t * */
	char *mount_options; /* options used when Slurm mounts resctrl */
} resctrl_conf_t;

/*
 * Free one group definition.
 * IN group - group to free, may be NULL
 */
extern void resctrl_conf_group_free(resctrl_conf_group_t *group);

/*
 * Free a parsed resctrl.yaml.
 * IN conf - configuration to free, may be NULL
 */
extern void resctrl_conf_free(resctrl_conf_t *conf);

/*
 * Read and validate resctrl.yaml from the Slurm configuration directory.
 * Every RESCTRL_GROUP_* must be defined exactly once, with a schemata, and
 * no other group is accepted.
 * OUT conf_out - the configuration, to free with resctrl_conf_free()
 * RET SLURM_SUCCESS, ENOENT when the file does not exist, or an error
 */
extern int resctrl_conf_load(resctrl_conf_t **conf_out);

/*
 * Look up a group definition by name.
 * IN conf - configuration returned by resctrl_conf_load()
 * IN name - group name
 * RET the group or NULL (do not free)
 */
extern resctrl_conf_group_t *resctrl_conf_find_group(resctrl_conf_t *conf,
						     const char *name);

#endif
