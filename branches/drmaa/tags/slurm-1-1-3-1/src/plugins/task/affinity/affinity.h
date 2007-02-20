/*****************************************************************************\
 *  src/plugins/task/affinity/affinity.h - task affinity plugin
 *  $Id: affinity.h,v 1.2 2005/11/04 02:46:51 palermo Exp $
 *****************************************************************************
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/
#if HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef HAVE_NUMA
#  include <numa.h>
#endif

#if HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif

#if HAVE_SYS_PRCTL_H
#  include <sys/prctl.h>
#endif

#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/poll.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>

#define _GNU_SOURCE
#define __USE_GNU
#include <sched.h> /* SMB */

#if HAVE_STDLIB_H
#  include <stdlib.h>
#endif

#include <slurm/slurm_errno.h>
#include "src/common/slurm_xlator.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

#include "src/common/cbuf.h"
#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/node_select.h"
#include "src/common/fd.h"
#include "src/common/safeopen.h"
#include "src/common/slurm_jobacct.h"
#include "src/common/switch.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"
#include "src/common/util-net.h"

/*** from affinity.c ***/
void	slurm_chkaffinity(cpu_set_t *mask, slurmd_job_t *job, int statval);
int	get_cpuset(cpu_set_t *mask, slurmd_job_t *job);
int	slurm_setaffinity(pid_t pid, size_t size, const cpu_set_t *mask);
int	slurm_getaffinity(pid_t pid, size_t size, cpu_set_t *mask);

/*** from numa.c ***/
#ifdef HAVE_NUMA
int	get_memset(nodemask_t *mask, slurmd_job_t *job);
void	slurm_chk_memset(nodemask_t *mask, slurmd_job_t *job);
#endif

/*** from schedutils.c ***/
int	char_to_val(int c);
int	str_to_cpuset(cpu_set_t *mask, const char* str);
char *	cpuset_to_str(const cpu_set_t *mask, char *str);
int	val_to_char(int v);
