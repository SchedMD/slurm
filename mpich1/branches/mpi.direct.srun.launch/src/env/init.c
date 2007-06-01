/*
 *  $Id: init.c,v 1.14 2002/04/08 23:12:19 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */


/* 
   define MPID_NO_FORTRAN if the Fortran interface is not to be supported
   (perhaps because there is no Fortran compiler)
 */
#include "mpiimpl.h"
#ifdef HAVE_STDLIB_H
#include <stdlib.h> /* for exit() */
#endif

#ifdef HAVE_WEAK_SYMBOLS

/* Undefing MPI_Init if mpi.h defined it to help catch library/headerfile
   conflicts */
#ifdef MPI_Init
#undef MPI_Init
#undef PMPI_Init
#endif

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Init = PMPI_Init

/* #pragma weak MPI_Init_vcheck = PMPI_Init_vcheck */
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Init  MPI_Init
/* #pragma _HP_SECONDARY_DEF PMPI_Init_vcheck  MPI_Init_vcheck */
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Init as PMPI_Init
/* #pragma _CRI duplicate MPI_Init_vcheck as PMPI_Init_vcheck */
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif

/*@
   MPI_Init - Initialize the MPI execution environment

   Input Parameters:
+  argc - Pointer to the number of arguments 
-  argv - Pointer to the argument vector

   Command line arguments:
   MPI specifies no command-line arguments but does allow an MPI 
   implementation to make use of them.

+  -mpiqueue - print out the state of the message queues when 'MPI_FINALIZE'
   is called.  All processors print; the output may be hard to decipher.  This
   is intended as a debugging aid.

.  -mpiversion - print out the version of the implementation (`not` of MPI),
    including the arguments that were used with configure.

.  -mpinice nn - Increments the nice value by 'nn' (lowering the priority 
    of the program by 'nn').  'nn' must be positive (except for root).  Not
    all systems support this argument; those that do not will ignore it.

.  -mpedbg - Start a debugger in an xterm window if there is an error (either
   detected by MPI or a normally fatal signal).  This works only if MPICH
   was configured with '-mpedbg'.  CURRENTLY DISABLED.  If you have TotalView,
   -mpichtv or mpirun -tv will give you a better environment anyway.

.  -mpimem - If MPICH was built with '-DMPIR_DEBUG_MEM', this checks all
    malloc and free operations (internal to MPICH) for signs of injury 
    to the memory allocation areas.

-  -mpidb options - Activate various debugging options.  Some require
   that MPICH have been built with special options.  These are intended 
   for debugging MPICH, not for debugging user programs.  The available 
   options include:
.vb     
        mem     - Enable dynamic memory tracing of internal MPI objects
	memall  - Generate output of all memory allocation/deallocation
        ptr     - Enable tracing of internal MPI pointer conversions
	rank n  - Limit subsequent -mpidb options to on the process with
	          the specified rank in MPI_COMM_WORLD.  A rank of -1
		  selects all of MPI_COMM_WORLD.
        ref     - Trace use of internal MPI objects
        reffile filename - Trace use of internal MPI objects with output
	          to the indicated file
	trace   - Trace routine calls
.ve

   Notes:
   Note that the Fortran binding for this routine has only the error return
   argument ('MPI_INIT(ierror)')

   Because the Fortran and C versions of 'MPI_Init' are different, there is 
   a restriction on who can call 'MPI_Init'.  The version (Fortran or C) must
   match the main program.  That is, if the main program is in C, then 
   the C version of 'MPI_Init' must be called.  If the main program is in 
   Fortran, the Fortran version must be called.

   On exit from this routine, all processes will have a copy of the argument
   list.  This is `not required` by the MPI standard, and truely portable codes
   should not rely on it.  This is provided as a service by this 
   implementation (an MPI implementation is allowed to distribute the
   command line arguments but is not required to).

   Command line arguments are not provided to Fortran programs.  More 
   precisely, non-standard Fortran routines such as getarg and iargc 
   have undefined behavior in MPI and in this implementation.

   The MPI standard does not say what a program can do before an 'MPI_INIT' or
   after an 'MPI_FINALIZE'.  In the MPICH implementation, you should do
   as little as possible.  In particular, avoid anything that changes the
   external state of the program, such as opening files, reading standard
   input or writing to standard output.

   Signals used:
   The MPI standard requires that all signals used be documented.  The MPICH
   implementation itself uses no signals, but some of the software that MPICH
   relies on may use some signals.  The list below is partial and should
   be independantly checked if you (and any package that you use) depend
   on particular signals.

   IBM POE/MPL for SP2:
   SIGHUP, SIGINT, SIGQUIT, SIGFPE, SIGSEGV, SIGPIPE, SIGALRM, SIGTERM,
   SIGIO

   -mpedbg switch:
   SIGQUIT, SIGILL, SIGFPE, SIGBUS, SIGSEGV, SIGSYS

   Meiko CS2:
   SIGUSR2

   ch_p4 device:
   SIGUSR1

   The ch_p4 device also catches SIGINT, SIGFPE, SIGBUS, and SIGSEGV; this
   helps the p4 device (and MPICH) more gracefully abort a failed program.

   Intel Paragon (ch_nx and nx device):
   SIGUSR2

   Shared Memory (ch_shmem device):
   SIGCHLD

   Note that if you are using software that needs the same signals, you may
   find that there is no way to use that software with the MPI implementation.
   The signals that cause the most trouble for applications include
   'SIGIO', 'SIGALRM', and 'SIGPIPE'.  For example, using 'SIGIO' and 
   'SIGPIPE' may prevent X11 routines from working.  

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_INIT
@*/
int MPI_Init(int *argc, char ***argv)
{
    return MPIR_Init(argc,argv);
}

#ifdef FOO
/* See the comments in mpi.h on this routine */
int MPI_Init_vcheck( int *argc, char ***argv, char version[] )
{
    if (strncmp( version, MPICH_VERSION, 100 )) {
	fprintf( stderr, 
	 "Version of mpi.h (%s) does not match the MPICH library (%s)\n", 
		 version, MPICH_VERSION );
	exit(1);
    }
    return MPI_Init( argc, argv );
}

#endif
