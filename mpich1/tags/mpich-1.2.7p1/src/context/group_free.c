/*
 *  $Id: group_free.c,v 1.7 2001/11/14 19:54:23 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */


#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Group_free = PMPI_Group_free
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Group_free  MPI_Group_free
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Group_free as PMPI_Group_free
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

MPI_Group_free - Frees a group

Input Parameter
. group - group (handle) 

Notes:
On output, group is set to 'MPI_GROUP_NULL'.

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_ARG
.N MPI_ERR_PERM_GROUP
@*/
int MPI_Group_free ( MPI_Group *group )
{
    struct MPIR_GROUP *group_ptr;
    int mpi_errno = MPI_SUCCESS;
    static char myname[] = "MPI_GROUP_FREE";
    
    TR_PUSH(myname);

    /* Check for bad arguments */
    MPIR_TEST_ARG(group);
    if (mpi_errno)
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );

    /* Free null groups generates error */
    if ( (*group) == MPI_GROUP_NULL ) {
	TR_POP;
	return MPIR_ERROR(MPIR_COMM_WORLD, MPI_ERR_GROUP, myname );
    }
	 
    group_ptr = MPIR_GET_GROUP_PTR(*group);
#ifndef MPIR_NO_ERROR_CHECKING
    /* MPIR_TEST_MPI_GROUP(*group,group_ptr,MPIR_COMM_WORLD,myname); */
    MPIR_TEST_GROUP(group_ptr);
    if (mpi_errno)
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );

    /* We can't free permanent objects unless finalize has been called */
    if  ( ( group_ptr->permanent == 1 ) && group_ptr->ref_count <= 1 && 
          (MPIR_Has_been_initialized == 1) )
	return MPIR_ERROR( MPIR_COMM_WORLD, 
	   MPIR_ERRCLASS_TO_CODE(MPI_ERR_ARG,MPIR_ERR_PERM_GROUP),myname );
#endif

    /* Free group */
    if ( group_ptr->ref_count <= 1 ) {
	MPIR_FreeGroup( group_ptr );
    }
    else {
	MPIR_REF_DECR(group_ptr);
    }
    /* This could be dangerous if the object is MPI_GROUP_EMPTY and not just
     a copy of it.... It would also be illegal. */
    (*group) = MPI_GROUP_NULL;

    TR_POP;
    return (mpi_errno);
}
