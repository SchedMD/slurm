/* $Id$ */

/* configurable logging for slurm - 
 *   log to file, stderr, syslog, or all.
 */

#ifndef _LOG_H
#define _LOG_H

#include <syslog.h> 	
#include "macros.h"

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
	LOG_LEVEL_QUIET,
	LOG_LEVEL_FATAL,
	LOG_LEVEL_ERROR,
	LOG_LEVEL_INFO,
	LOG_LEVEL_VERBOSE,
	LOG_LEVEL_DEBUG,
	LOG_LEVEL_DEBUG2,
	LOG_LEVEL_DEBUG3,
}	log_level_t;


/*
 * log options: Each of stderr, syslog, and logfile can have a different level
 */
typedef struct {
	unsigned    prefix_level;   /* prefix level (e.g. "debug: ") if 1 */
	log_level_t stderr_level;   /* max level to log to stderr         */
	log_level_t syslog_level;   /* max level to log to syslog         */
	log_level_t logfile_level;  /* max level to log to logfile        */
} 	log_options_t;

/* some useful initializers for log_options_t
 */
#define LOG_OPTS_INITIALIZER   \
    { 1, LOG_LEVEL_QUIET, LOG_LEVEL_QUIET, LOG_LEVEL_QUIET }

#define LOG_OPTS_SYSLOG_DEFAULT \
    { 1, LOG_LEVEL_QUIET, LOG_LEVEL_INFO, LOG_LEVEL_QUIET }  

#define LOG_OPTS_STDERR_ONLY	\
    { 1, LOG_LEVEL_INFO,  LOG_LEVEL_QUIET, LOG_LEVEL_QUIET }

/* 
 * initialize/reinitialize log module (may be called multiple times)
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
 */
int log_init(char *argv0, log_options_t opts, 
              log_facility_t fac, char *logfile);

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
 */
void	fatal(const char *, ...);
void	error(const char *, ...);
void	info(const char *, ...);
void	verbose(const char *, ...);
void	debug(const char *, ...);
void	debug2(const char *, ...);
void	debug3(const char *, ...);

#endif /* !_LOG_H */

