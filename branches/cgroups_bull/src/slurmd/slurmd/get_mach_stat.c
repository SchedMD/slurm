/*****************************************************************************\
 *  get_mach_stat.c - Get the status of the current machine 
 *
 *  NOTE: Some of these functions are system dependent. Built on RedHat2.4
 *  NOTE: While not currently used by SLURM, this code can also get a node's
 *       OS name and CPU speed. See code ifdef'ed out via USE_OS_NAME and 
 *       USE_CPU_SPEED
 *****************************************************************************
 *  Copyright (C) 2006 Hewlett-Packard Development Company, L.P.
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#ifdef HAVE_SYS_SYSTEMCFG_H
# include <sys/systemcfg.h>
#endif

#ifdef HAVE_SYS_DR_H
# include <sys/dr.h>
#endif

#ifdef HAVE_SYS_SYSCTL_H
# include <sys/sysctl.h>
#endif
 
#include <errno.h>
#include <fcntl.h> 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#if defined(HAVE_AIX) || defined(__sun) || defined(__APPLE__)
#  include <sys/times.h>
#  include <sys/types.h>
#else
/* NOTE: Getting the system uptime on AIX uses completely different logic.
 * sys/sysinfo.h on AIX defines structures that conflict with SLURM code. */
#  include <sys/sysinfo.h>
#endif

#include <sys/utsname.h>

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
#include "src/common/parse_spec.h"
#include "src/common/read_config.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmd/slurmd/get_mach_stat.h"

/* #define DEBUG_DETAIL	1 */	/* enable detailed debugging within SLURM */

#if DEBUG_MODULE
static char* _cpuinfo_path = "/proc/cpuinfo";

#define DEBUG_DETAIL	1
#define error 	printf
#define debug 	printf
#define debug1	printf
#define debug2	printf
#define debug3	printf
#undef xmalloc
#define xmalloc	malloc
#undef xfree
#define xfree	free
/* main is used here for testing purposes only:				*/
/* % gcc -DDEBUG_MODULE get_mach_stat.c -I../../.. -g -DUSE_CPU_SPEED	*/
int 
main(int argc, char * argv[]) 
{
	int error_code;
	uint16_t sockets, cores, threads;
	uint16_t block_map_size;
	uint16_t *block_map, *block_map_inv;
	struct config_record this_node;
	char node_name[MAX_SLURM_NAME];
	float speed;
	uint16_t testnumproc = 0;
	uint32_t up_time = 0;
	int days, hours, mins, secs;

	if (argc > 1) {
	    	_cpuinfo_path = argv[1];
		testnumproc = 1024;	/* since may not match test host */
	}
	debug3("%s:", _cpuinfo_path);

	error_code = get_mach_name(node_name);
	if (error_code != 0) 
		exit(1);    /* The show is all over without a node name */

	error_code += get_procs(&this_node.cpus);
	error_code += get_cpuinfo(MAX(this_node.cpus, testnumproc),
				  &this_node.sockets,
				  &this_node.cores,
				  &this_node.threads,
				  &block_map_size,
				  &block_map, &block_map_inv);
	xfree(block_map);	/* not used here */
	xfree(block_map_inv);	/* not used here */
	error_code += get_memory(&this_node.real_memory);
	error_code += get_tmp_disk(&this_node.tmp_disk, "/tmp");
	error_code += get_up_time(&up_time);
#ifdef USE_CPU_SPEED
	error_code += get_speed(&speed);
#endif

	debug3("");
	debug3("NodeName=%s CPUs=%u Sockets=%u Cores=%u Threads=%u",
		node_name, this_node.cpus,
		this_node.sockets, this_node.cores, this_node.threads);
	debug3("\tRealMemory=%u TmpDisk=%u Speed=%f",
		this_node.real_memory, this_node.tmp_disk, speed);
	secs  = up_time % 60;
	mins  = (up_time / 60) % 60;
	hours = (up_time / 3600) % 24;
	days  = (up_time / 86400);
	debug3("\tUpTime=%u=%u-%2.2u:%2.2u:%2.2u",
	       up_time, days, hours, mins, secs);
	if (error_code != 0) 
		debug3("get_mach_stat error_code=%d encountered", error_code);
	exit (error_code);
}


/* gethostname_short - equivalent to gethostname, but return only the first 
 * component of the fully qualified name 
 * (e.g. "linux123.foo.bar" becomes "linux123") 
 * OUT name
 */
int
gethostname_short (char *name, size_t len)
{
	int error_code, name_len;
	char *dot_ptr, path_name[1024];

	error_code = gethostname (path_name, sizeof(path_name));
	if (error_code)
		return error_code;

	dot_ptr = strchr (path_name, '.');
	if (dot_ptr == NULL)
		dot_ptr = path_name + strlen(path_name);
	else
		dot_ptr[0] = '\0';

	name_len = (dot_ptr - path_name);
	if (name_len > len)
		return ENAMETOOLONG;

	strcpy (name, path_name);
	return 0;
}
#endif

