/* $Id$ */

/* safer interface to open():
 *
 */

#ifndef _SAFEOPEN_H
#define _SAFEOPEN_H

/* safeopen flags:
 *
 * default is to create if needed, and fail if path is a soft link
 */
#define SAFEOPEN_LINK_OK	(1<<0) 	/* do not check for soft link	*/
#define SAFEOPEN_CREATE_ONLY	(1<<1)  /* create, fail if file exists	*/
#define SAFEOPEN_NOCREATE	(1<<2)	/* fail if file doesn't exist	*/

/* open a file for read, write, or append
 * perform some simple sanity checks on file and return stream pointer
 */
FILE *safeopen(const char *path, const char *mode, int flags);

#endif /* _SAFEOPEN_H */
