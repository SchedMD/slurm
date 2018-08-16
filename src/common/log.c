/*****************************************************************************\
 *  log.c - slurm logging facilities
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  Much of this code was derived or adapted from the log.c component of
 *  openssh which contains the following notices:
 *****************************************************************************
 * Author: Tatu Ylonen <ylo@cs.hut.fi>
 * Copyright (c) 1995 Tatu Ylonen <ylo@cs.hut.fi>, Espoo, Finland
 *                    All rights reserved
 *
 * As far as I am concerned, the code I have written for this software
 * can be used freely for any purpose.  Any derived versions of this
 * software must be clearly marked as such, and if the derived work is
 * incompatible with the protocol description in the RFC file, it must be
 * called by a name other than "ssh" or "Secure Shell".
 *****************************************************************************
 * Copyright (c) 2000 Markus Friedl.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
\*****************************************************************************/

/*
** MT safe
*/
#define _GNU_SOURCE

#include "config.h"

#if HAVE_SYS_PRCTL_H
#  include <sys/prctl.h>
#endif

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"
#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_time.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"

#ifndef LINEBUFSIZE
#  define LINEBUFSIZE 256
#endif

#define NAMELEN 16

/*
** Define slurm-specific aliases for use by plugins, see slurm_xlator.h
** for details.
 */
strong_alias(log_init,		slurm_log_init);
strong_alias(log_reinit,	slurm_log_reinit);
strong_alias(log_fini,		slurm_log_fini);
strong_alias(log_alter,		slurm_log_alter);
strong_alias(log_alter_with_fp, slurm_log_alter_with_fp);
strong_alias(log_set_fpfx,	slurm_log_set_fpfx);
strong_alias(log_fp,		slurm_log_fp);
strong_alias(log_fatal,		slurm_log_fatal);
strong_alias(log_oom,		slurm_log_oom);
strong_alias(log_has_data,	slurm_log_has_data);
strong_alias(log_flush,		slurm_log_flush);
strong_alias(fatal,		slurm_fatal);
strong_alias(fatal_abort,	slurm_fatal_abort);
strong_alias(error,		slurm_error);
strong_alias(info,		slurm_info);
strong_alias(verbose,		slurm_verbose);
strong_alias(debug,		slurm_debug);
strong_alias(debug2,		slurm_debug2);
strong_alias(debug3,		slurm_debug3);
strong_alias(debug4,		slurm_debug4);
strong_alias(debug5,		slurm_debug5);
strong_alias(sched_error,	slurm_sched_error);
strong_alias(sched_info,	slurm_sched_info);
strong_alias(sched_verbose,	slurm_sched_verbose);
strong_alias(sched_debug,	slurm_sched_debug);
strong_alias(sched_debug2,	slurm_sched_debug2);
strong_alias(sched_debug3,	slurm_sched_debug3);

/*
** struct defining a "log" type
*/
typedef struct {
	char *argv0;
	char *fpfx;              /* optional prefix for logfile entries */
	FILE *logfp;             /* log file pointer                    */
	cbuf_t buf;              /* stderr data buffer                  */
	cbuf_t fbuf;             /* logfile data buffer                 */
	log_facility_t facility;
	log_options_t opt;
	unsigned initialized:1;
	uint16_t fmt;            /* Flag for specifying timestamp format */
	uint64_t debug_flags;
}	log_t;

char *slurm_prog_name = NULL;

/* static variables */
static pthread_mutex_t  log_lock = PTHREAD_MUTEX_INITIALIZER;
static log_t            *log = NULL;
static log_t            *sched_log = NULL;

#define LOG_INITIALIZED ((log != NULL) && (log->initialized))
#define SCHED_LOG_INITIALIZED ((sched_log != NULL) && (sched_log->initialized))
/* define a default argv0 */
#if HAVE_PROGRAM_INVOCATION_NAME
/* This used to use program_invocation_short_name, but on some systems
 * that gets truncated at 16 bytes, too short for our needs. */
extern char * program_invocation_name;
#  define default_name	program_invocation_name
#else
#  define default_name ""
#endif


/*
 * pthread_atfork handlers:
 */
static void _atfork_prep()   { slurm_mutex_lock(&log_lock);   }
static void _atfork_parent() { slurm_mutex_unlock(&log_lock); }
static void _atfork_child()  { slurm_mutex_unlock(&log_lock); }
static bool at_forked = false;
#define atfork_install_handlers()					\
	while (!at_forked) {						\
		pthread_atfork(_atfork_prep, _atfork_parent, _atfork_child); \
		at_forked = true;					\
	}

static void _log_flush(log_t *log);


/* Write the current local time into the provided buffer. Returns the
 * number of characters written into the buffer. */
static size_t _make_timestamp(char *timestamp_buf, size_t max,
			      const char *timestamp_fmt)
{
	time_t timestamp_t = time(NULL);
	struct tm timestamp_tm;
	if (!slurm_localtime_r(&timestamp_t, &timestamp_tm)) {
		fprintf(stderr, "localtime_r() failed\n");
		return 0;
	}
	return strftime(timestamp_buf, max, timestamp_fmt, &timestamp_tm);
}

