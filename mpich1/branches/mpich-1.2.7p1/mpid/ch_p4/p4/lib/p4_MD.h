
/* ------------------ Machine Dependent Definitions ---------------- */
/*
        It is important to maintain the order of many of the 
        definitions in this file.
*/


#if defined(SUN_SOLARIS)
#define HP
#undef P4_MACHINE_TYPE
#define P4_MACHINE_TYPE "SUN"
#endif

/*  An I86_SOLARIS is SUN_SOLARIS in every way except P4_MACHINE_TYPE */
#if defined(I86_SOLARIS)
#define SUN_SOLARIS
#define HP
#undef P4_MACHINE_TYPE
#define P4_MACHINE_TYPE "I86_SOLARIS"
#endif

#if defined(ALPHA)
#define DEC5000
#undef P4_MACHINE_TYPE
#define P4_MACHINE_TYPE "DEC5000"
#endif

#if defined(MEIKO_CS2)
#define IPSC860
#undef P4_MACHINE_TYPE
#define P4_MACHINE_TYPE "IPSC860"
#endif

#if defined(SP1)
#define RS6000
#undef P4_MACHINE_TYPE
#define P4_MACHINE_TYPE "RS6000"
/* Some SP batch systems don't get getlogin right */
#define GETLOGIN_BROKEN
#endif

#if defined(SP1_EUI) || defined(SP1_EUIH)
#define RS6000
#undef P4_MACHINE_TYPE
#define P4_MACHINE_TYPE "RS6000"
/* Some SP batch systems don't get getlogin right */
#define GETLOGIN_BROKEN
#endif

#if defined(SGI_MP) || defined(SGI_CH) || defined (SGI_CH64) || \
    defined(SGI_CH32)
#define SGI
#define VENDOR_IPC
#undef P4_MACHINE_TYPE
#define P4_MACHINE_TYPE "SGI"
#endif

/* All SGIs ??? */
#if defined(SGI_CH64) || defined(SGI_MP) || defined(SGI)
/* Create a new session to work around kills that kill the process group */
#define SET_NEW_PGRP
#endif

#if defined(PARAGON)
#define IPSC860
#undef P4_MACHINE_TYPE
#define P4_MACHINE_TYPE "IPSC860"
#endif

#if defined(LINUX)
#define P4_MACHINE_TYPE "LINUX"
/* Linux distributions that use inetutils have versions of rsh that
   process ALL arguments, even those after the command!  This should be
   safe for Linux versions that do not do this, and fix the ones
   that do.  (The fix is to escape the arguments with \\ in front of the
   arg.)
 */
#define HAVE_BROKEN_RSH
#endif

#if defined(LINUX_PPC)
#define P4_MACHINE_TYPE "LINUX_PPC"
/* Otherwise like LINUX */
#ifndef LINUX
#define LINUX
#endif
#endif

#if defined(NETBSD)
#define P4_MACHINE_TYPE "NETBSD"
#endif

#if defined(FREEBSD)
#undef P4_MACHINE_TYPE
#define P4_MACHINE_TYPE "FREEBSD"
#endif

#if defined(FREEBSD_PPC)
#define P4_MACHINE_TYPE "FREEBSD_PPC"
/* Otherwise like FREEBSD */
#ifndef FREEBSD
#define FREEBSD
#endif
#endif

#if defined(CONVEX)
#define SUN
#undef P4_MACHINE_TYPE
#define P4_MACHINE_TYPE "SUN"
#endif

#if defined(TC_2000_TCMP)
#define TC_2000
#define TCMP
#endif

#if defined(FX8)  ||  defined(FX2800)  || defined(FX2800_SWITCH)
#define ALLIANT
#endif

/* Rumor has it LINUX supports VPRINTF */
#if defined(FX2800)  || defined(FX2800_SWITCH) || defined(FREEBSD) || \
    defined(NETBSD)
#define VPRINTF
#endif

