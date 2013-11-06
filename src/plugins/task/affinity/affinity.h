/*****************************************************************************\
 *  src/plugins/task/affinity/affinity.h - task affinity plugin
 *  $Id: affinity.h,v 1.2 2005/11/04 02:46:51 palermo Exp $
 *****************************************************************************
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
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
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef HAVE_NUMA
#  include <numa.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif

#ifdef HAVE_SYS_PRCTL_H
#  include <sys/prctl.h>
#endif

#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

#ifndef   _GNU_SOURCE
#  define _GNU_SOURCE
#endif
#ifndef   __USE_GNU
#define   __USE_GNU
#endif

#include <sched.h> /* SMB */

#ifdef HAVE_STDLIB_H
#  include <stdlib.h>
#endif

#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

#include "src/common/cbuf.h"
#include "src/common/cpu_frequency.h"
#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/node_select.h"
#include "src/common/fd.h"
#include "src/common/safeopen.h"
#include "src/common/switch.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"
#include "src/common/util-net.h"
#include "src/common/slurm_resource_info.h"

#ifndef CPUSET_DIR
#define CPUSET_DIR "/dev/cpuset"
#endif

/*** from affinity.c ***/
void	slurm_chkaffinity(cpu_set_t *mask, stepd_step_rec_t *job, int statval);
int	get_cpuset(cpu_set_t *mask, stepd_step_rec_t *job);
void	reset_cpuset(cpu_set_t *new_mask, cpu_set_t *cur_mask);
int	slurm_setaffinity(pid_t pid, size_t size, const cpu_set_t *mask);
int	slurm_getaffinity(pid_t pid, size_t size, cpu_set_t *mask);

/*** from cpuset.c ***/
#ifdef HAVE_NUMA
int	slurm_set_memset(char *path, nodemask_t *new_mask);
int	slurm_memset_available(void);
#endif
int	slurm_build_cpuset(char *base, char *path, uid_t uid, gid_t gid);
int	slurm_get_cpuset(char *path, pid_t pid, size_t size, cpu_set_t *mask);
int	slurm_set_cpuset(char *base, char *path, pid_t pid, size_t size,
		const cpu_set_t *mask);

/*** from numa.c ***/
#ifdef HAVE_NUMA
int	 get_memset(nodemask_t *mask, stepd_step_rec_t *job);
void	 slurm_chk_memset(nodemask_t *mask, stepd_step_rec_t *job);
uint16_t slurm_get_numa_node(uint16_t cpuid);
#endif

/*** from schedutils.c ***/
int	char_to_val(int c);
int	str_to_cpuset(cpu_set_t *mask, const char* str);
char *	cpuset_to_str(const cpu_set_t *mask, char *str);
int	val_to_char(int v);
