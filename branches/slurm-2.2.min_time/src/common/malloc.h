/* From the autoconf info document, required for AIX */
#if HAVE_CONFIG_H
#  include "config.h"
#  if (!HAVE_MALLOC)
#     undef malloc
      void *malloc ();
#     undef rpl_malloc
      void * rpl_malloc ();
#  endif
#endif
