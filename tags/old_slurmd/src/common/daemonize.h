/* src/common/daemonize.h
 * $Id$
 */

#ifndef _HAVE_DAEMONIZE_H
#define _HAVE_DAEMONIZE_H

/* fork process into background and inherit new session
 * if nochdir is 0, performs a chdir("/")
 * if noclose is 0, closes all fds and dups stdout/err of daemon onto /dev/null
 *
 * returns -1 on error.
 */
int daemon(int nochdir, int noclose);

#endif /* !_HAVE_DAEMONIZE_H */