#if defined(DELTA) || defined(IPSC860_SOCKETS) || defined(PARAGON)
#define IPSC860
#endif

#if defined(CM5_SOCKETS)
#define CM5
#endif

#if defined(NCUBE_SOCKETS)
#define NCUBE
#endif

#if defined(NEXT)  || defined(KSR) ||  defined(IPSC860)  || defined(NCUBE)
#define GLOBAL
#endif


#if defined(SUN)        || defined(DEC5000)  || defined(LINUX) || \
    defined(NEXT)       || defined(KSR)      || defined(FREEBSD) || \
    defined(SYMMETRY)   || defined(BALANCE)  || defined(NETBSD) || \
    defined(ALLIANT)    || defined(MULTIMAX) ||  defined(CM5) || \
    defined(GP_1000)    || defined(TC_2000)  ||  defined(IBM3090)

#define P4BSD

#endif

#if defined(SUN)        || defined(DEC5000)  || defined(LINUX) || \
    defined(NEXT)       || defined(KSR)      || defined(FREEBSD) || \
    defined(SYMMETRY)   || defined(BALANCE)  || defined(NETBSD) || \
    defined(ALLIANT)    || defined(MULTIMAX) || \
    defined(GP_1000)    || defined(TC_2000)  ||  defined(IBM3090)

#define CAN_DO_SETSOCKOPT

#endif

#if defined(RS6000)          ||                          \
    defined(IPSC860_SOCKETS) ||                          \
    defined(NCUBE_SOCKETS)   ||                          \
    defined(DELTA)           || defined(TITAN)        || \
    defined(SGI)             || defined(CRAY)         || \
    defined(HP)              || defined(SYMMETRY_PTX)

#define CAN_DO_SETSOCKOPT

/* If SGI, select features suggested by SGI */
#ifdef SGI
#define SGI_TEST
#endif

#ifdef NEEDS_NETINET
#include <netinet/in.h>
#endif

#endif


#if defined(RS6000)       || \
    defined(IPSC860)      ||                          \
    defined(NCUBE)        ||                          \
    defined(DELTA)        || defined(TITAN)        || \
    defined(SGI)          || defined(CRAY)         || \
    defined(HP)           || defined(SYMMETRY_PTX) || \
    defined(MEIKO_CS2)

#define P4SYSV

#endif

/* 
   sigaction is more reliable than signal, particularly on POSIX
   systems, where the semantics of signal have CHANGED (to reset to
   the default handler!) 

   Note that the SIGNAL_WITH_OLD_P4 version allows you to get the
   previous handler.  You must supply the equals; this lets
   you do
   SIGNAL_WITH_OLD_P4(SIGFOO,newfcn,oldfcn=(cast))
 */
#if defined(HAVE_SIGACTION)

/* Here is the most reliable version.  Systems that don't provide
   SA_RESETHAND are basically broken at a deep level. */
#if defined(SA_RESETHAND)
#define SIGNAL_WITH_OLD_P4(signame,sigf,oldsigf) {\
struct sigaction oldact;\
sigaction( signame, (struct sigaction *)0, &oldact );\
oldsigf oldact.sa_handler;\
oldact.sa_handler = sigf;\
oldact.sa_flags   = oldact.sa_flags & ~(SA_RESETHAND);\
sigaddset( &oldact.sa_mask, signame );\
sigaction( signame, &oldact, (struct sigaction *)0 );}

#define SIGNAL_P4(signame,sigf) {\
struct sigaction oldact;\
sigaction( signame, (struct sigaction *)0, &oldact );\
p4_CheckSighandler( oldact.sa_handler ); \
oldact.sa_handler = sigf;\
oldact.sa_flags   = oldact.sa_flags & ~(SA_RESETHAND);\
sigaddset( &oldact.sa_mask, signame );\
sigaction( signame, &oldact, (struct sigaction *)0 );}

