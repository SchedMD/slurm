/*****************************************************************************\
 *  get_mach_stat.c - Get the status of the current machine
 *
 *  NOTE: Some of these functions are system dependent. Built on RedHat2.4
 *****************************************************************************
 *  Copyright (C) 2006 Hewlett-Packard Development Company, L.P.
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
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

#include "config.h"

#ifdef HAVE_SYS_SYSTEMCFG_H
# include <sys/systemcfg.h>
#endif

#ifdef HAVE_SYS_DR_H
# include <sys/dr.h>
#endif

#ifdef HAVE_SYS_SYSCTL_H
#if defined(__FreeBSD__)
#include <sys/types.h>
#endif
# include <sys/sysctl.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#if defined(__APPLE__)
#  include <sys/times.h>
#  include <sys/types.h>
#elif defined(__NetBSD__) || defined(__FreeBSD__)
#  include <sys/times.h> /* for times(3) */
#else
/* NOTE: Getting the system uptime on AIX uses completely different logic.
 * sys/sysinfo.h on AIX defines structures that conflict with Slurm code. */
#  include <sys/sysinfo.h>
#endif

#include <sys/utsname.h>

#ifdef HAVE_SYS_STATVFS_H
#  include <sys/statvfs.h>
#endif

#ifdef HAVE_SYS_STATFS_H
#  include <sys/statfs.h>
#else
#ifdef HAVE_SYS_VFS_H
#  include <sys/vfs.h>
#endif
#endif

#ifdef HAVE_KSTAT_H
# include <kstat.h>
#endif

#include <unistd.h>

#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmd/slurmd/get_mach_stat.h"
#include "src/slurmd/slurmd/slurmd.h"


/*
 * get_mach_name - Return the name of this node
 * Input: node_name - buffer for the node name, must be at least MAX_SLURM_NAME characters
 * Output: node_name - filled in with node name
 *         return code - 0 if no error, otherwise errno
 */
extern int
get_mach_name(char *node_name)
{
    int error_code;

    error_code = gethostname_short(node_name, MAX_SLURM_NAME);
    if (error_code != 0)
	error ("get_mach_name: gethostname_short error %d", error_code);

    return error_code;
}


/*
 * get_memory - Return the count of procs on this system
 * Input: real_memory - buffer for the Real Memory size
 * Output: real_memory - the Real Memory size in MB, "1" if error
 *         return code - 0 if no error, otherwise errno
 */
extern int get_memory(uint64_t *real_memory)
{
#ifdef HAVE__SYSTEM_CONFIGURATION
	*real_memory = _system_configuration.physmem / (1024 * 1024);
#else
#  ifdef _SC_PHYS_PAGES
	long pages;

	*real_memory = 1;
	pages = sysconf(_SC_PHYS_PAGES);
	if (pages < 1) {
		error ("get_memory: error running sysconf(_SC_PHYS_PAGES)");
		return EINVAL;
	}
	*real_memory = (uint64_t)((float)pages * (sysconf(_SC_PAGE_SIZE) /
			1048576.0)); /* Megabytes of memory */
#  else  /* !_SC_PHYS_PAGES */
#    if HAVE_SYSCTLBYNAME
	int mem;
	size_t len = sizeof(mem);
	if (sysctlbyname("hw.physmem", &mem, &len, NULL, 0) == -1) {
		error("get_memory: error running sysctl(HW_PHYSMEM)");
		return EINVAL;
	}
	*real_memory = mem;
#    else /* !HAVE_SYSCTLBYNAME */
	*real_memory = 1;
#    endif /* HAVE_SYSCTLBYNAME */
#  endif /* _SC_PHYS_PAGES */
#endif /* HAVE__SYSTEM_CONFIGURATION */

	return 0;
}


/*
 * get_tmp_disk - Return the total size of temporary file system on
 *    this system
 * Input: tmp_disk - buffer for the disk space size
 *        tmp_fs - pathname of the temporary file system to status,
 *		   defaults to "/tmp"
 * Output: tmp_disk - filled in with disk space size in MB, zero if error
 *         return code - 0 if no error, otherwise errno
 */
