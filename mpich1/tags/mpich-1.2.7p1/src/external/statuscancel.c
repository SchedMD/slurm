#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Status_set_cancelled = PMPI_Status_set_cancelled
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Status_set_cancelled  MPI_Status_set_cancelled
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Status_set_cancelled as PMPI_Status_set_cancelled
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
   MPI_Status_set_cancelled - Set the opaque part of an MPI_Status so that
   MPI_Test_cancelled will return flag

   Input Parameters:
+  status   - Status to associate count with (Status)
-  flag     - if true indicates that request was cancelled (logical)

.N fortran

.N Errors
.N MPI_SUCCESS
@*/
int MPI_Status_set_cancelled( MPI_Status *status, int flag )
{
#ifdef MPIR_CHECK_ARGS
#endif
    if (flag) 
	status->MPI_TAG = MPIR_MSG_CANCELLED;

    return 0;
}