#ifdef USE_OS_NAME
/*
 * get_os_name - Return the operating system name and version 
 * Input: os_name - buffer for the OS name, must be at least MAX_OS_LEN characters
 * Output: os_name - filled in with OS name, "UNKNOWN" if error
 *         return code - 0 if no error, otherwise errno
 */
extern int 
get_os_name(char *os_name) 
{
	int error_code;
	struct utsname sys_info;

	strcpy(os_name, "UNKNOWN");
	error_code = uname(&sys_info);
	if (error_code != 0) {
		error ("get_os_name: uname error %d", error_code);
		return error_code;
	} 

	if ((strlen(sys_info.sysname) + strlen(sys_info.release) + 2) >= 
		MAX_OS_LEN) {
		error ("get_os_name: OS name too long");
		return error_code;
	} 

	strcpy(os_name, sys_info.sysname);
	strcat(os_name, ".");
	strcat(os_name, sys_info.release);
	return 0;
}
#endif


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
extern int
get_memory(uint32_t *real_memory)
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
	*real_memory = (uint32_t)((float)pages * (sysconf(_SC_PAGE_SIZE) / 
			1048576.0)); /* Megabytes of memory */
#  else  /* !_SC_PHYS_PAGES */
#    if HAVE_SYSCTLBYNAME
	int mem;
	size_t len = sizeof(mem);
	if (sysctlbyname("hw.physmem", &mem, &len, NULL, 0) == -1) {
		error("get_procs: error running sysctl(HW_PHYSMEM)");
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
 *	           defaults to "/tmp"
 * Output: tmp_disk - filled in with disk space size in MB, zero if error
 *         return code - 0 if no error, otherwise errno
 */
extern int 
get_tmp_disk(uint32_t *tmp_disk, char *tmp_fs) 
{
	int error_code = 0;
#ifdef HAVE_SYS_VFS_H
	struct statfs stat_buf;
	long   total_size;
	float page_size;
	char *tmp_fs_name = tmp_fs;

	*tmp_disk = 0;
	total_size = 0;
	page_size = (sysconf(_SC_PAGE_SIZE) / 1048576.0); /* MG per page */

	if (tmp_fs_name == NULL)
		tmp_fs_name = "/tmp";
#if defined (__sun)
	if (statfs(tmp_fs_name, &stat_buf, 0, 0) == 0) {
#else
	if (statfs(tmp_fs_name, &stat_buf) == 0) {
#endif
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
#if defined(HAVE_AIX) || defined(__sun) || defined(__APPLE__)
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

	*up_time = info.uptime;
#endif
	return 0;
}

#ifdef USE_CPU_SPEED
/* _chk_cpuinfo_float
 *	check a line of cpuinfo data (buffer) for a keyword.  If it
 *	exists, return the float value for that keyword in *valptr.
 * Input:  buffer - single line of cpuinfo data
 *	   keyword - keyword to check for
 * Output: valptr - float value corresponding to keyword
 *         return code - true if keyword found, false if not found
 */
static int _chk_cpuinfo_float(char *buffer, char *keyword, float *val)
{
	char *valptr;
	if (_chk_cpuinfo_str(buffer, keyword, &valptr)) {
		*val = (float) strtod(valptr, (char **)NULL);
		return true;
	} else {
		return false;
	}
}

/*
 * get_speed - Return the speed of procs on this system (MHz clock)
 * Input: procs - buffer for the CPU speed
 * Output: procs - filled in with CPU speed, "1.0" if error
 *         return code - 0 if no error, otherwise errno
 */
extern int 
get_speed(float *speed) 
{
#if defined (__sun)
	kstat_ctl_t   *kc;
	kstat_t       *ksp;
	kstat_named_t *knp;

	kc = kstat_open();
	if (kc == NULL) {
		error ("get speed: kstat error %d", errno);
		return errno;
	}

	ksp = kstat_lookup(kc, "cpu_info", -1, NULL);
	kstat_read(kc, ksp, NULL);
	knp = kstat_data_lookup(ksp, "clock_MHz");

	*speed = knp->value.l;
#else
	FILE *cpu_info_file;
	char buffer[128];

	*speed = 1.0;
	cpu_info_file = fopen(_cpuinfo_path, "r");
	if (cpu_info_file == NULL) {
		error("get_speed: error %d opening %s", errno, _cpuinfo_path);
		return errno;
	} 

	while (fgets(buffer, sizeof(buffer), cpu_info_file) != NULL) {
		_chk_cpuinfo_float(buffer, "cpu MHz", speed);
	} 

	fclose(cpu_info_file);
#endif
	return 0;
} 

#endif


