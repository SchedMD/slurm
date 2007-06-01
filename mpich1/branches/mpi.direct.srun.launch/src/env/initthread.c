/*
 *  (C) 1999 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */


/* 
   define MPID_NO_FORTRAN if the Fortran interface is not to be supported
   (perhaps because there is no Fortran compiler)
 */
#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Init_thread = PMPI_Init_thread
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Init_thread  MPI_Init_thread
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Init_thread as PMPI_Init_thread
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
   MPI_Init_thread - Initialize the MPI execution environment

   Input Parameters:
+  argc - Pointer to the number of arguments 
.  argv - Pointer to the argument vector
-  required - Level of desired thread support

   Output Parameter:
.  provided - Level of provided thread support

   Command line arguments:
   MPI specifies no command-line arguments but does allow an MPI 
   implementation to make use of them.  See 'MPI_INIT' for a description of 
   the command line arguments supported by 'MPI_INIT' and 'MPI_INIT_THREAD'.

   Notes:
   Note that the Fortran binding for this routine does not have the 'argc' and
   'argv' arguments. ('MPI_INIT_THREAD(required, provided, ierror)')

   Currently, MPICH places the same restrictions on 'MPI_INIT_THREAD' as 
   on 'MPI_INIT' (see the 'MPI_INIT' man page).  When MPICH fully supports
   MPI-2, this restriction will be removed (as requried by the MPI-2 
   standard).

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
int MPI_Init_thread(int *argc, char ***argv, int required, int *provided )
{
    /* The g++ compiler (2.95.1) does not accept char *((*argv)[]) (argv is a 
       pointer to an array of pointers).  Rather than use different bindings
       depending on whether g++ is used, we just use the equivalent char *** 
     */
    *provided = MPI_THREAD_FUNNELED;
    return MPIR_Init(argc,(char ***)argv);
}