#else
/* If SA_RESETHAND is not defined, we hope that by masking off the
   signal we're catching that it won't deliver that signal to SIG_DFL
 */
#define SIGNAL_WITH_OLD_P4(signame,sigf,oldsigf) {\
struct sigaction oldact;\
sigaction( signame, (struct sigaction *)0, &oldact );\
oldsigf oldact.sa_handler;\
oldact.sa_handler = sigf;\
sigaddset( &oldact.sa_mask, signame );\
sigaction( signame, &oldact, (struct sigaction *)0 );}

#define SIGNAL_P4(signame,sigf) {\
struct sigaction oldact;\
sigaction( signame, (struct sigaction *)0, &oldact );\
p4_CheckSighandler( oldact.sa_handler ); \
oldact.sa_handler = sigf;\
sigaddset( &oldact.sa_mask, signame );\
sigaction( signame, &oldact, (struct sigaction *)0 );}
#endif

#else
#ifdef P4SYSV
#   ifdef NCUBE
#   define SIGNAL_WITH_OLD_P4(signame,sigf,oldsigf) \
            oldsigf signal(signame,sigf)
#   define SIGNAL_P4(signame,sigf) signal(signame,sigf)
#   else
#   define SIGNAL_WITH_OLD_P4(signame,sigf,oldsigf) \
           oldsigf sigset(signame,sigf)
#   define SIGNAL_P4(signame,sigf) sigset(signame,sigf)
#   endif
#else
#define SIGNAL_WITH_OLD_P4(signame,sigf,oldsigf) oldsigf signal(signame,sigf)
#ifdef CHECK_SIGNALS
#define SIGNAL_P4(signame,sigf) p4_CheckSighandler( signal(signame,sigf) );
#else
#define SIGNAL_P4(signame,sigf) signal(signame,sigf);
#endif
#endif
#endif

#if defined(LINUX) || defined(RS6000) || defined(CRAY) || defined(SUN_SOLARIS)
/* These should actually use sigaction INSTEAD of signal; that's for later.
   This is for systems that make the signals "one-shot", so you can have
   race conditions if you expect your handler to always handle the signals.

   Other systems my need this but I'll add them as I test it.
 */
#define NEED_SIGACTION
#endif

#ifndef P4BOOL
#define P4BOOL int
#endif

#if defined(BALANCE)  ||  defined(FX8)
#define P4VOID int
#else 
#define P4VOID void
#endif


/*----------------- IBM SP-1 with EUI library ------------- */

#if defined(SP1_EUI)

#define NO_TYPE_EUI     0 
#define ACK_REQUEST_EUI 1
#define ACK_REPLY_EUI   2
#define ANY_P4TYPE_EUI  (-1)

#define MYNODE()  eui_mynode

#define CAN_DO_CUBE_MSGS
#define MD_cube_send  MD_eui_send
#define MD_cube_recv  MD_eui_recv
#define MD_cube_msgs_available  MD_eui_msgs_available

#endif

/*----------------- IBM SP-1 with EUI-H library ------------- */

#if defined(SP1_EUIH)

#define NO_TYPE_EUIH     0 
#define ACK_REQUEST_EUIH 1
#define ACK_REPLY_EUIH   2
#define ANY_P4TYPE_EUIH  -1

#define MYNODE()  euih_mynode

#define CAN_DO_CUBE_MSGS
#define MD_cube_send            MD_euih_send
#define MD_cube_recv            MD_euih_recv
#define MD_cube_msgs_available  MD_euih_msgs_available

#endif


/*------------------ Encore Multimax ---------------------- */


#if defined(MULTIMAX)

#include <parallel.h>

#ifndef LINT
typedef LOCK *MD_lock_t;
#define MD_lock_init(l)  *(l) = spin_create(PAR_UNLOCKED);
#define MD_lock(l)       spin_lock(*(l));
#define MD_unlock(l)     spin_unlock(*(l));
#endif