size_t rfc2822_timestamp(char *s, size_t max)
{
	return _make_timestamp(s, max, "%a, %d %b %Y %H:%M:%S %z");
}

size_t log_timestamp(char *s, size_t max)
{
	if (!log)
		return _make_timestamp(s, max, "%Y-%m-%dT%T");
	switch (log->fmt) {
	case LOG_FMT_RFC5424_MS:
	case LOG_FMT_RFC5424:
	{
		size_t written = _make_timestamp(s, max, "%Y-%m-%dT%T%z");
		if (max >= 26 && written == 24) {
			/* The strftime %z format creates timezone offsets of
			 * the form (+/-)hhmm, whereas the RFC 5424 format is
			 * (+/-)hh:mm. So shift the minutes one step back and
			 * insert the semicolon. */
			s[25] = '\0';
			s[24] = s[23];
			s[23] = s[22];
			s[22] = ':';
			return written + 1;
		}
		return written;
	}
	case LOG_FMT_SHORT:
		return _make_timestamp(s, max, "%b %d %T");
		break;
	default:
		return _make_timestamp(s, max, "%Y-%m-%dT%T");
		break;
	}
}

/* check to see if a file is writeable,
 * RET 1 if file can be written now,
 *     0 if can not be written to within 5 seconds
 *     -1 if file has been closed POLLHUP
 */
static int _fd_writeable(int fd)
{
	struct pollfd ufds;
	struct stat stat_buf;
	int write_timeout = 5000;
	int rc;
	char temp[2];

	ufds.fd     = fd;
	ufds.events = POLLOUT;
	while ((rc = poll(&ufds, 1, write_timeout)) < 0) {
		switch (errno) {
		case EINTR:
		case EAGAIN:
			continue;
		default:
			return -1;
		}
	}
	if (rc == 0)
		return 0;

	/*
	 * Check here to make sure that if this is a socket that it really
	 * is still connected. If not then exit out and notify the sender.
	 * This is here since a write doesn't always tell you the socket is
	 * gone, but getting 0 back from a nonblocking read means just that.
	 */
	if ((ufds.revents & POLLHUP) || fstat(fd, &stat_buf) ||
	    ((S_ISSOCK(stat_buf.st_mode) &&
	     (rc = recv(fd, &temp, 1, MSG_DONTWAIT) <= 0) &&
	     ((rc == 0) || ((errno != EAGAIN) && (errno != EWOULDBLOCK))))))
		return -1;
	else if ((ufds.revents & POLLNVAL)
		 || (ufds.revents & POLLERR)
		 || !(ufds.revents & POLLOUT))
		return 0;
	/* revents == POLLOUT */
	return 1;
}

/*
 * Initialize log with
 * prog = program name to tag error messages with
 * opt  = log_options_t specifying max log levels for syslog, stderr, and file
 * fac  = log facility for syslog (unused if syslog level == LOG_QUIET)
 * logfile =
 *        logfile name if logfile level > LOG_QUIET
 */
static int
_log_init(char *prog, log_options_t opt, log_facility_t fac, char *logfile )
{
	int rc = 0;

	if (!log)  {
		log = (log_t *)xmalloc(sizeof(log_t));
		log->logfp = NULL;
		log->argv0 = NULL;
		log->buf   = NULL;
		log->fbuf  = NULL;
		log->fpfx  = NULL;
		atfork_install_handlers();
	}

	if (prog) {
		if (log->argv0)
			xfree(log->argv0);
		log->argv0 = xstrdup(xbasename(prog));
	} else if (!log->argv0) {
		const char *short_name = strrchr(default_name, '/');
		if (short_name)
			short_name++;
		else
			short_name = default_name;
		log->argv0 = xstrdup(short_name);
	}

	/* Only take the first one here.  In some situations it can change. */
	if (!slurm_prog_name && log->argv0 && (strlen(log->argv0) > 0))
		slurm_prog_name = xstrdup(log->argv0);

	if (!log->fpfx)
		log->fpfx = xstrdup("");

	log->opt = opt;

	if (log->buf) {
		cbuf_destroy(log->buf);
		log->buf = NULL;
	}
	if (log->fbuf) {
		cbuf_destroy(log->fbuf);
		log->fbuf = NULL;
	}

	if (log->opt.buffered) {
		log->buf  = cbuf_create(128, 8192);
		log->fbuf = cbuf_create(128, 8192);
	}

	if (log->opt.syslog_level > LOG_LEVEL_QUIET)
		log->facility = fac;

	if (logfile && (log->opt.logfile_level > LOG_LEVEL_QUIET)) {
		int mode = O_CREAT | O_WRONLY | O_APPEND | O_CLOEXEC;
		int fd;
		FILE *fp;

		fd = open(logfile, mode, S_IRUSR | S_IWUSR);
		if (fd >= 0)
			fp = fdopen(fd, "a");

		if ((fd < 0) || !fp) {
			char *errmsg = slurm_strerror(errno);
			fprintf(stderr,
				"%s: %s: Unable to open logfile `%s': %s\n",
				prog, __func__, logfile, errmsg);
			if (fd >= 0)
				close(fd);
			rc = errno;
			goto out;
		}

		if (log->logfp)
			fclose(log->logfp); /* Ignore errors */

		log->logfp = fp;
	}

	if (log->logfp && (fileno(log->logfp) < 0))
		log->logfp = NULL;

	log->initialized = 1;
 out:
	return rc;
}

