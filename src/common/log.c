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

#include <arpa/inet.h>
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
#include <sys/un.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"
#include "src/common/data.h"
#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_time.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/serializer.h"

#include "src/slurmctld/slurmctld.h"


#ifndef LINEBUFSIZE
#  define LINEBUFSIZE 256
#endif

#define NAMELEN 16

#define LOG_MACRO(level, sched, fmt) {				\
	if ((level <= highest_log_level) ||			\
	    (sched && (level <= highest_sched_log_level))) {	\
		va_list ap;					\
		va_start(ap, fmt);				\
		_log_msg(level, sched, false, false, fmt, ap);	\
		va_end(ap);					\
	}							\
}

/*
** Define slurm-specific aliases for use by plugins, see slurm_xlator.h
** for details.
 */
strong_alias(get_log_level,	slurm_get_log_level);
strong_alias(get_sched_log_level, slurm_get_sched_log_level);
strong_alias(log_init,		slurm_log_init);
strong_alias(log_reinit,	slurm_log_reinit);
strong_alias(log_fini,		slurm_log_fini);
strong_alias(log_alter,		slurm_log_alter);
strong_alias(log_alter_with_fp, slurm_log_alter_with_fp);
strong_alias(log_set_prefix,	slurm_log_set_prefix);
strong_alias(log_fp,		slurm_log_fp);
strong_alias(log_oom,		slurm_log_oom);
strong_alias(log_has_data,	slurm_log_has_data);
strong_alias(log_flush,		slurm_log_flush);
strong_alias(log_var,		slurm_log_var);
strong_alias(fatal,		slurm_fatal) __attribute__((noreturn));
strong_alias(fatal_abort,	slurm_fatal_abort) __attribute__((noreturn));
strong_alias(error,		slurm_error);
strong_alias(spank_log,		slurm_spank_log);
strong_alias(sched_error,	slurm_sched_error);
strong_alias(sched_info,	slurm_sched_info);
strong_alias(sched_verbose,	slurm_sched_verbose);

/*
** struct defining a "log" type
*/
typedef struct {
	char *argv0;
	char *prefix;            /* optional prefix with log_set_prefix */
	FILE *logfp;             /* log file pointer                    */
	cbuf_t *buf;              /* stderr data buffer                  */
	cbuf_t *fbuf;             /* logfile data buffer                 */
	log_facility_t facility;
	log_options_t opt;
	bool initialized;
	uint16_t fmt;            /* Flag for specifying timestamp format */
}	log_t;

/* static variables */
static pthread_mutex_t  log_lock = PTHREAD_MUTEX_INITIALIZER;
static log_t            *log = NULL;
static log_t            *sched_log = NULL;
static bool syslog_open = false;
static volatile log_level_t highest_log_level = LOG_LEVEL_END;
static volatile log_level_t highest_sched_log_level = LOG_LEVEL_QUIET;

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

static log_level_t _highest_level(log_level_t a, log_level_t b, log_level_t c)
{
	if (a >= b) {
		return (a >= c) ? a : c;
	} else {
		return (b >= c) ? b : c;
	}
}

/* Write the current local time into the provided buffer. Returns the
 * number of characters written into the buffer. */
static size_t _make_timestamp(char *timestamp_buf, size_t max,
			      const char *timestamp_fmt)
{
	time_t timestamp_t = time(NULL);
	struct tm timestamp_tm;
	if (!localtime_r(&timestamp_t, &timestamp_tm)) {
		fprintf(stderr, "localtime_r() failed\n");
		return 0;
	}
	return strftime(timestamp_buf, max, timestamp_fmt, &timestamp_tm);
}

size_t rfc2822_timestamp(char *s, size_t max)
{
	return _make_timestamp(s, max, "%a, %d %b %Y %H:%M:%S %z");
}

