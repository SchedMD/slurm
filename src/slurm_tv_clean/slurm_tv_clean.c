/*****************************************************************************\
 * slurm_tv_clean.c - This daemon records the SLURM jobs which are under 
 *	the control of TotalView. If TotalView terminates the srun command 
 *	before srun terminates the slurm job, this daemon explicitly cancels
 *	that job. To be deleted, the slurm job must uniquely match the local  
 *	uid, sid, hostname, and start time (within some delta). Local process
 *	commands to match are "srun" and "tv*main". Other process names will 
 *	not be recognized.
 *
 *	NOTE: This daemon was prepared as a temporary measure to deal 
 *	with TotalView's abrupt termination of srun and will not be needed 
 *	once the slurmctld daemon is used to periodically test for the 
 *	existence of srun and perform clean-up as needed.
 *****************************************************************************
 *  Copyright (C) 2003 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Moe Jette <jette@llnl.gov>
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <slurm/slurm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#define _GNU_SOURCE
#include <getopt.h>

#include "src/common/daemonize.h"
#include "src/common/fd.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#define BUF_SIZE         2048
#define DEFAULT_DEBUG    0
#define DEFAULT_LOG_FILE  NULL
#define DEFAULT_PID_FILE  "/var/run/slurm_tv_clean.pid"
#define SLEEP_SECONDS    30
#define SRUN_COMMAND     "srun"
#define DEFAULT_VERBOSE  0

typedef struct {
	pid_t pid;
	pid_t ppid;
	int   sid;
	uid_t uid;
} proc_rec_t;

typedef struct {
	pid_t tv_pid;
	pid_t srun_pid;
	pid_t job_sid;
	uid_t job_uid;
	time_t start_time;
	int active;
} job_rec_t;

struct {
	int   debug;
	int   verbose;
	char *log_file;
	char *pid_file;
} config;
static int term_flag = 0;
static log_options_t log_opts = LOG_OPTS_INITIALIZER;

static void _cancel_defunct_jobs(List job_list);
static void _delete_jobs(void *x);
static void _delete_procs(void *x);
static int  _find_by_ppid(void *x, void *key);
static uint32_t _find_unique_job_id(job_rec_t *job_ptr, 
		job_info_msg_t *job_info_msg_ptr);
static int  _kill_job(job_rec_t *job_ptr);
static void _kill_old_tv_clean(void);
static int  _load_jobs (job_info_msg_t ** job_buffer_pptr);
static void _mark_all_jobs_inactive(List job_list);
static int  _parse_command_line(int argc, char *argv[]);
static void _parse_proc_stat(char* proc_stat, char **proc_cmd, int *proc_ppid, 
		int *proc_sid);
static void _print_version(void);
static int  _read_procs(List srun_list, List tv_list);
static void _sig_handler(int sig_num);
static int  _time_valid(time_t slurm_time, time_t proc_time);
static int  _tv_cmd_cmp(char *proc_cmd);
static void _update_job(List job_list, proc_rec_t *srun_ptr, 
		proc_rec_t *tv_ptr);
static int  _update_job_recs(List srun_list, List tv_list, List job_list);
static void _update_logging(void);
static void _usage (void);

int main(int argc, char *argv[])
{
	/* Log to stderr and syslog until becomes a daemon */
	List job_list, srun_list, tv_list;
	int pid_fd;

	/* Establish initial configuration */
	log_init(argv[0], log_opts, LOG_DAEMON, NULL);
	if (_parse_command_line(argc, argv))
		return 1;
	_update_logging();
	if ((config.debug == 0) && (daemon(1, 1)))
		error("daemon error %m");
	_kill_old_tv_clean();
	pid_fd = create_pidfile(config.pid_file);

	/* Collect data forever (or SIGTERM) */
	(void) signal(SIGTERM, &_sig_handler);
	job_list = list_create(_delete_jobs);
	while (!term_flag) {
		time_t now = time(NULL);
		debug("-------- %s", ctime(&now));
		srun_list = list_create(_delete_procs);
		tv_list   = list_create(_delete_procs);
		if (_read_procs(srun_list, tv_list) == 0) { 
			_update_job_recs(srun_list, tv_list, job_list);
			_cancel_defunct_jobs(job_list);
		}
		list_destroy(srun_list);
		list_destroy(tv_list);
		log_flush();
		sleep(SLEEP_SECONDS);
	}

	info("slurm_tv_clean terminating");
	close(pid_fd);
	if (unlink(config.pid_file) < 0)
		error("Unable to remove pidfile '%s': %m", config.pid_file);
	list_destroy(job_list);
	log_fini();
	return 0;
}

