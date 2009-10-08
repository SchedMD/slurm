/*****************************************************************************\
 *  log.h - configurable logging for slurm: log to file, stderr and/or syslog.
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
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

#ifndef _LOG_H
#define _LOG_H

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef HAVE_SYS_SYSLOG_H
#  include <sys/syslog.h>
#endif

#include <syslog.h> 	
#include <stdio.h>

#include "src/common/macros.h"
#include "src/common/cbuf.h"

/* supported syslog facilities and levels */
typedef enum {
	SYSLOG_FACILITY_DAEMON = 	LOG_DAEMON,
	SYSLOG_FACILITY_USER = 		LOG_USER,
	SYSLOG_FACILITY_AUTH = 		LOG_AUTH,
#ifdef LOG_AUTHPRIV
	SYSLOG_FACILITY_AUTHPRIV =	LOG_AUTHPRIV,
#endif 
	SYSLOG_FACILITY_LOCAL0 =	LOG_LOCAL0,
	SYSLOG_FACILITY_LOCAL1 =	LOG_LOCAL1,
	SYSLOG_FACILITY_LOCAL2 =	LOG_LOCAL2,
	SYSLOG_FACILITY_LOCAL3 =	LOG_LOCAL3,
	SYSLOG_FACILITY_LOCAL4 =	LOG_LOCAL4,
	SYSLOG_FACILITY_LOCAL5 =	LOG_LOCAL5,
	SYSLOG_FACILITY_LOCAL6 =	LOG_LOCAL6,
	SYSLOG_FACILITY_LOCAL7 =	LOG_LOCAL7 
} 	log_facility_t;

/*
 * log levels, logging will occur at or below the selected level
 * QUIET disable logging completely.
 */
typedef enum {
	LOG_LEVEL_QUIET = 0,
	LOG_LEVEL_FATAL,
	LOG_LEVEL_ERROR,
	LOG_LEVEL_INFO,
	LOG_LEVEL_VERBOSE,
	LOG_LEVEL_DEBUG,
	LOG_LEVEL_DEBUG2,
	LOG_LEVEL_DEBUG3,
	LOG_LEVEL_DEBUG4,
	LOG_LEVEL_DEBUG5,
	LOG_LEVEL_END
}	log_level_t;


/*
 * log options: Each of stderr, syslog, and logfile can have a different level
 */
typedef struct {
	log_level_t stderr_level;   /* max level to log to stderr         */
	log_level_t syslog_level;   /* max level to log to syslog         */
	log_level_t logfile_level;  /* max level to log to logfile        */
	unsigned    prefix_level:1; /* prefix level (e.g. "debug: ") if 1 */
	unsigned    buffered:1;     /* Use internal buffer to never block */
} 	log_options_t;

/* some useful initializers for log_options_t
 */
#define LOG_OPTS_INITIALIZER	\
	{ LOG_LEVEL_INFO, LOG_LEVEL_INFO, LOG_LEVEL_INFO, 1, 0 }

#define LOG_OPTS_SYSLOG_DEFAULT	\
	{ LOG_LEVEL_QUIET, LOG_LEVEL_INFO, LOG_LEVEL_QUIET, 1, 0 }  

#define LOG_OPTS_STDERR_ONLY	\
	{ LOG_LEVEL_INFO,  LOG_LEVEL_QUIET, LOG_LEVEL_QUIET, 1, 0 }

/* 
 * initialize log module (called only once)
 *
 * example:
 *
 * To initialize log module to print fatal messages to stderr, and
 * all messages up to and including info() to syslog:
 *
 * log_options_t logopts = LOG_OPTS_INITIALIZER;
 * logopts.stderr_level  = LOG_LEVEL_FATAL;
 * logopts.syslog_level  = LOG_LEVEL_INFO;
 *
 * rc = log_init(argv[0], logopts, SYSLOG_FACILITY_DAEMON, NULL);
 *
 * log function automatically takes the basename() of argv0.
 */
int log_init(char *argv0, log_options_t opts, 
              log_facility_t fac, char *logfile);

/* reinitialize log module.
 * Keep same log options as previously initialized log, but reinit mutex
 * that protects the log. This call is needed after a fork() in a threaded
 * program
 */
void log_reinit(void);

/* 
 * Close log and free associated memory
 */
void log_fini(void);

/* Alter log facility, options are like log_init() above, except that
 * an argv0 argument is not passed. 
 *
 * This function may be called multiple times.
 */
int log_alter(log_options_t opts, log_facility_t fac, char *logfile);

/* Set prefix for log file entries
 * (really only useful for slurmd at this point)
 */
void log_set_fpfx(char *pfx);

/* 
 * (re)set argv0 string prepended to all log messages
 */
void log_set_argv0(char *pfx);

/* grab the FILE * of the current logfile (or stderr if not logging to
 * a file)
 */
FILE *log_fp(void);

/* 
 * Buffered log functions:
 * 
 * log_has_data() returns true if there is data in the 
 * internal log buffer
 */
bool log_has_data(void);

/*
 * log_flush() attempts to flush all data in the internal
 * log buffer to the appropriate output stream.
 */
void log_flush(void);

/* 
 * the following log a message to the log facility at the appropriate level:
 *
 * Messages do not need a newline!
 *
 * args are printf style with the following exceptions:
 *
 * fmt     expands to
 * ~~~~    ~~~~~~~~~~~
 * "%m" => strerror(errno)
 * "%t" => strftime "%x %X"  (locally preferred short date/time) 
 * "%T" => strftime "%a %d %b %Y %H:%M:%S %z" (rfc822 date/time)
 */

/* fatal() aborts program unless NDEBUG defined
 * error() returns SLURM_ERROR
 */
void	fatal(const char *, ...);
int	error(const char *, ...);
void	info(const char *, ...);
void	verbose(const char *, ...);
void	debug(const char *, ...);
void	debug2(const char *, ...);
void	debug3(const char *, ...);
/*
 * Debug levels higher than debug3 are not written to stderr in the
 * slurmstepd process after stderr is connected back to the client (srun).
 */
void	debug4(const char *, ...);
void	debug5(const char *, ...);

void	dump_cleanup_list(void);
void	fatal_add_cleanup(void (*proc) (void *), void *context);
void	fatal_add_cleanup_job(void (*proc) (void *), void *context);
void	fatal_remove_cleanup(void (*proc) (void *context), void *context);
void	fatal_remove_cleanup_job(void (*proc) (void *context), void *context);
void	fatal_cleanup(void);

#endif /* !_LOG_H */

