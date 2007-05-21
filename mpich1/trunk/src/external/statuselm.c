#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Status_set_elements = PMPI_Status_set_elements
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Status_set_elements  MPI_Status_set_elements
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Status_set_elements as PMPI_Status_set_elements
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
   MPI_Status_set_elements - Set the opaque part of an MPI_Status so that
   MPI_Get_elements will return count.

   Input Parameters:
+  status   - Status to associate count with (Status)
.  datatype - datatype associated with count (handle)
-  count    - number of elements to associate with status (integer)

@*/
int MPI_Status_set_elements( MPI_Status *status, MPI_Datatype datatype,
			     int count )
{
    int size;
#ifdef MPIR_CHECK_ARGS
#endif

#ifdef MPID_Status_set_elements
    MPID_STATUS_SET_ELEMENTS( status, datatype, count );
#else
    /* This isn't quite correct, but it is a start */
    if (count >= 0) {
	MPI_Type_size( datatype, &size );
	status->count = count * size;
    }
    else {
	/* Allow undefined to be passed */
	status->count = MPI_UNDEFINED;
    }
#endif
    return 0;
}
