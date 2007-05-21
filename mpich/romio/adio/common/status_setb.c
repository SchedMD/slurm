/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "adio.h"
#include "mpi.h"

#if defined(MPICH2)
/* Not quite correct, but much closer for MPI2 */
/* TODO: still needs to handle partial datatypes and situations where the mpi
 * implementation fills status with something other than bytes (globus2 might
 * do this) */
int MPIR_Status_set_bytes(MPI_Status *status, MPI_Datatype datatype, 
			  int nbytes)
{
    ADIOI_UNREFERENCED_ARG(datatype);
    /* it's ok that ROMIO stores number-of-bytes in status, not 
     * count-of-copies, as long as MPI_GET_COUNT knows what to do */
    if (status != MPI_STATUS_IGNORE)
        MPI_Status_set_elements(status, MPI_BYTE, nbytes);
    return MPI_SUCCESS;
}
#elif defined(MPICH)

void MPID_Status_set_bytes(MPI_Status *status, int nbytes);
int MPIR_Status_set_bytes(MPI_Status *status, MPI_Datatype datatype, 
			  int nbytes);

int MPIR_Status_set_bytes(MPI_Status *status, MPI_Datatype datatype, 
			  int nbytes)
{
    if (status != MPI_STATUS_IGNORE)
        MPID_Status_set_bytes(status, nbytes);
    return MPI_SUCCESS;
}

#elif defined(MPILAM) || defined(MPISGI)
int MPIR_Status_set_bytes(MPI_Status *status, MPI_Datatype datatype,
		int nbytes)
{
  /* Bogusness to silence compiler warnings */
  if (datatype == MPI_DATATYPE_NULL);

  if (status != MPI_STATUS_IGNORE)
	  status->st_length = nbytes;
  return MPI_SUCCESS;
}
#endif
