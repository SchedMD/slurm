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
#include <sys/utsname.h>
#ifdef HAVE_SYS_VFS_H
#  include <sys/vfs.h>
#endif
#include <unistd.h>

#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/parse_spec.h"
#include "src/common/read_config.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmd/slurmd/get_mach_stat.h"

static char* _cpuinfo_path = "/proc/cpuinfo";

int compute_block_map(uint16_t numproc,
			uint16_t **block_map, uint16_t **block_map_inv);
int chk_cpuinfo_str(char *buffer, char *keyword, char **valptr);
int chk_cpuinfo_uint32(char *buffer, char *keyword, uint32_t *val);
int chk_cpuinfo_float(char *buffer, char *keyword, float *val);

/* #define DEBUG_DETAIL	1 */	/* enable detailed debugging within SLURM */

#if DEBUG_MODULE
#define DEBUG_DETAIL	1
#define debug0	printf
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

	if (argc > 1) {
	    	_cpuinfo_path = argv[1];
		testnumproc = 1024;	/* since may not match test host */
	}
	debug3("%s:\n", _cpuinfo_path);

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
#ifdef USE_CPU_SPEED
	error_code += get_speed(&speed);
#endif

	debug3("\n");
	debug3("NodeName=%s CPUs=%u Sockets=%u Cores=%u Threads=%u\n",
		node_name, this_node.cpus,
		this_node.sockets, this_node.cores, this_node.threads);
	debug3("\tRealMemory=%u TmpDisk=%u Speed=%f\n",
		this_node.real_memory, this_node.tmp_disk, speed);
	if (error_code != 0) 
		debug3("get_mach_stat error_code=%d encountered\n", error_code);
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


/*
 * get_procs - Return the count of procs on this system 
 * Input: procs - buffer for the CPU count
 * Output: procs - filled in with CPU count, "1" if error
 *         return code - 0 if no error, otherwise errno
 */
extern int 
get_procs(uint16_t *procs) 
{
#ifdef LPAR_INFO_FORMAT2
	/* AIX 5.3 only */
	lpar_info_format2_t info;

	*procs = 1;
	if (lpar_get_info(LPAR_INFO_FORMAT2, &info, sizeof(info)) != 0) {
		error("lpar_get_info() failed");
		return EINVAL;
	}
	
	*procs = (uint16_t) info.online_vcpus;
#else /* !LPAR_INFO_FORMAT2 */

#  ifdef _SC_NPROCESSORS_ONLN
	int my_proc_tally;

	*procs = 1;
	my_proc_tally = (int)sysconf(_SC_NPROCESSORS_ONLN);
	if (my_proc_tally < 1) {
		error ("get_procs: error running sysconf(_SC_NPROCESSORS_ONLN)");
		return EINVAL;
	} 

	*procs = (uint16_t) my_proc_tally;
#  else
#    ifdef HAVE_SYSCTLBYNAME
	int ncpu;
	size_t len = sizeof(ncpu);

	*procs = 1;
	if (sysctlbyname("hw.ncpus", &ncpu, &len, NULL, 0) == -1) {
		error("get_procs: error running sysctl(HW_NCPU)");
		return EINVAL;
	}
	*procs = (uint16_t) ncpu;
#    else /* !HAVE_SYSCTLBYNAME */
	*procs = 1;
#    endif /* HAVE_SYSCTLBYNAME */
#  endif /* _SC_NPROCESSORS_ONLN */
#endif /* LPAR_INFO_FORMAT2 */

	return 0;
}

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
		error ("get_os_name: uname error %d\n", error_code);
		return error_code;
	} 

	if ((strlen(sys_info.sysname) + strlen(sys_info.release) + 2) >= 
		MAX_OS_LEN) {
		error ("get_os_name: OS name too long\n");
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
	error ("get_mach_name: gethostname_short error %d\n", error_code);

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
	if (statfs(tmp_fs_name, &stat_buf) == 0) {
		total_size = (long)stat_buf.f_blocks;
	}
	else if (errno != ENOENT) {
		error_code = errno;
		error ("get_tmp_disk: error %d executing statfs on %s\n", 
			errno, tmp_fs_name);
	}

	*tmp_disk += (uint32_t)(total_size * page_size);
#else
	*tmp_disk = 1;
#endif
	return error_code;
}


