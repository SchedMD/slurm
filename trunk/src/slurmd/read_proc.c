/*
 * read_proc.c - Read the system's process table. This is used to 
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

#include <src/slurmctld/slurmctld.h>
#define SESSION_RECS 50

int hertz = 0;

int iteration = -1;
int sess_rec_cnt = 0;
struct sess_record {
    int uid;
    int session;
    long unsigned time;		/* Total system and user time, all processes */
    long resident_set_size;	/* Total Resident Set Size, all processes */
    int iteration;		/* Defunct records have value -1 */
    int processes;		/* Count of processes */
};
struct sess_record *session_ptr;

#define BUF_SIZE 1024
#define DEBUG_MODULE 1
#define DEBUG_SYSTEM 1

int dump_proc(int uid, int sid);
int init_proc();
int parse_proc_stat(char* proc_stat, int *session, 
		long unsigned *time, long *resident_set_size);
int read_proc();

#if DEBUG_MODULE
/* main is used here for testing purposes only */
int 
main(int argc, char * argv[]) 
{
	int error_code, i, iterations, uid;
	if ((argc < 2) || (argc > 3)) {
		printf ("Usage: %s <iterations> [<uid>]\n", argv[0]);
		exit (0);
	} 
	if (argc == 3) 
		uid = atoi (argv[2]);
	else
		uid = -1;
	iterations = atoi (argv[1]);
	error_code = init_proc ();
	if (error_code != 0) {
		printf("Error %d from init_proc\n", error_code);
		exit(1);
	} 

	for (i=0; i<iterations; i++) {
		if (i > 0) {
			sleep (10);
			printf("\n\n");
		} 
		error_code = read_proc ();
		if (error_code != 0) {
			printf ("Error %d from read_proc\n", error_code);
			exit (1);
		} 
		error_code = dump_proc (uid, -1);
		if (error_code != 0) {
			printf ("Error %d from dump_proc\n", error_code);
			exit (1);
		} 
	}
	exit (0);
}
#endif


/*
 * dump_proc - Print the contents of the Process table
 * Input: uid - optional UID filter, enter -1 if no uid filter
 * 	sid - optional session ID filter, enter -1 if no SID filter
 * Output: Return code is zero or errno
 */
int 
dump_proc(int uid, int sid) 
{
	struct sess_record *s_ptr;

	for (s_ptr=session_ptr; s_ptr<(session_ptr+sess_rec_cnt); s_ptr++) {
		if (s_ptr->iteration == -1) 
			continue;   /* Defunct */
		if ((uid != -1) && (uid != s_ptr->uid)) 
			continue;
		if ((sid != -1) && (sid != s_ptr->session))
			continue;
		printf ("uid=%d session=%d time=%lu resident_set_size=%ld iteration=%d processes=%d\n", 
			s_ptr->uid, s_ptr->session, s_ptr->time, 
			s_ptr->resident_set_size, s_ptr->iteration, s_ptr->processes);
	}
	return 0;
}


/*
 * init_proc - Initialize the sess_record data structure OR increase size as needed
 * Input: none
 * Output: Return code is zero or errno
 */
int 
init_proc() 
{
	struct sess_record *s_ptr;

	if (sess_rec_cnt == 0)
		session_ptr = (struct sess_record *) 
			malloc (sizeof (struct sess_record) * SESSION_RECS);
	else
		session_ptr = (struct sess_record *)
			realloc (session_ptr, sizeof (struct sess_record) * 
				(sess_rec_cnt+SESSION_RECS));

	if (session_ptr == NULL) {
		error ("init_proc: unable to allocate memory\n");
		exit(1);
	} 

	for (s_ptr= (session_ptr+sess_rec_cnt); 
	     s_ptr<(session_ptr+sess_rec_cnt+SESSION_RECS); s_ptr++) {
		s_ptr->iteration = -1;
	} 
	sess_rec_cnt += SESSION_RECS;
	return 0;
}


/*
 * parse_proc_stat - Break out all of a process' information from the stat file
 * Input: proc_stat - Process status info read from /proc/<pid>/stat
 *	session - Location into which the session ID is written
 *	time - Location into which total user and system time (in seconds) is written
 *	resident_set_size - Location into which the Resident Set Size is written
 * Output: Exit code is zero or errno
 *	Fill in contents of session, time, and resident_set_size
 */
int 
parse_proc_stat(char* proc_stat, int *session, long unsigned *time, 
	long *resident_set_size) {
	int pid, ppid, pgrp, tty, tpgid;
	char cmd[16], state[1];
	long unsigned flags, min_flt, cmin_flt, maj_flt, cmaj_flt, utime, stime;
	long cutime, cstime, priority, nice, timeout, it_real_value;
	long unsigned start_time, vsize;
	long unsigned resident_set_size_rlim, start_code, end_code;
	long unsigned start_stack, kstk_esp, kstk_eip;
	long unsigned w_chan, n_swap, sn_swap;
	int  l_proc;
	int num;
	char *str_ptr;
    
	/* split into "PID (cmd" and "<rest>" */
	str_ptr = (char *)strrchr(proc_stat, ')'); 
	*str_ptr = '\0';		/* replace trailing ')' with NULL */
	/* parse these two strings separately, skipping the leading "(". */
	memset (cmd, 0, sizeof(cmd));
	sscanf (proc_stat, "%d (%15c", &pid, cmd);   /* comm[16] in kernel */
	num = sscanf(str_ptr + 2,		/* skip space after ')' too */
		"%c "
		"%d %d %d %d %d "
		"%lu %lu %lu %lu %lu %lu %lu "
		"%ld %ld %ld %ld %ld %ld "
		"%lu %lu "
		"%ld "
		"%lu %lu %lu "
		"%lu %lu %lu "
		"%*s %*s %*s %*s " /* discard, no RT signals & Linux 2.1 used hex */
		"%lu %lu %lu %*d %d",
		state,
		&ppid, &pgrp, session, &tty, &tpgid,
		&flags, &min_flt, &cmin_flt, &maj_flt, &cmaj_flt, &utime, &stime, 
		&cutime, &cstime, &priority, &nice, &timeout, &it_real_value,
		&start_time, &vsize,
		resident_set_size,
		&resident_set_size_rlim, &start_code, &end_code, 
		&start_stack, &kstk_esp, &kstk_eip,
/*		&signal, &blocked, &sig_ignore, &sig_catch,   */ /* can't use */
		&w_chan, &n_swap, &sn_swap /* , &Exit_signal  */, &l_proc);
	*time = (utime + stime) / hertz;
	return 0;
}

