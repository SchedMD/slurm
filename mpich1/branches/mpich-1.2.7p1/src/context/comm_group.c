/*
 *  $Id: comm_group.c,v 1.7 2001/11/14 19:54:19 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Comm_group = PMPI_Comm_group
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Comm_group  MPI_Comm_group
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Comm_group as PMPI_Comm_group
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

MPI_Comm_group - Accesses the group associated with given communicator

Input Parameter:
. comm - Communicator

Output Parameter:
. group - Group in communicator

Using 'MPI_COMM_NULL' with 'MPI_Comm_group':

It is an error to use 'MPI_COMM_NULL' as one of the arguments to
'MPI_Comm_group'.  The relevant sections of the MPI standard are 

$(2.4.1 Opaque Objects)
A null handle argument is an erroneous 'IN' argument in MPI calls, unless an
exception is explicitly stated in the text that defines the function.

$(5.3.2. Group Constructors)
<no text in 'MPI_COMM_GROUP' allowing a null handle>

Previous versions of MPICH allow 'MPI_COMM_NULL' in this function.  In the
interests of promoting portability of applications, we have changed the
behavior of 'MPI_Comm_group' to detect this violation of the MPI standard.

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
@*/
int MPI_Comm_group ( 
	MPI_Comm comm, 
	MPI_Group *group )
{
    struct MPIR_COMMUNICATOR *comm_ptr;
    struct MPIR_GROUP *new_group;
    static char myname[] = "MPI_COMM_GROUP";
    int mpi_errno = MPI_SUCCESS;

    TR_PUSH(myname);

    comm_ptr = MPIR_GET_COMM_PTR(comm);
    MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr,myname);

    MPIR_Group_dup( comm_ptr->local_group, &new_group );
    *group = new_group->self;
    TR_POP;
    return (MPI_SUCCESS);
}