/* chk_cpuinfo_str
 *	check a line of cpuinfo data (buffer) for a keyword.  If it
 *	exists, return the string value for that keyword in *valptr.
 * Input:  buffer - single line of cpuinfo data
 *	   keyword - keyword to check for
 * Output: valptr - string value corresponding to keyword
 *         return code - true if keyword found, false if not found
 */
int chk_cpuinfo_str(char *buffer, char *keyword, char **valptr)
{
	char *ptr;
	if (strncmp(buffer, keyword, strlen(keyword)))
		return false;

	ptr = strstr(buffer, ":");
	if (ptr != NULL) 
		ptr++;
	*valptr = ptr;
	return true;
}

/* chk_cpuinfo_uint32
 *	check a line of cpuinfo data (buffer) for a keyword.  If it
 *	exists, return the uint16 value for that keyword in *valptr.
 * Input:  buffer - single line of cpuinfo data
 *	   keyword - keyword to check for
 * Output: valptr - uint32 value corresponding to keyword
 *         return code - true if keyword found, false if not found
 */
int chk_cpuinfo_uint32(char *buffer, char *keyword, uint32_t *val)
{
	char *valptr;
	if (chk_cpuinfo_str(buffer, keyword, &valptr)) {
		*val = strtoul(valptr, (char **)NULL, 10);
		return true;
	} else {
		return false;
	}
}

/* chk_cpuinfo_float
 *	check a line of cpuinfo data (buffer) for a keyword.  If it
 *	exists, return the float value for that keyword in *valptr.
 * Input:  buffer - single line of cpuinfo data
 *	   keyword - keyword to check for
 * Output: valptr - float value corresponding to keyword
 *         return code - true if keyword found, false if not found
 */
int chk_cpuinfo_float(char *buffer, char *keyword, float *val)
{
	char *valptr;
	if (chk_cpuinfo_str(buffer, keyword, &valptr)) {
		*val = (float) strtod(valptr, (char **)NULL);
		return true;
	} else {
		return false;
	}
}

#ifdef USE_CPU_SPEED
/*
 * get_speed - Return the speed of procs on this system (MHz clock)
 * Input: procs - buffer for the CPU speed
 * Output: procs - filled in with CPU speed, "1.0" if error
 *         return code - 0 if no error, otherwise errno
 */
extern int 
get_speed(float *speed) 
{
	FILE *cpu_info_file;
	char buffer[128];

	*speed = 1.0;
	cpu_info_file = fopen(_cpuinfo_path, "r");
	if (cpu_info_file == NULL) {
		error ("get_speed: error %d opening %s\n", errno, _cpuinfo_path);
		return errno;
	} 

	while (fgets(buffer, sizeof(buffer), cpu_info_file) != NULL) {
		chk_cpuinfo_float(buffer, "cpu MHz", speed);
	} 

	fclose(cpu_info_file);
	return 0;
} 

#endif

/*
 * get_cpuinfo - Return detailed cpuinfo on this system 
 * Input:  numproc - number of processors on the system
 * Output: p_sockets - number of physical processor sockets
 *         p_cores - total number of physical CPU cores
 *         p_threads - total number of hardware execution threads
 *         block_map - asbtract->physical block distribution map 
 *         block_map_inv - physical->abstract block distribution map (inverse)
 *         return code - 0 if no error, otherwise errno
 * NOTE: User must xfree block_map and block_map_inv  
 */
typedef struct cpuinfo {
	uint16_t seen;
	uint32_t cpuid;
	uint32_t physid;
	uint16_t physcnt;
	uint32_t coreid;
	uint16_t corecnt;
	uint16_t siblings;
	uint16_t cores;
} cpuinfo_t;
static cpuinfo_t *cpuinfo = NULL; /* array of CPU information for get_cpuinfo */
				  /* Note: file static for qsort/_compare_cpus*/
