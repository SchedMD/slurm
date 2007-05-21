/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 2003 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPIO_Waitall = PMPIO_Waitall
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPIO_Waitall MPIO_Waitall
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPIO_Waitall as PMPIO_Waitall
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPIO_BUILD_PROFILING
#include "mpioprof.h"
#endif

/*
  This is a temporary function until we switch to using MPI-2's generalized
  requests.
*/

int MPIO_Waitall( int count, MPIO_Request requests[], MPI_Status statuses[] )
{
    int notdone, i, flag, err; 

    MPID_CS_ENTER();

    if (count == 1)  {
    	    MPIR_Nest_incr();
	    err = MPIO_Wait(requests, statuses);
    	    MPIR_Nest_decr();
	    goto fn_exit;
    }
    
    
    do {
	notdone = 0;
	for (i=0; i<count; i++) {
	    if (requests[i] != MPIO_REQUEST_NULL) {
    		MPIR_Nest_incr();
		err = MPIO_Test( &requests[i], &flag, &statuses[i] );
    		MPIR_Nest_decr();
		if (!flag) notdone = 1;
		if (err) goto fn_exit;
	    }
	    else {
#ifdef MPICH2
		/* need to set empty status */
		if (statuses != MPI_STATUSES_IGNORE) {
		    statuses[i].MPI_SOURCE = MPI_ANY_SOURCE;
		    statuses[i].MPI_TAG    = MPI_ANY_TAG;
		    statuses[i].count      = 0;
		    statuses[i].cancelled  = 0;
		}
#else
		;
#endif
	    }
	}
    } while (notdone);

    err = MPI_SUCCESS;
fn_exit:

    MPID_CS_EXIT();
    return err;
}

