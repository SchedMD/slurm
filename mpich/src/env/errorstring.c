/*
 *  $Id: errorstring.c,v 1.13 2001/11/14 19:56:39 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */


#include "mpiimpl.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Error_string = PMPI_Error_string
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Error_string  MPI_Error_string
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Error_string as PMPI_Error_string
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif
#if defined(STDC_HEADERS) || defined(HAVE_STRING_H)
#include <string.h>
#endif


/*
 *                ************ IMPORTANT NOTE *************
 * The messages in this file are synchronized with the ones in
 * mpich.En_US (English/United States) and need to be changed together.
 * Also note that the numbering of messages is CRITICAL to the messages in
 * the file.  
 *                *****************************************
 */
/*@
   MPI_Error_string - Return a string for a given error code

Input Parameters:
. errorcode - Error code returned by an MPI routine or an MPI error class

Output Parameter:
+ string - Text that corresponds to the errorcode 
- resultlen - Length of string 

Notes:  Error codes are the values return by MPI routines (in C) or in the
'ierr' argument (in Fortran).  These can be converted into error classes 
with the routine 'MPI_Error_class'.  

.N fortran
@*/
int MPI_Error_string( int errorcode, char *string, int *resultlen )
{
#ifdef OLD_ERRMSG
    int error_case = errorcode & ~MPIR_ERR_CLASS_MASK;
#endif
    int mpi_errno = MPI_SUCCESS;
    const char *newmsg;

/* 
   error_case contains any additional details on the cause of the error.
   error_args can be used to indicate the presence of additional information.
   
 */

    string[0] = 0;
#ifdef OLD_ERRMSG
switch (errorcode & MPIR_ERR_CLASS_MASK) {
    case MPI_SUCCESS:
        strcpy( string, "No error" );
	break;
    case MPI_ERR_BUFFER:
	strcpy( string, "Invalid buffer pointer" );
	if (error_case == MPIR_ERR_BUFFER_EXISTS) {
	    strcat( string,
		": Can not attach buffer when a buffer already exists" );
	    error_case = 0;
	    }
	else if (error_case == MPIR_ERR_USER_BUFFER_EXHAUSTED) {
	    strcpy( string, 
		   "Insufficent space available in user-defined buffer" );
	    error_case = 0;
	    }
	else if (error_case == MPIR_ERR_BUFFER_ALIAS) {
	    strcat( string, 
                 ": Arguments must specify different buffers (no aliasing)" );
	    error_case = 0;
	    }
	else if (error_case == MPIR_ERR_BUFFER_SIZE) {
	    strcpy( string, 
                 "Buffer size (%d) is illegal" );
	    error_case = 0;
	    }
	break;
    case MPI_ERR_COUNT:
        strcpy( string, "Invalid count argument" );
	if (error_case == MPIR_ERR_COUNT_ARRAY_NEG) {
	    strcat( string, ": count[%d] is %d" );
	    error_case = 0;
	}
	break;
    case MPI_ERR_TYPE:
        strcpy( string, "Invalid datatype argument" );
	if (error_case == MPIR_ERR_UNCOMMITTED) {
	    strcat( string, ": datatype has not been committed" );
	    error_case = 0;
	    }
	else if (error_case == MPIR_ERR_TYPE_NULL) {
	    strcat( string, ": Datatype is MPI_TYPE_NULL" );
	    error_case = 0;
	}
	else if (error_case == MPIR_ERR_TYPE_CORRUPT) {
	    strcat( string, ": datatype argument is not a valid datatype\n\
Special bit pattern %x in datatype is incorrect.  May indicate an \n\
out-of-order argument or a deleted datatype" );
	    error_case = 0;
	}
	else if (error_case == MPIR_ERR_PERM_TYPE) {
	    strcat( string, ": Can not free permanent data type" );
	    error_case = 0;
	    }
/*
	else if (error_case == MPIR_ERR_TYPE_PREDEFINED) {
	    strcat( string, ": Datatype is predefined and may not be freed" );
	    error_case = 0;
	}
 */
	break;
    case MPI_ERR_TAG:
	strcpy( string, "Invalid message tag %d" );
	break;
    case MPI_ERR_COMM:
	strcpy( string, "Invalid communicator" );
        if (error_case == MPIR_ERR_COMM_NULL) {
	    strcat( string, ": Null communicator" );
	    error_case = 0;
	    }
	else if (error_case == MPIR_ERR_COMM_INTER) {
	    strcat( string, ": Intercommunicator is not allowed" );
	    error_case = 0;
	    }
	else if (error_case == MPIR_ERR_COMM_INTRA) {
	    strcat( string, ": Intracommunicator is not allowed" );
	    error_case = 0;
	    }
	else if (error_case == MPIR_ERR_COMM_CORRUPT) {
	    strcat( string,
": communicator argument is not a valid communicator\n\
Special bit pattern %x in communicator is incorrect.  May indicate an \n\
out-of-order argument or a freed communicator" );
	    error_case = 0;
	}
	else if (error_case == MPIR_ERR_COMM_NAME) {
	    strcpy( string, "Error setting communicator name" );
	    error_case = 0;
	}
	break;
    case MPI_ERR_RANK:
        strcpy( string, "Invalid rank %d" );
	break;
    case MPI_ERR_ROOT:
	strcpy( string, "Invalid root" );
	break;
    case MPI_ERR_GROUP:
	strcpy( string, "Invalid group passed to function" );
	if (error_case == MPIR_ERR_GROUP_NULL) {
	    strcat( string, ": Null group" );
	    error_case = 0;
	}
	else if (error_case == MPIR_ERR_GROUP_CORRUPT) {
	    error_case = 0;
	    strcat( string, 
": group argument is not a valid group\n\
Special bit pattern %x in group is incorrect.  May indicate an \n\
out-of-order argument or a freed group" );
	}
	break;
    case MPI_ERR_OP:
	strcpy( string, "Invalid operation" );
	if (error_case == MPIR_ERR_NOT_DEFINED) {
	    strcat( string, ": not defined for this datatype" );
	    error_case = 0;
	    }
	else if (error_case == MPIR_ERR_OP_NULL) {
	    strcat( string, ": Null MPI_Op" );
	    error_case = 0;
	}
	break;
    case MPI_ERR_TOPOLOGY:
	strcpy( string, "Invalid topology" );
	break;
    case MPI_ERR_DIMS:
        strcpy( string, "Illegal dimension argument %d" );
	break;
    case MPI_ERR_ARG:
	strcpy( string, "Invalid argument" );
	if (error_case == MPIR_ERR_ERRORCODE) {
	    strcat( string, ": Invalid error code" );
	    error_case = 0;
	    }
	else if (error_case == MPIR_ERR_NULL) {
	    strcat( string, ": Null parameter" );
	    error_case = 0;
	    }
	else if (error_case == MPIR_ERR_PERM_KEY) {
	    strcat( string, ": Can not free permanent attribute key" );
	    error_case = 0;
	    }
	else if (error_case == MPIR_ERR_PERM_OP) {
	    strcat( string, ": Can not free permanent MPI_Op" );
	    error_case = 0;
	    }
	else if (error_case == MPIR_ERR_PERM_GROUP) {
	    strcat( string, ": Can not free permanent MPI_Group" );
	    error_case = 0;
	    }
	else if (error_case == MPIR_ERR_FORTRAN_ADDRESS_RANGE) {
	    strcat( string, 
": Address of location given to MPI_ADDRESS does not fit in Fortran integer" );
	    error_case = 0;
	    }
	else if (error_case == MPIR_ERR_KEYVAL) {
	    strcat( string, ": Invalid keyval" );
	    error_case = 0;
	}
	else if (error_case == MPIR_ERR_ERRHANDLER_NULL) {
	    strcat( string, ": Null MPI_Errhandler" );
	    error_case = 0;
	}
	else if (error_case == MPIR_ERR_ERRHANDLER_CORRUPT) {
	    strcat( string, 
": MPI_Errhandler argument is not a valid errorhandler\n\
Special bit pattern %x in errhandler is incorrect.  May indicate an \n\
out-of-order argument or a deleted error handler" );
	    error_case = 0;
	}
/* MPI2 */
	else if (error_case == MPIR_ERR_STATUS_IGNORE) {
	    strcat( string, ": Illegal use of MPI_STATUS_IGNORE or \
MPI_STATUSES_IGNORE" );
	    error_case = 0;
	}
	break;

/* 
    case MPI_ERR_BAD_ARGS:
	strcpy( string, "Invalid arguments to MPI routine" );
	break;
*/

    case MPI_ERR_UNKNOWN:
	strcpy( string, "Unknown error" );
	break;
    case MPI_ERR_TRUNCATE:
	strcpy( string, "Message truncated" );
	break;
    case MPI_ERR_OTHER:
	/* This is slightly different from the other error codes/classes, 
	   in that there is no default message */
	if (error_case == MPIR_ERR_LIMIT) {
	    strcpy( string, "System resource limit exceeded" );
	    error_case = 0;
	    }
	else if (error_case == MPIR_ERR_NOMATCH) {
	    strcpy( string, "Ready send had no matching receive" );
	    error_case = 0;
	    }
	else if (error_case == MPIR_ERR_INIT) {
	    strcpy( string, "Can not call MPI_INIT twice!" );
	    error_case = 0;
	    }
	else if (error_case == MPIR_ERR_PRE_INIT) {
	    strcpy( string, 
		   "MPI_INIT must be called before other MPI routines" );
	    error_case = 0;
	    }
	else if (error_case == MPIR_ERR_MPIRUN) {
	    strcpy( string, 
	     "MPIRUN chose the wrong device %s; program needs device %s" );
	    error_case = 0;
	}
	else if (error_case == MPIR_ERR_BAD_INDEX) {
	    strcpy( string, "Could not convert index %d(%x) into\n\
a pointer.  The index may be an incorrect argument.\n\
Possible sources of this problem are a missing \"include 'mpif.h'\",\n\
a misspelled MPI object (e.g., MPI_COM_WORLD instead of MPI_COMM_WORLD)\n\
or a misspelled user variable for an MPI object (e.g., \n\
com instead of comm)." );
	    error_case = 0;
	}
	else if (error_case == MPIR_ERR_INDEX_EXHAUSTED) {
	    strcpy( string, "Pointer conversions exhausted\n\
Too many MPI objects may have been passed to/from Fortran\n\
without being freed" );
	    error_case = 0;
	}
	else if (error_case == MPIR_ERR_INDEX_FREED) {
	    strcpy( string, 
		    "Error in recovering Fortran pointer; already freed" );
	    error_case = 0;
	}
	else if (error_case == MPIR_ERR_BUFFER_TOO_SMALL) {
	    strcpy( string,
		    "Specified buffer is smaller than MPI_BSEND_OVERHEAD" );
	    error_case = 0;
	}
	else {
	    strcpy( string, "Unclassified error" );
	    }
	break;
    case MPI_ERR_INTERN:
	strcpy( string, "Internal MPI error!" );
	if (error_case == MPIR_ERR_EXHAUSTED) {
	    strcat( string, ": Out of internal memory" );
	    error_case = 0;
	    }
	else if (error_case == MPIR_ERR_ONE_CHAR) {
	    strcat( string, 
": Cray restriction: Either both or neither buffers must be of type character" );
	    error_case = 0;
	}
	else if (error_case == MPIR_ERR_MSGREP_SENDER) {
	    strcat( string, "WARNING - sender format not ready!" );
	    error_case = 0;
	}
	else if (error_case == MPIR_ERR_MSGREP_UNKNOWN) {
	    strcat( string, "WARNING - unrecognized msgrep %d" );
	    error_case = 0;
	}
	break;
    case MPI_ERR_IN_STATUS:
	strcpy( string, "Error code is in status" );
	break;
    case MPI_ERR_PENDING:
	strcpy( string, "Pending request (no error)" );
        break;

    case MPI_ERR_REQUEST:
	strcpy( string, "Illegal mpi_request handle" );
	if (error_case == MPIR_ERR_REQUEST_NULL) {
	    strcat( string, ": Null request" );
	    error_case = 0;
	}
	break;

    default:
	strcpy( string, "Unexpected error value!" );
	*resultlen = strlen( string );
	mpi_errno = MPI_ERR_ARG;
	return MPIR_ERROR( MPIR_COMM_WORLD, mpi_errno, "MPI_ERROR_STRING" );
    }

if (error_case != 0) {
    /* Unrecognized error case */
    MPIR_ERROR_PUSH_ARG(&error_case);
    strcat( string, ": unrecognized error code in error class %d" );
    mpi_errno = MPI_ERR_ARG;
    }

/* Now we have the default message.  Try to get a better one */
MPIR_GetErrorMessage( errorcode, string, &newmsg );
#else
MPIR_GetErrorMessage( errorcode, string, &newmsg );
/* newmsg = MPIR_Err_map_code_to_string( errorcode ); */
#endif
if (newmsg) {
    strcpy( string, newmsg );
}

*resultlen = strlen( string );
return mpi_errno;
}
