/* $Id$ */

/* safer interface to open()
 */


#if HAVE_CONFIG_H
#  include <config.h>
#endif 

#if HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "safeopen.h"

FILE * safeopen(const char *path, const char *mode, int flags)
{
	int fd;
	int oflags;
	struct stat fb1, fb2;

	if(mode[0] == 'w') {
		oflags = O_WRONLY;
	} else if (mode[0] == 'a') {
		oflags = O_CREAT | O_WRONLY | O_APPEND;
	} else
		oflags = O_RDONLY;

	oflags |= !(flags & SAFEOPEN_NOCREATE)   ? O_CREAT : 0;
	oflags |= (flags & SAFEOPEN_CREATE_ONLY) ? O_EXCL  : 0;

	if ((fd = open(path, oflags, S_IRUSR|S_IWUSR)) < 0)
		return NULL;

	if (!(flags & SAFEOPEN_LINK_OK)) {
		lstat(path, &fb1);
		fstat(fd,   &fb2);

		if (fb2.st_ino != fb1.st_ino) {
			fprintf(stderr, "safeopen(): refusing to open `%s', " 
					"which is a soft link\n", path);
			close(fd);
			return NULL;
		}
	}

	return fdopen(fd, mode);

}