/* convert to getopt */
static int _parse_command_line(int argc, char *argv[])
{
	static struct option long_options[] = {
		{"debug",   0, 0, 'D'},
		{"help",    0, 0, 'h'},
		{"logfile", 1, 0, 'L'},
		{"pidfile", 1, 0, 'p'},
		{"usage",   0, 0, 'u'},
		{"verbose", 0, 0, 'v'},
		{"version", 0, 0, 'V'},
	};
	int opt_char, option_index;

	config.debug    = DEFAULT_DEBUG;
	config.log_file = DEFAULT_LOG_FILE;
	config.pid_file = DEFAULT_PID_FILE;
	config.verbose  = DEFAULT_VERBOSE;

	while((opt_char = getopt_long(argc, argv, "DhL:p:uvV",
			long_options, &option_index)) != -1) {
		if      (opt_char == (int)'D')
			config.debug++;
		else if (opt_char == (int)'h') {
			_usage ();
			exit(0);
		}
		else if (opt_char == (int)'L')
			config.log_file = xstrdup(optarg);
		else if (opt_char == (int)'p')
			config.pid_file = xstrdup(optarg);
		else if (opt_char == (int)'u') {
			_usage ();
			exit(0);
		}
		else if (opt_char == (int)'v')
			config.verbose++;
		else if (opt_char == (int)'V') {
			_print_version();
			exit(0);
		}
		else {
			fprintf(stderr, "getopt error, returned %c\n", opt_char);
			return 1;
		}
	}

	if (config.debug) {
		printf("--------------\n");
		printf("debug   = %d\n", config.debug);
		printf("logfile = %s\n", config.log_file);
		printf("pidfile = %s\n", config.pid_file);
		printf("verbose = %d\n", config.verbose);
		printf("--------------\n");
	}
	return 0;
}

static void _usage (void)
{
	printf("Usage: slurm_tv_clean [OPTIONS]");
	printf("  -D          Run daemon in foreground.\n");
	printf("  -h          Print this help message.\n");
	printf("  -L logfile  Log messages to the specified file.\n");
	printf("  -p pidfile  Log daemon's pid to the specified file.\n");
	printf("  -u          Print this help message.\n");
	printf("  -v          Verbose mode. Multiple -v's increase verbosity.\n");
	printf("  -V          Print version and exit.\n");
}

static void _print_version(void)
{
	printf("%s %s\n", PACKAGE, SLURM_VERSION);
}

static void _update_logging(void)
{
	int log_level = LOG_LEVEL_INFO;

	if (config.verbose) {
		log_level = MIN(
			(LOG_LEVEL_INFO + config.verbose), LOG_LEVEL_DEBUG3);
	}

	log_opts.logfile_level = log_level;
	log_opts.stderr_level  = log_level;
	log_opts.syslog_level  = log_level;

	if (!config.debug) {
		log_opts.stderr_level  = LOG_LEVEL_QUIET;
		if (config.log_file)
			log_opts.syslog_level = LOG_LEVEL_QUIET;
	}

	(void) log_alter(log_opts, SYSLOG_FACILITY_DAEMON, config.log_file);
}

/* Kill the currently running slurm_tv_clean */
static void _kill_old_tv_clean(void)
{
	int fd;
	pid_t oldpid = read_pidfile(config.pid_file, &fd);
	if (oldpid != (pid_t) 0) {
		info("killing old slurmctld[%ld]", (long) oldpid);
		kill(oldpid, SIGTERM);

		/* 
		 * Wait for previous daemon to terminate
		 */
		if (fd_get_readw_lock(fd) < 0) 
			fatal ("unable to wait for readw lock: %m");
		(void) close(fd); /* Ignore errors */ 
	}
}

static void _sig_handler(int sig_num)
{
	term_flag = 1;
}

static void _delete_jobs(void *x)
{
	job_rec_t *job_rec = (job_rec_t *)x;
	xfree(job_rec);
}

static void _delete_procs(void *x)
{
	proc_rec_t *proc_rec = (proc_rec_t *)x;
	xfree(proc_rec);
}