#define GLOBMEMSIZE  (4*1024*1024)
#define CAN_DO_SOCKET_MSGS
#define CAN_DO_XDR
#define CAN_DO_SHMEM_MSGS
#define P4_MAX_MSG_QUEUES 64

#endif


/*------------------ Sequent Balance or Symmetry ---------------------- */


#if !defined(SYMMETRY) && !defined(SYMMETRY_PTX) && !defined(BALANCE)
#define CAN_HANDLE_SIGSEGV
#endif

#if defined(BALANCE) || defined(SYMMETRY) || defined(SYMMETRY_PTX)

#include <parallel/parallel.h>
#if defined(SYMMETRY_PTX)
#include <sys/timers.h>          /* for getclock */
#endif

typedef slock_t MD_lock_t;

#ifndef LINT
#define MD_lock_init(l)  s_init_lock(l);
#define MD_lock(l)       s_lock(l);
#define MD_unlock(l)     s_unlock(l);
#endif
extern char *shmalloc();
#if defined(SYMMETRY_PTX)
extern P4VOID *malloc();
#else
extern char *malloc();
#endif

#define GLOBMEMSIZE  (4*1024*1024)
#define CAN_DO_SOCKET_MSGS
#define CAN_DO_XDR
#define CAN_DO_SHMEM_MSGS
#define P4_MAX_MSG_QUEUES 64

#endif

/*---------------------------- Alliant -------------------------------- */
#if defined(ALLIANT)

typedef char MD_lock_t;

#ifndef LINT
#define MD_lock_init(l)  initialize_lock(l);
#define MD_lock(l)       lock(l);
#define MD_unlock(l)     unlock(l);
#endif
extern char *valloc();

#define GLOBMEMSIZE  (2*1024*1024)

#define USE_XX_SHMALLOC          /* If not defined uses dumb xx_malloc */

#define CAN_DO_SOCKET_MSGS
#define CAN_DO_XDR
#define CAN_DO_SHMEM_MSGS
#define P4_MAX_MSG_QUEUES 64

#endif

#if defined(FX2800_SWITCH)
#include "sw.h"
#define CAN_DO_SWITCH_MSGS
#endif


/*---------------------------- Others -------------------------- */

#if defined(CRAY) || defined(NEXT)
#define GLOBMEMSIZE  (4*1024*1024)
#define CAN_DO_SOCKET_MSGS
#define CAN_DO_XDR
#define P4_MAX_MSG_QUEUES 1
typedef int MD_lock_t;
#define MD_lock_init(l)
#define MD_lock(l)
#define MD_unlock(l)
#endif


#if defined(SUN_SOLARIS)
#include <sys/mman.h>
#include <sys/systeminfo.h>
#include <sys/processor.h>
#include <sys/procset.h>
#include <synch.h>
#if !defined(THREAD_LISTENER)
#define P4_PRECONNECT 1
#endif
#endif 

#if    defined(SUN)     || defined(SGI)  \
    || defined(DEC5000) || defined(LINUX) \
    || defined(RS6000)  || defined(IBM3090) || defined(FREEBSD) \
    || defined(TITAN)   || defined(NETBSD) \
    || defined(HP)

/* Increase P4_SYSV_SHM_SEGSIZE if you need more memory per segment.  
   Not all (few?) systems may support larger segments */
#ifndef P4_SYSV_SHM_SEGSIZE
#    define P4_SYSV_SHM_SEGSIZE (1*1024*1024)
#endif

#    if defined(SYSV_IPC)
/* Increase GLOBMEMSIZE to allow more memory.  Not all systems will be 
   able to allocate large amounts of shared memory */
#ifndef GLOBMEMSIZE
#        define GLOBMEMSIZE  (4*1024*1024)
#endif
#        define CAN_DO_SOCKET_MSGS
#        define CAN_DO_XDR
#        define CAN_DO_SHMEM_MSGS
#        define USE_XX_SHMALLOC
#        define P4_MAX_MSG_QUEUES 64
#        define P4_MAX_SYSV_SHMIDS  256
#        define P4_MAX_SYSV_SEMIDS  256
	 typedef struct { int semid;  int semnum; }   MD_lock_t;
