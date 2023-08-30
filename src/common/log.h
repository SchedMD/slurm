/*****************************************************************************\
 *  log.h - configurable logging for slurm: log to file, stderr and/or syslog.
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

#ifndef _LOG_H
#define _LOG_H

#include <syslog.h>
#include <stdarg.h>
#include <stdio.h>

#include "slurm/slurm.h"
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

typedef enum {
	LOG_FILE_FMT_TIMESTAMP = 0,
	LOG_FILE_FMT_JSON,
} log_file_fmt_t;

/*
 * log options: Each of stderr, syslog, and logfile can have a different level
 */
typedef struct {
	log_level_t stderr_level;   /* max level to log to stderr            */
	log_level_t syslog_level;   /* max level to log to syslog            */
	log_level_t logfile_level;  /* max level to log to logfile           */
	bool prefix_level;          /* prefix level (e.g. "debug: ") if true */
	bool buffered;              /* use internal buffer to never block    */
	bool raw;                   /* output is to a raw terminal           */
	log_file_fmt_t logfile_fmt; /* format of logfile output */
} 	log_options_t;

extern char *slurm_prog_name;

/* some useful initializers for log_options_t
 */
#define LOG_OPTS_INITIALIZER	\
	{ LOG_LEVEL_INFO, LOG_LEVEL_INFO, LOG_LEVEL_INFO, \
		1, 0, 0, LOG_FILE_FMT_TIMESTAMP }

#define LOG_OPTS_SYSLOG_DEFAULT	\
	{ LOG_LEVEL_QUIET, LOG_LEVEL_INFO, LOG_LEVEL_QUIET, \
		1, 0, 0, LOG_FILE_FMT_TIMESTAMP }

#define LOG_OPTS_STDERR_ONLY	\
	{ LOG_LEVEL_INFO,  LOG_LEVEL_QUIET, LOG_LEVEL_QUIET, \
		1, 0, 0, LOG_FILE_FMT_TIMESTAMP }

#define SCHEDLOG_OPTS_INITIALIZER	\
	{ LOG_LEVEL_QUIET, LOG_LEVEL_QUIET, LOG_LEVEL_QUIET, \
		0, 1, 0, LOG_FILE_FMT_TIMESTAMP }


/* Functions for filling in a char buffer with a timestamp. */
size_t rfc2822_timestamp(char *, size_t);
size_t log_timestamp(char *, size_t);


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

/*
 * initialize scheduler log module (called only once)
 */
int sched_log_init(char *argv0, log_options_t opts, log_facility_t fac,
		   char *logfile);

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

/*
 * Close scheduler log and free associated memory
 */
void sched_log_fini(void);

/* Alter log facility, options are like log_init() above, except that
 * an argv0 argument is not passed.
 *
 * This function may be called multiple times.
 */
int log_alter(log_options_t opts, log_facility_t fac, char *logfile);

/* Alter log facility, options are like log_alter() above, except that
 * an the file pointer is sent in instead of a filename.
 *
 * This function may only be called once.
 */
int log_alter_with_fp(log_options_t opt, log_facility_t fac, FILE *fp_in);

/* Sched alter log facility, options are like sched_log_init() above,
 * except that an argv0 argument is not passed.
 *
 * This function may be called multiple times.
 */
int sched_log_alter(log_options_t opts, log_facility_t fac, char *logfile);

/* Set prefix for log file entries
 * Note: will store pfx internally, do not use after this call.
 */
void log_set_prefix(char **pfx);

/*
 * (re)set argv0 string prepended to all log messages
 */
void log_set_argv0(char *pfx);

/* Return the FILE * of the current logfile (or stderr if not logging to
 * a file, but NOT both). Also see log_oom() below. */
FILE *log_fp(void);

/* Log out of memory without message buffering */
void log_oom(const char *file, int line, const char *func);

/* Set the log timestamp format */
void log_set_timefmt(unsigned);

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

/* log_set_debug_flags()
 * Set or reset the debug flags based on the configuration
 * file or the scontrol command.
 */
extern void log_set_debug_flags(void);