/*
 * _read_procs - read all process table records, adds process records of 
 *	interest to srun_list or tv_list (for srun and TotalView respectively)
 * srun_list IN/OUT - process records of srun (empty list input)
 * tv_list   IN/OUT - process records of TotalView (empty list input)
 * RET - zero or error code
 */
static int _read_procs(List srun_list, List tv_list)
{
	static int proc_stat_size = 0;
	static char *proc_stat = NULL;

	DIR *proc_fs;
	struct dirent *proc_ent;
	proc_rec_t *proc_ptr;
	int proc_fd, n;
	char proc_name[NAME_MAX+20];
	struct stat stat_buf;
	uid_t  proc_uid;
	char  *proc_cmd;
	int    proc_pid, proc_ppid, proc_sid;

	/* Initialization */
	if (proc_stat_size == 0) {
		proc_stat_size = BUF_SIZE;
		proc_stat = xmalloc(proc_stat_size);
	}
	proc_fs = opendir("/proc");
	if (proc_fs == NULL) {
		error ("read_proc: opendir unable to open /proc %m\n");
		return errno;
	}

	/* Read the entries */
	while ((proc_ent = readdir(proc_fs))) {
		char *end_ptr;
		if ((proc_ent->d_name == NULL) ||
		    (proc_ent->d_name[0] == '\0'))
			continue;	/* invalid pid */
		proc_pid = strtol(proc_ent->d_name, &end_ptr, 10);
		if (end_ptr[0] != '\0')
			continue;	/* invalid pid */
		sprintf (proc_name, "/proc/%d/stat", proc_pid);
		proc_fd = open (proc_name, O_RDONLY, 0);
		if (proc_fd == -1) 
			continue;  /* process is now gone */
		while ((n = read(proc_fd, proc_stat, proc_stat_size)) > 0) {
			if (n < proc_stat_size)
				break;
			proc_stat_size += BUF_SIZE;
			xrealloc(proc_stat, proc_stat_size);
			if (lseek(proc_fd, (off_t) 0, SEEK_SET) != 0) 
				break;
		}
		fstat(proc_fd, &stat_buf);
		close(proc_fd);
		if (n <= 0) 
			continue;
		proc_uid = stat_buf.st_uid;
		_parse_proc_stat (proc_stat, &proc_cmd, &proc_ppid, &proc_sid);
		if (strcmp(proc_cmd, SRUN_COMMAND) &&
		    _tv_cmd_cmp(proc_cmd)) {
			xfree(proc_cmd);	/* don't save */
			continue;
		}
		debug("Found proc cmd=%s, pid=%d, ppid=%d, sid=%d, uid=%d",
			proc_cmd, proc_pid, proc_ppid, 
			proc_sid, proc_uid);
		proc_ptr = xmalloc(sizeof(proc_rec_t));
		proc_ptr->pid  = proc_pid;
		proc_ptr->ppid = proc_ppid;
		proc_ptr->sid  = proc_sid;
		proc_ptr->uid  = proc_uid;
		if (strcmp(proc_cmd, SRUN_COMMAND) == 0)
			list_append(srun_list, proc_ptr);
		else
			list_append(tv_list, proc_ptr);
		xfree(proc_cmd);
	}

	/* Termination */
	closedir(proc_fs);
	return 0;
}

/*
 * parse_proc_stat - Break out a process' information from its stat file
 * IN proc_stat - Process status info read from /proc/<pid>/stat
 * OUT proc_cmd - Location into which the process' name is written, must 
 *		be xfree'd by the caller
 * OUT proc_ppid - Location into which the process' parent process ID is 
 *		written
 * OUT proc_sid - Location into which the process' session ID is written
 */
static void _parse_proc_stat(char* proc_stat, char **proc_cmd, int *proc_ppid, 
		int *proc_sid)
{
	int pid, ppid, pgrp, sid, tty, tpgid;
	char *cmd, state[1];
	long unsigned flags, min_flt, cmin_flt, maj_flt, cmaj_flt;
	long unsigned utime, stime;
	long cutime, cstime, priority, nice, timeout, it_real_value;
	long unsigned start_time, vsize, resident_set_size;
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
	cmd = xmalloc(16);
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
		&ppid, &pgrp, &sid, &tty, &tpgid,
		&flags, &min_flt, &cmin_flt, &maj_flt, &cmaj_flt, &utime, &stime, 
		&cutime, &cstime, &priority, &nice, &timeout, &it_real_value,
		&start_time, &vsize,
		&resident_set_size,
		&resident_set_size_rlim, &start_code, &end_code, 
		&start_stack, &kstk_esp, &kstk_eip,
/*		&signal, &blocked, &sig_ignore, &sig_catch, */ /* can't use */
		&w_chan, &n_swap, &sn_swap /* , &Exit_signal  */, &l_proc);

	*proc_cmd  = cmd;
	*proc_ppid = ppid;
	*proc_sid  = sid;
}

