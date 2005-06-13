/* $Id$ 
 *
 */

#ifndef _HAVE_ENV_H
#define _HAVE_ENV_H

/*
 * stolen from pdsh:
 *
 * Set a variable in the callers environment.  Args are printf style.
 * XXX Space is allocated on the heap and will never be reclaimed.
 * Example: setenvf("MPI_RANK=%d", rank);
 */
int setenvf(const char *fmt, ...);

/*
 * Return the number of elements in the environment array `env'
 */
int envcount (char **env);

#endif /* _HAVE_ENV_H */
