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

#include "src/interfaces/gres.h"
#include "src/common/list.h"
#include "src/interfaces/cgroup.h"

typedef struct {
	bitstr_t *bit_alloc;
	char ***env_ptr;
	gres_internal_flags_t flags;
	int global_id;
	char *global_list;
	uint64_t gres_cnt;
	uint32_t gres_conf_flags;
	List gres_devices;
	bool is_job;
	bool is_task;
	char *local_list;
	char *prefix;
	bitstr_t *usable_gres;
	bool use_dev_num;
} common_gres_env_t;

/* set the environment for a job/step with the appropriate values */
extern void common_gres_set_env(common_gres_env_t *gres_env);

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

/*
 * Set the appropriate env variables for all gpu like gres.
 */
extern void gres_common_gpu_set_env(common_gres_env_t *gres_env);

/*
 * Set environment variables as appropriate for a job's prolog or epilog based
 * GRES allocated to the job.
 *
 * RETURN: 1 if nothing was done, 0 otherwise.
 */
extern bool gres_common_prep_set_env(char ***prep_env_ptr,
				     gres_prep_t *gres_prep,
				     int node_inx, uint32_t gres_conf_flags,
				     List gres_devices);

extern int gres_common_set_env_types_on_node_flags(void *x, void *arg);

#endif