/* Return zero only if the supplied command is that of TotalView, 
 *	namely starting with "tv" and ending with "main" */
static int _tv_cmd_cmp(char *proc_cmd)
{
	int len;

	if (strncmp(proc_cmd, "tv", 2))
		return 1;

	len = strlen(proc_cmd);
	if (strcmp(&proc_cmd[len-4], "main"))
		return 2;

	return 0;
}

/*
 * _update_job_recs - update our job records based upon the process records; 
 *	for each tv_list record, look for an srun_list record with TV as 
 *	its ppid and add or update a record for that job in job_list 
 * srun_list IN - specifications for active srun processes
 * tv_list IN - specifications for active TotalView processes
 * job_list IN/OUT - specifications for active slurm jobs
 */
static int  _update_job_recs(List srun_list, List tv_list, List job_list)
{
	ListIterator srun_iterator = list_iterator_create(srun_list);
	ListIterator tv_iterator   = list_iterator_create(tv_list);
	proc_rec_t *srun_ptr, *tv_ptr;

	_mark_all_jobs_inactive(job_list);
	while ( (tv_ptr = list_next(tv_iterator)) ) {
		while ( (srun_ptr= list_find(srun_iterator, _find_by_ppid, 
		                             &(tv_ptr->pid))) )
			_update_job(job_list, srun_ptr, tv_ptr);
		list_iterator_reset(srun_iterator);
	}
	list_iterator_destroy(srun_iterator);
	list_iterator_destroy(tv_iterator);
	return 0;
}

static void _mark_all_jobs_inactive(List job_list)
{
	ListIterator job_iterator = list_iterator_create(job_list);
	job_rec_t *job_ptr;

	while ( (job_ptr = list_next(job_iterator)) )
		job_ptr->active = 0;
	list_iterator_destroy(job_iterator);
}

static int _find_by_ppid(void *x, void *key)
{
	proc_rec_t *srun_ptr = (proc_rec_t *) x;
	pid_t *ppid = (pid_t *) key;

	if (srun_ptr->ppid == *ppid)
		return 1;
	else
		return 0;
}

static void _update_job(List job_list, proc_rec_t *srun_ptr, 
		proc_rec_t *tv_ptr)
{
	ListIterator job_iterator = list_iterator_create(job_list);
	job_rec_t *job_ptr;

	while ( (job_ptr = list_next(job_iterator)) ) {
		if ((job_ptr->srun_pid != srun_ptr->pid) ||
		    (job_ptr->job_sid  != srun_ptr->sid) ||
		    (job_ptr->job_uid  != srun_ptr->uid) ||
		    (job_ptr->tv_pid   != tv_ptr->pid  ))
			continue;
		job_ptr->active = 1;
		break;
	}
	list_iterator_destroy(job_iterator);
	if (job_ptr)
		return;

	verbose("Add job srun_pid=%d, tv_pid=%d, sid=%d, uid=%d",
		srun_ptr->pid, tv_ptr->pid, srun_ptr->sid, srun_ptr->uid);
	job_ptr = xmalloc(sizeof(job_rec_t));
	job_ptr->srun_pid = srun_ptr->pid;
	job_ptr->tv_pid   = tv_ptr->pid;
	job_ptr->job_sid  = srun_ptr->sid;
	job_ptr->job_uid  = srun_ptr->uid;
	job_ptr->active   = 1;
	job_ptr->start_time = time(NULL);
	list_append(job_list, job_ptr);
}

/* cancel slurm jobs for which the srun command has terminated */
static void _cancel_defunct_jobs(List job_list)
{
	ListIterator job_iterator = list_iterator_create(job_list);
	job_rec_t *job_ptr;

	while ( (job_ptr = list_next(job_iterator)) ) {
		if ((job_ptr->active == 0) &&
		    (_kill_job(job_ptr) == 0))
			list_delete(job_iterator);
	}
	list_iterator_destroy(job_iterator);
}

