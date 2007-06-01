/* The PRIMARY source of this file is acconfig.h */
/* These are needed for ANY declaration that may be made by an AC_DEFINE */

/* Define if Fortran functions are pointers to pointers */
#undef FORTRAN_SPECIAL_FUNCTION_PTR

/* Define is C supports volatile declaration */
#undef HAS_VOLATILE

/* Define if XDR libraries available */
#undef HAS_XDR

/* Define if message catalog programs available */
#undef HAVE_GENCAT

/* Define if getdomainname function available */
#undef HAVE_GETDOMAINNAME

/* Define in gethostbyname function available */
#undef HAVE_GETHOSTBYNAME

/* Define if C has long long int */
#undef HAVE_LONG_LONG_INT

/* Define if C supports long doubles */
/* This is part of the default acconfig.h */
/* #undef HAVE _ LONG _ DOUBLE  */

/* Define if msem_init function available */
#undef HAVE_MSEM_INIT

/* Define if semget works properly */
#undef HAVE_SEMGET

/* Define if C does NOT support const */
#undef HAVE_NO_C_CONST

/* Define if C supports prototypes (but isn't ANSI C) */
#undef HAVE_PROTOTYPES

/* Define if C preprocessor does not accept ## for concatenation */
#undef OLD_STYLE_CPP_CONCAT

/* Define if uname function available */
#undef HAVE_UNAME

/* Define if an int is smaller than void * */
#undef INT_LT_POINTER

/* Define if malloc returns void * (and is an error to return char *) */
#undef MALLOC_RET_VOID

/* Define if MPE extensions are included in MPI libraries */
#undef MPE_USE_EXTENSIONS

/* Define if MPID contains special case code for collective over world */
#undef MPID_COLL_WORLD

/* Define if MPID supports ADI collective */
#undef MPID_USE_ADI_COLLECTIVE

/* Define is ADI should maintain a send queue for debugging */
#undef MPI_KEEP_SEND_QUEUE

/* Define if mpe debug features should NOT be included */
#undef MPI_NO_MPEDBG

/* Define if struct msemaphore rather than msemaphore required */
#undef MSEMAPHORE_IS_STRUCT

/* Define if void * is 8 bytes */
#undef POINTER_64_BITS

/* Define if stdarg can be used */
#undef USE_STDARG

/* Define if oldstyle stdarg (one arg va_start) can be used */
#undef USE_OLDSTYLE_STDARG

/* Define if stdarg.h exists */
#undef HAVE_STDARG_H

/* For Cray, define two word character descriptors in use */
#undef _TWO_WORD_FCD

/* Define if extra traceback information should be kept */
#undef DEBUG_TRACE

/* Define if Fortran is NOT available */
#undef MPID_NO_FORTRAN

/* Define if memory debugging should be enabled */
#undef MPIR_MEMDEBUG

/* Define if object debugging should be enabled */
#undef MPIR_OBJDEBUG

/* Define if ptr conversion debugging should be enabled */
#undef MPIR_PTRDEBUG

/* Define if ADI is ADI-2 (required!) */
#undef MPI_ADI2

/* Define if mmap does not work correctly for anonymous memory */
#undef HAVE_NO_ANON_MMAP

/* Define if signals reset to the default when used (SYSV vs BSD semantics).
   Such signals are essentially un-usable, because of the resulting race
   condition.  The fix is to use the sigaction etc. routines instead (they're
   usually available, since without them signals are entirely useless) */
#undef SIGNALS_RESET_WHEN_USED

/* Define if MPI Structs should align on 2 bytes */
#undef USE_BASIC_TWO_ALIGNMENT

/* Define if MPI Structs should align on 4 bytes */
#undef USE_BASIC_FOUR_ALIGNMENT

/* Define if MPI Structs should align on 8 bytes */
#undef USE_BASIC_EIGHT_ALIGNMENT

/* Define if MPI Structs should align on the largest basic element */
#undef USE_BASIC_ALIGNMENT

/* The number of processors expected on an SMP.  Usually undefined */
#undef PROCESSOR_COUNT

/* Define this to force a choice of shared memory allocator */
#undef SHMEM_PICKED

/* Define this to force SysV shmat for shared memory allocator */
#undef USE_SHMAT

/* Define this to force a choice for memory locking */
#undef LOCKS_PICKED

/* Define this to force SysV semop for locks */
#undef USE_SEMOP

/* Define this if the union semun that is REQUIRED for semctl is NOT 
   defined by ANY system header file */
#undef SEMUN_UNDEFINED

/* Define this if weak symbols are supported */
#undef HAVE_WEAK_SYMBOLS

/* Define this if the weak symbol support is pragma weak */
#undef HAVE_PRAGMA_WEAK

/* Define this if the weak symbol support is pragma _HP_SECONDARY_DEF */
#undef HAVE_PRAGMA_HP_SEC_DEF

/* Define this if the weak symbol support is pragma _CRI duplicate */
#undef HAVE_PRAGMA_CRI_DUP

/* Define this is semctl requires a union semun */
#undef SEMCTL_ARG_UNION

/* The following are for mpid/ch_shmem */
/* Define these for IRIX uslocks */
#undef HAVE_USLOCKS
#undef PREFER_USLOCKS
#undef PREFER_SPINLOCKS
#undef PREFER_ARENAS
#undef HAVE_ARENAS

/* Define these for SX-4 tslocks */
#undef HAVE_TSLOCKS
#undef PREFER_TSLOCKS

/* These provide information for initutil about the configuration options */
#undef CONFIGURE_ARGS_CLEAN
#undef MPIRUN_MACHINE
#undef MPIRUN_DEVICE

/* These enable tracking and output of debugging messages while a program is 
   running */
#undef USE_HOLD_LAST_DEBUG
#undef USE_PRINT_LAST_ON_ERROR

/* Define if there is a routine to print tracebacks */
#undef HAVE_PRINT_BACKTRACE

/* Define for POSIX Clocks.  See the tests to see why these are here */
#undef HAVE_CLOCK_GETTIME
#undef HAVE_CLOCK_GETRES

/* Define if mpid/<device> provided an mpich-mpid.h header file (needed for
   mpid.h) */
#undef HAVE_MPICH_MPID_H
