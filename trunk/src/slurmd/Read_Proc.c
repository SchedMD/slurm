/*
 * Read_Proc.c - Read the system's process table. This is used to 
 * determine if a job is still executing and how many resources 
 * are being allocated to it.
 *
 * Author: Moe Jette, jette@llnl.gov
 */
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <syslog.h>

#include "slurm.h"

#define BUF_SIZE 1024
#define DEBUG_MODULE 1
#define DEBUG_SYSTEM 1


int Read_Proc();

#if DEBUG_MODULE
/* main is used here for testing purposes only */
main(int argc, char * argv[]) {
    int Error_Code, uid;
    char Out_Line[BUF_SIZE];

    if (argc > 2) {
	printf("Usage: %s [<uid>]\n", argv[0]);
	exit(0);
    } /* if */
    if (argc == 2) uid=atoi(argv[1]);

    Error_Code = Read_Proc();
    if (Error_Code != 0) {
	printf("Error %d from Read_Proc\n", Error_Code);
	exit(1);
    } /* if */

/*     while (...) {
	if ((argc == 1) || (uid == pid->uid))
	    printf("uid=%d, sid=%d, cmd=%s, rss=%d, CPU=%d\n", ...
    } */

    exit(0);
} /* main */
#endif

/* 
 * Read_Proc - Read into a table key information about every process on the system
 * Input: none
 * Output: Return code is error number or zero if fine
 */
int Read_Proc() {
    DIR *Proc_FS;
    struct dirent *Proc_Ent;

    /* Initialization */
    Proc_FS = opendir("/proc");
    if (Proc_FS == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Read_Proc: opendir unable to open /proc, errno=%d\n", errno);
#else
	syslog(LOG_ALERT, "Read_Proc: opendir unable to open /proc, errno=%d\n", errno);
#endif
    } /* if */

    /* Read the entries */
    while ((Proc_Ent = readdir(Proc_FS)) != (struct dirent *)NULL) {
	if (Proc_Ent->d_name[0] < '0') continue;	/* Not "real" process ID */
	if (Proc_Ent->d_name[0] > '9') continue;	/* Not "real" process ID */
printf("%s\n",Proc_Ent->d_name);
    } /* while */

    /* Termination */
    closedir(Proc_FS);
} /* Read_Proc */