/*
 * Initialize scheduler log with
 * prog = program name to tag error messages with
 * opt  = log_options_t specifying max log levels for syslog, stderr, and file
 * fac  = log facility for syslog (unused if syslog level == LOG_QUIET)
 * logfile = logfile name if logfile level > LOG_QUIET
 */
static int
_sched_log_init(char *prog, log_options_t opt, log_facility_t fac,
		char *logfile)
{
	int rc = 0;

	if (!sched_log) {
		sched_log = (log_t *)xmalloc(sizeof(log_t));
		atfork_install_handlers();
	}

	if (prog) {
		xfree(sched_log->argv0);
		sched_log->argv0 = xstrdup(xbasename(prog));
	} else if (!sched_log->argv0) {
		const char *short_name;
		short_name = strrchr((const char *) default_name, '/');
		if (short_name)
			short_name++;
		else
			short_name = default_name;
		sched_log->argv0 = xstrdup(short_name);
	}

	if (!sched_log->fpfx)
		sched_log->fpfx = xstrdup("");

	sched_log->opt = opt;

	if (sched_log->buf) {
		cbuf_destroy(sched_log->buf);
		sched_log->buf = NULL;
	}
	if (sched_log->fbuf) {
		cbuf_destroy(sched_log->fbuf);
		sched_log->fbuf = NULL;
	}

	if (sched_log->opt.buffered) {
		sched_log->buf  = cbuf_create(128, 8192);
		sched_log->fbuf = cbuf_create(128, 8192);
	}

	if (sched_log->opt.syslog_level > LOG_LEVEL_QUIET)
		sched_log->facility = fac;

	if (logfile) {
		int mode = O_CREAT | O_WRONLY | O_APPEND | O_CLOEXEC;
		int fd;
		FILE *fp;

		fd = open(logfile, mode, S_IRUSR | S_IWUSR);
		if (fd >= 0)
			fp = fdopen(fd, "a");

		if ((fd < 0) || !fp) {
			char *errmsg = slurm_strerror(errno);
			fprintf(stderr,
				"%s: %s: Unable to open logfile `%s': %s\n",
				prog, __func__, logfile, errmsg);
			if (fd >= 0)
				close(fd);
			rc = errno;
			goto out;
		}

		if (sched_log->logfp)
			fclose(sched_log->logfp); /* Ignore errors */

		sched_log->logfp = fp;
	}

	if (sched_log->logfp && (fileno(sched_log->logfp) < 0))
		sched_log->logfp = NULL;

	sched_log->initialized = 1;
 out:
	return rc;
}

/* initialize log mutex, then initialize log data structures
 */
int log_init(char *prog, log_options_t opt, log_facility_t fac, char *logfile)
{
	int rc = 0;

	slurm_mutex_lock(&log_lock);
	rc = _log_init(prog, opt, fac, logfile);
	slurm_mutex_unlock(&log_lock);
	return rc;
}

/* initialize log mutex, then initialize scheduler log data structures
 */
int sched_log_init(char *prog, log_options_t opt, log_facility_t fac, char *logfile)
{
	int rc = 0;

	slurm_mutex_lock(&log_lock);
	rc = _sched_log_init(prog, opt, fac, logfile);
	slurm_mutex_unlock(&log_lock);
	if (rc)
		fatal("sched_log_alter could not open %s: %m", logfile);
	return rc;
}

void log_fini(void)
{
	if (!log)
		return;

	slurm_mutex_lock(&log_lock);
	_log_flush(log);
	xfree(log->argv0);
	xfree(log->fpfx);
	if (log->buf)
		cbuf_destroy(log->buf);
	if (log->fbuf)
		cbuf_destroy(log->fbuf);
	if (log->logfp)
		fclose(log->logfp);
	xfree(log);
	xfree(slurm_prog_name);
	slurm_mutex_unlock(&log_lock);
}

void sched_log_fini(void)
{
	if (!sched_log)
		return;

	slurm_mutex_lock(&log_lock);
	_log_flush(sched_log);
	xfree(sched_log->argv0);
	xfree(sched_log->fpfx);
	if (sched_log->buf)
		cbuf_destroy(sched_log->buf);
	if (sched_log->fbuf)
		cbuf_destroy(sched_log->fbuf);
	if (sched_log->logfp)
		fclose(sched_log->logfp);
	xfree(sched_log);
	slurm_mutex_unlock(&log_lock);
}

void log_reinit(void)
{
	slurm_mutex_init(&log_lock);
}

void log_set_fpfx(char **prefix)
{
	slurm_mutex_lock(&log_lock);
	xfree(log->fpfx);
	if (!prefix || !*prefix)
		log->fpfx = xstrdup("");
	else {
		log->fpfx = *prefix;
		*prefix = NULL;
	}
	slurm_mutex_unlock(&log_lock);
}

