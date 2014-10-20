/*****************************************************************************\
 *  log.c - slurm logging facilities
 *  $Id$
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>

#if HAVE_STRING_H
#  include <string.h>
#endif

#include <stdarg.h>
#include <errno.h>

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif /* WITH_PTHREADS */


#if HAVE_STDLIB_H
#  include <stdlib.h>	/* for abort() */
#endif

#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/unistd.h>

#include "slurm/slurm_errno.h"
#include "src/common/log.h"
#include "src/common/fd.h"
#include "src/common/macros.h"
#include "src/common/safeopen.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/slurm_protocol_api.h"

#ifndef LINEBUFSIZE
#  define LINEBUFSIZE 256
#endif

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
strong_alias(dump_cleanup_list,	slurm_dump_cleanup_list);
strong_alias(fatal_add_cleanup,	slurm_fatal_add_cleanup);
strong_alias(fatal_add_cleanup_job,	slurm_fatal_add_cleanup_job);
strong_alias(fatal_remove_cleanup,	slurm_fatal_remove_cleanup);
strong_alias(fatal_remove_cleanup_job,	slurm_fatal_remove_cleanup_job);
strong_alias(fatal_cleanup,	slurm_fatal_cleanup);
strong_alias(fatal,		slurm_fatal);
strong_alias(error,		slurm_error);
strong_alias(info,		slurm_info);
strong_alias(verbose,		slurm_verbose);
strong_alias(debug,		slurm_debug);
strong_alias(debug2,		slurm_debug2);
strong_alias(debug3,		slurm_debug3);
strong_alias(debug4,		slurm_debug4);
strong_alias(debug5,		slurm_debug5);

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
#ifdef WITH_PTHREADS
  static pthread_mutex_t  log_lock = PTHREAD_MUTEX_INITIALIZER;
#else
  static int              log_lock;
#endif /* WITH_PTHREADS */
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
#ifdef WITH_PTHREADS
static void _atfork_prep()   { slurm_mutex_lock(&log_lock);   }
static void _atfork_parent() { slurm_mutex_unlock(&log_lock); }
static void _atfork_child()  { slurm_mutex_unlock(&log_lock); }
static bool at_forked = false;
#  define atfork_install_handlers()                                           \
          while (!at_forked) {                                                \
                pthread_atfork(_atfork_prep, _atfork_parent, _atfork_child);  \
		at_forked = true;                                             \
	  }
#else
#  define atfork_install_handlers() (NULL)
#endif
static void _log_flush(log_t *log);


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
	    (S_ISSOCK(stat_buf.st_mode) && (recv(fd, &temp, 1, 0) == 0)))
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
	if (!slurm_prog_name && log->argv0 && (strlen(log->argv0) > 1))
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
		FILE *fp;

		fp = safeopen(logfile, "a", SAFEOPEN_LINK_OK);

		if (!fp) {
			char *errmsg = NULL;
			xslurm_strerrorcat(errmsg);
			fprintf(stderr,
				"%s: log_init(): Unable to open logfile"
			        "`%s': %s\n", prog, logfile, errmsg);
			xfree(errmsg);
			rc = errno;
			goto out;
		}

		if (log->logfp)
			fclose(log->logfp); /* Ignore errors */

		log->logfp = fp;
	}

	if (log->logfp) {
		int fd;
		if ((fd = fileno(log->logfp)) < 0)
			log->logfp = NULL;
		else
			fd_set_close_on_exec(fd);
	}

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
		FILE *fp;

		fp = safeopen(logfile, "a", SAFEOPEN_LINK_OK);

		if (!fp) {
			rc = errno;
			goto out;
		}

		if (sched_log->logfp)
			fclose(sched_log->logfp); /* Ignore errors */

		sched_log->logfp = fp;
	}

	if (sched_log->logfp) {
		int fd;
		if ((fd = fileno(sched_log->logfp)) < 0)
			sched_log->logfp = NULL;
		else
			fd_set_close_on_exec(fd);
	}

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