#        include <sys/ipc.h>
#        include <sys/shm.h>
#        include <sys/sem.h>

         static struct sembuf sem_lock[1] = {
	     {  0, -1, 0 }
         };
         static struct sembuf sem_unlock[1] = {
             { 0, 1, 0 }
         };
#    endif

#    if !defined(SYSV_IPC)  &&  !defined(VENDOR_IPC)
/* Increase GLOBMEMSIZE to allow more memory.  Not all systems will be 
   able to allocate large amounts of shared memory */
#ifndef GLOBMEMSIZE
#        define GLOBMEMSIZE  (4*1024*1024)
#endif
#        define CAN_DO_SOCKET_MSGS
#        define CAN_DO_XDR
#        define P4_MAX_MSG_QUEUES 1
	 typedef int MD_lock_t;
#        define MD_lock_init(l)
#        define MD_lock(l)
#        define MD_unlock(l)
#    endif
#endif


#if defined(SUN_SOLARIS)  &&  defined(VENDOR_IPC)

#define CAN_DO_SOCKET_MSGS
#define CAN_DO_XDR
#define P4_MAX_MSG_QUEUES 64
#    define CAN_DO_SHMEM_MSGS
#    define USE_XX_SHMALLOC
/* Increase GLOBMEMSIZE to allow more memory.  Not all systems will be 
   able to allocate large amounts of shared memory */
#ifndef GLOBMEMSIZE
#    define GLOBMEMSIZE  (16*1024*1024)
#endif
     typedef mutex_t MD_lock_t;
#    define MD_lock_init(l) mutex_init(l,USYNC_PROCESS,(P4VOID *)NULL)
#    define MD_lock(l)      mutex_lock(l)
#    define MD_unlock(l)    mutex_unlock(l)
#endif


#if defined(SGI)  &&  defined(VENDOR_IPC)

#define CAN_DO_SOCKET_MSGS
#define CAN_DO_XDR
#define P4_MAX_MSG_QUEUES 32
#    include <ulocks.h>
#    include <malloc.h>
#    define CAN_DO_SHMEM_MSGS
/* Increase GLOBMEMSIZE to allow more memory.  Not all systems will be 
   able to allocate large amounts of shared memory */
#ifndef GLOBMEMSIZE
#    define GLOBMEMSIZE  (16*1024*1024)
#endif
     typedef usema_t *MD_lock_t;
/*   MD_lock_init must be defined in p4_MD.c */
/*   spinlock method */
#    define MD_lock(l) ussetlock(*(l))
#    define MD_unlock(l) usunsetlock(*(l))
/*   semaphore method */
/*****
#    define MD_lock(l)      uspsema(*l)
#    define MD_unlock(l)    usvsema(*l)
*****/
#endif


/* following is for POSIX std versions of Unix */
/*  if defined(SGI)  ||  defined(RS6000)  ||  defined(HP) ||  */
#if defined(HAVE_UNISTD_H)
#include <unistd.h>
#endif

/* Peter Krauss suggested this change (POSIX ?) */
/* Unfortunately, this isn't correct.  The number of open files may
   be different from the size of an fd_set (unfortunate but true), in 
   part because some systems make the fd_set size static but the number of
   open files is (somewhat) dynamic */
#if defined(HP)
#define		getdtablesize()		sysconf(_SC_OPEN_MAX)
#endif



/*---------------------------- IPSC860 Cube --------------------------- */

#if defined(IPSC860)

#    if defined(DELTA)
#        define P4_MAX_CUBE_MSGS_OUT 5
#    else
#        define P4_MAX_CUBE_MSGS_OUT 5
#    endif

