/*
 * Read_Proc.c - Read the system's process table. This is used to 
 * determine if a job is still executing and how many resources 
 * are being allocated to it.
 *
 * Author: Moe Jette, jette@llnl.gov
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif 

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

#include "slurm.h"
#define SESSION_RECS 50

int Hertz = 0;

int Iteration = -1;
int Session_Record_Count = 0;
struct Session_Record {
    int Uid;
    int Session;
    long unsigned Time;		/* Total system and user time, all processes */
    long RSS;			/* Total Resident Set Size, all processes */
    int Iteration;		/* Defunct records have value -1 */
    int Processes;		/* Count of processes */
};
struct Session_Record *Session_Ptr;

#define BUF_SIZE 1024
#define DEBUG_MODULE 1
#define DEBUG_SYSTEM 1

int Dump_Proc(int uid, int sid);
int Init_Proc();
int Parse_Proc_Stat(char* Proc_Stat, int *Session, long unsigned *Time, long *RSS);
int Read_Proc();

#if DEBUG_MODULE
/* main is used here for testing purposes only */
main(int argc, char * argv[]) {
    int Error_Code, i, iterations, uid;
    char Out_Line[BUF_SIZE];

    if ((argc < 2) || (argc > 3)) {
	printf("Usage: %s <iterations> [<uid>]\n", argv[0]);
	exit(0);
    } /* if */
    if (argc == 3) 
	uid = atoi(argv[2]);
    else
	uid = -1;
    iterations = atoi(argv[1]);
    Error_Code = Init_Proc();
    if (Error_Code != 0) {
	printf("Error %d from Init_Proc\n", Error_Code);
	exit(1);
    } /* if */

    for (i=0; i<iterations; i++) {
	if (i > 0) {
	    sleep(10);
	    printf("\n\n");
	 } /* if */
	Error_Code = Read_Proc();
	if (Error_Code != 0) {
	    printf("Error %d from Read_Proc\n", Error_Code);
	    exit(1);
	 } /* if */
	Error_Code = Dump_Proc(uid, -1);
	if (Error_Code != 0) {
	    printf("Error %d from Dump_Proc\n", Error_Code);
	    exit(1);
	 } /* if */
    } /* for (i */

    exit(0);
} /* main */
#endif


/*
 * Dump_Proc - Print the contents of the Process table
 * Input: uid - optional UID filter, enter -1 if no uid filter
 * 	sid - optional Session ID filter, enter -1 if no SID filter
 * Output: Return code is zero or errno
 */
int Dump_Proc(int uid, int sid) {
    struct Session_Record *S_Ptr;

    for (S_Ptr=Session_Ptr; S_Ptr<(Session_Ptr+Session_Record_Count); S_Ptr++) {
	if (S_Ptr->Iteration == -1) continue;   /* Defunct */
	if ((uid != -1) && (uid != S_Ptr->Uid)) continue;
	if ((sid != -1) && (sid != S_Ptr->Session)) continue;
	printf("Uid=%d Session=%d Time=%lu RSS=%ld Iteration=%d Processes=%d\n", 
		S_Ptr->Uid, S_Ptr->Session, S_Ptr->Time, S_Ptr->RSS, S_Ptr->Iteration, S_Ptr->Processes);
    } /* for */
    return 0;
} /* Dump_Proc */


/*
 * Init_Proc - Initialize the Session_Record data structure OR increase size as needed
 * Input: none
 * Output: Return code is zero or errno
 */
int Init_Proc() {
    struct Session_Record *S_Ptr;

    if (Session_Record_Count == 0)
	Session_Ptr = (struct Session_Record *)malloc(sizeof(struct Session_Record) * SESSION_RECS);
    else
	Session_Ptr = (struct Session_Record *)realloc(Session_Ptr, sizeof(struct Session_Record) * (Session_Record_Count+SESSION_RECS));

    if (Session_Ptr == NULL) {
#if DEBUG_SYSTEM
	fprintf(stderr, "Init_Proc: unable to allocate memory\n");
#else
	syslog(LOG_ALARM, "Init_Proc: unable to allocate memory\n");
#endif
	exit(1);
    } /* if */

    for (S_Ptr=(Session_Ptr+Session_Record_Count); S_Ptr<(Session_Ptr+Session_Record_Count+SESSION_RECS); S_Ptr++) {
	S_Ptr->Iteration = -1;
    } /* for */
    Session_Record_Count += SESSION_RECS;
    return 0;
} /* Init_Proc */


/*
 * Parse_Proc_Stat - Break out all of a process' information from the stat file
 * Input: Proc_Stat - Process status info read from /proc/<pid>/stat
 *	Session - Location into which the Session ID is written
 *	Time - Location into which total user and system time (in seconds) is written
 *	RSS - Location into which the Resident Set Size is written
 * Output: Exit code is zero or errno
 *	Fill in contents of Session, Time, and RSS
 */
