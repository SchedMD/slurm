/*
 *  $Id: comm_rgroup.c,v 1.7 2001/11/14 19:54:21 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Comm_remote_group = PMPI_Comm_remote_group
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Comm_remote_group  MPI_Comm_remote_group
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Comm_remote_group as PMPI_Comm_remote_group
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

MPI_Comm_remote_group - Accesses the remote group associated with 
                        the given inter-communicator

Input Parameter:
. comm - Communicator (must be intercommunicator)

Output Parameter:
. group - remote group of communicator

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
@*/
int MPI_Comm_remote_group ( MPI_Comm comm, MPI_Group *group )
{
    struct MPIR_COMMUNICATOR *comm_ptr;
    struct MPIR_GROUP *group_ptr;
    int flag;
    int mpi_errno = MPI_SUCCESS;
    static char myname[] = "MPI_COMM_REMOTE_GROUP";

    TR_PUSH(myname);
    comm_ptr = MPIR_GET_COMM_PTR(comm);
    MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr,myname );

    /* Check for intra-communicator */
    MPI_Comm_test_inter ( comm, &flag );
    if (!flag) return MPIR_ERROR(comm_ptr,
	 MPIR_ERRCLASS_TO_CODE(MPI_ERR_COMM,MPIR_ERR_COMM_INTRA),myname);

    MPIR_Group_dup( comm_ptr->group, &group_ptr );
    *group = group_ptr->self;
    TR_POP;
    return (MPI_SUCCESS);
}