#define MD_cube_send  MD_i860_send
#define MD_cube_recv  MD_i860_recv
#define MD_cube_msgs_available  MD_i860_msgs_available

typedef int MD_lock_t;

#if defined(IPSC860)
#define MYNODE mynode
#endif

#ifndef LINT
#define MD_lock_init(l)
#define MD_lock(l)
#define MD_unlock(l)
#endif

/* Increase GLOBMEMSIZE to allow more memory.  Not all systems will be 
   able to allocate large amounts of shared memory */
#ifndef GLOBMEMSIZE
#define GLOBMEMSIZE  (1*1024*1024)
#endif
#define CAN_DO_CUBE_MSGS
#define P4_MAX_MSG_QUEUES 1

#define ALL_NODES -1

#define NO_TYPE_IPSC     0 
#define ACK_REQUEST_IPSC 1
#define ACK_REPLY_IPSC   2
#if defined(MEIKO_CS2)
#define ANY_P4TYPE_IPSC    -1
#else
#define ANY_P4TYPE_IPSC    0x80000007
#endif

#define NODE_PID 0

#if defined(IPSC860_SOCKETS)
#define CAN_DO_SOCKET_MSGS
/*****
#include <CMC/sys/types.h>
#include <CMC/sys/socket.h>
#include <CMC/netinet/in.h>
#include <CMC/netdb.h>
*****/
#include <CMC/ntoh.h>
#endif

#endif    

/*---------------------------- CM-5 --------------------------- */

#if defined(CM5)

#include <cm/cmmd.h>
/* #include <cm/cmmd-io.h> */

typedef int MD_lock_t;

#if defined(CM5)
#define MYNODE CMMD_self_address
#endif

#ifndef LINT
#define MD_lock_init(l)
#define MD_lock(l)
#define MD_unlock(l)
#endif

/* Increase GLOBMEMSIZE to allow more memory.  Not all systems will be 
   able to allocate large amounts of shared memory */
#ifndef GLOBMEMSIZE
#define GLOBMEMSIZE  (1*1024*1024)
#endif
#define CAN_DO_CUBE_MSGS
#define P4_MAX_MSG_QUEUES 1

#define NO_TYPE_CM5     0 
#define ACK_REQUEST_CM5 1
#define ACK_REPLY_CM5   2
#define ANY_P4TYPE_CM5    CMMD_ANY_TAG

#define MD_cube_send  MD_CM5_send
#define MD_cube_recv  MD_CM5_recv
#define MD_cube_msgs_available  MD_CM5_msgs_available

#endif    


/*---------------------------- NCUBE --------------------------- */

#if defined(NCUBE)

typedef int MD_lock_t;

#include <sysn.h> 

#define MYNODE npid

#ifndef LINT
#define MD_lock_init(l)
#define MD_lock(l)
#define MD_unlock(l)
#endif

/* Increase GLOBMEMSIZE to allow more memory.  Not all systems will be 
   able to allocate large amounts of shared memory */
#ifndef GLOBMEMSIZE
#define GLOBMEMSIZE  (1*1024*1024)
#endif
#define CAN_DO_CUBE_MSGS
#define P4_MAX_MSG_QUEUES 1

#define NO_TYPE_NCUBE     0 
#define ACK_REQUEST_NCUBE 1
#define ACK_REPLY_NCUBE   2
#define ANY_P4TYPE_NCUBE  (-1)

#define NCUBE_ANY_NODE  (-1)
#define NCUBE_ANY_TAG   (-1)

#define MD_cube_send  MD_NCUBE_send
#define MD_cube_recv  MD_NCUBE_recv
#define MD_cube_msgs_available  MD_NCUBE_msgs_available

#endif    

/*----------------   KSR             -------------------------*/
#if defined(KSR)
#include <sys/mman.h>
#include <pthread.h>

#define USE_XX_SHMALLOC
/* Increase GLOBMEMSIZE to allow more memory.  Not all systems will be 
   able to allocate large amounts of shared memory */
