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


void stat2proc(char* S, proc_t* P) {
    int num;
    char* tmp = strrchr(S, ')');	/* split into "PID (cmd" and "<rest>" */
    *tmp = '\0';			/* replace trailing ')' with NUL */
    /* parse these two strings separately, skipping the leading "(". */
    memset(P->cmd, 0, sizeof P->cmd);	/* clear even though *P xcalloc'd ?! */
    sscanf(S, "%d (%15c", &P->pid, P->cmd);   /* comm[16] in kernel */
    num = sscanf(tmp + 2,			/* skip space after ')' too */
       "%c "
       "%d %d %d %d %d "
       "%lu %lu %lu %lu %lu %lu %lu "
       "%ld %ld %ld %ld %ld %ld "
       "%lu %lu "
       "%ld "
       "%lu %lu %lu %lu %lu %lu "
       "%*s %*s %*s %*s " /* discard, no RT signals & Linux 2.1 used hex */
       "%lu %lu %lu %*d %d",
       &P->state,
       &P->ppid, &P->pgrp, &P->session, &P->tty, &P->tpgid,
       &P->flags, &P->min_flt, &P->cmin_flt, &P->maj_flt, &P->cmaj_flt, &P->utime, &P->stime,
       &P->cutime, &P->cstime, &P->priority, &P->nice, &P->timeout, &P->it_real_value,
       &P->start_time, &P->vsize,
       &P->rss,
       &P->rss_rlim, &P->start_code, &P->end_code, &P->start_stack, &P->kstk_esp, &P->kstk_eip,
/*     P->signal, P->blocked, P->sigignore, P->sigcatch,   */ /* can't use */
       &P->wchan, &P->nswap, &P->cnswap /* , &P->exit_signal  */, &P->lproc);
/* TODO: add &P->exit_signal support here, perhaps to identify Linux threads */
    
/*    fprintf(stderr, "stat2proc converted %d fields.\n",num); */
    if (P->tty == 0)
	P->tty = -1;  /* the old notty val, update elsewhere bef. moving to 0 */
    if (linux_version_code < LINUX_VERSION(1,3,39)) {
	P->priority = 2*15 - P->priority;	/* map old meanings to new */
	P->nice = 15 - P->nice;
    }
    if (linux_version_code < LINUX_VERSION(1,1,30) && P->tty != -1)
	P->tty = 4*0x100 + P->tty;		/* when tty wasn't full devno */
}