extern int
get_cpuinfo(uint16_t numproc,
		uint16_t *p_sockets, uint16_t *p_cores, uint16_t *p_threads,
		uint16_t *block_map_size,
		uint16_t **block_map, uint16_t **block_map_inv)
{
	FILE *cpu_info_file;
	char buffer[128];
	int retval;
	uint16_t curcpu, sockets, cores, threads;
	uint16_t numcpu	   = 0;		/* number of cpus seen */
	uint16_t numphys   = 0;		/* number of unique "physical id"s */
	uint16_t numcores  = 0;		/* number of unique "cores id"s */

	uint16_t maxsibs   = 0;		/* maximum value of "siblings" */
	uint16_t maxcores  = 0;		/* maximum value of "cores" */
	uint16_t minsibs   = 0xffff;	/* minimum value of "siblings" */
	uint16_t mincores  = 0xffff;	/* minimum value of "cores" */

	uint32_t maxcpuid  = 0;		/* maximum CPU ID ("processor") */
	uint32_t maxphysid = 0;		/* maximum "physical id" */
	uint32_t maxcoreid = 0;		/* maximum "core id" */
	uint32_t mincpuid  = 0xffffffff;/* minimum CPU ID ("processor") */
	uint32_t minphysid = 0xffffffff;/* minimum "physical id" */
	uint32_t mincoreid = 0xffffffff;/* minimum "core id" */
	int i;

	*p_sockets = numproc;		/* initially all single core/thread */
	*p_cores   = 1;
	*p_threads = 1;
	*block_map_size = 0;
	*block_map      = NULL;
	*block_map_inv  = NULL;

	cpu_info_file = fopen(_cpuinfo_path, "r");
	if (cpu_info_file == NULL) {
		error ("get_cpuinfo: error %d opening %s\n", 
			errno, _cpuinfo_path);
		return errno;
	}


	/* Note: assumes all processor IDs are within [0:numproc-1] */
	/*       treats physical/core IDs as tokens, not indices */
	if (cpuinfo)
		memset(cpuinfo, 0, numproc * sizeof(cpuinfo_t));
	else
		cpuinfo = xmalloc(numproc * sizeof(cpuinfo_t));

	curcpu = 0;
	while (fgets(buffer, sizeof(buffer), cpu_info_file) != NULL) {
		uint32_t val;
		if (chk_cpuinfo_uint32(buffer, "processor", &val)) {
			numcpu++;
			curcpu = val;
		    	if (val >= numproc) {	/* out of bounds, ignore */
				debug("cpuid is %u (> %d), ignored", 
					val, numproc);
				continue;
			}
			cpuinfo[val].seen = 1;
			cpuinfo[val].cpuid = val;
			maxcpuid = MAX(maxcpuid, val);
			mincpuid = MIN(mincpuid, val);
		} else if (chk_cpuinfo_uint32(buffer, "physical id", &val)) {
			/* see if the ID has already been seen */
			for (i=0; i<numproc; i++) {
				if ((cpuinfo[i].physid == val)
				&&  (cpuinfo[i].physcnt))
					break;
			}

			if (i == numproc) {		/* new ID... */
				numphys++;		/* ...increment total */
			} else {			/* existing ID... */
				cpuinfo[i].physcnt++;	/* ...update ID cnt */
			}

			if (curcpu < numproc) {
				cpuinfo[curcpu].physcnt++;
				cpuinfo[curcpu].physid = val;
			}

			maxphysid = MAX(maxphysid, val);
			minphysid = MIN(minphysid, val);
		} else if (chk_cpuinfo_uint32(buffer, "core id", &val)) {
			/* see if the ID has already been seen */
			for (i = 0; i < numproc; i++) {
				if ((cpuinfo[i].coreid == val)
				&&  (cpuinfo[i].corecnt))
					break;
			}

			if (i == numproc) {		/* new ID... */
				numcores++;		/* ...increment total */
			} else {			/* existing ID... */
				cpuinfo[i].corecnt++;	/* ...update ID cnt */
			}

			if (curcpu < numproc) {
				cpuinfo[curcpu].corecnt++;
				cpuinfo[curcpu].coreid = val;
			}

			maxcoreid = MAX(maxcoreid, val);
			mincoreid = MIN(mincoreid, val);
		} else if (chk_cpuinfo_uint32(buffer, "siblings", &val)) {
			/* Note: this value is a count, not an index */
		    	if (val > numproc) {	/* out of bounds, ignore */
				debug("siblings is %u (> %d), ignored",
					val, numproc);
				continue;
			}
			if (curcpu < numproc)
				cpuinfo[curcpu].siblings = val;
			maxsibs = MAX(maxsibs, val);
			minsibs = MIN(minsibs, val);
		} else if (chk_cpuinfo_uint32(buffer, "cpu cores", &val)) {
			/* Note: this value is a count, not an index */
		    	if (val > numproc) {	/* out of bounds, ignore */
				debug("cores is %u (> %d), ignored",
					val, numproc);
				continue;
			}
			if (curcpu < numproc)
				cpuinfo[curcpu].cores = val;
			maxcores = MAX(maxcores, val);
			mincores = MIN(mincores, val);
		}
	}

	fclose(cpu_info_file);


	/*** Sanity check ***/
	if (minsibs == 0) minsibs = 1;		/* guaranteee non-zero */
	if (maxsibs == 0) {
	    	minsibs = 1;
	    	maxsibs = 1;
	}
	if (maxcores == 0) {			/* no core data */
	    	mincores = 0;
	    	maxcores = 0;
	}

	/*** Compute Sockets/Cores/Threads ***/
	if ((minsibs == maxsibs) &&		/* homogeneous system */
	    (mincores == maxcores)) {
		sockets = numphys; 		/* unique "physical id" */
		if (sockets <= 1) {		/* verify single socket */
			sockets = numcpu / maxsibs; /* maximum "siblings" */
		}
		if (sockets == 0)
			sockets = 1;		/* guarantee non-zero */
	
		cores = numcores / sockets;	/* unique "core id" */
		cores = MAX(maxcores, cores);	/* maximum "cpu cores" */
	
		if (cores == 0) {
			cores = numcpu / sockets;	/* assume multi-core */
			if (cores > 1) {
				debug3("Warning: cpuinfo missing 'core id' or "
					"'cpu cores' but assuming multi-core");
			}
		}
		if (cores == 0)
			cores = 1;	/* guarantee non-zero */
	
		threads = numcpu / (sockets * cores); /* solve for threads */
		if (threads == 0)
			threads = 1;	/* guarantee non-zero */
	} else {				/* heterogeneous system */
		sockets = numcpu;
		cores   = 1;			/* one core per socket */
		threads = 1;			/* one core per core */
	}

	*p_sockets = sockets;		/* update output parameters */
	*p_cores   = cores;
	*p_threads = threads;

#if DEBUG_DETAIL
	/*** Display raw data ***/
	debug3("\n");
	debug3("numcpu:     %u\n", numcpu);
	debug3("numphys:    %u\n", numphys);
	debug3("numcores:   %u\n", numcores);

	debug3("cores:      %u->%u\n", mincores, maxcores);
	debug3("sibs:       %u->%u\n", minsibs,  maxsibs);

	debug3("cpuid:      %u->%u\n", mincpuid,  maxcpuid);
	debug3("physid:     %u->%u\n", minphysid, maxphysid);
	debug3("coreid:     %u->%u\n", mincoreid, maxcoreid);

	for (i = 0; i <= maxcpuid; i++) {
		debug3("CPU %d:", i);
		debug3(" seen:     %u", cpuinfo[i].seen);
		debug3(" physid:   %u", cpuinfo[i].physid);
		debug3(" physcnt:  %u", cpuinfo[i].physcnt);
		debug3(" siblings: %u", cpuinfo[i].siblings);
		debug3(" cores:    %u", cpuinfo[i].cores);
		debug3(" coreid:   %u", cpuinfo[i].coreid);
		debug3(" corecnt:  %u", cpuinfo[i].corecnt);
		debug3("\n");
	}

	debug3("\n");
	debug3("Sockets:          %u\n", sockets);
	debug3("Cores per socket: %u\n", cores);
	debug3("Threads per core: %u\n", threads);
#endif

	*block_map_size = numcpu;
	retval = compute_block_map(*block_map_size, block_map, block_map_inv);

	xfree(cpuinfo);		/* done with raw cpuinfo data */

	return retval;
}

