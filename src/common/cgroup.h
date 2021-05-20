/*****************************************************************************\
 *  cgroup.h - driver for cgroup plugin
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
#ifndef _CGROUP_H
#define _CGROUP_H

/* Check filesystem type */
#include <linux/magic.h>
#include <sys/vfs.h>

#include "slurm/slurm.h"
#include "src/common/slurm_opt.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

#include "src/common/plugin.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/xcgroup_read_config.h"
#include "src/plugins/cgroup/common/cgroup_common.h"

#ifdef __GNUC__
#define F_TYPE_EQUAL(a, b) (a == (__typeof__(a)) b)
#else
#define F_TYPE_EQUAL(a, b) (a == (__SWORD_TYPE) b)
#endif

/* Current supported cgroup controller types */
typedef enum {
	CG_TRACK,
	CG_CPUS,
	CG_MEMORY,
	CG_DEVICES,
	CG_CPUACCT,
	CG_CTL_CNT
} cgroup_ctl_type_t;

/* This data type is used to get/set various parameters in cgroup hierarchy */
typedef struct {
	/* task cpuset */
	char *allow_cores;
	char *allow_mems;
	size_t cores_size;
	size_t mems_size;
	/* task devices */
	bool allow_device;
	char *device_major;
	/* jobacct memory */
	uint64_t limit_in_bytes;
	uint64_t soft_limit_in_bytes;
	uint64_t kmem_limit_in_bytes;
	uint64_t memsw_limit_in_bytes;
	uint64_t swappiness;
} cgroup_limits_t;

typedef struct {
	uint64_t step_mem_failcnt;
	uint64_t step_memsw_failcnt;
	uint64_t job_mem_failcnt;
	uint64_t job_memsw_failcnt;
	uint64_t oom_kill_cnt;
} cgroup_oom_t;

typedef struct {
	uint64_t usec;
	uint64_t ssec;
	uint64_t total_rss;
	uint64_t total_pgmajfault;
} cgroup_acct_t;

extern void cgroup_free_limits(cgroup_limits_t *limits);

extern int cgroup_g_init(void);
extern int cgroup_g_fini(void);
extern int cgroup_g_initialize(cgroup_ctl_type_t sub);
extern int cgroup_g_system_create(cgroup_ctl_type_t sub);
extern int cgroup_g_system_addto(cgroup_ctl_type_t sub, pid_t *pids, int npids);
extern int cgroup_g_system_destroy(cgroup_ctl_type_t sub);
extern int cgroup_g_step_create(cgroup_ctl_type_t sub, stepd_step_rec_t *job);
extern int cgroup_g_step_addto(cgroup_ctl_type_t sub, pid_t *pids, int npids);
extern int cgroup_g_step_get_pids(pid_t **pids, int *npids);
extern int cgroup_g_step_suspend();
extern int cgroup_g_step_resume();
extern int cgroup_g_step_destroy(cgroup_ctl_type_t sub);
extern bool cgroup_g_has_pid(pid_t pid);
extern void cgroup_g_free_conf(slurm_cgroup_conf_t *cg_conf);
extern slurm_cgroup_conf_t *cgroup_g_get_conf();
extern List cgroup_g_get_conf_list(void);
extern void cgroup_g_reconfig();
extern void cgroup_g_conf_fini();
extern int cgroup_g_write_conf(int fd);
extern int cgroup_g_read_conf(int fd);
extern bool cgroup_g_memcg_job_confinement();
extern cgroup_limits_t *cgroup_g_root_constrain_get(cgroup_ctl_type_t sub);
extern int cgroup_g_root_constrain_set(cgroup_ctl_type_t sub,
				       cgroup_limits_t *limits);
extern cgroup_limits_t *cgroup_g_system_constrain_get(cgroup_ctl_type_t sub);
extern int cgroup_g_system_constrain_set(cgroup_ctl_type_t sub,
					 cgroup_limits_t *limits);
extern int cgroup_g_user_constrain_set(cgroup_ctl_type_t sub,
				       stepd_step_rec_t *job,
				       cgroup_limits_t *limits);
extern int cgroup_g_job_constrain_set(cgroup_ctl_type_t sub,
				      stepd_step_rec_t *job,
				      cgroup_limits_t *limits);
extern int cgroup_g_step_constrain_set(cgroup_ctl_type_t sub,
				       stepd_step_rec_t *job,
				       cgroup_limits_t *limits);
extern int cgroup_g_step_start_oom_mgr();
extern cgroup_oom_t *cgroup_g_step_stop_oom_mgr(stepd_step_rec_t *job);
extern int cgroup_g_accounting_init();
extern int cgroup_g_accounting_fini();
extern int cgroup_g_task_addto_accounting(pid_t pid, stepd_step_rec_t *job,
					  uint32_t task_id);
extern cgroup_acct_t *cgroup_g_task_get_acct_data(uint32_t taskid);
#endif /* !_SLURM_CGROUP_H */
