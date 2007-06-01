/*
 *  $Id: type_free.c,v 1.8 2001/11/14 20:10:07 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Type_free = PMPI_Type_free
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Type_free  MPI_Type_free
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Type_free as PMPI_Type_free
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif
/* pt2pt for MPIR_Type_free */
#include "mpipt2pt.h"

#ifndef MPIR_TRUE
#define MPIR_TRUE  1
#define MPIR_FALSE 0
#endif

/*@
    MPI_Type_free - Frees the datatype

Input Parameter:
. datatype - datatype that is freed (handle) 

Predefined types:

The MPI standard states that (in Opaque Objects)
.vb
MPI provides certain predefined opaque objects and predefined, static handles
to these objects. Such objects may not be destroyed.
.ve

Thus, it is an error to free a predefined datatype.  The same section makes
it clear that it is an error to free a null datatype.

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_TYPE
.N MPI_ERR_ARG
@*/
int MPI_Type_free ( MPI_Datatype *datatype )
{
    int mpi_errno = MPI_SUCCESS;
    struct MPIR_DATATYPE *dtype_ptr;
    static char myname[] = "MPI_TYPE_FREE";

    TR_PUSH(myname);
    /* Check for bad arguments */
    MPIR_TEST_ARG(datatype);
    if (mpi_errno)
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, myname );

    dtype_ptr   = MPIR_GET_DTYPE_PTR(*datatype);
    MPIR_TEST_DTYPE(*datatype,dtype_ptr,MPIR_COMM_WORLD, myname );

    /* Test for predefined datatypes */
/*    if (dtype_ptr->basic) {
	return MPIR_ERROR( MPIR_COMM_WORLD, MPI_ERR_  , myname );
    }
 */

    /* Freeing null datatypes succeeds silently */
    if ( (*datatype) == MPI_DATATYPE_NULL ) {
	return MPIR_ERROR( MPIR_COMM_WORLD, 
	     MPIR_ERRCLASS_TO_CODE(MPI_ERR_TYPE,MPIR_ERR_TYPE_NULL), myname );
    }

    /* We can't free permanent objects unless finalize has been called */
    if  ( ( (dtype_ptr)->permanent ) && MPIR_Has_been_initialized == 1) 
	return MPIR_ERROR( MPIR_COMM_WORLD, 
            MPIR_ERRCLASS_TO_CODE(MPI_ERR_TYPE,MPIR_ERR_PERM_TYPE), myname );

    mpi_errno = MPIR_Type_free( &dtype_ptr );

    (*datatype) = MPI_DATATYPE_NULL;
    TR_POP;
    return (mpi_errno);
}