static size_t _fix_tz(char *s, size_t max, size_t written)
{
	xassert(written == 24);
	xassert(max >= 26);

	if ((max < 26) || (written != 24))
		return written;

	/*
	 * The strftime %z format creates timezone offsets of
	 * the form (+/-)hhmm, whereas the RFC 5424 format is
	 * (+/-)hh:mm. So shift the minutes one step back and
	 * insert the semicolon.
	 */
	s[25] = '\0';
	s[24] = s[23];
	s[23] = s[22];
	s[22] = ':';

	return 25;
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
		return _fix_tz(s, max, written);
	}
	case LOG_FMT_RFC3339:
	{
		size_t written = _make_timestamp(s, max, "%FT%T%z");
		return _fix_tz(s, max, written);
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
	int write_timeout = 5000;
	int rc;

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

	if (ufds.revents & POLLHUP)
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
		log = xmalloc(sizeof(log_t));
		log->logfp = NULL;
		log->argv0 = NULL;
		log->buf   = NULL;
		log->fbuf  = NULL;
		log->prefix = NULL;
		atfork_install_handlers();
	}

	if (syslog_open) {
		closelog();
		syslog_open = false;
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

	if (!log->prefix)
		log->prefix = xstrdup("");

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

	if (log->opt.syslog_level > LOG_LEVEL_QUIET) {
		log->facility = fac;
		openlog(log->argv0, LOG_PID, log->facility);
		syslog_open = true;
	}

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

	highest_log_level = _highest_level(log->opt.syslog_level,
					   log->opt.logfile_level,
					   log->opt.stderr_level);

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
		sched_log = xmalloc(sizeof(log_t));
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

	if (!sched_log->prefix)
		sched_log->prefix = xstrdup("");

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

	highest_sched_log_level = _highest_level(sched_log->opt.syslog_level,
						 sched_log->opt.logfile_level,
						 sched_log->opt.stderr_level);

	/*
	 * The sched_log_level is (ab)used as a boolean. Force it to the end
	 * if set so that the LOG_MACRO checks at least stay relatively clean,
	 * and it's easier for us to introduce the idea of this log level
	 * varying in the future.
	 */
	if (highest_sched_log_level > LOG_LEVEL_QUIET)
		highest_sched_log_level = LOG_LEVEL_END;

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
	if (syslog_open) {
		closelog();
		syslog_open = false;
	}
	xfree(log->argv0);
	xfree(log->prefix);
	if (log->buf)
		cbuf_destroy(log->buf);
	if (log->fbuf)
		cbuf_destroy(log->fbuf);
	if (log->logfp)
		fclose(log->logfp);
	xfree(log);
	slurm_mutex_unlock(&log_lock);
}

