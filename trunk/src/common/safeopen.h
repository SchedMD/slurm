/* $Id$ */

/* safer interface to open():
 *
 */

#ifndef _SAFEOPEN_H
#define _SAFEOPEN_H

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

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

/*
 * create parent directories as needed so that a specified 
 * file or directory can later be create
 * path_name IN - path name of the file or directory to be later 
 *                created, only its parents are created
 * mode IN      - permission mode to be used in creating directories
 * RET          - zero on success, -1 on failure with errno set
 */
extern int mkdir_parent(const char *path_name, mode_t mode);

#endif /* _SAFEOPEN_H */