void log_set_argv0(char *argv0)
{
	slurm_mutex_lock(&log_lock);
	if (log->argv0)
		xfree(log->argv0);
	if (!argv0)
		log->argv0 = xstrdup("");
	else
		log->argv0 = xstrdup(argv0);
	slurm_mutex_unlock(&log_lock);
}

/* reinitialize log data structures. Like log_init, but do not init
 * the log mutex
 */
int log_alter(log_options_t opt, log_facility_t fac, char *logfile)
{
	int rc = 0;
	slurm_mutex_lock(&log_lock);
	rc = _log_init(NULL, opt, fac, logfile);
	slurm_mutex_unlock(&log_lock);
	log_set_debug_flags();
	return rc;
}

/* log_set_debug_flags()
 * Set or reset the debug flags based on the configuration
 * file or the scontrol command.
 */
void log_set_debug_flags(void)
{
	uint64_t debug_flags = slurm_get_debug_flags();

	slurm_mutex_lock(&log_lock);
	log->debug_flags = debug_flags;
	slurm_mutex_unlock(&log_lock);
}

/* reinitialize log data structures. Like log_init, but do not init
 * the log mutex
 */
int log_alter_with_fp(log_options_t opt, log_facility_t fac, FILE *fp_in)
{
	int rc = 0;
	slurm_mutex_lock(&log_lock);
	rc = _log_init(NULL, opt, fac, NULL);
	if (log->logfp)
		fclose(log->logfp); /* Ignore errors */
	log->logfp = fp_in;
	if (log->logfp) {
		int fd;
		if ((fd = fileno(log->logfp)) < 0)
			log->logfp = NULL;
		/* don't close fd on out since this fd was made
		 * outside of the logger */
	}
	slurm_mutex_unlock(&log_lock);
	return rc;
}

/* reinitialize scheduler log data structures. Like sched_log_init,
 * but do not init the log mutex
 */
int sched_log_alter(log_options_t opt, log_facility_t fac, char *logfile)
{
	int rc = 0;
	slurm_mutex_lock(&log_lock);
	rc = _sched_log_init(NULL, opt, fac, logfile);
	slurm_mutex_unlock(&log_lock);
	if (rc)
		fatal("sched_log_alter could not open %s: %m", logfile);
	return rc;
}

/* Return the FILE * of the current logfile (or stderr if not logging to
 * a file, but NOT both). Also see log_fatal() and log_oom() below. */
FILE *log_fp(void)
{
	FILE *fp;
	slurm_mutex_lock(&log_lock);
	if (log && log->logfp) {
		fp = log->logfp;
	} else
		fp = stderr;
	slurm_mutex_unlock(&log_lock);
	return fp;
}

/* Log fatal error without message buffering */
void log_fatal(const char *file, int line, const char *msg, const char *err_str)
{
	if (log && log->logfp) {
		fprintf(log->logfp, "ERROR: [%s:%d] %s: %s\n",
			file, line, msg, err_str);
		fflush(log->logfp);
	}
	if (!log || log->opt.stderr_level) {
		fprintf(stderr, "ERROR: [%s:%d] %s: %s\n",
			file, line, msg, err_str);
		fflush(stderr);
	}
}

/* Log out of memory without message buffering */
void log_oom(const char *file, int line, const char *func)
{
	if (log && log->logfp) {
		fprintf(log->logfp, "%s:%d: %s: malloc failed\n",
		        file, line, func);
	}
	if (!log || log->opt.stderr_level) {
		fprintf(stderr, "%s:%d: %s: malloc failed\n",
		        file, line, func);
	}
}


/* Set the timestamp format flag */
void log_set_timefmt(unsigned fmtflag)
{
	if (log) {
		slurm_mutex_lock(&log_lock);
		log->fmt = fmtflag;
		slurm_mutex_unlock(&log_lock);
	} else {
		fprintf(stderr, "%s:%d: %s Slurm log not initialized\n",
			__FILE__, __LINE__, __func__);
	}
}

/* set_idbuf()
 * Write in the input buffer the current time and milliseconds
 * the process id and the current thread id.
 */
static void
set_idbuf(char *idbuf)
{
	struct timeval now;
	char thread_name[NAMELEN];
	int max_len = 12; /* handles current longest thread name */

	gettimeofday(&now, NULL);
#if HAVE_SYS_PRCTL_H
	if (prctl(PR_GET_NAME, thread_name, NULL, NULL, NULL) < 0) {
		error("failed to get thread name: %m");
		max_len = 0;
		thread_name[0] = '\0';
	}
#else
	/* skip printing thread name if not available */
	max_len = 0;
	thread_name[0] = '\0';
#endif

	sprintf(idbuf, "%.15s.%-6d %5d %-*s %p", slurm_ctime(&now.tv_sec) + 4,
		(int)now.tv_usec, (int)getpid(), max_len, thread_name,
		(void *)pthread_self());
}

/*
 * jobid2fmt() - print a job ID as "JobId=..." including, as applicable,
 * the job array or hetjob component information with the raw jobid in
 * parenthesis.
 */
