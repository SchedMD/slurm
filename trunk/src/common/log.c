/* $Id */

/* 
** log facilities for slurm.
**
** Mark Grondona <mgrondona@llnl.gov>
**
*/

/*
** MT safe
*/

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>

#if HAVE_STRING_H
#  include <string.h>
#endif

#include <stdarg.h>
#include <errno.h>
#include <syslog.h>
#include <pthread.h>

#if HAVE_STDLIB_H
#  include <stdlib.h>	/* for abort() */
#endif

#include <src/common/macros.h>
#include <src/common/log.h>
#include <src/common/xmalloc.h>
#include <src/common/xassert.h>
#include <src/common/xstring.h>

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
static pthread_mutex_t  log_lock = PTHREAD_MUTEX_INITIALIZER;    
static log_t            *log = NULL;

#define SYSLOG_LEVEL    log->opt.syslog_level
#define STDERR_LEVEL    log->opt.stderr_level
#define LOGFILE_LEVEL   log->opt.logfile_level

#define LOG_INITIALIZED ((log != NULL) && log->initialized)

/*
 * Initialize log with 
 * prog = program name to tag error messages with
 * opt  = log_options_t specifying max log levels for syslog, stderr, and file
 * fac  = log facility for syslog (unused if syslog level == LOG_QUIET)
 * logfile =
 *        logfile name if logfile level > LOG_QUIET
 */
int log_init(char *prog, log_options_t opt, log_facility_t fac, char *logfile )
{
	int rc = 0;
	char *errmsg;

	pthread_mutex_lock(&log_lock);

	if (log) {
		if (log->argv0) 
			xfree(log->argv0);
	} else {
		log = (log_t *)xmalloc(sizeof(log_t));
	}

	log->logfp = NULL;

	log->argv0 = xstrdup(prog);

	log->opt = opt;

	if (SYSLOG_LEVEL > LOG_LEVEL_QUIET)
		log->facility = fac;

	if (LOGFILE_LEVEL > LOG_LEVEL_QUIET) {
		log->logfp = fopen(logfile, "a");
		if (!log->logfp) {
			xstrerrorcat(errmsg);
			fprintf(stderr, "%s: log_init(): Unable to open logfile"
					"`%s': %s", prog, logfile, errmsg);
			xfree(errmsg);
			rc = errno;
		}
	}

	log->initialized = 1;

	pthread_mutex_unlock(&log_lock);

	return rc;
}


/* retun a heap allocated string formed from fmt and ap arglist
 * returned string is allocated with xmalloc, so must free with xfree.
 * 
 * args are like printf, with the addition of the following format chars:
 * - %m expands to strerror(errno)
 * - %t expands to strftime("%x %X") [ locally preferred short date/time ]
 * - %T expands to rfc822 date time  [ "dd Mon yyyy hh:mm:ss GMT offset" ]
 *
 */
static char *vxstrfmt(const char *fmt, va_list ap)
{
	char        *buf = NULL;
	char        *p;
	size_t      len = (size_t) 0;
	char        tmp[LINEBUFSIZE];
	char        tmp2[LINEBUFSIZE];


	while (*fmt != '\0') {

		if ((p = strchr(fmt, '%')) == NULL) {  /* no more format chars */
			xstrcat(buf, fmt);
			return buf;

		} else {        /* *p == '%' */

			/* take difference from fmt to just before `%' */
			len = (size_t) ((int)(p) - (int)fmt);

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
				xstrerrorcat(buf);
				break;

			case 't': 	/* "%t" => locally preferred date/time*/ 
				xstrftimecat(buf, "%x %X");
				break;
			case 'T': 	/* "%T" => "dd Mon yyyy hh:mm:ss off" */
				xstrftimecat(buf, "%a %d %b %Y %H:%M:%S %z");   
				break;

			case 's':	/* "%s" => append string   */
				xstrcat(buf, va_arg(ap, char *));
				break;

			default:	/* try to handle the rest  */

				/* back up to `%' char */
				p--;

				/* take len just after this '%' to next '%' */
				len = (size_t) ((int)strchr(p+1, '%') - (int)p);

				if ((int)len <= 0)
					len = strlen(p);

				memcpy(tmp, p, len);
				tmp[len] = '\0';

				snprintf(tmp2, sizeof(tmp2), tmp, 
					 va_arg(ap, char *));
				xstrcat(buf, tmp2);

				p+=len-1;
				break;
			}

		}

		fmt = p + 1;
	}

	return buf;
}

/*
 * concatenate result of xstrfmt() to dst, expanding dst if necessary
 */
static void xstrfmtcat(char **dst, const char *fmt, ...)
{
	va_list ap;
	char *buf;

	va_start(ap, fmt);
	buf = vxstrfmt(fmt, ap);
	va_end(ap);

	xstrcat(*dst, buf);

	xfree(buf);

}

/*
 * log a message at the specified level to facilities that have been 
 * configured to recieve messages at that level
 */
static void log_msg(log_level_t level, const char *fmt, va_list args)
{
	char *pfx = "";
	char *buf = NULL;
	char *msgbuf = NULL;
	int priority = LOG_INFO;

	pthread_mutex_lock(&log_lock);

	if (!LOG_INITIALIZED) {
		log_options_t opts = LOG_OPTS_STDERR_ONLY;

		pthread_mutex_unlock(&log_lock);
		log_init("", opts, 0, NULL);
		pthread_mutex_lock(&log_lock);

	}

	if (level > SYSLOG_LEVEL  && 
	    level > LOGFILE_LEVEL && 
	    level > STDERR_LEVEL)
		return;

	if (log->opt.prefix_level || SYSLOG_LEVEL > level) {
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

	if (level <= STDERR_LEVEL) {
		fflush(stdout);
		fprintf(stderr, "%s: %s%s\n", log->argv0, pfx, buf);
		fflush(stderr);
	}

	if (level <= LOGFILE_LEVEL && log->logfp != NULL) {
		xstrfmtcat(&msgbuf, "[%T] %s%s", pfx, buf);

		fprintf(log->logfp, "%s\n", msgbuf);
		fflush(log->logfp);

		xfree(msgbuf);
	}

	if (level <=  SYSLOG_LEVEL) {
		xstrfmtcat(&msgbuf, "%s%s", pfx, buf);

		openlog(log->argv0, LOG_PID, log->facility);
		syslog(priority, "%.500s", msgbuf);
		closelog();

		xfree(msgbuf);
	}

	pthread_mutex_unlock(&log_lock);

	xfree(buf);
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
