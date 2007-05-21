/* Define if on AIX 3.
   System headers sometimes define this.
   We just want to avoid a redefinition error message.  */
#ifndef _ALL_SOURCE
#undef _ALL_SOURCE
#endif

/* Define if you don't have vprintf but do have _doprnt.  */
//#define HAVE_DOPRNT 0
#undef HAVE_DOPRNT

/* Define if the `long double' type works.  */
//#undef HAVE_LONG_DOUBLE
#define HAVE_LONG_DOUBLE 1

/* Define if you have the vprintf function.  */
//#undef HAVE_VPRINTF
#define HAVE_VPRINTF 1

/* Define if on MINIX.  */
//#define _MINIX 0
#undef _MINIX

/* Define if the system does not provide POSIX.1 features except
   with this defined.  */
//#define _POSIX_1_SOURCE 0
#undef _POSIX_1_SOURCE

/* Define if you need to in order for stat and other things to work.  */
//#define _POSIX_SOURCE 0
#undef _POSIX_SOURCE

/* Define as the return type of signal handlers (int or void).  */
//#undef RETSIGTYPE
#define RETSIGTYPE void

/* Define if you have the ANSI C header files.  */
#define STDC_HEADERS 1
//#undef STDC_HEADERS

/* Define if your processor stores words with the most significant
   byte first (like Motorola and SPARC, unlike Intel and VAX).  */
//#define WORDS_BIGENDIAN 0
#undef WORDS_BIGENDIAN

/* Define if Fortran functions are pointers to pointers */
//#define FORTRAN_SPECIAL_FUNCTION_PTR 0
#undef FORTRAN_SPECIAL_FUNCTION_PTR

/* Define is C supports volatile declaration */
//#undef HAS_VOLATILE
#define HAS_VOLATILE 1

/* Define if XDR libraries available */
//#define HAS_XDR 0
#undef HAS_XDR

/* Define if message catalog programs available */
//#define HAVE_GENCAT 0
#undef HAVE_GENCAT

/* Define if getdomainname function available */
//#define HAVE_GETDOMAINNAME 0
#undef HAVE_GETDOMAINNAME

/* Define in gethostbyname function available */
//#undef HAVE_GETHOSTBYNAME
#define HAVE_GETHOSTBYNAME

/* Define if C has long long int */
//#define HAVE_LONG_LONG_INT 0 
#undef HAVE_LONG_LONG_INT

/* Define if C supports long doubles */
//#undef HAVE_LONG_DOUBLE 
#define HAVE_LONG_DOUBLE 1

/* Define if msem_init function available */
//#define HAVE_MSEM_INIT 0
#undef HAVE_MSEM_INIT

/* Define if C does NOT support const */
//#define HAVE_NO_C_CONST 0 
#undef HAVE_NO_C_CONST

/* Define if C supports prototypes (but isn't ANSI C) */
//#define HAVE_PROTOTYPES 0
#undef HAVE_PROTOTYPES

/* Define if uname function available */
//#define HAVE_UNAME 0
#undef HAVE_UNAME

/* Define if an int is smaller than void * */
//#define INT_LT_POINTER 0
#undef INT_LT_POINTER

/* Define if malloc returns void * (and is an error to return char *) */
#define MALLOC_RET_VOID
//#undef MALLOC_RET_VOID

/* Define if MPE extensions are included in MPI libraries */
//#define MPE_USE_EXTENSIONS 0
#undef MPE_USE_EXTENSIONS

/* Define if MPID contains special case code for collective over world */
//#define MPID_COLL_WORLD 0 
#undef MPID_COLL_WORLD

/* Define if MPID supports ADI collective */
//#define MPID_USE_ADI_COLLECTIVE 0
#undef MPID_USE_ADI_COLLECTIVE

/* Define is ADI should maintain a send queue for debugging */
//#undef MPI_KEEP_SEND_QUEUE
#define MPI_KEEP_SEND_QUEUE 1

/* Define if mpe debug features should NOT be included */
//#define MPI_NO_MPEDBG 0
#undef MPI_NO_MPEDBG

/* Define if struct msemaphore rather than msemaphore required */
//#define MSEMAPHORE_IS_STRUCT 0
#undef MSEMAPHORE_IS_STRUCT

/* Define if void * is 8 bytes */
//#define POINTER_64_BITS 0
#undef POINTER_64_BITS

/* Define if stdarg can be used */
//#undef USE_STDARG
#define USE_STDARG 1

/* For Cray, define two word character descriptors in use */
//#define _TWO_WORD_FCD 0
#undef _TWO_WORD_FCD

/* Define if extra traceback information should be kept */
//#define DEBUG_TRACE 1
#undef DEBUG_TRACE

/* Define if Fortran is NOT available */
//#undef MPID_NO_FORTRAN
//#define MPID_NO_FORTRAN 1

/* Define if memory debugging should be enabled */
//#define MPIR_MEMDEBUG 0
#undef MPIR_MEMDEBUG

/* Define if object debugging should be enabled */
//#define MPIR_OBJDEBUG 0
#undef MPIR_OBJDEBUG

/* Define if ptr conversion debugging should be enabled */
//#define MPIR_PTRDEBUG 0
#undef MPIR_PTRDEBUG

/* Define if ADI is ADI-2 (required!) */
//#undef MPI_ADI2
#ifndef MPI_ADI2
#define MPI_ADI2 1
#endif

