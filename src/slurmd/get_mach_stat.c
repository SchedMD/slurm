/*
 * get_mach_stat.c - Get the status of the current machine and return it in the standard 
 *	node configuration format "Name=linux.llnl.gov procs=4 ..."
 * NOTE: Most of these modules are very much system specific. Built on RedHat2.4
 * NOTE: While not currently used by SLURM, this code can also get a nodes OS name 
 *       and CPU speed. See code ifdef'ed out via USE_OS_NAME and USE_CPU_SPEED
 *
 * Author: Moe Jette, jette@llnl.gov
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <errno.h>
#include <fcntl.h> 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/utsname.h>
#include <sys/vfs.h>
#include <unistd.h>

#include "slurm.h"

int get_procs(uint32_t *procs);
int get_mach_name(char *node_name);
int get_memory(uint32_t *real_memory);
int get_tmp_disk(uint32_t *tmp_disk);
#ifdef USE_OS_NAME
int get_os_name(char *os_name);
#endif
#ifdef USE_CPU_SPEED
int get_speed(float *speed);
#endif

#if DEBUG_MODULE
/* main is used here for testing purposes only */
int 
main(int argc, char * argv[]) 
{
	int error_code;
	struct config_record this_node;
	char node_name[MAX_NAME_LEN];

	error_code = get_mach_name(node_name);
	if (error_code != 0) 
		exit(1);    /* The show is all over without a node name */

	error_code += get_procs(&this_node.cpus);
	error_code += get_memory(&this_node.real_memory);
	error_code += get_tmp_disk(&this_node.tmp_disk);

	printf("NodeName=%s CPUs=%d RealMemory=%d TmpDisk=%d\n", 
		node_name, this_node.cpus, this_node.real_memory, 
		this_node.tmp_disk);
	if (error_code != 0) 
		printf("get_mach_stat error_code=%d encountered\n", error_code);
	exit (error_code);
}
#endif


/*
 * get_procs - Return the count of procs on this system 
 * Input: procs - buffer for the CPU count
 * Output: procs - filled in with CPU count, "1" if error
 *         return code - 0 if no error, otherwise errno
 */
int 
get_procs(uint32_t *procs) 
{
	int my_proc_tally;

	*procs = 1;
	my_proc_tally = (int)sysconf(_SC_NPROCESSORS_ONLN);
	if (my_proc_tally < 1) {
		error ("get_procs: error running sysconf(_SC_NPROCESSORS_ONLN)\n");
		return EINVAL;
	} 

	*procs = my_proc_tally;
	return 0;
}


#ifdef USE_OS_NAME
/*
 * get_os_name - Return the operating system name and version 
 * Input: os_name - buffer for the OS name, must be at least MAX_OS_LEN characters
 * Output: os_name - filled in with OS name, "UNKNOWN" if error
 *         return code - 0 if no error, otherwise errno
 */
int 
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
 * Input: node_name - buffer for the node name, must be at least MAX_NAME_LEN characters
 * Output: node_name - filled in with node name
 *         return code - 0 if no error, otherwise errno
 */
int 
get_mach_name(char *node_name) 
{
    int error_code;

    error_code = gethostname(node_name, MAX_NAME_LEN);
    if (error_code != 0)
	error ("get_mach_name: gethostname error %d\n", error_code);

    return error_code;
}


/*
 * get_memory - Return the count of procs on this system 
 * Input: real_memory - buffer for the Real Memory size
 * Output: real_memory - the Real Memory size in MB, "1" if error
 *         return code - 0 if no error, otherwise errno
 */
int
get_memory(uint32_t *real_memory)
{
	long pages;

	*real_memory = 1;
	pages = sysconf(_SC_PHYS_PAGES);
	if (pages < 1) {
		error ("get_memory: error running sysconf(_SC_PHYS_PAGES)\n");
		return EINVAL;
	} 

	*real_memory = (int)((float)pages * getpagesize() / 
			1048576.0); /* Megabytes of memory */
	return 0;
}

#ifdef USE_CPU_SPEED
/*
 * get_speed - Return the speed of procs on this system (MHz clock)
 * Input: procs - buffer for the CPU speed
 * Output: procs - filled in with CPU speed, "1.0" if error
 *         return code - 0 if no error, otherwise errno
 */
int 
get_speed(float *speed) 
{
	char buffer[128];
	FILE *cpu_info_file;
	char *buf_ptr1, *buf_ptr2;

	*speed = 1.0;
	cpu_info_file = fopen("/proc/cpuinfo", "r");
	if (cpu_info_file == NULL) {
		error ("get_speed: error %d opening /proc/cpuinfo\n", errno);
		return errno;
	} 

	while (fgets(buffer, sizeof(buffer), cpu_info_file) != NULL) {
		if ((buf_ptr1 = strstr(buffer, "cpu MHz")) != NULL)
			continue;
		buf_ptr1 += 7;
		buf_ptr2 = strstr(buf_ptr1, ":");
		if (buf_ptr2 != NULL) 
			buf_ptr1 = buf_ptr2 + 1;
		*speed = (float) strtod (buf_ptr1, (char **)NULL);
		break;
	} 

	fclose(cpu_info_file);
	return 0;
} 

#endif


/*
 * get_tmp_disk - Return the total size of /tmp file system on 
 *    this system 
 * Input: tmp_disk - buffer for the disk space size
 * Output: tmp_disk - filled in with disk space size in MB, zero if error
 *         return code - 0 if no error, otherwise errno
 */
int 
get_tmp_disk(uint32_t *tmp_disk) 
{
	struct statfs stat_buf;
	long   total_size;
	int error_code;
	float page_size;

	error_code = 0;
	*tmp_disk = 0;
	total_size = 0;
	page_size = (getpagesize() / 1048576.0); /* Megabytes per page */

	if (statfs(TMP_FS, &stat_buf) == 0) {
		total_size = (long)stat_buf.f_blocks;
	}
	else if (errno != ENOENT) {
		error_code = errno;
		error ("get_tmp_disk: error %d executing statfs on %s\n", 
			errno, TMP_FS);
	}

	*tmp_disk += (long)(total_size * page_size);
	return error_code;
}
