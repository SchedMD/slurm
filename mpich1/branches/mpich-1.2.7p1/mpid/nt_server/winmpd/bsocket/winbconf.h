/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#ifndef MPICH2WINCONF_H
#define MPICH2WINCONF_H

/* Define to empty if the keyword does not work.  */
/* #undef const */

/* Define if you don't have vprintf but do have _doprnt.  */
/* #undef HAVE_DOPRNT */

/* Define if you have <sys/wait.h> that is POSIX.1 compatible.  */
#undef HAVE_SYS_WAIT_H

/* Define if you have the vprintf function.  */
#define HAVE_VPRINTF 1

/* Define as __inline if that's what the C compiler calls it.  */
#define inline __inline

/* Define to `int' if <sys/types.h> doesn't define.  */
/* #undef pid_t */

/* Define if you have the ANSI C header files.  */
#define STDC_HEADERS 1

/* Define if you can safely include both <sys/time.h> and <time.h>.  */
#undef TIME_WITH_SYS_TIME

/* Define if configured for TCP */
#define TCP

/* Define if configured for shmem */
#define SHM

/* Define if configured for via */
#define VIA

/* Define if configured for via rdma */
#define VIA_RDMA

/* Define if profiling implementation of mpich2 */
/* #undef USE_MPID_PROFILING */

/* Define if configured for using busy locks */
#define USE_BUSY_LOCKS 1

/* Define if you have the getcwd function.  */
#undef HAVE_GETCWD

/* Define if you have the gethostname function.  */
#define HAVE_GETHOSTNAME 1

/* Define if you have the gettimeofday function.  */
#undef HAVE_GETTIMEOFDAY

/* Define if you have the putenv function.  */
#define HAVE_PUTENV 1

/* Define if you have the select function.  */
#define HAVE_SELECT 1

/* Define if you have the socket function.  */
#define HAVE_SOCKET 1

/* Define if you have the <fcntl.h> header file.  */
#undef HAVE_FCNTL_H

/* Define if you have the <strings.h> header file.  */
#define HAVE_STRINGS_H 1

/* Define if you have the <sys/time.h> header file.  */
#undef HAVE_SYS_TIME_H

/* Define if you have the <unistd.h> header file.  */
#undef HAVE_UNISTD_H

/* Name of package */
#define PACKAGE "mpich2"

/* Version number of package */
#define VERSION "1.0"

#define HAVE_WINDOWS_H
#define HAVE_WINDOWS_SOCKET
#define HAVE_WINSOCK2_H
#define HAVE_WIN32_SLEEP
#define HAVE_NT_LOCKS
#define HAVE_MAPVIEWOFFILE
#define HAVE_CREATEFILEMAPPING
#define HAVE_INTERLOCKEDEXCHANGE
#define HAVE_BOOL
//#define HAVE_SHARED_PROCESS_READ
//#define USE_GARBAGE_COLLECTING
#define USE_LINGER_SOCKOPT

#endif