/* Define if mmap does not work correctly for anonymous memory */
//#undef HAVE_NO_ANON_MMAP
#define HAVE_NO_ANON_MMAP 1

/* Define if signals reset to the default when used (SYSV vs BSD semantics).
   Such signals are essentially un-usable, because of the resulting race
   condition.  The fix is to use the sigaction etc. routines instead (they're
   usually available, since without them signals are entirely useless) */
//#define SIGNALS_RESET_WHEN_USED 0
#undef SIGNALS_RESET_WHEN_USED

/* Define if MPI Structs should align on the largest basic element */
//#define USE_BASIC_ALIGNMENT 0
//#undef USE_BASIC_ALIGNMENT
#define USE_BASIC_ALIGNMENT 1

/* The number of processors expected on an SMP.  Usually undefined */
//#define PROCESSOR_COUNT 0
#undef PROCESSOR_COUNT

/* Define this to force a choice of shared memory allocator */
//#define SHMEM_PICKED 0
#undef SHMEM_PICKED

/* Define this to force SysV shmat for shared memory allocator */
//#define USE_SHMAT 0
#undef USE_SHMAT

/* Define this to force a choice for memory locking */
//#define LOCKS_PICKED 0
#undef LOCKS_PICKED

/* Define this to force SysV semop for locks */
//#define USE_SEMOP 0
#undef USE_SEMOP

/* Define if you have BSDgettimeofday.  */
//#define HAVE_BSDGETTIMEOFDAY 0
#undef HAVE_BSDGETTIMEOFDAY

/* Define if you have catclose.  */
//#define HAVE_CATCLOSE 0
#undef HAVE_CATCLOSE

/* Define if you have catgets.  */
//#define HAVE_CATGETS 0
#undef HAVE_CATGETS

/* Define if you have catopen.  */
//#define HAVE_CATOPEN 0
#undef HAVE_CATOPEN

/* Define if you have gethostname.  */
//#undef HAVE_GETHOSTNAME
#define HAVE_GETHOSTNAME 1

/* Define if you have gettimeofday.  */
//#define HAVE_GETTIMEOFDAY 0
#undef HAVE_GETTIMEOFDAY

/* Define if you have mmap.  */
//#define HAVE_MMAP 0
#undef HAVE_MMAP

/* Define if you have mutex_init.  */
//#define HAVE_MUTEX_INIT 0
#undef HAVE_MUTEX_INIT

/* Define if you have nice.  */
//#define HAVE_NICE 0
#undef HAVE_NICE

/* Define if you have semop.  */
//#define HAVE_SEMOP 0
#undef HAVE_SEMOP

/* Define if you have shmat.  */
//#define HAVE_SHMAT 0
#undef HAVE_SHMAT

/* Define if you have sigaction.  */
//#define HAVE_SIGACTION 0
#undef HAVE_SIGACTION

/* Define if you have sigmask.  */
//#define HAVE_SIGMASK 0
#undef HAVE_SIGMASK

/* Define if you have signal.  */
//#undef HAVE_SIGNAL
#define HAVE_SIGNAL 1 

/* Define if you have sigprocmask.  */
//#define HAVE_SIGPROCMASK 0
#undef HAVE_SIGPROCMASK

/* Define if you have sigset.  */
//#define HAVE_SIGSET 0
#undef HAVE_SIGSET

/* Define if you have sysinfo.  */
//#define HAVE_SYSINFO 0
#undef HAVE_SYSINFO

/* Define if you have system.  */
//#undef HAVE_SYSTEM
#define HAVE_SYSTEM 1 

/* Define if you have the <memory.h> header file.  */
//#undef HAVE_MEMORY_H
#define HAVE_MEMORY_H 1

/* Define if you have the <mpproto.h> header file.  */
//#define HAVE_MPPROTO_H 0
#undef HAVE_MPPROTO_H

/* Define if you have the <netdb.h> header file.  */
//#define HAVE_NETDB_H 0
#undef HAVE_NETDB_H

/* Define if you have the <nl_types.h> header file.  */
//#define HAVE_NL_TYPES_H 0
#undef HAVE_NL_TYPES_H

/* Define if you have the <signal.h> header file.  */
//#undef HAVE_SIGNAL_H
#define HAVE_SIGNAL_H 1

/* Define if you have the <stdarg.h> header file.  */
//#undef HAVE_STDARG_H
#define HAVE_STDARG_H 1

/* Define if you have the <stdlib.h> header file.  */
//#undef HAVE_STDLIB_H
#define HAVE_STDLIB_H 1

/* Define if you have the <string.h> header file.  */
//#undef HAVE_STRING_H
#define HAVE_STRING_H 1

/* Define if you have the <sys/systeminfo.h> header file.  */
//#define HAVE_SYS_SYSTEMINFO_H 0
#undef HAVE_SYS_SYSTEMINFO_H

/* Define if you have the <unistd.h> header file.  */
//#define HAVE_UNISTD_H 0
#undef HAVE_UNISTD_H

/* Define if you have the nsl library (-lnsl).  */
//#define HAVE_LIBNSL 0
#undef HAVE_LIBNSL

/* Define if you have the rpc library (-lrpc).  */
//#define HAVE_LIBRPC 0
#undef HAVE_LIBRPC

/* Define if you have the thread library (-lthread).  */
//#define HAVE_LIBTHREAD 0
#undef HAVE_LIBTHREAD


#define HAS_MPIR_ERR_SETMSG
//#define USE_MPI_VERSIONS
