/* $Id$ */

/* safer interface to open()
 */


#if HAVE_CONFIG_H
#  include "config.h"
#endif 

#if HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "src/common/safeopen.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

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

int
mkdir_parent(const char *path_name, mode_t mode)
{
	char *dir_path, *tmp_ptr;
	int i, rc = 0;

	xassert(path_name);
	xassert(path_name[0] == '/');

	/* copy filename and strip off final element */
	dir_path = xstrdup(path_name);
	tmp_ptr  = strrchr(dir_path, (int)'/');
	tmp_ptr[0] = (char) 0;

	/* now create the parent directories */
	for (i=1; ; i++) {
		if (dir_path[i] == (char) 0) {
			if (mkdir(dir_path, mode) && (errno != EEXIST))
				rc = -1;
			break;
		}
		if (dir_path[i] == '/') {
			dir_path[i] = (char) 0;
			if (mkdir(dir_path, mode) && (errno != EEXIST))
				rc = -1;
			dir_path[i] = '/';
		}
	}

	xfree(dir_path);
	return rc;
}

