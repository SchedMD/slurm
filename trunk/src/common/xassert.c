/* $Id$ */

/* 
** xassert: replacement for assert which sends error to log instead of stderr
**
*/

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>

#include <src/common/log.h>

void __xassert_failed(char *expr, const char *file, int line, char *func)
{
	error("%s:%d: %s(): Assertion (%s) failed.\n", file, line, func, expr);
	log_flush();
	abort();
}
