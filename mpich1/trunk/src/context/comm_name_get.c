/*
 *  $Id: comm_name_get.c,v 1.9 2001/11/14 19:54:20 ashton Exp $
 *
 *  (C) 1996 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */
/* Update log
 *
 * Jun 18 1997 jcownie@dolphinics.com: They changed the calling convention when I wasn't
 *             looking ! Do what the Forum says...
 * Nov 28 1996 jcownie@dolphinics.com: Implement MPI-2 communicator naming function.
 */

#include "mpiimpl.h"
#include "mpimem.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Comm_get_name = PMPI_Comm_get_name
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Comm_get_name  MPI_Comm_get_name
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Comm_get_name as PMPI_Comm_get_name
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

MPI_Comm_get_name - return the print name from the communicator

  Input Parameter:
. comm - Communicator to get name of (handle)

  Output Parameters:
+ namep - One output, contains the name of the communicator.  It must
  be an array of size at least 'MPI_MAX_NAME_STRING'.
- reslen - Number of characters in name

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
@*/
int MPI_Comm_get_name( MPI_Comm comm, char *namep, int *reslen )
{
  struct MPIR_COMMUNICATOR *comm_ptr = MPIR_GET_COMM_PTR(comm);
  static char myname[] = "MPI_COMM_GET_NAME";
  int mpi_errno;
  char *nm;

  TR_PUSH(myname);

  MPIR_TEST_MPI_COMM(comm,comm_ptr,comm_ptr,myname);

  if (comm_ptr->comm_name)
    nm =  comm_ptr->comm_name;
  else
    nm = "";		/* The standard says null string... */

  /* The user better have allocated the result big enough ! */
  strncpy (namep, nm, MPI_MAX_NAME_STRING);
  *reslen = strlen (nm);

  TR_POP;
  return MPI_SUCCESS;
}

