/*****************************************************************************\
 *  log.c - slurm logging facilities
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>
 *  UCRL-CODE-2002-040.
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
#include <syslog.h>

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif /* WITH_PTHREADS */


#if HAVE_STDLIB_H
#  include <stdlib.h>	/* for abort() */
#endif

#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/safeopen.h"
#include "src/common/slurm_errno.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#ifndef LINEBUFSIZE
#  define LINEBUFSIZE 256
#endif

/* 
** struct defining a "log" type
*/
typedef struct {
	char *argv0;
	FILE *logfp;
	log_facility_t facility;
	log_options_t opt;
	unsigned initialized:1;
}	log_t;

/* static variables */
#ifdef WITH_PTHREADS
  static pthread_mutex_t  log_lock;
#else
  static int	log_lock;
#endif /* WITH_PTHREADS */
static log_t            *log = NULL;

#define LOG_INITIALIZED ((log != NULL) && (log->initialized))

/* define a default argv0 */
#if HAVE_PROGRAM_INVOCATION_SHORT_NAME
extern char * program_invocation_short_name;
#  define default_argv0	program_invocation_short_name
#else 
#  define default_argv0 ""
#endif

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
	}

	if (prog) {
		if (log->argv0)
			xfree(log->argv0);
		log->argv0 = xstrdup(xbasename(prog));
	} else if (!log->argv0) {
		log->argv0 = xstrdup(default_argv0);
	}

	log->opt = opt;

	if (log->opt.syslog_level > LOG_LEVEL_QUIET)
		log->facility = fac;

	if (logfile && log->opt.logfile_level > LOG_LEVEL_QUIET) {
		FILE *fp; 

		fp = safeopen(logfile, "a", SAFEOPEN_LINK_OK);

		if (!fp) {
			char *errmsg = NULL;
			slurm_mutex_unlock(&log_lock);
			xslurm_strerrorcat(errmsg);
			fprintf(stderr, 
			        "%s: log_init(): Unable to open logfile"
			        "`%s': %s\n", prog, logfile, errmsg);
			xfree(errmsg);
			rc = errno;
			goto out;
		} else
			log->logfp = fp;
	}

	log->initialized = 1;
 out:
	return rc;
}


/* initialize log mutex, then initialize log data structures
 */
int log_init(char *prog, log_options_t opt, log_facility_t fac, char *logfile)
{
	int rc = 0;

	slurm_mutex_init(&log_lock);
	slurm_mutex_lock(&log_lock);
	rc = _log_init(prog, opt, fac, logfile);
	slurm_mutex_unlock(&log_lock);
	return rc;
}

void log_reinit()
{
	slurm_mutex_init(&log_lock);
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

/* return the FILE * of the current logfile (stderr if logging to stderr)
 */
FILE *log_fp(void)
{
	FILE *fp;
	slurm_mutex_lock(&log_lock);
	if (log->logfp)
		fp = log->logfp;
	else
		fp = stderr;
	slurm_mutex_unlock(&log_lock);
	return fp;
}

/* return a heap allocated string formed from fmt and ap arglist
 * returned string is allocated with xmalloc, so must free with xfree.
 * 
 * args are like printf, with the addition of the following format chars:
 * - %m expands to strerror(errno)
 * - %t expands to strftime("%x %X") [ locally preferred short date/time ]
 * - %T expands to rfc822 date time  [ "dd Mon yyyy hh:mm:ss GMT offset" ]
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
			if (len > 0) {
				memcpy(tmp, fmt, len);
				tmp[len] = '\0';
				xstrcat(buf, tmp);
			}

			switch (*(++p)) {
		        case '%':	/* "%%" => "%" */
				xstrcatchar(buf, '%');
				break;

			case 'm':	/* "%m" => strerror(errno) */
				xslurm_strerrorcat(buf);
				break;

			case 't': 	/* "%t" => locally preferred date/time */ 
				xstrftimecat(buf, "%x %X");
				break;
			case 'T': 	/* "%T" => "dd Mon yyyy hh:mm:ss off"  */
				xstrftimecat(buf, "%a %d %b %Y %H:%M:%S %z");   
				break;
			case 'M':       /* "%M" => "Mon DD hh:mm:ss"           */
				xstrftimecat(buf, "%b %d %T");
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

	if (!LOG_INITIALIZED) {
		log_options_t opts = LOG_OPTS_STDERR_ONLY;
		log_init(NULL, opts, 0, NULL);
	}

	slurm_mutex_lock(&log_lock);

	if (level > log->opt.syslog_level  && 
	    level > log->opt.logfile_level && 
	    level > log->opt.stderr_level) {
		slurm_mutex_unlock(&log_lock);
		return;
	}

	if (log->opt.prefix_level || log->opt.syslog_level > level) {
		switch (level) {
		case LOG_LEVEL_FATAL:
			priority = LOG_CRIT;
			pfx = "fatal: ";
			break;

		case LOG_LEVEL_ERROR:
			priority = LOG_ERR;
			pfx = "error: ";
			break;

		case LOG_LEVEL_INFO:
		case LOG_LEVEL_VERBOSE:
			priority = LOG_INFO;
			break;

		case LOG_LEVEL_DEBUG:
			priority = LOG_DEBUG;
			pfx = "debug: ";
			break;

		case LOG_LEVEL_DEBUG2:
			priority = LOG_DEBUG;
			pfx = "debug2: ";
			break;

		case LOG_LEVEL_DEBUG3:
			priority = LOG_DEBUG;
			pfx = "debug3: ";
			break;

		default:
			priority = LOG_ERR;
			pfx = "internal error: ";
			break;
		}

	}

	/* format the basic message */
	buf = vxstrfmt(fmt, args);

	if (level <= log->opt.stderr_level) {
		fflush(stdout);
		if (strlen(buf) > 0 && buf[strlen(buf) - 1] == '\n')
			fprintf(stderr, "%s: %s%s", log->argv0, pfx, buf);
		else
			fprintf(stderr, "%s: %s%s\n", log->argv0, pfx, buf);
		fflush(stderr);
	}

	if (level <= log->opt.logfile_level && log->logfp != NULL) {
		xlogfmtcat(&msgbuf, "[%M] %s%s", pfx, buf);

		if (strlen(buf) > 0 && buf[strlen(buf) - 1] == '\n')
			fprintf(log->logfp, "%s", msgbuf);
		else
			fprintf(log->logfp, "%s\n", msgbuf);
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

/* LLNL Software development Toolbox (LSD-Tools)
 * fatal() and nomem() functions
 */
void
lsd_fatal_error(char *file, int line, char *msg)
{
	fatal("%s:%d %s: %m", file, line, msg);
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
	fatal_cleanup();

#ifndef  NDEBUG
	abort();
#endif
}

void error(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	log_msg(LOG_LEVEL_ERROR, fmt, ap);
	va_end(ap);
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
