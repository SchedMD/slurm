/*
 *  $Id: bufattach.c,v 1.9 2001/11/14 20:09:55 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */


#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Buffer_attach = PMPI_Buffer_attach
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Buffer_attach  MPI_Buffer_attach
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Buffer_attach as PMPI_Buffer_attach
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
  MPI_Buffer_attach - Attaches a user-defined buffer for sending

Input Parameters:
+ buffer - initial buffer address (choice) 
- size - buffer size, in bytes (integer) 

Notes:
The size given should be the sum of the sizes of all outstanding Bsends that
you intend to have, plus a few hundred bytes for each Bsend that you do.
For the purposes of calculating size, you should use 'MPI_Pack_size'. 
In other words, in the code
.vb
     MPI_Buffer_attach( buffer, size );
     MPI_Bsend( ..., count=20, datatype=type1,  ... );
     ...
     MPI_Bsend( ..., count=40, datatype=type2, ... );
.ve
the value of 'size' in the MPI_Buffer_attach call should be greater than
the value computed by
.vb
     MPI_Pack_size( 20, type1, comm, &s1 );
     MPI_Pack_size( 40, type2, comm, &s2 );
     size = s1 + s2 + 2 * MPI_BSEND_OVERHEAD;
.ve    
The 'MPI_BSEND_OVERHEAD' gives the maximum amount of space that may be used in 
the buffer for use by the BSEND routines in using the buffer.  This value 
is in 'mpi.h' (for C) and 'mpif.h' (for Fortran).

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_BUFFER
.N MPI_ERR_INTERN

.seealso: MPI_Buffer_detach, MPI_Bsend
@*/
int MPI_Buffer_attach( void *buffer, int size )
{
    int mpi_errno;
    static char myname[] = "MPI_BUFFER_ATTACH";

    TR_PUSH(myname);

#ifndef MPIR_NO_ERROR_CHECKING
    if (size < 0) {
	mpi_errno = MPIR_Err_setmsg( MPI_ERR_BUFFER, MPIR_ERR_BUFFER_SIZE, 
				     myname, (char *)0, (char *)0, size );
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
    }
#endif

    if ((mpi_errno = MPIR_BsendInitBuffer( buffer, size )))
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );
    TR_POP;
    return MPI_SUCCESS;
}
