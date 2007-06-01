/*
 *  $Id: getelements.c,v 1.11 2001/11/14 20:09:57 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Get_elements = PMPI_Get_elements
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Get_elements  MPI_Get_elements
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Get_elements as PMPI_Get_elements
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif
#include "mpidmpi.h"

/*@
  MPI_Get_elements - Returns the number of basic elements
                     in a datatype

Input Parameters:
+ status - return status of receive operation (Status) 
- datatype - datatype used by receive operation (handle) 

Output Parameter:
. count - number of received basic elements (integer) 

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_TYPE

@*/
int MPI_Get_elements ( MPI_Status *status, MPI_Datatype datatype, 
		       int *elements )
{
    int count;
    int mpi_errno = MPI_SUCCESS;
    struct MPIR_DATATYPE *dtype_ptr;
    static char myname[] = "MPI_GET_ELEMENTS";

    dtype_ptr   = MPIR_GET_DTYPE_PTR(datatype);
    MPIR_TEST_DTYPE(datatype,dtype_ptr,MPIR_COMM_WORLD,myname);

    /*********** Check to see if datatype is committed ********
     *********** Debbie Swider - 11/17/97 *********************/
    if (!dtype_ptr->committed) {
        return MPIR_ERROR( MPIR_COMM_WORLD, 
	    MPIR_ERRCLASS_TO_CODE(MPI_ERR_TYPE,MPIR_ERR_UNCOMMITTED), myname );
    }

#ifdef MPID_HAS_GET_ELEMENTS
    mpi_errno = MPID_Get_elements( status, datatype, elements );
#else
    /* Find the number of elements */
    MPI_Get_count (status, datatype, &count);
    if (count == MPI_UNDEFINED) {
	/* To do this correctly, we need to run through the datatype,
	   processing basic types until we run out of data.  
	   We can do this in part by computing how many full versions
	   of datatype will fit, and make use of the datatype->elements
	   field.  If there isn't an EXACT fit, we need to look into
	   the datatype for more details about the exact mapping to
	   elements.  We do this with MPIR_Unpack2.
       */
#ifdef FOO
	*elements = count;
	/* HACK ALERT -- the code in this if is not correct */
	/*               but for now ... */
	double cnt = 
	    (double) status->count / (double) dtype_ptr->size;
	(*elements) = (int) ( cnt * (double) dtype_ptr->elements );
#endif
	{
	    int srclen, destlen, used_len;
	    int i_dummy;
      
	    srclen   = status->count;
	    /* Need to set count so that we'll exit when we run out of 
	       items.  It could be ceil(status->count/dtype_ptr->size) .
	       Alternately, we could check that used_len >= srclen - epsilon
	       (in case there isn't enough for the last item).

	       Why isn't this correct?
	       */
	    if (dtype_ptr->size > 0)
		count = 1 + (srclen / dtype_ptr->size);
	    else {
		*elements = srclen ? MPI_UNDEFINED : 0;
		return MPI_SUCCESS;
	    }
	    *elements = 0;
	    used_len  = 0;
	    MPIR_Unpack2( (char *)&i_dummy, count, dtype_ptr, 
			  MPIR_Elementcnt, (void *)elements, (char *)&i_dummy,
			  srclen, &destlen, &used_len );
	    /* If anything is left, return undefined */
	    if (used_len != srclen)
		*elements = MPI_UNDEFINED;
	}
    }
    else
	(*elements) = count * dtype_ptr->elements;
#endif
    MPIR_RETURN( MPIR_COMM_WORLD, mpi_errno, myname );
}
