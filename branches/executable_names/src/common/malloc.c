/* From the autoconf info document, required for AIX */
#if HAVE_CONFIG_H
#  include "config.h"
#  if (!HAVE_MALLOC)

#    include <sys/types.h>

#    undef malloc
     void *malloc ();

     /* Allocate an N-byte block of memory from the heap.
        If N is zero, allocate a 1-byte block.  */

     void * rpl_malloc (size_t n)
     {
	if (n == 0)
		n = 1;
	return malloc (n);
     }

#  endif /*  (!HAVE_MALLOC) */
#endif	/* HAVE_CONFIG_H */
