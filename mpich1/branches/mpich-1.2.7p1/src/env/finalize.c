/*
 *  $Id: finalize.c,v 1.20 2002/04/09 13:59:29 gropp Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Finalize = PMPI_Finalize
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Finalize  MPI_Finalize
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Finalize as PMPI_Finalize
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif
#include "reqalloc.h"
#include "queue.h"
#define MPIR_SBdestroy MPID_SBdestroy
extern int MPIR_Dump_Mem;
extern int MPIR_Dump_Ptrs;
extern int MPIR_Print_queues;

#define DBG(a)

/*
  Function to un-initialize topology code 
 */
extern void MPIR_Topology_finalize (void);

#ifndef MPID_NO_FORTRAN
#include "mpi_fortran.h"
#endif

/*@
   MPI_Finalize - Terminates MPI execution environment

   Notes:
   All processes must call this routine before exiting.  The number of
   processes running `after` this routine is called is undefined; 
   it is best not to perform much more than a 'return rc' after calling
   'MPI_Finalize'.

.N fortran
@*/
int MPI_Finalize()
{
    TR_PUSH("MPI_Finalize");

    DBG(FPRINTF( stderr, "Entering system finalize\n" ); fflush(stderr);)

    /* Complete any remaining buffered sends first */
    { void *a; int b;
    MPIR_BsendRelease( &a, &b );
    }	  

    if (MPIR_Print_queues) {
	int i, np, rank;
	(void) MPI_Comm_rank( MPI_COMM_WORLD, &rank );
	(void) MPI_Comm_size( MPI_COMM_WORLD, &np );
	for (i=0; i<np; i++) {
	    MPI_Barrier( MPI_COMM_WORLD );
	    if (i == rank) {
		MPID_Dump_queues();
		fflush( stdout );
	    }
	}}

#ifdef MPID_END_NEEDS_BARRIER
    MPI_Barrier( MPI_COMM_WORLD );
#endif    

    /* This Barrier is needed in order for MPI_Cancel to work properly */
    MPI_Barrier( MPI_COMM_WORLD );

    /* Mark the MPI environment as having been destroyed.
       Note that the definition of MPI_Initialized returns only whether
       MPI_Init has been called; not if MPI_Finalize has also been called */
    MPIR_Has_been_initialized = 2;

    /* Un-initialize topology code */
    MPIR_Topology_finalize();

    /* Like the basic datatypes, the predefined operators are in 
       permanent storage */

    DBG(FPRINTF( stderr, "About to free operators\n" ); fflush( stderr );)
	{ MPI_Op tmp; 
    tmp = MPI_MAX; MPI_Op_free( &tmp );
    tmp = MPI_MIN; MPI_Op_free( &tmp );
    tmp = MPI_SUM; MPI_Op_free( &tmp );
    tmp = MPI_PROD; MPI_Op_free( &tmp );
    tmp = MPI_LAND; MPI_Op_free( &tmp );
    tmp = MPI_BAND; MPI_Op_free( &tmp );
    tmp = MPI_LOR; MPI_Op_free( &tmp );
    tmp = MPI_BOR; MPI_Op_free( &tmp );
    tmp = MPI_LXOR; MPI_Op_free( &tmp );
    tmp = MPI_BXOR; MPI_Op_free( &tmp );
    tmp = MPI_MAXLOC; MPI_Op_free( &tmp );
    tmp = MPI_MINLOC; MPI_Op_free( &tmp );
	}


    /* Free allocated space */
    /* Note that permanent datatypes are now stored in static storage
       so that we can not free them. */
    DBG(FPRINTF( stderr, "About to free dtes\n" ); fflush( stderr ););
    MPIR_Free_dtes();
#ifndef MPID_NO_FORTRAN
    MPIR_Free_Fortran_dtes();
#endif
#ifndef MPID_NO_FORTRAN
#ifdef FOO
    /* Free the parts of the structure types */
    MPIR_Type_free_struct( MPI_FLOAT_INT );
    MPIR_Type_free_struct( MPI_DOUBLE_INT );
    MPIR_Type_free_struct( MPI_LONG_INT );
    MPIR_Type_free_struct( MPI_SHORT_INT );
    MPIR_Type_free_struct( MPI_2INT );
#endif
#ifdef FOO    
    if (MPI_2INT != MPI_2INTEGER)
	MPIR_Free_perm_type( &MPI_2INTEGER );
    MPIR_Free_perm_type( &MPIR_2real_dte );
    MPIR_Free_perm_type( &MPIR_2double_dte );
    MPIR_Free_perm_type( &MPIR_2complex_dte );
    MPIR_Free_perm_type( &MPIR_2dcomplex_dte );
#endif

#endif
    DBG(FPRINTF( stderr, "About to free COMM_WORLD\n" ); fflush( stderr );)

	{ MPI_Comm lcomm = MPI_COMM_WORLD;
	MPI_Comm_free ( &lcomm ); }

    DBG(FPRINTF( stderr, "About to free COMM_SELF\n" ); fflush( stderr );)

	{ MPI_Comm lcomm = MPI_COMM_SELF;
	MPI_Comm_free ( &lcomm ); }

    DBG(FPRINTF( stderr, "About to free GROUP_EMPTY\n" ); fflush( stderr );)

	{ MPI_Group lgroup = MPI_GROUP_EMPTY;
	MPI_Group_free ( &lgroup ); }

    DBG(FPRINTF(stderr,"About to free permanent keyval's\n");fflush(stderr);)

/* 
   Free keyvals
 */
	{int tmp;
	tmp = MPI_TAG_UB;
	MPI_Keyval_free( &tmp );
	tmp = MPI_HOST;
	MPI_Keyval_free( &tmp );
	tmp = MPI_IO;
	MPI_Keyval_free( &tmp );
	tmp = MPI_WTIME_IS_GLOBAL;
	MPI_Keyval_free( &tmp );
	}
#ifndef MPID_NO_FORTRAN
    MPIR_Free_Fortran_keyvals();
#endif

    {MPI_Errhandler tmp;
    tmp = MPI_ERRORS_RETURN;
    MPI_Errhandler_free( &tmp );
    tmp = MPI_ERRORS_ARE_FATAL;
    MPI_Errhandler_free( &tmp );
    tmp = MPIR_ERRORS_WARN;
    MPI_Errhandler_free( &tmp );
    }

    if (MPIR_Infotable) {
        FREE(MPIR_Infotable);
    }

#ifdef MPID_HAS_PROC_INFO
    /* Release any space we allocated for the proc table */
    if (MPIR_proctable != 0) {
        MPIR_debug_state    = MPIR_DEBUG_ABORTING;
	FREE(MPIR_proctable);
    }
#endif
    /* Tell device that we are done.  We place this here to allow
       the device to tell us about any memory leaks, since MPIR_SB... will
       free the storage even if it has not been deallocated by MPIR_SBfree. 
     */
    DBG(FPRINTF( stderr, "About to close device\n" ); fflush( stderr );)

    MPID_End();

    DBG(FPRINTF( stderr, "About to free SBstuff\n" ); fflush( stderr );)

    MPIR_SBdestroy( MPIR_dtes );

    MPIR_SBdestroy( MPIR_errhandlers );

    MPIR_HBT_Free();
    MPIR_Topology_Free();

    MPIR_SENDQ_FINALIZE();
#if defined(MPIR_MEMDEBUG) || defined(MPIR_PTRDEBUG)
    /* 
       This dumps the number of Fortran pointers still in use.  For this 
       to be useful, we should delete all of the one that were allocated
       by the initutil.c routine.  Instead, we just set a "highwatermark"
       for the initial values.
     */
    if (MPIR_Dump_Ptrs) {
	MPIR_UsePointer( stdout );
	MPIR_DumpPointers( stdout );
    }
    MPIR_DestroyPointer();

    if (MPIR_Dump_Mem) {
	MPID_trdump( stdout );
    }
#endif    
    /* barrier */
    TR_POP;
    return MPI_SUCCESS;
}
