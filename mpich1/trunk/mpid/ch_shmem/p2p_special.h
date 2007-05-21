/* special declarations needed by various machine or OS environments */

/* For POSIX standard versions of Unix */
#if defined(HAVE_UNISTD_H)
#    include <unistd.h>
#endif

/* HP-Convex spp */

#if defined (MPI_cspp)
#   include <sys/cnx_types.h>
#   include "p2pcnx.c"
#ifdef MPID_FLUSH_CACHE
#undef MPID_FLUSH_CACHE
#endif
#   define MPID_FLUSH_CACHE(addr,size) dcache_flush_region(addr, size);
#endif

/* SGI machines and IRIX-based operating systems */

/* MPI_IRIX64, IRIXN32, and IRIX32 chooses (for the most part) same options as 
   MPI_IRIX */
#if defined(MPI_IRIX64) || defined(MPI_IRIXN32) || defined(MPI_IRIX32)
#    define MPI_IRIX
#endif

#if defined(MPI_IRIX)
#    define HAVE_ARENAS
#    define HAVE_USLOCKS
#endif

/* HP and Convex */

#if defined(MPI_HPUX)
#    define HAVE_HPLOCKS
#    define MSEMAPHORE_IS_STRUCT
#endif

/* NEC SX-4 */

#if defined(MPI_SX_4)
#    define HAVE_TSLOCKS
#endif

/* At the moment these are machine independent, since they are used only to
 * keep locks apart, but this might change, hence they are in this file.
 */
#ifndef MPID_CACHE_LINE_SIZE
#    define MPID_CACHE_LINE_SIZE 128
#    define MPID_CACHE_LINE_LOG_SIZE 7
#endif