/* Return the highest LOG_LEVEL_* used for any logging mechanism.
 * For example, if LOG_LEVEL_INFO is returned, we know that all verbose and
 * debug type messages will be ignored. */
extern int get_log_level(void);

/*
 * Returns the greater of the sched_log_level or the log_level.
 */
extern int get_sched_log_level(void);


#define STEP_ID_FLAG_NONE      0x0000
#define STEP_ID_FLAG_PS        0x0001
#define STEP_ID_FLAG_NO_JOB    0x0002
#define STEP_ID_FLAG_NO_PREFIX 0x0004
#define STEP_ID_FLAG_SPACE     0x0008

/*
 * log_build_step_id_str() - print a slurm_step_id_t as " StepId=...", with
 * Batch and Extern used as appropriate.
 */
extern char *log_build_step_id_str(
	slurm_step_id_t *step_id, char *buf, int buf_size, uint16_t flags);

/*
 * the following log a message to the log facility at the appropriate level:
 *
 * Messages do not need a newline!
 *
 * args are printf style with the following exceptions:
 * %m expands to strerror(errno)
 * %M expand to time stamp, format is configuration dependent
 * %pJ expands to "JobId=XXXX" for the given job_ptr, with the appropriate
 *     format for job arrays and hetjob components.
 * %pS expands to "JobId=XXXX StepId=YYYY" for a given step_ptr.
 * %t expands to strftime("%x %X") [ locally preferred short date/time ]
 * %T expands to rfc2822 date time  [ "dd, Mon yyyy hh:mm:ss GMT offset" ]
 */

/*
 * return a heap allocated string formed from fmt and ap arglist
 * returned string is allocated with xmalloc, so must free with xfree.
 *
 * args are like printf, with the addition of the following format chars:
 * - %m expands to strerror(errno)
 * - %M expand to time stamp, format is configuration dependent
 * - %pA expands to "AAA.BBB.CCC.DDD:XXXX" for the given slurm_addr_t.
 * - %pJ expands to "JobId=XXXX" for the given job_ptr, with the appropriate
 *       format for job arrays and hetjob components.
 * - %pS expands to "JobId=XXXX StepId=YYYY" for a given step_ptr.
 * - %t expands to strftime("%x %X") [ locally preferred short date/time ]
 * - %T expands to rfc2822 date time [ "dd, Mon yyyy hh:mm:ss GMT offset" ]
 *
 * these formats are expanded first, leaving all others to be passed to
 * vsnprintf() to complete the expansion using the ap arglist.
 */
extern char *vxstrfmt(const char *fmt, va_list ap);

/*
 * fatal() exits program
 * error() returns SLURM_ERROR
 */
void	log_var(const log_level_t, const char *, ...)
			__attribute__ ((format (printf, 2, 3)));
void	sched_log_var(const log_level_t, const char *, ...)
			__attribute__ ((format (printf, 2, 3)));
extern void fatal_abort(const char *, ...)
	__attribute__((format (printf, 1, 2))) __attribute__((noreturn));
extern void fatal(const char *, ...)
	__attribute__((format (printf, 1, 2))) __attribute__((noreturn));
int	error(const char *, ...) __attribute__ ((format (printf, 1, 2)));
void	warning(const char *, ...) __attribute__ ((format (printf, 1, 2)));
void	slurm_info(const char *, ...) __attribute__ ((format (printf, 1, 2)));
void	slurm_verbose(const char *, ...) __attribute__ ((format (printf, 1, 2)));

extern const char plugin_type[];

#ifdef SLURM_PLUGIN_DEBUG
/*
 * Print plugins with the plugin_type and func
 */
