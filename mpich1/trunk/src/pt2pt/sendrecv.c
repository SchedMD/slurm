/*
 *  $Id: sendrecv.c,v 1.8 2001/11/14 20:10:02 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Sendrecv = PMPI_Sendrecv
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Sendrecv  MPI_Sendrecv
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Sendrecv as PMPI_Sendrecv
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
    MPI_Sendrecv - Sends and receives a message

Input Parameters:
+ sendbuf - initial address of send buffer (choice) 
. sendcount - number of elements in send buffer (integer) 
. sendtype - type of elements in send buffer (handle) 
. dest - rank of destination (integer) 
. sendtag - send tag (integer) 
. recvcount - number of elements in receive buffer (integer) 
. recvtype - type of elements in receive buffer (handle) 
. source - rank of source (integer) 
. recvtag - receive tag (integer) 
- comm - communicator (handle) 

Output Parameters:
+ recvbuf - initial address of receive buffer (choice) 
- status - status object (Status).  This refers to the receive operation.
  
.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
.N MPI_ERR_COUNT
.N MPI_ERR_TYPE
.N MPI_ERR_TAG
.N MPI_ERR_RANK

@*/
int MPI_Sendrecv( void *sendbuf, int sendcount, MPI_Datatype sendtype, 
		  int dest, int sendtag, 
                  void *recvbuf, int recvcount, MPI_Datatype recvtype, 
		  int source, int recvtag, MPI_Comm comm, MPI_Status *status )
{
    int               mpi_errno = MPI_SUCCESS;
    MPI_Status        status_array[2];
    MPI_Request       req[2];
    MPIR_ERROR_DECL;
    struct MPIR_COMMUNICATOR *comm_ptr;
    static char myname[] = "MPI_SENDRECV";

    /* Let the Isend/Irecv check arguments */
    /* Comments on this:
       We can probably do an Irecv/Send/Wait on Irecv (blocking send)
       but what we really like to do is "send if odd, recv if even, 
       followed by send if even, recv if odd".  We can't do that, 
       because we don't require that these match up in any particular
       way (that is, there is no way to assert the "parity" of the 
       partners).  Note that the IBM "mp_bsendrecv" DOES require that
       only mp_bsendrecv be used.  

       Should there be a send/recv bit in the send mode? 

       Note that in this implementation, if the error handler is "return",
       these will return the error to the caller.  If the handler causes
       an abort or message, then that will occur in the called routine.
       Thus, this code need not call the error handler AGAIN.
     */

    comm_ptr = MPIR_GET_COMM_PTR(comm);
    MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr,myname);

    MPIR_ERROR_PUSH(comm_ptr);
    MPIR_CALL_POP(MPI_Irecv ( recvbuf, recvcount, recvtype,
		    source, recvtag, comm, &req[1] ),comm_ptr,myname);
    MPIR_CALL_POP(MPI_Isend ( sendbuf, sendcount, sendtype, dest,   
			    sendtag, comm, &req[0] ),comm_ptr,myname);
    /* FPRINTF( stderr, "[%d] Starting waitall\n", MPIR_tid );*/
    mpi_errno = MPI_Waitall( 2, req, status_array );
    /* We don't use MPIR_CALL_POP because we want to convert
       error in status to the direct error */
    /* MPIR_CALL_POP(MPI_Waitall ( 2, req, status_array ),comm_ptr,myname); */
    MPIR_ERROR_POP(comm_ptr);
    /*FPRINTF( stderr, "[%d] Ending waitall\n", MPIR_tid );*/

    if (mpi_errno == MPI_ERR_IN_STATUS) {
	if (status_array[0].MPI_ERROR) mpi_errno = status_array[0].MPI_ERROR;
	if (status_array[1].MPI_ERROR) mpi_errno = status_array[1].MPI_ERROR;
    }
    (*status) = status_array[1];
    MPIR_RETURN(comm_ptr,mpi_errno,myname);
}