/*
 * compute_block_map - Compute abstract->machine block mapping (and inverse)
 *   allows computation of CPU ID masks for an abstract block distribution
 *   of logical processors which can then be mapped the IDs used in the
 *   actual machine processor ID ordering (which can be BIOS/OS dependendent)
 * Input:  numproc - number of processors on the system
 *	   cpu - array of cpuinfo (file static for qsort/_compare_cpus)
 * Output: block_map, block_map_inv - asbtract->physical block distribution map 
 *         return code - 0 if no error, otherwise errno
 * NOTE: User must free block_map and block_map_inv
 *
 * For example, given a system with 8 logical processors arranged as:
 *
 *	Sockets:          4
 *	Cores per socket: 2
 *	Threads per core: 1
 *
 * and a logical CPU ID assignment of:
 *
 *	Machine logical CPU ID assignment:
 *	Logical CPU ID:        0  1  2  3  4  5  6  7
 *	Physical Socket ID:    0  1  3  2  0  1  3  2
 *
 * The block_map would be:
 *
 *	Abstract -> Machine logical CPU ID block mapping:
 *	Input: (Abstract ID)   0  1  2  3  4  5  6  7
 *	Output: (Machine ID)   0  4  1  5  3  7  2  6  <--- block_map[]
 *	Physical Socket ID:    0  0  1  1  2  2  3  3
 *
 * and it's inverse would be:
 *
 *	Machine -> Abstract logical CPU ID block mapping: (inverse)
 *	Input: (Machine ID)    0  1  2  3  4  5  6  7
 *	Output: (Abstract ID)  0  2  6  4  1  3  7  5  <--- block_map_inv[]
 *	Physical Socket ID:    0  1  3  2  0  1  3  2
 */