void sched_log_fini(void)
{
	if (!sched_log)
		return;

	slurm_mutex_lock(&log_lock);
	_log_flush(sched_log);
	xfree(sched_log->argv0);
	xfree(sched_log->prefix);
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

void log_set_prefix(char **prefix)
{
	slurm_mutex_lock(&log_lock);
	xfree(log->prefix);
	if (!prefix || !*prefix)
		log->prefix = xstrdup("");
	else {
		log->prefix = *prefix;
		*prefix = NULL;
	}
	slurm_mutex_unlock(&log_lock);
}

void log_set_argv0(char *argv0)
{
	slurm_mutex_lock(&log_lock);
	if (syslog_open) {
		closelog();
		syslog_open = false;
	}
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
	return rc;
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
 * a file, but NOT both). Also see log_oom() below. */
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

/*
 * _set_idbuf()
 * Write in the input buffer the current time and milliseconds
 * the process id and the current thread id.
 */
static void _set_idbuf(char *idbuf, size_t size)
{
	struct timeval now;
	char time[25];
	char thread_name[NAMELEN];
	int max_len = 12; /* handles current longest thread name */

	gettimeofday(&now, NULL);
#if HAVE_SYS_PRCTL_H
	if (prctl(PR_GET_NAME, thread_name, NULL, NULL, NULL) < 0) {
		fprintf(stderr, "failed to get thread name: %m\n");
		max_len = 0;
		thread_name[0] = '\0';
	}
#else
	/* skip printing thread name if not available */
	max_len = 0;
	thread_name[0] = '\0';
#endif
	slurm_ctime2_r(&now.tv_sec, time);

	snprintf(idbuf, size, "%.15s.%-6d %5d %-*s %p",
		 time + 4, (int) now.tv_usec, (int) getpid(), max_len,
		 thread_name, (void *) pthread_self());
}

/*
 * _addr2fmt() - print an IP address from slurm_addr_t
 */
static char *_addr2fmt(slurm_addr_t *addr_ptr, char *buf, int buf_size)
{
	/*
	 * NOTE: You will notice we put a %.0s in front of the string.
	 * This is to handle the fact that we can't remove the addr_ptr
	 * argument from the va_list directly. So when we call vsnprintf()
	 * to handle the va_list this will effectively skip this argument.
	 */
	if (addr_ptr == NULL)
		return "%.0sNULL";

	if (addr_ptr->ss_family == AF_INET6) {
		char addrbuf[INET6_ADDRSTRLEN];
		uint16_t port = 0;
		struct sockaddr_in6 *in6 = (struct sockaddr_in6 *) addr_ptr;
		inet_ntop(AF_INET6, &in6->sin6_addr, addrbuf, INET6_ADDRSTRLEN);
		port = ntohs(in6->sin6_port);
		snprintf(buf, buf_size, "[%%.0s%s]:%d", addrbuf, port);
	} else if (addr_ptr->ss_family == AF_INET) {
		char addrbuf[INET_ADDRSTRLEN];
		struct sockaddr_in *in = (struct sockaddr_in *) addr_ptr;
		uint16_t port = 0;
		inet_ntop(AF_INET, &in->sin_addr, addrbuf, INET_ADDRSTRLEN);
		port = ntohs(in->sin_port);
		snprintf(buf, buf_size, "%%.0s%s:%d", addrbuf, port);
	} else if (addr_ptr->ss_family == AF_UNIX) {
		struct sockaddr_un *un = (struct sockaddr_un *) addr_ptr;
		snprintf(buf, buf_size, "%%.0sunix:%s", un->sun_path);
	} else if (addr_ptr->ss_family == AF_UNSPEC) {
		/*
		 * AF_UNSPEC is place holder for unspecified which may be
		 * nothing or used as a wild card depending on the API call.
		 */
		snprintf(buf, buf_size, "%%.0sAF_UNSPEC");
	} else {
		snprintf(buf, buf_size, "%%.0sINVALID");
	}

	return buf;
}
/*
 * jobid2fmt() - print a job ID as "JobId=..." including, as applicable,
 * the job array or hetjob component information with the raw jobid in
 * parenthesis.
 */
static char *_jobid2fmt(job_record_t *job_ptr, char *buf, int buf_size)
{
	/*
	 * NOTE: You will notice we put a %.0s in front of the string.
	 * This is to handle the fact that we can't remove the job_ptr
	 * argument from the va_list directly. So when we call vsnprintf()
	 * to handle the va_list this will effectively skip this argument.
	 */
	if (job_ptr == NULL)
		return "%.0sJobId=Invalid";

	if (job_ptr->magic != JOB_MAGIC)
		return "%.0sJobId=CORRUPT";

	if (job_ptr->het_job_id) {
		snprintf(buf, buf_size, "%%.0sJobId=%u+%u(%u)",
			 job_ptr->het_job_id, job_ptr->het_job_offset,
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
static char *_stepid2fmt(step_record_t *step_ptr, char *buf, int buf_size)
{
	if (step_ptr == NULL)
		return " StepId=Invalid";

	if (step_ptr->magic != STEP_MAGIC)
		return " StepId=CORRUPT";

	return log_build_step_id_str(&step_ptr->step_id, buf, buf_size,
				     STEP_ID_FLAG_SPACE | STEP_ID_FLAG_NO_JOB);
}

static char *_print_data_t(const data_t *d, char *buffer, int size)
{
	/*
	 * NOTE: You will notice we put a %.0s in front of the string.
	 * This is to handle the fact that we can't remove the data_t
	 * argument from the va_list directly. So when we call vsnprintf()
	 * to handle the va_list this will effectively skip this argument.
	 */
	snprintf(buffer, size, "%%.0s%s(0x%"PRIxPTR")",
		 data_get_type_string(d), ((uintptr_t) d));
	return buffer;
}

static char *_print_data_json(const data_t *d, char *buffer, int size)
{
	char *nbuf = NULL;

	/*
	 * NOTE: You will notice we put a %.0s in front of the string.
	 * This is to handle the fact that we can't remove the data_t
	 * argument from the va_list directly. So when we call vsnprintf()
	 * to handle the va_list this will effectively skip this argument.
	 */
	if (serialize_g_data_to_string(&nbuf, NULL, d, MIME_TYPE_JSON,
				       SER_FLAGS_COMPACT))
		snprintf(buffer, size, "%%.0s(JSON serialization failed)");
	else
		snprintf(buffer, size, "%%.0s%s", nbuf);

	xfree(nbuf);

	return buffer;
}

extern char *vxstrfmt(const char *fmt, va_list ap)
{
	char *intermediate_fmt = NULL, *intermediate_pos = NULL;
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
			xstrcatat(intermediate_fmt, &intermediate_pos, fmt);
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
				case 'A':
				case 'd':
				case 'D':
				case 'J':
				case 's':
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
			xstrncatat(intermediate_fmt, &intermediate_pos,
				   fmt, p - fmt);
			fmt = p + 1;

			/*
			 * fill the substitute buffer with whatever text we want
			 * to substitute for the format sequence in question:
			 */
			switch (*fmt) {
			case 'p':
				fmt++;
				switch (*fmt) {
				case 'A':	/* "%pA" -> "AAA.BBB.CCC.DDD:XXXX" */
				{
					void *ptr = NULL;
					slurm_addr_t *addr_ptr;
					va_list	ap_copy;

					va_copy(ap_copy, ap);
					for (int i = 0; i < cnt; i++ )
						ptr = va_arg(ap_copy, void *);
					addr_ptr = ptr;
					xstrcatat(
						intermediate_fmt,
						&intermediate_pos,
						_addr2fmt(
							addr_ptr,
							substitute_on_stack,
							sizeof(substitute_on_stack)));
					va_end(ap_copy);
					break;
				}
				case 'd':	/* "%pd" -> compact JSON serialized string */
				{
					data_t *d = NULL;
					va_list	ap_copy;

					va_copy(ap_copy, ap);
					for (int i = 0; i < cnt; i++ )
						d = va_arg(ap_copy, void *);
					xstrcatat(
						intermediate_fmt,
						&intermediate_pos,
						_print_data_json(
							d,
							substitute_on_stack,
							sizeof(substitute_on_stack)));
					va_end(ap_copy);
					break;
				}
				case 'D':	/* "%pD" -> data_type(0xDEADBEEF) */
				{
					data_t *d = NULL;
					va_list	ap_copy;

					va_copy(ap_copy, ap);
					for (int i = 0; i < cnt; i++ )
						d = va_arg(ap_copy, void *);
					xstrcatat(
						intermediate_fmt,
						&intermediate_pos,
						_print_data_t(
							d,
							substitute_on_stack,
							sizeof(substitute_on_stack)));
					va_end(ap_copy);
					break;
				}
				case 'J':	/* "%pJ" => "JobId=..." */
				{
					int i;
					void *ptr = NULL;
					job_record_t *job_ptr;
					va_list	ap_copy;

					va_copy(ap_copy, ap);
					for (i = 0; i < cnt; i++ )
						ptr = va_arg(ap_copy, void *);
					job_ptr = ptr;
					xstrcatat(
						intermediate_fmt,
						&intermediate_pos,
						_jobid2fmt(
							job_ptr,
							substitute_on_stack,
							sizeof(substitute_on_stack)));
					va_end(ap_copy);
					break;
				}
				/*
				 * "%ps" => "StepId=... " on a
				 * slurm_step_id_t
				 */
				case 's':
				{
					int i;
					void *ptr = NULL;
					slurm_step_id_t *step_id = NULL;
					va_list	ap_copy;

					va_copy(ap_copy, ap);
					for (i = 0; i < cnt; i++ )
						ptr = va_arg(ap_copy, void *);
					step_id = ptr;
					xstrcatat(
						intermediate_fmt,
						&intermediate_pos,
						log_build_step_id_str(
							step_id,
							substitute_on_stack,
							sizeof(substitute_on_stack),
							STEP_ID_FLAG_PS));
					va_end(ap_copy);
					break;
				}
				/*
				 * "%pS" => "JobId=... StepId=..." on a
				 * step_record_t
				 */
				case 'S':
				{
					int i;
					void *ptr = NULL;
					step_record_t *step_ptr = NULL;
					job_record_t *job_ptr = NULL;
					va_list	ap_copy;

					va_copy(ap_copy, ap);
					for (i = 0; i < cnt; i++ )
						ptr = va_arg(ap_copy, void *);
					step_ptr = ptr;
					if (step_ptr &&
					    (step_ptr->magic == STEP_MAGIC))
						job_ptr = step_ptr->job_ptr;
					xstrcatat(
						intermediate_fmt,
						&intermediate_pos,
						_jobid2fmt(
							job_ptr,
							substitute_on_stack,
							sizeof(substitute_on_stack)));
					xstrcatat(
						intermediate_fmt,
						&intermediate_pos,
						_stepid2fmt(
							step_ptr,
							substitute_on_stack,
							sizeof(substitute_on_stack)));
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
				case LOG_FMT_RFC3339:
					/* "%M" => "yyyy-mm-ddThh:mm:ssZ" */
					xrfc3339timecat(substitute);
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
					_set_idbuf(substitute_on_stack,
						   sizeof(substitute_on_stack));
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
					xstrncatat(intermediate_fmt,
						   &intermediate_pos, s, p - s);
					xstrcatat(intermediate_fmt,
						  &intermediate_pos, "%%");
					s = p + 1;
				}
				if (*s) {
					/* append whatever's left of the substitution: */
					xstrcatat(intermediate_fmt,
						  &intermediate_pos, s);
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
			xstrcatat(intermediate_fmt, &intermediate_pos, fmt);
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

static void _log_printf(log_t *log, cbuf_t *cb, FILE *stream,
			const char *fmt, ...)
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
static void _log_msg(log_level_t level, bool sched, bool spank, bool warn,
		     const char *fmt, va_list args)
{
	char *pfx = "";
	char *buf = NULL;
	char *msgbuf = NULL;
	char *eol = "\n";
	int priority = LOG_INFO;

	/*
	 * Construct the message outside the lock as this can be slow.
	 * The format_print() macro should ensure that we're always going
	 * to print the resulting message through one or more channels.
	 */
	buf = vxstrfmt(fmt, args);

	slurm_mutex_lock(&log_lock);

	if (!LOG_INITIALIZED) {
		log_options_t opts = LOG_OPTS_STDERR_ONLY;
		_log_init(NULL, opts, 0, NULL);
	}

	if (log->opt.raw)
		eol = "\r\n";

	if (SCHED_LOG_INITIALIZED && sched &&
	    (highest_sched_log_level > LOG_LEVEL_QUIET)) {
		xlogfmtcat(&msgbuf, "[%M] %s%s", sched_log->prefix, pfx);
		_log_printf(sched_log, sched_log->fbuf, sched_log->logfp,
			    "sched: %s%s\n", msgbuf, buf);
		fflush(sched_log->logfp);
		xfree(msgbuf);
	}

	if (level > highest_log_level) {
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
			pfx = spank ? "" : pfx;
			break;

		case LOG_LEVEL_INFO:
		case LOG_LEVEL_VERBOSE:
			priority = warn ? LOG_WARNING : LOG_INFO;
			pfx = sched ? "sched: " : "";
			pfx = warn ? "warning: " : pfx;
			break;

		case LOG_LEVEL_DEBUG:
			priority = LOG_DEBUG;
			pfx = sched ? "debug:  sched: " : "debug:  ";
			break;

		case LOG_LEVEL_DEBUG2:
			priority = LOG_DEBUG;
			pfx = sched ? "debug2: sched: " : "debug2: ";
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

	if (level <= log->opt.stderr_level) {

		fflush(stdout);
		if (spank) {
			_log_printf(log, log->buf, stderr, "%s%s", buf, eol);
		} else if (running_in_daemon()) {
			xlogfmtcat(&msgbuf, "[%M]");
			_log_printf(log, log->buf, stderr, "%s %s%s%s", msgbuf,
				    pfx, buf, eol);
			xfree(msgbuf);
		} else {
			_log_printf(log, log->buf, stderr, "%s: %s%s%s",
				    log->argv0, pfx, buf, eol);
		}
		fflush(stderr);
	}

	if (!log->logfp || (level > log->opt.logfile_level)) {
		/* do nothing */
	} else if (log->opt.logfile_fmt == LOG_FILE_FMT_JSON) {
		/*
		 * log format json gets all of the logging to match
		 * https://docs.docker.com/config/containers/logging/json-file/
		 */
		char time[50];
		char *json = NULL;
		char *stream;
		data_t *out = data_set_dict(data_new());

		if (level <= log->opt.stderr_level)
			stream = "stderr";
		else
			stream = "stdout";

		log_timestamp(time, sizeof(time));
		data_set_string_fmt(data_key_set(out, "log"), "%s%s%s",
				    log->prefix, pfx, buf);
		data_set_string(data_key_set(out, "stream"), stream);
		data_set_string(data_key_set(out, "time"), time);

		serialize_g_data_to_string(&json, NULL, out, MIME_TYPE_JSON,
					   SER_FLAGS_COMPACT);
		FREE_NULL_DATA(out);

		if (json)
			_log_printf(log, log->fbuf, log->logfp, "%s\n", json);

		xfree(json);
		fflush(log->logfp);
	} else {
		xassert(log->opt.logfile_fmt == LOG_FILE_FMT_TIMESTAMP);
		xlogfmtcat(&msgbuf, "[%M] %s%s", log->prefix, pfx);
		_log_printf(log, log->fbuf, log->logfp, "%s%s\n", msgbuf, buf);
		fflush(log->logfp);

		xfree(msgbuf);
	}

	if (level <=  log->opt.syslog_level) {

		/* Avoid changing errno if syslog fails */
		int orig_errno = errno;
		syslog(priority, "%s%s%s", log->prefix, pfx, buf);
		errno = orig_errno;
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
	LOG_MACRO(LOG_LEVEL_FATAL, false, fmt);
	log_flush();

	if (getenv("ABORT_ON_FATAL"))
		abort();

	exit(1);
}

/*
 * attempt to log message and exit()
 */
void fatal_abort(const char *fmt, ...)
{
	LOG_MACRO(LOG_LEVEL_FATAL, false, fmt);
	log_flush();

	abort();
}

/*
 * Attempt to log message at a variable log level
 */
void log_var(const log_level_t log_lvl, const char *fmt, ...)
{
	LOG_MACRO(log_lvl, false, fmt);

	if (log_lvl == LOG_LEVEL_FATAL) {
		log_flush();
		exit(1);
	}
}

void sched_log_var(const log_level_t log_lvl, const char *fmt, ...)
{
	LOG_MACRO(log_lvl, true, fmt);

	if (log_lvl == LOG_LEVEL_FATAL) {
		log_flush();
		exit(1);
	}
}

int error(const char *fmt, ...)
{
	LOG_MACRO(LOG_LEVEL_ERROR, false, fmt);

	/*
	 *  Return SLURM_ERROR so calling functions can
	 *    do "return error (...);"
	 */
	return SLURM_ERROR;
}

/*
 * Like error(), but printed without the error: prefix so SPANK plugins
 * can have a convenient way to return messages to the user.
 */
void spank_log(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	_log_msg(LOG_LEVEL_ERROR, false, true, false, fmt, ap);
	va_end(ap);
}

void warning(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	_log_msg(LOG_LEVEL_INFO, false, false, true, fmt, ap);
	va_end(ap);
}

void slurm_info(const char *fmt, ...)
{
	LOG_MACRO(LOG_LEVEL_INFO, false, fmt);
}

void slurm_verbose(const char *fmt, ...)
{
	LOG_MACRO(LOG_LEVEL_VERBOSE, false, fmt);
}

void slurm_debug(const char *fmt, ...)
{
	LOG_MACRO(LOG_LEVEL_DEBUG, false, fmt);
}

void slurm_debug2(const char *fmt, ...)
{
	LOG_MACRO(LOG_LEVEL_DEBUG2, false, fmt);
}

void slurm_debug3(const char *fmt, ...)
{
	LOG_MACRO(LOG_LEVEL_DEBUG3, false, fmt);
}

/*
 * Debug levels higher than debug3 are not written to stderr in the
 * slurmstepd process after stderr is connected back to the client (srun).
 */
void slurm_debug4(const char *fmt, ...)
{
	LOG_MACRO(LOG_LEVEL_DEBUG4, false, fmt);
}

void slurm_debug5(const char *fmt, ...)
{
	LOG_MACRO(LOG_LEVEL_DEBUG5, false, fmt);
}

void sched_error(const char *fmt, ...)
{
	LOG_MACRO(LOG_LEVEL_ERROR, true, fmt);
}

void sched_info(const char *fmt, ...)
{
	LOG_MACRO(LOG_LEVEL_INFO, true, fmt);
}

void sched_verbose(const char *fmt, ...)
{
	LOG_MACRO(LOG_LEVEL_VERBOSE, true, fmt);
}

/* Return the highest LOG_LEVEL_* used for any logging mechanism.
 * For example, if LOG_LEVEL_INFO is returned, we know that all verbose and
 * debug type messages will be ignored. */
extern int get_log_level(void)
{
	return highest_log_level;
}

extern int get_sched_log_level(void)
{
	return MAX(highest_log_level, highest_sched_log_level);
}

/*
 * log_build_step_id_str() - print a slurm_step_id_t as " StepId=...", with
 * Batch and Extern used as appropriate.
 */
extern char *log_build_step_id_str(
	slurm_step_id_t *step_id, char *buf, int buf_size, uint16_t flags)
{
	int pos = 0;

	xassert(buf);
	xassert(buf_size > 1);

	buf[pos] = '\0';

	if (flags & STEP_ID_FLAG_SPACE)
		buf[pos++] = ' ';

	/*
	 * NOTE: You will notice we put a %.0s in front of the string if running
	 * with %ps like interactions.
	 * This is to handle the fact that we can't remove the step_id
	 * argument from the va_list directly. So when we call vsnprintf()
	 * to handle the va_list this will effectively skip this argument.
	 */
	if (flags & STEP_ID_FLAG_PS)
		pos += snprintf(buf + pos, buf_size - pos, "%%.0s");

	if (!(flags & STEP_ID_FLAG_NO_PREFIX))
		pos += snprintf(buf + pos, buf_size - pos, "%s",
				(!step_id || (step_id->step_id != NO_VAL)) ?
				"StepId=" : "JobId=");

	if (!step_id || !step_id->job_id) {
		snprintf(buf + pos, buf_size - pos, "Invalid");
		return buf;
	}

	if (step_id->job_id && !(flags & STEP_ID_FLAG_NO_JOB))
		pos += snprintf(buf + pos, buf_size - pos,
				"%u%s", step_id->job_id,
				step_id->step_id == NO_VAL ? "" : ".");

	if ((pos >= buf_size) || (step_id->step_id == NO_VAL))
		return buf;

	if (step_id->step_id == SLURM_BATCH_SCRIPT)
		pos += snprintf(buf + pos, buf_size - pos, "batch");
	else if (step_id->step_id == SLURM_EXTERN_CONT)
		pos += snprintf(buf + pos, buf_size - pos, "extern");
	else if (step_id->step_id == SLURM_INTERACTIVE_STEP)
		pos += snprintf(buf + pos, buf_size - pos, "interactive");
	else if (step_id->step_id == SLURM_PENDING_STEP)
		pos += snprintf(buf + pos, buf_size - pos, "TDB");
	else
		pos += snprintf(buf + pos, buf_size - pos, "%u",
				step_id->step_id);

	if (pos >= buf_size)
		return buf;

	if (step_id->step_het_comp != NO_VAL)
		snprintf(buf + pos, buf_size - pos, "+%u",
			 step_id->step_het_comp);

	return buf;
}

extern void _log_flag_hex(const void *data, size_t len, ssize_t start,
			  ssize_t end, const char *fmt, ...)
{
	va_list ap;
	char *prepend;
	static const int hex_cols = 16;

	if (!data || !len)
		return;

	if (start < 0)
		start = 0;
	if ((end < 0) || (end > len))
		end = len;

	va_start(ap, fmt);
	prepend = vxstrfmt(fmt, ap);
	va_end(ap);

	for (size_t i = start; (i < end); ) {
		int remain = end - i;
		int print = (remain < hex_cols) ? remain : hex_cols;
		char *phex = xstring_bytes2hex((data + i), print, " ");
		char *pstr = xstring_bytes2printable((data + i), print, '.');

		format_print(LOG_LEVEL_VERBOSE, "%s [%04zu/%04zu] 0x%s \"%s\"",
			     prepend, i, len, phex, pstr);

		i += print;
		xfree(phex);
		xfree(pstr);
	}

	xfree(prepend);
}