static char *_jobid2fmt(struct job_record *job_ptr, char *buf, int buf_size)
{
	/*
	 * NOTE: You will notice we put a %.0s in front of the string.
	 * This is to handle the fact that we can't remove the job_ptr
	 * argument from the va_list directly. So when we call vsnprintf()
	 * to handle the va_list this will effectively skip this argument.
	 */
	if (job_ptr == NULL)
		return "%.0sJobId=Invalid";

	xassert(job_ptr->magic == JOB_MAGIC);
	if (job_ptr->magic != JOB_MAGIC)
		return "%.0sJobId=CORRUPT";

	if (job_ptr->pack_job_id) {
		snprintf(buf, buf_size, "%%.0sJobId=%u+%u(%u)",
			 job_ptr->pack_job_id, job_ptr->pack_job_offset,
			 job_ptr->job_id);
	} else if (job_ptr->array_recs && (job_ptr->array_task_id == NO_VAL)) {
		snprintf(buf, buf_size, "%%.0sJobId=%u_*",
			 job_ptr->array_job_id);
	} else if (job_ptr->array_task_id == NO_VAL) {
		snprintf(buf, buf_size, "%%.0sJobId=%u", job_ptr->job_id);
	} else {
		snprintf(buf, buf_size, "%%.0sJobId=%u_%u(%u)",
			 job_ptr->array_job_id, job_ptr->array_task_id,
			 job_ptr->job_id);
	}

	return buf;
}

/*
 * stepid2fmt() - print a step ID as " StepId=...", with Batch and Extern
 * used as appropriate.
 * Note that the "%.0s" trick is already included by jobid2fmt above, and
 * should not be repeated here.
 */
static char *_stepid2fmt(struct step_record *step_ptr, char *buf, int buf_size)
{
	if (step_ptr == NULL)
		return " StepId=Invalid";

	xassert(step_ptr->magic == STEP_MAGIC);
	if (step_ptr->magic != STEP_MAGIC)
		return " StepId=CORRUPT";

	if (step_ptr->step_id == SLURM_EXTERN_CONT) {
		return " StepId=Extern";
	} else if (step_ptr->step_id == SLURM_BATCH_SCRIPT) {
		return " StepId=Batch";
	} else if (step_ptr->step_id == SLURM_PENDING_STEP) {
		return " StepId=TBD";
	} else {
		snprintf(buf, buf_size, " StepId=%u", step_ptr->step_id);
	}

	return buf;
}

/*
 * return a heap allocated string formed from fmt and ap arglist
 * returned string is allocated with xmalloc, so must free with xfree.
 *
 * args are like printf, with the addition of the following format chars:
 * - %m expands to strerror(errno)
 * - %M expand to time stamp, format is configuration dependent
 * - %pJ expands to "JobId=XXXX" for the given job_ptr, with the appropriate
 *       format for job arrays and hetjob components.
 * - %pS expands to "JobId=XXXX StepId=YYYY" for a given step_ptr.
 * - %t expands to strftime("%x %X") [ locally preferred short date/time ]
 * - %T expands to rfc2822 date time  [ "dd, Mon yyyy hh:mm:ss GMT offset" ]
 *
 * these formats are expanded first, leaving all others to be passed to
 * vsnprintf() to complete the expansion using the ap arglist.
 */