extern int
get_tmp_disk(uint32_t *tmp_disk, char *tmp_fs)
{
	int error_code = 0;

#if defined(HAVE_STATVFS)
	struct statvfs stat_buf;
	uint64_t total_size = 0;
	char *tmp_fs_name = tmp_fs;

	*tmp_disk = 0;
	total_size = 0;

	if (tmp_fs_name == NULL)
		tmp_fs_name = "/tmp";
	if (statvfs(tmp_fs_name, &stat_buf) == 0) {
		total_size = stat_buf.f_blocks * stat_buf.f_frsize;
		total_size /= 1024 * 1024;
	}
	else if (errno != ENOENT) {
		error_code = errno;
		error ("get_tmp_disk: error %d executing statvfs on %s",
			errno, tmp_fs_name);
	}
	*tmp_disk += (uint32_t)total_size;

#elif defined(HAVE_STATFS)
	struct statfs stat_buf;
	long   total_size;
	float page_size;
	char *tmp_fs_name = tmp_fs;

	*tmp_disk = 0;
	total_size = 0;
	page_size = (sysconf(_SC_PAGE_SIZE) / 1048576.0); /* MG per page */

	if (tmp_fs_name == NULL)
		tmp_fs_name = "/tmp";
	if (statfs(tmp_fs_name, &stat_buf) == 0) {
		total_size = (long)stat_buf.f_blocks;
	}
	else if (errno != ENOENT) {
		error_code = errno;
		error ("get_tmp_disk: error %d executing statfs on %s",
			errno, tmp_fs_name);
	}

	*tmp_disk += (uint32_t)(total_size * page_size);
#else
	*tmp_disk = 1;
#endif
	return error_code;
}

extern int get_up_time(uint32_t *up_time)
{
#if defined(__APPLE__) || defined(__NetBSD__) || defined(__FreeBSD__)
	clock_t tm;
	struct tms buf;

	tm = times(&buf);
	if (tm == (clock_t) -1) {
		*up_time = 0;
		return errno;
	}

	*up_time = tm / sysconf(_SC_CLK_TCK);
#else
	/* NOTE for Linux: The return value of times() may overflow the
	 * possible range of type clock_t. There is also an offset of
	 * 429 million seconds on some implementations. We just use the
	 * simpler sysinfo() function instead. */
	struct sysinfo info;

	if (sysinfo(&info) < 0) {
		*up_time = 0;
		return errno;
	}


	if (conf->boot_time) {
		/* Make node look like it rebooted when slurmd started */
		static uint32_t orig_uptime = 0;
		if (orig_uptime == 0)
			orig_uptime = info.uptime;
		*up_time = info.uptime - orig_uptime;
	} else {
		*up_time = info.uptime;
	}
#endif
	return 0;
}

extern int get_cpu_load(uint32_t *cpu_load)
{
#if defined(__APPLE__) || defined(__NetBSD__) || defined(__FreeBSD__)
	/* Not sure how to get CPU load on above systems.
	 * Perhaps some method below works. */
	*cpu_load = 0;
#else
	struct sysinfo info;
	float shift_float = (float) (1 << SI_LOAD_SHIFT);

	if (sysinfo(&info) < 0) {
		*cpu_load = 0;
		return errno;
	}

	*cpu_load = (info.loads[1] / shift_float) * 100.0;
#endif
	return 0;
}

extern int get_free_mem(uint64_t *free_mem)
{
#if defined(__APPLE__) || defined(__NetBSD__) || defined(__FreeBSD__)
	/* Not sure how to get CPU load on above systems.
	 * Perhaps some method below works. */
	*free_mem = 0;
#else
	struct sysinfo info;

	if (sysinfo(&info) < 0) {
		*free_mem = 0;
		return errno;
	}

	*free_mem = (((uint64_t )info.freeram)*info.mem_unit)/(1024*1024);
#endif
	return 0;
}
