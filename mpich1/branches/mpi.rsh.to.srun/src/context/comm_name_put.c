/*
 *  $Id: comm_name_put.c,v 1.7 2001/11/14 19:54:20 ashton Exp $
 *
 *  (C) 1996 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */
/* Update log
 *
 * Nov 28 1996 jcownie@dolphinics.com: Implement MPI-2 communicator naming function.
 */

#include "mpiimpl.h"
#include "mpimem.h"

#ifdef HAVE_WEAK_SYMBOLS

#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Comm_set_name = PMPI_Comm_set_name
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Comm_set_name  MPI_Comm_set_name
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Comm_set_name as PMPI_Comm_set_name
/* end of weak pragmas */
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif

static int MPIR_Name_put (struct MPIR_COMMUNICATOR *, char *);

/*@

  MPI_Comm_set_name - give a print name to the communicator

  Input Parameters:
+ com - Communicator to name (handle)
- name - Name for communicator

.N fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
@*/
int MPI_Comm_set_name( MPI_Comm com, char *name )
{
    int mpi_errno;
  struct MPIR_COMMUNICATOR *comm = MPIR_GET_COMM_PTR(com);

  MPIR_TEST_MPI_COMM(com,comm,comm,"MPI_COMM_SET_NAME");

  return MPIR_Name_put (comm, name);
}

/* The following needs to be in util */
static int MPIR_Name_put ( 
	struct MPIR_COMMUNICATOR *comm,
	char * name)
{
  /* Release any previous name */
  if (comm->comm_name)
    {
      FREE(comm->comm_name);
      comm->comm_name = 0;
    }

  /* Assign a new name */
  if (name)
    {
      char * new_string;

      MPIR_ALLOC(new_string,(char *)MALLOC(strlen(name)+1),comm,MPI_ERR_EXHAUSTED,
		 "MPI_COMM_SET_NAME" );
      strcpy(new_string, name);
      comm->comm_name = new_string;
    }

  /* And also name the collective communicator if it exists */
  if (comm->comm_coll != comm)
    {
      char collName[MPI_MAX_NAME_STRING+1];

      strncpy (collName,name,MPI_MAX_NAME_STRING);
      strncat (collName,"_collective", MPI_MAX_NAME_STRING-strlen(collName));
      MPIR_Name_put (comm->comm_coll, collName);
    }

  /* Bump the sequence number so that the debugger will notice something changed */
  ++MPIR_All_communicators.sequence_number;

  return MPI_SUCCESS;
}