int Parse_Proc_Stat(char* Proc_Stat, int *Session, long unsigned *Time, long *RSS) {
    int Pid, PPid, PGrp, TTY, TPGID;
    char Cmd[16], State[1];
    long unsigned Flags, Min_Flt, CMin_Flt, Maj_Flt, CMaj_Flt, UTime, STime;
    long CuTime, CsTime, Priority, Nice, Timeout, It_Real_Value;
    long unsigned Start_Time, VSize;
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
       &PPid, &PGrp, Session, &TTY, &TPGID,
       &Flags, &Min_Flt, &CMin_Flt, &Maj_Flt, &CMaj_Flt, &UTime, &STime, 
       &CuTime, &CsTime, &Priority, &Nice, &Timeout, &It_Real_Value,
       &Start_Time, &VSize,
       RSS,
       &RSS_RLim, &Start_Code, &End_Code, &Start_Stack, &KStk_Esp, &KStk_Eip,
/*     &Signal, &Blocked, &SigIgnore, &SigCatch,   */ /* can't use */
       &WChan, &NSwap, &CnSwap /* , &Exit_Signal  */, &LProc);
    *Time = (UTime + STime) / Hertz;
} /* Parse_Proc_Stat */

/* 
 * Read_Proc - Read into a table key information about every process on the system
 * Input: none
 * Output: Return code is zero or errno
 */
int Read_Proc() {
    DIR *Proc_FS;
    struct dirent *Proc_Ent;
    int Proc_FD, Proc_Stat_Size, found, n;
    char Proc_Name[22], *Proc_Stat;
    struct stat Buffer;
    int Uid, Session;
    long RSS;
    long unsigned Time;
    struct Session_Record *S_Ptr, *Sess_Free;

    /* Initialization */
    if (Hertz == 0) {
	Hertz = sysconf(_SC_CLK_TCK);
	if (Hertz == 0) {
#if DEBUG_SYSTEM
	    fprintf(stderr, "Read_Proc: unable to get clock rate\n");
#else
	    syslog(LOG_ALERT, "Read_Proc: unable to get clock rate\n");
#endif
	    Hertz = 100;
	} /* if */
    } /* if */
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
    Iteration++;

    /* Read the entries */
    while ((Proc_Ent = readdir(Proc_FS)) != (struct dirent *)NULL) {
	if (Proc_Ent->d_name[0] < '0') continue;	/* Not "real" process ID */
	if (Proc_Ent->d_name[0] > '9') continue;	/* Not "real" process ID */
	if (strlen(Proc_Ent->d_name) > 10) {
	    /* If you see this, make Proc_Name longer and change this value */
#if DEBUG_SYSTEM
	    fprintf(stderr, "Read_Proc: process ID number too long\n");
#else
	    syslog(LOG_ERR, "Read_Proc: process ID number too long\n");
#endif  
	    continue;
	} /* if */
	sprintf(Proc_Name, "/proc/%s/stat", Proc_Ent->d_name);
	Proc_FD = open(Proc_Name, O_RDONLY, 0);
	if (Proc_FD == -1) continue;  /* process is now gone */
	while ((n = read(Proc_FD, Proc_Stat, Proc_Stat_Size)) > 0) {
	    if (n < (Proc_Stat_Size-1)) break;
	    Proc_Stat_Size += BUF_SIZE;
	    Proc_Stat = (char *)realloc(Proc_Stat_Size);
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
	fstat(Proc_FD, &Buffer);
	close(Proc_FD);
	if (n <= 0) continue;
	Uid = Buffer.st_uid;
	Parse_Proc_Stat(Proc_Stat, &Session, &Time, &RSS);
	found = 0;
	Sess_Free = NULL;
	for (S_Ptr=Session_Ptr; S_Ptr<(Session_Ptr+Session_Record_Count); S_Ptr++) {
	    if (S_Ptr->Iteration == -1) {
		if (Sess_Free == NULL) Sess_Free = S_Ptr;
		continue;
	    } /* if */
	    if (S_Ptr->Session != Session) continue;
	    if (S_Ptr->Iteration != Iteration) {
		S_Ptr->Iteration = Iteration;
		S_Ptr->Processes = 0;
		S_Ptr->RSS       = 0;
		S_Ptr->Time      = 0;
	    } /* if */
	    S_Ptr->Processes += 1;
	    S_Ptr->RSS       += RSS;
	    S_Ptr->Time      += Time;
	    found = 1;
	    break;
	} /* for */
	if (found == 0) {
	    if (Sess_Free == NULL) {
		if (Init_Proc() != 0) exit(1); /* no memory, we're dead */
		for (S_Ptr=Session_Ptr; S_Ptr<(Session_Ptr+Session_Record_Count); S_Ptr++) {
		    if (S_Ptr->Iteration != -1) continue;
		    Sess_Free = S_Ptr;
		    break;
		} /* for */
		if (Sess_Free == NULL) {
#if DEBUG_SYSTEM
		    fprintf(stderr, "Read_Proc: Internal error\n");
#else
		    syslog(LOG_ERR, "Read_Proc: Internal error\n");
#endif  
		    exit(1);
		} /* if */
	    } /* if */
	    Sess_Free->Iteration = Iteration;
	    Sess_Free->Processes = 1;
	    Sess_Free->RSS       = RSS;
	    Sess_Free->Session   = Session;
	    Sess_Free->Time      = Time;
	    Sess_Free->Uid       = Uid;
	} /* if */
    } /* while */

    /* Termination */
    free(Proc_Stat);
    closedir(Proc_FS);
    for (S_Ptr=Session_Ptr; S_Ptr<(Session_Ptr+Session_Record_Count); S_Ptr++) {
	if (S_Ptr->Iteration != Iteration) S_Ptr->Iteration=-1;   /* Defunct */
    } /* for */
    return 0;
} /* Read_Proc */