static char *vxstrfmt(const char *fmt, va_list ap)
{
	char	*intermediate_fmt = NULL;
	char	*out_string = NULL;
	char	*p;
	bool found_other_formats = false;
	int     cnt = 0;

	while (*fmt != '\0') {
		bool is_our_format = false;

		p = (char *)strchr(fmt, '%');
		if (p == NULL) {
			/*
			 * no more format sequences, append the rest of
			 * fmt and exit the loop:
			 */
			xstrcat(intermediate_fmt, fmt);
			break;
		}

		/*
		 * make sure it's one of our format specifiers, skipping
		 * any that aren't:
		 */
		do {
			switch (*(p + 1)) {
			case 'm':
			case 't':
			case 'T':
			case 'M':
				is_our_format = true;
				break;
			case 'p':
				switch (*(p + 2)) {
				case 'J':
				case 'S':
					is_our_format = true;
					/*
					 * Need to set found_other_formats to
					 * still consume the %.0s if not other
					 * format strings are included.
					 */
					found_other_formats = true;
					break;
				default:
					found_other_formats = true;
					break;
				}
				break;
			default:
				found_other_formats = true;
				break;
			}
			cnt++;
		} while (!is_our_format &&
			 (p = (char *)strchr(p + 1, '%')));

		if (is_our_format) {
			char	*substitute = NULL;
			char	substitute_on_stack[256];
			int	should_xfree = 1;

			/*
			 * p points to the leading % of one of our formats;
			 * append anything from fmt up to p to the intermediate
			 * format string:
			 */
			xstrncat(intermediate_fmt, fmt, p - fmt);
			fmt = p + 1;

			/*
			 * fill the substitute buffer with whatever text we want
			 * to substitute for the format sequence in question:
			 */
			switch (*fmt) {
			case 'p':
				fmt++;
				switch (*fmt) {
				case 'J':	/* "%pJ" => "JobId=..." */
				{
					int i;
					void *ptr = NULL;
					struct job_record *job_ptr;
					va_list	ap_copy;

					va_copy(ap_copy, ap);
					for (i = 0; i < cnt; i++ )
						ptr = va_arg(ap_copy, void *);
					if (ptr) {
						job_ptr = ptr;
						xstrcat(intermediate_fmt,
							_jobid2fmt(
								job_ptr,
								substitute_on_stack,
								sizeof(substitute_on_stack)));
					}
					va_end(ap_copy);
					break;
				}
				/* "%pS" => "JobId=... StepId=..." */
				case 'S':
				{
					int i;
					void *ptr = NULL;
					struct step_record *step_ptr = NULL;
					struct job_record *job_ptr = NULL;
					va_list	ap_copy;

					va_copy(ap_copy, ap);
					for (i = 0; i < cnt; i++ )
						ptr = va_arg(ap_copy, void *);
					if (ptr) {
						step_ptr = ptr;
						if (step_ptr &&
						    (step_ptr->magic == STEP_MAGIC))
							job_ptr = step_ptr->job_ptr;
						xstrcat(intermediate_fmt,
							_jobid2fmt(
								job_ptr,
								substitute_on_stack,
								sizeof(substitute_on_stack)));
						xstrcat(intermediate_fmt,
							_stepid2fmt(
								step_ptr,
								substitute_on_stack,
								sizeof(substitute_on_stack)));
					}
					va_end(ap_copy);
					break;
				}
				default:
					/* Unknown */
					break;
				}
				break;
			case 'm':	/* "%m" => strerror(errno) */
				substitute = slurm_strerror(errno);
				should_xfree = 0;
				break;
			case 't': 	/* "%t" => locally preferred date/time*/
				xstrftimecat(substitute,
					     "%x %X");
				break;
			case 'T': 	/* "%T" => "dd, Mon yyyy hh:mm:ss off" */
				xstrftimecat(substitute,
					     "%a, %d %b %Y %H:%M:%S %z");
				break;
			case 'M':
				if (!log) {
					xiso8601timecat(substitute, true);
					break;
				}
				switch (log->fmt) {
				case LOG_FMT_ISO8601_MS:
					/* "%M" => "yyyy-mm-ddThh:mm:ss.fff"  */
					xiso8601timecat(substitute, true);
					break;
				case LOG_FMT_ISO8601:
					/* "%M" => "yyyy-mm-ddThh:mm:ss.fff"  */
					xiso8601timecat(substitute, false);
					break;
				case LOG_FMT_RFC5424_MS:
					/* "%M" => "yyyy-mm-ddThh:mm:ss.fff(+/-)hh:mm" */
					xrfc5424timecat(substitute, true);
					break;
				case LOG_FMT_RFC5424:
					/* "%M" => "yyyy-mm-ddThh:mm:ss.fff(+/-)hh:mm" */
					xrfc5424timecat(substitute, false);
					break;
				case LOG_FMT_CLOCK:
					/* "%M" => "usec" */
#if defined(__FreeBSD__)
					snprintf(substitute_on_stack,
						 sizeof(substitute_on_stack),
						 "%d", clock());
#else
					snprintf(substitute_on_stack,
						 sizeof(substitute_on_stack),
						 "%ld", clock());
#endif
					substitute = substitute_on_stack;
					should_xfree = 0;
					break;
				case LOG_FMT_SHORT:
					/* "%M" => "Mon DD hh:mm:ss" */
					xstrftimecat(substitute, "%b %d %T");
					break;
				case LOG_FMT_THREAD_ID:
					set_idbuf(substitute_on_stack);
					substitute = substitute_on_stack;
					should_xfree = 0;
					break;
				}
				break;
			}
			fmt++;

			if (substitute) {
				char *s = substitute;

				while (*s && (p = (char *)strchr(s, '%'))) {
					/* append up through the '%' */
					xstrncat(intermediate_fmt, s, p - s);
					xstrcat(intermediate_fmt, "%%");
					s = p + 1;
				}
				if (*s) {
					/* append whatever's left of the substitution: */
					xstrcat(intermediate_fmt, s);
				}

				/* deallocate substitute if necessary: */
				if (should_xfree) {
					xfree(substitute);
				}
			}
		} else {
			/*
			 * no more format sequences for us, append the rest of
			 * fmt and exit the loop:
			 */
			xstrcat(intermediate_fmt, fmt);
			break;
		}
	}

	if (intermediate_fmt && found_other_formats) {
		char	tmp[LINEBUFSIZE];
		int	actual_len;
		va_list	ap_copy;

		va_copy(ap_copy, ap);
		actual_len = vsnprintf(tmp, sizeof(tmp),
				       intermediate_fmt, ap_copy);
		va_end(ap_copy);

		if (actual_len >= 0) {
			if (actual_len < sizeof(tmp)) {
				out_string = xstrdup(tmp);
			} else {
				/*
				 * our C library's vsnprintf() was nice enough
				 * to return the necessary size of the buffer!
				 */
				out_string = xmalloc(actual_len + 1);
				if (out_string) {
					va_copy(ap_copy, ap);
					vsnprintf(out_string, actual_len + 1,
						  intermediate_fmt, ap_copy);
					va_end(ap_copy);
				}
			}
		} else {
			size_t	growable_tmp_size = LINEBUFSIZE;
			char	*growable_tmp = NULL;

			/*
			 * our C library's vsnprintf() doesn't return the
			 * necessary buffer size on overflow, it considers that
			 * an error condition.  So we need to iteratively grow
			 * a buffer until it accommodates the vsnprintf() call
			 */
			do {
				growable_tmp_size += LINEBUFSIZE;
				growable_tmp = xrealloc(growable_tmp,
							growable_tmp_size);
				if (!growable_tmp)
					break;
				va_copy(ap_copy, ap);
				actual_len = vsnprintf(growable_tmp,
						       growable_tmp_size,
						       intermediate_fmt,
						       ap_copy);
				va_end(ap_copy);
			} while (actual_len < 0);
			out_string = growable_tmp;
		}
		xfree(intermediate_fmt);
	} else if (intermediate_fmt) {
		/*
		 * no additional format sequences, so we can just return the
		 * intermediate_fmt string
		 */
		out_string = intermediate_fmt;
	}

	return out_string;
}