void log_set_fpfx(char *prefix)
{
	slurm_mutex_lock(&log_lock);
	xfree(log->fpfx);
	if (!prefix)
		log->fpfx = xstrdup("");
	else {
		log->fpfx = xstrdup(prefix);
		xstrcatchar(log->fpfx, ' ');
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

	gettimeofday(&now, NULL);

	sprintf(idbuf, "%.15s.%-6d %5d %p", ctime(&now.tv_sec) + 4,
	        (int)now.tv_usec, (int)getpid(), (void *)pthread_self());

}

/* return a heap allocated string formed from fmt and ap arglist
 * returned string is allocated with xmalloc, so must free with xfree.
 *
 * args are like printf, with the addition of the following format chars:
 * - %m expands to strerror(errno)
 * - %t expands to strftime("%x %X") [ locally preferred short date/time ]
 * - %T expands to rfc2822 date time  [ "dd, Mon yyyy hh:mm:ss GMT offset" ]
 *
 * simple format specifiers are handled explicitly to avoid calls to
 * vsnprintf and allow dynamic sizing of the message buffer. If a call
 * is made to vsnprintf, however, the message will be limited to 1024 bytes.
 * (inc. newline)
 *
 */
static char *vxstrfmt(const char *fmt, va_list ap)
{
	char        *buf = NULL;
	char        *p   = NULL;
	size_t      len = (size_t) 0;
	char        tmp[LINEBUFSIZE];
	int         unprocessed = 0;
	int         long_long = 0;

	while (*fmt != '\0') {

		if ((p = (char *)strchr(fmt, '%')) == NULL) {
			/* no more format chars */
			xstrcat(buf, fmt);
			break;

		} else {        /* *p == '%' */
			/* take difference from fmt to just before `%' */
			len = (size_t) ((long)(p) - (long)fmt);
			/* append from fmt to p into buf if there's
			 * anythere there
			 */
			if (len > 0)
				xstrncat(buf, fmt, len);

			switch (*(++p)) {
		        case '%':	/* "%%" => "%" */
				xstrcatchar(buf, '%');
				break;

			case 'm':	/* "%m" => strerror(errno) */
				xslurm_strerrorcat(buf);
				break;

			case 't': 	/* "%t" => locally preferred date/time*/
				xstrftimecat(buf, "%x %X");
				break;
			case 'T': 	/* "%T" => "dd, Mon yyyy hh:mm:ss off" */
				xstrftimecat(buf, "%a, %d %b %Y %H:%M:%S %z");
				break;
			case 'M':
				if (!log)
					xiso8601timecat(buf, true);
				else {
					switch (log->fmt) {
					case LOG_FMT_ISO8601_MS: /* "%M" => "yyyy-mm-ddThh:mm:ss.fff"  */
						xiso8601timecat(buf, true);
						break;
					case LOG_FMT_ISO8601: /* "%M" => "yyyy-mm-ddThh:mm:ss.fff"  */
						xiso8601timecat(buf, false);
						break;
					case LOG_FMT_RFC5424_MS: /* "%M" => "yyyy-mm-ddThh:mm:ss.fff(+/-)hh:mm" */
						xrfc5424timecat(buf, true);
						break;
					case LOG_FMT_RFC5424:  /* "%M" => "yyyy-mm-ddThh:mm:ss.fff(+/-)hh:mm" */
						xrfc5424timecat(buf, false);
						break;
					case LOG_FMT_CLOCK:
						/* "%M" => "usec" */
#if defined(__FreeBSD__)
						snprintf(tmp, sizeof(tmp), "%d", clock());
#else
						snprintf(tmp, sizeof(tmp), "%ld", clock());
#endif
						xstrcat(buf, tmp);
						break;
					case LOG_FMT_SHORT: /* "%M" => "Mon DD hh:mm:ss"         */
						xstrftimecat(buf, "%b %d %T");
						break;
					case LOG_FMT_THREAD_ID:
						set_idbuf(tmp);
						xstrcat(buf, tmp);
						break;
					}
				}
				break;
			case 's':	/* "%s" => append string */
				/* we deal with this case for efficiency */
				if (unprocessed == 0)
					xstrcat(buf, va_arg(ap, char *));
				else
					xstrcat(buf, "%s");
				break;
			case 'f': 	/* "%f" => append double */
				/* again, we only handle this for efficiency */
				if (unprocessed == 0) {
					snprintf(tmp, sizeof(tmp), "%f",
						 va_arg(ap, double));
					xstrcat(buf, tmp);
				} else
					xstrcat(buf, "%f");
				break;
			case 'd':
				if (unprocessed == 0) {
					snprintf(tmp, sizeof(tmp), "%d",
						 va_arg(ap, int));
					xstrcat(buf, tmp);
				} else
					xstrcat(buf, "%d");
				break;
			case 'u':
				if (unprocessed == 0) {
					snprintf(tmp, sizeof(tmp), "%u",
						 va_arg(ap, int));
					xstrcat(buf, tmp);
				} else
					xstrcat(buf, "%u");
				break;
			case 'l':
				if ((unprocessed == 0) && (*(p+1) == 'l')) {
					long_long = 1;
					p++;
				}

				if ((unprocessed == 0) && (*(p+1) == 'u')) {
					if (long_long) {
						snprintf(tmp, sizeof(tmp),
							"%llu",
							 va_arg(ap,
								long long unsigned));
						long_long = 0;
					} else
						snprintf(tmp, sizeof(tmp),
							 "%lu",
							 va_arg(ap,
								long unsigned));
					xstrcat(buf, tmp);
					p++;
				} else if ((unprocessed==0) && (*(p+1)=='d')) {
					if (long_long) {
						snprintf(tmp, sizeof(tmp),
							"%lld",
							 va_arg(ap,
								long long int));
						long_long = 0;
					} else
						snprintf(tmp, sizeof(tmp),
							 "%ld",
							 va_arg(ap, long int));
					xstrcat(buf, tmp);
					p++;
				} else if ((unprocessed==0) && (*(p+1)=='f')) {
					if (long_long) {
						xstrcat(buf, "%llf");
						long_long = 0;
					} else
						snprintf(tmp, sizeof(tmp),
							 "%lf",
							 va_arg(ap, double));
					xstrcat(buf, tmp);
					p++;
				} else if ((unprocessed==0) && (*(p+1)=='x')) {
					if (long_long) {
						snprintf(tmp, sizeof(tmp),
							 "%llx",
							 va_arg(ap,
								long long int));
						long_long = 0;
					} else
						snprintf(tmp, sizeof(tmp),
							 "%lx",
							 va_arg(ap, long int));
					xstrcat(buf, tmp);
					p++;
				} else if (long_long) {
					xstrcat(buf, "%ll");
					long_long = 0;
				} else
					xstrcat(buf, "%l");
				break;
			case 'L':
				if ((unprocessed==0) && (*(p+1)=='f')) {
					snprintf(tmp, sizeof(tmp), "%Lf",
						 va_arg(ap, long double));
					xstrcat(buf, tmp);
					p++;
				} else
					xstrcat(buf, "%L");
				break;
			default:	/* try to handle the rest  */
				xstrcatchar(buf, '%');
				xstrcatchar(buf, *p);
				unprocessed++;
				break;
			}

		}

		fmt = p + 1;
	}

	if (unprocessed > 0) {
		vsnprintf(tmp, sizeof(tmp)-1, buf, ap);
		xfree(buf);
		return xstrdup(tmp);
	}

	return buf;
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
static void log_msg(log_level_t level, const char *fmt, va_list args)
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

	if (SCHED_LOG_INITIALIZED &&
	    (sched_log->opt.logfile_level > LOG_LEVEL_QUIET) &&
	    (strncmp(fmt, "sched: ", 7) == 0)) {
		buf = vxstrfmt(fmt, args);
		xlogfmtcat(&msgbuf, "[%M] %s%s%s", sched_log->fpfx, pfx, buf);
		_log_printf(sched_log, sched_log->fbuf, sched_log->logfp,
			    "%s\n", msgbuf);
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
			pfx = "error: ";
			break;

		case LOG_LEVEL_SCHED:
		case LOG_LEVEL_INFO:
		case LOG_LEVEL_VERBOSE:
			priority = LOG_INFO;
			break;

		case LOG_LEVEL_DEBUG:
			priority = LOG_DEBUG;
			pfx = "debug:  ";
			break;

		case LOG_LEVEL_DEBUG2:
			priority = LOG_DEBUG;
			pfx = "debug2: ";
			break;

		case LOG_LEVEL_DEBUG3:
			priority = LOG_DEBUG;
			pfx = "debug3: ";
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
			_log_printf(log, log->buf, stderr, "%s %s: %s%s\n",
			            tmp, log->argv0, pfx, buf);
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

		xlogfmtcat(&msgbuf, "%s%s", pfx, buf);
		openlog(log->argv0, LOG_PID, log->facility);
		syslog(priority, "%.500s", msgbuf);
		closelog();

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

/* LLNL Software development Toolbox (LSD-Tools)
 * fatal() and nomem() functions
 */
void
lsd_fatal_error(char *file, int line, char *msg)
{
	error("%s:%d %s: %m", file, line, msg);
}

void *
lsd_nomem_error(char *file, int line, char *msg)
{
	error("%s:%d %s: %m", file, line, msg);
	slurm_seterrno(ENOMEM);
	return NULL;
}

/*
 * attempt to log message and abort()
 */
void fatal(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	log_msg(LOG_LEVEL_FATAL, fmt, ap);
	va_end(ap);
	log_flush();
	fatal_cleanup();

	exit(1);
}

int error(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	log_msg(LOG_LEVEL_ERROR, fmt, ap);
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
	log_msg(LOG_LEVEL_INFO, fmt, ap);
	va_end(ap);
}

void verbose(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	log_msg(LOG_LEVEL_VERBOSE, fmt, ap);
	va_end(ap);
}

void debug(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	log_msg(LOG_LEVEL_DEBUG, fmt, ap);
	va_end(ap);
}

void debug2(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	log_msg(LOG_LEVEL_DEBUG2, fmt, ap);
	va_end(ap);
}

void debug3(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	log_msg(LOG_LEVEL_DEBUG3, fmt, ap);
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
	log_msg(LOG_LEVEL_DEBUG4, fmt, ap);
	va_end(ap);
}

void debug5(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	log_msg(LOG_LEVEL_DEBUG5, fmt, ap);
	va_end(ap);
}

void schedlog(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	log_msg(LOG_LEVEL_SCHED, fmt, ap);
	va_end(ap);
}

/* Fatal cleanup */

struct fatal_cleanup {
	pthread_t thread_id;
	struct fatal_cleanup *next;
	void (*proc) (void *);
	void *context;
};

/* static variables */
#ifdef WITH_PTHREADS
  static pthread_mutex_t  fatal_lock = PTHREAD_MUTEX_INITIALIZER;
#else
  static int	fatal_lock;
#endif /* WITH_PTHREADS */
static struct fatal_cleanup *fatal_cleanups = NULL;

/* Registers a cleanup function to be called by fatal() for this thread
** before exiting. */
void
fatal_add_cleanup(void (*proc) (void *), void *context)
{
	struct fatal_cleanup *cu;

	slurm_mutex_lock(&fatal_lock);
	cu = xmalloc(sizeof(*cu));
	cu->thread_id = pthread_self();
	cu->proc = proc;
	cu->context = context;
	cu->next = fatal_cleanups;
	fatal_cleanups = cu;
	slurm_mutex_unlock(&fatal_lock);
}

/* Registers a cleanup function to be called by fatal() for all threads
** of the job. */
void
fatal_add_cleanup_job(void (*proc) (void *), void *context)
{
	struct fatal_cleanup *cu;

	slurm_mutex_lock(&fatal_lock);
	cu = xmalloc(sizeof(*cu));
	cu->thread_id = 0;
	cu->proc = proc;
	cu->context = context;
	cu->next = fatal_cleanups;
	fatal_cleanups = cu;
	slurm_mutex_unlock(&fatal_lock);
}

/* Removes a cleanup frunction to be called at fatal() for this thread. */
void
fatal_remove_cleanup(void (*proc) (void *context), void *context)
{
	struct fatal_cleanup **cup, *cu;
	pthread_t my_thread_id = pthread_self();

	slurm_mutex_lock(&fatal_lock);
	for (cup = &fatal_cleanups; *cup; cup = &cu->next) {
		cu = *cup;
		if (cu->thread_id == my_thread_id &&
		    cu->proc == proc &&
		    cu->context == context) {
			*cup = cu->next;
			xfree(cu);
			slurm_mutex_unlock(&fatal_lock);
			return;
		}
	}
	slurm_mutex_unlock(&fatal_lock);
	fatal("fatal_remove_cleanup: no such cleanup function: 0x%lx 0x%lx",
	    (u_long) proc, (u_long) context);
}

/* Removes a cleanup frunction to be called at fatal() for all threads of
** the job. */
void
fatal_remove_cleanup_job(void (*proc) (void *context), void *context)
{
	struct fatal_cleanup **cup, *cu;

	slurm_mutex_lock(&fatal_lock);
	for (cup = &fatal_cleanups; *cup; cup = &cu->next) {
		cu = *cup;
		if (cu->thread_id == 0 &&
		    cu->proc == proc &&
		    cu->context == context) {
			*cup = cu->next;
			xfree(cu);
			slurm_mutex_unlock(&fatal_lock);
			return;
		}
	}
	slurm_mutex_unlock(&fatal_lock);
	fatal("fatal_remove_cleanup_job: no such cleanup function: "
	      "0x%lx 0x%lx", (u_long) proc, (u_long) context);
}

/* Execute cleanup functions, first thread-specific then those for the
** whole job */
void
fatal_cleanup(void)
{
	struct fatal_cleanup **cup, *cu;
	pthread_t my_thread_id = pthread_self();

	slurm_mutex_lock(&fatal_lock);
	for (cup = &fatal_cleanups; *cup; ) {
		cu = *cup;
		if (cu->thread_id != my_thread_id) {
			cup = &cu->next;
			continue;
		}
		debug("Calling cleanup 0x%lx(0x%lx)",
		      (u_long) cu->proc, (u_long) cu->context);
		(*cu->proc) (cu->context);
		*cup = cu->next;
		xfree(cu);
	}
	for (cup = &fatal_cleanups; *cup; cup = &cu->next) {
		cu = *cup;
		if (cu->thread_id != 0)
			continue;
		debug("Calling cleanup 0x%lx(0x%lx)",
		      (u_long) cu->proc, (u_long) cu->context);
		(*cu->proc) (cu->context);
	}
	slurm_mutex_unlock(&fatal_lock);
}

/* Print a list of cleanup frunctions to be called at fatal(). */
void
dump_cleanup_list(void)
{
	struct fatal_cleanup **cup, *cu;

	slurm_mutex_lock(&fatal_lock);
	for (cup = &fatal_cleanups; *cup; cup = &cu->next) {
		cu = *cup;
		info ("loc=%ld thread_id=%ld proc=%ld, context=%ld, next=%ld",
			(long)cu, (long)cu->thread_id, (long)cu->proc,
			(long)cu->context, (long)cu->next);
	}
	slurm_mutex_unlock(&fatal_lock);
}
