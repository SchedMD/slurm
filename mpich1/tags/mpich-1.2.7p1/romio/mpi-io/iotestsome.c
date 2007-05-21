/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 2003 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "mpioimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPIO_Testsome = PMPIO_Testsome
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPIO_Testsome MPIO_Testsome
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPIO_Testsome as PMPIO_Testsome
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

int MPIO_Testsome(int count, MPIO_Request requests[], int *outcount,
		  int indices[], MPI_Status *statuses)
{
    int i, err; 
    int flag;

    MPID_CS_ENTER();

    if (count == 1) {
    	MPIR_Nest_incr();
	err = MPIO_Test( requests, &flag, statuses );
    	MPIR_Nest_decr();
	if (!err) {
	    if (flag) {
		indices[0] = 0;
		*outcount = 1;
	    }
	    else {
		*outcount = 0;
	    }
	}
	goto fn_exit;
    }

    /* Check for no active requests */
    for (i=0; i<count; i++) {
	if (requests[i] != MPIO_REQUEST_NULL) {
	    break;
	}
    }
    if (i == count) {
	*outcount = MPI_UNDEFINED;
	err = MPI_SUCCESS;
	goto fn_exit;
    }

    err = MPI_SUCCESS;
    *outcount = 0;
    for (i=0; i<count; i++) {
      if (requests[i] != MPIO_REQUEST_NULL) {
    	MPIR_Nest_incr();
	err = MPIO_Test( &requests[i], &flag, statuses );
    	MPIR_Nest_decr();
	if (flag) {
	  if (!err) {
	      indices[0] = i;
	      indices++;
	      statuses++;
	      *outcount = *outcount + 1;
	  }
	}
      }
    }

fn_exit:

    MPID_CS_EXIT();
    return err;
}