/*
 * concatenate result of xstrfmt() to dst, expanding dst if necessary
 */
static void xlogfmtcat(char **dst, const char *fmt, ...)
{
	va_list ap;
	char *buf = NULL;

	va_start(ap, fmt);
	buf = vxstrfmt(fmt, ap);
	va_end(ap);

	xstrcat(*dst, buf);

	xfree(buf);

}

static void
_log_printf(log_t *log, cbuf_t cb, FILE *stream, const char *fmt, ...)
{
	va_list ap;
	int fd = -1;

	/* If the fd is less than 0 just return since we can't do anything here.
	 * This can happen if a calling program is the one that set up the io.
	 */
	if (stream)
		fd = fileno(stream);
	if (fd < 0)
		return;

	/* If the socket has gone away we just return like all is
	   well. */
	if (_fd_writeable(fd) != 1)
		return;

	va_start(ap, fmt);
	if (log->opt.buffered && (cb != NULL)) {
		char *buf = vxstrfmt(fmt, ap);
		int   len = strlen(buf);
		int   dropped;

		cbuf_write(cb, buf, len, &dropped);
		cbuf_read_to_fd(cb, fd, -1);
		xfree(buf);
	} else  {
		vfprintf(stream, fmt, ap);
	}
	va_end(ap);

}

/*
 * log a message at the specified level to facilities that have been
 * configured to receive messages at that level
 */
static void _log_msg(log_level_t level, bool sched, const char *fmt, va_list args)
{
	char *pfx = "";
	char *buf = NULL;
	char *msgbuf = NULL;
	int priority = LOG_INFO;

	slurm_mutex_lock(&log_lock);

	if (!LOG_INITIALIZED) {
		log_options_t opts = LOG_OPTS_STDERR_ONLY;
		_log_init(NULL, opts, 0, NULL);
	}

	if (SCHED_LOG_INITIALIZED && sched &&
	    (sched_log->opt.logfile_level > LOG_LEVEL_QUIET)) {
		buf = vxstrfmt(fmt, args);
		xlogfmtcat(&msgbuf, "[%M] %s%s%s", sched_log->fpfx, pfx, buf);
		_log_printf(sched_log, sched_log->fbuf, sched_log->logfp,
			    "sched: %s\n", msgbuf);
		fflush(sched_log->logfp);
		xfree(msgbuf);
	}

	if ((level > log->opt.syslog_level)  &&
	    (level > log->opt.logfile_level) &&
	    (level > log->opt.stderr_level)) {
		slurm_mutex_unlock(&log_lock);
		xfree(buf);
		return;
	}

	if (log->opt.prefix_level || (log->opt.syslog_level > level)) {
		switch (level) {
		case LOG_LEVEL_FATAL:
			priority = LOG_CRIT;
			pfx = "fatal: ";
			break;

		case LOG_LEVEL_ERROR:
			priority = LOG_ERR;
			pfx = sched? "error: sched: " : "error: ";
			break;

		case LOG_LEVEL_INFO:
		case LOG_LEVEL_VERBOSE:
			priority = LOG_INFO;
			pfx = sched ? "sched: " : "";
			break;

		case LOG_LEVEL_DEBUG:
			priority = LOG_DEBUG;
			pfx = sched ? "debug:  sched: " : "debug:  ";
			break;

		case LOG_LEVEL_DEBUG2:
			priority = LOG_DEBUG;
			pfx = sched ? "debug: sched: " : "debug2: ";
			break;

		case LOG_LEVEL_DEBUG3:
			priority = LOG_DEBUG;
			pfx = sched ? "debug3: sched: " : "debug3: ";
			break;

		case LOG_LEVEL_DEBUG4:
			priority = LOG_DEBUG;
			pfx = "debug4: ";
			break;

		case LOG_LEVEL_DEBUG5:
			priority = LOG_DEBUG;
			pfx = "debug5: ";
			break;

		default:
			priority = LOG_ERR;
			pfx = "internal error: ";
			break;
		}

	}

	if (!buf) {
		/* format the basic message,
		 * if not already done for scheduling log */
		buf = vxstrfmt(fmt, args);
	}

	if (level <= log->opt.stderr_level) {

		fflush(stdout);
		if (log->fmt == LOG_FMT_THREAD_ID) {
			char tmp[64];
			set_idbuf(tmp);
			_log_printf(log, log->buf, stderr, "%s: %s%s\n",
			            tmp, pfx, buf);
		} else {
			_log_printf(log, log->buf, stderr, "%s: %s%s\n",
			            log->argv0, pfx, buf);
		}
		fflush(stderr);
	}

	if ((level <= log->opt.logfile_level) && (log->logfp != NULL)) {

		xlogfmtcat(&msgbuf, "[%M] %s%s%s", log->fpfx, pfx, buf);
		_log_printf(log, log->fbuf, log->logfp, "%s\n", msgbuf);
		fflush(log->logfp);

		xfree(msgbuf);
	}

	if (level <=  log->opt.syslog_level) {

		/* Avoid changing errno if syslog fails */
		int orig_errno = slurm_get_errno();
		xlogfmtcat(&msgbuf, "%s%s", pfx, buf);
		openlog(log->argv0, LOG_PID, log->facility);
		syslog(priority, "%.500s", msgbuf);
		closelog();
		slurm_seterrno(orig_errno);

		xfree(msgbuf);
	}

	slurm_mutex_unlock(&log_lock);

	xfree(buf);
}