/* 
 * read_proc - Read into a table key information about every process on the system
 * Input: none
 * Output: Return code is zero or errno
 */
int 
read_proc() 
{
	DIR *proc_fs;
	struct dirent *proc_ent;
	int proc_fd, proc_stat_size, found, n;
	char proc_name[22], *proc_stat;
	struct stat buffer;
	int uid, session;
	long resident_set_size;
	long unsigned time;
	struct sess_record *s_ptr, *sess_free;

	/* Initialization */
	if (hertz == 0) {
		hertz = sysconf(_SC_CLK_TCK);
		if (hertz == 0) {
			error ("read_proc: unable to get clock rate\n");
			hertz = 100;
		} 
	} 
	proc_stat_size = BUF_SIZE;
	proc_stat = (char *) malloc(proc_stat_size);
	if (proc_stat == NULL) {
		error ("read_proc: unable to allocate memory\n");
		return ENOMEM;
	} 
	proc_fs = opendir("/proc");
	if (proc_fs == NULL)
		error ("read_proc: opendir unable to open /proc, errno=%d\n", errno);
	iteration++;

	/* Read the entries */
	while ((proc_ent = readdir(proc_fs)) != (struct dirent *)NULL) {
		if (proc_ent->d_name[0] < '0') 
			continue;	/* Not "real" process ID */
		if (proc_ent->d_name[0] > '9') 
			continue;	/* Not "real" process ID */
		if (strlen(proc_ent->d_name) > 10) {
			/* make proc_name longer and change this value */
			error ("read_proc: process ID number too long\n");
			continue;
		} 
		sprintf (proc_name, "/proc/%s/stat", proc_ent->d_name);
		proc_fd = open (proc_name, O_RDONLY, 0);
		if (proc_fd == -1) 
			continue;  /* process is now gone */
		while ((n = read(proc_fd, proc_stat, proc_stat_size)) > 0) {
			if (n < (proc_stat_size-1)) break;
			proc_stat_size += BUF_SIZE;
			proc_stat = (char *)realloc(proc_stat, proc_stat_size);
			if (proc_stat == NULL) {
				error ("read_proc: unable to allocate memory\n");
				close(proc_fd);
				closedir(proc_fs);
				return ENOMEM;
			} 
			if (lseek(proc_fd, (off_t) 0, SEEK_SET) != 0) 
				break;
		}
		fstat(proc_fd, &buffer);
		close(proc_fd);
		if (n <= 0) 
			continue;
		uid = buffer.st_uid;
		parse_proc_stat (proc_stat, &session, &time, &resident_set_size);
		found = 0;
		sess_free = NULL;
		for (s_ptr=session_ptr; s_ptr<(session_ptr+sess_rec_cnt); s_ptr++) {
			if (s_ptr->iteration == -1) {
				if (sess_free == NULL) 
					sess_free = s_ptr;
				continue;
			} 
			if (s_ptr->session != session) 
				continue;
			if (s_ptr->iteration != iteration) {
				s_ptr->iteration = iteration;
				s_ptr->processes = 0;
				s_ptr->resident_set_size       = 0;
				s_ptr->time      = 0;
			} 
			s_ptr->processes += 1;
			s_ptr->resident_set_size       += resident_set_size;
			s_ptr->time      += time;
			found = 1;
			break;
		} 
		if (found == 0) {
			if (sess_free == NULL) {
				if (init_proc() != 0)
					exit(1); /* no memory, we're dead */
				for (s_ptr=session_ptr; 
				     s_ptr<(session_ptr+sess_rec_cnt); s_ptr++) {
					if (s_ptr->iteration != -1) 
						continue;
					sess_free = s_ptr;
					break;
				} 
				if (sess_free == NULL) {
					error ("read_proc: Internal error\n");
					exit(1);
				} 
			} 
			sess_free->iteration = iteration;
			sess_free->processes = 1;
			sess_free->resident_set_size       = resident_set_size;
			sess_free->session   = session;
			sess_free->time      = time;
			sess_free->uid       = uid;
		} 
	} /* while */

	/* Termination */
	free(proc_stat);
	closedir(proc_fs);
	for (s_ptr=session_ptr; s_ptr<(session_ptr+sess_rec_cnt); s_ptr++) {
		if (s_ptr->iteration != iteration) 
			s_ptr->iteration=-1;   /* Defunct */
	} 
	return 0;
}
