
#include <sys/types.h>

#ifndef _STRLCPY_H
#define _STRLCPY_H

/*
 * Copy src to string dst of size siz.  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz == 0).
 * Returns strlen(src); if retval >= siz, truncation occurred.
 */
size_t
strlcpy(char *dst, const char *src, size_t siz);

#endif /* !_STRLCPY_H */


