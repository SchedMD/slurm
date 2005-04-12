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

#include "src/common/log.h"
#include "src/common/macros.h"

/*
 * Define slurm-specific aliases for use by plugins, see slurm_xlator.h 
 * for details. 
 */
void __xassert_failed(char *expr, const char *file, int line, char *func);
strong_alias(__xassert_failed,	slurm_xassert_failed);

void __xassert_failed(char *expr, const char *file, int line, char *func)
{
	error("%s:%d: %s(): Assertion (%s) failed.\n", file, line, func, expr);
	log_flush();
	abort();
}
