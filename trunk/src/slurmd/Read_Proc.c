/*
 * Read_Proc.c - Read the system's process table. This is used to 
 * determine if a job is still executing and how many resources 
 * are being allocated to it.
 *
 * Author: Moe Jette, jette@llnl.gov
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>

#include "slurm.h"

#define BUF_SIZE 1024
#define DEBUG_MODULE 1
#define DEBUG_SYSTEM 1


int Parse_Proc_Stat(char* Proc_Stat);
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

    exit(0);
} /* main */
#endif

/* Parse_Proc_Stat - Break out all of a process' information from the stat file */
int Parse_Proc_Stat(char* Proc_Stat) {
    int Pid, PPid, PGrp, Session, TTY, TPGID;
    char Cmd[16], State[1];
    long unsigned Flags, Min_Flt, CMin_Flt, Maj_Flt, CMaj_Flt, UTime, STime;
    long CuTime, CsTime, Priority, Nice, Timeout, It_Real_Value;
    long unsigned Start_Time, VSize;
    long RSS;
    long unsigned RSS_RLim, Start_Code, End_Code, Start_Stack, KStk_Esp, KStk_Eip;
    long unsigned WChan, NSwap, CnSwap;
    int  LProc;
    int num;
    char *str_ptr;
    
    str_ptr = (char *)strrchr(Proc_Stat, ')');		/* split into "PID (cmd" and "<rest>" */
    *str_ptr = '\0';			/* replace trailing ')' with NUL */
    /* parse these two strings separately, skipping the leading "(". */
    memset(Cmd, 0, sizeof(Cmd));
    sscanf(Proc_Stat, "%d (%15c", &Pid, Cmd);   /* comm[16] in kernel */
    num = sscanf(str_ptr + 2,			/* skip space after ')' too */
       "%c "
       "%d %d %d %d %d "
       "%lu %lu %lu %lu %lu %lu %lu "
       "%ld %ld %ld %ld %ld %ld "
       "%lu %lu "
       "%ld "
       "%lu %lu %lu %lu %lu %lu "
       "%*s %*s %*s %*s " /* discard, no RT signals & Linux 2.1 used hex */
       "%lu %lu %lu %*d %d",
       &State,
       &PPid, &PGrp, &Session, &TTY, &TPGID,
       &Flags, &Min_Flt, &CMin_Flt, &Maj_Flt, &CMaj_Flt, &UTime, &STime, 
       &CuTime, &CsTime, &Priority, &Nice, &Timeout, &It_Real_Value,
       &Start_Time, &VSize,
       &RSS,
       &RSS_RLim, &Start_Code, &End_Code, &Start_Stack, &KStk_Esp, &KStk_Eip,
/*     &Signal, &Blocked, &SigIgnore, &SigCatch,   */ /* can't use */
       &WChan, &NSwap, &CnSwap /* , &Exit_Signal  */, &LProc);
printf("%d %s\n",Pid, Cmd);
} /* Parse_Proc_Stat */

/* 
 * Read_Proc - Read into a table key information about every process on the system
 * Input: none
 * Output: Return code is error number or zero if fine
 */
int Read_Proc() {
    DIR *Proc_FS;
    struct dirent *Proc_Ent;
    int Proc_FD, Proc_Stat_Size, n;
    char Proc_Name[22], *Proc_Stat;

    /* Initialization */
    Proc_Stat_Size = BUF_SIZE;
    Proc_Stat = (char *)malloc(Proc_Stat_Size);
    if (Proc_Stat == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Read_Proc: unable to allocate memory\n");
#else
	syslog(LOG_ALERT, "Read_Proc: unable to allocate memory\n");
#endif
	return ENOMEM;
    } /* if */
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
	if (strlen(Proc_Ent->d_name) > 10) {
	    /* If you see this, make Proc_Name longer and change this value */
#if DEBUG_SYSTEM
	    fprintf(stderr, "Read_Proc: process ID number too long\n");
#else
	    syslog(LOG_ERR, "Read_Proc: oprocess ID number too long\n");
#endif  
	    continue;
	} /* if */
	sprintf(Proc_Name, "/proc/%s/stat", Proc_Ent->d_name);
	Proc_FD = open(Proc_Name, O_RDONLY, 0);
	if (Proc_FD == -1) continue;  /* process is now gone */
	while ((n = read(Proc_FD, Proc_Stat, Proc_Stat_Size)) > 0) {
	    if (n < (Proc_Stat_Size-1)) break;
	    Proc_Stat_Size += BUF_SIZE;
	    Proc_Stat = (char *)malloc(Proc_Stat_Size);
	    if (Proc_Stat == NULL) {
#if DEBUG_SYSTEM
		fprintf(stderr, "Read_Proc: unable to allocate memory\n");
#else
		syslog(LOG_ALERT, "Read_Proc: unable to allocate memory\n");
#endif
		close(Proc_FD);
		closedir(Proc_FS);
		return ENOMEM;
	    } /* if */
	    if (lseek(Proc_FD, (off_t) 0, SEEK_SET) != 0) break;
	} /* while */
	close(Proc_Name);
	if (n <= 0) continue;
	Parse_Proc_Stat(Proc_Stat);
    } /* while */

    /* Termination */
    free(Proc_Stat);
    closedir(Proc_FS);
} /* Read_Proc */

