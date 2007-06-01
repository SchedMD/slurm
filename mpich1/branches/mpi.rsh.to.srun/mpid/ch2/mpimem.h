#ifndef MALLOC

/* This file contains the includes for memory operations.  There are two
   forms here.  The first form simply defines MALLOC/FREE/CALLOC as
   their lower-case versions.  The second form, if MPIR_MEMDEBUG is defined,
   uses the tracing allocation library (mpid/util/tr2.c) instead.
 */
   
#ifdef MPIR_MEMDEBUG
/* tr2.h defines MALLOC/FREE/CALLOC/NEW/STRDUP */
#include "tr2.h"
#define MPIR_trfree MPID_trfree
#define MPIR_trmalloc MPID_trmalloc
#define MPIR_trstrdup MPID_trstrdup
/* Make other uses of malloc/free/calloc illegal */
#ifndef malloc
#define malloc $'Use mpimem.h'$
#define free   $'Use mpimem.h'$
#define calloc $'Use mpimem.h'$
#ifdef strdup
/* Some Linux versions define strdup */
#undef strdup
#endif
#define strdup $'Use mpimem.h'$
#endif
#else
/* We'd like to have a definition for memset etc.  If we can find it ... */
#ifdef STDC_HEADERS
/* Prototype for memset() */
#include <string.h>
#elif defined(HAVE_STRING_H)
#include <string.h>
#elif defined(HAVE_MEMORY_H)
#include <memory.h>
#endif

/* Need to determine how to declare malloc for all systems, some of which
   may need/allow an explicit declaration */
#include <stdlib.h>
#define MALLOC(a) malloc( (unsigned)(a) )
#define FREE   free
#define CALLOC calloc
#define NEW(a) (a *)malloc(sizeof(a))
#define STRDUP(a) strdup(a)
#endif /* MPIR_MEMDEBUG */

#ifndef MEMCPY
#define MEMCPY memcpy
#endif

#endif
