/*
 * Get_Mach_Stat - Get the status of the current machine and return it in the standard 
 *	node configuration format "Name=linux.llnl.gov CPUs=4 ..."
 * NOTE: The following modules are system specific: Get_CPUs, Get_Speed, Get_Memory, Get_Disk
 *
 * Author: Moe Jette, jette@llnl.gov
 */

#include <errno.h>
#include <fcntl.h> 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/utsname.h>

#include "Mach_Stat_Mgr.h"

#define DEBUG_MODULE 1

int Get_CPUs(int *CPUs);
int Get_OS_Name(char *OS_Name);
int Get_Mach_Name(char *Node_Name);
int Get_Memory(int *RealMemory, int *VirtualMemory);
int Get_Speed(float *Speed);
int Get_TmpDisk(int *TmpDisk);

#ifdef DEBUG_MODULE
/* main is used here for testing purposes only */
main(int argc, char * argv[]) {
    int Error_Code;
    struct Node_Record This_Node;

    Error_Code = Get_Mach_Name(This_Node.Name);
    if (Error_Code != 0) exit(1);    /* The show is all over without a node name */

    Error_Code += Get_OS_Name(This_Node.OS);
    Error_Code += Get_CPUs(&This_Node.CPUs);
    Error_Code += Get_Speed(&This_Node.Speed);
    Error_Code += Get_Memory(&This_Node.RealMemory, &This_Node.VirtualMemory);
    Error_Code += Get_TmpDisk(&This_Node.TmpDisk);

    printf("Name=%s OS=%s CPUs=%d Speed=%f RealMemory=%d VirtualMemory=%d TmpDisk=%d\n", 
	This_Node.Name, This_Node.OS, This_Node.CPUs, This_Node.Speed, This_Node.RealMemory, 
	This_Node.VirtualMemory, This_Node.TmpDisk);
    if (Error_Code != 0) printf("Get_Mach_Stat Errors encountered, Error_Code=%d\n", Error_Code);
} /* main */
#endif

/*
 * Get_CPUs - Return the count of CPUs on this system 
 * Input: CPUs - buffer for the CPU count
 * Output: CPUs - filled in with CPU count, "1" if error
 *         return code - 0 if no error, otherwise errno
 */
int Get_CPUs(int *CPUs) {
    *CPUs = 1;
    return 0;
}

/*
 * Get_OS_Name - Return the operating system name and version 
 * Input: OS_Name - buffer for the OS name, must be at least MAX_OS_LEN characters
 * Output: OS_Name - filled in with OS name, "UNKNOWN" if error
 *         return code - 0 if no error, otherwise errno
 */
int Get_OS_Name(char *OS_Name) {
    int Error_Code;
    struct utsname Sys_Info;

    strcpy(OS_Name, "UNKNOWN");
    Error_Code = uname(&Sys_Info);
    if (Error_Code != 0) {
#ifdef DEBUG_MODULE
	fprintf(stderr, "Get_OS_Name: uname error %d\n", Error_Code);
#else
	syslog(LOG_WARN "Get_OS_Name: uname error %d\n", Error_Code);
#endif
	return Error_Code;
    } /* if */

    if ((strlen(Sys_Info.sysname) + strlen(Sys_Info.release)) >= MAX_OS_LEN) {
#ifdef DEBUG_MODULE
	fprintf(stderr, "Get_OS_Name: OS name too long\n");
#else
	syslog(LOG_WARN, "Get_OS_Name: OS name too long\n");
#endif
	return Error_Code;
    } /* if */

    strcpy(OS_Name, Sys_Info.sysname);
    strcat(OS_Name, Sys_Info.release);
    return 0;
} /* Get_OS_Name */

/*
 * Get_Mach_Name - Return the fully qualified name of this node 
 * Note: This uses getdomainname to get the domain name for non-Linux support
 * Input: Node_Name - buffer for the node name, must be at least MAX_NAME_LEN characters
 * Output: Node_Name - filled in with node name
 *         return code - 0 if no error, otherwise errno
 */