/* The totalview/srun job at specified sid is now complete, cancel the 
 * corresponding slurm job as needed.
 * RET zero on success */ 
static int _kill_job(job_rec_t *job_ptr)
{
	int error_code;
	job_info_msg_t *job_info_msg_ptr;
	uint32_t job_id;

	if ((error_code = _load_jobs(&job_info_msg_ptr)))
		return error_code;	/* retry later */

	/* search for matching node/sid/uid/start_time */
	job_id = _find_unique_job_id(job_ptr, job_info_msg_ptr);
	if (job_id == 0) {
		error("No unique slurm job for uid=%u sid=%u, possible orphan",
			job_ptr->job_uid, job_ptr->job_sid);
		return 0;
	}

	/* issue job cancel request */
	if (slurm_kill_job(job_id, SIGKILL)) {
		int rc = slurm_get_errno();
		if (rc == ESLURM_ALREADY_DONE)
			info("Slurm job %u for uid=%u sid=%u already done", 
			     job_id, job_ptr->job_uid, job_ptr->job_sid);
		else
			error("slurm_kill_job job_id=%u uid=%u sid=%u: %s",
			      job_id, job_ptr->job_uid, job_ptr->job_sid,
			      slurm_strerror(rc));
	} else
		info("Killed slurm job %u for uid=%u sid=%u", job_id, 
		     job_ptr->job_uid, job_ptr->job_sid);

	return 0;	/* don't bother retrying */
}

static int _load_jobs (job_info_msg_t ** job_buffer_pptr) 
{
	int error_code;
	static job_info_msg_t *old_job_buffer_ptr = NULL;
	job_info_msg_t * job_buffer_ptr = NULL;

	if (old_job_buffer_ptr) {
		error_code = slurm_load_jobs (old_job_buffer_ptr->last_update, 
					&job_buffer_ptr);
		if (error_code == SLURM_SUCCESS)
			slurm_free_job_info_msg (old_job_buffer_ptr);
		else if (slurm_get_errno () == SLURM_NO_CHANGE_IN_DATA) {
			job_buffer_ptr = old_job_buffer_ptr;
			error_code = SLURM_SUCCESS;
		} else {
			error("slurm_load_jobs: %s", 
				slurm_strerror(slurm_get_errno()));
		}
	}
	else
		error_code = slurm_load_jobs ((time_t) NULL, &job_buffer_ptr);

	if (error_code == SLURM_SUCCESS) {
		old_job_buffer_ptr = job_buffer_ptr;
		*job_buffer_pptr = job_buffer_ptr;
	}

	return error_code;
}

/* _find_unique_job_id - find the slurm job_id for a local TV/srun session
 *	Must match: uid, sid, hostname, start_time (within delta)
 * RET slurm job_id corresponding to the given job record or zero
 */
static uint32_t _find_unique_job_id(job_rec_t *job_ptr, 
		job_info_msg_t *job_info_msg_ptr)
{
	static char host[64] = "";
	uint32_t job_id = 0;
	int i;
	job_info_t *slurm_job_ptr;

	if (host[0] == '\0')
		getnodename(host, sizeof(host));

	for (i = 0; i < job_info_msg_ptr->record_count; i++) {
		slurm_job_ptr = &job_info_msg_ptr->job_array[i];
		if (slurm_job_ptr->user_id != job_ptr->job_uid)
			continue;
		if (slurm_job_ptr->alloc_sid != job_ptr->job_sid)
			continue;
		if (strcmp(slurm_job_ptr->alloc_node, host))
			continue;
		if (!_time_valid(slurm_job_ptr->start_time, 
		                 job_ptr->start_time))
			continue;

		/* Matches all job parameters */
		if (job_id) {
			debug("Multiple possible jobs %u and %u", job_id, 
			      slurm_job_ptr->job_id);
			job_id = 0;
			break;
		} else
			job_id = slurm_job_ptr->job_id;
	}

	return job_id;
}

/* Return 1 if the slurm job could be that of this process */
static int _time_valid(time_t slurm_time, time_t proc_time)
{
	long delta_t = difftime(proc_time, slurm_time);
	if (delta_t <= (SLEEP_SECONDS+1))
		return 1;
	else
		return 0;
}