bool
log_has_data()
{
	bool rc = false;
	slurm_mutex_lock(&log_lock);
	if (log->opt.buffered)
		rc = (cbuf_used(log->buf) > 0);
	slurm_mutex_unlock(&log_lock);
	return rc;
}

static void
_log_flush(log_t *log)
{
	if (!log->opt.buffered)
		return;

	if (log->opt.stderr_level)
		cbuf_read_to_fd(log->buf, fileno(stderr), -1);
	else if (log->logfp && (fileno(log->logfp) > 0))
		cbuf_read_to_fd(log->fbuf, fileno(log->logfp), -1);
}

void
log_flush()
{
	slurm_mutex_lock(&log_lock);
	_log_flush(log);
	slurm_mutex_unlock(&log_lock);
}

/*
 * attempt to log message and exit()
 */
void fatal(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	_log_msg(LOG_LEVEL_FATAL, false, fmt, ap);
	va_end(ap);
	log_flush();

	exit(1);
}

/*
 * attempt to log message and exit()
 */
void fatal_abort(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	_log_msg(LOG_LEVEL_FATAL, false, fmt, ap);
	va_end(ap);
	log_flush();

	abort();
}

int error(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	_log_msg(LOG_LEVEL_ERROR, false, fmt, ap);
	va_end(ap);

	/*
	 *  Return SLURM_ERROR so calling functions can
	 *    do "return error (...);"
	 */
	return SLURM_ERROR;
}

void info(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	_log_msg(LOG_LEVEL_INFO, false, fmt, ap);
	va_end(ap);
}

void verbose(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	_log_msg(LOG_LEVEL_VERBOSE, false, fmt, ap);
	va_end(ap);
}

void debug(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	_log_msg(LOG_LEVEL_DEBUG, false, fmt, ap);
	va_end(ap);
}

void debug2(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	_log_msg(LOG_LEVEL_DEBUG2, false, fmt, ap);
	va_end(ap);
}

void debug3(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	_log_msg(LOG_LEVEL_DEBUG3, false, fmt, ap);
	va_end(ap);
}

/*
 * Debug levels higher than debug3 are not written to stderr in the
 * slurmstepd process after stderr is connected back to the client (srun).
 */
void debug4(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	_log_msg(LOG_LEVEL_DEBUG4, false, fmt, ap);
	va_end(ap);
}

void debug5(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	_log_msg(LOG_LEVEL_DEBUG5, false, fmt, ap);
	va_end(ap);
}

int sched_error(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	_log_msg(LOG_LEVEL_ERROR, true, fmt, ap);
	va_end(ap);

	/*
	 *  Return SLURM_ERROR so calling functions can
	 *    do "return error (...);"
	 */
	return SLURM_ERROR;
}

void sched_info(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	_log_msg(LOG_LEVEL_INFO, true, fmt, ap);
	va_end(ap);
}

void sched_verbose(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	_log_msg(LOG_LEVEL_VERBOSE, true, fmt, ap);
	va_end(ap);
}

void sched_debug(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	_log_msg(LOG_LEVEL_DEBUG, true, fmt, ap);
	va_end(ap);
}

void sched_debug2(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	_log_msg(LOG_LEVEL_DEBUG2, true, fmt, ap);
	va_end(ap);
}

void sched_debug3(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	_log_msg(LOG_LEVEL_DEBUG3, true, fmt, ap);
	va_end(ap);
}

/* Return the highest LOG_LEVEL_* used for any logging mechanism.
 * For example, if LOG_LEVEL_INFO is returned, we know that all verbose and
 * debug type messages will be ignored. */
extern int get_log_level(void)
{
	int level;

	level = MAX(log->opt.syslog_level, log->opt.logfile_level);
	level = MAX(level, log->opt.stderr_level);
	return level;
}