int Get_Mach_Name(char *Node_Name) {
    int Error_Code;
    char Host_Name[MAX_NAME_LEN];
    char Domain_Name[MAX_NAME_LEN];

    Error_Code = gethostname(Host_Name, sizeof(Host_Name));
    if (Error_Code != 0) {
#ifdef DEBUG_MODULE
	fprintf(stderr, "Get_Mach_Name: gethostname error %d\n", Error_Code);
#else
	syslog(LOG_ERR, "Get_Mach_Name: gethostname error %d\n", Error_Code);
#endif
    } /* if */

    Error_Code = getdomainname(Domain_Name, sizeof(Domain_Name));
    if (Error_Code != 0) {
#ifdef DEBUG_MODULE
	fprintf(stderr, "Get_Mach_Name: getdomainname error %d\n", Error_Code);
#else
	syslog(LOG_ERR, "Get_Mach_Name: getdomainname error %d\n", Error_Code);
#endif
    } /* if */
    if (strcmp(Domain_Name, "(none)") == 0) strcpy(Domain_Name, "llnl.gov");

    if ((strlen(Host_Name) + strlen(Domain_Name) + 1) >= MAX_NAME_LEN) return E2BIG;
    strcpy(Node_Name, Host_Name);
    strcat(Node_Name, ".");
    strcat(Node_Name, Domain_Name);
    return 0;
} /* Get_Mach_Name */

/*
 * Get_Memory - Return the count of CPUs on this system 
 * Input: RealMemory - buffer for the Real Memory size
 *        VirtualMemory - buffer for the Virtual Memory size
 * Output: RealMemory - the Real Memory size in MB, "1" if error
 *         VirtualMemory - the Virtual Memory size in MB, "1" if error
 *         return code - 0 if no error, otherwise errno
 */
int Get_Memory(int *RealMemory, int *VirtualMemory) {
    char buffer[128];
    FILE *Mem_Info_File;
    char *buf_ptr;

    *RealMemory = 1;
    *VirtualMemory = 1;
    Mem_Info_File = fopen("/proc/meminfo", "r");
    if (Mem_Info_File == NULL) {
#ifdef DEBUG_MODULE
	fprintf(stderr, "Get_Memory: error %d opening /proc/meminfo\n", errno);
#else
	syslog(LOG_ERR, "Get_Memory: error %d opening /proc/meminfo\n", errno);
#endif
	return errno;
    } /* if */

    while (fgets(buffer, sizeof(buffer), Mem_Info_File) != NULL) {
	if ((buf_ptr=strstr(buffer, "MemTotal:")) != NULL) {
	    *RealMemory = (int)strtol(buf_ptr+9, (char **)NULL, 10);
	    if (strstr(buf_ptr, "kB") != NULL) *RealMemory /= 1024;
	} /* if */
	if ((buf_ptr=strstr(buffer, "SwapTotal:")) != NULL) {
	    *VirtualMemory = (int)strtol(buf_ptr+10, (char **)NULL, 10);
	    if (strstr(buf_ptr, "kB") != NULL) *VirtualMemory /= 1024;
	} /* if */
    } /* while */

    fclose(Mem_Info_File);
    return 0;
}

/*
 * Get_Speed - Return the speed of CPUs on this system (MHz clock)
 * Input: CPUs - buffer for the CPU speed
 * Output: CPUs - filled in with CPU speed, "1.0" if error
 *         return code - 0 if no error, otherwise errno
 */
int Get_Speed(float *Speed) {
    char buffer[128];
    FILE *CPU_Info_File;
    char *buf_ptr1, *buf_ptr2;

    *Speed = 1.0;
    CPU_Info_File = fopen("/proc/cpuinfo", "r");
    if (CPU_Info_File == NULL) {
#ifdef DEBUG_MODULE
	fprintf(stderr, "Get_Speed: error %d opening /proc/cpuinfo\n", errno);
#else
	syslog(LOG_ERR, "Get_Speed: error %d opening /proc/cpuinfo\n", errno);
#endif
	return errno;
    } /* if */

    while (fgets(buffer, sizeof(buffer), CPU_Info_File) != NULL) {
	if ((buf_ptr1=strstr(buffer, "cpu MHz")) != NULL) {
	    buf_ptr1 += 7;
	    buf_ptr2 = strstr(buf_ptr1, ":");
	    if (buf_ptr2 != NULL) buf_ptr1=buf_ptr2+1;
	    *Speed = (float)strtod(buf_ptr1, (char **)NULL);
	} /* if */
    } /* while */

    fclose(CPU_Info_File);
    return 0;
} /* Get_Speed */

/*
 * Get_TmpDisk - Return the total size of /var/tmp and /tmp on this system 
 * Input: TmpDisk - buffer for the disk space size
 * Output: TmpDisk - filled in with disk space size in MB, "1" if error
 *         return code - 0 if no error, otherwise errno
 */
int Get_TmpDisk(int *TmpDisk) {
    size_t Page_Size;

    *TmpDisk = 1;
    Page_Size = getpagesize();
/* Get code from DF command */
    return 0;
} /* Get_TmpDisk */
