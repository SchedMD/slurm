/* 
 *
 * env.c : environment manipulation 
 */

#include <stdio.h>	/* BUFSIZ */
#include <stdarg.h>	/* va_*	  */
#include <string.h>	/* strdup */
#include <stdlib.h>	/* putenv */

/*
 * setenvf() (stolen from pdsh)
 *
 * Set a variable in the callers environment.  Args are printf style.
 * XXX Space is allocated on the heap and will never be reclaimed.
 * Example: setenvf("RMS_RANK=%d", rank);
 */
int
setenvf(const char *fmt, ...)
{
	va_list ap;
	char buf[BUFSIZ];
	char *bufcpy;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	bufcpy = strdup(buf);
	if (bufcpy == NULL)
		return -1;
	return putenv(bufcpy);
}