#ifndef GLOBMEMSIZE
#define GLOBMEMSIZE  (16*1024*1024)
#endif
#define P4_MAX_MSG_QUEUES 64
#define CAN_DO_SOCKET_MSGS
#define CAN_DO_XDR
#define CAN_DO_SHMEM_MSGS

#define MD_lock_t       msemaphore
#define MD_lock_init(l) msem_init(l, MSEM_UNLOCKED)
#define MD_lock(l)      msem_lock(l, 0)
#define MD_unlock(l )   msem_unlock(l, 0)

#endif



/*------------------ Butterfly TC-2000/GP-1000 -------------- */
#if defined(TC_2000)  ||  defined(GP_1000)
#include <mach.h>    
#include <sys/cluster.h>
#include <sys/kern_return.h>
#include <heap.h>

char *xx_malloc();
P4VOID MD_malloc_hint();

#ifdef MALLOC_STATS
static unsigned int allocated = 0;
#endif

#define MD_lock_t       int
#ifndef LINT
#define MD_lock_init(l) simple_unlock(l)
#define MD_lock(l)      simple_lock(l)
#define MD_unlock(l)    simple_unlock(l)
#endif

/* Increase GLOBMEMSIZE to allow more memory.  Not all systems will be 
   able to allocate large amounts of shared memory */
#ifndef GLOBMEMSIZE
#define GLOBMEMSIZE  (8*1024*1024)
#endif
#define CAN_DO_SOCKET_MSGS
#define CAN_DO_XDR
#define CAN_DO_SHMEM_MSGS
#define P4_MAX_MSG_QUEUES 128

#endif

#ifdef TCMP
#define CAN_DO_TCMP_MSGS
/* #include </Net/sparky/sparky1/lusk/lepido/tcmp/tcmp.h> */
#include </usr/bbnm/tcmp/tcmp.h>
#endif

/* Some systems don't include XDR */
#if !defined(HAVE_XDRMEM_CREATE) || !defined(HAS_XDR)
#undef CAN_DO_XDR
#endif

/* ----------------- Thread definitions ------------------- */
#if defined(CYGWIN32_NT)
typedef HANDLE p4_thread_t;
/* The threadid appears useless; the HANDLE return value is used
   to manipulate the thread */
static LPDWORD threadid;
#define p4_create_thread(threadhandle,routine,args) \
    threadhandle = CreateThread(NULL,\
		      0,\
		      (LPTHREAD_START_ROUTINE) routine,\
		      (LPVOID) args,  /* arbitrary argument */\
		      0,    /* runs now; could use CREATE_SUSPENDED */\
		      (LPDWORD) &threadid)
#elif defined(USE_PTHREADS)
#include <pthread.h>
typedef pthread_t p4_thread_t;
#define p4_create_thread(threadhandle,routine,args) \
    pthread_create( &threadhandle, NULL, (void*(*)(void*))routine, (void *)args )
#else
/* No threads */
#endif
/* ----------------- Can be made machine dependent -------------------*/

typedef unsigned long p4_usc_time_t;

/* Bill says take this out, 12/22/94
extern P4VOID exit (int);
*/

#define P4_MAXPROCS 1024

/* For sysinfo */
#if defined(SUN_SOLARIS) || defined(MEIKO_CS2)
#include <sys/systeminfo.h>
#endif

/* Note that defining MEMDEBUG fails on HPs unless the ANSI option is 
   selected in the compiler */
/* #define MEMDEBUG */

#if defined(MEMDEBUG)

#ifndef LINT
#define  P4_INCLUDED
/* For this include to work, the include path must include the ch_p4mpd
   directory */
#include "tr2.h"
#define p4_malloc(size) MALLOC(size)
#define p4_free(p) FREE(p)
#define p4_clock MD_clock
#endif

#else

#ifndef LINT
#define p4_malloc malloc
#define p4_free free
#define p4_clock MD_clock
#endif

#endif
