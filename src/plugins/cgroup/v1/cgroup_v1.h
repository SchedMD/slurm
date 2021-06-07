/*****************************************************************************\
 *  cgroup_v1.h - Cgroup v1 plugin
 *****************************************************************************
 *  Copyright (C) 2021 SchedMD LLC
 *  Written by Felip Moll <felip.moll@schedmd.com>
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

#ifndef _CGROUP_V1_H
#define _CGROUP_V1_H

#define _GNU_SOURCE		/* For POLLRDHUP, O_CLOEXEC on older glibc */
#include <poll.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/bitstring.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/cgroup.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"
#include "src/plugins/cgroup/common/cgroup_common.h"

#include "xcgroup.h"

// http://lists.debian.org/debian-boot/2012/04/msg00047.html
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__)
#define POLLRDHUP POLLHUP
#define	MS_NOSUID MNT_NOSUID
#define	MS_NOEXEC MNT_NOEXEC
#define	MS_NODEV 0
#define	umount(d) unmount(d, 0)
#else
#include <sys/eventfd.h>
#endif

#define MAX_MOVE_WAIT 5000

/* Functions */
extern int init(void);
extern int fini(void);
extern int cgroup_p_initialize(cgroup_ctl_type_t sub);
extern int cgroup_p_system_create(cgroup_ctl_type_t sub);
extern int cgroup_p_system_addto(cgroup_ctl_type_t sub, pid_t *pids, int npids);
extern int cgroup_p_system_destroy(cgroup_ctl_type_t sub);
extern int cgroup_p_step_create(cgroup_ctl_type_t sub, stepd_step_rec_t *job);
extern int cgroup_p_step_addto(cgroup_ctl_type_t sub, pid_t *pids, int npids);
extern int cgroup_p_step_get_pids(pid_t **pids, int *npids);
extern int cgroup_p_step_suspend();
extern int cgroup_p_step_resume();
extern int cgroup_p_step_destroy(cgroup_ctl_type_t sub);
extern bool cgroup_p_has_pid(pid_t pid);
extern cgroup_limits_t *cgroup_p_root_constrain_get(cgroup_ctl_type_t sub);
extern int cgroup_p_root_constrain_set(cgroup_ctl_type_t sub,
				       cgroup_limits_t *limits);
extern cgroup_limits_t *cgroup_p_system_constrain_get(cgroup_ctl_type_t sub);
extern int cgroup_p_system_constrain_set(cgroup_ctl_type_t sub,
					 cgroup_limits_t *limits);
extern int cgroup_p_user_constrain_set(cgroup_ctl_type_t sub,
				       stepd_step_rec_t *job,
				       cgroup_limits_t *limits);
extern int cgroup_p_job_constrain_set(cgroup_ctl_type_t sub,
				      stepd_step_rec_t *job,
				      cgroup_limits_t *limits);
extern int cgroup_p_step_constrain_set(cgroup_ctl_type_t sub,
				       stepd_step_rec_t *job,
				       cgroup_limits_t *limits);
extern int cgroup_p_step_start_oom_mgr();
extern cgroup_oom_t *cgroup_p_step_stop_oom_mgr(stepd_step_rec_t *job);
extern int cgroup_p_accounting_init();
extern int cgroup_p_accounting_fini();
extern int cgroup_p_task_addto_accounting(pid_t pid, stepd_step_rec_t *job,
					  uint32_t task_id);
extern cgroup_acct_t *cgroup_p_task_get_acct_data(uint32_t taskid);

#endif /* !_CGROUP_V1_H */
