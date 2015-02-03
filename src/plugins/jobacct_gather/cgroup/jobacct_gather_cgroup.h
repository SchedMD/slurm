/*****************************************************************************\
 *  jobacct_gather_cgroup.h - slurm job accounting gather plugin for cgroup.
 *****************************************************************************
 *  Copyright (C) 2011 Bull.
 *  Written by Martin Perry, <martin.perry@bull.com>, who borrowed heavily
 *  from other parts of SLURM
 *  CODE-OCEC-09-009. All rights reserved.
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
 *
 *  This file is patterned after jobcomp_linux.c, written by Morris Jette and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/

#include "src/common/slurm_jobacct_gather.h"
#include "src/common/xcgroup_read_config.h"
#include "src/slurmd/common/xcgroup.h"

extern xcgroup_t task_memory_cg;
extern xcgroup_t task_cpuacct_cg;

extern int jobacct_gather_cgroup_cpuacct_init(
	slurm_cgroup_conf_t *slurm_cgroup_conf);

extern int jobacct_gather_cgroup_cpuacct_fini(
	slurm_cgroup_conf_t *slurm_cgroup_conf);

extern int jobacct_gather_cgroup_cpuacct_attach_task(
	pid_t pid, jobacct_id_t *jobacct_id);

extern int jobacct_gather_cgroup_memory_init(
	slurm_cgroup_conf_t *slurm_cgroup_conf);

extern int jobacct_gather_cgroup_memory_fini(
	slurm_cgroup_conf_t *slurm_cgroup_conf);

extern int jobacct_gather_cgroup_memory_attach_task(
	pid_t pid, jobacct_id_t *jobacct_id);

/* FIXME: Enable when kernel support ready. */
 /* extern xcgroup_t task_blkio_cg; */
/* extern int jobacct_gather_cgroup_blkio_init( */
/* 	slurm_cgroup_conf_t *slurm_cgroup_conf); */

/* extern int jobacct_gather_cgroup_blkio_fini( */
/* 	slurm_cgroup_conf_t *slurm_cgroup_conf); */

/* extern int jobacct_gather_cgroup_blkio_attach_task( */
/* 	pid_t pid, jobacct_id_t *jobacct_id); */

extern char* jobacct_cgroup_create_slurm_cg (xcgroup_ns_t* ns);