/* physical cpu comparison with void * arguments to allow use with
 * libc qsort()
 */
static int _icmp16(uint16_t a, uint16_t b)
{
    	if (a < b) {
		return -1;
	} else if (a == b) {
		return 0;
	} else {
		return 1;
	}
}
static int _icmp32(uint32_t a, uint32_t b)
{
	if (a < b) {
		return -1;
	} else if (a == b) {
		return 0;
	} else {
		return 1;
	}
}

int _compare_cpus(const void *a1, const void *b1) {
	uint16_t *a = (uint16_t *) a1;
	uint16_t *b = (uint16_t *) b1;
	int cmp;

	cmp = -1 * _icmp16(cpuinfo[*a].seen,cpuinfo[*b].seen); /* seen to front */
	if (cmp != 0)
		return cmp;

	cmp = _icmp32(cpuinfo[*a].physid, cpuinfo[*b].physid); /* key 1: physid */
	if (cmp != 0)
		return cmp;

	cmp = _icmp32(cpuinfo[*a].coreid, cpuinfo[*b].coreid); /* key 2: coreid */
	if (cmp != 0)
		return cmp;

	cmp = _icmp32(cpuinfo[*a].cpuid, cpuinfo[*b].cpuid);   /* key 3: cpu id */
	return cmp;
}

int compute_block_map(uint16_t numproc,
		uint16_t **block_map, uint16_t **block_map_inv)
{
	uint16_t i;
	/* Compute abstract->machine block mapping (and inverse) */
	if (block_map) {
		*block_map = xmalloc(numproc * sizeof(uint16_t));
		for (i = 0; i < numproc; i++) {
			(*block_map)[i] = i;
		}
		qsort(*block_map, numproc, sizeof(uint16_t), &_compare_cpus);
	}
	if (block_map_inv) {
		*block_map_inv = xmalloc(numproc * sizeof(uint16_t));
		for (i = 0; i < numproc; i++) {
			uint16_t idx = (*block_map)[i];
			(*block_map_inv)[idx] = i;
		}
	}

#if DEBUG_DETAIL
	/* Display the mapping tables */

	debug3("\nMachine logical CPU ID assignment:\n");
	debug3("Logical CPU ID:      ");
	for (i = 0; i < numproc; i++) {
		debug3("%3d", i);
	}
	debug3("\n");
	debug3("Physical Socket ID:  ");
	for (i = 0; i < numproc; i++) {
		debug3("%3u", cpuinfo[i].physid);
	}
	debug3("\n");

	if (block_map) {
		debug3("\nAbstract -> Machine logical CPU ID block mapping:\n");
		debug3("Input: (Abstract ID) ");
		for (i = 0; i < numproc; i++) {
			debug3("%3d", i);
		}
		debug3("\n");
		debug3("Output: (Machine ID) ");
		for (i = 0; i < numproc; i++) {
			debug3("%3u", (*block_map)[i]);
		}
		debug3("\n");
		debug3("Physical Socket ID:  ");
		for (i = 0; i < numproc; i++) {
			uint16_t id = (*block_map)[i];
			debug3("%3u", cpuinfo[id].physid);
		}
		debug3("\n");
	}

	if (block_map_inv) {
		debug3("\nMachine -> Abstract logical CPU ID block mapping: "
			"(inverse)\n");
		debug3("Input: (Machine ID)  ");
		for (i = 0; i < numproc; i++) {
			debug3("%3d", i);
		}
		debug3("\n");
		debug3("Output: (Abstract ID)");
		for (i = 0; i < numproc; i++) {
			debug3("%3u", (*block_map_inv)[i]);
		}
		debug3("\n");
		debug3("Physical Socket ID:  ");
		for (i = 0; i < numproc; i++) {
			debug3("%3u", cpuinfo[i].physid);
		}
		debug3("\n");
	}
#endif
	return 0;
}


