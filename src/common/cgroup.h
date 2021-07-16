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

#ifndef _COMMON_CGROUP_H_
#define _COMMON_CGROUP_H_

/* Check filesystem type */
#include <linux/magic.h>
#include <sys/vfs.h>

#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <inttypes.h>

#include "config.h"

#include "slurm/slurm.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

#include "src/common/log.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/parse_config.h"
#include "src/common/parse_time.h"
#include "src/common/read_config.h"
#include "src/common/plugin.h"
#include "src/common/slurm_opt.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/plugins/cgroup/common/cgroup_common.h"

#ifdef __GNUC__
#define F_TYPE_EQUAL(a, b) (a == (__typeof__(a)) b)
#else
#define F_TYPE_EQUAL(a, b) (a == (__SWORD_TYPE) b)
#endif

/* Not defined in non-supported v2 linux versions -- e.g centos7 */
#ifndef CGROUP2_SUPER_MAGIC
#define CGROUP2_SUPER_MAGIC 0x63677270
#endif

/*  Default lower bound on memory limit in MB. This is required so we
 *   don't immediately kill slurmstepd on mem cgroup creation if
 *   an administrator or user sets and absurdly low mem limit.
 */
#define XCGROUP_DEFAULT_MIN_RAM 30

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

/* Slurm cgroup plugins configuration parameters */
typedef struct {
	bool cgroup_automount;
	char *cgroup_mountpoint;

	char *cgroup_prepend;

	bool constrain_cores;
	bool task_affinity;

	bool constrain_ram_space;
	float allowed_ram_space;
	float max_ram_percent;		/* Upper bound on memory as % of RAM */

	uint64_t min_ram_space;		/* Lower bound on memory limit (MB) */

	bool constrain_kmem_space;
	float allowed_kmem_space;
	float max_kmem_percent;
	uint64_t min_kmem_space;

	bool constrain_swap_space;
	float allowed_swap_space;
	float max_swap_percent;		/* Upper bound on swap as % of RAM  */
	uint64_t memory_swappiness;

	bool constrain_devices;
	char *allowed_devices_file;
	char *cgroup_plugin;
} cgroup_conf_t;


extern cgroup_conf_t slurm_cgroup_conf;

/* global functions */
extern int cgroup_conf_init(void);
extern void cgroup_conf_destroy(void);
extern void cgroup_conf_reinit(void);
extern void cgroup_free_limits(cgroup_limits_t *limits);
extern List cgroup_get_conf_list(void);
extern int cgroup_write_conf(int fd);
extern int cgroup_read_conf(int fd);
extern bool cgroup_memcg_job_confinement(void);

/* global plugin functions */
extern int cgroup_g_init(void);
extern int cgroup_g_fini(void);
extern int cgroup_g_initialize(cgroup_ctl_type_t sub);
extern int cgroup_g_system_create(cgroup_ctl_type_t sub);
extern int cgroup_g_system_addto(cgroup_ctl_type_t sub, pid_t *pids, int npids);
extern int cgroup_g_system_destroy(cgroup_ctl_type_t sub);
extern int cgroup_g_step_create(cgroup_ctl_type_t sub, stepd_step_rec_t *job);
extern int cgroup_g_step_addto(cgroup_ctl_type_t sub, pid_t *pids, int npids);
extern int cgroup_g_step_get_pids(pid_t **pids, int *npids);
extern int cgroup_g_step_suspend(void);
extern int cgroup_g_step_resume(void);
extern int cgroup_g_step_destroy(cgroup_ctl_type_t sub);
extern bool cgroup_g_has_pid(pid_t pid);
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
extern int cgroup_g_task_constrain_set(cgroup_ctl_type_t sub,
				       cgroup_limits_t *limits,
				       uint32_t taskid);
extern int cgroup_g_step_start_oom_mgr(void);
extern cgroup_oom_t *cgroup_g_step_stop_oom_mgr(stepd_step_rec_t *job);
extern int cgroup_g_task_addto(cgroup_ctl_type_t sub, stepd_step_rec_t *job,
			       pid_t pid, uint32_t task_id);
extern cgroup_acct_t *cgroup_g_task_get_acct_data(uint32_t taskid);

#endif