#define format_print(l, fmt, ...)				\
	do {							\
		if (get_log_level() >= l)			\
			log_var(l, "%s: %s: "fmt, plugin_type,	\
				__func__, ##__VA_ARGS__);	\
	} while (0)
#else
/*
 * Normal log messages
 */
#define format_print(l, fmt, ...)			\
	do {						\
		if (get_log_level() >= l)		\
			log_var(l, fmt, ##__VA_ARGS__);	\
	} while (0)
#endif //SLURM_PLUGIN_DEBUG

#define info(fmt, ...)		\
	do {			\
	format_print(LOG_LEVEL_INFO, fmt, ##__VA_ARGS__);\
	} while (0)

#define verbose(fmt, ...)	\
	do {			\
	format_print(LOG_LEVEL_VERBOSE, fmt, ##__VA_ARGS__);\
	} while (0)

#define debug(fmt, ...)		\
	do {			\
	format_print(LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__);\
	} while (0)

#define debug2(fmt, ...)	\
	do {			\
	format_print(LOG_LEVEL_DEBUG2, fmt, ##__VA_ARGS__);\
	} while (0)

#define debug3(fmt, ...)	\
	do {			\
	format_print(LOG_LEVEL_DEBUG3, fmt, ##__VA_ARGS__);\
	} while (0)

#define debug4(fmt, ...)	\
	do {			\
	format_print(LOG_LEVEL_DEBUG4, fmt, ##__VA_ARGS__);\
	} while (0)

#define debug5(fmt, ...)	\
	do {			\
	format_print(LOG_LEVEL_DEBUG5, fmt, ##__VA_ARGS__);\
	} while (0)


/*
 * Like above logging messages, but prepend "sched: " to the log entry
 * and route the message into the sched_log if enabled.
 */
int	sched_error(const char *, ...) __attribute__ ((format (printf, 1, 2)));
void	sched_info(const char *, ...) __attribute__ ((format (printf, 1, 2)));
void	sched_verbose(const char *, ...) __attribute__ ((format (printf, 1, 2)));
#define sched_debug(fmt, ...)						\
	do {								\
		if (get_sched_log_level() >= LOG_LEVEL_DEBUG)		\
			sched_log_var(LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__); \
	} while (0)
#define sched_debug2(fmt, ...)						\
	do {								\
		if (get_sched_log_level() >= LOG_LEVEL_DEBUG2)		\
			sched_log_var(LOG_LEVEL_DEBUG2, fmt, ##__VA_ARGS__); \
	} while (0)
#define sched_debug3(fmt, ...)						\
	do {								\
		if (get_sched_log_level() >= LOG_LEVEL_DEBUG3)		\
			sched_log_var(LOG_LEVEL_DEBUG3, fmt, ##__VA_ARGS__); \
	} while (0)

/*
 * Print at the same log level as error(), but without prefixing the message
 * with "error: ". Useful to report back to srun commands from SPANK plugins,
 * as info() will only go to the logs.
 */
void spank_log(const char *, ...) __attribute__ ((format (printf, 1, 2)));


/*
 * Used to print log messages only when a specific DEBUG_FLAG_* option has
 * been enabled. Automatically prepends 'DEBUG_FLAG_' to the flag option name
 * to save space. E.g., to print a message only when DEBUG_FLAG_STEPS is
 * enabled, call `log_flag(STEPS, "%s: my important message", __func__);`.
 *
 * As this is implemented in a macro, this is no slower than the equivalent
 * conditional check.
 */
#define log_flag(flag, fmt, ...)					\
	do {								\
		if (slurm_conf.debug_flags & DEBUG_FLAG_##flag)		\
			format_print(LOG_LEVEL_VERBOSE, #flag ": " fmt,	\
				     ##__VA_ARGS__);			\
	} while (0)

/*
 * Log data as hex dump (use log_flag_hex() instead)
 * IN data - ptr to data
 * IN len - number of bytes pointed by data
 * IN fmt - message to prepend to hex dump
 */
extern void _log_flag_hex(const void *data, size_t len, const char *fmt, ...);

/*
 * Log data as hex dump
 * IN data - ptr to data
 * IN len - number of bytes pointed by data
 * IN fmt - message to prepend to hex dump
 */
#define log_flag_hex(flag, data, len, fmt, ...)                  \
	do {                                                     \
		if (slurm_conf.debug_flags & DEBUG_FLAG_##flag)  \
			_log_flag_hex(data, len, #flag ": " fmt, \
				      ##__VA_ARGS__);            \
	} while (0)

#endif /* !_LOG_H */
